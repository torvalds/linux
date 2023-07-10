// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static volatile int zero = 0;

int my_pid;
int arr[256];
int small_arr[16] SEC(".data.small_arr");

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
		  [small_arr]"p"(small_arr),
		  [zero]"p"(zero),
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
#pragma nounroll
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
#pragma nounroll
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
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 1000);
} arr_map SEC(".maps");

SEC("?raw_tp")
__failure __msg("invalid mem access 'scalar'")
int iter_err_too_permissive1(const void *ctx)
{
	int *map_val = NULL;
	int key = 0;

	MY_PID_GUARD();

	map_val = bpf_map_lookup_elem(&arr_map, &key);
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

	map_val = bpf_map_lookup_elem(&arr_map, &key);
	if (!map_val)
		return 0;

	bpf_repeat(1000000) {
		map_val = bpf_map_lookup_elem(&arr_map, &key);
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
		map_val = bpf_map_lookup_elem(&arr_map, &key);
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
		map_val = bpf_map_lookup_elem(&arr_map, &key);
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
	int *t, i, sum = 0;;

	while ((t = bpf_iter_num_next(it))) {
		i = *t;
		if (i >= n)
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

char _license[] SEC("license") = "GPL";
