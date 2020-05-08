// SPDX-License-Identifier: GPL-2.0
#include <string.h>

#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <sys/socket.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

char _license[] SEC("license") = "GPL";
int _version SEC("version") = 1;

SEC("cgroup/connect4")
int _connect4(struct bpf_sock_addr *ctx)
{
	struct sockaddr_in sa = {};

	sa.sin_family = AF_INET;
	sa.sin_port = bpf_htons(22222);
	sa.sin_addr.s_addr = bpf_htonl(0x7f000001); /* 127.0.0.1 */

	if (bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)) != 0)
		return 0;

	return 1;
}
