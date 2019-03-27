/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE

********************************************************************************/
/*******************************************************************************/
/** \file
 *
 *
 * This file contains Management IOCTL APIs
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/api/ostiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiglobal.h>

#ifdef FDS_SM
#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>
#endif

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/sas/common/tdtypes.h>
#include <dev/pms/freebsd/driver/common/osstring.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdutil.h>
#include <dev/pms/RefTisa/sallsdk/spc/mpidebug.h>

#ifdef INITIATOR_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdtypes.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itddefs.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdglobl.h>
#endif

#ifdef TARGET_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdglobl.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdxchg.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdtypes.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/common/tdsatypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdproto.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdioctl.h>

#include <dev/pms/RefTisa/sallsdk/spc/sadefs.h>
#include <dev/pms/RefTisa/sallsdk/spc/spcdefs.h>
#include <dev/pms/RefTisa/sallsdk/spc/mpi.h>
#include <dev/pms/RefTisa/sallsdk/spc/sallist.h>
#include <dev/pms/RefTisa/sallsdk/spc/satypes.h>


#define agFieldOffset(baseType,fieldName) \
            /*lint -e545 */ \
            ((bit32)((bitptr)(&(((baseType *)0)->fieldName)))) \

#ifdef SA_LL_API_TEST
osGLOBAL bit32 tdLlApiTestIoctl(tiRoot_t *tiRoot,
                                tiIOCTLPayload_t *agIOCTLPayload,
                                void *agParam1,
                                void *agParam2,
                                void *agParam3);
#endif /* SA_LL_API_TEST */


extern bit32 volatile sgpioResponseSet;

#ifdef SPC_ENABLE_PROFILE
/*****************************************************************************
*
* tdipFWProfileIoctl
*
* Purpose:  This routine is called to process the FW Profile IOCTL function.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32 tdipFWProfileIoctl(
                        tiRoot_t            *tiRoot,
                        tiIOCTLPayload_t    *agIOCTLPayload,
                        void                *agParam1,
                        void                *agParam2,
                        void                *agParam3
                        )
{

  bit32                status = IOCTL_CALL_SUCCESS;
  bit32                bufAddrUpper = 0;
  bit32                bufAddrLower = 0;
  tdFWProfile_t        *fwProfile;

  void                 *osMemHandle = agNULL;
  void                 *buffer = agNULL;
  agsaFwProfile_t     fwProfileInfo = {0};

  tdsaRoot_t           *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t           *agRoot = &tdsaAllShared->agRootInt;

  fwProfile = (tdFWProfile_t *)&agIOCTLPayload->FunctionSpecificArea[0];


  fwProfileInfo.processor = fwProfile->processor;
  fwProfileInfo.cmd = fwProfile->cmd;
  fwProfileInfo.len = fwProfile->len;
  fwProfileInfo.tcid = fwProfile->tcid;
  if(fwProfile->cmd == START_CODE_PROFILE)
  {
    fwProfileInfo.codeStartAdd = fwProfile->codeStartAdd;
      fwProfileInfo.codeEndAdd = fwProfile->codeEndAdd;
  }
  if((fwProfile->cmd == STOP_TIMER_PROFILE) || (fwProfile->cmd == STOP_CODE_PROFILE))
  {
    if(fwProfile->len != 0)
    {
      if(ostiAllocMemory( tiRoot,
              &osMemHandle,
              (void **)&buffer,
              &bufAddrUpper,
              &bufAddrLower,
              8,
              fwProfile->len,
              agFALSE))
        {
          return IOCTL_CALL_FAIL;
        }
      osti_memset((void *)buffer, 0, fwProfile->len);
    }
    fwProfileInfo.agSgl.sgLower = bufAddrLower;
    fwProfileInfo.agSgl.sgUpper = bufAddrUpper;
    fwProfileInfo.agSgl.len = fwProfile->len;
    fwProfileInfo.agSgl.extReserved = 0;
    tdsaAllShared->tdFWProfileEx.buffer = osMemHandle;
    tdsaAllShared->tdFWProfileEx.virtAddr = buffer;
    tdsaAllShared->tdFWProfileEx.len = fwProfile->len;
  }
  tdsaAllShared->tdFWProfileEx.tdFWProfile = fwProfile;
  tdsaAllShared->tdFWProfileEx.param1 = agParam1;
  tdsaAllShared->tdFWProfileEx.param2 = agParam2;
  tdsaAllShared->tdFWProfileEx.payload = agIOCTLPayload;
  tdsaAllShared->tdFWProfileEx.inProgress = 1;
  status = saFwProfile(agRoot,
            agNULL,
            0,
            &fwProfileInfo
            );
  if(status)
  {
    if((fwProfile->cmd == STOP_TIMER_PROFILE) || (fwProfile->cmd == STOP_CODE_PROFILE))
      ostiFreeMemory(tiRoot, osMemHandle, fwProfile->len);
    status = IOCTL_CALL_FAIL;
  }
  else
    status = IOCTL_CALL_PENDING;
  return status;
}


#endif

/*****************************************************************************
*
* tdipFWControlIoctl
*
* Purpose:  This routine is called to process the FW control IOCTL function.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32 tdipFWControlIoctl(
  tiRoot_t            *tiRoot,
  tiIOCTLPayload_t    *agIOCTLPayload,
  void                *agParam1,
  void                *agParam2,
  void                *agParam3
  ) {

  bit32               status = IOCTL_CALL_PENDING;
  bit32               bufAddrUpper = 0;
  bit32               bufAddrLower = 0;
  tdFWControl_t      *fwControl;
  void               *osMemHandle = agNULL;
  void               *buffer = agNULL;
  agsaUpdateFwFlash_t flashUpdateInfo;
  tdsaRoot_t         *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t      *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t         *agRoot = &tdsaAllShared->agRootInt;

  if( agIOCTLPayload->Length <
      ( agFieldOffset(tiIOCTLPayload_t, FunctionSpecificArea) +
        sizeof(tdFWControl_t) ) )  {
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
    status = IOCTL_CALL_FAIL;
    return status;
  }
  fwControl = (tdFWControl_t *)&agIOCTLPayload->FunctionSpecificArea[0];

  if(fwControl->len != 0)
  {
    if(ostiAllocMemory( tiRoot,
                        &osMemHandle,
                        (void **)&buffer,
                        &bufAddrUpper,
                        &bufAddrLower,
                        8,
                        fwControl->len,
                        agFALSE) )
      return IOCTL_CALL_FAIL;
  }
  osti_memset( (void *)buffer, 0, fwControl->len );
  osti_memcpy( (void *)buffer,
               fwControl->buffer,
               fwControl->len );
  flashUpdateInfo.agSgl.sgLower = bufAddrLower;
  flashUpdateInfo.agSgl.sgUpper = bufAddrUpper;
  flashUpdateInfo.agSgl.len     = fwControl->len;
  flashUpdateInfo.agSgl.extReserved  = 0;
  flashUpdateInfo.currentImageOffset = fwControl->offset;
  flashUpdateInfo.currentImageLen    = fwControl->len;
  flashUpdateInfo.totalImageLen      = fwControl->size;
  switch (agIOCTLPayload->MinorFunction)
  {
    case IOCTL_MN_FW_DOWNLOAD_DATA:
    {
      TI_DBG6(("tdipFWControlIoctl: calling saFwFlashUpdate\n"));
      tdsaAllShared->tdFWControlEx.tdFWControl = fwControl;
      tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
      tdsaAllShared->tdFWControlEx.param1 = agParam1;
      tdsaAllShared->tdFWControlEx.param2 = agParam2;
      tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
      tdsaAllShared->tdFWControlEx.inProgress = 1;
      status = saFwFlashUpdate( agRoot,
                                agNULL,
                                0,
                                &flashUpdateInfo );
      if(status) {
        status = IOCTL_CALL_FAIL;
        fwControl->retcode = IOCTL_CALL_TIMEOUT;
      }
      else {
        status = IOCTL_CALL_PENDING;
      }
      break;
    }
    default:
      status = IOCTL_CALL_INVALID_CODE;
      TI_DBG1( ("tdipFWControlIoctl: ERROR: Wrong IOCTL code %d\n",
                agIOCTLPayload->MinorFunction) );
      ostiFreeMemory(tiRoot, osMemHandle, fwControl->len);
      return status;
  } /* end IOCTL switch */
  return status;
} /* tdipFWControlIoctl */


