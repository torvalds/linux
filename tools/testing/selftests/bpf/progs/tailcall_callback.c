// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_test_utils.h"

int classifier_0(struct __sk_buff *skb);

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__array(values, void (void));
} jmp_table SEC(".maps") = {
	.values = {
		[0] = (void *) &classifier_0,
	},
};

__auxiliary
SEC("tc")
int classifier_0(struct __sk_buff *skb)
{
	return 0;
}

static __noinline
int subprog_tail0(struct __sk_buff *skb)
{
	int ret = 0;

	bpf_tail_call_static(skb, &jmp_table, 0);
	barrier_var(ret);
	return ret;
}

static __noinline
int callback_loop(int index, void **cb_ctx)
{
	int ret;

	ret = subprog_tail0(*cb_ctx);
	barrier_var(ret);
	return ret ? 1 : 0;
}

static __noinline
int callback_empty(int index, void *data)
{
	return 0;
}

/* callback involving subprog with tail call is rejected */
SEC("tc")
__failure __msg("cannot tail call within callback")
int tailcall_callback_1(struct __sk_buff *skb)
{
	clobber_regs_stack();

	bpf_loop(1, callback_loop, &skb, 0);
	return 0;
}

/* subprogs with tailcall do not affect no-tailcall callback */
SEC("tc")
__success
__retval(0)
int tailcall_callback_2(struct __sk_buff *skb)
{
	int ret;

	clobber_regs_stack();

	ret = subprog_tail0(skb);
	__sink(ret);

	bpf_loop(1, callback_empty, NULL, 0);
	return 0;
}

char __license[] SEC("license") = "GPL";
