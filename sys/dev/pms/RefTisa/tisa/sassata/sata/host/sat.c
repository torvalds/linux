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
 * The file implementing SCSI/ATA Translation (SAT).
 * The routines in this file are independent from HW LL API.
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
 *! \brief  satIOStart
 *
 *   This routine is called to initiate a new SCSI request to SATL.
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
GLOBAL bit32  satIOStart(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t  *tiScsiRequest,
                   satIOContext_t            *satIOContext
                  )
{

  bit32             retVal = tiSuccess;
  satDeviceData_t   *pSatDevData;
  scsiRspSense_t    *pSense;
  tiIniScsiCmnd_t   *scsiCmnd;
  tiLUN_t           *pLun;
  satInternalIo_t   *pSatIntIo;
#ifdef  TD_DEBUG_ENABLE
  tdsaDeviceData_t  *oneDeviceData;
#endif

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  pLun          = &scsiCmnd->lun;

  /*
   * Reject all other LUN other than LUN 0.
   */
  if ( ((pLun->lun[0] | pLun->lun[1] | pLun->lun[2] | pLun->lun[3] |
         pLun->lun[4] | pLun->lun[5] | pLun->lun[6] | pLun->lun[7] ) != 0) &&
        (scsiCmnd->cdb[0] != SCSIOPC_INQUIRY)
     )
  {
    TI_DBG1(("satIOStart: *** REJECT *** LUN not zero, cdb[0]=0x%x tiIORequest=%p tiDeviceHandle=%p\n",
                 scsiCmnd->cdb[0], tiIORequest, tiDeviceHandle));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_ILLEGAL_REQUEST,
                        0,
                        SCSI_SNSCODE_LOGICAL_NOT_SUPPORTED,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    retVal = tiSuccess;
    goto ext;
  }

  TI_DBG6(("satIOStart: satPendingIO %d satNCQMaxIO %d\n",pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));

  /* this may happen after tiCOMReset until OS sends inquiry */
  if (pSatDevData->IDDeviceValid == agFALSE && (scsiCmnd->cdb[0] != SCSIOPC_INQUIRY))
  {
#ifdef  TD_DEBUG_ENABLE
    oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
#endif
    TI_DBG1(("satIOStart: invalid identify device data did %d\n", oneDeviceData->id));
    retVal = tiIONoDevice;
    goto ext;
  }
  /*
   * Check if we need to return BUSY, i.e. recovery in progress
   */
  if (pSatDevData->satDriveState == SAT_DEV_STATE_IN_RECOVERY)
  {
#ifdef  TD_DEBUG_ENABLE
    oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
#endif
    TI_DBG1(("satIOStart: IN RECOVERY STATE cdb[0]=0x%x tiIORequest=%p tiDeviceHandle=%p\n",
                 scsiCmnd->cdb[0], tiIORequest, tiDeviceHandle));
    TI_DBG1(("satIOStart: IN RECOVERY STATE did %d\n", oneDeviceData->id));

    TI_DBG1(("satIOStart: device %p satPendingIO %d satNCQMaxIO %d\n",pSatDevData, pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    TI_DBG1(("satIOStart: device %p satPendingNCQIO %d satPendingNONNCQIO %d\n",pSatDevData, pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
    retVal = tiError;
    goto ext;
//    return tiBusy;
  }

  if (pSatDevData->satDeviceType == SATA_ATAPI_DEVICE)
  {
     if (scsiCmnd->cdb[0] == SCSIOPC_REPORT_LUN)
     {
        return satReportLun(tiRoot, tiIORequest, tiDeviceHandle, tiScsiRequest, satIOContext);
     }
     else
     {
        return satPacket(tiRoot, tiIORequest, tiDeviceHandle, tiScsiRequest, satIOContext);
     }
  }
  else /* pSatDevData->satDeviceType != SATA_ATAPI_DEVICE */
  {
     /* Parse CDB */
     switch(scsiCmnd->cdb[0])
     {
       case SCSIOPC_READ_6:
         retVal = satRead6( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);
         break;

       case SCSIOPC_READ_10:
         retVal = satRead10( tiRoot,
                             tiIORequest,
                             tiDeviceHandle,
                             tiScsiRequest,
                             satIOContext);
         break;

       case SCSIOPC_READ_12:
         TI_DBG5(("satIOStart: SCSIOPC_READ_12\n"));
         retVal = satRead12( tiRoot,
                             tiIORequest,
                             tiDeviceHandle,
                             tiScsiRequest,
                             satIOContext);
         break;

       case SCSIOPC_READ_16:
         retVal = satRead16( tiRoot,
                             tiIORequest,
                             tiDeviceHandle,
                             tiScsiRequest,
                             satIOContext);
         break;

       case SCSIOPC_WRITE_6:
         retVal = satWrite6( tiRoot,
                             tiIORequest,
                             tiDeviceHandle,
                             tiScsiRequest,
                             satIOContext);
         break;

       case SCSIOPC_WRITE_10:
         retVal = satWrite10( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);
         break;

       case SCSIOPC_WRITE_12:
         TI_DBG5(("satIOStart: SCSIOPC_WRITE_12 \n"));
         retVal = satWrite12( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);

         break;

       case SCSIOPC_WRITE_16:
         TI_DBG5(("satIOStart: SCSIOPC_WRITE_16\n"));
         retVal = satWrite16( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);

         break;

       case SCSIOPC_VERIFY_10:
         retVal = satVerify10( tiRoot,
                               tiIORequest,
                               tiDeviceHandle,
                               tiScsiRequest,
                               satIOContext);
         break;

       case SCSIOPC_VERIFY_12:
         TI_DBG5(("satIOStart: SCSIOPC_VERIFY_12\n"));
         retVal = satVerify12( tiRoot,
                               tiIORequest,
                               tiDeviceHandle,
                               tiScsiRequest,
                               satIOContext);
         break;

       case SCSIOPC_VERIFY_16:
         TI_DBG5(("satIOStart: SCSIOPC_VERIFY_16\n"));
         retVal = satVerify16( tiRoot,
                               tiIORequest,
                               tiDeviceHandle,
                               tiScsiRequest,
                               satIOContext);
         break;

       case SCSIOPC_TEST_UNIT_READY:
         retVal = satTestUnitReady( tiRoot,
                                    tiIORequest,
                                    tiDeviceHandle,
                                    tiScsiRequest,
                                    satIOContext);
         break;

       case SCSIOPC_INQUIRY:
         retVal = satInquiry( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);
         break;

       case SCSIOPC_REQUEST_SENSE:
         retVal = satRequestSense( tiRoot,
                                   tiIORequest,
                                   tiDeviceHandle,
                                   tiScsiRequest,
                                   satIOContext);
         break;

       case SCSIOPC_MODE_SENSE_6:
         retVal = satModeSense6( tiRoot,
                                 tiIORequest,
                                 tiDeviceHandle,
                                 tiScsiRequest,
                                 satIOContext);
         break;

       case SCSIOPC_MODE_SENSE_10: 
         retVal = satModeSense10( tiRoot,
                                 tiIORequest,
                                 tiDeviceHandle,
                                 tiScsiRequest,
                                 satIOContext);
         break;


       case SCSIOPC_READ_CAPACITY_10:
         retVal = satReadCapacity10( tiRoot,
                                     tiIORequest,
                                     tiDeviceHandle,
                                     tiScsiRequest,
                                     satIOContext);
         break;

       case SCSIOPC_READ_CAPACITY_16:
         retVal = satReadCapacity16( tiRoot,
                                     tiIORequest,
                                     tiDeviceHandle,
                                     tiScsiRequest,
                                     satIOContext);
         break;

       case SCSIOPC_REPORT_LUN:
         retVal = satReportLun( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);
         break;

       case SCSIOPC_FORMAT_UNIT: 
         TI_DBG5(("satIOStart: SCSIOPC_FORMAT_UNIT\n"));
         retVal = satFormatUnit( tiRoot,
                                 tiIORequest,
                                 tiDeviceHandle,
                                 tiScsiRequest,
                                 satIOContext);
         break;
       case SCSIOPC_SEND_DIAGNOSTIC: /* Table 28, p40 */
         TI_DBG5(("satIOStart: SCSIOPC_SEND_DIAGNOSTIC\n"));
         retVal = satSendDiagnostic( tiRoot,
                                     tiIORequest,
                                     tiDeviceHandle,
                                     tiScsiRequest,
                                     satIOContext);
         break;

       case SCSIOPC_START_STOP_UNIT:
         TI_DBG5(("satIOStart: SCSIOPC_START_STOP_UNIT\n"));
         retVal = satStartStopUnit( tiRoot,
                                    tiIORequest,
                                    tiDeviceHandle,
                                    tiScsiRequest,
                                    satIOContext);
         break;

       case SCSIOPC_WRITE_SAME_10: /*  sector and LBA; SAT p64 case 3 accessing payload and very
                                      inefficient now */
         TI_DBG5(("satIOStart: SCSIOPC_WRITE_SAME_10\n"));
         retVal = satWriteSame10( tiRoot,
                                  tiIORequest,
                                  tiDeviceHandle,
                                  tiScsiRequest,
                                  satIOContext);
         break;

       case SCSIOPC_WRITE_SAME_16: /* no support due to transfer length(sector count) */
         TI_DBG5(("satIOStart: SCSIOPC_WRITE_SAME_16\n"));
         retVal = satWriteSame16( tiRoot,
                                  tiIORequest,
                                  tiDeviceHandle,
                                  tiScsiRequest,
                                  satIOContext);
         break;

       case SCSIOPC_LOG_SENSE: /* SCT and log parameter(informational exceptions) */
         TI_DBG5(("satIOStart: SCSIOPC_LOG_SENSE\n"));
         retVal = satLogSense( tiRoot,
                               tiIORequest,
                               tiDeviceHandle,
                               tiScsiRequest,
                               satIOContext);
         break;

       case SCSIOPC_MODE_SELECT_6: /*mode layout and AlloLen check */
         TI_DBG5(("satIOStart: SCSIOPC_MODE_SELECT_6\n"));
         retVal = satModeSelect6( tiRoot,
                                  tiIORequest,
                                  tiDeviceHandle,
                                  tiScsiRequest,
                                  satIOContext);
         break;

       case SCSIOPC_MODE_SELECT_10: /* mode layout and AlloLen check and sharing CB with  satModeSelect6*/
         TI_DBG5(("satIOStart: SCSIOPC_MODE_SELECT_10\n"));
         retVal = satModeSelect10( tiRoot,
                                   tiIORequest,
                                   tiDeviceHandle,
                                   tiScsiRequest,
                                   satIOContext);
         break;

       case SCSIOPC_SYNCHRONIZE_CACHE_10: /* on error what to return, sharing CB with
                                           satSynchronizeCache16 */
         TI_DBG5(("satIOStart: SCSIOPC_SYNCHRONIZE_CACHE_10\n"));
         retVal = satSynchronizeCache10( tiRoot,
                                         tiIORequest,
                                         tiDeviceHandle,
                                         tiScsiRequest,
                                         satIOContext);
         break;

       case SCSIOPC_SYNCHRONIZE_CACHE_16:/* on error what to return, sharing CB with
                                            satSynchronizeCache16 */

         TI_DBG5(("satIOStart: SCSIOPC_SYNCHRONIZE_CACHE_16\n"));
         retVal = satSynchronizeCache16( tiRoot,
                                         tiIORequest,
                                         tiDeviceHandle,
                                         tiScsiRequest,
                                         satIOContext);
         break;

       case SCSIOPC_WRITE_AND_VERIFY_10: /* single write and multiple writes */
         TI_DBG5(("satIOStart: SCSIOPC_WRITE_AND_VERIFY_10\n"));
         retVal = satWriteAndVerify10( tiRoot,
                                       tiIORequest,
                                       tiDeviceHandle,
                                       tiScsiRequest,
                                       satIOContext);
         break;

       case SCSIOPC_WRITE_AND_VERIFY_12:
         TI_DBG5(("satIOStart: SCSIOPC_WRITE_AND_VERIFY_12\n"));
         retVal = satWriteAndVerify12( tiRoot,
                                       tiIORequest,
                                       tiDeviceHandle,
                                       tiScsiRequest,
                                       satIOContext);
         break;

       case SCSIOPC_WRITE_AND_VERIFY_16:
         TI_DBG5(("satIOStart: SCSIOPC_WRITE_AND_VERIFY_16\n"));
         retVal = satWriteAndVerify16( tiRoot,
                                       tiIORequest,
                                       tiDeviceHandle,
                                       tiScsiRequest,
                                       satIOContext);

         break;

       case SCSIOPC_READ_MEDIA_SERIAL_NUMBER:
         TI_DBG5(("satIOStart: SCSIOPC_READ_MEDIA_SERIAL_NUMBER\n"));
         retVal = satReadMediaSerialNumber( tiRoot,
                                            tiIORequest,
                                            tiDeviceHandle,
                                            tiScsiRequest,
                                            satIOContext);

         break;

       case SCSIOPC_READ_BUFFER:
         TI_DBG5(("satIOStart: SCSIOPC_READ_BUFFER\n"));
         retVal = satReadBuffer( tiRoot,
                                 tiIORequest,
                                 tiDeviceHandle,
                                 tiScsiRequest,
                                 satIOContext);

         break;

       case SCSIOPC_WRITE_BUFFER:
         TI_DBG5(("satIOStart: SCSIOPC_WRITE_BUFFER\n"));
         retVal = satWriteBuffer( tiRoot,
                                 tiIORequest,
                                 tiDeviceHandle,
                                 tiScsiRequest,
                                 satIOContext);

         break;

       case SCSIOPC_REASSIGN_BLOCKS:
         TI_DBG5(("satIOStart: SCSIOPC_REASSIGN_BLOCKS\n"));
         retVal = satReassignBlocks( tiRoot,
                                 tiIORequest,
                                 tiDeviceHandle,
                                 tiScsiRequest,
                                 satIOContext);

         break;

       default:
         /* Not implemented SCSI cmd, set up error response */
         TI_DBG1(("satIOStart: unsupported SCSI cdb[0]=0x%x tiIORequest=%p tiDeviceHandle=%p\n",
                    scsiCmnd->cdb[0], tiIORequest, tiDeviceHandle));

         satSetSensePayload( pSense,
                             SCSI_SNSKEY_ILLEGAL_REQUEST,
                             0,
                             SCSI_SNSCODE_INVALID_COMMAND,
                             satIOContext);

         ostiInitiatorIOCompleted( tiRoot,
                                   tiIORequest,
                                   tiIOSuccess,
                                   SCSI_STAT_CHECK_CONDITION,
                                   satIOContext->pTiSenseData,
                                   satIOContext->interruptContext );
         retVal = tiSuccess;

         break;

     }  /* end switch  */
  }
  if (retVal == tiBusy)
  {
#ifdef  TD_DEBUG_ENABLE
    oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
#endif
    TI_DBG1(("satIOStart: BUSY did %d\n", oneDeviceData->id));
    TI_DBG3(("satIOStart: LL is busy or target queue is full\n"));
    TI_DBG3(("satIOStart: device %p satPendingIO %d satNCQMaxIO %d\n",pSatDevData, pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
    TI_DBG3(("satIOStart: device %p satPendingNCQIO %d satPendingNONNCQIO %d\n",pSatDevData, pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
    pSatIntIo               = satIOContext->satIntIoContext;

    /* interal structure free */
    satFreeIntIoResource( tiRoot,
                          pSatDevData,
                          pSatIntIo);
  }

ext:
  return retVal;
}


/*****************************************************************************/
/*! \brief Setup up the SCSI Sense response.
 *
 *  This function is used to setup up the Sense Data payload for
 *     CHECK CONDITION status.
 *
 *  \param pSense:      Pointer to the scsiRspSense_t sense data structure.
 *  \param SnsKey:      SCSI Sense Key.
 *  \param SnsInfo:     SCSI Sense Info.
 *  \param SnsCode:     SCSI Sense Code.
 *
 *  \return: None
 */
/*****************************************************************************/
void satSetSensePayload( scsiRspSense_t   *pSense,
                         bit8             SnsKey,
                         bit32            SnsInfo,
                         bit16            SnsCode,
                         satIOContext_t   *satIOContext
                         )
{
  /* for fixed format sense data, SPC-4, p37 */
  bit32      i;
  bit32      senseLength;

  TI_DBG5(("satSetSensePayload: start\n"));

  senseLength  = sizeof(scsiRspSense_t);

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
    satIOContext->pTiSenseData->senseLen = 18;
  }
  else
  {
    TI_DBG1(("satSetSensePayload: satIOContext is NULL\n"));
  }
}

/*****************************************************************************/
/*! \brief Setup up the SCSI Sense response.
 *
 *  This function is used to setup up the Sense Data payload for
 *     CHECK CONDITION status.
 *
 *  \param pSense:      Pointer to the scsiRspSense_t sense data structure.
 *  \param SnsKey:      SCSI Sense Key.
 *  \param SnsInfo:     SCSI Sense Info.
 *  \param SnsCode:     SCSI Sense Code.
 *
 *  \return: None
 */
/*****************************************************************************/

void satSetDeferredSensePayload( scsiRspSense_t  *pSense,
                                 bit8             SnsKey,
                                 bit32            SnsInfo,
                                 bit16            SnsCode,
                                 satIOContext_t   *satIOContext
                                 )
{
  /* for fixed format sense data, SPC-4, p37 */
  bit32      i;
  bit32      senseLength;

  senseLength  = sizeof(scsiRspSense_t);

  /* zero out the data area */
  for (i=0;i< senseLength;i++)
  {
    ((bit8*)pSense)[i] = 0;
  }

  /*
   * SCSI Sense Data part of response data
   */
  pSense->snsRespCode  = 0x71;        /*  0xC0 == vendor specific */
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
    satIOContext->pTiSenseData->senseLen = 18;
  }
  else
  {
    TI_DBG1(("satSetDeferredSensePayload: satIOContext is NULL\n"));
  }

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
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     I/O request successfully initiated.
 *    - \e tiBusy:        No resources available, try again later.
 *    - \e tiIONoDevice:  Invalid device handle.
 *    - \e tiError:       Other errors.
 */
/*****************************************************************************/
GLOBAL bit32  satPacket(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t  *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_D2H_PKT;
  satDeviceData_t           *pSatDevData;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG3(("satPacket: start, SCSI CDB is 0x%X %X %X %X %X %X %X %X %X %X %X %X\n",
           scsiCmnd->cdb[0],scsiCmnd->cdb[1],scsiCmnd->cdb[2],scsiCmnd->cdb[3],
           scsiCmnd->cdb[4],scsiCmnd->cdb[5],scsiCmnd->cdb[6],scsiCmnd->cdb[7],
           scsiCmnd->cdb[8],scsiCmnd->cdb[9],scsiCmnd->cdb[10],scsiCmnd->cdb[11]));

  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set 1*/
  fis->h.command        = SAT_PACKET;             /* 0xA0 */
  if (pSatDevData->satDMADIRSupport)              /* DMADIR enabled*/
  {
     fis->h.features    = (tiScsiRequest->dataDirection == tiDirectionIn)? 0x04 : 0; /* 1 for D2H, 0 for H2D */
  }
  else
  {
     fis->h.features    = 0;                      /* FIS reserve */
  }
  /* Byte count low and byte count high */
  if ( scsiCmnd->expDataLength > 0xFFFF )
  {
     fis->d.lbaMid = 0xFF;                               /* FIS LBA (7 :0 ) */
     fis->d.lbaHigh = 0xFF;                              /* FIS LBA (15:8 ) */
  }
  else
  {
     fis->d.lbaMid = (bit8)scsiCmnd->expDataLength;       /* FIS LBA (7 :0 ) */
     fis->d.lbaHigh = (bit8)(scsiCmnd->expDataLength>>8); /* FIS LBA (15:8 ) */
  }

  fis->d.lbaLow         = 0;                      /* FIS LBA (7 :0 ) */
  fis->d.device         = 0;                      /* FIS LBA (27:24) and FIS LBA mode  */
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;                       /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                      /* FIS HOB bit clear */
  fis->d.reserved5      = 0;

  satIOContext->ATACmd = SAT_PACKET;

  if (tiScsiRequest->dataDirection == tiDirectionIn)
  {
      agRequestType = AGSA_SATA_PROTOCOL_D2H_PKT;
  }
  else
  {
      agRequestType = AGSA_SATA_PROTOCOL_H2D_PKT;
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

  satIOContext->satCompleteCB = &satPacketCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satPacket: return\n"));
  return (status);
}

/*****************************************************************************/
/*! \brief SAT implementation for satSetFeatures.
 *
 *  This function creates SetFeatures fis and sends the request to LL layer
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
GLOBAL bit32  satSetFeatures(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t  *tiScsiRequest,
                            satIOContext_t            *satIOContext,
                            bit8                      bIsDMAMode
                            )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;
  TI_DBG3(("satSetFeatures: start\n"));

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
  if (bIsDMAMode)
  {
      fis->d.sectorCount = 0x45;
      /*satIOContext->satCompleteCB = &satSetFeaturesDMACB;*/
  }
  else
  {
      fis->d.sectorCount = 0x0C;
      /*satIOContext->satCompleteCB = &satSetFeaturesPIOCB;*/
  }
  satIOContext->satCompleteCB = &satSetFeaturesCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satSetFeatures: return\n"));

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
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     I/O request successfully initiated.
 *    - \e tiBusy:        No resources available, try again later.
 *    - \e tiIONoDevice:  Invalid device handle.
 *    - \e tiError:       Other errors.
 */
/*****************************************************************************/
GLOBAL bit32  satRequestSenseForATAPI(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t  *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_D2H_PKT;
  satDeviceData_t           *pSatDevData;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  scsiCmnd->cdb[0]   = SCSIOPC_REQUEST_SENSE;
  scsiCmnd->cdb[1]   = 0;
  scsiCmnd->cdb[2]   = 0;
  scsiCmnd->cdb[3]   = 0;
  scsiCmnd->cdb[4]   = SENSE_DATA_LENGTH;
  scsiCmnd->cdb[5]   = 0;
  TI_DBG3(("satRequestSenseForATAPI: start, SCSI CDB is 0x%X %X %X %X %X %X %X %X %X %X %X %X\n",
           scsiCmnd->cdb[0],scsiCmnd->cdb[1],scsiCmnd->cdb[2],scsiCmnd->cdb[3],
           scsiCmnd->cdb[4],scsiCmnd->cdb[5],scsiCmnd->cdb[6],scsiCmnd->cdb[7],
           scsiCmnd->cdb[8],scsiCmnd->cdb[9],scsiCmnd->cdb[10],scsiCmnd->cdb[11]));

  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set 1*/
  fis->h.command        = SAT_PACKET;             /* 0xA0 */
  if (pSatDevData->satDMADIRSupport)              /* DMADIR enabled*/
  {
     fis->h.features    = (tiScsiRequest->dataDirection == tiDirectionIn)? 0x04 : 0; /* 1 for D2H, 0 for H2D */
  }
  else
  {
     fis->h.features    = 0;                         /* FIS reserve */
  }

  fis->d.lbaLow         = 0;                         /* FIS LBA (7 :0 ) */
  fis->d.lbaMid         = 0;                         /* FIS LBA (15:8 ) */
  fis->d.lbaHigh        = 0x20;                      /* FIS LBA (23:16) */
  fis->d.device         = 0;                         /* FIS LBA (27:24) and FIS LBA mode  */
  fis->d.lbaLowExp      = 0;
  fis->d.lbaMidExp      = 0;
  fis->d.lbaHighExp     = 0;
  fis->d.featuresExp    = 0;
  fis->d.sectorCount    = 0;                          /* FIS sector count (7:0) */
  fis->d.sectorCountExp = 0;
  fis->d.reserved4      = 0;
  fis->d.control        = 0;                         /* FIS HOB bit clear */
  fis->d.reserved5      = (bit32)(scsiCmnd->cdb[0]|(scsiCmnd->cdb[1]<<8)|(scsiCmnd->cdb[2]<<16)|(scsiCmnd->cdb[3]<<24));

  satIOContext->ATACmd = SAT_PACKET;

  agRequestType = AGSA_SATA_PROTOCOL_D2H_PKT;

  //if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
       fis->h.features |= 0x01;
    }
    else
    {
       fis->h.features |= 0x0;
    }
  }

  satIOContext->satCompleteCB = &satRequestSenseForATAPICB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satRequestSenseForATAPI: return\n"));
  return (status);
}
/*****************************************************************************/
/*! \brief SAT implementation for satDeviceReset.
 *
 *  This function creates DEVICE RESET fis and sends the request to LL layer
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
GLOBAL bit32 satDeviceReset(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t  *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;

  TI_DBG3(("satDeviceReset: start\n"));

  /*
   * Send the  Execute Device Diagnostic command.
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  fis->h.command        = SAT_DEVICE_RESET;   /* 0x90 */
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
  satIOContext->satCompleteCB = &satDeviceResetCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG3(("satDeviceReset: return\n"));

  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for saExecuteDeviceDiagnostic.
 *
 *  This function creates Execute Device Diagnostic fis and sends the request to LL layer
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
GLOBAL bit32  satExecuteDeviceDiagnostic(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t  *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;

  TI_DBG3(("satExecuteDeviceDiagnostic: start\n"));

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
  satIOContext->satCompleteCB = &satExecuteDeviceDiagnosticCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satExecuteDeviceDiagnostic: return\n"));

  return status;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI READ10.
 *
 *  SAT implementation for SCSI READ10 and send FIS request to LL layer.
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
GLOBAL bit32  satRead10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{

  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satRead10: start\n"));
  TI_DBG5(("satRead10: pSatDevData=%p\n", pSatDevData));
  //  tdhexdump("satRead10", (bit8 *)scsiCmnd->cdb, 10);

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
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

    TI_DBG1(("satRead10: return FUA_NV\n"));
    return tiSuccess;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satRead10: return control\n"));
    return tiSuccess;
  }

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = scsiCmnd->cdb[7];   /* MSB */
  TL[3] = scsiCmnd->cdb[8];   /* LSB */

  rangeChk = satAddNComparebit32(LBA, TL);

  /* cbd10; computing LBA and transfer length */
  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];


  TI_DBG5(("satRead10: lba %d functioned lba %d\n", lba, satComputeCDB10LBA(satIOContext)));
  TI_DBG5(("satRead10: lba 0x%x functioned lba 0x%x\n", lba, satComputeCDB10LBA(satIOContext)));
  TI_DBG5(("satRead10: tl %d functioned tl %d\n", tl, satComputeCDB10TL(satIOContext)));

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
      TI_DBG1(("satRead10: return LBA out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }


    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satRead10: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }

  /* case 1 and 2 */
  if (!rangeChk) //  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* READ DMA*/
      /* in case that we can't fit the transfer length,
         we need to make it fit by sending multiple ATA cmnds */
      TI_DBG5(("satRead10: case 2\n"));


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
      /* in case that we can't fit the transfer length,
         we need to make it fit by sending multiple ATA cmnds */
      TI_DBG5(("satRead10: case 1\n"));

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

   /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* READ DMA EXT */
      TI_DBG5(("satRead10: case 3\n"));
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
      TI_DBG5(("satRead10: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* Check FUA bit */
      if (scsiCmnd->cdb[1] & SCSI_READ10_FUA_MASK)
      {
       
        /* for now, no support for FUA */
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
        return tiSuccess;
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

  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* READ FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      TI_DBG5(("satRead10: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }

    TI_DBG6(("satRead10: case 5\n"));

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


  //  tdhexdump("satRead10 final fis", (bit8 *)fis, sizeof(agsaFisRegHostToDevice_t));

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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_SECTORS_EXT || fis->h.command == SAT_READ_DMA_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_READ_FPDMA_QUEUED */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;

  /* Initialize CB for SATA completion.
   */
  if (LoopNum == 1)
  {
    TI_DBG5(("satRead10: NON CHAINED data\n"));
    satIOContext->satCompleteCB = &satNonChainedDataIOCB;
  }
  else
  {
    TI_DBG1(("satRead10: CHAINED data\n"));
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
    satIOContext->satCompleteCB = &satChainedDataIOCB;

  }

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satRead10: return\n"));
  return (status);

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satRead_1.
 *
 *  SAT implementation for SCSI satRead_1
 *  Sub function of satRead10
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
/*
 * as a part of loop for read10
 */
GLOBAL bit32  satRead_1(
                          tiRoot_t                  *tiRoot,
                          tiIORequest_t             *tiIORequest,
                          tiDeviceHandle_t          *tiDeviceHandle,
                          tiScsiInitiatorRequest_t *tiScsiRequest,
                          satIOContext_t            *satIOContext)
{
  /*
    Assumption: error check on lba and tl has been done in satRead*()
    lba = lba + tl;
  */
  bit32                     status;
  satIOContext_t            *satOrgIOContext = agNULL;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  TI_DBG2(("satRead_1: start\n"));

  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;
  scsiCmnd        = satOrgIOContext->pScsiCmnd;

  osti_memset(LBA,0, sizeof(LBA));

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_READ_DMA:
    DenomTL = 0xFF;
    break;
  case SAT_READ_SECTORS:
    DenomTL = 0xFF;
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
    TI_DBG1(("satRead_1: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
    return tiError;
    break;
  }

  Remainder = satOrgIOContext->OrgTL % DenomTL;
  satOrgIOContext->currentLBA = satOrgIOContext->currentLBA + DenomTL;
  lba = satOrgIOContext->currentLBA;

  LBA[0] = (bit8)((lba & 0xF000) >> (8 * 3));
  LBA[1] = (bit8)((lba & 0xF00) >> (8 * 2));
  LBA[2] = (bit8)((lba & 0xF0) >> 8);
  LBA[3] = (bit8)(lba & 0xF);


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
      fis->d.sectorCount    = 0xFF;                  /* FIS sector count (7:0) */
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
      fis->d.sectorCount    = 0xFF;                   /* FIS sector count (7:0) */
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
    TI_DBG1(("satRead_1: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
    return tiError;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &satChainedDataIOCB;


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satRead_1: return\n"));
  return (status);
}
/*****************************************************************************/
/*! \brief SAT implementation for SCSI READ12.
 *
 *  SAT implementation for SCSI READ12 and send FIS request to LL layer.
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
GLOBAL bit32  satRead12(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satRead12: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
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

    TI_DBG1(("satRead12: return FUA_NV\n"));
    return tiSuccess;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satRead12: return control\n"));
    return tiSuccess;
  }

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */

  TL[0] = scsiCmnd->cdb[6];   /* MSB */
  TL[1] = scsiCmnd->cdb[7];
  TL[2] = scsiCmnd->cdb[8];
  TL[3] = scsiCmnd->cdb[9];   /* LSB */

  rangeChk = satAddNComparebit32(LBA, TL);

  lba = satComputeCDB12LBA(satIOContext);
  tl = satComputeCDB12TL(satIOContext);

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
      TI_DBG1(("satRead12: return LBA out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satRead12: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }

  /* case 1 and 2 */
  if (!rangeChk) //  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* READ DMA*/
      /* in case that we can't fit the transfer length,
         we need to make it fit by sending multiple ATA cmnds */
      TI_DBG5(("satRead12: case 2\n"));


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
      TI_DBG5(("satRead12: case 1\n"));

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
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* READ DMA EXT */
      TI_DBG5(("satRead12: case 3\n"));
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
      TI_DBG5(("satRead12: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* Check FUA bit */
      if (scsiCmnd->cdb[1] & SCSI_READ12_FUA_MASK)
      {
        /* for now, no support for FUA */
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
        return tiSuccess;
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
      TI_DBG5(("satRead12: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }

    TI_DBG6(("satRead12: case 5\n"));

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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_SECTORS_EXT || fis->h.command == SAT_READ_DMA_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_READ_FPDMA_QUEUEDK */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    TI_DBG5(("satRead12: NON CHAINED data\n"));
    satIOContext->satCompleteCB = &satNonChainedDataIOCB;
  }
  else
  {
    TI_DBG1(("satRead12: CHAINED data\n"));
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
    satIOContext->satCompleteCB = &satChainedDataIOCB;
  }

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satRead12: return\n"));
  return (status);
}
/*****************************************************************************/
/*! \brief SAT implementation for SCSI READ16.
 *
 *  SAT implementation for SCSI READ16 and send FIS request to LL layer.
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
GLOBAL bit32  satRead16(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */
  bit32                     limitChk = agFALSE; /* lba and tl range check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satRead16: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
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

    TI_DBG1(("satRead16: return FUA_NV\n"));
    return tiSuccess;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satRead16: return control\n"));
    return tiSuccess;
  }


  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));


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

 rangeChk = satAddNComparebit64(LBA, TL);

 limitChk = satCompareLBALimitbit(LBA);

 lba = satComputeCDB16LBA(satIOContext);
 tl = satComputeCDB16TL(satIOContext);


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
    if (limitChk)
    {
      TI_DBG1(("satRead16: return LBA out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satRead16: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }

  /* case 1 and 2 */
  if (!rangeChk) //  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* READ DMA*/
      /* in case that we can't fit the transfer length,
         we need to make it fit by sending multiple ATA cmnds */
      TI_DBG5(("satRead16: case 2\n"));


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
      TI_DBG5(("satRead16: case 1\n"));

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
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* READ DMA EXT */
      TI_DBG5(("satRead16: case 3\n"));
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
      TI_DBG5(("satRead16: case 4\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

      /* Check FUA bit */
      if (scsiCmnd->cdb[1] & SCSI_READ16_FUA_MASK)
      {
      
        /* for now, no support for FUA */
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
        return tiSuccess;
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
      TI_DBG5(("satRead16: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }

    TI_DBG6(("satRead16: case 5\n"));

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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_SECTORS_EXT || fis->h.command == SAT_READ_DMA_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_READ_FPDMA_QUEUEDK */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    TI_DBG5(("satRead16: NON CHAINED data\n"));
    satIOContext->satCompleteCB = &satNonChainedDataIOCB;
  }
  else
  {
    TI_DBG1(("satRead16: CHAINED data\n"));
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
    satIOContext->satCompleteCB = &satChainedDataIOCB;
  }

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satRead16: return\n"));
  return (status);

}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI READ6.
 *
 *  SAT implementation for SCSI READ6 and send FIS request to LL layer.
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
GLOBAL bit32  satRead6(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{

  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_READ;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit16                     tl = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;


   TI_DBG5(("satRead6: start\n"));

  /* no FUA checking since read6 */


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satRead6: return control\n"));
    return tiSuccess;
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
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    TI_DBG1(("satRead6: return LBA out of range\n"));
    return tiSuccess;
    }
  }

  /* case 1 and 2 */
  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* READ DMA*/
      TI_DBG5(("satRead6: case 2\n"));


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
      TI_DBG5(("satRead6: case 1\n"));

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
      TI_DBG5(("satRead6: case 3\n"));
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
      TI_DBG5(("satRead6: case 4\n"));

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
      TI_DBG5(("satRead6: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }
    TI_DBG5(("satRead6: case 5\n"));

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
  satIOContext->satCompleteCB = &satNonChainedDataIOCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);

}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI WRITE16.
 *
 *  SAT implementation for SCSI WRITE16 and send FIS request to LL layer.
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
GLOBAL bit32  satWrite16(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */
  bit32                     limitChk = agFALSE; /* lba and tl range check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satWrite16: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
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

    TI_DBG1(("satWrite16: return FUA_NV\n"));
    return tiSuccess;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satWrite16: return control\n"));
    return tiSuccess;
  }


  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));


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

  rangeChk = satAddNComparebit64(LBA, TL);

  limitChk = satCompareLBALimitbit(LBA);

  lba = satComputeCDB16LBA(satIOContext);
  tl = satComputeCDB16TL(satIOContext);



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
    if (limitChk)
    {
      TI_DBG1(("satWrite16: return LBA out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satWrite16: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }

  /* case 1 and 2 */
  if (!rangeChk) //  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* In case that we can't fit the transfer length, we loop */
      TI_DBG5(("satWrite16: case 2\n"));
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
      TI_DBG5(("satWrite16: case 1\n"));
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
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      TI_DBG5(("satWrite16: case 3\n"));
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
      TI_DBG5(("satWrite16: case 4\n"));
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
      TI_DBG5(("satWrite16: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }
    TI_DBG6(("satWrite16: case 5\n"));

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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    TI_DBG5(("satWrite16: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedDataIOCB;
  }
  else
  {
    TI_DBG1(("satWrite16: CHAINED data\n"));
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
    satIOContext->satCompleteCB = &satChainedDataIOCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI WRITE12.
 *
 *  SAT implementation for SCSI WRITE12 and send FIS request to LL layer.
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
GLOBAL bit32  satWrite12(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satWrite12: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
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

    TI_DBG1(("satWrite12: return FUA_NV\n"));
    return tiSuccess;

  }


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satWrite12: return control\n"));
    return tiSuccess;
  }


  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */

  TL[0] = scsiCmnd->cdb[6];   /* MSB */
  TL[1] = scsiCmnd->cdb[7];
  TL[2] = scsiCmnd->cdb[8];
  TL[3] = scsiCmnd->cdb[9];   /* LSB */

  rangeChk = satAddNComparebit32(LBA, TL);

  lba = satComputeCDB12LBA(satIOContext);
  tl = satComputeCDB12TL(satIOContext);


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
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    TI_DBG1(("satWrite12: return LBA out of range, not EXT\n"));
    return tiSuccess;
    }

    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satWrite12: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }


  /* case 1 and 2 */
  if (!rangeChk) //  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* In case that we can't fit the transfer length, we loop */
      TI_DBG5(("satWrite12: case 2\n"));
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
      TI_DBG5(("satWrite12: case 1\n"));
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
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      TI_DBG5(("satWrite12: case 3\n"));
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
      TI_DBG5(("satWrite12: case 4\n"));
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
      TI_DBG5(("satWrite12: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }
    TI_DBG6(("satWrite12: case 5\n"));

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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    TI_DBG5(("satWrite12: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedDataIOCB;
  }
  else
  {
    TI_DBG1(("satWrite12: CHAINED data\n"));
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
    satIOContext->satCompleteCB = &satChainedDataIOCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI WRITE10.
 *
 *  SAT implementation for SCSI WRITE10 and send FIS request to LL layer.
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
GLOBAL bit32  satWrite10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{

  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satWrite10: start\n"));

  /* checking FUA_NV */
  if (scsiCmnd->cdb[1] & SCSI_FUA_NV_MASK)
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

    TI_DBG1(("satWrite10: return FUA_NV\n"));
    return tiSuccess;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satWrite10: return control\n"));
    return tiSuccess;
  }

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = scsiCmnd->cdb[7];  /* MSB */
  TL[3] = scsiCmnd->cdb[8];  /* LSB */

  rangeChk = satAddNComparebit32(LBA, TL);


  /* cbd10; computing LBA and transfer length */
  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

  TI_DBG5(("satWrite10: lba %d functioned lba %d\n", lba, satComputeCDB10LBA(satIOContext)));
  TI_DBG5(("satWrite10: tl %d functioned tl %d\n", tl, satComputeCDB10TL(satIOContext)));

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
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

      TI_DBG1(("satWrite10: return LBA out of range, not EXT\n"));
      TI_DBG1(("satWrite10: cdb 0x%x 0x%x 0x%x 0x%x\n",scsiCmnd->cdb[2], scsiCmnd->cdb[3],
             scsiCmnd->cdb[4], scsiCmnd->cdb[5]));
      TI_DBG1(("satWrite10: lba 0x%x SAT_TR_LBA_LIMIT 0x%x\n", lba, SAT_TR_LBA_LIMIT));
      return tiSuccess;
    }

    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satWrite10: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

      return tiSuccess;
    }

  }


  /* case 1 and 2 */
  if (!rangeChk) //  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* can't fit the transfer length */
      TI_DBG5(("satWrite10: case 2\n"));
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
      TI_DBG5(("satWrite10: case 1\n"));
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
  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      TI_DBG5(("satWrite10: case 3\n"));
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
      TI_DBG5(("satWrite10: case 4\n"));
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
      TI_DBG5(("satWrite10: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }
    TI_DBG6(("satWrite10: case 5\n"));

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

  //  tdhexdump("satWrite10 final fis", (bit8 *)fis, sizeof(agsaFisRegHostToDevice_t));

  satIOContext->currentLBA = lba;
  satIOContext->OrgTL = tl;

  /*
    computing number of loop and remainder for tl
    0xFF in case not ext
    0xFFFF in case EXT
  */
  if (fis->h.command == SAT_WRITE_SECTORS || fis->h.command == SAT_WRITE_DMA)
  {
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    TI_DBG5(("satWrite10: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedDataIOCB;
  }
  else
  {
    TI_DBG1(("satWrite10: CHAINED data\n"));
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
    satIOContext->satCompleteCB = &satChainedDataIOCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWrite_1.
 *
 *  SAT implementation for SCSI WRITE10 and send FIS request to LL layer.
 *  This is used when WRITE10 is divided into multiple ATA commands
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
GLOBAL bit32  satWrite_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    Assumption: error check on lba and tl has been done in satWrite*()
    lba = lba + tl;
  */
  bit32                     status;
  satIOContext_t            *satOrgIOContext = agNULL;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  TI_DBG2(("satWrite_1: start\n"));

  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;
  scsiCmnd        = satOrgIOContext->pScsiCmnd;

  osti_memset(LBA,0, sizeof(LBA));

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
    TI_DBG1(("satWrite_1: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
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
    TI_DBG1(("satWrite_1: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
    return tiError;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &satChainedDataIOCB;


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satWrite_1: return\n"));
  return (status);
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI WRITE6.
 *
 *  SAT implementation for SCSI WRITE6 and send FIS request to LL layer.
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
GLOBAL bit32  satWrite6(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{

  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit16                     tl = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satWrite6: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satWrite6: return control\n"));
    return tiSuccess;
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
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    TI_DBG1(("satWrite6: return LBA out of range\n"));
    return tiSuccess;
    }
  }

  /* case 1 and 2 */
  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      TI_DBG5(("satWrite6: case 2\n"));


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
      TI_DBG5(("satWrite6: case 1\n"));

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
      TI_DBG5(("satWrite6: case 3\n"));
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
      TI_DBG5(("satWrite6: case 4\n"));

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
      TI_DBG5(("satWrite6: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }
    TI_DBG5(("satWrite6: case 5\n"));

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
  satIOContext->satCompleteCB = &satNonChainedDataIOCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI TEST UNIT READY.
 *
 *  SAT implementation for SCSI TUR and send FIS request to LL layer.
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
GLOBAL bit32  satTestUnitReady(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{

  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG6(("satTestUnitReady: entry tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satTestUnitReady: return control\n"));
    return tiSuccess;
  }

  /* SAT revision 8, 8.11.2, p42*/
  if (pSatDevData->satStopState == agTRUE)
  {
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NOT_READY,
                        0,
                        SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_INITIALIZING_COMMAND_REQUIRED,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    TI_DBG1(("satTestUnitReady: stop state\n"));
    return tiSuccess;
  }

  /*
   * Check if format is in progress
   */
  
  if (pSatDevData->satDriveState == SAT_DEV_STATE_FORMAT_IN_PROGRESS)
  {
    TI_DBG1(("satTestUnitReady() FORMAT_IN_PROGRESS  tiDeviceHandle=%p tiIORequest=%p\n",
         tiDeviceHandle, tiIORequest));

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NOT_READY,
                        0,
                        SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_FORMAT_IN_PROGRESS,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    TI_DBG1(("satTestUnitReady: format in progress\n"));
    return tiSuccess;
  }

  /*
    check previously issued ATA command
  */
  if (pSatDevData->satPendingIO != 0)
  {
    if (pSatDevData->satDeviceFaultState == agTRUE)
    {
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_HARDWARE_ERROR,
                          0,
                          SCSI_SNSCODE_LOGICAL_UNIT_FAILURE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
      TI_DBG1(("satTestUnitReady: previous command ended in error\n"));
      return tiSuccess;
    }
  }
  /*
    check removalbe media feature set
   */
  if(pSatDevData->satRemovableMedia && pSatDevData->satRemovableMediaEnabled)
  {
    TI_DBG5(("satTestUnitReady: sending get media status cmnd\n"));
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
    satIOContext->satCompleteCB = &satTestUnitReadyCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);

    return (status);
  }
  /*
    number 6) in SAT p42
    send ATA CHECK POWER MODE
  */
   TI_DBG5(("satTestUnitReady: sending check power mode cmnd\n"));
   status = satTestUnitReady_1( tiRoot,
                               tiIORequest,
                               tiDeviceHandle,
                               tiScsiRequest,
                               satIOContext);
   return (status);
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satTestUnitReady_1.
 *
 *  SAT implementation for SCSI satTestUnitReady_1.
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
GLOBAL bit32  satTestUnitReady_1(
                         tiRoot_t                  *tiRoot,
                         tiIORequest_t             *tiIORequest,
                         tiDeviceHandle_t          *tiDeviceHandle,
                         tiScsiInitiatorRequest_t *tiScsiRequest,
                         satIOContext_t            *satIOContext)
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

  TI_DBG5(("satTestUnitReady_1: start\n"));

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
  satIOContext->satCompleteCB = &satTestUnitReadyCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satTestUnitReady_1: return\n"));

  return status;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satReportLun.
 *
 *  SAT implementation for SCSI satReportLun. Only LUN0 is reported.
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
GLOBAL bit32  satReportLun(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  scsiRspSense_t        *pSense;
  bit32                 allocationLen;
  bit32                 reportLunLen;
  scsiReportLun_t       *pReportLun;
  tiIniScsiCmnd_t       *scsiCmnd;

  TI_DBG5(("satReportLun entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  pSense     = satIOContext->pSense;
  pReportLun = (scsiReportLun_t *) tiScsiRequest->sglVirtualAddr;
  scsiCmnd   = &tiScsiRequest->scsiCmnd;

//  tdhexdump("satReportLun cdb", (bit8 *)scsiCmnd, 16);

  /* Find the buffer size allocated by Initiator */
  allocationLen = (((bit32)scsiCmnd->cdb[6]) << 24) |
                  (((bit32)scsiCmnd->cdb[7]) << 16) |
                  (((bit32)scsiCmnd->cdb[8]) << 8 ) |
                  (((bit32)scsiCmnd->cdb[9])      );

  reportLunLen  = 16;     /* 8 byte header and 8 bytes of LUN0 */

  if (allocationLen < reportLunLen)
  {
    TI_DBG1(("satReportLun *** ERROR *** insufficient len=0x%x tiDeviceHandle=%p tiIORequest=%p\n",
        reportLunLen, tiDeviceHandle, tiIORequest));

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
    return tiSuccess;

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

  if (allocationLen > reportLunLen)
  {
    /* underrun */
    TI_DBG1(("satReportLun reporting underrun reportLunLen=0x%x allocationLen=0x%x \n", reportLunLen, allocationLen));

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOUnderRun,
                              allocationLen - reportLunLen,
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
  return tiSuccess;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI REQUEST SENSE.
 *
 *  SAT implementation for SCSI REQUEST SENSE.
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
GLOBAL bit32  satRequestSense(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
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
  scsiRspSense_t            *pSense;
  satDeviceData_t           *pSatDevData;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  tdIORequestBody_t         *tdIORequestBody;
  satInternalIo_t           *satIntIo = agNULL;
  satIOContext_t            *satIOContext2;

  TI_DBG4(("satRequestSense entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  pSense            = (scsiRspSense_t *) tiScsiRequest->sglVirtualAddr;
  pSatDevData       = satIOContext->pSatDevData;
  scsiCmnd          = &tiScsiRequest->scsiCmnd;
  fis               = satIOContext->pFis;

  TI_DBG4(("satRequestSense: pSatDevData=%p\n", pSatDevData));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satRequestSense: return control\n"));
    return tiSuccess;
  }

  /*
    Only fixed format sense data is support. In other words, we don't support DESC bit is set
    in Request Sense
   */
  if ( scsiCmnd->cdb[1] & ATA_REMOVABLE_MEDIA_DEVICE_MASK )
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

    TI_DBG1(("satRequestSense: DESC bit is set, which we don't support\n"));
    return tiSuccess;
  }


  if (pSatDevData->satSMARTEnabled == agTRUE)
  {
    /* sends SMART RETURN STATUS */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_SMART_RETURN_STATUS;    /* 0xB0 */
    fis->h.features       = 0xDA;                   /* FIS features */
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
    satIOContext->satCompleteCB = &satRequestSenseCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);

    TI_DBG4(("satRequestSense: if return, status %d\n", status));
    return (status);
  }
  else
  {
    /*allocate iocontext for xmitting xmit SAT_CHECK_POWER_MODE
      then call satRequestSense2 */

    TI_DBG4(("satRequestSense: before satIntIo %p\n", satIntIo));
    /* allocate iocontext */
    satIntIo = satAllocIntIoResource( tiRoot,
                                      tiIORequest, /* original request */
                                      pSatDevData,
                                      tiScsiRequest->scsiCmnd.expDataLength,
                                      satIntIo);

    TI_DBG4(("satRequestSense: after satIntIo %p\n", satIntIo));

    if (satIntIo == agNULL)
    {
      /* memory allocation failure */
      satFreeIntIoResource( tiRoot,
                            pSatDevData,
                            satIntIo);

      /* failed during sending SMART RETURN STATUS */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext );

      TI_DBG4(("satRequestSense: else fail 1\n"));
      return tiSuccess;
    } /* end of memory allocation failure */


    /*
     * Need to initialize all the fields within satIOContext except
     * reqType and satCompleteCB which will be set depending on cmd.
     */

    if (satIntIo == agNULL)
    {
      TI_DBG4(("satRequestSense: satIntIo is NULL\n"));
    }
    else
    {
      TI_DBG4(("satRequestSense: satIntIo is NOT NULL\n"));
    }
    /* use this --- tttttthe one the same */


    satIntIo->satOrgTiIORequest = tiIORequest;
    tdIORequestBody = (tdIORequestBody_t *)satIntIo->satIntRequestBody;
    satIOContext2 = &(tdIORequestBody->transport.SATA.satIOContext);

    satIOContext2->pSatDevData   = pSatDevData;
    satIOContext2->pFis          = &(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
    satIOContext2->pScsiCmnd     = &(satIntIo->satIntTiScsiXchg.scsiCmnd);
    satIOContext2->pSense        = &(tdIORequestBody->transport.SATA.sensePayload);
    satIOContext2->pTiSenseData  = &(tdIORequestBody->transport.SATA.tiSenseData);
    satIOContext2->pTiSenseData->senseData = satIOContext2->pSense;
    satIOContext2->tiRequestBody = satIntIo->satIntRequestBody;
    satIOContext2->interruptContext = satIOContext->interruptContext;
    satIOContext2->satIntIoContext  = satIntIo;
    satIOContext2->ptiDeviceHandle = tiDeviceHandle;
    satIOContext2->satOrgIOContext = satIOContext;

    TI_DBG4(("satRequestSense: satIntIo->satIntTiScsiXchg.agSgl1.len %d\n", satIntIo->satIntTiScsiXchg.agSgl1.len));

    TI_DBG4(("satRequestSense: satIntIo->satIntTiScsiXchg.agSgl1.upper %d\n", satIntIo->satIntTiScsiXchg.agSgl1.upper));

    TI_DBG4(("satRequestSense: satIntIo->satIntTiScsiXchg.agSgl1.lower %d\n", satIntIo->satIntTiScsiXchg.agSgl1.lower));

    TI_DBG4(("satRequestSense: satIntIo->satIntTiScsiXchg.agSgl1.type %d\n", satIntIo->satIntTiScsiXchg.agSgl1.type));

    status = satRequestSense_1( tiRoot,
                               &(satIntIo->satIntTiIORequest),
                               tiDeviceHandle,
                               &(satIntIo->satIntTiScsiXchg),
                               satIOContext2);

    if (status != tiSuccess)
    {
      satFreeIntIoResource( tiRoot,
                            pSatDevData,
                            satIntIo);

      /* failed during sending SMART RETURN STATUS */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NO_SENSE,
                          0,
                          SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                agNULL,
                                satIOContext->interruptContext );

      TI_DBG1(("satRequestSense: else fail 2\n"));
      return tiSuccess;
    }
    TI_DBG4(("satRequestSense: else return success\n"));
    return tiSuccess;
  }
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI REQUEST SENSE.
 *
 *  SAT implementation for SCSI REQUEST SENSE.
 *  Sub function of satRequestSense
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
GLOBAL bit32  satRequestSense_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    sends SAT_CHECK_POWER_MODE
  */
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  TI_DBG4(("satRequestSense_1 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  fis               = satIOContext->pFis;
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
  satIOContext->satCompleteCB = &satRequestSenseCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */


  TI_DBG4(("satRequestSense_1: agSgl1.len %d\n", tiScsiRequest->agSgl1.len));

  TI_DBG4(("satRequestSense_1: agSgl1.upper %d\n", tiScsiRequest->agSgl1.upper));

  TI_DBG4(("satRequestSense_1: agSgl1.lower %d\n", tiScsiRequest->agSgl1.lower));

  TI_DBG4(("satRequestSense_1: agSgl1.type %d\n", tiScsiRequest->agSgl1.type));

  //  tdhexdump("satRequestSense_1", (bit8 *)fis, sizeof(agsaFisRegHostToDevice_t));

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);



  return status;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI INQUIRY.
 *
 *  SAT implementation for SCSI INQUIRY.
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
GLOBAL bit32  satInquiry(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    CMDDT bit is obsolete in SPC-3 and this is assumed in SAT revision 8
  */
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  satDeviceData_t           *pSatDevData;
  bit32                     status;

  TI_DBG5(("satInquiry: start\n"));
  TI_DBG5(("satInquiry entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));
  pSense      = satIOContext->pSense;
  scsiCmnd    = &tiScsiRequest->scsiCmnd;
  pSatDevData = satIOContext->pSatDevData;
  TI_DBG5(("satInquiry: pSatDevData=%p\n", pSatDevData));
  //tdhexdump("satInquiry", (bit8 *)scsiCmnd->cdb, 6);
  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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
    TI_DBG2(("satInquiry: return control\n"));
    return tiSuccess;
  }

  /* checking EVPD and Allocation Length */
  /* SPC-4 spec 6.4 p141 */
  /* EVPD bit == 0 && PAGE CODE != 0 */
  if ( !(scsiCmnd->cdb[1] & SCSI_EVPD_MASK) &&
       (scsiCmnd->cdb[2] != 0)
       )
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
    TI_DBG1(("satInquiry: return EVPD and PAGE CODE\n"));
    return tiSuccess;
  }
  TI_DBG6(("satInquiry: allocation length 0x%x %d\n", ((scsiCmnd->cdb[3]) << 8) + scsiCmnd->cdb[4], ((scsiCmnd->cdb[3]) << 8) + scsiCmnd->cdb[4]));

  /* convert OS IO to TD internal IO */
  if ( pSatDevData->IDDeviceValid == agFALSE)
  {
    status = satStartIDDev(
                         tiRoot,
                         tiIORequest,
                         tiDeviceHandle,
                         tiScsiRequest,
                         satIOContext
                         );
    TI_DBG6(("satInquiry: end status %d\n", status));
    return status;
  }
  else
  {
    TI_DBG6(("satInquiry: calling satInquiryIntCB\n"));
    satInquiryIntCB(
                    tiRoot,
                    tiIORequest,
                    tiDeviceHandle,
                    tiScsiRequest,
                    satIOContext
                    );

    return tiSuccess;
  }

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satReadCapacity10.
 *
 *  SAT implementation for SCSI satReadCapacity10.
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
GLOBAL bit32  satReadCapacity10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  scsiRspSense_t          *pSense;
  tiIniScsiCmnd_t         *scsiCmnd;
  bit8              *pVirtAddr;
  satDeviceData_t         *pSatDevData;
  agsaSATAIdentifyData_t  *pSATAIdData;
  bit32                   lastLba;
  bit32                   word117_118;
  bit32                   word117;
  bit32                   word118;
  TI_DBG5(("satReadCapacity10: start: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  pSense      = satIOContext->pSense;
  pVirtAddr   = (bit8 *) tiScsiRequest->sglVirtualAddr;
  scsiCmnd    = &tiScsiRequest->scsiCmnd;
  pSatDevData = satIOContext->pSatDevData;
  pSATAIdData = &pSatDevData->satIdentifyData;


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satReadCapacity10: return control\n"));
    return tiSuccess;
  }


  /*
   * If Logical block address is not set to zero, return error
   */
  if ((scsiCmnd->cdb[2] || scsiCmnd->cdb[3] || scsiCmnd->cdb[4] || scsiCmnd->cdb[5]))
  {
    TI_DBG1(("satReadCapacity10 *** ERROR *** logical address non zero, tiDeviceHandle=%p tiIORequest=%p\n",
        tiDeviceHandle, tiIORequest));

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
    return tiSuccess;

  }

  /*
   * If PMI bit is not zero, return error
   */
  if ( ((scsiCmnd->cdb[8]) & SCSI_READ_CAPACITY10_PMI_MASK) != 0 )
  {
    TI_DBG1(("satReadCapacity10 *** ERROR *** PMI is not zero, tiDeviceHandle=%p tiIORequest=%p\n",
        tiDeviceHandle, tiIORequest));

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
    return tiSuccess;

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
      pVirtAddr[0] = 0xFF;        /* MSB number of block */
      pVirtAddr[1] = 0xFF;
      pVirtAddr[2] = 0xFF;
      pVirtAddr[3] = 0xFF;        /* LSB number of block */
      TI_DBG1(("satReadCapacity10: returns 0xFFFFFFFF\n"));
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


      pVirtAddr[0] = (bit8)((lastLba >> 24) & 0xFF);    /* MSB */
      pVirtAddr[1] = (bit8)((lastLba >> 16) & 0xFF);
      pVirtAddr[2] = (bit8)((lastLba >> 8)  & 0xFF);
      pVirtAddr[3] = (bit8)((lastLba )      & 0xFF);    /* LSB */

      TI_DBG3(("satReadCapacity10: lastLba is 0x%x %d\n", lastLba, lastLba));
      TI_DBG3(("satReadCapacity10: LBA 0 is 0x%x %d\n", pVirtAddr[0], pVirtAddr[0]));
      TI_DBG3(("satReadCapacity10: LBA 1 is 0x%x %d\n", pVirtAddr[1], pVirtAddr[1]));
      TI_DBG3(("satReadCapacity10: LBA 2 is 0x%x %d\n", pVirtAddr[2], pVirtAddr[2]));
      TI_DBG3(("satReadCapacity10: LBA 3 is 0x%x %d\n", pVirtAddr[3], pVirtAddr[3]));

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

    pVirtAddr[0] = (bit8)((lastLba >> 24) & 0xFF);    /* MSB */
    pVirtAddr[1] = (bit8)((lastLba >> 16) & 0xFF);
    pVirtAddr[2] = (bit8)((lastLba >> 8)  & 0xFF);
    pVirtAddr[3] = (bit8)((lastLba )      & 0xFF);    /* LSB */
  }
  /* SAT Rev 8d */
  if (((pSATAIdData->word104_107[2]) & 0x1000) == 0)
  {
    TI_DBG5(("satReadCapacity10: Default Block Length is 512\n"));
    /*
     * Set the block size, fixed at 512 bytes.
     */
    pVirtAddr[4] = 0x00;        /* MSB block size in bytes */
    pVirtAddr[5] = 0x00;
    pVirtAddr[6] = 0x02;
    pVirtAddr[7] = 0x00;        /* LSB block size in bytes */
  }
  else
  {
    word118 = pSATAIdData->word112_126[6];
    word117 = pSATAIdData->word112_126[5];

    word117_118 = (word118 << 16) + word117;
    word117_118 = word117_118 * 2;
    pVirtAddr[4] = (bit8)((word117_118 >> 24) & 0xFF);        /* MSB block size in bytes */
    pVirtAddr[5] = (bit8)((word117_118 >> 16) & 0xFF);
    pVirtAddr[6] = (bit8)((word117_118 >> 8) & 0xFF);
    pVirtAddr[7] = (bit8)(word117_118 & 0xFF);                /* LSB block size in bytes */

    TI_DBG1(("satReadCapacity10: Nondefault word118 %d 0x%x \n", word118, word118));
    TI_DBG1(("satReadCapacity10: Nondefault word117 %d 0x%x \n", word117, word117));
    TI_DBG1(("satReadCapacity10: Nondefault Block Length is %d 0x%x \n",word117_118, word117_118));

  }

  /* fill in MAX LBA, which is used in satSendDiagnostic_1() */
  pSatDevData->satMaxLBA[0] = 0;            /* MSB */
  pSatDevData->satMaxLBA[1] = 0;
  pSatDevData->satMaxLBA[2] = 0;
  pSatDevData->satMaxLBA[3] = 0;
  pSatDevData->satMaxLBA[4] = pVirtAddr[0];
  pSatDevData->satMaxLBA[5] = pVirtAddr[1];
  pSatDevData->satMaxLBA[6] = pVirtAddr[2];
  pSatDevData->satMaxLBA[7] = pVirtAddr[3]; /* LSB */


  TI_DBG4(("satReadCapacity10 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x , tiDeviceHandle=%p tiIORequest=%p\n",
        pVirtAddr[0], pVirtAddr[1], pVirtAddr[2], pVirtAddr[3],
        pVirtAddr[4], pVirtAddr[5], pVirtAddr[6], pVirtAddr[7],
        tiDeviceHandle, tiIORequest));


  /*
   * Send the completion response now.
   */
  ostiInitiatorIOCompleted( tiRoot,
                            tiIORequest,
                            tiIOSuccess,
                            SCSI_STAT_GOOD,
                            agNULL,
                            satIOContext->interruptContext);
  return tiSuccess;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satReadCapacity16.
 *
 *  SAT implementation for SCSI satReadCapacity16.
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
GLOBAL bit32  satReadCapacity16(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{

  scsiRspSense_t          *pSense;
  tiIniScsiCmnd_t         *scsiCmnd;
  bit8                    *pVirtAddr;
  satDeviceData_t         *pSatDevData;
  agsaSATAIdentifyData_t  *pSATAIdData;
  bit32                   lastLbaLo;
  bit32                   allocationLen;
  bit32                   readCapacityLen  = 32;
  bit32                   i = 0;
  TI_DBG5(("satReadCapacity16 start: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  pSense      = satIOContext->pSense;
  pVirtAddr   = (bit8 *) tiScsiRequest->sglVirtualAddr;
  scsiCmnd    = &tiScsiRequest->scsiCmnd;
  pSatDevData = satIOContext->pSatDevData;
  pSATAIdData = &pSatDevData->satIdentifyData;

  /* Find the buffer size allocated by Initiator */
  allocationLen = (((bit32)scsiCmnd->cdb[10]) << 24) |
                  (((bit32)scsiCmnd->cdb[11]) << 16) |
                  (((bit32)scsiCmnd->cdb[12]) << 8 ) |
                  (((bit32)scsiCmnd->cdb[13])      );


  if (allocationLen < readCapacityLen)
  {
    TI_DBG1(("satReadCapacity16 *** ERROR *** insufficient len=0x%x readCapacityLen=0x%x\n", allocationLen, readCapacityLen));

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
    return tiSuccess;

  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satReadCapacity16: return control\n"));
    return tiSuccess;
  }

  /*
   * If Logical blcok address is not set to zero, return error
   */
  if ((scsiCmnd->cdb[2] || scsiCmnd->cdb[3] || scsiCmnd->cdb[4] || scsiCmnd->cdb[5]) ||
      (scsiCmnd->cdb[6] || scsiCmnd->cdb[7] || scsiCmnd->cdb[8] || scsiCmnd->cdb[9])  )
  {
    TI_DBG1(("satReadCapacity16 *** ERROR *** logical address non zero, tiDeviceHandle=%p tiIORequest=%p\n",
        tiDeviceHandle, tiIORequest));

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
    return tiSuccess;

  }

  /*
   * If PMI bit is not zero, return error
   */
  if ( ((scsiCmnd->cdb[14]) & SCSI_READ_CAPACITY16_PMI_MASK) != 0 )
  {
    TI_DBG1(("satReadCapacity16 *** ERROR *** PMI is not zero, tiDeviceHandle=%p tiIORequest=%p\n",
        tiDeviceHandle, tiIORequest));

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
    return tiSuccess;

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
    pVirtAddr[0] = (bit8)(((pSATAIdData->maxLBA48_63) >> 8) & 0xff);  /* MSB */
    pVirtAddr[1] = (bit8)((pSATAIdData->maxLBA48_63)        & 0xff);
    pVirtAddr[2] = (bit8)(((pSATAIdData->maxLBA32_47) >> 8) & 0xff);
    pVirtAddr[3] = (bit8)((pSATAIdData->maxLBA32_47)        & 0xff);

    lastLbaLo = (((pSATAIdData->maxLBA16_31) << 16) ) | (pSATAIdData->maxLBA0_15);
    lastLbaLo = lastLbaLo - 1;      /* LBA starts from zero */

    pVirtAddr[4] = (bit8)((lastLbaLo >> 24) & 0xFF);
    pVirtAddr[5] = (bit8)((lastLbaLo >> 16) & 0xFF);
    pVirtAddr[6] = (bit8)((lastLbaLo >> 8)  & 0xFF);
    pVirtAddr[7] = (bit8)((lastLbaLo )      & 0xFF);    /* LSB */

  }

  /*
   * For 28-bit addressing, set capacity information from Identify
   * Device Word 60-61.
   */
  else
  {
    pVirtAddr[0] = 0;       /* MSB */
    pVirtAddr[1] = 0;
    pVirtAddr[2] = 0;
    pVirtAddr[3] = 0;

    lastLbaLo = (((pSATAIdData->numOfUserAddressableSectorsHi) << 16) ) |
                  (pSATAIdData->numOfUserAddressableSectorsLo);
    lastLbaLo = lastLbaLo - 1;      /* LBA starts from zero */

    pVirtAddr[4] = (bit8)((lastLbaLo >> 24) & 0xFF);
    pVirtAddr[5] = (bit8)((lastLbaLo >> 16) & 0xFF);
    pVirtAddr[6] = (bit8)((lastLbaLo >> 8)  & 0xFF);
    pVirtAddr[7] = (bit8)((lastLbaLo )      & 0xFF);    /* LSB */

  }

  /*
   * Set the block size, fixed at 512 bytes.
   */
  pVirtAddr[8]  = 0x00;        /* MSB block size in bytes */
  pVirtAddr[9]  = 0x00;
  pVirtAddr[10] = 0x02;
  pVirtAddr[11] = 0x00;        /* LSB block size in bytes */


  /* fill in MAX LBA, which is used in satSendDiagnostic_1() */
  pSatDevData->satMaxLBA[0] = pVirtAddr[0];            /* MSB */
  pSatDevData->satMaxLBA[1] = pVirtAddr[1];
  pSatDevData->satMaxLBA[2] = pVirtAddr[2];
  pSatDevData->satMaxLBA[3] = pVirtAddr[3];
  pSatDevData->satMaxLBA[4] = pVirtAddr[4];
  pSatDevData->satMaxLBA[5] = pVirtAddr[5];
  pSatDevData->satMaxLBA[6] = pVirtAddr[6];
  pSatDevData->satMaxLBA[7] = pVirtAddr[7];             /* LSB */

  TI_DBG5(("satReadCapacity16 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x , tiDeviceHandle=%p tiIORequest=%p\n",
        pVirtAddr[0], pVirtAddr[1], pVirtAddr[2], pVirtAddr[3],
        pVirtAddr[4], pVirtAddr[5], pVirtAddr[6], pVirtAddr[7],
        pVirtAddr[8], pVirtAddr[9], pVirtAddr[10], pVirtAddr[11],
        tiDeviceHandle, tiIORequest));

  for(i=12;i<=31;i++)
  {
    pVirtAddr[i] = 0x00;
  }

  /*
   * Send the completion response now.
   */
  if (allocationLen > readCapacityLen)
  {
    /* underrun */
    TI_DBG1(("satReadCapacity16 reporting underrun readCapacityLen=0x%x allocationLen=0x%x \n", readCapacityLen, allocationLen));

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOUnderRun,
                              allocationLen - readCapacityLen,
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
  return tiSuccess;

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI MODE SENSE (6).
 *
 *  SAT implementation for SCSI MODE SENSE (6).
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
GLOBAL bit32  satModeSense6(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{

  scsiRspSense_t          *pSense;
  bit32                   requestLen;
  tiIniScsiCmnd_t         *scsiCmnd;
  bit32                   pageSupported;
  bit8                    page;
  bit8                    *pModeSense;    /* Mode Sense data buffer */
  satDeviceData_t         *pSatDevData;
  bit8                    PC;
  bit8                    AllPages[MODE_SENSE6_RETURN_ALL_PAGES_LEN];
  bit8                    Control[MODE_SENSE6_CONTROL_PAGE_LEN];
  bit8                    RWErrorRecovery[MODE_SENSE6_READ_WRITE_ERROR_RECOVERY_PAGE_LEN];
  bit8                    Caching[MODE_SENSE6_CACHING_LEN];
  bit8                    InfoExceptionCtrl[MODE_SENSE6_INFORMATION_EXCEPTION_CONTROL_PAGE_LEN];
  bit8                    lenRead = 0;


  TI_DBG5(("satModeSense6 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  pSense      = satIOContext->pSense;
  scsiCmnd    = &tiScsiRequest->scsiCmnd;
  pModeSense  = (bit8 *) tiScsiRequest->sglVirtualAddr;
  pSatDevData = satIOContext->pSatDevData;

  //tdhexdump("satModeSense6", (bit8 *)scsiCmnd->cdb, 6);
  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satModeSense6: return control\n"));
    return tiSuccess;
  }

  /* checking PC(Page Control)
     SAT revion 8, 8.5.3 p33 and 10.1.2, p66
  */
  PC = (bit8)((scsiCmnd->cdb[2]) & SCSI_MODE_SENSE6_PC_MASK);
  if (PC != 0)
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

    TI_DBG1(("satModeSense6: return due to PC value pc 0x%x\n", PC >> 6));
    return tiSuccess;
  }

  /* reading PAGE CODE */
  page = (bit8)((scsiCmnd->cdb[2]) & SCSI_MODE_SENSE6_PAGE_CODE_MASK);


  TI_DBG5(("satModeSense6: page=0x%x, tiDeviceHandle=%p tiIORequest=%p\n",
             page, tiDeviceHandle, tiIORequest));

  requestLen = scsiCmnd->cdb[4];

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

    TI_DBG1(("satModeSense6 *** ERROR *** not supported page 0x%x tiDeviceHandle=%p tiIORequest=%p\n",
        page, tiDeviceHandle, tiIORequest));

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_ILLEGAL_REQUEST,
                        0,
                        SCSI_SNSCODE_INVALID_COMMAND,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    return tiSuccess;
  }

  switch(page)
  {
  case MODESENSE_RETURN_ALL_PAGES:
    lenRead = (bit8)MIN(requestLen, MODE_SENSE6_RETURN_ALL_PAGES_LEN);
    break;
  case MODESENSE_CONTROL_PAGE: /* control */
    lenRead = (bit8)MIN(requestLen, MODE_SENSE6_CONTROL_PAGE_LEN);
    break;
  case MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE: /* Read-Write Error Recovery */
    lenRead = (bit8)MIN(requestLen, MODE_SENSE6_READ_WRITE_ERROR_RECOVERY_PAGE_LEN);
    break;
  case MODESENSE_CACHING: /* caching */
    lenRead = (bit8)MIN(requestLen, MODE_SENSE6_CACHING_LEN);
    break;
  case MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE: /* informational exceptions control*/
    lenRead = (bit8)MIN(requestLen, MODE_SENSE6_INFORMATION_EXCEPTION_CONTROL_PAGE_LEN);
    break;
  default:
    TI_DBG1(("satModeSense6: default error page %d\n", page));
    break;
  }

  if (page == MODESENSE_RETURN_ALL_PAGES)
  {
    TI_DBG5(("satModeSense6: MODESENSE_RETURN_ALL_PAGES\n"));
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
#ifdef NOT_YET
    if (pSatDevData->satWriteCacheEnabled == agTRUE)
    {
      AllPages[26] = 0x04;/* WCE bit is set */
    }
    else
    {
      AllPages[26] = 0x00;/* WCE bit is NOT set */
    }
#endif
    AllPages[26] = 0x00;/* WCE bit is NOT set */

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

    osti_memcpy(pModeSense, &AllPages, lenRead);
  }
  else if (page == MODESENSE_CONTROL_PAGE)
  {
    TI_DBG5(("satModeSense6: MODESENSE_CONTROL_PAGE\n"));
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

    osti_memcpy(pModeSense, &Control, lenRead);

  }
  else if (page == MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE)
  {
    TI_DBG5(("satModeSense6: MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE\n"));
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

    osti_memcpy(pModeSense, &RWErrorRecovery, lenRead);

  }
  else if (page == MODESENSE_CACHING)
  {
    TI_DBG5(("satModeSense6: MODESENSE_CACHING\n"));
    /* special case */
    if (requestLen == 4 && page == MODESENSE_CACHING)
    {
      TI_DBG5(("satModeSense6: linux 2.6.8.24 support\n"));

      pModeSense[0] = 0x20 - 1; /* 32 - 1 */
      pModeSense[1] = 0x00; /* default medium type (currently mounted medium type) */
      pModeSense[2] = 0x00; /* no write-protect, no support for DPO-FUA */
      pModeSense[3] = 0x08; /* block descriptor length */
      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext);
      return tiSuccess;
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
#ifdef NOT_YET
    if (pSatDevData->satWriteCacheEnabled == agTRUE)
    {
      Caching[14] = 0x04;/* WCE bit is set */
    }
    else
    {
      Caching[14] = 0x00;/* WCE bit is NOT set */
    }
#endif
    Caching[14] = 0x00;/* WCE bit is NOT set */

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

    osti_memcpy(pModeSense, &Caching, lenRead);

  }
  else if (page == MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE)
  {
    TI_DBG5(("satModeSense6: MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE\n"));
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
    osti_memcpy(pModeSense, &InfoExceptionCtrl, lenRead);

  }
  else
  {
    /* Error */
    TI_DBG1(("satModeSense6: Error page %d\n", page));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_ILLEGAL_REQUEST,
                        0,
                        SCSI_SNSCODE_INVALID_COMMAND,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    return tiSuccess;
  }

  /* there can be only underrun not overrun in error case */
  if (requestLen > lenRead)
  {
    TI_DBG6(("satModeSense6 reporting underrun lenRead=0x%x requestLen=0x%x tiIORequest=%p\n", lenRead, requestLen, tiIORequest));

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOUnderRun,
                              requestLen - lenRead,
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

  return tiSuccess;

}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI MODE SENSE (10).
 *
 *  SAT implementation for SCSI MODE SENSE (10).
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
GLOBAL bit32  satModeSense10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{

  scsiRspSense_t          *pSense;
  bit32                   requestLen;
  tiIniScsiCmnd_t         *scsiCmnd;
  bit32                   pageSupported;
  bit8                    page;
  bit8                    *pModeSense;    /* Mode Sense data buffer */
  satDeviceData_t         *pSatDevData;
  bit8                    PC; /* page control */
  bit8                    LLBAA; /* Long LBA Accepted */
  bit32                   index;
  bit8                    AllPages[MODE_SENSE10_RETURN_ALL_PAGES_LLBAA_LEN];
  bit8                    Control[MODE_SENSE10_CONTROL_PAGE_LLBAA_LEN];
  bit8                    RWErrorRecovery[MODE_SENSE10_READ_WRITE_ERROR_RECOVERY_PAGE_LLBAA_LEN];
  bit8                    Caching[MODE_SENSE10_CACHING_LLBAA_LEN];
  bit8                    InfoExceptionCtrl[MODE_SENSE10_INFORMATION_EXCEPTION_CONTROL_PAGE_LLBAA_LEN];
  bit8                    lenRead = 0;

  TI_DBG5(("satModeSense10 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  pSense      = satIOContext->pSense;
  scsiCmnd    = &tiScsiRequest->scsiCmnd;
  pModeSense  = (bit8 *) tiScsiRequest->sglVirtualAddr;
  pSatDevData = satIOContext->pSatDevData;

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satModeSense10: return control\n"));
    return tiSuccess;
  }

  /* checking PC(Page Control)
     SAT revion 8, 8.5.3 p33 and 10.1.2, p66
  */
  PC = (bit8)((scsiCmnd->cdb[2]) & SCSI_MODE_SENSE10_PC_MASK);
  if (PC != 0)
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

    TI_DBG1(("satModeSense10: return due to PC value pc 0x%x\n", PC));
    return tiSuccess;
  }
  /* finding LLBAA bit */
  LLBAA = (bit8)((scsiCmnd->cdb[1]) & SCSI_MODE_SENSE10_LLBAA_MASK);
  /* reading PAGE CODE */
  page = (bit8)((scsiCmnd->cdb[2]) & SCSI_MODE_SENSE10_PAGE_CODE_MASK);

  TI_DBG5(("satModeSense10: page=0x%x, tiDeviceHandle=%p tiIORequest=%p\n",
             page, tiDeviceHandle, tiIORequest));
  requestLen = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

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

    TI_DBG1(("satModeSense10 *** ERROR *** not supported page 0x%x tiDeviceHandle=%p tiIORequest=%p\n",
        page, tiDeviceHandle, tiIORequest));

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_ILLEGAL_REQUEST,
                        0,
                        SCSI_SNSCODE_INVALID_COMMAND,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    return tiSuccess;
  }

  switch(page)
  {
  case MODESENSE_RETURN_ALL_PAGES:
    if (LLBAA)
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_RETURN_ALL_PAGES_LLBAA_LEN);
    }
    else
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_RETURN_ALL_PAGES_LEN);
    }
    break;
  case MODESENSE_CONTROL_PAGE: /* control */
    if (LLBAA)
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_CONTROL_PAGE_LLBAA_LEN);
    }
    else
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_CONTROL_PAGE_LEN);
    }
    break;
  case MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE: /* Read-Write Error Recovery */
    if (LLBAA)
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_READ_WRITE_ERROR_RECOVERY_PAGE_LLBAA_LEN);
    }
    else
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_READ_WRITE_ERROR_RECOVERY_PAGE_LEN);
    }
    break;
  case MODESENSE_CACHING: /* caching */
    if (LLBAA)
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_CACHING_LLBAA_LEN);
    }
    else
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_CACHING_LEN);
    }
    break;
  case MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE: /* informational exceptions control*/
    if (LLBAA)
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_INFORMATION_EXCEPTION_CONTROL_PAGE_LLBAA_LEN);
    }
    else
    {
      lenRead = (bit8)MIN(requestLen, MODE_SENSE10_INFORMATION_EXCEPTION_CONTROL_PAGE_LEN);
    }
    break;
  default:
    TI_DBG1(("satModeSense10: default error page %d\n", page));
    break;
  }

  if (page == MODESENSE_RETURN_ALL_PAGES)
  {
    TI_DBG5(("satModeSense10: MODESENSE_RETURN_ALL_PAGES\n"));
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
#ifdef NOT_YET
    if (pSatDevData->satWriteCacheEnabled == agTRUE)
    {
      AllPages[index+14] = 0x04;/* WCE bit is set */
    }
    else
    {
      AllPages[index+14] = 0x00;/* WCE bit is NOT set */
    }
#endif
    AllPages[index+14] = 0x00;/* WCE bit is NOT set */
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

    osti_memcpy(pModeSense, &AllPages, lenRead);
  }
  else if (page == MODESENSE_CONTROL_PAGE)
  {
    TI_DBG5(("satModeSense10: MODESENSE_CONTROL_PAGE\n"));
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

    osti_memcpy(pModeSense, &Control, lenRead);
  }
  else if (page == MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE)
  {
    TI_DBG5(("satModeSense10: MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE\n"));
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

    osti_memcpy(pModeSense, &RWErrorRecovery, lenRead);
  }
  else if (page == MODESENSE_CACHING)
  {
    TI_DBG5(("satModeSense10: MODESENSE_CACHING\n"));
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
#ifdef NOT_YET
    if (pSatDevData->satWriteCacheEnabled == agTRUE)
    {
      Caching[index+2] = 0x04;/* WCE bit is set */
    }
    else
    {
      Caching[index+2] = 0x00;/* WCE bit is NOT set */
    }
#endif
    Caching[index+2] = 0x00;/* WCE bit is NOT set */
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
    osti_memcpy(pModeSense, &Caching, lenRead);

  }
  else if (page == MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE)
  {
    TI_DBG5(("satModeSense10: MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE\n"));
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
    osti_memcpy(pModeSense, &InfoExceptionCtrl, lenRead);

  }
  else
  {
    /* Error */
    TI_DBG1(("satModeSense10: Error page %d\n", page));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_ILLEGAL_REQUEST,
                        0,
                        SCSI_SNSCODE_INVALID_COMMAND,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    return tiSuccess;
  }

  if (requestLen > lenRead)
  {
    TI_DBG1(("satModeSense10 reporting underrun lenRead=0x%x requestLen=0x%x tiIORequest=%p\n", lenRead, requestLen, tiIORequest));

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOUnderRun,
                              requestLen - lenRead,
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

  return tiSuccess;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI VERIFY (10).
 *
 *  SAT implementation for SCSI VERIFY (10).
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
GLOBAL bit32  satVerify10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    For simple implementation,
    no byte comparison supported as of 4/5/06
  */
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */


  TI_DBG5(("satVerify10 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  pSense            = satIOContext->pSense;
  scsiCmnd          = &tiScsiRequest->scsiCmnd;
  pSatDevData       = satIOContext->pSatDevData;
  fis               = satIOContext->pFis;

  /* checking BYTCHK */
  if (scsiCmnd->cdb[1] & SCSI_VERIFY_BYTCHK_MASK)
  {
    /*
      should do the byte check
      but not supported in this version
     */
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

    TI_DBG1(("satVerify10: no byte checking \n"));
    return tiSuccess;
  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satVerify10: return control\n"));
    return tiSuccess;
  }

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = scsiCmnd->cdb[7];  /* MSB */
  TL[3] = scsiCmnd->cdb[8];  /* LSB */

  rangeChk = satAddNComparebit32(LBA, TL);

  /* cbd10; computing LBA and transfer length */
  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    if (lba > SAT_TR_LBA_LIMIT - 1)
    {
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    TI_DBG1(("satVerify10: return LBA out of range, not EXT\n"));
    TI_DBG1(("satVerify10: cdb 0x%x 0x%x 0x%x 0x%x\n",scsiCmnd->cdb[2], scsiCmnd->cdb[3],
             scsiCmnd->cdb[4], scsiCmnd->cdb[5]));
    TI_DBG1(("satVerify10: lba 0x%x SAT_TR_LBA_LIMIT 0x%x\n", lba, SAT_TR_LBA_LIMIT));
    return tiSuccess;
    }

    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satVerify10: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    TI_DBG5(("satVerify10: SAT_READ_VERIFY_SECTORS_EXT\n"));
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
    TI_DBG5(("satVerify10: SAT_READ_VERIFY_SECTORS\n"));
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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    TI_DBG1(("satVerify10: error case 1!!!\n"));
    LoopNum = 1;
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    TI_DBG5(("satVerify10: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedVerifyCB;
  }
  else
  {
    TI_DBG1(("satVerify10: CHAINED data\n"));
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
      TI_DBG1(("satVerify10: error case 2!!!\n"));
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satChainedVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}

GLOBAL bit32  satChainedVerify(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  satIOContext_t            *satOrgIOContext = agNULL;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  TI_DBG2(("satChainedVerify: start\n"));

  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;
  osti_memset(LBA,0, sizeof(LBA));

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_READ_VERIFY_SECTORS:
    DenomTL = 0xFF;
    break;
  case SAT_READ_VERIFY_SECTORS_EXT:
    DenomTL = 0xFFFF;
    break;
  default:
    TI_DBG1(("satChainedVerify: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
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
    TI_DBG1(("satChainedVerify: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
    return tiError;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &satChainedVerifyCB;


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satChainedVerify: return\n"));
  return (status);

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI VERIFY (12).
 *
 *  SAT implementation for SCSI VERIFY (12).
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
GLOBAL bit32  satVerify12(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    For simple implementation,
    no byte comparison supported as of 4/5/06
  */
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */

  TI_DBG5(("satVerify12 entry: tiDeviceHandle=%p tiIORequest=%p\n",
           tiDeviceHandle, tiIORequest));

  pSense            = satIOContext->pSense;
  scsiCmnd          = &tiScsiRequest->scsiCmnd;
  pSatDevData       = satIOContext->pSatDevData;
  fis               = satIOContext->pFis;


  /* checking BYTCHK */
  if (scsiCmnd->cdb[1] & SCSI_VERIFY_BYTCHK_MASK)
  {
    /*
      should do the byte check
      but not supported in this version
     */
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

    TI_DBG1(("satVerify12: no byte checking \n"));
    return tiSuccess;
  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satVerify12: return control\n"));
    return tiSuccess;
  }

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */

  TL[0] = scsiCmnd->cdb[6];   /* MSB */
  TL[1] = scsiCmnd->cdb[7];
  TL[2] = scsiCmnd->cdb[7];
  TL[3] = scsiCmnd->cdb[8];   /* LSB */

  rangeChk = satAddNComparebit32(LBA, TL);

  lba = satComputeCDB12LBA(satIOContext);
  tl = satComputeCDB12TL(satIOContext);

  if (pSatDevData->satNCQ != agTRUE &&
      pSatDevData->sat48BitSupport != agTRUE
      )
  {
    if (lba > SAT_TR_LBA_LIMIT - 1)
    {
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    TI_DBG1(("satVerify12: return LBA out of range, not EXT\n"));
    TI_DBG1(("satVerify12: cdb 0x%x 0x%x 0x%x 0x%x\n",scsiCmnd->cdb[2], scsiCmnd->cdb[3],
             scsiCmnd->cdb[4], scsiCmnd->cdb[5]));
    TI_DBG1(("satVerify12: lba 0x%x SAT_TR_LBA_LIMIT 0x%x\n", lba, SAT_TR_LBA_LIMIT));
    return tiSuccess;
    }

    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satVerify12: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    TI_DBG5(("satVerify12: SAT_READ_VERIFY_SECTORS_EXT\n"));
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
    TI_DBG5(("satVerify12: SAT_READ_VERIFY_SECTORS\n"));
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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    TI_DBG1(("satVerify12: error case 1!!!\n"));
    LoopNum = 1;
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    TI_DBG5(("satVerify12: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedVerifyCB;
  }
  else
  {
    TI_DBG1(("satVerify12: CHAINED data\n"));
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
      TI_DBG1(("satVerify10: error case 2!!!\n"));
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satChainedVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}
/*****************************************************************************/
/*! \brief SAT implementation for SCSI VERIFY (16).
 *
 *  SAT implementation for SCSI VERIFY (16).
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
GLOBAL bit32  satVerify16(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    For simple implementation,
    no byte comparison supported as of 4/5/06
  */
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */
  bit32                     limitChk = agFALSE; /* lba and tl range check */

  TI_DBG5(("satVerify16 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  pSense            = satIOContext->pSense;
  scsiCmnd          = &tiScsiRequest->scsiCmnd;
  pSatDevData       = satIOContext->pSatDevData;
  fis               = satIOContext->pFis;

  /* checking BYTCHK */
  if (scsiCmnd->cdb[1] & SCSI_VERIFY_BYTCHK_MASK)
  {
    /*
      should do the byte check
      but not supported in this version
     */
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

    TI_DBG1(("satVerify16: no byte checking \n"));
    return tiSuccess;
  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satVerify16: return control\n"));
    return tiSuccess;
  }

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));


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

  rangeChk = satAddNComparebit64(LBA, TL);

  limitChk = satCompareLBALimitbit(LBA);

  lba = satComputeCDB16LBA(satIOContext);
  tl = satComputeCDB16TL(satIOContext);

  if (pSatDevData->satNCQ != agTRUE &&
     pSatDevData->sat48BitSupport != agTRUE
     )
  {
    if (limitChk)
    {
      TI_DBG1(("satVerify16: return LBA out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satVerify16: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    TI_DBG5(("satVerify16: SAT_READ_VERIFY_SECTORS_EXT\n"));
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
    TI_DBG5(("satVerify12: SAT_READ_VERIFY_SECTORS\n"));
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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    TI_DBG1(("satVerify12: error case 1!!!\n"));
    LoopNum = 1;
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    TI_DBG5(("satVerify12: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedVerifyCB;
  }
  else
  {
    TI_DBG1(("satVerify12: CHAINED data\n"));
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
      TI_DBG1(("satVerify10: error case 2!!!\n"));
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satChainedVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}
/*****************************************************************************/
/*! \brief SAT implementation for SCSI satFormatUnit.
 *
 *  SAT implementation for SCSI satFormatUnit.
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
GLOBAL bit32  satFormatUnit(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    note: we don't support media certification in this version and IP bit
    satDevData->satFormatState will be agFalse since SAT does not actually sends
    any ATA command
   */

  scsiRspSense_t          *pSense;
  tiIniScsiCmnd_t         *scsiCmnd;
  bit32                    index = 0;

  pSense        = satIOContext->pSense;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;

  TI_DBG5(("satFormatUnit:start\n"));

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
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satIOContext->interruptContext);

    TI_DBG2(("satFormatUnit: return opcode\n"));
    return tiSuccess;
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

      TI_DBG1(("satFormatUnit: return defect list format\n"));
      return tiSuccess;
    }
  }

   /* FMTDATA == 1 && CMPLIST == 1*/
  if ( (scsiCmnd->cdb[1] & SCSI_FORMAT_UNIT_FMTDATA_MASK) &&
       (scsiCmnd->cdb[1] & SCSI_FORMAT_UNIT_CMPLIST_MASK) )
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

    TI_DBG1(("satFormatUnit: return cmplist\n"));
    return tiSuccess;

  }

 if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satFormatUnit: return control\n"));
    return tiSuccess;
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
      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext);

      TI_DBG5(("satFormatUnit: return defect list case 1\n"));
      return tiSuccess;
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

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

      TI_DBG5(("satFormatUnit: return defect list case 2\n"));
      return tiSuccess;

    }
  }


  /*
   * Send the completion response now.
   */
  ostiInitiatorIOCompleted( tiRoot,
                            tiIORequest,
                            tiIOSuccess,
                            SCSI_STAT_GOOD,
                            agNULL,
                            satIOContext->interruptContext);

  TI_DBG5(("satFormatUnit: return last\n"));
  return tiSuccess;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satSendDiagnostic.
 *
 *  SAT implementation for SCSI satSendDiagnostic.
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
GLOBAL bit32  satSendDiagnostic(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     parmLen;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satSendDiagnostic:start\n"));

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

    TI_DBG1(("satSendDiagnostic: return PF, DEVOFFL, UNITOFFL, PARAM LIST\n"));
    return tiSuccess;
  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satSendDiagnostic: return control\n"));
    return tiSuccess;
  }

  parmLen = (scsiCmnd->cdb[3] << 8) + scsiCmnd->cdb[4];

  /* checking SELFTEST bit*/
  /* table 29, 8.10.3, p41 SAT Rev8 */
  /* case 1 */
  if ( !(scsiCmnd->cdb[1] & SCSI_SEND_DIAGNOSTIC_SELFTEST_MASK) &&
       (pSatDevData->satSMARTSelfTest == agFALSE)
       )
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

    TI_DBG1(("satSendDiagnostic: return Table 29 case 1\n"));
    return tiSuccess;
  }

  /* case 2 */
  if ( !(scsiCmnd->cdb[1] & SCSI_SEND_DIAGNOSTIC_SELFTEST_MASK) &&
       (pSatDevData->satSMARTSelfTest == agTRUE) &&
       (pSatDevData->satSMARTEnabled == agFALSE)
       )
  {
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_ABORTED_COMMAND,
                        0,
                        SCSI_SNSCODE_ATA_DEVICE_FEATURE_NOT_ENABLED,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );

    TI_DBG5(("satSendDiagnostic: return Table 29 case 2\n"));
    return tiSuccess;
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
    satIOContext->satCompleteCB = &satSendDiagnosticCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);


    TI_DBG5(("satSendDiagnostic: return Table 29 case 4\n"));
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
    fis->h.command        = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE;/* 0xB0 */
    fis->h.features       = 0xD4;                      /* FIS features NA       */
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
    satIOContext->satCompleteCB = &satSendDiagnosticCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);


    TI_DBG5(("satSendDiagnostic: return Table 29 case 5\n"));
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

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext );
      /* sends SMART EXECUTE OFF-LINE IMMEDIATE */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE;/* 0x40 */
      fis->h.features       = 0xD4;                      /* FIS features NA       */
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
      satIOContext->satCompleteCB = &satSendDiagnosticCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = sataLLIOStart( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);


      TI_DBG5(("satSendDiagnostic: return Table 28 case 1\n"));
      return (status);
    case 2:
      pSatDevData->satBGPendingDiag = agTRUE;

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext );


      /* issuing SMART EXECUTE OFF-LINE IMMEDIATE */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE;/* 0x40 */
      fis->h.features       = 0xD4;                      /* FIS features NA       */
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
      satIOContext->satCompleteCB = &satSendDiagnosticCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = sataLLIOStart( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);


      TI_DBG5(("satSendDiagnostic: return Table 28 case 2\n"));
      return (status);
    case 4:
      /* For simplicity, no abort is supported
         Returns good status
         need a flag in device data for previously sent background Send Diagnostic
      */
      if (parmLen != 0)
      {
        /* check condition */
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

        TI_DBG1(("satSendDiagnostic: case 4, non zero ParmLen %d\n", parmLen));
        return tiSuccess;
      }
      if (pSatDevData->satBGPendingDiag == agTRUE)
      {
        /* sends SMART EXECUTE OFF-LINE IMMEDIATE abort */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
        fis->h.command        = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE;/* 0x40 */
        fis->h.features       = 0xD4;                      /* FIS features NA       */
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
        satIOContext->satCompleteCB = &satSendDiagnosticCB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = sataLLIOStart( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);


        TI_DBG5(("satSendDiagnostic: send SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE case 3\n"));
        TI_DBG5(("satSendDiagnostic: Table 28 case 4\n"));
        return (status);
      }
      else
      {
        /* check condition */
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

        TI_DBG1(("satSendDiagnostic: case 4, no pending diagnostic in background\n"));
        TI_DBG5(("satSendDiagnostic: Table 28 case 4\n"));
        return tiSuccess;
      }
      break;
    case 5:
      /* issuing SMART EXECUTE OFF-LINE IMMEDIATE */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE;/* 0x40 */
      fis->h.features       = 0xD4;                      /* FIS features NA       */
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
      satIOContext->satCompleteCB = &satSendDiagnosticCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = sataLLIOStart( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);


      TI_DBG5(("satSendDiagnostic: return Table 28 case 5\n"));
      return (status);
    case 6:
      /* issuing SMART EXECUTE OFF-LINE IMMEDIATE */
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
      fis->h.command        = SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE;/* 0x40 */
      fis->h.features       = 0xD4;                      /* FIS features NA       */
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
      satIOContext->satCompleteCB = &satSendDiagnosticCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = sataLLIOStart( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);


      TI_DBG5(("satSendDiagnostic: return Table 28 case 6\n"));
      return (status);
    case 0:
    case 3: /* fall through */
    case 7: /* fall through */
    default:
      break;
    }/* switch */

    /* returns the results of default self-testing, which is good */
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satIOContext->interruptContext );

    TI_DBG5(("satSendDiagnostic: return Table 28 case 0,3,7 and default\n"));
    return tiSuccess;
  }


  ostiInitiatorIOCompleted( tiRoot,
                            tiIORequest,
                            tiIOSuccess,
                            SCSI_STAT_GOOD,
                            agNULL,
                            satIOContext->interruptContext );


  TI_DBG5(("satSendDiagnostic: return last\n"));
  return tiSuccess;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satSendDiagnostic_1.
 *
 *  SAT implementation for SCSI satSendDiagnostic_1.
 *  Sub function of satSendDiagnostic.
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
GLOBAL bit32  satSendDiagnostic_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    SAT Rev9, Table29, p41
    send 2nd SAT_READ_VERIFY_SECTORS(_EXT)
  */
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;

  TI_DBG5(("satSendDiagnostic_1 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

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
  satIOContext->satCompleteCB = &satSendDiagnosticCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);


  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satSendDiagnostic_2.
 *
 *  SAT implementation for SCSI satSendDiagnostic_2.
 *  Sub function of satSendDiagnostic.
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
GLOBAL bit32  satSendDiagnostic_2(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    SAT Rev9, Table29, p41
    send 3rd SAT_READ_VERIFY_SECTORS(_EXT)
  */
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;

  TI_DBG5(("satSendDiagnostic_2 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

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
  satIOContext->satCompleteCB = &satSendDiagnosticCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);


  return status;
}
/*****************************************************************************/
/*! \brief SAT implementation for SCSI satStartStopUnit.
 *
 *  SAT implementation for SCSI satStartStopUnit.
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
GLOBAL bit32  satStartStopUnit(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satStartStopUnit:start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satStartStopUnit: return control\n"));
    return tiSuccess;
  }

  /* Spec p55, Table 48 checking START and LOEJ bit */
  /* case 1 */
  if ( !(scsiCmnd->cdb[4] & SCSI_START_MASK) && !(scsiCmnd->cdb[4] & SCSI_LOEJ_MASK) )
  {
    if ( (scsiCmnd->cdb[1] & SCSI_IMMED_MASK) )
    {
      /* immed bit , SAT rev 8, 9.11.2.1 p 54*/
      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext );
      TI_DBG5(("satStartStopUnit: return table48 case 1-1\n"));
      return tiSuccess;
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
    satIOContext->satCompleteCB = &satStartStopUnitCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);


    TI_DBG5(("satStartStopUnit: return table48 case 1\n"));
    return (status);
  }
  /* case 2 */
  else if ( (scsiCmnd->cdb[4] & SCSI_START_MASK) && !(scsiCmnd->cdb[4] & SCSI_LOEJ_MASK) )
  {
    /* immed bit , SAT rev 8, 9.11.2.1 p 54*/
    if ( (scsiCmnd->cdb[1] & SCSI_IMMED_MASK) )
    {
      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext );

      TI_DBG5(("satStartStopUnit: return table48 case 2 1\n"));
      return tiSuccess;
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
    satIOContext->satCompleteCB = &satStartStopUnitCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);

    TI_DBG5(("satStartStopUnit: return table48 case 2 2\n"));
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
        ostiInitiatorIOCompleted( tiRoot,
                                  tiIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satIOContext->interruptContext );

        TI_DBG5(("satStartStopUnit: return table48 case 3 1\n"));
        return tiSuccess;
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
      satIOContext->satCompleteCB = &satStartStopUnitCB;

      /*
       * Prepare SGL and send FIS to LL layer.
       */
      satIOContext->reqType = agRequestType;       /* Save it */

      status = sataLLIOStart( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);

      return status;
    }
    else
    {
      /* no support for removal media */
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

      TI_DBG5(("satStartStopUnit: return Table 29 case 3 2\n"));
      return tiSuccess;
    }

  }
  /* case 4 */
  else /* ( (scsiCmnd->cdb[4] & SCSI_START_MASK) && (scsiCmnd->cdb[4] & SCSI_LOEJ_MASK) ) */
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

    TI_DBG5(("satStartStopUnit: return Table 29 case 4\n"));
    return tiSuccess;
  }


}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satStartStopUnit_1.
 *
 *  SAT implementation for SCSI satStartStopUnit_1.
 *  Sub function of satStartStopUnit
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
GLOBAL bit32  satStartStopUnit_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    SAT Rev 8, Table 48, 9.11.3 p55
    sends STANDBY
  */
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  TI_DBG5(("satStartStopUnit_1 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

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
  satIOContext->satCompleteCB = &satStartStopUnitCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satStartStopUnit_1 return status %d\n", status));
  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satRead10_2.
 *
 *  SAT implementation for SCSI satRead10_2
 *  Sub function of satRead10
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
GLOBAL bit32  satRead10_2(
                          tiRoot_t                  *tiRoot,
                          tiIORequest_t             *tiIORequest,
                          tiDeviceHandle_t          *tiDeviceHandle,
                          tiScsiInitiatorRequest_t *tiScsiRequest,
                          satIOContext_t            *satIOContext)
{
  /*
    externally generated ATA cmd, there is corresponding scsi cmnd
    called by satStartStopUnit() or maybe satRead10()
   */
   
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;

  TI_DBG5(("satReadVerifySectorsNoChain: start\n"));

  /* specifying ReadVerifySectors has no chain */
  pSatDevData->satVerifyState = 0xFFFFFFFF;

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    /* READ VERIFY SECTOR(S) EXT*/
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
    fis->h.command        = SAT_READ_VERIFY_SECTORS_EXT;/* 0x42 */
    fis->h.features       = 0;                      /* FIS reserve */
    fis->d.lbaLow         = 0x7F;                   /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = 0x4F;                   /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = 0x00;                   /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0xF1;                   /* FIS LBA (31:24) */
    fis->d.lbaMidExp      = 0x5F;                   /* FIS LBA (39:32) */
    fis->d.lbaHighExp     = 0xFF;                   /* FIS LBA (47:40) */
    fis->d.featuresExp    = 0;                      /* FIS reserve */
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;                      /* FIS sector count (15:8) */
    fis->d.reserved4      = 0;
    fis->d.device         = 0x4E;                   /* 01001110 */
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
    fis->d.lbaLow         = 0x7F;                      /* FIS LBA (7 :0 ) */
    fis->d.lbaMid         = 0x4F;                      /* FIS LBA (15:8 ) */
    fis->d.lbaHigh        = 0x00;                      /* FIS LBA (23:16) */
    fis->d.lbaLowExp      = 0;
    fis->d.lbaMidExp      = 0;
    fis->d.lbaHighExp     = 0;
    fis->d.featuresExp    = 0;
    fis->d.sectorCount    = 1;                      /* FIS sector count (7:0) */
    fis->d.sectorCountExp = 0;
    fis->d.reserved4      = 0;
    fis->d.device         = 0x4E;                   /* 01001110 */
    fis->d.control        = 0;                      /* FIS HOB bit clear */
    fis->d.reserved5      = 0;

    agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  }

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &satNonDataIOCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satReadVerifySectorsNoChain: return last\n"));

  return status;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteSame10.
 *
 *  SAT implementation for SCSI satWriteSame10.
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
GLOBAL bit32  satWriteSame10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satWriteSame10: start\n"));

  /* checking CONTROL */
    /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satWriteSame10: return control\n"));
    return tiSuccess;
  }


  /* checking LBDATA and PBDATA */
  /* case 1 */
  if ( !(scsiCmnd->cdb[1] & SCSI_WRITE_SAME_LBDATA_MASK) &&
       !(scsiCmnd->cdb[1] & SCSI_WRITE_SAME_PBDATA_MASK))
  {
    TI_DBG5(("satWriteSame10: case 1\n"));
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

      TI_DBG1(("satWriteSame10: return internal checking\n"));
      return tiSuccess;
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
        satSetSensePayload( pSense,
                            SCSI_SNSKEY_ILLEGAL_REQUEST,
                            0,
                            SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                            satIOContext);

        ostiInitiatorIOCompleted( tiRoot,
                                  tiIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_CHECK_CONDITION,
                                  satIOContext->pTiSenseData,
                                  satIOContext->interruptContext );

        TI_DBG1(("satWriteSame10: return LBA out of range\n"));
          return tiSuccess;
      }
    }

    if (lba + tl <= SAT_TR_LBA_LIMIT)
    {
      if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
      {
        /* case 2 */
        /* WRITE DMA */
        /* can't fit the transfer length since WRITE DMA has 1 byte for sector count */
        TI_DBG5(("satWriteSame10: case 1-2 !!! error due to writeSame10\n"));
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
        return tiSuccess;
      }
      else
      {
        /* case 1 */
        /* WRITE MULTIPLE or WRITE SECTOR(S) */
        /* WRITE SECTORS is chosen for easier implemetation */
        /* can't fit the transfer length since WRITE DMA has 1 byte for sector count */
        TI_DBG5(("satWriteSame10: case 1-1 !!! error due to writesame10\n"));
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
        return tiSuccess;
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
        TI_DBG5(("satWriteSame10: case 1-3\n"));
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
            TI_DBG5(("satWriteSame10: case 3 !!! warning can't fit sectors\n"));
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
            return tiSuccess;
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
        TI_DBG5(("satWriteSame10: case 1-4\n"));
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
            TI_DBG5(("satWriteSame10: case 4 !!! warning can't fit sectors\n"));
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
            return tiSuccess;
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
        TI_DBG5(("satWriteSame10: case 1-5 !!! error NCQ but 28 bit address support \n"));
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
        return tiSuccess;
      }
      TI_DBG5(("satWriteSame10: case 1-5\n"));

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
          TI_DBG5(("satWriteSame10: case 4 !!! warning can't fit sectors\n"));
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
          return tiSuccess;
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
    satIOContext->satCompleteCB = &satWriteSame10CB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);
    return (status);


  } /* end of case 1 */
  else if ( !(scsiCmnd->cdb[1] & SCSI_WRITE_SAME_LBDATA_MASK) &&
             (scsiCmnd->cdb[1] & SCSI_WRITE_SAME_PBDATA_MASK))
  {
    /* spec 9.26.2, Table 62, p64, case 2*/
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

    TI_DBG5(("satWriteSame10: return Table 62 case 2\n"));
    return tiSuccess;
  }
  else if ( (scsiCmnd->cdb[1] & SCSI_WRITE_SAME_LBDATA_MASK) &&
           !(scsiCmnd->cdb[1] & SCSI_WRITE_SAME_PBDATA_MASK))
  {
    TI_DBG5(("satWriteSame10: Table 62 case 3\n"));
    
  }
  else /* ( (scsiCmnd->cdb[1] & SCSI_WRITE_SAME_LBDATA_MASK) &&
            (scsiCmnd->cdb[1] & SCSI_WRITE_SAME_PBDATA_MASK)) */
  {

    /* spec 9.26.2, Table 62, p64, case 4*/
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

    TI_DBG5(("satWriteSame10: return Table 62 case 4\n"));
    return tiSuccess;
  }


  return tiSuccess;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteSame10_1.
 *
 *  SAT implementation for SCSI WRITESANE10 and send FIS request to LL layer.
 *  This is used when WRITESAME10 is divided into multiple ATA commands
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *  \param   lba:              LBA
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     I/O request successfully initiated.
 *    - \e tiBusy:        No resources available, try again later.
 *    - \e tiIONoDevice:  Invalid device handle.
 *    - \e tiError:       Other errors.
 */
/*****************************************************************************/
GLOBAL bit32  satWriteSame10_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
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

  TI_DBG5(("satWriteSame10_1 entry: tiDeviceHandle=%p tiIORequest=%p\n",
           tiDeviceHandle, tiIORequest));

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
  satIOContext->satCompleteCB = &satWriteSame10CB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satWriteSame10_1 return status %d\n", status));
  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteSame10_2.
 *
 *  SAT implementation for SCSI WRITESANE10 and send FIS request to LL layer.
 *  This is used when WRITESAME10 is divided into multiple ATA commands
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *  \param   lba:              LBA
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     I/O request successfully initiated.
 *    - \e tiBusy:        No resources available, try again later.
 *    - \e tiIONoDevice:  Invalid device handle.
 *    - \e tiError:       Other errors.
 */
/*****************************************************************************/
GLOBAL bit32  satWriteSame10_2(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
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

  TI_DBG5(("satWriteSame10_2 entry: tiDeviceHandle=%p tiIORequest=%p\n",
           tiDeviceHandle, tiIORequest));

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
  satIOContext->satCompleteCB = &satWriteSame10CB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satWriteSame10_2 return status %d\n", status));
  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteSame10_3.
 *
 *  SAT implementation for SCSI WRITESANE10 and send FIS request to LL layer.
 *  This is used when WRITESAME10 is divided into multiple ATA commands
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *  \param   lba:              LBA
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     I/O request successfully initiated.
 *    - \e tiBusy:        No resources available, try again later.
 *    - \e tiIONoDevice:  Invalid device handle.
 *    - \e tiError:       Other errors.
 */
/*****************************************************************************/
GLOBAL bit32  satWriteSame10_3(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
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

  TI_DBG5(("satWriteSame10_3 entry: tiDeviceHandle=%p tiIORequest=%p\n",
           tiDeviceHandle, tiIORequest));

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
  satIOContext->satCompleteCB = &satWriteSame10CB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satWriteSame10_2 return status %d\n", status));
  return status;
}
/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteSame16.
 *
 *  SAT implementation for SCSI satWriteSame16.
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
GLOBAL bit32  satWriteSame16(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  scsiRspSense_t            *pSense;

  pSense        = satIOContext->pSense;

  TI_DBG5(("satWriteSame16:start\n"));

 
  satSetSensePayload( pSense,
                      SCSI_SNSKEY_NO_SENSE,
                      0,
                      SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                      satIOContext);

  ostiInitiatorIOCompleted( tiRoot,
                            tiIORequest, /* == &satIntIo->satOrgTiIORequest */
                            tiIOSuccess,
                            SCSI_STAT_CHECK_CONDITION,
                            satIOContext->pTiSenseData,
                            satIOContext->interruptContext );
  TI_DBG5(("satWriteSame16: return internal checking\n"));
  return tiSuccess;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satLogSense_1.
 *
 *  Part of SAT implementation for SCSI satLogSense.
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
GLOBAL bit32  satLogSense_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;

  TI_DBG5(("satLogSense_1: start\n"));


  /* SAT Rev 8, 10.2.4 p74 */
  if ( pSatDevData->sat48BitSupport == agTRUE )
  {
    TI_DBG5(("satLogSense_1: case 2-1 sends READ LOG EXT\n"));
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
    satIOContext->satCompleteCB = &satLogSenseCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);
    return status;

  }
  else
  {
    TI_DBG5(("satLogSense_1: case 2-2 sends SMART READ LOG\n"));
    /* sends SMART READ LOG */
    fis->h.fisType        = 0x27;                   /* Reg host to device */
    fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

    fis->h.command        = SAT_SMART_READ_LOG;     /* 0x2F */
    fis->h.features       = 0x00;                   /* 0xd5 */
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
    satIOContext->satCompleteCB = &satLogSenseCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);
    return status;

  }
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satSMARTEnable.
 *
 *  Part of SAT implementation for SCSI satLogSense.
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
GLOBAL bit32  satSMARTEnable(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  TI_DBG4(("satSMARTEnable entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  fis               = satIOContext->pFis;

  /*
   * Send the SAT_SMART_ENABLE_OPERATIONS command.
   */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

  fis->h.command        = SAT_SMART_ENABLE_OPERATIONS;   /* 0xB0 */
  fis->h.features       = 0xD8;
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
  satIOContext->satCompleteCB = &satSMARTEnableCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);


  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satLogSense_3.
 *
 *  Part of SAT implementation for SCSI satLogSense.
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
GLOBAL bit32  satLogSense_3(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  TI_DBG4(("satLogSense_3 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  fis               = satIOContext->pFis;
  /* sends READ LOG EXT */
  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

  fis->h.command        = SAT_SMART_READ_LOG;     /* 0x2F */
  fis->h.features       = 0xD5;                   /* 0xd5 */
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
  satIOContext->satCompleteCB = &satLogSenseCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satLogSense_2.
 *
 *  Part of SAT implementation for SCSI satLogSense.
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
GLOBAL bit32  satLogSense_2(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  TI_DBG4(("satLogSense_2 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  fis               = satIOContext->pFis;
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
  satIOContext->satCompleteCB = &satLogSenseCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satLogSenseAllocate.
 *
 *  Part of SAT implementation for SCSI satLogSense.
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *  \param   payloadSize:      size of payload to be allocated.
 *  \param   flag:             flag value
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     I/O request successfully initiated.
 *    - \e tiBusy:        No resources available, try again later.
 *    - \e tiIONoDevice:  Invalid device handle.
 *    - \e tiError:       Other errors.
 *  \note
 *    - flag values: LOG_SENSE_0, LOG_SENSE_1, LOG_SENSE_2
 */
/*****************************************************************************/
GLOBAL bit32  satLogSenseAllocate(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
                   bit32                      payloadSize,
                   bit32                      flag
                   )
{
  satDeviceData_t           *pSatDevData;
  tdIORequestBody_t         *tdIORequestBody;
  satInternalIo_t           *satIntIo = agNULL;
  satIOContext_t            *satIOContext2;
  bit32                     status;

  TI_DBG4(("satLogSense_2 entry: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

  pSatDevData       = satIOContext->pSatDevData;

  /* create internal satIOContext */
  satIntIo = satAllocIntIoResource( tiRoot,
                                    tiIORequest, /* original request */
                                    pSatDevData,
                                    payloadSize,
                                    satIntIo);

  if (satIntIo == agNULL)
  {
    /* memory allocation failure */
    satFreeIntIoResource( tiRoot,
                          pSatDevData,
                          satIntIo);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              satIOContext->interruptContext );

    TI_DBG4(("satLogSense_2: fail in allocation\n"));
    return tiSuccess;
  } /* end of memory allocation failure */

  satIntIo->satOrgTiIORequest = tiIORequest;
  tdIORequestBody = (tdIORequestBody_t *)satIntIo->satIntRequestBody;
  satIOContext2 = &(tdIORequestBody->transport.SATA.satIOContext);

  satIOContext2->pSatDevData   = pSatDevData;
  satIOContext2->pFis          = &(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satIOContext2->pScsiCmnd     = &(satIntIo->satIntTiScsiXchg.scsiCmnd);
  satIOContext2->pSense        = &(tdIORequestBody->transport.SATA.sensePayload);
  satIOContext2->pTiSenseData  = &(tdIORequestBody->transport.SATA.tiSenseData);
  satIOContext2->pTiSenseData->senseData = satIOContext2->pSense;
  satIOContext2->tiRequestBody = satIntIo->satIntRequestBody;
  satIOContext2->interruptContext = satIOContext->interruptContext;
  satIOContext2->satIntIoContext  = satIntIo;
  satIOContext2->ptiDeviceHandle = tiDeviceHandle;
  satIOContext2->satOrgIOContext = satIOContext;

  if (flag == LOG_SENSE_0)
  {
    /* SAT_SMART_ENABLE_OPERATIONS */
    status = satSMARTEnable( tiRoot,
                         &(satIntIo->satIntTiIORequest),
                         tiDeviceHandle,
                         &(satIntIo->satIntTiScsiXchg),
                         satIOContext2);
  }
  else if (flag == LOG_SENSE_1)
  {
    /* SAT_READ_LOG_EXT */
    status = satLogSense_2( tiRoot,
                         &(satIntIo->satIntTiIORequest),
                         tiDeviceHandle,
                         &(satIntIo->satIntTiScsiXchg),
                         satIOContext2);
  }
  else
  {
    /* SAT_SMART_READ_LOG */
    /* SAT_READ_LOG_EXT */
    status = satLogSense_3( tiRoot,
                         &(satIntIo->satIntTiIORequest),
                         tiDeviceHandle,
                         &(satIntIo->satIntTiScsiXchg),
                         satIOContext2);

  }
  if (status != tiSuccess)
  {
    satFreeIntIoResource( tiRoot,
                          pSatDevData,
                          satIntIo);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              satIOContext->interruptContext );
    return tiSuccess;
  }


  return tiSuccess;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satLogSense.
 *
 *  SAT implementation for SCSI satLogSense.
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
GLOBAL bit32  satLogSense(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pLogPage;    /* Log Page data buffer */
  bit32                     flag = 0;
  bit16                     AllocLen = 0;       /* allocation length */
  bit8                      AllLogPages[8];
  bit16                     lenRead = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pLogPage      = (bit8 *) tiScsiRequest->sglVirtualAddr;

  TI_DBG5(("satLogSense: start\n"));

  osti_memset(&AllLogPages, 0, 8);
  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satLogSense: return control\n"));
    return tiSuccess;
  }


  AllocLen = (bit8)((scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8]);

  /* checking PC (Page Control) */
  /* nothing */

  /* special cases */
  if (AllocLen == 4)
  {
    TI_DBG1(("satLogSense: AllocLen is 4\n"));
    switch (scsiCmnd->cdb[2] & SCSI_LOG_SENSE_PAGE_CODE_MASK)
    {
      case LOGSENSE_SUPPORTED_LOG_PAGES:
        TI_DBG5(("satLogSense: case LOGSENSE_SUPPORTED_LOG_PAGES\n"));

        /* SAT Rev 8, 10.2.5 p76 */
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
            TI_DBG1(("satLogSense: error unallowed flag value %d\n", flag));
            break;
        }
        osti_memcpy(pLogPage, &AllLogPages, lenRead);
        break;
      case LOGSENSE_SELFTEST_RESULTS_PAGE:
        TI_DBG5(("satLogSense: case LOGSENSE_SUPPORTED_LOG_PAGES\n"));
        lenRead = 4;
        AllLogPages[0] = LOGSENSE_SELFTEST_RESULTS_PAGE;          /* page code */
        AllLogPages[1] = 0;          /* reserved  */
        /* page length = SELFTEST_RESULTS_LOG_PAGE_LENGTH - 1 - 3 = 400 = 0x190 */
        AllLogPages[2] = 0x01;
        AllLogPages[3] = 0x90;       /* page length */
        osti_memcpy(pLogPage, &AllLogPages, lenRead);

        break;
      case LOGSENSE_INFORMATION_EXCEPTIONS_PAGE:
        TI_DBG5(("satLogSense: case LOGSENSE_SUPPORTED_LOG_PAGES\n"));
        lenRead = 4;
        AllLogPages[0] = LOGSENSE_INFORMATION_EXCEPTIONS_PAGE;          /* page code */
        AllLogPages[1] = 0;          /* reserved  */
        AllLogPages[2] = 0;          /* page length */
        AllLogPages[3] = INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH - 1 - 3;       /* page length */
        osti_memcpy(pLogPage, &AllLogPages, lenRead);
        break;
      default:
        TI_DBG1(("satLogSense: default Page Code 0x%x\n", scsiCmnd->cdb[2] & SCSI_LOG_SENSE_PAGE_CODE_MASK));
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
        return tiSuccess;
    }
    ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext);
    return tiSuccess;

  } /* if */

  /* SAT rev8 Table 11  p30*/
  /* checking Page Code */
  switch (scsiCmnd->cdb[2] & SCSI_LOG_SENSE_PAGE_CODE_MASK)
  {
    case LOGSENSE_SUPPORTED_LOG_PAGES:
      TI_DBG5(("satLogSense: case 1\n"));

      /* SAT Rev 8, 10.2.5 p76 */

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
        TI_DBG1(("satLogSense: error unallowed flag value %d\n", flag));
        break;
      }

      osti_memcpy(pLogPage, &AllLogPages, lenRead);
      /* comparing allocation length to Log Page byte size */
      /* SPC-4, 4.3.4.6, p28 */
      if (AllocLen > lenRead )
      {
        TI_DBG1(("satLogSense reporting underrun lenRead=0x%x AllocLen=0x%x tiIORequest=%p\n", lenRead, AllocLen, tiIORequest));
       ostiInitiatorIOCompleted( tiRoot,
                                  tiIORequest,
                                  tiIOUnderRun,
                                  AllocLen - lenRead,
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
      break;
    case LOGSENSE_SELFTEST_RESULTS_PAGE:
      TI_DBG5(("satLogSense: case 2\n"));
      /* checking SMART self-test */
      if (pSatDevData->satSMARTSelfTest == agFALSE)
      {
        TI_DBG5(("satLogSense: case 2 no SMART Self Test\n"));
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
      }
      else
      {
        /* if satSMARTEnabled is false, send SMART_ENABLE_OPERATIONS */
        if (pSatDevData->satSMARTEnabled == agFALSE)
        {
          TI_DBG5(("satLogSense: case 2 calling satSMARTEnable\n"));
          status = satLogSenseAllocate(tiRoot,
                                       tiIORequest,
                                       tiDeviceHandle,
                                       tiScsiRequest,
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
          TI_DBG5(("satLogSense: case 2-1 sends READ LOG EXT\n"));
          status = satLogSenseAllocate(tiRoot,
                                       tiIORequest,
                                       tiDeviceHandle,
                                       tiScsiRequest,
                                       satIOContext,
                                       512,
                                       LOG_SENSE_1
                                       );

          return status;
        }
        else
        {
          TI_DBG5(("satLogSense: case 2-2 sends SMART READ LOG\n"));
          status = satLogSenseAllocate(tiRoot,
                                       tiIORequest,
                                       tiDeviceHandle,
                                       tiScsiRequest,
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
      TI_DBG5(("satLogSense: case 3\n"));
      /* checking SMART feature set */
      if (pSatDevData->satSMARTFeatureSet == agFALSE)
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
      }
      else
      {
        /* checking SMART feature enabled */
        if (pSatDevData->satSMARTEnabled == agFALSE)
        {
          satSetSensePayload( pSense,
                              SCSI_SNSKEY_ABORTED_COMMAND,
                              0,
                              SCSI_SNSCODE_ATA_DEVICE_FEATURE_NOT_ENABLED,
                              satIOContext);

          ostiInitiatorIOCompleted( tiRoot,
                                    tiIORequest,
                                    tiIOSuccess,
                                    SCSI_STAT_CHECK_CONDITION,
                                    satIOContext->pTiSenseData,
                                    satIOContext->interruptContext );
        }
        else
        {
          /* SAT Rev 8, 10.2.3 p72 */
          TI_DBG5(("satLogSense: case 3 sends SMART RETURN STATUS\n"));

          /* sends SMART RETURN STATUS */
          fis->h.fisType        = 0x27;                   /* Reg host to device */
          fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

          fis->h.command        = SAT_SMART_RETURN_STATUS;/* 0xB0 */
          fis->h.features       = 0xDA;                   /* FIS features */
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
          satIOContext->satCompleteCB = &satLogSenseCB;

          /*
           * Prepare SGL and send FIS to LL layer.
           */
          satIOContext->reqType = agRequestType;       /* Save it */

          status = sataLLIOStart( tiRoot,
                                  tiIORequest,
                                  tiDeviceHandle,
                                  tiScsiRequest,
                                  satIOContext);


          return status;
        }
      }
      break;
    default:
      TI_DBG1(("satLogSense: default Page Code 0x%x\n", scsiCmnd->cdb[2] & SCSI_LOG_SENSE_PAGE_CODE_MASK));
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

      break;
  } /* end switch */

  return tiSuccess;


}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satModeSelect6.
 *
 *  SAT implementation for SCSI satModeSelect6.
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
GLOBAL bit32  satModeSelect6(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pLogPage;    /* Log Page data buffer */
  bit32                     StartingIndex = 0;
  bit8                      PageCode = 0;
  bit32                     chkCnd = agFALSE;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pLogPage      = (bit8 *) tiScsiRequest->sglVirtualAddr;

  TI_DBG5(("satModeSelect6: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satModeSelect6: return control\n"));
    return tiSuccess;
  }

  /* checking PF bit */
  if ( !(scsiCmnd->cdb[1] & SCSI_MODE_SELECT6_PF_MASK))
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

    TI_DBG1(("satModeSelect6: PF bit check \n"));
    return tiSuccess;

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
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satIOContext->interruptContext);
    return tiSuccess;
  }
  else
  {
    TI_DBG1(("satModeSelect6: return mode parameter block descriptor 0x%x\n", pLogPage[3]));
    /* no more than one mode parameter block descriptor shall be supported */
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    return tiSuccess;
  }



  switch (PageCode) /* page code */
  {
  case MODESELECT_CONTROL_PAGE:
    TI_DBG1(("satModeSelect6: Control mode page\n"));
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

      TI_DBG1(("satModeSelect10: unexpected values\n"));
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
    return tiSuccess;
    break;
  case MODESELECT_READ_WRITE_ERROR_RECOVERY_PAGE:
    TI_DBG1(("satModeSelect6: Read-Write Error Recovery mode page\n"));
   
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
      TI_DBG5(("satModeSelect6: return check condition \n"));

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
      return tiSuccess;
    }
    else
    {
      TI_DBG5(("satModeSelect6: return GOOD \n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext);
      return tiSuccess;
    }

    break;
  case MODESELECT_CACHING:
    /* SAT rev8 Table67, p69*/
    TI_DBG5(("satModeSelect6: Caching mode page\n"));
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
      TI_DBG1(("satModeSelect6: return check condition \n"));

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
      return tiSuccess;

    }
    else
    {
      /* sends ATA SET FEATURES based on WCE bit */
      if ( !(pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_WCE_MASK) )
      {
        TI_DBG5(("satModeSelect6: disable write cache\n"));
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
        satIOContext->satCompleteCB = &satModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = sataLLIOStart( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);
        return status;
      }
      else
      {
        TI_DBG5(("satModeSelect6: enable write cache\n"));
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
        satIOContext->satCompleteCB = &satModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = sataLLIOStart( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);
        return status;

      }
    }
    break;
  case MODESELECT_INFORMATION_EXCEPTION_CONTROL_PAGE:
    TI_DBG5(("satModeSelect6: Informational Exception Control mode page\n"));
    if ( (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_PERF_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT6_TEST_MASK)
         )
    {
      TI_DBG1(("satModeSelect6: return check condition \n"));

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
      return tiSuccess;
    }
    else
    {
      /* sends either ATA SMART ENABLE/DISABLE OPERATIONS based on DEXCPT bit */
      if ( !(pLogPage[StartingIndex + 2] & 0x08) )
      {
        TI_DBG5(("satModeSelect6: enable information exceptions reporting\n"));
        /* sends SMART ENABLE OPERATIONS */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SMART_ENABLE_OPERATIONS;       /* 0xB0 */
        fis->h.features       = 0xD8;                   /* enable */
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
        satIOContext->satCompleteCB = &satModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = sataLLIOStart( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);
        return status;
      }
      else
      {
        TI_DBG5(("satModeSelect6: disable information exceptions reporting\n"));
        /* sends SMART DISABLE OPERATIONS */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SMART_DISABLE_OPERATIONS;       /* 0xB0 */
        fis->h.features       = 0xD9;                   /* disable */
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
        satIOContext->satCompleteCB = &satModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = sataLLIOStart( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);
        return status;

      }
    }
    break;
  default:
    TI_DBG1(("satModeSelect6: Error unknown page code 0x%x\n", pLogPage[12]));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    return tiSuccess;
  }

}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satModeSelect6n10_1.
 *
 *  This function is part of implementation of ModeSelect6 and ModeSelect10.
 *  When ModeSelect6 or ModeSelect10 is coverted into multiple ATA commands,
 *  this function is used.
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
GLOBAL bit32  satModeSelect6n10_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /* sends either ATA SET FEATURES based on DRA bit */
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pLogPage;    /* Log Page data buffer */
  bit32                     StartingIndex = 0;

  fis           = satIOContext->pFis;
  pLogPage      = (bit8 *) tiScsiRequest->sglVirtualAddr;
  TI_DBG5(("satModeSelect6_1: start\n"));
  /* checking Block Descriptor Length on Mode parameter header(6)*/
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
    TI_DBG5(("satModeSelect6_1: enable read look-ahead feature\n"));
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
    satIOContext->satCompleteCB = &satModeSelect6n10CB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);
    return status;
  }
  else
  {
    TI_DBG5(("satModeSelect6_1: disable read look-ahead feature\n"));
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
    satIOContext->satCompleteCB = &satModeSelect6n10CB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);
    return status;
  }

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satModeSelect10.
 *
 *  SAT implementation for SCSI satModeSelect10.
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
GLOBAL bit32  satModeSelect10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pLogPage;    /* Log Page data buffer */
  bit16                     BlkDescLen = 0;     /* Block Descriptor Length */
  bit32                     StartingIndex = 0;
  bit8                      PageCode = 0;
  bit32                     chkCnd = agFALSE;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pLogPage      = (bit8 *) tiScsiRequest->sglVirtualAddr;

  TI_DBG5(("satModeSelect10: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satModeSelect10: return control\n"));
    return tiSuccess;
  }

  /* checking PF bit */
  if ( !(scsiCmnd->cdb[1] & SCSI_MODE_SELECT10_PF_MASK))
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

    TI_DBG1(("satModeSelect10: PF bit check \n"));
    return tiSuccess;

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
    /*
      mode parameter block descriptor does not exist
      */
    PageCode = (bit8)(pLogPage[8] & 0x3F); /* page code and index is 8 + 0 */
    StartingIndex = 8;
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satIOContext->interruptContext);
    return tiSuccess;
  }
  else
  {
    TI_DBG1(("satModeSelect10: return mode parameter block descriptor 0x%x\n",  BlkDescLen));
    /* no more than one mode parameter block descriptor shall be supported */
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    return tiSuccess;
  }
  /*
    for debugging only
  */
  if (StartingIndex == 8)
  {
    tdhexdump("startingindex 8", (bit8 *)pLogPage, 8);
  }
  else if(StartingIndex == 16)
  {
    if (PageCode == MODESELECT_CACHING)
    {
      tdhexdump("startingindex 16", (bit8 *)pLogPage, 16+20);
    }
    else
    {
      tdhexdump("startingindex 16", (bit8 *)pLogPage, 16+12);
    }
  }
  else
  {
    if (PageCode == MODESELECT_CACHING)
    {
      tdhexdump("startingindex 24", (bit8 *)pLogPage, 24+20);
    }
    else
    {
      tdhexdump("startingindex 24", (bit8 *)pLogPage, 24+12);
    }
  }
  switch (PageCode) /* page code */
  {
  case MODESELECT_CONTROL_PAGE:
    TI_DBG5(("satModeSelect10: Control mode page\n"));
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

      TI_DBG1(("satModeSelect10: unexpected values\n"));
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
    return tiSuccess;
    break;
  case MODESELECT_READ_WRITE_ERROR_RECOVERY_PAGE:
    TI_DBG5(("satModeSelect10: Read-Write Error Recovery mode page\n"));
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
      TI_DBG1(("satModeSelect10: return check condition \n"));

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
      return tiSuccess;
    }
    else
    {
      TI_DBG2(("satModeSelect10: return GOOD \n"));
      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext);
      return tiSuccess;
    }

    break;
  case MODESELECT_CACHING:
    /* SAT rev8 Table67, p69*/
    TI_DBG5(("satModeSelect10: Caching mode page\n"));
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
      TI_DBG1(("satModeSelect10: return check condition \n"));

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
      return tiSuccess;

    }
    else
    {
      /* sends ATA SET FEATURES based on WCE bit */
      if ( !(pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_WCE_MASK) )
      {
        TI_DBG5(("satModeSelect10: disable write cache\n"));
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
        satIOContext->satCompleteCB = &satModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = sataLLIOStart( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);
        return status;
      }
      else
      {
        TI_DBG5(("satModeSelect10: enable write cache\n"));
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
        satIOContext->satCompleteCB = &satModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = sataLLIOStart( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);
        return status;

      }
    }
    break;
  case MODESELECT_INFORMATION_EXCEPTION_CONTROL_PAGE:
    TI_DBG5(("satModeSelect10: Informational Exception Control mode page\n"));
   
    if ( (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_PERF_MASK) ||
         (pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_TEST_MASK)
         )
    {
      TI_DBG1(("satModeSelect10: return check condition \n"));

      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
      return tiSuccess;
    }
    else
    {
      /* sends either ATA SMART ENABLE/DISABLE OPERATIONS based on DEXCPT bit */
      if ( !(pLogPage[StartingIndex + 2] & SCSI_MODE_SELECT10_DEXCPT_MASK) )
      {
        TI_DBG5(("satModeSelect10: enable information exceptions reporting\n"));
        /* sends SMART ENABLE OPERATIONS */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SMART_ENABLE_OPERATIONS;       /* 0xB0 */
        fis->h.features       = 0xD8;                   /* enable */
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
        satIOContext->satCompleteCB = &satModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = sataLLIOStart( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);
        return status;
      }
      else
      {
        TI_DBG5(("satModeSelect10: disable information exceptions reporting\n"));
        /* sends SMART DISABLE OPERATIONS */
        fis->h.fisType        = 0x27;                   /* Reg host to device */
        fis->h.c_pmPort       = 0x80;                   /* C Bit is set */

        fis->h.command        = SAT_SMART_DISABLE_OPERATIONS;       /* 0xB0 */
        fis->h.features       = 0xD9;                   /* disable */
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
        satIOContext->satCompleteCB = &satModeSelect6n10CB;

        /*
         * Prepare SGL and send FIS to LL layer.
         */
        satIOContext->reqType = agRequestType;       /* Save it */

        status = sataLLIOStart( tiRoot,
                                tiIORequest,
                                tiDeviceHandle,
                                tiScsiRequest,
                                satIOContext);
        return status;

      }
    }
    break;
  default:
    TI_DBG1(("satModeSelect10: Error unknown page code 0x%x\n", pLogPage[12]));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_NO_SENSE,
                        0,
                        SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              satIOContext->interruptContext );
    return tiSuccess;
  }

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satSynchronizeCache10.
 *
 *  SAT implementation for SCSI satSynchronizeCache10.
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
GLOBAL bit32  satSynchronizeCache10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satSynchronizeCache10: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satSynchronizeCache10: return control\n"));
    return tiSuccess;
  }

  /* checking IMMED bit */
  if (scsiCmnd->cdb[1] & SCSI_SYNC_CACHE_IMMED_MASK)
  {
    TI_DBG1(("satSynchronizeCache10: GOOD status due to IMMED bit\n"));

    /* return GOOD status first here */
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satIOContext->interruptContext);
  }

  /* sends FLUSH CACHE or FLUSH CACHE EXT */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    TI_DBG5(("satSynchronizeCache10: sends FLUSH CACHE EXT\n"));
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
    TI_DBG5(("satSynchronizeCache10: sends FLUSH CACHE\n"));
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
  satIOContext->satCompleteCB = &satSynchronizeCache10n16CB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);


  return (status);
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satSynchronizeCache16.
 *
 *  SAT implementation for SCSI satSynchronizeCache16.
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
GLOBAL bit32  satSynchronizeCache16(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satSynchronizeCache16: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satSynchronizeCache16: return control\n"));
    return tiSuccess;
  }


  /* checking IMMED bit */
  if (scsiCmnd->cdb[1] & SCSI_SYNC_CACHE_IMMED_MASK)
  {
    TI_DBG1(("satSynchronizeCache16: GOOD status due to IMMED bit\n"));

    /* return GOOD status first here */
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satIOContext->interruptContext);
  }

  /* sends FLUSH CACHE or FLUSH CACHE EXT */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    TI_DBG5(("satSynchronizeCache16: sends FLUSH CACHE EXT\n"));
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
    TI_DBG5(("satSynchronizeCache16: sends FLUSH CACHE\n"));
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
  satIOContext->satCompleteCB = &satSynchronizeCache10n16CB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);


  return (status);
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteAndVerify10.
 *
 *  SAT implementation for SCSI satWriteAndVerify10.
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
GLOBAL bit32  satWriteAndVerify10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    combination of write10 and verify10
  */

  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satWriteAndVerify10: start\n"));


  /* checking BYTCHK bit */
  if (scsiCmnd->cdb[1] & SCSI_WRITE_N_VERIFY_BYTCHK_MASK)
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

    TI_DBG1(("satWriteAndVerify10: BYTCHK bit checking \n"));
    return tiSuccess;
  }


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satWriteAndVerify10: return control\n"));
    return tiSuccess;
  }

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */

  TL[0] = 0;
  TL[1] = 0;
  TL[2] = scsiCmnd->cdb[7];  /* MSB */
  TL[3] = scsiCmnd->cdb[8];  /* LSB */

  rangeChk = satAddNComparebit32(LBA, TL);

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
    if (lba > SAT_TR_LBA_LIMIT - 1)
    {
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    TI_DBG1(("satWriteAndVerify10: return LBA out of range\n"));
    return tiSuccess;
    }

    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satWrite10: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }


  /* case 1 and 2 */
  if (!rangeChk) //  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* can't fit the transfer length */
      TI_DBG5(("satWriteAndVerify10: case 2 !!!\n"));
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
      TI_DBG5(("satWriteAndVerify10: case 1 !!!\n"));
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

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      TI_DBG5(("satWriteAndVerify10: case 3\n"));
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
      TI_DBG5(("satWriteAndVerify10: case 4\n"));
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
      TI_DBG5(("satWriteAndVerify10: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }
    TI_DBG5(("satWriteAndVerify10: case 5\n"));

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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUED */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    TI_DBG5(("satWriteAndVerify10: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedWriteNVerifyCB;
  }
  else
  {
    TI_DBG1(("satWriteAndVerify10: CHAINED data\n"));
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
    satIOContext->satCompleteCB = &satChainedWriteNVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);

}






#ifdef REMOVED
GLOBAL bit32  satWriteAndVerify10(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    combination of write10 and verify10
  */

  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satWriteAndVerify10: start\n"));


  /* checking BYTCHK bit */
  if (scsiCmnd->cdb[1] & SCSI_WRITE_N_VERIFY_BYTCHK_MASK)
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

    TI_DBG1(("satWriteAndVerify10: BYTCHK bit checking \n"));
    return tiSuccess;
  }


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satWriteAndVerify10: return control\n"));
    return tiSuccess;
  }

  /* let's do write10 */
  if ( pSatDevData->sat48BitSupport != agTRUE )
  {
    /*
      writeandverify10 but no support for 48 bit addressing -> problem in transfer
      length(sector count)
    */
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

    TI_DBG1(("satWriteAndVerify10: return internal checking\n"));
    return tiSuccess;
  }

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
    if (lba > SAT_TR_LBA_LIMIT - 1)
    {
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    TI_DBG1(("satWriteAndVerify10: return LBA out of range\n"));
    return tiSuccess;
    }
  }


  /* case 1 and 2 */
  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* can't fit the transfer length */
      TI_DBG5(("satWriteAndVerify10: case 2 !!!\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_DMA;          /* 0xCA */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (0x4 << 4) | (scsiCmnd->cdb[2] & 0xF);

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
      TI_DBG5(("satWriteAndVerify10: case 1 !!!\n"));
      fis->h.fisType        = 0x27;                   /* Reg host to device */
      fis->h.c_pmPort       = 0x80;                   /* C bit is set       */
      fis->h.command        = SAT_WRITE_SECTORS;      /* 0x30 */
      fis->h.features       = 0;                      /* FIS reserve */
      fis->d.lbaLow         = scsiCmnd->cdb[5];       /* FIS LBA (7 :0 ) */
      fis->d.lbaMid         = scsiCmnd->cdb[4];       /* FIS LBA (15:8 ) */
      fis->d.lbaHigh        = scsiCmnd->cdb[3];       /* FIS LBA (23:16) */

      /* FIS LBA mode set LBA (27:24) */
      fis->d.device         = (0x4 << 4) | (scsiCmnd->cdb[2] & 0xF);

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

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      TI_DBG5(("satWriteAndVerify10: case 3\n"));
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
    }
    else
    {
      /* case 4 */
      /* WRITE MULTIPLE EXT or WRITE MULTIPLE FUA EXT or WRITE SECTOR(S) EXT */
      /* WRITE SECTORS EXT for easier implemetation */
      TI_DBG5(("satWriteAndVerify10: case 4\n"));
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
    }
  }
  /* case 5 */
  if (pSatDevData->satNCQ == agTRUE)
  {
    /* WRITE FPDMA QUEUED */
    if (pSatDevData->sat48BitSupport != agTRUE)
    {
      TI_DBG5(("satWriteAndVerify10: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }
    TI_DBG5(("satWriteAndVerify10: case 5\n"));

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
  }

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &satWriteAndVerify10CB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);

}
#endif /* REMOVED */

#ifdef REMOVED
/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteAndVerify10_1.
 *
 *  SAT implementation for SCSI satWriteAndVerify10_1.
 *  Sub function of satWriteAndVerify10
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
GLOBAL bit32  satWriteAndVerify10_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satWriteAndVerify10_1: start\n"));

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
    satIOContext->satCompleteCB = &satWriteAndVerify10CB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);


    TI_DBG1(("satWriteAndVerify10_1: return status %d\n", status));
    return (status);
  }
  else
  {
    /* can't fit in SAT_READ_VERIFY_SECTORS becasue of Sector Count and LBA */
    TI_DBG1(("satWriteAndVerify10_1: can't fit in SAT_READ_VERIFY_SECTORS\n"));
    return tiError;
  }


  return tiSuccess;
}
#endif /* REMOVED */

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteAndVerify12.
 *
 *  SAT implementation for SCSI satWriteAndVerify12.
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
GLOBAL bit32  satWriteAndVerify12(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    combination of write12 and verify12
    temp: since write12 is not support (due to internal checking), no support
  */
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satWriteAndVerify12: start\n"));

  /* checking BYTCHK bit */
  if (scsiCmnd->cdb[1] & SCSI_WRITE_N_VERIFY_BYTCHK_MASK)
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

    TI_DBG1(("satWriteAndVerify12: BYTCHK bit checking \n"));
    return tiSuccess;
  }

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satWriteAndVerify12: return control\n"));
    return tiSuccess;
  }

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */

  TL[0] = scsiCmnd->cdb[6];   /* MSB */
  TL[1] = scsiCmnd->cdb[7];
  TL[2] = scsiCmnd->cdb[7];
  TL[3] = scsiCmnd->cdb[8];   /* LSB */

  rangeChk = satAddNComparebit32(LBA, TL);

  lba = satComputeCDB12LBA(satIOContext);
  tl = satComputeCDB12TL(satIOContext);


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
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    TI_DBG1(("satWriteAndVerify12: return LBA out of range, not EXT\n"));
    return tiSuccess;
    }

    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satWriteAndVerify12: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }

  /* case 1 and 2 */
  if (!rangeChk) //  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* In case that we can't fit the transfer length, we loop */
      TI_DBG5(("satWriteAndVerify12: case 2\n"));
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
      TI_DBG5(("satWriteAndVerify12: case 1\n"));
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
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      TI_DBG5(("satWriteAndVerify12: case 3\n"));
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
      TI_DBG5(("satWriteAndVerify12: case 4\n"));
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
      TI_DBG5(("satWriteAndVerify12: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }
    TI_DBG6(("satWriteAndVerify12: case 5\n"));

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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;
  satIOContext->LoopNum2 = LoopNum;


  if (LoopNum == 1)
  {
    TI_DBG5(("satWriteAndVerify12: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedWriteNVerifyCB;
  }
  else
  {
    TI_DBG1(("satWriteAndVerify12: CHAINED data\n"));
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
    satIOContext->satCompleteCB = &satChainedWriteNVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}

GLOBAL bit32  satNonChainedWriteNVerify_Verify(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satNonChainedWriteNVerify_Verify: start\n"));

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
    satIOContext->satCompleteCB = &satNonChainedWriteNVerifyCB;

    /*
     * Prepare SGL and send FIS to LL layer.
     */
    satIOContext->reqType = agRequestType;       /* Save it */

    status = sataLLIOStart( tiRoot,
                            tiIORequest,
                            tiDeviceHandle,
                            tiScsiRequest,
                            satIOContext);


    TI_DBG1(("satNonChainedWriteNVerify_Verify: return status %d\n", status));
    return (status);
  }
  else
  {
    /* can't fit in SAT_READ_VERIFY_SECTORS becasue of Sector Count and LBA */
    TI_DBG1(("satNonChainedWriteNVerify_Verify: can't fit in SAT_READ_VERIFY_SECTORS\n"));
    return tiError;
  }

}

GLOBAL bit32  satChainedWriteNVerify_Write(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    Assumption: error check on lba and tl has been done in satWrite*()
    lba = lba + tl;
  */
  bit32                     status;
  satIOContext_t            *satOrgIOContext = agNULL;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  TI_DBG1(("satChainedWriteNVerify_Write: start\n"));

  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;
  scsiCmnd        = satOrgIOContext->pScsiCmnd;

  osti_memset(LBA,0, sizeof(LBA));

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
    TI_DBG1(("satChainedWriteNVerify_Write: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
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
    TI_DBG1(("satChainedWriteNVerify_Write: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
    return tiError;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &satChainedWriteNVerifyCB;


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satChainedWriteNVerify_Write: return\n"));
  return (status);

}

/*
  similar to write12 and verify10;
  this will be similar to verify12
  */
GLOBAL bit32  satChainedWriteNVerify_Start_Verify(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    deal with transfer length; others have been handled previously at this point;
    no LBA check; no range check;
  */
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  satDeviceData_t           *pSatDevData;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[4];
  bit8                      TL[4];

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;

  TI_DBG5(("satChainedWriteNVerify_Start_Verify: start\n"));

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));

  /* do not use memcpy due to indexing in LBA and TL */
  LBA[0] = scsiCmnd->cdb[2];  /* MSB */
  LBA[1] = scsiCmnd->cdb[3];
  LBA[2] = scsiCmnd->cdb[4];
  LBA[3] = scsiCmnd->cdb[5];  /* LSB */

  TL[0] = scsiCmnd->cdb[6];   /* MSB */
  TL[1] = scsiCmnd->cdb[7];
  TL[2] = scsiCmnd->cdb[7];
  TL[3] = scsiCmnd->cdb[8];   /* LSB */

  lba = satComputeCDB12LBA(satIOContext);
  tl = satComputeCDB12TL(satIOContext);

  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    TI_DBG5(("satChainedWriteNVerify_Start_Verify: SAT_READ_VERIFY_SECTORS_EXT\n"));
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
    TI_DBG5(("satChainedWriteNVerify_Start_Verify: SAT_READ_VERIFY_SECTORS\n"));
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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_READ_VERIFY_SECTORS_EXT)
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    TI_DBG1(("satChainedWriteNVerify_Start_Verify: error case 1!!!\n"));
    LoopNum = 1;
  }

  satIOContext->LoopNum = LoopNum;

  if (LoopNum == 1)
  {
    TI_DBG5(("satChainedWriteNVerify_Start_Verify: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedWriteNVerifyCB;
  }
  else
  {
    TI_DBG1(("satChainedWriteNVerify_Start_Verify: CHAINED data\n"));
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
      TI_DBG1(("satChainedWriteNVerify_Start_Verify: error case 2!!!\n"));
    }

    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satChainedWriteNVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}

GLOBAL bit32  satChainedWriteNVerify_Verify(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  satIOContext_t            *satOrgIOContext = agNULL;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  bit32                     lba = 0;
  bit32                     DenomTL = 0xFF;
  bit32                     Remainder = 0;
  bit8                      LBA[4]; /* 0 MSB, 3 LSB */

  TI_DBG2(("satChainedWriteNVerify_Verify: start\n"));

  fis             = satIOContext->pFis;
  satOrgIOContext = satIOContext->satOrgIOContext;

  osti_memset(LBA,0, sizeof(LBA));

  switch (satOrgIOContext->ATACmd)
  {
  case SAT_READ_VERIFY_SECTORS:
    DenomTL = 0xFF;
    break;
  case SAT_READ_VERIFY_SECTORS_EXT:
    DenomTL = 0xFFFF;
    break;
  default:
    TI_DBG1(("satChainedWriteNVerify_Verify: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
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
    TI_DBG1(("satChainedWriteNVerify_Verify: error incorrect ata command 0x%x\n", satIOContext->ATACmd));
    return tiError;
    break;
  }

  /* Initialize CB for SATA completion.
   */
  /* chained data */
  satIOContext->satCompleteCB = &satChainedWriteNVerifyCB;


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satChainedWriteNVerify_Verify: return\n"));
  return (status);

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteAndVerify16.
 *
 *  SAT implementation for SCSI satWriteAndVerify16.
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
GLOBAL bit32  satWriteAndVerify16(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    combination of write16 and verify16
    since write16 has 8 bytes LBA -> problem ATA LBA(upto 6 bytes), no support
  */
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_DMA_WRITE;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     lba = 0;
  bit32                     tl = 0;
  bit32                     LoopNum = 1;
  bit8                      LBA[8];
  bit8                      TL[8];
  bit32                     rangeChk = agFALSE; /* lba and tl range check */
  bit32                     limitChk = agFALSE; /* lba and tl range check */

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  TI_DBG5(("satWriteAndVerify16:start\n"));

  /* checking BYTCHK bit */
  if (scsiCmnd->cdb[1] & SCSI_WRITE_N_VERIFY_BYTCHK_MASK)
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

    TI_DBG1(("satWriteAndVerify16: BYTCHK bit checking \n"));
    return tiSuccess;
  }


  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[15] & SCSI_NACA_MASK) || (scsiCmnd->cdb[15] & SCSI_LINK_MASK) )
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

    TI_DBG2(("satWriteAndVerify16: return control\n"));
    return tiSuccess;
  }

  osti_memset(LBA, 0, sizeof(LBA));
  osti_memset(TL, 0, sizeof(TL));


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

  rangeChk = satAddNComparebit64(LBA, TL);

  limitChk = satCompareLBALimitbit(LBA);

  lba = satComputeCDB16LBA(satIOContext);
  tl = satComputeCDB16TL(satIOContext);


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
    if (limitChk)
    {
      TI_DBG1(("satWriteAndVerify16: return LBA out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
    if (rangeChk) //    if (lba + tl > SAT_TR_LBA_LIMIT)
    {
      TI_DBG1(("satWriteAndVerify16: return LBA+TL out of range, not EXT\n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
    }
  }


  /* case 1 and 2 */
  if (!rangeChk) //  if (lba + tl <= SAT_TR_LBA_LIMIT)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 2 */
      /* WRITE DMA*/
      /* In case that we can't fit the transfer length, we loop */
      TI_DBG5(("satWriteAndVerify16: case 2\n"));
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
      TI_DBG5(("satWriteAndVerify16: case 1\n"));
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
  }

  /* case 3 and 4 */
  if (pSatDevData->sat48BitSupport == agTRUE)
  {
    if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
    {
      /* case 3 */
      /* WRITE DMA EXT or WRITE DMA FUA EXT */
      TI_DBG5(("satWriteAndVerify16: case 3\n"));
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
      TI_DBG5(("satWriteAndVerify16: case 4\n"));
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
      TI_DBG5(("satWriteAndVerify16: case 5 !!! error NCQ but 28 bit address support \n"));
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
      return tiSuccess;
    }
    TI_DBG6(("satWriteAndVerify16: case 5\n"));

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
    LoopNum = satComputeLoopNum(tl, 0xFF);
  }
  else if (fis->h.command == SAT_WRITE_SECTORS_EXT ||
           fis->h.command == SAT_WRITE_DMA_EXT     ||
           fis->h.command == SAT_WRITE_DMA_FUA_EXT
           )
  {
    /* SAT_READ_SECTORS_EXT, SAT_READ_DMA_EXT */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }
  else
  {
    /* SAT_WRITE_FPDMA_QUEUEDK */
    LoopNum = satComputeLoopNum(tl, 0xFFFF);
  }

  satIOContext->LoopNum = LoopNum;


  if (LoopNum == 1)
  {
    TI_DBG5(("satWriteAndVerify16: NON CHAINED data\n"));
    /* Initialize CB for SATA completion.
     */
    satIOContext->satCompleteCB = &satNonChainedWriteNVerifyCB;
  }
  else
  {
    TI_DBG1(("satWriteAndVerify16: CHAINED data\n"));
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
    satIOContext->satCompleteCB = &satChainedWriteNVerifyCB;
  }


  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  return (status);
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satReadMediaSerialNumber.
 *
 *  SAT implementation for SCSI Read Media Serial Number.
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
GLOBAL bit32  satReadMediaSerialNumber(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t  *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  agsaSATAIdentifyData_t    *pSATAIdData;
  bit8                      *pSerialNumber;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pSATAIdData   = &(pSatDevData->satIdentifyData);
  pSerialNumber = (bit8 *) tiScsiRequest->sglVirtualAddr;


  TI_DBG1(("satReadMediaSerialNumber: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[11] & SCSI_NACA_MASK) || (scsiCmnd->cdb[11] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satReadMediaSerialNumber: return control\n"));
    return tiSuccess;
  }

  if (tiScsiRequest->scsiCmnd.expDataLength == 4)
  {
    if (pSATAIdData->commandSetFeatureDefault & 0x4)
    {
      TI_DBG1(("satReadMediaSerialNumber: Media serial number returning only length\n"));
      /* SPC-3 6.16 p192; filling in length */
      pSerialNumber[0] = 0;
      pSerialNumber[1] = 0;
      pSerialNumber[2] = 0;
      pSerialNumber[3] = 0x3C;
    }
    else
    {
      /* 1 sector - 4 = 512 - 4 to avoid underflow; 0x1fc*/
      pSerialNumber[0] = 0;
      pSerialNumber[1] = 0;
      pSerialNumber[2] = 0x1;
      pSerialNumber[3] = 0xfc;
    }

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_GOOD,
                              agNULL,
                              satIOContext->interruptContext);

    return tiSuccess;
  }

  if ( pSatDevData->IDDeviceValid == agTRUE)
  {
    if (pSATAIdData->commandSetFeatureDefault & 0x4)
    {
      /* word87 bit2 Media serial number is valid */
      /* read word 176 to 205; length is 2*30 = 60 = 0x3C*/
      tdhexdump("ID satReadMediaSerialNumber", (bit8*)pSATAIdData->currentMediaSerialNumber, 2*30);
      /* SPC-3 6.16 p192; filling in length */
      pSerialNumber[0] = 0;
      pSerialNumber[1] = 0;
      pSerialNumber[2] = 0;
      pSerialNumber[3] = 0x3C;
      osti_memcpy(&pSerialNumber[4], (void *)pSATAIdData->currentMediaSerialNumber, 60);
      tdhexdump("satReadMediaSerialNumber", (bit8*)pSerialNumber, 2*30 + 4);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_GOOD,
                                agNULL,
                                satIOContext->interruptContext);
      return tiSuccess;


    }
    else
    {
     /* word87 bit2 Media serial number is NOT valid */
      TI_DBG1(("satReadMediaSerialNumber: Media serial number is NOT valid \n"));

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
      satIOContext->satCompleteCB = &satReadMediaSerialNumberCB;
      satIOContext->reqType = agRequestType;       /* Save it */
      status = sataLLIOStart( tiRoot,
                             tiIORequest,
                             tiDeviceHandle,
                             tiScsiRequest,
                             satIOContext);

      return status;
    }
  }
  else
  {
     /* temporary failure */
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              satIOContext->interruptContext);

    return tiSuccess;

  }

}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satReadBuffer.
 *
 *  SAT implementation for SCSI Read Buffer.
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
/* SAT-2, Revision 00*/
GLOBAL bit32  satReadBuffer(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  bit32                     status = tiSuccess;
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit32                     bufferOffset;
  bit32                     tl;
  bit8                      mode;
  bit8                      bufferID;
  bit8                      *pBuff;

  pSense        = satIOContext->pSense;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pBuff         = (bit8 *) tiScsiRequest->sglVirtualAddr;

  TI_DBG2(("satReadBuffer: start\n"));
  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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
    TI_DBG1(("satReadBuffer: return control\n"));
    return tiSuccess;
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
      satIOContext->satCompleteCB = &satReadBufferCB;
      satIOContext->reqType = agRequestType;       /* Save it */

      status = sataLLIOStart( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);
      return status;
    }
    if (bufferID == 0 && bufferOffset == 0 && tl != 512)
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
      TI_DBG1(("satReadBuffer: allocation length is not 512; it is %d\n", tl));
      return tiSuccess;
    }
    if (bufferID == 0 && bufferOffset != 0)
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
      TI_DBG1(("satReadBuffer: buffer offset is not 0; it is %d\n", bufferOffset));
      return tiSuccess;
    }
    /* all other cases unsupported */
    TI_DBG1(("satReadBuffer: unsupported case 1\n"));
    satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
    return tiSuccess;
  }
  else if (mode == READ_BUFFER_DESCRIPTOR_MODE) /* 3 */
  {
    if (tl < READ_BUFFER_DESCRIPTOR_MODE_DATA_LEN) /* 4 */
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
      TI_DBG1(("satReadBuffer: tl < 4; tl is %d\n", tl));
      return tiSuccess;
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
        TI_DBG1(("satReadBuffer: underrun tl %d data %d\n", tl, READ_BUFFER_DESCRIPTOR_MODE_DATA_LEN));
        ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOUnderRun,
                                tl - READ_BUFFER_DESCRIPTOR_MODE_DATA_LEN,
                                agNULL,
                                satIOContext->interruptContext );
        return tiSuccess;
      }
      else
      {
        ostiInitiatorIOCompleted( tiRoot,
                                  tiIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satIOContext->interruptContext);
        return tiSuccess;
      }
    }
    else
    {
      /* We don't support other than bufferID 0 */
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
      return tiSuccess;
    }
  }
  else
  {
    /* We don't support any other mode */
    TI_DBG1(("satReadBuffer: unsupported mode %d\n", mode));
    satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
    return tiSuccess;
  }
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satWriteBuffer.
 *
 *  SAT implementation for SCSI Write Buffer.
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
/* SAT-2, Revision 00*/
GLOBAL bit32  satWriteBuffer(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
#ifdef NOT_YET
  bit32                     agRequestType = AGSA_SATA_PROTOCOL_PIO_READ;
#endif
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  bit32                     bufferOffset;
  bit32                     parmLen;
  bit8                      mode;
  bit8                      bufferID;
  bit8                      *pBuff;

  pSense        = satIOContext->pSense;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  pBuff         = (bit8 *) tiScsiRequest->sglVirtualAddr;

  TI_DBG2(("satWriteBuffer: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[9] & SCSI_NACA_MASK) || (scsiCmnd->cdb[9] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satWriteBuffer: return control\n"));
    return tiSuccess;
  }

  bufferOffset = (scsiCmnd->cdb[3] << (8*2)) + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];
  parmLen = (scsiCmnd->cdb[6] << (8*2)) + (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];

  mode = (bit8)(scsiCmnd->cdb[1] & SCSI_READ_BUFFER_MODE_MASK);
  bufferID = scsiCmnd->cdb[2];

  /* for debugging only */
  tdhexdump("satWriteBuffer pBuff", (bit8 *)pBuff, 24);

  if (mode == WRITE_BUFFER_DATA_MODE) /* 2 */
  {
    if (bufferID == 0 && bufferOffset == 0 && parmLen == 512)
    {
      TI_DBG1(("satWriteBuffer: sending ATA WRITE BUFFER\n"));
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

      satIOContext->satCompleteCB = &satWriteBufferCB;

      satIOContext->reqType = agRequestType;       /* Save it */

      status = sataLLIOStart( tiRoot,
                              tiIORequest,
                              tiDeviceHandle,
                              tiScsiRequest,
                              satIOContext);
      return status;
#endif
      /* temp */
      ostiInitiatorIOCompleted( tiRoot,
                                  tiIORequest,
                                  tiIOSuccess,
                                  SCSI_STAT_GOOD,
                                  agNULL,
                                  satIOContext->interruptContext);
      return tiSuccess;
    }
    if ( (bufferID == 0 && bufferOffset != 0) ||
         (bufferID == 0 && parmLen != 512)
        )
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

      TI_DBG1(("satWriteBuffer: wrong buffer offset %d or parameter length parmLen %d\n", bufferOffset, parmLen));
      return tiSuccess;
    }

    /* all other cases unsupported */
    TI_DBG1(("satWriteBuffer: unsupported case 1\n"));
    satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;

  }
  else if (mode == WRITE_BUFFER_DL_MICROCODE_SAVE_MODE) /* 5 */
  {
    TI_DBG1(("satWriteBuffer: not yet supported mode %d\n", mode));
    satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
  }
  else
  {
    /* We don't support any other mode */
    TI_DBG1(("satWriteBuffer: unsupported mode %d\n", mode));
    satSetSensePayload( pSense,
                          SCSI_SNSKEY_ILLEGAL_REQUEST,
                          0,
                          SCSI_SNSCODE_INVALID_COMMAND,
                          satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );

    return tiSuccess;
  }

}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satReassignBlocks.
 *
 *  SAT implementation for SCSI Reassign Blocks.
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
GLOBAL bit32  satReassignBlocks(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)
{
  /*
    assumes all LBA fits in ATA command; no boundary condition is checked here yet
  */
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pParmList;    /* Log Page data buffer */
  bit8                      LongLBA;
  bit8                      LongList;
  bit32                     defectListLen;
  bit8                      LBA[8];
  bit32                     startingIndex;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pParmList     = (bit8 *) tiScsiRequest->sglVirtualAddr;

  TI_DBG5(("satReassignBlocks: start\n"));

  /* checking CONTROL */
  /* NACA == 1 or LINK == 1*/
  if ( (scsiCmnd->cdb[5] & SCSI_NACA_MASK) || (scsiCmnd->cdb[5] & SCSI_LINK_MASK) )
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

    TI_DBG1(("satReassignBlocks: return control\n"));
    return tiSuccess;
  }

  osti_memset(satIOContext->LBA, 0, 8);
  satIOContext->ParmIndex = 0;
  satIOContext->ParmLen = 0;

  LongList = (bit8)(scsiCmnd->cdb[1] & SCSI_REASSIGN_BLOCKS_LONGLIST_MASK);
  LongLBA = (bit8)(scsiCmnd->cdb[1] & SCSI_REASSIGN_BLOCKS_LONGLBA_MASK);
  osti_memset(LBA, 0, sizeof(LBA));

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

  tdhexdump("satReassignBlocks Parameter list", (bit8 *)pParmList, 4 + defectListLen);

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

  osti_memcpy(satIOContext->LBA, LBA, 8);
  satIOContext->ParmIndex = startingIndex;

  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;

  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &satReassignBlocksCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satReassignBlocks_1.
 *
 *  SAT implementation for SCSI Reassign Blocks. This is helper function for
 *  satReassignBlocks and satReassignBlocksCB. This sends ATA verify command.
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
/* next LBA; sends READ VERIFY SECTOR; update LBA and ParmIdx */
GLOBAL bit32  satReassignBlocks_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
                   satIOContext_t            *satOrgIOContext
                   )
{
  /*
    assumes all LBA fits in ATA command; no boundary condition is checked here yet
    tiScsiRequest is OS generated; needs for accessing parameter list
  */
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  tiIniScsiCmnd_t           *scsiCmnd;
  agsaFisRegHostToDevice_t  *fis;
  bit8                      *pParmList;    /* Log Page data buffer */
  bit8                      LongLBA;
  bit8                      LBA[8];
  bit32                     startingIndex;

  pSatDevData   = satIOContext->pSatDevData;
  scsiCmnd      = &tiScsiRequest->scsiCmnd;
  fis           = satIOContext->pFis;
  pParmList     = (bit8 *) tiScsiRequest->sglVirtualAddr;

  TI_DBG5(("satReassignBlocks_1: start\n"));

  LongLBA = (bit8)(scsiCmnd->cdb[1] & SCSI_REASSIGN_BLOCKS_LONGLBA_MASK);
  osti_memset(LBA, 0, sizeof(LBA));

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
  osti_memcpy(satOrgIOContext->LBA, LBA, 8);
  satOrgIOContext->ParmIndex = startingIndex;
  agRequestType = AGSA_SATA_PROTOCOL_NON_DATA;
  /* Initialize CB for SATA completion.
   */
  satIOContext->satCompleteCB = &satReassignBlocksCB;
  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  sataLLIOStart( tiRoot,
                 tiIORequest,
                 tiDeviceHandle,
                 tiScsiRequest,
                 satIOContext );
  return tiSuccess;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satReassignBlocks_2.
 *
 *  SAT implementation for SCSI Reassign Blocks. This is helper function for
 *  satReassignBlocks and satReassignBlocksCB. This sends ATA write command.
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *  \param   LBA:              Pointer to the LBA to be processed
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     I/O request successfully initiated.
 *    - \e tiBusy:        No resources available, try again later.
 *    - \e tiIONoDevice:  Invalid device handle.
 *    - \e tiError:       Other errors.
 */
/*****************************************************************************/
/* current LBA; sends WRITE */
GLOBAL bit32  satReassignBlocks_2(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
                   bit8                      *LBA
                   )
{
  /*
    assumes all LBA fits in ATA command; no boundary condition is checked here yet
    tiScsiRequest is TD generated for writing
  */
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
  agsaFisRegHostToDevice_t  *fis;

  pSense        = satIOContext->pSense;
  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;

  if (pSatDevData->satDMASupport == agTRUE && pSatDevData->satDMAEnabled == agTRUE)
  {
    /* case 2 */
    /* WRITE DMA*/
    /* can't fit the transfer length */
    TI_DBG5(("satReassignBlocks_2: case 2\n"));
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
    TI_DBG5(("satReassignBlocks_2: case 1\n"));
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
      TI_DBG5(("satReassignBlocks_2: case 3\n"));
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
      TI_DBG5(("satReassignBlocks_2: case 4\n"));
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
      TI_DBG5(("satReassignBlocks_2: case 5 !!! error NCQ but 28 bit address support \n"));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_HARDWARE_ERROR,
                          0,
                          SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED,
                          satIOContext);

      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                tiIOSuccess,
                                SCSI_STAT_CHECK_CONDITION,
                                satIOContext->pTiSenseData,
                                satIOContext->interruptContext );
      return tiSuccess;
    }
    TI_DBG6(("satWrite10: case 5\n"));

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

  satIOContext->satCompleteCB = &satReassignBlocksCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          /* not the original, should be the TD generated one */
                          tiScsiRequest,
                          satIOContext);
  return (status);
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI satPrepareNewIO.
 *
 *  This function fills in the fields of internal IO generated by TD layer.
 *  This is mostly used in the callback functions.
 *
 *  \param   satNewIntIo:      Pointer to the internal IO structure.
 *  \param   tiOrgIORequest:   Pointer to the original tiIOrequest sent by OS layer
 *  \param   satDevData:       Pointer to the device data.
 *  \param   scsiCmnd:         Pointer to SCSI command.
 *  \param   satOrgIOContext:  Pointer to the original SAT IO Context
 *
 *  \return
 *    - \e Pointer to the new SAT IO Context
 */
/*****************************************************************************/
GLOBAL satIOContext_t *satPrepareNewIO(
                            satInternalIo_t         *satNewIntIo,
                            tiIORequest_t           *tiOrgIORequest,
                            satDeviceData_t         *satDevData,
                            tiIniScsiCmnd_t         *scsiCmnd,
                            satIOContext_t          *satOrgIOContext
                            )
{
  satIOContext_t          *satNewIOContext;
  tdIORequestBody_t       *tdNewIORequestBody;

  TI_DBG2(("satPrepareNewIO: start\n"));

  /* the one to be used; good 8/2/07 */
  satNewIntIo->satOrgTiIORequest = tiOrgIORequest; /* this is already done in
                                                        satAllocIntIoResource() */

  tdNewIORequestBody = (tdIORequestBody_t *)satNewIntIo->satIntRequestBody;
  satNewIOContext = &(tdNewIORequestBody->transport.SATA.satIOContext);

  satNewIOContext->pSatDevData   = satDevData;
  satNewIOContext->pFis          = &(tdNewIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satNewIOContext->pScsiCmnd     = &(satNewIntIo->satIntTiScsiXchg.scsiCmnd);
  if (scsiCmnd != agNULL)
  {
    /* saves only CBD; not scsi command for LBA and number of blocks */
    osti_memcpy(satNewIOContext->pScsiCmnd->cdb, scsiCmnd->cdb, 16);
  }
  satNewIOContext->pSense        = &(tdNewIORequestBody->transport.SATA.sensePayload);
  satNewIOContext->pTiSenseData  = &(tdNewIORequestBody->transport.SATA.tiSenseData);
  satNewIOContext->pTiSenseData->senseData = satNewIOContext->pSense;
  satNewIOContext->tiRequestBody = satNewIntIo->satIntRequestBody;
  satNewIOContext->interruptContext = satNewIOContext->interruptContext;
  satNewIOContext->satIntIoContext  = satNewIntIo;
  satNewIOContext->ptiDeviceHandle = satOrgIOContext->ptiDeviceHandle;
  satNewIOContext->satOrgIOContext = satOrgIOContext;
  /* saves tiScsiXchg; only for writesame10() */
  satNewIOContext->tiScsiXchg = satOrgIOContext->tiScsiXchg;

  return satNewIOContext;
}
/*****************************************************************************
 *! \brief  satIOAbort
 *
 *   This routine is called to initiate a I/O abort to SATL.
 *   This routine is independent of HW/LL API.
 *
 *  \param  tiRoot:     Pointer to TISA initiator driver/port instance.
 *  \param  taskTag:    Pointer to TISA I/O request context/tag to be aborted.
 *
 *  \return:
 *
 *  \e tiSuccess:     I/O request successfully initiated.
 *  \e tiBusy:        No resources available, try again later.
 *  \e tiError:       Other errors that prevent the I/O request to be started.
 *
 *
 *****************************************************************************/
GLOBAL bit32 satIOAbort(
                          tiRoot_t      *tiRoot,
                          tiIORequest_t *taskTag )
{

  tdsaRoot_t          *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t       *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t          *agRoot;
  tdIORequestBody_t   *tdIORequestBody;
  tdIORequestBody_t   *tdIONewRequestBody;
  agsaIORequest_t     *agIORequest;
  bit32               status;
  agsaIORequest_t     *agAbortIORequest;
  tdIORequestBody_t   *tdAbortIORequestBody;
  bit32               PhysUpper32;
  bit32               PhysLower32;
  bit32               memAllocStatus;
  void                *osMemHandle;
  satIOContext_t      *satIOContext;
  satInternalIo_t     *satIntIo;

  TI_DBG2(("satIOAbort: start\n"));

  agRoot          = &(tdsaAllShared->agRootNonInt);
  tdIORequestBody = (tdIORequestBody_t *)taskTag->tdData;

  /* needs to distinguish internally generated or externally generated */
  satIOContext = &(tdIORequestBody->transport.SATA.satIOContext);
  satIntIo     = satIOContext->satIntIoContext;
  if (satIntIo == agNULL)
  {
    TI_DBG1(("satIOAbort: External, OS generated\n"));
    agIORequest     = &(tdIORequestBody->agIORequest);
  }
  else
  {
    TI_DBG1(("satIOAbort: Internal, TD generated\n"));
    tdIONewRequestBody = (tdIORequestBody_t *)satIntIo->satIntRequestBody;
    agIORequest     = &(tdIONewRequestBody->agIORequest);
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
    TI_DBG1(("satIOAbort: ostiAllocMemory failed...\n"));
    return tiError;
  }

  if (tdAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    TI_DBG1(("satIOAbort: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
    return tiError;
  }

  /* setup task management structure */
  tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  tdAbortIORequestBody->tiDevHandle = tdIORequestBody->tiDevHandle;

  /* initialize agIORequest */
  agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) tdAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

  /* remember IO to be aborted */
  tdAbortIORequestBody->tiIOToBeAbortedRequest = taskTag;

  status = saSATAAbort( agRoot, agAbortIORequest, 0, agNULL, 0, agIORequest, agNULL );

  TI_DBG5(("satIOAbort: return status=0x%x\n", status));

  if (status == AGSA_RC_SUCCESS)
    return tiSuccess;
  else
    return tiError;

}


/*****************************************************************************
 *! \brief  satTM
 *
 *   This routine is called to initiate a TM request to SATL.
 *   This routine is independent of HW/LL API.
 *
 *  \param  tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param  tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param  task:             SAM-3 task management request.
 *  \param  lun:              Pointer to LUN.
 *  \param  taskTag:          Pointer to the associated task where the TM
 *                            command is to be applied.
 *  \param  currentTaskTag:   Pointer to tag/context for this TM request.
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
 /* save task in satIOContext */
osGLOBAL bit32 satTM(
                        tiRoot_t          *tiRoot,
                        tiDeviceHandle_t  *tiDeviceHandle,
                        bit32             task,
                        tiLUN_t           *lun,
                        tiIORequest_t     *taskTag,
                        tiIORequest_t     *currentTaskTag,
                        tdIORequestBody_t *tiRequestBody,
                        bit32              NotifyOS
                        )
{
  tdIORequestBody_t           *tdIORequestBody = agNULL;
  satIOContext_t              *satIOContext = agNULL;
  tdsaDeviceData_t            *oneDeviceData = agNULL;
  bit32                       status;

  TI_DBG3(("satTM: tiDeviceHandle=%p task=0x%x\n", tiDeviceHandle, task ));

  /* set satIOContext fields and etc */
  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;


  tdIORequestBody = (tdIORequestBody_t *)tiRequestBody;
  satIOContext = &(tdIORequestBody->transport.SATA.satIOContext);

  satIOContext->pSatDevData   = &oneDeviceData->satDevData;
  satIOContext->pFis          =
    &tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev;


  satIOContext->tiRequestBody = tiRequestBody;
  satIOContext->ptiDeviceHandle = tiDeviceHandle;
  satIOContext->satIntIoContext  = agNULL;
  satIOContext->satOrgIOContext  = agNULL;

  /* followings are used only for internal IO */
  satIOContext->currentLBA = 0;
  satIOContext->OrgTL = 0;

  /* saving task in satIOContext */
  satIOContext->TMF = task;

  satIOContext->satToBeAbortedIOContext = agNULL;

  if (NotifyOS == agTRUE)
  {
    satIOContext->NotifyOS = agTRUE;
  }
  else
  {
    satIOContext->NotifyOS = agFALSE;
  }
  /*
   * Our SAT supports RESET LUN and partially support ABORT TASK (only if there
   * is no more than one I/O pending on the drive.
   */

  if (task == AG_LOGICAL_UNIT_RESET)
  {
    status = satTmResetLUN( tiRoot,
                            currentTaskTag,
                            tiDeviceHandle,
                            agNULL,
                            satIOContext,
                            lun);
    return status;
  }
#ifdef TO_BE_REMOVED
  else if (task == AG_TARGET_WARM_RESET)
  {
    status = satTmWarmReset( tiRoot,
                             currentTaskTag,
                             tiDeviceHandle,
                             agNULL,
                             satIOContext);

    return status;
  }
#endif
  else if (task == AG_ABORT_TASK)
  {
    status = satTmAbortTask( tiRoot,
                             currentTaskTag,
                             tiDeviceHandle,
                             agNULL,
                             satIOContext,
                             taskTag);

    return status;
  }
  else if (task == TD_INTERNAL_TM_RESET)
  {
    status = satTDInternalTmReset( tiRoot,
                                   currentTaskTag,
                                   tiDeviceHandle,
                                   agNULL,
                                   satIOContext);
   return status;
  }
  else
  {
    TI_DBG1(("satTM: tiDeviceHandle=%p UNSUPPORTED TM task=0x%x\n",
        tiDeviceHandle, task ));

    /* clean up TD layer's IORequestBody */
    ostiFreeMemory(
                   tiRoot,
                   tiRequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return tiError;
  }

}


/*****************************************************************************
 *! \brief  satTmResetLUN
 *
 *   This routine is called to initiate a TM RESET LUN request to SATL.
 *   This routine is independent of HW/LL API.
 *
 *  \param  tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param  tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param  lun:              Pointer to LUN.
 *  \param  currentTaskTag:   Pointer to tag/context for this TM request.
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
osGLOBAL bit32 satTmResetLUN(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest, /* current task tag */
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext,
                            tiLUN_t                   *lun)
{

  tdsaDeviceData_t        *tdsaDeviceData;
  satDeviceData_t         *satDevData;

  tdsaDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  satDevData      = &tdsaDeviceData->satDevData;

  TI_DBG1(("satTmResetLUN: tiDeviceHandle=%p.\n", tiDeviceHandle ));

  /*
   * Only support LUN 0
   */
  if ( (lun->lun[0] | lun->lun[1] | lun->lun[2] | lun->lun[3] |
        lun->lun[4] | lun->lun[5] | lun->lun[6] | lun->lun[7] ) != 0 )
  {
    TI_DBG1(("satTmResetLUN: *** REJECT *** LUN not zero, tiDeviceHandle=%p\n",
                tiDeviceHandle));
    return tiError;
  }

  /*
   * Check if there is other TM request pending
   */
  if (satDevData->satTmTaskTag != agNULL)
  {
    TI_DBG1(("satTmResetLUN: *** REJECT *** other TM pending, tiDeviceHandle=%p\n",
                tiDeviceHandle));
    return tiError;
  }

  /*
   * Save tiIORequest, will be returned at device reset completion to return
   * the TM completion.
   */
   satDevData->satTmTaskTag = tiIORequest;

  /*
   * Set flag to indicate device in recovery mode.
   */
  satDevData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;

  /*
   * Issue SATA device reset. Set flag to indicate NOT to automatically abort
   * at the completion of SATA device reset.
   */
  satDevData->satAbortAfterReset = agFALSE;

  /* SAT rev8 6.3.6 p22 */
  satStartResetDevice(
                      tiRoot,
                      tiIORequest, /* currentTaskTag */
                      tiDeviceHandle,
                      tiScsiRequest,
                      satIOContext
                      );


  return tiSuccess;

}

/*****************************************************************************
 *! \brief  satTmWarmReset
 *
 *   This routine is called to initiate a TM warm RESET request to SATL.
 *   This routine is independent of HW/LL API.
 *
 *  \param  tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param  tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param  currentTaskTag:   Pointer to tag/context for this TM request.
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
osGLOBAL bit32 satTmWarmReset(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest, /* current task tag */
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext)
{

  tdsaDeviceData_t        *tdsaDeviceData;
  satDeviceData_t         *satDevData;

  tdsaDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  satDevData      = &tdsaDeviceData->satDevData;

  TI_DBG1(("satTmWarmReset: tiDeviceHandle=%p.\n", tiDeviceHandle ));

  /*
   * Check if there is other TM request pending
   */
  if (satDevData->satTmTaskTag != agNULL)
  {
    TI_DBG1(("satTmWarmReset: *** REJECT *** other TM pending, tiDeviceHandle=%p\n",
                tiDeviceHandle));
    return tiError;
  }

  /*
   * Save tiIORequest, will be returned at device reset completion to return
   * the TM completion.
   */
   satDevData->satTmTaskTag = tiIORequest;

  /*
   * Set flag to indicate device in recovery mode.
   */
  satDevData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;

  /*
   * Issue SATA device reset. Set flag to indicate NOT to automatically abort
   * at the completion of SATA device reset.
   */
  satDevData->satAbortAfterReset = agFALSE;

  /* SAT rev8 6.3.6 p22 */
  satStartResetDevice(
                      tiRoot,
                      tiIORequest, /* currentTaskTag */
                      tiDeviceHandle,
                      tiScsiRequest,
                      satIOContext
                      );

  return tiSuccess;

}

osGLOBAL bit32 satTDInternalTmReset(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest, /* current task tag */
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext)
{

  tdsaDeviceData_t        *tdsaDeviceData;
  satDeviceData_t         *satDevData;

  tdsaDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  satDevData      = &tdsaDeviceData->satDevData;

  TI_DBG1(("satTmWarmReset: tiDeviceHandle=%p.\n", tiDeviceHandle ));

  /*
   * Check if there is other TM request pending
   */
  if (satDevData->satTmTaskTag != agNULL)
  {
    TI_DBG1(("satTmWarmReset: *** REJECT *** other TM pending, tiDeviceHandle=%p\n",
                tiDeviceHandle));
    return tiError;
  }

  /*
   * Save tiIORequest, will be returned at device reset completion to return
   * the TM completion.
   */
   satDevData->satTmTaskTag = tiIORequest;

  /*
   * Set flag to indicate device in recovery mode.
   */
  satDevData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;

  /*
   * Issue SATA device reset. Set flag to indicate NOT to automatically abort
   * at the completion of SATA device reset.
   */
  satDevData->satAbortAfterReset = agFALSE;

  /* SAT rev8 6.3.6 p22 */
  satStartResetDevice(
                      tiRoot,
                      tiIORequest, /* currentTaskTag */
                      tiDeviceHandle,
                      tiScsiRequest,
                      satIOContext
                      );

  return tiSuccess;

}

/*****************************************************************************
 *! \brief  satTmAbortTask
 *
 *   This routine is called to initiate a TM ABORT TASK request to SATL.
 *   This routine is independent of HW/LL API.
 *
 *  \param  tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param  tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param  taskTag:          Pointer to the associated task where the TM
 *                            command is to be applied.
 *  \param  currentTaskTag:   Pointer to tag/context for this TM request.
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
osGLOBAL bit32 satTmAbortTask(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,  /* current task tag */
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest, /* NULL */
                            satIOContext_t            *satIOContext,
                            tiIORequest_t             *taskTag)
{

  tdsaDeviceData_t        *tdsaDeviceData;
  satDeviceData_t         *satDevData;
  satIOContext_t          *satTempIOContext = agNULL;
  tdIORequestBody_t       *tdIORequestBody;
  tdIORequestBody_t       *TMtdIORequestBody;
  tdList_t                *elementHdr;
  bit32                   found = agFALSE;
  tiIORequest_t           *tiIOReq;

  tdsaDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  satDevData      = &tdsaDeviceData->satDevData;
  TMtdIORequestBody = (tdIORequestBody_t *)tiIORequest->tdData;

  TI_DBG1(("satTmAbortTask: tiDeviceHandle=%p taskTag=%p.\n", tiDeviceHandle, taskTag ));
  /*
   * Check if there is other TM request pending
   */
  if (satDevData->satTmTaskTag != agNULL)
  {
    TI_DBG1(("satTmAbortTask: REJECT other TM pending, tiDeviceHandle=%p\n",
                tiDeviceHandle));
    /* clean up TD layer's IORequestBody */
    ostiFreeMemory(
                   tiRoot,
                   TMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return tiError;
  }

#ifdef REMOVED
  /*
   * Check if there is only one I/O pending.
   */
  if (satDevData->satPendingIO > 0)
  {
    TI_DBG1(("satTmAbortTask: REJECT num pending I/O, tiDeviceHandle=%p, satPendingIO=0x%x\n",
                tiDeviceHandle, satDevData->satPendingIO));
    /* clean up TD layer's IORequestBody */
    ostiFreeMemory(
                   tiRoot,
                   TMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );

    return tiError;
  }
#endif

  /*
   * Check that the only pending I/O matches taskTag. If not return tiError.
   */
  elementHdr = satDevData->satIoLinkList.flink;

  while (elementHdr != &satDevData->satIoLinkList)
  {
    satTempIOContext = TDLIST_OBJECT_BASE( satIOContext_t,
                                           satIoContextLink,
                                           elementHdr );

    tdIORequestBody = (tdIORequestBody_t *) satTempIOContext->tiRequestBody;
    tiIOReq = tdIORequestBody->tiIORequest;

    elementHdr = elementHdr->flink;   /* for the next while loop  */

    /*
     * Check if the tag matches
     */
    if ( tiIOReq == taskTag)
    {
      found = agTRUE;
      satIOContext->satToBeAbortedIOContext = satTempIOContext;
      TI_DBG1(("satTmAbortTask: found matching tag.\n"));

      break;

    } /* if matching tag */

  } /* while loop */


  if (found == agFALSE )
  {
    TI_DBG1(("satTmAbortTask: *** REJECT *** no match, tiDeviceHandle=%p\n",
                tiDeviceHandle ));

    /* clean up TD layer's IORequestBody */
    ostiFreeMemory(
                   tiRoot,
                   TMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );

    return tiError;
  }

  /*
   * Save tiIORequest, will be returned at device reset completion to return
   * the TM completion.
   */
   satDevData->satTmTaskTag = tiIORequest;

  /*
   * Set flag to indicate device in recovery mode.
   */
  satDevData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;


  /*
   * Issue SATA device reset or check power mode. Set flag to to automatically abort
   * at the completion of SATA device reset.
   * SAT r09 p25
   */
  satDevData->satAbortAfterReset = agTRUE;

  if ( (satTempIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_WRITE) ||
       (satTempIOContext->reqType == AGSA_SATA_PROTOCOL_FPDMA_READ)
      )
  {
    TI_DBG1(("satTmAbortTask: calling satStartCheckPowerMode\n"));
    /* send check power mode */
    satStartCheckPowerMode(
                      tiRoot,
                      tiIORequest, /* currentTaskTag */
                      tiDeviceHandle,
                      tiScsiRequest,
                      satIOContext
                      );
  }
  else
  {
    TI_DBG1(("satTmAbortTask: calling satStartResetDevice\n"));
    /* send AGSA_SATA_PROTOCOL_SRST_ASSERT */
    satStartResetDevice(
                      tiRoot,
                      tiIORequest, /* currentTaskTag */
                      tiDeviceHandle,
                      tiScsiRequest,
                      satIOContext
                      );
  }


  return tiSuccess;
}

/*****************************************************************************
 *! \brief  osSatResetCB
 *
 *   This routine is called to notify the completion of SATA device reset
 *   which was initiated previously through the call to sataLLReset().
 *   This routine is independent of HW/LL API.
 *
 *  \param  tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param  tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param  resetStatus:      Reset status either tiSuccess or tiError.
 *  \param  respFis:          Pointer to the Register Device-To-Host FIS
 *                            received from the device.
 *
 *  \return: None
 *
 *****************************************************************************/
osGLOBAL void osSatResetCB(
                tiRoot_t          *tiRoot,
                tiDeviceHandle_t  *tiDeviceHandle,
                bit32             resetStatus,
                void              *respFis)
{

  agsaRoot_t              *agRoot;
  tdsaDeviceData_t        *tdsaDeviceData;
  satDeviceData_t         *satDevData;
  satIOContext_t          *satIOContext;
  tdIORequestBody_t       *tdIORequestBodyTmp;
  tdList_t                *elementHdr;
  agsaIORequest_t         *agAbortIORequest;
  tdIORequestBody_t       *tdAbortIORequestBody;
  bit32                   PhysUpper32;
  bit32                   PhysLower32;
  bit32                   memAllocStatus;
  void                    *osMemHandle;

  tdsaDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  agRoot          = tdsaDeviceData->agRoot;
  satDevData      = &tdsaDeviceData->satDevData;

  TI_DBG5(("osSatResetCB: tiDeviceHandle=%p resetStatus=0x%x\n",
      tiDeviceHandle, resetStatus ));

  /* We may need to check FIS to check device operating condition */


  /*
   * Check if need to abort all pending I/Os
   */
  if ( satDevData->satAbortAfterReset == agTRUE )
  {
    /*
     * Issue abort to LL layer to all other pending I/Os for the same SATA drive
     */
    elementHdr = satDevData->satIoLinkList.flink;
    while (elementHdr != &satDevData->satIoLinkList)
    {
      satIOContext = TDLIST_OBJECT_BASE( satIOContext_t,
                                         satIoContextLink,
                                         elementHdr );

      tdIORequestBodyTmp = (tdIORequestBody_t *)satIOContext->tiRequestBody;

      /*
       * Issue abort
       */
      TI_DBG5(("osSatResetCB: issuing ABORT tiDeviceHandle=%p agIORequest=%p\n",
      tiDeviceHandle, &tdIORequestBodyTmp->agIORequest ));

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
        TI_DBG1(("osSatResetCB: ostiAllocMemory failed...\n"));
        return;
      }

      if (tdAbortIORequestBody == agNULL)
      {
        /* let os process IO */
        TI_DBG1(("osSatResetCB: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
        return;
      }
      /* setup task management structure */
      tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
      tdAbortIORequestBody->tiDevHandle = tiDeviceHandle;

      /* initialize agIORequest */
      agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
      agAbortIORequest->osData = (void *) tdAbortIORequestBody;
      agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

      saSATAAbort( agRoot, agAbortIORequest, 0, agNULL, 0, &(tdIORequestBodyTmp->agIORequest), agNULL );
      elementHdr = elementHdr->flink;   /* for the next while loop  */

    } /* while */

    /* Reset flag */
    satDevData->satAbortAfterReset = agFALSE;

  }


  /*
   * Check if the device reset if the result of TM request.
   */
  if ( satDevData->satTmTaskTag != agNULL )
  {
    TI_DBG5(("osSatResetCB: calling TM completion tiDeviceHandle=%p satTmTaskTag=%p\n",
    tiDeviceHandle, satDevData->satTmTaskTag ));

    ostiInitiatorEvent( tiRoot,
                        agNULL,               /* portalContext not used */
                        tiDeviceHandle,
                        tiIntrEventTypeTaskManagement,
                        tiTMOK,
                        satDevData->satTmTaskTag);
    /*
     * Reset flag
     */
    satDevData->satTmTaskTag = agNULL;
  }

}


/*****************************************************************************
 *! \brief  osSatIOCompleted
 *
 *   This routine is a callback for SATA completion that required FIS status
 *   translation to SCSI status.
 *
 *  \param   tiRoot:          Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:     Pointer to TISA I/O request context for this I/O.
 *  \param   respFis:         Pointer to status FIS to read.
 *  \param   respFisLen:      Length of response FIS to read.
 *  \param   satIOContext:    Pointer to SAT context.
 *  \param   interruptContext:      Interrupt context
 *
 *  \return: None
 *
 *****************************************************************************/
osGLOBAL void osSatIOCompleted(
                          tiRoot_t           *tiRoot,
                          tiIORequest_t      *tiIORequest,
                          agsaFisHeader_t    *agFirstDword,
                          bit32              respFisLen,
                          agsaFrameHandle_t  agFrameHandle,
                          satIOContext_t     *satIOContext,
                          bit32              interruptContext)

{
  satDeviceData_t           *pSatDevData;
  scsiRspSense_t            *pSense;
#ifdef  TD_DEBUG_ENABLE
  tiIniScsiCmnd_t           *pScsiCmnd;
#endif
  agsaFisRegHostToDevice_t  *hostToDevFis = agNULL;
  bit32                     ataStatus = 0;
  bit32                     ataError;
  satInternalIo_t           *satIntIo = agNULL;
  bit32                     status;
  tiDeviceHandle_t          *tiDeviceHandle;
  satIOContext_t            *satIOContext2;
  tdIORequestBody_t         *tdIORequestBody;
  agsaFisRegD2HHeader_t     *statDevToHostFisHeader = agNULL;
  agsaFisSetDevBitsHeader_t *statSetDevBitFisHeader = agNULL;
  tiIORequest_t             tiIORequestTMP;

  pSense          = satIOContext->pSense;
  pSatDevData     = satIOContext->pSatDevData;
#ifdef  TD_DEBUG_ENABLE
  pScsiCmnd       = satIOContext->pScsiCmnd;
#endif
  hostToDevFis    = satIOContext->pFis;

  tiDeviceHandle  = &((tdsaDeviceData_t *)(pSatDevData->satSaDeviceData))->tiDeviceHandle;
  /*
   * Find out the type of response FIS:
   * Set Device Bit FIS or Reg Device To Host FIS.
   */

  /* First assume it is Reg Device to Host FIS */
  statDevToHostFisHeader = (agsaFisRegD2HHeader_t *)&(agFirstDword->D2H);
  ataStatus     = statDevToHostFisHeader->status;   /* ATA Status register */
  ataError      = statDevToHostFisHeader->error;    /* ATA Eror register   */

  /* for debugging */
  TI_DBG1(("osSatIOCompleted: H to D command 0x%x\n", hostToDevFis->h.command));
  TI_DBG1(("osSatIOCompleted: D to H fistype 0x%x\n", statDevToHostFisHeader->fisType));


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
    TI_DBG1(("osSatIOCompleted: *** UNEXPECTED RESP FIS TYPE 0x%x *** tiIORequest=%p\n",
                 statDevToHostFisHeader->fisType, tiIORequest));

    satSetSensePayload( pSense,
                        SCSI_SNSKEY_HARDWARE_ERROR,
                        0,
                        SCSI_SNSCODE_INTERNAL_TARGET_FAILURE,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              interruptContext );
    return;

  }

  if ( ataStatus & DF_ATA_STATUS_MASK )
  {
    pSatDevData->satDeviceFaultState = agTRUE;
  }
  else
  {
    pSatDevData->satDeviceFaultState = agFALSE;
  }

  TI_DBG5(("osSatIOCompleted: tiIORequest=%p  CDB=0x%x ATA CMD =0x%x\n",
    tiIORequest, pScsiCmnd->cdb[0], hostToDevFis->h.command));

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
       *   return tiSuccess;

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
       *    satReadLogExtCB(). Steps 6,7,8,9 below are the step 1,2,3,4 in
       *    satReadLogExtCB().
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

      TI_DBG1(("osSatIOCompleted: NCQ ERROR tiIORequest=%p ataStatus=0x%x ataError=0x%x\n",
          tiIORequest, ataStatus, ataError ));

      /* Set flag to indicate we are in recovery */
      pSatDevData->satDriveState = SAT_DEV_STATE_IN_RECOVERY;

      /* Return the failed NCQ I/O to OS-Apps Specifiic layer */
      osSatDefaultTranslation( tiRoot,
                               tiIORequest,
                               satIOContext,
                               pSense,
                               (bit8)ataStatus,
                               (bit8)ataError,
                               interruptContext );

      /*
       * Allocate resource for READ LOG EXT page 10h
       */
      satIntIo = satAllocIntIoResource( tiRoot,
                                        &(tiIORequestTMP), /* anything but NULL */
                                        pSatDevData,
                                        sizeof (satReadLogExtPage10h_t),
                                        satIntIo);

      if (satIntIo == agNULL)
      {
        TI_DBG1(("osSatIOCompleted: can't send RLE due to resource lack\n"));

        /* Abort I/O after completion of device reset */
        pSatDevData->satAbortAfterReset = agTRUE;
#ifdef NOT_YET
        /* needs further investigation */
        /* no report to OS layer */
        satSubTM(tiRoot,
                 tiDeviceHandle,
                 TD_INTERNAL_TM_RESET,
                 agNULL,
                 agNULL,
                 agNULL,
                 agFALSE);
#endif


        TI_DBG1(("osSatIOCompleted: calling saSATADeviceReset 1\n"));
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

      if (status != tiSuccess)
      {
        TI_DBG1(("osSatIOCompleted: can't send RLE due to LL api failure\n"));
        satFreeIntIoResource( tiRoot,
                              pSatDevData,
                              satIntIo);

        /* Abort I/O after completion of device reset */
        pSatDevData->satAbortAfterReset = agTRUE;
#ifdef NOT_YET
        /* needs further investigation */
        /* no report to OS layer */
        satSubTM(tiRoot,
                 tiDeviceHandle,
                 TD_INTERNAL_TM_RESET,
                 agNULL,
                 agNULL,
                 agNULL,
                 agFALSE);
#endif

        TI_DBG1(("osSatIOCompleted: calling saSATADeviceReset 2\n"));
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
      osSatDefaultTranslation( tiRoot,
                               tiIORequest,
                               satIOContext,
                               pSense,
                               (bit8)ataStatus,
                               (bit8)ataError,
                               interruptContext );
      break;

  }  /* end switch  */
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI STANDARD INQUIRY.
 *
 *  SAT implementation for SCSI STANDARD INQUIRY.
 *
 *  \param   pInquiry:         Pointer to Inquiry Data buffer.
 *  \param   pSATAIdData:      Pointer to ATA IDENTIFY DEVICE data.
 *
 *  \return None.
 */
/*****************************************************************************/
GLOBAL void  satInquiryStandard(
                                bit8                    *pInquiry,
                                agsaSATAIdentifyData_t  *pSATAIdData,
                                tiIniScsiCmnd_t          *scsiCmnd
                                )
{
  tiLUN_t       *pLun;
  pLun          = &scsiCmnd->lun;

  /*
    Assumption: Basic Task Mangement is supported
    -> BQUE 1 and CMDQUE 0, SPC-4, Table96, p147
  */
 /*
    See SPC-4, 6.4.2, p 143
    and SAT revision 8, 8.1.2, p 28
   */

  TI_DBG5(("satInquiryStandard: start\n"));

  if (pInquiry == agNULL)
  {
    TI_DBG1(("satInquiryStandard: pInquiry is NULL, wrong\n"));
    return;
  }
  else
  {
    TI_DBG5(("satInquiryStandard: pInquiry is NOT NULL\n"));
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
  osti_strncpy((char*)&pInquiry[8],  AG_SAT_VENDOR_ID_STRING,8);   /* 8 bytes   */

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
       (pSATAIdData->firmwareVersion[5] == 0x00 ) &&
       (pSATAIdData->firmwareVersion[6] == 0x20 ) &&
       (pSATAIdData->firmwareVersion[7] == 0x00 )
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
       (pSATAIdData->firmwareVersion[5] == 0x00 ) &&
       (pSATAIdData->firmwareVersion[6] == 0x20 ) &&
       (pSATAIdData->firmwareVersion[7] == 0x00 )
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

  TI_DBG5(("satInquiryStandard: end\n"));

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI INQUIRY page 0.
 *
 *  SAT implementation for SCSI INQUIRY page 0.
 *
 *  \param   pInquiry:         Pointer to Inquiry Data buffer.
 *  \param   pSATAIdData:      Pointer to ATA IDENTIFY DEVICE data.
 *
 *  \return None.
 */
/*****************************************************************************/
GLOBAL void  satInquiryPage0(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData)
{

  TI_DBG5(("satInquiryPage0: entry\n"));

  /*
    See SPC-4, 7.6.9, p 345
    and SAT revision 8, 10.3.2, p 77
   */
  pInquiry[0] = 0x00;
  pInquiry[1] = 0x00; /* page code */
  pInquiry[2] = 0x00; /* reserved */
  pInquiry[3] = 7 - 3; /* last index(in this case, 6) - 3; page length */

  /* supported vpd page list */
  pInquiry[4] = 0x00; /* page 0x00 supported */
  pInquiry[5] = 0x80; /* page 0x80 supported */
  pInquiry[6] = 0x83; /* page 0x83 supported */
  pInquiry[7] = 0x89; /* page 0x89 supported */

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI INQUIRY page 83.
 *
 *  SAT implementation for SCSI INQUIRY page 83.
 *
 *  \param   pInquiry:         Pointer to Inquiry Data buffer.
 *  \param   pSATAIdData:      Pointer to ATA IDENTIFY DEVICE data.
 *
 *  \return None.
 */
/*****************************************************************************/
GLOBAL void  satInquiryPage83(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData,
                    satDeviceData_t         *pSatDevData)
{

  satSimpleSATAIdentifyData_t   *pSimpleData;

  /*
   * When translating the fields, in some cases using the simple form of SATA
   * Identify Device Data is easier. So we define it here.
   * Both pSimpleData and pSATAIdData points to the same data.
   */
  pSimpleData = ( satSimpleSATAIdentifyData_t *)pSATAIdData;

  TI_DBG5(("satInquiryPage83: entry\n"));

  pInquiry[0] = 0x00;
  pInquiry[1] = 0x83; /* page code */
  pInquiry[2] = 0;    /* Reserved */

  /*
   * If the ATA device returns word 87 bit 8 set to one in its IDENTIFY DEVICE
   * data indicating that it supports the WORLD WIDE NAME field
   * (i.e., words 108-111), the SATL shall include an identification descriptor
   * containing a logical unit name.
   */
  if ( pSatDevData->satWWNSupport)
  {
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

  }
  else
  {
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
    osti_strncpy((char *)&pInquiry[8], AG_SAT_VENDOR_ID_STRING, 8);


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
  }

}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI INQUIRY page 89.
 *
 *  SAT implementation for SCSI INQUIRY page 89.
 *
 *  \param   pInquiry:         Pointer to Inquiry Data buffer.
 *  \param   pSATAIdData:      Pointer to ATA IDENTIFY DEVICE data.
 *  \param   pSatDevData       Pointer to internal device data structure
 *
 *  \return None.
 */
/*****************************************************************************/
GLOBAL void  satInquiryPage89(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData,
                    satDeviceData_t         *pSatDevData)
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

  TI_DBG5(("satInquiryPage89: start\n"));

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
  osti_strncpy((char*)&pInquiry[8],  "PMC-SIERRA", 8);   /* 8 bytes   */

  /* SAT Product Idetification */
  osti_strncpy((char*)&pInquiry[16],  "Tachyon-SPC    ", 16);   /* 16 bytes   */

  /* SAT Product Revision Level */
  osti_strncpy((char*)&pInquiry[32],  "01", 4);   /* 4 bytes   */

  /* Signature, SAT revision8, Table88, p85 */


  pInquiry[36] = 0x34;    /* FIS type */
  if (pSatDevData->satDeviceType == SATA_ATA_DEVICE)
  {
    /* interrupt assume to be 0 */
    pInquiry[37] = (bit8)((pSatDevData->satPMField) >> (4 * 7)); /* first four bits of PM field */
  }
  else
  {
    /* interrupt assume to be 1 */
    pInquiry[37] = (bit8)(0x40 + (bit8)(((pSatDevData->satPMField) >> (4 * 7)))); /* first four bits of PM field */
  }
  pInquiry[38] = 0;
  pInquiry[39] = 0;

  if (pSatDevData->satDeviceType == SATA_ATA_DEVICE)
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
  if (pSatDevData->satDeviceType == SATA_ATA_DEVICE)
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

  /* Identify Device */
  osti_memcpy(&pInquiry[60], pSimpleData, sizeof(satSimpleSATAIdentifyData_t));
  return;
}

/*****************************************************************************/
/*! \brief SAT implementation for SCSI INQUIRY page 0.
 *
 *  SAT implementation for SCSI INQUIRY page 0.
 *
 *  \param   pInquiry:         Pointer to Inquiry Data buffer.
 *  \param   pSATAIdData:      Pointer to ATA IDENTIFY DEVICE data.
 *
 *  \return None.
 */
/*****************************************************************************/
GLOBAL void  satInquiryPage80(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData)
{

  TI_DBG5(("satInquiryPage80: entry\n"));

  /*
    See SPC-4, 7.6.9, p 345
    and SAT revision 8, 10.3.3, p 77
   */
  pInquiry[0] = 0x00;
  pInquiry[1] = 0x80; /* page code */
  pInquiry[2] = 0x00; /* reserved */
  pInquiry[3] = 0x14; /* page length */

  /* supported vpd page list */
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


}



/*****************************************************************************/
/*! \brief  Send READ LOG EXT ATA PAGE 10h command to sata drive.
 *
 *  Send READ LOG EXT ATA command PAGE 10h request to LL layer.
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
GLOBAL bit32  satSendReadLogExt(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext)

{

  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;

  fis           = satIOContext->pFis;

  TI_DBG1(("satSendReadLogExt: tiDeviceHandle=%p tiIORequest=%p\n",
      tiDeviceHandle, tiIORequest));

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
  satIOContext->satCompleteCB = &satReadLogExtCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG1(("satSendReadLogExt: end status %d\n", status));

  return (status);

}


/*****************************************************************************/
/*! \brief  SAT default ATA status and ATA error translation to SCSI.
 *
 *  SSAT default ATA status and ATA error translation to SCSI.
 *
 *  \param   tiRoot:        Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:   Pointer to TISA I/O request context for this I/O.
 *  \param   satIOContext:  Pointer to the SAT IO Context
 *  \param   pSense:        Pointer to scsiRspSense_t
 *  \param   ataStatus:     ATA status register
 *  \param   ataError:      ATA error register
 *  \param   interruptContext:    Interrupt context
 *
 *  \return  None
 */
/*****************************************************************************/
GLOBAL void  osSatDefaultTranslation(
                   tiRoot_t             *tiRoot,
                   tiIORequest_t        *tiIORequest,
                   satIOContext_t       *satIOContext,
                   scsiRspSense_t       *pSense,
                   bit8                 ataStatus,
                   bit8                 ataError,
                   bit32                interruptContext )
{

  /*
   * Check for device fault case
   */
  if ( ataStatus & DF_ATA_STATUS_MASK )
  {
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_HARDWARE_ERROR,
                        0,
                        SCSI_SNSCODE_INTERNAL_TARGET_FAILURE,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
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
      TI_DBG1(("osSatDefaultTranslation: NM_ATA_ERROR ataError= 0x%x, tiIORequest=%p\n",
                 ataError, tiIORequest));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_NOT_READY,
                          0,
                          SCSI_SNSCODE_MEDIUM_NOT_PRESENT,
                          satIOContext);
    }

    else if (ataError & UNC_ATA_ERROR_MASK)
    {
      TI_DBG1(("osSatDefaultTranslation: UNC_ATA_ERROR ataError= 0x%x, tiIORequest=%p\n",
                 ataError, tiIORequest));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_MEDIUM_ERROR,
                          0,
                          SCSI_SNSCODE_UNRECOVERED_READ_ERROR,
                          satIOContext);
    }

    else if (ataError & IDNF_ATA_ERROR_MASK)
    {
      TI_DBG1(("osSatDefaultTranslation: IDNF_ATA_ERROR ataError= 0x%x, tiIORequest=%p\n",
                 ataError, tiIORequest));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_MEDIUM_ERROR,
                          0,
                          SCSI_SNSCODE_RECORD_NOT_FOUND,
                          satIOContext);
    }

    else if (ataError & MC_ATA_ERROR_MASK)
    {
      TI_DBG1(("osSatDefaultTranslation: MC_ATA_ERROR ataError= 0x%x, tiIORequest=%p\n",
                 ataError, tiIORequest));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_UNIT_ATTENTION,
                          0,
                          SCSI_SNSCODE_NOT_READY_TO_READY_CHANGE,
                          satIOContext);
    }

    else if (ataError & MCR_ATA_ERROR_MASK)
    {
      TI_DBG1(("osSatDefaultTranslation: MCR_ATA_ERROR ataError= 0x%x, tiIORequest=%p\n",
                 ataError, tiIORequest));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_UNIT_ATTENTION,
                          0,
                          SCSI_SNSCODE_OPERATOR_MEDIUM_REMOVAL_REQUEST,
                          satIOContext);
    }

    else if (ataError & ICRC_ATA_ERROR_MASK)
    {
      TI_DBG1(("osSatDefaultTranslation: ICRC_ATA_ERROR ataError= 0x%x, tiIORequest=%p\n",
                 ataError, tiIORequest));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ABORTED_COMMAND,
                          0,
                          SCSI_SNSCODE_INFORMATION_UNIT_CRC_ERROR,
                          satIOContext);
    }

    else if (ataError & ABRT_ATA_ERROR_MASK)
    {
      TI_DBG1(("osSatDefaultTranslation: ABRT_ATA_ERROR ataError= 0x%x, tiIORequest=%p\n",
                 ataError, tiIORequest));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_ABORTED_COMMAND,
                          0,
                          SCSI_SNSCODE_NO_ADDITIONAL_INFO,
                          satIOContext);
    }

    else
    {
      TI_DBG1(("osSatDefaultTranslation: **** UNEXPECTED ATA_ERROR **** ataError= 0x%x, tiIORequest=%p\n",
                 ataError, tiIORequest));
      satSetSensePayload( pSense,
                          SCSI_SNSKEY_HARDWARE_ERROR,
                          0,
                          SCSI_SNSCODE_INTERNAL_TARGET_FAILURE,
                          satIOContext);
    }

    /* Send the completion response now */
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              interruptContext );
    return;


  }

  else /*  (ataStatus & ERR_ATA_STATUS_MASK ) is false */
  {
    /* This case should never happen */
    TI_DBG1(("osSatDefaultTranslation: *** UNEXPECTED ATA status 0x%x *** tiIORequest=%p\n",
                 ataStatus, tiIORequest));
    satSetSensePayload( pSense,
                        SCSI_SNSKEY_HARDWARE_ERROR,
                        0,
                        SCSI_SNSCODE_INTERNAL_TARGET_FAILURE,
                        satIOContext);

    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              tiIOSuccess,
                              SCSI_STAT_CHECK_CONDITION,
                              satIOContext->pTiSenseData,
                              interruptContext );
    return;

  }


}

/*****************************************************************************/
/*! \brief  Allocate resource for SAT intervally generated I/O.
 *
 *  Allocate resource for SAT intervally generated I/O.
 *
 *  \param   tiRoot:      Pointer to TISA driver/port instance.
 *  \param   satDevData:  Pointer to SAT specific device data.
 *  \param   allocLength: Length in byte of the DMA mem to allocate, upto
 *                        one page size.
 *  \param   satIntIo:    Pointer (output) to context for SAT internally
 *                        generated I/O that is allocated by this routine.
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     Success.
 *    - \e tiError:       Failed allocating resource.
 */
/*****************************************************************************/
GLOBAL satInternalIo_t * satAllocIntIoResource(
                    tiRoot_t              *tiRoot,
                    tiIORequest_t         *tiIORequest,
                    satDeviceData_t       *satDevData,
                    bit32                 dmaAllocLength,
                    satInternalIo_t       *satIntIo)
{
  tdList_t          *tdList = agNULL;
  bit32             memAllocStatus;

  TI_DBG1(("satAllocIntIoResource: start\n"));
  TI_DBG6(("satAllocIntIoResource: satIntIo %p\n", satIntIo));
  if (satDevData == agNULL)
  {
    TI_DBG1(("satAllocIntIoResource: ***** ASSERT satDevData is null\n"));
    return agNULL;
  }

  tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
  if (!TDLIST_EMPTY(&(satDevData->satFreeIntIoLinkList)))
  {
    TDLIST_DEQUEUE_FROM_HEAD(&tdList, &(satDevData->satFreeIntIoLinkList));
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);
    TI_DBG1(("satAllocIntIoResource() no more internal free link.\n"));
    return agNULL;
  }

  if (tdList == agNULL)
  {
    tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);
    TI_DBG1(("satAllocIntIoResource() FAIL to alloc satIntIo.\n"));
    return agNULL;
  }

  satIntIo = TDLIST_OBJECT_BASE( satInternalIo_t, satIntIoLink, tdList);
  TI_DBG6(("satAllocIntIoResource: satDevData %p satIntIo id %d\n", satDevData, satIntIo->id));

  /* Put in active list */
  TDLIST_DEQUEUE_THIS (&(satIntIo->satIntIoLink));
  TDLIST_ENQUEUE_AT_TAIL (&(satIntIo->satIntIoLink), &(satDevData->satActiveIntIoLinkList));
  tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);

