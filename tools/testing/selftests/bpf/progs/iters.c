// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_compiler.h"

#define unlikely(x)	__builtin_expect(!!(x), 0)

static volatile int zero = 0;

int my_pid;
int arr[256];
int small_arr[16] SEC(".data.small_arr");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10);
	__type(key, int);
	__type(value, int);
} amap SEC(".maps");

#ifdef REAL_TEST
#define MY_PID_GUARD() if (my_pid != (bpf_get_current_pid_tgid() >> 32)) return 0
#else
#define MY_PID_GUARD() ({ })
#endif

SEC("?raw_tp")
__failure __msg("math between map_value pointer and register with unbounded min value is not allowed")
int iter_err_unsafe_c_loop(const void *ctx)
{
	struct bpf_iter_num it;
	int *v, i = zero; /* obscure initial value of i */

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 0, 1000);
	while ((v = bpf_iter_num_next(&it))) {
		i++;
	}
	bpf_iter_num_destroy(&it);

	small_arr[i] = 123; /* invalid */

	return 0;
}

SEC("?raw_tp")
__failure __msg("unbounded memory access")
int iter_err_unsafe_asm_loop(const void *ctx)
{
	struct bpf_iter_num it;

	MY_PID_GUARD();

	asm volatile (
		"r6 = %[zero];" /* iteration counter */
		"r1 = %[it];" /* iterator state */
		"r2 = 0;"
		"r3 = 1000;"
		"r4 = 1;"
		"call %[bpf_iter_num_new];"
	"loop:"
		"r1 = %[it];"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto out;"
		"r6 += 1;"
		"goto loop;"
	"out:"
		"r1 = %[it];"
		"call %[bpf_iter_num_destroy];"
		"r1 = %[small_arr];"
		"r2 = r6;"
		"r2 <<= 2;"
		"r1 += r2;"
		"*(u32 *)(r1 + 0) = r6;" /* invalid */
		:
		: [it]"r"(&it),
		  [small_arr]"r"(small_arr),
		  [zero]"r"(zero),
		  __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy)
		: __clobber_common, "r6"
	);

	return 0;
}

SEC("raw_tp")
__success
int iter_while_loop(const void *ctx)
{
	struct bpf_iter_num it;
	int *v;

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 0, 3);
	while ((v = bpf_iter_num_next(&it))) {
		bpf_printk("ITER_BASIC: E1 VAL: v=%d", *v);
	}
	bpf_iter_num_destroy(&it);

	return 0;
}

SEC("raw_tp")
__success
int iter_while_loop_auto_cleanup(const void *ctx)
{
	__attribute__((cleanup(bpf_iter_num_destroy))) struct bpf_iter_num it;
	int *v;

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 0, 3);
	while ((v = bpf_iter_num_next(&it))) {
		bpf_printk("ITER_BASIC: E1 VAL: v=%d", *v);
	}
	/* (!) no explicit bpf_iter_num_destroy() */

	return 0;
}

SEC("raw_tp")
__success
int iter_for_loop(const void *ctx)
{
	struct bpf_iter_num it;
	int *v;

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 5, 10);
	for (v = bpf_iter_num_next(&it); v; v = bpf_iter_num_next(&it)) {
		bpf_printk("ITER_BASIC: E2 VAL: v=%d", *v);
	}
	bpf_iter_num_destroy(&it);

	return 0;
}

SEC("raw_tp")
__success
int iter_bpf_for_each_macro(const void *ctx)
{
	int *v;

	MY_PID_GUARD();

	bpf_for_each(num, v, 5, 10) {
		bpf_printk("ITER_BASIC: E2 VAL: v=%d", *v);
	}

	return 0;
}

SEC("raw_tp")
__success
int iter_bpf_for_macro(const void *ctx)
{
	int i;

	MY_PID_GUARD();

	bpf_for(i, 5, 10) {
		bpf_printk("ITER_BASIC: E2 VAL: v=%d", i);
	}

	return 0;
}

SEC("raw_tp")
__success
int iter_pragma_unroll_loop(const void *ctx)
{
	struct bpf_iter_num it;
	int *v, i;

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 0, 2);
	__pragma_loop_no_unroll
	for (i = 0; i < 3; i++) {
		v = bpf_iter_num_next(&it);
		bpf_printk("ITER_BASIC: E3 VAL: i=%d v=%d", i, v ? *v : -1);
	}
	bpf_iter_num_destroy(&it);

	return 0;
}

SEC("raw_tp")
__success
int iter_manual_unroll_loop(const void *ctx)
{
	struct bpf_iter_num it;
	int *v;

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 100, 200);
	v = bpf_iter_num_next(&it);
	bpf_printk("ITER_BASIC: E4 VAL: v=%d", v ? *v : -1);
	v = bpf_iter_num_next(&it);
	bpf_printk("ITER_BASIC: E4 VAL: v=%d", v ? *v : -1);
	v = bpf_iter_num_next(&it);
	bpf_printk("ITER_BASIC: E4 VAL: v=%d", v ? *v : -1);
	v = bpf_iter_num_next(&it);
	bpf_printk("ITER_BASIC: E4 VAL: v=%d\n", v ? *v : -1);
	bpf_iter_num_destroy(&it);

	return 0;
}

