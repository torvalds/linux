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
/*! \file saapi.h
 *  \brief The file defines the declaration of tSDK APIs
 *
 *
 *
 *
 */
/******************************************************************************/

#ifndef  __SSDKAPI_H__
#define __SSDKAPI_H__

/********************************************************************************
 *                             SA LL Function Declaration                       *
 ********************************************************************************/

/***************************************************************************
 * Definition of interrupt related functions start                         *
 ***************************************************************************/


GLOBAL bit32 FORCEINLINE saDelayedInterruptHandler(
                              agsaRoot_t  *agRoot,
                              bit32       interruptVectorIndex,
                              bit32       count
                              );

GLOBAL bit32 FORCEINLINE saInterruptHandler(
                              agsaRoot_t  *agRoot,
                              bit32       interruptVectorIndex
                              );

GLOBAL void saSystemInterruptsActive(
                              agsaRoot_t  *agRoot,
                              agBOOLEAN     sysIntsActive
                              );

GLOBAL FORCEINLINE void saSystemInterruptsEnable(
                              agsaRoot_t  *agRoot,
                              bit32       interruptVectorIndex
                              );
/***************************************************************************
 * Definition of interrupt related functions end                           *
 ***************************************************************************/


/***************************************************************************
 * Definition of timer related functions start                             *
 ***************************************************************************/
GLOBAL void saTimerTick(agsaRoot_t  *agRoot);
/***************************************************************************
 * Definition of timer related functions end                               *
 ***************************************************************************/

/***************************************************************************
 * Definition of initialization related functions start                    *
 ***************************************************************************/
GLOBAL void saGetRequirements(
                              agsaRoot_t              *agRoot,
                              agsaSwConfig_t          *swConfig,
                              agsaMemoryRequirement_t *memoryRequirement,
                              bit32                   *usecsPerTick,
                              bit32                   *maxNumLocks
                              );

GLOBAL bit32 saInitialize(
                          agsaRoot_t                  *agRoot,
                          agsaMemoryRequirement_t     *memoryAllocated,
                          agsaHwConfig_t              *hwConfig,
                          agsaSwConfig_t              *swConfig,
                          bit32                       usecsPerTick
                          );
/***************************************************************************
 * Definition of initialization related functions end                      *
 ***************************************************************************/

/***************************************************************************
 * Definition of hardware related functions start                          *
 ***************************************************************************/
GLOBAL void saHwReset(
                      agsaRoot_t  *agRoot,
                      bit32       resetType,
                      bit32       resetParm
                      );

GLOBAL void saHwShutdown(agsaRoot_t *agRoot);

/***************************************************************************
 * Definition of hardware related functions end                            *
 ***************************************************************************/

/***************************************************************************
 * Definition of phy related functions start                               *
 ***************************************************************************/
GLOBAL bit32 saPhyStart(
                        agsaRoot_t          *agRoot,
                        agsaContext_t       *agContext,
                        bit32               queueNum,
                        bit32               phyId,
                        agsaPhyConfig_t     *agPhyConfig,
                        agsaSASIdentify_t   *agSASIdentify
                        );

GLOBAL bit32 saPhyStop(
                      agsaRoot_t            *agRoot,
                      agsaContext_t         *agContext,
                      bit32                 queueNum,
                      bit32                 phyId
                      );


GLOBAL bit32 saLocalPhyControl(
                      agsaRoot_t             *agRoot,
                      agsaContext_t          *agContext,
                      bit32                   queueNum,
                      bit32                   phyId,
                      bit32                   phyOperation,
                      ossaLocalPhyControlCB_t agCB
                      );

GLOBAL bit32 saGetPhyProfile(
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                    queueNum,
                      bit32                    ppc,
                      bit32                    phyID
                      );

GLOBAL bit32 saSetPhyProfile (
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                    queueNum,
                      bit32                    ppc,
                      bit32                    length,
                      void                     *buffer,
                      bit32                    phyID
                      );

GLOBAL bit32 saHwEventAck(
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      bit32              queueNum,
                      agsaEventSource_t *eventSource,
                      bit32              param0,
                      bit32              param1
                      );


