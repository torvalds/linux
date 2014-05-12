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
		"TXA",
		.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0) /* A == len * 2 */
		},
		SKB,
		{ 10, 20, 30, 40, 50 },
		{ { 1, 2 }, { 3, 6 }, { 4, 8 } },
	},
	{
		"ADD_SUB_MUL_K",
		.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 2),
			BPF_STMT(BPF_LDX | BPF_IMM, 3),
			BPF_STMT(BPF_ALU | BPF_SUB | BPF_X, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 0xffffffff),
			BPF_STMT(BPF_ALU | BPF_MUL | BPF_K, 3),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		0,
		{ },
		{ { 0, 0xfffffffd } }
	},
	{
		"DIV_KX",
		.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 8),
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_K, 2),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 0xffffffff),
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_X, 0),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 0xffffffff),
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_K, 0x70000000),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		0,
		{ },
		{ { 0, 0x40000001 } }
	},
	{
		"AND_OR_LSH_K",
		.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 0xff),
			BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0xf0),
			BPF_STMT(BPF_ALU | BPF_LSH | BPF_K, 27),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 0xf),
			BPF_STMT(BPF_ALU | BPF_OR | BPF_K, 0xf0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		0,
		{ },
		{ { 0, 0x800000ff }, { 1, 0x800000ff } },
	},
	{
		"LD_IND",
		.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_IND, MAX_K),
			BPF_STMT(BPF_RET | BPF_K, 1)
		},
		SKB,
		{ },
		{ { 1, 0 }, { 10, 0 }, { 60, 0 } },
	},
	{
		"LD_ABS",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 1000),
			BPF_STMT(BPF_RET | BPF_K, 1)
		},
		SKB,
		{ },
		{ { 1, 0 }, { 10, 0 }, { 60, 0 } },
	},
	{
		"LD_ABS_LL",
		.insns = {
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_LL_OFF),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_LL_OFF + 1),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ 1, 2, 3 },
		{ { 1, 0 }, { 2, 3 } },
	},
	{
		"LD_IND_LL",
		.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, SKF_LL_OFF - 1),
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_IND, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ 1, 2, 3, 0xff },
		{ { 1, 1 }, { 3, 3 }, { 4, 0xff } },
	},
	{
		"LD_ABS_NET",
		.insns = {
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_NET_OFF),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_NET_OFF + 1),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3 },
		{ { 15, 0 }, { 16, 3 } },
	},
	{
		"LD_IND_NET",
		.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, SKF_NET_OFF - 15),
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_IND, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3 },
		{ { 14, 0 }, { 15, 1 }, { 17, 3 } },
	},
	{
		"LD_PKTTYPE",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PKTTYPE),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SKB_TYPE, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PKTTYPE),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SKB_TYPE, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PKTTYPE),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SKB_TYPE, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{ { 1, 3 }, { 10, 3 } },
	},
	{
		"LD_MARK",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_MARK),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{ { 1, SKB_MARK}, { 10, SKB_MARK} },
	},
	{
		"LD_RXHASH",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_RXHASH),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{ { 1, SKB_HASH}, { 10, SKB_HASH} },
	},
	{
		"LD_QUEUE",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_QUEUE),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{ { 1, SKB_QUEUE_MAP }, { 10, SKB_QUEUE_MAP } },
	},
	{
		"LD_PROTOCOL",
		.insns = {
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 1),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 20, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 0),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PROTOCOL),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 2),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 30, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 0),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ 10, 20, 30 },
		{ { 10, ETH_P_IP }, { 100, ETH_P_IP } },
	},
	{
		"LD_VLAN_TAG",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_VLAN_TAG),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{
			{ 1, SKB_VLAN_TCI & ~VLAN_TAG_PRESENT },
			{ 10, SKB_VLAN_TCI & ~VLAN_TAG_PRESENT }
		},
	},
	{
		"LD_VLAN_TAG_PRESENT",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{
			{ 1, !!(SKB_VLAN_TCI & VLAN_TAG_PRESENT) },
			{ 10, !!(SKB_VLAN_TCI & VLAN_TAG_PRESENT) }
		},
	},
	{
		"LD_IFINDEX",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_IFINDEX),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{ { 1, SKB_DEV_IFINDEX }, { 10, SKB_DEV_IFINDEX } },
	},
	{
		"LD_HATYPE",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_HATYPE),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{ { 1, SKB_DEV_TYPE }, { 10, SKB_DEV_TYPE } },
	},
	{
		"LD_CPU",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_CPU),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_CPU),
			BPF_STMT(BPF_ALU | BPF_SUB | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{ { 1, 0 }, { 10, 0 } },
	},
	{
		"LD_NLATTR",
		.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 1),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_LDX | BPF_IMM, 3),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ 0xff, 4, 0, 2, 0, 4, 0, 3, 0 },
		{ { 4, 0 }, { 20, 5 } },
	},
	{
		"LD_NLATTR_NEST",
		.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_LDX | BPF_IMM, 3),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ 0xff, 12, 0, 1, 0, 4, 0, 2, 0, 4, 0, 3, 0 },
		{ { 4, 0 }, { 20, 9 } },
	},
	{
		"LD_PAYLOAD_OFF",
		.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		/* 00:00:00:00:00:00 > 00:00:00:00:00:00, ethtype IPv4 (0x0800),
		 * length 98: 127.0.0.1 > 127.0.0.1: ICMP echo request,
		 * id 9737, seq 1, length 64
		 */
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x08, 0x00,
		  0x45, 0x00, 0x00, 0x54, 0xac, 0x8b, 0x40, 0x00, 0x40,
		  0x01, 0x90, 0x1b, 0x7f, 0x00, 0x00, 0x01 },
		{ { 30, 0 }, { 100, 42 } },
	},
	{
		"LD_ANC_XOR",
		.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 10),
			BPF_STMT(BPF_LDX | BPF_IMM, 300),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_ALU_XOR_X),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{ { 4, 10 ^ 300 }, { 20, 10 ^ 300 } },
	},
	{
		"SPILL_FILL",
		.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_ALU | BPF_RSH, 1),
			BPF_STMT(BPF_ALU | BPF_XOR | BPF_X, 0),
			BPF_STMT(BPF_ST, 1), /* M1 = 1 ^ len */
			BPF_STMT(BPF_ALU | BPF_XOR | BPF_K, 0x80000000),
			BPF_STMT(BPF_ST, 2), /* M2 = 1 ^ len ^ 0x80000000 */
			BPF_STMT(BPF_STX, 15), /* M3 = len */
			BPF_STMT(BPF_LDX | BPF_MEM, 1),
			BPF_STMT(BPF_LD | BPF_MEM, 2),
			BPF_STMT(BPF_ALU | BPF_XOR | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 15),
			BPF_STMT(BPF_ALU | BPF_XOR | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{ },
		{ { 1, 0x80000001 }, { 2, 0x80000002 }, { 60, 0x80000000 ^ 60 } }
	},
	{
		"JEQ",
		.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 2),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_X, 0, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		SKB,
		{ 3, 3, 3, 3, 3 },
		{ { 1, 0 }, { 3, 1 }, { 4, MAX_K } },
	},
	{
		"JGT",
		.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 2),
			BPF_JUMP(BPF_JMP | BPF_JGT | BPF_X, 0, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		SKB,
		{ 4, 4, 4, 3, 3 },
		{ { 2, 0 }, { 3, 1 }, { 4, MAX_K } },
	},
	{
		"JGE",
		.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_IND, MAX_K),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 1, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 10),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 2, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 20),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 3, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 4, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 40),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		SKB,
		{ 1, 2, 3, 4, 5 },
		{ { 1, 20 }, { 3, 40 }, { 5, MAX_K } },
	},
	{
		"JSET",
		.insns = {
			BPF_JUMP(BPF_JMP | BPF_JA, 0, 0, 0),
			BPF_JUMP(BPF_JMP | BPF_JA, 1, 1, 1),
			BPF_JUMP(BPF_JMP | BPF_JA, 0, 0, 0),
			BPF_JUMP(BPF_JMP | BPF_JA, 0, 0, 0),
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_SUB | BPF_K, 4),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_W | BPF_IND, 0),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 1, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 10),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x80000000, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 20),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		SKB,
		{ 0, 0xAA, 0x55, 1 },
		{ { 4, 10 }, { 5, 20 }, { 6, MAX_K } },
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
		"tcpdump complex",
		.insns = {
			/* tcpdump -nei eth0 'tcp port 22 and (((ip[2:2] -
			 * ((ip[0]&0xf)<<2)) - ((tcp[12]&0xf0)>>2)) != 0) and
			 * (len > 115 or len < 30000000000)' -d
			 */
			{ 0x28,  0,  0, 0x0000000c },
			{ 0x15, 30,  0, 0x000086dd },
			{ 0x15,  0, 29, 0x00000800 },
			{ 0x30,  0,  0, 0x00000017 },
			{ 0x15,  0, 27, 0x00000006 },
			{ 0x28,  0,  0, 0x00000014 },
			{ 0x45, 25,  0, 0x00001fff },
			{ 0xb1,  0,  0, 0x0000000e },
			{ 0x48,  0,  0, 0x0000000e },
			{ 0x15,  2,  0, 0x00000016 },
			{ 0x48,  0,  0, 0x00000010 },
			{ 0x15,  0, 20, 0x00000016 },
			{ 0x28,  0,  0, 0x00000010 },
			{ 0x02,  0,  0, 0x00000001 },
			{ 0x30,  0,  0, 0x0000000e },
			{ 0x54,  0,  0, 0x0000000f },
			{ 0x64,  0,  0, 0x00000002 },
			{ 0x07,  0,  0, 0x00000005 },
			{ 0x60,  0,  0, 0x00000001 },
			{ 0x1c,  0,  0, 0x00000000 },
			{ 0x02,  0,  0, 0x00000005 },
			{ 0xb1,  0,  0, 0x0000000e },
			{ 0x50,  0,  0, 0x0000001a },
			{ 0x54,  0,  0, 0x000000f0 },
			{ 0x74,  0,  0, 0x00000002 },
			{ 0x07,  0,  0, 0x00000009 },
			{ 0x60,  0,  0, 0x00000005 },
			{ 0x1d,  4,  0, 0x00000000 },
			{ 0x80,  0,  0, 0x00000000 },
			{ 0x25,  1,  0, 0x00000073 },
			{ 0x35,  1,  0, 0xfc23ac00 },
			{ 0x06,  0,  0, 0x0000ffff },
			{ 0x06,  0,  0, 0x00000000 },
		},
		SKB,
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
		"RET_A",
		.insns = {
			/* check that unitialized X and A contain zeros */
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		SKB,
		{},
		{ {1, 0}, {2, 0} },
	},
	{
		"INT: ADD trivial",
		.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_ADD, R1, 2),
			BPF_ALU64_IMM(BPF_MOV, R2, 3),
			BPF_ALU64_REG(BPF_SUB, R1, R2),
			BPF_ALU64_IMM(BPF_ADD, R1, -1),
			BPF_ALU64_IMM(BPF_MUL, R1, 3),
			BPF_ALU64_REG(BPF_MOV, R0, R1),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, 0xfffffffd } }
	},
	{
		"INT: MUL_X",
		.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, -1),
			BPF_ALU64_IMM(BPF_MOV, R1, -1),
			BPF_ALU64_IMM(BPF_MOV, R2, 3),
			BPF_ALU64_REG(BPF_MUL, R1, R2),
			BPF_JMP_IMM(BPF_JEQ, R1, 0xfffffffd, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, 1 } }
	},
	{
		"INT: MUL_X2",
		.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, -1),
			BPF_ALU32_IMM(BPF_MOV, R1, -1),
			BPF_ALU32_IMM(BPF_MOV, R2, 3),
			BPF_ALU64_REG(BPF_MUL, R1, R2),
			BPF_ALU64_IMM(BPF_RSH, R1, 8),
			BPF_JMP_IMM(BPF_JEQ, R1, 0x2ffffff, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, 1 } }
	},
	{
		"INT: MUL32_X",
		.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, -1),
			BPF_ALU64_IMM(BPF_MOV, R1, -1),
			BPF_ALU32_IMM(BPF_MOV, R2, 3),
			BPF_ALU32_REG(BPF_MUL, R1, R2),
			BPF_ALU64_IMM(BPF_RSH, R1, 8),
			BPF_JMP_IMM(BPF_JEQ, R1, 0xffffff, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, 1 } }
	},
	{
		/* Have to test all register combinations, since
		 * JITing of different registers will produce
		 * different asm code.
		 */
		"INT: ADD 64-bit",
		.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 0),
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU64_IMM(BPF_MOV, R3, 3),
			BPF_ALU64_IMM(BPF_MOV, R4, 4),
			BPF_ALU64_IMM(BPF_MOV, R5, 5),
			BPF_ALU64_IMM(BPF_MOV, R6, 6),
			BPF_ALU64_IMM(BPF_MOV, R7, 7),
			BPF_ALU64_IMM(BPF_MOV, R8, 8),
			BPF_ALU64_IMM(BPF_MOV, R9, 9),
			BPF_ALU64_IMM(BPF_ADD, R0, 20),
			BPF_ALU64_IMM(BPF_ADD, R1, 20),
			BPF_ALU64_IMM(BPF_ADD, R2, 20),
			BPF_ALU64_IMM(BPF_ADD, R3, 20),
			BPF_ALU64_IMM(BPF_ADD, R4, 20),
			BPF_ALU64_IMM(BPF_ADD, R5, 20),
			BPF_ALU64_IMM(BPF_ADD, R6, 20),
			BPF_ALU64_IMM(BPF_ADD, R7, 20),
			BPF_ALU64_IMM(BPF_ADD, R8, 20),
			BPF_ALU64_IMM(BPF_ADD, R9, 20),
			BPF_ALU64_IMM(BPF_SUB, R0, 10),
			BPF_ALU64_IMM(BPF_SUB, R1, 10),
			BPF_ALU64_IMM(BPF_SUB, R2, 10),
			BPF_ALU64_IMM(BPF_SUB, R3, 10),
			BPF_ALU64_IMM(BPF_SUB, R4, 10),
			BPF_ALU64_IMM(BPF_SUB, R5, 10),
			BPF_ALU64_IMM(BPF_SUB, R6, 10),
			BPF_ALU64_IMM(BPF_SUB, R7, 10),
			BPF_ALU64_IMM(BPF_SUB, R8, 10),
			BPF_ALU64_IMM(BPF_SUB, R9, 10),
			BPF_ALU64_REG(BPF_ADD, R0, R0),
			BPF_ALU64_REG(BPF_ADD, R0, R1),
			BPF_ALU64_REG(BPF_ADD, R0, R2),
			BPF_ALU64_REG(BPF_ADD, R0, R3),
			BPF_ALU64_REG(BPF_ADD, R0, R4),
			BPF_ALU64_REG(BPF_ADD, R0, R5),
			BPF_ALU64_REG(BPF_ADD, R0, R6),
			BPF_ALU64_REG(BPF_ADD, R0, R7),
			BPF_ALU64_REG(BPF_ADD, R0, R8),
			BPF_ALU64_REG(BPF_ADD, R0, R9), /* R0 == 155 */
			BPF_JMP_IMM(BPF_JEQ, R0, 155, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R1, R0),
			BPF_ALU64_REG(BPF_ADD, R1, R1),
			BPF_ALU64_REG(BPF_ADD, R1, R2),
			BPF_ALU64_REG(BPF_ADD, R1, R3),
			BPF_ALU64_REG(BPF_ADD, R1, R4),
			BPF_ALU64_REG(BPF_ADD, R1, R5),
			BPF_ALU64_REG(BPF_ADD, R1, R6),
			BPF_ALU64_REG(BPF_ADD, R1, R7),
			BPF_ALU64_REG(BPF_ADD, R1, R8),
			BPF_ALU64_REG(BPF_ADD, R1, R9), /* R1 == 456 */
			BPF_JMP_IMM(BPF_JEQ, R1, 456, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R2, R0),
			BPF_ALU64_REG(BPF_ADD, R2, R1),
			BPF_ALU64_REG(BPF_ADD, R2, R2),
			BPF_ALU64_REG(BPF_ADD, R2, R3),
			BPF_ALU64_REG(BPF_ADD, R2, R4),
			BPF_ALU64_REG(BPF_ADD, R2, R5),
			BPF_ALU64_REG(BPF_ADD, R2, R6),
			BPF_ALU64_REG(BPF_ADD, R2, R7),
			BPF_ALU64_REG(BPF_ADD, R2, R8),
			BPF_ALU64_REG(BPF_ADD, R2, R9), /* R2 == 1358 */
			BPF_JMP_IMM(BPF_JEQ, R2, 1358, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R3, R0),
			BPF_ALU64_REG(BPF_ADD, R3, R1),
			BPF_ALU64_REG(BPF_ADD, R3, R2),
			BPF_ALU64_REG(BPF_ADD, R3, R3),
			BPF_ALU64_REG(BPF_ADD, R3, R4),
			BPF_ALU64_REG(BPF_ADD, R3, R5),
			BPF_ALU64_REG(BPF_ADD, R3, R6),
			BPF_ALU64_REG(BPF_ADD, R3, R7),
			BPF_ALU64_REG(BPF_ADD, R3, R8),
			BPF_ALU64_REG(BPF_ADD, R3, R9), /* R3 == 4063 */
			BPF_JMP_IMM(BPF_JEQ, R3, 4063, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R4, R0),
			BPF_ALU64_REG(BPF_ADD, R4, R1),
			BPF_ALU64_REG(BPF_ADD, R4, R2),
			BPF_ALU64_REG(BPF_ADD, R4, R3),
			BPF_ALU64_REG(BPF_ADD, R4, R4),
			BPF_ALU64_REG(BPF_ADD, R4, R5),
			BPF_ALU64_REG(BPF_ADD, R4, R6),
			BPF_ALU64_REG(BPF_ADD, R4, R7),
			BPF_ALU64_REG(BPF_ADD, R4, R8),
			BPF_ALU64_REG(BPF_ADD, R4, R9), /* R4 == 12177 */
			BPF_JMP_IMM(BPF_JEQ, R4, 12177, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R5, R0),
			BPF_ALU64_REG(BPF_ADD, R5, R1),
			BPF_ALU64_REG(BPF_ADD, R5, R2),
			BPF_ALU64_REG(BPF_ADD, R5, R3),
			BPF_ALU64_REG(BPF_ADD, R5, R4),
			BPF_ALU64_REG(BPF_ADD, R5, R5),
			BPF_ALU64_REG(BPF_ADD, R5, R6),
			BPF_ALU64_REG(BPF_ADD, R5, R7),
			BPF_ALU64_REG(BPF_ADD, R5, R8),
			BPF_ALU64_REG(BPF_ADD, R5, R9), /* R5 == 36518 */
			BPF_JMP_IMM(BPF_JEQ, R5, 36518, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R6, R0),
			BPF_ALU64_REG(BPF_ADD, R6, R1),
			BPF_ALU64_REG(BPF_ADD, R6, R2),
			BPF_ALU64_REG(BPF_ADD, R6, R3),
			BPF_ALU64_REG(BPF_ADD, R6, R4),
			BPF_ALU64_REG(BPF_ADD, R6, R5),
			BPF_ALU64_REG(BPF_ADD, R6, R6),
			BPF_ALU64_REG(BPF_ADD, R6, R7),
			BPF_ALU64_REG(BPF_ADD, R6, R8),
			BPF_ALU64_REG(BPF_ADD, R6, R9), /* R6 == 109540 */
			BPF_JMP_IMM(BPF_JEQ, R6, 109540, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R7, R0),
			BPF_ALU64_REG(BPF_ADD, R7, R1),
			BPF_ALU64_REG(BPF_ADD, R7, R2),
			BPF_ALU64_REG(BPF_ADD, R7, R3),
			BPF_ALU64_REG(BPF_ADD, R7, R4),
			BPF_ALU64_REG(BPF_ADD, R7, R5),
			BPF_ALU64_REG(BPF_ADD, R7, R6),
			BPF_ALU64_REG(BPF_ADD, R7, R7),
			BPF_ALU64_REG(BPF_ADD, R7, R8),
			BPF_ALU64_REG(BPF_ADD, R7, R9), /* R7 == 328605 */
			BPF_JMP_IMM(BPF_JEQ, R7, 328605, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R8, R0),
			BPF_ALU64_REG(BPF_ADD, R8, R1),
			BPF_ALU64_REG(BPF_ADD, R8, R2),
			BPF_ALU64_REG(BPF_ADD, R8, R3),
			BPF_ALU64_REG(BPF_ADD, R8, R4),
			BPF_ALU64_REG(BPF_ADD, R8, R5),
			BPF_ALU64_REG(BPF_ADD, R8, R6),
			BPF_ALU64_REG(BPF_ADD, R8, R7),
			BPF_ALU64_REG(BPF_ADD, R8, R8),
			BPF_ALU64_REG(BPF_ADD, R8, R9), /* R8 == 985799 */
			BPF_JMP_IMM(BPF_JEQ, R8, 985799, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R9, R0),
			BPF_ALU64_REG(BPF_ADD, R9, R1),
			BPF_ALU64_REG(BPF_ADD, R9, R2),
			BPF_ALU64_REG(BPF_ADD, R9, R3),
			BPF_ALU64_REG(BPF_ADD, R9, R4),
			BPF_ALU64_REG(BPF_ADD, R9, R5),
			BPF_ALU64_REG(BPF_ADD, R9, R6),
			BPF_ALU64_REG(BPF_ADD, R9, R7),
			BPF_ALU64_REG(BPF_ADD, R9, R8),
			BPF_ALU64_REG(BPF_ADD, R9, R9), /* R9 == 2957380 */
			BPF_ALU64_REG(BPF_MOV, R0, R9),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, 2957380 } }
	},
	{
		"INT: ADD 32-bit",
		.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 20),
			BPF_ALU32_IMM(BPF_MOV, R1, 1),
			BPF_ALU32_IMM(BPF_MOV, R2, 2),
			BPF_ALU32_IMM(BPF_MOV, R3, 3),
			BPF_ALU32_IMM(BPF_MOV, R4, 4),
			BPF_ALU32_IMM(BPF_MOV, R5, 5),
			BPF_ALU32_IMM(BPF_MOV, R6, 6),
			BPF_ALU32_IMM(BPF_MOV, R7, 7),
			BPF_ALU32_IMM(BPF_MOV, R8, 8),
			BPF_ALU32_IMM(BPF_MOV, R9, 9),
			BPF_ALU64_IMM(BPF_ADD, R1, 10),
			BPF_ALU64_IMM(BPF_ADD, R2, 10),
			BPF_ALU64_IMM(BPF_ADD, R3, 10),
			BPF_ALU64_IMM(BPF_ADD, R4, 10),
			BPF_ALU64_IMM(BPF_ADD, R5, 10),
			BPF_ALU64_IMM(BPF_ADD, R6, 10),
			BPF_ALU64_IMM(BPF_ADD, R7, 10),
			BPF_ALU64_IMM(BPF_ADD, R8, 10),
			BPF_ALU64_IMM(BPF_ADD, R9, 10),
			BPF_ALU32_REG(BPF_ADD, R0, R1),
			BPF_ALU32_REG(BPF_ADD, R0, R2),
			BPF_ALU32_REG(BPF_ADD, R0, R3),
			BPF_ALU32_REG(BPF_ADD, R0, R4),
			BPF_ALU32_REG(BPF_ADD, R0, R5),
			BPF_ALU32_REG(BPF_ADD, R0, R6),
			BPF_ALU32_REG(BPF_ADD, R0, R7),
			BPF_ALU32_REG(BPF_ADD, R0, R8),
			BPF_ALU32_REG(BPF_ADD, R0, R9), /* R0 == 155 */
			BPF_JMP_IMM(BPF_JEQ, R0, 155, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R1, R0),
			BPF_ALU32_REG(BPF_ADD, R1, R1),
			BPF_ALU32_REG(BPF_ADD, R1, R2),
			BPF_ALU32_REG(BPF_ADD, R1, R3),
			BPF_ALU32_REG(BPF_ADD, R1, R4),
			BPF_ALU32_REG(BPF_ADD, R1, R5),
			BPF_ALU32_REG(BPF_ADD, R1, R6),
			BPF_ALU32_REG(BPF_ADD, R1, R7),
			BPF_ALU32_REG(BPF_ADD, R1, R8),
			BPF_ALU32_REG(BPF_ADD, R1, R9), /* R1 == 456 */
			BPF_JMP_IMM(BPF_JEQ, R1, 456, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R2, R0),
			BPF_ALU32_REG(BPF_ADD, R2, R1),
			BPF_ALU32_REG(BPF_ADD, R2, R2),
			BPF_ALU32_REG(BPF_ADD, R2, R3),
			BPF_ALU32_REG(BPF_ADD, R2, R4),
			BPF_ALU32_REG(BPF_ADD, R2, R5),
			BPF_ALU32_REG(BPF_ADD, R2, R6),
			BPF_ALU32_REG(BPF_ADD, R2, R7),
			BPF_ALU32_REG(BPF_ADD, R2, R8),
			BPF_ALU32_REG(BPF_ADD, R2, R9), /* R2 == 1358 */
			BPF_JMP_IMM(BPF_JEQ, R2, 1358, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R3, R0),
			BPF_ALU32_REG(BPF_ADD, R3, R1),
			BPF_ALU32_REG(BPF_ADD, R3, R2),
			BPF_ALU32_REG(BPF_ADD, R3, R3),
			BPF_ALU32_REG(BPF_ADD, R3, R4),
			BPF_ALU32_REG(BPF_ADD, R3, R5),
			BPF_ALU32_REG(BPF_ADD, R3, R6),
			BPF_ALU32_REG(BPF_ADD, R3, R7),
			BPF_ALU32_REG(BPF_ADD, R3, R8),
			BPF_ALU32_REG(BPF_ADD, R3, R9), /* R3 == 4063 */
			BPF_JMP_IMM(BPF_JEQ, R3, 4063, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R4, R0),
			BPF_ALU32_REG(BPF_ADD, R4, R1),
			BPF_ALU32_REG(BPF_ADD, R4, R2),
			BPF_ALU32_REG(BPF_ADD, R4, R3),
			BPF_ALU32_REG(BPF_ADD, R4, R4),
			BPF_ALU32_REG(BPF_ADD, R4, R5),
			BPF_ALU32_REG(BPF_ADD, R4, R6),
			BPF_ALU32_REG(BPF_ADD, R4, R7),
			BPF_ALU32_REG(BPF_ADD, R4, R8),
			BPF_ALU32_REG(BPF_ADD, R4, R9), /* R4 == 12177 */
			BPF_JMP_IMM(BPF_JEQ, R4, 12177, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R5, R0),
			BPF_ALU32_REG(BPF_ADD, R5, R1),
			BPF_ALU32_REG(BPF_ADD, R5, R2),
			BPF_ALU32_REG(BPF_ADD, R5, R3),
			BPF_ALU32_REG(BPF_ADD, R5, R4),
			BPF_ALU32_REG(BPF_ADD, R5, R5),
			BPF_ALU32_REG(BPF_ADD, R5, R6),
			BPF_ALU32_REG(BPF_ADD, R5, R7),
			BPF_ALU32_REG(BPF_ADD, R5, R8),
			BPF_ALU32_REG(BPF_ADD, R5, R9), /* R5 == 36518 */
			BPF_JMP_IMM(BPF_JEQ, R5, 36518, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R6, R0),
			BPF_ALU32_REG(BPF_ADD, R6, R1),
			BPF_ALU32_REG(BPF_ADD, R6, R2),
			BPF_ALU32_REG(BPF_ADD, R6, R3),
			BPF_ALU32_REG(BPF_ADD, R6, R4),
			BPF_ALU32_REG(BPF_ADD, R6, R5),
			BPF_ALU32_REG(BPF_ADD, R6, R6),
			BPF_ALU32_REG(BPF_ADD, R6, R7),
			BPF_ALU32_REG(BPF_ADD, R6, R8),
			BPF_ALU32_REG(BPF_ADD, R6, R9), /* R6 == 109540 */
			BPF_JMP_IMM(BPF_JEQ, R6, 109540, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R7, R0),
			BPF_ALU32_REG(BPF_ADD, R7, R1),
			BPF_ALU32_REG(BPF_ADD, R7, R2),
			BPF_ALU32_REG(BPF_ADD, R7, R3),
			BPF_ALU32_REG(BPF_ADD, R7, R4),
			BPF_ALU32_REG(BPF_ADD, R7, R5),
			BPF_ALU32_REG(BPF_ADD, R7, R6),
			BPF_ALU32_REG(BPF_ADD, R7, R7),
			BPF_ALU32_REG(BPF_ADD, R7, R8),
			BPF_ALU32_REG(BPF_ADD, R7, R9), /* R7 == 328605 */
			BPF_JMP_IMM(BPF_JEQ, R7, 328605, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R8, R0),
			BPF_ALU32_REG(BPF_ADD, R8, R1),
			BPF_ALU32_REG(BPF_ADD, R8, R2),
			BPF_ALU32_REG(BPF_ADD, R8, R3),
			BPF_ALU32_REG(BPF_ADD, R8, R4),
			BPF_ALU32_REG(BPF_ADD, R8, R5),
			BPF_ALU32_REG(BPF_ADD, R8, R6),
			BPF_ALU32_REG(BPF_ADD, R8, R7),
			BPF_ALU32_REG(BPF_ADD, R8, R8),
			BPF_ALU32_REG(BPF_ADD, R8, R9), /* R8 == 985799 */
			BPF_JMP_IMM(BPF_JEQ, R8, 985799, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R9, R0),
			BPF_ALU32_REG(BPF_ADD, R9, R1),
			BPF_ALU32_REG(BPF_ADD, R9, R2),
			BPF_ALU32_REG(BPF_ADD, R9, R3),
			BPF_ALU32_REG(BPF_ADD, R9, R4),
			BPF_ALU32_REG(BPF_ADD, R9, R5),
			BPF_ALU32_REG(BPF_ADD, R9, R6),
			BPF_ALU32_REG(BPF_ADD, R9, R7),
			BPF_ALU32_REG(BPF_ADD, R9, R8),
			BPF_ALU32_REG(BPF_ADD, R9, R9), /* R9 == 2957380 */
			BPF_ALU32_REG(BPF_MOV, R0, R9),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, 2957380 } }
	},
	{	/* Mainly checking JIT here. */
		"INT: SUB",
		.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 0),
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU64_IMM(BPF_MOV, R3, 3),
			BPF_ALU64_IMM(BPF_MOV, R4, 4),
			BPF_ALU64_IMM(BPF_MOV, R5, 5),
			BPF_ALU64_IMM(BPF_MOV, R6, 6),
			BPF_ALU64_IMM(BPF_MOV, R7, 7),
			BPF_ALU64_IMM(BPF_MOV, R8, 8),
			BPF_ALU64_IMM(BPF_MOV, R9, 9),
			BPF_ALU64_REG(BPF_SUB, R0, R0),
			BPF_ALU64_REG(BPF_SUB, R0, R1),
			BPF_ALU64_REG(BPF_SUB, R0, R2),
			BPF_ALU64_REG(BPF_SUB, R0, R3),
			BPF_ALU64_REG(BPF_SUB, R0, R4),
			BPF_ALU64_REG(BPF_SUB, R0, R5),
			BPF_ALU64_REG(BPF_SUB, R0, R6),
			BPF_ALU64_REG(BPF_SUB, R0, R7),
			BPF_ALU64_REG(BPF_SUB, R0, R8),
			BPF_ALU64_REG(BPF_SUB, R0, R9),
			BPF_ALU64_IMM(BPF_SUB, R0, 10),
			BPF_JMP_IMM(BPF_JEQ, R0, -55, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R1, R0),
			BPF_ALU64_REG(BPF_SUB, R1, R2),
			BPF_ALU64_REG(BPF_SUB, R1, R3),
			BPF_ALU64_REG(BPF_SUB, R1, R4),
			BPF_ALU64_REG(BPF_SUB, R1, R5),
			BPF_ALU64_REG(BPF_SUB, R1, R6),
			BPF_ALU64_REG(BPF_SUB, R1, R7),
			BPF_ALU64_REG(BPF_SUB, R1, R8),
			BPF_ALU64_REG(BPF_SUB, R1, R9),
			BPF_ALU64_IMM(BPF_SUB, R1, 10),
			BPF_ALU64_REG(BPF_SUB, R2, R0),
			BPF_ALU64_REG(BPF_SUB, R2, R1),
			BPF_ALU64_REG(BPF_SUB, R2, R3),
			BPF_ALU64_REG(BPF_SUB, R2, R4),
			BPF_ALU64_REG(BPF_SUB, R2, R5),
			BPF_ALU64_REG(BPF_SUB, R2, R6),
			BPF_ALU64_REG(BPF_SUB, R2, R7),
			BPF_ALU64_REG(BPF_SUB, R2, R8),
			BPF_ALU64_REG(BPF_SUB, R2, R9),
			BPF_ALU64_IMM(BPF_SUB, R2, 10),
			BPF_ALU64_REG(BPF_SUB, R3, R0),
			BPF_ALU64_REG(BPF_SUB, R3, R1),
			BPF_ALU64_REG(BPF_SUB, R3, R2),
			BPF_ALU64_REG(BPF_SUB, R3, R4),
			BPF_ALU64_REG(BPF_SUB, R3, R5),
			BPF_ALU64_REG(BPF_SUB, R3, R6),
			BPF_ALU64_REG(BPF_SUB, R3, R7),
			BPF_ALU64_REG(BPF_SUB, R3, R8),
			BPF_ALU64_REG(BPF_SUB, R3, R9),
			BPF_ALU64_IMM(BPF_SUB, R3, 10),
			BPF_ALU64_REG(BPF_SUB, R4, R0),
			BPF_ALU64_REG(BPF_SUB, R4, R1),
			BPF_ALU64_REG(BPF_SUB, R4, R2),
			BPF_ALU64_REG(BPF_SUB, R4, R3),
			BPF_ALU64_REG(BPF_SUB, R4, R5),
			BPF_ALU64_REG(BPF_SUB, R4, R6),
			BPF_ALU64_REG(BPF_SUB, R4, R7),
			BPF_ALU64_REG(BPF_SUB, R4, R8),
			BPF_ALU64_REG(BPF_SUB, R4, R9),
			BPF_ALU64_IMM(BPF_SUB, R4, 10),
			BPF_ALU64_REG(BPF_SUB, R5, R0),
			BPF_ALU64_REG(BPF_SUB, R5, R1),
			BPF_ALU64_REG(BPF_SUB, R5, R2),
			BPF_ALU64_REG(BPF_SUB, R5, R3),
			BPF_ALU64_REG(BPF_SUB, R5, R4),
			BPF_ALU64_REG(BPF_SUB, R5, R6),
			BPF_ALU64_REG(BPF_SUB, R5, R7),
			BPF_ALU64_REG(BPF_SUB, R5, R8),
			BPF_ALU64_REG(BPF_SUB, R5, R9),
			BPF_ALU64_IMM(BPF_SUB, R5, 10),
			BPF_ALU64_REG(BPF_SUB, R6, R0),
			BPF_ALU64_REG(BPF_SUB, R6, R1),
			BPF_ALU64_REG(BPF_SUB, R6, R2),
			BPF_ALU64_REG(BPF_SUB, R6, R3),
			BPF_ALU64_REG(BPF_SUB, R6, R4),
			BPF_ALU64_REG(BPF_SUB, R6, R5),
			BPF_ALU64_REG(BPF_SUB, R6, R7),
			BPF_ALU64_REG(BPF_SUB, R6, R8),
			BPF_ALU64_REG(BPF_SUB, R6, R9),
			BPF_ALU64_IMM(BPF_SUB, R6, 10),
			BPF_ALU64_REG(BPF_SUB, R7, R0),
			BPF_ALU64_REG(BPF_SUB, R7, R1),
			BPF_ALU64_REG(BPF_SUB, R7, R2),
			BPF_ALU64_REG(BPF_SUB, R7, R3),
			BPF_ALU64_REG(BPF_SUB, R7, R4),
			BPF_ALU64_REG(BPF_SUB, R7, R5),
			BPF_ALU64_REG(BPF_SUB, R7, R6),
			BPF_ALU64_REG(BPF_SUB, R7, R8),
			BPF_ALU64_REG(BPF_SUB, R7, R9),
			BPF_ALU64_IMM(BPF_SUB, R7, 10),
			BPF_ALU64_REG(BPF_SUB, R8, R0),
			BPF_ALU64_REG(BPF_SUB, R8, R1),
			BPF_ALU64_REG(BPF_SUB, R8, R2),
			BPF_ALU64_REG(BPF_SUB, R8, R3),
			BPF_ALU64_REG(BPF_SUB, R8, R4),
			BPF_ALU64_REG(BPF_SUB, R8, R5),
			BPF_ALU64_REG(BPF_SUB, R8, R6),
			BPF_ALU64_REG(BPF_SUB, R8, R7),
			BPF_ALU64_REG(BPF_SUB, R8, R9),
			BPF_ALU64_IMM(BPF_SUB, R8, 10),
			BPF_ALU64_REG(BPF_SUB, R9, R0),
			BPF_ALU64_REG(BPF_SUB, R9, R1),
			BPF_ALU64_REG(BPF_SUB, R9, R2),
			BPF_ALU64_REG(BPF_SUB, R9, R3),
			BPF_ALU64_REG(BPF_SUB, R9, R4),
			BPF_ALU64_REG(BPF_SUB, R9, R5),
			BPF_ALU64_REG(BPF_SUB, R9, R6),
			BPF_ALU64_REG(BPF_SUB, R9, R7),
			BPF_ALU64_REG(BPF_SUB, R9, R8),
			BPF_ALU64_IMM(BPF_SUB, R9, 10),
			BPF_ALU64_IMM(BPF_SUB, R0, 10),
			BPF_ALU64_IMM(BPF_NEG, R0, 0),
			BPF_ALU64_REG(BPF_SUB, R0, R1),
			BPF_ALU64_REG(BPF_SUB, R0, R2),
			BPF_ALU64_REG(BPF_SUB, R0, R3),
			BPF_ALU64_REG(BPF_SUB, R0, R4),
			BPF_ALU64_REG(BPF_SUB, R0, R5),
			BPF_ALU64_REG(BPF_SUB, R0, R6),
			BPF_ALU64_REG(BPF_SUB, R0, R7),
			BPF_ALU64_REG(BPF_SUB, R0, R8),
			BPF_ALU64_REG(BPF_SUB, R0, R9),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, 11 } }
	},
	{	/* Mainly checking JIT here. */
		"INT: XOR",
		.insns_int = {
			BPF_ALU64_REG(BPF_SUB, R0, R0),
			BPF_ALU64_REG(BPF_XOR, R1, R1),
			BPF_JMP_REG(BPF_JEQ, R0, R1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R0, 10),
			BPF_ALU64_IMM(BPF_MOV, R1, -1),
			BPF_ALU64_REG(BPF_SUB, R1, R1),
			BPF_ALU64_REG(BPF_XOR, R2, R2),
			BPF_JMP_REG(BPF_JEQ, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R2, R2),
			BPF_ALU64_REG(BPF_XOR, R3, R3),
			BPF_ALU64_IMM(BPF_MOV, R0, 10),
			BPF_ALU64_IMM(BPF_MOV, R1, -1),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R3, R3),
			BPF_ALU64_REG(BPF_XOR, R4, R4),
			BPF_ALU64_IMM(BPF_MOV, R2, 1),
			BPF_ALU64_IMM(BPF_MOV, R5, -1),
			BPF_JMP_REG(BPF_JEQ, R3, R4, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R4, R4),
			BPF_ALU64_REG(BPF_XOR, R5, R5),
			BPF_ALU64_IMM(BPF_MOV, R3, 1),
			BPF_ALU64_IMM(BPF_MOV, R7, -1),
			BPF_JMP_REG(BPF_JEQ, R5, R4, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R5, 1),
			BPF_ALU64_REG(BPF_SUB, R5, R5),
			BPF_ALU64_REG(BPF_XOR, R6, R6),
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_MOV, R8, -1),
			BPF_JMP_REG(BPF_JEQ, R5, R6, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R6, R6),
			BPF_ALU64_REG(BPF_XOR, R7, R7),
			BPF_JMP_REG(BPF_JEQ, R7, R6, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R7, R7),
			BPF_ALU64_REG(BPF_XOR, R8, R8),
			BPF_JMP_REG(BPF_JEQ, R7, R8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R8, R8),
			BPF_ALU64_REG(BPF_XOR, R9, R9),
			BPF_JMP_REG(BPF_JEQ, R9, R8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R9, R9),
			BPF_ALU64_REG(BPF_XOR, R0, R0),
			BPF_JMP_REG(BPF_JEQ, R9, R0, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R1, R1),
			BPF_ALU64_REG(BPF_XOR, R0, R0),
			BPF_JMP_REG(BPF_JEQ, R9, R0, 2),
			BPF_ALU64_IMM(BPF_MOV, R0, 0),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, 1 } }
	},
	{	/* Mainly checking JIT here. */
		"INT: MUL",
		.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 11),
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU64_IMM(BPF_MOV, R3, 3),
			BPF_ALU64_IMM(BPF_MOV, R4, 4),
			BPF_ALU64_IMM(BPF_MOV, R5, 5),
			BPF_ALU64_IMM(BPF_MOV, R6, 6),
			BPF_ALU64_IMM(BPF_MOV, R7, 7),
			BPF_ALU64_IMM(BPF_MOV, R8, 8),
			BPF_ALU64_IMM(BPF_MOV, R9, 9),
			BPF_ALU64_REG(BPF_MUL, R0, R0),
			BPF_ALU64_REG(BPF_MUL, R0, R1),
			BPF_ALU64_REG(BPF_MUL, R0, R2),
			BPF_ALU64_REG(BPF_MUL, R0, R3),
			BPF_ALU64_REG(BPF_MUL, R0, R4),
			BPF_ALU64_REG(BPF_MUL, R0, R5),
			BPF_ALU64_REG(BPF_MUL, R0, R6),
			BPF_ALU64_REG(BPF_MUL, R0, R7),
			BPF_ALU64_REG(BPF_MUL, R0, R8),
			BPF_ALU64_REG(BPF_MUL, R0, R9),
			BPF_ALU64_IMM(BPF_MUL, R0, 10),
			BPF_JMP_IMM(BPF_JEQ, R0, 439084800, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_MUL, R1, R0),
			BPF_ALU64_REG(BPF_MUL, R1, R2),
			BPF_ALU64_REG(BPF_MUL, R1, R3),
			BPF_ALU64_REG(BPF_MUL, R1, R4),
			BPF_ALU64_REG(BPF_MUL, R1, R5),
			BPF_ALU64_REG(BPF_MUL, R1, R6),
			BPF_ALU64_REG(BPF_MUL, R1, R7),
			BPF_ALU64_REG(BPF_MUL, R1, R8),
			BPF_ALU64_REG(BPF_MUL, R1, R9),
			BPF_ALU64_IMM(BPF_MUL, R1, 10),
			BPF_ALU64_REG(BPF_MOV, R2, R1),
			BPF_ALU64_IMM(BPF_RSH, R2, 32),
			BPF_JMP_IMM(BPF_JEQ, R2, 0x5a924, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_LSH, R1, 32),
			BPF_ALU64_IMM(BPF_ARSH, R1, 32),
			BPF_JMP_IMM(BPF_JEQ, R1, 0xebb90000, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_MUL, R2, R0),
			BPF_ALU64_REG(BPF_MUL, R2, R1),
			BPF_ALU64_REG(BPF_MUL, R2, R3),
			BPF_ALU64_REG(BPF_MUL, R2, R4),
			BPF_ALU64_REG(BPF_MUL, R2, R5),
			BPF_ALU64_REG(BPF_MUL, R2, R6),
			BPF_ALU64_REG(BPF_MUL, R2, R7),
			BPF_ALU64_REG(BPF_MUL, R2, R8),
			BPF_ALU64_REG(BPF_MUL, R2, R9),
			BPF_ALU64_IMM(BPF_MUL, R2, 10),
			BPF_ALU64_IMM(BPF_RSH, R2, 32),
			BPF_ALU64_REG(BPF_MOV, R0, R2),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, 0x35d97ef2 } }
	},
	{
		"INT: ALU MIX",
		.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 11),
			BPF_ALU64_IMM(BPF_ADD, R0, -1),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU64_IMM(BPF_XOR, R2, 3),
			BPF_ALU64_REG(BPF_DIV, R0, R2),
			BPF_JMP_IMM(BPF_JEQ, R0, 10, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOD, R0, 3),
			BPF_JMP_IMM(BPF_JEQ, R0, 1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R0, -1),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ },
		{ { 0, -1 } }
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
		"INT: DIV by zero",
		.insns_int = {
			BPF_ALU64_REG(BPF_MOV, R6, R1),
			BPF_ALU64_IMM(BPF_MOV, R7, 0),
			BPF_LD_ABS(BPF_B, 3),
			BPF_ALU32_REG(BPF_DIV, R0, R7),
			BPF_EXIT_INSN(),
		},
		SKB_INT,
		{ 10, 20, 30, 40, 50 },
		{ { 3, 0 }, { 4, 0 } }
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
	{
		"check: div_k_0",
		.insns = {
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_K, 0),
			BPF_STMT(BPF_RET | BPF_K, 0)
		},
		EXPECTED_FAIL,
		{ },
		{ }
	},
	{
		"check: unknown insn",
		.insns = {
			/* seccomp insn, rejected in socket filter */
			BPF_STMT(BPF_LDX | BPF_W | BPF_ABS, 0),
			BPF_STMT(BPF_RET | BPF_K, 0)
		},
		EXPECTED_FAIL,
		{ },
		{ }
	},
	{
		"check: out of range spill/fill",
		.insns = {
			BPF_STMT(BPF_STX, 16),
			BPF_STMT(BPF_RET | BPF_K, 0)
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
