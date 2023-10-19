// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Facebook */

#include <errno.h>
#include <string.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct test_info {
	int x;
	struct bpf_dynptr ptr;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_dynptr);
} array_map1 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct test_info);
} array_map2 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} array_map3 SEC(".maps");

struct sample {
	int pid;
	long value;
	char comm[16];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} ringbuf SEC(".maps");

int err, val;

static int get_map_val_dynptr(struct bpf_dynptr *ptr)
{
	__u32 key = 0, *map_val;

	bpf_map_update_elem(&array_map3, &key, &val, 0);

	map_val = bpf_map_lookup_elem(&array_map3, &key);
	if (!map_val)
		return -ENOENT;

	bpf_dynptr_from_mem(map_val, sizeof(*map_val), 0, ptr);

	return 0;
}

/* Every bpf_ringbuf_reserve_dynptr call must have a corresponding
 * bpf_ringbuf_submit/discard_dynptr call
 */
SEC("?raw_tp")
int ringbuf_missing_release1(void *ctx)
{
	struct bpf_dynptr ptr;

	bpf_ringbuf_reserve_dynptr(&ringbuf, val, 0, &ptr);

	/* missing a call to bpf_ringbuf_discard/submit_dynptr */

	return 0;
}

SEC("?raw_tp")
int ringbuf_missing_release2(void *ctx)
{
	struct bpf_dynptr ptr1, ptr2;
	struct sample *sample;

	bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(*sample), 0, &ptr1);
	bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(*sample), 0, &ptr2);

	sample = bpf_dynptr_data(&ptr1, 0, sizeof(*sample));
	if (!sample) {
		bpf_ringbuf_discard_dynptr(&ptr1, 0);
		bpf_ringbuf_discard_dynptr(&ptr2, 0);
		return 0;
	}

	bpf_ringbuf_submit_dynptr(&ptr1, 0);

	/* missing a call to bpf_ringbuf_discard/submit_dynptr on ptr2 */

	return 0;
}

static int missing_release_callback_fn(__u32 index, void *data)
{
	struct bpf_dynptr ptr;

	bpf_ringbuf_reserve_dynptr(&ringbuf, val, 0, &ptr);

	/* missing a call to bpf_ringbuf_discard/submit_dynptr */

	return 0;
}

/* Any dynptr initialized within a callback must have bpf_dynptr_put called */
SEC("?raw_tp")
int ringbuf_missing_release_callback(void *ctx)
{
	bpf_loop(10, missing_release_callback_fn, NULL, 0);
	return 0;
}

/* Can't call bpf_ringbuf_submit/discard_dynptr on a non-initialized dynptr */
SEC("?raw_tp")
int ringbuf_release_uninit_dynptr(void *ctx)
{
	struct bpf_dynptr ptr;

	/* this should fail */
	bpf_ringbuf_submit_dynptr(&ptr, 0);

	return 0;
}

/* A dynptr can't be used after it has been invalidated */
SEC("?raw_tp")
int use_after_invalid(void *ctx)
{
	struct bpf_dynptr ptr;
	char read_data[64];

	bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(read_data), 0, &ptr);

	bpf_dynptr_read(read_data, sizeof(read_data), &ptr, 0, 0);

	bpf_ringbuf_submit_dynptr(&ptr, 0);

	/* this should fail */
	bpf_dynptr_read(read_data, sizeof(read_data), &ptr, 0, 0);

	return 0;
}

/* Can't call non-dynptr ringbuf APIs on a dynptr ringbuf sample */
SEC("?raw_tp")
int ringbuf_invalid_api(void *ctx)
{
	struct bpf_dynptr ptr;
	struct sample *sample;

	bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(*sample), 0, &ptr);
	sample = bpf_dynptr_data(&ptr, 0, sizeof(*sample));
	if (!sample)
		goto done;

	sample->pid = 123;

	/* invalid API use. need to use dynptr API to submit/discard */
	bpf_ringbuf_submit(sample, 0);

