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
 * This file contains initiator IO related functions in TD layer
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
*! \brief  tiINIIOStart
*
*   Purpose:  This routine is called to initiate a new SCSI request.
*
*  \param   tiRoot:           Pointer to initiator driver/port instance.
*  \param   tiIORequest:      Pointer to the I/O request context for this I/O.
*  \param   tiDeviceHandle:   Pointer to device handle for this I/O.
*  \param   tiScsiRequest:    Pointer to the SCSI-3 I/O request and SGL list.
*  \param   tiRequestBody:    Pointer to the OS Specific module allocated storage
*                             to be used by the TD layer for executing this I/O.
*  \param   interruptContext: The interrupt context within which this function
*                       is called.
*  \return:
*
*  tiSuccess:     I/O request successfully initiated.
*  tiBusy:        No resources available, try again later.
*  tiIONoDevice:  Invalid device handle.
*  tiError:       Other errors that prevent the I/O request to be started.
*
*
*****************************************************************************/
osGLOBAL bit32
tiINIIOStart(
             tiRoot_t                  *tiRoot,
             tiIORequest_t             *tiIORequest,
             tiDeviceHandle_t          *tiDeviceHandle,
             tiScsiInitiatorRequest_t  *tiScsiRequest,
             void                      *tiRequestBody,
             bit32                     interruptContext
             )
{
  tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaDeviceData_t          *oneDeviceData;
  agsaRoot_t                *agRoot = agNULL;
  agsaIORequest_t           *agIORequest = agNULL;
  agsaDevHandle_t           *agDevHandle = agNULL;
  bit32                     agRequestType;
  agsaSASRequestBody_t      *agSASRequestBody = agNULL;
  bit32                     tiStatus = tiError;
  bit32                     saStatus = AGSA_RC_FAILURE;

  tdIORequestBody_t         *tdIORequestBody;
  agsaSSPInitiatorRequest_t *agSSPInitiatorRequest;
#ifdef REMOVED
  /* only for debugging */
  bit32                      i;
#endif

#ifdef  SATA_ENABLE
#ifndef FDS_SM
  satIOContext_t            *satIOContext;
#endif
#endif
#ifdef FDS_SM
  smRoot_t                  *smRoot = &(tdsaAllShared->smRoot);
  smIORequest_t             *smIORequest;
  smDeviceHandle_t          *smDeviceHandle;
  smScsiInitiatorRequest_t  *smSCSIRequest;
#endif

  TDSA_INP_ENTER(tiRoot);
  TI_DBG6(("tiINIIOStart: start\n"));
  TI_DBG6(("tiINIIOStart:: ******* tdsaRoot %p tdsaAllShared %p \n", tdsaRoot,tdsaAllShared));

  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;

  TI_DBG6(("tiINIIOStart: onedevicedata %p\n", oneDeviceData));

  if(oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINIIOStart: tiDeviceHandle=%p DeviceData is NULL\n", tiDeviceHandle ));
    tiStatus = tiIONoDevice;
    goto ext;
  }

  /* for hotplug */
  if (oneDeviceData->valid != agTRUE || oneDeviceData->registered != agTRUE ||
      oneDeviceData->tdPortContext == agNULL )
  {
    TI_DBG1(("tiINIIOStart: tiDeviceHandle=%p did %d DeviceData was removed\n", tiDeviceHandle, oneDeviceData->id));
    TI_DBG6(("tiINIIOStart: device AddrHi 0x%08x AddrLo 0x%08x\n",
    oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));
    // for debugging
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;
    tdIORequestBody->IOCompletionFunc = itdssIOForDebugging1Completed;
    TI_DBG6(("tiINIIOStart: IOCompletionFunc %p\n", tdIORequestBody->IOCompletionFunc));
    tiStatus = tiIONoDevice;
    goto ext;
  }
#if 1
  if (tiIORequest->osData == agNULL)
  {
    TI_DBG1(("tiINIIOStart: tiIORequest->osData is NULL, wrong\n"));
  }
#endif

  /* starting IO with SAS device */
  if (oneDeviceData->DeviceType == TD_SAS_DEVICE)
  {
    TI_DBG6(("tiINIIOStart: calling saSSPStart\n"));

    agRoot = oneDeviceData->agRoot;
    agDevHandle = oneDeviceData->agDevHandle;

    /* OS layer has tdlayer data structure pointer in
       tdIORequestBody_t    tdIOReqBody;
       in ccb_t in agtiapi.h
    */
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;

    /* initialize */
    osti_memset(tdIORequestBody, 0, sizeof(tdIORequestBody_t));

    /* let's initialize tdIOrequestBody */
    /* initialize callback */
    tdIORequestBody->IOCompletionFunc = itdssIOCompleted;

    /* initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;

    /* initialize tiIORequest */
    tdIORequestBody->tiIORequest = tiIORequest;

    /* save context if we need to abort later */
    tiIORequest->tdData = tdIORequestBody;

    /* initialize expDataLength */
    tdIORequestBody->IOType.InitiatorRegIO.expDataLength
      = tiScsiRequest->scsiCmnd.expDataLength;

    tdIORequestBody->IOType.InitiatorRegIO.sglVirtualAddr
      = tiScsiRequest->sglVirtualAddr;

    /* initializes "agsaSgl_t   agSgl" of "agsaDifSSPInitiatorRequest_t" */
    tiStatus = itdssIOPrepareSGL(
                                 tiRoot,
                                 tdIORequestBody,
                                 &tiScsiRequest->agSgl1,
                                 tiScsiRequest->sglVirtualAddr
                                 );

    if (tiStatus != tiSuccess)
    {
      TI_DBG1(("tiINIIOStart: can't get SGL\n"));
      goto ext;
    }


    /* initialize agIORequest */
    agIORequest = &(tdIORequestBody->agIORequest);
    agIORequest->osData = (void *) tdIORequestBody;
    agIORequest->sdkData = agNULL; /* LL takes care of this */


    /*
      initialize
      tdIORequestBody_t tdIORequestBody -> agSASRequestBody
    */
    agSASRequestBody = &(tdIORequestBody->transport.SAS.agSASRequestBody);
    agSSPInitiatorRequest = &(agSASRequestBody->sspInitiatorReq);

    agSSPInitiatorRequest->flag = 0;

    /* copy cdb bytes */
    osti_memcpy(agSSPInitiatorRequest->sspCmdIU.cdb, tiScsiRequest->scsiCmnd.cdb, 16);

    /* copy lun field */
    osti_memcpy(agSSPInitiatorRequest->sspCmdIU.lun,
                tiScsiRequest->scsiCmnd.lun.lun, 8);


    /* setting the data length */
    agSSPInitiatorRequest->dataLength  = tiScsiRequest->scsiCmnd.expDataLength;
    TI_DBG6(("tiINIIOStart: tiScsiRequest->scsiCmnd.expDataLength %d\n", tiScsiRequest->scsiCmnd.expDataLength));

    agSSPInitiatorRequest->firstBurstSize = 0;

    /*
      process taskattribute
    */
    if (tiScsiRequest->scsiCmnd.taskAttribute == TASK_SIMPLE)
    {
      agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute = (bit8)
       agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute | TD_TASK_SIMPLE;
    }
    else if (tiScsiRequest->scsiCmnd.taskAttribute == TASK_ORDERED)
    {
      agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute = (bit8)
       agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute | TD_TASK_ORDERED;
    }
    else if (tiScsiRequest->scsiCmnd.taskAttribute == TASK_HEAD_OF_QUEUE)
    {
      agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute = (bit8)
       agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute | TD_TASK_HEAD_OF_QUEUE;
    }
    else if (tiScsiRequest->scsiCmnd.taskAttribute == TASK_ACA)
    {
      agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute = (bit8)
       agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute | TD_TASK_ACA;
    }

    if (tiScsiRequest->dataDirection == tiDirectionIn)
    {
      agRequestType = AGSA_SSP_INIT_READ;
      TI_DBG6(("tiINIIOStart: READ\n"));
    }
    else if (tiScsiRequest->dataDirection == tiDirectionOut)
    {
      agRequestType = AGSA_SSP_INIT_WRITE;
      TI_DBG6(("tiINIIOStart: WRITE\n"));
    }
    else
    {
      agRequestType = AGSA_REQ_TYPE_UNKNOWN;
      TI_DBG1(("tiINIIOStart: unknown data direction\n"));
    }

    tdIORequestBody->agRequestType = agRequestType;

    TI_DBG6(("tiINIIOStart: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG6(("tiINIIOStart: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

    /* for debugging */
    if (tdIORequestBody->IOCompletionFunc == agNULL)
    {
      TI_DBG1(("tiINIIOStart: Error!!!! IOCompletionFunc is NULL\n"));
    }
    saStatus = saSSPStart(agRoot,
                          agIORequest,
                          tdsaRotateQnumber(tiRoot, oneDeviceData),
                          agDevHandle,
                          agRequestType,
                          agSASRequestBody,
                          agNULL,
                          &ossaSSPCompleted);

    tdIORequestBody->ioStarted = agTRUE;
    tdIORequestBody->ioCompleted = agFALSE;
    tdIORequestBody->reTries = 0;

    if (saStatus == AGSA_RC_SUCCESS)
    {
      Initiator->NumIOsActive++;
      tiStatus = tiSuccess;
    }
    else
    {
      tdIORequestBody->ioStarted = agFALSE;
      tdIORequestBody->ioCompleted = agTRUE;
      if (saStatus == AGSA_RC_BUSY)
      {
        TI_DBG4(("tiINIIOStart: saSSPStart busy\n"));
        tiStatus = tiBusy;
      }
      else
      {
        tiStatus = tiError;
      }
      goto ext;
    }
  }
#ifdef FDS_SM
  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {
    TI_DBG5(("tiINIIOStart: calling satIOStart\n"));
    TI_DBG5(("tiINIIOStart: onedevicedata did %d\n", oneDeviceData->id));
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;
    /* initialize */
    osti_memset(tdIORequestBody, 0, sizeof(tdIORequestBody_t));
    /* initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;
    tdIORequestBody->superIOFlag = agFALSE;

    tiIORequest->tdData = tdIORequestBody;
    tdIORequestBody->tiIORequest = tiIORequest;
    smIORequest = (smIORequest_t *)&(tdIORequestBody->smIORequest);
    smIORequest->tdData = tdIORequestBody;

    smDeviceHandle = (smDeviceHandle_t *)&(oneDeviceData->smDeviceHandle);
    smDeviceHandle->tdData = oneDeviceData;

    smSCSIRequest = (smScsiInitiatorRequest_t *)&(tdIORequestBody->SM.smSCSIRequest);
    osti_memcpy(smSCSIRequest, tiScsiRequest, sizeof(smScsiInitiatorRequest_t));

    tiStatus = smIOStart(smRoot,
                         smIORequest,
                         smDeviceHandle,
                         smSCSIRequest,
                         interruptContext);
    /*
osGLOBAL bit32
smIOStart(
          smRoot_t          *smRoot,
          smIORequest_t         *smIORequest,
          smDeviceHandle_t      *smDeviceHandle,
          smScsiInitiatorRequest_t  *smSCSIRequest,
          bit32             interruptContext
         )


    */
  }
#else
  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {
    TI_DBG5(("tiINIIOStart: calling satIOStart\n"));
    TI_DBG5(("tiINIIOStart: onedevicedata did %d\n", oneDeviceData->id));

#ifdef  SATA_ENABLE
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;

    /* initialize */
    osti_memset(tdIORequestBody, 0, sizeof(tdIORequestBody_t));

    /* initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;

    /* initialize tiIORequest */
    tdIORequestBody->tiIORequest = tiIORequest;
    tdIORequestBody->IOCompletionFunc = itdssIOForDebugging2Completed;

    satIOContext = &(tdIORequestBody->transport.SATA.satIOContext);

    /*
     * Need to initialize all the fields within satIOContext except
     * reqType and satCompleteCB which will be set in sat.c depending on cmd.
     */
    tdIORequestBody->transport.SATA.tiSenseData.senseData = agNULL;
    tdIORequestBody->transport.SATA.tiSenseData.senseLen = 0;
    satIOContext->pSatDevData   = &oneDeviceData->satDevData;
    satIOContext->pFis          =
      &tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev;
    satIOContext->pScsiCmnd     = &tiScsiRequest->scsiCmnd;
    satIOContext->pSense        = &tdIORequestBody->transport.SATA.sensePayload;
    satIOContext->pTiSenseData  = &tdIORequestBody->transport.SATA.tiSenseData;
    satIOContext->pTiSenseData->senseData = satIOContext->pSense;
    /*    satIOContext->pSense = (scsiRspSense_t *)satIOContext->pTiSenseData->senseData; */
    satIOContext->tiRequestBody = tiRequestBody;
    satIOContext->interruptContext = interruptContext;
    satIOContext->ptiDeviceHandle = tiDeviceHandle;
    satIOContext->tiScsiXchg = tiScsiRequest;
    satIOContext->satIntIoContext  = agNULL;
    satIOContext->satOrgIOContext  = agNULL;
    /*    satIOContext->tiIORequest      = tiIORequest; */

    /* save context if we need to abort later */
    tiIORequest->tdData = tdIORequestBody;

    /* followings are used only for internal IO */
    satIOContext->currentLBA = 0;
    satIOContext->OrgTL = 0;

    TI_DBG5(("tiINIIOStart: pSatDevData=%p\n", satIOContext->pSatDevData ));

    tiStatus = satIOStart( tiRoot,
                           tiIORequest,
                           tiDeviceHandle,
                           tiScsiRequest,
                           satIOContext);
    goto ext;
#endif
  }
#endif /* else of FDS_SM */
  else
  {

    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;
    tdIORequestBody->IOCompletionFunc = itdssIOForDebugging3Completed;
    TI_DBG1(("tiINIIOStart: wrong unspported Device %d\n", oneDeviceData->DeviceType));
    /*
      error. unsupported IO
     */
  }
ext:
  TDSA_INP_LEAVE(tiRoot);
  return tiStatus;
}

#ifdef FAST_IO_TEST
osGLOBAL bit32
tiINIFastIOSend(void *ioh)
{
  bit32 saStatus, tiStatus;

  saStatus = saFastSSPSend(ioh);
  if (saStatus == AGSA_RC_SUCCESS)
    tiStatus = tiSuccess;
  else
    tiStatus = tiError;
  return tiStatus;
}

osGLOBAL bit32
tiINIFastIOCancel(void *ioh)
{
  bit32 saStatus, tiStatus;

  saStatus = saFastSSPCancel(ioh);
  if (saStatus == AGSA_RC_SUCCESS)
    tiStatus = tiSuccess;
  else
    tiStatus = tiError;
  return tiStatus;
}

osGLOBAL void*
tiINIFastIOPrepare(
            tiRoot_t          *tiRoot,
            void              *ioHandle,
            agsaFastCommand_t *fc)
{
  tdsaDeviceData_t *oneDeviceData;
  tiDeviceHandle_t *tiDeviceHandle = fc->devHandle;
  bit32            taskAttribute = fc->taskAttribute;
  void             *ioh = ioHandle;

  TDSA_INP_ENTER(tiRoot);
  TI_DBG6(("tiINIFastIOPrepare: enter\n"));

  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  if(oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINIFastIOPrepare: tiDeviceHandle=%p DeviceData is NULL\n",
             tiDeviceHandle));
    ioHandle = 0;
    TD_ASSERT((0), "");
    goto ext;
  }
  TI_DBG6(("tiINIFastIOPrepare: onedevicedata %p\n", oneDeviceData));

  /* starting IO with SAS device */
  if (oneDeviceData->DeviceType != TD_SAS_DEVICE)
  {
    TI_DBG1(("tiINISuperIOSend: wrong Device %d\n", oneDeviceData->DeviceType));
    /* error: unsupported IO */
    ioHandle = 0;
    TD_ASSERT((0), "");
    goto ext;
  }

  fc->agRoot = oneDeviceData->agRoot;
  TD_ASSERT((NULL != fc->agRoot), "");

  fc->devHandle = oneDeviceData->agDevHandle;
  TD_ASSERT((NULL != fc->devHandle), "");
  fc->safb->oneDeviceData = oneDeviceData;

  /*
    process taskattribute
  */
  switch (taskAttribute)
  {
    case TASK_SIMPLE:
      fc->taskAttribute = TD_TASK_SIMPLE;
      break;
    case TASK_ORDERED:
      fc->taskAttribute = TD_TASK_ORDERED;
      break;
    case TASK_HEAD_OF_QUEUE:
      fc->taskAttribute = TD_TASK_HEAD_OF_QUEUE;
      break;
    case TASK_ACA:
      fc->taskAttribute = TD_TASK_ACA;
      break;
      /* compile out for "iniload" */
  }


  TI_DBG3(("tiINIFastIOPrepare: data direction: %x\n", fc->agRequestType));
  TI_DBG6(("tiINIFastIOPrepare: device AddrHi/Lo 0x%08x / 0x%08x\n",
           oneDeviceData->SASAddressID.sasAddressHi,
           oneDeviceData->SASAddressID.sasAddressLo));

  fc->queueNum = tdsaRotateQnumber(tiRoot, oneDeviceData);

  ioHandle = saFastSSPPrepare(ioHandle, fc, ossaFastSSPCompleted, fc->safb);
  if (!ioHandle)
  {
    TI_DBG1(("tiINIFastIOPrepare: saSuperSSPSend error\n"));
    TD_ASSERT((0), "");
    //goto ext;
  }

ext:
  if (ioh && !ioHandle)
  {
    saFastSSPCancel(ioh);
  }

  TI_DBG6(("tiINIFastIOPrepare: leave\n"));

  TDSA_INP_LEAVE(tiRoot);
  return ioHandle;
} /* tiINIFastIOPrepare */
#endif

