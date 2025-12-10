/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM pci

#if !defined(_TRACE_HW_EVENT_PCI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HW_EVENT_PCI_H

#include <uapi/linux/pci_regs.h>
#include <linux/tracepoint.h>

#define PCI_HOTPLUG_EVENT						\
	EM(PCI_HOTPLUG_LINK_UP,			"LINK_UP")		\
	EM(PCI_HOTPLUG_LINK_DOWN,		"LINK_DOWN")		\
	EM(PCI_HOTPLUG_CARD_PRESENT,		"CARD_PRESENT")		\
	EMe(PCI_HOTPLUG_CARD_NOT_PRESENT,	"CARD_NOT_PRESENT")

/* Enums require being exported to userspace, for user tool parsing */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

PCI_HOTPLUG_EVENT

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}

/*
 * Note: For generic PCI hotplug events, we pass already-resolved strings
 * (port_name, slot) instead of driver-specific structures like 'struct
 * controller'.  This is because different PCI hotplug drivers (pciehp, cpqphp,
 * ibmphp, shpchp) define their own versions of 'struct controller' with
 * different fields and helper functions. Using driver-specific structures would
 * make the tracepoint interface non-generic and cause compatibility issues
 * across different drivers.
 */
TRACE_EVENT(pci_hp_event,

	TP_PROTO(const char *port_name,
		 const char *slot,
		 const int event),

	TP_ARGS(port_name, slot, event),

	TP_STRUCT__entry(
		__string(	port_name,	port_name	)
		__string(	slot,		slot		)
		__field(	int,		event	)
	),

	TP_fast_assign(
		__assign_str(port_name);
		__assign_str(slot);
		__entry->event = event;
	),

	TP_printk("%s slot:%s, event:%s\n",
		__get_str(port_name),
		__get_str(slot),
		__print_symbolic(__entry->event, PCI_HOTPLUG_EVENT)
	)
);

#define PCI_EXP_LNKSTA_LINK_STATUS_MASK (PCI_EXP_LNKSTA_LBMS | \
					 PCI_EXP_LNKSTA_LABS | \
					 PCI_EXP_LNKSTA_LT | \
					 PCI_EXP_LNKSTA_DLLLA)

#define LNKSTA_FLAGS					\
	{ PCI_EXP_LNKSTA_LT,	"LT"},			\
	{ PCI_EXP_LNKSTA_DLLLA,	"DLLLA"},		\
	{ PCI_EXP_LNKSTA_LBMS,	"LBMS"},		\
	{ PCI_EXP_LNKSTA_LABS,	"LABS"}

TRACE_EVENT(pcie_link_event,

	TP_PROTO(struct pci_bus *bus,
		  unsigned int reason,
		  unsigned int width,
		  unsigned int status
		),

	TP_ARGS(bus, reason, width, status),

	TP_STRUCT__entry(
		__string(	port_name,	pci_name(bus->self))
		__field(	unsigned int,	type		)
		__field(	unsigned int,	reason		)
		__field(	unsigned int,	cur_bus_speed	)
		__field(	unsigned int,	max_bus_speed	)
		__field(	unsigned int,	width		)
		__field(	unsigned int,	flit_mode	)
		__field(	unsigned int,	link_status	)
	),

	TP_fast_assign(
		__assign_str(port_name);
		__entry->type			= pci_pcie_type(bus->self);
		__entry->reason			= reason;
		__entry->cur_bus_speed		= bus->cur_bus_speed;
		__entry->max_bus_speed		= bus->max_bus_speed;
		__entry->width			= width;
		__entry->flit_mode		= bus->flit_mode;
		__entry->link_status		= status;
	),

	TP_printk("%s type:%d, reason:%d, cur_bus_speed:%d, max_bus_speed:%d, width:%u, flit_mode:%u, status:%s\n",
		__get_str(port_name),
		__entry->type,
		__entry->reason,
		__entry->cur_bus_speed,
		__entry->max_bus_speed,
		__entry->width,
		__entry->flit_mode,
		__print_flags((unsigned long)__entry->link_status, "|",
				LNKSTA_FLAGS)
	)
);

#endif /* _TRACE_HW_EVENT_PCI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
