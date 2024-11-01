/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2015 Jiri Pirko <jiri@resnulli.us>
 */

#ifndef __NET_TC_BPF_H
#define __NET_TC_BPF_H

#include <linux/filter.h>
#include <net/act_api.h>

struct tcf_bpf {
	struct tc_action	common;
	struct bpf_prog __rcu	*filter;
	union {
		u32		bpf_fd;
		u16		bpf_num_ops;
	};
	struct sock_filter	*bpf_ops;
	const char		*bpf_name;
};
#define to_bpf(a) ((struct tcf_bpf *)a)

#endif /* __NET_TC_BPF_H */
