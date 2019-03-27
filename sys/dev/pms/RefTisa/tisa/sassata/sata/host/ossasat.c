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
/*****************************************************************************/
/** \file
 *
 * The file implementing SCSI/ATA Translation (SAT) for LL Layer callback
 *
 */
/*****************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#ifdef SATA_ENABLE

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

#include <dev/pms/RefTisa/tisa/sassata/sata/host/sat.h>
#include <dev/pms/RefTisa/tisa/sassata/sata/host/satproto.h>

/*****************************************************************************
*! \brief  ossaSATACompleted
*
*   This routine is called to complete a SATA request previously issued to the
*    LL Layer in saSATAStart()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*
*  \return: none
*
*****************************************************************************/
GLOBAL void
ossaSATACompleted(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  void              *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam
                  )

{
  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t           *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  tdIORequestBody_t    *tdIORequestBody;
  satIOContext_t       *satIOContext;
  satDeviceData_t      *pSatDevData;
  tdsaDeviceData_t     *tdsaDeviceData = agNULL;
  tdsaPortContext_t    *onePortContext;
  tiDeviceHandle_t     *tiDeviceHandle = agNULL;
  agsaDevHandle_t      *agDevHandle = agNULL;
  bit32                status;
  tdsaDeviceData_t     *oneDeviceData = agNULL;

  TDSA_OUT_ENTER(tiRoot);

  TI_DBG6(("ossaSATACompleted: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
    agIORequest, agIOStatus, agIOInfoLen));

  if (agIORequest == agNULL)
  {
    TI_DBG1(("ossaSATACompleted: agIORequest is NULL!!!!\n"));
    return;
  }

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  if (tdIORequestBody == agNULL)
  {
    TI_DBG1(("ossaSATACompleted: tdIORequestBody is NULL!!!!\n"));
    return;
  }
  /* for debugging */
  if (tdIORequestBody->ioCompleted == agTRUE)
  {
    tiDeviceHandle = tdIORequestBody->tiDevHandle;
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("ossaSATACompleted: tiDeviceHandle is NULL!!!!\n"));
      return;
    }
    tdsaDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    TI_DBG1(("ossaSATACompleted: Error!!!!!! double completion\n"));
    if (tdsaDeviceData == agNULL)
    {
      TI_DBG1(("ossaSATACompleted: tdsaDeviceData is NULL!!!!\n"));
      return;
    }
    TI_DBG1(("ossaSATACompleted: did %d \n", tdsaDeviceData->id));
    return;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  satIOContext    = &(tdIORequestBody->transport.SATA.satIOContext);

  if (satIOContext == agNULL)
  {
    TI_DBG1(("ossaSATACompleted: satIOContext is NULL!!!!\n"));
    return;
  }

  pSatDevData     = satIOContext->pSatDevData;

  if (tdIORequestBody->tiDevHandle != agNULL)
  {
    oneDeviceData = (tdsaDeviceData_t *)tdIORequestBody->tiDevHandle->tdData;
  }

  if (pSatDevData == agNULL && oneDeviceData != agNULL)
  {
    TI_DBG1(("ossaSATACompleted: pSatDevData is NULL, loc 1, wrong\n"));
    pSatDevData = &(oneDeviceData->satDevData);
  }

  if (pSatDevData == agNULL)
  {
    TI_DBG1(("ossaSATACompleted: pSatDevData is NULL loc 2, wrong\n"));
    if (satIOContext->satOrgIOContext == agNULL)
    {
      TI_DBG1(("ossaSATACompleted: external command\n"));
    }
    else
    {
      TI_DBG1(("ossaSATACompleted: internal command\n"));
    }
    goto ext;
  }

  tdsaDeviceData  = (tdsaDeviceData_t *)pSatDevData->satSaDeviceData;
  if (oneDeviceData != tdsaDeviceData)
  {
    if (satIOContext->satOrgIOContext == agNULL)
    {
      TI_DBG1(("ossaSATACompleted: diff device handle; external command\n"));
    }
    else
    {
      TI_DBG1(("ossaSATACompleted: diff device handle; internal command\n"));
    }
  }

  if (tdsaDeviceData == agNULL)
  {
    TI_DBG1(("ossaSATACompleted: tdsaDeviceData is NULL!!!!\n"));
    return;
  }

  onePortContext   = tdsaDeviceData->tdPortContext;

  /* retries in OSSA_IO_XFER_OPEN_RETRY_TIMEOUT */
  if (agIOStatus == OSSA_IO_XFER_OPEN_RETRY_TIMEOUT)
  {
    if (tdsaDeviceData->valid == agTRUE && tdsaDeviceData->registered == agTRUE &&
        tdsaDeviceData->tdPortContext != agNULL )
    {
      if (tdIORequestBody->reTries <= OPEN_RETRY_RETRIES) /* 10 */
      {
        agDevHandle = tdsaDeviceData->agDevHandle;
        status = saSATAStart( agRoot,
                              agIORequest,
                              tdsaRotateQnumber(tiRoot, tdsaDeviceData),
                              agDevHandle,
                              satIOContext->reqType,
                              &(tdIORequestBody->transport.SATA.agSATARequestBody),
                              satIOContext->sataTag,
                              ossaSATACompleted);

        if (status == AGSA_RC_SUCCESS)
        {
          TI_DBG1(("ossaSATACompleted: retried\n"));
          tdIORequestBody->ioStarted = agTRUE;
          tdIORequestBody->ioCompleted = agFALSE;
          tdIORequestBody->reTries++;
          goto ext;
        }
        else
        {
          TI_DBG1(("ossaSATACompleted: retry failed\n"));
          tdIORequestBody->ioStarted = agFALSE;
          tdIORequestBody->ioCompleted = agTRUE;
          tdIORequestBody->reTries = 0;
        }
      }
      else
      {
        /* retries is over, do nothing */
        TI_DBG1(("ossaSATACompleted: retry is over and fail\n"));
        tdIORequestBody->reTries = 0;
      }
    }
    else
    {
      TI_DBG1(("ossaSATACompleted: incorrect device state or no portcontext\n"));
      tdIORequestBody->reTries = 0;
    }
  } /* if OSSA_IO_XFER_OPEN_RETRY_TIMEOUT*/

  /* release tag value for SATA */
  if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
       (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
  {
    satTagRelease(tiRoot, pSatDevData, satIOContext->sataTag);
  }

  /* send SMP_PHY_CONTROL_HARD_RESET */
  if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY && tdsaAllShared->FCA)
  {
    if (pSatDevData->NumOfFCA <= 0) /* does SMP HARD RESET only upto one time */
    {
      TI_DBG1(("ossaSATACompleted: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; sending HARD_RESET\n"));
      pSatDevData->NumOfFCA++;
      tdsaPhyControlSend(tiRoot,
                         tdsaDeviceData,
                         SMP_PHY_CONTROL_HARD_RESET,
                         agNULL,
                         tdsaRotateQnumber(tiRoot, tdsaDeviceData)
                        );
    }
    else
    {
      /* given up after one time of SMP HARD RESET; */
      TI_DBG1(("ossaSATACompleted: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; NO!!! sending HARD_RESET\n"));
      if (tdsaDeviceData->registered == agTRUE && tdsaAllShared->ResetInDiscovery == 0)
      {
        /*
          1. remove this device
          2. device removal event
        */
        tdsaAbortAll(tiRoot, agRoot, tdsaDeviceData);
        tdsaDeviceData->valid = agFALSE;
        tdsaDeviceData->valid2 = agFALSE;
        tdsaDeviceData->registered = agFALSE;
//      pSatDevData->NumOfFCA = 0;
        ostiInitiatorEvent(
                            tiRoot,
                            onePortContext->tiPortalContext,
                            agNULL,
                            tiIntrEventTypeDeviceChange,
                            tiDeviceRemoval,
                            agNULL
                            );
      }
    }
  }

  if (agIOStatus == OSSA_IO_ABORTED)
  {
    /*
       free abort IO request itself - agParam; done in ossaSATAEvent()
    */
  }
  /* just for debugging */
  if (agIOStatus == OSSA_IO_DS_NON_OPERATIONAL)
  {
    TI_DBG1(("ossaSATACompleted: agIOStatus is OSSA_IO_DS_NON_OPERATIONAL\n"));
  }
  if (agIOStatus == OSSA_IO_DS_IN_RECOVERY)
  {
    TI_DBG1(("ossaSATACompleted: agIOStatus is OSSA_IO_DS_IN_RECOVERY\n"));
  }

  satIOContext->satCompleteCB( agRoot,
                               agIORequest,
                               agIOStatus,
                               agFirstDword,
                               agIOInfoLen,
                               agParam,
                               satIOContext);
ext:
  TDSA_OUT_LEAVE(tiRoot);
}

