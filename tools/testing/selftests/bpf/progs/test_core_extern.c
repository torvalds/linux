// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <stdint.h>
#include <stdbool.h>
#include <linux/ptrace.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* non-existing BPF helper, to test dead code elimination */
static int (*bpf_missing_helper)(const void *arg1, int arg2) = (void *) 999;

extern int LINUX_KERNEL_VERSION __kconfig;
extern bool CONFIG_BPF_SYSCALL __kconfig; /* strong */
extern enum libbpf_tristate CONFIG_TRISTATE __kconfig __weak;
extern bool CONFIG_BOOL __kconfig __weak;
extern char CONFIG_CHAR __kconfig __weak;
extern uint16_t CONFIG_USHORT __kconfig __weak;
extern int CONFIG_INT __kconfig __weak;
extern uint64_t CONFIG_ULONG __kconfig __weak;
extern const char CONFIG_STR[8] __kconfig __weak;
extern uint64_t CONFIG_MISSING __kconfig __weak;

uint64_t kern_ver = -1;
uint64_t bpf_syscall = -1;
uint64_t tristate_val = -1;
uint64_t bool_val = -1;
uint64_t char_val = -1;
uint64_t ushort_val = -1;
uint64_t int_val = -1;
uint64_t ulong_val = -1;
char str_val[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
uint64_t missing_val = -1;

SEC("raw_tp/sys_enter")
int handle_sys_enter(struct pt_regs *ctx)
{
	int i;

	kern_ver = LINUX_KERNEL_VERSION;
	bpf_syscall = CONFIG_BPF_SYSCALL;
	tristate_val = CONFIG_TRISTATE;
	bool_val = CONFIG_BOOL;
	char_val = CONFIG_CHAR;
	ushort_val = CONFIG_USHORT;
	int_val = CONFIG_INT;
	ulong_val = CONFIG_ULONG;

	for (i = 0; i < sizeof(CONFIG_STR); i++) {
		str_val[i] = CONFIG_STR[i];
	}

	if (CONFIG_MISSING)
		/* invalid, but dead code - never executed */
		missing_val = bpf_missing_helper(ctx, 123);
	else
		missing_val = 0xDEADC0DE;

	return 0;
}

char _license[] SEC("license") = "GPL";
