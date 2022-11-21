// SPDX-License-Identifier: GPL-2.0
/*  GPLv2, Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc. */
#include "xdp_sample.bpf.h"

#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

array_map rx_cnt SEC(".maps");
array_map redir_err_cnt SEC(".maps");
array_map cpumap_enqueue_cnt SEC(".maps");
array_map cpumap_kthread_cnt SEC(".maps");
array_map exception_cnt SEC(".maps");
array_map devmap_xmit_cnt SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 32 * 32);
	__type(key, u64);
	__type(value, struct datarec);
} devmap_xmit_cnt_multi SEC(".maps");

const volatile int nr_cpus = 0;

/* These can be set before loading so that redundant comparisons can be DCE'd by
 * the verifier, and only actual matches are tried after loading tp_btf program.
 * This allows sample to filter tracepoint stats based on net_device.
 */
const volatile int from_match[32] = {};
const volatile int to_match[32] = {};

int cpumap_map_id = 0;

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

SEC("tp_btf/xdp_cpumap_enqueue")
int BPF_PROG(tp_xdp_cpumap_enqueue, int map_id, unsigned int processed,
	     unsigned int drops, int to_cpu)
{
	u32 cpu = bpf_get_smp_processor_id();
	struct datarec *rec;
	u32 idx;

	if (cpumap_map_id && cpumap_map_id != map_id)
		return 0;

	idx = to_cpu * nr_cpus + cpu;
	rec = bpf_map_lookup_elem(&cpumap_enqueue_cnt, &idx);
	if (!rec)
		return 0;
	NO_TEAR_ADD(rec->processed, processed);
	NO_TEAR_ADD(rec->dropped, drops);
	/* Record bulk events, then userspace can calc average bulk size */
	if (processed > 0)
		NO_TEAR_INC(rec->issue);
	/* Inception: It's possible to detect overload situations, via
	 * this tracepoint.  This can be used for creating a feedback
	 * loop to XDP, which can take appropriate actions to mitigate
	 * this overload situation.
	 */
	return 0;
}

SEC("tp_btf/xdp_cpumap_kthread")
int BPF_PROG(tp_xdp_cpumap_kthread, int map_id, unsigned int processed,
	     unsigned int drops, int sched, struct xdp_cpumap_stats *xdp_stats)
{
	struct datarec *rec;
	u32 cpu;

	if (cpumap_map_id && cpumap_map_id != map_id)
		return 0;

	cpu = bpf_get_smp_processor_id();
	rec = bpf_map_lookup_elem(&cpumap_kthread_cnt, &cpu);
	if (!rec)
		return 0;
	NO_TEAR_ADD(rec->processed, processed);
	NO_TEAR_ADD(rec->dropped, drops);
	NO_TEAR_ADD(rec->xdp_pass, xdp_stats->pass);
	NO_TEAR_ADD(rec->xdp_drop, xdp_stats->drop);
	NO_TEAR_ADD(rec->xdp_redirect, xdp_stats->redirect);
	/* Count times kthread yielded CPU via schedule call */
	if (sched)
		NO_TEAR_INC(rec->issue);
	return 0;
}

SEC("tp_btf/xdp_exception")
int BPF_PROG(tp_xdp_exception, const struct net_device *dev,
	     const struct bpf_prog *xdp, u32 act)
{
	u32 cpu = bpf_get_smp_processor_id();
	struct datarec *rec;
	u32 key = act, idx;

	if (!IN_SET(from_match, dev->ifindex))
		return 0;
	if (!IN_SET(to_match, dev->ifindex))
		return 0;

	if (key > XDP_REDIRECT)
		key = XDP_REDIRECT + 1;

	idx = key * nr_cpus + cpu;
	rec = bpf_map_lookup_elem(&exception_cnt, &idx);
	if (!rec)
		return 0;
	NO_TEAR_INC(rec->dropped);

	return 0;
}

SEC("tp_btf/xdp_devmap_xmit")
int BPF_PROG(tp_xdp_devmap_xmit, const struct net_device *from_dev,
	     const struct net_device *to_dev, int sent, int drops, int err)
{
	struct datarec *rec;
	int idx_in, idx_out;
	u32 cpu;

	idx_in = from_dev->ifindex;
	idx_out = to_dev->ifindex;

	if (!IN_SET(from_match, idx_in))
		return 0;
	if (!IN_SET(to_match, idx_out))
		return 0;

	cpu = bpf_get_smp_processor_id();
	rec = bpf_map_lookup_elem(&devmap_xmit_cnt, &cpu);
	if (!rec)
		return 0;
	NO_TEAR_ADD(rec->processed, sent);
	NO_TEAR_ADD(rec->dropped, drops);
	/* Record bulk events, then userspace can calc average bulk size */
	NO_TEAR_INC(rec->info);
	/* Record error cases, where no frame were sent */
	/* Catch API error of drv ndo_xdp_xmit sent more than count */
	if (err || drops < 0)
		NO_TEAR_INC(rec->issue);
	return 0;
}

SEC("tp_btf/xdp_devmap_xmit")
int BPF_PROG(tp_xdp_devmap_xmit_multi, const struct net_device *from_dev,
	     const struct net_device *to_dev, int sent, int drops, int err)
{
	struct datarec empty = {};
	struct datarec *rec;
	int idx_in, idx_out;
	u64 idx;

	idx_in = from_dev->ifindex;
	idx_out = to_dev->ifindex;
	idx = idx_in;
	idx = idx << 32 | idx_out;

	if (!IN_SET(from_match, idx_in))
		return 0;
	if (!IN_SET(to_match, idx_out))
		return 0;

	bpf_map_update_elem(&devmap_xmit_cnt_multi, &idx, &empty, BPF_NOEXIST);
	rec = bpf_map_lookup_elem(&devmap_xmit_cnt_multi, &idx);
	if (!rec)
		return 0;

	NO_TEAR_ADD(rec->processed, sent);
	NO_TEAR_ADD(rec->dropped, drops);
	NO_TEAR_INC(rec->info);
	if (err || drops < 0)
		NO_TEAR_INC(rec->issue);
	return 0;
}
