// SPDX-License-Identifier: GPL-2.0

#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_experimental.h"
#include <bpf_arena_common.h>

const volatile __u64 old_marker = 0x1111222233334444ULL;
const volatile __u64 new_marker = 0x5555666677778888ULL;

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1024);
#ifdef __TARGET_ARCH_arm64
	__ulong(map_extra, 0x1ull << 32);
#else
	__ulong(map_extra, 0x1ull << 44);
#endif
} arena SEC(".maps");

bool skip;
int page_idx;

void __arena *ptr;
void __arena *realloc_ptr;
bool realloc_after_free;
__u64 current_marker;
__u64 marker_seq;

int target_tid;
int pause_on_zap;
int zap_entered;
int release;
int timed_out;

__u64 trigger_pid_tgid;
long trigger_syscall;
int deferred_free;
int worker_armed;
int worker_exited;

static __always_inline void wait_for_release(void)
{
	while (!*(volatile int *)&release && can_loop)
		;
	if (!*(volatile int *)&release)
		timed_out = 1;
}

SEC("syscall")
int alloc_old(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) || defined(BPF_ARENA_FORCE_ASM)
	__u64 __arena *p;
	char __arena *base = arena_base(&arena);

	realloc_ptr = NULL;
	ptr = bpf_arena_alloc_pages(&arena, base + (page_idx + 1) * __PAGE_SIZE, 1,
				    NUMA_NO_NODE, 0);
	if (!ptr)
		return 1;
	p = (__u64 __arena *)ptr;
	*p = old_marker;
	current_marker = old_marker;
#else
	skip = true;
#endif
	return 0;
}

SEC("syscall")
int free_page(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) || defined(BPF_ARENA_FORCE_ASM)
	__u64 __arena *p;
	__u64 marker;

	if (!ptr)
		return 1;
	bpf_arena_free_pages(&arena, ptr, 1);
	if (!realloc_after_free)
		return 0;

	marker = new_marker + ++marker_seq;
	realloc_ptr = bpf_arena_alloc_pages(&arena, ptr, 1, NUMA_NO_NODE, 0);
	if (!realloc_ptr)
		return 0;
	ptr = realloc_ptr;
	p = (__u64 __arena *)ptr;
	*p = marker;
	current_marker = marker;
#endif
	return 0;
}

SEC("syscall")
int try_realloc(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) || defined(BPF_ARENA_FORCE_ASM)
	__u64 __arena *p;

	realloc_ptr = bpf_arena_alloc_pages(&arena, ptr, 1, NUMA_NO_NODE, 0);
	if (realloc_ptr) {
		ptr = realloc_ptr;
		p = (__u64 __arena *)realloc_ptr;
		*p = new_marker;
		current_marker = new_marker;
	}
#endif
	return 0;
}

SEC("tp_btf/sys_enter")
int BPF_PROG(deferred_free_prog, struct pt_regs *regs, long id)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) || defined(BPF_ARENA_FORCE_ASM)
	if (!deferred_free || bpf_get_current_pid_tgid() != trigger_pid_tgid ||
	    id != trigger_syscall)
		return 0;

	deferred_free = 0;
	/* The worker can run on another CPU before the kfunc returns. */
	worker_armed = 1;
	bpf_arena_free_pages(&arena, ptr, 1);
#endif
	return 0;
}

SEC("fentry/arena_free_worker")
int BPF_PROG(trace_free_worker, struct work_struct *work)
{
	if (worker_armed && !target_tid)
		target_tid = (__u32)bpf_get_current_pid_tgid();
	return 0;
}

SEC("fexit/arena_free_worker")
int BPF_PROG(trace_free_worker_ret, struct work_struct *work)
{
	if ((__u32)bpf_get_current_pid_tgid() == target_tid)
		worker_exited = 1;
	return 0;
}

/*
 * Pause a free while it zaps user mappings: after it cleared the kernel PTEs
 * and flushed the TLB, but before it releases the range. zap_vma_range() is
 * generic MM code, so it is an fentry target even where
 * flush_tlb_kernel_range() is inline (arm64).
 */
SEC("?fentry/zap_vma_range")
int BPF_PROG(trace_zap, struct vm_area_struct *vma, unsigned long address,
	     unsigned long size)
{
	if ((__u32)bpf_get_current_pid_tgid() != target_tid)
		return 0;
	zap_entered = 1;
	if (pause_on_zap)
		wait_for_release();
	return 0;
}

char _license[] SEC("license") = "GPL";