/*****************************************************************************
*
*   tiINIIOStartDif
*
*   Purpose:  This routine is called to initiate a new SCSI request with
*             DIF enable.
*
*   Parameters:
*     tiRoot:           Pointer to initiator driver/port instance.
*     tiIORequest:      Pointer to the I/O request context for this I/O.
*     tiDeviceHandle:   Pointer to device handle for this I/O.
*     tiScsiRequest:    Pointer to the SCSI-3 I/O request and SGL list.
*     tiRequestBody:    Pointer to the OS Specific module allocated storage
*                       to be used by the TD layer for executing this I/O.
*     interruptContext: The interrupt context within which this function
*                       is called.
*     difOption:        DIF option.
*
*  Return:
*
*  tiSuccess:     I/O request successfully initiated.
*  tiBusy:        No resources available, try again later.
*  tiIONoDevice:  Invalid device handle.
*  tiError:       Other errors that prevent the I/O request to be started.
*
*
*****************************************************************************/
osGLOBAL bit32 tiINIIOStartDif(
                        tiRoot_t                    *tiRoot,
                        tiIORequest_t               *tiIORequest,
                        tiDeviceHandle_t            *tiDeviceHandle,
                        tiScsiInitiatorRequest_t   *tiScsiRequest,
                        void                      *tiRequestBody,
                        bit32                       interruptContext,
                        tiDif_t                     *difOption
                        )
{

  /* This function was never used by SAS/SATA. Use tiINISuperIOStart() instead. */
  return tiBusy;
}


