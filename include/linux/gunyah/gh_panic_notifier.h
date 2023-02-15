/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __GH_PANIC_NOTIFIER_H
#define __GH_PANIC_NOTIFIER_H

#if IS_ENABLED(CONFIG_GH_PANIC_NOTIFIER)
int gh_panic_notifier_register(struct notifier_block *nb);
int gh_panic_notifier_unregister(struct notifier_block *nb);
#else
static inline int gh_panic_notifier_register(struct notifier_block *nb)
{
	return 0;
}
static inline int gh_panic_notifier_unregister(struct notifier_block *nb)
{
	return 0;
}
#endif

#endif
