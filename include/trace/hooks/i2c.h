/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM i2c

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_I2C_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_I2C_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_of_i2c_get_board_info,
	TP_PROTO(struct device_node *node, const char **dev_name),
	TP_ARGS(node, dev_name));

#endif /* _TRACE_HOOK_I2C_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
