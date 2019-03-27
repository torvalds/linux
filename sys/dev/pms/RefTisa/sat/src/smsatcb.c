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

extern smRoot_t *gsmRoot;

/******************************** completion ***********************************************************/

FORCEINLINE void
smllSATACompleted(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  void              *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam
                 )
{
  smRoot_t             *smRoot = agNULL;
//  smIntRoot_t          *smIntRoot = agNULL;
//  smIntContext_t       *smAllShared = agNULL;
  smIORequestBody_t    *smIORequestBody;
  smSatIOContext_t       *satIOContext;
  smDeviceData_t       *pSatDevData;
  smDeviceHandle_t     *smDeviceHandle = agNULL;
  smDeviceData_t       *oneDeviceData = agNULL;

  SM_DBG2(("smllSATACompleted: start\n"));

  if (agIORequest == agNULL)
  {
    SM_DBG1(("smllSATACompleted: agIORequest is NULL!!!\n"));
    return;
  }

  smIORequestBody = (smIORequestBody_t *)agIORequest->osData;

  if (smIORequestBody == agNULL)
  {
    SM_DBG1(("smllSATACompleted: smIORequestBody is NULL!!!\n"));
    return;
  }

  /* for debugging */
  if (smIORequestBody->ioCompleted == agTRUE)
  {
    smDeviceHandle = smIORequestBody->smDevHandle;
    if (smDeviceHandle == agNULL)
    {
      SM_DBG1(("smllSATACompleted: smDeviceHandle is NULL!!!\n"));
      return;
    }
    oneDeviceData  = (smDeviceData_t *)smDeviceHandle->smData;
    SM_DBG1(("smllSATACompleted: Error!!!!!! double completion!!!, ID %d!!!\n", smIORequestBody->id));
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smllSATACompleted: oneDeviceData is NULL!!!\n"));
      return;
    }
    SM_DBG1(("smllSATACompleted: did %d!!!\n", oneDeviceData->id));
    return;
  }

  smIORequestBody->ioCompleted = agTRUE;
  satIOContext    = &(smIORequestBody->transport.SATA.satIOContext);

  if (satIOContext == agNULL)
  {
    SM_DBG1(("smllSATACompleted: satIOContext is NULL!!!\n"));
    return;
  }

  pSatDevData     = satIOContext->pSatDevData;

  if (pSatDevData == agNULL)
  {
    SM_DBG1(("smllSATACompleted: pSatDevData is NULL loc 1, wrong!!!\n"));
    if (satIOContext->satIntIoContext == agNULL)
    {
      SM_DBG1(("smllSATACompleted: external command!!!\n"));
    }
    else
    {
      SM_DBG1(("smllSATACompleted: internal command!!!\n"));
    }
    return;
  }

  smDeviceHandle = smIORequestBody->smDevHandle;

  if (smDeviceHandle == agNULL)
  {
    SM_DBG1(("smllSATACompleted: smDeviceHandle is NULL!!!!\n"));
    return;
  }

  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;

  if (oneDeviceData != pSatDevData)
  {
    SM_DBG1(("smllSATACompleted: diff device handle!!!\n"));
    if (satIOContext->satIntIoContext == agNULL)
    {
      SM_DBG1(("smllSATACompleted: external command!!!\n"));
    }
    else
    {
      SM_DBG1(("smllSATACompleted: internal command!!!\n"));
    }
    return;
  }

  if (oneDeviceData == agNULL)
  {
    SM_DBG1(("smllSATACompleted: oneDeviceData is NULL!!!!\n"));
    if (satIOContext->satIntIoContext == agNULL)
    {
      SM_DBG1(("smllSATACompleted: external command!!!\n"));
    }
    else
    {
      SM_DBG1(("smllSATACompleted: internal command!!!\n"));
    }
    return;
  }

  smRoot = oneDeviceData->smRoot;

  /* release tag value for SATA */
  if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
       (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
  {
    smsatTagRelease(smRoot, pSatDevData, satIOContext->sataTag);
    SM_DBG3(("smllSATACompleted: ncq tag 0x%x\n",satIOContext->sataTag));
  }

  /* just for debugging */
  if (agIOStatus == OSSA_IO_DS_NON_OPERATIONAL)
  {
    SM_DBG1(("smllSATACompleted: agIOStatus is OSSA_IO_DS_NON_OPERATIONAL!!!\n"));
  }
  if (agIOStatus == OSSA_IO_DS_IN_RECOVERY)
  {
    SM_DBG1(("smllSATACompleted: agIOStatus is OSSA_IO_DS_IN_RECOVERY!!!\n"));
  }
  if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS)
  {
    SM_DBG1(("smllSATACompleted: agIOStatus is OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS!!!\n"));
  }

  satIOContext->satCompleteCB( agRoot,
                               agIORequest,
                               agIOStatus,
                               agFirstDword,
                               agIOInfoLen,
                               agParam,
                               satIOContext);



  return;
}
/*****************************************************************************
*! \brief  smsatPacketCB
*
*   This routine is a callback function called from smllSATACompleted().
*   This CB routine deals with normal Packet command I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to smSatIOContext_t.
*
*  \return: none
*
*****************************************************************************/

osGLOBAL void
smsatPacketCB(
   agsaRoot_t        *agRoot,
   agsaIORequest_t   *agIORequest,
   bit32             agIOStatus,
   agsaFisHeader_t   *agFirstDword,
   bit32             agIOInfoLen,
   void              *agParam,
   void              *ioContext
   )
{
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatIOContext_t         *satNewIOContext;
  smSatInternalIo_t        *satIntIo;
  smSatInternalIo_t        *satNewIntIo = agNULL;
  smDeviceData_t           *oneDeviceData;
  bit32                     interruptContext;
  smScsiRspSense_t         *pSense;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;
//  bit32                     ataStatus = 0;
//  bit32                     ataError;

  bit32                     status = SM_RC_SUCCESS;
//  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
//  bit32                     dataLength;
  bit8                      bSenseKey = 0;
  bit16                     bSenseCodeInfo = 0;

  SM_DBG3(("smsatPacketCB: start\n"));
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  if (satIOContext == agNULL)
  {
    SM_DBG1(("smsatPacketCB: satIOContext is NULL\n"));
    return;
  }
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  interruptContext       = satIOContext->interruptContext;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG5(("smsatPacketCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG5(("smsatPacketCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted   = agFALSE;

  /* interal structure free */
  smsatFreeIntIoResource( smRoot, oneDeviceData, satIntIo);
  if( agIOStatus == OSSA_IO_SUCCESS && agIOInfoLen == 0 && agFirstDword == agNULL)
  {
    SM_DBG3(("smsatPacketCB: First, agIOStatus == OSSA_IO_SUCCESS, agFirstDword == agNULL, agIOInfoLen = %d\n", agIOInfoLen));
    tdsmIOCompletedCB(smRoot,
                      smOrgIORequest,
                      smIOSuccess,
                      SCSI_STAT_GOOD,
                      agNULL,
                      interruptContext);
  }
  else if (agIOStatus == OSSA_IO_SUCCESS &&  !(agIOInfoLen == 0 && agFirstDword == agNULL))
  {
      SM_DBG2(("smsatPacketCB: Second, agIOStatus == OSSA_IO_SUCCESS , agFirstDword %p agIOInfoLen = %d\n", agFirstDword, agIOInfoLen));
      /*The SCSI command status is error, need to send REQUEST SENSE for getting more sense information*/
      satNewIntIo = smsatAllocIntIoResource( smRoot,
                                       smOrgIORequest,
                                       oneDeviceData,
                                       SENSE_DATA_LENGTH,
                                       satNewIntIo);
      if (satNewIntIo == agNULL)
      {
          /* memory allocation failure */
          /* just translate the ATAPI error register to sense information */
          smsatTranslateATAPIErrorsToSCSIErrors(
                          scsiCmnd->cdb[0],
                          agFirstDword->D2H.status,
                          agFirstDword->D2H.error,
                          &bSenseKey,
                          &bSenseCodeInfo
                          );
          smsatSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
          tdsmIOCompletedCB( smRoot,
                             smOrgIORequest,
                             smIOSuccess,
                             SCSI_STAT_CHECK_CONDITION,
                             satOrgIOContext->pSmSenseData,
                             interruptContext);
          SM_DBG1(("smsatPacketCB: momory allocation fails\n"));
          return;
      } /* end memory allocation */

      satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                        smOrgIORequest,
                                        oneDeviceData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );
      /* sends request sense to ATAPI device for acquiring sense information */
      status = smsatRequestSenseForATAPI(smRoot,
                              &satNewIntIo->satIntSmIORequest,
                              satNewIOContext->psmDeviceHandle,
                              &satNewIntIo->satIntSmScsiXchg,
                              satNewIOContext
                              );
      if (status != SM_RC_SUCCESS)
      {
          smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satNewIntIo);
          /* just translate the ATAPI error register to sense information */
          smsatTranslateATAPIErrorsToSCSIErrors(
                          scsiCmnd->cdb[0],
                          agFirstDword->D2H.status,
                          agFirstDword->D2H.error,
                          &bSenseKey,
                          &bSenseCodeInfo
                          );
          smsatSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
          tdsmIOCompletedCB(smRoot,
                            smOrgIORequest,
                            smIOSuccess,
                            SCSI_STAT_CHECK_CONDITION,
                            satOrgIOContext->pSmSenseData,
                            interruptContext);
          SM_DBG1(("smsatPacketCB: failed to call satRequestSenseForATAPI()\n"));
      }
  }
  else if (agIOStatus != OSSA_IO_SUCCESS )
  {
      SM_DBG2(("smsatPacketCB: agIOStatus != OSSA_IO_SUCCESS, status %d\n", agIOStatus));
      smsatProcessAbnormalCompletion(
                    agRoot,
                    agIORequest,
                    agIOStatus,
                    agFirstDword,
                    agIOInfoLen,
                    agParam,
                    satIOContext);
  }
  else
  {
    SM_DBG1(("smsatPacketCB: Unknown error \n"));
    tdsmIOCompletedCB(smRoot,
                      smOrgIORequest,
                      smIOFailed,
                      smDetailOtherError,
                      agNULL,
                      interruptContext);
  }
}
/*****************************************************************************
*! \brief  smsatRequestSenseForATAPICB
*
*   This routine is a callback function called from smllSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to smSatIOContext_t.
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void
smsatRequestSenseForATAPICB(
   agsaRoot_t        *agRoot,
   agsaIORequest_t   *agIORequest,
   bit32             agIOStatus,
   agsaFisHeader_t   *agFirstDword,
   bit32             agIOInfoLen,
   void              *agParam,
   void              *ioContext
   )
{
   smRoot_t                  *smRoot = agNULL;
   smIntRoot_t               *smIntRoot = agNULL;
   smIntContext_t            *smAllShared = agNULL;
   smIORequestBody_t         *smIORequestBody;
   smIORequestBody_t         *smOrgIORequestBody;
   smSatIOContext_t            *satIOContext;
   smSatIOContext_t            *satOrgIOContext;
//   smSatIOContext_t            *satNewIOContext;
   smSatInternalIo_t           *satIntIo;
//   smSatInternalIo_t           *satNewIntIo = agNULL;
   smDeviceData_t            *oneDeviceData;
   bit32                     interruptContext;
   bit8                      dataLength;
   smIniScsiCmnd_t           *scsiCmnd;
   smIORequest_t             *smOrgIORequest;

   SM_DBG3(("smsatRequestSenseForATAPICB: start\n"));
   smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
   satIOContext           = (smSatIOContext_t *) ioContext;
   if (satIOContext == agNULL)
   {
     SM_DBG1(("smsatRequestSenseForATAPICB: satIOContext is NULL\n"));
     return;
   }
   satIntIo               = satIOContext->satIntIoContext;
   oneDeviceData          = satIOContext->pSatDevData;
   interruptContext = satIOContext->interruptContext;
   smRoot                 = oneDeviceData->smRoot;
   smIntRoot              = (smIntRoot_t *)smRoot->smData;
   smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
   if (satIntIo == agNULL)
   {
     SM_DBG5(("smsatRequestSenseForATAPICB: External smSatInternalIo_t satIntIoContext\n"));
     satOrgIOContext = satIOContext;
     smOrgIORequest  = smIORequestBody->smIORequest;
     scsiCmnd        = satIOContext->pScsiCmnd;
   }
   else
   {
     SM_DBG5(("smsatRequestSenseForATAPICB: Internal smSatInternalIo_t satIntIoContext\n"));
     satOrgIOContext        = satIOContext->satOrgIOContext;
     smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
     smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
     scsiCmnd      = satOrgIOContext->pScsiCmnd;
   }

   smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
   smIORequestBody->ioCompleted = agTRUE;
   smIORequestBody->ioStarted   = agFALSE;
   if ( (agIOStatus == OSSA_IO_SUCCESS && agIOInfoLen == 0 && agFirstDword == agNULL))
   {
      /* copy the request sense buffer to original IO buffer*/
      if (satIntIo)
      {
        sm_memcpy(satOrgIOContext->pSmSenseData->senseData, satIntIo->satIntDmaMem.virtPtr, SENSE_DATA_LENGTH);
      }
      satOrgIOContext->pSmSenseData->senseLen = SENSE_DATA_LENGTH;
      /* interal structure free */
      smsatFreeIntIoResource( smRoot, oneDeviceData, satIntIo);

      /* notify the OS to complete this SRB */
      tdsmIOCompletedCB( smRoot,
                  smOrgIORequest,
                  smIOSuccess,
                  SCSI_STAT_CHECK_CONDITION,
                  satOrgIOContext->pSmSenseData,
                  interruptContext);
   }
   else if (agIOStatus == OSSA_IO_UNDERFLOW )
   {
      /* copy the request sense buffer to original IO buffer*/
      SM_DBG1(("smsatRequestSenseForATAPICB: OSSA_IO_UNDERFLOW agIOInfoLen = %d\n", agIOInfoLen));
      dataLength = (bit8)(scsiCmnd->expDataLength - agIOInfoLen);
      if (satIntIo)
      {
        sm_memcpy(satOrgIOContext->pSmSenseData->senseData, satIntIo->satIntDmaMem.virtPtr, dataLength);
      }
      satOrgIOContext->pSmSenseData->senseLen = dataLength;
      /* interal structure free */
      smsatFreeIntIoResource( smRoot, oneDeviceData, satIntIo);

      /* notify the OS to complete this SRB */
      tdsmIOCompletedCB( smRoot,
                  smOrgIORequest,
                  smIOSuccess,
                  SCSI_STAT_CHECK_CONDITION,
                  satOrgIOContext->pSmSenseData,
                  interruptContext);
   }
   else
   {
      SM_DBG1(("smsatRequestSenseForATAPICB: failed, agIOStatus error = 0x%x agIOInfoLen = %d\n", agIOStatus, agIOInfoLen));
      /* interal structure free */
      smsatFreeIntIoResource( smRoot, oneDeviceData, satIntIo);

      /* notify the OS to complete this SRB */
      tdsmIOCompletedCB( smRoot,
                  smOrgIORequest,
                  smIOFailed,
                  smDetailOtherError,
                  agNULL,
                  interruptContext);
   }
   SM_DBG3(("smsatRequestSenseForATAPICB: end\n"));
}

/*****************************************************************************
*! \brief  smsatSetFeaturesPIOCB
*
*   This routine is a callback function called from smllSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to smSatIOContext_t.
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void
smsatSetFeaturesPIOCB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    smRoot_t                  *smRoot = agNULL;
    smIntRoot_t               *smIntRoot = agNULL;
    smIntContext_t            *smAllShared = agNULL;
    smIORequestBody_t         *smIORequestBody;
    smIORequestBody_t         *smOrgIORequestBody = agNULL;
    smSatIOContext_t          *satIOContext;
    smSatIOContext_t          *satOrgIOContext;
    smSatIOContext_t          *satNewIOContext;
    smSatInternalIo_t         *satIntIo;
    smSatInternalIo_t         *satNewIntIo = agNULL;
    smDeviceData_t            *oneDeviceData;
    smIniScsiCmnd_t           *scsiCmnd;
    smIORequest_t             *smOrgIORequest;
    smDeviceHandle_t          *smDeviceHandle;
    bit32                      status = SM_RC_FAILURE;
    smIORequest_t             *smIORequest;

    SM_DBG2(("smsatSetFeaturesPIOCB: start\n"));
    smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
    satIOContext           = (smSatIOContext_t *) ioContext;
    if (satIOContext == agNULL)
    {
      SM_DBG1(("smsatSetFeaturesPIOCB: satIOContext is NULL\n"));
      return;
    }
    satIntIo               = satIOContext->satIntIoContext;
    oneDeviceData          = satIOContext->pSatDevData;
    smDeviceHandle         = satIOContext->psmDeviceHandle;
    smRoot                 = oneDeviceData->smRoot;
    smIntRoot              = (smIntRoot_t *)smRoot->smData;
    smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
    if (satIntIo == agNULL)
    {
      SM_DBG2(("smsatSetFeaturesPIOCB: External smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext = satIOContext;
      smOrgIORequest  = smIORequestBody->smIORequest;
      scsiCmnd        = satIOContext->pScsiCmnd;
    }
    else
    {
      SM_DBG2(("smsatSetFeaturesPIOCB: Internal smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext        = satIOContext->satOrgIOContext;
      smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
      smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
      scsiCmnd      = satOrgIOContext->pScsiCmnd;
    }
    smIORequest  = smOrgIORequestBody->smIORequest;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smIORequestBody->ioCompleted = agTRUE;
    smIORequestBody->ioStarted   = agFALSE;
    /* interal structure free */
    smsatFreeIntIoResource(smRoot,
                           oneDeviceData,
                           satIntIo);
    if (smIORequest->tdData == smIORequest->smData)
    {
      SM_DBG1(("smsatSetFeaturesPIOCB: the same tdData and smData error!\n"));
    }
    /* check the agIOStatus */
    if (agIOStatus == OSSA_IO_ABORTED ||
        agIOStatus == OSSA_IO_NO_DEVICE ||
        agIOStatus == OSSA_IO_PORT_IN_RESET ||
        agIOStatus == OSSA_IO_DS_NON_OPERATIONAL ||
        agIOStatus == OSSA_IO_DS_IN_RECOVERY ||
        agIOStatus == OSSA_IO_DS_IN_ERROR ||
        agIOStatus == OSSA_IO_DS_INVALID
       )
    {
      SM_DBG1(("smsatSetFeaturesPIOCB: error status 0x%x\n", agIOStatus));
      SM_DBG1(("smsatSetFeaturesPIOCB: did %d!!!\n", oneDeviceData->id));
      tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
      return;
    }
    /*if the ATAPI device support DMA, then enble this feature*/
    if (oneDeviceData->satDMASupport)
    {
        satNewIntIo = smsatAllocIntIoResource(smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           0,
                                           satNewIntIo);
        if (satNewIntIo == agNULL)
        {
            SM_DBG1(("smsatSetFeaturesPIOCB: memory allocation fails\n"));
            /*Complete this identify packet device IO */
            tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
            return;
        } /* end memory allocation */

        satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                          smOrgIORequest,
                                          oneDeviceData,
                                          scsiCmnd,
                                          satOrgIOContext
                                          );
        /* sends another ATA SET FEATURES based on DMA bit */
        status = smsatSetFeaturesDMA(smRoot,
                                &satNewIntIo->satIntSmIORequest,
                                satNewIOContext->psmDeviceHandle,
                                &satNewIntIo->satIntSmScsiXchg,
                                satNewIOContext
                                );
        if (status != SM_RC_SUCCESS)
        {
            smsatFreeIntIoResource(smRoot, oneDeviceData, satNewIntIo);
            SM_DBG2(("satSetFeaturesPIOCB: failed to call smsatSetFeatures()\n"));
            /*Complete this identify packet device IO */
            tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
        }
    }
    else
    {
        /*Complete this identify packet device IO */
        tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
    }
    SM_DBG2(("smsatSetFeaturesPIOCB: exit, agIOStatus 0x%x\n", agIOStatus));
}

/*****************************************************************************
*! \brief  smsatDeviceResetCB
*
*   This routine is a callback function called from smllSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to smSatIOContext_t.
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void
smsatDeviceResetCB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    smRoot_t                 *smRoot = agNULL;
    smIntRoot_t              *smIntRoot = agNULL;
    smIntContext_t           *smAllShared = agNULL;
    smIORequestBody_t        *smIORequestBody;
    smSatIOContext_t         *satIOContext;
    smSatIOContext_t         *satOrgIOContext;
//    smSatIOContext_t          *satNewIOContext;
    smSatInternalIo_t        *satIntIo;
//    smSatInternalIo_t         *satNewIntIo = agNULL;
    smDeviceData_t           *oneDeviceData;
#ifdef  TD_DEBUG_ENABLE
    agsaFisPioSetupHeader_t  *satPIOSetupHeader = agNULL;
    bit32                     ataStatus = 0;
    bit32                     ataError;
#endif
//    bit32                     status;
    bit32                     AbortTM = agFALSE;
    smDeviceHandle_t         *smDeviceHandle;

    SM_DBG1(("smsatDeviceResetCB: start\n"));
    smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
    satIOContext           = (smSatIOContext_t *) ioContext;
    satIntIo               = satIOContext->satIntIoContext;
    oneDeviceData          = satIOContext->pSatDevData;
    smRoot                 = oneDeviceData->smRoot;
    smIntRoot              = (smIntRoot_t *)smRoot->smData;
    smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
    smDeviceHandle         = oneDeviceData->smDevHandle;
    if (satIntIo == agNULL)
    {
      SM_DBG6(("smsatDeviceResetCB: External, OS generated\n"));
      satOrgIOContext      = satIOContext;
    }
    else
    {
      SM_DBG6(("smsatDeviceResetCB: Internal, TD generated\n"));
      satOrgIOContext        = satIOContext->satOrgIOContext;
      if (satOrgIOContext == agNULL)
      {
        SM_DBG6(("smsatDeviceResetCB: satOrgIOContext is NULL, wrong\n"));
        return;
      }
      else
      {
        SM_DBG6(("smsatDeviceResetCB: satOrgIOContext is NOT NULL\n"));
      }
    }
    smIORequestBody->ioCompleted = agTRUE;
    smIORequestBody->ioStarted = agFALSE;
    if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatDeviceResetCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
      /* TM completed */
      tdsmEventCB( smRoot,
                   smDeviceHandle,
                   smIntrEventTypeTaskManagement,
                   smTMFailed,
                   oneDeviceData->satTmTaskTag);
      oneDeviceData->satTmTaskTag = agNULL;
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }
    if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BREAK ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY
        )
    {
      SM_DBG1(("smsatDeviceResetCB: OSSA_IO_OPEN_CNX_ERROR!!!\n"));
      /* TM completed */
      tdsmEventCB( smRoot,
                   smDeviceHandle,
                   smIntrEventTypeTaskManagement,
                   smTMFailed,
                   oneDeviceData->satTmTaskTag);
      oneDeviceData->satTmTaskTag = agNULL;
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }
    if (agIOStatus != OSSA_IO_SUCCESS)
    {
#ifdef  TD_DEBUG_ENABLE
       /* only agsaFisPioSetup_t is expected */
       satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
       ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
       ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
       SM_DBG1(("smsatDeviceResetCB: ataStatus 0x%x ataError 0x%x!!!\n", ataStatus, ataError));
       /* TM completed */
       tdsmEventCB( smRoot,
                    smDeviceHandle,
                    smIntrEventTypeTaskManagement,
                    smTMFailed,
                    oneDeviceData->satTmTaskTag);
       oneDeviceData->satTmTaskTag = agNULL;
       smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
       smsatFreeIntIoResource( smRoot,
                               oneDeviceData,
                               satIntIo);
       return;
    }
    /*success */
    if (satOrgIOContext->TMF == AG_ABORT_TASK)
    {
      AbortTM = agTRUE;
    }
    if (AbortTM == agTRUE)
    {
      SM_DBG1(("smsatDeviceResetCB: calling satAbort!!!\n"));
      smsatAbort(smRoot, agRoot, satOrgIOContext->satToBeAbortedIOContext);
    }
    oneDeviceData->satTmTaskTag = agNULL;
    oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    SM_DBG1(("smsatDeviceResetCB: satPendingIO %d satNCQMaxIO %d!!!\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
    SM_DBG1(("smsatDeviceResetCB: satPendingNCQIO %d satPendingNONNCQIO %d!!!\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMOK,
                 oneDeviceData->satTmTaskTag);


    SM_DBG3(("smsatDeviceResetCB: return\n"));
}


/*****************************************************************************
*! \brief  smsatExecuteDeviceDiagnosticCB
*
*   This routine is a callback function called from smllSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to smSatIOContext_t.
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void
smsatExecuteDeviceDiagnosticCB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    smRoot_t                  *smRoot = agNULL;
    smIntRoot_t               *smIntRoot = agNULL;
    smIntContext_t            *smAllShared = agNULL;
    smIORequestBody_t         *smIORequestBody;
    smSatIOContext_t          *satIOContext;
    smSatIOContext_t          *satOrgIOContext;
//    smSatIOContext_t            *satNewIOContext;
    smSatInternalIo_t         *satIntIo;
//    smSatInternalIo_t           *satNewIntIo = agNULL;
    smDeviceData_t            *oneDeviceData;

    SM_DBG6(("smsatSetFeaturesDMACB: start\n"));
    smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
    satIOContext           = (smSatIOContext_t *) ioContext;
    satIntIo               = satIOContext->satIntIoContext;
    oneDeviceData          = satIOContext->pSatDevData;
    smRoot                 = oneDeviceData->smRoot;
    smIntRoot              = (smIntRoot_t *)smRoot->smData;
    smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
    if (satIntIo == agNULL)
    {
      SM_DBG5(("smsatExecuteDeviceDiagnosticCB: External smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext = satIOContext;
    }
    else
    {
      SM_DBG5(("smsatExecuteDeviceDiagnosticCB: Internal smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext        = satIOContext->satOrgIOContext;
      if (satOrgIOContext == agNULL)
      {
        SM_DBG5(("smsatExecuteDeviceDiagnosticCB: satOrgIOContext is NULL\n"));
      }
      else
      {
        SM_DBG5(("smsatExecuteDeviceDiagnosticCB: satOrgIOContext is NOT NULL\n"));
      }
    }
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smIORequestBody->ioCompleted = agTRUE;
    smIORequestBody->ioStarted   = agFALSE;
     /* interal structure free */
    smsatFreeIntIoResource(smRoot,oneDeviceData, satIntIo);
}

GLOBAL void
smsatTranslateATAPIErrorsToSCSIErrors(
    bit8   bCommand,
    bit8   bATAStatus,
    bit8   bATAError,
    bit8   *pSenseKey,
    bit16  *pSenseCodeInfo
    )
{
    if (pSenseKey == agNULL || pSenseCodeInfo == agNULL)
    {
        SM_DBG1(("TranslateATAErrorsToSCSIErros: pSenseKey == agNULL || pSenseCodeInfo == agNULL\n"));
        return;
    }
    if (bATAStatus & ERR_ATA_STATUS_MASK )
    {
        if(bATAError & NM_ATA_ERROR_MASK)
        {
          *pSenseKey = SCSI_SNSKEY_NOT_READY;
          *pSenseCodeInfo = 0x3a00;
        }
        else if(bATAError & ABRT_ATA_ERROR_MASK)
        {
          *pSenseKey = SCSI_SNSKEY_ABORTED_COMMAND;
          *pSenseCodeInfo = 0;
        }
        else if(bATAError & MCR_ATA_ERROR_MASK)
        {
          *pSenseKey = SCSI_SNSKEY_UNIT_ATTENTION;
          *pSenseCodeInfo = 0x5a01;
        }
        else if(bATAError & IDNF_ATA_ERROR_MASK)
        {
          *pSenseKey = SCSI_SNSKEY_MEDIUM_ERROR;
          *pSenseCodeInfo = 0x1401;
        }
        else if(bATAError & MC_ATA_ERROR_MASK)
        {
          *pSenseKey = SCSI_SNSKEY_UNIT_ATTENTION;
          *pSenseCodeInfo = 0x2800;
        }
        else if(bATAError & UNC_ATA_ERROR_MASK)
        {
          /*READ*/
          *pSenseKey = SCSI_SNSKEY_MEDIUM_ERROR;
          *pSenseCodeInfo = 0x1100;

          /*add WRITE here */
        }
        else if(bATAError & ICRC_ATA_ERROR_MASK)
        {
          *pSenseKey = SCSI_SNSKEY_ABORTED_COMMAND;
          *pSenseCodeInfo = 0x4703;
        }
    }
    else if((bATAStatus & DF_ATA_STATUS_MASK))
    {
        *pSenseKey = SCSI_SNSKEY_HARDWARE_ERROR;
        *pSenseCodeInfo = 0x4400;
    }
    else
    {
        SM_DBG1(("unhandled ata error: bATAStatus = 0x%x, bATAError = 0x%x\n", bATAStatus, bATAError));
    }
}

GLOBAL void 
smsatTranslateATAErrorsToSCSIErrors(
    bit8   bATAStatus,
    bit8   bATAError,
    bit8   *pSenseKey,
    bit16  *pSenseCodeInfo
    )
{

  SM_DBG1(("TranslateATAErrorsToSCSIErros: bATAStatus=%d  bATAError= %d \n",bATAStatus,bATAError));

  if (pSenseKey == agNULL || pSenseCodeInfo == agNULL)
  {
    SM_DBG1(("TranslateATAErrorsToSCSIErros: pSenseKey == agNULL || pSenseCodeInfo == agNULL\n"));
    return;
  }
	
  if (bATAStatus & ERR_ATA_STATUS_MASK) 
  {
    if(bATAError & NM_ATA_ERROR_MASK)
    {
      *pSenseKey = SCSI_SNSKEY_NOT_READY;
      *pSenseCodeInfo = SCSI_SNSCODE_MEDIUM_NOT_PRESENT;
    }
    else if(bATAError & UNC_ATA_ERROR_MASK)
    {
      *pSenseKey = SCSI_SNSKEY_MEDIUM_ERROR;
      *pSenseCodeInfo = SCSI_SNSCODE_UNRECOVERED_READ_ERROR;
    }
    else if(bATAError & IDNF_ATA_ERROR_MASK)
    {
      *pSenseKey = SCSI_SNSKEY_ILLEGAL_REQUEST;
      *pSenseCodeInfo = SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
    }
    else if(bATAError & ABRT_ATA_ERROR_MASK)
    {
      *pSenseKey = SCSI_SNSKEY_ABORTED_COMMAND;
      *pSenseCodeInfo = SCSI_SNSCODE_NO_ADDITIONAL_INFO;
    }
    else if(bATAError & MC_ATA_ERROR_MASK)
    {
      *pSenseKey = SCSI_SNSKEY_UNIT_ATTENTION;
      *pSenseCodeInfo = SCSI_SNSCODE_NOT_READY_TO_READY_CHANGE;
    }
    else if(bATAError & MCR_ATA_ERROR_MASK)
    {
      *pSenseKey = SCSI_SNSKEY_UNIT_ATTENTION;
      *pSenseCodeInfo = SCSI_SNSCODE_OPERATOR_MEDIUM_REMOVAL_REQUEST;
    }
    else if(bATAError & ICRC_ATA_ERROR_MASK)
    {
      *pSenseKey = SCSI_SNSKEY_ABORTED_COMMAND;
      *pSenseCodeInfo = SCSI_SNSCODE_INFORMATION_UNIT_CRC_ERROR;
    }
    else
    {
      *pSenseKey = SCSI_SNSKEY_NO_SENSE;
      *pSenseCodeInfo = SCSI_SNSCODE_NO_ADDITIONAL_INFO;

    }
  }
  else if (bATAStatus & DF_ATA_STATUS_MASK) /* INTERNAL TARGET FAILURE */
  {
    *pSenseKey = SCSI_SNSKEY_HARDWARE_ERROR;
    *pSenseCodeInfo = SCSI_SNSCODE_INTERNAL_TARGET_FAILURE; 
  }
	
	
}


FORCEINLINE void
smsatNonChainedDataIOCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        void              *agParam,
                        void              *ioContext
                       )
{
  smIORequestBody_t    *smIORequestBody = (smIORequestBody_t *)agIORequest->osData;
  smSatIOContext_t       *satIOContext    = (smSatIOContext_t *) ioContext;
  smSatInternalIo_t      *SatIntIo        = satIOContext->satIntIoContext;
  smDeviceData_t       *oneDeviceData   = satIOContext->pSatDevData;
  smRoot_t             *smRoot          = oneDeviceData->smRoot;
  smIntRoot_t          *smIntRoot       = (smIntRoot_t *)smRoot->smData;
  smIntContext_t       *smAllShared     = (smIntContext_t *)&smIntRoot->smAllShared;
  bit32                interruptContext = satIOContext->interruptContext;

  SM_DBG2(("smsatNonChainedDataIOCB: start\n"));

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted   = agFALSE;

  /* interal structure free */
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          SatIntIo);

  /* Process completion */
  if( (agIOStatus == OSSA_IO_SUCCESS) && (agIOInfoLen == 0))
  {
    SM_DBG5(("smsatNonChainedDataIOCB: success\n"));
    SM_DBG5(("smsatNonChainedDataIOCB: success agIORequest %p\n", agIORequest));
    /*
     * Command was completed OK, this is the normal path.
     * Now call the OS-App Specific layer about this completion.
     */
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       interruptContext);
  }
  else
  {
    SM_DBG1(("smsatNonChainedDataIOCB: calling smsatProcessAbnormalCompletion!!!\n"));
    /* More checking needed */
    smsatProcessAbnormalCompletion( agRoot,
                                    agIORequest,
                                    agIOStatus,
                                    agFirstDword,
                                    agIOInfoLen,
                                    agParam,
                                    satIOContext);
  }

  return;
}

FORCEINLINE void
smsatChainedDataIOCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     void              *agParam,
                     void              *ioContext
                    )
{

  smRoot_t                  *smRoot = agNULL;
  smIntRoot_t               *smIntRoot = agNULL;
  smIntContext_t            *smAllShared = agNULL;
  smIORequestBody_t         *smIORequestBody;
  smIORequestBody_t         *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatIOContext_t          *satNewIOContext;
  smSatInternalIo_t         *satIntIo;
  smSatInternalIo_t         *satNewIntIo = agNULL;
//  smDeviceData_t             *satDevData;
  smDeviceData_t            *oneDeviceData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smIORequest_t             *smOrgIORequest;
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                      ataStatus = 0;
  bit32                      status = tiError;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  bit32                      dataLength;

  SM_DBG6(("smsatChainedDataIOCB: start\n"));
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  if (satIOContext == agNULL)
  {
    SM_DBG1(("smsatChainedDataIOCB: satIOContext is NULL\n"));
    return;
  }
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  if (satIntIo == agNULL)
  {
    SM_DBG5(("smsatChainedDataIOCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG5(("smsatChainedDataIOCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     SM_DBG1(("smsatChainedDataIOCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
     smsatSetSensePayload( pSense,
                           SCSI_SNSKEY_NO_SENSE,
                           0,
                           SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                           satOrgIOContext);

     tdsmIOCompletedCB( smRoot,
                        smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                        smIOSuccess,
                        SCSI_STAT_CHECK_CONDITION,
                        satOrgIOContext->pSmSenseData,
                        satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);
    return;
  }

  /*
    checking IO status, FIS type and error status
  */
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* agsaFisPioSetup_t or agsaFisRegDeviceToHost_t or agsaFisSetDevBits_t for read
       agsaFisRegDeviceToHost_t or agsaFisSetDevBits_t for write
       first, assumed to be Reg Device to Host FIS
       This is OK to just find fis type
    */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
    /* for debugging */
    if( (statDevToHostFisHeader->fisType != PIO_SETUP_DEV_TO_HOST_FIS) &&
        (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) &&
        (statDevToHostFisHeader->fisType != SET_DEV_BITS_FIS)
        )
    {
      SM_DBG1(("smsatChainedDataIOCB: FAILED, Wrong FIS type 0x%x!!!\n", statDevToHostFisHeader->fisType));
    }

    /* for debugging */
    if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      SM_DBG1(("smsatChainedDataIOCB: FAILED, error status and command 0x%x!!!\n", hostToDevFis->h.command));
    }

    /* the function below handles abort case */
    smsatDelayedProcessAbnormalCompletion(agRoot,
                                          agIORequest,
                                          agIOStatus,
                                          agFirstDword,
                                          agIOInfoLen,
                                          agParam,
                                          satIOContext);

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  } /* end of error */

  switch (hostToDevFis->h.command)
  {
  case SAT_READ_DMA: /* fall through */
  case SAT_READ_SECTORS: /* fall through */
  case SAT_READ_DMA_EXT: /* fall through */
  case SAT_READ_SECTORS_EXT: /* fall through */
  case SAT_READ_FPDMA_QUEUED: /* fall through */
  case SAT_WRITE_DMA: /* fall through */
  case SAT_WRITE_SECTORS:/* fall through */
  case SAT_WRITE_DMA_FUA_EXT: /* fall through */
  case SAT_WRITE_DMA_EXT: /* fall through */
  case SAT_WRITE_SECTORS_EXT: /* fall through */
  case SAT_WRITE_FPDMA_QUEUED:

    SM_DBG5(("smsatChainedDataIOCB: READ/WRITE success case\n"));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with internally genereated SAT_SMART_RETURN_STATUS */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    /* let's loop till TL */

    /* lba = lba + tl
       loopnum--;
       if (loopnum == 0) done
     */
    (satOrgIOContext->LoopNum)--;
    if (satOrgIOContext->LoopNum == 0)
    {
      /* done with read */
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
      return;
    }
    
    /* don't need to allocate payload memory here. Use the one allocated by OS layer */
    dataLength = 0;

    satNewIntIo = smsatAllocIntIoResource( smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           dataLength,
                                           satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         satOrgIOContext->interruptContext );

      SM_DBG1(("smsatChainedDataIOCB: momory allocation fails!!!\n"));
      return;
    } /* end of memory allocation failure */

       /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = smsatPrepareNewIO(
                                        satNewIntIo,
                                        smOrgIORequest,
                                        oneDeviceData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );

    /* sending another ATA command */
    switch (scsiCmnd->cdb[0])
    {
    case SCSIOPC_READ_6:
      /* no loop should occur with READ6 since it fits in one ATA command */
      break;
    case SCSIOPC_READ_10: /* fall through */
    case SCSIOPC_READ_12: /* fall through */
    case SCSIOPC_READ_16: /* fall through */
      status = smsatRead_1( smRoot,
                            &satNewIntIo->satIntSmIORequest,
                            satNewIOContext->psmDeviceHandle,
                            &satNewIntIo->satIntSmScsiXchg,
                            satNewIOContext);
      break;
    case SCSIOPC_WRITE_6:
      /* no loop should occur with WRITE6 since it fits in one ATA command */
      break;
    case SCSIOPC_WRITE_10: /* fall through */
    case SCSIOPC_WRITE_12: /* fall through */
    case SCSIOPC_WRITE_16: /* fall through */
      status = smsatWrite_1( smRoot,
                             &satNewIntIo->satIntSmIORequest,
                             satNewIOContext->psmDeviceHandle,
                             &satNewIntIo->satIntSmScsiXchg,
                             satNewIOContext);
      break;
    default:
      SM_DBG1(("smsatChainedDataIOCB: success but default case scsi cmd 0x%x ata cmd 0x%x!!!\n",scsiCmnd->cdb[0], hostToDevFis->h.command));
      status = tiError;
      break;
    }



    if (status != SM_RC_SUCCESS)
    {
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satNewIntIo);
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         satOrgIOContext->interruptContext );
      SM_DBG1(("smsatChainedDataIOCB: calling satRead10_1 fails!!!\n"));
      return;
    }

    break;


  default:
    SM_DBG1(("smsatChainedDataIOCB: success but default case command 0x%x!!!\n",hostToDevFis->h.command));
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);

    break;
  }


  return;
}

