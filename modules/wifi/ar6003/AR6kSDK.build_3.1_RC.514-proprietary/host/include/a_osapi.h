//------------------------------------------------------------------------------
// <copyright file="a_osapi.h" company="Atheros">
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
// This file contains the definitions of the basic atheros data types.
// It is used to map the data types in atheros files to a platform specific
// type.
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _A_OSAPI_H_
#define _A_OSAPI_H_

#if defined(__linux__) && !defined(LINUX_EMULATION)
#include "../os/linux/include/osapi_linux.h"
#endif

#ifdef ATHR_WM_NWF
#include "../os/windows/include/osapi.h"
#include "../os/windows/include/netbuf.h"
#endif

#ifdef ATHR_CE_LEGACY
#include "../os/windows/include/osapi.h"
#include "../os/windows/include/netbuf.h"
#endif

#ifdef REXOS
#include "../os/rexos/include/common/osapi_rexos.h"
#endif

#if defined ART_WIN
#include "../os/win_art/include/osapi_win.h"
#include "../os/win_art/include/netbuf.h"
#endif

#ifdef ATHR_WIN_NWF
#include "../os/windows/include/win/osapi_win.h"
#include "../os/windows/include/netbuf.h"
#endif

#endif /* _OSAPI_H_ */
