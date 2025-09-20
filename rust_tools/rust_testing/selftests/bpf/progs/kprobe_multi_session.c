// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>
#include "bpf_kfuncs.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

extern const void bpf_fentry_test1 __ksym;
extern const void bpf_fentry_test2 __ksym;
extern const void bpf_fentry_test3 __ksym;
extern const void bpf_fentry_test4 __ksym;
extern const void bpf_fentry_test5 __ksym;
extern const void bpf_fentry_test6 __ksym;
extern const void bpf_fentry_test7 __ksym;
extern const void bpf_fentry_test8 __ksym;

int pid = 0;

__u64 kprobe_session_result[8];

static int session_check(void *ctx)
{
	unsigned int i;
	__u64 addr;
	const void *kfuncs[] = {
		&bpf_fentry_test1,
		&bpf_fentry_test2,
		&bpf_fentry_test3,
		&bpf_fentry_test4,
		&bpf_fentry_test5,
		&bpf_fentry_test6,
		&bpf_fentry_test7,
		&bpf_fentry_test8,
	};

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 1;

	addr = bpf_get_func_ip(ctx);

	for (i = 0; i < ARRAY_SIZE(kfuncs); i++) {
		if (kfuncs[i] == (void *) addr) {
			kprobe_session_result[i]++;
			break;
		}
	}

	/*
	 * Force probes for function bpf_fentry_test[5-8] not to
	 * install and execute the return probe
	 */
	if (((const void *) addr == &bpf_fentry_test5) ||
	    ((const void *) addr == &bpf_fentry_test6) ||
	    ((const void *) addr == &bpf_fentry_test7) ||
	    ((const void *) addr == &bpf_fentry_test8))
		return 1;

	return 0;
}

/*
 * No tests in here, just to trigger 'bpf_fentry_test*'
 * through tracing test_run
 */
SEC("fentry/bpf_modify_return_test")
int BPF_PROG(trigger)
{
	return 0;
}

SEC("kprobe.session/bpf_fentry_test*")
int test_kprobe(struct pt_regs *ctx)
{
	return session_check(ctx);
}
