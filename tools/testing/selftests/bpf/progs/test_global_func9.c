// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct S {
	int x;
};

struct C {
	int x;
	int y;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct S);
} map SEC(".maps");

enum E {
	E_ITEM
};

static int global_data_x = 100;
static int volatile global_data_y = 500;

__noinline int foo(const struct S *s)
{
	if (s)
		return bpf_get_prandom_u32() < s->x;

	return 0;
}

__noinline int bar(int *x)
{
	if (x)
		*x &= bpf_get_prandom_u32();

	return 0;
}
__noinline int baz(volatile int *x)
{
	if (x)
		*x &= bpf_get_prandom_u32();

	return 0;
}

__noinline int qux(enum E *e)
{
	if (e)
		return *e;

	return 0;
}

__noinline int quux(int (*arr)[10])
{
	if (arr)
		return (*arr)[9];

	return 0;
}

__noinline int quuz(int **p)
{
	if (p)
		*p = NULL;

	return 0;
}

SEC("cgroup_skb/ingress")
__success
int global_func9(struct __sk_buff *skb)
{
	int result = 0;

	{
		const struct S s = {.x = skb->len };

		result |= foo(&s);
	}

	{
		const __u32 key = 1;
		const struct S *s = bpf_map_lookup_elem(&map, &key);

		result |= foo(s);
	}

	{
		const struct C c = {.x = skb->len, .y = skb->family };

		result |= foo((const struct S *)&c);
	}

	{
		result |= foo(NULL);
	}

	{
		bar(&result);
		bar(&global_data_x);
	}

	{
		result |= baz(&global_data_y);
	}

	{
		enum E e = E_ITEM;

		result |= qux(&e);
	}

	{
		int array[10] = {0};

		result |= quux(&array);
	}

	{
		int *p;

		result |= quuz(&p);
	}

	return result ? 1 : 0;
}
