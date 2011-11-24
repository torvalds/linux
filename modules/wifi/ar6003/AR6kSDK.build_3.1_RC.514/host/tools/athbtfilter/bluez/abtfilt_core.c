//------------------------------------------------------------------------------
// <copyright file="abtfilt_core.c" company="Atheros">
//    Copyright (c) 2011 Atheros Corporation.  All rights reserved.
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

//==============================================================================
/*
 * Bluetooth Filter Front End
 *
 */

#include "abtfilt_int.h"

/* Defines */
#define MAX_VAL_DATA_LENGTH     128

const A_CHAR *g_IndicationStrings[ATH_BT_MAX_STATE_INDICATION] =
{
   "NOP",
   "INQUIRY",
   "CONNECT",
   "SCO",
   "ACL",
   "A2DP",
   "ESCO",
};

extern A_FILE_HANDLE gConfigFile;

/* Function Prototypes */
static void BtStateActionProper(ATHBT_FILTER_INFO *pInfo,
                                ATHBT_STATE_INDICATION Indication,
                                ATHBT_STATE State);
static void DoBtStateAction(ATHBT_FILTER_INFO *pInfo,
                            ATHBT_STATE_INDICATION Indication,
                            ATHBT_STATE State);
static void AthFilterCmdEventsCallback(void *pContext,
                                       ATHBT_HCI_CTRL_TYPE Type,
                                       unsigned char *pBuffer, int Length);
static void AthFilterIndicateStateCallback(void *pContext,
                                           ATHBT_STATE_INDICATION Indication, 
                                           ATHBT_STATE State, 
                                           unsigned char LMPVersion);
static void AthFilterAclDataOutCallback(void *pContext,
                                        unsigned char *pBuffer, int Length);
static void AthFilterAclDataInCallback(void *pContext, 
                                       unsigned char *pBuffer, int Length);
static void ProcessBTActionMessages(ATHBT_FILTER_INFO      *pInfo, 
                                    BTACTION_QUEUE_PROC    Process,
                                    ATHBT_STATE_INDICATION StateToFlush);
static void ExecuteBtAction(ATHBT_FILTER_INFO *pInfo, 
                            BT_ACTION_MSG *pBtActionMsg);
static ATHBT_STATE_INDICATION IndicateA2DP(ATHBT_FILTER_INFO *pInfo , 
                                           ATHBT_STATE_INDICATION Indication,
                                           ATHBT_STATE State,
                                           unsigned char *pACLBuffer);
static void *FilterThread(void *arg);
static void SyncBTState(ATHBT_FILTER_INFO *pInfo);
static void ProcessActionOverride(ATHBT_FILTER_INFO *pInfo, 
                                  A_CHAR            *pIndicationStr, 
                                  A_CHAR            *pModifyAction,
                                  A_CHAR            *pAction);
static void GetActionStringOverrides(ATHBT_FILTER_INFO *pInfo);

/* APIs exported to other modules */
void 
AthBtFilter_State_Off(ATHBT_FILTER_INFO *pInfo)
{
            /*
             * before we exit we need to counter-act the coexistence
             * settings. Currently we just indicate that each state is now
             * OFF (if they are ON). This state synchronization is typically
             * required on HOT-removable BT adapters or where the low level
             * adapter can be surprise removed before the BT stack can clean
             * up HCI connections and states
             */
            if (pInfo->AdapterAvailable) {
                int         indication, newIndication;
                ATHBT_STATE newState;

                /*
                 * the BT adapter is going away, indicate that all indications
                 * are now in the OFF state, this may queue up control action
                 * messages, which is okay
                 */
                for (indication = 0; indication < ATH_BT_MAX_STATE_INDICATION;
                     indication++)
                {
                    A_LOCK_FILTER(pInfo);
                    newIndication =
                        FCore_FilterIndicatePreciseState(&pInfo->FilterCore,
                                          indication, STATE_OFF, &newState);
                    A_UNLOCK_FILTER(pInfo);

                    if (newIndication == ATH_BT_NOOP) {
                        continue;
                    }

                    DoBtStateAction(pInfo, indication, newState);
                }

                /* issue control actions */
                ProcessBTActionMessages(pInfo, BTACTION_QUEUE_SYNC_STATE,
                                        ATH_BT_NOOP);
            }

       
}

int
AthBtFilter_Attach(ATH_BT_FILTER_INSTANCE *pInstance, A_UINT32 flags)
{
    int                             i;
    int                             retVal = -1;
    ATHBT_FILTER_INFO              *pInfo = NULL;
    BT_ACTION_MSG                  *pBTActionMsg;
    A_UINT32                        maxBTActionMsgs = MAX_BT_ACTION_MESSAGES;
    A_STATUS                        status;
    BT_FILTER_CORE_INFO         *   pCoreInfo = NULL;

    do {
        pInfo = (ATHBT_FILTER_INFO *)A_MALLOC(sizeof(ATHBT_FILTER_INFO) +
                                   maxBTActionMsgs * (sizeof(BT_ACTION_MSG)));
        if (NULL == pInfo) {
            A_ERR("[%s] Failed to allocate BT filter info\n", __FUNCTION__);
            break;
        }
        A_MEMZERO(pInfo, sizeof(ATHBT_FILTER_INFO));

        pInstance->pContext = pInfo;
        pInfo->pInstance = pInstance;
        pInfo->MaxBtActionMessages = (int)maxBTActionMsgs;
        pInfo->AdapterAvailable = FALSE;
        pInfo->Shutdown = FALSE;

        status = A_MUTEX_INIT(&pInfo->CritSection);
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to initialize critical section\n",
                  __FUNCTION__);
            break;
        }

        DL_LIST_INIT(&pInfo->BTActionMsgList);
        DL_LIST_INIT(&pInfo->FreeActionMsgList);
        pCoreInfo = &pInfo->FilterCore;
        pCoreInfo->FilterState.btFilterFlags = flags;

        A_DEBUG("Calling Fcore Init\n");
        if (!FCore_Init(pCoreInfo)) {
            A_DEBUG(" Fcore Init failed \n");
            break;
        }

        GetActionStringOverrides(pInfo);

        status = FCore_RefreshActionList(&pInfo->FilterCore);
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed refresh action list (status:%d)\n",
                  __FUNCTION__, status);
            break;
        }

        /* message buffers are at the end of our context blob */
        pBTActionMsg = (BT_ACTION_MSG *)((A_UCHAR *)pInfo +
                                         sizeof(ATHBT_FILTER_INFO));

        for (i = 0; i < pInfo->MaxBtActionMessages; i++, pBTActionMsg++) {
            /* create the event for blocking requests */
            status = A_COND_INIT(&pBTActionMsg->hWaitEvent);
            if (A_FAILED(status)) {
                A_ERR("[%s] Failed to allocate BT action event wait object\n",
                      __FUNCTION__);
                /* if we are running out of memory we'll fail farther down */
                break;
            }

            status = A_MUTEX_INIT(&pBTActionMsg->hWaitEventLock);
            if (A_FAILED(status)) {
                A_ERR("[%s] Failed to initialize the mutex\n", __FUNCTION__);
                /* if we are running out of memory we'll fail farther down */
                break;
            }

            /* free to list */
            FREE_BT_ACTION_MSG(pInfo, pBTActionMsg);
        }

        /* create the wake event for our dispatcher thread */
        status = A_COND_INIT(&pInfo->hWakeEvent);
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to allocate wakeup event\n", __FUNCTION__);
            break;
        }

        status = A_MUTEX_INIT(&pInfo->hWakeEventLock);
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to initialize critical section\n",
                  __FUNCTION__);
            break;
        }

        /*
         * get the event types that the filter core can ignore. The BT
         * notification side can handle them TODO
         */
        pInfo->FilterCore.StateFilterIgnore = 0;

        pInstance->pFilterCmdEvents = AthFilterCmdEventsCallback;
        pInstance->pIndicateState = AthFilterIndicateStateCallback;
        pInstance->pFilterAclDataOut = AthFilterAclDataOutCallback;
        pInstance->pFilterAclDataIn = AthFilterAclDataInCallback;

        /*
         * We are fully initialized and ready to filter. The filter core
         * needs to stay in sync with the BT radio state until the WLAN
         * driver comes up, when the WLAN driver comes on-line the filter
         * will issue operating parameters for the current BT radio state
         * (see HandleAdapterEvent)
         */
        pInstance->FilterEnabled = TRUE;

        status = A_TASK_CREATE(&pInfo->hFilterThread, FilterThread, pInfo);
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to create filter thread\n", __FUNCTION__);
            break;
        }

        pInfo->FilterThreadValid = TRUE;

        retVal = 0;
        A_INFO("BT Filter Core init complete\n");
    } while (FALSE);

    if (retVal < 0) {
        AthBtFilter_Detach(pInstance);
    }

    return retVal;
}

