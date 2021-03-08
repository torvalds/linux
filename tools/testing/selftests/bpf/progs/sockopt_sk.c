// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <linux/tcp.h>
#include <linux/bpf.h>
#include <netinet/in.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 1;

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

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

	if (ctx->level == SOL_IP && ctx->optname == IP_TOS) {
		/* Not interested in SOL_IP:IP_TOS;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
		ctx->optlen = 0; /* bypass optval>PAGE_SIZE */
		return 1;
	}

	if (ctx->level == SOL_SOCKET && ctx->optname == SO_SNDBUF) {
		/* Not interested in SOL_SOCKET:SO_SNDBUF;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
		return 1;
	}

	if (ctx->level == SOL_TCP && ctx->optname == TCP_CONGESTION) {
		/* Not interested in SOL_TCP:TCP_CONGESTION;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
		return 1;
	}

	if (ctx->level == SOL_TCP && ctx->optname == TCP_ZEROCOPY_RECEIVE) {
		/* Verify that TCP_ZEROCOPY_RECEIVE triggers.
		 * It has a custom implementation for performance
		 * reasons.
		 */

		if (optval + sizeof(struct tcp_zerocopy_receive) > optval_end)
			return 0; /* EPERM, bounds check */

		if (((struct tcp_zerocopy_receive *)optval)->address != 0)
			return 0; /* EPERM, unexpected data */

		return 1;
	}

	if (ctx->level == SOL_IP && ctx->optname == IP_FREEBIND) {
		if (optval + 1 > optval_end)
			return 0; /* EPERM, bounds check */

		ctx->retval = 0; /* Reset system call return value to zero */

		/* Always export 0x55 */
		optval[0] = 0x55;
		ctx->optlen = 1;

		/* Userspace buffer is PAGE_SIZE * 2, but BPF
		 * program can only see the first PAGE_SIZE
		 * bytes of data.
		 */
		if (optval_end - optval != PAGE_SIZE)
			return 0; /* EPERM, unexpected data size */

		return 1;
	}

	if (ctx->level != SOL_CUSTOM)
		return 0; /* EPERM, deny everything except custom level */

	if (optval + 1 > optval_end)
		return 0; /* EPERM, bounds check */

	storage = bpf_sk_storage_get(&socket_storage_map, ctx->sk, 0,
				     BPF_SK_STORAGE_GET_F_CREATE);
	if (!storage)
		return 0; /* EPERM, couldn't get sk storage */

	if (!ctx->retval)
		return 0; /* EPERM, kernel should not have handled
			   * SOL_CUSTOM, something is wrong!
			   */
	ctx->retval = 0; /* Reset system call return value to zero */

	optval[0] = storage->val;
	ctx->optlen = 1;

	return 1;
}

SEC("cgroup/setsockopt")
int _setsockopt(struct bpf_sockopt *ctx)
{
	__u8 *optval_end = ctx->optval_end;
	__u8 *optval = ctx->optval;
	struct sockopt_sk *storage;

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
			return 0; /* EPERM, bounds check */

		*(__u32 *)optval = 0x55AA;
		ctx->optlen = 4;

		return 1;
	}

	if (ctx->level == SOL_TCP && ctx->optname == TCP_CONGESTION) {
		/* Always use cubic */

		if (optval + 5 > optval_end)
			return 0; /* EPERM, bounds check */

		memcpy(optval, "cubic", 5);
		ctx->optlen = 5;

		return 1;
	}

	if (ctx->level == SOL_IP && ctx->optname == IP_FREEBIND) {
		/* Original optlen is larger than PAGE_SIZE. */
		if (ctx->optlen != PAGE_SIZE * 2)
			return 0; /* EPERM, unexpected data size */

		if (optval + 1 > optval_end)
			return 0; /* EPERM, bounds check */

		/* Make sure we can trim the buffer. */
		optval[0] = 0;
		ctx->optlen = 1;

		/* Usepace buffer is PAGE_SIZE * 2, but BPF
		 * program can only see the first PAGE_SIZE
		 * bytes of data.
		 */
		if (optval_end - optval != PAGE_SIZE)
			return 0; /* EPERM, unexpected data size */

		return 1;
	}

	if (ctx->level != SOL_CUSTOM)
		return 0; /* EPERM, deny everything except custom level */

	if (optval + 1 > optval_end)
		return 0; /* EPERM, bounds check */

	storage = bpf_sk_storage_get(&socket_storage_map, ctx->sk, 0,
				     BPF_SK_STORAGE_GET_F_CREATE);
	if (!storage)
		return 0; /* EPERM, couldn't get sk storage */

	storage->val = optval[0];
	ctx->optlen = -1; /* BPF has consumed this option, don't call kernel
			   * setsockopt handler.
			   */

	return 1;
}
