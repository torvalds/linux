/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *
 */

#ifndef __GH_IRQ_LEND_H
#define __GH_IRQ_LEND_H

#include <linux/types.h>

#include "gh_common.h"
#include "gh_rm_drv.h"

enum gh_irq_label {
	GH_IRQ_LABEL_SDE,
	GH_IRQ_LABEL_TRUSTED_TOUCH_PRIMARY,
	GH_IRQ_LABEL_TRUSTED_TOUCH_SECONDARY,
	GH_IRQ_LABEL_TEST_TUIVM,
	GH_IRQ_LABEL_TEST_OEMVM,
	GH_IRQ_LABEL_MAX
};


typedef void (*gh_irq_handle_fn)(void *req, enum gh_irq_label label);
typedef void (*gh_irq_handle_fn_v2)(void *req, unsigned long notif_type,
						enum gh_irq_label label);

#if IS_ENABLED(CONFIG_GH_IRQ_LEND)
int gh_irq_lend(enum gh_irq_label label, enum gh_vm_names name,
		int hw_irq, gh_irq_handle_fn cb_handle, void *data);
int gh_irq_lend_v2(enum gh_irq_label label, enum gh_vm_names name,
		int hw_irq, gh_irq_handle_fn_v2 cb_handle, void *data);
int gh_irq_lend_notify(enum gh_irq_label label);
int gh_irq_reclaim(enum gh_irq_label label);
int gh_irq_wait_for_lend(enum gh_irq_label label, enum gh_vm_names name,
			 gh_irq_handle_fn on_lend, void *data);
int gh_irq_wait_for_lend_v2(enum gh_irq_label label, enum gh_vm_names name,
			 gh_irq_handle_fn_v2 on_lend, void *data);
int gh_irq_accept(enum gh_irq_label label, int irq, int type);
int gh_irq_accept_notify(enum gh_irq_label label);
int gh_irq_release(enum gh_irq_label label);
int gh_irq_release_notify(enum gh_irq_label label);
#else

int gh_irq_lend(enum gh_irq_label label, enum gh_vm_names name,
		int hw_irq, gh_irq_handle_fn cb_handle, void *data)
{
	return -EINVAL;
}

int gh_irq_lend_v2(enum gh_irq_label label, enum gh_vm_names name,
		int hw_irq, gh_irq_handle_fn_v2 cb_handle, void *data)
{
	return -EINVAL;
}

int gh_irq_lend_notify(enum gh_irq_label label)
{
	return -EINVAL;
}

int gh_irq_reclaim(enum gh_irq_label label)
{
	return -EINVAL;
}

int gh_irq_wait_for_lend(enum gh_irq_label label, enum gh_vm_names name,
			 gh_irq_handle_fn on_lend, void *data)
{
	return -EINVAL;
}

int gh_irq_wait_for_lend_v2(enum gh_irq_label label, enum gh_vm_names name,
			 gh_irq_handle_fn_v2 on_lend, void *data)
{
	return -EINVAL;
}

int gh_irq_accept(enum gh_irq_label label, int irq, int type)
{
	return -EINVAL;
}

int gh_irq_accept_notify(enum gh_irq_label label)
{
	return -EINVAL;
}

int gh_irq_release(enum gh_irq_label label)
{
	return -EINVAL;
}

int gh_irq_release_notify(enum gh_irq_label label)
{
	return -EINVAL;
}

#endif

#endif
