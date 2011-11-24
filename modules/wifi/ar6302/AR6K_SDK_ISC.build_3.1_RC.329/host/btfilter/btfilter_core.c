//------------------------------------------------------------------------------
// <copyright file="btfilter_core.c" company="Atheros">
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
// Bluetooth filter core action tables and action lookup
//
// Author(s): ="Atheros"
//==============================================================================

#include <stdlib.h>
#include <string.h>
#include "athdefs.h"
#define  ATH_MODULE_NAME btfilt
#include "a_debug.h"
#include "a_types.h"
#include "a_osapi.h"
#include "wmi.h"
#include "athbtfilter.h"
#include "btfilter_core.h"

#ifdef DEBUG

ATH_DEBUG_INSTANTIATE_MODULE_VAR(btfilt,
                                 "btfilt",
                                 "BT Filter Core",
                                 ATH_DEBUG_MASK_DEFAULTS,
                                 0,
                                 NULL);
                                 
#endif

#define IS_STATE_FILTER_IGNORED(pCore,indication) ((pCore)->StateFilterIgnore & (1 << (indication)))

static A_BOOL IsACLSignaling(BT_FILTER_CORE_INFO *pCore,
                             A_UINT8             *pBuffer, 
                             int                 Length);

static A_BOOL IsA2DPConnection(BT_FILTER_CORE_INFO *pCore,
                               A_UINT8             *pBuffer, 
                               int                 Length);

static void ParseACLPacket(BT_FILTER_CORE_INFO *pCore,
                           A_UINT8             *pBuffer, 
                           int                 Length);

static void ProcessA2DPconnection(BT_FILTER_CORE_INFO *pCore,
                                  L2CAP_CONTROL       *pL2Ctrl,
                                  A_BOOL              IsConnect);

static ATHBT_STATE_INDICATION A2DP_StateIndication(BT_FILTER_CORE_INFO *pCore,
                                                   A_UINT8             *pBuffer, 
                                                   int                 Length,
                                                   ATHBT_STATE         *pNewState);

static ATHBT_STATE_INDICATION ProcessA2DPCMD(BT_FILTER_CORE_INFO *pCore,
                                             A_UINT8             CMDID,
                                             ATHBT_STATE         *pNewState,
                                             A_BOOL		         IsACPT);

ATHBT_STATE_INDICATION FCore_FilterBTCommand(BT_FILTER_CORE_INFO *pCore, A_UINT8 *pBuffer, int Length, ATHBT_STATE *pNewState)
{
    ATHBT_STATE_INDICATION indication = ATH_BT_NOOP;
    ATHBT_STATE            state = STATE_OFF;
    A_UINT16               command;


    if (!IS_LINK_CONTROL_CMD(pBuffer)) {
            /* we only filter link control commands */
        return indication;
    }

    command = HCI_GET_OP_CODE(pBuffer);

    switch (command) {
        case HCI_INQUIRY :
            if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_INQUIRY)) {
                break;
            }
            indication = ATH_BT_INQUIRY;
            state = STATE_ON;
            break;

        case HCI_INQUIRY_CANCEL :
            if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_INQUIRY)) {
                break;
            }
            indication = ATH_BT_INQUIRY;
            state = STATE_OFF;
            break;

        case HCI_CREATE_CONNECTION :
            if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_CONNECT)) {
                break;
            }
            indication = ATH_BT_CONNECT;
            state = STATE_ON;
            break;

        case HCI_ACCEPT_CONN_REQ :
            if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_CONNECT)) {
                break;
            }
            indication = ATH_BT_CONNECT;
            state = STATE_ON;
            break;

        case HCI_ADD_SCO :
            /* we do not need to handle this command, we can pick up the connection complete event */
        case HCI_DISCONNECT :
            /* we don't need to handle this command as we can pick up the disconnect event */
        case HCI_PER_INQUIRY :
        case HCI_PER_INQUIRY_CANCEL :
            /* we do not handle these currently */
        default :
            break;
    }

    if (indication != ATH_BT_NOOP) {
            /* the HCI filter simply determines the "precise" state and calls the shared function */
        return FCore_FilterIndicatePreciseState(pCore, indication, state, pNewState);
    }

    return ATH_BT_NOOP;
}


