/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fault
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FAULT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FAULT_H
#include <trace/hooks/vendor_hooks.h>

struct pt_regs;
DECLARE_RESTRICTED_HOOK(android_rvh_die_kernel_fault,
	TP_PROTO(const char *msg, unsigned long addr, unsigned long esr, struct pt_regs *regs),
	TP_ARGS(msg, addr, esr, regs), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_do_sea,
	TP_PROTO(unsigned long addr, unsigned long esr, struct pt_regs *regs),
	TP_ARGS(addr, esr, regs), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_do_sp_pc_abort,
	TP_PROTO(unsigned long addr, unsigned long esr, struct pt_regs *regs),
	TP_ARGS(addr, esr, regs),
	TP_CONDITION(!user_mode(regs)));

#endif /* _TRACE_HOOK_FAULT_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
