// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Facebook */

#include <string.h>
#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_kfuncs.h"
#include "errno.h"

char _license[] SEC("license") = "GPL";

int pid, err, val;

struct sample {
	int pid;
	int seq;
	long value;
	char comm[16];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 4096);
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} array_map SEC(".maps");

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_read_write(void *ctx)
{
	char write_data[64] = "hello there, world!!";
	char read_data[64] = {};
	struct bpf_dynptr ptr;
	int i;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(write_data), 0, &ptr);

	/* Write data into the dynptr */
	err = bpf_dynptr_write(&ptr, 0, write_data, sizeof(write_data), 0);

	/* Read the data that was written into the dynptr */
	err = err ?: bpf_dynptr_read(read_data, sizeof(read_data), &ptr, 0, 0);

	/* Ensure the data we read matches the data we wrote */
	for (i = 0; i < sizeof(read_data); i++) {
		if (read_data[i] != write_data[i]) {
			err = 1;
			break;
		}
	}

	bpf_ringbuf_discard_dynptr(&ptr, 0);
	return 0;
}

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_dynptr_data(void *ctx)
{
	__u32 key = 0, val = 235, *map_val;
	struct bpf_dynptr ptr;
	__u32 map_val_size;
	void *data;

	map_val_size = sizeof(*map_val);

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	bpf_map_update_elem(&array_map, &key, &val, 0);

	map_val = bpf_map_lookup_elem(&array_map, &key);
	if (!map_val) {
		err = 1;
		return 0;
	}

	bpf_dynptr_from_mem(map_val, map_val_size, 0, &ptr);

	/* Try getting a data slice that is out of range */
	data = bpf_dynptr_data(&ptr, map_val_size + 1, 1);
	if (data) {
		err = 2;
		return 0;
	}

	/* Try getting more bytes than available */
	data = bpf_dynptr_data(&ptr, 0, map_val_size + 1);
	if (data) {
		err = 3;
		return 0;
	}

	data = bpf_dynptr_data(&ptr, 0, sizeof(__u32));
	if (!data) {
		err = 4;
		return 0;
	}

	*(__u32 *)data = 999;

	err = bpf_probe_read_kernel(&val, sizeof(val), data);
	if (err)
		return 0;

	if (val != *(int *)data)
		err = 5;

	return 0;
}

static int ringbuf_callback(__u32 index, void *data)
{
	struct sample *sample;

	struct bpf_dynptr *ptr = (struct bpf_dynptr *)data;

	sample = bpf_dynptr_data(ptr, 0, sizeof(*sample));
	if (!sample)
		err = 2;
	else
		sample->pid += index;

	return 0;
}

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_ringbuf(void *ctx)
{
	struct bpf_dynptr ptr;
	struct sample *sample;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	val = 100;

	/* check that you can reserve a dynamic size reservation */
	err = bpf_ringbuf_reserve_dynptr(&ringbuf, val, 0, &ptr);

	sample = err ? NULL : bpf_dynptr_data(&ptr, 0, sizeof(*sample));
	if (!sample) {
		err = 1;
		goto done;
	}

	sample->pid = 10;

	/* Can pass dynptr to callback functions */
	bpf_loop(10, ringbuf_callback, &ptr, 0);

	if (sample->pid != 55)
		err = 2;

done:
	bpf_ringbuf_discard_dynptr(&ptr, 0);
	return 0;
}

SEC("?cgroup_skb/egress")
int test_skb_readonly(struct __sk_buff *skb)
{
	__u8 write_data[2] = {1, 2};
	struct bpf_dynptr ptr;
	int ret;

	if (bpf_dynptr_from_skb(skb, 0, &ptr)) {
		err = 1;
		return 1;
	}

	/* since cgroup skbs are read only, writes should fail */
	ret = bpf_dynptr_write(&ptr, 0, write_data, sizeof(write_data), 0);
	if (ret != -EINVAL) {
		err = 2;
		return 1;
	}

	return 1;
}

SEC("?cgroup_skb/egress")
int test_dynptr_skb_data(struct __sk_buff *skb)
{
	struct bpf_dynptr ptr;
	__u64 *data;

	if (bpf_dynptr_from_skb(skb, 0, &ptr)) {
		err = 1;
		return 1;
	}

	/* This should return NULL. Must use bpf_dynptr_slice API */
	data = bpf_dynptr_data(&ptr, 0, 1);
	if (data) {
		err = 2;
		return 1;
	}

	return 1;
}

