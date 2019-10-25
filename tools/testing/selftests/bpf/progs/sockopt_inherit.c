// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include "bpf_helpers.h"

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 1;

#define SOL_CUSTOM			0xdeadbeef
#define CUSTOM_INHERIT1			0
#define CUSTOM_INHERIT2			1
#define CUSTOM_LISTENER			2

struct sockopt_inherit {
	__u8 val;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC | BPF_F_CLONE);
	__type(key, int);
	__type(value, struct sockopt_inherit);
} cloned1_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC | BPF_F_CLONE);
	__type(key, int);
	__type(value, struct sockopt_inherit);
} cloned2_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct sockopt_inherit);
} listener_only_map SEC(".maps");

static __inline struct sockopt_inherit *get_storage(struct bpf_sockopt *ctx)
{
	if (ctx->optname == CUSTOM_INHERIT1)
		return bpf_sk_storage_get(&cloned1_map, ctx->sk, 0,
					  BPF_SK_STORAGE_GET_F_CREATE);
	else if (ctx->optname == CUSTOM_INHERIT2)
		return bpf_sk_storage_get(&cloned2_map, ctx->sk, 0,
					  BPF_SK_STORAGE_GET_F_CREATE);
	else
		return bpf_sk_storage_get(&listener_only_map, ctx->sk, 0,
					  BPF_SK_STORAGE_GET_F_CREATE);
}

SEC("cgroup/getsockopt")
int _getsockopt(struct bpf_sockopt *ctx)
{
	__u8 *optval_end = ctx->optval_end;
	struct sockopt_inherit *storage;
	__u8 *optval = ctx->optval;

	if (ctx->level != SOL_CUSTOM)
		return 1; /* only interested in SOL_CUSTOM */

	if (optval + 1 > optval_end)
		return 0; /* EPERM, bounds check */

	storage = get_storage(ctx);
	if (!storage)
		return 0; /* EPERM, couldn't get sk storage */

	ctx->retval = 0; /* Reset system call return value to zero */

	optval[0] = storage->val;
	ctx->optlen = 1;

	return 1;
}

SEC("cgroup/setsockopt")
int _setsockopt(struct bpf_sockopt *ctx)
{
	__u8 *optval_end = ctx->optval_end;
	struct sockopt_inherit *storage;
	__u8 *optval = ctx->optval;

	if (ctx->level != SOL_CUSTOM)
		return 1; /* only interested in SOL_CUSTOM */

	if (optval + 1 > optval_end)
		return 0; /* EPERM, bounds check */

	storage = get_storage(ctx);
	if (!storage)
		return 0; /* EPERM, couldn't get sk storage */

	storage->val = optval[0];
	ctx->optlen = -1;

	return 1;
}