SEC("raw_tp")
__success
int iter_multiple_sequential_loops(const void *ctx)
{
	struct bpf_iter_num it;
	int *v, i;

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 0, 3);
	while ((v = bpf_iter_num_next(&it))) {
		bpf_printk("ITER_BASIC: E1 VAL: v=%d", *v);
	}
	bpf_iter_num_destroy(&it);

	bpf_iter_num_new(&it, 5, 10);
	for (v = bpf_iter_num_next(&it); v; v = bpf_iter_num_next(&it)) {
		bpf_printk("ITER_BASIC: E2 VAL: v=%d", *v);
	}
	bpf_iter_num_destroy(&it);

	bpf_iter_num_new(&it, 0, 2);
	__pragma_loop_no_unroll
	for (i = 0; i < 3; i++) {
		v = bpf_iter_num_next(&it);
		bpf_printk("ITER_BASIC: E3 VAL: i=%d v=%d", i, v ? *v : -1);
	}
	bpf_iter_num_destroy(&it);

	bpf_iter_num_new(&it, 100, 200);
	v = bpf_iter_num_next(&it);
	bpf_printk("ITER_BASIC: E4 VAL: v=%d", v ? *v : -1);
	v = bpf_iter_num_next(&it);
	bpf_printk("ITER_BASIC: E4 VAL: v=%d", v ? *v : -1);
	v = bpf_iter_num_next(&it);
	bpf_printk("ITER_BASIC: E4 VAL: v=%d", v ? *v : -1);
	v = bpf_iter_num_next(&it);
	bpf_printk("ITER_BASIC: E4 VAL: v=%d\n", v ? *v : -1);
	bpf_iter_num_destroy(&it);

	return 0;
}

SEC("raw_tp")
__success
int iter_limit_cond_break_loop(const void *ctx)
{
	struct bpf_iter_num it;
	int *v, i = 0, sum = 0;

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 0, 10);
	while ((v = bpf_iter_num_next(&it))) {
		bpf_printk("ITER_SIMPLE: i=%d v=%d", i, *v);
		sum += *v;

		i++;
		if (i > 3)
			break;
	}
	bpf_iter_num_destroy(&it);

	bpf_printk("ITER_SIMPLE: sum=%d\n", sum);

	return 0;
}

SEC("raw_tp")
__success
int iter_obfuscate_counter(const void *ctx)
{
	struct bpf_iter_num it;
	int *v, sum = 0;
	/* Make i's initial value unknowable for verifier to prevent it from
	 * pruning if/else branch inside the loop body and marking i as precise.
	 */
	int i = zero;

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 0, 10);
	while ((v = bpf_iter_num_next(&it))) {
		int x;

		i += 1;

		/* If we initialized i as `int i = 0;` above, verifier would
		 * track that i becomes 1 on first iteration after increment
		 * above, and here verifier would eagerly prune else branch
		 * and mark i as precise, ruining open-coded iterator logic
		 * completely, as each next iteration would have a different
		 * *precise* value of i, and thus there would be no
		 * convergence of state. This would result in reaching maximum
		 * instruction limit, no matter what the limit is.
		 */
		if (i == 1)
			x = 123;
		else
			x = i * 3 + 1;

		bpf_printk("ITER_OBFUSCATE_COUNTER: i=%d v=%d x=%d", i, *v, x);

		sum += x;
	}
	bpf_iter_num_destroy(&it);

	bpf_printk("ITER_OBFUSCATE_COUNTER: sum=%d\n", sum);

	return 0;
}

SEC("raw_tp")
__success
int iter_search_loop(const void *ctx)
{
	struct bpf_iter_num it;
	int *v, *elem = NULL;
	bool found = false;

	MY_PID_GUARD();

	bpf_iter_num_new(&it, 0, 10);

	while ((v = bpf_iter_num_next(&it))) {
		bpf_printk("ITER_SEARCH_LOOP: v=%d", *v);

		if (*v == 2) {
			found = true;
			elem = v;
			barrier_var(elem);
		}
	}

	/* should fail to verify if bpf_iter_num_destroy() is here */

	if (found)
		/* here found element will be wrong, we should have copied
		 * value to a variable, but here we want to make sure we can
		 * access memory after the loop anyways
		 */
		bpf_printk("ITER_SEARCH_LOOP: FOUND IT = %d!\n", *elem);
	else
		bpf_printk("ITER_SEARCH_LOOP: NOT FOUND IT!\n");

	bpf_iter_num_destroy(&it);

	return 0;
}

SEC("raw_tp")
__success
int iter_array_fill(const void *ctx)
{
	int sum, i;

	MY_PID_GUARD();

	bpf_for(i, 0, ARRAY_SIZE(arr)) {
		arr[i] = i * 2;
	}

	sum = 0;
	bpf_for(i, 0, ARRAY_SIZE(arr)) {
		sum += arr[i];
	}

	bpf_printk("ITER_ARRAY_FILL: sum=%d (should be %d)\n", sum, 255 * 256);

	return 0;
}

static int arr2d[4][5];
static int arr2d_row_sums[4];
static int arr2d_col_sums[5];