#ifdef REMOVED
  /* Put in active list */
  tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
  TDLIST_DEQUEUE_THIS (tdList);
  TDLIST_ENQUEUE_AT_TAIL (tdList, &(satDevData->satActiveIntIoLinkList));
  tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);

  satIntIo = TDLIST_OBJECT_BASE( satInternalIo_t, satIntIoLink, tdList);
  TI_DBG6(("satAllocIntIoResource: satDevData %p satIntIo id %d\n", satDevData, satIntIo->id));
#endif

  /*
    typedef struct
    {
      tdList_t                    satIntIoLink;
      tiIORequest_t               satIntTiIORequest;
      void                        *satIntRequestBody;
      tiScsiInitiatorRequest_t   satIntTiScsiXchg;
      tiMem_t                     satIntDmaMem;
      tiMem_t                     satIntReqBodyMem;
      bit32                       satIntFlag;
    } satInternalIo_t;
  */

  /*
   * Allocate mem for Request Body
   */
  satIntIo->satIntReqBodyMem.totalLength = sizeof(tdIORequestBody_t);

  memAllocStatus = ostiAllocMemory( tiRoot,
                                    &satIntIo->satIntReqBodyMem.osHandle,
                                    (void **)&satIntIo->satIntRequestBody,
                                    &satIntIo->satIntReqBodyMem.physAddrUpper,
                                    &satIntIo->satIntReqBodyMem.physAddrLower,
                                    8,
                                    satIntIo->satIntReqBodyMem.totalLength,
                                    agTRUE );

  if (memAllocStatus != tiSuccess)
  {
    TI_DBG1(("satAllocIntIoResource() FAIL to alloc mem for Req Body.\n"));
    /*
     * Return satIntIo to the free list
     */
    tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
    TDLIST_DEQUEUE_THIS (&satIntIo->satIntIoLink);
    TDLIST_ENQUEUE_AT_HEAD(&satIntIo->satIntIoLink, &satDevData->satFreeIntIoLinkList);
    tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);

    return agNULL;
  }

  /*
   *   Allocate DMA memory if required
   */
  if (dmaAllocLength != 0)
  {
    satIntIo->satIntDmaMem.totalLength = dmaAllocLength;

    memAllocStatus = ostiAllocMemory( tiRoot,
                                      &satIntIo->satIntDmaMem.osHandle,
                                      (void **)&satIntIo->satIntDmaMem.virtPtr,
                                      &satIntIo->satIntDmaMem.physAddrUpper,
                                      &satIntIo->satIntDmaMem.physAddrLower,
                                      8,
                                      satIntIo->satIntDmaMem.totalLength,
                                      agFALSE);
    TI_DBG6(("satAllocIntIoResource: len %d \n", satIntIo->satIntDmaMem.totalLength));
    TI_DBG6(("satAllocIntIoResource: pointer %p \n", satIntIo->satIntDmaMem.osHandle));

    if (memAllocStatus != tiSuccess)
    {
      TI_DBG1(("satAllocIntIoResource() FAIL to alloc mem for DMA mem.\n"));
      /*
       * Return satIntIo to the free list
       */
      tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
      TDLIST_DEQUEUE_THIS (&satIntIo->satIntIoLink);
      TDLIST_ENQUEUE_AT_HEAD(&satIntIo->satIntIoLink, &satDevData->satFreeIntIoLinkList);
      tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);

      /*
       * Free mem allocated for Req body
       */
      ostiFreeMemory( tiRoot,
                      satIntIo->satIntReqBodyMem.osHandle,
                      satIntIo->satIntReqBodyMem.totalLength);

      return agNULL;
    }
  }

  /*
    typedef struct
    {
      tdList_t                    satIntIoLink;
      tiIORequest_t               satIntTiIORequest;
      void                        *satIntRequestBody;
      tiScsiInitiatorRequest_t   satIntTiScsiXchg;
      tiMem_t                     satIntDmaMem;
      tiMem_t                     satIntReqBodyMem;
      bit32                       satIntFlag;
    } satInternalIo_t;
  */

  /*
   * Initialize satIntTiIORequest field
   */
  satIntIo->satIntTiIORequest.osData = agNULL;  /* Not used for internal SAT I/O */
  satIntIo->satIntTiIORequest.tdData = satIntIo->satIntRequestBody;

  /*
   * saves the original tiIOrequest
   */
  satIntIo->satOrgTiIORequest = tiIORequest;
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
   * Initialize satIntTiScsiXchg. Since the internal SAT request is NOT
   * originated from SCSI request, only the following fields are initialized:
   *  - sglVirtualAddr if DMA transfer is involved
   *  - agSgl1 if DMA transfer is involved
   *  - expDataLength in scsiCmnd since this field is read by sataLLIOStart()
   */
  if (dmaAllocLength != 0)
  {
    satIntIo->satIntTiScsiXchg.sglVirtualAddr = satIntIo->satIntDmaMem.virtPtr;

    OSSA_WRITE_LE_32(agNULL, &satIntIo->satIntTiScsiXchg.agSgl1.len, 0,
                     satIntIo->satIntDmaMem.totalLength);
    satIntIo->satIntTiScsiXchg.agSgl1.lower = satIntIo->satIntDmaMem.physAddrLower;
    satIntIo->satIntTiScsiXchg.agSgl1.upper = satIntIo->satIntDmaMem.physAddrUpper;
    satIntIo->satIntTiScsiXchg.agSgl1.type  = tiSgl;

    satIntIo->satIntTiScsiXchg.scsiCmnd.expDataLength = satIntIo->satIntDmaMem.totalLength;
  }
  else
  {
    satIntIo->satIntTiScsiXchg.sglVirtualAddr = agNULL;

    satIntIo->satIntTiScsiXchg.agSgl1.len   = 0;
    satIntIo->satIntTiScsiXchg.agSgl1.lower = 0;
    satIntIo->satIntTiScsiXchg.agSgl1.upper = 0;
    satIntIo->satIntTiScsiXchg.agSgl1.type  = tiSgl;

    satIntIo->satIntTiScsiXchg.scsiCmnd.expDataLength = 0;
  }

  TI_DBG5(("satAllocIntIoResource: satIntIo->satIntTiScsiXchg.agSgl1.len %d\n", satIntIo->satIntTiScsiXchg.agSgl1.len));

  TI_DBG5(("satAllocIntIoResource: satIntIo->satIntTiScsiXchg.agSgl1.upper %d\n", satIntIo->satIntTiScsiXchg.agSgl1.upper));

  TI_DBG5(("satAllocIntIoResource: satIntIo->satIntTiScsiXchg.agSgl1.lower %d\n", satIntIo->satIntTiScsiXchg.agSgl1.lower));

  TI_DBG5(("satAllocIntIoResource: satIntIo->satIntTiScsiXchg.agSgl1.type %d\n", satIntIo->satIntTiScsiXchg.agSgl1.type));
    TI_DBG5(("satAllocIntIoResource: return satIntIo %p\n", satIntIo));
  return  satIntIo;

}

