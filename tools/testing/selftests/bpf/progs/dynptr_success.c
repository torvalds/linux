// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Facebook */

#include <vmlinux.h>
#include <string.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "errno.h"

#define PAGE_SIZE_64K 65536

char _license[] SEC("license") = "GPL";

int pid, err, val;

struct ringbuf_sample {
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
	struct ringbuf_sample *sample;

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
	struct ringbuf_sample *sample;

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

SEC("?tc")
int test_dynptr_skb_meta_data(struct __sk_buff *skb)
{
	struct bpf_dynptr meta;
	__u8 *md;
	int ret;

	err = 1;
	ret = bpf_dynptr_from_skb_meta(skb, 0, &meta);
	if (ret)
		return 1;

	/* This should return NULL. Must use bpf_dynptr_slice API */
	err = 2;
	md = bpf_dynptr_data(&meta, 0, sizeof(*md));
	if (md)
		return 1;

	err = 0;
	return 1;
}

/* Check that skb metadata dynptr ops don't accept any flags. */
SEC("?tc")
int test_dynptr_skb_meta_flags(struct __sk_buff *skb)
{
	const __u64 INVALID_FLAGS = ~0ULL;
	struct bpf_dynptr meta;
	__u8 buf;
	int ret;

	err = 1;
	ret = bpf_dynptr_from_skb_meta(skb, INVALID_FLAGS, &meta);
	if (ret != -EINVAL)
		return 1;

	err = 2;
	ret = bpf_dynptr_from_skb_meta(skb, 0, &meta);
	if (ret)
		return 1;

	err = 3;
	ret = bpf_dynptr_read(&buf, 0, &meta, 0, INVALID_FLAGS);
	if (ret != -EINVAL)
		return 1;

	err = 4;
	ret = bpf_dynptr_write(&meta, 0, &buf, 0, INVALID_FLAGS);
	if (ret != -EINVAL)
		return 1;

	err = 0;
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

SEC("tp_btf/kfree_skb")
int BPF_PROG(test_dynptr_skb_tp_btf, void *skb, void *location)
{
	__u8 write_data[2] = {1, 2};
	struct bpf_dynptr ptr;
	int ret;

	if (bpf_dynptr_from_skb(skb, 0, &ptr)) {
		err = 1;
		return 1;
	}

	/* since tp_btf skbs are read only, writes should fail */
	ret = bpf_dynptr_write(&ptr, 0, write_data, sizeof(write_data), 0);
	if (ret != -EINVAL) {
		err = 2;
		return 1;
	}

	return 1;
}

static inline int bpf_memcmp(const char *a, const char *b, u32 size)
{
	int i;

	bpf_for(i, 0, size) {
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	}
	return 0;
}

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_dynptr_copy(void *ctx)
{
	char data[] = "hello there, world!!";
	char buf[32] = {'\0'};
	__u32 sz = sizeof(data);
	struct bpf_dynptr src, dst;

	bpf_ringbuf_reserve_dynptr(&ringbuf, sz, 0, &src);
	bpf_ringbuf_reserve_dynptr(&ringbuf, sz, 0, &dst);

	/* Test basic case of copying contiguous memory backed dynptrs */
	err = bpf_dynptr_write(&src, 0, data, sz, 0);
	err = err ?: bpf_dynptr_copy(&dst, 0, &src, 0, sz);
	err = err ?: bpf_dynptr_read(buf, sz, &dst, 0, 0);
	err = err ?: bpf_memcmp(data, buf, sz);

	/* Test that offsets are handled correctly */
	err = err ?: bpf_dynptr_copy(&dst, 3, &src, 5, sz - 5);
	err = err ?: bpf_dynptr_read(buf, sz - 5, &dst, 3, 0);
	err = err ?: bpf_memcmp(data + 5, buf, sz - 5);

	bpf_ringbuf_discard_dynptr(&src, 0);
	bpf_ringbuf_discard_dynptr(&dst, 0);
	return 0;
}

SEC("xdp")
int test_dynptr_copy_xdp(struct xdp_md *xdp)
{
	struct bpf_dynptr ptr_buf, ptr_xdp;
	char data[] = "qwertyuiopasdfghjkl";
	char buf[32] = {'\0'};
	__u32 len = sizeof(data), xdp_data_size;
	int i, chunks = 200;

	/* ptr_xdp is backed by non-contiguous memory */
	bpf_dynptr_from_xdp(xdp, 0, &ptr_xdp);
	xdp_data_size = bpf_dynptr_size(&ptr_xdp);
	bpf_ringbuf_reserve_dynptr(&ringbuf, len * chunks, 0, &ptr_buf);

	/* Destination dynptr is backed by non-contiguous memory */
	bpf_for(i, 0, chunks) {
		err = bpf_dynptr_write(&ptr_buf, i * len, data, len, 0);
		if (err)
			goto out;
	}

	err = bpf_dynptr_copy(&ptr_xdp, 0, &ptr_buf, 0, len * chunks);
	if (err)
		goto out;

	bpf_for(i, 0, chunks) {
		__builtin_memset(buf, 0, sizeof(buf));
		err = bpf_dynptr_read(&buf, len, &ptr_xdp, i * len, 0);
		if (err)
			goto out;
		if (bpf_memcmp(data, buf, len) != 0)
			goto out;
	}

	/* Source dynptr is backed by non-contiguous memory */
	__builtin_memset(buf, 0, sizeof(buf));
	bpf_for(i, 0, chunks) {
		err = bpf_dynptr_write(&ptr_buf, i * len, buf, len, 0);
		if (err)
			goto out;
	}

	err = bpf_dynptr_copy(&ptr_buf, 0, &ptr_xdp, 0, len * chunks);
	if (err)
		goto out;

	bpf_for(i, 0, chunks) {
		__builtin_memset(buf, 0, sizeof(buf));
		err = bpf_dynptr_read(&buf, len, &ptr_buf, i * len, 0);
		if (err)
			goto out;
		if (bpf_memcmp(data, buf, len) != 0)
			goto out;
	}

	/* Both source and destination dynptrs are backed by non-contiguous memory */
	err = bpf_dynptr_copy(&ptr_xdp, 2, &ptr_xdp, len, len * (chunks - 1));
	if (err)
		goto out;

	bpf_for(i, 0, chunks - 1) {
		__builtin_memset(buf, 0, sizeof(buf));
		err = bpf_dynptr_read(&buf, len, &ptr_xdp, 2 + i * len, 0);
		if (err)
			goto out;
		if (bpf_memcmp(data, buf, len) != 0)
			goto out;
	}

	if (bpf_dynptr_copy(&ptr_xdp, xdp_data_size - 3000, &ptr_xdp, 0, len * chunks) != -E2BIG)
		err = 1;

out:
	bpf_ringbuf_discard_dynptr(&ptr_buf, 0);
	return XDP_DROP;
}

char memset_zero_data[] = "data to be zeroed";

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_dynptr_memset_zero(void *ctx)
{
	__u32 data_sz = sizeof(memset_zero_data);
	char zeroes[32] = {'\0'};
	struct bpf_dynptr ptr;

	err = bpf_dynptr_from_mem(memset_zero_data, data_sz, 0, &ptr);
	err = err ?: bpf_dynptr_memset(&ptr, 0, data_sz, 0);
	err = err ?: bpf_memcmp(zeroes, memset_zero_data, data_sz);

	return 0;
}

#define DYNPTR_MEMSET_VAL 42

char memset_notzero_data[] = "data to be overwritten";

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_dynptr_memset_notzero(void *ctx)
{
	u32 data_sz = sizeof(memset_notzero_data);
	struct bpf_dynptr ptr;
	char expected[32];

	__builtin_memset(expected, DYNPTR_MEMSET_VAL, data_sz);

	err = bpf_dynptr_from_mem(memset_notzero_data, data_sz, 0, &ptr);
	err = err ?: bpf_dynptr_memset(&ptr, 0, data_sz, DYNPTR_MEMSET_VAL);
	err = err ?: bpf_memcmp(expected, memset_notzero_data, data_sz);

	return 0;
}

char memset_zero_offset_data[] = "data to be zeroed partially";

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_dynptr_memset_zero_offset(void *ctx)
{
	char expected[] = "data to \0\0\0\0eroed partially";
	__u32 data_sz = sizeof(memset_zero_offset_data);
	struct bpf_dynptr ptr;

	err = bpf_dynptr_from_mem(memset_zero_offset_data, data_sz, 0, &ptr);
	err = err ?: bpf_dynptr_memset(&ptr, 8, 4, 0);
	err = err ?: bpf_memcmp(expected, memset_zero_offset_data, data_sz);

	return 0;
}

char memset_zero_adjusted_data[] = "data to be zeroed partially";

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_dynptr_memset_zero_adjusted(void *ctx)
{
	char expected[] = "data\0\0\0\0be zeroed partially";
	__u32 data_sz = sizeof(memset_zero_adjusted_data);
	struct bpf_dynptr ptr;

	err = bpf_dynptr_from_mem(memset_zero_adjusted_data, data_sz, 0, &ptr);
	err = err ?: bpf_dynptr_adjust(&ptr, 4, 8);
	err = err ?: bpf_dynptr_memset(&ptr, 0, bpf_dynptr_size(&ptr), 0);
	err = err ?: bpf_memcmp(expected, memset_zero_adjusted_data, data_sz);

	return 0;
}

char memset_overflow_data[] = "memset overflow data";

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_dynptr_memset_overflow(void *ctx)
{
	__u32 data_sz = sizeof(memset_overflow_data);
	struct bpf_dynptr ptr;
	int ret;

	err = bpf_dynptr_from_mem(memset_overflow_data, data_sz, 0, &ptr);
	ret = bpf_dynptr_memset(&ptr, 0, data_sz + 1, 0);
	if (ret != -E2BIG)
		err = 1;

	return 0;
}

SEC("?tp/syscalls/sys_enter_nanosleep")
int test_dynptr_memset_overflow_offset(void *ctx)
{
	__u32 data_sz = sizeof(memset_overflow_data);
	struct bpf_dynptr ptr;
	int ret;

	err = bpf_dynptr_from_mem(memset_overflow_data, data_sz, 0, &ptr);
	ret = bpf_dynptr_memset(&ptr, 1, data_sz, 0);
	if (ret != -E2BIG)
		err = 1;

	return 0;
}

SEC("?cgroup_skb/egress")
int test_dynptr_memset_readonly(struct __sk_buff *skb)
{
	struct bpf_dynptr ptr;
	int ret;

	err = bpf_dynptr_from_skb(skb, 0, &ptr);

	/* cgroup skbs are read only, memset should fail */
	ret = bpf_dynptr_memset(&ptr, 0, bpf_dynptr_size(&ptr), 0);
	if (ret != -EINVAL)
		err = 1;

	return 0;
}

#define min_t(type, x, y) ({		\
	type __x = (x);			\
	type __y = (y);			\
	__x < __y ? __x : __y; })