void
AthBtFilter_Detach(ATH_BT_FILTER_INSTANCE *pInstance)
{
    A_STATUS           status;
    ATHBT_FILTER_INFO  *pInfo = (ATHBT_FILTER_INFO  *)pInstance->pContext;
    BT_ACTION_MSG      *pBTActionMsg;
    int                i;

    /* make sure filter is disabled */
    pInstance->FilterEnabled = FALSE;

    if (NULL == pInfo) {
        return;    
    }

    pInfo->Shutdown = TRUE;

        /* wake filter thread */
    A_MUTEX_LOCK(&pInfo->hWakeEventLock);
    A_COND_SIGNAL(&pInfo->hWakeEvent);
    A_MUTEX_UNLOCK(&pInfo->hWakeEventLock);
   
    if (pInfo->FilterThreadValid) {
        pInfo->FilterThreadValid = FALSE;
            /* wait for thread to exit */
        status = A_TASK_JOIN(&pInfo->hFilterThread);
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to JOIN filter thread \n",
                  __FUNCTION__);
        }
    }

    A_COND_DEINIT(&pInfo->hWakeEvent);
    A_MUTEX_DEINIT(&pInfo->hWakeEventLock);

    pBTActionMsg = (BT_ACTION_MSG *)((A_UCHAR *)pInfo +
                                     sizeof(ATHBT_FILTER_INFO));
    /* close all action message wait objects */
    for (i = 0; i < pInfo->MaxBtActionMessages; i++, pBTActionMsg++) {
        A_COND_DEINIT(&pBTActionMsg->hWaitEvent);
        A_MUTEX_DEINIT(&pBTActionMsg->hWaitEventLock);
    }

    pInstance->pContext = NULL;
    A_MUTEX_DEINIT(&pInfo->CritSection);
    FCore_Cleanup(&pInfo->FilterCore);
    A_FREE(pInfo);

    A_INFO("BT Filter Core de-init complete\n");
}

