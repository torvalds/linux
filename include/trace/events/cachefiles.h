/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CacheFiles tracepoints
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cachefiles

#if !defined(_TRACE_CACHEFILES_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CACHEFILES_H

#include <linux/tracepoint.h>

/*
 * Define enums for tracing information.
 */
#ifndef __CACHEFILES_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __CACHEFILES_DECLARE_TRACE_ENUMS_ONCE_ONLY

#endif

/*
 * Define enum -> string mappings for display.
 */


/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }


#endif /* _TRACE_CACHEFILES_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
