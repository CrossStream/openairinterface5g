/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file pdcp_fifo.c
 * \brief pdcp interface with linux IP interface, have a look at http://man7.org/linux/man-pages/man7/netlink.7.html for netlink
 * \author  Navid Nikaein and Lionel GAUTHIER
 * \date 2009 - 2016
 * \version 0.5
 * \email navid.nikaein@eurecom.fr
 * \warning This component can be runned only in user-space
 * @ingroup pdcp
 */

#define PDCP_FIFO_C
#define PDCP_DEBUG 1
//#define DEBUG_PDCP_FIFO_FLUSH_SDU

extern int otg_enabled;

#include "pdcp.h"
#include "pdcp_primitives.h"

#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define rtf_put write
#define rtf_get read

#include "../MAC/mac_extern.h"
#include "RRC/L2_INTERFACE/openair_rrc_L2_interface.h"
#include "NETWORK_DRIVER/LITE/constant.h"
//#include "SIMULATION/ETH_TRANSPORT/extern.h"
#include "UTIL/OCG/OCG.h"
#include "UTIL/OCG/OCG_extern.h"
#include "common/utils/LOG/log.h"
#include "UTIL/OTG/otg_tx.h"
#include "UTIL/FIFO/pad_list.h"
#include "common/utils/LOG/vcd_signal_dumper.h"
#include "platform_constants.h"
#include "msc.h"
#include "pdcp.h"

#include "assertions.h"



extern Packet_OTG_List_t *otg_pdcp_buffer;

#  include "gtpv1u_eNB_task.h"
#  include "gtpv1u_eNB_defs.h"


extern int gtpv1u_new_data_req( uint8_t  enb_module_idP, rnti_t   ue_rntiP, uint8_t  rab_idP, uint8_t *buffer_pP, uint32_t buf_lenP, uint32_t buf_offsetP);

/* Prevent de-queueing the same PDCP SDU from the queue twice
 * by multiple threads. This has happened in TDD when thread-odd
 * is flushing a PDCP SDU after UE_RX() processing; whereas
 * thread-even is at a special-subframe, skips the UE_RX() process
 * and goes straight to the PDCP SDU flushing. The 2nd flushing
 * dequeues the same SDU again causing unexpected behavior.
 *
 * comment out the MACRO below to disable this protection
 */
#define PDCP_SDU_FLUSH_LOCK

#ifdef PDCP_SDU_FLUSH_LOCK
static pthread_mutex_t mtex = PTHREAD_MUTEX_INITIALIZER;
#endif

pdcp_data_req_header_t pdcp_read_header_g;

