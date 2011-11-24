//------------------------------------------------------------------------------
// <copyright file="btfilter_core.h" company="Atheros">
//    Copyright (c) 2007 Atheros Corporation.  All rights reserved.
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
// Internal APIs and definitions for Bluetooth filter core
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef BTFILTER_INTERNAL_H_
#define BTFILTER_INTERNAL_H_

#include "btdefs.h"
#include "athdefs.h"
#include "dl_list.h"
#include "wmi.h"

typedef enum _BT_CONTROL_ACTION_TYPE {
    BT_CONTROL_ACTION_STATUS = 0,
    BT_CONTROL_ACTION_PARAMS = 1
} BT_CONTROL_ACTION_TYPE;

typedef union _BT_CONTROL_ACTION_BUFFER {
    WMI_SET_BT_STATUS_CMD AsStatusCmd;              /* parameters for a status command */
    WMI_SET_BT_PARAMS_CMD AsParamCmd;               /* parameters for a set params command */
    WMI_SET_BTCOEX_SCO_CONFIG_CMD scoConfigCmd;
    WMI_SET_BTCOEX_A2DP_CONFIG_CMD a2dpConfigCmd;
} BT_CONTROL_ACTION_BUFFER;

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define A_ROUND_UP_PWR2(x, align)    (((int) (x) + ((align)-1)) & ~((align)-1))

#define MAX_BT_CONTROL_ACTION_DATA_SIZE A_ROUND_UP_PWR2(sizeof(BT_CONTROL_ACTION_BUFFER),4) /* nicely aligned */

    /* core state flags and variables */
typedef struct _BT_FILTER_CORE_STATE {
    A_UINT16    SCO_ConnectionHandle;
    A_UINT16    ACL_ConnectionHandle;
    A_UINT16    eSCO_ConnectionHandle;
    A_UINT32    StateBitMap;
    A_UINT8     AVDTP_state;
    A_UINT8     RFCOMM_state;
    A_UINT16    AVDTP_SOURCE_CID;
    A_UINT16    AVDTP_DESTINATION_CID;
    A_UINT16    RFCOMM_SOURCE_CID;
    A_UINT16    RFCOMM_DESTINATION_CID;
    A_UINT32    btFilterFlags;
} BT_FILTER_CORE_STATE;

#define BT_ACTION_STRING_MAX_LENGTH 64  /* maximum action string length */
#define BT_ACTION_MAX_ARGS          18  /* maximum number of numberic arguments */

#define BT_CA_DESC_FLAGS_ALLOCATED (1 << 0)

    /* a control action descriptor */
typedef struct _BT_CONTROL_ACTION_DESC {
    A_CHAR                            *pActionString; /* action string (described below) */
    A_UINT32                           Flags;         /* flags for this entry */
    struct _BT_CONTROL_ACTION_DESC    *pNext;         /* link to next action */
} BT_CONTROL_ACTION_DESC;

    /* control action descriptor containing actions for each state */
typedef struct _BT_CONTROL_ACTION_DESC_STATE {
    BT_CONTROL_ACTION_DESC Action[STATE_MAX];
} BT_CONTROL_ACTION_DESC_STATE;


/* ACTION strings:
 *    The BT control actions are described in string format.  The string is parsed for parameters
 *    (based on tags and order) and the parameter list passed to a function that assembles the WMI
 *    commands (status or params) to issue.
 *
 *    The string format is selected to follow the command line ordering from the WMI config application.
 *
 *
 * BT STATUS:
 *      "-s <streamtype> <status>"
 *
 * BT PARAM SCO:
 *      "-pSCO <noSCOPkts> <pspollTimeout> <stompbt> <scoOptFlags> <dutyCycleMin> <dutyCycleMax> <latencyFraction>
 *              <noScoSlots> <noIdleSlots> <optOffRssi> <optOnRssi> <rtsCount>"
 *
 * BT PARAM A2DP:
 *      "-pA2DP <wlanUsageLimit> <burstCountMin> <dataRespTimeout> <master-slave> <a2dpOptFlags> <optoffRssi> <optonRssi> <rtsCount>"
 *
 * BT PARAM MISC - WLAN Tx Protection:
 *      "-pMISC_TxProtect <period> <dutycycle> <stompbt> <protect policy>"
 *
 * BT PARAM MISC - Coex Policy:
 *      "-pMISC_Coex <ctrlflags>"
 *
 * BT PARAM REGISTERS :
 *      "-pREGS <mode> <sco weights> <a2dp weights> <general weights> <mode2> <set values>"
 *
 * A string may contain multiple commands delimited by a continuation character ';' .  For example:
 *
 *    "-pA2DP 100 40 1 ; -s 2 1"
 *
 *    Would issue a BT PARAM-A2DP command followed by a BT STATUS command.
 *
 */

