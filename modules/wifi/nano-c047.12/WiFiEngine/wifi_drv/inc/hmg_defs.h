
#ifndef HMG_DEFS_H
#define HMG_DEFS_H

#include "ucos.h"
#include "wifi_engine_internal.h"
#define INT_MESSAGE_TYPE 0x40
#define MGMT_MESSAGE_RX  0x80
#define MGMT_MESSAGE_TX  0x00

typedef enum
{
 /* If you update this, please also update HMG_Signal_toToName() */

   //INTSIG_INIT =   0x40000000,
   INTSIG_INIT = (INT_MESSAGE_TYPE << 24),
   INTSIG_EXIT,
   INTSIG_EXECUTE,
   INTSIG_NET_IDLE,
   INTSIG_NET_LOST,
   INTSIG_NET_LIST,
   INTSIG_NET_RESELECT,
   INTSIG_NET_CONNECT,
   INTSIG_NET_JOIN,
   INTSIG_NET_AUTHENTICATE,
   INTSIG_NET_ASSOCIATE,
   INTSIG_NET_START,
   INTSIG_NET_DEAUTHENTICATE,
   INTSIG_NET_LEAVE_IBSS, 
   INTSIG_NET_LEAVE_BSS,
   INTSIG_NET_REASSOCIATE,
   INTSIG_NET_DISASSOCIATE, 
   INTSIG_POWER_UP,
   INTSIG_UNPLUG_TRAFFIC,
   INTSIG_UNPLUG_PS,
   INTSIG_DEVICE_POWER_SLEEP,   
   INTSIG_ENABLE_WMM_PS,
   INTSIG_DISABLE_WMM_PS, 
   INTSIG_LEGACY_PS_CONFIGURATION_CHANGED,
   INTSIG_DISABLE_PS,
   INTSIG_ENABLE_PS,
   INTSIG_DEVICE_CONNECTED,
   INTSIG_DEVICE_DISCONNECTED, 
   INTSIG_PS_SLEEP_FOREVER,
   INTSIG_EVAL_HCI
   
} HMG_Signals_e;

// _type, _id and direction are all uint8_t
#define wei_message2signal(_id,_type, _direction) (((uint32_t)_direction << 24) | (_type << 8) | (_id))

#define RX_TYPE_ID(_type,_id) ((MGMT_MESSAGE_RX << 24)) | (_type << 8) | (_id)) 
#define TX_TYPE_ID(_type,_id) ((MGMT_MESSAGE_TX << 24)) | (_type << 8) | (_id)) 

//#define INTSIG_CASE(_id) ((INT_MESSAGE_TYPE << 24) | (_id))
#define INTSIG_CASE(_id) ((_id))
#define CTRL_RX_CASE(_id) (((uint32_t)MGMT_MESSAGE_RX << 24) | (HIC_MESSAGE_TYPE_CTRL << 8) | (_id))
#define MGMT_RX_CASE(_id) (((uint32_t)MGMT_MESSAGE_RX << 24) | (HIC_MESSAGE_TYPE_MGMT << 8) | (_id))
#define MGMT_TX_CASE(_id) (((uint32_t)MGMT_MESSAGE_TX << 24) | (HIC_MESSAGE_TYPE_MGMT << 8) | (_id))

/* State function definitions. */
typedef void*     (*StateFn_ref)(ucos_msg_id_t msg, wei_sm_queue_param_s *param);
typedef StateFn_ref (*StateFn_t)(ucos_msg_id_t msg, wei_sm_queue_param_s *param);

#define DECLARE_STATE_FUNCTION(StateFunctionName) \
   static StateFn_ref StateFunctionName(ucos_msg_id_t msg, wei_sm_queue_param_s *param)

#define DEFINE_STATE_FUNCTION(StateFunctionName) \
   StateFn_ref StateFunctionName(ucos_msg_id_t msg, wei_sm_queue_param_s *param)

#define SET_NEW_SUB_STATE(StateFunctionName) stateVars.newSubState_fn = StateFunctionName
#define SET_NEW_STATE(StateFunctionName)     stateVars.newState_fn = StateFunctionName

/* Definition of structure containing all state variables,  that are unique for each instance.
 */
typedef struct
{
   StateFn_t currentState_fn;    /* Current state     */
   StateFn_t newState_fn;        /* New state         */
   StateFn_t subState_fn;        /* Current sub state */
   StateFn_t newSubState_fn;     /* New sub state     */
} StateVariables_t;

#endif /* HMG_DEFS_H */