SEC("raw_tp")
__success
int iter_nested_iters(const void *ctx)
{
	int sum, row, col;

	MY_PID_GUARD();

	bpf_for(row, 0, ARRAY_SIZE(arr2d)) {
		bpf_for( col, 0, ARRAY_SIZE(arr2d[0])) {
			arr2d[row][col] = row * col;
		}
	}

	/* zero-initialize sums */
	sum = 0;
	bpf_for(row, 0, ARRAY_SIZE(arr2d)) {
		arr2d_row_sums[row] = 0;
	}
	bpf_for(col, 0, ARRAY_SIZE(arr2d[0])) {
		arr2d_col_sums[col] = 0;
	}

	/* calculate sums */
	bpf_for(row, 0, ARRAY_SIZE(arr2d)) {
		bpf_for(col, 0, ARRAY_SIZE(arr2d[0])) {
			sum += arr2d[row][col];
			arr2d_row_sums[row] += arr2d[row][col];
			arr2d_col_sums[col] += arr2d[row][col];
		}
	}

	bpf_printk("ITER_NESTED_ITERS: total sum=%d", sum);
	bpf_for(row, 0, ARRAY_SIZE(arr2d)) {
		bpf_printk("ITER_NESTED_ITERS: row #%d sum=%d", row, arr2d_row_sums[row]);
	}
	bpf_for(col, 0, ARRAY_SIZE(arr2d[0])) {
		bpf_printk("ITER_NESTED_ITERS: col #%d sum=%d%s",
			   col, arr2d_col_sums[col],
			   col == ARRAY_SIZE(arr2d[0]) - 1 ? "\n" : "");
	}

	return 0;
}

SEC("raw_tp")
__success
int iter_nested_deeply_iters(const void *ctx)
{
	int sum = 0;

	MY_PID_GUARD();

	bpf_repeat(10) {
		bpf_repeat(10) {
			bpf_repeat(10) {
				bpf_repeat(10) {
					bpf_repeat(10) {
						sum += 1;
					}
				}
			}
		}
		/* validate that we can break from inside bpf_repeat() */
		break;
	}

	return sum;
}

static __noinline void fill_inner_dimension(int row)
{
	int col;

	bpf_for(col, 0, ARRAY_SIZE(arr2d[0])) {
		arr2d[row][col] = row * col;
	}
}

static __noinline int sum_inner_dimension(int row)
{
	int sum = 0, col;

	bpf_for(col, 0, ARRAY_SIZE(arr2d[0])) {
		sum += arr2d[row][col];
		arr2d_row_sums[row] += arr2d[row][col];
		arr2d_col_sums[col] += arr2d[row][col];
	}

	return sum;
}

SEC("raw_tp")
__success
int iter_subprog_iters(const void *ctx)
{
	int sum, row, col;

	MY_PID_GUARD();

	bpf_for(row, 0, ARRAY_SIZE(arr2d)) {
		fill_inner_dimension(row);
	}

	/* zero-initialize sums */
	sum = 0;
	bpf_for(row, 0, ARRAY_SIZE(arr2d)) {
		arr2d_row_sums[row] = 0;
	}
	bpf_for(col, 0, ARRAY_SIZE(arr2d[0])) {
		arr2d_col_sums[col] = 0;
	}

	/* calculate sums */
	bpf_for(row, 0, ARRAY_SIZE(arr2d)) {
		sum += sum_inner_dimension(row);
	}

	bpf_printk("ITER_SUBPROG_ITERS: total sum=%d", sum);
	bpf_for(row, 0, ARRAY_SIZE(arr2d)) {
		bpf_printk("ITER_SUBPROG_ITERS: row #%d sum=%d",
			   row, arr2d_row_sums[row]);
	}
	bpf_for(col, 0, ARRAY_SIZE(arr2d[0])) {
		bpf_printk("ITER_SUBPROG_ITERS: col #%d sum=%d%s",
			   col, arr2d_col_sums[col],
			   col == ARRAY_SIZE(arr2d[0]) - 1 ? "\n" : "");
	}

	return 0;
}

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 1000);
} hash_map SEC(".maps");

SEC("?raw_tp")
__failure __msg("invalid mem access 'scalar'")
int iter_err_too_permissive1(const void *ctx)
{
	int *map_val = NULL;
	int key = 0;

	MY_PID_GUARD();

	map_val = bpf_map_lookup_elem(&hash_map, &key);
	if (!map_val)
		return 0;

	bpf_repeat(1000000) {
		map_val = NULL;
	}

	*map_val = 123;

	return 0;
}

SEC("?raw_tp")
__failure __msg("invalid mem access 'map_value_or_null'")
int iter_err_too_permissive2(const void *ctx)
{
	int *map_val = NULL;
	int key = 0;

	MY_PID_GUARD();

	map_val = bpf_map_lookup_elem(&hash_map, &key);
	if (!map_val)
		return 0;

	bpf_repeat(1000000) {
		map_val = bpf_map_lookup_elem(&hash_map, &key);
	}

	*map_val = 123;

	return 0;
}

SEC("?raw_tp")
__failure __msg("invalid mem access 'map_value_or_null'")
int iter_err_too_permissive3(const void *ctx)
{
	int *map_val = NULL;
	int key = 0;
	bool found = false;

	MY_PID_GUARD();

	bpf_repeat(1000000) {
		map_val = bpf_map_lookup_elem(&hash_map, &key);
		found = true;
	}

	if (found)
		*map_val = 123;

	return 0;
}

SEC("raw_tp")
__success
int iter_tricky_but_fine(const void *ctx)
{
	int *map_val = NULL;
	int key = 0;
	bool found = false;

	MY_PID_GUARD();

	bpf_repeat(1000000) {
		map_val = bpf_map_lookup_elem(&hash_map, &key);
		if (map_val) {
			found = true;
			break;
		}
	}

	if (found)
		*map_val = 123;

	return 0;
}

