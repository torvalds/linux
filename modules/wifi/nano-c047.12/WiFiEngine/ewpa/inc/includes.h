/*
 * wpa_supplicant/hostapd - Default include files
 * Copyright (c) 2005-2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This header file is included into all C files so that commonly used header
 * files can be selected with OS specific #ifdefs in one place instead of
 * having to have OS/C library specific selection in many files.
 */

#ifndef INCLUDES_H
#define INCLUDES_H

/* Include possible build time configuration before including anything else */
#include "build_config.h"

#ifdef CONFIG_DRIVER_WIFIENGINE
#include "sysdef.h" /* Must be first include */
#include "wifi_engine_internal.h"

#if defined(__mqx__)
#define printf my_printf
#define vprintf my_vprintf
#endif /* __mqx__ */

#ifdef __rtke__
#ifndef inline
#define inline __inline
#endif
#define printf my_printf
#define vprintf my_vprintf
#endif /* __rtke__ */

#else /* CONFIG_DRIVER_WIFIENGINE */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifndef _WIN32_WCE
#ifndef CONFIG_TI_COMPILER
#include <signal.h>
#include <sys/types.h>
#endif /* CONFIG_TI_COMPILER */
#include <errno.h>
#endif /* _WIN32_WCE */
#include <ctype.h>
#include <time.h>

#ifndef CONFIG_TI_COMPILER
#ifndef _MSC_VER
#include <unistd.h>
#endif /* _MSC_VER */
#endif /* CONFIG_TI_COMPILER */

#ifndef CONFIG_NATIVE_WINDOWS
#ifndef CONFIG_TI_COMPILER
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifndef __vxworks
#include <sys/uio.h>
#include <sys/time.h>
#endif /* __vxworks */
#endif /* CONFIG_TI_COMPILER */
#endif /* CONFIG_NATIVE_WINDOWS */

#endif /* CONFIG_DRIVER_WIFIENGINE */

#endif /* INCLUDES_H */
