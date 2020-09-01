// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct s {
	int a;
	long long b;
} __attribute__((packed));

/* .data section */
int in1 = -1;
long long in2 = -1;

/* .bss section */
char in3 = '\0';
long long in4 __attribute__((aligned(64))) = 0;
struct s in5 = {};

/* .rodata section */
const volatile struct {
	const int in6;
} in = {};

/* .data section */
int out1 = -1;
long long out2 = -1;

/* .bss section */
char out3 = 0;
long long out4 = 0;
int out6 = 0;

extern bool CONFIG_BPF_SYSCALL __kconfig;
extern int LINUX_KERNEL_VERSION __kconfig;
bool bpf_syscall = 0;
int kern_ver = 0;

SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	static volatile struct s out5;

	out1 = in1;
	out2 = in2;
	out3 = in3;
	out4 = in4;
	out5 = in5;
	out6 = in.in6;

	bpf_syscall = CONFIG_BPF_SYSCALL;
	kern_ver = LINUX_KERNEL_VERSION;

	return 0;
}

char _license[] SEC("license") = "GPL";
