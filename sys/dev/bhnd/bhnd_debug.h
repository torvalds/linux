/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Michael Zhilin <mizhka@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

/* $FreeBSD$ */

/*
 * This file provides set of macros for logging:
 *  - BHND_<LEVEL> and
 *  - BHND_<LEVEL>_DEV
 * where LEVEL = {ERROR,WARN,INFO,DEBUG}
 *
 * BHND_<LEVEL> macros is proxies to printf call and accept same parameters,
 * for instance:
 * 	BHND_INFO("register %d has value %d", reg, val);
 *
 * BHND_<LEVEL>_DEV macros is proxies to device_printf call and accept
 * same parameters, for instance:
 * 	BHND_INFO_DEV(dev, "register %d has value %d", reg, val);
 *
 * All macros contains newline char at the end of each call
 *
 * ERROR, WARN, INFO messages are printed only if:
 * 	- log message level is lower than BHND_LOGGING (logging threshold)
 *
 * DEBUG, TRACE messages are printed only if:
 * 	- bootverbose and
 * 	- log message level is lower than BHND_LOGGING (logging threshold)
 *
 * In addition, for debugging purpose log message contains information about
 * file name and line number if BHND_LOGGING is more than BHND_INFO_LEVEL
 *
 * NOTE: macros starting with underscore (_) are private and should be not used
 *
 * To override logging (for instance, force tracing), you can use:
 *  - "options BHND_LOGLEVEL=BHND_TRACE_LEVEL" in kernel configuration
 *  - "#define	BHND_LOGGING	BHND_TRACE_LEVEL" in source code file
 *
 * NOTE: kernel config option doesn't override log level defined on file level,
 * so try to avoid "#define	BHND_LOGGING"
 */

#ifndef _BHND_BHND_DEBUG_H_
#define _BHND_BHND_DEBUG_H_

#include <sys/systm.h>

#define	BHND_ERROR_LEVEL	0x00
#define	BHND_ERROR_MSG		"ERROR"
#define	BHND_WARN_LEVEL		0x10
#define	BHND_WARN_MSG		"!WARN"
#define	BHND_INFO_LEVEL		0x20
#define	BHND_INFO_MSG		" info"
#define	BHND_DEBUG_LEVEL	0x30
#define	BHND_DEBUG_MSG		"debug"
#define	BHND_TRACE_LEVEL	0x40
#define	BHND_TRACE_MSG		"trace"

#if !(defined(BHND_LOGGING))
#if !(defined(BHND_LOGLEVEL))
/* By default logging will print only INFO+ message*/
#define	BHND_LOGGING		BHND_INFO_LEVEL
#else /* defined(BHND_LOGLEVEL) */
/* Kernel configuration specifies logging level */
#define	BHND_LOGGING		BHND_LOGLEVEL
#endif /* !(defined(BHND_LOGLEVEL)) */
#endif /* !(defined(BHND_LOGGING)) */

#if BHND_LOGGING > BHND_INFO_LEVEL
#define	_BHND_PRINT(fn, level, fmt, ...)				\
	do {								\
		if (level##LEVEL < BHND_DEBUG_LEVEL || bootverbose)	\
		    fn "[BHND " level##MSG "] %s:%d => " fmt "\n",	\
			__func__, __LINE__, ## __VA_ARGS__);		\
	} while(0);
#else /* BHND_LOGGING <= BHND_INFO_LEVEL */
#define	_BHND_PRINT(fn, level, fmt, ...)				\
	do {								\
		if (level##LEVEL < BHND_DEBUG_LEVEL || bootverbose)	\
		    fn "bhnd: " fmt "\n", ## __VA_ARGS__);		\
	} while(0);
#endif /* BHND_LOGGING > BHND_INFO_LEVEL */


#define	_BHND_RAWPRINTFN	printf(
#define	_BHND_DEVPRINTFN(dev)	device_printf(dev,

#define	_BHND_LOGPRINT(level, fmt, ...) 				\
	_BHND_PRINT(_BHND_RAWPRINTFN, level, fmt, ## __VA_ARGS__)
#define	_BHND_DEVPRINT(dev, level, fmt, ...)				\
	_BHND_PRINT(_BHND_DEVPRINTFN(dev), level, fmt, ## __VA_ARGS__)

#define	BHND_ERROR(fmt, ...)						\
	_BHND_LOGPRINT(BHND_ERROR_, fmt, ## __VA_ARGS__);
#define	BHND_ERROR_DEV(dev, fmt, ...)					\
	_BHND_DEVPRINT(dev, BHND_ERROR_, fmt, ## __VA_ARGS__)

#if BHND_LOGGING >= BHND_WARN_LEVEL
#define	BHND_WARN(fmt, ...)						\
	_BHND_LOGPRINT(BHND_WARN_, fmt, ## __VA_ARGS__)
#define	BHND_WARN_DEV(dev, fmt, ...)					\
	_BHND_DEVPRINT(dev, BHND_WARN_, fmt, ## __VA_ARGS__)

#if BHND_LOGGING >= BHND_INFO_LEVEL
#define	BHND_INFO(fmt, ...)						\
	_BHND_LOGPRINT(BHND_INFO_, fmt, ## __VA_ARGS__)
#define	BHND_INFO_DEV(dev, fmt, ...)					\
	_BHND_DEVPRINT(dev, BHND_INFO_, fmt, ## __VA_ARGS__)

#if BHND_LOGGING >= BHND_DEBUG_LEVEL
#define	BHND_DEBUG(fmt, ...)						\
	_BHND_LOGPRINT(BHND_DEBUG_, fmt, ## __VA_ARGS__)
#define	BHND_DEBUG_DEV(dev, fmt, ...)					\
	_BHND_DEVPRINT(dev, BHND_DEBUG_, fmt, ## __VA_ARGS__)

#if BHND_LOGGING >= BHND_TRACE_LEVEL
#define	BHND_TRACE(fmt, ...)						\
	_BHND_LOGPRINT(BHND_TRACE_, fmt, ## __VA_ARGS__)
#define	BHND_TRACE_DEV(dev, fmt, ...)					\
	_BHND_DEVPRINT(dev, BHND_TRACE_, fmt, ## __VA_ARGS__)

#endif /* BHND_LOGGING >= BHND_TRACE_LEVEL */
#endif /* BHND_LOGGING >= BHND_DEBUG_LEVEL */
#endif /* BHND_LOGGING >= BHND_INFO_LEVEL */
#endif /* BHND_LOGGING >= BHND_WARN_LEVEL */

/*
 * Empty defines without device context
 */
#if !(defined(BHND_WARN))
#define	BHND_WARN(fmt, ...);
#endif

#if !(defined(BHND_INFO))
#define	BHND_INFO(fmt, ...);
#endif

#if !(defined(BHND_DEBUG))
#define	BHND_DEBUG(fmt, ...);
#endif

#if !(defined(BHND_TRACE))
#define	BHND_TRACE(fmt, ...);
#endif

/*
 * Empty defines with device context
 */
#if !(defined(BHND_WARN_DEV))
#define	BHND_WARN_DEV(dev, fmt, ...);
#endif

#if !(defined(BHND_INFO_DEV))
#define	BHND_INFO_DEV(dev, fmt, ...);
#endif

#if !(defined(BHND_DEBUG_DEV))
#define	BHND_DEBUG_DEV(dev, fmt, ...);
#endif

#if !(defined(BHND_TRACE_DEV))
#define	BHND_TRACE_DEV(dev, fmt, ...);
#endif

#endif /* _BHND_BHND_DEBUG_H_ */
