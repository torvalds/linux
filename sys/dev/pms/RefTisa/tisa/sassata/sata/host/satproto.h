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
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/** \file
 *
 *
 * The file contaning function protoptype used by SAT layer.
 *
 */

#ifndef  __SATPROTO_H__
#define __SATPROTO_H__


/*****************************************************************************
*! \brief  itdsatProcessAbnormalCompletion
*
*   This routine is called to complete error case for SATA request previously
*   issued to the LL Layer in saSATAStart()
*
*  \param  agRoot:       Handles for this instance of SAS/SATA hardware
*  \param  agIORequest:  Pointer to the LL I/O request context for this I/O.
*  \param  agIOStatus:   Status of completed I/O.
*  \param  agSATAParm1:  Additional info based on status.
*  \param  agIOInfoLen:  Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param  satIOContext: Pointer to satIOContext_t.
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
                           agsaFrameHandle_t agFrameHandle,
                           satIOContext_t    *satIOContext
                           );
void  itdsatDelayedProcessAbnormalCompletion(
                                             agsaRoot_t        *agRoot,
                                             agsaIORequest_t   *agIORequest,
                                             bit32             agIOStatus,
                                             agsaFisHeader_t   *agFirstDword,
                                             bit32             agIOInfoLen,
                                             agsaFrameHandle_t agFrameHandle,
                                             satIOContext_t    *satIOContext
                                             );

void  itdsatErrorSATAEventHandle(
                                             agsaRoot_t        *agRoot,
                                             agsaIORequest_t   *agIORequest,
                                             agsaPortContext_t *agPortContext,
                                             agsaDevHandle_t   *agDevHandle,
                                             bit32             event,
                                             satIOContext_t    *ioContext
                                             );

void itdsatEncryptionHandler (
                       agsaRoot_t              *agRoot,
                       agsaIORequest_t         *agIORequest,
                       bit32                   agIOStatus,
                       bit32                   agIOInfoLen,
                       void                    *agParam,
                       bit32                   agOtherInfo
                       );

osGLOBAL void
itdsatDifHandler(
                 agsaRoot_t              *agRoot,
                 agsaIORequest_t         *agIORequest,
                 bit32                   agIOStatus,
                 bit32                   agIOInfoLen,
                 void                    *agParam,
                 bit32                   agOtherInfo
                );

void  satProcessAbort(
                      tiRoot_t          *tiRoot,
                      tiIORequest_t     *tiIORequest,
                      satIOContext_t    *satIOContext
                      );
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
 *  \return None
 */
/*****************************************************************************/

void satSetSensePayload( scsiRspSense_t   *pSense,
                         bit8             SnsKey,
                         bit32            SnsInfo,
                         bit16            SnsCode,
                         satIOContext_t   *satIOContext);


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
 *  \return None
 */
/*****************************************************************************/

void satSetDeferredSensePayload( scsiRspSense_t   *pSense,
                                 bit8             SnsKey,
                                 bit32            SnsInfo,
                                 bit16            SnsCode,
                                 satIOContext_t   *satIOContext
                                 );

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
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

void satPacketCB(
                 agsaRoot_t        *agRoot,
                 agsaIORequest_t   *agIORequest,
                 bit32             agIOStatus,
                 agsaFisHeader_t   *agFirstDword,
                 bit32             agIOInfoLen,
                 void              *agParam,
                 void              *ioContext
                 );
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
GLOBAL bit32  satDeviceReset(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t  *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            );

void satDeviceResetCB(
                 agsaRoot_t        *agRoot,
                 agsaIORequest_t   *agIORequest,
                 bit32             agIOStatus,
                 agsaFisHeader_t   *agFirstDword,
                 bit32             agIOInfoLen,
                 void              *agParam,
                 void              *ioContext
                 );

