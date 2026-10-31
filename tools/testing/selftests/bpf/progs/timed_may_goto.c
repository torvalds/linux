// SPDX-License-Identifier: GPL-2.0
/*
 * There is one time slice of may_goto for an invocation of a prog: one call from the kernel into
 * the main prog, with the subprogs and sync callbacks that it reaches.
 *
 * Most tests call a function that spins until may_goto stops it, four times, each in a different
 * way. Only the first call can spin. With a time slice for each call all four do.
 */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

#if (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64)) && __clang_major__ >= 18

#define TRIES 4

int zero;

/* Return 1 if the loop went on until the time was up. */
static __always_inline int spin(void)
{
	__u64 t0 = bpf_ktime_get_ns();

	while (can_loop)
		;
	return bpf_ktime_get_ns() - t0 > 100 * 1000 * 1000;
}

static __noinline int spins(void)
{
	return spin();
}

static __noinline int counts_to_1000(void)
{
	int i, cnt = zero;

	for (i = zero; i < 1000 && can_loop; i++)
		cnt++;
	return cnt;
}

SEC("tc")
__description("timed may_goto: call")
__success __retval(1)
int call_subprog(void *ctx)
{
	int i, n = 0;

	for (i = 0; i < TRIES; i++)
		n += spins();
	return n;
}

SEC("tc")
__description("timed may_goto: a call after the time is up makes no iteration")
__success __retval(0)
int short_loop_after_timeout(void *ctx)
{
	spins();
	return counts_to_1000();
}

SEC("tc")
__description("timed may_goto: the loop of the main prog stops when the time is up in a subprog")
__success __retval(1)
int main_loop_after_timeout(void *ctx)
{
	int n = 0;

	while (can_loop && n < 2 * TRIES)
		n += spins();
	return n;
}

static __noinline int passes(int i)
{
	return spins() + i;
}

SEC("tc")
__description("timed may_goto: call through a function without a loop")
__success __retval(1)
int call_through(void *ctx)
{
	int i, n = 0;

	for (i = 0; i < TRIES; i++)
		n += passes(zero);
	return n;
}

__noinline int global_spins(void)
{
	return spin();
}

SEC("tc")
__description("timed may_goto: call of a global function")
__success __retval(1)
int call_global(void *ctx)
{
	int i, n = 0;

	for (i = 0; i < TRIES; i++)
		n += global_spins();
	return n;
}

SEC("tc")
__description("timed may_goto: callx")
__success __retval(1)
__naked void callx_subprog(void)
{
	asm volatile (
		"r6 = 0;"
		"r7 = 0;"
	"1:"
		"r3 = %[spins] ll;"
		"callx r3;"
		"r6 += r0;"
		"r7 += 1;"
		"if r7 < 4 goto 1b;"
		"r0 = r6;"
		"exit;"
		:
		: __imm_addr(spins)
		: __clobber_all);
}

/* With a time slice for each call it gives up, so that the test fails and doesn't lock up. */
static int loop_cb(__u64 i, int *spun)
{
	*spun += spin();
	return *spun > 2 * TRIES;
}

SEC("tc")
__description("timed may_goto: bpf_loop() inlined")
__success __retval(1)
int loop_inlined(void *ctx)
{
	int spun = 0;

	bpf_loop(1 << 20, loop_cb, &spun, 0);
	return spun;
}

SEC("tc")
__description("timed may_goto: bpf_loop() not inlined")
__success __retval(1)
int loop_not_inlined(void *ctx)
{
	int spun = 0;

	bpf_loop(1 << 20, loop_cb, &spun, zero);
	return spun;
}

static int count_cb(__u64 i, int *calls)
{
	(*calls)++;
	return 0;
}

