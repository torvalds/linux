// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__u32 monitored_tid;

int sig_keyring_serial;
int sig_keyring_type;
int sig_verdict;
int seen;

SEC("lsm/bpf_prog_load")
int BPF_PROG(inspect_prog_load, struct bpf_prog *prog, union bpf_attr *attr,
	     struct bpf_token *token, bool kernel)
{
	__u32 tid = bpf_get_current_pid_tgid() & 0xffffffff;

	if (!monitored_tid || tid != monitored_tid)
		return 0;

	seen++;
	sig_keyring_serial = prog->aux->sig.keyring_serial;
	sig_keyring_type = prog->aux->sig.keyring_type;
	sig_verdict = prog->aux->sig.verdict;
	return 0;
}
