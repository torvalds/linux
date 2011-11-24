//------------------------------------------------------------------------------
// <copyright file="abtfilt_int.h" company="Atheros">
//    Copyright (c) 2008 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

/*
 * Bluetooth filter internal definitions
 * 
 */
 

#ifndef ABTFILT_INT_H_
#define ABTFILT_INT_H_

#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#include <linux/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include "a_config.h"
#include "a_osapi.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_debug.h"
#include "dl_list.h"
#include "athbtfilter.h"
#include "btfilter_core.h"
#include "athdrv_linux.h"
#include "wmi.h"

#ifndef IW_EV_LCP_PK_LEN
#define IW_EV_LCP_PK_LEN (4)
#define IW_EV_POINT_PK_LEN (IW_EV_LCP_PK_LEN + 4)
#define IW_EV_POINT_OFF (((char *) &(((struct iw_point *) NULL)->length)) - \
                          (char *) NULL)
#define IW_EV_POINT_LEN (IW_EV_LCP_LEN + sizeof(struct iw_point) - \
                         IW_EV_POINT_OFF)
#endif

/* Forward Declarations */
struct _ABF_BT_INFO;
struct _ABF_WLAN_INFO;

/*-----------------------------------------------------------------------*/
/* Utils Section */

/* Task Management operations and definitions */
#define WAITFOREVER             -1
#define A_TASK_HANDLE           pthread_t
#define A_COND_OBJECT           pthread_cond_t
#define A_MUTEX_OBJECT          pthread_mutex_t
#define A_FILE_HANDLE           FILE *

/* Function Prototypes */
INLINE A_STATUS A_TASK_CREATE(A_TASK_HANDLE *handle, 
                              void *(*func)(void *), void *arg);
INLINE A_STATUS A_TASK_JOIN(A_TASK_HANDLE *handle);
INLINE void A_TASK_CLEANUP(void);

INLINE A_STATUS A_MUTEX_INIT(A_MUTEX_OBJECT *mutex);
INLINE void A_MUTEX_LOCK(A_MUTEX_OBJECT *mutex);
INLINE void A_MUTEX_UNLOCK(A_MUTEX_OBJECT *mutex);
INLINE void A_MUTEX_DEINIT(A_MUTEX_OBJECT *mutex);

INLINE A_STATUS A_COND_INIT(A_COND_OBJECT *cond);
INLINE A_STATUS A_COND_WAIT(A_COND_OBJECT *cond, 
                            A_MUTEX_OBJECT *mutex, int timeout);
INLINE void A_COND_SIGNAL(A_COND_OBJECT *cond);
INLINE void A_COND_DEINIT(A_COND_OBJECT *cond);
INLINE A_STATUS A_COND_RESET(A_COND_OBJECT *cond);

INLINE void A_STR2ADDR(const char *str, A_UINT8 *addr);

#ifdef ABF_DEBUG
INLINE void A_DBG_INIT(const char *ident, const char *message, ...);
INLINE void A_DEBUG(const char *format, ...);
INLINE void A_INFO(const char *format, ...);
INLINE void A_ERR(const char *format, ...);
INLINE void A_SET_DEBUG(int enable);
INLINE void A_DBG_DEINIT(void);
void A_DUMP_BUFFER(A_UCHAR *buffer, int length, char *pDescription);
void A_DBG_SET_OUTPUT_TO_CONSOLE(void);
#else
#define A_DBG_INIT(args...) 
#define A_DEBUG(args...)
#define A_INFO(args...)
#define A_ERR(args...)
#define A_SET_DEBUG(arg)
#define A_DBG_DEINIT()
#define A_DUMP_BUFFER(buffer, length, pDescription)
#define A_DBG_SET_OUTPUT_TO_CONSOLE() 

#endif /* ABF_DEBUG */
/*-----------------------------------------------------------------------*/

/*-----------------------------------------------------------------------*/
/* Filter Section */
#define MAX_BT_ACTION_MESSAGES      16
#define ACTION_WAIT_TIMEOUT         100

