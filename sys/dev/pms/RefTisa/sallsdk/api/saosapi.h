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
/*! \file saosapi.h
 *  \brief The file defines the declaration of OS APIs
 *
 */
/*******************************************************************************/

#ifndef  __SSDKOSAPI_H__
#define __SSDKOSAPI_H__

#ifdef LINUX
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)

#ifdef FORCEINLINE
#undef FORCEINLINE
#define FORCEINLINE
#endif
      
#endif
#endif

/***************************************************************************
 * Definition of register access related functions start                   *
 ***************************************************************************/
#ifndef ossaHwRegRead
GLOBAL FORCEINLINE
bit32 ossaHwRegRead(
                          agsaRoot_t  *agRoot,
                          bit32       regOffset
                          );
#endif

#ifndef ossaHwRegWrite
GLOBAL FORCEINLINE
void ossaHwRegWrite(
                          agsaRoot_t  *agRoot,
                          bit32       regOffset,
                          bit32       regValue
                          );
#endif

#ifndef ossaHwRegReadExt
GLOBAL FORCEINLINE
bit32 ossaHwRegReadExt(
                             agsaRoot_t  *agRoot,
                             bit32       busBaseNumber,
                             bit32       regOffset
                             );
#endif

#ifndef ossaHwRegWriteExt
GLOBAL FORCEINLINE
void ossaHwRegWriteExt(
                             agsaRoot_t  *agRoot,
                             bit32       busBaseNumber,
                             bit32       regOffset,
                             bit32       regValue
                             );
#endif

#ifndef ossaHwRegReadConfig32
osGLOBAL bit32 ossaHwRegReadConfig32(
              agsaRoot_t  *agRoot,
              bit32       regOffset
              );
#endif


/***************************************************************************
 * Definition of register access related functions end                     *
 ***************************************************************************/

/***************************************************************************
 * Definition of thread related functions start                            *
 ***************************************************************************/
#ifndef ossaSingleThreadedEnter
GLOBAL FORCEINLINE void ossaSingleThreadedEnter(
                                    agsaRoot_t  *agRoot,
                                    bit32       syncLockId
                                   );
#endif

#ifndef ossaSingleThreadedLeave
GLOBAL FORCEINLINE void ossaSingleThreadedLeave(
                                    agsaRoot_t  *agRoot,
                                    bit32       syncLockId
                                   );
#endif

#ifndef ossaStallThread
GLOBAL void ossaStallThread(
                            agsaRoot_t  *agRoot,
                            bit32       microseconds
                            );
#endif
/***************************************************************************
 * Definition of thread related functions end                              *
 ***************************************************************************/

/***************************************************************************
 * Definition of interrupt related functions start                         *
 ***************************************************************************/
#ifndef ossaDisableInterrupts
#define ossaDisableInterrupts(agRoot, interruptVectorIndex) \
do                                                          \
{                                                           \
  agsaLLRoot_t  *saROOT = (agsaLLRoot_t *)(agRoot->sdkData);\
  saROOT->DisableInterrupts(agRoot, interruptVectorIndex); \
} while(0)
#endif

#ifndef ossaReenableInterrupts
#define ossaReenableInterrupts(agRoot, interruptVectorIndex) \
do                                                           \
{                                                            \
  agsaLLRoot_t  *saROOT = (agsaLLRoot_t *)(agRoot->sdkData); \
  saROOT->ReEnableInterrupts(agRoot, interruptVectorIndex); \
} while(0)
#endif

/***************************************************************************
 * Definition of interrupt related functions end                           *
 ***************************************************************************/

/***************************************************************************
 * Definition of cache related functions start                             *
 ***************************************************************************/
#ifndef ossaCacheInvalidate
GLOBAL FORCEINLINE void ossaCacheInvalidate(
                                agsaRoot_t  *agRoot,
                                void        *osMemHandle,
                                void        *virtPtr,
                                bit32       length
                                );
#endif

