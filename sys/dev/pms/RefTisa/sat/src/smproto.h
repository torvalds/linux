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
#ifndef __SMPROTO_H__
#define __SMPROTO_H__

#include <dev/pms/RefTisa/sat/src/smtypes.h>

/***************** start of util ****************************************/
osGLOBAL FORCEINLINE void*
sm_memset(void *s, int c, bit32 n);

osGLOBAL FORCEINLINE void *
sm_memcpy(void *dst, const void *src, bit32 count);

osGLOBAL char 
*sm_strncpy(char *dst, const char *src, bit32 len);


osGLOBAL void 
smhexdump(const char *ptitle, bit8 *pbuf, size_t len);
/***************** end of util ****************************************/

/***************** start of timer fns ****************************************/
osGLOBAL void   
smTimerTick(smRoot_t 		*smRoot );

osGLOBAL void
smInitTimerRequest(
                   smRoot_t                *smRoot, 
                   smTimerRequest_t        *timerRequest
                  );
osGLOBAL void
smSetTimerRequest(
                  smRoot_t            *smRoot,
                  smTimerRequest_t    *timerRequest,
                  bit32               timeout,
                  smTimerCBFunc_t     CBFunc,
                  void                *timerData1,
                  void                *timerData2,
                  void                *timerData3
                  );

osGLOBAL void
smAddTimer(
           smRoot_t            *smRoot,
           smList_t            *timerListHdr, 
           smTimerRequest_t    *timerRequest
          );

osGLOBAL void
smKillTimer(
            smRoot_t            *smRoot,
            smTimerRequest_t    *timerRequest
           );

osGLOBAL void 
smProcessTimers(
                smRoot_t *smRoot
               );
								  				  		  		  

/***************** end of timer fns ****************************************/

osGLOBAL void
smInitTimers(
             smRoot_t *smRoot 
            );

osGLOBAL void
smDeviceDataInit(
                 smRoot_t *smRoot,
                 bit32    max_dev		  
                );

osGLOBAL void
smIOInit(
         smRoot_t *smRoot 
        );

osGLOBAL FORCEINLINE void
smIOReInit(
          smRoot_t          *smRoot,
          smIORequestBody_t *smIORequestBody
          );

osGLOBAL void
smDeviceDataReInit(
                   smRoot_t        *smRoot,
                   smDeviceData_t  *oneDeviceData       
                  );

osGLOBAL void  
smEnqueueIO(
             smRoot_t           *smRoot,
             smSatIOContext_t   *satIOContext
             );

osGLOBAL FORCEINLINE void  
smsatFreeIntIoResource(
             smRoot_t           *smRoot,
             smDeviceData_t     *satDevData,
             smSatInternalIo_t  *satIntIo
             );

osGLOBAL smSatInternalIo_t * 
smsatAllocIntIoResource(
                        smRoot_t              *smRoot,
                        smIORequest_t         *smIORequest,
                        smDeviceData_t        *satDevData,
                        bit32                 dmaAllocLength,
                        smSatInternalIo_t     *satIntIo);



osGLOBAL smDeviceData_t *
smAddToSharedcontext(
                     smRoot_t                   *smRoot,
                     agsaDevHandle_t            *agDevHandle,
                     smDeviceHandle_t           *smDeviceHandle,
                     agsaDevHandle_t            *agExpDevHandle,
                     bit32                      phyID
                    );

osGLOBAL bit32
smRemoveFromSharedcontext(
                          smRoot_t                      *smRoot,
                          agsaDevHandle_t               *agDevHandle,
                          smDeviceHandle_t              *smDeviceHandle
                         );

osGLOBAL smDeviceData_t *
smFindInSharedcontext(
                      smRoot_t                  *smRoot,
                      agsaDevHandle_t           *agDevHandle
                      );

osGLOBAL bit32  
smsatLogSenseAllocate(
                      smRoot_t                  *smRoot,
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smSCSIRequest,
                      smSatIOContext_t            *satIOContext,
                      bit32                     payloadSize,
                      bit32                     flag
                     );

osGLOBAL bit32  
smsatIDSubStart(
                 smRoot_t                 *smRoot,
                 smIORequest_t            *smIORequest,
                 smDeviceHandle_t         *smDeviceHandle,
                 smScsiInitiatorRequest_t *smSCSIRequest,
                 smSatIOContext_t           *satIOContext
               );


osGLOBAL bit32  
smsatIDStart(
              smRoot_t                  *smRoot,
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smSCSIRequest,
              smSatIOContext_t            *satIOContext
             );


osGLOBAL FORCEINLINE bit32  
smsatIOStart(
              smRoot_t                  *smRoot,
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smSCSIRequest,
              smSatIOContext_t            *satIOContext
             );

osGLOBAL void 
smsatSetSensePayload( 
                     smScsiRspSense_t   *pSense,
                     bit8               SnsKey,
                     bit32              SnsInfo,
                     bit16              SnsCode,
                     smSatIOContext_t     *satIOContext
		    );

osGLOBAL void 
smsatSetDeferredSensePayload( 
                             smScsiRspSense_t *pSense,
                             bit8             SnsKey,
                             bit32            SnsInfo,
                             bit16            SnsCode,
                             smSatIOContext_t   *satIOContext                         
                            );

