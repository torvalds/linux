/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Oracle and/or its affiliates.
 *
 * Common types and format specifiers for sunrpc.
 */

#if !defined(_TRACE_SUNRPC_BASE_H)
#define _TRACE_SUNRPC_BASE_H

#include <linux/tracepoint.h>

#define SUNRPC_TRACE_PID_SPECIFIER	"%08x"
#define SUNRPC_TRACE_CLID_SPECIFIER	"%08x"
#define SUNRPC_TRACE_TASK_SPECIFIER \
	"task:" SUNRPC_TRACE_PID_SPECIFIER "@" SUNRPC_TRACE_CLID_SPECIFIER

#endif /* _TRACE_SUNRPC_BASE_H */
