//------------------------------------------------------------------------------
// <copyright file="athstartpack.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
// The software source and binaries included in this development package are
// licensed, not sold. You, or your company, received the package under one
// or more license agreements. The rights granted to you are specifically
// listed in these license agreement(s). All other rights remain with Atheros
// Communications, Inc., its subsidiaries, or the respective owner including
// those listed on the included copyright notices.  Distribution of any
// portion of this package must be in strict compliance with the license
// agreement(s) terms.
// </copyright>
// 
// <summary>
// 	Wifi driver for AR6002
// </summary>
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