osGLOBAL FORCEINLINE bit32 
smsatIOPrepareSGL(
                  smRoot_t                 *smRoot,
                  smIORequestBody_t        *smIORequestBody,
                  smSgl_t                  *smSgl1,
                  void                     *sglVirtualAddr
                  );
osGLOBAL FORCEINLINE void 
smsatBitSet(smRoot_t *smRoot,bit8 *data, bit32 index);

osGLOBAL FORCEINLINE void 
smsatBitClear(smRoot_t *smRoot,bit8 *data, bit32 index);

osGLOBAL FORCEINLINE BOOLEAN
smsatBitTest(smRoot_t *smRoot,bit8 *data, bit32 index);

osGLOBAL FORCEINLINE bit32 
smsatTagAlloc(
               smRoot_t         *smRoot,
               smDeviceData_t   *pSatDevData,
               bit8             *pTag
             );

osGLOBAL FORCEINLINE bit32 
smsatTagRelease(
                smRoot_t         *smRoot,
                smDeviceData_t   *pSatDevData,
                bit8              tag
               );

osGLOBAL FORCEINLINE void 
smsatDecrementPendingIO(
                        smRoot_t                *smRoot,
                        smIntContext_t          *smAllShared,
                        smSatIOContext_t        *satIOContext
                        );

osGLOBAL smSatIOContext_t * 
smsatPrepareNewIO(
                  smSatInternalIo_t       *satNewIntIo,
                  smIORequest_t           *smOrgIORequest,
                  smDeviceData_t          *satDevData,
                  smIniScsiCmnd_t         *scsiCmnd,
                  smSatIOContext_t        *satOrgIOContext
                 );

osGLOBAL void 
smsatSetDevInfo(
                 smDeviceData_t            *oneDeviceData,
                 agsaSATAIdentifyData_t    *SATAIdData
               );

osGLOBAL void 
smsatInquiryStandard(
                     bit8                    *pInquiry, 
                     agsaSATAIdentifyData_t  *pSATAIdData,
                     smIniScsiCmnd_t         *scsiCmnd
                    );

osGLOBAL void 
smsatInquiryPage0(
                   bit8                    *pInquiry, 
                   agsaSATAIdentifyData_t  *pSATAIdData
		 );

osGLOBAL void 
smsatInquiryPage83(
                    bit8                    *pInquiry, 
                    agsaSATAIdentifyData_t  *pSATAIdData,
                    smDeviceData_t          *oneDeviceData
		  );


osGLOBAL void 
smsatInquiryPage89(
                    bit8                    *pInquiry, 
                    agsaSATAIdentifyData_t  *pSATAIdData,
                    smDeviceData_t          *oneDeviceData,
                    bit32                   len
		  );

osGLOBAL void 
smsatInquiryPage80(
                    bit8                    *pInquiry, 
                    agsaSATAIdentifyData_t  *pSATAIdData
		   );

osGLOBAL void 
smsatInquiryPageB1(
                    bit8                    *pInquiry, 
                    agsaSATAIdentifyData_t  *pSATAIdData
		   );

osGLOBAL void 
smsatDefaultTranslation(
                        smRoot_t                  *smRoot, 
                        smIORequest_t             *smIORequest,
                        smSatIOContext_t            *satIOContext,
                        smScsiRspSense_t          *pSense,
                        bit8                      ataStatus,
                        bit8                      ataError,
                        bit32                     interruptContext 
                       );
		       
osGLOBAL bit32
smPhyControlSend(
                  smRoot_t             *smRoot, 
                  smDeviceData_t       *oneDeviceData,
                  bit8                 phyOp,
                  smIORequest_t        *CurrentTaskTag,
                  bit32                queueNumber 		  
                );

osGLOBAL bit32 
smsatTaskManagement(
                    smRoot_t          *smRoot, 
                    smDeviceHandle_t  *smDeviceHandle,
                    bit32             task,
                    smLUN_t           *lun,
                    smIORequest_t     *taskTag,
                    smIORequest_t     *currentTaskTag,
                    smIORequestBody_t *smIORequestBody
		   );
		       
osGLOBAL bit32 
smsatTmAbortTask(
                  smRoot_t                  *smRoot, 
                  smIORequest_t             *currentTaskTag, 
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *tiScsiRequest,
                  smSatIOContext_t            *satIOContext,
                  smIORequest_t             *taskTag);

osGLOBAL bit32  
smsatStartCheckPowerMode(
                         smRoot_t                  *smRoot, 
                         smIORequest_t             *currentTaskTag, 
                         smDeviceHandle_t          *smDeviceHandle,
                         smScsiInitiatorRequest_t  *smScsiRequest,
                         smSatIOContext_t            *satIOContext
                        );
osGLOBAL bit32  
smsatStartResetDevice(
                       smRoot_t                  *smRoot, 
                       smIORequest_t             *currentTaskTag, 
                       smDeviceHandle_t          *smDeviceHandle,
                       smScsiInitiatorRequest_t  *smScsiRequest,
                       smSatIOContext_t            *satIOContext
                     );
