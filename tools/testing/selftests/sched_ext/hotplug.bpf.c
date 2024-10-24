/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

#include "hotplug_test.h"

UEI_DEFINE(uei);

void BPF_STRUCT_OPS(hotplug_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

static void exit_from_hotplug(s32 cpu, bool onlining)
{
	/*
	 * Ignored, just used to verify that we can invoke blocking kfuncs
	 * from the hotplug path.
	 */
	scx_bpf_create_dsq(0, -1);

	s64 code = SCX_ECODE_ACT_RESTART | HOTPLUG_EXIT_RSN;

	if (onlining)
		code |= HOTPLUG_ONLINING;

	scx_bpf_exit(code, "hotplug event detected (%d going %s)", cpu,
		     onlining ? "online" : "offline");
}

void BPF_STRUCT_OPS_SLEEPABLE(hotplug_cpu_online, s32 cpu)
{
	exit_from_hotplug(cpu, true);
}

void BPF_STRUCT_OPS_SLEEPABLE(hotplug_cpu_offline, s32 cpu)
{
	exit_from_hotplug(cpu, false);
}

SEC(".struct_ops.link")
struct sched_ext_ops hotplug_cb_ops = {
	.cpu_online		= (void *) hotplug_cpu_online,
	.cpu_offline		= (void *) hotplug_cpu_offline,
	.exit			= (void *) hotplug_exit,
	.name			= "hotplug_cbs",
	.timeout_ms		= 1000U,
};

SEC(".struct_ops.link")
struct sched_ext_ops hotplug_nocb_ops = {
	.exit			= (void *) hotplug_exit,
	.name			= "hotplug_nocbs",
	.timeout_ms		= 1000U,
};
