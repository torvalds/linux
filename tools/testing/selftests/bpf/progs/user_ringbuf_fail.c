// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct sample {
	int pid;
	int seq;
	long value;
	char comm[16];
};

struct {
	__uint(type, BPF_MAP_TYPE_USER_RINGBUF);
} user_ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 2);
} ringbuf SEC(".maps");

static int map_value;

static long
bad_access1(struct bpf_dynptr *dynptr, void *context)
{
	const struct sample *sample;

	sample = bpf_dynptr_data(dynptr - 1, 0, sizeof(*sample));
	bpf_printk("Was able to pass bad pointer %lx\n", (__u64)dynptr - 1);

	return 0;
}

/* A callback that accesses a dynptr in a bpf_user_ringbuf_drain callback should
 * not be able to read before the pointer.
 */
SEC("?raw_tp/")
int user_ringbuf_callback_bad_access1(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, bad_access1, NULL, 0);

	return 0;
}

static long
bad_access2(struct bpf_dynptr *dynptr, void *context)
{
	const struct sample *sample;

	sample = bpf_dynptr_data(dynptr + 1, 0, sizeof(*sample));
	bpf_printk("Was able to pass bad pointer %lx\n", (__u64)dynptr + 1);

	return 0;
}

/* A callback that accesses a dynptr in a bpf_user_ringbuf_drain callback should
 * not be able to read past the end of the pointer.
 */
SEC("?raw_tp/")
int user_ringbuf_callback_bad_access2(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, bad_access2, NULL, 0);

	return 0;
}

static long
write_forbidden(struct bpf_dynptr *dynptr, void *context)
{
	*((long *)dynptr) = 0;

	return 0;
}

/* A callback that accesses a dynptr in a bpf_user_ringbuf_drain callback should
 * not be able to write to that pointer.
 */
SEC("?raw_tp/")
int user_ringbuf_callback_write_forbidden(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, write_forbidden, NULL, 0);

	return 0;
}

static long
null_context_write(struct bpf_dynptr *dynptr, void *context)
{
	*((__u64 *)context) = 0;

	return 0;
}

/* A callback that accesses a dynptr in a bpf_user_ringbuf_drain callback should
 * not be able to write to that pointer.
 */
SEC("?raw_tp/")
int user_ringbuf_callback_null_context_write(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, null_context_write, NULL, 0);

	return 0;
}

static long
null_context_read(struct bpf_dynptr *dynptr, void *context)
{
	__u64 id = *((__u64 *)context);

	bpf_printk("Read id %lu\n", id);

	return 0;
}

/* A callback that accesses a dynptr in a bpf_user_ringbuf_drain callback should
 * not be able to write to that pointer.
 */
SEC("?raw_tp/")
int user_ringbuf_callback_null_context_read(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, null_context_read, NULL, 0);

	return 0;
}

static long
try_discard_dynptr(struct bpf_dynptr *dynptr, void *context)
{
	bpf_ringbuf_discard_dynptr(dynptr, 0);

	return 0;
}

/* A callback that accesses a dynptr in a bpf_user_ringbuf_drain callback should
 * not be able to read past the end of the pointer.
 */
SEC("?raw_tp/")
int user_ringbuf_callback_discard_dynptr(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, try_discard_dynptr, NULL, 0);

	return 0;
}

static long
try_submit_dynptr(struct bpf_dynptr *dynptr, void *context)
{
	bpf_ringbuf_submit_dynptr(dynptr, 0);

	return 0;
}

/* A callback that accesses a dynptr in a bpf_user_ringbuf_drain callback should
 * not be able to read past the end of the pointer.
 */
SEC("?raw_tp/")
int user_ringbuf_callback_submit_dynptr(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, try_submit_dynptr, NULL, 0);

	return 0;
}

static long
invalid_drain_callback_return(struct bpf_dynptr *dynptr, void *context)
{
	return 2;
}

/* A callback that accesses a dynptr in a bpf_user_ringbuf_drain callback should
 * not be able to write to that pointer.
 */
SEC("?raw_tp/")
int user_ringbuf_callback_invalid_return(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, invalid_drain_callback_return, NULL, 0);

	return 0;
}

static long
try_reinit_dynptr_mem(struct bpf_dynptr *dynptr, void *context)
{
	bpf_dynptr_from_mem(&map_value, 4, 0, dynptr);
	return 0;
}

static long
try_reinit_dynptr_ringbuf(struct bpf_dynptr *dynptr, void *context)
{
	bpf_ringbuf_reserve_dynptr(&ringbuf, 8, 0, dynptr);
	return 0;
}

SEC("?raw_tp/")
int user_ringbuf_callback_reinit_dynptr_mem(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, try_reinit_dynptr_mem, NULL, 0);
	return 0;
}

SEC("?raw_tp/")
int user_ringbuf_callback_reinit_dynptr_ringbuf(void *ctx)
{
	bpf_user_ringbuf_drain(&user_ringbuf, try_reinit_dynptr_ringbuf, NULL, 0);
	return 0;
}