/*****************************************************************************/
/*! \brief SAT implementation for satExecuteDeviceDiagnostic.
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
                            );

void satExecuteDeviceDiagnosticCB(
                 agsaRoot_t        *agRoot,
                 agsaIORequest_t   *agIORequest,
                 bit32             agIOStatus,
                 agsaFisHeader_t   *agFirstDword,
                 bit32             agIOInfoLen,
                 void              *agParam,
                 void              *ioContext
                 );

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
                            bit8                      bTransferMode
                            );
 void satSetFeaturesPIOCB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam,
                  void              *ioContext
                  );

 void satSetFeaturesCB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam,
                  void              *ioContext
                  );
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
                   satIOContext_t            *satIOContext);

 void satRequestSenseForATAPICB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam,
                  void              *ioContext
                  );

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);


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
                   satIOContext_t            *satIOContext);
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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);
GLOBAL bit32  satWrite_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);


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
                   satIOContext_t            *satIOContext);


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
                   satIOContext_t            *satIOContext);


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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);


GLOBAL bit32  satInquiry(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);
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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);
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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);
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
                   satIOContext_t            *satIOContext);

/*****************************************************************************/
/*! \brief SAT implementation for SCSI satRead10_1.
 *
 *  SAT implementation for SCSI satRead10_1
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
GLOBAL bit32  satRead_1(
                         tiRoot_t                  *tiRoot,
                         tiIORequest_t             *tiIORequest,
                         tiDeviceHandle_t          *tiDeviceHandle,
                         tiScsiInitiatorRequest_t *tiScsiRequest,
                         satIOContext_t            *satIOContext);
GLOBAL bit32  satRead10_2(
                         tiRoot_t                  *tiRoot,
                         tiIORequest_t             *tiIORequest,
                         tiDeviceHandle_t          *tiDeviceHandle,
                         tiScsiInitiatorRequest_t *tiScsiRequest,
                         satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

GLOBAL bit32  satWriteSame10_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
                   bit32                     lba
                   );
GLOBAL bit32  satWriteSame10_2(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
                   bit32                     lba
                   );
GLOBAL bit32  satWriteSame10_3(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
                   bit32                     lba
                   );
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
                   satIOContext_t            *satIOContext);

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
                   );

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);


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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);
GLOBAL bit32  satModeSelect6n10_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);
#endif

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
                   satIOContext_t            *satIOContext);

GLOBAL bit32  satNonChainedWriteNVerify_Verify(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

GLOBAL bit32  satChainedWriteNVerify_Write(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

GLOBAL bit32  satChainedWriteNVerify_Verify(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

GLOBAL bit32  satChainedWriteNVerify_Start_Verify(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);


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
                   satIOContext_t            *satIOContext);

GLOBAL bit32  satChainedVerify16(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);


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
                   satIOContext_t            *satIOContext);


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
                   satIOContext_t            *satIOContext);

GLOBAL bit32  satChainedVerify(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

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
                   satIOContext_t            *satIOContext);
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
                   satIOContext_t            *satIOContext);
/*****************************************************************************/
/*! \brief SAT implementation for SCSI satTestUnitReady_1.
 *
 *  SAT implementation for SCSI satTestUnitReady_1
 *  Sub function of satTestUnitReady
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
                         satIOContext_t            *satIOContext);

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
                    tiIniScsiCmnd_t         *scsiCmnd
                    );


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
                    agsaSATAIdentifyData_t  *pSATAIdData);


/*****************************************************************************/
/*! \brief SAT implementation for SCSI INQUIRY page 83.
 *
 *  SAT implementation for SCSI INQUIRY page 83.
 *
 *  \param   pInquiry:         Pointer to Inquiry Data buffer.
 *  \param   pSATAIdData:      Pointer to ATA IDENTIFY DEVICE data.
 *  \param   pSatDevData       Pointer to internal device data structure
 *
 *  \return None.
 */
/*****************************************************************************/
GLOBAL void  satInquiryPage83(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData,
                    satDeviceData_t         *pSatDevData);

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
                    satDeviceData_t         *pSatDevData);

/*****************************************************************************/
/*! \brief SAT implementation for SCSI INQUIRY page 80.
 *
 *  SAT implementation for SCSI INQUIRY page 80.
 *
 *  \param   pInquiry:         Pointer to Inquiry Data buffer.
 *  \param   pSATAIdData:      Pointer to ATA IDENTIFY DEVICE data.
 *
 *  \return None.
 */
/*****************************************************************************/
GLOBAL void  satInquiryPage80(
                    bit8                    *pInquiry,
                    agsaSATAIdentifyData_t  *pSATAIdData);


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
 *  tiSuccess:     I/O request successfully initiated.
 *  tiBusy:        No resources available, try again later.
 *  tiIONoDevice:  Invalid device handle.
 *  tiError:       Other errors that prevent the I/O request to be started.
 *
 *
 *****************************************************************************/

