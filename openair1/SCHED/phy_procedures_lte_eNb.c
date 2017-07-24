/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.0  (the "License"); you may not use this file
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

/*! \file phy_procedures_lte_eNB.c
 * \brief Implementation of eNB procedures from 36.213 LTE specifications
 * \author R. Knopp, F. Kaltenberger, N. Nikaein
 * \date 2011
 * \version 0.1
 * \company Eurecom
 * \email: knopp@eurecom.fr,florian.kaltenberger@eurecom.fr,navid.nikaein@eurecom.fr
 * \note
 * \warning
 */

#include "PHY/defs.h"
#include "PHY/extern.h"
#include "SCHED/defs.h"
#include "SCHED/extern.h"

#include "PHY/LTE_TRANSPORT/if4_tools.h"
#include "PHY/LTE_TRANSPORT/if5_tools.h"

#ifdef EMOS
#include "SCHED/phy_procedures_emos.h"
#endif

//#define DEBUG_PHY_PROC (Already defined in cmake)
//#define DEBUG_ULSCH

#include "LAYER2/MAC/extern.h"
#include "LAYER2/MAC/defs.h"
#include "UTIL/LOG/log.h"
#include "UTIL/LOG/vcd_signal_dumper.h"

#include "T.h"

#include "assertions.h"
#include "msc.h"

#include <time.h>

#if defined(ENABLE_ITTI)
#   include "intertask_interface.h"
#endif

//#define DIAG_PHY

#define NS_PER_SLOT 500000

#define PUCCH 1

//DLSH thread
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <string.h>
#include <semaphore.h>
#include <sys/sysinfo.h>
#include <sys/signal.h>
#include <assert.h>
#include <sched.h>
#include <pthread.h>

//TEST
#define NUM_THREAD 4


void exit_fun(const char* s);

extern int exit_openair;

// Fix per CC openair rf/if device update
// extern openair0_device openair0;

unsigned char dlsch_input_buffer[2700] __attribute__ ((aligned(32)));
int eNB_sync_buffer0[640*6] __attribute__ ((aligned(32)));
int eNB_sync_buffer1[640*6] __attribute__ ((aligned(32)));
int *eNB_sync_buffer[2] = {eNB_sync_buffer0, eNB_sync_buffer1};

extern uint16_t hundred_times_log10_NPRB[100];

unsigned int max_peak_val;
int max_sync_pos;

//DCI_ALLOC_t dci_alloc[8];

#ifdef EMOS
fifo_dump_emos_eNB emos_dump_eNB;
#endif

#if defined(SMBV) 
extern const char smbv_fname[];
extern unsigned short config_frames[4];
extern uint8_t smbv_frame_cnt;
#endif

#ifdef DIAG_PHY
extern int rx_sig_fifo;
#endif

uint8_t is_SR_subframe(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,uint8_t UE_id)
{

  const int subframe = proc->subframe_rx;
  const int frame = proc->frame_rx;

  LOG_D(PHY,"[eNB %d][SR %x] Frame %d subframe %d Checking for SR TXOp(sr_ConfigIndex %d)\n",
        eNB->Mod_id,eNB->ulsch[UE_id]->rnti,frame,subframe,
        eNB->scheduling_request_config[UE_id].sr_ConfigIndex);

  if (eNB->scheduling_request_config[UE_id].sr_ConfigIndex <= 4) {        // 5 ms SR period
    if ((subframe%5) == eNB->scheduling_request_config[UE_id].sr_ConfigIndex)
      return(1);
  } else if (eNB->scheduling_request_config[UE_id].sr_ConfigIndex <= 14) { // 10 ms SR period
    if (subframe==(eNB->scheduling_request_config[UE_id].sr_ConfigIndex-5))
      return(1);
  } else if (eNB->scheduling_request_config[UE_id].sr_ConfigIndex <= 34) { // 20 ms SR period
    if ((10*(frame&1)+subframe) == (eNB->scheduling_request_config[UE_id].sr_ConfigIndex-15))
      return(1);
  } else if (eNB->scheduling_request_config[UE_id].sr_ConfigIndex <= 74) { // 40 ms SR period
    if ((10*(frame&3)+subframe) == (eNB->scheduling_request_config[UE_id].sr_ConfigIndex-35))
      return(1);
  } else if (eNB->scheduling_request_config[UE_id].sr_ConfigIndex <= 154) { // 80 ms SR period
    if ((10*(frame&7)+subframe) == (eNB->scheduling_request_config[UE_id].sr_ConfigIndex-75))
      return(1);
  }

  return(0);
}


int32_t add_ue(int16_t rnti, PHY_VARS_eNB *eNB)
{
  uint8_t i;


  LOG_D(PHY,"[eNB %d/%d] Adding UE with rnti %x\n",
        eNB->Mod_id,
        eNB->CC_id,
        (uint16_t)rnti);

  for (i=0; i<NUMBER_OF_UE_MAX; i++) {
    if ((eNB->dlsch[i]==NULL) || (eNB->ulsch[i]==NULL)) {
      MSC_LOG_EVENT(MSC_PHY_ENB, "0 Failed add ue %"PRIx16" (ENOMEM)", rnti);
      LOG_E(PHY,"Can't add UE, not enough memory allocated\n");
      return(-1);
    } else {
      if (eNB->UE_stats[i].crnti==0) {
        MSC_LOG_EVENT(MSC_PHY_ENB, "0 Add ue %"PRIx16" ", rnti);
        LOG_D(PHY,"UE_id %d associated with rnti %x\n",i, (uint16_t)rnti);
        eNB->dlsch[i][0]->rnti = rnti;
        eNB->ulsch[i]->rnti = rnti;
        eNB->UE_stats[i].crnti = rnti;

	eNB->UE_stats[i].Po_PUCCH1_below = 0;
	eNB->UE_stats[i].Po_PUCCH1_above = (int32_t)pow(10.0,.1*(eNB->frame_parms.ul_power_control_config_common.p0_NominalPUCCH+eNB->rx_total_gain_dB));
	eNB->UE_stats[i].Po_PUCCH        = (int32_t)pow(10.0,.1*(eNB->frame_parms.ul_power_control_config_common.p0_NominalPUCCH+eNB->rx_total_gain_dB));
	LOG_D(PHY,"Initializing Po_PUCCH: p0_NominalPUCCH %d, gain %d => %d\n",
	      eNB->frame_parms.ul_power_control_config_common.p0_NominalPUCCH,
	      eNB->rx_total_gain_dB,
	      eNB->UE_stats[i].Po_PUCCH);
  
        return(i);
      }
    }
  }
  return(-1);
}

int mac_phy_remove_ue(module_id_t Mod_idP,rnti_t rntiP) {
  uint8_t i;
  int CC_id;
  PHY_VARS_eNB *eNB;

  for (CC_id=0;CC_id<MAX_NUM_CCs;CC_id++) {
    eNB = PHY_vars_eNB_g[Mod_idP][CC_id];
    for (i=0; i<NUMBER_OF_UE_MAX; i++) {
      if ((eNB->dlsch[i]==NULL) || (eNB->ulsch[i]==NULL)) {
	MSC_LOG_EVENT(MSC_PHY_ENB, "0 Failed remove ue %"PRIx16" (ENOMEM)", rnti);
	LOG_E(PHY,"Can't remove UE, not enough memory allocated\n");
	return(-1);
      } else {
	if (eNB->UE_stats[i].crnti==rntiP) {
	  MSC_LOG_EVENT(MSC_PHY_ENB, "0 Removed ue %"PRIx16" ", rntiP);

	  LOG_D(PHY,"eNB %d removing UE %d with rnti %x\n",eNB->Mod_id,i,rntiP);

	  //LOG_D(PHY,("[PHY] UE_id %d\n",i);
	  clean_eNb_dlsch(eNB->dlsch[i][0]);
	  clean_eNb_ulsch(eNB->ulsch[i]);
	  //eNB->UE_stats[i].crnti = 0;
	  memset(&eNB->UE_stats[i],0,sizeof(LTE_eNB_UE_stats));
	  //  mac_exit_wrapper("Removing UE");
	  

	  return(i);
	}
      }
    }
  }
  MSC_LOG_EVENT(MSC_PHY_ENB, "0 Failed remove ue %"PRIx16" (not found)", rntiP);
  return(-1);
}

int8_t find_next_ue_index(PHY_VARS_eNB *eNB)
{
  uint8_t i;

  for (i=0; i<NUMBER_OF_UE_MAX; i++) {
    if (eNB->UE_stats[i].crnti==0) {
      /*if ((eNB->dlsch[i]) &&
	(eNB->dlsch[i][0]) &&
	(eNB->dlsch[i][0]->rnti==0))*/
      LOG_D(PHY,"Next free UE id is %d\n",i);
      return(i);
    }
  }

  return(-1);
}

int get_ue_active_harq_pid(const uint8_t Mod_id,const uint8_t CC_id,const uint16_t rnti, const int frame, const uint8_t subframe,uint8_t *harq_pid,uint8_t *round,const uint8_t ul_flag)
{
  LTE_eNB_DLSCH_t *DLSCH_ptr;
  LTE_eNB_ULSCH_t *ULSCH_ptr;
  uint8_t ulsch_subframe,ulsch_frame;
  int i;
  int8_t UE_id = find_ue(rnti,PHY_vars_eNB_g[Mod_id][CC_id]);

  if (UE_id==-1) {
    LOG_D(PHY,"Cannot find UE with rnti %x (Mod_id %d, CC_id %d)\n",rnti, Mod_id, CC_id);
    *round=0;
    return(-1);
  }

  if (ul_flag == 0)  {// this is a DL request
    DLSCH_ptr = PHY_vars_eNB_g[Mod_id][CC_id]->dlsch[(uint32_t)UE_id][0];

    /* let's go synchronous for the moment - maybe we can change at some point */
    i = (frame * 10 + subframe) % 8;

    if (DLSCH_ptr->harq_processes[i]->status == ACTIVE) {
      *harq_pid = i;
      *round = DLSCH_ptr->harq_processes[i]->round;
    } else if (DLSCH_ptr->harq_processes[i]->status == SCH_IDLE) {
      *harq_pid = i;
      *round = 0;
    } else {
      printf("%s:%d: bad state for harq process - PLEASE REPORT!!\n", __FILE__, __LINE__);
      abort();
    }
  } else { // This is a UL request

    ULSCH_ptr = PHY_vars_eNB_g[Mod_id][CC_id]->ulsch[(uint32_t)UE_id];
    ulsch_subframe = pdcch_alloc2ul_subframe(&PHY_vars_eNB_g[Mod_id][CC_id]->frame_parms,subframe);
    ulsch_frame    = pdcch_alloc2ul_frame(&PHY_vars_eNB_g[Mod_id][CC_id]->frame_parms,frame,subframe);
    // Note this is for TDD configuration 3,4,5 only
    *harq_pid = subframe2harq_pid(&PHY_vars_eNB_g[Mod_id][CC_id]->frame_parms,
                                  ulsch_frame,
                                  ulsch_subframe);
    *round    = ULSCH_ptr->harq_processes[*harq_pid]->round;
    LOG_T(PHY,"[eNB %d][PUSCH %d] Frame %d subframe %d Checking HARQ, round %d\n",Mod_id,*harq_pid,frame,subframe,*round);
  }

  return(0);
}

int16_t get_target_pusch_rx_power(const module_id_t module_idP, const uint8_t CC_id)
{
  return PHY_vars_eNB_g[module_idP][CC_id]->frame_parms.ul_power_control_config_common.p0_NominalPUSCH;
}

int16_t get_target_pucch_rx_power(const module_id_t module_idP, const uint8_t CC_id)
{
  return PHY_vars_eNB_g[module_idP][CC_id]->frame_parms.ul_power_control_config_common.p0_NominalPUCCH;
}

#ifdef EMOS
void phy_procedures_emos_eNB_TX(unsigned char subframe, PHY_VARS_eNB *eNB)
{

}
#endif



void phy_procedures_eNB_S_RX(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,relaying_type_t r_type)
{
  UNUSED(r_type);
  int subframe = proc->subframe_rx;

#ifdef DEBUG_PHY_PROC
  LOG_D(PHY,"[eNB %d] Frame %d: Doing phy_procedures_eNB_S_RX(%d)\n", eNB->Mod_id,proc->frame_rx, subframe);
#endif


  if (eNB->abstraction_flag == 0) {
    lte_eNB_I0_measurements(eNB,
			    subframe,
                            0,
                            eNB->first_run_I0_measurements);
  }

#ifdef PHY_ABSTRACTION
  else {
    lte_eNB_I0_measurements_emul(eNB,
                                 0);
  }

#endif


}



#ifdef EMOS
void phy_procedures_emos_eNB_RX(unsigned char subframe,PHY_VARS_eNB *eNB)
{

  uint8_t aa;
  uint16_t last_subframe_emos;
  uint16_t pilot_pos1 = 3 - eNB->frame_parms.Ncp, pilot_pos2 = 10 - 2*eNB->frame_parms.Ncp;
  uint32_t bytes;

  last_subframe_emos=0;




#ifdef EMOS_CHANNEL

  //if (last_slot%2==1) // this is for all UL subframes
  if (subframe==3)
    for (aa=0; aa<eNB->frame_parms.nb_antennas_rx; aa++) {
      memcpy(&emos_dump_eNB.channel[aa][last_subframe_emos*2*eNB->frame_parms.N_RB_UL*12],
             &eNB->pusch_vars[0]->drs_ch_estimates[0][aa][eNB->frame_parms.N_RB_UL*12*pilot_pos1],
             eNB->frame_parms.N_RB_UL*12*sizeof(int));
      memcpy(&emos_dump_eNB.channel[aa][(last_subframe_emos*2+1)*eNB->frame_parms.N_RB_UL*12],
             &eNB->pusch_vars[0]->drs_ch_estimates[0][aa][eNB->frame_parms.N_RB_UL*12*pilot_pos2],
             eNB->frame_parms.N_RB_UL*12*sizeof(int));
    }

#endif

  if (subframe==4) {
    emos_dump_eNB.timestamp = rt_get_time_ns();
    emos_dump_eNB.frame_tx = eNB->proc[subframe].frame_rx;
    emos_dump_eNB.rx_total_gain_dB = eNB->rx_total_gain_dB;
    emos_dump_eNB.mimo_mode = eNB->transmission_mode[0];
    memcpy(&emos_dump_eNB.measurements,
           &eNB->measurements[0],
           sizeof(PHY_MEASUREMENTS_eNB));
    memcpy(&emos_dump_eNB.UE_stats[0],&eNB->UE_stats[0],NUMBER_OF_UE_MAX*sizeof(LTE_eNB_UE_stats));

    bytes = rtf_put(CHANSOUNDER_FIFO_MINOR, &emos_dump_eNB, sizeof(fifo_dump_emos_eNB));

    //bytes = rtf_put(CHANSOUNDER_FIFO_MINOR, "test", sizeof("test"));
    if (bytes!=sizeof(fifo_dump_emos_eNB)) {
      LOG_W(PHY,"[eNB %d] Frame %d, subframe %d, Problem writing EMOS data to FIFO (bytes=%d, size=%d)\n",
            eNB->Mod_id,eNB->proc[(subframe+1)%10].frame_rx, subframe,bytes,sizeof(fifo_dump_emos_eNB));
    } else {
      if (eNB->proc[(subframe+1)%10].frame_tx%100==0) {
        LOG_I(PHY,"[eNB %d] Frame %d (%d), subframe %d, Writing %d bytes EMOS data to FIFO\n",
              eNB->Mod_id,eNB->proc[(subframe+1)%10].frame_rx, ((fifo_dump_emos_eNB*)&emos_dump_eNB)->frame_tx, subframe, bytes);
      }
    }
  }
}
#endif


#define AMP_OVER_SQRT2 ((AMP*ONE_OVER_SQRT2_Q15)>>15)
#define AMP_OVER_2 (AMP>>1)
int QPSK[4]= {AMP_OVER_SQRT2|(AMP_OVER_SQRT2<<16),AMP_OVER_SQRT2|((65536-AMP_OVER_SQRT2)<<16),((65536-AMP_OVER_SQRT2)<<16)|AMP_OVER_SQRT2,((65536-AMP_OVER_SQRT2)<<16)|(65536-AMP_OVER_SQRT2)};
int QPSK2[4]= {AMP_OVER_2|(AMP_OVER_2<<16),AMP_OVER_2|((65536-AMP_OVER_2)<<16),((65536-AMP_OVER_2)<<16)|AMP_OVER_2,((65536-AMP_OVER_2)<<16)|(65536-AMP_OVER_2)};



unsigned int taus(void);
DCI_PDU DCI_pdu_tmp;


void pmch_procedures(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,PHY_VARS_RN *rn,relaying_type_t r_type) {


#ifdef Rel10
  MCH_PDU *mch_pduP;
  MCH_PDU  mch_pdu;
  //  uint8_t sync_area=255;
#endif

  int subframe = proc->subframe_tx;

  if (eNB->abstraction_flag==0) {
    // This is DL-Cell spec pilots in Control region
    generate_pilots_slot(eNB,
			 eNB->common_vars.txdataF[0],
			 AMP,
			 subframe<<1,1);
  }
  
#ifdef Rel10
  // if mcch is active, send regardless of the node type: eNB or RN
  // when mcch is active, MAC sched does not allow MCCH and MTCH multiplexing
  mch_pduP = mac_xface->get_mch_sdu(eNB->Mod_id,
				    eNB->CC_id,
				    proc->frame_tx,
				    subframe);
  
  switch (r_type) {
  case no_relay:
    if ((mch_pduP->Pdu_size > 0) && (mch_pduP->sync_area == 0)) // TEST: only transmit mcch for sync area 0
      LOG_I(PHY,"[eNB%"PRIu8"] Frame %d subframe %d : Got MCH pdu for MBSFN (MCS %"PRIu8", TBS %d) \n",
	    eNB->Mod_id,proc->frame_tx,subframe,mch_pduP->mcs,
	    eNB->dlsch_MCH->harq_processes[0]->TBS>>3);
    else {
      LOG_D(PHY,"[DeNB %"PRIu8"] Frame %d subframe %d : Do not transmit MCH pdu for MBSFN sync area %"PRIu8" (%s)\n",
	    eNB->Mod_id,proc->frame_tx,subframe,mch_pduP->sync_area,
	    (mch_pduP->Pdu_size == 0)? "Empty MCH PDU":"Let RN transmit for the moment");
      mch_pduP = NULL;
    }
    
    break;
    
  case multicast_relay:
    if ((mch_pduP->Pdu_size > 0) && ((mch_pduP->mcch_active == 1) || mch_pduP->msi_active==1)) {
      LOG_I(PHY,"[RN %"PRIu8"] Frame %d subframe %d: Got the MCH PDU for MBSFN  sync area %"PRIu8" (MCS %"PRIu8", TBS %"PRIu16")\n",
	    rn->Mod_id,rn->frame, subframe,
	    mch_pduP->sync_area,mch_pduP->mcs,mch_pduP->Pdu_size);
    } else if (rn->mch_avtive[subframe%5] == 1) { // SF2 -> SF7, SF3 -> SF8
      mch_pduP= &mch_pdu;
      memcpy(&mch_pduP->payload, // could be a simple copy
	     rn->dlsch_rn_MCH[subframe%5]->harq_processes[0]->b,
	     rn->dlsch_rn_MCH[subframe%5]->harq_processes[0]->TBS>>3);
      mch_pduP->Pdu_size = (uint16_t) (rn->dlsch_rn_MCH[subframe%5]->harq_processes[0]->TBS>>3);
      mch_pduP->mcs = rn->dlsch_rn_MCH[subframe%5]->harq_processes[0]->mcs;
      LOG_I(PHY,"[RN %"PRIu8"] Frame %d subframe %d: Forward the MCH PDU for MBSFN received on SF %d sync area %"PRIu8" (MCS %"PRIu8", TBS %"PRIu16")\n",
	    rn->Mod_id,rn->frame, subframe,subframe%5,
	    rn->sync_area[subframe%5],mch_pduP->mcs,mch_pduP->Pdu_size);
    } else {
      mch_pduP=NULL;
    }
    
    rn->mch_avtive[subframe]=0;
    break;
    
  default:
    LOG_W(PHY,"[eNB %"PRIu8"] Frame %d subframe %d: unknown relaying type %d \n",
	  eNB->Mod_id,proc->frame_tx,subframe,r_type);
    mch_pduP=NULL;
    break;
  }// switch
  
  if (mch_pduP) {
    fill_eNB_dlsch_MCH(eNB,mch_pduP->mcs,1,0);
    // Generate PMCH
    generate_mch(eNB,proc,(uint8_t*)mch_pduP->payload);
  } else {
    LOG_D(PHY,"[eNB/RN] Frame %d subframe %d: MCH not generated \n",proc->frame_tx,subframe);
  }
  
#endif
}