/*****************************************************************************/
/*! \brief  Free resource for SAT intervally generated I/O.
 *
 *  Free resource for SAT intervally generated I/O that was previously
 *  allocated in satAllocIntIoResource().
 *
 *  \param   tiRoot:      Pointer to TISA driver/port instance.
 *  \param   satDevData:  Pointer to SAT specific device data.
 *  \param   satIntIo:    Pointer to context for SAT internal I/O that was
 *                        previously allocated in satAllocIntIoResource().
 *
 *  \return  None
 */
/*****************************************************************************/
GLOBAL void  satFreeIntIoResource(
                    tiRoot_t              *tiRoot,
                    satDeviceData_t       *satDevData,
                    satInternalIo_t       *satIntIo)
{
  TI_DBG6(("satFreeIntIoResource: start\n"));

  if (satIntIo == agNULL)
  {
    TI_DBG6(("satFreeIntIoResource: allowed call\n"));
    return;
  }

  /* sets the original tiIOrequest to agNULL for internally generated ATA cmnd */
  satIntIo->satOrgTiIORequest = agNULL;

  /*
   * Free DMA memory if previosly alocated
   */
  if (satIntIo->satIntTiScsiXchg.scsiCmnd.expDataLength != 0)
  {
    TI_DBG1(("satFreeIntIoResource: DMA len %d\n", satIntIo->satIntDmaMem.totalLength));
    TI_DBG6(("satFreeIntIoResource: pointer %p\n", satIntIo->satIntDmaMem.osHandle));

    ostiFreeMemory( tiRoot,
                    satIntIo->satIntDmaMem.osHandle,
                    satIntIo->satIntDmaMem.totalLength);
    satIntIo->satIntTiScsiXchg.scsiCmnd.expDataLength = 0;
  }

  if (satIntIo->satIntReqBodyMem.totalLength != 0)
  {
    TI_DBG1(("satFreeIntIoResource: req body len %d\n", satIntIo->satIntReqBodyMem.totalLength));
    /*
     * Free mem allocated for Req body
     */
    ostiFreeMemory( tiRoot,
                    satIntIo->satIntReqBodyMem.osHandle,
                    satIntIo->satIntReqBodyMem.totalLength);

    satIntIo->satIntReqBodyMem.totalLength = 0;
  }

  TI_DBG6(("satFreeIntIoResource: satDevData %p satIntIo id %d\n", satDevData, satIntIo->id));
  /*
   * Return satIntIo to the free list
   */
  tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
  TDLIST_DEQUEUE_THIS (&(satIntIo->satIntIoLink));
  TDLIST_ENQUEUE_AT_TAIL (&(satIntIo->satIntIoLink), &(satDevData->satFreeIntIoLinkList));
  tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);

}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI INQUIRY.
 *
 *  SAT implementation for SCSI INQUIRY.
 *  This function sends ATA Identify Device data command for SCSI INQUIRY
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
GLOBAL bit32  satSendIDDev(
                           tiRoot_t                  *tiRoot,
                           tiIORequest_t             *tiIORequest,
                           tiDeviceHandle_t          *tiDeviceHandle,
                           tiScsiInitiatorRequest_t *tiScsiRequest,
                           satIOContext_t            *satIOContext)

