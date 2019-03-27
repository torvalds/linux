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
/********************************************************************************
*   tiapi.h
*
*   Abstract:   This module contains function prototype of the Transport
*               Independent API (TIAPI) Layer for both initiator and target.
** Version Control Information:
**
**
*******************************************************************************/


#ifndef TIAPI_H
#define TIAPI_H

#include <dev/pms/RefTisa/tisa/api/tiglobal.h>
#include <dev/pms/RefTisa/tisa/api/titypes.h>

/*****************************************************************************
 *  INITIATOR/TARGET SHARED APIs
 *****************************************************************************/

osGLOBAL void tiCOMGetResource (
                        tiRoot_t                *tiRoot,
                        tiLoLevelResource_t     *loResource,
                        tiInitiatorResource_t   *initiatorResource,
                        tiTargetResource_t      *targetResource,
                        tiTdSharedMem_t         *tdSharedMem
                        );

osGLOBAL bit32 tiCOMInit(
                        tiRoot_t                *tiRoot,
                        tiLoLevelResource_t     *loResource,
                        tiInitiatorResource_t   *initiatorResource,
                        tiTargetResource_t      *targetResource,
                        tiTdSharedMem_t         *tdSharedMem
                        );

osGLOBAL bit32 tiCOMPortInit(
                        tiRoot_t   *tiRoot,
                        bit32       sysIntsActive
                        );

osGLOBAL bit32 tiCOMPortStart(
                        tiRoot_t          *tiRoot,
                        bit32             portID,
                        tiPortalContext_t *portalContext,
                        bit32             option
                        );

osGLOBAL void tiCOMShutDown( tiRoot_t    *tiRoot);

osGLOBAL bit32 tiCOMPortStop(
                        tiRoot_t          *tiRoot,
                        tiPortalContext_t *portalContext
                        );

osGLOBAL void tiCOMReset (
                        tiRoot_t    *tiRoot,
                        bit32       option
                        );

osGLOBAL bit32 
tdsaGetNumOfLUNIOCTL(
               tiRoot_t            *tiRoot,
               tiIOCTLPayload_t    *agIOCTLPayload,
               void                *agParam1,
               void                *agParam2,
               void                *agParam3
               );

osGLOBAL void ostiNumOfLUNIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        );
osGLOBAL bit32
tiNumOfLunIOCTLreq(
             tiRoot_t                       *tiRoot, 
             tiIORequest_t                  *tiIORequest,
             tiDeviceHandle_t               *tiDeviceHandle,
             void                           *tiRequestBody,
             tiIOCTLPayload_t               *agIOCTLPayload,
             void                           *agParam1,
             void                           *agParam2
             );


osGLOBAL FORCEINLINE bit32 tiCOMInterruptHandler(
                        tiRoot_t    *tiRoot,
                        bit32       channelNum
                        );

osGLOBAL FORCEINLINE bit32 tiCOMDelayedInterruptHandler (
                        tiRoot_t    *tiRoot,
                        bit32       channelNum,
                        bit32       count,
                        bit32       context
                        );

osGLOBAL bit32  tiCOMLegacyInterruptHandler(
                        tiRoot_t    *tiRoot,
                        bit32       channelNum
                        );


osGLOBAL void tiCOMLegacyDelayedInterruptHandler(
                        tiRoot_t    *tiRoot,
                        bit32       channelNum,
                        bit32       count,
                        bit32       context
                        );
osGLOBAL void tiCOMTimerTick( tiRoot_t  *tiRoot );

osGLOBAL void tiCOMSystemInterruptsActive (
                        tiRoot_t    *tiRoot ,
                        bit32       sysIntsActive
                        );


osGLOBAL FORCEINLINE void
tiCOMInterruptEnable(
                      tiRoot_t * tiRoot,
                      bit32      channelNum);

osGLOBAL void tiCOMFrameReadBlock(
                        tiRoot_t          *tiRoot,
                        void              *agFrame,
                        bit32             FrameOffset,
                        void              *FrameBuffer,
                        bit32             FrameBufLen );
osGLOBAL bit32 tiCOMEncryptGetInfo(
                        tiRoot_t            *tiRoot);

osGLOBAL bit32 tiCOMEncryptSetMode(
                        tiRoot_t            *tiRoot,
                        bit32               securityCipherMode
                        );

osGLOBAL bit32  tiCOMSetControllerConfig (
                    tiRoot_t   *tiRoot,
                    bit32       modePage,
                    bit32       length,
                    void        *buffer,
                    void        *context
                    );