#define __bpf_memzero(p, sz) bpf_probe_read_kernel((p), (sz), 0)

SEC("raw_tp")
__success
int iter_stack_array_loop(const void *ctx)
{
	long arr1[16], arr2[16], sum = 0;
	int i;

	MY_PID_GUARD();

	/* zero-init arr1 and arr2 in such a way that verifier doesn't know
	 * it's all zeros; if we don't do that, we'll make BPF verifier track
	 * all combination of zero/non-zero stack slots for arr1/arr2, which
	 * will lead to O(2^(ARRAY_SIZE(arr1)+ARRAY_SIZE(arr2))) different
	 * states
	 */
	__bpf_memzero(arr1, sizeof(arr1));
	__bpf_memzero(arr2, sizeof(arr1));

	/* validate that we can break and continue when using bpf_for() */
	bpf_for(i, 0, ARRAY_SIZE(arr1)) {
		if (i & 1) {
			arr1[i] = i;
			continue;
		} else {
			arr2[i] = i;
			break;
		}
	}

	bpf_for(i, 0, ARRAY_SIZE(arr1)) {
		sum += arr1[i] + arr2[i];
	}

	return sum;
}

static __noinline void fill(struct bpf_iter_num *it, int *arr, __u32 n, int mul)
{
	int *t, i;

	while ((t = bpf_iter_num_next(it))) {
		i = *t;
		if (i >= n)
			break;
		arr[i] =  i * mul;
	}
}

static __noinline int sum(struct bpf_iter_num *it, int *arr, __u32 n)
{
	int *t, i, sum = 0;

	while ((t = bpf_iter_num_next(it))) {
		i = *t;
		if ((__u32)i >= n)
			break;
		sum += arr[i];
	}

	return sum;
}

SEC("raw_tp")
__success
int iter_pass_iter_ptr_to_subprog(const void *ctx)
{
	int arr1[16], arr2[32];
	struct bpf_iter_num it;
	int n, sum1, sum2;

	MY_PID_GUARD();

	/* fill arr1 */
	n = ARRAY_SIZE(arr1);
	bpf_iter_num_new(&it, 0, n);
	fill(&it, arr1, n, 2);
	bpf_iter_num_destroy(&it);

	/* fill arr2 */
	n = ARRAY_SIZE(arr2);
	bpf_iter_num_new(&it, 0, n);
	fill(&it, arr2, n, 10);
	bpf_iter_num_destroy(&it);

	/* sum arr1 */
	n = ARRAY_SIZE(arr1);
	bpf_iter_num_new(&it, 0, n);
	sum1 = sum(&it, arr1, n);
	bpf_iter_num_destroy(&it);

	/* sum arr2 */
	n = ARRAY_SIZE(arr2);
	bpf_iter_num_new(&it, 0, n);
	sum2 = sum(&it, arr2, n);
	bpf_iter_num_destroy(&it);

	bpf_printk("sum1=%d, sum2=%d", sum1, sum2);

	return 0;
}

SEC("?raw_tp")
__failure
__msg("R1 type=scalar expected=fp")
__naked int delayed_read_mark(void)
{
	/* This is equivalent to C program below.
	 * The call to bpf_iter_num_next() is reachable with r7 values &fp[-16] and 0xdead.
	 * State with r7=&fp[-16] is visited first and follows r6 != 42 ... continue branch.
	 * At this point iterator next() call is reached with r7 that has no read mark.
	 * Loop body with r7=0xdead would only be visited if verifier would decide to continue
	 * with second loop iteration. Absence of read mark on r7 might affect state
	 * equivalent logic used for iterator convergence tracking.
	 *
	 * r7 = &fp[-16]
	 * fp[-16] = 0
	 * r6 = bpf_get_prandom_u32()
	 * bpf_iter_num_new(&fp[-8], 0, 10)
	 * while (bpf_iter_num_next(&fp[-8])) {
	 *   r6++
	 *   if (r6 != 42) {
	 *     r7 = 0xdead
	 *     continue;
	 *   }
	 *   bpf_probe_read_user(r7, 8, 0xdeadbeef); // this is not safe
	 * }
	 * bpf_iter_num_destroy(&fp[-8])
	 * return 0
	 */
	asm volatile (
		"r7 = r10;"
		"r7 += -16;"
		"r0 = 0;"
		"*(u64 *)(r7 + 0) = r0;"
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
	"1:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto 2f;"
		"r6 += 1;"
		"if r6 != 42 goto 3f;"
		"r7 = 0xdead;"
		"goto 1b;"
	"3:"
		"r1 = r7;"
		"r2 = 8;"
		"r3 = 0xdeadbeef;"
		"call %[bpf_probe_read_user];"
		"goto 1b;"
	"2:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy),
		  __imm(bpf_probe_read_user)
		: __clobber_all
	);
}