void common_signal_procedures (PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc) {

  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  int **txdataF = eNB->common_vars.txdataF[0];
  uint8_t *pbch_pdu=&eNB->pbch_pdu[0];
  int subframe = proc->subframe_tx;
  int frame = proc->frame_tx;

  // generate Cell-Specific Reference Signals for both slots
  if (eNB->abstraction_flag==0) {
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_ENB_RS_TX,1);
    generate_pilots_slot(eNB,
			 txdataF,
			 AMP,
			 subframe<<1,0);
    // check that 2nd slot is for DL
    if (subframe_select(fp,subframe) == SF_DL)
      generate_pilots_slot(eNB,
			   txdataF,
			   AMP,
			   (subframe<<1)+1,0);
    
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_ENB_RS_TX,0);
  }    

  // First half of PSS/SSS (FDD, slot 0)
  if (subframe == 0) {
    if ((fp->frame_type == FDD) &&
	(eNB->abstraction_flag==0)) {
      generate_pss(txdataF,
		   AMP,
		   fp,
		   (fp->Ncp==NORMAL) ? 6 : 5,
		   0);
      generate_sss(txdataF,
		   AMP,
		   fp,
		   (fp->Ncp==NORMAL) ? 5 : 4,
		   0);
      
    }
    
    // generate PBCH (Physical Broadcast CHannel) info
    if ((frame&3) == 0) {
      pbch_pdu[2] = 0;
      
      // FIXME setting pbch_pdu[2] to zero makes the switch statement easier: remove all the or-operators
      switch (fp->N_RB_DL) {
      case 6:
	pbch_pdu[2] = (pbch_pdu[2]&0x1f) | (0<<5);
	break;
	
      case 15:
	pbch_pdu[2] = (pbch_pdu[2]&0x1f) | (1<<5);
	break;
	
      case 25:
	pbch_pdu[2] = (pbch_pdu[2]&0x1f) | (2<<5);
	break;
	
      case 50:
	pbch_pdu[2] = (pbch_pdu[2]&0x1f) | (3<<5);
	break;
	
      case 75:
	pbch_pdu[2] = (pbch_pdu[2]&0x1f) | (4<<5);
	break;
	
      case 100:
	pbch_pdu[2] = (pbch_pdu[2]&0x1f) | (5<<5);
	break;
	
      default:
	// FIXME if we get here, this should be flagged as an error, right?
	pbch_pdu[2] = (pbch_pdu[2]&0x1f) | (2<<5);
	break;
      }
      
      pbch_pdu[2] = (pbch_pdu[2]&0xef) |
	((fp->phich_config_common.phich_duration << 4)&0x10);
      
      switch (fp->phich_config_common.phich_resource) {
      case oneSixth:
	pbch_pdu[2] = (pbch_pdu[2]&0xf3) | (0<<2);
	break;
	
      case half:
	pbch_pdu[2] = (pbch_pdu[2]&0xf3) | (1<<2);
	break;
	
      case one:
	pbch_pdu[2] = (pbch_pdu[2]&0xf3) | (2<<2);
	break;
	
      case two:
	pbch_pdu[2] = (pbch_pdu[2]&0xf3) | (3<<2);
	break;
	
      default:
	// unreachable
	break;
      }
      
      pbch_pdu[2] = (pbch_pdu[2]&0xfc) | ((frame>>8)&0x3);
      pbch_pdu[1] = frame&0xfc;
      pbch_pdu[0] = 0;
    }
      
    /// First half of SSS (TDD, slot 1)
    
    if ((fp->frame_type == TDD)&&
	(eNB->abstraction_flag==0)){
      generate_sss(txdataF,
		   AMP,
		   fp,
		   (fp->Ncp==NORMAL) ? 6 : 5,
		   1);
    }

    /// generate PBCH
    if (eNB->abstraction_flag==0) {
      generate_pbch(&eNB->pbch,
                    txdataF,
                    AMP,
                    fp,
                    pbch_pdu,
                    frame&3);
    }
#ifdef PHY_ABSTRACTION
    else {
      generate_pbch_emul(eNB,pbch_pdu);
    }
#endif

  }
  else if ((subframe == 1) &&
	   (fp->frame_type == TDD)&&
	   (eNB->abstraction_flag==0)) {
    generate_pss(txdataF,
		 AMP,
		 fp,
		 2,
		 2);
  }
  
  // Second half of PSS/SSS (FDD, slot 10)
  else if ((subframe == 5) && 
	   (fp->frame_type == FDD) &&
	   (eNB->abstraction_flag==0)) {
    generate_pss(txdataF,
		 AMP,
		 &eNB->frame_parms,
		 (fp->Ncp==NORMAL) ? 6 : 5,
		 10);
    generate_sss(txdataF,
		 AMP,
		 &eNB->frame_parms,
		 (fp->Ncp==NORMAL) ? 5 : 4,
		 10);

  }

  //  Second-half of SSS (TDD, slot 11)
  else if ((subframe == 5) &&
	   (fp->frame_type == TDD) &&
	   (eNB->abstraction_flag==0)) {
    generate_sss(txdataF,
		 AMP,
		 fp,
		 (fp->Ncp==NORMAL) ? 6 : 5,
		 11);
  }

  // Second half of PSS (TDD, slot 12)
  else if ((subframe == 6) &&
	   (fp->frame_type == TDD) &&
	   (eNB->abstraction_flag==0)) {
    generate_pss(txdataF,
		 AMP,
		 fp,
		 2,
		 12);
  }

}

void generate_eNB_dlsch_params(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,DCI_ALLOC_t *dci_alloc,const int UE_id) {

  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  int frame = proc->frame_tx;
  int subframe = proc->subframe_tx;

  // if we have SI_RNTI, configure dlsch parameters and CCE index
  if (dci_alloc->rnti == SI_RNTI) {
    LOG_D(PHY,"Generating dlsch params for SI_RNTI\n");
    generate_eNB_dlsch_params_from_dci(frame,
				       subframe,
				       &dci_alloc->dci_pdu[0],
				       dci_alloc->rnti,
				       dci_alloc->format,
				       &eNB->dlsch_SI,
				       fp,
				       eNB->pdsch_config_dedicated,
				       SI_RNTI,
				       0,
				       P_RNTI,
				       eNB->UE_stats[0].DL_pmi_single);
    
    
    eNB->dlsch_SI->nCCE[subframe] = dci_alloc->firstCCE;
    
    LOG_T(PHY,"[eNB %"PRIu8"] Frame %d subframe %d : CCE resource for common DCI (SI)  => %"PRIu8"\n",eNB->Mod_id,frame,subframe,
	  eNB->dlsch_SI->nCCE[subframe]);
    
#if defined(SMBV) 
    
    // configure SI DCI
    if (smbv_is_config_frame(frame) && (smbv_frame_cnt < 4)) {
      LOG_D(PHY,"[SMBV] Frame %3d, SI in SF %d DCI %"PRIu32"\n",frame,subframe,i);
      smbv_configure_common_dci(smbv_fname,(smbv_frame_cnt*10) + (subframe), "SI", dci_alloc, i);
    }
    
#endif
    
    
  } else if (dci_alloc->ra_flag == 1) {  // This is format 1A allocation for RA
    // configure dlsch parameters and CCE index
    LOG_D(PHY,"Generating dlsch params for RA_RNTI\n");    
    generate_eNB_dlsch_params_from_dci(frame,
				       subframe,
				       &dci_alloc->dci_pdu[0],
				       dci_alloc->rnti,
				       dci_alloc->format,
				       &eNB->dlsch_ra,
				       fp,
				       eNB->pdsch_config_dedicated,
				       SI_RNTI,
				       dci_alloc->rnti,
				       P_RNTI,
				       eNB->UE_stats[0].DL_pmi_single);
    
    
    eNB->dlsch_ra->nCCE[subframe] = dci_alloc->firstCCE;
    
    LOG_D(PHY,"[eNB %"PRIu8"] Frame %d subframe %d : CCE resource for common DCI (RA)  => %"PRIu8"\n",eNB->Mod_id,frame,subframe,
	  eNB->dlsch_ra->nCCE[subframe]);
#if defined(SMBV) 
    
    // configure RA DCI
    if (smbv_is_config_frame(frame) && (smbv_frame_cnt < 4)) {
      LOG_D(PHY,"[SMBV] Frame %3d, RA in SF %d DCI %"PRIu32"\n",frame,subframe,i);
      smbv_configure_common_dci(smbv_fname,(smbv_frame_cnt*10) + (subframe), "RA", dci_alloc, i);
    }
    
#endif
    
  }
  
  else if ((dci_alloc->format != format0)&&
	   (dci_alloc->format != format3)&&
	   (dci_alloc->format != format3A)&&
	   (dci_alloc->format != format4)){ // this is a normal DLSCH allocation
    

    
    if (UE_id>=0) {
#if defined(SMBV) 
      // Configure this user
      if (smbv_is_config_frame(frame) && (smbv_frame_cnt < 4)) {
	LOG_D(PHY,"[SMBV] Frame %3d, SF %d (SMBV SF %d) Configuring user %d with RNTI %"PRIu16" in TM%"PRIu8"\n",frame,subframe,(smbv_frame_cnt*10) + (subframe),UE_id+1,
              dci_alloc->rnti,eNB->transmission_mode[(uint8_t)UE_id]);
	smbv_configure_user(smbv_fname,UE_id+1,eNB->transmission_mode[(uint8_t)UE_id],dci_alloc->rnti);
      }
      
#endif

      LOG_D(PHY,"Generating dlsch params for RNTI %x\n",dci_alloc->rnti);      
      generate_eNB_dlsch_params_from_dci(frame,
					 subframe,
					 &dci_alloc->dci_pdu[0],
					 dci_alloc->rnti,
					 dci_alloc->format,
					 eNB->dlsch[(uint8_t)UE_id],
					 fp,
					 eNB->pdsch_config_dedicated,
					 SI_RNTI,
					 0,
					 P_RNTI,
					 eNB->UE_stats[(uint8_t)UE_id].DL_pmi_single);
      LOG_D(PHY,"[eNB %"PRIu8"][PDSCH %"PRIx16"/%"PRIu8"] Frame %d subframe %d: Generated dlsch params\n",
	    eNB->Mod_id,dci_alloc->rnti,eNB->dlsch[(uint8_t)UE_id][0]->current_harq_pid,frame,subframe);
      
      
      T(T_ENB_PHY_DLSCH_UE_DCI, T_INT(eNB->Mod_id), T_INT(frame), T_INT(subframe), T_INT(UE_id),
        T_INT(dci_alloc->rnti), T_INT(dci_alloc->format),
        T_INT(eNB->dlsch[(int)UE_id][0]->current_harq_pid));

      eNB->dlsch[(uint8_t)UE_id][0]->nCCE[subframe] = dci_alloc->firstCCE;
      
      LOG_D(PHY,"[eNB %"PRIu8"] Frame %d subframe %d : CCE resource for ue DCI (PDSCH %"PRIx16")  => %"PRIu8"/%u\n",eNB->Mod_id,frame,subframe,
	    dci_alloc->rnti,eNB->dlsch[(uint8_t)UE_id][0]->nCCE[subframe]);
      
#if defined(SMBV) 
      
      // configure UE-spec DCI
      if (smbv_is_config_frame(frame) && (smbv_frame_cnt < 4)) {
	LOG_D(PHY,"[SMBV] Frame %3d, PDSCH in SF %d DCI %"PRIu32"\n",frame,subframe,i);
	smbv_configure_ue_spec_dci(smbv_fname,(smbv_frame_cnt*10) + (subframe), UE_id+1,dci_alloc, i);
      }
      
#endif
      
      LOG_D(PHY,"[eNB %"PRIu8"][DCI][PDSCH %"PRIx16"] Frame %d subframe %d UE_id %"PRId8" Generated DCI format %d, aggregation %d\n",
	    eNB->Mod_id, dci_alloc->rnti,
	    frame, subframe,UE_id,
	    dci_alloc->format,
	    1<<dci_alloc->L);
    } else {
      LOG_D(PHY,"[eNB %"PRIu8"][PDSCH] Frame %d : No UE_id with corresponding rnti %"PRIx16", dropping DLSCH\n",
	    eNB->Mod_id,frame,dci_alloc->rnti);
    }
  }
  
}

void generate_eNB_ulsch_params(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,DCI_ALLOC_t *dci_alloc,const int UE_id) {

  int harq_pid;
  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  int frame = proc->frame_tx;
  int subframe = proc->subframe_tx;

  LOG_D(PHY,
	"[eNB %"PRIu8"][PUSCH %"PRIu8"] Frame %d subframe %d UL Frame %"PRIu32", UL Subframe %"PRIu8", Generated ULSCH (format0) DCI (rnti %"PRIx16", dci %"PRIx8"), aggregation %d\n",
	eNB->Mod_id,
	subframe2harq_pid(fp,
			  pdcch_alloc2ul_frame(fp,frame,subframe),
			  pdcch_alloc2ul_subframe(fp,subframe)),
	frame,
	subframe,
	pdcch_alloc2ul_frame(fp,frame,subframe),
	pdcch_alloc2ul_subframe(fp,subframe),
	dci_alloc->rnti,
	dci_alloc->dci_pdu[0],
	1<<dci_alloc->L);
  
  generate_eNB_ulsch_params_from_dci(eNB,
				     proc,
				     &dci_alloc->dci_pdu[0],
				     dci_alloc->rnti,
				     format0,
				     UE_id,
				     SI_RNTI,
				     0,
				     P_RNTI,
				     CBA_RNTI,
				     0);  // do_srs
  
  LOG_T(PHY,"[eNB %"PRIu8"] Frame %d subframe %d : CCE resources for UE spec DCI (PUSCH %"PRIx16") => %d\n",
	eNB->Mod_id,frame,subframe,dci_alloc->rnti,
	dci_alloc->firstCCE);
  
#if defined(SMBV) 
  
  // configure UE-spec DCI for UL Grant
  if (smbv_is_config_frame(frame) && (smbv_frame_cnt < 4)) {
    LOG_D(PHY,"[SMBV] Frame %3d, SF %d UL DCI %"PRIu32"\n",frame,subframe,i);
    smbv_configure_ue_spec_dci(smbv_fname,(smbv_frame_cnt*10) + (subframe), UE_id+1, &DCI_pdu->dci_alloc[i], i);
  }
  
#endif
  
  
  // get the hard_pid for this subframe 
  harq_pid = subframe2harq_pid(fp,
			       pdcch_alloc2ul_frame(fp,frame,subframe),
			       pdcch_alloc2ul_subframe(fp,subframe));
  
  if (harq_pid==255) { // should not happen, log an error and exit, this is a fatal error
    LOG_E(PHY,"[eNB %"PRIu8"] Frame %d: Bad harq_pid for ULSCH allocation\n",eNB->Mod_id,frame);
    mac_xface->macphy_exit("FATAL\n"); 
  }
  
  if ((dci_alloc->rnti  >= CBA_RNTI) && (dci_alloc->rnti < P_RNTI))
    eNB->ulsch[(uint32_t)UE_id]->harq_processes[harq_pid]->subframe_cba_scheduling_flag = 1;
  else
    eNB->ulsch[(uint32_t)UE_id]->harq_processes[harq_pid]->subframe_scheduling_flag = 1;
  
  T(T_ENB_PHY_ULSCH_UE_DCI, T_INT(eNB->Mod_id), T_INT(frame), T_INT(subframe), T_INT(UE_id),
    T_INT(dci_alloc->rnti), T_INT(harq_pid));

}

void pdsch_procedures(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,LTE_eNB_DLSCH_t *dlsch, LTE_eNB_DLSCH_t *dlsch1,LTE_eNB_UE_stats *ue_stats,int ra_flag,int num_pdcch_symbols) {

  int frame=proc->frame_tx;
  int subframe=proc->subframe_tx;
  int harq_pid = dlsch->current_harq_pid;
  LTE_DL_eNB_HARQ_t *dlsch_harq=dlsch->harq_processes[harq_pid];
  int input_buffer_length = dlsch_harq->TBS/8;
  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  uint8_t *DLSCH_pdu=NULL;
  uint8_t DLSCH_pdu_tmp[768*8];
  uint8_t DLSCH_pdu_rar[256];
  int i;

  LOG_D(PHY,
	"[eNB %"PRIu8"][PDSCH %"PRIx16"/%"PRIu8"] Frame %d, subframe %d: Generating PDSCH/DLSCH with input size = %"PRIu16", G %d, nb_rb %"PRIu16", mcs %"PRIu8", pmi_alloc %"PRIx16", rv %"PRIu8" (round %"PRIu8")\n",
	eNB->Mod_id, dlsch->rnti,harq_pid,
	frame, subframe, input_buffer_length,
	get_G(fp,
	      dlsch_harq->nb_rb,
	      dlsch_harq->rb_alloc,
	      get_Qm(dlsch_harq->mcs),
	      dlsch_harq->Nl,
	      num_pdcch_symbols,frame,subframe),
	dlsch_harq->nb_rb,
	dlsch_harq->mcs,
	pmi2hex_2Ar1(dlsch_harq->pmi_alloc),
	dlsch_harq->rvidx,
	dlsch_harq->round);

#if defined(MESSAGE_CHART_GENERATOR_PHY)
  MSC_LOG_TX_MESSAGE(
		     MSC_PHY_ENB,MSC_PHY_UE,
		     NULL,0,
		     "%05u:%02u PDSCH/DLSCH input size = %"PRIu16", G %d, nb_rb %"PRIu16", mcs %"PRIu8", pmi_alloc %"PRIx16", rv %"PRIu8" (round %"PRIu8")",
		     frame, subframe,
		     input_buffer_length,
		     get_G(fp,
			   dlsch_harq->nb_rb,
			   dlsch_harq->rb_alloc,
			   get_Qm(dlsch_harq->mcs),
			   dlsch_harq->Nl,
			   num_pdcch_symbols,frame,subframe),
		     dlsch_harq->nb_rb,
		     dlsch_harq->mcs,
		     pmi2hex_2Ar1(dlsch_harq->pmi_alloc),
		     dlsch_harq->rvidx,
		     dlsch_harq->round);
#endif

  if (ue_stats) ue_stats->dlsch_sliding_cnt++;

  if (dlsch_harq->round == 0) {

    if (ue_stats)
      ue_stats->dlsch_trials[harq_pid][0]++;

    if (eNB->mac_enabled==1) {
      if (ra_flag == 0) {
	DLSCH_pdu = mac_xface->get_dlsch_sdu(eNB->Mod_id,
					     eNB->CC_id,
					     frame,
					     dlsch->rnti,
					     0);
      }
      else {
	int16_t crnti = mac_xface->fill_rar(eNB->Mod_id,
					    eNB->CC_id,
					    frame,
					    DLSCH_pdu_rar,
					    fp->N_RB_UL,
					    input_buffer_length);
	DLSCH_pdu = DLSCH_pdu_rar;

	int UE_id;

	if (crnti!=0) 
	  UE_id = add_ue(crnti,eNB);
	else 
	  UE_id = -1;
  
	if (UE_id==-1) {
	  LOG_W(PHY,"[eNB] Max user count reached.\n");
	  mac_xface->cancel_ra_proc(eNB->Mod_id,
				    eNB->CC_id,
				    frame,
				    crnti);
	} else {
	  eNB->UE_stats[(uint32_t)UE_id].mode = RA_RESPONSE;
	  // Initialize indicator for first SR (to be cleared after ConnectionSetup is acknowledged)
	  eNB->first_sr[(uint32_t)UE_id] = 1;
	      
	  generate_eNB_ulsch_params_from_rar(DLSCH_pdu,
					     frame,
					     (subframe),
					     eNB->ulsch[(uint32_t)UE_id],
					     fp);
	      
	  eNB->ulsch[(uint32_t)UE_id]->Msg3_active = 1;
	      
	  get_Msg3_alloc(fp,
			 subframe,
			 frame,
			 &eNB->ulsch[(uint32_t)UE_id]->Msg3_frame,
			 &eNB->ulsch[(uint32_t)UE_id]->Msg3_subframe);
	  LOG_D(PHY,"[eNB][RAPROC] Frame %d subframe %d, Activated Msg3 demodulation for UE %"PRId8" in frame %"PRIu32", subframe %"PRIu8"\n",
		frame,
		subframe,
		UE_id,
		eNB->ulsch[(uint32_t)UE_id]->Msg3_frame,
		eNB->ulsch[(uint32_t)UE_id]->Msg3_subframe);
	}
	if (ue_stats) ue_stats->total_TBS_MAC += dlsch_harq->TBS;
      }
    }
    else {
      DLSCH_pdu = DLSCH_pdu_tmp;
	  
      for (i=0; i<input_buffer_length; i++)
	DLSCH_pdu[i] = (unsigned char)(taus()&0xff);
    }
	
#if defined(SMBV) 

    // Configures the data source of allocation (allocation is configured by DCI)
    if (smbv_is_config_frame(frame) && (smbv_frame_cnt < 4)) {
      LOG_D(PHY,"[SMBV] Frame %3d, Configuring PDSCH payload in SF %d alloc %"PRIu8"\n",frame,(smbv_frame_cnt*10) + (subframe),smbv_alloc_cnt);
      //          smbv_configure_datalist_for_user(smbv_fname, find_ue(dlsch->rnti,eNB)+1, DLSCH_pdu, input_buffer_length);
    }

#endif



#ifdef DEBUG_PHY_PROC
#ifdef DEBUG_DLSCH
    LOG_T(PHY,"eNB DLSCH SDU: \n");

    //eNB->dlsch[(uint8_t)UE_id][0]->nCCE[subframe] = DCI_pdu->dci_alloc[i].firstCCE;

    LOG_D(PHY,"[eNB %"PRIu8"] Frame %d subframe %d : CCE resource for ue DCI (PDSCH %"PRIx16")  => %"PRIu8"/%u\n",eNB->Mod_id,eNB->proc[sched_subframe].frame_tx,subframe,
	  DCI_pdu->dci_alloc[i].rnti,eNB->dlsch[(uint8_t)UE_id][0]->nCCE[subframe],DCI_pdu->dci_alloc[i].firstCCE);


    for (i=0; i<dlsch_harq->TBS>>3; i++)
      LOG_T(PHY,"%"PRIx8".",DLSCH_pdu[i]);

    LOG_T(PHY,"\n");
#endif
#endif
  } else {
    ue_stats->dlsch_trials[harq_pid][dlsch_harq->round]++;
#ifdef DEBUG_PHY_PROC
#ifdef DEBUG_DLSCH
    LOG_D(PHY,"[eNB] This DLSCH is a retransmission\n");
#endif
#endif
  }

  if (eNB->abstraction_flag==0) {

    LOG_D(PHY,"Generating DLSCH/PDSCH %d\n",ra_flag);
    // 36-212
    start_meas(&eNB->dlsch_encoding_stats);
    eNB->te(eNB,
	    DLSCH_pdu,
	    num_pdcch_symbols,
	    dlsch,
	    frame,subframe,
	    &eNB->dlsch_rate_matching_stats,
	    &eNB->dlsch_turbo_encoding_stats,
	    &eNB->dlsch_interleaving_stats);
    stop_meas(&eNB->dlsch_encoding_stats);
    // 36-211
    start_meas(&eNB->dlsch_scrambling_stats);
    dlsch_scrambling(fp,
		     0,
		     dlsch,
		     get_G(fp,
			   dlsch_harq->nb_rb,
			   dlsch_harq->rb_alloc,
			   get_Qm(dlsch_harq->mcs),
			   dlsch_harq->Nl,
			   num_pdcch_symbols,frame,subframe),
		     0,
		     subframe<<1);
    stop_meas(&eNB->dlsch_scrambling_stats);

    start_meas(&eNB->dlsch_modulation_stats);


    dlsch_modulation(eNB->common_vars.txdataF[0],
		     AMP,
		     subframe,
		     fp,
		     num_pdcch_symbols,
		     dlsch,
		     dlsch1);
	
    stop_meas(&eNB->dlsch_modulation_stats);
  }


#ifdef PHY_ABSTRACTION
  else {
    start_meas(&eNB->dlsch_encoding_stats);
    dlsch_encoding_emul(eNB,
			DLSCH_pdu,
			dlsch);
    stop_meas(&eNB->dlsch_encoding_stats);
  }

#endif
  dlsch->active = 0;
}

void phy_procedures_eNB_TX(PHY_VARS_eNB *eNB,
			   eNB_rxtx_proc_t *proc,
                           relaying_type_t r_type,
			   PHY_VARS_RN *rn,
			   int do_meas)
{
  UNUSED(rn);
  int frame=proc->frame_tx;
  int subframe=proc->subframe_tx;
  //  uint16_t input_buffer_length;
  uint32_t i,aa;
  uint8_t harq_pid;
  DCI_PDU *DCI_pdu;
  DCI_PDU DCI_pdu_tmp;
  int8_t UE_id=0;
  uint8_t num_pdcch_symbols=0;
  uint8_t ul_subframe;
  uint32_t ul_frame;
  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  DCI_ALLOC_t *dci_alloc=(DCI_ALLOC_t *)NULL;

  int offset = proc == &eNB->proc.proc_rxtx[0] ? 0 : 1;
  struct timespec tt1, tt2, tt3, tt4;
#if defined(SMBV) 
  // counts number of allocations in subframe
  // there is at least one allocation for PDCCH
  uint8_t smbv_alloc_cnt = 1;Exiting eNB thread RXn_TXnp4

#endif

  if ((fp->frame_type == TDD) && (subframe_select(fp,subframe)!=SF_DL)) return;
  
  //clock_gettime(CLOCK_REALTIME, &tt3);
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_PROCEDURES_ENB_TX+offset,1);
  if (do_meas==1) start_meas(&eNB->phy_proc_tx);

  T(T_ENB_PHY_DL_TICK, T_INT(eNB->Mod_id), T_INT(frame), T_INT(subframe));

  eNB->complete_m2p = 0;
  eNB->complete_dci = 0;
  eNB->complete_sch_SR = 0;
  eNB->flag_m2p=1;

  while(eNB->complete_m2p!=1);
  eNB->thread_cch.r_type = r_type;
  eNB->complete_cch = 0;
  eNB->flag_cch=1;
  while(eNB->complete_dci!=1);
 
	int k;
	eNB->complete_pdsch=0;
	for(int i=NUM_THREAD;i<NUMBER_OF_UE_MAX;i++)
	{
		eNB->ue_status[i]=0;	
	}
	/*VCD for all UE's nb_rb*/
	for(UE_id=0;UE_id<NUMBER_OF_UE_MAX;++UE_id)
	{
		if ((eNB->dlsch[(uint8_t)UE_id][0])&&
		  (eNB->dlsch[(uint8_t)UE_id][0]->rnti>0)&&
		  (eNB->dlsch[(uint8_t)UE_id][0]->active == 1))
		  {
			  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_ENB_RB01+UE_id,eNB->dlsch[(uint8_t)UE_id][0]->harq_processes[eNB->dlsch[(uint8_t)UE_id][0]->current_harq_pid]->nb_rb);
		  }
		else
		{
			VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_ENB_RB01+UE_id,0);
		}
	}	
	eNB->flag_sch=1;
	while(eNB->complete_pdsch!=NUMBER_OF_UE_MAX);
	
    while(eNB->complete_cch!=1);
    while(eNB->complete_sch_SR!=1);
