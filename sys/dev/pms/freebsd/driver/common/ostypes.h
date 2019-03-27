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
/******************************************************************************

Note:
*******************************************************************************
Module Name:  
  ostypes.h
Abstract:  
  Request by fclayer for data type define.
Authors:  
  EW - Yiding(Eddie) Wang
Environment:  
  Kernel or loadable module  

Version Control Information:  
  $ver. 1.0.0
    
Revision History:
  $Revision: 114125 $0.1.0
  $Date: 2012-04-23 23:37:56 -0700 (Mon, 23 Apr 2012) $09-27-2001
  $Modtime: 11/12/01 11:15a $15:56:00

Notes:
**************************** MODIFICATION HISTORY ***************************** 
NAME     DATE         Rev.          DESCRIPTION
----     ----         ----          -----------
EW     09-16-2002     0.1.0     Header file for most constant definitions
******************************************************************************/

#ifndef __OSTYPES_H__
#define __OSTYPES_H__

#include <sys/types.h>
#include <sys/kernel.h>


/*
** Included for Linux 2.4, built in kernel and other possible cases.
*/
/*
#ifdef  TARGET_DRIVER
#if !defined(AGBUILD_TFE_DRIVER) && !defined(COMBO_IBE_TFE_MODULE)
#include "lxtgtdef.h"
#endif
#endif
*/
/*
** Included for possible lower layer ignorance.
*/
#include "osdebug.h"

#ifdef  STATIC
#undef  STATIC
#endif

#define STATIC

#ifndef INLINE
#define INLINE inline
#endif


#ifndef FORCEINLINE
#define FORCEINLINE
//#define FORCEINLINE inline

#endif
#if defined (__amd64__)
#define BITS_PER_LONG	    64
#else
#define BITS_PER_LONG	    32
#endif


typedef unsigned char       bit8;
typedef unsigned short      bit16;
typedef unsigned int        bit32;
typedef char                sbit8;
typedef short               sbit16;
typedef int                 sbit32;
typedef unsigned int        BOOLEAN;
typedef unsigned long long  bit64;
typedef long long           sbit64;

//typedef unsigned long long  bitptr;
#if 1
#if (BITS_PER_LONG == 64)
typedef unsigned long long  bitptr;
#else
typedef unsigned long       bitptr;
#endif
#endif

typedef char                S08;
typedef short               S16;
typedef int                 S32;
typedef long                S32_64;
typedef long long           S64;

typedef unsigned char       U08;
typedef unsigned short      U16;
typedef unsigned int        U32;
typedef unsigned long       U32_64;
typedef unsigned long long  U64;

/*
** some really basic defines
*/ 
#define GLOBAL extern
#define LOCAL static
#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif
#ifndef SUCCESS
#define SUCCESS	0
#define FAILURE	1
#endif
#ifndef NULL
#define NULL ((void*)0)
#endif


#define agBOOLEAN  BOOLEAN
#define osGLOBAL   GLOBAL
#define osLOCAL    LOCAL
#define agTRUE     TRUE
#define agFALSE    FALSE
#define agNULL     NULL

#define AGTIAPI_UNKNOWN     2
#define AGTIAPI_SUCCESS     1
#define AGTIAPI_FAIL        0

#define AGTIAPI_DRIVER_VERSION "1.4.0.10800"

/***************************************************************************
****************************************************************************
* MACROS - some basic macros    
****************************************************************************
***************************************************************************/
#ifndef BIT
#define BIT(x)          (1<<x)
#endif

#define osti_sprintf    sprintf

#endif  /* __OSTYPES_H__ */