/*****************************************************************************
*! \brief  satPacketCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with normal Packet command I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/

void satPacketCB(
                 agsaRoot_t        *agRoot,
                 agsaIORequest_t   *agIORequest,
                 bit32             agIOStatus,
                 agsaFisHeader_t   *agFirstDword,
                 bit32             agIOInfoLen,
                 void              *agParam,
                 void              *ioContext
                 )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t          *pSense;
  tiIORequest_t           *tiOrgIORequest;
  tiIniScsiCmnd_t         *scsiCmnd;
  bit32                   interruptContext = osData->IntContext;
  bit8                    bSenseKey = 0;
  bit16                   bSenseCodeInfo = 0;
  bit32                   status = 0;

  TI_DBG4(("satPacketCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;

  /*ttttttthe one */
  if (satIntIo == agNULL)
  {
    TI_DBG4(("satPacketCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satOrgIOContext->pSense;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satPacketCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satPacketCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satPacketCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
    pSense                 = satOrgIOContext->pSense;
    scsiCmnd               = satOrgIOContext->pScsiCmnd;
  }

  /* Parse CDB */
  switch(scsiCmnd->cdb[0])
  {
    case SCSIOPC_TEST_UNIT_READY:
      //satTestUnitReadyCB(agRoot, agIORequest, agIOStatus, agFirstDword, agIOInfoLen, agParam, ioContext);
      //break;
    case SCSIOPC_GET_EVENT_STATUS_NOTIFICATION:
      //break;
    case SCSIOPC_READ_CAPACITY_10:
    case SCSIOPC_READ_CAPACITY_16:
      //satPacketReadCapacityCB(agRoot, agIORequest, agIOStatus, agFirstDword, agIOInfoLen, agParam, ioContext);
      //break;
    default:
       break;
   }

  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted   = agFALSE;

  /* interal structure free */
  satFreeIntIoResource( tiRoot, satDevData, satIntIo);

  if( agIOStatus == OSSA_IO_SUCCESS && agFirstDword == agNULL)
  {
    TI_DBG1(("satPacketCB: agIOStatus == OSSA_IO_SUCCESS, agFirstDword == agNULL \n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              interruptContext);
  }
  else if (agIOStatus == OSSA_IO_SUCCESS && agFirstDword != agNULL)
  {
      TI_DBG1(("satPacketCB: wrong. agIOStatus == OSSA_IO_SUCCESS && agFirstDword != agNULL \n"));
      satNewIntIo = satAllocIntIoResource( tiRoot,
                                       tiOrgIORequest,
                                       satDevData,
                                       32,
                                       satNewIntIo);
      if (satNewIntIo == agNULL)
      {
          /* memory allocation failure */
          /* just translate the ATAPI error register to sense information */
          satTranslateATAPIErrorsToSCSIErrors(
                          scsiCmnd->cdb[0],
                          agFirstDword->D2H.status,
                          agFirstDword->D2H.error,
                          &bSenseKey,
                          &bSenseCodeInfo
                          );
          satSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
          ostiInitiatorIOCompleted( tiRoot,
                                  tdIORequestBody->tiIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  interruptContext);
          TI_DBG1(("satPacketCB: momory allocation fails\n"));
          return;
      } /* end memory allocation */

      satNewIOContext = satPrepareNewIO(satNewIntIo,
                                        tiOrgIORequest,
                                        satDevData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );
      /* sends request sense to ATAPI device for acquiring sense information */
      status = satRequestSenseForATAPI(tiRoot,
                              &satNewIntIo->satIntTiIORequest,
                              satNewIOContext->ptiDeviceHandle,
                              &satNewIntIo->satIntTiScsiXchg,
                              satNewIOContext
                              );
      if (status != tiSuccess)
      {
          satFreeIntIoResource( tiRoot,
                                satDevData,
                                satNewIntIo);
          /* just translate the ATAPI error register to sense information */
          satTranslateATAPIErrorsToSCSIErrors(
                          scsiCmnd->cdb[0],
                          agFirstDword->D2H.status,
                          agFirstDword->D2H.error,
                          &bSenseKey,
                          &bSenseCodeInfo
                          );
          satSetSensePayload(pSense, bSenseKey, 0, bSenseCodeInfo, satOrgIOContext);
          ostiInitiatorIOCompleted( tiRoot,
                                  tdIORequestBody->tiIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  interruptContext);
          TI_DBG1(("satPacketCB: failed to call satRequestSenseForATAPI()\n"));
      }
  }
  else if (agIOStatus != OSSA_IO_SUCCESS)
  {
      TI_DBG1(("satPacketCB: wrong. agIOStatus != OSSA_IO_SUCCESS, status %d\n", agIOStatus));
      itdsatProcessAbnormalCompletion(
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
      TI_DBG1(("satPacketCB: Unknown error \n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tdIORequestBody->tiIORequest,
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                interruptContext);
  }
}
/*****************************************************************************
*! \brief  satRequestSenseForATAPICB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satRequestSenseForATAPICB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam,
                  void              *ioContext
                  )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;
  tiIORequest_t           *tiOrgIORequest;
  bit32                   interruptContext = osData->IntContext;

  TI_DBG4(("satPacketCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;

  /*ttttttthe one */
  if (satIntIo == agNULL)
  {
    TI_DBG4(("satPacketCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
  }
  else
  {
    TI_DBG4(("satPacketCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satPacketCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satPacketCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
  }

  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted   = agFALSE;

  /* copy the request sense buffer to original IO buffer*/
  if (satIntIo != agNULL)
  {
    osti_memcpy(satOrgIOContext->pTiSenseData->senseData, satIntIo->satIntDmaMem.virtPtr, SENSE_DATA_LENGTH);
    satOrgIOContext->pTiSenseData->senseLen = SENSE_DATA_LENGTH;
    /* interal structure free */
    satFreeIntIoResource( tiRoot, satDevData, satIntIo);
  }

  /* notify the OS to complete this SRB */
  ostiInitiatorIOCompleted( tiRoot,
              tiOrgIORequest,
              tiIOSuccess,
              SCSI_STAT_CHECK_CONDITION,
              satOrgIOContext->pTiSenseData,
              interruptContext);
}
/*****************************************************************************
*! \brief  satSetFeaturesPIOCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satSetFeaturesPIOCB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
    tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
    tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tdIORequestBody_t       *tdIORequestBody;
    tdIORequestBody_t       *tdOrgIORequestBody;
    satIOContext_t          *satIOContext;
    satIOContext_t          *satOrgIOContext;
    satIOContext_t          *satNewIOContext;
    satInternalIo_t         *satIntIo;
    satInternalIo_t         *satNewIntIo = agNULL;
    satDeviceData_t         *satDevData;
    tiIORequest_t           *tiOrgIORequest;
    tiIniScsiCmnd_t         *scsiCmnd;
    bit32                   status;

    TI_DBG3(("satSetFeaturesPIOCB start\n"));

    /* internally generate tiIOContext */
    tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
    satIOContext           = (satIOContext_t *) ioContext;
    satIntIo               = satIOContext->satIntIoContext;
    satDevData             = satIOContext->pSatDevData;

    /*ttttttthe one */
    if (satIntIo == agNULL)
    {
        TI_DBG4(("satSetFeaturesPIOCB: External satInternalIo_t satIntIoContext\n"));
        satOrgIOContext = satIOContext;
        tiOrgIORequest  = tdIORequestBody->tiIORequest;
        scsiCmnd        = satOrgIOContext->pScsiCmnd;
    }
    else
    {
        TI_DBG4(("satSetFeaturesPIOCB: Internal satInternalIo_t satIntIoContext\n"));
        satOrgIOContext = satIOContext->satOrgIOContext;
        if (satOrgIOContext == agNULL)
        {
            TI_DBG4(("satSetFeaturesPIOCB: satOrgIOContext is NULL, wrong\n"));
            return;
        }
        else
        {
            TI_DBG4(("satSetFeaturesPIOCB: satOrgIOContext is NOT NULL\n"));
        }
        tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
        tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
        scsiCmnd               = satOrgIOContext->pScsiCmnd;
    }
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    tdIORequestBody->ioCompleted = agTRUE;
    tdIORequestBody->ioStarted   = agFALSE;

    /* interal structure free */
    satFreeIntIoResource(tiRoot,
                         satDevData,
                         satIntIo);

    /*if the ATAPI device support DMA, then enble this feature*/
    if (satDevData->satDMASupport && satDevData->satDMAEnabled)
    {
        satNewIntIo = satAllocIntIoResource( tiRoot,
                                           tiOrgIORequest,
                                           satDevData,
                                           0,
                                           satNewIntIo);
        if (satNewIntIo == agNULL)
        {
            TI_DBG1(("satSetFeaturesPIOCB: momory allocation fails\n"));
            return;
        } /* end memory allocation */

        satNewIOContext = satPrepareNewIO(satNewIntIo,
                                          tiOrgIORequest,
                                          satDevData,
                                          scsiCmnd,
                                          satOrgIOContext
                                          );
        /* sends either ATA SET FEATURES based on DMA bit */
        status = satSetFeatures(tiRoot,
                                &satNewIntIo->satIntTiIORequest,
                                satNewIOContext->ptiDeviceHandle,
                                &satNewIntIo->satIntTiScsiXchg,
                                satNewIOContext,
                                agTRUE
                                );
        if (status != tiSuccess)
        {
            satFreeIntIoResource( tiRoot, satDevData, satNewIntIo);
            TI_DBG1(("satSetFeaturesPIOCB: failed to call satSetFeatures()\n"));
        }
    }
}

/*****************************************************************************
*! \brief  satSetFeaturesCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satSetFeaturesCB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
    tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
    tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tdIORequestBody_t       *tdIORequestBody;
    tdIORequestBody_t       *tdOrgIORequestBody = agNULL;
    satIOContext_t          *satIOContext;
    satIOContext_t          *satOrgIOContext;
    satInternalIo_t         *satIntIo;
    satDeviceData_t         *satDevData;
    tdsaPortContext_t       *onePortContext = agNULL;
    tiPortalContext_t       *tiPortalContext = agNULL;
    tdsaDeviceData_t        *oneDeviceData = agNULL;
    bit8                    PhyID =0;
    TI_DBG3(("satSetFeaturesCB start\n"));

    /* internally generate tiIOContext */
    tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
    satIOContext           = (satIOContext_t *) ioContext;
    satIntIo               = satIOContext->satIntIoContext;
    satDevData             = satIOContext->pSatDevData;
    oneDeviceData          = (tdsaDeviceData_t *)tdIORequestBody->tiDevHandle->tdData;
    onePortContext         = oneDeviceData->tdPortContext;
    if (onePortContext == agNULL)
    {
        TI_DBG4(("satSetFeaturesCB: onePortContext is  NULL, wrong\n"));
        return;
    }
    tiPortalContext        = onePortContext->tiPortalContext;
    PhyID                  = oneDeviceData->phyID;
    /*ttttttthe one */
    if (satIntIo == agNULL)
    {
        TI_DBG4(("satSetFeaturesCB: External satInternalIo_t satIntIoContext\n"));
        satOrgIOContext = satIOContext;
    }
    else
    {
        TI_DBG4(("satSetFeaturesCB: Internal satInternalIo_t satIntIoContext\n"));
        satOrgIOContext = satIOContext->satOrgIOContext;
        if (satOrgIOContext == agNULL)
        {
            TI_DBG4(("satSetFeaturesCB: satOrgIOContext is NULL, wrong\n"));
            return;
        }
        else
        {
            TI_DBG4(("satSetFeaturesCB: satOrgIOContext is NOT NULL\n"));
        }
        tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    }
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    tdIORequestBody->ioCompleted = agTRUE;
    tdIORequestBody->ioStarted   = agFALSE;

    /* interal structure free */
    satFreeIntIoResource(tiRoot,
                         satDevData,
                         satIntIo);


    /* clean up TD layer's IORequestBody */
    if (tdOrgIORequestBody!= agNULL)
    {
      ostiFreeMemory(tiRoot,
                     tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }

    if (onePortContext != agNULL)
    {
        /* this condition is for tdsaDiscoveryStartIDDevCB routine*/
        if (onePortContext->DiscoveryState == ITD_DSTATE_COMPLETED)
        {
            TI_DBG1(("satSetFeaturesCB: ID completed after discovery is done; tiDeviceArrival\n"));
            /* in case registration is finished after discovery is finished */
            ostiInitiatorEvent(
                             tiRoot,
                             tiPortalContext,
                             agNULL,
                             tiIntrEventTypeDeviceChange,
                             tiDeviceArrival,
                             agNULL
                             );
            return;
        }
        TI_DBG2(("satSetFeaturesCB: pid %d\n", tdsaAllShared->Ports[PhyID].portContext->id));
        /* the below codes is for satAddSATAIDDevCB routine*/
        /* notifying link up */
        ostiPortEvent (
                       tiRoot,
                       tiPortLinkUp,
                       tiSuccess,
                       (void *)tdsaAllShared->Ports[PhyID].tiPortalContext
                       );
         #ifdef INITIATOR_DRIVER
         /* triggers discovery */
         ostiPortEvent(
                      tiRoot,
                      tiPortDiscoveryReady,
                      tiSuccess,
                      (void *) tdsaAllShared->Ports[PhyID].tiPortalContext
                      );
        #endif
    }
    else
    {
        TI_DBG1(("satSetFeaturesCB: onePortContext is NULL, wrong\n"));
    }
}
/*****************************************************************************
*! \brief  satDeviceResetCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satDeviceResetCB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
 /* callback for satResetDevice */
   tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
   tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
   tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
   tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
   tdIORequestBody_t       *tdIORequestBody;
   tdIORequestBody_t       *tdOrgIORequestBody = agNULL;
   satIOContext_t          *satIOContext;
   satIOContext_t          *satOrgIOContext;
//   satIOContext_t          *satNewIOContext;
   satInternalIo_t         *satIntIo;
//   satInternalIo_t         *satNewIntIo = agNULL;
   satDeviceData_t         *satDevData;
   tiIORequest_t             *tiOrgIORequest;
#ifdef  TD_DEBUG_ENABLE
   bit32                     ataStatus = 0;
   bit32                     ataError;
   agsaFisPioSetupHeader_t   *satPIOSetupHeader = agNULL;
#endif
//   bit32                     status;
   bit32                     report = agFALSE;
   bit32                     AbortTM = agFALSE;

   TI_DBG1(("satDeviceResetCB: start\n"));

   TI_DBG6(("satDeviceResetCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

   tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
   satIOContext           = (satIOContext_t *) ioContext;
   satIntIo               = satIOContext->satIntIoContext;
   satDevData             = satIOContext->pSatDevData;

   if (satIntIo == agNULL)
   {
     TI_DBG6(("satDeviceResetCB: External, OS generated\n"));
     satOrgIOContext      = satIOContext;
     tiOrgIORequest       = tdIORequestBody->tiIORequest;
   }
   else
   {
     TI_DBG6(("satDeviceResetCB: Internal, TD generated\n"));
     satOrgIOContext        = satIOContext->satOrgIOContext;
     if (satOrgIOContext == agNULL)
     {
       TI_DBG6(("satDeviceResetCB: satOrgIOContext is NULL, wrong\n"));
       return;
     }
     else
     {
       TI_DBG6(("satDeviceResetCB: satOrgIOContext is NOT NULL\n"));
     }
     tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
     tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
   }

   tdIORequestBody->ioCompleted = agTRUE;
   tdIORequestBody->ioStarted = agFALSE;

   if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
   {
     TI_DBG1(("satDeviceResetCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));

     if (satOrgIOContext->NotifyOS == agTRUE)
     {
       ostiInitiatorEvent( tiRoot,
                           NULL,
                           NULL,
                           tiIntrEventTypeTaskManagement,
                           tiTMFailed,
                           tiOrgIORequest );
     }

     satDevData->satTmTaskTag = agNULL;

     satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

     satFreeIntIoResource( tiRoot,
                           satDevData,
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
     TI_DBG1(("satDeviceResetCB: OSSA_IO_OPEN_CNX_ERROR\n"));

     if (satOrgIOContext->NotifyOS == agTRUE)
     {
       ostiInitiatorEvent( tiRoot,
                           NULL,
                           NULL,
                           tiIntrEventTypeTaskManagement,
                           tiTMFailed,
                           tiOrgIORequest );
     }

     satDevData->satTmTaskTag = agNULL;

     satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

     satFreeIntIoResource( tiRoot,
                          satDevData,
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
     TI_DBG1(("satDeviceResetCB: ataStatus 0x%x ataError 0x%x\n", ataStatus, ataError));

      if (satOrgIOContext->NotifyOS == agTRUE)
      {
       ostiInitiatorEvent( tiRoot,
                           NULL,
                           NULL,
                           tiIntrEventTypeTaskManagement,
                           tiTMFailed,
                           tiOrgIORequest );
      }

     satDevData->satTmTaskTag = agNULL;

     satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

     satFreeIntIoResource( tiRoot,
                           satDevData,
                           satIntIo);
     return;
   }

   /* success */
  if (satOrgIOContext->TMF == AG_ABORT_TASK)
  {
    AbortTM = agTRUE;
  }

  if (satOrgIOContext->NotifyOS == agTRUE)
  {
    report = agTRUE;
  }

  if (AbortTM == agTRUE)
  {
    TI_DBG1(("satDeResetDeviceCB: calling satAbort\n"));
    satAbort(agRoot, satOrgIOContext->satToBeAbortedIOContext);
  }
  satDevData->satTmTaskTag = agNULL;

  satDevData->satDriveState = SAT_DEV_STATE_NORMAL;

  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  TI_DBG1(("satDeviceResetCB: satPendingIO %d satNCQMaxIO %d\n", satDevData->satPendingIO, satDevData->satNCQMaxIO ));
  TI_DBG1(("satDeviceResetCB: satPendingNCQIO %d satPendingNONNCQIO %d\n", satDevData->satPendingNCQIO, satDevData->satPendingNONNCQIO));

  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

  /* clean up TD layer's IORequestBody */
  if (tdOrgIORequestBody != agNULL)
  {
    ostiFreeMemory(
                   tiRoot,
                   tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
  }
  else
  {
    TI_DBG1(("satDeviceResetCB: tdOrgIORequestBody is NULL, wrong\n"));
  }


  if (report)
  {
    ostiInitiatorEvent( tiRoot,
                        NULL,
                        NULL,
                        tiIntrEventTypeTaskManagement,
                        tiTMOK,
                        tiOrgIORequest );
  }


  TI_DBG5(("satDeviceResetCB: device %p pending IO %d\n", satDevData, satDevData->satPendingIO));
  TI_DBG6(("satDeviceResetCB: end\n"));
  return;
}

/*****************************************************************************
*! \brief  satExecuteDeviceDiagnosticCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satExecuteDeviceDiagnosticCB(
    agsaRoot_t        *agRoot,
    agsaIORequest_t   *agIORequest,
    bit32             agIOStatus,
    agsaFisHeader_t   *agFirstDword,
    bit32             agIOInfoLen,
    void              *agParam,
    void              *ioContext
    )
{
    tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
    tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
    tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    tdIORequestBody_t       *tdIORequestBody;
    satIOContext_t          *satIOContext;
    satIOContext_t          *satOrgIOContext;
    satInternalIo_t         *satIntIo;
    satDeviceData_t         *satDevData;

    TI_DBG3(("satExecuteDeviceDiagnosticCB start\n"));

    /* internally generate tiIOContext */
    tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
    satIOContext           = (satIOContext_t *) ioContext;
    satIntIo               = satIOContext->satIntIoContext;
    satDevData             = satIOContext->pSatDevData;

    /*ttttttthe one */
    if (satIntIo == agNULL)
    {
        TI_DBG4(("satExecuteDeviceDiagnosticCB: External satInternalIo_t satIntIoContext\n"));
        satOrgIOContext = satIOContext;
    }
    else
    {
        TI_DBG4(("satExecuteDeviceDiagnosticCB: Internal satInternalIo_t satIntIoContext\n"));
        satOrgIOContext = satIOContext->satOrgIOContext;
        if (satOrgIOContext == agNULL)
        {
            TI_DBG4(("satExecuteDeviceDiagnosticCB: satOrgIOContext is NULL, wrong\n"));
            return;
        }
        else
        {
            TI_DBG4(("satExecuteDeviceDiagnosticCB: satOrgIOContext is NOT NULL\n"));
        }
    }
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    tdIORequestBody->ioCompleted = agTRUE;
    tdIORequestBody->ioStarted   = agFALSE;

    /* interal structure free */
    satFreeIntIoResource(tiRoot,
                         satDevData,
                         satIntIo);
}
/*****************************************************************************
*! \brief  satNonChainedDataIOCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/

void satNonChainedDataIOCB(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           void              *agParam,
                           void              *ioContext
                           )
{

  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t           *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t    *tdIORequestBody;
  bit32                interruptContext = osData->IntContext;
  satIOContext_t       *satIOContext;
  satInternalIo_t      *SatIntIo;
  satDeviceData_t      *SatDevData;

  TI_DBG6(("satNonChainedDataIOCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
    agIORequest, agIOStatus, agIOInfoLen));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext    = (satIOContext_t *) ioContext;
  SatIntIo               = satIOContext->satIntIoContext;
  SatDevData      = satIOContext->pSatDevData;
  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);
  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted   = agFALSE;

  /* interal structure free */
  satFreeIntIoResource( tiRoot,
                         SatDevData,
                         SatIntIo);

  /* Process completion */
  if( (agIOStatus == OSSA_IO_SUCCESS) && (agIOInfoLen == 0))
  {
    TI_DBG5(("satNonChainedDataIOCB: success\n"));
    TI_DBG5(("satNonChainedDataIOCB: success agIORequest %p\n", agIORequest));
    /*
     * Command was completed OK, this is the normal path.
     * Now call the OS-App Specific layer about this completion.
     */
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              interruptContext);
  }
  else
  {
    TI_DBG1(("satNonChainedDataIOCB: calling itdsatProcessAbnormalCompletion\n"));
    /* More checking needed */
    itdsatProcessAbnormalCompletion( agRoot,
                                     agIORequest,
                                     agIOStatus,
                                     agFirstDword,
                                     agIOInfoLen,
                                     agParam,
                                     satIOContext);
  }

  return;


}
/*****************************************************************************
*! \brief  satChainedDataIOCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with normal chained data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satChainedDataIOCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        void              *agParam,
                        void              *ioContext
                        )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status = tiError;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  bit32                     dataLength;

  TI_DBG6(("satChainedDataIOCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
           agIORequest, agIOStatus, agIOInfoLen));


  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  if (satIOContext == agNULL)
  {
    TI_DBG1(("satChainedDataIOCB: satIOContext is NULL\n"));
    return;
  }
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG5(("satChainedDataIOCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG5(("satChainedDataIOCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG5(("satChainedDataIOCB: satOrgIOContext is NULL\n"));
    }
    else
    {
      TI_DBG5(("satChainedDataIOCB: satOrgIOContext is NOT NULL\n"));
    }

    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     TI_DBG1(("satChainedDataIOCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
     satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

     ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      TI_DBG1(("satChainedDataIOCB: FAILED, Wrong FIS type 0x%x\n", statDevToHostFisHeader->fisType));
    }

    /* for debugging */
    if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      TI_DBG1(("satChainedDataIOCB: FAILED, error status and command 0x%x\n", hostToDevFis->h.command));
    }

    /* the function below handles abort case */
    itdsatDelayedProcessAbnormalCompletion(agRoot,
                                           agIORequest,
                                           agIOStatus,
                                           agFirstDword,
                                           agIOInfoLen,
                                           agParam,
                                           satIOContext);

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);
    satFreeIntIoResource( tiRoot,
                          satDevData,
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

    TI_DBG5(("satChainedDataIOCB: READ/WRITE success case\n"));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with internally genereated SAT_SMART_RETURN_STATUS */
    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
      return;
    }
    if (satOrgIOContext->superIOFlag)
    {
      dataLength = ((tiSuperScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }
    else
    {
      dataLength = ((tiScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }

    satNewIntIo = satAllocIntIoResource( tiRoot,
                                         tiOrgIORequest,
                                         satDevData,
                                         dataLength,
                                         satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* memory allocation failure */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
       ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                satOrgIOContext->interruptContext );

      TI_DBG1(("satChainedDataIOCB: momory allocation fails\n"));
      return;
    } /* end of memory allocation failure */

       /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = satPrepareNewIO(
                                      satNewIntIo,
                                      tiOrgIORequest,
                                      satDevData,
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
      status = satRead_1( tiRoot,
                          &satNewIntIo->satIntTiIORequest,
                          satNewIOContext->ptiDeviceHandle,
                          &satNewIntIo->satIntTiScsiXchg,
                          satNewIOContext);
      break;
    case SCSIOPC_WRITE_6:
      /* no loop should occur with WRITE6 since it fits in one ATA command */
      break;
    case SCSIOPC_WRITE_10: /* fall through */
    case SCSIOPC_WRITE_12: /* fall through */
    case SCSIOPC_WRITE_16: /* fall through */
      status = satWrite_1( tiRoot,
                           &satNewIntIo->satIntTiIORequest,
                           satNewIOContext->ptiDeviceHandle,
                           &satNewIntIo->satIntTiScsiXchg,
                             satNewIOContext);
      break;
    default:
      TI_DBG1(("satChainedDataIOCB: success but default case scsi cmd 0x%x ata cmd 0x%x\n",scsiCmnd->cdb[0], hostToDevFis->h.command));
      status = tiError;
      break;
    }



    if (status != tiSuccess)
    {
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                satOrgIOContext->interruptContext );
      TI_DBG1(("satChainedDataIOCB: calling satRead10_1 fails\n"));
      return;
    }

    break;


  default:
    TI_DBG1(("satChainedDataIOCB: success but default case command 0x%x\n",hostToDevFis->h.command));
    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    break;
  }


  return;
}
void satNonChainedWriteNVerifyCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        void              *agParam,
                        void              *ioContext
                        )
{

  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  tiScsiInitiatorRequest_t *tiScsiRequest; /* tiScsiXchg */
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  agsaFisSetDevBitsHeader_t *statSetDevBitFisHeader = agNULL;

  TI_DBG5(("satNonChainedWriteNVerifyCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  /* SPC: Self-Test Result Log page */
  tiScsiRequest          = satIOContext->tiScsiXchg;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satNonChainedWriteNVerifyCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satOrgIOContext->pSense;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satNonChainedWriteNVerifyCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satNonChainedWriteNVerifyCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satNonChainedWriteNVerifyCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;


  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satNonChainedWriteNVerifyCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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

      /* ATA Eror register   */
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
        TI_DBG1(("satNonChainedWriteNVerifyCB: FAILED, NOT IO_SUCCESS\n"));
      }
      else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
      {
        TI_DBG1(("satNonChainedWriteNVerifyCB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
      }
      else if (statDevToHostFisHeader->fisType != SET_DEV_BITS_FIS)
      {
        TI_DBG1(("satNonChainedWriteNVerifyCB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
      }
      else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
                (ataStatus & DF_ATA_STATUS_MASK)
                )
      {
        TI_DBG1(("satNonChainedWriteNVerifyCB: FAILED, FAILED, error status\n"));
      }


      /* Process abort case */
      if (agIOStatus == OSSA_IO_ABORTED)
      {
        satProcessAbort(tiRoot,
                        tiOrgIORequest,
                        satOrgIOContext
                        );

        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
        return;
      }

      /* for debugging */
      switch (hostToDevFis->h.command)
      {
      case SAT_WRITE_DMA_FUA_EXT:
        TI_DBG1(("satNonChainedWriteNVerifyCB: SAT_WRITE_DMA_FUA_EXT\n"));
        break;
      case SAT_WRITE_DMA_EXT:
        TI_DBG1(("satNonChainedWriteNVerifyCB: SAT_WRITE_DMA_EXT\n"));
        break;
      case SAT_WRITE_SECTORS_EXT:
        TI_DBG1(("satNonChainedWriteNVerifyCB: SAT_WRITE_SECTORS_EXT\n"));
        break;
      case SAT_WRITE_FPDMA_QUEUED:
        TI_DBG1(("satNonChainedWriteNVerifyCB: SAT_WRITE_FPDMA_QUEUED\n"));
        break;
      case SAT_READ_VERIFY_SECTORS:
        TI_DBG1(("satNonChainedWriteNVerifyCB: SAT_READ_VERIFY_SECTORS\n"));
        break;
      case SAT_READ_VERIFY_SECTORS_EXT:
        TI_DBG1(("satNonChainedWriteNVerifyCB: SAT_READ_VERIFY_SECTORS_EXT\n"));
        break;
      default:
        TI_DBG1(("satNonChainedWriteNVerifyCB: error default case command 0x%x\n", hostToDevFis->h.command));
        break;
      }

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    } /* end error checking */
  }

  /* process success from this point on */

  switch (hostToDevFis->h.command)
  {
  case SAT_WRITE_DMA_FUA_EXT:
    TI_DBG5(("satNonChainedWriteNVerifyCB: SAT_WRITE_DMA_FUA_EXT success\n"));
    break;
  case SAT_WRITE_DMA_EXT:
    TI_DBG5(("satNonChainedWriteNVerifyCB: SAT_WRITE_DMA_EXT success\n"));
    break;
  case SAT_WRITE_SECTORS_EXT:
    TI_DBG5(("satNonChainedWriteNVerifyCB: SAT_WRITE_SECTORS_EXT succes\n"));

    break;
  case SAT_WRITE_FPDMA_QUEUED:
    TI_DBG5(("satNonChainedWriteNVerifyCB: SAT_WRITE_FPDMA_QUEUED succes\n"));
    break;
  case SAT_READ_VERIFY_SECTORS:
    TI_DBG5(("satNonChainedWriteNVerifyCB: SAT_READ_VERIFY_SECTORS succes\n"));
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* free */
    satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

    /* return stat_good */
    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext );
    return;
    break;
  case SAT_READ_VERIFY_SECTORS_EXT:
    TI_DBG5(("satNonChainedWriteNVerifyCB: SAT_READ_VERIFY_SECTORS_EXT succes\n"));
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* free */
    satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

    /* return stat_good */
    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext );
    return;
    break;
  default:
    TI_DBG1(("satNonChainedWriteNVerifyCB:  error default case command 0x%x success\n", hostToDevFis->h.command));

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
    break;
  }

  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  /* free */
  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

  satNewIntIo = satAllocIntIoResource( tiRoot,
                                       tiOrgIORequest,
                                       satDevData,
                                       0,
                                       satNewIntIo);
  if (satNewIntIo == agNULL)
  {
    /* memory allocation failure */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satNewIntIo);

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );
    TI_DBG1(("satNonChainedWriteNVerifyCB: momory allocation fails\n"));
    return;
  } /* end memory allocation */

  satNewIOContext = satPrepareNewIO(
                                    satNewIntIo,
                                    tiOrgIORequest,
                                    satDevData,
                                    scsiCmnd,
                                    satOrgIOContext
                                    );

  /* sends ATA verify command(READ_VERIFY_SECTORS or READ_VERIFY_SECTORS_EXT) */
  status = satNonChainedWriteNVerify_Verify(tiRoot,
                                             &satNewIntIo->satIntTiIORequest,
                                             satNewIOContext->ptiDeviceHandle,
                                             tiScsiRequest, /* orginal from OS layer */
                                             satNewIOContext
                                             );


  if (status != tiSuccess)
  {
    /* sending ATA command fails */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satNewIntIo);
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );
    TI_DBG1(("satNonChainedWriteNVerifyCB: calling satWriteAndVerify10_1 fails\n"));
    return;
  } /* end send fails */

  return;
}


void satChainedWriteNVerifyCB(
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
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     dataLength;
  bit32                     status = tiError;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;

  TI_DBG6(("satChainedWriteNVerifyCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
           agIORequest, agIOStatus, agIOInfoLen));


  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG5(("satChainedWriteNVerifyCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG5(("satChainedWriteNVerifyCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG5(("satChainedWriteNVerifyCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG5(("satChainedWriteNVerifyCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     TI_DBG1(("satChainedWriteNVerifyCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
     satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

     ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      TI_DBG1(("satChainedWriteNVerifyCB: FAILED, Wrong FIS type 0x%x\n", statDevToHostFisHeader->fisType));
    }

    /* for debugging */
    if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      TI_DBG1(("satChainedWriteNVerifyCB: FAILED, error status and command 0x%x\n", hostToDevFis->h.command));
    }

    /* the function below handles abort case */
    itdsatDelayedProcessAbnormalCompletion(agRoot,
                                           agIORequest,
                                           agIOStatus,
                                           agFirstDword,
                                           agIOInfoLen,
                                           agParam,
                                           satIOContext);

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);
    satFreeIntIoResource( tiRoot,
                          satDevData,
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

    TI_DBG5(("satChainedWriteNVerifyCB: WRITE success case\n"));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with internally genereated SAT_SMART_RETURN_STATUS */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    /* let's loop till TL */

  
    (satOrgIOContext->LoopNum)--;
  
    if (satOrgIOContext->superIOFlag)
    {
      dataLength = ((tiSuperScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }
    else
    {
      dataLength = ((tiScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }

    satNewIntIo = satAllocIntIoResource( tiRoot,
                                         tiOrgIORequest,
                                         satDevData,
                                         dataLength,
                                         satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* memory allocation failure */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                satOrgIOContext->interruptContext );

      TI_DBG1(("satChainedWriteNVerifyCB: momory allocation fails\n"));
      return;
    } /* end of memory allocation failure */

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = satPrepareNewIO(
                                      satNewIntIo,
                                      tiOrgIORequest,
                                      satDevData,
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
      status = satChainedWriteNVerify_Start_Verify(tiRoot,
                                    &satNewIntIo->satIntTiIORequest,
                                    satNewIOContext->ptiDeviceHandle,
                                    &satNewIntIo->satIntTiScsiXchg,
                                    satNewIOContext);
    }
    else
    {
      status = satChainedWriteNVerify_Write(tiRoot,
                                    &satNewIntIo->satIntTiIORequest,
                                    satNewIOContext->ptiDeviceHandle,
                                    &satNewIntIo->satIntTiScsiXchg,
                                    satNewIOContext);
    }

    if (status != tiSuccess)
    {
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                satOrgIOContext->interruptContext );
      TI_DBG1(("satChainedWriteNVerifyCB: calling satChainedWriteNVerify_Write fails\n"));
      return;
    }

    break;

  case SAT_READ_VERIFY_SECTORS: /* fall through */
  case SAT_READ_VERIFY_SECTORS_EXT:
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with internally genereated SAT_SMART_RETURN_STATUS */
    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
      return;
    }

    if (satOrgIOContext->superIOFlag)
    {
      dataLength = ((tiSuperScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }
    else
    {
      dataLength = ((tiScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }

    satNewIntIo = satAllocIntIoResource( tiRoot,
                                         tiOrgIORequest,
                                         satDevData,
                                         dataLength,
                                         satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* memory allocation failure */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                satOrgIOContext->interruptContext );

      TI_DBG1(("satChainedWriteNVerifyCB: momory allocation fails\n"));
      return;
    } /* end of memory allocation failure */

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = satPrepareNewIO(
                                      satNewIntIo,
                                      tiOrgIORequest,
                                      satDevData,
                                      scsiCmnd,
                                      satOrgIOContext
                                      );
    status = satChainedWriteNVerify_Verify(tiRoot,
                                    &satNewIntIo->satIntTiIORequest,
                                    satNewIOContext->ptiDeviceHandle,
                                    &satNewIntIo->satIntTiScsiXchg,
                                    satNewIOContext);

    if (status != tiSuccess)
    {
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                satOrgIOContext->interruptContext );
      TI_DBG1(("satChainedWriteNVerifyCB: calling satChainedWriteNVerify_Verify fails\n"));
      return;
    }

    break;

  default:
    TI_DBG1(("satChainedWriteNVerifyCB: success but default case command 0x%x\n",hostToDevFis->h.command));
    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    break;
  }


  return;
}
/*****************************************************************************
*! \brief  itdsatProcessAbnormalCompletion
*
*   This routine is called to complete error case for SATA request previously
*   issued to the LL Layer in saSATAStart()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void  itdsatProcessAbnormalCompletion(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           void              *agParam,
                           satIOContext_t    *satIOContext
                           )
{

  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                interruptContext = osData->IntContext;
  tdIORequestBody_t    *tdIORequestBody;
  satDeviceData_t      *pSatDevData;
  tiDeviceHandle_t     *tiDeviceHandle;
  tdsaDeviceData_t     *oneDeviceData = agNULL;
  agsaDevHandle_t      *agDevHandle = agNULL;

  TI_DBG5(("itdsatProcessAbnormalCompletion: agIORequest=%p agIOStatus=0x%x agIOInfoLen=%d\n",
          agIORequest, agIOStatus, agIOInfoLen));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  pSatDevData     = satIOContext->pSatDevData;
  tiDeviceHandle  = satIOContext->ptiDeviceHandle;

  /* Get into the detail */
  switch(agIOStatus)
  {
  case OSSA_IO_SUCCESS:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_SUCCESS agIOInfoLen %d calling osSatIOCompleted\n", agIOInfoLen));
    /*
     * At this point agIOInfoLen should be non-zero and there is valid FIS
     * to read. Pass this info to the SAT layer in order to do the ATA status
     * to SCSI status translation.
     */
      osSatIOCompleted( tiRoot,
                        tdIORequestBody->tiIORequest,
                        agFirstDword,
                        agIOInfoLen,
                        agParam,
                        satIOContext,
                        interruptContext);
    break;


  case OSSA_IO_ABORTED:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_ABORTED\n"));

    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailAborted,
                              agNULL,
                              interruptContext);

    if ( pSatDevData->satTmTaskTag != agNULL )
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: TM callback\n"));
      if (tiDeviceHandle == agNULL)
      {
        TI_DBG1(("itdsatProcessAbnormalCompletion: wrong, tiDeviceHandle is NULL\n"));
      }
      /* TM completed */
      ostiInitiatorEvent( tiRoot,
                          agNULL,               /* portalContext not used */
                          tiDeviceHandle,
                          tiIntrEventTypeTaskManagement,
                          tiTMOK,
                          pSatDevData->satTmTaskTag);
      /*
       * Reset flag
       */
      pSatDevData->satTmTaskTag = agNULL;
    }

    /*
     * Check if we are in recovery mode and need to update the recovery flag
     */
    if ((pSatDevData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
        (pSatDevData->satPendingIO == 0 ))
    {
      pSatDevData->satDriveState = SAT_DEV_STATE_NORMAL;
      TI_DBG1(("itdsatProcessAbnormalCompletion: STATE NORMAL.\n"));
    }

    TI_DBG1(("itdsatProcessAbnormalCompletion: satDriveState %d\n", pSatDevData->satDriveState));
    TI_DBG1(("itdsatProcessAbnormalCompletion: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    TI_DBG1(("itdsatProcessAbnormalCompletion: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));

    break;
  case OSSA_IO_UNDERFLOW:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_UNDERFLOW\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOUnderRun,
                              agIOInfoLen,
                              agNULL,
                              interruptContext);
    break;


  case OSSA_IO_FAILED:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_FAILED\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;

  case OSSA_IO_ABORT_RESET:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_ABORT_RESET\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailAbortReset,
                              agNULL,
                              interruptContext);
    /*
     * Check if we are in recovery mode and need to update the recovery flag
     */
    if ((pSatDevData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
        (pSatDevData->satPendingIO == 0 ))
    {
      pSatDevData->satDriveState = SAT_DEV_STATE_NORMAL;
      TI_DBG1(("itdsatProcessAbnormalCompletion: STATE NORMAL.\n"));
    }

    TI_DBG1(("itdsatProcessAbnormalCompletion: satDriveState %d\n", pSatDevData->satDriveState));
    TI_DBG1(("itdsatProcessAbnormalCompletion: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    TI_DBG1(("itdsatProcessAbnormalCompletion: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));

    break;


  case OSSA_IO_NO_DEVICE:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_NO_DEVICE\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailNoLogin,
                              agNULL,
                              interruptContext);
    break;

  case OSSA_IO_PROG_ERROR:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_PROG_ERROR\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;
  case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_BREAK: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR: /* fall through */
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_OPEN_CNX_ERROR_* 0x%x\n", agIOStatus));
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: wrong, tiDeviceHandle is NULL\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tdIORequestBody->tiIORequest,
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                interruptContext);
      return;
    }
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: wrong, oneDeviceData is NULL\n"));
    }
    else
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: did %d\n", oneDeviceData->id));
    }

    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError, //tiDetailNoDeviceError, //tiDetailAborted,
                              agNULL,
                              interruptContext);
    break;

  case OSSA_IO_XFER_ERROR_BREAK: /* fall throuth */
  case OSSA_IO_XFER_ERROR_PHY_NOT_READY: /* fall throuth */
  case OSSA_IO_XFER_ERROR_PEER_ABORTED: /* fall throuth */
  case OSSA_IO_XFER_ERROR_DMA: /* fall throuth */
  case OSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_ABORTED_DUE_TO_SRST: /* fall throuth */
  case OSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE: /* fall throuth */
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_XFER_ERROR_* 0x%x\n", agIOStatus));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;
  case OSSA_IO_DS_IN_ERROR:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_DS_IN_ERROR\n"));
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: wrong, tiDeviceHandle is NULL\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tdIORequestBody->tiIORequest,
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                interruptContext);
      return;
    }
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: wrong, oneDeviceData is NULL\n"));
    }
    else
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: did %d\n", oneDeviceData->id));
    }
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;
  case OSSA_IO_DS_NON_OPERATIONAL:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = OSSA_IO_DS_NON_OPERATIONAL\n"));
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: wrong, tiDeviceHandle is NULL\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tdIORequestBody->tiIORequest,
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                interruptContext);
      return;
    }
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: wrong, oneDeviceData is NULL\n"));
    }
    else
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: did %d\n", oneDeviceData->id));
      agDevHandle = oneDeviceData->agDevHandle;
      if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE &&
          oneDeviceData->tdPortContext != agNULL )
      {
        saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, SA_DS_OPERATIONAL);
      }
    }

    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;
  case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS: /* fall through */
  case OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = ENCRYPTION ERROR 0x%x\n", agIOStatus));
    itdsatEncryptionHandler(agRoot,
                            agIORequest,
                            agIOStatus,
                            agIOInfoLen,
                            agParam,
                            0);
    break;
  case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = DIF ERROR 0x%x\n", agIOStatus));
    itdsatDifHandler(agRoot,
                     agIORequest,
                     agIOStatus,
                     agIOInfoLen,
                     agParam,
                     0);
    break;
  default:
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    TI_DBG1(("itdsatProcessAbnormalCompletion: agIOStatus = unknown 0x%x\n", agIOStatus));
    if (oneDeviceData != agNULL)
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: did %d\n", oneDeviceData->id));
    }
    else
    {
      TI_DBG1(("itdsatProcessAbnormalCompletion: oneDeviceData is NULL\n"));
    }

    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;

  } /* switch */
}