void FilterConnectDisconnectComplete(BT_FILTER_CORE_INFO    *pCore,
                                     A_UINT8                LinkType,
                                     A_UINT16               ConnectionHandle,
                                     A_BOOL                 Connected,
                                     ATHBT_STATE_INDICATION *pIndication,
                                     ATHBT_STATE            *pState)
{


    do {
        if (Connected) {

            if (LinkType == BT_LINK_TYPE_SCO) {

                    /* SCO connection */
                if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_SCO)) {
                    break;
                }

                pCore->FilterState.SCO_ConnectionHandle = ConnectionHandle;
                *pIndication = ATH_BT_SCO;
                *pState = STATE_ON;

            } else if (LinkType == BT_LINK_TYPE_ACL) {

                    /* ACL connection */
                if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_ACL)) {
                    break;
                }

                /**** TODO , currently we do not keep track of ACL connections
                  at the moment we rely on upper layer stack to detect specific
                  traffic types like A2DP. Since there can be multiple ACL connections
                  it is not practical to provide a single action for all ACL connections.
                  This is just a place holder in case a customer does require specific
                  actions to take for an ACL connection *****/

            } else if (LinkType == BT_LINK_TYPE_ESCO) {

                    /* eSCO connection */
                if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_ESCO)) {
                    break;
                }

                pCore->FilterState.eSCO_ConnectionHandle = ConnectionHandle;
                *pIndication = ATH_BT_ESCO;
                *pState = STATE_ON;
            }

            break;
        }

        /* if we get here, we are handling the disconnect case */

        if (ConnectionHandle == pCore->FilterState.ACL_ConnectionHandle) {

            /*** TODO, we currently do not keep track of these, see note above */

        } else if (ConnectionHandle == pCore->FilterState.SCO_ConnectionHandle) {

            pCore->FilterState.SCO_ConnectionHandle = INVALID_BT_CONN_HANDLE;
            *pIndication = ATH_BT_SCO;
            *pState = STATE_OFF;

        } else if (ConnectionHandle == pCore->FilterState.eSCO_ConnectionHandle) {

            pCore->FilterState.eSCO_ConnectionHandle = INVALID_BT_CONN_HANDLE;
            *pIndication = ATH_BT_ESCO;
            *pState = STATE_OFF;
        }

    } while (FALSE);

}


ATHBT_STATE_INDICATION FCore_FilterBTEvent(BT_FILTER_CORE_INFO *pCore, A_UINT8 *pBuffer, int Length, ATHBT_STATE *pNewState)
{
    ATHBT_STATE_INDICATION indication = ATH_BT_NOOP;
    ATHBT_STATE            state = STATE_OFF;

    switch HCI_GET_EVENT_CODE(pBuffer) {
        case HCI_EVT_INQUIRY_COMPLETE :
            if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_INQUIRY)) {
                break;
            }
            indication = ATH_BT_INQUIRY;
            state = STATE_OFF;
            break;

        case HCI_EVT_CONNECT_COMPLETE :
                         
            if (BT_CONN_EVENT_STATUS_SUCCESS(pBuffer)) {
                FilterConnectDisconnectComplete(pCore,
                                                GET_BT_CONN_LINK_TYPE(pBuffer),
                                                GET_BT_CONN_HANDLE(pBuffer),
                                                TRUE, /* connected */
                                                &indication,
                                                &state);
            }
                                       
            break;

        case HCI_EVT_CONNECT_REQUEST :
            if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_CONNECT)) {
                break;
            }

            indication = ATH_BT_CONNECT;
            state = STATE_ON;
            break;

        case HCI_EVT_SCO_CONNECT_COMPLETE:

            if (BT_CONN_EVENT_STATUS_SUCCESS(pBuffer)) {
                FilterConnectDisconnectComplete(pCore,
                                                GET_BT_CONN_LINK_TYPE(pBuffer),
                                                GET_BT_CONN_HANDLE(pBuffer),
                                                TRUE, /* connected */
                                                &indication,
                                                &state);
            }

            break;

        case HCI_EVT_DISCONNECT :

            FilterConnectDisconnectComplete(pCore,
                                            GET_BT_CONN_LINK_TYPE(pBuffer),
                                            GET_BT_CONN_HANDLE(pBuffer),
                                            FALSE, /* disconnected */
                                            &indication,
                                            &state);
            break;

        case HCI_EVT_REMOTE_NAME_REQ :

            /* TODO */
            break;

        case HCI_EVT_ROLE_CHANGE :
            /* TODO */
            break;

        default:
            break;
    }

    if (indication != ATH_BT_NOOP) {
            /* the HCI filter simply determines the "precise" state and calls the shared function */
        return FCore_FilterIndicatePreciseState(pCore, indication, state, pNewState);
    }

    return ATH_BT_NOOP;
}