osGLOBAL void 
smsatAbort(
           smRoot_t          *smRoot,
           agsaRoot_t        *agRoot,
           smSatIOContext_t    *satIOContext
	  );

osGLOBAL smIORequestBody_t *
smDequeueIO(smRoot_t          *smRoot);

osGLOBAL bit32
smsatDecodeSATADeviceType(bit8 * pSignature);

/******************************** beginning of start ******************************************************/

/*! \brief SAT implementation for ATAPI Packet Command.
 *
 *  SAT implementation for ATAPI Packet and send FIS request to LL layer.
 * 
 *  \param   smRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   smIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   smDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   smScsiRequest:    Pointer to TISA SCSI I/O request and SGL list. 
 *  \param   smSatIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e smIOSuccess: 	  I/O request successfully initiated. 
 *    - \e smIOBusy:        No resources available, try again later. 
 *    - \e smIOIONoDevice:  Invalid device handle.
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
            );

osGLOBAL void 
smsatPacketCB(
            agsaRoot_t        *agRoot,
            agsaIORequest_t   *agIORequest,
            bit32             agIOStatus,
            agsaFisHeader_t   *agFirstDword,
            bit32             agIOInfoLen,
            void              *agParam,
            void              *ioContext
            );
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
 *    - \e smIOSuccess: 	  I/O request successfully initiated. 
 *    - \e smIOBusy:        No resources available, try again later. 
 *    - \e smIOIONoDevice:  Invalid device handle.
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
       );

osGLOBAL void 
smsatExecuteDeviceDiagnosticCB(
       agsaRoot_t        *agRoot,
       agsaIORequest_t   *agIORequest,
       bit32             agIOStatus,
       agsaFisHeader_t   *agFirstDword,
       bit32             agIOInfoLen,
       void              *agParam,
       void              *ioContext
       );
/* set feature for auto activate */       
osGLOBAL bit32
smsatSetFeaturesAA(
           smRoot_t                  *smRoot, 
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t            *satIOContext
           );
osGLOBAL void
smsatSetFeaturesAACB(
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
 *  \param   smRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   smIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   smDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   smScsiRequest:    Pointer to TISA SCSI I/O request and SGL list. 
 *  \param   smSatIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e smIOSuccess: 	  I/O request successfully initiated. 
 *    - \e smIOBusy:        No resources available, try again later. 
 *    - \e smIOIONoDevice:  Invalid device handle.
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
           );
osGLOBAL void
smsatSetFeaturesPIOCB(
          agsaRoot_t        *agRoot,
          agsaIORequest_t   *agIORequest,
          bit32             agIOStatus,
          agsaFisHeader_t   *agFirstDword,
          bit32             agIOInfoLen,
          void              *agParam,
          void              *ioContext
          );

osGLOBAL bit32
smsatSetFeaturesDMA(
           smRoot_t                  *smRoot, 
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t            *satIOContext
           );

osGLOBAL void
smsatSetFeaturesDMACB(
         agsaRoot_t        *agRoot,
         agsaIORequest_t   *agIORequest,
         bit32             agIOStatus,
         agsaFisHeader_t   *agFirstDword,
         bit32             agIOInfoLen,
         void              *agParam,
         void              *ioContext
         );

osGLOBAL bit32
smsatSetFeaturesReadLookAhead(
           smRoot_t                  *smRoot, 
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t            *satIOContext
           );

osGLOBAL void
smsatSetFeaturesReadLookAheadCB(
         agsaRoot_t        *agRoot,
         agsaIORequest_t   *agIORequest,
         bit32             agIOStatus,
         agsaFisHeader_t   *agFirstDword,
         bit32             agIOInfoLen,
         void              *agParam,
         void              *ioContext
         );

osGLOBAL bit32
smsatSetFeaturesVolatileWriteCache(
           smRoot_t                  *smRoot, 
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t            *satIOContext
           );

osGLOBAL void
smsatSetFeaturesVolatileWriteCacheCB(
         agsaRoot_t        *agRoot,
         agsaIORequest_t   *agIORequest,
         bit32             agIOStatus,
         agsaFisHeader_t   *agFirstDword,
         bit32             agIOInfoLen,
         void              *agParam,
         void              *ioContext
         );

osGLOBAL void
smsatSMARTEnablePassCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     agsaFrameHandle_t agFrameHandle,
                     void              *ioContext
                    );

osGLOBAL void 
smsatSMARTRStatusPassCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext                   
               );
osGLOBAL void 
smsatSMARTReadLogCB(
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
 *  \param   smRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   smIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   smDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   smScsiRequest:    Pointer to TISA SCSI I/O request and SGL list. 
 *  \param   smSatIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return If command is started successfully
 *    - \e smIOSuccess: 	  I/O request successfully initiated. 
 *    - \e smIOBusy:        No resources available, try again later. 
 *    - \e smIOIONoDevice:  Invalid device handle.
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
        );