/*****************************************************************************
*
* tiCOMMgntIOCTL
*
* Purpose:  This routine is a TISA API for processing the PMC specific
*           IOCTL function.
*
*           Each IOCTL function is identified by the IOCTL header
*           specified in the data payload as the following:
*           Field                 Description
*           -----                 -----------
*           Signature             PMC IOCTL signature.
*                                 #define PMC_IOCTL_SIGNATURE   0x1234
*           MajorFunction         Major function number.
*           MinorFunction         Minor function number.
*           Length                Length of this structure in bytes.
*           Status                Return status for this IOCTL function.
*           FunctionSpecificArea  Variable length function specific area.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*   IOCTL_CALL_INVALID_DEVICE Invalid target or destination device.
*
* Note:
*  Used ostiAllocMemory() OS layer callback function to allocate memory
*  for DMA operaion. Then use ostiFreeMemory() to deallocate the memory.
*
*****************************************************************************/
osGLOBAL bit32
tiCOMMgntIOCTL(
               tiRoot_t            *tiRoot,
               tiIOCTLPayload_t    *agIOCTLPayload,
               void                *agParam1,
               void                *agParam2,
               void                *agParam3
               )
{
  bit32                     status = IOCTL_CALL_INVALID_CODE;
  tdsaRoot_t                *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                *agRoot = &(tdsaAllShared->agRootNonInt);
  bit32                     EventLogLength = 0;
  bit32                     EventLogOption;
  bit32                     ReadLength = 0;
  bit32                     Offset = 0;
  bit32                     RequestLength = 0;  /* user request on how much data to pass to application */
  agsaContext_t		    *agContext = NULL;
  bit8                      *loc = NULL;

  TI_DBG3(("tiCOMMgntIOCTL: start\n"));

  TI_DBG3(("tiCOMMgntIOCTL: tiRoot %p agIOCTLPayload %p agParam1 %p agParam2 %p agParam3 %p\n",
                            tiRoot,agIOCTLPayload,agParam1,agParam2,agParam3 ));

  TI_DBG3(("tiCOMMgntIOCTL: Signature %X\ntiCOMMgntIOCTL: MajorFunction 0x%X\ntiCOMMgntIOCTL: MinorFunction 0x%X\ntiCOMMgntIOCTL: Length 0x%X\ntiCOMMgntIOCTL: Status 0x%X\ntiCOMMgntIOCTL: Reserved 0x%X\ntiCOMMgntIOCTL: FunctionSpecificArea 0x%X\n",
                           agIOCTLPayload->Signature,
                           agIOCTLPayload->MajorFunction,
                           agIOCTLPayload->MinorFunction,
                           agIOCTLPayload->Length,
                           agIOCTLPayload->Status,
                           agIOCTLPayload->Reserved,
                           agIOCTLPayload->FunctionSpecificArea[0] ));

  /* PMC IOCTL signatures matched ? */
  if(agIOCTLPayload->Signature != PMC_IOCTL_SIGNATURE)
  {
    TI_DBG1(("tiCOMMgntIOCTL:agIOCTLPayload->Signature %x IOCTL_CALL_INVALID_CODE\n",agIOCTLPayload->Signature ));
    status = IOCTL_CALL_INVALID_CODE;
    return (status);
  }

  switch (agIOCTLPayload->MajorFunction)
  {
//TODO: make the card identification more robust. For now - just to keep going with FW download
#ifdef IOCTL_INTERRUPT_TIME_CONFIG
  case IOCTL_MJ_CARD_PARAMETER:
  {
    switch( agIOCTLPayload->MinorFunction )
    {
      case  IOCTL_MN_CARD_GET_INTERRUPT_CONFIG:
      {
          agsaInterruptConfigPage_t *pInterruptConfig = (agsaInterruptConfigPage_t *)&agIOCTLPayload->FunctionSpecificArea[0];
          status = saGetControllerConfig(agRoot,
                                0,
                                AGSA_INTERRUPT_CONFIGURATION_PAGE,
                                pInterruptConfig->vectorMask0,
                                pInterruptConfig->vectorMask1,
                                agParam2);
          if(status == AGSA_RC_SUCCESS) {
              status = IOCTL_CALL_PENDING;
              agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
          } else {
              agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
          }
          break;
      }
      case  IOCTL_MN_CARD_GET_TIMER_CONFIG:
          status = saGetControllerConfig(agRoot, 0, AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE, 0, 0, agParam2);
          if(status == AGSA_RC_SUCCESS) {
              status = IOCTL_CALL_PENDING;
              agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
          } else {
              agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
          }
          break;
    }
    break;
  }
#endif /* IOCTL_INTERRUPT_TIME_CONFIG */
  case IOCTL_MJ_INI_DRIVER_IDENTIFY:
  {
    status=IOCTL_CALL_SUCCESS;
    break;
  }
  case IOCTL_MJ_GET_DEVICE_LUN:
		status = tdsaGetNumOfLUNIOCTL(tiRoot,agIOCTLPayload, agParam1, agParam2, agParam3);	
    	if(status == IOCTL_CALL_SUCCESS)
        {
    	  status = IOCTL_CALL_PENDING;
    	}
   break;
case IOCTL_MJ_SMP_REQUEST:
	status = tdsaSendSMPIoctl(tiRoot, agIOCTLPayload,
             	agParam1,agParam2,agParam3);
	break;

  case IOCTL_MJ_FW_CONTROL:
  {
    //ostiIOCTLClearSignal (tiRoot, &agParam1, &agParam2, &agParam3);
    status = tdipFWControlIoctl( tiRoot, agIOCTLPayload,
                                   agParam1, agParam2, agParam3);

    break;
  }
//#ifdef EVENT_LOG_INFO_TESTING
  /* Reserved field in tiIOCTLPayload_t is used as offset */
  case IOCTL_MJ_GET_EVENT_LOG1:
  {
    switch (agIOCTLPayload->MinorFunction)
    {
      case IOCTL_MN_FW_GET_TRACE_BUFFER:
      {
        agsaControllerEventLog_t EventLog;
        saGetControllerEventLogInfo(agRoot, &EventLog);
        TI_DBG6(("tiCOMMgntIOCTL: IOCTL_MJ_GET_EVENT_LOG1 Length %d\n", agIOCTLPayload->Length));
        RequestLength = agIOCTLPayload->Length;
        Offset = agIOCTLPayload->Reserved;
        EventLogLength = EventLog.eventLog1.totalLength;
        EventLogOption = EventLog.eventLog1Option;
        if (EventLogLength <= Offset)
        {
          TI_DBG1(("tiCOMMgntIOCTL: 1 out of range Requestlength %d Offset %d event log length %d\n", RequestLength, Offset, EventLogLength));
          // out of range
          agIOCTLPayload->Status = IOCTL_ERR_STATUS_NO_MORE_DATA;
          agIOCTLPayload->Length = 0;
          if(EventLogOption == 0)
          {
            agIOCTLPayload->Status = IOCTL_ERR_FW_EVENTLOG_DISABLED;
          }
          status=IOCTL_CALL_SUCCESS;
          return status;
         }
        ReadLength = MIN(EventLogLength - Offset, RequestLength);
        loc = (bit8 *)EventLog.eventLog1.virtPtr + Offset;
        osti_memcpy(&(agIOCTLPayload->FunctionSpecificArea), loc, ReadLength);
      //   tdhexdump("IOCTL_MJ_GET_EVENT_LOG1 first 32bytes", (bit8 *)&(agIOCTLPayload->FunctionSpecificArea), 32);
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
        agIOCTLPayload->Length = (bit16)ReadLength;
        status=IOCTL_CALL_SUCCESS;
        break;
     }
     case IOCTL_MN_FW_GET_EVENT_FLASH_LOG1:
     {
       TI_DBG6(("tiCOMMgntIOCTL: IOCTL_MN_FW_GET_EVENT_FLASH_LOG1\n"));
       status = tdsaRegDumpGetIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
       break;
     }
   }
   break;
  }

  case IOCTL_MJ_GET_EVENT_LOG2:
  {
    switch (agIOCTLPayload->MinorFunction)
    {
      case IOCTL_MN_FW_GET_TRACE_BUFFER:
      {
        agsaControllerEventLog_t EventLog;
        saGetControllerEventLogInfo(agRoot, &EventLog);
        TI_DBG6(("tiCOMMgntIOCTL: IOCTL_MJ_GET_EVENT_LOG2 Length %d\n", agIOCTLPayload->Length));
        RequestLength = agIOCTLPayload->Length;
        Offset = agIOCTLPayload->Reserved;
        EventLogLength = EventLog.eventLog2.totalLength;
        EventLogOption = EventLog.eventLog2Option;
        if (EventLogLength <= Offset)
        {
          TI_DBG1(("tiCOMMgntIOCTL: 2 out of range Requestlength %d Offset %d event log length %d\n", RequestLength, Offset, EventLogLength));
          /* out of range */
          agIOCTLPayload->Status = IOCTL_ERR_STATUS_NO_MORE_DATA;
          agIOCTLPayload->Length = 0;
          if(EventLogOption == 0)
          {
            agIOCTLPayload->Status = IOCTL_ERR_FW_EVENTLOG_DISABLED;
          }
          status=IOCTL_CALL_SUCCESS;
          return status;
        }
        ReadLength = MIN(EventLogLength - Offset, RequestLength);
        loc = (bit8 *)EventLog.eventLog2.virtPtr + Offset;
        osti_memcpy(&(agIOCTLPayload->FunctionSpecificArea), loc, ReadLength);
    //    tdhexdump("IOCTL_MJ_GET_EVENT_LOG2 first 32bytes", (bit8 *)&(agIOCTLPayload->FunctionSpecificArea), 32);
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
        agIOCTLPayload->Length = (bit16)ReadLength;
        status=IOCTL_CALL_SUCCESS;
        break;
      }
      case IOCTL_MN_FW_GET_EVENT_FLASH_LOG2:
      {
        TI_DBG6(("tiCOMMgntIOCTL: IOCTL_MN_FW_GET_EVENT_FLASH_LOG2\n"));
        status = tdsaRegDumpGetIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
        break;
      }
    }
    break;
  }


  case IOCTL_MJ_FW_INFO:
  {
    agsaControllerInfo_t ControllerInfo;
    saGetControllerInfo(agRoot, &ControllerInfo);
    TI_DBG1(("tiCOMMgntIOCTL: IOCTL_MJ_FW_INFO Length %d\n", agIOCTLPayload->Length));
    RequestLength = agIOCTLPayload->Length;
    Offset = agIOCTLPayload->Reserved;
    if (RequestLength == 0)
    {
      TI_DBG1(("tiCOMMgntIOCTL: IOCTL_MJ_FW_INFO: No more Data!\n"));
      /* out of range */
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_NO_MORE_DATA;
      agIOCTLPayload->Length = 0;
      status=IOCTL_CALL_SUCCESS;
      return status;
    }

    osti_memcpy((bit8*)&(agIOCTLPayload->FunctionSpecificArea), (bit8*)&ControllerInfo, sizeof(agsaControllerInfo_t));

    TI_DBG1(("tiCOMMgntIOCTL:IOCTL_MJ_FW_INFO ControllerInfo signature 0x%X\n",ControllerInfo.signature));
    TI_DBG1(("tiCOMMgntIOCTL:IOCTL_MJ_FW_INFO ControllerInfo PCILinkRate 0x%X\n",ControllerInfo.PCILinkRate));
    TI_DBG1(("tiCOMMgntIOCTL:IOCTL_MJ_FW_INFO ControllerInfo PCIWidth 0x%X\n",ControllerInfo.PCIWidth));
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
    status=IOCTL_CALL_SUCCESS;
    break;

  }

  case IOCTL_MJ_GET_FW_REV:
  {
    agsaControllerInfo_t ControllerInfo;
    saGetControllerInfo(agRoot, &ControllerInfo);
    TI_DBG3(("tiCOMMgntIOCTL: IOCTL_MJ_GET_FW_REV Length %d\n", agIOCTLPayload->Length));
    RequestLength = agIOCTLPayload->Length;
    Offset = agIOCTLPayload->Reserved;
    if (RequestLength == 0)
    {
      TI_DBG1(("tiCOMMgntIOCTL: IOCTL_MJ_GET_FW_REV: No more Data!\n"));
      /* out of range */
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_NO_MORE_DATA;
      agIOCTLPayload->Length = 0;
      status=IOCTL_CALL_SUCCESS;
      return status;
    }

    osti_memcpy((bit8*)&(agIOCTLPayload->FunctionSpecificArea), (bit8*)&ControllerInfo.fwRevision, sizeof(bit32));
    loc = (bit8 *)&(agIOCTLPayload->FunctionSpecificArea)+ sizeof(bit32);
    osti_memcpy(loc, (bit8*)&ControllerInfo.sdkRevision, sizeof(bit32));

    agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
    status=IOCTL_CALL_SUCCESS;
    break;

  }

#ifdef SPC_ENABLE_PROFILE
  case IOCTL_MJ_FW_PROFILE:
  {
    TI_DBG6(("tiCOMMgntIOCTL: IOCTL_MJ_FW_PROFILE\n"));
    status = tdipFWProfileIoctl( tiRoot, agIOCTLPayload,
                                   agParam1, agParam2, agParam3);
    break;
  }
#endif /* SPC_ENABLE_PROFILE */

  case IOCTL_MJ_GET_CORE_DUMP:
  {
    TI_DBG6(("tiCOMMgntIOCTL: IOCTL_MJ_GET_CORE_DUMP\n"));
    if (tiIS_SPC(agRoot))
    {
      status = tdsaRegDumpGetIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
    }
    else
    {
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_NOT_SUPPORTED;
      status = IOCTL_CALL_SUCCESS;
    }
    break;
  }
//#endif
  case IOCTL_MJ_NVMD_SET:
  {
    bit8 nvmDev;
    TI_DBG6(("tiCOMMgntIOCTL: IOCTL_MJ_NVMD_SET\n"));
    nvmDev = (bit8) agIOCTLPayload->Status;
    agIOCTLPayload->Status = 0;
    status = tdsaNVMDSetIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, &nvmDev);
	break;
	}
#if 0
case IOCTL_MJ_GPIO: 
  {
    bit32 sVid =0;
    TI_DBG6(("tiCOMMgntIOCTL: IOCTL_MJ_GPIO\n"));

    /* Get Subsystem vendor  */
    sVid = ostiChipConfigReadBit32(tiRoot,0x2C);
    sVid = sVid & 0xFFFF;

    /* GPIO is only intended for chip down design 
     * therefore it's only applies to 8H/SPCv product family 
     */
    if(sVid == 0x9005)
    return IOCTL_CALL_INVALID_DEVICE;
    
    status = tdsaGpioSetup(tiRoot, agContext, agIOCTLPayload, agParam1, agParam2);
    if(status == IOCTL_CALL_SUCCESS)  
        status = IOCTL_CALL_PENDING; /* Wait for response from the Controller */
    else 
      return status;  

    break;
  }
#endif
  
  case IOCTL_MJ_SGPIO:
  {
    TI_DBG6(("tiCOMMgntIOCTL: IOCTL_MJ_SGPIO\n"));
    status = tdsaSGpioIoctlSetup(tiRoot, agContext, agIOCTLPayload, agParam1, agParam2);
    break;
  }

  case IOCTL_MJ_NVMD_GET:
  {
    bit8 nvmDev;
    TI_DBG3(("tiCOMMgntIOCTL: IOCTL_MJ_NVMD_GET\n"));
    nvmDev = (bit8) agIOCTLPayload->Status;
    agIOCTLPayload->Status = 0;
    status = tdsaNVMDGetIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, &nvmDev);
    break;
  }

  case IOCTL_MJ_GET_FORENSIC_DATA:
  {
    TI_DBG3(("tiCOMMgntIOCTL: IOCTL_MJ_GET_FORENSIC_DATA\n"));
    status = tdsaForensicDataGetIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
    break;
  }
  case IOCTL_MJ_GET_DEVICE_INFO:
  {
    TI_DBG3(("tiCOMMgntIOCTL: IOCTL_MJ_GET_DEVICE_INFO\n"));
    status = tdsaDeviceInfoGetIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
    break;
  }

  case IOCTL_MJ_GET_IO_ERROR_STATISTIC:
  {
    TI_DBG3(("tiCOMMgntIOCTL: IOCTL_MJ_GET_IO_ERROR_STATISTIC\n"));
    status = tdsaIoErrorStatisticGetIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
    break;
  }

  case IOCTL_MJ_GET_IO_EVENT_STATISTIC:
  {
    TI_DBG3(("tiCOMMgntIOCTL: IOCTL_MJ_GET_IO_EVENT_STATISTIC\n"));
    status = tdsaIoEventStatisticGetIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
    break;
  }

  case IOCTL_MJ_SEND_BIST:
  {
    TI_DBG1(("tiCOMMgntIOCTL: IOCTL_MJ_SEND_BIST\n"));
    status = tdsaSendBISTIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
    break;
  }

#if 0 
  case IOCTL_MJ_SET_OR_GET_REGISTER:
  {
    TI_DBG3(("tiCOMMgntIOCTL: IOCTL_MJ_SET_OR_GET_REGISTER\n"));
    status = tdsaRegisterIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
    break;
  }
  
#endif
   case IOCTL_MJ_PHY_DETAILS:
   {
	PhyDetails_t  *PhyDetails = (PhyDetails_t*)&agIOCTLPayload->FunctionSpecificArea;
        agsaRoot_t  *agRoot = &(tdsaAllShared->agRootNonInt);
        agsaLLRoot_t  *saRoot = (agsaLLRoot_t *)(agRoot->sdkData); 	
	bit8  *sasAddressHi;
	bit8  *sasAddressLo;
	bit8  sas_dev_type;
	int i = 0;

	tiIniGetDirectSataSasAddr(tiRoot, i , &sasAddressHi, &sasAddressLo);
	for( i = 0; i < saRoot->phyCount ; i++)
        {	
		PhyDetails[i].attached_phy = saRoot->phys[i].sasIdentify.phyIdentifier;
		/* deice types
 		 * SAS	 
 		 * 0x01 - Sas end device   
 		 * 0x02 - Expander device 
 		 * SATA
 		 * 0x11 - Sata
 		 * NO DEVICE 0x00
 		 */
		sas_dev_type = (saRoot->phys[i].sasIdentify.deviceType_addressFrameType & 0x70 ) >> 4 ;
		if ((saRoot->phys[i].status == 1) && (sas_dev_type == 0)){ //status 1 - Phy Up 
			//Sata phy 
			PhyDetails[i].attached_dev_type = SAS_PHY_SATA_DEVICE;//0x11 for sata end device
			osti_memcpy(&PhyDetails[i].attached_sasAddressHi, tdsaAllShared->Ports[i].SASID.sasAddressHi, sizeof(bit32));
			osti_memcpy(&PhyDetails[i].attached_sasAddressLo, tdsaAllShared->Ports[i].SASID.sasAddressLo, sizeof(bit32));
			PhyDetails[i].attached_sasAddressLo[3] += i + 16; 
		}	
		else {
			PhyDetails[i].attached_dev_type = sas_dev_type;
	        	osti_memcpy(&PhyDetails[i].attached_sasAddressHi, saRoot->phys[i].sasIdentify.sasAddressHi, sizeof(bit32));
			osti_memcpy(&PhyDetails[i].attached_sasAddressLo, saRoot->phys[i].sasIdentify.sasAddressLo, sizeof(bit32));
		}
		osti_memcpy(&PhyDetails[i].sasAddressLo,&(tdsaAllShared->Ports[i].SASID.sasAddressLo), sizeof(bit32));
		osti_memcpy(&PhyDetails[i].sasAddressHi,&(tdsaAllShared->Ports[i].SASID.sasAddressHi), sizeof(bit32));
	}

//    	osti_memcpy(&agIoctlPayload->FunctionSpecificArea,&PhyInfo, sizeof(agsaSGpioReqResponse_t));
//	printk("Ioctl success\n");
	return IOCTL_CALL_SUCCESS;		
   }

   case IOCTL_MJ_PHY_GENERAL_STATUS:
 	  {
		agsaPhyGeneralState_t     *PhyData=NULL;
		bit32					   ret = AGSA_RC_FAILURE;
  		PhyData = (agsaPhyGeneralState_t*) &agIOCTLPayload->FunctionSpecificArea[0];

        PhyData->Reserved2 = 0;
        /* Validate the length */
        if (agIOCTLPayload->Length < sizeof(agsaPhyGeneralState_t))
        {
          status = IOCTL_CALL_FAIL;
          break;
        }
 
        tdsaAllShared->tdFWControlEx.param1 = agParam1;
        tdsaAllShared->tdFWControlEx.param2 = agParam2;
        tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
        tdsaAllShared->tdFWControlEx.inProgress = 1;
	//tdsaAllShared->tdFWControlEx.usrAddr = PhyData;

    	ret = tdsaGetPhyGeneralStatusIoctl(tiRoot,PhyData);
    	if(ret == AGSA_RC_FAILURE)
        {
    	  status = IOCTL_CALL_FAIL;
		  tdsaAllShared->tdFWControlEx.payload = NULL; 
		  tdsaAllShared->tdFWControlEx.inProgress = 0;
		  break;
    	}
		else if(ret == IOCTL_ERR_STATUS_NOT_SUPPORTED)
		{

		  agIOCTLPayload->Status = IOCTL_ERR_STATUS_NOT_SUPPORTED;
		  status = IOCTL_CALL_SUCCESS;
		  break;
		}

    	//status = IOCTL_CALL_PENDING;
    	status = IOCTL_CALL_PENDING;
     }

   break;
#if 1 
  case IOCTL_MJ_GET_PHY_PROFILE:
  {
    TI_DBG1(("tiCOMMgntIOCTL: IOCTL_MJ_GET_PHY_PROFILE %p %p %p\n",agParam1,agParam2,agParam3));
    status = tdsaPhyProfileIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, agParam3);
    break;
  }
