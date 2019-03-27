/*******************************************************************************
**
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
**
* $FreeBSD$
*
********************************************************************************/
/********************************************************************************
*   dmapi.h 
*
*   Abstract:   This module contains function prototype of the Discovery
*               Module (DM) API for initiator.
*******************************************************************************/

#ifndef DMAPI_H
#define DMAPI_H

#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/sallsdk/api/sa.h>

osGLOBAL bit32  dmCreatePort(  
       dmRoot_t        *dmRoot,
       dmPortContext_t *dmPortContext,
       dmPortInfo_t    *dmPortInfo);

osGLOBAL bit32  dmDestroyPort(  
       dmRoot_t        *dmRoot,
       dmPortContext_t *dmPortContext,
       dmPortInfo_t    *dmPortInfo);

osGLOBAL bit32  dmRegisterDevice(  
       dmRoot_t        *dmRoot,
       dmPortContext_t *dmPortContext,
       dmDeviceInfo_t  *dmDeviceInfo,
       agsaDevHandle_t *agDevHandle);

osGLOBAL bit32  dmDiscover(  
       dmRoot_t        *dmRoot,
       dmPortContext_t *dmPortContext,
       bit32            option);

osGLOBAL void dmGetRequirements(
       dmRoot_t              *dmRoot,
       dmSwConfig_t          *swConfig,
       dmMemoryRequirement_t *memoryRequirement,
       bit32                 *usecsPerTick,
       bit32                 *maxNumLocks);

osGLOBAL void dmNotifyBC(
       dmRoot_t        *dmRoot,
       dmPortContext_t *dmPortContext,
       bit32            type);

osGLOBAL bit32  dmQueryDiscovery(  
       dmRoot_t        *dmRoot,
       dmPortContext_t *dmPortContext);
       
osGLOBAL bit32 	
dmResetFailedDiscovery(  
                 dmRoot_t               *dmRoot,
                 dmPortContext_t        *dmPortContext);

osGLOBAL bit32  dmInitialize(
       dmRoot_t             *dmRoot,
       agsaRoot_t           *agRoot,
       dmMemoryRequirement_t *memoryAllocated,
       dmSwConfig_t          *swConfig,
       bit32                 usecsPerTick );

osGLOBAL void   dmTimerTick ( dmRoot_t  *dmRoot );

#endif  /* DMAPI_H */
