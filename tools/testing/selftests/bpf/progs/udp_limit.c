// SPDX-License-Identifier: GPL-2.0-only

#include <sys/socket.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

int invocations = 0, in_use = 0;

SEC("cgroup/sock_create")
int sock(struct bpf_sock *ctx)
{
	__u32 key;

	if (ctx->type != SOCK_DGRAM)
		return 1;

	__sync_fetch_and_add(&invocations, 1);

	if (in_use > 0) {
		/* BPF_CGROUP_INET_SOCK_RELEASE is _not_ called
		 * when we return an error from the BPF
		 * program!
		 */
		return 0;
	}

	__sync_fetch_and_add(&in_use, 1);
	return 1;
}

SEC("cgroup/sock_release")
int sock_release(struct bpf_sock *ctx)
{
	__u32 key;

	if (ctx->type != SOCK_DGRAM)
		return 1;

	__sync_fetch_and_add(&invocations, 1);
	__sync_fetch_and_add(&in_use, -1);
	return 1;
}
