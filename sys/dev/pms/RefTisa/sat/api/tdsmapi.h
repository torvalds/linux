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
*   tmsmapi.h 
*
*   Abstract:   This module contains function prototype of the SAT
*               Module (SM) API callback for initiator.
*******************************************************************************/

#ifndef TDSMAPI_H
#define TDSMAPI_H

osGLOBAL void	
tdsmIDCompletedCB(
                  smRoot_t   *smRoot,
                  smIORequest_t   *smIORequest,
                  smDeviceHandle_t   *smDeviceHandle,
                  bit32    status,
                  void    *IDdata
                 );

osGLOBAL FORCEINLINE void 
tdsmIOCompletedCB(
                  smRoot_t   *smRoot,
                  smIORequest_t   *smIORequest,
                  bit32    status,
                  bit32    statusDetail,
                  smSenseData_t   *senseData,
                  bit32    interruptContext
                  );
osGLOBAL void 
tdsmEventCB(
                smRoot_t          *smRoot,
                smDeviceHandle_t  *smDeviceHandle,
                smIntrEventType_t  eventType,
                bit32              eventStatus,
                void              *parm
                );

osGLOBAL FORCEINLINE void 
tdsmSingleThreadedEnter(
                        smRoot_t   *smRoot,
                        bit32       syncLockId
                       );

osGLOBAL FORCEINLINE void 
tdsmSingleThreadedLeave(
                        smRoot_t   *smRoot,
                        bit32       syncLockId
                        );

osGLOBAL FORCEINLINE bit8 
tdsmBitScanForward(
                  smRoot_t   *smRoot,
                  bit32      *Index,
                  bit32       Mask
                  );

#ifdef LINUX_VERSION_CODE

osGLOBAL FORCEINLINE sbit32 
tdsmInterlockedIncrement(
                   smRoot_t        *smRoot,
                   sbit32 volatile *Addend
                   );

osGLOBAL FORCEINLINE sbit32 
tdsmInterlockedDecrement(
                   smRoot_t         *smRoot,
                   sbit32 volatile  *Addend
                   );

osGLOBAL FORCEINLINE sbit32 
tdsmAtomicBitClear(
               smRoot_t         *smRoot,
               sbit32 volatile  *Destination,
               sbit32            Value
               );

osGLOBAL FORCEINLINE sbit32 
tdsmAtomicBitSet(
               smRoot_t         *smRoot,
               sbit32 volatile  *Destination,
               sbit32            Value
               );

osGLOBAL FORCEINLINE sbit32 
tdsmAtomicExchange(
               smRoot_t        *smRoot,
               sbit32 volatile *Target,
               sbit32           Value
               );
#else

osGLOBAL FORCEINLINE sbit32 
tdsmInterlockedIncrement(
                   smRoot_t        *smRoot,
                   sbit32 volatile *Addend
                   );

osGLOBAL FORCEINLINE sbit32 
tdsmInterlockedDecrement(
                   smRoot_t        *smRoot,
                   sbit32 volatile *Addend
                   );

osGLOBAL FORCEINLINE sbit32 
tdsmInterlockedAnd(
               smRoot_t         *smRoot,
               sbit32 volatile  *Destination,
               sbit32            Value
               );

osGLOBAL FORCEINLINE sbit32 
tdsmInterlockedOr(
               smRoot_t         *smRoot,
               sbit32 volatile  *Destination,
               sbit32            Value
               );

osGLOBAL FORCEINLINE sbit32 
tdsmInterlockedExchange(
               smRoot_t        *smRoot,
               sbit32 volatile *Target,
               sbit32           Value
               );

#endif /*LINUX_VERSION_CODE*/

osGLOBAL bit32 
tdsmAllocMemory(
                smRoot_t    *smRoot,
                void        **osMemHandle,
                void        ** virtPtr, 
                bit32       * physAddrUpper,
                bit32       * physAddrLower,
                bit32       alignment,
                bit32       allocLength,
                smBOOLEAN   isCacheable
               );

osGLOBAL bit32 
tdsmFreeMemory(
               smRoot_t    *smRoot,
               void        *osDMAHandle,
               bit32        allocLength
              );

osGLOBAL FORCEINLINE bit32
tdsmRotateQnumber(smRoot_t        *smRoot,
                         smDeviceHandle_t *smDeviceHandle
                         );

osGLOBAL bit32
tdsmSetDeviceQueueDepth(smRoot_t      *smRoot,
                                 smIORequest_t *smIORequest,
                                 bit32          QueueDepth
                                 );


#ifndef tdsmLogDebugString 
GLOBAL void tdsmLogDebugString(
                         smRoot_t     *smRoot,
                         bit32        level,
                         char         *string,
                         void         *ptr1,
                         void         *ptr2,
                         bit32        value1,
                         bit32        value2
                         );
#endif

  

osGLOBAL bit32 tdsmGetTransportParam(
                        smRoot_t    *smRoot,
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

#endif  /* TDSMAPI_H */

