// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

extern int LINUX_KERNEL_VERSION __kconfig;
/* when an extern is defined as both strong and weak, resulting symbol will be strong */
extern bool CONFIG_BPF_SYSCALL __kconfig;
extern const void __start_BTF __ksym;

int input_bss2;
int input_data2 = 2;
const volatile int input_rodata2 = 22;

int input_bss_weak __weak;
/* these two weak variables should lose */
int input_data_weak __weak = 20;
const volatile int input_rodata_weak __weak = 200;

extern int input_bss1;
extern int input_data1;
extern const int input_rodata1;

int output_bss2;
int output_data2;
int output_rodata2;

int output_sink2;

static __noinline int get_data_res(void)
{
	/* just make sure all the relocations work against .text as well */
	return input_data1 + input_data2 + input_data_weak;
}

SEC("raw_tp/sys_enter")
int BPF_PROG(handler2)
{
	output_bss2 = input_bss1 + input_bss2 + input_bss_weak;
	output_data2 = get_data_res();
	output_rodata2 = input_rodata1 + input_rodata2 + input_rodata_weak;

	/* make sure we actually use above special externs, otherwise compiler
	 * will optimize them out
	 */
	output_sink2 = LINUX_KERNEL_VERSION
		       + CONFIG_BPF_SYSCALL
		       + (long)&__start_BTF;

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
