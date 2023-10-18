/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __GH_MEM_NOTIFIER_H
#define __GH_MEM_NOTIFIER_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/types.h>

enum gh_mem_notifier_tag {
	GH_MEM_NOTIFIER_TAG_DISPLAY,
	GH_MEM_NOTIFIER_TAG_TOUCH_PRIMARY,
	GH_MEM_NOTIFIER_TAG_TOUCH_SECONDARY,
	GH_MEM_NOTIFIER_TAG_TLMM,
	GH_MEM_NOTIFIER_TAG_TEST_TLMM,
	GH_MEM_NOTIFIER_TAG_TEST_TUIVM,
	GH_MEM_NOTIFIER_TAG_TEST_OEMVM,
	GH_MEM_NOTIFIER_TAG_EVA,
	GH_MEM_NOTIFIER_TAG_CAM_BASE,
	GH_MEM_NOTIFIER_TAG_CAM_MAX = GH_MEM_NOTIFIER_TAG_CAM_BASE + 49,
	GH_MEM_NOTIFIER_TAG_MAX
};

typedef void (*gh_mem_notifier_handler)(enum gh_mem_notifier_tag tag,
					unsigned long notif_type,
					void *entry_data, void *notif_msg);

#if IS_ENABLED(CONFIG_GH_MEM_NOTIFIER)
void *gh_mem_notifier_register(enum gh_mem_notifier_tag tag,
			       gh_mem_notifier_handler notif_handler,
			       void *data);
void gh_mem_notifier_unregister(void *cookie);
#else
static void __maybe_unused *gh_mem_notifier_register(enum gh_mem_notifier_tag tag,
				      gh_mem_notifier_handler notif_handler,
				      void *data)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static void __maybe_unused gh_mem_notifier_unregister(void *cookie)
{
}
#endif
#endif
