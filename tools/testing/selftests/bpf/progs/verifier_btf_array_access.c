// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"
#include "bpf_misc.h"

/*
 * Variable-offset access into arrays within a BTF access chain, e.g. a[i].b,
 * where 'i' is a register with a varying offset. The register's step has to
 * match the stride of one of the arrays crossed while walking the access
 * chain, and the maximal possible offset has to stay within that array.
 *
 * A program-allocated object (bpf_obj_new) is used as the source of a
 * PTR_TO_BTF_ID with a shape we fully control.
 */
struct inner {
	int a;
	int b;
};

struct outer {
	struct inner arr[8];	/* off 0,   stride 8, size 64 */
	char bytes[32];		/* off 64,  stride 1, size 32 */
	long longs[8];		/* off 96,  stride 8, size 64 */
	int grid[4][4];		/* off 160, 16 ints,  size 64 */
};

/* arr[i].b, i in [0, 7], 4-byte read: stays within arr. */
SEC("syscall")
__success
int arr_field_in_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct outer *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 7;						\
	r2 *= 8;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 4);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/* arr[i].b, i in [0, 15]: max offset runs past the end of arr. */
SEC("syscall")
__failure
__msg("invalid variable offset access into struct outer")
int arr_field_out_of_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct outer *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 15;						\
	r2 *= 8;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 4);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/* longs[i], i in [0, 7], 8-byte read: scalar array with stride 8. */
SEC("syscall")
__success
int long_array_in_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct outer *o;
	long val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 7;						\
	r2 *= 8;						\
	r1 += r2;						\
	%[val] = *(u64 *)(r1 + 96);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/*
 * bytes[i], i in [0, 31], read as 4 bytes: min_off is valid within the array,
 * but the 4-byte read at max_off runs past the array end. Exercises the
 * size-aware bound in btf_struct_access().
 */
SEC("syscall")
__failure
__flag(BPF_F_ANY_ALIGNMENT)
__msg("invalid variable offset access into struct outer")
int byte_array_size_spanning(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct outer *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 31;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 64);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/* bytes[i], i in [0, 31], 1-byte read: step 1 matches the char array stride. */
SEC("syscall")
__success
int byte_array_in_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct outer *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 31;						\
	r1 += r2;						\
	%[val] = *(u8 *)(r1 + 64);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/*
 * arr[] accessed with step 4 (half of sizeof(struct inner)): the step is not
 * a whole number of elements, so no crossed array has a matching stride.
 */
SEC("syscall")
__failure
__msg("invalid variable offset access into struct outer")
int arr_unaligned_step(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct outer *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 7;						\
	r2 *= 4;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 0);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/*
 * grid[i][0], i in [0, 3]: steps along the outer dimension of a 2D array
 * (stride 16). __btf_resolve_size() linearizes the array; the step is a
 * multiple of the innermost element size (4).
 */
SEC("syscall")
__success
int grid_outer_dim(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct outer *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 3;						\
	r2 *= 16;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 160);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/*
 * grid[0][i], i in [0, 3]: steps along the inner dimension of a 2D array
 * (stride 4, the innermost element size).
 */
SEC("syscall")
__success
int grid_inner_dim(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct outer *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 3;						\
	r2 *= 4;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 160);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/*
 * An array of pointers within a program-allocated object. Reading a pointer
 * member of a local object yields a SCALAR_VALUE, but the varying offset still
 * has to be validated against the array bounds.
 */
struct with_ptrs {
	struct inner *parr[8];	/* off 0, stride 8 (sizeof ptr), size 64 */
};

/* parr[i], i in [0, 7], 8-byte read: stays within parr. */
SEC("syscall")
__success
int ptr_array_in_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct with_ptrs *o;
	long val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 7;						\
	r2 *= 8;						\
	r1 += r2;						\
	%[val] = *(u64 *)(r1 + 0);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/*
 * parr[i], i in [0, 15]: max offset runs past the end of parr. Without the
 * variable-offset check on the WALK_PTR path this would be wrongly accepted.
 */
SEC("syscall")
__failure
__msg("invalid variable offset access into struct with_ptrs")
int ptr_array_out_of_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct with_ptrs *o;
	long val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 15;						\
	r2 *= 8;						\
	r1 += r2;						\
	%[val] = *(u64 *)(r1 + 0);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/*
 * The cases above all use bpf_obj_new() (PTR_TO_BTF_ID | MEM_ALLOC). Exercise
 * the trusted kernel-BTF path too, using a task_struct from
 * bpf_get_current_task_btf() and its embedded char comm[] array. The comm
 * offset is folded into the pointer as a constant before the varying index.
 */

/* task->comm[i], i in [0, 15], 1-byte read: stays within comm. */
SEC("syscall")
__success
int kernel_btf_array_in_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct task_struct *task;
	int val = 0;

	task = bpf_get_current_task_btf();
	asm volatile ("						\
	r1 = %[task];						\
	r1 += %[off];						\
	r2 = %[i];						\
	r2 &= 15;						\
	r1 += r2;						\
	%[val] = *(u8 *)(r1 + 0);				\
"	: [val] "=r"(val)
	: [task] "r"(task), [i] "r"(i),
	  [off] "i"(offsetof(struct task_struct, comm))
	: "r1", "r2");
	return val;
}