#ifdef EMOS
  phy_procedures_emos_eNB_TX(subframe, eNB);
#endif

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_PROCEDURES_ENB_TX+offset,0);
  /*clock_gettime(CLOCK_REALTIME, &tt4);
  eNB->timing[eNB->temp] = tt4.tv_nsec - tt3.tv_nsec;
  if(eNB->temp==MAX_OF_TIMING-1) eNB->temp = 0; 
  else ++eNB->temp;*/
  if (do_meas==1) stop_meas(&eNB->phy_proc_tx);
}

void process_Msg3(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,uint8_t UE_id, uint8_t harq_pid)
{
  // this prepares the demodulation of the first PUSCH of a new user, containing Msg3
  int subframe = proc->subframe_rx;
  int frame = proc->frame_rx;

  LOG_D(PHY,"[eNB %d][RAPROC] frame %d : subframe %d : process_Msg3 UE_id %d (active %d, subframe %d, frame %d)\n",
        eNB->Mod_id,
        frame,subframe,
        UE_id,eNB->ulsch[(uint32_t)UE_id]->Msg3_active,
        eNB->ulsch[(uint32_t)UE_id]->Msg3_subframe,
        eNB->ulsch[(uint32_t)UE_id]->Msg3_frame);
  eNB->ulsch[(uint32_t)UE_id]->Msg3_flag = 0;
  
  if ((eNB->ulsch[(uint32_t)UE_id]->Msg3_active == 1) &&
      (eNB->ulsch[(uint32_t)UE_id]->Msg3_subframe == subframe) &&
      (eNB->ulsch[(uint32_t)UE_id]->Msg3_frame == (uint32_t)frame))   {
	//printf("======================MESSAGE3========================================\n");
    //    harq_pid = 0;
    eNB->ulsch[(uint32_t)UE_id]->Msg3_active = 0;
    eNB->ulsch[(uint32_t)UE_id]->Msg3_flag = 1;
    eNB->ulsch[(uint32_t)UE_id]->harq_processes[harq_pid]->subframe_scheduling_flag=1;
    LOG_D(PHY,"[eNB %d][RAPROC] frame %d, subframe %d: Setting subframe_scheduling_flag (Msg3) for UE %d\n",
          eNB->Mod_id,
          frame,subframe,UE_id);
  }
}


// This function retrieves the harq_pid of the corresponding DLSCH process
// and updates the error statistics of the DLSCH based on the received ACK
// info from UE along with the round index.  It also performs the fine-grain
// rate-adaptation based on the error statistics derived from the ACK/NAK process

void process_HARQ_feedback(uint8_t UE_id,
                           PHY_VARS_eNB *eNB,
			   eNB_rxtx_proc_t *proc,
                           uint8_t pusch_flag,
                           uint8_t *pucch_payload,
                           uint8_t pucch_sel,
                           uint8_t SR_payload)
{

  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  uint8_t dl_harq_pid[8],dlsch_ACK[8],dl_subframe;
  LTE_eNB_DLSCH_t *dlsch             =  eNB->dlsch[(uint32_t)UE_id][0];
  LTE_eNB_UE_stats *ue_stats         =  &eNB->UE_stats[(uint32_t)UE_id];
  LTE_DL_eNB_HARQ_t *dlsch_harq_proc;
  uint8_t subframe_m4,M,m;
  int mp;
  int all_ACKed=1,nb_alloc=0,nb_ACK=0;
  int frame = proc->frame_rx;
  int subframe = proc->subframe_rx;
  int harq_pid = subframe2harq_pid( fp,frame,subframe);


  if (fp->frame_type == FDD) { //FDD
    subframe_m4 = (subframe<4) ? subframe+6 : subframe-4;

    dl_harq_pid[0] = dlsch->harq_ids[subframe_m4];
    M=1;

    if (pusch_flag == 1) {
      dlsch_ACK[0] = eNB->ulsch[(uint8_t)UE_id]->harq_processes[harq_pid]->o_ACK[0];
      if (dlsch->subframe_tx[subframe_m4]==1)
	LOG_D(PHY,"[eNB %d] Frame %d: Received ACK/NAK %d on PUSCH for subframe %d\n",eNB->Mod_id,
	      frame,dlsch_ACK[0],subframe_m4);
    }
    else {
      dlsch_ACK[0] = pucch_payload[0];
      LOG_D(PHY,"[eNB %d] Frame %d: Received ACK/NAK %d on PUCCH for subframe %d\n",eNB->Mod_id,
	    frame,dlsch_ACK[0],subframe_m4);
      /*
	if (dlsch_ACK[0]==0)
	AssertFatal(0,"Exiting on NAK on PUCCH\n");
      */
    }


#if defined(MESSAGE_CHART_GENERATOR_PHY)
    MSC_LOG_RX_MESSAGE(
		       MSC_PHY_ENB,MSC_PHY_UE,
		       NULL,0,
		       "%05u:%02u %s received %s  rnti %x harq id %u  tx SF %u",
		       frame,subframe,
		       (pusch_flag == 1)?"PUSCH":"PUCCH",
		       (dlsch_ACK[0])?"ACK":"NACK",
		       dlsch->rnti,
		       dl_harq_pid[0],
		       subframe_m4
		       );
#endif
  } else { // TDD Handle M=1,2 cases only

    M=ul_ACK_subframe2_M(fp,
                         subframe);

    // Now derive ACK information for TDD
    if (pusch_flag == 1) { // Do PUSCH ACK/NAK first
      // detect missing DAI
      //FK: this code is just a guess
      //RK: not exactly, yes if scheduled from PHICH (i.e. no DCI format 0)
      //    otherwise, it depends on how many of the PDSCH in the set are scheduled, we can leave it like this,
      //    but we have to adapt the code below.  For example, if only one out of 2 are scheduled, only 1 bit o_ACK is used

      dlsch_ACK[0] = eNB->ulsch[(uint8_t)UE_id]->harq_processes[harq_pid]->o_ACK[0];
      dlsch_ACK[1] = (eNB->pucch_config_dedicated[UE_id].tdd_AckNackFeedbackMode == bundling)
	?eNB->ulsch[(uint8_t)UE_id]->harq_processes[harq_pid]->o_ACK[0]:eNB->ulsch[(uint8_t)UE_id]->harq_processes[harq_pid]->o_ACK[1];
    }

    else {  // PUCCH ACK/NAK
      if ((SR_payload == 1)&&(pucch_sel!=2)) {  // decode Table 7.3 if multiplexing and SR=1
        nb_ACK = 0;

        if (M == 2) {
          if ((pucch_payload[0] == 1) && (pucch_payload[1] == 1)) // b[0],b[1]
            nb_ACK = 1;
          else if ((pucch_payload[0] == 1) && (pucch_payload[1] == 0))
            nb_ACK = 2;
        } else if (M == 3) {
          if ((pucch_payload[0] == 1) && (pucch_payload[1] == 1))
            nb_ACK = 1;
          else if ((pucch_payload[0] == 1) && (pucch_payload[1] == 0))
            nb_ACK = 2;
          else if ((pucch_payload[0] == 0) && (pucch_payload[1] == 1))
            nb_ACK = 3;
        }
      } else if (pucch_sel == 2) { // bundling or M=1
        dlsch_ACK[0] = pucch_payload[0];
        dlsch_ACK[1] = pucch_payload[0];
      } else { // multiplexing with no SR, this is table 10.1
        if (M==1)
          dlsch_ACK[0] = pucch_payload[0];
        else if (M==2) {
          if (((pucch_sel == 1) && (pucch_payload[0] == 1) && (pucch_payload[1] == 1)) ||
              ((pucch_sel == 0) && (pucch_payload[0] == 0) && (pucch_payload[1] == 1)))
            dlsch_ACK[0] = 1;
          else
            dlsch_ACK[0] = 0;

          if (((pucch_sel == 1) && (pucch_payload[0] == 1) && (pucch_payload[1] == 1)) ||
              ((pucch_sel == 1) && (pucch_payload[0] == 0) && (pucch_payload[1] == 0)))
            dlsch_ACK[1] = 1;
          else
            dlsch_ACK[1] = 0;
        }
      }
    }
  }

  // handle case where positive SR was transmitted with multiplexing
  if ((SR_payload == 1)&&(pucch_sel!=2)&&(pusch_flag == 0)) {
    nb_alloc = 0;

    for (m=0; m<M; m++) {
      dl_subframe = ul_ACK_subframe2_dl_subframe(fp,
						 subframe,
						 m);

      if (dlsch->subframe_tx[dl_subframe]==1)
        nb_alloc++;
    }

    if (nb_alloc == nb_ACK)
      all_ACKed = 1;
    else
      all_ACKed = 0;
  }


  for (m=0,mp=-1; m<M; m++) {

    dl_subframe = ul_ACK_subframe2_dl_subframe(fp,
					       subframe,
					       m);

    if (dlsch->subframe_tx[dl_subframe]==1) {
      if (pusch_flag == 1)
        mp++;
      else
        mp = m;

      dl_harq_pid[m]     = dlsch->harq_ids[dl_subframe];

      if ((pucch_sel != 2)&&(pusch_flag == 0)) { // multiplexing
        if ((SR_payload == 1)&&(all_ACKed == 1))
          dlsch_ACK[m] = 1;
        else
          dlsch_ACK[m] = 0;
      }

      if (dl_harq_pid[m]<dlsch->Mdlharq) {
        dlsch_harq_proc = dlsch->harq_processes[dl_harq_pid[m]];
#ifdef DEBUG_PHY_PROC
        LOG_D(PHY,"[eNB %d][PDSCH %x/%d] subframe %d, status %d, round %d (mcs %d, rv %d, TBS %d)\n",eNB->Mod_id,
              dlsch->rnti,dl_harq_pid[m],dl_subframe,
              dlsch_harq_proc->status,dlsch_harq_proc->round,
              dlsch->harq_processes[dl_harq_pid[m]]->mcs,
              dlsch->harq_processes[dl_harq_pid[m]]->rvidx,
              dlsch->harq_processes[dl_harq_pid[m]]->TBS);

        if (dlsch_harq_proc->status==DISABLED)
          LOG_E(PHY,"dlsch_harq_proc is disabled? \n");

#endif

        if ((dl_harq_pid[m]<dlsch->Mdlharq) &&
            (dlsch_harq_proc->status == ACTIVE)) {
          // dl_harq_pid of DLSCH is still active

          if ( dlsch_ACK[mp]==0) {
            // Received NAK
#ifdef DEBUG_PHY_PROC
            LOG_D(PHY,"[eNB %d][PDSCH %x/%d] M = %d, m= %d, mp=%d NAK Received in round %d, requesting retransmission\n",eNB->Mod_id,
                  dlsch->rnti,dl_harq_pid[m],M,m,mp,dlsch_harq_proc->round);
#endif

            T(T_ENB_PHY_DLSCH_UE_NACK, T_INT(eNB->Mod_id), T_INT(frame), T_INT(subframe), T_INT(UE_id), T_INT(dlsch->rnti),
              T_INT(dl_harq_pid[m]));

            if (dlsch_harq_proc->round == 0)
              ue_stats->dlsch_NAK_round0++;

            ue_stats->dlsch_NAK[dl_harq_pid[m]][dlsch_harq_proc->round]++;


            // then Increment DLSCH round index
            dlsch_harq_proc->round++;


            if (dlsch_harq_proc->round == dlsch->Mlimit) {
              // This was the last round for DLSCH so reset round and increment l2_error counter
#ifdef DEBUG_PHY_PROC
              LOG_W(PHY,"[eNB %d][PDSCH %x/%d] DLSCH retransmissions exhausted, dropping packet\n",eNB->Mod_id,
                    dlsch->rnti,dl_harq_pid[m]);
#endif
#if defined(MESSAGE_CHART_GENERATOR_PHY)
              MSC_LOG_EVENT(MSC_PHY_ENB, "0 HARQ DLSCH Failed RNTI %"PRIx16" round %u",
                            dlsch->rnti,
                            dlsch_harq_proc->round);
#endif

              dlsch_harq_proc->round = 0;
              ue_stats->dlsch_l2_errors[dl_harq_pid[m]]++;
              dlsch_harq_proc->status = SCH_IDLE;
              dlsch->harq_ids[dl_subframe] = dlsch->Mdlharq;
            }
          } else {
#ifdef DEBUG_PHY_PROC
            LOG_D(PHY,"[eNB %d][PDSCH %x/%d] ACK Received in round %d, resetting process\n",eNB->Mod_id,
                  dlsch->rnti,dl_harq_pid[m],dlsch_harq_proc->round);
#endif

            T(T_ENB_PHY_DLSCH_UE_ACK, T_INT(eNB->Mod_id), T_INT(frame), T_INT(subframe), T_INT(UE_id), T_INT(dlsch->rnti),
              T_INT(dl_harq_pid[m]));

            ue_stats->dlsch_ACK[dl_harq_pid[m]][dlsch_harq_proc->round]++;

            // Received ACK so set round to 0 and set dlsch_harq_pid IDLE
            dlsch_harq_proc->round  = 0;
            dlsch_harq_proc->status = SCH_IDLE;
            dlsch->harq_ids[dl_subframe] = dlsch->Mdlharq;

            ue_stats->total_TBS = ue_stats->total_TBS +
	      eNB->dlsch[(uint8_t)UE_id][0]->harq_processes[dl_harq_pid[m]]->TBS;
            /*
              ue_stats->total_transmitted_bits = ue_stats->total_transmitted_bits +
              eNB->dlsch[(uint8_t)UE_id][0]->harq_processes[dl_harq_pid[m]]->TBS;
            */
          }

          // Do fine-grain rate-adaptation for DLSCH
          if (ue_stats->dlsch_NAK_round0 > dlsch->error_threshold) {
            if (ue_stats->dlsch_mcs_offset == 1)
              ue_stats->dlsch_mcs_offset=0;
            else
              ue_stats->dlsch_mcs_offset=-1;
          }

#ifdef DEBUG_PHY_PROC
          LOG_D(PHY,"[process_HARQ_feedback] Frame %d Setting round to %d for pid %d (subframe %d)\n",frame,
                dlsch_harq_proc->round,dl_harq_pid[m],subframe);
#endif

          // Clear NAK stats and adjust mcs offset
          // after measurement window timer expires
          if (ue_stats->dlsch_sliding_cnt == dlsch->ra_window_size) {
            if ((ue_stats->dlsch_mcs_offset == 0) && (ue_stats->dlsch_NAK_round0 < 2))
              ue_stats->dlsch_mcs_offset = 1;

            if ((ue_stats->dlsch_mcs_offset == 1) && (ue_stats->dlsch_NAK_round0 > 2))
              ue_stats->dlsch_mcs_offset = 0;

            if ((ue_stats->dlsch_mcs_offset == 0) && (ue_stats->dlsch_NAK_round0 > 2))
              ue_stats->dlsch_mcs_offset = -1;

            if ((ue_stats->dlsch_mcs_offset == -1) && (ue_stats->dlsch_NAK_round0 < 2))
              ue_stats->dlsch_mcs_offset = 0;

            ue_stats->dlsch_NAK_round0 = 0;
            ue_stats->dlsch_sliding_cnt = 0;
          }


        }
      }
    }
  }
}

void get_n1_pucch_eNB(PHY_VARS_eNB *eNB,
		      eNB_rxtx_proc_t *proc,
                      uint8_t UE_id,
                      int16_t *n1_pucch0,
                      int16_t *n1_pucch1,
                      int16_t *n1_pucch2,
                      int16_t *n1_pucch3)
{

  LTE_DL_FRAME_PARMS *frame_parms=&eNB->frame_parms;
  uint8_t nCCE0,nCCE1;
  int sf;
  int frame = proc->frame_rx;
  int subframe = proc->subframe_rx;

  if (frame_parms->frame_type == FDD ) {
    sf = (subframe<4) ? (subframe+6) : (subframe-4);

    if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[sf]>0) {
      *n1_pucch0 = frame_parms->pucch_config_common.n1PUCCH_AN + eNB->dlsch[(uint32_t)UE_id][0]->nCCE[sf];
      *n1_pucch1 = -1;
    } else {
      *n1_pucch0 = -1;
      *n1_pucch1 = -1;
    }
  } else {

    switch (frame_parms->tdd_config) {
    case 1:  // DL:S:UL:UL:DL:DL:S:UL:UL:DL
      if (subframe == 2) {  // ACK subframes 5 and 6
        /*  if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[6]>0) {
	    nCCE1 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[6];
	    *n1_pucch1 = get_Np(frame_parms->N_RB_DL,nCCE1,1) + nCCE1 + frame_parms->pucch_config_common.n1PUCCH_AN;
	    }
	    else
	    *n1_pucch1 = -1;*/

        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[5]>0) {
          nCCE0 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[5];
          *n1_pucch0 = get_Np(frame_parms->N_RB_DL,nCCE0,0) + nCCE0+ frame_parms->pucch_config_common.n1PUCCH_AN;
        } else
          *n1_pucch0 = -1;

        *n1_pucch1 = -1;
      } else if (subframe == 3) { // ACK subframe 9

        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[9]>0) {
          nCCE0 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[9];
          *n1_pucch0 = get_Np(frame_parms->N_RB_DL,nCCE0,0) + nCCE0 +frame_parms->pucch_config_common.n1PUCCH_AN;
        } else
          *n1_pucch0 = -1;

        *n1_pucch1 = -1;

      } else if (subframe == 7) { // ACK subframes 0 and 1
        //harq_ack[0].nCCE;
        //harq_ack[1].nCCE;
        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[0]>0) {
          nCCE0 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[0];
          *n1_pucch0 = get_Np(frame_parms->N_RB_DL,nCCE0,0) + nCCE0 + frame_parms->pucch_config_common.n1PUCCH_AN;
        } else
          *n1_pucch0 = -1;

        *n1_pucch1 = -1;
      } else if (subframe == 8) { // ACK subframes 4
        //harq_ack[4].nCCE;
        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[4]>0) {
          nCCE0 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[4];
          *n1_pucch0 = get_Np(frame_parms->N_RB_DL,nCCE0,0) + nCCE0 + frame_parms->pucch_config_common.n1PUCCH_AN;
        } else
          *n1_pucch0 = -1;

        *n1_pucch1 = -1;
      } else {
        LOG_D(PHY,"[eNB %d] frame %d: phy_procedures_lte.c: get_n1pucch, illegal subframe %d for tdd_config %d\n",
              eNB->Mod_id,
              frame,
              subframe,frame_parms->tdd_config);
        return;
      }

      break;

    case 3:  // DL:S:UL:UL:UL:DL:DL:DL:DL:DL
      if (subframe == 2) {  // ACK subframes 5,6 and 1 (S in frame-2), forget about n-11 for the moment (S-subframe)
        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[6]>0) {
          nCCE1 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[6];
          *n1_pucch1 = get_Np(frame_parms->N_RB_DL,nCCE1,1) + nCCE1 + frame_parms->pucch_config_common.n1PUCCH_AN;
        } else
          *n1_pucch1 = -1;

        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[5]>0) {
          nCCE0 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[5];
          *n1_pucch0 = get_Np(frame_parms->N_RB_DL,nCCE0,0) + nCCE0+ frame_parms->pucch_config_common.n1PUCCH_AN;
        } else
          *n1_pucch0 = -1;
      } else if (subframe == 3) { // ACK subframes 7 and 8
        LOG_D(PHY,"get_n1_pucch_eNB : subframe 3, subframe_tx[7] %d, subframe_tx[8] %d\n",
              eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[7],eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[8]);

        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[8]>0) {
          nCCE1 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[8];
          *n1_pucch1 = get_Np(frame_parms->N_RB_DL,nCCE1,1) + nCCE1 + frame_parms->pucch_config_common.n1PUCCH_AN;
          LOG_D(PHY,"nCCE1 %d, n1_pucch1 %d\n",nCCE1,*n1_pucch1);
        } else
          *n1_pucch1 = -1;

        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[7]>0) {
          nCCE0 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[7];
          *n1_pucch0 = get_Np(frame_parms->N_RB_DL,nCCE0,0) + nCCE0 +frame_parms->pucch_config_common.n1PUCCH_AN;
          LOG_D(PHY,"nCCE0 %d, n1_pucch0 %d\n",nCCE0,*n1_pucch0);
        } else
          *n1_pucch0 = -1;
      } else if (subframe == 4) { // ACK subframes 9 and 0
        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[0]>0) {
          nCCE1 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[0];
          *n1_pucch1 = get_Np(frame_parms->N_RB_DL,nCCE1,1) + nCCE1 + frame_parms->pucch_config_common.n1PUCCH_AN;
        } else
          *n1_pucch1 = -1;

        if (eNB->dlsch[(uint32_t)UE_id][0]->subframe_tx[9]>0) {
          nCCE0 = eNB->dlsch[(uint32_t)UE_id][0]->nCCE[9];
          *n1_pucch0 = get_Np(frame_parms->N_RB_DL,nCCE0,0) + nCCE0 +frame_parms->pucch_config_common.n1PUCCH_AN;
        } else
          *n1_pucch0 = -1;
      } else {
        LOG_D(PHY,"[eNB %d] Frame %d: phy_procedures_lte.c: get_n1pucch, illegal subframe %d for tdd_config %d\n",
              eNB->Mod_id,frame,subframe,frame_parms->tdd_config);
        return;
      }

      break;
    }  // switch tdd_config

    // Don't handle the case M>2
    *n1_pucch2 = -1;
    *n1_pucch3 = -1;
  }
}