SEC("tp/syscalls/sys_enter_nanosleep")
int test_adjust(void *ctx)
{
	struct bpf_dynptr ptr;
	__u32 bytes = 64;
	__u32 off = 10;
	__u32 trim = 15;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	err = bpf_ringbuf_reserve_dynptr(&ringbuf, bytes, 0, &ptr);
	if (err) {
		err = 1;
		goto done;
	}

	if (bpf_dynptr_size(&ptr) != bytes) {
		err = 2;
		goto done;
	}

	/* Advance the dynptr by off */
	err = bpf_dynptr_adjust(&ptr, off, bpf_dynptr_size(&ptr));
	if (err) {
		err = 3;
		goto done;
	}

	if (bpf_dynptr_size(&ptr) != bytes - off) {
		err = 4;
		goto done;
	}

	/* Trim the dynptr */
	err = bpf_dynptr_adjust(&ptr, off, 15);
	if (err) {
		err = 5;
		goto done;
	}

	/* Check that the size was adjusted correctly */
	if (bpf_dynptr_size(&ptr) != trim - off) {
		err = 6;
		goto done;
	}

done:
	bpf_ringbuf_discard_dynptr(&ptr, 0);
	return 0;
}

SEC("tp/syscalls/sys_enter_nanosleep")
int test_adjust_err(void *ctx)
{
	char write_data[45] = "hello there, world!!";
	struct bpf_dynptr ptr;
	__u32 size = 64;
	__u32 off = 20;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	if (bpf_ringbuf_reserve_dynptr(&ringbuf, size, 0, &ptr)) {
		err = 1;
		goto done;
	}

	/* Check that start can't be greater than end */
	if (bpf_dynptr_adjust(&ptr, 5, 1) != -EINVAL) {
		err = 2;
		goto done;
	}

	/* Check that start can't be greater than size */
	if (bpf_dynptr_adjust(&ptr, size + 1, size + 1) != -ERANGE) {
		err = 3;
		goto done;
	}

	/* Check that end can't be greater than size */
	if (bpf_dynptr_adjust(&ptr, 0, size + 1) != -ERANGE) {
		err = 4;
		goto done;
	}

	if (bpf_dynptr_adjust(&ptr, off, size)) {
		err = 5;
		goto done;
	}

	/* Check that you can't write more bytes than available into the dynptr
	 * after you've adjusted it
	 */
	if (bpf_dynptr_write(&ptr, 0, &write_data, sizeof(write_data), 0) != -E2BIG) {
		err = 6;
		goto done;
	}

	/* Check that even after adjusting, submitting/discarding
	 * a ringbuf dynptr works
	 */
	bpf_ringbuf_submit_dynptr(&ptr, 0);
	return 0;

done:
	bpf_ringbuf_discard_dynptr(&ptr, 0);
	return 0;
}

SEC("tp/syscalls/sys_enter_nanosleep")
int test_zero_size_dynptr(void *ctx)
{
	char write_data = 'x', read_data;
	struct bpf_dynptr ptr;
	__u32 size = 64;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	if (bpf_ringbuf_reserve_dynptr(&ringbuf, size, 0, &ptr)) {
		err = 1;
		goto done;
	}

	/* After this, the dynptr has a size of 0 */
	if (bpf_dynptr_adjust(&ptr, size, size)) {
		err = 2;
		goto done;
	}

	/* Test that reading + writing non-zero bytes is not ok */
	if (bpf_dynptr_read(&read_data, sizeof(read_data), &ptr, 0, 0) != -E2BIG) {
		err = 3;
		goto done;
	}

	if (bpf_dynptr_write(&ptr, 0, &write_data, sizeof(write_data), 0) != -E2BIG) {
		err = 4;
		goto done;
	}

	/* Test that reading + writing 0 bytes from a 0-size dynptr is ok */
	if (bpf_dynptr_read(&read_data, 0, &ptr, 0, 0)) {
		err = 5;
		goto done;
	}

	if (bpf_dynptr_write(&ptr, 0, &write_data, 0, 0)) {
		err = 6;
		goto done;
	}

	err = 0;

done:
	bpf_ringbuf_discard_dynptr(&ptr, 0);
	return 0;
}

