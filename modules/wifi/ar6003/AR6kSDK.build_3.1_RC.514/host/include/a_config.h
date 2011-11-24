//------------------------------------------------------------------------------
// <copyright file="a_config.h" company="Atheros">
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