#endif
  case IOCTL_MJ_LL_TRACING:
  {
    void * stu = &agIOCTLPayload->FunctionSpecificArea[0];
    switch(agIOCTLPayload->MinorFunction)
    {

      case IOCTL_MN_LL_RESET_TRACE_INDEX:
      {

#ifdef SA_ENABLE_TRACE_FUNCTIONS
        TSTMTID_TRACE_BUFFER_RESET *llist = (TSTMTID_TRACE_BUFFER_RESET *)stu;
        hpTraceBufferParms_t  BufferParms;
        TI_DBG5(("tdReturnIOCTL_Info: hpIOCTL_ResetTraceIndex\n"));

        BufferParms.TraceCompiled  = 0;
        BufferParms.TraceWrap      = 0;
        BufferParms.CurrentTraceIndexWrapCount = 0;
        BufferParms.BufferSize     = 0;
        BufferParms.CurrentIndex   = 0;
        BufferParms.pTrace         = NULL;
        BufferParms.pTraceIndexWrapCount        = NULL;
        BufferParms.pTraceMask     = NULL;
        BufferParms.pCurrentTraceIndex  = NULL;

        smTraceGetInfo(agRoot,&BufferParms);
        TI_DBG5(("tdReturnIOCTL_Info: pTrace                %p\n",BufferParms.pTrace));
        TI_DBG5(("tdReturnIOCTL_Info: pCurrentTraceIndex    %p %X\n",BufferParms.pCurrentTraceIndex,*BufferParms.pCurrentTraceIndex));
        TI_DBG5(("tdReturnIOCTL_Info: pTraceIndexWrapCount  %p %X\n",BufferParms.pTraceIndexWrapCount,*BufferParms.pTraceIndexWrapCount));
        TI_DBG5(("tdReturnIOCTL_Info: pTraceMask            %p %X\n",BufferParms.pTraceMask,*BufferParms.pTraceMask));

        if( llist->Flag != 0)
        {
          if( llist->TraceMask != *BufferParms.pTraceMask)
          {
            smTraceSetMask(agRoot,  llist->TraceMask );
          }
        }
        if( llist->Reset)
        {

          *BufferParms.pCurrentTraceIndex = 0;
          smResetTraceBuffer(agRoot);

          *BufferParms.pCurrentTraceIndex = 0;
          *BufferParms.pTraceIndexWrapCount =0;
          llist->TraceMask = *BufferParms.pTraceMask;
        }
#endif  /* SA_ENABLE_TRACE_FUNCTIONS  */
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
        status = IOCTL_CALL_SUCCESS;

      }
      break;

    case IOCTL_MN_LL_GET_TRACE_BUFFER_INFO:
      {
        hpTraceBufferParms_t  BufferParms;
        TSTMTID_TRACE_BUFFER_INFO *llist = (TSTMTID_TRACE_BUFFER_INFO *)stu;
        TI_DBG5(("tdReturnIOCTL_Info: hpIOCTL_GetTraceBufferInfo\n"));


        BufferParms.TraceCompiled  = 0;
        BufferParms.TraceWrap      = 0;
        BufferParms.CurrentTraceIndexWrapCount = 0;
        BufferParms.BufferSize     = 0;
        BufferParms.CurrentIndex   = 0;
        BufferParms.pTrace         = NULL;
        BufferParms.pTraceMask     = NULL;
#ifdef SA_ENABLE_TRACE_FUNCTIONS
        smTraceGetInfo(agRoot,&BufferParms);
#endif  /* SA_ENABLE_TRACE_FUNCTIONS not enabled */
        llist->TraceCompiled = BufferParms.TraceCompiled;
        llist->BufferSize = BufferParms.BufferSize;
        llist->CurrentIndex = BufferParms.CurrentIndex ;
        llist->CurrentTraceIndexWrapCount =  BufferParms.CurrentTraceIndexWrapCount;
        llist->TraceWrap = BufferParms.TraceWrap;
        if(BufferParms.pTraceMask != NULL)
        {
          llist->TraceMask = *BufferParms.pTraceMask;
        }
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
        status = IOCTL_CALL_SUCCESS;
      }
      break;

    case IOCTL_MN_LL_GET_TRACE_BUFFER:
      {
#ifdef SA_ENABLE_TRACE_FUNCTIONS
        TSTMTID_TRACE_BUFFER_FETCH *llist = (TSTMTID_TRACE_BUFFER_FETCH *)stu;

        hpTraceBufferParms_t  BufferParms;
        bit32 c= 0;

        BufferParms.TraceCompiled  = 0;
        BufferParms.TraceWrap      = 0;
        BufferParms.CurrentTraceIndexWrapCount = 0;
        BufferParms.BufferSize     = 0;
        BufferParms.CurrentIndex   = 0;
        BufferParms.pTrace         = NULL;
        smTraceGetInfo(agRoot,&BufferParms);

        TI_DBG6(("tdReturnIOCTL_Info: hpIOCTL_GetTraceBuffer\n"));

        if(llist->LowFence != LowFence32Bits)
        {
          break;
        }
        if(llist->HighFence != HighFence32Bits)
        {
          break;
        }

        if(llist->BufferOffsetBegin + FetchBufferSIZE > BufferParms.BufferSize  )
        {
        }

        for ( c=0; c < FetchBufferSIZE;c++)
        {
          llist->Data[c] = *(BufferParms.pTrace+( c + llist->BufferOffsetBegin));
        }
#endif  /* SA_ENABLE_TRACE_FUNCTIONS not enabled */
      }
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
      status = IOCTL_CALL_SUCCESS;
      break;
    }
    break;
  }

#ifdef SA_LL_API_TEST
  case IOCTL_MJ_LL_API_TEST:
  {
    status = tdLlApiTestIoctl( tiRoot, agIOCTLPayload,
               agParam1,agParam2,agParam3 );
    break;
  }

#endif /* SA_LL_API_TEST */

  case IOCTL_MJ_MODE_CTL_PAGE:
  {
    /* The SPCv controller has some options accessed via mode pages */
    tiEncryptDekConfigPage_t *pModePage= (tiEncryptDekConfigPage_t *) &agIOCTLPayload->FunctionSpecificArea[0];
    bit32 pageLength = 0;
    bit32 pageCode;
    bit32 modeOperation;

    pageCode = pModePage->pageCode & 0xFF;
    modeOperation = *(bit32 *) agParam2;

    switch(modeOperation)
    {

      case tiModePageSet:
        switch (pageCode)
        {
          case TI_ENCRYPTION_DEK_CONFIG_PAGE:
            pageLength = sizeof(tiEncryptDekConfigPage_t);
            break;

          case TI_ENCRYPTION_CONTROL_PARM_PAGE:
            pageLength = sizeof(tiEncryptControlParamPage_t);
            break;

          case TI_ENCRYPTION_GENERAL_CONFIG_PAGE:
            /* Pages are currently unsupported */
            pageLength = 0;
            break;
        }

        status = saSetControllerConfig(agRoot, 0, pageCode, pageLength, pModePage, (agsaContext_t *)agIOCTLPayload);
        break;

      case tiModePageGet:
        status = saGetControllerConfig(agRoot, 0, pageCode, 0, 0, (agsaContext_t *)agIOCTLPayload);
        break;

      default:
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_NOT_SUPPORTED;
    }
  }
    break;
#ifdef PHY_RESTART_TEST
    case IOCTL_MJ_PORT_START:
    {
      bit32 portID, tiStatus;
      bit32 *data = (bit32*) &agIOCTLPayload->FunctionSpecificArea[0];
      portID = *data;

      tiStatus = tiCOMPortStart(tiRoot, portID, tdsaAllShared->Ports[portID].tiPortalContext, 0);

      if (tiStatus == tiSuccess)
      {
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
      }
      else
      {
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
      }
      status = IOCTL_CALL_SUCCESS;
      break;
    }

    case IOCTL_MJ_PORT_STOP:
    {
      bit32 portID, tiStatus;
      bit32 *data = (bit32*) &agIOCTLPayload->FunctionSpecificArea[0];
      portID =  *data;

      tiStatus = tiCOMPortStop(tiRoot, tdsaAllShared->Ports[portID].tiPortalContext);
      if (tiStatus == tiSuccess)
      {
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
      }
      else
      {
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
      }

      status = IOCTL_CALL_SUCCESS;
      break;
    }
#endif
 case IOCTL_MJ_SEND_TMF:
       switch(agIOCTLPayload->MinorFunction)
	{
	     case IOCTL_MN_TMF_DEVICE_RESET:
		status = tdsaSendTMFIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, AG_TARGET_WARM_RESET);
	        break;
	     case IOCTL_MN_TMF_LUN_RESET:
	 	status = tdsaSendTMFIoctl(tiRoot, agIOCTLPayload, agParam1, agParam2, AG_LOGICAL_UNIT_RESET);
		break;
	}
	break;
 case IOCTL_MJ_GET_DRIVER_VERSION:
        osti_sprintf(agIOCTLPayload->FunctionSpecificArea, "%s", AGTIAPI_DRIVER_VERSION);
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
        status=IOCTL_CALL_SUCCESS;
	break;
  default:
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_NOT_SUPPORTED;
    break;
  }

  return status;
}

#if 0
/*****************************************************************************
*
* tdsaGpioSetup 
*
* Purpose:  This routine is called to set Gpio parameters to the controller.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agsaContext_t :
*   tiIOCTLPayload_t :  ioctl header with payload gpio info 
*   agParam1,agParam2 :  Generic parameters
*
* Return: status
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaGpioSetup(
                tiRoot_t            *tiRoot,
                agsaContext_t       *agContext,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2
                )
{

  tdsaTimerRequest_t        *osIoctlTimer;
  agsaGpioEventSetupInfo_t  *gpioEventSetupInfo;
  agsaGpioWriteSetupInfo_t  *gpioWriteSetupInfo;
  agsaGpioPinSetupInfo_t    *gpioPinSetupInfo;
  tdsaRoot_t                *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                *agRoot = &(tdsaAllShared->agRootInt);
  bit32                     status = IOCTL_CALL_SUCCESS;

  TI_DBG3(("tdsaGpioSetup: start\n"));

  if(tiRoot == agNULL || agIOCTLPayload == agNULL )
  return IOCTL_CALL_FAIL;

  osIoctlTimer = &tdsaAllShared->osIoctlTimer;
  tdsaInitTimerRequest(tiRoot, osIoctlTimer);
  tdIoctlStartTimer(tiRoot, osIoctlTimer); /* Start the timout handler for both ioctl and controller response */
  tdsaAllShared->tdFWControlEx.virtAddr = (bit8 *)osIoctlTimer;

  tdsaAllShared->tdFWControlEx.usrAddr = (bit8 *)&agIOCTLPayload->FunctionSpecificArea[0];
  tdsaAllShared->tdFWControlEx.param1 = agParam1;
  tdsaAllShared->tdFWControlEx.param2 = agParam2;
  tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
  tdsaAllShared->tdFWControlEx.inProgress = 1;

    switch (agIOCTLPayload->MinorFunction)
    {
     
     case IOCTL_MN_GPIO_PINSETUP:
     {
	 TI_DBG3(("tdsaGpioSetup: IOCTL_MN_GPIO_PINSETUP\n"));
         gpioPinSetupInfo =(agsaGpioPinSetupInfo_t *)&agIOCTLPayload->FunctionSpecificArea[0];
         status = saGpioPinSetup(agRoot, agContext, 0, gpioPinSetupInfo);

         break;
     }	 
     case IOCTL_MN_GPIO_EVENTSETUP:
     {
	TI_DBG3(("tdsaGpioSetup: IOCTL_MN_GPIO_EVENTSETUP\n"));
        gpioEventSetupInfo = (agsaGpioEventSetupInfo_t  *)&agIOCTLPayload->FunctionSpecificArea[0];
        status = saGpioEventSetup(agRoot, agContext, 0, gpioEventSetupInfo);

        break;
     }
   	
     case IOCTL_MN_GPIO_READ:
     {
	 TI_DBG3(("tdsaGpioSetup: IOCTL_MN_GPIO_READ\n"));
         status = saGpioRead(agRoot, agContext, 0);

        break;
     }   	 	 

     case IOCTL_MN_GPIO_WRITE:
     {
	 TI_DBG3(("tdsaGpioSetup: IOCTL_MN_GPIO_WRITE\n"));
         gpioWriteSetupInfo = (agsaGpioWriteSetupInfo_t *)&agIOCTLPayload->FunctionSpecificArea[0];
         status = saGpioWrite(agRoot, agContext, 0, gpioWriteSetupInfo->gpioWritemask, gpioWriteSetupInfo->gpioWriteVal);

         break;
     }
     
     default :
         return status;
    }

    if(status != AGSA_RC_SUCCESS)
    {
      status = IOCTL_CALL_FAIL;
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;

      tdsaSingleThreadedEnter(tiRoot, TD_TIMER_LOCK);
      if (osIoctlTimer->timerRunning == agTRUE)
      {
         tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);
         tdsaKillTimer(tiRoot, osIoctlTimer);
        
      }else{
         tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);
      }
    }

    TI_DBG3(("tdsaGpioPinSetup: End\n"));
    return status;

}
#endif

/*****************************************************************************
*
* ostiGetGpioIOCTLRsp
*
* Purpose:  This routine is called for Get Gpio IOCTL reaponse has been received.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   payloadRsp:     Pointer to the FW download IOMB's payload.
*
* Return: none
*
*
*****************************************************************************/

osGLOBAL void  ostiGetGpioIOCTLRsp(
                        tiRoot_t                 *tiRoot,
                        bit32                    status,
                        bit32                    gpioReadValue,
                        agsaGpioPinSetupInfo_t   *gpioPinSetupInfo,
                        agsaGpioEventSetupInfo_t *gpioEventSetupInfo
                        )
{
     tdsaRoot_t                *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
     tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
     tiIOCTLPayload_t          *agIoctlPayload ;
     agsaGpioReadInfo_t        *gpioReadInfo;
      
     tdsaTimerRequest_t        *osIoctlTimer;
	 osIoctlTimer = (tdsaTimerRequest_t *)tdsaAllShared->tdFWControlEx.virtAddr;         

     TI_DBG2(("ostiGetGpioIOCTLRsp: start, status = %d \n", status));

     agIoctlPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload); 
    
     if(agIoctlPayload == agNULL){
        return;  
      }

     agIoctlPayload->Status =(bit16) status;

     if( (status != IOCTL_CALL_TIMEOUT) && (osIoctlTimer != NULL))
     {
        tdsaSingleThreadedEnter(tiRoot, TD_TIMER_LOCK);
        if (osIoctlTimer->timerRunning == agTRUE)
        {
           tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);
           tdsaKillTimer(tiRoot, osIoctlTimer);
        
        }else{
           tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);
        }
     }else  {
         tdsaAllShared->tdFWControlEx.inProgress = 0;
         agIoctlPayload->Status = (bit16)status;
         ostiIOCTLSetSignal(tiRoot, tdsaAllShared->tdFWControlEx.param1,
                               tdsaAllShared->tdFWControlEx.param2, NULL);
        return; 
     }

     if(status == SUCCESS) 
       TI_DBG3((" ostiGetGpioIOCTLRsp:Got GPIO response from OUTBuf"));
    else {
      tdsaAllShared->tdFWControlEx.inProgress = 0;
      ostiIOCTLSetSignal(tiRoot, tdsaAllShared->tdFWControlEx.param1,
                               tdsaAllShared->tdFWControlEx.param2, NULL);
      return;     
    }

    switch (agIoctlPayload->MinorFunction)
     {

     case IOCTL_MN_GPIO_PINSETUP:
      {
       TI_DBG3((" ostiGetGpioIOCTLRsp:Got GPIO response for IOCTL_MN_GPIO_PINSETUP"));

         break;
      }	 
     case IOCTL_MN_GPIO_EVENTSETUP:
     {
       TI_DBG3((" ostiGetGpioIOCTLRsp:Got GPIO response for IOCTL_MN_GPIO_EVENTSETUP"));

         break;
     }

     case IOCTL_MN_GPIO_WRITE:
     {
       TI_DBG3((" ostiGetGpioIOCTLRsp:Got GPIO response for IOCTL_MN_GPIO_WRITE"));

         break;
     }
   	
    case IOCTL_MN_GPIO_READ:
    {
         gpioReadInfo = ( agsaGpioReadInfo_t *)tdsaAllShared->tdFWControlEx.usrAddr;

         gpioReadInfo->gpioReadValue = gpioReadValue;
         gpioReadInfo->gpioInputEnabled = gpioPinSetupInfo->gpioInputEnabled ; /* GPIOIE */
         gpioReadInfo->gpioEventLevelChangePart1 = gpioPinSetupInfo->gpioTypePart1; /* GPIEVCHANGE (pins 11-0) */
         gpioReadInfo->gpioEventLevelChangePart2 = gpioPinSetupInfo->gpioTypePart2; /* GPIEVCHANGE (pins 23-20) */
         gpioReadInfo->gpioEventRisingEdgePart1 = 0xFFF & gpioEventSetupInfo->gpioEventRisingEdge; /* GPIEVRISE (pins 11-0) */
         gpioReadInfo->gpioEventRisingEdgePart2 = 0x00F00000 & (gpioEventSetupInfo->gpioEventRisingEdge); /* GPIEVRISE (pins 23-20) */
         gpioReadInfo->gpioEventFallingEdgePart1 = 0xFFF & gpioEventSetupInfo->gpioEventFallingEdge; /* GPIEVALL (pins 11-0) */
         gpioReadInfo->gpioEventFallingEdgePart2 = 0x00F00000  & gpioEventSetupInfo->gpioEventFallingEdge; /* GPIEVALL (pins 23-20 */

         break;
     }   	 	 
 
    default : 
         break;     
    }

    if(tdsaAllShared->tdFWControlEx.inProgress) 
    {
      tdsaAllShared->tdFWControlEx.inProgress = 0;
      ostiIOCTLSetSignal(tiRoot, tdsaAllShared->tdFWControlEx.param1,
                              tdsaAllShared->tdFWControlEx.param2, NULL);
    }
    TI_DBG2(("ostiGetGpioIOCTLRsp: end \n"));

   return ;
}