#define QUEUE_BT_ACTION_MSG(p,a) \
    DL_ListInsertTail(&(p)->BTActionMsgList,&(a)->ListEntry)
#define FREE_BT_ACTION_MSG(p,a) \
    DL_ListInsertTail(&(p)->FreeActionMsgList,&(a)->ListEntry)

#define A_LOCK_FILTER(p) \
    A_MUTEX_LOCK(&((p)->CritSection))

#define A_UNLOCK_FILTER(p) \
    A_MUTEX_UNLOCK(&((p)->CritSection))

typedef enum _BTACTION_QUEUE_PROC {
    BTACTION_QUEUE_NORMAL      = 0, /* normal processing of the action queue */
    BTACTION_QUEUE_FLUSH_ALL   = 1, /* flush all actions for shutdown */ 
    BTACTION_QUEUE_FLUSH_STATE,     /* flush all actions associated with a 
                                       specific state */     
    BTACTION_QUEUE_SYNC_STATE       /* issue queued actions when we are 
                                       syncing radio state on adapter 
                                       available */         
} BTACTION_QUEUE_PROC;

typedef enum _ATH_ADAPTER_EVENT {
   ATH_ADAPTER_ARRIVED = 0,
   ATH_ADAPTER_REMOVED = 1,
} ATH_ADAPTER_EVENT;
    
typedef struct _BT_ACTION_MSG {
    DL_LIST                         ListEntry;
    A_COND_OBJECT                   hWaitEvent;      /* wait object for 
                                                        blocking requests */
    A_MUTEX_OBJECT                  hWaitEventLock;
    A_BOOL                          Blocking;        /* this action requires 
                                                        the calling thread to 
                                                        block until the 
                                                        dispatcher submits 
                                                        the command */
    ATHBT_STATE_INDICATION          IndicationForControlAction; /* indication
                                                                   associated 
                                                                   with the 
                                                                   control 
                                                                   action */
    ATHBT_STATE                     StateForControlAction;
    BT_CONTROL_ACTION               ControlAction;
} BT_ACTION_MSG; 

typedef struct _ATHBT_SCO_CONNECTION_INFO {
    A_BOOL                          Valid;
    A_UCHAR                         LinkType;
    A_UCHAR                         TransmissionInterval;
    A_UCHAR                         RetransmissionInterval;
    A_UINT16                        RxPacketLength;
    A_UINT16                        TxPacketLength;
} ATHBT_SCO_CONNECTION_INFO;

typedef struct _ATHBT_FILTER_INFO {
    ATH_BT_FILTER_INSTANCE         *pInstance;
    A_UINT32                        MaxBtActionMessages;
    A_MUTEX_OBJECT                  CritSection;
    DL_LIST                         BTActionMsgList;
    DL_LIST                         FreeActionMsgList;
    BT_FILTER_CORE_INFO             FilterCore;
    A_BOOL                          Shutdown;
    A_COND_OBJECT                   hWakeEvent;
    A_MUTEX_OBJECT                  hWakeEventLock;
    A_TASK_HANDLE                   hFilterThread;
    A_BOOL                          FilterThreadValid;
    ATHBT_SCO_CONNECTION_INFO       SCOConnectInfo;
    A_UCHAR                         LMPVersion;
    A_BOOL                          AdapterAvailable;
    A_TASK_HANDLE                   hBtThread;
    A_TASK_HANDLE                   hWlanThread;
    struct _ABF_WLAN_INFO          *pWlanInfo;
    struct _ABF_BT_INFO            *pBtInfo;
    A_UCHAR                        SCOConnection_LMPVersion;   /* lmp version of remote SCO device */
    A_UCHAR                        A2DPConnection_LMPVersion;  /* lmp version of remote A2DP device */ 
    A_UCHAR                        SCOConnection_Role;         /* role of remote SCO device */
    A_UCHAR                        A2DPConnection_Role;        /* role of remote A2DP device */
    A_UINT32                       Flags;
} ATHBT_FILTER_INFO;