#ifndef ossaCacheFlush
GLOBAL FORCEINLINE void ossaCacheFlush(
                          agsaRoot_t  *agRoot,
                          void        *osMemHandle,
                          void        *virtPtr,
                          bit32       length
                          );
#endif

#ifndef ossaCachePreFlush
GLOBAL FORCEINLINE void ossaCachePreFlush(
                              agsaRoot_t  *agRoot,
                              void        *osMemHandle,
                              void        *virtPtr,
                              bit32       length
                              );
#endif

/***************************************************************************
 * Definition of cache related functions end                               *
 ***************************************************************************/

/***************************************************************************
 * Definition of hardware related functions start                          *
 ***************************************************************************/
#ifndef ossaHwCB
GLOBAL void ossaHwCB(
                    agsaRoot_t        *agRoot,
                    agsaPortContext_t *agPortContext,
                    bit32             event,
                    bit32             eventParm1,
                    void              *eventParm2,
                    void              *eventParm3
                    );
#endif

#ifndef ossaHwEventAckCB
GLOBAL void ossaHwEventAckCB(
                            agsaRoot_t    *agRoot,
                            agsaContext_t *agContext,
                            bit32         status
                            );
#endif
/***************************************************************************
 * Definition of hardware related functions end                            *
 ***************************************************************************/

/***************************************************************************
 * Definition of SATA related functions start                              *
 ***************************************************************************/
#ifndef ossaSATACompleted
GLOBAL void ossaSATACompleted(
                              agsaRoot_t        *agRoot,
                              agsaIORequest_t   *agIORequest,
                              bit32             agIOStatus,
                              void              *agFirstDword,
                              bit32             agIOInfoLen,
                              void              *agParam
                              );

#endif

#ifndef ossaSATAEvent
GLOBAL void ossaSATAEvent(
                        agsaRoot_t              *agRoot,
                        agsaIORequest_t         *agIORequest,
                        agsaPortContext_t       *agPortContext,
                        agsaDevHandle_t         *agDevHandle,
                        bit32                   event,
                        bit32                   agIOInfoLen,
                        void                    *agParam
                        );
#endif

#ifndef ossaSATAAbortCB
 GLOBAL void  ossaSATAAbortCB(
                        agsaRoot_t               *agRoot,
                        agsaIORequest_t          *agIORequest,
                        bit32                    flag,
                        bit32                    status
                        );
#endif

/***************************************************************************
 * Definition of SATA related functions end                                *
 ***************************************************************************/


/***************************************************************************
 * Definition of SAS related functions start                               *
 ***************************************************************************/
#ifndef ossaSSPEvent
GLOBAL void ossaSSPEvent(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  agsaPortContext_t *agPortContext,
                  agsaDevHandle_t   *agDevHandle,
                  bit32             event,
                  bit16             sspTag,
                  bit32             agIOInfoLen,
                  void              *agParam
                  );
#endif

osGLOBAL void 
ossaSMPIoctlCompleted( 
				 agsaRoot_t            *agRoot,
                 agsaIORequest_t       *agIORequest,
                 bit32                 agIOStatus,
                 bit32                 agIOInfoLen,
                 agsaFrameHandle_t     agFrameHandle
                 );

#ifndef ossaSMPCompleted
GLOBAL void ossaSMPCompleted(
                            agsaRoot_t            *agRoot,
                            agsaIORequest_t       *agIORequest,
                            bit32                 agIOStatus,
                            bit32                 agIOInfoLen,
                            agsaFrameHandle_t     agFrameHandle
                            );
#endif

#ifndef ossaSMPReqReceived
GLOBAL void ossaSMPReqReceived(
                              agsaRoot_t            *agRoot,
                              agsaDevHandle_t       *agDevHandle,
                              agsaFrameHandle_t     agFrameHandle,
                              bit32                 agFrameLength,
                              bit32                 phyId
                              );
#endif

#ifndef ossaSSPCompleted
GLOBAL FORCEINLINE void ossaSSPCompleted(
                            agsaRoot_t          *agRoot,
                            agsaIORequest_t     *agIORequest,
                            bit32               agIOStatus,
                            bit32               agIOInfoLen,
                            void                *agParam,
                            bit16               sspTag,
                            bit32               agOtherInfo
                            );
