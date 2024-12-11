// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>
#include "bpf_kfuncs.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

int pid = 0;

int idx_entry = 0;
int idx_return = 0;

__u64 test_uprobe_cookie_entry[6];
__u64 test_uprobe_cookie_return[3];

static int check_cookie(void)
{
	__u64 *cookie = bpf_session_cookie();

	if (bpf_session_is_return()) {
		if (idx_return >= ARRAY_SIZE(test_uprobe_cookie_return))
			return 1;
		test_uprobe_cookie_return[idx_return++] = *cookie;
		return 0;
	}

	if (idx_entry >= ARRAY_SIZE(test_uprobe_cookie_entry))
		return 1;
	*cookie = test_uprobe_cookie_entry[idx_entry];
	return idx_entry++ % 2;
}


SEC("uprobe.session//proc/self/exe:uprobe_session_recursive")
int uprobe_recursive(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 1;

	return check_cookie();
}
