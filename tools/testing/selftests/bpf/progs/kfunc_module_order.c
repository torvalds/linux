// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

extern int bpf_test_modorder_retx(void) __ksym;
extern int bpf_test_modorder_rety(void) __ksym;

SEC("classifier")
int call_kfunc_xy(struct __sk_buff *skb)
{
	int ret1, ret2;

	ret1 = bpf_test_modorder_retx();
	ret2 = bpf_test_modorder_rety();

	return ret1 == 'x' && ret2 == 'y' ? 0 : -1;
}

SEC("classifier")
int call_kfunc_yx(struct __sk_buff *skb)
{
	int ret1, ret2;

	ret1 = bpf_test_modorder_rety();
	ret2 = bpf_test_modorder_retx();

	return ret1 == 'y' && ret2 == 'x' ? 0 : -1;
}

char _license[] SEC("license") = "GPL";