/* Internal functions */
static void
AdjustBtControlAction(ATHBT_FILTER_INFO      *pInfo,
                      BT_ACTION_MSG          *pActionMsg)
{

#define A2DP_CONFIG_ALLOW_OPTIMIZATION        ( 1 << 0)
#define A2DP_CONFIG_EDR_CAPABLE               ( 1 << 1)
#define A2DP_CONFIG_IS_COLOCATED_IS_MASTER    ( 1 << 2)
#define A2DP_CONFIG_A2DP_IS_HIGH_PRI          ( 1 << 3)

    if (pActionMsg->StateForControlAction != STATE_ON) {
         /* nothing to adjust in OFF case */
         return;
    }

    if (pActionMsg->ControlAction.Type != BT_CONTROL_ACTION_PARAMS) {
         /* only modify action that issues a PARAMS control action */
            return;
    }

    if(pInfo->Flags & ABF_WIFI_CHIP_IS_VENUS) {

        if (pActionMsg->IndicationForControlAction == ATH_BT_A2DP) {
            WMI_SET_BTCOEX_A2DP_CONFIG_CMD *pA2dpParamsCmd =
                               (WMI_SET_BTCOEX_A2DP_CONFIG_CMD *)
                               (pActionMsg->ControlAction.Buffer);
            BTCOEX_A2DP_CONFIG *pA2dpGenericConfig =
                                            &pA2dpParamsCmd->a2dpConfig;
            BTCOEX_PSPOLLMODE_A2DP_CONFIG * pA2dpPspollConfig =
                                            &pA2dpParamsCmd->a2dppspollConfig;
            BTCOEX_OPTMODE_A2DP_CONFIG * pA2dpOptModeConfig =
                                            &pA2dpParamsCmd->a2dpOptConfig;

            pA2dpGenericConfig->a2dpFlags = 0;
            /*Role =0 is Master, Role =1, is slave */
            if(pInfo->A2DPConnection_Role == 0) {
                 pA2dpGenericConfig->a2dpFlags |= A2DP_CONFIG_IS_COLOCATED_IS_MASTER;
            }else {
                pA2dpGenericConfig->a2dpFlags &= ~A2DP_CONFIG_IS_COLOCATED_IS_MASTER;
            }

            switch (pInfo->A2DPConnection_LMPVersion) {
                case 0: // 1.0
                case 1: // 1.1
                case 2: // 1.2
                    pA2dpPspollConfig->a2dpWlanMaxDur = 30;
                    pA2dpOptModeConfig->a2dpMaxAggrSize = 1;
                    pA2dpOptModeConfig->a2dpMinlowRateMbps =52;
            	    pA2dpPspollConfig->a2dpDataRespTimeout = 10;
                    pA2dpPspollConfig->a2dpMinBurstCnt = 4;
                    break;
                case 3: // 2.0
                case 4: // 2.1
                default:
            	    pA2dpPspollConfig->a2dpDataRespTimeout = 20;
                    pA2dpPspollConfig->a2dpWlanMaxDur = 50;
                    pA2dpOptModeConfig->a2dpMaxAggrSize = 16;
                    pA2dpOptModeConfig->a2dpMinlowRateMbps =36;
#ifdef MSM_7230
                    if(pA2dpGenericConfig->a2dpFlag & A2DP_CONFIG_IS_COLOCATED_IS_MASTER) {
                    pA2dpPspollConfig->a2dpMinBurstCnt = 1;
                   }else{
                    pA2dpPspollConfig->a2dpMinBurstCnt = 2;
                   }

                    
#else
                    pA2dpPspollConfig->a2dpMinBurstCnt = 2;

#endif
                    /* Indicate that remote device is EDR capable */
                    pA2dpGenericConfig->a2dpFlags |= A2DP_CONFIG_EDR_CAPABLE;
                    break;
            }
            if(pInfo->Flags & ABF_BT_CHIP_IS_ATHEROS) {
                pA2dpGenericConfig->a2dpFlags |= A2DP_CONFIG_A2DP_IS_HIGH_PRI ;
                /* Enable optmization for all the modes */
                pA2dpGenericConfig->a2dpFlags |= A2DP_CONFIG_ALLOW_OPTIMIZATION;
            }else {
                /* Enable optmization for only for master role */
                if(pInfo->A2DPConnection_Role == 0) {
                    pA2dpGenericConfig->a2dpFlags |= A2DP_CONFIG_ALLOW_OPTIMIZATION;
                }else {
                    pA2dpGenericConfig->a2dpFlags &= ~A2DP_CONFIG_ALLOW_OPTIMIZATION;
                    pA2dpPspollConfig->a2dpWlanMaxDur = 30;
                }

            }
            pA2dpOptModeConfig->a2dpLowRateCnt =5;
            pA2dpOptModeConfig->a2dpHighPktRatio = 5;
            pA2dpOptModeConfig->a2dpPktStompCnt = 6;
	  /* Continuation of addressing EV#80876 and EV#80859. Disabling OPT mode always as
	   * device is forced to SLAVE */  
	    pA2dpGenericConfig->a2dpFlags &= ~A2DP_CONFIG_ALLOW_OPTIMIZATION;

            A_DEBUG(("ATHBT: BT PARAMS A2DP Adjustments :\r\n"));
            A_DEBUG(("    a2dpWlanUsageLimit  : %d\r\n"),
                            pA2dpPspollConfig->a2dpWlanMaxDur);
            A_DEBUG(("    a2dpBurstCntMin     : %d\r\n"),
                              pA2dpPspollConfig->a2dpMinBurstCnt);
            A_DEBUG(("    a2dpDataRespTimeout : %d\r\n"),
                                pA2dpPspollConfig->a2dpDataRespTimeout);
            A_DEBUG(("    A2DP OptMode Config-MaxAggrSize : %d\r\n"),
                                 pA2dpOptModeConfig->a2dpMaxAggrSize);
            A_DEBUG(("   A2DP Flags : %d\r\n"),
                             pA2dpGenericConfig->a2dpFlags);
#if 0
            A_DEBUG(("   A2DP OptMode Config - MinLowRateMbps : %d\r\n"),
                            pA2dpOptModeConfig->a2dpMinlowRateMbps);
            A_DEBUG(("   A2DP OptMode Config - LowRateCnt : %d\r\n"),
                            pA2dpOptModeConfig->a2dpLowRateCnt );
            A_DEBUG(("   A2DP High Pkt Ratio Config- PktRatio : %d\r\n"),
                            pA2dpOptModeConfig->a2dpHighPktRatio );
            A_DEBUG(("   A2DP High Pkt Ratio Config- StompCnt : %d\r\n"),
                            pA2dpOptModeConfig->a2dpPktStompCnt );
#endif
        }
        /* adjust control action for BT_PARAMS_SCO control action  */
        if ((pActionMsg->IndicationForControlAction == ATH_BT_SCO) ||
            (pActionMsg->IndicationForControlAction == ATH_BT_ESCO))
        {
            WMI_SET_BTCOEX_SCO_CONFIG_CMD *pScoParamsCmd =
                               (WMI_SET_BTCOEX_SCO_CONFIG_CMD *)
                               (pActionMsg->ControlAction.Buffer);
            BTCOEX_SCO_CONFIG *pScoGenericConfig =
                                            &pScoParamsCmd->scoConfig;
            BTCOEX_PSPOLLMODE_SCO_CONFIG * pScoPspollConfig =
                                            &pScoParamsCmd->scoPspollConfig;
            BTCOEX_OPTMODE_SCO_CONFIG * pScoOptModeConfig =
                                            &pScoParamsCmd->scoOptModeConfig;
            BTCOEX_WLANSCAN_SCO_CONFIG * pScoWlanScanConfig = 
                                            &pScoParamsCmd->scoWlanScanConfig;

            pScoGenericConfig->scoFlags = 0;
            do {
                if ((pInfo->SCOConnectInfo.LinkType == BT_LINK_TYPE_ESCO) &&
                    (pInfo->SCOConnectInfo.Valid))
                {
                    A_UCHAR scoSlots;
                    /* decode packet length to get packet type */
                    if (pInfo->SCOConnectInfo.TxPacketLength <= 30) {
                     /* EV3 */
                        scoSlots = 1;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 60) {
                        /* 2-EV3 */
                        scoSlots = 1;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 90) {
                        /*3-EV3 */
                        scoSlots = 1;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 120) {
                        /* EV4: */
                        scoSlots = 3;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 180) {
                        /* EV5: */
                        scoSlots = 3;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 360) {
                        /* 2-EV5 */
                        scoSlots = 3;
                    } else {
                        /* 3-EV5 */
                        scoSlots = 3;
                    }

                    scoSlots *= 2;
                    pScoGenericConfig->scoSlots = scoSlots;

                    if (pInfo->SCOConnectInfo.TransmissionInterval >= scoSlots) {
                        pScoGenericConfig->scoIdleSlots =
                        pInfo->SCOConnectInfo.TransmissionInterval - scoSlots;
                    } else {
                        A_DEBUG(("Invalid scoSlot,  got:%d, transInt: %d\n"),
                                    scoSlots,
                                    pInfo->SCOConnectInfo.TransmissionInterval);
                   }
                } else {
                       /* legacy SCO */
                    pScoGenericConfig->scoSlots = 2;
                    pScoGenericConfig->scoIdleSlots = 4;
                }
 //               pScoGenericConfig->scoFlags |= WMI_SCO_CONFIG_FLAG_ALLOW_OPTIMIZATION;
                if(pInfo->SCOConnectInfo.LinkType == BT_LINK_TYPE_ESCO) {
                    pScoGenericConfig->scoFlags |= WMI_SCO_CONFIG_FLAG_IS_EDR_CAPABLE;
                }
                if(pScoGenericConfig->scoIdleSlots >= 10) {
                    pScoPspollConfig->scoPsPollLatencyFraction = 2;
                    pScoPspollConfig->scoStompDutyCyleVal = 2;
                    pScoWlanScanConfig->scanInterval = 100;
                    pScoWlanScanConfig->maxScanStompCnt = 2;
                }else {
                    pScoPspollConfig->scoPsPollLatencyFraction = 1;
                    pScoPspollConfig->scoStompDutyCyleVal = 5;
                    pScoWlanScanConfig->scanInterval = 100;
                    pScoWlanScanConfig->maxScanStompCnt = 4;
                }
            }while(FALSE);
            pScoPspollConfig->scoCyclesForceTrigger = 10;
            pScoPspollConfig->scoDataResponseTimeout = 10;
            pScoPspollConfig->scoStompDutyCyleMaxVal = 6;

             A_DEBUG(("ATHBT: BT PARAMS SCO adjustment (%s) \n"),
                pInfo->SCOConnectInfo.LinkType == BT_LINK_TYPE_ESCO ? "eSCO":"SCO");
             A_DEBUG(("    numScoCyclesForceTrigger : %d \n"),
                      pScoPspollConfig->scoCyclesForceTrigger);
             A_DEBUG(("    dataResponseTimeout      : %d \n"),
                    pScoPspollConfig->scoDataResponseTimeout = 20);
             A_DEBUG(("    stompDutyCyleVal         : %d \n"),
                    pScoPspollConfig->scoStompDutyCyleVal);
             A_DEBUG(("    psPollLatencyFraction    : %d \n"),
                pScoPspollConfig->scoPsPollLatencyFraction);
             A_DEBUG(("    noSCOSlots               : %d \n"),
                pScoGenericConfig->scoSlots);
             A_DEBUG(("    noIdleSlots              : %d \n"),
                pScoGenericConfig->scoIdleSlots);
     	    A_DEBUG((" scoFlags                      : %d \n"),
                pScoGenericConfig->scoFlags);

             pScoOptModeConfig->scoStompCntIn100ms = 3;
             pScoOptModeConfig->scoContStompMax = 3;
             pScoOptModeConfig->scoMinlowRateMbps = 36;
             pScoOptModeConfig->scoLowRateCnt = 5;
             pScoOptModeConfig->scoHighPktRatio = 5;

             if(pScoGenericConfig->scoIdleSlots >= 10) {
                 pScoOptModeConfig->scoMaxAggrSize = 8;
             }else {
                 pScoOptModeConfig->scoMaxAggrSize = 1;
             }

        }
    }
    else
    {
        WMI_SET_BT_PARAMS_CMD   *pParamsCmd;
        if (pActionMsg->IndicationForControlAction == ATH_BT_A2DP) {
            do {
                pParamsCmd =
                   (WMI_SET_BT_PARAMS_CMD *)(pActionMsg->ControlAction.Buffer);

                if (pParamsCmd->paramType != BT_PARAM_A2DP) {
                        /* only modify A2DP params */
                    break;
                }
                /*Role =0 is Master, Role =1, is slave */
                if(pInfo->A2DPConnection_Role == 0) {
                    pParamsCmd->info.a2dpParams.isCoLocatedBtRoleMaster = 1;
                }else {
                    pParamsCmd->info.a2dpParams.isCoLocatedBtRoleMaster = 0;
                    /* workaround for local BT radio that disables EDR
                     * rates when operating as a slave. We downgrade
                     * the remote lmp version to protect A2DP as if the radio was 1.2 */
                     pInfo->A2DPConnection_LMPVersion = 2;
                }

                switch (pInfo->A2DPConnection_LMPVersion) {
                case 0: // 1.0
                case 1: // 1.1
                case 2: // 1.2
                    pParamsCmd->info.a2dpParams.a2dpWlanUsageLimit = 30;
                    pParamsCmd->info.a2dpParams.a2dpBurstCntMin = 3;
                    pParamsCmd->info.a2dpParams.a2dpDataRespTimeout =10;
                    break;
                case 3: // 2.0
                case 4: // 2.1
                default:
                    if( pParamsCmd->info.a2dpParams.isCoLocatedBtRoleMaster) {
                        /* allow close range optimization for newer BT radios */
                    }
                    pParamsCmd->info.a2dpParams.a2dpWlanUsageLimit = 100;
                    pParamsCmd->info.a2dpParams.a2dpBurstCntMin = 1;
                    pParamsCmd->info.a2dpParams.a2dpDataRespTimeout =10;
                    break;
                }

                A_DEBUG(("ATHBT: BT PARAMS A2DP Adjustments :\r\n"));
                A_DEBUG(("    a2dpWlanUsageLimit  : %d\r\n"),
                        pParamsCmd->info.a2dpParams.a2dpWlanUsageLimit);
                A_DEBUG(("    a2dpBurstCntMin     : %d\r\n"),
                        pParamsCmd->info.a2dpParams.a2dpBurstCntMin);
                A_DEBUG(("    a2dpDataRespTimeout : %d\r\n"),
                        pParamsCmd->info.a2dpParams.a2dpDataRespTimeout);
            }while (FALSE);
        }

        /* adjust control action for BT_PARAMS_SCO control action  */
        if ((pActionMsg->IndicationForControlAction == ATH_BT_SCO) ||
            (pActionMsg->IndicationForControlAction == ATH_BT_ESCO))
        {
            do {
                pParamsCmd =
                    (WMI_SET_BT_PARAMS_CMD *)(pActionMsg->ControlAction.Buffer);

                if (pParamsCmd->paramType != BT_PARAM_SCO) {
                    /* only modify SCO params */
                    break;
                }

                if ((pInfo->SCOConnectInfo.LinkType == BT_LINK_TYPE_ESCO) &&
                    (pInfo->SCOConnectInfo.Valid)) {
                    A_UCHAR scoSlots;
                    pInfo->SCOConnectInfo.Valid = FALSE;
                    /* decode packet length to get packet type */
                    if (pInfo->SCOConnectInfo.TxPacketLength <= 30) {
                        /* EV3 */
                        scoSlots = 1;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 60) {
                        /* 2-EV3 */
                        scoSlots = 1;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 90) {
                        /*3-EV3 */
                        scoSlots = 1;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 120) {
                        /* EV4: */
                        scoSlots = 3;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 180) {
                        /* EV5: */
                        scoSlots = 3;
                    } else if (pInfo->SCOConnectInfo.TxPacketLength <= 360) {
                        /* 2-EV5 */
                        scoSlots = 3;
                    } else {
                        /* 3-EV5 */
                        scoSlots = 3;
                    }

                    /* account for RX/TX */
                    scoSlots *= 2;
                    pParamsCmd->info.scoParams.noSCOSlots =  scoSlots;

                    if (pInfo->SCOConnectInfo.TransmissionInterval >= scoSlots) {
                        pParamsCmd->info.scoParams.noIdleSlots =
                            pInfo->SCOConnectInfo.TransmissionInterval - scoSlots;
                    } else {
                        A_DEBUG(("Invalid scoSlot,  got:%d, transInt: %d\n"),
                                scoSlots,
                                pInfo->SCOConnectInfo.TransmissionInterval);
                    }
                } else {
                   /* legacy SCO */
                    pParamsCmd->info.scoParams.noSCOSlots = 2;
                    pParamsCmd->info.scoParams.noIdleSlots = 4;
                }

                A_DEBUG(("ATHBT: BT PARAMS SCO adjustment (%s) \n"),
                        pInfo->SCOConnectInfo.LinkType == BT_LINK_TYPE_ESCO ? "eSCO":"SCO");
                A_DEBUG(("    numScoCyclesForceTrigger : %d \n"),
                        pParamsCmd->info.scoParams.numScoCyclesForceTrigger);
                A_DEBUG(("    dataResponseTimeout      : %d \n"),
                        pParamsCmd->info.scoParams.dataResponseTimeout);
                A_DEBUG(("    stompScoRules            : %d \n"),
                        pParamsCmd->info.scoParams.stompScoRules);
                A_DEBUG(("    stompDutyCyleVal         : %d \n"),
                        pParamsCmd->info.scoParams.stompDutyCyleVal);
                A_DEBUG(("    psPollLatencyFraction    : %d \n"),
                        pParamsCmd->info.scoParams.psPollLatencyFraction);
                A_DEBUG(("    noSCOSlots               : %d \n"),
                        pParamsCmd->info.scoParams.noSCOSlots);
                A_DEBUG(("    noIdleSlots              : %d \n"),
                        pParamsCmd->info.scoParams.noIdleSlots);
            } while (FALSE);
        }
    }
}