SEC("xdp")
int test_dynptr_memset_xdp_chunks(struct xdp_md *xdp)
{
	u32 data_sz, chunk_sz, offset = 0;
	const int max_chunks = 200;
	struct bpf_dynptr ptr_xdp;
	char expected_buf[32];
	char buf[32];
	int i;

	__builtin_memset(expected_buf, DYNPTR_MEMSET_VAL, sizeof(expected_buf));

	/* ptr_xdp is backed by non-contiguous memory */
	bpf_dynptr_from_xdp(xdp, 0, &ptr_xdp);
	data_sz = bpf_dynptr_size(&ptr_xdp);

	err = bpf_dynptr_memset(&ptr_xdp, 0, data_sz, DYNPTR_MEMSET_VAL);
	if (err) {
		/* bpf_dynptr_memset() eventually called bpf_xdp_pointer()
		 * where if data_sz is greater than 0xffff, -EFAULT will be
		 * returned. For 64K page size, data_sz is greater than
		 * 64K, so error is expected and let us zero out error and
		 * return success.
		 */
		if (data_sz >= PAGE_SIZE_64K)
			err = 0;
		goto out;
	}

	bpf_for(i, 0, max_chunks) {
		offset = i * sizeof(buf);
		if (offset >= data_sz)
			goto out;
		chunk_sz = min_t(u32, sizeof(buf), data_sz - offset);
		err = bpf_dynptr_read(&buf, chunk_sz, &ptr_xdp, offset, 0);
		if (err)
			goto out;
		err = bpf_memcmp(buf, expected_buf, sizeof(buf));
		if (err)
			goto out;
	}
out:
	return XDP_DROP;
}