/* a precise state can be indicated by the porting layer or indicated by the HCI command/event filtering.
 * The caller needs to protect this call with a lock */
ATHBT_STATE_INDICATION FCore_FilterIndicatePreciseState(BT_FILTER_CORE_INFO    *pCore,
                                                        ATHBT_STATE_INDICATION  Indication,
                                                        ATHBT_STATE             StateOn,
                                                        ATHBT_STATE            *pNewState)
{
    A_UINT32 bitmap = (1 << Indication);
    A_UINT32 oldBitMap;

    oldBitMap = pCore->FilterState.StateBitMap;

    if (StateOn == STATE_ON) {
        pCore->FilterState.StateBitMap |= bitmap;
    } else {
        pCore->FilterState.StateBitMap &= ~bitmap;
    }

    if (oldBitMap ^ pCore->FilterState.StateBitMap) {
        *pNewState = StateOn;
        return Indication;
    } else {
            /* no state change */
        return ATH_BT_NOOP;
    }
}

A_UINT32 FCore_GetCurrentBTStateBitMap(BT_FILTER_CORE_INFO    *pCore)
{
    return pCore->FilterState.StateBitMap;
}

ATHBT_STATE_INDICATION FCore_FilterACLDataIn(BT_FILTER_CORE_INFO *pCore,
                                             A_UINT8             *pBuffer, 
                                             int                 Length, 
                                             ATHBT_STATE         *pNewState)
{
    ATHBT_STATE_INDICATION  stateIndication = ATH_BT_NOOP;
   
    *pNewState  = STATE_OFF;
    
    do {
        
        if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_A2DP)) {
            /* we only filter for A2DP stream state if the OS-ported layer
             * cannot do it */
            break;
        }
        
        /***** the following code filters for A2DP stream state ONLY *****/
        
            /* filter ACL packets for application specific traffic */
        if (IsACLSignaling(pCore,pBuffer,Length)) {
        	ParseACLPacket(pCore,pBuffer,Length);
        } else if ( IsA2DPConnection(pCore,pBuffer,Length)) {
        	stateIndication = A2DP_StateIndication(pCore,pBuffer,Length,pNewState);
        }   
        
    } while (FALSE);	
  
    return stateIndication;    
}

ATHBT_STATE_INDICATION FCore_FilterACLDataOut(BT_FILTER_CORE_INFO *pCore, 
                                              A_UINT8             *pBuffer, 
                                              int                 Length, 
                                              ATHBT_STATE         *pNewState)
{ 
    ATHBT_STATE_INDICATION  stateIndication = ATH_BT_NOOP;
      
    *pNewState  = STATE_OFF;
        
    do {
        
        if (IS_STATE_FILTER_IGNORED(pCore, ATH_BT_A2DP)) {
            /* we only filter for A2DP stream state if the OS-ported layer
             * cannot do it */
            break;
        }
        
        /***** the following code filters for A2DP stream state ONLY *****/
             
        if (IsACLSignaling(pCore,pBuffer,Length)) {
        	ParseACLPacket(pCore,pBuffer,Length);
        } else if (IsA2DPConnection(pCore,pBuffer,Length)) {
        	stateIndication = A2DP_StateIndication(pCore,pBuffer,Length,pNewState);
        }   
    
    } while (FALSE);	
    
    return stateIndication;
}

static INLINE A_BOOL ParseCAPheader(L2CAP_HEADER *pL2hdr,
                                    A_UINT8      *pBuffer,
                                    int          Length)
{
    A_UCHAR    *pTemp = pBuffer + sizeof(ACL_HEADER);
    
    if (Length < (sizeof(ACL_HEADER)+sizeof(L2CAP_HEADER))) {
        return FALSE;
    }

    pL2hdr->Length = GETUINT16(pTemp);
    pTemp += sizeof(A_UINT16);
    pL2hdr->CID = GETUINT16(pTemp);
    pTemp += sizeof(A_UINT16);
    return TRUE;
}


