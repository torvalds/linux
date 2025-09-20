// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/cgroup_storage.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
	__uint(max_entries, 0);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, char[TEST_DATA_LEN]);
} cgroup_storage SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE);
	__uint(max_entries, 0);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, char[64]);
} percpu_cgroup_storage SEC(".maps");

SEC("cgroup/skb")
__description("valid cgroup storage access")
__success __success_unpriv __retval(0)
__naked void valid_cgroup_storage_access(void)
{
	asm volatile ("					\
	r2 = 0;						\
	r1 = %[cgroup_storage] ll;			\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 0);				\
	r0 = r1;					\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(cgroup_storage)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid cgroup storage access 1")
__failure __msg("cannot pass map_type 1 into func bpf_get_local_storage")
__failure_unpriv
__naked void invalid_cgroup_storage_access_1(void)
{
	asm volatile ("					\
	r2 = 0;						\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 0);				\
	r0 = r1;					\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid cgroup storage access 2")
__failure __msg("fd 1 is not pointing to valid bpf_map")
__failure_unpriv
__naked void invalid_cgroup_storage_access_2(void)
{
	asm volatile ("					\
	r2 = 0;						\
	.8byte %[ld_map_fd];				\
	.8byte 0;					\
	call %[bpf_get_local_storage];			\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_insn(ld_map_fd, BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, BPF_REG_1, BPF_PSEUDO_MAP_FD, 0, 1))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid cgroup storage access 3")
__failure __msg("invalid access to map value, value_size=64 off=256 size=4")
__failure_unpriv
__naked void invalid_cgroup_storage_access_3(void)
{
	asm volatile ("					\
	r2 = 0;						\
	r1 = %[cgroup_storage] ll;			\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 256);				\
	r1 += 1;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(cgroup_storage)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid cgroup storage access 4")
__failure __msg("invalid access to map value, value_size=64 off=-2 size=4")
__failure_unpriv
__flag(BPF_F_ANY_ALIGNMENT)
__naked void invalid_cgroup_storage_access_4(void)
{
	asm volatile ("					\
	r2 = 0;						\
	r1 = %[cgroup_storage] ll;			\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 - 2);				\
	r0 = r1;					\
	r1 += 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(cgroup_storage)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid cgroup storage access 5")
__failure __msg("get_local_storage() doesn't support non-zero flags")
__failure_unpriv
__naked void invalid_cgroup_storage_access_5(void)
{
	asm volatile ("					\
	r2 = 7;						\
	r1 = %[cgroup_storage] ll;			\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 0);				\
	r0 = r1;					\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(cgroup_storage)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid cgroup storage access 6")
__failure __msg("get_local_storage() doesn't support non-zero flags")
__msg_unpriv("R2 leaks addr into helper function")
__naked void invalid_cgroup_storage_access_6(void)
{
	asm volatile ("					\
	r2 = r1;					\
	r1 = %[cgroup_storage] ll;			\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 0);				\
	r0 = r1;					\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(cgroup_storage)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("valid per-cpu cgroup storage access")
__success __success_unpriv __retval(0)
__naked void per_cpu_cgroup_storage_access(void)
{
	asm volatile ("					\
	r2 = 0;						\
	r1 = %[percpu_cgroup_storage] ll;		\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 0);				\
	r0 = r1;					\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(percpu_cgroup_storage)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid per-cpu cgroup storage access 1")
__failure __msg("cannot pass map_type 1 into func bpf_get_local_storage")
__failure_unpriv
__naked void cpu_cgroup_storage_access_1(void)
{
	asm volatile ("					\
	r2 = 0;						\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 0);				\
	r0 = r1;					\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid per-cpu cgroup storage access 2")
__failure __msg("fd 1 is not pointing to valid bpf_map")
__failure_unpriv
__naked void cpu_cgroup_storage_access_2(void)
{
	asm volatile ("					\
	r2 = 0;						\
	.8byte %[ld_map_fd];				\
	.8byte 0;					\
	call %[bpf_get_local_storage];			\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_insn(ld_map_fd, BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, BPF_REG_1, BPF_PSEUDO_MAP_FD, 0, 1))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid per-cpu cgroup storage access 3")
__failure __msg("invalid access to map value, value_size=64 off=256 size=4")
__failure_unpriv
__naked void cpu_cgroup_storage_access_3(void)
{
	asm volatile ("					\
	r2 = 0;						\
	r1 = %[percpu_cgroup_storage] ll;		\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 256);				\
	r1 += 1;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(percpu_cgroup_storage)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid per-cpu cgroup storage access 4")
__failure __msg("invalid access to map value, value_size=64 off=-2 size=4")
__failure_unpriv
__flag(BPF_F_ANY_ALIGNMENT)
__naked void cpu_cgroup_storage_access_4(void)
{
	asm volatile ("					\
	r2 = 0;						\
	r1 = %[cgroup_storage] ll;			\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 - 2);				\
	r0 = r1;					\
	r1 += 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(cgroup_storage)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid per-cpu cgroup storage access 5")
__failure __msg("get_local_storage() doesn't support non-zero flags")
__failure_unpriv
__naked void cpu_cgroup_storage_access_5(void)
{
	asm volatile ("					\
	r2 = 7;						\
	r1 = %[percpu_cgroup_storage] ll;		\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 0);				\
	r0 = r1;					\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(percpu_cgroup_storage)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("invalid per-cpu cgroup storage access 6")
__failure __msg("get_local_storage() doesn't support non-zero flags")
__msg_unpriv("R2 leaks addr into helper function")
__naked void cpu_cgroup_storage_access_6(void)
{
	asm volatile ("					\
	r2 = r1;					\
	r1 = %[percpu_cgroup_storage] ll;		\
	call %[bpf_get_local_storage];			\
	r1 = *(u32*)(r0 + 0);				\
	r0 = r1;					\
	r0 &= 1;					\
	exit;						\
"	:
	: __imm(bpf_get_local_storage),
	  __imm_addr(percpu_cgroup_storage)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
