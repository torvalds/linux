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

/*
 * This table is used to map LL Layer saSATAStart() status to TISA status.
 */


FORCEINLINE bit32  
smsataLLIOStart(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t          *satIOContext
               )
{
  smDeviceData_t              *oneDeviceData  = (smDeviceData_t *)smDeviceHandle->smData;
  smIntRoot_t                 *smIntRoot      = (smIntRoot_t *) smRoot->smData;
  smIntContext_t              *smAllShared    = (smIntContext_t *)&(smIntRoot->smAllShared);
  smIORequestBody_t           *smIORequestBody = (smIORequestBody_t *)satIOContext->smRequestBody;
  smDeviceData_t              *pSatDevData   = satIOContext->pSatDevData;
  smSatInternalIo_t           *satIntIo      = satIOContext->satIntIoContext;
  agsaRoot_t                  *agRoot        = smAllShared->agRoot;
  agsaIORequest_t             *agIORequest   = &(smIORequestBody->agIORequest);
  agsaDevHandle_t             *agDevHandle   = oneDeviceData->agDevHandle;
  agsaSATAInitiatorRequest_t  *agSATAReq     = &(smIORequestBody->transport.SATA.agSATARequestBody);
  bit32                       RLERecovery    = agFALSE;
  bit32                       status         = SM_RC_FAILURE;
  bit32                       nQNumber       = 0;
  /* 
   * If this is a super I/O request, check for optional settings.
   * Be careful. Use the superRequest pointer for all references 
   * in this block of code.
   */
  agSATAReq->option = 0;
  if (satIOContext->superIOFlag)
  {
    smSuperScsiInitiatorRequest_t *superRequest = (smSuperScsiInitiatorRequest_t *) smScsiRequest;

    if (superRequest->flags & SM_SCSI_INITIATOR_ENCRYPT)
    {
      /* Copy all of the relevant encrypt information  */
      agSATAReq->option |= AGSA_SATA_ENABLE_ENCRYPTION;
      sm_memcpy(&agSATAReq->encrypt, &superRequest->Encrypt, sizeof(agsaEncrypt_t));
    }
    {
      /* initialize expDataLength */
      if (satIOContext->reqType == AGSA_SATA_PROTOCOL_NON_DATA ||  
          satIOContext->reqType == AGSA_SATA_PROTOCOL_SRST_ASSERT ||
          satIOContext->reqType == AGSA_SATA_PROTOCOL_SRST_DEASSERT )
      { 
        smIORequestBody->IOType.InitiatorRegIO.expDataLength = 0;
      }
      else
      {
        smIORequestBody->IOType.InitiatorRegIO.expDataLength = smScsiRequest->scsiCmnd.expDataLength;
      }
          
      agSATAReq->dataLength = smIORequestBody->IOType.InitiatorRegIO.expDataLength;
    }
  } 
  else
  {
    /* initialize expDataLength */
    if (satIOContext->reqType == AGSA_SATA_PROTOCOL_NON_DATA ||  
        satIOContext->reqType == AGSA_SATA_PROTOCOL_SRST_ASSERT ||
        satIOContext->reqType == AGSA_SATA_PROTOCOL_SRST_DEASSERT )
    { 
      smIORequestBody->IOType.InitiatorRegIO.expDataLength = 0;
    }
    else
    {
      smIORequestBody->IOType.InitiatorRegIO.expDataLength = smScsiRequest->scsiCmnd.expDataLength;
    }

    agSATAReq->dataLength = smIORequestBody->IOType.InitiatorRegIO.expDataLength;
  }

  if ( (pSatDevData->satDriveState == SAT_DEV_STATE_IN_RECOVERY) && 
       (satIOContext->pFis->h.command == SAT_READ_LOG_EXT) )
  {
     RLERecovery = agTRUE;
  }

  /* check max io, be sure to free */
  if ( (pSatDevData->satDriveState != SAT_DEV_STATE_IN_RECOVERY) ||
       (RLERecovery == agTRUE) )  
  {
    if (RLERecovery == agFALSE) /* RLE is not checked against pending IO's */
    {
#ifdef CCFLAG_OPTIMIZE_SAT_LOCK
      bit32 volatile satPendingNCQIO = 0;
      bit32 volatile satPendingNONNCQIO = 0;
      bit32 volatile satPendingIO = 0;

      tdsmInterlockedExchange(smRoot, &satPendingNCQIO, pSatDevData->satPendingNCQIO);
      tdsmInterlockedExchange(smRoot, &satPendingNONNCQIO, pSatDevData->satPendingNONNCQIO);
      tdsmInterlockedExchange(smRoot, &satPendingIO, pSatDevData->satPendingIO);
#endif
    
      if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
           (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
      {
      #ifdef CCFLAG_OPTIMIZE_SAT_LOCK
        if ( satPendingNCQIO >= pSatDevData->satNCQMaxIO ||
             satPendingNONNCQIO != 0)
        {
          SM_DBG1(("smsataLLIOStart: 1st busy did %d!!!\n", pSatDevData->id));
          SM_DBG1(("smsataLLIOStart: 1st busy NCQ. NCQ Pending 0x%x NONNCQ Pending 0x%x All Pending 0x%x!!!\n", satPendingNCQIO, 
                    satPendingNONNCQIO, satPendingIO));
          /* free resource */
          smsatFreeIntIoResource( smRoot,
                                  pSatDevData,
                                  satIntIo); 
          return SM_RC_DEVICE_BUSY;
        }
      #else
        tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
        if (pSatDevData->satPendingNCQIO >= pSatDevData->satNCQMaxIO ||
            pSatDevData->satPendingNONNCQIO != 0)
        {
          SM_DBG1(("smsataLLIOStart: 1st busy did %d!!!\n", pSatDevData->id));
          SM_DBG1(("smsataLLIOStart: 1st busy NCQ. NCQ Pending 0x%x NONNCQ Pending 0x%x All Pending 0x%x!!!\n", pSatDevData->satPendingNCQIO, 
                    pSatDevData->satPendingNONNCQIO, pSatDevData->satPendingIO));
          tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
          /* free resource */
          smsatFreeIntIoResource( smRoot,
                                  pSatDevData,
                                  satIntIo); 
          return SM_RC_DEVICE_BUSY;
        }
        tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
      #endif
      
      }
      else if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_D2H_PKT) ||
                (satIOContext->reqType == AGSA_SATA_PROTOCOL_H2D_PKT) ||
                (satIOContext->reqType == AGSA_SATA_PROTOCOL_NON_PKT) )
      {
        sm_memcpy(agSATAReq->scsiCDB, smScsiRequest->scsiCmnd.cdb, 16);    
      #ifdef CCFLAG_OPTIMIZE_SAT_LOCK
        if ( satPendingNONNCQIO >= SAT_APAPI_CMDQ_MAX ||
             satPendingNCQIO != 0)
        {
          SM_DBG1(("smsataLLIOStart: ATAPI busy did %d!!!\n", pSatDevData->id));
          SM_DBG1(("smsataLLIOStart: ATAPI busy NON-NCQ. NCQ Pending 0x%x NON-NCQ Pending 0x%x All Pending 0x%x!!!\n", satPendingNCQIO, 
                    satPendingNONNCQIO, satPendingIO));
          /* free resource */
          smsatFreeIntIoResource( smRoot,
                                  pSatDevData,
                                  satIntIo); 
          return SM_RC_DEVICE_BUSY;
        }
      #else
        tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
        if ( pSatDevData->satPendingNONNCQIO >= SAT_APAPI_CMDQ_MAX ||
             pSatDevData->satPendingNCQIO != 0)
        {
          SM_DBG1(("smsataLLIOStart: ATAPI busy did %d!!!\n", pSatDevData->id));
          SM_DBG1(("smsataLLIOStart: ATAPI busy NON-NCQ. NCQ Pending 0x%x NON-NCQ Pending 0x%x All Pending 0x%x!!!\n", pSatDevData->satPendingNCQIO, 
                    pSatDevData->satPendingNONNCQIO, pSatDevData->satPendingIO));
          tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
          /* free resource */
          smsatFreeIntIoResource( smRoot,
                                  pSatDevData,
                                  satIntIo); 
          return SM_RC_DEVICE_BUSY;
        }
        tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
      #endif
      
      }
      else
      {
#ifdef CCFLAG_OPTIMIZE_SAT_LOCK
        if ( satPendingNONNCQIO >= SAT_NONNCQ_MAX ||
             satPendingNCQIO != 0)
        {
          SM_DBG1(("smsataLLIOStart: 2nd busy did %d!!!\n", pSatDevData->id));
          SM_DBG1(("smsataLLIOStart: 2nd busy NCQ. NCQ Pending 0x%x NONNCQ Pending 0x%x All Pending 0x%x!!!\n", satPendingNCQIO, 
                    satPendingNONNCQIO, satPendingIO));
          /* free resource */
          smsatFreeIntIoResource( smRoot,
                                  pSatDevData,
                                  satIntIo); 
          return SM_RC_DEVICE_BUSY;
        }
#else
        tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
        if (pSatDevData->satPendingNONNCQIO >= SAT_NONNCQ_MAX ||
            pSatDevData->satPendingNCQIO != 0)
        {
          SM_DBG1(("smsataLLIOStart: 2nd busy did %d!!!\n", pSatDevData->id));
          SM_DBG1(("smsataLLIOStart: 2nd busy NCQ. NCQ Pending 0x%x NONNCQ Pending 0x%x All Pending 0x%x!!!\n", pSatDevData->satPendingNCQIO, 
                    pSatDevData->satPendingNONNCQIO, pSatDevData->satPendingIO));
          tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
          /* free resource */
          smsatFreeIntIoResource( smRoot,
                                  pSatDevData,
                                  satIntIo); 
          return SM_RC_DEVICE_BUSY;
        }
        tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
#endif
      }
    } /* RLE */
    /* for internal SATA command only */
    if (satIOContext->satOrgIOContext != agNULL)
    {  
      /* Initialize tiIORequest */
      smIORequestBody->smIORequest = smIORequest;
      if (smIORequest == agNULL)
      {
        SM_DBG1(("smsataLLIOStart: 1 check!!!\n"));
      }
    }
    /* Initialize tiDevhandle */
    smIORequestBody->smDevHandle = smDeviceHandle;
       
    /* Initializes Scatter Gather and ESGL */
    status = smsatIOPrepareSGL( smRoot, 
                                smIORequestBody, 
                                &smScsiRequest->smSgl1, 
                                smScsiRequest->sglVirtualAddr );
      
    if (status != SM_RC_SUCCESS)
    {
      SM_DBG1(("smsataLLIOStart: can't get SGL!!!\n"));
      /* free resource */
      smsatFreeIntIoResource( smRoot,
                              pSatDevData,
                              satIntIo);
      return status;
    }

    /* Initialize LL Layer agIORequest */    
    agIORequest->osData = (void *) smIORequestBody;
    agIORequest->sdkData = agNULL; /* SA takes care of this */

    smIORequestBody->ioStarted = agTRUE;
    smIORequestBody->ioCompleted = agFALSE;

    /* assign tag value for SATA */
    if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
         (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
    {
      if (agFALSE == smsatTagAlloc(smRoot, pSatDevData, &satIOContext->sataTag))
      {
        SM_DBG1(("smsataLLIOStart: No more NCQ tag!!!\n"));
        smIORequestBody->ioStarted = agFALSE;
        smIORequestBody->ioCompleted = agTRUE;
        return SM_RC_DEVICE_BUSY;
      }
      SM_DBG3(("smsataLLIOStart: ncq tag 0x%x\n",satIOContext->sataTag));
    }
    else
    {
      satIOContext->sataTag = 0xFF;
    }
  }
  else /* AGSA_SATA_PROTOCOL_SRST_ASSERT or AGSA_SATA_PROTOCOL_SRST_DEASSERT 
          or SAT_CHECK_POWER_MODE as ABORT */
  {
    agsaSgl_t          *agSgl;
  
    /* for internal SATA command only */
    if (satIOContext->satOrgIOContext != agNULL)
    {  
      /* Initialize tiIORequest */
      smIORequestBody->smIORequest = smIORequest;
      if (smIORequest == agNULL)
      {
        SM_DBG1(("smsataLLIOStart: 2 check!!!\n"));
      }
    }
    /* Initialize tiDevhandle */
    smIORequestBody->smDevHandle = smDeviceHandle;
    
    
    smIORequestBody->IOType.InitiatorRegIO.expDataLength = 0;
    /* SGL for SATA request */
    agSgl = &(smIORequestBody->transport.SATA.agSATARequestBody.agSgl);
    agSgl->len = 0;

    agSgl->sgUpper = 0;
    agSgl->sgLower = 0;
    agSgl->len = 0;
    SM_CLEAR_ESGL_EXTEND(agSgl->extReserved);
  
    /* Initialize LL Layer agIORequest */
    agIORequest = &(smIORequestBody->agIORequest);
    agIORequest->osData = (void *) smIORequestBody;
    agIORequest->sdkData = agNULL; /* SA takes care of this */

    smIORequestBody->ioStarted = agTRUE;
    smIORequestBody->ioCompleted = agFALSE;
  
    /* setting the data length */
    agSATAReq->dataLength = 0;
  
  }


  smIORequestBody->reTries = 0;
  
#ifdef TD_INTERNAL_DEBUG
  smhexdump("smsataLLIOStart", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t)); 
  smhexdump("smsataLLIOStart LL", (bit8 *)&agSATAReq->fis.fisRegHostToDev,
            sizeof(agsaFisRegHostToDevice_t));
