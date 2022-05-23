/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GH_VM_H
#define _GH_VM_H

int gh_register_vm_notifier(struct notifier_block *nb);
int gh_unregister_vm_notifier(struct notifier_block *nb);

/* VM power notifications */
#define GH_VM_BEFORE_POWERUP		0x1
#define GH_VM_POWERUP_FAIL		0x2
#define GH_VM_EARLY_POWEROFF		0x3
#define GH_VM_POWEROFF			0x4

#endif /* _GH_VM_H */
