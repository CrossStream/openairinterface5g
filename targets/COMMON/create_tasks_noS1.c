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
/* temporary file for noS1 support, while moving from compile to config option */
#if defined(ENABLE_ITTI)
# include "intertask_interface.h"
# include "create_tasks.h"
# include "common/utils/LOG/log.h"

# ifdef OPENAIR2
#     include "udp_eNB_task.h"
#   if ENABLE_RAL
#     include "lteRALue.h"
#     include "lteRALenb.h"
#   endif
#   include "RRC/LTE/rrc_defs.h"
# endif
# include "enb_app.h"
#include "s1ap_eNB.h"


s1ap_eNB_config_t s1ap_config;
int noS1_create_s1u_tunnel( const instance_t instanceP,
                            const gtpv1u_enb_create_tunnel_req_t *create_tunnel_req,
                            gtpv1u_enb_create_tunnel_resp_t *create_tunnel_resp_pP) {
  LOG_D(ENB_APP, "noS1 exec should not go there...\n");
  /*
    on a pas l'adresse IP du UE, mais on l'aura dans le premier paquet sortant
    Sinon, il faut la passer dans le create tunnel
  */
  return 0;

}
int gtpv1u_create_s1u_tunnel( const instance_t instanceP,
                              const gtpv1u_enb_create_tunnel_req_t *create_tunnel_req,
                              gtpv1u_enb_create_tunnel_resp_t *create_tunnel_resp_pP) {
  LOG_D(ENB_APP, "noS1 exec should not go there...\n");
  /*
    on a pas l'adresse IP du UE, mais on l'aura dans le premier paquet sortant
    Sinon, il faut la passer dans le create tunnel
  */
  return 0;

}
int noS1_update_s1u_tunnel(     const instance_t                              instanceP,
    const gtpv1u_enb_create_tunnel_req_t * const  create_tunnel_req_pP,
    const rnti_t                                  prior_rnti) {
  LOG_D(ENB_APP, "noS1 exec should not go there...\n");
  /*
    on a pas l'adresse IP du UE, mais on l'aura dans le premier paquet sortant
    Sinon, il faut la passer dans le create tunnel
  */
  return 0;

}
int gtpv1u_update_s1u_tunnel(     const instance_t                              instanceP,
    const gtpv1u_enb_create_tunnel_req_t * const  create_tunnel_req_pP,
    const rnti_t                                  prior_rnti) {
  LOG_D(ENB_APP, "noS1 exec should not go there...\n");
  /*
    on a pas l'adresse IP du UE, mais on l'aura dans le premier paquet sortant
    Sinon, il faut la passer dans le create tunnel
  */
  return 0;

}
uint32_t s1ap_generate_eNB_id(void)
{
  LOG_D(ENB_APP, "noS1 exec should not go there...\n");
  return 0;
}

int decode_attach_request(attach_request_msg *attach_request, uint8_t *buffer, uint32_t len) {
  LOG_D(ENB_APP, "noS1 exec should not go there...\n");
  return 0;
}
int decode_identity_response(identity_response_msg  *attach_request, uint8_t *buffer, uint32_t len) {
  LOG_D(ENB_APP, "noS1 exec should not go there...\n");
  return 0;
}
int rrc_eNB_process_GTPV1U_CREATE_TUNNEL_RESP(const protocol_ctxt_t* const ctxt_pP, const gtpv1u_enb_create_tunnel_resp_t * const create_tunnel_resp_pP) {
  LOG_D(ENB_APP, "noS1 exec should not go there...\n");
  return 0;
}
int create_tasks(uint32_t enb_nb, int32_t noS1)
{ 

  LOG_D(ENB_APP, "%s(enb_nb (built-in noS1):%d\n", __FUNCTION__, enb_nb);
  itti_wait_ready(1);
  if (itti_create_task (TASK_L2L1, l2l1_task, NULL) < 0) {
    LOG_E(PDCP, "Create task for L2L1 failed\n");
    return -1;
  }

  if (enb_nb > 0) {
    if (itti_create_task (TASK_ENB_APP, eNB_app_task, NULL) < 0) {
      LOG_E(ENB_APP, "Create task for eNB APP failed\n");
      return -1;
    }



    LOG_I(RRC,"Creating RRC eNB Task\n");

    if (itti_create_task (TASK_RRC_ENB, rrc_enb_task, NULL) < 0) {
  	LOG_E(RRC, "Create task for RRC eNB failed\n");
  	return -1;
    }
  }
  itti_wait_ready(0);

  return 0;
}
#endif