SEC("?raw_tp")
__failure
__msg("math between fp pointer and register with unbounded")
__naked int delayed_precision_mark(void)
{
	/* This is equivalent to C program below.
	 * The test is similar to delayed_iter_mark but verifies that incomplete
	 * precision don't fool verifier.
	 * The call to bpf_iter_num_next() is reachable with r7 values -16 and -32.
	 * State with r7=-16 is visited first and follows r6 != 42 ... continue branch.
	 * At this point iterator next() call is reached with r7 that has no read
	 * and precision marks.
	 * Loop body with r7=-32 would only be visited if verifier would decide to continue
	 * with second loop iteration. Absence of precision mark on r7 might affect state
	 * equivalent logic used for iterator convergence tracking.
	 *
	 * r8 = 0
	 * fp[-16] = 0
	 * r7 = -16
	 * r6 = bpf_get_prandom_u32()
	 * bpf_iter_num_new(&fp[-8], 0, 10)
	 * while (bpf_iter_num_next(&fp[-8])) {
	 *   if (r6 != 42) {
	 *     r7 = -32
	 *     r6 = bpf_get_prandom_u32()
	 *     continue;
	 *   }
	 *   r0 = r10
	 *   r0 += r7
	 *   r8 = *(u64 *)(r0 + 0)           // this is not safe
	 *   r6 = bpf_get_prandom_u32()
	 * }
	 * bpf_iter_num_destroy(&fp[-8])
	 * return r8
	 */
	asm volatile (
		"r8 = 0;"
		"*(u64 *)(r10 - 16) = r8;"
		"r7 = -16;"
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
	"1:"
		"r1 = r10;"
		"r1 += -8;\n"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto 2f;"
		"if r6 != 42 goto 3f;"
		"r7 = -33;"
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"goto 1b;\n"
	"3:"
		"r0 = r10;"
		"r0 += r7;"
		"r8 = *(u64 *)(r0 + 0);"
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"goto 1b;\n"
	"2:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r0 = r8;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy),
		  __imm(bpf_probe_read_user)
		: __clobber_all
	);
}

SEC("?raw_tp")
__failure
__msg("math between fp pointer and register with unbounded")
__flag(BPF_F_TEST_STATE_FREQ)
__naked int loop_state_deps1(void)
{
	/* This is equivalent to C program below.
	 *
	 * The case turns out to be tricky in a sense that:
	 * - states with c=-25 are explored only on a second iteration
	 *   of the outer loop;
	 * - states with read+precise mark on c are explored only on
	 *   second iteration of the inner loop and in a state which
	 *   is pushed to states stack first.
	 *
	 * Depending on the details of iterator convergence logic
	 * verifier might stop states traversal too early and miss
	 * unsafe c=-25 memory access.
	 *
	 *   j = iter_new();		 // fp[-16]
	 *   a = 0;			 // r6
	 *   b = 0;			 // r7
	 *   c = -24;			 // r8
	 *   while (iter_next(j)) {
	 *     i = iter_new();		 // fp[-8]
	 *     a = 0;			 // r6
	 *     b = 0;			 // r7
	 *     while (iter_next(i)) {
	 *	 if (a == 1) {
	 *	   a = 0;
	 *	   b = 1;
	 *	 } else if (a == 0) {
	 *	   a = 1;
	 *	   if (random() == 42)
	 *	     continue;
	 *	   if (b == 1) {
	 *	     *(r10 + c) = 7;  // this is not safe
	 *	     iter_destroy(i);
	 *	     iter_destroy(j);
	 *	     return;
	 *	   }
	 *	 }
	 *     }
	 *     iter_destroy(i);
	 *     a = 0;
	 *     b = 0;
	 *     c = -25;
	 *   }
	 *   iter_destroy(j);
	 *   return;
	 */
	asm volatile (
		"r1 = r10;"
		"r1 += -16;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
		"r6 = 0;"
		"r7 = 0;"
		"r8 = -24;"
	"j_loop_%=:"
		"r1 = r10;"
		"r1 += -16;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto j_loop_end_%=;"
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
		"r6 = 0;"
		"r7 = 0;"
	"i_loop_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto i_loop_end_%=;"
	"check_one_r6_%=:"
		"if r6 != 1 goto check_zero_r6_%=;"
		"r6 = 0;"
		"r7 = 1;"
		"goto i_loop_%=;"
	"check_zero_r6_%=:"
		"if r6 != 0 goto i_loop_%=;"
		"r6 = 1;"
		"call %[bpf_get_prandom_u32];"
		"if r0 != 42 goto check_one_r7_%=;"
		"goto i_loop_%=;"
	"check_one_r7_%=:"
		"if r7 != 1 goto i_loop_%=;"
		"r0 = r10;"
		"r0 += r8;"
		"r1 = 7;"
		"*(u64 *)(r0 + 0) = r1;"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r1 = r10;"
		"r1 += -16;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"
	"i_loop_end_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r6 = 0;"
		"r7 = 0;"
		"r8 = -25;"
		"goto j_loop_%=;"
	"j_loop_end_%=:"
		"r1 = r10;"
		"r1 += -16;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy)
		: __clobber_all
	);
}