/*****************************************************************************
*
* tdsaSGpioIoctlSetup 
*
* Purpose:  This routine is called to send SGPIO request to the controller.
*
* Parameters:
*   tiRoot:             Pointer to driver instance
*   agsaContext_t:      Context for this request
*   tiIOCTLPayload_t:   ioctl header with payload sgpio info 
*   agParam1,agParam2:  Generic parameters
*
* Return: status
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaSGpioIoctlSetup(
                tiRoot_t            *tiRoot,
                agsaContext_t       *agContext,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2
                )
{
  tdsaRoot_t                *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                *agRoot = &(tdsaAllShared->agRootInt);
  bit32                     status = IOCTL_CALL_FAIL;
  agsaSGpioReqResponse_t    *pSGpioReq = (agsaSGpioReqResponse_t *)&agIOCTLPayload->FunctionSpecificArea[0];

  TI_DBG3(("tdsaSGpioIoctlSetup: start\n"));
  
  agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
  
  do
  {
    if (tiRoot == agNULL || agIOCTLPayload == agNULL)
    {
      break;
    }
    
    /* Validate the length */
    if (agIOCTLPayload->Length < sizeof(agsaSGpioReqResponse_t))
    {
      TI_DBG3(("Invalid length\n"));
      break;
    }
  
    /* Validate the SMP Frame Type, Function and Register Type fields */
    if ((pSGpioReq->smpFrameType != SMP_REQUEST) || \
        ((pSGpioReq->function != SMP_READ_GPIO_REGISTER) && (pSGpioReq->function != SMP_WRITE_GPIO_REGISTER)) || \
        (pSGpioReq->registerType > AGSA_SGPIO_GENERAL_PURPOSE_TRANSMIT_REG))
    {
      TI_DBG4(("Invalid Parameter\n"));
      break;
    }
		
    /* Specific validation for configuration register type */
    if (AGSA_SGPIO_CONFIG_REG == pSGpioReq->registerType)
    {
      if ((pSGpioReq->registerIndex > 0x01) || \
          ((0x00 == pSGpioReq->registerIndex) && (pSGpioReq->registerCount > 0x02)) || \
          ((0x01 == pSGpioReq->registerIndex) && (pSGpioReq->registerCount > 0x01)))
      {
        break;
      }
    }
  
    /* Use FW control place in shared structure to keep the necessary information */
    tdsaAllShared->tdFWControlEx.param1 = agParam1;
    tdsaAllShared->tdFWControlEx.param2 = agParam2;
    tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
    tdsaAllShared->tdFWControlEx.inProgress = 1;
	  
    status = saSgpio(agRoot, agContext, 0, pSGpioReq);
    if (status != AGSA_RC_SUCCESS)
    {
      break;
    }

    status = IOCTL_CALL_PENDING;

  } while (0);
  
  TI_DBG3(("tdsaGpioPinSetup: End\n"));
  return status;
}

/*****************************************************************************
*
* ostiSgpioIoctlRsp
*
* Purpose:  This routine is called when a SGPIO IOCTL response is received.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   pSgpioResponse: Pointer to the SGPIO response
*
* Return: none
*
*
*****************************************************************************/
osGLOBAL void ostiSgpioIoctlRsp(
                            tiRoot_t                *tiRoot,
                            agsaSGpioReqResponse_t  *pSgpioResponse
                            )
{
  tdsaRoot_t        *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tiIOCTLPayload_t  *agIoctlPayload = agNULL;

  TI_DBG3(("ostiSgpioIoctlRsp: start\n"));

  if (tdsaAllShared->tdFWControlEx.inProgress) 
  {
    agIoctlPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
    if (agIoctlPayload)
    {
      tdsaAllShared->tdFWControlEx.payload = NULL; 
      osti_memcpy(&agIoctlPayload->FunctionSpecificArea[0], pSgpioResponse, sizeof(agsaSGpioReqResponse_t));
      agIoctlPayload->Status = IOCTL_ERR_STATUS_OK;
      sgpioResponseSet = 1;
    }
	tdsaAllShared->sgpioResponseSet = 1;    //Sunitha:Check if needed?
    
    ostiIOCTLSetSignal(tiRoot, tdsaAllShared->tdFWControlEx.param1,
                  tdsaAllShared->tdFWControlEx.param2, agNULL);
                  
    tdsaAllShared->tdFWControlEx.inProgress = 0;
  }

  TI_DBG3(("ostiSgpioIoctlRsp: end\n"));
}
/*****************************************************************************
*
* ostiCOMMgntIOCTLRsp
*
* Purpose:  This routine is called when FW control IOCTL reaponse has been received.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:          Pointer to driver instance
*   payloadRsp:     Pointer to the FW download IOMB's payload.
*
* Return: none
*
*
*
*****************************************************************************/

osGLOBAL void ostiCOMMgntIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        )
{
    tdsaRoot_t           *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

    TI_DBG1(("ostiCOMMgntIOCTLRsp: status 0x%x\n",status));
    (tdsaAllShared->tdFWControlEx.tdFWControl)->retcode = status;

    ostiFreeMemory(tiRoot,
                   tdsaAllShared->tdFWControlEx.buffer,
                   tdsaAllShared->tdFWControlEx.tdFWControl->len);

    ostiIOCTLSetSignal(tiRoot,
                       tdsaAllShared->tdFWControlEx.param1,
                       tdsaAllShared->tdFWControlEx.param2,
                       NULL);
}


/*****************************************************************************
*
* ostiRegDumpIOCTLRsp
*
* Purpose:  This routine is called when Register Dump from flash IOCTL reaponse has been received.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:          Pointer to driver instance
*   payloadRsp:     Pointer to the FW download IOMB's payload.
*
* Return: none
*
*
*
*****************************************************************************/

osGLOBAL void ostiRegDumpIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        )
{
    tdsaRoot_t           *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

    TI_DBG1(("ostiRegDumpIOCTLRsp: start\n"));
//    (tdsaAllShared->tdFWControlEx.tdFWControl)->retcode = status;
    osti_memcpy((void *)(tdsaAllShared->tdFWControlEx.usrAddr),
                (void *)(tdsaAllShared->tdFWControlEx.virtAddr),
                tdsaAllShared->tdFWControlEx.len);

    ostiFreeMemory(tiRoot,
                  tdsaAllShared->tdFWControlEx.buffer,
                  tdsaAllShared->tdFWControlEx.len);

    ostiIOCTLSetSignal(tiRoot,
                       tdsaAllShared->tdFWControlEx.param1,
                       tdsaAllShared->tdFWControlEx.param2,
                       NULL);
}

/*****************************************************************************
*
* ostiSetNVMDIOCTLRsp
*
* Purpose:  This routine is called for Set NVMD IOCTL reaponse has been received.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:          Pointer to driver instance
*   payloadRsp:     Pointer to the FW download IOMB's payload.
*
* Return: none
*
*
*
*****************************************************************************/

osGLOBAL void ostiSetNVMDIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        )
{
    tdsaRoot_t           *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tiIOCTLPayload_t     *agIOCTLPayload;

    if(status)
    {
        agIOCTLPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
        agIOCTLPayload->Status = (bit16)status;
    }

    TI_DBG1(("ostiSetNVMDIOCTLRsp: start, status = %d\n", status));
//    (tdsaAllShared->tdFWControlEx.tdFWControl)->retcode = status;
    ostiFreeMemory(tiRoot,
                       tdsaAllShared->tdFWControlEx.buffer,
                       tdsaAllShared->tdFWControlEx.len);

    ostiIOCTLSetSignal(tiRoot,
                       tdsaAllShared->tdFWControlEx.param1,
                       tdsaAllShared->tdFWControlEx.param2,
                       NULL);
}
#ifdef SPC_ENABLE_PROFILE
/*****************************************************************************
*
* ostiFWProfileIOCTLRsp
*
* Purpose:  This routine is called for Fw Profile IOCTL reaponse has been received.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:          Pointer to driver instance
*   status:
*
* Return: none
*
*
*
*****************************************************************************/

osGLOBAL void ostiFWProfileIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status,
            bit32               len)
{
    tdsaRoot_t           *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tdFWProfile_t        *fwProfile;

    TI_DBG1(("ostiFWProfileIOCTLRsp: start\n"));
    fwProfile = (tdFWProfile_t *)tdsaAllShared->tdFWProfileEx.tdFWProfile;
  //    (tdsaAllShared->tdFWControlEx.tdFWControl)->retcode = status;
    if (status == AGSA_RC_SUCCESS)
    {
      if((fwProfile->cmd == STOP_TIMER_PROFILE) || (fwProfile->cmd == STOP_CODE_PROFILE))
        {
        osti_memcpy((void *)(fwProfile->buffer),
                  (void *)(tdsaAllShared->tdFWProfileEx.virtAddr),
                  len);

        ostiFreeMemory(tiRoot,
                         tdsaAllShared->tdFWProfileEx.buffer,
                         tdsaAllShared->tdFWProfileEx.len);
      }
    }
    fwProfile->status = status;
    fwProfile->len = len;
    ostiIOCTLSetSignal(tiRoot,
                       tdsaAllShared->tdFWProfileEx.param1,
                       tdsaAllShared->tdFWProfileEx.param2,
                       NULL);
}
#endif
/*****************************************************************************
*
* ostiGetNVMDIOCTLRsp
*
* Purpose:  This routine is called for Get NVMD IOCTL reaponse has been received.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:          Pointer to driver instance
*   payloadRsp:     Pointer to the FW download IOMB's payload.
*
* Return: none
*
*
*
*****************************************************************************/

osGLOBAL void ostiGetNVMDIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        )
{
    tdsaRoot_t           *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tiIOCTLPayload_t     *agIOCTLPayload;

    if(status)
    {
        agIOCTLPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
        agIOCTLPayload->Status = (bit16)status;
    }

    TI_DBG1(("ostiGetNVMDIOCTLRsp: start, status = %d\n", status));
    tdsaAllShared->NvmdResponseSet = 1;
   
    if(tdsaAllShared->tdFWControlEx.param1 != agNULL)
    {
    osti_memcpy((void *)(tdsaAllShared->tdFWControlEx.usrAddr),
                (void *)(tdsaAllShared->tdFWControlEx.virtAddr),
                tdsaAllShared->tdFWControlEx.len);

    ostiFreeMemory(tiRoot,
                   tdsaAllShared->tdFWControlEx.buffer,
                   tdsaAllShared->tdFWControlEx.len);

    ostiIOCTLSetSignal(tiRoot,
                       tdsaAllShared->tdFWControlEx.param1,
                       tdsaAllShared->tdFWControlEx.param2,
                       NULL);
    }
}


/*****************************************************************************
*
* ostiGetPhyProfileIOCTLRsp
*
* Purpose:  This routine is called for phy response has been received.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:          Pointer to driver instance
*   payloadRsp:     Pointer to the IOMB's payload.
*
* Return: none
*
*
*
*****************************************************************************/

osGLOBAL void ostiGetPhyProfileIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        )
{
    tdsaRoot_t           *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tiIOCTLPayload_t     *agIOCTLPayload;
    tdPhyCount_t     *PhyBlob = agNULL;
    if(status)
    {
      agIOCTLPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
      agIOCTLPayload->Status = (bit16)status;

      PhyBlob = (tdPhyCount_t*)&agIOCTLPayload->FunctionSpecificArea[0];
      if(PhyBlob)
      {
//        PhyBlob->Phy |= 0x800;
        if(PhyBlob->phyResetProblem == 0 )
        {
          PhyBlob->phyResetProblem = -1;
        }
        TI_DBG1(("ostiGetPhyProfileIOCTLRsp: PhyBlob->Phy                   0x%x\n",PhyBlob->Phy));
        TI_DBG1(("ostiGetPhyProfileIOCTLRsp: PhyBlob->BW_rx                 0x%x\n",PhyBlob->BW_rx));
        TI_DBG1(("ostiGetPhyProfileIOCTLRsp: PhyBlob->BW_tx                 0x%x\n",PhyBlob->BW_tx));
        TI_DBG1(("ostiGetPhyProfileIOCTLRsp: PhyBlob->InvalidDword          0x%x\n",PhyBlob->InvalidDword));
        TI_DBG1(("ostiGetPhyProfileIOCTLRsp: PhyBlob->runningDisparityError 0x%x\n",PhyBlob->runningDisparityError));
        TI_DBG1(("ostiGetPhyProfileIOCTLRsp: PhyBlob->codeViolation         0x%x\n",PhyBlob->codeViolation));
        TI_DBG1(("ostiGetPhyProfileIOCTLRsp: PhyBlob->phyResetProblem       0x%x\n",PhyBlob->phyResetProblem));
        TI_DBG1(("ostiGetPhyProfileIOCTLRsp: PhyBlob->inboundCRCError       0x%x\n",PhyBlob->inboundCRCError));

      }


    }

    TI_DBG1(("ostiGetPhyProfileIOCTLRsp: start, status = %d\n", status));
    TI_DBG1(("ostiGetPhyProfileIOCTLRsp: start, len = %d %p %p\n", tdsaAllShared->tdFWControlEx.len,tdsaAllShared->tdFWControlEx.usrAddr,tdsaAllShared->tdFWControlEx.virtAddr));

//    osti_memcpy((void *)(tdsaAllShared->tdFWControlEx.usrAddr),
//                (void *)(tdsaAllShared->tdFWControlEx.virtAddr),
//                 tdsaAllShared->tdFWControlEx.len);

    ostiFreeMemory(tiRoot,
                   tdsaAllShared->tdFWControlEx.buffer,
                   tdsaAllShared->tdFWControlEx.len);

    ostiIOCTLSetSignal(tiRoot,
                       tdsaAllShared->tdFWControlEx.param1,
                       tdsaAllShared->tdFWControlEx.param2,
                       NULL);
}


/*****************************************************************************
*
* ostiGenEventIOCTLRsp
*
* Purpose:  This routine is called when General Event happened while waiting for IOCTL response.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:          Pointer to driver instance
*   payloadRsp:     Pointer to the FW download IOMB's payload.
*
* Return: none
*
*
*
*****************************************************************************/

osGLOBAL void ostiGenEventIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        )
{
    tdsaRoot_t           *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tiIOCTLPayload_t     *agIOCTLPayload;

    TI_DBG1(("ostiGenEventIOCTLRsp: start\n"));

    if(tdsaAllShared->tdFWControlEx.inProgress)  /*Free only if our IOCTL is in progress*/
    {
      agIOCTLPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
      (tdsaAllShared->tdFWControlEx.tdFWControl)->retcode = IOCTL_ERR_STATUS_INTERNAL_ERROR;

      ostiFreeMemory(tiRoot,
                     tdsaAllShared->tdFWControlEx.buffer,
                     tdsaAllShared->tdFWControlEx.len);

      ostiIOCTLSetSignal(tiRoot,
                         tdsaAllShared->tdFWControlEx.param1,
                         tdsaAllShared->tdFWControlEx.param2,
                         NULL);
      tdsaAllShared->tdFWControlEx.inProgress = 0;
    }
#ifdef SPC_ENABLE_PROFILE
    if(tdsaAllShared->tdFWProfileEx.inProgress)
    {
      agIOCTLPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWProfileEx.payload);
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
      if(tdsaAllShared->tdFWProfileEx.virtAddr != NULL)  /*Free only if our IOCTL is in progress*/
      {
        ostiFreeMemory(tiRoot,
                       tdsaAllShared->tdFWProfileEx.buffer,
                       tdsaAllShared->tdFWProfileEx.len);
        tdsaAllShared->tdFWProfileEx.virtAddr = NULL;
      }
      ostiIOCTLSetSignal(tiRoot,
                         tdsaAllShared->tdFWProfileEx.param1,
                         tdsaAllShared->tdFWProfileEx.param2,
                         NULL);
      tdsaAllShared->tdFWProfileEx.inProgress = 0;

    }
#endif /*SPC_ENABLE_PROFILE*/

}

