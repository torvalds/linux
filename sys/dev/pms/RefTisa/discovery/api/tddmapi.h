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
*   tmdmapi.h 
*
*   Abstract:   This module contains function prototype of the Discovery
*               Module (DM) API callback for initiator.
*******************************************************************************/

#ifndef TDDMAPI_H

#define TDDMAPI_H

osGLOBAL void tddmCacheFlush(
				dmRoot_t 	*dmRoot,
				void 		*tdMemHandle,
				void 		*virtPtr,
				bit32 		length
				);

osGLOBAL void tddmCacheInvalidate(
				dmRoot_t 	*dmRoot,
				void 		*tdMemHandle,
				void 		*virtPtr,
				bit32 		length
				);

osGLOBAL void tddmCachePreFlush(
				dmRoot_t 	*dmRoot,
				void 		*tdMemHandle,
				void 		*virtPtr,
				bit32 		length
				);

osGLOBAL void tddmDiscoverCB(
				dmRoot_t 		*dmRoot,
				dmPortContext_t		*dmPortContext,
				bit32			eventStatus
				);

osGLOBAL void tddmQueryDiscoveryCB(
				dmRoot_t 		*dmRoot,
				dmPortContext_t		*dmPortContext,
				bit32          		discType,
				bit32			discState
				);

osGLOBAL void tddmReportDevice(
				dmRoot_t 		*dmRoot,
				dmPortContext_t		*dmPortContext,
				dmDeviceInfo_t		*dmDeviceInfo,
                                dmDeviceInfo_t		*dmExpDeviceInfo,
				bit32                   flag				
				);

osGLOBAL bit8 tddmSATADeviceTypeDecode(bit8 * pSignature);

osGLOBAL void tddmSingleThreadedEnter(
				       dmRoot_t 		*dmRoot,
				       bit32    		syncLockId
				      );
				      
osGLOBAL void tddmSingleThreadedLeave(
				       dmRoot_t 		*dmRoot,
				       bit32    		syncLockId
				      );
osGLOBAL bit32 tddmGetTransportParam(
                        dmRoot_t    *dmRoot,
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
osGLOBAL bit32 
tddmRotateQnumber(
                  dmRoot_t          *dmRoot,
                  agsaDevHandle_t   *agDevHandle
                 );
#ifndef tddmLogDebugString 
GLOBAL void tddmLogDebugString(
                         dmRoot_t     *dmRoot,
                         bit32        level,
                         char         *string,
                         void         *ptr1,
                         void         *ptr2,
                         bit32        value1,
                         bit32        value2
                         );
#endif


#endif  /* TDDMAPI_H */