#endif

#ifdef FAST_IO_TEST
GLOBAL void ossaFastSSPCompleted(
                            agsaRoot_t          *agRoot,
                            agsaIORequest_t     *cbArg,
                            bit32               agIOStatus,
                            bit32               agIOInfoLen,
                            void                *agParam,
                            bit16               sspTag,
                            bit32               agOtherInfo
                            );
#endif

#ifndef ossaSSPReqReceived
GLOBAL void ossaSSPReqReceived(
                              agsaRoot_t        *agRoot,
                              agsaDevHandle_t   *agDevHandle,
                              agsaFrameHandle_t agFrameHandle,
                              bit16             agInitiatorTag,
                              bit32             parameter,
                              bit32             agFrameLen
                              );
#endif

osGLOBAL void
ossaSSPIoctlCompleted(
                agsaRoot_t                        *agRoot,
                agsaIORequest_t           *agIORequest,
                bit32                             agIOStatus,
                bit32                             agIOInfoLen,
                void                              *agParam,
                bit16                             sspTag,
                bit32                             agOtherInfo
                );


#ifndef ossaSSPAbortCB
GLOBAL void ossaSSPAbortCB(
                        agsaRoot_t              *agRoot,
                        agsaIORequest_t         *agIORequest,
                        bit32                   flag,
                        bit32                   status
                        );
#endif

#ifndef ossaSMPAbortCB
GLOBAL void ossaSMPAbortCB(
                        agsaRoot_t              *agRoot,
                        agsaIORequest_t         *agIORequest,
                        bit32                   flag,
                        bit32                   status
                        );
#endif

#ifndef ossaReconfigSASParamsCB
GLOBAL void ossaReconfigSASParamsCB(
                        agsaRoot_t        *agRoot,
                        agsaContext_t     *agContext,
                        bit32             status,
                        agsaSASReconfig_t *agSASConfig
);
#endif

/***************************************************************************
 * Definition of SAS related functions end                                 *
 ***************************************************************************/

/***************************************************************************
 * Definition of Discovery related functions start                         *
 ***************************************************************************/
#ifndef ossaDiscoverSataCB
GLOBAL void ossaDiscoverSataCB(
                              agsaRoot_t          *agRoot,
                              agsaPortContext_t   *agPortContext,
                              bit32               event,
                              void                *pParm1,
                              void                *pParm2
                              );
#endif

#ifndef ossaDiscoverSasCB
GLOBAL void ossaDiscoverSasCB(
                              agsaRoot_t          *agRoot,
                              agsaPortContext_t   *agPortContext,
                              bit32               event,
                              void                *pParm1,
                              void                *pParm2
                              );
#endif

#ifndef ossaDeviceHandleAccept
GLOBAL bit32 ossaDeviceHandleAccept(
                                    agsaRoot_t          *agRoot,
                                    agsaDevHandle_t     *agDevHandle,
                                    agsaSASDeviceInfo_t *agDeviceInfo,
                                    agsaPortContext_t   *agPortContext,
                                    bit32               *hostAssignedDeviceId
                                    );
#endif

#ifndef ossaGetDeviceHandlesCB
GLOBAL void ossaGetDeviceHandlesCB(
                                agsaRoot_t        *agRoot,
                                agsaContext_t     *agContext,
                                agsaPortContext_t *agPortContext,
                                agsaDevHandle_t   *agDev[],
                                bit32             validDevs
                                );
#endif

#ifndef ossaGetDeviceInfoCB
GLOBAL void ossaGetDeviceInfoCB(
                                agsaRoot_t        *agRoot,
                                agsaContext_t     *agContext,
                                agsaDevHandle_t   *agDevHandle,
                                bit32             status,
                                void              *agInfo
                                );
#endif

#ifndef ossaDeviceHandleRemovedEvent
GLOBAL void ossaDeviceHandleRemovedEvent (
                                agsaRoot_t        *agRoot,
                                agsaDevHandle_t   *agDevHandle,
                                agsaPortContext_t *agPortContext
                                );