/* The time is up: the helper calls the callback one more time. It returns 1 in its first insns. */
#define LOOP_ENDS(name, flags)							\
SEC("tc")									\
__description("timed may_goto: bpf_loop() " #name " ends when the time is up")	\
__success __retval(10)								\
int loop_ends_##name(void *ctx)							\
{										\
	int calls = 0;								\
										\
	spins();								\
	return bpf_loop(1000, count_cb, &calls, flags) * 10 + calls;		\
}

LOOP_ENDS(inlined, 0)
LOOP_ENDS(not_inlined, zero)

struct nested {
	__u64 t0;
	int spun;
};

/* Without an end of bpf_loop(): give up, so that the test fails and doesn't lock up. */
static int inner_cb(__u64 i, struct nested *n)
{
	if (!n->spun)
		n->spun = spin();
	return (i & 1023) == 0 && bpf_ktime_get_ns() - n->t0 > 1000 * 1000 * 1000;
}

static int outer_cb(__u64 i, struct nested *n)
{
	bpf_loop(1 << 23, inner_cb, n, 0);
	return bpf_ktime_get_ns() - n->t0 > 1000 * 1000 * 1000;
}

SEC("tc")
__description("timed may_goto: bpf_loop() in bpf_loop() ends when the time is up")
__success __retval(1)
int loop_in_loop(void *ctx)
{
	struct nested n = { .t0 = bpf_ktime_get_ns() };

	bpf_loop(1 << 23, outer_cb, &n, 0);
	return n.spun && bpf_ktime_get_ns() - n.t0 < 500 * 1000 * 1000;
}

static __noinline int called_cb(__u64 i, int *calls)
{
	(*calls)++;
	return 0;
}

/* BPF code calls the callback too, so its return value has to be its own. bpf_loop() goes on. */
SEC("tc")
__description("timed may_goto: bpf_loop() with a callback that is called as well")
__success __retval(45)
int loop_and_call(void *ctx)
{
	int calls = 0;

	spins();
	if (called_cb(zero, &calls))
		return 100;
	return bpf_loop(TRIES, called_cb, &calls, 0) * 10 + calls;
}

static int short_cb(__u64 i, __u64 *t0)
{
	int j;

	for (j = zero; j < 60000 && can_loop; j++)
		;
	return (i & 1023) == 0 && bpf_ktime_get_ns() - *t0 > 1000 * 1000 * 1000;
}

/*
 * The main prog has no may_goto. Each call of the callback ends before it reads the clock. The
 * iterations of all calls add up.
 */
SEC("tc")
__description("timed may_goto: bpf_loop() the callback makes 60000 iterations")
__success __retval(1)
int loop_short_cb(void *ctx)
{
	__u64 t0 = bpf_ktime_get_ns();

	bpf_loop(1 << 20, short_cb, &t0, 0);
	return bpf_ktime_get_ns() - t0 < 1000 * 1000 * 1000;
}

/* spins() uses up the time. The loop that calls it ends at its next may_goto. */
static __noinline int loops_around_spins(void)
{
	int n = zero;

	while (can_loop && n < 20000) {
		if (n < 2 * TRIES)
			spins();
		n++;
	}
	return n;
}

SEC("tc")
__description("timed may_goto: a loop that is on the stack ends when the time is up")
__success __retval(1)
int rest_of_batch(void *ctx)
{
	return loops_around_spins();
}

static __noinline int one_iteration(void)
{
	int i, n = zero;

	for (i = zero; i < 1 && can_loop; i++)
		n++;
	return n;
}

long calls_ok;

static int call_one(__u64 i, __u64 *t0)
{
	if (!one_iteration())
		return 1;
	calls_ok++;
	/* With a time slice for each call: give up. */
	return (i & 1023) == 0 && bpf_ktime_get_ns() - *t0 > 1000 * 1000 * 1000;
}

static int call_many(__u64 i, __u64 *t0)
{
	long before = calls_ok;

	bpf_loop(1 << 23, call_one, t0, 0);
	return calls_ok - before != 1 << 23;
}

/*
 * The iterations of all calls count: the prog reads the clock in each 65535th call. The call that
 * finds out that the time is up makes its iteration.
 */
SEC("tc")
__description("timed may_goto: 65535 calls that make one iteration read the clock")
__success __retval(1)
int clock_after_65535_calls(void *ctx)
{
	__u64 t0 = bpf_ktime_get_ns();

	calls_ok = 0;
	bpf_loop(1000, call_many, &t0, 0);
	return calls_ok > 0 && calls_ok % 65535 == 0;
}

#define MAP(name, map_type, flags)			\
struct {						\
	__uint(type, map_type);				\
	__uint(max_entries, TRIES);			\
	__uint(map_flags, flags);			\
	__type(key, int);				\
	__type(value, int);				\
} name SEC(".maps")

MAP(array, BPF_MAP_TYPE_ARRAY, 0);
MAP(hash, BPF_MAP_TYPE_HASH, 0);

/* The first call uses up the time. The second call ends the loop of the helper. */
static int elem_cb(void *map, int *key, int *val, int *spun)
{
	*spun += spin();
	return 0;
}

#define FOR_EACH(name)								\
SEC("tc")									\
__description("timed may_goto: bpf_for_each_map_elem() " #name)			\
__success __retval(21)								\
int for_each_##name(void *ctx)							\
{										\
	int i, key, spun = 0;							\
										\
	for (i = 0; i < TRIES; i++) {						\
		key = i;							\
		bpf_map_update_elem(&name, &key, &key, 0);			\
	}									\
	return bpf_for_each_map_elem(&name, elem_cb, &spun, 0) * 10 + spun;	\
}

FOR_EACH(array)
FOR_EACH(hash)

static long vma_cb(struct task_struct *task, struct vm_area_struct *vma, int *spun)
{
	*spun += spin();
	return 0;
}

SEC("raw_tp")
__description("timed may_goto: bpf_find_vma()")
__success __retval(1)
int find_vma(void *ctx)
{
	struct task_struct *task = bpf_get_current_task_btf();
	__u64 addr = task->mm->arg_start;
	int i, spun = 0;

	for (i = 0; i < TRIES; i++)
		if (bpf_find_vma(task, addr, vma_cb, &spun, 0))
			return 100;
	return spun;
}

struct tree_node {
	struct bpf_rb_node rb;
	int key;
};

#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))
private(A) struct bpf_spin_lock lock;
private(A) struct bpf_rb_root root __contains(tree_node, rb);

int less_spun, less_calls;

static bool less(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	long n = zero;

	/* no helpers under the lock */
	while (can_loop)
		n++;
	less_calls++;
	less_spun += n > 1000 * 1000;
	return container_of(a, struct tree_node, rb)->key <
	       container_of(b, struct tree_node, rb)->key;
}

SEC("tc")
__description("timed may_goto: bpf_rbtree_add()")
__success __retval(1)
int rbtree_add(void *ctx)
{
	struct bpf_rb_node *rb;
	struct tree_node *n;
	int i;

	less_spun = less_calls = 0;
	for (i = 0; i < TRIES; i++) {
		n = bpf_obj_new(typeof(*n));
		if (!n)
			return 100;
		n->key = TRIES - i;
		bpf_spin_lock(&lock);
		bpf_rbtree_add(&root, &n->rb, less);
		bpf_spin_unlock(&lock);
	}
	for (i = 0; i < TRIES; i++) {
		bpf_spin_lock(&lock);
		rb = bpf_rbtree_first(&root);
		if (rb)
			rb = bpf_rbtree_remove(&root, rb);
		bpf_spin_unlock(&lock);
		if (!rb)
			return 101;
		bpf_obj_drop(container_of(rb, struct tree_node, rb));
	}
	if (less_calls < TRIES - 1)
		return 102;
	return less_spun;
}

/*
 * All registers are live at the may_goto, so it saves one of them in the stack. They are the same
 * after 70000 iterations: the loop reads the clock.
 */
__naked __noinline __used
static long regs_across_may_goto(void)
{
	asm volatile (
		"r0 = 10;"
		"r6 = 6;"
		"r7 = 7;"
		"r8 = 8;"
		"r9 = 0;"
	"1:"
		"may_goto 2f;"
		"r9 += 1;"
		"if r9 < 70000 goto 1b;"
	"2:"
		"r0 += r1;"
		"r0 += r2;"
		"r0 += r3;"
		"r0 += r4;"
		"r0 += r5;"
		"r0 += r6;"
		"r0 += r7;"
		"r0 += r8;"
		"r0 += r9;"
		"exit;"
		::: __clobber_all);
}

SEC("tc")
__description("timed may_goto: registers across may_goto")
__success __retval(70046)
__naked void regs_across_may_goto_main(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call regs_across_may_goto;"
		"exit;"
		::: __clobber_all);
}

/*
 * may_goto uses a register that is dead. In live_r0_rN() R0 - RN are live at the may_goto and the
 * next one is not. The values come from memory, so the verifier doesn't know them. Return their sum
 * and the number of iterations.
 */
__u64 vals[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
__u64 zero64;

#define LD0(b)		"r0 = *(u64 *)(r" #b " + 0);"
#define LD1(b) LD0(b)	"r1 = *(u64 *)(r" #b " + 8);"
#define LD2(b) LD1(b)	"r2 = *(u64 *)(r" #b " + 16);"
#define LD3(b) LD2(b)	"r3 = *(u64 *)(r" #b " + 24);"
#define LD4(b) LD3(b)	"r4 = *(u64 *)(r" #b " + 32);"
#define LD5(b) LD4(b)	"r5 = *(u64 *)(r" #b " + 40);"
#define LD6(b) LD5(b)	"r6 = *(u64 *)(r" #b " + 48);"
#define LD7(b) LD6(b)	"r7 = *(u64 *)(r" #b " + 56);"
#define LD8(b) LD7(b)	"r8 = *(u64 *)(r" #b " + 64);"

#define SUM0
#define SUM1 SUM0	"r0 += r1;"
#define SUM2 SUM1	"r0 += r2;"
#define SUM3 SUM2	"r0 += r3;"
#define SUM4 SUM3	"r0 += r4;"
#define SUM5 SUM4	"r0 += r5;"
#define SUM6 SUM5	"r0 += r6;"
#define SUM7 SUM6	"r0 += r7;"
#define SUM8 SUM7	"r0 += r8;"

/* The count of iterations is at r10 - 8. It begins with a number from memory too. */
#define COUNT_INIT(t)						\
		"r" #t " = %[zero64] ll;"			\
		"r" #t " = *(u64 *)(r" #t " + 0);"		\
		"*(u64 *)(r10 - 8) = r" #t ";"
#define COUNT_AND_LOOP(t)					\
		"r" #t " = *(u64 *)(r10 - 8);"			\
		"r" #t " += 1;"					\
		"*(u64 *)(r10 - 8) = r" #t ";"			\
		"if r" #t " < 70000 goto 1b;"
#define ADD_COUNT_AND_EXIT					\
		"r1 = *(u64 *)(r10 - 8);"			\
		"r0 += r1;"					\
		"exit;"						\
		:						\
		: __imm_addr(vals), __imm_addr(zero64)		\
		: __clobber_all

#define LIVE_REGS(n, next)					\
__naked __noinline __used					\
static long live_r0_r##n(void)					\
{								\
	asm volatile (						\
		COUNT_INIT(next)				\
		"r" #n " = %[vals] ll;"				\
		LD##n(n)					\
	"1:"							\
		"may_goto 2f;"					\
		COUNT_AND_LOOP(next)				\
	"2:"							\
		SUM##n						\
		ADD_COUNT_AND_EXIT);				\
}

LIVE_REGS(0, 1)
LIVE_REGS(1, 2)
LIVE_REGS(2, 3)
LIVE_REGS(3, 4)
LIVE_REGS(4, 5)
LIVE_REGS(5, 6)
LIVE_REGS(6, 7)
LIVE_REGS(7, 8)
LIVE_REGS(8, 9)

/* R2 is read at the top of the loop only: it is live at the may_goto through the back edge. */
__naked __noinline __used
static long live_back_edge(void)
{
	asm volatile (
		COUNT_INIT(3)
		"r2 = %[vals] ll;"
		LD2(2)
	"1:"
		"*(u64 *)(r10 - 16) = r2;"
		"may_goto 2f;"
		COUNT_AND_LOOP(3)
	"2:"
		"r0 += r1;"
		"r2 = *(u64 *)(r10 - 16);"
		"r0 += r2;"
		ADD_COUNT_AND_EXIT);
}

/* R2 is read only where the may_goto jumps to. */
__naked __noinline __used
static long live_target(void)
{
	asm volatile (
		COUNT_INIT(3)
		"r2 = %[vals] ll;"
		LD2(2)
	"1:"
		"may_goto 2f;"
		COUNT_AND_LOOP(3)
		"goto 3f;"
	"2:"
		"r0 += r2;"
	"3:"
		"r0 += r1;"
		ADD_COUNT_AND_EXIT);
}

/* R2 is set in front of the may_goto in each iteration, and read only right behind it. */
__naked __noinline __used
static long live_fall_through(void)
{
	asm volatile (
		COUNT_INIT(3)
		"r1 = %[vals] ll;"
		LD1(1)
		"*(u64 *)(r10 - 16) = r0;"
	"1:"
		"r2 = %[vals] ll;"
		"r2 = *(u64 *)(r2 + 16);"
		"may_goto 2f;"
		"*(u64 *)(r10 - 16) = r2;"
		COUNT_AND_LOOP(3)
	"2:"
		"r0 += r1;"
		"r2 = *(u64 *)(r10 - 16);"
		"r0 += r2;"
		ADD_COUNT_AND_EXIT);
}

#define CALL_AND_ADD(f)	"call " #f ";" "r6 += r0;"
#define CALL_ALL_LIVE						\
		"r6 = 0;"					\
		CALL_AND_ADD(live_r0_r0)			\
		CALL_AND_ADD(live_r0_r1)			\
		CALL_AND_ADD(live_r0_r2)			\
		CALL_AND_ADD(live_r0_r3)			\
		CALL_AND_ADD(live_r0_r4)			\
		CALL_AND_ADD(live_r0_r5)			\
		CALL_AND_ADD(live_r0_r6)			\
		CALL_AND_ADD(live_r0_r7)			\
		CALL_AND_ADD(live_r0_r8)			\
		CALL_AND_ADD(live_back_edge)			\
		CALL_AND_ADD(live_target)			\
		CALL_AND_ADD(live_fall_through)			\
		"r0 = r6;"					\
		"exit;"

/*
 * The sums: 1 + 3 + 6 + 10 + 15 + 21 + 28 + 36 + 45 = 165. Then 6, 3 (the may_goto doesn't jump)
 * and 6. 12 * 70000 iterations: each function reads the clock.
 */
SEC("tc")
__description("timed may_goto: may_goto takes a register that is dead")
__success __retval(840180)
__naked void dead_reg(void)
{
	asm volatile (
		CALL_ALL_LIVE
		::: __clobber_all);
}

/* No iteration: 165, then 6, 6 (the may_goto jumps) and 4 (R2 isn't stored). */
SEC("tc")
__description("timed may_goto: may_goto takes a register that is dead, the time is up")
__success __retval(181)
__naked void dead_reg_time_is_up(void)
{
	asm volatile (
		"call spins_on_may_goto;"
		CALL_ALL_LIVE
		::: __clobber_all);
}

/*
 * The subprog uses up the time of the prog and returns. With a time slice for each call the caller
 * calls it 65535 times before it reads the clock.
 */
__naked __noinline __used
static unsigned long spins_on_may_goto(void)
{
	asm volatile (
	"1:"
		"may_goto 2f;"
		"goto 1b;"
	"2:"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/* The first may_goto of the function finds that the time is up */
SEC("tc")
__description("timed may_goto: registers across may_goto when the time is up")
__success __retval(46)
__naked void regs_when_time_is_up(void)
{
	asm volatile (
		"call spins_on_may_goto;"
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call regs_across_may_goto;"
		"exit;"
		::: __clobber_all);
}

/* The first insn of the main prog is the target of a jump. It must not set the count again. */
SEC("socket")
__description("timed may_goto: 9 insns")
__success __retval(1)
__naked void loop_calls_subprog_with_may_goto(void)
{
	asm volatile (
	"1:"
		"call spins_on_may_goto;"
		"may_goto 2f;"
		"goto 1b;"
	"2:"
		"r0 = 1;"
		"exit;"
		::: __clobber_all);
}

/*
 * These subprogs make 60000 iterations in each call. With a read of the clock after 65535
 * iterations of a call none of them reads it, and their caller does it after
 * 65535 * 60000 * 60000 iterations.
 */
__naked __noinline __used
static unsigned long loops_60000(void)
{
	asm volatile (
		"r1 = 60000;"
	"1:"
		"may_goto 2f;"
		"r1 += -1;"
		"if r1 != 0 goto 1b;"
	"2:"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

__naked __noinline __used
static unsigned long calls_60000_times(void)
{
	asm volatile (
		"r6 = 60000;"
	"1:"
		"call loops_60000;"
		"may_goto 2f;"
		"r6 += -1;"
		"if r6 != 0 goto 1b;"
	"2:"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__description("timed may_goto: three levels of loops that end before they read the clock")
__success __retval(1)
__naked void three_levels_of_short_loops(void)
{
	asm volatile (
	"1:"
		"call calls_60000_times;"
		"may_goto 2f;"
		"goto 1b;"
	"2:"
		"r0 = 1;"
		"exit;"
		::: __clobber_all);
}

/* The callee ends one iteration before it reads the clock. */
__naked __noinline __used
static unsigned long loops_65534(void)
{
	asm volatile (
		"r1 = 65534;"
	"1:"
		"may_goto 2f;"
		"r1 += -1;"
		"if r1 != 0 goto 1b;"
	"2:"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__description("timed may_goto: the subprog makes 65534 iterations")
__success __retval(1)
__naked void loop_calls_subprog_with_65534_iterations(void)
{
	asm volatile (
		"call %[bpf_ktime_get_ns];"
		"r6 = r0;"
	"1:"
		"call loops_65534;"
		"may_goto 2f;"
		"goto 1b;"
	"2:"
		"call %[bpf_ktime_get_ns];"
		"r0 -= r6;"
		"r1 = r0;"
		"r0 = 1;"
		"if r1 < 1000000000 goto +1;"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_ktime_get_ns)
		: __clobber_all);
}

/* The subprog returns 16 bytes in R0 and R2. may_goto must not change them. */
__naked __noinline __used
static unsigned __int128 returns_pair_after_may_goto(void)
{
	asm volatile (
		"r0 = 1;"
		"r2 = 2;"
	"1:"
		"may_goto 2f;"
		"goto 1b;"
	"2:"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__description("timed may_goto: the subprog returns 16 bytes")
__success __retval(3)
__naked void subprog_with_may_goto_returns_pair(void)
{
	asm volatile (
		"call returns_pair_after_may_goto;"
		"r0 += r2;"
		"exit;"
		::: __clobber_all);
}

/* Dead code doesn't count: a prog that has no may_goto left gets no insns. */
__naked __noinline __used
static unsigned long may_goto_is_dead_code(void)
{
	asm volatile (
		"r0 = 0;"
		"if r0 == 0 goto 2f;"
	"1:"
		"may_goto 2f;"
		"goto 1b;"
	"2:"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__description("timed may_goto: may_goto in dead code")
__success __retval(0)
__xlated("0: call pc+1")
__xlated("1: exit")
__xlated("2: r0 = 0")
__xlated("3: exit")
__naked void dead_may_goto(void)
{
	asm volatile (
		"call may_goto_is_dead_code;"
		"exit;"
		::: __clobber_all);
}

static __noinline int throws_after_spin(void)
{
	spin();
	bpf_throw(0);
	return 0;
}

__noinline int exception_cb_loops(u64 cookie)
{
	return counts_to_1000();
}

/* The exception callback belongs to the invocation that throws. */
SEC("tc")
__description("timed may_goto: bpf_throw() the exception callback has a loop")
__exception_cb(exception_cb_loops)
__success __retval(0)
int throw_after_timeout(void *ctx)
{
	return throws_after_spin();
}

#else

SEC("socket")
__description("timed may_goto: not supported dummy test")
__success
int dummy_test(void)
{
	return 0;
}

#endif