/*****************************************************************************
*! \brief  itdsatDelayedProcessAbnormalCompletion
*
*   This routine is called to complete error case for SATA request previously
*   issued to the LL Layer in saSATAStart().
*   This is used when command is chained.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void  itdsatDelayedProcessAbnormalCompletion(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           void              *agParam,
                           satIOContext_t    *satIOContext
                           )
{

  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                interruptContext = osData->IntContext;
  tdIORequestBody_t    *tdIORequestBody;
  satDeviceData_t      *pSatDevData;
  tiDeviceHandle_t     *tiDeviceHandle;
  tdsaDeviceData_t     *oneDeviceData = agNULL;
  agsaDevHandle_t      *agDevHandle = agNULL;

  TI_DBG5(("itdsatDelayedProcessAbnormalCompletion: agIORequest=%p agIOStatus=0x%x agIOInfoLen=%d\n",
          agIORequest, agIOStatus, agIOInfoLen));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  pSatDevData     = satIOContext->pSatDevData;
  tiDeviceHandle  = satIOContext->ptiDeviceHandle;

  /* Get into the detail */
  switch(agIOStatus)
  {
  case OSSA_IO_SUCCESS:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_SUCCESS calling osSatIOCompleted\n"));
    /* do nothing */
    break;


  case OSSA_IO_ABORTED:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_ABORTED\n"));

    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailAborted,
                              agNULL,
                              interruptContext);

    if ( pSatDevData->satTmTaskTag != agNULL )
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: TM callback\n"));
      if (tiDeviceHandle == agNULL)
      {
        TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: wrong, tiDeviceHandle is NULL\n"));
      }
      /* TM completed */
      ostiInitiatorEvent( tiRoot,
                          agNULL,               /* portalContext not used */
                          tiDeviceHandle,
                          tiIntrEventTypeTaskManagement,
                          tiTMOK,
                          pSatDevData->satTmTaskTag);
      /*
       * Reset flag
       */
      pSatDevData->satTmTaskTag = agNULL;
    }

    /*
     * Check if we are in recovery mode and need to update the recovery flag
     */
    if ((pSatDevData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
        (pSatDevData->satPendingIO == 0 ))
    {
      pSatDevData->satDriveState = SAT_DEV_STATE_NORMAL;
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: STATE NORMAL.\n"));
    }

    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: satDriveState %d\n", pSatDevData->satDriveState));
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));

    break;
  case OSSA_IO_UNDERFLOW:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_UNDERFLOW\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOUnderRun,
                              agIOInfoLen,
                              agNULL,
                              interruptContext);
    break;


  case OSSA_IO_FAILED:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_FAILED\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;

  case OSSA_IO_ABORT_RESET:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_ABORT_RESET\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailAbortReset,
                              agNULL,
                              interruptContext);
    /*
     * Check if we are in recovery mode and need to update the recovery flag
     */
    if ((pSatDevData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
        (pSatDevData->satPendingIO == 0 ))
    {
      pSatDevData->satDriveState = SAT_DEV_STATE_NORMAL;
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: STATE NORMAL.\n"));
    }

    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: satDriveState %d\n", pSatDevData->satDriveState));
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));

    break;


  case OSSA_IO_NO_DEVICE:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_NO_DEVICE\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailNoLogin,
                              agNULL,
                              interruptContext);
    break;

  case OSSA_IO_PROG_ERROR:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_PROG_ERROR\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;
  case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_BREAK: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION: /* fall through */
  case OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR: /* fall through */
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_OPEN_CNX_ERROR_* 0x%x\n", agIOStatus));
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: wrong, tiDeviceHandle is NULL\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tdIORequestBody->tiIORequest,
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                interruptContext);
      return;
    }
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: wrong, oneDeviceData is NULL\n"));
    }
    else
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: did %d\n", oneDeviceData->id));
    }
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError, //tiDetailNoDeviceError, //tiDetailAborted,
                              agNULL,
                              interruptContext);
    break;

  case OSSA_IO_XFER_ERROR_BREAK: /* fall throuth */
  case OSSA_IO_XFER_ERROR_PHY_NOT_READY: /* fall throuth */
  case OSSA_IO_XFER_ERROR_PEER_ABORTED: /* fall throuth */
  case OSSA_IO_XFER_ERROR_DMA: /* fall throuth */
  case OSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT: /* fall throuth */
  case OSSA_IO_XFER_ERROR_ABORTED_DUE_TO_SRST: /* fall throuth */
  case OSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE: /* fall throuth */
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_XFER_ERROR_* 0x%x\n", agIOStatus));

    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;
  case OSSA_IO_DS_IN_ERROR:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_DS_IN_ERROR\n"));
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: wrong, tiDeviceHandle is NULL\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tdIORequestBody->tiIORequest,
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                interruptContext);
      return;
    }
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: wrong, oneDeviceData is NULL\n"));
    }
    else
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: did %d\n", oneDeviceData->id));
    }
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;
  case OSSA_IO_DS_NON_OPERATIONAL:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = OSSA_IO_DS_NON_OPERATIONAL\n"));
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: wrong, tiDeviceHandle is NULL\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tdIORequestBody->tiIORequest,
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                interruptContext);
      return;
    }
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: wrong, oneDeviceData is NULL\n"));
    }
    else
    {
      TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: did %d\n", oneDeviceData->id));
      agDevHandle = oneDeviceData->agDevHandle;
      if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE &&
          oneDeviceData->tdPortContext != agNULL )
      {
        saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, SA_DS_OPERATIONAL);
      }
    }
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;
  case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS: /* fall through */
  case OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = ENCRYPTION ERROR 0x%x\n", agIOStatus));
    itdsatEncryptionHandler(agRoot,
                            agIORequest,
                            agIOStatus,
                            agIOInfoLen,
                            agParam,
                            0);
      break;
  case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH: /* fall through */
  case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = DIF ERROR 0x%x\n", agIOStatus));
    itdsatDifHandler(agRoot,
                     agIORequest,
                     agIOStatus,
                     agIOInfoLen,
                     agParam,
                     0);
      break;
  default:
    TI_DBG1(("itdsatDelayedProcessAbnormalCompletion: agIOStatus = unknown\n"));
    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
    break;

  } /* switch */
}

/*****************************************************************************
*! \brief itdsatEncryptionHandler
*
*  Purpose:  This function processes I/Os completed and returned by SATA lower
*            layer with any encryption specific agIOStatus.
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdsatEncryptionHandler(
                       agsaRoot_t              *agRoot,
                       agsaIORequest_t         *agIORequest,
                       bit32                   agIOStatus,
                       bit32                   agIOInfoLen,
                       void                    *agParam,
                       bit32                   agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  bit32                  errorDetail = tiDetailOtherError;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdsatEncryptionHandler: start\n"));
  TI_DBG1(("itdsatEncryptionHandler: agIOStatus 0x%x\n", agIOStatus));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  switch (agIOStatus)
  {
  case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS:
      TI_DBG1(("itdsatEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS\n"));
      errorDetail = tiDetailDekKeyCacheMiss;
      break;
  case OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID:
      TI_DBG1(("itdsatEncryptionHandler: OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID\n"));
      errorDetail = tiDetailCipherModeInvalid;
      break;
  case OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH:
      TI_DBG1(("itdsatEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH\n"));
      errorDetail = tiDetailDekIVMismatch;
      break;
  case OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR:
      TI_DBG1(("itdsatEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR\n"));
      errorDetail = tiDetailDekRamInterfaceError;
      break;
  default:
      TI_DBG1(("itdsatEncryptionHandler: other error!!! 0x%x\n", agIOStatus));
      errorDetail = tiDetailOtherError;
      break;
  }

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOEncryptError,
                            errorDetail,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************
*! \brief itdsatDifHandler
*
*  Purpose:  This function processes I/Os completed and returned by SATA lower
*            layer with any DIF specific agIOStatus.
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdsatDifHandler(
                 agsaRoot_t              *agRoot,
                 agsaIORequest_t         *agIORequest,
                 bit32                   agIOStatus,
                 bit32                   agIOInfoLen,
                 void                    *agParam,
                 bit32                   agOtherInfo
                )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  bit32                  errorDetail = tiDetailOtherError;
  tdIORequestBody_t      *tdIORequestBody;
#ifdef  TD_DEBUG_ENABLE
  agsaDifDetails_t       *DifDetail;
#endif

  TI_DBG2(("itdsatDifHandler: start\n"));
  TI_DBG2(("itdsatDifHandler: agIOStatus 0x%x\n", agIOStatus));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
#ifdef  TD_DEBUG_ENABLE
  DifDetail = (agsaDifDetails_t *)agParam;
#endif
  switch (agIOStatus)
  {
  case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH:
      TI_DBG1(("itdsatDifHandler: OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH\n"));
      errorDetail = tiDetailDifAppTagMismatch;
      break;
  case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH:
      TI_DBG1(("itdsatDifHandler: OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH\n"));
      errorDetail = tiDetailDifRefTagMismatch;
      break;
  case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
      TI_DBG1(("itdsatDifHandler: OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH\n"));
      errorDetail = tiDetailDifCrcMismatch;
      break;
  default:
      TI_DBG1(("itdsatDifHandler: other error!!! 0x%x\n", agIOStatus));
      errorDetail = tiDetailOtherError;
      break;
  }

  TI_DBG1(("smsatDifHandler: DIF detail UpperLBA 0x%08x LowerLBA 0x%08x\n", DifDetail->UpperLBA, DifDetail->LowerLBA));

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIODifError,
                            errorDetail,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************/
/*! \brief satProcessAbort
 *
 *  This function processes abort.
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return
 *           None
 */
/*****************************************************************************/
void  satProcessAbort(
                      tiRoot_t          *tiRoot,
                      tiIORequest_t     *tiIORequest,
                      satIOContext_t    *satIOContext
                      )
{
  satDeviceData_t           *pSatDevData;
  //tiDeviceHandle_t          *tiDeviceHandle;
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;

  TI_DBG5(("satProcessAbort: start\n"));

  pSatDevData     = satIOContext->pSatDevData;
  //tiDeviceHandle  = satIOContext->ptiDeviceHandle;
  hostToDevFis    = satIOContext->pFis;
  if ( (hostToDevFis->h.command == SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE) &&
       (hostToDevFis->d.lbaLow != 0x01 && hostToDevFis->d.lbaLow != 0x02)
      )
  {
    /* no completion for send diagnotic in background. It is done in satSendDiagnostic() */
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOFailed,
                              tiDetailAborted,
                              agNULL,
                              satIOContext->interruptContext);
  }

  if ( pSatDevData->satTmTaskTag != agNULL )
  {
    TI_DBG1(("satProcessAbort: TM callback\n"));
    /*
     * Reset flag
     */
    pSatDevData->satTmTaskTag = agNULL;
  }

  /*
   * Check if we are in recovery mode and need to update the recovery flag
   */
  if ((pSatDevData->satDriveState == SAT_DEV_STATE_IN_RECOVERY ) &&
      (pSatDevData->satPendingIO == 0 ))
  {
    pSatDevData->satDriveState = SAT_DEV_STATE_NORMAL;
    TI_DBG1(("satProcessAbort: STATE NORMAL.\n"));
  }
  TI_DBG1(("satProcessAbort: satDriveState %d\n", pSatDevData->satDriveState));
  TI_DBG1(("satProcessAbort: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
  TI_DBG1(("satProcessAbort: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));



  return;
}

/*****************************************************************************
*! \brief  satNonDataIOCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with non-data I/O SATA request.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satNonDataIOCB(
                    agsaRoot_t        *agRoot,
                    agsaIORequest_t   *agIORequest,
                    bit32             agIOStatus,
                    agsaFisHeader_t   *agFirstDword,
                    bit32             agIOInfoLen,
                    void              *agParam,
                    void              *ioContext
                    )
{

  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t           *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t    *tdIORequestBody;
  bit32                interruptContext = osData->IntContext;
  satIOContext_t       *satIOContext;

  TI_DBG5(("satNonDataIOCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
    agIORequest, agIOStatus, agIOInfoLen));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext    = (satIOContext_t *) ioContext;
  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  /* Process completion */
  if( (agIOStatus == OSSA_IO_SUCCESS) && (agIOInfoLen==0))
  {
    /*
     * !!! We expect that agIOInfoLen should be non-zero !!!!.
     * Now call the OS-App Specific layer about this unexpected completion.
     */
    TI_DBG1(("satNonDataIOCB: *** ERROR***  agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
      agIORequest, agIOStatus, agIOInfoLen));

    ostiInitiatorIOCompleted( tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              interruptContext);
  }
  else
  {
    /* More checking needed, for non-data IO this should be the normal case */
    itdsatProcessAbnormalCompletion( agRoot,
                                     agIORequest,
                                     agIOStatus,
                                     agFirstDword,
                                     agIOInfoLen,
                                     agParam,
                                     satIOContext);
  }

}

/*****************************************************************************
*! \brief  tdssSATADeviceTypeDecode
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
tdssSATADeviceTypeDecode(
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

/*****************************************************************************
*! \brief ossaDiscoverSataCB
*
*  Purpose:  This function is called by lower layer to inform TD layer of
*            STP/SATA discovery results
*
*
*  \param   agRoot         Pointer to chip/driver Instance.
*  \param   agPortContext  Pointer to the port context of TD and Lower layer
*  \param   event          event type
*  \param   pParm1         Pointer to data associated with event
*  \param   pParm2         Pointer to data associated with event
*
*  \return: none
*
*  \note -  For details, refer to SAS/SATA Low-Level API Specification
*
*****************************************************************************/

osGLOBAL void ossaDiscoverSataCB( agsaRoot_t        *agRoot,
                                  agsaPortContext_t *agPortContext,
                                  bit32             event,
                                  void              *pParm1,
                                  void              *pParm2
                                  )
{
  tdsaRootOsData_t      *osData;
  tiRoot_t              *tiRoot;
  tdsaPortContext_t     *onePortContext;
  tdsaDeviceData_t      *oneDeviceData;
  agsaDevHandle_t       *agDevHandle;
  agsaSATADeviceInfo_t  *agSATADeviceInfo;
  tiPortalContext_t     *tiPortalContext;

  bit32                 devicetype = UNKNOWN_DEVICE;

  osData          = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot          = (tiRoot_t *)osData->tiRoot;

  TI_DBG5(("ossaDiscoverSataCB: start\n"));

  if (agPortContext == agNULL)
  {
    TI_DBG1(("ossaDiscoverSataCB: NULL agsaPortContext; wrong\n"));
    return;
  }

  onePortContext  = (tdsaPortContext_t *)agPortContext->osData;
  tiPortalContext = (tiPortalContext_t *)onePortContext->tiPortalContext;

  switch ( event )
  {
    case OSSA_DISCOVER_STARTED:
    {
      TI_DBG5(("ossaDiscoverSataCB: STARTED\n"));
      /* Do nothing */
      break;
    }

    case OSSA_DISCOVER_FOUND_DEVICE:
    {
      TI_DBG5(("ossaDiscoverSataCB: ***** FOUND DEVICE\n"));
      agDevHandle      = (agsaDevHandle_t *) pParm1;
      agSATADeviceInfo = (agsaSATADeviceInfo_t *) pParm2;

      /* parse the device type */
      devicetype = tdssSATADeviceTypeDecode(agSATADeviceInfo->signature);


      /* for now, TD handles only ATA Device or ATAPI Device */
      if (devicetype == SATA_ATA_DEVICE || devicetype == SATA_ATAPI_DEVICE)
      {
        TI_DBG5(("ossaDiscoverSataCB: ***** adding ....\n"));
        /* Add SATA device */
        tdssAddSATAToSharedcontext( onePortContext,
                                    agRoot,
                                    agDevHandle,
                                    agSATADeviceInfo,
                                    agTRUE,
                                    agSATADeviceInfo->stpPhyIdentifier 
                                    );
#ifdef INITIATOR_DRIVER
        ostiInitiatorEvent(
                           tiRoot,
                           tiPortalContext,
                           agNULL,
                           tiIntrEventTypeDeviceChange,
                           tiDeviceArrival,
                           agNULL
                           );
#endif
      } /* end of ATA_ATA_DEVICE or ATA_ATAPI_DEVICE */
      else
      {
        TI_DBG5(("ossaDiscoverSataCB: ***** not adding ..... devicetype 0x%x\n", devicetype));
      }
      break;
    }

    case OSSA_DISCOVER_REMOVED_DEVICE:
    {
      TI_DBG1(("ossaDiscoverSataCB: REMOVED_DEVICE\n"));
      agDevHandle      = (agsaDevHandle_t *) pParm1;
      agSATADeviceInfo = (agsaSATADeviceInfo_t *) pParm2;

      oneDeviceData = (tdsaDeviceData_t *) agDevHandle->osData;

      TI_DBG1(("ossaDiscoverSataCB: signature: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
        agSATADeviceInfo->signature[0], agSATADeviceInfo->signature[1],
        agSATADeviceInfo->signature[2], agSATADeviceInfo->signature[3],
        agSATADeviceInfo->signature[4], agSATADeviceInfo->signature[5],
        agSATADeviceInfo->signature[6], agSATADeviceInfo->signature[7] ));

      if (oneDeviceData == agNULL)
      {
        TI_DBG1(("ossaDiscoverSataCB: Wrong. DevHandle->osData is NULL but is being removed\n"));
      }
      tdssRemoveSATAFromSharedcontext( onePortContext,
                                       oneDeviceData,
                                       agRoot
                                       );
      agDevHandle->osData = agNULL;
#ifdef INITIATOR_DRIVER
      ostiInitiatorEvent(
                         tiRoot,
                         tiPortalContext,
                         agNULL,
                         tiIntrEventTypeDeviceChange,
                         tiDeviceRemoval,
                         agNULL
                         );
#endif
      break;
    }

    case OSSA_DISCOVER_COMPLETE:
    {
      TI_DBG1(("ossaDiscoverSataCB: COMPLETE\n"));
      onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED;
      TI_DBG6(("ossaDiscoverSataCB: COMPLETE pid %d\n", onePortContext->id));

      /* Let OS-Apps specific layer know discovery has been successfully complete */
      ostiInitiatorEvent( tiRoot,
                          tiPortalContext,
                          agNULL,
                          tiIntrEventTypeDiscovery,
                          tiDiscOK,
                          agNULL );
      break;
    }

    case OSSA_DISCOVER_ABORT:
    {
      TI_DBG1(("ossaDiscoverSataCB: OSSA_DISCOVER_ABORT\n"));
      /* Let OS-Apps specific layer know discovery has failed */
      ostiInitiatorEvent( tiRoot,
                          tiPortalContext,
                          agNULL,
                          tiIntrEventTypeDiscovery,
                          tiDiscFailed,
                          agNULL );

      break;
     }

    default:
    {
       TI_DBG1(("ossaDiscoverSataCB: error default event 0x%x\n", event));
      /* Let OS-Apps specific layer know discovery has failed */
      ostiInitiatorEvent( tiRoot,
                          tiPortalContext,
                          agNULL,
                          tiIntrEventTypeDiscovery,
                          tiDiscFailed,
                          agNULL );
      break;
    }

  } /* end of switch */

  return;
}

/*****************************************************************************
*! \brief tdssAddSataToSharedcontext
*
*  Purpose:  This function adds a discovered SATA device to a device list of
*            a port context
*
*  \param   tsddPortContext_Instance Pointer to the target port context
*  \param   agRoot                   Pointer to the root data structure of
*                                    TD and Lower layer
*  \param   agDevHandle              Pointer to a device handle
*  \param   agSATADeviceInfo         Pointer to SATA device info structure
*  \param   registered               indication flag for registration to LL
*
*  \Return: none
*
*****************************************************************************/
/* split into devicedata allocation/registration and sending identify device data */
osGLOBAL void
tdssAddSATAToSharedcontext( tdsaPortContext_t    *tdsaPortContext_Instance,
                            agsaRoot_t           *agRoot,
                            agsaDevHandle_t      *agDevHandle,
                            agsaSATADeviceInfo_t *agSATADeviceInfo,
                            bit32                 registered,
                            bit8                  phyID
                            )
{
  tdsaPortContext_t           *onePortContext = agNULL;
  tdList_t                    *PortContextList;
  tdsaDeviceData_t            *oneDeviceData = agNULL;
  tdList_t                    *DeviceListList = agNULL;
  tdsaRootOsData_t            *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                    *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  satDeviceData_t             *pSatDevData;
  bit32                       Indenom = tdsaAllShared->QueueConfig.numInboundQueues;
  bit32                       Outdenom = tdsaAllShared->QueueConfig.numOutboundQueues;
  bit8                        dev_s_rate = 0;
  bit8                        sasorsata = 1;
  bit8                        connectionRate;
  bit8                        flag = 0;
  bit8                        TLR = 0;
  bit32                       found = agFALSE;
  TI_DBG5(("tdssAddSataToSharedcontext: start\n"));

  /*
   * Find a right portcontext, then get devicedata from FreeLink in DeviceList.
   * Then, add the devicedata to the portcontext.
   */

  /* Find a right portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    if (onePortContext == tdsaPortContext_Instance)
    {
      TI_DBG5(("tdssAddSataToSharedcontext: found; oneportContext ID %d\n",
        onePortContext->id));
      found = agTRUE;
      break;
    }
    PortContextList = PortContextList->flink;
  }

  if (found == agFALSE)
  {
    TI_DBG1(("tdssAddSataToSharedcontext: No corressponding tdsaPortContext\n"));
    return;
  }

  /*
   1. add the devicedata
   2. Send Identify Device Data
   3. In CB of Identify Device Data (satAddSATAIDDevCB), finds out the devicedata is new or old
  */


  tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
  if (!TDLIST_NOT_EMPTY(&(tdsaAllShared->FreeDeviceList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG1(("tdssAddSataToSharedcontext: ERROR empty DeviceData FreeLink\n"));
    /* notifying link up */
    ostiPortEvent (
                   tiRoot,
                   tiPortLinkUp,
                   tiSuccess,
                   (void *)tdsaAllShared->Ports[phyID].tiPortalContext
                   );
#ifdef INITIATOR_DRIVER
    /* triggers discovery */
    ostiPortEvent(
                  tiRoot,
                  tiPortDiscoveryReady,
                  tiSuccess,
                  (void *) tdsaAllShared->Ports[phyID].tiPortalContext
                  );
#endif
    return;
  }

  TDLIST_DEQUEUE_FROM_HEAD(&DeviceListList, &(tdsaAllShared->FreeDeviceList));
  tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
  oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, FreeLink, DeviceListList);
  TDLIST_DEQUEUE_THIS(&(oneDeviceData->FreeLink));

  TI_DBG1(("tdssAddSataToSharedcontext: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
  oneDeviceData->InQID = oneDeviceData->id % Indenom;
  oneDeviceData->OutQID = oneDeviceData->id % Outdenom;

  pSatDevData = (satDeviceData_t *)&(oneDeviceData->satDevData);
  pSatDevData->satDriveState = SAT_DEV_STATE_NORMAL;
  pSatDevData->satPendingIO = 0;
  pSatDevData->satNCQMaxIO = 0;
  pSatDevData->satPendingNCQIO = 0;
  pSatDevData->satPendingNONNCQIO = 0;
  pSatDevData->IDDeviceValid = agFALSE;
  pSatDevData->satDeviceType = tdssSATADeviceTypeDecode(onePortContext->remoteSignature);

  osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));

  onePortContext->Count++;
  oneDeviceData->DeviceType = TD_SATA_DEVICE; // either TD_SAS_DEVICE or TD_SATA_DEVICE
  oneDeviceData->agRoot = agRoot;
//  oneDeviceData->agDevHandle = agDevHandle;

//  agDevHandle->osData = oneDeviceData; /* TD layer */
  oneDeviceData->tdPortContext = onePortContext;
  oneDeviceData->valid = agTRUE;

  oneDeviceData->directlyAttached = agTRUE;
  oneDeviceData->initiator_ssp_stp_smp = 0;
  oneDeviceData->target_ssp_stp_smp = 0x1; /* setting SATA device bit */
  oneDeviceData->phyID = phyID;

  /* parse sasIDframe to fill in agDeviceInfo */
  flag = (bit8)((phyID << 4) | TLR);
  DEVINFO_PUT_SMPTO(&oneDeviceData->agDeviceInfo, DEFAULT_SMP_TIMEOUT);
  DEVINFO_PUT_ITNEXUSTO(&oneDeviceData->agDeviceInfo, (bit16)tdsaAllShared->itNexusTimeout);
  DEVINFO_PUT_FBS(&oneDeviceData->agDeviceInfo, 0);
  //temp
  //DEVINFO_PUT_FLAG(&oneDeviceData->agDeviceInfo, 0);
  DEVINFO_PUT_FLAG(&oneDeviceData->agDeviceInfo, flag);

  sasorsata = SATA_DEVICE_TYPE; /* SATA disk */
  connectionRate = onePortContext->LinkRate; 
  dev_s_rate = (bit8)(dev_s_rate | (sasorsata << 4));
  dev_s_rate = (bit8)(dev_s_rate | connectionRate);
  DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->agDeviceInfo, dev_s_rate);

  DEVINFO_PUT_SAS_ADDRESSLO(
                            &oneDeviceData->agDeviceInfo,
                            0
                            );
  DEVINFO_PUT_SAS_ADDRESSHI(
                            &oneDeviceData->agDeviceInfo,
                            0
                            );

  if (pSatDevData->satDeviceType == SATA_ATAPI_DEVICE )
  {
     oneDeviceData->agDeviceInfo.flag |= ATAPI_DEVICE_FLAG; /* ATAPI device flag*/
  }

  oneDeviceData->agContext.osData = oneDeviceData;
  oneDeviceData->agContext.sdkData = agNULL;

  if (oneDeviceData->registered == agFALSE)
  {
    saRegisterNewDevice(  /* tdssAddSATAToSharedcontext  */
                        agRoot,
                        &oneDeviceData->agContext,
                        0,/*tdsaRotateQnumber(tiRoot, oneDeviceData),*/
                        &oneDeviceData->agDeviceInfo,
                        onePortContext->agPortContext,
                        0
                        );
  }
  return;
}
/*****************************************************************************
*! \brief tdssRetrySATAID
*
*  Purpose:  This function retries identify device data to directly attached SATA
*            device after device registration
*
*  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
*  \param   oneDeviceData     Pointer to a device data
*  \Return: none
*
*****************************************************************************/
osGLOBAL void
tdssRetrySATAID( tiRoot_t             *tiRoot,
                 tdsaDeviceData_t     *oneDeviceData
               )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  void                        *osMemHandle;
  tdIORequestBody_t           *tdIORequestBody;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  agsaIORequest_t             *agIORequest = agNULL; /* identify device data itself */
  satIOContext_t              *satIOContext = agNULL;
  bit32                       status;

  TI_DBG5(("tdssRetrySATAID: start\n"));
  /* allocate identify device data and sends it */
  /* allocation tdIORequestBody and pass it to satTM() */
  memAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &osMemHandle,
                                   (void **)&tdIORequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(tdIORequestBody_t),
                                   agTRUE
                                   );

  if (memAllocStatus != tiSuccess)
  {
    TI_DBG1(("tdssRetrySATAID: ostiAllocMemory failed... loc 2\n"));
    /* notifying link up */
    ostiPortEvent (
                   tiRoot,
                   tiPortLinkUp,
                   tiSuccess,
                   (void *)tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                   );
#ifdef INITIATOR_DRIVER
    /* triggers discovery */
    ostiPortEvent(
                  tiRoot,
                  tiPortDiscoveryReady,
                  tiSuccess,
                  (void *) tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                  );
#endif

    return;
  }

  if (tdIORequestBody == agNULL)
  {
    TI_DBG1(("tdssRetrySATAID: ostiAllocMemory returned NULL tdIORequestBody loc 2\n"));
    /* notifying link up */
    ostiPortEvent (
                   tiRoot,
                   tiPortLinkUp,
                   tiSuccess,
                   (void *)tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                   );
#ifdef INITIATOR_DRIVER
    /* triggers discovery */
    ostiPortEvent(
                  tiRoot,
                  tiPortDiscoveryReady,
                  tiSuccess,
                  (void *) tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                  );
#endif

    return;
  }

  /* setup identify device data IO structure */
  tdIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  tdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag = agNULL;
  tdIORequestBody->IOType.InitiatorTMIO.TaskTag = agNULL;

  /* initialize tiDevhandle */
  tdIORequestBody->tiDevHandle = &(oneDeviceData->tiDeviceHandle);
  tdIORequestBody->tiDevHandle->tdData = oneDeviceData;

  /* initialize tiIORequest */
  tdIORequestBody->tiIORequest = agNULL;

  /* initialize agIORequest */
  agIORequest = &(tdIORequestBody->agIORequest);
  agIORequest->osData = (void *) tdIORequestBody;
  agIORequest->sdkData = agNULL; /* SA takes care of this */

  /* set up satIOContext */
  satIOContext = &(tdIORequestBody->transport.SATA.satIOContext);
  satIOContext->pSatDevData   = &(oneDeviceData->satDevData);
  satIOContext->pFis          =
    &(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);


  satIOContext->tiRequestBody = tdIORequestBody;
  satIOContext->ptiDeviceHandle = &(oneDeviceData->tiDeviceHandle);
  satIOContext->tiScsiXchg = agNULL;
  satIOContext->satIntIoContext  = agNULL;
  satIOContext->satOrgIOContext  = agNULL;

  /* followings are used only for internal IO */
  satIOContext->currentLBA = 0;
  satIOContext->OrgTL = 0;


  satIOContext->satToBeAbortedIOContext = agNULL;

  satIOContext->NotifyOS = agFALSE;

  satIOContext->pid = tdsaAllShared->Ports[oneDeviceData->phyID].portContext->id;

  status = satAddSATAStartIDDev(tiRoot,
                                agNULL,
                                &(oneDeviceData->tiDeviceHandle),
                                agNULL,
                                satIOContext
                                );

  /* assumption; always new device data */


  if (status == tiSuccess)
  {
    TI_DBG6(("tdssRetrySATAID: successfully sent identify device data\n"));
    TI_DBG6(("tdssRetrySATAID: one case did %d \n", oneDeviceData->id));
  }
  else
  {
    TI_DBG1(("tdssRetrySATAID: failed in sending identify device data\n"));
    /* put onedevicedata back to free list */
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    /* notifying link up */
    ostiPortEvent (
                   tiRoot,
                   tiPortLinkUp,
                   tiSuccess,
                   (void *)tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                   );
#ifdef INITIATOR_DRIVER
    /* triggers discovery */
    ostiPortEvent(
                  tiRoot,
                  tiPortDiscoveryReady,
                  tiSuccess,
                  (void *) tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                  );
#endif

  }

  return;
}