osGLOBAL void 
smsatRequestSenseForATAPICB(
        agsaRoot_t        *agRoot,
        agsaIORequest_t   *agIORequest,
        bit32             agIOStatus,
        agsaFisHeader_t   *agFirstDword,
        bit32             agIOInfoLen,
        void              *agParam,
        void              *ioContext
        );

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
 *    - \e smIOSuccess: 	  I/O request successfully initiated. 
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
        );

osGLOBAL void
smsatDeviceResetCB(
         agsaRoot_t        *agRoot,
         agsaIORequest_t   *agIORequest,
         bit32             agIOStatus,
         agsaFisHeader_t   *agFirstDword,
         bit32             agIOInfoLen,
         void              *agParam,
         void              *ioContext
         );


osGLOBAL void 
smsatTranslateATAPIErrorsToSCSIErrors(
        bit8   bCommand,
        bit8   bATAStatus,
        bit8   bATAError,
        bit8   *pSenseKey,
        bit16  *pSenseCodeInfo
        );

GLOBAL void 
smsatTranslateATAErrorsToSCSIErrors(
    bit8   bATAStatus,
    bit8   bATAError,
    bit8   *pSenseKey,
    bit16  *pSenseCodeInfo
    );

/*****************************************************************************/

osGLOBAL bit32  
smsatRead6(
           smRoot_t                  *smRoot, 
           smIORequest_t             *smIORequest,
           smDeviceHandle_t          *smDeviceHandle,
           smScsiInitiatorRequest_t  *smScsiRequest,
           smSatIOContext_t            *satIOContext
	  );

osGLOBAL FORCEINLINE bit32  
smsatRead10(
            smRoot_t                  *smRoot, 
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
	   );

osGLOBAL bit32  
smsatRead12(
            smRoot_t                  *smRoot, 
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
	   );

osGLOBAL bit32  
smsatRead16(
            smRoot_t                  *smRoot, 
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
	   );

osGLOBAL bit32  
smsatWrite6(
            smRoot_t                  *smRoot, 
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
	   );

osGLOBAL FORCEINLINE bit32  
smsatWrite10(
             smRoot_t                  *smRoot, 
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
            );

osGLOBAL bit32  
smsatWrite12(
             smRoot_t                  *smRoot, 
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
            );

osGLOBAL bit32  
smsatWrite16(
             smRoot_t                  *smRoot, 
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
            );

osGLOBAL bit32  
smsatVerify10(
              smRoot_t                  *smRoot, 
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smScsiRequest,
              smSatIOContext_t            *satIOContext
             );

osGLOBAL bit32  
smsatVerify12(
              smRoot_t                  *smRoot, 
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smScsiRequest,
              smSatIOContext_t            *satIOContext
             );

osGLOBAL bit32  
smsatVerify16(
              smRoot_t                  *smRoot, 
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smScsiRequest,
              smSatIOContext_t            *satIOContext
             );

osGLOBAL bit32  
smsatTestUnitReady(
                   smRoot_t                  *smRoot, 
                   smIORequest_t             *smIORequest,
                   smDeviceHandle_t          *smDeviceHandle,
                   smScsiInitiatorRequest_t  *smScsiRequest,
                   smSatIOContext_t            *satIOContext
                  );

osGLOBAL bit32  
smsatInquiry(
             smRoot_t                  *smRoot, 
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
            );

osGLOBAL bit32  
smsatRequestSense(
                  smRoot_t                  *smRoot, 
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 );

osGLOBAL bit32  
smsatModeSense6(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               );

osGLOBAL bit32  
smsatModeSense10(
                  smRoot_t                  *smRoot, 
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 );

osGLOBAL bit32  
smsatReadCapacity10(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   );

osGLOBAL bit32  
smsatReadCapacity16(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   );

osGLOBAL bit32  
smsatReportLun(
               smRoot_t                  *smRoot, 
               smIORequest_t             *smIORequest,
               smDeviceHandle_t          *smDeviceHandle,
               smScsiInitiatorRequest_t  *smScsiRequest,
               smSatIOContext_t            *satIOContext
              );

osGLOBAL bit32  
smsatFormatUnit(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               );

osGLOBAL bit32  
smsatSendDiagnostic(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   );

osGLOBAL bit32  
smsatStartStopUnit(
                   smRoot_t                  *smRoot, 
                   smIORequest_t             *smIORequest,
                   smDeviceHandle_t          *smDeviceHandle,
                   smScsiInitiatorRequest_t  *smScsiRequest,
                   smSatIOContext_t            *satIOContext
                  );

osGLOBAL bit32  
smsatWriteSame10(
                  smRoot_t                  *smRoot, 
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 );

osGLOBAL bit32  
smsatWriteSame16(
                  smRoot_t                  *smRoot, 
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 );

osGLOBAL bit32  
smsatLogSense(
              smRoot_t                  *smRoot, 
              smIORequest_t             *smIORequest,
              smDeviceHandle_t          *smDeviceHandle,
              smScsiInitiatorRequest_t  *smScsiRequest,
              smSatIOContext_t            *satIOContext
             );

osGLOBAL bit32  
smsatModeSelect6(
                 smRoot_t                  *smRoot, 
                 smIORequest_t             *smIORequest,
                 smDeviceHandle_t          *smDeviceHandle,
                 smScsiInitiatorRequest_t  *smScsiRequest,
                 smSatIOContext_t            *satIOContext
                );


