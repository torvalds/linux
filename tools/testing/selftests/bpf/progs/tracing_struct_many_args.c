// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

struct bpf_testmod_struct_arg_4 {
	u64 a;
	int b;
};

long t7_a, t7_b, t7_c, t7_d, t7_e, t7_f_a, t7_f_b, t7_ret;
long t8_a, t8_b, t8_c, t8_d, t8_e, t8_f_a, t8_f_b, t8_g, t8_ret;

SEC("fentry/bpf_testmod_test_struct_arg_7")
int BPF_PROG2(test_struct_many_args_1, __u64, a, void *, b, short, c, int, d,
	      void *, e, struct bpf_testmod_struct_arg_4, f)
{
	t7_a = a;
	t7_b = (long)b;
	t7_c = c;
	t7_d = d;
	t7_e = (long)e;
	t7_f_a = f.a;
	t7_f_b = f.b;
	return 0;
}

SEC("fexit/bpf_testmod_test_struct_arg_7")
int BPF_PROG2(test_struct_many_args_2, __u64, a, void *, b, short, c, int, d,
	      void *, e, struct bpf_testmod_struct_arg_4, f, int, ret)
{
	t7_ret = ret;
	return 0;
}

SEC("fentry/bpf_testmod_test_struct_arg_8")
int BPF_PROG2(test_struct_many_args_3, __u64, a, void *, b, short, c, int, d,
	      void *, e, struct bpf_testmod_struct_arg_4, f, int, g)
{
	t8_a = a;
	t8_b = (long)b;
	t8_c = c;
	t8_d = d;
	t8_e = (long)e;
	t8_f_a = f.a;
	t8_f_b = f.b;
	t8_g = g;
	return 0;
}

SEC("fexit/bpf_testmod_test_struct_arg_8")
int BPF_PROG2(test_struct_many_args_4, __u64, a, void *, b, short, c, int, d,
	      void *, e, struct bpf_testmod_struct_arg_4, f, int, g,
	      int, ret)
{
	t8_ret = ret;
	return 0;
}

char _license[] SEC("license") = "GPL";
