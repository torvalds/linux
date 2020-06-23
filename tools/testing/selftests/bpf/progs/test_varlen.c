// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define MAX_LEN 256

char buf_in1[MAX_LEN] = {};
char buf_in2[MAX_LEN] = {};

int test_pid = 0;
bool capture = false;

/* .bss */
long payload1_len1 = 0;
long payload1_len2 = 0;
long total1 = 0;
char payload1[MAX_LEN + MAX_LEN] = {};

/* .data */
int payload2_len1 = -1;
int payload2_len2 = -1;
int total2 = -1;
char payload2[MAX_LEN + MAX_LEN] = { 1 };

SEC("raw_tp/sys_enter")
int handler64(void *regs)
{
	int pid = bpf_get_current_pid_tgid() >> 32;
	void *payload = payload1;
	u64 len;

	/* ignore irrelevant invocations */
	if (test_pid != pid || !capture)
		return 0;

	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in1[0]);
	if (len <= MAX_LEN) {
		payload += len;
		payload1_len1 = len;
	}

	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in2[0]);
	if (len <= MAX_LEN) {
		payload += len;
		payload1_len2 = len;
	}

	total1 = payload - (void *)payload1;

	return 0;
}

SEC("tp_btf/sys_enter")
int handler32(void *regs)
{
	int pid = bpf_get_current_pid_tgid() >> 32;
	void *payload = payload2;
	u32 len;

	/* ignore irrelevant invocations */
	if (test_pid != pid || !capture)
		return 0;

	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in1[0]);
	if (len <= MAX_LEN) {
		payload += len;
		payload2_len1 = len;
	}

	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in2[0]);
	if (len <= MAX_LEN) {
		payload += len;
		payload2_len2 = len;
	}

	total2 = payload - (void *)payload2;

	return 0;
}

SEC("tp_btf/sys_exit")
int handler_exit(void *regs)
{
	long bla;

	if (bpf_probe_read_kernel(&bla, sizeof(bla), 0))
		return 1;
	else
		return 0;
}

char LICENSE[] SEC("license") = "GPL";