static INLINE A_BOOL ParseCAPCtrl(L2CAP_CONTROL *pL2Ctrl,
                                  A_UINT8       *pBuffer,
                                  int           Length)
{
    A_UCHAR     *pTemp = pBuffer + sizeof(ACL_HEADER);
      
    if (Length < (sizeof(ACL_HEADER)+sizeof(L2CAP_HEADER)+sizeof(L2CAP_CONTROL)-6)) {
        return FALSE;
    }
    
    pTemp +=sizeof(L2CAP_HEADER);

    pL2Ctrl->CODE = *pTemp++;
    pL2Ctrl->ID = *pTemp++;
    pL2Ctrl->Length = GETUINT16(pTemp) ;
    pTemp += sizeof(A_UINT16);
    pL2Ctrl->PSM = pL2Ctrl->DESTINATION_CID=GETUINT16(pTemp);
    pTemp += sizeof(A_UINT16);
    pL2Ctrl->SOURCE_CID = GETUINT16(pTemp);
    pTemp += sizeof(A_UINT16);
    
    if (Length >= (sizeof(ACL_HEADER)+sizeof(L2CAP_HEADER)+sizeof(L2CAP_CONTROL)-sizeof(A_UINT16))) { 
        pL2Ctrl->RESULT = GETUINT16(pTemp);
        pTemp += sizeof(A_UINT16);
        pL2Ctrl->STATUS = GETUINT16(pTemp);
        pTemp += sizeof(A_UINT16);
    }
    
    return TRUE;
} 

static A_BOOL IsACLSignaling(BT_FILTER_CORE_INFO *pCore,
                             A_UINT8             *pBuffer, 
                             int                 Length)
{
    L2CAP_HEADER    L2hdr;
    
    A_MEMZERO(&L2hdr,sizeof(L2CAP_HEADER));
    
    if (!ParseCAPheader(&L2hdr,pBuffer,Length)) {
         return FALSE;
    }
         
    if (L2hdr.CID == SIGNALING) {
        return TRUE;
    }   
    
    return FALSE;   
}

static A_BOOL IsA2DPConnection(BT_FILTER_CORE_INFO *pCore,
                               A_UINT8             *pBuffer, 
                               int                 Length)
{
    L2CAP_HEADER    L2hdr;
    
    A_MEMZERO(&L2hdr,sizeof(L2CAP_HEADER));
    
    if (!ParseCAPheader(&L2hdr,pBuffer,Length)) {
        /* not L2CAP header */        
        return FALSE;
    }
     
    if (pCore->FilterState.AVDTP_state==STATE_CONNECTED && 
        ((L2hdr.CID==pCore->FilterState.AVDTP_DESTINATION_CID) ||
         (L2hdr.CID==pCore->FilterState.AVDTP_SOURCE_CID))) {
        //ACL normal packet
        return TRUE;                   
    }
    
    return FALSE;  
}   

static void ParseACLPacket(BT_FILTER_CORE_INFO *pCore,
                           A_UINT8             *pBuffer, 
                           int                 Length)
{
    L2CAP_CONTROL   L2ctrl;
    
    A_MEMZERO(&L2ctrl,sizeof(L2CAP_CONTROL));
    
    if (!ParseCAPCtrl(&L2ctrl,pBuffer,Length)) {
        return; 
    }      
    
    switch( L2ctrl.CODE) {
        case CONNECT_REQ:
        case CONNECT_RSP:
            ProcessA2DPconnection(pCore,&L2ctrl,TRUE);
            break;        
        case DISCONNECT_REQ:
        case DISCONNECT_RSP:
            ProcessA2DPconnection(pCore,&L2ctrl,FALSE);
            break;  
    }   
}