/* Function Prototypes */
void HandleAdapterEvent(ATHBT_FILTER_INFO *pInfo, ATH_ADAPTER_EVENT Event);
/*-----------------------------------------------------------------------*/

/*-----------------------------------------------------------------------*/
/* WLAN Section */
#define WLAN_ADAPTER_NAME_SIZE_MAX  31

#define WLAN_GET_HOME_CHANNEL(pInfo) \
    (pInfo)->pAbfWlanInfo->Channel

typedef struct _ABF_WLAN_INFO {
    ATHBT_FILTER_INFO              *pInfo;
    A_INT32                         Handle;
    A_UINT8                         PhyCapability;
    A_UCHAR                         AdapterName[WLAN_ADAPTER_NAME_SIZE_MAX+1];
    A_UINT32                        HostVersion;
    A_UINT32                        TargetVersion;
    A_CHAR                          IfName[IFNAMSIZ];
    A_INT32                         IfIndex;
    A_BOOL                          Loop;
    A_COND_OBJECT                   hWaitEvent;
    A_MUTEX_OBJECT                  hWaitEventLock;
    A_UINT16                        Channel;
} ABF_WLAN_INFO;

/* Function Prototypes */
void Abf_WlanCheckSettings(A_CHAR *wifname, A_UINT32 *btfiltFlags);
A_STATUS Abf_WlanStackNotificationInit(ATH_BT_FILTER_INSTANCE *pInstance, A_UINT32 flags);
void Abf_WlanStackNotificationDeInit(ATH_BT_FILTER_INSTANCE *pInstance);
A_STATUS Abf_WlanDispatchIO(ATHBT_FILTER_INFO *pInfo, unsigned long int req,
                            void *data, int size);
/*-----------------------------------------------------------------------*/

/* Function Prototypes */

#define ABF_ENABLE_AFH_CHANNEL_CLASSIFICATION   (1 << 0)
#define ABF_USE_HCI_FILTER_FOR_HEADSET_PROFILE  (1 << 1)
#define ABF_WIFI_CHIP_IS_VENUS                  (1 << 2)
#define ABF_BT_CHIP_IS_ATHEROS                  (1 << 3)
#define ABF_USE_ONLY_DBUS_FILTERING             (1 << 4)
#define ABF_FE_ANT_IS_SA                        (1 << 5)

A_STATUS Abf_BtStackNotificationInit(ATH_BT_FILTER_INSTANCE *pInstance, A_UINT32 Flags);
void Abf_BtStackNotificationDeInit(ATH_BT_FILTER_INSTANCE *pInstance);

#ifdef CONFIG_NO_HCILIBS
#define Abf_HciLibInit(_flags) (A_ERROR)
#define Abf_HciLibDeInit()
#define Abf_RegisterToHciLib(_pInfo) 
#define Abf_UnRegisterToHciLib(_pInfo)
#define Abf_IssueAFHViaHciLib(_pInfo, _ch) do { } while (0)
#else
A_STATUS Abf_HciLibInit(A_UINT32 *flags);
void Abf_HciLibDeInit(void);
void Abf_RegisterToHciLib(struct _ABF_BT_INFO * pAbfBtInfo);
void Abf_UnRegisterToHciLib(struct _ABF_BT_INFO * pAbfBtInfo);
A_STATUS  Abf_IssueAFHViaHciLib (struct _ABF_BT_INFO  * pAbfBtInfo, int CurrentWLANChannel);
#endif

/* WLAN channel number can be expressed as either 1-14 or expressed in Mhz (i.e. 2412) */ 
void IndicateCurrentWLANOperatingChannel(ATHBT_FILTER_INFO *pFilterInfo, int CurrentWLANChannel);

/*-----------------------------------------------------------------------*/

A_STATUS Abf_WlanGetSleepState(ATHBT_FILTER_INFO * pInfo);
A_STATUS Abf_WlanGetCurrentWlanOperatingFreq(ATHBT_FILTER_INFO * pInfo);
A_STATUS Abf_WlanIssueFrontEndConfig(ATHBT_FILTER_INFO * pInfo);
#endif /* ABTFILT_INT_H_ */