GLOBAL bit32  sataLLIOStart (
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext
                  );

/*****************************************************************************
*! \brief itdsataIOPrepareSGL
*
*  This function is called to prepare and translate the TISA SGL information
*  to the SAS/SATA LL layer specific SGL.
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
osGLOBAL bit32 itdsataIOPrepareSGL(
                  tiRoot_t                 *tiRoot,
                  tdIORequestBody_t        *tdIORequestBody,
                  tiSgl_t                  *tiSgl1,
                  void                     *sglVirtualAddr
                  );

/*****************************************************************************
*! \brief  satNonChainedDataIOCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with normal non-chained data I/O SATA request.
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

void satNonChainedDataIOCB(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           agsaFrameHandle_t agFrameHandle,
                           void              *ioContext
                           );
void satChainedDataIOCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        );

void satNonChainedWriteNVerifyCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        );

void satChainedWriteNVerifyCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        );

/*****************************************************************************
*! \brief  satNonDataIOCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with non-data I/O SATA request.
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
void satNonDataIOCB(
                    agsaRoot_t        *agRoot,
                    agsaIORequest_t   *agIORequest,
                    bit32             agIOStatus,
                    agsaFisHeader_t   *agFirstDword,
                    bit32             agIOInfoLen,
                    agsaFrameHandle_t agFrameHandle,
                    void              *ioContext
                    );

/*****************************************************************************
*! \brief  satSMARTEnableCB
*
*   This routine is a callback function for satSMARTEnable()
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
void satSMARTEnableCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                      ) ;

/*****************************************************************************
*! \brief  satLogSenseCB
*
*   This routine is a callback function for satLogSense()
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
void satLogSenseCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioCotext
                   );
void satModeSelect6n10CB(
                         agsaRoot_t        *agRoot,
                         agsaIORequest_t   *agIORequest,
                         bit32             agIOStatus,
                         agsaFisHeader_t   *agFirstDword,
                         bit32             agIOInfoLen,
                         agsaFrameHandle_t agFrameHandle,
                         void              *ioContext
                         );
void satSynchronizeCache10n16CB(
                                agsaRoot_t        *agRoot,
                                agsaIORequest_t   *agIORequest,
                                bit32             agIOStatus,
                                agsaFisHeader_t   *agFirstDword,
                                bit32             agIOInfoLen,
                                agsaFrameHandle_t agFrameHandle,
                                void              *ioContext
                                );
#ifdef REMOVED
void satWriteAndVerify10CB(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           agsaFrameHandle_t agFrameHandle,
                           void              *ioContext
                           );
#endif

/*****************************************************************************
*! \brief  satReadLogExtCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals READ LOG EXT completion.
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
void satReadLogExtCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     agsaFrameHandle_t agFrameHandle,
                     void              *ioContext
                     );
void satTestUnitReadyCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        );
void satWriteSame10CB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                      );
/*****************************************************************************
*! \brief  satSendDiagnosticCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Send Diagnostic completion.
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
void satSendDiagnosticCB(
                         agsaRoot_t        *agRoot,
                         agsaIORequest_t   *agIORequest,
                         bit32             agIOStatus,
                         agsaFisHeader_t   *agFirstDword,
                         bit32             agIOInfoLen,
                         agsaFrameHandle_t agFrameHandle,
                         void              *ioContext
                         );
/*****************************************************************************
*! \brief  satRequestSenseCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Request Sense completion.
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
void satRequestSenseCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        );
/*****************************************************************************
*! \brief  satStartStopUnitCB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Send Diagnostic completion.
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
void satStartStopUnitCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext
                        );
/*****************************************************************************
*! \brief  satVerify10CB
*
*   This routine is a callback function called from ossaSATACompleted().
*   This CB routine deals with Verify(10) completion.
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
void satVerify10CB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContex
                   );

void satNonChainedVerifyCB(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           agsaFrameHandle_t agFrameHandle,
                           void              *ioContext
                           );

void satChainedVerifyCB(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           agsaFrameHandle_t agFrameHandle,
                           void              *ioContext
                           );

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
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext,
                            tiLUN_t                   *lun);

osGLOBAL bit32 satTmWarmReset(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext);

osGLOBAL bit32 satTDInternalTmReset(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext);

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
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext,
                            tiIORequest_t             *taskTag);

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
                void              *respFis);

osGLOBAL void
ossaSATADeviceResetCB(
                      agsaRoot_t        *agRoot,
                      agsaDevHandle_t   *agDevHandle,
                      bit32             resetStatus,
                      void              *resetparm);

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
                          agsaFrameHandle_t agFrameHandle,
                          satIOContext_t     *satIOContext,
                          bit32              interruptContext);


/*****************************************************************************
*! \brief tdssAddSataToSharedcontext
*
*  Purpose:  This function adds a discovered SATA device to a device list of
*            a port context
*
*  \param   tdsaPortContext          Pointer to a port context
*  \param   tdsaDeviceData           Pointer to a device data
*  \param   tsddPortContext_Instance Pointer to the target port context
*  \param   agRoot                   Pointer to the root data structure of
*                                    TD and Lower layer
*  \param   agDevHandle              Pointer to a device handle
*  \param   agSATADeviceInfo         Pointer to SATA device info structure
*
*  \Return: none
*
*****************************************************************************/
osGLOBAL void
tdssRemoveSATAFromSharedcontext(
                               tdsaPortContext_t *tdsaPortContext_Ins,
                               tdsaDeviceData_t  *tdsaDeviceData_ins,
                               agsaRoot_t        *agRoot
                               );

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
                   bit32                interruptContext );

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
GLOBAL satInternalIo_t *  satAllocIntIoResource(
                    tiRoot_t              *tiRoot,
                    tiIORequest_t         *tiIORequest,
                    satDeviceData_t       *satDevData,
                    bit32                 dmaAllocLength,
                    satInternalIo_t       *satIntIo);

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
                   satIOContext_t            *satIOContext);


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
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

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
                        );

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
GLOBAL bit32  satReadBuffer(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

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
                        );

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
GLOBAL bit32  satWriteBuffer(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext);

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
                        );

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
                   satIOContext_t            *satIOContext);

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
                        );

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
GLOBAL bit32  satReassignBlocks_1(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
                   satIOContext_t            *satOrgIOContext);

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
GLOBAL bit32  satReassignBlocks_2(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext,
                   bit8                      *LBA
                   );

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
                            );

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