void *user_ptr;
/* Contains the copy of the data pointed by user_ptr.
 * Size 384 to make it not fit into a single kernel chunk when copying
 * but less than the maximum bpf stack size (512).
 */
char expected_str[384];
__u32 test_len[7] = {0/* placeholder */, 0, 1, 2, 255, 256, 257};

typedef int (*bpf_read_dynptr_fn_t)(struct bpf_dynptr *dptr, u32 off,
				    u32 size, const void *unsafe_ptr);

/* Returns the offset just before the end of the maximum sized xdp fragment.
 * Any write larger than 32 bytes will be split between 2 fragments.
 */
__u32 xdp_near_frag_end_offset(void)
{
	const __u32 headroom = 256;
	const __u32 max_frag_size =  __PAGE_SIZE - headroom - sizeof(struct skb_shared_info);

	/* 32 bytes before the approximate end of the fragment */
	return max_frag_size - 32;
}

/* Use __always_inline on test_dynptr_probe[_str][_xdp]() and callbacks
 * of type bpf_read_dynptr_fn_t to prevent compiler from generating
 * indirect calls that make program fail to load with "unknown opcode" error.
 */
static __always_inline void test_dynptr_probe(void *ptr, bpf_read_dynptr_fn_t bpf_read_dynptr_fn)
{
	char buf[sizeof(expected_str)];
	struct bpf_dynptr ptr_buf;
	int i;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return;

	err = bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(buf), 0, &ptr_buf);

	bpf_for(i, 0, ARRAY_SIZE(test_len)) {
		__u32 len = test_len[i];

		err = err ?: bpf_read_dynptr_fn(&ptr_buf, 0, test_len[i], ptr);
		if (len > sizeof(buf))
			break;
		err = err ?: bpf_dynptr_read(&buf, len, &ptr_buf, 0, 0);

		if (err || bpf_memcmp(expected_str, buf, len))
			err = 1;

		/* Reset buffer and dynptr */
		__builtin_memset(buf, 0, sizeof(buf));
		err = err ?: bpf_dynptr_write(&ptr_buf, 0, buf, len, 0);
	}
	bpf_ringbuf_discard_dynptr(&ptr_buf, 0);
}