/*****************************************************************************
*! \brief tdssSubAddSATAToSharedcontext
*
*  Purpose:  This function sends identify device data to directly attached SATA
*            device after device registration
*
*  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
*  \param   oneDeviceData     Pointer to a device data
*  \Return: none
*
*****************************************************************************/
osGLOBAL void
tdssSubAddSATAToSharedcontext( tiRoot_t             *tiRoot,
                               tdsaDeviceData_t     *oneDeviceData
                              )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  void                        *osMemHandle;
  tdIORequestBody_t           *tdIORequestBody;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  agsaIORequest_t             *agIORequest = agNULL; /* identify device data itself */
  satIOContext_t              *satIOContext = agNULL;
  bit32                       status;

  TI_DBG1(("tdssSubAddSATAToSharedcontext: start\n"));
  /* allocate identify device data and sends it */
  /* allocation tdIORequestBody and pass it to satTM() */
  memAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &osMemHandle,
                                   (void **)&tdIORequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(tdIORequestBody_t),
                                   agTRUE
                                   );

  if (memAllocStatus != tiSuccess)
  {
    TI_DBG1(("tdssSubAddSATAToSharedcontext: ostiAllocMemory failed... loc 2\n"));
    /* notifying link up */
    ostiPortEvent (
                   tiRoot,
                   tiPortLinkUp,
                   tiSuccess,
                   (void *)tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                   );
#ifdef INITIATOR_DRIVER
    /* triggers discovery */
    ostiPortEvent(
                  tiRoot,
                  tiPortDiscoveryReady,
                  tiSuccess,
                  (void *) tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                  );
#endif

    return;
  }

  if (tdIORequestBody == agNULL)
  {
    TI_DBG1(("tdssSubAddSATAToSharedcontext: ostiAllocMemory returned NULL tdIORequestBody loc 2\n"));
    /* notifying link up */
    ostiPortEvent (
                   tiRoot,
                   tiPortLinkUp,
                   tiSuccess,
                   (void *)tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                   );
#ifdef INITIATOR_DRIVER
    /* triggers discovery */
    ostiPortEvent(
                  tiRoot,
                  tiPortDiscoveryReady,
                  tiSuccess,
                  (void *) tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                  );
#endif

    return;
  }

  /* setup identify device data IO structure */
  tdIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  tdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag = agNULL;
  tdIORequestBody->IOType.InitiatorTMIO.TaskTag = agNULL;

  /* initialize tiDevhandle */
  tdIORequestBody->tiDevHandle = &(oneDeviceData->tiDeviceHandle);
  tdIORequestBody->tiDevHandle->tdData = oneDeviceData;

  /* initialize tiIORequest */
  tdIORequestBody->tiIORequest = agNULL;

  /* initialize agIORequest */
  agIORequest = &(tdIORequestBody->agIORequest);
  agIORequest->osData = (void *) tdIORequestBody;
  agIORequest->sdkData = agNULL; /* SA takes care of this */

  /* set up satIOContext */
  satIOContext = &(tdIORequestBody->transport.SATA.satIOContext);
  satIOContext->pSatDevData   = &(oneDeviceData->satDevData);
  satIOContext->pFis          =
    &(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);


  satIOContext->tiRequestBody = tdIORequestBody;
  satIOContext->ptiDeviceHandle = &(oneDeviceData->tiDeviceHandle);
  satIOContext->tiScsiXchg = agNULL;
  satIOContext->satIntIoContext  = agNULL;
  satIOContext->satOrgIOContext  = agNULL;

  /* followings are used only for internal IO */
  satIOContext->currentLBA = 0;
  satIOContext->OrgTL = 0;


  satIOContext->satToBeAbortedIOContext = agNULL;

  satIOContext->NotifyOS = agFALSE;

  satIOContext->pid = tdsaAllShared->Ports[oneDeviceData->phyID].portContext->id;

  status = satAddSATAStartIDDev(tiRoot,
                                agNULL,
                                &(oneDeviceData->tiDeviceHandle),
                                agNULL,
                                satIOContext
                                );

  /* assumption; always new device data */


  if (status == tiSuccess)
  {
    TI_DBG6(("tdssSubAddSATAToSharedcontext: successfully sent identify device data\n"));

    /* Add the devicedata to the mainlink */
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->MainLink), &(tdsaAllShared->MainDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG6(("tdssSubAddSATAToSharedcontext: one case did %d \n", oneDeviceData->id));
  }
  else
  {
    TI_DBG1(("tdssSubAddSATAToSharedcontext: failed in sending identify device data\n"));
    /* put onedevicedata back to free list */
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    /* notifying link up */
    ostiPortEvent (
                   tiRoot,
                   tiPortLinkUp,
                   tiSuccess,
                   (void *)tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                   );
#ifdef INITIATOR_DRIVER
    /* triggers discovery */
    ostiPortEvent(
                  tiRoot,
                  tiPortDiscoveryReady,
                  tiSuccess,
                  (void *) tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext
                  );
#endif

  }

  return;
}


/*****************************************************************************
*! \brief tdssRemoveSATAFromSharedcontext
*
*  Purpose:  This function removes a discovered device from a device list of
*            a port context
*
*  \param   tsddPortContext_Ins      Pointer to the target port context
*  \param   tdsaDeviceData_Ins       Pointer to the target device
*  \param   agRoot                   Pointer to the root data structure of
*                                    TD and Lower layer

*
*  \Return: none
*
*****************************************************************************/
osGLOBAL void
tdssRemoveSATAFromSharedcontext(
                               tdsaPortContext_t *tdsaPortContext_Ins,
                               tdsaDeviceData_t  *tdsaDeviceData_ins,
                               agsaRoot_t        *agRoot
                               )
{
  TI_DBG1(("tdssRemoveSATAFromSharedcontex: start\n"));
  return;
}


/*****************************************************************************
*! \brief satSetDevInfo
*
*  Purpose:  Based on ATA identify device data, this functions sets fields of
*            device data maintained in TD layer
*
*  \param   satDevData          Pointer to a device data
*  \param   SATAIdData          Pointer to ATA identify device data
*
*
*  \Return: none
*
*****************************************************************************/
void satSetDevInfo(
                   satDeviceData_t           *satDevData,
                   agsaSATAIdentifyData_t    *SATAIdData
                   )
{
  TI_DBG3(("satSetDevInfo: start\n"));

  satDevData->satDriveState = SAT_DEV_STATE_NORMAL;
  satDevData->satFormatState = agFALSE;
  satDevData->satDeviceFaultState = agFALSE;
  satDevData->satTmTaskTag  = agNULL;
  satDevData->satAbortAfterReset = agFALSE;
  satDevData->satAbortCalled = agFALSE;
  satDevData->satSectorDone  = 0;

  /* Qeueu depth, Word 75 */
  satDevData->satNCQMaxIO = SATAIdData->queueDepth + 1;
  TI_DBG3(("satSetDevInfo: max queue depth %d\n",satDevData->satNCQMaxIO));

  /* Support NCQ, if Word 76 bit 8 is set */
  if (SATAIdData->sataCapabilities & 0x100)
  {
    TI_DBG3(("satSetDevInfo: device supports NCQ\n"));
    satDevData->satNCQ   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no NCQ\n"));
    satDevData->satNCQ = agFALSE;
  }

  /* Support 48 bit addressing, if Word 83 bit 10 and Word 86 bit 10 are set */
  if ((SATAIdData->commandSetSupported1 & 0x400) &&
      (SATAIdData->commandSetFeatureEnabled1 & 0x400) )
  {
    TI_DBG3(("satSetDevInfo: support 48 bit addressing\n"));
    satDevData->sat48BitSupport = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: NO 48 bit addressing\n"));
    satDevData->sat48BitSupport = agFALSE;
  }

  /* Support SMART Self Test, word84 bit 1 */
  if (SATAIdData->commandSetFeatureSupportedExt & 0x02)
  {
    TI_DBG3(("satSetDevInfo: SMART self-test supported \n"));
    satDevData->satSMARTSelfTest   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no SMART self-test suppored\n"));
    satDevData->satSMARTSelfTest = agFALSE;
  }



  /* Support SMART feature set, word82 bit 0 */
  if (SATAIdData->commandSetSupported & 0x01)
  {
    TI_DBG3(("satSetDevInfo: SMART feature set supported \n"));
    satDevData->satSMARTFeatureSet   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no SMART feature set suppored\n"));
    satDevData->satSMARTFeatureSet = agFALSE;
  }



  /* Support SMART enabled, word85 bit 0 */
  if (SATAIdData->commandSetFeatureEnabled & 0x01)
  {
    TI_DBG3(("satSetDevInfo: SMART enabled \n"));
    satDevData->satSMARTEnabled   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no SMART enabled\n"));
    satDevData->satSMARTEnabled = agFALSE;
  }

  satDevData->satVerifyState = 0;

  /* Removable Media feature set support, word82 bit 2 */
  if (SATAIdData->commandSetSupported & 0x4)
  {
    TI_DBG3(("satSetDevInfo: Removable Media supported \n"));
    satDevData->satRemovableMedia   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no Removable Media suppored\n"));
    satDevData->satRemovableMedia = agFALSE;
  }

  /* Removable Media feature set enabled, word 85, bit 2 */
  if (SATAIdData->commandSetFeatureEnabled & 0x4)
  {
    TI_DBG3(("satSetDevInfo: Removable Media enabled\n"));
    satDevData->satRemovableMediaEnabled   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no Removable Media enabled\n"));
    satDevData->satRemovableMediaEnabled = agFALSE;
  }

  /* DMA Support, word49 bit8 */
  if (SATAIdData->dma_lba_iod_ios_stimer & 0x100)
  {
    TI_DBG3(("satSetDevInfo: DMA supported \n"));
    satDevData->satDMASupport   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no DMA suppored\n"));
    satDevData->satDMASupport = agFALSE;
  }

  /* DMA Enabled, word88 bit0-6, bit8-14*/
  /* 0x7F7F = 0111 1111 0111 1111*/
  if (SATAIdData->ultraDMAModes & 0x7F7F)
  {
    TI_DBG3(("satSetDevInfo: DMA enabled \n"));
    satDevData->satDMAEnabled   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no DMA enabled\n"));
    satDevData->satDMAEnabled = agFALSE;
  }

  /*
    setting MaxUserAddrSectors: max user addressable setctors
    word60 - 61, should be 0x 0F FF FF FF
  */
  satDevData->satMaxUserAddrSectors
    = (SATAIdData->numOfUserAddressableSectorsHi << (8*2) )
    + SATAIdData->numOfUserAddressableSectorsLo;
  TI_DBG3(("satSetDevInfo: MaxUserAddrSectors 0x%x decimal %d\n", satDevData->satMaxUserAddrSectors, satDevData->satMaxUserAddrSectors));
  /* Support DMADIR, if Word 62 bit 8 is set */
  if (SATAIdData->word62_74[0] & 0x8000)
  {
     TI_DBG3(("satSetDevInfo: DMADIR enabled\n"));
     satDevData->satDMADIRSupport   = agTRUE;
  }
  else
  {
     TI_DBG3(("satSetDevInfo: DMADIR disabled\n"));
     satDevData->satDMADIRSupport   = agFALSE;
  }


  /* write cache enabled for caching mode page SAT Table 67 p69, word85 bit5 */
  if (SATAIdData->commandSetFeatureEnabled & 0x20)
  {
    TI_DBG3(("satSetDevInfo: write cache enabled\n"));
    satDevData->satWriteCacheEnabled   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no write cache enabled\n"));
    satDevData->satWriteCacheEnabled = agFALSE;
  }

  /* look ahead enabled for caching mode page SAT Table 67 p69, word85 bit6 */
  if (SATAIdData->commandSetFeatureEnabled & 0x40)
  {
    TI_DBG3(("satSetDevInfo: look ahead enabled\n"));
    satDevData->satLookAheadEnabled   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no look ahead enabled\n"));
    satDevData->satLookAheadEnabled = agFALSE;
  }

  /* Support WWN, if Word 87 bit 8 is set */
  if (SATAIdData->commandSetFeatureDefault & 0x100)
  {
    TI_DBG3(("satSetDevInfo: device supports WWN\n"));
    satDevData->satWWNSupport   = agTRUE;
  }
  else
  {
    TI_DBG3(("satSetDevInfo: no WWN\n"));
    satDevData->satWWNSupport = agFALSE;
  }


  return;
}

/*****************************************************************************
*! \brief  satInquiryCB
*
*   This routine is a callback function for satInquiry()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satInquiryCB(
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
  tdsaRootOsData_t         *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                 *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t               *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t            *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t        *tdIORequestBody;
  tdIORequestBody_t        *tdOrgIORequestBody;
  satIOContext_t           *satIOContext;
  satIOContext_t           *satOrgIOContext;
  satInternalIo_t          *satIntIo;
  satDeviceData_t          *satDevData;
#ifdef  TD_DEBUG_ENABLE
  tdsaDeviceData_t         *tdsaDeviceData;
  bit32                     ataStatus = 0;
  bit32                     ataError;
  agsaFisPioSetupHeader_t  *satPIOSetupHeader = agNULL;
#endif
  scsiRspSense_t           *pSense;
  tiIniScsiCmnd_t          *scsiCmnd;
  tiIORequest_t            *tiOrgIORequest;
  tiScsiInitiatorRequest_t *tiScsiRequest; /* TD's tiScsiXchg */
  tiScsiInitiatorRequest_t *tiOrgScsiRequest; /* OS's tiScsiXchg */
  agsaSATAIdentifyData_t   *pSATAIdData;
  bit8                     *pInquiry;
  bit8                      page = 0xFF;
  bit16                    *tmpptr,tmpptr_tmp;
  bit32                     x;
  bit32                     lenReceived;
  bit32                     lenNeeded = 0;

  TI_DBG6(("satInquiryCB: start\n"));
  TI_DBG6(("satInquiryCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
#ifdef  TD_DEBUG_ENABLE
  tdsaDeviceData         = (tdsaDeviceData_t *)satDevData->satSaDeviceData;
#endif
  tiScsiRequest          = satIOContext->tiScsiXchg;
  if (satIntIo == agNULL)
  {
    TI_DBG6(("satInquiryCB: External, OS generated\n"));
    pSense               = satIOContext->pSense;
    scsiCmnd             = satIOContext->pScsiCmnd;
    satOrgIOContext      = satIOContext;
    tiOrgIORequest       = tdIORequestBody->tiIORequest;
  }
  else
  {
    TI_DBG6(("satInquiryCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG1(("satInquiryCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG6(("satInquiryCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody    = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest        = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
    pSense                = satOrgIOContext->pSense;
    scsiCmnd              = satOrgIOContext->pScsiCmnd;
  }

  tiOrgScsiRequest        = satOrgIOContext->tiScsiXchg;
  pInquiry                = (bit8 *) tiOrgScsiRequest->sglVirtualAddr;

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  TI_DBG3(("satInquiryCB: did %d\n", tdsaDeviceData->id));

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satInquiryCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY)
    {
      TI_DBG1(("satInquiryCB: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY\n"));
      /* should NOT be retried */
      ostiInitiatorIOCompleted (
                                tiRoot,
                                tiOrgIORequest,
                                tiIOFailed,
                                tiDetailNoLogin,
                                agNULL,
                                satOrgIOContext->interruptContext
                                );
    }
    else
    {
      TI_DBG1(("satInquiryCB: NOT OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY\n"));
      ostiInitiatorIOCompleted (
                                tiRoot,
                                tiOrgIORequest,
                                tiIOFailed,
                                tiDetailNoLogin,
                                agNULL,
                                satOrgIOContext->interruptContext
                               );
    }
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
    TI_DBG1(("satInquiryCB: OSSA_IO_OPEN_CNX_ERROR\n"));

    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailNoLogin,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                         satDevData,
                         satIntIo);
    return;
  }

 if ( agIOStatus != OSSA_IO_SUCCESS ||
      (agIOStatus == OSSA_IO_SUCCESS && agFirstDword != agNULL && agIOInfoLen != 0)
    )
 {
#ifdef  TD_DEBUG_ENABLE
   // only agsaFisPioSetup_t is expected
   satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
   ataStatus         = satPIOSetupHeader->status;   // ATA Status register
   ataError          = satPIOSetupHeader->error;    // ATA Eror register
#endif
   TI_DBG1(("satInquiryCB: ataStatus 0x%x ataError 0x%x\n", ataStatus, ataError));
   /* Process abort case */
   if (agIOStatus == OSSA_IO_ABORTED)
   {
     satProcessAbort(tiRoot,
                     tiOrgIORequest,
                     satOrgIOContext
                     );

     satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

     satFreeIntIoResource( tiRoot,
                           satDevData,
                           satIntIo);
     return;
   }

    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );

   satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

   satFreeIntIoResource( tiRoot,
                         satDevData,
                         satIntIo);
   return;
  }

 /* success */


 /* Convert to host endian */
 tmpptr = (bit16*)(tiScsiRequest->sglVirtualAddr);
 for (x=0; x < sizeof(agsaSATAIdentifyData_t)/sizeof(bit16); x++)
 {
   OSSA_READ_LE_16(AGROOT, &tmpptr_tmp, tmpptr, 0);
   *tmpptr = tmpptr_tmp;
   tmpptr++;
   /*Print tmpptr_tmp here for debugging purpose*/
 }

 pSATAIdData = (agsaSATAIdentifyData_t *)(tiScsiRequest->sglVirtualAddr);

 TI_DBG5(("satInquiryCB: OS satOrgIOContext %p \n", satOrgIOContext));
 TI_DBG5(("satInquiryCB: TD satIOContext %p \n", satIOContext));
 TI_DBG5(("satInquiryCB: OS tiScsiXchg %p \n", satOrgIOContext->tiScsiXchg));
 TI_DBG5(("satInquiryCB: TD tiScsiXchg %p \n", satIOContext->tiScsiXchg));

 /* copy ID Dev data to satDevData */
 satDevData->satIdentifyData = *pSATAIdData;
 satDevData->IDDeviceValid = agTRUE;
#ifdef TD_INTERNAL_DEBUG
 tdhexdump("satInquiryCB ID Dev data",(bit8 *)pSATAIdData, sizeof(agsaSATAIdentifyData_t));
 tdhexdump("satInquiryCB Device ID Dev data",(bit8 *)&satDevData->satIdentifyData, sizeof(agsaSATAIdentifyData_t));
#endif
// tdhexdump("satInquiryCB Device ID Dev data",(bit8 *)&satDevData->satIdentifyData, sizeof(agsaSATAIdentifyData_t));

 /* set satDevData fields from IndentifyData */
 satSetDevInfo(satDevData,pSATAIdData);

  lenReceived = ((scsiCmnd->cdb[3]) << 8) + scsiCmnd->cdb[4];

  /* SPC-4, spec 6.4 p 141 */
  /* EVPD bit == 0 */
  if (!(scsiCmnd->cdb[1] & SCSI_EVPD_MASK))
  {
    /* Returns the standard INQUIRY data */
    lenNeeded = STANDARD_INQUIRY_SIZE;


    satInquiryStandard(pInquiry, pSATAIdData, scsiCmnd);
    //tdhexdump("satInquiryCB ***standard***", (bit8 *)pInquiry, 36);

  }
  else
  {
    /* EVPD bit != 0 && PAGE CODE != 0 */
    /* returns the pages of vital product data information */

    /* we must support page 00h, 83h and 89h */
    page = scsiCmnd->cdb[2];
    if ((page != INQUIRY_SUPPORTED_VPD_PAGE) &&
        (page != INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE) &&
        (page != INQUIRY_ATA_INFORMATION_VPD_PAGE))
    {
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                       satDevData,
                       satIntIo);
      TI_DBG1(("satInquiryCB: invalid PAGE CODE 0x%x\n", page));
      return;
    }

    /* checking length */
    switch (page)
    {
    case INQUIRY_SUPPORTED_VPD_PAGE:
      lenNeeded = SATA_PAGE0_INQUIRY_SIZE; /* 36 */
      break;
    case INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE:
      if (satDevData->satWWNSupport)
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
    default:
      TI_DBG1(("satInquiryCB: wrong!!! invalid PAGE CODE 0x%x\n", page));
      break;
    }


    /*
     * Fill in the Inquiry data depending on what Inquiry data we are returning.
     */
    switch (page)
    {
    case INQUIRY_SUPPORTED_VPD_PAGE:
      satInquiryPage0(pInquiry, pSATAIdData);
      break;
    case INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE:
      satInquiryPage83(pInquiry, pSATAIdData, satDevData);
      break;
    case INQUIRY_ATA_INFORMATION_VPD_PAGE:
      satInquiryPage89(pInquiry, pSATAIdData, satDevData);
      break;
    default:
      TI_DBG1(("satInquiryCB: wrong!!! invalidinvalid PAGE CODE 0x%x\n", page));
      break;
    }
  } /* else */

  TI_DBG6(("satInquiryCB: calling ostiInitiatorIOCompleted\n"));

  if (lenReceived > lenNeeded)
  {
    TI_DBG6(("satInquiryCB reporting underrun lenNeeded=0x%x lenReceived=0x%x tiIORequest=%p\n",
        lenNeeded, lenReceived, tiOrgIORequest));

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOUnderRun,
                              lenReceived - lenNeeded,
                              agNULL,
                              satOrgIOContext->interruptContext );
  }
  else
  {
    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext);
  }

  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);
  TI_DBG5(("satInquiryCB: device %p pending IO %d\n", satDevData, satDevData->satPendingIO));
  TI_DBG6(("satInquiryCB: end\n"));
  return;
}


/*****************************************************************************/
/*! \brief satInquiryIntCB.
 *
 *  This function is part of INQUIRY SAT implementation. This is called when
 *  ATA identify device data is available.
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     I/O request successfully initiated.
 *    - \e tiBusy:        No resources available, try again later.
 *    - \e tiIONoDevice:  Invalid device handle.
 *    - \e tiError:       Other errors.
 */
/*****************************************************************************/
void satInquiryIntCB(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext
                   )
{
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  satDeviceData_t           *satDevData;
  agsaSATAIdentifyData_t    *pSATAIdData;

  bit8                      *pInquiry;
  bit8                      page = 0xFF;
  bit32                     lenReceived;
  bit32                     lenNeeded = 0;

  TI_DBG6(("satInquiryIntCB: start\n"));

  pSense      = satIOContext->pSense;
  scsiCmnd    = &tiScsiRequest->scsiCmnd;
  pInquiry    = (bit8 *) tiScsiRequest->sglVirtualAddr;
  satDevData = satIOContext->pSatDevData;
  pSATAIdData = &satDevData->satIdentifyData;


  lenReceived = ((scsiCmnd->cdb[3]) << 8) + scsiCmnd->cdb[4];

  /* SPC-4, spec 6.4 p 141 */
  /* EVPD bit == 0 */
  if (!(scsiCmnd->cdb[1] & SCSI_EVPD_MASK))
  {
    /* Returns the standard INQUIRY data */
    lenNeeded = STANDARD_INQUIRY_SIZE;

     satInquiryStandard(pInquiry, pSATAIdData, scsiCmnd);
    //tdhexdump("satInquiryIntCB ***standard***", (bit8 *)pInquiry, 36);

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
        (page != INQUIRY_UNIT_SERIAL_NUMBER_VPD_PAGE))
    {
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_IN_CDB,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

      TI_DBG1(("satInquiryIntCB: invalid PAGE CODE 0x%x\n", page));
      return;
    }

    /* checking length */
    switch (page)
    {
    case INQUIRY_SUPPORTED_VPD_PAGE:
      lenNeeded = SATA_PAGE0_INQUIRY_SIZE; /* 36 */
      break;
    case INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE:
      if (satDevData->satWWNSupport)
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

    default:
      TI_DBG1(("satInquiryIntCB: wrong!!! invalidinvalid PAGE CODE 0x%x\n", page));
      break;
    }


    /*
     * Fill in the Inquiry data depending on what Inquiry data we are returning.
     */
    switch (page)
    {
    case INQUIRY_SUPPORTED_VPD_PAGE:
      satInquiryPage0(pInquiry, pSATAIdData);
      break;
    case INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE:
      satInquiryPage83(pInquiry, pSATAIdData, satDevData);
      break;
    case INQUIRY_ATA_INFORMATION_VPD_PAGE:
      satInquiryPage89(pInquiry, pSATAIdData, satDevData);
      break;
    case INQUIRY_UNIT_SERIAL_NUMBER_VPD_PAGE:
      satInquiryPage80(pInquiry, pSATAIdData);
      break;
    default:
      TI_DBG1(("satInquiryIntCB: wrong!!! invalidinvalid PAGE CODE 0x%x\n", page));
      break;
    }
  } /* else */

  TI_DBG6(("satInquiryIntCB: calling ostiInitiatorIOCompleted\n"));

  if (lenReceived > lenNeeded)
  {
    TI_DBG6(("satInquiryIntCB reporting underrun lenNeeded=0x%x lenReceived=0x%x tiIORequest=%p\n",
        lenNeeded, lenReceived, tiIORequest));

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOUnderRun,
                              lenReceived - lenNeeded,
                              agNULL,
                              satIOContext->interruptContext );
  }
  else
  {
    ostiInitiatorIOCompleted( tiRoot,
                            tiIORequest,
                            tiIOSuccess,
                            SCSI_STAT_GOOD,
                            agNULL,
                            satIOContext->interruptContext);
  }

  TI_DBG5(("satInquiryIntCB: device %p pending IO %d\n", satDevData, satDevData->satPendingIO));
  TI_DBG6(("satInquiryIntCB: end\n"));
  return;
}