GLOBAL bit32 sataLLIOAbort (
                tiRoot_t        *tiRoot,
                tiIORequest_t   *taskTag );



void satInquiryCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                   );

void satInquiryIntCB(
                   tiRoot_t                  *tiRoot,
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext
                   );

GLOBAL bit32  satSendIDDev(
                           tiRoot_t                  *tiRoot,
                           tiIORequest_t             *tiIORequest,
                           tiDeviceHandle_t          *tiDeviceHandle,
                           tiScsiInitiatorRequest_t *tiScsiRequest,
                           satIOContext_t            *satIOContext);


GLOBAL bit32  satStartIDDev(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            );

void satSetDevInfo(
                   satDeviceData_t           *satDevData,
                   agsaSATAIdentifyData_t    *SATAIdData
                   );

GLOBAL bit32  satAddSATAStartIDDev(
                                   tiRoot_t                  *tiRoot,
                                   tiIORequest_t             *tiIORequest,
                                   tiDeviceHandle_t          *tiDeviceHandle,
                                   tiScsiInitiatorRequest_t *tiScsiRequest,
                                   satIOContext_t            *satIOContext
                                  );

GLOBAL bit32  satAddSATASendIDDev(
                                  tiRoot_t                  *tiRoot,
                                  tiIORequest_t             *tiIORequest,
                                  tiDeviceHandle_t          *tiDeviceHandle,
                                  tiScsiInitiatorRequest_t *tiScsiRequest,
                                  satIOContext_t            *satIOContext
                                 );

void satAddSATAIDDevCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                   );

void satAddSATAIDDevCBReset(
                   agsaRoot_t        *agRoot,
                   tdsaDeviceData_t  *oneDeviceData,
                   satIOContext_t    *satIOContext,
                   tdIORequestBody_t *tdIORequestBody
                   );

void satAddSATAIDDevCBCleanup(
                   agsaRoot_t        *agRoot,
                   tdsaDeviceData_t  *oneDeviceData,
                   satIOContext_t    *satIOContext,
                   tdIORequestBody_t *tdIORequestBody
                   );

