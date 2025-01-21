// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <errno.h>
#include "bpf_misc.h"

u32 dynamic_sz = 1;
int kprobe2_res = 0;
int kretprobe2_res = 0;
int uprobe_byname_res = 0;
int uretprobe_byname_res = 0;
int uprobe_byname2_res = 0;
int uretprobe_byname2_res = 0;
int uprobe_byname3_sleepable_res = 0;
int uprobe_byname3_str_sleepable_res = 0;
int uprobe_byname3_res = 0;
int uretprobe_byname3_sleepable_res = 0;
int uretprobe_byname3_str_sleepable_res = 0;
int uretprobe_byname3_res = 0;
void *user_ptr = 0;

int bpf_copy_from_user_str(void *dst, u32, const void *, u64) __weak __ksym;

SEC("ksyscall/nanosleep")
int BPF_KSYSCALL(handle_kprobe_auto, struct __kernel_timespec *req, struct __kernel_timespec *rem)
{
	kprobe2_res = 11;
	return 0;
}

SEC("kretsyscall/nanosleep")
int BPF_KRETPROBE(handle_kretprobe_auto, int ret)
{
	kretprobe2_res = 22;
	return ret;
}

SEC("uprobe")
int handle_uprobe_ref_ctr(struct pt_regs *ctx)
{
	return 0;
}

SEC("uretprobe")
int handle_uretprobe_ref_ctr(struct pt_regs *ctx)
{
	return 0;
}

SEC("uprobe")
int handle_uprobe_byname(struct pt_regs *ctx)
{
	uprobe_byname_res = 5;
	return 0;
}

/* use auto-attach format for section definition. */
SEC("uretprobe//proc/self/exe:trigger_func2")
int handle_uretprobe_byname(struct pt_regs *ctx)
{
	uretprobe_byname_res = 6;
	return 0;
}

SEC("uprobe")
int BPF_UPROBE(handle_uprobe_byname2, const char *pathname, const char *mode)
{
	char mode_buf[2] = {};

	/* verify fopen mode */
	bpf_probe_read_user(mode_buf, sizeof(mode_buf), mode);
	if (mode_buf[0] == 'r' && mode_buf[1] == 0)
		uprobe_byname2_res = 7;
	return 0;
}

SEC("uretprobe")
int BPF_URETPROBE(handle_uretprobe_byname2, void *ret)
{
	uretprobe_byname2_res = 8;
	return 0;
}

static __always_inline bool verify_sleepable_user_copy(void)
{
	char data[9];

	bpf_copy_from_user(data, sizeof(data), user_ptr);
	return bpf_strncmp(data, sizeof(data), "test_data") == 0;
}

static __always_inline bool verify_sleepable_user_copy_str(void)
{
	int ret;
	char data_long[20];
	char data_long_pad[20];
	char data_long_err[20];
	char data_short[4];
	char data_short_pad[4];

	ret = bpf_copy_from_user_str(data_short, sizeof(data_short), user_ptr, 0);

	if (bpf_strncmp(data_short, 4, "tes\0") != 0 || ret != 4)
		return false;

	ret = bpf_copy_from_user_str(data_short_pad, sizeof(data_short_pad), user_ptr, BPF_F_PAD_ZEROS);

	if (bpf_strncmp(data_short, 4, "tes\0") != 0 || ret != 4)
		return false;

	/* Make sure this passes the verifier */
	ret = bpf_copy_from_user_str(data_long, dynamic_sz & sizeof(data_long), user_ptr, 0);

	if (ret != 0)
		return false;

	ret = bpf_copy_from_user_str(data_long, sizeof(data_long), user_ptr, 0);

	if (bpf_strncmp(data_long, 10, "test_data\0") != 0 || ret != 10)
		return false;

	ret = bpf_copy_from_user_str(data_long_pad, sizeof(data_long_pad), user_ptr, BPF_F_PAD_ZEROS);

	if (bpf_strncmp(data_long_pad, 10, "test_data\0") != 0 || ret != 10 || data_long_pad[19] != '\0')
		return false;

	ret = bpf_copy_from_user_str(data_long_err, sizeof(data_long_err), (void *)data_long, BPF_F_PAD_ZEROS);

	if (ret > 0 || data_long_err[19] != '\0')
		return false;

	ret = bpf_copy_from_user_str(data_long, sizeof(data_long), user_ptr, 2);

	if (ret != -EINVAL)
		return false;

	return true;
}

SEC("uprobe.s//proc/self/exe:trigger_func3")
int handle_uprobe_byname3_sleepable(struct pt_regs *ctx)
{
	if (verify_sleepable_user_copy())
		uprobe_byname3_sleepable_res = 9;
	if (verify_sleepable_user_copy_str())
		uprobe_byname3_str_sleepable_res = 10;
	return 0;
}

/**
 * same target as the uprobe.s above to force sleepable and non-sleepable
 * programs in the same bpf_prog_array
 */
SEC("uprobe//proc/self/exe:trigger_func3")
int handle_uprobe_byname3(struct pt_regs *ctx)
{
	uprobe_byname3_res = 11;
	return 0;
}

SEC("uretprobe.s//proc/self/exe:trigger_func3")
int handle_uretprobe_byname3_sleepable(struct pt_regs *ctx)
{
	if (verify_sleepable_user_copy())
		uretprobe_byname3_sleepable_res = 12;
	if (verify_sleepable_user_copy_str())
		uretprobe_byname3_str_sleepable_res = 13;
	return 0;
}

SEC("uretprobe//proc/self/exe:trigger_func3")
int handle_uretprobe_byname3(struct pt_regs *ctx)
{
	uretprobe_byname3_res = 14;
	return 0;
}


char _license[] SEC("license") = "GPL";
