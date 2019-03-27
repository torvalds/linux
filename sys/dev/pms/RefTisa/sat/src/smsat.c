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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/tisa/api/titypes.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>

#include <dev/pms/RefTisa/sat/src/smdefs.h>
#include <dev/pms/RefTisa/sat/src/smproto.h>
#include <dev/pms/RefTisa/sat/src/smtypes.h>

/* start smapi defined APIs */
osGLOBAL bit32
smRegisterDevice(
                 smRoot_t                       *smRoot,
                 agsaDevHandle_t                *agDevHandle,
                 smDeviceHandle_t               *smDeviceHandle,
                 agsaDevHandle_t                *agExpDevHandle,
                 bit32                          phyID,
                 bit32                          DeviceType
                )
{
  smDeviceData_t            *oneDeviceData = agNULL;

  SM_DBG2(("smRegisterDevice: start\n"));

  if (smDeviceHandle == agNULL)
  {
    SM_DBG1(("smRegisterDevice: smDeviceHandle is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  if (agDevHandle == agNULL)
  {
    SM_DBG1(("smRegisterDevice: agDevHandle is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  oneDeviceData = smAddToSharedcontext(smRoot, agDevHandle, smDeviceHandle, agExpDevHandle, phyID);
  if (oneDeviceData != agNULL)
  {
    oneDeviceData->satDeviceType = DeviceType;
    return SM_RC_SUCCESS;
  }
  else
  {
    return SM_RC_FAILURE;
  }

}

osGLOBAL bit32
smDeregisterDevice(
                   smRoot_t                     *smRoot,
                   agsaDevHandle_t              *agDevHandle,
                   smDeviceHandle_t             *smDeviceHandle
                  )
{
  bit32                     status = SM_RC_FAILURE;

  SM_DBG2(("smDeregisterDevice: start\n"));

  if (smDeviceHandle == agNULL)
  {
    SM_DBG1(("smDeregisterDevice: smDeviceHandle is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  if (agDevHandle == agNULL)
  {
    SM_DBG1(("smDeregisterDevice: agDevHandle is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  status = smRemoveFromSharedcontext(smRoot, agDevHandle, smDeviceHandle);

  return status;
}

osGLOBAL bit32
smIOAbort(
           smRoot_t                     *smRoot,
           smIORequest_t                *tasktag
         )

{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  agsaRoot_t                *agRoot;
  smIORequestBody_t         *smIORequestBody = agNULL;
  smIORequestBody_t         *smIONewRequestBody = agNULL;
  agsaIORequest_t           *agIORequest = agNULL; /* IO to be aborted */
  bit32                     status = SM_RC_FAILURE;
  agsaIORequest_t           *agAbortIORequest;  /* abort IO itself */
  smIORequestBody_t         *smAbortIORequestBody;
#if 1
  bit32                     PhysUpper32;
  bit32                     PhysLower32;
  bit32                     memAllocStatus;
  void                      *osMemHandle;
#endif
  smSatIOContext_t            *satIOContext;
  smSatInternalIo_t           *satIntIo;
  smSatIOContext_t            *satAbortIOContext;

  SM_DBG1(("smIOAbort: start\n"));
  SM_DBG2(("smIOAbort: tasktag %p\n", tasktag));
  /*
    alloc smIORequestBody for abort itself
    call saSATAAbort()
  */

  agRoot = smAllShared->agRoot;
  smIORequestBody =  (smIORequestBody_t *)tasktag->smData;

  if (smIORequestBody == agNULL)
  {
    SM_DBG1(("smIOAbort: smIORequestBody is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  /* needs to distinguish internally generated or externally generated */
  satIOContext = &(smIORequestBody->transport.SATA.satIOContext);
  satIntIo     = satIOContext->satIntIoContext;
  if (satIntIo == agNULL)
  {
    SM_DBG2(("smIOAbort: External, OS generated\n"));
    agIORequest     = &(smIORequestBody->agIORequest);
  }
  else
  {
    SM_DBG2(("smIOAbort: Internal, SM generated\n"));
    smIONewRequestBody = (smIORequestBody_t *)satIntIo->satIntRequestBody;
    agIORequest     = &(smIONewRequestBody->agIORequest);
  }

  /*
    allocate smAbortIORequestBody for abort request itself
  */

#if 1
  /* allocating agIORequest for abort itself */
  memAllocStatus = tdsmAllocMemory(
                                   smRoot,
                                   &osMemHandle,
                                   (void **)&smAbortIORequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(smIORequestBody_t),
                                   agTRUE
                                   );
  if (memAllocStatus != SM_RC_SUCCESS)
  {
    /* let os process IO */
    SM_DBG1(("smIOAbort: tdsmAllocMemory failed...!!!\n"));
    return SM_RC_FAILURE;
  }

  if (smAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    SM_DBG1(("smIOAbort: tdsmAllocMemory returned NULL smAbortIORequestBody!!!\n"));
    return SM_RC_FAILURE;
  }

  smIOReInit(smRoot, smAbortIORequestBody);

  /* setup task management structure */
  smAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  satAbortIOContext = &(smAbortIORequestBody->transport.SATA.satIOContext);
  satAbortIOContext->smRequestBody = smAbortIORequestBody;

  smAbortIORequestBody->smDevHandle = smIORequestBody->smDevHandle;

  /* initialize agIORequest */
  agAbortIORequest = &(smAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) smAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

  /* remember IO to be aborted */
  smAbortIORequestBody->smIOToBeAbortedRequest = tasktag;

  status = saSATAAbort(agRoot, agAbortIORequest, 0, agNULL, 0, agIORequest, smaSATAAbortCB);

  SM_DBG2(("smIOAbort: return status=0x%x\n", status));

#endif /* 1 */


  if (status == AGSA_RC_SUCCESS)
  {
    return SM_RC_SUCCESS;
  }
  else
  {
    SM_DBG1(("smIOAbort: failed to call saSATAAbort, status=%d!!!\n", status));
    tdsmFreeMemory(smRoot,
               smAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
               sizeof(smIORequestBody_t)
               );
    return SM_RC_FAILURE;
  }
}

osGLOBAL bit32
smIOAbortAll(
             smRoot_t                     *smRoot,
             smDeviceHandle_t             *smDeviceHandle
            )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  agsaRoot_t                *agRoot;
  bit32                     status = SM_RC_FAILURE;
  agsaIORequest_t           *agAbortIORequest;
  smIORequestBody_t         *smAbortIORequestBody;
  smSatIOContext_t          *satAbortIOContext;
  smDeviceData_t            *oneDeviceData = agNULL;
  agsaDevHandle_t           *agDevHandle;

  bit32                     PhysUpper32;
  bit32                     PhysLower32;
  bit32                     memAllocStatus;
  void                      *osMemHandle;


  SM_DBG2(("smIOAbortAll: start\n"));

  agRoot = smAllShared->agRoot;

  if (smDeviceHandle == agNULL)
  {
    SM_DBG1(("smIOAbortAll: smDeviceHandle is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;
  if (oneDeviceData == agNULL)
  {
    SM_DBG1(("smIOAbortAll: oneDeviceData is NULL!!!\n"));
    return SM_RC_FAILURE;
  }
  if (oneDeviceData->valid == agFALSE)
  {
    SM_DBG1(("smIOAbortAll: oneDeviceData is not valid, did %d !!!\n", oneDeviceData->id));
    return SM_RC_FAILURE;
  }

  agDevHandle     = oneDeviceData->agDevHandle;
  if (agDevHandle == agNULL)
  {
    SM_DBG1(("smIOAbortAll: agDevHandle is NULL!!!\n"));
    return SM_RC_FAILURE;
  }
/*
  smAbortIORequestBody = smDequeueIO(smRoot);
  if (smAbortIORequestBody == agNULL)
  {
    SM_DBG1(("smIOAbortAll: empty freeIOList!!!\n"));
    return SM_RC_FAILURE;
  }
*/
  /* allocating agIORequest for abort itself */
  memAllocStatus = tdsmAllocMemory(
                                   smRoot,
                                   &osMemHandle,
                                   (void **)&smAbortIORequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(smIORequestBody_t),
                                   agTRUE
                                   );
  if (memAllocStatus != SM_RC_SUCCESS)
  {
     /* let os process IO */
     SM_DBG1(("smIOAbortAll: tdsmAllocMemory failed...!!!\n"));
     return SM_RC_FAILURE;
  }

  if (smAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    SM_DBG1(("smIOAbortAll: tdsmAllocMemory returned NULL smAbortIORequestBody!!!\n"));
    return SM_RC_FAILURE;
  }

  smIOReInit(smRoot, smAbortIORequestBody);

  /* setup task management structure */
  smAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;

  satAbortIOContext = &(smAbortIORequestBody->transport.SATA.satIOContext);
  satAbortIOContext->smRequestBody = smAbortIORequestBody;
  smAbortIORequestBody->smDevHandle = smDeviceHandle;

  /* initialize agIORequest */
  agAbortIORequest = &(smAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) smAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

  oneDeviceData->OSAbortAll = agTRUE;
  /* abort all */
  status = saSATAAbort(agRoot, agAbortIORequest, tdsmRotateQnumber(smRoot, smDeviceHandle), agDevHandle, 1, agNULL, smaSATAAbortCB);
  if (status != AGSA_RC_SUCCESS)
  {
    SM_DBG1(("smIOAbortAll: failed to call saSATAAbort, status=%d!!!\n", status));
    tdsmFreeMemory(smRoot,
                   smAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(smIORequestBody_t)
                   );
  }

  return status;
}

osGLOBAL bit32
smSuperIOStart(
               smRoot_t                         *smRoot,
               smIORequest_t                    *smIORequest,
               smDeviceHandle_t                 *smDeviceHandle,
               smSuperScsiInitiatorRequest_t    *smSCSIRequest,
               bit32                            AddrHi,
               bit32                            AddrLo,
               bit32                            interruptContext
              )
{
  smDeviceData_t            *oneDeviceData = agNULL;
  smIORequestBody_t         *smIORequestBody = agNULL;
  smSatIOContext_t            *satIOContext = agNULL;
  bit32                     status = SM_RC_FAILURE;

  SM_DBG2(("smSuperIOStart: start\n"));

  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;
  if (oneDeviceData == agNULL)
  {
    SM_DBG1(("smSuperIOStart: oneDeviceData is NULL!!!\n"));
    return SM_RC_FAILURE;
  }
  if (oneDeviceData->valid == agFALSE)
  {
    SM_DBG1(("smSuperIOStart: oneDeviceData is not valid, did %d !!!\n", oneDeviceData->id));
    return SM_RC_FAILURE;
  }
  smIORequestBody = (smIORequestBody_t*)smIORequest->smData;//smDequeueIO(smRoot);

  if (smIORequestBody == agNULL)
  {
    SM_DBG1(("smSuperIOStart: smIORequestBody is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  smIOReInit(smRoot, smIORequestBody);

  SM_DBG3(("smSuperIOStart: io ID %d!!!\n", smIORequestBody->id ));
  
  oneDeviceData->sasAddressHi = AddrHi;
  oneDeviceData->sasAddressLo = AddrLo;
  
  smIORequestBody->smIORequest = smIORequest;
  smIORequestBody->smDevHandle = smDeviceHandle;

  satIOContext = &(smIORequestBody->transport.SATA.satIOContext);

  /*
   * Need to initialize all the fields within satIOContext except
   * reqType and satCompleteCB which will be set later in SM.
   */
  smIORequestBody->transport.SATA.smSenseData.senseData = agNULL;
  smIORequestBody->transport.SATA.smSenseData.senseLen = 0;
  satIOContext->pSatDevData   = oneDeviceData;
  satIOContext->pFis          =
    &smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev;
  satIOContext->pScsiCmnd     = &smSCSIRequest->scsiCmnd;
  satIOContext->pSense        = &smIORequestBody->transport.SATA.sensePayload;
  satIOContext->pSmSenseData  = &smIORequestBody->transport.SATA.smSenseData;
  satIOContext->pSmSenseData->senseData = satIOContext->pSense;
  /*    satIOContext->pSense = (scsiRspSense_t *)satIOContext->pSmSenseData->senseData; */
  satIOContext->smRequestBody = smIORequestBody;
  satIOContext->interruptContext = interruptContext;
  satIOContext->psmDeviceHandle = smDeviceHandle;
  satIOContext->smScsiXchg = smSCSIRequest;
  satIOContext->superIOFlag = agTRUE;
//  satIOContext->superIOFlag = agFALSE;

  satIOContext->satIntIoContext  = agNULL;
  satIOContext->satOrgIOContext  = agNULL;
  /*    satIOContext->tiIORequest      = tiIORequest; */

  /* save context if we need to abort later */
  /*smIORequest->smData = smIORequestBody;*/

  /* followings are used only for internal IO */
  satIOContext->currentLBA = 0;
  satIOContext->OrgTL = 0;

  status = smsatIOStart(smRoot, smIORequest, smDeviceHandle, (smScsiInitiatorRequest_t *)smSCSIRequest, satIOContext);

  return status;
}

/*
osGLOBAL bit32
tiINIIOStart(
             tiRoot_t                  *tiRoot,
             tiIORequest_t             *tiIORequest,
             tiDeviceHandle_t          *tiDeviceHandle,
             tiScsiInitiatorRequest_t  *tiScsiRequest,
             void                      *tiRequestBody,
             bit32                     interruptContext
             )

GLOBAL bit32  satIOStart(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t  *tiScsiRequest,
                   smSatIOContext_t            *satIOContext
                  )
smIOStart(
          smRoot_t      *smRoot,
          smIORequest_t     *smIORequest,
          smDeviceHandle_t    *smDeviceHandle,
          smScsiInitiatorRequest_t  *smSCSIRequest,
          smIORequestBody_t             *smRequestBody,
          bit32       interruptContext
         )


*/
FORCEINLINE bit32
smIOStart(
          smRoot_t                      *smRoot,
          smIORequest_t                 *smIORequest,
          smDeviceHandle_t              *smDeviceHandle,
          smScsiInitiatorRequest_t      *smSCSIRequest,
          bit32                         interruptContext
         )
{
  smDeviceData_t            *oneDeviceData = agNULL;
  smIORequestBody_t         *smIORequestBody = agNULL;
  smSatIOContext_t          *satIOContext = agNULL;
  bit32                     status = SM_RC_FAILURE;

  SM_DBG2(("smIOStart: start\n"));

  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;
  if (oneDeviceData == agNULL)
  {
    SM_DBG1(("smIOStart: oneDeviceData is NULL!!!\n"));
    return SM_RC_FAILURE;
  }
  if (oneDeviceData->valid == agFALSE)
  {
    SM_DBG1(("smIOStart: oneDeviceData is not valid, did %d !!!\n", oneDeviceData->id));
    return SM_RC_FAILURE;
  }
  smIORequestBody = (smIORequestBody_t*)smIORequest->smData;//smDequeueIO(smRoot);

  if (smIORequestBody == agNULL)
  {
    SM_DBG1(("smIOStart: smIORequestBody is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  smIOReInit(smRoot, smIORequestBody);

  SM_DBG3(("smIOStart: io ID %d!!!\n", smIORequestBody->id ));

  smIORequestBody->smIORequest = smIORequest;
  smIORequestBody->smDevHandle = smDeviceHandle;

  satIOContext = &(smIORequestBody->transport.SATA.satIOContext);

  /*
   * Need to initialize all the fields within satIOContext except
   * reqType and satCompleteCB which will be set later in SM.
   */
  smIORequestBody->transport.SATA.smSenseData.senseData = agNULL;
  smIORequestBody->transport.SATA.smSenseData.senseLen = 0;
  satIOContext->pSatDevData   = oneDeviceData;
  satIOContext->pFis          =
    &smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev;
  satIOContext->pScsiCmnd     = &smSCSIRequest->scsiCmnd;
  satIOContext->pSense        = &smIORequestBody->transport.SATA.sensePayload;
  satIOContext->pSmSenseData  = &smIORequestBody->transport.SATA.smSenseData;
  satIOContext->pSmSenseData->senseData = satIOContext->pSense;
  /*    satIOContext->pSense = (scsiRspSense_t *)satIOContext->pSmSenseData->senseData; */
  satIOContext->smRequestBody = smIORequestBody;
  satIOContext->interruptContext = interruptContext;
  satIOContext->psmDeviceHandle = smDeviceHandle;
  satIOContext->smScsiXchg = smSCSIRequest;
  satIOContext->superIOFlag = agFALSE;

  satIOContext->satIntIoContext  = agNULL;
  satIOContext->satOrgIOContext  = agNULL;
  satIOContext->currentLBA = 0;
  satIOContext->OrgTL = 0;

  status = smsatIOStart(smRoot, smIORequest, smDeviceHandle, smSCSIRequest, satIOContext);

  return status;

}



osGLOBAL bit32
smTaskManagement(
                 smRoot_t                       *smRoot,
                 smDeviceHandle_t               *smDeviceHandle,
                 bit32                          task,
                 smLUN_t                        *lun,
                 smIORequest_t                  *taskTag, /* io to be aborted */
                 smIORequest_t                  *currentTaskTag /* task management */
                )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  agsaRoot_t                *agRoot = smAllShared->agRoot;
  smDeviceData_t            *oneDeviceData = agNULL;
  smIORequestBody_t         *smIORequestBody = agNULL;
  bit32                     status;
  agsaContext_t             *agContext = agNULL;
  smSatIOContext_t          *satIOContext;

  SM_DBG1(("smTaskManagement: start\n"));
  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;

  if (task == SM_LOGICAL_UNIT_RESET || task == SM_TARGET_WARM_RESET || task == SM_ABORT_TASK)
  {
    if (task == AG_LOGICAL_UNIT_RESET)
    {
      if ( (lun->lun[0] | lun->lun[1] | lun->lun[2] | lun->lun[3] |
            lun->lun[4] | lun->lun[5] | lun->lun[6] | lun->lun[7] ) != 0 )
      {
        SM_DBG1(("smTaskManagement: *** REJECT *** LUN not zero, did %d!!!\n",
                oneDeviceData->id));
        return SM_RC_FAILURE;
      }
    }

    oneDeviceData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;
    oneDeviceData->satAbortAfterReset = agFALSE;

    saSetDeviceState(agRoot,
                     agNULL,
                     tdsmRotateQnumber(smRoot, smDeviceHandle),
                     oneDeviceData->agDevHandle,
                     SA_DS_IN_RECOVERY
                     );

    if (oneDeviceData->directlyAttached == agFALSE)
    {
      /* expander attached */
      SM_DBG1(("smTaskManagement: LUN reset or device reset expander attached!!!\n"));
      status = smPhyControlSend(smRoot,
                                oneDeviceData,
                                SMP_PHY_CONTROL_HARD_RESET,
                                currentTaskTag,
                                tdsmRotateQnumber(smRoot, smDeviceHandle)
                               );
      return status;
    }
    else
    {
      SM_DBG1(("smTaskManagement: LUN reset or device reset directly attached\n"));

      smIORequestBody = (smIORequestBody_t*)currentTaskTag->smData;//smDequeueIO(smRoot);

      if (smIORequestBody == agNULL)
      {
        SM_DBG1(("smTaskManagement: smIORequestBody is NULL!!!\n"));
        return SM_RC_FAILURE;
      }

      smIOReInit(smRoot, smIORequestBody);

      satIOContext = &(smIORequestBody->transport.SATA.satIOContext);
      satIOContext->smRequestBody = smIORequestBody;
      smIORequestBody->smDevHandle = smDeviceHandle;

      agContext = &(oneDeviceData->agDeviceResetContext);
      agContext->osData = currentTaskTag;

      status = saLocalPhyControl(agRoot,
                                 agContext,
                                 tdsmRotateQnumber(smRoot, smDeviceHandle) &0xFFFF,
                                 oneDeviceData->phyID,
                                 AGSA_PHY_HARD_RESET,
                                 smLocalPhyControlCB
                                 );

      if ( status == AGSA_RC_SUCCESS)
      {
        return SM_RC_SUCCESS;
      }
      else if (status == AGSA_RC_BUSY)
      {
        return SM_RC_BUSY;
      }
      else if (status == AGSA_RC_FAILURE)
      {
        return SM_RC_FAILURE;
      }
      else
      {
        SM_DBG1(("smTaskManagement: unknown status %d\n",status));
        return SM_RC_FAILURE;
      }
    }
  }
  else
  {
    /* smsatsmTaskManagement() which is satTM() */
    smIORequestBody = (smIORequestBody_t*)currentTaskTag->smData;//smDequeueIO(smRoot);

    if (smIORequestBody == agNULL)
    {
      SM_DBG1(("smTaskManagement: smIORequestBody is NULL!!!\n"));
      return SM_RC_FAILURE;
    }

    smIOReInit(smRoot, smIORequestBody);
    /*currentTaskTag->smData = smIORequestBody;*/

    status = smsatTaskManagement(smRoot,
                                 smDeviceHandle,
                                 task,
                                 lun,
                                 taskTag,
                                 currentTaskTag,
                                 smIORequestBody
                                );

    return status;
  }
  return SM_RC_SUCCESS;
}



/********************************************************* end smapi defined APIS */
/* counterpart is
   smEnqueueIO(smRoot_t       *smRoot,
               smSatIOContext_t       *satIOContext)
*/
osGLOBAL smIORequestBody_t *
smDequeueIO(smRoot_t          *smRoot)
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  smIORequestBody_t         *smIORequestBody = agNULL;
  smList_t                  *IOListList;

  SM_DBG2(("smDequeueIO: start\n"));

  tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
  if (SMLIST_EMPTY(&(smAllShared->freeIOList)))
  {
    SM_DBG1(("smDequeueIO: empty freeIOList!!!\n"));
    tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
    return agNULL;
  }

  SMLIST_DEQUEUE_FROM_HEAD(&IOListList, &(smAllShared->freeIOList));
  smIORequestBody = SMLIST_OBJECT_BASE(smIORequestBody_t, satIoBodyLink, IOListList);
  SMLIST_DEQUEUE_THIS(&(smIORequestBody->satIoBodyLink));
  SMLIST_ENQUEUE_AT_TAIL(&(smIORequestBody->satIoBodyLink), &(smAllShared->mainIOList));
  tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);

  if (smIORequestBody->InUse == agTRUE)
  {
    SM_DBG1(("smDequeueIO: wrong. already in USE ID %d!!!!\n", smIORequestBody->id));
  }
  smIOReInit(smRoot, smIORequestBody);


  SM_DBG2(("smDequeueIO: io ID %d!\n", smIORequestBody->id));

  /* debugging */
  if (smIORequestBody->satIoBodyLink.flink == agNULL)
  {
    SM_DBG1(("smDequeueIO: io ID %d, flink is NULL!!!\n", smIORequestBody->id));
  }
  if (smIORequestBody->satIoBodyLink.blink == agNULL)
  {
    SM_DBG1(("smDequeueIO: io ID %d, blink is NULL!!!\n", smIORequestBody->id));
  }

  return smIORequestBody;
}

//start here
//compare with ossaSATAAbortCB()
//qqq1
osGLOBAL void
smsatAbort(
           smRoot_t          *smRoot,
           agsaRoot_t        *agRoot,
           smSatIOContext_t  *satIOContext
    )
{
  smIORequestBody_t         *smIORequestBody = agNULL; /* abort itself */
  smIORequestBody_t         *smToBeAbortedIORequestBody; /* io to be aborted */
  agsaIORequest_t           *agToBeAbortedIORequest; /* io to be aborted */
  agsaIORequest_t           *agAbortIORequest;  /* abort io itself */
  smSatIOContext_t          *satAbortIOContext;
  bit32                      PhysUpper32;
  bit32                      PhysLower32;
  bit32                      memAllocStatus;
  void                       *osMemHandle;


  SM_DBG2(("smsatAbort: start\n"));

  if (satIOContext == agNULL)
  {
    SM_DBG1(("smsatAbort: satIOContext is NULL, wrong!!!\n"));
    return;
  }

  smToBeAbortedIORequestBody = (smIORequestBody_t *)satIOContext->smRequestBody;
  agToBeAbortedIORequest = (agsaIORequest_t *)&(smToBeAbortedIORequestBody->agIORequest);
  /*
  smIORequestBody = smDequeueIO(smRoot);

  if (smIORequestBody == agNULL)
  {
    SM_DBG1(("smsatAbort: empty freeIOList!!!\n"));
    return;
  }
   */
  /* allocating agIORequest for abort itself */
  memAllocStatus = tdsmAllocMemory(
                                   smRoot,
                                   &osMemHandle,
                                   (void **)&smIORequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(smIORequestBody_t),
                                   agTRUE
                                   );
  if (memAllocStatus != tiSuccess)
  {
    /* let os process IO */
    SM_DBG1(("smsatAbort: ostiAllocMemory failed...\n"));
    return;
  }

  if (smIORequestBody == agNULL)
  {
    /* let os process IO */
    SM_DBG1(("smsatAbort: ostiAllocMemory returned NULL smIORequestBody\n"));
    return;
  }
  smIOReInit(smRoot, smIORequestBody);

  smIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  smIORequestBody->smDevHandle = smToBeAbortedIORequestBody->smDevHandle;
  /* initialize agIORequest */
  satAbortIOContext = &(smIORequestBody->transport.SATA.satIOContext);
  satAbortIOContext->smRequestBody = smIORequestBody;

  agAbortIORequest = &(smIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) smIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

  /*
   * Issue abort
   */
                                                                                                                                                                 saSATAAbort( agRoot, agAbortIORequest, 0, agNULL, 0, agToBeAbortedIORequest, smaSATAAbortCB);


  SM_DBG1(("satAbort: end!!!\n"));

  return;
}

osGLOBAL bit32 
smsatStartCheckPowerMode(
                         smRoot_t                  *smRoot,
                         smIORequest_t             *currentTaskTag,
                         smDeviceHandle_t          *smDeviceHandle,
                         smScsiInitiatorRequest_t  *smScsiRequest,
                         smSatIOContext_t            *satIOContext
                        )
{
  smSatInternalIo_t           *satIntIo = agNULL;
  smDeviceData_t            *oneDeviceData = agNULL;
  smSatIOContext_t            *satNewIOContext;
  bit32                     status;

  SM_DBG1(("smsatStartCheckPowerMode: start\n"));

  oneDeviceData = satIOContext->pSatDevData;

  SM_DBG6(("smsatStartCheckPowerMode: before alloc\n"));

  /* allocate any fis for seting SRT bit in device control */
  satIntIo = smsatAllocIntIoResource( smRoot,
                                      currentTaskTag,
                                      oneDeviceData,
                                      0,
                                      satIntIo);

  SM_DBG6(("smsatStartCheckPowerMode: before after\n"));

  if (satIntIo == agNULL)
  {
    SM_DBG1(("smsatStartCheckPowerMode: can't alloacate!!!\n"));
    /*smEnqueueIO(smRoot, satIOContext);*/
    return SM_RC_FAILURE;
  }

  satNewIOContext = smsatPrepareNewIO(satIntIo,
                                      currentTaskTag,
                                      oneDeviceData,
                                      agNULL,
                                      satIOContext);

  SM_DBG6(("smsatStartCheckPowerMode: TD satIOContext %p \n", satIOContext));
  SM_DBG6(("smsatStartCheckPowerMode: SM satNewIOContext %p \n", satNewIOContext));
  SM_DBG6(("smsatStartCheckPowerMode: TD smScsiXchg %p \n", satIOContext->smScsiXchg));
  SM_DBG6(("smsatStartCheckPowerMode: SM smScsiXchg %p \n", satNewIOContext->smScsiXchg));



  SM_DBG2(("smsatStartCheckPowerMode: satNewIOContext %p \n", satNewIOContext));

  status = smsatCheckPowerMode(smRoot,
                               &satIntIo->satIntSmIORequest, /* New smIORequest */
                               smDeviceHandle,
                               satNewIOContext->smScsiXchg, /* New tiScsiInitiatorRequest_t *smScsiRequest, */
                               satNewIOContext);

  if (status != SM_RC_SUCCESS)
  {
    SM_DBG1(("smsatStartCheckPowerMode: failed in sending!!!\n"));

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    /*smEnqueueIO(smRoot, satIOContext);*/

    return SM_RC_FAILURE;
  }


  SM_DBG6(("smsatStartCheckPowerMode: end\n"));

  return status;
}

osGLOBAL bit32
smsatStartResetDevice(
                       smRoot_t                  *smRoot,
                       smIORequest_t             *currentTaskTag,
                       smDeviceHandle_t          *smDeviceHandle,
                       smScsiInitiatorRequest_t  *smScsiRequest,
                       smSatIOContext_t            *satIOContext
                     )
{
  smSatInternalIo_t           *satIntIo = agNULL;
  smDeviceData_t            *oneDeviceData = agNULL;
  smSatIOContext_t            *satNewIOContext;
  bit32                     status;

  SM_DBG1(("smsatStartResetDevice: start\n"));

  oneDeviceData = satIOContext->pSatDevData;

  SM_DBG6(("smsatStartResetDevice: before alloc\n"));

  /* allocate any fis for seting SRT bit in device control */
  satIntIo = smsatAllocIntIoResource( smRoot,
                                      currentTaskTag,
                                      oneDeviceData,
                                      0,
                                      satIntIo);

  SM_DBG6(("smsatStartResetDevice: before after\n"));

  if (satIntIo == agNULL)
  {
    SM_DBG1(("smsatStartResetDevice: can't alloacate!!!\n"));
    /*smEnqueueIO(smRoot, satIOContext);*/
    return SM_RC_FAILURE;
  }

  satNewIOContext = smsatPrepareNewIO(satIntIo,
                                      currentTaskTag,
                                      oneDeviceData,
                                      agNULL,
                                      satIOContext);

  SM_DBG6(("smsatStartResetDevice: TD satIOContext %p \n", satIOContext));
  SM_DBG6(("smsatStartResetDevice: SM satNewIOContext %p \n", satNewIOContext));
  SM_DBG6(("smsatStartResetDevice: TD smScsiXchg %p \n", satIOContext->smScsiXchg));
  SM_DBG6(("smsatStartResetDevice: SM smScsiXchg %p \n", satNewIOContext->smScsiXchg));



  SM_DBG6(("smsatStartResetDevice: satNewIOContext %p \n", satNewIOContext));

  if (oneDeviceData->satDeviceType == SATA_ATAPI_DEVICE)
  {
      /*if ATAPI device, send DEVICE RESET command to ATAPI device*/
      status = smsatDeviceReset(smRoot,
                            &satIntIo->satIntSmIORequest, /* New smIORequest */
                            smDeviceHandle,
                            satNewIOContext->smScsiXchg, /* New smScsiInitiatorRequest_t *smScsiRequest, NULL */
                            satNewIOContext);
  }
  else
  {
      status = smsatResetDevice(smRoot,
                            &satIntIo->satIntSmIORequest, /* New smIORequest */
                            smDeviceHandle,
                            satNewIOContext->smScsiXchg, /* New smScsiInitiatorRequest_t *smScsiRequest, NULL */
                            satNewIOContext);
   }

  if (status != SM_RC_SUCCESS)
  {
    SM_DBG1(("smsatStartResetDevice: failed in sending!!!\n"));

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    /*smEnqueueIO(smRoot, satIOContext);*/

    return SM_RC_FAILURE;
  }


  SM_DBG6(("smsatStartResetDevice: end\n"));

  return status;
}

osGLOBAL bit32
smsatTmAbortTask(
                  smRoot_t                  *smRoot,
                  smIORequest_t             *currentTaskTag, /* task management */
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest, /* NULL */
                  smSatIOContext_t            *satIOContext, /* task management */
                  smIORequest_t             *taskTag) /* io to be aborted */
{
  smDeviceData_t          *oneDeviceData = agNULL;
  smSatIOContext_t        *satTempIOContext = agNULL;
  smList_t                *elementHdr;
  bit32                   found = agFALSE;
  smIORequestBody_t       *smIORequestBody = agNULL;
  smIORequest_t           *smIOReq = agNULL;
  bit32                   status;

  SM_DBG1(("smsatTmAbortTask: start\n"));

  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;

  /*
   * Check that the only pending I/O matches taskTag. If not return tiError.
   */
  tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);

  elementHdr = oneDeviceData->satIoLinkList.flink;

  while (elementHdr != &oneDeviceData->satIoLinkList)
  {
    satTempIOContext = SMLIST_OBJECT_BASE( smSatIOContext_t,
                                           satIoContextLink,
                                           elementHdr );

    if ( satTempIOContext != agNULL)
    {
      smIORequestBody = (smIORequestBody_t *) satTempIOContext->smRequestBody;
      smIOReq = smIORequestBody->smIORequest;
    }

    elementHdr = elementHdr->flink;   /* for the next while loop  */

    /*
     * Check if the tag matches
     */
    if ( smIOReq == taskTag)
    {
      found = agTRUE;
      satIOContext->satToBeAbortedIOContext = satTempIOContext;
      SM_DBG1(("smsatTmAbortTask: found matching tag.\n"));

      break;

    } /* if matching tag */

  } /* while loop */

  tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);

  if (found == agFALSE )
  {
    SM_DBG1(("smsatTmAbortTask: *** REJECT *** no match!!!\n"));

    /*smEnqueueIO(smRoot, satIOContext);*/
    /* clean up TD layer's smIORequestBody */
    if (smIORequestBody)
    {
      if (smIORequestBody->IOType.InitiatorTMIO.osMemHandle != agNULL)
      {
        tdsmFreeMemory(
                     smRoot,
                     smIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(smIORequestBody_t)
                     );
      }
    }
    else
    {
      SM_DBG1(("smsatTmAbortTask: smIORequestBody is NULL!!!\n"));
    }

    return SM_RC_FAILURE;
  }

  if (satTempIOContext == agNULL)
  {
    SM_DBG1(("smsatTmAbortTask: satTempIOContext is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  /*
   * Save smIORequest, will be returned at device reset completion to return
   * the TM completion.
   */
  oneDeviceData->satTmTaskTag = currentTaskTag;

  /*
   * Set flag to indicate device in recovery mode.
   */
  oneDeviceData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;


  /*
   * Issue SATA device reset or check power mode.. Set flag to to automatically abort
   * at the completion of SATA device reset.
   * SAT r09 p25
   */
  oneDeviceData->satAbortAfterReset = agTRUE;

  if ( (satTempIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
       (satTempIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ)
      )
  {
    SM_DBG1(("smsatTmAbortTask: calling satStartCheckPowerMode!!!\n"));
    /* send check power mode */
    status = smsatStartCheckPowerMode(
                                       smRoot,
                                       currentTaskTag, /* currentTaskTag */
                                       smDeviceHandle,
                                       smScsiRequest, /* NULL */
                                       satIOContext
                                     );
  }
  else
  {
    SM_DBG1(("smsatTmAbortTask: calling satStartResetDevice!!!\n"));
    /* send AGSA_SATA_PROTOCOL_SRST_ASSERT */
    status = smsatStartResetDevice(
                                    smRoot,
                                    currentTaskTag, /* currentTaskTag */
                                    smDeviceHandle,
                                    smScsiRequest, /* NULL */
                                    satIOContext
                                  );
  }
  return status;
}

/* satTM() */
osGLOBAL bit32
smsatTaskManagement(
                    smRoot_t          *smRoot,
                    smDeviceHandle_t  *smDeviceHandle,
                    bit32             task,
                    smLUN_t           *lun,
                    smIORequest_t     *taskTag, /* io to be aborted */
                    smIORequest_t     *currentTaskTag, /* task management */
                    smIORequestBody_t *smIORequestBody
       )
{
  smSatIOContext_t              *satIOContext = agNULL;
  smDeviceData_t              *oneDeviceData = agNULL;
  bit32                       status;

  SM_DBG1(("smsatTaskManagement: start\n"));
  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;

  satIOContext = &(smIORequestBody->transport.SATA.satIOContext);

  satIOContext->pSatDevData   = oneDeviceData;
  satIOContext->pFis          =
    &(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);


  satIOContext->smRequestBody = smIORequestBody;
  satIOContext->psmDeviceHandle = smDeviceHandle;
  satIOContext->satIntIoContext  = agNULL;
  satIOContext->satOrgIOContext  = agNULL;

  /* followings are used only for internal IO */
  satIOContext->currentLBA = 0;
  satIOContext->OrgTL = 0;

  /* saving task in satIOContext */
  satIOContext->TMF = task;

  satIOContext->satToBeAbortedIOContext = agNULL;

  if (task == AG_ABORT_TASK)
  {
    status = smsatTmAbortTask( smRoot,
                               currentTaskTag,
                               smDeviceHandle,
                               agNULL,
                               satIOContext,
                               taskTag);

    return status;
  }
  else
  {
    SM_DBG1(("smsatTaskManagement: UNSUPPORTED TM task=0x%x!!!\n", task ));

    /*smEnqueueIO(smRoot, satIOContext);*/

    return SM_RC_FAILURE;
  }

  return SM_RC_SUCCESS;
}


osGLOBAL bit32
smPhyControlSend(
                  smRoot_t             *smRoot,
                  smDeviceData_t       *oneDeviceData, /* sata disk itself */
                  bit8                 phyOp,
                  smIORequest_t        *CurrentTaskTag,
                  bit32                queueNumber
                )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  agsaRoot_t                *agRoot = smAllShared->agRoot;
  agsaDevHandle_t           *agExpDevHandle;
  smpReqPhyControl_t        smpPhyControlReq;
  void                      *osMemHandle;
  bit32                     PhysUpper32;
  bit32                     PhysLower32;
  bit32                     memAllocStatus;
  bit32                     expectedRspLen = 0;
  smSMPRequestBody_t        *smSMPRequestBody;
  agsaSASRequestBody_t      *agSASRequestBody;
  agsaSMPFrame_t            *agSMPFrame;
  agsaIORequest_t           *agIORequest;
//  agsaDevHandle_t           *agDevHandle;
  smSMPFrameHeader_t        smSMPFrameHeader;
  bit32                     status;
  bit8                      *pSmpBody; /* smp payload itself w/o first 4 bytes(header) */
  bit32                     smpBodySize; /* smp payload size w/o first 4 bytes(header) */
  bit32                     agRequestType;

  SM_DBG2(("smPhyControlSend: start\n"));

  agExpDevHandle = oneDeviceData->agExpDevHandle;

  if (agExpDevHandle == agNULL)
  {
    SM_DBG1(("smPhyControlSend: agExpDevHandle is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  SM_DBG5(("smPhyControlSend: phyID %d\n", oneDeviceData->phyID));

  sm_memset(&smpPhyControlReq, 0, sizeof(smpReqPhyControl_t));

  /* fill in SMP payload */
  smpPhyControlReq.phyIdentifier = (bit8)oneDeviceData->phyID;
  smpPhyControlReq.phyOperation = phyOp;

  /* allocate smp and send it */
  memAllocStatus = tdsmAllocMemory(
                                   smRoot,
                                   &osMemHandle,
                                   (void **)&smSMPRequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(smSMPRequestBody_t),
                                   agTRUE
                                   );

  if (memAllocStatus != SM_RC_SUCCESS)
  {
    SM_DBG1(("smPhyControlSend: tdsmAllocMemory failed...!!!\n"));
    return SM_RC_FAILURE;
  }

  if (smSMPRequestBody == agNULL)
  {
    SM_DBG1(("smPhyControlSend: tdsmAllocMemory returned NULL smSMPRequestBody!!!\n"));
    return SM_RC_FAILURE;
  }

  /* saves mem handle for freeing later */
  smSMPRequestBody->osMemHandle = osMemHandle;

  /* saves oneDeviceData */
  smSMPRequestBody->smDeviceData = oneDeviceData; /* sata disk */

  /* saves oneDeviceData */
  smSMPRequestBody->smDevHandle = oneDeviceData->smDevHandle;

//  agDevHandle = oneDeviceData->agDevHandle;

  /* save the callback funtion */
  smSMPRequestBody->SMPCompletionFunc = smSMPCompleted; /* in satcb.c */

  /* for simulate warm target reset */
  smSMPRequestBody->CurrentTaskTag = CurrentTaskTag;

  if (CurrentTaskTag != agNULL)
  {
    CurrentTaskTag->smData = smSMPRequestBody;
  }

  /* initializes the number of SMP retries */
  smSMPRequestBody->retries = 0;

#ifdef TD_INTERNAL_DEBUG  /* debugging */
  SM_DBG4(("smPhyControlSend: SMPRequestbody %p\n", smSMPRequestBody));
  SM_DBG4(("smPhyControlSend: callback fn %p\n", smSMPRequestBody->SMPCompletionFunc));
#endif

  agIORequest = &(smSMPRequestBody->agIORequest);
  agIORequest->osData = (void *) smSMPRequestBody;
  agIORequest->sdkData = agNULL; /* SALL takes care of this */


  agSASRequestBody = &(smSMPRequestBody->agSASRequestBody);
  agSMPFrame = &(agSASRequestBody->smpFrame);

  SM_DBG3(("smPhyControlSend: agIORequest %p\n", agIORequest));
  SM_DBG3(("smPhyControlSend: SMPRequestbody %p\n", smSMPRequestBody));

  expectedRspLen = 4;

  pSmpBody = (bit8 *)&smpPhyControlReq;
  smpBodySize = sizeof(smpReqPhyControl_t);
  agRequestType = AGSA_SMP_INIT_REQ;

  if (SMIsSPC(agRoot))
  {
    if ( (smpBodySize + 4) <= SMP_DIRECT_PAYLOAD_LIMIT) /* 48 */
    {
      SM_DBG3(("smPhyControlSend: DIRECT smp payload\n"));
      sm_memset(&smSMPFrameHeader, 0, sizeof(smSMPFrameHeader_t));
      sm_memset(smSMPRequestBody->smpPayload, 0, SMP_DIRECT_PAYLOAD_LIMIT);

      /* SMP header */
      smSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
      smSMPFrameHeader.smpFunction = (bit8)SMP_PHY_CONTROL;
      smSMPFrameHeader.smpFunctionResult = 0;
      smSMPFrameHeader.smpReserved = 0;

      sm_memcpy(smSMPRequestBody->smpPayload, &smSMPFrameHeader, 4);
      sm_memcpy((smSMPRequestBody->smpPayload)+4, pSmpBody, smpBodySize);

      /* direct SMP payload eg) REPORT_GENERAL, DISCOVER etc */
      agSMPFrame->outFrameBuf = smSMPRequestBody->smpPayload;
      agSMPFrame->outFrameLen = smpBodySize + 4; /* without last 4 byte crc */
      /* to specify DIRECT SMP response */
      agSMPFrame->inFrameLen = 0;

      /* temporary solution for T2D Combo*/
#if defined (INITIATOR_DRIVER) && defined (TARGET_DRIVER)
      /* force smp repsonse to be direct */
      agSMPFrame->expectedRespLen = 0;
#else
      agSMPFrame->expectedRespLen = expectedRspLen;
#endif
  //    smhexdump("smPhyControlSend", (bit8*)agSMPFrame->outFrameBuf, agSMPFrame->outFrameLen);
  //    smhexdump("smPhyControlSend new", (bit8*)smSMPRequestBody->smpPayload, agSMPFrame->outFrameLen);
  //    smhexdump("smPhyControlSend - smSMPRequestBody", (bit8*)smSMPRequestBody, sizeof(smSMPRequestBody_t));
    }
    else
    {
      SM_DBG1(("smPhyControlSend: INDIRECT smp payload, not supported!!!\n"));
      tdsmFreeMemory(
                     smRoot,
                     osMemHandle,
                     sizeof(smSMPRequestBody_t)
                     );

      return SM_RC_FAILURE;
    }
  }
  else /* SPCv controller */
  {
    /* only direct mode for both request and response */
    SM_DBG3(("smPhyControlSend: DIRECT smp payload\n"));
    agSMPFrame->flag = 0;
    sm_memset(&smSMPFrameHeader, 0, sizeof(smSMPFrameHeader_t));
    sm_memset(smSMPRequestBody->smpPayload, 0, SMP_DIRECT_PAYLOAD_LIMIT);

    /* SMP header */
    smSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
    smSMPFrameHeader.smpFunction = (bit8)SMP_PHY_CONTROL;
    smSMPFrameHeader.smpFunctionResult = 0;
    smSMPFrameHeader.smpReserved = 0;

    sm_memcpy(smSMPRequestBody->smpPayload, &smSMPFrameHeader, 4);
    sm_memcpy((smSMPRequestBody->smpPayload)+4, pSmpBody, smpBodySize);

    /* direct SMP payload eg) REPORT_GENERAL, DISCOVER etc */
    agSMPFrame->outFrameBuf = smSMPRequestBody->smpPayload;
    agSMPFrame->outFrameLen = smpBodySize + 4; /* without last 4 byte crc */
    /* to specify DIRECT SMP response */
    agSMPFrame->inFrameLen = 0;

    /* temporary solution for T2D Combo*/
#if defined (INITIATOR_DRIVER) && defined (TARGET_DRIVER)
    /* force smp repsonse to be direct */
    agSMPFrame->expectedRespLen = 0;
#else
    agSMPFrame->expectedRespLen = expectedRspLen;
#endif
//    smhexdump("smPhyControlSend", (bit8*)agSMPFrame->outFrameBuf, agSMPFrame->outFrameLen);
//    smhexdump("smPhyControlSend new", (bit8*)smSMPRequestBody->smpPayload, agSMPFrame->outFrameLen);
//    smhexdump("smPhyControlSend - smSMPRequestBody", (bit8*)smSMPRequestBody, sizeof(smSMPRequestBody_t));
  }

  status = saSMPStart(
                      agRoot,
                      agIORequest,
                      queueNumber,
                      agExpDevHandle,
                      agRequestType,
                      agSASRequestBody,
                      &smSMPCompletedCB
                      );

  if (status == AGSA_RC_SUCCESS)
  {
    return SM_RC_SUCCESS;
  }
  else if (status == AGSA_RC_BUSY)
  {
    SM_DBG1(("smPhyControlSend: saSMPStart is busy!!!\n"));
    tdsmFreeMemory(
                   smRoot,
                   osMemHandle,
                   sizeof(smSMPRequestBody_t)
                   );

    return SM_RC_BUSY;
  }
  else /* AGSA_RC_FAILURE */
  {
    SM_DBG1(("smPhyControlSend: saSMPStart is failed. status %d!!!\n", status));
    tdsmFreeMemory(
                   smRoot,
                   osMemHandle,
                   sizeof(smSMPRequestBody_t)
                   );

    return SM_RC_FAILURE;
  }
}

/* free IO which are internally completed within SM
   counterpart is
   osGLOBAL smIORequestBody_t *
   smDequeueIO(smRoot_t          *smRoot)
*/
osGLOBAL void
smEnqueueIO(
             smRoot_t               *smRoot,
             smSatIOContext_t         *satIOContext
      )
{
  smIntRoot_t          *smIntRoot = agNULL;
  smIntContext_t       *smAllShared = agNULL;
  smIORequestBody_t    *smIORequestBody;

  SM_DBG3(("smEnqueueIO: start\n"));
  smIORequestBody = (smIORequestBody_t *)satIOContext->smRequestBody;
  smIntRoot       = (smIntRoot_t *)smRoot->smData;
  smAllShared     = (smIntContext_t *)&smIntRoot->smAllShared;

  /* enque back to smAllShared->freeIOList */
  if (satIOContext->satIntIoContext == agNULL)
  {
    SM_DBG2(("smEnqueueIO: external command!!!, io ID %d!!!\n", smIORequestBody->id));
    /* debugging only */
    if (smIORequestBody->satIoBodyLink.flink == agNULL)
    {
      SM_DBG1(("smEnqueueIO: external command!!!, io ID %d, flink is NULL!!!\n", smIORequestBody->id));
    }
    if (smIORequestBody->satIoBodyLink.blink == agNULL)
    {
      SM_DBG1(("smEnqueueIO: external command!!!, io ID %d, blink is NULL!!!\n", smIORequestBody->id));
    }
  }
  else
  {
    SM_DBG2(("smEnqueueIO: internal command!!!, io ID %d!!!\n", smIORequestBody->id));
    /* debugging only */
    if (smIORequestBody->satIoBodyLink.flink == agNULL)
    {
      SM_DBG1(("smEnqueueIO: internal command!!!, io ID %d, flink is NULL!!!\n", smIORequestBody->id));
    }
    if (smIORequestBody->satIoBodyLink.blink == agNULL)
    {
      SM_DBG1(("smEnqueueIO: internal command!!!, io ID %d, blink is NULL!!!\n", smIORequestBody->id));
    }
  }

  if (smIORequestBody->smIORequest == agNULL)
  {
    SM_DBG1(("smEnqueueIO: smIORequest is NULL, io ID %d!!!\n", smIORequestBody->id));
  }

  if (smIORequestBody->InUse == agTRUE)
  {
    smIORequestBody->InUse = agFALSE;
    tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
    SMLIST_DEQUEUE_THIS(&(smIORequestBody->satIoBodyLink));
    SMLIST_ENQUEUE_AT_TAIL(&(smIORequestBody->satIoBodyLink), &(smAllShared->freeIOList));
    tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
  }
  else
  {
    SM_DBG2(("smEnqueueIO: check!!!, io ID %d!!!\n", smIORequestBody->id));
  }


  return;
}

FORCEINLINE void
smsatFreeIntIoResource(
       smRoot_t              *smRoot,
       smDeviceData_t        *satDevData,
       smSatInternalIo_t     *satIntIo
       )
{
  SM_DBG3(("smsatFreeIntIoResource: start\n"));

  if (satIntIo == agNULL)
  {
    SM_DBG2(("smsatFreeIntIoResource: allowed call\n"));
    return;
  }

  /* sets the original smIOrequest to agNULL for internally generated ATA cmnd */
  satIntIo->satOrgSmIORequest = agNULL;

  /*
   * Free DMA memory if previosly alocated
   */
  if (satIntIo->satIntSmScsiXchg.scsiCmnd.expDataLength != 0)
  {
    SM_DBG3(("smsatFreeIntIoResource: DMA len %d\n", satIntIo->satIntDmaMem.totalLength));
    SM_DBG3(("smsatFreeIntIoResource: pointer %p\n", satIntIo->satIntDmaMem.osHandle));

    tdsmFreeMemory( smRoot,
                    satIntIo->satIntDmaMem.osHandle,
                    satIntIo->satIntDmaMem.totalLength);
    satIntIo->satIntSmScsiXchg.scsiCmnd.expDataLength = 0;
  }

  if (satIntIo->satIntReqBodyMem.totalLength != 0)
  {
    SM_DBG3(("smsatFreeIntIoResource: req body len %d\n", satIntIo->satIntReqBodyMem.totalLength));
    /*
     * Free mem allocated for Req body
     */
    tdsmFreeMemory( smRoot,
                    satIntIo->satIntReqBodyMem.osHandle,
                    satIntIo->satIntReqBodyMem.totalLength);

    satIntIo->satIntReqBodyMem.totalLength = 0;
  }

  SM_DBG3(("smsatFreeIntIoResource: satDevData %p satIntIo id %d\n", satDevData, satIntIo->id));
  /*
   * Return satIntIo to the free list
   */
  tdsmSingleThreadedEnter(smRoot, SM_INTERNAL_IO_LOCK);
  SMLIST_DEQUEUE_THIS (&(satIntIo->satIntIoLink));
  SMLIST_ENQUEUE_AT_TAIL (&(satIntIo->satIntIoLink), &(satDevData->satFreeIntIoLinkList));
  tdsmSingleThreadedLeave(smRoot, SM_INTERNAL_IO_LOCK);

  return;
}
//start here
osGLOBAL smSatInternalIo_t *
smsatAllocIntIoResource(
                        smRoot_t              *smRoot,
                        smIORequest_t         *smIORequest,
                        smDeviceData_t        *satDevData,
                        bit32                 dmaAllocLength,
                        smSatInternalIo_t     *satIntIo)
{
  smList_t          *smList = agNULL;
  bit32             memAllocStatus;

  SM_DBG3(("smsatAllocIntIoResource: start\n"));
  SM_DBG3(("smsatAllocIntIoResource: satIntIo %p\n", satIntIo));
  if (satDevData == agNULL)
  {
    SM_DBG1(("smsatAllocIntIoResource: ***** ASSERT satDevData is null!!!\n"));
    return agNULL;
  }

  tdsmSingleThreadedEnter(smRoot, SM_INTERNAL_IO_LOCK);
  if (!SMLIST_EMPTY(&(satDevData->satFreeIntIoLinkList)))
  {
    SMLIST_DEQUEUE_FROM_HEAD(&smList, &(satDevData->satFreeIntIoLinkList));
  }
  else
  {
    tdsmSingleThreadedLeave(smRoot, SM_INTERNAL_IO_LOCK);
    SM_DBG1(("smsatAllocIntIoResource() no more internal free link!!!\n"));
    return agNULL;
  }

  if (smList == agNULL)
  {
    tdsmSingleThreadedLeave(smRoot, SM_INTERNAL_IO_LOCK);
    SM_DBG1(("smsatAllocIntIoResource() FAIL to alloc satIntIo!!!\n"));
    return agNULL;
  }

  satIntIo = SMLIST_OBJECT_BASE( smSatInternalIo_t, satIntIoLink, smList);
  SM_DBG3(("smsatAllocIntIoResource: satDevData %p satIntIo id %d\n", satDevData, satIntIo->id));

  /* Put in active list */
  SMLIST_DEQUEUE_THIS (&(satIntIo->satIntIoLink));
  SMLIST_ENQUEUE_AT_TAIL (&(satIntIo->satIntIoLink), &(satDevData->satActiveIntIoLinkList));
  tdsmSingleThreadedLeave(smRoot, SM_INTERNAL_IO_LOCK);

#ifdef REMOVED
  /* Put in active list */
  tdsmSingleThreadedEnter(smRoot, SM_INTERNAL_IO_LOCK);
  SMLIST_DEQUEUE_THIS (smList);
  SMLIST_ENQUEUE_AT_TAIL (smList, &(satDevData->satActiveIntIoLinkList));
  tdsmSingleThreadedLeave(smRoot, SM_INTERNAL_IO_LOCK);

  satIntIo = SMLIST_OBJECT_BASE( smSatInternalIo_t, satIntIoLink, smList);
  SM_DBG3(("smsatAllocIntIoResource: satDevData %p satIntIo id %d\n", satDevData, satIntIo->id));
#endif

  /*
    typedef struct
    {
      tdList_t                    satIntIoLink;
      smIORequest_t               satIntSmIORequest;
      void                        *satIntRequestBody;
      smScsiInitiatorRequest_t    satIntSmScsiXchg;
      smMem_t                     satIntDmaMem;
      smMem_t                     satIntReqBodyMem;
      bit32                       satIntFlag;
    } smSatInternalIo_t;
  */

  /*
   * Allocate mem for Request Body
   */
  satIntIo->satIntReqBodyMem.totalLength = sizeof(smIORequestBody_t);

  memAllocStatus = tdsmAllocMemory( smRoot,
                                    &satIntIo->satIntReqBodyMem.osHandle,
                                    (void **)&satIntIo->satIntRequestBody,
                                    &satIntIo->satIntReqBodyMem.physAddrUpper,
                                    &satIntIo->satIntReqBodyMem.physAddrLower,
                                    8,
                                    satIntIo->satIntReqBodyMem.totalLength,
                                    agTRUE );

  if (memAllocStatus != SM_RC_SUCCESS)
  {
    SM_DBG1(("smsatAllocIntIoResource() FAIL to alloc mem for Req Body!!!\n"));
    /*
     * Return satIntIo to the free list
     */
    tdsmSingleThreadedEnter(smRoot, SM_INTERNAL_IO_LOCK);
    SMLIST_DEQUEUE_THIS (&satIntIo->satIntIoLink);
    SMLIST_ENQUEUE_AT_HEAD(&satIntIo->satIntIoLink, &satDevData->satFreeIntIoLinkList);
    tdsmSingleThreadedLeave(smRoot, SM_INTERNAL_IO_LOCK);

    return agNULL;
  }

  /*
   *   Allocate DMA memory if required
   */
  if (dmaAllocLength != 0)
  {
    satIntIo->satIntDmaMem.totalLength = dmaAllocLength;

    memAllocStatus = tdsmAllocMemory( smRoot,
                                      &satIntIo->satIntDmaMem.osHandle,
                                      (void **)&satIntIo->satIntDmaMem.virtPtr,
                                      &satIntIo->satIntDmaMem.physAddrUpper,
                                      &satIntIo->satIntDmaMem.physAddrLower,
                                      8,
                                      satIntIo->satIntDmaMem.totalLength,
                                      agFALSE);
    SM_DBG3(("smsatAllocIntIoResource: len %d \n", satIntIo->satIntDmaMem.totalLength));
    SM_DBG3(("smsatAllocIntIoResource: pointer %p \n", satIntIo->satIntDmaMem.osHandle));

    if (memAllocStatus != SM_RC_SUCCESS)
    {
      SM_DBG1(("smsatAllocIntIoResource() FAIL to alloc mem for DMA mem!!!\n"));
      /*
       * Return satIntIo to the free list
       */
      tdsmSingleThreadedEnter(smRoot, SM_INTERNAL_IO_LOCK);
      SMLIST_DEQUEUE_THIS (&satIntIo->satIntIoLink);
      SMLIST_ENQUEUE_AT_HEAD(&satIntIo->satIntIoLink, &satDevData->satFreeIntIoLinkList);
      tdsmSingleThreadedLeave(smRoot, SM_INTERNAL_IO_LOCK);

      /*
       * Free mem allocated for Req body
       */
      tdsmFreeMemory( smRoot,
                      satIntIo->satIntReqBodyMem.osHandle,
                      satIntIo->satIntReqBodyMem.totalLength);

      return agNULL;
    }
  }

  /*
    typedef struct
    {
      smList_t                    satIntIoLink;
      smIORequest_t               satIntSmIORequest;
      void                        *satIntRequestBody;
      smScsiInitiatorRequest_t    satIntSmScsiXchg;
      smMem_t                     satIntDmaMem;
      smMem_t                     satIntReqBodyMem;
      bit32                       satIntFlag;
    } smSatInternalIo_t;
  */

  /*
   * Initialize satIntSmIORequest field
   */
  satIntIo->satIntSmIORequest.tdData = agNULL;  /* Not used for internal SAT I/O */
  satIntIo->satIntSmIORequest.smData = satIntIo->satIntRequestBody;

  /*
   * saves the original smIOrequest
   */
  satIntIo->satOrgSmIORequest = smIORequest;
  /*
    typedef struct tiIniScsiCmnd
    {
      tiLUN_t     lun;
      bit32       expDataLength;
      bit32       taskAttribute;
      bit32       crn;
      bit8        cdb[16];
    } tiIniScsiCmnd_t;

    typedef struct tiScsiInitiatorExchange
    {
      void                *sglVirtualAddr;
      tiIniScsiCmnd_t     scsiCmnd;
      tiSgl_t             agSgl1;
      tiSgl_t             agSgl2;
      tiDataDirection_t   dataDirection;
    } tiScsiInitiatorRequest_t;

  */

  /*
   * Initialize satIntSmScsiXchg. Since the internal SAT request is NOT
   * originated from SCSI request, only the following fields are initialized:
   *  - sglVirtualAddr if DMA transfer is involved
   *  - agSgl1 if DMA transfer is involved
   *  - expDataLength in scsiCmnd since this field is read by smsataLLIOStart()
   */
  if (dmaAllocLength != 0)
  {
    satIntIo->satIntSmScsiXchg.sglVirtualAddr = satIntIo->satIntDmaMem.virtPtr;

    OSSA_WRITE_LE_32(agNULL, &satIntIo->satIntSmScsiXchg.smSgl1.len, 0,
                     satIntIo->satIntDmaMem.totalLength);
    satIntIo->satIntSmScsiXchg.smSgl1.lower = satIntIo->satIntDmaMem.physAddrLower;
    satIntIo->satIntSmScsiXchg.smSgl1.upper = satIntIo->satIntDmaMem.physAddrUpper;
    satIntIo->satIntSmScsiXchg.smSgl1.type  = tiSgl;

    satIntIo->satIntSmScsiXchg.scsiCmnd.expDataLength = satIntIo->satIntDmaMem.totalLength;
  }
  else
  {
    satIntIo->satIntSmScsiXchg.sglVirtualAddr = agNULL;

    satIntIo->satIntSmScsiXchg.smSgl1.len   = 0;
    satIntIo->satIntSmScsiXchg.smSgl1.lower = 0;
    satIntIo->satIntSmScsiXchg.smSgl1.upper = 0;
    satIntIo->satIntSmScsiXchg.smSgl1.type  = tiSgl;

    satIntIo->satIntSmScsiXchg.scsiCmnd.expDataLength = 0;
  }

  SM_DBG5(("smsatAllocIntIoResource: satIntIo->satIntSmScsiXchg.agSgl1.len %d\n", satIntIo->satIntSmScsiXchg.smSgl1.len));

  SM_DBG5(("smsatAllocIntIoResource: satIntIo->satIntSmScsiXchg.agSgl1.upper %d\n", satIntIo->satIntSmScsiXchg.smSgl1.upper));

  SM_DBG5(("smsatAllocIntIoResource: satIntIo->satIntSmScsiXchg.agSgl1.lower %d\n", satIntIo->satIntSmScsiXchg.smSgl1.lower));

  SM_DBG5(("smsatAllocIntIoResource: satIntIo->satIntSmScsiXchg.agSgl1.type %d\n", satIntIo->satIntSmScsiXchg.smSgl1.type));
  SM_DBG5(("smsatAllocIntIoResource: return satIntIo %p\n", satIntIo));
  return  satIntIo;
}

osGLOBAL smDeviceData_t *
smAddToSharedcontext(
                     smRoot_t                   *smRoot,
                     agsaDevHandle_t            *agDevHandle,
                     smDeviceHandle_t           *smDeviceHandle,
                     agsaDevHandle_t            *agExpDevHandle,
                     bit32                      phyID
                    )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  smDeviceData_t            *oneDeviceData = agNULL;
  smList_t                  *DeviceListList;
  bit32                     new_device = agTRUE;

  SM_DBG2(("smAddToSharedcontext: start\n"));

  /* find a device's existence */
  DeviceListList = smAllShared->MainDeviceList.flink;
  while (DeviceListList != &(smAllShared->MainDeviceList))
  {
    oneDeviceData = SMLIST_OBJECT_BASE(smDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smAddToSharedcontext: oneDeviceData is NULL!!!\n"));
      return agNULL;
    }
    if (oneDeviceData->agDevHandle == agDevHandle)
    {
      SM_DBG2(("smAddToSharedcontext: did %d\n", oneDeviceData->id));
      new_device = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }

  /* new device */
  if (new_device == agTRUE)
  {
    SM_DBG2(("smAddToSharedcontext: new device\n"));
    tdsmSingleThreadedEnter(smRoot, SM_DEVICE_LOCK);
    if (SMLIST_EMPTY(&(smAllShared->FreeDeviceList)))
    {
      tdsmSingleThreadedLeave(smRoot, SM_DEVICE_LOCK);
      SM_DBG1(("smAddToSharedcontext: empty DeviceData FreeLink!!!\n"));
      smDeviceHandle->smData = agNULL;
      return agNULL;
    }

    SMLIST_DEQUEUE_FROM_HEAD(&DeviceListList, &(smAllShared->FreeDeviceList));
    tdsmSingleThreadedLeave(smRoot, SM_DEVICE_LOCK);
    oneDeviceData = SMLIST_OBJECT_BASE(smDeviceData_t, FreeLink, DeviceListList);
    oneDeviceData->smRoot = smRoot;
    oneDeviceData->agDevHandle = agDevHandle;
    oneDeviceData->valid = agTRUE;
    smDeviceHandle->smData = oneDeviceData;
    oneDeviceData->smDevHandle = smDeviceHandle;
    if (agExpDevHandle == agNULL)
    {
      oneDeviceData->directlyAttached = agTRUE;
    }
    else
    {
      oneDeviceData->directlyAttached = agFALSE;
    }
    oneDeviceData->agExpDevHandle = agExpDevHandle;
    oneDeviceData->phyID = phyID;
    oneDeviceData->satPendingIO = 0;
    oneDeviceData->satPendingNCQIO = 0;
    oneDeviceData->satPendingNONNCQIO = 0;
    /* add the devicedata to the portcontext */
    tdsmSingleThreadedEnter(smRoot, SM_DEVICE_LOCK);
    SMLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->MainLink), &(smAllShared->MainDeviceList));
    tdsmSingleThreadedLeave(smRoot, SM_DEVICE_LOCK);
    SM_DBG2(("smAddToSharedcontext: new case did %d\n", oneDeviceData->id));
  }
  else
  {
    SM_DBG2(("smAddToSharedcontext: old device\n"));
    oneDeviceData->smRoot = smRoot;
    oneDeviceData->agDevHandle = agDevHandle;
    oneDeviceData->valid = agTRUE;
    smDeviceHandle->smData = oneDeviceData;
    oneDeviceData->smDevHandle = smDeviceHandle;
    if (agExpDevHandle == agNULL)
    {
      oneDeviceData->directlyAttached = agTRUE;
    }
    else
    {
      oneDeviceData->directlyAttached = agFALSE;
    }
    oneDeviceData->agExpDevHandle = agExpDevHandle;
    oneDeviceData->phyID = phyID;
    oneDeviceData->satPendingIO = 0;
    oneDeviceData->satPendingNCQIO = 0;
    oneDeviceData->satPendingNONNCQIO = 0;
    SM_DBG2(("smAddToSharedcontext: old case did %d\n", oneDeviceData->id));
  }

  return  oneDeviceData;
}

osGLOBAL bit32
smRemoveFromSharedcontext(
                          smRoot_t                      *smRoot,
                          agsaDevHandle_t               *agDevHandle,
                          smDeviceHandle_t              *smDeviceHandle
                         )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  smDeviceData_t            *oneDeviceData = agNULL;

  SM_DBG2(("smRemoveFromSharedcontext: start\n"));

  //due to device all and completion
  //smDeviceHandle->smData = agNULL;

  /* find oneDeviceData from MainLink */
  oneDeviceData = smFindInSharedcontext(smRoot, agDevHandle);

  if (oneDeviceData == agNULL)
  {
    return SM_RC_FAILURE;
  }
  else
  {
    if (oneDeviceData->valid == agTRUE)
    {
      smDeviceDataReInit(smRoot, oneDeviceData);
      tdsmSingleThreadedEnter(smRoot, SM_DEVICE_LOCK);
      SMLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
      SMLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(smAllShared->FreeDeviceList));
      tdsmSingleThreadedLeave(smRoot, SM_DEVICE_LOCK);
      return SM_RC_SUCCESS;
    }
    else
    {
      SM_DBG1(("smRemoveFromSharedcontext: did %d bad case!!!\n", oneDeviceData->id));
      return SM_RC_FAILURE;
    }
  }

}

osGLOBAL smDeviceData_t *
smFindInSharedcontext(
                      smRoot_t                  *smRoot,
                      agsaDevHandle_t           *agDevHandle
                      )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  smDeviceData_t            *oneDeviceData = agNULL;
  smList_t                  *DeviceListList;

  SM_DBG2(("smFindInSharedcontext: start\n"));

  tdsmSingleThreadedEnter(smRoot, SM_DEVICE_LOCK);
  if (SMLIST_EMPTY(&(smAllShared->MainDeviceList)))
  {
    SM_DBG1(("smFindInSharedcontext: empty MainDeviceList!!!\n"));
    tdsmSingleThreadedLeave(smRoot, SM_DEVICE_LOCK);
    return agNULL;
  }
  else
  {
    tdsmSingleThreadedLeave(smRoot, SM_DEVICE_LOCK);
  }

  DeviceListList = smAllShared->MainDeviceList.flink;
  while (DeviceListList != &(smAllShared->MainDeviceList))
  {
    oneDeviceData = SMLIST_OBJECT_BASE(smDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smFindInSharedcontext: oneDeviceData is NULL!!!\n"));
      return agNULL;
    }
    if ((oneDeviceData->agDevHandle == agDevHandle) &&
        (oneDeviceData->valid == agTRUE)
       )
    {
      SM_DBG2(("smFindInSharedcontext: found, did %d\n", oneDeviceData->id));
      return oneDeviceData;
    }
    DeviceListList = DeviceListList->flink;
  }
  SM_DBG2(("smFindInSharedcontext: not found\n"));
  return agNULL;
}

osGLOBAL smSatIOContext_t *
smsatPrepareNewIO(
                  smSatInternalIo_t       *satNewIntIo,
                  smIORequest_t           *smOrgIORequest,
                  smDeviceData_t          *satDevData,
                  smIniScsiCmnd_t         *scsiCmnd,
                  smSatIOContext_t        *satOrgIOContext
                 )
{
  smSatIOContext_t        *satNewIOContext;
  smIORequestBody_t       *smNewIORequestBody;

  SM_DBG3(("smsatPrepareNewIO: start\n"));

  /* the one to be used; good 8/2/07 */
  satNewIntIo->satOrgSmIORequest = smOrgIORequest; /* this is already done in
                                                      smsatAllocIntIoResource() */

  smNewIORequestBody = (smIORequestBody_t *)satNewIntIo->satIntRequestBody;
  satNewIOContext = &(smNewIORequestBody->transport.SATA.satIOContext);

  satNewIOContext->pSatDevData   = satDevData;
  satNewIOContext->pFis          = &(smNewIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satNewIOContext->pScsiCmnd     = &(satNewIntIo->satIntSmScsiXchg.scsiCmnd);
  if (scsiCmnd != agNULL)
  {
    /* saves only CBD; not scsi command for LBA and number of blocks */
    sm_memcpy(satNewIOContext->pScsiCmnd->cdb, scsiCmnd->cdb, 16);
  }
  satNewIOContext->pSense        = &(smNewIORequestBody->transport.SATA.sensePayload);
  satNewIOContext->pSmSenseData  = &(smNewIORequestBody->transport.SATA.smSenseData);
  satNewIOContext->pSmSenseData->senseData = satNewIOContext->pSense;
  satNewIOContext->smRequestBody = satNewIntIo->satIntRequestBody;
  satNewIOContext->interruptContext = satNewIOContext->interruptContext;
  satNewIOContext->satIntIoContext  = satNewIntIo;
  satNewIOContext->psmDeviceHandle = satOrgIOContext->psmDeviceHandle;
  satNewIOContext->satOrgIOContext = satOrgIOContext;
  /* saves tiScsiXchg; only for writesame10() */
  satNewIOContext->smScsiXchg = satOrgIOContext->smScsiXchg;

  return satNewIOContext;
}


osGLOBAL void
smsatSetDevInfo(
                 smDeviceData_t            *oneDeviceData,
                 agsaSATAIdentifyData_t    *SATAIdData
               )
{
  SM_DBG3(("smsatSetDevInfo: start\n"));

  oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
  oneDeviceData->satFormatState = agFALSE;
  oneDeviceData->satDeviceFaultState = agFALSE;
  oneDeviceData->satTmTaskTag  = agNULL;
  oneDeviceData->satAbortAfterReset = agFALSE;
  oneDeviceData->satAbortCalled = agFALSE;
  oneDeviceData->satSectorDone  = 0;

  /* Qeueu depth, Word 75 */
  oneDeviceData->satNCQMaxIO = SATAIdData->queueDepth + 1;
  SM_DBG3(("smsatSetDevInfo: max queue depth %d\n",oneDeviceData->satNCQMaxIO));

  /* Support NCQ, if Word 76 bit 8 is set */
  if (SATAIdData->sataCapabilities & 0x100)
  {
    SM_DBG3(("smsatSetDevInfo: device supports NCQ\n"));
    oneDeviceData->satNCQ   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no NCQ\n"));
    oneDeviceData->satNCQ = agFALSE;
  }

  /* Support 48 bit addressing, if Word 83 bit 10 and Word 86 bit 10 are set */
  if ((SATAIdData->commandSetSupported1 & 0x400) &&
      (SATAIdData->commandSetFeatureEnabled1 & 0x400) )
  {
    SM_DBG3(("smsatSetDevInfo: support 48 bit addressing\n"));
    oneDeviceData->sat48BitSupport = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: NO 48 bit addressing\n"));
    oneDeviceData->sat48BitSupport = agFALSE;
  }

  /* Support SMART Self Test, word84 bit 1 */
  if (SATAIdData->commandSetFeatureSupportedExt & 0x02)
  {
    SM_DBG3(("smsatSetDevInfo: SMART self-test supported \n"));
    oneDeviceData->satSMARTSelfTest   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no SMART self-test suppored\n"));
    oneDeviceData->satSMARTSelfTest = agFALSE;
  }

  /* Support SMART feature set, word82 bit 0 */
  if (SATAIdData->commandSetSupported & 0x01)
  {
    SM_DBG3(("smsatSetDevInfo: SMART feature set supported \n"));
    oneDeviceData->satSMARTFeatureSet   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no SMART feature set suppored\n"));
    oneDeviceData->satSMARTFeatureSet = agFALSE;
  }

  /* Support SMART enabled, word85 bit 0 */
  if (SATAIdData->commandSetFeatureEnabled & 0x01)
  {
    SM_DBG3(("smsatSetDevInfo: SMART enabled \n"));
    oneDeviceData->satSMARTEnabled   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no SMART enabled\n"));
    oneDeviceData->satSMARTEnabled = agFALSE;
  }

  oneDeviceData->satVerifyState = 0;

  /* Removable Media feature set support, word82 bit 2 */
  if (SATAIdData->commandSetSupported & 0x4)
  {
    SM_DBG3(("smsatSetDevInfo: Removable Media supported \n"));
    oneDeviceData->satRemovableMedia   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no Removable Media suppored\n"));
    oneDeviceData->satRemovableMedia = agFALSE;
  }

  /* Removable Media feature set enabled, word 85, bit 2 */
  if (SATAIdData->commandSetFeatureEnabled & 0x4)
  {
    SM_DBG3(("smsatSetDevInfo: Removable Media enabled\n"));
    oneDeviceData->satRemovableMediaEnabled   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no Removable Media enabled\n"));
    oneDeviceData->satRemovableMediaEnabled = agFALSE;
  }

  /* DMA Support, word49 bit8 */
  if (SATAIdData->dma_lba_iod_ios_stimer & 0x100)
  {
    SM_DBG3(("smsatSetDevInfo: DMA supported \n"));
    oneDeviceData->satDMASupport   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no DMA suppored\n"));
    oneDeviceData->satDMASupport = agFALSE;
  }

  /* Support DMADIR, if Word 62 bit 8 is set */
  if (SATAIdData->word62_74[0] & 0x8000)
  {
     SM_DBG3(("satSetDevInfo: DMADIR enabled\n"));
     oneDeviceData->satDMADIRSupport   = agTRUE;
  }
  else
  {
     SM_DBG3(("satSetDevInfo: DMADIR disabled\n"));
     oneDeviceData->satDMADIRSupport   = agFALSE;
  }

  /* DMA Enabled, word88 bit0-6, bit8-14*/
  /* 0x7F7F = 0111 1111 0111 1111*/
  if (SATAIdData->ultraDMAModes & 0x7F7F)
  {
    SM_DBG3(("smsatSetDevInfo: DMA enabled \n"));
    oneDeviceData->satDMAEnabled   = agTRUE;
    if (SATAIdData->ultraDMAModes & 0x40)
    {
       oneDeviceData->satUltraDMAMode = 6;
    }
    else if (SATAIdData->ultraDMAModes & 0x20)
    {
       oneDeviceData->satUltraDMAMode = 5;
    }
    else if (SATAIdData->ultraDMAModes & 0x10)
    {
       oneDeviceData->satUltraDMAMode = 4;
    }
    else if (SATAIdData->ultraDMAModes & 0x08)
    {
       oneDeviceData->satUltraDMAMode = 3;
    }
    else if (SATAIdData->ultraDMAModes & 0x04)
    {
       oneDeviceData->satUltraDMAMode = 2;
    }
    else if (SATAIdData->ultraDMAModes & 0x01)
    {
       oneDeviceData->satUltraDMAMode = 1;
    }
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no DMA enabled\n"));
    oneDeviceData->satDMAEnabled = agFALSE;
    oneDeviceData->satUltraDMAMode = 0;
  }

  /*
    setting MaxUserAddrSectors: max user addressable setctors
    word60 - 61, should be 0x 0F FF FF FF
  */
  oneDeviceData->satMaxUserAddrSectors
    = (SATAIdData->numOfUserAddressableSectorsHi << (8*2) )
    + SATAIdData->numOfUserAddressableSectorsLo;
  SM_DBG3(("smsatSetDevInfo: MaxUserAddrSectors 0x%x decimal %d\n", oneDeviceData->satMaxUserAddrSectors, oneDeviceData->satMaxUserAddrSectors));

  /* Read Look-ahead is supported */
  if (SATAIdData->commandSetSupported & 0x40)
  {
    SM_DBG3(("smsatSetDevInfo: Read Look-ahead is supported\n"));
    oneDeviceData->satReadLookAheadSupport= agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: Read Look-ahead is not supported\n"));
    oneDeviceData->satReadLookAheadSupport= agFALSE;
  }

  /* Volatile Write Cache is supported */
  if (SATAIdData->commandSetSupported & 0x20)
  {
    SM_DBG3(("smsatSetDevInfo: Volatile Write Cache is supported\n"));
    oneDeviceData->satVolatileWriteCacheSupport = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: Volatile Write Cache is not supported\n"));
    oneDeviceData->satVolatileWriteCacheSupport = agFALSE;
  }

  /* write cache enabled for caching mode page SAT Table 67 p69, word85 bit5 */
  if (SATAIdData->commandSetFeatureEnabled & 0x20)
  {
    SM_DBG3(("smsatSetDevInfo: write cache enabled\n"));
    oneDeviceData->satWriteCacheEnabled   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no write cache enabled\n"));
    oneDeviceData->satWriteCacheEnabled = agFALSE;
  }

  /* look ahead enabled for caching mode page SAT Table 67 p69, word85 bit6 */
  if (SATAIdData->commandSetFeatureEnabled & 0x40)
  {
    SM_DBG3(("smsatSetDevInfo: look ahead enabled\n"));
    oneDeviceData->satLookAheadEnabled   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no look ahead enabled\n"));
    oneDeviceData->satLookAheadEnabled = agFALSE;
  }

  /* Support WWN, if Word 87 bit 8 is set */
  if (SATAIdData->commandSetFeatureDefault & 0x100)
  {
    SM_DBG3(("smsatSetDevInfo: device supports WWN\n"));
    oneDeviceData->satWWNSupport   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no WWN\n"));
    oneDeviceData->satWWNSupport = agFALSE;
  }

  /* Support DMA Setup Auto-Activate, if Word 78 bit 2 is set */
  if (SATAIdData->sataFeaturesSupported & 0x4)
  {
    SM_DBG3(("smsatSetDevInfo: device supports DMA Setup Auto-Activate\n"));
    oneDeviceData->satDMASetupAA   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no DMA Setup Auto-Activate\n"));
    oneDeviceData->satDMASetupAA = agFALSE;
  }

  /* Support NCQ Queue Management Command, if Word 77 bit 5 is set */
  if (SATAIdData->word77 & 0x10)
  {
    SM_DBG3(("smsatSetDevInfo: device supports NCQ Queue Management Command\n"));
    oneDeviceData->satNCQQMgntCmd   = agTRUE;
  }
  else
  {
    SM_DBG3(("smsatSetDevInfo: no NCQ Queue Management Command\n"));
    oneDeviceData->satNCQQMgntCmd = agFALSE;
  }
  return;
}


osGLOBAL void
smsatInquiryStandard(
                     bit8                    *pInquiry,
                     agsaSATAIdentifyData_t  *pSATAIdData,
                     smIniScsiCmnd_t         *scsiCmnd
                    )
{
  smLUN_t       *pLun;
  pLun          = &scsiCmnd->lun;

  /*
    Assumption: Basic Task Mangement is supported
    -> BQUE 1 and CMDQUE 0, SPC-4, Table96, p147
  */
 /*
    See SPC-4, 6.4.2, p 143
    and SAT revision 8, 8.1.2, p 28
   */
  SM_DBG5(("smsatInquiryStandard: start\n"));

  if (pInquiry == agNULL)
  {
    SM_DBG1(("smsatInquiryStandard: pInquiry is NULL, wrong\n"));
    return;
  }
  else
  {
    SM_DBG5(("smsatInquiryStandard: pInquiry is NOT NULL\n"));
  }
  /*
   * Reject all other LUN other than LUN 0.
   */
  if ( ((pLun->lun[0] | pLun->lun[1] | pLun->lun[2] | pLun->lun[3] |
         pLun->lun[4] | pLun->lun[5] | pLun->lun[6] | pLun->lun[7] ) != 0) )
  {
    /* SAT Spec Table 8, p27, footnote 'a' */
    pInquiry[0] = 0x7F;

  }
  else
  {
    pInquiry[0] = 0x00;
  }

  if (pSATAIdData->rm_ataDevice & ATA_REMOVABLE_MEDIA_DEVICE_MASK )
  {
    pInquiry[1] = 0x80;
  }
  else
  {
    pInquiry[1] = 0x00;
  }
  pInquiry[2] = 0x05;   /* SPC-3 */
  pInquiry[3] = 0x12;   /* set HiSup 1; resp data format set to 2 */
  pInquiry[4] = 0x1F;   /* 35 - 4 = 31; Additional length */
  pInquiry[5] = 0x00;
  /* The following two are for task management. SAT Rev8, p20 */
  if (pSATAIdData->sataCapabilities & 0x100)
  {
    /* NCQ supported; multiple outstanding SCSI IO are supported */
    pInquiry[6] = 0x00;   /* BQUE bit is not set */
    pInquiry[7] = 0x02;   /* CMDQUE bit is set */
  }
  else
  {
    pInquiry[6] = 0x80;   /* BQUE bit is set */
    pInquiry[7] = 0x00;   /* CMDQUE bit is not set */
  }
  /*
   * Vendor ID.
   */
  sm_strncpy((char*)&pInquiry[8],  AG_SAT_VENDOR_ID_STRING, 8);   /* 8 bytes   */

  /*
   * Product ID
   */
  /* when flipped by LL */
  pInquiry[16] = pSATAIdData->modelNumber[1];
  pInquiry[17] = pSATAIdData->modelNumber[0];
  pInquiry[18] = pSATAIdData->modelNumber[3];
  pInquiry[19] = pSATAIdData->modelNumber[2];
  pInquiry[20] = pSATAIdData->modelNumber[5];
  pInquiry[21] = pSATAIdData->modelNumber[4];
  pInquiry[22] = pSATAIdData->modelNumber[7];
  pInquiry[23] = pSATAIdData->modelNumber[6];
  pInquiry[24] = pSATAIdData->modelNumber[9];
  pInquiry[25] = pSATAIdData->modelNumber[8];
  pInquiry[26] = pSATAIdData->modelNumber[11];
  pInquiry[27] = pSATAIdData->modelNumber[10];
  pInquiry[28] = pSATAIdData->modelNumber[13];
  pInquiry[29] = pSATAIdData->modelNumber[12];
  pInquiry[30] = pSATAIdData->modelNumber[15];
  pInquiry[31] = pSATAIdData->modelNumber[14];

  /* when flipped */
  /*
   * Product Revision level.
   */

  /*
   * If the IDENTIFY DEVICE data received in words 25 and 26 from the ATA
   * device are ASCII spaces (20h), do this translation.
   */
  if ( (pSATAIdData->firmwareVersion[4] == 0x20 ) &&
       (pSATAIdData->firmwareVersion[5] == 0x20 ) &&
       (pSATAIdData->firmwareVersion[6] == 0x20 ) &&
       (pSATAIdData->firmwareVersion[7] == 0x20 )
       )
  {
    pInquiry[32] = pSATAIdData->firmwareVersion[1];
    pInquiry[33] = pSATAIdData->firmwareVersion[0];
    pInquiry[34] = pSATAIdData->firmwareVersion[3];
    pInquiry[35] = pSATAIdData->firmwareVersion[2];
  }
  else
  {
    pInquiry[32] = pSATAIdData->firmwareVersion[5];
    pInquiry[33] = pSATAIdData->firmwareVersion[4];
    pInquiry[34] = pSATAIdData->firmwareVersion[7];
    pInquiry[35] = pSATAIdData->firmwareVersion[6];
  }


#ifdef REMOVED
  /*
   * Product ID
   */
  /* when flipped by LL */
  pInquiry[16] = pSATAIdData->modelNumber[0];
  pInquiry[17] = pSATAIdData->modelNumber[1];
  pInquiry[18] = pSATAIdData->modelNumber[2];
  pInquiry[19] = pSATAIdData->modelNumber[3];
  pInquiry[20] = pSATAIdData->modelNumber[4];
  pInquiry[21] = pSATAIdData->modelNumber[5];
  pInquiry[22] = pSATAIdData->modelNumber[6];
  pInquiry[23] = pSATAIdData->modelNumber[7];
  pInquiry[24] = pSATAIdData->modelNumber[8];
  pInquiry[25] = pSATAIdData->modelNumber[9];
  pInquiry[26] = pSATAIdData->modelNumber[10];
  pInquiry[27] = pSATAIdData->modelNumber[11];
  pInquiry[28] = pSATAIdData->modelNumber[12];
  pInquiry[29] = pSATAIdData->modelNumber[13];
  pInquiry[30] = pSATAIdData->modelNumber[14];
  pInquiry[31] = pSATAIdData->modelNumber[15];

  /* when flipped */
  /*
   * Product Revision level.
   */

  /*
   * If the IDENTIFY DEVICE data received in words 25 and 26 from the ATA
   * device are ASCII spaces (20h), do this translation.
   */
  if ( (pSATAIdData->firmwareVersion[4] == 0x20 ) &&
       (pSATAIdData->firmwareVersion[5] == 0x20 ) &&
       (pSATAIdData->firmwareVersion[6] == 0x20 ) &&
       (pSATAIdData->firmwareVersion[7] == 0x20 )
       )
  {
    pInquiry[32] = pSATAIdData->firmwareVersion[0];
    pInquiry[33] = pSATAIdData->firmwareVersion[1];
    pInquiry[34] = pSATAIdData->firmwareVersion[2];
    pInquiry[35] = pSATAIdData->firmwareVersion[3];
  }
  else
  {
    pInquiry[32] = pSATAIdData->firmwareVersion[4];
    pInquiry[33] = pSATAIdData->firmwareVersion[5];
    pInquiry[34] = pSATAIdData->firmwareVersion[6];
    pInquiry[35] = pSATAIdData->firmwareVersion[7];
  }
#endif

  SM_DBG5(("smsatInquiryStandard: end\n"));

  return;
}

osGLOBAL void
smsatInquiryPage0(
                   bit8                    *pInquiry,
                   agsaSATAIdentifyData_t  *pSATAIdData
     )
{
  SM_DBG5(("smsatInquiryPage0: start\n"));

  /*
    See SPC-4, 7.6.9, p 345
    and SAT revision 8, 10.3.2, p 77
   */
  pInquiry[0] = 0x00;
  pInquiry[1] = 0x00; /* page code */
  pInquiry[2] = 0x00; /* reserved */
  pInquiry[3] = 8 - 3; /* last index(in this case, 6) - 3; page length */

  /* supported vpd page list */
  pInquiry[4] = 0x00; /* page 0x00 supported */
  pInquiry[5] = 0x80; /* page 0x80 supported */
  pInquiry[6] = 0x83; /* page 0x83 supported */
  pInquiry[7] = 0x89; /* page 0x89 supported */
  pInquiry[8] = 0xB1; /* page 0xB1 supported */

  return;
}

osGLOBAL void
smsatInquiryPage83(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData,
                    smDeviceData_t          *oneDeviceData
      )
{
  satSimpleSATAIdentifyData_t   *pSimpleData;

  /*
   * When translating the fields, in some cases using the simple form of SATA
   * Identify Device Data is easier. So we define it here.
   * Both pSimpleData and pSATAIdData points to the same data.
   */
  pSimpleData = ( satSimpleSATAIdentifyData_t *)pSATAIdData;

  SM_DBG5(("smsatInquiryPage83: start\n"));

  pInquiry[0] = 0x00;
  pInquiry[1] = 0x83; /* page code */
  pInquiry[2] = 0;    /* Reserved */
  /*
   * If the ATA device returns word 87 bit 8 set to one in its IDENTIFY DEVICE
   * data indicating that it supports the WORLD WIDE NAME field
   * (i.e., words 108-111), the SATL shall include an identification descriptor
   * containing a logical unit name.
   */
  if ( oneDeviceData->satWWNSupport)
  {
#ifndef PMC_FREEBSD  
    /* Fill in SAT Rev8 Table85 */
    /*
     * Logical unit name derived from the world wide name.
     */
    pInquiry[3] = 12;         /* 15-3; page length, no addition ID descriptor assumed*/

    /*
     * Identifier descriptor
     */
    pInquiry[4]  = 0x01;                        /* Code set: binary codes */
    pInquiry[5]  = 0x03;                        /* Identifier type : NAA  */
    pInquiry[6]  = 0x00;                        /* Reserved               */
    pInquiry[7]  = 0x08;                        /* Identifier length      */

    /* Bit 4-7 NAA field, bit 0-3 MSB of IEEE Company ID */
    pInquiry[8]  = (bit8)((pSATAIdData->namingAuthority) >> 8);
    pInquiry[9]  = (bit8)((pSATAIdData->namingAuthority) & 0xFF);           /* IEEE Company ID */
    pInquiry[10] = (bit8)((pSATAIdData->namingAuthority1) >> 8);            /* IEEE Company ID */
    /* Bit 4-7 LSB of IEEE Company ID, bit 0-3 MSB of Vendor Specific ID */
    pInquiry[11] = (bit8)((pSATAIdData->namingAuthority1) & 0xFF);
    pInquiry[12] = (bit8)((pSATAIdData->uniqueID_bit16_31) >> 8);       /* Vendor Specific ID  */
    pInquiry[13] = (bit8)((pSATAIdData->uniqueID_bit16_31) & 0xFF);     /* Vendor Specific ID  */
    pInquiry[14] = (bit8)((pSATAIdData->uniqueID_bit0_15) >> 8);        /* Vendor Specific ID  */
    pInquiry[15] = (bit8)((pSATAIdData->uniqueID_bit0_15) & 0xFF);      /* Vendor Specific ID  */
    
#else

    /* For FreeBSD */

    /* Fill in SAT Rev8 Table85 */
    /*
     * Logical unit name derived from the world wide name.
     */
    pInquiry[3] = 24;         /* 35-3; page length, no addition ID descriptor assumed*/
   /*
     * Identifier descriptor
     */
    pInquiry[4]  = 0x01;                        /* Code set: binary codes; this is proto_codeset in FreeBSD */
    pInquiry[5]  = 0x03;                        /* Identifier type : NAA ; this is  id_type in FreeBSD*/
    pInquiry[6]  = 0x00;                        /* Reserved               */
    pInquiry[7]  = 0x08;                        /* Identifier length      */

    /* Bit 4-7 NAA field, bit 0-3 MSB of IEEE Company ID */
    pInquiry[8]  = (bit8)((pSATAIdData->namingAuthority) >> 8);
    pInquiry[9]  = (bit8)((pSATAIdData->namingAuthority) & 0xFF);           /* IEEE Company ID */
    pInquiry[10] = (bit8)((pSATAIdData->namingAuthority1) >> 8);            /* IEEE Company ID */
    /* Bit 4-7 LSB of IEEE Company ID, bit 0-3 MSB of Vendor Specific ID */
    pInquiry[11] = (bit8)((pSATAIdData->namingAuthority1) & 0xFF);
    pInquiry[12] = (bit8)((pSATAIdData->uniqueID_bit16_31) >> 8);       /* Vendor Specific ID  */
    pInquiry[13] = (bit8)((pSATAIdData->uniqueID_bit16_31) & 0xFF);     /* Vendor Specific ID  */
    pInquiry[14] = (bit8)((pSATAIdData->uniqueID_bit0_15) >> 8);        /* Vendor Specific ID  */
    pInquiry[15] = (bit8)((pSATAIdData->uniqueID_bit0_15) & 0xFF);      /* Vendor Specific ID  */

    pInquiry[16]  = 0x61;                        /* Code set: binary codes; this is proto_codeset in FreeBSD; SCSI_PROTO_SAS and SVPD_ID_CODESET_BINARY */
    pInquiry[17]  = 0x93;                        /* Identifier type : NAA ; this is  id_type in FreeBSD; PIV set, ASSOCIATION is 01b and NAA (3h)   */
    pInquiry[18]  = 0x00;                        /* Reserved               */
    pInquiry[19]  = 0x08;                        /* Identifier length      */
    
    SM_DBG5(("smsatInquiryPage83: sasAddressHi 0x%08x\n", oneDeviceData->sasAddressHi));
    SM_DBG5(("smsatInquiryPage83: sasAddressLo 0x%08x\n", oneDeviceData->sasAddressLo));
    
    /* SAS address of SATA */
    pInquiry[20]  = ((oneDeviceData->sasAddressHi) & 0xFF000000 ) >> 24; 
    pInquiry[21]  = ((oneDeviceData->sasAddressHi) & 0xFF0000 ) >> 16;  
    pInquiry[22]  = ((oneDeviceData->sasAddressHi) & 0xFF00 ) >> 8;     
    pInquiry[23]  = (oneDeviceData->sasAddressHi) & 0xFF;                        
    pInquiry[24]  = ((oneDeviceData->sasAddressLo) & 0xFF000000 ) >> 24;                        
    pInquiry[25]  = ((oneDeviceData->sasAddressLo) & 0xFF0000 ) >> 16;                       
    pInquiry[26]  = ((oneDeviceData->sasAddressLo) & 0xFF00 ) >> 8;                        
    pInquiry[27]  = (oneDeviceData->sasAddressLo) & 0xFF;                        
#endif        
  }
  else
  {
#ifndef PMC_FREEBSD  
    /* Fill in SAT Rev8 Table86 */
    /*
     * Logical unit name derived from the model number and serial number.
     */
    pInquiry[3] = 72;    /* 75 - 3; page length */

    /*
     * Identifier descriptor
     */
    pInquiry[4] = 0x02;             /* Code set: ASCII codes */
    pInquiry[5] = 0x01;             /* Identifier type : T10 vendor ID based */
    pInquiry[6] = 0x00;             /* Reserved */
    pInquiry[7] = 0x44;               /* 0x44, 68 Identifier length */

    /* Byte 8 to 15 is the vendor id string 'ATA     '. */
    sm_strncpy((char *)&pInquiry[8], AG_SAT_VENDOR_ID_STRING, 8);


        /*
     * Byte 16 to 75 is vendor specific id
     */
    pInquiry[16] = (bit8)((pSimpleData->word[27]) >> 8);
    pInquiry[17] = (bit8)((pSimpleData->word[27]) & 0x00ff);
    pInquiry[18] = (bit8)((pSimpleData->word[28]) >> 8);
    pInquiry[19] = (bit8)((pSimpleData->word[28]) & 0x00ff);
    pInquiry[20] = (bit8)((pSimpleData->word[29]) >> 8);
    pInquiry[21] = (bit8)((pSimpleData->word[29]) & 0x00ff);
    pInquiry[22] = (bit8)((pSimpleData->word[30]) >> 8);
    pInquiry[23] = (bit8)((pSimpleData->word[30]) & 0x00ff);
    pInquiry[24] = (bit8)((pSimpleData->word[31]) >> 8);
    pInquiry[25] = (bit8)((pSimpleData->word[31]) & 0x00ff);
    pInquiry[26] = (bit8)((pSimpleData->word[32]) >> 8);
    pInquiry[27] = (bit8)((pSimpleData->word[32]) & 0x00ff);
    pInquiry[28] = (bit8)((pSimpleData->word[33]) >> 8);
    pInquiry[29] = (bit8)((pSimpleData->word[33]) & 0x00ff);
    pInquiry[30] = (bit8)((pSimpleData->word[34]) >> 8);
    pInquiry[31] = (bit8)((pSimpleData->word[34]) & 0x00ff);
    pInquiry[32] = (bit8)((pSimpleData->word[35]) >> 8);
    pInquiry[33] = (bit8)((pSimpleData->word[35]) & 0x00ff);
    pInquiry[34] = (bit8)((pSimpleData->word[36]) >> 8);
    pInquiry[35] = (bit8)((pSimpleData->word[36]) & 0x00ff);
    pInquiry[36] = (bit8)((pSimpleData->word[37]) >> 8);
    pInquiry[37] = (bit8)((pSimpleData->word[37]) & 0x00ff);
    pInquiry[38] = (bit8)((pSimpleData->word[38]) >> 8);
    pInquiry[39] = (bit8)((pSimpleData->word[38]) & 0x00ff);
    pInquiry[40] = (bit8)((pSimpleData->word[39]) >> 8);
    pInquiry[41] = (bit8)((pSimpleData->word[39]) & 0x00ff);
    pInquiry[42] = (bit8)((pSimpleData->word[40]) >> 8);
    pInquiry[43] = (bit8)((pSimpleData->word[40]) & 0x00ff);
    pInquiry[44] = (bit8)((pSimpleData->word[41]) >> 8);
    pInquiry[45] = (bit8)((pSimpleData->word[41]) & 0x00ff);
    pInquiry[46] = (bit8)((pSimpleData->word[42]) >> 8);
    pInquiry[47] = (bit8)((pSimpleData->word[42]) & 0x00ff);
    pInquiry[48] = (bit8)((pSimpleData->word[43]) >> 8);
    pInquiry[49] = (bit8)((pSimpleData->word[43]) & 0x00ff);
    pInquiry[50] = (bit8)((pSimpleData->word[44]) >> 8);
    pInquiry[51] = (bit8)((pSimpleData->word[44]) & 0x00ff);
    pInquiry[52] = (bit8)((pSimpleData->word[45]) >> 8);
    pInquiry[53] = (bit8)((pSimpleData->word[45]) & 0x00ff);
    pInquiry[54] = (bit8)((pSimpleData->word[46]) >> 8);
    pInquiry[55] = (bit8)((pSimpleData->word[46]) & 0x00ff);

    pInquiry[56] = (bit8)((pSimpleData->word[10]) >> 8);
    pInquiry[57] = (bit8)((pSimpleData->word[10]) & 0x00ff);
    pInquiry[58] = (bit8)((pSimpleData->word[11]) >> 8);
    pInquiry[59] = (bit8)((pSimpleData->word[11]) & 0x00ff);
    pInquiry[60] = (bit8)((pSimpleData->word[12]) >> 8);
    pInquiry[61] = (bit8)((pSimpleData->word[12]) & 0x00ff);
    pInquiry[62] = (bit8)((pSimpleData->word[13]) >> 8);
    pInquiry[63] = (bit8)((pSimpleData->word[13]) & 0x00ff);
    pInquiry[64] = (bit8)((pSimpleData->word[14]) >> 8);
    pInquiry[65] = (bit8)((pSimpleData->word[14]) & 0x00ff);
    pInquiry[66] = (bit8)((pSimpleData->word[15]) >> 8);
    pInquiry[67] = (bit8)((pSimpleData->word[15]) & 0x00ff);
    pInquiry[68] = (bit8)((pSimpleData->word[16]) >> 8);
    pInquiry[69] = (bit8)((pSimpleData->word[16]) & 0x00ff);
    pInquiry[70] = (bit8)((pSimpleData->word[17]) >> 8);
    pInquiry[71] = (bit8)((pSimpleData->word[17]) & 0x00ff);
    pInquiry[72] = (bit8)((pSimpleData->word[18]) >> 8);
    pInquiry[73] = (bit8)((pSimpleData->word[18]) & 0x00ff);
    pInquiry[74] = (bit8)((pSimpleData->word[19]) >> 8);
    pInquiry[75] = (bit8)((pSimpleData->word[19]) & 0x00ff);
#else
    /* for the FreeBSD */
    /* Fill in SAT Rev8 Table86 */
    /*
     * Logical unit name derived from the model number and serial number.
     */
    pInquiry[3] = 84;    /* 87 - 3; page length */

    /*
     * Identifier descriptor
     */
    pInquiry[4] = 0x02;             /* Code set: ASCII codes */
    pInquiry[5] = 0x01;             /* Identifier type : T10 vendor ID based */
    pInquiry[6] = 0x00;             /* Reserved */
    pInquiry[7] = 0x44;               /* 0x44, 68 Identifier length */

    /* Byte 8 to 15 is the vendor id string 'ATA     '. */
    sm_strncpy((char *)&pInquiry[8], AG_SAT_VENDOR_ID_STRING, 8);


        /*
     * Byte 16 to 75 is vendor specific id
     */
    pInquiry[16] = (bit8)((pSimpleData->word[27]) >> 8);
    pInquiry[17] = (bit8)((pSimpleData->word[27]) & 0x00ff);
    pInquiry[18] = (bit8)((pSimpleData->word[28]) >> 8);
    pInquiry[19] = (bit8)((pSimpleData->word[28]) & 0x00ff);
    pInquiry[20] = (bit8)((pSimpleData->word[29]) >> 8);
    pInquiry[21] = (bit8)((pSimpleData->word[29]) & 0x00ff);
    pInquiry[22] = (bit8)((pSimpleData->word[30]) >> 8);
    pInquiry[23] = (bit8)((pSimpleData->word[30]) & 0x00ff);
    pInquiry[24] = (bit8)((pSimpleData->word[31]) >> 8);
    pInquiry[25] = (bit8)((pSimpleData->word[31]) & 0x00ff);
    pInquiry[26] = (bit8)((pSimpleData->word[32]) >> 8);
    pInquiry[27] = (bit8)((pSimpleData->word[32]) & 0x00ff);
    pInquiry[28] = (bit8)((pSimpleData->word[33]) >> 8);
    pInquiry[29] = (bit8)((pSimpleData->word[33]) & 0x00ff);
    pInquiry[30] = (bit8)((pSimpleData->word[34]) >> 8);
    pInquiry[31] = (bit8)((pSimpleData->word[34]) & 0x00ff);
    pInquiry[32] = (bit8)((pSimpleData->word[35]) >> 8);
    pInquiry[33] = (bit8)((pSimpleData->word[35]) & 0x00ff);
    pInquiry[34] = (bit8)((pSimpleData->word[36]) >> 8);
    pInquiry[35] = (bit8)((pSimpleData->word[36]) & 0x00ff);
    pInquiry[36] = (bit8)((pSimpleData->word[37]) >> 8);
    pInquiry[37] = (bit8)((pSimpleData->word[37]) & 0x00ff);
    pInquiry[38] = (bit8)((pSimpleData->word[38]) >> 8);
    pInquiry[39] = (bit8)((pSimpleData->word[38]) & 0x00ff);
    pInquiry[40] = (bit8)((pSimpleData->word[39]) >> 8);
    pInquiry[41] = (bit8)((pSimpleData->word[39]) & 0x00ff);
    pInquiry[42] = (bit8)((pSimpleData->word[40]) >> 8);
    pInquiry[43] = (bit8)((pSimpleData->word[40]) & 0x00ff);
    pInquiry[44] = (bit8)((pSimpleData->word[41]) >> 8);
    pInquiry[45] = (bit8)((pSimpleData->word[41]) & 0x00ff);
    pInquiry[46] = (bit8)((pSimpleData->word[42]) >> 8);
    pInquiry[47] = (bit8)((pSimpleData->word[42]) & 0x00ff);
    pInquiry[48] = (bit8)((pSimpleData->word[43]) >> 8);
    pInquiry[49] = (bit8)((pSimpleData->word[43]) & 0x00ff);
    pInquiry[50] = (bit8)((pSimpleData->word[44]) >> 8);
    pInquiry[51] = (bit8)((pSimpleData->word[44]) & 0x00ff);
    pInquiry[52] = (bit8)((pSimpleData->word[45]) >> 8);
    pInquiry[53] = (bit8)((pSimpleData->word[45]) & 0x00ff);
    pInquiry[54] = (bit8)((pSimpleData->word[46]) >> 8);
    pInquiry[55] = (bit8)((pSimpleData->word[46]) & 0x00ff);

    pInquiry[56] = (bit8)((pSimpleData->word[10]) >> 8);
    pInquiry[57] = (bit8)((pSimpleData->word[10]) & 0x00ff);
    pInquiry[58] = (bit8)((pSimpleData->word[11]) >> 8);
    pInquiry[59] = (bit8)((pSimpleData->word[11]) & 0x00ff);
    pInquiry[60] = (bit8)((pSimpleData->word[12]) >> 8);
    pInquiry[61] = (bit8)((pSimpleData->word[12]) & 0x00ff);
    pInquiry[62] = (bit8)((pSimpleData->word[13]) >> 8);
    pInquiry[63] = (bit8)((pSimpleData->word[13]) & 0x00ff);
    pInquiry[64] = (bit8)((pSimpleData->word[14]) >> 8);
    pInquiry[65] = (bit8)((pSimpleData->word[14]) & 0x00ff);
    pInquiry[66] = (bit8)((pSimpleData->word[15]) >> 8);
    pInquiry[67] = (bit8)((pSimpleData->word[15]) & 0x00ff);
    pInquiry[68] = (bit8)((pSimpleData->word[16]) >> 8);
    pInquiry[69] = (bit8)((pSimpleData->word[16]) & 0x00ff);
    pInquiry[70] = (bit8)((pSimpleData->word[17]) >> 8);
    pInquiry[71] = (bit8)((pSimpleData->word[17]) & 0x00ff);
    pInquiry[72] = (bit8)((pSimpleData->word[18]) >> 8);
    pInquiry[73] = (bit8)((pSimpleData->word[18]) & 0x00ff);
    pInquiry[74] = (bit8)((pSimpleData->word[19]) >> 8);
    pInquiry[75] = (bit8)((pSimpleData->word[19]) & 0x00ff);

    pInquiry[76]  = 0x61;                        /* Code set: binary codes; this is proto_codeset in FreeBSD; SCSI_PROTO_SAS and SVPD_ID_CODESET_BINARY */
    pInquiry[77]  = 0x93;                        /* Identifier type : NAA ; this is  id_type in FreeBSD; PIV set, ASSOCIATION is 01b and NAA (3h)   */
    pInquiry[78]  = 0x00;                        /* Reserved               */
    pInquiry[79]  = 0x08;                        /* Identifier length      */
    
    SM_DBG5(("smsatInquiryPage83: NO WWN sasAddressHi 0x%08x\n", oneDeviceData->sasAddressHi));
    SM_DBG5(("smsatInquiryPage83: No WWN sasAddressLo 0x%08x\n", oneDeviceData->sasAddressLo));
    
    /* SAS address of SATA */
    pInquiry[80]  = ((oneDeviceData->sasAddressHi) & 0xFF000000 ) >> 24; 
    pInquiry[81]  = ((oneDeviceData->sasAddressHi) & 0xFF0000 ) >> 16;  
    pInquiry[82]  = ((oneDeviceData->sasAddressHi) & 0xFF00 ) >> 8;     
    pInquiry[83]  = (oneDeviceData->sasAddressHi) & 0xFF;                        
    pInquiry[84]  = ((oneDeviceData->sasAddressLo) & 0xFF000000 ) >> 24;                        
    pInquiry[85]  = ((oneDeviceData->sasAddressLo) & 0xFF0000 ) >> 16;                       
    pInquiry[86]  = ((oneDeviceData->sasAddressLo) & 0xFF00 ) >> 8;                        
    pInquiry[87]  = (oneDeviceData->sasAddressLo) & 0xFF;                

#endif    
  }
 
  return;
}

osGLOBAL void
smsatInquiryPage89(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData,
                    smDeviceData_t          *oneDeviceData,
                    bit32                   len		    
      )
{
  /*
    SAT revision 8, 10.3.5, p 83
   */
  satSimpleSATAIdentifyData_t   *pSimpleData;

  /*
   * When translating the fields, in some cases using the simple form of SATA
   * Identify Device Data is easier. So we define it here.
   * Both pSimpleData and pSATAIdData points to the same data.
   */
  pSimpleData = ( satSimpleSATAIdentifyData_t *)pSATAIdData;

  SM_DBG5(("smsatInquiryPage89: start\n"));

  pInquiry[0] = 0x00;   /* Peripheral Qualifier and Peripheral Device Type */
  pInquiry[1] = 0x89;   /* page code */

  /* Page length 0x238 */
  pInquiry[2] = 0x02;
  pInquiry[3] = 0x38;

  pInquiry[4] = 0x0;    /* reserved */
  pInquiry[5] = 0x0;    /* reserved */
  pInquiry[6] = 0x0;    /* reserved */
  pInquiry[7] = 0x0;    /* reserved */

  /* SAT Vendor Identification */
  sm_strncpy((char*)&pInquiry[8],  "PMC-SIERRA", 8);   /* 8 bytes   */

  /* SAT Product Idetification */
  sm_strncpy((char*)&pInquiry[16],  "Tachyon-SPC    ", 16);   /* 16 bytes   */

  /* SAT Product Revision Level */
  sm_strncpy((char*)&pInquiry[32],  "01", 4);   /* 4 bytes   */

  /* Signature, SAT revision8, Table88, p85 */


  pInquiry[36] = 0x34;    /* FIS type */
  if (oneDeviceData->satDeviceType == SATA_ATA_DEVICE)
  {
    /* interrupt assume to be 0 */
    pInquiry[37] = (bit8)((oneDeviceData->satPMField) >> (4 * 7)); /* first four bits of PM field */
  }
  else
  {
    /* interrupt assume to be 1 */
    pInquiry[37] = (bit8)(0x40 + (bit8)(((oneDeviceData->satPMField) >> (4 * 7)))); /* first four bits of PM field */
  }
  pInquiry[38] = 0;
  pInquiry[39] = 0;

  if (oneDeviceData->satDeviceType == SATA_ATA_DEVICE)
  {
    pInquiry[40] = 0x01; /* LBA Low          */
    pInquiry[41] = 0x00; /* LBA Mid          */
    pInquiry[42] = 0x00; /* LBA High         */
    pInquiry[43] = 0x00; /* Device           */
    pInquiry[44] = 0x00; /* LBA Low Exp      */
    pInquiry[45] = 0x00; /* LBA Mid Exp      */
    pInquiry[46] = 0x00; /* LBA High Exp     */
    pInquiry[47] = 0x00; /* Reserved         */
    pInquiry[48] = 0x01; /* Sector Count     */
    pInquiry[49] = 0x00; /* Sector Count Exp */
  }
  else
  {
    pInquiry[40] = 0x01; /* LBA Low          */
    pInquiry[41] = 0x00; /* LBA Mid          */
    pInquiry[42] = 0x00; /* LBA High         */
    pInquiry[43] = 0x00; /* Device           */
    pInquiry[44] = 0x00; /* LBA Low Exp      */
    pInquiry[45] = 0x00; /* LBA Mid Exp      */
    pInquiry[46] = 0x00; /* LBA High Exp     */
    pInquiry[47] = 0x00; /* Reserved         */
    pInquiry[48] = 0x01; /* Sector Count     */
    pInquiry[49] = 0x00; /* Sector Count Exp */
  }

  /* Reserved */
  pInquiry[50] = 0x00;
  pInquiry[51] = 0x00;
  pInquiry[52] = 0x00;
  pInquiry[53] = 0x00;
  pInquiry[54] = 0x00;
  pInquiry[55] = 0x00;

  /* Command Code */
  if (oneDeviceData->satDeviceType == SATA_ATA_DEVICE)
  {
    pInquiry[56] = 0xEC;    /* IDENTIFY DEVICE */
  }
  else
  {
    pInquiry[56] = 0xA1;    /* IDENTIFY PACKET DEVICE */
  }
  /* Reserved */
  pInquiry[57] = 0x0;
  pInquiry[58] = 0x0;
  pInquiry[59] = 0x0;

  /* check the length; len is assumed to be at least 60  */
  if (len < SATA_PAGE89_INQUIRY_SIZE)
  {
    /* Identify Device */
    sm_memcpy(&pInquiry[60], pSimpleData, MIN((len - 60), sizeof(satSimpleSATAIdentifyData_t)));
  }
  else
  {
    /* Identify Device */
    sm_memcpy(&pInquiry[60], pSimpleData, sizeof(satSimpleSATAIdentifyData_t));
  }

  return;
}

osGLOBAL void
smsatInquiryPage80(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData
       )
{
  SM_DBG5(("smsatInquiryPage89: start\n"));
  /*
    See SPC-4, 7.6.9, p 345
    and SAT revision 8, 10.3.3, p 77
   */
  pInquiry[0] = 0x00;
  pInquiry[1] = 0x80; /* page code */
  pInquiry[2] = 0x00; /* reserved */
  pInquiry[3] = 0x14; /* page length */

  /* product serial number */
  pInquiry[4] = pSATAIdData->serialNumber[1];
  pInquiry[5] = pSATAIdData->serialNumber[0];
  pInquiry[6] = pSATAIdData->serialNumber[3];
  pInquiry[7] = pSATAIdData->serialNumber[2];
  pInquiry[8] = pSATAIdData->serialNumber[5];
  pInquiry[9] = pSATAIdData->serialNumber[4];
  pInquiry[10] = pSATAIdData->serialNumber[7];
  pInquiry[11] = pSATAIdData->serialNumber[6];
  pInquiry[12] = pSATAIdData->serialNumber[9];
  pInquiry[13] = pSATAIdData->serialNumber[8];
  pInquiry[14] = pSATAIdData->serialNumber[11];
  pInquiry[15] = pSATAIdData->serialNumber[10];
  pInquiry[16] = pSATAIdData->serialNumber[13];
  pInquiry[17] = pSATAIdData->serialNumber[12];
  pInquiry[18] = pSATAIdData->serialNumber[15];
  pInquiry[19] = pSATAIdData->serialNumber[14];
  pInquiry[20] = pSATAIdData->serialNumber[17];
  pInquiry[21] = pSATAIdData->serialNumber[16];
  pInquiry[22] = pSATAIdData->serialNumber[19];
  pInquiry[23] = pSATAIdData->serialNumber[18];

  return;
}

osGLOBAL void
smsatInquiryPageB1(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData
       )
{
  bit32 i;
  satSimpleSATAIdentifyData_t   *pSimpleData;

  SM_DBG5(("smsatInquiryPageB1: start\n"));

  pSimpleData = ( satSimpleSATAIdentifyData_t *)pSATAIdData;
  /*
    See SBC-3, revision31, Table193, p273
    and SAT-3 revision 3, 10.3.6, p141
   */
  pInquiry[0] = 0x00;   /* Peripheral Qualifier and Peripheral Device Type */
  pInquiry[1] = 0xB1; /* page code */

  /* page length */
  pInquiry[2] = 0x0;
  pInquiry[3] = 0x3C;

  /* medium rotation rate */
  pInquiry[4] = (bit8) ((pSimpleData->word[217]) >> 8);
  pInquiry[5] = (bit8) ((pSimpleData->word[217]) & 0xFF);

  /* reserved */
  pInquiry[6] = 0x0;

  /* nominal form factor bits 3:0 */
  pInquiry[7] = (bit8) ((pSimpleData->word[168]) & 0xF);


  /* reserved */
  for (i=8;i<64;i++)
  {
    pInquiry[i] = 0x0;
  }
  return;
}

osGLOBAL void
smsatDefaultTranslation(
                        smRoot_t                  *smRoot,
                        smIORequest_t             *smIORequest,
                        smSatIOContext_t            *satIOContext,
                        smScsiRspSense_t          *pSense,
                        bit8                      ataStatus,
                        bit8                      ataError,
                        bit32                     interruptContext
                       )
{
  SM_DBG5(("smsatDefaultTranslation: start\n"));
  /*
   * Check for device fault case
   */
  if ( ataStatus & DF_ATA_STATUS_MASK )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_HARDWARE_ERROR,
                          0,
                          SCSI_SNSCODE_INTERNAL_TARGET_FAILURE,
                          satIOContext);

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       interruptContext );
    return;
  }

  /*
   * If status error bit it set, need to check the error register
   */
  if ( ataStatus & ERR_ATA_STATUS_MASK )
  {
    if ( ataError & NM_ATA_ERROR_MASK )
    {
      SM_DBG1(("smsatDefaultTranslation: NM_ATA_ERROR ataError= 0x%x, smIORequest=%p!!!\n",
                 ataError, smIORequest));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NOT_READY,
                            0,
                            SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                            satIOContext);
    }

    else if (ataError & UNC_ATA_ERROR_MASK)
    {
      SM_DBG1(("smsatDefaultTranslation: UNC_ATA_ERROR ataError= 0x%x, smIORequest=%p!!!\n",
                 ataError, smIORequest));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_MEDIUM_ERROR,
                            0,
                            SCSI_SNSCODE_UNRECOVERED_READ_ERROR,
                            satIOContext);
    }

    else if (ataError & IDNF_ATA_ERROR_MASK)
    {
      SM_DBG1(("smsatDefaultTranslation: IDNF_ATA_ERROR ataError= 0x%x, smIORequest=%p!!!\n",
                 ataError, smIORequest));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_MEDIUM_ERROR,
                            0,
                            SCSI_SNSCODE_RECORD_NOT_FOUND,
                            satIOContext);
    }

    else if (ataError & MC_ATA_ERROR_MASK)
    {
      SM_DBG1(("smsatDefaultTranslation: MC_ATA_ERROR ataError= 0x%x, smIORequest=%p!!!\n",
                 ataError, smIORequest));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_UNIT_ATTENTION,
                            0,
                            SCSI_SNSCODE_NOT_READY_TO_READY_CHANGE,
                            satIOContext);
    }

    else if (ataError & MCR_ATA_ERROR_MASK)
    {
      SM_DBG1(("smsatDefaultTranslation: MCR_ATA_ERROR ataError= 0x%x, smIORequest=%p!!!\n",
                 ataError, smIORequest));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_UNIT_ATTENTION,
                            0,
                            SCSI_SNSCODE_OPERATOR_MEDIUM_REMOVAL_REQUEST,
                            satIOContext);
    }

    else if (ataError & ICRC_ATA_ERROR_MASK)
    {
      SM_DBG1(("smsatDefaultTranslation: ICRC_ATA_ERROR ataError= 0x%x, smIORequest=%p!!!\n",
                 ataError, smIORequest));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ABORTED_COMMAND,
                            0,
                            SCSI_SNSCODE_INFORMATION_UNIT_CRC_ERROR,
                            satIOContext);
    }

    else if (ataError & ABRT_ATA_ERROR_MASK)
    {
      SM_DBG1(("smsatDefaultTranslation: ABRT_ATA_ERROR ataError= 0x%x, smIORequest=%p!!!\n",
                 ataError, smIORequest));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ABORTED_COMMAND,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satIOContext);
    }

    else
    {
      SM_DBG1(("smsatDefaultTranslation: **** UNEXPECTED ATA_ERROR **** ataError= 0x%x, smIORequest=%p!!!\n",
                 ataError, smIORequest));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_INTERNAL_TARGET_FAILURE,
                            satIOContext);
    }

    /* Send the completion response now */
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       interruptContext );
    return;


  }

  else /*  (ataStatus & ERR_ATA_STATUS_MASK ) is false */
  {
    /* This case should never happen */
    SM_DBG1(("smsatDefaultTranslation: *** UNEXPECTED ATA status 0x%x *** smIORequest=%p!!!\n",
                 ataStatus, smIORequest));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_HARDWARE_ERROR,
                          0,
                          SCSI_SNSCODE_INTERNAL_TARGET_FAILURE,
                          satIOContext);

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       interruptContext );
    return;

  }

  return;
}

osGLOBAL bit32
smIDStart(
          smRoot_t                     *smRoot,
          smIORequest_t                *smIORequest,
          smDeviceHandle_t             *smDeviceHandle
         )
{
  smDeviceData_t            *oneDeviceData = agNULL;
  smIORequestBody_t         *smIORequestBody = agNULL;
  smSatIOContext_t            *satIOContext = agNULL;
  bit32                     status = SM_RC_FAILURE;

  SM_DBG2(("smIDStart: start, smIORequest %p\n", smIORequest));

  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;
  if (oneDeviceData == agNULL)
  {
    SM_DBG1(("smIDStart: oneDeviceData is NULL!!!\n"));
    return SM_RC_FAILURE;
  }
  if (oneDeviceData->valid == agFALSE)
  {
    SM_DBG1(("smIDStart: oneDeviceData is not valid, did %d !!!\n", oneDeviceData->id));
    return SM_RC_FAILURE;
  }

  smIORequestBody = (smIORequestBody_t*)smIORequest->smData;//smDequeueIO(smRoot);

  if (smIORequestBody == agNULL)
  {
    SM_DBG1(("smIDStart: smIORequestBody is NULL!!!\n"));
    return SM_RC_FAILURE;
  }

  smIOReInit(smRoot, smIORequestBody);

  SM_DBG3(("smIDStart: io ID %d!!!\n", smIORequestBody->id ));

  smIORequestBody->smIORequest = smIORequest;
  smIORequestBody->smDevHandle = smDeviceHandle;
  satIOContext = &(smIORequestBody->transport.SATA.satIOContext);

  /* setting up satIOContext */
  satIOContext->pSatDevData   = oneDeviceData;
  satIOContext->pFis          = &(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satIOContext->smRequestBody = smIORequestBody;
  satIOContext->psmDeviceHandle = smDeviceHandle;
  satIOContext->smScsiXchg = agNULL;

  /*smIORequest->smData = smIORequestBody;*/
  SM_DBG3(("smIDStart: smIORequestBody %p smIORequestBody->smIORequest %p!!!\n", smIORequestBody, smIORequestBody->smIORequest));
  SM_DBG1(("smIDStart: did %d\n",  oneDeviceData->id));

  status = smsatIDSubStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            agNULL,
                            satIOContext);

  if (status != SM_RC_SUCCESS)
  {
    SM_DBG1(("smIDStart: smsatIDSubStart failure %d!!!\n", status));
    /*smEnqueueIO(smRoot, satIOContext);*/
  }
  SM_DBG2(("smIDStart: exit\n"));

  return status;
}

/*
  SM generated IO, needs to call smsatAllocIntIoResource()
  allocating using smsatAllocIntIoResource
*/
osGLOBAL bit32
smsatIDSubStart(
                 smRoot_t                 *smRoot,
                 smIORequest_t            *smIORequest,
                 smDeviceHandle_t         *smDeviceHandle,
                 smScsiInitiatorRequest_t *smSCSIRequest, /* agNULL */
                 smSatIOContext_t         *satIOContext
               )
{
  smSatInternalIo_t           *satIntIo = agNULL;
  smDeviceData_t            *satDevData = agNULL;
  smIORequestBody_t         *smIORequestBody;
  smSatIOContext_t            *satNewIOContext;
  bit32                     status;
  SM_DBG2(("smsatIDSubStart: start\n"));

  satDevData = satIOContext->pSatDevData;

  /* allocate identify device command */
  satIntIo = smsatAllocIntIoResource( smRoot,
                                      smIORequest,
                                      satDevData,
                                      sizeof(agsaSATAIdentifyData_t), /* 512; size of identify device data */
                                      satIntIo);

  if (satIntIo == agNULL)
  {
    SM_DBG1(("smsatIDSubStart: can't alloacate!!!\n"));
    return SM_RC_FAILURE;
  }

  satIOContext->satIntIoContext = satIntIo;

  /* fill in fields */
  /* real ttttttthe one worked and the same; 5/21/07/ */
  satIntIo->satOrgSmIORequest = smIORequest; /* changed */
  smIORequestBody = satIntIo->satIntRequestBody;
  satNewIOContext = &(smIORequestBody->transport.SATA.satIOContext);

  satNewIOContext->pSatDevData   = satDevData;
  satNewIOContext->pFis          = &(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satNewIOContext->pScsiCmnd     = &(satIntIo->satIntSmScsiXchg.scsiCmnd);
  satNewIOContext->pSense        = &(smIORequestBody->transport.SATA.sensePayload);
  satNewIOContext->pSmSenseData  = &(smIORequestBody->transport.SATA.smSenseData);
  satNewIOContext->smRequestBody = satIntIo->satIntRequestBody; /* key fix */
  //  satNewIOContext->interruptContext = tiInterruptContext;
  satNewIOContext->satIntIoContext  = satIntIo;

  satNewIOContext->psmDeviceHandle = smDeviceHandle;   
  satNewIOContext->satOrgIOContext = satIOContext; /* changed */

  /* this is valid only for TD layer generated (not triggered by OS at all) IO */
  satNewIOContext->smScsiXchg = &(satIntIo->satIntSmScsiXchg);


  SM_DBG6(("smsatIDSubStart: SM satIOContext %p \n", satIOContext));
  SM_DBG6(("smsatIDSubStart: SM satNewIOContext %p \n", satNewIOContext));
  SM_DBG6(("smsatIDSubStart: SM tiScsiXchg %p \n", satIOContext->smScsiXchg));
  SM_DBG6(("smsatIDSubStart: SM tiScsiXchg %p \n", satNewIOContext->smScsiXchg));



  SM_DBG3(("smsatIDSubStart: satNewIOContext %p smIORequestBody %p\n", satNewIOContext, smIORequestBody));

  status = smsatIDStart(smRoot,
                        &satIntIo->satIntSmIORequest, /* New smIORequest */
                        smDeviceHandle,
                        satNewIOContext->smScsiXchg, /* New smScsiInitiatorRequest_t *smScsiRequest, */
                        satNewIOContext);

  if (status != SM_RC_SUCCESS)
  {
    SM_DBG1(("smsatIDSubStart: failed in sending %d!!!\n", status));

    smsatFreeIntIoResource( smRoot,
                            satDevData,
                            satIntIo);

    return SM_RC_FAILURE;
  }


  SM_DBG2(("smsatIDSubStart: end\n"));

  return status;

}


osGLOBAL bit32
smsatIDStart(
              smRoot_t                  *smRoot,
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smSCSIRequest,
              smSatIOContext_t            *satIOContext
             )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
#ifdef SM_INTERNAL_DEBUG
  smIORequestBody_t         *smIORequestBody;
  smSatInternalIo_t         *satIntIoContext;
#endif

  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;
  SM_DBG2(("smsatIDStart: start\n"));
#ifdef SM_INTERNAL_DEBUG
  satIntIoContext = satIOContext->satIntIoContext;
  smIORequestBody = satIntIoContext->satIntRequestBody;
#endif
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  if (pSatDevData->satDeviceType == SATA_ATAPI_DEVICE)
  {
    SM_DBG2(("smsatIDStart: IDENTIFY_PACKET_DEVICE\n"));
    fis->h.command    = SAT_IDENTIFY_PACKET_DEVICE;  /* 0x40 */
  }
  else
  {
    SM_DBG2(("smsatIDStart: IDENTIFY_DEVICE\n"));
    fis->h.command    = SAT_IDENTIFY_DEVICE;    /* 0xEC */
  }
  fis->h.features       = 0;                      /* FIS reserve */
  fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
  fis->d.device         = 0;                      /* FIS LBA mode  */
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatIDStartCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

#ifdef SM_INTERNAL_DEBUG
  smhexdump("smsatIDStart", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
  smhexdump("smsatIDStart LL", (bit8 *)&(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev), sizeof(agsaFisRegHostToDevice_t));
#endif
  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smSCSIRequest,
                            satIOContext);

  SM_DBG2(("smsatIDStart: end status %d\n", status));

  return status;
}


osGLOBAL FORCEINLINE bit32
smsatIOStart(
              smRoot_t                  *smRoot,
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smSCSIRequest,
              smSatIOContext_t            *satIOContext
             )
{
  smDeviceData_t            *pSatDevData = satIOContext->pSatDevData;
  smScsiRspSense_t          *pSense      = satIOContext->pSense;
  smIniScsiCmnd_t           *scsiCmnd    = &smSCSIRequest->scsiCmnd;
  smLUN_t                   *pLun        = &scsiCmnd->lun;
  smSatInternalIo_t         *pSatIntIo   = agNULL;
  bit32                     status       = SM_RC_FAILURE;

  SM_DBG2(("smsatIOStart: start\n"));

  /*
   * Reject all other LUN other than LUN 0.
   */
  if ( ((pLun->lun[0] | pLun->lun[1] | pLun->lun[2] | pLun->lun[3] |
         pLun->lun[4] | pLun->lun[5] | pLun->lun[6] | pLun->lun[7] ) != 0) &&
        (scsiCmnd->cdb[0] != SCSIOPC_INQUIRY)
     )
  {
    SM_DBG1(("smsatIOStart: *** REJECT *** LUN not zero, cdb[0]=0x%x did %d !!!\n",
                 scsiCmnd->cdb[0], pSatDevData->id));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_NOT_SUPPORTED,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    return SM_RC_SUCCESS;
  }

  SM_DBG2(("smsatIOStart: satPendingIO %d satNCQMaxIO %d\n",pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));

  /* this may happen after tiCOMReset until OS sends inquiry */
  if (pSatDevData->IDDeviceValid == agFALSE && (scsiCmnd->cdb[0] != SCSIOPC_INQUIRY))
  {
    SM_DBG1(("smsatIOStart: invalid identify device data did %d !!!\n", pSatDevData->id));
    SM_DBG1(("smsatIOStart: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    SM_DBG1(("smsatIOStart: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));

    /*smEnqueueIO(smRoot, satIOContext);*/

    return SM_RC_NODEVICE;
  }

  /*
   * Check if we need to return BUSY, i.e. recovery in progress
   */
  if (pSatDevData->satDriveState == SAT_DEV_STATE_IN_RECOVERY)
  {
    SM_DBG1(("smsatIOStart: IN RECOVERY STATE cdb[0]=0x%x did=%d !!!\n",
                 scsiCmnd->cdb[0], pSatDevData->id));
    SM_DBG2(("smsatIOStart: device %p satPendingIO %d satNCQMaxIO %d\n", pSatDevData, pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    SM_DBG2(("smsatIOStart: device %p satPendingNCQIO %d satPendingNONNCQIO %d\n",pSatDevData, pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));

    /*smEnqueueIO(smRoot, satIOContext);*/

//    return  SM_RC_FAILURE;
    return SM_RC_DEVICE_BUSY;
  }

  if (pSatDevData->satDeviceType == SATA_ATAPI_DEVICE)
  {
     if (scsiCmnd->cdb[0] == SCSIOPC_REPORT_LUN)
     {
        return smsatReportLun(smRoot, smIORequest, smDeviceHandle, smSCSIRequest, satIOContext);
     }
     else
     {
        return smsatPacket(smRoot, smIORequest, smDeviceHandle, smSCSIRequest, satIOContext);
     }
  }
  else
  {
     /* Parse CDB */
     switch(scsiCmnd->cdb[0])
     {
       case SCSIOPC_READ_10:
         status = smsatRead10( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smSCSIRequest,
                              satIOContext);
         break;

       case SCSIOPC_WRITE_10:
         status = smsatWrite10( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smSCSIRequest,
                                satIOContext);
         break;

       case SCSIOPC_READ_6:
         status = smsatRead6( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smSCSIRequest,
                              satIOContext);
         break;

       case SCSIOPC_READ_12:
         SM_DBG5(("smsatIOStart: SCSIOPC_READ_12\n"));
         status = smsatRead12( smRoot,
                               smIORequest,
                               smDeviceHandle,
                               smSCSIRequest,
                               satIOContext);
         break;

       case SCSIOPC_READ_16:
         status = smsatRead16( smRoot,
                               smIORequest,
                               smDeviceHandle,
                               smSCSIRequest,
                               satIOContext);
         break;

       case SCSIOPC_WRITE_6:
         status = smsatWrite6( smRoot,
                               smIORequest,
                               smDeviceHandle,
                               smSCSIRequest,
                               satIOContext);
         break;

       case SCSIOPC_WRITE_12:
         SM_DBG5(("smsatIOStart: SCSIOPC_WRITE_12 \n"));
         status = smsatWrite12( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smSCSIRequest,
                                satIOContext);
         break;

       case SCSIOPC_WRITE_16:
         SM_DBG5(("smsatIOStart: SCSIOPC_WRITE_16 \n"));
         status = smsatWrite16( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smSCSIRequest,
                                satIOContext);
         break;

       case SCSIOPC_VERIFY_10:
         status = smsatVerify10( smRoot,
                                 smIORequest,
                                 smDeviceHandle,
                                 smSCSIRequest,
                                 satIOContext);
         break;

       case SCSIOPC_VERIFY_12:
         SM_DBG5(("smsatIOStart: SCSIOPC_VERIFY_12\n"));
         status = smsatVerify12( smRoot,
                                 smIORequest,
                                 smDeviceHandle,
                                 smSCSIRequest,
                                 satIOContext);
         break;

       case SCSIOPC_VERIFY_16:
         SM_DBG5(("smsatIOStart: SCSIOPC_VERIFY_16\n"));
         status = smsatVerify16( smRoot,
                                 smIORequest,
                                 smDeviceHandle,
                                 smSCSIRequest,
                                 satIOContext);
         break;

       case SCSIOPC_TEST_UNIT_READY:
         status = smsatTestUnitReady( smRoot,
                                      smIORequest,
                                      smDeviceHandle,
                                      smSCSIRequest,
                                      satIOContext);
         break;

       case SCSIOPC_INQUIRY:
         status = smsatInquiry( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smSCSIRequest,
                                satIOContext);
         break;

       case SCSIOPC_REQUEST_SENSE:
         status = smsatRequestSense( smRoot,
                                     smIORequest,
                                     smDeviceHandle,
                                     smSCSIRequest,
                                     satIOContext);
         break;

       case SCSIOPC_MODE_SENSE_6:
         status = smsatModeSense6( smRoot,
                                   smIORequest,
                                   smDeviceHandle,
                                   smSCSIRequest,
                                   satIOContext);
         break;

       case SCSIOPC_MODE_SENSE_10: 
         status = smsatModeSense10( smRoot,
                                    smIORequest,
                                    smDeviceHandle,
                                    smSCSIRequest,
                                    satIOContext);
         break;

       case SCSIOPC_READ_CAPACITY_10:
         status = smsatReadCapacity10( smRoot,
                                       smIORequest,
                                       smDeviceHandle,
                                       smSCSIRequest,
                                       satIOContext);
         break;

       case SCSIOPC_READ_CAPACITY_16:
         status = smsatReadCapacity16( smRoot,
                                       smIORequest,
                                       smDeviceHandle,
                                       smSCSIRequest,
                                       satIOContext);
         break;


       case SCSIOPC_REPORT_LUN:
         status = smsatReportLun( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smSCSIRequest,
                                  satIOContext);
         break;

       case SCSIOPC_FORMAT_UNIT: 
         SM_DBG5(("smsatIOStart: SCSIOPC_FORMAT_UNIT\n"));
         status = smsatFormatUnit( smRoot,
                                   smIORequest,
                                   smDeviceHandle,
                                   smSCSIRequest,
                                   satIOContext);
         break;

       case SCSIOPC_SEND_DIAGNOSTIC: 
         SM_DBG5(("smsatIOStart: SCSIOPC_SEND_DIAGNOSTIC\n"));
         status = smsatSendDiagnostic( smRoot,
                                       smIORequest,
                                       smDeviceHandle,
                                       smSCSIRequest,
                                       satIOContext);
         break;

       case SCSIOPC_START_STOP_UNIT:
         SM_DBG5(("smsatIOStart: SCSIOPC_START_STOP_UNIT\n"));
         status = smsatStartStopUnit( smRoot,
                                      smIORequest,
                                      smDeviceHandle,
                                      smSCSIRequest,
                                      satIOContext);
         break;

       case SCSIOPC_WRITE_SAME_10: 
         SM_DBG5(("smsatIOStart: SCSIOPC_WRITE_SAME_10\n"));
         status = smsatWriteSame10( smRoot,
                                    smIORequest,
                                    smDeviceHandle,
                                    smSCSIRequest,
                                    satIOContext);
         break;

       case SCSIOPC_WRITE_SAME_16: /* no support due to transfer length(sector count) */
         SM_DBG5(("smsatIOStart: SCSIOPC_WRITE_SAME_16\n"));
         status = smsatWriteSame16( smRoot,
                                    smIORequest,
                                    smDeviceHandle,
                                    smSCSIRequest,
                                    satIOContext);
         break;

       case SCSIOPC_LOG_SENSE: 
         SM_DBG5(("smsatIOStart: SCSIOPC_LOG_SENSE\n"));
         status = smsatLogSense( smRoot,
                                 smIORequest,
                                 smDeviceHandle,
                                 smSCSIRequest,
                                 satIOContext);
         break;

       case SCSIOPC_MODE_SELECT_6: 
         SM_DBG5(("smsatIOStart: SCSIOPC_MODE_SELECT_6\n"));
         status = smsatModeSelect6( smRoot,
                                    smIORequest,
                                    smDeviceHandle,
                                    smSCSIRequest,
                                    satIOContext);
         break;

       case SCSIOPC_MODE_SELECT_10: 
         SM_DBG5(("smsatIOStart: SCSIOPC_MODE_SELECT_10\n"));
         status = smsatModeSelect10( smRoot,
                                     smIORequest,
                                     smDeviceHandle,
                                     smSCSIRequest,
                                     satIOContext);
         break;

       case SCSIOPC_SYNCHRONIZE_CACHE_10: /* on error what to return, sharing CB with
                                           satSynchronizeCache16 */
         SM_DBG5(("smsatIOStart: SCSIOPC_SYNCHRONIZE_CACHE_10\n"));
         status = smsatSynchronizeCache10( smRoot,
                                           smIORequest,
                                           smDeviceHandle,
                                           smSCSIRequest,
                                           satIOContext);
         break;

       case SCSIOPC_SYNCHRONIZE_CACHE_16:/* on error what to return, sharing CB with
                                            satSynchronizeCache16 */

         SM_DBG5(("smsatIOStart: SCSIOPC_SYNCHRONIZE_CACHE_16\n"));
         status = smsatSynchronizeCache16( smRoot,
                                           smIORequest,
                                           smDeviceHandle,
                                           smSCSIRequest,
                                           satIOContext);
         break;

       case SCSIOPC_WRITE_AND_VERIFY_10: /* single write and multiple writes */
         SM_DBG5(("smsatIOStart: SCSIOPC_WRITE_AND_VERIFY_10\n"));
         status = smsatWriteAndVerify10( smRoot,
                                         smIORequest,
                                         smDeviceHandle,
                                         smSCSIRequest,
                                         satIOContext);
         break;

       case SCSIOPC_WRITE_AND_VERIFY_12:
         SM_DBG5(("smsatIOStart: SCSIOPC_WRITE_AND_VERIFY_12\n"));
         status = smsatWriteAndVerify12( smRoot,
                                         smIORequest,
                                         smDeviceHandle,
                                         smSCSIRequest,
                                         satIOContext);
         break;

       case SCSIOPC_WRITE_AND_VERIFY_16:
         SM_DBG5(("smsatIOStart: SCSIOPC_WRITE_AND_VERIFY_16\n"));
         status = smsatWriteAndVerify16( smRoot,
                                         smIORequest,
                                         smDeviceHandle,
                                         smSCSIRequest,
                                         satIOContext);

         break;

       case SCSIOPC_READ_MEDIA_SERIAL_NUMBER:
         SM_DBG5(("smsatIOStart: SCSIOPC_READ_MEDIA_SERIAL_NUMBER\n"));
         status = smsatReadMediaSerialNumber( smRoot,
                                              smIORequest,
                                              smDeviceHandle,
                                              smSCSIRequest,
                                              satIOContext);

         break;

       case SCSIOPC_READ_BUFFER:
         SM_DBG5(("smsatIOStart: SCSIOPC_READ_BUFFER\n"));
         status = smsatReadBuffer( smRoot,
                                   smIORequest,
                                   smDeviceHandle,
                                   smSCSIRequest,
                                   satIOContext);

         break;

       case SCSIOPC_WRITE_BUFFER:
         SM_DBG5(("smsatIOStart: SCSIOPC_WRITE_BUFFER\n"));
         status = smsatWriteBuffer( smRoot,
                                    smIORequest,
                                    smDeviceHandle,
                                    smSCSIRequest,
                                    satIOContext);

         break;

       case SCSIOPC_REASSIGN_BLOCKS:
         SM_DBG5(("smsatIOStart: SCSIOPC_REASSIGN_BLOCKS\n"));
         status = smsatReassignBlocks( smRoot,
                                       smIORequest,
                                       smDeviceHandle,
                                       smSCSIRequest,
                                       satIOContext);

         break;
       
       case SCSIOPC_ATA_PASS_THROUGH12: /* fall through */
       case SCSIOPC_ATA_PASS_THROUGH16:
         SM_DBG5(("smsatIOStart: SCSIOPC_ATA_PASS_THROUGH\n"));
         status = smsatPassthrough( smRoot, 
                                    smIORequest,
                                    smDeviceHandle,
                                    smSCSIRequest,
                                    satIOContext);
         break;

       default:
         /* Not implemented SCSI cmd, set up error response */
         SM_DBG1(("smsatIOStart: unsupported SCSI cdb[0]=0x%x did=%d !!!\n",
                    scsiCmnd->cdb[0], pSatDevData->id));

         smsatSetSensePayload( pSense,
                               SCSI_SNSKEY_ILLEGAL_REQUEST,
                               0,
                               SCSI_SNSCODE_INVALID_COMMAND,
                               satIOContext);

         /*smEnqueueIO(smRoot, satIOContext);*/

         tdsmIOCompletedCB( smRoot,
                            smIORequest,
                            smIOSuccess,
                            SCSI_STAT_CHECK_CONDITION,
                            satIOContext->pSmSenseData,
                            satIOContext->interruptContext );
         status = SM_RC_SUCCESS;

         break;

     }  /* end switch  */
  }

  if (status == SM_RC_BUSY || status == SM_RC_DEVICE_BUSY)
  {
    SM_DBG1(("smsatIOStart: BUSY did %d!!!\n", pSatDevData->id));
    SM_DBG2(("smsatIOStart: LL is busy or target queue is full\n"));
    SM_DBG2(("smsatIOStart: device %p satPendingIO %d satNCQMaxIO %d\n",pSatDevData, pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    SM_DBG2(("smsatIOStart: device %p satPendingNCQIO %d satPendingNONNCQIO %d\n",pSatDevData, pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
    pSatIntIo               = satIOContext->satIntIoContext;

    /*smEnqueueIO(smRoot, satIOContext);*/

    /* interal structure free */
    smsatFreeIntIoResource( smRoot,
                            pSatDevData,
                            pSatIntIo);
  }

  return status;
}

osGLOBAL void
smsatSetSensePayload(
                     smScsiRspSense_t   *pSense,
                     bit8               SnsKey,
                     bit32              SnsInfo,
                     bit16              SnsCode,
                     smSatIOContext_t     *satIOContext)
{
  /* for fixed format sense data, SPC-4, p37 */
  bit32      i;
  bit32      senseLength;
  bit8       tmp = 0;

  SM_DBG2(("smsatSetSensePayload: start\n"));

  senseLength  = sizeof(smScsiRspSense_t);

  /* zero out the data area */
  for (i=0;i< senseLength;i++)
  {
    ((bit8*)pSense)[i] = 0;
  }

  /*
   * SCSI Sense Data part of response data
   */
  pSense->snsRespCode  = 0x70;    /*  0xC0 == vendor specific */
                                      /*  0x70 == standard current error */
  pSense->senseKey     = SnsKey;
  /*
   * Put sense info in scsi order format
   */
  pSense->info[0]      = (bit8)((SnsInfo >> 24) & 0xff);
  pSense->info[1]      = (bit8)((SnsInfo >> 16) & 0xff);
  pSense->info[2]      = (bit8)((SnsInfo >> 8) & 0xff);
  pSense->info[3]      = (bit8)((SnsInfo) & 0xff);
  pSense->addSenseLen  = 11;          /* fixed size of sense data = 18 */
  pSense->addSenseCode = (bit8)((SnsCode >> 8) & 0xFF);
  pSense->senseQual    = (bit8)(SnsCode & 0xFF);
  /*
   * Set pointer in scsi status
   */
  switch(SnsKey)
  {
    /*
     * set illegal request sense key specific error in cdb, no bit pointer
     */
    case SCSI_SNSKEY_ILLEGAL_REQUEST:
      pSense->skeySpecific[0] = 0xC8;
      break;

    default:
      break;
  }
  /* setting sense data length */
  if (satIOContext != agNULL)
  {
    satIOContext->pSmSenseData->senseLen = 18;
  }
  else
  {
    SM_DBG1(("smsatSetSensePayload: satIOContext is NULL!!!\n"));
  }
  
  /* Only for SCSI_SNSCODE_ATA_PASS_THROUGH_INFORMATION_AVAILABLE */
  if (SnsCode == SCSI_SNSCODE_ATA_PASS_THROUGH_INFORMATION_AVAILABLE)
  {
    /* filling in COMMAND-SPECIFIC INFORMATION */
    tmp = satIOContext->extend << 7 | satIOContext->Sector_Cnt_Upper_Nonzero << 6 | satIOContext->LBA_Upper_Nonzero << 5;        
    SM_DBG3(("smsatSetSensePayload: extend 0x%x Sector_Cnt_Upper_Nonzero 0x%x LBA_Upper_Nonzero 0x%x\n",
    satIOContext->extend, satIOContext->Sector_Cnt_Upper_Nonzero, satIOContext->LBA_Upper_Nonzero));
    SM_DBG3(("smsatSetSensePayload: tmp 0x%x\n", tmp));
    pSense->cmdSpecific[0]      = tmp;
    pSense->cmdSpecific[1]      = satIOContext->LBAHigh07;
    pSense->cmdSpecific[2]      = satIOContext->LBAMid07;
    pSense->cmdSpecific[3]      = satIOContext->LBALow07;
//    smhexdump("smsatSetSensePayload: cmdSpecific",(bit8 *)pSense->cmdSpecific, 4);
//    smhexdump("smsatSetSensePayload: info",(bit8 *)pSense->info, 4);
    
  }
  return;
}

/*****************************************************************************
*! \brief  smsatDecodeSATADeviceType
*
*   This routine decodes ATA signature
*
*  \param   pSignature:       ATA signature
*
*
*  \return:
*          TRUE if ATA signature
*          FALSE otherwise
*
*****************************************************************************/
/*
  ATA p65
  PM p65
  SATAII p79, p80
 */
GLOBAL bit32
smsatDecodeSATADeviceType(
                         bit8  *pSignature
                         )
{
  bit32 deviceType = UNKNOWN_DEVICE;

  if ( (pSignature)[0] == 0x01 && (pSignature)[1] == 0x01
       && (pSignature)[2] == 0x00 && (pSignature)[3] == 0x00
       && (pSignature)[4] == 0xA0 )    /* this is the signature of a Hitachi SATA HDD*/
  {
    deviceType = SATA_ATA_DEVICE;
  }
  else if ( (pSignature)[0] == 0x01 && (pSignature)[1] == 0x01
      && (pSignature)[2] == 0x00 && (pSignature)[3] == 0x00
      && (pSignature)[4] == 0x00 )
  {
    deviceType = SATA_ATA_DEVICE;
  }
  else if ( (pSignature)[0] == 0x01 && (pSignature)[1] == 0x01
          && (pSignature)[2] == 0x14 && (pSignature)[3] == 0xEB
          && ( (pSignature)[4] == 0x00 || (pSignature)[4] == 0x10) )
  {
    deviceType = SATA_ATAPI_DEVICE;
  }
  else if ( (pSignature)[0] == 0x01 && (pSignature)[1] == 0x01
          && (pSignature)[2] == 0x69 && (pSignature)[3] == 0x96
          && (pSignature)[4] == 0x00 )
  {
    deviceType = SATA_PM_DEVICE;
  }
  else if ( (pSignature)[0] == 0x01 && (pSignature)[1] == 0x01
          && (pSignature)[2] == 0x3C && (pSignature)[3] == 0xC3
          && (pSignature)[4] == 0x00 )
  {
    deviceType = SATA_SEMB_DEVICE;
  }
  else if ( (pSignature)[0] == 0xFF && (pSignature)[1] == 0xFF
          && (pSignature)[2] == 0xFF && (pSignature)[3] == 0xFF
          && (pSignature)[4] == 0xFF )
  {
    deviceType = SATA_SEMB_WO_SEP_DEVICE;
  }

  return deviceType;
}


/*****************************************************************************/
/*! \brief SAT implementation for ATAPI Packet Command.
 *
 *  SAT implementation for ATAPI Packet and send FIS request to LL layer.
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   smSatIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e smIOSuccess:     I/O request successfully initiated.
 *    - \e smIOBusy:        No resources available, try again later.
 *    - \e smIONoDevice:  Invalid device handle.
 *    - \e smIOError:       Other errors.
 */
/*****************************************************************************/
osGLOBAL bit32
smsatPacket(
          smRoot_t                  *smRoot,
          smIORequest_t             *smIORequest,
          smDeviceHandle_t          *smDeviceHandle,
          smScsiInitiatorRequest_t  *smScsiRequest,
          smSatIOContext_t            *satIOContext
  )
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_D2H_PKT;
  smDeviceData_t            *pSatDevData;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG3(("smsatPacket: start, SCSI CDB is 0x%X %X %X %X %X %X %X %X %X %X %X %X\n",
           scsiCmnd->cdb[0],scsiCmnd->cdb[1],scsiCmnd->cdb[2],scsiCmnd->cdb[3],
           scsiCmnd->cdb[4],scsiCmnd->cdb[5],scsiCmnd->cdb[6],scsiCmnd->cdb[7],
           scsiCmnd->cdb[8],scsiCmnd->cdb[9],scsiCmnd->cdb[10],scsiCmnd->cdb[11]));

  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set 1*/
  fis->h.command        = SAT_PACKET;             /* 0xA0 */
  if (pSatDevData->satDMADIRSupport)              /* DMADIR enabled*/
  {
     fis->h.features    = (smScsiRequest->dataDirection == smDirectionIn)? 0x04 : 0; /* 1 for D2H, 0 for H2D */
  }
  else
  {
     fis->h.features    = 0;                      /* FIS reserve */
  }

  if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
  {
     /*DMA transfer mode*/
     fis->h.features |= 0x01;
  }
  else
  {
     /*PIO transfer mode*/
     fis->h.features |= 0x0;
  }
  /* Byte count low and byte count high */
  if ( scsiCmnd->expDataLength > 0xFFFF )
  {
     fis->d.lbaMid = 0xFF;                                 /* FIS LBA (15:8 ) */
     fis->d.lbaHigh = 0xFF;                                /* FIS LBA (23:16) */
  }
  else
  {
     fis->d.lbaMid = (bit8)scsiCmnd->expDataLength;        /* FIS LBA (15:8 ) */
     fis->d.lbaHigh = (bit8)(scsiCmnd->expDataLength>>8);  /* FIS LBA (23:16) */
  }

  fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
  fis->d.device         = 0;                      /* FIS LBA (27:24) and FIS LBA mode  */
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  satIOContext->ATACmd = SAT_PACKET;

  if (smScsiRequest->dataDirection == smDirectionIn)
  {
      agRequestType = AGSA_SATA_PROTOCOL_D2H_PKT;
  }
  else
  {
      agRequestType = AGSA_SATA_PROTOCOL_H2D_PKT;
  }

  satIOContext->satCompleteCB = &smsatPacketCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart(smRoot,
                          smIORequest,
                          smDeviceHandle,
                          smScsiRequest,
                          satIOContext);

  SM_DBG3(("smsatPacket: return\n"));
  return (status);
}

/*****************************************************************************/
/*! \brief SAT implementation for smsatSetFeaturePIO.
 *
 *  This function creates Set Features fis and sends the request to LL layer
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   smSatIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e smIOSuccess:     I/O request successfully initiated.
 *    - \e smIOBusy:        No resources available, try again later.
 *    - \e smIONoDevice:  Invalid device handle.
 *    - \e smIOError:       Other errors.
 */
/*****************************************************************************/
osGLOBAL bit32
smsatSetFeaturesPIO(
  smRoot_t                  *smRoot,
  smIORequest_t             *smIORequest,
  smDeviceHandle_t          *smDeviceHandle,
  smScsiInitiatorRequest_t  *smScsiRequest,
  smSatIOContext_t          *satIOContext
  )
{
  bit32                     status = SM_RC_FAILURE;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t *fis;

  fis           = satIOContext->pFis;
  SM_DBG2(("smsatSetFeaturesPIO: start\n"));
  /*
   * Send the Set Features command.
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
  fis->h.features       = 0x03;                   /* set transfer mode */
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  fis->d.sectorCount = 0x0C;                     /*enable PIO transfer mode */
  satIOContext->satCompleteCB = &smsatSetFeaturesPIOCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                          smIORequest,
                          smDeviceHandle,
                          smScsiRequest,
                          satIOContext);

  SM_DBG2(("smsatSetFeaturesPIO: return\n"));
  /* debugging code */
  if (smIORequest->tdData == smIORequest->smData)
  {
    SM_DBG1(("smsatSetFeaturesPIO: incorrect smIORequest\n"));
  }

  return status;
}
/*****************************************************************************/
/*! \brief SAT implementation for SCSI REQUEST SENSE to ATAPI device.
 *
 *  SAT implementation for SCSI REQUEST SENSE.
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   smSatIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e smIOSuccess:     I/O request successfully initiated.
 *    - \e smIOBusy:        No resources available, try again later.
 *    - \e smIONoDevice:  Invalid device handle.
 *    - \e smIOError:       Other errors.
 */
/*****************************************************************************/
osGLOBAL bit32
smsatRequestSenseForATAPI(
  smRoot_t                  *smRoot,
  smIORequest_t             *smIORequest,
  smDeviceHandle_t          *smDeviceHandle,
  smScsiInitiatorRequest_t  *smScsiRequest,
  smSatIOContext_t            *satIOContext
  )
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_D2H_PKT;
  smDeviceData_t            *pSatDevData;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  scsiCmnd->cdb[0]   = SCSIOPC_REQUEST_SENSE;
  scsiCmnd->cdb[1]   = 0;
  scsiCmnd->cdb[2]   = 0;
  scsiCmnd->cdb[3]   = 0;
  scsiCmnd->cdb[4]   = (bit8)scsiCmnd->expDataLength;
  scsiCmnd->cdb[5]   = 0;
  SM_DBG3(("smsatRequestSenseForATAPI: start, SCSI CDB is 0x%X %X %X %X %X %X %X %X %X %X %X %X\n",
           scsiCmnd->cdb[0],scsiCmnd->cdb[1],scsiCmnd->cdb[2],scsiCmnd->cdb[3],
           scsiCmnd->cdb[4],scsiCmnd->cdb[5],scsiCmnd->cdb[6],scsiCmnd->cdb[7],
           scsiCmnd->cdb[8],scsiCmnd->cdb[9],scsiCmnd->cdb[10],scsiCmnd->cdb[11]));

  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set 1*/
  fis->h.command        = SAT_PACKET;             /* 0xA0 */
  if (pSatDevData->satDMADIRSupport)              /* DMADIR enabled*/
  {
     fis->h.features    = (smScsiRequest->dataDirection == smDirectionIn)? 0x04 : 0; /* 1 for D2H, 0 for H2D */
  }
  else
  {
     fis->h.features    = 0;                      /* FIS reserve */
  }

  if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
  {
     fis->h.features |= 0x01;
  }
  else
  {
     fis->h.features |= 0x0;
  }

  fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = (bit8)scsiCmnd->expDataLength;        /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = (bit8)(scsiCmnd->expDataLength>>8);  /* FIS LBA (23:16) */
  fis->d.device         = 0;                      /* FIS LBA (27:24) and FIS LBA mode  */
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  satIOContext->ATACmd = SAT_PACKET;

  agRequestType = AGSA_SATA_PROTOCOL_D2H_PKT;


  satIOContext->satCompleteCB = &smsatRequestSenseForATAPICB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                          smIORequest,
                          smDeviceHandle,
                          smScsiRequest,
                          satIOContext);

  SM_DBG3(("smsatRequestSenseForATAPI: return\n"));
  return (status);
}
/*****************************************************************************/
/*! \brief SAT implementation for smsatDeviceReset.
 *
 *  This function creates DEVICE RESET fis and sends the request to LL layer
 *
 *  \param   smRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   smIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   smDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   smScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   smSatIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e smIOSuccess:     I/O request successfully initiated.
 *    - \e smIOBusy:        No resources available, try again later.
 *    - \e smIONoDevice:  Invalid device handle.
 *    - \e smIOError:       Other errors.
 */
/*****************************************************************************/
osGLOBAL bit32
smsatDeviceReset(
  smRoot_t                  *smRoot,
  smIORequest_t             *smIORequest,
  smDeviceHandle_t          *smDeviceHandle,
  smScsiInitiatorRequest_t  *smScsiRequest,
  smSatIOContext_t            *satIOContext
  )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t *fis;

  fis           = satIOContext->pFis;
  SM_DBG3(("smsatDeviceReset: start\n"));
  /*
   * Send the  Execute Device Diagnostic command.
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_DEVICE_RESET;       /* 0x08 */
  fis->h.features       = 0;
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_DEV_RESET;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatDeviceResetCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                          smIORequest,
                          smDeviceHandle,
                          smScsiRequest,
                          satIOContext);

  SM_DBG3(("smsatDeviceReset: return\n"));

  return status;
}


/*****************************************************************************/
/*! \brief SAT implementation for smsatExecuteDeviceDiagnostic.
 *
 *  This function creates Execute Device Diagnostic fis and sends the request to LL layer
 *
 *  \param   smRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   smIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   smDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   smScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   smSatIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e smIOSuccess:     I/O request successfully initiated.
 *    - \e smIOBusy:        No resources available, try again later.
 *    - \e smIONoDevice:  Invalid device handle.
 *    - \e smIOError:       Other errors.
 */
/*****************************************************************************/
osGLOBAL bit32
smsatExecuteDeviceDiagnostic(
  smRoot_t                  *smRoot,
  smIORequest_t             *smIORequest,
  smDeviceHandle_t          *smDeviceHandle,
  smScsiInitiatorRequest_t  *smScsiRequest,
  smSatIOContext_t            *satIOContext
  )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t *fis;

  fis           = satIOContext->pFis;
  SM_DBG3(("smsatExecuteDeviceDiagnostic: start\n"));
  /*
   * Send the  Execute Device Diagnostic command.
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_EXECUTE_DEVICE_DIAGNOSTIC;   /* 0x90 */
  fis->h.features       = 0;
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatExecuteDeviceDiagnosticCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                          smIORequest,
                          smDeviceHandle,
                          smScsiRequest,
                          satIOContext);

  SM_DBG3(("smsatExecuteDeviceDiagnostic: return\n"));

  return status;
}


osGLOBAL void
smsatSetDeferredSensePayload(
                             smScsiRspSense_t *pSense,
                             bit8             SnsKey,
                             bit32            SnsInfo,
                             bit16            SnsCode,
                             smSatIOContext_t   *satIOContext
                            )
{
  SM_DBG2(("smsatSetDeferredSensePayload: start\n"));
  return;
}


GLOBAL bit32
smsatRead6(
           smRoot_t                  *smRoot,
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t            *satIOContext
    )
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit16                     tl = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG2(("smsatRead6: start\n"));

  /* no FUA checking since read6 */


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatRead6: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* cbd6; computing LBA and transfer length */
  lba = (((scsiCmnd->cdb[1]) & 0x1f) << (8*2))
    + (scsiCmnd->cdb[2] << 8) + scsiCmnd->cdb[3];
  tl = scsiCmnd->cdb[4];

  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
   */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    if (lba > SAT_TR_LBA_LIMIT - 1)
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    SM_DBG1(("smsatRead6: return LBA out of range!!!\n"));
    return SM_RC_SUCCESS;
    }
  }

  /* case 1 and 2 */
  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* READ DMA*/
      SM_DBG5(("smsatRead6: case 2\n"));


      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_DMA;           /* 0xC8 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      if (tl == 0)
      {
        /* temporary fix */
        fis->d.sectorCount    = 0xff;                   /* FIS sector count (7:0) */
      }
      else
      {
        fis->d.sectorCount    = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
      }
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
    }
    else
    {
      /* case 1 */
      /* READ SECTORS for easier implemetation */
      SM_DBG5(("smsatRead6: case 1\n"));

      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_SECTORS;       /* 0x20 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      if (tl == 0)
      {
        /* temporary fix */
        fis->d.sectorCount    = 0xff;                   /* FIS sector count (7:0) */
      }
      else
      {
        fis->d.sectorCount    = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
      }
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;

    }
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* READ DMA EXT only */
      SM_DBG5(("smsatRead6: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_DMA_EXT;       /* 0x25 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      if (tl == 0)
      {
        /* sector count is 256, 0x100*/
        fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0x01;                      /* FIS sector count (15:8) */
      }
      else
      {
        fis->d.sectorCount    = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
      }
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
    }
    else
    {
      /* case 4 */
      /* READ SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatRead6: case 4\n"));

      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_SECTORS_EXT;   /* 0x24 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      if (tl == 0)
      {
        /* sector count is 256, 0x100*/
        fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0x01;                      /* FIS sector count (15:8) */
      }
      else
      {
        fis->d.sectorCount    = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
      }
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
    }
  }

  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* READ FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      /* sanity check */
      SM_DBG1(("smsatRead6: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    SM_DBG5(("smsatRead6: case 5\n"));

    /* Support 48-bit FPDMA addressing, use READ FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_FPDMA_QUEUED;  /* 0x60 */
    fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS FUA clear */
    fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    if (tl == 0)
    {
      /* sector count is 256, 0x100*/
      fis->h.features       = 0;                         /* FIS sector count (7:0) */
      fis->d.featuresExp    = 0x01;                      /* FIS sector count (15:8) */
    }
    else
    {
      fis->h.features       = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
      fis->d.featuresExp    = 0;                      /* FIS sector count (15:8) */
    }
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_READ;
  }

   /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatNonChainedDataIOCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);

}

osGLOBAL FORCEINLINE bit32
smsatRead10(
            smRoot_t                  *smRoot,
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
     )
{
  smDeviceData_t            *pSatDevData = satIOContext->pSatDevData;
  smScsiRspSense_t          *pSense      = satIOContext->pSense;
  smIniScsiCmnd_t           *scsiCmnd    = &smScsiRequest->scsiCmnd;
  agsaFisRegHostToDevice_t  *fis         = satIOContext->pFis;

  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  SM_DBG2(("smsatRead10: start\n"));
  SM_DBG2(("smsatRead10: pSatDevData did=%d\n", pSatDevData->id));
  //  smhexdump("smsatRead10", (bit8 *)scsiCmnd->cdb, 10);

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatRead10: return FUA_NV!!!\n"));
    return SM_RC_SUCCESS;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatRead10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }
  /*
  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));
  */
  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = 0;                  /* MSB */
  LBA[1] = 0;
  LBA[2] = 0;
  LBA[3] = 0;  
  LBA[4] = scsiCmnd->cdb[2];  
  LBA[5] = scsiCmnd->cdb[3];
  LBA[6] = scsiCmnd->cdb[4];
  LBA[7] = scsiCmnd->cdb[5];   /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = 0;   
  TL[3] = 0;  
  TL[4] = 0;
  TL[5] = 0;
  TL[6] = scsiCmnd->cdb[7];   
  TL[7] = scsiCmnd->cdb[8];    /* LSB */


  /* cbd10; computing LBA and transfer length */
  lba = (scsiCmnd->cdb[2] << 24) + (scsiCmnd->cdb[3] << 16)
        + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];


  SM_DBG5(("smsatRead10: lba %d functioned lba %d\n", lba, smsatComputeCDB10LBA(satIOContext)));
  SM_DBG5(("smsatRead10: lba 0x%x functioned lba 0x%x\n", lba, smsatComputeCDB10LBA(satIOContext)));
  SM_DBG5(("smsatRead10: tl %d functioned tl %d\n", tl, smsatComputeCDB10TL(satIOContext)));

  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
   */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */

  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatRead10: return LBA out of range, not EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatRead10: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }
    /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* READ FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      SM_DBG1(("smsatRead10: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }

    SM_DBG6(("smsatRead10: case 5\n"));

    /* Support 48-bit FPDMA addressing, use READ FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_FPDMA_QUEUED;  /* 0x60 */
    fis->h.features       = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_READ10_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_READ;
    satIOContext->ATACmd = SAT_READ_FPDMA_QUEUED;
  }
  else if (pSatDevData->sat48BitSupport == agTRUE) /* case 3 and 4 */
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* READ DMA EXT */
      SM_DBG5(("smsatRead10: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */

      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_DMA_EXT;       /* 0x25 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
      satIOContext->ATACmd = SAT_READ_DMA_EXT;

    }
    else
    {
      /* case 4 */
      /* READ MULTIPLE EXT or READ SECTOR(S) EXT or READ VERIFY SECTOR(S) EXT*/
      /* READ SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatRead10: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* Check FUA bit */
      if (scsiCmnd->cdb[1] & SCSI_READ10_FUA_MASK)
      {
        
        /* for now, no support for FUA */
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
        return SM_RC_SUCCESS;
      }

      fis->h.command        = SAT_READ_SECTORS_EXT;      /* 0x24 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
      satIOContext->ATACmd = SAT_READ_SECTORS_EXT;
    }
  }
  else/* case 1 and 2 */
  {
      if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
      {
        /* case 2 */
        /* READ DMA*/
        /* in case that we can't fit the transfer length, we need to make it fit by sending multiple ATA cmnds */
        SM_DBG5(("smsatRead10: case 2\n"));


        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
        fis->h.command        = SAT_READ_DMA;           /* 0xC8 */
        fis->h.features       = 0;                      /* FIS reserve */
        fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
        fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
        fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
        fis->d.device         =
          (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));        /* FIS LBA (27:24) and FIS LBA mode  */
        fis->d.lbaLowExp      = 0;
        fis->d.lbaMidExp      = 0;
        fis->d.lbaHighExp     = 0;
        fis->d.featuresExp    = 0;
        fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;


        agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
        satIOContext->ATACmd = SAT_READ_DMA;
      }
      else
      {
        /* case 1 */
        /* READ MULTIPLE or READ SECTOR(S) */
        /* READ SECTORS for easier implemetation */
        /* in case that we can't fit the transfer length, we need to make it fit by sending multiple ATA cmnds */
        SM_DBG5(("smsatRead10: case 1\n"));

        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
        fis->h.command        = SAT_READ_SECTORS;       /* 0x20 */
        fis->h.features       = 0;                      /* FIS reserve */
        fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
        fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
        fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
        fis->d.device         =
          (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));        /* FIS LBA (27:24) and FIS LBA mode  */
        fis->d.lbaLowExp      = 0;
        fis->d.lbaMidExp      = 0;
        fis->d.lbaHighExp     = 0;
        fis->d.featuresExp    = 0;
        fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;


        agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
        satIOContext->ATACmd = SAT_READ_SECTORS;
    }
  }
  //  smhexdump("satRead10 final fis", (bit8 *)fis, sizeof(agsaFisRegHostToDevice_t));

  /* saves the current LBA and orginal TL */
  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

 /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_READ_SECTORS || fis->h.command == SAT_READ_DMA)
  {
    LoopNum = smsatComputeLoopNum(tl, 0x100);
  }
  else
  {
     /* SAT_READ_FPDMA_QUEUED */
     /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
     LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;

  /* Initialize CB for SATA completion.
   */
  if (LoopNum == 1)
  {
    SM_DBG5(("smsatRead10: NON CHAINED data\n"));
    satIOContext->satCompleteCB = &smsatNonChainedDataIOCB;
  }
  else
  {
    SM_DBG2(("smsatRead10: CHAINED data!!!\n"));

    /* re-setting tl */
    if (fis->h.command == SAT_READ_SECTORS || fis->h.command == SAT_READ_DMA)
    {
      fis->d.sectorCount    = 0x0;
      smsatSplitSGL(smRoot,
                    smIORequest,
                    smDeviceHandle,
                    smScsiRequest,
                    satIOContext,
                    NON_BIT48_ADDRESS_TL_LIMIT*SATA_SECTOR_SIZE, /* 0x100 * 0x200 */
                    (satIOContext->OrgTL)*SATA_SECTOR_SIZE,
                    agTRUE);
    }
    else if (fis->h.command == SAT_READ_SECTORS_EXT || fis->h.command == SAT_READ_DMA_EXT)
    {
      /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
      smsatSplitSGL(smRoot,
                    smIORequest,
                    smDeviceHandle,
                    smScsiRequest,
                    satIOContext,
                    BIT48_ADDRESS_TL_LIMIT*SATA_SECTOR_SIZE, /* 0xFFFF * 0x200 */
                    (satIOContext->OrgTL)*SATA_SECTOR_SIZE,
                    agTRUE);
    }
    else
    {
      /* SAT_READ_FPDMA_QUEUED */
      fis->h.features       = 0xFF;
      fis->d.featuresExp    = 0xFF;
      smsatSplitSGL(smRoot,
                    smIORequest,
                    smDeviceHandle,
                    smScsiRequest,
                    satIOContext,
                    BIT48_ADDRESS_TL_LIMIT*SATA_SECTOR_SIZE, /* 0xFFFF * 0x200 */
                    (satIOContext->OrgTL)*SATA_SECTOR_SIZE,
                    agTRUE);
    }

    /* chained data */
    satIOContext->satCompleteCB = &smsatChainedDataIOCB;

  }

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatRead10: return\n"));
  return (status);

}

osGLOBAL bit32
smsatRead12(
            smRoot_t                  *smRoot,
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
     )
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatRead12: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatRead12: return FUA_NV!!!\n"));
    return SM_RC_SUCCESS;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatRead12: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = 0;                  /* MSB */
  LBA[1] = 0;
  LBA[2] = 0;
  LBA[3] = 0;
  LBA[4] = scsiCmnd->cdb[2];
  LBA[5] = scsiCmnd->cdb[3];
  LBA[6] = scsiCmnd->cdb[4];
  LBA[7] = scsiCmnd->cdb[5];   /* LSB */

  TL[0] = 0;                   /* MSB */
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;   
  TL[4] = scsiCmnd->cdb[6];   
  TL[5] = scsiCmnd->cdb[7];
  TL[6] = scsiCmnd->cdb[8];
  TL[7] = scsiCmnd->cdb[9];   	/* LSB */


  lba = smsatComputeCDB12LBA(satIOContext);
  tl = smsatComputeCDB12TL(satIOContext);

  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
   */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);    
    if (AllChk)
    {
      SM_DBG1(("smsatRead12: return LBA out of range, not EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);    
    if (AllChk)
    {
      SM_DBG1(("smsatRead12: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }

  /* case 1 and 2 */
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* READ DMA*/
      /* in case that we can't fit the transfer length,
         we need to make it fit by sending multiple ATA cmnds */
      SM_DBG5(("smsatRead12: case 2\n"));


      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_DMA;           /* 0xC8 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         =
        (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));        /* FIS LBA (27:24) and FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;


      agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
      satIOContext->ATACmd = SAT_READ_DMA;
    }
    else
    {
      /* case 1 */
      /* READ MULTIPLE or READ SECTOR(S) */
      /* READ SECTORS for easier implemetation */
      /* can't fit the transfer length but need to make it fit by sending multiple*/
      SM_DBG5(("smsatRead12: case 1\n"));

      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_SECTORS;       /* 0x20 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         =
        (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));        /* FIS LBA (27:24) and FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;


      agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
      satIOContext->ATACmd = SAT_READ_SECTORS;
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* READ DMA EXT */
      SM_DBG5(("smsatRead12: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */

      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_DMA_EXT;       /* 0x25 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
      satIOContext->ATACmd = SAT_READ_DMA_EXT;

    }
    else
    {
      /* case 4 */
      /* READ MULTIPLE EXT or READ SECTOR(S) EXT or READ VERIFY SECTOR(S) EXT*/
      /* READ SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatRead12: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* Check FUA bit */
      if (scsiCmnd->cdb[1] & SCSI_READ12_FUA_MASK)
      {
           
        /* for now, no support for FUA */
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
        return SM_RC_SUCCESS;
      }

      fis->h.command        = SAT_READ_SECTORS_EXT;      /* 0x24 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
      satIOContext->ATACmd = SAT_READ_SECTORS_EXT;
    }
  }

  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* READ FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      SM_DBG1(("smsatRead12: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }

    SM_DBG6(("smsatRead12: case 5\n"));

    /* Support 48-bit FPDMA addressing, use READ FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_FPDMA_QUEUED;  /* 0x60 */
    fis->h.features       = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_READ12_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_READ;
    satIOContext->ATACmd = SAT_READ_FPDMA_QUEUED;
  }

  /* saves the current LBA and orginal TL */
  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_READ_SECTORS || fis->h.command == SAT_READ_DMA)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_SECTORS_EXT || fis->h.command == SAT_READ_DMA_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_READ_FPDMA_QUEUEDK */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    SM_DBG5(("smsatRead12: NON CHAINED data\n"));
    satIOContext->satCompleteCB = &smsatNonChainedDataIOCB;
  }
  else
  {
    SM_DBG1(("smsatRead12: CHAINED data\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_READ_SECTORS || fis->h.command == SAT_READ_DMA)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_READ_SECTORS_EXT || fis->h.command == SAT_READ_DMA_EXT)
    {
      /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      /* SAT_READ_FPDMA_QUEUED */
      fis->h.features       = 0xFF;
      fis->d.featuresExp    = 0xFF;
    }

    /* chained data */
    satIOContext->satCompleteCB = &smsatChainedDataIOCB;
  }

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatRead12: return\n"));
  return (status);
}

osGLOBAL bit32
smsatRead16(
            smRoot_t                  *smRoot,
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
     )
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */
//  bit32                     limitExtChk = agFALSE; /* lba limit check for bit48 addressing check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatRead16: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatRead16: return FUA_NV!!!\n"));
    return SM_RC_SUCCESS;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatRead16: return control!!!\n"));
    return SM_RC_SUCCESS;
  }


  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));


  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];
  LBA[4] = scsiCmnd->cdb[6];
  LBA[5] = scsiCmnd->cdb[7];
  LBA[6] = scsiCmnd->cdb[8];
  LBA[7] = scsiCmnd->cdb[9];  /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;
  TL[4] = scsiCmnd->cdb[10];   /* MSB */
  TL[5] = scsiCmnd->cdb[11];
  TL[6] = scsiCmnd->cdb[12];
  TL[7] = scsiCmnd->cdb[13];   /* LSB */



 
 lba = smsatComputeCDB16LBA(satIOContext);
 tl = smsatComputeCDB16TL(satIOContext);


  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
   */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatRead16: return LBA out of range, not EXT!!!\n"));

      /*smEnqueueIO(smRoot, satIOContext);*/


      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      return SM_RC_SUCCESS;
    }
  }
  else
  {
//    rangeChk = smsatAddNComparebit64(LBA, TL);

    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);


    if (AllChk)
    {
      SM_DBG1(("smsatRead16: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      return SM_RC_SUCCESS;
    }
  }

  /* case 1 and 2 */
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* READ DMA*/
      /* in case that we can't fit the transfer length,
         we need to make it fit by sending multiple ATA cmnds */
      SM_DBG5(("smsatRead16: case 2\n"));


      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_DMA;           /* 0xC8 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
      fis->d.device         =
        (bit8)((0x4 << 4) | (scsiCmnd->cdb[6] & 0xF));        /* FIS LBA (27:24) and FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;


      agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
      satIOContext->ATACmd = SAT_READ_DMA;
    }
    else
    {
      /* case 1 */
      /* READ MULTIPLE or READ SECTOR(S) */
      /* READ SECTORS for easier implemetation */
      /* can't fit the transfer length but need to make it fit by sending multiple*/
      SM_DBG5(("smsatRead16: case 1\n"));

      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_SECTORS;       /* 0x20 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
      fis->d.device         =
        (bit8)((0x4 << 4) | (scsiCmnd->cdb[6] & 0xF));        /* FIS LBA (27:24) and FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;


      agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
      satIOContext->ATACmd = SAT_READ_SECTORS;
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* READ DMA EXT */
      SM_DBG5(("smsatRead16: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */

      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_DMA_EXT;       /* 0x25 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[12];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
      satIOContext->ATACmd = SAT_READ_DMA_EXT;

    }
    else
    {
      /* case 4 */
      /* READ MULTIPLE EXT or READ SECTOR(S) EXT or READ VERIFY SECTOR(S) EXT*/
      /* READ SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatRead16: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* Check FUA bit */
      if (scsiCmnd->cdb[1] & SCSI_READ16_FUA_MASK)
      {
        /* for now, no support for FUA */
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
        return SM_RC_SUCCESS;
      }

      fis->h.command        = SAT_READ_SECTORS_EXT;      /* 0x24 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[12];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
      satIOContext->ATACmd = SAT_READ_SECTORS_EXT;
    }
  }


  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* READ FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      SM_DBG1(("smsatRead16: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }

    SM_DBG6(("smsatRead16: case 5\n"));

    /* Support 48-bit FPDMA addressing, use READ FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_FPDMA_QUEUED;  /* 0x60 */
    fis->h.features       = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
    fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_READ16_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
    fis->d.featuresExp    = scsiCmnd->cdb[12];      /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_READ;
    satIOContext->ATACmd = SAT_READ_FPDMA_QUEUED;
  }

  /* saves the current LBA and orginal TL */
  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_READ_SECTORS || fis->h.command == SAT_READ_DMA)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_SECTORS_EXT || fis->h.command == SAT_READ_DMA_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_READ_FPDMA_QUEUEDK */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    SM_DBG5(("smsatRead16: NON CHAINED data\n"));
    satIOContext->satCompleteCB = &smsatNonChainedDataIOCB;
  }
  else
  {
    SM_DBG1(("smsatRead16: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_READ_SECTORS || fis->h.command == SAT_READ_DMA)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_READ_SECTORS_EXT || fis->h.command == SAT_READ_DMA_EXT)
    {
      /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      /* SAT_READ_FPDMA_QUEUED */
      fis->h.features       = 0xFF;
      fis->d.featuresExp    = 0xFF;
    }

    /* chained data */
    satIOContext->satCompleteCB = &smsatChainedDataIOCB;
  }

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatRead16: return\n"));
  return (status);

}

osGLOBAL bit32
smsatWrite6(
            smRoot_t                  *smRoot,
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
     )
{

  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit16                     tl = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatWrite6: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWrite6: return control!!!\n"));
    return SM_RC_SUCCESS;
  }


  /* cbd6; computing LBA and transfer length */
  lba = (((scsiCmnd->cdb[1]) & 0x1f) << (8*2))
    + (scsiCmnd->cdb[2] << 8) + scsiCmnd->cdb[3];
  tl = scsiCmnd->cdb[4];


  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
   */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    if (lba > SAT_TR_LBA_LIMIT - 1)
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    SM_DBG1(("smsatWrite6: return LBA out of range!!!\n"));
    return SM_RC_SUCCESS;
    }
  }

  /* case 1 and 2 */
  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      SM_DBG5(("smsatWrite6: case 2\n"));


      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      if (tl == 0)
      {
        /* temporary fix */
        fis->d.sectorCount    = 0xff;                   /* FIS sector count (7:0) */
      }
      else
      {
        fis->d.sectorCount    = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
      }
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
    }
    else
    {
      /* case 1 */
      /* WRITE SECTORS for easier implemetation */
      SM_DBG5(("smsatWrite6: case 1\n"));

      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_SECTORS;          /* 0xCA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      if (tl == 0)
      {
        /* temporary fix */
        fis->d.sectorCount    = 0xff;                   /* FIS sector count (7:0) */
      }
      else
      {
        fis->d.sectorCount    = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
      }
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;

    }
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT only */
      SM_DBG5(("smsatWrite6: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x35 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      if (tl == 0)
      {
        /* sector count is 256, 0x100*/
        fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0x01;                      /* FIS sector count (15:8) */
      }
      else
      {
        fis->d.sectorCount    = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
      }
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
    }
    else
    {
      /* case 4 */
      /* WRITE SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatWrite6: case 4\n"));

      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      if (tl == 0)
      {
        /* sector count is 256, 0x100*/
        fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0x01;                      /* FIS sector count (15:8) */
      }
      else
      {
        fis->d.sectorCount    = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
      }
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
    }
  }

   /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* WRITE FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      /* sanity check */
      SM_DBG5(("smsatWrite6: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    SM_DBG5(("smsatWrite6: case 5\n"));

    /* Support 48-bit FPDMA addressing, use WRITE FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->d.lbaLow         = scsiCmnd->cdb[3];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[2];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = (bit8)((scsiCmnd->cdb[1]) & 0x1f);       /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS FUA clear */
    fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    if (tl == 0)
    {
      /* sector count is 256, 0x100*/
      fis->h.features       = 0;                         /* FIS sector count (7:0) */
      fis->d.featuresExp    = 0x01;                      /* FIS sector count (15:8) */
    }
    else
    {
      fis->h.features       = scsiCmnd->cdb[4];       /* FIS sector count (7:0) */
      fis->d.featuresExp    = 0;                      /* FIS sector count (15:8) */
    }
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
  }

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatNonChainedDataIOCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);
}

osGLOBAL FORCEINLINE bit32
smsatWrite10(
             smRoot_t                  *smRoot,
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
            )
{
  smDeviceData_t           *pSatDevData = satIOContext->pSatDevData;
  smScsiRspSense_t         *pSense      = satIOContext->pSense;
  smIniScsiCmnd_t          *scsiCmnd    = &smScsiRequest->scsiCmnd;
  agsaFisRegHostToDevice_t *fis         =  satIOContext->pFis;
  bit32                     status      = SM_RC_FAILURE;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */
  bit8                      LBA[8];
  bit8                      TL[8];

  SM_DBG2(("smsatWrite10: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWrite10: return FUA_NV!!!\n"));
    return SM_RC_SUCCESS;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWrite10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }
/*
  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));
*/
  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = 0;                  /* MSB */
  LBA[1] = 0;
  LBA[2] = 0;
  LBA[3] = 0;  
  LBA[4] = scsiCmnd->cdb[2];  
  LBA[5] = scsiCmnd->cdb[3];
  LBA[6] = scsiCmnd->cdb[4];
  LBA[7] = scsiCmnd->cdb[5];   /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;
  TL[4] = 0;
  TL[5] = 0;
  TL[6] = scsiCmnd->cdb[7];  
  TL[7] = scsiCmnd->cdb[8];  	/* LSB */



  /* cbd10; computing LBA and transfer length */
  lba = (scsiCmnd->cdb[2] << (24)) + (scsiCmnd->cdb[3] << (16))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

  SM_DBG5(("smsatWrite10: lba %d functioned lba %d\n", lba, smsatComputeCDB10LBA(satIOContext)));
  SM_DBG5(("smsatWrite10: tl %d functioned tl %d\n", tl, smsatComputeCDB10TL(satIOContext)));

  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
   */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);
    if (AllChk)
    {
     SM_DBG1(("smsatWrite10: return LBA out of range, not EXT!!!\n"));
     SM_DBG1(("smsatWrite10: cdb 0x%x 0x%x 0x%x 0x%x!!!\n",scsiCmnd->cdb[2], scsiCmnd->cdb[3],
             scsiCmnd->cdb[4], scsiCmnd->cdb[5]));
     SM_DBG1(("smsatWrite10: lba 0x%x SAT_TR_LBA_LIMIT 0x%x!!!\n", lba, SAT_TR_LBA_LIMIT));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatWrite10: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }

  }

  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* WRITE FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      SM_DBG1(("smsatWrite10: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    SM_DBG6(("smsatWrite10: case 5\n"));

    /* Support 48-bit FPDMA addressing, use WRITE FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->h.features       = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_WRITE10_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    satIOContext->ATACmd = SAT_WRITE_FPDMA_QUEUED;
  }
  /* case 3 and 4 */
  else if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      SM_DBG5(("smsatWrite10: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* SAT_WRITE_DMA_FUA_EXT is optional and we don't support it */
      fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x35 */
      satIOContext->ATACmd  = SAT_WRITE_DMA_EXT;

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
    }
    else
    {
      /* case 4 */
      /* WRITE MULTIPLE EXT or WRITE MULTIPLE FUA EXT or WRITE SECTOR(S) EXT */
      /* WRITE SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatWrite10: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS_EXT;
    }
  }
  else /* case 1 and 2 */
  {  
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* can't fit the transfer length */
      SM_DBG5(("smsatWrite10: case 2\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA;
    }
    else
    {
      /* case 1 */
      /* WRITE MULTIPLE or WRITE SECTOR(S) */
      /* WRITE SECTORS for easier implemetation */
      /* can't fit the transfer length */
      SM_DBG5(("smsatWrite10: case 1\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS;
    }      
  }

  //  smhexdump("satWrite10 final fis", (bit8 *)fis, sizeof(agsaFisRegHostToDevice_t));

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
  {
    LoopNum = smsatComputeLoopNum(tl, 0x100);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    SM_DBG5(("smsatWrite10: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedDataIOCB;
  }
  else
  {
    SM_DBG2(("smsatWrite10: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
    {
      fis->d.sectorCount    = 0x0;
      smsatSplitSGL(smRoot,
                    smIORequest,
                    smDeviceHandle,
                    smScsiRequest,
                    satIOContext,
                    NON_BIT48_ADDRESS_TL_LIMIT*SATA_SECTOR_SIZE, /* 0x100 * 0x200 */
                    (satIOContext->OrgTL)*SATA_SECTOR_SIZE,
                    agTRUE);
    }
    else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
             fis->h.command == SAT_WRITE_DMA_EXT ||
             fis->h.command == SAT_WRITE_DMA_FUA_EXT
             )
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
      smsatSplitSGL(smRoot,
                    smIORequest,
                    smDeviceHandle,
                    smScsiRequest,
                    satIOContext,
                    BIT48_ADDRESS_TL_LIMIT*SATA_SECTOR_SIZE, /* 0xFFFF * 0x200 */
                    (satIOContext->OrgTL)*SATA_SECTOR_SIZE,
                    agTRUE);
    }
    else
    {
      /* SAT_WRITE_FPDMA_QUEUED */
      fis->h.features       = 0xFF;
      fis->d.featuresExp    = 0xFF;
      smsatSplitSGL(smRoot,
                    smIORequest,
                    smDeviceHandle,
                    smScsiRequest,
                    satIOContext,
                    BIT48_ADDRESS_TL_LIMIT*SATA_SECTOR_SIZE, /* 0xFFFF * 0x200 */
                    (satIOContext->OrgTL)*SATA_SECTOR_SIZE,
                    agTRUE);
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedDataIOCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);
}

osGLOBAL bit32
smsatWrite12(
             smRoot_t                  *smRoot,
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
            )
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatWrite12: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWrite12: return FUA_NV!!!\n"));
    return SM_RC_SUCCESS;

  }


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWrite10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }


  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = 0;                  /* MSB */
  LBA[1] = 0;
  LBA[2] = 0;
  LBA[3] = 0;  
  LBA[4] = scsiCmnd->cdb[2];	
  LBA[5] = scsiCmnd->cdb[3];
  LBA[6] = scsiCmnd->cdb[4];
  LBA[7] = scsiCmnd->cdb[5];  	/* LSB */

  TL[0] = 0;                    /* MSB */
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;   
  TL[4] = scsiCmnd->cdb[6];   
  TL[5] = scsiCmnd->cdb[7];
  TL[6] = scsiCmnd->cdb[8];
  TL[7] = scsiCmnd->cdb[9];   	/* LSB */


  lba = smsatComputeCDB12LBA(satIOContext);
  tl = smsatComputeCDB12TL(satIOContext);


  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
   */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);    

      /*smEnqueueIO(smRoot, satIOContext);*/



    if (AllChk)
    {
      SM_DBG1(("smsatWrite12: return LBA out of range, not EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);    
    if (AllChk)
    {
      SM_DBG1(("smsatWrite12: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);
      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
  }
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* In case that we can't fit the transfer length, we loop */
      SM_DBG5(("smsatWrite10: case 2\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA;
    }
    else
    {
      /* case 1 */
      /* WRITE MULTIPLE or WRITE SECTOR(S) */
      /* WRITE SECTORS for easier implemetation */
      /* In case that we can't fit the transfer length, we loop */
      SM_DBG5(("smsatWrite10: case 1\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS;
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      SM_DBG5(("smsatWrite10: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* SAT_WRITE_DMA_FUA_EXT is optional and we don't support it */
      fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x35 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA_EXT;
    }
    else
    {
      /* case 4 */
      /* WRITE MULTIPLE EXT or WRITE MULTIPLE FUA EXT or WRITE SECTOR(S) EXT */
      /* WRITE SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatWrite10: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS_EXT;
    }
  }

  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* WRITE FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
       SM_DBG5(("smsatWrite10: case 5 !!! error NCQ but 28 bit address support!!!\n"));
       smsatSetSensePayload( pSense,
                             SCSI_SNSKEY_ILLEGAL_REQUEST,
                             0,
                             SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                             satIOContext);

       /*smEnqueueIO(smRoot, satIOContext);*/

       tdsmIOCompletedCB( smRoot,
                          smIORequest,
                          smIOSuccess,
                          SCSI_STAT_CHECK_CONDITION,
                          satIOContext->pSmSenseData,
                          satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    SM_DBG6(("smsatWrite10: case 5\n"));

    /* Support 48-bit FPDMA addressing, use WRITE FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->h.features       = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_WRITE12_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    satIOContext->ATACmd = SAT_WRITE_FPDMA_QUEUED;
  }

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    SM_DBG5(("smsatWrite10: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedDataIOCB;
  }
  else
  {
    SM_DBG1(("smsatWrite10: CHAINED data\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
             fis->h.command == SAT_WRITE_DMA_EXT ||
             fis->h.command == SAT_WRITE_DMA_FUA_EXT
             )
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      /* SAT_WRITE_FPDMA_QUEUED */
      fis->h.features       = 0xFF;
      fis->d.featuresExp    = 0xFF;
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedDataIOCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);
}

osGLOBAL bit32
smsatWrite16(
             smRoot_t                  *smRoot,
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
            )
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatWrite16: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWrite16: return FUA_NV!!!\n"));
    return SM_RC_SUCCESS;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWrite16: return control!!!\n"));
    return SM_RC_SUCCESS;
  }


  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));


  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];
  LBA[4] = scsiCmnd->cdb[6];
  LBA[5] = scsiCmnd->cdb[7];
  LBA[6] = scsiCmnd->cdb[8];
  LBA[7] = scsiCmnd->cdb[9];  /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;
  TL[4] = scsiCmnd->cdb[10];   /* MSB */
  TL[5] = scsiCmnd->cdb[11];
  TL[6] = scsiCmnd->cdb[12];
  TL[7] = scsiCmnd->cdb[13];   /* LSB */



  lba = smsatComputeCDB16LBA(satIOContext);
  tl = smsatComputeCDB16TL(satIOContext);



  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
  */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
     )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatWrite16: return LBA out of range, not EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatWrite16: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }

  /* case 1 and 2 */
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* In case that we can't fit the transfer length, we loop */
      SM_DBG5(("smsatWrite16: case 2\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[6] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA;
    }
    else
    {
      /* case 1 */
      /* WRITE MULTIPLE or WRITE SECTOR(S) */
      /* WRITE SECTORS for easier implemetation */
      /* In case that we can't fit the transfer length, we loop */
      SM_DBG5(("smsatWrite16: case 1\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[6] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS;
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      SM_DBG5(("smsatWrite16: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* SAT_WRITE_DMA_FUA_EXT is optional and we don't support it */
      fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x35 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[12];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA_EXT;
    }
    else
    {
      /* case 4 */
      /* WRITE MULTIPLE EXT or WRITE MULTIPLE FUA EXT or WRITE SECTOR(S) EXT */
      /* WRITE SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatWrite16: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[12];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS_EXT;
    }
  }

  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* WRITE FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      SM_DBG5(("smsatWrite16: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    SM_DBG6(("smsatWrite16: case 5\n"));

    /* Support 48-bit FPDMA addressing, use WRITE FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->h.features       = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
    fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_WRITE16_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
    fis->d.featuresExp    = scsiCmnd->cdb[12];       /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    satIOContext->ATACmd = SAT_WRITE_FPDMA_QUEUED;
  }

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    SM_DBG5(("smsatWrite16: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedDataIOCB;
  }
  else
  {
    SM_DBG1(("smsatWrite16: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
             fis->h.command == SAT_WRITE_DMA_EXT ||
             fis->h.command == SAT_WRITE_DMA_FUA_EXT
             )
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      /* SAT_WRITE_FPDMA_QUEUED */
      fis->h.features       = 0xFF;
      fis->d.featuresExp    = 0xFF;
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedDataIOCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);
}


osGLOBAL bit32
smsatVerify10(
              smRoot_t                  *smRoot,
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smScsiRequest,
              smSatIOContext_t            *satIOContext
             )
{
  /*
    For simple implementation,
    no byte comparison supported as of 4/5/06
  */
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smDeviceData_t            *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  pSense            = satIOContext->pSense;
  scsiCmnd          = &smScsiRequest->scsiCmnd;
  pSatDevData       = satIOContext->pSatDevData;
  fis               = satIOContext->pFis;
  SM_DBG5(("smsatVerify10: start\n"));
  /* checking BYTCHK */
  if (scsiCmnd->cdb[1] & SCSI_VERIFY_BYTCHK_MASK)
  {
    /*
      should do the byte check
      but not supported in this version
     */
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
    /*smEnqueueIO(smRoot, satIOContext);*/
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatVerify10: no byte checking!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatVerify10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }


  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = 0;                  /* MSB */
  LBA[1] = 0;
  LBA[2] = 0;
  LBA[3] = 0;
  LBA[4] = scsiCmnd->cdb[2];  
  LBA[5] = scsiCmnd->cdb[3];
  LBA[6] = scsiCmnd->cdb[4];
  LBA[7] = scsiCmnd->cdb[5];  	/* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;
  TL[4] = 0;
  TL[5] = 0;
  TL[6] = scsiCmnd->cdb[7];  
  TL[7] = scsiCmnd->cdb[8];  	/* LSB */


  /* cbd10; computing LBA and transfer length */
  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatVerify10: return LBA out of range, not EXT!!!\n"));
      SM_DBG1(("smsatVerify10: cdb 0x%x 0x%x 0x%x 0x%x!!!\n",scsiCmnd->cdb[2], scsiCmnd->cdb[3],
             scsiCmnd->cdb[4], scsiCmnd->cdb[5]));
      SM_DBG1(("smsatVerify10: lba 0x%x SAT_TR_LBA_LIMIT 0x%x!!!\n", lba, SAT_TR_LBA_LIMIT));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatVerify10: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    SM_DBG5(("smsatVerify10: SAT_READ_VERIFY_SECTORS_EXT\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set 01000000 */
    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
    fis->d.sectorCountExp = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */

    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    satIOContext->ATACmd = SAT_READ_VERIFY_SECTORS_EXT;
  }
  else
  {
    SM_DBG5(("smsatVerify10: SAT_READ_VERIFY_SECTORS\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;      /* 0x40 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    satIOContext->ATACmd = SAT_READ_VERIFY_SECTORS;

 }

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_READ_VERIFY_SECTORS)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    SM_DBG1(("smsatVerify10: error case 1!!!\n"));
    LoopNum = 1;
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    SM_DBG5(("smsatVerify10: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedVerifyCB;
  }
  else
  {
    SM_DBG1(("smsatVerify10: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_READ_VERIFY_SECTORS)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      SM_DBG1(("smsatVerify10: error case 2!!!\n"));
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);
}

osGLOBAL bit32
smsatVerify12(
              smRoot_t                  *smRoot,
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smScsiRequest,
              smSatIOContext_t            *satIOContext
             )
{
  /*
    For simple implementation,
    no byte comparison supported as of 4/5/06
  */
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smDeviceData_t            *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  pSense            = satIOContext->pSense;
  scsiCmnd          = &smScsiRequest->scsiCmnd;
  pSatDevData       = satIOContext->pSatDevData;
  fis               = satIOContext->pFis;
  SM_DBG5(("smsatVerify12: start\n"));
  /* checking BYTCHK */
  if (scsiCmnd->cdb[1] & SCSI_VERIFY_BYTCHK_MASK)
  {
    /*
      should do the byte check
      but not supported in this version
     */
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatVerify12: no byte checking!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatVerify12: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = 0;                  /* MSB */
  LBA[1] = 0;
  LBA[2] = 0;
  LBA[3] = 0;
  LBA[4] = scsiCmnd->cdb[2];  
  LBA[5] = scsiCmnd->cdb[3];
  LBA[6] = scsiCmnd->cdb[4];
  LBA[7] = scsiCmnd->cdb[5];  	/* LSB */

  TL[0] = 0;                    /* MSB */
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;
  TL[4] = scsiCmnd->cdb[6];   
  TL[5] = scsiCmnd->cdb[7];
  TL[6] = scsiCmnd->cdb[8];
  TL[7] = scsiCmnd->cdb[9];   	/* LSB */


  lba = smsatComputeCDB12LBA(satIOContext);
  tl = smsatComputeCDB12TL(satIOContext);

  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);    
    if (AllChk)
    {
      SM_DBG1(("smsatVerify12: return LBA out of range, not EXT!!!\n"));
      SM_DBG1(("smsatVerify12: cdb 0x%x 0x%x 0x%x 0x%x!!!\n",scsiCmnd->cdb[2], scsiCmnd->cdb[3],
             scsiCmnd->cdb[4], scsiCmnd->cdb[5]));
      SM_DBG1(("smsatVerify12: lba 0x%x SAT_TR_LBA_LIMIT 0x%x!!!\n", lba, SAT_TR_LBA_LIMIT));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);    
    if (AllChk)
    {
      SM_DBG1(("smsatVerify12: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    SM_DBG5(("smsatVerify12: SAT_READ_VERIFY_SECTORS_EXT\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set 01000000 */
    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
    fis->d.sectorCountExp = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */

    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    satIOContext->ATACmd = SAT_READ_VERIFY_SECTORS_EXT;
  }
  else
  {
    SM_DBG5(("smsatVerify12: SAT_READ_VERIFY_SECTORS\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;      /* 0x40 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    satIOContext->ATACmd = SAT_READ_VERIFY_SECTORS;

 }

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_READ_VERIFY_SECTORS)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    SM_DBG1(("smsatVerify12: error case 1!!!\n"));
    LoopNum = 1;
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    SM_DBG5(("smsatVerify12: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedVerifyCB;
  }
  else
  {
    SM_DBG1(("smsatVerify12: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_READ_VERIFY_SECTORS)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      SM_DBG1(("smsatVerify12: error case 2!!!\n"));
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);
}

osGLOBAL bit32
smsatVerify16(
              smRoot_t                  *smRoot,
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smScsiRequest,
              smSatIOContext_t            *satIOContext
             )
{
  /*
    For simple implementation,
    no byte comparison supported as of 4/5/06
  */
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smDeviceData_t            *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  pSense            = satIOContext->pSense;
  scsiCmnd          = &smScsiRequest->scsiCmnd;
  pSatDevData       = satIOContext->pSatDevData;
  fis               = satIOContext->pFis;
  SM_DBG5(("smsatVerify16: start\n"));
  /* checking BYTCHK */
  if (scsiCmnd->cdb[1] & SCSI_VERIFY_BYTCHK_MASK)
  {
    /*
      should do the byte check
      but not supported in this version
     */
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
    /*smEnqueueIO(smRoot, satIOContext);*/
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    SM_DBG1(("smsatVerify16: no byte checking!!!\n"));
    return SM_RC_SUCCESS;
  }
  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
    /*smEnqueueIO(smRoot, satIOContext);*/
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    SM_DBG1(("smsatVerify16: return control!!!\n"));
    return SM_RC_SUCCESS;
  }
  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];
  LBA[4] = scsiCmnd->cdb[6];
  LBA[5] = scsiCmnd->cdb[7];
  LBA[6] = scsiCmnd->cdb[8];
  LBA[7] = scsiCmnd->cdb[9];  /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;
  TL[4] = scsiCmnd->cdb[10];   /* MSB */
  TL[5] = scsiCmnd->cdb[11];
  TL[6] = scsiCmnd->cdb[12];
  TL[7] = scsiCmnd->cdb[13];   /* LSB */
  lba = smsatComputeCDB16LBA(satIOContext);
  tl = smsatComputeCDB16TL(satIOContext);

  if (pSatDevData->satNCQ != agTRUE &&
     pSatDevData->sat48BitSupport != agTRUE
     )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatVerify16: return LBA out of range, not EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);
     /*smEnqueueIO(smRoot, satIOContext);*/
     tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
    return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatVerify16: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);
      /*smEnqueueIO(smRoot, satIOContext);*/
      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
    return SM_RC_SUCCESS;
    }
  }

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    SM_DBG5(("smsatVerify16: SAT_READ_VERIFY_SECTORS_EXT\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set 01000000 */
    fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
    fis->d.sectorCountExp = scsiCmnd->cdb[12];       /* FIS sector count (15:8) */

    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    satIOContext->ATACmd = SAT_READ_VERIFY_SECTORS_EXT;
  }
  else
  {
    SM_DBG5(("smsatVerify16: SAT_READ_VERIFY_SECTORS\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;      /* 0x40 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
      /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[6] & 0xF));
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    satIOContext->ATACmd = SAT_READ_VERIFY_SECTORS;

 }

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_READ_VERIFY_SECTORS)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    SM_DBG1(("smsatVerify16: error case 1!!!\n"));
    LoopNum = 1;
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    SM_DBG5(("smsatVerify16: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedVerifyCB;
  }
  else
  {
    SM_DBG1(("smsatVerify16: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_READ_VERIFY_SECTORS)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      SM_DBG1(("smsatVerify16: error case 2!!!\n"));
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);
}

osGLOBAL bit32
smsatTestUnitReady(
                   smRoot_t                  *smRoot,
                   smIORequest_t             *smIORequest,
                   smDeviceHandle_t          *smDeviceHandle,
                   smScsiInitiatorRequest_t  *smScsiRequest,
                   smSatIOContext_t            *satIOContext
                  )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatTestUnitReady: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatTestUnitReady: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* SAT revision 8, 8.11.2, p42*/
  if (pSatDevData->satStopState == agTRUE)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_INITIALIZING_COMMAND_REQUIRED,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    SM_DBG1(("smsatTestUnitReady: stop state!!!\n"));
    return SM_RC_SUCCESS;
  }

  /*
   * Check if format is in progress
   */
  if (pSatDevData->satDriveState == SAT_DEV_STATE_FORMAT_IN_PROGRESS)
  {
    SM_DBG1(("smsatTestUnitReady: FORMAT_IN_PROGRESS!!!\n"));

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_FORMAT_IN_PROGRESS,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    SM_DBG1(("smsatTestUnitReady: format in progress!!!\n"));
    return SM_RC_SUCCESS;
  }

  /*
    check previously issued ATA command
  */
  if (pSatDevData->satPendingIO != 0)
  {
    if (pSatDevData->satDeviceFaultState == agTRUE)
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_FAILURE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      SM_DBG1(("smsatTestUnitReady: previous command ended in error!!!\n"));
      return SM_RC_SUCCESS;
    }
  }

  /*
    check removalbe media feature set
   */
  if(pSatDevData->satRemovableMedia && pSatDevData->satRemovableMediaEnabled)
  {
    SM_DBG5(("smsatTestUnitReady: sending get media status cmnd\n"));
    /* send GET MEDIA STATUS command */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_GET_MEDIA_STATUS;   /* 0xDA */
    fis->h.features       = 0;                      /* FIS features NA       */
    fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
    fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;
    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatTestUnitReadyCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = smsataLLIOStart( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smScsiRequest,
                              satIOContext);

    return (status);
  }
  /*
    number 6) in SAT p42
    send ATA CHECK POWER MODE
  */
   SM_DBG5(("smsatTestUnitReady: sending check power mode cmnd\n"));
   status = smsatTestUnitReady_1( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);
   return (status);
}

osGLOBAL bit32
smsatTestUnitReady_1(
                     smRoot_t                  *smRoot,
                     smIORequest_t             *smIORequest,
                     smDeviceHandle_t          *smDeviceHandle,
                     smScsiInitiatorRequest_t  *smScsiRequest,
                     smSatIOContext_t            *satIOContext
                    )
{
  /*
    sends SAT_CHECK_POWER_MODE as a part of TESTUNITREADY
    internally generated - no directly corresponding scsi
    called in satIOCompleted as a part of satTestUnitReady(), SAT, revision8, 8.11.2, p42
  */
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;
  SM_DBG5(("smsatTestUnitReady_1: start\n"));
  /*
   * Send the ATA CHECK POWER MODE command.
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_CHECK_POWER_MODE;   /* 0xE5 */
  fis->h.features       = 0;
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatTestUnitReadyCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatTestUnitReady_1: return\n"));

  return status;
}

osGLOBAL bit32
smsatInquiry(
             smRoot_t                  *smRoot,
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
            )
{
  /*
    CMDDT bit is obsolete in SPC-3 and this is assumed in SAT revision 8
  */
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smDeviceData_t            *pSatDevData;
  bit32                      status;

  pSense      = satIOContext->pSense;
  scsiCmnd    = &smScsiRequest->scsiCmnd;
  pSatDevData = satIOContext->pSatDevData;
  SM_DBG5(("smsatInquiry: start\n"));
  SM_DBG5(("smsatInquiry: pSatDevData did %d\n", pSatDevData->id));
  //smhexdump("smsatInquiry", (bit8 *)scsiCmnd->cdb, 6);
  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
    /*smEnqueueIO(smRoot, satIOContext);*/
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    SM_DBG1(("smsatInquiry: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* checking EVPD and Allocation Length */
  /* SPC-4 spec 6.4 p141 */
  /* EVPD bit == 0 && PAGE CODE != 0 */
  if ( !(scsiCmnd->cdb[1] & SCSI_EVPD_MASK) &&
       (scsiCmnd->cdb[2] != 0)
       )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
    /*smEnqueueIO(smRoot, satIOContext);*/
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    SM_DBG1(("smsatInquiry: return EVPD and PAGE CODE!!!\n"));
    return SM_RC_SUCCESS;
  }
  SM_DBG6(("smsatInquiry: allocation length 0x%x %d\n", ((scsiCmnd->cdb[3]) << 8) + scsiCmnd->cdb[4], ((scsiCmnd->cdb[3]) << 8) + scsiCmnd->cdb[4]));
  /* convert OS IO to TD internal IO */
  if ( pSatDevData->IDDeviceValid == agFALSE)
  {
    status = smsatStartIDDev(
                             smRoot,
                             smIORequest,
                             smDeviceHandle,
                             smScsiRequest,
                             satIOContext
                            );
    SM_DBG6(("smsatInquiry: end status %d\n", status));
    return status;
  }
  else
  {
    SM_DBG6(("smsatInquiry: calling satInquiryIntCB\n"));
    smsatInquiryIntCB(
                      smRoot,
                      smIORequest,
                      smDeviceHandle,
                      smScsiRequest,
                      satIOContext
                     );
    /*smEnqueueIO(smRoot, satIOContext);*/
    return SM_RC_SUCCESS;
  }
}


osGLOBAL bit32
smsatStartIDDev(
                smRoot_t                  *smRoot,
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               )
{
  smSatInternalIo_t        *satIntIo = agNULL;
  smDeviceData_t           *satDevData = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smSatIOContext_t         *satNewIOContext;
  bit32                     status;

  SM_DBG5(("smsatStartIDDev: start\n"));

  satDevData = satIOContext->pSatDevData;

  SM_DBG6(("smsatStartIDDev: before alloc\n"));

  /* allocate identify device command */
  satIntIo = smsatAllocIntIoResource( smRoot,
                                      smIORequest,
                                      satDevData,
                                      sizeof(agsaSATAIdentifyData_t), /* 512; size of identify device data */
                                      satIntIo);

  SM_DBG6(("smsatStartIDDev: before after\n"));

  if (satIntIo == agNULL)
  {
    SM_DBG1(("smsatStartIDDev: can't alloacate!!!\n"));

    /*smEnqueueIO(smRoot, satIOContext);*/

    return SM_RC_FAILURE;
  }

  satIntIo->satOrgSmIORequest = smIORequest; /* changed */
  smIORequestBody = satIntIo->satIntRequestBody;
  satNewIOContext = &(smIORequestBody->transport.SATA.satIOContext);

  satNewIOContext->pSatDevData   = satDevData;
  satNewIOContext->pFis          = &(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satNewIOContext->pScsiCmnd     = &(satIntIo->satIntSmScsiXchg.scsiCmnd);
  satNewIOContext->pSense        = &(smIORequestBody->transport.SATA.sensePayload);
  satNewIOContext->pSmSenseData  = &(smIORequestBody->transport.SATA.smSenseData);
  satNewIOContext->smRequestBody = satIntIo->satIntRequestBody; /* key fix */
  satNewIOContext->interruptContext = tiInterruptContext;
  satNewIOContext->satIntIoContext  = satIntIo;

  satNewIOContext->psmDeviceHandle = agNULL;
  satNewIOContext->satOrgIOContext = satIOContext; /* changed */

  /* this is valid only for TD layer generated (not triggered by OS at all) IO */
  satNewIOContext->smScsiXchg = &(satIntIo->satIntSmScsiXchg);


  SM_DBG6(("smsatStartIDDev: OS satIOContext %p \n", satIOContext));
  SM_DBG6(("smsatStartIDDev: TD satNewIOContext %p \n", satNewIOContext));
  SM_DBG6(("smsatStartIDDev: OS tiScsiXchg %p \n", satIOContext->smScsiXchg));
  SM_DBG6(("smsatStartIDDev: TD tiScsiXchg %p \n", satNewIOContext->smScsiXchg));



  SM_DBG1(("smsatStartIDDev: satNewIOContext %p smIORequestBody %p!!!\n", satNewIOContext, smIORequestBody));

  status = smsatSendIDDev( smRoot,
                           &satIntIo->satIntSmIORequest, /* New smIORequest */
                           smDeviceHandle,
                           satNewIOContext->smScsiXchg, /* New tiScsiInitiatorRequest_t *tiScsiRequest, */
                           satNewIOContext);

  if (status != SM_RC_SUCCESS)
  {
    SM_DBG1(("smsatStartIDDev: failed in sending!!!\n"));

    smsatFreeIntIoResource( smRoot,
                            satDevData,
                            satIntIo);
    /*smEnqueueIO(smRoot, satIOContext);*/

    return SM_RC_FAILURE;
  }


  SM_DBG6(("smsatStartIDDev: end\n"));

  return status;
}

osGLOBAL bit32
smsatSendIDDev(
                smRoot_t                  *smRoot,
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t *fis;
#ifdef SM_INTERNAL_DEBUG
  smIORequestBody_t        *smIORequestBody;
  smSatInternalIo_t        *satIntIoContext;
#endif

  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;
  SM_DBG6(("smsatSendIDDev: start\n"));
  SM_DBG6(("smsatSendIDDev: did %d\n", pSatDevData->id));
#ifdef SM_INTERNAL_DEBUG
  satIntIoContext = satIOContext->satIntIoContext;
  smIORequestBody = satIntIoContext->satIntRequestBody;
#endif
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  if (pSatDevData->satDeviceType == SATA_ATAPI_DEVICE)
      fis->h.command    = SAT_IDENTIFY_PACKET_DEVICE;  /* 0x40 */
  else
      fis->h.command    = SAT_IDENTIFY_DEVICE;    /* 0xEC */
  fis->h.features       = 0;                      /* FIS reserve */
  fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
  fis->d.device         = 0;                      /* FIS LBA mode  */
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatInquiryCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

#ifdef SM_INTERNAL_DEBUG
  smhexdump("smsatSendIDDev", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
  smhexdump("smsatSendIDDev LL", (bit8 *)&(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev), sizeof(agsaFisRegHostToDevice_t));
#endif
  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG6(("smsatSendIDDev: end status %d\n", status));
  return status;
}

osGLOBAL bit32
smsatRequestSense(
                  smRoot_t                  *smRoot,
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 )
{
  /*
    SAT Rev 8 p38, Table25
    sending SMART RETURN STATUS
    Checking SMART Treshold Exceeded Condition is done in satRequestSenseCB()
    Only fixed format sense data is support. In other words, we don't support DESC bit is set
    in Request Sense
   */
  bit32                     status;
  bit32                     agRequestType;
  smScsiRspSense_t          *pSense;
  smDeviceData_t            *pSatDevData;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  smIORequestBody_t         *smIORequestBody;
  smSatInternalIo_t           *satIntIo = agNULL;
  smSatIOContext_t            *satIOContext2;
  bit8                      *pDataBuffer = agNULL;
  bit32                     allocationLen = 0;

  pSense            = satIOContext->pSense;
  pSatDevData       = satIOContext->pSatDevData;
  scsiCmnd          = &smScsiRequest->scsiCmnd;
  fis               = satIOContext->pFis;
  pDataBuffer       = (bit8 *) smScsiRequest->sglVirtualAddr;
  allocationLen     = scsiCmnd->cdb[4];
  allocationLen     = MIN(allocationLen, scsiCmnd->expDataLength);
  SM_DBG5(("smsatRequestSense: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
    sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatRequestSense: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /*
    Only fixed format sense data is support. In other words, we don't support DESC bit is set
    in Request Sense
   */
  if ( scsiCmnd->cdb[1] & ATA_REMOVABLE_MEDIA_DEVICE_MASK )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
    sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatRequestSense: DESC bit is set, which we don't support!!!\n"));
    return SM_RC_SUCCESS;
  }


  if (pSatDevData->satSMARTEnabled == agTRUE)
  {
    /* sends SMART RETURN STATUS */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_SMART;               /* 0xB0 */
    fis->h.features       = SAT_SMART_RETURN_STATUS; /* FIS features */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
    fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
    fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
    fis->d.lbaMid         = 0x4F;                   /* FIS LBA (15:8 ) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHigh        = 0xC2;                   /* FIS LBA (23:16) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved4      = 0;
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatRequestSenseCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = smsataLLIOStart( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smScsiRequest,
                              satIOContext);

    SM_DBG4(("smsatRequestSense: if return, status %d\n", status));
    return (status);
  }
  else
  {
    /*allocate iocontext for xmitting xmit SAT_CHECK_POWER_MODE
      then call satRequestSense2 */

    SM_DBG4(("smsatRequestSense: before satIntIo %p\n", satIntIo));
    /* allocate iocontext */
    satIntIo = smsatAllocIntIoResource( smRoot,
                                        smIORequest, /* original request */
                                        pSatDevData,
                                        smScsiRequest->scsiCmnd.expDataLength,
                                        satIntIo);

    SM_DBG4(("smsatRequestSense: after satIntIo %p\n", satIntIo));

    if (satIntIo == agNULL)
    {
      /* failed during sending SMART RETURN STATUS */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                            satIOContext);
      sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatRequestSense: else fail 1!!!\n"));
      return SM_RC_SUCCESS;
    } /* end of memory allocation failure */


    /*
     * Need to initialize all the fields within satIOContext except
     * reqType and satCompleteCB which will be set depending on cmd.
     */

    if (satIntIo == agNULL)
    {
      SM_DBG4(("smsatRequestSense: satIntIo is NULL\n"));
    }
    else
    {
      SM_DBG4(("smsatRequestSense: satIntIo is NOT NULL\n"));
    }
    /* use this --- tttttthe one the same */


    satIntIo->satOrgSmIORequest = smIORequest;
    smIORequestBody = (smIORequestBody_t *)satIntIo->satIntRequestBody;
    satIOContext2 = &(smIORequestBody->transport.SATA.satIOContext);

    satIOContext2->pSatDevData   = pSatDevData;
    satIOContext2->pFis          = &(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
    satIOContext2->pScsiCmnd     = &(satIntIo->satIntSmScsiXchg.scsiCmnd);
    satIOContext2->pSense        = &(smIORequestBody->transport.SATA.sensePayload);
    satIOContext2->pSmSenseData  = &(smIORequestBody->transport.SATA.smSenseData);
    satIOContext2->pSmSenseData->senseData = satIOContext2->pSense;
    satIOContext2->smRequestBody = satIntIo->satIntRequestBody;
    satIOContext2->interruptContext = satIOContext->interruptContext;
    satIOContext2->satIntIoContext  = satIntIo;
    satIOContext2->psmDeviceHandle = smDeviceHandle;
    satIOContext2->satOrgIOContext = satIOContext;

    SM_DBG4(("smsatRequestSense: satIntIo->satIntSmScsiXchg.agSgl1.len %d\n", satIntIo->satIntSmScsiXchg.smSgl1.len));

    SM_DBG4(("smsatRequestSense: satIntIo->satIntSmScsiXchg.agSgl1.upper %d\n", satIntIo->satIntSmScsiXchg.smSgl1.upper));

    SM_DBG4(("smsatRequestSense: satIntIo->satIntSmScsiXchg.agSgl1.lower %d\n", satIntIo->satIntSmScsiXchg.smSgl1.lower));

    SM_DBG4(("smsatRequestSense: satIntIo->satIntSmScsiXchg.agSgl1.type %d\n", satIntIo->satIntSmScsiXchg.smSgl1.type));

    status = smsatRequestSense_1( smRoot,
                                  &(satIntIo->satIntSmIORequest),
                                  smDeviceHandle,
                                  &(satIntIo->satIntSmScsiXchg),
                                  satIOContext2);

    if (status != SM_RC_SUCCESS)
    {
      smsatFreeIntIoResource( smRoot,
                              pSatDevData,
                              satIntIo);

      /* failed during sending SMART RETURN STATUS */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                            satIOContext);
      sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         agNULL,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatRequestSense: else fail 2!!!\n"));
      return SM_RC_SUCCESS;
    }
    SM_DBG4(("smsatRequestSense: else return success\n"));
    return SM_RC_SUCCESS;
  }
}

osGLOBAL bit32
smsatRequestSense_1(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   )
{
  /*
    sends SAT_CHECK_POWER_MODE
  */
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis               = satIOContext->pFis;
  SM_DBG5(("smsatRequestSense_1: start\n"));
  /*
   * Send the ATA CHECK POWER MODE command.
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_CHECK_POWER_MODE;   /* 0xE5 */
  fis->h.features       = 0;
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatRequestSenseCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */


  SM_DBG4(("smsatRequestSense_1: smSgl1.len %d\n", smScsiRequest->smSgl1.len));

  SM_DBG4(("smsatRequestSense_1: smSgl1.upper %d\n", smScsiRequest->smSgl1.upper));

  SM_DBG4(("smsatRequestSense_1: smSgl1.lower %d\n", smScsiRequest->smSgl1.lower));

  SM_DBG4(("smsatRequestSense_1: smSgl1.type %d\n", smScsiRequest->smSgl1.type));

  //  smhexdump("smsatRequestSense_1", (bit8 *)fis, sizeof(agsaFisRegHostToDevice_t));

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);



  return status;
}

osGLOBAL bit32
smsatModeSense6(
                smRoot_t                  *smRoot,
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               )
{
  smScsiRspSense_t        *pSense;
  bit32                   allocationLen;
  smIniScsiCmnd_t         *scsiCmnd;
  bit32                   pageSupported;
  bit8                    page;
  bit8                    *pModeSense;    /* Mode Sense data buffer */
  smDeviceData_t          *pSatDevData;
  bit8                    PC;
  bit8                    AllPages[MODE_SENSE6_RETURN_ALL_PAGES_LEN];
  bit8                    Control[MODE_SENSE6_CONTROL_PAGE_LEN];
  bit8                    RWErrorRecovery[MODE_SENSE6_READ_WRITE_ERROR_RECOVERY_PAGE_LEN];
  bit8                    Caching[MODE_SENSE6_CACHING_LEN];
  bit8                    InfoExceptionCtrl[MODE_SENSE6_INFORMATION_EXCEPTION_CONTROL_PAGE_LEN];
  bit8                    lenRead = 0;


  pSense      = satIOContext->pSense;
  scsiCmnd    = &smScsiRequest->scsiCmnd;
  pModeSense  = (bit8 *) smScsiRequest->sglVirtualAddr;
  pSatDevData = satIOContext->pSatDevData;

  //smhexdump("smsatModeSense6", (bit8 *)scsiCmnd->cdb, 6);
  SM_DBG5(("smsatModeSense6: start\n"));
  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
   /*smEnqueueIO(smRoot, satIOContext);*/
   tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    SM_DBG1(("smsatModeSense6: return control!!!\n"));
    return SM_RC_SUCCESS;
  }
  /* checking PC(Page Control)
     SAT revion 8, 8.5.3 p33 and 10.1.2, p66
  */
  PC = (bit8)((scsiCmnd->cdb[2]) & SCSI_MODE_SENSE6_PC_MASK);
  if (PC != 0)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
    /*smEnqueueIO(smRoot, satIOContext);*/
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    SM_DBG1(("smsatModeSense6: return due to PC value pc 0x%x!!!\n", PC >> 6));
    return SM_RC_SUCCESS;
  }
  /* reading PAGE CODE */
  page = (bit8)((scsiCmnd->cdb[2]) & SCSI_MODE_SENSE6_PAGE_CODE_MASK);


  SM_DBG5(("smsatModeSense6: page=0x%x\n", page));

  allocationLen = scsiCmnd->cdb[4];
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength);
    /*
    Based on page code value, returns a corresponding mode page
    note: no support for subpage
  */
  switch(page)
  {
    case MODESENSE_RETURN_ALL_PAGES:
    case MODESENSE_CONTROL_PAGE: /* control */
    case MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE: /* Read-Write Error Recovery */
    case MODESENSE_CACHING: /* caching */
    case MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE: /* informational exceptions control*/
      pageSupported = agTRUE;
      break;
    case MODESENSE_VENDOR_SPECIFIC_PAGE: /* vendor specific */
    default:
      pageSupported = agFALSE;
      break;
  }

  if (pageSupported == agFALSE)
  {

    SM_DBG1(("smsatModeSense6 *** ERROR *** not supported page 0x%x did %d!!!\n",
        page, pSatDevData->id));

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }

  switch(page)
  {
  case MODESENSE_RETURN_ALL_PAGES:
    lenRead = (bit8)MIN(allocationLen, MODE_SENSE6_RETURN_ALL_PAGES_LEN); 
    break;
  case MODESENSE_CONTROL_PAGE: /* control */
    lenRead = (bit8)MIN(allocationLen, MODE_SENSE6_CONTROL_PAGE_LEN); 
    break;
  case MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE: /* Read-Write Error Recovery */
    lenRead = (bit8)MIN(allocationLen, MODE_SENSE6_READ_WRITE_ERROR_RECOVERY_PAGE_LEN); 
    break;
  case MODESENSE_CACHING: /* caching */
    lenRead = (bit8)MIN(allocationLen, MODE_SENSE6_CACHING_LEN); 
    break;
  case MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE: /* informational exceptions control*/
    lenRead = (bit8)MIN(allocationLen, MODE_SENSE6_INFORMATION_EXCEPTION_CONTROL_PAGE_LEN); 
    break;
  default:
    SM_DBG1(("smsatModeSense6: default error page %d!!!\n", page));
    break;
  }

  if (page == MODESENSE_RETURN_ALL_PAGES)
  {
    SM_DBG5(("smsatModeSense6: MODESENSE_RETURN_ALL_PAGES\n"));
    AllPages[0] = (bit8)(lenRead - 1);
    AllPages[1] = 0x00; /* default medium type (currently mounted medium type) */
    AllPages[2] = 0x00; /* no write-protect, no support for DPO-FUA */
    AllPages[3] = 0x08; /* block descriptor length */

    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    /* density code */
    AllPages[4]  = 0x04; /* density-code : reserved for direct-access */
    /* number of blocks */
    AllPages[5]  = 0x00; /* unspecified */
    AllPages[6]  = 0x00; /* unspecified */
    AllPages[7]  = 0x00; /* unspecified */
    /* reserved */
    AllPages[8]  = 0x00; /* reserved */
    /* Block size */
    AllPages[9]  = 0x00;
    AllPages[10] = 0x02;   /* Block size is always 512 bytes */
    AllPages[11] = 0x00;

    /* MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE */
    AllPages[12] = 0x01; /* page code */
    AllPages[13] = 0x0A; /* page length */
    AllPages[14] = 0x40; /* ARRE is set */
    AllPages[15] = 0x00;
    AllPages[16] = 0x00;
    AllPages[17] = 0x00;
    AllPages[18] = 0x00;
    AllPages[19] = 0x00;
    AllPages[20] = 0x00;
    AllPages[21] = 0x00;
    AllPages[22] = 0x00;
    AllPages[23] = 0x00;
    /* MODESENSE_CACHING */
    AllPages[24] = 0x08; /* page code */
    AllPages[25] = 0x12; /* page length */
    if (pSatDevData->satWriteCacheEnabled == agTRUE)
    {
      AllPages[26] = 0x04;/* WCE bit is set */
    }
    else
    {
      AllPages[26] = 0x00;/* WCE bit is NOT set */
    }

    AllPages[27] = 0x00;
    AllPages[28] = 0x00;
    AllPages[29] = 0x00;
    AllPages[30] = 0x00;
    AllPages[31] = 0x00;
    AllPages[32] = 0x00;
    AllPages[33] = 0x00;
    AllPages[34] = 0x00;
    AllPages[35] = 0x00;
    if (pSatDevData->satLookAheadEnabled == agTRUE)
    {
      AllPages[36] = 0x00;/* DRA bit is NOT set */
    }
    else
    {
      AllPages[36] = 0x20;/* DRA bit is set */
    }
    AllPages[37] = 0x00;
    AllPages[38] = 0x00;
    AllPages[39] = 0x00;
    AllPages[40] = 0x00;
    AllPages[41] = 0x00;
    AllPages[42] = 0x00;
    AllPages[43] = 0x00;
    /* MODESENSE_CONTROL_PAGE */
    AllPages[44] = 0x0A; /* page code */
    AllPages[45] = 0x0A; /* page length */
    AllPages[46] = 0x02; /* only GLTSD bit is set */
    if (pSatDevData->satNCQ == agTRUE)
    {
      AllPages[47] = 0x12; /* Queue Alogorithm modifier 1b and QErr 01b*/
    }
    else
    {
      AllPages[47] = 0x02; /* Queue Alogorithm modifier 0b and QErr 01b */
    }
    AllPages[48] = 0x00;
    AllPages[49] = 0x00;
    AllPages[50] = 0x00; /* obsolete */
    AllPages[51] = 0x00; /* obsolete */
    AllPages[52] = 0xFF; /* Busy Timeout Period */
    AllPages[53] = 0xFF; /* Busy Timeout Period */
    AllPages[54] = 0x00; /* we don't support non-000b value for the self-test code */
    AllPages[55] = 0x00; /* we don't support non-000b value for the self-test code */
    /* MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE */
    AllPages[56] = 0x1C; /* page code */
    AllPages[57] = 0x0A; /* page length */
    if (pSatDevData->satSMARTEnabled == agTRUE)
    {
      AllPages[58] = 0x00;/* DEXCPT bit is NOT set */
    }
    else
    {
      AllPages[58] = 0x08;/* DEXCPT bit is set */
    }
    AllPages[59] = 0x00; /* We don't support MRIE */
    AllPages[60] = 0x00; /* Interval timer vendor-specific */
    AllPages[61] = 0x00;
    AllPages[62] = 0x00;
    AllPages[63] = 0x00;
    AllPages[64] = 0x00; /* REPORT-COUNT */
    AllPages[65] = 0x00;
    AllPages[66] = 0x00;
    AllPages[67] = 0x00;

    sm_memcpy(pModeSense, &AllPages, lenRead);
  }
  else if (page == MODESENSE_CONTROL_PAGE)
  {
    SM_DBG5(("smsatModeSense6: MODESENSE_CONTROL_PAGE\n"));
    Control[0] = MODE_SENSE6_CONTROL_PAGE_LEN - 1;
    Control[1] = 0x00; /* default medium type (currently mounted medium type) */
    Control[2] = 0x00; /* no write-protect, no support for DPO-FUA */
    Control[3] = 0x08; /* block descriptor length */
    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    /* density code */
    Control[4]  = 0x04; /* density-code : reserved for direct-access */
    /* number of blocks */
    Control[5]  = 0x00; /* unspecified */
    Control[6]  = 0x00; /* unspecified */
    Control[7]  = 0x00; /* unspecified */
    /* reserved */
    Control[8]  = 0x00; /* reserved */
    /* Block size */
    Control[9]  = 0x00;
    Control[10] = 0x02;   /* Block size is always 512 bytes */
    Control[11] = 0x00;
    /*
     * Fill-up control mode page, SAT, Table 65
     */
    Control[12] = 0x0A; /* page code */
    Control[13] = 0x0A; /* page length */
    Control[14] = 0x02; /* only GLTSD bit is set */
    if (pSatDevData->satNCQ == agTRUE)
    {
      Control[15] = 0x12; /* Queue Alogorithm modifier 1b and QErr 01b*/
    }
    else
    {
      Control[15] = 0x02; /* Queue Alogorithm modifier 0b and QErr 01b */
    }
    Control[16] = 0x00;
    Control[17] = 0x00;
    Control[18] = 0x00; /* obsolete */
    Control[19] = 0x00; /* obsolete */
    Control[20] = 0xFF; /* Busy Timeout Period */
    Control[21] = 0xFF; /* Busy Timeout Period */
    Control[22] = 0x00; /* we don't support non-000b value for the self-test code */
    Control[23] = 0x00; /* we don't support non-000b value for the self-test code */

    sm_memcpy(pModeSense, &Control, lenRead);

  }
  else if (page == MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE)
  {
    SM_DBG5(("smsatModeSense6: MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE\n"));
    RWErrorRecovery[0] = MODE_SENSE6_READ_WRITE_ERROR_RECOVERY_PAGE_LEN - 1;
    RWErrorRecovery[1] = 0x00; /* default medium type (currently mounted medium type) */
    RWErrorRecovery[2] = 0x00; /* no write-protect, no support for DPO-FUA */
    RWErrorRecovery[3] = 0x08; /* block descriptor length */
    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    /* density code */
    RWErrorRecovery[4]  = 0x04; /* density-code : reserved for direct-access */
    /* number of blocks */
    RWErrorRecovery[5]  = 0x00; /* unspecified */
    RWErrorRecovery[6]  = 0x00; /* unspecified */
    RWErrorRecovery[7]  = 0x00; /* unspecified */
    /* reserved */
    RWErrorRecovery[8]  = 0x00; /* reserved */
    /* Block size */
    RWErrorRecovery[9]  = 0x00;
    RWErrorRecovery[10] = 0x02;   /* Block size is always 512 bytes */
    RWErrorRecovery[11] = 0x00;
    /*
     * Fill-up Read-Write Error Recovery mode page, SAT, Table 66
     */
    RWErrorRecovery[12] = 0x01; /* page code */
    RWErrorRecovery[13] = 0x0A; /* page length */
    RWErrorRecovery[14] = 0x40; /* ARRE is set */
    RWErrorRecovery[15] = 0x00;
    RWErrorRecovery[16] = 0x00;
    RWErrorRecovery[17] = 0x00;
    RWErrorRecovery[18] = 0x00;
    RWErrorRecovery[19] = 0x00;
    RWErrorRecovery[20] = 0x00;
    RWErrorRecovery[21] = 0x00;
    RWErrorRecovery[22] = 0x00;
    RWErrorRecovery[23] = 0x00;

    sm_memcpy(pModeSense, &RWErrorRecovery, lenRead);

  }
  else if (page == MODESENSE_CACHING)
  {
    SM_DBG5(("smsatModeSense6: MODESENSE_CACHING\n"));
    /* special case */
    if (allocationLen == 4 && page == MODESENSE_CACHING)
    {
      SM_DBG5(("smsatModeSense6: linux 2.6.8.24 support\n"));

      Caching[0] = 0x20 - 1; /* 32 - 1 */
      Caching[1] = 0x00; /* default medium type (currently mounted medium type) */
      Caching[2] = 0x00; /* no write-protect, no support for DPO-FUA */
      Caching[3] = 0x08; /* block descriptor length */

      sm_memcpy(pModeSense, &Caching, 4);
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext);
      return SM_RC_SUCCESS;
    }
    Caching[0] = MODE_SENSE6_CACHING_LEN - 1;
    Caching[1] = 0x00; /* default medium type (currently mounted medium type) */
    Caching[2] = 0x00; /* no write-protect, no support for DPO-FUA */
    Caching[3] = 0x08; /* block descriptor length */
    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    /* density code */
    Caching[4]  = 0x04; /* density-code : reserved for direct-access */
    /* number of blocks */
    Caching[5]  = 0x00; /* unspecified */
    Caching[6]  = 0x00; /* unspecified */
    Caching[7]  = 0x00; /* unspecified */
    /* reserved */
    Caching[8]  = 0x00; /* reserved */
    /* Block size */
    Caching[9]  = 0x00;
    Caching[10] = 0x02;   /* Block size is always 512 bytes */
    Caching[11] = 0x00;
    /*
     * Fill-up Caching mode page, SAT, Table 67
     */
    /* length 20 */
    Caching[12] = 0x08; /* page code */
    Caching[13] = 0x12; /* page length */
    if (pSatDevData->satWriteCacheEnabled == agTRUE)
    {
      Caching[14] = 0x04;/* WCE bit is set */
    }
    else
    {
      Caching[14] = 0x00;/* WCE bit is NOT set */
    }

    Caching[15] = 0x00;
    Caching[16] = 0x00;
    Caching[17] = 0x00;
    Caching[18] = 0x00;
    Caching[19] = 0x00;
    Caching[20] = 0x00;
    Caching[21] = 0x00;
    Caching[22] = 0x00;
    Caching[23] = 0x00;
    if (pSatDevData->satLookAheadEnabled == agTRUE)
    {
      Caching[24] = 0x00;/* DRA bit is NOT set */
    }
    else
    {
      Caching[24] = 0x20;/* DRA bit is set */
    }
    Caching[25] = 0x00;
    Caching[26] = 0x00;
    Caching[27] = 0x00;
    Caching[28] = 0x00;
    Caching[29] = 0x00;
    Caching[30] = 0x00;
    Caching[31] = 0x00;

    sm_memcpy(pModeSense, &Caching, lenRead);

  }
  else if (page == MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE)
  {
    SM_DBG5(("smsatModeSense6: MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE\n"));
    InfoExceptionCtrl[0] = MODE_SENSE6_INFORMATION_EXCEPTION_CONTROL_PAGE_LEN - 1;
    InfoExceptionCtrl[1] = 0x00; /* default medium type (currently mounted medium type) */
    InfoExceptionCtrl[2] = 0x00; /* no write-protect, no support for DPO-FUA */
    InfoExceptionCtrl[3] = 0x08; /* block descriptor length */
    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    /* density code */
    InfoExceptionCtrl[4]  = 0x04; /* density-code : reserved for direct-access */
    /* number of blocks */
    InfoExceptionCtrl[5]  = 0x00; /* unspecified */
    InfoExceptionCtrl[6]  = 0x00; /* unspecified */
    InfoExceptionCtrl[7]  = 0x00; /* unspecified */
    /* reserved */
    InfoExceptionCtrl[8]  = 0x00; /* reserved */
    /* Block size */
    InfoExceptionCtrl[9]  = 0x00;
    InfoExceptionCtrl[10] = 0x02;   /* Block size is always 512 bytes */
    InfoExceptionCtrl[11] = 0x00;
    /*
     * Fill-up informational-exceptions control mode page, SAT, Table 68
     */
    InfoExceptionCtrl[12] = 0x1C; /* page code */
    InfoExceptionCtrl[13] = 0x0A; /* page length */
     if (pSatDevData->satSMARTEnabled == agTRUE)
    {
      InfoExceptionCtrl[14] = 0x00;/* DEXCPT bit is NOT set */
    }
    else
    {
      InfoExceptionCtrl[14] = 0x08;/* DEXCPT bit is set */
    }
    InfoExceptionCtrl[15] = 0x00; /* We don't support MRIE */
    InfoExceptionCtrl[16] = 0x00; /* Interval timer vendor-specific */
    InfoExceptionCtrl[17] = 0x00;
    InfoExceptionCtrl[18] = 0x00;
    InfoExceptionCtrl[19] = 0x00;
    InfoExceptionCtrl[20] = 0x00; /* REPORT-COUNT */
    InfoExceptionCtrl[21] = 0x00;
    InfoExceptionCtrl[22] = 0x00;
    InfoExceptionCtrl[23] = 0x00;
    sm_memcpy(pModeSense, &InfoExceptionCtrl, lenRead);

  }
  else
  {
    /* Error */
    SM_DBG1(("smsatModeSense6: Error page %d!!!\n", page));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }

  /* there can be only underrun not overrun in error case */
  if (allocationLen > lenRead)
  {
    SM_DBG6(("smsatModeSense6 reporting underrun lenRead=0x%x allocationLen=0x%x\n", lenRead, allocationLen));      

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOUnderRun,
                       allocationLen - lenRead,
                       agNULL,
                       satIOContext->interruptContext );


  }
  else
  {
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
  }

  return SM_RC_SUCCESS;

}

osGLOBAL bit32
smsatModeSense10(
                  smRoot_t                  *smRoot,
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 )
{
  smScsiRspSense_t        *pSense;
  bit32                   allocationLen;
  smIniScsiCmnd_t         *scsiCmnd;
  bit32                   pageSupported;
  bit8                    page;
  bit8                    *pModeSense;    /* Mode Sense data buffer */
  smDeviceData_t          *pSatDevData;
  bit8                    PC; /* page control */
  bit8                    LLBAA; /* Long LBA Accepted */
  bit32                   index;
  bit8                    AllPages[MODE_SENSE10_RETURN_ALL_PAGES_LLBAA_LEN];
  bit8                    Control[MODE_SENSE10_CONTROL_PAGE_LLBAA_LEN];
  bit8                    RWErrorRecovery[MODE_SENSE10_READ_WRITE_ERROR_RECOVERY_PAGE_LLBAA_LEN];
  bit8                    Caching[MODE_SENSE10_CACHING_LLBAA_LEN];
  bit8                    InfoExceptionCtrl[MODE_SENSE10_INFORMATION_EXCEPTION_CONTROL_PAGE_LLBAA_LEN];
  bit8                    lenRead = 0;

  pSense      = satIOContext->pSense;
  scsiCmnd    = &smScsiRequest->scsiCmnd;
  pModeSense  = (bit8 *) smScsiRequest->sglVirtualAddr;
  pSatDevData = satIOContext->pSatDevData;
  SM_DBG5(("smsatModeSense10: start\n"));
  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatModeSense10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* checking PC(Page Control)
     SAT revion 8, 8.5.3 p33 and 10.1.2, p66
  */
  PC = (bit8)((scsiCmnd->cdb[2]) & SCSI_MODE_SENSE10_PC_MASK);
  if (PC != 0)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatModeSense10: return due to PC value pc 0x%x!!!\n", PC));
    return SM_RC_SUCCESS;
  }

  /* finding LLBAA bit */
  LLBAA = (bit8)((scsiCmnd->cdb[1]) & SCSI_MODE_SENSE10_LLBAA_MASK);

  /* reading PAGE CODE */
  page = (bit8)((scsiCmnd->cdb[2]) & SCSI_MODE_SENSE10_PAGE_CODE_MASK);
  SM_DBG5(("smsatModeSense10: page=0x%x, did %d\n", page, pSatDevData->id));
  allocationLen = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength);

  /*
    Based on page code value, returns a corresponding mode page
    note: no support for subpage
  */
  switch(page)
  {
    case MODESENSE_RETURN_ALL_PAGES: /* return all pages */
    case MODESENSE_CONTROL_PAGE: /* control */
    case MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE: /* Read-Write Error Recovery */
    case MODESENSE_CACHING: /* caching */
    case MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE: /* informational exceptions control*/
      pageSupported = agTRUE;
      break;
    case MODESENSE_VENDOR_SPECIFIC_PAGE: /* vendor specific */
    default:
      pageSupported = agFALSE;
      break;
  }
  if (pageSupported == agFALSE)
  {
    SM_DBG1(("smsatModeSense10 *** ERROR *** not supported page 0x%x did %d!!!\n", page, pSatDevData->id));

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);
    /*smEnqueueIO(smRoot, satIOContext);*/
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }
  switch(page)
  {
  case MODESENSE_RETURN_ALL_PAGES:
    if (LLBAA)
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_RETURN_ALL_PAGES_LLBAA_LEN); 
    }
    else
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_RETURN_ALL_PAGES_LEN);
    }
    break;
  case MODESENSE_CONTROL_PAGE: /* control */
    if (LLBAA)
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_CONTROL_PAGE_LLBAA_LEN); 
    }
    else
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_CONTROL_PAGE_LEN);
    }
    break;
  case MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE: /* Read-Write Error Recovery */
    if (LLBAA)
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_READ_WRITE_ERROR_RECOVERY_PAGE_LLBAA_LEN); 
    }
    else
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_READ_WRITE_ERROR_RECOVERY_PAGE_LEN);
    }
    break;
  case MODESENSE_CACHING: /* caching */
    if (LLBAA)
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_CACHING_LLBAA_LEN); 
    }
    else
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_CACHING_LEN);
    }
    break;
  case MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE: /* informational exceptions control*/
    if (LLBAA)
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_INFORMATION_EXCEPTION_CONTROL_PAGE_LLBAA_LEN); 
    }
    else
    {
      lenRead = (bit8)MIN(allocationLen, MODE_SENSE10_INFORMATION_EXCEPTION_CONTROL_PAGE_LEN);
    }
    break;
  default:
    SM_DBG1(("smsatModeSense10: default error page %d!!!\n", page));
    break;
  }

  if (page == MODESENSE_RETURN_ALL_PAGES)
  {
    SM_DBG5(("smsatModeSense10: MODESENSE_RETURN_ALL_PAGES\n"));
    AllPages[0] = 0;
    AllPages[1] = (bit8)(lenRead - 2);
    AllPages[2] = 0x00; /* medium type: default medium type (currently mounted medium type) */
    AllPages[3] = 0x00; /* device-specific param: no write-protect, no support for DPO-FUA */
    if (LLBAA)
    {
      AllPages[4] = 0x00; /* reserved and LONGLBA */
      AllPages[4] = (bit8)(AllPages[4] | 0x1); /* LONGLBA is set */
    }
    else
    {
      AllPages[4] = 0x00; /* reserved and LONGLBA: LONGLBA is not set */
    }
    AllPages[5] = 0x00; /* reserved */
    AllPages[6] = 0x00; /* block descriptot length */
    if (LLBAA)
    {
      AllPages[7] = 0x10; /* block descriptor length: LONGLBA is set. So, length is 16 */
    }
    else
    {
      AllPages[7] = 0x08; /* block descriptor length: LONGLBA is NOT set. So, length is 8 */
    }

    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    if (LLBAA)
    {
      /* density code */
      AllPages[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      AllPages[9]   = 0x00; /* unspecified */
      AllPages[10]  = 0x00; /* unspecified */
      AllPages[11]  = 0x00; /* unspecified */
      AllPages[12]  = 0x00; /* unspecified */
      AllPages[13]  = 0x00; /* unspecified */
      AllPages[14]  = 0x00; /* unspecified */
      AllPages[15]  = 0x00; /* unspecified */
      /* reserved */
      AllPages[16]  = 0x00; /* reserved */
      AllPages[17]  = 0x00; /* reserved */
      AllPages[18]  = 0x00; /* reserved */
      AllPages[19]  = 0x00; /* reserved */
      /* Block size */
      AllPages[20]  = 0x00;
      AllPages[21]  = 0x00;
      AllPages[22]  = 0x02;   /* Block size is always 512 bytes */
      AllPages[23]  = 0x00;
    }
    else
    {
      /* density code */
      AllPages[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      AllPages[9]   = 0x00; /* unspecified */
      AllPages[10]  = 0x00; /* unspecified */
      AllPages[11]  = 0x00; /* unspecified */
      /* reserved */
      AllPages[12]  = 0x00; /* reserved */
      /* Block size */
      AllPages[13]  = 0x00;
      AllPages[14]  = 0x02;   /* Block size is always 512 bytes */
      AllPages[15]  = 0x00;
    }

    if (LLBAA)
    {
      index = 24;
    }
    else
    {
      index = 16;
    }
    /* MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE */
    AllPages[index+0] = 0x01; /* page code */
    AllPages[index+1] = 0x0A; /* page length */
    AllPages[index+2] = 0x40; /* ARRE is set */
    AllPages[index+3] = 0x00;
    AllPages[index+4] = 0x00;
    AllPages[index+5] = 0x00;
    AllPages[index+6] = 0x00;
    AllPages[index+7] = 0x00;
    AllPages[index+8] = 0x00;
    AllPages[index+9] = 0x00;
    AllPages[index+10] = 0x00;
    AllPages[index+11] = 0x00;

    /* MODESENSE_CACHING */
    /*
     * Fill-up Caching mode page, SAT, Table 67
     */
    /* length 20 */
    AllPages[index+12] = 0x08; /* page code */
    AllPages[index+13] = 0x12; /* page length */
    if (pSatDevData->satWriteCacheEnabled == agTRUE)
    {
      AllPages[index+14] = 0x04;/* WCE bit is set */
    }
    else
    {
      AllPages[index+14] = 0x00;/* WCE bit is NOT set */
    }

    AllPages[index+15] = 0x00;
    AllPages[index+16] = 0x00;
    AllPages[index+17] = 0x00;
    AllPages[index+18] = 0x00;
    AllPages[index+19] = 0x00;
    AllPages[index+20] = 0x00;
    AllPages[index+21] = 0x00;
    AllPages[index+22] = 0x00;
    AllPages[index+23] = 0x00;
    if (pSatDevData->satLookAheadEnabled == agTRUE)
    {
      AllPages[index+24] = 0x00;/* DRA bit is NOT set */
    }
    else
    {
      AllPages[index+24] = 0x20;/* DRA bit is set */
    }
    AllPages[index+25] = 0x00;
    AllPages[index+26] = 0x00;
    AllPages[index+27] = 0x00;
    AllPages[index+28] = 0x00;
    AllPages[index+29] = 0x00;
    AllPages[index+30] = 0x00;
    AllPages[index+31] = 0x00;

    /* MODESENSE_CONTROL_PAGE */
    /*
     * Fill-up control mode page, SAT, Table 65
     */
    AllPages[index+32] = 0x0A; /* page code */
    AllPages[index+33] = 0x0A; /* page length */
    AllPages[index+34] = 0x02; /* only GLTSD bit is set */
    if (pSatDevData->satNCQ == agTRUE)
    {
      AllPages[index+35] = 0x12; /* Queue Alogorithm modifier 1b and QErr 01b*/
    }
    else
    {
      AllPages[index+35] = 0x02; /* Queue Alogorithm modifier 0b and QErr 01b */
    }
    AllPages[index+36] = 0x00;
    AllPages[index+37] = 0x00;
    AllPages[index+38] = 0x00; /* obsolete */
    AllPages[index+39] = 0x00; /* obsolete */
    AllPages[index+40] = 0xFF; /* Busy Timeout Period */
    AllPages[index+41] = 0xFF; /* Busy Timeout Period */
    AllPages[index+42] = 0x00; /* we don't support non-000b value for the self-test code */
    AllPages[index+43] = 0x00; /* we don't support non-000b value for the self-test code */

    /* MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE */
    /*
     * Fill-up informational-exceptions control mode page, SAT, Table 68
     */
    AllPages[index+44] = 0x1C; /* page code */
    AllPages[index+45] = 0x0A; /* page length */
     if (pSatDevData->satSMARTEnabled == agTRUE)
    {
      AllPages[index+46] = 0x00;/* DEXCPT bit is NOT set */
    }
    else
    {
      AllPages[index+46] = 0x08;/* DEXCPT bit is set */
    }
    AllPages[index+47] = 0x00; /* We don't support MRIE */
    AllPages[index+48] = 0x00; /* Interval timer vendor-specific */
    AllPages[index+49] = 0x00;
    AllPages[index+50] = 0x00;
    AllPages[index+51] = 0x00;
    AllPages[index+52] = 0x00; /* REPORT-COUNT */
    AllPages[index+53] = 0x00;
    AllPages[index+54] = 0x00;
    AllPages[index+55] = 0x00;

    sm_memcpy(pModeSense, &AllPages, lenRead);
  }
  else if (page == MODESENSE_CONTROL_PAGE)
  {
    SM_DBG5(("smsatModeSense10: MODESENSE_CONTROL_PAGE\n"));
    Control[0] = 0;
    Control[1] = (bit8)(lenRead - 2);
    Control[2] = 0x00; /* medium type: default medium type (currently mounted medium type) */
    Control[3] = 0x00; /* device-specific param: no write-protect, no support for DPO-FUA */
    if (LLBAA)
    {
      Control[4] = 0x00; /* reserved and LONGLBA */
      Control[4] = (bit8)(Control[4] | 0x1); /* LONGLBA is set */
    }
    else
    {
      Control[4] = 0x00; /* reserved and LONGLBA: LONGLBA is not set */
    }
    Control[5] = 0x00; /* reserved */
    Control[6] = 0x00; /* block descriptot length */
    if (LLBAA)
    {
      Control[7] = 0x10; /* block descriptor length: LONGLBA is set. So, length is 16 */
    }
    else
    {
      Control[7] = 0x08; /* block descriptor length: LONGLBA is NOT set. So, length is 8 */
    }

    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    if (LLBAA)
    {
      /* density code */
      Control[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      Control[9]   = 0x00; /* unspecified */
      Control[10]  = 0x00; /* unspecified */
      Control[11]  = 0x00; /* unspecified */
      Control[12]  = 0x00; /* unspecified */
      Control[13]  = 0x00; /* unspecified */
      Control[14]  = 0x00; /* unspecified */
      Control[15]  = 0x00; /* unspecified */
      /* reserved */
      Control[16]  = 0x00; /* reserved */
      Control[17]  = 0x00; /* reserved */
      Control[18]  = 0x00; /* reserved */
      Control[19]  = 0x00; /* reserved */
      /* Block size */
      Control[20]  = 0x00;
      Control[21]  = 0x00;
      Control[22]  = 0x02;   /* Block size is always 512 bytes */
      Control[23]  = 0x00;
    }
    else
    {
      /* density code */
      Control[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      Control[9]   = 0x00; /* unspecified */
      Control[10]  = 0x00; /* unspecified */
      Control[11]  = 0x00; /* unspecified */
      /* reserved */
      Control[12]  = 0x00; /* reserved */
      /* Block size */
      Control[13]  = 0x00;
      Control[14]  = 0x02;   /* Block size is always 512 bytes */
      Control[15]  = 0x00;
    }

    if (LLBAA)
    {
      index = 24;
    }
    else
    {
      index = 16;
    }
    /*
     * Fill-up control mode page, SAT, Table 65
     */
    Control[index+0] = 0x0A; /* page code */
    Control[index+1] = 0x0A; /* page length */
    Control[index+2] = 0x02; /* only GLTSD bit is set */
    if (pSatDevData->satNCQ == agTRUE)
    {
      Control[index+3] = 0x12; /* Queue Alogorithm modifier 1b and QErr 01b*/
    }
    else
    {
      Control[index+3] = 0x02; /* Queue Alogorithm modifier 0b and QErr 01b */
    }
    Control[index+4] = 0x00;
    Control[index+5] = 0x00;
    Control[index+6] = 0x00; /* obsolete */
    Control[index+7] = 0x00; /* obsolete */
    Control[index+8] = 0xFF; /* Busy Timeout Period */
    Control[index+9] = 0xFF; /* Busy Timeout Period */
    Control[index+10] = 0x00; /* we don't support non-000b value for the self-test code */
    Control[index+11] = 0x00; /* we don't support non-000b value for the self-test code */

    sm_memcpy(pModeSense, &Control, lenRead);
  }
  else if (page == MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE)
  {
    SM_DBG5(("smsatModeSense10: MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE\n"));
    RWErrorRecovery[0] = 0;
    RWErrorRecovery[1] = (bit8)(lenRead - 2);
    RWErrorRecovery[2] = 0x00; /* medium type: default medium type (currently mounted medium type) */
    RWErrorRecovery[3] = 0x00; /* device-specific param: no write-protect, no support for DPO-FUA */
    if (LLBAA)
    {
      RWErrorRecovery[4] = 0x00; /* reserved and LONGLBA */
      RWErrorRecovery[4] = (bit8)(RWErrorRecovery[4] | 0x1); /* LONGLBA is set */
    }
    else
    {
      RWErrorRecovery[4] = 0x00; /* reserved and LONGLBA: LONGLBA is not set */
    }
    RWErrorRecovery[5] = 0x00; /* reserved */
    RWErrorRecovery[6] = 0x00; /* block descriptot length */
    if (LLBAA)
    {
      RWErrorRecovery[7] = 0x10; /* block descriptor length: LONGLBA is set. So, length is 16 */
    }
    else
    {
      RWErrorRecovery[7] = 0x08; /* block descriptor length: LONGLBA is NOT set. So, length is 8 */
    }

    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    if (LLBAA)
    {
      /* density code */
      RWErrorRecovery[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      RWErrorRecovery[9]   = 0x00; /* unspecified */
      RWErrorRecovery[10]  = 0x00; /* unspecified */
      RWErrorRecovery[11]  = 0x00; /* unspecified */
      RWErrorRecovery[12]  = 0x00; /* unspecified */
      RWErrorRecovery[13]  = 0x00; /* unspecified */
      RWErrorRecovery[14]  = 0x00; /* unspecified */
      RWErrorRecovery[15]  = 0x00; /* unspecified */
      /* reserved */
      RWErrorRecovery[16]  = 0x00; /* reserved */
      RWErrorRecovery[17]  = 0x00; /* reserved */
      RWErrorRecovery[18]  = 0x00; /* reserved */
      RWErrorRecovery[19]  = 0x00; /* reserved */
      /* Block size */
      RWErrorRecovery[20]  = 0x00;
      RWErrorRecovery[21]  = 0x00;
      RWErrorRecovery[22]  = 0x02;   /* Block size is always 512 bytes */
      RWErrorRecovery[23]  = 0x00;
    }
    else
    {
      /* density code */
      RWErrorRecovery[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      RWErrorRecovery[9]   = 0x00; /* unspecified */
      RWErrorRecovery[10]  = 0x00; /* unspecified */
      RWErrorRecovery[11]  = 0x00; /* unspecified */
      /* reserved */
      RWErrorRecovery[12]  = 0x00; /* reserved */
      /* Block size */
      RWErrorRecovery[13]  = 0x00;
      RWErrorRecovery[14]  = 0x02;   /* Block size is always 512 bytes */
      RWErrorRecovery[15]  = 0x00;
    }

    if (LLBAA)
    {
      index = 24;
    }
    else
    {
      index = 16;
    }
    /*
     * Fill-up Read-Write Error Recovery mode page, SAT, Table 66
     */
    RWErrorRecovery[index+0] = 0x01; /* page code */
    RWErrorRecovery[index+1] = 0x0A; /* page length */
    RWErrorRecovery[index+2] = 0x40; /* ARRE is set */
    RWErrorRecovery[index+3] = 0x00;
    RWErrorRecovery[index+4] = 0x00;
    RWErrorRecovery[index+5] = 0x00;
    RWErrorRecovery[index+6] = 0x00;
    RWErrorRecovery[index+7] = 0x00;
    RWErrorRecovery[index+8] = 0x00;
    RWErrorRecovery[index+9] = 0x00;
    RWErrorRecovery[index+10] = 0x00;
    RWErrorRecovery[index+11] = 0x00;

    sm_memcpy(pModeSense, &RWErrorRecovery, lenRead);
  }
  else if (page == MODESENSE_CACHING)
  {
    SM_DBG5(("smsatModeSense10: MODESENSE_CACHING\n"));
    Caching[0] = 0;
    Caching[1] = (bit8)(lenRead - 2);
    Caching[2] = 0x00; /* medium type: default medium type (currently mounted medium type) */
    Caching[3] = 0x00; /* device-specific param: no write-protect, no support for DPO-FUA */
    if (LLBAA)
    {
      Caching[4] = 0x00; /* reserved and LONGLBA */
      Caching[4] = (bit8)(Caching[4] | 0x1); /* LONGLBA is set */
    }
    else
    {
      Caching[4] = 0x00; /* reserved and LONGLBA: LONGLBA is not set */
    }
    Caching[5] = 0x00; /* reserved */
    Caching[6] = 0x00; /* block descriptot length */
    if (LLBAA)
    {
      Caching[7] = 0x10; /* block descriptor length: LONGLBA is set. So, length is 16 */
    }
    else
    {
      Caching[7] = 0x08; /* block descriptor length: LONGLBA is NOT set. So, length is 8 */
    }

    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    if (LLBAA)
    {
      /* density code */
      Caching[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      Caching[9]   = 0x00; /* unspecified */
      Caching[10]  = 0x00; /* unspecified */
      Caching[11]  = 0x00; /* unspecified */
      Caching[12]  = 0x00; /* unspecified */
      Caching[13]  = 0x00; /* unspecified */
      Caching[14]  = 0x00; /* unspecified */
      Caching[15]  = 0x00; /* unspecified */
      /* reserved */
      Caching[16]  = 0x00; /* reserved */
      Caching[17]  = 0x00; /* reserved */
      Caching[18]  = 0x00; /* reserved */
      Caching[19]  = 0x00; /* reserved */
      /* Block size */
      Caching[20]  = 0x00;
      Caching[21]  = 0x00;
      Caching[22]  = 0x02;   /* Block size is always 512 bytes */
      Caching[23]  = 0x00;
    }
    else
    {
      /* density code */
      Caching[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      Caching[9]   = 0x00; /* unspecified */
      Caching[10]  = 0x00; /* unspecified */
      Caching[11]  = 0x00; /* unspecified */
      /* reserved */
      Caching[12]  = 0x00; /* reserved */
      /* Block size */
      Caching[13]  = 0x00;
      Caching[14]  = 0x02;   /* Block size is always 512 bytes */
      Caching[15]  = 0x00;
    }

    if (LLBAA)
    {
      index = 24;
    }
    else
    {
      index = 16;
    }
    /*
     * Fill-up Caching mode page, SAT, Table 67
     */
    /* length 20 */
    Caching[index+0] = 0x08; /* page code */
    Caching[index+1] = 0x12; /* page length */
    if (pSatDevData->satWriteCacheEnabled == agTRUE)
    {
      Caching[index+2] = 0x04;/* WCE bit is set */
    }
    else
    {
      Caching[index+2] = 0x00;/* WCE bit is NOT set */
    }

    Caching[index+3] = 0x00;
    Caching[index+4] = 0x00;
    Caching[index+5] = 0x00;
    Caching[index+6] = 0x00;
    Caching[index+7] = 0x00;
    Caching[index+8] = 0x00;
    Caching[index+9] = 0x00;
    Caching[index+10] = 0x00;
    Caching[index+11] = 0x00;
    if (pSatDevData->satLookAheadEnabled == agTRUE)
    {
      Caching[index+12] = 0x00;/* DRA bit is NOT set */
    }
    else
    {
      Caching[index+12] = 0x20;/* DRA bit is set */
    }
    Caching[index+13] = 0x00;
    Caching[index+14] = 0x00;
    Caching[index+15] = 0x00;
    Caching[index+16] = 0x00;
    Caching[index+17] = 0x00;
    Caching[index+18] = 0x00;
    Caching[index+19] = 0x00;
    sm_memcpy(pModeSense, &Caching, lenRead);

  }
  else if (page == MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE)
  {
    SM_DBG5(("smsatModeSense10: MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE\n"));
    InfoExceptionCtrl[0] = 0;
    InfoExceptionCtrl[1] = (bit8)(lenRead - 2);
    InfoExceptionCtrl[2] = 0x00; /* medium type: default medium type (currently mounted medium type) */
    InfoExceptionCtrl[3] = 0x00; /* device-specific param: no write-protect, no support for DPO-FUA */
    if (LLBAA)
    {
      InfoExceptionCtrl[4] = 0x00; /* reserved and LONGLBA */
      InfoExceptionCtrl[4] = (bit8)(InfoExceptionCtrl[4] | 0x1); /* LONGLBA is set */
    }
    else
    {
      InfoExceptionCtrl[4] = 0x00; /* reserved and LONGLBA: LONGLBA is not set */
    }
    InfoExceptionCtrl[5] = 0x00; /* reserved */
    InfoExceptionCtrl[6] = 0x00; /* block descriptot length */
    if (LLBAA)
    {
      InfoExceptionCtrl[7] = 0x10; /* block descriptor length: LONGLBA is set. So, length is 16 */
    }
    else
    {
      InfoExceptionCtrl[7] = 0x08; /* block descriptor length: LONGLBA is NOT set. So, length is 8 */
    }

    /*
     * Fill-up direct-access device block-descriptor, SAT, Table 19
     */

    if (LLBAA)
    {
      /* density code */
      InfoExceptionCtrl[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      InfoExceptionCtrl[9]   = 0x00; /* unspecified */
      InfoExceptionCtrl[10]  = 0x00; /* unspecified */
      InfoExceptionCtrl[11]  = 0x00; /* unspecified */
      InfoExceptionCtrl[12]  = 0x00; /* unspecified */
      InfoExceptionCtrl[13]  = 0x00; /* unspecified */
      InfoExceptionCtrl[14]  = 0x00; /* unspecified */
      InfoExceptionCtrl[15]  = 0x00; /* unspecified */
      /* reserved */
      InfoExceptionCtrl[16]  = 0x00; /* reserved */
      InfoExceptionCtrl[17]  = 0x00; /* reserved */
      InfoExceptionCtrl[18]  = 0x00; /* reserved */
      InfoExceptionCtrl[19]  = 0x00; /* reserved */
      /* Block size */
      InfoExceptionCtrl[20]  = 0x00;
      InfoExceptionCtrl[21]  = 0x00;
      InfoExceptionCtrl[22]  = 0x02;   /* Block size is always 512 bytes */
      InfoExceptionCtrl[23]  = 0x00;
    }
    else
    {
      /* density code */
      InfoExceptionCtrl[8]   = 0x04; /* density-code : reserved for direct-access */
      /* number of blocks */
      InfoExceptionCtrl[9]   = 0x00; /* unspecified */
      InfoExceptionCtrl[10]  = 0x00; /* unspecified */
      InfoExceptionCtrl[11]  = 0x00; /* unspecified */
      /* reserved */
      InfoExceptionCtrl[12]  = 0x00; /* reserved */
      /* Block size */
      InfoExceptionCtrl[13]  = 0x00;
      InfoExceptionCtrl[14]  = 0x02;   /* Block size is always 512 bytes */
      InfoExceptionCtrl[15]  = 0x00;
    }

    if (LLBAA)
    {
      index = 24;
    }
    else
    {
      index = 16;
    }
    /*
     * Fill-up informational-exceptions control mode page, SAT, Table 68
     */
    InfoExceptionCtrl[index+0] = 0x1C; /* page code */
    InfoExceptionCtrl[index+1] = 0x0A; /* page length */
     if (pSatDevData->satSMARTEnabled == agTRUE)
    {
      InfoExceptionCtrl[index+2] = 0x00;/* DEXCPT bit is NOT set */
    }
    else
    {
      InfoExceptionCtrl[index+2] = 0x08;/* DEXCPT bit is set */
    }
    InfoExceptionCtrl[index+3] = 0x00; /* We don't support MRIE */
    InfoExceptionCtrl[index+4] = 0x00; /* Interval timer vendor-specific */
    InfoExceptionCtrl[index+5] = 0x00;
    InfoExceptionCtrl[index+6] = 0x00;
    InfoExceptionCtrl[index+7] = 0x00;
    InfoExceptionCtrl[index+8] = 0x00; /* REPORT-COUNT */
    InfoExceptionCtrl[index+9] = 0x00;
    InfoExceptionCtrl[index+10] = 0x00;
    InfoExceptionCtrl[index+11] = 0x00;
    sm_memcpy(pModeSense, &InfoExceptionCtrl, lenRead);

  }
  else
  {
    /* Error */
    SM_DBG1(("smsatModeSense10: Error page %d!!!\n", page));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }

  if (allocationLen > lenRead)
  {
    SM_DBG1(("smsatModeSense10: reporting underrun lenRead=0x%x allocationLen=0x%x smIORequest=%p\n", lenRead, allocationLen, smIORequest));      

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOUnderRun,
                       allocationLen - lenRead,
                       agNULL,
                       satIOContext->interruptContext );


  }
  else
  {
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
  }

  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatReadCapacity10(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   )
{
  smScsiRspSense_t        *pSense;
  smIniScsiCmnd_t         *scsiCmnd;
  bit8                    dataBuffer[8] = {0};
  bit32                   allocationLen;
  bit8  	              *pVirtAddr = agNULL;
  smDeviceData_t          *pSatDevData;
  agsaSATAIdentifyData_t  *pSATAIdData;
  bit32                   lastLba;
  bit32                   word117_118;
  bit32                   word117;
  bit32                   word118;

  pSense      = satIOContext->pSense;
  pVirtAddr   = (bit8 *) smScsiRequest->sglVirtualAddr;
  scsiCmnd    = &smScsiRequest->scsiCmnd;
  pSatDevData = satIOContext->pSatDevData;
  pSATAIdData = &pSatDevData->satIdentifyData;
  allocationLen = scsiCmnd->expDataLength;

  SM_DBG5(("smsatReadCapacity10: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatReadCapacity10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }


  /*
   * If Logical block address is not set to zero, return error
   */
  if ((scsiCmnd->cdb[2] || scsiCmnd->cdb[3] || scsiCmnd->cdb[4] || scsiCmnd->cdb[5]))
  {
    SM_DBG1(("smsatReadCapacity10: *** ERROR *** logical address non zero, did %d!!!\n",
        pSatDevData->id));

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;

  }

  /*
   * If PMI bit is not zero, return error
   */
  if ( ((scsiCmnd->cdb[8]) & SCSI_READ_CAPACITY10_PMI_MASK) != 0 )
  {
    SM_DBG1(("smsatReadCapacity10: *** ERROR *** PMI is not zero, did %d\n",
        pSatDevData->id));

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;

  }

  /*
    filling in Read Capacity parameter data
    saved identify device has been already flipped
    See ATA spec p125 and p136 and SBC spec p54
  */
  /*
   * If 48-bit addressing is supported, set capacity information from Identify
   * Device Word 100-103.
   */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    /*
     * Setting RETURNED LOGICAL BLOCK ADDRESS in READ CAPACITY(10) response data:
     * SBC-2 specifies that if the capacity exceeded the 4-byte RETURNED LOGICAL
     * BLOCK ADDRESS in READ CAPACITY(10) parameter data, the RETURNED LOGICAL
     * BLOCK ADDRESS should be set to 0xFFFFFFFF so the application client would
     * then issue a READ CAPACITY(16) command.
     */
    /* ATA Identify Device information word 100 - 103 */
    if ( (pSATAIdData->maxLBA32_47 != 0 ) || (pSATAIdData->maxLBA48_63 != 0))
    {
      dataBuffer[0] = 0xFF;        /* MSB number of block */
      dataBuffer[1] = 0xFF;
      dataBuffer[2] = 0xFF;
      dataBuffer[3] = 0xFF;        /* LSB number of block */
      SM_DBG1(("smsatReadCapacity10: returns 0xFFFFFFFF!!!\n"));
    }
    else  /* Fit the Readcapacity10 4-bytes response length */
    {
      lastLba = (((pSATAIdData->maxLBA16_31) << 16) ) |
                  (pSATAIdData->maxLBA0_15);
      lastLba = lastLba - 1;      /* LBA starts from zero */

      /*
        for testing
      lastLba = lastLba - (512*10) - 1;
      */


      dataBuffer[0] = (bit8)((lastLba >> 24) & 0xFF);    /* MSB */
      dataBuffer[1] = (bit8)((lastLba >> 16) & 0xFF);
      dataBuffer[2] = (bit8)((lastLba >> 8)  & 0xFF);
      dataBuffer[3] = (bit8)((lastLba )      & 0xFF);    /* LSB */
      
      SM_DBG3(("smsatReadCapacity10: lastLba is 0x%x %d\n", lastLba, lastLba));
      SM_DBG3(("smsatReadCapacity10: LBA 0 is 0x%x %d\n", dataBuffer[0], dataBuffer[0]));
      SM_DBG3(("smsatReadCapacity10: LBA 1 is 0x%x %d\n", dataBuffer[1], dataBuffer[1]));
      SM_DBG3(("smsatReadCapacity10: LBA 2 is 0x%x %d\n", dataBuffer[2], dataBuffer[2]));
      SM_DBG3(("smsatReadCapacity10: LBA 3 is 0x%x %d\n", dataBuffer[3], dataBuffer[3]));

    }
  }

  /*
   * For 28-bit addressing, set capacity information from Identify
   * Device Word 60-61.
   */
  else
  {
    /* ATA Identify Device information word 60 - 61 */
    lastLba = (((pSATAIdData->numOfUserAddressableSectorsHi) << 16) ) |
                (pSATAIdData->numOfUserAddressableSectorsLo);
    lastLba = lastLba - 1;      /* LBA starts from zero */

    dataBuffer[0] = (bit8)((lastLba >> 24) & 0xFF);    /* MSB */
    dataBuffer[1] = (bit8)((lastLba >> 16) & 0xFF);
    dataBuffer[2] = (bit8)((lastLba >> 8)  & 0xFF);
    dataBuffer[3] = (bit8)((lastLba )      & 0xFF);    /* LSB */  
  }
  /* SAT Rev 8d */
  if (((pSATAIdData->word104_107[2]) & 0x1000) == 0)
  {
    SM_DBG5(("smsatReadCapacity10: Default Block Length is 512\n"));
    /*
     * Set the block size, fixed at 512 bytes.
     */
    dataBuffer[4] = 0x00;        /* MSB block size in bytes */
    dataBuffer[5] = 0x00;
    dataBuffer[6] = 0x02;
    dataBuffer[7] = 0x00;        /* LSB block size in bytes */
  }
  else
  {
    word118 = pSATAIdData->word112_126[6];
    word117 = pSATAIdData->word112_126[5];

    word117_118 = (word118 << 16) + word117;
    word117_118 = word117_118 * 2;
    dataBuffer[4] = (bit8)((word117_118 >> 24) & 0xFF);        /* MSB block size in bytes */
    dataBuffer[5] = (bit8)((word117_118 >> 16) & 0xFF);
    dataBuffer[6] = (bit8)((word117_118 >> 8) & 0xFF);
    dataBuffer[7] = (bit8)(word117_118 & 0xFF);                /* LSB block size in bytes */

    SM_DBG1(("smsatReadCapacity10: Nondefault word118 %d 0x%x !!!\n", word118, word118));
    SM_DBG1(("smsatReadCapacity10: Nondefault word117 %d 0x%x !!!\n", word117, word117));
    SM_DBG1(("smsatReadCapacity10: Nondefault Block Length is %d 0x%x !!!\n",word117_118, word117_118));

  }

  /* fill in MAX LBA, which is used in satSendDiagnostic_1() */
  pSatDevData->satMaxLBA[0] = 0;            /* MSB */
  pSatDevData->satMaxLBA[1] = 0;
  pSatDevData->satMaxLBA[2] = 0;
  pSatDevData->satMaxLBA[3] = 0;
  pSatDevData->satMaxLBA[4] = dataBuffer[0]; 
  pSatDevData->satMaxLBA[5] = dataBuffer[1];
  pSatDevData->satMaxLBA[6] = dataBuffer[2];
  pSatDevData->satMaxLBA[7] = dataBuffer[3]; /* LSB */
   
  
  SM_DBG4(("smsatReadCapacity10: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x , did %d\n", 
        dataBuffer[0], dataBuffer[1], dataBuffer[2], dataBuffer[3], 
        dataBuffer[4], dataBuffer[5], dataBuffer[6], dataBuffer[7],
        pSatDevData->id));

  sm_memcpy(pVirtAddr, dataBuffer, MIN(allocationLen, 8));

  /*
   * Send the completion response now.
   */
  /*smEnqueueIO(smRoot, satIOContext);*/

  tdsmIOCompletedCB( smRoot,
                     smIORequest,
                     smIOSuccess,
                     SCSI_STAT_GOOD,
                     agNULL,
                     satIOContext->interruptContext);
  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatReadCapacity16(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   )
{
  smScsiRspSense_t        *pSense;
  smIniScsiCmnd_t         *scsiCmnd;
  bit8                    dataBuffer[32] = {0};
  bit8  	              *pVirtAddr = agNULL;  
  smDeviceData_t          *pSatDevData;
  agsaSATAIdentifyData_t  *pSATAIdData;
  bit32                   lastLbaLo;
  bit32                   allocationLen;
  bit32                   readCapacityLen  = 32;
  bit32                   i = 0;

  pSense      = satIOContext->pSense;
  pVirtAddr   = (bit8 *) smScsiRequest->sglVirtualAddr;
  scsiCmnd    = &smScsiRequest->scsiCmnd;
  pSatDevData = satIOContext->pSatDevData;
  pSATAIdData = &pSatDevData->satIdentifyData;

  SM_DBG5(("smsatReadCapacity16: start\n"));

  /* Find the buffer size allocated by Initiator */
  allocationLen = (((bit32)scsiCmnd->cdb[10]) << 24) |
                  (((bit32)scsiCmnd->cdb[11]) << 16) |
                  (((bit32)scsiCmnd->cdb[12]) << 8 ) |
                  (((bit32)scsiCmnd->cdb[13])      );
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength); 

#ifdef REMOVED
  if (allocationLen < readCapacityLen)
  {
    SM_DBG1(("smsatReadCapacity16: *** ERROR *** insufficient len=0x%x readCapacityLen=0x%x!!!\n", allocationLen, readCapacityLen));

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;

  }
#endif

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatReadCapacity16: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /*
   * If Logical blcok address is not set to zero, return error
   */
  if ((scsiCmnd->cdb[2] || scsiCmnd->cdb[3] || scsiCmnd->cdb[4] || scsiCmnd->cdb[5]) ||
      (scsiCmnd->cdb[6] || scsiCmnd->cdb[7] || scsiCmnd->cdb[8] || scsiCmnd->cdb[9])  )
  {
    SM_DBG1(("smsatReadCapacity16: *** ERROR *** logical address non zero, did %d\n",
        pSatDevData->id));

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;

  }

  /*
   * If PMI bit is not zero, return error
   */
  if ( ((scsiCmnd->cdb[14]) & SCSI_READ_CAPACITY16_PMI_MASK) != 0 )
  {
    SM_DBG1(("smsatReadCapacity16: *** ERROR *** PMI is not zero, did %d\n",
        pSatDevData->id));

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;

  }

  /*
    filling in Read Capacity parameter data
  */

  /*
   * If 48-bit addressing is supported, set capacity information from Identify
   * Device Word 100-103.
   */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    dataBuffer[0] = (bit8)(((pSATAIdData->maxLBA48_63) >> 8) & 0xff);  /* MSB */
    dataBuffer[1] = (bit8)((pSATAIdData->maxLBA48_63)        & 0xff);
    dataBuffer[2] = (bit8)(((pSATAIdData->maxLBA32_47) >> 8) & 0xff);
    dataBuffer[3] = (bit8)((pSATAIdData->maxLBA32_47)        & 0xff); 

    lastLbaLo = (((pSATAIdData->maxLBA16_31) << 16) ) | (pSATAIdData->maxLBA0_15);
    lastLbaLo = lastLbaLo - 1;      /* LBA starts from zero */

    dataBuffer[4] = (bit8)((lastLbaLo >> 24) & 0xFF);
    dataBuffer[5] = (bit8)((lastLbaLo >> 16) & 0xFF);
    dataBuffer[6] = (bit8)((lastLbaLo >> 8)  & 0xFF);
    dataBuffer[7] = (bit8)((lastLbaLo )      & 0xFF);    /* LSB */

  }

  /*
   * For 28-bit addressing, set capacity information from Identify
   * Device Word 60-61.
   */
  else
  {
    dataBuffer[0] = 0;       /* MSB */
    dataBuffer[1] = 0;
    dataBuffer[2] = 0;
    dataBuffer[3] = 0;

    lastLbaLo = (((pSATAIdData->numOfUserAddressableSectorsHi) << 16) ) |
                  (pSATAIdData->numOfUserAddressableSectorsLo);
    lastLbaLo = lastLbaLo - 1;      /* LBA starts from zero */

    dataBuffer[4] = (bit8)((lastLbaLo >> 24) & 0xFF);
    dataBuffer[5] = (bit8)((lastLbaLo >> 16) & 0xFF);
    dataBuffer[6] = (bit8)((lastLbaLo >> 8)  & 0xFF);
    dataBuffer[7] = (bit8)((lastLbaLo )      & 0xFF);    /* LSB */  

  }

  /*
   * Set the block size, fixed at 512 bytes.
   */
  dataBuffer[8]  = 0x00;        /* MSB block size in bytes */
  dataBuffer[9]  = 0x00;
  dataBuffer[10] = 0x02;
  dataBuffer[11] = 0x00;        /* LSB block size in bytes */


  /* fill in MAX LBA, which is used in satSendDiagnostic_1() */
  pSatDevData->satMaxLBA[0] = dataBuffer[0];            /* MSB */
  pSatDevData->satMaxLBA[1] = dataBuffer[1];
  pSatDevData->satMaxLBA[2] = dataBuffer[2];
  pSatDevData->satMaxLBA[3] = dataBuffer[3];  
  pSatDevData->satMaxLBA[4] = dataBuffer[4]; 
  pSatDevData->satMaxLBA[5] = dataBuffer[5];
  pSatDevData->satMaxLBA[6] = dataBuffer[6];
  pSatDevData->satMaxLBA[7] = dataBuffer[7];             /* LSB */
  
  SM_DBG5(("smsatReadCapacity16: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x , did %d\n", 
        dataBuffer[0], dataBuffer[1], dataBuffer[2], dataBuffer[3], 
        dataBuffer[4], dataBuffer[5], dataBuffer[6], dataBuffer[7],
        dataBuffer[8], dataBuffer[9], dataBuffer[10], dataBuffer[11],
        pSatDevData->id));

  if (allocationLen > 0xC) /* 0xc = 12 */
  {
    for(i=12;i<=31;i++)
    {
      dataBuffer[i] = 0x00;  
    }
  }

  sm_memcpy(pVirtAddr, dataBuffer, MIN(allocationLen, readCapacityLen));
  /*
   * Send the completion response now.
   */
  if (allocationLen > readCapacityLen)
  {
    /* underrun */
    SM_DBG1(("smsatReadCapacity16: reporting underrun readCapacityLen=0x%x allocationLen=0x%x !!!\n", readCapacityLen, allocationLen));

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOUnderRun,
                       allocationLen - readCapacityLen,
                       agNULL,
                       satIOContext->interruptContext );


  }
  else
  {
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
  }
  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatReportLun(
               smRoot_t                  *smRoot,
               smIORequest_t             *smIORequest,
               smDeviceHandle_t          *smDeviceHandle,
               smScsiInitiatorRequest_t  *smScsiRequest,
               smSatIOContext_t            *satIOContext
              )
{
  smScsiRspSense_t      *pSense;
  bit8                  dataBuffer[16] = {0};
  bit32                 allocationLen;
  bit32                 reportLunLen;
  smScsiReportLun_t     *pReportLun;
  smIniScsiCmnd_t       *scsiCmnd;
#ifdef  TD_DEBUG_ENABLE
  smDeviceData_t        *pSatDevData;
#endif

  pSense     = satIOContext->pSense;
  pReportLun = (smScsiReportLun_t *) dataBuffer;
  scsiCmnd   = &smScsiRequest->scsiCmnd;
#ifdef  TD_DEBUG_ENABLE
  pSatDevData = satIOContext->pSatDevData;
#endif
  SM_DBG5(("smsatReportLun: start\n"));
//  smhexdump("smsatReportLun: cdb", (bit8 *)scsiCmnd, 16);
  /* Find the buffer size allocated by Initiator */
  allocationLen = (((bit32)scsiCmnd->cdb[6]) << 24) |
                  (((bit32)scsiCmnd->cdb[7]) << 16) |
                  (((bit32)scsiCmnd->cdb[8]) << 8 ) |
                  (((bit32)scsiCmnd->cdb[9])      );
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength);                  
  reportLunLen  = 16;     /* 8 byte header and 8 bytes of LUN0 */
  if (allocationLen < reportLunLen)
  {
    SM_DBG1(("smsatReportLun: *** ERROR *** insufficient len=0x%x did %d\n",
        reportLunLen, pSatDevData->id));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
    /*smEnqueueIO(smRoot, satIOContext);*/
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }
  /* Set length to one entry */
  pReportLun->len[0] = 0;
  pReportLun->len[1] = 0;
  pReportLun->len[2] = 0;
  pReportLun->len[3] = sizeof (tiLUN_t);
  pReportLun->reserved = 0;
  /* Set to LUN 0:
   * - address method to 0x00: Peripheral device addressing method,
   * - bus identifier to 0
   */
  pReportLun->lunList[0].lun[0] = 0;
  pReportLun->lunList[0].lun[1] = 0;
  pReportLun->lunList[0].lun[2] = 0;
  pReportLun->lunList[0].lun[3] = 0;
  pReportLun->lunList[0].lun[4] = 0;
  pReportLun->lunList[0].lun[5] = 0;
  pReportLun->lunList[0].lun[6] = 0;
  pReportLun->lunList[0].lun[7] = 0;

  sm_memcpy(smScsiRequest->sglVirtualAddr, dataBuffer, MIN(allocationLen, reportLunLen));
  if (allocationLen > reportLunLen)
  {
    /* underrun */
    SM_DBG1(("smsatReportLun: reporting underrun reportLunLen=0x%x allocationLen=0x%x !!!\n", reportLunLen, allocationLen));

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOUnderRun,
                       allocationLen - reportLunLen,
                       agNULL,
                       satIOContext->interruptContext );


  }
  else
  {
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
  }
  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatFormatUnit(
                smRoot_t                  *smRoot,
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               )
{
  /*
    note: we don't support media certification in this version and IP bit
    satDevData->satFormatState will be agFalse since SAT does not actually sends
    any ATA command
   */

  smScsiRspSense_t        *pSense;
  smIniScsiCmnd_t         *scsiCmnd;
  bit32                    index = 0;

  pSense        = satIOContext->pSense;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  SM_DBG5(("smsatFormatUnit: start\n"));
  /*
    checking opcode
    1. FMTDATA bit == 0(no defect list header)
    2. FMTDATA bit == 1 and DCRT bit == 1(defect list header is provided
    with DCRT bit set)
  */
  if ( ((scsiCmnd->cdb[1] & SCSI_FORMAT_UNIT_FMTDATA_MASK) == 0) ||
       ((scsiCmnd->cdb[1] & SCSI_FORMAT_UNIT_FMTDATA_MASK) &&
        (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_DCRT_MASK))
       )
  {
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);

    SM_DBG1(("smsatFormatUnit: return opcode!!!\n"));
    return SM_RC_SUCCESS;
  }

  /*
    checking DEFECT LIST FORMAT and defect list length
  */
  if ( (((scsiCmnd->cdb[1] & SCSI_FORMAT_UNIT_DEFECT_LIST_FORMAT_MASK) == 0x00) ||
        ((scsiCmnd->cdb[1] & SCSI_FORMAT_UNIT_DEFECT_LIST_FORMAT_MASK) == 0x06)) )
  {
    /* short parameter header */
    if ((scsiCmnd->cdb[2] & SCSI_FORMAT_UNIT_LONGLIST_MASK) == 0x00)
    {
      index = 8;
    }
    /* long parameter header */
    if ((scsiCmnd->cdb[2] & SCSI_FORMAT_UNIT_LONGLIST_MASK) == 0x01)
    {
      index = 10;
    }
    /* defect list length */
    if ((scsiCmnd->cdb[index] != 0) || (scsiCmnd->cdb[index+1] != 0))
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatFormatUnit: return defect list format!!!\n"));
      return SM_RC_SUCCESS;
    }
  }

  if ( (scsiCmnd->cdb[1] & SCSI_FORMAT_UNIT_FMTDATA_MASK) &&
       (scsiCmnd->cdb[1] & SCSI_FORMAT_UNIT_CMPLIST_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatFormatUnit: return cmplist!!!\n"));
    return SM_RC_SUCCESS;

  }

  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatFormatUnit: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* defect list header filed, if exists, SAT rev8, Table 37, p48 */
  if (scsiCmnd->cdb[1] & SCSI_FORMAT_UNIT_FMTDATA_MASK)
  {
    /* case 1,2,3 */
    /* IMMED 1; FOV 0; FOV 1, DCRT 1, IP 0 */
    if ( (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_IMMED_MASK) ||
         ( !(scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_FOV_MASK)) ||
         ( (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_FOV_MASK) &&
           (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_DCRT_MASK) &&
           !(scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_IP_MASK))
         )
    {
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext);

      SM_DBG5(("smsatFormatUnit: return defect list case 1\n"));
      return SM_RC_SUCCESS;
    }
    /* case 4,5,6 */
    /*
        1. IMMED 0, FOV 1, DCRT 0, IP 0
        2. IMMED 0, FOV 1, DCRT 0, IP 1
        3. IMMED 0, FOV 1, DCRT 1, IP 1
      */

    if ( ( !(scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_IMMED_MASK) &&
            (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_FOV_MASK) &&
           !(scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_DCRT_MASK) &&
           !(scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_IP_MASK) )
         ||
         ( !(scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_IMMED_MASK) &&
            (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_FOV_MASK) &&
           !(scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_DCRT_MASK) &&
            (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_IP_MASK) )
         ||
         ( !(scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_IMMED_MASK) &&
            (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_FOV_MASK) &&
            (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_DCRT_MASK) &&
            (scsiCmnd->cdb[7] & SCSI_FORMAT_UNIT_IP_MASK) )
         )
    {

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG5(("smsatFormatUnit: return defect list case 2\n"));
      return SM_RC_SUCCESS;

    }
  }


  /*
   * Send the completion response now.
   */
  /*smEnqueueIO(smRoot, satIOContext);*/

  tdsmIOCompletedCB( smRoot,
                     smIORequest,
                     smIOSuccess,
                     SCSI_STAT_GOOD,
                     agNULL,
                     satIOContext->interruptContext);

  SM_DBG5(("smsatFormatUnit: return last\n"));
  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatSendDiagnostic(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     parmLen;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatSendDiagnostic: start\n"));

  /* reset satVerifyState */
  pSatDevData->satVerifyState = 0;
  /* no pending diagnostic in background */
  pSatDevData->satBGPendingDiag = agFALSE;

  /* table 27, 8.10 p39 SAT Rev8 */
  /*
    1. checking PF == 1
    2. checking DEVOFFL == 1
    3. checking UNITOFFL == 1
    4. checking PARAMETER LIST LENGTH != 0

  */
  if ( (scsiCmnd->cdb[1] & SCSI_PF_MASK) ||
       (scsiCmnd->cdb[1] & SCSI_DEVOFFL_MASK) ||
       (scsiCmnd->cdb[1] & SCSI_UNITOFFL_MASK) ||
       ( (scsiCmnd->cdb[3] != 0) || (scsiCmnd->cdb[4] != 0) )
       )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatSendDiagnostic: return PF, DEVOFFL, UNITOFFL, PARAM LIST!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatSendDiagnostic: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  parmLen = (scsiCmnd->cdb[3] << 8) + scsiCmnd->cdb[4];

  /* checking SELFTEST bit*/
  /* table 29, 8.10.3, p41 SAT Rev8 */
  /* case 1 */
  if ( !(scsiCmnd->cdb[1] & SCSI_SEND_DIAGNOSTIC_SELFTEST_MASK) &&
       (pSatDevData->satSMARTSelfTest == agFALSE)
       )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatSendDiagnostic: return Table 29 case 1!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* case 2 */
  if ( !(scsiCmnd->cdb[1] & SCSI_SEND_DIAGNOSTIC_SELFTEST_MASK) &&
       (pSatDevData->satSMARTSelfTest == agTRUE) &&
       (pSatDevData->satSMARTEnabled == agFALSE)
       )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ABORTED_COMMAND,
                          0,
                          SCSI_SNSCODE_ATA_DEVICE_FEATURE_NOT_ENABLED,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG5(("smsatSendDiagnostic: return Table 29 case 2\n"));
    return SM_RC_SUCCESS;
  }
  /*
    case 3
     see SELF TEST CODE later
  */



  /* case 4 */

  /*
    sends three ATA verify commands

  */
  if ( ((scsiCmnd->cdb[1] & SCSI_SEND_DIAGNOSTIC_SELFTEST_MASK) &&
        (pSatDevData->satSMARTSelfTest == agFALSE))
       ||
       ((scsiCmnd->cdb[1] & SCSI_SEND_DIAGNOSTIC_SELFTEST_MASK) &&
        (pSatDevData->satSMARTSelfTest == agTRUE) &&
        (pSatDevData->satSMARTEnabled == agFALSE))
       )
  {
    /*
      sector count 1, LBA 0
      sector count 1, LBA MAX
      sector count 1, LBA random
    */
    if (pSatDevData->sat48BitSupport == agTRUE)
    {
      /* sends READ VERIFY SECTOR(S) EXT*/
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
      fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.device         = 0x40;                   /* 01000000 */
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    }
    else
    {
      /* READ VERIFY SECTOR(S)*/
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_VERIFY_SECTORS;/* 0x40 */
      fis->h.features       = 0;                      /* FIS features NA       */
      fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.device         = 0x40;                   /* 01000000 */
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatSendDiagnosticCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = smsataLLIOStart( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smScsiRequest,
                              satIOContext);


    SM_DBG5(("smsatSendDiagnostic: return Table 29 case 4\n"));
    return (status);
  }
  /* case 5 */
  if ( (scsiCmnd->cdb[1] & SCSI_SEND_DIAGNOSTIC_SELFTEST_MASK) &&
       (pSatDevData->satSMARTSelfTest == agTRUE) &&
       (pSatDevData->satSMARTEnabled == agTRUE)
       )
  {
    /* sends SMART EXECUTE OFF-LINE IMMEDIATE */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_SMART;               /* 0xB0 */
    fis->h.features       = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE; /* FIS features NA       */
    fis->d.lbaLow         = 0x81;                      /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = 0x4F;                      /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = 0xC2;                      /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.device         = 0;                         /* FIS DEV is discared in SATA */
    fis->d.control        = 0;                         /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatSendDiagnosticCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = smsataLLIOStart( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smScsiRequest,
                              satIOContext);


    SM_DBG5(("smsatSendDiagnostic: return Table 29 case 5\n"));
    return (status);
  }




  /* SAT rev8 Table29 p41 case 3*/
  /* checking SELF TEST CODE*/
  if ( !(scsiCmnd->cdb[1] & SCSI_SEND_DIAGNOSTIC_SELFTEST_MASK) &&
       (pSatDevData->satSMARTSelfTest == agTRUE) &&
       (pSatDevData->satSMARTEnabled == agTRUE)
       )
  {
    /* SAT rev8 Table28 p40 */
    /* finding self-test code */
    switch ((scsiCmnd->cdb[1] & SCSI_SEND_DIAGNOSTIC_TEST_CODE_MASK) >> 5)
    {
    case 1:
      pSatDevData->satBGPendingDiag = agTRUE;

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext );
      /* sends SMART EXECUTE OFF-LINE IMMEDIATE */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_SMART;              /* 0x40 */
      fis->h.features       = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE;  /* FIS features NA       */
      fis->d.lbaLow         = 0x01;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0x4F;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0xC2;                      /* FIS LBA (23:16) */

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.device         = 0;                         /* FIS DEV is discared in SATA */
      fis->d.control        = 0;                         /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

      /* Initialize CB for SATA completion.
       */
      satIOContext->satCompleteCB = &smsatSendDiagnosticCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = smsataLLIOStart( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smScsiRequest,
                                satIOContext);


      SM_DBG5(("smsatSendDiagnostic: return Table 28 case 1\n"));
      return (status);
    case 2:
      pSatDevData->satBGPendingDiag = agTRUE;

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext );


      /* issuing SMART EXECUTE OFF-LINE IMMEDIATE */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_SMART;              /* 0x40 */
      fis->h.features       = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE; /* FIS features NA       */
      fis->d.lbaLow         = 0x02;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0x4F;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0xC2;                      /* FIS LBA (23:16) */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.device         = 0;                         /* FIS DEV is discared in SATA */
      fis->d.control        = 0;                         /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

      /* Initialize CB for SATA completion.
       */
      satIOContext->satCompleteCB = &smsatSendDiagnosticCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = smsataLLIOStart( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smScsiRequest,
                                satIOContext);


      SM_DBG5(("smsatSendDiagnostic: return Table 28 case 2\n"));
      return (status);
    case 4:
   
      if (parmLen != 0)
      {
        /* check condition */
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );

        SM_DBG1(("smsatSendDiagnostic: case 4, non zero ParmLen %d!!!\n", parmLen));
        return SM_RC_SUCCESS;
      }
      if (pSatDevData->satBGPendingDiag == agTRUE)
      {
        /* sends SMART EXECUTE OFF-LINE IMMEDIATE abort */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
        fis->h.command        = SAT_SMART;              /* 0x40 */
        fis->h.features       = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE; /* FIS features NA       */
        fis->d.lbaLow         = 0x7F;                      /* FIS LBA (7 :0 ) */
        fis->d.lbaMid         = 0x4F;                      /* FIS LBA (15:8 ) */
        fis->d.lbaHigh        = 0xC2;                      /* FIS LBA (23:16) */

        fis->d.lbaLowExp      = 0;
        fis->d.lbaMidExp      = 0;
        fis->d.lbaHighExp     = 0;
        fis->d.featuresExp    = 0;
        fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;
        fis->d.reserved4      = 0;
        fis->d.device         = 0;                         /* FIS DEV is discared in SATA */
        fis->d.control        = 0;                         /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

        /* Initialize CB for SATA completion.
         */
        satIOContext->satCompleteCB = &smsatSendDiagnosticCB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = smsataLLIOStart( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);


        SM_DBG5(("smsatSendDiagnostic: send SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE case 3\n"));
        SM_DBG5(("smsatSendDiagnostic: Table 28 case 4\n"));
        return (status);
      }
      else
      {
        /* check condition */
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );

        SM_DBG1(("smsatSendDiagnostic: case 4, no pending diagnostic in background!!!\n"));
        SM_DBG5(("smsatSendDiagnostic: Table 28 case 4\n"));
        return SM_RC_SUCCESS;
      }
      break;
    case 5:
      /* issuing SMART EXECUTE OFF-LINE IMMEDIATE */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_SMART;              /* 0x40 */
      fis->h.features       = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE; /* FIS features NA       */
      fis->d.lbaLow         = 0x81;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0x4F;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0xC2;                      /* FIS LBA (23:16) */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.device         = 0;                         /* FIS DEV is discared in SATA */
      fis->d.control        = 0;                         /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

      /* Initialize CB for SATA completion.
       */
      satIOContext->satCompleteCB = &smsatSendDiagnosticCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = smsataLLIOStart( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smScsiRequest,
                                satIOContext);


      SM_DBG5(("smsatSendDiagnostic: return Table 28 case 5\n"));
      return (status);
    case 6:
      /* issuing SMART EXECUTE OFF-LINE IMMEDIATE */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_SMART;              /* 0x40 */
      fis->h.features       = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE; /* FIS features NA       */
      fis->d.lbaLow         = 0x82;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0x4F;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0xC2;                      /* FIS LBA (23:16) */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = 0;                         /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.device         = 0;                         /* FIS DEV is discared in SATA */
      fis->d.control        = 0;                         /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

      /* Initialize CB for SATA completion.
       */
      satIOContext->satCompleteCB = &smsatSendDiagnosticCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = smsataLLIOStart( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smScsiRequest,
                                satIOContext);


      SM_DBG5(("smsatSendDiagnostic: return Table 28 case 6\n"));
      return (status);
    case 0:
    case 3: /* fall through */
    case 7: /* fall through */
    default:
      break;
    }/* switch */

    /* returns the results of default self-testing, which is good */
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext );

    SM_DBG5(("smsatSendDiagnostic: return Table 28 case 0,3,7 and default\n"));
    return SM_RC_SUCCESS;
  }


  /*smEnqueueIO(smRoot, satIOContext);*/

  tdsmIOCompletedCB( smRoot,
                     smIORequest,
                     smIOSuccess,
                     SCSI_STAT_GOOD,
                     agNULL,
                     satIOContext->interruptContext );


  SM_DBG5(("smsatSendDiagnostic: return last\n"));
  return SM_RC_SUCCESS;

}

osGLOBAL bit32
smsatStartStopUnit(
                   smRoot_t                  *smRoot,
                   smIORequest_t             *smIORequest,
                   smDeviceHandle_t          *smDeviceHandle,
                   smScsiInitiatorRequest_t  *smScsiRequest,
                   smSatIOContext_t            *satIOContext
                  )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatStartStopUnit: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatStartStopUnit: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* Spec p55, Table 48 checking START and LOEJ bit */
  /* case 1 */
  if ( !(scsiCmnd->cdb[4] & SCSI_START_MASK) && !(scsiCmnd->cdb[4] & SCSI_LOEJ_MASK) )
  {
    if ( (scsiCmnd->cdb[1] & SCSI_IMMED_MASK) )
    {
      /* immed bit , SAT rev 8, 9.11.2.1 p 54*/
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext );
      SM_DBG5(("smsatStartStopUnit: return table48 case 1-1\n"));
      return SM_RC_SUCCESS;
    }
    /* sends FLUSH CACHE or FLUSH CACHE EXT */
    if (pSatDevData->sat48BitSupport == agTRUE)
    {
      /* FLUSH CACHE EXT */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      fis->h.command        = SAT_FLUSH_CACHE_EXT;    /* 0xEA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
      fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
      fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved4      = 0;
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    }
    else
    {
      /* FLUSH CACHE */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      fis->h.command        = SAT_FLUSH_CACHE;        /* 0xE7 */
      fis->h.features       = 0;                      /* FIS features NA       */
      fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved4      = 0;
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatStartStopUnitCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = smsataLLIOStart( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smScsiRequest,
                              satIOContext);


    SM_DBG5(("smsatStartStopUnit: return table48 case 1\n"));
    return (status);
  }
  /* case 2 */
  else if ( (scsiCmnd->cdb[4] & SCSI_START_MASK) && !(scsiCmnd->cdb[4] & SCSI_LOEJ_MASK) )
  {
    /* immed bit , SAT rev 8, 9.11.2.1 p 54*/
    if ( (scsiCmnd->cdb[1] & SCSI_IMMED_MASK) )
    {
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext );

      SM_DBG5(("smsatStartStopUnit: return table48 case 2 1\n"));
      return SM_RC_SUCCESS;
    }
    /*
      sends READ_VERIFY_SECTORS(_EXT)
      sector count 1, any LBA between zero to Maximum
    */
    if (pSatDevData->sat48BitSupport == agTRUE)
    {
      /* READ VERIFY SECTOR(S) EXT*/
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = 0x01;                   /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0x00;                   /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0x00;                   /* FIS LBA (23:16) */
      fis->d.lbaLowExp      = 0x00;                   /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0x00;                   /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0x00;                   /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.device         = 0x40;                   /* 01000000 */
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

    }
    else
    {
      /* READ VERIFY SECTOR(S)*/
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      fis->h.command        = SAT_READ_VERIFY_SECTORS;/* 0x40 */
      fis->h.features       = 0;                      /* FIS features NA       */
      fis->d.lbaLow         = 0x01;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0x00;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0x00;                      /* FIS LBA (23:16) */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.device         = 0x40;                   /* 01000000 */
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

    }

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatStartStopUnitCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = smsataLLIOStart( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smScsiRequest,
                              satIOContext);

    SM_DBG5(("smsatStartStopUnit: return table48 case 2 2\n"));
    return status;
  }
  /* case 3 */
  else if ( !(scsiCmnd->cdb[4] & SCSI_START_MASK) && (scsiCmnd->cdb[4] & SCSI_LOEJ_MASK) )
  {
    if(pSatDevData->satRemovableMedia && pSatDevData->satRemovableMediaEnabled)
    {
      /* support for removal media */
      /* immed bit , SAT rev 8, 9.11.2.1 p 54*/
      if ( (scsiCmnd->cdb[1] & SCSI_IMMED_MASK) )
      {
        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satIOContext->interruptContext );

        SM_DBG5(("smsatStartStopUnit: return table48 case 3 1\n"));
        return SM_RC_SUCCESS;
      }
      /*
        sends MEDIA EJECT
      */
      /* Media Eject fis */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      fis->h.command        = SAT_MEDIA_EJECT;        /* 0xED */
      fis->h.features       = 0;                      /* FIS features NA       */
      fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      /* sector count zero */
      fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved4      = 0;
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

      /* Initialize CB for SATA completion.
       */
      satIOContext->satCompleteCB = &smsatStartStopUnitCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = smsataLLIOStart( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smScsiRequest,
                                satIOContext);

      return status;
    }
    else
    {
      /* no support for removal media */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG5(("smsatStartStopUnit: return Table 29 case 3 2\n"));
      return SM_RC_SUCCESS;
    }

  }
  /* case 4 */
  else /* ( (scsiCmnd->cdb[4] & SCSI_START_MASK) && (scsiCmnd->cdb[4] & SCSI_LOEJ_MASK) ) */
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG5(("smsatStartStopUnit: return Table 29 case 4\n"));
    return SM_RC_SUCCESS;
  }
}

osGLOBAL bit32
smsatWriteSame10(
                  smRoot_t                  *smRoot,
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 )
{
 
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatWriteSame10: start\n"));

  /* checking CONTROL */
    /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWriteSame10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }


  /* checking LBDATA and PBDATA */
  /* case 1 */
  if ( !(scsiCmnd->cdb[1] & SCSI_WRITE_SAME_LBDATA_MASK) &&
       !(scsiCmnd->cdb[1] & SCSI_WRITE_SAME_PBDATA_MASK))
  {
    SM_DBG5(("smsatWriteSame10: case 1\n"));
    /* spec 9.26.2, Table 62, p64, case 1*/
    /*
      normal case
      just like write in 9.17.1
    */

    if ( pSatDevData->sat48BitSupport != agTRUE )
    {
      /*
        writeSame10 but no support for 48 bit addressing
        -> problem in transfer length. Therefore, return check condition
      */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatWriteSame10: return internal checking!!!\n"));
      return SM_RC_SUCCESS;
    }

    /* cdb10; computing LBA and transfer length */
    lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
      + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
    tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];


    /* Table 34, 9.1, p 46 */
    /*
      note: As of 2/10/2006, no support for DMA QUEUED
    */

    /*
      Table 34, 9.1, p 46, b (footnote)
      When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
      return check condition
    */
    if (pSatDevData->satNCQ != agTRUE &&
        pSatDevData->sat48BitSupport != agTRUE
          )
    {
      if (lba > SAT_TR_LBA_LIMIT - 1) /* SAT_TR_LBA_LIMIT is 2^28, 0x10000000 */
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );

        SM_DBG1(("smsatWriteSame10: return LBA out of range!!!\n"));
        return SM_RC_SUCCESS;
      }
    }

   
    if (lba + tl <= SAT_TR_LBA_LIMIT)
    {
      if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
      {
        /* case 2 */
        /* WRITE DMA */
        /* can't fit the transfer length since WRITE DMA has 1 byte for sector count */
        SM_DBG1(("smsatWriteSame10: case 1-2 !!! error due to writesame10!!!\n"));
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
        return SM_RC_SUCCESS;
      }
      else
      {
        /* case 1 */
        /* WRITE MULTIPLE or WRITE SECTOR(S) */
        /* WRITE SECTORS is chosen for easier implemetation */
        /* can't fit the transfer length since WRITE DMA has 1 byte for sector count */
        SM_DBG1(("smsatWriteSame10: case 1-1 !!! error due to writesame10!!!\n"));
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
        return SM_RC_SUCCESS;
      }
    } /* end of case 1 and 2 */

    /* case 3 and 4 */
    if (pSatDevData->sat48BitSupport == agTRUE)
    {
      if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
      {
        /* case 3 */
        /* WRITE DMA EXT or WRITE DMA FUA EXT */
        /* WRITE DMA EXT is chosen since WRITE SAME does not have FUA bit */
        SM_DBG5(("smsatWriteSame10: case 1-3\n"));
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_WRITE_DMA_EXT;          /* 0x35 */

        fis->h.features       = 0;                      /* FIS reserve */
        fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
        fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
        fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
        fis->d.device         = 0x40;                   /* FIS LBA mode set */
        fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
        fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
        fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
        fis->d.featuresExp    = 0;                      /* FIS reserve */
        if (tl == 0)
        {
          /* error check
             ATA spec, p125, 6.17.29
             pSatDevData->satMaxUserAddrSectors should be 0x0FFFFFFF
             and allowed value is 0x0FFFFFFF - 1
          */
          if (pSatDevData->satMaxUserAddrSectors > 0x0FFFFFFF)
          {
            SM_DBG1(("smsatWriteSame10: case 3 !!! warning can't fit sectors!!!\n"));
            smsatSetSensePayload( pSense,
                                  SCSI_SNSKEY_ILLEGAL_REQUEST,
                                  0,
                                  SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                                  satIOContext);

            /*smEnqueueIO(smRoot, satIOContext);*/

            tdsmIOCompletedCB( smRoot,
                               smIORequest,
                               smIOSuccess,
                               SCSI_STAT_CHECK_CONDITION,
                               satIOContext->pSmSenseData,
                               satIOContext->interruptContext );
            return SM_RC_SUCCESS;
          }
        }
        /* one sector at a time */
        fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      }
      else
      {
        /* case 4 */
        /* WRITE MULTIPLE EXT or WRITE MULTIPLE FUA EXT or WRITE SECTOR(S) EXT */
        /* WRITE SECTORS EXT is chosen for easier implemetation */
        SM_DBG5(("smsatWriteSame10: case 1-4\n"));
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */
        fis->h.features       = 0;                      /* FIS reserve */
        fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
        fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
        fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
        fis->d.device         = 0x40;                   /* FIS LBA mode set */
        fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
        fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
        fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
        fis->d.featuresExp    = 0;                      /* FIS reserve */
        if (tl == 0)
        {
          /* error check
             ATA spec, p125, 6.17.29
             pSatDevData->satMaxUserAddrSectors should be 0x0FFFFFFF
             and allowed value is 0x0FFFFFFF - 1
          */
          if (pSatDevData->satMaxUserAddrSectors > 0x0FFFFFFF)
          {
            SM_DBG1(("smsatWriteSame10: case 4 !!! warning can't fit sectors!!!\n"));
            smsatSetSensePayload( pSense,
                                  SCSI_SNSKEY_ILLEGAL_REQUEST,
                                  0,
                                  SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                                  satIOContext);

            /*smEnqueueIO(smRoot, satIOContext);*/

            tdsmIOCompletedCB( smRoot,
                               smIORequest,
                               smIOSuccess,
                               SCSI_STAT_CHECK_CONDITION,
                               satIOContext->pSmSenseData,
                               satIOContext->interruptContext );
            return SM_RC_SUCCESS;
          }
        }
        /* one sector at a time */
        fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      }
    }

    /* case 5 */
    if (pSatDevData->satNCQ == agTRUE)
    {
      /* WRITE FPDMA QUEUED */
      if (pSatDevData->sat48BitSupport != agTRUE)
      {
        SM_DBG1(("smsatWriteSame10: case 1-5 !!! error NCQ but 28 bit address support!!!\n"));
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
        return SM_RC_SUCCESS;
      }
      SM_DBG5(("smsatWriteSame10: case 1-5\n"));

      /* Support 48-bit FPDMA addressing, use WRITE FPDMA QUEUE command */

      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */

      if (tl == 0)
      {
        /* error check
           ATA spec, p125, 6.17.29
           pSatDevData->satMaxUserAddrSectors should be 0x0FFFFFFF
           and allowed value is 0x0FFFFFFF - 1
        */
        if (pSatDevData->satMaxUserAddrSectors > 0x0FFFFFFF)
        {
          SM_DBG1(("smsatWriteSame10: case 4 !!! warning can't fit sectors!!!\n"));
          smsatSetSensePayload( pSense,
                                SCSI_SNSKEY_ILLEGAL_REQUEST,
                                0,
                                SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                                satIOContext);

          /*smEnqueueIO(smRoot, satIOContext);*/

          tdsmIOCompletedCB( smRoot,
                             smIORequest,
                             smIOSuccess,
                             SCSI_STAT_CHECK_CONDITION,
                             satIOContext->pSmSenseData,
                             satIOContext->interruptContext );
          return SM_RC_SUCCESS;
        }
      }
      /* one sector at a time */
      fis->h.features       = 1;            /* FIS sector count (7:0) */
      fis->d.featuresExp    = 0;            /* FIS sector count (15:8) */


      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* NO FUA bit in the WRITE SAME 10 */
      fis->d.device       = 0x40;                     /* FIS FUA clear */

      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    }
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatWriteSame10CB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = smsataLLIOStart( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smScsiRequest,
                              satIOContext);
    return (status);


  } /* end of case 1 */
  else if ( !(scsiCmnd->cdb[1] & SCSI_WRITE_SAME_LBDATA_MASK) &&
             (scsiCmnd->cdb[1] & SCSI_WRITE_SAME_PBDATA_MASK))
  {
    /* spec 9.26.2, Table 62, p64, case 2*/
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG5(("smsatWriteSame10: return Table 62 case 2\n"));
    return SM_RC_SUCCESS;
  }
  else if ( (scsiCmnd->cdb[1] & SCSI_WRITE_SAME_LBDATA_MASK) &&
           !(scsiCmnd->cdb[1] & SCSI_WRITE_SAME_PBDATA_MASK))
  {
    SM_DBG5(("smsatWriteSame10: Table 62 case 3\n"));
   
  }
  else /* ( (scsiCmnd->cdb[1] & SCSI_WRITE_SAME_LBDATA_MASK) &&
            (scsiCmnd->cdb[1] & SCSI_WRITE_SAME_PBDATA_MASK)) */
  {

    /* spec 9.26.2, Table 62, p64, case 4*/
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG5(("smsatWriteSame10: return Table 62 case 4\n"));
    return SM_RC_SUCCESS;
  }


  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatWriteSame16(
                  smRoot_t                  *smRoot,
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 )
{
  smScsiRspSense_t          *pSense;

  pSense        = satIOContext->pSense;

  SM_DBG5(("smsatWriteSame16: start\n"));


  smsatSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satIOContext);

  /*smEnqueueIO(smRoot, satIOContext);*/

  tdsmIOCompletedCB( smRoot,
                     smIORequest, /* == &satIntIo->satOrgSmIORequest */
                     smIOSuccess,
                     SCSI_STAT_CHECK_CONDITION,
                     satIOContext->pSmSenseData,
                     satIOContext->interruptContext );
  SM_DBG1(("smsatWriteSame16: return internal checking!!!\n"));
  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatLogSense(
              smRoot_t                  *smRoot,
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smScsiRequest,
              smSatIOContext_t            *satIOContext
             )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pLogPage;    /* Log Page data buffer */
  bit32                     flag = 0;
  bit16                     AllocLen = 0;       /* allocation length */
  bit8                      AllLogPages[8];
  bit16                     lenRead = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pLogPage      = (bit8 *) smScsiRequest->sglVirtualAddr;

  SM_DBG5(("smsatLogSense: start\n"));

  sm_memset(&AllLogPages, 0, 8);
  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatLogSense: return control!!!\n"));
    return SM_RC_SUCCESS;
  }


  AllocLen = ((scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8]);
  AllocLen = MIN(AllocLen, scsiCmnd->expDataLength);

  /* checking PC (Page Control) */
  /* nothing */

  /* special cases */
  if (AllocLen == 4)
  {
    SM_DBG1(("smsatLogSense: AllocLen is 4!!!\n"));
    switch (scsiCmnd->cdb[2] & SCSI_LOG_SENSE_PAGE_CODE_MASK)
    {
      case LOGSENSE_SUPPORTED_LOG_PAGES:
        SM_DBG5(("smsatLogSense: case LOGSENSE_SUPPORTED_LOG_PAGES\n"));

        if (pSatDevData->satSMARTFeatureSet == agTRUE)
        {
          /* add informational exception log */
          flag = 1;
          if (pSatDevData->satSMARTSelfTest == agTRUE)
          {
            /* add Self-Test results log page */
            flag = 2;
          }
        }
        else
        {
          /* only supported, no informational exception log, no  Self-Test results log page */
          flag = 0;
        }
        lenRead = 4;
        AllLogPages[0] = LOGSENSE_SUPPORTED_LOG_PAGES;          /* page code */
        AllLogPages[1] = 0;          /* reserved  */
        switch (flag)
        {
          case 0:
            /* only supported */
            AllLogPages[2] = 0;          /* page length */
            AllLogPages[3] = 1;          /* page length */
            break;
          case 1:
            /* supported and informational exception log */
            AllLogPages[2] = 0;          /* page length */
            AllLogPages[3] = 2;          /* page length */
            break;
          case 2:
            /* supported and informational exception log */
            AllLogPages[2] = 0;          /* page length */
            AllLogPages[3] = 3;          /* page length */
            break;
          default:
            SM_DBG1(("smsatLogSense: error unallowed flag value %d!!!\n", flag));
            break;
        }
        sm_memcpy(pLogPage, &AllLogPages, lenRead);
        break;
      case LOGSENSE_SELFTEST_RESULTS_PAGE:
        SM_DBG5(("smsatLogSense: case LOGSENSE_SUPPORTED_LOG_PAGES\n"));
        lenRead = 4;
        AllLogPages[0] = LOGSENSE_SELFTEST_RESULTS_PAGE;          /* page code */
        AllLogPages[1] = 0;          /* reserved  */
        /* page length = SELFTEST_RESULTS_LOG_PAGE_LENGTH - 1 - 3 = 400 = 0x190 */
        AllLogPages[2] = 0x01;
        AllLogPages[3] = 0x90;       /* page length */
        sm_memcpy(pLogPage, &AllLogPages, lenRead);

        break;
      case LOGSENSE_INFORMATION_EXCEPTIONS_PAGE:
        SM_DBG5(("smsatLogSense: case LOGSENSE_SUPPORTED_LOG_PAGES\n"));
        lenRead = 4;
        AllLogPages[0] = LOGSENSE_INFORMATION_EXCEPTIONS_PAGE;          /* page code */
        AllLogPages[1] = 0;          /* reserved  */
        AllLogPages[2] = 0;          /* page length */
        AllLogPages[3] = INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH - 1 - 3;       /* page length */
        sm_memcpy(pLogPage, &AllLogPages, lenRead);
        break;
      default:
        SM_DBG1(("smsatLogSense: default Page Code 0x%x!!!\n", scsiCmnd->cdb[2] & SCSI_LOG_SENSE_PAGE_CODE_MASK));
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
        return SM_RC_SUCCESS;
    }
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
    return SM_RC_SUCCESS;

  } /* if */

  /* SAT rev8 Table 11  p30*/
  /* checking Page Code */
  switch (scsiCmnd->cdb[2] & SCSI_LOG_SENSE_PAGE_CODE_MASK)
  {
    case LOGSENSE_SUPPORTED_LOG_PAGES:
      SM_DBG5(("smsatLogSense: case 1\n"));

      if (pSatDevData->satSMARTFeatureSet == agTRUE)
      {
        /* add informational exception log */
        flag = 1;
        if (pSatDevData->satSMARTSelfTest == agTRUE)
        {
          /* add Self-Test results log page */
          flag = 2;
        }
      }
      else
      {
        /* only supported, no informational exception log, no  Self-Test results log page */
        flag = 0;
      }
      AllLogPages[0] = 0;          /* page code */
      AllLogPages[1] = 0;          /* reserved  */
      switch (flag)
      {
      case 0:
        /* only supported */
        AllLogPages[2] = 0;          /* page length */
        AllLogPages[3] = 1;          /* page length */
        AllLogPages[4] = 0x00;       /* supported page list */
        lenRead = (bit8)(MIN(AllocLen, 5));
        break;
      case 1:
        /* supported and informational exception log */
        AllLogPages[2] = 0;          /* page length */
        AllLogPages[3] = 2;          /* page length */
        AllLogPages[4] = 0x00;       /* supported page list */
        AllLogPages[5] = 0x10;       /* supported page list */
        lenRead = (bit8)(MIN(AllocLen, 6));
        break;
      case 2:
        /* supported and informational exception log */
        AllLogPages[2] = 0;          /* page length */
        AllLogPages[3] = 3;          /* page length */
        AllLogPages[4] = 0x00;       /* supported page list */
        AllLogPages[5] = 0x10;       /* supported page list */
        AllLogPages[6] = 0x2F;       /* supported page list */
       lenRead = (bit8)(MIN(AllocLen, 7));
       break;
      default:
        SM_DBG1(("smsatLogSense: error unallowed flag value %d!!!\n", flag));
        break;
      }

      sm_memcpy(pLogPage, &AllLogPages, lenRead);
      /* comparing allocation length to Log Page byte size */
      /* SPC-4, 4.3.4.6, p28 */
      if (AllocLen > lenRead )
      {
        SM_DBG1(("smsatLogSense: reporting underrun lenRead=0x%x AllocLen=0x%x!!!\n", lenRead, AllocLen));
        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOUnderRun,
                           AllocLen - lenRead,
                           agNULL,
                           satIOContext->interruptContext );
      }
      else
      {
        /*smEnqueueIO(smRoot, satIOContext);*/
        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satIOContext->interruptContext);
      }
      break;
    case LOGSENSE_SELFTEST_RESULTS_PAGE:
      SM_DBG5(("smsatLogSense: case 2\n"));
      /* checking SMART self-test */
      if (pSatDevData->satSMARTSelfTest == agFALSE)
      {
        SM_DBG5(("smsatLogSense: case 2 no SMART Self Test\n"));
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
      }
      else
      {
        /* if satSMARTEnabled is false, send SMART_ENABLE_OPERATIONS */
        if (pSatDevData->satSMARTEnabled == agFALSE)
        {
          SM_DBG5(("smsatLogSense: case 2 calling satSMARTEnable\n"));
          status = smsatLogSenseAllocate(smRoot,
                                         smIORequest,
                                         smDeviceHandle,
                                         smScsiRequest,
                                         satIOContext,
                                         0,
                                         LOG_SENSE_0
                                         );

          return status;

        }
        else
        {
        /* SAT Rev 8, 10.2.4 p74 */
        if ( pSatDevData->sat48BitSupport == agTRUE )
        {
          SM_DBG5(("smsatLogSense: case 2-1 sends READ LOG EXT\n"));
          status = smsatLogSenseAllocate(smRoot,
                                         smIORequest,
                                         smDeviceHandle,
                                         smScsiRequest,
                                         satIOContext,
                                         512,
                                         LOG_SENSE_1
                                         );

          return status;
        }
        else
        {
          SM_DBG5(("smsatLogSense: case 2-2 sends SMART READ LOG\n"));
          status = smsatLogSenseAllocate(smRoot,
                                         smIORequest,
                                         smDeviceHandle,
                                         smScsiRequest,
                                         satIOContext,
                                         512,
                                         LOG_SENSE_2
                                         );

          return status;
        }
      }
      }
      break;
    case LOGSENSE_INFORMATION_EXCEPTIONS_PAGE:
      SM_DBG5(("smsatLogSense: case 3\n"));
      /* checking SMART feature set */
      if (pSatDevData->satSMARTFeatureSet == agFALSE)
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ILLEGAL_REQUEST,
                              0,
                              SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                              satIOContext);

        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satIOContext->pSmSenseData,
                           satIOContext->interruptContext );
      }
      else
      {
        /* checking SMART feature enabled */
        if (pSatDevData->satSMARTEnabled == agFALSE)
        {
          smsatSetSensePayload( pSense,
                                SCSI_SNSKEY_ABORTED_COMMAND,
                                0,
                                SCSI_SNSCODE_ATA_DEVICE_FEATURE_NOT_ENABLED,
                                satIOContext);

          /*smEnqueueIO(smRoot, satIOContext);*/

          tdsmIOCompletedCB( smRoot,
                             smIORequest,
                             smIOSuccess,
                             SCSI_STAT_CHECK_CONDITION,
                             satIOContext->pSmSenseData,
                             satIOContext->interruptContext );
        }
        else
        {
          /* SAT Rev 8, 10.2.3 p72 */
          SM_DBG5(("smsatLogSense: case 3 sends SMART RETURN STATUS\n"));

          /* sends SMART RETURN STATUS */
          fis->h.fisType        = 0x27;                   /* Reg host to device */
          fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

          fis->h.command        = SAT_SMART;              /* 0xB0 */
          fis->h.features       = SAT_SMART_RETURN_STATUS;/* FIS features */
          fis->d.featuresExp    = 0;                      /* FIS reserve */
          fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
          fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
          fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
          fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
          fis->d.lbaMid         = 0x4F;                   /* FIS LBA (15:8 ) */
          fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
          fis->d.lbaHigh        = 0xC2;                   /* FIS LBA (23:16) */
          fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
          fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
          fis->d.control        = 0;                      /* FIS HOB bit clear */
          fis->d.reserved4      = 0;
          fis->d.reserved5      = 0;

          agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
          /* Initialize CB for SATA completion.
           */
          satIOContext->satCompleteCB = &smsatLogSenseCB;

          /*
           * Prepare SGL and send FIS to LL layer.
           */
          satIOContext->reqType = agRequestType;       /* Save it */

          status = smsataLLIOStart( smRoot,
                                    smIORequest,
                                    smDeviceHandle,
                                    smScsiRequest,
                                    satIOContext);


          return status;
        }
      }
      break;
    default:
      SM_DBG1(("smsatLogSense: default Page Code 0x%x!!!\n", scsiCmnd->cdb[2] & SCSI_LOG_SENSE_PAGE_CODE_MASK));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      break;
  } /* end switch */

  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatLogSenseAllocate(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smSCSIRequest,
                      smSatIOContext_t            *satIOContext,
                      bit32                     payloadSize,
                      bit32                     flag
                     )
{
  smDeviceData_t            *pSatDevData;
  smIORequestBody_t         *smIORequestBody;
  smSatInternalIo_t           *satIntIo = agNULL;
  smSatIOContext_t            *satIOContext2;
  bit32                     status;

  SM_DBG5(("smsatLogSenseAllocate: start\n"));

  pSatDevData       = satIOContext->pSatDevData;

  /* create internal satIOContext */
  satIntIo = smsatAllocIntIoResource( smRoot,
                                      smIORequest, /* original request */
                                      pSatDevData,
                                      payloadSize,
                                      satIntIo);

  if (satIntIo == agNULL)
  {
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatLogSenseAllocate: fail in allocation!!!\n"));
    return SM_RC_SUCCESS;
  } /* end of memory allocation failure */

  satIntIo->satOrgSmIORequest = smIORequest;
  smIORequestBody = (smIORequestBody_t *)satIntIo->satIntRequestBody;
  satIOContext2 = &(smIORequestBody->transport.SATA.satIOContext);

  satIOContext2->pSatDevData   = pSatDevData;
  satIOContext2->pFis          = &(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satIOContext2->pScsiCmnd     = &(satIntIo->satIntSmScsiXchg.scsiCmnd);
  satIOContext2->pSense        = &(smIORequestBody->transport.SATA.sensePayload);
  satIOContext2->pSmSenseData  = &(smIORequestBody->transport.SATA.smSenseData);
  satIOContext2->pSmSenseData->senseData = satIOContext2->pSense;
  satIOContext2->smRequestBody = satIntIo->satIntRequestBody;
  satIOContext2->interruptContext = satIOContext->interruptContext;
  satIOContext2->satIntIoContext  = satIntIo;
  satIOContext2->psmDeviceHandle = smDeviceHandle;
  satIOContext2->satOrgIOContext = satIOContext;

  if (flag == LOG_SENSE_0)
  {
    /* SAT_SMART_ENABLE_OPERATIONS */
    status = smsatSMARTEnable( smRoot,
                               &(satIntIo->satIntSmIORequest),
                               smDeviceHandle,
                               &(satIntIo->satIntSmScsiXchg),
                               satIOContext2);
  }
  else if (flag == LOG_SENSE_1)
  {
    /* SAT_READ_LOG_EXT */
    status = smsatLogSense_2( smRoot,
                              &(satIntIo->satIntSmIORequest),
                              smDeviceHandle,
                              &(satIntIo->satIntSmScsiXchg),
                              satIOContext2);
  }
  else
  {
    /* SAT_SMART_READ_LOG */
    /* SAT_READ_LOG_EXT */
    status = smsatLogSense_3( smRoot,
                              &(satIntIo->satIntSmIORequest),
                              smDeviceHandle,
                              &(satIntIo->satIntSmScsiXchg),
                              satIOContext2);

  }
  if (status != SM_RC_SUCCESS)
  {
    smsatFreeIntIoResource( smRoot,
                            pSatDevData,
                            satIntIo);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }


  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatSMARTEnable(
                 smRoot_t                  *smRoot,
                 smIORequest_t             *smIORequest,
                 smDeviceHandle_t          *smDeviceHandle,
                 smScsiInitiatorRequest_t  *smScsiRequest,
                 smSatIOContext_t            *satIOContext
               )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis               = satIOContext->pFis;
  SM_DBG5(("smsatSMARTEnable: start\n"));
  /*
   * Send the SAT_SMART_ENABLE_OPERATIONS command.
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_SMART;              /* 0xB0 */
  fis->h.features       = SAT_SMART_ENABLE_OPERATIONS;
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0x4F;
  fis->d.lbaHigh        = 0xC2;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatSMARTEnableCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);


  return status;
}

osGLOBAL bit32
smsatLogSense_2(
                smRoot_t                  *smRoot,
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis               = satIOContext->pFis;
  SM_DBG5(("smsatLogSense_2: start\n"));

  /* sends READ LOG EXT */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

  fis->h.command        = SAT_READ_LOG_EXT;       /* 0x2F */
  fis->h.features       = 0;                      /* FIS reserve */
  fis->d.lbaLow         = 0x07;                   /* 0x07 */
  fis->d.lbaMid         = 0;                      /*  */
  fis->d.lbaHigh        = 0;                      /*  */
  fis->d.device         = 0;                      /*  */
  fis->d.lbaLowExp      = 0;                      /*  */
  fis->d.lbaMidExp      = 0;                      /*  */
  fis->d.lbaHighExp     = 0;                      /*  */
  fis->d.featuresExp    = 0;                      /* FIS reserve */
  fis->d.sectorCount    = 0x01;                     /* 1 sector counts */
  fis->d.sectorCountExp = 0x00;                      /* 1 sector counts */
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatLogSenseCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return status;
}

osGLOBAL bit32
smsatLogSense_3(
                smRoot_t                  *smRoot,
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis               = satIOContext->pFis;
  SM_DBG5(("smsatLogSense_3: start\n"));
  /* sends READ LOG EXT */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_SMART;              /* 0x2F */
  fis->h.features       = SAT_SMART_READ_LOG;     /* 0xd5 */
  fis->d.lbaLow         = 0x06;                   /* 0x06 */
  fis->d.lbaMid         = 0x4F;                   /* 0x4f */
  fis->d.lbaHigh        = 0xC2;                   /* 0xc2 */
  fis->d.device         = 0;                      /*  */
  fis->d.lbaLowExp      = 0;                      /*  */
  fis->d.lbaMidExp      = 0;                      /*  */
  fis->d.lbaHighExp     = 0;                      /*  */
  fis->d.featuresExp    = 0;                      /* FIS reserve */
  fis->d.sectorCount    = 0x01;                     /* 1 sector counts */
  fis->d.sectorCountExp = 0x00;                      /* 1 sector counts */
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;
  agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatLogSenseCB;
  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */
  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return status;
}


osGLOBAL bit32
smsatModeSelect6(
                 smRoot_t                  *smRoot,
                 smIORequest_t             *smIORequest,
                 smDeviceHandle_t          *smDeviceHandle,
                 smScsiInitiatorRequest_t  *smScsiRequest,
                 smSatIOContext_t            *satIOContext
                )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pLogPage;    /* Log Page data buffer */
  bit32                     StartingIndex = 0;
  bit8                      PageCode = 0;
  bit32                     chkCnd = agFALSE;
  bit32                     parameterListLen = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pLogPage      = (bit8 *) smScsiRequest->sglVirtualAddr;

  SM_DBG5(("smsatModeSelect6: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatModeSelect6: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* checking PF bit */
  if ( !(scsiCmnd->cdb[1] & SCSI_MODE_SELECT6_PF_MASK))
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatModeSelect6: PF bit check!!!\n"));
    return SM_RC_SUCCESS;
  }

  parameterListLen = scsiCmnd->cdb[4];
  parameterListLen = MIN(parameterListLen, scsiCmnd->expDataLength);
  if ((0 == parameterListLen) || (agNULL == pLogPage))
  {
    tdsmIOCompletedCB( smRoot, 
                       smIORequest, 
                       smIOSuccess, 
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
    return SM_RC_SUCCESS;
  }

  /* checking Block Descriptor Length on Mode parameter header(6)*/
  if (pLogPage[3] == 8)
  {
    /* mode parameter block descriptor exists */
    PageCode = (bit8)(pLogPage[12] & 0x3F);   /* page code and index is 4 + 8 */
    StartingIndex = 12;
  }
  else if (pLogPage[3] == 0)
  {
    /* mode parameter block descriptor does not exist */
    PageCode = (bit8)(pLogPage[4] & 0x3F); /* page code and index is 4 + 0 */
    StartingIndex = 4;
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
    return SM_RC_SUCCESS;
  }
  else
  {
    SM_DBG1(("smsatModeSelect6: return mode parameter block descriptor 0x%x!!!\n", pLogPage[3]));
   
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }



  switch (PageCode) /* page code */
  {
  case MODESELECT_CONTROL_PAGE:
    SM_DBG1(("smsatModeSelect6: Control mode page!!!\n"));
   
    if ( pLogPage[StartingIndex+1] != 0x0A ||
         pLogPage[StartingIndex+2] != 0x02 ||
         (pSatDevData->satNCQ == agTRUE && pLogPage[StartingIndex+3] != 0x12) ||
         (pSatDevData->satNCQ == agFALSE && pLogPage[StartingIndex+3] != 0x02) ||
         (pLogPage[StartingIndex+4] & BIT3_MASK) != 0x00 || /* SWP bit */
         (pLogPage[StartingIndex+4] & BIT4_MASK) != 0x00 || /* UA_INTLCK_CTRL */
         (pLogPage[StartingIndex+4] & BIT5_MASK) != 0x00 || /* UA_INTLCK_CTRL */

         (pLogPage[StartingIndex+5] & BIT0_MASK) != 0x00 || /* AUTOLOAD MODE */
         (pLogPage[StartingIndex+5] & BIT1_MASK) != 0x00 || /* AUTOLOAD MODE */
         (pLogPage[StartingIndex+5] & BIT2_MASK) != 0x00 || /* AUTOLOAD MODE */
         (pLogPage[StartingIndex+5] & BIT6_MASK) != 0x00 || /* TAS bit */

         pLogPage[StartingIndex+8] != 0xFF ||
         pLogPage[StartingIndex+9] != 0xFF ||
         pLogPage[StartingIndex+10] != 0x00 ||
         pLogPage[StartingIndex+11] != 0x00
       )
    {
      chkCnd = agTRUE;
    }
    if (chkCnd == agTRUE)
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatModeSelect6: unexpected values!!!\n"));
    }
    else
    {
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext);
    }
    return SM_RC_SUCCESS;
    break;
  case MODESELECT_READ_WRITE_ERROR_RECOVERY_PAGE:
    SM_DBG1(("smsatModeSelect6: Read-Write Error Recovery mode page!!!\n"));
   
    if ( (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_AWRE_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_RC_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_EER_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_PER_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_DTE_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_DCR_MASK) ||
         (pLogPage[StartingIndex + 10]) ||
         (pLogPage[StartingIndex + 11])
         )
    {
      SM_DBG5(("smsatModeSelect6: return check condition\n"));

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    else
    {
      SM_DBG5(("smsatModeSelect6: return GOOD \n"));
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext);
      return SM_RC_SUCCESS;
    }

    break;
  case MODESELECT_CACHING:
    /* SAT rev8 Table67, p69*/
    SM_DBG5(("smsatModeSelect6: Caching mode page\n"));
    if ( (pLogPage[StartingIndex + 2] & 0xFB) || /* 1111 1011 */
         (pLogPage[StartingIndex + 3]) ||
         (pLogPage[StartingIndex + 4]) ||
         (pLogPage[StartingIndex + 5]) ||
         (pLogPage[StartingIndex + 6]) ||
         (pLogPage[StartingIndex + 7]) ||
         (pLogPage[StartingIndex + 8]) ||
         (pLogPage[StartingIndex + 9]) ||
         (pLogPage[StartingIndex + 10]) ||
         (pLogPage[StartingIndex + 11]) ||

         (pLogPage[StartingIndex + 12] & 0xC1) || /* 1100 0001 */
         (pLogPage[StartingIndex + 13]) ||
         (pLogPage[StartingIndex + 14]) ||
         (pLogPage[StartingIndex + 15])
         )
    {
      SM_DBG1(("smsatModeSelect6: return check condition!!!\n"));

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;

    }
    else
    {
      /* sends ATA SET FEATURES based on WCE bit */
      if ( !(pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_WCE_MASK) )
      {
        SM_DBG5(("smsatModeSelect6: disable write cache\n"));
        /* sends SET FEATURES */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
        fis->h.features       = 0x82;                   /* disable write cache */
        fis->d.lbaLow         = 0;                      /* */
        fis->d.lbaMid         = 0;                      /* */
        fis->d.lbaHigh        = 0;                      /* */
        fis->d.device         = 0;                      /* */
        fis->d.lbaLowExp      = 0;                      /* */
        fis->d.lbaMidExp      = 0;                      /* */
        fis->d.lbaHighExp     = 0;                      /* */
        fis->d.featuresExp    = 0;                      /* */
        fis->d.sectorCount    = 0;                      /* */
        fis->d.sectorCountExp = 0;                      /* */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

        /* Initialize CB for SATA completion.
         */
        satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = smsataLLIOStart( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);
        return status;
      }
      else
      {
        SM_DBG5(("smsatModeSelect6: enable write cache\n"));
        /* sends SET FEATURES */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
        fis->h.features       = 0x02;                   /* enable write cache */
        fis->d.lbaLow         = 0;                      /* */
        fis->d.lbaMid         = 0;                      /* */
        fis->d.lbaHigh        = 0;                      /* */
        fis->d.device         = 0;                      /* */
        fis->d.lbaLowExp      = 0;                      /* */
        fis->d.lbaMidExp      = 0;                      /* */
        fis->d.lbaHighExp     = 0;                      /* */
        fis->d.featuresExp    = 0;                      /* */
        fis->d.sectorCount    = 0;                      /* */
        fis->d.sectorCountExp = 0;                      /* */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

        /* Initialize CB for SATA completion.
         */
        satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = smsataLLIOStart( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);
        return status;

      }
    }
    break;
  case MODESELECT_INFORMATION_EXCEPTION_CONTROL_PAGE:
    SM_DBG5(("smsatModeSelect6: Informational Exception Control mode page\n"));
  
    if ( (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_PERF_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_TEST_MASK)
         )
    {
      SM_DBG1(("smsatModeSelect6: return check condition!!! \n"));

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    else
    {
      /* sends either ATA SMART ENABLE/DISABLE OPERATIONS based on DEXCPT bit */
      if ( !(pLogPage[StartingIndex + 2] & 0x08) )
      {
        SM_DBG5(("smsatModeSelect6: enable information exceptions reporting\n"));
        /* sends SMART ENABLE OPERATIONS */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SMART;              /* 0xB0 */
        fis->h.features       = SAT_SMART_ENABLE_OPERATIONS;       /* enable */
        fis->d.lbaLow         = 0;                      /* */
        fis->d.lbaMid         = 0x4F;                   /* 0x4F */
        fis->d.lbaHigh        = 0xC2;                   /* 0xC2 */
        fis->d.device         = 0;                      /* */
        fis->d.lbaLowExp      = 0;                      /* */
        fis->d.lbaMidExp      = 0;                      /* */
        fis->d.lbaHighExp     = 0;                      /* */
        fis->d.featuresExp    = 0;                      /* */
        fis->d.sectorCount    = 0;                      /* */
        fis->d.sectorCountExp = 0;                      /* */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

        /* Initialize CB for SATA completion.
         */
        satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = smsataLLIOStart( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);
        return status;
      }
      else
      {
        SM_DBG5(("smsatModeSelect6: disable information exceptions reporting\n"));
        /* sends SMART DISABLE OPERATIONS */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SMART;              /* 0xB0 */
        fis->h.features       = SAT_SMART_DISABLE_OPERATIONS; /* disable */
        fis->d.lbaLow         = 0;                      /* */
        fis->d.lbaMid         = 0x4F;                   /* 0x4F */
        fis->d.lbaHigh        = 0xC2;                   /* 0xC2 */
        fis->d.device         = 0;                      /* */
        fis->d.lbaLowExp      = 0;                      /* */
        fis->d.lbaMidExp      = 0;                      /* */
        fis->d.lbaHighExp     = 0;                      /* */
        fis->d.featuresExp    = 0;                      /* */
        fis->d.sectorCount    = 0;                      /* */
        fis->d.sectorCountExp = 0;                      /* */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

        /* Initialize CB for SATA completion.
         */
        satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = smsataLLIOStart( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);
        return status;

      }
    }
    break;
  default:
    SM_DBG1(("smsatModeSelect6: Error unknown page code 0x%x!!!\n", pLogPage[12]));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }
}


osGLOBAL bit32
smsatModeSelect10(
                  smRoot_t                  *smRoot,
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pLogPage;    /* Log Page data buffer */
  bit16                     BlkDescLen = 0;     /* Block Descriptor Length */
  bit32                     StartingIndex = 0;
  bit8                      PageCode = 0;
  bit32                     chkCnd = agFALSE;
  bit32                     parameterListLen = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pLogPage      = (bit8 *) smScsiRequest->sglVirtualAddr;

  SM_DBG5(("smsatModeSelect10: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatModeSelect10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* checking PF bit */
  if ( !(scsiCmnd->cdb[1] & SCSI_MODE_SELECT10_PF_MASK))
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatModeSelect10: PF bit check!!!\n"));
    return SM_RC_SUCCESS;
  }

  parameterListLen = ((scsiCmnd->cdb[7]) << 8) + scsiCmnd->cdb[8];
  parameterListLen = MIN(parameterListLen, scsiCmnd->expDataLength);
  if ((0 == parameterListLen) || (agNULL == pLogPage))
  {
    tdsmIOCompletedCB( smRoot, 
                       smIORequest, 
                       smIOSuccess, 
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
    return SM_RC_SUCCESS;
  }

  BlkDescLen = (bit8)((pLogPage[6] << 8) + pLogPage[7]);

  /* checking Block Descriptor Length on Mode parameter header(10) and LONGLBA bit*/
  if ( (BlkDescLen == 8) && !(pLogPage[4] & SCSI_MODE_SELECT10_LONGLBA_MASK) )
  {
    /* mode parameter block descriptor exists and length is 8 byte */
    PageCode = (bit8)(pLogPage[16] & 0x3F);   /* page code and index is 8 + 8 */
    StartingIndex = 16;
  }
  else if ( (BlkDescLen == 16) && (pLogPage[4] & SCSI_MODE_SELECT10_LONGLBA_MASK) )
  {
    /* mode parameter block descriptor exists and length is 16 byte */
    PageCode = (bit8)(pLogPage[24] & 0x3F);   /* page code and index is 8 + 16 */
    StartingIndex = 24;
  }
  else if (BlkDescLen == 0)
  {
    PageCode = (bit8)(pLogPage[8] & 0x3F); /* page code and index is 8 + 0 */
    StartingIndex = 8;
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
    return SM_RC_SUCCESS;
  }
  else
  {
    SM_DBG1(("smsatModeSelect10: return mode parameter block descriptor 0x%x!!!\n",  BlkDescLen));
    /* no more than one mode parameter block descriptor shall be supported */
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }
  /*
    for debugging only
  */
  if (StartingIndex == 8)
  {
    smhexdump("startingindex 8", (bit8 *)pLogPage, 8);
  }
  else if(StartingIndex == 16)
  {
    if (PageCode == MODESELECT_CACHING)
    {
      smhexdump("startingindex 16", (bit8 *)pLogPage, 16+20);
    }
    else
    {
      smhexdump("startingindex 16", (bit8 *)pLogPage, 16+12);
    }
  }
  else
  {
    if (PageCode == MODESELECT_CACHING)
    {
      smhexdump("startingindex 24", (bit8 *)pLogPage, 24+20);
    }
    else
    {
      smhexdump("startingindex 24", (bit8 *)pLogPage, 24+12);
    }
  }
  switch (PageCode) /* page code */
  {
  case MODESELECT_CONTROL_PAGE:
    SM_DBG5(("smsatModeSelect10: Control mode page\n"));
    /*
      compare pLogPage to expected value (SAT Table 65, p67)
      If not match, return check condition
     */
    if ( pLogPage[StartingIndex+1] != 0x0A ||
         pLogPage[StartingIndex+2] != 0x02 ||
         (pSatDevData->satNCQ == agTRUE && pLogPage[StartingIndex+3] != 0x12) ||
         (pSatDevData->satNCQ == agFALSE && pLogPage[StartingIndex+3] != 0x02) ||
         (pLogPage[StartingIndex+4] & BIT3_MASK) != 0x00 || /* SWP bit */
         (pLogPage[StartingIndex+4] & BIT4_MASK) != 0x00 || /* UA_INTLCK_CTRL */
         (pLogPage[StartingIndex+4] & BIT5_MASK) != 0x00 || /* UA_INTLCK_CTRL */

         (pLogPage[StartingIndex+5] & BIT0_MASK) != 0x00 || /* AUTOLOAD MODE */
         (pLogPage[StartingIndex+5] & BIT1_MASK) != 0x00 || /* AUTOLOAD MODE */
         (pLogPage[StartingIndex+5] & BIT2_MASK) != 0x00 || /* AUTOLOAD MODE */
         (pLogPage[StartingIndex+5] & BIT6_MASK) != 0x00 || /* TAS bit */

         pLogPage[StartingIndex+8] != 0xFF ||
         pLogPage[StartingIndex+9] != 0xFF ||
         pLogPage[StartingIndex+10] != 0x00 ||
         pLogPage[StartingIndex+11] != 0x00
       )
    {
      chkCnd = agTRUE;
    }
    if (chkCnd == agTRUE)
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatModeSelect10: unexpected values!!!\n"));
    }
    else
    {
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext);
    }
    return SM_RC_SUCCESS;
    break;
  case MODESELECT_READ_WRITE_ERROR_RECOVERY_PAGE:
    SM_DBG5(("smsatModeSelect10: Read-Write Error Recovery mode page\n"));
  
    if ( (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_AWRE_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_RC_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_EER_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_PER_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_DTE_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_DCR_MASK) ||
         (pLogPage[StartingIndex + 10]) ||
         (pLogPage[StartingIndex + 11])
         )
    {
      SM_DBG1(("smsatModeSelect10: return check condition!!!\n"));

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    else
    {
      SM_DBG2(("smsatModeSelect10: return GOOD \n"));
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext);
      return SM_RC_SUCCESS;
    }

    break;
  case MODESELECT_CACHING:
    /* SAT rev8 Table67, p69*/
    SM_DBG5(("smsatModeSelect10: Caching mode page\n"));
    if ( (pLogPage[StartingIndex + 2] & 0xFB) || /* 1111 1011 */
         (pLogPage[StartingIndex + 3]) ||
         (pLogPage[StartingIndex + 4]) ||
         (pLogPage[StartingIndex + 5]) ||
         (pLogPage[StartingIndex + 6]) ||
         (pLogPage[StartingIndex + 7]) ||
         (pLogPage[StartingIndex + 8]) ||
         (pLogPage[StartingIndex + 9]) ||
         (pLogPage[StartingIndex + 10]) ||
         (pLogPage[StartingIndex + 11]) ||

         (pLogPage[StartingIndex + 12] & 0xC1) || /* 1100 0001 */
         (pLogPage[StartingIndex + 13]) ||
         (pLogPage[StartingIndex + 14]) ||
         (pLogPage[StartingIndex + 15])
         )
    {
      SM_DBG1(("smsatModeSelect10: return check condition!!!\n"));

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;

    }
    else
    {
      /* sends ATA SET FEATURES based on WCE bit */
      if ( !(pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_WCE_MASK) )
      {
        SM_DBG5(("smsatModeSelect10: disable write cache\n"));
        /* sends SET FEATURES */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
        fis->h.features       = 0x82;                   /* disable write cache */
        fis->d.lbaLow         = 0;                      /* */
        fis->d.lbaMid         = 0;                      /* */
        fis->d.lbaHigh        = 0;                      /* */
        fis->d.device         = 0;                      /* */
        fis->d.lbaLowExp      = 0;                      /* */
        fis->d.lbaMidExp      = 0;                      /* */
        fis->d.lbaHighExp     = 0;                      /* */
        fis->d.featuresExp    = 0;                      /* */
        fis->d.sectorCount    = 0;                      /* */
        fis->d.sectorCountExp = 0;                      /* */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

        /* Initialize CB for SATA completion.
         */
        satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = smsataLLIOStart( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);
        return status;
      }
      else
      {
        SM_DBG5(("smsatModeSelect10: enable write cache\n"));
        /* sends SET FEATURES */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
        fis->h.features       = 0x02;                   /* enable write cache */
        fis->d.lbaLow         = 0;                      /* */
        fis->d.lbaMid         = 0;                      /* */
        fis->d.lbaHigh        = 0;                      /* */
        fis->d.device         = 0;                      /* */
        fis->d.lbaLowExp      = 0;                      /* */
        fis->d.lbaMidExp      = 0;                      /* */
        fis->d.lbaHighExp     = 0;                      /* */
        fis->d.featuresExp    = 0;                      /* */
        fis->d.sectorCount    = 0;                      /* */
        fis->d.sectorCountExp = 0;                      /* */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

        /* Initialize CB for SATA completion.
         */
        satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = smsataLLIOStart( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);
        return status;

      }
    }
    break;
  case MODESELECT_INFORMATION_EXCEPTION_CONTROL_PAGE:
    SM_DBG5(("smsatModeSelect10: Informational Exception Control mode page\n"));
   
    if ( (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_PERF_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_TEST_MASK)
         )
    {
      SM_DBG1(("smsatModeSelect10: return check condition!!!\n"));

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    else
    {
      /* sends either ATA SMART ENABLE/DISABLE OPERATIONS based on DEXCPT bit */
      if ( !(pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_DEXCPT_MASK) )
      {
        SM_DBG5(("smsatModeSelect10: enable information exceptions reporting\n"));
        /* sends SMART ENABLE OPERATIONS */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SMART;              /* 0xB0 */
        fis->h.features       = SAT_SMART_ENABLE_OPERATIONS;       /* enable */
        fis->d.lbaLow         = 0;                      /* */
        fis->d.lbaMid         = 0x4F;                   /* 0x4F */
        fis->d.lbaHigh        = 0xC2;                   /* 0xC2 */
        fis->d.device         = 0;                      /* */
        fis->d.lbaLowExp      = 0;                      /* */
        fis->d.lbaMidExp      = 0;                      /* */
        fis->d.lbaHighExp     = 0;                      /* */
        fis->d.featuresExp    = 0;                      /* */
        fis->d.sectorCount    = 0;                      /* */
        fis->d.sectorCountExp = 0;                      /* */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

        /* Initialize CB for SATA completion.
         */
        satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = smsataLLIOStart( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);
        return status;
      }
      else
      {
        SM_DBG5(("smsatModeSelect10: disable information exceptions reporting\n"));
        /* sends SMART DISABLE OPERATIONS */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SMART;              /* 0xB0 */
        fis->h.features       = SAT_SMART_DISABLE_OPERATIONS; /* disable */
        fis->d.lbaLow         = 0;                      /* */
        fis->d.lbaMid         = 0x4F;                   /* 0x4F */
        fis->d.lbaHigh        = 0xC2;                   /* 0xC2 */
        fis->d.device         = 0;                      /* */
        fis->d.lbaLowExp      = 0;                      /* */
        fis->d.lbaMidExp      = 0;                      /* */
        fis->d.lbaHighExp     = 0;                      /* */
        fis->d.featuresExp    = 0;                      /* */
        fis->d.sectorCount    = 0;                      /* */
        fis->d.sectorCountExp = 0;                      /* */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

        /* Initialize CB for SATA completion.
         */
        satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = smsataLLIOStart( smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smScsiRequest,
                                  satIOContext);
        return status;

      }
    }
    break;
  default:
    SM_DBG1(("smsatModeSelect10: Error unknown page code 0x%x!!!\n", pLogPage[12]));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );
    return SM_RC_SUCCESS;
  }
}

osGLOBAL bit32
smsatSynchronizeCache10(
                        smRoot_t                  *smRoot,
                        smIORequest_t             *smIORequest,
                        smDeviceHandle_t          *smDeviceHandle,
                        smScsiInitiatorRequest_t  *smScsiRequest,
                        smSatIOContext_t            *satIOContext
                       )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatSynchronizeCache10: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatSynchronizeCache10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* checking IMMED bit */
  if (scsiCmnd->cdb[1] & SCSI_SYNC_CACHE_IMMED_MASK)
  {
    SM_DBG1(("smsatSynchronizeCache10: GOOD status due to IMMED bit!!!\n"));

    /* return GOOD status first here */
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
  }

  /* sends FLUSH CACHE or FLUSH CACHE EXT */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    SM_DBG5(("smsatSynchronizeCache10: sends FLUSH CACHE EXT\n"));
    /* FLUSH CACHE EXT */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_FLUSH_CACHE_EXT;    /* 0xEA */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
    fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
    fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
    fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved4      = 0;
    fis->d.reserved5      = 0;

  }
  else
  {
    SM_DBG5(("smsatSynchronizeCache10: sends FLUSH CACHE\n"));
    /* FLUSH CACHE */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_FLUSH_CACHE;        /* 0xE7 */
    fis->h.features       = 0;                      /* FIS features NA       */
    fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved4      = 0;
    fis->d.reserved5      = 0;

  }

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatSynchronizeCache10n16CB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);


  return (status);
}

osGLOBAL bit32
smsatSynchronizeCache16(
                        smRoot_t                  *smRoot,
                        smIORequest_t             *smIORequest,
                        smDeviceHandle_t          *smDeviceHandle,
                        smScsiInitiatorRequest_t  *smScsiRequest,
                        smSatIOContext_t            *satIOContext
                       )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatSynchronizeCache10: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatSynchronizeCache10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }


  /* checking IMMED bit */
  if (scsiCmnd->cdb[1] & SCSI_SYNC_CACHE_IMMED_MASK)
  {
    SM_DBG1(("smsatSynchronizeCache10: GOOD status due to IMMED bit!!!\n"));

    /* return GOOD status first here */
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
  }

  /* sends FLUSH CACHE or FLUSH CACHE EXT */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    SM_DBG5(("smsatSynchronizeCache10: sends FLUSH CACHE EXT\n"));
    /* FLUSH CACHE EXT */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_FLUSH_CACHE_EXT;    /* 0xEA */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
    fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
    fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
    fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved4      = 0;
    fis->d.reserved5      = 0;

  }
  else
  {
    SM_DBG5(("smsatSynchronizeCache10: sends FLUSH CACHE\n"));
    /* FLUSH CACHE */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_FLUSH_CACHE;        /* 0xE7 */
    fis->h.features       = 0;                      /* FIS features NA       */
    fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.device         = 0;                      /* FIS DEV is discared in SATA */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved4      = 0;
    fis->d.reserved5      = 0;

  }

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatSynchronizeCache10n16CB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);


  return (status);
}

osGLOBAL bit32
smsatWriteAndVerify10(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
                     )
{
  /*
    combination of write10 and verify10
  */

  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatWriteAndVerify10: start\n"));

  /* checking BYTCHK bit */
  if (scsiCmnd->cdb[1] & SCSI_WRITE_N_VERIFY_BYTCHK_MASK)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWriteAndVerify10: BYTCHK bit checking!!!\n"));
    return SM_RC_SUCCESS;
  }


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWriteAndVerify10: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = 0;                  /* MSB */
  LBA[1] = 0;
  LBA[2] = 0;
  LBA[3] = 0;  
  LBA[4] = scsiCmnd->cdb[2];  
  LBA[5] = scsiCmnd->cdb[3];
  LBA[6] = scsiCmnd->cdb[4];
  LBA[7] = scsiCmnd->cdb[5];   /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = 0;  
  TL[3] = 0;
  TL[4] = 0;			
  TL[5] = 0;
  TL[6] = scsiCmnd->cdb[7];  
  TL[7] = scsiCmnd->cdb[8];    /* LSB */


  /* cbd10; computing LBA and transfer length */
  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];


  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
   */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatWriteAndVerify10: return LBA out of range!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatWriteAndVerify10: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      return SM_RC_SUCCESS;
    }
  }


  /* case 1 and 2 */
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* can't fit the transfer length */
      SM_DBG5(("smsatWriteAndVerify10: case 2\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA;
    }
    else
    {
      /* case 1 */
      /* WRITE MULTIPLE or WRITE SECTOR(S) */
      /* WRITE SECTORS for easier implemetation */
      /* can't fit the transfer length */
      SM_DBG5(("smsatWriteAndVerify10: case 1\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS;

  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      SM_DBG5(("smsatWriteAndVerify10: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* SAT_WRITE_DMA_FUA_EXT is optional and we don't support it */
      fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x35 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA_EXT;
    }
    else
    {
      /* case 4 */
      /* WRITE MULTIPLE EXT or WRITE MULTIPLE FUA EXT or WRITE SECTOR(S) EXT */
      /* WRITE SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatWriteAndVerify10: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS_EXT;
    }
  }
  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* WRITE FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      SM_DBG1(("smsatWriteAndVerify10: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    SM_DBG5(("smsatWriteAndVerify10: case 5\n"));

    /* Support 48-bit FPDMA addressing, use WRITE FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->h.features       = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_WRITE_N_VERIFY10_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    satIOContext->ATACmd = SAT_WRITE_FPDMA_QUEUED;
  }

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUED */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    SM_DBG5(("smsatWriteAndVerify10: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedWriteNVerifyCB;
  }
  else
  {
    SM_DBG1(("smsatWriteAndVerify10: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
             fis->h.command == SAT_WRITE_DMA_EXT ||
             fis->h.command == SAT_WRITE_DMA_FUA_EXT
             )
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      /* SAT_WRITE_FPDMA_QUEUED */
      fis->h.features       = 0xFF;
      fis->d.featuresExp    = 0xFF;
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedWriteNVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);

}

osGLOBAL bit32
smsatWriteAndVerify12(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
                     )
{
  /*
    combination of write12 and verify12
    temp: since write12 is not support (due to internal checking), no support
  */
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatWriteAndVerify12: start\n"));

  /* checking BYTCHK bit */
  if (scsiCmnd->cdb[1] & SCSI_WRITE_N_VERIFY_BYTCHK_MASK)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWriteAndVerify12: BYTCHK bit checking!!!\n"));
    return SM_RC_SUCCESS;
  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWriteAndVerify12: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = 0;                  /* MSB */
  LBA[1] = 0;
  LBA[2] = 0;
  LBA[3] = 0;
  LBA[4] = scsiCmnd->cdb[2];  
  LBA[5] = scsiCmnd->cdb[3];
  LBA[6] = scsiCmnd->cdb[4];
  LBA[7] = scsiCmnd->cdb[5];   /* LSB */

  TL[0] = 0;                   /* MSB */
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;   
  TL[4] = scsiCmnd->cdb[6];   
  TL[5] = scsiCmnd->cdb[7];
  TL[6] = scsiCmnd->cdb[8];
  TL[7] = scsiCmnd->cdb[9];    /* LSB */


  lba = smsatComputeCDB12LBA(satIOContext);
  tl = smsatComputeCDB12TL(satIOContext);


  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
   */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);    
    if (AllChk)
    {

      /*smEnqueueIO(smRoot, satIOContext);*/


      SM_DBG1(("smsatWriteAndVerify12: return LBA out of range, not EXT!!!\n"));

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

    return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);    
    if (AllChk)
  {
      SM_DBG1(("smsatWriteAndVerify12: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);
      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
    return SM_RC_SUCCESS;
    }
  }
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* In case that we can't fit the transfer length, we loop */
      SM_DBG5(("smsatWriteAndVerify12: case 2\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA;
    }
    else
    {
      /* case 1 */
      /* WRITE MULTIPLE or WRITE SECTOR(S) */
      /* WRITE SECTORS for easier implemetation */
      /* In case that we can't fit the transfer length, we loop */
      SM_DBG5(("smsatWriteAndVerify12: case 1\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS;
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      SM_DBG5(("smsatWriteAndVerify12: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* SAT_WRITE_DMA_FUA_EXT is optional and we don't support it */
      fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x35 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA_EXT;
    }
    else
    {
      /* case 4 */
      /* WRITE MULTIPLE EXT or WRITE MULTIPLE FUA EXT or WRITE SECTOR(S) EXT */
      /* WRITE SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatWriteAndVerify12: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS_EXT;
    }
  }

  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* WRITE FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      SM_DBG1(("smsatWriteAndVerify12: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    SM_DBG6(("smsatWriteAndVerify12: case 5\n"));

    /* Support 48-bit FPDMA addressing, use WRITE FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->h.features       = scsiCmnd->cdb[9];       /* FIS sector count (7:0) */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_WRITE12_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = scsiCmnd->cdb[8];       /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    satIOContext->ATACmd = SAT_WRITE_FPDMA_QUEUED;
  }

  satIOContext->currentLBA = lba;
//  satIOContext->OrgLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;
  satIOContext->LoopNum2 = LoopNum;


  if (LoopNum == 1)
  {
    SM_DBG5(("smsatWriteAndVerify12: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedWriteNVerifyCB;
  }
  else
  {
    SM_DBG1(("smsatWriteAndVerify12: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
             fis->h.command == SAT_WRITE_DMA_EXT ||
             fis->h.command == SAT_WRITE_DMA_FUA_EXT
             )
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      /* SAT_WRITE_FPDMA_QUEUED */
      fis->h.features       = 0xFF;
      fis->d.featuresExp    = 0xFF;
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedWriteNVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);
}

osGLOBAL bit32
smsatWriteAndVerify16(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
                     )
{
  /*
    combination of write16 and verify16
    since write16 has 8 bytes LBA -> problem ATA LBA(upto 6 bytes), no support
  */
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     AllChk = agFALSE; /* lba, lba+tl check against ATA limit and Disk capacity */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatWriteAndVerify16: start\n"));

  /* checking BYTCHK bit */
  if (scsiCmnd->cdb[1] & SCSI_WRITE_N_VERIFY_BYTCHK_MASK)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWriteAndVerify16: BYTCHK bit checking!!!\n"));
    return SM_RC_SUCCESS;
  }


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWriteAndVerify16: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));


  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];
  LBA[4] = scsiCmnd->cdb[6];
  LBA[5] = scsiCmnd->cdb[7];
  LBA[6] = scsiCmnd->cdb[8];
  LBA[7] = scsiCmnd->cdb[9];  /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = 0;
  TL[3] = 0;
  TL[4] = scsiCmnd->cdb[10];   /* MSB */
  TL[5] = scsiCmnd->cdb[11];
  TL[6] = scsiCmnd->cdb[12];
  TL[7] = scsiCmnd->cdb[13];   /* LSB */



  lba = smsatComputeCDB16LBA(satIOContext);
  tl = smsatComputeCDB16TL(satIOContext);


  /* Table 34, 9.1, p 46 */
  /*
    note: As of 2/10/2006, no support for DMA QUEUED
  */

  /*
    Table 34, 9.1, p 46, b
    When no 48-bit addressing support or NCQ, if LBA is beyond (2^28 - 1),
    return check condition
  */
  if (pSatDevData->satNCQ != agTRUE &&
     pSatDevData->sat48BitSupport != agTRUE
     )
  {
    AllChk = smsatCheckLimit(LBA, TL, agFALSE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatWriteAndVerify16: return LBA out of range, not EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      return SM_RC_SUCCESS;
    }
  }
  else
  {
    AllChk = smsatCheckLimit(LBA, TL, agTRUE, pSatDevData);
    if (AllChk)
    {
      SM_DBG1(("smsatWriteAndVerify16: return LBA out of range, EXT!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      return SM_RC_SUCCESS;
    }
  }


  /* case 1 and 2 */
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* In case that we can't fit the transfer length, we loop */
      SM_DBG5(("smsatWriteAndVerify16: case 2\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[6] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA;
    }
    else
    {
      /* case 1 */
      /* WRITE MULTIPLE or WRITE SECTOR(S) */
      /* WRITE SECTORS for easier implemetation */
      /* In case that we can't fit the transfer length, we loop */
      SM_DBG5(("smsatWriteAndVerify16: case 1\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[6] & 0xF));

      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS;
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      SM_DBG5(("smsatWriteAndVerify16: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* SAT_WRITE_DMA_FUA_EXT is optional and we don't support it */
      fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x35 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[12];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
      satIOContext->ATACmd = SAT_WRITE_DMA_EXT;
    }
    else
    {
      /* case 4 */
      /* WRITE MULTIPLE EXT or WRITE MULTIPLE FUA EXT or WRITE SECTOR(S) EXT */
      /* WRITE SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatWriteAndVerify16: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = scsiCmnd->cdb[12];       /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS_EXT;
    }
  }

  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* WRITE FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      SM_DBG1(("smsatWriteAndVerify16: case 5 !!! error NCQ but 28 bit address support!!!\n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    SM_DBG6(("smsatWriteAndVerify16: case 5\n"));

    /* Support 48-bit FPDMA addressing, use WRITE FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->h.features       = scsiCmnd->cdb[13];       /* FIS sector count (7:0) */
    fis->d.lbaLow         = scsiCmnd->cdb[9];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[8];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[7];       /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_WRITE16_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = scsiCmnd->cdb[6];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = scsiCmnd->cdb[5];       /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = scsiCmnd->cdb[4];       /* FIS LBA (47:40) */
    fis->d.featuresExp    = scsiCmnd->cdb[12];       /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    satIOContext->ATACmd = SAT_WRITE_FPDMA_QUEUED;
  }

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    SM_DBG5(("smsatWriteAndVerify16: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedWriteNVerifyCB;
  }
  else
  {
    SM_DBG1(("smsatWriteAndVerify16: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
             fis->h.command == SAT_WRITE_DMA_EXT ||
             fis->h.command == SAT_WRITE_DMA_FUA_EXT
             )
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      /* SAT_WRITE_FPDMA_QUEUED */
      fis->h.features       = 0xFF;
      fis->d.featuresExp    = 0xFF;
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedWriteNVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);
}

osGLOBAL bit32
smsatReadMediaSerialNumber(
                           smRoot_t                  *smRoot,
                           smIORequest_t             *smIORequest,
                           smDeviceHandle_t          *smDeviceHandle,
                           smScsiInitiatorRequest_t  *smScsiRequest,
                           smSatIOContext_t            *satIOContext
                          )
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  agsaSATAIdentifyData_t    *pSATAIdData;
  bit8                      *pSerialNumber;
  bit8                      MediaSerialNumber[64] = {0};
  bit32                     allocationLen = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pSATAIdData   = &(pSatDevData->satIdentifyData);
  pSerialNumber = (bit8 *) smScsiRequest->sglVirtualAddr;

  SM_DBG5(("smsatReadMediaSerialNumber: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatReadMediaSerialNumber: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  allocationLen = (((bit32)scsiCmnd->cdb[6]) << 24) |
                  (((bit32)scsiCmnd->cdb[7]) << 16) |
                  (((bit32)scsiCmnd->cdb[8]) << 8 ) |
                  (((bit32)scsiCmnd->cdb[9]));
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength);
  if (allocationLen == 4)
  {
    if (pSATAIdData->commandSetFeatureDefault & 0x4)
    {
      SM_DBG1(("smsatReadMediaSerialNumber: Media serial number returning only length!!!\n"));
      /* SPC-3 6.16 p192; filling in length */
      MediaSerialNumber[0] = 0;
      MediaSerialNumber[1] = 0;
      MediaSerialNumber[2] = 0;
      MediaSerialNumber[3] = 0x3C;
    }
    else
    {
      /* 1 sector - 4 = 512 - 4 to avoid underflow; 0x1fc*/
      MediaSerialNumber[0] = 0;
      MediaSerialNumber[1] = 0;
      MediaSerialNumber[2] = 0x1;
      MediaSerialNumber[3] = 0xfc;
    }

    sm_memcpy(pSerialNumber, MediaSerialNumber, 4);
    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);

    return SM_RC_SUCCESS;
  }

  if ( pSatDevData->IDDeviceValid == agTRUE)
  {
    if (pSATAIdData->commandSetFeatureDefault & 0x4)
    {
      /* word87 bit2 Media serial number is valid */
      /* read word 176 to 205; length is 2*30 = 60 = 0x3C*/
#ifdef LOG_ENABLE
      smhexdump("ID smsatReadMediaSerialNumber", (bit8*)pSATAIdData->currentMediaSerialNumber, 2*30);
#endif
      /* SPC-3 6.16 p192; filling in length */
      MediaSerialNumber[0] = 0;
      MediaSerialNumber[1] = 0;
      MediaSerialNumber[2] = 0;
      MediaSerialNumber[3] = 0x3C;
      sm_memcpy(&MediaSerialNumber[4], (void *)pSATAIdData->currentMediaSerialNumber, 60);
#ifdef LOG_ENABLE
      smhexdump("smsatReadMediaSerialNumber", (bit8*)MediaSerialNumber, 2*30 + 4);
#endif
      sm_memcpy(pSerialNumber, MediaSerialNumber, MIN(allocationLen, 64));
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext);
      return SM_RC_SUCCESS;


    }
    else
    {
     /* word87 bit2 Media serial number is NOT valid */
      SM_DBG1(("smsatReadMediaSerialNumber: Media serial number is NOT valid!!!\n"));

      if (pSatDevData->sat48BitSupport == agTRUE)
      {
        /* READ VERIFY SECTORS EXT */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
        fis->h.command        = SAT_READ_SECTORS_EXT;      /* 0x24 */

        fis->h.features       = 0;                      /* FIS reserve */
        fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
        fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
        fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
        fis->d.device         = 0x40;                   /* FIS LBA mode set */
        fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
        fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
        fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
        fis->d.featuresExp    = 0;                      /* FIS reserve */
        fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;

        agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
      }
      else
      {
        /* READ VERIFY SECTORS */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
        fis->h.command        = SAT_READ_SECTORS;       /* 0x20 */
        fis->h.features       = 0;                      /* FIS reserve */
        fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
        fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
        fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
        fis->d.device         = 0x40;                   /* FIS LBA (27:24) and FIS LBA mode  */
        fis->d.lbaLowExp      = 0;
        fis->d.lbaMidExp      = 0;
        fis->d.lbaHighExp     = 0;
        fis->d.featuresExp    = 0;
        fis->d.sectorCount    = 1;                       /* FIS sector count (7:0) */
        fis->d.sectorCountExp = 0;
        fis->d.reserved4      = 0;
        fis->d.control        = 0;                      /* FIS HOB bit clear */
        fis->d.reserved5      = 0;


        agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
      }
      satIOContext->satCompleteCB = &smsatReadMediaSerialNumberCB;
      satIOContext->reqType = agRequestType;       /* Save it */
      status = smsataLLIOStart( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smScsiRequest,
                                satIOContext);

      return status;
    }
  }
  else
  {

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satIOContext->interruptContext);

    return SM_RC_SUCCESS;

  }
}

osGLOBAL bit32
smsatReadBuffer(
                smRoot_t                  *smRoot,
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               )
{
  bit32                      status = SM_RC_SUCCESS;
  bit32                      agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                      bufferOffset;
  bit32                      tl;
  bit8                       mode;
  bit8                       bufferID;
  bit8                      *pBuff;

  pSense        = satIOContext->pSense;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pBuff         = (bit8 *) smScsiRequest->sglVirtualAddr;

  SM_DBG5(("smsatReadBuffer: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatReadBuffer: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  bufferOffset = (scsiCmnd->cdb[3] << (8*2)) + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[6] << (8*2)) + (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

  mode = (bit8)(scsiCmnd->cdb[1] & SCSI_READ_BUFFER_MODE_MASK);
  bufferID = scsiCmnd->cdb[2];

  if (mode == READ_BUFFER_DATA_MODE) /* 2 */
  {
    if (bufferID == 0 && bufferOffset == 0 && tl == 512)
    {
      /* send ATA READ BUFFER */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_READ_BUFFER;        /* 0xE4 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA (27:24) and FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;


      agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;

      satIOContext->satCompleteCB = &smsatReadBufferCB;

      satIOContext->reqType = agRequestType;       /* Save it */

      status = smsataLLIOStart( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smScsiRequest,
                                satIOContext);
      return status;
    }

    if (bufferID == 0 && bufferOffset == 0 && tl != 512)
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatReadBuffer: allocation length is not 512; it is %d!!!\n", tl));
      return SM_RC_SUCCESS;
    }

    if (bufferID == 0 && bufferOffset != 0)
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatReadBuffer: buffer offset is not 0; it is %d!!!\n", bufferOffset));
      return SM_RC_SUCCESS;
    }
    /* all other cases unsupported */
    SM_DBG1(("smsatReadBuffer: unsupported case 1!!!\n"));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    return SM_RC_SUCCESS;

  }
  else if (mode == READ_BUFFER_DESCRIPTOR_MODE) /* 3 */
  {
    if (tl < READ_BUFFER_DESCRIPTOR_MODE_DATA_LEN) /* 4 */
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatReadBuffer: tl < 4; tl is %d!!!\n", tl));
      return SM_RC_SUCCESS;
    }
    if (bufferID == 0)
    {
      /* SPC-4, 6.15.5, p189; SAT-2 Rev00, 8.7.2.3, p41*/
      pBuff[0] = 0xFF;
      pBuff[1] = 0x00;
      pBuff[2] = 0x02;
      pBuff[3] = 0x00;
      if (READ_BUFFER_DESCRIPTOR_MODE_DATA_LEN < tl)
      {
        /* underrrun */
        SM_DBG1(("smsatReadBuffer: underrun tl %d data %d!!!\n", tl, READ_BUFFER_DESCRIPTOR_MODE_DATA_LEN));
        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOUnderRun,
                           tl - READ_BUFFER_DESCRIPTOR_MODE_DATA_LEN,
                           agNULL,
                           satIOContext->interruptContext );

        return SM_RC_SUCCESS;
      }
      else
      {
        /*smEnqueueIO(smRoot, satIOContext);*/

        tdsmIOCompletedCB( smRoot,
                           smIORequest,
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satIOContext->interruptContext);
        return SM_RC_SUCCESS;
      }
    }
    else
    {
      /* We don't support other than bufferID 0 */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_COMMAND,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      return SM_RC_SUCCESS;
    }
  }
  else
  {
    /* We don't support any other mode */
    SM_DBG1(("smsatReadBuffer: unsupported mode %d!!!\n", mode));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    return SM_RC_SUCCESS;
  }
}

osGLOBAL bit32
smsatWriteBuffer(
                 smRoot_t                  *smRoot,
                 smIORequest_t             *smIORequest,
                 smDeviceHandle_t          *smDeviceHandle,
                 smScsiInitiatorRequest_t  *smScsiRequest,
                 smSatIOContext_t            *satIOContext
                )
{
#ifdef NOT_YET
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
#endif
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
#ifdef NOT_YET
  agsaFisRegHostToDevice_t  *fis;
#endif
  bit32                     bufferOffset;
  bit32                     parmLen;
  bit8                      mode;
  bit8                      bufferID;
  bit8                      *pBuff;

  pSense        = satIOContext->pSense;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
#ifdef NOT_YET
  fis           = satIOContext->pFis;
#endif
  pBuff         = (bit8 *) smScsiRequest->sglVirtualAddr;

  SM_DBG5(("smsatWriteBuffer: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatWriteBuffer: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  bufferOffset = (scsiCmnd->cdb[3] << (8*2)) + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  parmLen = (scsiCmnd->cdb[6] << (8*2)) + (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

  mode = (bit8)(scsiCmnd->cdb[1] & SCSI_READ_BUFFER_MODE_MASK);
  bufferID = scsiCmnd->cdb[2];

  /* for debugging only */
  smhexdump("smsatWriteBuffer pBuff", (bit8 *)pBuff, 24);

  if (mode == WRITE_BUFFER_DATA_MODE) /* 2 */
  {
    if (bufferID == 0 && bufferOffset == 0 && parmLen == 512)
    {
      SM_DBG1(("smsatWriteBuffer: sending ATA WRITE BUFFER!!!\n"));
      /* send ATA WRITE BUFFER */
#ifdef NOT_YET
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_BUFFER;       /* 0xE8 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA (27:24) and FIS LBA mode  */
      fis->d.lbaLowExp      = 0;
      fis->d.lbaMidExp      = 0;
      fis->d.lbaHighExp     = 0;
      fis->d.featuresExp    = 0;
      fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;


      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;

      satIOContext->satCompleteCB = &smsatWriteBufferCB;

      satIOContext->reqType = agRequestType;       /* Save it */

      status = smsataLLIOStart( smRoot,
                                smIORequest,
                                smDeviceHandle,
                                smScsiRequest,
                                satIOContext);
      return status;
#endif
      /* temp */
      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satIOContext->interruptContext);
      return SM_RC_SUCCESS;
    }
    if ( (bufferID == 0 && bufferOffset != 0) ||
         (bufferID == 0 && parmLen != 512)
        )
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );

      SM_DBG1(("smsatWriteBuffer: wrong buffer offset %d or parameter length parmLen %d!!!\n", bufferOffset, parmLen));
      return SM_RC_SUCCESS;
    }

    /* all other cases unsupported */
    SM_DBG1(("smsatWriteBuffer: unsupported case 1!!!\n"));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    return SM_RC_SUCCESS;

  }
  else if (mode == WRITE_BUFFER_DL_MICROCODE_SAVE_MODE) /* 5 */
  {
    /* temporary */
    SM_DBG1(("smsatWriteBuffer: not yet supported mode %d!!!\n", mode));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

  
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    return SM_RC_SUCCESS;
  }
  else
  {
    /* We don't support any other mode */
    SM_DBG1(("smsatWriteBuffer: unsupported mode %d!!!\n", mode));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    return SM_RC_SUCCESS;
  }

}

osGLOBAL bit32
smsatReassignBlocks(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   )
{
  /*
    assumes all LBA fits in ATA command; no boundary condition is checked here yet
  */
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pParmList;    /* Log Page data buffer */
  bit8                      LongLBA;
  bit8                      LongList;
  bit32                     defectListLen;
  bit8                      LBA[8];
  bit32                     startingIndex;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pParmList     = (bit8 *) smScsiRequest->sglVirtualAddr;

  SM_DBG5(("smsatReassignBlocks: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

    /*smEnqueueIO(smRoot, satIOContext);*/

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satIOContext->interruptContext );

    SM_DBG1(("smsatReassignBlocks: return control!!!\n"));
    return SM_RC_SUCCESS;
  }

  sm_memset(satIOContext->LBA, 0, 8);
  satIOContext->ParmIndex = 0;
  satIOContext->ParmLen = 0;

  LongList = (bit8)(scsiCmnd->cdb[1] & SCSI_REASSIGN_BLOCKS_LONGLIST_MASK);
  LongLBA = (bit8)(scsiCmnd->cdb[1] & SCSI_REASSIGN_BLOCKS_LONGLBA_MASK);
  sm_memset(LBA, 0, sizeof(LBA));

  if (LongList == 0)
  {
    defectListLen = (pParmList[2] << 8) + pParmList[3];
  }
  else
  {
    defectListLen = (pParmList[0] << (8*3)) + (pParmList[1] << (8*2))
                  + (pParmList[2] << 8) + pParmList[3];
  }
  /* SBC 5.16.2, p61*/
  satIOContext->ParmLen = defectListLen + 4 /* header size */;

  startingIndex = 4;

  if (LongLBA == 0)
  {
    LBA[4] = pParmList[startingIndex];   /* MSB */
    LBA[5] = pParmList[startingIndex+1];
    LBA[6] = pParmList[startingIndex+2];
    LBA[7] = pParmList[startingIndex+3];  /* LSB */
    startingIndex = startingIndex + 4;
  }
  else
  {
    LBA[0] = pParmList[startingIndex];    /* MSB */
    LBA[1] = pParmList[startingIndex+1];
    LBA[2] = pParmList[startingIndex+2];
    LBA[3] = pParmList[startingIndex+3];
    LBA[4] = pParmList[startingIndex+4];
    LBA[5] = pParmList[startingIndex+5];
    LBA[6] = pParmList[startingIndex+6];
    LBA[7] = pParmList[startingIndex+7];  /* LSB */
    startingIndex = startingIndex + 8;
  }

  smhexdump("smsatReassignBlocks Parameter list", (bit8 *)pParmList, 4 + defectListLen);

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    /* sends READ VERIFY SECTOR(S) EXT*/
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[7];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[6];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[5];                 /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = LBA[4];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = LBA[3];                 /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = LBA[2];                 /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
    fis->d.reserved4      = 0;
    fis->d.device         = 0x40;                   /* 01000000 */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;
  }
  else
  {
    /* READ VERIFY SECTOR(S)*/
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;/* 0x40 */
    fis->h.features       = 0;                      /* FIS features NA       */
    fis->d.lbaLow         = LBA[7];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[6];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[5];                 /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[4] & 0xF));
                            /* DEV and LBA 27:24 */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;
  }

  sm_memcpy(satIOContext->LBA, LBA, 8);
  satIOContext->ParmIndex = startingIndex;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatReassignBlocksCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  return status;
}

osGLOBAL bit32
smsatRead_1(
            smRoot_t                  *smRoot,
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
          )
{
  /*
    Assumption: error check on lba and tl has been done in satRead*()
    lba = lba + tl;
  */
  bit32                     status;
  smSatIOContext_t            *satOrgIOContext = agNULL;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  SM_DBG2(("smsatRead_1: start\n"));

  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;
  scsiCmnd        = satOrgIOContext->pScsiCmnd;

  sm_memset(LBA,0, sizeof(LBA));

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_READ_DMA:
    DenomTL = 0x100;
    break;
  case SAT_READ_SECTORS:
    DenomTL = 0x100;
    break;
  case SAT_READ_DMA_EXT:
    DenomTL = 0xFFFF;
    break;
  case SAT_READ_SECTORS_EXT:
    DenomTL = 0xFFFF;
    break;
  case SAT_READ_FPDMA_QUEUED:
    DenomTL = 0xFFFF;
    break;
  default:
    SM_DBG1(("smsatRead_1: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return SM_RC_FAILURE;
    break;
  }

  Remainder = satOrgIOContext->OrgTL % DenomTL;
  satOrgIOContext->currentLBA = satOrgIOContext->currentLBA + DenomTL;
  lba = satOrgIOContext->currentLBA;

  LBA[0] = (bit8)((lba & 0xFF000000) >> (8 * 3));
  LBA[1] = (bit8)((lba & 0xFF0000) >> (8 * 2));
  LBA[2] = (bit8)((lba & 0xFF00) >> 8);
  LBA[3] = (bit8)(lba & 0xFF);

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_READ_DMA:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_DMA;           /* 0xC8 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         =
      (bit8)((0x4 << 4) | (LBA[0] & 0xF));                  /* FIS LBA (27:24) and FIS LBA mode  */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;

    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)Remainder;            /* FIS sector count (7:0) */
    }
    else
    {
      fis->d.sectorCount    = 0x0;                  /* FIS sector count (7:0) */
    }

    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;

    break;
  case SAT_READ_SECTORS:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_SECTORS;       /* 0x20 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         =
      (bit8)((0x4 << 4) | (LBA[0] & 0xF));                  /* FIS LBA (27:24) and FIS LBA mode  */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)Remainder;            /* FIS sector count (7:0) */
    }
    else
    {
      fis->d.sectorCount    = 0x0;                   /* FIS sector count (7:0) */
    }
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;

    break;
  case SAT_READ_DMA_EXT:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_DMA_EXT;       /* 0x25 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set */
    fis->d.lbaLowExp      = LBA[0];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)(Remainder & 0xFF);     /* FIS sector count (7:0) */
      fis->d.sectorCountExp = (bit8)((Remainder & 0xFF00) >> 8);       /* FIS sector count (15:8) */

    }
    else
    {
      fis->d.sectorCount    = 0xFF;       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0xFF;       /* FIS sector count (15:8) */
    }
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;

    break;
  case SAT_READ_SECTORS_EXT:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_SECTORS_EXT;   /* 0x24 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set */
    fis->d.lbaLowExp      = LBA[0];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)(Remainder & 0xFF);     /* FIS sector count (7:0) */
      fis->d.sectorCountExp = (bit8)((Remainder & 0xFF00) >> 8);  /* FIS sector count (15:8) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0xFF;       /* FIS sector count (15:8) */
    }
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
    break;
  case SAT_READ_FPDMA_QUEUED:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_FPDMA_QUEUED;  /* 0x60 */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_READ10_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = LBA[0];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->h.features       = (bit8)(Remainder & 0xFF);       /* FIS sector count (7:0) */
      fis->d.featuresExp    = (bit8)((Remainder & 0xFF00) >> 8);       /* FIS sector count (15:8) */
    }
    else
    {
      fis->h.features       = 0xFF;       /* FIS sector count (7:0) */
      fis->d.featuresExp    = 0xFF;       /* FIS sector count (15:8) */
    }
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_READ;
    break;
  default:
    SM_DBG1(("smsatRead_1: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return SM_RC_FAILURE;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &smsatChainedDataIOCB;

  if (satOrgIOContext->ATACmd == SAT_READ_DMA || satOrgIOContext->ATACmd == SAT_READ_SECTORS)
  {
    smsatSplitSGL(smRoot,
                  smIORequest,
                  smDeviceHandle,
                  (smScsiInitiatorRequest_t *)satOrgIOContext->smScsiXchg,
                  satOrgIOContext,
                  NON_BIT48_ADDRESS_TL_LIMIT * SATA_SECTOR_SIZE, /* 0x100 * 0x200*/
                  (satOrgIOContext->OrgTL) * SATA_SECTOR_SIZE,
                  agFALSE);
  }
  else
  {
    smsatSplitSGL(smRoot,
                  smIORequest,
                  smDeviceHandle,
                  (smScsiInitiatorRequest_t *)satOrgIOContext->smScsiXchg,
                  satOrgIOContext,
                  BIT48_ADDRESS_TL_LIMIT * SATA_SECTOR_SIZE, /* 0xFFFF * 0x200*/
                  (satOrgIOContext->OrgTL) * SATA_SECTOR_SIZE,
                  agFALSE);
  }

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            (smScsiInitiatorRequest_t *)satOrgIOContext->smScsiXchg, //smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatRead_1: return\n"));
  return (status);
}

osGLOBAL bit32
smsatWrite_1(
             smRoot_t                  *smRoot,
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
           )
{
  /*
    Assumption: error check on lba and tl has been done in satWrite*()
    lba = lba + tl;
  */
  bit32                     status;
  smSatIOContext_t            *satOrgIOContext = agNULL;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  SM_DBG2(("smsatWrite_1: start\n"));

  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;
  scsiCmnd        = satOrgIOContext->pScsiCmnd;

  sm_memset(LBA,0, sizeof(LBA));

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_WRITE_DMA:
    DenomTL = 0x100;
    break;
  case SAT_WRITE_SECTORS:
    DenomTL = 0x100;
    break;
  case SAT_WRITE_DMA_EXT:
    DenomTL = 0xFFFF;
    break;
  case SAT_WRITE_DMA_FUA_EXT:
    DenomTL = 0xFFFF;
    break;
  case SAT_WRITE_SECTORS_EXT:
    DenomTL = 0xFFFF;
    break;
  case SAT_WRITE_FPDMA_QUEUED:
    DenomTL = 0xFFFF;
    break;
  default:
    SM_DBG1(("smsatWrite_1: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return SM_RC_FAILURE;
    break;
  }

  Remainder = satOrgIOContext->OrgTL % DenomTL;
  satOrgIOContext->currentLBA = satOrgIOContext->currentLBA + DenomTL;
  lba = satOrgIOContext->currentLBA;


  LBA[0] = (bit8)((lba & 0xFF000000) >> (8 * 3));
  LBA[1] = (bit8)((lba & 0xFF0000) >> (8 * 2));
  LBA[2] = (bit8)((lba & 0xFF00) >> 8);
  LBA[3] = (bit8)(lba & 0xFF);

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_WRITE_DMA:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */

    /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[0] & 0xF));

    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)Remainder;             /* FIS sector count (7:0) */
    }
    else
    {
      fis->d.sectorCount    = 0x0;                   /* FIS sector count (7:0) */
    }
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;

    break;
  case SAT_WRITE_SECTORS:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */

    /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[0] & 0xF));

    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)Remainder;            /* FIS sector count (7:0) */
    }
    else
    {
      fis->d.sectorCount    = 0x0;                 /* FIS sector count (7:0) */
    }
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;

    break;
  case SAT_WRITE_DMA_EXT:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x3D */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set */
    fis->d.lbaLowExp      = LBA[0];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)(Remainder & 0xFF);       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = (bit8)((Remainder & 0xFF00) >> 8);       /* FIS sector count (15:8) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                  /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0xFF;                  /* FIS sector count (15:8) */
    }
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                       /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;

    break;
  case SAT_WRITE_SECTORS_EXT:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */

    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set */
    fis->d.lbaLowExp      = LBA[0];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)(Remainder & 0xFF);     /* FIS sector count (7:0) */
      fis->d.sectorCountExp = (bit8)((Remainder & 0xFF00) >> 8);   /* FIS sector count (15:8) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                 /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0xFF;                 /* FIS sector count (15:8) */
    }
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;

    break;
  case SAT_WRITE_FPDMA_QUEUED:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_WRITE10_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = LBA[0];;                /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->h.features       = (bit8)(Remainder & 0xFF);     /* FIS sector count (7:0) */
      fis->d.featuresExp    = (bit8)((Remainder & 0xFF00) >> 8);       /* FIS sector count (15:8) */
    }
    else
    {
      fis->h.features       = 0xFF;                 /* FIS sector count (7:0) */
      fis->d.featuresExp    = 0xFF;                 /* FIS sector count (15:8) */
    }
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    break;

  default:
    SM_DBG1(("smsatWrite_1: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return SM_RC_FAILURE;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &smsatChainedDataIOCB;

  if (satOrgIOContext->ATACmd == SAT_WRITE_DMA || satOrgIOContext->ATACmd == SAT_WRITE_SECTORS)
  {
    smsatSplitSGL(smRoot,
                  smIORequest,
                  smDeviceHandle,
                  (smScsiInitiatorRequest_t *)satOrgIOContext->smScsiXchg,
                  satOrgIOContext,
                  NON_BIT48_ADDRESS_TL_LIMIT * SATA_SECTOR_SIZE, /* 0x100 * 0x200*/
                  (satOrgIOContext->OrgTL) * SATA_SECTOR_SIZE,
                  agFALSE);
  }
  else
  {
    smsatSplitSGL(smRoot,
                  smIORequest,
                  smDeviceHandle,
                  (smScsiInitiatorRequest_t *)satOrgIOContext->smScsiXchg,
                  satOrgIOContext,
                  BIT48_ADDRESS_TL_LIMIT * SATA_SECTOR_SIZE, /* 0xFFFF * 0x200*/
                  (satOrgIOContext->OrgTL) * SATA_SECTOR_SIZE,
                  agFALSE);
  }

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            (smScsiInitiatorRequest_t *)satOrgIOContext->smScsiXchg, //smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatWrite_1: return\n"));
  return (status);
}

osGLOBAL bit32  
smsatPassthrough(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   )
{
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smDeviceData_t            *pSatDevData;
  agsaFisRegHostToDevice_t	  *fis;
  bit32                      status;
  bit32 					agRequestType;
  smAtaPassThroughHdr_t       ataPassThroughHdr;

	  
  pSense      = satIOContext->pSense;
  scsiCmnd    = &smScsiRequest->scsiCmnd;  
  pSatDevData = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;

  SM_DBG1(("smsatPassthrough: START!!!\n"));

  osti_memset(&ataPassThroughHdr, 0 , sizeof(smAtaPassThroughHdr_t));
	  
  ataPassThroughHdr.opc = scsiCmnd->cdb[0];
  ataPassThroughHdr.mulCount = scsiCmnd->cdb[1] >> 5;
  ataPassThroughHdr.proto = (scsiCmnd->cdb[1] >> 1) & 0x0F;
  ataPassThroughHdr.extend = scsiCmnd->cdb[1] & 1;
  ataPassThroughHdr.offline = scsiCmnd->cdb[2] >> 6;
  ataPassThroughHdr.ckCond = (scsiCmnd->cdb[2] >> 5) & 1;
  ataPassThroughHdr.tType = (scsiCmnd->cdb[2] >> 4) & 1;
  ataPassThroughHdr.tDir = (scsiCmnd->cdb[2] >> 3) & 1;
  ataPassThroughHdr.byteBlock = (scsiCmnd->cdb[2] >> 2) & 1;
  ataPassThroughHdr.tlength = scsiCmnd->cdb[2] & 0x3;
	  
  switch(ataPassThroughHdr.proto)
  {
    case 0:
    case 9:
    	    agRequestType = AGSA_SATA_PROTOCOL_DEV_RESET;	//Device Reset
	    break;
    case 1:
       	    agRequestType = AGSA_SATA_PROTOCOL_SRST_ASSERT;		//Software reset
	    break;
    case 3:
            agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;	//Non Data mode
	    break;
    case 4:
       	    agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;	//IO_Data_In mode
	    break;
    case 5:
            agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;		//PIO_Data_out
            break;
    case 6:
            agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;	//DMA READ and WRITE
            break;
    case 8:
            agRequestType = AGSA_SATA_ATAP_EXECDEVDIAG; 	//device diagnostic
	    break;
    case 12:
            agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;		//FPDMA Read and Write
            break;
    default:
            agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;	//Default Non Data Mode
            break;
  }
	  
  
  if((ataPassThroughHdr.tlength == 0) && (agRequestType != AGSA_SATA_PROTOCOL_NON_DATA))
  {
    SM_DBG1(("smsatPassthrough SCSI_SNSCODE_INVALID_FIELD_IN_CDB\n"));
		   
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
			  SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
				
    tdsmIOCompletedCB( smRoot, 
                       smIORequest, 
                       smIOSuccess, 
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData, 
                       satIOContext->interruptContext );
			
    return SM_RC_SUCCESS; 
  }
				  	
  if(scsiCmnd->cdb[0] == 0xA1)
  {
    SM_DBG1(("smsatPassthrough A1h: COMMAND: %x  FEATURE: %x \n",scsiCmnd->cdb[9],scsiCmnd->cdb[3]));

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.features       = scsiCmnd->cdb[3];
    fis->d.sectorCount	  = scsiCmnd->cdb[4];		  /* 0x01  FIS sector count (7:0) */
    fis->d.lbaLow 		  = scsiCmnd->cdb[5];		  /* Reading LBA  FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[6];
    fis->d.lbaHigh        = scsiCmnd->cdb[7];
    fis->d.device         = scsiCmnd->cdb[8]; 
    fis->h.command		  = scsiCmnd->cdb[9]; 
    fis->d.featuresExp	  = 0;
    fis->d.sectorCountExp = 0; 
    fis->d.lbaLowExp	  = 0; 
    fis->d.lbaMidExp	  = 0;
    fis->d.lbaHighExp 	  = 0; 
    fis->d.reserved4	  = 0;
    fis->d.control		  = 0;					  /* FIS HOB bit clear */
    fis->d.reserved5	  = 0;

    /* Initialize CB for SATA completion*/
    satIOContext->satCompleteCB = &smsatPassthroughCB;
		
    /*
        * Prepare SGL and send FIS to LL layer.
    */

    satIOContext->reqType = agRequestType;
    status = smsataLLIOStart( smRoot, 
                              smIORequest,
	                      smDeviceHandle,
			      smScsiRequest,
                              satIOContext);
    return status;
    
   }
   else if(scsiCmnd->cdb[0] == 0x85)
   {
     SM_DBG1(("smsatPassthrough 85h: COMMAND: %x  FEATURE: %x \n",scsiCmnd->cdb[14],scsiCmnd->cdb[4]));
		
     fis->h.fisType        = 0x27;                   /* Reg host to device */
     fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

     if(1 == ataPassThroughHdr.extend)
     {
       fis->d.featuresExp    = scsiCmnd->cdb[3];
       fis->d.sectorCountExp = scsiCmnd->cdb[5];
       fis->d.lbaMidExp      = scsiCmnd->cdb[9];
       fis->d.lbaHighExp     = scsiCmnd->cdb[11];
       fis->d.lbaLowExp      = scsiCmnd->cdb[7];
     }
     fis->h.features = scsiCmnd->cdb[4]; 
     fis->d.sectorCount = scsiCmnd->cdb[6];		 
     fis->d.lbaLow = scsiCmnd->cdb[8];
     fis->d.lbaMid = scsiCmnd->cdb[10];
     fis->d.lbaHigh = scsiCmnd->cdb[12];
     fis->d.device  = scsiCmnd->cdb[13]; 
     fis->h.command = scsiCmnd->cdb[14]; 
     fis->d.reserved4 = 0;
     fis->d.control = 0;					 
     fis->d.reserved5	  = 0;


     /* Initialize CB for SATA completion.
      */

     satIOContext->satCompleteCB = &smsatPassthroughCB;
	  		
     /*
       * Prepare SGL and send FIS to LL layer.
      */
     satIOContext->reqType = agRequestType;
     status = smsataLLIOStart( smRoot, 
                               smIORequest,
                               smDeviceHandle,
                               smScsiRequest,
                               satIOContext);
     return status;
	  
   }
   else
   {
     SM_DBG1(("smsatPassthrough : INVALD PASSTHROUGH!!!\n"));
     smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);
     tdsmIOCompletedCB( smRoot, 
                       smIORequest, 
                       smIOSuccess, 
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData, 
                       satIOContext->interruptContext );
    
     SM_DBG1(("smsatPassthrough : return control!!!\n"));

     return SM_RC_SUCCESS; 
   }
}

osGLOBAL bit32
smsatNonChainedWriteNVerify_Verify(
                                   smRoot_t                  *smRoot,
                                   smIORequest_t             *smIORequest,
                                   smDeviceHandle_t          *smDeviceHandle,
                                   smScsiInitiatorRequest_t  *smScsiRequest,
                                   smSatIOContext_t            *satIOContext
                                  )
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  smDeviceData_t            *pSatDevData;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  SM_DBG5(("smsatNonChainedWriteNVerify_Verify: start\n"));
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set 01000000 */
    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
    fis->d.sectorCountExp = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */

    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedWriteNVerifyCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = smsataLLIOStart( smRoot,
                              smIORequest,
                              smDeviceHandle,
                              smScsiRequest,
                              satIOContext);


    SM_DBG1(("smsatNonChainedWriteNVerify_Verify: return status %d!!!\n", status));
    return (status);
  }
  else
  {
    /* can't fit in SAT_READ_VERIFY_SECTORS becasue of Sector Count and LBA */
    SM_DBG1(("smsatNonChainedWriteNVerify_Verify: can't fit in SAT_READ_VERIFY_SECTORS!!!\n"));
    return SM_RC_FAILURE;
  }
}

osGLOBAL bit32
smsatChainedWriteNVerify_Start_Verify(
                                      smRoot_t                  *smRoot,
                                      smIORequest_t             *smIORequest,
                                      smDeviceHandle_t          *smDeviceHandle,
                                      smScsiInitiatorRequest_t  *smScsiRequest,
                                      smSatIOContext_t            *satIOContext
                                     )
{
  /*
    deal with transfer length; others have been handled previously at this point;
    no LBA check; no range check;
  */
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  smDeviceData_t            *pSatDevData;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatChainedWriteNVerify_Start_Verify: start\n"));
  sm_memset(LBA, 0, sizeof(LBA));
  sm_memset(TL, 0, sizeof(TL));
  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */
  TL[0] = scsiCmnd->cdb[6];   /* MSB */
  TL[1] = scsiCmnd->cdb[7];
  TL[2] = scsiCmnd->cdb[7];
  TL[3] = scsiCmnd->cdb[8];   /* LSB */
  lba = smsatComputeCDB12LBA(satIOContext);
  tl = smsatComputeCDB12TL(satIOContext);
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    SM_DBG5(("smsatChainedWriteNVerify_Start_Verify: SAT_READ_VERIFY_SECTORS_EXT\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set 01000000 */
    fis->d.lbaLowExp      = scsiCmnd->cdb[2];       /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
    fis->d.sectorCountExp = scsiCmnd->cdb[7];       /* FIS sector count (15:8) */

    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    satIOContext->ATACmd = SAT_READ_VERIFY_SECTORS_EXT;
  }
  else
  {
    SM_DBG5(("smsatChainedWriteNVerify_Start_Verify: SAT_READ_VERIFY_SECTORS\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;      /* 0x40 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */
      /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (scsiCmnd->cdb[2] & 0xF));
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = scsiCmnd->cdb[8];       /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
    satIOContext->ATACmd = SAT_READ_VERIFY_SECTORS;

 }

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_READ_VERIFY_SECTORS)
  {
    LoopNum = smsatComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = smsatComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    SM_DBG1(("smsatChainedWriteNVerify_Start_Verify: error case 1!!!\n"));
    LoopNum = 1;
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    SM_DBG5(("smsatChainedWriteNVerify_Start_Verify: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatNonChainedWriteNVerifyCB;
  }
  else
  {
    SM_DBG1(("smsatChainedWriteNVerify_Start_Verify: CHAINED data!!!\n"));
    /* re-setting tl */
    if (fis->h.command == SAT_READ_VERIFY_SECTORS)
    {
       fis->d.sectorCount    = 0xFF;
    }
    else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
    {
      fis->d.sectorCount    = 0xFF;
      fis->d.sectorCountExp = 0xFF;
    }
    else
    {
      SM_DBG1(("smsatChainedWriteNVerify_Start_Verify: error case 2!!!\n"));
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatChainedWriteNVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  return (status);


}

osGLOBAL bit32
smsatChainedWriteNVerify_Write(
                               smRoot_t                  *smRoot,
                               smIORequest_t             *smIORequest,
                               smDeviceHandle_t          *smDeviceHandle,
                               smScsiInitiatorRequest_t  *smScsiRequest,
                               smSatIOContext_t            *satIOContext
                              )
{
  /*
    Assumption: error check on lba and tl has been done in satWrite*()
    lba = lba + tl;
  */
  bit32                     status;
  smSatIOContext_t            *satOrgIOContext = agNULL;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  SM_DBG1(("smsatChainedWriteNVerify_Write: start\n"));

  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;
  scsiCmnd        = satOrgIOContext->pScsiCmnd;


  sm_memset(LBA,0, sizeof(LBA));

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_WRITE_DMA:
    DenomTL = 0xFF;
    break;
  case SAT_WRITE_SECTORS:
    DenomTL = 0xFF;
    break;
  case SAT_WRITE_DMA_EXT:
    DenomTL = 0xFFFF;
    break;
  case SAT_WRITE_DMA_FUA_EXT:
    DenomTL = 0xFFFF;
    break;
  case SAT_WRITE_SECTORS_EXT:
    DenomTL = 0xFFFF;
    break;
  case SAT_WRITE_FPDMA_QUEUED:
    DenomTL = 0xFFFF;
    break;
  default:
    SM_DBG1(("satChainedWriteNVerify_Write: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return SM_RC_FAILURE;
    break;
  }

  Remainder = satOrgIOContext->OrgTL % DenomTL;
  satOrgIOContext->currentLBA = satOrgIOContext->currentLBA + DenomTL;
  lba = satOrgIOContext->currentLBA;

  LBA[0] = (bit8)((lba & 0xF000) >> (8 * 3)); /* MSB */
  LBA[1] = (bit8)((lba & 0xF00) >> (8 * 2));
  LBA[2] = (bit8)((lba & 0xF0) >> 8);
  LBA[3] = (bit8)(lba & 0xF);               /* LSB */

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_WRITE_DMA:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */

    /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[0] & 0xF));

    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)Remainder;             /* FIS sector count (7:0) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                   /* FIS sector count (7:0) */
    }
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;

    break;
  case SAT_WRITE_SECTORS:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */

    /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[0] & 0xF));

    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)Remainder;            /* FIS sector count (7:0) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                 /* FIS sector count (7:0) */
    }
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;

    break;
  case SAT_WRITE_DMA_EXT:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x3D */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set */
    fis->d.lbaLowExp      = LBA[0];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)(Remainder & 0xFF);       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = (bit8)((Remainder & 0xFF00) >> 8);       /* FIS sector count (15:8) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                  /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0xFF;                  /* FIS sector count (15:8) */
    }
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                       /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;

    break;
  case SAT_WRITE_SECTORS_EXT:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */

    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set */
    fis->d.lbaLowExp      = LBA[0];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)(Remainder & 0xFF);     /* FIS sector count (7:0) */
      fis->d.sectorCountExp = (bit8)((Remainder & 0xFF00) >> 8);   /* FIS sector count (15:8) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                 /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0xFF;                 /* FIS sector count (15:8) */
    }
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;

    break;
  case SAT_WRITE_FPDMA_QUEUED:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */

    /* Check FUA bit */
    if (scsiCmnd->cdb[1] & SCSI_WRITE10_FUA_MASK)
      fis->d.device       = 0xC0;                   /* FIS FUA set */
    else
      fis->d.device       = 0x40;                   /* FIS FUA clear */

    fis->d.lbaLowExp      = LBA[0];;                /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->h.features       = (bit8)(Remainder & 0xFF);     /* FIS sector count (7:0) */
      fis->d.featuresExp    = (bit8)((Remainder & 0xFF00) >> 8);       /* FIS sector count (15:8) */
    }
    else
    {
      fis->h.features       = 0xFF;                 /* FIS sector count (7:0) */
      fis->d.featuresExp    = 0xFF;                 /* FIS sector count (15:8) */
    }
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    break;

  default:
    SM_DBG1(("satChainedWriteNVerify_Write: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return SM_RC_FAILURE;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &smsatChainedWriteNVerifyCB;


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("satChainedWriteNVerify_Write: return\n"));
  return (status);
}

osGLOBAL bit32
smsatChainedWriteNVerify_Verify(
                                smRoot_t                  *smRoot,
                                smIORequest_t             *smIORequest,
                                smDeviceHandle_t          *smDeviceHandle,
                                smScsiInitiatorRequest_t  *smScsiRequest,
                                smSatIOContext_t            *satIOContext
                               )
{
  bit32                     status;
  smSatIOContext_t         *satOrgIOContext = agNULL;
  agsaFisRegHostToDevice_t *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  SM_DBG2(("smsatChainedWriteNVerify_Verify: start\n"));
  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;
  sm_memset(LBA,0, sizeof(LBA));
  switch (satOrgIOContext->ATACmd)
  {
  case SAT_READ_VERIFY_SECTORS:
    DenomTL = 0xFF;
    break;
  case SAT_READ_VERIFY_SECTORS_EXT:
    DenomTL = 0xFFFF;
    break;
  default:
    SM_DBG1(("smsatChainedWriteNVerify_Verify: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return SM_RC_FAILURE;
    break;
  }

  Remainder = satOrgIOContext->OrgTL % DenomTL;
  satOrgIOContext->currentLBA = satOrgIOContext->currentLBA + DenomTL;
  lba = satOrgIOContext->currentLBA;

  LBA[0] = (bit8)((lba & 0xF000) >> (8 * 3)); /* MSB */
  LBA[1] = (bit8)((lba & 0xF00) >> (8 * 2));
  LBA[2] = (bit8)((lba & 0xF0) >> 8);
  LBA[3] = (bit8)(lba & 0xF);               /* LSB */

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_READ_VERIFY_SECTORS:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;          /* 0x40 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */

    /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[0] & 0xF));

    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)Remainder;             /* FIS sector count (7:0) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                   /* FIS sector count (7:0) */
    }
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    break;
  case SAT_READ_VERIFY_SECTORS_EXT:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;      /* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set */
    fis->d.lbaLowExp      = LBA[0];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)(Remainder & 0xFF);       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = (bit8)((Remainder & 0xFF00) >> 8);       /* FIS sector count (15:8) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                  /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0xFF;                  /* FIS sector count (15:8) */
    }
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                       /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    break;

  default:
    SM_DBG1(("smsatChainedWriteNVerify_Verify: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return SM_RC_FAILURE;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &smsatChainedWriteNVerifyCB;


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatChainedWriteNVerify_Verify: return\n"));
  return (status);
}

osGLOBAL bit32
smsatChainedVerify(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
       )
{
  bit32                     status;
  smSatIOContext_t         *satOrgIOContext = agNULL;
  agsaFisRegHostToDevice_t *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  SM_DBG2(("smsatChainedVerify: start\n"));
  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;
  sm_memset(LBA,0, sizeof(LBA));
  switch (satOrgIOContext->ATACmd)
  {
  case SAT_READ_VERIFY_SECTORS:
    DenomTL = 0xFF;
    break;
  case SAT_READ_VERIFY_SECTORS_EXT:
    DenomTL = 0xFFFF;
    break;
  default:
    SM_DBG1(("satChainedVerify: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return tiError;
    break;
  }

  Remainder = satOrgIOContext->OrgTL % DenomTL;
  satOrgIOContext->currentLBA = satOrgIOContext->currentLBA + DenomTL;
  lba = satOrgIOContext->currentLBA;

  LBA[0] = (bit8)((lba & 0xF000) >> (8 * 3)); /* MSB */
  LBA[1] = (bit8)((lba & 0xF00) >> (8 * 2));
  LBA[2] = (bit8)((lba & 0xF0) >> 8);
  LBA[3] = (bit8)(lba & 0xF);               /* LSB */

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_READ_VERIFY_SECTORS:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;          /* 0x40 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */

    /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[0] & 0xF));

    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)Remainder;             /* FIS sector count (7:0) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                   /* FIS sector count (7:0) */
    }
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    break;
  case SAT_READ_VERIFY_SECTORS_EXT:
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;      /* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[3];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[2];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[1];                 /* FIS LBA (23:16) */
    fis->d.device         = 0x40;                   /* FIS LBA mode set */
    fis->d.lbaLowExp      = LBA[0];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    if (satOrgIOContext->LoopNum == 1)
    {
      /* last loop */
      fis->d.sectorCount    = (bit8)(Remainder & 0xFF);       /* FIS sector count (7:0) */
      fis->d.sectorCountExp = (bit8)((Remainder & 0xFF00) >> 8);       /* FIS sector count (15:8) */
    }
    else
    {
      fis->d.sectorCount    = 0xFF;                  /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0xFF;                  /* FIS sector count (15:8) */
    }
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                       /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    break;

  default:
    SM_DBG1(("satChainedVerify: error incorrect ata command 0x%x!!!\n", satIOContext->ATACmd));
    return tiError;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &smsatChainedVerifyCB;


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("satChainedVerify: return\n"));
  return (status);
}

osGLOBAL bit32
smsatWriteSame10_1(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext,
                    bit32                     lba
                  )
{
  /*
    sends SAT_WRITE_DMA_EXT
  */

  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      lba1, lba2 ,lba3, lba4;

  SM_DBG5(("smsatWriteSame10_1: start\n"));
  fis               = satIOContext->pFis;
  /* MSB */
  lba1 = (bit8)((lba & 0xFF000000) >> (8*3));
  lba2 = (bit8)((lba & 0x00FF0000) >> (8*2));
  lba3 = (bit8)((lba & 0x0000FF00) >> (8*1));
  /* LSB */
  lba4 = (bit8)(lba & 0x000000FF);
  /* SAT_WRITE_DMA_EXT */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x35 */
  fis->h.features       = 0;                      /* FIS reserve */
  fis->d.lbaLow         = lba4;                   /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = lba3;                   /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = lba2;                   /* FIS LBA (23:16) */
  fis->d.device         = 0x40;                   /* FIS LBA mode set */
  fis->d.lbaLowExp      = lba1;                   /* FIS LBA (31:24) */
  fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
  fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
  fis->d.featuresExp    = 0;                      /* FIS reserve */
  /* one sector at a time */
  fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;
  agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatWriteSame10CB;
  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */
  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  SM_DBG5(("smsatWriteSame10_1 return status %d\n", status));
  return status;
}


osGLOBAL bit32
smsatWriteSame10_2(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext,
                    bit32                     lba
                  )
{
  /*
    sends SAT_WRITE_SECTORS_EXT
  */

  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      lba1, lba2 ,lba3, lba4;

  SM_DBG5(("smsatWriteSame10_2: start\n"));
  fis               = satIOContext->pFis;
  /* MSB */
  lba1 = (bit8)((lba & 0xFF000000) >> (8*3));
  lba2 = (bit8)((lba & 0x00FF0000) >> (8*2));
  lba3 = (bit8)((lba & 0x0000FF00) >> (8*1));
  /* LSB */
  lba4 = (bit8)(lba & 0x000000FF);
  /* SAT_WRITE_SECTORS_EXT */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */
  fis->h.features       = 0;                      /* FIS reserve */
  fis->d.lbaLow         = lba4;                   /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = lba3;                   /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = lba2;                   /* FIS LBA (23:16) */
  fis->d.device         = 0x40;                   /* FIS LBA mode set */
  fis->d.lbaLowExp      = lba1;                   /* FIS LBA (31:24) */
  fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
  fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
  fis->d.featuresExp    = 0;                      /* FIS reserve */
  /* one sector at a time */
  fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;
  agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatWriteSame10CB;
  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */
  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
  SM_DBG5(("smsatWriteSame10_2 return status %d\n", status));
  return status;
}


osGLOBAL bit32
smsatWriteSame10_3(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext,
                    bit32                     lba
                  )
{
  /*
    sends SAT_WRITE_FPDMA_QUEUED
  */

  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      lba1, lba2 ,lba3, lba4;

  SM_DBG5(("smsatWriteSame10_3: start\n"));
  fis               = satIOContext->pFis;
  /* MSB */
  lba1 = (bit8)((lba & 0xFF000000) >> (8*3));
  lba2 = (bit8)((lba & 0x00FF0000) >> (8*2));
  lba3 = (bit8)((lba & 0x0000FF00) >> (8*1));
  /* LSB */
  lba4 = (bit8)(lba & 0x000000FF);

  /* SAT_WRITE_FPDMA_QUEUED */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */


  /* one sector at a time */
  fis->h.features       = 1;                      /* FIS sector count (7:0) */
  fis->d.featuresExp    = 0;                      /* FIS sector count (15:8) */

  fis->d.lbaLow         = lba4;                   /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = lba3;                   /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = lba2;                   /* FIS LBA (23:16) */
  /* NO FUA bit in the WRITE SAME 10 */
  fis->d.device         = 0x40;                   /* FIS FUA clear */
  fis->d.lbaLowExp      = lba1;                   /* FIS LBA (31:24) */
  fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
  fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
  fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;
  agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatWriteSame10CB;
  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */
  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatWriteSame10_3 return status %d\n", status));
  return status;
}

osGLOBAL bit32
smsatStartStopUnit_1(
                     smRoot_t                  *smRoot,
                     smIORequest_t             *smIORequest,
                     smDeviceHandle_t          *smDeviceHandle,
                     smScsiInitiatorRequest_t  *smScsiRequest,
                     smSatIOContext_t            *satIOContext
        )
{
  /*
    SAT Rev 8, Table 48, 9.11.3 p55
    sends STANDBY
  */
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  SM_DBG5(("smsatStartStopUnit_1: start\n"));
  fis               = satIOContext->pFis;
  /* STANDBY */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_STANDBY;            /* 0xE2 */
  fis->h.features       = 0;                      /* FIS features NA       */
  fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.device         = 0;                      /* 0 */
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatStartStopUnitCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatStartStopUnit_1 return status %d\n", status));
  return status;
}

osGLOBAL bit32
smsatSendDiagnostic_1(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
         )
{
  /*
    SAT Rev9, Table29, p41
    send 2nd SAT_READ_VERIFY_SECTORS(_EXT)
  */
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;

  SM_DBG5(("smsatSendDiagnostic_1: start\n"));
  pSatDevData       = satIOContext->pSatDevData;
  fis               = satIOContext->pFis;
  /*
    sector count 1, LBA MAX
  */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    /* sends READ VERIFY SECTOR(S) EXT*/
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = pSatDevData->satMaxLBA[7]; /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = pSatDevData->satMaxLBA[6]; /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = pSatDevData->satMaxLBA[5]; /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = pSatDevData->satMaxLBA[4]; /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = pSatDevData->satMaxLBA[3]; /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = pSatDevData->satMaxLBA[2]; /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
    fis->d.reserved4      = 0;
    fis->d.device         = 0x40;                   /* 01000000 */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

  }
  else
  {
    /* READ VERIFY SECTOR(S)*/
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;/* 0x40 */
    fis->h.features       = 0;                      /* FIS features NA       */
    fis->d.lbaLow         = pSatDevData->satMaxLBA[7]; /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = pSatDevData->satMaxLBA[6]; /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = pSatDevData->satMaxLBA[5]; /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.device         = (bit8)((0x4 << 4) | (pSatDevData->satMaxLBA[4] & 0xF));
                            /* DEV and LBA 27:24 */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

  }

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatSendDiagnosticCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);


  return status;
}

osGLOBAL bit32
smsatSendDiagnostic_2(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
         )
{
  /*
    SAT Rev9, Table29, p41
    send 3rd SAT_READ_VERIFY_SECTORS(_EXT)
  */
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;

  SM_DBG5(("smsatSendDiagnostic_2: start\n"));

  pSatDevData       = satIOContext->pSatDevData;
  fis               = satIOContext->pFis;
  /*
    sector count 1, LBA Random
  */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    /* sends READ VERIFY SECTOR(S) EXT*/
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = 0x7F;                   /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0;                      /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0;                      /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0;                      /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
    fis->d.reserved4      = 0;
    fis->d.device         = 0x40;                   /* 01000000 */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

  }
  else
  {
    /* READ VERIFY SECTOR(S)*/
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;/* 0x40 */
    fis->h.features       = 0;                      /* FIS features NA       */
    fis->d.lbaLow         = 0x7F;                   /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.device         = 0x40;                   /* FIS LBA mode set 01000000 */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

  }

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatSendDiagnosticCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);


  return status;
}

osGLOBAL bit32
smsatModeSelect6n10_1(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
         )
{
  /* sends either ATA SET FEATURES based on DRA bit */
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pLogPage;    /* Log Page data buffer */
  bit32                     StartingIndex = 0;

  fis           = satIOContext->pFis;
  pLogPage      = (bit8 *) smScsiRequest->sglVirtualAddr;
  SM_DBG5(("smsatModeSelect6n10_1: start\n"));
 
  if (pLogPage[3] == 8)
  {
    /* mode parameter block descriptor exists */
    StartingIndex = 12;
  }
  else
  {
    /* mode parameter block descriptor does not exist */
    StartingIndex = 4;
  }

  /* sends ATA SET FEATURES based on DRA bit */
  if ( !(pLogPage[StartingIndex + 12] & SCSI_MODE_SELECT6_DRA_MASK) )
  {
    SM_DBG5(("smsatModeSelect6n10_1: enable read look-ahead feature\n"));
    /* sends SET FEATURES */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
    fis->h.features       = 0xAA;                   /* enable read look-ahead */
    fis->d.lbaLow         = 0;                      /* */
    fis->d.lbaMid         = 0;                      /* */
    fis->d.lbaHigh        = 0;                      /* */
    fis->d.device         = 0;                      /* */
    fis->d.lbaLowExp      = 0;                      /* */
    fis->d.lbaMidExp      = 0;                      /* */
    fis->d.lbaHighExp     = 0;                      /* */
    fis->d.featuresExp    = 0;                      /* */
    fis->d.sectorCount    = 0;                      /* */
    fis->d.sectorCountExp = 0;                      /* */
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
    return status;
  }
  else
  {
    SM_DBG5(("smsatModeSelect6n10_1: disable read look-ahead feature\n"));
        /* sends SET FEATURES */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
    fis->h.features       = 0x55;                   /* disable read look-ahead */
    fis->d.lbaLow         = 0;                      /* */
    fis->d.lbaMid         = 0;                      /* */
    fis->d.lbaHigh        = 0;                      /* */
    fis->d.device         = 0;                      /* */
    fis->d.lbaLowExp      = 0;                      /* */
    fis->d.lbaMidExp      = 0;                      /* */
    fis->d.lbaHighExp     = 0;                      /* */
    fis->d.featuresExp    = 0;                      /* */
    fis->d.sectorCount    = 0;                      /* */
    fis->d.sectorCountExp = 0;                      /* */
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatModeSelect6n10CB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
    return status;
  }
}


osGLOBAL bit32
smsatLogSense_1(
                smRoot_t                  *smRoot,
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               )
{
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;

  SM_DBG5(("smsatLogSense_1: start\n"));

  /* SAT Rev 8, 10.2.4 p74 */
  if ( pSatDevData->sat48BitSupport == agTRUE )
  {
    SM_DBG5(("smsatLogSense_1: case 2-1 sends READ LOG EXT\n"));
    /* sends READ LOG EXT */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_READ_LOG_EXT;       /* 0x2F */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = 0x07;                   /* 0x07 */
    fis->d.lbaMid         = 0;                      /*  */
    fis->d.lbaHigh        = 0;                      /*  */
    fis->d.device         = 0;                      /*  */
    fis->d.lbaLowExp      = 0;                      /*  */
    fis->d.lbaMidExp      = 0;                      /*  */
    fis->d.lbaHighExp     = 0;                      /*  */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 0x01;                     /* 1 sector counts */
    fis->d.sectorCountExp = 0x00;                      /* 1 sector counts */
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatLogSenseCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
    return status;

  }
  else
  {
    SM_DBG5(("smsatLogSense_1: case 2-2 sends SMART READ LOG\n"));
    /* sends SMART READ LOG */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_SMART;              /* 0x2F */
    fis->h.features       = SAT_SMART_READ_LOG;     /* 0xd5 */
    fis->d.lbaLow         = 0x06;                   /* 0x06 */
    fis->d.lbaMid         = 0x00;                   /* 0x4f */
    fis->d.lbaHigh        = 0x00;                   /* 0xc2 */
    fis->d.device         = 0;                      /*  */
    fis->d.lbaLowExp      = 0;                      /*  */
    fis->d.lbaMidExp      = 0;                      /*  */
    fis->d.lbaHighExp     = 0;                      /*  */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 0x01;                      /*  */
    fis->d.sectorCountExp = 0x00;                      /*  */
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &smsatLogSenseCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);
    return status;

  }
}

osGLOBAL bit32
smsatReassignBlocks_2(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext,
                      bit8                      *LBA
                     )
{
  /*
    assumes all LBA fits in ATA command; no boundary condition is checked here yet
    tiScsiRequest is TD generated for writing
  */
  bit32                     status;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smScsiRspSense_t          *pSense;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;
  SM_DBG5(("smsatReassignBlocks_2: start\n"));

  if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
  {
    /* case 2 */
    /* WRITE DMA*/
    /* can't fit the transfer length */
    SM_DBG5(("smsatReassignBlocks_2: case 2\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[7];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[6];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[5];                 /* FIS LBA (23:16) */

    /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[4] & 0xF));

    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
    satIOContext->ATACmd = SAT_WRITE_DMA;
  }
  else
  {
    /* case 1 */
    /* WRITE MULTIPLE or WRITE SECTOR(S) */
    /* WRITE SECTORS for easier implemetation */
    /* can't fit the transfer length */
    SM_DBG5(("smsatReassignBlocks_2: case 1\n"));
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
    fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[7];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[6];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[7];                 /* FIS LBA (23:16) */

    /* FIS LBA mode set LBA (27:24) */
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[4] & 0xF));

    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
    satIOContext->ATACmd = SAT_WRITE_SECTORS;
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      SM_DBG5(("smsatReassignBlocks_2: case 3\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* SAT_WRITE_DMA_FUA_EXT is optional and we don't support it */
      fis->h.command        = SAT_WRITE_DMA_EXT;      /* 0x35 */
      satIOContext->ATACmd  = SAT_WRITE_DMA_EXT;

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = LBA[7];                 /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = LBA[6];                 /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = LBA[5];                 /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = LBA[4];                 /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = LBA[3];                 /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = LBA[2];                 /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
    }
    else
    {
      /* case 4 */
      /* WRITE MULTIPLE EXT or WRITE MULTIPLE FUA EXT or WRITE SECTOR(S) EXT */
      /* WRITE SECTORS EXT for easier implemetation */
      SM_DBG5(("smsatReassignBlocks_2: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_WRITE_SECTORS_EXT;  /* 0x34 */

      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = LBA[7];                 /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = LBA[6];                 /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = LBA[5];                 /* FIS LBA (23:16) */
      fis->d.device         = 0x40;                   /* FIS LBA mode set */
      fis->d.lbaLowExp      = LBA[4];                 /* FIS LBA (31:24) */
      fis->d.lbaMidExp      = LBA[3];                 /* FIS LBA (39:32) */
      fis->d.lbaHighExp     = LBA[2];                 /* FIS LBA (47:40) */
      fis->d.featuresExp    = 0;                      /* FIS reserve */
      fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
      fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
      fis->d.reserved4      = 0;
      fis->d.control        = 0;                      /* FIS HOB bit clear */
      fis->d.reserved5      = 0;

      agRequestType = AGSA_SATA_PROTOCOL_PIO_WRITE;
      satIOContext->ATACmd = SAT_WRITE_SECTORS_EXT;
    }
  }
  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* WRITE FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      SM_DBG5(("smsatReassignBlocks_2: case 5 !!! error NCQ but 28 bit address support \n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                            satIOContext);

      /*smEnqueueIO(smRoot, satIOContext);*/

      tdsmIOCompletedCB( smRoot,
                         smIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satIOContext->pSmSenseData,
                         satIOContext->interruptContext );
      return SM_RC_SUCCESS;
    }
    SM_DBG6(("satWrite10: case 5\n"));

    /* Support 48-bit FPDMA addressing, use WRITE FPDMA QUEUE command */

    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_WRITE_FPDMA_QUEUED; /* 0x61 */
    fis->h.features       = 1;                      /* FIS sector count (7:0) */
    fis->d.lbaLow         = LBA[7];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[6];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[5];                 /* FIS LBA (23:16) */

    /* Check FUA bit */
    fis->d.device       = 0x40;                     /* FIS FUA clear */

    fis->d.lbaLowExp      = LBA[4];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = LBA[3];                 /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = LBA[2];                 /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS sector count (15:8) */
    fis->d.sectorCount    = 0;                      /* Tag (7:3) set by LL layer */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_FPDMA_WRITE;
    satIOContext->ATACmd = SAT_WRITE_FPDMA_QUEUED;
  }

  satIOContext->satCompleteCB = &smsatReassignBlocksCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            /* not the original, should be the TD generated one */
                            smScsiRequest,
                            satIOContext);
  return (status);
}

osGLOBAL bit32
smsatReassignBlocks_1(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext,
                      smSatIOContext_t            *satOrgIOContext
                     )
{
  /*
    assumes all LBA fits in ATA command; no boundary condition is checked here yet
    tiScsiRequest is OS generated; needs for accessing parameter list
  */
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  smIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pParmList;    /* Log Page data buffer */
  bit8                      LongLBA;
  bit8                      LBA[8];
  bit32                     startingIndex;

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &smScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pParmList     = (bit8 *) smScsiRequest->sglVirtualAddr;
  SM_DBG5(("smsatReassignBlocks_1: start\n"));
  LongLBA = (bit8)(scsiCmnd->cdb[1] & SCSI_REASSIGN_BLOCKS_LONGLBA_MASK);
  sm_memset(LBA, 0, sizeof(LBA));
  startingIndex = satOrgIOContext->ParmIndex;
  if (LongLBA == 0)
  {
    LBA[4] = pParmList[startingIndex];
    LBA[5] = pParmList[startingIndex+1];
    LBA[6] = pParmList[startingIndex+2];
    LBA[7] = pParmList[startingIndex+3];
    startingIndex = startingIndex + 4;
  }
  else
  {
    LBA[0] = pParmList[startingIndex];
    LBA[1] = pParmList[startingIndex+1];
    LBA[2] = pParmList[startingIndex+2];
    LBA[3] = pParmList[startingIndex+3];
    LBA[4] = pParmList[startingIndex+4];
    LBA[5] = pParmList[startingIndex+5];
    LBA[6] = pParmList[startingIndex+6];
    LBA[7] = pParmList[startingIndex+7];
    startingIndex = startingIndex + 8;
  }

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    /* sends READ VERIFY SECTOR(S) EXT*/
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = LBA[7];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[6];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[5];                 /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = LBA[4];                 /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = LBA[3];                 /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = LBA[2];                 /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
    fis->d.reserved4      = 0;
    fis->d.device         = 0x40;                   /* 01000000 */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;
  }
  else
  {
    /* READ VERIFY SECTOR(S)*/
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS;/* 0x40 */
    fis->h.features       = 0;                      /* FIS features NA       */
    fis->d.lbaLow         = LBA[7];                 /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = LBA[6];                 /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = LBA[5];                 /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.device         = (bit8)((0x4 << 4) | (LBA[4] & 0xF));
                            /* DEV and LBA 27:24 */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;
  }

  sm_memcpy(satOrgIOContext->LBA, LBA, 8);
  satOrgIOContext->ParmIndex = startingIndex;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatReassignBlocksCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  return SM_RC_SUCCESS;
}

osGLOBAL bit32
smsatSendReadLogExt(
                     smRoot_t                  *smRoot,
                     smIORequest_t             *smIORequest,
                     smDeviceHandle_t          *smDeviceHandle,
                     smScsiInitiatorRequest_t  *smScsiRequest,
                     smSatIOContext_t            *satIOContext
       )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;
  SM_DBG1(("smsatSendReadLogExt: start\n"));
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_READ_LOG_EXT;       /* 0x2F */
  fis->h.features       = 0;                      /* FIS reserve */
  fis->d.lbaLow         = 0x10;                   /* Page number */
  fis->d.lbaMid         = 0;                      /*  */
  fis->d.lbaHigh        = 0;                      /*  */
  fis->d.device         = 0;                      /* DEV is ignored in SATA */
  fis->d.lbaLowExp      = 0;                      /*  */
  fis->d.lbaMidExp      = 0;                      /*  */
  fis->d.lbaHighExp     = 0;                      /*  */
  fis->d.featuresExp    = 0;                      /* FIS reserve */
  fis->d.sectorCount    = 0x01;                   /*  1 sector counts*/
  fis->d.sectorCountExp = 0x00;                   /*  1 sector counts */
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatReadLogExtCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG1(("smsatSendReadLogExt: end status %d!!!\n", status));

  return (status);
}

osGLOBAL bit32
smsatCheckPowerMode(
                     smRoot_t                  *smRoot,
                     smIORequest_t             *smIORequest,
                     smDeviceHandle_t          *smDeviceHandle,
                     smScsiInitiatorRequest_t  *smScsiRequest,
                     smSatIOContext_t          *satIOContext
       )
{
  /*
    sends SAT_CHECK_POWER_MODE as a part of ABORT TASKMANGEMENT for NCQ commands
    internally generated - no directly corresponding scsi
  */
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;
  SM_DBG1(("smsatCheckPowerMode: start\n"));
  /*
   * Send the ATA CHECK POWER MODE command.
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_CHECK_POWER_MODE;   /* 0xE5 */
  fis->h.features       = 0;
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatCheckPowerModeCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG5(("smsatCheckPowerMode: return\n"));

  return status;
}

osGLOBAL bit32
smsatResetDevice(
                  smRoot_t                  *smRoot,
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest, /* NULL */
                  smSatIOContext_t            *satIOContext
                )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;
#ifdef  TD_DEBUG_ENABLE
  smIORequestBody_t         *smIORequestBody;
  smSatInternalIo_t           *satIntIoContext;
#endif

  fis           = satIOContext->pFis;
  SM_DBG1(("smsatResetDevice: start\n"));
#ifdef  TD_DEBUG_ENABLE
  satIntIoContext = satIOContext->satIntIoContext;
  smIORequestBody = satIntIoContext->satIntRequestBody;
#endif
  SM_DBG5(("smsatResetDevice: satIOContext %p smIORequestBody %p\n", satIOContext, smIORequestBody));
  /* any fis should work */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0;                      /* C Bit is not set */
  fis->h.command        = 0;                      /* any command */
  fis->h.features       = 0;                      /* FIS reserve */
  fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
  fis->d.device         = 0;                      /* FIS LBA mode  */
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0x4;                    /* SRST bit is set  */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_SRST_ASSERT;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatResetDeviceCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

#ifdef SM_INTERNAL_DEBUG
  smhexdump("smsatResetDevice", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
#ifdef  TD_DEBUG_ENABLE
  smhexdump("smsatResetDevice LL", (bit8 *)&(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev), sizeof(agsaFisRegHostToDevice_t));
#endif
#endif

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG6(("smsatResetDevice: end status %d\n", status));
  return status;
}

osGLOBAL bit32
smsatDeResetDevice(
                    smRoot_t                  *smRoot,
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;
#ifdef  TD_DEBUG_ENABLE
  smIORequestBody_t         *smIORequestBody;
  smSatInternalIo_t           *satIntIoContext;
#endif

  fis           = satIOContext->pFis;
  SM_DBG1(("smsatDeResetDevice: start\n"));
#ifdef  TD_DEBUG_ENABLE
  satIntIoContext = satIOContext->satIntIoContext;
  smIORequestBody = satIntIoContext->satIntRequestBody;
#endif
  SM_DBG5(("smsatDeResetDevice: satIOContext %p smIORequestBody %p\n", satIOContext, smIORequestBody));
  /* any fis should work */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0;                      /* C Bit is not set */
  fis->h.command        = 0;                      /* any command */
  fis->h.features       = 0;                      /* FIS reserve */
  fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = 0;                      /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = 0;                      /* FIS LBA (23:16) */
  fis->d.device         = 0;                      /* FIS LBA mode  */
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;                      /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                    /* SRST bit is not set  */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_SRST_DEASSERT;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatDeResetDeviceCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

#ifdef SM_INTERNAL_DEBUG
  smhexdump("smsatDeResetDevice", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
#ifdef  TD_DEBUG_ENABLE
  smhexdump("smsatDeResetDevice LL", (bit8 *)&(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev), sizeof(agsaFisRegHostToDevice_t));
#endif
#endif

  status = smsataLLIOStart( smRoot,
                            smIORequest,
                            smDeviceHandle,
                            smScsiRequest,
                            satIOContext);

  SM_DBG6(("smsatDeResetDevice: end status %d\n", status));
  return status;
}

/* set feature for auto activate */
osGLOBAL bit32
smsatSetFeaturesAA(
           smRoot_t                  *smRoot,
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t          *satIOContext
           )
{
  bit32                     status = SM_RC_FAILURE;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;
  SM_DBG2(("smsatSetFeaturesAA: start\n"));
  /*
   * Send the Set Features command.
   * See SATA II 1.0a spec
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
  fis->h.features       = 0x10;                   /* enable SATA feature */
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0x02;                   /* DMA Setup FIS Auto-Activate */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatSetFeaturesAACB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                          smIORequest,
                          smDeviceHandle,
                          smScsiRequest,
                          satIOContext);

  /* debugging code */
  if (smIORequest->tdData == smIORequest->smData)
  {
    SM_DBG1(("smsatSetFeaturesAA: incorrect smIORequest\n"));
  }
  SM_DBG2(("smsatSetFeatures: return\n"));
  return status;
}


/* set feature for DMA transfer mode*/
osGLOBAL bit32
smsatSetFeaturesDMA(
           smRoot_t                  *smRoot,
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t          *satIOContext
           )
{
  bit32                     status = SM_RC_FAILURE;
  bit32                     agRequestType;
  smDeviceData_t            *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;
  SM_DBG2(("smsatSetFeaturesDMA: start\n"));
  /*
   * Send the Set Features command.
   * See SATA II 1.0a spec
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
  fis->h.features       = 0x03;                   /* enable ATA transfer mode */
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0x40 |(bit8)pSatDevData->satUltraDMAMode;   /* enable Ultra DMA mode */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatSetFeaturesDMACB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                          smIORequest,
                          smDeviceHandle,
                          smScsiRequest,
                          satIOContext);

  /* debugging code */
  if (smIORequest->tdData == smIORequest->smData)
  {
    SM_DBG1(("smsatSetFeaturesDMA: incorrect smIORequest\n"));
  }

  SM_DBG2(("smsatSetFeaturesDMA: return\n"));

  return status;
}

/* set feature for Read Look Ahead*/
osGLOBAL bit32
smsatSetFeaturesReadLookAhead(
           smRoot_t                  *smRoot,
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t          *satIOContext
           )
{
  bit32                     status = SM_RC_FAILURE;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;
  SM_DBG2(("smsatSetFeaturesReadLookAhead: start\n"));
  /*
   * Send the Set Features command.
   * See SATA II 1.0a spec
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
  fis->h.features       = 0xAA;                   /* Enable read look-ahead feature */
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatSetFeaturesReadLookAheadCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                          smIORequest,
                          smDeviceHandle,
                          smScsiRequest,
                          satIOContext);

  /* debugging code */
  if (smIORequest->tdData == smIORequest->smData)
  {
    SM_DBG1(("smsatSetFeaturesReadLookAhead: incorrect smIORequest\n"));
  }

  SM_DBG2(("smsatSetFeaturesReadLookAhead: return\n"));

  return status;
}

/* set feature for Volatile Write Cache*/
osGLOBAL bit32
smsatSetFeaturesVolatileWriteCache(
           smRoot_t                  *smRoot,
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t            *satIOContext
           )
{
  bit32                     status = SM_RC_FAILURE;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;
  SM_DBG2(("smsatSetFeaturesVolatileWriteCache: start\n"));
  /*
   * Send the Set Features command.
   * See SATA II 1.0a spec
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_SET_FEATURES;       /* 0xEF */
  fis->h.features       = 0x02;                   /* Enable Volatile Write Cache feature */
  fis->d.lbaLow         = 0;
  fis->d.lbaMid         = 0;
  fis->d.lbaHigh        = 0;
  fis->d.device         = 0;
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &smsatSetFeaturesVolatileWriteCacheCB;
  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = smsataLLIOStart( smRoot,
                          smIORequest,
                          smDeviceHandle,
                          smScsiRequest,
                          satIOContext);
  /* debugging code */
  if (smIORequest->tdData == smIORequest->smData)
  {
    SM_DBG1(("smsatSetFeaturesVolatileWriteCache: incorrect smIORequest\n"));
  }
  SM_DBG2(("smsatSetFeaturesVolatileWriteCache: return\n"));

  return status;
}



/******************************** start of utils    ***********************************************************/
osGLOBAL FORCEINLINE void
smsatBitSet(smRoot_t *smRoot, bit8 *data, bit32 index)
{
  data[index>>3] |= (1 << (index&7));
}

osGLOBAL FORCEINLINE void
smsatBitClear(smRoot_t *smRoot, bit8 *data, bit32 index)
{
  data[index>>3] &= ~(1 << (index&7));
}

osGLOBAL FORCEINLINE BOOLEAN
smsatBitTest(smRoot_t *smRoot, bit8 *data, bit32 index)
{
   return ( (BOOLEAN)((data[index>>3] & (1 << (index&7)) ) ? 1: 0));
}


FORCEINLINE bit32
smsatTagAlloc(
               smRoot_t         *smRoot,
               smDeviceData_t   *pSatDevData,
               bit8             *pTag
             )
{
  bit32             retCode = agFALSE;
  bit32             i;

  tdsmSingleThreadedEnter(smRoot, SM_NCQ_TAG_LOCK);

#ifdef CCFLAG_OPTIMIZE_SAT_LOCK

  if (tdsmBitScanForward(smRoot, &i, ~(pSatDevData->freeSATAFDMATagBitmap)))
  {
    smsatBitSet(smRoot, (bit8*)&pSatDevData->freeSATAFDMATagBitmap, i);
    *pTag = (bit8)i;
    retCode = agTRUE;
  }

#else

  for ( i = 0; i < pSatDevData->satNCQMaxIO; i ++ )
  {
    if ( 0 == smsatBitTest(smRoot, (bit8 *)&pSatDevData->freeSATAFDMATagBitmap, i) )
    {
      smsatBitSet(smRoot, (bit8*)&pSatDevData->freeSATAFDMATagBitmap, i);
      *pTag = (bit8) i;
      retCode = agTRUE;
      break;
    }
  }

#endif

  tdsmSingleThreadedLeave(smRoot, SM_NCQ_TAG_LOCK);

  return retCode;
}

FORCEINLINE bit32
smsatTagRelease(
                smRoot_t         *smRoot,
                smDeviceData_t   *pSatDevData,
                bit8              tag
               )
{
  bit32             retCode = agFALSE;

  if ( tag < pSatDevData->satNCQMaxIO )
  {
    tdsmSingleThreadedEnter(smRoot, SM_NCQ_TAG_LOCK);
    smsatBitClear(smRoot, (bit8 *)&pSatDevData->freeSATAFDMATagBitmap, (bit32)tag);
    tdsmSingleThreadedLeave(smRoot, SM_NCQ_TAG_LOCK);
    /*tdsmInterlockedAnd(smRoot, (volatile LONG *)(&pSatDevData->freeSATAFDMATagBitmap), ~(1 << (tag&31)));*/
    retCode = agTRUE;
  }
  else
  {
    SM_DBG1(("smsatTagRelease: tag %d >= satNCQMaxIO %d!!!!\n", tag, pSatDevData->satNCQMaxIO));
  }
  return retCode;
}



osGLOBAL bit32
smsatComputeCDB10LBA(smSatIOContext_t            *satIOContext)
{
  smIniScsiCmnd_t           *scsiCmnd;
  smScsiInitiatorRequest_t  *smScsiRequest;
  bit32                     lba = 0;

  SM_DBG5(("smsatComputeCDB10LBA: start\n"));
  smScsiRequest = satIOContext->smScsiXchg;
  scsiCmnd      = &(smScsiRequest->scsiCmnd);

  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];

  return lba;
}

osGLOBAL bit32
smsatComputeCDB10TL(smSatIOContext_t            *satIOContext)
{

  smIniScsiCmnd_t           *scsiCmnd;
  smScsiInitiatorRequest_t  *smScsiRequest;
  bit32                     tl = 0;

  SM_DBG5(("smsatComputeCDB10TL: start\n"));
  smScsiRequest = satIOContext->smScsiXchg;
  scsiCmnd      = &(smScsiRequest->scsiCmnd);

  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];
  return tl;
}

osGLOBAL bit32
smsatComputeCDB12LBA(smSatIOContext_t            *satIOContext)
{
  smIniScsiCmnd_t           *scsiCmnd;
  smScsiInitiatorRequest_t  *smScsiRequest;
  bit32                     lba = 0;

  SM_DBG5(("smsatComputeCDB12LBA: start\n"));
  smScsiRequest = satIOContext->smScsiXchg;
  scsiCmnd      = &(smScsiRequest->scsiCmnd);

  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];

  return lba;
}

osGLOBAL bit32
smsatComputeCDB12TL(smSatIOContext_t            *satIOContext)
{

  smIniScsiCmnd_t           *scsiCmnd;
  smScsiInitiatorRequest_t  *smScsiRequest;
  bit32                     tl = 0;

  SM_DBG5(("smsatComputeCDB12TL: start\n"));
  smScsiRequest = satIOContext->smScsiXchg;
  scsiCmnd      = &(smScsiRequest->scsiCmnd);

  tl = (scsiCmnd->cdb[6] << (8*3)) + (scsiCmnd->cdb[7] << (8*2))
    + (scsiCmnd->cdb[8] << 8) + scsiCmnd->cdb[9];
  return tl;
}

/*
  CBD16 has bit64 LBA
  But it has to be less than (2^28 - 1)
  Therefore, use last four bytes to compute LBA is OK
*/
osGLOBAL bit32
smsatComputeCDB16LBA(smSatIOContext_t            *satIOContext)
{
  smIniScsiCmnd_t           *scsiCmnd;
  smScsiInitiatorRequest_t  *smScsiRequest;
  bit32                     lba = 0;

  SM_DBG5(("smsatComputeCDB16LBA: start\n"));
  smScsiRequest = satIOContext->smScsiXchg;
  scsiCmnd      = &(smScsiRequest->scsiCmnd);

  lba = (scsiCmnd->cdb[6] << (8*3)) + (scsiCmnd->cdb[7] << (8*2))
    + (scsiCmnd->cdb[8] << 8) + scsiCmnd->cdb[9];

  return lba;
}

osGLOBAL bit32
smsatComputeCDB16TL(smSatIOContext_t            *satIOContext)
{

  smIniScsiCmnd_t           *scsiCmnd;
  smScsiInitiatorRequest_t  *smScsiRequest;
  bit32                     tl = 0;

  SM_DBG5(("smsatComputeCDB16TL: start\n"));
  smScsiRequest = satIOContext->smScsiXchg;
  scsiCmnd      = &(smScsiRequest->scsiCmnd);

  tl = (scsiCmnd->cdb[10] << (8*3)) + (scsiCmnd->cdb[11] << (8*2))
    + (scsiCmnd->cdb[12] << 8) + scsiCmnd->cdb[13];
  return tl;
}

/*
  (tl, denom)
  tl can be upto bit32 because CDB16 has bit32 tl
  Therefore, fine
  either (tl, 0xFF) or (tl, 0xFFFF)
*/
osGLOBAL FORCEINLINE bit32
smsatComputeLoopNum(bit32 a, bit32 b)
{
  bit32 LoopNum = 0;

  SM_DBG5(("smsatComputeLoopNum: start\n"));

  if (a < b || a == 0)
  {
    LoopNum = 1;
  }
  else
  {
    if (a == b || a == 0)
    {
      LoopNum = a/b;
    }
    else
    {
      LoopNum = a/b + 1;
    }
  }

  return LoopNum;
}

/*
  Generic new function for checking
  LBA itself, LBA+TL < SAT_TR_LBA_LIMIT or SAT_EXT_TR_LBA_LIMIT 
  and LBA+TL < Read Capacity Limit
  flag: false - not 48BitSupport; true - 48BitSupport
  returns TRUE when over the limit
  
*/
osGLOBAL FORCEINLINE bit32
smsatCheckLimit(bit8 *lba, bit8 *tl, int flag, smDeviceData_t *pSatDevData)
{
  bit32 lbaCheck = agFALSE;
  int i;
  bit8 limit[8];
  bit32 rangeCheck = agFALSE;
  bit16 ans[8];       // 0 MSB, 8 LSB
  bit8  final_ans[9]; // 0 MSB, 9 LSB
  bit8  Bit28max[8];
  bit8  Bit48max[8];
  bit32 ReadCapCheck = agFALSE;
  bit32 ret;
  
  bit8  final_satMaxLBA[9];
  bit8  oneTL[8];
  bit8  temp_satMaxLBA[8];       // 0 MSB, 8 LSB  
  /* 
    check LBA
  */
  if (flag == agFALSE)
  {
    /* limit is 0xF FF FF = 2^28 - 1 */
    limit[0] = 0x0;   /* MSB */
    limit[1] = 0x0;
    limit[2] = 0x0;
    limit[3] = 0x0;
    limit[4] = 0xF;
    limit[5] = 0xFF;
    limit[6] = 0xFF;
    limit[7] = 0xFF;  /* LSB */ 
  }
  else 
  {
    /* limit is 0xF FF FF = 2^48 - 1 */
    limit[0] = 0x0;   /* MSB */
    limit[1] = 0x0;
    limit[2] = 0xFF;
    limit[3] = 0xFF;
    limit[4] = 0xFF;
    limit[5] = 0xFF;
    limit[6] = 0xFF;
    limit[7] = 0xFF;  /* LSB */
  }
  //compare lba to limit
  for(i=0;i<8;i++)
  {
    if (lba[i] > limit[i])
    {
      SM_DBG1(("smsatCheckLimit: LBA check True at %d\n", i));
      lbaCheck = agTRUE;
      break;
    }
    else if (lba[i] < limit[i])
    {
      SM_DBG5(("smsatCheckLimit: LBA check False at %d\n", i));
      lbaCheck = agFALSE;
      break;
    }
    else
    {
      continue;
    }
  }
  
  if (lbaCheck == agTRUE)
  {
    SM_DBG1(("smsatCheckLimit: return LBA check True\n"));
    return agTRUE;
  }
  
  /*
    check LBA+TL < SAT_TR_LBA_LIMIT or SAT_EXT_TR_LBA_LIMIT 
  */      
  sm_memset(ans, 0, sizeof(ans));
  sm_memset(final_ans, 0, sizeof(final_ans));
  
  // adding from LSB to MSB
  for(i=7;i>=0;i--)
  {
    ans[i] = (bit16)(lba[i] + tl[i]);
    if (i != 7)
    {
      ans[i] = (bit16)(ans[i] + ((ans[i+1] & 0xFF00) >> 8));
    }
  }

  /*
    filling in the final answer
   */
  final_ans[0] = (bit8)(((ans[0] & 0xFF00) >> 8));

  for(i=1;i<=8;i++)
  {
    final_ans[i] = (bit8)(ans[i-1] & 0xFF);
  }

  
  if (flag == agFALSE)
  {
    sm_memset(Bit28max, 0, sizeof(Bit28max));
    Bit28max[4] = 0x10; // max =0x1000 0000
  
    //compare final_ans to max
    if (final_ans[0] != 0 || final_ans[1] != 0 || final_ans[2] != 0 
        || final_ans[3] != 0 || final_ans[4] != 0)
    {
      SM_DBG1(("smsatCheckLimit: before 28Bit addressing TRUE\n"));
      rangeCheck = agTRUE;
    }
    else
    {
      for(i=5;i<=8;i++)
      {
        if (final_ans[i] > Bit28max[i-1])
        {
          SM_DBG1(("smsatCheckLimit: 28Bit addressing TRUE at %d\n", i));
          rangeCheck = agTRUE;
          break;
        }
        else if (final_ans[i] < Bit28max[i-1])
        {
          SM_DBG5(("smsatCheckLimit: 28Bit addressing FALSE at %d\n", i));
          rangeCheck = agFALSE;
          break;
        }
        else
        {
          continue;
        }
      }
    }	  
  }
  else
  {
    sm_memset(Bit48max, 0, sizeof(Bit48max));
    Bit48max[1] = 0x1; //max = 0x1 0000 0000 0000
    
    //compare final_ans to max
    if (final_ans[0] != 0 || final_ans[1] != 0)
    {
      SM_DBG1(("smsatCheckLimit: before 48Bit addressing TRUE\n"));
      rangeCheck = agTRUE;
    }
    else
    {
      for(i=2;i<=8;i++)
      {
        if (final_ans[i] > Bit48max[i-1])
        {
          SM_DBG1(("smsatCheckLimit: 48Bit addressing TRUE at %d\n", i));
          rangeCheck = agTRUE;
	  break;
        }
        else if (final_ans[i] < Bit48max[i-1])
        {
          SM_DBG5(("smsatCheckLimit: 48Bit addressing FALSE at %d\n", i));
          rangeCheck = agFALSE;
	  break;
        }
        else
        {
          continue;
        }
      }
    }  
  }  
  if (rangeCheck == agTRUE)
  {
    SM_DBG1(("smsatCheckLimit: return rangeCheck True\n"));
    return agTRUE;
  }
  
  /*  
    LBA+TL < Read Capacity Limit
  */
  sm_memset(temp_satMaxLBA, 0, sizeof(temp_satMaxLBA));
  sm_memset(oneTL, 0, sizeof(oneTL));
  sm_memset(final_satMaxLBA, 0, sizeof(final_satMaxLBA));  
  sm_memset(ans, 0, sizeof(ans));

  sm_memcpy(&temp_satMaxLBA, &pSatDevData->satMaxLBA, sizeof(temp_satMaxLBA));
  oneTL[7] = 1;
    
  // adding temp_satMaxLBA to oneTL
  for(i=7;i>=0;i--)
  {
    ans[i] = (bit16)(temp_satMaxLBA[i] + oneTL[i]);
    if (i != 7)
    {
      ans[i] = (bit16)(ans[i] + ((ans[i+1] & 0xFF00) >> 8));
    }
  }

  /*
    filling in the final answer
   */
  final_satMaxLBA[0] = (bit8)(((ans[0] & 0xFF00) >> 8));

  for(i=1;i<=8;i++)
  {
    final_satMaxLBA[i] = (bit8)(ans[i-1] & 0xFF);
  }  
  if ( pSatDevData->ReadCapacity == 10)
  {
    for (i=0;i<=8;i++)
    {
      if (final_ans[i] > final_satMaxLBA[i])
      {
        SM_DBG1(("smsatCheckLimit: Read Capacity 10 TRUE at %d\n", i));
        ReadCapCheck = agTRUE;
        break;
      }
      else if (final_ans[i] < final_satMaxLBA[i])
      {
        SM_DBG5(("smsatCheckLimit: Read Capacity 10 FALSE at %d\n", i));
        ReadCapCheck = agFALSE;
        break;
      }
      else
      {
        continue;
      }  
    }
    if ( ReadCapCheck)
    {
      SM_DBG1(("smsatCheckLimit: after Read Capacity 10 TRUE\n"));
    }
    else
    {
      SM_DBG5(("smsatCheckLimit: after Read Capacity 10 FALSE\n"));
    }  
  }    
  else if ( pSatDevData->ReadCapacity == 16)
  {
    for (i=0;i<=8;i++)
    {
      if (final_ans[i] > final_satMaxLBA[i])
      {
        SM_DBG1(("smsatCheckLimit: Read Capacity 16 TRUE at %d\n", i));
        ReadCapCheck = agTRUE;
        break;
      }
      else if (final_ans[i] < final_satMaxLBA[i])
      {
        SM_DBG5(("smsatCheckLimit: Read Capacity 16 FALSE at %d\n", i));
        ReadCapCheck = agFALSE;
        break;
      }
      else
      {
        continue;
      }  
    }
    if ( ReadCapCheck)
    {
      SM_DBG1(("smsatCheckLimit: after Read Capacity 16 TRUE\n"));
    }
    else
    {
      SM_DBG5(("smsatCheckLimit: after Read Capacity 16 FALSE\n"));
    }  
  }
  else
  {
    SM_DBG5(("smsatCheckLimit: unknown pSatDevData->ReadCapacity %d\n", pSatDevData->ReadCapacity));  
  }
  
  if (ReadCapCheck == agTRUE)
  {
    SM_DBG1(("smsatCheckLimit: return ReadCapCheck True\n"));
    return agTRUE;
  }


  ret = (lbaCheck | rangeCheck | ReadCapCheck);
  if (ret == agTRUE)
  {
    SM_DBG1(("smsatCheckLimit: final check TRUE\n"));  
  }
  else
  {
    SM_DBG5(("smsatCheckLimit: final check FALSE\n"));  
  }
  return   ret;
}



osGLOBAL void
smsatPrintSgl(
            smRoot_t                  *smRoot,
            agsaEsgl_t                *agEsgl,
      bit32                     idx
      )
{
  bit32                     i=0;
#ifdef  TD_DEBUG_ENABLE
  agsaSgl_t                 *agSgl;
#endif

  for (i=0;i<idx;i++)
  {
#ifdef  TD_DEBUG_ENABLE
    agSgl = &(agEsgl->descriptor[i]);
#endif
    SM_DBG3(("smsatPrintSgl: agSgl %d upperAddr 0x%08x lowerAddr 0x%08x len 0x%08x ext 0x%08x\n",
      i, agSgl->sgUpper, agSgl->sgLower, agSgl->len,  agSgl->extReserved));
  }

  return;
}


osGLOBAL void
smsatSplitSGL(
     smRoot_t                  *smRoot,
     smIORequest_t             *smIORequest,
     smDeviceHandle_t          *smDeviceHandle,
     smScsiInitiatorRequest_t  *smScsiRequest,
     smSatIOContext_t          *satIOContext,
     bit32                     split, /*in sector number, depeding on IO value */
     bit32                     tl, /* in sector number */
     bit32                     flag
    )
{
  agsaSgl_t                 *agSgl;
  agsaEsgl_t                *agEsgl;
  bit32                     i=0;
  smIniScsiCmnd_t           *scsiCmnd;
  bit32                     totalLen=0; /* in bytes */
  bit32                     splitLen=0; /* in bytes */
  bit32                     splitDiffByte = 0; /* in bytes */
  bit32                     splitDiffExtra = 0; /* in bytes */
  bit32                     splitIdx = 0;
  bit32                     UpperAddr, LowerAddr;
  bit32                     tmpLowerAddr;
  void                      *sglVirtualAddr;
  void                      *sglSplitVirtualAddr;

  scsiCmnd      = &smScsiRequest->scsiCmnd;
  SM_DBG3(("smsatSplitSGL: start\n"));

  if (smScsiRequest->smSgl1.type == 0x80000000) /* esgl */
  {
    if (flag == agFALSE)
    {
      SM_DBG3(("smsatSplitSGL: Not first time\n"));
      SM_DBG3(("smsatSplitSGL: UpperAddr 0x%08x LowerAddr 0x%08x\n", satIOContext->UpperAddr, satIOContext->LowerAddr));
      SM_DBG3(("smsatSplitSGL: SplitIdx %d AdjustBytes 0x%08x\n", satIOContext->SplitIdx, satIOContext->AdjustBytes));

      sglVirtualAddr = smScsiRequest->sglVirtualAddr;

      agEsgl = (agsaEsgl_t *)smScsiRequest->sglVirtualAddr;

      sglSplitVirtualAddr = &(agEsgl->descriptor[satIOContext->SplitIdx]);

      agEsgl = (agsaEsgl_t *)sglSplitVirtualAddr;

      if (agEsgl == agNULL)
      {
        SM_DBG1(("smsatSplitSGL: error!\n"));
        return;
      }
      /* first sgl ajustment */
      agSgl = &(agEsgl->descriptor[0]);
      agSgl->sgUpper = satIOContext->UpperAddr;
      agSgl->sgLower = satIOContext->LowerAddr;
      agSgl->len = satIOContext->AdjustBytes;
      sm_memcpy(sglVirtualAddr, sglSplitVirtualAddr, (satIOContext->EsglLen) * sizeof(agsaSgl_t));
      agEsgl = (agsaEsgl_t *)smScsiRequest->sglVirtualAddr;
      smsatPrintSgl(smRoot, (agsaEsgl_t *)sglVirtualAddr, satIOContext->EsglLen);
    }
    else
    {
      /* first time */
      SM_DBG3(("smsatSplitSGL: first time\n"));
      satIOContext->EsglLen = smScsiRequest->smSgl1.len;
      agEsgl = (agsaEsgl_t *)smScsiRequest->sglVirtualAddr;
      if (agEsgl == agNULL)
      {
        return;
      }
      smsatPrintSgl(smRoot, agEsgl, satIOContext->EsglLen);
    }

    if (tl > split)
    {
      /* split */
      SM_DBG3(("smsatSplitSGL: split case\n"));
      i = 0;
      while (1)
      {
        agSgl = &(agEsgl->descriptor[i]);
        splitLen = splitLen + agSgl->len;
        if (splitLen >= split)
        {
          splitDiffExtra = splitLen - split;
          splitDiffByte = agSgl->len - splitDiffExtra;
          splitIdx = i;
          break;
        }
        i++;
      }
      SM_DBG3(("smsatSplitSGL: splitIdx %d\n", splitIdx));
      SM_DBG3(("smsatSplitSGL: splitDiffByte 0x%8x\n", splitDiffByte));
      SM_DBG3(("smsatSplitSGL: splitDiffExtra 0x%8x \n", splitDiffExtra));


      agSgl = &(agEsgl->descriptor[splitIdx]);
      UpperAddr = agSgl->sgUpper;
      LowerAddr = agSgl->sgLower;
      tmpLowerAddr = LowerAddr + splitDiffByte;
      if (tmpLowerAddr < LowerAddr)
      {
        UpperAddr = UpperAddr + 1;
      }
      SM_DBG3(("smsatSplitSGL: UpperAddr 0x%08x tmpLowerAddr 0x%08x\n", UpperAddr, tmpLowerAddr));
      agSgl->len = splitDiffByte;
      /* Esgl len adjustment */
      smScsiRequest->smSgl1.len =  splitIdx;
      /* expected data lent adjustment */
      scsiCmnd->expDataLength = 0x20000;
      /* remeber for the next round */
      satIOContext->UpperAddr = UpperAddr;
      satIOContext->LowerAddr = tmpLowerAddr;
      satIOContext->SplitIdx = splitIdx;
      satIOContext->AdjustBytes = splitDiffExtra;
      satIOContext->EsglLen =  satIOContext->EsglLen - smScsiRequest->smSgl1.len;
      satIOContext->OrgTL = satIOContext->OrgTL - 0x100;
//    smsatPrintSgl(smRoot, agEsgl, satIOContext->EsglLen);

    }
    else
    {
      /* no split */
      SM_DBG3(("smsatSplitSGL: no split case\n"));
      /* Esgl len adjustment */
      smScsiRequest->smSgl1.len = satIOContext->EsglLen;
      for (i=0;i< smScsiRequest->smSgl1.len;i++)
      {
        agSgl = &(agEsgl->descriptor[i]);
        totalLen = totalLen + (agSgl->len);
      }
      /* expected data lent adjustment */
      scsiCmnd->expDataLength = totalLen;
//    smsatPrintSgl(smRoot, agEsgl, satIOContext->EsglLen);
    }
  }
  else
  {
    SM_DBG1(("not exntened esgl\n"));

  }

  return;
}


/******************************** end   of utils    ***********************************************************/