/*****************************************************************************
*! \brief  satVerify10CB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Verify(10) completion.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satVerify10CB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   void              *agParam,
                   void              *ioContext
                   )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  TI_DBG5(("satVerify10CB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satVerify10CB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satIOContext->pSense;
  }
  else
  {
    TI_DBG4(("satVerify10CB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satVerify10CB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satVerify10CB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
    pSense        = satOrgIOContext->pSense;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     TI_DBG1(("satVerify10CB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
     satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

     ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      TI_DBG1(("satVerify10CB: FAILED, NOT IO_SUCCESS\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      TI_DBG1(("satVerify10CB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      TI_DBG1(("satVerify10CB: FAILED, FAILED, error status\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    /* for debugging */
    switch (hostToDevFis->h.command)
    {
    case SAT_READ_VERIFY_SECTORS_EXT:
      TI_DBG1(("satVerify10CB: SAT_READ_VERIFY_SECTORS_EXT\n"));
      break;
    default:
      TI_DBG1(("satVerify10CB: error default case command 0x%x\n", hostToDevFis->h.command));
      break;
    }

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  } /* end error checking */
  }

  /* process success from this point on */
  switch (hostToDevFis->h.command)
  {
  case SAT_READ_VERIFY_SECTORS_EXT:
    TI_DBG5(("satVerify10CB: SAT_WRITE_DMA_EXT success \n"));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext);
    break;
  default:
    TI_DBG1(("satVerify10CB: success but error default case command 0x%x\n", hostToDevFis->h.command));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    break;
  }

  return;
}

/* similar to satVerify10CB */
void satNonChainedVerifyCB(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           agsaFrameHandle_t agFrameHandle,
                           void              *ioContext
                           )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  TI_DBG5(("satNonChainedVerifyCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satNonChainedVerifyCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satIOContext->pSense;
  }
  else
  {
    TI_DBG4(("satNonChainedVerifyCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satNonChainedVerifyCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satNonChainedVerifyCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
    pSense        = satOrgIOContext->pSense;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     TI_DBG1(("satNonChainedVerifyCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
     satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

     ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
        TI_DBG1(("satNonChainedVerifyCB: FAILED, NOT IO_SUCCESS\n"));
      }
      else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
      {
        TI_DBG1(("satNonChainedVerifyCB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
      }
      else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
                (ataStatus & DF_ATA_STATUS_MASK)
                )
      {
        TI_DBG1(("satNonChainedVerifyCB: FAILED, FAILED, error status\n"));
      }

      /* Process abort case */
      if (agIOStatus == OSSA_IO_ABORTED)
      {
        satProcessAbort(tiRoot,
                        tiOrgIORequest,
                        satOrgIOContext
                        );

        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
        return;
      }

      /* for debugging */
      switch (hostToDevFis->h.command)
      {
      case SAT_READ_VERIFY_SECTORS:
        TI_DBG1(("satNonChainedVerifyCB: SAT_READ_VERIFY_SECTORS\n"));
        break;
      case SAT_READ_VERIFY_SECTORS_EXT:
        TI_DBG1(("satNonChainedVerifyCB: SAT_READ_VERIFY_SECTORS_EXT\n"));
        break;
      default:
        TI_DBG1(("satNonChainedVerifyCB: error default case command 0x%x\n", hostToDevFis->h.command));
        break;
      }

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    } /* end error checking */
  }

  /* process success from this point on */
  switch (hostToDevFis->h.command)
  {
  case SAT_READ_VERIFY_SECTORS: /* fall through */
  case SAT_READ_VERIFY_SECTORS_EXT:
    TI_DBG5(("satNonChainedVerifyCB: SAT_WRITE_DMA_EXT success \n"));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext);
    break;
  default:
    TI_DBG1(("satNonChainedVerifyCB: success but error default case command 0x%x\n", hostToDevFis->h.command));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    break;
  }

  return;
}

void satChainedVerifyCB(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           agsaFrameHandle_t agFrameHandle,
                           void              *ioContext
                           )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;
  bit32                     status = tiError;
  bit32                     dataLength;

  TI_DBG5(("satChainedVerifyCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satChainedVerifyCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satChainedVerifyCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satChainedVerifyCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satChainedVerifyCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     TI_DBG1(("satChainedVerifyCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
     satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

     ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
        TI_DBG1(("satChainedVerifyCB: FAILED, NOT IO_SUCCESS\n"));
      }
      else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
      {
        TI_DBG1(("satChainedVerifyCB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
      }
      else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
                (ataStatus & DF_ATA_STATUS_MASK)
                )
      {
        TI_DBG1(("satChainedVerifyCB: FAILED, FAILED, error status\n"));
      }

      /* Process abort case */
      if (agIOStatus == OSSA_IO_ABORTED)
      {
        satProcessAbort(tiRoot,
                        tiOrgIORequest,
                        satOrgIOContext
                        );

        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
        return;
      }

      /* for debugging */
      switch (hostToDevFis->h.command)
      {
      case SAT_READ_VERIFY_SECTORS:
        TI_DBG1(("satChainedVerifyCB: SAT_READ_VERIFY_SECTORS\n"));
        break;
      case SAT_READ_VERIFY_SECTORS_EXT:
        TI_DBG1(("satChainedVerifyCB: SAT_READ_VERIFY_SECTORS_EXT\n"));
        break;
      default:
        TI_DBG1(("satChainedVerifyCB: error default case command 0x%x\n", hostToDevFis->h.command));
        break;
      }

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    } /* end error checking */
  }

  /* process success from this point on */
  switch (hostToDevFis->h.command)
  {
  case SAT_READ_VERIFY_SECTORS: /* fall through */
  case SAT_READ_VERIFY_SECTORS_EXT:
    TI_DBG5(("satChainedVerifyCB: SAT_WRITE_DMA_EXT success \n"));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      ostiInitiatorIOCompleted( tiRoot,

                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
      return;
    }

    if (satOrgIOContext->superIOFlag)
    {
      dataLength = ((tiSuperScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }
    else
    {
      dataLength = ((tiScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }

    satNewIntIo = satAllocIntIoResource( tiRoot,
                                         tiOrgIORequest,
                                         satDevData,
                                         dataLength,
                                         satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* memory allocation failure */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                satOrgIOContext->interruptContext );

      TI_DBG1(("satChainedVerifyCB: momory allocation fails\n"));
      return;
    } /* end of memory allocation failure */

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = satPrepareNewIO(
                                      satNewIntIo,
                                      tiOrgIORequest,
                                      satDevData,
                                      scsiCmnd,
                                      satOrgIOContext
                                      );
    status = satChainedVerify(tiRoot,
                                    &satNewIntIo->satIntTiIORequest,
                                    satNewIOContext->ptiDeviceHandle,
                                    &satNewIntIo->satIntTiScsiXchg,
                                    satNewIOContext);

    if (status != tiSuccess)
    {
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                satOrgIOContext->interruptContext );
      TI_DBG1(("satChainedVerifyCB: calling satChainedVerify fails\n"));
      return;
    }

    break;
  default:
    TI_DBG1(("satChainedVerifyCB: success but error default case command 0x%x\n", hostToDevFis->h.command));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    break;
  }
  return;
}

/*****************************************************************************
*! \brief  satTestUnitReadyCB
*
*   This routine is a callback function for satTestUnitReady()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satTestUnitReadyCB(
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
    In the process of TestUnitReady
    Process SAT_GET_MEDIA_STATUS
    Process SAT_CHECK_POWER_MODE
  */
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     ataError;

  bit32                     status;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  TI_DBG6(("satTestUnitReadyCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG5(("satTestUnitReadyCB: no internal satInternalIo_t satIntIoContext\n"));
    pSense        = satIOContext->pSense;
    scsiCmnd      = satIOContext->pScsiCmnd;
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
  }
  else
  {
    TI_DBG5(("satTestUnitReadyCB: yes internal satInternalIo_t satIntIoContext\n"));

    /* orginal tiIOContext */
    tiOrgIORequest         = (tiIORequest_t *)satIOContext->satIntIoContext->satOrgTiIORequest;
    tdOrgIORequestBody     = (tdIORequestBody_t *)tiOrgIORequest->tdData;
    satOrgIOContext        = &(tdOrgIORequestBody->transport.SATA.satIOContext);

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     TI_DBG1(("satTestUnitReadyCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    satSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                          satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                            satDevData,
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
      TI_DBG1(("satTestUnitReadyCB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
            )
    {
      TI_DBG1(("satTestUnitReadyCB: FAILED, FAILED, error status\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    switch (hostToDevFis->h.command)
    {
    case SAT_GET_MEDIA_STATUS:
      TI_DBG1(("satTestUnitReadyCB: SAT_GET_MEDIA_STATUS failed \n"));

      /* checking NM bit */
      if (ataError & SCSI_NM_MASK)
      {
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_NOT_READY,
                            0,
                            SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                            satOrgIOContext);
      }
      else
      {
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_NOT_READY,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                            satOrgIOContext);
      }

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      break;
    case SAT_CHECK_POWER_MODE:
      TI_DBG1(("satTestUnitReadyCB: SAT_CHECK_POWER_MODE failed \n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_DOES_NOT_RESPOND_TO_SELECTION,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      break;
    default:
      TI_DBG1(("satTestUnitReadyCB: default failed command %d\n", hostToDevFis->h.command));

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      break;

    }
    return;
  }/* end error */

  /* ATA command completes sucessfully */
  switch (hostToDevFis->h.command)
  {
  case SAT_GET_MEDIA_STATUS:

    TI_DBG5(("satTestUnitReadyCB: SAT_GET_MEDIA_STATUS success\n"));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    satNewIntIo = satAllocIntIoResource( tiRoot,
                                         tiOrgIORequest,
                                         satDevData,
                                         0,
                                         satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* memory allocation failure */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      TI_DBG1(("satTestUnitReadyCB: momory allocation fails\n"));
      return;
    }

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = satPrepareNewIO(
                                      satNewIntIo,
                                      tiOrgIORequest,
                                      satDevData,
                                      scsiCmnd,
                                      satOrgIOContext
                                      );

    /* sends SAT_CHECK_POWER_MODE */
    status = satTestUnitReady_1( tiRoot,
                               &satNewIntIo->satIntTiIORequest,
                               satNewIOContext->ptiDeviceHandle,
                               &satNewIntIo->satIntTiScsiXchg,
                               satNewIOContext);

   if (status != tiSuccess)
   {
     /* sending SAT_CHECK_POWER_MODE fails */
     satFreeIntIoResource( tiRoot,
                           satDevData,
                           satNewIntIo);
     satSetSensePayload( pSense,
                         SCSI_SNSKEY_NOT_READY,
                         0,
                         SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                         satOrgIOContext);

     ostiInitiatorIOCompleted( tiRoot,
                               tiOrgIORequest,
                               tiIOSuccess,
                               SCSI_STAT_CHECK_CONDITION,
                               satOrgIOContext->pTiSenseData,
                               satOrgIOContext->interruptContext );

      TI_DBG1(("satTestUnitReadyCB: calling satTestUnitReady_1 fails\n"));
      return;
   }

    break;
  case SAT_CHECK_POWER_MODE:
    TI_DBG5(("satTestUnitReadyCB: SAT_CHECK_POWER_MODE success\n"));


    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    /* returns good status */
    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext );

    break;
  default:
    TI_DBG1(("satTestUnitReadyCB: default success command %d\n", hostToDevFis->h.command));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NOT_READY,
                        0,
                        SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );


    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    break;
  }

  return;
}

/*****************************************************************************
*! \brief  satWriteSame10CB
*
*   This routine is a callback function for satWriteSame10()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satWriteSame10CB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      void              *agParam,
                      void              *ioContext
                      )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  tdIORequestBody_t       *tdNewIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;

  bit32                     sectorcount = 0;
  bit32                     lba = 0, tl = 0;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  agsaFisSetDevBitsHeader_t *statSetDevBitFisHeader = agNULL;

  TI_DBG5(("satWriteSame10CB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satWriteSame10CB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satWriteSame10CB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satWriteSame10CB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satWriteSame10CB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }


  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     TI_DBG1(("satWriteSame10CB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
     satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

     ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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

      /* ATA Eror register   */

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
      TI_DBG1(("satWriteSame10CB: FAILED, NOT IO_SUCCESS\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      TI_DBG1(("satWriteSame10CB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if (statDevToHostFisHeader->fisType != SET_DEV_BITS_FIS)
    {
      TI_DBG1(("satWriteSame10CB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      TI_DBG1(("satWriteSame10CB: FAILED, FAILED, error status\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );


      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    /* for debugging */
    switch (hostToDevFis->h.command)
    {
    case SAT_WRITE_DMA_EXT:
      TI_DBG1(("satWriteSame10CB: SAT_WRITE_DMA_EXT\n"));
      break;
    case SAT_WRITE_SECTORS_EXT:
      TI_DBG1(("satWriteSame10CB: SAT_WRITE_SECTORS_EXT\n"));
      break;
    case SAT_WRITE_FPDMA_QUEUED:
      TI_DBG1(("satWriteSame10CB: SAT_WRITE_FPDMA_QUEUED\n"));
      break;
    default:
      TI_DBG1(("satWriteSame10CB: error default case command 0x%x\n", hostToDevFis->h.command));
      break;
    }

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );


    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
    TI_DBG5(("satWriteSame10CB: SAT_WRITE_DMA_EXT success\n"));
  }
  else if (hostToDevFis->h.command == SAT_WRITE_SECTORS_EXT)
  {
    TI_DBG5(("satWriteSame10CB: SAT_WRITE_SECTORS_EXT success\n"));
  }
  else if (hostToDevFis->h.command == SAT_WRITE_FPDMA_QUEUED)
  {
    TI_DBG5(("satWriteSame10CB: SAT_WRITE_FPDMA_QUEUED success\n"));
  }
  else
  {
    TI_DBG1(("satWriteSame10CB: error case command 0x%x success\n", hostToDevFis->h.command));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );


    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }


  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  /* free */
  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

  /*
    increment LBA by one, keeping the same sector count(1)
    sends another ATA command with the changed parameters
  */

  tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
  satDevData->satSectorDone++;
  tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);

  TI_DBG1(("satWriteSame10CB: sectordone %d\n", satDevData->satSectorDone));

  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
      + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

  TI_DBG5(("satWriteSame10CB: lba 0x%x tl 0x%x\n", lba, tl));

  if (tl == 0)
  {
    /* (satDevData->satMaxUserAddrSectors - 1) - lba*/
    sectorcount = (0x0FFFFFFF - 1) - lba;
  }
  else
  {
    sectorcount = tl;
  }

  if (sectorcount <= 0)
  {
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );
    TI_DBG1(("satWriteSame10CB: incorrect sectorcount 0x%x\n", sectorcount));
    return;
  }

  if (sectorcount == satDevData->satSectorDone)
  {
    /*
      done with writesame
    */
    TI_DBG1(("satWriteSame10CB: return writesame done\n"));
    satDevData->satSectorDone = 0;

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext );
  }
  else
  {
    /* sends another ATA command */
    if (hostToDevFis->h.command == SAT_WRITE_DMA_EXT)
    {
      TI_DBG1(("satWriteSame10CB: sends another SAT_WRITE_DMA_EXT\n"));
    }
    else if (hostToDevFis->h.command == SAT_WRITE_SECTORS_EXT)
    {
      TI_DBG1(("satWriteSame10CB: sends another SAT_WRITE_SECTORS_EXT\n"));
    }
    else if (hostToDevFis->h.command == SAT_WRITE_FPDMA_QUEUED)
    {
      TI_DBG1(("satWriteSame10CB: sends another SAT_WRITE_FPDMA_QUEUED\n"));
    }

    satNewIntIo = satAllocIntIoResource( tiRoot,
                                         tiOrgIORequest,
                                         satDevData,
                                         0,
                                         satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* memory allocation failure */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );
      TI_DBG1(("satWriteSame10CB: momory allocation fails\n"));
      return;
    } /* end memory allocation */

    /* the one to be used */
    tdNewIORequestBody = satNewIntIo->satIntRequestBody;
    satNewIOContext = &tdNewIORequestBody->transport.SATA.satIOContext;

    satNewIOContext->pSatDevData   = satDevData;
    satNewIOContext->pFis          = &tdNewIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev;
    satNewIOContext->pScsiCmnd     = &satNewIntIo->satIntTiScsiXchg.scsiCmnd;
    /* saves scsi command for LBA and number of blocks */
    osti_memcpy(satNewIOContext->pScsiCmnd, scsiCmnd, sizeof(tiIniScsiCmnd_t));
    satNewIOContext->pSense        = &tdNewIORequestBody->transport.SATA.sensePayload;
    satNewIOContext->pTiSenseData  = &tdNewIORequestBody->transport.SATA.tiSenseData;
    satNewIOContext->pTiSenseData->senseData = satNewIOContext->pSense;
    satNewIOContext->tiRequestBody = satNewIntIo->satIntRequestBody;
    satNewIOContext->interruptContext = satNewIOContext->interruptContext;
    satNewIOContext->satIntIoContext  = satNewIntIo;
    satNewIOContext->ptiDeviceHandle = satIOContext->ptiDeviceHandle;
    /* saves tiScsiXchg; only for writesame10() */
    satNewIOContext->tiScsiXchg = satOrgIOContext->tiScsiXchg;

    if (hostToDevFis->h.command == SAT_WRITE_DMA_EXT)
    {
      status = satWriteSame10_1( tiRoot,
                                 &satNewIntIo->satIntTiIORequest,
                                 satNewIOContext->ptiDeviceHandle,
                                 &satNewIntIo->satIntTiScsiXchg,
                                 satNewIOContext,
                                 lba + satDevData->satSectorDone
                                 );
    }
    else if (hostToDevFis->h.command == SAT_WRITE_SECTORS_EXT)
    {
      status = satWriteSame10_2( tiRoot,
                                 &satNewIntIo->satIntTiIORequest,
                                 satNewIOContext->ptiDeviceHandle,
                                 &satNewIntIo->satIntTiScsiXchg,
                                 satNewIOContext,
                                 lba + satDevData->satSectorDone
                                 );
    }
    else if (hostToDevFis->h.command == SAT_WRITE_FPDMA_QUEUED)
    {
      status = satWriteSame10_3( tiRoot,
                                 &satNewIntIo->satIntTiIORequest,
                                 satNewIOContext->ptiDeviceHandle,
                                 &satNewIntIo->satIntTiScsiXchg,
                                 satNewIOContext,
                                 lba + satDevData->satSectorDone
                                 );
    }
    else
    {
      status = tiError;
      TI_DBG1(("satWriteSame10CB: sucess but error in command 0x%x\n", hostToDevFis->h.command));
    }

    if (status != tiSuccess)
    {
      /* sending ATA command fails */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );
      TI_DBG1(("satWriteSame10CB:calling satWriteSame10_1 fails\n"));
      return;
    } /* end send fails */

  } /* end sends another ATA command */

  return;
}
/*****************************************************************************
*! \brief  satStartStopUnitCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Send Diagnostic completion.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satStartStopUnitCB(
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
    In the process of StartStopUnit
    Process FLUSH CACHE (EXT)
    Process STANDBY
    Process READ VERIFY SECTOR(S) EXT
    Process MEDIA EJECT
  */
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  TI_DBG5(("satStartStopUnitCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satStartStopUnitCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satStartStopUnitCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satStartStopUnitCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satStartStopUnitCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     TI_DBG1(("satStartStopUnitCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));

      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        TI_DBG1(("satStartStopUnitCB: immed bit 0\n"));
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_ABORTED_COMMAND,
                            0,
                            SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );


        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);
        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        TI_DBG1(("satStartStopUnitCB: immed bit 1\n"));
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_ABORTED_COMMAND,
                                    0,
                                    SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                    satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );


        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);
        satFreeIntIoResource( tiRoot,
                              satDevData,
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
      TI_DBG1(("satStartStopUnitCB: FAILED, NOT IO_SUCCESS\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      TI_DBG1(("satStartStopUnitCB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      TI_DBG1(("satStartStopUnitCB: FAILED, FAILED, error status\n"));
    }


    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );


      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    switch (hostToDevFis->h.command)
    {
    case SAT_FLUSH_CACHE: /* fall through */
    case SAT_FLUSH_CACHE_EXT:
      TI_DBG1(("satStartStopUnitCB: SAT_FLUSH_CACHE(_EXT)\n"));
      /* check immed bit in scsi command */
      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_ABORTED_COMMAND,
                            0,
                            SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );


        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_ABORTED_COMMAND,
                                    0,
                                    SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                    satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );


        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
      }
      break;
    case SAT_STANDBY:
      TI_DBG5(("satStartStopUnitCB: SAT_STANDBY\n"));
      /* check immed bit in scsi command */
      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_ABORTED_COMMAND,
                            0,
                            SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );


        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_ABORTED_COMMAND,
                                    0,
                                    SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                    satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );


        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
      }
      break;
    case SAT_READ_VERIFY_SECTORS:     /* fall through */
    case SAT_READ_VERIFY_SECTORS_EXT:
      TI_DBG5(("satStartStopUnitCB: SAT_READ_VERIFY_SECTORS(_EXT)\n"));
       /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_ABORTED_COMMAND,
                            0,
                            SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );


        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_ABORTED_COMMAND,
                                    0,
                                    SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                    satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );


        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
      }
      break;
    case SAT_MEDIA_EJECT:
      TI_DBG5(("satStartStopUnitCB: SAT_MEDIA_EJECT\n"));
       /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_ABORTED_COMMAND,
                            0,
                            SCSI_SNSCODE_MEDIA_LOAD_OR_EJECT_FAILED,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );


        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
      }
      /* IMMED == 1 */
      if ( scsiCmnd->cdb[1] & SCSI_IMMED_MASK)
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_ABORTED_COMMAND,
                                    0,
                                    SCSI_SNSCODE_MEDIA_LOAD_OR_EJECT_FAILED,
                                    satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );

        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);
      }
      break;
    default:
      /* unspecified case, return no sense and no addition info */
      TI_DBG5(("satStartStopUnitCB: default command %d\n", hostToDevFis->h.command));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );


      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
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
    TI_DBG5(("satStartStopUnitCB: SAT_READ_VERIFY_SECTORS(_EXT) success case\n"));


    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with SAT_FLUSH_CACHE(_EXT) */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    /* at this point, successful SAT_READ_VERIFY_SECTORS(_EXT)
       send SAT_SATNDBY
    */
    satNewIntIo = satAllocIntIoResource( tiRoot,
                                         tiOrgIORequest,
                                         satDevData,
                                         0,
                                         satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* memory allocation failure */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);
      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_ABORTED_COMMAND,
                            0,
                            SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                            satOrgIOContext);
      }
      else   /* IMMED == 1 */
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_ABORTED_COMMAND,
                                    0,
                                    SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                    satOrgIOContext);
      }
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      TI_DBG1(("satStartStopUnitCB: momory allocation fails\n"));
      return;
    } /* end of memory allocation failure */

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = satPrepareNewIO(
                                      satNewIntIo,
                                      tiOrgIORequest,
                                      satDevData,
                                      scsiCmnd,
                                      satOrgIOContext
                                      );

    /* sending SAT_STANDBY */
    status = satStartStopUnit_1( tiRoot,
                                &satNewIntIo->satIntTiIORequest,
                                satNewIOContext->ptiDeviceHandle,
                                &satNewIntIo->satIntTiScsiXchg,
                                satNewIOContext);

    if (status != tiSuccess)
    {
      /* sending SAT_CHECK_POWER_MODE fails */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);

      /* IMMED == 0 */
      if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
      {
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_ABORTED_COMMAND,
                            0,
                            SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                            satOrgIOContext);
      }
      else   /* IMMED == 1 */
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_ABORTED_COMMAND,
                                    0,
                                    SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR,
                                    satOrgIOContext);
      }
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      TI_DBG1(("satStartStopUnitCB: calling satStartStopUnit_1 fails\n"));
      return;
    }
    break;
  case SAT_STANDBY:
    TI_DBG5(("satStartStopUnitCB: SAT_STANDBY success case\n"));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with SAT_STANDBY */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    /*
      if immed == 0, return good status
     */
    /* IMMED == 0 */
    if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
    {
        ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
    }
    satDevData->satStopState = agTRUE;
    break;
  case SAT_READ_VERIFY_SECTORS:     /* fall through */
  case SAT_READ_VERIFY_SECTORS_EXT:
    TI_DBG5(("satStartStopUnitCB: SAT_READ_VERIFY_SECTORS(_EXT) success case\n"));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with SAT_READ_VERIFY_SECTORS(_EXT) */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    /*
      if immed == 0, return good status
     */
    if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
    {
        ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
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
    satDevData->satStopState = agFALSE;
    break;
  case SAT_MEDIA_EJECT:
    TI_DBG5(("satStartStopUnitCB: SAT_MEDIA_EJECT success case\n"));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with SAT_READ_VERIFY_SECTORS(_EXT) */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    /*
      if immed == 0, return good status
     */
    if (!( scsiCmnd->cdb[1] & SCSI_IMMED_MASK))
    {
        ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
    }
    break;
  default:
    TI_DBG1(("satStartStopUnitCB:success but  error default case command 0x%x\n", hostToDevFis->h.command));

    /* unspecified case, return no sense and no addition info */
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    break;
  }
  return;
}

