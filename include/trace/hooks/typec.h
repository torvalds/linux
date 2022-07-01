/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM typec
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_TYPEC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_TYPEC_H
#include <linux/tracepoint.h>
#include <linux/usb/pd.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#ifdef __GENKSYMS__
struct tcpci_data;
#else
/* struct tcpci_data */
#include <../drivers/usb/typec/tcpm/tcpci.h>
#endif /* __GENKSYMS__ */
struct tcpci;
struct tcpm_port;

#ifndef TYPEC_TIMER
#define TYPEC_TIMER
enum typec_timer {
	SINK_WAIT_CAP,
	SOURCE_OFF,
	CC_DEBOUNCE,
	SINK_DISCOVERY_BC12,
};
#endif

DECLARE_HOOK(android_vh_typec_tcpci_override_toggling,
	TP_PROTO(struct tcpci *tcpci, struct tcpci_data *data, int *override_toggling),
	TP_ARGS(tcpci, data, override_toggling));

DECLARE_RESTRICTED_HOOK(android_rvh_typec_tcpci_chk_contaminant,
	TP_PROTO(struct tcpci *tcpci, struct tcpci_data *data, int *ret),
	TP_ARGS(tcpci, data, ret), 1);

/*
 * This hook is for addressing hardware anomalies where TCPC_POWER_STATUS_VBUS_PRES bit can return 0
 * even before falling below sSinkDisconnect threshold.
 * Handler has to set bypass to override the value that would otherwise be returned by this
 * function.
 * Handler can set vbus or clear vbus to indicate vbus present or absent
 */
DECLARE_RESTRICTED_HOOK(android_rvh_typec_tcpci_get_vbus,
	TP_PROTO(struct tcpci *tcpci, struct tcpci_data *data, int *vbus, int *bypass),
	TP_ARGS(tcpci, data, vbus, bypass), 1);

DECLARE_HOOK(android_vh_typec_tcpm_get_timer,
	TP_PROTO(const char *state, enum typec_timer timer, unsigned int *msecs),
	TP_ARGS(state, timer, msecs));

DECLARE_HOOK(android_vh_typec_store_partner_src_caps,
	TP_PROTO(struct tcpm_port *port, unsigned int *nr_source_caps,
		 u32 (*source_caps)[PDO_MAX_OBJECTS]),
	TP_ARGS(port, nr_source_caps, source_caps));

DECLARE_HOOK(android_vh_typec_tcpm_adj_current_limit,
	TP_PROTO(const char *state, u32 port_current_limit, u32 port_voltage, bool pd_capable,
		  u32 *current_limit, bool *adjust),
	TP_ARGS(state, port_current_limit, port_voltage, pd_capable, current_limit, adjust));

DECLARE_HOOK(android_vh_typec_tcpm_log,
	TP_PROTO(const char *log, bool *bypass),
	TP_ARGS(log, bypass));

#endif /* _TRACE_HOOK_UFSHCD_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
