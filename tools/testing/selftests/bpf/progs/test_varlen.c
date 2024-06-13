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
__u64 payload1_len1 = 0;
__u64 payload1_len2 = 0;
__u64 total1 = 0;
char payload1[MAX_LEN + MAX_LEN] = {};
__u64 ret_bad_read = 0;

/* .data */
int payload2_len1 = -1;
int payload2_len2 = -1;
int total2 = -1;
char payload2[MAX_LEN + MAX_LEN] = { 1 };

int payload3_len1 = -1;
int payload3_len2 = -1;
int total3= -1;
char payload3[MAX_LEN + MAX_LEN] = { 1 };

int payload4_len1 = -1;
int payload4_len2 = -1;
int total4= -1;
char payload4[MAX_LEN + MAX_LEN] = { 1 };

char payload_bad[5] = { 0x42, 0x42, 0x42, 0x42, 0x42 };

SEC("raw_tp/sys_enter")
int handler64_unsigned(void *regs)
{
	int pid = bpf_get_current_pid_tgid() >> 32;
	void *payload = payload1;
	long len;

	/* ignore irrelevant invocations */
	if (test_pid != pid || !capture)
		return 0;

	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in1[0]);
	if (len >= 0) {
		payload += len;
		payload1_len1 = len;
	}

	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in2[0]);
	if (len >= 0) {
		payload += len;
		payload1_len2 = len;
	}

	total1 = payload - (void *)payload1;

	ret_bad_read = bpf_probe_read_kernel_str(payload_bad + 2, 1, (void *) -1);

	return 0;
}

SEC("raw_tp/sys_exit")
int handler64_signed(void *regs)
{
	int pid = bpf_get_current_pid_tgid() >> 32;
	void *payload = payload3;
	long len;

	/* ignore irrelevant invocations */
	if (test_pid != pid || !capture)
		return 0;

	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in1[0]);
	if (len >= 0) {
		payload += len;
		payload3_len1 = len;
	}
	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in2[0]);
	if (len >= 0) {
		payload += len;
		payload3_len2 = len;
	}
	total3 = payload - (void *)payload3;

	return 0;
}

SEC("tp/raw_syscalls/sys_enter")
int handler32_unsigned(void *regs)
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

SEC("tp/raw_syscalls/sys_exit")
int handler32_signed(void *regs)
{
	int pid = bpf_get_current_pid_tgid() >> 32;
	void *payload = payload4;
	long len;

	/* ignore irrelevant invocations */
	if (test_pid != pid || !capture)
		return 0;

	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in1[0]);
	if (len >= 0) {
		payload += len;
		payload4_len1 = len;
	}
	len = bpf_probe_read_kernel_str(payload, MAX_LEN, &buf_in2[0]);
	if (len >= 0) {
		payload += len;
		payload4_len2 = len;
	}
	total4 = payload - (void *)payload4;

	return 0;
}

SEC("tp/syscalls/sys_exit_getpid")
int handler_exit(void *regs)
{
	long bla;

	if (bpf_probe_read_kernel(&bla, sizeof(bla), 0))
		return 1;
	else
		return 0;
}

char LICENSE[] SEC("license") = "GPL";
