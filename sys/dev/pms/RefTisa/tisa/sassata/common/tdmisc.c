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
 * This file contains TB misc. functions
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

/*****************************************************************************
*! \brief tiINIIOAbort
*
*  Purpose:  This function is called to abort an I/O request previously started
*             by a call to tiINIIOStart() or tiINIIOStartDif() .
*
*  \param  tiRoot:          Pointer to initiator driver/port instance.
*  \param  taskTag:         Pointer to the associated task to be aborted
*
*  \return: 
*
*          tiSuccess:     I/O request successfully initiated. 
*          tiBusy:        No resources available, try again later.
*          tiIONoDevice:  Invalid device handle.
*          tiError:       Other errors that prevent the I/O request to be
*                         started.
*
*****************************************************************************/
#ifdef INITIATOR_DRIVER							/*TBD: INITIATOR SPECIFIC API in tiapi.h (TP)*/
osGLOBAL bit32
tiINIIOAbort(
             tiRoot_t            *tiRoot,
             tiIORequest_t       *taskTag
             )
{
  tdsaRoot_t          *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t       *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t          *agRoot = agNULL;
  tdIORequestBody_t   *tdIORequestBody = agNULL;
  agsaIORequest_t     *agIORequest = agNULL;
  bit32               sasStatus = AGSA_RC_FAILURE;
  tdsaDeviceData_t    *oneDeviceData;
  bit32               status= tiError;
  agsaIORequest_t     *agAbortIORequest;  
  tdIORequestBody_t   *tdAbortIORequestBody;
  bit32               PhysUpper32;
  bit32               PhysLower32;
  bit32               memAllocStatus;
  void                *osMemHandle;
  agsaDevHandle_t     *agDevHandle = agNULL;
#ifdef FDS_SM
  smRoot_t                    *smRoot;
  tdIORequestBody_t           *ToBeAbortedtdIORequestBody;
  smIORequest_t               *ToBeAborted = agNULL;
#endif  
  TI_DBG2(("tiINIIOAbort: start\n"));

  if(taskTag == agNULL)
  {
    TI_DBG1(("tiINIIOAbort: taskTag is NULL\n"));
    return tiError;
  }

  agRoot          = &(tdsaAllShared->agRootNonInt);
  tdIORequestBody = (tdIORequestBody_t *)taskTag->tdData;
  agIORequest     = &(tdIORequestBody->agIORequest);
  oneDeviceData   = tdIORequestBody->tiDevHandle->tdData;
  
  if(oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINIIOAbort: DeviceData is NULL\n"));
    return tiSuccess;
  }
  
  agDevHandle = oneDeviceData->agDevHandle;
  
  TI_DBG2(("tiINIIOAbort: did %d\n", oneDeviceData->id));
  
  /* for hotplug */
  if (oneDeviceData->valid != agTRUE || oneDeviceData->registered != agTRUE ||
      oneDeviceData->tdPortContext == agNULL )
  {
    TI_DBG1(("tiINIIOAbort: NO Device did %d\n", oneDeviceData->id ));
    TI_DBG1(("tiINIIOAbort: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG1(("tiINIIOAbort: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
    return tiError;
  } 
  
  /* allocating agIORequest for abort itself */
  memAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &osMemHandle,
                                   (void **)&tdAbortIORequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(tdIORequestBody_t),
                                   agTRUE
                                   );
  if (memAllocStatus != tiSuccess)
  {
    /* let os process IO */
    TI_DBG1(("tiINIIOAbort: ostiAllocMemory failed...\n"));
    return tiError;
  }
      
  if (tdAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    TI_DBG1(("tiINIIOAbort: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
    return tiError;
  }

  /* setup task management structure */
  tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  /* setting callback */
  tdAbortIORequestBody->IOCompletionFunc = itdssIOAbortedHandler;  
  tdAbortIORequestBody->tiDevHandle = tdIORequestBody->tiDevHandle;
  
  /* initialize agIORequest */
  agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) tdAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */
  
  /* remember IO to be aborted */
  tdAbortIORequestBody->tiIOToBeAbortedRequest = taskTag;      
 
  if (oneDeviceData->DeviceType == TD_SAS_DEVICE)
  {
    sasStatus = saSSPAbort(agRoot, 
                           agAbortIORequest, 
                           tdsaRotateQnumber(tiRoot, oneDeviceData), 
                           agDevHandle, 
                           0/* flag */, 
                           agIORequest,
                           agNULL); 
                           
    if (sasStatus == AGSA_RC_SUCCESS)
    {
      return tiSuccess;
    }
    else
    {
      return tiError;
    }
  }

  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {
    TI_DBG2(("tiINIIOAbort: calling satIOAbort() oneDeviceData=%p\n", oneDeviceData));
#ifdef FDS_SM
    smRoot = &(tdsaAllShared->smRoot);
    if ( taskTag != agNULL)
    {    
      ToBeAbortedtdIORequestBody = (tdIORequestBody_t *)taskTag->tdData;
      ToBeAborted = &(ToBeAbortedtdIORequestBody->smIORequest);
      status = smIOAbort(smRoot, ToBeAborted);
      return status;
    }
    else
    {
      TI_DBG1(("tiINIIOAbort: taskTag is NULL!!!\n"));
      return tiError;
    }      

#else

#ifdef SATA_ENABLE  
    status = satIOAbort(tiRoot, taskTag );
#endif

    return status;
#endif /* else FDS_SM */ 
  }
  
  else
  {
    return tiError;
  }

}

osGLOBAL bit32
tiINIIOAbortAll(
             tiRoot_t            *tiRoot,
             tiDeviceHandle_t    *tiDeviceHandle
             )
{
  agsaRoot_t          *agRoot = agNULL;
  tdsaDeviceData_t    *oneDeviceData = agNULL;
  bit32               status = tiError;
#ifdef FDS_SM
  tdsaRoot_t          *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t       *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t            *smRoot = &(tdsaAllShared->smRoot);
  smDeviceHandle_t    *smDeviceHandle;
#endif
  
  TI_DBG1(("tiINIIOAbortAll: start\n"));
  
  if (tiDeviceHandle == agNULL)
  {
    TI_DBG1(("tiINIIOAbortAll: tiDeviceHandle is NULL!!!\n"));
    return tiError;
  }      
  
  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;

  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINIIOAbortAll: oneDeviceData is NULL!!!\n"));
    return tiError;
  }
  
  /* for hotplug */
  if (oneDeviceData->valid != agTRUE || oneDeviceData->registered != agTRUE ||
      oneDeviceData->tdPortContext == agNULL )
  {
    TI_DBG1(("tiINIIOAbortAll: NO Device did %d\n", oneDeviceData->id ));
    TI_DBG1(("tiINIIOAbortAll: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG1(("tiINIIOAbortAll: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
    return tiError;
  } 
  
  agRoot = oneDeviceData->agRoot;
  
  if (agRoot == agNULL)
  {
    TI_DBG1(("tiINIIOAbortAll: agRoot is NULL!!!\n"));
    return tiError;
  }
  
  /* this is processed in ossaSSPAbortCB, ossaSATAAbortCB, ossaSMPAbortCB */
  if (oneDeviceData->OSAbortAll == agTRUE)
  {
    TI_DBG1(("tiINIIOAbortAll: already pending!!!\n"));
    return tiBusy;
  }
  else
  {
    oneDeviceData->OSAbortAll = agTRUE;
  }    
  
#ifdef FDS_SM
  if ( DEVICE_IS_SSP_TARGET(oneDeviceData) || DEVICE_IS_SMP_TARGET(oneDeviceData))
  {
    status = tdsaAbortAll(tiRoot, agRoot, oneDeviceData);  
  }
  else if (DEVICE_IS_SATA_DEVICE(oneDeviceData) ||
           DEVICE_IS_STP_TARGET(oneDeviceData)
          )
  {
    TI_DBG2(("tiINIIOAbortAll: calling smIOAbortAll\n"));
    smDeviceHandle = (smDeviceHandle_t *)&(oneDeviceData->smDeviceHandle);
    smDeviceHandle->tdData = oneDeviceData;
    status = smIOAbortAll(smRoot, smDeviceHandle);
  }
  else
  {
    TI_DBG1(("tiINIIOAbortAll: unknow device type!!! 0x%x\n", oneDeviceData->target_ssp_stp_smp));
    status = AGSA_RC_FAILURE;
  }
#else  
  status = tdsaAbortAll(tiRoot, agRoot, oneDeviceData);    
#endif
  
  return status;

}
#endif /* INITIATOR_DRIVER	*/

/*****************************************************************************
*! \brief tdsaAbortAll
*
*  Purpose:  This function is called to abort an all pending I/O request on a
*            device
*
*  \param  tiRoot:          Pointer to initiator driver/port instance.
*  \param  agRoot:          Pointer to chip/driver Instance.
*  \param  oneDeviceData:   Pointer to the device
*
*  \return: 
*
*          None
*
*****************************************************************************/
osGLOBAL bit32
tdsaAbortAll( 
             tiRoot_t                   *tiRoot,
             agsaRoot_t                 *agRoot,
             tdsaDeviceData_t           *oneDeviceData
             )
{
  agsaIORequest_t     *agAbortIORequest = agNULL;  
  tdIORequestBody_t   *tdAbortIORequestBody = agNULL;
  bit32               PhysUpper32;
  bit32               PhysLower32;
  bit32               memAllocStatus;
  void                *osMemHandle;
  bit32               status = AGSA_RC_FAILURE;

  TI_DBG1(("tdsaAbortAll: did %d\n", oneDeviceData->id));
 
  /* allocating agIORequest for abort itself */
  memAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &osMemHandle,
                                   (void **)&tdAbortIORequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(tdIORequestBody_t),
                                   agTRUE
                                   );
  if (memAllocStatus != tiSuccess)
  {
    /* let os process IO */
    TI_DBG1(("tdsaAbortAll: ostiAllocMemory failed...\n"));
    return tiError;
  }
      
  if (tdAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    TI_DBG1(("tdsaAbortAll: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
    return tiError;
  }
  
  /* setup task management structure */
  tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  /* setting callback but not used later */
  tdAbortIORequestBody->IOCompletionFunc = agNULL;
  //tdAbortIORequestBody->IOCompletionFunc = itdssIOAbortedHandler;
  
  tdAbortIORequestBody->tiDevHandle = (tiDeviceHandle_t *)&(oneDeviceData->tiDeviceHandle);
  
  /* initialize agIORequest */
  agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) tdAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */    
  
  if ( DEVICE_IS_SSP_TARGET(oneDeviceData))
  {
    /* SSPAbort */
    status = saSSPAbort(agRoot, 
                        agAbortIORequest,
                        tdsaRotateQnumber(tiRoot, oneDeviceData), //0,
                        oneDeviceData->agDevHandle,
                        1, /* abort all */
                        agNULL,
                        agNULL		
                        );
  }
  else if (DEVICE_IS_SATA_DEVICE(oneDeviceData) ||
           DEVICE_IS_STP_TARGET(oneDeviceData)
          )
  {
    /* SATAAbort*/
    if (oneDeviceData->satDevData.IDDeviceValid == agFALSE)
    {    
      TI_DBG2(("tdsaAbortAll: saSATAAbort\n"));
      status = saSATAAbort(agRoot, 
                           agAbortIORequest,
                           0,
                           oneDeviceData->agDevHandle,
                           1, /* abort all */
                           agNULL,
                           agNULL
                           );
    }
    else
    {
      TI_DBG2(("tdsaAbortAll: saSATAAbort IDDeviceValid\n"));
      status = saSATAAbort(agRoot, 
                           agAbortIORequest,
                           tdsaRotateQnumber(tiRoot, oneDeviceData), //0,
                           oneDeviceData->agDevHandle,
                           1, /* abort all */
                           agNULL,
                           agNULL
                           );
    }			 
  }
  else if (DEVICE_IS_SMP_TARGET(oneDeviceData))
  {
    /* SMPAbort*/
    TI_DBG2(("tdsaAbortAll: saSMPAbort \n"));
    status = saSMPAbort(agRoot, 
                        agAbortIORequest,
                        tdsaRotateQnumber(tiRoot, oneDeviceData), //0,
                        oneDeviceData->agDevHandle,
                        1, /* abort all */
                        agNULL,
                        agNULL
                        );
  }
  else
  {
    TI_DBG1(("tdsaAbortAll: unknown device type!!! 0x%x\n", oneDeviceData->target_ssp_stp_smp));
    status = AGSA_RC_FAILURE;
  }
  
  if (status == AGSA_RC_SUCCESS)
  {
    return tiSuccess;
  }
  else
  {
    TI_DBG1(("tdsaAbortAll: failed status=%d\n", status));
    //failed to send abort command, we need to free the memory
    ostiFreeMemory(
               tiRoot, 
               tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle, 
               sizeof(tdIORequestBody_t)
               );
    return tiError;
  }

}



/*****************************************************************************
*! \brief tiCOMReset
*
*  Purpose:  This function is called to trigger soft or hard reset
*
*  \param  tiRoot:          Pointer to initiator driver/port instance.
*  \param  option:          Options
*
*  \return: 
*
*          None
*
*****************************************************************************/
osGLOBAL void
tiCOMReset( 
           tiRoot_t    *tiRoot,
           bit32       option
           )
{
  tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                *agRoot = agNULL;


#ifdef TI_GETFOR_ONRESET
  agsaControllerStatus_t controllerStatus;
  agsaForensicData_t         forensicData;
  bit32 once = 1;
  bit32 status;
#endif /* TI_GETFOR_ONRESET */
  
  TI_DBG1(("tiCOMReset: start option 0x%x\n",option));
  tdsaAllShared->resetCount++;
  TI_DBG2(("tiCOMReset: reset count %d\n", tdsaAllShared->resetCount));
  
  agRoot = &(tdsaAllShared->agRootNonInt);

  if (tdsaAllShared->flags.resetInProgress == agTRUE)
  {
    TI_DBG1(("tiCOMReset : Reset is already in progress : \n"));
    
    /* don't do anything : just return */
    return;
  }

  tdsaAllShared->flags.resetInProgress            = agTRUE;
  
#ifdef TI_GETFOR_ONRESET
  saGetControllerStatus(agRoot, &controllerStatus);
  if(controllerStatus.fatalErrorInfo.errorInfo1)
  {

    bit8 * DirectData = (bit8 * )tdsaAllShared->FatalErrorData;
    forensicData.DataType = TYPE_FATAL;
    forensicData.dataBuf.directLen =  (8 * 1024);
    forensicData.dataBuf.directOffset = 0; /* current offset */
    forensicData.dataBuf.readLen = 0;   /* Data read */
    getmoreData:
    forensicData.dataBuf.directData = DirectData;
    status = saGetForensicData( agRoot, agNULL, &forensicData);
    TI_DBG1(("tiCOMReset:status %d readLen 0x%x directLen 0x%x directOffset 0x%x\n",
      status,
      forensicData.dataBuf.readLen,
      forensicData.dataBuf.directLen,
      forensicData.dataBuf.directOffset));

    if( forensicData.dataBuf.readLen == forensicData.dataBuf.directLen && !status && once)
    {
       DirectData += forensicData.dataBuf.readLen;
      goto getmoreData;
    }
    TI_DBG1(("tiCOMReset:saGetForensicData type %d read 0x%x bytes\n",    forensicData.DataType,    forensicData.dataBuf.directOffset ));
  }

#endif /* TI_GETFOR_ONRESET */
  if (option == tiSoftReset)
  {
    /* soft reset */
    TI_DBG6(("tiCOMReset: soft reset\n"));
    saHwReset(agRoot, AGSA_SOFT_RESET, 0);
    return;
  }
  else
  {
    saHwReset(agRoot, AGSA_SOFT_RESET, 0);
#ifdef NOT_YET  
    /* hard reset */
    saHwReset(agRoot, AGSA_CHIP_RESET, 0);
#endif    
  }
  return;
}


/*****************************************************************************/
/*! \biref tiINIReportErrorToEventLog
 *
 *  Purpose: This function is called to report errors that needs to be logged
 *           into event log.
 *
 *  \param tiRoot:      Pointer to initiator specific root data structure  for this
 *                      instance of the driver.
 *  \param agEventData: Event data structure.
 *
 *  \return None.
 *
 */
/*****************************************************************************/
#ifdef INITIATOR_DRIVER
osGLOBAL bit32
tiINIReportErrorToEventLog(
                           tiRoot_t            *tiRoot,
                           tiEVTData_t         *agEventData
                           )
{
  TI_DBG6(("tiINIReportErrorToEventLog: start\n"));
  return tiError;
}
#endif /* INITIATOR_DRIVER */

/*****************************************************************************/
/*! \brief ossaReenableInterrupts
 *  
 *
 *  Purpose: This routine is called to enable interrupt
 *
 *  
 *  \param  agRoot:               Pointer to chip/driver Instance.
 *  \param  outboundChannelNum:   Zero-base channel number
 *
 * 
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *
 */
/*****************************************************************************/
#ifndef ossaReenableInterrupts
osGLOBAL void 
ossaReenableInterrupts(
                       agsaRoot_t  *agRoot,
                       bit32       outboundChannelNum
                       )
{
  tdsaRootOsData_t *osData = (tdsaRootOsData_t *) (agRoot->osData);

  ostiInterruptEnable(
                      osData->tiRoot,
                      outboundChannelNum
                      );
  return;
}
                       
#endif                                                                
             



/*
1. initiator
   send task management
   call saSSPAbort()

2. Target
   call saSSPAbort()

*/

/*****************************************************************************
*! \brief tiINITaskManagement
*
* Purpose:  This routine is called to explicitly ask the Transport Dependent
*           Layer to issue a Task Management command to a device.
*
*  \param tiRoot:         Pointer to driver instance
*  \param tiDeviveHandle: Pointer to the device handle for this session.
*  \param task:           SAM-2 task management request.
*  \param lun:            Pointer to the SCSI-3 LUN information
*                         when applicable. Set to zero when not applicable.
*  \param taskTag:        Pointer to the associated task where the task
*                         management command is to be applied. Set to agNULL
*                         if not applicable for the specific Task Management
*                         task.
*  \param currentTaskTag: The current context or task tag for this task. This
*                         task tag will be passed back in ostiInitiatorEvent()
*                         when this task management is completed.
*
*  \return:
*         tiSuccess     TM request successfully initiated.
*         tiBusy        No resources available, try again later.
*         tiIONoDevice  Invalid device handle.
*         tiError       Other errors that prevent the TM request to be started.
*
*****************************************************************************/
/* 
  warm reset->smp phy control(hard reset) or saLocalPhyControl(AGSA_PHY_HARD_RESET)
  
*/
#ifdef INITIATOR_DRIVER
osGLOBAL bit32
tiINITaskManagement (
                     tiRoot_t          *tiRoot,
                     tiDeviceHandle_t  *tiDeviceHandle,
                     bit32             task,
                     tiLUN_t           *lun,
                     tiIORequest_t     *taskTag, /* being aborted one */
                     tiIORequest_t     *currentTaskTag /* task management itself */
                     )
{

  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  agsaRoot_t                  *agRoot = agNULL;
  bit32                       tiStatus = tiError;
  bit32                       notImplemented = agFALSE;
  tdsaDeviceData_t            *oneDeviceData = agNULL;
  void                        *osMemHandle;
  tdIORequestBody_t           *TMtdIORequestBody;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  bit32                       agRequestType;
  agsaIORequest_t             *agIORequest = agNULL; /* task management itself */
  agsaIORequest_t             *agTMRequest = agNULL; /* IO being task managed */
  agsaDevHandle_t             *agDevHandle = agNULL;
  agsaSASRequestBody_t        *agSASRequestBody = agNULL;
  agsaSSPScsiTaskMgntReq_t    *agSSPTaskMgntRequest;
  bit32                       saStatus;
  tdIORequestBody_t           *tdIORequestBody;
#ifdef FDS_SM
  smRoot_t                    *smRoot;
  smDeviceHandle_t            *smDeviceHandle;
  smIORequest_t               *ToBeAborted = agNULL;
  smIORequest_t               *TaskManagement;
  tdIORequestBody_t           *ToBeAbortedtdIORequestBody;
  tdIORequestBody_t           *SMTMtdIORequestBody;
  void                        *SMosMemHandle;
  bit32                       SMPhysUpper32;
  bit32                       SMPhysLower32;
  bit32                       SMmemAllocStatus;
#endif  
  
  TI_DBG2(("tiINITaskManagement: start\n"));

  /* just for testing only */
#ifdef REMOVED  
//start temp  
  if(tiDeviceHandle == agNULL)
  {
    TI_DBG1(("tiINITaskManagement: tiDeviceHandle is NULL\n"));
    return tiError;
  }
  
  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  if(oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINITaskManagement: tiDeviceHandle=%p DeviceData is NULL\n", tiDeviceHandle));
    return tiError;
  }
  TI_DBG1(("tiINITaskManagement: did %d\n", oneDeviceData->id ));
  return tiError;
//end temp

// just for testing
  if (task == AG_LOGICAL_UNIT_RESET)
  {
    TI_DBG1(("tiINITaskManagement: failing LUN RESET for testing\n"));
    return tiError;
  }

#endif

  switch(task)
  {
  case AG_ABORT_TASK:
    TI_DBG6(("tiINITaskManagement: ABORT_TASK\n"));
    break;
  case AG_ABORT_TASK_SET:
    TI_DBG6(("tiINITaskManagement: ABORT_TASK_SET\n"));
    break;
  case AG_CLEAR_ACA:
    TI_DBG6(("tiINITaskManagement: CLEAR_ACA\n"));
    break;
  case AG_CLEAR_TASK_SET:
    TI_DBG6(("tiINITaskManagement: CLEAR_TASK_SET\n"));
    break;
  case AG_LOGICAL_UNIT_RESET:
    TI_DBG6(("tiINITaskManagement: LOGICAL_UNIT_RESET\n"));
    break;
  case AG_TARGET_WARM_RESET:
    TI_DBG6(("tiINITaskManagement: TARGET_WARM_RESET\n"));
    break;
  case AG_QUERY_TASK:
    TI_DBG6(("tiINITaskManagement: QUERY_TASK\n"));
    break;
  default:
    TI_DBG1(("tiINITaskManagement: notImplemented 0x%0x !!!\n",task));
    notImplemented = agTRUE;
    break;
  }

  if (notImplemented)
  {
    TI_DBG1(("tiINITaskManagement: not implemented 0x%0x !!!\n",task));
    return tiStatus;
  }
  
  if(tiDeviceHandle == agNULL)
  {
    TI_DBG1(("tiINITaskManagement: tiDeviceHandle is NULL\n"));
    return tiError;
  }
  
  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  if(oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINITaskManagement: tiDeviceHandle=%p DeviceData is NULL\n", tiDeviceHandle));
    return tiIONoDevice;
  }
  
  /* for hotplug */
  if (oneDeviceData->valid != agTRUE || oneDeviceData->registered != agTRUE ||
      oneDeviceData->tdPortContext == agNULL )
  {
    TI_DBG1(("tiINITaskManagement: NO Device did %d Addr 0x%08x:0x%08x\n", oneDeviceData->id , oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));
    return tiIONoDevice;
  } 
  
  /* 1. call tiINIOAbort() 
     2. call tdssTaskXmit()
  */
  
  if (oneDeviceData->DeviceType == TD_SAS_DEVICE)
  {
    agRoot = oneDeviceData->agRoot;
    agDevHandle = oneDeviceData->agDevHandle;
    TI_DBG1(("tiINITaskManagement: SAS Device\n"));
    
    /* 
      WARM_RESET is experimental code. 
      Needs more testing and debugging
    */
    if (task == AG_TARGET_WARM_RESET)
    {
      agsaContext_t           *agContext;
      tdsaDeviceData_t        *tdsaDeviceData;
      
      tdsaDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
      currentTaskTag->tdData = tdsaDeviceData;
      agContext = &(tdsaDeviceData->agDeviceResetContext);
      agContext->osData = currentTaskTag;   
            
      TI_DBG2(("tiINITaskManagement: did %d device reset for SAS\n", oneDeviceData->id));
      saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, SA_DS_IN_RECOVERY);
      
      /* warm reset by saLocalPhyControl or SMP PHY control */
      if (oneDeviceData->directlyAttached == agTRUE)
      {
        TI_DBG2(("tiINITaskManagement: device reset directly attached\n"));
        saLocalPhyControl(agRoot, 
                          agContext, 
                          tdsaRotateQnumber(tiRoot, oneDeviceData),
                          oneDeviceData->phyID, 
                          AGSA_PHY_HARD_RESET, 
                          agNULL
                          ); 
        return tiSuccess;
      }
      else
      {
        TI_DBG2(("tiINITaskManagement: device reset expander attached\n"));
        saStatus = tdsaPhyControlSend(tiRoot,
                                      oneDeviceData, 
                                      SMP_PHY_CONTROL_HARD_RESET, 
                                      currentTaskTag,
                                      tdsaRotateQnumber(tiRoot, oneDeviceData)				      
                                     );
        return saStatus;			      
      }
    }
    else
    {
      /* task management */
      TI_DBG6(("tiINITaskManagement: making task management frame \n"));
      /* 1. create task management frame
         2. sends it using "saSSPStart()"
      */
      /* Allocate memory for task management */
      memAllocStatus = ostiAllocMemory(
                                       tiRoot,
                                       &osMemHandle,
                                       (void **)&TMtdIORequestBody,
                                       &PhysUpper32,
                                       &PhysLower32,
                                       8,
                                       sizeof(tdIORequestBody_t),
                                       agTRUE
                                       );
    
      if (memAllocStatus != tiSuccess)
      {
        TI_DBG1(("tiINITaskManagement: ostiAllocMemory failed...\n"));
        return tiError;
      }
      
      if (TMtdIORequestBody == agNULL)
      {
        TI_DBG1(("tiINITaskManagement: ostiAllocMemory returned NULL TMIORequestBody\n"));
        return tiError;
      }
      
      /* initialize */
      osti_memset(TMtdIORequestBody, 0, sizeof(tdIORequestBody_t));

      /* setup task management structure */
      TMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
      TMtdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag = currentTaskTag;
      TMtdIORequestBody->IOType.InitiatorTMIO.TaskTag = taskTag;
    
      /* let's initialize tdIOrequestBody */
      /* initialize jump table */
    
      /* direct callback for task management */
      TMtdIORequestBody->IOCompletionFunc = itdssTaskCompleted;
      /* to be removed */
      /* TMtdIORequestBody->IOCompletionFunc = itdssIOCompleted; */
    
      /* initialize tiDevhandle */
      TMtdIORequestBody->tiDevHandle = tiDeviceHandle;
    
      /* initialize tiIORequest */
      TMtdIORequestBody->tiIORequest = currentTaskTag;
      /* save context if we need to abort later */
      currentTaskTag->tdData = TMtdIORequestBody; 

      /* initialize agIORequest */
      agIORequest = &(TMtdIORequestBody->agIORequest);
      agIORequest->osData = (void *) TMtdIORequestBody;
      agIORequest->sdkData = agNULL; /* SA takes care of this */
      
      /* request type */
      agRequestType = AGSA_SSP_TASK_MGNT_REQ;
      TMtdIORequestBody->agRequestType = AGSA_SSP_TASK_MGNT_REQ;
      /*
        initialize
        tdIORequestBody_t tdIORequestBody -> agSASRequestBody
      */
      agSASRequestBody = &(TMtdIORequestBody->transport.SAS.agSASRequestBody);
      agSSPTaskMgntRequest = &(agSASRequestBody->sspTaskMgntReq);
    
      TI_DBG2(("tiINITaskManagement: did %d LUN reset for SAS\n", oneDeviceData->id));
      /* fill up LUN field */
      if (lun == agNULL)
      {
        osti_memset(agSSPTaskMgntRequest->lun, 0, 8);
      }
      else
      {             
        osti_memcpy(agSSPTaskMgntRequest->lun, lun->lun, 8);
      }
    
      /* default: unconditionally set device state to SA_DS_IN_RECOVERY
         bit1 (DS) bit0 (ADS)
         bit1: 1 bit0: 0
      */
      agSSPTaskMgntRequest->tmOption = 2;
    
       /* sets taskMgntFunction field */
      switch(task)
      {
      case AG_ABORT_TASK:
        agSSPTaskMgntRequest->taskMgntFunction = AGSA_ABORT_TASK;
        /* For abort task management, unconditionally set device state to SA_DS_IN_RECOVERY
           and if can't find, set device state to SA_DS_IN_RECOVERY
           bit1 (DS) bit0 (ADS)
           bit1: 1; bit0: 1
        */
        agSSPTaskMgntRequest->tmOption = 3;
        break;
      case AG_ABORT_TASK_SET:
        agSSPTaskMgntRequest->taskMgntFunction = AGSA_ABORT_TASK_SET;
        break;
      case AG_CLEAR_ACA:
        agSSPTaskMgntRequest->taskMgntFunction = AGSA_CLEAR_ACA;
        break;
      case AG_CLEAR_TASK_SET:
        agSSPTaskMgntRequest->taskMgntFunction = AGSA_CLEAR_TASK_SET;
        break;
      case AG_LOGICAL_UNIT_RESET:
        agSSPTaskMgntRequest->taskMgntFunction = AGSA_LOGICAL_UNIT_RESET;
        break;
      case AG_QUERY_TASK:
        agSSPTaskMgntRequest->taskMgntFunction = AGSA_QUERY_TASK;
        break;
      default:
        TI_DBG1(("tiINITaskManagement: notImplemented task\n"));
        break;
      }
  
      if (task == AGSA_ABORT_TASK || task == AGSA_QUERY_TASK)
      {
        /* set agTMRequest, which is IO being task managed */
        tdIORequestBody = (tdIORequestBody_t *)taskTag->tdData;
        if (tdIORequestBody == agNULL)
        {
           /* to be aborted IO has been completed. */
          /* free up allocated memory */
          TI_DBG1(("tiINITaskManagement: IO has been completed\n"));
          ostiFreeMemory(
                         tiRoot,
                         osMemHandle,
                         sizeof(tdIORequestBody_t)
                         );
          return tiIONoDevice;
        }
        else
        {
        agTMRequest = &(tdIORequestBody->agIORequest);
        }	  
      }
      else
      {
        /*
          For LUN RESET, WARM_RESET, ABORT_TASK_SET, CLEAR_ACA and CLEAR_TASK_SET
          no tag to be managed.
          Therefore, set it to zero.
        */
        agSSPTaskMgntRequest->tagOfTaskToBeManaged = 0;
        agTMRequest = agNULL; 
      
      }
    
      TDLIST_INIT_HDR(&TMtdIORequestBody->EsglPageList);
      /* debuggging */
      if (TMtdIORequestBody->IOCompletionFunc == agNULL)
      {
        TI_DBG1(("tiINITaskManagement: Error!!!!! IOCompletionFunc is NULL\n"));
      }
      saStatus = saSSPStart(agRoot,
                            agIORequest, /* task management itself */
                            tdsaRotateQnumber(tiRoot, oneDeviceData),
                            agDevHandle,
                            agRequestType,
                            agSASRequestBody, /* task management itself */
                            agTMRequest, /* io to be aborted if exits */
                            &ossaSSPCompleted);
    
    
      if (saStatus == AGSA_RC_SUCCESS)
      {
        Initiator->NumIOsActive++;
        tiStatus = tiSuccess;
      }
      else
      {
        TI_DBG1(("tiINITaskManagement: saSSPStart failed 0x%x\n",saStatus));
        /* free up allocated memory */
        ostiFreeMemory(
                       tiRoot, 
                       TMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle, 
                       sizeof(tdIORequestBody_t)
                      );
        if (saStatus == AGSA_RC_FAILURE)		   
        {
          tiStatus = tiError;
        }
        else
        {
          /* AGSA_RC_BUSY */
          tiStatus = tiBusy;
        }
      }
    }
  } /* end of sas device */
  
#ifdef FDS_SM
  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {  
    agsaContext_t           *agContext = agNULL;
    
    /* save the task tag in tdsaDeviceData_t structure, for handling PORT_RESET_COMPLETE hw event */
    agContext = &(oneDeviceData->agDeviceResetContext);
    agContext->osData = currentTaskTag;
    
#ifdef REMOVED
    /* for directly attached SATA, do localphycontrol for LUN and target reset, not smTaskManagement*/
    if (oneDeviceData->directlyAttached == agTRUE &&
        (task == AG_LOGICAL_UNIT_RESET || task == AG_TARGET_WARM_RESET))
    {
      agRoot = oneDeviceData->agRoot;
      agDevHandle = oneDeviceData->agDevHandle;
      
      currentTaskTag->tdData = oneDeviceData;
      
      if (task == AG_LOGICAL_UNIT_RESET)
      {
        if ( (lun->lun[0] | lun->lun[1] | lun->lun[2] | lun->lun[3] |
              lun->lun[4] | lun->lun[5] | lun->lun[6] | lun->lun[7] ) != 0 )
        {
          TI_DBG1(("tiINITaskManagement: *** REJECT *** LUN not zero, tiDeviceHandle=%p\n", 
                  tiDeviceHandle));
          return tiError;
        }
     }
     saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, SA_DS_IN_RECOVERY);
     tiStatus = saLocalPhyControl(agRoot, agContext, tdsaRotateQnumber(tiRoot, oneDeviceData), oneDeviceData->phyID, AGSA_PHY_HARD_RESET, agNULL); 
    }
    else
#endif    
    {
      smRoot = &(tdsaAllShared->smRoot);
      smDeviceHandle = &(oneDeviceData->smDeviceHandle);
      TI_DBG1(("tiINITaskManagement: FDS_SM SATA Device\n"));
    
      if ( taskTag != agNULL)
      {    
        ToBeAbortedtdIORequestBody = (tdIORequestBody_t *)taskTag->tdData;
        ToBeAborted = &(ToBeAbortedtdIORequestBody->smIORequest);
      }      
      SMmemAllocStatus = ostiAllocMemory(
                                         tiRoot,
                                         &SMosMemHandle,
                                         (void **)&SMTMtdIORequestBody,
                                         &SMPhysUpper32,
                                         &SMPhysLower32,
                                         8,
                                         sizeof(tdIORequestBody_t),
                                         agTRUE
                                         );
      if (SMmemAllocStatus != tiSuccess)
      {
        TI_DBG1(("tiINITaskManagement: ostiAllocMemory failed... loc 2\n"));
        return tiError;
      }
      
      if (SMTMtdIORequestBody == agNULL)
      {
        TI_DBG1(("tiINITaskManagement: ostiAllocMemory returned NULL TMIORequestBody loc 2\n"));
        return tiError;
      }
  
      /* initialize */
      osti_memset(SMTMtdIORequestBody, 0, sizeof(tdIORequestBody_t));
      
      /* setup task management structure */
      SMTMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle = SMosMemHandle;
      SMTMtdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag = currentTaskTag;
      SMTMtdIORequestBody->IOType.InitiatorTMIO.TaskTag = taskTag;
    
      /* initialize tiDevhandle */
      SMTMtdIORequestBody->tiDevHandle = tiDeviceHandle;
    
      /* initialize tiIORequest */
      SMTMtdIORequestBody->tiIORequest = currentTaskTag;
      /* save context if we need to abort later */
      currentTaskTag->tdData = SMTMtdIORequestBody; 
  
      TaskManagement = &(SMTMtdIORequestBody->smIORequest);
    
      TaskManagement->tdData = SMTMtdIORequestBody;
      TaskManagement->smData = &SMTMtdIORequestBody->smIORequestBody;
    
      tiStatus = smTaskManagement(smRoot, 
      	                           smDeviceHandle, 
      	                           task, 
      	                           (smLUN_t*)lun, 
      	                           ToBeAborted, 
      	                           TaskManagement
      	                           );
      if (tiStatus != SM_RC_SUCCESS)
      {
        TI_DBG1(("tiINITaskManagement: smTaskManagement failed... loc 2\n"));
        /* free up allocated memory */
        ostiFreeMemory(
                       tiRoot, 
                       SMTMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle, 
                       sizeof(tdIORequestBody_t)
                      );
      }
    } /* else */      
  }
#else   
  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {
    agRoot = oneDeviceData->agRoot;
    agDevHandle = oneDeviceData->agDevHandle;
    TI_DBG1(("tiINITaskManagement: not FDS_SM SATA Device\n"));
    /*
      WARM_RESET is experimental
      Needs more testing and debugging
      Soft reset for SATA as LUN RESET tends not to work. 
      Let's do hard reset
    */
    if (task == AG_LOGICAL_UNIT_RESET || task == AG_TARGET_WARM_RESET)
    {
    
      agsaContext_t           *agContext;
      satDeviceData_t         *satDevData;
      tdsaDeviceData_t        *tdsaDeviceData;
      
      TI_DBG2(("tiINITaskManagement: did %d LUN reset or device reset for SATA\n", oneDeviceData->id));
      tdsaDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
      satDevData      = &tdsaDeviceData->satDevData;
      currentTaskTag->tdData = tdsaDeviceData;
      agContext = &(tdsaDeviceData->agDeviceResetContext);
      agContext->osData = currentTaskTag;   
      
      
      if (task == AG_LOGICAL_UNIT_RESET)
      {
        if ( (lun->lun[0] | lun->lun[1] | lun->lun[2] | lun->lun[3] |
              lun->lun[4] | lun->lun[5] | lun->lun[6] | lun->lun[7] ) != 0 )
        {
          TI_DBG1(("tiINITaskManagement: *** REJECT *** LUN not zero, tiDeviceHandle=%p\n", 
                  tiDeviceHandle));
          return tiError;
        }

        /* 
         * Check if there is other TM request pending 
         */
        if (satDevData->satTmTaskTag != agNULL)
        {
          TI_DBG1(("tiINITaskManagement: *** REJECT *** other TM pending, tiDeviceHandle=%p\n", 
                   tiDeviceHandle));
          return tiError;
        }
      }
      satDevData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;
      satDevData->satAbortAfterReset = agFALSE;
      
      saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, SA_DS_IN_RECOVERY);
   
      /*
        warm reset by saLocalPhyControl or SMP PHY control 
       */
      if (oneDeviceData->directlyAttached == agTRUE)
      {
        TI_DBG1(("tiINITaskManagement: LUN reset or device reset directly attached\n"));
        saLocalPhyControl(agRoot, agContext, tdsaRotateQnumber(tiRoot, oneDeviceData), oneDeviceData->phyID, AGSA_PHY_HARD_RESET, agNULL); 
        return tiSuccess;
      }
      else
      {
        TI_DBG1(("tiINITaskManagement: LUN reset or device reset expander attached\n"));
        saStatus = tdsaPhyControlSend(tiRoot,
                                      oneDeviceData, 
                                      SMP_PHY_CONTROL_HARD_RESET, 
                                      currentTaskTag,
                                      tdsaRotateQnumber(tiRoot, oneDeviceData)
                                     );
        return saStatus;			      
      }
    }
    else
    {
      TI_DBG2(("tiINITaskManagement: calling satTM().\n")); 
      /* allocation tdIORequestBody and pass it to satTM() */
      memAllocStatus = ostiAllocMemory(
                                       tiRoot,
                                       &osMemHandle,
                                       (void **)&TMtdIORequestBody,
                                       &PhysUpper32,
                                       &PhysLower32,
                                       8,
                                       sizeof(tdIORequestBody_t),
                                       agTRUE
                                       );
    
      if (memAllocStatus != tiSuccess)
      {
        TI_DBG1(("tiINITaskManagement: ostiAllocMemory failed... loc 2\n"));
        return tiError;
      }
      
      if (TMtdIORequestBody == agNULL)
      {
        TI_DBG1(("tiINITaskManagement: ostiAllocMemory returned NULL TMIORequestBody loc 2\n"));
        return tiError;
      
      }
      
      /* initialize */
      osti_memset(TMtdIORequestBody, 0, sizeof(tdIORequestBody_t));
      
      /* setup task management structure */
      TMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
      TMtdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag = currentTaskTag;
      TMtdIORequestBody->IOType.InitiatorTMIO.TaskTag = taskTag;
    
      /* initialize tiDevhandle */
      TMtdIORequestBody->tiDevHandle = tiDeviceHandle;
    
      /* initialize tiIORequest */
      TMtdIORequestBody->tiIORequest = currentTaskTag;
      /* save context if we need to abort later */
      currentTaskTag->tdData = TMtdIORequestBody; 

      /* initialize agIORequest */
      agIORequest = &(TMtdIORequestBody->agIORequest);
      agIORequest->osData = (void *) TMtdIORequestBody;
      agIORequest->sdkData = agNULL; /* SA takes care of this */

        
#ifdef  SATA_ENABLE
      tiStatus = satTM( tiRoot,
                        tiDeviceHandle,
                        task,
                        lun,
                        taskTag,
                        currentTaskTag,
                        TMtdIORequestBody,
                        agTRUE
                        );
#endif
    }
  }
#endif /* FDS_SM else*/

  return tiStatus;
}
#endif  /* INITIATOR_DRIVER */

