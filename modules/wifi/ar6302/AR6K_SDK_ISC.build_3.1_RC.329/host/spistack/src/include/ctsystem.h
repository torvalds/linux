//------------------------------------------------------------------------------
// <copyright file="ctsystem.h" company="Atheros">
//    Copyright (c) 2007-2008 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#ifndef __CPSYSTEM_H___
#define __CPSYSTEM_H___

/* SDIO stack status defines */
/* < 0 error, >0 warning, 0 success */
#define SDIO_IS_WARNING(status) ((status) > 0)
#define SDIO_IS_ERROR(status) ((status) < 0)
#define SDIO_SUCCESS(status) ((SDIO_STATUS)(status) >= 0)
#define SDIO_STATUS_SUCCESS             0
#define SDIO_STATUS_ERROR              -1
#define SDIO_STATUS_INVALID_PARAMETER  -2
#define SDIO_STATUS_PENDING             3
#define SDIO_STATUS_DEVICE_NOT_FOUND   -4
#define SDIO_STATUS_DEVICE_ERROR       -5
#define SDIO_STATUS_INTERRUPTED        -6
#define SDIO_STATUS_NO_RESOURCES       -7  
#define SDIO_STATUS_CANCELED           -8  
#define SDIO_STATUS_BUFFER_TOO_SMALL   -9
#define SDIO_STATUS_NO_MORE_MESSAGES   -10
#define SDIO_STATUS_IO_TIMEOUT         -11
#define SDIO_STATUS_BUS_READ_ERROR     -22   /* readfailed */
#define SDIO_STATUS_BUS_WRITE_ERROR    -23   /* write failed */
#define SDIO_STATUS_SDREQ_QUEUE_FAILED   -34 /* request failed to insert into queue */ 
#define SDIO_STATUS_UNSUPPORTED          -36  /* not supported  */

#ifdef VXWORKS
/* Wind River VxWorks support */
#include "vxworks/ctsystem_vxworks.h"
#endif /* VXWORKS */

#if defined(LINUX) || defined(__linux__)
/* Linux support */
#include "linux/ctsystem_linux.h"
#endif /* LINUX */

#ifdef QNX
/* QNX Neutrino support */
#include "nto/ctsystem_qnx.h"
#endif /* QNX */

#ifdef INTEGRITY
/* Greenhils Integrity support */
#include "integrity/ctsystem_integrity.h"
#endif /* INTEGRITY */

#ifdef NUCLEUS_PLUS
/* Mentor Graphics Nucleus support */
#include "nucleus/ctsystem_nucleus.h"
#endif /* NUCLEUS_PLUS */

#ifdef UNDER_CE
#define CTSYSTEM_NO_FUNCTION_PROXIES
/* Windows CE  support */
#include "wince/ctsystem_wince.h"
#endif /* WINCE */

#ifdef _IFX_
/* IFX support */
#include "ifx/ctsystem_ifx.h"
#endif /* IFX */

/* get structure from contained field */
#define CONTAINING_STRUCT(address, struct_type, field_name)\
            ((struct_type *)((ULONG_PTR)(address) - (ULONG_PTR)(&((struct_type *)0)->field_name)))

#define ZERO_OBJECT(obj) memset(&(obj),0,sizeof(obj))    
#define ZERO_POBJECT(pObj) memset((pObj),0,sizeof(*(pObj)))  
    
    
/* bit field support functions */
static INLINE void SetBit(PULONG pField, UINT position) {
    *pField |= 1 << position;
}
static INLINE void ClearBit(PULONG pField, UINT position) {
    *pField &= ~(1 << position);
}
static INLINE BOOL IsBitSet(PULONG pField, UINT position) {
    return (*pField & (1 << position));
}
static INLINE INT FirstClearBit(PULONG pField) {
    UINT ii;
    for(ii = 0; ii < sizeof(ULONG)*8; ii++) {
        if (!IsBitSet(pField, ii)) {
            return ii;
        }  
    }
    /* no clear bits found */
    return -1;
}

#endif /* __CPSYSTEM_H___ */
