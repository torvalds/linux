/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 Hengqi Chen */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_tracing_net.h"

const volatile pid_t my_pid = 0;
char path[256] = {};

SEC("fentry/unix_listen")
int BPF_PROG(unix_listen, struct socket *sock, int backlog)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;
	struct unix_sock *unix_sk;
	int i, len;

	if (pid != my_pid)
		return 0;

	unix_sk = (struct unix_sock *)bpf_skc_to_unix_sock(sock->sk);
	if (!unix_sk)
		return 0;

	if (unix_sk->addr->name->sun_path[0])
		return 0;

	len = unix_sk->addr->len - sizeof(short);
	path[0] = '@';
	for (i = 1; i < len; i++) {
		if (i >= (int)sizeof(struct sockaddr_un))
			break;

		path[i] = unix_sk->addr->name->sun_path[i];
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
