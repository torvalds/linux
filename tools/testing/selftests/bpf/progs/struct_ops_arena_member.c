// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_experimental.h"
#include <bpf_arena_common.h>
#include "../test_kmods/bpf_testmod.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	/* page 0 hosts the arena globals, page 1 is for allocations */
	__uint(max_entries, 2);
} arena SEC(".maps");

/* also associates the callback with the arena */
u64 __arena arena_touch;
/* the register value a load of args->ptr produced in the callback */
u64 __arena member_ptr_val;

/*
 * The header is shared with the module, so the members are u64 __arena * on
 * this side too and the loads need no cast. The verifier derives the type of
 * the loaded value from the module BTF, where the members carry the tag.
 */
SEC("struct_ops/test_arena_member")
int BPF_PROG(test_arena_member_cb, struct bpf_testmod_arena_member_args *args)
{
	u64 __arena *ptr = args->ptr, *acq;
	u64 raw;

	arena_touch++;

	/* the register right after the load, untouched by the compiler */
	asm volatile("%[raw] = *(u64 *)(%[args] + %[off])"
		     : [raw]"=r"(raw)
		     : [args]"r"(args),
		       [off]"i"(offsetof(struct bpf_testmod_arena_member_args, ptr)));
	member_ptr_val = raw;

	*ptr += 1;
	/* an acquire load of the member yields the same arena pointer */
	asm volatile("r1 = %[args];\n"
		     ".8byte 0x00000100000010db;\n"	/* r0 = load_acquire((u64 *)(r1 + 0)) */
		     "%[acq] = r0;"
		     : [acq]"=r"(acq)
		     : [args]"r"(args)
		     : "r0", "r1");
	if (acq != ptr)
		return 0xbad;
	*acq += 1;
	if (args->null_ptr)
		return 0xbad;
	*args->nested.ptr += 1;
	if (args->cookie != 0xc00c1e)
		return 0xbad;
	/*
	 * @faulting is a plain kernel pointer, so the member behind it is read
	 * with a probed load, which the JIT skips for a user address or which
	 * faults, and reads as NULL rather than as an arena pointer.
	 */
	if (args->faulting->ptr)
		return 0xbad;
	/* an arena pointer to kfuncs as well, the kfunc adds one */
	bpf_kfunc_arena_arg_test((u64 *)args->ptr);
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_arena_member = {
	.test_arena_member = (void *)test_arena_member_cb,
};

SEC("syscall")
int trigger(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	u64 __arena *val;
	int ret;

	val = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!val)
		return 1;

	*val = 41;
	ret = bpf_testmod_ops3_call_test_arena_member((u64 *)val);
	if (ret)
		return 2;
	/* ptr, its acquire load, nested.ptr and the kfunc each added one */
	if (*val != 45)
		return 3;

	/*
	 * The callback saw (u32)(kaddr - kern_vm_start), the arena offset of
	 * val with the upper 32 bits clear, the same as an __arena argument.
	 */
	if (member_ptr_val != (u32)(u64)val)
		return 4;

	bpf_arena_free_pages(&arena, (void __arena *)val, 1);
#endif
	return 0;
}