void prach_procedures(PHY_VARS_eNB *eNB) {

  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  uint16_t preamble_energy_list[64],preamble_delay_list[64];
  uint16_t preamble_max,preamble_energy_max;
  uint16_t i;
  int8_t UE_id;
  int subframe = eNB->proc.subframe_prach;
  int frame = eNB->proc.frame_prach;
  uint8_t CC_id = eNB->CC_id;

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_ENB_PRACH_RX,1);
  memset(&preamble_energy_list[0],0,64*sizeof(uint16_t));
  memset(&preamble_delay_list[0],0,64*sizeof(uint16_t));

  if (eNB->abstraction_flag == 0) {
    LOG_D(PHY,"[eNB %d][RAPROC] Frame %d, Subframe %d : PRACH RX Signal Power : %d dBm\n",eNB->Mod_id, 
          frame,subframe,dB_fixed(signal_energy(&eNB->common_vars.rxdata[0][0][subframe*fp->samples_per_tti],512)) - eNB->rx_total_gain_dB);


    rx_prach(eNB,
             preamble_energy_list,
             preamble_delay_list,
             frame,
             0);
  } else {
    for (UE_id=0; UE_id<NB_UE_INST; UE_id++) {

      LOG_D(PHY,"[RAPROC] UE_id %d (%p), generate_prach %d, UE RSI %d, eNB RSI %d preamble index %d\n",
            UE_id,PHY_vars_UE_g[UE_id][CC_id],PHY_vars_UE_g[UE_id][CC_id]->generate_prach,
            PHY_vars_UE_g[UE_id][CC_id]->frame_parms.prach_config_common.rootSequenceIndex,
            fp->prach_config_common.rootSequenceIndex,
            PHY_vars_UE_g[UE_id][CC_id]->prach_PreambleIndex);

      if ((PHY_vars_UE_g[UE_id][CC_id]->generate_prach==1) &&
          (PHY_vars_UE_g[UE_id][CC_id]->frame_parms.prach_config_common.rootSequenceIndex ==
           fp->prach_config_common.rootSequenceIndex) ) {
        preamble_energy_list[PHY_vars_UE_g[UE_id][CC_id]->prach_PreambleIndex] = 800;
        preamble_delay_list[PHY_vars_UE_g[UE_id][CC_id]->prach_PreambleIndex] = 5;

      }
    }
  }

  preamble_energy_max = preamble_energy_list[0];
  preamble_max = 0;

  for (i=1; i<64; i++) {
    if (preamble_energy_max < preamble_energy_list[i]) {
      preamble_energy_max = preamble_energy_list[i];
      preamble_max = i;
    }
  }

#ifdef DEBUG_PHY_PROC
  LOG_D(PHY,"[RAPROC] Most likely preamble %d, energy %d dB delay %d\n",
        preamble_max,
        preamble_energy_list[preamble_max],
        preamble_delay_list[preamble_max]);
#endif

  if (preamble_energy_list[preamble_max] > 580) {

    UE_id = find_next_ue_index(eNB);
 
    if (UE_id>=0) {
      eNB->UE_stats[(uint32_t)UE_id].UE_timing_offset = preamble_delay_list[preamble_max]&0x1FFF; //limit to 13 (=11+2) bits

      eNB->UE_stats[(uint32_t)UE_id].sector = 0;
      LOG_D(PHY,"[eNB %d/%d][RAPROC] Frame %d, subframe %d Initiating RA procedure (UE_id %d) with preamble %d, energy %d.%d dB, delay %d\n",
            eNB->Mod_id,
            eNB->CC_id,
            frame,
            subframe,
	    UE_id,
            preamble_max,
            preamble_energy_max/10,
            preamble_energy_max%10,
            preamble_delay_list[preamble_max]);

      if (eNB->mac_enabled==1) {
        uint8_t update_TA=4;

        switch (fp->N_RB_DL) {
        case 6:
          update_TA = 16;
          break;

        case 25:
          update_TA = 4;
          break;

        case 50:
          update_TA = 2;
          break;

        case 100:
          update_TA = 1;
          break;
        }

	mac_xface->initiate_ra_proc(eNB->Mod_id,
				    eNB->CC_id,
				    frame,
				    preamble_max,
				    preamble_delay_list[preamble_max]*update_TA,
				    0,subframe,0);
      }      

    } else {
      MSC_LOG_EVENT(MSC_PHY_ENB, "0 RA Failed add user, too many");
      LOG_I(PHY,"[eNB %d][RAPROC] frame %d, subframe %d: Unable to add user, max user count reached\n",
            eNB->Mod_id,frame, subframe);
    }
  }
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_ENB_PRACH_RX,0);
}

void pucch_procedures(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,int UE_id,int harq_pid) {

  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  uint8_t SR_payload = 0,*pucch_payload=NULL,pucch_payload0[2]= {0,0},pucch_payload1[2]= {0,0};
  int16_t n1_pucch0,n1_pucch1,n1_pucch2,n1_pucch3;
  uint8_t do_SR = 0;
  uint8_t pucch_sel = 0;
  int32_t metric0=0,metric1=0,metric0_SR=0;
  ANFBmode_t bundling_flag;
  PUCCH_FMT_t format;
  const int subframe = proc->subframe_rx;
  const int frame = proc->frame_rx;

  if ((eNB->dlsch[UE_id][0]) &&
      (eNB->dlsch[UE_id][0]->rnti>0) &&
      (eNB->ulsch[UE_id]->harq_processes[harq_pid]->subframe_scheduling_flag==0)) { 

    // check SR availability
    do_SR = is_SR_subframe(eNB,proc,UE_id);
    //      do_SR = 0;
    
    // Now ACK/NAK
    // First check subframe_tx flag for earlier subframes

    get_n1_pucch_eNB(eNB,
		     proc,
		     UE_id,
		     &n1_pucch0,
		     &n1_pucch1,
		     &n1_pucch2,
		     &n1_pucch3);
    
    LOG_D(PHY,"[eNB %d][PDSCH %x] Frame %d, subframe %d Checking for PUCCH (%d,%d,%d,%d) SR %d\n",
	  eNB->Mod_id,eNB->dlsch[UE_id][0]->rnti,
	  frame,subframe,
	  n1_pucch0,n1_pucch1,n1_pucch2,n1_pucch3,do_SR);
    
    if ((n1_pucch0==-1) && (n1_pucch1==-1) && (do_SR==0)) {  // no TX PDSCH that have to be checked and no SR for this UE_id
    } else {
      // otherwise we have some PUCCH detection to do
      
      // Null out PUCCH PRBs for noise measurement
      switch(fp->N_RB_UL) {
      case 6:
	eNB->rb_mask_ul[0] |= (0x1 | (1<<5)); //position 5
	break;
      case 15:
	eNB->rb_mask_ul[0] |= (0x1 | (1<<14)); // position 14
	break;
      case 25:
	eNB->rb_mask_ul[0] |= (0x1 | (1<<24)); // position 24
	break;
      case 50:
	eNB->rb_mask_ul[0] |= 0x1;
	eNB->rb_mask_ul[1] |= (1<<17); // position 49 (49-32)
	break;
      case 75:
	eNB->rb_mask_ul[0] |= 0x1;
	eNB->rb_mask_ul[2] |= (1<<10); // position 74 (74-64)
	break;
      case 100:
	eNB->rb_mask_ul[0] |= 0x1;
	eNB->rb_mask_ul[3] |= (1<<3); // position 99 (99-96)
	break;
      default:
	LOG_E(PHY,"Unknown number for N_RB_UL %d\n",fp->N_RB_UL);
	break;
      }
      
      if (do_SR == 1) {
	eNB->UE_stats[UE_id].sr_total++;


	if (eNB->abstraction_flag == 0)
	  metric0_SR = rx_pucch(eNB,
				pucch_format1,
				UE_id,
				eNB->scheduling_request_config[UE_id].sr_PUCCH_ResourceIndex,
				0, // n2_pucch
				0, // shortened format, should be use_srs flag, later
				&SR_payload,
				frame,
				subframe,
				PUCCH1_THRES);
	
#ifdef PHY_ABSTRACTION
	else {
	  metric0_SR = rx_pucch_emul(eNB,
				     proc,
				     UE_id,
				     pucch_format1,
				     0,
				     &SR_payload);
	  LOG_D(PHY,"[eNB %d][SR %x] Frame %d subframe %d Checking SR (UE SR %d/%d)\n",eNB->Mod_id,
		eNB->ulsch[UE_id]->rnti,frame,subframe,SR_payload,eNB->scheduling_request_config[UE_id].sr_PUCCH_ResourceIndex);
	}
	
#endif

	if (SR_payload == 1) {
	  LOG_D(PHY,"[eNB %d][SR %x] Frame %d subframe %d Got SR for PUSCH, transmitting to MAC\n",eNB->Mod_id,
		eNB->ulsch[UE_id]->rnti,frame,subframe);
	  eNB->UE_stats[UE_id].sr_received++;
	  
	  if (eNB->first_sr[UE_id] == 1) { // this is the first request for uplink after Connection Setup, so clear HARQ process 0 use for Msg4
	    eNB->first_sr[UE_id] = 0;
	    eNB->dlsch[UE_id][0]->harq_processes[0]->round=0;
	    eNB->dlsch[UE_id][0]->harq_processes[0]->status=SCH_IDLE;
	    LOG_D(PHY,"[eNB %d][SR %x] Frame %d subframe %d First SR\n",
		  eNB->Mod_id,
		  eNB->ulsch[UE_id]->rnti,frame,subframe);
	  }
	  
	  if (eNB->mac_enabled==1) {
	    mac_xface->SR_indication(eNB->Mod_id,
				     eNB->CC_id,
				     frame,
				     eNB->dlsch[UE_id][0]->rnti,subframe);
	  }
	}
      }// do_SR==1
      
      if ((n1_pucch0==-1) && (n1_pucch1==-1)) { // just check for SR
      } else if (eNB->frame_parms.frame_type==FDD) { // FDD
	// if SR was detected, use the n1_pucch from SR, else use n1_pucch0
	//          n1_pucch0 = (SR_payload==1) ? eNB->scheduling_request_config[UE_id].sr_PUCCH_ResourceIndex:n1_pucch0;
	
	LOG_D(PHY,"Demodulating PUCCH for ACK/NAK: n1_pucch0 %d (%d), SR_payload %d\n",n1_pucch0,eNB->scheduling_request_config[UE_id].sr_PUCCH_ResourceIndex,SR_payload);
	
	if (eNB->abstraction_flag == 0) {
	  
	  
	  
	  metric0 = rx_pucch(eNB,
			     pucch_format1a,
			     UE_id,
			     (uint16_t)n1_pucch0,
			     0, //n2_pucch
			     0, // shortened format
			     pucch_payload0,
			     frame,
			     subframe,
			     PUCCH1a_THRES);
	  
	  if (metric0 < metric0_SR)
	    metric0=rx_pucch(eNB,
			     pucch_format1a,
			     UE_id,
			     eNB->scheduling_request_config[UE_id].sr_PUCCH_ResourceIndex,
			     0, //n2_pucch
			     0, // shortened format
			     pucch_payload0,
			     frame,
			     subframe,
			     PUCCH1a_THRES);
	  
	}
	
	if (eNB->mac_enabled==1) {
	  mac_xface->SR_indication(eNB->Mod_id,
				   eNB->CC_id,
				   frame,
				   eNB->dlsch[UE_id][0]->rnti,subframe);
	}
      }
    }// do_SR==1
    
    if ((n1_pucch0==-1) && (n1_pucch1==-1)) { // just check for SR
    } else if (fp->frame_type==FDD) { // FDD
      // if SR was detected, use the n1_pucch from SR, else use n1_pucch0
      //          n1_pucch0 = (SR_payload==1) ? eNB->scheduling_request_config[UE_id].sr_PUCCH_ResourceIndex:n1_pucch0;
      
      LOG_D(PHY,"Demodulating PUCCH for ACK/NAK: n1_pucch0 %d (%d), SR_payload %d\n",n1_pucch0,eNB->scheduling_request_config[UE_id].sr_PUCCH_ResourceIndex,SR_payload);
      
      if (eNB->abstraction_flag == 0) {
	
	
	
	metric0 = rx_pucch(eNB,
			   pucch_format1a,
			   UE_id,
			   (uint16_t)n1_pucch0,
			   0, //n2_pucch
			   0, // shortened format
			   pucch_payload0,
			   frame,
			   subframe,
			   PUCCH1a_THRES);
	  
	if (metric0 < metric0_SR)
	  metric0=rx_pucch(eNB,
			   pucch_format1a,
			   UE_id,
			   eNB->scheduling_request_config[UE_id].sr_PUCCH_ResourceIndex,
			   0, //n2_pucch
			   0, // shortened format
			   pucch_payload0,
			   frame,
			   subframe,
			   PUCCH1a_THRES);
      }
      else {
#ifdef PHY_ABSTRACTION
	metric0 = rx_pucch_emul(eNB,
				proc,
				UE_id,
				pucch_format1a,
				0,
				pucch_payload0);
#endif
      }
	
#ifdef DEBUG_PHY_PROC
      LOG_D(PHY,"[eNB %d][PDSCH %x] Frame %d subframe %d pucch1a (FDD) payload %d (metric %d)\n",
	    eNB->Mod_id,
	    eNB->dlsch[UE_id][0]->rnti,
	    frame,subframe,
	    pucch_payload0[0],metric0);
#endif
	
      process_HARQ_feedback(UE_id,eNB,proc,
			    0,// pusch_flag
			    pucch_payload0,
			    2,
			    SR_payload);

    } // FDD
    else {  //TDD

      bundling_flag = eNB->pucch_config_dedicated[UE_id].tdd_AckNackFeedbackMode;

      // fix later for 2 TB case and format1b

      if ((fp->frame_type==FDD) ||
	  (bundling_flag==bundling)    ||
	  ((fp->frame_type==TDD)&&(fp->tdd_config==1)&&((subframe!=2)||(subframe!=7)))) {
	format = pucch_format1a;
      } else {
	format = pucch_format1b;
      }

      // if SR was detected, use the n1_pucch from SR
      if (SR_payload==1) {
#ifdef DEBUG_PHY_PROC
	LOG_D(PHY,"[eNB %d][PDSCH %x] Frame %d subframe %d Checking ACK/NAK (%d,%d,%d,%d) format %d with SR\n",eNB->Mod_id,
	      eNB->dlsch[UE_id][0]->rnti,
	      frame,subframe,
	      n1_pucch0,n1_pucch1,n1_pucch2,n1_pucch3,format);
#endif

	if (eNB->abstraction_flag == 0)
	  metric0_SR = rx_pucch(eNB,
				format,
				UE_id,
				eNB->scheduling_request_config[UE_id].sr_PUCCH_ResourceIndex,
				0, //n2_pucch
				0, // shortened format
				pucch_payload0,
				frame,
				subframe,
				PUCCH1a_THRES);
	else {
#ifdef PHY_ABSTRACTION
	  metric0 = rx_pucch_emul(eNB,proc,
				  UE_id,
				  format,
				  0,
				  pucch_payload0);
#endif
	}
      } else { //using n1_pucch0/n1_pucch1 resources
#ifdef DEBUG_PHY_PROC
	LOG_D(PHY,"[eNB %d][PDSCH %x] Frame %d subframe %d Checking ACK/NAK (%d,%d,%d,%d) format %d\n",eNB->Mod_id,
	      eNB->dlsch[UE_id][0]->rnti,
	      frame,subframe,
	      n1_pucch0,n1_pucch1,n1_pucch2,n1_pucch3,format);
#endif
	metric0=0;
	metric1=0;

	// Check n1_pucch0 metric
	if (n1_pucch0 != -1) {
	  if (eNB->abstraction_flag == 0)
	    metric0 = rx_pucch(eNB,
			       format,
			       UE_id,
			       (uint16_t)n1_pucch0,
			       0, // n2_pucch
			       0, // shortened format
			       pucch_payload0,
			       frame,
			       subframe,
			       PUCCH1a_THRES);
	  else {
#ifdef PHY_ABSTRACTION
	    metric0 = rx_pucch_emul(eNB,
				    proc,
				    UE_id,
				    format,
				    0,
				    pucch_payload0);
#endif
	  }
	}

	// Check n1_pucch1 metric
	if (n1_pucch1 != -1) {
	  if (eNB->abstraction_flag == 0)
	    metric1 = rx_pucch(eNB,
			       format,
			       UE_id,
			       (uint16_t)n1_pucch1,
			       0, //n2_pucch
			       0, // shortened format
			       pucch_payload1,
			       frame,
			       subframe,
			       PUCCH1a_THRES);
	  else {
#ifdef PHY_ABSTRACTION
	    metric1 = rx_pucch_emul(eNB,
				    proc,
				    UE_id,
				    format,
				    1,
				    pucch_payload1);
#endif
	  }
	}
      }

      if (SR_payload == 1) {
	pucch_payload = pucch_payload0;

	if (bundling_flag == bundling)
	  pucch_sel = 2;
      } else if (bundling_flag == multiplexing) { // multiplexing + no SR
	pucch_payload = (metric1>metric0) ? pucch_payload1 : pucch_payload0;
	pucch_sel     = (metric1>metric0) ? 1 : 0;
      } else { // bundling + no SR
	if (n1_pucch1 != -1)
	  pucch_payload = pucch_payload1;
	else if (n1_pucch0 != -1)
	  pucch_payload = pucch_payload0;

	pucch_sel = 2;  // indicate that this is a bundled ACK/NAK
      }

#ifdef DEBUG_PHY_PROC
      LOG_D(PHY,"[eNB %d][PDSCH %x] Frame %d subframe %d ACK/NAK metric 0 %d, metric 1 %d, sel %d, (%d,%d)\n",eNB->Mod_id,
	    eNB->dlsch[UE_id][0]->rnti,
	    frame,subframe,
	    metric0,metric1,pucch_sel,pucch_payload[0],pucch_payload[1]);
#endif
      process_HARQ_feedback(UE_id,eNB,proc,
			    0,// pusch_flag
			    pucch_payload,
			    pucch_sel,
			    SR_payload);
    }
  }  
}


void cba_procedures(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,int UE_id,int harq_pid) {

  uint8_t access_mode;
  int num_active_cba_groups;
  const int subframe = proc->subframe_rx;
  const int frame = proc->frame_rx;
  uint16_t rnti=0;
  int ret=0;
  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;

  if (eNB->ulsch[UE_id]==NULL) return;

  num_active_cba_groups = eNB->ulsch[UE_id]->num_active_cba_groups;
 
  if ((num_active_cba_groups > 0) &&
      (eNB->ulsch[UE_id]->cba_rnti[UE_id%num_active_cba_groups]>0) &&
      (eNB->ulsch[UE_id]->harq_processes[harq_pid]->subframe_cba_scheduling_flag==1)) {
    rnti=0;
    
#ifdef DEBUG_PHY_PROC
    LOG_D(PHY,"[eNB %d][PUSCH %d] frame %d subframe %d Checking PUSCH/ULSCH CBA Reception for UE %d with cba rnti %x mode %s\n",
	  eNB->Mod_id,harq_pid,
	  frame,subframe,
	  UE_id, (uint16_t)eNB->ulsch[UE_id]->cba_rnti[UE_id%num_active_cba_groups],mode_string[eNB->UE_stats[UE_id].mode]);
#endif
    
    if (eNB->abstraction_flag==0) {
      rx_ulsch(eNB,proc,
	       eNB->UE_stats[UE_id].sector,  // this is the effective sector id
	       UE_id,
	       eNB->ulsch,
	       0);
    }
    
#ifdef PHY_ABSTRACTION
    else {
      rx_ulsch_emul(eNB,proc,
		    eNB->UE_stats[UE_id].sector,  // this is the effective sector id
		    UE_id);
    }
    
#endif
    
    if (eNB->abstraction_flag == 0) {
      ret = ulsch_decoding(eNB,proc,
			   UE_id,
			   0, // control_only_flag
			   eNB->ulsch[UE_id]->harq_processes[harq_pid]->V_UL_DAI,
			   eNB->ulsch[UE_id]->harq_processes[harq_pid]->nb_rb>20 ? 1 : 0);
    }
    
#ifdef PHY_ABSTRACTION
    else {
      ret = ulsch_decoding_emul(eNB,
				proc,
				UE_id,
				&rnti);
    }
    
#endif
    
    if (eNB->ulsch[UE_id]->harq_processes[harq_pid]->cqi_crc_status == 1) {
#ifdef DEBUG_PHY_PROC
      
      print_CQI(eNB->ulsch[UE_id]->harq_processes[harq_pid]->o,eNB->ulsch[UE_id]->harq_processes[harq_pid]->uci_format,0,fp->N_RB_DL);
#endif
      access_mode = UNKNOWN_ACCESS;
      extract_CQI(eNB->ulsch[UE_id]->harq_processes[harq_pid]->o,
		  eNB->ulsch[UE_id]->harq_processes[harq_pid]->uci_format,
		  &eNB->UE_stats[UE_id],
		  fp->N_RB_DL,
		  &rnti, &access_mode);
      eNB->UE_stats[UE_id].rank = eNB->ulsch[UE_id]->harq_processes[harq_pid]->o_RI[0];
    }
    
    eNB->ulsch[UE_id]->harq_processes[harq_pid]->subframe_cba_scheduling_flag=0;
    eNB->ulsch[UE_id]->harq_processes[harq_pid]->status= SCH_IDLE;
      
    if ((num_active_cba_groups > 0) &&
	(UE_id + num_active_cba_groups < NUMBER_OF_UE_MAX) &&
	(eNB->ulsch[UE_id+num_active_cba_groups]->cba_rnti[UE_id%num_active_cba_groups] > 0 ) &&
	(eNB->ulsch[UE_id+num_active_cba_groups]->num_active_cba_groups> 0)) {
#ifdef DEBUG_PHY_PROC
      LOG_D(PHY,"[eNB %d][PUSCH %d] frame %d subframe %d UE %d harq_pid %d resetting the subframe_scheduling_flag for Ue %d cba groups %d members\n",
	    eNB->Mod_id,harq_pid,frame,subframe,UE_id,harq_pid,
	    UE_id+num_active_cba_groups, UE_id%eNB->ulsch[UE_id]->num_active_cba_groups);
#endif
      eNB->ulsch[UE_id+num_active_cba_groups]->harq_processes[harq_pid]->subframe_cba_scheduling_flag=1;
      eNB->ulsch[UE_id+num_active_cba_groups]->harq_processes[harq_pid]->status= CBA_ACTIVE;
      eNB->ulsch[UE_id+num_active_cba_groups]->harq_processes[harq_pid]->TBS=eNB->ulsch[UE_id]->harq_processes[harq_pid]->TBS;
    }

    if (ret == (1+MAX_TURBO_ITERATIONS)) {
      eNB->UE_stats[UE_id].ulsch_round_errors[harq_pid][eNB->ulsch[UE_id]->harq_processes[harq_pid]->round]++;
      eNB->ulsch[UE_id]->harq_processes[harq_pid]->phich_active = 1;
      eNB->ulsch[UE_id]->harq_processes[harq_pid]->phich_ACK = 0;
      eNB->ulsch[UE_id]->harq_processes[harq_pid]->round++;
    } // ulsch in error
    else {
      LOG_D(PHY,"[eNB %d][PUSCH %d] Frame %d subframe %d ULSCH received, setting round to 0, PHICH ACK\n",
	    eNB->Mod_id,harq_pid,
	    frame,subframe);

      eNB->ulsch[UE_id]->harq_processes[harq_pid]->phich_active = 1;
      eNB->ulsch[UE_id]->harq_processes[harq_pid]->phich_ACK = 1;
      eNB->ulsch[UE_id]->harq_processes[harq_pid]->round = 0;
      eNB->UE_stats[UE_id].ulsch_consecutive_errors = 0;
#ifdef DEBUG_PHY_PROC
#ifdef DEBUG_ULSCH
      LOG_D(PHY,"[eNB] Frame %d, Subframe %d : ULSCH SDU (RX harq_pid %d) %d bytes:",
	    frame,subframe,
	    harq_pid,eNB->ulsch[UE_id]->harq_processes[harq_pid]->TBS>>3);

      for (j=0; j<eNB->ulsch[UE_id]->harq_processes[harq_pid]->TBS>>3; j++)
	LOG_T(PHY,"%x.",eNB->ulsch[UE_id]->harq_processes[harq_pid]->b[j]);

      LOG_T(PHY,"\n");
#endif
#endif

      if (access_mode > UNKNOWN_ACCESS) {
	LOG_D(PHY,"[eNB %d] Frame %d, Subframe %d : received ULSCH SDU from CBA transmission, UE (%d,%x), CBA (group %d, rnti %x)\n",
	      eNB->Mod_id, frame,subframe,
	      UE_id, eNB->ulsch[UE_id]->rnti,
	      UE_id % eNB->ulsch[UE_id]->num_active_cba_groups, eNB->ulsch[UE_id]->cba_rnti[UE_id%num_active_cba_groups]);

	// detect if there is a CBA collision
	if ((eNB->cba_last_reception[UE_id%num_active_cba_groups] == 0 ) && 
	    (eNB->mac_enabled==1)) {
	  mac_xface->rx_sdu(eNB->Mod_id,
			    eNB->CC_id,
			    frame,subframe,
			    eNB->ulsch[UE_id]->rnti,
			    eNB->ulsch[UE_id]->harq_processes[harq_pid]->b,
			    eNB->ulsch[UE_id]->harq_processes[harq_pid]->TBS>>3,
			    harq_pid,
			    NULL);

	  eNB->cba_last_reception[UE_id%num_active_cba_groups]+=1;//(subframe);
	} else {
	  if (eNB->cba_last_reception[UE_id%num_active_cba_groups] == 1 )
	    LOG_N(PHY,"[eNB%d] Frame %d subframe %d : first CBA collision detected \n ",
		  eNB->Mod_id,frame,subframe);

	  LOG_N(PHY,"[eNB%d] Frame %d subframe %d : CBA collision set SR for UE %d in group %d \n ",
		eNB->Mod_id,frame,subframe,
		eNB->cba_last_reception[UE_id%num_active_cba_groups],UE_id%num_active_cba_groups );

	  eNB->cba_last_reception[UE_id%num_active_cba_groups]+=1;

	  mac_xface->SR_indication(eNB->Mod_id,
				   eNB->CC_id,
				   frame,
				   eNB->dlsch[UE_id][0]->rnti,subframe);
	}
      } // UNKNOWN_ACCESS
    } // ULSCH CBA not in error
  }

}