osGLOBAL void
smsatNonChainedVerifyCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                       )
{
 
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                *smRoot = agNULL;
  smIntRoot_t             *smIntRoot = agNULL;
  smIntContext_t          *smAllShared = agNULL;
  smIORequestBody_t       *smIORequestBody;
  smIORequestBody_t       *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatInternalIo_t         *satIntIo;
//  satDeviceData_t         *satDevData;
  smDeviceData_t          *oneDeviceData;
  smScsiRspSense_t          *pSense;
  smIORequest_t             *smOrgIORequest;
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  SM_DBG2(("smsatNonChainedVerifyCB: start\n"));
  SM_DBG5(("smsatNonChainedVerifyCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatNonChainedVerifyCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satIOContext->pSense;
  }
  else
  {
    SM_DBG4(("smsatNonChainedVerifyCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatNonChainedVerifyCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatNonChainedVerifyCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense                 = satOrgIOContext->pSense;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     SM_DBG1(("smsatNonChainedVerifyCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
     smsatSetSensePayload( pSense,
                           SCSI_SNSKEY_NO_SENSE,
                           0,
                           SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                           satOrgIOContext);
     tdsmIOCompletedCB( smRoot,
                        smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                        smIOSuccess,
                        SCSI_STAT_CHECK_CONDITION,
                        satOrgIOContext->pSmSenseData,
                        satOrgIOContext->interruptContext );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    if ( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
         ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
         )
    {
      /* for debugging */
      if( agIOStatus != OSSA_IO_SUCCESS)
      {
        SM_DBG1(("smsatNonChainedVerifyCB: FAILED, NOT IO_SUCCESS!!!\n"));
      }
      else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
      {
        SM_DBG1(("smsatNonChainedVerifyCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
      }
      else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
                (ataStatus & DF_ATA_STATUS_MASK)
                )
      {
        SM_DBG1(("smsatNonChainedVerifyCB: FAILED, FAILED, error status!!!\n"));
      }

      /* Process abort case */
      if (agIOStatus == OSSA_IO_ABORTED)
      {
        smsatProcessAbort(smRoot,
                          smOrgIORequest,
                          satOrgIOContext
                          );

        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
        return;
      }

      /* for debugging */
      switch (hostToDevFis->h.command)
      {
      case SAT_READ_VERIFY_SECTORS:
        SM_DBG1(("smsatNonChainedVerifyCB: SAT_READ_VERIFY_SECTORS!!!\n"));
        break;
      case SAT_READ_VERIFY_SECTORS_EXT:
        SM_DBG1(("smsatNonChainedVerifyCB: SAT_READ_VERIFY_SECTORS_EXT!!!\n"));
        break;
      default:
        SM_DBG1(("smsatNonChainedVerifyCB: error default case command 0x%x!!!\n", hostToDevFis->h.command));
        break;
      }

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    } /* end error checking */
  }

  /* process success from this point on */
  switch (hostToDevFis->h.command)
  {
  case SAT_READ_VERIFY_SECTORS: /* fall through */
  case SAT_READ_VERIFY_SECTORS_EXT:
    SM_DBG5(("smsatNonChainedVerifyCB: SAT_WRITE_DMA_EXT success \n"));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext);
    break;
  default:
    SM_DBG1(("smsatNonChainedVerifyCB: success but error default case command 0x%x!!!\n", hostToDevFis->h.command));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    break;
  }

  return;
}

osGLOBAL void
smsatChainedVerifyCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     agsaFrameHandle_t agFrameHandle,
                     void              *ioContext
                    )
{
  
  smRoot_t                  *smRoot = agNULL;
  smIntRoot_t               *smIntRoot = agNULL;
  smIntContext_t            *smAllShared = agNULL;
  smIORequestBody_t         *smIORequestBody;
  smIORequestBody_t         *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatIOContext_t          *satNewIOContext;
  smSatInternalIo_t         *satIntIo;
  smSatInternalIo_t         *satNewIntIo = agNULL;
  smDeviceData_t            *oneDeviceData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smIORequest_t             *smOrgIORequest;
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                      ataStatus = 0;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  bit32                      status = tiError;
  bit32                      dataLength;

  SM_DBG2(("smsatChainedVerifyCB: start\n"));
  SM_DBG5(("smsatChainedVerifyCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatChainedVerifyCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatChainedVerifyCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatChainedVerifyCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatChainedVerifyCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     SM_DBG1(("smsatChainedVerifyCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
     smsatSetSensePayload( pSense,
                           SCSI_SNSKEY_NO_SENSE,
                           0,
                           SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                           satOrgIOContext);

     tdsmIOCompletedCB( smRoot,
                        smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                        smIOSuccess,
                        SCSI_STAT_CHECK_CONDITION,
                        satOrgIOContext->pSmSenseData,
                        satOrgIOContext->interruptContext );

     smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

     smsatFreeIntIoResource( smRoot,
                             oneDeviceData,
                             satIntIo);
     return;
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    if ( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
         ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
         )
    {
      /* for debugging */
      if( agIOStatus != OSSA_IO_SUCCESS)
      {
        SM_DBG1(("smsatChainedVerifyCB: FAILED, NOT IO_SUCCESS!!!\n"));
      }
      else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
      {
        SM_DBG1(("smsatChainedVerifyCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
      }
      else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
                (ataStatus & DF_ATA_STATUS_MASK)
                )
      {
        SM_DBG1(("smsatChainedVerifyCB: FAILED, FAILED, error status!!!\n"));
      }

      /* Process abort case */
      if (agIOStatus == OSSA_IO_ABORTED)
      {
        smsatProcessAbort(smRoot,
                          smOrgIORequest,
                          satOrgIOContext
                          );

        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
        return;
      }

      /* for debugging */
      switch (hostToDevFis->h.command)
      {
      case SAT_READ_VERIFY_SECTORS:
        SM_DBG1(("smsatChainedVerifyCB: SAT_READ_VERIFY_SECTORS!!!\n"));
        break;
      case SAT_READ_VERIFY_SECTORS_EXT:
        SM_DBG1(("smsatChainedVerifyCB: SAT_READ_VERIFY_SECTORS_EXT!!!\n"));
        break;
      default:
        SM_DBG1(("smsatChainedVerifyCB: error default case command 0x%x!!!\n", hostToDevFis->h.command));
        break;
      }

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

     tdsmIOCompletedCB( smRoot,
                        smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                        smIOSuccess,
                        SCSI_STAT_CHECK_CONDITION,
                        satOrgIOContext->pSmSenseData,
                        satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    } /* end error checking */
  }

  /* process success from this point on */
  switch (hostToDevFis->h.command)
  {
  case SAT_READ_VERIFY_SECTORS: /* fall through */
  case SAT_READ_VERIFY_SECTORS_EXT:
    SM_DBG5(("smsatChainedVerifyCB: SAT_WRITE_DMA_EXT success \n"));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    /* let's loop till TL */

    /* lba = lba + tl
       loopnum--;
       if (loopnum == 0) done
     */
    (satOrgIOContext->LoopNum)--;
    if (satOrgIOContext->LoopNum == 0)
    {
      /*
        done with write and verify
      */
     tdsmIOCompletedCB( smRoot,
                        smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                        smIOSuccess,
                        SCSI_STAT_GOOD,
                        agNULL,
                        satOrgIOContext->interruptContext );
      return;
    }

    if (satOrgIOContext->superIOFlag)
    {
      dataLength = ((tiSuperScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->scsiCmnd.expDataLength;
    }
    else
    {
      dataLength = ((tiScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->scsiCmnd.expDataLength;
    }

    satNewIntIo = smsatAllocIntIoResource( smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           dataLength,
                                           satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         satOrgIOContext->interruptContext );

      SM_DBG1(("smsatChainedVerifyCB: momory allocation fails!!!\n"));
      return;
    } /* end of memory allocation failure */

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = smsatPrepareNewIO(
                                        satNewIntIo,
                                        smOrgIORequest,
                                        oneDeviceData,
                                        scsiCmnd,
                                        satOrgIOContext
                                       );
    status = smsatChainedVerify(smRoot,
                                &satNewIntIo->satIntSmIORequest,
                                satNewIOContext->psmDeviceHandle,
                                &satNewIntIo->satIntSmScsiXchg,
                                satNewIOContext);

    if (status != SM_RC_SUCCESS)
    {
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satNewIntIo);
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         satOrgIOContext->interruptContext );
      SM_DBG1(("smsatChainedVerifyCB: calling satChainedVerify fails!!!\n"));
      return;
    }

    break;
  default:
    SM_DBG1(("smsatChainedVerifyCB: success but error default case command 0x%x!!!\n", hostToDevFis->h.command));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    break;
  }
  return;
}


osGLOBAL void
smsatTestUnitReadyCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     agsaFrameHandle_t agFrameHandle,
                     void              *ioContext
                    )
{
  /*
    In the process of TestUnitReady
    Process SAT_GET_MEDIA_STATUS
    Process SAT_CHECK_POWER_MODE
  */
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                *smRoot = agNULL;
  smIntRoot_t             *smIntRoot = agNULL;
  smIntContext_t          *smAllShared = agNULL;
  smIORequestBody_t       *smIORequestBody;
  smIORequestBody_t       *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatIOContext_t          *satNewIOContext;
  smSatInternalIo_t         *satIntIo;
  smSatInternalIo_t         *satNewIntIo = agNULL;
//  satDeviceData_t         *satDevData;
  smDeviceData_t          *oneDeviceData;

  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smIORequest_t             *smOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     ataError;

  bit32                     status;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  SM_DBG2(("smsatTestUnitReadyCB: start\n"));
  SM_DBG6(("smsatTestUnitReadyCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG5(("smsatTestUnitReadyCB: no internal smSatInternalIo_t satIntIoContext\n"));
    pSense        = satIOContext->pSense;
    scsiCmnd      = satIOContext->pScsiCmnd;
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
  }
  else
  {
    SM_DBG5(("smsatTestUnitReadyCB: yes internal smSatInternalIo_t satIntIoContext\n"));

    /* orginal smIOContext */
    smOrgIORequest         = (smIORequest_t *)satIOContext->satIntIoContext->satOrgSmIORequest;
    smOrgIORequestBody     = (smIORequestBody_t *)smOrgIORequest->tdData;
    satOrgIOContext        = &(smOrgIORequestBody->transport.SATA.satIOContext);

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agIOStatus == OSSA_IO_ABORTED)
  {
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailAborted,
                       agNULL,
                       satIOContext->interruptContext);

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    return;
  }

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatTestUnitReadyCB: agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    return;
  }
  /*
    HW checks an error for us and the results is agIOStatus
  */
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
    ataError      = statDevToHostFisHeader->error;    /* ATA Eror register   */
    if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      SM_DBG1(("smsatTestUnitReadyCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
            )
    {
      SM_DBG1(("smsatTestUnitReadyCB: FAILED, FAILED, error status!!!\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }

    switch (hostToDevFis->h.command)
    {
    case SAT_GET_MEDIA_STATUS:
      SM_DBG1(("smsatTestUnitReadyCB: SAT_GET_MEDIA_STATUS failed!!! \n"));

      /* checking NM bit */
      if (ataError & SCSI_NM_MASK)
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_NOT_READY,
                              0,
                              SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                              satOrgIOContext);
      }
      else
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_NOT_READY,
                              0,
                              SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                              satOrgIOContext);
      }

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      break;
    case SAT_CHECK_POWER_MODE:
      SM_DBG1(("smsatTestUnitReadyCB: SAT_CHECK_POWER_MODE failed!!! \n"));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NOT_READY,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_DOES_NOT_RESPOND_TO_SELECTION,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      break;
    default:
      SM_DBG1(("smsatTestUnitReadyCB: default failed command %d!!!\n", hostToDevFis->h.command));

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NOT_READY,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      break;

    }
    return;
  }/* end error */

  /* ATA command completes sucessfully */
  switch (hostToDevFis->h.command)
  {
  case SAT_GET_MEDIA_STATUS:

    SM_DBG5(("smsatTestUnitReadyCB: SAT_GET_MEDIA_STATUS success\n"));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    satNewIntIo = smsatAllocIntIoResource( smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           0,
                                           satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NOT_READY,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      SM_DBG1(("smsatTestUnitReadyCB: momory allocation fails!!!\n"));
      return;
    }

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = smsatPrepareNewIO(
                                        satNewIntIo,
                                        smOrgIORequest,
                                        oneDeviceData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );

    /* sends SAT_CHECK_POWER_MODE */
    status = smsatTestUnitReady_1( smRoot,
                                   &satNewIntIo->satIntSmIORequest,
                                   satNewIOContext->psmDeviceHandle,
                                   &satNewIntIo->satIntSmScsiXchg,
                                   satNewIOContext);

    if (status != SM_RC_SUCCESS)
    {
      /* sending SAT_CHECK_POWER_MODE fails */
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satNewIntIo);
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NOT_READY,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                        smOrgIORequest,
                        smIOSuccess,
                        SCSI_STAT_CHECK_CONDITION,
                        satOrgIOContext->pSmSenseData,
                        satOrgIOContext->interruptContext );

       SM_DBG1(("smsatTestUnitReadyCB: calling satTestUnitReady_1 fails!!!\n"));
       return;
    }

    break;
  case SAT_CHECK_POWER_MODE:
    SM_DBG5(("smsatTestUnitReadyCB: SAT_CHECK_POWER_MODE success\n"));


    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    /* returns good status */
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext );

    break;
  default:
    SM_DBG1(("smsatTestUnitReadyCB: default success command %d!!!\n", hostToDevFis->h.command));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );


    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    break;
  }

  return;
}

osGLOBAL void
smsatRequestSenseCB(
                    agsaRoot_t        *agRoot,
                    agsaIORequest_t   *agIORequest,
                    bit32             agIOStatus,
                    agsaFisHeader_t   *agFirstDword,
                    bit32             agIOInfoLen,
                    void              *agParam,
                    void              *ioContext
                   )
{
  /* ATA Vol 1, p299 SAT_SMART_RETURN_STATUS */
  /*
    if threshold exceeds, return SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE
    else call satRequestSense_1 to send CHECK_POWER_MODE
  */

//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                *smRoot = agNULL;
  smIntRoot_t             *smIntRoot = agNULL;
  smIntContext_t          *smAllShared = agNULL;
  smIORequestBody_t       *smIORequestBody;
  smIORequestBody_t       *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatIOContext_t          *satNewIOContext;
  smSatInternalIo_t         *satIntIo;
  smSatInternalIo_t         *satNewIntIo = agNULL;
//  satDeviceData_t         *satDevData;
  smDeviceData_t          *oneDeviceData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smIORequest_t             *smOrgIORequest;
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  agsaFisRegD2HData_t       statDevToHostFisData;
  bit32                     allocationLen = 0;
  bit32                     dataLength;
  bit8                      *pDataBuffer = agNULL;

  SM_DBG2(("smsatRequestSenseCB: start\n"));
  SM_DBG4(("smsatRequestSenseCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  /*ttttttthe one */
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatRequestSenseCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    if (satOrgIOContext->superIOFlag)
    {
        pDataBuffer = (bit8 *)(((tiSuperScsiInitiatorRequest_t *)satOrgIOContext->smScsiXchg)->sglVirtualAddr);//satOrgIOContext->pSense;
    }
    else
    {
        pDataBuffer = (bit8 *)(((tiScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->sglVirtualAddr);//satOrgIOContext->pSense;

    }
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
    pSense          = satOrgIOContext->pSense;
  }
  else
  {
    SM_DBG4(("smsatRequestSenseCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatRequestSenseCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatRequestSenseCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;

    if (satOrgIOContext->superIOFlag)
    {
      pDataBuffer = (bit8 *)(((tiSuperScsiInitiatorRequest_t *)satOrgIOContext->smScsiXchg)->sglVirtualAddr);//satOrgIOContext->pSense;
    }
    else
    {
      pDataBuffer = (bit8 *)(((tiScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->sglVirtualAddr);//satOrgIOContext->pSense;

    }
    scsiCmnd               = satOrgIOContext->pScsiCmnd;
    pSense                 = satOrgIOContext->pSense;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  SM_DBG4(("smsatRequestSenseCB: fis command 0x%x\n", hostToDevFis->h.command));
  
  allocationLen = scsiCmnd->cdb[4];
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength);
  SM_DBG1(("smsatRequestSenseCB: allocationLen in CDB %d 0x%x!!!\n", allocationLen,allocationLen));

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatRequestSenseCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    return;
  }

  /*
    checking IO status, FIS type and error status
  */
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */

    /* for debugging */
    if( statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      if (hostToDevFis->h.command == SAT_SMART && hostToDevFis->h.features == SAT_SMART_RETURN_STATUS)
      {
        SM_DBG1(("smsatRequestSenseCB: FAILED, Wrong FIS type 0x%x and SAT_SMART_RETURN_STATU!!!\n", statDevToHostFisHeader->fisType));
      }
      else
      {
        SM_DBG1(("smsatRequestSenseCB: FAILED, Wrong FIS type 0x%x and SAT_CHECK_POWER_MODE!!!\n",statDevToHostFisHeader->fisType));
      }
    }

    /* for debugging */
    if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      if (hostToDevFis->h.command == SAT_SMART && hostToDevFis->h.features == SAT_SMART_RETURN_STATUS)
      {
        SM_DBG1(("smsatRequestSenseCB: FAILED, error status and SAT_SMART_RETURN_STATU!!!\n"));
      }
      else
      {
        SM_DBG1(("smsatRequestSenseCB: FAILED, error status and SAT_CHECK_POWER_MODE!!!\n"));
      }
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }
    if (hostToDevFis->h.command == SAT_SMART && hostToDevFis->h.features == SAT_SMART_RETURN_STATUS)
    {
      /* report using the original tiIOrequst */
      /* failed during sending SMART RETURN STATUS */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                            satOrgIOContext);
      sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));
      if (SENSE_DATA_LENGTH < allocationLen)
      {
        /* underrun */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOUnderRun,
                           allocationLen - SENSE_DATA_LENGTH,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }
      else
      {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }
    }
    else
    {
      /* report using the original tiIOrequst */
      /* failed during sending SAT_CHECK_POWER_MODE */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_LOW_POWER_CONDITION_ON,
                            satOrgIOContext);
      sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

      if (SENSE_DATA_LENGTH < allocationLen)
      {
        /* underrun */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOUnderRun,
                           allocationLen - SENSE_DATA_LENGTH,
                           agNULL,
                           satOrgIOContext->interruptContext );
       }
       else
       {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );
       }
    }


    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  saFrameReadBlock(agRoot, agParam, 0, &statDevToHostFisData, sizeof(agsaFisRegD2HData_t));

  switch (hostToDevFis->h.command)
  {
  case SAT_SMART:
    SM_DBG4(("smsatRequestSenseCB: SAT_SMART_RETURN_STATUS case\n"));
    if (statDevToHostFisData.lbaMid == 0xF4 || statDevToHostFisData.lbaHigh == 0x2C)
    {
      /* threshold exceeds */
      SM_DBG1(("smsatRequestSenseCB: threshold exceeds!!!\n"));


      /* report using the original tiIOrequst */
      /* failed during sending SMART RETURN STATUS */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                            satOrgIOContext);
      sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

      if (SENSE_DATA_LENGTH < allocationLen)
      {
        /* underrun */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOUnderRun,
                           allocationLen - SENSE_DATA_LENGTH,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }
      else
      {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }


      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }


    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with internally genereated SAT_SMART_RETURN_STATUS */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    /* at this point, successful SMART_RETURN_STATUS
       xmit SAT_CHECK_POWER_MODE
    */
    if (satOrgIOContext->superIOFlag)
    {
        dataLength = ((tiSuperScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->scsiCmnd.expDataLength;
    }
    else
    {
        dataLength = ((tiScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->scsiCmnd.expDataLength;
    }

    satNewIntIo = smsatAllocIntIoResource( smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           dataLength,
                                           satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* failed as a part of sending SMART RETURN STATUS */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                            satOrgIOContext);
      sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

      if (SENSE_DATA_LENGTH < allocationLen)
      {
        /* underrun */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOUnderRun,
                           allocationLen - SENSE_DATA_LENGTH,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }
      else
      {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }

      SM_DBG1(("smsatRequestSenseCB: momory allocation fails!!!\n"));
      return;
    } /* end of memory allocation failure */


    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = smsatPrepareNewIO(
                                        satNewIntIo,
                                        smOrgIORequest,
                                        oneDeviceData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );

    /* sending SAT_CHECK_POWER_MODE */
    status = smsatRequestSense_1( smRoot,
                                  &satNewIntIo->satIntSmIORequest,
                                  satNewIOContext->psmDeviceHandle,
                                  &satNewIntIo->satIntSmScsiXchg,
                                  satNewIOContext);

    if (status != SM_RC_SUCCESS)
    {
      /* sending SAT_CHECK_POWER_MODE fails */
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satNewIntIo);

      /* failed during sending SAT_CHECK_POWER_MODE */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_LOW_POWER_CONDITION_ON,
                            satOrgIOContext);
      sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

      if (SENSE_DATA_LENGTH < allocationLen)
      {
        /* underrun */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOUnderRun,
                           allocationLen - SENSE_DATA_LENGTH,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }
      else
      {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }

      SM_DBG1(("smsatRequestSenseCB: calling satRequestSense_1 fails!!!\n"));
      return;
    }

    break;
  case SAT_CHECK_POWER_MODE:
    SM_DBG4(("smsatRequestSenseCB: SAT_CHECK_POWER_MODE case\n"));

    /* check ATA STANDBY state */
    if (statDevToHostFisData.sectorCount == 0x00)
    {
      /* in STANDBY */
      SM_DBG1(("smsatRequestSenseCB: in standby!!!\n"));


      /* report using the original tiIOrequst */
      /* failed during sending SAT_CHECK_POWER_MODE */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_LOW_POWER_CONDITION_ON,
                            satOrgIOContext);
      sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

      if (SENSE_DATA_LENGTH < allocationLen)
      {
        /* underrun */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOUnderRun,
                           allocationLen - SENSE_DATA_LENGTH,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }
      else
      {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with internnaly generated SAT_CHECK_POWER_MODE */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    if (oneDeviceData->satFormatState == agTRUE)
    {
      SM_DBG1(("smsatRequestSenseCB: in format!!!\n"));


      /* report using the original tiIOrequst */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NOT_READY,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_FORMAT_IN_PROGRESS,
                            satOrgIOContext);
      sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

      if (SENSE_DATA_LENGTH < allocationLen)
      {
        /* underrun */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOUnderRun,
                           allocationLen - SENSE_DATA_LENGTH,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }
      else
      {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );
      }

      return;
    }

    /* normal: returns good status for requestsense */
    /* report using the original tiIOrequst */
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);
    sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));
    SM_DBG4(("smsatRequestSenseCB: returning good status for requestsense\n"));
    if (SENSE_DATA_LENGTH < allocationLen)
    {
      /* underrun */
      SM_DBG6(("smsatRequestSenseCB reporting underrun lenNeeded=0x%x lenReceived=0x%x smIORequest=%p\n",
        SENSE_DATA_LENGTH, allocationLen, smOrgIORequest));      
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOUnderRun,
                         allocationLen - SENSE_DATA_LENGTH,
                         agNULL,
                         satOrgIOContext->interruptContext );

    }
    else
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
    }

    break;
  default:
     SM_DBG1(("smsatRequestSenseCB: success but error default case command 0x%x!!!\n", hostToDevFis->h.command));
     /* pSense here is a part of satOrgIOContext */
     pSense = satOrgIOContext->pSmSenseData->senseData;
     satOrgIOContext->pSmSenseData->senseLen = SENSE_DATA_LENGTH;
     /* unspecified case, return no sense and no addition info */
     smsatSetSensePayload( pSense,
                           SCSI_SNSKEY_NO_SENSE,
                           0,
                           SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                           satOrgIOContext);
     sm_memcpy(pDataBuffer, pSense, MIN(SENSE_DATA_LENGTH, allocationLen));

     tdsmIOCompletedCB( smRoot,
                        smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                        smIOSuccess,
                        SCSI_STAT_CHECK_CONDITION,
                        satOrgIOContext->pSmSenseData,
                        satOrgIOContext->interruptContext );

     smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

     smsatFreeIntIoResource( smRoot,
                             oneDeviceData,
                             satIntIo);
    break;
  } /* switch */

  return;

}

osGLOBAL void
smsatSendDiagnosticCB(
                       agsaRoot_t        *agRoot,
                       agsaIORequest_t   *agIORequest,
                       bit32             agIOStatus,
                       agsaFisHeader_t   *agFirstDword,
                       bit32             agIOInfoLen,
                       agsaFrameHandle_t agFrameHandle,
                       void              *ioContext
                     )
{
  /*
    In the process of SendDiagnotic
    Process READ VERIFY SECTOR(S) EXT two time
    Process SMART ECECUTE OFF-LINE IMMEDIATE
  */
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatIOContext_t         *satNewIOContext;
  smSatInternalIo_t        *satIntIo;
  smSatInternalIo_t        *satNewIntIo = agNULL;
//  satDeviceData_t         *satDevData;
  smDeviceData_t           *oneDeviceData;
  smScsiRspSense_t         *pSense;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;
  agsaFisRegHostToDevice_t *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;

  bit32                     status;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  SM_DBG2(("smsatSendDiagnosticCB: start\n"));
  SM_DBG5(("smsatSendDiagnosticCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatSendDiagnosticCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatSendDiagnosticCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatSendDiagnosticCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatSendDiagnosticCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense                 = satOrgIOContext->pSense;
    scsiCmnd               = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     SM_DBG1(("smsatSendDiagnosticCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
     oneDeviceData->satVerifyState = 0;
     oneDeviceData->satBGPendingDiag = agFALSE;

    if (hostToDevFis->d.lbaLow != 0x01 && hostToDevFis->d.lbaLow != 0x02)
    {
      /* no completion for background send diagnotic. It is done in satSendDiagnostic() */
      tdsmIOCompletedCB(
                         smRoot,
                         smOrgIORequest,
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         satOrgIOContext->interruptContext
                        );
     }
     smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

     smsatFreeIntIoResource( smRoot,
                             oneDeviceData,
                              satIntIo);
    return;

  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  SM_DBG5(("smsatSendDiagnosticCB: fis command 0x%x\n", hostToDevFis->h.command));

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
  /*
    checking IO status, FIS type and error status
  */
  oneDeviceData->satVerifyState = 0;
  oneDeviceData->satBGPendingDiag = agFALSE;

  if( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
      ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
      )
  {

    /* for debugging */
    if( agIOStatus != OSSA_IO_SUCCESS)
    {
      if ( hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT )
      {
        SM_DBG1(("smsatSendDiagnosticCB: FAILED, NOT IO_SUCCESS and SAT_READ_VERIFY_SECTORS(_EXT)!!!\n"));
      }
      else
      {
        SM_DBG1(("smsatSendDiagnosticCB: FAILED, NOT IO_SUCCESS and SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE!!!\n"));
      }
    }

    /* for debugging */
    if( statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      if ( hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT )
      {
        SM_DBG1(("smsatSendDiagnosticCB: FAILED, Wrong FIS type 0x%x and SAT_READ_VERIFY_SECTORS(_EXT)!!!\n", statDevToHostFisHeader->fisType));
      }
      else
      {
        SM_DBG1(("smsatSendDiagnosticCB: FAILED, Wrong FIS type 0x%x and SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE!!!\n",statDevToHostFisHeader->fisType));
      }
    }

    /* for debugging */
    if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      if ( hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT )
      {
        SM_DBG1(("smsatSendDiagnosticCB: FAILED, error status and SAT_READ_VERIFY_SECTORS(_EXT)!!!\n"));
      }
      else
      {
        SM_DBG1(("smsatSendDiagnosticCB: FAILED, error status and SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE!!!\n"));
      }
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }

    if ( (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS) ||
         (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT) )
    {
      /* report using the original tiIOrequst */
      /* failed during sending SAT_READ_VERIFY_SECTORS(_EXT) */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }
    else
    {
      /* report using the original tiIOrequst */
      /* failed during sending SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE */
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST,
                            satOrgIOContext);

      if (hostToDevFis->d.lbaLow != 0x01 && hostToDevFis->d.lbaLow != 0x02)
      {
        /* no completion for background send diagnotic. It is done in satSendDiagnostic() */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );

      }
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }
  }
  }

  /* processing success case */
  switch (hostToDevFis->h.command)
  {
  case SAT_READ_VERIFY_SECTORS:     /* fall through */
  case SAT_READ_VERIFY_SECTORS_EXT:
    SM_DBG5(("smsatSendDiagnosticCB: SAT_READ_VERIFY_SECTORS(_EXT) case\n"));
    tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
    oneDeviceData->satVerifyState++;
    tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
    SM_DBG5(("smsatSendDiagnosticCB: satVerifyState %d\n",oneDeviceData->satVerifyState));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with internally genereated AT_READ_VERIFY_SECTORS(_EXT) */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    if (oneDeviceData->satVerifyState == 3)
    {
      /* reset satVerifyState */
      oneDeviceData->satVerifyState = 0;
      /* return GOOD status */
      SM_DBG5(("smsatSendDiagnosticCB: return GOOD status\n"));
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
     return;
    }
    else
    {
      /* prepare SAT_READ_VERIFY_SECTORS(_EXT) */
      satNewIntIo = smsatAllocIntIoResource( smRoot,
                                             smOrgIORequest,
                                             oneDeviceData,
                                             0,
                                             satNewIntIo);
      if (satNewIntIo == agNULL)
      {
        /* reset satVerifyState */
        oneDeviceData->satVerifyState = 0;

        /* failed as a part of sending SAT_READ_VERIFY_SECTORS(_EXT) */
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_HARDWARE_ERROR,
                              0,
                              SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );

        SM_DBG1(("smsatSendDiagnosticCB: momory allocation fails!!!\n"));
        return;
      } /* end of memory allocation failure */

      /*
       * Need to initialize all the fields within satIOContext
       */

      satNewIOContext = smsatPrepareNewIO(
                                          satNewIntIo,
                                          smOrgIORequest,
                                          oneDeviceData,
                                          scsiCmnd,
                                          satOrgIOContext
                                          );

      if (oneDeviceData->satVerifyState == 1)
      {
        /* sending SAT_CHECK_POWER_MODE */
        status = smsatSendDiagnostic_1( smRoot,
                                        &satNewIntIo->satIntSmIORequest,
                                        satNewIOContext->psmDeviceHandle,
                                        &satNewIntIo->satIntSmScsiXchg,
                                        satNewIOContext);
      }
      else
      {
        /* oneDeviceData->satVerifyState == 2 */
        status = smsatSendDiagnostic_2( smRoot,
                                        &satNewIntIo->satIntSmIORequest,
                                        satNewIOContext->psmDeviceHandle,
                                        &satNewIntIo->satIntSmScsiXchg,
                                        satNewIOContext);
      }

      if (status != SM_RC_SUCCESS)
      {
        /* sending SAT_READ_VERIFY_SECTORS(_EXT) fails */
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satNewIntIo);

        /* failed during sending SAT_READ_VERIFY_SECTORS(_EXT) */
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_HARDWARE_ERROR,
                              0,
                              SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );

        /* reset satVerifyState */
        oneDeviceData->satVerifyState = 0;
        SM_DBG1(("smsatSendDiagnosticCB: calling satSendDiagnostic_1 or _2 fails!!!\n"));
        return;
      }
    } /* oneDeviceData->satVerifyState == 1 or 2 */

    break;
  case SAT_SMART:
    if (hostToDevFis->h.features == SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE)
    {
      SM_DBG5(("smsatSendDiagnosticCB: SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE case\n"));

      oneDeviceData->satBGPendingDiag = agFALSE;

      if (hostToDevFis->d.lbaLow == 0x01 || hostToDevFis->d.lbaLow == 0x02)
      {
        /* for background send diagnostic, no completion here. It is done already. */
        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        /* done with AT_SMART_EXEUTE_OFF_LINE_IMMEDIATE */
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
        SM_DBG5(("smsatSendDiagnosticCB: returning but no IOCompleted\n"));
      }
      else
      {
        SM_DBG5(("smsatSendDiagnosticCB: returning good status for senddiagnostic\n"));
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        /* done with AT_SMART_EXEUTE_OFF_LINE_IMMEDIATE */
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
    }
    break;
  default:
    SM_DBG1(("smsatSendDiagnosticCB: success but error default case command 0x%x!!!\n", hostToDevFis->h.command));
    /* unspecified case, return no sense and no addition info */
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    break;
  }
  return;

}

osGLOBAL void
smsatStartStopUnitCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     agsaFrameHandle_t agFrameHandle,
                     void              *ioContext
                    )
{
  /*
    In the process of StartStopUnit
    Process FLUSH CACHE (EXT)
    Process STANDBY
    Process READ VERIFY SECTOR(S) EXT
    Process MEDIA EJECT
  */
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatIOContext_t         *satNewIOContext;
  smSatInternalIo_t        *satIntIo;
  smSatInternalIo_t        *satNewIntIo = agNULL;
//  satDeviceData_t         *satDevData;
  smDeviceData_t           *oneDeviceData;
  smScsiRspSense_t         *pSense;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;
  agsaFisRegHostToDevice_t *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  SM_DBG2(("smsatStartStopUnitCB: start\n"));
  SM_DBG5(("smsatStartStopUnitCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatStartStopUnitCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatStartStopUnitCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatStartStopUnitCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatStartStopUnitCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     SM_DBG1(("smsatStartStopUnitCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));

      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        SM_DBG1(("smsatStartStopUnitCB: immed bit 0!!!\n"));
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ABORTED_COMMAND,
                              0,
                              SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        SM_DBG1(("smsatStartStopUnitCB: immed bit 1!!!\n"));
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_ABORTED_COMMAND,
                                      0,
                                      SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                      satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
     }



    return;
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }
  /*
    checking IO status, FIS type and error status
  */
  if( agIOStatus != OSSA_IO_SUCCESS)
  {
  if( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
      ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
      )
  {
    /* for debugging */
    if( agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatStartStopUnitCB: FAILED, NOT IO_SUCCESS!!!\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      SM_DBG1(("smsatStartStopUnitCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      SM_DBG1(("smsatStartStopUnitCB: FAILED, FAILED, error status!!!\n"));
    }


    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );


      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }

    switch (hostToDevFis->h.command)
    {
    case SAT_FLUSH_CACHE: /* fall through */
    case SAT_FLUSH_CACHE_EXT:
      SM_DBG1(("smsatStartStopUnitCB: SAT_FLUSH_CACHE(_EXT)!!!\n"));
      /* check immed bit in scsi command */
      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ABORTED_COMMAND,
                              0,
                              SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_ABORTED_COMMAND,
                                      0,
                                      SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                      satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
      break;
    case SAT_STANDBY:
      SM_DBG5(("smsatStartStopUnitCB: SAT_STANDBY\n"));
      /* check immed bit in scsi command */
      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ABORTED_COMMAND,
                              0,
                              SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_ABORTED_COMMAND,
                                      0,
                                      SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                      satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
      break;
    case SAT_READ_VERIFY_SECTORS:     /* fall through */
    case SAT_READ_VERIFY_SECTORS_EXT:
      SM_DBG5(("smsatStartStopUnitCB: SAT_READ_VERIFY_SECTORS(_EXT)\n"));
       /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ABORTED_COMMAND,
                              0,
                              SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_ABORTED_COMMAND,
                                      0,
                                      SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                      satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
      break;
    case SAT_MEDIA_EJECT:
      SM_DBG5(("smsatStartStopUnitCB: SAT_MEDIA_EJECT\n"));
       /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ABORTED_COMMAND,
                              0,
                              SCSI_SNSCODE_MEDIA_LOAD_OR_EJECT_FAILED,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );


        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_ABORTED_COMMAND,
                                      0,
                                      SCSI_SNSCODE_MEDIA_LOAD_OR_EJECT_FAILED,
                                      satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );

        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
      }
      break;
    default:
      /* unspecified case, return no sense and no addition info */
      SM_DBG5(("smsatStartStopUnitCB: default command %d\n", hostToDevFis->h.command));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );


      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      break;
    } /* switch */

    return;
  } /* error check */
  }

  /* ATA command completes sucessfully */
  switch (hostToDevFis->h.command)
  {
  case SAT_FLUSH_CACHE: /* fall through */
  case SAT_FLUSH_CACHE_EXT:
    SM_DBG5(("smsatStartStopUnitCB: SAT_READ_VERIFY_SECTORS(_EXT) success case\n"));


    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with SAT_FLUSH_CACHE(_EXT) */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    /* at this point, successful SAT_READ_VERIFY_SECTORS(_EXT)
       send SAT_SATNDBY
    */
    satNewIntIo = smsatAllocIntIoResource( smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           0,
                                           satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ABORTED_COMMAND,
                              0,
                              SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                              satOrgIOContext);
      }
      else   /* IMMED == 1 */
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_ABORTED_COMMAND,
                                      0,
                                      SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                      satOrgIOContext);
      }
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      SM_DBG1(("smsatStartStopUnitCB: momory allocation fails!!!\n"));
      return;
    } /* end of memory allocation failure */

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = smsatPrepareNewIO(
                                        satNewIntIo,
                                        smOrgIORequest,
                                        oneDeviceData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );

    /* sending SAT_STANDBY */
    status = smsatStartStopUnit_1( smRoot,
                                   &satNewIntIo->satIntSmIORequest,
                                   satNewIOContext->psmDeviceHandle,
                                   &satNewIntIo->satIntSmScsiXchg,
                                   satNewIOContext);

    if (status != SM_RC_SUCCESS)
    {
      /* sending SAT_CHECK_POWER_MODE fails */
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satNewIntIo);

      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_ABORTED_COMMAND,
                              0,
                              SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                              satOrgIOContext);
      }
      else   /* IMMED == 1 */
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_ABORTED_COMMAND,
                                      0,
                                      SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                      satOrgIOContext);
      }
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      SM_DBG1(("smsatStartStopUnitCB: calling satStartStopUnit_1 fails!!!\n"));
      return;
    }
    break;
  case SAT_STANDBY:
    SM_DBG5(("smsatStartStopUnitCB: SAT_STANDBY success case\n"));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with SAT_STANDBY */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    /*
      if immed == 0, return good status
     */
    /* IMMED == 0 */
    if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
    {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );
    }
    oneDeviceData->satStopState = agTRUE;
    break;
  case SAT_READ_VERIFY_SECTORS:     /* fall through */
  case SAT_READ_VERIFY_SECTORS_EXT:
    SM_DBG5(("smsatStartStopUnitCB: SAT_READ_VERIFY_SECTORS(_EXT) success case\n"));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with SAT_READ_VERIFY_SECTORS(_EXT) */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    /*
      if immed == 0, return good status
     */
    if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
    {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext );
    }
    /*
      if immed == 0, return good status
     */
    /*
      don't forget to check and set driver state; Active power state
    */
    oneDeviceData->satStopState = agFALSE;
    break;
  case SAT_MEDIA_EJECT:
    SM_DBG5(("smsatStartStopUnitCB: SAT_MEDIA_EJECT success case\n"));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with SAT_READ_VERIFY_SECTORS(_EXT) */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    /*
      if immed == 0, return good status
     */
    if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
    }
    break;
  default:
    SM_DBG1(("smsatStartStopUnitCB:success but  error default case command 0x%x!!!\n", hostToDevFis->h.command));

    /* unspecified case, return no sense and no addition info */
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    break;
  }
  return;

}

