// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

extern void bpf_kfunc_kasan_poison(void *mem, __u32 mem__sz) __ksym;
extern void bpf_kfunc_kasan_unpoison(void *mem, __u32 mem__sz) __ksym;

struct bpf_testmod_oob {
	__u8 data;
	union {
		__u8 redzone_1;
		__u16 redzone_2;
		__u32 redzone_4;
		__u64 redzone_8;
	};
};

extern struct bpf_testmod_oob *bpf_testmod_oob_alloc(void) __ksym;
extern void bpf_testmod_oob_free(struct bpf_testmod_oob *oob) __ksym;

int access_size;

struct kasan_test_val {
	__u8 data_1;
	__u16 data_2;
	__u32 data_4;
	__u64 data_8;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct kasan_test_val);
} test_map SEC(".maps");

/*
 * ST instructions are only emitted if the BPF cpu supports it (eg cpuv4),
 * they are otherwise turned into MOV + STX, so compile and exercise ST
 * only if supported.
 */
#ifdef __BPF_FEATURE_ST
SEC("tcx/ingress")
int st_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val val;

	bpf_kfunc_kasan_poison(&val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 1:
		val.data_1 = 0xAA;
		break;
	case 2:
		val.data_2 = 0xAA;
		break;
	case 4:
		val.data_4 = 0xAA;
		break;
	case 8:
		val.data_8 = 0xAA;
		break;
	}
	bpf_kfunc_kasan_unpoison(&val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int st_not_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val *val;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	bpf_kfunc_kasan_poison(val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 1:
		val->data_1 = 0xAA;
		break;
	case 2:
		val->data_2 = 0xAA;
		break;
	case 4:
		val->data_4 = 0xAA;
		break;
	case 8:
		val->data_8 = 0xAA;
		break;
	}
	bpf_kfunc_kasan_unpoison(val, sizeof(struct kasan_test_val));
	return 0;
}

bool skip_st_tests SEC(".data") = 0;
#else
bool skip_st_tests SEC(".data") = 1;
#endif