static void
BtStateActionProper(ATHBT_FILTER_INFO *pInfo,
                    ATHBT_STATE_INDICATION Indication, ATHBT_STATE State)
{
    A_COND_OBJECT           *hWait = NULL;
    BT_ACTION_MSG           *pActionMsg;
    DL_LIST                 *pListEntry;
    BT_CONTROL_ACTION_ITEM  *pBtControlAction;
    int                     queued = 0;

    A_LOCK_FILTER(pInfo);

    pBtControlAction = FCore_GetControlAction(&pInfo->FilterCore,
                                              Indication,
                                              State,
                                              NULL);

    A_DEBUG("[%s], Indication =%d, state=%d,ControlAction.Length=%d\n",
            __FUNCTION__, Indication, State,
           ( (pBtControlAction == NULL)? 0 : pBtControlAction->ControlAction.Length));

    while (pBtControlAction != NULL) {
        /* allocate an action message */
        pListEntry = DL_ListRemoveItemFromHead(&pInfo->FreeActionMsgList);

        if (NULL == pListEntry) {
            A_DEBUG("action messages exhausted\n");
            break;
        }

        pActionMsg = A_CONTAINING_STRUCT(pListEntry, BT_ACTION_MSG,
                                         ListEntry);

        /* save state for later flushing */
        pActionMsg->StateForControlAction = State;
        pActionMsg->IndicationForControlAction = Indication;

        /* we need to buffer the control actions */
        A_MEMCPY(&pActionMsg->ControlAction,
                 &pBtControlAction->ControlAction,
                 sizeof(pActionMsg->ControlAction));

        /* When is it ever set to blocking TODO */
        if (pActionMsg->Blocking) {
            /* this is the action to wait on */
            hWait = &pActionMsg->hWaitEvent;
            A_COND_RESET(hWait);
        }
        /* allow for adjustments to the control action beyond the defaults */
        AdjustBtControlAction(pInfo, pActionMsg);

        /* queue action */
        QUEUE_BT_ACTION_MSG(pInfo, pActionMsg);
        queued++;

        /* get the next action using the current one as a starter */
        pBtControlAction = FCore_GetControlAction(&pInfo->FilterCore,
                                                  Indication,
                                                  State,
                                                  pBtControlAction);
    }

    A_UNLOCK_FILTER(pInfo);

    if (queued > 0) {
        /* wake thread to process all the queued up actions */
        A_MUTEX_LOCK(&pInfo->hWakeEventLock);
        A_COND_SIGNAL(&pInfo->hWakeEvent);
        A_MUTEX_UNLOCK(&pInfo->hWakeEventLock);
    }

    /* check if we need to block until the dispatch thread issued the
     * last action if the adapter becomes unavailable we cannot block
     * the thread (queue will stall), so only block if the adapter is
     * available and use a reasonable timeout
     */
    if (hWait) {
        A_COND_WAIT(hWait, &pInfo->CritSection, ACTION_WAIT_TIMEOUT);
    }
}

