
#ifndef HMG_TRAFFIC_H
#define HMG_TRAFFIC_H

#include "wifi_engine_internal.h"

void HMG_init_traffic(void);
void HMG_startUp_traffic(void);
void HMG_resume_traffic(void);
void HMG_Unplug_traffic(void);

int  HMG_GetState_traffic(void);
int  HMG_GetSubState_traffic(void);


#endif /* HMG_TRAFFIC_H */