typedef struct {
  PHY_VARS_eNB *eNB;
  int slot;
} fep_task;

void fep0(PHY_VARS_eNB *eNB,int slot) {

  eNB_proc_t *proc       = &eNB->proc;
  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;
  int l;

  //  printf("fep0: slot %d\n",slot);

  remove_7_5_kHz(eNB,(slot&1)+(proc->subframe_rx<<1));
  for (l=0; l<fp->symbols_per_tti/2; l++) {
    slot_fep_ul(fp,
		&eNB->common_vars,
		l,
		(slot&1)+(proc->subframe_rx<<1),
		0,
		0
		);
  }
}



extern int oai_exit;

static void *fep_thread(void *param) {

  PHY_VARS_eNB *eNB = (PHY_VARS_eNB *)param;
  eNB_proc_t *proc  = &eNB->proc;
  while (!oai_exit) {

    if (wait_on_condition(&proc->mutex_fep,&proc->cond_fep,&proc->instance_cnt_fep,"fep thread")<0) break;  
    fep0(eNB,0);
    if (release_thread(&proc->mutex_fep,&proc->instance_cnt_fep,"fep thread")<0) break;

    if (pthread_cond_signal(&proc->cond_fep) != 0) {
      printf("[eNB] ERROR pthread_cond_signal for fep thread exit\n");
      exit_fun( "ERROR pthread_cond_signal" );
      return NULL;
    }
  }



  return(NULL);
}

void init_fep_thread(PHY_VARS_eNB *eNB,pthread_attr_t *attr_fep) {

  eNB_proc_t *proc = &eNB->proc;

  proc->instance_cnt_fep         = -1;
    
  pthread_mutex_init( &proc->mutex_fep, NULL);
  pthread_cond_init( &proc->cond_fep, NULL);

  pthread_create(&proc->pthread_fep, attr_fep, fep_thread, (void*)eNB);


}

extern void *td_thread(void*);

void init_td_thread(PHY_VARS_eNB *eNB,pthread_attr_t *attr_td) {

  eNB_proc_t *proc = &eNB->proc;

  proc->tdp.eNB = eNB;
  proc->instance_cnt_td         = -1;
    
  pthread_mutex_init( &proc->mutex_td, NULL);
  pthread_cond_init( &proc->cond_td, NULL);

  pthread_create(&proc->pthread_td, attr_td, td_thread, (void*)&proc->tdp);

}

extern void *te_thread(void*);

void init_te_thread(PHY_VARS_eNB *eNB,pthread_attr_t *attr_te) {

  eNB_proc_t *proc = &eNB->proc;

  proc->tep.eNB = eNB;
  proc->instance_cnt_te         = -1;
    
  pthread_mutex_init( &proc->mutex_te, NULL);
  pthread_cond_init( &proc->cond_te, NULL);

  printf("Creating te_thread\n");
  pthread_create(&proc->pthread_te, attr_te, te_thread, (void*)&proc->tep);

}


void eNB_fep_full_2thread(PHY_VARS_eNB *eNB) {

  eNB_proc_t *proc = &eNB->proc;

  struct timespec wait;

  wait.tv_sec=0;
  wait.tv_nsec=5000000L;

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_ENB_SLOT_FEP,1);
  start_meas(&eNB->ofdm_demod_stats);

  if (pthread_mutex_timedlock(&proc->mutex_fep,&wait) != 0) {
    printf("[eNB] ERROR pthread_mutex_lock for fep thread (IC %d)\n", proc->instance_cnt_fep);
    exit_fun( "error locking mutex_fep" );
    return;
  }

  if (proc->instance_cnt_fep==0) {
    printf("[eNB] FEP thread busy\n");
    exit_fun("FEP thread busy");
    pthread_mutex_unlock( &proc->mutex_fep );
    return;
  }
  
  ++proc->instance_cnt_fep;


  if (pthread_cond_signal(&proc->cond_fep) != 0) {
    printf("[eNB] ERROR pthread_cond_signal for fep thread\n");
    exit_fun( "ERROR pthread_cond_signal" );
    return;
  }
  
  pthread_mutex_unlock( &proc->mutex_fep );

  // call second slot in this symbol
  fep0(eNB,1);

  wait_on_busy_condition(&proc->mutex_fep,&proc->cond_fep,&proc->instance_cnt_fep,"fep thread");  

  stop_meas(&eNB->ofdm_demod_stats);
}



void eNB_fep_full(PHY_VARS_eNB *eNB) {

  eNB_proc_t *proc = &eNB->proc;
  int l;
  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  struct timespec tt1, tt2;	

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_ENB_SLOT_FEP,1);
  start_meas(&eNB->ofdm_demod_stats);
  //clock_gettime(CLOCK_REALTIME, &tt1);
  remove_7_5_kHz(eNB,proc->subframe_rx<<1);
  remove_7_5_kHz(eNB,1+(proc->subframe_rx<<1));
  //clock_gettime(CLOCK_REALTIME, &tt2);
  //printf("Remove CP consumes %ld nanoseconds!\n", tt2.tv_nsec - tt1.tv_nsec);
  for (l=0; l<fp->symbols_per_tti/2; l++) {
    slot_fep_ul(fp,
		&eNB->common_vars,
		l,
		proc->subframe_rx<<1,
		0,
		0
		);
    slot_fep_ul(fp,
		&eNB->common_vars,
		l,
		1+(proc->subframe_rx<<1),
		0,
		0
		);
  }
  stop_meas(&eNB->ofdm_demod_stats);
  
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_ENB_SLOT_FEP,0);
  
  if (eNB->node_function == NGFI_RRU_IF4p5) {
    /// **** send_IF4 of rxdataF to RCC (no prach now) **** ///
    send_IF4p5(eNB, proc->frame_rx, proc->subframe_rx, IF4p5_PULFFT, 0);
  }    
}

void eNB_fep_rru_if5(PHY_VARS_eNB *eNB) {

  eNB_proc_t *proc=&eNB->proc;
  uint8_t seqno=0;

  /// **** send_IF5 of rxdata to BBU **** ///       
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_SEND_IF5, 1 );  
  send_IF5(eNB, proc->timestamp_rx, proc->subframe_rx, &seqno, IF5_RRH_GW_UL);
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_SEND_IF5, 0 );          

}

void do_prach(PHY_VARS_eNB *eNB) {

  eNB_proc_t *proc = &eNB->proc;
  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;

  // check if we have to detect PRACH first
  if (is_prach_subframe(fp,proc->frame_rx,proc->subframe_rx)>0) { 
    /* accept some delay in processing - up to 5ms */
    int i;
    for (i = 0; i < 10 && proc->instance_cnt_prach == 0; i++) {
      LOG_W(PHY,"[eNB] Frame %d Subframe %d, eNB PRACH thread busy (IC %d)!!\n", proc->frame_rx,proc->subframe_rx,proc->instance_cnt_prach);
      usleep(500);
    }
    if (proc->instance_cnt_prach == 0) {
      exit_fun( "PRACH thread busy" );
      return;
    }
    
    // wake up thread for PRACH RX
    if (pthread_mutex_lock(&proc->mutex_prach) != 0) {
      LOG_E( PHY, "[eNB] ERROR pthread_mutex_lock for eNB PRACH thread %d (IC %d)\n", proc->instance_cnt_prach );
      exit_fun( "error locking mutex_prach" );
      return;
    }
    
    ++proc->instance_cnt_prach;
    // set timing for prach thread
    proc->frame_prach = proc->frame_rx;
    proc->subframe_prach = proc->subframe_rx;
    
    // the thread can now be woken up
    if (pthread_cond_signal(&proc->cond_prach) != 0) {
      LOG_E( PHY, "[eNB] ERROR pthread_cond_signal for eNB PRACH thread %d\n", proc->thread_index);
      exit_fun( "ERROR pthread_cond_signal" );
      return;
    }
    
    pthread_mutex_unlock( &proc->mutex_prach );
  }

}

void phy_procedures_eNB_common_RX(PHY_VARS_eNB *eNB){


  eNB_proc_t *proc       = &eNB->proc;
  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;
  const int subframe     = proc->subframe_rx;
  const int frame        = proc->frame_rx;
  int offset             = (eNB->single_thread_flag==1) ? 0 : (subframe&1);

  if ((fp->frame_type == TDD) && (subframe_select(fp,subframe)!=SF_UL)) return;

  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_FRAME_NUMBER_RX0_ENB+offset, proc->frame_rx );
  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_SUBFRAME_NUMBER_RX0_ENB+offset, proc->subframe_rx );
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_PROCEDURES_ENB_RX_COMMON+offset, 1 );
  
  start_meas(&eNB->phy_proc_rx);
  LOG_D(PHY,"[eNB %d] Frame %d: Doing phy_procedures_eNB_common_RX(%d)\n",eNB->Mod_id,frame,subframe);


  if (eNB->fep) eNB->fep(eNB);

  if (eNB->do_prach) eNB->do_prach(eNB);

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_PROCEDURES_ENB_RX_COMMON+offset, 0 );
}

///////////////////////////////////////////////////////////////////////////////////////// ISIP
void Build_RS(PHY_VARS_eNB *eNB,
		   eNB_rxtx_proc_t *proc,
		   LTE_eNB_ULSCH_t **ulsch,
              uint8_t eNB_id,
			  uint8_t UE_id,
			  uint8_t Ns,
              unsigned int subframe,
			  uint32_t harq_pid)
              //unsigned int first_rb,
              //unsigned int nb_rb)
{

	int16_t alpha_re[12] = {32767, 28377, 16383,     0,-16384,  -28378,-32768,-28378,-16384,    -1, 16383, 28377};
    int16_t alpha_im[12] = {0,     16383, 28377, 32767, 28377,   16383,     0,-16384,-28378,-32768,-28378,-16384};

	LTE_DL_FRAME_PARMS *frame_parms = &eNB->frame_parms;
	LTE_eNB_COMMON *common_vars = &eNB->common_vars;
	LTE_eNB_PUSCH *pusch_vars = eNB->pusch_vars[UE_id];
	
	int **ul_ch_mag	 =  pusch_vars->ul_ch_mag[eNB_id];
	int **ul_ch_magb   =  pusch_vars->ul_ch_magb[eNB_id];
	
	short *mag,*magb ;
	
	int first_rb = ulsch[UE_id]->harq_processes[harq_pid]->first_rb;
	int nb_rb    = ulsch[UE_id]->harq_processes[harq_pid]->nb_rb   ;
	
	uint8_t cyclic_shift,alpha_ind;
	uint16_t Msc_RS,Msc_RS_idx;
    uint16_t *Msc_idx_ptr;	
	
	short Qm = get_Qm_ul(ulsch[UE_id]->harq_processes[harq_pid]->mcs);
	
    uint32_t u=frame_parms->pusch_config_common.ul_ReferenceSignalsPUSCH.grouphop[Ns+(subframe<<1)];
    uint32_t v=frame_parms->pusch_config_common.ul_ReferenceSignalsPUSCH.seqhop[Ns+(subframe<<1)];
	
	int   *ptr_rs,*ptr_rs512;
	short  nb_rb1,nb_rb2;
    int    firstSymobol_offset,offset,i,l;
    int    aa =0;
	short  re,im;
	
	int **ReferenceSignal = common_vars->ReferenceSignals[eNB_id];
	int **cyclicShift = common_vars->cyclicShift_ISIP[eNB_id];
	
	short *cyclicShift_ptr;
	
	short OFDM_size = frame_parms->ofdm_symbol_size ;
	short first_carrier_offset = frame_parms->first_carrier_offset ;
	
    cyclic_shift = (frame_parms->pusch_config_common.ul_ReferenceSignalsPUSCH.cyclicShift +
                    ulsch[UE_id]->harq_processes[harq_pid]->n_DMRS2 +
                    frame_parms->pusch_config_common.ul_ReferenceSignalsPUSCH.nPRS[(subframe<<1) + Ns]) % 12;	
	Msc_RS  = nb_rb*12 ;

	//firstSymobol_offset = 0;

//uint16_t dftsizes[33] = {12,24,36,48,60,72,96,108,120,144,180,192,216,240,288,300,324,360,384,432,480,540,576,600,648,720,864,900,960,972,1080,1152,1200};		
#if defined(USER_MODE)
    Msc_idx_ptr = (uint16_t*) bsearch(&Msc_RS, dftsizes, 33, sizeof(uint16_t), compareints);
   
    if (Msc_idx_ptr)
      Msc_RS_idx = Msc_idx_ptr - dftsizes;
    else {
      msg("lte_ul_channel_estimation: index for Msc_RS=%d not found\n",Msc_RS);
      return(-1);
    } 
    
#else
  uint8_t b;

  for (b=0; b<33; b++)
    if (Msc_RS==dftsizes[b])
      Msc_RS_idx = b;

#endif
	
    nb_rb1 = cmin(cmax((int)(frame_parms->N_RB_UL) - (int)(2*first_rb),(int)0),(int)(2*nb_rb));   
    nb_rb2 = 2*nb_rb - nb_rb1 ; //remain RBs

	ptr_rs  = ( int* )ul_ref_sigs_rx[u][v][Msc_RS_idx];
		
	//printf("first_rb %d nb_rb %d nb_rb1 %d nb_rb2 %d \n",first_rb,nb_rb,nb_rb1,nb_rb2);
	//printf("[YAO] u %d v %d UE_id %d first_rb %d nb_rb %d Msc_RS_idx %d cyclic_shift %d \n",u,v,UE_id,first_rb,nb_rb,Msc_RS_idx,cyclic_shift);
	if(nb_rb1){	
	
	   firstSymobol_offset = first_rb*12 + first_carrier_offset + Ns*OFDM_size ; 
	   
	   ptr_rs512 = &ReferenceSignal[aa][firstSymobol_offset];

	   memcpy(ptr_rs512, ptr_rs, nb_rb1*6*sizeof(int));

	   ptr_rs += nb_rb1*6 ; 

		if (nb_rb2)  {
			
			 firstSymobol_offset = Ns*OFDM_size ;
			 
		     ptr_rs512 =  &ReferenceSignal[aa][firstSymobol_offset];
			 	
			 memcpy(ptr_rs512, ptr_rs, nb_rb2*6*sizeof(int));

			 ptr_rs += nb_rb2*6; 
			 
		}
	}
	   else{

			 firstSymobol_offset = 6*(2*first_rb - frame_parms->N_RB_UL) + Ns*OFDM_size ;
			 
		     ptr_rs512 = &ReferenceSignal[aa][firstSymobol_offset]; 

		     memcpy(ptr_rs512, ptr_rs, nb_rb2*6*sizeof(int));
			
			 ptr_rs += nb_rb2*6; 
	   }
	 //ptr_rs512 = &ReferenceSignal[aa][firstSymobol_offset];
     //for ( i = 0 ; i< nb_rb1*6 ; i++) printf("[YAO][ptr_rs512] %d re %d im %d \n",i,((short*)ptr_rs512)[2*i],((short*)ptr_rs512)[2*i+1]);
//cyclic shift
    alpha_ind = 0 ; 
	
	cyclicShift_ptr = ( short *)&cyclicShift[aa][ Ns*OFDM_size ]; 
	
	if(cyclic_shift != 0 ){
		
		 for(i=0 ; i< nb_rb*12 ; i++){
		 	 
           offset = ( first_rb*12 + first_carrier_offset + i) % OFDM_size ;
		   
		   //offset = ( offset >= 150 && offset < 362)? offset+212: offset;
		   
		   cyclicShift_ptr[offset<<1]	= alpha_re[alpha_ind] ;
		   
		   cyclicShift_ptr[(offset<<1) + 1] = alpha_im[alpha_ind] ;
		 
		   //printf("YAO C %d re %d im %d \n",offset,cyclicShift_ptr[offset<<1],cyclicShift_ptr[(offset<<1) + 1]);
		   
		   alpha_ind += cyclic_shift ;
		   if (alpha_ind>11)
                     alpha_ind-=12;
	  }
	}
	else{
		
	  for(i=0 ; i< nb_rb*12 ; i++){
		 	 
           offset = ( first_rb*12 + first_carrier_offset + i) % OFDM_size ;
		   
		   //offset = ( offset >= 150 && offset < 362)? offset+212: offset;
		   
		   cyclicShift_ptr[offset<<1]	    = 123 ;
		   
		   cyclicShift_ptr[(offset<<1) + 1] = 0 ;
		   
	  }
	}
//mag	
  if(Ns == 0){
	  
    for(l = 0 ; l < 14 ; l ++ ){
		
		if(l!= 3 && l!=10){
			
		  mag   = (short*) &ul_ch_mag[0][l*frame_parms->N_RB_DL*12];
		  magb = (short*) &ul_ch_magb[0][l*frame_parms->N_RB_DL*12];
		
		    for(i=0 ; i< nb_rb*12 ; i ++){
			 
			    if ( Qm==4 ){ 
			    
			        mag[2*i]    =324;  
			        mag[2*i +1] =324;	 
			  
			} 
			    else { 
			  
			          mag[2*i]     =316;   
			          mag[2*i +1]  =316;  
					  
			          magb[2*i]    =158;    
			          magb[2*i +1] =158;    
     				  
     			    }  
     		    } 
     		}
     	}
    }		
}
 