//-----------------------------------------------------------------------------
int pdcp_fifo_flush_sdus(const protocol_ctxt_t* const  ctxt_pP)
{
   //-----------------------------------------------------------------------------

    int ret = 0;
 #ifdef DEBUG_PDCP_FIFO_FLUSH_SDU
#define THREAD_NAME_LEN 16
   static char threadname[THREAD_NAME_LEN];
   ret = pthread_getname_np(pthread_self(), threadname, THREAD_NAME_LEN);
   if (ret != 0)
   {
      perror("pthread_getname_np : ");
      exit_fun("Error getting thread name");
   }
#undef THREAD_NAME_LEN
#endif

#ifdef PDCP_SDU_FLUSH_LOCK
   ret = pthread_mutex_trylock(&mtex);
   if (ret == EBUSY) {
#ifdef DEBUG_PDCP_FIFO_FLUSH_SDU
      LOG_W(PDCP, "[%s] at SFN/SF=%d/%d wait for PDCP FIFO to be unlocked\n",
            threadname, ctxt_pP->frame, ctxt_pP->subframe);
#endif
      if (pthread_mutex_lock(&mtex)) {
         exit_fun("PDCP_SDU_FLUSH_LOCK lock error!");
      }
#ifdef DEBUG_PDCP_FIFO_FLUSH_SDU
      LOG_I(PDCP, "[%s] at SFN/SF=%d/%d PDCP FIFO is unlocked\n",
            threadname, ctxt_pP->frame, ctxt_pP->subframe);
#endif
   } else if (ret != 0) {
      exit_fun("PDCP_SDU_FLUSH_LOCK trylock error!");
   }

#endif

   mem_block_t     *sdu_p            = list_get_head (&pdcp_sdu_list);
   int              bytes_wrote      = 0;
   int              pdcp_nb_sdu_sent = 0;
   uint8_t          cont             = 1;


   VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_FIFO_FLUSH, 1 );
   while (sdu_p && cont) {

#ifdef DEBUG_PDCP_FIFO_FLUSH_SDU
      LOG_D(PDCP, "[%s] SFN/SF=%d/%d inst=%d size=%d\n",
            threadname, ctxt_pP->frame, ctxt_pP->subframe,
            ((pdcp_data_ind_header_t*) sdu_p->data)->inst,
            ((pdcp_data_ind_header_t *) sdu_p->data)->data_size);
#else
      ((pdcp_data_ind_header_t *)(sdu_p->data))->inst = 0;
#endif

      if (LINK_ENB_PDCP_TO_GTPV1U) {

  	if (ctxt_pP->enb_flag) {
  	   AssertFatal(0, "Now execution should not go here");
  	   LOG_D(PDCP,"Sending to GTPV1U %d bytes\n", ((pdcp_data_ind_header_t *)(sdu_p->data))->data_size);
  	   gtpv1u_new_data_req(
  		 ctxt_pP->module_id, //gtpv1u_data_t *gtpv1u_data_p,
  		 ctxt_pP->rnti,//rb_id/maxDRB, TO DO UE ID
  		 ((pdcp_data_ind_header_t *)(sdu_p->data))->rb_id + 4,
  		 &(((uint8_t *) sdu_p->data)[sizeof (pdcp_data_ind_header_t)]),
  		 ((pdcp_data_ind_header_t *)(sdu_p->data))->data_size,
  		 0);

  	   list_remove_head (&pdcp_sdu_list);
  	   free_mem_block (sdu_p, __func__);
  	   cont = 1;
  	   pdcp_nb_sdu_sent += 1;
  	   sdu_p = list_get_head (&pdcp_sdu_list);
  	   LOG_D(OTG,"After  GTPV1U\n");
  	   continue; // loop again
  	}
      } /* defined(LINK_ENB_PDCP_TO_GTPV1U) */
#ifdef PDCP_DEBUG
      LOG_D(PDCP, "PDCP->IP TTI %d INST %d: Preparing %d Bytes of data from rab %d to Nas_mesh\n",
            ctxt_pP->frame, ((pdcp_data_ind_header_t *)(sdu_p->data))->inst,
            ((pdcp_data_ind_header_t *)(sdu_p->data))->data_size, ((pdcp_data_ind_header_t *)(sdu_p->data))->rb_id);
#endif //PDCP_DEBUG
      cont = 0;

//TTN - for D2D (PC5S)
#if (RRC_VERSION >= MAKE_VERSION(14, 0, 0))
      sidelink_pc5s_element *sl_pc5s_msg_recv = NULL;
      char send_buf[BUFSIZE];
      int rb_id = ((pdcp_data_ind_header_t *)(sdu_p->data))->rb_id;

      if (rb_id == 10) { //hardcoded for PC5-Signaling
      //if ((rb_id == 28) | (rb_id == 29) | (rb_id == 30))

#ifdef PDCP_DEBUG
         sl_pc5s_msg_recv = calloc(1, sizeof(sidelink_pc5s_element));
         memcpy((void*)sl_pc5s_msg_recv, (void*)(sdu_p->data+sizeof(pdcp_data_ind_header_t)), sizeof(sidelink_pc5s_element));
         LOG_D(PDCP,"Received PC5S message, header traffic_type: %d)\n", sl_pc5s_msg_recv->pc5s_header.traffic_type);
         LOG_D(PDCP,"Received PC5S message, header rb_id: %d)\n", sl_pc5s_msg_recv->pc5s_header.rb_id);
         LOG_D(PDCP,"Received PC5S message, header data_size: %d)\n", sl_pc5s_msg_recv->pc5s_header.data_size);
         LOG_D(PDCP,"Received PC5S message, header inst: %d)\n", sl_pc5s_msg_recv->pc5s_header.inst);
         LOG_D(PDCP,"Received PC5-S message, sourceL2Id: 0x%08x\n)\n", sl_pc5s_msg_recv->pc5s_header.sourceL2Id);
         LOG_D(PDCP,"Received PC5-S message, destinationL1Id: 0x%08x\n)\n", sl_pc5s_msg_recv->pc5s_header.destinationL2Id);
         free(sl_pc5s_msg_recv);
#endif
         memset(send_buf, 0, BUFSIZE);
         memcpy((void *)send_buf, (void*)(sdu_p->data+sizeof(pdcp_data_ind_header_t)), sizeof(sidelink_pc5s_element));

         int prose_addr_len = sizeof(prose_pdcp_addr);
         int n = sendto(pdcp_pc5_sockfd, (char *)send_buf, sizeof(sidelink_pc5s_element), 0, (struct sockaddr *)&prose_pdcp_addr, prose_addr_len);
         if (n < 0) {
            LOG_E(PDCP, "ERROR: Failed to send to ProSe App\n");
            exit(EXIT_FAILURE);
         }
      }
#endif

      if (!pdcp_output_sdu_bytes_to_write) {
         if (!pdcp_output_header_bytes_to_write) {
            pdcp_output_header_bytes_to_write = sizeof (pdcp_data_ind_header_t);
         }


         bytes_wrote = pdcp_output_header_bytes_to_write;

#ifdef PDCP_DEBUG
         LOG_D(PDCP, "Frame %d Sent %d Bytes of header to Nas_mesh\n",
               ctxt_pP->frame,
               bytes_wrote);
#endif //PDCP_DEBUG

         if (bytes_wrote > 0) {
            pdcp_output_header_bytes_to_write = pdcp_output_header_bytes_to_write - bytes_wrote;

            if (!pdcp_output_header_bytes_to_write) { // continue with sdu
               pdcp_output_sdu_bytes_to_write = ((pdcp_data_ind_header_t *) sdu_p->data)->data_size;
               AssertFatal(pdcp_output_sdu_bytes_to_write >= 0, "invalid data_size!");


          bytes_wrote= pdcp_output_sdu_bytes_to_write;

#ifdef PDCP_DEBUG
               LOG_D(PDCP, "PDCP->IP Frame %d INST %d: Sent %d Bytes of data from rab %d to higher layers\n",
                     ctxt_pP->frame,
                     ((pdcp_data_ind_header_t *)(sdu_p->data))->inst,
                     bytes_wrote,
                     ((pdcp_data_ind_header_t *)(sdu_p->data))->rb_id);
#endif //PDCP_DEBUG

               if (bytes_wrote > 0) {
                  pdcp_output_sdu_bytes_to_write -= bytes_wrote;

                  if (!pdcp_output_sdu_bytes_to_write) { // OK finish with this SDU
                     // LOG_D(PDCP, "rb sent a sdu qos_sap %d\n", sapiP);
                     LOG_D(PDCP,
                           "[FRAME %05d][xxx][PDCP][MOD xx/xx][RB %u][--- PDCP_DATA_IND / %d Bytes --->][IP][INSTANCE %u][RB %u]\n",
                           ctxt_pP->frame,
                           ((pdcp_data_ind_header_t *)(sdu_p->data))->rb_id,
                           ((pdcp_data_ind_header_t *)(sdu_p->data))->data_size,
                           ((pdcp_data_ind_header_t *)(sdu_p->data))->inst,
                           ((pdcp_data_ind_header_t *)(sdu_p->data))->rb_id);

                     list_remove_head (&pdcp_sdu_list);
                     free_mem_block (sdu_p, __func__);
                     cont = 1;
                     pdcp_nb_sdu_sent += 1;
                     sdu_p = list_get_head (&pdcp_sdu_list);
                  } else {
                     LOG_D(PDCP, "1 skip free_mem_block: pdcp_output_sdu_bytes_to_write = %d\n", pdcp_output_sdu_bytes_to_write);
                     AssertFatal(pdcp_output_sdu_bytes_to_write > 0, "pdcp_output_sdu_bytes_to_write cannot be negative!");
                  }
               } else {
                  LOG_W(PDCP, "2: RADIO->IP SEND SDU CONGESTION!\n");
               }
            } else {
               LOG_W(PDCP, "3: RADIO->IP SEND SDU CONGESTION!\n");
            }
         } else {
            LOG_D(PDCP, "4 skip free_mem_block: bytes_wrote = %d\n", bytes_wrote);
         }
      } else {
         // continue writing sdu
         bytes_wrote = pdcp_output_sdu_bytes_to_write;
         LOG_D(PDCP, "THINH 2 bytes_wrote = %d\n", bytes_wrote);

         if (bytes_wrote > 0) {
            pdcp_output_sdu_bytes_to_write -= bytes_wrote;

            if (!pdcp_output_sdu_bytes_to_write) {     // OK finish with this SDU
               //PRINT_RB_SEND_OUTPUT_SDU ("[PDCP] RADIO->IP SEND SDU\n");
               list_remove_head (&pdcp_sdu_list);
               free_mem_block (sdu_p, __func__);
               cont = 1;
               pdcp_nb_sdu_sent += 1;
               sdu_p = list_get_head (&pdcp_sdu_list);
               // LOG_D(PDCP, "rb sent a sdu from rab\n");
            } else {
               LOG_D(PDCP, "5 skip free_mem_block: pdcp_output_sdu_bytes_to_write = %d\n", pdcp_output_sdu_bytes_to_write);
            }
         } else {
            LOG_D(PDCP, "6 skip free_mem_block: bytes_wrote = %d\n", bytes_wrote);
         }
      }
   }
   VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_FIFO_FLUSH, 0 );