static void DoBtStateAction(ATHBT_FILTER_INFO *pInfo, ATHBT_STATE_INDICATION Indication, ATHBT_STATE State)
{
    A_UINT32 bitmap = FCore_GetCurrentBTStateBitMap(&pInfo->FilterCore);

    if ((Indication == ATH_BT_INQUIRY) && (State == STATE_ON)) {
        int i;
        for (i=0; i<ATH_BT_MAX_STATE_INDICATION; ++i) {
            if ((i!=Indication) && (bitmap & (1<<i))) {
                BtStateActionProper(pInfo,i,STATE_OFF);
            }
        }
    }

    if ((Indication == ATH_BT_A2DP) && ((bitmap & (1<<ATH_BT_SCO)) || (bitmap & (1<<ATH_BT_ESCO)))) {
        /* SCO protection is running, don't take any actions */
        return;
    }

    if ((Indication == ATH_BT_A2DP) && (bitmap & (1<<ATH_BT_INQUIRY))) {
        BtStateActionProper(pInfo,ATH_BT_INQUIRY, STATE_OFF);
    }

    if (((Indication == ATH_BT_SCO) || (Indication == ATH_BT_ESCO)) && (State == STATE_ON)) {
        if (bitmap & (1<<ATH_BT_A2DP)) {
            BtStateActionProper(pInfo,ATH_BT_A2DP,STATE_OFF);
        }
    }

    BtStateActionProper(pInfo, Indication, State);

    if ((Indication == ATH_BT_INQUIRY) && (State == STATE_OFF)) {
        SyncBTState(pInfo);
    }

    if (((Indication == ATH_BT_SCO) || (Indication == ATH_BT_ESCO)) && (State == STATE_OFF)) {
        if (bitmap & (1<<ATH_BT_A2DP)) {
            BtStateActionProper(pInfo,ATH_BT_A2DP,STATE_ON);
        }
    }
}

