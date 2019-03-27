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
This program is separated from main driver source due to the common usage
of both initiator and target.
*******************************************************************************
Module Name:  
  osstring.h
Abstract:  
  FreeBSD SPCv Initiator driver module OS API definitions
Authors:  
  EW - Eddie Wang
Environment:  
  Kernel or loadable module  

Version Control Information:  
  $ver. 1.0.0
    
Revision History:
  $Revision: 114125 $0.1.0
  $Date: 2012-01-06 17:12:27 -0800 (Fri, 06 Jan 2012) $08-27-2001
  $Modtime: 11/12/01 11:15a $11:46:00

Notes:

**************************** MODIFICATION HISTORY ***************************** 
NAME     DATE         Rev.        DESCRIPTION
----     ----         ----        -----------
EW     05-27-2002     1.0.0     Code construction started.
******************************************************************************/

#ifndef __OSSTRING_H__
#define __OSSTRING_H__
#include <sys/libkern.h>
#include <sys/syslimits.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/ctype.h>

#define osti_memcmp(s1, s2, n)     memcmp((void *)s1, (void *)s2, (size_t)n)
#define osti_memcpy(des, src, n)   memcpy((void *)des, (void *)src, (size_t)n)
#define osti_memset(s, c, n)       memset((void *)s, (int)c, (size_t)n)  
#define osti_strcat(des, src)      strcat((char *)des, (char *)src)  
#define osti_strchr(s, n)          strchr((char *)s, (int)n)  
#define osti_strcmp(s1, s2)        strcmp((char *)s1, (char *)s2)
#define osti_strcpy(des, src)      strcpy((char *)des, (char *)src)  
#define osti_strlen(s)             strlen((char *)s)  
#define osti_strncmp(s1, s2, n)    strncmp((char *)s1, (char *)s2, (size_t)n)
#define osti_strncpy(des, src, n)  strncpy((char *)des, (char *)src, (size_t)n)
#define osti_strstr(s1, s2)        strstr((char *)s1, (char *)s2)  

#define osti_strtoul(nptr, endptr, base)    \
          strtoul((char *)nptr, (char **)endptr, 0)

#define osti_isxdigit(c)           isxdigit(c)
#define osti_isdigit(c)            isdigit(c)
#define osti_islower(c)            islower(c)

#define osMemCpy(des, src, n)   memcpy((void *)des, (void *)src, (size_t)n)
#define osMemSet(s, c, n)       memset((void *)s, (int)c, (size_t)n)  

#endif  /* __OSSTRING_H__ */
