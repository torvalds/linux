/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM regmap
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_REGMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_REGMAP_H
#include <trace/hooks/vendor_hooks.h>

struct regmap_config;
struct regmap;

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_HOOK(android_vh_regmap_update,
	TP_PROTO(const struct regmap_config *config, struct regmap *map),
	TP_ARGS(config, map));

#endif /* _TRACE_HOOK_REGMAP_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