{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
#ifdef  TD_DEBUG_ENABLE
  satInternalIo_t           *satIntIoContext;
  tdsaDeviceData_t          *oneDeviceData;
  tdIORequestBody_t         *tdIORequestBody;
#endif

  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;

  TI_DBG5(("satSendIDDev: start\n"));
#ifdef  TD_DEBUG_ENABLE
  oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
#endif
  TI_DBG5(("satSendIDDev: did %d\n", oneDeviceData->id));


#ifdef  TD_DEBUG_ENABLE
  satIntIoContext = satIOContext->satIntIoContext;
  tdIORequestBody = satIntIoContext->satIntRequestBody;
#endif

  TI_DBG5(("satSendIDDev: satIOContext %p tdIORequestBody %p\n", satIOContext, tdIORequestBody));

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
  satIOContext->satCompleteCB = &satInquiryCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

#ifdef TD_INTERNAL_DEBUG
  tdhexdump("satSendIDDev", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
#ifdef  TD_DEBUG_ENABLE
  tdhexdump("satSendIDDev LL", (bit8 *)&(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev), sizeof(agsaFisRegHostToDevice_t));
#endif
#endif

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG6(("satSendIDDev: end status %d\n", status));
  return status;
}


/*****************************************************************************/
/*! \brief SAT implementation for SCSI INQUIRY.
 *
 *  SAT implementation for SCSI INQUIRY.
 *  This function prepares TD layer internal resource to send ATA
 *  Identify Device data command for SCSI INQUIRY
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
/* prerequsite: tdsaDeviceData and agdevhandle must exist; in other words, LL discovered the device
   already */
/*
  convert OS generated IO to TD generated IO due to difference in sgl
*/
GLOBAL bit32  satStartIDDev(
                               tiRoot_t                  *tiRoot,
                               tiIORequest_t             *tiIORequest,
                               tiDeviceHandle_t          *tiDeviceHandle,
                               tiScsiInitiatorRequest_t *tiScsiRequest,
                               satIOContext_t            *satIOContext
                            )
{
  satInternalIo_t           *satIntIo = agNULL;
  satDeviceData_t           *satDevData = agNULL;
  tdIORequestBody_t         *tdIORequestBody;
  satIOContext_t            *satNewIOContext;
  bit32                     status;

  TI_DBG6(("satStartIDDev: start\n"));

  satDevData = satIOContext->pSatDevData;

  TI_DBG6(("satStartIDDev: before alloc\n"));

  /* allocate identify device command */
  satIntIo = satAllocIntIoResource( tiRoot,
                                    tiIORequest,
                                    satDevData,
                                    sizeof(agsaSATAIdentifyData_t), /* 512; size of identify device data */
                                    satIntIo);

  TI_DBG6(("satStartIDDev: before after\n"));

  if (satIntIo == agNULL)
  {
    TI_DBG1(("satStartIDDev: can't alloacate\n"));

#if 0
    ostiInitiatorIOCompleted (
                              tiRoot,
                              tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              satIOContext->interruptContext
                              );
#endif

    return tiError;
  }

  /* fill in fields */
  /* real ttttttthe one worked and the same; 5/21/07/ */
  satIntIo->satOrgTiIORequest = tiIORequest; /* changed */
  tdIORequestBody = satIntIo->satIntRequestBody;
  satNewIOContext = &(tdIORequestBody->transport.SATA.satIOContext);

  satNewIOContext->pSatDevData   = satDevData;
  satNewIOContext->pFis          = &(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satNewIOContext->pScsiCmnd     = &(satIntIo->satIntTiScsiXchg.scsiCmnd);
  satNewIOContext->pSense        = &(tdIORequestBody->transport.SATA.sensePayload);
  satNewIOContext->pTiSenseData  = &(tdIORequestBody->transport.SATA.tiSenseData);
  satNewIOContext->tiRequestBody = satIntIo->satIntRequestBody; /* key fix */
  satNewIOContext->interruptContext = tiInterruptContext;
  satNewIOContext->satIntIoContext  = satIntIo;

  satNewIOContext->ptiDeviceHandle = agNULL;
  satNewIOContext->satOrgIOContext = satIOContext; /* changed */

  /* this is valid only for TD layer generated (not triggered by OS at all) IO */
  satNewIOContext->tiScsiXchg = &(satIntIo->satIntTiScsiXchg);


  TI_DBG6(("satStartIDDev: OS satIOContext %p \n", satIOContext));
  TI_DBG6(("satStartIDDev: TD satNewIOContext %p \n", satNewIOContext));
  TI_DBG6(("satStartIDDev: OS tiScsiXchg %p \n", satIOContext->tiScsiXchg));
  TI_DBG6(("satStartIDDev: TD tiScsiXchg %p \n", satNewIOContext->tiScsiXchg));



  TI_DBG1(("satStartIDDev: satNewIOContext %p tdIORequestBody %p\n", satNewIOContext, tdIORequestBody));

  status = satSendIDDev( tiRoot,
                         &satIntIo->satIntTiIORequest, /* New tiIORequest */
                         tiDeviceHandle,
                         satNewIOContext->tiScsiXchg, /* New tiScsiInitiatorRequest_t *tiScsiRequest, */
                         satNewIOContext);

  if (status != tiSuccess)
  {
    TI_DBG1(("satStartIDDev: failed in sending\n"));

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

#if 0
    ostiInitiatorIOCompleted (
                              tiRoot,
                              tiIORequest,
                              tiIOFailed,
                              tiDetailOtherError,
                              agNULL,
                              satIOContext->interruptContext
                              );
#endif

    return tiError;
  }


  TI_DBG6(("satStartIDDev: end\n"));

  return status;


}

/*****************************************************************************/
/*! \brief satComputeCDB10LBA.
 *
 *  This fuctions computes LBA of CDB10.
 *
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return
 *    - \e LBA
 */
/*****************************************************************************/
bit32 satComputeCDB10LBA(satIOContext_t            *satIOContext)
{
  tiIniScsiCmnd_t           *scsiCmnd;
  tiScsiInitiatorRequest_t *tiScsiRequest;
  bit32                     lba = 0;

  TI_DBG5(("satComputeCDB10LBA: start\n"));
  tiScsiRequest = satIOContext->tiScsiXchg;
  scsiCmnd      = &(tiScsiRequest->scsiCmnd);

  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];

  return lba;
}

/*****************************************************************************/
/*! \brief satComputeCDB10TL.
 *
 *  This fuctions computes transfer length of CDB10.
 *
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return
 *    - \e TL
 */
/*****************************************************************************/
bit32 satComputeCDB10TL(satIOContext_t            *satIOContext)
{

  tiIniScsiCmnd_t           *scsiCmnd;
  tiScsiInitiatorRequest_t *tiScsiRequest;
  bit32                     tl = 0;

  TI_DBG5(("satComputeCDB10TL: start\n"));
  tiScsiRequest = satIOContext->tiScsiXchg;
  scsiCmnd      = &(tiScsiRequest->scsiCmnd);

  tl = (scsiCmnd->cdb[7] << 8) + scsiCmnd->cdb[8];
  return tl;
}

/*****************************************************************************/
/*! \brief satComputeCDB12LBA.
 *
 *  This fuctions computes LBA of CDB12.
 *
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return
 *    - \e LBA
 */
/*****************************************************************************/
bit32 satComputeCDB12LBA(satIOContext_t            *satIOContext)
{
  tiIniScsiCmnd_t           *scsiCmnd;
  tiScsiInitiatorRequest_t *tiScsiRequest;
  bit32                     lba = 0;

  TI_DBG5(("satComputeCDB10LBA: start\n"));
  tiScsiRequest = satIOContext->tiScsiXchg;
  scsiCmnd      = &(tiScsiRequest->scsiCmnd);

  lba = (scsiCmnd->cdb[2] << (8*3)) + (scsiCmnd->cdb[3] << (8*2))
    + (scsiCmnd->cdb[4] << 8) + scsiCmnd->cdb[5];

  return lba;
}

/*****************************************************************************/
/*! \brief satComputeCDB12TL.
 *
 *  This fuctions computes transfer length of CDB12.
 *
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return
 *    - \e TL
 */
/*****************************************************************************/
bit32 satComputeCDB12TL(satIOContext_t            *satIOContext)
{

  tiIniScsiCmnd_t           *scsiCmnd;
  tiScsiInitiatorRequest_t *tiScsiRequest;
  bit32                     tl = 0;

  TI_DBG5(("satComputeCDB10TL: start\n"));
  tiScsiRequest = satIOContext->tiScsiXchg;
  scsiCmnd      = &(tiScsiRequest->scsiCmnd);

  tl = (scsiCmnd->cdb[6] << (8*3)) + (scsiCmnd->cdb[7] << (8*2))
    + (scsiCmnd->cdb[8] << 8) + scsiCmnd->cdb[9];
  return tl;
}


/*****************************************************************************/
/*! \brief satComputeCDB16LBA.
 *
 *  This fuctions computes LBA of CDB16.
 *
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return
 *    - \e LBA
 */
/*****************************************************************************/
/*
  CBD16 has bit64 LBA
  But it has to be less than (2^28 - 1)
  Therefore, use last four bytes to compute LBA is OK
*/
bit32 satComputeCDB16LBA(satIOContext_t            *satIOContext)
{
  tiIniScsiCmnd_t           *scsiCmnd;
  tiScsiInitiatorRequest_t *tiScsiRequest;
  bit32                     lba = 0;

  TI_DBG5(("satComputeCDB10LBA: start\n"));
  tiScsiRequest = satIOContext->tiScsiXchg;
  scsiCmnd      = &(tiScsiRequest->scsiCmnd);

  lba = (scsiCmnd->cdb[6] << (8*3)) + (scsiCmnd->cdb[7] << (8*2))
    + (scsiCmnd->cdb[8] << 8) + scsiCmnd->cdb[9];

  return lba;
}

/*****************************************************************************/
/*! \brief satComputeCDB16TL.
 *
 *  This fuctions computes transfer length of CDB16.
 *
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return
 *    - \e TL
 */
/*****************************************************************************/
bit32 satComputeCDB16TL(satIOContext_t            *satIOContext)
{

  tiIniScsiCmnd_t           *scsiCmnd;
  tiScsiInitiatorRequest_t *tiScsiRequest;
  bit32                     tl = 0;

  TI_DBG5(("satComputeCDB10TL: start\n"));
  tiScsiRequest = satIOContext->tiScsiXchg;
  scsiCmnd      = &(tiScsiRequest->scsiCmnd);

  tl = (scsiCmnd->cdb[10] << (8*3)) + (scsiCmnd->cdb[11] << (8*2))
    + (scsiCmnd->cdb[12] << 8) + scsiCmnd->cdb[13];
  return tl;
}

/*****************************************************************************/
/*! \brief satComputeLoopNum.
 *
 *  This fuctions computes the number of interation needed for a transfer
 *  length with a specific number.
 *
 *  \param   a:   a numerator
 *  \param   b:   a denominator
 *
 *  \return
 *    - \e number of interation
 */
/*****************************************************************************/
/*
  (tl, denom)
  tl can be upto bit32 because CDB16 has bit32 tl
  Therefore, fine
  either (tl, 0xFF) or (tl, 0xFFFF)
*/
bit32 satComputeLoopNum(bit32 a, bit32 b)
{

  bit32 quo = 0, rem = 0;
  bit32 LoopNum = 0;

  TI_DBG5(("satComputeLoopNum: start\n"));

  quo = a/b;

  if (quo == 0)
  {
    LoopNum = 1;
  }
  else
  {
    rem = a % b;
    if (rem == 0)
    {
      LoopNum = quo;
    }
    else
    {
      LoopNum = quo + 1;
    }
  }

  return LoopNum;
}

/*****************************************************************************/
/*! \brief satAddNComparebit64.
 *
 *
 *
 *
 *  \param   a:   lba
 *  \param   b:   tl
 *
 *  \return
 *    - \e TRUE if (lba + tl > SAT_TR_LBA_LIMIT)
 *    - \e FALSE otherwise
 *  \note: a and b must be in the same length
 */
/*****************************************************************************/
/*
  input: bit8 a[8], bit8 b[8] (lba, tl) must be in same length
  if (lba + tl > SAT_TR_LBA_LIMIT)
  then returns true
  else returns false
  (LBA,TL)
*/
bit32 satAddNComparebit64(bit8 *a, bit8 *b)
{
  bit16 ans[8];       // 0 MSB, 8 LSB
  bit8  final_ans[9]; // 0 MSB, 9 LSB
  bit8  max[9];
  int i;

  TI_DBG5(("satAddNComparebit64: start\n"));

  osti_memset(ans, 0, sizeof(ans));
  osti_memset(final_ans, 0, sizeof(final_ans));
  osti_memset(max, 0, sizeof(max));

  max[0] = 0x1; //max = 0x1 0000 0000 0000 0000

  // adding from LSB to MSB
  for(i=7;i>=0;i--)
  {
    ans[i] = (bit16)(a[i] + b[i]);
    if (i != 7)
    {
      ans[i] = (bit16)(ans[i] + ((ans[i+1] & 0xFF00) >> 8));
    }
  }

  /*
    filling in the final answer
   */
  final_ans[0] = (bit8)(((ans[0] & 0xFF00) >> 8));
  final_ans[1] = (bit8)(ans[0] & 0xFF);

  for(i=2;i<=8;i++)
  {
    final_ans[i] = (bit8)(ans[i-1] & 0xFF);
  }

  //compare final_ans to max
  for(i=0;i<=8;i++)
  {
    if (final_ans[i] > max[i])
    {
      TI_DBG5(("satAddNComparebit64: yes at %d\n", i));
      return agTRUE;
    }
    else if (final_ans[i] < max[i])
    {
      TI_DBG5(("satAddNComparebit64: no at %d\n", i));
      return agFALSE;
    }
    else
    {
      continue;
    }
  }


  return agFALSE;
}

/*****************************************************************************/
/*! \brief satAddNComparebit32.
 *
 *
 *
 *
 *  \param   a:   lba
 *  \param   b:   tl
 *
 *  \return
 *    - \e TRUE if (lba + tl > SAT_TR_LBA_LIMIT)
 *    - \e FALSE otherwise
 *  \note: a and b must be in the same length
 */
/*****************************************************************************/
/*
  input: bit8 a[4], bit8 b[4] (lba, tl) must be in same length
  if (lba + tl > SAT_TR_LBA_LIMIT)
  then returns true
  else returns false
  (LBA,TL)
*/
bit32 satAddNComparebit32(bit8 *a, bit8 *b)
{
  bit16 ans[4];       // 0 MSB, 4 LSB
  bit8  final_ans[5]; // 0 MSB, 5 LSB
  bit8   max[4];
  int i;

  TI_DBG5(("satAddNComparebit32: start\n"));

  osti_memset(ans, 0, sizeof(ans));
  osti_memset(final_ans, 0, sizeof(final_ans));
  osti_memset(max, 0, sizeof(max));

  max[0] = 0x10; // max =0x1000 0000

  // adding from LSB to MSB
  for(i=3;i>=0;i--)
  {
    ans[i] = (bit16)(a[i] + b[i]);
    if (i != 3)
    {
      ans[i] = (bit16)(ans[i] + ((ans[i+1] & 0xFF00) >> 8));
    }
  }


  /*
    filling in the final answer
   */
  final_ans[0] = (bit8)(((ans[0] & 0xFF00) >> 8));
  final_ans[1] = (bit8)(ans[0] & 0xFF);

  for(i=2;i<=4;i++)
  {
    final_ans[i] = (bit8)(ans[i-1] & 0xFF);
  }

  //compare final_ans to max
  if (final_ans[0] != 0)
  {
    TI_DBG5(("satAddNComparebit32: yes bigger and out of range\n"));
    return agTRUE;
  }
  for(i=1;i<=4;i++)
  {
    if (final_ans[i] > max[i-1])
    {
      TI_DBG5(("satAddNComparebit32: yes at %d\n", i));
      return agTRUE;
    }
    else if (final_ans[i] < max[i-1])
    {
      TI_DBG5(("satAddNComparebit32: no at %d\n", i));
      return agFALSE;
    }
    else
    {
      continue;
    }
  }


  return agFALSE;;
}

/*****************************************************************************/
/*! \brief satCompareLBALimitbit.
 *
 *
 *
 *
 *  \param   lba:   lba
 *
 *  \return
 *    - \e TRUE if (lba > SAT_TR_LBA_LIMIT - 1)
 *    - \e FALSE otherwise
 *  \note: a and b must be in the same length
 */
/*****************************************************************************/

/*
  lba
*/
/*
  input: bit8 lba[8]
  if (lba > SAT_TR_LBA_LIMIT - 1)
  then returns true
  else returns false
  (LBA,TL)
*/
bit32 satCompareLBALimitbit(bit8 *lba)
{
  bit32 i;
  bit8 limit[8];

  /* limit is 0xF FF FF = 2^28 - 1 */
  limit[0] = 0x0;   /* MSB */
  limit[1] = 0x0;
  limit[2] = 0x0;
  limit[3] = 0x0;
  limit[4] = 0xF;
  limit[5] = 0xFF;
  limit[6] = 0xFF;
  limit[7] = 0xFF; /* LSB */

  //compare lba to limit
  for(i=0;i<8;i++)
  {
    if (lba[i] > limit[i])
    {
      TI_DBG5(("satCompareLBALimitbit64: yes at %d\n", i));
      return agTRUE;
    }
    else if (lba[i] < limit[i])
    {
      TI_DBG5(("satCompareLBALimitbit64: no at %d\n", i));
      return agFALSE;
    }
    else
    {
      continue;
    }
  }


  return agFALSE;

}
/*****************************************************************************
*! \brief
*  Purpose: bitwise set
*
*  Parameters:
*   data        - input output buffer
*   index       - bit to set
*
*  Return:
*   none
*
*****************************************************************************/
GLOBAL void
satBitSet(bit8 *data, bit32 index)
{
  data[index/8] |= (1 << (index%8));
}

/*****************************************************************************
*! \brief
*  Purpose: bitwise clear
*
*  Parameters:
*   data        - input output buffer
*   index       - bit to clear
*
*  Return:
*   none
*
*****************************************************************************/
GLOBAL void
satBitClear(bit8 *data, bit32 index)
{
  data[index/8] &= ~(1 << (index%8));
}

/*****************************************************************************
*! \brief
*  Purpose: bitwise test
*
*  Parameters:
*   data        - input output buffer
*   index       - bit to test
*
*  Return:
*   0 - not set
*   1 - set
*
*****************************************************************************/
GLOBAL agBOOLEAN
satBitTest(bit8 *data, bit32 index)
{
  return ( (BOOLEAN)((data[index/8] & (1 << (index%8)) ) ? 1: 0));
}


/******************************************************************************/
/*! \brief allocate an available SATA tag
 *
 *  allocate an available SATA tag
 *
 *  \param tiRoot           Pointer to TISA initiator driver/port instance.
 *  \param pSatDevData
 *  \param pTag
 *
 *  \return -Success or fail-
 */
/*******************************************************************************/
GLOBAL bit32 satTagAlloc(
                           tiRoot_t          *tiRoot,
                           satDeviceData_t   *pSatDevData,
                           bit8              *pTag
                           )
{
  bit32             retCode = agFALSE;
  bit32             i;

  tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
  for ( i = 0; i < pSatDevData->satNCQMaxIO; i ++ )
  {
    if ( 0 == satBitTest((bit8 *)&pSatDevData->freeSATAFDMATagBitmap, i) )
    {
      satBitSet((bit8*)&pSatDevData->freeSATAFDMATagBitmap, i);
      *pTag = (bit8) i;
      retCode = agTRUE;
      break;
    }
  }
  tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);
  return retCode;
}

