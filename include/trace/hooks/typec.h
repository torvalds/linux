/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM typec
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_TYPEC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_TYPEC_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct tcpci;
struct tcpci_data;

DECLARE_HOOK(android_vh_typec_tcpci_override_toggling,
	TP_PROTO(struct tcpci *tcpci, struct tcpci_data *data, int *override_toggling),
	TP_ARGS(tcpci, data, override_toggling));

DECLARE_HOOK(android_vh_typec_tcpci_check_contaminant,
	TP_PROTO(struct tcpci *tcpci, struct tcpci_data *data, int *ret),
	TP_ARGS(tcpci, data, ret));
#endif /* _TRACE_HOOK_UFSHCD_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