#endif

#ifndef ossaGetDeviceStateCB
GLOBAL void ossaGetDeviceStateCB(
                                agsaRoot_t        *agRoot,
                                agsaContext_t     *agContext,
                                agsaDevHandle_t   *agDevHandle,
                                bit32             status,
                                bit32             deviceState
                                );
#endif

#ifndef ossaSetDeviceInfoCB
GLOBAL void ossaSetDeviceInfoCB(
                                agsaRoot_t        *agRoot,
                                agsaContext_t     *agContext,
                                agsaDevHandle_t   *agDevHandle,
                                bit32             status,
                                bit32             option,
                                bit32             param
                                );
#endif

#ifndef ossaSetDeviceStateCB
GLOBAL void ossaSetDeviceStateCB(
                                agsaRoot_t        *agRoot,
                                agsaContext_t     *agContext,
                                agsaDevHandle_t   *agDevHandle,
                                bit32             status,
                                bit32             newDeviceState,
                                bit32             previousDeviceState
                                );
#endif

/***************************************************************************
 * Definition of Discovery related functions end                           *
 ***************************************************************************/

/***************************************************************************
 * Definition of Misc. related functions start                             *
 ***************************************************************************/

#ifndef ossaTimeStamp
GLOBAL bit32 ossaTimeStamp(agsaRoot_t     *agRoot); 
#endif /* ossaTimeStamp */

#ifndef ossaTimeStamp64
GLOBAL bit64 ossaTimeStamp64(agsaRoot_t     *agRoot); 
#endif /* ossaTimeStamp64 */


#ifndef ossaLocalPhyControlCB
GLOBAL void ossaLocalPhyControlCB(
                      agsaRoot_t     *agRoot,
                      agsaContext_t  *agContext,
                      bit32          phyId,
                      bit32          phyOperation,
                      bit32          status,
                      void           *parm);
#endif

#ifndef ossaGetPhyProfileCB
GLOBAL void   ossaGetPhyProfileCB(
                      agsaRoot_t    *agRoot,
                      agsaContext_t *agContext,
                      bit32         status,
                      bit32         ppc,
                      bit32         phyID,
                      void          *parm );
#endif

#ifndef ossaSetPhyProfileCB
GLOBAL void ossaSetPhyProfileCB(
                     agsaRoot_t    *agRoot,
                     agsaContext_t *agContext,
                     bit32         status,
                     bit32         ppc,
                     bit32         phyID,
                     void          *parm );
#endif

#ifndef ossaFwFlashUpdateCB
GLOBAL void ossaFwFlashUpdateCB(
                      agsaRoot_t    *agRoot,
                      agsaContext_t *agContext,
                      bit32         status);
#endif

#ifndef ossaFlashExtExecuteCB
GLOBAL void   ossaFlashExtExecuteCB(
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                    status,
                      bit32                    command,
                      agsaFlashExtResponse_t  *agFlashExtRsp);

#endif

#ifdef SPC_ENABLE_PROFILE
GLOBAL void ossaFwProfileCB(
                      agsaRoot_t    *agRoot,
                      agsaContext_t *agContext,
                      bit32         status,
                      bit32         len
                      );

#endif
#ifndef ossaEchoCB
GLOBAL void ossaEchoCB(
                      agsaRoot_t    *agRoot,
                      agsaContext_t *agContext,
                      void          *echoPayload);
#endif

#ifndef ossaGpioResponseCB
GLOBAL void ossaGpioResponseCB(
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                    status,
                      bit32                    gpioReadValue,
                      agsaGpioPinSetupInfo_t   *gpioPinSetupInfo,
                      agsaGpioEventSetupInfo_t *gpioEventSetupInfo);
#endif

#ifndef ossaGpioEvent
GLOBAL void ossaGpioEvent(
                      agsaRoot_t    *agRoot,
                      bit32         gpioEvent);
#endif