osGLOBAL void
smsatWriteSame10CB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                  )
{
  smRoot_t                *smRoot = agNULL;
  smIntRoot_t             *smIntRoot = agNULL;
  smIntContext_t          *smAllShared = agNULL;
  smIORequestBody_t       *smIORequestBody;
  smIORequestBody_t       *smOrgIORequestBody;
  smIORequestBody_t       *smNewIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatIOContext_t          *satNewIOContext;
  smSatInternalIo_t         *satIntIo;
  smSatInternalIo_t         *satNewIntIo = agNULL;
  smDeviceData_t          *oneDeviceData;
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smIORequest_t             *smOrgIORequest;
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  bit32                     sectorcount = 0;
  bit32                     lba = 0, tl = 0;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  agsaFisSetDevBitsHeader_t *statSetDevBitFisHeader = agNULL;

  SM_DBG2(("smsatWriteSame10CB: start\n"));
  SM_DBG5(("smsatWriteSame10CB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatWriteSame10CB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatWriteSame10CB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatWriteSame10CB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatWriteSame10CB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }


  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     SM_DBG1(("smsatWriteSame10CB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
     smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

     tdsmIOCompletedCB( smRoot,
                        smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                        smIOSuccess,
                        SCSI_STAT_CHECK_CONDITION,
                        satOrgIOContext->pSmSenseData,
                        satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* FP, DMA and PIO write */
    /* First, assumed to be Reg Device to Host FIS */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    if (statDevToHostFisHeader->fisType == SET_DEV_BITS_FIS)
    {
      statSetDevBitFisHeader = (agsaFisSetDevBitsHeader_t *)&(agFirstDword->D2H);

      /* Get ATA Status register */
      ataStatus = (statSetDevBitFisHeader->statusHi_Lo & 0x70);               /* bits 4,5,6 */
      ataStatus = ataStatus | (statSetDevBitFisHeader->statusHi_Lo & 0x07);   /* bits 0,1,2 */
    }
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
  /*
    checking IO status, FIS type and error status
    FIS type should be either REG_DEV_TO_HOST_FIS or SET_DEV_BITS_FIS
  */
  if (  ((statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) &&
        (statDevToHostFisHeader->fisType != SET_DEV_BITS_FIS)) ||
       ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
       )
  {
    /* for debugging */
    if( agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatWriteSame10CB: FAILED, NOT IO_SUCCESS!!!\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      SM_DBG1(("smsatWriteSame10CB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if (statDevToHostFisHeader->fisType != SET_DEV_BITS_FIS)
    {
      SM_DBG1(("smsatWriteSame10CB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      SM_DBG1(("smsatWriteSame10CB: FAILED, FAILED, error status!!!\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );


      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }

    /* for debugging */
    switch (hostToDevFis->h.command)
    {
    case SAT_WRITE_DMA_EXT:
      SM_DBG1(("smsatWriteSame10CB: SAT_WRITE_DMA_EXT!!!\n"));
      break;
    case SAT_WRITE_SECTORS_EXT:
      SM_DBG1(("smsatWriteSame10CB: SAT_WRITE_SECTORS_EXT!!!\n"));
      break;
    case SAT_WRITE_FPDMA_QUEUED:
      SM_DBG1(("smsatWriteSame10CB: SAT_WRITE_FPDMA_QUEUED!!!\n"));
      break;
    default:
      SM_DBG1(("smsatWriteSame10CB: error default case command 0x%x!!!\n", hostToDevFis->h.command));
      break;
    }

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );


    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  } /* end error */
  }

  /* process success from this point on */
  /*
    note: inefficient implementation until a single block can be manipulated
  */

  if (hostToDevFis->h.command == SAT_WRITE_DMA_EXT)
  {
    SM_DBG5(("smsatWriteSame10CB: SAT_WRITE_DMA_EXT success\n"));
  }
  else if (hostToDevFis->h.command == SAT_WRITE_SECTORS_EXT)
  {
    SM_DBG5(("smsatWriteSame10CB: SAT_WRITE_SECTORS_EXT success\n"));
  }
  else if (hostToDevFis->h.command == SAT_WRITE_FPDMA_QUEUED)
  {
    SM_DBG5(("smsatWriteSame10CB: SAT_WRITE_FPDMA_QUEUED success\n"));
  }
  else
  {
    SM_DBG1(("smsatWriteSame10CB: error case command 0x%x success!!!\n", hostToDevFis->h.command));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );


    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }


  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  /* free */
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);

  /*
    increment LBA by one, keeping the same sector count(1)
    sends another ATA command with the changed parameters
  */

  tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
  oneDeviceData->satSectorDone++;
  tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);

  SM_DBG1(("smsatWriteSame10CB: sectordone %d!!!\n", oneDeviceData->satSectorDone));

  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
      + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

  SM_DBG5(("smsatWriteSame10CB: lba 0x%x tl 0x%x\n", lba, tl));

  if (tl == 0)
  {
    /* (oneDeviceData->satMaxUserAddrSectors - 1) - lba*/
    sectorcount = (0x0FFFFFFF - 1) - lba;
  }
  else
  {
    sectorcount = tl;
  }

  if (sectorcount <= 0)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );
    SM_DBG1(("smsatWriteSame10CB: incorrect sectorcount 0x%x!!!\n", sectorcount));
    return;
  }

  if (sectorcount == oneDeviceData->satSectorDone)
  {
    /*
      done with writesame
    */
    SM_DBG1(("smsatWriteSame10CB: return writesame done!!!\n"));
    oneDeviceData->satSectorDone = 0;

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext );
  }
  else
  {
    /* sends another ATA command */
    if (hostToDevFis->h.command == SAT_WRITE_DMA_EXT)
    {
      SM_DBG1(("smsatWriteSame10CB: sends another SAT_WRITE_DMA_EXT!!!\n"));
    }
    else if (hostToDevFis->h.command == SAT_WRITE_SECTORS_EXT)
    {
      SM_DBG1(("smsatWriteSame10CB: sends another SAT_WRITE_SECTORS_EXT!!!\n"));
    }
    else if (hostToDevFis->h.command == SAT_WRITE_FPDMA_QUEUED)
    {
      SM_DBG1(("smsatWriteSame10CB: sends another SAT_WRITE_FPDMA_QUEUED!!!\n"));
    }

    satNewIntIo = smsatAllocIntIoResource( smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           0,
                                           satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );
      SM_DBG1(("smsatWriteSame10CB: momory allocation fails!!!\n"));
      return;
    } /* end memory allocation */

    /* the one to be used */
    smNewIORequestBody = satNewIntIo->satIntRequestBody;
    satNewIOContext = &smNewIORequestBody->transport.SATA.satIOContext;

    satNewIOContext->pSatDevData   = oneDeviceData;
    satNewIOContext->pFis          = &smNewIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev;
    satNewIOContext->pScsiCmnd     = &satNewIntIo->satIntSmScsiXchg.scsiCmnd;
    /* saves scsi command for LBA and number of blocks */
    sm_memcpy(satNewIOContext->pScsiCmnd, scsiCmnd, sizeof(smIniScsiCmnd_t));
    satNewIOContext->pSense        = &smNewIORequestBody->transport.SATA.sensePayload;
    satNewIOContext->pSmSenseData  = &smNewIORequestBody->transport.SATA.smSenseData;
    satNewIOContext->pSmSenseData->senseData = satNewIOContext->pSense;
    satNewIOContext->smRequestBody = satNewIntIo->satIntRequestBody;
    satNewIOContext->interruptContext = satNewIOContext->interruptContext;
    satNewIOContext->satIntIoContext  = satNewIntIo;
    satNewIOContext->psmDeviceHandle = satIOContext->psmDeviceHandle;
    /* saves smScsiXchg; only for writesame10() */
    satNewIOContext->smScsiXchg = satOrgIOContext->smScsiXchg;

    if (hostToDevFis->h.command == SAT_WRITE_DMA_EXT)
    {
      status = smsatWriteSame10_1( smRoot,
                                   &satNewIntIo->satIntSmIORequest,
                                   satNewIOContext->psmDeviceHandle,
                                   &satNewIntIo->satIntSmScsiXchg,
                                   satNewIOContext,
                                   lba + oneDeviceData->satSectorDone
                                   );
    }
    else if (hostToDevFis->h.command == SAT_WRITE_SECTORS_EXT)
    {
      status = smsatWriteSame10_2( smRoot,
                                   &satNewIntIo->satIntSmIORequest,
                                   satNewIOContext->psmDeviceHandle,
                                   &satNewIntIo->satIntSmScsiXchg,
                                   satNewIOContext,
                                   lba + oneDeviceData->satSectorDone
                                  );
    }
    else if (hostToDevFis->h.command == SAT_WRITE_FPDMA_QUEUED)
    {
      status = smsatWriteSame10_3( smRoot,
                                   &satNewIntIo->satIntSmIORequest,
                                   satNewIOContext->psmDeviceHandle,
                                   &satNewIntIo->satIntSmScsiXchg,
                                   satNewIOContext,
                                   lba + oneDeviceData->satSectorDone
                                  );
    }
    else
    {
      status = tiError;
      SM_DBG1(("smsatWriteSame10CB: sucess but error in command 0x%x!!!\n", hostToDevFis->h.command));
    }

    if (status != SM_RC_SUCCESS)
    {
      /* sending ATA command fails */
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satNewIntIo);
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );
      SM_DBG1(("smsatWriteSame10CB:calling satWriteSame10_1 fails!!!\n"));
      return;
    } /* end send fails */

  } /* end sends another ATA command */

  return;

}

osGLOBAL void
smsatLogSenseCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext
               )
{
  smRoot_t                  *smRoot = agNULL;
  smIntRoot_t               *smIntRoot = agNULL;
  smIntContext_t            *smAllShared = agNULL;
  smIORequestBody_t         *smIORequestBody;
  smIORequestBody_t         *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatInternalIo_t         *satIntIo;
//  satDeviceData_t          *satDevData;
  smDeviceData_t            *oneDeviceData;

  smScsiRspSense_t          *pSense;
  smIORequest_t             *smOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                      ataStatus = 0;
  smScsiInitiatorRequest_t  *smScsiRequest; /* tiScsiXchg */
  smScsiInitiatorRequest_t  *smOrgScsiRequest; /* tiScsiXchg */
  satReadLogExtSelfTest_t   *virtAddr1;
  satSmartReadLogSelfTest_t *virtAddr2;
  bit8                      *pLogPage;
  bit8                      LogPage[SELFTEST_RESULTS_LOG_PAGE_LENGTH];
  bit8                       SelfTestExecutionStatus = 0;
  bit32                      i = 0;

  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  agsaFisRegD2HData_t        statDevToHostFisData;
  smIniScsiCmnd_t           *scsiCmnd;
  bit32                      allocationLen = 0;

  SM_DBG2(("smsatLogSenseCB: start\n"));
  SM_DBG5(("smsatLogSenseCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  if (satIOContext == agNULL)
  {
    SM_DBG1(("smsatLogSenseCB: satIOContext is NULL\n"));
    return;
  }
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatLogSenseCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
    smOrgScsiRequest   = satOrgIOContext->smScsiXchg;
    /* SCSI command response payload to OS layer */
    pLogPage        = (bit8 *) smOrgScsiRequest->sglVirtualAddr;
    /* ATA command response payload */
    smScsiRequest   = satOrgIOContext->smScsiXchg;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatLogSenseCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;

    pSense        = satOrgIOContext->pSense;
    smOrgScsiRequest   = satOrgIOContext->smScsiXchg;
    /* SCSI command response payload to OS layer */
    pLogPage        = (bit8 *) smOrgScsiRequest->sglVirtualAddr;
    /* ATA command response payload */
    smScsiRequest   =  (smScsiInitiatorRequest_t *)&(satIntIo->satIntSmScsiXchg);
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatLogSenseCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* non-data and pio read -> device to host and pio setup fis are expected */
    /*
      first, assumed to be Reg Device to Host FIS
      This is OK to just find fis type
    */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
  if ( ((statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) &&
        (statDevToHostFisHeader->fisType != PIO_SETUP_DEV_TO_HOST_FIS)) ||
       ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
       )
  {
    /* for debugging */
    if( agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatLogSenseCB: FAILED, NOT IO_SUCCESS!!!\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      SM_DBG1(("smsatLogSenseCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if (statDevToHostFisHeader->fisType != PIO_SETUP_DEV_TO_HOST_FIS)
    {
      SM_DBG1(("smsatLogSenseCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      SM_DBG1(("smsatLogSenseCB: FAILED, FAILED, error status!!!\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }

    /* for debugging */
    if (hostToDevFis->h.command == SAT_READ_LOG_EXT)
    {
      SM_DBG1(("smsatLogSenseCB: SAT_READ_LOG_EXT failed!!!\n"));
    }
    else if (hostToDevFis->h.command == SAT_SMART)
    {
      if (hostToDevFis->h.features == SAT_SMART_READ_LOG)
      {
        SM_DBG1(("smsatLogSenseCB: SAT_SMART_READ_LOG failed!!!\n"));
      }
      else if (hostToDevFis->h.features == SAT_SMART_RETURN_STATUS)
      {
        SM_DBG1(("smsatLogSenseCB: SAT_SMART_RETURN_STATUS failed!!!\n"));
      }
      else
      {
        SM_DBG1(("smsatLogSenseCB: error unknown command 0x%x feature 0x%x!!!\n", hostToDevFis->h.command, hostToDevFis->h.features));
      }
    }
    else
    {
      SM_DBG1(("smsatLogSenseCB: error default case command 0x%x!!!\n", hostToDevFis->h.command));
    }

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;

  } /* error checking */
  }

  /* prcessing the success case */
  saFrameReadBlock(agRoot, agParam, 0, &statDevToHostFisData, sizeof(agsaFisRegD2HData_t));

  allocationLen = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength);
  SM_DBG5(("smsatLogSenseCB: allocationLen in CDB %d 0x%x\n", allocationLen,allocationLen));


  if (hostToDevFis->h.command == SAT_READ_LOG_EXT)
  {
    SM_DBG5(("smsatLogSenseCB: SAT_READ_LOG_EXT success\n"));

    /* process log data and sends it to upper */

    /* ATA: Extended Self-Test Log */
    virtAddr1  = (satReadLogExtSelfTest_t *)(smScsiRequest->sglVirtualAddr);
    /*
      ATA/ATAPI VOLII, p197, 287
      self-test execution status (4 bits); ((virtAddr1->byte[5] & 0xF0) >> 4)
    */
    SelfTestExecutionStatus  = (bit8)(((virtAddr1->byte[5] & 0xF0) >> 4));

    /* fills in the log page from ATA log page */
    /* SPC-4, 7.2.10, Table 216, 217, p 259 - 260 */
    LogPage[0] = 0x10; /* page code */
    LogPage[1] = 0;
    LogPage[2] = 0x01;    /* 0x190, page length */
    LogPage[3] = 0x90;

    /* SPC-4, Table 217 */
    LogPage[4] = 0;    /* Parameter Code */
    LogPage[5] = 0x01; /* Parameter Code,  unspecfied but ... */
    LogPage[6] = 3;    /* unspecified but ... */
    LogPage[7] = 0x10; /* Parameter Length */
    LogPage[8] = (bit8)(0 | ((virtAddr1->byte[5] & 0xF0) >> 4)); /* Self Test Code and Self-Test Result */
    LogPage[9] = 0;    /* self test number */
    LogPage[10] = virtAddr1->byte[7];    /* time stamp, MSB */
    LogPage[11] = virtAddr1->byte[6];    /* time stamp, LSB */

    LogPage[12] = 0;    /* address of first failure MSB*/
    LogPage[13] = 0;    /* address of first failure */
    LogPage[14] = virtAddr1->byte[14];    /* address of first failure */
    LogPage[15] = virtAddr1->byte[13];    /* address of first failure */
    LogPage[16] = virtAddr1->byte[12];    /* address of first failure */
    LogPage[17] = virtAddr1->byte[11];    /* address of first failure */
    LogPage[18] = virtAddr1->byte[10];    /* address of first failure */
    LogPage[19] = virtAddr1->byte[9];    /* address of first failure LSB */

    /* SAT rev8 Table75, p 76 */
    switch (SelfTestExecutionStatus)
    {
    case 0:
      LogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
      LogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
      LogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
      break;
    case 1:
      LogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
      LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      LogPage[22] = 0x81;
      break;
    case 2:
      LogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
      LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      LogPage[22] = 0x82;
      break;
    case 3:
      LogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
      LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      LogPage[22] = 0x83;
      break;
    case 4:
      LogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
      LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      LogPage[22] = 0x84;
    break;
    case 5:
      LogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
      LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      LogPage[22] = 0x85;
      break;
    case 6:
      LogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
      LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      LogPage[22] = 0x86;
      break;
    case 7:
      LogPage[20] = 0 | SCSI_SNSKEY_MEDIUM_ERROR;
      LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      LogPage[22] = 0x87;
      break;
    case 8:
      LogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
      LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      LogPage[22] = 0x88;
      break;
    case 9: /* fall through */
    case 10:/* fall through */
    case 11:/* fall through */
    case 12:/* fall through */
    case 13:/* fall through */
    case 14:
      LogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
      LogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
      LogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
      break;
    case 15:
      LogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
      LogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
      LogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
      break;
    default:
      SM_DBG1(("smsatLogSenseCB: Error, incorrect SelfTestExecutionStatus 0x%x!!!\n", SelfTestExecutionStatus));

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);

      return;
    }

    LogPage[23] = 0;    /* vendor specific */

    /* the rest of Self-test results log */
    /* 403 is from SPC-4, 7.2.10, Table 216, p 259*/
    for (i=24;i<=403;i++)
    {
      LogPage[i] = 0;    /* vendor specific */
    }

    sm_memcpy(pLogPage, LogPage, MIN(allocationLen, SELFTEST_RESULTS_LOG_PAGE_LENGTH));
    if (SELFTEST_RESULTS_LOG_PAGE_LENGTH < allocationLen)
    {
      SM_DBG6(("smsatLogSenseCB: 1st underrun allocationLen %d len %d \n", allocationLen, SELFTEST_RESULTS_LOG_PAGE_LENGTH));

      /* underrun */
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                         smIOUnderRun,
                         allocationLen - SELFTEST_RESULTS_LOG_PAGE_LENGTH,
                         agNULL,
                         satOrgIOContext->interruptContext );

    }
    else
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext);
    }

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    return;
  }
  else if (hostToDevFis->h.command == SAT_SMART)
  {
    if (hostToDevFis->h.features == SAT_SMART_READ_LOG)
    {
      SM_DBG5(("smsatLogSenseCB: SAT_SMART_READ_LOG success\n"));
      /* process log data and sends it to upper */

      /* ATA: Extended Self-Test Log */
      virtAddr2  = (satSmartReadLogSelfTest_t *)(smScsiRequest->sglVirtualAddr);
      /*
        SPC-4, p197, 287
        self-test execution status (4 bits); ((virtAddr2->byte[3] & 0xF0) >> 4)
      */
      SelfTestExecutionStatus  = (bit8)(((virtAddr2->byte[3] & 0xF0) >> 4));

      /* fills in the log page from ATA log page */
      /* SPC-4, 7.2.10, Table 216, 217, p 259 - 260 */
      LogPage[0] = 0x10;    /* page code */
      LogPage[1] = 0;
      LogPage[2] = 0x01;    /* 0x190, page length */
      LogPage[3] = 0x90;    /* 0x190, page length */

      /* SPC-4, Table 217 */
      LogPage[4] = 0;    /* Parameter Code */
      LogPage[5] = 0x01; /* Parameter Code unspecfied but ... */
      LogPage[6] = 3;    /* unspecified but ... */
      LogPage[7] = 0x10; /* Parameter Length */
      LogPage[8] = (bit8)(0 | ((virtAddr2->byte[3] & 0xF0) >> 4)); /* Self Test Code and Self-Test Result */
      LogPage[9] = 0;    /* self test number */
      LogPage[10] = virtAddr2->byte[5];    /* time stamp, MSB */
      LogPage[11] = virtAddr2->byte[4];    /* time stamp, LSB */

      LogPage[12] = 0;    /* address of first failure MSB*/
      LogPage[13] = 0;    /* address of first failure */
      LogPage[14] = 0;    /* address of first failure */
      LogPage[15] = 0;    /* address of first failure */
      LogPage[16] = virtAddr2->byte[10];    /* address of first failure */
      LogPage[17] = virtAddr2->byte[9];    /* address of first failure */
      LogPage[18] = virtAddr2->byte[8];    /* address of first failure */
      LogPage[19] = virtAddr2->byte[7];    /* address of first failure LSB */

      /* SAT rev8 Table75, p 76 */
      switch (SelfTestExecutionStatus)
      {
      case 0:
        LogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
        LogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
        LogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
        break;
      case 1:
        LogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
        LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        LogPage[22] = 0x81;
        break;
      case 2:
        LogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
        LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        LogPage[22] = 0x82;
        break;
      case 3:
        LogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
        LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        LogPage[22] = 0x83;
        break;
      case 4:
        LogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
        LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        LogPage[22] = 0x84;
        break;
      case 5:
        LogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
        LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        LogPage[22] = 0x85;
        break;
      case 6:
        LogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
        LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        LogPage[22] = 0x86;
        break;
      case 7:
        LogPage[20] = 0 | SCSI_SNSKEY_MEDIUM_ERROR;
        LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        LogPage[22] = 0x87;
        break;
      case 8:
        LogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
        LogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        LogPage[22] = 0x88;
        break;
      case 9: /* fall through */
      case 10:/* fall through */
      case 11:/* fall through */
      case 12:/* fall through */
      case 13:/* fall through */
      case 14:
        /* unspecified */
        LogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
        LogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
        LogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
        break;
      case 15:
        LogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
        LogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
        LogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
        break;
      default:
        SM_DBG1(("smsatLogSenseCB: Error, incorrect SelfTestExecutionStatus 0x%x!!!\n", SelfTestExecutionStatus));

        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_NO_SENSE,
                              0,
                              SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );

        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);

        return;
      }

      LogPage[23] = 0;    /* vendor specific */

      /* the rest of Self-test results log */
      /* 403 is from SPC-4, 7.2.10, Table 216, p 259*/
      for (i=24;i<=403;i++)
      {
        LogPage[i] = 0;    /* vendor specific */
      }

      sm_memcpy(pLogPage, LogPage, MIN(allocationLen, SELFTEST_RESULTS_LOG_PAGE_LENGTH));
      if (SELFTEST_RESULTS_LOG_PAGE_LENGTH < allocationLen)
      {
        SM_DBG6(("smsatLogSenseCB: 2nd underrun allocationLen %d len %d \n", allocationLen, SELFTEST_RESULTS_LOG_PAGE_LENGTH));

        /* underrun */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOUnderRun,
                           allocationLen - SELFTEST_RESULTS_LOG_PAGE_LENGTH,
                           agNULL,
                           satOrgIOContext->interruptContext );

      }
      else
      {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext);
      }
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);

      return;
    }
    else if (hostToDevFis->h.features == SAT_SMART_RETURN_STATUS)
    {
      SM_DBG5(("smsatLogSenseCB: SAT_SMART_RETURN_STATUS success\n"));

      /* fills in the log page from ATA output */
      /* SPC-4, 7.2.5, Table 209, 211, p 255 */
      LogPage[0] = 0x2F;    /* page code unspecified */
      LogPage[1] = 0;       /* reserved */
      LogPage[2] = 0;       /* page length */
      LogPage[3] = 0x07;    /* page length */

      /*
        SPC-4, 7.2.5, Table 211, p 255
        no vendor specific field
       */
      LogPage[4] = 0;    /* Parameter Code */
      LogPage[5] = 0;    /* Parameter Code unspecfied but to do: */
      LogPage[6] = 0;    /* unspecified */
      LogPage[7] = 0x03; /* Parameter length, unspecified */

      /* SAT rev8, 10.2.3.1 Table 72, p 73 */
      if (statDevToHostFisData.lbaMid == 0x4F || statDevToHostFisData.lbaHigh == 0xC2)
      {
        LogPage[8] = 0;   /* Sense code */ 
        LogPage[9] = 0;   /* Sense code qualifier */ 
      }
      else if (statDevToHostFisData.lbaMid == 0xF4 || statDevToHostFisData.lbaHigh == 0x2C)
      {
        LogPage[8] = 0x5D;   /* Sense code */ 
        LogPage[9] = 0x10;   /* Sense code qualifier */ 
      }

      /* Assumption: No support for SCT */
      LogPage[10] = 0xFF; /* Most Recent Temperature Reading */

      sm_memcpy(pLogPage, LogPage, MIN(allocationLen, INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH));
      if (INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH < allocationLen)
      {
        SM_DBG6(("smsatLogSenseCB: 3rd underrun allocationLen %d len %d \n", allocationLen, INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH));

        /* underrun */
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                           smIOUnderRun,
                           allocationLen - INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH,
                           agNULL,
                           satOrgIOContext->interruptContext );

      }
      else
      {
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest,
                           smIOSuccess,
                           SCSI_STAT_GOOD,
                           agNULL,
                           satOrgIOContext->interruptContext);
      }

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);


      return;
    }
    else
    {
      SM_DBG1(("smsatLogSenseCB: error unknown command success 0x%x feature 0x%x!!!\n", hostToDevFis->h.command, hostToDevFis->h.features));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);

      return;
    }
  }
  else
  {
    SM_DBG1(("smsatLogSenseCB: error unknown command success 0x%x!!!\n", hostToDevFis->h.command));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    return;
  }

  return;
}

osGLOBAL void
smsatSMARTEnableCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                  )
{
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatIOContext_t         *satNewIOContext;
  smSatInternalIo_t        *satIntIo;
  smSatInternalIo_t        *satNewIntIo = agNULL;
//  satDeviceData_t           *satDevData;
  smDeviceData_t           *oneDeviceData;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;
  bit32                     status;

  SM_DBG2(("smsatSMARTEnableCB: start\n"));
  SM_DBG4(("smsatSMARTEnableCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  /*ttttttthe one */
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatSMARTEnableCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatSMARTEnableCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatSMARTEnableCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatSMARTEnableCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    scsiCmnd               = satOrgIOContext->pScsiCmnd;
  }
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatSMARTEnableCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                      smRoot,
                      smOrgIORequest,
                      smIOFailed,
                      smDetailOtherError,
                      agNULL,
                      satOrgIOContext->interruptContext
                     );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  /*
    checking IO status, FIS type and error status
  */
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatSMARTEnableCB: not success status, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  /* process success case */
  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);
  satNewIntIo = smsatAllocIntIoResource( smRoot,
                                         smOrgIORequest,
                                         oneDeviceData,
                                         512,
                                         satNewIntIo);
  if (satNewIntIo == agNULL)
  {
    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );
    return;
  }
  satNewIOContext = smsatPrepareNewIO(
                                      satNewIntIo,
                                      smOrgIORequest,
                                      oneDeviceData,
                                      scsiCmnd,
                                      satOrgIOContext
                                      );
  status = smsatLogSense_1(smRoot,
                           &satNewIntIo->satIntSmIORequest,
                           satNewIOContext->psmDeviceHandle,
                           &satNewIntIo->satIntSmScsiXchg,
                           satNewIOContext);
  if (status != SM_RC_SUCCESS)
  {
    /* sending SAT_CHECK_POWER_MODE fails */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satNewIntIo);
    tdsmIOCompletedCB(
                      smRoot,
                      smOrgIORequest,
                      smIOFailed,
                      smDetailOtherError,
                      agNULL,
                      satOrgIOContext->interruptContext
                     );
    return;
  }
  return;
}

osGLOBAL void
smsatModeSelect6n10CB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                     )
{
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatIOContext_t         *satNewIOContext;
  smSatInternalIo_t        *satIntIo;
  smSatInternalIo_t        *satNewIntIo = agNULL;
//  satDeviceData_t         *satDevData;
  smDeviceData_t           *oneDeviceData;

  smScsiRspSense_t         *pSense;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;

  agsaFisRegHostToDevice_t *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  smScsiInitiatorRequest_t *smScsiRequest; /* smScsiXchg */
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  SM_DBG2(("smsatModeSelect6n10CB: start\n"));
  SM_DBG5(("smsatModeSelect6n10CB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatModeSelect6n10CB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    smScsiRequest   = satOrgIOContext->smScsiXchg;
    pSense          = satOrgIOContext->pSense;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatModeSelect6n10CB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatModeSelect6n10CB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatModeSelect6n10CB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;

    smScsiRequest = satOrgIOContext->smScsiXchg;
    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatModeSelect6n10CB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
  if ( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
       ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
       )
  {
    /* for debugging */
    if( agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatModeSelect6n10CB: FAILED, NOT IO_SUCCESS!!!\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      SM_DBG1(("smsatModeSelect6n10CB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      SM_DBG1(("smsatModeSelect6n10CB: FAILED, FAILED, error status!!!\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }

    /* for debugging */
    if (hostToDevFis->h.command == SAT_SET_FEATURES)
    {
      if ((hostToDevFis->h.features == 0x82) || (hostToDevFis->h.features == 0x02))
      {
        SM_DBG1(("smsatModeSelect6n10CB: 1 SAT_SET_FEATURES failed, feature 0x%x!!!\n", hostToDevFis->h.features));
      }
      else if ((hostToDevFis->h.features == 0xAA) || (hostToDevFis->h.features == 0x55))
      {
        SM_DBG1(("smsatModeSelect6n10CB: 2 SAT_SET_FEATURES failed, feature 0x%x!!!\n", hostToDevFis->h.features));
      }
      else
      {
        SM_DBG1(("smsatModeSelect6n10CB: error unknown command 0x%x feature 0x%x!!!\n", hostToDevFis->h.command, hostToDevFis->h.features));
      }
    }
    else if (hostToDevFis->h.command == SAT_SMART)
    {
      if ((hostToDevFis->h.features == SAT_SMART_ENABLE_OPERATIONS) || (hostToDevFis->h.features == SAT_SMART_DISABLE_OPERATIONS))
      {
        SM_DBG1(("smsatModeSelect6n10CB: SAT_SMART_ENABLE/DISABLE_OPERATIONS failed, feature 0x%x!!!\n", hostToDevFis->h.features));
      }
      else
      {
        SM_DBG1(("smsatModeSelect6n10CB: error unknown command 0x%x feature 0x%x!!!\n", hostToDevFis->h.command, hostToDevFis->h.features));
      }
    }
    else
    {
      SM_DBG1(("smsatModeSelect6n10CB: error default case command 0x%x!!!\n", hostToDevFis->h.command));
    }


    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );


    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  } /* error checking */
  }


  /* prcessing the success case */


  if (hostToDevFis->h.command == SAT_SET_FEATURES)
  {
    if ((hostToDevFis->h.features == 0x82) || (hostToDevFis->h.features == 0x02))
    {
      SM_DBG5(("smsatModeSelect6n10CB: 1 SAT_SET_FEATURES success, feature 0x%x\n", hostToDevFis->h.features));
      if (hostToDevFis->h.features == 0x02)
      {
        /* enable write cache */
        oneDeviceData->satWriteCacheEnabled = agTRUE;
      }
      else
      {
        /* disable write cache */
        oneDeviceData->satWriteCacheEnabled = agFALSE;
      }

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);

      satNewIntIo = smsatAllocIntIoResource( smRoot,
                                             smOrgIORequest,
                                             oneDeviceData,
                                             0,
                                             satNewIntIo);
      if (satNewIntIo == agNULL)
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_NO_SENSE,
                              0,
                              SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );
        SM_DBG1(("smsatModeSelect6n10CB: momory allocation fails!!!\n"));
        return;
      } /* end memory allocation */

      satNewIOContext = smsatPrepareNewIO(
                                          satNewIntIo,
                                          smOrgIORequest,
                                          oneDeviceData,
                                          scsiCmnd,
                                          satOrgIOContext
                                         );
      /* sends either ATA SET FEATURES based on DRA bit */
      status = smsatModeSelect6n10_1( smRoot,
                                      &satNewIntIo->satIntSmIORequest,
                                      satNewIOContext->psmDeviceHandle,
                                      smScsiRequest, /* orginal from OS layer */
                                      satNewIOContext
                                    );

      if (status != SM_RC_SUCCESS)
      {
        /* sending ATA command fails */
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satNewIntIo);
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_NO_SENSE,
                              0,
                              SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );
        SM_DBG1(("smsatModeSelect6n10CB: calling satModeSelect6_1 fails!!!\n"));
        return;
      } /* end send fails */
      return;
    }
    else if ((hostToDevFis->h.features == 0xAA) || (hostToDevFis->h.features == 0x55))
    {
      SM_DBG5(("smsatModeSelect6n10CB: 2 SAT_SET_FEATURES success, feature 0x%x\n", hostToDevFis->h.features));

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);

      /* return stat_good */
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
      return;
    }
    else
    {
      SM_DBG1(("smsatModeSelect6n10CB: error unknown command success 0x%x feature 0x%x!!!\n", hostToDevFis->h.command, hostToDevFis->h.features));

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );
      return;
    }
  }
  else if (hostToDevFis->h.command == SAT_SMART )
  {
    if ((hostToDevFis->h.features == SAT_SMART_ENABLE_OPERATIONS) || (hostToDevFis->h.features == SAT_SMART_DISABLE_OPERATIONS))
    {
      SM_DBG5(("smsatModeSelect6n10CB: SAT_SMART_ENABLE/DISABLE_OPERATIONS success, feature 0x%x\n", hostToDevFis->h.features));

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      /* return stat_good */
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
      return;
    }
    else
    {
      SM_DBG1(("smsatModeSelect6n10CB: error unknown command failed 0x%x feature 0x%x!!!\n", hostToDevFis->h.command, hostToDevFis->h.features));

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );
      return;
    }
  }

  else
  {
    SM_DBG1(("smsatModeSelect6n10CB: error default case command success 0x%x!!!\n", hostToDevFis->h.command));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );
    return;
  }

  return;

}

osGLOBAL void
smsatSynchronizeCache10n16CB(
                             agsaRoot_t        *agRoot,
                             agsaIORequest_t   *agIORequest,
                             bit32             agIOStatus,
                             agsaFisHeader_t   *agFirstDword,
                             bit32             agIOInfoLen,
                             agsaFrameHandle_t agFrameHandle,
                             void              *ioContext
                            )
{
  /*
    In the process of SynchronizeCache10 and SynchronizeCache16
    Process SAT_FLUSH_CACHE_EXT
    Process SAT_FLUSH_CACHE
  */


  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatInternalIo_t        *satIntIo;
  smDeviceData_t           *oneDeviceData;

  smScsiRspSense_t         *pSense;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;

  agsaFisRegHostToDevice_t *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  SM_DBG2(("smsatSynchronizeCache10n16CB: start\n"));
  SM_DBG5(("smsatSynchronizeCache10n16CB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  /* SPC: Self-Test Result Log page */
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatSynchronizeCache10n16CB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatSynchronizeCache10n16CB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatSynchronizeCache10n16CB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatSynchronizeCache10n16CB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatSynchronizeCache10n16CB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));

    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
  if ( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
       ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
       )
  {
    /* for debugging */
    if( agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatSynchronizeCache10n16CB: FAILED, NOT IO_SUCCESS!!!\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      SM_DBG1(("smsatSynchronizeCache10n16CB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      SM_DBG1(("smsatSynchronizeCache10n16CB: FAILED, FAILED, error status!!!\n"));
    }


    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }

    switch (hostToDevFis->h.command)
    {
    case SAT_FLUSH_CACHE:
      SM_DBG1(("smsatSynchronizeCache10n16CB: SAT_FLUSH_CACHE failed!!!\n"));
      /* checking IMMED bit */
      if (scsiCmnd->cdb[1] & SCSI_FLUSH_CACHE_IMMED_MASK)
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_NO_SENSE,
                                      0,
                                      SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                                      satOrgIOContext);
      }
      else
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_NO_SENSE,
                                      0,
                                      SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                                      satOrgIOContext);
      }


      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
      break;
    case SAT_FLUSH_CACHE_EXT:
      SM_DBG1(("smsatSynchronizeCache10n16CB: SAT_FLUSH_CACHE_EXT failed!!!\n"));
       /* checking IMMED bit */
      if (scsiCmnd->cdb[1] & SCSI_FLUSH_CACHE_IMMED_MASK)
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_NO_SENSE,
                                      0,
                                      SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                                      satOrgIOContext);
      }
      else
      {
        smsatSetDeferredSensePayload( pSense,
                                      SCSI_SNSKEY_NO_SENSE,
                                      0,
                                      SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                                      satOrgIOContext);
      }


      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
      break;
    default:
      SM_DBG1(("smsatSynchronizeCache10n16CB: error unknown command 0x%x!!!\n", hostToDevFis->h.command));
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);


      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
      break;
    }

    return;
  } /* end of error checking */
  }

  /* prcessing the success case */
  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);


  switch (hostToDevFis->h.command)
  {
  case SAT_FLUSH_CACHE:
    SM_DBG5(("smsatSynchronizeCache10n16CB: SAT_FLUSH_CACHE success\n"));

    /* checking IMMED bit */
    if ( !(scsiCmnd->cdb[1] & SCSI_FLUSH_CACHE_IMMED_MASK))
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
      return;
    }


    break;
  case SAT_FLUSH_CACHE_EXT:
    SM_DBG5(("smsatSynchronizeCache10n16CB: SAT_FLUSH_CACHE_EXT success\n"));

    /* checking IMMED bit */
    if ( !(scsiCmnd->cdb[1] & SCSI_FLUSH_CACHE_IMMED_MASK))
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
      return;
    }

    break;
  default:
    SM_DBG5(("smsatSynchronizeCache10n16CB: error unknown command 0x%x\n", hostToDevFis->h.command));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);


    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    return;
    break;
  }

  return;
}