osGLOBAL void
ostiGetDeviceInfoIOCTLRsp(
                        tiRoot_t    *tiRoot,
                        bit32        status,
                        void        *param
                        )
{
    tdsaRoot_t             *tdsaRoot       = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t          *tdsaAllShared  = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tiIOCTLPayload_t       *agIOCTLPayload = agNULL;
    tdDeviceInfoPayload_t  *pTDDeviceInfo  = agNULL;
    agsaDeviceInfo_t       *pSADeviceInfo  = agNULL;

    TI_DBG1(("ostiGetDeviceInfoIOCTLRsp: start\n"));

    agIOCTLPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
    pSADeviceInfo = (agsaDeviceInfo_t*)param;
    pTDDeviceInfo = (tdDeviceInfoPayload_t*)agIOCTLPayload->FunctionSpecificArea;

    if (pSADeviceInfo != agNULL)
    {
      /* fill the device information in IOCTL payload */
      osti_memcpy(&pTDDeviceInfo->devInfo.sasAddressHi, pSADeviceInfo->sasAddressHi, sizeof(bit32));
      osti_memcpy(&pTDDeviceInfo->devInfo.sasAddressLo, pSADeviceInfo->sasAddressLo, sizeof(bit32));

      pTDDeviceInfo->devInfo.sasAddressHi = DMA_BEBIT32_TO_BIT32(pTDDeviceInfo->devInfo.sasAddressHi);
      pTDDeviceInfo->devInfo.sasAddressLo = DMA_BEBIT32_TO_BIT32(pTDDeviceInfo->devInfo.sasAddressLo);

      pTDDeviceInfo->devInfo.deviceType = (pSADeviceInfo->devType_S_Rate & 0x30) >> 4;
      pTDDeviceInfo->devInfo.linkRate   = pSADeviceInfo->devType_S_Rate & 0x0F;

      agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
    }
    else
    {
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_INVALID_DEVICE;
    }

    if(tdsaAllShared->tdFWControlEx.inProgress)  /*Free only if our IOCTL is in progress*/
    {
      ostiIOCTLSetSignal(tiRoot,
                         tdsaAllShared->tdFWControlEx.param1,
                         tdsaAllShared->tdFWControlEx.param2,
                         NULL);
      tdsaAllShared->tdFWControlEx.inProgress = 0;
    }
}


#ifdef INITIATOR_DRIVER
osGLOBAL void
ostiGetIoErrorStatsIOCTLRsp(
                        tiRoot_t    *tiRoot,
                        bit32        status,
                        void        *param
                        )
{
    tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t               *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    itdsaIni_t                  *Initiator       = (itdsaIni_t *)tdsaAllShared->itdsaIni;
    tiIOCTLPayload_t            *agIOCTLPayload  = agNULL;
    tdIoErrorStatisticPayload_t *pIoErrorPayload = agNULL;
    agsaIOErrorEventStats_t     *pIoErrorCount   = agNULL;

    OS_ASSERT(sizeof(agsaIOErrorEventStats_t) == sizeof(tdIoErrorEventStatisticIOCTL_t), "agsaIOErrorEventStats_t tdIoErrorEventStatisticIOCTL_t\n");
    TI_DBG1(("ostiGetIoErrorStatsIOCTLRsp: start\n"));

    agIOCTLPayload  = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
    pIoErrorPayload = (tdIoErrorStatisticPayload_t*)agIOCTLPayload->FunctionSpecificArea;
    pIoErrorCount   = (agsaIOErrorEventStats_t*)param;

    osti_memcpy(&pIoErrorPayload->IoError, pIoErrorCount, sizeof(agsaIOErrorEventStats_t));
    /*copy SCSI status and sense key count from OS layer to TD layer*/
    osti_memcpy(&pIoErrorPayload->ScsiStatusCounter, &Initiator->ScsiStatusCounts, sizeof(tdSCSIStatusCount_t));
    osti_memcpy(&pIoErrorPayload->SenseKeyCounter, &Initiator->SenseKeyCounter, sizeof(tdSenseKeyCount_t));
    if (pIoErrorPayload->flag)
    {
      osti_memset(&Initiator->ScsiStatusCounts, 0,sizeof(tdSCSIStatusCount_t) );
      osti_memset(&Initiator->SenseKeyCounter, 0,sizeof(tdSenseKeyCount_t) );
    }

    agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
}
#endif /* INITIATOR_DRIVER */

osGLOBAL void
ostiGetIoEventStatsIOCTLRsp(
                        tiRoot_t    *tiRoot,
                        bit32        status,
                        void        *param
                        )
{
    tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t               *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tiIOCTLPayload_t            *agIOCTLPayload  = agNULL;
    tdIoEventStatisticPayload_t *pIoEventPayload = agNULL;
    agsaIOErrorEventStats_t     *pIoEventCount   = agNULL;

    TI_DBG1(("ostiGetIoEventStatsIOCTLRsp: start\n"));

    agIOCTLPayload  = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
    pIoEventPayload = (tdIoEventStatisticPayload_t*)agIOCTLPayload->FunctionSpecificArea;
    pIoEventCount   = (agsaIOErrorEventStats_t*)param;

    osti_memcpy(&pIoEventPayload->IoEvent, pIoEventCount, sizeof(agsaIOErrorEventStats_t));

    agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
}

osGLOBAL void
ostiGetForensicDataIOCTLRsp(
                        tiRoot_t    *tiRoot,
                        bit32        status,
                        void        *param
                        )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tiIOCTLPayload_t            *agIOCTLPayload  = agNULL;
  tdForensicDataPayload_t     *pForensicDataPayload = agNULL;
  agsaForensicData_t          *pForensicData   = agNULL;

  TI_DBG3(("ostiGetForensicDataIOCTLRsp: start, status = %d\n", status));

  agIOCTLPayload  = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
  pForensicDataPayload = (tdForensicDataPayload_t*)agIOCTLPayload->FunctionSpecificArea;
  pForensicData   = (agsaForensicData_t*)param;

  if (agNULL == agIOCTLPayload)
  {
    return;
  }

  if (FORENSIC_DATA_TYPE_CHECK_FATAL == pForensicData->DataType)
  {
    agIOCTLPayload->Status = (bit16)status;
    return;
  }

  if (status == AGSA_RC_SUCCESS)
  {
    switch (pForensicData->DataType)
    {
      case FORENSIC_DATA_TYPE_NON_FATAL:
      case FORENSIC_DATA_TYPE_FATAL:
           pForensicDataPayload->dataBuffer.directOffset = pForensicData->BufferType.dataBuf.directOffset;
           pForensicDataPayload->dataBuffer.readLen      = pForensicData->BufferType.dataBuf.readLen;
           break;
      case FORENSIC_DATA_TYPE_GSM_SPACE:
           pForensicDataPayload->gsmBuffer.directOffset  = pForensicData->BufferType.gsmBuf.directOffset;
	   pForensicDataPayload->gsmBuffer.readLen 	 = pForensicData->BufferType.gsmBuf.readLen;
           break;

      case FORENSIC_DATA_TYPE_QUEUE:
           break;

      default:
           TI_DBG1(("ostiGetForensicDataIOCTLRsp: forensic data type error %d\n", pForensicData->DataType));
           break;
    }
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
  }
  else if(status == IOCTL_ERROR_NO_FATAL_ERROR)
  {
    agIOCTLPayload->Status = (bit16)status;
  }
  else
  {
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
  }

  /*Free only if our IOCTL is in progress*/
  if(tdsaAllShared->tdFWControlEx.inProgress)
  {
    TI_DBG3(("ostiGetForensicDataIOCTLRsp: Waiting for the signal \n"));
    ostiIOCTLSetSignal(tiRoot,
          tdsaAllShared->tdFWControlEx.param1,
          tdsaAllShared->tdFWControlEx.param2,
          NULL);
    TI_DBG3(("ostiGetForensicDataIOCTLRsp: Signal wait completed \n"));
    tdsaAllShared->tdFWControlEx.inProgress = 0;
  }
}

/*****************************************************************************
*
* tdsaRegDumpGetIoctl
*
* Purpose:  This routine is called to get Register Dump information.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaRegDumpGetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                )
{
    tdsaRoot_t    *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    agsaRoot_t    *agRoot = &(tdsaAllShared->agRootInt);
//    agsaControllerStatus_t RegDump;
    bit32               Offset = 0;
    bit32               RequestLength = 0;  /* user request on how much data to pass to application */
    agsaRegDumpInfo_t   regDumpInfo;
    void                *buffer = agNULL;
    void                *osMemHandle = agNULL;
    bit32               status = IOCTL_CALL_SUCCESS;
    bit32               CoreDumpLength = 16384; /* change it once data is available */
    bit32               EventLogOffset = 65536;

    ///saGetControllerStatus(agRoot, &RegDump);
    /* length of FSA as provided by application */
    RequestLength = agIOCTLPayload->Length;
///    FunctionSpecificOffset = 0; /* Offset into the FunctionSpecificArea of payload */
    /* offset into core dump that was passed from application */
    Offset = agIOCTLPayload->Reserved;

  if((CoreDumpLength <= Offset)&&
    (agIOCTLPayload->MinorFunction != IOCTL_MN_FW_GET_EVENT_FLASH_LOG1)&&
    (agIOCTLPayload->MinorFunction != IOCTL_MN_FW_GET_EVENT_FLASH_LOG2))
  {
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_NO_MORE_DATA;
      agIOCTLPayload->Length = 0;
      status=IOCTL_CALL_SUCCESS;
      return status;
    }
    regDumpInfo.regDumpOffset = Offset;
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
    /* dump either aap1 or iop registers */
    switch(agIOCTLPayload->MinorFunction){

    /*Coredump*/
    case IOCTL_MN_FW_GET_CORE_DUMP_AAP1:
            //CoreDumpBAROffset = RegDump.fatalErrorInfo.regDumpOffset0;    /* get this from mpi config table */
            //CoreDumpLength = RegDump.fatalErrorInfo.regDumpLen0;
            /*changes for added Call back*/
            tdsaAllShared->tdFWControlEx.param1 = agParam1;
            tdsaAllShared->tdFWControlEx.param2 = agParam2;
            regDumpInfo.regDumpSrc = 0;
            regDumpInfo.regDumpNum = 0;
            regDumpInfo.directLen = RequestLength;
            regDumpInfo.directData = &agIOCTLPayload->FunctionSpecificArea[0];
            /*changes for added Call back*/
            //status = IOCTL_CALL_SUCCESS;
            tdsaAllShared->tdFWControlEx.inProgress = 1;
            status = IOCTL_CALL_PENDING;
            break;
    case IOCTL_MN_FW_GET_CORE_DUMP_IOP:
        //CoreDumpBAROffset = RegDump.fatalErrorInfo.regDumpOffset1;    /* get this from mpi config table */
        //CoreDumpLength = RegDump.fatalErrorInfo.regDumpLen1;
        /*changes for added Call back*/
        tdsaAllShared->tdFWControlEx.param1 = agParam1;
        tdsaAllShared->tdFWControlEx.param2 = agParam2;
        regDumpInfo.regDumpSrc = 0;
        regDumpInfo.regDumpNum = 1;
        regDumpInfo.directLen = RequestLength;
        regDumpInfo.directData = &agIOCTLPayload->FunctionSpecificArea[0];
        /*changes for added Call back*/
        //status = IOCTL_CALL_SUCCESS;
        tdsaAllShared->tdFWControlEx.inProgress = 1;
        status = IOCTL_CALL_PENDING;
        break;
    case IOCTL_MN_FW_GET_CORE_DUMP_FLASH_AAP1:
        regDumpInfo.regDumpSrc = 1;
        regDumpInfo.regDumpNum = 0;
        if(RequestLength != 0)
        {
            if(ostiAllocMemory( tiRoot,
                    &osMemHandle,
                    (void **)&buffer,
                    &(regDumpInfo.indirectAddrUpper32),
                    &(regDumpInfo.indirectAddrLower32),
                    8,
                    RequestLength,
                    agFALSE))
                return IOCTL_CALL_FAIL;
        }
        osti_memset((void *)buffer, 0, RequestLength);
        regDumpInfo.indirectLen = RequestLength;

        // use FW control place in shared structure to keep the neccesary information
        tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
        tdsaAllShared->tdFWControlEx.virtAddr = buffer;
        tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
        tdsaAllShared->tdFWControlEx.len = RequestLength;
        tdsaAllShared->tdFWControlEx.param1 = agParam1;
        tdsaAllShared->tdFWControlEx.param2 = agParam2;
        tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
        tdsaAllShared->tdFWControlEx.inProgress = 1;
        status = IOCTL_CALL_PENDING;
        break;
    case IOCTL_MN_FW_GET_CORE_DUMP_FLASH_IOP:
        regDumpInfo.regDumpSrc = 1;
        regDumpInfo.regDumpNum = 1;
        if(RequestLength != 0)
        {
            if(ostiAllocMemory( tiRoot,
                    &osMemHandle,
                    (void **)&buffer,
                    &(regDumpInfo.indirectAddrUpper32),
                    &(regDumpInfo.indirectAddrLower32),
                    8,
                    RequestLength,
                    agFALSE))
                return IOCTL_CALL_FAIL;
        }
        osti_memset((void *)buffer, 0, RequestLength);
        regDumpInfo.indirectLen = RequestLength;

        // use FW control place in shared structure to keep the neccesary information
        tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
        tdsaAllShared->tdFWControlEx.virtAddr = buffer;
        tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
        tdsaAllShared->tdFWControlEx.len = RequestLength;
        tdsaAllShared->tdFWControlEx.param1 = agParam1;
        tdsaAllShared->tdFWControlEx.param2 = agParam2;
        tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
        tdsaAllShared->tdFWControlEx.inProgress = 1;
        status = IOCTL_CALL_PENDING;
        break;
    /*EventLog from Flash*/
    case IOCTL_MN_FW_GET_EVENT_FLASH_LOG1:      //aap1 Eventlog
      if(CoreDumpLength + EventLogOffset <= Offset)
      {
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_NO_MORE_DATA;
        agIOCTLPayload->Length = 0;
        status=IOCTL_CALL_SUCCESS;
        return status;
      }
      regDumpInfo.regDumpSrc = 1;
      regDumpInfo.regDumpNum = 0;
      if(RequestLength != 0)
      {
          if(ostiAllocMemory( tiRoot,
                  &osMemHandle,
                  (void **)&buffer,
                  &(regDumpInfo.indirectAddrUpper32),
                  &(regDumpInfo.indirectAddrLower32),
                  8,
                  RequestLength,
                  agFALSE))
              return IOCTL_CALL_FAIL;
      }
      osti_memset((void *)buffer, 0, RequestLength);
      regDumpInfo.indirectLen = RequestLength;

      // use FW control place in shared structure to keep the neccesary information
      tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
      tdsaAllShared->tdFWControlEx.virtAddr = buffer;
      tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
      tdsaAllShared->tdFWControlEx.len = RequestLength;
      tdsaAllShared->tdFWControlEx.param1 = agParam1;
      tdsaAllShared->tdFWControlEx.param2 = agParam2;
      tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
      tdsaAllShared->tdFWControlEx.inProgress = 1;
      status = IOCTL_CALL_PENDING;
      break;
    case IOCTL_MN_FW_GET_EVENT_FLASH_LOG2:      //iop Eventlog
      if(CoreDumpLength + EventLogOffset <= Offset)
      {
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_NO_MORE_DATA;
        agIOCTLPayload->Length = 0;
        status=IOCTL_CALL_SUCCESS;
        return status;
      }
      regDumpInfo.regDumpSrc = 1;
      regDumpInfo.regDumpNum = 1;
      if(RequestLength != 0)
      {
          if(ostiAllocMemory( tiRoot,
                  &osMemHandle,
                  (void **)&buffer,
                  &(regDumpInfo.indirectAddrUpper32),
                  &(regDumpInfo.indirectAddrLower32),
                  8,
                  RequestLength,
                  agFALSE))
              return IOCTL_CALL_FAIL;
      }
      osti_memset((void *)buffer, 0, RequestLength);
      regDumpInfo.indirectLen = RequestLength;

      // use FW control place in shared structure to keep the neccesary information
      tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
      tdsaAllShared->tdFWControlEx.virtAddr = buffer;
      tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
      tdsaAllShared->tdFWControlEx.len = RequestLength;
      tdsaAllShared->tdFWControlEx.param1 = agParam1;
      tdsaAllShared->tdFWControlEx.param2 = agParam2;
      tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
      tdsaAllShared->tdFWControlEx.inProgress = 1;
      status = IOCTL_CALL_PENDING;
      break;
  default:
      status = IOCTL_CALL_INVALID_CODE;
      TI_DBG1(("tiCOMMgntIOCTL: ERROR: Wrong IOCTL code %d\n", agIOCTLPayload->MinorFunction));
      break;
    }
    if(saGetRegisterDump(agRoot, agNULL, 0, &regDumpInfo) != AGSA_RC_SUCCESS)
    {
        status = IOCTL_CALL_FAIL;
        agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
    }

    return status;
}