/*****************************************************************************
*! \brief  satSendDiagnosticCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Send Diagnostic completion.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satSendDiagnosticCB(
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
    In the process of SendDiagnotic
    Process READ VERIFY SECTOR(S) EXT two time
    Process SMART ECECUTE OFF-LINE IMMEDIATE
  */
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;


  TI_DBG5(("satSendDiagnosticCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satSendDiagnosticCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satOrgIOContext->pSense;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satSendDiagnosticCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satSendDiagnosticCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satSendDiagnosticCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
    pSense                 = satOrgIOContext->pSense;
    scsiCmnd               = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
     TI_DBG1(("satSendDiagnosticCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
     satDevData->satVerifyState = 0;
     satDevData->satBGPendingDiag = agFALSE;

    if (hostToDevFis->d.lbaLow != 0x01 && hostToDevFis->d.lbaLow != 0x02)
    {
      /* no completion for background send diagnotic. It is done in satSendDiagnostic() */
      ostiInitiatorIOCompleted (
                                tiRoot,
                                tiOrgIORequest,
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                satOrgIOContext->interruptContext
                               );
     }
     satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

     satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
    return;

  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* only agsaFisRegDeviceToHost_t is expected */
    statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
    ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  }

  TI_DBG5(("satSendDiagnosticCB: fis command 0x%x\n", hostToDevFis->h.command));

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
  /*
    checking IO status, FIS type and error status
  */
  satDevData->satVerifyState = 0;
  satDevData->satBGPendingDiag = agFALSE;

  if( (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS) ||
      ((ataStatus & ERR_ATA_STATUS_MASK) || (ataStatus & DF_ATA_STATUS_MASK))
      )
  {

    /* for debugging */
    if( agIOStatus != OSSA_IO_SUCCESS)
    {
      if ( (hostToDevFis->h.command == SAT_SMART_RETURN_STATUS) ||
           (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT) )
      {
        TI_DBG1(("satSendDiagnosticCB: FAILED, NOT IO_SUCCESS and SAT_READ_VERIFY_SECTORS(_EXT)\n"));
      }
      else
      {
        TI_DBG1(("satSendDiagnosticCB: FAILED, NOT IO_SUCCESS and SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE\n"));
      }
    }

    /* for debugging */
    if( statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      if ( (hostToDevFis->h.command == SAT_SMART_RETURN_STATUS) ||
           (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT) )
      {
        TI_DBG1(("satSendDiagnosticCB: FAILED, Wrong FIS type 0x%x and SAT_READ_VERIFY_SECTORS(_EXT)\n", statDevToHostFisHeader->fisType));
      }
      else
      {
        TI_DBG1(("satSendDiagnosticCB: FAILED, Wrong FIS type 0x%x and SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE\n",statDevToHostFisHeader->fisType));
      }
    }

    /* for debugging */
    if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      if ( (hostToDevFis->h.command == SAT_SMART_RETURN_STATUS) ||
           (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT) )
      {
        TI_DBG1(("satSendDiagnosticCB: FAILED, error status and SAT_READ_VERIFY_SECTORS(_EXT)\n"));
      }
      else
      {
        TI_DBG1(("satSendDiagnosticCB: FAILED, error status and SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE\n"));
      }
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    if ( (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS) ||
         (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT) )
    {
      /* report using the original tiIOrequst */
      /* failed during sending SAT_READ_VERIFY_SECTORS(_EXT) */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_HARDWARE_ERROR,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }
    else
    {
      /* report using the original tiIOrequst */
      /* failed during sending SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_HARDWARE_ERROR,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST,
                          satOrgIOContext);

      if (hostToDevFis->d.lbaLow != 0x01 && hostToDevFis->d.lbaLow != 0x02)
      {
        /* no completion for background send diagnotic. It is done in satSendDiagnostic() */
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );

      }
      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
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
    TI_DBG5(("satSendDiagnosticCB: SAT_READ_VERIFY_SECTORS(_EXT) case\n"));
    tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
    satDevData->satVerifyState++;
    tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);
    TI_DBG5(("satSendDiagnosticCB: satVerifyState %d\n",satDevData->satVerifyState));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with internally genereated AT_READ_VERIFY_SECTORS(_EXT) */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    if (satDevData->satVerifyState == 3)
    {
      /* reset satVerifyState */
      satDevData->satVerifyState = 0;
      /* return GOOD status */
      TI_DBG5(("satSendDiagnosticCB: return GOOD status\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
     return;
    }
    else
    {
      
      /* prepare SAT_READ_VERIFY_SECTORS(_EXT) */
      satNewIntIo = satAllocIntIoResource( tiRoot,
                                           tiOrgIORequest,
                                           satDevData,
                                           0,
                                           satNewIntIo);
      if (satNewIntIo == agNULL)
      {
        /* reset satVerifyState */
        satDevData->satVerifyState = 0;
        /* memory allocation failure */
        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satNewIntIo);

        /* failed as a part of sending SAT_READ_VERIFY_SECTORS(_EXT) */
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );

        TI_DBG1(("satSendDiagnosticCB: momory allocation fails\n"));
        return;
      } /* end of memory allocation failure */

      /*
       * Need to initialize all the fields within satIOContext
       */

      satNewIOContext = satPrepareNewIO(
                                        satNewIntIo,
                                        tiOrgIORequest,
                                        satDevData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );

      if (satDevData->satVerifyState == 1)
      {
        /* sending SAT_CHECK_POWER_MODE */
        status = satSendDiagnostic_1( tiRoot,
                                      &satNewIntIo->satIntTiIORequest,
                                      satNewIOContext->ptiDeviceHandle,
                                      &satNewIntIo->satIntTiScsiXchg,
                                      satNewIOContext);
      }
      else
      {
        /* satDevData->satVerifyState == 2 */
        status = satSendDiagnostic_2( tiRoot,
                                      &satNewIntIo->satIntTiIORequest,
                                      satNewIOContext->ptiDeviceHandle,
                                      &satNewIntIo->satIntTiScsiXchg,
                                      satNewIOContext);
      }

      if (status != tiSuccess)
      {
        /* sending SAT_READ_VERIFY_SECTORS(_EXT) fails */
        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satNewIntIo);

        /* failed during sending SAT_READ_VERIFY_SECTORS(_EXT) */
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );

        /* reset satVerifyState */
        satDevData->satVerifyState = 0;
        TI_DBG1(("satSendDiagnosticCB: calling satSendDiagnostic_1 or _2 fails\n"));
        return;
      }
    } /* satDevData->satVerifyState == 1 or 2 */

    break;
  case SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE:
    TI_DBG5(("satSendDiagnosticCB: SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE case\n"));

    satDevData->satBGPendingDiag = agFALSE;

    if (hostToDevFis->d.lbaLow == 0x01 || hostToDevFis->d.lbaLow == 0x02)
    {
      /* for background send diagnostic, no completion here. It is done already. */
      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      /* done with AT_SMART_EXEUTE_OFF_LINE_IMMEDIATE */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      TI_DBG5(("satSendDiagnosticCB: returning but no IOCompleted\n"));
    }
    else
    {
      TI_DBG5(("satSendDiagnosticCB: returning good status for senddiagnostic\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );


      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      /* done with AT_SMART_EXEUTE_OFF_LINE_IMMEDIATE */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
    }

    break;
  default:
    TI_DBG1(("satSendDiagnosticCB: success but error default case command 0x%x\n", hostToDevFis->h.command));
    /* unspecified case, return no sense and no addition info */
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    break;
  }
  return;
}
/*****************************************************************************
*! \brief  satRequestSenseCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Request Sense completion.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
/*
  CB for internnaly generated SMART_RETURN_STATUS and SAT_CHECK_POWER_MODE
  in the process of RequestSense

*/
void satRequestSenseCB(
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

  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIORequest_t             *tiOrgIORequest;
  tiIniScsiCmnd_t           *scsiCmnd;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  agsaFisRegD2HData_t       statDevToHostFisData;
  bit32                     lenReceived = 0;
  bit32                     dataLength;

  TI_DBG4(("satRequestSenseCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  /*ttttttthe one */
  if (satIntIo == agNULL)
  {
    TI_DBG4(("satRequestSenseCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    if (satOrgIOContext->superIOFlag)
    {
      pSense = (scsiRspSense_t *)(((tiSuperScsiInitiatorRequest_t *)satOrgIOContext->tiScsiXchg)->sglVirtualAddr);//satOrgIOContext->pSense;
    }
    else
    {
      pSense = (scsiRspSense_t *)(((tiScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->sglVirtualAddr);//satOrgIOContext->pSense;
    }
    scsiCmnd = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satRequestSenseCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satRequestSenseCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satRequestSenseCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    if (satOrgIOContext->superIOFlag)
    {
      pSense = (scsiRspSense_t *)(((tiSuperScsiInitiatorRequest_t *)satOrgIOContext->tiScsiXchg)->sglVirtualAddr);//satOrgIOContext->pSense;
    }
    else
    {
      pSense = (scsiRspSense_t *)(((tiScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->sglVirtualAddr);//satOrgIOContext->pSense;
    }
    scsiCmnd = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  TI_DBG4(("satRequestSenseCB: fis command 0x%x\n", hostToDevFis->h.command));

  lenReceived = scsiCmnd->cdb[4];
  TI_DBG1(("satRequestSenseCB: lenReceived in CDB %d 0x%x\n", lenReceived,lenReceived));

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satRequestSenseCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      if (hostToDevFis->h.command == SAT_SMART_RETURN_STATUS)
      {
        TI_DBG1(("satRequestSenseCB: FAILED, Wrong FIS type 0x%x and SAT_SMART_RETURN_STATU\n", statDevToHostFisHeader->fisType));
      }
      else
      {
        TI_DBG1(("satRequestSenseCB: FAILED, Wrong FIS type 0x%x and SAT_CHECK_POWER_MODE\n",statDevToHostFisHeader->fisType));
      }
    }

    /* for debugging */
    if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
         (ataStatus & DF_ATA_STATUS_MASK)
         )
    {
      if (hostToDevFis->h.command == SAT_SMART_RETURN_STATUS)
      {
        TI_DBG1(("satRequestSenseCB: FAILED, error status and SAT_SMART_RETURN_STATU\n"));
      }
      else
      {
        TI_DBG1(("satRequestSenseCB: FAILED, error status and SAT_CHECK_POWER_MODE\n"));
      }
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    if (hostToDevFis->h.command == SAT_SMART_RETURN_STATUS)
    {
      /* report using the original tiIOrequst */
      /* failed during sending SMART RETURN STATUS */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                          satOrgIOContext);

      if (SENSE_DATA_LENGTH < lenReceived)
      {
        /* underrun */
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOUnderRun,
                                  lenReceived - SENSE_DATA_LENGTH,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }
      else
      {
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }
    }
    else
    {
      /* report using the original tiIOrequst */
      /* failed during sending SAT_CHECK_POWER_MODE */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_LOW_POWER_CONDITION_ON,
                          satOrgIOContext);

      if (SENSE_DATA_LENGTH < lenReceived)
      {
        /* underrun */
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOUnderRun,
                                  lenReceived - SENSE_DATA_LENGTH,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
       }
       else
       {
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
       }
    }


    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
    return;
  }

  saFrameReadBlock(agRoot, agParam, 0, &statDevToHostFisData, sizeof(agsaFisRegD2HData_t));

  switch (hostToDevFis->h.command)
  {
  case SAT_SMART_RETURN_STATUS:
    TI_DBG4(("satRequestSenseCB: SAT_SMART_RETURN_STATUS case\n"));
    if (statDevToHostFisData.lbaMid == 0xF4 || statDevToHostFisData.lbaHigh == 0x2C)
    {
      /* threshold exceeds */
      TI_DBG1(("satRequestSenseCB: threshold exceeds\n"));


      /* report using the original tiIOrequst */
      /* failed during sending SMART RETURN STATUS */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                          satOrgIOContext);

      if (SENSE_DATA_LENGTH < lenReceived)
      {
        /* underrun */
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOUnderRun,
                                  lenReceived - SENSE_DATA_LENGTH,
                                  agNULL,
                                  satOrgIOContext->interruptContext );                                     }
      else
      {
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satOrgIOContext->interruptContext );                                     }


      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }


    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with internally genereated SAT_SMART_RETURN_STATUS */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    /* at this point, successful SMART_RETURN_STATUS
       xmit SAT_CHECK_POWER_MODE
    */
    if (satOrgIOContext->superIOFlag)
    {
      dataLength = ((tiSuperScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }
    else
    {
      dataLength = ((tiScsiInitiatorRequest_t *) satOrgIOContext->tiScsiXchg)->scsiCmnd.expDataLength;
    }

    satNewIntIo = satAllocIntIoResource( tiRoot,
                                         tiOrgIORequest,
                                         satDevData,
                                         dataLength,
                                         satNewIntIo);
    if (satNewIntIo == agNULL)
    {
      /* memory allocation failure */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);

      /* failed as a part of sending SMART RETURN STATUS */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                          satOrgIOContext);

      if (SENSE_DATA_LENGTH < lenReceived)
      {
        /* underrun */
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest,
                                  tiIOUnderRun,
                                  lenReceived - SENSE_DATA_LENGTH,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }
      else
      {
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }

      TI_DBG1(("satRequestSenseCB: momory allocation fails\n"));
      return;
    } /* end of memory allocation failure */


    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = satPrepareNewIO(
                                      satNewIntIo,
                                      tiOrgIORequest,
                                      satDevData,
                                      scsiCmnd,
                                      satOrgIOContext
                                      );

    /* sending SAT_CHECK_POWER_MODE */
    status = satRequestSense_1( tiRoot,
                               &satNewIntIo->satIntTiIORequest,
                               satNewIOContext->ptiDeviceHandle,
                               &satNewIntIo->satIntTiScsiXchg,
                               satNewIOContext);

    if (status != tiSuccess)
    {
      /* sending SAT_CHECK_POWER_MODE fails */
      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satNewIntIo);

      /* failed during sending SAT_CHECK_POWER_MODE */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_LOW_POWER_CONDITION_ON,
                          satOrgIOContext);

      if (SENSE_DATA_LENGTH < lenReceived)
      {
        /* underrun */
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest,
                                  tiIOUnderRun,
                                  lenReceived - SENSE_DATA_LENGTH,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }
      else
      {
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }

      TI_DBG1(("satRequestSenseCB: calling satRequestSense_1 fails\n"));
      return;
    }

    break;
  case SAT_CHECK_POWER_MODE:
    TI_DBG4(("satRequestSenseCB: SAT_CHECK_POWER_MODE case\n"));

    /* check ATA STANDBY state */
    if (statDevToHostFisData.sectorCount == 0x00)
    {
      /* in STANDBY */
      TI_DBG1(("satRequestSenseCB: in standby\n"));


      /* report using the original tiIOrequst */
      /* failed during sending SAT_CHECK_POWER_MODE */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_LOW_POWER_CONDITION_ON,
                          satOrgIOContext);

      if (SENSE_DATA_LENGTH < lenReceived)
      {
        /* underrun */
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOUnderRun,
                                  lenReceived - SENSE_DATA_LENGTH,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }
      else
      {
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* done with internnaly generated SAT_CHECK_POWER_MODE */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    if (satDevData->satFormatState == agTRUE)
    {
      TI_DBG1(("satRequestSenseCB: in format\n"));


      /* report using the original tiIOrequst */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_FORMAT_IN_PROGRESS,
                          satOrgIOContext);

      if (SENSE_DATA_LENGTH < lenReceived)
      {
        /* underrun */
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOUnderRun,
                                  lenReceived - SENSE_DATA_LENGTH,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }
      else
      {
        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satOrgIOContext->interruptContext );
      }

      return;
    }

    /* normal: returns good status for requestsense */
    /* report using the original tiIOrequst */
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);
    TI_DBG4(("satRequestSenseCB: returning good status for requestsense\n"));
    if (SENSE_DATA_LENGTH < lenReceived)
    {
      /* underrun */
      TI_DBG6(("satRequestSenseCB reporting underrun lenNeeded=0x%x lenReceived=0x%x tiIORequest=%p\n",
        SENSE_DATA_LENGTH, lenReceived, tiOrgIORequest));
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOUnderRun,
                                lenReceived - SENSE_DATA_LENGTH,
                                agNULL,
                                satOrgIOContext->interruptContext );

    }
    else
    {
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
    }

    break;
  default:
     TI_DBG1(("satRequestSenseCB: success but error default case command 0x%x\n", hostToDevFis->h.command));
     /* pSense here is a part of satOrgIOContext */
     pSense = satOrgIOContext->pTiSenseData->senseData;
     satOrgIOContext->pTiSenseData->senseLen = SENSE_DATA_LENGTH;
     /* unspecified case, return no sense and no addition info */
     satSetSensePayload( pSense,
                         SCSI_SNSKEY_NO_SENSE,
                         0,
                         SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                         satOrgIOContext);

     ostiInitiatorIOCompleted( tiRoot,
                               tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                               tiIOSuccess,
                               SCSI_STAT_CHECK_CONDITION,
                               satOrgIOContext->pTiSenseData,
                               satOrgIOContext->interruptContext );

     satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

     satFreeIntIoResource( tiRoot,
                           satDevData,
                           satIntIo);
    break;
  } /* switch */

  return;
}

/*****************************************************************************
*! \brief  satSynchronizeCache10n16CB
*
*   This routine is a callback function for satSynchronizeCache10 and
*   satSynchronizeCache1016()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satSynchronizeCache10n16CB(
                                agsaRoot_t        *agRoot,
                                agsaIORequest_t   *agIORequest,
                                bit32             agIOStatus,
                                agsaFisHeader_t   *agFirstDword,
                                bit32             agIOInfoLen,
                                void              *agParam,
                                void              *ioContext
                                )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;

  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  TI_DBG5(("satSynchronizeCache10n16CB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  /* SPC: Self-Test Result Log page */

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satSynchronizeCache10n16CB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satIOContext->pSense;
    scsiCmnd        = satIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satSynchronizeCache10n16CB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satSynchronizeCache10n16CB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satSynchronizeCache10n16CB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satSynchronizeCache10n16CB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));

    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      TI_DBG1(("satSynchronizeCache10n16CB: FAILED, NOT IO_SUCCESS\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      TI_DBG1(("satSynchronizeCache10n16CB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      TI_DBG1(("satSynchronizeCache10n16CB: FAILED, FAILED, error status\n"));
    }


    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    switch (hostToDevFis->h.command)
    {
    case SAT_FLUSH_CACHE:
      TI_DBG1(("satSynchronizeCache10n16CB: SAT_FLUSH_CACHE failed\n"));
      /* checking IMMED bit */
      if (scsiCmnd->cdb[1] & SCSI_FLUSH_CACHE_IMMED_MASK)
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_NO_SENSE,
                                    0,
                                    SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                                    satOrgIOContext);
      }
      else
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_NO_SENSE,
                                    0,
                                    SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                                    satOrgIOContext);
      }


      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
      break;
    case SAT_FLUSH_CACHE_EXT:
      TI_DBG1(("satSynchronizeCache10n16CB: SAT_FLUSH_CACHE_EXT failed\n"));
       /* checking IMMED bit */
      if (scsiCmnd->cdb[1] & SCSI_FLUSH_CACHE_IMMED_MASK)
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_NO_SENSE,
                                    0,
                                    SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                                    satOrgIOContext);
      }
      else
      {
        satSetDeferredSensePayload( pSense,
                                    SCSI_SNSKEY_NO_SENSE,
                                    0,
                                    SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                                    satOrgIOContext);
      }


      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
      break;
    default:
      TI_DBG1(("satSynchronizeCache10n16CB: error unknown command 0x%x\n", hostToDevFis->h.command));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);


      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
      break;
    }

    return;
  } /* end of error checking */
  }

  /* prcessing the success case */
  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);


  switch (hostToDevFis->h.command)
  {
  case SAT_FLUSH_CACHE:
    TI_DBG5(("satSynchronizeCache10n16CB: SAT_FLUSH_CACHE success\n"));

    /* checking IMMED bit */
    if ( !(scsiCmnd->cdb[1] & SCSI_FLUSH_CACHE_IMMED_MASK))
    {
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
      return;
    }


    break;
  case SAT_FLUSH_CACHE_EXT:
    TI_DBG5(("satSynchronizeCache10n16CB: SAT_FLUSH_CACHE_EXT success\n"));

    /* checking IMMED bit */
    if ( !(scsiCmnd->cdb[1] & SCSI_FLUSH_CACHE_IMMED_MASK))
    {
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
      return;
    }

    break;
  default:
    TI_DBG5(("satSynchronizeCache10n16CB: error unknown command 0x%x\n", hostToDevFis->h.command));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);


    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    return;
    break;
  }

  return;
}

/*****************************************************************************
*! \brief  satModeSelect6n10CB
*
*   This routine is a callback function for satModeSelect6() and
*   satModeSelect10()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satModeSelect6n10CB(
                         agsaRoot_t        *agRoot,
                         agsaIORequest_t   *agIORequest,
                         bit32             agIOStatus,
                         agsaFisHeader_t   *agFirstDword,
                         bit32             agIOInfoLen,
                         void              *agParam,
                         void              *ioContext
                         )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  tiScsiInitiatorRequest_t *tiScsiRequest; /* tiScsiXchg */
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  TI_DBG5(("satModeSelect6n10CB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satModeSelect6n10CB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    tiScsiRequest   = satOrgIOContext->tiScsiXchg;
    pSense          = satOrgIOContext->pSense;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satModeSelect6n10CB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satModeSelect6n10CB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satModeSelect6n10CB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    tiScsiRequest = satOrgIOContext->tiScsiXchg;
    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satModeSelect6n10CB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      TI_DBG1(("satModeSelect6n10CB FAILED, NOT IO_SUCCESS\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      TI_DBG1(("satModeSelect6n10CB FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      TI_DBG1(("satModeSelect6n10CB FAILED, FAILED, error status\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    /* for debugging */
    if (hostToDevFis->h.command == SAT_SET_FEATURES)
    {
      if ((hostToDevFis->h.features == 0x82) || (hostToDevFis->h.features == 0x02))
      {
        TI_DBG1(("satModeSelect6n10CB 1 SAT_SET_FEATURES failed, feature 0x%x\n", hostToDevFis->h.features));
      }
      else if ((hostToDevFis->h.features == 0xAA) || (hostToDevFis->h.features == 0x55))
      {
        TI_DBG1(("ssatModeSelect6n10CB 2 SAT_SET_FEATURES failed, feature 0x%x\n", hostToDevFis->h.features));
      }
      else
      {
        TI_DBG1(("satModeSelect6n10CB error unknown command 0x%x feature 0x%x\n", hostToDevFis->h.command, hostToDevFis->h.features));
      }
    }
    else if (hostToDevFis->h.command == SAT_SMART)
    {
      if ((hostToDevFis->h.features == SAT_SMART_ENABLE_OPERATIONS) || (hostToDevFis->h.features == SAT_SMART_DISABLE_OPERATIONS))
      {
        TI_DBG1(("satModeSelect6n10CB SAT_SMART_ENABLE/DISABLE_OPERATIONS failed, feature 0x%x\n", hostToDevFis->h.features));
      }
      else
      {
        TI_DBG1(("satModeSelect6n10CB error unknown command 0x%x feature 0x%x\n", hostToDevFis->h.command, hostToDevFis->h.features));
      }
    }
    else
    {
      TI_DBG1(("satModeSelect6n10CB error default case command 0x%x\n", hostToDevFis->h.command));
    }


    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );


    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  } /* error checking */
  }


  /* prcessing the success case */


  if (hostToDevFis->h.command == SAT_SET_FEATURES)
  {
    if ((hostToDevFis->h.features == 0x82) || (hostToDevFis->h.features == 0x02))
    {
      TI_DBG5(("satModeSelect6n10CB 1 SAT_SET_FEATURES success, feature 0x%x\n", hostToDevFis->h.features));

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);

      satNewIntIo = satAllocIntIoResource( tiRoot,
                                           tiOrgIORequest,
                                           satDevData,
                                           0,
                                           satNewIntIo);
      if (satNewIntIo == agNULL)
      {
        /* memory allocation failure */
        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satNewIntIo);

        satSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );
        TI_DBG1(("satModeSelect6n10CB: momory allocation fails\n"));
        return;
      } /* end memory allocation */

      satNewIOContext = satPrepareNewIO(
                                        satNewIntIo,
                                        tiOrgIORequest,
                                        satDevData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );
      /* sends either ATA SET FEATURES based on DRA bit */
      status = satModeSelect6n10_1( tiRoot,
                                 &satNewIntIo->satIntTiIORequest,
                                 satNewIOContext->ptiDeviceHandle,
                                 tiScsiRequest, /* orginal from OS layer */
                                 satNewIOContext
                                 );

      if (status != tiSuccess)
      {
        /* sending ATA command fails */
        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satNewIntIo);
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );
        TI_DBG1(("satModeSelect6n10CB calling satModeSelect6_1 fails\n"));
        return;
      } /* end send fails */
      return;
    }
    else if ((hostToDevFis->h.features == 0xAA) || (hostToDevFis->h.features == 0x55))
    {
      TI_DBG5(("satModeSelect6n10CB 2 SAT_SET_FEATURES success, feature 0x%x\n", hostToDevFis->h.features));

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);

      /* return stat_good */
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
      return;
    }
    else
    {
      TI_DBG1(("satModeSelect6n10CB error unknown command success 0x%x feature 0x%x\n", hostToDevFis->h.command, hostToDevFis->h.features));

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );
      return;
    }
  }
  else if (hostToDevFis->h.command == SAT_SMART_ENABLE_OPERATIONS ||
             hostToDevFis->h.command == SAT_SMART_DISABLE_OPERATIONS
            )
  {
    if ((hostToDevFis->h.features == 0xD8) || (hostToDevFis->h.features == 0xD9))
    {
      TI_DBG5(("satModeSelect6n10CB SAT_SMART_ENABLE/DISABLE_OPERATIONS success, feature 0x%x\n", hostToDevFis->h.features));

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      /* return stat_good */
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
      return;
    }
    else
    {
      TI_DBG1(("satModeSelect6n10CB error unknown command failed 0x%x feature 0x%x\n", hostToDevFis->h.command, hostToDevFis->h.features));

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );
      return;
    }
  }

  else
  {
    TI_DBG1(("satModeSelect6n10CB error default case command success 0x%x\n", hostToDevFis->h.command));

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );
    return;
  }

  return;
}

/*****************************************************************************
*! \brief  satSMARTEnableCB
*
*   This routine is a callback function for satSMARTEnable()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satSMARTEnableCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      void              *agParam,
                      void              *ioContext
                      )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;
  tiIORequest_t             *tiOrgIORequest;
  tiIniScsiCmnd_t           *scsiCmnd;
  bit32                     status;

  TI_DBG4(("satSMARTEnableCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;

  /*ttttttthe one */
  if (satIntIo == agNULL)
  {
    TI_DBG4(("satSMARTEnableCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satSMARTEnableCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satSMARTEnableCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satSMARTEnableCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    scsiCmnd               = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satSMARTEnableCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    return;
  }

  /*
    checking IO status, FIS type and error status
  */
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satSMARTEnableCB: not success status, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    return;
  }

  /* process success case */
  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

  satNewIntIo = satAllocIntIoResource( tiRoot,
                                       tiOrgIORequest,
                                       satDevData,
                                       512,
                                       satNewIntIo);

  if (satNewIntIo == agNULL)
  {
    /* memory allocation failure */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satNewIntIo);

    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );
    return;
  }

  satNewIOContext = satPrepareNewIO(
                                    satNewIntIo,
                                    tiOrgIORequest,
                                    satDevData,
                                    scsiCmnd,
                                    satOrgIOContext
                                    );

  status = satLogSense_1(tiRoot,
                        &satNewIntIo->satIntTiIORequest,
                        satNewIOContext->ptiDeviceHandle,
                        &satNewIntIo->satIntTiScsiXchg,
                        satNewIOContext);

  if (status != tiSuccess)
  {
    /* sending SAT_CHECK_POWER_MODE fails */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satNewIntIo);

    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );

    return;
  }

  return;
}


