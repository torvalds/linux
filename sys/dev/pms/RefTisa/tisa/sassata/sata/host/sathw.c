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
 * The file implementing LL HW encapsulation for SCSI/ATA Translation (SAT).
 *
 */
/*****************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#ifdef SATA_ENABLE

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

/*
 * This table is used to map LL Layer saSATAStart() status to TISA status.
 */
static bit32 mapStat[3] =
{
  tiSuccess,
  tiError,
  tiBusy
};


/*****************************************************************************
 *! \brief  sataLLIOStart
 *
 *   This routine is called to initiate a new SATA request to LL layer.
 *   This function implements/encapsulates HW and LL API dependency.
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return:
 *
 *  \e tiSuccess:     I/O request successfully initiated.
 *  \e tiBusy:        No resources available, try again later.
 *  \e tiIONoDevice:  Invalid device handle.
 *  \e tiError:       Other errors that prevent the I/O request to be started.
 *
 *
 *****************************************************************************/

GLOBAL bit32  sataLLIOStart (
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t  *tiScsiRequest,
                   satIOContext_t            *satIOContext
                  )
{

  tdsaDeviceData_t            *oneDeviceData;
  agsaRoot_t                  *agRoot;
  agsaIORequest_t             *agIORequest;
  agsaDevHandle_t             *agDevHandle;
  bit32                       status;
  tdIORequestBody_t           *tdIORequestBody;
  agsaSATAInitiatorRequest_t  *agSATAReq;
  satDeviceData_t             *pSatDevData;
  satInternalIo_t             *satIntIo;
  bit32                       RLERecovery = agFALSE;

  oneDeviceData   = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  agRoot          = oneDeviceData->agRoot;
  agDevHandle     = oneDeviceData->agDevHandle;
  tdIORequestBody = (tdIORequestBody_t *)satIOContext->tiRequestBody;
  agSATAReq       = &(tdIORequestBody->transport.SATA.agSATARequestBody);
  pSatDevData     = satIOContext->pSatDevData;
  satIntIo        = satIOContext->satIntIoContext;

  /*
   * If this is a super I/O request, check for optional settings.
   * Be careful. Use the superRequest pointer for all references
   * in this block of code.
   */
  agSATAReq->option = 0;
  if (satIOContext->superIOFlag)
  {
      tiSuperScsiInitiatorRequest_t *superRequest = (tiSuperScsiInitiatorRequest_t *) tiScsiRequest;
      agBOOLEAN                 needPlusDataLenAdjustment = agFALSE;
      agBOOLEAN                 needMinusDataLenAdjustment = agFALSE;
      bit32                     adjusted_length;

      if (superRequest->flags & TI_SCSI_INITIATOR_ENCRYPT)
      {
        /*
         * Copy all of the relevant encrypt information
         */
        agSATAReq->option |= AGSA_SATA_ENABLE_ENCRYPTION;
        osti_memcpy(&agSATAReq->encrypt, &superRequest->Encrypt, sizeof(agsaEncrypt_t));
      }

      if (superRequest->flags & TI_SCSI_INITIATOR_DIF)
      {
          /*
           * Copy all of the relevant DIF information
           */
          agSATAReq->option |= AGSA_SATA_ENABLE_DIF;
          osti_memcpy(&agSATAReq->dif, &superRequest->Dif, sizeof(agsaDif_t));

          /*
           * Set SGL data len
           * XXX This code needs to support more sector sizes
           */
          if (needPlusDataLenAdjustment == agTRUE)
          {
              adjusted_length = superRequest->scsiCmnd.expDataLength;
              adjusted_length += (adjusted_length/512) * 8;
              agSATAReq->dataLength = adjusted_length;
          }
          else if (needMinusDataLenAdjustment == agTRUE)
          {
              adjusted_length = superRequest->scsiCmnd.expDataLength;
              adjusted_length -= (adjusted_length/520) * 8;
              agSATAReq->dataLength = adjusted_length;
          }
          else
          {
              /* setting the data length */
              agSATAReq->dataLength  = superRequest->scsiCmnd.expDataLength;
          }

          tdIORequestBody->IOType.InitiatorRegIO.expDataLength = agSATAReq->dataLength;
      }
      else
      {
           /* initialize expDataLength */
          if (satIOContext->reqType == AGSA_SATA_PROTOCOL_NON_DATA ||
              satIOContext->reqType == AGSA_SATA_PROTOCOL_SRST_ASSERT ||
              satIOContext->reqType == AGSA_SATA_PROTOCOL_SRST_DEASSERT
             )
          {
              tdIORequestBody->IOType.InitiatorRegIO.expDataLength = 0;
          }
          else
          {
              tdIORequestBody->IOType.InitiatorRegIO.expDataLength = tiScsiRequest->scsiCmnd.expDataLength;
          }

          agSATAReq->dataLength = tdIORequestBody->IOType.InitiatorRegIO.expDataLength;
      }
  }
  else
  {
      agSATAReq->option = 0;
      /* initialize expDataLength */
      if (satIOContext->reqType == AGSA_SATA_PROTOCOL_NON_DATA ||
          satIOContext->reqType == AGSA_SATA_PROTOCOL_SRST_ASSERT ||
          satIOContext->reqType == AGSA_SATA_PROTOCOL_SRST_DEASSERT
         )
      {
          tdIORequestBody->IOType.InitiatorRegIO.expDataLength = 0;
      }
      else
      {
          tdIORequestBody->IOType.InitiatorRegIO.expDataLength = tiScsiRequest->scsiCmnd.expDataLength;
      }

      agSATAReq->dataLength = tdIORequestBody->IOType.InitiatorRegIO.expDataLength;
  }

  if ( (pSatDevData->satDriveState == SAT_DEV_STATE_IN_RECOVERY) &&
       (satIOContext->pFis->h.command == SAT_READ_LOG_EXT)
       )
   {
     RLERecovery = agTRUE;
   }

  /* check max io */
  /* be sure to free */
  if ( (pSatDevData->satDriveState != SAT_DEV_STATE_IN_RECOVERY) ||
       (RLERecovery == agTRUE)
      )
  {
    if (RLERecovery == agFALSE) /* RLE is not checked against pending IO's */
    {
      if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
           (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
      {
        if (pSatDevData->satPendingNCQIO >= pSatDevData->satNCQMaxIO ||
            pSatDevData->satPendingNONNCQIO != 0)
        {
          TI_DBG1(("sataLLIOStart: 1st busy NCQ. NCQ Pending %d NONNCQ Pending %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
          /* free resource */
          satFreeIntIoResource( tiRoot,
                                pSatDevData,
                                satIntIo);
          return tiBusy;
        }
      }
      else
      {
        if (pSatDevData->satPendingNONNCQIO >= SAT_NONNCQ_MAX ||
            pSatDevData->satPendingNCQIO != 0)
        {
          TI_DBG1(("sataLLIOStart: 2nd busy NON-NCQ. NCQ Pending %d NON-NCQ Pending %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
          /* free resource */
          satFreeIntIoResource( tiRoot,
                                pSatDevData,
                                satIntIo);
          return tiBusy;
        }
      }
    } /* RLE */
    /* for internal SATA command only */
    if (satIOContext->satOrgIOContext != agNULL)
    {
      /* Initialize tiIORequest */
      tdIORequestBody->tiIORequest = tiIORequest;
    }
    /* Initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;

    /* Initializes Scatter Gather and ESGL */
    status = itdsataIOPrepareSGL( tiRoot,
                                  tdIORequestBody,
                                  &tiScsiRequest->agSgl1,
                                  tiScsiRequest->sglVirtualAddr );

    if (status != tiSuccess)
    {
      TI_DBG1(("sataLLIOStart: can't get SGL\n"));
      return status;
    }


    /* Initialize LL Layer agIORequest */
    agIORequest = &(tdIORequestBody->agIORequest);
    agIORequest->osData = (void *) tdIORequestBody;
    agIORequest->sdkData = agNULL; /* SA takes care of this */

    tdIORequestBody->ioStarted = agTRUE;
    tdIORequestBody->ioCompleted = agFALSE;

    /*

  #ifdef PRE_SALL_v033
GLOBAL bit32 saSATAStart(
                        agsaRoot_t      *agRoot,
                        agsaIORequest_t *agIORequest,
                        agsaDevHandle_t *agDevHandle,
                        bit32           agRequestType,
                        agsaSATAInitiatorRequest_t  *agSATAReq,
                        bit8            *agTag
                        );
#endif
GLOBAL bit32 saSATAStart(
                        agsaRoot_t                  *agRoot,
                        agsaIORequest_t             *agIORequest,
                        agsaDevHandle_t             *agDevHandle,
                        bit32                       agRequestType,
                        agsaSATAInitiatorRequest_t  *agSATAReq,
                        bit8                        agTag,
                        ossaSATACompletedCB_t       agCB
                        );
  */

    /* assign tag value for SATA */
    if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
         (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
    {
      if (agFALSE == satTagAlloc(tiRoot, pSatDevData, &satIOContext->sataTag))
      {
        TI_DBG1(("sataLLIOStart: No more NCQ tag\n"));
        tdIORequestBody->ioStarted = agFALSE;
        tdIORequestBody->ioCompleted = agTRUE;
        return tiBusy;
      }
      TI_DBG3(("sataLLIOStart: ncq tag 0x%x\n",satIOContext->sataTag));
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
      tdIORequestBody->tiIORequest = tiIORequest;
    }
    /* Initialize tiDevhandle */
    tdIORequestBody->tiDevHandle = tiDeviceHandle;


    tdIORequestBody->IOType.InitiatorRegIO.expDataLength = 0;
    /* SGL for SATA request */
    agSgl = &(tdIORequestBody->transport.SATA.agSATARequestBody.agSgl);
    agSgl->len = 0;

    agSgl->sgUpper = 0;
    agSgl->sgLower = 0;
    agSgl->len = 0;
    CLEAR_ESGL_EXTEND(agSgl->extReserved);

    /* Initialize LL Layer agIORequest */
    agIORequest = &(tdIORequestBody->agIORequest);
    agIORequest->osData = (void *) tdIORequestBody;
    agIORequest->sdkData = agNULL; /* SA takes care of this */

    tdIORequestBody->ioStarted = agTRUE;
    tdIORequestBody->ioCompleted = agFALSE;

    /* setting the data length */
    agSATAReq->dataLength = 0;

  }

  tdIORequestBody->reTries = 0;
  osti_memset(agSATAReq->scsiCDB, 0, 16);
  osti_memcpy(agSATAReq->scsiCDB, tiScsiRequest->scsiCmnd.cdb, 16);
#ifdef TD_INTERNAL_DEBUG
  tdhexdump("sataLLIOStart", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
  tdhexdump("sataLLIOStart LL", (bit8 *)&agSATAReq->fis.fisRegHostToDev,
            sizeof(agsaFisRegHostToDevice_t));
#endif

  TI_DBG6(("sataLLIOStart: agDevHandle %p\n", agDevHandle));
  status = saSATAStart( agRoot,
                        agIORequest,
                        tdsaRotateQnumber(tiRoot, oneDeviceData),
                        agDevHandle,
                        satIOContext->reqType,
                        agSATAReq,
                        satIOContext->sataTag,
                        ossaSATACompleted
                        );

  if (status == AGSA_RC_SUCCESS)
  {
    tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
    oneDeviceData->satDevData.satPendingIO++;
    if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
         (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
    {
      oneDeviceData->satDevData.satPendingNCQIO++;
    }
    else
    {
      oneDeviceData->satDevData.satPendingNONNCQIO++;
    }

    TDLIST_INIT_ELEMENT (&satIOContext->satIoContextLink);
    TDLIST_ENQUEUE_AT_TAIL (&satIOContext->satIoContextLink,
                            &oneDeviceData->satDevData.satIoLinkList);
    tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);
    //    TI_DBG5(("sataLLIOStart: device %p pending IO %d\n", oneDeviceData->satDevData,oneDeviceData->satDevData.satPendingIO));
  }
  else
  {
    if (status == AGSA_RC_BUSY)
    {
      TI_DBG1(("sataLLIOStart: saSATAStart busy\n"));
    }
    else
    {
      TI_DBG1(("sataLLIOStart: saSATAStart failed\n"));
    }
    if ( (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
         (satIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ) )
    {
      satTagRelease(tiRoot, pSatDevData, satIOContext->sataTag);
    }

    /* Free the ESGL pages associated with this I/O */
    tdIORequestBody->ioStarted = agFALSE;
    tdIORequestBody->ioCompleted = agTRUE;
    /*
     * Map the SAS/SATA LL layer status to the TISA status
     */
    status = mapStat[status];
    return (status);
  }

  return (tiSuccess);

}


/*****************************************************************************
*! \brief itdsataIOPrepareSGL
*
*  This function is called to prepare and translate the TISA SGL information
*  to the SAS/SATA LL layer specific SGL. This function is similar to
*  itdssIOPrepareSGL(), except the request body reflects SATA host request.
*
*  \param    tiRoot:         Pointer to initiator driver/port instance.
*  \param    IORequestBody:  TD layer request body for the I/O.
*  \param    tiSgl1:         First TISA SGL info.
*  \param    tiSgl2:         Second TISA SGL info.
*  \param    sglVirtualAddr: The virtual address of the first element in
*                            tiSgl1 when tiSgl1 is used with the type tiSglList.
*
*  \return:
*
*  \e tiSuccess:     SGL initialized successfully.
*  \e tiError:       Failed to initialize SGL.
*
*
*****************************************************************************/\
osGLOBAL bit32 itdsataIOPrepareSGL(
                  tiRoot_t                 *tiRoot,
                  tdIORequestBody_t        *tdIORequestBody,
                  tiSgl_t                  *tiSgl1,
                  void                     *sglVirtualAddr
                  )
{
  agsaSgl_t          *agSgl;

  /* Uppper should be zero-out */
  TI_DBG5(("itdsataIOPrepareSGL: start\n"));

  TI_DBG5(("itdsataIOPrepareSGL: tiSgl1->upper %d tiSgl1->lower %d tiSgl1->len %d\n",
    tiSgl1->upper, tiSgl1->lower, tiSgl1->len));
  TI_DBG5(("itdsataIOPrepareSGL: tiSgl1->type %d\n", tiSgl1->type));

  /* SGL for SATA request */
  agSgl = &(tdIORequestBody->transport.SATA.agSATARequestBody.agSgl);
  agSgl->len = 0;

  if (tiSgl1 == agNULL)
  {
    TI_DBG1(("itdsataIOPrepareSGL: Error tiSgl1 is NULL\n"));
    return tiError;
  }

  if (tdIORequestBody->IOType.InitiatorRegIO.expDataLength == 0)
  {
    TI_DBG3(("itdsataIOPrepareSGL: expDataLength is 0\n"));
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

/*****************************************************************************
 *! \brief  sataLLIOAbort
 *
 *   This routine is called to initiate an I/O abort to LL layer.
 *   This function implements/encapsulates HW and LL API dependency.
 *
 *  \param   tiRoot:      Pointer to TISA initiator driver/port instance.
 *  \param   taskTag:     Pointer to TISA I/O context to be aborted.
 *
 *  \return:
 *
 *  \e tiSuccess:     Abort request was successfully initiated.
 *  \e tiBusy:        No resources available, try again later.
 *  \e tiError:       Other errors that prevent the abort request from being
 *                    started..
 *
 *
 *****************************************************************************/
#ifdef REMOVED /* not in use */
GLOBAL bit32 sataLLIOAbort (
                tiRoot_t        *tiRoot,
                tiIORequest_t   *taskTag )

{
  tdsaRoot_t            *tdsaRoot;
  tdsaContext_t         *tdsaAllShared;
  agsaRoot_t            *agRoot;
  tdIORequestBody_t     *tdIORequestBody;
  agsaIORequest_t       *agIORequest;
  bit32                 status;

  TI_DBG2(("sataLLIOAbort: start\n"));

  tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agRoot          = &(tdsaAllShared->agRootNonInt);
  tdIORequestBody = (tdIORequestBody_t *)taskTag->tdData;
  agIORequest     = &(tdIORequestBody->agIORequest);

  status = saSATAAbort(agRoot, 0, agIORequest);

  TI_DBG2(("sataLLIOAbort: agIORequest %p\n", agIORequest));
  TI_DBG2(("sataLLIOAbort: saSATAAbort returns status, %x\n", status));

  if (status == AGSA_RC_SUCCESS)
  {
    return tiSuccess;
  }
  else
  {
    return tiError;
  }

}
#endif

#ifdef REMOVED
/*****************************************************************************
 *! \brief  sataLLReset
 *
 *   This routine is called to initiate a SATA device reset to LL layer.
 *   This function implements/encapsulates HW and LL API dependency.
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   option:           SATA device reset option
 *
 *  \return: None
 *
 *
 *****************************************************************************/
/* not in use */
GLOBAL  void  sataLLReset(
                  tiRoot_t          *tiRoot,
                  tiDeviceHandle_t  *tiDeviceHandle,
                  bit32             option)
{

  tdsaRoot_t            *tdsaRoot;
  tdsaContext_t         *tdsaAllShared;
  tdsaDeviceData_t      *oneDeviceData;
  agsaRoot_t            *agRoot;
  agsaDevHandle_t       *agDevHandle;

  TI_DBG2(("sataLLReset: extry\n"));

  tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agRoot          = &(tdsaAllShared->agRootNonInt);
  oneDeviceData   = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  agDevHandle     = oneDeviceData->agDevHandle;

  satSATADeviceReset( tiRoot,
                      oneDeviceData,
                      AGSA_PHY_HARD_RESET);

}
#endif /* 0 */
#endif  /* #ifdef SATA_ENABLE */
