// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Google LLC. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

__u32 pid = 0;

char num_out[64] = {};
long num_ret = 0;

char ip_out[64] = {};
long ip_ret = 0;

char sym_out[64] = {};
long sym_ret = 0;

char addr_out[64] = {};
long addr_ret = 0;

char str_out[64] = {};
long str_ret = 0;

char over_out[6] = {};
long over_ret = 0;

char pad_out[10] = {};
long pad_ret = 0;

char noarg_out[64] = {};
long noarg_ret = 0;

long nobuf_ret = 0;

extern const void schedule __ksym;

SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	/* Convenient values to pretty-print */
	const __u8 ex_ipv4[] = {127, 0, 0, 1};
	const __u8 ex_ipv6[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
	static const char str1[] = "str1";
	static const char longstr[] = "longstr";

	if ((int)bpf_get_current_pid_tgid() != pid)
		return 0;

	/* Integer types */
	num_ret  = BPF_SNPRINTF(num_out, sizeof(num_out),
				"%d %u %x %li %llu %lX",
				-8, 9, 150, -424242, 1337, 0xDABBAD00);
	/* IP addresses */
	ip_ret   = BPF_SNPRINTF(ip_out, sizeof(ip_out), "%pi4 %pI6",
				&ex_ipv4, &ex_ipv6);
	/* Symbol lookup formatting */
	sym_ret  = BPF_SNPRINTF(sym_out,  sizeof(sym_out), "%ps %pS %pB",
				&schedule, &schedule, &schedule);
	/* Kernel pointers */
	addr_ret = BPF_SNPRINTF(addr_out, sizeof(addr_out), "%pK %px %p",
				0, 0xFFFF00000ADD4E55, 0xFFFF00000ADD4E55);
	/* Strings embedding */
	str_ret  = BPF_SNPRINTF(str_out, sizeof(str_out), "%s %+05s",
				str1, longstr);
	/* Overflow */
	over_ret = BPF_SNPRINTF(over_out, sizeof(over_out), "%%overflow");
	/* Padding of fixed width numbers */
	pad_ret = BPF_SNPRINTF(pad_out, sizeof(pad_out), "%5d %0900000X", 4, 4);
	/* No args */
	noarg_ret = BPF_SNPRINTF(noarg_out, sizeof(noarg_out), "simple case");
	/* No buffer */
	nobuf_ret = BPF_SNPRINTF(NULL, 0, "only interested in length %d", 60);

	return 0;
}

char _license[] SEC("license") = "GPL";