/******************************************************************************/
/*! \brief release an SATA tag
 *
 *  release an available SATA tag
 *
 *  \param tiRoot           Pointer to TISA initiator driver/port instance.
 *  \param pSatDevData
 *  \param Tag
 *
 *  \return -the tag-
 */
/*******************************************************************************/
GLOBAL bit32 satTagRelease(
                              tiRoot_t          *tiRoot,
                              satDeviceData_t   *pSatDevData,
                              bit8              tag
                              )
{
  bit32             retCode = agFALSE;

  tdsaSingleThreadedEnter(tiRoot, TD_SATA_LOCK);
  if ( tag < pSatDevData->satNCQMaxIO )
  {
    satBitClear( (bit8 *)&pSatDevData->freeSATAFDMATagBitmap, (bit32)tag);
    retCode = agTRUE;
  }
  tdsaSingleThreadedLeave(tiRoot, TD_SATA_LOCK);
  return retCode;
}

/*****************************************************************************
 *! \brief  satSubTM
 *
 *   This routine is called to initiate a TM request to SATL.
 *   This routine is independent of HW/LL API.
 *
 *  \param  tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param  tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param  task:             SAM-3 task management request.
 *  \param  lun:              Pointer to LUN.
 *  \param  taskTag:          Pointer to the associated task where the TM
 *                            command is to be applied.
 *  \param  currentTaskTag:   Pointer to tag/context for this TM request.
 *  \param  NotifyOS          flag determines whether notify OS layer or not
 *
 *  \return:
 *
 *  \e tiSuccess:     I/O request successfully initiated.
 *  \e tiBusy:        No resources available, try again later.
 *  \e tiIONoDevice:  Invalid device handle.
 *  \e tiError:       Other errors that prevent the I/O request to be started.
 *
 *  \note:
 *        This funcion is triggered bottom up. Not yet in use.
 *****************************************************************************/