//qqqqqqqq
osGLOBAL void
smsatNonChainedWriteNVerifyCB(
                              agsaRoot_t        *agRoot,
                              agsaIORequest_t   *agIORequest,
                              bit32             agIOStatus,
                              agsaFisHeader_t   *agFirstDword,
                              bit32             agIOInfoLen,
                              void              *agParam,
                              void              *ioContext
                             )
{
  /*
    In the process of WriteAndVerify10
    Process SAT_WRITE_DMA_FUA_EXT
    Process SAT_WRITE_DMA_EXT
    Process SAT_WRITE_SECTORS_EXT
    Process SAT_WRITE_FPDMA_QUEUED
    Process SAT_READ_VERIFY_SECTORS
    Process SAT_READ_VERIFY_SECTORS_EXT
    chained command
  */


  smRoot_t                *smRoot = agNULL;
  smIntRoot_t             *smIntRoot = agNULL;
  smIntContext_t          *smAllShared = agNULL;
  smIORequestBody_t       *smIORequestBody;
  smIORequestBody_t       *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatIOContext_t          *satNewIOContext;
  smSatInternalIo_t         *satIntIo;
  smSatInternalIo_t         *satNewIntIo = agNULL;
//  satDeviceData_t         *satDevData;
  smDeviceData_t          *oneDeviceData;

  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
  smIORequest_t             *smOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  smScsiInitiatorRequest_t  *smScsiRequest; /* smScsiXchg */
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  agsaFisSetDevBitsHeader_t *statSetDevBitFisHeader = agNULL;

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  /* SPC: Self-Test Result Log page */
  smScsiRequest          = satIOContext->smScsiXchg;

  SM_DBG2(("smsatNonChainedWriteNVerifyCB: start\n"));
  SM_DBG5(("smsatNonChainedWriteNVerifyCB: start agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));


  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatNonChainedWriteNVerifyCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatNonChainedWriteNVerifyCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatNonChainedWriteNVerifyCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatNonChainedWriteNVerifyCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;


  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatNonChainedWriteNVerifyCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                       );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }


  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /*
      FIS type should be either REG_DEV_TO_HOST_FIS or SET_DEV_BITS_FIS
    */
    /* First, assumed to be Reg Device to Host FIS */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    if (statDevToHostFisHeader->fisType == SET_DEV_BITS_FIS)
    {
      statSetDevBitFisHeader = (agsaFisSetDevBitsHeader_t *)&(agFirstDword->D2H);

      /* Get ATA Status register */
      ataStatus = (statSetDevBitFisHeader->statusHi_Lo & 0x70);               /* bits 4,5,6 */
      ataStatus = ataStatus | (statSetDevBitFisHeader->statusHi_Lo & 0x07);   /* bits 0,1,2 */
   }
  }


  if( agIOStatus != OSSA_IO_SUCCESS)
  {
  /*
    checking IO status, FIS type and error status
    FIS type should be either REG_DEV_TO_HOST_FIS or SET_DEV_BITS_FIS
    Both have fisType in the same location
  */
  if ( ((statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) &&
        (statDevToHostFisHeader->fisType != SET_DEV_BITS_FIS)) ||
       ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
       )
    {
      /* for debugging */
      if( agIOStatus != OSSA_IO_SUCCESS)
      {
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: FAILED, NOT IO_SUCCESS!!!\n"));
      }
      else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
      {
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
      }
      else if (statDevToHostFisHeader->fisType != SET_DEV_BITS_FIS)
      {
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
      }
      else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
                (ataStatus & DF_ATA_STATUS_MASK)
                )
      {
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: FAILED, FAILED, error status!!!\n"));
      }


      /* Process abort case */
      if (agIOStatus == OSSA_IO_ABORTED)
      {
        smsatProcessAbort(smRoot,
                          smOrgIORequest,
                          satOrgIOContext
                          );

        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
        return;
      }

      /* for debugging */
      switch (hostToDevFis->h.command)
      {
      case SAT_WRITE_DMA_FUA_EXT:
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: SAT_WRITE_DMA_FUA_EXT!!!\n"));
        break;
      case SAT_WRITE_DMA_EXT:
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: SAT_WRITE_DMA_EXT!!!\n"));
        break;
      case SAT_WRITE_SECTORS_EXT:
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: SAT_WRITE_SECTORS_EXT!!!\n"));
        break;
      case SAT_WRITE_FPDMA_QUEUED:
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: SAT_WRITE_FPDMA_QUEUED!!!\n"));
        break;
      case SAT_READ_VERIFY_SECTORS:
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: SAT_READ_VERIFY_SECTORS!!!\n"));
        break;
      case SAT_READ_VERIFY_SECTORS_EXT:
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: SAT_READ_VERIFY_SECTORS_EXT!!!\n"));
        break;
      default:
        SM_DBG1(("smsatNonChainedWriteNVerifyCB: error default case command 0x%x!!!\n", hostToDevFis->h.command));
        break;
      }

      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    } /* end error checking */
  }

  /* process success from this point on */

  switch (hostToDevFis->h.command)
  {
  case SAT_WRITE_DMA_FUA_EXT:
    SM_DBG5(("smsatNonChainedWriteNVerifyCB: SAT_WRITE_DMA_FUA_EXT success\n"));
    break;
  case SAT_WRITE_DMA_EXT:
    SM_DBG5(("smsatNonChainedWriteNVerifyCB: SAT_WRITE_DMA_EXT success\n"));
    break;
  case SAT_WRITE_SECTORS_EXT:
    SM_DBG5(("smsatNonChainedWriteNVerifyCB: SAT_WRITE_SECTORS_EXT succes\n"));

    break;
  case SAT_WRITE_FPDMA_QUEUED:
    SM_DBG5(("smsatNonChainedWriteNVerifyCB: SAT_WRITE_FPDMA_QUEUED succes\n"));
    break;
  case SAT_READ_VERIFY_SECTORS:
    SM_DBG5(("smsatNonChainedWriteNVerifyCB: SAT_READ_VERIFY_SECTORS succes\n"));
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* free */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    /* return stat_good */
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext );
    return;
    break;
  case SAT_READ_VERIFY_SECTORS_EXT:
    SM_DBG5(("smsatNonChainedWriteNVerifyCB: SAT_READ_VERIFY_SECTORS_EXT succes\n"));
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* free */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    /* return stat_good */
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext );
    return;
    break;
  default:
    SM_DBG1(("smsatNonChainedWriteNVerifyCB:  error default case command 0x%x success!!!\n", hostToDevFis->h.command));

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
    break;
  }

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  /* free */
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);

  satNewIntIo = smsatAllocIntIoResource( smRoot,
                                         smOrgIORequest,
                                         oneDeviceData,
                                         0,
                                         satNewIntIo);
  if (satNewIntIo == agNULL)
  {
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );
    SM_DBG1(("smsatNonChainedWriteNVerifyCB: momory allocation fails!!!\n"));
    return;
  } /* end memory allocation */

  satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                      smOrgIORequest,
                                      oneDeviceData,
                                      scsiCmnd,
                                      satOrgIOContext
                                     );

  /* sends ATA verify command(READ_VERIFY_SECTORS or READ_VERIFY_SECTORS_EXT) */
  status = smsatNonChainedWriteNVerify_Verify(smRoot,
                                              &satNewIntIo->satIntSmIORequest,
                                              satNewIOContext->psmDeviceHandle,
                                              smScsiRequest, /* orginal from OS layer */
                                              satNewIOContext
                                             );


  if (status != SM_RC_SUCCESS)
  {
    /* sending ATA command fails */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satNewIntIo);
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );
    SM_DBG1(("smsatNonChainedWriteNVerifyCB: calling satWriteAndVerify10_1 fails!!!\n"));
    return;
  } /* end send fails */

  return;

}

osGLOBAL void
smsatChainedWriteNVerifyCB(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           void              *agParam,
                           void              *ioContext
                          )
{
  /*
    send write in loop
    then, send verify in loop
  */

  smRoot_t                *smRoot = agNULL;
  smIntRoot_t             *smIntRoot = agNULL;
  smIntContext_t          *smAllShared = agNULL;
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatIOContext_t         *satNewIOContext;
  smSatInternalIo_t        *satIntIo;
  smSatInternalIo_t        *satNewIntIo = agNULL;
//  satDeviceData_t         *satDevData;
  smDeviceData_t           *oneDeviceData;

  smScsiRspSense_t         *pSense;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;
  agsaFisRegHostToDevice_t *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     dataLength;
  bit32                     status = tiError;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  SM_DBG2(("smsatChainedWriteNVerifyCB: start\n"));
  SM_DBG6(("smsatChainedWriteNVerifyCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
           agIORequest, agIOStatus, agIOInfoLen));

  if (satIntIo == agNULL)
  {
    SM_DBG5(("smsatChainedWriteNVerifyCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG5(("smsatChainedWriteNVerifyCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG5(("smsatChainedWriteNVerifyCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG5(("smsatChainedWriteNVerifyCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     SM_DBG1(("smsatChainedWriteNVerifyCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
     smsatSetSensePayload( pSense,
                           SCSI_SNSKEY_NO_SENSE,
                           0,
                           SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                           satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  /*
    checking IO status, FIS type and error status
  */
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* agsaFisPioSetup_t or agsaFisRegDeviceToHost_t or agsaFisSetDevBits_t for read
       agsaFisRegDeviceToHost_t or agsaFisSetDevBits_t for write
       first, assumed to be Reg Device to Host FIS
       This is OK to just find fis type
    */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
    /* for debugging */
    if( (statDevToHostFisHeader->fisType != PIO_SETUP_DEV_TO_HOST_FIS) &&
        (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) &&
        (statDevToHostFisHeader->fisType != SET_DEV_BITS_FIS)
        )
    {
      SM_DBG1(("smsatChainedWriteNVerifyCB: FAILED, Wrong FIS type 0x%x!!!\n", statDevToHostFisHeader->fisType));
    }

    /* for debugging */
    if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      SM_DBG1(("smsatChainedWriteNVerifyCB: FAILED, error status and command 0x%x!!!\n", hostToDevFis->h.command));
    }

    /* the function below handles abort case */
    smsatDelayedProcessAbnormalCompletion(agRoot,
                                          agIORequest,
                                          agIOStatus,
                                          agFirstDword,
                                          agIOInfoLen,
                                          agParam,
                                          satIOContext);

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  } /* end of error */

  /* process the success case */
  switch (hostToDevFis->h.command)
  {
  case SAT_WRITE_DMA: /* fall through */
  case SAT_WRITE_SECTORS:/* fall through */
//  case SAT_WRITE_DMA_FUA_EXT: /* fall through */
  case SAT_WRITE_DMA_EXT: /* fall through */
  case SAT_WRITE_SECTORS_EXT: /* fall through */
  case SAT_WRITE_FPDMA_QUEUED:

    SM_DBG5(("smsatChainedWriteNVerifyCB: WRITE success case\n"));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with internally genereated SAT_SMART_RETURN_STATUS */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    /* let's loop till TL */

    /* lba = lba + tl
       loopnum--;
       if (loopnum == 0) done
     */
    (satOrgIOContext->LoopNum)--;
  
    if (satOrgIOContext->superIOFlag)
    {
        dataLength = ((tiSuperScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->scsiCmnd.expDataLength;
    }
    else
    {
        dataLength = ((tiScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->scsiCmnd.expDataLength;
    }

    satNewIntIo = smsatAllocIntIoResource( smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           dataLength,
                                           satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         satOrgIOContext->interruptContext );

      SM_DBG1(("smsatChainedWriteNVerifyCB: momory allocation fails!!!\n"));
      return;
    } /* end of memory allocation failure */

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = smsatPrepareNewIO(
                                        satNewIntIo,
                                        smOrgIORequest,
                                        oneDeviceData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );

    if (satOrgIOContext->LoopNum == 0)
    {
      /*
        done with write
        start with verify
      */
      satOrgIOContext->LoopNum = satOrgIOContext->LoopNum2;
      status = smsatChainedWriteNVerify_Start_Verify(smRoot,
                                                     &satNewIntIo->satIntSmIORequest,
                                                     satNewIOContext->psmDeviceHandle,
                                                     &satNewIntIo->satIntSmScsiXchg,
                                                     satNewIOContext);
    }
    else
    {
      status = smsatChainedWriteNVerify_Write(smRoot,
                                             &satNewIntIo->satIntSmIORequest,
                                             satNewIOContext->psmDeviceHandle,
                                             &satNewIntIo->satIntSmScsiXchg,
                                             satNewIOContext);
    }

    if (status != SM_RC_SUCCESS)
    {
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satNewIntIo);
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         satOrgIOContext->interruptContext );
      SM_DBG1(("smsatChainedWriteNVerifyCB: calling satChainedWriteNVerify_Write fails!!!\n"));
      return;
    }

    break;

  case SAT_READ_VERIFY_SECTORS: /* fall through */
  case SAT_READ_VERIFY_SECTORS_EXT:
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    /* done with internally genereated SAT_SMART_RETURN_STATUS */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    /* let's loop till TL */

    /* lba = lba + tl
       loopnum--;
       if (loopnum == 0) done
     */
    (satOrgIOContext->LoopNum)--;
    if (satOrgIOContext->LoopNum == 0)
    {
      /*
        done with write and verify
      */
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
      return;
    }

    if (satOrgIOContext->superIOFlag)
    {
        dataLength = ((tiSuperScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->scsiCmnd.expDataLength;
    }
    else
    {
        dataLength = ((tiScsiInitiatorRequest_t *) satOrgIOContext->smScsiXchg)->scsiCmnd.expDataLength;
    }

    satNewIntIo = smsatAllocIntIoResource( smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           dataLength,
                                           satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         satOrgIOContext->interruptContext );

      SM_DBG1(("smsatChainedWriteNVerifyCB: momory allocation fails!!!\n"));
      return;
    } /* end of memory allocation failure */

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = smsatPrepareNewIO(
                                        satNewIntIo,
                                        smOrgIORequest,
                                        oneDeviceData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );
    status = smsatChainedWriteNVerify_Verify(smRoot,
                                             &satNewIntIo->satIntSmIORequest,
                                             satNewIOContext->psmDeviceHandle,
                                             &satNewIntIo->satIntSmScsiXchg,
                                             satNewIOContext);

    if (status != SM_RC_SUCCESS)
    {
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satNewIntIo);
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         satOrgIOContext->interruptContext );
      SM_DBG1(("smsatChainedWriteNVerifyCB: calling satChainedWriteNVerify_Verify fails!!!\n"));
      return;
    }

    break;

  default:
    SM_DBG1(("smsatChainedWriteNVerifyCB: success but default case command 0x%x!!!\n",hostToDevFis->h.command));
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    break;
  }


  return;
}

osGLOBAL void
smsatReadMediaSerialNumberCB(
                             agsaRoot_t        *agRoot,
                             agsaIORequest_t   *agIORequest,
                             bit32             agIOStatus,
                             agsaFisHeader_t   *agFirstDword,
                             bit32             agIOInfoLen,
                             agsaFrameHandle_t agFrameHandle,
                             void              *ioContext
                            )
{
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatInternalIo_t        *satIntIo;
//  satDeviceData_t           *satDevData;
  smDeviceData_t           *oneDeviceData;

  smScsiRspSense_t         *pSense;
  smIORequest_t            *smOrgIORequest;

  agsaFisRegHostToDevice_t *hostToDevFis = agNULL;
  smScsiInitiatorRequest_t *smOrgScsiRequest; /* tiScsiXchg */
  bit8                     *pMediaSerialNumber;
  bit8                      MediaSerialNumber[ZERO_MEDIA_SERIAL_NUMBER_LENGTH] = {0};
  smIniScsiCmnd_t          *scsiCmnd;
  bit32                     allocationLen = 0;

  SM_DBG2(("smsatReadMediaSerialNumberCB: start\n"));
  SM_DBG4(("smsatReadMediaSerialNumberCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatReadMediaSerialNumberCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
    smOrgScsiRequest          = satOrgIOContext->smScsiXchg;
    /* SCSI command response payload to OS layer */
    pMediaSerialNumber        = (bit8 *) smOrgScsiRequest->sglVirtualAddr;
    /* ATA command response payload */
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatReadMediaSerialNumberCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatReadMediaSerialNumberCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatReadMediaSerialNumberCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest     = (smIORequest_t *)smOrgIORequestBody->smIORequest;

    pSense             = satOrgIOContext->pSense;
    smOrgScsiRequest   = satOrgIOContext->smScsiXchg;
    /* SCSI command response payload to OS layer */
    pMediaSerialNumber = (bit8 *) smOrgScsiRequest->sglVirtualAddr;
    scsiCmnd           = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatReadMediaSerialNumberCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  /* process success case */
  allocationLen = (scsiCmnd->cdb[6] << (8*3)) + (scsiCmnd->cdb[7] << (8*2))
                + (scsiCmnd->cdb[8] << 8) + scsiCmnd->cdb[9];
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength);
  SM_DBG5(("smsatReadMediaSerialNumberCB: allocationLen in CDB %d 0x%x\n", allocationLen,allocationLen));

  if (hostToDevFis->h.command == SAT_READ_SECTORS ||
      hostToDevFis->h.command == SAT_READ_SECTORS_EXT
     )
  {
    MediaSerialNumber[0] = 0;
    MediaSerialNumber[1] = 0;
    MediaSerialNumber[2] = 0;
    MediaSerialNumber[3] = 4;
    MediaSerialNumber[4] = 0;
    MediaSerialNumber[5] = 0;
    MediaSerialNumber[6] = 0;
    MediaSerialNumber[7] = 0;

    sm_memcpy(pMediaSerialNumber, MediaSerialNumber, MIN(allocationLen, ZERO_MEDIA_SERIAL_NUMBER_LENGTH));
    if (ZERO_MEDIA_SERIAL_NUMBER_LENGTH < allocationLen)
    {
      SM_DBG1(("smsatReadMediaSerialNumberCB: 1st underrun allocationLen %d len %d !!!\n", allocationLen, ZERO_MEDIA_SERIAL_NUMBER_LENGTH));

      /* underrun */
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == satIntIo->satOrgSmIORequest */
                         smIOUnderRun,
                         allocationLen - ZERO_MEDIA_SERIAL_NUMBER_LENGTH,
                         agNULL,
                         satOrgIOContext->interruptContext );

    }
    else
    {
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext);
    }
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  else
  {
    SM_DBG1(("smsatReadMediaSerialNumberCB: error unknown command success 0x%x!!!\n", hostToDevFis->h.command));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    return;
  }
  return;
}

osGLOBAL void
smsatReadBufferCB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  agsaFrameHandle_t agFrameHandle,
                  void              *ioContext
                 )
{
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatInternalIo_t        *satIntIo;
  smDeviceData_t           *oneDeviceData;
  smScsiRspSense_t         *pSense;
  smIORequest_t            *smOrgIORequest;
  agsaFisRegHostToDevice_t *hostToDevFis = agNULL;


  SM_DBG2(("smsatReadBufferCB: start\n"));
  SM_DBG4(("smsatReadBufferCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));
  /* internally generate tiIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatReadBufferCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
  }
  else
  {
    SM_DBG4(("smsatReadBufferCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatReadBufferCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatReadBufferCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense        = satOrgIOContext->pSense;
  }
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatReadBufferCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  /* process success case */
  if (hostToDevFis->h.command == SAT_READ_BUFFER )
  {

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext);
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  else
  {
    SM_DBG1(("smsatReadBufferCB: error unknown command success 0x%x!!!\n", hostToDevFis->h.command));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    return;
  }

  return;
}

osGLOBAL void
smsatWriteBufferCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                  )
{
  smRoot_t                *smRoot = agNULL;
  smIntRoot_t             *smIntRoot = agNULL;
  smIntContext_t          *smAllShared = agNULL;
  smIORequestBody_t       *smIORequestBody;
  smIORequestBody_t       *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatInternalIo_t         *satIntIo;
  smDeviceData_t          *oneDeviceData;
  smScsiRspSense_t          *pSense;
  smIORequest_t             *smOrgIORequest;
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;


  SM_DBG2(("smsatWriteBufferCB: start\n"));
  SM_DBG4(("smsatWriteBufferCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));
  /* internally generate tiIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatWriteBufferCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
    /* SCSI command response payload to OS layer */
//    pMediaSerialNumber        = (bit8 *) s,OrgScsiRequest->sglVirtualAddr;
  }
  else
  {
    SM_DBG4(("smsatWriteBufferCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatWriteBufferCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatWriteBufferCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense        = satOrgIOContext->pSense;
  }
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatWriteBufferCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                          satOrgIOContext);
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  /* process success case */
  if (hostToDevFis->h.command == SAT_WRITE_BUFFER )
  {
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext);
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  else
  {
    SM_DBG1(("smsatWriteBufferCB: error unknown command success 0x%x!!!\n", hostToDevFis->h.command));
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  return;
}

osGLOBAL void
smsatReassignBlocksCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                     )
{
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatIOContext_t         *satNewIOContext;
  smSatInternalIo_t        *satIntIo;
  smSatInternalIo_t        *satNewIntIo = agNULL;
  smDeviceData_t           *oneDeviceData;
  smScsiRspSense_t         *pSense;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;
  agsaFisRegHostToDevice_t *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  smScsiInitiatorRequest_t *smScsiRequest; /* smScsiXchg */
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  SM_DBG2(("smsatReassignBlocksCB: start\n"));
  SM_DBG5(("smsatReassignBlocksCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatReassignBlocksCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    smScsiRequest   = satOrgIOContext->smScsiXchg;
    pSense          = satOrgIOContext->pSense;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    SM_DBG4(("smsatReassignBlocksCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatReassignBlocksCB: satOrgIOContext is NULL, Wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatReassignBlocksCB: satOrgIOContext is NOT NULL, Wrong\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    smScsiRequest = satOrgIOContext->smScsiXchg;
    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatReassignBlocksCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                       smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
  if ( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
       ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
       )
  {
    /* for debugging */
    if( agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatReassignBlocksCB FAILED, NOT IO_SUCCESS!!!\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      SM_DBG1(("smsatReassignBlocksCB FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      SM_DBG1(("smsatReassignBlocksCB FAILED, FAILED, error status!!!\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }
    /* for debugging */
    if (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS ||
        hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT
       )
    {
      SM_DBG1(("smsatReassignBlocksCB SAT_READ_VERIFY_SECTORS(_EXT) failed!!!\n"));
      /* Verify failed; send Write with same LBA */
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);

      satNewIntIo = smsatAllocIntIoResource( smRoot,
                                             smOrgIORequest,
                                             oneDeviceData,
                                             512, /* writing 1 sector */
                                             satNewIntIo);
      if (satNewIntIo == agNULL)
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_HARDWARE_ERROR,
                              0,
                              SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                              satOrgIOContext);
        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );
        SM_DBG1(("smsatReassignBlocksCB: momory allocation fails!!!\n"));
        return;
      } /* end memory allocation */
      satNewIOContext = smsatPrepareNewIO(
                                          satNewIntIo,
                                          smOrgIORequest,
                                          oneDeviceData,
                                          scsiCmnd,
                                          satOrgIOContext
                                          );
      /* send Write with same LBA */
      status = smsatReassignBlocks_2(
                                     smRoot,
                                     &satNewIntIo->satIntSmIORequest,
                                     satNewIOContext->psmDeviceHandle,
                                     &satNewIntIo->satIntSmScsiXchg,
                                     satNewIOContext,
                                     satOrgIOContext->LBA
                                    );

      if (status != SM_RC_SUCCESS)
      {
        /* sending ATA command fails */
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satNewIntIo);
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_HARDWARE_ERROR,
                              0,
                              SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );
        SM_DBG1(("smsatReassignBlocksCB calling fail 1!!!\n"));
        return;
      } /* end send fails */

      return;
    }
    else if (hostToDevFis->h.command == SAT_WRITE_DMA ||
             hostToDevFis->h.command == SAT_WRITE_SECTORS ||
             hostToDevFis->h.command == SAT_WRITE_DMA_EXT ||
             hostToDevFis->h.command == SAT_WRITE_SECTORS_EXT ||
             hostToDevFis->h.command == SAT_WRITE_FPDMA_QUEUED
             )
    {
      SM_DBG1(("smsatReassignBlocksCB SAT_WRITE failed!!!\n"));
      /* fall through */
    }
    else
    {
      SM_DBG1(("smsatReassignBlocksCB error default case unexpected command 0x%x!!!\n", hostToDevFis->h.command));
    }


    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_HARDWARE_ERROR,
                          0,
                          SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );


    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  } /* error checking */
  }


  /* prcessing the success case */
  if (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS ||
      hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT ||
      hostToDevFis->h.command == SAT_WRITE_DMA ||
      hostToDevFis->h.command == SAT_WRITE_SECTORS ||
      hostToDevFis->h.command == SAT_WRITE_DMA_EXT ||
      hostToDevFis->h.command == SAT_WRITE_SECTORS_EXT ||
      hostToDevFis->h.command == SAT_WRITE_FPDMA_QUEUED
      )
  {
    /* next LBA; verify */
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    if (satOrgIOContext->ParmIndex >= satOrgIOContext->ParmLen)
    {
      SM_DBG5(("smsatReassignBlocksCB: GOOD status\n"));
      /* return stat_good */
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_GOOD,
                         agNULL,
                         satOrgIOContext->interruptContext );
      return;
    }
    else
    {
      SM_DBG5(("smsatReassignBlocksCB: processing next LBA\n"));
      satNewIntIo = smsatAllocIntIoResource( smRoot,
                                             smOrgIORequest,
                                             oneDeviceData,
                                             0,
                                             satNewIntIo);
      if (satNewIntIo == agNULL)
      {
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_HARDWARE_ERROR,
                              0,
                              SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );
        SM_DBG1(("smsatReassignBlocksCB: momory allocation fails!!!\n"));
        return;
      } /* end memory allocation */

      satNewIOContext = smsatPrepareNewIO(
                                          satNewIntIo,
                                          smOrgIORequest,
                                          oneDeviceData,
                                          scsiCmnd,
                                          satOrgIOContext
                                          );

      /* send Verify with the next LBA */
      status = smsatReassignBlocks_1(
                                     smRoot,
                                     &satNewIntIo->satIntSmIORequest,
                                     satNewIOContext->psmDeviceHandle,
                                     smScsiRequest, /* orginal from OS layer */
                                     satNewIOContext,
                                     satOrgIOContext
                                     );

      if (status != SM_RC_SUCCESS)
      {
        /* sending ATA command fails */
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satNewIntIo);
        smsatSetSensePayload( pSense,
                              SCSI_SNSKEY_HARDWARE_ERROR,
                              0,
                              SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                              satOrgIOContext);

        tdsmIOCompletedCB( smRoot,
                           smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                           smIOSuccess,
                           SCSI_STAT_CHECK_CONDITION,
                           satOrgIOContext->pSmSenseData,
                           satOrgIOContext->interruptContext );
        SM_DBG1(("smsatReassignBlocksCB calling satModeSelect6_1 fails!!!\n"));
        return;
      } /* end send fails */
    } /* else */
    return;

  }
  else if (hostToDevFis->h.command == SAT_WRITE_DMA ||
           hostToDevFis->h.command == SAT_WRITE_SECTORS ||
           hostToDevFis->h.command == SAT_WRITE_DMA_EXT ||
           hostToDevFis->h.command == SAT_WRITE_SECTORS_EXT ||
           hostToDevFis->h.command == SAT_WRITE_FPDMA_QUEUED
           )
  {
    /* next LBA; verify */
  }
  else
  {
      SM_DBG1(("smsatReassignBlocksCB error unknown command success 0x%x !!!\n", hostToDevFis->h.command));

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );
      return;
  }
  return;
}


osGLOBAL FORCEINLINE void
smsatDecrementPendingIO(
                        smRoot_t                *smRoot,
                        smIntContext_t          *smAllShared,
                        smSatIOContext_t        *satIOContext
                        )
{
#ifdef CCFLAG_OPTIMIZE_SAT_LOCK
  bit32 volatile satPendingNCQIO = 0;
  bit32 volatile satPendingNONNCQIO = 0;
  bit32 volatile satPendingIO = 0;
#endif /* CCFLAG_OPTIMIZE_SAT_LOCK */
  smDeviceData_t       *oneDeviceData   = satIOContext->pSatDevData;
  smSatInternalIo_t    *satIntIo        = satIOContext->satIntIoContext;
  smSatIOContext_t     *satOrgIOContext = satIOContext->satOrgIOContext;
#ifdef  TD_DEBUG_ENABLE
  smIORequestBody_t    *smIORequestBody = agNULL;
  smIORequestBody = (smIORequestBody_t *)satIOContext->smRequestBody;
#endif

  SM_DBG3(("smsatDecrementPendingIO: start\n"));

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
  /* temp */
  tdsmInterlockedExchange(smRoot, &satPendingNCQIO, oneDeviceData->satPendingNCQIO);
  tdsmInterlockedExchange(smRoot, &satPendingNONNCQIO, oneDeviceData->satPendingNONNCQIO);
  tdsmInterlockedExchange(smRoot, &satPendingIO, oneDeviceData->satPendingIO);
  if (satPendingNCQIO == -1)
  {
    SM_DBG1(("smsatDecrementPendingIO: satPendingNCQIO adjustment!!!\n"));
    oneDeviceData->satPendingNCQIO = 0;
  }
  if (satPendingNONNCQIO == -1)
  {
    SM_DBG1(("smsatDecrementPendingIO: satPendingNONNCQIO adjustment!!!\n"));
    oneDeviceData->satPendingNONNCQIO = 0;
  }
  if (satPendingIO == -1)
  {
    SM_DBG1(("smsatDecrementPendingIO: satPendingIO adjustment!!!\n"));
    oneDeviceData->satPendingIO = 0;
  }

#else

  if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
       (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
  {
    tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
    oneDeviceData->satPendingNCQIO--;
    oneDeviceData->satPendingIO--;
    SMLIST_DEQUEUE_THIS (&satIOContext->satIoContextLink);
    /* temp */
    if (oneDeviceData->satPendingNCQIO == -1)
    {
      SM_DBG1(("smsatDecrementPendingIO: satPendingNCQIO adjustment!!!\n"));
      oneDeviceData->satPendingNCQIO = 0;
    }
    if (oneDeviceData->satPendingIO == -1)
    {
      SM_DBG1(("smsatDecrementPendingIO: satPendingIO adjustment!!!\n"));
      oneDeviceData->satPendingIO = 0;
    }
    tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
  }
  else
  {
    tdsmSingleThreadedEnter(smRoot, SM_EXTERNAL_IO_LOCK);
    oneDeviceData->satPendingNONNCQIO--;
    oneDeviceData->satPendingIO--;
    SMLIST_DEQUEUE_THIS (&satIOContext->satIoContextLink);
    /* temp */
    if (oneDeviceData->satPendingNONNCQIO == -1)
    {
      SM_DBG1(("smsatDecrementPendingIO: satPendingNONNCQIO adjustment!!!\n"));
      oneDeviceData->satPendingNONNCQIO = 0;
    }
    if (oneDeviceData->satPendingIO == -1)
    {
      SM_DBG1(("smsatDecrementPendingIO: satPendingIO adjustment!!!\n"));
      oneDeviceData->satPendingIO = 0;
    }
    tdsmSingleThreadedLeave(smRoot, SM_EXTERNAL_IO_LOCK);
  }

#endif /* CCFLAG_OPTIMIZE_SAT_LOCK */

  if (satIntIo == agNULL)
  {
    SM_DBG3(("smsatDecrementPendingIO: external command!!!\n"));
    /*smEnqueueIO(smRoot, satIOContext);*/
  }
  else
  {
    SM_DBG3(("smsatDecrementPendingIO: internal command!!!\n"));
    if (satOrgIOContext == agNULL)
    {
      /* No smEnqueueIO since only alloc used */
      SM_DBG3(("smsatDecrementPendingIO: internal only command!!!, ID %d!!!\n", smIORequestBody->id));
      return;
    }
    else
    {
      /* smDequeueIO used */
      /*smEnqueueIO(smRoot, satOrgIOContext);*/
    }
  }

  return;
}


osGLOBAL void
smsatProcessAbnormalCompletion(
                               agsaRoot_t        *agRoot,
                               agsaIORequest_t   *agIORequest,
                               bit32             agIOStatus,
                               agsaFisHeader_t   *agFirstDword,
                               bit32             agIOInfoLen,
                               void              *agParam,
                               smSatIOContext_t    *satIOContext
                              )
{

  smRoot_t             *smRoot = agNULL;
//  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                interruptContext;
  smIORequestBody_t    *smIORequestBody;
//  satDeviceData_t      *pSatDevData;
  smDeviceHandle_t     *smDeviceHandle;
  smDeviceData_t       *oneDeviceData = agNULL;
  agsaDevHandle_t      *agDevHandle = agNULL;

  smIORequestBody = (smIORequestBody_t *)agIORequest->osData;
  oneDeviceData   = satIOContext->pSatDevData;
  if (oneDeviceData == agNULL)
  {
    SM_DBG1(("smsatProcessAbnormalCompletion: oneDeviceData is NULL\n"));
    return;
  }
  smDeviceHandle  = satIOContext->psmDeviceHandle;
  smRoot          = oneDeviceData->smRoot;
  interruptContext = satIOContext->interruptContext;

  SM_DBG5(("smsatProcessAbnormalCompletion: start\n"));

  /* Get into the detail */
  switch(agIOStatus)
  {
  case OSSA_IO_SUCCESS:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_SUCCESS agIOInfoLen 0x%x calling smsatIOCompleted!!!\n", agIOInfoLen));
    /*
     * At this point agIOInfoLen should be non-zero and there is valid FIS
     * to read. Pass this info to the SAT layer in order to do the ATA status
     * to SCSI status translation.
     */
      smsatIOCompleted( smRoot,
                        smIORequestBody->smIORequest,
                        agFirstDword,
                        agIOInfoLen,
                        agParam,
                        satIOContext,
                        interruptContext);
    break;


  case OSSA_IO_ABORTED:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_ABORTED!!!\n"));

    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailAborted,
                       agNULL,
                       interruptContext);

#ifdef REMOVED
    if ( oneDeviceData->satTmTaskTag != agNULL )
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: TM callback!!!\n"));
      if (smDeviceHandle == agNULL)
      {
        SM_DBG1(("smsatProcessAbnormalCompletion: wrong, smDeviceHandle is NULL!!!\n"));
      }
      /* TM completed */
      tdsmEventCB( smRoot,
                   smDeviceHandle,
                   smIntrEventTypeTaskManagement,
                   smTMOK,
                   oneDeviceData->satTmTaskTag);
      /*
       * Reset flag
       */
      oneDeviceData->satTmTaskTag = agNULL;
    }
#endif

    /*
     * Check if we are in recovery mode and need to update the recovery flag
     */
    if ((oneDeviceData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
        (oneDeviceData->satPendingIO == 0 ))
    {
      oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
      SM_DBG1(("smsatProcessAbnormalCompletion: STATE NORMAL!!!\n"));
    }

    SM_DBG1(("smsatProcessAbnormalCompletion: did %d satDriveState %d!!!\n", oneDeviceData->id, oneDeviceData->satDriveState));
    SM_DBG1(("smsatProcessAbnormalCompletion: satPendingIO %d satNCQMaxIO %d!!!\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
    SM_DBG1(("smsatProcessAbnormalCompletion: satPendingNCQIO %d satPendingNONNCQIO %d!!!\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));

    break;
#ifdef REMOVED
  case OSSA_IO_OVERFLOW:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_OVERFLOW!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOOverRun,
                       agIOInfoLen,
                       agNULL,
                       interruptContext);
    break;
#endif
  case OSSA_IO_UNDERFLOW:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_UNDERFLOW!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOUnderRun,
                       agIOInfoLen,
                       agNULL,
                       interruptContext);
    break;


  case OSSA_IO_FAILED:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_FAILED!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_ABORT_RESET:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_ABORT_RESET!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailAbortReset,
                       agNULL,
                       interruptContext);
    /*
     * Check if we are in recovery mode and need to update the recovery flag
     */
    if ((oneDeviceData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
        (oneDeviceData->satPendingIO == 0 ))
    {
      oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
      SM_DBG1(("smsatProcessAbnormalCompletion: STATE NORMAL!!!\n"));
    }

    SM_DBG1(("smsatProcessAbnormalCompletion: satDriveState %d!!!\n", oneDeviceData->satDriveState));
    SM_DBG1(("smsatProcessAbnormalCompletion: satPendingIO %d satNCQMaxIO %d!!!\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
    SM_DBG1(("smsatProcessAbnormalCompletion: satPendingNCQIO %d satPendingNONNCQIO %d!!!\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));

    break;

#ifdef REMOVED
  case OSSA_IO_NOT_VALID:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_NOT_VALID!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailNotValid,
                       agNULL,
                       interruptContext);
    break;
#endif

  case OSSA_IO_NO_DEVICE:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_NO_DEVICE!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailNoLogin,
                       agNULL,
                       interruptContext);
    break;

#ifdef REMOVED /* removed from spec */
  case OSSA_IO_ILLEGAL_PARAMETER:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_ILLEGAL_PARAMETER!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_LINK_FAILURE:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_LINK_FAILURE!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_PROG_ERROR:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_PROG_ERROR!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
#endif
  case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_BREAK: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED: /* fall through */
#ifdef REMOVED /* removed from spec */
  case OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR: /* fall through */
#endif
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_OPEN_CNX_ERROR_* 0x%x!!!\n", agIOStatus));
    if (smDeviceHandle == agNULL)
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: wrong, smDeviceHandle is NULL!!!\n"));
      tdsmIOCompletedCB( smRoot,
                         smIORequestBody->smIORequest,
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         interruptContext);
      return;
    }
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: wrong, oneDeviceData is NULL!!!\n"));
    }
    else
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: did %d!!!\n", oneDeviceData->id));
    }

    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
  case OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailBusy,
                       agNULL,
                       interruptContext);
    break;
#ifdef REMOVED
  case OSSA_IO_XFER_ERROR_BREAK: /* fall throuth */
#endif

  case OSSA_IO_XFER_ERROR_PHY_NOT_READY: /* fall throuth */
  case OSSA_IO_XFER_ERROR_NAK_RECEIVED: /* fall throuth */

#ifdef REMOVED
  case OSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_PEER_ABORTED: /* fall throuth */
#endif
  case OSSA_IO_XFER_ERROR_DMA: /* fall throuth */
#ifdef REMOVED
  case OSSA_IO_XFER_ERROR_RX_FRAME: /* fall throuth */
  case OSSA_IO_XFER_ERROR_CREDIT_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_SATA: /* fall throuth */
#endif
  case OSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_ABORTED_DUE_TO_SRST: /* fall throuth */
  case OSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE: /* fall throuth */
#ifdef REMOVED
  case OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN:
  case OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE: /* fall throuth */
  case OSSA_IO_XFER_ERROR_DISRUPTED_PHY_DOWN: /* fall throuth */
  case OSSA_IO_XFER_ERROR_OFFSET_MISMATCH: /* fall throuth */
  case OSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN: /* fall throuth */
#endif
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_XFER_ERROR_* 0x%x!!!\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
#ifdef REMOVED
  case OSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_CMD_ISSUE_BREAK_BEFORE_ACK_NAK: /* fall throuth */
  case OSSA_IO_XFER_ERROR_CMD_ISSUE_PHY_DOWN_BEFORE_ACK_NAK: /* fall throuth */
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_XFER_ERROR_CMD_ISSUE_* 0x%x!!!\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
  case OSSA_IO_XFER_PIO_SETUP_ERROR:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_XFER_PIO_SETUP_ERROR!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
#endif
  case OSSA_IO_DS_IN_ERROR:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_DS_IN_ERROR!!!\n"));
    if (smDeviceHandle == agNULL)
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: wrong, smDeviceHandle is NULL!!!\n"));
      tdsmIOCompletedCB( smRoot,
                         smIORequestBody->smIORequest,
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         interruptContext);
      return;
    }
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: wrong, oneDeviceData is NULL!!!\n"));
    }
    else
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: did %d!!!\n", oneDeviceData->id));
    }
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
  case OSSA_IO_DS_NON_OPERATIONAL:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_DS_NON_OPERATIONAL!!!\n"));
    if (smDeviceHandle == agNULL)
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: wrong, smDeviceHandle is NULL!!!\n"));
      tdsmIOCompletedCB( smRoot,
                         smIORequestBody->smIORequest,
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         interruptContext);
      return;
    }
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: wrong, oneDeviceData is NULL!!!\n"));
    }
    else
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: did %d!!!\n", oneDeviceData->id));
      agDevHandle = oneDeviceData->agDevHandle;
      if (oneDeviceData->valid == agTRUE)
      {
        saSetDeviceState(agRoot, agNULL, tdsmRotateQnumber(smRoot, smDeviceHandle), agDevHandle, SA_DS_OPERATIONAL);
      }
    }

    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_PORT_IN_RESET:
  case OSSA_IO_DS_IN_RECOVERY:
    SM_DBG1(("smsatProcessAbnormalCompletion: OSSA_IO_DS_IN_RECOVERY or OSSA_IO_PORT_IN_RESET status %x\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
    SM_DBG1(("smsatProcessAbnormalCompletion: SSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_XX status %x\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                      smIORequestBody->smIORequest,
                      smIOFailed,
                      smDetailOtherError,
                      agNULL,
                      interruptContext);
    break;

  case OSSA_MPI_IO_RQE_BUSY_FULL:
  case OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE:
  case OSSA_MPI_ERR_ATAPI_DEVICE_BUSY:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = OSSA_MPI_%x!!!\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailBusy,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS: /* fall through */
#ifdef REMOVED
  case OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH:
#endif
  case OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID: /* fall through */
  case OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR: /* fall through */
  case OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS:
  case OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE:

    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = ENCRYPTION ERROR 0x%x!!!\n", agIOStatus));
    smsatEncryptionHandler(smRoot,
                           agIORequest,
                           agIOStatus,
                           agIOInfoLen,
                           agParam,
                           0,
                           interruptContext);
    break;

#ifdef REMOVED
  case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = DIF ERROR 0x%x!!!\n", agIOStatus));
    smsatDifHandler(smRoot,
                    agIORequest,
                    agIOStatus,
                    agIOInfoLen,
                    agParam,
                    0,
                    interruptContext);
    break;
#endif

  default:
    SM_DBG1(("smsatProcessAbnormalCompletion: agIOStatus = unknown 0x%x!!!\n", agIOStatus));
    if (oneDeviceData != agNULL)
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: did %d!!!\n", oneDeviceData->id));
    }
    else
    {
      SM_DBG1(("smsatProcessAbnormalCompletion: oneDeviceData is NULL!!!\n"));
    }

    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  } /* switch */

  return;
}

