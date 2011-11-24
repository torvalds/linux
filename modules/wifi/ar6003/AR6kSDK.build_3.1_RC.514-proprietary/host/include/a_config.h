//------------------------------------------------------------------------------
// <copyright file="a_config.h" company="Atheros">
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
// This file contains software configuration options that enables
// specific software "features"
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _A_CONFIG_H_
#define _A_CONFIG_H_

#ifdef ATHR_WM_NWF
#include "../os/windows/include/config.h"
#endif

#ifdef ATHR_CE_LEGACY
#include "../os/windows/include/config.h"
#endif

#if defined(__linux__) && !defined(LINUX_EMULATION)
#include "../os/linux/include/config_linux.h"
#endif

#ifdef REXOS
#include "../os/rexos/include/common/config_rexos.h"
#endif

#ifdef ATHR_WIN_NWF
#include "../os/windows/include/config.h"
#pragma warning( disable:4242)
#pragma warning( disable:4100)
#pragma warning( disable:4189)
#pragma warning( disable:4244)
#pragma warning( disable:4701)
#pragma warning( disable:4389)
#pragma warning( disable:4057)
#pragma warning( disable:28193)
#endif

#endif