GLOBAL bit32 tdsaDiscoveryIntStartIDDev(
                                   tiRoot_t                  *tiRoot,
                                   tiIORequest_t             *tiIORequest,
                                   tiDeviceHandle_t          *tiDeviceHandle,
                                   tiScsiInitiatorRequest_t *tiScsiRequest,
                                   satIOContext_t            *satIOContext
                                  );

GLOBAL bit32 tdsaDiscoverySendIDDev(
                                   tiRoot_t                  *tiRoot,
                                   tiIORequest_t             *tiIORequest,
                                   tiDeviceHandle_t          *tiDeviceHandle,
                                   tiScsiInitiatorRequest_t *tiScsiRequest,
                                   satIOContext_t            *satIOContext
                                  );

void tdsaDiscoveryStartIDDevCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                   );


/*
  utility functions
 */

bit32 satComputeCDB10LBA(satIOContext_t            *satIOContext);
bit32 satComputeCDB10TL(satIOContext_t            *satIOContext);
bit32 satComputeCDB12LBA(satIOContext_t            *satIOContext);
bit32 satComputeCDB12TL(satIOContext_t            *satIOContext);
bit32 satComputeCDB16LBA(satIOContext_t            *satIOContext);
bit32 satComputeCDB16TL(satIOContext_t            *satIOContext);
bit32 satComputeLoopNum(bit32 a,
                        bit32 b);
bit32 satAddNComparebit64(bit8 *a, bit8 *b);
bit32 satAddNComparebit32(bit8 *a, bit8 *b);
bit32 satCompareLBALimitbit(bit8 *lba);

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
satBitSet(bit8 *data, bit32 index);

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
satBitClear(bit8 *data, bit32 index);

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
satBitTest(bit8 *data, bit32 index);

/******************************************************************************/
/*! \brief allocate an available SATA tag
 *
 *  allocate an available SATA tag
 *
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
                           );

/******************************************************************************/
/*! \brief release an SATA tag
 *
 *  release an available SATA tag
 *
 *  \param pSatDevData
 *
 *  \return -the tag-
 */
/*******************************************************************************/
GLOBAL bit32 satTagRelease(
                              tiRoot_t          *tiRoot,
                              satDeviceData_t   *pSatDevData,
                              bit8              tag
                              );

GLOBAL void
satDecrementPendingIO(
                      tiRoot_t                *tiRoot,
                      tdsaContext_t           *tdsaAllShared,
                      satIOContext_t          *satIOContext
                      );

GLOBAL bit32  satStartResetDevice(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            );

GLOBAL bit32  satResetDevice(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            );

GLOBAL void satResetDeviceCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                   );

osGLOBAL bit32 satSubTM(
                        tiRoot_t          *tiRoot,
                        tiDeviceHandle_t  *tiDeviceHandle,
                        bit32             task,
                        tiLUN_t           *lun,
                        tiIORequest_t     *taskTag,
                        tiIORequest_t     *currentTaskTag,
                        bit32              NotifyOS
                        );

GLOBAL bit32  satStartDeResetDevice(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            );

GLOBAL bit32  satDeResetDevice(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            );


GLOBAL void satDeResetDeviceCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                   );


GLOBAL bit32  satStartCheckPowerMode(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            );

GLOBAL bit32  satCheckPowerMode(
                            tiRoot_t                  *tiRoot,
                            tiIORequest_t             *tiIORequest,
                            tiDeviceHandle_t          *tiDeviceHandle,
                            tiScsiInitiatorRequest_t *tiScsiRequest,
                            satIOContext_t            *satIOContext
                            );

GLOBAL void satCheckPowerModeCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                   );

GLOBAL void satAbort(agsaRoot_t        *agRoot,
                     satIOContext_t    *satIOContext
                     );

GLOBAL void satTranslateATAPIErrorsToSCSIErrors(
                   bit8   bCommand,
                   bit8   bATAStatus,
                   bit8   bATAError,
                   bit8   *pSenseKey,
                   bit16  *pSenseCodeInfo
                   );

osGLOBAL void
satSATADeviceReset(tiRoot_t            *tiRoot,
                  tdsaDeviceData_t    *oneDeviceData,
                  bit32               flag);

#ifdef REMOVED
osGLOBAL void
satSATADeviceReset(                                                                                                     tiRoot_t            *tiRoot,
                   tdsaDeviceData_t    *oneDeviceData,
                   bit32               flag
                   );
#endif
#endif  /*__SATPROTO_H__ */
