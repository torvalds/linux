/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fault
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FAULT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FAULT_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct pt_regs;
DECLARE_RESTRICTED_HOOK(android_rvh_die_kernel_fault,
	TP_PROTO(struct pt_regs *regs, unsigned int esr, unsigned long addr, const char *msg),
	TP_ARGS(regs, esr, addr, msg), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_do_sea,
	TP_PROTO(struct pt_regs *regs, unsigned int esr, unsigned long addr, const char *msg),
	TP_ARGS(regs, esr, addr, msg), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_do_mem_abort,
	TP_PROTO(struct pt_regs *regs, unsigned int esr, unsigned long addr, const char *msg),
	TP_ARGS(regs, esr, addr, msg), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_do_sp_pc_abort,
	TP_PROTO(struct pt_regs *regs, unsigned int esr, unsigned long addr, bool user),
	TP_ARGS(regs, esr, addr, user),
	TP_CONDITION(!user));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_FAULT_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