SEC("tcx/ingress")
int stx_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val val;

	bpf_kfunc_kasan_poison(&val, sizeof(struct kasan_test_val));
	/*
	 * Unlike the st() programs above, the stored value comes from a
	 * runtime source (skb->len), so it cannot be constant-folded and
	 * clang always emits a genuine BPF_STX (register store) regardless
	 * of the target cpu version.
	 */
	switch (access_size) {
	case 1:
		val.data_1 = (__u8)skb->len;
		break;
	case 2:
		val.data_2 = (__u16)skb->len;
		break;
	case 4:
		val.data_4 = (__u32)skb->len;
		break;
	case 8:
		val.data_8 = (__u64)skb->len;
		break;
	}
	bpf_kfunc_kasan_unpoison(&val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int stx_not_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val *val;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	bpf_kfunc_kasan_poison(val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 1:
		val->data_1 = (__u8)skb->len;
		break;
	case 2:
		val->data_2 = (__u16)skb->len;
		break;
	case 4:
		val->data_4 = (__u32)skb->len;
		break;
	case 8:
		val->data_8 = (__u64)skb->len;
		break;
	}
	bpf_kfunc_kasan_unpoison(val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int ldx_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val val;

	bpf_kfunc_kasan_poison(&val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 1:
		__sink(val.data_1);
		break;
	case 2:
		__sink(val.data_2);
		break;
	case 4:
		__sink(val.data_4);
		break;
	case 8:
		__sink(val.data_8);
		break;
	}
	bpf_kfunc_kasan_unpoison(&val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int ldx_not_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val *val;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	bpf_kfunc_kasan_poison(val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 1:
		__sink(val->data_1);
		break;
	case 2:
		__sink(val->data_2);
		break;
	case 4:
		__sink(val->data_4);
		break;
	case 8:
		__sink(val->data_8);
		break;
	}
	bpf_kfunc_kasan_unpoison(val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int ldx_patched_not_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val *val;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	bpf_kfunc_kasan_poison(val, sizeof(struct kasan_test_val));
	__sink(val->data_4);
	bpf_kfunc_kasan_unpoison(val, sizeof(struct kasan_test_val));

	return 0;
}

SEC("tcx/ingress")
int ldx_patched_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val val;

	bpf_kfunc_kasan_poison(&val, sizeof(struct kasan_test_val));
	__sink(val.data_4);
	bpf_kfunc_kasan_unpoison(&val, sizeof(struct kasan_test_val));

	return 0;
}

SEC("tcx/ingress")
int simple_atomic_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val val;

	bpf_kfunc_kasan_poison(&val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 4:
		__sync_fetch_and_add(&val.data_4, 4);
		break;
	case 8:
		__sync_fetch_and_add(&val.data_8, 8);
		break;
	}
	bpf_kfunc_kasan_unpoison(&val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int simple_atomic_not_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val *val;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	bpf_kfunc_kasan_poison(val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 4:
		__sync_fetch_and_add(&val->data_4, 4);
		break;
	case 8:
		__sync_fetch_and_add(&val->data_8, 8);
		break;
	}
	bpf_kfunc_kasan_unpoison(val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int simple_atomic_fetch_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val val;

	bpf_kfunc_kasan_poison(&val, sizeof(struct kasan_test_val));
	__sync_fetch_and_or(&val.data_8, 8);
	bpf_kfunc_kasan_unpoison(&val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int simple_atomic_fetch_not_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val *val;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	bpf_kfunc_kasan_poison(val, sizeof(struct kasan_test_val));
	__sync_fetch_and_or(&val->data_8, 8);
	bpf_kfunc_kasan_unpoison(val, sizeof(struct kasan_test_val));
	return 0;
}

#ifdef __BPF_FEATURE_LOAD_ACQ_STORE_REL
bool skip_load_acq_store_rel_tests SEC(".data") = 0;

SEC("tcx/ingress")
int load_acquire_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val val;

	bpf_kfunc_kasan_poison(&val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 1:
		__atomic_load_n(&val.data_1, __ATOMIC_ACQUIRE);
		break;
	case 2:
		__atomic_load_n(&val.data_2, __ATOMIC_ACQUIRE);
		break;
	case 4:
		__atomic_load_n(&val.data_4, __ATOMIC_ACQUIRE);
		break;
	case 8:
		__atomic_load_n(&val.data_8, __ATOMIC_ACQUIRE);
		break;
	}
	bpf_kfunc_kasan_unpoison(&val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int load_acquire_not_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val *val;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	bpf_kfunc_kasan_poison(val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 1:
		__atomic_load_n(&val->data_1, __ATOMIC_ACQUIRE);
		break;
	case 2:
		__atomic_load_n(&val->data_2, __ATOMIC_ACQUIRE);
		break;
	case 4:
		__atomic_load_n(&val->data_4, __ATOMIC_ACQUIRE);
		break;
	case 8:
		__atomic_load_n(&val->data_8, __ATOMIC_ACQUIRE);
		break;
	}
	bpf_kfunc_kasan_unpoison(val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int store_release_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val val;

	bpf_kfunc_kasan_poison(&val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 1:
		__atomic_store_n(&val.data_1, 0xAA, __ATOMIC_RELEASE);
		break;
	case 2:
		__atomic_store_n(&val.data_2, 0xBBBB, __ATOMIC_RELEASE);
		break;
	case 4:
		__atomic_store_n(&val.data_4, 0xCCCCCCCC, __ATOMIC_RELEASE);
		break;
	case 8:
		__atomic_store_n(&val.data_8, 0xDDDDDDDDDDDDDDDD,
				 __ATOMIC_RELEASE);
		break;
	}
	bpf_kfunc_kasan_unpoison(&val, sizeof(struct kasan_test_val));
	return 0;
}

SEC("tcx/ingress")
int store_release_not_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val *val;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	bpf_kfunc_kasan_poison(val, sizeof(struct kasan_test_val));
	switch (access_size) {
	case 1:
		__atomic_store_n(&val->data_1, 0xAA, __ATOMIC_RELEASE);
		break;
	case 2:
		__atomic_store_n(&val->data_2, 0xBBBB, __ATOMIC_RELEASE);
		break;
	case 4:
		__atomic_store_n(&val->data_4, 0xCCCCCCCC, __ATOMIC_RELEASE);
		break;
	case 8:
		__atomic_store_n(&val->data_8, 0xDDDDDDDDDDDDDDDD,
				 __ATOMIC_RELEASE);
		break;
	}
	bpf_kfunc_kasan_unpoison(val, sizeof(struct kasan_test_val));
	return 0;
}
#else
bool skip_load_acq_store_rel_tests SEC(".data") = 1;
#endif

SEC("tcx/ingress")
int verifier_paths_stack_and_non_stack(struct __sk_buff *skb)
{
	struct kasan_test_val stack_val = {};
	struct kasan_test_val *val;
	void *ptr;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	if (access_size)
		ptr = val;
	else
		ptr = &stack_val;

	bpf_kfunc_kasan_poison(val, sizeof(*val));
	*(__u8 *)ptr = 0xAA;
	bpf_kfunc_kasan_unpoison(val, sizeof(*val));
	return 0;
}

SEC("tcx/ingress")
int ldx_oob(struct __sk_buff *skb)
{
	struct bpf_testmod_oob *val;
	struct kasan_test_val volatile tmp;

	val = bpf_testmod_oob_alloc();
	if (!val)
		return 0;

	switch (access_size) {
	case 1:
		tmp.data_1 = (__u8)val->redzone_1;
		break;
	case 2:
		tmp.data_2 = (__u16)val->redzone_2;
		break;
	case 4:
		tmp.data_4 = (__u32)val->redzone_4;
		break;
	case 8:
		tmp.data_8 = (__u64)val->redzone_8;
		break;
	}
	bpf_testmod_oob_free(val);
	return tmp.data_1;
}

SEC("tcx/ingress")
int ldx_self_alias_on_stack(struct __sk_buff *skb)
{
	struct kasan_test_val val;
	__u64 addr;

	bpf_kfunc_kasan_poison(&val, sizeof(val));
	/*
	 * Check that a stack access with dst_reg == src_reg is correctly
	 * flagged as stack-only access
	 */
	addr = (__u64)&val;
	asm volatile(
		"r1 = %0\n"
		"r1 = *(u64 *)(r1 + 0)\n"
		:
		: "r"(addr)
		: "r1", "memory");
	bpf_kfunc_kasan_unpoison(&val, sizeof(val));

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
