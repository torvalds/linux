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

$RCSfile: osdebug.h,v $
$Revision: 114125 $

Note:
***************************************************************************/

#ifndef __OSDEBUG_H__
#define __OSDEBUG_H__

#ifdef AGTIAPI_KDB_ENABLE
#include <linux/kdb.h>
#endif

/***************************************************************************
OS_ASSERT : This macro is used when an internal error is detected.      
***************************************************************************/
#ifdef  AGTIAPI_KDB_ENABLE
#define OS_ASSERT(expr, message)                                  \
do {                                                              \
          if (!(expr))                                            \
          {                                                       \
            printf("ASSERT: %s", message);                        \
            printf(" - file %s, line %d\n", __FILE__, __LINE__);  \
            BUG_ON(1);                                            \
            KDB_ENTER();                                          \
          }                                                       \
} while (0)
#else
#define OS_ASSERT(expr, message)                                  \
do {                                                              \
          if (!(expr))                                            \
          {                                                       \
            printf("ASSERT: %s", message);                        \
            printf(" - file %s, line %d\n", __FILE__, __LINE__);  \
          }                                                       \
} while (0)
#endif

#define AG_ERROR_MSG(mask, val, format) \
do {                                    \
          if (mask)                     \
          {                             \
            if (mask >= val)            \
              printf format;            \
          }                             \
          else                          \
            printf format;              \
} while (0)

#ifdef  TD_DEBUG_ENABLE
#define TIDEBUG_MSG(mask, val, format)  \
do {                                    \
          if (mask)                     \
          {                             \
            if (!val)                   \
              printf format ;           \
            else                        \
              if (!(mask & 0x80000000)) \
              {                         \
                if (mask >= val)        \
                  printf format ;       \
              }                         \
              else                      \
              {                         \
                if (mask & val)         \
                  printf format ;       \
              }                         \
          }                             \
} while (0)

#define TIDEBUG_MSG0(format)            \
do {                                    \
              printf format ;           \
} while (0)
#else
#define TIDEBUG_MSG(mask, val, format)
#define TIDEBUG_MSG0(format)
#endif

/***************************************************************************
FC debug - The following is used for FC specific debug.
**************************************************************************/
#ifdef AG_PROTOCOL_FC
#ifndef fcEnableTraceFunctions
#define fcEnableTraceFunctions 1
#endif
#else
#ifndef fcEnableTraceFunctions
#define fcEnableTraceFunctions 0
#endif

#endif /* AG_PROTOCOL_FC */

#endif /* #ifndef __OSDEBUG_H__ */