/*****************************************************************************
*! \brief  satLogSenseCB
*
*   This routine is a callback function for satLogSense()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satLogSenseCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   void              *agParam,
                   void              *ioContext
                )
{

  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;

  scsiRspSense_t            *pSense;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  tiScsiInitiatorRequest_t *tiScsiRequest; /* tiScsiXchg */
  tiScsiInitiatorRequest_t *tiOrgScsiRequest; /* tiScsiXchg */
  satReadLogExtSelfTest_t   *virtAddr1;
  satSmartReadLogSelfTest_t *virtAddr2;
  bit8                      *pLogPage;
  bit8                      SelfTestExecutionStatus = 0;
  bit32                     i = 0;

  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;
  agsaFisRegD2HData_t      statDevToHostFisData;
  tiIniScsiCmnd_t          *scsiCmnd;
  bit32                     lenReceived = 0;

  TI_DBG5(("satLogSenseCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  if (satIOContext == agNULL)
  {
    TI_DBG1(("satLogSenseCB: satIOContext is NULL\n"));
    return;
  }
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satLogSenseCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satOrgIOContext->pSense;
    tiOrgScsiRequest   = satOrgIOContext->tiScsiXchg;
    /* SCSI command response payload to OS layer */
    pLogPage        = (bit8 *) tiOrgScsiRequest->sglVirtualAddr;
    /* ATA command response payload */
    tiScsiRequest   = satOrgIOContext->tiScsiXchg;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satLogSenseCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satLogSenseCB: satOrgIOContext is NULL\n"));
    }
    else
    {
      TI_DBG4(("satLogSenseCB: satOrgIOContext is NOT NULL\n"));
    }

    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;
    tiOrgScsiRequest   = satOrgIOContext->tiScsiXchg;
    /* SCSI command response payload to OS layer */
    pLogPage        = (bit8 *) tiOrgScsiRequest->sglVirtualAddr;
    /* ATA command response payload */
    tiScsiRequest   =  (tiScsiInitiatorRequest_t *)&(satIntIo->satIntTiScsiXchg);
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satLogSenseCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      TI_DBG1(("satLogSenseCB: FAILED, NOT IO_SUCCESS\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      TI_DBG1(("satLogSenseCB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if (statDevToHostFisHeader->fisType != PIO_SETUP_DEV_TO_HOST_FIS)
    {
      TI_DBG1(("satLogSenseCB: FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      TI_DBG1(("satLogSenseCB: FAILED, FAILED, error status\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    /* for debugging */
    if (hostToDevFis->h.command == SAT_READ_LOG_EXT)
    {
      TI_DBG1(("satLogSenseCB: SAT_READ_LOG_EXT failed\n"));
    }
    else if (hostToDevFis->h.command == SAT_SMART)
    {
      if (hostToDevFis->h.features == SAT_SMART_READ_LOG)
      {
        TI_DBG1(("satLogSenseCB: SAT_SMART_READ_LOG failed\n"));
      }
      else if (hostToDevFis->h.features == SAT_SMART_RETURN_STATUS)
      {
        TI_DBG1(("satLogSenseCB: SAT_SMART_RETURN_STATUS failed\n"));
      }
      else
      {
        TI_DBG1(("satLogSenseCB: error unknown command 0x%x feature 0x%x\n", hostToDevFis->h.command, hostToDevFis->h.features));
      }
    }
    else
    {
      TI_DBG1(("satLogSenseCB: error default case command 0x%x\n", hostToDevFis->h.command));
    }

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;

  } /* error checking */
  }

  /* prcessing the success case */
  saFrameReadBlock(agRoot, agParam, 0, &statDevToHostFisData, sizeof(agsaFisRegD2HData_t));

  lenReceived = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];
  TI_DBG5(("satLogSenseCB: lenReceived in CDB %d 0x%x\n", lenReceived,lenReceived));


  if (hostToDevFis->h.command == SAT_READ_LOG_EXT)
  {
    TI_DBG5(("satLogSenseCB: SAT_READ_LOG_EXT success\n"));

    /* process log data and sends it to upper */

    /* ATA: Extended Self-Test Log */
    virtAddr1  = (satReadLogExtSelfTest_t *)(tiScsiRequest->sglVirtualAddr);
    /*
      ATA/ATAPI VOLII, p197, 287
      self-test execution status (4 bits); ((virtAddr1->byte[5] & 0xF0) >> 4)
    */
    SelfTestExecutionStatus  = (bit8)(((virtAddr1->byte[5] & 0xF0) >> 4));

    /* fills in the log page from ATA log page */
    /* SPC-4, 7.2.10, Table 216, 217, p 259 - 260 */
    pLogPage[0] = 0x10; /* page code */
    pLogPage[1] = 0;
    pLogPage[2] = 0x01;    /* 0x190, page length */
    pLogPage[3] = 0x90;

    /* SPC-4, Table 217 */
    pLogPage[4] = 0;    /* Parameter Code */
    pLogPage[5] = 0x01; /* Parameter Code,  unspecfied but ... */
    pLogPage[6] = 3;    /* unspecified but ... */
    pLogPage[7] = 0x10; /* Parameter Length */
    pLogPage[8] = (bit8)(0 | ((virtAddr1->byte[5] & 0xF0) >> 4)); /* Self Test Code and Self-Test Result */
    pLogPage[9] = 0;    /* self test number */
    pLogPage[10] = virtAddr1->byte[7];    /* time stamp, MSB */
    pLogPage[11] = virtAddr1->byte[6];    /* time stamp, LSB */

    pLogPage[12] = 0;    /* address of first failure MSB*/
    pLogPage[13] = 0;    /* address of first failure */
    pLogPage[14] = virtAddr1->byte[14];    /* address of first failure */
    pLogPage[15] = virtAddr1->byte[13];    /* address of first failure */
    pLogPage[16] = virtAddr1->byte[12];    /* address of first failure */
    pLogPage[17] = virtAddr1->byte[11];    /* address of first failure */
    pLogPage[18] = virtAddr1->byte[10];    /* address of first failure */
    pLogPage[19] = virtAddr1->byte[9];    /* address of first failure LSB */

    /* SAT rev8 Table75, p 76 */
    switch (SelfTestExecutionStatus)
    {
    case 0:
      pLogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
      pLogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
      pLogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
      break;
    case 1:
      pLogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
      pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      pLogPage[22] = 0x81;
      break;
    case 2:
      pLogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
      pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      pLogPage[22] = 0x82;
      break;
    case 3:
      pLogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
      pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      pLogPage[22] = 0x83;
      break;
    case 4:
      pLogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
      pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      pLogPage[22] = 0x84;
    break;
    case 5:
      pLogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
      pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      pLogPage[22] = 0x85;
      break;
    case 6:
      pLogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
      pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      pLogPage[22] = 0x86;
      break;
    case 7:
      pLogPage[20] = 0 | SCSI_SNSKEY_MEDIUM_ERROR;
      pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      pLogPage[22] = 0x87;
      break;
    case 8:
      pLogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
      pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
      pLogPage[22] = 0x88;
      break;
    case 9: /* fall through */
    case 10:/* fall through */
    case 11:/* fall through */
    case 12:/* fall through */
    case 13:/* fall through */
    case 14:
      pLogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
      pLogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
      pLogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
      break;
    case 15:
      pLogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
      pLogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
      pLogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
      break;
    default:
      TI_DBG1(("satLogSenseCB: Error, incorrect SelfTestExecutionStatus 0x%x\n", SelfTestExecutionStatus));

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);

      return;
    }

    pLogPage[23] = 0;    /* vendor specific */

    /* the rest of Self-test results log */
    /* 403 is from SPC-4, 7.2.10, Table 216, p 259*/
    for (i=24;i<=403;i++)
    {
      pLogPage[i] = 0;    /* vendor specific */
    }

    if (SELFTEST_RESULTS_LOG_PAGE_LENGTH < lenReceived)
    {
      TI_DBG6(("satLogSenseCB: 1st underrun lenReceived %d len %d \n", lenReceived, SELFTEST_RESULTS_LOG_PAGE_LENGTH));

      /* underrun */
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                tiIOUnderRun,
                                lenReceived - SELFTEST_RESULTS_LOG_PAGE_LENGTH,
                                agNULL,
                                satOrgIOContext->interruptContext );

    }
    else
    {
      ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext);
    }

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    return;
  }
  else if (hostToDevFis->h.command == SAT_SMART_READ_LOG
           || hostToDevFis->h.command == SAT_SMART_RETURN_STATUS)
  {
    if (hostToDevFis->h.features == 0xd5)
    {
      TI_DBG5(("satLogSenseCB: SAT_SMART_READ_LOG success\n"));
      /* process log data and sends it to upper */

      /* ATA: Extended Self-Test Log */
      virtAddr2  = (satSmartReadLogSelfTest_t *)(tiScsiRequest->sglVirtualAddr);
      /*
        SPC-4, p197, 287
        self-test execution status (4 bits); ((virtAddr2->byte[3] & 0xF0) >> 4)
      */
      SelfTestExecutionStatus  = (bit8)(((virtAddr2->byte[3] & 0xF0) >> 4));

      /* fills in the log page from ATA log page */
      /* SPC-4, 7.2.10, Table 216, 217, p 259 - 260 */
      pLogPage[0] = 0x10;    /* page code */
      pLogPage[1] = 0;
      pLogPage[2] = 0x01;    /* 0x190, page length */
      pLogPage[3] = 0x90;    /* 0x190, page length */

      /* SPC-4, Table 217 */
      pLogPage[4] = 0;    /* Parameter Code */
      pLogPage[5] = 0x01; /* Parameter Code unspecfied but ... */
      pLogPage[6] = 3;    /* unspecified but ... */
      pLogPage[7] = 0x10; /* Parameter Length */
      pLogPage[8] = (bit8)(0 | ((virtAddr2->byte[3] & 0xF0) >> 4)); /* Self Test Code and Self-Test Result */
      pLogPage[9] = 0;    /* self test number */
      pLogPage[10] = virtAddr2->byte[5];    /* time stamp, MSB */
      pLogPage[11] = virtAddr2->byte[4];    /* time stamp, LSB */

      pLogPage[12] = 0;    /* address of first failure MSB*/
      pLogPage[13] = 0;    /* address of first failure */
      pLogPage[14] = 0;    /* address of first failure */
      pLogPage[15] = 0;    /* address of first failure */
      pLogPage[16] = virtAddr2->byte[10];    /* address of first failure */
      pLogPage[17] = virtAddr2->byte[9];    /* address of first failure */
      pLogPage[18] = virtAddr2->byte[8];    /* address of first failure */
      pLogPage[19] = virtAddr2->byte[7];    /* address of first failure LSB */

      /* SAT rev8 Table75, p 76 */
      switch (SelfTestExecutionStatus)
      {
      case 0:
        pLogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
        pLogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
        pLogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
        break;
      case 1:
        pLogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
        pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        pLogPage[22] = 0x81;
        break;
      case 2:
        pLogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
        pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        pLogPage[22] = 0x82;
        break;
      case 3:
        pLogPage[20] = 0 | SCSI_SNSKEY_ABORTED_COMMAND;
        pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        pLogPage[22] = 0x83;
        break;
      case 4:
        pLogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
        pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        pLogPage[22] = 0x84;
        break;
      case 5:
        pLogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
        pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        pLogPage[22] = 0x85;
        break;
      case 6:
        pLogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
        pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        pLogPage[22] = 0x86;
        break;
      case 7:
        pLogPage[20] = 0 | SCSI_SNSKEY_MEDIUM_ERROR;
        pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        pLogPage[22] = 0x87;
        break;
      case 8:
        pLogPage[20] = 0 | SCSI_SNSKEY_HARDWARE_ERROR;
        pLogPage[21] = (SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN >> 8) & 0xFF;
        pLogPage[22] = 0x88;
        break;
      case 9: /* fall through */
      case 10:/* fall through */
      case 11:/* fall through */
      case 12:/* fall through */
      case 13:/* fall through */
      case 14:
        /* unspecified */
        pLogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
        pLogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
        pLogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
        break;
      case 15:
        pLogPage[20] = 0 | SCSI_SNSKEY_NO_SENSE;
        pLogPage[21] = (SCSI_SNSCODE_NO_ADDITIONAL_INFO >> 8) & 0xFF;
        pLogPage[22] = SCSI_SNSCODE_NO_ADDITIONAL_INFO & 0xFF;
        break;
      default:
        TI_DBG1(("satLogSenseCB: Error, incorrect SelfTestExecutionStatus 0x%x\n", SelfTestExecutionStatus));

        satSetSensePayload( pSense,
                            SCSI_SNSKEY_NO_SENSE,
                            0,
                            SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );

        satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satIntIo);

        return;
      }

      pLogPage[23] = 0;    /* vendor specific */

      /* the rest of Self-test results log */
      /* 403 is from SPC-4, 7.2.10, Table 216, p 259*/
      for (i=24;i<=403;i++)
      {
        pLogPage[i] = 0;    /* vendor specific */
      }

      if (SELFTEST_RESULTS_LOG_PAGE_LENGTH < lenReceived)
      {
        TI_DBG6(("satLogSenseCB: 2nd underrun lenReceived %d len %d \n", lenReceived, SELFTEST_RESULTS_LOG_PAGE_LENGTH));

        /* underrun */
        ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                tiIOUnderRun,
                                lenReceived - SELFTEST_RESULTS_LOG_PAGE_LENGTH,
                                agNULL,
                                satOrgIOContext->interruptContext );

      }
      else
      {
        ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext);
      }
      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);

      return;
    }
    else if (hostToDevFis->h.features == 0xda)
    {
      TI_DBG5(("satLogSenseCB: SAT_SMART_RETURN_STATUS success\n"));

      /* fills in the log page from ATA output */
      /* SPC-4, 7.2.5, Table 209, 211, p 255 */
      pLogPage[0] = 0x2F;    /* page code unspecified */
      pLogPage[1] = 0;       /* reserved */
      pLogPage[2] = 0;       /* page length */
      pLogPage[3] = 0x07;    /* page length */

      /*
        SPC-4, 7.2.5, Table 211, p 255
        no vendor specific field
       */
      pLogPage[4] = 0;    /* Parameter Code */
      pLogPage[5] = 0;    /* Parameter Code unspecfied but to do: */
      pLogPage[6] = 0;    /* unspecified */
      pLogPage[7] = 0x03; /* Parameter length, unspecified */

      /* SAT rev8, 10.2.3.1 Table 72, p 73 */
      if (statDevToHostFisData.lbaMid == 0x4F || statDevToHostFisData.lbaHigh == 0xC2)
      {
        pLogPage[8] = 0;   /* Sense code */
        pLogPage[9] = 0;   /* Sense code qualifier */
      }
      else if (statDevToHostFisData.lbaMid == 0xF4 || statDevToHostFisData.lbaHigh == 0x2C)
      {
        pLogPage[8] = 0x5D;   /* Sense code */
        pLogPage[9] = 0x10;   /* Sense code qualifier */
      }

      /* Assumption: No support for SCT */
      pLogPage[10] = 0xFF; /* Most Recent Temperature Reading */

      if (INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH < lenReceived)
      {
        TI_DBG6(("satLogSenseCB: 3rd underrun lenReceived %d len %d \n", lenReceived, INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH));

        /* underrun */
        ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                tiIOUnderRun,
                                lenReceived - INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH,
                                agNULL,
                                satOrgIOContext->interruptContext );

      }
      else
      {
        ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext);
      }

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);


      return;
    }
    else
    {
      TI_DBG1(("satLogSenseCB: error unknown command success 0x%x feature 0x%x\n", hostToDevFis->h.command, hostToDevFis->h.features));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );
      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);

      return;
    }
  }
  else
  {
    TI_DBG1(("satLogSenseCB: error unknown command success 0x%x\n", hostToDevFis->h.command));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    return;
  }

  return;
}

/*****************************************************************************
*! \brief  satReadMediaSerialNumberCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Read Media Serial Number completion.
*
*  \param   agRoot:       Handles for this instance of SAS/SATA hardware
*  \param   agIORequest:  Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:   Status of completed I/O.
*  \param   agSATAParm1:  Additional info based on status.
*  \param   agIOInfoLen:  Length in bytes of overrun/underrun residual or FIS
*                         length.
*  \param   ioContext:    Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satReadMediaSerialNumberCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;

  scsiRspSense_t          *pSense;
  tiIORequest_t           *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  tiScsiInitiatorRequest_t *tiOrgScsiRequest; /* tiScsiXchg */
  bit8                      *pMediaSerialNumber;

  tiIniScsiCmnd_t          *scsiCmnd;
  bit32                    lenReceived = 0;

  TI_DBG4(("satReadMediaSerialNumberCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satReadMediaSerialNumberCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satOrgIOContext->pSense;
    tiOrgScsiRequest          = satOrgIOContext->tiScsiXchg;
    /* SCSI command response payload to OS layer */
    pMediaSerialNumber        = (bit8 *) tiOrgScsiRequest->sglVirtualAddr;
    /* ATA command response payload */
    scsiCmnd        = satOrgIOContext->pScsiCmnd;


  }
  else
  {
    TI_DBG4(("satReadMediaSerialNumberCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satReadMediaSerialNumberCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satReadMediaSerialNumberCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;
    tiOrgScsiRequest   = satOrgIOContext->tiScsiXchg;
    /* SCSI command response payload to OS layer */
    pMediaSerialNumber        = (bit8 *) tiOrgScsiRequest->sglVirtualAddr;
    /* ATA command response payload */
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satReadMediaSerialNumberCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NOT_READY,
                        0,
                        SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }

  /* process success case */
  lenReceived = (scsiCmnd->cdb[6] << (8*3)) + (scsiCmnd->cdb[7] << (8*2))
                + (scsiCmnd->cdb[8] << 8) + scsiCmnd->cdb[9];
  TI_DBG5(("satReadMediaSerialNumberCB: lenReceived in CDB %d 0x%x\n", lenReceived,lenReceived));

  if (hostToDevFis->h.command == SAT_READ_SECTORS ||
      hostToDevFis->h.command == SAT_READ_SECTORS_EXT
     )
  {
    pMediaSerialNumber[0] = 0;
    pMediaSerialNumber[1] = 0;
    pMediaSerialNumber[2] = 0;
    pMediaSerialNumber[3] = 4;
    pMediaSerialNumber[4] = 0;
    pMediaSerialNumber[5] = 0;
    pMediaSerialNumber[6] = 0;
    pMediaSerialNumber[7] = 0;

    if (ZERO_MEDIA_SERIAL_NUMBER_LENGTH < lenReceived)
    {
      TI_DBG1(("satReadMediaSerialNumberCB: 1st underrun lenReceived %d len %d \n", lenReceived, ZERO_MEDIA_SERIAL_NUMBER_LENGTH));

      /* underrun */
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == satIntIo->satOrgTiIORequest */
                                tiIOUnderRun,
                                lenReceived - ZERO_MEDIA_SERIAL_NUMBER_LENGTH,
                                agNULL,
                                satOrgIOContext->interruptContext );

    }
    else
    {
      ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext);
    }
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }
  else
  {
    TI_DBG1(("satReadMediaSerialNumberCB: error unknown command success 0x%x\n", hostToDevFis->h.command));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    return;
  }
  return;
}

/*****************************************************************************
*! \brief  satReadBufferCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Read Buffer.
*
*  \param   agRoot:       Handles for this instance of SAS/SATA hardware
*  \param   agIORequest:  Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:   Status of completed I/O.
*  \param   agSATAParm1:  Additional info based on status.
*  \param   agIOInfoLen:  Length in bytes of overrun/underrun residual or FIS
*                         length.
*  \param   ioContext:    Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satReadBufferCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;
  scsiRspSense_t          *pSense;
  tiIORequest_t           *tiOrgIORequest;
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;

  TI_DBG4(("satReadBufferCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;


  if (satIntIo == agNULL)
  {
    TI_DBG4(("satReadBufferCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satOrgIOContext->pSense;

    /* SCSI command response payload to OS layer */

    /* ATA command response payload */

  }
  else
  {
    TI_DBG4(("satReadBufferCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satReadBufferCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satReadBufferCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;

    /* SCSI command response payload to OS layer */

    /* ATA command response payload */

  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satReadBufferCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NOT_READY,
                        0,
                        SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }

  /* process success case */
  if (hostToDevFis->h.command == SAT_READ_BUFFER )
  {

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext);
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }
  else
  {
    TI_DBG1(("satReadBufferCB: error unknown command success 0x%x\n", hostToDevFis->h.command));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    return;
  }

  return;
}

/*****************************************************************************
*! \brief  satWriteBufferCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Write Buffer.
*
*  \param   agRoot:       Handles for this instance of SAS/SATA hardware
*  \param   agIORequest:  Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:   Status of completed I/O.
*  \param   agSATAParm1:  Additional info based on status.
*  \param   agIOInfoLen:  Length in bytes of overrun/underrun residual or FIS
*                         length.
*  \param   ioContext:    Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satWriteBufferCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;
  scsiRspSense_t          *pSense;
  tiIORequest_t           *tiOrgIORequest;
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;

  TI_DBG4(("satWriteBufferCB:agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;


  if (satIntIo == agNULL)
  {
    TI_DBG4(("satWriteBufferCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    pSense          = satOrgIOContext->pSense;
    /* SCSI command response payload to OS layer */

    /* ATA command response payload */

  }
  else
  {
    TI_DBG4(("satWriteBufferCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satWriteBufferCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satWriteBufferCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    pSense        = satOrgIOContext->pSense;
    /* SCSI command response payload to OS layer */

    /* ATA command response payload */

  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satWriteBufferCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }

  if( agIOStatus != OSSA_IO_SUCCESS)
  {
    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NOT_READY,
                        0,
                        SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }
  /* process success case */
  if (hostToDevFis->h.command == SAT_WRITE_BUFFER )
  {

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satOrgIOContext->interruptContext);
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    return;
  }
  else
  {
    TI_DBG1(("satWriteBufferCB: error unknown command success 0x%x\n", hostToDevFis->h.command));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    return;
  }

  return;
}

/*****************************************************************************
*! \brief  satReassignBlocksCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Reassign Blocks.
*
*  \param   agRoot:       Handles for this instance of SAS/SATA hardware
*  \param   agIORequest:  Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:   Status of completed I/O.
*  \param   agSATAParm1:  Additional info based on status.
*  \param   agIOInfoLen:  Length in bytes of overrun/underrun residual or FIS
*                         length.
*  \param   ioContext:    Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
void satReassignBlocksCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satIOContext_t          *satNewIOContext;
  satInternalIo_t         *satIntIo;
  satInternalIo_t         *satNewIntIo = agNULL;
  satDeviceData_t         *satDevData;

  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  tiIORequest_t             *tiOrgIORequest;

  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     status;
  tiScsiInitiatorRequest_t *tiScsiRequest; /* tiScsiXchg */
  agsaFisRegD2HHeader_t    *statDevToHostFisHeader = agNULL;

  TI_DBG5(("satReassignBlocksCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  /* internally generate tiIOContext */
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  hostToDevFis           = satIOContext->pFis;

  if (satIntIo == agNULL)
  {
    TI_DBG4(("satReassignBlocksCB: External satInternalIo_t satIntIoContext\n"));
    satOrgIOContext = satIOContext;
    tiOrgIORequest  = tdIORequestBody->tiIORequest;
    tiScsiRequest   = satOrgIOContext->tiScsiXchg;
    pSense          = satOrgIOContext->pSense;
    scsiCmnd        = satOrgIOContext->pScsiCmnd;
  }
  else
  {
    TI_DBG4(("satReassignBlocksCB: Internal satInternalIo_t satIntIoContext\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG4(("satReassignBlocksCB: satOrgIOContext is NULL, Wrong\n"));
      return;
    }
    else
    {
      TI_DBG4(("satReassignBlocksCB: satOrgIOContext is NOT NULL, Wrong\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;

    tiScsiRequest = satOrgIOContext->tiScsiXchg;
    pSense        = satOrgIOContext->pSense;
    scsiCmnd      = satOrgIOContext->pScsiCmnd;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satReassignBlocksCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    ostiInitiatorIOCompleted (
                             tiRoot,
                             tiOrgIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             satOrgIOContext->interruptContext
                             );
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
      TI_DBG1(("satReassignBlocksCB FAILED, NOT IO_SUCCESS\n"));
    }
    else if (statDevToHostFisHeader->fisType != REG_DEV_TO_HOST_FIS)
    {
      TI_DBG1(("satReassignBlocksCB FAILED, Wrong FIS type 0x%x\n",statDevToHostFisHeader->fisType));
    }
    else if ( (ataStatus & ERR_ATA_STATUS_MASK) ||
              (ataStatus & DF_ATA_STATUS_MASK)
              )
    {
      TI_DBG1(("satReassignBlocksCB FAILED, FAILED, error status\n"));
    }

    /* Process abort case */
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      satProcessAbort(tiRoot,
                      tiOrgIORequest,
                      satOrgIOContext
                      );

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      return;
    }

    /* for debugging */
    if (hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS ||
        hostToDevFis->h.command == SAT_READ_VERIFY_SECTORS_EXT
       )
    {
      TI_DBG1(("satReassignBlocksCB SAT_READ_VERIFY_SECTORS(_EXT) failed\n"));
      /* Verify failed; send Write with same LBA */
      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);

      satNewIntIo = satAllocIntIoResource( tiRoot,
                                           tiOrgIORequest,
                                           satDevData,
                                           512, /* writing 1 sector */
                                           satNewIntIo);
      if (satNewIntIo == agNULL)
      {
        /* memory allocation failure */
        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satNewIntIo);

        satSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );
        TI_DBG1(("satReassignBlocksCB: momory allocation fails\n"));
        return;
      } /* end memory allocation */

      satNewIOContext = satPrepareNewIO(
                                        satNewIntIo,
                                        tiOrgIORequest,
                                        satDevData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );

      /* send Write with same LBA */
      status = satReassignBlocks_2(
                                   tiRoot,
                                   &satNewIntIo->satIntTiIORequest,
                                   satNewIOContext->ptiDeviceHandle,
                                   &satNewIntIo->satIntTiScsiXchg,
                                   satNewIOContext,
                                   satOrgIOContext->LBA
                                 );

      if (status != tiSuccess)
      {
        /* sending ATA command fails */
        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satNewIntIo);
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );
        TI_DBG1(("satReassignBlocksCB calling fail 1\n"));
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
      TI_DBG1(("satReassignBlocksCB SAT_WRITE failed\n"));
      /* fall through */
    }
    else
    {
      TI_DBG1(("satReassignBlocksCB error default case unexpected command 0x%x\n", hostToDevFis->h.command));
    }


    satSetSensePayload( pSense,
                        SCSI_SNSKEY_HARDWARE_ERROR,
                        0,
                        SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                        satOrgIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satOrgIOContext->pTiSenseData,
                              satOrgIOContext->interruptContext );


    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
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
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    if (satOrgIOContext->ParmIndex >= satOrgIOContext->ParmLen)
    {
      TI_DBG5(("satReassignBlocksCB: GOOD status\n"));
      /* return stat_good */
      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satOrgIOContext->interruptContext );
      return;
    }
    else
    {
      TI_DBG5(("satReassignBlocksCB: processing next LBA\n"));
      satNewIntIo = satAllocIntIoResource( tiRoot,
                                           tiOrgIORequest,
                                           satDevData,
                                           0,
                                           satNewIntIo);
      if (satNewIntIo == agNULL)
      {
        /* memory allocation failure */
        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satNewIntIo);

        satSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );
        TI_DBG1(("satReassignBlocksCB: momory allocation fails\n"));
        return;
      } /* end memory allocation */

      satNewIOContext = satPrepareNewIO(
                                        satNewIntIo,
                                        tiOrgIORequest,
                                        satDevData,
                                        scsiCmnd,
                                        satOrgIOContext
                                        );

      /* send Verify with the next LBA */
      status = satReassignBlocks_1(
                                   tiRoot,
                                   &satNewIntIo->satIntTiIORequest,
                                   satNewIOContext->ptiDeviceHandle,
                                   tiScsiRequest, /* orginal from OS layer */
                                   satNewIOContext,
                                   satOrgIOContext
                                   );

      if (status != tiSuccess)
      {
        /* sending ATA command fails */
        satFreeIntIoResource( tiRoot,
                              satDevData,
                              satNewIntIo);
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_HARDWARE_ERROR,
                            0,
                            SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                            satOrgIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satOrgIOContext->pTiSenseData,
                                  satOrgIOContext->interruptContext );
        TI_DBG1(("satReassignBlocksCB calling satModeSelect6_1 fails\n"));
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
      TI_DBG1(("satReassignBlocksCB error unknown command success 0x%x \n", hostToDevFis->h.command));

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                            satDevData,
                            satIntIo);
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_HARDWARE_ERROR,
                          0,
                          SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                          satOrgIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiOrgIORequest, /* == &satIntIo->satOrgTiIORequest */
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satOrgIOContext->pTiSenseData,
                                satOrgIOContext->interruptContext );
      return;
  }
  return;
}
/*****************************************************************************
*! \brief  satReadLogExtCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals READ LOG EXT completion.
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*  \param   ioContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
/*
  SATAII spec p42

*/
void satReadLogExtCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     void              *agParam,
                     void              *ioContext
                     )