/* called for bottom up */
osGLOBAL bit32 satSubTM(
                        tiRoot_t          *tiRoot,
                        tiDeviceHandle_t  *tiDeviceHandle,
                        bit32             task,
                        tiLUN_t           *lun,
                        tiIORequest_t     *taskTag,
                        tiIORequest_t     *currentTaskTag,
                        bit32              NotifyOS
                        )
{
  void                        *osMemHandle;
  tdIORequestBody_t           *TMtdIORequestBody;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  agsaIORequest_t             *agIORequest = agNULL;

  TI_DBG6(("satSubTM: start\n"));

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
    TI_DBG1(("satSubTM: ostiAllocMemory failed... \n"));
    return tiError;
  }

  if (TMtdIORequestBody == agNULL)
  {
    TI_DBG1(("satSubTM: ostiAllocMemory returned NULL TMIORequestBody\n"));
    return tiError;
   }

  /* setup task management structure */
  TMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  TMtdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag = agNULL;
  TMtdIORequestBody->IOType.InitiatorTMIO.TaskTag = agNULL;

  /* initialize tiDevhandle */
  TMtdIORequestBody->tiDevHandle = tiDeviceHandle;

  /* initialize tiIORequest */
  TMtdIORequestBody->tiIORequest = agNULL;

  /* initialize agIORequest */
  agIORequest = &(TMtdIORequestBody->agIORequest);
  agIORequest->osData = (void *) TMtdIORequestBody;
  agIORequest->sdkData = agNULL; /* SA takes care of this */
  satTM(tiRoot,
        tiDeviceHandle,
        task, /* TD_INTERNAL_TM_RESET */
        agNULL,
        agNULL,
        agNULL,
        TMtdIORequestBody,
        agFALSE);

  return tiSuccess;
}