osGLOBAL bit32 tiCOMGetControllerConfig(
                    tiRoot_t    *tiRoot,
                    bit32       modePage,
                    bit32       flag,
                    void        *context
                    );


osGLOBAL bit32 tiCOMEncryptDekAdd(
                        tiRoot_t            *tiRoot,
                        bit32               kekIndex,
                        bit32               dekTableSelect,
                        bit32               dekAddrHi,
                        bit32               dekAddrLo,
                        bit32               dekIndex,
                        bit32               dekNumberOfEntries,
                        bit32               dekBlobFormat,
                        bit32               dekTableKeyEntrySize
                        );

osGLOBAL bit32 tiCOMEncryptDekInvalidate(
                        tiRoot_t            *tiRoot,
                        bit32               dekTable,
                        bit32               dekIndex
                        );


osGLOBAL bit32 tiCOMEncryptKekAdd(
                        tiRoot_t            *tiRoot,
                        bit32               kekIndex,
                        bit32               wrapperKekIndex,
                        bit32               blobFormat,
                        tiEncryptKekBlob_t  *encryptKekBlob
                        );

osGLOBAL tiDeviceHandle_t *
tiINIGetExpDeviceHandleBySasAddress(
                      tiRoot_t          * tiRoot,
                      tiPortalContext_t * tiPortalContext,
					  bit32 sas_addr_hi,
					  bit32 sas_addr_lo,
					  bit32               maxDevs
                      );


#ifdef HIALEAH_ENCRYPTION 
osGLOBAL bit32 tiCOMEncryptHilSet(tiRoot_t  *tiRoot );
#endif /* HIALEAH_ENCRYPTION */

osGLOBAL bit32 tiCOMEncryptKekStore(
                        tiRoot_t            *tiRoot,
                        bit32               kekIndex
                        );

osGLOBAL bit32 tiCOMEncryptKekLoad(
                        tiRoot_t            *tiRoot,
                        bit32               kekIndex
                        );

osGLOBAL bit32 tiCOMEncryptSelfTest(
                        tiRoot_t  *tiRoot,
                        bit32     type,
                        bit32     length,
                        void      *TestDescriptor
                        );

osGLOBAL bit32 tiCOMSetOperator(
                        tiRoot_t      *tiRoot,
                        bit32          flag,
                        void           *cert
                        );

osGLOBAL bit32 tiCOMGetOperator(
                           tiRoot_t   *tiRoot,
                           bit32       option,
                           bit32       AddrHi,
                           bit32       AddrLo
                           );

osGLOBAL bit32 tiCOMOperatorManagement(
                        tiRoot_t            *tiRoot,
                        bit32                flag,
                        bit8                 role,
                        tiID_t              *idString,
                        tiEncryptKekBlob_t  *kekBlob
                        );

/*
 * PMC-Sierra Management IOCTL module
 */
osGLOBAL bit32 tiCOMMgntIOCTL(
                        tiRoot_t            *tiRoot,
                        tiIOCTLPayload_t    *agIOCTLPayload,
                        void                *agParam1,
                        void                *agParam2,
                        void                *agParam3
                        );

osGLOBAL void ostiCOMMgntIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        );

osGLOBAL void ostiRegDumpIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        );

osGLOBAL void ostiSetNVMDIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        );

osGLOBAL void ostiGetPhyProfileIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        );

osGLOBAL void ostiGetNVMDIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        );
osGLOBAL bit32 tiCOMGetPortInfo(
                        tiRoot_t            *tiRoot,
                        tiPortalContext_t   *portalContext,
                        tiPortInfo_t        *tiPortInfo
                        );

osGLOBAL void ostiSendSMPIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        );

osGLOBAL void ostiGenEventIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status
                        );

osGLOBAL void
ostiGetDeviceInfoIOCTLRsp(
                        tiRoot_t    *tiRoot,
                        bit32        status,
                        void        *param
                        );

osGLOBAL void
ostiGetIoErrorStatsIOCTLRsp(
                        tiRoot_t    *tiRoot,
                        bit32        status,
                        void        *param
                        );

osGLOBAL void
ostiGetIoEventStatsIOCTLRsp(
                        tiRoot_t    *tiRoot,
                        bit32        status,
                        void        *param
                        );

osGLOBAL void
ostiGetForensicDataIOCTLRsp(
                        tiRoot_t    *tiRoot,
                        bit32        status,
                        void        *param
                        );