done:
	bpf_ringbuf_discard_dynptr(&ptr, 0);
	return 0;
}

/* Can't add a dynptr to a map */
SEC("?raw_tp")
int add_dynptr_to_map1(void *ctx)
{
	struct bpf_dynptr ptr;
	int key = 0;

	bpf_ringbuf_reserve_dynptr(&ringbuf, val, 0, &ptr);

	/* this should fail */
	bpf_map_update_elem(&array_map1, &key, &ptr, 0);

	bpf_ringbuf_submit_dynptr(&ptr, 0);

	return 0;
}

/* Can't add a struct with an embedded dynptr to a map */
SEC("?raw_tp")
int add_dynptr_to_map2(void *ctx)
{
	struct test_info x;
	int key = 0;

	bpf_ringbuf_reserve_dynptr(&ringbuf, val, 0, &x.ptr);

	/* this should fail */
	bpf_map_update_elem(&array_map2, &key, &x, 0);

	bpf_ringbuf_submit_dynptr(&x.ptr, 0);

	return 0;
}

/* A data slice can't be accessed out of bounds */
SEC("?raw_tp")
int data_slice_out_of_bounds_ringbuf(void *ctx)
{
	struct bpf_dynptr ptr;
	void *data;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 8, 0, &ptr);

	data  = bpf_dynptr_data(&ptr, 0, 8);
	if (!data)
		goto done;

	/* can't index out of bounds of the data slice */
	val = *((char *)data + 8);

done:
	bpf_ringbuf_submit_dynptr(&ptr, 0);
	return 0;
}

SEC("?raw_tp")
int data_slice_out_of_bounds_map_value(void *ctx)
{
	__u32 key = 0, map_val;
	struct bpf_dynptr ptr;
	void *data;

	get_map_val_dynptr(&ptr);

	data  = bpf_dynptr_data(&ptr, 0, sizeof(map_val));
	if (!data)
		return 0;

	/* can't index out of bounds of the data slice */
	val = *((char *)data + (sizeof(map_val) + 1));

	return 0;
}

/* A data slice can't be used after it has been released */
SEC("?raw_tp")
int data_slice_use_after_release1(void *ctx)
{
	struct bpf_dynptr ptr;
	struct sample *sample;

	bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(*sample), 0, &ptr);
	sample = bpf_dynptr_data(&ptr, 0, sizeof(*sample));
	if (!sample)
		goto done;

	sample->pid = 123;

	bpf_ringbuf_submit_dynptr(&ptr, 0);

	/* this should fail */
	val = sample->pid;

	return 0;

done:
	bpf_ringbuf_discard_dynptr(&ptr, 0);
	return 0;
}

/* A data slice can't be used after it has been released.
 *
 * This tests the case where the data slice tracks a dynptr (ptr2)
 * that is at a non-zero offset from the frame pointer (ptr1 is at fp,
 * ptr2 is at fp - 16).
 */
SEC("?raw_tp")
int data_slice_use_after_release2(void *ctx)
{
	struct bpf_dynptr ptr1, ptr2;
	struct sample *sample;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 64, 0, &ptr1);
	bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(*sample), 0, &ptr2);

	sample = bpf_dynptr_data(&ptr2, 0, sizeof(*sample));
	if (!sample)
		goto done;

	sample->pid = 23;

	bpf_ringbuf_submit_dynptr(&ptr2, 0);

	/* this should fail */
	sample->pid = 23;

	bpf_ringbuf_submit_dynptr(&ptr1, 0);

	return 0;

done:
	bpf_ringbuf_discard_dynptr(&ptr2, 0);
	bpf_ringbuf_discard_dynptr(&ptr1, 0);
	return 0;
}

