#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

const char LICENSE[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} array SEC(".maps");

__noinline int sub1(int x)
{
	int key = 0;

	bpf_map_lookup_elem(&array, &key);
	return x + 1;
}

static __noinline int sub5(int v);

__noinline int sub2(int y)
{
	return sub5(y + 2);
}

static __noinline int sub3(int z)
{
	return z + 3 + sub1(4);
}

static __noinline int sub4(int w)
{
	int key = 0;

	bpf_map_lookup_elem(&array, &key);
	return w + sub3(5) + sub1(6);
}

/* sub5() is an identitify function, just to test weirder functions layout and
 * call patterns
 */
static __noinline int sub5(int v)
{
	return sub1(v) - 1; /* compensates sub1()'s + 1 */
}

/* unfortunately verifier rejects `struct task_struct *t` as an unkown pointer
 * type, so we need to accept pointer as integer and then cast it inside the
 * function
 */
__noinline int get_task_tgid(uintptr_t t)
{
	/* this ensures that CO-RE relocs work in multi-subprogs .text */
	return BPF_CORE_READ((struct task_struct *)(void *)t, tgid);
}

int res1 = 0;
int res2 = 0;
int res3 = 0;
int res4 = 0;

SEC("raw_tp/sys_enter")
int prog1(void *ctx)
{
	/* perform some CO-RE relocations to ensure they work with multi-prog
	 * sections correctly
	 */
	struct task_struct *t = (void *)bpf_get_current_task();

	if (!BPF_CORE_READ(t, pid) || !get_task_tgid((uintptr_t)t))
		return 1;

	res1 = sub1(1) + sub3(2); /* (1 + 1) + (2 + 3 + (4 + 1)) = 12 */
	return 0;
}

SEC("raw_tp/sys_exit")
int prog2(void *ctx)
{
	struct task_struct *t = (void *)bpf_get_current_task();

	if (!BPF_CORE_READ(t, pid) || !get_task_tgid((uintptr_t)t))
		return 1;

	res2 = sub2(3) + sub3(4); /* (3 + 2) + (4 + 3 + (4 + 1)) = 17 */
	return 0;
}

static int empty_callback(__u32 index, void *data)
{
	return 0;
}

/* prog3 has the same section name as prog1 */
SEC("raw_tp/sys_enter")
int prog3(void *ctx)
{
	struct task_struct *t = (void *)bpf_get_current_task();

	if (!BPF_CORE_READ(t, pid) || !get_task_tgid((uintptr_t)t))
		return 1;

	/* test that ld_imm64 with BPF_PSEUDO_FUNC doesn't get blinded */
	bpf_loop(1, empty_callback, NULL, 0);

	res3 = sub3(5) + 6; /* (5 + 3 + (4 + 1)) + 6 = 19 */
	return 0;
}

/* prog4 has the same section name as prog2 */
SEC("raw_tp/sys_exit")
int prog4(void *ctx)
{
	struct task_struct *t = (void *)bpf_get_current_task();

	if (!BPF_CORE_READ(t, pid) || !get_task_tgid((uintptr_t)t))
		return 1;

	res4 = sub4(7) + sub1(8); /* (7 + (5 + 3 + (4 + 1)) + (6 + 1)) + (8 + 1) = 36 */
	return 0;
}