SEC("?raw_tp")
__failure
__msg("math between fp pointer and register with unbounded")
__flag(BPF_F_TEST_STATE_FREQ)
__naked int loop_state_deps2(void)
{
	/* This is equivalent to C program below.
	 *
	 * The case turns out to be tricky in a sense that:
	 * - states with read+precise mark on c are explored only on a second
	 *   iteration of the first inner loop and in a state which is pushed to
	 *   states stack first.
	 * - states with c=-25 are explored only on a second iteration of the
	 *   second inner loop and in a state which is pushed to states stack
	 *   first.
	 *
	 * Depending on the details of iterator convergence logic
	 * verifier might stop states traversal too early and miss
	 * unsafe c=-25 memory access.
	 *
	 *   j = iter_new();             // fp[-16]
	 *   a = 0;                      // r6
	 *   b = 0;                      // r7
	 *   c = -24;                    // r8
	 *   while (iter_next(j)) {
	 *     i = iter_new();           // fp[-8]
	 *     a = 0;                    // r6
	 *     b = 0;                    // r7
	 *     while (iter_next(i)) {
	 *       if (a == 1) {
	 *         a = 0;
	 *         b = 1;
	 *       } else if (a == 0) {
	 *         a = 1;
	 *         if (random() == 42)
	 *           continue;
	 *         if (b == 1) {
	 *           *(r10 + c) = 7;     // this is not safe
	 *           iter_destroy(i);
	 *           iter_destroy(j);
	 *           return;
	 *         }
	 *       }
	 *     }
	 *     iter_destroy(i);
	 *     i = iter_new();           // fp[-8]
	 *     a = 0;                    // r6
	 *     b = 0;                    // r7
	 *     while (iter_next(i)) {
	 *       if (a == 1) {
	 *         a = 0;
	 *         b = 1;
	 *       } else if (a == 0) {
	 *         a = 1;
	 *         if (random() == 42)
	 *           continue;
	 *         if (b == 1) {
	 *           a = 0;
	 *           c = -25;
	 *         }
	 *       }
	 *     }
	 *     iter_destroy(i);
	 *   }
	 *   iter_destroy(j);
	 *   return;
	 */
	asm volatile (
		"r1 = r10;"
		"r1 += -16;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
		"r6 = 0;"
		"r7 = 0;"
		"r8 = -24;"
	"j_loop_%=:"
		"r1 = r10;"
		"r1 += -16;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto j_loop_end_%=;"

		/* first inner loop */
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
		"r6 = 0;"
		"r7 = 0;"
	"i_loop_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto i_loop_end_%=;"
	"check_one_r6_%=:"
		"if r6 != 1 goto check_zero_r6_%=;"
		"r6 = 0;"
		"r7 = 1;"
		"goto i_loop_%=;"
	"check_zero_r6_%=:"
		"if r6 != 0 goto i_loop_%=;"
		"r6 = 1;"
		"call %[bpf_get_prandom_u32];"
		"if r0 != 42 goto check_one_r7_%=;"
		"goto i_loop_%=;"
	"check_one_r7_%=:"
		"if r7 != 1 goto i_loop_%=;"
		"r0 = r10;"
		"r0 += r8;"
		"r1 = 7;"
		"*(u64 *)(r0 + 0) = r1;"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r1 = r10;"
		"r1 += -16;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"
	"i_loop_end_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"

		/* second inner loop */
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
		"r6 = 0;"
		"r7 = 0;"
	"i2_loop_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto i2_loop_end_%=;"
	"check2_one_r6_%=:"
		"if r6 != 1 goto check2_zero_r6_%=;"
		"r6 = 0;"
		"r7 = 1;"
		"goto i2_loop_%=;"
	"check2_zero_r6_%=:"
		"if r6 != 0 goto i2_loop_%=;"
		"r6 = 1;"
		"call %[bpf_get_prandom_u32];"
		"if r0 != 42 goto check2_one_r7_%=;"
		"goto i2_loop_%=;"
	"check2_one_r7_%=:"
		"if r7 != 1 goto i2_loop_%=;"
		"r6 = 0;"
		"r8 = -25;"
		"goto i2_loop_%=;"
	"i2_loop_end_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"

		"r6 = 0;"
		"r7 = 0;"
		"goto j_loop_%=;"
	"j_loop_end_%=:"
		"r1 = r10;"
		"r1 += -16;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy)
		: __clobber_all
	);
}

SEC("?raw_tp")
__failure
__msg("math between fp pointer and register with unbounded")
__flag(BPF_F_TEST_STATE_FREQ)
__naked int loop_state_deps3(void)
{
	/* This is equivalent to a C program below.
	 *
	 *   if (random() != 24) {       // assume false branch is placed first
	 *     i = iter_new();           // fp[-8]
	 *     while (iter_next(i));
	 *     iter_destroy(i);
	 *     return;
	 *   }
	 *
	 *   for (i = 10; i > 0; i--);   // increase dfs_depth for child states
	 *
	 *   i = iter_new();             // fp[-8]
	 *   b = -24;                    // r8
	 *   for (;;) {                  // checkpoint (L)
	 *     if (iter_next(i))         // checkpoint (N)
	 *       break;
	 *     if (random() == 77) {     // assume false branch is placed first
	 *       *(u64 *)(r10 + b) = 7;  // this is not safe when b == -25
	 *       iter_destroy(i);
	 *       return;
	 *     }
	 *     if (random() == 42) {     // assume false branch is placed first
	 *       b = -25;
	 *     }
	 *   }
	 *   iter_destroy(i);
	 *
	 * In case of a buggy verifier first loop might poison
	 * env->cur_state->loop_entry with a state having 0 branches
	 * and small dfs_depth. This would trigger NOT_EXACT states
	 * comparison for some states within second loop.
	 * Specifically, checkpoint (L) might be problematic if:
	 * - branch with '*(u64 *)(r10 + b) = 7' is not explored yet;
	 * - checkpoint (L) is first reached in state {b=-24};
	 * - traversal is pruned at checkpoint (N) setting checkpoint's (L)
	 *   branch count to 0, thus making it eligible for use in pruning;
	 * - checkpoint (L) is next reached in state {b=-25},
	 *   this would cause NOT_EXACT comparison with a state {b=-24}
	 *   while 'b' is not marked precise yet.
	 */
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"if r0 == 24 goto 2f;"
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 5;"
		"call %[bpf_iter_num_new];"
	"1:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_next];"
		"if r0 != 0 goto 1b;"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"
	"2:"
		/* loop to increase dfs_depth */
		"r0 = 10;"
	"3:"
		"r0 -= 1;"
		"if r0 != 0 goto 3b;"
		/* end of loop */
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
		"r8 = -24;"
	"main_loop_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto main_loop_end_%=;"
		/* first if */
		"call %[bpf_get_prandom_u32];"
		"if r0 == 77 goto unsafe_write_%=;"
		/* second if */
		"call %[bpf_get_prandom_u32];"
		"if r0 == 42 goto poison_r8_%=;"
		/* iterate */
		"goto main_loop_%=;"
	"main_loop_end_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"

	"unsafe_write_%=:"
		"r0 = r10;"
		"r0 += r8;"
		"r1 = 7;"
		"*(u64 *)(r0 + 0) = r1;"
		"goto main_loop_end_%=;"

	"poison_r8_%=:"
		"r8 = -25;"
		"goto main_loop_%=;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy)
		: __clobber_all
	);
}