osGLOBAL void
smsatDelayedProcessAbnormalCompletion(
                                      agsaRoot_t        *agRoot,
                                      agsaIORequest_t   *agIORequest,
                                      bit32             agIOStatus,
                                      agsaFisHeader_t   *agFirstDword,
                                      bit32             agIOInfoLen,
                                      void              *agParam,
                                      smSatIOContext_t    *satIOContext
                                     )
{
  smRoot_t             *smRoot = agNULL;
//  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
//  bit32                interruptContext = osData->IntContext;
  bit32                interruptContext;
  smIORequestBody_t    *smIORequestBody;
//  satDeviceData_t      *pSatDevData;
  smDeviceHandle_t     *smDeviceHandle;
  smDeviceData_t       *oneDeviceData = agNULL;
  agsaDevHandle_t      *agDevHandle = agNULL;

  smIORequestBody = (smIORequestBody_t *)agIORequest->osData;
  oneDeviceData     = satIOContext->pSatDevData;
  if (oneDeviceData == agNULL)
  {
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: oneDeviceData is NULL\n"));
    return;
  }
  smDeviceHandle  = satIOContext->psmDeviceHandle;
  smRoot          = oneDeviceData->smRoot;
  interruptContext = satIOContext->interruptContext;

  SM_DBG5(("smsatDelayedProcessAbnormalCompletion: start\n"));

  /* Get into the detail */
  switch(agIOStatus)
  {
  case OSSA_IO_SUCCESS:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_SUCCESS calling smsatIOCompleted!!!\n"));
    /* do nothing */
    break;


  case OSSA_IO_ABORTED:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_ABORTED!!!\n"));

    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailAborted,
                       agNULL,
                       interruptContext);

    if ( oneDeviceData->satTmTaskTag != agNULL )
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: TM callback!!!\n"));
      if (smDeviceHandle == agNULL)
      {
        SM_DBG1(("smsatDelayedProcessAbnormalCompletion: wrong, smDeviceHandle is NULL!!!\n"));
      }
      else
      {
        /* TM completed */
        tdsmEventCB( smRoot,
                     smDeviceHandle,
                     smIntrEventTypeTaskManagement,
                     smTMOK,
                     oneDeviceData->satTmTaskTag);
        /*
         * Reset flag
         */
        oneDeviceData->satTmTaskTag = agNULL;
      }
    }

    /*
     * Check if we are in recovery mode and need to update the recovery flag
     */
    if ((oneDeviceData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
        (oneDeviceData->satPendingIO == 0 ))
    {
      oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: STATE NORMAL.!!!\n"));
    }

    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: satDriveState %d!!!\n", oneDeviceData->satDriveState));
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: satPendingIO %d satNCQMaxIO %d!!!\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: satPendingNCQIO %d satPendingNONNCQIO %d!!!\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));

    break;
#ifdef REMOVED
  case OSSA_IO_OVERFLOW:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_OVERFLOW!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOOverRun,
                       agIOInfoLen,
                       agNULL,
                       interruptContext);
    break;
#endif
  case OSSA_IO_UNDERFLOW:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_UNDERFLOW!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOUnderRun,
                       agIOInfoLen,
                       agNULL,
                       interruptContext);
    break;


  case OSSA_IO_FAILED:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_FAILED!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_ABORT_RESET:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_ABORT_RESET!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailAbortReset,
                       agNULL,
                       interruptContext);
    /*
     * Check if we are in recovery mode and need to update the recovery flag
     */
    if ((oneDeviceData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
        (oneDeviceData->satPendingIO == 0 ))
    {
      oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: STATE NORMAL.!!!\n"));
    }

    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: satDriveState %d!!!\n", oneDeviceData->satDriveState));
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: satPendingIO %d satNCQMaxIO %d!!!\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: satPendingNCQIO %d satPendingNONNCQIO %d!!!\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));

    break;

#ifdef REMOVED
  case OSSA_IO_NOT_VALID:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_NOT_VALID!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailNotValid,
                       agNULL,
                       interruptContext);
    break;
#endif

  case OSSA_IO_NO_DEVICE:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_NO_DEVICE!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailNoLogin,
                       agNULL,
                       interruptContext);
    break;

#ifdef REMOVED /* removed from spec */
  case OSSA_IO_ILLEGAL_PARAMETER:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_ILLEGAL_PARAMETER!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_LINK_FAILURE:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_LINK_FAILURE!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_PROG_ERROR:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_PROG_ERROR!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
#endif
  case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_BREAK: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED: /* fall through */
#ifdef REMOVED /* removed from spec */
  case OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR: /* fall through */
#endif
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_OPEN_CNX_ERROR_* 0x%x!!!\n", agIOStatus));
    if (smDeviceHandle == agNULL)
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: wrong, smDeviceHandle is NULL!!!\n"));
      tdsmIOCompletedCB( smRoot,
                         smIORequestBody->smIORequest,
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         interruptContext);
      return;
    }
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: wrong, oneDeviceData is NULL!!!\n"));
    }
    else
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: did %d!!!\n", oneDeviceData->id));
    }
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailBusy,
                       agNULL,
                       interruptContext);
    break;
#ifdef REMOVED
  case OSSA_IO_XFER_ERROR_BREAK: /* fall throuth */
#endif

  case OSSA_IO_XFER_ERROR_PHY_NOT_READY: /* fall throuth */
  case OSSA_IO_XFER_ERROR_NAK_RECEIVED: /* fall throuth */

#ifdef REMOVED
  case OSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_PEER_ABORTED: /* fall throuth */
#endif

  case OSSA_IO_XFER_ERROR_DMA: /* fall throuth */

#ifdef REMOVED
  case OSSA_IO_XFER_ERROR_RX_FRAME: /* fall throuth */
  case OSSA_IO_XFER_ERROR_CREDIT_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_SATA: /* fall throuth */
#endif
  case OSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_ABORTED_DUE_TO_SRST: /* fall throuth */
  case OSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE: /* fall throuth */
#ifdef REMOVED
  case OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE: /* fall throuth */
  case OSSA_IO_XFER_ERROR_DISRUPTED_PHY_DOWN: /* fall throuth */
  case OSSA_IO_XFER_ERROR_OFFSET_MISMATCH: /* fall throuth */
  case OSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN: /* fall throuth */
#endif
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_XFER_ERROR_* 0x%x!!!\n", agIOStatus));

    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
#ifdef REMOVED
  case OSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_CMD_ISSUE_BREAK_BEFORE_ACK_NAK: /* fall throuth */
  case OSSA_IO_XFER_ERROR_CMD_ISSUE_PHY_DOWN_BEFORE_ACK_NAK: /* fall throuth */
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_XFER_ERROR_CMD_ISSUE_* 0x%x!!!\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
  case OSSA_IO_XFER_PIO_SETUP_ERROR:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_XFER_PIO_SETUP_ERROR!!!\n"));
    if (smDeviceHandle == agNULL)
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: wrong, smDeviceHandle is NULL!!!\n"));
    }
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: wrong, oneDeviceData is NULL!!!\n"));
    }
    else
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: did %d!!!\n", oneDeviceData->id));
    }
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
#endif
  case OSSA_IO_DS_IN_ERROR:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_DS_IN_ERROR!!!\n"));
    if (smDeviceHandle == agNULL)
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: wrong, smDeviceHandle is NULL!!!\n"));
      tdsmIOCompletedCB( smRoot,
                         smIORequestBody->smIORequest,
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         interruptContext);
      return;
    }
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: wrong, oneDeviceData is NULL!!!\n"));
    }
    else
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: did %d!!!\n", oneDeviceData->id));
    }
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;
  case OSSA_IO_DS_NON_OPERATIONAL:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_DS_NON_OPERATIONAL!!!\n"));
    if (smDeviceHandle == agNULL)
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: wrong, smDeviceHandle is NULL!!!\n"));
      tdsmIOCompletedCB( smRoot,
                         smIORequestBody->smIORequest,
                         smIOFailed,
                         smDetailOtherError,
                         agNULL,
                         interruptContext);
      return;
    }
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: wrong, oneDeviceData is NULL!!!\n"));
    }
    else
    {
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: did %d!!!\n", oneDeviceData->id));
      agDevHandle = oneDeviceData->agDevHandle;
      if (oneDeviceData->valid == agTRUE)
      {
        saSetDeviceState(agRoot, agNULL, tdsmRotateQnumber(smRoot, smDeviceHandle), agDevHandle, SA_DS_OPERATIONAL);
      }
    }
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_PORT_IN_RESET:
  case OSSA_IO_DS_IN_RECOVERY:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: OSSA_IO_DS_IN_RECOVERY or OSSA_IO_PORT_IN_RESET status %x\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: SSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_XX status %x\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                      smIORequestBody->smIORequest,
                      smIOFailed,
                      smDetailOtherError,
                      agNULL,
                      interruptContext);
    break;
  case OSSA_IO_DS_INVALID:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: OSSA_IO_DS_INVALID status %x\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                      smIORequestBody->smIORequest,
                      smIOFailed,
                      smDetailOtherError,
                      agNULL,
                      interruptContext);
    break;

  case OSSA_MPI_IO_RQE_BUSY_FULL:
  case OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE:
  case OSSA_MPI_ERR_ATAPI_DEVICE_BUSY:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_MPI_%x!!!\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailBusy,
                       agNULL,
                       interruptContext);
    break;

  case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS: /* fall through */
#ifdef REMOVED
  case OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH:
#endif
  case OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID: /* fall through */
  case OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR: /* fall through */
  case OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS:
  case OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE:

      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = ENCRYPTION ERROR 0x%x!!!\n", agIOStatus));
      smsatEncryptionHandler(smRoot,
                             agIORequest,
                             agIOStatus,
                             agIOInfoLen,
                             agParam,
                             0,
           interruptContext);
      break;

#ifdef REMOVED
  case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
      SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = DIF ERROR 0x%x!!!\n", agIOStatus));
      smsatDifHandler(smRoot,
                      agIORequest,
                      agIOStatus,
                      agIOInfoLen,
                      agParam,
                      0,
                      interruptContext);
      break;
#endif

  default:
    SM_DBG1(("smsatDelayedProcessAbnormalCompletion: agIOStatus = unknown!!!\n"));
    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
    break;

  } /* switch */
  return;
}

osGLOBAL void
smsatIDStartCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext
               )
{
 /*
    In the process of SAT_IDENTIFY_DEVICE during discovery
  */
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                  *smRoot = agNULL;
  smIntRoot_t               *smIntRoot = agNULL;
  smIntContext_t            *smAllShared = agNULL;
  smIORequestBody_t         *smIORequestBody;
  smIORequestBody_t         *smOrgIORequestBody = agNULL;
  smDeviceHandle_t          *smDeviceHandle;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatIOContext_t          *satNewIOContext;
  smSatInternalIo_t         *satIntIo;
  smSatInternalIo_t         *satNewIntIo = agNULL;
  smDeviceData_t            *oneDeviceData;
  smIORequest_t             *smOrgIORequest = agNULL;
//  agsaFisRegD2HData_t       *deviceToHostFisData = agNULL;
//  bit8                      signature[8];
#ifdef  TD_DEBUG_ENABLE
  agsaFisPioSetupHeader_t   *satPIOSetupHeader = agNULL;
  bit32                      ataStatus = 0;
  bit32                      ataError;
#endif
  agsaSATAIdentifyData_t    *pSATAIdData;
  bit16                     *tmpptr, tmpptr_tmp;
  bit32                      x;
  void                      *sglVirtualAddr;
  bit32                      status = 0;
//  tdsaPortContext_t         *onePortContext = agNULL;
//  tiPortalContext_t         *tiPortalContext = agNULL;
//  bit32                     retry_status;
  smIORequest_t             *smIORequest;
  agsaDevHandle_t           *agDevHandle = agNULL;

  SM_DBG1(("smsatIDStartCB: start\n"));
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  smDeviceHandle         = satIOContext->psmDeviceHandle;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  SM_DBG1(("smsatIDStartCB: did %d\n", oneDeviceData->id));
//  onePortContext = oneDeviceData->tdPortContext;
//  tiPortalContext= onePortContext->tiPortalContext;
  oneDeviceData->IDDeviceValid = agFALSE;
  if (satIntIo == agNULL)
  {
    SM_DBG1(("smsatIDStartCB: External, OS generated!!!\n"));
    SM_DBG1(("smsatIDStartCB: Not possible case!!!\n"));
    satOrgIOContext      = satIOContext;
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  else
  {
    SM_DBG3(("smsatIDStartCB: Internal, SM generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG5(("smsatIDStartCB: satOrgIOContext is NULL\n"));
    }
    else
    {
      SM_DBG5(("smsatIDStartCB: satOrgIOContext is NOT NULL\n"));
      smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
      if (smOrgIORequestBody == agNULL)
      {
        SM_DBG1(("smsatIDStartCB: smOrgIORequestBody is NULL!!!\n"));
        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource(smRoot, oneDeviceData, satIntIo);
        return;
      }
    }
    sglVirtualAddr         = satIntIo->satIntSmScsiXchg.sglVirtualAddr;
  }
  smOrgIORequest           = smIORequestBody->smIORequest;
  smIORequest              = smOrgIORequestBody->smIORequest;
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;


  if ( agIOStatus == OSSA_IO_XFER_OPEN_RETRY_TIMEOUT ||
       agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY ||
       agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED ||
       agIOStatus == OSSA_IO_DS_NON_OPERATIONAL )
  {
    SM_DBG1(("smsatIDStartCB: OPEN_RETRY_TIMEOUT or STP_RESOURCES_BUSY or OPEN_RETRY_BACKOFF_THRESHOLD_REACHED or OSSA_IO_DS_NON_OPERATIONAL!!! 0x%x\n", agIOStatus));
    SM_DBG1(("smsatIDStartCB: did %d!!!\n", oneDeviceData->id));
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    SM_DBG2(("smsatIDStartCB: smOrgIORequestBody %p smIORequest %p\n", smOrgIORequestBody, smIORequest));
    SM_DBG2(("smsatIDStartCB: smOrgIORequestBody->id %d\n", smOrgIORequestBody->id));
    if (agIOStatus == OSSA_IO_XFER_OPEN_RETRY_TIMEOUT)
    {
      tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIORetry, &(oneDeviceData->satIdentifyData));
    }
    else if ( agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED ||
              agIOStatus == OSSA_IO_DS_NON_OPERATIONAL )
    {
      /* set device to operational */
      agDevHandle = oneDeviceData->agDevHandle;
      if (oneDeviceData->valid == agTRUE)
      {
        saSetDeviceState(agRoot, agNULL, tdsmRotateQnumber(smRoot, smDeviceHandle), agDevHandle, SA_DS_OPERATIONAL);
      }
      tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIORetry, &(oneDeviceData->satIdentifyData));
    }
    else
    {
      tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSTPResourceBusy, &(oneDeviceData->satIdentifyData));
    }
    return;
  }

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatIDStartCB: agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    SM_DBG1(("smsatIDStartCB: did %d!!!\n", oneDeviceData->id));
    SM_DBG1(("smsatIDStartCB: before pending IO %d NCQ pending IO %d NONNCQ pending IO %d\n",
    oneDeviceData->satPendingIO, oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    SM_DBG1(("smsatIDStartCB: after pending IO %d NCQ pending IO %d NONNCQ pending IO %d\n",
    oneDeviceData->satPendingIO, oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    SM_DBG2(("smsatIDStartCB: smOrgIORequestBody %p smIORequest %p\n", smOrgIORequestBody, smIORequest));
    SM_DBG2(("smsatIDStartCB: smOrgIORequestBody->id %d\n", smOrgIORequestBody->id));
    tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOFailed, &(oneDeviceData->satIdentifyData));
    return;
  }

  if (agIOStatus == OSSA_IO_ABORTED ||
      agIOStatus == OSSA_IO_UNDERFLOW ||
      agIOStatus == OSSA_IO_XFER_ERROR_BREAK ||
      agIOStatus == OSSA_IO_XFER_ERROR_PHY_NOT_READY ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BREAK ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION ||
      agIOStatus == OSSA_IO_XFER_ERROR_NAK_RECEIVED ||
      agIOStatus == OSSA_IO_XFER_ERROR_DMA ||
      agIOStatus == OSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT ||
      agIOStatus == OSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE ||
      agIOStatus == OSSA_IO_NO_DEVICE ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION ||
      agIOStatus == OSSA_IO_PORT_IN_RESET ||
      agIOStatus == OSSA_IO_DS_NON_OPERATIONAL ||
      agIOStatus == OSSA_IO_DS_IN_RECOVERY ||
      agIOStatus == OSSA_IO_DS_IN_ERROR ||
      agIOStatus == OSSA_IO_DS_INVALID
      )
  {
    SM_DBG1(("smsatIDStartCB: OSSA_IO_OPEN_CNX_ERROR 0x%x!!!\n", agIOStatus));
    SM_DBG1(("smsatIDStartCB: did %d!!!\n", oneDeviceData->id));
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);


    SM_DBG2(("smsatIDStartCB: smOrgIORequestBody %p smIORequest %p\n", smOrgIORequestBody, smIORequest));
    SM_DBG2(("smsatIDStartCB: smOrgIORequestBody->id %d\n", smOrgIORequestBody->id));
    tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOFailed, &(oneDeviceData->satIdentifyData));
    return;
  }

  if ( agIOStatus != OSSA_IO_SUCCESS ||
       (agIOStatus == OSSA_IO_SUCCESS && agFirstDword != agNULL && agIOInfoLen != 0)
     )
  {
#ifdef  TD_DEBUG_ENABLE
    /* only agsaFisPioSetup_t is expected */
    satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
    ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
    ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
    SM_DBG1(("smsatIDStartCB: ataStatus 0x%x ataError 0x%x!!!\n", ataStatus, ataError));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    SM_DBG2(("smsatIDStartCB: smOrgIORequestBody %p smIORequest %p\n", smOrgIORequestBody, smIORequest));
    SM_DBG2(("smsatIDStartCB: smOrgIORequestBody->id %d\n", smOrgIORequestBody->id));

    {
       tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOFailed, &(oneDeviceData->satIdentifyData));
    }
    return;
  }


  /* success */
  SM_DBG3(("smsatIDStartCB: Success\n"));
  SM_DBG3(("smsatIDStartCB: Success did %d\n", oneDeviceData->id));

  /* Convert to host endian */
  tmpptr = (bit16*)sglVirtualAddr;
  for (x=0; x < sizeof(agsaSATAIdentifyData_t)/sizeof(bit16); x++)
  {
    OSSA_READ_LE_16(AGROOT, &tmpptr_tmp, tmpptr, 0);
    *tmpptr = tmpptr_tmp;
    tmpptr++;
  }

  pSATAIdData = (agsaSATAIdentifyData_t *)sglVirtualAddr;
  //smhexdump("satAddSATAIDDevCB before", (bit8 *)pSATAIdData, sizeof(agsaSATAIdentifyData_t));

  SM_DBG5(("smsatIDStartCB: OS satOrgIOContext %p \n", satOrgIOContext));
  SM_DBG5(("smsatIDStartCB: TD satIOContext %p \n", satIOContext));
  SM_DBG5(("smsatIDStartCB: OS tiScsiXchg %p \n", satOrgIOContext->smScsiXchg));
  SM_DBG5(("smsatIDStartCB: TD tiScsiXchg %p \n", satIOContext->smScsiXchg));


   /* copy ID Dev data to oneDeviceData */
  oneDeviceData->satIdentifyData = *pSATAIdData;
  oneDeviceData->IDDeviceValid = agTRUE;

#ifdef SM_INTERNAL_DEBUG
  smhexdump("smsatIDStartCB ID Dev data",(bit8 *)pSATAIdData, sizeof(agsaSATAIdentifyData_t));
  smhexdump("smsatIDStartCB Device ID Dev data",(bit8 *)&oneDeviceData->satIdentifyData, sizeof(agsaSATAIdentifyData_t));
#endif

  /* set oneDeviceData fields from IndentifyData */
  smsatSetDevInfo(oneDeviceData,pSATAIdData);
  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);

  if (smIORequest->tdData == smIORequest->smData)
  {
    SM_DBG1(("smsatIDStartCB: the same tdData and smData error!\n"));
  }

  /* send the Set Feature ATA command to SATA device for enbling PIO and DMA transfer mode*/
  satNewIntIo = smsatAllocIntIoResource( smRoot,
                                   smOrgIORequest,
                                   oneDeviceData,
                                   0,
                                   satNewIntIo);

  if (satNewIntIo == agNULL)
  {
    SM_DBG1(("smsatIDStartCB: momory allocation fails\n"));
    tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOFailed, &(oneDeviceData->satIdentifyData));
    return;
  } /* end memory allocation */

  satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                    smOrgIORequest,
                                    oneDeviceData,
                                    agNULL,
                                    satOrgIOContext
                                    );
  /*enable PIO mode*/
  status = smsatSetFeaturesPIO(smRoot,
                     &satNewIntIo->satIntSmIORequest,
                     satNewIOContext->psmDeviceHandle,
                     &satNewIntIo->satIntSmScsiXchg, /* orginal from OS layer */
                     satNewIOContext
                     );

  if (status != SM_RC_SUCCESS)
  {
      smsatFreeIntIoResource(smRoot,
                             oneDeviceData,
                             satNewIntIo);
      /* clean up TD layer's IORequestBody */
      tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOFailed, &(oneDeviceData->satIdentifyData));
  }

  SM_DBG2(("smsatIDStartCB: End device id %d\n", oneDeviceData->id));
  return;
}


osGLOBAL void
smsatIOCompleted(
                 smRoot_t           *smRoot,
                 smIORequest_t      *smIORequest,
                 agsaFisHeader_t    *agFirstDword,
                 bit32              respFisLen,
                 agsaFrameHandle_t  agFrameHandle,
                 smSatIOContext_t     *satIOContext,
                 bit32              interruptContext
    )
{
//  satDeviceData_t           *pSatDevData;
  smDeviceData_t            *oneDeviceData;
  smScsiRspSense_t          *pSense;
#ifdef  TD_DEBUG_ENABLE
  smIniScsiCmnd_t           *pScsiCmnd;
#endif
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                      ataStatus = 0;
  bit32                      ataError;
  smSatInternalIo_t         *satIntIo = agNULL;
  bit32                      status;
//  agsaRoot_t                *agRoot;
//  agsaDevHandle_t           *agDevHandle;
  smDeviceHandle_t          *smDeviceHandle;
  smSatIOContext_t          *satIOContext2;
  smIORequestBody_t         *smIORequestBody;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  agsaFisSetDevBitsHeader_t *statSetDevBitFisHeader = agNULL;
  smIORequest_t              smIORequestTMP;

  pSense          = satIOContext->pSense;
  oneDeviceData   = satIOContext->pSatDevData;
#ifdef  TD_DEBUG_ENABLE
  pScsiCmnd       = satIOContext->pScsiCmnd;
#endif
  hostToDevFis    = satIOContext->pFis;


//  agRoot          = ((tdsaDeviceData_t *)(pSatDevData->satSaDeviceData))->agRoot;
//  agDevHandle     = ((tdsaDeviceData_t *)(pSatDevData->satSaDeviceData))->agDevHandle;
//  tiDeviceHandle  = &((tdsaDeviceData_t *)(pSatDevData->satSaDeviceData))->tiDeviceHandle;
  smDeviceHandle    = satIOContext->psmDeviceHandle;
  /*
   * Find out the type of response FIS:
   * Set Device Bit FIS or Reg Device To Host FIS.
   */

  /* First assume it is Reg Device to Host FIS */
  statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
  ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  ataError      = statDevToHostFisHeader->error;    /* ATA Eror register   */

  SM_DBG5(("smsatIOCompleted: start\n"));

  /* for debugging */
  SM_DBG1(("smsatIOCompleted: H to D command 0x%x!!!\n", hostToDevFis->h.command));
  SM_DBG1(("smsatIOCompleted: D to H fistype 0x%x!!!\n", statDevToHostFisHeader->fisType));


  if (statDevToHostFisHeader->fisType == SET_DEV_BITS_FIS)
  {
    /* It is Set Device Bits FIS */
    statSetDevBitFisHeader = (agsaFisSetDevBitsHeader_t *)&(agFirstDword->D2H);
    /* Get ATA Status register */
    ataStatus = (statSetDevBitFisHeader->statusHi_Lo & 0x70);               /* bits 4,5,6 */
    ataStatus = ataStatus | (statSetDevBitFisHeader->statusHi_Lo & 0x07);   /* bits 0,1,2 */

    /* ATA Eror register   */
    ataError  = statSetDevBitFisHeader->error;

    statDevToHostFisHeader = agNULL;
  }

  else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
  {
    SM_DBG1(("smsatIOCompleted: *** UNEXPECTED RESP FIS TYPE 0x%x *** smIORequest=%p!!!\n",
                 statDevToHostFisHeader->fisType, smIORequest));

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

  if ( ataStatus & DF_ATA_STATUS_MASK )
  {
    oneDeviceData->satDeviceFaultState = agTRUE;
  }
  else
  {
    oneDeviceData->satDeviceFaultState = agFALSE;
  }

  SM_DBG5(("smsatIOCompleted: smIORequest=%p  CDB=0x%x ATA CMD =0x%x\n",
    smIORequest, pScsiCmnd->cdb[0], hostToDevFis->h.command));

  /*
   * Decide which ATA command is the translation needed
   */
  switch(hostToDevFis->h.command)
  {
    case SAT_READ_FPDMA_QUEUED:
    case SAT_WRITE_FPDMA_QUEUED:

      /************************************************************************
       *
       * !!!! See Section 13.5.2.4 of SATA 2.5 specs.                      !!!!
       * !!!! If the NCQ error ends up here, it means that the device sent !!!!
       * !!!! Set Device Bit FIS (which has SActive register) instead of   !!!!
       * !!!! Register Device To Host FIS (which does not have SActive     !!!!
       * !!!! register). The callback ossaSATAEvent() deals with the case  !!!!
       * !!!! where Register Device To Host FIS was sent by the device.    !!!!
       *
       * For NCQ we need to issue READ LOG EXT command with log page 10h
       * to get the error and to allow other I/Os to continue.
       *
       * Here is the basic flow or sequence of error recovery, note that due
       * to the SATA HW assist that we have, this sequence is slighly different
       * from the one described in SATA 2.5:
       *
       * 1. Set SATA device flag to indicate error condition and returning busy
       *    for all new request.
       *   return SM_RC_SUCCESS;

       * 2. Because the HW/LL layer received Set Device Bit FIS, it can get the
       *    tag or I/O context for NCQ request, SATL would translate the ATA error
       *    to SCSI status and return the original NCQ I/O with the appopriate
       *    SCSI status.
       *
       * 3. Prepare READ LOG EXT page 10h command. Set flag to indicate that
       *    the failed I/O has been returned to the OS Layer. Send command.
       *
       * 4. When the device receives READ LOG EXT page 10h request all other
       *    pending I/O are implicitly aborted. No completion (aborted) status
       *    will be sent to the host for these aborted commands.
       *
       * 5. SATL receives the completion for READ LOG EXT command in
       *    smsatReadLogExtCB(). Steps 6,7,8,9 below are the step 1,2,3,4 in
       *    smsatReadLogExtCB().
       *
       * 6. Check flag that indicates whether the failed I/O has been returned
       *    to the OS Layer. If not, search the I/O context in device data
       *    looking for a matched tag. Then return the completion of the failed
       *    NCQ command with the appopriate/trasnlated SCSI status.
       *
       * 7. Issue abort to LL layer to all other pending I/Os for the same SATA
       *    drive.
       *
       * 8. Free resource allocated for the internally generated READ LOG EXT.
       *
       * 9. At the completion of abort, in the context of ossaSATACompleted(),
       *    return the I/O with error status to the OS-App Specific layer.
       *    When all I/O aborts are completed, clear SATA device flag to
       *    indicate ready to process new request.
       *
       ***********************************************************************/

      SM_DBG1(("smsatIOCompleted: NCQ ERROR smIORequest=%p ataStatus=0x%x ataError=0x%x!!!\n",
          smIORequest, ataStatus, ataError ));

      /* Set flag to indicate we are in recovery */
      oneDeviceData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;

      /* Return the failed NCQ I/O to OS-Apps Specifiic layer */
      smsatDefaultTranslation( smRoot,
                               smIORequest,
                               satIOContext,
                               pSense,
                               (bit8)ataStatus,
                               (bit8)ataError,
                               interruptContext );

      /*
       * Allocate resource for READ LOG EXT page 10h
       */
      satIntIo = smsatAllocIntIoResource( smRoot,
                                          &(smIORequestTMP), /* anything but NULL */
                                          oneDeviceData,
                                          sizeof (satReadLogExtPage10h_t),
                                          satIntIo);

      /*
       * If we cannot allocate resource for READ LOG EXT 10 in order to do
       * the normal NCQ recovery, we will do SATA device reset.
       */
      if (satIntIo == agNULL)
      {
        SM_DBG1(("smsatIOCompleted: can't send RLE due to resource lack!!!\n"));

        /* Abort I/O after completion of device reset */
        oneDeviceData->satAbortAfterReset = agTRUE;
#ifdef NOT_YET
        /* needs further investigation */
        /* no report to OS layer */
        satSubTM(smRoot,
                 smDeviceHandle,
                 SM_INTERNAL_TM_RESET,
                 agNULL,
                 agNULL,
                 agNULL,
                 agFALSE);
#endif


        SM_DBG1(("smsatIOCompleted: calling saSATADeviceReset 1!!!\n"));
        return;
      }


      /*
       * Set flag to indicate that the failed I/O has been returned to the
       * OS-App specific Layer.
       */
      satIntIo->satIntFlag = AG_SAT_INT_IO_FLAG_ORG_IO_COMPLETED;

      /* compare to satPrepareNewIO() */
      /* Send READ LOG EXIT page 10h command */

      /*
       * Need to initialize all the fields within satIOContext except
       * reqType and satCompleteCB which will be set depending on cmd.
       */

      smIORequestBody = (smIORequestBody_t *)satIntIo->satIntRequestBody;
      satIOContext2 = &(smIORequestBody->transport.SATA.satIOContext);

      satIOContext2->pSatDevData   = oneDeviceData;
      satIOContext2->pFis          = &(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
      satIOContext2->pScsiCmnd     = &(satIntIo->satIntSmScsiXchg.scsiCmnd);
      satIOContext2->pSense        = &(smIORequestBody->transport.SATA.sensePayload);
      satIOContext2->pSmSenseData  = &(smIORequestBody->transport.SATA.smSenseData);
      satIOContext2->pSmSenseData->senseData = satIOContext2->pSense;

      satIOContext2->smRequestBody = satIntIo->satIntRequestBody;
      satIOContext2->interruptContext = interruptContext;
      satIOContext2->satIntIoContext  = satIntIo;

      satIOContext2->psmDeviceHandle = smDeviceHandle;
      satIOContext2->satOrgIOContext = agNULL;
      satIOContext2->smScsiXchg = agNULL;

      status = smsatSendReadLogExt( smRoot,
                                    &satIntIo->satIntSmIORequest,
                                    smDeviceHandle,
                                    &satIntIo->satIntSmScsiXchg,
                                    satIOContext2);

      if (status != SM_RC_SUCCESS)
      {
        SM_DBG1(("smsatIOCompleted: can't send RLE due to LL api failure!!!\n"));
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);

        /* Abort I/O after completion of device reset */
        oneDeviceData->satAbortAfterReset = agTRUE;
#ifdef NOT_YET
        /* needs further investigation */
        /* no report to OS layer */
        satSubTM(smRoot,
                 smDeviceHandle,
                 SM_INTERNAL_TM_RESET,
                 agNULL,
                 agNULL,
                 agNULL,
                 agFALSE);
#endif

        SM_DBG1(("smsatIOCompleted: calling saSATADeviceReset 2!!!\n"));
        return;
      }

      break;

    case SAT_READ_DMA_EXT:
      /* fall through */
      /* Use default status/error translation */

    case SAT_READ_DMA:
      /* fall through */
      /* Use default status/error translation */

    default:
      smsatDefaultTranslation( smRoot,
                               smIORequest,
                               satIOContext,
                               pSense,
                               (bit8)ataStatus,
                               (bit8)ataError,
                               interruptContext );
      break;

  }  /* end switch  */
  return;
}


osGLOBAL void
smsatEncryptionHandler(
                       smRoot_t                *smRoot,
                       agsaIORequest_t         *agIORequest,
                       bit32                   agIOStatus,
                       bit32                   agIOInfoLen,
                       void                    *agParam,
                       bit32                   agOtherInfo,
                       bit32                   interruptContext
                      )
{
  smIORequestBody_t      *smIORequestBody;
  bit32                  errorDetail = smDetailOtherError;

  SM_DBG1(("smsatEncryptionHandler: start\n"));
  SM_DBG1(("smsatEncryptionHandler: agIOStatus 0x%x\n", agIOStatus));

  smIORequestBody = (smIORequestBody_t *)agIORequest->osData;

  switch (agIOStatus)
  {
  case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS:
      SM_DBG1(("smsatEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS\n"));
      errorDetail = smDetailDekKeyCacheMiss;
      break;
  case OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID:
      SM_DBG1(("smsatEncryptionHandler: OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID\n"));
      errorDetail = smDetailCipherModeInvalid;
      break;
  case OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH:
      SM_DBG1(("smsatEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH\n"));
      errorDetail = smDetailDekIVMismatch;
      break;
  case OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR:
      SM_DBG1(("smsatEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR\n"));
      errorDetail = smDetailDekRamInterfaceError;
      break;
  case OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS:
      SM_DBG1(("smsatEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS\n"));
      errorDetail = smDetailDekIndexOutofBounds;
      break;
  case OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE:
      SM_DBG1(("smsatEncryptionHandler:OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE\n"));
      errorDetail = smDetailOtherError;
      break;
  default:
      SM_DBG1(("smsatEncryptionHandler: other error!!! 0x%x\n", agIOStatus));
      errorDetail = smDetailOtherError;
      break;
  }

  tdsmIOCompletedCB( smRoot,
                     smIORequestBody->smIORequest,
                     smIOEncryptError,
                     errorDetail,
                     agNULL,
                     interruptContext
                   );
  return;
}

osGLOBAL void
smsatDifHandler(
                smRoot_t                *smRoot,
                agsaIORequest_t         *agIORequest,
                bit32                   agIOStatus,
                bit32                   agIOInfoLen,
                void                    *agParam,
                bit32                   agOtherInfo,
                bit32                   interruptContext
               )
{
  smIORequestBody_t      *smIORequestBody;
  bit32                  errorDetail = smDetailOtherError;
#ifdef  TD_DEBUG_ENABLE
  agsaDifDetails_t       *DifDetail;
#endif

  SM_DBG1(("smsatDifHandler: start\n"));
  SM_DBG1(("smsatDifHandler: agIOStatus 0x%x\n", agIOStatus));
  smIORequestBody = (smIORequestBody_t *)agIORequest->osData;
#ifdef  TD_DEBUG_ENABLE
  DifDetail = (agsaDifDetails_t *)agParam;
#endif

  switch (agIOStatus)
  {
  case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH:
      SM_DBG1(("smsatDifHandler: OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH\n"));
      errorDetail = smDetailDifAppTagMismatch;
      break;
  case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH:
      SM_DBG1(("smsatDifHandler: OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH\n"));
      errorDetail = smDetailDifRefTagMismatch;
      break;
  case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
      SM_DBG1(("smsatDifHandler: OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH\n"));
      errorDetail = smDetailDifCrcMismatch;
      break;
  default:
      SM_DBG1(("smsatDifHandler: other error!!! 0x%x\n", agIOStatus));
      errorDetail = smDetailOtherError;
      break;
  }

  SM_DBG1(("smsatDifHandler: DIF detail UpperLBA 0x%08x LowerLBA 0x%08x\n", DifDetail->UpperLBA, DifDetail->LowerLBA));

  tdsmIOCompletedCB( smRoot,
                     smIORequestBody->smIORequest,
                     smIODifError,
                     errorDetail,
                     agNULL,
                     interruptContext
                   );
  return;
}

osGLOBAL void
smsatProcessAbort(
                  smRoot_t           *smRoot,
                  smIORequest_t      *smIORequest,
                  smSatIOContext_t     *satIOContext
                 )
{
  smDeviceData_t            *oneDeviceData;
#ifdef REMOVED
  smDeviceHandle_t          *smDeviceHandle;
#endif
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;

  SM_DBG5(("smsatProcessAbort: start\n"));

  oneDeviceData   = satIOContext->pSatDevData;
#ifdef REMOVED
  smDeviceHandle  = satIOContext->psmDeviceHandle;
#endif
  hostToDevFis    = satIOContext->pFis;

  if ( (hostToDevFis->h.command == SAT_SMART && hostToDevFis->h.features == SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE) &&
       (hostToDevFis->d.lbaLow != 0x01 && hostToDevFis->d.lbaLow != 0x02)
      )
  {
    /* no completion for send diagnotic in background. It is done in satSendDiagnostic() */
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOFailed,
                       smDetailAborted,
                       agNULL,
                       satIOContext->interruptContext);
  }

  if ( oneDeviceData->satTmTaskTag != agNULL )
  {
    SM_DBG1(("smsatProcessAbort: TM callback!!!\n"));
#ifdef REMOVED
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMOK,
                 oneDeviceData->satTmTaskTag);
#endif
    /*
     * Reset flag
     */
    oneDeviceData->satTmTaskTag = agNULL;
  }

  /*
   * Check if we are in recovery mode and need to update the recovery flag
   */
  if ((oneDeviceData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
      (oneDeviceData->satPendingIO == 0 ))
  {
    oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
    SM_DBG1(("smsatProcessAbort: STATE NORMAL.!!!\n"));
  }
  SM_DBG1(("smsatProcessAbort: satDriveState %d!!!\n", oneDeviceData->satDriveState));
  SM_DBG1(("smsatProcessAbort: satPendingIO %d satNCQMaxIO %d!!!\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
  SM_DBG1(("smsatProcessAbort: satPendingNCQIO %d satPendingNONNCQIO %d!!!\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));

  return;
}


osGLOBAL void
smsatNonDataIOCB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam,
                  void              *ioContext
                 )
{
  smRoot_t             *smRoot = agNULL;
  smIntRoot_t          *smIntRoot = agNULL;
  smIntContext_t       *smAllShared = agNULL;
  smIORequestBody_t    *smIORequestBody;
  bit32                interruptContext;
  smSatIOContext_t       *satIOContext;
  smDeviceData_t       *oneDeviceData;

  SM_DBG2(("smsatNonDataIOCB: start\n"));
  SM_DBG5(("satNonDataIOCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
    agIORequest, agIOStatus, agIOInfoLen));

  smIORequestBody = (smIORequestBody_t *)agIORequest->osData;
  satIOContext    = (smSatIOContext_t *) ioContext;
  oneDeviceData   = satIOContext->pSatDevData;
  smRoot          = oneDeviceData->smRoot;
  smIntRoot       = (smIntRoot_t *)smRoot->smData;
  smAllShared     = (smIntContext_t *)&smIntRoot->smAllShared;
  interruptContext = satIOContext->interruptContext;

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);


  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  /* Process completion */
  if( (agIOStatus == OSSA_IO_SUCCESS) && (agIOInfoLen==0))
  {
   
    SM_DBG1(("satNonDataIOCB: *** ERROR***  agIORequest=%p agIOStatus=0x%x agIOInfoLen %d!!!\n",
      agIORequest, agIOStatus, agIOInfoLen));

    tdsmIOCompletedCB( smRoot,
                       smIORequestBody->smIORequest,
                       smIOFailed,
                       smDetailOtherError,
                       agNULL,
                       interruptContext);
  }
  else
  {
    /* More checking needed, for non-data IO this should be the normal case */
    smsatProcessAbnormalCompletion( agRoot,
                                    agIORequest,
                                    agIOStatus,
                                    agFirstDword,
                                    agIOInfoLen,
                                    agParam,
                                    satIOContext);
  }
  return;
}

osGLOBAL void
smsatInquiryCB(
               agsaRoot_t        *agRoot,
               agsaIORequest_t   *agIORequest,
               bit32             agIOStatus,
               agsaFisHeader_t   *agFirstDword,
               bit32             agIOInfoLen,
               void              *agParam,
               void              *ioContext
              )
{
  /*
    In the process of Inquiry
    Process SAT_IDENTIFY_DEVICE
  */
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatInternalIo_t        *satIntIo;
//  satDeviceData_t         *satDevData;
  smDeviceData_t           *oneDeviceData;
  smScsiRspSense_t         *pSense;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;
#ifdef  TD_DEBUG_ENABLE
  agsaFisPioSetupHeader_t  *satPIOSetupHeader = agNULL;
  bit32                     ataStatus = 0;
  bit32                     ataError;
#endif
  smScsiInitiatorRequest_t *smScsiRequest; /* TD's smScsiXchg */
  smScsiInitiatorRequest_t *smOrgScsiRequest; /* OS's smScsiXchg */
  agsaSATAIdentifyData_t   *pSATAIdData;
  bit8                     *pInquiry;
  bit8                      page = 0xFF;
  bit16                    *tmpptr,tmpptr_tmp;
  bit32                     x;
  bit32                     lenReceived = 0;
  bit32                     allocationLen = 0;
  bit32                     lenNeeded = 0;
  bit8                      dataBuffer[SATA_PAGE89_INQUIRY_SIZE] = {0};


  SM_DBG6(("smsatInquiryCB: start\n"));
  SM_DBG6(("smsatInquiryCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  smScsiRequest          = satIOContext->smScsiXchg;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG6(("smsatInquiryCB: External, OS generated\n"));
    pSense               = satIOContext->pSense;
    scsiCmnd             = satIOContext->pScsiCmnd;
    satOrgIOContext      = satIOContext;
    smOrgIORequest       = smIORequestBody->smIORequest;
  }
  else
  {
    SM_DBG6(("smsatInquiryCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG1(("smsatInquiryCB: satOrgIOContext is NULL, wrong!!!\n"));
      return;
    }
    else
    {
      SM_DBG6(("smsatInquiryCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense                 = satOrgIOContext->pSense;
    scsiCmnd               = satOrgIOContext->pScsiCmnd;
  }

  smOrgScsiRequest         = satOrgIOContext->smScsiXchg;
  pInquiry                 = dataBuffer;

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  SM_DBG3(("smsatInquiryCB: did %d\n", oneDeviceData->id));

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatInquiryCB: agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY)
    {
      SM_DBG1(("smsatInquiryCB: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY!!!\n"));
      /* should NOT be retried */
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOFailed,
                         smDetailNoLogin,
                         agNULL,
                         satOrgIOContext->interruptContext
                       );
    }
    else
    {
      SM_DBG1(("smsatInquiryCB: NOT OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY!!!\n"));
      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOFailed,
                         smDetailNoLogin,
                         agNULL,
                         satOrgIOContext->interruptContext
                        );
    }
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BREAK ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY
      )
  {
    SM_DBG1(("smsatInquiryCB: OSSA_IO_OPEN_CNX_ERROR!!!\n"));

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOFailed,
                       smDetailNoLogin,
                       agNULL,
                       satOrgIOContext->interruptContext
                     );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

 if ( agIOStatus != OSSA_IO_SUCCESS ||
      (agIOStatus == OSSA_IO_SUCCESS && agFirstDword != agNULL && agIOInfoLen != 0)
    )
 {
#ifdef  TD_DEBUG_ENABLE
   /* only agsaFisPioSetup_t is expected */
   satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
   ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
   ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
   SM_DBG1(("smsatInquiryCB: ataStatus 0x%x ataError 0x%x!!!\n", ataStatus, ataError));

   /* Process abort case */
   if (agIOStatus == OSSA_IO_ABORTED)
   {
     smsatProcessAbort(smRoot,
                       smOrgIORequest,
                       satOrgIOContext
                      );

     smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

     smsatFreeIntIoResource( smRoot,
                             oneDeviceData,
                             satIntIo);
     return;
   }

   tdsmIOCompletedCB( smRoot,
                      smOrgIORequest,
                      smIOFailed,
                      smDetailOtherError,
                      agNULL,
                      satOrgIOContext->interruptContext
                     );

   smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

   smsatFreeIntIoResource( smRoot,
                           oneDeviceData,
                           satIntIo);
   return;
  }

 /* success */


 /* Convert to host endian */
 tmpptr = (bit16*)(smScsiRequest->sglVirtualAddr);
 for (x=0; x < sizeof(agsaSATAIdentifyData_t)/sizeof(bit16); x++)
 {
   OSSA_READ_LE_16(AGROOT, &tmpptr_tmp, tmpptr, 0);
   *tmpptr = tmpptr_tmp;
   tmpptr++;
   /*Print tmpptr_tmp here for debugging purpose*/
 }

 pSATAIdData = (agsaSATAIdentifyData_t *)(smScsiRequest->sglVirtualAddr);

 SM_DBG5(("smsatInquiryCB: OS satOrgIOContext %p \n", satOrgIOContext));
 SM_DBG5(("smsatInquiryCB: TD satIOContext %p \n", satIOContext));
 SM_DBG5(("smsatInquiryCB: OS smScsiXchg %p \n", satOrgIOContext->smScsiXchg));
 SM_DBG5(("smsatInquiryCB: TD smScsiXchg %p \n", satIOContext->smScsiXchg));

 /* copy ID Dev data to oneDeviceData */
 oneDeviceData->satIdentifyData = *pSATAIdData;
 oneDeviceData->IDDeviceValid = agTRUE;
#ifdef SM_INTERNAL_DEBUG
 smhexdump("smsatInquiryCB ID Dev data",(bit8 *)pSATAIdData, sizeof(agsaSATAIdentifyData_t));
 smhexdump("smsatInquiryCB Device ID Dev data",(bit8 *)&oneDeviceData->satIdentifyData, sizeof(agsaSATAIdentifyData_t));
#endif
// smhexdump("smsatInquiryCB Device ID Dev data",(bit8 *)&oneDeviceData->satIdentifyData, sizeof(agsaSATAIdentifyData_t));

 /* set oneDeviceData fields from IndentifyData */
 smsatSetDevInfo(oneDeviceData,pSATAIdData);

  allocationLen = ((scsiCmnd->cdb[3]) << 8) + scsiCmnd->cdb[4];
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength);

  /* SPC-4, spec 6.4 p 141 */
  /* EVPD bit == 0 */
  if (!(scsiCmnd->cdb[1] & SCSI_EVPD_MASK))
  {
    /* Returns the standard INQUIRY data */
    lenNeeded = STANDARD_INQUIRY_SIZE;


    smsatInquiryStandard(pInquiry, pSATAIdData, scsiCmnd);
    //smhexdump("smsatInquiryCB ***standard***", (bit8 *)pInquiry, 36);

  }
  else
  {
    /* EVPD bit != 0 && PAGE CODE != 0 */
    /* returns the pages of vital product data information */

    /* we must support page 00h, 83h and 89h */
    page = scsiCmnd->cdb[2];
    if ((page != INQUIRY_SUPPORTED_VPD_PAGE) &&
        (page != INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE) &&
        (page != INQUIRY_ATA_INFORMATION_VPD_PAGE) &&
        (page != INQUIRY_BLOCK_DEVICE_CHARACTERISTICS_VPD_PAGE))
    {
      smsatSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                            satOrgIOContext);

      tdsmIOCompletedCB( smRoot,
                         smOrgIORequest,
                         smIOSuccess,
                         SCSI_STAT_CHECK_CONDITION,
                         satOrgIOContext->pSmSenseData,
                         satOrgIOContext->interruptContext );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      SM_DBG1(("smsatInquiryCB: invalid PAGE CODE 0x%x!!!\n", page));
      return;
    }

    /* checking length */
    switch (page)
    {
    case INQUIRY_SUPPORTED_VPD_PAGE:
      lenNeeded = SATA_PAGE0_INQUIRY_SIZE; /* 9 */
      break;
    case INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE:
      if (oneDeviceData->satWWNSupport)
      {
        lenNeeded = SATA_PAGE83_INQUIRY_WWN_SIZE; /* 16 */
      }
      else
      {
        lenNeeded = SATA_PAGE83_INQUIRY_NO_WWN_SIZE; /* 76 */
      }
      break;
    case INQUIRY_ATA_INFORMATION_VPD_PAGE:
      lenNeeded = SATA_PAGE89_INQUIRY_SIZE; /* 572 */
      break;
    case INQUIRY_BLOCK_DEVICE_CHARACTERISTICS_VPD_PAGE:
      lenNeeded = SATA_PAGEB1_INQUIRY_SIZE; /* 64 */
      break;
    default:
      SM_DBG1(("smsatInquiryCB: wrong!!! invalid PAGE CODE 0x%x!!!\n", page));
      break;
    }


    /*
     * Fill in the Inquiry data depending on what Inquiry data we are returning.
     */
    switch (page)
    {
    case INQUIRY_SUPPORTED_VPD_PAGE:
      smsatInquiryPage0(pInquiry, pSATAIdData);
      break;
    case INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE:
      smsatInquiryPage83(pInquiry, pSATAIdData, oneDeviceData);
      break;
    case INQUIRY_ATA_INFORMATION_VPD_PAGE:
      smsatInquiryPage89(pInquiry, pSATAIdData, oneDeviceData, lenReceived);
      break;
    case INQUIRY_BLOCK_DEVICE_CHARACTERISTICS_VPD_PAGE:
      smsatInquiryPageB1(pInquiry, pSATAIdData);
      break;
    default:
      SM_DBG1(("smsatInquiryCB: wrong!!! invalidinvalid PAGE CODE 0x%x!!!\n", page));
      break;
    }
  } /* else */

  SM_DBG6(("smsatInquiryCB: calling tdsmIOCompletedCB\n"));

  /* if this is a standard Inquiry command, notify Stoport to set the device queue depth to max NCQ */
  if ( (oneDeviceData->satNCQ == agTRUE) &&
       ((scsiCmnd->cdb[1] & 0x01) == 0))
  {
    if (tdsmSetDeviceQueueDepth(smRoot,
                                smOrgIORequest,
                                oneDeviceData->satNCQMaxIO-1
                                ) == agFALSE)
    {
      SM_DBG1(("smsatInquiryCB: failed to call tdsmSetDeviceQueueDepth()!!! Q=%d\n", oneDeviceData->satNCQMaxIO));
    }
  }

  sm_memcpy(smOrgScsiRequest->sglVirtualAddr, dataBuffer, MIN(allocationLen, lenNeeded));
  if (allocationLen > lenNeeded)
  {
    SM_DBG6(("smsatInquiryCB reporting underrun lenNeeded=0x%x allocationLen=0x%x smIORequest=%p\n", 
        lenNeeded, allocationLen, smOrgIORequest));      

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOUnderRun,
                       allocationLen - lenNeeded,
                       agNULL,
                       satOrgIOContext->interruptContext );
  }
  else
  {
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext);
  }

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);
  SM_DBG5(("smsatInquiryCB: device %p pending IO %d\n", oneDeviceData, oneDeviceData->satPendingIO));
  SM_DBG6(("smsatInquiryCB: end\n"));
  return;
}

