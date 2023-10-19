// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

struct S {
	int v;
};

struct S global_variable = {};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 7);
	__type(key, __u32);
	__type(value, int);
} values SEC(".maps");

static void save_value(__u32 index, int value)
{
	bpf_map_update_elem(&values, &index, &value, 0);
}

__noinline int foo(__u32 index, struct S *s)
{
	if (s) {
		save_value(index, s->v);
		return ++s->v;
	}

	save_value(index, 0);

	return 1;
}

__noinline int bar(__u32 index, volatile struct S *s)
{
	if (s) {
		save_value(index, s->v);
		return ++s->v;
	}

	save_value(index, 0);

	return 1;
}

__noinline int baz(struct S **s)
{
	if (s)
		*s = 0;

	return 0;
}

SEC("cgroup_skb/ingress")
int test_cls(struct __sk_buff *skb)
{
	__u32 index = 0;

	{
		const int v = foo(index++, 0);

		save_value(index++, v);
	}

	{
		struct S s = { .v = 100 };

		foo(index++, &s);
		save_value(index++, s.v);
	}

	{
		global_variable.v = 42;
		bar(index++, &global_variable);
		save_value(index++, global_variable.v);
	}

	{
		struct S v, *p = &v;

		baz(&p);
		save_value(index++, !p);
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