SEC("?raw_tp")
__success
__naked int triple_continue(void)
{
	/* This is equivalent to C program below.
	 * High branching factor of the loop body turned out to be
	 * problematic for one of the iterator convergence tracking
	 * algorithms explored.
	 *
	 * r6 = bpf_get_prandom_u32()
	 * bpf_iter_num_new(&fp[-8], 0, 10)
	 * while (bpf_iter_num_next(&fp[-8])) {
	 *   if (bpf_get_prandom_u32() != 42)
	 *     continue;
	 *   if (bpf_get_prandom_u32() != 42)
	 *     continue;
	 *   if (bpf_get_prandom_u32() != 42)
	 *     continue;
	 *   r0 += 0;
	 * }
	 * bpf_iter_num_destroy(&fp[-8])
	 * return 0
	 */
	asm volatile (
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
	"loop_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto loop_end_%=;"
		"call %[bpf_get_prandom_u32];"
		"if r0 != 42 goto loop_%=;"
		"call %[bpf_get_prandom_u32];"
		"if r0 != 42 goto loop_%=;"
		"call %[bpf_get_prandom_u32];"
		"if r0 != 42 goto loop_%=;"
		"r0 += 0;"
		"goto loop_%=;"
	"loop_end_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy)
		: __clobber_all
	);
}

SEC("?raw_tp")
__success
__naked int widen_spill(void)
{
	/* This is equivalent to C program below.
	 * The counter is stored in fp[-16], if this counter is not widened
	 * verifier states representing loop iterations would never converge.
	 *
	 * fp[-16] = 0
	 * bpf_iter_num_new(&fp[-8], 0, 10)
	 * while (bpf_iter_num_next(&fp[-8])) {
	 *   r0 = fp[-16];
	 *   r0 += 1;
	 *   fp[-16] = r0;
	 * }
	 * bpf_iter_num_destroy(&fp[-8])
	 * return 0
	 */
	asm volatile (
		"r0 = 0;"
		"*(u64 *)(r10 - 16) = r0;"
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
	"loop_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto loop_end_%=;"
		"r0 = *(u64 *)(r10 - 16);"
		"r0 += 1;"
		"*(u64 *)(r10 - 16) = r0;"
		"goto loop_%=;"
	"loop_end_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy)
		: __clobber_all
	);
}