osGLOBAL void
smsatInquiryIntCB(
                   smRoot_t                  *smRoot,
                   smIORequest_t             *smIORequest,
                   smDeviceHandle_t          *smDeviceHandle,
                   smScsiInitiatorRequest_t  *smScsiRequest,
                   smSatIOContext_t            *satIOContext
                  )
{
  smScsiRspSense_t          *pSense;
  smIniScsiCmnd_t           *scsiCmnd;
//  satDeviceData_t           *satDevData;
  smDeviceData_t            *oneDeviceData;
  agsaSATAIdentifyData_t    *pSATAIdData;

  bit8                      *pInquiry;
  bit8                      page = 0xFF;
  bit32                     lenReceived = 0;
  bit32                     allocationLen = 0;
  bit32                     lenNeeded = 0;
  bit8                      dataBuffer[SATA_PAGE89_INQUIRY_SIZE] = {0};

  SM_DBG6(("smsatInquiryIntCB: start\n"));

  pSense      = satIOContext->pSense;
  scsiCmnd    = &smScsiRequest->scsiCmnd;
  pInquiry    = dataBuffer;
  oneDeviceData = satIOContext->pSatDevData;
  pSATAIdData = &oneDeviceData->satIdentifyData;

  allocationLen = ((scsiCmnd->cdb[3]) << 8) + scsiCmnd->cdb[4];
  allocationLen = MIN(allocationLen, scsiCmnd->expDataLength);

  /* SPC-4, spec 6.4 p 141 */
  /* EVPD bit == 0 */
  if (!(scsiCmnd->cdb[1] & SCSI_EVPD_MASK))
  {
    /* Returns the standard INQUIRY data */
    lenNeeded = STANDARD_INQUIRY_SIZE;

     smsatInquiryStandard(pInquiry, pSATAIdData, scsiCmnd);
    //smhexdump("satInquiryIntCB ***standard***", (bit8 *)pInquiry, 36);

  }
  else
  {
    /* EVPD bit != 0 && PAGE CODE != 0 */
    /* returns the pages of vital product data information */

    /* we must support page 00h, 83h and 89h */
    page = scsiCmnd->cdb[2];
    if ((page != INQUIRY_SUPPORTED_VPD_PAGE) &&
        (page != INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE) &&
        (page != INQUIRY_ATA_INFORMATION_VPD_PAGE) &&
        (page != INQUIRY_UNIT_SERIAL_NUMBER_VPD_PAGE) &&
        (page != INQUIRY_BLOCK_DEVICE_CHARACTERISTICS_VPD_PAGE))
    {
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

      SM_DBG1(("smsatInquiryIntCB: invalid PAGE CODE 0x%x!!!\n", page));
      return;
    }

    /* checking length */
    switch (page)
    {
    case INQUIRY_SUPPORTED_VPD_PAGE:
      lenNeeded = SATA_PAGE0_INQUIRY_SIZE; /* 36 */
      break;
    case INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE:
      if (oneDeviceData->satWWNSupport)
      {
        lenNeeded = SATA_PAGE83_INQUIRY_WWN_SIZE; /* 16 */
      }
      else
      {
        lenNeeded = SATA_PAGE83_INQUIRY_NO_WWN_SIZE; /* 76 */
      }
      break;
    case INQUIRY_ATA_INFORMATION_VPD_PAGE:
      lenNeeded = SATA_PAGE89_INQUIRY_SIZE; /* 572 */
      break;
    case INQUIRY_UNIT_SERIAL_NUMBER_VPD_PAGE:
      lenNeeded = SATA_PAGE80_INQUIRY_SIZE; /* 24 */
      break;
    case INQUIRY_BLOCK_DEVICE_CHARACTERISTICS_VPD_PAGE:
      lenNeeded = SATA_PAGEB1_INQUIRY_SIZE; /* 64 */
      break;
    default:
      SM_DBG1(("smsatInquiryIntCB: wrong!!! invalidinvalid PAGE CODE 0x%x!!!\n", page));
      break;
    }


    /*
     * Fill in the Inquiry data depending on what Inquiry data we are returning.
     */
    switch (page)
    {
    case INQUIRY_SUPPORTED_VPD_PAGE:
      smsatInquiryPage0(pInquiry, pSATAIdData);
      break;
    case INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE:
      smsatInquiryPage83(pInquiry, pSATAIdData, oneDeviceData);
      break;
    case INQUIRY_ATA_INFORMATION_VPD_PAGE:
      smsatInquiryPage89(pInquiry, pSATAIdData, oneDeviceData, lenReceived);
      break;
    case INQUIRY_UNIT_SERIAL_NUMBER_VPD_PAGE:
      smsatInquiryPage80(pInquiry, pSATAIdData);
      break;
    case INQUIRY_BLOCK_DEVICE_CHARACTERISTICS_VPD_PAGE:
      smsatInquiryPageB1(pInquiry, pSATAIdData);
      break;
    default:
      SM_DBG1(("smsatInquiryIntCB: wrong!!! invalidinvalid PAGE CODE 0x%x!!!\n", page));
      break;
    }
  } /* else */

  SM_DBG6(("smsatInquiryIntCB: calling tdsmIOCompletedCB\n"));

  /* if this is a standard Inquiry command, notify Stoport to set the device queue depth to max NCQ */
  if ( (oneDeviceData->satNCQ == agTRUE) &&
       ((scsiCmnd->cdb[1] & 0x01) == 0))
  {
    if (tdsmSetDeviceQueueDepth(smRoot,
                                smIORequest,
                                oneDeviceData->satNCQMaxIO-1
                                ) == agFALSE)
    {
      SM_DBG1(("smsatInquiryIntCB: failed to call tdsmSetDeviceQueueDepth()!!! Q=%d\n", oneDeviceData->satNCQMaxIO));
    }
  }

  sm_memcpy(smScsiRequest->sglVirtualAddr, dataBuffer, MIN(allocationLen, lenNeeded));
  if (allocationLen > lenNeeded)
  {
    SM_DBG6(("smsatInquiryIntCB reporting underrun lenNeeded=0x%x allocationLen=0x%x smIORequest=%p\n", 
        lenNeeded, allocationLen, smIORequest));      

    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOUnderRun,
                       allocationLen - lenNeeded,
                       agNULL,
                       satIOContext->interruptContext );
  }
  else
  {
    tdsmIOCompletedCB( smRoot,
                       smIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satIOContext->interruptContext);
  }

  SM_DBG5(("smsatInquiryIntCB: device %p pending IO %d\n", oneDeviceData, oneDeviceData->satPendingIO));
  SM_DBG6(("smsatInquiryIntCB: end\n"));
  return;

}

osGLOBAL void
smsatVerify10CB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext
               )
{
  smRoot_t                *smRoot = agNULL;
  smIntRoot_t             *smIntRoot = agNULL;
  smIntContext_t          *smAllShared = agNULL;
  smIORequestBody_t       *smIORequestBody;
  smIORequestBody_t       *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatInternalIo_t         *satIntIo;
  smDeviceData_t          *oneDeviceData;

  smScsiRspSense_t          *pSense;
  smIORequest_t             *smOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  SM_DBG5(("smsatVerify10CB: start\n"));
  SM_DBG5(("smsatVerify10CB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatVerify10CB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satIOContext->pSense;
  }
  else
  {
    SM_DBG4(("smsatVerify10CB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatVerify10CB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatVerify10CB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense        = satOrgIOContext->pSense;
  }

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     SM_DBG1(("smsatVerify10CB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
     smsatSetSensePayload( pSense,
                           SCSI_SNSKEY_NO_SENSE,
                           0,
                           SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                           satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                              satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
  if ( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
       ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
       )
  {
    /* for debugging */
    if( agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatVerify10CB: FAILED, NOT IO_SUCCESS!!!\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      SM_DBG1(("smsatVerify10CB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      SM_DBG1(("smsatVerify10CB: FAILED, FAILED, error status!!!\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;
    }

    /* for debugging */
    switch (hostToDevFis->h.command)
    {
    case SAT_READ_VERIFY_SECTORS_EXT:
      SM_DBG1(("smsatVerify10CB: SAT_READ_VERIFY_SECTORS_EXT!!!\n"));
      break;
    default:
      SM_DBG1(("smsatVerify10CB: error default case command 0x%x!!!\n", hostToDevFis->h.command));
      break;
    }

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  } /* end error checking */
  }

  /* process success from this point on */
  switch (hostToDevFis->h.command)
  {
  case SAT_READ_VERIFY_SECTORS_EXT:
    SM_DBG5(("smsatVerify10CB: SAT_WRITE_DMA_EXT success \n"));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext);
    break;
  default:
    SM_DBG1(("smsatVerify10CB: success but error default case command 0x%x!!!\n", hostToDevFis->h.command));

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest, /* == &satIntIo->satOrgSmIORequest */
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satOrgIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );

    break;
  }

  return;
}

osGLOBAL void
smsatReadLogExtCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   void              *agParam,
                   void              *ioContext
                 )
{
  smRoot_t                *smRoot = agNULL;
  smIntRoot_t             *smIntRoot = agNULL;
  smIntContext_t          *smAllShared = agNULL;
  smIORequestBody_t       *smIORequestBody;
  smSatIOContext_t          *satReadLogExtIOContext;
  smSatIOContext_t          *satIOContext;
  smSatInternalIo_t         *satIntIo;
  smDeviceData_t          *oneDeviceData;
  agsaIORequest_t         *agAbortIORequest;
  smIORequestBody_t       *smAbortIORequestBody;
  bit32                   PhysUpper32;
  bit32                   PhysLower32;
  bit32                   memAllocStatus;
  void                    *osMemHandle;
  smDeviceHandle_t        *smDeviceHandle;

  SM_DBG5(("smsatReadLogExtCB: start\n"));
  SM_DBG1(("smsatReadLogExtCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
    agIORequest, agIOStatus, agIOInfoLen));
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satReadLogExtIOContext = (smSatIOContext_t *) ioContext;
  satIntIo               = satReadLogExtIOContext->satIntIoContext;
  oneDeviceData          = satReadLogExtIOContext->pSatDevData;
  smDeviceHandle         = satReadLogExtIOContext->psmDeviceHandle;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  SM_DBG1(("smsatReadLogExtCB: did %d!!!\n", oneDeviceData->id));
  SM_DBG1(("smsatReadLogExtCB: smIORequestBody ID %d!!!\n", smIORequestBody->id));
  SM_DBG1(("smsatReadLogExtCB: smIORequestBody ioCompleted %d ioStarted %d\n", smIORequestBody->ioCompleted, smIORequestBody->ioStarted));
  smsatDecrementPendingIO(smRoot, smAllShared, satReadLogExtIOContext);

  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  /*
   * If READ LOG EXT failed, we issue device reset.
   */
  if ( agIOStatus != OSSA_IO_SUCCESS ||
       (agIOStatus == OSSA_IO_SUCCESS && agFirstDword != agNULL && agIOInfoLen != 0)
     )
  {
    SM_DBG1(("smsatReadLogExtCB: FAILED.!!!\n"));

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    /* Abort I/O after completion of device reset */
    oneDeviceData->satAbortAfterReset = agTRUE;
#ifdef NOT_YET
    /* needs to investigate this case */
    /* no report to OS layer */
    satSubTM(smRoot,
             satReadLogExtIOContext->ptiDeviceHandle,
             TD_INTERNAL_TM_RESET,
             agNULL,
             agNULL,
             agNULL,
             agFALSE);
#endif
    return;
  }


  /***************************************************************************
   * The following steps take place when READ LOG EXT successfully completed.
   ***************************************************************************/

  /************************************************************************
   *
   * 1. Issue abort to LL layer to all other pending I/Os for the same SATA
   *    drive.
   *
   * 2. Free resource allocated for the internally generated READ LOG EXT.
   *
   * 3. At the completion of abort, in the context of ossaSATACompleted(),
   *    return the I/O with error status to the OS-App Specific layer.
   *    When all I/O aborts are completed, clear SATA device flag to
   *    indicate ready to process new request.
   *
   ***********************************************************************/

  /*
   * Issue abort to LL layer to all other pending I/Os for the same SATA drive
   */
  /*
    replace the single IO abort with device abort
  */

  SM_DBG1(("smsatReadLogExtCB: issuing saSATAAbort. Device Abort!!!\n"));
  oneDeviceData->SMAbortAll = agTRUE;
  /*
  smAbortIORequestBody = smDequeueIO(smRoot);

  if (smAbortIORequestBody == agNULL)
  {
    SM_DBG1(("smsatReadLogExtCB: empty freeIOList!!!\n"));
    return;
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

  if (memAllocStatus != tiSuccess)
  {
    /* let os process IO */
    SM_DBG1(("smsatReadLogExtCB: ostiAllocMemory failed...\n"));
    return;
  }

  if (smAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    SM_DBG1(("smsatReadLogExtCB: ostiAllocMemory returned NULL smAbortIORequestBody\n"));
    return;
  }
  smIOReInit(smRoot, smAbortIORequestBody);
  /* setup task management structure */
  smAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  smAbortIORequestBody->smDevHandle = smDeviceHandle;
  /* setup task management structure */
//  smAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  satIOContext = &(smAbortIORequestBody->transport.SATA.satIOContext);
  satIOContext->smRequestBody = smAbortIORequestBody;

  /* initialize agIORequest */
  agAbortIORequest = &(smAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) smAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

  /*
   * Issue abort (device abort all)
   */
  saSATAAbort( agRoot, agAbortIORequest, tdsmRotateQnumber(smRoot, smDeviceHandle), oneDeviceData->agDevHandle, 1, agNULL, smaSATAAbortCB);

  /*
   * Free resource allocated for the internally generated READ LOG EXT.
   */
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);

  /*
   * Sequence of recovery continue at some other context:
   * At the completion of abort, in the context of ossaSATACompleted(),
   * return the I/O with error status to the OS-App Specific layer.
   * When all I/O aborts are completed, clear SATA device flag to
   * indicate ready to process new request.
   */

  oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;

  SM_DBG1(("smsatReadLogExtCB: end return!!!\n"));
  return;
}

osGLOBAL void
ossaSATAEvent(
               agsaRoot_t              *agRoot,
               agsaIORequest_t         *agIORequest,
               agsaPortContext_t       *agPortContext,
               agsaDevHandle_t         *agDevHandle,
               bit32                   event,
               bit32                   agIOInfoLen,
               void                    *agParam
         )
{
  smRoot_t                  *smRoot = gsmRoot;
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  smDeviceHandle_t          *smDeviceHandle = agNULL;
  smDeviceData_t            *oneDeviceData = agNULL;
  smList_t                  *DeviceListList;
  bit32                     found = agFALSE;
  smIORequestBody_t         *smIORequestBody = agNULL;
  smSatInternalIo_t           *satIntIo = agNULL;
  smSatIOContext_t            *satIOContext2;
  smIORequest_t             smIORequestTMP;
  bit32                     status;
#ifdef REMOVED
  agsaDifDetails_t          agDifDetails;
  bit8                      framePayload[256];
  bit16                     frameOffset = 0;
  bit16                     frameLen = 0;
#endif

  SM_DBG1(("ossaSATAEvent: start\n"));
  if (event == OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE)
  {
    /* agIORequest is invalid, search for smDeviceHandle from smAllShared using agDevHandle */
    /* find a device's existence */
    DeviceListList = smAllShared->MainDeviceList.flink;
    while (DeviceListList != &(smAllShared->MainDeviceList))
    {
      oneDeviceData = SMLIST_OBJECT_BASE(smDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        SM_DBG1(("ossaSATAEvent: oneDeviceData is NULL!!!\n"));
        return;
      }
      if (oneDeviceData->agDevHandle == agDevHandle)
      {
        SM_DBG2(("ossaSATAEvent: did %d\n", oneDeviceData->id));
        found = agTRUE;
        break;
      }
      DeviceListList = DeviceListList->flink;
    }
    if (found == agFALSE)
    {
      SM_DBG2(("ossaSATAEvent: not found!!!\n"));
      return;
    }
    if (oneDeviceData->valid == agFALSE)
    {
      SM_DBG2(("ossaSATAEvent: oneDeviceData is not valid did %d!!!\n", oneDeviceData->id));
      return;
    }
    /**************************************************************************
     *
     * !!!! See Section 13.5.2.4 of SATA 2.5 specs.                       !!!!
     * !!!! If the NCQ error ends up here, it means that the device sent  !!!!
     * !!!! Register Device To Host FIS (which does not have SActive      !!!!
     * !!!! register) instead of Set Device Bit FIS (which has SActive    !!!!
     * !!!! register). The routine osSatIOCompleted() deals with the case !!!!
     * !!!! where Set Device Bit FIS was sent by the device.              !!!!
     *
     * For NCQ we need to issue READ LOG EXT command with log page 10h
     * to get the error and to allow other I/Os to continue.
     *
     * Here is the basic flow or sequence of error recovery, this sequence is
     * similar to the one described in SATA 2.5:
     *
     * 1. Set SATA device flag to indicate error condition and returning busy
     *    for all new request.
     *
     * 2. Prepare READ LOG EXT page 10h command. Set flag to indicate that
     *    the failed I/O has NOT been returned to the OS Layer. Send command.
     *
     * 3. When the device receives READ LOG EXT page 10h request all other
     *    pending I/O are implicitly aborted. No completion (aborted) status
     *    will be sent to the host for these aborted commands.
     *
     * 4. SATL receives the completion for READ LOG EXT command in
     *    smsatReadLogExtCB(). Steps 5,6,7,8 below are the step 1,2,3,4 in
     *    smsatReadLogExtCB().
     *
     * 5. Check flag that indicates whether the failed I/O has been returned
     *    to the OS Layer. If not, search the I/O context in device data
     *    looking for a matched tag. Then return the completion of the failed
     *    NCQ command with the appopriate/trasnlated SCSI status.
     *
     * 6. Issue abort to LL layer to all other pending I/Os for the same SATA
     *    drive.
     *
     * 7. Free resource allocated for the internally generated READ LOG EXT.
     *
     * 8. At the completion of abort, in the context of ossaSATACompleted(),
     *    return the I/O with error status to the OS-App Specific layer.
     *    When all I/O aborts are completed, clear SATA device flag to
     *    indicate ready to process new request.
     *
     *************************************************************************/

    smDeviceHandle = oneDeviceData->smDevHandle;
    SM_DBG1(("ossaSATAEvent: did %d!!!\n", oneDeviceData->id));

    if (oneDeviceData->satDriveState == SAT_DEV_STATE_NORMAL)
    {
      SM_DBG1(("ossaSATAEvent: NCQ ERROR did %d!!!\n", oneDeviceData->id ));

      /* Set flag to indicate we are in recovery */
      oneDeviceData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;

      /*
       * Allocate resource for READ LOG EXIT page 10h
       */
      satIntIo = smsatAllocIntIoResource( smRoot,
                                          &(smIORequestTMP), /* anything but NULL */
                                          oneDeviceData,
                                          sizeof (satReadLogExtPage10h_t),
                                          satIntIo);

      /*
       * If we cannot allocate resource to do the normal NCQ recovery, we
       * will do SATA device reset.
       */
      if (satIntIo == agNULL)
      {
        /* Abort I/O after completion of device reset */
        oneDeviceData->satAbortAfterReset = agTRUE;
        SM_DBG1(("ossaSATAEvent: can't send RLE due to resource lack!!!\n"));

#ifdef NOT_YET
        /* needs to investigate this case */
        /* no report to OS layer */
        smsatSubTM(smRoot,
                   smDeviceHandle,
                   TD_INTERNAL_TM_RESET,
                   agNULL,
                   agNULL,
                   agNULL,
                   agFALSE);
#endif

        return;
      }


      /*
       * Clear flag to indicate that the failed I/O has NOT been returned to the
       * OS-App specific Layer.
       */
      satIntIo->satIntFlag = 0;

      /* compare to satPrepareNewIO() */
      /* Send READ LOG EXIT page 10h command */

      /*
       * Need to initialize all the fields within satIOContext except
       * reqType and satCompleteCB which will be set depending on cmd.
       */

      smIORequestBody = (smIORequestBody_t *)satIntIo->satIntRequestBody;
      satIOContext2 = &(smIORequestBody->transport.SATA.satIOContext);

      satIOContext2->pSatDevData   = oneDeviceData;
      satIOContext2->pFis          = &(smIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
      satIOContext2->pScsiCmnd     = &(satIntIo->satIntSmScsiXchg.scsiCmnd);
      satIOContext2->pSense        = &(smIORequestBody->transport.SATA.sensePayload);
      satIOContext2->pSmSenseData  = &(smIORequestBody->transport.SATA.smSenseData);
      satIOContext2->pSmSenseData->senseData = satIOContext2->pSense;

      satIOContext2->smRequestBody = satIntIo->satIntRequestBody;
      //not used
//      satIOContext2->interruptContext = interruptContext;
      satIOContext2->satIntIoContext  = satIntIo;

      satIOContext2->psmDeviceHandle = smDeviceHandle;
      satIOContext2->satOrgIOContext = agNULL;
      satIOContext2->smScsiXchg = agNULL;

      SM_DBG1(("ossaSATAEvent: smIORequestBody ID %d!!!\n", smIORequestBody->id));
      SM_DBG1(("ossaSATAEvent: smIORequestBody ioCompleted %d ioStarted %d\n", smIORequestBody->ioCompleted, smIORequestBody->ioStarted));
      status = smsatSendReadLogExt( smRoot,
                                    &satIntIo->satIntSmIORequest,
                                    smDeviceHandle,
                                    &satIntIo->satIntSmScsiXchg,
                                    satIOContext2);

      if (status != SM_RC_SUCCESS)
      {
        SM_DBG1(("ossaSATAEvent: can't send RLE due to LL api failure!!!\n"));
        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo);
        /* Abort I/O after completion of device reset */
        oneDeviceData->satAbortAfterReset = agTRUE;
#ifdef NOT_YET
        /* needs to investigate this case */
        /* no report to OS layer */
        smsatSubTM(smRoot,
                   smDeviceHandle,
                   TD_INTERNAL_TM_RESET,
                   agNULL,
                   agNULL,
                   agNULL,
                   agFALSE);
#endif

        return;
      }
    }
    else
    {
      SM_DBG1(("ossaSATAEvent: NCQ ERROR but recovery in progress!!!\n"));
    }
  }
  else if (event == OSSA_IO_XFER_CMD_FRAME_ISSUED)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_XFER_CMD_FRAME_ISSUED\n"));
  }
  else if (event == OSSA_IO_XFER_PIO_SETUP_ERROR)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_XFER_PIO_SETUP_ERROR\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED\n"));
  }
  else if (event == OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH\n"));
  }
#ifdef REMOVED
  else if (event == OSSA_IO_XFR_ERROR_DIF_MISMATCH || event == OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH ||
           event == OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH || event == OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH )
  {
    SM_DBG1(("ossaSATAEvent: DIF related, event 0x%x\n", event));
    /* process DIF detail information */
    SM_DBG2(("ossaSATAEvent: agIOInfoLen %d\n", agIOInfoLen));
    if (agParam == agNULL)
    {
      SM_DBG2(("ossaSATAEvent: agParam is NULL!!!\n"));
      return;
    }
    if (agIOInfoLen < sizeof(agsaDifDetails_t))
    {
      SM_DBG2(("ossaSATAEvent: wrong agIOInfoLen!!! agIOInfoLen %d sizeof(agsaDifDetails_t) %d\n", agIOInfoLen, (int)sizeof(agsaDifDetails_t)));
      return;
    }
    /* reads agsaDifDetails_t */
    saFrameReadBlock(agRoot, agParam, 0, &agDifDetails, sizeof(agsaDifDetails_t));
    frameOffset = (agDifDetails.ErrBoffsetEDataLen & 0xFFFF);
    frameLen = (agDifDetails.ErrBoffsetEDataLen & 0xFFFF0000) >> 16;

    SM_DBG2(("ossaSATAEvent: UpperLBA 0x%08x LowerLBA 0x%08x\n", agDifDetails.UpperLBA, agDifDetails.LowerLBA));
    SM_DBG2(("ossaSATAEvent: SASAddrHI 0x%08x SASAddrLO 0x%08x\n",
             SM_GET_SAS_ADDRESSHI(agDifDetails.sasAddressHi), SM_GET_SAS_ADDRESSLO(agDifDetails.sasAddressLo)));
    SM_DBG2(("ossaSATAEvent: DIF error mask 0x%x Device ID 0x%x\n",
             (agDifDetails.DIFErrDevID) & 0xFF, (agDifDetails.DIFErrDevID & 0xFFFF0000) >> 16));
    if (frameLen != 0 && frameLen <= 256)
    {
      saFrameReadBlock(agRoot, agParam, sizeof(agsaDifDetails_t), framePayload, frameLen);
      smhexdump("ossaSATAEvent frame", framePayload, frameLen);
    }
  }
#endif
  else if (event == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY)
  {
    smIORequestBody = (smIORequestBody_t *)agIORequest->osData;
    if (smIORequestBody == agNULL)
    {
      SM_DBG1(("ossaSATAEvent: smIORequestBody is NULL!!!\n"));
      return;
    }
    smDeviceHandle = smIORequestBody->smDevHandle;
    if (smDeviceHandle == agNULL)
    {
      SM_DBG1(("ossaSATAEvent: smDeviceHandle is NULL!!!\n"));
      return;
    }
    oneDeviceData  = (smDeviceData_t *)smDeviceHandle->smData;
    if (oneDeviceData == agNULL)
    {
      SM_DBG1(("ossaSATAEvent: oneDeviceData is NULL!!!\n"));
      return;
    }
    SM_DBG1(("ossaSATAEvent: ERROR event %d did=%d\n", event, oneDeviceData->id));


    if (smAllShared->FCA)
    {
      if (oneDeviceData->SMNumOfFCA <= 0) /* does SMP HARD RESET only upto one time */
      {
        SM_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; sending HARD_RESET\n"));
        oneDeviceData->SMNumOfFCA++;
        smPhyControlSend(smRoot,
                         oneDeviceData,
                         SMP_PHY_CONTROL_HARD_RESET,
                         agNULL,
                         tdsmRotateQnumber(smRoot, smDeviceHandle)
                        );
      }
      else
      {
        /* given up after one time of SMP HARD RESET; */
        SM_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; but giving up sending HARD_RESET!!!\n"));
      }
    }
  }
  else if (event == OSSA_IO_XFER_ERROR_NAK_RECEIVED)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_XFER_ERROR_NAK_RECEIVED\n"));
  }
  else if (event == OSSA_IO_XFER_ERROR_DMA_ACTIVATE_TIMEOUT)
  {
    SM_DBG1(("ossaSATAEvent: OSSA_IO_XFER_ERROR_DMA_ACTIVATE_TIMEOUT\n"));
  }
  else
  {
    SM_DBG1(("ossaSATAEvent: other event 0x%x\n", event));
  }

  return;
}

osGLOBAL void
smSMPCompletedCB(
                  agsaRoot_t            *agRoot,
                  agsaIORequest_t       *agIORequest,
                  bit32                 agIOStatus,
                  bit32                 agIOInfoLen,
                  agsaFrameHandle_t     agFrameHandle
                )
{
  smSMPRequestBody_t *smSMPRequestBody = (smSMPRequestBody_t *) agIORequest->osData;

  SM_DBG2(("smSMPCompletedCB: start\n"));

  if (smSMPRequestBody == agNULL)
  {
    SM_DBG1(("smSMPCompletedCB: smSMPRequestBody is NULL!!!\n"));
    return;
  }

  if (smSMPRequestBody->SMPCompletionFunc == agNULL)
  {
    SM_DBG1(("smSMPCompletedCB: smSMPRequestBody->SMPCompletionFunc is NULL!!!\n"));
    return;
  }

  /* calling smSMPCompleted */
  smSMPRequestBody->SMPCompletionFunc(
                                       agRoot,
                                       agIORequest,
                                       agIOStatus,
                                       agIOInfoLen,
                                       agFrameHandle
                                     );
  return;
}

