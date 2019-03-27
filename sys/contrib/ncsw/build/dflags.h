/*-
 * Copyright (c) 2011 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef DFLAGS_H_
#define DFLAGS_H_

#include "opt_platform.h"
#include "events_mapping.h"

#if defined(P3041DS)
#define	P3041
#elif defined(P2041RDB)
#define P2041
#elif defined(P5020DS)
#define P5020
#else
#define P5020
#endif

#define	NCSW_PPC_CORE
#define	NCSW_FREEBSD

/* Debugging */
#define	DEBUG_ERRORS		1
#define	DPAA_DEBUG		1
#if defined(DPAA_DEBUG)
#define	DEBUG_GLOBAL_LEVEL	REPORT_LEVEL_INFO

#else
#define	DEBUG_GLOBAL_LEVEL	REPORT_LEVEL_WARNING
#endif

/* Events */
#define	REPORT_EVENTS		1
#define	EVENT_GLOBAL_LEVEL	REPORT_LEVEL_MINOR

#endif /* DFLAGS_H_ */