static void
AthFilterCmdEventsCallback(void *pContext, ATHBT_HCI_CTRL_TYPE Type,
                           unsigned char *pBuffer, int Length)
{
    ATHBT_STATE_INDICATION  indication;
    ATHBT_FILTER_INFO       *pInfo = (ATHBT_FILTER_INFO *)pContext;
    ATHBT_STATE             state;

    if (Type == ATHBT_HCI_EVENT) {
        if (HCI_GET_EVENT_CODE(pBuffer) == HCI_EVT_NUM_COMPLETED_PKTS) {
            /* don't delays these packets, we don't act on them anyways */
            return;
        }
    }

    if (pInfo->Shutdown) {
        return;
    }

   /*
    * the filter state machine needs to be protected in case the HCI layer
    * can process commands and events in an unserialize manner
    */
    A_LOCK_FILTER(pInfo);

    if (Type == ATHBT_HCI_COMMAND) {
        A_DUMP_BUFFER(pBuffer, Length, "BT HCI Command");
        indication = FCore_FilterBTCommand(&pInfo->FilterCore, pBuffer,
                                           Length, &state);
    } else {
        A_DUMP_BUFFER(pBuffer, Length, "BT HCI Event");
        indication = FCore_FilterBTEvent(&pInfo->FilterCore, pBuffer,
                                         Length, &state);
        /* check SCO and ESCO connection events */
        if ((indication == ATH_BT_SCO) || (indication == ATH_BT_ESCO)) {
            if (HCI_GET_EVENT_CODE(pBuffer) == HCI_EVT_SCO_CONNECT_COMPLETE) {
                A_DEBUG(("SCO_CONNECT_COMPLETE (%s)\n"),
                        (state == STATE_ON) ? "ON" : "OFF");
                if (state == STATE_ON) {
                    /* save these off for the BT Action adjustment */
                    pInfo->SCOConnectInfo.LinkType =
                        GET_BT_CONN_LINK_TYPE(pBuffer);
                    pInfo->SCOConnectInfo.TransmissionInterval =
                        GET_TRANS_INTERVAL(pBuffer);
                    pInfo->SCOConnectInfo.RetransmissionInterval =
                        GET_RETRANS_INTERVAL(pBuffer);
                    pInfo->SCOConnectInfo.RxPacketLength =
                        GET_RX_PKT_LEN(pBuffer);
                    pInfo->SCOConnectInfo.TxPacketLength =
                        GET_TX_PKT_LEN(pBuffer);
                    A_DEBUG(("ATHBT: SCO conn info (%d, %d, %d, %d, %d))\n"),
                            pInfo->SCOConnectInfo.LinkType,
                            pInfo->SCOConnectInfo.TransmissionInterval,
                            pInfo->SCOConnectInfo.RetransmissionInterval,
                            pInfo->SCOConnectInfo.RxPacketLength,
                            pInfo->SCOConnectInfo.TxPacketLength);
                    /* now valid */
                    pInfo->SCOConnectInfo.Valid = TRUE;
                } else {
                    /* disconnected, invalidate */
                    pInfo->SCOConnectInfo.Valid = FALSE; 
                }             
            }
        }
    }
    
    A_UNLOCK_FILTER(pInfo);
    
    if (indication == ATH_BT_NOOP) {
        return;    
    }

    A_DEBUG(("New Indication :%d State:%s (map:0x%X)\n"), 
            indication, (state == STATE_ON) ? "ON" : "OFF", 
            FCore_GetCurrentBTStateBitMap(&pInfo->FilterCore)); 

    if (pInfo->AdapterAvailable) {
        DoBtStateAction(pInfo, indication, state);
    }
}

static ATHBT_STATE_INDICATION 
IndicateA2DP(ATHBT_FILTER_INFO        *pInfo, 
             ATHBT_STATE_INDICATION   Indication,
             ATHBT_STATE              State,
             unsigned char            *pACLBuffer)
{
    A_ERR("[%s] Not yet implemented\n", __FUNCTION__);

    return ATH_BT_NOOP; /* TODO */
}

static void 
AthFilterAclDataOutCallback(void *pContext, unsigned char *pBuffer, int Length)
{
    ATHBT_STATE_INDICATION  indication;
    ATHBT_FILTER_INFO       *pInfo = (ATHBT_FILTER_INFO *)pContext;
    ATHBT_STATE             state;
    
    if (pInfo->Shutdown) {
        return;    
    }
    
    A_LOCK_FILTER(pInfo);
    
    indication = FCore_FilterACLDataOut(&pInfo->FilterCore,
                                        pBuffer, 
                                        Length, 
                                        &state);
                                        
    if (indication == ATH_BT_A2DP) {
        indication = IndicateA2DP(pInfo, 
                                  ATH_BT_A2DP,
                                  state,
                                  pBuffer);   
    }
    
    A_UNLOCK_FILTER(pInfo);
    
    if (indication == ATH_BT_NOOP) {
        return;    
    }

    A_DEBUG(("New Indication :%d State:%s (map:0x%X)\n"), 
            indication, (state == STATE_ON) ? "ON" : "OFF", 
            FCore_GetCurrentBTStateBitMap(&pInfo->FilterCore));

    if (pInfo->AdapterAvailable) {
        DoBtStateAction(pInfo, indication, state);
    }
    
}

static void 
AthFilterAclDataInCallback(void *pContext, unsigned char *pBuffer, int Length)
{   
    ATHBT_STATE_INDICATION  indication;
    ATHBT_FILTER_INFO       *pInfo = (ATHBT_FILTER_INFO *)pContext;
    ATHBT_STATE             state;

    if (pInfo->Shutdown) {
        return;
    }

    A_LOCK_FILTER(pInfo);

    indication = FCore_FilterACLDataIn(&pInfo->FilterCore,
                                        pBuffer,
                                        Length,
                                        &state);

    if (indication == ATH_BT_A2DP) {
        indication = IndicateA2DP(pInfo,
                                  ATH_BT_A2DP,
                                  state,
                                  pBuffer);
    }

    A_UNLOCK_FILTER(pInfo);

    if (indication == ATH_BT_NOOP) {
        return;
    }

    A_DEBUG(("New Indication :%d State:%s (map:0x%X)\n"),
            indication, (state == STATE_ON) ? "ON" : "OFF",
            FCore_GetCurrentBTStateBitMap(&pInfo->FilterCore));

    if (pInfo->AdapterAvailable) {
        DoBtStateAction(pInfo, indication, state);
    }
}

static void
AthFilterIndicateStateCallback(void *pContext,
                               ATHBT_STATE_INDICATION Indication,
                               ATHBT_STATE State, unsigned char LMPVersion)
{
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pContext;
    ATHBT_STATE             newState;

    if (pInfo->Shutdown) {
        return;
    }

    A_LOCK_FILTER(pInfo);
    Indication = FCore_FilterIndicatePreciseState(&pInfo->FilterCore,
                                            Indication, State, &newState);
    A_UNLOCK_FILTER(pInfo);

    if (Indication == ATH_BT_NOOP) {
        return;
    }

    A_DEBUG(("New Indication :%d State:%s (map:0x%X) \r\n"),
            Indication, (newState == STATE_ON) ? "ON" : "OFF",
            FCore_GetCurrentBTStateBitMap(&pInfo->FilterCore));

    if ((newState == STATE_ON) && (LMPVersion < 5)) {
        pInfo->LMPVersion = LMPVersion;
    }

    if (pInfo->AdapterAvailable) {
        DoBtStateAction(pInfo, Indication, newState);
    }
}

static void *
FilterThread(void *pContext)
{
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pContext;

    A_INFO("Starting the BT Filter task\n");
    while (1) {
        /* Wait to be woken up the BT thread */
        A_MUTEX_LOCK(&pInfo->hWakeEventLock);
        A_COND_WAIT(&pInfo->hWakeEvent, &pInfo->hWakeEventLock, WAITFOREVER);
        A_MUTEX_UNLOCK(&pInfo->hWakeEventLock);

        if (pInfo->AdapterAvailable) {
            ProcessBTActionMessages(pInfo, BTACTION_QUEUE_NORMAL, ATH_BT_NOOP);
        }

        if (pInfo->Shutdown) {
            /*
             * before we exit we need to counter-act the coexistence
             * settings. Currently we just indicate that each state is now 
             * OFF (if they are ON). This state synchronization is typically 
             * required on HOT-removable BT adapters or where the low level 
             * adapter can be surprise removed before the BT stack can clean 
             * up HCI connections and states 
             */
            if (pInfo->AdapterAvailable) {
                int         indication, newIndication;
                ATHBT_STATE newState;
                
                /* 
                 * the BT adapter is going away, indicate that all indications 
                 * are now in the OFF state, this may queue up control action 
                 * messages, which is okay 
                 */
                for (indication = 0; indication < ATH_BT_MAX_STATE_INDICATION;
                     indication++) 
                {
                    A_LOCK_FILTER(pInfo);
                    newIndication = 
                        FCore_FilterIndicatePreciseState(&pInfo->FilterCore, 
                                          indication, STATE_OFF, &newState);
                    A_UNLOCK_FILTER(pInfo);

                    if (newIndication == ATH_BT_NOOP) {
                        continue;
                    }
                    
                    DoBtStateAction(pInfo, indication, newState);
                }
                
                /* issue control actions */
                ProcessBTActionMessages(pInfo, BTACTION_QUEUE_SYNC_STATE, 
                                        ATH_BT_NOOP);
            }

            break;    
        } 
    }

    A_INFO("Terminating the BT Filter task\n");
    return NULL;
}

