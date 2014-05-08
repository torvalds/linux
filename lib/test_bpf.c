/*
 * Testsuite for BPF interpreter and BPF JIT compiler
 *
 * Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/filter.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>

#define MAX_SUBTESTS	3
#define MAX_DATA	128
#define MAX_INSNS	512
#define MAX_K		0xffffFFFF

/* define few constants used to init test 'skb' */
#define SKB_TYPE	3
#define SKB_MARK	0x1234aaaa
#define SKB_HASH	0x1234aaab
#define SKB_QUEUE_MAP	123
#define SKB_VLAN_TCI	0xffff
#define SKB_DEV_IFINDEX	577
#define SKB_DEV_TYPE	588

/* redefine REGs to make tests less verbose */
#define R0 BPF_REG_0
#define R1 BPF_REG_1
#define R2 BPF_REG_2
#define R3 BPF_REG_3
#define R4 BPF_REG_4
#define R5 BPF_REG_5
#define R6 BPF_REG_6
#define R7 BPF_REG_7
#define R8 BPF_REG_8
#define R9 BPF_REG_9
#define R10 BPF_REG_10

struct bpf_test {
	const char *descr;
	union {
		struct sock_filter insns[MAX_INSNS];
		struct sock_filter_int insns_int[MAX_INSNS];
	};
	enum {
		NO_DATA,
		EXPECTED_FAIL,
		SKB,
		SKB_INT
	} data_type;
	__u8 data[MAX_DATA];
	struct {
		int data_size;
		__u32 result;
	} test[MAX_SUBTESTS];
};

static struct bpf_test tests[] = {
	{
		"TAX",
		.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_ALU | BPF_NEG, 0), /* A == -3 */
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_LEN, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_MISC | BPF_TAX, 0), /* X == len - 3 */
			BPF_STMT(BPF_LD | BPF_B | BPF_IND, 1),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ 10, 20, 30, 40, 50 },
		{ { 2, 10 }, { 3, 20 }, { 4, 30 } },
	},
	{
		"tcpdump port 22",
		.insns = {
			{ 0x28,  0,  0, 0x0000000c },
			{ 0x15,  0,  8, 0x000086dd },
			{ 0x30,  0,  0, 0x00000014 },
			{ 0x15,  2,  0, 0x00000084 },
			{ 0x15,  1,  0, 0x00000006 },
			{ 0x15,  0, 17, 0x00000011 },
			{ 0x28,  0,  0, 0x00000036 },
			{ 0x15, 14,  0, 0x00000016 },
			{ 0x28,  0,  0, 0x00000038 },
			{ 0x15, 12, 13, 0x00000016 },
			{ 0x15,  0, 12, 0x00000800 },
			{ 0x30,  0,  0, 0x00000017 },
			{ 0x15,  2,  0, 0x00000084 },
			{ 0x15,  1,  0, 0x00000006 },
			{ 0x15,  0,  8, 0x00000011 },
			{ 0x28,  0,  0, 0x00000014 },
			{ 0x45,  6,  0, 0x00001fff },
			{ 0xb1,  0,  0, 0x0000000e },
			{ 0x48,  0,  0, 0x0000000e },
			{ 0x15,  2,  0, 0x00000016 },
			{ 0x48,  0,  0, 0x00000010 },
			{ 0x15,  0,  1, 0x00000016 },
			{ 0x06,  0,  0, 0x0000ffff },
			{ 0x06,  0,  0, 0x00000000 },
		},
		SKB,
		/* 3c:07:54:43:e5:76 > 10:bf:48:d6:43:d6, ethertype IPv4(0x0800)
		 * length 114: 10.1.1.149.49700 > 10.1.2.10.22: Flags [P.],
		 * seq 1305692979:1305693027, ack 3650467037, win 65535,
		 * options [nop,nop,TS val 2502645400 ecr 3971138], length 48
		 */
		{ 0x10, 0xbf, 0x48, 0xd6, 0x43, 0xd6,
		  0x3c, 0x07, 0x54, 0x43, 0xe5, 0x76,
		  0x08, 0x00,
		  0x45, 0x10, 0x00, 0x64, 0x75, 0xb5,
		  0x40, 0x00, 0x40, 0x06, 0xad, 0x2e, /* IP header */
		  0x0a, 0x01, 0x01, 0x95, /* ip src */
		  0x0a, 0x01, 0x02, 0x0a, /* ip dst */
		  0xc2, 0x24,
		  0x00, 0x16 /* dst port */ },
		{ { 10, 0 }, { 30, 0 }, { 100, 65535 } },
	},
	{
		"INT: DIV + ABS",
		.insns_int = {
			BPF_ALU64_REG(BPF_MOV, R6, R1),
			BPF_LD_ABS(BPF_B, 3),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU32_REG(BPF_DIV, R0, R2),
			BPF_ALU64_REG(BPF_MOV, R8, R0),
			BPF_LD_ABS(BPF_B, 4),
			BPF_ALU64_REG(BPF_ADD, R8, R0),
			BPF_LD_IND(BPF_B, R8, -70),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ 10, 20, 30, 40, 50 },
		{ { 4, 0 }, { 5, 10 } }
	},
	{
		"check: missing ret",
		.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 1),
		},
		EXPECTED_FAIL,
		{ },
		{ }
	},
};

