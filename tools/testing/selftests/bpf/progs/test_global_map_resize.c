// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

/* rodata section */
const volatile pid_t pid;
const volatile size_t bss_array_len;
const volatile size_t data_array_len;

/* bss section */
int sum = 0;
int array[1];

/* custom data secton */
int my_array[1] SEC(".data.custom");

/* custom data section which should NOT be resizable,
 * since it contains a single var which is not an array
 */
int my_int SEC(".data.non_array");

/* custom data section which should NOT be resizable,
 * since its last var is not an array
 */
int my_array_first[1] SEC(".data.array_not_last");
int my_int_last SEC(".data.array_not_last");

int percpu_arr[1] SEC(".data.percpu_arr");

SEC("tp/syscalls/sys_enter_getpid")
int bss_array_sum(void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	/* this will be zero, we just rely on verifier not rejecting this */
	sum = percpu_arr[bpf_get_smp_processor_id()];

	for (size_t i = 0; i < bss_array_len; ++i)
		sum += array[i];

	return 0;
}

SEC("tp/syscalls/sys_enter_getuid")
int data_array_sum(void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	/* this will be zero, we just rely on verifier not rejecting this */
	sum = percpu_arr[bpf_get_smp_processor_id()];

	for (size_t i = 0; i < data_array_len; ++i)
		sum += my_array[i];

	return 0;
}