osGLOBAL bit32  
smsatModeSelect10(
                  smRoot_t                  *smRoot, 
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                 );

osGLOBAL bit32  
smsatSynchronizeCache10(
                        smRoot_t                  *smRoot, 
                        smIORequest_t             *smIORequest,
                        smDeviceHandle_t          *smDeviceHandle,
                        smScsiInitiatorRequest_t  *smScsiRequest,
                        smSatIOContext_t            *satIOContext
                       );

osGLOBAL bit32  
smsatSynchronizeCache16(
                        smRoot_t                  *smRoot, 
                        smIORequest_t             *smIORequest,
                        smDeviceHandle_t          *smDeviceHandle,
                        smScsiInitiatorRequest_t  *smScsiRequest,
                        smSatIOContext_t            *satIOContext
                       );

osGLOBAL bit32  
smsatWriteAndVerify10(
                      smRoot_t                  *smRoot, 
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
                     );

osGLOBAL bit32  
smsatWriteAndVerify12(
                      smRoot_t                  *smRoot, 
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
                     );

osGLOBAL bit32  
smsatWriteAndVerify16(
                      smRoot_t                  *smRoot, 
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
                     );

osGLOBAL bit32  
smsatReadMediaSerialNumber(
                           smRoot_t                  *smRoot, 
                           smIORequest_t             *smIORequest,
                           smDeviceHandle_t          *smDeviceHandle,
                           smScsiInitiatorRequest_t  *smScsiRequest,
                           smSatIOContext_t            *satIOContext
                          );

osGLOBAL bit32  
smsatReadBuffer(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               );

osGLOBAL bit32  
smsatWriteBuffer(
                 smRoot_t                  *smRoot, 
                 smIORequest_t             *smIORequest,
                 smDeviceHandle_t          *smDeviceHandle,
                 smScsiInitiatorRequest_t  *smScsiRequest,
                 smSatIOContext_t            *satIOContext
                );

osGLOBAL bit32  
smsatReassignBlocks(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   );

osGLOBAL bit32  
smsatPassthrough(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   );

osGLOBAL FORCEINLINE bit32  
smsataLLIOStart(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               );
osGLOBAL bit32  
smsatTestUnitReady_1(
                     smRoot_t                  *smRoot, 
                     smIORequest_t             *smIORequest,
                     smDeviceHandle_t          *smDeviceHandle,
                     smScsiInitiatorRequest_t  *smScsiRequest,
                     smSatIOContext_t            *satIOContext
                    );
osGLOBAL bit32
smsatStartIDDev(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               );

osGLOBAL bit32
smsatSendIDDev(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               );

osGLOBAL bit32
smsatRequestSense_1(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   );

osGLOBAL bit32
smsatSMARTEnable(
                 smRoot_t                  *smRoot, 
                 smIORequest_t             *smIORequest,
                 smDeviceHandle_t          *smDeviceHandle,
                 smScsiInitiatorRequest_t  *smScsiRequest,
                 smSatIOContext_t            *satIOContext
               );

osGLOBAL bit32
smsatLogSense_2(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               );

osGLOBAL bit32
smsatLogSense_3(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               );

osGLOBAL bit32
smsatRead_1(
            smRoot_t                  *smRoot, 
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext
          );

osGLOBAL bit32
smsatWrite_1(
             smRoot_t                  *smRoot, 
             smIORequest_t             *smIORequest,
             smDeviceHandle_t          *smDeviceHandle,
             smScsiInitiatorRequest_t  *smScsiRequest,
             smSatIOContext_t            *satIOContext
           );

osGLOBAL bit32
smsatNonChainedWriteNVerify_Verify(
                                   smRoot_t                  *smRoot, 
                                   smIORequest_t             *smIORequest,
                                   smDeviceHandle_t          *smDeviceHandle,
                                   smScsiInitiatorRequest_t  *smScsiRequest,
                                   smSatIOContext_t            *satIOContext
                                  );

osGLOBAL bit32
smsatChainedWriteNVerify_Start_Verify(
                                      smRoot_t                  *smRoot, 
                                      smIORequest_t             *smIORequest,
                                      smDeviceHandle_t          *smDeviceHandle,
                                      smScsiInitiatorRequest_t  *smScsiRequest,
                                      smSatIOContext_t            *satIOContext
                                     );

osGLOBAL bit32
smsatChainedWriteNVerify_Write(
                               smRoot_t                  *smRoot, 
                               smIORequest_t             *smIORequest,
                               smDeviceHandle_t          *smDeviceHandle,
                               smScsiInitiatorRequest_t  *smScsiRequest,
                               smSatIOContext_t            *satIOContext
                              );
		   		   
osGLOBAL bit32
smsatChainedWriteNVerify_Verify(
                                smRoot_t                  *smRoot, 
                                smIORequest_t             *smIORequest,
                                smDeviceHandle_t          *smDeviceHandle,
                                smScsiInitiatorRequest_t  *smScsiRequest,
                                smSatIOContext_t            *satIOContext
                               );