#ifndef ossaSASDiagExecuteCB
GLOBAL void ossaSASDiagExecuteCB(
                      agsaRoot_t      *agRoot,
                      agsaContext_t   *agContext,
                      bit32           status,
                      bit32           command,
                      bit32           reportData);
#endif

#ifndef ossaSASDiagStartEndCB
GLOBAL void ossaSASDiagStartEndCB(
                      agsaRoot_t      *agRoot,
                      agsaContext_t   *agContext,
                      bit32           status);
#endif

#ifndef ossaGetTimeStampCB
GLOBAL void ossaGetTimeStampCB(
                      agsaRoot_t      *agRoot,
                      agsaContext_t   *agContext,
                      bit32           timeStampLower,
                      bit32           timeStampUpper);
#endif

#ifndef ossaPortControlCB
GLOBAL void ossaPortControlCB(
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      agsaPortContext_t *agPortContext,
                      bit32             portOperation,
                      bit32             status);
#endif

#ifndef ossaGeneralEvent
GLOBAL void ossaGeneralEvent(
                      agsaRoot_t        *agRoot,
                      bit32             status,
                      agsaContext_t     *agContext,
                      bit32             *msg);
#endif

#ifndef ossaGetRegisterDumpCB
void ossaGetRegisterDumpCB(
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      bit32             status);
#endif

GLOBAL void ossaGetForensicDataCB (
        agsaRoot_t          *agRoot,
        agsaContext_t       *agContext,
        bit32                status,
        agsaForensicData_t  *forensicData
        );


#ifndef ossaGetNVMDResponseCB
GLOBAL void ossaGetNVMDResponseCB(
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      bit32             status,
                      bit8              indirectPayload,
                      bit32             agInfoLen,
                      agsaFrameHandle_t agFrameHandle );
#endif

#ifndef ossaSetNVMDResponseCB
GLOBAL void ossaSetNVMDResponseCB(
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      bit32             status );
#endif

#ifndef ossaQueueProcessed
#ifdef SALLSDK_TEST_SET_OB_QUEUE
GLOBAL void ossaQueueProcessed(agsaRoot_t *agRoot,
                                          bit32 queue,
                                          bit32 obpi,
                                          bit32 obci);
#else
#define ossaQueueProcessed(agRoot, queue, obpi, obci)
#endif
#endif

#ifndef ossaSGpioCB
GLOBAL void ossaSGpioCB(
                    agsaRoot_t              *agRoot,
                    agsaContext_t           *agContext, 
                    agsaSGpioReqResponse_t  *pSgpioResponse
                    );
#endif

#ifndef ossaPCIeDiagExecuteCB
GLOBAL void ossaPCIeDiagExecuteCB(
            agsaRoot_t             *agRoot,
            agsaContext_t         *agContext,
            bit32                  status,
            bit32                  command,
            agsaPCIeDiagResponse_t *resp );
#endif

#ifndef ossaGetDFEDataCB
GLOBAL void ossaGetDFEDataCB(
                             agsaRoot_t     *agRoot,
                             agsaContext_t  *agContext,
                             bit32           status,
                             bit32           agInfoLen
                             );
#endif

#ifndef ossaVhistCaptureCB
GLOBAL void ossaVhistCaptureCB(
                            agsaRoot_t    *agRoot,
                            agsaContext_t *agContext,
                            bit32         status,
                            bit32         len);
#endif

#ifndef ossaGetIOErrorStatsCB
GLOBAL void ossaGetIOErrorStatsCB (
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                     status,
                      agsaIOErrorEventStats_t  *stats
                      );
#endif

#ifndef ossaGetIOEventStatsCB
GLOBAL void ossaGetIOEventStatsCB (
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                     status,
                      agsaIOErrorEventStats_t  *stats
                      );
#endif

#ifndef ossaOperatorManagementCB
GLOBAL void ossaOperatorManagementCB(
                  agsaRoot_t    *agRoot,
                  agsaContext_t *agContext,
                  bit32          status,
                  bit32          eq
                  );
#endif


