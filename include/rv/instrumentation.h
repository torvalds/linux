/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
 *
 * Helper functions to facilitate the instrumentation of auto-generated
 * RV monitors create by dot2k.
 *
 * The dot2k tool is available at tools/verification/dot2/
 */

#include <linux/ftrace.h>

/*
 * rv_attach_trace_probe - check and attach a handler function to a tracepoint
 */
#define rv_attach_trace_probe(monitor, tp, rv_handler)					\
	do {										\
		check_trace_callback_type_##tp(rv_handler);				\
		WARN_ONCE(register_trace_##tp(rv_handler, NULL),			\
				"fail attaching " #monitor " " #tp "handler");		\
	} while (0)

/*
 * rv_detach_trace_probe - detach a handler function to a tracepoint
 */
#define rv_detach_trace_probe(monitor, tp, rv_handler)					\
	do {										\
		unregister_trace_##tp(rv_handler, NULL);				\
	} while (0)
