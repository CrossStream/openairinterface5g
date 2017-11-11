





#ifndef __IF_MODULE_L1_PRIMITIVES_NB_IOT_H__
#define __IF_MODULE_L1_PRIMITIVES_NB_IOT_H__

//#include "openair2/PHY_INTERFACE/IF_Module_nb_iot.h"
#include "openair1/PHY/defs_NB_IoT.h"
//#include "LAYER2/MAC/extern.h"
//#include "LAYER2/MAC/proto_NB_IoT.h"



/*Interface for Downlink, transmitting the DLSCH SDU, DCI SDU*/
void schedule_response_NB_IoT(Sched_Rsp_NB_IoT_t *Sched_INFO);

/*Interface for PHY Configuration
 * Trigger the phy_config_xxx functions using parameters from the shared PHY_Config structure
 * */
void PHY_config_req_NB_IoT(PHY_Config_NB_IoT_t* config_INFO);

void handle_nfapi_dlsch_pdu_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,
				   eNB_rxtx_proc_NB_IoT_t *proc,
				   nfapi_dl_config_request_pdu_t *dl_config_pdu,
				   uint8_t *sdu);

#endif