static int get_length(struct sock_filter *fp)
{
	int len = 0;

	while (fp->code != 0 || fp->k != 0) {
		fp++;
		len++;
	}

	return len;
}

struct net_device dev;
struct sk_buff *populate_skb(char *buf, int size)
{
	struct sk_buff *skb;

	if (size >= MAX_DATA)
		return NULL;

	skb = alloc_skb(MAX_DATA, GFP_KERNEL);
	if (!skb)
		return NULL;

	memcpy(__skb_put(skb, size), buf, size);
	skb_reset_mac_header(skb);
	skb->protocol = htons(ETH_P_IP);
	skb->pkt_type = SKB_TYPE;
	skb->mark = SKB_MARK;
	skb->hash = SKB_HASH;
	skb->queue_mapping = SKB_QUEUE_MAP;
	skb->vlan_tci = SKB_VLAN_TCI;
	skb->dev = &dev;
	skb->dev->ifindex = SKB_DEV_IFINDEX;
	skb->dev->type = SKB_DEV_TYPE;
	skb_set_network_header(skb, min(size, ETH_HLEN));

	return skb;
}

static int run_one(struct sk_filter *fp, struct bpf_test *t)
{
	u64 start, finish, res, cnt = 100000;
	int err_cnt = 0, err, i, j;
	u32 ret = 0;
	void *data;

	for (i = 0; i < MAX_SUBTESTS; i++) {
		if (t->test[i].data_size == 0 &&
		    t->test[i].result == 0)
			break;
		if (t->data_type == SKB ||
		    t->data_type == SKB_INT) {
			data = populate_skb(t->data, t->test[i].data_size);
			if (!data)
				return -ENOMEM;
		} else {
			data = NULL;
		}

		start = ktime_to_us(ktime_get());
		for (j = 0; j < cnt; j++)
			ret = SK_RUN_FILTER(fp, data);
		finish = ktime_to_us(ktime_get());

		res = (finish - start) * 1000;
		do_div(res, cnt);

		err = ret != t->test[i].result;
		if (!err)
			pr_cont("%lld ", res);

		if (t->data_type == SKB || t->data_type == SKB_INT)
			kfree_skb(data);

		if (err) {
			pr_cont("ret %d != %d ", ret, t->test[i].result);
			err_cnt++;
		}
	}

	return err_cnt;
}

static __init int test_bpf(void)
{
	struct sk_filter *fp, *fp_ext = NULL;
	struct sock_fprog fprog;
	int err, i, err_cnt = 0;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		pr_info("#%d %s ", i, tests[i].descr);

		fprog.filter = tests[i].insns;
		fprog.len = get_length(fprog.filter);

		if (tests[i].data_type == SKB_INT) {
			fp_ext = kzalloc(4096, GFP_KERNEL);
			if (!fp_ext)
				return -ENOMEM;
			fp = fp_ext;
			memcpy(fp_ext->insns, tests[i].insns_int,
			       fprog.len * 8);
			fp->len = fprog.len;
			fp->bpf_func = sk_run_filter_int_skb;
		} else {
			err = sk_unattached_filter_create(&fp, &fprog);
			if (tests[i].data_type == EXPECTED_FAIL) {
				if (err == -EINVAL) {
					pr_cont("PASS\n");
					continue;
				} else {
					pr_cont("UNEXPECTED_PASS\n");
					/* verifier didn't reject the test
					 * that's bad enough, just return
					 */
					return -EINVAL;
				}
			}
			if (err) {
				pr_cont("FAIL to attach err=%d len=%d\n",
					err, fprog.len);
				return err;
			}
		}

		err = run_one(fp, &tests[i]);

		if (tests[i].data_type != SKB_INT)
			sk_unattached_filter_destroy(fp);
		else
			kfree(fp);

		if (err) {
			pr_cont("FAIL %d\n", err);
			err_cnt++;
		} else {
			pr_cont("PASS\n");
		}
	}

	if (err_cnt)
		return -EINVAL;
	else
		return 0;
}

static int __init test_bpf_init(void)
{
	return test_bpf();
}

static void __exit test_bpf_exit(void)
{
}

module_init(test_bpf_init);
module_exit(test_bpf_exit);
MODULE_LICENSE("GPL");
