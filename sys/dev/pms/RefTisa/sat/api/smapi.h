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
*   smapi.h 
*
*   Abstract:   This module contains function prototype of the SAT
*               Module (SM) API for initiator.
*******************************************************************************/

#ifndef SMAPI_H
#define SMAPI_H

#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sallsdk/api/sa.h>

osGLOBAL bit32
smRegisterDevice(
                 smRoot_t                       *smRoot,
                 agsaDevHandle_t                *agDevHandle,
                 smDeviceHandle_t               *smDeviceHandle,
                 agsaDevHandle_t                *agExpDevHandle,
                 bit32                          phyID,
                 bit32                          DeviceType
                );

osGLOBAL bit32
smDeregisterDevice(
                   smRoot_t                     *smRoot,
                   agsaDevHandle_t              *agDevHandle,
                   smDeviceHandle_t             *smDeviceHandle
                  );
		  
osGLOBAL void
smGetRequirements(
                  smRoot_t                      *smRoot,
                  smSwConfig_t                  *swConfig,
                  smMemoryRequirement_t         *memoryRequirement,
                  bit32                         *usecsPerTick,
                  bit32                         *maxNumLocks
                 );

osGLOBAL bit32
smIDStart(
          smRoot_t                     *smRoot,
          smIORequest_t                *smIORequest,
          smDeviceHandle_t             *smDeviceHandle
         );

osGLOBAL bit32
smInitialize(
             smRoot_t                           *smRoot,
             agsaRoot_t                         *agRoot,
             smMemoryRequirement_t              *memoryAllocated,
             smSwConfig_t                       *swConfig,
             bit32                              usecsPerTick 
            );

osGLOBAL bit32
smIOAbort(
           smRoot_t                     *smRoot,
           smIORequest_t                *tasktag
         );

osGLOBAL bit32
smIOAbortAll(
             smRoot_t                     *smRoot,
             smDeviceHandle_t             *smDeviceHandle
            );

osGLOBAL FORCEINLINE bit32
smIOStart(
          smRoot_t                      *smRoot,
          smIORequest_t                 *smIORequest,
          smDeviceHandle_t              *smDeviceHandle,
          smScsiInitiatorRequest_t      *smSCSIRequest,
          bit32                         interruptContext
         );

osGLOBAL bit32
smSuperIOStart(
               smRoot_t                         *smRoot,
               smIORequest_t                    *smIORequest,
               smDeviceHandle_t                 *smDeviceHandle,
               smSuperScsiInitiatorRequest_t    *smSCSIRequest,
               bit32                            AddrHi,
               bit32                            AddrLo,	       
               bit32                            interruptContext
              );
	 
osGLOBAL bit32
smTaskManagement(
                 smRoot_t                       *smRoot,
                 smDeviceHandle_t               *smDeviceHandle,
                 bit32                          task,
                 smLUN_t                        *lun,
                 smIORequest_t                  *taskTag,
                 smIORequest_t                  *currentTaskTag
                );

#endif  /* SMAPI_H */

