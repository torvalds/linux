/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GH_VM_H
#define _GH_VM_H

/* VM power notifications */
#define GH_VM_BEFORE_POWERUP		0x1
#define GH_VM_POWERUP_FAIL		0x2
#define GH_VM_EARLY_POWEROFF		0x3
#define GH_VM_POWEROFF			0x4

#if IS_ENABLED(CONFIG_GH_SECURE_VM_LOADER)
int gh_register_vm_notifier(struct notifier_block *nb);
int gh_unregister_vm_notifier(struct notifier_block *nb);
#else
static inline int gh_register_vm_notifier(struct notifier_block *nb)
{
	return -EINVAL;
}
static inline int gh_unregister_vm_notifier(struct notifier_block *nb)
{
	return -EINVAL;
}
#endif

#if IS_ENABLED(CONFIG_GH_PROXY_SCHED)
int gh_poll_vcpu_run(gh_vmid_t vmid);
#else
static inline int gh_poll_vcpu_run(gh_vmid_t vmid)
{
	return 0;
}
#endif

#endif /* _GH_VM_H */