/*****************************************************************************/
/*! \brief SAT implementation for satStartResetDevice.
 *
 *  SAT implementation for sending SRT and send FIS request to LL layer.
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
 *  \note : triggerred by OS layer or bottom up
 */
/*****************************************************************************/
/* OS triggerred or bottom up */
GLOBAL bit32
satStartResetDevice(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest, /* currentTaskTag */
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest, /* should be NULL */
                            satIOContext_t            *satIOContext
                            )
{
  satInternalIo_t           *satIntIo = agNULL;
  satDeviceData_t           *satDevData = agNULL;
  satIOContext_t            *satNewIOContext;
  bit32                     status;
  tiIORequest_t             *currentTaskTag = agNULL;

  TI_DBG1(("satStartResetDevice: start\n"));

  currentTaskTag = tiIORequest;

  satDevData = satIOContext->pSatDevData;

  TI_DBG6(("satStartResetDevice: before alloc\n"));

  /* allocate any fis for seting SRT bit in device control */
  satIntIo = satAllocIntIoResource( tiRoot,
                                    tiIORequest,
                                    satDevData,
                                    0,
                                    satIntIo);

  TI_DBG6(("satStartResetDevice: before after\n"));

  if (satIntIo == agNULL)
  {
    TI_DBG1(("satStartResetDevice: can't alloacate\n"));
    if (satIOContext->NotifyOS)
    {
      ostiInitiatorEvent( tiRoot,
                          NULL,
                          NULL,
                          tiIntrEventTypeTaskManagement,
                          tiTMFailed,
                          currentTaskTag );
    }
    return tiError;
  }

  satNewIOContext = satPrepareNewIO(satIntIo,
                                    tiIORequest,
                                    satDevData,
                                    agNULL,
                                    satIOContext);

  TI_DBG6(("satStartResetDevice: OS satIOContext %p \n", satIOContext));
  TI_DBG6(("satStartResetDevice: TD satNewIOContext %p \n", satNewIOContext));
  TI_DBG6(("satStartResetDevice: OS tiScsiXchg %p \n", satIOContext->tiScsiXchg));
  TI_DBG6(("satStartResetDevice: TD tiScsiXchg %p \n", satNewIOContext->tiScsiXchg));



  TI_DBG6(("satStartResetDevice: satNewIOContext %p \n", satNewIOContext));

  if (satDevData->satDeviceType == SATA_ATAPI_DEVICE)
  {
    status = satDeviceReset(tiRoot,
                          &satIntIo->satIntTiIORequest, /* New tiIORequest */
                          tiDeviceHandle,
                          satNewIOContext->tiScsiXchg, /* New tiScsiInitiatorRequest_t *tiScsiRequest, */
                          satNewIOContext);
  }
  else
  {
    status = satResetDevice(tiRoot,
                          &satIntIo->satIntTiIORequest, /* New tiIORequest */
                          tiDeviceHandle,
                          satNewIOContext->tiScsiXchg, /* New tiScsiInitiatorRequest_t *tiScsiRequest, */
                          satNewIOContext);
  }

  if (status != tiSuccess)
  {
    TI_DBG1(("satStartResetDevice: failed in sending\n"));

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    if (satIOContext->NotifyOS)
    {
      ostiInitiatorEvent( tiRoot,
                          NULL,
                          NULL,
                          tiIntrEventTypeTaskManagement,
                          tiTMFailed,
                          currentTaskTag );
    }

    return tiError;
  }


  TI_DBG6(("satStartResetDevice: end\n"));

  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for satResetDevice.
 *
 *  SAT implementation for building SRT FIS and sends the request to LL layer.
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

/*
  create any fis and set SRST bit in device control
*/
GLOBAL bit32
satResetDevice(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;
#ifdef  TD_DEBUG_ENABLE
  tdIORequestBody_t         *tdIORequestBody;
  satInternalIo_t           *satIntIoContext;
#endif

  fis           = satIOContext->pFis;

  TI_DBG2(("satResetDevice: start\n"));

#ifdef  TD_DEBUG_ENABLE
  satIntIoContext = satIOContext->satIntIoContext;
  tdIORequestBody = satIntIoContext->satIntRequestBody;
#endif
  TI_DBG5(("satResetDevice: satIOContext %p tdIORequestBody %p\n", satIOContext, tdIORequestBody));
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

  satIOContext->satCompleteCB = &satResetDeviceCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

#ifdef TD_INTERNAL_DEBUG
  tdhexdump("satResetDevice", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
#ifdef  TD_DEBUG_ENABLE
  tdhexdump("satResetDevice LL", (bit8 *)&(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev), sizeof(agsaFisRegHostToDevice_t));
#endif
#endif

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG6(("satResetDevice: end status %d\n", status));
  return status;
}

/*****************************************************************************
*! \brief  satResetDeviceCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with SRT completion. This function send DSRT
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
GLOBAL void satResetDeviceCB(
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
  tdsaRootOsData_t   *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t           *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t         *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t      *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequestBody_t  *tdIORequestBody;
  tdIORequestBody_t  *tdOrgIORequestBody;
  satIOContext_t     *satIOContext;
  satIOContext_t     *satOrgIOContext;
  satIOContext_t     *satNewIOContext;
  satInternalIo_t    *satIntIo;
  satInternalIo_t    *satNewIntIo = agNULL;
  satDeviceData_t    *satDevData;
  tiIORequest_t      *tiOrgIORequest;
#ifdef  TD_DEBUG_ENABLE
  bit32                     ataStatus = 0;
  bit32                     ataError;
  agsaFisPioSetupHeader_t  *satPIOSetupHeader = agNULL;
#endif
  bit32                     status;

  TI_DBG1(("satResetDeviceCB: start\n"));
  TI_DBG6(("satResetDeviceCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  if (satIntIo == agNULL)
  {
    TI_DBG6(("satResetDeviceCB: External, OS generated\n"));
    satOrgIOContext      = satIOContext;
    tiOrgIORequest       = tdIORequestBody->tiIORequest;
  }
  else
  {
    TI_DBG6(("satResetDeviceCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG6(("satResetDeviceCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG6(("satResetDeviceCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody    = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest        = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satResetDeviceCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
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
    TI_DBG1(("satResetDeviceCB: OSSA_IO_OPEN_CNX_ERROR\n"));

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
#ifdef  TD_DEBUG_ENABLE
    /* only agsaFisPioSetup_t is expected */
    satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
    ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
    ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
    TI_DBG1(("satResetDeviceCB: ataStatus 0x%x ataError 0x%x\n", ataStatus, ataError));

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

  satNewIntIo = satAllocIntIoResource( tiRoot,
                                       tiOrgIORequest,
                                       satDevData,
                                       0,
                                       satNewIntIo);
  if (satNewIntIo == agNULL)
  {
    satDevData->satTmTaskTag = agNULL;

    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    /* memory allocation failure */
    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satNewIntIo);

    if (satOrgIOContext->NotifyOS == agTRUE)
    {
      ostiInitiatorEvent( tiRoot,
                          NULL,
                          NULL,
                          tiIntrEventTypeTaskManagement,
                          tiTMFailed,
                          tiOrgIORequest );
    }


      TI_DBG1(("satResetDeviceCB: momory allocation fails\n"));
      return;
    } /* end of memory allocation failure */

    /*
     * Need to initialize all the fields within satIOContext
     */

    satNewIOContext = satPrepareNewIO(
                                      satNewIntIo,
                                      tiOrgIORequest,
                                      satDevData,
                                      agNULL,
                                      satOrgIOContext
                                      );




    /* send AGSA_SATA_PROTOCOL_SRST_DEASSERT */
    status = satDeResetDevice(tiRoot,
                              tiOrgIORequest,
                              satOrgIOContext->ptiDeviceHandle,
                              agNULL,
                              satNewIOContext
                              );

    if (status != tiSuccess)
    {
      if (satOrgIOContext->NotifyOS == agTRUE)
      {
        ostiInitiatorEvent( tiRoot,
                            NULL,
                            NULL,
                            tiIntrEventTypeTaskManagement,
                            tiTMFailed,
                            tiOrgIORequest );
      }

      /* sending AGSA_SATA_PROTOCOL_SRST_DEASSERT fails */

      satDevData->satTmTaskTag = agNULL;

      satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

      satFreeIntIoResource( tiRoot,
                          satDevData,
                          satNewIntIo);
      return;

    }

  satDevData->satTmTaskTag = agNULL;

  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);
  TI_DBG5(("satResetDeviceCB: device %p pending IO %d\n", satDevData, satDevData->satPendingIO));
  TI_DBG6(("satResetDeviceCB: end\n"));
  return;

}


/*****************************************************************************/
/*! \brief SAT implementation for satDeResetDevice.
 *
 *  SAT implementation for building DSRT FIS and sends the request to LL layer.
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
GLOBAL bit32  satDeResetDevice(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            )
{
  bit32                     status;
  bit32                     agRequestType;
  agsaFisRegHostToDevice_t  *fis;
#ifdef  TD_DEBUG_ENABLE
  tdIORequestBody_t         *tdIORequestBody;
  satInternalIo_t           *satIntIoContext;
#endif
  fis           = satIOContext->pFis;

  TI_DBG6(("satDeResetDevice: start\n"));

#ifdef  TD_DEBUG_ENABLE
  satIntIoContext = satIOContext->satIntIoContext;
  tdIORequestBody = satIntIoContext->satIntRequestBody;
  TI_DBG5(("satDeResetDevice: satIOContext %p tdIORequestBody %p\n", satIOContext, tdIORequestBody));
#endif
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

  satIOContext->satCompleteCB = &satDeResetDeviceCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

#ifdef TD_INTERNAL_DEBUG
  tdhexdump("satDeResetDevice", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
#ifdef  TD_DEBUG_ENABLE
  tdhexdump("satDeResetDevice LL", (bit8 *)&(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev), sizeof(agsaFisRegHostToDevice_t));
#endif
#endif

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG6(("satDeResetDevice: end status %d\n", status));
  return status;

}

/*****************************************************************************
*! \brief  satDeResetDeviceCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with DSRT completion.
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
GLOBAL void satDeResetDeviceCB(
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
  tiIORequest_t           *tiOrgIORequest;
#ifdef  TD_DEBUG_ENABLE
  bit32                    ataStatus = 0;
  bit32                    ataError;
  agsaFisPioSetupHeader_t *satPIOSetupHeader = agNULL;
#endif
  bit32                     report = agFALSE;
  bit32                     AbortTM = agFALSE;

  TI_DBG1(("satDeResetDeviceCB: start\n"));
  TI_DBG6(("satDeResetDeviceCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  if (satIntIo == agNULL)
  {
    TI_DBG6(("satDeResetDeviceCB: External, OS generated\n"));
    satOrgIOContext      = satIOContext;
    tiOrgIORequest       = tdIORequestBody->tiIORequest;
  }
  else
  {
    TI_DBG6(("satDeResetDeviceCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG6(("satDeResetDeviceCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG6(("satDeResetDeviceCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
  }

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satDeResetDeviceCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
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
    TI_DBG1(("satDeResetDeviceCB: OSSA_IO_OPEN_CNX_ERROR\n"));

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
#ifdef  TD_DEBUG_ENABLE
    /* only agsaFisPioSetup_t is expected */
    satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
    ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
    ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
    TI_DBG1(("satDeResetDeviceCB: ataStatus 0x%x ataError 0x%x\n", ataStatus, ataError));

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
  TI_DBG1(("satDeResetDeviceCB: success \n"));
  TI_DBG1(("satDeResetDeviceCB: TMF %d\n", satOrgIOContext->TMF));

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

  TI_DBG1(("satDeResetDeviceCB: satPendingIO %d satNCQMaxIO %d\n", satDevData->satPendingIO, satDevData->satNCQMaxIO ));
  TI_DBG1(("satDeResetDeviceCB: satPendingNCQIO %d satPendingNONNCQIO %d\n", satDevData->satPendingNCQIO, satDevData->satPendingNONNCQIO));

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
    TI_DBG1(("satDeResetDeviceCB: tdOrgIORequestBody is NULL, wrong\n"));
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


  TI_DBG5(("satDeResetDeviceCB: device %p pending IO %d\n", satDevData, satDevData->satPendingIO));
  TI_DBG6(("satDeResetDeviceCB: end\n"));
  return;

}

/*****************************************************************************/
/*! \brief SAT implementation for satStartCheckPowerMode.
 *
 *  SAT implementation for abort task management for non-ncq sata disk.
 *  This function sends CHECK POWER MODE
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
GLOBAL bit32  satStartCheckPowerMode(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t  *tiScsiRequest, /* NULL */
                            satIOContext_t            *satIOContext
                            )
{
  satInternalIo_t           *satIntIo = agNULL;
  satDeviceData_t           *satDevData = agNULL;
  satIOContext_t            *satNewIOContext;
  bit32                     status;
  tiIORequest_t             *currentTaskTag = agNULL;

  TI_DBG6(("satStartCheckPowerMode: start\n"));

  currentTaskTag = tiIORequest;

  satDevData = satIOContext->pSatDevData;

  TI_DBG6(("satStartCheckPowerMode: before alloc\n"));

  /* allocate any fis for seting SRT bit in device control */
  satIntIo = satAllocIntIoResource( tiRoot,
                                    tiIORequest,
                                    satDevData,
                                    0,
                                    satIntIo);

  TI_DBG6(("satStartCheckPowerMode: before after\n"));

  if (satIntIo == agNULL)
  {
    TI_DBG1(("satStartCheckPowerMode: can't alloacate\n"));
    if (satIOContext->NotifyOS)
    {
      ostiInitiatorEvent( tiRoot,
                          NULL,
                          NULL,
                          tiIntrEventTypeTaskManagement,
                          tiTMFailed,
                          currentTaskTag );
    }
    return tiError;
  }

  satNewIOContext = satPrepareNewIO(satIntIo,
                                    tiIORequest,
                                    satDevData,
                                    agNULL,
                                    satIOContext);

  TI_DBG6(("satStartCheckPowerMode: OS satIOContext %p \n", satIOContext));
  TI_DBG6(("satStartCheckPowerMode: TD satNewIOContext %p \n", satNewIOContext));
  TI_DBG6(("satStartCheckPowerMode: OS tiScsiXchg %p \n", satIOContext->tiScsiXchg));
  TI_DBG6(("satStartCheckPowerMode: TD tiScsiXchg %p \n", satNewIOContext->tiScsiXchg));



  TI_DBG1(("satStartCheckPowerMode: satNewIOContext %p \n", satNewIOContext));

  status = satCheckPowerMode(tiRoot,
                             &satIntIo->satIntTiIORequest, /* New tiIORequest */
                             tiDeviceHandle,
                             satNewIOContext->tiScsiXchg, /* New tiScsiInitiatorRequest_t *tiScsiRequest, */
                             satNewIOContext);

  if (status != tiSuccess)
  {
    TI_DBG1(("satStartCheckPowerMode: failed in sending\n"));

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);
    if (satIOContext->NotifyOS)
    {
      ostiInitiatorEvent( tiRoot,
                          NULL,
                          NULL,
                          tiIntrEventTypeTaskManagement,
                          tiTMFailed,
                          currentTaskTag );
    }

    return tiError;
  }


  TI_DBG6(("satStartCheckPowerMode: end\n"));

  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for satCheckPowerMode.
 *
 *  This function creates CHECK POWER MODE fis and sends the request to LL layer
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
GLOBAL bit32  satCheckPowerMode(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
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
  TI_DBG5(("satCheckPowerMode: start\n"));
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
  satIOContext->satCompleteCB = &satCheckPowerModeCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG5(("satCheckPowerMode: return\n"));

  return status;
}

/*****************************************************************************
*! \brief  satCheckPowerModeCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with CHECK POWER MODE completion as abort task
*   management.
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
GLOBAL void satCheckPowerModeCB(
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

  tiIORequest_t             *tiOrgIORequest;
#ifdef  TD_DEBUG_ENABLE
  bit32                     ataStatus = 0;
  bit32                     ataError;
  agsaFisPioSetupHeader_t   *satPIOSetupHeader = agNULL;
#endif
  bit32                     report = agFALSE;
  bit32                     AbortTM = agFALSE;


  TI_DBG1(("satCheckPowerModeCB: start\n"));

  TI_DBG1(("satCheckPowerModeCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));

  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  if (satIntIo == agNULL)
  {
    TI_DBG6(("satCheckPowerModeCB: External, OS generated\n"));
    satOrgIOContext      = satIOContext;
    tiOrgIORequest       = tdIORequestBody->tiIORequest;
  }
  else
  {
    TI_DBG6(("satCheckPowerModeCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG6(("satCheckPowerModeCB: satOrgIOContext is NULL, wrong\n"));
      return;
    }
    else
    {
      TI_DBG6(("satCheckPowerModeCB: satOrgIOContext is NOT NULL\n"));
    }
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tiOrgIORequest         = (tiIORequest_t *)tdOrgIORequestBody->tiIORequest;
  }


  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satCheckPowerModeCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));

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
    TI_DBG1(("satCheckPowerModeCB: OSSA_IO_OPEN_CNX_ERROR\n"));

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
#ifdef  TD_DEBUG_ENABLE
    /* only agsaFisPioSetup_t is expected */
    satPIOSetupHeader = (agsaFisPioSetupHeader_t *)&(agFirstDword->PioSetup);
    ataStatus     = satPIOSetupHeader->status;   /* ATA Status register */
    ataError      = satPIOSetupHeader->error;    /* ATA Eror register   */
#endif
    TI_DBG1(("satCheckPowerModeCB: ataStatus 0x%x ataError 0x%x\n", ataStatus, ataError));

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
  TI_DBG1(("satCheckPowerModeCB: success\n"));
  TI_DBG1(("satCheckPowerModeCB: TMF %d\n", satOrgIOContext->TMF));

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
    TI_DBG1(("satCheckPowerModeCB: calling satAbort\n"));
    satAbort(agRoot, satOrgIOContext->satToBeAbortedIOContext);
  }
  satDevData->satTmTaskTag = agNULL;

  satDevData->satDriveState = SAT_DEV_STATE_NORMAL;

  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  TI_DBG1(("satCheckPowerModeCB: satPendingIO %d satNCQMaxIO %d\n", satDevData->satPendingIO, satDevData->satNCQMaxIO ));
  TI_DBG1(("satCheckPowerModeCB: satPendingNCQIO %d satPendingNONNCQIO %d\n", satDevData->satPendingNCQIO, satDevData->satPendingNONNCQIO));

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
    TI_DBG1(("satCheckPowerModeCB: tdOrgIORequestBody is NULL, wrong\n"));
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

  TI_DBG5(("satCheckPowerModeCB: device %p pending IO %d\n", satDevData, satDevData->satPendingIO));
  TI_DBG2(("satCheckPowerModeCB: end\n"));
  return;

}

/*****************************************************************************/
/*! \brief SAT implementation for satAddSATAStartIDDev.
 *
 *  This function sends identify device data to find out the uniqueness
 *  of device.
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
GLOBAL bit32  satAddSATAStartIDDev(
                               tiRoot_t                  *tiRoot,
                               tiIORequest_t             *tiIORequest,
                               tiDeviceHandle_t          *tiDeviceHandle,
                               tiScsiInitiatorRequest_t  *tiScsiRequest, // NULL
                               satIOContext_t            *satIOContext
                            )
{
  satInternalIo_t           *satIntIo = agNULL;
  satDeviceData_t           *satDevData = agNULL;
  tdIORequestBody_t         *tdIORequestBody;
  satIOContext_t            *satNewIOContext;
  bit32                     status;

  TI_DBG2(("satAddSATAStartIDDev: start\n"));

  satDevData = satIOContext->pSatDevData;

  TI_DBG2(("satAddSATAStartIDDev: before alloc\n"));

  /* allocate identify device command */
  satIntIo = satAllocIntIoResource( tiRoot,
                                    tiIORequest,
                                    satDevData,
                                    sizeof(agsaSATAIdentifyData_t), /* 512; size of identify device data */
                                    satIntIo);

  TI_DBG2(("satAddSATAStartIDDev: after alloc\n"));

  if (satIntIo == agNULL)
  {
    TI_DBG1(("satAddSATAStartIDDev: can't alloacate\n"));

    return tiError;
  }

  /* fill in fields */
  /* real ttttttthe one worked and the same; 5/21/07/ */
  satIntIo->satOrgTiIORequest = tiIORequest; /* changed */
  tdIORequestBody = satIntIo->satIntRequestBody;
  satNewIOContext = &(tdIORequestBody->transport.SATA.satIOContext);

  satNewIOContext->pSatDevData   = satDevData;
  satNewIOContext->pFis          = &(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satNewIOContext->pScsiCmnd     = &(satIntIo->satIntTiScsiXchg.scsiCmnd);
  satNewIOContext->pSense        = &(tdIORequestBody->transport.SATA.sensePayload);
  satNewIOContext->pTiSenseData  = &(tdIORequestBody->transport.SATA.tiSenseData);
  satNewIOContext->tiRequestBody = satIntIo->satIntRequestBody; /* key fix */
  satNewIOContext->interruptContext = tiInterruptContext;
  satNewIOContext->satIntIoContext  = satIntIo;

  satNewIOContext->ptiDeviceHandle = agNULL;
  satNewIOContext->satOrgIOContext = satIOContext; /* changed */

  /* this is valid only for TD layer generated (not triggered by OS at all) IO */
  satNewIOContext->tiScsiXchg = &(satIntIo->satIntTiScsiXchg);


  TI_DBG6(("satAddSATAStartIDDev: OS satIOContext %p \n", satIOContext));
  TI_DBG6(("satAddSATAStartIDDev: TD satNewIOContext %p \n", satNewIOContext));
  TI_DBG6(("satAddSATAStartIDDev: OS tiScsiXchg %p \n", satIOContext->tiScsiXchg));
  TI_DBG6(("satAddSATAStartIDDev: TD tiScsiXchg %p \n", satNewIOContext->tiScsiXchg));



  TI_DBG2(("satAddSATAStartIDDev: satNewIOContext %p tdIORequestBody %p\n", satNewIOContext, tdIORequestBody));

  status = satAddSATASendIDDev( tiRoot,
                                &satIntIo->satIntTiIORequest, /* New tiIORequest */
                                tiDeviceHandle,
                                satNewIOContext->tiScsiXchg, /* New tiScsiInitiatorRequest_t *tiScsiRequest, */
                                satNewIOContext);

  if (status != tiSuccess)
  {
    TI_DBG1(("satAddSATAStartIDDev: failed in sending\n"));

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    return tiError;
  }


  TI_DBG6(("satAddSATAStartIDDev: end\n"));

  return status;


}

