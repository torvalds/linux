/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020, Red Hat, Inc.
 */
#include "kvm_util.h"

struct guest_mode {
	bool supported;
	bool enabled;
};

extern struct guest_mode guest_modes[NUM_VM_MODES];

#define guest_mode_append(mode, enabled) ({ \
	guest_modes[mode] = (struct guest_mode){ (enabled), (enabled) }; \
})

void guest_modes_append_default(void);
void for_each_guest_mode(void (*func)(enum vm_guest_mode, void *), void *arg);
void guest_modes_help(void);
void guest_modes_cmdline(const char *arg);
