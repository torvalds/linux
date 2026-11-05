// SPDX-License-Identifier: GPL-2.0
/*
 * The arena accesses of jit_insns in their own object, so that a kernel
 * without arena support fails only these programs.
 */
#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"
#include "bpf_arena_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1);
} arena SEC(".maps");

/*
 * The first bytes in memory of the test value and its sum with the slot
 * after the u32 store depend on the byte order.
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define VAL_B0	0x88
#define VAL_H0	0x7788
#define VAL_W0	0x55667788
#define VAL_SUM	0x22446688556689bc
#else
#define VAL_B0	0x11
#define VAL_H0	0x1122
#define VAL_W0	0x11223344
#define VAL_SUM	0x11224578aaccef10
#endif

#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
SEC("syscall")
__success __retval(0)
int arena_zero_off(void *ctx)
{
	void __arena *page;
	__u64 res;

	page = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!page)
		return 1;
	res = (__u32)(__u64)page;
	/* Accesses through two base registers */
	asm volatile ("					\
	r7 = %[res];					\
	r7 = addr_space_cast(r7, 0x0, 0x1);		\
	r1 = r7;					\
	r8 = 0x1122334455667788 ll;			\
	*(u64 *)(r7 + 0) = r8;				\
	r9 = *(u64 *)(r7 + 0);				\
	r9 ^= r8;					\
	*(u64 *)(r1 + 0) = r8;				\
	r2 = *(u64 *)(r1 + 0);				\
	r2 ^= r8;					\
	r9 |= r2;					\
	r3 = *(u8 *)(r1 + 0);				\
	r3 ^= %[b0];					\
	r9 |= r3;					\
	r4 = *(u16 *)(r1 + 0);				\
	r4 ^= %[h0];					\
	r9 |= r4;					\
	r5 = *(u32 *)(r1 + 0);				\
	r5 ^= %[w0];					\
	r9 |= r5;					\
	*(u32 *)(r1 + 0) = 0x1234;			\
	lock *(u64 *)(r7 + 0) += r8;			\
	r2 = *(u64 *)(r1 + 0);				\
	r8 = %[sum] ll;					\
	r2 ^= r8;					\
	r9 |= r2;					\
	%[res] = r9;					\
"	: [res]"+r"(res)
	: __imm_const(b0, VAL_B0), __imm_const(h0, VAL_H0),
	  __imm_const(w0, VAL_W0), __imm_const(sum, VAL_SUM)
	: "r1", "r2", "r3", "r4", "r5", "r7", "r8", "r9");
	return res != 0;
}

SEC("syscall")
__success __retval(0)
int cast_to_user(void *ctx)
{
	void __arena *page;
	__u64 res;

	page = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!page)
		return 1;
	/* The user address comes back from the kernel one, NULL stays NULL */
	asm volatile ("					\
	r7 = %[page];					\
	r1 = addr_space_cast(r7, 0x0, 0x1);		\
	r1 = addr_space_cast(r1, 0x1, 0x0);		\
	r9 = r1;					\
	r9 ^= r7;					\
	r2 = 0;						\
	r2 = addr_space_cast(r2, 0x1, 0x0);		\
	r9 |= r2;					\
	%[res] = r9;					\
"	: [res]"=r"(res)
	: [page]"r"(page)
	: "r1", "r2", "r7", "r9");
	return res != 0;
}
#endif

char _license[] SEC("license") = "GPL";