#ifdef SPC_ENABLE_PROFILE
osGLOBAL void ostiFWProfileIOCTLRsp(
                        tiRoot_t            *tiRoot,
                        bit32               status,
    bit32               len
                        );
#endif

/*****************************************************************************
 *  INITIATOR SPECIFIC APIs
 *****************************************************************************/

/*
 * Session management module.
 */
osGLOBAL bit32 tiINIGetExpander(
                  tiRoot_t          * tiRoot,
                  tiPortalContext_t * tiPortalContext,
                  tiDeviceHandle_t  * tiDev,
                  tiDeviceHandle_t  ** tiExp
                 );
osGLOBAL bit32 tiINIGetDeviceHandles(
                        tiRoot_t            *tiRoot,
                        tiPortalContext_t   *portalContext,
                        tiDeviceHandle_t    *agDev[],
                        bit32               maxDevs
                        );

osGLOBAL bit32 tiINIGetDeviceHandlesForWinIOCTL(
                        tiRoot_t            *tiRoot,
                        tiPortalContext_t   *portalContext,
                        tiDeviceHandle_t    *agDev[],
                        bit32               maxDevs
                        );

osGLOBAL void tiIniGetDirectSataSasAddr(tiRoot_t * tiRoot, bit32 phyId, bit8 **sasAddressHi, bit8 **sasAddressLo);
osGLOBAL bit32 tiINIDiscoverTargets(
                        tiRoot_t            *tiRoot,
                        tiPortalContext_t   *portalContext,
                        bit32               option
                        );

osGLOBAL bit32 tiINILogin(
                        tiRoot_t            *tiRoot,
                        tiDeviceHandle_t    *tiDeviceHandle
                        );

osGLOBAL bit32 tiINILogout(
                        tiRoot_t            *tiRoot,
                        tiDeviceHandle_t    *tiDeviceHandle
                        );

osGLOBAL bit32 tiINIGetDeviceInfo(
                        tiRoot_t            *tiRoot,
                        tiDeviceHandle_t    *tiDeviceHandle,
                        tiDeviceInfo_t      *tiDeviceInfo);

/*
 * Transport recovery module.
 */
osGLOBAL void tiINITransportRecovery(
                        tiRoot_t            *tiRoot,
                        tiDeviceHandle_t    *tiDeviceHandle
                        );

osGLOBAL bit32 tiINITaskManagement (
                        tiRoot_t          *tiRoot,
                        tiDeviceHandle_t  *tiDeviceHandle,
                        bit32             task,
                        tiLUN_t           *lun,
                        tiIORequest_t     *taskTag,
                        tiIORequest_t     *currentTaskTag
                        );
osGLOBAL bit32 tiINISMPStart(
            tiRoot_t                  *tiRoot,
            tiIORequest_t             *tiIORequest,
            tiDeviceHandle_t          *tiDeviceHandle,
            tiSMPFrame_t              *tiScsiRequest,
            void                      *tiSMPBody,
            bit32                     interruptContext
            );
/*
 * I/O module.
 */
osGLOBAL bit32 tiINIIOStart(
                        tiRoot_t                    *tiRoot,
                        tiIORequest_t               *tiIORequest,
                        tiDeviceHandle_t            *tiDeviceHandle,
                        tiScsiInitiatorRequest_t   *tiScsiRequest,
                        void                        *tiRequestBody,
                        bit32                       interruptContext
                        );

osGLOBAL void tiINIDebugDumpIO(
                        tiRoot_t                  *tiRoot,
                        tiIORequest_t             *tiIORequest
                        );

osGLOBAL bit32 tiINIIOStartDif(
                        tiRoot_t                    *tiRoot,
                        tiIORequest_t               *tiIORequest,
                        tiDeviceHandle_t            *tiDeviceHandle,
                        tiScsiInitiatorRequest_t   *tiScsiRequest,
                        void                        *tiRequestBody,
                        bit32                       interruptContext,
                        tiDif_t                     *difOption
                        );
osGLOBAL bit32 tiINISuperIOStart (
        tiRoot_t                      *tiRoot,
        tiIORequest_t                 *tiIORequest,
        tiDeviceHandle_t              *tiDeviceHandle,
        tiSuperScsiInitiatorRequest_t *tiScsiRequest,
        void                          *tiRequestBody,
        bit32                         interruptContext
        );

#ifdef FAST_IO_TEST
osGLOBAL void *tiINIFastIOPrepare(
             tiRoot_t                 *tiRoot,
             void                     *ioHandle,
             agsaFastCommand_t        *fc);

