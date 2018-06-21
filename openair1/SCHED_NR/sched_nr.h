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

/*
  \author Guy De Souza
  \company EURECOM
  \email desouza@eurecom.fr
*/

#ifndef __openair_SCHED_NR_DEFS_H__
#define __openair_SCHED_NR_DEFS_H__

#include "PHY/defs_gNB.h"
#include "PHY_INTERFACE/phy_interface.h"
#include "SCHED/sched_eNB.h"

lte_subframe_t nr_subframe_select (nfapi_config_request_t *cfg, unsigned char subframe);
void nr_set_ssb_first_subcarrier(nfapi_config_request_t *cfg, NR_DL_FRAME_PARMS *fp);
void phy_procedures_gNB_TX(PHY_VARS_gNB *gNB, gNB_rxtx_proc_t *proc, int do_meas);
void nr_init_feptx_thread(RU_t *ru,pthread_attr_t *attr_feptx);
void nr_feptx_ofdm(RU_t *ru);
void nr_feptx_ofdm_2thread(RU_t *ru);
void nr_feptx0(RU_t *ru,int slot);

#endif