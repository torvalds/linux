// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/usdt.bpf.h>

char _license[] SEC("license") = "GPL";

__u64 uprobe_multi_func_1_addr = 0;
__u64 uprobe_multi_func_2_addr = 0;
__u64 uprobe_multi_func_3_addr = 0;

__u64 uprobe_multi_func_1_result = 0;
__u64 uprobe_multi_func_2_result = 0;
__u64 uprobe_multi_func_3_result = 0;

__u64 uretprobe_multi_func_1_result = 0;
__u64 uretprobe_multi_func_2_result = 0;
__u64 uretprobe_multi_func_3_result = 0;

__u64 uprobe_multi_sleep_result = 0;

int pid = 0;
int child_pid = 0;
int child_tid = 0;
int child_pid_usdt = 0;
int child_tid_usdt = 0;

int expect_pid = 0;
bool bad_pid_seen = false;
bool bad_pid_seen_usdt = false;

bool test_cookie = false;
void *user_ptr = 0;

static __always_inline bool verify_sleepable_user_copy(void)
{
	char data[9];

	bpf_copy_from_user(data, sizeof(data), user_ptr);
	return bpf_strncmp(data, sizeof(data), "test_data") == 0;
}

static void uprobe_multi_check(void *ctx, bool is_return, bool is_sleep)
{
	__u64 cur_pid_tgid = bpf_get_current_pid_tgid();
	__u32 cur_pid;

	cur_pid = cur_pid_tgid >> 32;
	if (pid && cur_pid != pid)
		return;

	if (expect_pid && cur_pid != expect_pid)
		bad_pid_seen = true;

	child_pid = cur_pid_tgid >> 32;
	child_tid = (__u32)cur_pid_tgid;

	__u64 cookie = test_cookie ? bpf_get_attach_cookie(ctx) : 0;
	__u64 addr = bpf_get_func_ip(ctx);

#define SET(__var, __addr, __cookie) ({			\
	if (addr == __addr &&				\
	   (!test_cookie || (cookie == __cookie)))	\
		__var += 1;				\
})

	if (is_return) {
		SET(uretprobe_multi_func_1_result, uprobe_multi_func_1_addr, 2);
		SET(uretprobe_multi_func_2_result, uprobe_multi_func_2_addr, 3);
		SET(uretprobe_multi_func_3_result, uprobe_multi_func_3_addr, 1);
	} else {
		SET(uprobe_multi_func_1_result, uprobe_multi_func_1_addr, 3);
		SET(uprobe_multi_func_2_result, uprobe_multi_func_2_addr, 1);
		SET(uprobe_multi_func_3_result, uprobe_multi_func_3_addr, 2);
	}

#undef SET

	if (is_sleep && verify_sleepable_user_copy())
		uprobe_multi_sleep_result += 1;
}

SEC("uprobe.multi//proc/self/exe:uprobe_multi_func_*")
int uprobe(struct pt_regs *ctx)
{
	uprobe_multi_check(ctx, false, false);
	return 0;
}

SEC("uretprobe.multi//proc/self/exe:uprobe_multi_func_*")
int uretprobe(struct pt_regs *ctx)
{
	uprobe_multi_check(ctx, true, false);
	return 0;
}

SEC("uprobe.multi.s//proc/self/exe:uprobe_multi_func_*")
int uprobe_sleep(struct pt_regs *ctx)
{
	uprobe_multi_check(ctx, false, true);
	return 0;
}

SEC("uretprobe.multi.s//proc/self/exe:uprobe_multi_func_*")
int uretprobe_sleep(struct pt_regs *ctx)
{
	uprobe_multi_check(ctx, true, true);
	return 0;
}

SEC("uprobe.multi//proc/self/exe:uprobe_multi_func_*")
int uprobe_extra(struct pt_regs *ctx)
{
	/* we need this one just to mix PID-filtered and global uprobes */
	return 0;
}

SEC("usdt")
int usdt_pid(struct pt_regs *ctx)
{
	__u64 cur_pid_tgid = bpf_get_current_pid_tgid();
	__u32 cur_pid;

	cur_pid = cur_pid_tgid >> 32;
	if (pid && cur_pid != pid)
		return 0;

	if (expect_pid && cur_pid != expect_pid)
		bad_pid_seen_usdt = true;

	child_pid_usdt = cur_pid_tgid >> 32;
	child_tid_usdt = (__u32)cur_pid_tgid;

	return 0;
}

SEC("usdt")
int usdt_extra(struct pt_regs *ctx)
{
	/* we need this one just to mix PID-filtered and global USDT probes */
	return 0;
}
