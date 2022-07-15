/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM module

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_MODULE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MODULE_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#ifdef __GENKSYMS__
struct module;
#else
/* struct module */
#include <linux/module.h>
#endif /* __GENKSYMS__ */
DECLARE_HOOK(android_vh_set_module_permit_before_init,
	TP_PROTO(const struct module *mod),
	TP_ARGS(mod));

DECLARE_HOOK(android_vh_set_module_permit_after_init,
	TP_PROTO(const struct module *mod),
	TP_ARGS(mod));

#endif /* _TRACE_HOOK_MODULE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