static __always_inline void test_dynptr_probe_str(void *ptr,
						  bpf_read_dynptr_fn_t bpf_read_dynptr_fn)
{
	char buf[sizeof(expected_str)];
	struct bpf_dynptr ptr_buf;
	__u32 cnt, i;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return;

	bpf_ringbuf_reserve_dynptr(&ringbuf, sizeof(buf), 0, &ptr_buf);

	bpf_for(i, 0, ARRAY_SIZE(test_len)) {
		__u32 len = test_len[i];

		cnt = bpf_read_dynptr_fn(&ptr_buf, 0, len, ptr);
		if (cnt != len)
			err = 1;

		if (len > sizeof(buf))
			continue;
		err = err ?: bpf_dynptr_read(&buf, len, &ptr_buf, 0, 0);
		if (!len)
			continue;
		if (err || bpf_memcmp(expected_str, buf, len - 1) || buf[len - 1] != '\0')
			err = 1;
	}
	bpf_ringbuf_discard_dynptr(&ptr_buf, 0);
}

static __always_inline void test_dynptr_probe_xdp(struct xdp_md *xdp, void *ptr,
						  bpf_read_dynptr_fn_t bpf_read_dynptr_fn)
{
	struct bpf_dynptr ptr_xdp;
	char buf[sizeof(expected_str)];
	__u32 off, i;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return;

	off = xdp_near_frag_end_offset();
	err = bpf_dynptr_from_xdp(xdp, 0, &ptr_xdp);

	bpf_for(i, 0, ARRAY_SIZE(test_len)) {
		__u32 len = test_len[i];

		err = err ?: bpf_read_dynptr_fn(&ptr_xdp, off, len, ptr);
		if (len > sizeof(buf))
			continue;
		err = err ?: bpf_dynptr_read(&buf, len, &ptr_xdp, off, 0);
		if (err || bpf_memcmp(expected_str, buf, len))
			err = 1;
		/* Reset buffer and dynptr */
		__builtin_memset(buf, 0, sizeof(buf));
		err = err ?: bpf_dynptr_write(&ptr_xdp, off, buf, len, 0);
	}
}