osGLOBAL bit32 
smsatChainedVerify(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
		   );

osGLOBAL bit32 
smsatWriteSame10_1(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext,
                    bit32                     lba
                  );

osGLOBAL bit32 
smsatWriteSame10_2(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext,
                    bit32                     lba
                  );

osGLOBAL bit32 
smsatWriteSame10_3(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext,
                    bit32                     lba
                  );

osGLOBAL bit32 
smsatStartStopUnit_1(
                     smRoot_t                  *smRoot, 
                     smIORequest_t             *smIORequest,
                     smDeviceHandle_t          *smDeviceHandle,
                     smScsiInitiatorRequest_t  *smScsiRequest,
                     smSatIOContext_t            *satIOContext
		    );

osGLOBAL bit32 
smsatSendDiagnostic_1(
                      smRoot_t                  *smRoot, 
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
		     );
		     
osGLOBAL bit32 
smsatSendDiagnostic_2(
                      smRoot_t                  *smRoot, 
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
		     );

osGLOBAL bit32 
smsatModeSelect6n10_1(
                      smRoot_t                  *smRoot, 
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext
		     );

osGLOBAL bit32 
smsatLogSense_1(
                smRoot_t                  *smRoot, 
                smIORequest_t             *smIORequest,
                smDeviceHandle_t          *smDeviceHandle,
                smScsiInitiatorRequest_t  *smScsiRequest,
                smSatIOContext_t            *satIOContext
               );

osGLOBAL bit32 
smsatReassignBlocks_2(
                      smRoot_t                  *smRoot, 
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext,
                      bit8                      *LBA
                     );

osGLOBAL bit32 
smsatReassignBlocks_1(
                      smRoot_t                  *smRoot, 
                      smIORequest_t             *smIORequest,
                      smDeviceHandle_t          *smDeviceHandle,
                      smScsiInitiatorRequest_t  *smScsiRequest,
                      smSatIOContext_t            *satIOContext,
                      smSatIOContext_t            *satOrgIOContext
                     );

osGLOBAL bit32 
smsatSendReadLogExt(
                     smRoot_t                  *smRoot, 
                     smIORequest_t             *smIORequest,
                     smDeviceHandle_t          *smDeviceHandle,
                     smScsiInitiatorRequest_t  *smScsiRequest,
                     smSatIOContext_t            *satIOContext
		   );

osGLOBAL bit32  
smsatCheckPowerMode(
                     smRoot_t                  *smRoot, 
                     smIORequest_t             *smIORequest,
                     smDeviceHandle_t          *smDeviceHandle,
                     smScsiInitiatorRequest_t  *smScsiRequest,
                     smSatIOContext_t            *satIOContext
		   );

osGLOBAL bit32  
smsatResetDevice(
                  smRoot_t                  *smRoot, 
                  smIORequest_t             *smIORequest,
                  smDeviceHandle_t          *smDeviceHandle,
                  smScsiInitiatorRequest_t  *smScsiRequest,
                  smSatIOContext_t            *satIOContext
                );
		
osGLOBAL bit32  
smsatDeResetDevice(
                    smRoot_t                  *smRoot, 
                    smIORequest_t             *smIORequest,
                    smDeviceHandle_t          *smDeviceHandle,
                    smScsiInitiatorRequest_t  *smScsiRequest,
                    smSatIOContext_t            *satIOContext
                   );
/******************************** beginning of completion ******************************************************/
osGLOBAL FORCEINLINE void 
smllSATACompleted(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  void              *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam
                 );


osGLOBAL FORCEINLINE void 
smsatNonChainedDataIOCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        void              *agParam,
                        void              *ioContext
                       );

osGLOBAL FORCEINLINE void 
smsatChainedDataIOCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     void              *agParam,
                     void              *ioContext
                    );

osGLOBAL void 
smsatNonChainedVerifyCB(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        bit32             agIOStatus,
                        agsaFisHeader_t   *agFirstDword,
                        bit32             agIOInfoLen,
                        agsaFrameHandle_t agFrameHandle,
                        void              *ioContext                          
                       );

osGLOBAL void 
smsatChainedVerifyCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     agsaFrameHandle_t agFrameHandle,
                     void              *ioContext                          
                    );
		    
osGLOBAL void 
smsatTestUnitReadyCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     agsaFrameHandle_t agFrameHandle,
                     void              *ioContext
                    );
osGLOBAL void 
smsatRequestSenseCB(
                    agsaRoot_t        *agRoot,
                    agsaIORequest_t   *agIORequest,
                    bit32             agIOStatus,
                    agsaFisHeader_t   *agFirstDword,
                    bit32             agIOInfoLen,
                    void              *agParam,
                    void              *ioContext
                   );  

osGLOBAL void 
smsatSendDiagnosticCB(
                       agsaRoot_t        *agRoot,
                       agsaIORequest_t   *agIORequest,
                       bit32             agIOStatus,
                       agsaFisHeader_t   *agFirstDword,
                       bit32             agIOInfoLen,
                       agsaFrameHandle_t agFrameHandle,
                       void              *ioContext                     
                     );