osGLOBAL void
ostiCOMMgntVPDSetIOCTLRsp(
                          tiRoot_t            *tiRoot,
                          bit32               status
                          )
{
    tdsaRoot_t    *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
//    agsaRoot_t    *agRoot = &(tdsaAllShared->agRootInt);

    TI_DBG1(("ostiCOMMgntVPDSetIOCTLRsp: start\n"));
    (tdsaAllShared->tdFWControlEx.tdFWControl)->retcode = status;

    ostiFreeMemory(tiRoot,
                   tdsaAllShared->tdFWControlEx.buffer,
                   tdsaAllShared->tdFWControlEx.len);

    ostiIOCTLSetSignal(tiRoot, tdsaAllShared->tdFWControlEx.param1,
                               tdsaAllShared->tdFWControlEx.param2,
                               NULL);
}

/*****************************************************************************
*
* tdsaNVMDSetIoctl
*
* Purpose:  This routine is called to set Config. SEEPROM information.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaNVMDSetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                )
{
  bit32                  RequestLength = 0;
  bit32                  bufAddrUpper = 0;
  bit32                  bufAddrLower = 0;
  tdsaRoot_t             *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t          *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t             *agRoot = &(tdsaAllShared->agRootInt);
  void                   *buffer = agNULL;
  void                   *osMemHandle = agNULL;
  bit32                  status = IOCTL_CALL_SUCCESS;
  agsaNVMDData_t         nvmdInfo;


  TI_DBG2(("tdsaNVMDSetIoctl: start\n"));

  RequestLength = agIOCTLPayload->Length;

  osti_memset(&nvmdInfo, 0, sizeof(agsaNVMDData_t));

  switch(agIOCTLPayload->MinorFunction)
  {
    case IOCTL_MN_NVMD_SET_CONFIG:

      //nvmdInfo.NVMDevice = 1;
      nvmdInfo.NVMDevice = *((bit8*)agParam3);
      nvmdInfo.signature = 0xFEDCBA98;
      nvmdInfo.dataOffsetAddress = agIOCTLPayload->Reserved;
      nvmdInfo.indirectPayload = 1;
      nvmdInfo.indirectLen = RequestLength;

      if (nvmdInfo.NVMDevice == 0) {
        nvmdInfo.TWIDeviceAddress = 0xa0;
        nvmdInfo.TWIBusNumber = 0;
        nvmdInfo.TWIDevicePageSize = 0;
        nvmdInfo.TWIDeviceAddressSize = 1;
      }

      if(RequestLength != 0)
      {
        if(ostiAllocMemory( tiRoot,
            &osMemHandle,
            (void **)&buffer,
            &bufAddrUpper,
            &bufAddrLower,
            8,
            RequestLength,
            agFALSE))
          return IOCTL_CALL_FAIL;
      }
      else
      {
        return IOCTL_CALL_FAIL;
      }

      osti_memset((void *)buffer, 0, RequestLength);

      osti_memcpy((void *)buffer,
            agIOCTLPayload->FunctionSpecificArea,
            RequestLength);

      nvmdInfo.indirectAddrLower32 = bufAddrLower;
      nvmdInfo.indirectAddrUpper32 = bufAddrUpper;
      // use FW control place in shared structure to keep the neccesary information
      tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
      tdsaAllShared->tdFWControlEx.virtAddr = buffer;
      tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
      tdsaAllShared->tdFWControlEx.len = RequestLength;
      tdsaAllShared->tdFWControlEx.param1 = agParam1;
      tdsaAllShared->tdFWControlEx.param2 = agParam2;
      tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
      tdsaAllShared->tdFWControlEx.inProgress = 1;
      status = IOCTL_CALL_PENDING;
      break;
    default:
        status = IOCTL_CALL_INVALID_CODE;
        TI_DBG1(("tdsaNVMDSetIoctl: ERROR: Wrong IOCTL code %d\n", agIOCTLPayload->MinorFunction));
        break;
  }

  if(saSetNVMDCommand(agRoot, agNULL, 0, &nvmdInfo) != AGSA_RC_SUCCESS)
  {
      status = IOCTL_CALL_FAIL;
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
  }

  return status;

}

/*****************************************************************************
*
* tdsaNVMDGetIoctl
*
* Purpose:  This routine is called to get Config. SEEPROM information.
*           This function is used for both target and initiator.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaNVMDGetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                )
{
  tdsaRoot_t      *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t   *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t      *agRoot = &(tdsaAllShared->agRootInt);
  void            *buffer = agNULL;
  void            *osMemHandle = agNULL;
  bit32           status = IOCTL_CALL_SUCCESS;
  agsaNVMDData_t  nvmdInfo;
  bit32           Offset = 0;
  bit32           RequestLength = 0;
  bit32		  ostiMemoryStatus = 0;
  bit32		  i,j;
  bit8*		  seepromBuffer;
  bit8*		  phySettingsBuffer;


  TI_DBG2(("tdsaNVMDGetIoctl: start\n"));

  RequestLength = agIOCTLPayload->Length;
  Offset = agIOCTLPayload->Reserved;

  osti_memset(&nvmdInfo, 0, sizeof(agsaNVMDData_t));
  /* This condition is not valid for direct read so commenting */
  /*if(!tiIS_SPC(agRoot)) {
     if( RequestLength <= Offset ) //4096-max seeprom size
     {
    	agIOCTLPayload->Status = IOCTL_ERR_STATUS_NO_MORE_DATA;
    	agIOCTLPayload->Length = 0;
    	status=IOCTL_CALL_SUCCESS;
    	return status;
     }
  }*/

  agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;

  switch(agIOCTLPayload->MinorFunction)
  {
    case IOCTL_MN_NVMD_GET_CONFIG:

   //   nvmdInfo.NVMDevice = 1;
      nvmdInfo.NVMDevice = *((bit8*)agParam3);
      nvmdInfo.signature = 0xFEDCBA98;
      nvmdInfo.dataOffsetAddress = Offset;
      nvmdInfo.indirectPayload = 1;
      nvmdInfo.indirectLen = RequestLength;

      if (nvmdInfo.NVMDevice == 0) {
        nvmdInfo.TWIDeviceAddress = 0xa0;
        nvmdInfo.TWIBusNumber = 0;
        nvmdInfo.TWIDevicePageSize = 0;
        nvmdInfo.TWIDeviceAddressSize = 1;
      }

      if(RequestLength != 0)
      {
        ostiMemoryStatus = ostiAllocMemory( tiRoot,
            &osMemHandle,
            (void **)&buffer,
            &(nvmdInfo.indirectAddrUpper32),
            &(nvmdInfo.indirectAddrLower32),
            8,
            RequestLength,
            agFALSE);
	if((ostiMemoryStatus != tiSuccess) && (buffer == agNULL))
        return IOCTL_CALL_FAIL;
     }
      else
      {
        return IOCTL_CALL_FAIL;
      }
      osti_memset((void *)buffer, 0, RequestLength);

      // use FW control place in shared structure to keep the neccesary information
      tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
      tdsaAllShared->tdFWControlEx.virtAddr = buffer;
      tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
      tdsaAllShared->tdFWControlEx.len = RequestLength;
      tdsaAllShared->tdFWControlEx.param1 = agParam1;
      tdsaAllShared->tdFWControlEx.param2 = agParam2;
      tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
      tdsaAllShared->tdFWControlEx.inProgress = 1;
      status = IOCTL_CALL_PENDING;
      break;
      default:
      status = IOCTL_CALL_INVALID_CODE;
      TI_DBG1(("tiCOMMgntIOCTL: ERROR: Wrong IOCTL code %d\n", agIOCTLPayload->MinorFunction));
      break;
  }
  tdsaAllShared->NvmdResponseSet = 0;

  if(saGetNVMDCommand(agRoot, agNULL, 0, &nvmdInfo) != AGSA_RC_SUCCESS)
  {
    status = IOCTL_CALL_FAIL;
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
    return status;
  }
  /* Copy the SAS address */
  if(agParam1 == agNULL)
 
  {
     while(!tdsaAllShared->NvmdResponseSet)
     {
   //	tiCOMDelayedInterruptHandler(tiRoot, 0, 1, tiNonInterruptContext);
     }
     if(nvmdInfo.NVMDevice == 4 || nvmdInfo.NVMDevice == 1)
     {
	seepromBuffer = buffer;
	/*Get Initiator SAS address*/

	if(tiIS_SPC(agRoot))
	{
	   for(j=0,i=ADAPTER_WWN_SPC_START_OFFSET; i<= ADAPTER_WWN_SPC_END_OFFSET; i++,j++)
	  agIOCTLPayload->FunctionSpecificArea[j] = seepromBuffer[i];
        }
 	else
	{
	  for(j=0,i=ADAPTER_WWN_START_OFFSET; i<= ADAPTER_WWN_END_OFFSET; i++,j++)
	  agIOCTLPayload->FunctionSpecificArea[j] = seepromBuffer[i];
	}
    }
    /* Copy the Phy settings */
    else if(nvmdInfo.NVMDevice == 6)
    {
      phySettingsBuffer = buffer;
      for(i=0; i<PHY_SETTINGS_LEN; i++)
	agIOCTLPayload->FunctionSpecificArea[i] = phySettingsBuffer[i];
    }
    tdsaAllShared->NvmdResponseSet = 0;
    ostiFreeMemory(tiRoot, tdsaAllShared->tdFWControlEx.buffer, tdsaAllShared->tdFWControlEx.len);

  }
  return status;

}

/*****************************************************************************
*
* tdsaDeviceInfoGetIoctl
*
* Purpose:  This routine is called to get the specified device information.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaDeviceInfoGetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                )
{
  tdsaDeviceData_t       *oneDeviceData = agNULL;
  tiDeviceHandle_t       *tiDeviceHandle = agNULL;
  tdDeviceInfoPayload_t  *pDeviceInfo = agNULL;
  /*agsaDevHandle_t  *agDevHandle = agNULL;*/
  bit32            status = IOCTL_CALL_SUCCESS;

  pDeviceInfo = (tdDeviceInfoPayload_t*)agIOCTLPayload->FunctionSpecificArea;

  TI_DBG3(("tdsaDeviceInfoGetIoctl: %d:%3d:%d %p %p %p\n",
                                     (bit8)pDeviceInfo->PathId,
                                     (bit8)pDeviceInfo->TargetId,
                                     (bit8)pDeviceInfo->Lun,
                                      agParam1,
                                      agParam2,
                                      agParam3));

  tiDeviceHandle = ostiMapToDevHandle(tiRoot,
                                     (bit8)pDeviceInfo->PathId,
                                     (bit8)pDeviceInfo->TargetId,
                                     (bit8)pDeviceInfo->Lun
                                     );

  if (tiDeviceHandle == agNULL)
  {
    TI_DBG1(("tdsaDeviceInfoGetIoctl: tiDeviceHandle is NULL !!!! SCSI address = %d:%3d:%d\n",
              pDeviceInfo->PathId, pDeviceInfo->TargetId, pDeviceInfo->Lun));
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_INVALID_DEVICE;
    status = IOCTL_CALL_FAIL;
    return status;
  }

  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  if(oneDeviceData == agNULL)
  {
    TI_DBG1(("tdsaDeviceInfoGetIoctl: tiDeviceHandle=%p DeviceData is NULL!!! SCSI address = %d:%3d:%d\n",
             tiDeviceHandle, pDeviceInfo->PathId, pDeviceInfo->TargetId, pDeviceInfo->Lun));
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_INVALID_DEVICE;
    status = IOCTL_CALL_FAIL;
    return status;
  }

  /* for hotplug */
  if (oneDeviceData->valid != agTRUE || oneDeviceData->registered != agTRUE ||
      oneDeviceData->tdPortContext == agNULL )
  {
    TI_DBG1(("tdsaDeviceInfoGetIoctl: tiDeviceHandle=%p did %d DeviceData was removed!!! SCSI address = %d:%3d:%d\n",
             tiDeviceHandle, oneDeviceData->id, pDeviceInfo->PathId, pDeviceInfo->TargetId, pDeviceInfo->Lun));
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_INVALID_DEVICE;
    status = IOCTL_CALL_FAIL;
    return status;
  }

  /* fill the device information in IOCTL payload */
  pDeviceInfo->devInfo.phyId = oneDeviceData->phyID;
  osti_memcpy(&pDeviceInfo->devInfo.sasAddressHi, oneDeviceData->agDeviceInfo.sasAddressHi, sizeof(bit32));
  osti_memcpy(&pDeviceInfo->devInfo.sasAddressLo, oneDeviceData->agDeviceInfo.sasAddressLo, sizeof(bit32));

  pDeviceInfo->devInfo.sasAddressHi = DMA_BEBIT32_TO_BIT32(pDeviceInfo->devInfo.sasAddressHi);
  pDeviceInfo->devInfo.sasAddressLo = DMA_BEBIT32_TO_BIT32(pDeviceInfo->devInfo.sasAddressLo);

  pDeviceInfo->devInfo.deviceType = (oneDeviceData->agDeviceInfo.devType_S_Rate & 0x30) >> 4;
  pDeviceInfo->devInfo.linkRate   = oneDeviceData->agDeviceInfo.devType_S_Rate & 0x0F;

  agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;

  TI_DBG3(("tdsaDeviceInfoGetIoctl:IOCTL_CALL_SUCCESS\n"));

  /*saGetDeviceInfo(agRoot, agNULL, 0, 0, agDevHandle);*/

  status = IOCTL_CALL_SUCCESS;

  return status;
}
/*****************************************************************************
*
* tdsaIoErrorStatisticGetIoctl
*
* Purpose:  This routine is called to get the IO error statistic.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaIoErrorStatisticGetIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                  *agRoot          = &(tdsaAllShared->agRootInt);
  tdIoErrorStatisticPayload_t *pIoErrorPayload = agNULL;
  bit32                        status = IOCTL_CALL_SUCCESS;

  pIoErrorPayload = (tdIoErrorStatisticPayload_t*)agIOCTLPayload->FunctionSpecificArea;

  tdsaAllShared->tdFWControlEx.buffer = agNULL;
  tdsaAllShared->tdFWControlEx.virtAddr = agNULL;
  tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
  tdsaAllShared->tdFWControlEx.len = 0;
  tdsaAllShared->tdFWControlEx.param1 = agParam1;
  tdsaAllShared->tdFWControlEx.param2 = agParam2;
  tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
  tdsaAllShared->tdFWControlEx.inProgress = 0;

  saGetIOErrorStats(agRoot, agNULL, pIoErrorPayload->flag);

  return status;
}

/*****************************************************************************
*
* tdsaIoEventStatisticGetIoctl
*
* Purpose:  This routine is called to get the IO event statistic.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaIoEventStatisticGetIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                  *agRoot          = &(tdsaAllShared->agRootInt);
  tdIoEventStatisticPayload_t *pIoEventPayload = agNULL;
  bit32                        status = IOCTL_CALL_SUCCESS;

  pIoEventPayload = (tdIoEventStatisticPayload_t*)agIOCTLPayload->FunctionSpecificArea;

  tdsaAllShared->tdFWControlEx.buffer = agNULL;
  tdsaAllShared->tdFWControlEx.virtAddr = agNULL;
  tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
  tdsaAllShared->tdFWControlEx.len = 0;
  tdsaAllShared->tdFWControlEx.param1 = agParam1;
  tdsaAllShared->tdFWControlEx.param2 = agParam2;
  tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
  tdsaAllShared->tdFWControlEx.inProgress = 0;

  saGetIOEventStats(agRoot, agNULL, pIoEventPayload->flag);

  return status;
}

/*****************************************************************************
*
* tdsaRegisterIoctl
*
* Purpose:  This routine is called to get Forensic Data.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaRegisterIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
//  agsaRoot_t                  *agRoot          = &(tdsaAllShared->agRootInt);
  tdRegisterPayload_t         *pRegisterPayload = agNULL;
  bit32                        status = IOCTL_CALL_SUCCESS;

  pRegisterPayload = (tdRegisterPayload_t*)agIOCTLPayload->FunctionSpecificArea;

  tdsaAllShared->tdFWControlEx.buffer = agNULL;
  tdsaAllShared->tdFWControlEx.virtAddr = agNULL;
  tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
  tdsaAllShared->tdFWControlEx.len = 0;
  tdsaAllShared->tdFWControlEx.param1 = agParam1;
  tdsaAllShared->tdFWControlEx.param2 = agParam2;
  tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
  tdsaAllShared->tdFWControlEx.inProgress = 0;

  TI_DBG1(("tdsaRegisterIoctl: Flag %d RegAddr 0x%x RegValue 0x%x\n",
            pRegisterPayload->flag, pRegisterPayload->RegAddr, pRegisterPayload->RegValue));

  if (pRegisterPayload->flag)
  {
    /* set register */
    ostiChipWriteBit32Ext(tiRoot, 0, pRegisterPayload->RegAddr, pRegisterPayload->RegValue);
  }
  else
  {
    /* get register */
    pRegisterPayload->RegValue = ostiChipReadBit32Ext(tiRoot, 0, pRegisterPayload->RegAddr);
  }
  agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
  return status;
}

