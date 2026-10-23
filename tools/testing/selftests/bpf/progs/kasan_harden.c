// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

extern void bpf_kfunc_kasan_poison(void *mem, __u32 mem__sz) __ksym;
extern void bpf_kfunc_kasan_unpoison(void *mem, __u32 mem__sz) __ksym;

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
int st_blinded(struct __sk_buff *skb)
{
	struct kasan_test_val *val;
	__u32 key = 0;

	val = bpf_map_lookup_elem(&test_map, &key);
	if (!val)
		return 0;

	bpf_kfunc_kasan_poison(val, sizeof(struct kasan_test_val));
	val->data_1 = 0xAA;
	bpf_kfunc_kasan_unpoison(val, sizeof(struct kasan_test_val));

	return 0;
}
bool skip_st_tests SEC(".data") = 0;
#else
bool skip_st_tests SEC(".data") = 1;
#endif

char LICENSE[] SEC("license") = "GPL";