/***************************************************************************
 * Definition of phy related functions end                                 *
 ***************************************************************************/

/***************************************************************************
 * Definition of discovery related functions start                         *
 ***************************************************************************/
GLOBAL bit32 saDiscover(
                        agsaRoot_t          *agRoot,
                        agsaPortContext_t   *agPortContext,
                        bit32               type,
                        bit32               option
                        );
/***************************************************************************
 * Definition of discovery related functions end                           *
 ***************************************************************************/

/***************************************************************************
 * Definition of frame related functions start                             *
 ***************************************************************************/
GLOBAL bit32 saFrameReadBit32(
                        agsaRoot_t          *agRoot,
                        agsaFrameHandle_t   agFrame,
                        bit32               frameOffset
                        );

GLOBAL void saFrameReadBlock(
                        agsaRoot_t          *agRoot,
                        agsaFrameHandle_t   agFrame,
                        bit32               frameOffset,
                        void                *frameBuffer,
                        bit32               frameBufLen
                        );
/***************************************************************************
 * Definition of frame related functions end                               *
 ***************************************************************************/

/***************************************************************************
 * Definition of SATA related functions start                              *
 ***************************************************************************/
GLOBAL bit32 saSATAStart(
                        agsaRoot_t                  *agRoot,
                        agsaIORequest_t             *agIORequest,
                        bit32                       queueNum,
                        agsaDevHandle_t             *agDevHandle,
                        bit32                       agRequestType,
                        agsaSATAInitiatorRequest_t  *agSATAReq,
                        bit8                        agTag,
                        ossaSATACompletedCB_t       agCB
                        );

GLOBAL bit32 saSATAAbort(
                        agsaRoot_t                  *agRoot,
                        agsaIORequest_t             *agIORequest,
                        bit32                       queueNum,
                        agsaDevHandle_t             *agDevHandle,
                        bit32                       flag,
                        void                        *abortParam,
                        ossaGenericAbortCB_t        agCB
                        );

/***************************************************************************
 * Definition of SATA related functions end                                *
 ***************************************************************************/

/***************************************************************************
 * Definition of SAS related functions start                               *
 ***************************************************************************/

GLOBAL bit32 saSendSMPIoctl(
						agsaRoot_t                *agRoot,
						agsaDevHandle_t           *agDevHandle,
						bit32                      queueNum,
						agsaSMPFrame_t            *pSMPFrame,  
						ossaSMPCompletedCB_t       agCB
						);

GLOBAL bit32 saSMPStart(
                        agsaRoot_t                *agRoot,
                        agsaIORequest_t           *agIORequest,
                        bit32                     queueNum,
                        agsaDevHandle_t           *agDevHandle,
                        bit32                     agRequestType,
                        agsaSASRequestBody_t      *agRequestBody,
                        ossaSMPCompletedCB_t      agCB
                        );

GLOBAL bit32 saSMPAbort(
                        agsaRoot_t                *agRoot,
                        agsaIORequest_t           *agIORequest,
                        bit32                     queueNum,
                        agsaDevHandle_t           *agDevHandle,
                        bit32                     flag,
                        void                      *abortParam,
                        ossaGenericAbortCB_t      agCB
                        );

GLOBAL bit32 saSSPStart(
                        agsaRoot_t                *agRoot,
                        agsaIORequest_t           *agIORequest,
                        bit32                     queueNum,
                        agsaDevHandle_t           *agDevHandle,
                        bit32                     agRequestType,
                        agsaSASRequestBody_t      *agRequestBody,
                        agsaIORequest_t           *agTMRequest,
                        ossaSSPCompletedCB_t      agCB
                        );

#ifdef FAST_IO_TEST
GLOBAL void *saFastSSPPrepare(
                        void                 *ioHandle,
                        agsaFastCommand_t    *fc,
                        ossaSSPCompletedCB_t cb,
                        void                 *cbArg);

GLOBAL bit32 saFastSSPSend(void    *ioHandle);
GLOBAL bit32 saFastSSPCancel(void  *ioHandle);
#endif

