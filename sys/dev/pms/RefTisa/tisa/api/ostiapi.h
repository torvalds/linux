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
**
** Version Control Information:
**
**
*******************************************************************************/
/********************************************************************************
**
**   ostiapi.h
**
**   Abstract:   This module contains function prototype of the Transport
**               Independent (TIAPI) OS Callback interface.
**
********************************************************************************/

#ifndef OSTIAPI_H

#define OSTIAPI_H

/*
 * Definition for return status is defined in tiStatus_t in TIDEFS.H
 */

/*****************************************************************************
 *  Initiator/Target Shared Callbacks
 *****************************************************************************/

osGLOBAL bit32 ostiGetTransportParam(
                        tiRoot_t    *tiRoot,
                        char        *key,
                        char        *subkey1,
                        char        *subkey2,
                        char        *subkey3,
                        char        *subkey4,
                        char        *subkey5,
                        char        *valueName,
                        char        *buffer,
                        bit32       bufferLen,
                        bit32       *lenReceived
                        );

osGLOBAL void  ostiPortEvent(
                        tiRoot_t      *tiRoot,
                        tiPortEvent_t eventType,
                        bit32         status,
                        void          *pParm
                        );

osGLOBAL bit32  ostiTimeStamp( tiRoot_t  *tiRoot);
osGLOBAL bit64  ostiTimeStamp64( tiRoot_t  *tiRoot);

osGLOBAL FORCEINLINE bit32 ostiChipConfigReadBit32(
                        tiRoot_t      *tiRoot,
                        bit32         chipConfigOffset
                        );

osGLOBAL FORCEINLINE void ostiChipConfigWriteBit32(
                        tiRoot_t      *tiRoot,
                        bit32         chipConfigOffset,
                        bit32         chipConfigValue
                        );

osGLOBAL FORCEINLINE bit32 ostiChipReadBit32(
                        tiRoot_t      *tiRoot,
                        bit32         chipOffset
                        );

osGLOBAL FORCEINLINE void ostiChipWriteBit32(
                        tiRoot_t      *tiRoot,
                        bit32         chipOffset,
                        bit32         chipValue
                        );

osGLOBAL FORCEINLINE bit8 ostiChipReadBit8(
                        tiRoot_t      *tiRoot,
                        bit32         chipOffset
                        );

osGLOBAL FORCEINLINE void ostiChipWriteBit8(
                        tiRoot_t      *tiRoot,
                        bit32         chipOffset,
                        bit8          chipValue
                        );

osGLOBAL void ostiFlashReadBlock(
                        tiRoot_t      *tiRoot,
                        bit32         flashOffset,
                        void          *buffer,
                        bit32         bufferLen
                        );

osGLOBAL FORCEINLINE
tiDeviceHandle_t*
ostiGetDevHandleFromSasAddr(
  tiRoot_t    *root,
  unsigned char *sas_addr
);

osGLOBAL FORCEINLINE void ostidisableEncryption(tiRoot_t *root);

osGLOBAL FORCEINLINE void ostiSingleThreadedEnter(
                        tiRoot_t      *tiRoot,
                        bit32         queueId
                        );

osGLOBAL FORCEINLINE void ostiSingleThreadedLeave(
                        tiRoot_t      *tiRoot,
                        bit32         queueId
                        );


osGLOBAL bit32 ostiNumOfLUNIOCTLreq(tiRoot_t           *root,
									void               *param1,
                                    void               *param2,
                                    void                           **tiRequestBody,
                                    tiIORequest_t          **tiIORequest
                                    );

#ifdef PERF_COUNT
osGLOBAL void ostiEnter(tiRoot_t *ptiRoot, bit32 layer, int io);
osGLOBAL void ostiLeave(tiRoot_t *ptiRoot, bit32 layer, int io);
#define OSTI_INP_ENTER(root) ostiEnter(root, 2, 0)
#define OSTI_INP_LEAVE(root) ostiLeave(root, 2, 0)
#define OSTI_OUT_ENTER(root) ostiEnter(root, 2, 1)
#define OSTI_OUT_LEAVE(root) ostiLeave(root, 2, 1)
#else
#define OSTI_INP_ENTER(root)
#define OSTI_INP_LEAVE(root)
#define OSTI_OUT_ENTER(root)
#define OSTI_OUT_LEAVE(root)
#endif

osGLOBAL void  ostiStallThread(
                        tiRoot_t      *tiRoot,
                        bit32         microseconds
                        );

osGLOBAL FORCEINLINE bit8
ostiBitScanForward(
                  tiRoot_t   *root,
                  bit32      *Index,
                  bit32       Mask
                  );

#ifdef LINUX_VERSION_CODE

