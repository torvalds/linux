// SPDX-License-Identifier: GPL-2.0
/*  GPLv2, Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc. */
#include "xdp_sample.bpf.h"

#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

array_map rx_cnt SEC(".maps");
array_map redir_err_cnt SEC(".maps");

const volatile int nr_cpus = 0;

/* These can be set before loading so that redundant comparisons can be DCE'd by
 * the verifier, and only actual matches are tried after loading tp_btf program.
 * This allows sample to filter tracepoint stats based on net_device.
 */
const volatile int from_match[32] = {};
const volatile int to_match[32] = {};

/* Find if b is part of set a, but if a is empty set then evaluate to true */
#define IN_SET(a, b)                                                 \
	({                                                           \
		bool __res = !(a)[0];                                \
		for (int i = 0; i < ARRAY_SIZE(a) && (a)[i]; i++) { \
			__res = (a)[i] == (b);                       \
			if (__res)                                   \
				break;                               \
		}                                                    \
		__res;                                               \
	})

static __always_inline __u32 xdp_get_err_key(int err)
{
	switch (err) {
	case 0:
		return 0;
	case -EINVAL:
		return 2;
	case -ENETDOWN:
		return 3;
	case -EMSGSIZE:
		return 4;
	case -EOPNOTSUPP:
		return 5;
	case -ENOSPC:
		return 6;
	default:
		return 1;
	}
}

static __always_inline int xdp_redirect_collect_stat(int from, int err)
{
	u32 cpu = bpf_get_smp_processor_id();
	u32 key = XDP_REDIRECT_ERROR;
	struct datarec *rec;
	u32 idx;

	if (!IN_SET(from_match, from))
		return 0;

	key = xdp_get_err_key(err);

	idx = key * nr_cpus + cpu;
	rec = bpf_map_lookup_elem(&redir_err_cnt, &idx);
	if (!rec)
		return 0;
	if (key)
		NO_TEAR_INC(rec->dropped);
	else
		NO_TEAR_INC(rec->processed);
	return 0; /* Indicate event was filtered (no further processing)*/
	/*
	 * Returning 1 here would allow e.g. a perf-record tracepoint
	 * to see and record these events, but it doesn't work well
	 * in-practice as stopping perf-record also unload this
	 * bpf_prog.  Plus, there is additional overhead of doing so.
	 */
}

SEC("tp_btf/xdp_redirect_err")
int BPF_PROG(tp_xdp_redirect_err, const struct net_device *dev,
	     const struct bpf_prog *xdp, const void *tgt, int err,
	     const struct bpf_map *map, u32 index)
{
	return xdp_redirect_collect_stat(dev->ifindex, err);
}

SEC("tp_btf/xdp_redirect_map_err")
int BPF_PROG(tp_xdp_redirect_map_err, const struct net_device *dev,
	     const struct bpf_prog *xdp, const void *tgt, int err,
	     const struct bpf_map *map, u32 index)
{
	return xdp_redirect_collect_stat(dev->ifindex, err);
}

SEC("tp_btf/xdp_redirect")
int BPF_PROG(tp_xdp_redirect, const struct net_device *dev,
	     const struct bpf_prog *xdp, const void *tgt, int err,
	     const struct bpf_map *map, u32 index)
{
	return xdp_redirect_collect_stat(dev->ifindex, err);
}

SEC("tp_btf/xdp_redirect_map")
int BPF_PROG(tp_xdp_redirect_map, const struct net_device *dev,
	     const struct bpf_prog *xdp, const void *tgt, int err,
	     const struct bpf_map *map, u32 index)
{
	return xdp_redirect_collect_stat(dev->ifindex, err);
}