/*****************************************************************************
*
*   tiINISuperIOStart
*
*   Purpose:  This routine is called to initiate a new SCSI request.
*
*   Parameters:
*     tiRoot:           Pointer to initiator driver/port instance.
*     tiIORequest:      Pointer to the I/O request context for this I/O.
*     tiDeviceHandle:   Pointer to device handle for this I/O.
*     tiScsiRequest:    Pointer to the SCSI-3 I/O request and SGL list.
*     tiRequestBody:    Pointer to the OS Specific module allocated storage
*                       to be used by the TD layer for executing this I/O.
*     interruptContext: The interrupt context within which this function
*                       is called.
*  Return:
*
*  tiSuccess:     I/O request successfully initiated.
*  tiBusy:        No resources available, try again later.
*  tiIONoDevice:  Invalid device handle.
*  tiError:       Other errors that prevent the I/O request to be started.
*
*
*****************************************************************************/
osGLOBAL bit32
tiINISuperIOStart(
             tiRoot_t                       *tiRoot,
             tiIORequest_t                  *tiIORequest,
             tiDeviceHandle_t               *tiDeviceHandle,
             tiSuperScsiInitiatorRequest_t  *tiScsiRequest,
             void                           *tiRequestBody,
             bit32                          interruptContext
             )
{
  tdsaRoot_t                *tdsaRoot = agNULL;
  tdsaContext_t             *tdsaAllShared = agNULL;
  itdsaIni_t                *Initiator = agNULL;
  tdsaDeviceData_t          *oneDeviceData = agNULL;
  tdIORequestBody_t         *tdIORequestBody = agNULL;
  agsaSSPInitiatorRequest_t *agSSPInitiatorRequest = agNULL;
  agsaRoot_t                *agRoot = agNULL;
  agsaIORequest_t           *agIORequest = agNULL;
  agsaDevHandle_t           *agDevHandle = agNULL;
  agsaSASRequestBody_t      *agSASRequestBody = agNULL;
  bit32                     tiStatus = tiError;
  bit32                     saStatus = AGSA_RC_FAILURE;
  bit32                     adjusted_length = 0;
  bit32                     agRequestType   = 0;
  agBOOLEAN                 needPlusDataLenAdjustment = agFALSE;
  agBOOLEAN                 needMinusDataLenAdjustment = agFALSE;

#ifdef  SATA_ENABLE
#ifndef FDS_SM
  satIOContext_t            *satIOContext;
#endif
#endif
#ifdef FDS_SM
  smRoot_t                  *smRoot;
  smIORequest_t             *smIORequest;
  smDeviceHandle_t          *smDeviceHandle;
  smSuperScsiInitiatorRequest_t  *smSuperSCSIRequest;
#endif
#ifdef CCBUILD_INDIRECT_CDB
  agsaSSPInitiatorRequestIndirect_t *agSSPInitiatorIndRequest = agNULL;
#endif
  TD_ASSERT(tiRoot , "tiRoot");
  TD_ASSERT(tiIORequest, "tiIORequest");
  TD_ASSERT(tiDeviceHandle, "tiDeviceHandle");
  TD_ASSERT(tiRequestBody, "tiRequestBody");
  TD_ASSERT(tiRoot->tdData, "tiRoot->tdData");
  TD_ASSERT(tiDeviceHandle, "tiDeviceHandle");

  tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  TD_ASSERT(tdsaRoot, "tdsaRoot");

  tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  TD_ASSERT(tdsaAllShared, "tdsaAllShared");

  Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  TD_ASSERT(Initiator, "Initiator");

  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  TD_ASSERT(oneDeviceData, "oneDeviceData");


#ifdef FDS_SM
  smRoot = &(tdsaAllShared->smRoot);
  TD_ASSERT(smRoot , "smRoot");
#endif


  TI_DBG6(("tiINISuperIOStart: start\n"));
  TI_DBG6(("tiINISuperIOStart:: ******* tdsaRoot %p tdsaAllShared %p \n", tdsaRoot,tdsaAllShared));

  TI_DBG6(("tiINISuperIOStart: onedevicedata %p\n", oneDeviceData));

  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINISuperIOStart: tiDeviceHandle=%p DeviceData is NULL\n", tiDeviceHandle ));
    return tiIONoDevice;
  }

  /* for hotplug */
  if (oneDeviceData->valid != agTRUE || oneDeviceData->registered != agTRUE ||
      oneDeviceData->tdPortContext == agNULL )
  {
    TI_DBG1(("tiINISuperIOStart: tiDeviceHandle=%p did %d DeviceData was removed\n", tiDeviceHandle, oneDeviceData->id));
    TI_DBG6(("tiINISuperIOStart: device AddrHi 0x%08x AddrLo 0x%08x\n",
    oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));
    // for debugging
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;
    tdIORequestBody->IOCompletionFunc = itdssIOForDebugging1Completed;
    TI_DBG6(("tiINISuperIOStart: IOCompletionFunc %p\n", tdIORequestBody->IOCompletionFunc));
    return tiIONoDevice;
  }

#ifdef DBG
  if (tiIORequest->osData == agNULL)
  {
    TI_DBG1(("tiINISuperIOStart: tiIORequest->osData is NULL, wrong\n"));
    return tiError;
  }