#ifdef PASSTHROUGH
osGLOBAL bit32
tiCOMPassthroughCmndStart(
                          tiRoot_t                *tiRoot,
                          tiPassthroughRequest_t      *tiPassthroughRequest,
                          tiDeviceHandle_t            *tiDeviceHandle,
                          tiPassthroughCmnd_t           *tiPassthroughCmnd,
                          void                      *tiPassthroughBody,
                          tiPortalContext_t           *tiportalContext,
                          ostiPassthroughCmndEvent_t        agEventCB
                          )
{
  tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t          *oneDeviceData;
  agsaRoot_t                *agRoot = agNULL;
  agsaIORequest_t           *agIORequest = agNULL;
  agsaDevHandle_t           *agDevHandle = agNULL;
  bit32                     agRequestType;
  agsaSASRequestBody_t      *agSASRequestBody = agNULL;
  
  tdPassthroughCmndBody_t   *tdPTCmndBody;
  tdssSMPRequestBody_t      *tdssSMPRequestBody;
  agsaSMPFrame_t            *agSMPFrame; 
  agsaSSPVSFrame_t          *agSSPVendorFrame; /* RMC */
  bit32                     SMPFn, SMPFnResult, SMPFrameLen;
  bit32                     tiStatus = tiError;
  bit32                     saStatus = AGSA_RC_FAILURE;
  tdsaPortStartInfo_t       *tdsaPortStartInfo;
  tdsaPortContext_t         *tdsaPortContext;
    
  TI_DBG2(("tiCOMPassthroughCmndStart: start\n"));

  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  
  TI_DBG6(("tiCOMPassthroughCmndStart: onedevicedata %p\n", oneDeviceData));

  
  tdPTCmndBody = (tdPassthroughCmndBody_t *)tiPassthroughBody;
  

  if (tiPassthroughCmnd->passthroughCmnd != tiSMPCmnd ||
      tiPassthroughCmnd->passthroughCmnd != tiRMCCmnd)
  {
    return tiNotSupported;
  }
  

  if (oneDeviceData == agNULL && tiPassthroughCmnd->passthroughCmnd != tiSMPCmnd)
  {
    TI_DBG1(("tiCOMPassthroughCmndStart: tiDeviceHandle=%p DeviceData is NULL\n", tiDeviceHandle ));
    return tiIONoDevice;
  }

  /* starting IO with SAS device */
  if (oneDeviceData->DeviceType == TD_SAS_DEVICE)
  {
    if (tiPassthroughCmnd->passthroughCmnd == tiSMPCmnd)
    {
      TI_DBG2(("tiCOMPassthroughCmndStart: SMP\n"));
      if (oneDeviceData == agNULL)
      {
        tdsaPortStartInfo = (tdsaPortStartInfo_t *)tiportalContext->tdData;
        tdsaPortContext = tdsaPortStartInfo->portContext;
        agRoot = tdsaPortContext->agRoot;
      }
      else
      {
        agRoot = oneDeviceData->agRoot;
        agDevHandle = oneDeviceData->agDevHandle;
      }

      
      tdssSMPRequestBody =  &(tdPTCmndBody->protocol.SMP.SMPBody);
      agSASRequestBody = &(tdssSMPRequestBody->agSASRequestBody);
      agSMPFrame = &(agSASRequestBody->smpFrame);

      /* saves callback function */
      tdPTCmndBody->EventCB = agEventCB;

      /* initialize command type  */
      tdPTCmndBody->tiPassthroughCmndType = tiSMPCmnd;

      /* initialize tipassthroughrequest  */
      tdPTCmndBody->tiPassthroughRequest = tiPassthroughRequest;
      tiPassthroughRequest->tdData = tdPTCmndBody;

      /* initialize tiDevhandle */
      tdPTCmndBody->tiDevHandle = tiDeviceHandle;
      
      /* fill in SMP header */
      agSMPFrame->frameHeader.smpFrameType
        = tiPassthroughCmnd->protocol.SMP.SMPHeader.smpFrameType;
      agSMPFrame->frameHeader.smpFunction
        = tiPassthroughCmnd->protocol.SMP.SMPHeader.smpFunction;
      agSMPFrame->frameHeader.smpFunctionResult
        = tiPassthroughCmnd->protocol.SMP.SMPHeader.smpFunctionResult;
      agSMPFrame->frameHeader.smpReserved
        = tiPassthroughCmnd->protocol.SMP.SMPHeader.smpReserved;
        
      if (tiPassthroughCmnd->protocol.SMP.IT == SMP_INITIATOR)
        {
          agRequestType = AGSA_SMP_INIT_REQ;
        }
      else
        {
          agRequestType = AGSA_SMP_TGT_RESPONSE;
          /* this is only for SMP target */
          agSMPFrame->phyId = tiPassthroughCmnd->protocol.SMP.phyID;
        }

      /* fill in payload */
      /* assumption: SMP payload is in tisgl1 */
      agSMPFrame->frameAddrUpper32 = tiPassthroughCmnd->tiSgl.upper;
      agSMPFrame->frameAddrLower32 = tiPassthroughCmnd->tiSgl.lower;

      /* This length excluding SMP header (4 bytes) and CRC field */
      agSMPFrame->frameLen = tiPassthroughCmnd->tiSgl.len;

      /* initialize agIORequest */
      /*
        Compare:
        tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
      */
      agIORequest = &(tdssSMPRequestBody->agIORequest);
      agIORequest->osData = (void *) tdPTCmndBody;
      agIORequest->sdkData = agNULL; /* LL takes care of this */

      
      
      /* not work yet because of high priority q */
      saStatus = saSMPStart(
                            agRoot,
                            agIORequest,
                            agDevHandle,
                            agRequestType,
                            agSASRequestBody,
                            &ossaSMPCompleted
                            ); 

      if (saStatus == AGSA_RC_SUCCESS)
      {
        tiStatus = tiSuccess;
      }
      else if (saStatus == AGSA_RC_FAILURE)
      {
        TI_DBG1(("tiCOMPassthroughCmndStart: saSMPStart failed\n"));
        tiStatus = tiError;
      }
      else
      {
        /* AGSA_RC_BUSY */
        TI_DBG1(("tiCOMPassthroughCmndStart: saSMPStart busy\n"));
        tiStatus = tiBusy;
      }
      return tiStatus;
      
      
#ifdef TO_DO      
      /* fill in SMP header */
      if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          agSMPFrame->frameHeader.smpFrameType = SMP_REQUEST; /* SMP REQUEST */
          agRequestType = AGSA_SMP_INIT_REQ;
        }
      else
        {
          /* SMP target */
          agSMPFrame->frameHeader.smpFrameType = SMP_RESPONSE; /* SMP RESPONSE */
          agRequestType = AGSA_SMP_TGT_RESPONSE;
          switch (tdPTCmndBody->protocol.SMP.SMPFnResult)
          {
          case tiSMPFunctionAccepted:
            SMPFnResult = SMP_FUNCTION_ACCEPTED;
            break;
          case tiUnknownSMPFunction:
            SMPFnResult = UNKNOWN_SMP_FUNCTION;
            break;
          case tiSMPFunctionFailed:
            SMPFnResult = SMP_FUNCTION_FAILED;
            break;
          case tiInvalidRequestFrameLength:
            SMPFnResult = INVALID_REQUEST_FRAME_LENGTH;
            break;
          case tiPhyDoesNotExist:
            SMPFnResult =PHY_DOES_NOT_EXIST;
            break;
          case tiIndexDoesNotExist:
            SMPFnResult = INDEX_DOES_NOT_EXIST;
            break;
          case tiPhyDoesNotSupportSATA:
            SMPFnResult = PHY_DOES_NOT_SUPPORT_SATA;
            break;
          case tiUnknownPhyOperation:
            SMPFnResult = UNKNOWN_PHY_OPERATION;
            break;
          case tiUnknownPhyTestFunction:
            SMPFnResult = UNKNOWN_PHY_TEST_FUNCTION;
            break;
          case tiPhyTestFunctionInProgress:
            SMPFnResult = PHY_TEST_FUNCTION_IN_PROGRESS;
            break;
          case tiPhyVacant:
            SMPFnResult = PHY_VACANT;
            break;
            
          default:
            TI_DBG1(("tiCOMPassthroughCmndStart: unknown SMP function result %d\n", tdPTCmndBody->protocol.SMP.SMPFnResult));
            return tiError;
          }
          agSMPFrame->frameHeader.smpFunctionResult = SMPFnResult;
        }

      /* common */
      switch (tdPTCmndBody->protocol.SMP.SMPFn)
      {
      case tiGeneral:
        SMPFn = SMP_REPORT_GENERAL;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = 0;
        }
        else
        {
          SMPFrameLen = sizeof(smpRespReportGeneral_t);
        }
        break;
        
      case tiManufacturerInfo:
        SMPFn = SMP_REPORT_MANUFACTURE_INFORMATION;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = 0;
        }
        else
        {
          SMPFrameLen = sizeof(smpRespReportManufactureInfo_t);
        }
        break;
        
      case tiDiscover:
        SMPFn = SMP_DISCOVER;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = sizeof(smpReqDiscover_t);
        }
        else
        {
          SMPFrameLen = sizeof(smpRespDiscover_t);
        }
        break;
        
      case tiReportPhyErrLog:
        SMPFn = SMP_REPORT_PHY_ERROR_LOG;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = 8;
        }
        else
        {
          SMPFrameLen = 24;
        }
        break;
        
      case tiReportPhySATA:
        SMPFn = SMP_REPORT_PHY_SATA;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = sizeof(SmpReqReportPhySata_t);
        }
        else
        {
          SMPFrameLen = sizeof(SmpRespReportPhySata_t);
        }
        break;
        
      case tiReportRteInfo:
        SMPFn = SMP_REPORT_ROUTING_INFORMATION;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = sizeof(SmpReqReportRouteTable_t);
        }
        else
        {
          SMPFrameLen = sizeof(SmpRespReportRouteTable_t);
        }
        break;
        
      case tiConfigureRteInfo:
        SMPFn = SMP_CONFIGURE_ROUTING_INFORMATION;;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = sizeof(SmpReqConfigureRouteInformation_t);
        }
        else
        {
          SMPFrameLen = 0;
        }
        break;

      case tiPhyCtrl:
        SMPFn = SMP_PHY_CONTROL;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = sizeof(SmpReqPhyControl_t);
        }
        else
        {
          SMPFrameLen = 0;
        }
        break;

      case tiPhyTestFn:
        SMPFn = SMP_PHY_TEST_FUNCTION;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = 36;
        }
        else
        {
          SMPFrameLen = 0;
        }
        break;

      case tiPMC:
        SMPFn = SMP_PMC_SPECIFIC;
        if (tdPTCmndBody->protocol.SMP.IT == SMP_INITIATOR)
        {
          SMPFrameLen = 0;
        }
        else
        {
          SMPFrameLen = 0;
        }
        break;
        
        
      default:
        TI_DBG1(("tiCOMPassthroughCmndStart: unknown SMP function %d\n", tdPTCmndBody->protocol.SMP.SMPFn));
        return tiError;
      }
      agSMPFrame->frameHeader.smpFunction = SMPFn;

     
      /* assumption: SMP payload is in tisgl1 */
      agSMPFrame->frameAddrUpper32 = tdPTCmndBody->tiSgl.upper;
      agSMPFrame->frameAddrLower32 = tdPTCmndBody->tiSgl.lower;

      /* This length excluding SMP header (4 bytes) and CRC field */
      agSMPFrame->frameLen = SMPFrameLen;

      
      
     


