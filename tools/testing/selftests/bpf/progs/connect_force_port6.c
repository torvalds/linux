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

SEC("cgroup/connect6")
int _connect6(struct bpf_sock_addr *ctx)
{
	struct sockaddr_in6 sa = {};

	sa.sin6_family = AF_INET6;
	sa.sin6_port = bpf_htons(22223);
	sa.sin6_addr.s6_addr32[3] = bpf_htonl(1); /* ::1 */

	if (bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)) != 0)
		return 0;

	return 1;
}