static void ProcessA2DPconnection(BT_FILTER_CORE_INFO *pCore,
                                  PL2CAP_CONTROL      pL2Ctrl,
                                  A_BOOL              IsConnect)
{
    
    if (IsConnect) {
        if (pL2Ctrl->CODE==CONNECT_REQ) { 
            if (pCore->FilterState.AVDTP_state==STATE_DISCONNECT) {
                pCore->FilterState.AVDTP_state = pL2Ctrl->PSM == 
                                    A2DP_TYPE ? STATE_CONNECTING:STATE_DISCONNECT;                
            } else if (pCore->FilterState.RFCOMM_state==STATE_DISCONNECT) {
                pCore->FilterState.RFCOMM_state= pL2Ctrl->PSM == 
                                    RFCOMM_TYPE ? STATE_CONNECTING:STATE_DISCONNECT;
            }
        } else if (pL2Ctrl->CODE==CONNECT_RSP) {
            switch (pL2Ctrl->RESULT) {
                case STATE_SUCCESS:
                    if (pCore->FilterState.AVDTP_state == STATE_CONNECTING) {
                        pCore->FilterState.AVDTP_SOURCE_CID=NULL_ID;
                        pCore->FilterState.AVDTP_DESTINATION_CID=NULL_ID;
                        if ( pL2Ctrl->STATUS==STATE_SUCCESS) {
                            pCore->FilterState.AVDTP_state= STATE_CONNECTED;
                            pCore->FilterState.AVDTP_SOURCE_CID= pL2Ctrl->SOURCE_CID;  
                            pCore->FilterState.AVDTP_DESTINATION_CID= pL2Ctrl->DESTINATION_CID; 
                        }
                    } else if (pCore->FilterState.RFCOMM_state == STATE_CONNECTING) {
                        pCore->FilterState.RFCOMM_SOURCE_CID=NULL_ID;
                        pCore->FilterState.RFCOMM_DESTINATION_CID=NULL_ID;
                        if ( pL2Ctrl->STATUS==STATE_SUCCESS) {
                            pCore->FilterState.RFCOMM_state= STATE_CONNECTED;
                            pCore->FilterState.RFCOMM_SOURCE_CID= pL2Ctrl->SOURCE_CID; 
                            pCore->FilterState.RFCOMM_DESTINATION_CID= pL2Ctrl->DESTINATION_CID;
                        }
                    } 
                    break;
                case STATE_PENDING:
                    break;
                default: 
                    pCore->FilterState.RFCOMM_state=STATE_DISCONNECT;   
                    pCore->FilterState.RFCOMM_SOURCE_CID=NULL_ID;
                    pCore->FilterState.RFCOMM_DESTINATION_CID=NULL_ID;
                    pCore->FilterState.AVDTP_state=STATE_DISCONNECT;    
                    pCore->FilterState.AVDTP_SOURCE_CID = NULL_ID;
                    pCore->FilterState.AVDTP_DESTINATION_CID=NULL_ID;
                    break;
            }
       }
    } else {
        pCore->FilterState.AVDTP_state=STATE_DISCONNECT;    
        pCore->FilterState.RFCOMM_state=STATE_DISCONNECT;   
    }
    
}                        


static ATHBT_STATE_INDICATION A2DP_StateIndication(BT_FILTER_CORE_INFO *pCore,
                                                   A_UINT8             *pBuffer, 
                                                   int                 Length,
                                                   ATHBT_STATE         *pNewState)                                       

{
    AVDTP_HEADER            Avdtphdr;   
    ATHBT_STATE_INDICATION  stateIndication = ATH_BT_NOOP;
    A_UCHAR                 *pTemp = pBuffer+sizeof(ACL_HEADER)+sizeof(L2CAP_HEADER);
    
    Avdtphdr.MESSAGE_TYPE= (*pTemp++) & 0x03; // get Message Type
    Avdtphdr.CMD_ID = (*pTemp++) & 0x3F;     //Get command code
    
    switch (Avdtphdr.MESSAGE_TYPE) {
        case TYPE_ACPT:
            stateIndication = ProcessA2DPCMD(pCore,Avdtphdr.CMD_ID,pNewState,TRUE);
            break;
        case TYPE_REJ: 
            stateIndication = ProcessA2DPCMD(pCore,Avdtphdr.CMD_ID,pNewState,FALSE);
            break;       
        default:
            break;    
    }
    
    return stateIndication; 
}                                       

static ATHBT_STATE_INDICATION ProcessA2DPCMD(BT_FILTER_CORE_INFO *pCore,
                                             A_UINT8             CMDID,
                                             ATHBT_STATE         *pNewState,
                                             A_BOOL              IsACPT)

{
    ATHBT_STATE_INDICATION stateIndication = ATH_BT_NOOP;
    
    switch(CMDID) {        
        case AVDTP_START:
            stateIndication = ATH_BT_A2DP;
            *pNewState = IsACPT ? STATE_ON:STATE_OFF;
            break;
        case AVDTP_OPEN:
                // don't assign any state in here.
            break;         
        case AVDTP_SUSPEND:
            stateIndication = ATH_BT_A2DP;
            *pNewState = STATE_OFF;       
            break;
        case AVDTP_CLOSE:     
            stateIndication = ATH_BT_A2DP;
            *pNewState = STATE_OFF;       
            break;  
        default:
            break;
    }    
   
    return stateIndication;   
}                  

                    
