// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include <stddef.h>
#include <string.h>
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include "bpf_helpers.h"

int _version SEC("version") = 1;

#if  __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define TEST_FIELD(TYPE, FIELD, MASK)					\
	{								\
		TYPE tmp = *(volatile TYPE *)&skb->FIELD;		\
		if (tmp != ((*(volatile __u32 *)&skb->FIELD) & MASK))	\
			return TC_ACT_SHOT;				\
	}
#else
#define TEST_FIELD_OFFSET(a, b)	((sizeof(a) - sizeof(b)) / sizeof(b))
#define TEST_FIELD(TYPE, FIELD, MASK)					\
	{								\
		TYPE tmp = *((volatile TYPE *)&skb->FIELD +		\
			      TEST_FIELD_OFFSET(skb->FIELD, TYPE));	\
		if (tmp != ((*(volatile __u32 *)&skb->FIELD) & MASK))	\
			return TC_ACT_SHOT;				\
	}
#endif

SEC("test1")
int process(struct __sk_buff *skb)
{
	TEST_FIELD(__u8,  len, 0xFF);
	TEST_FIELD(__u16, len, 0xFFFF);
	TEST_FIELD(__u32, len, 0xFFFFFFFF);
	TEST_FIELD(__u16, protocol, 0xFFFF);
	TEST_FIELD(__u32, protocol, 0xFFFFFFFF);
	TEST_FIELD(__u8,  hash, 0xFF);
	TEST_FIELD(__u16, hash, 0xFFFF);
	TEST_FIELD(__u32, hash, 0xFFFFFFFF);

	return TC_ACT_OK;
}