int32_t lte_ul_channel_estimation_ISIP(PHY_VARS_eNB *eNB,
				       eNB_rxtx_proc_t *proc,
                                        uint8_t eNB_id,
                                        //uint8_t UE_id, 
									    uint8_t subframe,
										short nb_rb
										)
{
	
	PHY_VARS_UE *UE;
	
	LTE_DL_FRAME_PARMS *frame_parms = &eNB->frame_parms;
    //LTE_eNB_PUSCH *pusch_vars = eNB->pusch_vars[UE_id];
	LTE_eNB_COMMON *common_vars = &eNB->common_vars;
	
	int32_t **ul_ch_estimates =common_vars->drs_ch_estimates_ISIP[eNB_id];
	int32_t **ul_ch_estimates1=common_vars->drs_ch_estimates_ISIP1[eNB_id];
	int32_t **ul_ch_estimates2=common_vars->drs_ch_estimates_ISIP2[eNB_id];
	int32_t **rxdataF_ext 	  =common_vars->rxdataF[eNB_id]; 
	int32_t **ReferenceSignal =common_vars->ReferenceSignals[eNB_id];
	int32_t **cyclicShift     =common_vars->cyclicShift_ISIP[eNB_id];
	
	//uint8_t harq_pid = subframe2harq_pid(frame_parms,proc->frame_rx,subframe);
	int16_t delta_phase = 0;
    int16_t ru_90 [2*128] = {32767, 0,32766, 402,32758, 804,32746, 1206,32729, 1608,32706, 2009,32679, 2411,32647, 2811,32610, 3212,32568, 3612,32522, 4011,32470, 4410,32413, 4808,32352, 5205,32286, 5602,32214, 5998,32138, 6393,32058, 6787,31972, 7180,31881, 7571,31786, 7962,31686, 8351,31581, 8740,31471, 9127,31357, 9512,31238, 9896,31114, 10279,30986, 10660,30853, 11039,30715, 11417,30572, 11793,30425, 12167,30274, 12540,30118, 12910,29957, 13279,29792, 13646,29622, 14010,29448, 14373,29269, 14733,29086, 15091,28899, 15447,28707, 15800,28511, 16151,28311, 16500,28106, 16846,27897, 17190,27684, 17531,27467, 17869,27246, 18205,27020, 18538,26791, 18868,26557, 19195,26320, 19520,26078, 19841,25833, 20160,25583, 20475,25330, 20788,25073, 21097,24812, 21403,24548, 21706,24279, 22006,24008, 22302,23732, 22595,23453, 22884,23170, 23170,22884, 23453,22595, 23732,22302, 24008,22006, 24279,21706, 24548,21403, 24812,21097, 25073,20788, 25330,20475, 25583,20160, 25833,19841, 26078,19520, 26320,19195, 26557,18868, 26791,18538, 27020,18205, 27246,17869, 27467,17531, 27684,17190, 27897,16846, 28106,16500, 28311,16151, 28511,15800, 28707,15447, 28899,15091, 29086,14733, 29269,14373, 29448,14010, 29622,13646, 29792,13279, 29957,12910, 30118,12540, 30274,12167, 30425,11793, 30572,11417, 30715,11039, 30853,10660, 30986,10279, 31114,9896, 31238,9512, 31357,9127, 31471,8740, 31581,8351, 31686,7962, 31786,7571, 31881,7180, 31972,6787, 32058,6393, 32138,5998, 32214,5602, 32286,5205, 32352,4808, 32413,4410, 32470,4011, 32522,3612, 32568,3212, 32610,2811, 32647,2411, 32679,2009, 32706,1608, 32729,1206, 32746,804, 32758,402, 32766};
    int16_t ru_90c[2*128] = {32767, 0,32766, -402,32758, -804,32746, -1206,32729, -1608,32706, -2009,32679, -2411,32647, -2811,32610, -3212,32568, -3612,32522, -4011,32470, -4410,32413, -4808,32352, -5205,32286, -5602,32214, -5998,32138, -6393,32058, -6787,31972, -7180,31881, -7571,31786, -7962,31686, -8351,31581, -8740,31471, -9127,31357, -9512,31238, -9896,31114, -10279,30986, -10660,30853, -11039,30715, -11417,30572, -11793,30425, -12167,30274, -12540,30118, -12910,29957, -13279,29792, -13646,29622, -14010,29448, -14373,29269, -14733,29086, -15091,28899, -15447,28707, -15800,28511, -16151,28311, -16500,28106, -16846,27897, -17190,27684, -17531,27467, -17869,27246, -18205,27020, -18538,26791, -18868,26557, -19195,26320, -19520,26078, -19841,25833, -20160,25583, -20475,25330, -20788,25073, -21097,24812, -21403,24548, -21706,24279, -22006,24008, -22302,23732, -22595,23453, -22884,23170, -23170,22884, -23453,22595, -23732,22302, -24008,22006, -24279,21706, -24548,21403, -24812,21097, -25073,20788, -25330,20475, -25583,20160, -25833,19841, -26078,19520, -26320,19195, -26557,18868, -26791,18538, -27020,18205, -27246,17869, -27467,17531, -27684,17190, -27897,16846, -28106,16500, -28311,16151, -28511,15800, -28707,15447, -28899,15091, -29086,14733, -29269,14373, -29448,14010, -29622,13646, -29792,13279, -29957,12910, -30118,12540, -30274,12167, -30425,11793, -30572,11417, -30715,11039, -30853,10660, -30986,10279, -31114,9896, -31238,9512, -31357,9127, -31471,8740, -31581,8351, -31686,7962, -31786,7571, -31881,7180, -31972,6787, -32058,6393, -32138,5998, -32214,5602, -32286,5205, -32352,4808, -32413,4410, -32470,4011, -32522,3612, -32568,3212, -32610,2811, -32647,2411, -32679,2009, -32706,1608, -32729,1206, -32746,804, -32758,402, -32766};
	
	int interp_table0[14] = {0xB6DA,  0xA491,  0x9248,  0x7FFF,  0x6DB6, 0x5B6D, 0x4924, 0x36DB, 0x2492, 0x1249, 0x0, -0x1249, -0x2492, -0x36DB};
	int16_t interp_table1[14] = {0x3FFF,  0x3FFF,  0x3FFF,  0x7FFF,  0x6DB6, 0x5B6D, 0x4924, 0x36DB, 0x2492, 0x1249, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF};
	int16_t interp_table2[14] = {0x3FFF,  0x3FFF,  0x3FFF,  0x3FFF,  0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF};
	
	float interpf1[14] = { 1.4285714 , 1.2857142 , 1.1428571 ,1,0.8571428,0.7142857,0.5714285,0.4285714,0.2857142,0.1428571,0,-0.1428571,-0.2857142,-0.4285714};
	 
	float interpf2[14] = { -3/7,-2/7,-1/7,0,0.8571428,0.7142857,0.5714285,0.4285714,0.2857142,0.1428571,1,8/7,9/7,10/7};
	
	float interpf3[14] = { 0.5,0.5,0.5,1,0.8571428,0.7142857,0.5714285,0.4285714,0.2857142,0.1428571,0,0.5,0.5,0.5};
	
	int16_t *ru1 = ru_90;
    int16_t *ru2 = ru_90;
	int16_t *ru3 = ru_90;
	int16_t *ru4 = ru_90;
    int16_t current_phase1,current_phase2,phase_idx;
    uint16_t aa,Msc_RS,Msc_RS_idx;
    uint16_t * Msc_idx_ptr;
	int avg[2];
	int Ravg[2];
    int re_sqrt,im_sqrt;
	int re_sqrt1,im_sqrt1;
	int image,real,rv,iv;
	short conj_flag,k,a_idx;
	short Pilot[24];
	short pilot1_re,pilot1_im,pilot2_re,pilot2_im;
	short *rxdataF16,*ul_ref16,*ul_ch16;
	short real_part,image_part;
	short real_part1,image_part1;
	float imagef,realf;
	float real_partf,image_partf;
	float real_partf1,image_partf1;
	float real_partf2,image_partf2;
	float real_partf3,image_partf3;
	float angle1,angle2;
	float pilot1_leng,pilot2_leng;
	float leng;
	float re[600],im[600] ;
    float cos1,sin1,cos2,sin2;
	short OFDM_size = frame_parms->ofdm_symbol_size ;
	short first_carrier_offset = frame_parms->first_carrier_offset ;
	
	short *ul_ch1 = (short*)&ul_ch_estimates[0][ 3*OFDM_size];
	short *ul_ch2 = (short*)&ul_ch_estimates[0][10*OFDM_size];

	int offset,i,Ns,output_shift;
	short rs_re,rs_im,y_re,y_im;
	double ang,ang1;
	double phase1,phase2;
	float avg_amp;
	float current_amp1,current_amp2;

	int16_t alpha[128] = {201, 402, 603, 804, 1006, 1207, 1408, 1610, 1811, 2013, 2215, 2417, 2619, 2822, 3024, 3227, 3431, 3634, 3838, 4042, 4246, 4450, 4655, 4861, 5066, 5272, 5479, 5686, 5893, 6101, 6309, 6518, 6727, 6937, 7147, 7358, 7570, 7782, 7995, 8208, 8422, 8637, 8852, 9068, 9285, 9503, 9721, 9940, 10160, 10381, 10603, 10825, 11049, 11273, 11498, 11725, 11952, 12180, 12410, 12640, 12872, 13104, 13338, 13573, 13809, 14046, 14285, 14525, 14766, 15009, 15253, 15498, 15745, 15993, 16243, 16494, 16747, 17001, 17257, 17515, 17774, 18035, 18298, 18563, 18829, 19098, 19368, 19640, 19915, 20191, 20470, 20750, 21033, 21318, 21605, 21895, 22187, 22481, 22778, 23078, 23380, 23685, 23992, 24302, 24615, 24931, 25250, 25572, 25897, 26226, 26557, 26892, 27230, 27572, 27917, 28266, 28618, 28975, 29335, 29699, 30067, 30440, 30817, 31198, 31583, 31973, 32368, 32767};
	a_idx = 64;
	aa = 0 ;
	
	memset( (int*)&ul_ch_estimates[0][0],0,sizeof(int)*(frame_parms->ofdm_symbol_size)*(frame_parms->symbols_per_tti));
	
	for(Ns = 0 ; Ns < 2 ; Ns ++){
	     //printf("======================%d===================\n",Ns);
		 for( i =0 ; i< frame_parms->N_RB_UL*12 ;i++){
	     	
	       offset = ( first_carrier_offset + i) % OFDM_size ; 
           //offset = ( offset >= 150 && offset < 362)? offset+212: offset;	
	                rs_re = ((short*) ReferenceSignal[aa])[(offset +  Ns*OFDM_size )<<1];	
	                rs_im = ((short*) ReferenceSignal[aa])[((offset +  Ns*OFDM_size )<<1)+1];
	                
                    y_re = ((short*) rxdataF_ext[aa])[(3*OFDM_size + offset +  Ns*7*OFDM_size )<<1];
	                y_im = ((short*) rxdataF_ext[aa])[((3*OFDM_size +offset + Ns*7*OFDM_size )<<1)+1];
	                
                    ((short*) ul_ch_estimates[aa])[( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1]     = 
			         						(int16_t) (((int32_t) rs_re * (int32_t) (y_re) +
			         									(int32_t) rs_im * (int32_t) (y_im))>>15);
			         	  
	                ((short*) ul_ch_estimates[aa])[(( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1)+1] =  
			         						(int16_t) (((int32_t) rs_re * (int32_t) (y_im) -
			         							        (int32_t) rs_im * (int32_t) (y_re))>>15);	
					//printf(" %d re %d im %d \n",i,rs_re,rs_im);									
		}
	}

	for(Ns = 0 ; Ns < 2 ; Ns ++){

		 for( i =0 ; i< frame_parms->N_RB_UL*12 ;i++){
	     	
	       offset = ( first_carrier_offset + i) % OFDM_size ; 
           //offset = ( offset >= 150 && offset < 362)? offset+212: offset;	   
	     
	         rs_re =  ((short*) ul_ch_estimates[aa])[( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1]	;
	         rs_im =  ((short*) ul_ch_estimates[aa])[(( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1)+1];
			 
			 real_partf  = re[300*Ns + i];
			 image_partf = im[300*Ns + i];
	         
             y_re = ((short*) cyclicShift[aa])[(offset +  Ns*OFDM_size )<<1];
	         y_im = ((short*) cyclicShift[aa])[((offset +  Ns*OFDM_size )<<1)+1];
	         
		     if(y_re != 123){
		     	 
                   ((short*) ul_ch_estimates[aa])[( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1]     = 
		          							(int16_t) (((int32_t) rs_re * (int32_t) (y_re) +
		          										(int32_t) rs_im * (int32_t) (y_im))>>15);
		          		  
	               ((short*) ul_ch_estimates[aa])[(( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1)+1] =  
		          							(int16_t) ((-(int32_t) rs_re * (int32_t) (y_im) +
											        (int32_t) rs_im * (int32_t) (y_re))>>15);	
													
                   ((short*) ul_ch_estimates1[aa])[( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1]     = 
		          							(int16_t) (((int32_t) rs_re * (int32_t) (y_re) +
		          										(int32_t) rs_im * (int32_t) (y_im))>>15);
		          		  
	               ((short*) ul_ch_estimates1[aa])[(( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1)+1] =  
		          							(int16_t) ((-(int32_t) rs_re * (int32_t) (y_im) +
											        (int32_t) rs_im * (int32_t) (y_re))>>15);		
 
				   ((short*) ul_ch_estimates2[aa])[( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1]     = 
		          							(int16_t) (((int32_t) rs_re * (int32_t) (y_re) +
		          										(int32_t) rs_im * (int32_t) (y_im))>>15);
		          		  
	               ((short*) ul_ch_estimates2[aa])[(( 3*OFDM_size + offset +  Ns*7*OFDM_size )<<1)+1] =  
		          							(int16_t) ((-(int32_t) rs_re * (int32_t) (y_im) +
											        (int32_t) rs_im * (int32_t) (y_re))>>15);	
													
			}										
		}
	}
	
    memset( (int*)&ReferenceSignal[0][0],0,sizeof(int)*(frame_parms->ofdm_symbol_size)*2);
	memset( (int*)&cyclicShift[0][0]    ,0,sizeof(int)*(frame_parms->ofdm_symbol_size)*2);
	
	avg[0]  = avg[1]  = 0 ;
	Ravg[0] = Ravg[1] = 0 ;
	conj_flag = 0 ;
	
	for( i=0 ; i<  frame_parms->N_RB_UL*12 ;i++){
		
		offset = ( first_carrier_offset + i) % OFDM_size ; 
		
		re_sqrt = ul_ch1[2*offset]*ul_ch1[2*offset];
		im_sqrt = ul_ch1[2*offset + 1 ]*ul_ch1[2*offset + 1];
		
		re_sqrt1 = ul_ch2[2*offset]*ul_ch2[2*offset];
		im_sqrt1 = ul_ch2[2*offset + 1 ]*ul_ch2[2*offset + 1]; 

        avg[0] +=  re_sqrt + im_sqrt ;
		avg[1] +=  re_sqrt1+ im_sqrt1;
		
	}
	
     //printf(" avg[0]  %d \n", avg[0] );
	 avg[0] =  avg[0] / ( nb_rb*12 ) ;
	 avg[1] =  avg[1] / ( nb_rb*12 ) ;
	 
	 avg_amp =( sqrt(avg[1]) - sqrt(avg[0]) ) / 7 ;
	 //printf("avg_amp %f \n",avg_amp);
     avg[0] = cmax(avg[0],avg[1]);
     avg[1] = log2_approx(avg[0]);
     output_shift = cmax(0,avg[1]-10);	

	
	for( i = 0 ; i< frame_parms->N_RB_UL ; i++ ){
		
	 for(k=0 ; k< 12 ; k++){ 	
		
	  offset = ( first_carrier_offset + 12*i +k) % OFDM_size ; 
      //offset = ( offset >= 150 && offset < 362)? offset+212: offset;	
	  
	  //printf(" %d re %d im %d \n",12*i+k,((short*) ul_ch_estimates[aa])[( 1536 + offset )<<1] ,((short*) ul_ch_estimates[aa])[( (1536 + offset )<<1 )+ 1]);
	  //printf(" %d re %d im %d \n",12*i+k,((short*) ul_ch_estimates[aa])[( 5120 + offset )<<1] ,((short*) ul_ch_estimates[aa])[( (5120 + offset )<<1 )+ 1]);
      real  = (((short*) ul_ch_estimates[aa])[( 3*OFDM_size + offset )<<1] )*(((short*) ul_ch_estimates[aa])[( 10*OFDM_size + offset )<<1] ) +
						(((short*) ul_ch_estimates[aa])[(( 3*OFDM_size + offset )<<1)+1])*(((short*) ul_ch_estimates[aa])[(( 10*OFDM_size + offset )<<1)+1]) ;

	  image = (((short*) ul_ch_estimates[aa])[( 3*OFDM_size + offset )<<1] )*(((short*) ul_ch_estimates[aa])[(( 10*OFDM_size + offset )<<1)+1]) -
						(((short*) ul_ch_estimates[aa])[(( 3*OFDM_size + offset )<<1)+1])*(((short*) ul_ch_estimates[aa])[( 10*OFDM_size + offset )<<1]) ;
	  
	  //printf(" %d re %d im %d %d \n",12*i+k,real,image,output_shift);
	  Pilot[2*k]     = ( real  >> output_shift );
	  Pilot[2*k + 1] = ( image >> output_shift );
	  //printf(" %d re %d im %d \n",12*i+k,Pilot[2*k],Pilot[2*k + 1]);
	 }
	  for(k=0;k<4;k++) {
			Pilot[2*k] = ( Pilot[2*k] >> 1 ) + ( Pilot[2*k + 8] >> 1 ) ;
			Pilot[2*k + 1] = ( Pilot[2*k + 1] >> 1 ) + ( Pilot[2*k + 9] >> 1 ) ;
	   }
	  
	  for(k=0;k<4;k++) {
			Pilot[2*k] = ( Pilot[2*k] >> 1 ) + ( Pilot[2*k + 16] >> 1 ) ;
			Pilot[2*k + 1] = ( Pilot[2*k + 1] >> 1 ) + ( Pilot[2*k + 17] >> 1 ) ;
	   }	  
	  	
    Ravg[0] += (Pilot[0] +
                Pilot[2] +
                Pilot[4] +
                Pilot[6])/(nb_rb*4);

    Ravg[1] += (Pilot[1] +
                Pilot[3] + 
                Pilot[5] +
                Pilot[7])/(nb_rb*4);	
    //printf("%d 1 re %d im %d \n",k ,Ravg[0],Ravg[1]); 
	}

     rv = Ravg[0];
     iv = Ravg[1];

     if (iv<0)
       iv = -Ravg[1];
    
     if (rv<iv) {
       rv = iv;
       iv = Ravg[0];
       conj_flag = 1;
     }

      for (k=0; k<6; k++) {
        (iv<(((int32_t)(alpha[a_idx]*rv))>>15)) ? (a_idx -= 32>>k) : (a_idx += 32>>k);
      }
      (conj_flag==1) ? (phase_idx = 127-(a_idx>>1)) : (phase_idx = (a_idx>>1));
    
      if (Ravg[1]<0)
        phase_idx = -phase_idx; 
	 
     delta_phase = phase_idx;
	 //printf("delta_phase %d \n",delta_phase);
	//printf("YAO rv %d iv %d delta_phase %d \n",Ravg[0],Ravg[1],delta_phase);
	
	short *txdata = &UE->common_vars.txdataF[0][eNB->frame_parms.ofdm_symbol_size*14*subframe];
	short tx_re ,tx_im;
/*	
	for(i=0 ; i < frame_parms->N_RB_UL*12 ;i++){
		
		offset = ( first_carrier_offset + i) % OFDM_size ; 
		
		pilot1_re  = ((short*) ul_ch_estimates[aa])[( 3*OFDM_size + offset )<<1];
		pilot1_im  = ((short*) ul_ch_estimates[aa])[((3*OFDM_size + offset )<<1)+1];

		pilot2_re  = ((short*) ul_ch_estimates[aa])[( 10*OFDM_size + offset )<<1];
		pilot2_im  = ((short*) ul_ch_estimates[aa])[((10*OFDM_size + offset )<<1)+1];
		

		real_partf 	= (float)pilot1_re*(float)pilot2_re + (float)pilot1_im*(float)pilot2_im ;
	    image_partf = (float)pilot2_re*(-(float)pilot1_im) + (float)pilot1_re*(float)pilot2_im;
		
		pilot1_leng = sqrt(pilot1_re*pilot1_re + pilot1_im*pilot1_im);
		pilot2_leng = sqrt(pilot2_re*pilot2_re + pilot2_im*pilot2_im);		
		
		ang1 =atan2(image_partf,real_partf);
		
		//printf("ang1 %f \n",ang1);
		
		for( k=0 ; k<14 ; k++){
		
			if(k!=3 && k!= 10 ){
				
				
		        phase1 = (ang1/7)*(k-3 );
                phase2 = (ang1/7)*(k-10);	
				
				//printf("%d phase1 %f phase2 %f \n",k,phase1,phase2);
				
				real_partf  =  (float)pilot1_re*cos(phase1)  - (float)pilot1_im*sin(phase1)   ; 
				image_partf =  (float)pilot1_re*sin(phase1)  + (float)pilot1_im*cos(phase1)   ;  

				real_partf1  = (float)pilot2_re*cos(phase2)  - (float)pilot2_im*sin(phase2)   ; 
				image_partf1 = (float)pilot2_re*sin(phase2)  + (float)pilot2_im*cos(phase2)   ; 
				
				//printf(" pilot1 %f +(%f)*j  pilot2  %f +(%f)*j \n",real_partf,image_partf,real_partf1,image_partf1);
				
				real_partf2  =  (float)real_partf*0.5;
				image_partf2 =  (float)image_partf*0.5;
				               
				real_partf3  =  (float)real_partf1*0.5;
                image_partf3 =  (float)image_partf1*0.5;	
				
				realf = real_partf2  + real_partf3 ;
				imagef= image_partf2 + image_partf3;
										
				ang =atan2(imagef,realf);

				//realf = ( pilot1_leng*interpf3[k] + pilot2_leng*interpf3[13-k] )*cos(ang);
				
				//imagef = ( pilot1_leng*interpf3[k] + pilot2_leng*interpf3[13-k])*sin(ang);				
					
				
				//((short*) ul_ch_estimates1[aa])[( OFDM_size*k + offset )<<1]    = (short)realf ;
                //((short*) ul_ch_estimates1[aa])[((OFDM_size*k + offset )<<1)+1] = (short)imagef;

			}
		}
	}
	
*/	
	 
	for( k=0 ; k<14 ; k++){
		
	  if(k!=3 && k!= 10 ){
           
		   current_phase1 = (delta_phase/7)*(k-3 );
           current_phase2 = (delta_phase/7)*(k-10);	
		   
		   (current_phase1 > 0) ? (ru1 = ru_90) : (ru1 = ru_90c);
           (current_phase2 > 0) ? (ru2 = ru_90) : (ru2 = ru_90c);
		   		   
           //printf("phase1 %d phase2 %d \n",phase1,phase2);
           // take absolute value and clip
           current_phase1 = cmin(abs(current_phase1),127);
           current_phase2 = cmin(abs(current_phase2),127);

		    for(i=0 ; i < frame_parms->N_RB_UL*12 ;i++){
				
				offset = ( first_carrier_offset + i) % OFDM_size ; 
			    //offset = ( offset >= 150 && offset < 362)? offset+212: offset;	
				
				pilot1_re  = ((short*) ul_ch_estimates[aa])[( 3*OFDM_size + offset )<<1];
				pilot1_im  = ((short*) ul_ch_estimates[aa])[((3*OFDM_size + offset )<<1)+1];

				pilot2_re  = ((short*) ul_ch_estimates[aa])[( 10*OFDM_size + offset )<<1];
				pilot2_im  = ((short*) ul_ch_estimates[aa])[((10*OFDM_size + offset )<<1)+1];	
		
/*	
				real_part  = ( ( pilot1_re*ru1[2*current_phase1] + pilot1_im*-ru1[2*current_phase1 +1] )>>15);  
				image_part = ( ( pilot1_re*ru1[2*current_phase1 +1] + pilot1_im*ru1[2*current_phase1]  )>>15);  
				
				real_part1  = ( ( pilot2_re*ru2[2*current_phase2] + pilot2_im*-ru2[2*current_phase2 + 1] )>>15);
				image_part1 = ( ( pilot2_re*ru2[2*current_phase2 + 1] + pilot2_im*ru2[2*current_phase2]  )>>15);

				real_part =  (((real_part*interp_table2[k])>>16)<<1);
				image_part = (((image_part*interp_table2[k])>>16)<<1);
			    
				real_part1  = (((real_part1*interp_table2[13-k])>>16)<<1);
				image_part1 = (((image_part1*interp_table2[13-k])>>16)<<1);
				
				((short*) ul_ch_estimates[aa])[( OFDM_size*k + offset )<<1]    = real_part  + real_part1 ;
				((short*) ul_ch_estimates[aa])[((OFDM_size*k + offset )<<1)+1] = image_part + image_part1;
*/		
				real_partf  =  (float)pilot1_re*((float)ru1[2*current_phase1]/32767)    + (float)pilot1_im*-((float)ru1[2*current_phase1 +1]/32767) ; 
				image_partf =  (float)pilot1_re*((float)ru1[2*current_phase1 +1]/32767) + (float)pilot1_im*((float)ru1[2*current_phase1]/32767)     ;  

				real_partf1  = (float)pilot2_re*((float)ru2[2*current_phase2]/32767) +     (float)pilot2_im*-((float)ru2[2*current_phase2 + 1]/32767);
				image_partf1 = (float)pilot2_re*((float)ru2[2*current_phase2 + 1]/32767) + (float)pilot2_im*((float)ru2[2*current_phase2]/32767)     ;
				
			    real_partf2  =  (float)real_partf*0.5;
				image_partf2 =  (float)image_partf*0.5;
				               
				real_partf3  =  (float)real_partf1*0.5;
                image_partf3 =  (float)image_partf1*0.5;
				
				realf = real_partf2  + real_partf3 ;
				imagef= image_partf2 + image_partf3;
								
				leng = sqrt(realf*realf + imagef*imagef);
				
				ang = atan2(imagef,realf);
				
				((short*) ul_ch_estimates[aa])[( OFDM_size*k + offset )<<1]    =  (short)realf ;
				((short*) ul_ch_estimates[aa])[((OFDM_size*k + offset )<<1)+1] =  (short)imagef;
				
				pilot1_leng = sqrt(pilot1_re*pilot1_re + pilot1_im*pilot1_im);
				pilot2_leng = sqrt(pilot2_re*pilot2_re + pilot2_im*pilot2_im);
				
				//realf = ( pilot1_leng*interpf3[k] + pilot2_leng*interpf3[13-k] )*cos(ang);
				//imagef = ( pilot1_leng*interpf3[k] + pilot2_leng*interpf3[13-k])*sin(ang);
				//printf("%d %f\n",k,( pilot1_leng*interpf1[k] + pilot2_leng*interpf1[13-k] ));
				
				real_partf2  = (float)real_partf*interpf3[k];
				image_partf2 = (float)image_partf*interpf3[k];
				//            
				real_partf3  = (float)real_partf1*interpf3[13-k];
                image_partf3 = (float)image_partf1*interpf3[13-k];	
				
				realf = real_partf2  + real_partf3 ;
				imagef= image_partf2 + image_partf3;
				
				((short*) ul_ch_estimates1[aa])[( OFDM_size*k + offset )<<1]    = (short)realf ;
                ((short*) ul_ch_estimates1[aa])[((OFDM_size*k + offset )<<1)+1] = (short)imagef;
					
				real_partf2  = (float)real_partf*interpf1[k]; 
				image_partf2 = (float)image_partf*interpf1[k];
				//            
				real_partf3  = (float)real_partf1*interpf1[13-k];
                image_partf3 = (float)image_partf1*interpf1[13-k];	
				
				realf = real_partf2  + real_partf3 ;
				imagef= image_partf2 + image_partf3;
				
				((short*) ul_ch_estimates2[aa])[( OFDM_size*k + offset )<<1]    = (short)realf ;
                ((short*) ul_ch_estimates2[aa])[((OFDM_size*k + offset )<<1)+1] = (short)imagef;

		}	 
	  }
	}
    return 0 ; 
}	

void ul_ulsch_freq_equalization(PHY_VARS_eNB *eNB,
				       eNB_rxtx_proc_t *proc,
								uint8_t eNB_id,
								//uint8_t UE_id,
								uint8_t subframe,
								uint8_t nb_rb
								)
{
	
int16_t inv_ch[256*8] = {512,512,512,512,512,512,512,512,
                         256,256,256,256,256,256,256,256,
                         170,170,170,170,170,170,170,170,
                         128,128,128,128,128,128,128,128,
                         102,102,102,102,102,102,102,102,
                         85,85,85,85,85,85,85,85,
                         73,73,73,73,73,73,73,73,
                         64,64,64,64,64,64,64,64,
                         56,56,56,56,56,56,56,56,
                         51,51,51,51,51,51,51,51,
                         46,46,46,46,46,46,46,46,
                         42,42,42,42,42,42,42,42,
                         39,39,39,39,39,39,39,39,
                         36,36,36,36,36,36,36,36,
                         34,34,34,34,34,34,34,34,
                         32,32,32,32,32,32,32,32,
                         30,30,30,30,30,30,30,30,
                         28,28,28,28,28,28,28,28,
                         26,26,26,26,26,26,26,26,
                         25,25,25,25,25,25,25,25,
                         24,24,24,24,24,24,24,24,
                         23,23,23,23,23,23,23,23,
                         22,22,22,22,22,22,22,22,
                         21,21,21,21,21,21,21,21,
                         20,20,20,20,20,20,20,20,
                         19,19,19,19,19,19,19,19,
                         18,18,18,18,18,18,18,18,
                         18,18,18,18,18,18,18,18,
                         17,17,17,17,17,17,17,17,
                         17,17,17,17,17,17,17,17,
                         16,16,16,16,16,16,16,16,
                         16,16,16,16,16,16,16,16,
                         15,15,15,15,15,15,15,15,
                         15,15,15,15,15,15,15,15,
                         14,14,14,14,14,14,14,14,
                         14,14,14,14,14,14,14,14,
                         13,13,13,13,13,13,13,13,
                         13,13,13,13,13,13,13,13,
                         13,13,13,13,13,13,13,13,
                         12,12,12,12,12,12,12,12,
                         12,12,12,12,12,12,12,12,
                         12,12,12,12,12,12,12,12,
                         11,11,11,11,11,11,11,11,
                         11,11,11,11,11,11,11,11,
                         11,11,11,11,11,11,11,11,
                         11,11,11,11,11,11,11,11,
                         10,10,10,10,10,10,10,10,
                         10,10,10,10,10,10,10,10,
                         10,10,10,10,10,10,10,10,
                         10,10,10,10,10,10,10,10,
                         10,10,10,10,10,10,10,10,
                         9,9,9,9,9,9,9,9,
                         9,9,9,9,9,9,9,9,
                         9,9,9,9,9,9,9,9,
                         9,9,9,9,9,9,9,9,
                         9,9,9,9,9,9,9,9,
                         8,8,8,8,8,8,8,8,
                         8,8,8,8,8,8,8,8,
                         8,8,8,8,8,8,8,8,
                         8,8,8,8,8,8,8,8,
                         8,8,8,8,8,8,8,8,
                         8,8,8,8,8,8,8,8,
                         8,8,8,8,8,8,8,8,
                         8,8,8,8,8,8,8,8,
                         7,7,7,7,7,7,7,7,
                         7,7,7,7,7,7,7,7,
                         7,7,7,7,7,7,7,7,
                         7,7,7,7,7,7,7,7,
                         7,7,7,7,7,7,7,7,
                         7,7,7,7,7,7,7,7,
                         7,7,7,7,7,7,7,7,
                         7,7,7,7,7,7,7,7,
                         7,7,7,7,7,7,7,7,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         6,6,6,6,6,6,6,6,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         5,5,5,5,5,5,5,5,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         4,4,4,4,4,4,4,4,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         3,3,3,3,3,3,3,3,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                         2,2,2,2,2,2,2,2,
                        };
	
	LTE_DL_FRAME_PARMS *frame_parms = &eNB->frame_parms;
    //LTE_eNB_PUSCH *pusch_vars = eNB->pusch_vars[UE_id];
	LTE_eNB_COMMON *common_vars = &eNB->common_vars;
	
	int32_t **ul_ch_estimates =common_vars->drs_ch_estimates_ISIP[eNB_id];
	int32_t **rxdataF_comp    =common_vars->rxdata_comp[eNB_id];
	int32_t **ul_ch_mag	      =common_vars->ul_ch_mag[eNB_id];
	int32_t **rxdataF_ext     =common_vars->rxdataF[eNB_id]; 	
	
	short *ul_ch16,output_shift;
	int avg,avgs,i,j,k,offset;
	int aa = 0 ;
	short amp,real,image;
	short harq_pid = subframe2harq_pid(frame_parms,proc->frame_rx,subframe);
    //short Qm = get_Qm_ul(eNB->ulsch[UE_id]->harq_processes[harq_pid]->mcs);
	
	short OFDM_size = frame_parms->ofdm_symbol_size ;
	short first_carrier_offset = frame_parms->first_carrier_offset ;
	

// channel level
    ul_ch16 = (short*)ul_ch_estimates[aa];
	
	avg = 0 ;
	for(i=0;i< frame_parms->N_RB_UL*12 ;i++) {
		offset = ( first_carrier_offset + i) % OFDM_size ;
		avg +=(ul_ch16[2*offset]*ul_ch16[2*offset] + ul_ch16[2*offset+1]*ul_ch16[2*offset+1]);
	}
	//printf("avg %d \n",avg);
	avg = avg/(nb_rb*12);
	
	avgs = 0;
	avgs = cmax(avgs,avg);
	output_shift = (log2_approx(avgs)/2)+ log2_approx(frame_parms->nb_antennas_rx-1)+4;
	
// compensation

   for(k=0 ; k<14 ; k++){
	
	if(k!=3 && k!= 10){
			
		 for(i=0 ; i < frame_parms->N_RB_UL*12 ;i++){
			
			offset = ( first_carrier_offset + i) % OFDM_size ; 
			//offset = ( offset >= 150 && offset < 362)? offset+212: offset;	
			
			amp = ((((short*) ul_ch_estimates[aa])[( OFDM_size*k + offset )<<1]*((short*) ul_ch_estimates[aa])[( OFDM_size*k + offset )<<1]  
					+ ((short*) ul_ch_estimates[aa])[((OFDM_size*k + offset )<<1)+1]*((short*) ul_ch_estimates[aa])[((OFDM_size*k + offset )<<1)+1]) >> output_shift );
			
			((short*) ul_ch_mag[aa])[( OFDM_size*k + offset )<<1]    = amp ;
            ((short*) ul_ch_mag[aa])[((OFDM_size*k + offset )<<1)+1] = amp ;
			//printf("1L%d %d re %d im %d \n",k,i,((short*) ul_ch_mag[aa])[( 300*k + i )<<1], ((short*) ul_ch_mag[aa])[((300*k + i )<<1)+1]);
            real  = (((((short*) rxdataF_ext[aa])[( OFDM_size*k  + offset )<<1] )*(((short*) ul_ch_estimates[aa])[( OFDM_size*k  + offset )<<1] ) +
	        				(((short*) rxdataF_ext[aa])[(( OFDM_size*k  + offset )<<1)+1])*(((short*) ul_ch_estimates[aa])[(( OFDM_size*k  + offset )<<1)+1])) >> output_shift);
            
	        image = (((((short*) ul_ch_estimates[aa])[( OFDM_size*k  + offset )<<1] )*(((short*) rxdataF_ext[aa])[(( OFDM_size*k  + offset )<<1)+1]) -
	        				(((short*) ul_ch_estimates[aa])[(( OFDM_size*k  + offset )<<1)+1])*(((short*) rxdataF_ext[aa])[( OFDM_size*k  + offset )<<1])) >> output_shift);
			
			//printf("L%d %d re %d im %d \n",k,i,((short*) rxdataF_ext[aa])[( OFDM_size*k  + offset )<<1], ((short*) rxdataF_ext[aa])[(( OFDM_size*k  + offset )<<1)+1] );
			//jitter				
			switch(i%4){
				
				case 0 : real =real + 1;
						 break;
				case 1 : image=image+ 1;
						 break;
				case 2 : image=image+ 1;
						 break;
				case 3 : real =real + 1;
						 break;
				default :
					break;
			}
			//	short jitter[8]  = {1,0,0,1,0,1,1,0};
			((short*) rxdataF_comp[aa])[( OFDM_size*k + offset )<<1]    = real;	 			
			((short*) rxdataF_comp[aa])[((OFDM_size*k + offset )<<1)+1] = image;	
			//printf("1L%d %d re %d im %d \n",k,i,((short*) rxdataF_comp[aa])[( 512*k + offset )<<1], ((short*) rxdataF_comp[aa])[((512*k + offset )<<1)+1]);	
	    }
//equalizer	

		 for(i=0 ; i < frame_parms->N_RB_UL*3 ;i++){
			
			offset = ( first_carrier_offset + 4*i) % OFDM_size ; 
			//offset = ( offset >= 150 && offset < 362)? offset+212: offset;
			
			amp = ((short*) ul_ch_mag[aa])[( OFDM_size*k + offset )<<1] ;
			//printf("1 amp %d\n",amp);
			if (amp>255)
					amp=255;
				//printf("1 amp %d\n",amp);
			for( j = 0 ; j < 4 ; j++){
			    
				//printf("1L%d %d re %d im %d \n",k,4*i+j,((short*) rxdataF_comp[aa])[( 512*k + offset )<<1], ((short*) rxdataF_comp[aa])[((512*k + offset )<<1)+1]);		
			    
				((short*) rxdataF_comp[aa])[( OFDM_size*k + offset )<<1]    = (((short*) rxdataF_comp[aa])[( OFDM_size*k + offset )<<1])*(inv_ch[8*amp]) ;  
			    ((short*) rxdataF_comp[aa])[((OFDM_size*k + offset )<<1)+1] = (((short*) rxdataF_comp[aa])[((OFDM_size*k + offset )<<1)+1])*(inv_ch[8*amp]) ;
				
				//printf("1L%d %d re %d im %d \n",k,4*i+j,((short*) rxdataF_comp[aa])[( 512*k + offset )<<1], ((short*) rxdataF_comp[aa])[((512*k + offset )<<1)+1]);
/*			    if (Qm==4){
                  ((short*) ul_ch_mag[aa])[( 512*k + offset )<<1]    =324;  // this is 512*2/sqrt(10)
				  ((short*) ul_ch_mag[aa])[((512*k + offset )<<1)+1] =324;	
				}
                else {
                  ((short*) ul_ch_mag[aa])[( 512*k + offset )<<1]     =316;    // this is 512*4/sqrt(42)
				  ((short*) ul_ch_mag[aa])[((512*k + offset )<<1)+1]  =316;
                  ((short*) ul_ch_magb[aa])[( 512*k + offset )<<1]    =158;    // this is 512*2/sqrt(42)			    
			      ((short*) ul_ch_magb[aa])[((512*k + offset )<<1)+1] =158;  
				}*/
				 offset = ( offset + 1) % OFDM_size;
				 //offset = ( offset >= 150 && offset < 362)? offset+212: offset;				 
			  
		        }
            }
	    }
    } 
}	

/////////////////////////////////////////////////////////////////////////////////////////

void phy_procedures_eNB_uespec_RX(PHY_VARS_eNB *eNB,eNB_rxtx_proc_t *proc,const relaying_type_t r_type)
{
  //RX processing for ue-specific resources (i
  UNUSED(r_type);
  LTE_eNB_COMMON *common_vars = &eNB->common_vars;
  
  uint32_t i,j,k;
  uint32_t harq_pid, harq_idx, round;
  uint8_t nPRS;
  int sync_pos;
  uint16_t rnti=0;
  uint8_t access_mode;
  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  int total_nb_rb = 0 ;
  int est[300];
  short RB[fp->N_RB_UL],flag_RS;
  short Qm,flag,Ns ;
   
  //int **est1 = common_vars->drs_ch_estimates_ISIP[0] ;
  //int *ptr;
  struct timespec tt1, tt2;	
  
  const int subframe = proc->subframe_rx;
  const int frame    = proc->frame_rx;
  int offset         = (proc == &eNB->proc.proc_rxtx[0]) ? 0 : 1;
  //int temp_subframe_scheduling_flag[NUMBER_OF_UE_MAX];

  if ((fp->frame_type == TDD) && (subframe_select(fp,subframe)!=SF_UL)) return;

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_PROCEDURES_ENB_RX_UESPEC+offset, 1 );

#ifdef DEBUG_PHY_PROC
  LOG_D(PHY,"[eNB %d] Frame %d: Doing phy_procedures_eNB_uespec_RX(%d)\n",eNB->Mod_id,frame, subframe);
#endif

  T(T_ENB_PHY_UL_TICK, T_INT(eNB->Mod_id), T_INT(frame), T_INT(subframe));

  T(T_ENB_PHY_INPUT_SIGNAL, T_INT(eNB->Mod_id), T_INT(frame), T_INT(subframe), T_INT(0),
    T_BUFFER(&eNB->common_vars.rxdata[0][0][subframe*eNB->frame_parms.samples_per_tti],
             eNB->frame_parms.samples_per_tti * 4));

  eNB->rb_mask_ul[0]=0;
  eNB->rb_mask_ul[1]=0;
  eNB->rb_mask_ul[2]=0;
  eNB->rb_mask_ul[3]=0; 
/**************************************************/ 
  //初始化RX Global 參數
  eNB->flag_pucch = 0;
  eNB->flag_pusch = 0;
  eNB->flag_decoding_parameters = 0;
  eNB->flag_turbo = 0;
  eNB->flag_viterbi = 0;
  eNB->complete_pucch = 0;
  eNB->complete_viterbi_thread = 0;
  eNB->complete_turbo_thread = 0;//Now unused
  for(i=0;i<NUMBER_OF_UE_MAX;i++) {
	  eNB->ret[i] = 0;
	  eNB->Q_CQI[i] = 0;
	  eNB->turbo_status[i] = 0;
	  eNB->complete_pusch[i] = 0;
	  eNB->complete_viterbi_parameter[i] = 0;
	  eNB->complete_turbo_parameter[i] = 0;
	  eNB->complete_viterbi[i] = 0;
	  eNB->complete_turbo[i] = 0;
  }
/**************************************************/
  for(i=0 ; i< fp->N_RB_UL ; i ++) RB[i] = 0 ;
  
  // Check for active processes in current subframe
  harq_pid = subframe2harq_pid(fp,
                               frame,subframe);
  // reset the cba flag used for collision detection
  for (i=0; i < NUM_MAX_CBA_GROUP; i++) {
    eNB->cba_last_reception[i]=0;
  }
  //16
  flag = 0 ;
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_BEFORE_PUCCH,1);
//clock_gettime(CLOCK_REALTIME, &tt1);
  for (i=0; i<NUMBER_OF_UE_MAX; i++) {
	  
// check for Msg3

    if (eNB->mac_enabled==1) {
      if (eNB->UE_stats[i].mode == RA_RESPONSE) {
	process_Msg3(eNB,proc,i,harq_pid);
      }
    }
	
	eNB->pusch_stats_rb[i][(frame*10)+subframe] = -63;
    eNB->pusch_stats_round[i][(frame*10)+subframe] = 0;
    eNB->pusch_stats_mcs[i][(frame*10)+subframe] = -63;
	

    if ((eNB->ulsch[i]) &&
        (eNB->ulsch[i]->rnti>0) &&
        (eNB->ulsch[i]->harq_processes[harq_pid]->subframe_scheduling_flag==1)){
		
		flag_RS = 0 ;	
	
		if( RB[ eNB->ulsch[i]->harq_processes[harq_pid]->first_rb ] == 0 ){	
			flag_RS = 1 ;
			
			total_nb_rb += eNB->ulsch[i]->harq_processes[harq_pid]->nb_rb ;
			
			for(k=0 ; k< eNB->ulsch[i]->harq_processes[harq_pid]->nb_rb ; k ++ ) 
						RB[ (eNB->ulsch[i]->harq_processes[harq_pid]->first_rb ) + k ] = 1 ;
				
		}
		else flag_RS = 0 ;
		
		if(flag_RS == 1 && eNB->abstraction_flag==0){	
		       for(Ns = 0 ; Ns < 2 ; Ns ++){	
			   
	            		Build_RS(eNB,
	            		         proc,
	            				 eNB->ulsch,
	            				 0,
                                //eNB_id,
	            				 i,
	            		         Ns,
                                 subframe,
								 harq_pid);
	            } 	
		    flag = 1 ; 
			}						
	    }
  }	
	
	if(flag){	
	    lte_ul_channel_estimation_ISIP(eNB,
	    							   proc,
	    							   0,
                                       //eNB_id,
	    							   subframe,
									   total_nb_rb);
	            ul_ulsch_freq_equalization(eNB,
	                                       proc,
	            						   0,
                                           //eNB_id,
	            						   subframe,
										   total_nb_rb);
										   			   
	}		
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_BEFORE_PUCCH,0);	
  // Do PUCCH processing first
/*for (i=0; i<NUMBER_OF_UE_MAX; i++) {
    pucch_procedures(eNB,proc,i,harq_pid);
  }*/

//Trigger PUCCH & PUSCH and wait for PUCCH in the end of phy->mac
eNB->flag_pucch = 1;
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PUCCH,1);
eNB->flag_pusch = 1;
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PUSCH,1);
//Trigger decoding_parameters & viterbi & turbo and use busy waiting to get jobs
eNB->flag_decoding_parameters = 1;
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_decoding_para,1);
eNB->flag_viterbi = 1;
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_viterbi,1);
eNB->flag_turbo = 1;
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_turbo,1);
/*for (i=0; i<NUMBER_OF_UE_MAX; i++) {
   
	if ((eNB->ulsch[i]) &&
        (eNB->ulsch[i]->rnti>0) &&
        (eNB->ulsch[i]->harq_processes[harq_pid]->subframe_scheduling_flag==1)){
    // UE is has ULSCH scheduling
      round = eNB->ulsch[i]->harq_processes[harq_pid]->round;
      for (int rb=0;
           rb<=eNB->ulsch[i]->harq_processes[harq_pid]->nb_rb;
	   rb++) {
	int rb2 = rb+eNB->ulsch[i]->harq_processes[harq_pid]->first_rb;
	eNB->rb_mask_ul[rb2>>5] |= (1<<(rb2&31));
      }
		
      if (eNB->ulsch[i]->Msg3_flag == 1) {
        LOG_D(PHY,"[eNB %d] frame %d, subframe %d: Scheduling ULSCH Reception for Msg3 in Sector %d\n",
              eNB->Mod_id,
              frame,
              subframe,
              eNB->UE_stats[i].sector);
	VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_ENB_ULSCH_MSG3,1);
      } else {

        LOG_D(PHY,"[eNB %d] frame %d, subframe %d: Scheduling ULSCH Reception for UE %d Mode %s\n",
              eNB->Mod_id,
              frame,
              subframe,
              i,
              mode_string[eNB->UE_stats[i].mode]);
      }


      nPRS = fp->pusch_config_common.ul_ReferenceSignalsPUSCH.nPRS[subframe<<1];

      eNB->ulsch[i]->cyclicShift = (eNB->ulsch[i]->harq_processes[harq_pid]->n_DMRS2 + fp->pusch_config_common.ul_ReferenceSignalsPUSCH.cyclicShift +
				    nPRS)%12;

      if (fp->frame_type == FDD ) {
        int sf = (subframe<4) ? (subframe+6) : (subframe-4);

        if (eNB->dlsch[i][0]->subframe_tx[sf]>0) { // we have downlink transmission
          eNB->ulsch[i]->harq_processes[harq_pid]->O_ACK = 1;
        } else {
          eNB->ulsch[i]->harq_processes[harq_pid]->O_ACK = 0;
        }
      }

      LOG_D(PHY,
            "[eNB %d][PUSCH %d] Frame %d Subframe %d Demodulating PUSCH: dci_alloc %d, rar_alloc %d, round %d, first_rb %d, nb_rb %d, mcs %d, TBS %d, rv %d, cyclic_shift %d (n_DMRS2 %d, cyclicShift_common %d, nprs %d), O_ACK %d \n",
            eNB->Mod_id,harq_pid,frame,subframe,
            eNB->ulsch[i]->harq_processes[harq_pid]->dci_alloc,
            eNB->ulsch[i]->harq_processes[harq_pid]->rar_alloc,
            eNB->ulsch[i]->harq_processes[harq_pid]->round,
            eNB->ulsch[i]->harq_processes[harq_pid]->first_rb,
            eNB->ulsch[i]->harq_processes[harq_pid]->nb_rb,
            eNB->ulsch[i]->harq_processes[harq_pid]->mcs,
            eNB->ulsch[i]->harq_processes[harq_pid]->TBS,
            eNB->ulsch[i]->harq_processes[harq_pid]->rvidx,
            eNB->ulsch[i]->cyclicShift,
            eNB->ulsch[i]->harq_processes[harq_pid]->n_DMRS2,
            fp->pusch_config_common.ul_ReferenceSignalsPUSCH.cyclicShift,
            nPRS,
            eNB->ulsch[i]->harq_processes[harq_pid]->O_ACK);
			
      eNB->pusch_stats_rb[i][(frame*10)+subframe] = eNB->ulsch[i]->harq_processes[harq_pid]->nb_rb;
      eNB->pusch_stats_round[i][(frame*10)+subframe] = eNB->ulsch[i]->harq_processes[harq_pid]->round;
      eNB->pusch_stats_mcs[i][(frame*10)+subframe] = eNB->ulsch[i]->harq_processes[harq_pid]->mcs;
      start_meas(&eNB->ulsch_demodulation_stats);
	  //printf("eNB %d\n",eNB->UE_stats[i].sector);	
      if (eNB->abstraction_flag==0) {
		//printf("OAI rx_ulsch \n");
		rx_ulsch(eNB,proc,
                 eNB->UE_stats[i].sector,  // this is the effective sector id
                 i,
                 eNB->ulsch,
                 0);
      }

#ifdef PHY_ABSTRACTION
      else {
        rx_ulsch_emul(eNB,proc,
                      eNB->UE_stats[i].sector,  // this is the effective sector id
                      i);
      }

#endif
      stop_meas(&eNB->ulsch_demodulation_stats);
    }
	//Trigger rx_ulsch[UE_id] down
	eNB->complete_pusch[i] = 1;
  }*/
  /*for (i=0; i<NUMBER_OF_UE_MAX; i++) {
	if ((eNB->ulsch[i]) &&
        (eNB->ulsch[i]->rnti>0) &&
        (eNB->ulsch[i]->harq_processes[harq_pid]->subframe_scheduling_flag==1)){*/
/*      start_meas(&eNB->ulsch_decoding_stats);

      if (eNB->abstraction_flag == 0) {
        ulsch_decoding(eNB,proc,
                             i,//nothing
                             0, // control_only_flag
                             i,//nothing
							 i);//nothing
		
      }*/
/*
#ifdef PHY_ABSTRACTION
      else {
        eNB->ret[i] = ulsch_decoding_emul(eNB,
				  proc,
                                  i,
                                  &rnti);
      }

#endif*/
    //  stop_meas(&eNB->ulsch_decoding_stats);
  //while(eNB->complete_viterbi_thread!=1);
  //VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_viterbi,0);
  //while(eNB->complete_turbo_thread!=NUMBER_OF_UE_MAX);
  //VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_turbo,0);
  uint8_t complete_decoding = 0,wait_decoding[NUMBER_OF_UE_MAX];
  for(i=0;i<NUMBER_OF_UE_MAX;i++) wait_decoding[i]=1;
//---------------------Split PHY to MAC by for(NUMBER_OF_UE_MAX)-----------------------------
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_p2m,1);
while(complete_decoding!=NUMBER_OF_UE_MAX){
  for (i=0; i<NUMBER_OF_UE_MAX; i++) {
	if(eNB->complete_viterbi[i]==1&&eNB->complete_turbo[i]==1&&wait_decoding[i]==1){
		wait_decoding[i] = 0;
		complete_decoding = complete_decoding+1;
		if ((eNB->ulsch[i]) &&
			(eNB->ulsch[i]->rnti>0) &&
			(eNB->ulsch[i]->harq_processes[harq_pid]->subframe_scheduling_flag==1)){
      LOG_D(PHY,"[eNB %d][PUSCH %d] frame %d subframe %d RNTI %x RX power (%d,%d) RSSI (%d,%d) N0 (%d,%d) dB ACK (%d,%d), decoding iter %d\n",
            eNB->Mod_id,harq_pid,
            frame,subframe,
            eNB->ulsch[i]->rnti,
            dB_fixed(eNB->pusch_vars[i]->ulsch_power[0]),
            dB_fixed(eNB->pusch_vars[i]->ulsch_power[1]),
            eNB->UE_stats[i].UL_rssi[0],
            eNB->UE_stats[i].UL_rssi[1],
            eNB->measurements->n0_power_dB[0],
            eNB->measurements->n0_power_dB[1],
            eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[0],
            eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[1],
            eNB->ret[i]);

      //compute the expected ULSCH RX power (for the stats)
      eNB->ulsch[(uint32_t)i]->harq_processes[harq_pid]->delta_TF =
        get_hundred_times_delta_IF_eNB(eNB,i,harq_pid, 0); // 0 means bw_factor is not considered

      eNB->UE_stats[i].ulsch_decoding_attempts[harq_pid][eNB->ulsch[i]->harq_processes[harq_pid]->round]++;
#ifdef DEBUG_PHY_PROC
      LOG_D(PHY,"[eNB %d][PUSCH %d] frame %d subframe %d UE %d harq_pid %d Clearing subframe_scheduling_flag\n",
            eNB->Mod_id,harq_pid,frame,subframe,i,harq_pid);
#endif
      eNB->ulsch[i]->harq_processes[harq_pid]->subframe_scheduling_flag=0;

      if (eNB->ulsch[i]->harq_processes[harq_pid]->cqi_crc_status == 1) {
#ifdef DEBUG_PHY_PROC
        //if (((frame%10) == 0) || (frame < 50))
        print_CQI(eNB->ulsch[i]->harq_processes[harq_pid]->o,eNB->ulsch[i]->harq_processes[harq_pid]->uci_format,0,fp->N_RB_DL);
#endif
        extract_CQI(eNB->ulsch[i]->harq_processes[harq_pid]->o,
                    eNB->ulsch[i]->harq_processes[harq_pid]->uci_format,
                    &eNB->UE_stats[i],
                    fp->N_RB_DL,
                    &rnti, &access_mode);
        eNB->UE_stats[i].rank = eNB->ulsch[i]->harq_processes[harq_pid]->o_RI[0];

      }

      if (eNB->ulsch[i]->Msg3_flag == 1)
	VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_ENB_ULSCH_MSG3,0);

      if (eNB->ret[i] == (1+MAX_TURBO_ITERATIONS)) {
        T(T_ENB_PHY_ULSCH_UE_NACK, T_INT(eNB->Mod_id), T_INT(frame), T_INT(subframe), T_INT(i), T_INT(eNB->ulsch[i]->rnti),
          T_INT(harq_pid));

        eNB->UE_stats[i].ulsch_round_errors[harq_pid][eNB->ulsch[i]->harq_processes[harq_pid]->round]++;
        eNB->ulsch[i]->harq_processes[harq_pid]->phich_active = 1;
        eNB->ulsch[i]->harq_processes[harq_pid]->phich_ACK = 0;
        eNB->ulsch[i]->harq_processes[harq_pid]->round++;

        LOG_D(PHY,"[eNB][PUSCH %d] Increasing to round %d\n",harq_pid,eNB->ulsch[i]->harq_processes[harq_pid]->round);

        if (eNB->ulsch[i]->Msg3_flag == 1) {
          LOG_D(PHY,"[eNB %d/%d][RAPROC] frame %d, subframe %d, UE %d: Error receiving ULSCH (Msg3), round %d/%d\n",
                eNB->Mod_id,
                eNB->CC_id,
                frame,subframe, i,
                eNB->ulsch[i]->harq_processes[harq_pid]->round-1,
                fp->maxHARQ_Msg3Tx-1);

	  LOG_D(PHY,"[eNB %d][PUSCH %d] frame %d subframe %d RNTI %x RX power (%d,%d) RSSI (%d,%d) N0 (%d,%d) dB ACK (%d,%d), decoding iter %d\n",
		eNB->Mod_id,harq_pid,
		frame,subframe,
		eNB->ulsch[i]->rnti,
		dB_fixed(eNB->pusch_vars[i]->ulsch_power[0]),
		dB_fixed(eNB->pusch_vars[i]->ulsch_power[1]),
		eNB->UE_stats[i].UL_rssi[0],
		eNB->UE_stats[i].UL_rssi[1],
		eNB->measurements->n0_power_dB[0],
		eNB->measurements->n0_power_dB[1],
		eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[0],
		eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[1],
		eNB->ret[i]);

          if (eNB->ulsch[i]->harq_processes[harq_pid]->round ==
              fp->maxHARQ_Msg3Tx) {
            LOG_D(PHY,"[eNB %d][RAPROC] maxHARQ_Msg3Tx reached, abandoning RA procedure for UE %d\n",
                  eNB->Mod_id, i);
            eNB->UE_stats[i].mode = PRACH;
			
	    if (eNB->mac_enabled==1) {
	      mac_xface->cancel_ra_proc(eNB->Mod_id,
					eNB->CC_id,
					frame,
					eNB->UE_stats[i].crnti);
	
					
	    }
	
            mac_phy_remove_ue(eNB->Mod_id,eNB->UE_stats[i].crnti);
			
            eNB->ulsch[(uint32_t)i]->Msg3_active = 0;
            //eNB->ulsch[i]->harq_processes[harq_pid]->phich_active = 0;

          } else {
            // activate retransmission for Msg3 (signalled to UE PHY by PHICH (not MAC/DCI)
            eNB->ulsch[(uint32_t)i]->Msg3_active = 1;
			
            get_Msg3_alloc_ret(fp,
                               subframe,
                               frame,
                               &eNB->ulsch[i]->Msg3_frame,
                               &eNB->ulsch[i]->Msg3_subframe);
          }
          LOG_D(PHY,"[eNB] Frame %d, Subframe %d: Msg3 in error, i = %d \n", frame,subframe,i);
        } // This is Msg3 error

        else { //normal ULSCH
          LOG_D(PHY,"[eNB %d][PUSCH %d] frame %d subframe %d UE %d Error receiving ULSCH, round %d/%d (ACK %d,%d)\n",
                eNB->Mod_id,harq_pid,
                frame,subframe, i,
                eNB->ulsch[i]->harq_processes[harq_pid]->round-1,
                eNB->ulsch[i]->Mlimit,
                eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[0],
                eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[1]);

#if defined(MESSAGE_CHART_GENERATOR_PHY)
          MSC_LOG_RX_DISCARDED_MESSAGE(
				       MSC_PHY_ENB,MSC_PHY_UE,
				       NULL,0,
				       "%05u:%02u ULSCH received rnti %x harq id %u round %d",
				       frame,subframe,
				       eNB->ulsch[i]->rnti,harq_pid,
				       eNB->ulsch[i]->harq_processes[harq_pid]->round-1
				       );
#endif
		 
          if (eNB->ulsch[i]->harq_processes[harq_pid]->round== eNB->ulsch[i]->Mlimit) {
            LOG_D(PHY,"[eNB %d][PUSCH %d] frame %d subframe %d UE %d ULSCH Mlimit %d reached\n",
                  eNB->Mod_id,harq_pid,
                  frame,subframe, i,
                  eNB->ulsch[i]->Mlimit);

            eNB->ulsch[i]->harq_processes[harq_pid]->round=0;
            eNB->ulsch[i]->harq_processes[harq_pid]->phich_active=0;
            eNB->UE_stats[i].ulsch_errors[harq_pid]++;
            eNB->UE_stats[i].ulsch_consecutive_errors++;
	
	    // indicate error to MAC
	    if (eNB->mac_enabled == 1){
	      mac_xface->rx_sdu(eNB->Mod_id,
				eNB->CC_id,
				frame,subframe,
				eNB->ulsch[i]->rnti,
				NULL,
				0,
				harq_pid,
				&eNB->ulsch[i]->Msg3_flag);
	
			}
          }
        }
      }  // ulsch in error
      else {



        T(T_ENB_PHY_ULSCH_UE_ACK, T_INT(eNB->Mod_id), T_INT(frame), T_INT(subframe), T_INT(i), T_INT(eNB->ulsch[i]->rnti),
          T_INT(harq_pid));

        if (eNB->ulsch[i]->Msg3_flag == 1) {
	  LOG_D(PHY,"[eNB %d][PUSCH %d] Frame %d subframe %d ULSCH received, setting round to 0, PHICH ACK\n",
		eNB->Mod_id,harq_pid,
		frame,subframe);
	  LOG_D(PHY,"[eNB %d][PUSCH %d] frame %d subframe %d RNTI %x RX power (%d,%d) RSSI (%d,%d) N0 (%d,%d) dB ACK (%d,%d), decoding iter %d\n",
		eNB->Mod_id,harq_pid,
		frame,subframe,
		eNB->ulsch[i]->rnti,
		dB_fixed(eNB->pusch_vars[i]->ulsch_power[0]),
		dB_fixed(eNB->pusch_vars[i]->ulsch_power[1]),
		eNB->UE_stats[i].UL_rssi[0],
		eNB->UE_stats[i].UL_rssi[1],
		eNB->measurements->n0_power_dB[0],
		eNB->measurements->n0_power_dB[1],
		eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[0],
		eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[1],
		eNB->ret[i]);
	}
#if defined(MESSAGE_CHART_GENERATOR_PHY)
        MSC_LOG_RX_MESSAGE(
			   MSC_PHY_ENB,MSC_PHY_UE,
			   NULL,0,
			   "%05u:%02u ULSCH received rnti %x harq id %u",
			   frame,subframe,
			   eNB->ulsch[i]->rnti,harq_pid
			   );
#endif
        for (j=0; j<fp->nb_antennas_rx; j++)
          //this is the RSSI per RB
          eNB->UE_stats[i].UL_rssi[j] =
	    
            dB_fixed(eNB->pusch_vars[i]->ulsch_power[j]*
                     (eNB->ulsch[i]->harq_processes[harq_pid]->nb_rb*12)/
                     fp->ofdm_symbol_size) -
            eNB->rx_total_gain_dB -
            hundred_times_log10_NPRB[eNB->ulsch[i]->harq_processes[harq_pid]->nb_rb-1]/100 -
            get_hundred_times_delta_IF_eNB(eNB,i,harq_pid, 0)/100;
	    
        eNB->ulsch[i]->harq_processes[harq_pid]->phich_active = 1;
        eNB->ulsch[i]->harq_processes[harq_pid]->phich_ACK = 1;
        eNB->ulsch[i]->harq_processes[harq_pid]->round = 0;
        eNB->UE_stats[i].ulsch_consecutive_errors = 0;

        if (eNB->ulsch[i]->Msg3_flag == 1) {
	  if (eNB->mac_enabled==1) {

	    LOG_I(PHY,"[eNB %d][RAPROC] Frame %d Terminating ra_proc for harq %d, UE %d\n",
		  eNB->Mod_id,
		  frame,harq_pid,i);
	    if (eNB->mac_enabled){
	      mac_xface->rx_sdu(eNB->Mod_id,
				eNB->CC_id,
				frame,subframe,
				eNB->ulsch[i]->rnti,
				eNB->ulsch[i]->harq_processes[harq_pid]->b,
				eNB->ulsch[i]->harq_processes[harq_pid]->TBS>>3,
				harq_pid,
				&eNB->ulsch[i]->Msg3_flag);
		
	    }
	    // one-shot msg3 detection by MAC: empty PDU (e.g. CRNTI)
	    if (eNB->ulsch[i]->Msg3_flag == 0 ) {
	      eNB->UE_stats[i].mode = PRACH;
	      mac_xface->cancel_ra_proc(eNB->Mod_id,
					eNB->CC_id,
					frame,
					eNB->UE_stats[i].crnti);
		
	      mac_phy_remove_ue(eNB->Mod_id,eNB->UE_stats[i].crnti);
	      eNB->ulsch[(uint32_t)i]->Msg3_active = 0;
	    } // Msg3_flag == 0
	    
	  } // mac_enabled==1

          eNB->UE_stats[i].mode = PUSCH;
          eNB->ulsch[i]->Msg3_flag = 0;
			 
	  LOG_D(PHY,"[eNB %d][RAPROC] Frame %d : RX Subframe %d Setting UE %d mode to PUSCH\n",eNB->Mod_id,frame,subframe,i);

          for (k=0; k<8; k++) { //harq_processes
            for (j=0; j<eNB->dlsch[i][0]->Mlimit; j++) {
              eNB->UE_stats[i].dlsch_NAK[k][j]=0;
              eNB->UE_stats[i].dlsch_ACK[k][j]=0;
              eNB->UE_stats[i].dlsch_trials[k][j]=0;
            }

            eNB->UE_stats[i].dlsch_l2_errors[k]=0;
            eNB->UE_stats[i].ulsch_errors[k]=0;
            eNB->UE_stats[i].ulsch_consecutive_errors=0;

            for (j=0; j<eNB->ulsch[i]->Mlimit; j++) {
              eNB->UE_stats[i].ulsch_decoding_attempts[k][j]=0;
              eNB->UE_stats[i].ulsch_decoding_attempts_last[k][j]=0;
              eNB->UE_stats[i].ulsch_round_errors[k][j]=0;
              eNB->UE_stats[i].ulsch_round_fer[k][j]=0;
            }
          }

          eNB->UE_stats[i].dlsch_sliding_cnt=0;
          eNB->UE_stats[i].dlsch_NAK_round0=0;
          eNB->UE_stats[i].dlsch_mcs_offset=0;
        } // Msg3_flag==1
	else {  // Msg3_flag == 0

#ifdef DEBUG_PHY_PROC
#ifdef DEBUG_ULSCH
          LOG_D(PHY,"[eNB] Frame %d, Subframe %d : ULSCH SDU (RX harq_pid %d) %d bytes:",frame,subframe,
                harq_pid,eNB->ulsch[i]->harq_processes[harq_pid]->TBS>>3);

          for (j=0; j<eNB->ulsch[i]->harq_processes[harq_pid]->TBS>>3; j++)
            LOG_T(PHY,"%x.",eNB->ulsch[i]->harq_processes[harq_pid]->b[j]);

          LOG_T(PHY,"\n");
#endif
#endif

	  if (eNB->mac_enabled==1) {

	    mac_xface->rx_sdu(eNB->Mod_id,
			      eNB->CC_id,
			      frame,subframe,
			      eNB->ulsch[i]->rnti,
			      eNB->ulsch[i]->harq_processes[harq_pid]->b,
			      eNB->ulsch[i]->harq_processes[harq_pid]->TBS>>3,
			      harq_pid,
			      NULL);

#ifdef LOCALIZATION
	    start_meas(&eNB->localization_stats);
	    aggregate_eNB_UE_localization_stats(eNB,
						i,
						frame,
						subframe,
						get_hundred_times_delta_IF_eNB(eNB,i,harq_pid, 1)/100);
	    stop_meas(&eNB->localization_stats);
#endif
	    
	  } // mac_enabled==1
        } // Msg3_flag == 0

        // estimate timing advance for MAC
        if (eNB->abstraction_flag == 0) {
          sync_pos = lte_est_timing_advance_pusch(eNB,i);
		  //printf("lte_est_timing_advance_pusch %d \n",sync_pos);
          eNB->UE_stats[i].timing_advance_update = sync_pos - fp->nb_prefix_samples/4; //to check
        }

#ifdef DEBUG_PHY_PROC
        LOG_D(PHY,"[eNB %d] frame %d, subframe %d: user %d: timing advance = %d\n",
              eNB->Mod_id,
              frame, subframe,
              i,
              eNB->UE_stats[i].timing_advance_update);
#endif


      }  // ulsch not in error

      // process HARQ feedback
#ifdef DEBUG_PHY_PROC
      LOG_D(PHY,"[eNB %d][PDSCH %x] Frame %d subframe %d, Processing HARQ feedback for UE %d (after PUSCH)\n",eNB->Mod_id,
            eNB->dlsch[i][0]->rnti,
            frame,subframe,
            i);
#endif
      process_HARQ_feedback(i,
                            eNB,proc,
                            1, // pusch_flag
                            0,
                            0,
                            0);

#ifdef DEBUG_PHY_PROC
      LOG_D(PHY,"[eNB %d] Frame %d subframe %d, sect %d: received ULSCH harq_pid %d for UE %d, ret = %d, CQI CRC Status %d, ACK %d,%d, ulsch_errors %d/%d\n",
            eNB->Mod_id,frame,subframe,
            eNB->UE_stats[i].sector,
            harq_pid,
            i,
            eNB->ret[i],
            eNB->ulsch[i]->harq_processes[harq_pid]->cqi_crc_status,
            eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[0],
            eNB->ulsch[i]->harq_processes[harq_pid]->o_ACK[1],
            eNB->UE_stats[i].ulsch_errors[harq_pid],
            eNB->UE_stats[i].ulsch_decoding_attempts[harq_pid][0]);
#endif
      
      // dump stats to VCD
      if (i==0) {
	VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_UE0_MCS0+harq_pid,eNB->pusch_stats_mcs[0][(frame*10)+subframe]);
	VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_UE0_RB0+harq_pid,eNB->pusch_stats_rb[0][(frame*10)+subframe]);
	VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_UE0_ROUND0+harq_pid,eNB->pusch_stats_round[0][(frame*10)+subframe]);
	VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_UE0_RSSI0+harq_pid,dB_fixed(eNB->pusch_vars[0]->ulsch_power[0]));
	VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_UE0_RES0+harq_pid,eNB->ret[i]);
	VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_UE0_SFN0+harq_pid,(frame*10)+subframe);
      }
    } // ulsch[0] && ulsch[0]->rnti>0 && ulsch[0]->subframe_scheduling_flag == 1


    // update ULSCH statistics for tracing
    if ((frame % 100 == 0) && (subframe == 4)) {
      for (harq_idx=0; harq_idx<8; harq_idx++) {
        for (round=0; round<eNB->ulsch[i]->Mlimit; round++) {
          if ((eNB->UE_stats[i].ulsch_decoding_attempts[harq_idx][round] -
               eNB->UE_stats[i].ulsch_decoding_attempts_last[harq_idx][round]) != 0) {
            eNB->UE_stats[i].ulsch_round_fer[harq_idx][round] =
              (100*(eNB->UE_stats[i].ulsch_round_errors[harq_idx][round] -
                    eNB->UE_stats[i].ulsch_round_errors_last[harq_idx][round]))/
              (eNB->UE_stats[i].ulsch_decoding_attempts[harq_idx][round] -
               eNB->UE_stats[i].ulsch_decoding_attempts_last[harq_idx][round]);
          } else {
            eNB->UE_stats[i].ulsch_round_fer[harq_idx][round] = 0;
          }

          eNB->UE_stats[i].ulsch_decoding_attempts_last[harq_idx][round] =
            eNB->UE_stats[i].ulsch_decoding_attempts[harq_idx][round];
          eNB->UE_stats[i].ulsch_round_errors_last[harq_idx][round] =
            eNB->UE_stats[i].ulsch_round_errors[harq_idx][round];
        }
      }
    }

    if ((frame % 100 == 0) && (subframe==4)) {
      eNB->UE_stats[i].dlsch_bitrate = (eNB->UE_stats[i].total_TBS -
					eNB->UE_stats[i].total_TBS_last);

      eNB->UE_stats[i].total_TBS_last = eNB->UE_stats[i].total_TBS;
    }

    // CBA (non-LTE)
    cba_procedures(eNB,proc,i,harq_pid);
	}//complete_viterbi[i]&&complete_turbo[i]?
  } // loop i=0 ... NUMBER_OF_UE_MAX-1
}
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_p2m,0);
  //Last,wait for finishing pucch
  while(eNB->complete_pucch!=1);
  
  if (eNB->abstraction_flag == 0) {
    lte_eNB_I0_measurements(eNB,
			    subframe,
			    0,
			    eNB->first_run_I0_measurements);
    eNB->first_run_I0_measurements = 0;
  }

