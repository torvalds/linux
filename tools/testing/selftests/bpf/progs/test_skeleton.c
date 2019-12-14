// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <linux/bpf.h>
#include "bpf_helpers.h"

struct s {
	int a;
	long long b;
} __attribute__((packed));

int in1 = 0;
long long in2 = 0;
char in3 = '\0';
long long in4 __attribute__((aligned(64))) = 0;
struct s in5 = {};

long long out2 = 0;
char out3 = 0;
long long out4 = 0;
int out1 = 0;


SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	static volatile struct s out5;

	out1 = in1;
	out2 = in2;
	out3 = in3;
	out4 = in4;
	out5 = in5;
	return 0;
}

char _license[] SEC("license") = "GPL";