GLOBAL bit32 saSSPAbort(
                        agsaRoot_t                *agRoot,
                        agsaIORequest_t           *agIORequest,
                        bit32                     queueNum,
                        agsaDevHandle_t           *agDevHandle,
                        bit32                     flag,
                        void                      *abortParam,
                        ossaGenericAbortCB_t      agCB
                        );

GLOBAL void saGetDifErrorDetails(
                        agsaRoot_t                *agRoot,
                        agsaIORequest_t           *agIORequest,
                        agsaDifDetails_t          *difDetails
                        );

GLOBAL bit32 saRegisterEventCallback(
                        agsaRoot_t                *agRoot,
                        bit32                     eventSourceType,
                        ossaGenericCB_t           callbackPtr
                        );

/***************************************************************************
 * Definition of SAS related functions end                                 *
 ***************************************************************************/

/***************************************************************************
 * Definition of Device related functions start                            *
 ***************************************************************************/
GLOBAL bit32 saRegisterNewDevice(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaDeviceInfo_t          *agDeviceInfo,
                        agsaPortContext_t         *agPortContext,
                        bit16                     hostAssignedDeviceId
                        );

GLOBAL bit32 saDeregisterDeviceHandle(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        agsaDevHandle_t           *agDevHandle,
                        bit32                     queueNum
                        );

GLOBAL bit32 saGetDeviceHandles(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaPortContext_t         *agPortContext,
                        bit32                     flags,
                        agsaDevHandle_t           *agDev[],
                        bit32                     skipCount,
                        bit32                     maxDevs
                        );

GLOBAL bit32 saGetDeviceInfo(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     option,
                        bit32                     queueNum,
                        agsaDevHandle_t           *agDevHandle
                        );

GLOBAL bit32 saGetDeviceState(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaDevHandle_t           *agDevHandle
                        );

GLOBAL bit32 saSetDeviceInfo(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum ,
                        agsaDevHandle_t           *agDevHandle,
                        bit32                     option,
                        bit32                     param,
                        ossaSetDeviceInfoCB_t   agCB
                        );

GLOBAL bit32 saSetDeviceState(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaDevHandle_t           *agDevHandle,
                        bit32                     newDeviceState
                        );

/***************************************************************************
 * Definition of Device related functions end                              *
 ***************************************************************************/

/***************************************************************************
 * Definition of Misc related functions start                              *
 ***************************************************************************/
GLOBAL bit32 saFwFlashUpdate(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaUpdateFwFlash_t       *flashUpdateInfo
                        );

GLOBAL bit32 saFlashExtExecute (
                        agsaRoot_t            *agRoot,
                        agsaContext_t         *agContext,
                        bit32                 queueNum,
                        agsaFlashExtExecute_t *agFlashExtExe
                        );

#ifdef SPC_ENABLE_PROFILE
GLOBAL bit32 saFwProfile(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaFwProfile_t           *fwProfileInfo
                        );
#endif

GLOBAL bit32 saEchoCommand(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        void                      *echoPayload
                        );

GLOBAL bit32 saGetControllerInfo(
                        agsaRoot_t                *agRoot,
                        agsaControllerInfo_t      *controllerInfo
                        );

GLOBAL bit32 saGetControllerStatus(
                          agsaRoot_t              *agRoot,
                          agsaControllerStatus_t  *controllerStatus
                        );

GLOBAL bit32 saGetControllerEventLogInfo(
                        agsaRoot_t                 *agRoot,
                          agsaControllerEventLog_t *eventLogInfo
                        );

GLOBAL bit32 saGpioEventSetup(
                        agsaRoot_t                 *agRoot,
                        agsaContext_t              *agContext,
                        bit32                      queueNum,
                        agsaGpioEventSetupInfo_t   *gpioEventSetupInfo
                        );

GLOBAL bit32 saGpioPinSetup(
                        agsaRoot_t                 *agRoot,
                        agsaContext_t              *agContext,
                        bit32                      queueNum,
                        agsaGpioPinSetupInfo_t     *gpioPinSetupInfo
                        );

GLOBAL bit32 saGpioRead(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum
                        );

GLOBAL bit32 saGpioWrite(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        bit32                     gpioWriteMask,
                        bit32                     gpioWriteValue
                        );