osGLOBAL void
smSMPCompleted(
                agsaRoot_t            *agRoot,
                agsaIORequest_t       *agIORequest,
                bit32                 agIOStatus,
                bit32                 agIOInfoLen,
                agsaFrameHandle_t     agFrameHandle
              )
{
  smRoot_t           *smRoot = gsmRoot;
  smSMPRequestBody_t *smSMPRequestBody = (smSMPRequestBody_t *) agIORequest->osData;
  smDeviceData_t     *oneDeviceData;
  smDeviceHandle_t   *smDeviceHandle;
  smIORequest_t      *CurrentTaskTag;
  bit8                smpHeader[4];
  smSMPFrameHeader_t *smSMPFrameHeader;
  agsaDevHandle_t    *agDevHandle = agNULL;

  SM_DBG2(("smSMPCompleted: start\n"));

  if (smSMPRequestBody == agNULL)
  {
    SM_DBG1(("smSMPCompleted: smSMPRequestBody is NULL, wrong!!!\n"));
    return;
  }

  CurrentTaskTag  = smSMPRequestBody->CurrentTaskTag;
  oneDeviceData = smSMPRequestBody->smDeviceData;
  smDeviceHandle = smSMPRequestBody->smDevHandle;
  if (smDeviceHandle == agNULL)
  {
    SM_DBG2(("smSMPCompleted: smDeviceHandle is NULL, wrong!!!\n"));
    return;
  }

  if (oneDeviceData == agNULL)
  {
    SM_DBG2(("smSMPCompleted: oneDeviceData is NULL, wrong!!!\n"));
    return;
  }
  agDevHandle = oneDeviceData->agExpDevHandle;
  if (agIOStatus == OSSA_IO_SUCCESS)
  {
    saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
    smSMPFrameHeader = (smSMPFrameHeader_t *)smpHeader;
    if (smSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
    {
      SM_DBG3(("smSMPCompleted: phy control\n"));
      if (agIOInfoLen != 4 &&
          smSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED) /*zero length is expected */
      {
        SM_DBG1(("smSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x!!!\n", agIOInfoLen, 4));
        tdsmFreeMemory(
                       smRoot,
                       smSMPRequestBody->osMemHandle,
                       sizeof(smSMPRequestBody_t)
                      );
        if (CurrentTaskTag != agNULL)
        {
          tdsmEventCB(smRoot,
                      smDeviceHandle,
                      smIntrEventTypeTaskManagement,
                      smTMFailed,
                      CurrentTaskTag);
        }

        return;
      }
      smPhyControlRespRcvd(smRoot,
                           agRoot,
                           agIORequest,
                           oneDeviceData,
                           smSMPFrameHeader,
                           agFrameHandle,
                           CurrentTaskTag
                           );
    }
    else
    {
      /* unknown SMP function */
      SM_DBG2(("smSMPCompleted: unknown smSMPFrameHeader %d!!!\n", smSMPFrameHeader->smpFunction));
      tdsmFreeMemory(
                      smRoot,
                      smSMPRequestBody->osMemHandle,
                      sizeof(smSMPRequestBody_t)
                     );
      if (CurrentTaskTag != agNULL)
      {
        tdsmEventCB(smRoot,
                    smDeviceHandle,
                    smIntrEventTypeTaskManagement,
                    smTMFailed,
                    CurrentTaskTag);
      }
      return;
    }
  }
  else
  {
    SM_DBG2(("smSMPCompleted: failed agIOStatus %d!!!\n", agIOStatus));
    if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE ||
        agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED ||
        agIOStatus == OSSA_IO_DS_NON_OPERATIONAL
       )
    {
      SM_DBG1(("smSMPCompleted: setting back to operational\n"));
      if (agDevHandle != agNULL)
      {
        saSetDeviceState(agRoot, agNULL, tdsmRotateQnumber(smRoot, smDeviceHandle), agDevHandle, SA_DS_OPERATIONAL);
      }
      else
      {
        SM_DBG1(("smSMPCompleted: agDevHandle is NULL\n"));
      }
    }
    tdsmFreeMemory(
                    smRoot,
                    smSMPRequestBody->osMemHandle,
                    sizeof(smSMPRequestBody_t)
                  );
    if (CurrentTaskTag != agNULL)
    {
      tdsmEventCB(smRoot,
                  smDeviceHandle,
                  smIntrEventTypeTaskManagement,
                  smTMFailed,
                  CurrentTaskTag);
    }
    return;
  }

  tdsmFreeMemory(
                  smRoot,
                  smSMPRequestBody->osMemHandle,
                  sizeof(smSMPRequestBody_t)
                );
  return;
}

osGLOBAL void
smPhyControlRespRcvd(
                      smRoot_t              *smRoot,
                      agsaRoot_t            *agRoot,
                      agsaIORequest_t       *agIORequest,
                      smDeviceData_t        *oneDeviceData, /* sata disk */
                      smSMPFrameHeader_t    *frameHeader,
                      agsaFrameHandle_t     frameHandle,
                      smIORequest_t         *CurrentTaskTag
                     )
{
  smDeviceData_t        *TargetDeviceData = agNULL;
  agsaDevHandle_t       *agDevHandle = agNULL;
  smSMPRequestBody_t    *smSMPRequestBody;
  smDeviceHandle_t      *smDeviceHandle;

  SM_DBG2(("smPhyControlRespRcvd: start\n"));

  if (CurrentTaskTag == agNULL )
  {
    SM_DBG1(("smPhyControlRespRcvd: CurrentTaskTag is NULL; allowed\n"));
    return;
  }

  smSMPRequestBody = (smSMPRequestBody_t *)CurrentTaskTag->smData;
  if (smSMPRequestBody == agNULL)
  {
    SM_DBG1(("smPhyControlRespRcvd: smSMPRequestBody is NULL!!!\n"));
    return;
  }

  smDeviceHandle = smSMPRequestBody->smDevHandle;
  if (smDeviceHandle == agNULL)
  {
    SM_DBG2(("smPhyControlRespRcvd: smDeviceHandle is NULL!!!\n"));
    return;
  }

  TargetDeviceData = smSMPRequestBody->smDeviceData;
  if (oneDeviceData != TargetDeviceData)
  {
    SM_DBG1(("smPhyControlRespRcvd: oneDeviceData != TargetDeviceData!!!\n"));
    return;
  }

  agDevHandle = TargetDeviceData->agDevHandle;


  if (frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    SM_DBG2(("smPhyControlRespRcvd: SMP success\n"));
    SM_DBG1(("smPhyControlRespRcvd: callback to TD layer with success\n"));
    TargetDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
    saSetDeviceState(agRoot, agNULL, tdsmRotateQnumber(smRoot, smDeviceHandle), agDevHandle, SA_DS_OPERATIONAL);

    tdsmEventCB(smRoot,
                smDeviceHandle,
                smIntrEventTypeTaskManagement,
                smTMOK,
                CurrentTaskTag);
  }
  else
  {
    SM_DBG1(("smPhyControlRespRcvd: SMP failure; result %d!!!\n", frameHeader->smpFunctionResult));
    tdsmEventCB(smRoot,
                smDeviceHandle,
                smIntrEventTypeTaskManagement,
                smTMFailed,
                CurrentTaskTag);
  }
  return;
}

osGLOBAL void
smsatCheckPowerModeCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                     )
{
  /* callback for satDeResetDevice */
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL;
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatInternalIo_t        *satIntIo;
//  satDeviceData_t         *satDevData;
  smDeviceData_t           *oneDeviceData;
#ifdef  TD_DEBUG_ENABLE
  bit32                     ataStatus = 0;
  bit32                     ataError;
  agsaFisPioSetupHeader_t  *satPIOSetupHeader = agNULL;
#endif
  bit32                     AbortTM = agFALSE;
  smDeviceHandle_t         *smDeviceHandle;

  SM_DBG1(("smsatCheckPowerModeCB: start\n"));
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  smDeviceHandle         = oneDeviceData->smDevHandle;
  if (satIntIo == agNULL)
  {
    SM_DBG6(("smsatCheckPowerModeCB: External, OS generated\n"));
    satOrgIOContext      = satIOContext;
  }
  else
  {
    SM_DBG6(("smsatCheckPowerModeCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG6(("smsatCheckPowerModeCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG6(("smsatCheckPowerModeCB: satOrgIOContext is NOT NULL\n"));
    }
  }
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatCheckPowerModeCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);

    oneDeviceData->satTmTaskTag = agNULL;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BREAK ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY
      )
  {
    SM_DBG1(("smsatCheckPowerModeCB: OSSA_IO_OPEN_CNX_ERROR!!!\n"));
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);
    oneDeviceData->satTmTaskTag = agNULL;

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
 if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisPioSetup_t is expected */
#ifdef  TD_DEBUG_ENABLE
    satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
    ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
    ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
    SM_DBG1(("smsatCheckPowerModeCB: ataStatus 0x%x ataError 0x%x!!!\n", ataStatus, ataError));
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);
    oneDeviceData->satTmTaskTag = agNULL;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  /* success */
  SM_DBG1(("smsatCheckPowerModeCB: success!!!\n"));
  SM_DBG1(("smsatCheckPowerModeCB: TMF %d!!!\n", satOrgIOContext->TMF));

  if (satOrgIOContext->TMF == AG_ABORT_TASK)
  {
    AbortTM = agTRUE;
  }
  if (AbortTM == agTRUE)
  {
    SM_DBG1(("smsatCheckPowerModeCB: calling local satAbort!!!\n"));
    smsatAbort(smRoot, agRoot, satOrgIOContext->satToBeAbortedIOContext);
  }
  oneDeviceData->satTmTaskTag = agNULL;
  oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  SM_DBG1(("smsatCheckPowerModeCB: satPendingIO %d satNCQMaxIO %d!!!\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
  SM_DBG1(("smsatCheckPowerModeCB: satPendingNCQIO %d satPendingNONNCQIO %d!!!\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);

  /* TM completed */
  tdsmEventCB( smRoot,
               smDeviceHandle,
               smIntrEventTypeTaskManagement,
               smTMOK,
               oneDeviceData->satTmTaskTag);
  SM_DBG5(("smsatCheckPowerModeCB: device %p pending IO %d\n", oneDeviceData, oneDeviceData->satPendingIO));
  SM_DBG2(("smsatCheckPowerModeCB: end\n"));
  return;
}

osGLOBAL void 
smsatCheckPowerModePassCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                     )

{
  
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL; 
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatInternalIo_t        *satIntIo;
  smIORequest_t             *smOrgIORequest;
  smIORequestBody_t         *smOrgIORequestBody;
//  satDeviceData_t         *satDevData;
  smDeviceData_t           *oneDeviceData;
#ifdef  TD_DEBUG_ENABLE
  bit32                     ataStatus = 0;
  bit32                     ataError;
  agsaFisPioSetupHeader_t  *satPIOSetupHeader = agNULL;
#endif
 
  smScsiRspSense_t			*pSense;
  bit8						bSenseKey = 0;
  bit16 					bSenseCodeInfo = 0;

  SM_DBG1(("smsatCheckPowerModePassCB: start\n"));
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;  
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG6(("smsatCheckPowerModePassCB: External, OS generated\n"));
    satOrgIOContext      = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
  }
  else
  {
    SM_DBG6(("smsatCheckPowerModePassCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG6(("smsatCheckPowerModePassCB: satOrgIOContext is NULL, wrong\n"));
      return;      
    }
    else
    {
      SM_DBG6(("smsatCheckPowerModePassCB: satOrgIOContext is NOT NULL\n"));
    }
  }  
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatCheckPowerModePassCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
  
    tdsmIOCompletedCB(
                       smRoot, 
                       smOrgIORequest,
                       smIOFailed, 
                       smDetailOtherError,
                       agNULL, 
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;

  }
  
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisPioSetup_t is expected */
#ifdef  TD_DEBUG_ENABLE
    satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
    ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
    ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
    SM_DBG1(("smsatCheckPowerModePassCB: ataStatus 0x%x ataError 0x%x!!!\n", ataStatus, ataError));
   

    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );
      
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
			      oneDeviceData,
			      satIntIo); 
      return;
    }
    smsatTranslateATAErrorsToSCSIErrors(
                                        agFirstDword->D2H.status,
                                        agFirstDword->D2H.error,
                                        &bSenseKey,
                                        &bSenseCodeInfo
                                        );
    smsatSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
    tdsmIOCompletedCB(smRoot,
                      smOrgIORequest,
                      smIOSuccess,
                      SCSI_STAT_CHECK_CONDITION, 
                      satOrgIOContext->pSmSenseData,
                      satOrgIOContext->interruptContext );
	
	
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
	
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;

  }
  /* success */
  SM_DBG1(("smsatCheckPowerModePassCB: success!!!\n"));
  
  tdsmIOCompletedCB( smRoot,
                     smOrgIORequest,
                     smIOSuccess,
                     SCSI_STAT_GOOD,
                     agNULL,
                     satOrgIOContext->interruptContext);
			  

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
 
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);
                            

  return;
}

osGLOBAL void 
smsatIDDataPassCB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  agsaFrameHandle_t agFrameHandle,
                  void              *ioContext
                 )
{
  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL; 
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  smSatInternalIo_t        *satIntIo;
  smIORequest_t             *smOrgIORequest;
  smIORequestBody_t         *smOrgIORequestBody;
//  satDeviceData_t         *satDevData;
  smDeviceData_t           *oneDeviceData;
#ifdef  TD_DEBUG_ENABLE
  bit32                     ataStatus = 0;
  bit32                     ataError;
  agsaFisPioSetupHeader_t  *satPIOSetupHeader = agNULL;
#endif
 
  smScsiRspSense_t			*pSense;
  bit8						bSenseKey = 0;
  bit16 					bSenseCodeInfo = 0;

  SM_DBG3(("smsatIDDataPassCB: start\n"));
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;  
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

  if (satIntIo == agNULL)
  {
    SM_DBG6(("smsatIDDataPassCB: External, OS generated\n"));
    satOrgIOContext      = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
  }
  else
  {
    SM_DBG6(("smsatIDDataPassCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG6(("smsatIDDataPassCB: satOrgIOContext is NULL, wrong\n"));
      return;      
    }
    else
    {
      SM_DBG6(("smsatIDDataPassCB: satOrgIOContext is NOT NULL\n"));
    }
  }  
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatIDDataPassCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
  
    tdsmIOCompletedCB(
                       smRoot, 
                       smOrgIORequest,
                       smIOFailed, 
                       smDetailOtherError,
                       agNULL, 
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;

  }
  
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisPioSetup_t is expected */
#ifdef  TD_DEBUG_ENABLE
    satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
    ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
    ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
    SM_DBG1(("smsatIDDataPassCB: ataStatus 0x%x ataError 0x%x!!!\n", ataStatus, ataError));
   

    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                        );
      
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
			      oneDeviceData,
			      satIntIo); 
      return;
    }
    smsatTranslateATAErrorsToSCSIErrors(
                                        agFirstDword->D2H.status,
                                        agFirstDword->D2H.error,
                                        &bSenseKey,
                                        &bSenseCodeInfo
                                        );
    smsatSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
    tdsmIOCompletedCB(smRoot,
                      smOrgIORequest,
                      smIOSuccess,
                      SCSI_STAT_CHECK_CONDITION, 
                      satOrgIOContext->pSmSenseData,
                      satOrgIOContext->interruptContext );
	
	
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
	
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;

  }
  /* success */
  SM_DBG3(("smsatIDDataPassCB: success!!!\n"));
  
  SM_DBG3(("smsatIDDataPassCB: extend 0x%x ck_cond 0x%x sectorCnt07 0x%x\n", satOrgIOContext->extend, 
  satIOContext->ck_cond, satOrgIOContext->sectorCnt07));
  SM_DBG3(("smsatIDDataPassCB: LBAHigh07 0x%x LBAMid07 0x%x LBALow07 0x%x\n", satOrgIOContext->LBAHigh07, 
  satOrgIOContext->LBAMid07, satOrgIOContext->LBALow07));
  
  if (satIOContext->ck_cond) 
  {  
    smsatSetSensePayload( pSense,
                          SCSI_SNSKEY_RECOVERED_ERROR,
                          satOrgIOContext->sectorCnt07,
                          SCSI_SNSCODE_ATA_PASS_THROUGH_INFORMATION_AVAILABLE,
                          satIOContext);

    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_CHECK_CONDITION,
                       satIOContext->pSmSenseData,
                       satOrgIOContext->interruptContext );
  }
  else
  {  			
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOSuccess,
                       SCSI_STAT_GOOD,
                       agNULL,
                       satOrgIOContext->interruptContext);
  }			  

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
 
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);
                            

  return;
}

osGLOBAL void
smsatResetDeviceCB(
                    agsaRoot_t        *agRoot,
                    agsaIORequest_t   *agIORequest,
                    bit32             agIOStatus,
                    agsaFisHeader_t   *agFirstDword,
                    bit32             agIOInfoLen,
                    agsaFrameHandle_t agFrameHandle,
                    void              *ioContext
                  )
{
  /* callback for satResetDevice */
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                  *smRoot = agNULL;
  smIntRoot_t               *smIntRoot = agNULL;
  smIntContext_t            *smAllShared = agNULL;
  smIORequestBody_t         *smIORequestBody;
  smIORequestBody_t         *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatIOContext_t          *satNewIOContext;
  smSatInternalIo_t         *satIntIo;
  smSatInternalIo_t         *satNewIntIo = agNULL;
//  satDeviceData_t         *satDevData;
  smDeviceData_t            *oneDeviceData;
  smIORequest_t             *smOrgIORequest;
#ifdef  TD_DEBUG_ENABLE
  bit32                      ataStatus = 0;
  bit32                      ataError;
  agsaFisPioSetupHeader_t   *satPIOSetupHeader = agNULL;
#endif
  bit32                      status;
  smDeviceHandle_t          *smDeviceHandle;

  SM_DBG1(("smsatResetDeviceCB: start\n"));
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  smDeviceHandle         = oneDeviceData->smDevHandle;

  if (satIntIo == agNULL)
  {
    SM_DBG6(("smsatResetDeviceCB: External, OS generated\n"));
    satOrgIOContext      = satIOContext;
    smOrgIORequest       = smIORequestBody->smIORequest;
  }
  else
  {
    SM_DBG6(("smsatResetDeviceCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG6(("smsatResetDeviceCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG6(("smsatResetDeviceCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
  }
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatResetDeviceCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);
    oneDeviceData->satTmTaskTag = agNULL;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BREAK ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY
      )
  {
    SM_DBG1(("smsatResetDeviceCB: OSSA_IO_OPEN_CNX_ERROR!!!\n"));
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);

    oneDeviceData->satTmTaskTag = agNULL;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisPioSetup_t is expected */
#ifdef  TD_DEBUG_ENABLE
    satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
    ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
    ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
    SM_DBG1(("smsatResetDeviceCB: ataStatus 0x%x ataError 0x%x!!!\n", ataStatus, ataError));
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);

    oneDeviceData->satTmTaskTag = agNULL;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  /* success */
  satNewIntIo = smsatAllocIntIoResource( smRoot,
                                         smOrgIORequest,
                                         oneDeviceData,
                                         0,
                                         satNewIntIo);
  if (satNewIntIo == agNULL)
  {
    oneDeviceData->satTmTaskTag = agNULL;

    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    /* memory allocation failure */
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);
    SM_DBG1(("smsatResetDeviceCB: momory allocation fails!!!\n"));
    return;
  } /* end of memory allocation failure */
    /*
     * Need to initialize all the fields within satIOContext
     */
    satNewIOContext = smsatPrepareNewIO(
                                         satNewIntIo,
                                         smOrgIORequest,
                                         oneDeviceData,
                                         agNULL,
                                         satOrgIOContext
                                        );
    /* send AGSA_SATA_PROTOCOL_SRST_DEASSERT */
    status = smsatDeResetDevice(smRoot,
                                smOrgIORequest,
                                satOrgIOContext->psmDeviceHandle,
                                agNULL,
                                satNewIOContext
                               );
    if (status != SM_RC_SUCCESS)
    {
      /* TM completed */
      tdsmEventCB( smRoot,
                   smDeviceHandle,
                   smIntrEventTypeTaskManagement,
                   smTMFailed,
                   oneDeviceData->satTmTaskTag);
      /* sending AGSA_SATA_PROTOCOL_SRST_DEASSERT fails */
      oneDeviceData->satTmTaskTag = agNULL;

      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satNewIntIo);
      return;
    }
//  oneDeviceData->satTmTaskTag = agNULL;

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);
  SM_DBG5(("smsatResetDeviceCB: device %p pending IO %d\n", oneDeviceData, oneDeviceData->satPendingIO));
  SM_DBG6(("smsatResetDeviceCB: end\n"));
  return;
}

osGLOBAL void
smsatDeResetDeviceCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                   )
{
  /* callback for satDeResetDevice */
//  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
//  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
//  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
//  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smRoot_t                  *smRoot = agNULL;
  smIntRoot_t               *smIntRoot = agNULL;
  smIntContext_t            *smAllShared = agNULL;
  smIORequestBody_t         *smIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatInternalIo_t         *satIntIo;
//  satDeviceData_t           *satDevData;
  smDeviceData_t            *oneDeviceData;
#ifdef  TD_DEBUG_ENABLE
  bit32                      ataStatus = 0;
  bit32                      ataError;
  agsaFisPioSetupHeader_t   *satPIOSetupHeader = agNULL;
#endif
  bit32                      AbortTM = agFALSE;
  smDeviceHandle_t          *smDeviceHandle;

  SM_DBG1(("smsatDeResetDeviceCB: start!!!\n"));
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  smDeviceHandle         = oneDeviceData->smDevHandle;
  if (satIntIo == agNULL)
  {
    SM_DBG6(("smsatDeResetDeviceCB: External, OS generated\n"));
    satOrgIOContext      = satIOContext;
  }
  else
  {
    SM_DBG6(("smsatDeResetDeviceCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG6(("smsatDeResetDeviceCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      SM_DBG6(("smsatDeResetDeviceCB: satOrgIOContext is NOT NULL\n"));
    }
  }
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatDeResetDeviceCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);
    oneDeviceData->satTmTaskTag = agNULL;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BREAK ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY
      )
  {
    SM_DBG1(("smsatDeResetDeviceCB: OSSA_IO_OPEN_CNX_ERROR!!!\n"));

    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);
    oneDeviceData->satTmTaskTag = agNULL;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }
 if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisPioSetup_t is expected */
#ifdef  TD_DEBUG_ENABLE
    satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
    ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
    ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
    SM_DBG1(("smsatDeResetDeviceCB: ataStatus 0x%x ataError 0x%x!!!\n", ataStatus, ataError));
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 oneDeviceData->satTmTaskTag);
    oneDeviceData->satTmTaskTag = agNULL;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

  /* success */
  SM_DBG1(("smsatDeResetDeviceCB: success !!!\n"));
  SM_DBG1(("smsatDeResetDeviceCB: TMF %d!!!\n", satOrgIOContext->TMF));

  if (satOrgIOContext->TMF == AG_ABORT_TASK)
  {
    AbortTM = agTRUE;
  }
  if (AbortTM == agTRUE)
  {
    SM_DBG1(("smsatDeResetDeviceCB: calling satAbort!!!\n"));
    smsatAbort(smRoot, agRoot, satOrgIOContext->satToBeAbortedIOContext);
  }
  oneDeviceData->satTmTaskTag = agNULL;
  oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  SM_DBG1(("smsatDeResetDeviceCB: satPendingIO %d satNCQMaxIO %d!!!\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
  SM_DBG1(("smsatDeResetDeviceCB: satPendingNCQIO %d satPendingNONNCQIO %d!!!\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));
  smsatFreeIntIoResource( smRoot, oneDeviceData, satIntIo );

  /* TM completed */
  tdsmEventCB( smRoot,
               smDeviceHandle,
               smIntrEventTypeTaskManagement,
               smTMOK,
               oneDeviceData->satTmTaskTag);
  SM_DBG5(("smsatDeResetDeviceCB: device %p pending IO %d\n", oneDeviceData, oneDeviceData->satPendingIO));
  SM_DBG6(("smsatDeResetDeviceCB: end\n"));
  return;
}

osGLOBAL void
smaSATAAbortCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             flag,
                bit32             status
        )
{
  smRoot_t                  *smRoot = gsmRoot;
  smIORequestBody_t         *smIORequestBody = agNULL;
  smSatIOContext_t          *satIOContext;
  smDeviceHandle_t          *smDeviceHandle;
  smDeviceData_t            *oneDeviceData = agNULL;

  SM_DBG1(("smaSATAAbortCB: start\n"));

  smIORequestBody = (smIORequestBody_t *)agIORequest->osData;
  if (smIORequestBody == agNULL)
  {
    SM_DBG1(("smaSATAAbortCB: smIORequestBody is NULL!!! \n"));
    return;
  }

  satIOContext = &(smIORequestBody->transport.SATA.satIOContext);
  if (satIOContext == agNULL)
  {
    SM_DBG1(("smaSATAAbortCB: satIOContext is NULL!!! \n"));
    if (smIORequestBody->IOType.InitiatorTMIO.osMemHandle != agNULL)
    {
      tdsmFreeMemory(smRoot,
                     smIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(smIORequestBody_t)
                     );
    }
    return;
  }

  smDeviceHandle = smIORequestBody->smDevHandle;
  if (smDeviceHandle == agNULL)
  {
    SM_DBG1(("smaSATAAbortCB: smDeviceHandle is NULL!!!\n"));
    if (smIORequestBody->IOType.InitiatorTMIO.osMemHandle != agNULL)
    {
      tdsmFreeMemory(smRoot,
                     smIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(smIORequestBody_t)
                     );
    }
    return;
  }

  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;
  if (oneDeviceData == agNULL)
  {
    SM_DBG1(("smaSATAAbortCB: oneDeviceData is NULL!!!\n"));
    if (smIORequestBody->IOType.InitiatorTMIO.osMemHandle != agNULL)
    {
      tdsmFreeMemory(smRoot,
                     smIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(smIORequestBody_t)
                     );
    }

    return;
  }

  if (flag == 2)
  {
    /* abort per port */
    SM_DBG1(("smaSATAAbortCB: abort per port, not yet!!!\n"));
  }
  else if (flag == 1)
  {
     SM_DBG1(("smaSATAAbortCB: abort all!!!\n"));
    if (oneDeviceData->OSAbortAll == agTRUE)
    {
      oneDeviceData->OSAbortAll = agFALSE;
#if 0
      ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortOK,
                            agNULL);
#endif
#if 1
      tdsmEventCB( smRoot,
                   smDeviceHandle,
                   smIntrEventTypeLocalAbort,
                   smTMOK,
                   agNULL);
#endif

    }
    if (smIORequestBody->IOType.InitiatorTMIO.osMemHandle != agNULL)
    {
      tdsmFreeMemory(smRoot,
                     smIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(smIORequestBody_t)
                     );
    }
  }
  else if (flag == 0)
  {
    SM_DBG1(("smaSATAAbortCB: abort one\n"));
    if (status == OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smaSATAAbortCB: OSSA_IO_SUCCESS\n"));
    }
    else if (status == OSSA_IO_NOT_VALID)
    {
      SM_DBG1(("smaSATAAbortCB: OSSA_IO_NOT_VALID\n"));
    }
    else if (status == OSSA_IO_NO_DEVICE)
    {
      SM_DBG1(("smaSATAAbortCB: OSSA_IO_NO_DEVICE\n"));
    }
    else if (status == OSSA_IO_ABORT_IN_PROGRESS)
    {
      SM_DBG1(("smaSATAAbortCB: OSSA_IO_ABORT_IN_PROGRESS\n"));
    }
#ifdef REMOVED
    else if (status == OSSA_IO_ABORT_DELAYED)
    {
      SM_DBG1(("smaSATAAbortCB: OSSA_IO_ABORT_DELAYED\n"));
    }
#endif
    else
    {
      SM_DBG1(("smaSATAAbortCB: unspecified status 0x%x\n", status ));
    }
    if (smIORequestBody->IOType.InitiatorTMIO.osMemHandle != agNULL)
    {
      tdsmFreeMemory(smRoot,
                     smIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(smIORequestBody_t)
                     );
    }
  }
  else
  {
    SM_DBG1(("smaSATAAbortCB: wrong flag %d\n", flag));
  }

  return;
}

osGLOBAL void
smLocalPhyControlCB(
                     agsaRoot_t     *agRoot,
                     agsaContext_t  *agContext,
                     bit32          phyId,
                     bit32          phyOperation,
                     bit32          status,
                     void           *parm
                    )
{
  smRoot_t                  *smRoot = gsmRoot;
  smIORequestBody_t         *smIORequestBody = agNULL;
  smDeviceHandle_t          *smDeviceHandle;
  smDeviceData_t            *oneDeviceData = agNULL;
  smIORequest_t             *currentTaskTag;
  agsaDevHandle_t           *agDevHandle = agNULL;

  SM_DBG1(("smLocalPhyControlCB: start phyId 0x%x phyOperation 0x%x status 0x%x\n",phyId,phyOperation,status));

  if (agContext == agNULL)
  {
    SM_DBG1(("smLocalPhyControlCB: agContext is NULL!!!\n"));
    return;
  }
  currentTaskTag = (smIORequest_t *)agContext->osData;
  if (currentTaskTag == agNULL)
  {
    SM_DBG1(("smLocalPhyControlCB: currentTaskTag is NULL!!!\n"));
    return;
  }
  smIORequestBody = (smIORequestBody_t *)currentTaskTag->smData;
  if (smIORequestBody == agNULL)
  {
    SM_DBG1(("smLocalPhyControlCB: smIORequestBody is NULL!!!\n"));
    return;
  }
  smDeviceHandle = smIORequestBody->smDevHandle;
  if (smDeviceHandle == agNULL)
  {
    SM_DBG1(("smLocalPhyControlCB: smDeviceHandle is NULL!!!\n"));
    return;
  }
  oneDeviceData = (smDeviceData_t *)smDeviceHandle->smData;
  if (oneDeviceData == agNULL)
  {
    SM_DBG1(("smLocalPhyControlCB: oneDeviceData is NULL!!!\n"));
    return;
  }
  switch (phyOperation)
  {
  case AGSA_PHY_LINK_RESET: /* fall through */
  case AGSA_PHY_HARD_RESET:
    if (status == OSSA_SUCCESS)
    {
      SM_DBG2(("smLocalPhyControlCB: callback to TD layer with success\n"));
      agDevHandle = oneDeviceData->agDevHandle;
      SM_DBG2(("smLocalPhyControlCB: satPendingIO %d satNCQMaxIO %d\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
      SM_DBG1(("smLocalPhyControlCB: satPendingNCQIO %d satPendingNONNCQIO %d\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));
      oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
#ifdef REMOVED
      saSetDeviceState(agRoot,
                       agNULL,
                       tdsmRotateQnumber(smRoot, smDeviceHandle),
                       agDevHandle,
                       SA_DS_OPERATIONAL
                       );
      /* TM completed */
      tdsmEventCB( smRoot,
                   smDeviceHandle,
                   smIntrEventTypeTaskManagement,
                   smTMOK,
                   currentTaskTag);
#endif
    }
    else
    {
      SM_DBG1(("smLocalPhyControlCB: callback to TD layer with failure!!!\n"));
      /* TM completed */
      tdsmEventCB( smRoot,
                   smDeviceHandle,
                   smIntrEventTypeTaskManagement,
                   smTMFailed,
                   currentTaskTag);
    }
    break;
  default:
    SM_DBG1(("ossaLocalPhyControlCB: error default case. phyOperation is %d!!!\n", phyOperation));
    /* TM completed */
    tdsmEventCB( smRoot,
                 smDeviceHandle,
                 smIntrEventTypeTaskManagement,
                 smTMFailed,
                 currentTaskTag);
    break;
  }
  return;
}

osGLOBAL void
smsatSetFeaturesAACB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    smRoot_t                  *smRoot = agNULL;
    smIntRoot_t               *smIntRoot = agNULL;
    smIntContext_t            *smAllShared = agNULL;
    smIORequestBody_t         *smIORequestBody;
    smIORequestBody_t         *smOrgIORequestBody = agNULL;
    smSatIOContext_t          *satIOContext;
    smSatIOContext_t          *satOrgIOContext;
    smSatInternalIo_t         *satIntIo;
    smDeviceData_t            *oneDeviceData;
    smIORequest_t             *smOrgIORequest;
    smDeviceHandle_t          *smDeviceHandle;
    smIORequest_t             *smIORequest;
    bit32                     ataStatus = 0;
    bit32                     ataError = 0;
    agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;

    SM_DBG2(("smsatSetFeaturesAACB: start\n"));
    smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
    satIOContext           = (smSatIOContext_t *) ioContext;
    if (satIOContext == agNULL)
    {
      SM_DBG1(("smsatSetFeaturesAACB: satIOContext is NULL\n"));
      return;
    }
    satIntIo               = satIOContext->satIntIoContext;
    oneDeviceData          = satIOContext->pSatDevData;
    smDeviceHandle         = satIOContext->psmDeviceHandle;
    smRoot                 = oneDeviceData->smRoot;
    smIntRoot              = (smIntRoot_t *)smRoot->smData;
    smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
    if (satIntIo == agNULL)
    {
      SM_DBG5(("smsatSetFeaturesAACB: External smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext = satIOContext;
      smOrgIORequest  = smIORequestBody->smIORequest;
      smIORequest     = smOrgIORequest;
    }
    else
    {
      SM_DBG5(("smsatSetFeaturesAACB: Internal smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext        = satIOContext->satOrgIOContext;
      smOrgIORequestBody  = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
      smOrgIORequest      = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    }
    smIORequest  = smOrgIORequestBody->smIORequest;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smIORequestBody->ioCompleted = agTRUE;
    smIORequestBody->ioStarted   = agFALSE;
    if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatSetFeaturesAACB: fail, case 1 agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    }
    if (agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatSetFeaturesAACB: fail, case 2 status %d!!!\n", agIOStatus));
    }
    if (agIOInfoLen != 0 && agIOStatus == OSSA_IO_SUCCESS)
    {
      statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
      ataStatus   = statDevToHostFisHeader->status;   /* ATA Status register */
      ataError    = statDevToHostFisHeader->error;    /* ATA Eror register   */
      if ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
      {
        SM_DBG1(("smsatSetFeaturesAACB: fail, case 3 ataStatus %d ataError %d!!!\n", ataStatus, ataError));
      }
      if (ataError != 0)
      {
        SM_DBG1(("smsatSetFeaturesAACB: fail, case 4 ataStatus %d ataError %d!!!\n", ataStatus, ataError));
      }
    }
    /* interal structure free */
    smsatFreeIntIoResource(smRoot,oneDeviceData, satIntIo);
    if (smIORequest->tdData == smIORequest->smData)
    {
      SM_DBG1(("smsatSetFeaturesAACB: the same tdData and smData error!\n"));
    }
    /*Complete this identify device IO */
    tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
    SM_DBG2(("smsatSetFeaturesAACB: end\n"));
}

/*****************************************************************************
*! \brief  smsatSetFeaturesDMACB
*
*   This routine is a callback function called from smllSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to smSatIOContext_t.
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void
smsatSetFeaturesDMACB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    smRoot_t                  *smRoot = agNULL;
    smIntRoot_t               *smIntRoot = agNULL;
    smIntContext_t            *smAllShared = agNULL;
    smIORequestBody_t         *smIORequestBody;
    smIORequestBody_t         *smOrgIORequestBody = agNULL;
    smSatIOContext_t          *satIOContext;
    smSatIOContext_t          *satOrgIOContext;
    smSatIOContext_t          *satNewIOContext;
    smSatInternalIo_t         *satIntIo;
    smSatInternalIo_t         *satNewIntIo = agNULL;
    smDeviceData_t            *oneDeviceData;
    smIniScsiCmnd_t           *scsiCmnd;
    smIORequest_t             *smOrgIORequest;
    smDeviceHandle_t          *smDeviceHandle;
    bit32                      status = SM_RC_FAILURE;
    smIORequest_t             *smIORequest;

    SM_DBG2(("smsatSetFeaturesDMACB: start\n"));
    smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
    satIOContext           = (smSatIOContext_t *) ioContext;
    if (satIOContext == agNULL)
    {
      SM_DBG1(("smsatSetFeaturesDMACB: satIOContext is NULL\n"));
      return;
    }
    satIntIo               = satIOContext->satIntIoContext;
    oneDeviceData          = satIOContext->pSatDevData;
    smDeviceHandle         = satIOContext->psmDeviceHandle;
    smRoot                 = oneDeviceData->smRoot;
    smIntRoot              = (smIntRoot_t *)smRoot->smData;
    smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
    if (satIntIo == agNULL)
    {
      SM_DBG2(("smsatSetFeaturesDMACB: External smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext = satIOContext;
      smOrgIORequest  = smIORequestBody->smIORequest;
      scsiCmnd        = satIOContext->pScsiCmnd;
    }
    else
    {
      SM_DBG2(("smsatSetFeaturesDMACB: Internal smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext        = satIOContext->satOrgIOContext;
      smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
      smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
      scsiCmnd      = satOrgIOContext->pScsiCmnd;
    }
    smIORequest  = smOrgIORequestBody->smIORequest;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smIORequestBody->ioCompleted = agTRUE;
    smIORequestBody->ioStarted   = agFALSE;

    oneDeviceData->satDMAEnabled = agTRUE;
    /* interal structure free */
    smsatFreeIntIoResource(smRoot,
                           oneDeviceData,
                           satIntIo);

    if (smIORequest->tdData == smIORequest->smData)
    {
      SM_DBG1(("smsatSetFeaturesDMACB: the same tdData and smData error!\n"));
    }
    SM_DBG2(("smsatSetFeaturesDMACB: agIOStatus 0x%x\n", agIOStatus));
    /* check the agIOStatus */
    if (agIOStatus == OSSA_IO_ABORTED ||
        agIOStatus == OSSA_IO_NO_DEVICE ||
        agIOStatus == OSSA_IO_PORT_IN_RESET ||
        agIOStatus == OSSA_IO_DS_NON_OPERATIONAL ||
        agIOStatus == OSSA_IO_DS_IN_RECOVERY ||
        agIOStatus == OSSA_IO_DS_IN_ERROR ||
        agIOStatus == OSSA_IO_DS_INVALID
       )
    {
      SM_DBG1(("smsatSetFeaturesDMACB: error status 0x%x\n", agIOStatus));
      SM_DBG1(("smsatSetFeaturesDMACB: did %d!!!\n", oneDeviceData->id));
      tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
      return;
    }
    if (oneDeviceData->satDeviceType == SATA_ATAPI_DEVICE)
    {
       /*if ATAPI device, only need to enable PIO and DMA transfer mode, then complete this identify device command */
       tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
       return;
    }

    /* enble read look-ahead feature*/
    if (oneDeviceData->satReadLookAheadSupport == agTRUE)
    {
        satNewIntIo = smsatAllocIntIoResource(smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           0,
                                           satNewIntIo);
        if (satNewIntIo == agNULL)
        {
            SM_DBG1(("smsatSetFeaturesDMACB: memory allocation fails\n"));
            /*Complete this identify packet device IO */
            tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
            return;
        } /* end memory allocation */

        satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                          smOrgIORequest,
                                          oneDeviceData,
                                          scsiCmnd,
                                          satOrgIOContext
                                          );
        /* sends SET FEATURES  command to enable Read Look-Ahead  */
        status = smsatSetFeaturesReadLookAhead(smRoot,
                                &satNewIntIo->satIntSmIORequest,
                                satNewIOContext->psmDeviceHandle,
                                &satNewIntIo->satIntSmScsiXchg,
                                satNewIOContext
                                );
        if (status != SM_RC_SUCCESS)
        {
            smsatFreeIntIoResource(smRoot, oneDeviceData, satNewIntIo);
            SM_DBG1(("smsatSetFeaturesDMACB: failed to call smsatSetFeatures()\n"));
            /*Complete this identify device IO */
            tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
        }
        SM_DBG2(("smsatSetFeaturesDMACB: end\n"));
        return;
    }
    /* enble Volatile Write Cache feature*/
    if (oneDeviceData->satVolatileWriteCacheSupport == agTRUE)
    {
       satNewIntIo = smsatAllocIntIoResource(smRoot,
                                             smOrgIORequest,
                                             oneDeviceData,
                                             0,
                                             satNewIntIo);
        if (satNewIntIo == agNULL)
        {
           SM_DBG1(("smsatSetFeaturesDMACB: memory allocation fails\n"));
           /*Complete this identify packet device IO */
           tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
           return;
        } /* end memory allocation */
        satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                            smOrgIORequest,
                                            oneDeviceData,
                                            scsiCmnd,
                                            satOrgIOContext
                                            );
        /* sends SET FEATURES command to enable Volatile Write Cache */
        status = smsatSetFeaturesVolatileWriteCache(smRoot,
                                    &satNewIntIo->satIntSmIORequest,
                                    satNewIOContext->psmDeviceHandle,
                                    &satNewIntIo->satIntSmScsiXchg,
                                    satNewIOContext
                                    );
        if (status != SM_RC_SUCCESS)
        {
           smsatFreeIntIoResource(smRoot, oneDeviceData, satNewIntIo);
           SM_DBG1(("smsatSetFeaturesDMACB: failed to call smsatSetFeatures()\n"));
           /*Complete this identify device IO */
           tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
        }
        SM_DBG2(("smsatSetFeaturesDMACB: end\n"));
        return;
    }
    /* turn on DMA Setup FIS auto-activate by sending set feature FIS */
    if (oneDeviceData->satNCQ == agTRUE && oneDeviceData->satDMASetupAA == agTRUE)
    {
        satNewIntIo = smsatAllocIntIoResource( smRoot,
                                               smOrgIORequest,
                                               oneDeviceData,
                                               0,
                                               satNewIntIo);

        if (satNewIntIo == agNULL)
        {
          SM_DBG1(("smsatSetFeaturesDMACB: momory allocation fails; can't send set feature\n"));
          tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
          return;
        } /* end memory allocation */
        satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                            smOrgIORequest,
                                            oneDeviceData,
                                            agNULL,
                                            satOrgIOContext
                                            );
        /* send the Set Feature ATA command to SATA device for enable DMA Setup FIS auto-activate */
        status = smsatSetFeaturesAA(smRoot,
                                    &satNewIntIo->satIntSmIORequest,
                                    satNewIOContext->psmDeviceHandle,
                                    &satNewIntIo->satIntSmScsiXchg, /* orginal from OS layer */
                                    satNewIOContext);
        if (status != SM_RC_SUCCESS)
        {
            SM_DBG1(("smsatSetFeaturesDMACB: failed to send set feature!!!\n"));
            smsatFreeIntIoResource( smRoot,
                                    oneDeviceData,
                                    satNewIntIo);
            tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOFailed, &(oneDeviceData->satIdentifyData));
        }
    }
    else
    {
        /*Complete this identify device IO */
        tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
    }
    SM_DBG2(("smsatSetFeaturesDMACB: end\n"));
}

/*****************************************************************************
*! \brief  smsatSetFeaturesReadLookAheadCB
*
*   This routine is a callback function called from smllSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to smSatIOContext_t.
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void
smsatSetFeaturesReadLookAheadCB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    smRoot_t                  *smRoot = agNULL;
    smIntRoot_t               *smIntRoot = agNULL;
    smIntContext_t            *smAllShared = agNULL;
    smIORequestBody_t         *smIORequestBody;
    smIORequestBody_t         *smOrgIORequestBody = agNULL;
    smSatIOContext_t          *satIOContext;
    smSatIOContext_t          *satOrgIOContext;
    smSatIOContext_t          *satNewIOContext;
    smSatInternalIo_t         *satIntIo;
    smSatInternalIo_t         *satNewIntIo = agNULL;
    smDeviceData_t            *oneDeviceData;
    smIniScsiCmnd_t           *scsiCmnd;
    smIORequest_t             *smOrgIORequest;
    smDeviceHandle_t          *smDeviceHandle;
    bit32                      status = SM_RC_FAILURE;
    smIORequest_t             *smIORequest;

    SM_DBG2(("smsatSetFeaturesReadLookAheadCB: start\n"));
    smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
    satIOContext           = (smSatIOContext_t *) ioContext;
    if (satIOContext == agNULL)
    {
      SM_DBG1(("smsatSetFeaturesReadLookAheadCB: satIOContext is NULL\n"));
      return;
    }
    satIntIo               = satIOContext->satIntIoContext;
    oneDeviceData          = satIOContext->pSatDevData;
    smDeviceHandle         = satIOContext->psmDeviceHandle;
    smRoot                 = oneDeviceData->smRoot;
    smIntRoot              = (smIntRoot_t *)smRoot->smData;
    smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;

    if (satIntIo == agNULL)
    {
      SM_DBG2(("smsatSetFeaturesReadLookAheadCB: External smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext = satIOContext;
      smOrgIORequest  = smIORequestBody->smIORequest;
      scsiCmnd        = satIOContext->pScsiCmnd;
    }
    else
    {
      SM_DBG2(("smsatSetFeaturesReadLookAheadCB: Internal smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext        = satIOContext->satOrgIOContext;
      smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
      smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
      scsiCmnd      = satOrgIOContext->pScsiCmnd;
    }
    smIORequest  = smOrgIORequestBody->smIORequest;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smIORequestBody->ioCompleted = agTRUE;
    smIORequestBody->ioStarted   = agFALSE;

    oneDeviceData->satLookAheadEnabled = agTRUE;

    /* interal structure free */
    smsatFreeIntIoResource(smRoot,
                           oneDeviceData,
                           satIntIo);

    /* check the agIOStatus */
    if (agIOStatus == OSSA_IO_ABORTED ||
        agIOStatus == OSSA_IO_NO_DEVICE ||
        agIOStatus == OSSA_IO_PORT_IN_RESET ||
        agIOStatus == OSSA_IO_DS_NON_OPERATIONAL ||
        agIOStatus == OSSA_IO_DS_IN_RECOVERY ||
        agIOStatus == OSSA_IO_DS_IN_ERROR ||
        agIOStatus == OSSA_IO_DS_INVALID
       )
    {
      SM_DBG1(("smsatSetFeaturesReadLookAheadCB: error status 0x%x\n", agIOStatus));
      SM_DBG1(("smsatSetFeaturesReadLookAheadCB: did %d!!!\n", oneDeviceData->id));
      tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
      return;
    }



    /* enble Volatile Write Cache feature*/
    if (oneDeviceData->satVolatileWriteCacheSupport == agTRUE)
    {
        satNewIntIo = smsatAllocIntIoResource(smRoot,
                                           smOrgIORequest,
                                           oneDeviceData,
                                           0,
                                           satNewIntIo);
        if (satNewIntIo == agNULL)
        {
            SM_DBG1(("smsatSetFeaturesReadLookAheadCB: memory allocation fails\n"));
            /*Complete this identify packet device IO */
            tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
            return;
        } /* end memory allocation */

        satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                          smOrgIORequest,
                                          oneDeviceData,
                                          scsiCmnd,
                                          satOrgIOContext
                                          );
        /* sends SET FEATURES command to enable Volatile Write Cache */
        status = smsatSetFeaturesVolatileWriteCache(smRoot,
                                &satNewIntIo->satIntSmIORequest,
                                satNewIOContext->psmDeviceHandle,
                                &satNewIntIo->satIntSmScsiXchg,
                                satNewIOContext
                                );
        if (status != SM_RC_SUCCESS)
        {
            smsatFreeIntIoResource(smRoot, oneDeviceData, satNewIntIo);
            SM_DBG1(("smsatSetFeaturesReadLookAheadCB: failed to call smsatSetFeatures()\n"));
            /*Complete this identify device IO */
            tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
        }
        SM_DBG2(("smsatSetFeaturesReadLookAheadCB: end\n"));

        return;
    }

    /* turn on DMA Setup FIS auto-activate by sending set feature FIS */
    if (oneDeviceData->satNCQ == agTRUE && oneDeviceData->satDMASetupAA == agTRUE)
    {
        satNewIntIo = smsatAllocIntIoResource( smRoot,
                                               smOrgIORequest,
                                               oneDeviceData,
                                               0,
                                               satNewIntIo);

        if (satNewIntIo == agNULL)
        {
          SM_DBG1(("smsatSetFeaturesReadLookAheadCB: momory allocation fails; can't send set feature\n"));
          tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
          return;
        } /* end memory allocation */

        satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                            smOrgIORequest,
                                            oneDeviceData,
                                            agNULL,
                                            satOrgIOContext
                                            );
        /* send the Set Feature ATA command to SATA device for enable DMA Setup FIS auto-activate */
        status = smsatSetFeaturesAA(smRoot,
                                    &satNewIntIo->satIntSmIORequest,
                                    satNewIOContext->psmDeviceHandle,
                                    &satNewIntIo->satIntSmScsiXchg, /* orginal from OS layer */
                                    satNewIOContext);

        if (status != SM_RC_SUCCESS)
        {
            SM_DBG1(("smsatSetFeaturesReadLookAheadCB: failed to send set feature!!!\n"));
            smsatFreeIntIoResource( smRoot,
                                    oneDeviceData,
                                    satNewIntIo);
            /* clean up TD layer's IORequestBody */
            tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOFailed, &(oneDeviceData->satIdentifyData));
        }
    }
    else
    {
        /*Complete this identify device IO */
        tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
    }
    SM_DBG2(("smsatSetFeaturesReadLookAheadCB: end\n"));
}
/*****************************************************************************
*! \brief  smsatSetFeaturesVolatileWriteCacheCB
*
*   This routine is a callback function called from smllSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to smSatIOContext_t.
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void
smsatSetFeaturesVolatileWriteCacheCB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    smRoot_t                  *smRoot = agNULL;
    smIntRoot_t               *smIntRoot = agNULL;
    smIntContext_t            *smAllShared = agNULL;
    smIORequestBody_t         *smIORequestBody;
    smIORequestBody_t         *smOrgIORequestBody = agNULL;
    smSatIOContext_t          *satIOContext;
    smSatIOContext_t          *satOrgIOContext;
    smSatIOContext_t          *satNewIOContext;
    smSatInternalIo_t         *satIntIo;
    smSatInternalIo_t         *satNewIntIo = agNULL;
    smDeviceData_t            *oneDeviceData;
    smIORequest_t             *smOrgIORequest;
    smDeviceHandle_t          *smDeviceHandle;
    smIORequest_t             *smIORequest;
    bit32                     ataStatus = 0;
    bit32                     ataError = 0;
    agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
    bit32                     status = SM_RC_FAILURE;

    SM_DBG2(("smsatSetFeaturesVolatileWriteCacheCB: start\n"));
    smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
    satIOContext           = (smSatIOContext_t *) ioContext;
    if (satIOContext == agNULL)
    {
      SM_DBG1(("smsatSetFeaturesVolatileWriteCacheCB: satIOContext is NULL\n"));
      return;
    }
    satIntIo               = satIOContext->satIntIoContext;
    oneDeviceData          = satIOContext->pSatDevData;
    smDeviceHandle         = satIOContext->psmDeviceHandle;
    smRoot                 = oneDeviceData->smRoot;
    smIntRoot              = (smIntRoot_t *)smRoot->smData;
    smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
    if (satIntIo == agNULL)
    {
      SM_DBG5(("smsatSetFeaturesVolatileWriteCacheCB: External smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext = satIOContext;
      smOrgIORequest  = smIORequestBody->smIORequest;
      smIORequest     = smOrgIORequest;
    }
    else
    {
      SM_DBG5(("smsatSetFeaturesVolatileWriteCacheCB: Internal smSatInternalIo_t satIntIoContext\n"));
      satOrgIOContext        = satIOContext->satOrgIOContext;
      smOrgIORequestBody  = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
      smOrgIORequest      = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    }
    smIORequest  = smOrgIORequestBody->smIORequest;
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smIORequestBody->ioCompleted = agTRUE;
    smIORequestBody->ioStarted   = agFALSE;
    if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatSetFeaturesVolatileWriteCacheCB: fail, case 1 agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    }
    if (agIOStatus != OSSA_IO_SUCCESS)
    {
      SM_DBG1(("smsatSetFeaturesVolatileWriteCacheCB: fail, case 2 status %d!!!\n", agIOStatus));
    }
    if (agIOInfoLen != 0 && agIOStatus == OSSA_IO_SUCCESS)
    {
      statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
      ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
      ataError      = statDevToHostFisHeader->error;    /* ATA Eror register   */
      if ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
      {
        SM_DBG1(("smsatSetFeaturesVolatileWriteCacheCB: fail, case 3 ataStatus %d ataError %d!!!\n", ataStatus, ataError));
      }
      if (ataError != 0)
      {
        SM_DBG1(("smsatSetFeaturesVolatileWriteCacheCB: fail, case 4 ataStatus %d ataError %d!!!\n", ataStatus, ataError));
      }
    }

    oneDeviceData->satWriteCacheEnabled = agTRUE;

    /* interal structure free */
    smsatFreeIntIoResource(smRoot,oneDeviceData, satIntIo);
    /* check the agIOStatus */
    if (agIOStatus == OSSA_IO_ABORTED ||
        agIOStatus == OSSA_IO_NO_DEVICE ||
        agIOStatus == OSSA_IO_PORT_IN_RESET ||
        agIOStatus == OSSA_IO_DS_NON_OPERATIONAL ||
        agIOStatus == OSSA_IO_DS_IN_RECOVERY ||
        agIOStatus == OSSA_IO_DS_IN_ERROR ||
        agIOStatus == OSSA_IO_DS_INVALID
       )
    {
      SM_DBG1(("smsatSetFeaturesVolatileWriteCacheCB: error status 0x%x\n", agIOStatus));
      SM_DBG1(("smsatSetFeaturesVolatileWriteCacheCB: did %d!!!\n", oneDeviceData->id));
      tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
      return;
    }
    /* turn on DMA Setup FIS auto-activate by sending set feature FIS */
    if (oneDeviceData->satNCQ == agTRUE && oneDeviceData->satDMASetupAA == agTRUE)
    {
        satNewIntIo = smsatAllocIntIoResource( smRoot,
                                               smOrgIORequest,
                                               oneDeviceData,
                                               0,
                                               satNewIntIo);
        if (satNewIntIo == agNULL)
        {
          SM_DBG1(("smsatSetFeaturesVolatileWriteCacheCB: momory allocation fails; can't send set feature\n"));
          tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
          return;
        } /* end memory allocation */
        satNewIOContext = smsatPrepareNewIO(satNewIntIo,
                                            smOrgIORequest,
                                            oneDeviceData,
                                            agNULL,
                                            satOrgIOContext
                                            );
        /* send the Set Feature ATA command to SATA device for enable DMA Setup FIS auto-activate */
        status = smsatSetFeaturesAA(smRoot,
                                    &satNewIntIo->satIntSmIORequest,
                                    satNewIOContext->psmDeviceHandle,
                                    &satNewIntIo->satIntSmScsiXchg, /* orginal from OS layer */
                                    satNewIOContext);
        if (status != SM_RC_SUCCESS)
        {
            SM_DBG1(("smsatSetFeaturesVolatileWriteCacheCB: failed to send set feature!!!\n"));
            smsatFreeIntIoResource( smRoot,
                                    oneDeviceData,
                                    satNewIntIo);
            /* clean up TD layer's IORequestBody */
            tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOFailed, &(oneDeviceData->satIdentifyData));
        }
    }
    else
    {
        /*Complete this identify device IO */
        tdsmIDCompletedCB(smRoot, smIORequest, smDeviceHandle, smIOSuccess, &(oneDeviceData->satIdentifyData));
    }
    SM_DBG2(("smsatSetFeaturesVolatileWriteCacheCB: end\n"));
}