#endif

      
    }
    else if (tiPassthroughCmnd->passthroughCmnd == tiRMCCmnd)
    {
      TI_DBG2(("tiCOMPassthroughCmndStart: RMC\n"));
    }
    else
    {
      TI_DBG1(("tiCOMPassthroughCmndStart: unknown protocol %d\n", tiPassthroughCmnd->passthroughCmnd));
    }

    
  }
  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {
    TI_DBG1(("tiCOMPassthroughCmndStart: error !!! no SATA support\n"));
    return tiError;
  }
  else
  {
    TI_DBG1(("tiCOMPassthroughCmndStart: error !!! unknown devietype %d\n", oneDeviceData->DeviceType));
    return tiError;

  }
  
  return tiSuccess;
}


osGLOBAL bit32                                
tiCOMPassthroughCmndAbort(
                          tiRoot_t                *tiRoot,
                          tiPassthroughRequest_t    *taskTag
                          )
{
  tdsaRoot_t                *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                *agRoot = agNULL;
  tdPassthroughCmndBody_t   *tdPTCmndBody = agNULL;
  tdssSMPRequestBody_t      *tdssSMPRequestBody = agNULL;
  agsaIORequest_t           *agIORequest = agNULL;
  bit32                     saStatus, tiStatus = tiError;
  
  TI_DBG2(("tiCOMPassthroughCmndAbort: start\n"));

  agRoot          = &(tdsaAllShared->agRootNonInt);
  tdPTCmndBody    = (tdPassthroughCmndBody_t *)taskTag->tdData;
  
  if (tdPTCmndBody->tiPassthroughCmndType == tiSMPCmnd)
  {
    tdssSMPRequestBody =  &(tdPTCmndBody->protocol.SMP.SMPBody);
    agIORequest = &(tdssSMPRequestBody->agIORequest);

    saStatus = saSMPAbort(agRoot, agIORequest);
    
    if (saStatus == AGSA_RC_SUCCESS)
      {
        tiStatus = tiSuccess;
      }
      else if (saStatus == AGSA_RC_FAILURE)
      {
        TI_DBG1(("tiCOMPassthroughCmndAbort: saSMPAbort failed\n"));
        tiStatus = tiError;
      }
      else
      {
        /* AGSA_RC_BUSY */
        TI_DBG1(("tiCOMPassthroughCmndAbort: saSMPAbort busy\n"));
        tiStatus = tiBusy;
      }
      return tiStatus;
  }
  else if (tdPTCmndBody->tiPassthroughCmndType == tiRMCCmnd)
  {
    TI_DBG1(("tiCOMPassthroughCmndAbort: RMC passthrough command type, not yet\n"));

  }
  else
  {
    TI_DBG1(("tiCOMPassthroughCmndAbort: unknown passthrough command type %d\n", tdPTCmndBody->tiPassthroughCmndType));
    return tiStatus;
  }


}