/* A data slice must be first checked for NULL */
SEC("?raw_tp")
int data_slice_missing_null_check1(void *ctx)
{
	struct bpf_dynptr ptr;
	void *data;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 8, 0, &ptr);

	data  = bpf_dynptr_data(&ptr, 0, 8);

	/* missing if (!data) check */

	/* this should fail */
	*(__u8 *)data = 3;

	bpf_ringbuf_submit_dynptr(&ptr, 0);
	return 0;
}

/* A data slice can't be dereferenced if it wasn't checked for null */
SEC("?raw_tp")
int data_slice_missing_null_check2(void *ctx)
{
	struct bpf_dynptr ptr;
	__u64 *data1, *data2;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 16, 0, &ptr);

	data1 = bpf_dynptr_data(&ptr, 0, 8);
	data2 = bpf_dynptr_data(&ptr, 0, 8);
	if (data1)
		/* this should fail */
		*data2 = 3;

done:
	bpf_ringbuf_discard_dynptr(&ptr, 0);
	return 0;
}

/* Can't pass in a dynptr as an arg to a helper function that doesn't take in a
 * dynptr argument
 */
SEC("?raw_tp")
int invalid_helper1(void *ctx)
{
	struct bpf_dynptr ptr;

	get_map_val_dynptr(&ptr);

	/* this should fail */
	bpf_strncmp((const char *)&ptr, sizeof(ptr), "hello!");

	return 0;
}

/* A dynptr can't be passed into a helper function at a non-zero offset */
SEC("?raw_tp")
int invalid_helper2(void *ctx)
{
	struct bpf_dynptr ptr;
	char read_data[64];

	get_map_val_dynptr(&ptr);

	/* this should fail */
	bpf_dynptr_read(read_data, sizeof(read_data), (void *)&ptr + 8, 0, 0);

	return 0;
}

/* A bpf_dynptr is invalidated if it's been written into */
SEC("?raw_tp")
int invalid_write1(void *ctx)
{
	struct bpf_dynptr ptr;
	void *data;
	__u8 x = 0;

	get_map_val_dynptr(&ptr);

	memcpy(&ptr, &x, sizeof(x));

	/* this should fail */
	data = bpf_dynptr_data(&ptr, 0, 1);

	return 0;
}

/*
 * A bpf_dynptr can't be used as a dynptr if it has been written into at a fixed
 * offset
 */
SEC("?raw_tp")
int invalid_write2(void *ctx)
{
	struct bpf_dynptr ptr;
	char read_data[64];
	__u8 x = 0;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 64, 0, &ptr);

	memcpy((void *)&ptr + 8, &x, sizeof(x));

	/* this should fail */
	bpf_dynptr_read(read_data, sizeof(read_data), &ptr, 0, 0);

	bpf_ringbuf_submit_dynptr(&ptr, 0);

	return 0;
}

/*
 * A bpf_dynptr can't be used as a dynptr if it has been written into at a
 * non-const offset
 */
SEC("?raw_tp")
int invalid_write3(void *ctx)
{
	struct bpf_dynptr ptr;
	char stack_buf[16];
	unsigned long len;
	__u8 x = 0;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 8, 0, &ptr);

	memcpy(stack_buf, &val, sizeof(val));
	len = stack_buf[0] & 0xf;

	memcpy((void *)&ptr + len, &x, sizeof(x));

	/* this should fail */
	bpf_ringbuf_submit_dynptr(&ptr, 0);

	return 0;
}

static int invalid_write4_callback(__u32 index, void *data)
{
	*(__u32 *)data = 123;

	return 0;
}

/* If the dynptr is written into in a callback function, it should
 * be invalidated as a dynptr
 */
SEC("?raw_tp")
int invalid_write4(void *ctx)
{
	struct bpf_dynptr ptr;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 64, 0, &ptr);

	bpf_loop(10, invalid_write4_callback, &ptr, 0);

	/* this should fail */
	bpf_ringbuf_submit_dynptr(&ptr, 0);

	return 0;
}