osGLOBAL void 
smsatSMARTEnablePassCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     agsaFrameHandle_t agFrameHandle,
                     void              *ioContext
                    )
  {

  smRoot_t                 *smRoot = agNULL;
  smIntRoot_t              *smIntRoot = agNULL; 
  smIntContext_t           *smAllShared = agNULL;
  smIORequestBody_t        *smIORequestBody;
  smIORequestBody_t        *smOrgIORequestBody;
  smSatIOContext_t         *satIOContext;
  smSatIOContext_t         *satOrgIOContext;
  //smSatIOContext_t         *satNewIOContext;
  smSatInternalIo_t        *satIntIo;
 //smSatInternalIo_t        *satNewIntIo = agNULL;
//  satDeviceData_t           *satDevData;
  smDeviceData_t           *oneDeviceData;
  smIniScsiCmnd_t          *scsiCmnd;
  smIORequest_t            *smOrgIORequest;
  //bit32                     status;
  smScsiRspSense_t          *pSense;
  bit8						bSenseKey = 0;
  bit16 					bSenseCodeInfo = 0;
 

  SM_DBG2(("smsatSMARTEnablePassCB: start\n"));
  SM_DBG4(("smsatSMARTEnablePassCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;  
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  /*ttttttthe one */
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatSMARTEnablePassCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;   
	pSense          = satOrgIOContext->pSense;
  }
  else
  {
    SM_DBG4(("smsatSMARTEnablePassCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatSMARTEnablePassCB: satOrgIOContext is NULL, wrong\n"));
      return;      
    }
    else
    {
      SM_DBG4(("smsatSMARTEnablePassCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    scsiCmnd               = satOrgIOContext->pScsiCmnd; 
	pSense          = satOrgIOContext->pSense;
  }
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatSMARTEnablePassCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                      smRoot, 
                      smOrgIORequest,
                      smIOFailed, 
                      smDetailOtherError,
                      agNULL, 
                      satOrgIOContext->interruptContext
                     );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  } 
  /*
    checking IO status, FIS type and error status
  */
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatSMARTEnablePassCB: not success status, status %d!!!\n", agIOStatus));
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      smsatProcessAbort(smRoot,
                        smOrgIORequest,
                        satOrgIOContext
                       );
  
      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo); 
      return;
    }
			
    smsatTranslateATAErrorsToSCSIErrors(
				agFirstDword->D2H.status,
				agFirstDword->D2H.error,
				&bSenseKey,
				&bSenseCodeInfo
				);
    smsatSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
    tdsmIOCompletedCB(smRoot,
                      smOrgIORequest,
                      smIOSuccess,
                      SCSI_STAT_CHECK_CONDITION, 
                      satOrgIOContext->pSmSenseData,
                      satOrgIOContext->interruptContext );

	
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);

    return;    
  }
  /* process success case */
  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);

 SM_DBG1(("smsatSMARTEnablePassCB:success status, status %d!!!\n", agIOStatus));
 tdsmIOCompletedCB(
					smRoot, 
					smOrgIORequest,
					smIOSuccess, 
					SCSI_STAT_GOOD,
					agNULL, 
					satOrgIOContext->interruptContext
				   );

 
                            
  return;
}

osGLOBAL void 
smsatSMARTRStatusPassCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext                   
               )

{


  smRoot_t                  *smRoot = agNULL;
  smIntRoot_t               *smIntRoot = agNULL; 
  smIntContext_t            *smAllShared = agNULL;
  smIORequestBody_t         *smIORequestBody;
  smIORequestBody_t         *smOrgIORequestBody;
  smSatIOContext_t          *satIOContext;
  smSatIOContext_t          *satOrgIOContext;
  smSatInternalIo_t         *satIntIo;
//  satDeviceData_t          *satDevData;
  smDeviceData_t            *oneDeviceData;

  smScsiRspSense_t          *pSense;
  smIORequest_t             *smOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                      ataStatus = 0;
  smScsiInitiatorRequest_t  *smScsiRequest; /* tiScsiXchg */
  smScsiInitiatorRequest_t  *smOrgScsiRequest; /* tiScsiXchg */
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
//  agsaFisRegD2HData_t        statDevToHostFisData;
  smIniScsiCmnd_t           *scsiCmnd;
  bit8						bSenseKey = 0;
  bit16 					bSenseCodeInfo = 0;
 
  
  SM_DBG2(("smsatSMARTRStatusPassCB: start\n"));
  SM_DBG5(("smsatSMARTRStatusPassCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody        = (smIORequestBody_t *)agIORequest->osData;
  satIOContext           = (smSatIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  oneDeviceData          = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;
  smRoot                 = oneDeviceData->smRoot;
  smIntRoot              = (smIntRoot_t *)smRoot->smData;  
  smAllShared            = (smIntContext_t *)&smIntRoot->smAllShared;
  
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatSMARTRStatusPassCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest  = smIORequestBody->smIORequest;
    pSense          = satOrgIOContext->pSense;
    smOrgScsiRequest   = satOrgIOContext->smScsiXchg;
     /* ATA command response payload */
    smScsiRequest   = satOrgIOContext->smScsiXchg;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;   
	SM_DBG1((" 0x%02x, 0x%02x, 0x%02x, 0x%02x,\n", scsiCmnd->cdb[0], scsiCmnd->cdb[1],scsiCmnd->cdb[2], scsiCmnd->cdb[3]));
	SM_DBG1((" 0x%02x, 0x%02x, 0x%02x, 0x%02x,\n", scsiCmnd->cdb[4], scsiCmnd->cdb[5],scsiCmnd->cdb[6], scsiCmnd->cdb[7]));
	SM_DBG1((" 0x%02x, 0x%02x, 0x%02x, 0x%02x,\n", scsiCmnd->cdb[8], scsiCmnd->cdb[9],scsiCmnd->cdb[10], scsiCmnd->cdb[11]));


  }
  else
  {
    SM_DBG4(("smsatSMARTRStatusPassCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatSMARTRStatusPassCB: satOrgIOContext is NULL\n"));
	  
	  return;
	  
    }
    else
    {
      SM_DBG4(("smsatSMARTRStatusPassCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody     = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest         = (smIORequest_t *)smOrgIORequestBody->smIORequest;
    
    smOrgScsiRequest   = satOrgIOContext->smScsiXchg;
    /* ATA command response payload */
    smScsiRequest   =  (smScsiInitiatorRequest_t *)&(satIntIo->satIntSmScsiXchg);
    scsiCmnd        = satOrgIOContext->pScsiCmnd; 
	pSense          = satOrgIOContext->pSense;
  }
  
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatSMARTRStatusPassCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                       smRoot, 
                       smOrgIORequest,
                       smIOFailed, 
                       smDetailOtherError,
                       agNULL, 
                       satOrgIOContext->interruptContext
                      );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }    
    
  if( agIOStatus != OSSA_IO_SUCCESS)
  {
  
    /* non-data -> device to host  fis are expected */
	 
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
	
    if ( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
         ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
       )
    {
      /* for debugging */
      if( agIOStatus != OSSA_IO_SUCCESS)
      {
        SM_DBG1(("smsatSMARTRStatusPassCB: FAILED, NOT IO_SUCCESS!!!\n"));
      }
      else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
      {
        SM_DBG1(("smsatSMARTRStatusPassCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
      }
      else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
                (ataStatus & DF_ATA_STATUS_MASK)
               )      
      {
        SM_DBG1(("smsatSMARTRStatusPassCB: FAILED, FAILED, error status!!!\n"));
      }

      /* Process abort case */
      if (agIOStatus == OSSA_IO_ABORTED)
      {
        smsatProcessAbort(smRoot,
                          smOrgIORequest,
                          satOrgIOContext
                         );

        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
                                oneDeviceData,
                                satIntIo); 
        return;
      }
		
      smsatTranslateATAErrorsToSCSIErrors(
				agFirstDword->D2H.status,
				agFirstDword->D2H.error,
				&bSenseKey,
				&bSenseCodeInfo
				);
      smsatSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
      tdsmIOCompletedCB(smRoot,
                        smOrgIORequest,
                        smIOSuccess,
                        SCSI_STAT_CHECK_CONDITION, 
                        satOrgIOContext->pSmSenseData,
                        satOrgIOContext->interruptContext );


      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;

    } /* error checking */
  }

  /* prcessing the success case */
  SM_DBG5(("smsatSMARTRStatusPassCB: SAT_SMART_RETURN_STATUS success\n"));
      
  tdsmIOCompletedCB( smRoot,
                     smOrgIORequest,
                     smIOSuccess,
                     SCSI_STAT_GOOD,
                     agNULL,
                     satOrgIOContext->interruptContext);
                                  

  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
 
  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);
                                

 return;
}

osGLOBAL void 
smsatSMARTReadLogCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext                   
               )
{

  smRoot_t                      *smRoot = agNULL;
  smIntRoot_t                   *smIntRoot = agNULL; 
  smIntContext_t                *smAllShared = agNULL;
  smIORequestBody_t             *smIORequestBody;
  smIORequestBody_t             *smOrgIORequestBody;
  smSatIOContext_t              *satIOContext;
  smSatIOContext_t              *satOrgIOContext;
  smSatInternalIo_t             *satIntIo;
//	satDeviceData_t 		 *satDevData;
  smDeviceData_t                *oneDeviceData;

  smScsiRspSense_t              *pSense;
  smIORequest_t                 *smOrgIORequest;

  agsaFisRegHostToDevice_t      *hostToDevFis = agNULL;
  bit32                         ataStatus = 0;
  smScsiInitiatorRequest_t      *smScsiRequest; /* tiScsiXchg */
  smScsiInitiatorRequest_t      *smOrgScsiRequest; /* tiScsiXchg */
//	  satReadLogExtSelfTest_t	*virtAddr1;
//	  satSmartReadLogSelfTest_t *virtAddr2;
  //bit8						*pLogPage;
//	  bit8						 SelfTestExecutionStatus = 0;
//	  bit32 					 i = 0;
  
  agsaFisRegD2HHeader_t         *statDevToHostFisHeader = agNULL;
//	  agsaFisRegD2HData_t		 statDevToHostFisData;
  smIniScsiCmnd_t               *scsiCmnd;
//	  bit32 					 lenReceived = 0;
  bit8                          bSenseKey = 0;
  bit16                         bSenseCodeInfo = 0;
	  
  SM_DBG2(("smsatSMARTReadLogCB: start\n"));
  SM_DBG5(("smsatSMARTReadLogCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate smIOContext */
  smIORequestBody		 = (smIORequestBody_t *)agIORequest->osData;
  satIOContext			 = (smSatIOContext_t *) ioContext;
  satIntIo				 = satIOContext->satIntIoContext;
  oneDeviceData 		 = satIOContext->pSatDevData;
  hostToDevFis			 = satIOContext->pFis;
  smRoot				 = oneDeviceData->smRoot;
  smIntRoot 			 = (smIntRoot_t *)smRoot->smData;  
  smAllShared			 = (smIntContext_t *)&smIntRoot->smAllShared;
  
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatSMARTReadLogCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest	= smIORequestBody->smIORequest;
    pSense			= satOrgIOContext->pSense;
    smOrgScsiRequest   = satOrgIOContext->smScsiXchg;

    /* ATA command response payload */
    smScsiRequest	= satOrgIOContext->smScsiXchg;
    scsiCmnd		= satOrgIOContext->pScsiCmnd;	 


  }
  else
  {
    SM_DBG4(("smsatSMARTReadLogCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext 	   = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatSMARTReadLogCB: satOrgIOContext is NULL\n"));
	  
      return;
	  
    }
    else
    {
      SM_DBG4(("smsatSMARTReadLogCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody	   = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest		   = (smIORequest_t *)smOrgIORequestBody->smIORequest;
	
    pSense		  = satOrgIOContext->pSense;
    smOrgScsiRequest   = satOrgIOContext->smScsiXchg;

    /* ATA command response payload */
    smScsiRequest	=  (smScsiInitiatorRequest_t *)&(satIntIo->satIntSmScsiXchg);
    scsiCmnd		= satOrgIOContext->pScsiCmnd;  
  }
	  
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatSMARTReadLogCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB(
                      smRoot, 
                      smOrgIORequest,
                      smIOFailed, 
                      smDetailOtherError,
                      agNULL, 
                      satOrgIOContext->interruptContext
                     );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }    

  //for Debuggings
  if(agFirstDword != NULL)
  {
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    SM_DBG1(("smsatSMARTReadLogCB: statDevToHostFisHeader->status, status %d!!!\n", statDevToHostFisHeader->status));
  }
  if ((agIOStatus != OSSA_IO_SUCCESS) && (agFirstDword != NULL))
  { 		   
    /* non-data and pio read -> device to host and pio setup fis are expected */
    /*
      first, assumed to be Reg Device to Host FIS
      This is OK to just find fis type
    */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus	  = statDevToHostFisHeader->status;   /* ATA Status register */
  }
	
  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    if ( ((statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) &&
         (statDevToHostFisHeader->fisType != PIO_SETUP_DEV_TO_HOST_FIS)) ||
         ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
       )
    {
      /* for debugging */
      if( agIOStatus != OSSA_IO_SUCCESS)
      {
        SM_DBG1(("smsatSMARTReadLogCB: FAILED, NOT IO_SUCCESS!!!\n"));
      }
      else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
      {
        SM_DBG1(("smsatSMARTReadLogCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
      }
      else if (statDevToHostFisHeader->fisType != PIO_SETUP_DEV_TO_HOST_FIS)
      {
        SM_DBG1(("smsatSMARTReadLogCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
      }
      else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
                (ataStatus & DF_ATA_STATUS_MASK)
              ) 	 
      {
        SM_DBG1(("smsatSMARTReadLogCB: FAILED, FAILED, error status!!!\n"));
      }
		
      /* Process abort case */
      if (agIOStatus == OSSA_IO_ABORTED)
      {
        smsatProcessAbort(smRoot,
                          smOrgIORequest,
                          satOrgIOContext
                         );
  
        smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

        smsatFreeIntIoResource( smRoot,
						  oneDeviceData,
						  satIntIo); 
        return;
      }
			
      /* for debugging */
  
      if (hostToDevFis->h.command == SAT_SMART)
      {
        if (hostToDevFis->h.features == SAT_SMART_READ_LOG)
        {
          SM_DBG1(("smsatSMARTReadLogCB: SAT_SMART_READ_LOG failed!!!\n"));
        }
        else
        {
          SM_DBG1(("smsatSMARTReadLogCB: error unknown command 0x%x feature 0x%x!!!\n", hostToDevFis->h.command, hostToDevFis->h.features));
        }
      }
      else
      {
        SM_DBG1(("smsatSMARTReadLogCB: error default case command 0x%x!!!\n", hostToDevFis->h.command));
      }
			
      smsatTranslateATAErrorsToSCSIErrors(
				agFirstDword->D2H.status,
				agFirstDword->D2H.error,
				&bSenseKey,
				&bSenseCodeInfo
				);
      smsatSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
      tdsmIOCompletedCB(smRoot,
                        smOrgIORequest,
                        smIOSuccess,
                        SCSI_STAT_CHECK_CONDITION, 
                        satOrgIOContext->pSmSenseData,
                        satOrgIOContext->interruptContext );


      smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

      smsatFreeIntIoResource( smRoot,
                              oneDeviceData,
                              satIntIo);
      return;

    } /* error checking */
  }
	
  /* prcessing the success case */
	  

  tdsmIOCompletedCB( smRoot,
                     smOrgIORequest,
                     smIOSuccess,
                     SCSI_STAT_GOOD,
                     agNULL,
                     satOrgIOContext->interruptContext);
							   
  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

  smsatFreeIntIoResource( smRoot,
                          oneDeviceData,
                          satIntIo);
			
  return;
}

osGLOBAL void 
smsatPassthroughCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext                   
               )
{
  smRoot_t	                *smRoot = agNULL;
  smIntRoot_t			*smIntRoot = agNULL; 
  smIntContext_t		*smAllShared = agNULL;
  smIORequestBody_t 		*smIORequestBody;
  smIORequestBody_t 		*smOrgIORequestBody;
  smSatIOContext_t		*satIOContext;
  smSatIOContext_t		*satOrgIOContext;
  smSatInternalIo_t 		*satIntIo;
  smDeviceData_t		*oneDeviceData;
  smScsiRspSense_t		*pSense;
  smIORequest_t 		*smOrgIORequest;
  agsaFisRegHostToDevice_t	*hostToDevFis = agNULL;
  bit32 			 ataStatus = 0;
  smScsiInitiatorRequest_t	*smScsiRequest; /* tiScsiXchg */
  smScsiInitiatorRequest_t	*smOrgScsiRequest; /* tiScsiXchg */
	  
  agsaFisRegD2HHeader_t 	*statDevToHostFisHeader = agNULL;
  smIniScsiCmnd_t		*scsiCmnd;
  bit8				 bSenseKey = 0;
  bit16 			 bSenseCodeInfo = 0;
	  
  SM_DBG2(("smsatPassthroughCB: start\n"));
  SM_DBG5(("smsatPassthroughCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));
	
  /* internally generate smIOContext */
  smIORequestBody		 = (smIORequestBody_t *)agIORequest->osData;
  satIOContext			 = (smSatIOContext_t *) ioContext;
  satIntIo			 = satIOContext->satIntIoContext;
  oneDeviceData 		 = satIOContext->pSatDevData;
  hostToDevFis			 = satIOContext->pFis;
  smRoot			 = oneDeviceData->smRoot;
  smIntRoot 			 = (smIntRoot_t *)smRoot->smData;  
  smAllShared			 = (smIntContext_t *)&smIntRoot->smAllShared;
  
  if (satIntIo == agNULL)
  {
    SM_DBG4(("smsatPassthroughCB: External smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    smOrgIORequest = smIORequestBody->smIORequest;
    pSense = satOrgIOContext->pSense;
    smOrgScsiRequest   = satOrgIOContext->smScsiXchg;

    /* ATA command response payload */
    smScsiRequest	= satOrgIOContext->smScsiXchg;
    scsiCmnd		= satOrgIOContext->pScsiCmnd;	 
  }
  else
  {
    SM_DBG4(("smsatPassthroughCB: Internal smSatInternalIo_t satIntIoContext\n"));
    satOrgIOContext 	   = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      SM_DBG4(("smsatPassthroughCB: satOrgIOContext is NULL\n"));
      return;
    }
    else
    {
      SM_DBG4(("smsatPassthroughCB: satOrgIOContext is NOT NULL\n"));
    }
    smOrgIORequestBody  = (smIORequestBody_t *)satOrgIOContext->smRequestBody;
    smOrgIORequest = (smIORequest_t *)smOrgIORequestBody->smIORequest;
		
    pSense = satOrgIOContext->pSense;
    smOrgScsiRequest   = satOrgIOContext->smScsiXchg;

    /* ATA command response payload */
    smScsiRequest	=  (smScsiInitiatorRequest_t *)&(satIntIo->satIntSmScsiXchg);
    scsiCmnd		= satOrgIOContext->pScsiCmnd;  
  }
	  
  smIORequestBody->ioCompleted = agTRUE;
  smIORequestBody->ioStarted = agFALSE;


   if (agIOStatus == OSSA_IO_UNDERFLOW)
  {
    SM_DBG1(("smsatPassthroughCB: IO_UNDERFLOW, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB( smRoot,
                       smOrgIORequest,
                       smIOUnderRun,
                       agIOInfoLen,
                       agNULL,
                       satOrgIOContext->interruptContext
                     );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);

    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
                            satIntIo);
    return;
  }

	
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    SM_DBG1(("smsatPassthroughCB: wrong. agFirstDword is NULL when error, status %d!!!\n", agIOStatus));
    tdsmIOCompletedCB( smRoot, 
		       smOrgIORequest,
		       smIOFailed, 
		       smDetailOtherError,
		       agNULL, 
		       satOrgIOContext->interruptContext
		     );
    smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
	
    smsatFreeIntIoResource( smRoot,
                            oneDeviceData,
			    satIntIo);
    return;
  }    

  //for Debuggings

   if ((agIOStatus != OSSA_IO_SUCCESS) && (agFirstDword != NULL))
   { 		   
     /* non-data and pio read -> device to host and pio setup fis are expected */
       /*
          first, assumed to be Reg Device to Host FIS
	  This is OK to just find fis type
        */
     statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
     ataStatus	  = statDevToHostFisHeader->status;   /* ATA Status register */
   }
   if( agIOStatus != OSSA_IO_SUCCESS)
   {
     if ( ((statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) &&
          (statDevToHostFisHeader->fisType != PIO_SETUP_DEV_TO_HOST_FIS)) ||
	  ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
        )
     {
       /* for debugging */
       if( agIOStatus != OSSA_IO_SUCCESS)
       {
         SM_DBG1(("smsatPassthroughCB: FAILED, NOT IO_SUCCESS!!!\n"));
       }
       else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
       {
         SM_DBG1(("smsatPassthroughCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
       }
       else if (statDevToHostFisHeader->fisType != PIO_SETUP_DEV_TO_HOST_FIS)
       {
         SM_DBG1(("smsatPassthroughCB: FAILED, Wrong FIS type 0x%x!!!\n",statDevToHostFisHeader->fisType));
       }
       else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
                 (ataStatus & DF_ATA_STATUS_MASK)
	       ) 	 
       {
         SM_DBG1(("smsatPassthroughCB: FAILED, FAILED, error status!!!\n"));
       }
		
       /* Process abort case */
       if (agIOStatus == OSSA_IO_ABORTED)
       {
         smsatProcessAbort( smRoot,
			    smOrgIORequest,
			    satOrgIOContext);
			  
	 smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
		
         smsatFreeIntIoResource( smRoot,
	                         oneDeviceData,
				 satIntIo); 
         return;
       }
			
       smsatTranslateATAErrorsToSCSIErrors( agFirstDword->D2H.status,
					    agFirstDword->D2H.error,
					    &bSenseKey,
					    &bSenseCodeInfo
					  );
       smsatSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
       tdsmIOCompletedCB( smRoot,
                          smOrgIORequest,
		          smIOSuccess,
			  SCSI_STAT_CHECK_CONDITION, 
			  satOrgIOContext->pSmSenseData,
			  satOrgIOContext->interruptContext );
		   
		    
       smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
       smsatFreeIntIoResource( smRoot,
                               oneDeviceData,
                              satIntIo);
       return;
		
     } /* error checking */
   }
	
   /* prcessing the success case */
   if(agFirstDword != NULL)
   {
     statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
     SM_DBG1(("smsatPassthroughCB: statDevToHostFisHeader->status, status %d!!!\n", statDevToHostFisHeader->status));
     smsatTranslateATAErrorsToSCSIErrors( agFirstDword->D2H.status,
					  agFirstDword->D2H.error,
					  &bSenseKey,
					  &bSenseCodeInfo);
     smsatSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
     if(agFirstDword->D2H.status & 0x01)
     {
       tdsmIOCompletedCB( smRoot,
                          smOrgIORequest,
			  smIOSuccess,
			  SCSI_STAT_CHECK_CONDITION, 
			  satOrgIOContext->pSmSenseData,
			  satOrgIOContext->interruptContext );
       smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
       smsatFreeIntIoResource( smRoot,
                               oneDeviceData,
                               satIntIo);
       return;
     }
   }

  tdsmIOCompletedCB( smRoot,
                     smOrgIORequest,
	             smIOSuccess,
	             SCSI_STAT_GOOD,
	             agNULL,
	             satOrgIOContext->interruptContext);
	 						   
  smsatDecrementPendingIO(smRoot, smAllShared, satIOContext);
	 
  smsatFreeIntIoResource( smRoot,
    			  oneDeviceData,
			  satIntIo);
					
  return;
}

