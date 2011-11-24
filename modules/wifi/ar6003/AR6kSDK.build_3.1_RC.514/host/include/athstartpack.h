//------------------------------------------------------------------------------
// <copyright file="athstartpack.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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
// start compiler-specific structure packing
//
// Author(s): ="Atheros"
//==============================================================================
#ifdef VXWORKS
#endif /* VXWORKS */

#if defined(LINUX) || defined(__linux__)
#endif /* LINUX */

#ifdef QNX
#endif /* QNX */

#ifdef INTEGRITY
#include "integrity/athstartpack_integrity.h"
#endif /* INTEGRITY */

#ifdef NUCLEUS
#endif /* NUCLEUS */

#ifdef ATHR_WM_NWF
#include "../os/windows/include/athstartpack.h"
#define PREPACK
#endif

#ifdef ATHR_CE_LEGACY
#include "../os/windows/include/athstartpack.h"
#endif /* WINCE */

#ifdef ATHR_WIN_NWF

#ifndef PREPACK
#define PREPACK __declspec(align(1))
#endif

#include <athstartpack_win.h>
#define __ATTRIB_PACK POSTPACK

#endif

#if __LONG_MAX__ == __INT_MAX__
/* 32-bit compilation */
#define PREPACK64
#define POSTPACK64
#else
/* 64-bit compilation */
#define PREPACK64 PREPACK
#define POSTPACK64 POSTPACK
#endif
