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
*******************************************************************************/
/***************************************************************************

Version Control Information:
 
$RCSfile: osenv.h,v $
$Revision: 114125 $

Note:  This file defines the working environment of the system.  All
       defines listed in this file could also be compiler flags.
       I am listing all the defines (even if used as a compiler flag)
       so that they can be seen and documented.
***************************************************************************/

#ifndef __OSENV_H__
#define __OSENV_H__
#include <dev/pms/freebsd/driver/common/osstring.h>

/* 
** Define the protocols to compile with.  Currently, these defines are
** only for this header file and are used further down to define the protocol
** specific environment:
**
**      #define AG_PROTOCOL_ISCSI
**      #define AG_PROTOCOL_FC
*/

/* 
** Define the application types:
**
**      #define INITIATOR_DRIVER
**      #define TARGET_DRIVER
*/ 

/* 
** Define the endian-ness of the host CPU using one of the following:
**
**      #define AG_CPU_LITTLE_ENDIAN
**      #define AG_CPU_BIG_ENDIAN
*/ 

/*
** Define the host CPU word size 
**
**      #define AG_CPU_32_BIT
**      #define AG_CPU_64_BIT
**
*/
#ifdef CONFIG_IA64
#define AG_CPU_64_BIT
#else
#define AG_CPU_32_BIT
#endif

/*
** The following allow the code to use defines for word alignment and adding
** to allow for 32bit and 64bit system differences.
*/
#ifdef AG_CPU_32_BIT
#define AG_WORD_ALIGN_ADD      3
#define AG_WORD_ALIGN_MASK     0xfffffffc
#else
#define AG_WORD_ALIGN_ADD      7
#define AG_WORD_ALIGN_MASK     0xfffffff8
#endif

/***************************************************************************
iSCSI environment - The following is used for compiling the iSCSI
                     protocol.
**************************************************************************/

/*
** Define the existence of an external bus swapper using on of the
** following: 
**
**      #define AG_SWAPPING_BUS
**      #define AG_NON_SWAPPING_BUS
**
*/

/*
** Define the use of cache memory for message system: 
**
**      #define AG_CACHED_MSG_SYSTEM
**
*/
/* #define AG_CACHED_MSG_SYSTEM */

/***************************************************************************
FC environment - The following is used for compiling the FC protocol.
**************************************************************************/

/*
** Define if an PMC-Sierra card is being used: 
**
**      #define CCFLAGS_PMC_SIERRA_BOARD
**
*/

/*
** Define if the TSDK is being used: 
**
**      #define FCLayer_Tsdk
**
*/

/*
** The following defines are not changed directly, but use either previous
** defines, or compiler directives.
**
*/
#ifdef AG_CPU_LITTLE_ENDIAN
#define FC_DMA_LITTLE_ENDIAN
#define FC_CPU_LITTLE_ENDIAN
#define SA_DMA_LITTLE_ENDIAN
#define SA_CPU_LITTLE_ENDIAN
#endif

#ifdef AG_CPU_BIG_ENDIAN
#define FC_DMA_BIG_ENDIAN
#define FC_CPU_BIG_ENDIAN
#define SA_DMA_BIG_ENDIAN
#define SA_CPU_BIG_ENDIAN
#endif

/* warning: leave this next line as-is.  it is used for FC-Layer testing      */ 
#undef   FC_CHECKMACRO 

#endif /* __OSENV_H__ */