#ifndef ossaEncryptSelftestExecuteCB
GLOBAL void ossaEncryptSelftestExecuteCB (
                        agsaRoot_t    *agRoot,
                        agsaContext_t *agContext,
                        bit32          status,
                        bit32          type,
                        bit32          length,
                        void          *TestResult
                        );

#endif

#ifndef ossaGetOperatorCB
GLOBAL void ossaGetOperatorCB(
               agsaRoot_t    *agRoot,
               agsaContext_t *agContext,
               bit32          status,
               bit32          option,
               bit32          num,
               bit32          role,
               agsaID_t      *id
               );

#endif

#ifndef ossaSetOperatorCB
GLOBAL void ossaSetOperatorCB(
              agsaRoot_t    *agRoot,
              agsaContext_t *agContext,
              bit32          status,
              bit32          eq
              );

#endif

#ifndef ossaDIFEncryptionOffloadStartCB
GLOBAL void ossaDIFEncryptionOffloadStartCB(
              agsaRoot_t    *agRoot,
              agsaContext_t *agContext,
              bit32          status,
              agsaOffloadDifDetails_t *agsaOffloadDifDetails
              );
#endif

/***************************************************************************
 * Definition of Misc related functions end                                *
 ***************************************************************************/

/***************************************************************************
 * Definition of Debug related functions start                             *
 ***************************************************************************/
#ifndef ossaLogTrace0
GLOBAL void ossaLogTrace0(
                          agsaRoot_t  *agRoot,
                          bit32       traceCode
                          );
#endif

#ifndef ossaLogTrace1
GLOBAL void ossaLogTrace1(
                          agsaRoot_t  *agRoot,
                          bit32       traceCode,
                          bit32       value1
                          );
#endif

#ifndef ossaLogTrace2
GLOBAL void ossaLogTrace2(
                          agsaRoot_t  *agRoot,
                          bit32       traceCode,
                          bit32       value1,
                          bit32       value2
                          );
#endif

#ifndef ossaLogTrace3
GLOBAL void ossaLogTrace3(
                          agsaRoot_t  *agRoot,
                          bit32       traceCode,
                          bit32       value1,
                          bit32       value2,
                          bit32       value3
                          );
#endif

#ifndef ossaLogTrace4
GLOBAL void ossaLogTrace4(
                          agsaRoot_t  *agRoot,
                          bit32       traceCode,
                          bit32       value1,
                          bit32       value2,
                          bit32       value3,
                          bit32       value4
                          );
#endif

#ifndef ossaLogDebugString
GLOBAL void ossaLogDebugString(
                         agsaRoot_t   *agRoot,
                         bit32        level,
                         char         *string,
                         void         *ptr1,
                         void         *ptr2,
                         bit32        value1,
                         bit32        value2
                         );
#endif

#ifdef SALLSDK_OS_IOMB_LOG_ENABLE
GLOBAL void ossaLogIomb(agsaRoot_t  *agRoot,
                        bit32        queueNum,
                        agBOOLEAN      isInbound,
                        void        *pMsg,
                        bit32        msgLength);
#else
#define ossaLogIomb(a, b,c,d,e )
#endif

osGLOBAL void ossaPCI_TRIGGER(agsaRoot_t  *agRoot );

#ifdef PERF_COUNT
osGLOBAL void ossaEnter(agsaRoot_t *agRoot, int io);
osGLOBAL void ossaLeave(agsaRoot_t *agRoot, int io);
#define OSSA_INP_ENTER(root) ossaEnter(root, 0)
#define OSSA_INP_LEAVE(root) ossaLeave(root, 0)
#define OSSA_OUT_ENTER(root) ossaEnter(root, 1)
#define OSSA_OUT_LEAVE(root) ossaLeave(root, 1)
#else
#define OSSA_INP_ENTER(root)
#define OSSA_INP_LEAVE(root)
#define OSSA_OUT_ENTER(root)
#define OSSA_OUT_LEAVE(root)
#endif
/***************************************************************************
 * Definition of Debug related functions end                               *
 ***************************************************************************/

#endif  /*__SSDKOSAPI_H__ */