osGLOBAL bit32
tiINIPassthroughCmndRemoteAbort(
                                tiRoot_t            *tiRoot,
                                tiDeviceHandle_t      *tiDeviceHandle,
                                tiPassthroughRequest_t    *taskTag,
                                tiPassthroughRequest_t    *currentTaskTag,
                                tiPortalContext_t       *tiportalContext
                                )
{
  TI_DBG2(("tiINIPassthroughCmndRemoteAbort: start\n"));
  /*
    for SMP, nothing. Can't abot remotely
  */
  return tiSuccess;
}
#endif /* PASSTHROUGH */


/*****************************************************************************
*! \brief tiCOMShutDown
*
*  Purpose: This function is called to shutdown the initiator and/or target
*           operation. Following the completion of this call, the state is
*           equivalent to the state prior to tiCOMInit()
*
*  \param tiRoot:  Pointer to root data structure.
*
*  \return     None
*
*
*****************************************************************************/
osGLOBAL void
tiCOMShutDown( tiRoot_t    *tiRoot)
{
  tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

// #define  TI_GETFOR_ONSHUTDOWN
#ifdef TI_GETFOR_ONSHUTDOWN
  agsaForensicData_t         forensicData;
  bit32 once = 1;
  bit32  status;
#endif /* TI_GETFOR_ONSHUTDOWN */

  agsaRoot_t                *agRoot = agNULL;
   
  TI_DBG1(("tiCOMShutDown: start\n"));

  
  agRoot = &(tdsaAllShared->agRootNonInt);
  /*
    1. free up cardID
    2. call saHwShutdown()
    3. tdInitEsgl(tiRoot);
    4. tdsaResetComMemFlags(tiRoot)
    5. ostiPortEvent()
  */
  
  tdsaFreeCardID(tiRoot, tdsaAllShared->CardID);

#ifdef TI_GETFOR_ONSHUTDOWN
  forensicData.DataType = TYPE_NON_FATAL;
  forensicData.dataBuf.directLen =  (8 * 1024);
  forensicData.dataBuf.directOffset = 0; /* current offset */
  forensicData.dataBuf.directData = agNULL;
  forensicData.dataBuf.readLen = 0;   /* Data read */

  getmoreData:
  status = saGetForensicData( agRoot, agNULL, &forensicData);

  TI_DBG1(("tiCOMShutDown:readLen 0x%x directLen 0x%x directOffset 0x%x\n",
      forensicData.dataBuf.readLen,
      forensicData.dataBuf.directLen,
      forensicData.dataBuf.directOffset));
  if( forensicData.dataBuf.readLen == forensicData.dataBuf.directLen && !status && once)
  {
    goto getmoreData;
  }
  
  TI_DBG1(("tiCOMShutDown:saGetForensicData type %d read 0x%x bytes\n",    forensicData.DataType,    forensicData.dataBuf.directOffset ));
#endif /* TI_GETFOR_ONSHUTDOWN */

  saHwShutdown(agRoot);
 
  /* resets all the relevant flags */
  tdsaResetComMemFlags(tiRoot);

  /*
   * send an event to the oslayer
   */
  ostiPortEvent (
                 tiRoot, 
                 tiPortShutdown, 
                 tiSuccess,
                 agNULL
                 );
 
  return;
}

