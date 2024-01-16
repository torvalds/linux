// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define __read_mostly SEC(".data.read_mostly")

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

struct s out5 = {};


const volatile int in_dynarr_sz SEC(".rodata.dyn");
const volatile int in_dynarr[4] SEC(".rodata.dyn") = { -1, -2, -3, -4 };

int out_dynarr[4] SEC(".data.dyn") = { 1, 2, 3, 4 };

int read_mostly_var __read_mostly;
int out_mostly_var;

char huge_arr[16 * 1024 * 1024];

SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	int i;

	out1 = in1;
	out2 = in2;
	out3 = in3;
	out4 = in4;
	out5 = in5;
	out6 = in.in6;

	bpf_syscall = CONFIG_BPF_SYSCALL;
	kern_ver = LINUX_KERNEL_VERSION;

	for (i = 0; i < in_dynarr_sz; i++)
		out_dynarr[i] = in_dynarr[i];

	out_mostly_var = read_mostly_var;

	huge_arr[sizeof(huge_arr) - 1] = 123;

	return 0;
}

char _license[] SEC("license") = "GPL";