osGLOBAL sbit32
ostiAtomicIncrement(
                   tiRoot_t        *root,
                   sbit32 volatile *Addend
                   );

osGLOBAL sbit32
ostiAtomicDecrement(
                   tiRoot_t        *root,
                   sbit32 volatile *Addend
                   );


osGLOBAL sbit32
ostiAtomicBitClear(
                   tiRoot_t          *root,
                   sbit32 volatile   *Destination,
                   sbit32             Value
                   );

osGLOBAL sbit32
ostiAtomicBitSet(
                   tiRoot_t          *root,
                   sbit32 volatile   *Destination,
                   sbit32             Value
                   );

osGLOBAL sbit32
ostiAtomicExchange(
                   tiRoot_t         *root,
                   sbit32 volatile  *Target,
                   sbit32            Value
                   );

#else

osGLOBAL FORCEINLINE sbit32
ostiInterlockedIncrement(
                   tiRoot_t        *root,
                   sbit32 volatile *Addend
                   );

osGLOBAL FORCEINLINE sbit32
ostiInterlockedDecrement(
                   tiRoot_t         *root,
                   sbit32 volatile  *Addend
                   );


osGLOBAL FORCEINLINE sbit32
ostiInterlockedAnd(
                   tiRoot_t         *root,
                   sbit32 volatile  *Destination,
                   sbit32            Value
                   );

osGLOBAL FORCEINLINE sbit32
ostiInterlockedOr(
                   tiRoot_t         *root,
                   sbit32 volatile  *Destination,
                   sbit32            Value
                   );

osGLOBAL FORCEINLINE sbit32
ostiInterlockedExchange(
                   tiRoot_t        *root,
                   sbit32 volatile *Target,
                   sbit32           Value
                   );
#endif /*LINUX_VERSION_CODE*/

osGLOBAL bit32 ostiAllocMemory(
                        tiRoot_t    *tiRoot,
                        void        **osMemHandle,
                        void        ** virtPtr,
                        bit32       * physAddrUpper,
                        bit32       * physAddrLower,
                        bit32       alignment,
                        bit32       allocLength,
                        agBOOLEAN   isCacheable
                        );

osGLOBAL bit32 ostiFreeMemory(
                        tiRoot_t    *tiRoot,
                        void        *osDMAHandle,
                        bit32       allocLength
                        );

osGLOBAL FORCEINLINE void ostiCacheFlush(
                        tiRoot_t    *tiRoot,
                        void        *osMemHandle,
                        void        *virtPtr,
                        bit32       length
                        );

osGLOBAL FORCEINLINE void ostiCacheInvalidate(
                        tiRoot_t    *tiRoot,
                        void        *osMemHandle,
                        void        *virtPtr,
                        bit32       length
                        );

osGLOBAL FORCEINLINE void ostiCachePreFlush(
                        tiRoot_t    *tiRoot,
                        void        *osMemHandle,
                        void        *virtPtr,
                        bit32       length
                        );

/*
 *  The following two functions are for SAS/SATA
 */
osGLOBAL void
ostiInterruptEnable(
                        tiRoot_t  *ptiRoot,
                        bit32     channelNum
                        );

osGLOBAL void
ostiInterruptDisable(
                       tiRoot_t  *ptiRoot,
                       bit32     channelNum
                       );

osGLOBAL FORCEINLINE bit32
ostiChipReadBit32Ext(
                        tiRoot_t  *tiRoot,
                        bit32     busBaseNumber,
                        bit32     chipOffset
                        );

osGLOBAL FORCEINLINE void
ostiChipWriteBit32Ext(
                        tiRoot_t  *tiRoot,
                        bit32     busBaseNumber,
                        bit32     chipOffset,
                        bit32     chipValue
                        );


/*****************************************************************************
 *  Initiator specific Callbacks
 *****************************************************************************/

/*
 * Initiator specific IO Completion
 */
osGLOBAL void ostiInitiatorIOCompleted(
                        tiRoot_t            *tiRoot,
                        tiIORequest_t       *tiIORequest,
                        tiIOStatus_t        status,
                        bit32               statusDetail,
                        tiSenseData_t       *senseData,
                        bit32               context
                        );

osGLOBAL tiDeviceHandle_t*
ostiMapToDevHandle(tiRoot_t  *root,
                          bit8      pathId,
                          bit8      targetId,
                          bit8      LUN
                          );
osGLOBAL bit32 ostiSendResetDeviceIoctl(tiRoot_t *root,
			  void *pccb,
			  bit8 pathId,
  			  bit8 targetId,
			  bit8 lun,
			  unsigned long resetType
			);