static void
ProcessBTActionMessages(ATHBT_FILTER_INFO      *pInfo,
                        BTACTION_QUEUE_PROC    Process,
                        ATHBT_STATE_INDICATION StateToFlush)
{
    BT_ACTION_MSG *pActionMsg = NULL;
    DL_LIST       *pListEntry = NULL;

    A_LOCK_FILTER(pInfo);

    while (1) {
        /* determine how we want to pull the message out */
        if (BTACTION_QUEUE_FLUSH_STATE == Process) {
            if (NULL == pListEntry) {
                /* first time through */
                if (!DL_LIST_IS_EMPTY(&pInfo->BTActionMsgList)) {
                    /* get the entry at the head of the list , don't remove */
                    pListEntry =
                        DL_LIST_GET_ITEM_AT_HEAD(&pInfo->BTActionMsgList);
                }
            } else {
                /* move onto the next one */
                pListEntry = pListEntry->pNext;
                if (pListEntry == &pInfo->BTActionMsgList) {
                    /* reached the end */
                    pListEntry = NULL;
                }
            }
        } else {
            /*
             * for all others we are removing items from the head of the
             * queue
             */
            pListEntry = DL_ListRemoveItemFromHead(&pInfo->BTActionMsgList);
        }

        if (NULL == pListEntry) {
            /* queue was empty */
            break;
        }

        pActionMsg = A_CONTAINING_STRUCT(pListEntry, BT_ACTION_MSG, ListEntry);

        /* now determine what to do with the message that was found */
        switch (Process) {
            case BTACTION_QUEUE_FLUSH_STATE:
                /*
                 * caller wants to just flush action messages matching a
                 * state
                 */
                if (pActionMsg->StateForControlAction == StateToFlush) {
                    A_DEBUG(("Removed action for state=%d from queue\n"),
                            StateToFlush);

                    /* remove from list, it will get freed below */
                    DL_ListRemove(&pActionMsg->ListEntry);

                    /*
                     * this would re-start the scan to the head of the list
                     * each time we found one.  This type of flush doesn't
                     * happen very often so restarting from the head of the
                     * list and rescanning isn't time consuming
                     */
                    pListEntry = NULL;
                } else {
                    /* not the one we are interested in */
                    pActionMsg = NULL;
                }
                break;

            case BTACTION_QUEUE_NORMAL:
            case BTACTION_QUEUE_SYNC_STATE:
                /* issue/execute actions */
                A_UNLOCK_FILTER(pInfo);
                A_DEBUG(("Processing action for indication=%d (%s) (%s)\n"),
                        pActionMsg->IndicationForControlAction,
                        (pActionMsg->StateForControlAction == STATE_ON) ?
                        "ON" : "OFF", (BTACTION_QUEUE_SYNC_STATE == Process) ?
                        "Sync State" : "Normal");

                if (BTACTION_QUEUE_SYNC_STATE == Process) {
                    /* let's not issue these too fast ... */
                    usleep(10000);
                }

                ExecuteBtAction(pInfo, pActionMsg);

                if (pActionMsg->Blocking) {
                    pActionMsg->Blocking = FALSE;

                    /* set the event to unblock the caller */
                    A_MUTEX_LOCK(&pActionMsg->hWaitEventLock);
                    A_COND_SIGNAL(&pActionMsg->hWaitEvent);
                    A_MUTEX_UNLOCK(&pActionMsg->hWaitEventLock);
                }
                A_LOCK_FILTER(pInfo); 
                break;

            case BTACTION_QUEUE_FLUSH_ALL:
                A_DEBUG(("Flushed action for state=%d from queue\n"),
                        pActionMsg->StateForControlAction);
                /* 
                 * nothing to do here, the action message will get 
                 * recycled below 
                 */
                break;

            default:
                break;
        }

        if (pActionMsg) {
            /* recycle message */        
            FREE_BT_ACTION_MSG(pInfo, pActionMsg);
        }
    }

    A_UNLOCK_FILTER(pInfo);
}
    
static void 
SyncBTState(ATHBT_FILTER_INFO *pInfo)
{
    int      stateIndication;
    A_UINT32 stateBitMap;

    A_LOCK_FILTER(pInfo);
    stateBitMap = FCore_GetCurrentBTStateBitMap(&pInfo->FilterCore);
    A_UNLOCK_FILTER(pInfo);
    
    /* 
     * the state bit map is a simple STATE ON/OFF bit map, if we detect 
     * that one of the states is ON we process the BT action to synchronize 
     * the WLAN side with the BT radio state 
     */
    for (stateIndication = 0; stateIndication < ATH_BT_MAX_STATE_INDICATION; 
         stateIndication++)
    {
        if (stateBitMap & (1 << stateIndication)) {
            /* this state is ON */
            DoBtStateAction(pInfo, stateIndication, STATE_ON);
        }
    }

}

void
HandleAdapterEvent(ATHBT_FILTER_INFO *pInfo, ATH_ADAPTER_EVENT Event)
{
    A_UINT32 btfiltFlags;
    switch (Event) {
        case ATH_ADAPTER_ARRIVED:
            A_INFO("BT Filter Core : WLAN Arrived \n");
            btfiltFlags = pInfo->Flags;
            Abf_WlanCheckSettings(pInfo->pWlanInfo->IfName, &btfiltFlags);
            if (btfiltFlags != pInfo->Flags) {
                A_STATUS status;
                BT_FILTER_CORE_INFO *pCoreInfo = &pInfo->FilterCore;
                pInfo->Flags &= ~ABF_WIFI_CHIP_IS_VENUS;
                pInfo->Flags |= (btfiltFlags & ABF_WIFI_CHIP_IS_VENUS);
                pCoreInfo->FilterState.btFilterFlags &= ~ABF_WIFI_CHIP_IS_VENUS;
                pCoreInfo->FilterState.btFilterFlags |= (btfiltFlags & ABF_WIFI_CHIP_IS_VENUS);
                FCore_ResetActionDescriptors(pCoreInfo);
                GetActionStringOverrides(pInfo);
                status = FCore_RefreshActionList(pCoreInfo);
                if (A_FAILED(status)) {
                    A_ERR("[%s] Failed refresh action list (status:%d)\n",
                        __FUNCTION__, status);
                }
            }

            pInfo->AdapterAvailable = TRUE;

            Abf_WlanIssueFrontEndConfig(pInfo);

	        Abf_WlanGetCurrentWlanOperatingFreq(pInfo);
            /* sync BT state */
            SyncBTState(pInfo);

#if 0
            /*
             * the call to sync BT state may queue a bunch of actions to
             * the action queue, we want to issues these differently
             */
            ProcessBTActionMessages(pInfo, BTACTION_QUEUE_SYNC_STATE,
                                    ATH_BT_NOOP);
#endif
            break;

        case ATH_ADAPTER_REMOVED:
            A_INFO("BT Filter Core : WLAN removed \n");
            pInfo->AdapterAvailable = FALSE;

            /* flush messages */
            ProcessBTActionMessages(pInfo, BTACTION_QUEUE_FLUSH_ALL,
                                    ATH_BT_NOOP);
            break;
        default:
            break;
    }
}