#ifdef INITIATOR_DRIVER
osGLOBAL void
tiINITimerTick( tiRoot_t  *tiRoot )
{
  /*
    no timer is used in SAS TD layer.
    Therefore, this function is null.
  */
  //  TI_DBG2(("tiINITimerTick: start\n"));
  /*itdsaProcessTimers(tiRoot);*/
  return;
}
#endif

/*****************************************************************************/
/*! \brief ossaDisableInterrupts
 *  
 *
 *  Purpose: This routine is called to disable interrupt
 *
 *  
 *  \param  agRoot:               Pointer to chip/driver Instance.
 *  \param  outboundChannelNum:   Zero-base channel number
 *
 * 
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *
 */
/*****************************************************************************/
#ifndef ossaDisableInterrupts
osGLOBAL void 
ossaDisableInterrupts(
                      agsaRoot_t  *agRoot,
                      bit32       outboundChannelNum
                      )
{
  tdsaRootOsData_t *osData = (tdsaRootOsData_t *) (agRoot->osData);

  ostiInterruptDisable(
                       osData->tiRoot,
                       outboundChannelNum
                       );
  return;
}
                            
#endif                            


osGLOBAL void
tiCOMFrameReadBlock( 
                    tiRoot_t          *tiRoot,
                    void              *agFrame, 
                    bit32             FrameOffset, 
                    void              *FrameBuffer, 
                    bit32             FrameBufLen )
{
  tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                *agRoot = agNULL;
   
  TI_DBG6(("tiCOMFrameReadBlock: start\n"));

  
  agRoot = &(tdsaAllShared->agRootNonInt);

  
  TI_DBG6(("tiCOMFrameReadBlock: start\n"));
  
  saFrameReadBlock(agRoot, agFrame, FrameOffset, FrameBuffer, FrameBufLen);

  return;
}