GLOBAL bit32 saSASDiagExecute(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaSASDiagExecute_t      *diag
                        );

GLOBAL bit32 saSASDiagStartEnd(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        bit32                     phyId,
                        bit32                     operation
                        );

GLOBAL bit32 saGetTimeStamp(
                        agsaRoot_t    *agRoot,
                        agsaContext_t *agContext,
                        bit32         queueNum
                        );

GLOBAL bit32 saPortControl(
                        agsaRoot_t        *agRoot,
                        agsaContext_t     *agContext,
                        bit32             queueNum,
                        agsaPortContext_t *agPortContext,
                        bit32             portOperation,
                        bit32             param0,
                        bit32             param1
                        );

GLOBAL bit32 saGetRegisterDump(
                        agsaRoot_t        *agRoot,
                        agsaContext_t     *agContext,
                        bit32             queueNum,
                        agsaRegDumpInfo_t *regDumpInfo
                        );

GLOBAL bit32 saGetForensicData(
                        agsaRoot_t          *agRoot,
                        agsaContext_t       *agContext,
                        agsaForensicData_t  *forensicData
                        );

bit32 saGetIOErrorStats(
                         agsaRoot_t        *agRoot,
                         agsaContext_t     *agContext,
                         bit32              flag
                         );

bit32 saGetIOEventStats(
                         agsaRoot_t        *agRoot,
                         agsaContext_t     *agContext,
                         bit32              flag
                         );

GLOBAL bit32 saGetNVMDCommand(
                        agsaRoot_t        *agRoot,
                        agsaContext_t     *agContext,
                        bit32             queueNum,
                        agsaNVMDData_t    *NVMDInfo
                        );

GLOBAL bit32 saSetNVMDCommand(
                        agsaRoot_t        *agRoot,
                        agsaContext_t     *agContext,
                        bit32             queueNum,
                        agsaNVMDData_t    *NVMDInfo
                        );

GLOBAL bit32 saReconfigSASParams(
                        agsaRoot_t        *agRoot,
                        agsaContext_t     *agContext,
                        bit32             queueNum ,
                        agsaSASReconfig_t *agSASConfig
                        );

GLOBAL bit32 saSgpio(
                agsaRoot_t              *agRoot,
                agsaContext_t           *agContext,
                bit32                   queueNum,
                agsaSGpioReqResponse_t  *pSGpioReq
                );

GLOBAL bit32 saPCIeDiagExecute(
                        agsaRoot_t             *agRoot,
                        agsaContext_t          *agContext,
                        bit32                   queueNum,
                        agsaPCIeDiagExecute_t  *diag);


GLOBAL bit32 saEncryptSelftestExecute(
                        agsaRoot_t    *agRoot,
                        agsaContext_t *agContext,
                        bit32          queueNum,
                        bit32          type,
                        bit32          length,
                        void          *TestDescriptor);

GLOBAL bit32 saSetOperator(
                  agsaRoot_t     *agRoot,
                  agsaContext_t  *agContext,
                  bit32           queueNum,
                  bit32           flag,
                  void           *cert);

GLOBAL bit32 saGetOperator(
                  agsaRoot_t     *agRoot,
                  agsaContext_t  *agContext,
                  bit32           queueNum,
                  bit32           option,
                  bit32           AddrHi,
                  bit32           AddrLo);

GLOBAL bit32 saOperatorManagement(
                        agsaRoot_t           *agRoot,
                        agsaContext_t        *agContext,
                        bit32                 queueNum,
                        bit32                 flag,
                        bit8                  role,
                        agsaID_t              *id,
                        agsaEncryptKekBlob_t  *kblob);


/***************************************************************************
 * Definition of Misc. related functions end                               *
 ***************************************************************************/

GLOBAL bit32 saSetControllerConfig(
                      agsaRoot_t        *agRoot,
                      bit32             queueNum,
                      bit32             modePage,
                      bit32             length,
                      void              *buffer,
                      agsaContext_t     *agContext
                      );