/* task->comm[i], i in [0, 31]: max offset runs past comm. */
SEC("syscall")
__failure
__msg("invalid variable offset access into struct task_struct")
int kernel_btf_array_out_of_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct task_struct *task;
	int val = 0;

	task = bpf_get_current_task_btf();
	asm volatile ("						\
	r1 = %[task];						\
	r1 += %[off];						\
	r2 = %[i];						\
	r2 &= 31;						\
	r1 += r2;						\
	%[val] = *(u8 *)(r1 + 0);				\
"	: [val] "=r"(val)
	: [task] "r"(task), [i] "r"(i),
	  [off] "i"(offsetof(struct task_struct, comm))
	: "r1", "r2");
	return val;
}

/*
 * Array nested below a struct member at a non-zero offset. The walk descends
 * through 'm' (resetting the running offset) before reaching 'arr', exercising
 * the array_start = min_off - arrays[i].off telescoping across a WALK_STRUCT
 * dive.
 */
struct mid {
	struct inner arr[8];	/* stride 8, size 64 */
};

struct nest {
	long pad;		/* off 0 */
	struct mid m;		/* off 8 */
};

/* m.arr[i].b, i in [0, 7], 4-byte read: stays within m.arr. */
SEC("syscall")
__success
int nested_struct_in_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct nest *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 7;						\
	r2 *= 8;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 12);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/* m.arr[i].b, i in [0, 15]: max offset runs past the end of m.arr. */
SEC("syscall")
__failure
__msg("invalid variable offset access into struct nest")
int nested_struct_out_of_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct nest *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 15;						\
	r2 *= 8;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 12);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

/*
 * A trailing flexible array member makes the struct tail an unbounded region,
 * so a varying access into it has no upper bound to exceed (mirrors how
 * unix_address.name[] is accessed via sun_path[i]).
 *
 * A custom (program) BTF struct can only be reached as a PTR_TO_BTF_ID through
 * bpf_obj_new(), which does not allow flexible array members. So use a kernel
 * type via an untrusted PTR_TO_BTF_ID (bpf_core_cast()). struct vring_used ends
 * with a flexible array 'ring[]' of struct vring_used_elem { __virtio32 id, len; },
 * mirroring a 'struct foo { int a; int b; }' flexible array.
 */

/* ring[i].len, i in [0, 63], 4-byte read: the flexible array has no upper bound. */
SEC("syscall")
__success
int flex_array_field(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct vring_used *o;
	int val = 0;

	o = bpf_core_cast(bpf_get_current_task_btf(), struct vring_used);
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 63;						\
	r2 *= 8;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 8);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	return val;
}

/*
 * ring[] accessed with step 4 (half of sizeof(struct vring_used_elem)): the
 * step is not a whole number of elements, so the flexible array stride does
 * not match.
 */
SEC("syscall")
__failure
__msg("invalid variable offset access into struct vring_used")
int flex_array_unaligned_step(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct vring_used *o;
	int val = 0;

	o = bpf_core_cast(bpf_get_current_task_btf(), struct vring_used);
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 7;						\
	r2 *= 4;						\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 4);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i)
	: "r1", "r2");
	return val;
}

/*
 * Access either name[0].sun_path[0] or one byte past sun_path.
 * Step 108 does not match sizeof(struct sockaddr_un), which is 110.
 * It matches the inner char array's stride, but exceeds its bounds.
 * The flexible-tail exemption must not apply to that fixed array.
 */
SEC("syscall")
__failure
__msg("invalid variable offset access into struct unix_address")
int flex_array_inner_array_out_of_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct unix_address *o;
	int val = 0;

	o = bpf_core_cast(bpf_get_current_task_btf(), struct unix_address);
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 1;						\
	r2 *= %[len];						\
	r1 += r2;						\
	%[val] = *(u8 *)(r1 + %[off]);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i),
	  __imm_const(len, sizeof_field(struct sockaddr_un, sun_path)),
	  __imm_const(off, offsetof(struct unix_address, name) +
			   offsetof(struct sockaddr_un, sun_path))
	: "r1", "r2");
	return val;
}

typedef int array_element_t;
typedef array_element_t array_type_t[4];

struct typedef_array {
	array_type_t values;
};

/*
 * Resolving values' size crosses both the array and element typedefs.
 * Record the array ID, not the int ID left by element-type resolution.
 * values[i], i in [0, 3], must pass the variable-offset array check.
 */
SEC("syscall")
__success
int typedef_array_in_bounds(void *ctx)
{
	unsigned long i = bpf_get_prandom_u32();
	struct typedef_array *o;
	int val = 0;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		return 0;
	asm volatile ("						\
	r1 = %[o];						\
	r2 = %[i];						\
	r2 &= 3;						\
	r2 *= %[elem_size];					\
	r1 += r2;						\
	%[val] = *(u32 *)(r1 + 0);				\
"	: [val] "=r"(val)
	: [o] "r"(o), [i] "r"(i),
	  __imm_const(elem_size, sizeof(array_element_t))
	: "r1", "r2");
	bpf_obj_drop(o);
	return val;
}

char _license[] SEC("license") = "GPL";