osGLOBAL bit32
tdsaGetPhyGeneralStatusIoctl(
				tiRoot_t			      *tiRoot,
				agsaPhyGeneralState_t     *PhyData
				)
{
  tdsaRoot_t				*tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t 			*tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t				*agRoot = &(tdsaAllShared->agRootNonInt);
//  agsaLLRoot_t	            *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
//  bit8                      totalValidPhys;
  bit32 					 status = AGSA_RC_SUCCESS;
  bit32                      i = 0;
  agsaControllerInfo_t ControllerInfo;
  saGetControllerInfo(agRoot,&ControllerInfo);

  TI_DBG3(("tdsaGetPhyGeneralStatusIoctl: start\n"));
  do
  {
    if(tIsSPC(agRoot)||tIsSPCHIL(agRoot))
    {
  	    status = IOCTL_ERR_STATUS_NOT_SUPPORTED;
		break;
    }
	
    PhyData->Reserved1 = ControllerInfo.phyCount;
    for(i=0;i<PhyData->Reserved1;i++)
    {
      status = saGetPhyProfile( agRoot,agNULL,tdsaRotateQnumber(tiRoot, agNULL), AGSA_SAS_PHY_GENERAL_STATUS_PAGE,i);
      if(status == AGSA_RC_FAILURE)
	  {
	    break;
	  }
    }
  }while(0);
  TI_DBG3(("tdsaGetPhyGeneralStatusIoctl: End\n"));
  return status;
}
/*****************************************************************************
*
* ostiGetPhyGeneralStatusRsp
*
* Purpose:  This routine is called when a PhyStatus IOCTL response is received.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agsaSASPhyGeneralStatusPage_t:   Status of the phy.
*   bit32:          phyID
*
* Return: none
*
*
*****************************************************************************/
osGLOBAL void ostiGetPhyGeneralStatusRsp(
                            tiRoot_t                      *tiRoot,
                        	agsaSASPhyGeneralStatusPage_t *GenStatus,
                        	bit32                          phyID
                            )
{

  tdsaRoot_t               *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t            *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tiIOCTLPayload_t         *agIoctlPayload = agNULL;
  agsaPhyGeneralState_t    *pSetPhyStatusRes = agNULL;

                   
  TI_DBG1(("ostiGetPhyGeneralStatusRsp: start\n"));

  if (tdsaAllShared->tdFWControlEx.inProgress) 
  {
      agIoctlPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
      if ((agIoctlPayload) && (PMC_IOCTL_SIGNATURE == agIoctlPayload->Signature)&& 
	  	                 (IOCTL_MJ_PHY_GENERAL_STATUS == agIoctlPayload->MajorFunction))
      {
        pSetPhyStatusRes = (agsaPhyGeneralState_t*) &agIoctlPayload->FunctionSpecificArea[0];
		osti_memcpy(&pSetPhyStatusRes->PhyGenData[phyID], GenStatus, sizeof(agsaSASPhyGeneralStatusPage_t));
		pSetPhyStatusRes->Reserved2++;
        if(pSetPhyStatusRes->Reserved1 == pSetPhyStatusRes->Reserved2)
        {
  		  tdsaAllShared->tdFWControlEx.payload = NULL; 
          ostiIOCTLSetSignal(tiRoot, tdsaAllShared->tdFWControlEx.param1,
                          tdsaAllShared->tdFWControlEx.param2, agNULL);
	  tdsaAllShared->tdFWControlEx.inProgress = 0;
          agIoctlPayload->Status = IOCTL_ERR_STATUS_OK;
		
        }
  	  } 
  }

  TI_DBG1(("ostiGetPhyGeneralStatusRsp: end\n"));
}


osGLOBAL bit32
tdsaPhyProfileIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 )
{
  tdsaRoot_t       *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t    *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t       *agRoot          = &(tdsaAllShared->agRootInt);
  void             *buffer = agNULL;
  void             *osMemHandle = agNULL;
  bit32            status = IOCTL_CALL_SUCCESS;
  bit32            retcode = AGSA_RC_FAILURE;
  bit32            RequestLength= agIOCTLPayload->Length;
  bit32 	   bufAddrUpper = 0;
  bit32 	   bufAddrLower = 0;

  tdPhyCount_t     *PhyBlob = (tdPhyCount_t*)&agIOCTLPayload->FunctionSpecificArea[0];


  if(ostiAllocMemory( tiRoot,
      &osMemHandle,
      (void **)&buffer,
      &bufAddrUpper,
      &bufAddrLower,
      RequestLength,
      RequestLength,
      agTRUE))
    return IOCTL_CALL_FAIL;


  tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
  tdsaAllShared->tdFWControlEx.virtAddr = buffer;
  tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
  tdsaAllShared->tdFWControlEx.len = 32;
  tdsaAllShared->tdFWControlEx.param1 = agParam1;
  tdsaAllShared->tdFWControlEx.param2 = agParam2;
  tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
  tdsaAllShared->tdFWControlEx.inProgress = 1;

  TI_DBG1(("tdsaPhyProfileIoctl: MinorFunction %d\n",agIOCTLPayload->MinorFunction));
//  PhyBlob->Phy |= 0x100;

  if( tiIS_SPC(agRoot) )
  {
    TI_DBG1(("tdsaPhyProfileIoctl: SPC operation 0x%x PHY %d\n",agIOCTLPayload->MinorFunction,PhyBlob->Phy));
    retcode = saLocalPhyControl(agRoot,agNULL,0 ,PhyBlob->Phy ,agIOCTLPayload->MinorFunction , agNULL);
    if(retcode ==  AGSA_RC_SUCCESS)
    {
      status = IOCTL_CALL_PENDING;
    }
  }
  else
  {
    TI_DBG1(("tdsaPhyProfileIoctl: SPCv operation 0x%x PHY %d\n",agIOCTLPayload->MinorFunction,PhyBlob->Phy));
    retcode = saGetPhyProfile( agRoot,agNULL,0,agIOCTLPayload->MinorFunction , PhyBlob->Phy);

    if(retcode ==  AGSA_RC_SUCCESS)
    {
      status = IOCTL_CALL_PENDING;
    }

  }

  TI_DBG2(("tdsaPhyProfileIoctl: after\n"));


  return status;
}

/*****************************************************************************
*
* tdsaForensicDataGetIoctl
*
* Purpose:  This routine is called to get Forensic Data.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaForensicDataGetIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                  *agRoot          = &(tdsaAllShared->agRootInt);
  tdForensicDataPayload_t     *pForensicDataPayload = agNULL;
  agsaForensicData_t           ForensicData;
  bit32                        status = IOCTL_CALL_SUCCESS;

  pForensicDataPayload = (tdForensicDataPayload_t*)agIOCTLPayload->FunctionSpecificArea;

  tdsaAllShared->tdFWControlEx.buffer = agNULL;
  tdsaAllShared->tdFWControlEx.virtAddr = agNULL;
  tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
  tdsaAllShared->tdFWControlEx.len = 0;
  tdsaAllShared->tdFWControlEx.param1 = agParam1;
  tdsaAllShared->tdFWControlEx.param2 = agParam2;
  tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
  tdsaAllShared->tdFWControlEx.inProgress = 0;

  osti_memset(&ForensicData, 0, sizeof(agsaForensicData_t));

  ForensicData.DataType = pForensicDataPayload->DataType;

  switch (ForensicData.DataType)
  {
    case FORENSIC_DATA_TYPE_NON_FATAL:
    case FORENSIC_DATA_TYPE_FATAL:
         ForensicData.BufferType.dataBuf.directLen = pForensicDataPayload->dataBuffer.directLen;
         ForensicData.BufferType.dataBuf.directOffset = pForensicDataPayload->dataBuffer.directOffset;
         ForensicData.BufferType.dataBuf.readLen = pForensicDataPayload->dataBuffer.readLen;
         ForensicData.BufferType.dataBuf.directData = (void*)pForensicDataPayload->dataBuffer.directData;         
		 break;
    case FORENSIC_DATA_TYPE_GSM_SPACE:
         ForensicData.BufferType.gsmBuf.directLen = pForensicDataPayload->gsmBuffer.directLen;
         ForensicData.BufferType.gsmBuf.directOffset = pForensicDataPayload->gsmBuffer.directOffset;
         ForensicData.BufferType.dataBuf.readLen      = pForensicDataPayload->gsmBuffer.readLen;
         ForensicData.BufferType.gsmBuf.directData = (void*)pForensicDataPayload->gsmBuffer.directData;
         break;

    case FORENSIC_DATA_TYPE_IB_QUEUE:
         ForensicData.BufferType.queueBuf.directLen = pForensicDataPayload->queueBuffer.directLen;
         //ForensicData.BufferType.queueBuf.queueType = pForensicDataPayload->queueBuffer.queueType;
         ForensicData.BufferType.queueBuf.queueType = FORENSIC_DATA_TYPE_IB_QUEUE;
         ForensicData.BufferType.queueBuf.queueIndex = pForensicDataPayload->queueBuffer.queueIndex;
         ForensicData.BufferType.queueBuf.directData = (void*)pForensicDataPayload->queueBuffer.directData;
         break;
    case FORENSIC_DATA_TYPE_OB_QUEUE:
         ForensicData.BufferType.queueBuf.directLen = pForensicDataPayload->queueBuffer.directLen;
         ForensicData.BufferType.queueBuf.queueType = FORENSIC_DATA_TYPE_OB_QUEUE;
         ForensicData.BufferType.queueBuf.queueIndex = pForensicDataPayload->queueBuffer.queueIndex;
         ForensicData.BufferType.queueBuf.directData = (void*)pForensicDataPayload->queueBuffer.directData;
         break;

    default:
         TI_DBG1(("tdsaGetForensicDataIoctl: forensic data type error %d\n", pForensicDataPayload->DataType));
         status = IOCTL_CALL_INVALID_CODE;
         return status;
  }

  if ( saGetForensicData(agRoot, agNULL, &ForensicData) != AGSA_RC_SUCCESS )
  {
    status = IOCTL_CALL_FAIL;
  }

  return status;
}

osGLOBAL bit32
tdsaSendSMPIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                )
{
	tdsaRoot_t		*tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
	tdsaContext_t	*tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
	agsaRoot_t		*agRoot = &(tdsaAllShared->agRootInt);
	void			*reqBuffer = agNULL;
	void			*respBuffer = agNULL;
	void			*osMemHandle = agNULL;
	bit32			status = IOCTL_CALL_SUCCESS;
//	bit32			Offset = 0;
//	bit32			RequestLength = 0;
	bit32			ostiMemoryStatus = 0;
	smp_pass_through_req_t *smp_pass_through_req;
	
	tiDeviceHandle_t *devHandle;
	agsaSMPFrame_t			  agSMPFrame;
	tdsaDeviceData_t          *oneDeviceData = agNULL;
	bit32 i;
	
	TI_DBG2(("tdsaSendSMPIoctl: start\n"));
	
 	smp_pass_through_req = (smp_pass_through_req_t*)agIOCTLPayload->FunctionSpecificArea;

	for(i=0;i<8;i++)
		TI_DBG2(("SAS Address[%d]:%x",i,smp_pass_through_req->exp_sas_addr[i]));
	TI_DBG2(("SAS Request Length:%d",smp_pass_through_req->smp_req_len));
	TI_DBG2(("SAS Response Length:%d",smp_pass_through_req->smp_resp_len));
	for(i=0;i<smp_pass_through_req->smp_req_len;i++)
		TI_DBG2(("SAS request + %d:%x",i,smp_pass_through_req->smp_req_resp[i]));

	devHandle = ostiGetDevHandleFromSasAddr(tiRoot, smp_pass_through_req->exp_sas_addr);
	if(devHandle == NULL)
	{
		status = IOCTL_CALL_FAIL;
		agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
		return status;
	}
	

	
	//agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
	agIOCTLPayload->Status = IOCTL_ERR_STATUS_NOT_RESPONDING;
	


	if((ostiMemoryStatus != tiSuccess) && (reqBuffer == agNULL  ))
			return IOCTL_CALL_FAIL;
		
   
    tdsaAllShared->tdFWControlEx.param3 = osMemHandle;
	

	agSMPFrame.outFrameBuf = smp_pass_through_req->smp_req_resp;
	agSMPFrame.expectedRespLen = smp_pass_through_req->smp_resp_len;
	agSMPFrame.inFrameLen = smp_pass_through_req->smp_resp_len - 4;

	if(!(smp_pass_through_req->smp_req_len - 8) && !tiIS_SPC(agRoot))
	{
		agSMPFrame.flag = 1;  // Direct request Indirect response
		agSMPFrame.outFrameLen = smp_pass_through_req->smp_req_len - 4; //Exclude header
	}
	else
	{
	
		agSMPFrame.flag = 3;  //Indirect request and Indirect response
		ostiMemoryStatus = ostiAllocMemory( tiRoot,
										  &osMemHandle,
										  (void **)&reqBuffer,
										  &(agSMPFrame.outFrameAddrUpper32),
										  &(agSMPFrame.outFrameAddrLower32),
										  8,
										  smp_pass_through_req->smp_req_len,
										  agFALSE);
		tdsaAllShared->tdFWControlEx.param3 = osMemHandle;
		if(tiIS_SPC(agRoot))
		{
		  agSMPFrame.outFrameLen = smp_pass_through_req->smp_req_len - 4; //Exclude crc
		  osti_memcpy((void *)reqBuffer, (void *)(smp_pass_through_req->smp_req_resp), smp_pass_through_req->smp_req_len);
		}
		else
		{
		  agSMPFrame.outFrameLen = smp_pass_through_req->smp_req_len - 8; //Exclude header and crc
		  osti_memcpy((void *)reqBuffer, (void *)(smp_pass_through_req->smp_req_resp + 4), smp_pass_through_req->smp_req_len - 4);
		}
	}

	ostiMemoryStatus = ostiAllocMemory( tiRoot,
										  &osMemHandle,
										  (void **)&respBuffer,
										  &(agSMPFrame.inFrameAddrUpper32),
										  &(agSMPFrame.inFrameAddrLower32),
										  8,
										  smp_pass_through_req->smp_resp_len + 4,
										  agFALSE);
	if((ostiMemoryStatus != tiSuccess) && (respBuffer == agNULL  ))
			return IOCTL_CALL_FAIL;
		

	osti_memset((void *)respBuffer, 0, smp_pass_through_req->smp_resp_len);
	
		// use FW control place in shared structure to keep the neccesary information
	tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
	tdsaAllShared->tdFWControlEx.virtAddr = respBuffer;
	tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)smp_pass_through_req->smp_req_resp + smp_pass_through_req->smp_req_len;
	tdsaAllShared->tdFWControlEx.len = smp_pass_through_req->smp_resp_len;
	tdsaAllShared->tdFWControlEx.param1 = agParam1;
	tdsaAllShared->tdFWControlEx.param2 = agParam2;
	tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
	tdsaAllShared->tdFWControlEx.inProgress = 1;
	status = IOCTL_CALL_PENDING;

	oneDeviceData = (tdsaDeviceData_t *)devHandle->tdData;
	if(saSendSMPIoctl(agRoot, oneDeviceData->agDevHandle, 0, &agSMPFrame, &ossaSMPIoctlCompleted) != AGSA_RC_SUCCESS)
	{
	  status = IOCTL_CALL_FAIL;
	  agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
	}
	return status;
}

osGLOBAL void ostiSendSMPIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        )
{
    tdsaRoot_t           *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tiIOCTLPayload_t     *agIOCTLPayload;

    agIOCTLPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
    agIOCTLPayload->Status = (bit16)status;

    TI_DBG1(("ostiSendSMPIOCTLRsp: start, status = %d\n", status));

//	if(tdsaAllShared->tdFWControlEx.param1 != agNULL)
//	{
      osti_memcpy((void *)(tdsaAllShared->tdFWControlEx.usrAddr),
                  (void *)(tdsaAllShared->tdFWControlEx.virtAddr),
                  tdsaAllShared->tdFWControlEx.len);
//	}
	ostiFreeMemory(tiRoot,
                   tdsaAllShared->tdFWControlEx.buffer,
                   tdsaAllShared->tdFWControlEx.len);
	ostiFreeMemory(tiRoot,
                   tdsaAllShared->tdFWControlEx.param3,
                   tdsaAllShared->tdFWControlEx.len);
    //if(tdsaAllShared->tdFWControlEx.param1 != agNULL)
//	{
      ostiIOCTLComplete(tiRoot, 
                         tdsaAllShared->tdFWControlEx.param1,
                         tdsaAllShared->tdFWControlEx.param2,
                         NULL);
//    }
}




/*****************************************************************************
*
* tdsaSendBISTIoctl
*
* Purpose:  This routine is called to get Forensic Data.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   agIOCTLPayload: Pointer to the IOCTL payload.
*   agParam1:       Pointer to pass context handle for IOCTL DMA operation
*   agParam2:       Pointer to pass context handle for IOCTL DMA operation
*   agParam3:       Pointer to pass context handle for IOCTL DMA operation
*
* Return:
*
*   IOCTL_CALL_SUCCESS        The requested operation completed successfully.
*   IOCTL_CALL_FAIL           Fail to complete the IOCTL request.
*                             Detail error code is function specific and
*                             defined by the specific IOCTL function.
*   IOCTL_CALL_PENDING        This request is asynchronous and completed
*                             in some other context.
*   IOCTL_CALL_INVALID_CODE   This IOCTL function is not recognized.
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaSendBISTIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 )
{
  tdsaRoot_t      *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t   *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t      *agRoot          = &(tdsaAllShared->agRootInt);
  tdBistPayload_t *pBistPayload;
//  bit32            length = 0;
//  bit32            status = IOCTL_CALL_SUCCESS;
  bit32            status = IOCTL_CALL_FAIL;

  pBistPayload = (tdBistPayload_t*)agIOCTLPayload->FunctionSpecificArea;

  tdsaAllShared->tdFWControlEx.buffer = agNULL;
  tdsaAllShared->tdFWControlEx.virtAddr = agNULL;
  tdsaAllShared->tdFWControlEx.usrAddr = (bit8*)&agIOCTLPayload->FunctionSpecificArea[0];
  tdsaAllShared->tdFWControlEx.len = 0;
  tdsaAllShared->tdFWControlEx.param1 = agParam1;
  tdsaAllShared->tdFWControlEx.param2 = agParam2;
  tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
  tdsaAllShared->tdFWControlEx.inProgress = 0;

  TI_DBG1(("tdsaSendBISTIoctl: Type %d Length %d Data %p\n",
      pBistPayload->testType,
      pBistPayload->testLength,
      pBistPayload->testData ));


  // pBistPayload->testtype = AGSA_BIST_TEST;

  if( pBistPayload->testType == AGSA_BIST_TEST)
  {
    if( pBistPayload->testLength != sizeof(agsaEncryptSelfTestBitMap_t))
    {
      return status;
    }
  }
  else if( pBistPayload->testType == AGSA_SHA_TEST)
  {
    if( pBistPayload->testLength != sizeof(agsaEncryptSHATestDescriptor_t) )
    {
      return status;
    }
  }
  else if( pBistPayload->testType == AGSA_HMAC_TEST )
  {
    if( pBistPayload->testLength != sizeof(agsaEncryptHMACTestDescriptor_t))
    {
      return status;
    }
  }

/*
GLOBAL bit32 saEncryptSelftestExecute(
                        agsaRoot_t    *agRoot,
                        agsaContext_t *agContext,
                        bit32         queueNum,
                        bit32         type,
                        bit32         length,
                        void          *TestDescriptor);

*/
  if ( saEncryptSelftestExecute(agRoot,
        agNULL,
        0,
        pBistPayload->testType,
        pBistPayload->testLength,
        pBistPayload->testData  ) != AGSA_RC_SUCCESS )
  {
    status = IOCTL_CALL_FAIL;
  }

  return status;
}


