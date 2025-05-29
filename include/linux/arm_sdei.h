// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2017 Arm Ltd.
#ifndef __LINUX_ARM_SDEI_H
#define __LINUX_ARM_SDEI_H

#include <uapi/linux/arm_sdei.h>

#include <acpi/ghes.h>

#ifdef CONFIG_ARM_SDE_INTERFACE
#include <asm/sdei.h>
#endif

/* Arch code should override this to set the entry point from firmware... */
#ifndef sdei_arch_get_entry_point
#define sdei_arch_get_entry_point(conduit)	(0)
#endif

/*
 * When an event occurs sdei_event_handler() will call a user-provided callback
 * like this in NMI context on the CPU that received the event.
 */
typedef int (sdei_event_callback)(u32 event, struct pt_regs *regs, void *arg);

/*
 * Register your callback to claim an event. The event must be described
 * by firmware.
 */
int sdei_event_register(u32 event_num, sdei_event_callback *cb, void *arg);

/*
 * Calls to sdei_event_unregister() may return EINPROGRESS. Keep calling
 * it until it succeeds.
 */
int sdei_event_unregister(u32 event_num);

int sdei_event_enable(u32 event_num);
int sdei_event_disable(u32 event_num);

/* GHES register/unregister helpers */
int sdei_register_ghes(struct ghes *ghes, sdei_event_callback *normal_cb,
		       sdei_event_callback *critical_cb);
int sdei_unregister_ghes(struct ghes *ghes);

#ifdef CONFIG_ARM_SDE_INTERFACE
/* For use by arch code when CPU hotplug notifiers are not appropriate. */
int sdei_mask_local_cpu(void);
int sdei_unmask_local_cpu(void);
void __init acpi_sdei_init(void);
void sdei_handler_abort(void);
#else
static inline int sdei_mask_local_cpu(void) { return 0; }
static inline int sdei_unmask_local_cpu(void) { return 0; }
static inline void acpi_sdei_init(void) { }
static inline void sdei_handler_abort(void) { }
#endif /* CONFIG_ARM_SDE_INTERFACE */


/*
 * This struct represents an event that has been registered. The driver
 * maintains a list of all events, and which ones are registered. (Private
 * events have one entry in the list, but are registered on each CPU).
 * A pointer to this struct is passed to firmware, and back to the event
 * handler. The event handler can then use this to invoke the registered
 * callback, without having to walk the list.
 *
 * For CPU private events, this structure is per-cpu.
 */
struct sdei_registered_event {
	/* For use by arch code: */
	struct pt_regs          interrupted_regs;

	sdei_event_callback	*callback;
	void			*callback_arg;
	u32			 event_num;
	u8			 priority;
};

/* The arch code entry point should then call this when an event arrives. */
int notrace sdei_event_handler(struct pt_regs *regs,
			       struct sdei_registered_event *arg);

/* arch code may use this to retrieve the extra registers. */
int sdei_api_event_context(u32 query, u64 *result);

#endif /* __LINUX_ARM_SDEI_H */