#ifdef PHY_ABSTRACTION
  else {
    lte_eNB_I0_measurements_emul(eNB,
				 0);
  }

#endif

#ifdef EMOS
  phy_procedures_emos_eNB_RX(subframe,eNB);
#endif
#ifdef PRESSURETEST_RX
   if ((eNB->ulsch[0]) &&
       (eNB->ulsch[0]->rnti>0) &&
       (eNB->ulsch[0]->harq_processes[harq_pid]->subframe_scheduling_flag==1)){
	for(i=1;i<NUMBER_OF_UE_MAX;++i) free(eNB->ulsch_t[i]);//free fake decode
	}
#endif  
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_PROCEDURES_ENB_RX_UESPEC+offset, 0 );

  stop_meas(&eNB->phy_proc_rx);

}

#undef DEBUG_PHY_PROC

#ifdef Rel10
int phy_procedures_RN_eNB_TX(unsigned char last_slot, unsigned char next_slot, relaying_type_t r_type)
{

  int do_proc=0;// do nothing

  switch(r_type) {
  case no_relay:
    do_proc= no_relay; // perform the normal eNB operation
    break;

  case multicast_relay:
    if (((next_slot >>1) < 6) || ((next_slot >>1) > 8))
      do_proc = 0; // do nothing
    else // SF#6, SF#7 and SF#8
      do_proc = multicast_relay; // do PHY procedures eNB TX

    break;

  default: // should'not be here
    LOG_W(PHY,"Not supported relay type %d, do nothing\n", r_type);
    do_proc=0;
    break;
  }

  return do_proc;
}
#endif