osGLOBAL void 
smsatStartStopUnitCB(
                     agsaRoot_t        *agRoot,
                     agsaIORequest_t   *agIORequest,
                     bit32             agIOStatus,
                     agsaFisHeader_t   *agFirstDword,
                     bit32             agIOInfoLen,
                     agsaFrameHandle_t agFrameHandle,
                     void              *ioContext
                    );


osGLOBAL void 
smsatWriteSame10CB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext                     
                  );


osGLOBAL void 
smsatLogSenseCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioCotext                   
               );

osGLOBAL void 
smsatSMARTEnableCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                  ); 

osGLOBAL void 
smsatModeSelect6n10CB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                     );

osGLOBAL void 
smsatSynchronizeCache10n16CB(
                             agsaRoot_t        *agRoot,
                             agsaIORequest_t   *agIORequest,
                             bit32             agIOStatus,
                             agsaFisHeader_t   *agFirstDword,
                             bit32             agIOInfoLen,
                             agsaFrameHandle_t agFrameHandle,
                             void              *ioContext
                            );

osGLOBAL void 
smsatNonChainedWriteNVerifyCB(
                              agsaRoot_t        *agRoot,
                              agsaIORequest_t   *agIORequest,
                              bit32             agIOStatus,
                              agsaFisHeader_t   *agFirstDword,
                              bit32             agIOInfoLen,
                              void              *agParam,
                              void              *ioContext
                             );

osGLOBAL void 
smsatChainedWriteNVerifyCB(
                           agsaRoot_t        *agRoot,
                           agsaIORequest_t   *agIORequest,
                           bit32             agIOStatus,
                           agsaFisHeader_t   *agFirstDword,
                           bit32             agIOInfoLen,
                           void              *agParam,
                           void              *ioContext
                          );

osGLOBAL void 
smsatReadMediaSerialNumberCB(
                             agsaRoot_t        *agRoot,
                             agsaIORequest_t   *agIORequest,
                             bit32             agIOStatus,
                             agsaFisHeader_t   *agFirstDword,
                             bit32             agIOInfoLen,
                             agsaFrameHandle_t agFrameHandle,
                             void              *ioContext
                            );  

osGLOBAL void 
smsatReadBufferCB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  agsaFrameHandle_t agFrameHandle,
                  void              *ioContext
                 );  

osGLOBAL void 
smsatWriteBufferCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   agsaFrameHandle_t agFrameHandle,
                   void              *ioContext
                  );  

osGLOBAL void 
smsatReassignBlocksCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                     );  

osGLOBAL void 
smsatProcessAbnormalCompletion(
                               agsaRoot_t        *agRoot,
                               agsaIORequest_t   *agIORequest,
                               bit32             agIOStatus,
                               agsaFisHeader_t   *agFirstDword,
                               bit32             agIOInfoLen,
                               void              *agParam,
                               smSatIOContext_t    *satIOContext
                              );

osGLOBAL void 
smsatDelayedProcessAbnormalCompletion(
                                      agsaRoot_t        *agRoot,
                                      agsaIORequest_t   *agIORequest,
                                      bit32             agIOStatus,
                                      agsaFisHeader_t   *agFirstDword,
                                      bit32             agIOInfoLen,
                                      void              *agParam,
                                      smSatIOContext_t    *satIOContext
                                     );

osGLOBAL void 
smsatIOCompleted(
                 smRoot_t           *smRoot,
                 smIORequest_t      *smIORequest,
                 agsaFisHeader_t    *agFirstDword,
                 bit32              respFisLen,
                 agsaFrameHandle_t  agFrameHandle,
                 smSatIOContext_t     *satIOContext,
                 bit32              interruptContext
		);

osGLOBAL void 
smsatEncryptionHandler(
                       smRoot_t                *smRoot,
                       agsaIORequest_t         *agIORequest, 
                       bit32                   agIOStatus,  
                       bit32                   agIOInfoLen,
                       void                    *agParam,
                       bit32                   agOtherInfo,
                       bit32                   interruptContext
                      );

osGLOBAL void 
smsatDifHandler(
                smRoot_t                *smRoot,
                agsaIORequest_t         *agIORequest, 
                bit32                   agIOStatus,  
                bit32                   agIOInfoLen,
                void                    *agParam,
                bit32                   agOtherInfo,
                bit32                   interruptContext
               );
	       
osGLOBAL void 
smsatProcessAbort(
                  smRoot_t           *smRoot,
                  smIORequest_t      *smIORequest,
                  smSatIOContext_t     *satIOContext
                 );

osGLOBAL void 
smsatNonDataIOCB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam,
                  void              *ioContext
                 );

osGLOBAL void 
smsatInquiryCB(
               agsaRoot_t        *agRoot,
               agsaIORequest_t   *agIORequest,
               bit32             agIOStatus,
               agsaFisHeader_t   *agFirstDword,
               bit32             agIOInfoLen,
               void              *agParam,
               void              *ioContext
              );


osGLOBAL void 
smsatInquiryIntCB(
                   smRoot_t                  *smRoot, 
                   smIORequest_t             *smIORequest,
                   smDeviceHandle_t          *smDeviceHandle,
                   smScsiInitiatorRequest_t  *smScsiRequest,
                   smSatIOContext_t            *satIOContext
                  );