#endif  

  SM_DBG6(("smsataLLIOStart: agDevHandle %p\n", agDevHandle));

  /* to get better IO performance, rotate the OBQ number on main IO path */
  if (smScsiRequest == agNULL)
  {
    nQNumber = 0;
  }
  else
  {
    switch (smScsiRequest->scsiCmnd.cdb[0])
    {
      case SCSIOPC_READ_10:
      case SCSIOPC_WRITE_10:
      case SCSIOPC_READ_6:
      case SCSIOPC_WRITE_6:
      case SCSIOPC_READ_12:
      case SCSIOPC_WRITE_12:
      case SCSIOPC_READ_16:
      case SCSIOPC_WRITE_16:
         nQNumber = tdsmRotateQnumber(smRoot, smDeviceHandle);
         break;

      default:
         nQNumber = 0;
         break;
    }
  }

  SM_DBG3(("sataLLIOStart: Lock in\n"));
  
#ifdef CCFLAG_OPTIMIZE_SAT_LOCK
  if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
       (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
  {
     tdsmInterlockedIncrement(smRoot,&pSatDevData->satPendingNCQIO); 
  }
  else
  {
     tdsmInterlockedIncrement(smRoot,&pSatDevData->satPendingNONNCQIO);
  }
  tdsmInterlockedIncrement(smRoot,&pSatDevData->satPendingIO);
#else
  tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
  if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
       (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
  {
     pSatDevData->satPendingNCQIO++;
  }
  else
  {
     pSatDevData->satPendingNONNCQIO++;
  }    
  pSatDevData->satPendingIO++;

  SMLIST_INIT_ELEMENT (&satIOContext->satIoContextLink);
  SMLIST_ENQUEUE_AT_TAIL (&satIOContext->satIoContextLink, &pSatDevData->satIoLinkList);
  tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
#endif
  /* post SATA command to low level MPI */
  status = saSATAStart( agRoot,
                        agIORequest,
                        nQNumber,                        
                        agDevHandle,
                        satIOContext->reqType,
                        agSATAReq,
                        satIOContext->sataTag,
                        smllSATACompleted
                        );
    
  if (status != AGSA_RC_SUCCESS)
  {
    if (status == AGSA_RC_BUSY)
    {
      SM_DBG1(("smsataLLIOStart: saSATAStart busy!!!\n")); 
      status = SM_RC_BUSY;
    }
    else
    {
      SM_DBG1(("smsataLLIOStart: saSATAStart failed!!!\n"));
      status = SM_RC_FAILURE;
    }

    if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
         (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
    {
      smsatTagRelease(smRoot, pSatDevData, satIOContext->sataTag);
    }

#ifdef CCFLAG_OPTIMIZE_SAT_LOCK
    if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
         (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
    {
      tdsmInterlockedDecrement(smRoot,&oneDeviceData->satPendingNCQIO);
    }
    else
    {
      tdsmInterlockedDecrement(smRoot,&oneDeviceData->satPendingNONNCQIO);
    }
    tdsmInterlockedDecrement(smRoot,&oneDeviceData->satPendingIO);      
#else
    if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
         (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
    {
      tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
      oneDeviceData->satPendingNCQIO--;
      oneDeviceData->satPendingIO--;
      SMLIST_DEQUEUE_THIS (&satIOContext->satIoContextLink);
      tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
    }
    else
    {
      tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
      oneDeviceData->satPendingNONNCQIO--;
      oneDeviceData->satPendingIO--;
      SMLIST_DEQUEUE_THIS (&satIOContext->satIoContextLink);
      tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);     
    }    
#endif /* CCFLAG_OPTIMIZE_SAT_LOCK */
    
    /* Free the ESGL pages associated with this I/O */
    smIORequestBody->ioStarted = agFALSE;
    smIORequestBody->ioCompleted = agTRUE;
    return (status);
  }
  
  return SM_RC_SUCCESS;
}	       


osGLOBAL FORCEINLINE bit32 
smsatIOPrepareSGL(
                  smRoot_t                 *smRoot,
                  smIORequestBody_t        *smIORequestBody,
                  smSgl_t                  *smSgl1,
                  void                     *sglVirtualAddr
                  )
{
  agsaSgl_t          *agSgl;
  
  /* Uppper should be zero-out */
  SM_DBG5(("smsatIOPrepareSGL: start\n"));
  
  SM_DBG5(("smsatIOPrepareSGL: smSgl1->upper %d smSgl1->lower %d smSgl1->len %d\n", 
    smSgl1->upper, smSgl1->lower, smSgl1->len));
  SM_DBG5(("smsatIOPrepareSGL: smSgl1->type %d\n", smSgl1->type)); 

  /* SGL for SATA request */
  agSgl = &(smIORequestBody->transport.SATA.agSATARequestBody.agSgl);
  agSgl->len = 0;

  if (smSgl1 == agNULL)
  {
    SM_DBG1(("smsatIOPrepareSGL: Error smSgl1 is NULL!!!\n"));
    return tiError;
  }

  if (smIORequestBody->IOType.InitiatorRegIO.expDataLength == 0)
  {
    SM_DBG3(("smsatIOPrepareSGL: expDataLength is 0\n"));
    agSgl->sgUpper = 0;
    agSgl->sgLower = 0;
    agSgl->len = 0;
    SM_CLEAR_ESGL_EXTEND(agSgl->extReserved);
    return SM_RC_SUCCESS;
  }

  agSgl->sgUpper = smSgl1->upper;
  agSgl->sgLower = smSgl1->lower;
  agSgl->len = smSgl1->len;
  agSgl->extReserved = smSgl1->type;

  return SM_RC_SUCCESS;

}		  