typedef struct _BT_CONTROL_ACTION {
    BT_CONTROL_ACTION_TYPE      Type;                                    /* the action type (i.e. status, params) */
    A_UCHAR                     Buffer[MAX_BT_CONTROL_ACTION_DATA_SIZE]; /* buffer holding parameters */
    int                         Length;                                  /* length of parameters */
} BT_CONTROL_ACTION;

typedef struct _BT_CONTROL_ACTION_ITEM {
    DL_LIST                 ListEntry;
    BT_CONTROL_ACTION       ControlAction;
} BT_CONTROL_ACTION_ITEM;

    /* BT Core state blob */
typedef struct _BT_FILTER_CORE_INFO {
    BT_FILTER_CORE_STATE         FilterState;
    A_UINT32                     StateFilterIgnore;   /* bitmap of state to ignore*/
    BT_CONTROL_ACTION_DESC_STATE ActionDescriptors[ATH_BT_MAX_STATE_INDICATION];
    DL_LIST                      ActionListsState[ATH_BT_MAX_STATE_INDICATION][STATE_MAX];
} BT_FILTER_CORE_INFO;

/* filter core APIs called by the OS adaptation layer */

A_BOOL FCore_Init(BT_FILTER_CORE_INFO *pCore);
void FCore_Cleanup(BT_FILTER_CORE_INFO *pCore);
void FCore_ResetActionDescriptors(BT_FILTER_CORE_INFO *pCore);

/* filter BT commands and events and extract current BT radio state
 * ***** NOTE: the caller must protect this function with locks to protect internal
 *             state.  This is however stack dependent. Some stacks may serialize HCI command and
 *             event processing. Some may allow these two operations to overlap.
 */
ATHBT_STATE_INDICATION FCore_FilterBTCommand(BT_FILTER_CORE_INFO *pCore, A_UINT8 *pBuffer, int Length, ATHBT_STATE *pNewState);
ATHBT_STATE_INDICATION FCore_FilterBTEvent(BT_FILTER_CORE_INFO *pCore, A_UINT8 *pBuffer, int Length, ATHBT_STATE *pNewState);

/* filter ACL IN and OUT data to obtain BT application states
 * ***** NOTE: the caller must protect this function with locks to protect internal
 *             state.  
 */
ATHBT_STATE_INDICATION FCore_FilterACLDataOut(BT_FILTER_CORE_INFO *pCore,
                                              A_UINT8             *pBuffer, 
                                              int                 Length, 
                                              ATHBT_STATE         *pNewState);
                                              
ATHBT_STATE_INDICATION FCore_FilterACLDataIn(BT_FILTER_CORE_INFO *pCore,
                                             A_UINT8             *pBuffer, 
                                             int                 Length, 
                                             ATHBT_STATE         *pNewState);
                                      
/* core state reset, usually called when the WLAN side goes away */
#define FCore_ResetState(p)  /* doesn't do anything now */

/* get the action list tem for this state indication
 * The caller should pass pStart=NULL to obtain the first item.  Then using each item returned,
 * the caller can set pStart to that item and obtain the *next* item */
BT_CONTROL_ACTION_ITEM *FCore_GetControlAction(BT_FILTER_CORE_INFO       *pCore,
                                               ATHBT_STATE_INDICATION    Indication,
                                               ATHBT_STATE              State,
                                               BT_CONTROL_ACTION_ITEM    *pStart);

A_STATUS FCore_RefreshActionList(BT_FILTER_CORE_INFO *pCore);

/* called to indicate the precise state of the BT radio when determined by external means */
ATHBT_STATE_INDICATION FCore_FilterIndicatePreciseState(BT_FILTER_CORE_INFO    *pCore,
                                                        ATHBT_STATE_INDICATION Indication,
                                                        ATHBT_STATE            State,
                                                        ATHBT_STATE           *pNewState);

A_UINT32 FCore_GetCurrentBTStateBitMap(BT_FILTER_CORE_INFO    *pCore);

typedef enum _ATHBT_MODIFY_CONTROL_ACTION {
    ATHBT_MODIFY_CONTROL_ACTION_NOOP,
    ATHBT_MODIFY_CONTROL_ACTION_APPEND,
    ATHBT_MODIFY_CONTROL_ACTION_REPLACE
} ATHBT_MODIFY_CONTROL_ACTION;

/* called to modify a control action string (Replace or Append) */
A_STATUS FCore_ModifyControlActionString(BT_FILTER_CORE_INFO         *pCore,
                                         ATHBT_STATE_INDICATION       Indication,
                                         ATHBT_STATE                  State,
                                         A_CHAR                       *pAction,
                                         int                          StringLength,
                                         ATHBT_MODIFY_CONTROL_ACTION  ModifyAction);


#endif /*BTFILTER_INTERNAL_H_*/