/* execute the BT action
 * this function is called by the single dispatcher thread
 */
static void
ExecuteBtAction(ATHBT_FILTER_INFO *pInfo, BT_ACTION_MSG *pBtActionMsg)
{
    A_UINT32 size;
    A_UINT32 controlCode;
    A_STATUS status;


    if(pInfo->Flags & ABF_WIFI_CHIP_IS_VENUS) {
        if (pBtActionMsg->ControlAction.Type == BT_CONTROL_ACTION_STATUS) {
            /* this action issues a STATUS OID command */
            controlCode = AR6000_XIOCTL_WMI_SET_BT_OPERATING_STATUS;
            size = sizeof(WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD);
        } else {
            if(pBtActionMsg->IndicationForControlAction == ATH_BT_SCO ||
               pBtActionMsg->IndicationForControlAction == ATH_BT_ESCO)
            {
                controlCode = AR6000_XIOCTL_WMI_SET_BTCOEX_SCO_CONFIG;
                size = sizeof(WMI_SET_BTCOEX_SCO_CONFIG_CMD);
            }
            if(pBtActionMsg->IndicationForControlAction == ATH_BT_A2DP){
                controlCode = AR6000_XIOCTL_WMI_SET_BTCOEX_A2DP_CONFIG;
                size = sizeof(WMI_SET_BTCOEX_A2DP_CONFIG_CMD);
            }
        }
    }else {
        if (pBtActionMsg->ControlAction.Type == BT_CONTROL_ACTION_STATUS) {
            /* this action issues a STATUS OID command */
            controlCode = AR6000_XIOCTL_WMI_SET_BT_STATUS;
        } else {
            /* this action issues a PARAMS OID command */
            controlCode = AR6000_XIOCTL_WMI_SET_BT_PARAMS;
        }
        size  = sizeof(WMI_SET_BT_PARAMS_CMD);
    }
    if (pBtActionMsg->ControlAction.Length > size) {
       A_ERR("Bad control action length : %d \n", pBtActionMsg->ControlAction.Length);
       return;
    }
    do
    {
        A_UCHAR  buf[sizeof(A_UINT32) + size];

        A_MEMZERO(buf, sizeof(buf));
        A_MEMCPY(buf, &controlCode, sizeof(A_UINT32));
        A_MEMCPY((buf + sizeof(A_UINT32)), (void *)pBtActionMsg->ControlAction.Buffer,
                 pBtActionMsg->ControlAction.Length);
        status = Abf_WlanDispatchIO(pInfo, AR6000_IOCTL_EXTENDED, (void *)buf,
                                    pBtActionMsg->ControlAction.Length + sizeof(A_UINT32));

        if (A_FAILED(status)) {
          /* these can actually fail if the device powers down */
            A_ERR("[%s] BT Action issue failed, Ioctl: 0x%x, Len: %d\n",
                   __FUNCTION__, ((int *)buf)[0],
                  pBtActionMsg->ControlAction.Length);
        } else {
            A_DEBUG("BT Action issued to WLAN Adapter, Ioctl: 0x%x, Len: %d\n",
                        ((int *)buf)[0], pBtActionMsg->ControlAction.Length);
            A_DUMP_BUFFER(pBtActionMsg->ControlAction.Buffer,
                           pBtActionMsg->ControlAction.Length,
                           "BT Control Action");
        }
    }while(FALSE);
}

static void
ProcessActionOverride(ATHBT_FILTER_INFO *pInfo,
                      A_CHAR            *pIndicationStr,
                      A_CHAR            *pModifyAction,
                      A_CHAR            *pAction)
{
    int                          i;
    ATHBT_STATE_INDICATION       indication;
    ATHBT_STATE                  state = STATE_MAX;
    ATHBT_MODIFY_CONTROL_ACTION  modifyAction =
                                 ATHBT_MODIFY_CONTROL_ACTION_NOOP;
    char                         charBuffer[MAX_VAL_DATA_LENGTH];

    /*
     * parse the indication string to figure which indication and state
     * to change i.e.  <indication>-ON or <indication>-OFF
     */
    for (indication = 0; indication < ATH_BT_MAX_STATE_INDICATION;
         indication++)
    {
        if (strstr(pIndicationStr, g_IndicationStrings[indication]) != NULL) {
            /* found one */
            if (strstr(pIndicationStr, "-ON") != NULL) {
                state = STATE_ON;    
            }    

            if (strstr(pIndicationStr, "-OFF") != NULL) {
                state = STATE_OFF;
            }  

            if (strstr(pModifyAction, "REPLACE") != NULL) {
                modifyAction = ATHBT_MODIFY_CONTROL_ACTION_REPLACE;
            }     

            if (strstr(pModifyAction, "APPEND") != NULL) {
                modifyAction = ATHBT_MODIFY_CONTROL_ACTION_APPEND;
            } 

            break;    
        }
    }    

    if ((indication == ATH_BT_MAX_STATE_INDICATION) || 
        (state == STATE_MAX)               || 
        (modifyAction == ATHBT_MODIFY_CONTROL_ACTION_NOOP)) 
    {
        return;    
    }

    A_DEBUG("Found Action override : %s (%s) (%s)\n", 
            pIndicationStr, pModifyAction, pAction);

    A_MEMZERO(charBuffer, sizeof(charBuffer));

    for (i = 0; (i < (int)strlen(pAction)) && (i < (MAX_VAL_DATA_LENGTH - 1)); 
         i++)
    {
        charBuffer[i] = (char)pAction[i];    
    }

    FCore_ModifyControlActionString(&pInfo->FilterCore, 
                                    indication,
                                    state,
                                    charBuffer,
                                    i,
                                    modifyAction);
}

static void 
GetActionStringOverrides(ATHBT_FILTER_INFO *pInfo)
{
    A_CHAR *ptr, *indication, *modify, *action;
    A_CHAR *string = (A_CHAR *)A_MALLOC(MAX_VAL_DATA_LENGTH);

    if (!(gConfigFile)) return;

    fgets(string, MAX_VAL_DATA_LENGTH, gConfigFile);
    while (!(feof(gConfigFile))) {
        ptr = string;
        indication = strsep(&string, ":");
        modify = strsep(&string, ":");
        action = string;
        ProcessActionOverride(pInfo, indication, modify, action);
        string = ptr;
        fgets(string, MAX_VAL_DATA_LENGTH, gConfigFile);
    }

    A_FREE(string);
}