#ifdef PDCP_SDU_FLUSH_LOCK
   if (pthread_mutex_unlock(&mtex)) exit_fun("PDCP_SDU_FLUSH_LOCK unlock error!");
#endif

   return pdcp_nb_sdu_sent;
}

//-----------------------------------------------------------------------------
int pdcp_fifo_read_input_sdus (const protocol_ctxt_t* const  ctxt_pP)
{
#ifdef UE_NAS_USE_TUN
  protocol_ctxt_t ctxt = *ctxt_pP;
  hash_key_t key = HASHTABLE_NOT_A_KEY_VALUE;
  hashtable_rc_t h_rc;
  pdcp_t* pdcp_p = NULL;
  int len=0;
  rb_id_t rab_id = DEFAULT_RAB_ID;

  do {
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_FIFO_READ, 1 );
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_FIFO_READ_BUFFER, 1 );
    len = read(nas_sock_fd[ctxt_pP->module_id], &nl_rx_buf, NL_MAX_PAYLOAD);
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_FIFO_READ_BUFFER, 0 );

    if (len<=0) continue;
    LOG_D(PDCP, "PDCP_COLL_KEY_DEFAULT_DRB_VALUE(module_id=%d, rnti=%x, enb_flag=%d)\n",
          ctxt.module_id, ctxt.rnti, ctxt.enb_flag);
    key = PDCP_COLL_KEY_DEFAULT_DRB_VALUE(ctxt.module_id, ctxt.rnti, ctxt.enb_flag);
    h_rc = hashtable_get(pdcp_coll_p, key, (void**)&pdcp_p);
    if (h_rc == HASH_TABLE_OK) {
      LOG_D(PDCP, "[FRAME %5u][UE][NETLINK][IP->PDCP] INST %d: Received socket with length %d on Rab %d \n",
            ctxt.frame, ctxt.instance, len, rab_id);

      LOG_D(PDCP, "[FRAME %5u][UE][IP][INSTANCE %u][RB %u][--- PDCP_DATA_REQ / %d Bytes --->][PDCP][MOD %u][UE %u][RB %u]\n",
            ctxt.frame, ctxt.instance, rab_id, len, ctxt.module_id,
            ctxt.rnti, rab_id);
      MSC_LOG_RX_MESSAGE((ctxt_pP->enb_flag == ENB_FLAG_YES) ? MSC_PDCP_ENB:MSC_PDCP_UE,
                         (ctxt_pP->enb_flag == ENB_FLAG_YES) ? MSC_IP_ENB:MSC_IP_UE,
                         NULL, 0,
                         MSC_AS_TIME_FMT" DATA-REQ inst %u rb %u rab %u size %u",
                         MSC_AS_TIME_ARGS(ctxt_pP),
                         ctxt.instance, rab_id, rab_id, len);

      pdcp_data_req(&ctxt, SRB_FLAG_NO, rab_id, RLC_MUI_UNDEFINED,
                    RLC_SDU_CONFIRM_NO, len, (unsigned char *)nl_rx_buf,
                    PDCP_TRANSMISSION_MODE_DATA
#if (RRC_VERSION >= MAKE_VERSION(14, 0, 0))
                    , NULL, NULL
#endif
                   );
    } else {
      MSC_LOG_RX_DISCARDED_MESSAGE(
      (ctxt_pP->enb_flag == ENB_FLAG_YES) ? MSC_PDCP_ENB:MSC_PDCP_UE,
      (ctxt_pP->enb_flag == ENB_FLAG_YES) ? MSC_IP_ENB:MSC_IP_UE,
      NULL,
      0,
      MSC_AS_TIME_FMT" DATA-REQ inst %u rb %u rab %u size %u",
      MSC_AS_TIME_ARGS(ctxt_pP),
      ctxt.instance, rab_id, rab_id, len);
      LOG_D(PDCP,
            "[FRAME %5u][UE][IP][INSTANCE %u][RB %u][--- PDCP_DATA_REQ / %d Bytes ---X][PDCP][MOD %u][UE %u][RB %u] NON INSTANCIATED INSTANCE key 0x%"PRIx64", DROPPED\n",
            ctxt.frame, ctxt.instance, rab_id, len, ctxt.module_id,
            ctxt.rnti, rab_id, key);
    }
  } while (len > 0);
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_FIFO_READ, 0 );
  return len;