SEC("raw_tp")
__success
__naked int checkpoint_states_deletion(void)
{
	/* This is equivalent to C program below.
	 *
	 *   int *a, *b, *c, *d, *e, *f;
	 *   int i, sum = 0;
	 *   bpf_for(i, 0, 10) {
	 *     a = bpf_map_lookup_elem(&amap, &i);
	 *     b = bpf_map_lookup_elem(&amap, &i);
	 *     c = bpf_map_lookup_elem(&amap, &i);
	 *     d = bpf_map_lookup_elem(&amap, &i);
	 *     e = bpf_map_lookup_elem(&amap, &i);
	 *     f = bpf_map_lookup_elem(&amap, &i);
	 *     if (a) sum += 1;
	 *     if (b) sum += 1;
	 *     if (c) sum += 1;
	 *     if (d) sum += 1;
	 *     if (e) sum += 1;
	 *     if (f) sum += 1;
	 *   }
	 *   return 0;
	 *
	 * The body of the loop spawns multiple simulation paths
	 * with different combination of NULL/non-NULL information for a/b/c/d/e/f.
	 * Each combination is unique from states_equal() point of view.
	 * Explored states checkpoint is created after each iterator next call.
	 * Iterator convergence logic expects that eventually current state
	 * would get equal to one of the explored states and thus loop
	 * exploration would be finished (at-least for a specific path).
	 * Verifier evicts explored states with high miss to hit ratio
	 * to to avoid comparing current state with too many explored
	 * states per instruction.
	 * This test is designed to "stress test" eviction policy defined using formula:
	 *
	 *    sl->miss_cnt > sl->hit_cnt * N + N // if true sl->state is evicted
	 *
	 * Currently N is set to 64, which allows for 6 variables in this test.
	 */
	asm volatile (
		"r6 = 0;"                  /* a */
		"r7 = 0;"                  /* b */
		"r8 = 0;"                  /* c */
		"*(u64 *)(r10 - 24) = r6;" /* d */
		"*(u64 *)(r10 - 32) = r6;" /* e */
		"*(u64 *)(r10 - 40) = r6;" /* f */
		"r9 = 0;"                  /* sum */
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
	"loop_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto loop_end_%=;"

		"*(u64 *)(r10 - 16) = r0;"

		"r1 = %[amap] ll;"
		"r2 = r10;"
		"r2 += -16;"
		"call %[bpf_map_lookup_elem];"
		"r6 = r0;"

		"r1 = %[amap] ll;"
		"r2 = r10;"
		"r2 += -16;"
		"call %[bpf_map_lookup_elem];"
		"r7 = r0;"

		"r1 = %[amap] ll;"
		"r2 = r10;"
		"r2 += -16;"
		"call %[bpf_map_lookup_elem];"
		"r8 = r0;"

		"r1 = %[amap] ll;"
		"r2 = r10;"
		"r2 += -16;"
		"call %[bpf_map_lookup_elem];"
		"*(u64 *)(r10 - 24) = r0;"

		"r1 = %[amap] ll;"
		"r2 = r10;"
		"r2 += -16;"
		"call %[bpf_map_lookup_elem];"
		"*(u64 *)(r10 - 32) = r0;"

		"r1 = %[amap] ll;"
		"r2 = r10;"
		"r2 += -16;"
		"call %[bpf_map_lookup_elem];"
		"*(u64 *)(r10 - 40) = r0;"

		"if r6 == 0 goto +1;"
		"r9 += 1;"
		"if r7 == 0 goto +1;"
		"r9 += 1;"
		"if r8 == 0 goto +1;"
		"r9 += 1;"
		"r0 = *(u64 *)(r10 - 24);"
		"if r0 == 0 goto +1;"
		"r9 += 1;"
		"r0 = *(u64 *)(r10 - 32);"
		"if r0 == 0 goto +1;"
		"r9 += 1;"
		"r0 = *(u64 *)(r10 - 40);"
		"if r0 == 0 goto +1;"
		"r9 += 1;"

		"goto loop_%=;"
	"loop_end_%=:"
		"r1 = r10;"
		"r1 += -8;"
		"call %[bpf_iter_num_destroy];"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_map_lookup_elem),
		  __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy),
		  __imm_addr(amap)
		: __clobber_all
	);
}

struct {
	int data[32];
	int n;
} loop_data;

SEC("raw_tp")
__success
int iter_arr_with_actual_elem_count(const void *ctx)
{
	int i, n = loop_data.n, sum = 0;

	if (n > ARRAY_SIZE(loop_data.data))
		return 0;

	bpf_for(i, 0, n) {
		/* no rechecking of i against ARRAY_SIZE(loop_data.n) */
		sum += loop_data.data[i];
	}

	return sum;
}

__u32 upper, select_n, result;
__u64 global;

static __noinline bool nest_2(char *str)
{
	/* some insns (including branch insns) to ensure stacksafe() is triggered
	 * in nest_2(). This way, stacksafe() can compare frame associated with nest_1().
	 */
	if (str[0] == 't')
		return true;
	if (str[1] == 'e')
		return true;
	if (str[2] == 's')
		return true;
	if (str[3] == 't')
		return true;
	return false;
}

static __noinline bool nest_1(int n)
{
	/* case 0: allocate stack, case 1: no allocate stack */
	switch (n) {
	case 0: {
		char comm[16];

		if (bpf_get_current_comm(comm, 16))
			return false;
		return nest_2(comm);
	}
	case 1:
		return nest_2((char *)&global);
	default:
		return false;
	}
}

SEC("raw_tp")
__success
int iter_subprog_check_stacksafe(const void *ctx)
{
	long i;

	bpf_for(i, 0, upper) {
		if (!nest_1(select_n)) {
			result = 1;
			return 0;
		}
	}

	result = 2;
	return 0;
}

struct bpf_iter_num global_it;

SEC("raw_tp")
__failure __msg("arg#0 expected pointer to an iterator on stack")
int iter_new_bad_arg(const void *ctx)
{
	bpf_iter_num_new(&global_it, 0, 1);
	return 0;
}

SEC("raw_tp")
__failure __msg("arg#0 expected pointer to an iterator on stack")
int iter_next_bad_arg(const void *ctx)
{
	bpf_iter_num_next(&global_it);
	return 0;
}

SEC("raw_tp")
__failure __msg("arg#0 expected pointer to an iterator on stack")
int iter_destroy_bad_arg(const void *ctx)
{
	bpf_iter_num_destroy(&global_it);
	return 0;
}

SEC("raw_tp")
__success
int clean_live_states(const void *ctx)
{
	char buf[1];
	int i, j, k, l, m, n, o;

	bpf_for(i, 0, 10)
	bpf_for(j, 0, 10)
	bpf_for(k, 0, 10)
	bpf_for(l, 0, 10)
	bpf_for(m, 0, 10)
	bpf_for(n, 0, 10)
	bpf_for(o, 0, 10) {
		if (unlikely(bpf_get_prandom_u32()))
			buf[0] = 42;
		bpf_printk("%s", buf);
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