osGLOBAL bit32
tdsaSendTMFIoctl( tiRoot_t	     	*tiRoot,
		  tiIOCTLPayload_t	*agIOCTLPayload,
		  void                  *agParam1,
		  void			*agParam2,
		  unsigned long		resetType
		)
{
	bit32		status;
	tmf_pass_through_req_t  *tmf_req = (tmf_pass_through_req_t*)agIOCTLPayload->FunctionSpecificArea;
#if !(defined(__FreeBSD__))
	status = ostiSendResetDeviceIoctl(tiRoot, agParam2, tmf_req->pathId, tmf_req->targetId, tmf_req->lun, resetType);
#endif
	TI_DBG3(("Status returned from ostiSendResetDeviceIoctl is %d\n",status));
	if(status !=  IOCTL_CALL_SUCCESS)
	{
		agIOCTLPayload->Status = status;
		return status;
	}
	status = IOCTL_CALL_SUCCESS;
	return status;
}


#ifdef VPD_TESTING
/* temporary to test saSetVPDCommand() and saGetVPDCommand */
osGLOBAL bit32
tdsaVPDSet(
                tiRoot_t            *tiRoot
                )
{
  tdsaRoot_t    *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t    *agRoot = &(tdsaAllShared->agRootInt);
  bit32         status = IOCTL_CALL_SUCCESS;
  agsaVPD_t     VPDInfo;
  bit32         ret = AGSA_RC_SUCCESS;

  bit32                 bufAddrUpper = 0;
  bit32                 bufAddrLower = 0;
  tdVPDControl_t        *VPDControl;

  void                  *osMemHandle = agNULL;
  void                  *buffer;
  bit32                 timeCount=0;
  bit8                  ioctlErr=0;
  bit8                  VPDPayload[32];
  bit8                  i;
  TI_DBG2(("tdsaVPDSet: start\n"));

  for(i=0;i<sizeof(VPDPayload);i++)
  {
    VPDPayload[i] = i;
  }
  if(ostiAllocMemory( tiRoot,
                        &osMemHandle,
                        (void **)&buffer,
                        &bufAddrUpper,
                        &bufAddrLower,
                        8,
                        sizeof(VPDPayload),
                        agFALSE))
  {
    return tiError;
  }
  osti_memcpy((void *)buffer,
               VPDPayload,
               sizeof(VPDPayload));


  osti_memset(&VPDInfo, 0, sizeof(agsaVPD_t));
#ifdef NOT_YET /* direct mode worked */
  /* For now, only direct mode */
  VPDInfo.indirectMode = 0; /* direct mode */
  VPDInfo.VPDDevice = 1; /* SEEPROM-1 */
  VPDInfo.directLen  = (bit8)sizeof(VPDPayload);
  VPDInfo.VPDOffset = 0;
  VPDInfo.directData = buffer;
  VPDInfo.indirectAddrUpper32 = bufAddrUpper;
  VPDInfo.indirectAddrLower32 = bufAddrLower;
  VPDInfo.indirectLen = sizeof(VPDPayload);
#endif

  /* indirect mode */
  VPDInfo.indirectMode = 1; /* indirect mode */
  VPDInfo.VPDDevice = 1; /* SEEPROM-1 */
  VPDInfo.directLen  = 0;
  VPDInfo.VPDOffset = 0;
  VPDInfo.directData = agNULL;
  VPDInfo.indirectAddrUpper32 = bufAddrUpper;
  VPDInfo.indirectAddrLower32 = bufAddrLower;
  VPDInfo.indirectLen = sizeof(VPDPayload);

  tdsaAllShared->tdFWControlEx.buffer = osMemHandle;
  tdsaAllShared->tdFWControlEx.param1 = agParam1;
  tdsaAllShared->tdFWControlEx.param2 = agParam2;

  /* for testing only */
  tdsaAllShared->addrUpper = bufAddrUpper;
  tdsaAllShared->addrLower = bufAddrLower;

  ret = saSetVPDCommand(agRoot, agNULL, 0, &VPDInfo);

  if (ret == AGSA_RC_SUCCESS)
  {
    status = tiSuccess;
  }
  else
  {
    status = tiError;
  }

    ostiFreeMemory(tiRoot, osMemHandle, sizeof(VPDPayload));
  return status;
}

/* temporary to test saSetVPDCommand() and saGetVPDCommand */
osGLOBAL bit32
tdsaVPDGet(tiRoot_t            *tiRoot)
{
  tdsaRoot_t    *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t    *agRoot = &(tdsaAllShared->agRootInt);
  bit32         status = IOCTL_CALL_SUCCESS;
  agsaVPD_t     VPDInfo;
  bit32         ret = AGSA_RC_SUCCESS;


  TI_DBG2(("tdsaVPDGet: start\n"));

  osti_memset(&VPDInfo, 0, sizeof(agsaVPD_t));

  /* direct mode worked */
  VPDInfo.indirectMode = 0; /* direct mode */
  VPDInfo.VPDDevice = 1; /* SEEPROM-1*/
  VPDInfo.directLen  = 32;
  VPDInfo.VPDOffset = 0;
  VPDInfo.directData = agNULL;
  VPDInfo.indirectAddrUpper32 = 0;
  VPDInfo.indirectAddrLower32 = 0;
  VPDInfo.indirectLen = 0;


#ifdef NOT_YET /* worked; can't read VPD in ossaGetVPDResponseCB() because of indirect */
  VPDInfo.indirectMode = 1; /* direct mode */
  VPDInfo.VPDDevice = 1; /* SEEPROM-1*/
  VPDInfo.directLen  = 0;
  VPDInfo.VPDOffset = 0;
  VPDInfo.directData = agNULL;
  VPDInfo.indirectAddrUpper32 = tdsaAllShared->addrUpper;
  VPDInfo.indirectAddrLower32 = tdsaAllShared->addrLower;
  VPDInfo.indirectLen = 32;
#endif
  ret = saGetVPDCommand(agRoot, agNULL, 0, &VPDInfo);

  if (ret == AGSA_RC_SUCCESS)
  {
    status = tiSuccess;
  }
  else
  {
    status = tiError;
  }
  return status;
}
#endif
/*****************************************************************************
*
* tdsaGetNumOfLUNIOCTL
*
* Purpose:  This routine is called to send Report LUN SSP command request.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   tiIOCTLPayload_t:        Status of the Controller Reset.
*   agParam1:        Void pointer to device extension
*   agParam2:        Void pointer to SRB
*   agParam3:        NULL
*
*   Return: status
*
*
*****************************************************************************/
osGLOBAL bit32
tdsaGetNumOfLUNIOCTL(
               tiRoot_t            *tiRoot,
               tiIOCTLPayload_t    *agIOCTLPayload,
               void                *agParam1,
               void                *agParam2,
               void                *agParam3
               )
{  
  tdsaRoot_t	              *tdsaRoot			= (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared 	= (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t	              *agRoot 			= &(tdsaAllShared->agRootInt);
  tdDeviceLUNInfoIOCTL_t	  *pDeviceLUNInfo	= agNULL;
  tiDeviceHandle_t            *devHandle 		= agNULL;
  void				          *tiRequestBody 	= agNULL;
  tiIORequest_t 	          *tiIORequest 		= agNULL;
  bit32			              status 			= IOCTL_CALL_SUCCESS;	
  
  TI_DBG2(("tdsaGetNumOfLUNIOCTL: Start\n"));  
  do
  {
    pDeviceLUNInfo = (tdDeviceLUNInfoIOCTL_t*)agIOCTLPayload->FunctionSpecificArea;
  
    if (agIOCTLPayload->Length < sizeof(tdDeviceLUNInfoIOCTL_t))
    {
  	  status = IOCTL_CALL_FAIL;
  	  break;
    }
    if(!pDeviceLUNInfo->tiDeviceHandle)
    {
      status = IOCTL_CALL_FAIL;
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
      break;
    }
	devHandle = (tiDeviceHandle_t*)pDeviceLUNInfo->tiDeviceHandle;
	agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;

	status = ostiNumOfLUNIOCTLreq(tiRoot,agParam1,agParam2,&tiRequestBody,&tiIORequest);

	
    if(status != AGSA_RC_SUCCESS) 	
    {
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
	  break;
    }
    status = tiNumOfLunIOCTLreq(tiRoot,tiIORequest,devHandle,tiRequestBody,agIOCTLPayload,agParam1,agParam2);
    
    if(status != AGSA_RC_SUCCESS)	
    {
         agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
	  break;
    }
//	ostiIOCTLWaitForSignal (tiRoot, agParam1, agParam2, agParam3);

  }while(0);
  TI_DBG2(("tdsaGetNumOfLUNIOCTL: End\n"));
  return status;
}


/*****************************************************************************
*
* ostiNumOfLUNIOCTLRsp
*
* Purpose:  This routine is called when a Report LUN SSP command response id recieved.
*
* Parameters:
*   tiRoot:         Pointer to driver instance
*   bit32               status
*
* Return: none
*
*
*****************************************************************************/
osGLOBAL void ostiNumOfLUNIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        )
{
  tdsaRoot_t                  *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tiIOCTLPayload_t            *agIOCTLPayload;
  tdDeviceLUNInfoIOCTL_t	  *pDeviceLUNInfo = NULL;
  bit32                       count = 0;
  bit32                       numOfLUN =0;
  
  TI_DBG1(("ostiNumOfLUNIOCTLRsp: start, status = %d\n", status));

  if(tdsaAllShared->tdFWControlEx.inProgress == 1)
  {
    agIOCTLPayload = (tiIOCTLPayload_t *)(tdsaAllShared->tdFWControlEx.payload);
	if ((agIOCTLPayload) && (PMC_IOCTL_SIGNATURE == agIOCTLPayload->Signature)&& 
					   (IOCTL_MJ_GET_DEVICE_LUN == agIOCTLPayload->MajorFunction))
	{
      agIOCTLPayload->Status = (bit16)status;
      pDeviceLUNInfo = (tdDeviceLUNInfoIOCTL_t*)agIOCTLPayload->FunctionSpecificArea;
      numOfLUN = ((tdsaAllShared->tdFWControlEx.virtAddr[0] << 24)|(tdsaAllShared->tdFWControlEx.virtAddr[1] << 16)|\
                 (tdsaAllShared->tdFWControlEx.virtAddr[2] << 8)|(tdsaAllShared->tdFWControlEx.virtAddr[3])); 
      numOfLUN = numOfLUN/8;
      pDeviceLUNInfo->numOfLun = numOfLUN;
//	  ostiFreeMemory(tiRoot,
//                     tdsaAllShared->tdFWControlEx.virtAddr,
//                     tdsaAllShared->tdFWControlEx.len);   
  //    if(tdsaAllShared->tdFWControlEx.param1 != agNULL)
  //    {
        ostiIOCTLSetSignal(tiRoot, 
                           tdsaAllShared->tdFWControlEx.param1,
                           tdsaAllShared->tdFWControlEx.param2,
                           NULL);
  	    tdsaAllShared->tdFWControlEx.payload = NULL; 	    
  //    }
	  
	  tdsaAllShared->tdFWControlEx.inProgress = 0;
	}
  }
  TI_DBG1(("ostiNumOfLUNIOCTLRsp: End\n"));
}

