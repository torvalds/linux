/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * callback.h
 *
 * Register guest OS callbacks with Xen.
 *
 * Copyright (c) 2006, Ian Campbell
 */

#ifndef __XEN_PUBLIC_CALLBACK_H__
#define __XEN_PUBLIC_CALLBACK_H__

#include <xen/interface/xen.h>

/*
 * Prototype for this hypercall is:
 *   long callback_op(int cmd, void *extra_args)
 * @cmd        == CALLBACKOP_??? (callback operation).
 * @extra_args == Operation-specific extra arguments (NULL if none).
 */

/* x86: Callback for event delivery. */
#define CALLBACKTYPE_event                 0

/* x86: Failsafe callback when guest state cannot be restored by Xen. */
#define CALLBACKTYPE_failsafe              1

/* x86/64 hypervisor: Syscall by 64-bit guest app ('64-on-64-on-64'). */
#define CALLBACKTYPE_syscall               2

/*
 * x86/32 hypervisor: Only available on x86/32 when supervisor_mode_kernel
 *     feature is enabled. Do not use this callback type in new code.
 */
#define CALLBACKTYPE_sysenter_deprecated   3

/* x86: Callback for NMI delivery. */
#define CALLBACKTYPE_nmi                   4

/*
 * x86: sysenter is only available as follows:
 * - 32-bit hypervisor: with the supervisor_mode_kernel feature enabled
 * - 64-bit hypervisor: 32-bit guest applications on Intel CPUs
 *                      ('32-on-32-on-64', '32-on-64-on-64')
 *                      [nb. also 64-bit guest applications on Intel CPUs
 *                           ('64-on-64-on-64'), but syscall is preferred]
 */
#define CALLBACKTYPE_sysenter              5

/*
 * x86/64 hypervisor: Syscall by 32-bit guest app on AMD CPUs
 *                    ('32-on-32-on-64', '32-on-64-on-64')
 */
#define CALLBACKTYPE_syscall32             7

/*
 * Disable event deliver during callback? This flag is ignored for event and
 * NMI callbacks: event delivery is unconditionally disabled.
 */
#define _CALLBACKF_mask_events             0
#define CALLBACKF_mask_events              (1U << _CALLBACKF_mask_events)

/*
 * Register a callback.
 */
#define CALLBACKOP_register                0
struct callback_register {
	uint16_t type;
	uint16_t flags;
	xen_callback_t address;
};

/*
 * Unregister a callback.
 *
 * Not all callbacks can be unregistered. -EINVAL will be returned if
 * you attempt to unregister such a callback.
 */
#define CALLBACKOP_unregister              1
struct callback_unregister {
    uint16_t type;
    uint16_t _unused;
};

#endif /* __XEN_PUBLIC_CALLBACK_H__ */