{

  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdIORequestBody;
  satIOContext_t          *satReadLogExtIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;
  tdsaDeviceData_t        *tdsaDeviceData;
  agsaIORequest_t         *agAbortIORequest;
  tdIORequestBody_t       *tdAbortIORequestBody;
  bit32                   PhysUpper32;
  bit32                   PhysLower32;
  bit32                   memAllocStatus;
  void                    *osMemHandle;

  TI_DBG1(("satReadLogExtCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n",
    agIORequest, agIOStatus, agIOInfoLen));

  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satReadLogExtIOContext = (satIOContext_t *) ioContext;
  satIntIo               = satReadLogExtIOContext->satIntIoContext;
  satDevData             = satReadLogExtIOContext->pSatDevData;
  tdsaDeviceData         = (tdsaDeviceData_t *)satDevData->satSaDeviceData;

  TI_DBG1(("satReadLogExtCB: did %d\n", tdsaDeviceData->id));
  satDecrementPendingIO(tiRoot, tdsaAllShared, satReadLogExtIOContext);


  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  /*
   * If READ LOG EXT failed, we issue device reset.
   */
  if ( agIOStatus != OSSA_IO_SUCCESS ||
       (agIOStatus == OSSA_IO_SUCCESS && agFirstDword != agNULL && agIOInfoLen != 0)
     )
  {
    TI_DBG1(("satReadLogExtCB: FAILED.\n"));

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    /* Abort I/O after completion of device reset */
    satDevData->satAbortAfterReset = agTRUE;
#ifdef NOT_YET
    /* needs to investigate this case */
    /* no report to OS layer */
    satSubTM(tiRoot,
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

  TI_DBG1(("satReadLogExtCB: issuing saSATAAbort. Device Abort\n"));
  /* do not deregister this device */
  tdsaDeviceData->OSAbortAll = agTRUE;

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
    TI_DBG1(("satReadLogExtCB: ostiAllocMemory failed...\n"));
    return;
  }

  if (tdAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    TI_DBG1(("satReadLogExtCB: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
    return;
  }

  /* setup task management structure */
  tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  tdAbortIORequestBody->tiDevHandle = (tiDeviceHandle_t *)&(tdsaDeviceData->tiDeviceHandle);
  /* initialize agIORequest */
  agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) tdAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

  /*
   * Issue abort
   */
  saSATAAbort( agRoot, agAbortIORequest, 0, tdsaDeviceData->agDevHandle, 1, agNULL, agNULL);


  /*
   * Free resource allocated for the internally generated READ LOG EXT.
   */
  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

  /*
   * Sequence of recovery continue at some other context:
   * At the completion of abort, in the context of ossaSATACompleted(),
   * return the I/O with error status to the OS-App Specific layer.
   * When all I/O aborts are completed, clear SATA device flag to
   * indicate ready to process new request.
   */

   satDevData->satDriveState = SAT_DEV_STATE_NORMAL;

   TI_DBG1(("satReadLogExtCB: end return\n"));
   return;
}

#ifndef FDS_SM
/*****************************************************************************
*! \brief  ossaSATAEvent
*
*   This routine is called to notify the OS Layer of an event associated with
*   SATA port or SATA device
*
*  \param   agRoot:        Handles for this instance of SAS/SATA hardware
*  \param   agIORequest:   Pointer to the LL I/O request context for this I/O.
*  \param   agPortContext  Pointer to the port context of TD and Lower layer
*  \param   agDevHandle:   Pointer to a device handle
*  \param   event:         event type
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void ossaSATAEvent(
                        agsaRoot_t              *agRoot,
                        agsaIORequest_t         *agIORequest,
                        agsaPortContext_t       *agPortContext,
                        agsaDevHandle_t         *agDevHandle,
                        bit32                   event,
                        bit32                   agIOInfoLen,
                        void                    *agParam
                           )
{

  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32                   interruptContext = osData->IntContext;
  tdsaDeviceData_t        *pDeviceData;
  satDeviceData_t         *pSatDevData;
  satInternalIo_t         *satIntIo = agNULL;
  bit32                   status;
  satIOContext_t          *satIOContext2;
  tdIORequestBody_t       *tdIORequestBody;
  tiDeviceHandle_t        *tiDeviceHandle;
  tiIORequest_t           tiIORequestTMP;
  agsaDifDetails_t        agDifDetails;
  bit8                    framePayload[256];
  bit16                   frameOffset = 0;
  bit16                   frameLen = 0;

  /* new */
  tdsaDeviceData_t        *tdsaDeviceData = agNULL;
  satIOContext_t          *satIOContext;
  tdsaPortContext_t       *onePortContext;

  if (event == OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE)
  {

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
     *    satReadLogExtCB(). Steps 5,6,7,8 below are the step 1,2,3,4 in
     *    satReadLogExtCB().
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

    pDeviceData = (tdsaDeviceData_t *) agDevHandle->osData;
    pSatDevData = &pDeviceData->satDevData;
    tiDeviceHandle  = &((tdsaDeviceData_t *)(pSatDevData->satSaDeviceData))->tiDeviceHandle;

    TI_DBG1(("ossaSATAEvent: did %d\n", pDeviceData->id));

    if (pSatDevData->satDriveState == SAT_DEV_STATE_NORMAL)
    {
      TI_DBG1(("ossaSATAEvent: NCQ ERROR agDevHandle=%p.\n", agDevHandle ));

      /* Set flag to indicate we are in recovery */
      pSatDevData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;

      /*
       * Allocate resource for READ LOG EXIT page 10h
       */
      satIntIo = satAllocIntIoResource( tiRoot,
                                        &(tiIORequestTMP), /* anything but NULL */
                                        pSatDevData,
                                        sizeof (satReadLogExtPage10h_t),
                                        satIntIo);

      /*
       * If we cannot allocate resource to do the normal NCQ recovery, we
       * will do SATA device reset.
       */
      if (satIntIo == agNULL)
      {
        /* Abort I/O after completion of device reset */
        pSatDevData->satAbortAfterReset = agTRUE;
        TI_DBG1(("ossaSATAEvent: can't send RLE due to resource lack\n"));

#ifdef NOT_YET
        /* needs to investigate this case */
        /* no report to OS layer */
        satSubTM(tiRoot,
                 tiDeviceHandle,
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

      tdIORequestBody = (tdIORequestBody_t *)satIntIo->satIntRequestBody;
      satIOContext2 = &(tdIORequestBody->transport.SATA.satIOContext);

      satIOContext2->pSatDevData   = pSatDevData;
      satIOContext2->pFis          = &(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
      satIOContext2->pScsiCmnd     = &(satIntIo->satIntTiScsiXchg.scsiCmnd);
      satIOContext2->pSense        = &(tdIORequestBody->transport.SATA.sensePayload);
      satIOContext2->pTiSenseData  = &(tdIORequestBody->transport.SATA.tiSenseData);
      satIOContext2->pTiSenseData->senseData = satIOContext2->pSense;

      satIOContext2->tiRequestBody = satIntIo->satIntRequestBody;
      satIOContext2->interruptContext = interruptContext;
      satIOContext2->satIntIoContext  = satIntIo;

      satIOContext2->ptiDeviceHandle = tiDeviceHandle;
      satIOContext2->satOrgIOContext = agNULL;
      satIOContext2->tiScsiXchg = agNULL;

      status = satSendReadLogExt( tiRoot,
                                  &satIntIo->satIntTiIORequest,
                                  tiDeviceHandle,
                                  &satIntIo->satIntTiScsiXchg,
                                  satIOContext2);

      if (status !=tiSuccess)
      {
        TI_DBG1(("ossaSATAEvent: can't send RLE due to LL api failure\n"));
        satFreeIntIoResource( tiRoot,
                              pSatDevData,
                              satIntIo);
        /* Abort I/O after completion of device reset */
        pSatDevData->satAbortAfterReset = agTRUE;
#ifdef NOT_YET
        /* needs to investigate this case */
        /* no report to OS layer */
        satSubTM(tiRoot,
                 tiDeviceHandle,
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
      TI_DBG1(("ossaSATAEvent: NCQ ERROR but recovery in progress\n"));
    }

  }
  else if (event == OSSA_IO_XFER_CMD_FRAME_ISSUED)
  {
    TI_DBG1(("ossaSATAEvent: OSSA_IO_XFER_CMD_FRAME_ISSUED\n"));
  }
  else if (event == OSSA_IO_XFER_PIO_SETUP_ERROR)
  {
    TI_DBG1(("ossaSATAEvent: OSSA_IO_XFER_PIO_SETUP_ERROR\n"));

  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED)
  {
    TI_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO)
  {
    TI_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST)
  {
    TI_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE)
  {
    TI_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED)
  {
    TI_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED\n"));
  }
  else if (event == OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH)
  {
    TI_DBG1(("ossaSATAEvent: OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH\n"));
  }
  else if (event == OSSA_IO_XFR_ERROR_DIF_MISMATCH || event == OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH  ||
           event == OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH || event == OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH)
  {
    TI_DBG1(("ossaSSPEvent: DIF related, event 0x%x\n", event));
    /* process DIF detail information */
    TI_DBG2(("ossaSSPEvent: agIOInfoLen %d\n", agIOInfoLen));
    if (agParam == agNULL)
    {
      TI_DBG2(("ossaSSPEvent: agParam is NULL!!!\n"));
      return;
    }
    if (agIOInfoLen < sizeof(agsaDifDetails_t))
    {
      TI_DBG2(("ossaSSPEvent: wrong agIOInfoLen!!! agIOInfoLen %d sizeof(agsaDifDetails_t) %d\n", agIOInfoLen, sizeof(agsaDifDetails_t)));
      return;
    }
    /* reads agsaDifDetails_t */
    saFrameReadBlock(agRoot, agParam, 0, &agDifDetails, sizeof(agsaDifDetails_t));
    frameOffset = (agDifDetails.ErrBoffsetEDataLen & 0xFFFF);
    frameLen = (agDifDetails.ErrBoffsetEDataLen & 0xFFFF0000) >> 16;

    TI_DBG2(("ossaSSPEvent: UpperLBA 0x%08x LowerLBA 0x%08x\n", agDifDetails.UpperLBA, agDifDetails.LowerLBA));
    TI_DBG2(("ossaSSPEvent: SASAddrHI 0x%08x SASAddrLO 0x%08x\n",
             TD_GET_SAS_ADDRESSHI(agDifDetails.sasAddressHi), TD_GET_SAS_ADDRESSLO(agDifDetails.sasAddressLo)));
    TI_DBG2(("ossaSSPEvent: DIF error mask 0x%x Device ID 0x%x\n",
             (agDifDetails.DIFErrDevID) & 0xFF, (agDifDetails.DIFErrDevID & 0xFFFF0000) >> 16));
    if (frameLen != 0 && frameLen <= 256)
    {
      saFrameReadBlock(agRoot, agParam, sizeof(agsaDifDetails_t), framePayload, frameLen);
      tdhexdump("ossaSSPEvent frame", framePayload, frameLen);
    }
  }
  else
  {
    TI_DBG1(("ossaSATAEvent: ERROR event %d agDevHandle=%p.\n", event, agDevHandle ));

    tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
    satIOContext    = &(tdIORequestBody->transport.SATA.satIOContext);
    pSatDevData     = satIOContext->pSatDevData;
    tdsaDeviceData  = (tdsaDeviceData_t *)pSatDevData->satSaDeviceData;
    onePortContext   = tdsaDeviceData->tdPortContext;
    TI_DBG1(("ossaSATAEvent: did %d\n", tdsaDeviceData->id));

    /* send SMP_PHY_CONTROL_HARD_RESET */
    if (event == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY && tdsaAllShared->FCA)
    {
      if (pSatDevData->NumOfFCA <= 0) /* does SMP HARD RESET only upto one time */
      {
        TI_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; sending HARD_RESET\n"));
        pSatDevData->NumOfFCA++;
        tdsaPhyControlSend(tiRoot,
                           tdsaDeviceData,
                           SMP_PHY_CONTROL_HARD_RESET,
                           agNULL);
      }
      else
      {
        /* given up after one time of SMP HARD RESET; */
        TI_DBG1(("ossaSATAEvent: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; NO!!! sending HARD_RESET\n"));
        if (tdsaDeviceData->registered == agTRUE && tdsaAllShared->ResetInDiscovery == 0)
        {
          /*
            1. remove this device
            2. device removal event
          */
          tdsaAbortAll(tiRoot, agRoot, tdsaDeviceData);
          tdsaDeviceData->valid = agFALSE;
          tdsaDeviceData->valid2 = agFALSE;
          tdsaDeviceData->registered = agFALSE;
          ostiInitiatorEvent(
                             tiRoot,
                             onePortContext->tiPortalContext,
                             agNULL,
                             tiIntrEventTypeDeviceChange,
                             tiDeviceRemoval,
                             agNULL
                             );
        }
      }
    }

  }
}
#endif /* FDS_SM */

/*****************************************************************************
*! \brief  itdsatErrorSATAEventHandle
*
*   This routine is called to handle SATA error event
*
*  \param   agRoot:        Handles for this instance of SAS/SATA hardware
*  \param   agIORequest:   Pointer to the LL I/O request context for this I/O.
*  \param   agPortContext  Pointer to the port context of TD and Lower layer
*  \param   agDevHandle:   Pointer to a device handle
*  \param   event:         event type
*  \param   ioContext:     Pointer to satIOContext_t
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void  itdsatErrorSATAEventHandle(
                                          agsaRoot_t        *agRoot,
                                          agsaIORequest_t   *agIORequest,
                                          agsaPortContext_t *agPortContext,
                                          agsaDevHandle_t   *agDevHandle,
                                          bit32             event,
                                          satIOContext_t    *ioContext
                                          )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t       *tdOrgIORequestBody;
  satIOContext_t          *satIOContext;
  satIOContext_t          *satOrgIOContext;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;
  bit32                   interruptContext = osData->IntContext;

  TI_DBG1(("itdsatErrorSATAEventHandle: start\n"));
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;


  TI_DBG1(("itdsatErrorSATAEventHandle: event 0x%x\n", event));

  if (satIntIo == agNULL)
  {
    TI_DBG1(("itdsatErrorSATAEventHandle: External, OS generated\n"));
    satOrgIOContext      = satIOContext;
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    if (event == OSSA_IO_OVERFLOW)
    {
      TI_DBG1(("itdsatErrorSATAEventHandle: tiIOOverRun\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tdOrgIORequestBody->tiIORequest,
                                tiIOOverRun,
                                0,
                                agNULL,
                                interruptContext);
    }
    else
    {
      TI_DBG1(("itdsatErrorSATAEventHandle: else\n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tdOrgIORequestBody->tiIORequest,
                                tiIOFailed,
                                tiDetailOtherError,
                                agNULL,
                                interruptContext);
    }
  }
  else
  {
    TI_DBG1(("itdsatErrorSATAEventHandle: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG1(("itdsatErrorSATAEventHandle: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG6(("itdsatErrorSATAEventHandle: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    /* clean up TD layer's IORequestBody */
    ostiFreeMemory(
                   tiRoot,
                   tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
           );

  }
  return;
}

osGLOBAL void ossaSATAAbortCB(
                              agsaRoot_t        *agRoot,
                              agsaIORequest_t   *agIORequest,
                              bit32             flag,
                              bit32             status)
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdIORequestBody_t       *tdAbortIORequestBody = agNULL;
  tdsaDeviceData_t        *oneDeviceData        = agNULL;
  tiDeviceHandle_t        *tiDeviceHandle       = agNULL;
  tiIORequest_t           *taskTag              = agNULL;

  TI_DBG1(("ossaSATAAbortCB: start\n"));

  tdAbortIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  if (tdAbortIORequestBody == agNULL)
  {
    TI_DBG1(("ossaSATAAbortCB: tdAbortIORequestBody is NULL warning!!!!\n"));
    return;
  }

  if (flag == 2)
  {
    /* abort per port */
    TI_DBG1(("ossaSATAAbortCB: abort per port\n"));
  }
  else if (flag == 1)
  {
    TI_DBG1(("ossaSATAAbortCB: abort all\n"));
    tiDeviceHandle = (tiDeviceHandle_t *)tdAbortIORequestBody->tiDevHandle;
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("ossaSATAAbortCB: tiDeviceHandle is NULL warning!!!!\n"));
      ostiFreeMemory(
               tiRoot,
               tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
               sizeof(tdIORequestBody_t)
               );
      return;
    }

    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("ossaSATAAbortCB: oneDeviceData is NULL warning!!!!\n"));
      ostiFreeMemory(
               tiRoot,
               tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
               sizeof(tdIORequestBody_t)
               );
      return;
    }

    if (status == OSSA_IO_SUCCESS)
    {
      TI_DBG1(("ossaSATAAbortCB: OSSA_IO_SUCCESS\n"));
      /* clean up TD layer's IORequestBody */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortOK,
                            agNULL);
      }
      else
      {
        TI_DBG1(("ossaSATAAbortCB: calling saDeregisterDeviceHandle did %d\n", oneDeviceData->id));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));	
      }
      /* callback to OS layer here ??? */
      TI_DBG1(("ossaSATAAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                   tiRoot,
                   tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );

    }
    else if (status == OSSA_IO_NOT_VALID)
    {
      TI_DBG1(("ossaSATAAbortCB: OSSA_IO_NOT_VALID\n"));
      /*
        Nothing is reproted to OS layer
      */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortFailed,
                            agNULL );
      }
      else
      {
        TI_DBG1(("ossaSATAAbortCB: calling saDeregisterDeviceHandle did %d\n", oneDeviceData->id));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      TI_DBG1(("ossaSATAAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_NO_DEVICE)
    {
      TI_DBG1(("ossaSATAAbortCB: OSSA_IO_NO_DEVICE\n"));
      /*
        Nothing is reproted to OS layer
      */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortInProgress,
                            agNULL );
      }
      else
      {
        TI_DBG1(("ossaSATAAbortCB: calling saDeregisterDeviceHandle did %d\n", oneDeviceData->id));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      TI_DBG1(("ossaSATAAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_ABORT_IN_PROGRESS)
    {
      TI_DBG1(("ossaSATAAbortCB: OSSA_IO_ABORT_IN_PROGRESS\n"));
      /*
        Nothing is reproted to OS layer
      */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortInProgress,
                            agNULL );
      }
      else
      {
        TI_DBG1(("ossaSATAAbortCB: calling saDeregisterDeviceHandle did %d\n", oneDeviceData->id));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      TI_DBG1(("ossaSATAAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else
    {
      TI_DBG1(("ossaSATAAbortCB: unspecified status 0x%x\n", status ));
      /*
        Nothing is reproted to OS layer
      */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortInProgress,
                            agNULL );
      }
      else
      {
        TI_DBG1(("ossaSATAAbortCB: calling saDeregisterDeviceHandle did %d\n", oneDeviceData->id));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));      
      }
      TI_DBG1(("ossaSATAAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
  }
  else if (flag == 0)
  {
    TI_DBG1(("ossaSATAAbortCB: abort one\n"));
    taskTag = tdAbortIORequestBody->tiIOToBeAbortedRequest;

    if (status == OSSA_IO_SUCCESS)
    {
      TI_DBG1(("ossaSATAAbortCB: OSSA_IO_SUCCESS\n"));

      ostiInitiatorEvent( tiRoot,
                          agNULL,
                          agNULL,
                          tiIntrEventTypeLocalAbort,
                          tiAbortOK,
                          taskTag );
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );

    }
    else if (status == OSSA_IO_NOT_VALID)
    {
      TI_DBG1(("ossaSATAAbortCB: OSSA_IO_NOT_VALID\n"));

      ostiInitiatorEvent( tiRoot,
                          agNULL,
                          agNULL,
                          tiIntrEventTypeLocalAbort,
                          tiAbortFailed,
                          taskTag );

      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_NO_DEVICE)
    {
      TI_DBG1(("ossaSATAAbortCB: OSSA_IO_NO_DEVICE\n"));

      ostiInitiatorEvent( tiRoot,
                          agNULL,
                          agNULL,
                          tiIntrEventTypeLocalAbort,
                          tiAbortInProgress,
                          taskTag );

      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_ABORT_IN_PROGRESS)
    {
      TI_DBG1(("ossaSATAAbortCB: OSSA_IO_ABORT_IN_PROGRESS\n"));

      ostiInitiatorEvent( tiRoot,
                          agNULL,
                          agNULL,
                          tiIntrEventTypeLocalAbort,
                          tiAbortInProgress,
                          taskTag );

      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else
    {
      TI_DBG1(("ossaSATAAbortCB: unspecified status 0x%x\n", status ));

      ostiInitiatorEvent( tiRoot,
                          agNULL,
                          agNULL,
                          tiIntrEventTypeLocalAbort,
                          tiAbortFailed,
                          taskTag );

      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
  }
  else
  {
    TI_DBG1(("ossaSATAAbortCB: wrong flag %d\n", flag));
  }
  return;
}

/*****************************************************************************
*! \brief  ossaSATADeviceResetCB
*
*   This routine is called to complete a SATA device reset request previously
*   issued to the LL Layer in saSATADeviceReset().
*
*  \param agRoot:      Handles for this instance of SAS/SATA hardware
*  \param agDevHandle: Pointer to a device handle
*  \param resetStatus: Reset status:
*                      OSSA_SUCCESS: The reset operation completed successfully.
*                      OSSA_FAILURE: The reset operation failed.
*  \param resetparm:  Pointer to the Device-To-Host FIS received from the device.
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void
ossaSATADeviceResetCB(
                      agsaRoot_t        *agRoot,
                      agsaDevHandle_t   *agDevHandle,
                      bit32             resetStatus,
                      void              *resetparm)
{
  bit32               tiResetStatus;
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaDeviceData_t    *pDeviceData;
  tiDeviceHandle_t    *tiDeviceHandle;

  TI_DBG1(("ossaSATADeviceResetCB: agDevHandle=%p resetStatus=0x%x\n",
      agDevHandle, resetStatus ));

  pDeviceData = (tdsaDeviceData_t *) agDevHandle->osData;
  tiDeviceHandle = &(pDeviceData->tiDeviceHandle);

  if (resetStatus == OSSA_SUCCESS )
    tiResetStatus = tiSuccess;
  else
    tiResetStatus = tiError;

  osSatResetCB( tiRoot,
                tiDeviceHandle,
                tiResetStatus,
                resetparm);

}


/*****************************************************************************/
/*! \brief satDecrementPendingIO
 *
 *  This function decrements the number of pending IO's
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tdsaAllShared:    Pointer to TD context.
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return
 *          None
 */
/*****************************************************************************/
GLOBAL void
satDecrementPendingIO(
                      tiRoot_t                *tiRoot,
                      tdsaContext_t           *tdsaAllShared,
                      satIOContext_t          *satIOContext
                      )
{
  satDeviceData_t         *satDevData;

  TI_DBG4(("satDecrementPendingIO: start\n"));

  satDevData             = satIOContext->pSatDevData;

  if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
       (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
  {
    tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
    satDevData->satPendingNCQIO--;
    satIOContext->pSatDevData->satPendingIO--;
    TDLIST_DEQUEUE_THIS (&satIOContext->satIoContextLink);
    tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);
  }
  else
  {
    tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
    satDevData->satPendingNONNCQIO--;
    satIOContext->pSatDevData->satPendingIO--;
    TDLIST_DEQUEUE_THIS (&satIOContext->satIoContextLink);
    tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);
  }

  return;
}

GLOBAL void
satTranslateATAPIErrorsToSCSIErrors(
    bit8   bCommand,
    bit8   bATAStatus,
    bit8   bATAError,
    bit8   *pSenseKey,
    bit16  *pSenseCodeInfo
    )
{
    if (pSenseKey == agNULL || pSenseCodeInfo == agNULL)
    {
        TI_DBG0(("TranslateATAErrorsToSCSIErros: pSenseKey == agNULL || pSenseCodeInfo == agNULL\n"));
        return;
    }

    if((bATAStatus & ERR_ATA_STATUS_MASK) && (bATAError & NM_ATA_ERROR_MASK))
    {
      *pSenseKey = SCSI_SNSKEY_NOT_READY;
      *pSenseCodeInfo = 0x3a00;
    }
    else if((bATAStatus & ERR_ATA_STATUS_MASK) && (bATAError & ABRT_ATA_ERROR_MASK))
    {
      *pSenseKey = SCSI_SNSKEY_ABORTED_COMMAND;
      *pSenseCodeInfo = 0;
    }
    else if((bATAStatus & ERR_ATA_STATUS_MASK) && (bATAError & MCR_ATA_ERROR_MASK))
    {
      *pSenseKey = SCSI_SNSKEY_UNIT_ATTENTION;
      *pSenseCodeInfo = 0x5a01;
    }
    else if((bATAStatus & ERR_ATA_STATUS_MASK) && (bATAError & IDNF_ATA_ERROR_MASK))
    {
      *pSenseKey = SCSI_SNSKEY_MEDIUM_ERROR;
      *pSenseCodeInfo = 0x1401;
    }
    else if((bATAStatus & ERR_ATA_STATUS_MASK) && (bATAError & MC_ATA_ERROR_MASK))
    {
      *pSenseKey = SCSI_SNSKEY_UNIT_ATTENTION;
      *pSenseCodeInfo = 0x2800;
    }
    else if((bATAStatus & ERR_ATA_STATUS_MASK) && (bATAError & UNC_ATA_ERROR_MASK))
    {
      /*READ*/
      *pSenseKey = SCSI_SNSKEY_MEDIUM_ERROR;
      *pSenseCodeInfo = 0x1100;

      /*add WRITE here */
    }
    else if((bATAStatus & ERR_ATA_STATUS_MASK) && (bATAError & ICRC_ATA_ERROR_MASK))
    {
      *pSenseKey = SCSI_SNSKEY_ABORTED_COMMAND;
      *pSenseCodeInfo = 0x4703;
    }
    else if((bATAStatus & DF_ATA_STATUS_MASK))
    {
      *pSenseKey = SCSI_SNSKEY_HARDWARE_ERROR;
      *pSenseCodeInfo = 0x4400;
    }
    else
    {
      TI_DBG0(("unhandled ata error: bATAStatus = 0x%x, bATAError = 0x%x\n",
                 bATAStatus, bATAError));
    }

}

#endif /* #ifdef SATA_ENABLE */