/*****************************************************************************
*! \brief tiINITransportRecovery
*
* Purpose:  This routine is called to explicitly ask the Transport Dependent
*           Layer to initiate the recovery for the transport/protocol specific
*           error for a specific device connection.
*
*  \param   tiRoot:         Pointer to driver instance
*  \param   tiDeviveHandle: Pointer to the device handle for this session.
*
*  \return: None
*
*
*****************************************************************************/
#ifdef INITIATOR_DRIVER
osGLOBAL void
tiINITransportRecovery (
                        tiRoot_t          *tiRoot,
                        tiDeviceHandle_t  *tiDeviceHandle
                        )
{
  agsaRoot_t                  *agRoot = agNULL;
  tdsaDeviceData_t            *oneDeviceData = agNULL;
  tdsaPortContext_t           *onePortContext = agNULL;
  tiPortalContext_t           *tiPortalContext = agNULL;
  tiIORequest_t               *currentTaskTag; 
  agsaDevHandle_t             *agDevHandle = agNULL;
   
  TI_DBG1(("tiINITransportRecovery: start\n"));
 
  if (tiDeviceHandle == agNULL)
  {
    TI_DBG1(("tiINITransportRecovery: tiDeviceHandle is NULL\n"));
  
    return;
  }
  
  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;

  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINITransportRecovery: oneDeviceData is NULL\n"));
    return;
  }

  /* for hotplug */
  if (oneDeviceData->valid != agTRUE || oneDeviceData->registered != agTRUE ||
      oneDeviceData->tdPortContext == agNULL )
  {
    TI_DBG1(("tiINITransportRecovery: NO Device did %d\n", oneDeviceData->id ));
    TI_DBG1(("tiINITransportRecovery: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG1(("tiINITransportRecovery: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
    return;
  } 
  
  onePortContext = oneDeviceData->tdPortContext;
  
  if (onePortContext == agNULL)
  {
    TI_DBG1(("tiINITransportRecovery: onePortContext is NULL\n"));
    return;
  }
  
  tiPortalContext = onePortContext->tiPortalContext;
  currentTaskTag = &(oneDeviceData->TransportRecoveryIO);
  currentTaskTag->osData = agNULL;
  agRoot = oneDeviceData->agRoot;
  agDevHandle = oneDeviceData->agDevHandle;
  
  if (oneDeviceData->DeviceType == TD_SAS_DEVICE)
  {
    agsaContext_t           *agContext;
    currentTaskTag->tdData = oneDeviceData;
    agContext = &(oneDeviceData->agDeviceResetContext);
    agContext->osData = currentTaskTag;   
    oneDeviceData->TRflag = agTRUE;
    
    TI_DBG2(("tiINITransportRecovery: SAS device\n"));
    saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, SA_DS_IN_RECOVERY);

    if (oneDeviceData->directlyAttached == agTRUE)
    {
      TI_DBG2(("tiINITransportRecovery: saLocalPhyControl\n"));
      saLocalPhyControl(agRoot, agContext, tdsaRotateQnumber(tiRoot, oneDeviceData), oneDeviceData->phyID, AGSA_PHY_HARD_RESET, agNULL);
      ostiInitiatorEvent(tiRoot,
                         tiPortalContext,
                         tiDeviceHandle,
                         tiIntrEventTypeTransportRecovery,
                         tiRecStarted,
                         agNULL
                        );
       
      return;
    }
    else
    {
      TI_DBG2(("tiINITransportRecovery: device reset expander attached\n"));
      tdsaPhyControlSend(tiRoot,
                         oneDeviceData, 
                         SMP_PHY_CONTROL_HARD_RESET, 
                         currentTaskTag,
                         tdsaRotateQnumber(tiRoot, oneDeviceData)
                        );
      ostiInitiatorEvent(tiRoot,
                         tiPortalContext,
                         tiDeviceHandle,
                         tiIntrEventTypeTransportRecovery,
                         tiRecStarted,
                         agNULL
                        );
      return;
    }
  }
  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {
    agsaContext_t           *agContext;
    currentTaskTag->tdData = oneDeviceData;
    agContext = &(oneDeviceData->agDeviceResetContext);
    agContext->osData = currentTaskTag;   
    oneDeviceData->TRflag = agTRUE;
    
    TI_DBG2(("tiINITransportRecovery: SATA device\n"));
    saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, SA_DS_IN_RECOVERY);
    
    if (oneDeviceData->directlyAttached == agTRUE)
    {
      TI_DBG2(("tiINITransportRecovery: saLocalPhyControl\n"));
      saLocalPhyControl(agRoot, agContext, tdsaRotateQnumber(tiRoot, oneDeviceData), oneDeviceData->phyID, AGSA_PHY_LINK_RESET, agNULL);
      ostiInitiatorEvent(tiRoot,
                         tiPortalContext,
                         tiDeviceHandle,
                         tiIntrEventTypeTransportRecovery,
                         tiRecStarted,
                         agNULL
                        );
       
      return;
    }
    else
    {
      TI_DBG2(("tiINITransportRecovery: device reset expander attached\n"));
      tdsaPhyControlSend(tiRoot,
                         oneDeviceData, 
                         SMP_PHY_CONTROL_LINK_RESET, 
                         currentTaskTag,
                         tdsaRotateQnumber(tiRoot, oneDeviceData)
                        );
      ostiInitiatorEvent(tiRoot,
                         tiPortalContext,
                         tiDeviceHandle,
                         tiIntrEventTypeTransportRecovery,
                         tiRecStarted,
                         agNULL
                        );
      return;
    }
  }
  else
  {
    TI_DBG1(("tiINITransportRecovery: wrong device type %d\n", oneDeviceData->DeviceType));
  }
   
  
  return;
}
#endif

#if defined (INITIATOR_DRIVER) && defined (TARGET_DRIVER)
/*****************************************************************************
*! \brief  tdsaPhyControlSend
*
*  Purpose:  This function sends Phy Control to a device.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   phyId: Phy Identifier.
*  \param   queueNumber: bits 0-15:  inbound queue number.
*                        bits 16-31: outbound queue number.
*
*  \return:
*           Status
*
*   \note:
*
*****************************************************************************/
/* phyop of interest
SMP_PHY_CONTROL_HARD_RESET or SMP_PHY_CONTROL_CLEAR_AFFILIATION
if CurrentTaskTag == agNULL, clear affiliation
if CurrentTaskTag != agNULL, PHY_CONTROL (device reset)

*/
osGLOBAL bit32
tdsaPhyControlSend(
                   tiRoot_t             *tiRoot,
                   tdsaDeviceData_t     *oneDeviceData, /* taget disk */
                   bit8                 phyOp,
                   tiIORequest_t        *CurrentTaskTag,
                   bit32                queueNumber		   
                   )
{
  return 0;
}
#endif

#ifdef TARGET_DRIVER
/*****************************************************************************
*! \brief  tdsaPhyControlSend
*
*  Purpose:  This function sends Phy Control to a device.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   phyId: Phy Identifier.
*  \param   queueNumber: bits 0-15:  inbound queue number.
*                        bits 16-31: outbound queue number.
*
*  \return:
*           Status
*
*   \note:
*
*****************************************************************************/
/* phyop of interest
SMP_PHY_CONTROL_HARD_RESET or SMP_PHY_CONTROL_CLEAR_AFFILIATION
if CurrentTaskTag == agNULL, clear affiliation
if CurrentTaskTag != agNULL, PHY_CONTROL (device reset)

*/
osGLOBAL bit32
tdsaPhyControlSend(
                   tiRoot_t             *tiRoot,
                   tdsaDeviceData_t     *oneDeviceData, /* taget disk */
                   bit8                 phyOp,
                   tiIORequest_t        *CurrentTaskTag,
                   bit32                queueNumber		   
                   )
{
  return 0;
}
#endif


