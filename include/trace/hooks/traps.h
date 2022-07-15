/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM traps
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_TRAPS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_TRAPS_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#ifdef __GENKSYMS__
struct pt_regs;
#else
/* struct pt_regs */
#include <asm/ptrace.h>
#endif /* __GENKSYMS__ */
DECLARE_RESTRICTED_HOOK(android_rvh_do_undefinstr,
	TP_PROTO(struct pt_regs *regs, bool user),
	TP_ARGS(regs, user),
	TP_CONDITION(!user));

DECLARE_RESTRICTED_HOOK(android_rvh_do_ptrauth_fault,
	TP_PROTO(struct pt_regs *regs, unsigned int esr, bool user),
	TP_ARGS(regs, esr, user),
	TP_CONDITION(!user));

DECLARE_RESTRICTED_HOOK(android_rvh_bad_mode,
	TP_PROTO(struct pt_regs *regs, unsigned int esr, int reason),
	TP_ARGS(regs, reason, esr), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_arm64_serror_panic,
	TP_PROTO(struct pt_regs *regs, unsigned int esr),
	TP_ARGS(regs, esr), 1);

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_TRAPS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
