// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/sched.h>

#include <trace/hooks/sched.h>

#include "walt.h"
#include "walt_debug.h"

static void android_rvh_schedule_bug(void *unused, void *unused2)
{
	BUG();
}

static int __init walt_debug_init(void)
{
	int ret;

	ret = preemptirq_long_init();
	if (ret)
		return ret;

	register_trace_android_rvh_schedule_bug(android_rvh_schedule_bug, NULL);

	return 0;
}
module_init(walt_debug_init);

MODULE_DESCRIPTION("QTI WALT Debug Module");
MODULE_LICENSE("GPL v2");
