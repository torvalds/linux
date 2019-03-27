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
 **   tiglobal.h
 **
 **   Abstract:
 **
 ********************************************************************************/


#ifndef TIGLOBAL_H
#define TIGLOBAL_H

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#ifndef TIDEBUG_MSG
#define TIDEBUG_MSG(mask, val, format)
#endif

extern bit32 gTiDebugLevel;

#define TI_DBG0(a)    TIDEBUG_MSG0(a) /* always print */

#define TI_DBG1(a)    TIDEBUG_MSG(gTiDebugLevel,1, a )
#define TI_DBG2(a)    TIDEBUG_MSG(gTiDebugLevel,2, a )
#define TI_DBG3(a)    TIDEBUG_MSG(gTiDebugLevel,3, a )
#define TI_DBG4(a)    TIDEBUG_MSG(gTiDebugLevel,4, a )
#define TI_DBG5(a)    TIDEBUG_MSG(gTiDebugLevel,5, a ) /* OsDebugLevel 4 */
#define TI_DBG6(a)    TIDEBUG_MSG(gTiDebugLevel,6, a ) 
#define TI_DBG7(a)    

extern bit32 gTiDebugMask;
#define TI_BIT1(a)    TIDEBUG_MSG(gTiDebugMask,0x00000001, a )
#define TI_BIT2(a)    TIDEBUG_MSG(gTiDebugMask,0x00000002, a )
#define TI_BIT3(a)    TIDEBUG_MSG(gTiDebugMask,0x00000004, a )
#define TI_BIT4(a)    TIDEBUG_MSG(gTiDebugMask,0x00000008, a )
#define TI_BIT5(a)    TIDEBUG_MSG(gTiDebugMask,0x00000010, a )
#define TI_BIT6(a)    TIDEBUG_MSG(gTiDebugMask,0x00000020, a )



#endif  /* TIGLOBAL_H */
