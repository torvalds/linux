// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";
struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key, int);
	__type(value, int);
} perf_buf_map SEC(".maps");

#define _(P) (__builtin_preserve_access_index(P))

/* define few struct-s that bpf program needs to access */
struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
};
struct dev_ifalias {
	struct callback_head rcuhead;
};

struct net_device /* same as kernel's struct net_device */ {
	int ifindex;
	struct dev_ifalias *ifalias;
};

typedef struct {
        int counter;
} atomic_t;
typedef struct refcount_struct {
        atomic_t refs;
} refcount_t;

struct sk_buff {
	/* field names and sizes should match to those in the kernel */
	unsigned int len, data_len;
	__u16 mac_len, hdr_len, queue_mapping;
	struct net_device *dev;
	/* order of the fields doesn't matter */
	refcount_t users;
	unsigned char *data;
	char __pkt_type_offset[0];
	char cb[48];
};

struct meta {
	int ifindex;
	__u32 cb32_0;
	__u8 cb8_0;
};

/* TRACE_EVENT(kfree_skb,
 *         TP_PROTO(struct sk_buff *skb, void *location),
 */
SEC("tp_btf/kfree_skb")
int BPF_PROG(trace_kfree_skb, struct sk_buff *skb, void *location)
{
	struct net_device *dev;
	struct callback_head *ptr;
	void *func;
	int users;
	unsigned char *data;
	unsigned short pkt_data;
	struct meta meta = {};
	char pkt_type;
	__u32 *cb32;
	__u8 *cb8;

	__builtin_preserve_access_index(({
		users = skb->users.refs.counter;
		data = skb->data;
		dev = skb->dev;
		ptr = dev->ifalias->rcuhead.next;
		func = ptr->func;
		cb8 = (__u8 *)&skb->cb;
		cb32 = (__u32 *)&skb->cb;
	}));

	meta.ifindex = _(dev->ifindex);
	meta.cb8_0 = cb8[8];
	meta.cb32_0 = cb32[2];

	bpf_probe_read_kernel(&pkt_type, sizeof(pkt_type), _(&skb->__pkt_type_offset));
	pkt_type &= 7;

	/* read eth proto */
	bpf_probe_read_kernel(&pkt_data, sizeof(pkt_data), data + 12);

	bpf_printk("rcuhead.next %llx func %llx\n", ptr, func);
	bpf_printk("skb->len %d users %d pkt_type %x\n",
		   _(skb->len), users, pkt_type);
	bpf_printk("skb->queue_mapping %d\n", _(skb->queue_mapping));
	bpf_printk("dev->ifindex %d data %llx pkt_data %x\n",
		   meta.ifindex, data, pkt_data);
	bpf_printk("cb8_0:%x cb32_0:%x\n", meta.cb8_0, meta.cb32_0);

	if (users != 1 || pkt_data != bpf_htons(0x86dd) || meta.ifindex != 1)
		/* raw tp ignores return value */
		return 0;

	/* send first 72 byte of the packet to user space */
	bpf_skb_output(skb, &perf_buf_map, (72ull << 32) | BPF_F_CURRENT_CPU,
		       &meta, sizeof(meta));
	return 0;
}

struct {
	bool fentry_test_ok;
	bool fexit_test_ok;
} result = {};

SEC("fentry/eth_type_trans")
int BPF_PROG(fentry_eth_type_trans, struct sk_buff *skb, struct net_device *dev,
	     unsigned short protocol)
{
	int len, ifindex;

	__builtin_preserve_access_index(({
		len = skb->len;
		ifindex = dev->ifindex;
	}));

	/* fentry sees full packet including L2 header */
	if (len != 74 || ifindex != 1)
		return 0;
	result.fentry_test_ok = true;
	return 0;
}

SEC("fexit/eth_type_trans")
int BPF_PROG(fexit_eth_type_trans, struct sk_buff *skb, struct net_device *dev,
	     unsigned short protocol)
{
	int len, ifindex;

	__builtin_preserve_access_index(({
		len = skb->len;
		ifindex = dev->ifindex;
	}));

	/* fexit sees packet without L2 header that eth_type_trans should have
	 * consumed.
	 */
	if (len != 60 || protocol != bpf_htons(0x86dd) || ifindex != 1)
		return 0;
	result.fexit_test_ok = true;
	return 0;
}
