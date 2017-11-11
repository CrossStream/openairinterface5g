#include "openair2/PHY_INTERFACE/IF_Module_NB_IoT.h"
//#include "LAYER2/MAC/extern.h"



#ifndef __IF_MODULE_L2_PRIMITIVES_NB_IOT_H__
#define __IF_MODULE_L2_PRIMITIVES_NB_IOT_H__

/*Interface for uplink, transmitting the Preamble(list), ULSCH SDU, NAK, Tick (trigger scheduler)
*/
void UL_indication_NB_IoT(UL_IND_NB_IoT_t *UL_INFO);

#endif