SEC("tp/syscalls/sys_enter_nanosleep")
int test_dynptr_is_null(void *ctx)
{
	struct bpf_dynptr ptr1;
	struct bpf_dynptr ptr2;
	__u64 size = 4;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	/* Pass in invalid flags, get back an invalid dynptr */
	if (bpf_ringbuf_reserve_dynptr(&ringbuf, size, 123, &ptr1) != -EINVAL) {
		err = 1;
		goto exit_early;
	}

	/* Test that the invalid dynptr is null */
	if (!bpf_dynptr_is_null(&ptr1)) {
		err = 2;
		goto exit_early;
	}

	/* Get a valid dynptr */
	if (bpf_ringbuf_reserve_dynptr(&ringbuf, size, 0, &ptr2)) {
		err = 3;
		goto exit;
	}

	/* Test that the valid dynptr is not null */
	if (bpf_dynptr_is_null(&ptr2)) {
		err = 4;
		goto exit;
	}

exit:
	bpf_ringbuf_discard_dynptr(&ptr2, 0);
exit_early:
	bpf_ringbuf_discard_dynptr(&ptr1, 0);
	return 0;
}

SEC("cgroup_skb/egress")
int test_dynptr_is_rdonly(struct __sk_buff *skb)
{
	struct bpf_dynptr ptr1;
	struct bpf_dynptr ptr2;
	struct bpf_dynptr ptr3;

	/* Pass in invalid flags, get back an invalid dynptr */
	if (bpf_dynptr_from_skb(skb, 123, &ptr1) != -EINVAL) {
		err = 1;
		return 0;
	}

	/* Test that an invalid dynptr is_rdonly returns false */
	if (bpf_dynptr_is_rdonly(&ptr1)) {
		err = 2;
		return 0;
	}

	/* Get a read-only dynptr */
	if (bpf_dynptr_from_skb(skb, 0, &ptr2)) {
		err = 3;
		return 0;
	}

	/* Test that the dynptr is read-only */
	if (!bpf_dynptr_is_rdonly(&ptr2)) {
		err = 4;
		return 0;
	}

	/* Get a read-writeable dynptr */
	if (bpf_ringbuf_reserve_dynptr(&ringbuf, 64, 0, &ptr3)) {
		err = 5;
		goto done;
	}

	/* Test that the dynptr is read-only */
	if (bpf_dynptr_is_rdonly(&ptr3)) {
		err = 6;
		goto done;
	}

done:
	bpf_ringbuf_discard_dynptr(&ptr3, 0);
	return 0;
}

SEC("cgroup_skb/egress")
int test_dynptr_clone(struct __sk_buff *skb)
{
	struct bpf_dynptr ptr1;
	struct bpf_dynptr ptr2;
	__u32 off = 2, size;

	/* Get a dynptr */
	if (bpf_dynptr_from_skb(skb, 0, &ptr1)) {
		err = 1;
		return 0;
	}

	if (bpf_dynptr_adjust(&ptr1, off, bpf_dynptr_size(&ptr1))) {
		err = 2;
		return 0;
	}

	/* Clone the dynptr */
	if (bpf_dynptr_clone(&ptr1, &ptr2)) {
		err = 3;
		return 0;
	}

	size = bpf_dynptr_size(&ptr1);

	/* Check that the clone has the same size and rd-only */
	if (bpf_dynptr_size(&ptr2) != size) {
		err = 4;
		return 0;
	}

	if (bpf_dynptr_is_rdonly(&ptr2) != bpf_dynptr_is_rdonly(&ptr1)) {
		err = 5;
		return 0;
	}

	/* Advance and trim the original dynptr */
	bpf_dynptr_adjust(&ptr1, 5, 5);

	/* Check that only original dynptr was affected, and the clone wasn't */
	if (bpf_dynptr_size(&ptr2) != size) {
		err = 6;
		return 0;
	}

	return 0;
}

SEC("?cgroup_skb/egress")
int test_dynptr_skb_no_buff(struct __sk_buff *skb)
{
	struct bpf_dynptr ptr;
	__u64 *data;

	if (bpf_dynptr_from_skb(skb, 0, &ptr)) {
		err = 1;
		return 1;
	}

	/* This may return NULL. SKB may require a buffer */
	data = bpf_dynptr_slice(&ptr, 0, NULL, 1);

	return !!data;
}

SEC("?cgroup_skb/egress")
int test_dynptr_skb_strcmp(struct __sk_buff *skb)
{
	struct bpf_dynptr ptr;
	char *data;

	if (bpf_dynptr_from_skb(skb, 0, &ptr)) {
		err = 1;
		return 1;
	}

	/* This may return NULL. SKB may require a buffer */
	data = bpf_dynptr_slice(&ptr, 0, NULL, 10);
	if (data) {
		bpf_strncmp(data, 10, "foo");
		return 1;
	}

	return 1;
}