GLOBAL bit32 saGetControllerConfig(
                      agsaRoot_t        *agRoot,
                      bit32             queueNum,
                      bit32             modePage,
                      bit32             flag0,
                      bit32             flag1,
                      agsaContext_t     *agContext
                      );

GLOBAL bit32 saEncryptDekCacheUpdate(
                     agsaRoot_t        *agRoot,
                     agsaContext_t     *agContext,
                     bit32             queueNum,
                     bit32             kekIndex,
                     bit32             dekTableSelect,
                     bit32             dekAddrHi,
                     bit32             dekAddrLo,
                     bit32             dekIndex,
                     bit32             dekNumberOfEntries,
                     bit32             dekBlobFormat,
                     bit32             dekTableKeyEntrySize
                     );

GLOBAL bit32 saEncryptDekCacheInvalidate(
                    agsaRoot_t         *agRoot,
                    agsaContext_t      *agContext,
                    bit32              queueNum,
                    bit32              dekTable,
                    bit32              dekIndex
                    );

GLOBAL bit32 saEncryptGetMode(
                    agsaRoot_t         *agRoot,
                    agsaContext_t      *agContext,
                    agsaEncryptInfo_t  *encryptInfo
                    );

GLOBAL bit32 saEncryptSetMode (
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      bit32             queueNum,
                      agsaEncryptInfo_t *mode
                      );

GLOBAL bit32 saEncryptKekInvalidate(
                    agsaRoot_t         *agRoot,
                     agsaContext_t     *agContext,
                    bit32              queueNum,
                    bit32              kekIndex
                    );

GLOBAL bit32 saEncryptKekUpdate(
                    agsaRoot_t         *agRoot,
                    agsaContext_t     *agContext,
                    bit32              queueNum,
                    bit32              flags,
                    bit32              newKekIndex,
                    bit32              wrapperKekIndex,
                    bit32              blobFormat,
                    agsaEncryptKekBlob_t *encryptKekBlob
                    );

#ifdef HIALEAH_ENCRYPTION
GLOBAL bit32 saEncryptHilUpdate(
                    agsaRoot_t         *agRoot,
                    agsaContext_t      *agContext,
                    bit32              queueNum
                    );
#endif /* HIALEAH_ENCRYPTION */

GLOBAL bit32 saGetDFEData(
                          agsaRoot_t    *agRoot,
                          agsaContext_t   *agContext,
                          bit32     queueNum,
                          bit32                 interface,
                          bit32                 laneNumber,
                          bit32                 interations,
                          agsaSgl_t             *agSgl);


GLOBAL bit32 saFatalInterruptHandler(
                          agsaRoot_t  *agRoot,
                          bit32       interruptVectorIndex
  );


GLOBAL bit32 saDIFEncryptionOffloadStart(
                          agsaRoot_t         *agRoot,
                          agsaContext_t      *agContext,
                          bit32               queueNum,
                          bit32               op,
                          agsaDifEncPayload_t *agsaDifEncPayload,
                          ossaDIFEncryptionOffloadStartCB_t agCB);


GLOBAL bit32 saVhistCapture(
                          agsaRoot_t    *agRoot,
                          agsaContext_t *agContext,
                          bit32         queueNum,
                          bit32         Channel,
                          bit32         NumBitLo,
                          bit32         NumBitHi,
                          bit32         PcieAddrLo,
                          bit32         PcieAddrHi,
                          bit32         ByteCount );


GLOBAL void saCountActiveIORequests(  agsaRoot_t              *agRoot);

#ifdef SA_64BIT_TIMESTAMP
osGLOBAL bit64  osTimeStamp64(void);
#endif /* SA_64BIT_TIMESTAMP */

#ifdef SALL_API_TEST
/***************************************************************************
 * Definition of LL Test related API functions start                       *
 ***************************************************************************/
GLOBAL bit32 saGetLLCounters(
                      agsaRoot_t          *agRoot,
                      bit32               counters,
                      agsaLLCountInfo_t   *LLCountInfo
                      );

GLOBAL bit32 saResetLLCounters(
                      agsaRoot_t     *agRoot,
                      bit32          counters
                      );
#endif

#endif  /*__SSDKAPI_H__ */
