/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM pci_controller

#if !defined(_TRACE_HW_EVENT_PCI_CONTROLLER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HW_EVENT_PCI_CONTROLLER_H

#include <uapi/linux/pci_regs.h>
#include <linux/tracepoint.h>

#define RATE					\
	EM(PCIE_SPEED_2_5GT,  "2.5 GT/s")	\
	EM(PCIE_SPEED_5_0GT,  "5.0 GT/s")	\
	EM(PCIE_SPEED_8_0GT,  "8.0 GT/s")	\
	EM(PCIE_SPEED_16_0GT, "16.0 GT/s")	\
	EM(PCIE_SPEED_32_0GT, "32.0 GT/s")	\
	EM(PCIE_SPEED_64_0GT, "64.0 GT/s")	\
	EMe(PCI_SPEED_UNKNOWN, "Unknown")


#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

RATE

#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}

TRACE_EVENT(pcie_ltssm_state_transition,
	TP_PROTO(const char *dev_name, const char *state, u32 rate),
	TP_ARGS(dev_name, state, rate),

	TP_STRUCT__entry(
		__string(dev_name, dev_name)
		__string(state, state)
		__field(u32, rate)
	),

	TP_fast_assign(
		__assign_str(dev_name);
		__assign_str(state);
		__entry->rate = rate;
	),

	TP_printk("dev: %s state: %s rate: %s",
		__get_str(dev_name), __get_str(state),
		__print_symbolic(__entry->rate, RATE)
	)
);

#endif /* _TRACE_HW_EVENT_PCI_CONTROLLER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
