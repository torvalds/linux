/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF programs attached to network namespace
 */

#ifndef __NETNS_BPF_H__
#define __NETNS_BPF_H__

#include <linux/list.h>

struct bpf_prog;
struct bpf_prog_array;

enum netns_bpf_attach_type {
	NETNS_BPF_INVALID = -1,
	NETNS_BPF_FLOW_DISSECTOR = 0,
	NETNS_BPF_SK_LOOKUP,
	MAX_NETNS_BPF_ATTACH_TYPE
};

struct netns_bpf {
	/* Array of programs to run compiled from progs or links */
	struct bpf_prog_array __rcu *run_array[MAX_NETNS_BPF_ATTACH_TYPE];
	struct bpf_prog *progs[MAX_NETNS_BPF_ATTACH_TYPE];
	struct list_head links[MAX_NETNS_BPF_ATTACH_TYPE];
};

#endif /* __NETNS_BPF_H__ */