#else /* UE_NAS_USE_TUN */


#endif /* #else UE_NAS_USE_TUN */
 return 0;
}


void pdcp_fifo_read_input_sdus_from_otg (const protocol_ctxt_t* const  ctxt_pP) {


  module_id_t          dst_id; // dst for otg
  protocol_ctxt_t      ctxt;
  // we need to add conditions to avoid transmitting data when the UE is not RRC connected.
  if ((otg_enabled==1) && (ctxt_pP->enb_flag == ENB_FLAG_YES)) { // generate DL traffic

    PROTOCOL_CTXT_SET_BY_MODULE_ID(
      &ctxt,
      ctxt_pP->module_id,
      ctxt_pP->enb_flag,
      NOT_A_RNTI,
      ctxt_pP->frame,
      ctxt_pP->subframe,
      ctxt_pP->module_id);

    for (dst_id = 0; dst_id<MAX_MOBILES_PER_ENB; dst_id++) {
      ctxt.rnti = oai_emulation.info.eNB_ue_module_id_to_rnti[ctxt.module_id][dst_id];
    }
  }
}

//TTN for D2D (PC5S)
#if (RRC_VERSION >= MAKE_VERSION(14, 0, 0))

void
pdcp_pc5_socket_init() {
	//pthread_attr_t     attr;
   //struct sched_param sched_param;
   int optval; // flag value for setsockopt
   //int n; // message byte size

   //create PDCP socket
   pdcp_pc5_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
   if (pdcp_pc5_sockfd < 0){
      LOG_E(PDCP,"[pdcp_pc5_socket_init] Error opening socket %d (%d:%s)\n",pdcp_pc5_sockfd,errno, strerror(errno));
      exit(EXIT_FAILURE);
   }

   optval = 1;
   setsockopt(pdcp_pc5_sockfd, SOL_SOCKET, SO_REUSEADDR,
         (const void *)&optval , sizeof(int));

   fcntl(pdcp_pc5_sockfd,F_SETFL,O_NONBLOCK);

   bzero((char *) &pdcp_sin, sizeof(pdcp_sin));
   pdcp_sin.sin_family = AF_INET;
   pdcp_sin.sin_addr.s_addr = htonl(INADDR_ANY);
   pdcp_sin.sin_port = htons(PDCP_SOCKET_PORT_NO);
   // associate the parent socket with a port
   if (bind(pdcp_pc5_sockfd, (struct sockaddr *) &pdcp_sin,
         sizeof(pdcp_sin)) < 0) {
      LOG_E(PDCP,"[pdcp_pc5_socket_init] ERROR: Failed on binding the socket\n");
      exit(1);
   }

}

#endif
