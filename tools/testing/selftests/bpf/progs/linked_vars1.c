// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

extern int LINUX_KERNEL_VERSION __kconfig;
/* this weak extern will be strict due to the other file's strong extern */
extern bool CONFIG_BPF_SYSCALL __kconfig __weak;
extern const void bpf_link_fops __ksym __weak;

int input_bss1;
int input_data1 = 1;
const volatile int input_rodata1 = 11;

int input_bss_weak __weak;
/* these two definitions should win */
int input_data_weak __weak = 10;
const volatile int input_rodata_weak __weak = 100;

extern int input_bss2;
extern int input_data2;
extern const int input_rodata2;

int output_bss1;
int output_data1;
int output_rodata1;

long output_sink1;

static __noinline int get_bss_res(void)
{
	/* just make sure all the relocations work against .text as well */
	return input_bss1 + input_bss2 + input_bss_weak;
}

SEC("raw_tp/sys_enter")
int BPF_PROG(handler1)
{
	output_bss1 = get_bss_res();
	output_data1 = input_data1 + input_data2 + input_data_weak;
	output_rodata1 = input_rodata1 + input_rodata2 + input_rodata_weak;

	/* make sure we actually use above special externs, otherwise compiler
	 * will optimize them out
	 */
	output_sink1 = LINUX_KERNEL_VERSION
		       + CONFIG_BPF_SYSCALL
		       + (long)&bpf_link_fops;
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
