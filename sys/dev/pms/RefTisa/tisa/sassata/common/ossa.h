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
/*******************************************************************************/
/** \file
 *
 * The file defines the declaration of OS types
 *
 */

#ifndef __OS_SA_H__
#define __OS_SA_H__

#define DEBUG_LEVEL   OSSA_DEBUG_LEVEL_1
#define ossaLogDebugString    TIDEBUG_MSG
#define ossaAssert OS_ASSERT

#define tddmLogDebugString TIDEBUG_MSG
#define DM_ASSERT OS_ASSERT

#define tdsmLogDebugString TIDEBUG_MSG
#define SM_ASSERT OS_ASSERT

#ifdef NOT_YET /* no longer valid */
#define ossaLogDebugString(agRoot, level, string, ptr1, ptr2, value1, value2) \
  do { \
    if ( level <= DEBUG_LEVEL ) \
    {                          \
      printk("%s:", __FUNCTION__); \
      if ( agNULL != string )  \
      {                        \
        printk("%s:", string); \
      }                        \
      if ( agNULL != ptr1 )    \
      {                        \
        printk("ptr1=%p,", ptr1); \
      }                           \
      if ( agNULL != ptr2 )       \
      {                           \
        printk("ptr2=%p,", ptr2); \
      }                           \
      if ( OSSA_DEBUG_PRINT_INVALID_NUMBER != value1 ) \
      {                                   \
        printk("value1=0x%08x ", value1); \
      }                                   \
      if ( OSSA_DEBUG_PRINT_INVALID_NUMBER != value2 ) \
      {                                                \
        printk("value2=0x%08x ", value2);              \
      } \
      printk("\n"); \
    }        \
  } while (0); 

#ifndef ossaAssert
#define ossaAssert(agRoot, expr, message) \
  do {                                                              \
    if (agFALSE == (expr))                                            \
    {                                                       \
      printk("ossaAssert: %s", (message));                        \
      printk(" - file %s, line %d\n", __FILE__, __LINE__);  \
    }                                                       \
  } while (0);
#endif
#endif /* 0 */
#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#endif  /* __OS_SA_H__ */