osGLOBAL void
ostiGetSenseKeyCount(tiRoot_t  *root,
                            bit32      fIsClear,
                            void      *SenseKeyCount,
                            bit32      length
                            );

osGLOBAL void
ostiGetSCSIStatusCount(tiRoot_t  *root,
                            bit32      fIsClear,
                            void      *ScsiStatusCount,
                            bit32      length
                            );

osGLOBAL bit32
ostiSetDeviceQueueDepth(tiRoot_t       *tiRoot,
                                tiIORequest_t  *tiIORequest,
                                bit32           QueueDepth
                                );


#ifdef FAST_IO_TEST
typedef void (*ostiFastSSPCb_t)(tiRoot_t     *ptiRoot,
                                 void         *arg,
                                 tiIOStatus_t IOStatus,
                                 bit32         statusDetail);

void osti_FastIOCb(tiRoot_t     *ptiRoot,
                   void         *arg,
                   tiIOStatus_t IOStatus,
                   bit32        statusDetail);
#endif

osGLOBAL void
ostiInitiatorSMPCompleted(tiRoot_t    *tiRoot,
               tiIORequest_t  *tiSMPRequest,
               tiSMPStatus_t  smpStatus,
               bit32          tiSMPInfoLen,
               void           *tiFrameHandle,
               bit32          context);
/*
 * Initiator specific event
 */
osGLOBAL void ostiInitiatorEvent (
                        tiRoot_t            *tiRoot,
                        tiPortalContext_t   *portalContext,
                        tiDeviceHandle_t    *tiDeviceHandle,
                        tiIntrEventType_t   eventType,
                        bit32               eventStatus,
                        void                *parm
                        );


/*
 * PMC-Sierra IOCTL semaphoring
 */
osGLOBAL void ostiIOCTLClearSignal (
                        tiRoot_t    *tiRoot,
                        void        **agParam1,
                        void        **agParam2,
                        void        **agParam3
                        );

osGLOBAL void ostiIOCTLWaitForSignal (
                        tiRoot_t    *tigRoot,
                        void        *agParam1,
                        void        *agParam2,
                        void        *agParam3
                        );

osGLOBAL void ostiIOCTLSetSignal (
                        tiRoot_t    *tiRoot,
                        void        *agParam1,
                        void        *agParam2,
                        void        *agParam3
                        );

osGLOBAL void ostiIOCTLWaitForComplete (
                        tiRoot_t    *tigRoot,
                        void        *agParam1,
                        void        *agParam2,
                        void        *agParam3
                        );

osGLOBAL void ostiIOCTLComplete (
                        tiRoot_t    *tiRoot,
                        void        *agParam1,
                        void        *agParam2,
                        void        *agParam3
                        );

/*****************************************************************************
 *  Target specific Callbacks
 *****************************************************************************/

osGLOBAL void ostiProcessScsiReq(
                        tiRoot_t            *tiRoot,
                        tiTargetScsiCmnd_t  *tiTgtScsiCmnd,
                        void                *agFrameHandle,
                        bit32               immDataLength,
                        tiIORequest_t       *tiIORequest,
                        tiDeviceHandle_t    *tiDeviceHandle);

osGLOBAL void ostiNextDataPhase(
                        tiRoot_t          *tiRoot,
                        tiIORequest_t     *tiIORequest);

osGLOBAL void ostiTaskManagement (
                        tiRoot_t          *tiRoot,
                        bit32             task,
                        bit8              *scsiLun,
                        tiIORequest_t     *refTiIORequest,
                        tiIORequest_t     *tiTMRequest,
                        tiDeviceHandle_t  *tiDeviceHandle);

osGLOBAL void ostiTargetIOCompleted(
                        tiRoot_t          *tiRoot,
                        tiIORequest_t     *tiIORequest,
                        tiIOStatus_t      status
                        );

osGLOBAL bit32 ostiTargetEvent (
                        tiRoot_t          *tiRoot,
                        tiPortalContext_t *portalContext,
                        tiDeviceHandle_t  *tiDeviceHandle,
                        tiTgtEventType_t  eventType,
                        bit32             eventStatus,
                        void              *parm
                        );

osGLOBAL void ostiTargetIOError(
                        tiRoot_t          *tiRoot,
                        tiIORequest_t     *tiIORequest,
                        tiIOStatus_t      status,
                        bit32             statusDetail
                        );

osGLOBAL void ostiTargetTmCompleted(
                        tiRoot_t          *tiRoot,
                        tiIORequest_t     *tiTmRequest,
                        tiIOStatus_t      status,
                        bit32             statusDetail
                        );

osGLOBAL void ostiPCI_TRIGGER( tiRoot_t *tiRoot );


#endif  /* OSTIAPI_H */