static __always_inline void test_dynptr_probe_str_xdp(struct xdp_md *xdp, void *ptr,
						      bpf_read_dynptr_fn_t bpf_read_dynptr_fn)
{
	struct bpf_dynptr ptr_xdp;
	char buf[sizeof(expected_str)];
	__u32 cnt, off, i;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return;

	off = xdp_near_frag_end_offset();
	err = bpf_dynptr_from_xdp(xdp, 0, &ptr_xdp);
	if (err)
		return;

	bpf_for(i, 0, ARRAY_SIZE(test_len)) {
		__u32 len = test_len[i];

		cnt = bpf_read_dynptr_fn(&ptr_xdp, off, len, ptr);
		if (cnt != len)
			err = 1;

		if (len > sizeof(buf))
			continue;
		err = err ?: bpf_dynptr_read(&buf, len, &ptr_xdp, off, 0);

		if (!len)
			continue;
		if (err || bpf_memcmp(expected_str, buf, len - 1) || buf[len - 1] != '\0')
			err = 1;

		__builtin_memset(buf, 0, sizeof(buf));
		err = err ?: bpf_dynptr_write(&ptr_xdp, off, buf, len, 0);
	}
}

SEC("xdp")
int test_probe_read_user_dynptr(struct xdp_md *xdp)
{
	test_dynptr_probe(user_ptr, bpf_probe_read_user_dynptr);
	if (!err)
		test_dynptr_probe_xdp(xdp, user_ptr, bpf_probe_read_user_dynptr);
	return XDP_PASS;
}

SEC("xdp")
int test_probe_read_kernel_dynptr(struct xdp_md *xdp)
{
	test_dynptr_probe(expected_str, bpf_probe_read_kernel_dynptr);
	if (!err)
		test_dynptr_probe_xdp(xdp, expected_str, bpf_probe_read_kernel_dynptr);
	return XDP_PASS;
}

SEC("xdp")
int test_probe_read_user_str_dynptr(struct xdp_md *xdp)
{
	test_dynptr_probe_str(user_ptr, bpf_probe_read_user_str_dynptr);
	if (!err)
		test_dynptr_probe_str_xdp(xdp, user_ptr, bpf_probe_read_user_str_dynptr);
	return XDP_PASS;
}

SEC("xdp")
int test_probe_read_kernel_str_dynptr(struct xdp_md *xdp)
{
	test_dynptr_probe_str(expected_str, bpf_probe_read_kernel_str_dynptr);
	if (!err)
		test_dynptr_probe_str_xdp(xdp, expected_str, bpf_probe_read_kernel_str_dynptr);
	return XDP_PASS;
}

SEC("fentry.s/" SYS_PREFIX "sys_nanosleep")
int test_copy_from_user_dynptr(void *ctx)
{
	test_dynptr_probe(user_ptr, bpf_copy_from_user_dynptr);
	return 0;
}

SEC("fentry.s/" SYS_PREFIX "sys_nanosleep")
int test_copy_from_user_str_dynptr(void *ctx)
{
	test_dynptr_probe_str(user_ptr, bpf_copy_from_user_str_dynptr);
	return 0;
}

static int bpf_copy_data_from_user_task(struct bpf_dynptr *dptr, u32 off,
					u32 size, const void *unsafe_ptr)
{
	struct task_struct *task = bpf_get_current_task_btf();

	return bpf_copy_from_user_task_dynptr(dptr, off, size, unsafe_ptr, task);
}

static int bpf_copy_data_from_user_task_str(struct bpf_dynptr *dptr, u32 off,
					    u32 size, const void *unsafe_ptr)
{
	struct task_struct *task = bpf_get_current_task_btf();

	return bpf_copy_from_user_task_str_dynptr(dptr, off, size, unsafe_ptr, task);
}

SEC("fentry.s/" SYS_PREFIX "sys_nanosleep")
int test_copy_from_user_task_dynptr(void *ctx)
{
	test_dynptr_probe(user_ptr, bpf_copy_data_from_user_task);
	return 0;
}

SEC("fentry.s/" SYS_PREFIX "sys_nanosleep")
int test_copy_from_user_task_str_dynptr(void *ctx)
{
	test_dynptr_probe_str(user_ptr, bpf_copy_data_from_user_task_str);
	return 0;
}
