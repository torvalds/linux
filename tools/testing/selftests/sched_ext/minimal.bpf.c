/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A completely minimal scheduler.
 *
 * This scheduler defines the absolute minimal set of struct sched_ext_ops
 * fields: its name. It should _not_ fail to be loaded, and can be used to
 * exercise the default scheduling paths in ext.c.
 *
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

SEC(".struct_ops.link")
struct sched_ext_ops minimal_ops = {
	.name			= "minimal",
};
