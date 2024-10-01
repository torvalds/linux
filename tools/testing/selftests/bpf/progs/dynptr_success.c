// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Facebook */

#include <string.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
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
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} array_map SEC(".maps");

SEC("tp/syscalls/sys_enter_nanosleep")
int test_read_write(void *ctx)
{
	char write_data[64] = "hello there, world!!";
	char read_data[64] = {}, buf[64] = {};
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

SEC("tp/syscalls/sys_enter_nanosleep")
int test_data_slice(void *ctx)
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

SEC("tp/syscalls/sys_enter_nanosleep")
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