#ifdef INITIATOR_DRIVER                
/*****************************************************************************
*! \brief  tdsaPhyControlSend
*
*  Purpose:  This function sends Phy Control to a device.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   phyId: Phy Identifier.
*  \param   queueNumber: bits 0-15:  inbound queue number.
*                        bits 16-31: outbound queue number.
*
*  \return:
*           Status
*
*   \note:
*
*****************************************************************************/
/* phyop of interest
SMP_PHY_CONTROL_HARD_RESET or SMP_PHY_CONTROL_CLEAR_AFFILIATION
if CurrentTaskTag == agNULL, clear affiliation
if CurrentTaskTag != agNULL, PHY_CONTROL (device reset)

*/
osGLOBAL bit32
tdsaPhyControlSend(
                   tiRoot_t             *tiRoot,
                   tdsaDeviceData_t     *oneDeviceData, /* taget disk */
                   bit8                 phyOp,
                   tiIORequest_t        *CurrentTaskTag,
                   bit32                queueNumber		   
                   )
{
  agsaRoot_t            *agRoot;
  tdsaDeviceData_t      *oneExpDeviceData; 
  tdsaPortContext_t     *onePortContext;
  smpReqPhyControl_t    smpPhyControlReq;
  bit8                  phyID;
  bit32                 status;
  
  TI_DBG3(("tdsaPhyControlSend: start\n"));

  agRoot = oneDeviceData->agRoot;
  onePortContext = oneDeviceData->tdPortContext;
  oneExpDeviceData = oneDeviceData->ExpDevice;
  phyID = oneDeviceData->phyID;
  
  if (oneDeviceData->directlyAttached == agTRUE)
  {
    TI_DBG1(("tdsaPhyControlSend: Error!!! deivce is directly attached\n")); 
    return AGSA_RC_FAILURE;
  }
  if (onePortContext == agNULL)
  {
    TI_DBG1(("tdsaPhyControlSend: Error!!! portcontext is NULL\n"));
    return AGSA_RC_FAILURE;
  }

  if (oneExpDeviceData == agNULL)
  {
    TI_DBG1(("tdsaPhyControlSend: Error!!! expander is NULL\n"));
    return AGSA_RC_FAILURE;
  }
  
  if (phyOp == SMP_PHY_CONTROL_HARD_RESET)
  {
    TI_DBG3(("tdsaPhyControlSend: SMP_PHY_CONTROL_HARD_RESET\n"));
  }
  if (phyOp == SMP_PHY_CONTROL_LINK_RESET)
  {
    TI_DBG3(("tdsaPhyControlSend: SMP_PHY_CONTROL_LINK_RESET\n"));
  }
  if (phyOp == SMP_PHY_CONTROL_CLEAR_AFFILIATION)
  {
    TI_DBG3(("tdsaPhyControlSend: SMP_PHY_CONTROL_CLEAR_AFFILIATION\n"));
  }
  TI_DBG3(("tdsaPhyControlSend: target device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaPhyControlSend: target device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
  TI_DBG3(("tdsaPhyControlSend: expander AddrHi 0x%08x\n", oneExpDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaPhyControlSend: expander AddrLo 0x%08x\n", oneExpDeviceData->SASAddressID.sasAddressLo));
  TI_DBG3(("tdsaPhyControlSend: did %d expander did %d phyid %d\n", oneDeviceData->id, oneExpDeviceData->id, phyID));

  
  osti_memset(&smpPhyControlReq, 0, sizeof(smpReqPhyControl_t));

  /* fill in SMP payload */
  smpPhyControlReq.phyIdentifier = phyID;
  smpPhyControlReq.phyOperation = phyOp;
  
  status = tdSMPStart(
                      tiRoot,
                      agRoot,
                      oneExpDeviceData,
                      SMP_PHY_CONTROL,
                      (bit8 *)&smpPhyControlReq,
                      sizeof(smpReqPhyControl_t),
                      AGSA_SMP_INIT_REQ,
                      CurrentTaskTag,
                      queueNumber		      
                     );
  return status;
}
#endif
  
/*****************************************************************************
*! \brief  tdsaPhyControlFailureRespRcvd
*
*  Purpose:  This function processes the failure of Phy Control response.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   agRoot: Pointer to chip/driver Instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   frameHeader: Pointer to SMP frame header.
*  \param   frameHandle: A Handle used to refer to the response frame
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaPhyControlFailureRespRcvd(
                              tiRoot_t              *tiRoot,
                              agsaRoot_t            *agRoot,
                              tdsaDeviceData_t      *oneDeviceData,
                              tdssSMPFrameHeader_t  *frameHeader,
                              agsaFrameHandle_t     frameHandle,
                              tiIORequest_t         *CurrentTaskTag
                             )
{
#if defined(INITIATOR_DRIVER) || defined(TD_DEBUG_ENABLE)
  tdsaDeviceData_t      *TargetDeviceData = agNULL;
#endif   
#ifdef TD_DEBUG_ENABLE  
  satDeviceData_t       *pSatDevData = agNULL;
#endif  
//  agsaDevHandle_t       *agDevHandle = agNULL;
  
  TI_DBG1(("tdsaPhyControlFailureRespRcvd: start\n"));
 
  TI_DBG3(("tdsaPhyControlFailureRespRcvd: expander device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaPhyControlFailureRespRcvd: expander device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
  
  if (CurrentTaskTag != agNULL )
  {
    /* This was set in tiINITaskmanagement() */
#if defined(INITIATOR_DRIVER) || defined(TD_DEBUG_ENABLE)
    TargetDeviceData = (tdsaDeviceData_t *)CurrentTaskTag->tdData;
#endif    
#ifdef TD_DEBUG_ENABLE     
    pSatDevData = (satDeviceData_t *)&(TargetDeviceData->satDevData);
#endif    
//    agDevHandle = TargetDeviceData->agDevHandle; 
    TI_DBG2(("tdsaPhyControlFailureRespRcvd: target AddrHi 0x%08x\n", TargetDeviceData->SASAddressID.sasAddressHi));
    TI_DBG2(("tdsaPhyControlFailureRespRcvd: target AddrLo 0x%08x\n", TargetDeviceData->SASAddressID.sasAddressLo));

#ifdef TD_DEBUG_ENABLE     
    TI_DBG2(("tdsaPhyControlFailureRespRcvd: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    TI_DBG2(("tdsaPhyControlFailureRespRcvd: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
#endif    
  }
  
#ifdef INITIATOR_DRIVER    
  if (CurrentTaskTag != agNULL )
  {
    TI_DBG1(("tdsaPhyControlRespRcvd: callback to OS layer with failure\n"));
    if (TargetDeviceData->TRflag == agTRUE)
    {
      TargetDeviceData->TRflag = agFALSE;
      ostiInitiatorEvent(tiRoot,
                         TargetDeviceData->tdPortContext->tiPortalContext,
                         &(TargetDeviceData->tiDeviceHandle),
                         tiIntrEventTypeTransportRecovery,
                         tiRecFailed ,
                         agNULL
                        );
    }
    else
    {
      ostiInitiatorEvent( tiRoot,
                          NULL,
                          NULL,
                          tiIntrEventTypeTaskManagement,
                          tiTMFailed,
                          CurrentTaskTag );
    }                          
  }                          
#endif  
  return;
}
/*****************************************************************************
*! \brief  tdsaPhyControlRespRcvd
*
*  Purpose:  This function processes Phy Control response.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   agRoot: Pointer to chip/driver Instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   frameHeader: Pointer to SMP frame header.
*  \param   frameHandle: A Handle used to refer to the response frame
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaPhyControlRespRcvd(
                       tiRoot_t              *tiRoot,
                       agsaRoot_t            *agRoot,
                       agsaIORequest_t       *agIORequest,
                       tdsaDeviceData_t      *oneDeviceData,
                       tdssSMPFrameHeader_t  *frameHeader,
                       agsaFrameHandle_t     frameHandle,
                       tiIORequest_t         *CurrentTaskTag
                       )
{
#if defined(INITIATOR_DRIVER) || defined(TD_DEBUG_ENABLE)
  tdsaDeviceData_t      *TargetDeviceData = agNULL; 
#endif
#ifdef INITIATOR_DRIVER                
  satDeviceData_t       *pSatDevData = agNULL;
  agsaDevHandle_t       *agDevHandle = agNULL;
#endif  
  
  TI_DBG3(("tdsaPhyControlRespRcvd: start\n"));
 
  TI_DBG3(("tdsaPhyControlRespRcvd: expander device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaPhyControlRespRcvd: expander device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
  
  if (CurrentTaskTag != agNULL )
  {
    /* This was set in tiINITaskmanagement() */
#if defined(INITIATOR_DRIVER) || defined(TD_DEBUG_ENABLE)
    TargetDeviceData = (tdsaDeviceData_t *)CurrentTaskTag->tdData;
#endif
#ifdef INITIATOR_DRIVER                
    pSatDevData = (satDeviceData_t *)&(TargetDeviceData->satDevData);
    agDevHandle = TargetDeviceData->agDevHandle;
#endif     
    TI_DBG2(("tdsaPhyControlRespRcvd: target AddrHi 0x%08x\n", TargetDeviceData->SASAddressID.sasAddressHi));
    TI_DBG2(("tdsaPhyControlRespRcvd: target AddrLo 0x%08x\n", TargetDeviceData->SASAddressID.sasAddressLo));

#ifdef INITIATOR_DRIVER                
    TI_DBG2(("tdsaPhyControlRespRcvd: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    TI_DBG2(("tdsaPhyControlRespRcvd: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
#endif    
  }
   
#ifdef INITIATOR_DRIVER                
  /* no payload */
  if (frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    TI_DBG3(("tdsaPhyControlRespRcvd: SMP success\n"));
    
    /* warm reset or clear affiliation is done 
       call ostiInitiatorEvent()
    */
    if (CurrentTaskTag != agNULL )
    {
      TI_DBG3(("tdsaPhyControlRespRcvd: callback to OS layer with success\n"));
      pSatDevData->satDriveState = SAT_DEV_STATE_NORMAL;
      saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, TargetDeviceData), agDevHandle, SA_DS_OPERATIONAL);       
    
      if (TargetDeviceData->TRflag == agTRUE)
      {
        TargetDeviceData->TRflag = agFALSE;
        ostiInitiatorEvent(tiRoot,
                           TargetDeviceData->tdPortContext->tiPortalContext,
                           &(TargetDeviceData->tiDeviceHandle),
                           tiIntrEventTypeTransportRecovery,
                           tiRecOK,
                           agNULL
                          );
      }
      else
      {
        agDevHandle = TargetDeviceData->agDevHandle;
        if (agDevHandle == agNULL)
        {
          TI_DBG1(("tdsaPhyControlRespRcvd: wrong, agDevHandle is NULL\n"));      
        }
        ostiInitiatorEvent( tiRoot,
                            NULL,
                            NULL,
                            tiIntrEventTypeTaskManagement,
                            tiTMOK,
                            CurrentTaskTag );
      }                  
    }                          

  }
  else
  {
    TI_DBG1(("tdsaPhyControlRespRcvd: SMP failure; result %d\n", frameHeader->smpFunctionResult));
    /* warm reset or clear affiliation is done
    */
    if (CurrentTaskTag != agNULL )
    {
      TI_DBG1(("tdsaPhyControlRespRcvd: callback to OS layer with failure\n"));
      if (TargetDeviceData->TRflag == agTRUE)
      {
        TargetDeviceData->TRflag = agFALSE;
        ostiInitiatorEvent(tiRoot,
                           TargetDeviceData->tdPortContext->tiPortalContext,
                           &(TargetDeviceData->tiDeviceHandle),
                           tiIntrEventTypeTransportRecovery,
                           tiRecFailed ,
                           agNULL
                          );
      }
      else
      {
        ostiInitiatorEvent( tiRoot,
                            NULL,
                            NULL,
                            tiIntrEventTypeTaskManagement,
                            tiTMFailed,
                            CurrentTaskTag );
      }                          
    }                          
   
  }
#endif
  return;
}


#ifdef TARGET_DRIVER
/*****************************************************************************
*! \brief ttdsaAbortAll
*
*  Purpose:  This function is called to abort an all pending I/O request on a
*            device
*
*  \param  tiRoot:          Pointer to initiator driver/port instance.
*  \param  agRoot:          Pointer to chip/driver Instance.
*  \param  oneDeviceData:   Pointer to the device
*
*  \return: 
*
*          None
*
*****************************************************************************/
/*
  for abort itself,
  should we allocate tdAbortIORequestBody or get one from ttdsaXchg_t?
  Currently, we allocate tdAbortIORequestBody.
*/
osGLOBAL void
ttdsaAbortAll( 
             tiRoot_t                   *tiRoot,
             agsaRoot_t                 *agRoot,
             tdsaDeviceData_t           *oneDeviceData
             )
{
  agsaIORequest_t     *agAbortIORequest = agNULL;  
  tdIORequestBody_t   *tdAbortIORequestBody = agNULL;
  bit32               PhysUpper32;
  bit32               PhysLower32;
  bit32               memAllocStatus;
  void                *osMemHandle;

  TI_DBG3(("tdsaAbortAll: start\n"));
  
  TI_DBG3(("tdsaAbortAll: did %d\n", oneDeviceData->id));
  
  
  /* allocating agIORequest for abort itself */
  memAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &osMemHandle,
                                   (void **)&tdAbortIORequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(tdIORequestBody_t),
                                   agTRUE
                                   );
  if (memAllocStatus != tiSuccess)
  {
    /* let os process IO */
    TI_DBG1(("tdsaAbortAll: ostiAllocMemory failed...\n"));
    return;
  }
      
  if (tdAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    TI_DBG1(("tdsaAbortAll: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
    return;
  }
  
  /* setup task management structure */
  tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  /* setting callback */
  /* not needed; it is already set to be ossaSSPAbortCB() */
  tdAbortIORequestBody->IOCompletionFunc = ttdssIOAbortedHandler;
  
  tdAbortIORequestBody->tiDevHandle = (tiDeviceHandle_t *)&(oneDeviceData->tiDeviceHandle);
  
  /* initialize agIORequest */
  agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) tdAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */    
  
  /* SSPAbort */
  saSSPAbort(agRoot, 
             agAbortIORequest,
             0,
             oneDeviceData->agDevHandle,
             1, /* abort all */
             agNULL,
             agNULL 	     
             );
  return;
}
#endif /* TARGET_DRIVER */


osGLOBAL void 
tdsaDeregisterDevicesInPort(
                tiRoot_t             *tiRoot,
                tdsaPortContext_t    *onePortContext
               )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  agsaRoot_t        *agRoot = agNULL;
  
  agRoot = &(tdsaAllShared->agRootNonInt);

  TI_DBG1(("tdsaDeregisterDevicesInPort: start\n"));

  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdsaDeregisterDevicesInPort: oneDeviceData is NULL!!!\n"));
      return;
    }
    if (oneDeviceData->tdPortContext == onePortContext)
    {
      TI_DBG3(("tdsaDeregisterDevicesInPort: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      if ( !( DEVICE_IS_SMP_TARGET(oneDeviceData) && oneDeviceData->directlyAttached == agTRUE))
      {
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      else
      {
        TI_DBG1(("tdsaDeregisterDevicesInPort: keeping\n"));
        oneDeviceData->registered = agTRUE;      
      }	
     }
    DeviceListList = DeviceListList->flink;
  }
    
  TI_DBG3(("tdsaDeregisterDevicesInPort: end\n"));
  
  return;
}  							

/******************** for debugging only ***************************/
osGLOBAL void
tdsaPrintSwConfig(
                  agsaSwConfig_t *SwConfig
                  )
{
  if (SwConfig == agNULL)
  {
    TI_DBG6(("tdsaPrintSwConfig: SwConfig is NULL\n"));
    return;
  }
  else
  {
    TI_DBG6(("SwConfig->maxActiveIOs %d\n", SwConfig->maxActiveIOs));
    TI_DBG6(("SwConfig->smpReqTimeout %d\n", SwConfig->smpReqTimeout));
  }

  return;

}

osGLOBAL void
tdsaPrintHwConfig(
                  agsaHwConfig_t *HwConfig
                  )
{
  if  (HwConfig == agNULL)
  {
    TI_DBG6(("tdsaPrintHwConfig: HwConfig is NULL\n"));
    return;
  }
  else
  {
    TI_DBG6(("HwConfig->phyCount %d\n", HwConfig->phyCount));
  }
  return;
}

osGLOBAL void
tdssPrintSASIdentify(
                     agsaSASIdentify_t *id
                     )
{
  if  (id == agNULL)
  {
    TI_DBG1(("tdsaPrintSASIdentify: ID is NULL\n"));
    return;
  }
  else
  {
    TI_DBG6(("SASID->sspTargetPort %d\n", SA_IDFRM_IS_SSP_TARGET(id)?1:0));
    TI_DBG6(("SASID->stpTargetPort %d\n", SA_IDFRM_IS_STP_TARGET(id)?1:0));
    TI_DBG6(("SASID->smpTargetPort %d\n", SA_IDFRM_IS_SMP_TARGET(id)?1:0));
    TI_DBG6(("SASID->sspInitiatorPort %d\n", SA_IDFRM_IS_SSP_INITIATOR(id)?1:0));
    TI_DBG6(("SASID->stpInitiatorPort %d\n", SA_IDFRM_IS_STP_INITIATOR(id)?1:0));
    TI_DBG6(("SASID->smpInitiatorPort %d\n", SA_IDFRM_IS_SMP_INITIATOR(id)?1:0));
    TI_DBG6(("SASID->deviceType %d\n", SA_IDFRM_GET_DEVICETTYPE(id)));
    TI_DBG6(("SASID->sasAddressHi 0x%x\n", SA_IDFRM_GET_SAS_ADDRESSHI(id)));
    TI_DBG6(("SASID->sasAddressLo 0x%x\n", SA_IDFRM_GET_SAS_ADDRESSLO(id)));
    TI_DBG6(("SASID->phyIdentifier 0x%x\n", id->phyIdentifier));
    
  }
  
  return;
}

osGLOBAL void 
tdsaInitTimerHandler(
                     tiRoot_t  *tiRoot,
                     void      *timerData
                     )
{

  TI_DBG6(("tdsaInitTimerHandler: start\n"));
  return;
}

/*
  type: 1 portcontext 2 devicedata
  flag: 1 FreeLink 2 MainLink
*/

osGLOBAL void
print_tdlist_flink(tdList_t *hdr, int type, int flag)
{
  tdList_t *hdr_tmp1 = NULL;
#ifdef  TD_DEBUG_ENABLE
  tdsaPortContext_t *ele1;
#endif
#ifdef REMOVED
  tdsaDeviceData_t *ele2;
#endif
  hdr_tmp1 = hdr;

  if (type == 1 && flag == 1)
  {
    TI_DBG6(("PortContext and FreeLink\n"));
  }
  else if (type != 1 && flag == 1)
  {
    TI_DBG6(("DeviceData and FreeLink\n"));
  }
  else if (type == 1 && flag != 1)
  {
    TI_DBG6(("PortContext and MainLink\n"));
  }
  else
  {
    TI_DBG6(("DeviceData and MainLink\n"));
  }
  if (type == 1)
  {
    do
    {
      /* data structure type variable = (data structure type, file name, header of the tdList) */
      if (flag == 1)
      {
#ifdef  TD_DEBUG_ENABLE
        ele1 = TDLIST_OBJECT_BASE(tdsaPortContext_t, FreeLink, hdr_tmp1);
#endif
      }
      else
      {
#ifdef  TD_DEBUG_ENABLE
        ele1 = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, hdr_tmp1);
#endif
      }
      TI_DBG6(("flist ele %d\n", ele1->id));
      TI_DBG6(("flist ele %p\n", ele1));
      hdr_tmp1 = hdr_tmp1->flink;
    } while (hdr_tmp1 != hdr);
  }
  else
  {
    do
    {
      /* data structure type variable = (data structure type, file name, header of the tdList) */
#ifdef REMOVED
      if (flag == 1)
      {
        ele2 = TDLIST_OBJECT_BASE(tdsaDeviceData_t, FreeLink, hdr_tmp1);
      }
      else
      {
        ele2 = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, hdr_tmp1);
      }
      TI_DBG6(("flist ele %d\n", ele2->id));
      TI_DBG6(("flist ele %p\n", ele2));
#endif
      hdr_tmp1 = hdr_tmp1->flink;
    } while (hdr_tmp1 != hdr);
  }
  TI_DBG6(("\n"));
}

