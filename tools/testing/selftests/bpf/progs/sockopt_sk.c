// SPDX-License-Identifier: GPL-2.0
#include <netinet/in.h>
#include <linux/bpf.h>
#include "bpf_helpers.h"

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 1;

#define SOL_CUSTOM			0xdeadbeef

struct sockopt_sk {
	__u8 val;
};

struct bpf_map_def SEC("maps") socket_storage_map = {
	.type = BPF_MAP_TYPE_SK_STORAGE,
	.key_size = sizeof(int),
	.value_size = sizeof(struct sockopt_sk),
	.map_flags = BPF_F_NO_PREALLOC,
};
BPF_ANNOTATE_KV_PAIR(socket_storage_map, int, struct sockopt_sk);

SEC("cgroup/getsockopt")
int _getsockopt(struct bpf_sockopt *ctx)
{
	__u8 *optval_end = ctx->optval_end;
	__u8 *optval = ctx->optval;
	struct sockopt_sk *storage;

	if (ctx->level == SOL_IP && ctx->optname == IP_TOS)
		/* Not interested in SOL_IP:IP_TOS;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
		return 1;

	if (ctx->level == SOL_SOCKET && ctx->optname == SO_SNDBUF) {
		/* Not interested in SOL_SOCKET:SO_SNDBUF;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
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

	if (ctx->level == SOL_IP && ctx->optname == IP_TOS)
		/* Not interested in SOL_IP:IP_TOS;
		 * let next BPF program in the cgroup chain or kernel
		 * handle it.
		 */
		return 1;

	if (ctx->level == SOL_SOCKET && ctx->optname == SO_SNDBUF) {
		/* Overwrite SO_SNDBUF value */

		if (optval + sizeof(__u32) > optval_end)
			return 0; /* EPERM, bounds check */

		*(__u32 *)optval = 0x55AA;
		ctx->optlen = 4;

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
