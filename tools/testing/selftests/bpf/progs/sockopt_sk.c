// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <linux/tcp.h>
#include <linux/bpf.h>
#include <netinet/in.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

int page_size = 0; /* userspace should set it */

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

#define SOL_CUSTOM			0xdeadbeef

struct sockopt_sk {
	__u8 val;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct sockopt_sk);
} socket_storage_map SEC(".maps");

SEC("cgroup/getsockopt")
int _getsockopt(struct bpf_sockopt *ctx)
{
	__u8 *optval_end = ctx->optval_end;
	__u8 *optval = ctx->optval;
	struct sockopt_sk *storage;
	struct bpf_sock *sk;

	/* Bypass AF_NETLINK. */
	sk = ctx->sk;
	if (sk && sk->family == AF_NETLINK)
		goto out;

	/* Make sure bpf_get_netns_cookie is callable.
	 */
	if (bpf_get_netns_cookie(NULL) == 0)
		return 0;

	if (bpf_get_netns_cookie(ctx) == 0)
		return 0;

	if (ctx->level == SOL_IP && ctx->optname == IP_TOS) {
		/* Not interested in SOL_IP:IP_TOS;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
		goto out;
	}

	if (ctx->level == SOL_SOCKET && ctx->optname == SO_SNDBUF) {
		/* Not interested in SOL_SOCKET:SO_SNDBUF;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
		goto out;
	}

	if (ctx->level == SOL_TCP && ctx->optname == TCP_CONGESTION) {
		/* Not interested in SOL_TCP:TCP_CONGESTION;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
		goto out;
	}

	if (ctx->level == SOL_TCP && ctx->optname == TCP_ZEROCOPY_RECEIVE) {
		/* Verify that TCP_ZEROCOPY_RECEIVE triggers.
		 * It has a custom implementation for performance
		 * reasons.
		 */

		/* Check that optval contains address (__u64) */
		if (optval + sizeof(__u64) > optval_end)
			return 0; /* bounds check */

		if (((struct tcp_zerocopy_receive *)optval)->address != 0)
			return 0; /* unexpected data */

		goto out;
	}

	if (ctx->level == SOL_IP && ctx->optname == IP_FREEBIND) {
		if (optval + 1 > optval_end)
			return 0; /* bounds check */

		ctx->retval = 0; /* Reset system call return value to zero */

		/* Always export 0x55 */
		optval[0] = 0x55;
		ctx->optlen = 1;

		/* Userspace buffer is PAGE_SIZE * 2, but BPF
		 * program can only see the first PAGE_SIZE
		 * bytes of data.
		 */
		if (optval_end - optval != page_size)
			return 0; /* unexpected data size */

		return 1;
	}

	if (ctx->level != SOL_CUSTOM)
		return 0; /* deny everything except custom level */

	if (optval + 1 > optval_end)
		return 0; /* bounds check */

	storage = bpf_sk_storage_get(&socket_storage_map, ctx->sk, 0,
				     BPF_SK_STORAGE_GET_F_CREATE);
	if (!storage)
		return 0; /* couldn't get sk storage */

	if (!ctx->retval)
		return 0; /* kernel should not have handled
			   * SOL_CUSTOM, something is wrong!
			   */
	ctx->retval = 0; /* Reset system call return value to zero */

	optval[0] = storage->val;
	ctx->optlen = 1;

	return 1;

out:
	/* optval larger than PAGE_SIZE use kernel's buffer. */
	if (ctx->optlen > page_size)
		ctx->optlen = 0;
	return 1;
}

SEC("cgroup/setsockopt")
int _setsockopt(struct bpf_sockopt *ctx)
{
	__u8 *optval_end = ctx->optval_end;
	__u8 *optval = ctx->optval;
	struct sockopt_sk *storage;
	struct bpf_sock *sk;

	/* Bypass AF_NETLINK. */
	sk = ctx->sk;
	if (sk && sk->family == AF_NETLINK)
		goto out;

	/* Make sure bpf_get_netns_cookie is callable.
	 */
	if (bpf_get_netns_cookie(NULL) == 0)
		return 0;

	if (bpf_get_netns_cookie(ctx) == 0)
		return 0;

	if (ctx->level == SOL_IP && ctx->optname == IP_TOS) {
		/* Not interested in SOL_IP:IP_TOS;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
		ctx->optlen = 0; /* bypass optval>PAGE_SIZE */
		return 1;
	}

	if (ctx->level == SOL_SOCKET && ctx->optname == SO_SNDBUF) {
		/* Overwrite SO_SNDBUF value */

		if (optval + sizeof(__u32) > optval_end)
			return 0; /* bounds check */

		*(__u32 *)optval = 0x55AA;
		ctx->optlen = 4;

		return 1;
	}

	if (ctx->level == SOL_TCP && ctx->optname == TCP_CONGESTION) {
		/* Always use cubic */

		if (optval + 5 > optval_end)
			return 0; /* bounds check */

		memcpy(optval, "cubic", 5);
		ctx->optlen = 5;

		return 1;
	}

	if (ctx->level == SOL_IP && ctx->optname == IP_FREEBIND) {
		/* Original optlen is larger than PAGE_SIZE. */
		if (ctx->optlen != page_size * 2)
			return 0; /* unexpected data size */

		if (optval + 1 > optval_end)
			return 0; /* bounds check */

		/* Make sure we can trim the buffer. */
		optval[0] = 0;
		ctx->optlen = 1;

		/* Usepace buffer is PAGE_SIZE * 2, but BPF
		 * program can only see the first PAGE_SIZE
		 * bytes of data.
		 */
		if (optval_end - optval != page_size)
			return 0; /* unexpected data size */

		return 1;
	}

	if (ctx->level != SOL_CUSTOM)
		return 0; /* deny everything except custom level */

	if (optval + 1 > optval_end)
		return 0; /* bounds check */

	storage = bpf_sk_storage_get(&socket_storage_map, ctx->sk, 0,
				     BPF_SK_STORAGE_GET_F_CREATE);
	if (!storage)
		return 0; /* couldn't get sk storage */

	storage->val = optval[0];
	ctx->optlen = -1; /* BPF has consumed this option, don't call kernel
			   * setsockopt handler.
			   */

	return 1;

out:
	/* optval larger than PAGE_SIZE use kernel's buffer. */
	if (ctx->optlen > page_size)
		ctx->optlen = 0;
	return 1;
}