/* A globally-defined bpf_dynptr can't be used (it must reside as a stack frame) */
struct bpf_dynptr global_dynptr;
SEC("?raw_tp")
int global(void *ctx)
{
	/* this should fail */
	bpf_ringbuf_reserve_dynptr(&ringbuf, 16, 0, &global_dynptr);

	bpf_ringbuf_discard_dynptr(&global_dynptr, 0);

	return 0;
}

/* A direct read should fail */
SEC("?raw_tp")
int invalid_read1(void *ctx)
{
	struct bpf_dynptr ptr;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 64, 0, &ptr);

	/* this should fail */
	val = *(int *)&ptr;

	bpf_ringbuf_discard_dynptr(&ptr, 0);

	return 0;
}

/* A direct read at an offset should fail */
SEC("?raw_tp")
int invalid_read2(void *ctx)
{
	struct bpf_dynptr ptr;
	char read_data[64];

	get_map_val_dynptr(&ptr);

	/* this should fail */
	bpf_dynptr_read(read_data, sizeof(read_data), (void *)&ptr + 1, 0, 0);

	return 0;
}

/* A direct read at an offset into the lower stack slot should fail */
SEC("?raw_tp")
int invalid_read3(void *ctx)
{
	struct bpf_dynptr ptr1, ptr2;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 16, 0, &ptr1);
	bpf_ringbuf_reserve_dynptr(&ringbuf, 16, 0, &ptr2);

	/* this should fail */
	memcpy(&val, (void *)&ptr1 + 8, sizeof(val));

	bpf_ringbuf_discard_dynptr(&ptr1, 0);
	bpf_ringbuf_discard_dynptr(&ptr2, 0);

	return 0;
}

static int invalid_read4_callback(__u32 index, void *data)
{
	/* this should fail */
	val = *(__u32 *)data;

	return 0;
}

/* A direct read within a callback function should fail */
SEC("?raw_tp")
int invalid_read4(void *ctx)
{
	struct bpf_dynptr ptr;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 64, 0, &ptr);

	bpf_loop(10, invalid_read4_callback, &ptr, 0);

	bpf_ringbuf_submit_dynptr(&ptr, 0);

	return 0;
}

/* Initializing a dynptr on an offset should fail */
SEC("?raw_tp")
int invalid_offset(void *ctx)
{
	struct bpf_dynptr ptr;

	/* this should fail */
	bpf_ringbuf_reserve_dynptr(&ringbuf, 64, 0, &ptr + 1);

	bpf_ringbuf_discard_dynptr(&ptr, 0);

	return 0;
}

/* Can't release a dynptr twice */
SEC("?raw_tp")
int release_twice(void *ctx)
{
	struct bpf_dynptr ptr;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 16, 0, &ptr);

	bpf_ringbuf_discard_dynptr(&ptr, 0);

	/* this second release should fail */
	bpf_ringbuf_discard_dynptr(&ptr, 0);

	return 0;
}

static int release_twice_callback_fn(__u32 index, void *data)
{
	/* this should fail */
	bpf_ringbuf_discard_dynptr(data, 0);

	return 0;
}

/* Test that releasing a dynptr twice, where one of the releases happens
 * within a calback function, fails
 */
SEC("?raw_tp")
int release_twice_callback(void *ctx)
{
	struct bpf_dynptr ptr;

	bpf_ringbuf_reserve_dynptr(&ringbuf, 32, 0, &ptr);

	bpf_ringbuf_discard_dynptr(&ptr, 0);

	bpf_loop(10, release_twice_callback_fn, &ptr, 0);

	return 0;
}

/* Reject unsupported local mem types for dynptr_from_mem API */
SEC("?raw_tp")
int dynptr_from_mem_invalid_api(void *ctx)
{
	struct bpf_dynptr ptr;
	int x = 0;

	/* this should fail */
	bpf_dynptr_from_mem(&x, sizeof(x), 0, &ptr);

	return 0;
}