osGLOBAL void*
tiINIFastIOPrepare2(
            tiRoot_t          *tiRoot,
            void              *ioHandle,
            agsaFastCommand_t *fc,
            void                 *pMessage,
            void  *pRequest);

osGLOBAL bit32 tiINIFastIOSend(void *ioHandle);
osGLOBAL bit32 tiINIFastIOCancel(void *ioHandle);
#endif

osGLOBAL bit32 tiCOMEncryptGetMode(tiRoot_t            *tiRoot);
osGLOBAL bit32 tiCOMEncryptSetOn_Off(tiRoot_t          *tiRoot, bit32 On);

osGLOBAL bit32 tiInitDevEncrypt(
        tiRoot_t                      *tiRoot,
        void                          *tideviceptr );

osGLOBAL bit32 tiTGTSuperIOStart (
                              tiRoot_t         *tiRoot,
                              tiIORequest_t    *tiIORequest,
                              tiSuperScsiTargetRequest_t *tiScsiRequest
                              );

osGLOBAL void tiINITimerTick(
                        tiRoot_t            *tiRoot
                        );


osGLOBAL bit32 tiINIIOAbort(
                        tiRoot_t            *tiRoot,
                        tiIORequest_t       *taskTag
                        );

osGLOBAL bit32 tiINIIOAbortAll(
                        tiRoot_t            *tiRoot,
                        tiDeviceHandle_t    *tiDeviceHandle
                        );
/*
 * Event Logging module
 */
osGLOBAL bit32 tiINIReportErrorToEventLog(
                        tiRoot_t            *tiRoot,
                        tiEVTData_t         *agEventData
                        );


/*****************************************************************************
 *  TARGET SPECIFIC APIs
 *****************************************************************************/

osGLOBAL void tiTGTTimerTick(
                        tiRoot_t  *tiRoot
                        );

osGLOBAL void *tiTGTSenseBufferGet(
                        tiRoot_t        *tiRoot,
                        tiIORequest_t   *tiIORequest,
                        bit32           length
                        );

osGLOBAL void tiTGTSetResp(
                        tiRoot_t        *tiRoot,
                        tiIORequest_t   *tiIORequest,
                        bit32           dataSentLength,
                        bit8            ScsiStatus,
                        bit32           senseLength
                        );

osGLOBAL bit32 tiTGTIOStart (
                        tiRoot_t        *tiRoot,
                        tiIORequest_t   *tiIORequest,
                        bit32           dataOffset,
                        bit32           dataLength,
                        tiSgl_t         *dataSGL,
                        void            *sglVirtualAddr
                        );

osGLOBAL bit32 tiTGTIOStartMirror (
                        tiRoot_t        *tiRoot,
                        tiIORequest_t   *tiIORequest,
                        bit32           dataOffset,
                        bit32           dataLength,
                        tiSgl_t         *dataSGL,
                        void            *sglVirtualAddr,
                        tiSgl_t         *dataSGLMirror,
                        void            *sglVirtualAddrMirror
                        );

osGLOBAL bit32 tiTGTIOStartDif (
                        tiRoot_t        *tiRoot,
                        tiIORequest_t   *tiIORequest,
                        bit32           dataOffset,
                        bit32           dataLength,
                        tiSgl_t         *dataSGL,
                        void            *sglVirtualAddr,
                        tiDif_t         *difOption
                        );


osGLOBAL bit32 tiTGTGetDeviceHandles(
                        tiRoot_t          *tiRoot,
                        tiPortalContext_t *portalContext,
                        tiDeviceHandle_t  *agDev[],
                        bit32             maxDevs
                        );

osGLOBAL bit32 tiTGTGetDeviceInfo(
                        tiRoot_t            *tiRoot,
                        tiDeviceHandle_t    *tiDeviceHandle,
                        tiDeviceInfo_t      *tiDeviceInfo);

osGLOBAL bit32 tiTGTIOAbort(
                        tiRoot_t            *tiRoot,
                        tiIORequest_t       *taskTag
                        );

osGLOBAL bit32 tiTGTSendTmResp (
                        tiRoot_t          *tiRoot,
                        tiIORequest_t     *tiTMRequest,
                        bit32             status
                        );

void tiPCI_TRIGGER( tiRoot_t        *tiRoot);

void tiComCountActiveIORequests( tiRoot_t        *tiRoot);

#endif  /* TIAPI_H */