/* not verified yet. 6/15/2005 */
osGLOBAL void
print_tdlist_blink(tdList_t *hdr, int flag)
{
  tdList_t *hdr_tmp1 = NULL;
#ifdef REMOVED
  tdsaPortContext_t *ele1;
#endif
  hdr_tmp1 = hdr;

  do
  {
    /* data structure type variable = (data structure type, file name, header of the tdList) */
#ifdef REMOVED      
    if (flag == 1)
    {
      ele1 = TDLIST_OBJECT_BASE(tdsaPortContext_t, FreeLink, hdr_tmp1);
    }
    else
    {
      ele1 = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, hdr_tmp1);
    }
    TI_DBG6(("blist ele %d\n", ele1->id));
#endif    
    
    hdr_tmp1 = hdr_tmp1->blink;
  } while (hdr_tmp1 != hdr);
}


/** hexidecimal dump */
void tdhexdump(const char *ptitle, bit8 *pbuf, int len)
{
  int i;
  TI_DBG2(("%s - hexdump(len=%d):\n", ptitle, (int)len));
  if (!pbuf)
  {
    TI_DBG1(("pbuf is NULL\n"));
    return;
  }
  for (i = 0; i < len; )
  {
    if (len - i > 4)
    {
      TI_DBG2((" 0x%02x, 0x%02x, 0x%02x, 0x%02x,\n", pbuf[i], pbuf[i+1], pbuf[i+2], pbuf[i+3]));
      i += 4;
    }
    else
    {
      TI_DBG2((" 0x%02x,", pbuf[i]));
      i++;
    }
  }
  TI_DBG2(("\n"));
}

void
tdsaSingleThreadedEnter(tiRoot_t *ptiRoot, bit32 queueId)
{
  tdsaRoot_t * tiroot = agNULL;
  bit32 offset = 0;
  TD_ASSERT(ptiRoot,"ptiRoot");
  tiroot = ptiRoot->tdData;

  offset = tiroot->tdsaAllShared.MaxNumLLLocks + tiroot->tdsaAllShared.MaxNumOSLocks;
  
  ostiSingleThreadedEnter(ptiRoot, queueId + offset);
}

void
tdsaSingleThreadedLeave(tiRoot_t *ptiRoot, bit32 queueId)
{
  tdsaRoot_t * tiroot = agNULL;
  bit32 offset = 0;
  
  TD_ASSERT(ptiRoot,"ptiRoot");
  tiroot = ptiRoot->tdData;

  offset = tiroot->tdsaAllShared.MaxNumLLLocks + tiroot->tdsaAllShared.MaxNumOSLocks;
  
  ostiSingleThreadedLeave(ptiRoot, queueId + offset);
}

#ifdef PERF_COUNT
void
tdsaEnter(tiRoot_t *ptiRoot, int io)
{
  ostiEnter(ptiRoot, 1, io);
}

void
tdsaLeave(tiRoot_t *ptiRoot, int io)
{
  ostiLeave(ptiRoot, 1, io);
}
#endif