osGLOBAL void 
smsatVerify10CB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext
               );

osGLOBAL void 
smsatReadLogExtCB(
                   agsaRoot_t        *agRoot,
                   agsaIORequest_t   *agIORequest,
                   bit32             agIOStatus,
                   agsaFisHeader_t   *agFirstDword,
                   bit32             agIOInfoLen,
                   void              *agParam,
                   void              *ioContext                     
                 );


osGLOBAL void 
smsatIDStartCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext
               );

osGLOBAL void
smSMPCompleted(
                agsaRoot_t            *agRoot,
                agsaIORequest_t       *agIORequest,
                bit32                 agIOStatus,
                bit32                 agIOInfoLen,
                agsaFrameHandle_t     agFrameHandle                   
              );

osGLOBAL void
smSMPCompletedCB(
                  agsaRoot_t            *agRoot,
                  agsaIORequest_t       *agIORequest,
                  bit32                 agIOStatus,
                  bit32                 agIOInfoLen,
                  agsaFrameHandle_t     agFrameHandle                   
                );
		
osGLOBAL void
smPhyControlRespRcvd(
                      smRoot_t              *smRoot,
                      agsaRoot_t            *agRoot,
                      agsaIORequest_t       *agIORequest,
                      smDeviceData_t        *oneDeviceData,
                      smSMPFrameHeader_t    *frameHeader,
                      agsaFrameHandle_t     frameHandle,
                      smIORequest_t         *CurrentTaskTag
                     );

osGLOBAL void 
smsatCheckPowerModeCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                     );

osGLOBAL void 
smsatCheckPowerModePassCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                     );

osGLOBAL void 
smsatIDDataPassCB(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  agsaFisHeader_t   *agFirstDword,
                  bit32             agIOInfoLen,
                  agsaFrameHandle_t agFrameHandle,
                  void              *ioContext
                 );

osGLOBAL void 
smsatResetDeviceCB(
                    agsaRoot_t        *agRoot,
                    agsaIORequest_t   *agIORequest,
                    bit32             agIOStatus,
                    agsaFisHeader_t   *agFirstDword,
                    bit32             agIOInfoLen,
                    agsaFrameHandle_t agFrameHandle,
                    void              *ioContext
                  );

osGLOBAL void 
smsatDeResetDeviceCB(
                      agsaRoot_t        *agRoot,
                      agsaIORequest_t   *agIORequest,
                      bit32             agIOStatus,
                      agsaFisHeader_t   *agFirstDword,
                      bit32             agIOInfoLen,
                      agsaFrameHandle_t agFrameHandle,
                      void              *ioContext
                    );
osGLOBAL void 
smaSATAAbortCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             flag,
                bit32             status
	      );
		    
osGLOBAL void 
smLocalPhyControlCB(
                     agsaRoot_t     *agRoot,
                     agsaContext_t  *agContext,
                     bit32          phyId,
                     bit32          phyOperation,
                     bit32          status,
                     void           *parm
                    );
/******************************** end of completion ***********************************************************/

/******************************** start of utils    ***********************************************************/
osGLOBAL bit32 smsatComputeCDB10LBA(smSatIOContext_t            *satIOContext);
osGLOBAL bit32 smsatComputeCDB10TL(smSatIOContext_t            *satIOContext);
osGLOBAL bit32 smsatComputeCDB12LBA(smSatIOContext_t            *satIOContext);
osGLOBAL bit32 smsatComputeCDB12TL(smSatIOContext_t            *satIOContext);
osGLOBAL bit32 smsatComputeCDB16LBA(smSatIOContext_t            *satIOContext);
osGLOBAL bit32 smsatComputeCDB16TL(smSatIOContext_t            *satIOContext);
osGLOBAL FORCEINLINE bit32 smsatComputeLoopNum(bit32 a, bit32 b);
osGLOBAL FORCEINLINE bit32 smsatCheckLimit(bit8 *lba, bit8 *tl, int flag, smDeviceData_t *pSatDevData);

osGLOBAL void  
smsatSplitSGL(
            smRoot_t                  *smRoot, 
            smIORequest_t             *smIORequest,
            smDeviceHandle_t          *smDeviceHandle,
            smScsiInitiatorRequest_t  *smScsiRequest,
            smSatIOContext_t            *satIOContext,
            bit32                     split,
            bit32                     tl,
            bit32                     flag
	   );

osGLOBAL void  
smsatPrintSgl(
            smRoot_t                  *smRoot, 
            agsaEsgl_t                *agEsgl,
            bit32                     idx
            );
/******************************** end   of utils    ***********************************************************/


osGLOBAL void 
smsatPassthroughCB(
                agsaRoot_t        *agRoot,
                agsaIORequest_t   *agIORequest,
                bit32             agIOStatus,
                agsaFisHeader_t   *agFirstDword,
                bit32             agIOInfoLen,
                void              *agParam,
                void              *ioContext                   
               );


#endif                          /* __SMPROTO_H__ */