/*****************************************************************************/
/*! \brief SAT implementation for satAddSATASendIDDev.
 *
 *  This function creates identify device data fis and send it to LL
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
GLOBAL bit32  satAddSATASendIDDev(
                           tiRoot_t                  *tiRoot,
                           tiIORequest_t             *tiIORequest,
                           tiDeviceHandle_t          *tiDeviceHandle,
                           tiScsiInitiatorRequest_t  *tiScsiRequest,
                           satIOContext_t            *satIOContext)
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
#ifdef  TD_DEBUG_ENABLE
  tdIORequestBody_t         *tdIORequestBody;
  satInternalIo_t           *satIntIoContext;
#endif

  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;
  TI_DBG2(("satAddSATASendIDDev: start\n"));
#ifdef  TD_DEBUG_ENABLE
  satIntIoContext = satIOContext->satIntIoContext;
  tdIORequestBody = satIntIoContext->satIntRequestBody;
#endif
  TI_DBG5(("satAddSATASendIDDev: satIOContext %p tdIORequestBody %p\n", satIOContext, tdIORequestBody));

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
  satIOContext->satCompleteCB = &satAddSATAIDDevCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

#ifdef TD_INTERNAL_DEBUG
  tdhexdump("satAddSATASendIDDev", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
#ifdef  TD_DEBUG_ENABLE
  tdhexdump("satAddSATASendIDDev LL", (bit8 *)&(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev), sizeof(agsaFisRegHostToDevice_t));
#endif
#endif

  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);

  TI_DBG2(("satAddSATASendIDDev: end status %d\n", status));
  return status;
}

/*****************************************************************************
*! \brief  satAddSATAIDDevCB
*
*   This routine is a callback function for satAddSATASendIDDev()
*   Using Identify Device Data, this function finds whether devicedata is
*   new or old. If new, add it to the devicelist.
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
void satAddSATAIDDevCB(
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
  tiIORequest_t           *tiOrgIORequest = agNULL;
  agsaSATAIdentifyData_t    *pSATAIdData;
  bit16                     *tmpptr, tmpptr_tmp;
  bit32                     x;
  tdsaDeviceData_t          *NewOneDeviceData = agNULL;
  tdsaDeviceData_t          *oneDeviceData = agNULL;
  tdList_t                  *DeviceListList;
  int                       new_device = agTRUE;
  bit8                      PhyID;
  void                      *sglVirtualAddr;
  bit32                     retry_status;
  agsaContext_t             *agContext;
  tdsaPortContext_t         *onePortContext;
  bit32                     status = 0;

  TI_DBG2(("satAddSATAIDDevCB: start\n"));
  TI_DBG6(("satAddSATAIDDevCB: agIORequest=%p agIOStatus=0x%x agIOInfoLen %d\n", agIORequest, agIOStatus, agIOInfoLen));
  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;

  NewOneDeviceData = (tdsaDeviceData_t *)tdIORequestBody->tiDevHandle->tdData;
  TI_DBG2(("satAddSATAIDDevCB: NewOneDeviceData %p did %d\n", NewOneDeviceData, NewOneDeviceData->id));
  PhyID = NewOneDeviceData->phyID;
  TI_DBG2(("satAddSATAIDDevCB: phyID %d\n", PhyID));
  agContext = &(NewOneDeviceData->agDeviceResetContext);
  agContext->osData = agNULL;
  if (satIntIo == agNULL)
  {
    TI_DBG1(("satAddSATAIDDevCB: External, OS generated\n"));
    TI_DBG1(("satAddSATAIDDevCB: Not possible case\n"));
    satOrgIOContext      = satIOContext;
    tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
    tdsaAbortAll(tiRoot, agRoot, NewOneDeviceData);

    /* put onedevicedata back to free list */
    osti_memset(&(NewOneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
    TDLIST_DEQUEUE_THIS(&(NewOneDeviceData->MainLink));
    TDLIST_ENQUEUE_AT_TAIL(&(NewOneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));

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
    return;
  }
  else
  {
    TI_DBG1(("satAddSATAIDDevCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG6(("satAddSATAIDDevCB: satOrgIOContext is NULL\n"));
      return;
    }
    else
    {
      TI_DBG6(("satAddSATAIDDevCB: satOrgIOContext is NOT NULL\n"));
      tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
      sglVirtualAddr         = satIntIo->satIntTiScsiXchg.sglVirtualAddr;
    }
  }
  tiOrgIORequest           = tdIORequestBody->tiIORequest;

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;
  TI_DBG2(("satAddSATAIDDevCB: satOrgIOContext->pid %d\n", satOrgIOContext->pid));
  /* protect against double completion for old port */
  if (satOrgIOContext->pid != tdsaAllShared->Ports[PhyID].portContext->id)
  {
    TI_DBG2(("satAddSATAIDDevCB: incorrect pid\n"));
    TI_DBG2(("satAddSATAIDDevCB: satOrgIOContext->pid %d\n", satOrgIOContext->pid));
    TI_DBG2(("satAddSATAIDDevCB: tiPortalContext pid %d\n", tdsaAllShared->Ports[PhyID].portContext->id));
    tdsaAbortAll(tiRoot, agRoot, NewOneDeviceData);
    /* put onedevicedata back to free list */
    osti_memset(&(NewOneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
    TDLIST_DEQUEUE_THIS(&(NewOneDeviceData->MainLink));
    TDLIST_ENQUEUE_AT_TAIL(&(NewOneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
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
    /* no notification to OS layer */
    return;
  }
  /* completion after portcontext is invalidated */
  onePortContext = NewOneDeviceData->tdPortContext;
  if (onePortContext != agNULL)
  {
    if (onePortContext->valid == agFALSE)
    {
      TI_DBG1(("satAddSATAIDDevCB: portcontext is invalid\n"));
      TI_DBG1(("satAddSATAIDDevCB: onePortContext->id pid %d\n", onePortContext->id));
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
      /* no notification to OS layer */
      return;
    }
  }
  else
  {
    TI_DBG1(("satAddSATAIDDevCB: onePortContext is NULL!!!\n"));
    return;
  }
  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("satAddSATAIDDevCB: wrong. agFirstDword is NULL when error, status %d\n", agIOStatus));
    if (tdsaAllShared->ResetInDiscovery != 0 && satDevData->ID_Retries < SATA_ID_DEVICE_DATA_RETRIES)
    {
      satDevData->satPendingNONNCQIO--;
      satDevData->satPendingIO--;
      retry_status = sataLLIOStart(tiRoot,
                                   &satIntIo->satIntTiIORequest,
                                   &(NewOneDeviceData->tiDeviceHandle),
                                   satIOContext->tiScsiXchg,
                                   satIOContext);
      if (retry_status != tiSuccess)
      {
        /* simply give up */
        satDevData->ID_Retries = 0;
        satAddSATAIDDevCBCleanup(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
        return;
      }
      satDevData->ID_Retries++;
      tdIORequestBody->ioCompleted = agFALSE;
      tdIORequestBody->ioStarted = agTRUE;
      return;
    }
    else
    {
      if (tdsaAllShared->ResetInDiscovery == 0)
      {
        satAddSATAIDDevCBCleanup(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
      }
      else /* ResetInDiscovery in on */
      {
        /* RESET only one after ID retries */
        if (satDevData->NumOfIDRetries <= 0)
        {
          satDevData->NumOfIDRetries++;
          satDevData->ID_Retries = 0;
          satAddSATAIDDevCBReset(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
          /* send link reset */
          saLocalPhyControl(agRoot,
                            agContext,
                            tdsaRotateQnumber(tiRoot, NewOneDeviceData),
                            PhyID,
                            AGSA_PHY_HARD_RESET,
                            agNULL);
        }
        else
        {
          satDevData->ID_Retries = 0;
          satAddSATAIDDevCBCleanup(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
        }
      }
      return;
    }
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
    TI_DBG1(("satAddSATAIDDevCB: OSSA_IO_OPEN_CNX_ERROR\n"));
    if (tdsaAllShared->ResetInDiscovery != 0 && satDevData->ID_Retries < SATA_ID_DEVICE_DATA_RETRIES)
    {
      satDevData->satPendingNONNCQIO--;
      satDevData->satPendingIO--;
      retry_status = sataLLIOStart(tiRoot,
                                   &satIntIo->satIntTiIORequest,
                                   &(NewOneDeviceData->tiDeviceHandle),
                                   satIOContext->tiScsiXchg,
                                   satIOContext);
      if (retry_status != tiSuccess)
      {
        /* simply give up */
        satDevData->ID_Retries = 0;
        satAddSATAIDDevCBCleanup(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
        return;
      }
      satDevData->ID_Retries++;
      tdIORequestBody->ioCompleted = agFALSE;
      tdIORequestBody->ioStarted = agTRUE;
      return;
    }
    else
    {
      if (tdsaAllShared->ResetInDiscovery == 0)
      {
        satAddSATAIDDevCBCleanup(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
      }
      else /* ResetInDiscovery in on */
      {
        /* RESET only one after ID retries */
        if (satDevData->NumOfIDRetries <= 0)
        {
          satDevData->NumOfIDRetries++;
          satDevData->ID_Retries = 0;
          satAddSATAIDDevCBReset(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
          /* send link reset */
          saLocalPhyControl(agRoot,
                            agContext,
                            tdsaRotateQnumber(tiRoot, NewOneDeviceData),
                            PhyID,
                            AGSA_PHY_HARD_RESET,
                            agNULL);
        }
        else
        {
          satDevData->ID_Retries = 0;
          satAddSATAIDDevCBCleanup(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
        }
      }
      return;
    }
  }

  if ( agIOStatus != OSSA_IO_SUCCESS ||
      (agIOStatus == OSSA_IO_SUCCESS && agFirstDword != agNULL && agIOInfoLen != 0)
    )
  {
    if (tdsaAllShared->ResetInDiscovery != 0 && satDevData->ID_Retries < SATA_ID_DEVICE_DATA_RETRIES)
    {
      satIOContext->pSatDevData->satPendingNONNCQIO--;
      satIOContext->pSatDevData->satPendingIO--;
      retry_status = sataLLIOStart(tiRoot,
                                   &satIntIo->satIntTiIORequest,
                                   &(NewOneDeviceData->tiDeviceHandle),
                                   satIOContext->tiScsiXchg,
                                   satIOContext);
      if (retry_status != tiSuccess)
      {
        /* simply give up */
        satDevData->ID_Retries = 0;
        satAddSATAIDDevCBCleanup(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
        return;
      }
      satDevData->ID_Retries++;
      tdIORequestBody->ioCompleted = agFALSE;
      tdIORequestBody->ioStarted = agTRUE;
      return;
    }
    else
    {
      if (tdsaAllShared->ResetInDiscovery == 0)
      {
        satAddSATAIDDevCBCleanup(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
      }
      else /* ResetInDiscovery in on */
      {
        /* RESET only one after ID retries */
        if (satDevData->NumOfIDRetries <= 0)
        {
          satDevData->NumOfIDRetries++;
          satDevData->ID_Retries = 0;
          satAddSATAIDDevCBReset(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
          /* send link reset */
          saLocalPhyControl(agRoot,
                            agContext,
                            tdsaRotateQnumber(tiRoot, NewOneDeviceData),
                            PhyID,
                            AGSA_PHY_HARD_RESET,
                            agNULL);
        }
        else
        {
          satDevData->ID_Retries = 0;
          satAddSATAIDDevCBCleanup(agRoot, NewOneDeviceData, satIOContext, tdOrgIORequestBody);
        }
      }
      return;
    }
  }

  /* success */
  TI_DBG2(("satAddSATAIDDevCB: Success\n"));
  /* Convert to host endian */
  tmpptr = (bit16*)sglVirtualAddr;
  //tdhexdump("satAddSATAIDDevCB before", (bit8 *)sglVirtualAddr, sizeof(agsaSATAIdentifyData_t));
  for (x=0; x < sizeof(agsaSATAIdentifyData_t)/sizeof(bit16); x++)
  {
   OSSA_READ_LE_16(AGROOT, &tmpptr_tmp, tmpptr, 0);
   *tmpptr = tmpptr_tmp;
   tmpptr++;
    /*Print tmpptr_tmp here for debugging purpose*/
  }

  pSATAIdData = (agsaSATAIdentifyData_t *)sglVirtualAddr;
  //tdhexdump("satAddSATAIDDevCB after", (bit8 *)pSATAIdData, sizeof(agsaSATAIdentifyData_t));

  TI_DBG5(("satAddSATAIDDevCB: OS satOrgIOContext %p \n", satOrgIOContext));
  TI_DBG5(("satAddSATAIDDevCB: TD satIOContext %p \n", satIOContext));
  TI_DBG5(("satAddSATAIDDevCB: OS tiScsiXchg %p \n", satOrgIOContext->tiScsiXchg));
  TI_DBG5(("satAddSATAIDDevCB: TD tiScsiXchg %p \n", satIOContext->tiScsiXchg));


  /* compare idenitfy device data to the exiting list */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    TI_DBG1(("satAddSATAIDDevCB: LOOP oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
    //tdhexdump("satAddSATAIDDevCB LOOP", (bit8 *)&oneDeviceData->satDevData.satIdentifyData, sizeof(agsaSATAIdentifyData_t));

    /* what is unique ID for sata device -> response of identify devicedata; not really
       Let's compare serial number, firmware version, model number
    */
    if ( oneDeviceData->DeviceType == TD_SATA_DEVICE &&
         (osti_memcmp (oneDeviceData->satDevData.satIdentifyData.serialNumber,
                       pSATAIdData->serialNumber,
                       20) == 0) &&
         (osti_memcmp (oneDeviceData->satDevData.satIdentifyData.firmwareVersion,
                       pSATAIdData->firmwareVersion,
                       8) == 0) &&
         (osti_memcmp (oneDeviceData->satDevData.satIdentifyData.modelNumber,
                       pSATAIdData->modelNumber,
                       40) == 0)
       )
    {
      TI_DBG2(("satAddSATAIDDevCB: did %d\n", oneDeviceData->id));
      new_device = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }

  if (new_device == agFALSE)
  {
    TI_DBG2(("satAddSATAIDDevCB: old device data\n"));
    oneDeviceData->valid = agTRUE;
    oneDeviceData->valid2 = agTRUE;
    /* save data field from new device data */
    oneDeviceData->agRoot = agRoot;
    oneDeviceData->agDevHandle = NewOneDeviceData->agDevHandle;
    oneDeviceData->agDevHandle->osData = oneDeviceData; /* TD layer */
    oneDeviceData->tdPortContext = NewOneDeviceData->tdPortContext;
    oneDeviceData->phyID = NewOneDeviceData->phyID;

    /*
      one SATA directly attached device per phy;
      Therefore, deregister then register
    */
    saDeregisterDeviceHandle(agRoot, agNULL, NewOneDeviceData->agDevHandle, 0);

    if (oneDeviceData->registered == agFALSE)
    {
      TI_DBG2(("satAddSATAIDDevCB: re-registering old device data\n"));
      /* already has old information; just register it again */
      saRegisterNewDevice( /* satAddSATAIDDevCB */
                          agRoot,
                          &oneDeviceData->agContext,
                          tdsaRotateQnumber(tiRoot, oneDeviceData),
                          &oneDeviceData->agDeviceInfo,
                          oneDeviceData->tdPortContext->agPortContext,
                          0
                          );
    }

//    tdsaAbortAll(tiRoot, agRoot, NewOneDeviceData);
    /* put onedevicedata back to free list */
    osti_memset(&(NewOneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
    TDLIST_DEQUEUE_THIS(&(NewOneDeviceData->MainLink));
    TDLIST_ENQUEUE_AT_TAIL(&(NewOneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
    satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    if (satDevData->satDeviceType == SATA_ATAPI_DEVICE)
    {
      /* send the Set Feature ATA command to ATAPI device for enbling PIO and DMA transfer mode*/
      satNewIntIo = satAllocIntIoResource( tiRoot,
                                       tiOrgIORequest,
                                       satDevData,
                                       0,
                                       satNewIntIo);

      if (satNewIntIo == agNULL)
      {
        TI_DBG1(("tdsaDiscoveryStartIDDevCB: momory allocation fails\n"));
          /* clean up TD layer's IORequestBody */
        ostiFreeMemory(
                     tiRoot,
                     tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
        return;
      } /* end memory allocation */

      satNewIOContext = satPrepareNewIO(satNewIntIo,
                                        tiOrgIORequest,
                                        satDevData,
                                        agNULL,
                                        satOrgIOContext
                                        );
      /* enable PIO mode, then enable Ultra DMA mode in the satSetFeaturesCB callback function*/
      status = satSetFeatures(tiRoot,
                     &satNewIntIo->satIntTiIORequest,
                     satNewIOContext->ptiDeviceHandle,
                     &satNewIntIo->satIntTiScsiXchg, /* orginal from OS layer */
                     satNewIOContext,
                     agFALSE);
      if (status != tiSuccess)
      {
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
    }
    else
    {
      /* clean up TD layer's IORequestBody */
      ostiFreeMemory(
                   tiRoot,
                   tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
      TI_DBG2(("satAddSATAIDDevCB: pid %d\n", tdsaAllShared->Ports[PhyID].portContext->id));
      /* notifying link up */
      ostiPortEvent(
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
    return;
  }

  TI_DBG2(("satAddSATAIDDevCB: new device data\n"));
  /* copy ID Dev data to satDevData */
  satDevData->satIdentifyData = *pSATAIdData;


  satDevData->IDDeviceValid = agTRUE;
#ifdef TD_INTERNAL_DEBUG
  tdhexdump("satAddSATAIDDevCB ID Dev data",(bit8 *)pSATAIdData, sizeof(agsaSATAIdentifyData_t));
  tdhexdump("satAddSATAIDDevCB Device ID Dev data",(bit8 *)&satDevData->satIdentifyData, sizeof(agsaSATAIdentifyData_t));
#endif

  /* set satDevData fields from IndentifyData */
  satSetDevInfo(satDevData,pSATAIdData);
  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

  if (satDevData->satDeviceType == SATA_ATAPI_DEVICE)
  {
      /* send the Set Feature ATA command to ATAPI device for enbling PIO and DMA transfer mode*/
      satNewIntIo = satAllocIntIoResource( tiRoot,
                                       tiOrgIORequest,
                                       satDevData,
                                       0,
                                       satNewIntIo);

      if (satNewIntIo == agNULL)
      {
        TI_DBG1(("tdsaDiscoveryStartIDDevCB: momory allocation fails\n"));
          /* clean up TD layer's IORequestBody */
        ostiFreeMemory(
                     tiRoot,
                     tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
        return;
      } /* end memory allocation */

      satNewIOContext = satPrepareNewIO(satNewIntIo,
                                        tiOrgIORequest,
                                        satDevData,
                                        agNULL,
                                        satOrgIOContext
                                        );
      /* enable PIO mode, then enable Ultra DMA mode in the satSetFeaturesCB callback function*/
      status = satSetFeatures(tiRoot,
                     &satNewIntIo->satIntTiIORequest,
                     satNewIOContext->ptiDeviceHandle,
                     &satNewIntIo->satIntTiScsiXchg, /* orginal from OS layer */
                     satNewIOContext,
                     agFALSE);
      if (status != tiSuccess)
      {
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

  }
  else
  {
       /* clean up TD layer's IORequestBody */
      ostiFreeMemory(
                     tiRoot,
                     tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );

      TI_DBG2(("satAddSATAIDDevCB: pid %d\n", tdsaAllShared->Ports[PhyID].portContext->id));
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

 TI_DBG2(("satAddSATAIDDevCB: end\n"));
 return;

}

/*****************************************************************************
*! \brief  satAddSATAIDDevCBReset
*
*   This routine cleans up IOs for failed Identify device data
*
*  \param   agRoot:           Handles for this instance of SAS/SATA hardware
*  \param   oneDeviceData:    Pointer to the device data.
*  \param   ioContext:        Pointer to satIOContext_t.
*  \param   tdIORequestBody:  Pointer to the request body
*  \param   flag:             Decrement pending io or not
*
*  \return: none
*
*****************************************************************************/
void satAddSATAIDDevCBReset(
                   agsaRoot_t        *agRoot,
                   tdsaDeviceData_t  *oneDeviceData,
                   satIOContext_t    *satIOContext,
                   tdIORequestBody_t *tdIORequestBody
                   )
{
  tdsaRootOsData_t   *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t           *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t         *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t      *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  satInternalIo_t    *satIntIo;
  satDeviceData_t    *satDevData;

  TI_DBG2(("satAddSATAIDDevCBReset: start\n"));
  satIntIo           = satIOContext->satIntIoContext;
  satDevData         = satIOContext->pSatDevData;
  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);
  /* clean up TD layer's IORequestBody */
  ostiFreeMemory(
                 tiRoot,
                 tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                 sizeof(tdIORequestBody_t)
                );
  return;
}


/*****************************************************************************
*! \brief  satAddSATAIDDevCBCleanup
*
*   This routine cleans up IOs for failed Identify device data
*
*  \param   agRoot:           Handles for this instance of SAS/SATA hardware
*  \param   oneDeviceData:    Pointer to the device data.
*  \param   ioContext:        Pointer to satIOContext_t.
*  \param   tdIORequestBody:  Pointer to the request body
*
*  \return: none
*
*****************************************************************************/
void satAddSATAIDDevCBCleanup(
                   agsaRoot_t        *agRoot,
                   tdsaDeviceData_t  *oneDeviceData,
                   satIOContext_t    *satIOContext,
                   tdIORequestBody_t *tdIORequestBody
                   )
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  satInternalIo_t         *satIntIo;
  satDeviceData_t         *satDevData;
  bit8                    PhyID;

  TI_DBG2(("satAddSATAIDDevCBCleanup: start\n"));
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  PhyID                  = oneDeviceData->phyID;
  tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
  /* put onedevicedata back to free list */
  osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
  TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
  TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));

  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);


  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

  /* clean up TD layer's IORequestBody */
  ostiFreeMemory(
                 tiRoot,
                 tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                 sizeof(tdIORequestBody_t)
                );

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

  return;
}

/*****************************************************************************/
/*! \brief SAT implementation for tdsaDiscoveryStartIDDev.
 *
 *  This function sends identify device data to SATA device in discovery
 *
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list.
 *  \param   oneDeviceData :   Pointer to the device data.
 *
 *  \return If command is started successfully
 *    - \e tiSuccess:     I/O request successfully initiated.
 *    - \e tiBusy:        No resources available, try again later.
 *    - \e tiIONoDevice:  Invalid device handle.
 *    - \e tiError:       Other errors.
 */
/*****************************************************************************/
GLOBAL bit32
tdsaDiscoveryStartIDDev(tiRoot_t                  *tiRoot,
                        tiIORequest_t             *tiIORequest, /* agNULL */
                        tiDeviceHandle_t          *tiDeviceHandle,
                        tiScsiInitiatorRequest_t *tiScsiRequest, /* agNULL */
                        tdsaDeviceData_t          *oneDeviceData
                        )
{
  void                        *osMemHandle;
  tdIORequestBody_t           *tdIORequestBody;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  agsaIORequest_t             *agIORequest = agNULL; /* identify device data itself */
  satIOContext_t              *satIOContext = agNULL;
  bit32                       status;

  /* allocate tdiorequestbody and call tdsaDiscoveryIntStartIDDev
  tdsaDiscoveryIntStartIDDev(tiRoot, agNULL, tiDeviceHandle, satIOContext);

  */

  TI_DBG3(("tdsaDiscoveryStartIDDev: start\n"));
  TI_DBG3(("tdsaDiscoveryStartIDDev: did %d\n", oneDeviceData->id));

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
    TI_DBG1(("tdsaDiscoveryStartIDDev: ostiAllocMemory failed... loc 1\n"));
    return tiError;
  }
  if (tdIORequestBody == agNULL)
  {
    TI_DBG1(("tdsaDiscoveryStartIDDev: ostiAllocMemory returned NULL tdIORequestBody loc 2\n"));
    return tiError;
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

  /* saving port ID just in case of full discovery to full discovery transition */
  satIOContext->pid = oneDeviceData->tdPortContext->id;
  osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0x0, sizeof(agsaSATAIdentifyData_t));
  status = tdsaDiscoveryIntStartIDDev(tiRoot,
                                      tiIORequest, /* agNULL */
                                      tiDeviceHandle, /* &(oneDeviceData->tiDeviceHandle)*/
                                      agNULL,
                                      satIOContext
                                      );
  if (status != tiSuccess)
  {
    TI_DBG1(("tdsaDiscoveryStartIDDev: failed in sending %d\n", status));
    ostiFreeMemory(tiRoot, osMemHandle, sizeof(tdIORequestBody_t));
  }
  return status;
}

/*****************************************************************************/
/*! \brief SAT implementation for tdsaDiscoveryIntStartIDDev.
 *
 *  This function sends identify device data to SATA device.
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
GLOBAL bit32
tdsaDiscoveryIntStartIDDev(tiRoot_t                  *tiRoot,
                           tiIORequest_t             *tiIORequest, /* agNULL */
                           tiDeviceHandle_t          *tiDeviceHandle,
                           tiScsiInitiatorRequest_t  *tiScsiRequest, /* agNULL */
                           satIOContext_t            *satIOContext
                           )
{
  satInternalIo_t           *satIntIo = agNULL;
  satDeviceData_t           *satDevData = agNULL;
  tdIORequestBody_t         *tdIORequestBody;
  satIOContext_t            *satNewIOContext;
  bit32                     status;

  TI_DBG3(("tdsaDiscoveryIntStartIDDev: start\n"));

  satDevData = satIOContext->pSatDevData;

  /* allocate identify device command */
  satIntIo = satAllocIntIoResource( tiRoot,
                                    tiIORequest,
                                    satDevData,
                                    sizeof(agsaSATAIdentifyData_t), /* 512; size of identify device data */
                                    satIntIo);

  if (satIntIo == agNULL)
  {
    TI_DBG2(("tdsaDiscoveryIntStartIDDev: can't alloacate\n"));

    return tiError;
  }

  /* fill in fields */
  /* real ttttttthe one worked and the same; 5/21/07/ */
  satIntIo->satOrgTiIORequest = tiIORequest; /* changed */
  tdIORequestBody = satIntIo->satIntRequestBody;
  satNewIOContext = &(tdIORequestBody->transport.SATA.satIOContext);

  satNewIOContext->pSatDevData   = satDevData;
  satNewIOContext->pFis          = &(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev);
  satNewIOContext->pScsiCmnd     = &(satIntIo->satIntTiScsiXchg.scsiCmnd);
  satNewIOContext->pSense        = &(tdIORequestBody->transport.SATA.sensePayload);
  satNewIOContext->pTiSenseData  = &(tdIORequestBody->transport.SATA.tiSenseData);
  satNewIOContext->tiRequestBody = satIntIo->satIntRequestBody; /* key fix */
  satNewIOContext->interruptContext = tiInterruptContext;
  satNewIOContext->satIntIoContext  = satIntIo;

  satNewIOContext->ptiDeviceHandle = agNULL;
  satNewIOContext->satOrgIOContext = satIOContext; /* changed */

  /* this is valid only for TD layer generated (not triggered by OS at all) IO */
  satNewIOContext->tiScsiXchg = &(satIntIo->satIntTiScsiXchg);


  TI_DBG6(("tdsaDiscoveryIntStartIDDev: OS satIOContext %p \n", satIOContext));
  TI_DBG6(("tdsaDiscoveryIntStartIDDev: TD satNewIOContext %p \n", satNewIOContext));
  TI_DBG6(("tdsaDiscoveryIntStartIDDev: OS tiScsiXchg %p \n", satIOContext->tiScsiXchg));
  TI_DBG6(("tdsaDiscoveryIntStartIDDev: TD tiScsiXchg %p \n", satNewIOContext->tiScsiXchg));



  TI_DBG3(("tdsaDiscoveryIntStartIDDev: satNewIOContext %p tdIORequestBody %p\n", satNewIOContext, tdIORequestBody));

  status = tdsaDiscoverySendIDDev(tiRoot,
                                  &satIntIo->satIntTiIORequest, /* New tiIORequest */
                                  tiDeviceHandle,
                                  satNewIOContext->tiScsiXchg, /* New tiScsiInitiatorRequest_t *tiScsiRequest, */
                                  satNewIOContext);

  if (status != tiSuccess)
  {
    TI_DBG1(("tdsaDiscoveryIntStartIDDev: failed in sending %d\n", status));

    satFreeIntIoResource( tiRoot,
                          satDevData,
                          satIntIo);

    return tiError;
  }


  TI_DBG6(("tdsaDiscoveryIntStartIDDev: end\n"));

  return status;
}


/*****************************************************************************/
/*! \brief SAT implementation for tdsaDiscoverySendIDDev.
 *
 *  This function prepares identify device data FIS and sends it to SATA device.
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
GLOBAL bit32
tdsaDiscoverySendIDDev(tiRoot_t                  *tiRoot,
                       tiIORequest_t             *tiIORequest,
                       tiDeviceHandle_t          *tiDeviceHandle,
                       tiScsiInitiatorRequest_t  *tiScsiRequest,
                       satIOContext_t            *satIOContext
                       )
{
  bit32                     status;
  bit32                     agRequestType;
  satDeviceData_t           *pSatDevData;
  agsaFisRegHostToDevice_t  *fis;
#ifdef  TD_DEBUG_ENABLE
  tdIORequestBody_t         *tdIORequestBody;
  satInternalIo_t           *satIntIoContext;
#endif

  pSatDevData   = satIOContext->pSatDevData;
  fis           = satIOContext->pFis;
  TI_DBG3(("tdsaDiscoverySendIDDev: start\n"));
#ifdef  TD_DEBUG_ENABLE
  satIntIoContext = satIOContext->satIntIoContext;
  tdIORequestBody = satIntIoContext->satIntRequestBody;
#endif
  TI_DBG5(("tdsaDiscoverySendIDDev: satIOContext %p tdIORequestBody %p\n", satIOContext, tdIORequestBody));

  fis->h.fisType        = 0x27;                   /* Reg host to device */
  fis->h.c_pmPort       = 0x80;                   /* C Bit is set */
  if (pSatDevData->satDeviceType == SATA_ATAPI_DEVICE)
      fis->h.command    = SAT_IDENTIFY_PACKET_DEVICE;  /* 0xA1 */
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
  satIOContext->satCompleteCB = &tdsaDiscoveryStartIDDevCB;

  /*
   * Prepare SGL and send FIS to LL layer.
   */
  satIOContext->reqType = agRequestType;       /* Save it */

#ifdef TD_INTERNAL_DEBUG
  tdhexdump("tdsaDiscoverySendIDDev", (bit8 *)satIOContext->pFis, sizeof(agsaFisRegHostToDevice_t));
#ifdef  TD_DEBUG_ENABLE
  tdhexdump("tdsaDiscoverySendIDDev LL", (bit8 *)&(tdIORequestBody->transport.SATA.agSATARequestBody.fis.fisRegHostToDev), sizeof(agsaFisRegHostToDevice_t));
#endif
#endif
  status = sataLLIOStart( tiRoot,
                          tiIORequest,
                          tiDeviceHandle,
                          tiScsiRequest,
                          satIOContext);
  TI_DBG3(("tdsaDiscoverySendIDDev: end status %d\n", status));
  return status;
}


/*****************************************************************************
*! \brief  tdsaDiscoveryStartIDDevCB
*
*   This routine is a callback function for tdsaDiscoverySendIDDev()
*   Using Identify Device Data, this function finds whether devicedata is
*   new or old. If new, add it to the devicelist. This is done as a part
*   of discovery.
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
void tdsaDiscoveryStartIDDevCB(
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
  tiIORequest_t           *tiOrgIORequest = agNULL;

#ifdef  TD_DEBUG_ENABLE
  bit32                     ataStatus = 0;
  bit32                     ataError;
  agsaFisPioSetupHeader_t   *satPIOSetupHeader = agNULL;
#endif
  agsaSATAIdentifyData_t    *pSATAIdData;
  bit16                     *tmpptr, tmpptr_tmp;
  bit32                     x;
  tdsaDeviceData_t          *oneDeviceData = agNULL;
  void                      *sglVirtualAddr;
  tdsaPortContext_t         *onePortContext = agNULL;
  tiPortalContext_t         *tiPortalContext = agNULL;
  bit32                     retry_status;

  TI_DBG3(("tdsaDiscoveryStartIDDevCB: start\n"));

  tdIORequestBody        = (tdIORequestBody_t *)agIORequest->osData;
  satIOContext           = (satIOContext_t *) ioContext;
  satIntIo               = satIOContext->satIntIoContext;
  satDevData             = satIOContext->pSatDevData;
  oneDeviceData = (tdsaDeviceData_t *)tdIORequestBody->tiDevHandle->tdData;
  TI_DBG3(("tdsaDiscoveryStartIDDevCB: did %d\n", oneDeviceData->id));
  onePortContext = oneDeviceData->tdPortContext;
  if (onePortContext == agNULL)
  {
      TI_DBG1(("tdsaDiscoveryStartIDDevCB: onePortContext is NULL\n"));
      return;
  }
  tiPortalContext= onePortContext->tiPortalContext;

  satDevData->IDDeviceValid = agFALSE;

  if (satIntIo == agNULL)
  {
    TI_DBG1(("tdsaDiscoveryStartIDDevCB: External, OS generated\n"));
    TI_DBG1(("tdsaDiscoveryStartIDDevCB: Not possible case\n"));
    satOrgIOContext      = satIOContext;
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
    return;
  }
  else
  {
    TI_DBG3(("tdsaDiscoveryStartIDDevCB: Internal, TD generated\n"));
    satOrgIOContext        = satIOContext->satOrgIOContext;
    if (satOrgIOContext == agNULL)
    {
      TI_DBG6(("tdsaDiscoveryStartIDDevCB: satOrgIOContext is NULL\n"));
      return;
    }
    else
    {
      TI_DBG6(("tdsaDiscoveryStartIDDevCB: satOrgIOContext is NOT NULL\n"));
      tdOrgIORequestBody     = (tdIORequestBody_t *)satOrgIOContext->tiRequestBody;
      sglVirtualAddr         = satIntIo->satIntTiScsiXchg.sglVirtualAddr;
    }
  }

  tiOrgIORequest           = tdIORequestBody->tiIORequest;
  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  TI_DBG3(("tdsaDiscoveryStartIDDevCB: satOrgIOContext->pid %d\n", satOrgIOContext->pid));

  /* protect against double completion for old port */
  if (satOrgIOContext->pid != oneDeviceData->tdPortContext->id)
  {
    TI_DBG3(("tdsaDiscoveryStartIDDevCB: incorrect pid\n"));
    TI_DBG3(("tdsaDiscoveryStartIDDevCB: satOrgIOContext->pid %d\n", satOrgIOContext->pid));
    TI_DBG3(("tdsaDiscoveryStartIDDevCB: tiPortalContext pid %d\n", oneDeviceData->tdPortContext->id));

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

    return;
  }

  /* completion after portcontext is invalidated */
  if (onePortContext != agNULL)
  {
    if (onePortContext->valid == agFALSE)
    {
      TI_DBG1(("tdsaDiscoveryStartIDDevCB: portcontext is invalid\n"));
      TI_DBG1(("tdsaDiscoveryStartIDDevCB: onePortContext->id pid %d\n", onePortContext->id));
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

      /* no notification to OS layer */
      return;
    }
  }

  if (agFirstDword == agNULL && agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("tdsaDiscoveryStartIDDevCB: agFirstDword is NULL when error, status %d\n", agIOStatus));
    TI_DBG1(("tdsaDiscoveryStartIDDevCB: did %d\n", oneDeviceData->id));

    if (tdsaAllShared->ResetInDiscovery != 0 && satDevData->ID_Retries < SATA_ID_DEVICE_DATA_RETRIES)
    {
      satIOContext->pSatDevData->satPendingNONNCQIO--;
      satIOContext->pSatDevData->satPendingIO--;
      retry_status = sataLLIOStart(tiRoot,
                                   &satIntIo->satIntTiIORequest,
           &(oneDeviceData->tiDeviceHandle),
           satIOContext->tiScsiXchg,
           satIOContext);
      if (retry_status != tiSuccess)
      {
        /* simply give up */
        satDevData->ID_Retries = 0;
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
        return;
      }
      satDevData->ID_Retries++;
      tdIORequestBody->ioCompleted = agFALSE;
      tdIORequestBody->ioStarted = agTRUE;
      return;
    }
    else
    {
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
      if (tdsaAllShared->ResetInDiscovery != 0)
      {
        /* ResetInDiscovery in on */
        if (satDevData->NumOfIDRetries <= 0)
        {
          satDevData->NumOfIDRetries++;
          satDevData->ID_Retries = 0;
          /* send link reset */
          tdsaPhyControlSend(tiRoot,
                             oneDeviceData,
                             SMP_PHY_CONTROL_HARD_RESET,
                             agNULL,
                             tdsaRotateQnumber(tiRoot, oneDeviceData)
                            );
        }
      }
      return;
    }
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
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION ||
      agIOStatus == OSSA_IO_XFER_ERROR_NAK_RECEIVED ||
      agIOStatus == OSSA_IO_XFER_ERROR_DMA ||
      agIOStatus == OSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT ||
      agIOStatus == OSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE ||
      agIOStatus == OSSA_IO_XFER_OPEN_RETRY_TIMEOUT ||
      agIOStatus == OSSA_IO_NO_DEVICE ||
      agIOStatus == OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION ||
      agIOStatus == OSSA_IO_PORT_IN_RESET ||
      agIOStatus == OSSA_IO_DS_NON_OPERATIONAL ||
      agIOStatus == OSSA_IO_DS_IN_RECOVERY ||
      agIOStatus == OSSA_IO_DS_IN_ERROR
      )
  {
    TI_DBG1(("tdsaDiscoveryStartIDDevCB: OSSA_IO_OPEN_CNX_ERROR 0x%x\n", agIOStatus));
    if (tdsaAllShared->ResetInDiscovery != 0 && satDevData->ID_Retries < SATA_ID_DEVICE_DATA_RETRIES)
    {
      satIOContext->pSatDevData->satPendingNONNCQIO--;
      satIOContext->pSatDevData->satPendingIO--;
      retry_status = sataLLIOStart(tiRoot,
                                   &satIntIo->satIntTiIORequest,
           &(oneDeviceData->tiDeviceHandle),
           satIOContext->tiScsiXchg,
           satIOContext);
      if (retry_status != tiSuccess)
      {
        /* simply give up */
        satDevData->ID_Retries = 0;
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
        return;
      }
      satDevData->ID_Retries++;
      tdIORequestBody->ioCompleted = agFALSE;
      tdIORequestBody->ioStarted = agTRUE;
      return;
    }
    else
    {
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
      if (tdsaAllShared->ResetInDiscovery != 0)
      {
        /* ResetInDiscovery in on */
        if (satDevData->NumOfIDRetries <= 0)
        {
          satDevData->NumOfIDRetries++;
          satDevData->ID_Retries = 0;
          /* send link reset */
          tdsaPhyControlSend(tiRoot,
                             oneDeviceData,
                             SMP_PHY_CONTROL_HARD_RESET,
                             agNULL,
                             tdsaRotateQnumber(tiRoot, oneDeviceData)
                            );
        }
      }
      return;
    }
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
    TI_DBG1(("tdsaDiscoveryStartIDDevCB: ataStatus 0x%x ataError 0x%x\n", ataStatus, ataError));

    if (tdsaAllShared->ResetInDiscovery != 0 && satDevData->ID_Retries < SATA_ID_DEVICE_DATA_RETRIES)
    {
      satIOContext->pSatDevData->satPendingNONNCQIO--;
      satIOContext->pSatDevData->satPendingIO--;
      retry_status = sataLLIOStart(tiRoot,
                                   &satIntIo->satIntTiIORequest,
           &(oneDeviceData->tiDeviceHandle),
           satIOContext->tiScsiXchg,
           satIOContext);
      if (retry_status != tiSuccess)
      {
        /* simply give up */
        satDevData->ID_Retries = 0;
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
        return;
      }
      satDevData->ID_Retries++;
      tdIORequestBody->ioCompleted = agFALSE;
      tdIORequestBody->ioStarted = agTRUE;
      return;
    }
    else
    {
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
      if (tdsaAllShared->ResetInDiscovery != 0)
      {
        /* ResetInDiscovery in on */
        if (satDevData->NumOfIDRetries <= 0)
        {
          satDevData->NumOfIDRetries++;
          satDevData->ID_Retries = 0;
          /* send link reset */
          tdsaPhyControlSend(tiRoot,
                             oneDeviceData,
                             SMP_PHY_CONTROL_HARD_RESET,
                             agNULL,
                             tdsaRotateQnumber(tiRoot, oneDeviceData)
                            );
        }
      }
      return;
    }
  }


  /* success */
  TI_DBG3(("tdsaDiscoveryStartIDDevCB: Success\n"));
  TI_DBG3(("tdsaDiscoveryStartIDDevCB: Success did %d\n", oneDeviceData->id));

  /* Convert to host endian */
  tmpptr = (bit16*)sglVirtualAddr;
  for (x=0; x < sizeof(agsaSATAIdentifyData_t)/sizeof(bit16); x++)
  {
    OSSA_READ_LE_16(AGROOT, &tmpptr_tmp, tmpptr, 0);
    *tmpptr = tmpptr_tmp;
    tmpptr++;
  }

  pSATAIdData = (agsaSATAIdentifyData_t *)sglVirtualAddr;
  //tdhexdump("satAddSATAIDDevCB before", (bit8 *)pSATAIdData, sizeof(agsaSATAIdentifyData_t));

  TI_DBG5(("tdsaDiscoveryStartIDDevCB: OS satOrgIOContext %p \n", satOrgIOContext));
  TI_DBG5(("tdsaDiscoveryStartIDDevCB: TD satIOContext %p \n", satIOContext));
  TI_DBG5(("tdsaDiscoveryStartIDDevCB: OS tiScsiXchg %p \n", satOrgIOContext->tiScsiXchg));
  TI_DBG5(("tdsaDiscoveryStartIDDevCB: TD tiScsiXchg %p \n", satIOContext->tiScsiXchg));


   /* copy ID Dev data to satDevData */
  satDevData->satIdentifyData = *pSATAIdData;
  satDevData->IDDeviceValid = agTRUE;

#ifdef TD_INTERNAL_DEBUG
  tdhexdump("tdsaDiscoveryStartIDDevCB ID Dev data",(bit8 *)pSATAIdData, sizeof(agsaSATAIdentifyData_t));
  tdhexdump("tdsaDiscoveryStartIDDevCB Device ID Dev data",(bit8 *)&satDevData->satIdentifyData, sizeof(agsaSATAIdentifyData_t));
#endif

  /* set satDevData fields from IndentifyData */
  satSetDevInfo(satDevData,pSATAIdData);
  satDecrementPendingIO(tiRoot, tdsaAllShared, satIOContext);

  satFreeIntIoResource( tiRoot,
                        satDevData,
                        satIntIo);

  if (satDevData->satDeviceType == SATA_ATAPI_DEVICE)
  {
      /* send the Set Feature ATA command to ATAPI device for enbling PIO and DMA transfer mode*/
      satNewIntIo = satAllocIntIoResource( tiRoot,
                                       tiOrgIORequest,
                                       satDevData,
                                       0,
                                       satNewIntIo);

      if (satNewIntIo == agNULL)
      {
        TI_DBG1(("tdsaDiscoveryStartIDDevCB: momory allocation fails\n"));
          /* clean up TD layer's IORequestBody */
        ostiFreeMemory(
                     tiRoot,
                     tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
        return;
      } /* end memory allocation */

      satNewIOContext = satPrepareNewIO(satNewIntIo,
                                        tiOrgIORequest,
                                        satDevData,
                                        agNULL,
                                        satOrgIOContext
                                        );
      /* enable PIO mode, then enable Ultra DMA mode in the satSetFeaturesCB callback function*/
      retry_status = satSetFeatures(tiRoot,
                                 &satNewIntIo->satIntTiIORequest,
                                 satNewIOContext->ptiDeviceHandle,
                                 &satNewIntIo->satIntTiScsiXchg, /* orginal from OS layer */
                                 satNewIOContext,
                                 agFALSE);
      if (retry_status != tiSuccess)
      {
          satFreeIntIoResource(tiRoot, satDevData, satIntIo);
          /* clean up TD layer's IORequestBody */
          ostiFreeMemory(
                 tiRoot,
                 tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                 sizeof(tdIORequestBody_t)
                 );
      }
  }
  else
  {
      /* clean up TD layer's IORequestBody */
      ostiFreeMemory(
                     tiRoot,
                     tdOrgIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
      if (onePortContext != agNULL)
      {
        if (onePortContext->DiscoveryState == ITD_DSTATE_COMPLETED)
        {
          TI_DBG1(("tdsaDiscoveryStartIDDevCB: ID completed after discovery is done; tiDeviceArrival\n"));
          /* in case registration is finished after discovery is finished */
          ostiInitiatorEvent(
                             tiRoot,
                             tiPortalContext,
                             agNULL,
                             tiIntrEventTypeDeviceChange,
                             tiDeviceArrival,
                             agNULL
                             );
        }
      }
      else
      {
        TI_DBG1(("tdsaDiscoveryStartIDDevCB: onePortContext is NULL, wrong\n"));
      }
  }
  TI_DBG3(("tdsaDiscoveryStartIDDevCB: end\n"));
  return;
}
/*****************************************************************************
*! \brief  satAbort
*
*   This routine does local abort for outstanding FIS.
*
*  \param   agRoot:         Handles for this instance of SAS/SATA hardware
*  \param   satIOContext:   Pointer to satIOContext_t.
*
*  \return: none
*
*****************************************************************************/
GLOBAL void satAbort(agsaRoot_t        *agRoot,
                     satIOContext_t    *satIOContext)
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdIORequestBody_t       *tdIORequestBody; /* io to be aborted */
  tdIORequestBody_t       *tdAbortIORequestBody; /* abort io itself */
  agsaIORequest_t         *agToBeAbortedIORequest; /* io to be aborted */
  agsaIORequest_t         *agAbortIORequest;  /* abort io itself */
  bit32                   PhysUpper32;
  bit32                   PhysLower32;
  bit32                   memAllocStatus;
  void                    *osMemHandle;

  TI_DBG1(("satAbort: start\n"));

  if (satIOContext == agNULL)
  {
    TI_DBG1(("satAbort: satIOContext is NULL, wrong\n"));
    return;
  }
  tdIORequestBody = (tdIORequestBody_t *)satIOContext->tiRequestBody;
  agToBeAbortedIORequest = (agsaIORequest_t *)&(tdIORequestBody->agIORequest);
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
    TI_DBG1(("satAbort: ostiAllocMemory failed...\n"));
    return;
  }

  if (tdAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    TI_DBG1(("satAbort: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
    return;
  }
  /* setup task management structure */
  tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  tdAbortIORequestBody->tiDevHandle = tdIORequestBody->tiDevHandle;

  /* initialize agIORequest */
  agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) tdAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */


  /*
   * Issue abort
   */
  saSATAAbort( agRoot, agAbortIORequest, 0, agNULL, 0, agToBeAbortedIORequest, agNULL );


  TI_DBG1(("satAbort: end\n"));
  return;
}

/*****************************************************************************
 *! \brief  satSATADeviceReset
 *
 *   This routine is called to reset all phys of port which a device belongs to
 *
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   oneDeviceData:    Pointer to the device data.
 *  \param   flag:             reset flag
 *
 *  \return:
 *
 *  none
 *
 *****************************************************************************/
osGLOBAL void
satSATADeviceReset(                                                                                                  tiRoot_t            *tiRoot,
                tdsaDeviceData_t    *oneDeviceData,
                bit32               flag)
{
  agsaRoot_t              *agRoot;
  tdsaPortContext_t       *onePortContext;
  bit32                   i;

  TI_DBG1(("satSATADeviceReset: start\n"));
  agRoot         = oneDeviceData->agRoot;
  onePortContext = oneDeviceData->tdPortContext;

  if (agRoot == agNULL)
  {
    TI_DBG1(("satSATADeviceReset: Error!!! agRoot is NULL\n"));
    return;
  }
  if (onePortContext == agNULL)
  {
    TI_DBG1(("satSATADeviceReset: Error!!! onePortContext is NULL\n"));
    return;
  }

   for(i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    if (onePortContext->PhyIDList[i] == agTRUE)
    {
      saLocalPhyControl(agRoot, agNULL, tdsaRotateQnumber(tiRoot, agNULL), i, flag, agNULL);
    }
  }

  return;
}

#endif  /* #ifdef SATA_ENABLE */