#endif
  /* starting IO with SAS device */
  if (oneDeviceData->DeviceType == TD_SAS_DEVICE)
  {
    TI_DBG3(("tiINISuperIOStart: calling saSSPStart\n"));

    agRoot = oneDeviceData->agRoot;
    agDevHandle = oneDeviceData->agDevHandle;

    /* OS layer has tdlayer data structure pointer in tdIORequestBody_t  tdIOReqBody; in ccb_t in agtiapi.h */
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;

    /* initialize */
    /*the tdIORequestBody has been initialized in HwBuildIo routine */
    /*osti_memset(tdIORequestBody, 0, sizeof(tdIORequestBody_t));*/

    /* let's initialize tdIOrequestBody */
    /* initialize callback */
    tdIORequestBody->IOCompletionFunc = itdssIOCompleted;

    /* initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;

    /* initialize tiIORequest */
    tdIORequestBody->tiIORequest = tiIORequest;

    /* save context if we need to abort later */
    tiIORequest->tdData = tdIORequestBody;

    /* initialize expDataLength */
    tdIORequestBody->IOType.InitiatorRegIO.expDataLength
      = tiScsiRequest->scsiCmnd.expDataLength;

    tdIORequestBody->IOType.InitiatorRegIO.sglVirtualAddr
      = tiScsiRequest->sglVirtualAddr;

    /* initialize agIORequest */
    agIORequest = &(tdIORequestBody->agIORequest);
    agIORequest->osData = (void *) tdIORequestBody;

    /* initialize tdIORequestBody_t tdIORequestBody -> agSASRequestBody */
    agSASRequestBody = &(tdIORequestBody->transport.SAS.agSASRequestBody);
    agSSPInitiatorRequest = &(agSASRequestBody->sspInitiatorReq);

    agSSPInitiatorRequest->flag = 0;
    if (tiScsiRequest->flags & TI_SCSI_INITIATOR_ENCRYPT)
    {
      TI_DBG3(("tiINISuperIOStart: TI_SCSI_INITIATOR_ENCRYPT\n"));

      /*  Copy all of the relevant encrypt information */
      agSSPInitiatorRequest->flag |= AGSA_SAS_ENABLE_ENCRYPTION;
      TD_ASSERT( sizeof(tiEncrypt_t) == sizeof(agsaEncrypt_t) , "sizeof(tiEncrypt_t) == sizeof(agsaEncrypt_t)");
      osti_memcpy(&agSSPInitiatorRequest->encrypt, &tiScsiRequest->Encrypt, sizeof(agsaEncrypt_t));
    }

    if ((tiScsiRequest->flags & TI_SCSI_INITIATOR_DIF) &&
         (tiScsiRequest->scsiCmnd.cdb[0] == SCSIOPC_READ_10 ||
          tiScsiRequest->scsiCmnd.cdb[0] == SCSIOPC_WRITE_10 ||
          tiScsiRequest->scsiCmnd.cdb[0] == SCSIOPC_WRITE_6 ||
          tiScsiRequest->scsiCmnd.cdb[0] == SCSIOPC_READ_6 ||
          tiScsiRequest->scsiCmnd.cdb[0] == SCSIOPC_READ_12 ||
          tiScsiRequest->scsiCmnd.cdb[0] == SCSIOPC_WRITE_12 ||
          tiScsiRequest->scsiCmnd.cdb[0] == SCSIOPC_WRITE_16 ||
          tiScsiRequest->scsiCmnd.cdb[0] == SCSIOPC_READ_16 ))
    {
      TI_DBG3(("tiINISuperIOStart: TI_SCSI_INITIATOR_DIF\n"));
      /* Copy all of the relevant DIF information */
      agSSPInitiatorRequest->flag |= AGSA_SAS_ENABLE_DIF;
      osti_memcpy(&agSSPInitiatorRequest->dif, &tiScsiRequest->Dif, sizeof(agsaDif_t));

      /* Check if need to adjust dataLength. */
      switch (tiScsiRequest->dataDirection)
      {
      case tiDirectionOut: /* Write/Outbound */
          break;

      case tiDirectionIn:  /* Read/Inbound */
          if ((agSSPInitiatorRequest->dif.flags & DIF_ACTION_FLAG_MASK) == DIF_INSERT)
          {
              needPlusDataLenAdjustment = agTRUE;
          }
          break;
      }

      /* Set SGL data len XXX This code needs to support more sector sizes */
      /* Length adjustment for PCIe DMA only not SAS */
      if (needPlusDataLenAdjustment == agTRUE)
      {
        adjusted_length = tiScsiRequest->scsiCmnd.expDataLength;
        adjusted_length += (adjusted_length/512) * 8;
        agSSPInitiatorRequest->dataLength = adjusted_length;
      }
      else if (needMinusDataLenAdjustment == agTRUE)
      {
        adjusted_length = tiScsiRequest->scsiCmnd.expDataLength;
        adjusted_length -= (adjusted_length/520) * 8;
        agSSPInitiatorRequest->dataLength = adjusted_length;
      }
      else
      {
        /* setting the data length */
        agSSPInitiatorRequest->dataLength  = tiScsiRequest->scsiCmnd.expDataLength;
      }

      /* initializes "agsaSgl_t   agSgl" of "agsaDifSSPInitiatorRequest_t" */
      tiStatus = itdssIOPrepareSGL(
                                   tiRoot,
                                   tdIORequestBody,
                                   &tiScsiRequest->agSgl1,
                                   tiScsiRequest->sglVirtualAddr
                                   );
      TI_DBG2(("tiINISuperIOStart:TI_SCSI_INITIATOR_DIF needMinusDataLenAdjustment %d needPlusDataLenAdjustment %d difAction %X\n",
                   needMinusDataLenAdjustment,
                   needPlusDataLenAdjustment,
                   agSSPInitiatorRequest->dif.flags & DIF_ACTION_FLAG_MASK));

    }
    else
    {
      /* setting the data length */
      agSSPInitiatorRequest->dataLength  = tiScsiRequest->scsiCmnd.expDataLength;

      /* initializes "agsaSgl_t   agSgl" of "agsaSSPInitiatorRequest_t" */
      tiStatus = itdssIOPrepareSGL(
                                   tiRoot,
                                   tdIORequestBody,
                                   &tiScsiRequest->agSgl1,
                                   tiScsiRequest->sglVirtualAddr
                                   );
    }

    if (tiStatus != tiSuccess)
    {
      TI_DBG1(("tiINISuperIOStart: can't get SGL\n"));
      return tiStatus;
    }

    TI_DBG6(("tiINISuperIOStart: tiScsiRequest->scsiCmnd.expDataLength %d\n", tiScsiRequest->scsiCmnd.expDataLength));

    /* process taskattribute */
    if (tiScsiRequest->scsiCmnd.taskAttribute == TASK_SIMPLE)
    {
      agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute = (bit8)
       agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute | TD_TASK_SIMPLE;
    }
    else if (tiScsiRequest->scsiCmnd.taskAttribute == TASK_ORDERED)
    {
      agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute = (bit8)
       agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute | TD_TASK_ORDERED;
    }
    else if (tiScsiRequest->scsiCmnd.taskAttribute == TASK_HEAD_OF_QUEUE)
    {
      agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute = (bit8)
       agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute | TD_TASK_HEAD_OF_QUEUE;
    }
    else if (tiScsiRequest->scsiCmnd.taskAttribute == TASK_ACA)
    {
      agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute = (bit8)
       agSSPInitiatorRequest->sspCmdIU.efb_tp_taskAttribute | TD_TASK_ACA;
    }

    /* copy cdb bytes */
    osti_memcpy(agSSPInitiatorRequest->sspCmdIU.cdb, tiScsiRequest->scsiCmnd.cdb, 16);
    /* copy lun field */
    osti_memcpy(agSSPInitiatorRequest->sspCmdIU.lun, tiScsiRequest->scsiCmnd.lun.lun, 8);
#ifdef CCBUILD_INDIRECT_CDB
    /* check the Indirect CDB flag */
    if (tiScsiRequest->flags & TI_SCSI_INITIATOR_INDIRECT_CDB)
    {
      /* Indirect CDB */
      if (tiScsiRequest->dataDirection == tiDirectionIn)
      {
        agRequestType = AGSA_SSP_INIT_READ_INDIRECT;
        TI_DBG6(("tiINISuperIOStart: Indirect READ\n"));
      }
      else if (tiScsiRequest->dataDirection == tiDirectionOut)
      {
        agRequestType = AGSA_SSP_INIT_WRITE_INDIRECT;
        TI_DBG6(("tiINISuperIOStart: Indirect WRITE\n"));
      }
      else
      {
        agRequestType = AGSA_REQ_TYPE_UNKNOWN;
        TI_DBG1(("tiINISuperIOStart: unknown data direction\n"));
      }
      agSSPInitiatorIndRequest = &(agSASRequestBody->sspInitiatorReqIndirect);
      /* copy the constructed SSPIU info to indirect SSPIU buffer */
      osti_memcpy(tiScsiRequest->IndCDBBuffer, &agSSPInitiatorRequest->sspCmdIU, sizeof(agsaSSPCmdInfoUnit_t));
      /* initialize the indirect CDB buffer address and length */
      agSSPInitiatorIndRequest->sspInitiatorReqAddrLower32 = tiScsiRequest->IndCDBLowAddr;
      agSSPInitiatorIndRequest->sspInitiatorReqAddrUpper32 = tiScsiRequest->IndCDBHighAddr;
      agSSPInitiatorIndRequest->sspInitiatorReqLen         = sizeof(agsaSSPCmdInfoUnit_t);
    }
    else
#endif //CCBUILD_INDIRECT_CDB
    {
      /* Direct CDB */
      if (tiScsiRequest->dataDirection == tiDirectionIn)
      {
        agRequestType = AGSA_SSP_INIT_READ;
        TI_DBG6(("tiINISuperIOStart: READ\n"));
      }
      else if (tiScsiRequest->dataDirection == tiDirectionOut)
      {
        agRequestType = AGSA_SSP_INIT_WRITE;
        TI_DBG6(("tiINISuperIOStart: WRITE\n"));
      }
      else
      {
        agRequestType = AGSA_REQ_TYPE_UNKNOWN;
        TI_DBG1(("tiINISuperIOStart: unknown data direction\n"));
      }
    }

    tdIORequestBody->agRequestType = agRequestType;
   
    TI_DBG6(("tiINISuperIOStart: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG6(("tiINISuperIOStart: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

#ifdef DBG
    /* for debugging */
    if (tdIORequestBody->IOCompletionFunc == agNULL)
    {
      TI_DBG1(("tiINISuperIOStart: Error!!!! IOCompletionFunc is NULL\n"));
      return tiError;
    }
#endif
    saStatus = saSSPStart(agRoot,
                          agIORequest,
                          tdsaRotateQnumber(tiRoot, oneDeviceData),
                          agDevHandle,
                          agRequestType,
                          agSASRequestBody,
                          agNULL,
                          &ossaSSPCompleted);

    if (saStatus == AGSA_RC_SUCCESS)
    {
      Initiator->NumIOsActive++;
      tdIORequestBody->ioStarted = agTRUE;
      tdIORequestBody->ioCompleted = agFALSE;
      tiStatus = tiSuccess;
    }
    else
    {
      tdIORequestBody->ioStarted = agFALSE;
      tdIORequestBody->ioCompleted = agTRUE;
      if (saStatus == AGSA_RC_BUSY)
      {
        TI_DBG4(("tiINISuperIOStart: saSSPStart busy\n"));
        tiStatus = tiBusy;
      }
      else
      {
        tiStatus = tiError;
      }
      return tiStatus;
    }
  }
#ifdef FDS_SM
  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {
    TI_DBG5(("tiINISuperIOStart: calling satIOStart\n"));
    TI_DBG5(("tiINISuperIOStart: onedevicedata did %d\n", oneDeviceData->id));
    TI_DBG5(("tiINISuperIOStart: SATA sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG5(("tiINISuperIOStart: SATA sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
    
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;
    /* initialize */
    /* the tdIORequestBody has been initialized by Storport in SRB Extension */
    /*osti_memset(tdIORequestBody, 0, sizeof(tdIORequestBody_t));*/
    /* initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;
    tdIORequestBody->superIOFlag = agTRUE;

    tiIORequest->tdData = tdIORequestBody;
    tdIORequestBody->tiIORequest = tiIORequest;
    smIORequest = (smIORequest_t *)&(tdIORequestBody->smIORequest);
    smIORequest->tdData = tdIORequestBody;
    smIORequest->smData = &tdIORequestBody->smIORequestBody;

    smDeviceHandle = (smDeviceHandle_t *)&(oneDeviceData->smDeviceHandle);
    smDeviceHandle->tdData = oneDeviceData;

    smSuperSCSIRequest = (smSuperScsiInitiatorRequest_t *)&(tdIORequestBody->SM.smSuperSCSIRequest);
    osti_memcpy(smSuperSCSIRequest, tiScsiRequest, sizeof(smSuperScsiInitiatorRequest_t));

    tiStatus = smSuperIOStart(smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smSuperSCSIRequest,
                              oneDeviceData->SASAddressID.sasAddressHi,			      
                              oneDeviceData->SASAddressID.sasAddressLo,
                              interruptContext);

  }
#else
  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {

    TI_DBG5(("tiINISuperIOStart: calling satIOStart\n"));
    TI_DBG5(("tiINISuperIOStart: onedevicedata did %d\n", oneDeviceData->id));

#ifdef  SATA_ENABLE
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;

    /* initialize */
    osti_memset(tdIORequestBody, 0, sizeof(tdIORequestBody_t));

    /* initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;

    /* initialize tiIORequest */
    tdIORequestBody->tiIORequest = tiIORequest;
    tdIORequestBody->IOCompletionFunc = itdssIOForDebugging2Completed;

    satIOContext = &(tdIORequestBody->transport.SATA.satIOContext);

    /*
     * Need to initialize all the fields within satIOContext except
     * reqType and satCompleteCB which will be set in sat.c depending on cmd.
     */
    tdIORequestBody->transport.SATA.tiSenseData.senseData = agNULL;
    tdIORequestBody->transport.SATA.tiSenseData.senseLen = 0;
    satIOContext->pSatDevData   = &oneDeviceData->satDevData;
    satIOContext->pFis          =
      &tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev;
    satIOContext->pScsiCmnd     = &tiScsiRequest->scsiCmnd;
    satIOContext->pSense        = &tdIORequestBody->transport.SATA.sensePayload;
    satIOContext->pTiSenseData  = &tdIORequestBody->transport.SATA.tiSenseData;
    satIOContext->pTiSenseData->senseData = satIOContext->pSense;
    /*    satIOContext->pSense = (scsiRspSense_t *)satIOContext->pTiSenseData->senseData; */
    satIOContext->tiRequestBody = tiRequestBody;
    satIOContext->interruptContext = interruptContext;
    satIOContext->ptiDeviceHandle = tiDeviceHandle;
    /*
     This code uses a kludge for the tiScsiXchg. Many subroutines in the SATA code
     require a tiScsiInitiatorRequest. Since it would be a lot of work to replicate
     those functions for a tiSuperScsiInitiatorRequest, we will use a short cut.
     The standard pointer will be passed, but the superIOFlag marks the real type of the structure.
    */
    satIOContext->tiScsiXchg = tiScsiRequest;
    satIOContext->superIOFlag = agTRUE;

    satIOContext->satIntIoContext  = agNULL;
    satIOContext->satOrgIOContext  = agNULL;
    /*    satIOContext->tiIORequest      = tiIORequest; */

    /* save context if we need to abort later */
    tiIORequest->tdData = tdIORequestBody;

    /* followings are used only for internal IO */
    satIOContext->currentLBA = 0;
    satIOContext->OrgTL = 0;

    TI_DBG5(("tiINISuperIOStart: pSatDevData=%p\n", satIOContext->pSatDevData ));

    tiStatus = satIOStart( tiRoot,
                           tiIORequest,
                           tiDeviceHandle,
                           satIOContext->tiScsiXchg,
                           satIOContext);

    return tiStatus;
#endif
  }
#endif /* else of FDS_SM */

  else
  {

    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;
    tdIORequestBody->IOCompletionFunc = itdssIOForDebugging3Completed;
    TI_DBG1(("tiINISuperIOStart: wrong unspported Device %d\n", oneDeviceData->DeviceType));
    /*
      error. unsupported IO
     */
  }
  return tiStatus;
}

osGLOBAL bit32
tiINISMPStart(
       tiRoot_t                  *tiRoot,
       tiIORequest_t             *tiIORequest,
       tiDeviceHandle_t          *tiDeviceHandle,
       tiSMPFrame_t              *tiSMPFrame,
       void                      *tiSMPBody,
       bit32                     interruptContext
       )
{
  tdsaDeviceData_t          *oneDeviceData;
  agsaIORequest_t           *agIORequest = agNULL;
  tdIORequestBody_t         *tdSMPRequestBody = agNULL;
  agsaRoot_t                *agRoot = agNULL;
  agsaDevHandle_t           *agDevHandle = agNULL;
  agsaSASRequestBody_t      *agRequestBody = agNULL;
  agsaSMPFrame_t            *agSMPFrame = agNULL;
  bit32                     agRequestType;
  bit32                     tiStatus = tiError;
  bit32                     saStatus = AGSA_RC_FAILURE;
  bit32                     queueNum;
  TDSA_INP_ENTER(tiRoot);
    TI_DBG6(("tiINISMPStart: start\n"));
    oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  TI_DBG6(("tiINISMPStart: onedevicedata %p\n", oneDeviceData));
    TI_DBG6(("tiINISMPStart: tiDeviceHandle %p\n", tiDeviceHandle));
  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINISMPStart: tiDeviceHandle=%p Expander DeviceData is NULL\n", tiDeviceHandle ));
    return tiError;
  }
  if (tiIORequest->osData == agNULL)
  {
    TI_DBG1(("tiINISMPStart: tiIORequest->osData is NULL, wrong\n"));
    return tiError;
  }
  agRoot = oneDeviceData->agRoot;
  agDevHandle = oneDeviceData->agDevHandle;
  tdSMPRequestBody = (tdIORequestBody_t *)tiSMPBody;
  tdSMPRequestBody->tiIORequest = tiIORequest;
  tiIORequest->tdData = tdSMPRequestBody;
  agIORequest = &(tdSMPRequestBody->agIORequest);
  agIORequest->osData = (void *) tdSMPRequestBody;
  agRequestBody = &(tdSMPRequestBody->transport.SAS.agSASRequestBody);
  agSMPFrame = &(agRequestBody->smpFrame);
  if (!DEVICE_IS_SMP_TARGET(oneDeviceData))
  {
    TI_DBG1(("tiINISMPStart: Target Device is not SMP device\n"));
    return tiError;
  }
  if (tiSMPFrame->flag == 0) // define DIRECT SMP at td layer?
  {
    TI_DBG6(("tiINISMPStart: Direct SMP\n"));
    agSMPFrame->outFrameBuf = tiSMPFrame->outFrameBuf;
    agSMPFrame->outFrameLen = tiSMPFrame->outFrameLen;
    tdhexdump("tiINISMPStart agSMPFrame", (bit8 *)agSMPFrame->outFrameBuf, agSMPFrame->outFrameLen);
    agSMPFrame->expectedRespLen = tiSMPFrame->expectedRespLen;
    agSMPFrame->inFrameLen = 0;
    agSMPFrame->flag = tiSMPFrame->flag;
    agRequestType = AGSA_SMP_INIT_REQ;
    queueNum = 0;
    saStatus = saSMPStart(agRoot,
                agIORequest,
                queueNum,
                agDevHandle,
                agRequestType,
                agRequestBody,
                &ossaSMPCAMCompleted
               );
    if (saStatus == AGSA_RC_SUCCESS)
    {
      tiStatus = tiSuccess;
    }
    else
    {
      if (saStatus == AGSA_RC_BUSY)
      {
        TI_DBG1(("tiINISMPStart: saSSPStart busy\n"));
        tiStatus = tiBusy;
      }
      else
      {
        TI_DBG1(("tiINISMPStart: saSSPStart error\n"));
        tiStatus = tiError;
      }
      return tiStatus;
    }
  }
  else
  {
    TI_DBG1(("tiINISMPStart: Indirect SMP! Not supported yet\n"));
    tiStatus = tiError;
  }
  return tiStatus;
}
#ifdef TD_INT_COALESCE
osGLOBAL bit32
tiINIIOStartIntCoalesce(
             tiRoot_t                  *tiRoot,
             tiIORequest_t             *tiIORequest,
             tiDeviceHandle_t          *tiDeviceHandle,
             tiScsiInitiatorRequest_t *tiScsiRequest,
             void                      *tiRequestBody,
             bit32                     interruptContext,
             tiIntCoalesceContext_t    *tiIntCoalesceCxt
             )
{
  tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaDeviceData_t          *oneDeviceData;
  agsaRoot_t                *agRoot = agNULL;
  agsaIORequest_t           *agIORequest = agNULL;
  agsaDevHandle_t           *agDevHandle = agNULL;
  bit32                     agRequestType;
  agsaSASRequestBody_t      *agSASRequestBody = agNULL;
  bit32                     tiStatus = tiError;
  bit32                     saStatus = AGSA_RC_FAILURE;

  tdIORequestBody_t         *tdIORequestBody;
  agsaSSPInitiatorRequest_t *agSSPInitiatorRequest;
  tdsaIntCoalesceContext_t  *tdsaIntCoalCxt;
  agsaIntCoalesceContext_t  *agIntCoalCxt;

  TI_DBG1(("tiINIIOStartIntCoalesce: start\n"));
  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;

  TI_DBG6(("tiINIIOStartIntCoalesce: onedevicedata %p\n", oneDeviceData));

  if(oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINIIOStartIntCoalesce: tiDeviceHandle=%p DeviceData is NULL\n", tiDeviceHandle ));
    return tiIONoDevice;
  }

  /* starting IO with SAS device */
  if (oneDeviceData->DeviceType == TD_SAS_DEVICE)
  {
    TI_DBG6(("tiINIIOStartIntCoalesce: calling saSSPStart\n"));

    agRoot = oneDeviceData->agRoot;
    agDevHandle = oneDeviceData->agDevHandle;

    /* OS layer has tdlayer data structure pointer in
       tdIORequestBody_t    tdIOReqBody;
       in ccb_t in agtiapi.h
    */
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;

    /* let's initialize tdIOrequestBody */
    /* initialize callback */
    tdIORequestBody->IOCompletionFunc = itdssIOCompleted;

    /* initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;

    /* initialize tiIORequest */
    tdIORequestBody->tiIORequest = tiIORequest;

    /* save context if we need to abort later */
    tiIORequest->tdData = tdIORequestBody;

    /* initialize expDataLength */
    tdIORequestBody->IOType.InitiatorRegIO.expDataLength
      = tiScsiRequest->scsiCmnd.expDataLength;

    /* initializes "agsaSgl_t   agSgl" of "agsaDifSSPInitiatorRequest_t" */
    tiStatus = itdssIOPrepareSGL(
                                 tiRoot,
                                 tdIORequestBody,
                                 &tiScsiRequest->agSgl1,
                                 tiScsiRequest->sglVirtualAddr
                                 );

    if (tiStatus != tiSuccess)
    {
      TI_DBG1(("tiINIIOStartIntCoalesce: can't get SGL\n"));
      return tiStatus;
    }


    /* initialize agIORequest */
    agIORequest = &(tdIORequestBody->agIORequest);
    agIORequest->osData = (void *) tdIORequestBody;
    agIORequest->sdkData = agNULL; /* LL takes care of this */


    /*
      initialize
      tdIORequestBody_t tdIORequestBody -> agSASRequestBody
    */
    agSASRequestBody = &(tdIORequestBody->transport.SAS.agSASRequestBody);
    agSSPInitiatorRequest = &(agSASRequestBody->sspInitiatorReq);


    /* copy cdb bytes */
    osti_memcpy(agSSPInitiatorRequest->sspCmdIU.cdb, tiScsiRequest->scsiCmnd.cdb, 16);

    /* copy lun field */
    osti_memcpy(agSSPInitiatorRequest->sspCmdIU.lun,
                tiScsiRequest->scsiCmnd.lun.lun, 8);

    /* setting the data length */
    agSSPInitiatorRequest->dataLength  = tiScsiRequest->scsiCmnd.expDataLength;
    TI_DBG6(("tiINIIOStartIntCoalesce: tiScsiRequest->scsiCmnd.expDataLength %d\n", tiScsiRequest->scsiCmnd.expDataLength));

    agSSPInitiatorRequest->firstBurstSize = 0;

    if (tiScsiRequest->dataDirection == tiDirectionIn)
    {
      agRequestType = AGSA_SSP_INIT_READ;
      TI_DBG6(("tiINIIOStartIntCoalesce: READ\n"));
    }
    else if (tiScsiRequest->dataDirection == tiDirectionOut)
    {
      agRequestType = AGSA_SSP_INIT_WRITE;
      TI_DBG6(("tiINIIOStartIntCoalesce: WRITE\n"));
    }
    else
    {
      agRequestType = AGSA_REQ_TYPE_UNKNOWN;
      TI_DBG1(("tiINIIOStartIntCoalesce: unknown data direction\n"));
    }

    tdIORequestBody->agRequestType = agRequestType;

    tdsaIntCoalCxt = (tdsaIntCoalesceContext_t *)tiIntCoalesceCxt->tdData;
    agIntCoalCxt = &(tdsaIntCoalCxt->agIntCoalCxt);


   
#ifdef LL_INT_COALESCE
    saStatus = saSSPStartIntCoalesce(agRoot,
                                     agIORequest,
                                     agIntCoalCxt,
                                     agDevHandle,
                                     agRequestType,
                                     agSASRequestBody,
                                     &ossaSSPCompleted);
#endif

    tdIORequestBody->ioStarted = agTRUE;
    tdIORequestBody->ioCompleted = agFALSE;

    if (saStatus == AGSA_RC_SUCCESS)
    {
      Initiator->NumIOsActive++;
      tiStatus = tiSuccess;
    }
    else
    {
      TI_DBG1(("tiINIIOStartIntCoalesce: saSSPStart failed\n"));
      tdIORequestBody->ioStarted = agFALSE;
      tdIORequestBody->ioCompleted = agTRUE;
      if (saStatus == AGSA_RC_BUSY)
      {
        tiStatus = tiBusy;
      }
      else
      {
        tiStatus = tiError;
      }
      return tiStatus;
    }
  }

  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {
    /*
      satIOStart() -> saSATAStartIntCoalesce()
    */
    TI_DBG1(("tiINIIOStartIntCoalesce: SATA not supported yet\n"));
    return tiStatus;
  }
  else
  {
    TI_DBG1(("tiINIIOStartIntCoalesce: wrong unspported Device %d\n", oneDeviceData->DeviceType));
    /*
      error. unsupported IO
     */
  }
  return tiStatus;


}

osGLOBAL bit32
tiINIIOStartIntCoalesceDif(
                           tiRoot_t                  *tiRoot,
                           tiIORequest_t             *tiIORequest,
                           tiDeviceHandle_t          *tiDeviceHandle,
                           tiScsiInitiatorRequest_t *tiScsiRequest,
                           void                      *tiRequestBody,
                           bit32                     interruptContext,
                           tiIntCoalesceContext_t    *tiIntCoalesceCxt,
                           tiDif_t                   *difOption
                           )
{
  tdsaRoot_t                   *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t                *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                   *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaDeviceData_t             *oneDeviceData;
  agsaRoot_t                   *agRoot = agNULL;
  agsaIORequest_t              *agIORequest = agNULL;
  agsaDevHandle_t              *agDevHandle = agNULL;
  bit32                        agRequestType;
  agsaDifSSPRequestBody_t      *agEdcSSPRequestBody = agNULL;
  bit32                        tiStatus = tiError;
  bit32                        saStatus = AGSA_RC_FAILURE;

  tdIORequestBody_t            *tdIORequestBody;
  agsaDifSSPInitiatorRequest_t *agEdcSSPInitiatorRequest;
  agsaDif_t                    *agEdc;
  bit32                        agUpdateMask = 0;
  bit32                        agVerifyMask = 0;
  tdsaIntCoalesceContext_t     *tdsaIntCoalCxt;
  agsaIntCoalesceContext_t     *agIntCoalCxt;

  TI_DBG1(("tiINIIOStartIntCoalesceDif: start\n"));
  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;

  TI_DBG6(("tiINIIOStartIntCoalesceDif: onedevicedata %p\n", oneDeviceData));

  if(oneDeviceData == agNULL)
  {
    TI_DBG1(("tiINIIOStartIntCoalesceDif: tiDeviceHandle=%p DeviceData is NULL\n", tiDeviceHandle ));
    return tiIONoDevice;
  }

  /* starting IO with SAS device */
  if (oneDeviceData->DeviceType == TD_SAS_DEVICE)
  {
    TI_DBG6(("tiINIIOStartIntCoalesceDif: calling saSSPStart\n"));

    agRoot = oneDeviceData->agRoot;
    agDevHandle = oneDeviceData->agDevHandle;

    /* OS layer has tdlayer data structure pointer in
       tdIORequestBody_t    tdIOReqBody;
       in ccb_t in agtiapi.h
    */
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;

    /* let's initialize tdIOrequestBody */
    /* initialize callback */
    tdIORequestBody->IOCompletionFunc = itdssIOCompleted;

    /* initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;

    /* initialize tiIORequest */
    tdIORequestBody->tiIORequest = tiIORequest;

    /* save context if we need to abort later */
    tiIORequest->tdData = tdIORequestBody;

    /* initialize expDataLength */
    tdIORequestBody->IOType.InitiatorRegIO.expDataLength
      = tiScsiRequest->scsiCmnd.expDataLength;

    /* initializes "agsaSgl_t   agSgl" of "agsaDifSSPInitiatorRequest_t" */
    tiStatus = itdssIOPrepareSGL(
                                 tiRoot,
                                 tdIORequestBody,
                                 &tiScsiRequest->agSgl1,
                                 tiScsiRequest->sglVirtualAddr
                                 );

    if (tiStatus != tiSuccess)
    {
      TI_DBG1(("tiINIIOStartIntCoalesceDif: can't get SGL\n"));
      return tiStatus;
    }


    /* initialize agIORequest */
    agIORequest = &(tdIORequestBody->agIORequest);
    agIORequest->osData = (void *) tdIORequestBody;
    agIORequest->sdkData = agNULL; /* LL takes care of this */


    /*
      initialize
      tdIORequestBody_t tdIORequestBody -> agSASRequestBody
    */
    agEdcSSPRequestBody = &(tdIORequestBody->transport.SAS.agEdcSSPRequestBody);
    agEdcSSPInitiatorRequest = &(agEdcSSPRequestBody->edcSSPInitiatorReq);


    /* copy cdb bytes */
    osti_memcpy(agEdcSSPInitiatorRequest->sspCmdIU.cdb, tiScsiRequest->scsiCmnd.cdb, 16);

    /* copy lun field */
    osti_memcpy(agEdcSSPInitiatorRequest->sspCmdIU.lun,
                tiScsiRequest->scsiCmnd.lun.lun, 8);


    /* setting the data length */
    agEdcSSPInitiatorRequest->dataLength  = tiScsiRequest->scsiCmnd.expDataLength;
    TI_DBG6(("tiINIIOStartIntCoalesceDif: tiScsiRequest->scsiCmnd.expDataLength %d\n", tiScsiRequest->scsiCmnd.expDataLength));

    agEdcSSPInitiatorRequest->firstBurstSize = 0;


    if (tiScsiRequest->dataDirection == tiDirectionIn)
    {
      agRequestType = AGSA_SSP_INIT_READ;
      TI_DBG1(("tiINIIOStartIntCoalesceDif: READ difAction %X\n",difOption->difAction));
    }
    else if (tiScsiRequest->dataDirection == tiDirectionOut)
    {
      agRequestType = AGSA_SSP_INIT_WRITE;
      TI_DBG1(("tiINIIOStartIntCoalesceDif: WRITE difAction %X\n",difOption->difAction));
    }
    else
    {
      agRequestType = AGSA_REQ_TYPE_UNKNOWN;
      TI_DBG1(("tiINIIOStartIntCoalesceDif: unknown data direction\n"));
    }

    tdIORequestBody->agRequestType = agRequestType;

    /* process interrupt coalesce context */
    tdsaIntCoalCxt = (tdsaIntCoalesceContext_t *)tiIntCoalesceCxt->tdData;
    agIntCoalCxt = &(tdsaIntCoalCxt->agIntCoalCxt);

    /* process DIF */

    agEdc = &(agEdcSSPInitiatorRequest->edc);

    osti_memset(agEdc, 0, sizeof(agsaDif_t));

    /* setting edcFlag */
    if (difOption->enableBlockCount)
    {
      /* enables block count; bit5 */
      agEdc->edcFlag = agEdc->edcFlag | 0x20; /* 0010 0000 */
    }

    if (difOption->enableCrc)
    {
      /* enables CRC verification; bit6 */
      agEdc->edcFlag = agEdc->edcFlag | 0x40; /* 0100 0000 */
    }

    if (difOption->enableIOSeed)
    {
      
    }
    if (difOption->difAction == DIF_INSERT)
    {
      /* bit 0 - 2; 000 */
      agEdc->edcFlag = agEdc->edcFlag & 0xFFFFFFF8;
    }
    else if (difOption->difAction == DIF_VERIFY_FORWARD)
    {
      /* bit 0 - 2; 001 */
      agEdc->edcFlag = agEdc->edcFlag | 0x01;
    }
    else if (difOption->difAction == DIF_VERIFY_DELETE)
    {
      /* bit 0 - 2; 010 */
      agEdc->edcFlag = agEdc->edcFlag | 0x02;
    }
    else
    {
      /* DIF_VERIFY_REPLACE */
      /* bit 0 - 2; 011 */
      agEdc->edcFlag = agEdc->edcFlag | 0x04;
    }

    /* set Update Mask; bit 16-21 */
    agUpdateMask = (difOption->tagUpdateMask) & 0x3F; /* 0011 1111 */
    agUpdateMask = agUpdateMask << 16;
    agEdc->edcFlag = agEdc->edcFlag | agUpdateMask;

    /* set Verify Mask bit 24-29 */
    agVerifyMask = (difOption->tagVerifyMask) & 0x3F; /* 0011 1111 */
    agVerifyMask = agVerifyMask << 24;
    agEdc->edcFlag = agEdc->edcFlag | agVerifyMask;

    agEdc->appTag = difOption->udtArray[0];
    agEdc->appTag = (agEdc->appTag << 8) | difOption->udtArray[1];

    agEdc->lbaReferenceTag =  difOption->udtArray[2];
    agEdc->lbaReferenceTag = (agEdc->lbaReferenceTag << 8) | difOption->udtArray[3];
    agEdc->lbaReferenceTag = (agEdc->lbaReferenceTag << 8) | difOption->udtArray[4];
    agEdc->lbaReferenceTag = (agEdc->lbaReferenceTag << 8) | difOption->udtArray[5];

    /* currently TISA supports only 512 logical block size */
    agEdc->lbSize = 512;


#ifdef LL_INT_COALESCE
    saStatus = saSSPStartIntCoalesceEdc(agRoot,
                                        agIORequest,
                                        agIntCoalCxt,
                                        agDevHandle,
                                        agRequestType,
                                        agEdcSSPRequestBody,
                                        &ossaSSPCompleted);
#endif

    tdIORequestBody->ioStarted = agTRUE;
    tdIORequestBody->ioCompleted = agFALSE;

    if (saStatus == AGSA_RC_SUCCESS)
    {
      Initiator->NumIOsActive++;
      tiStatus = tiSuccess;
    }
    else
    {
      TI_DBG1(("tiINIIOStartIntCoalesceDif: saSSPStart failed\n"));
      tdIORequestBody->ioStarted = agFALSE;
      tdIORequestBody->ioCompleted = agTRUE;
      if (saStatus == AGSA_RC_BUSY)
      {
        tiStatus = tiBusy;
      }
      else
      {
        tiStatus = tiError;
      }
      return tiStatus;
    }
  }
  else if (oneDeviceData->DeviceType == TD_SATA_DEVICE)
  {
    /*
      satIOStart() -> saSATAStartIntCoalesceEdc()
    */
    TI_DBG1(("tiINIIOStartIntCoalesceDif: SATA not supported yet\n"));
    return tiStatus;
  }
  else
  {
    TI_DBG1(("tiINIIOStartIntCoalesceDif: wrong unspported Device %d\n", oneDeviceData->DeviceType));
    /*
      error. unsupported IO
     */
  }
  return tiStatus;
}


osGLOBAL bit32
tiINIIntCoalesceInit(
                     tiRoot_t                  *tiRoot,
                     tiIntCoalesceContext_t    *tiIntCoalesceCxt,
                     bit32                     count
                     )
{

  tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t                *agRoot = agNULL;
  tdsaIntCoalesceContext_t  *tdsaIntCoalCxtHead
    = (tdsaIntCoalesceContext_t *)tdsaAllShared->IntCoalesce;
  tdsaIntCoalesceContext_t  *tdsaIntCoalCxt;
  agsaIntCoalesceContext_t  *agIntCoalCxt;
  tdList_t                  *tdsaIntCoalCxtList = agNULL;

  bit32                     tiStatus = tiError;

  TI_DBG1(("tiINIIntCoalesceInit: start\n"));

  tdsaSingleThreadedEnter(tiRoot, TD_INTCOAL_LOCK);
  if (TDLIST_NOT_EMPTY(&(tdsaIntCoalCxtHead->FreeLink)))
  {
    TDLIST_DEQUEUE_FROM_HEAD(&tdsaIntCoalCxtList, &(tdsaIntCoalCxtHead->FreeLink));
    tdsaSingleThreadedLeave(tiRoot, TD_INTCOAL_LOCK);
    tdsaIntCoalCxt
      = TDLIST_OBJECT_BASE(tdsaIntCoalesceContext_t, FreeLink, tdsaIntCoalCxtList);

    TI_DBG1(("tiINIIntCoalesceInit: id %d\n", tdsaIntCoalCxt->id));

    agRoot = &(tdsaAllShared->agRootNonInt);

    agIntCoalCxt = &(tdsaIntCoalCxt->agIntCoalCxt);
    tdsaIntCoalCxt->tiIntCoalesceCxt = tiIntCoalesceCxt;
    tiIntCoalesceCxt->tdData = tdsaIntCoalCxt;
    agIntCoalCxt->osData = tdsaIntCoalCxt;

    tdsaSingleThreadedEnter(tiRoot, TD_INTCOAL_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&(tdsaIntCoalCxt->MainLink), &(tdsaIntCoalCxtHead->MainLink));
    tdsaSingleThreadedLeave(tiRoot, TD_INTCOAL_LOCK);

    /*
      note: currently asynchronously call is assumed. In other words,
      "ossaIntCoalesceInitCB()" -> "ostiInitiatorCoalesceInitCB()" are used
    */
#ifdef LL_INT_COALESCE
    tiStatus = saIntCoalesceInit(agRoot, agIntCoalCxt, count);
#endif

    TI_DBG6(("tiINIIntCoalesceInit: status %d\n", tiStatus));
    return tiStatus;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_INTCOAL_LOCK);
    TI_DBG1(("tiINIIntCoalesceInit: no more interrupt coalesce context; return fail\n"));
    return tiStatus;
  }
}
#endif /* TD_INT_COALESCE */

/*****************************************************************************
*! \brief itdssIOPrepareSGL
*
*  Purpose:  This function is called to translate TISA SGL information to the
*            LL layer SGL.
*
*  \param    tiRoot:         Pointer to initiator driver/port instance.
*  \param    IORequestBody:  TD layer request body for the I/O.
*  \param    tiSgl1:         First TISA SGL info.
*  \param    sglVirtualAddr: The virtual address of the first element in
*                            tiSgl1 when tiSgl1 is used with the type tiSglList.
*
*  \return:
*
*  tiSuccess:     SGL initialized successfully.
*  tiError:       Failed to initialize SGL.
*
*
*****************************************************************************/
osGLOBAL FORCEINLINE bit32
itdssIOPrepareSGL(
                  tiRoot_t                 *tiRoot,
                  tdIORequestBody_t        *tdIORequestBody,
                  tiSgl_t                  *tiSgl1,
                  void                     *sglVirtualAddr
                  )
{
  agsaSgl_t                 *agSgl;

  TI_DBG6(("itdssIOPrepareSGL: start\n"));

  agSgl = &(tdIORequestBody->transport.SAS.agSASRequestBody.sspInitiatorReq.agSgl);

  agSgl->len = 0;

  if (tiSgl1 == agNULL)
  {
    TI_DBG1(("itdssIOPrepareSGL: Error tiSgl1 is NULL\n"));
    return tiError;
  }

  if (tdIORequestBody->IOType.InitiatorRegIO.expDataLength == 0)
  {
    TI_DBG6(("itdssIOPrepareSGL: expDataLength is 0\n"));
    agSgl->sgUpper = 0;
    agSgl->sgLower = 0;
    agSgl->len = 0;
    CLEAR_ESGL_EXTEND(agSgl->extReserved);
    return tiSuccess;
  }

  agSgl->sgUpper = tiSgl1->upper;
  agSgl->sgLower = tiSgl1->lower;
  agSgl->len = tiSgl1->len;
  agSgl->extReserved = tiSgl1->type;

  return tiSuccess;
}

osGLOBAL bit32
tiNumOfLunIOCTLreq(
             tiRoot_t                       *tiRoot, 
             tiIORequest_t                  *tiIORequest,
             tiDeviceHandle_t               *tiDeviceHandle,
             void                           *tiRequestBody,
             tiIOCTLPayload_t               *agIOCTLPayload,
             void                           *agParam1,
             void                           *agParam2
             )
{
  tdsaRoot_t			    *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t			    *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t			    *agRoot = &(tdsaAllShared->agRootInt);
  void					    *respBuffer = agNULL;
  void					    *osMemHandle = agNULL;
  bit32					    ostiMemoryStatus = 0;
  tdsaDeviceData_t		    *oneDeviceData = agNULL;
  agsaSSPInitiatorRequest_t *agSSPFrame = agNULL;
  bit32					    status = IOCTL_CALL_SUCCESS;	
  bit32					    agRequestType = 0;
  agsaDevHandle_t 		    *agDevHandle = agNULL;
  agsaIORequest_t 		    *agIORequest = agNULL;
  tdIORequestBody_t		    *tdIORequestBody = agNULL;
  agsaSASRequestBody_t	    *agSASRequestBody = agNULL;

  do
  {
    if((tiIORequest == agNULL) || (tiRequestBody == agNULL))
    {
      status = IOCTL_CALL_FAIL;
      break;
    }
    tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;
    tdIORequestBody->tiIORequest = tiIORequest;
    
    /* save context if we need to abort later */
    tiIORequest->tdData = tdIORequestBody; 
    
    agIORequest = &(tdIORequestBody->agIORequest);
    agIORequest->osData = (void *) tdIORequestBody;
    agSASRequestBody = &(tdIORequestBody->transport.SAS.agSASRequestBody);
    agSSPFrame = &(agSASRequestBody->sspInitiatorReq);
    
    ostiMemoryStatus = ostiAllocMemory( tiRoot,
    									  &osMemHandle,
    									  (void **)&respBuffer,
    									  &(agSSPFrame->agSgl.sgUpper),
    									  &(agSSPFrame->agSgl.sgLower),
    									  8,
    									  REPORT_LUN_LEN,
    									  agFALSE);
    if((ostiMemoryStatus != tiSuccess) && (respBuffer == agNULL  ))
    {
      status = IOCTL_CALL_FAIL;
      break;
    }
    	
    osti_memset((void *)respBuffer, 0, REPORT_LUN_LEN);
    
    	// use FW control place in shared structure to keep the neccesary information
    tdsaAllShared->tdFWControlEx.virtAddr = respBuffer;
    tdsaAllShared->tdFWControlEx.len = REPORT_LUN_LEN;
    tdsaAllShared->tdFWControlEx.param1 = agParam1;
    tdsaAllShared->tdFWControlEx.param2 = agParam2;
    tdsaAllShared->tdFWControlEx.payload = agIOCTLPayload;
    tdsaAllShared->tdFWControlEx.inProgress = 1;
    agRequestType = AGSA_SSP_INIT_READ;
    
    status = IOCTL_CALL_PENDING;
    oneDeviceData = (tdsaDeviceData_t *)(tiDeviceHandle->tdData);
    agDevHandle = oneDeviceData->agDevHandle;
    
    agSSPFrame->sspCmdIU.cdb[0] = REPORT_LUN_OPCODE;
    agSSPFrame->sspCmdIU.cdb[1] = 0x0;
    agSSPFrame->sspCmdIU.cdb[2] = 0x0; 
    agSSPFrame->sspCmdIU.cdb[3] = 0x0;
    agSSPFrame->sspCmdIU.cdb[4] = 0x0;
    agSSPFrame->sspCmdIU.cdb[5] = 0x0;
    agSSPFrame->sspCmdIU.cdb[6] = 0x0;
    agSSPFrame->sspCmdIU.cdb[7] = 0x0;
    agSSPFrame->sspCmdIU.cdb[8] = 0x0;
    agSSPFrame->sspCmdIU.cdb[9] = REPORT_LUN_LEN;
    agSSPFrame->sspCmdIU.cdb[10] = 0x0;
    agSSPFrame->sspCmdIU.cdb[11] = 0x0;
      
    agSSPFrame->dataLength = REPORT_LUN_LEN;
    agSSPFrame->agSgl.len =	sizeof(agsaSSPCmdInfoUnit_t);
    agSSPFrame->agSgl.extReserved = 0;
    CLEAR_ESGL_EXTEND(agSSPFrame->agSgl.extReserved);

    status = saSSPStart(agRoot, agIORequest, 0, agDevHandle, agRequestType,agSASRequestBody,agNULL,
    										   &ossaSSPIoctlCompleted);
    if(status != AGSA_RC_SUCCESS)
	{
      ostiFreeMemory(tiRoot,
    				 tdsaAllShared->tdFWControlEx.virtAddr,
    				 tdsaAllShared->tdFWControlEx.len); 
      tdsaAllShared->tdFWControlEx.payload = NULL; 
      tdsaAllShared->tdFWControlEx.inProgress = 0;
      status = IOCTL_CALL_FAIL;
    }
  }while(0);
  return status;
}


