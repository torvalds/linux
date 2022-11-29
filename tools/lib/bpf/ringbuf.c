// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/*
 * Ring buffer operations.
 *
 * Copyright (C) 2020 Facebook, Inc.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <linux/err.h>
#include <linux/bpf.h>
#include <asm/barrier.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <time.h>

#include "libbpf.h"
#include "libbpf_internal.h"
#include "bpf.h"

struct ring {
	ring_buffer_sample_fn sample_cb;
	void *ctx;
	void *data;
	unsigned long *consumer_pos;
	unsigned long *producer_pos;
	unsigned long mask;
	int map_fd;
};

struct ring_buffer {
	struct epoll_event *events;
	struct ring *rings;
	size_t page_size;
	int epoll_fd;
	int ring_cnt;
};

struct user_ring_buffer {
	struct epoll_event event;
	unsigned long *consumer_pos;
	unsigned long *producer_pos;
	void *data;
	unsigned long mask;
	size_t page_size;
	int map_fd;
	int epoll_fd;
};

/* 8-byte ring buffer header structure */
struct ringbuf_hdr {
	__u32 len;
	__u32 pad;
};

static void ringbuf_unmap_ring(struct ring_buffer *rb, struct ring *r)
{
	if (r->consumer_pos) {
		munmap(r->consumer_pos, rb->page_size);
		r->consumer_pos = NULL;
	}
	if (r->producer_pos) {
		munmap(r->producer_pos, rb->page_size + 2 * (r->mask + 1));
		r->producer_pos = NULL;
	}
}

/* Add extra RINGBUF maps to this ring buffer manager */
int ring_buffer__add(struct ring_buffer *rb, int map_fd,
		     ring_buffer_sample_fn sample_cb, void *ctx)
{
	struct bpf_map_info info;
	__u32 len = sizeof(info);
	struct epoll_event *e;
	struct ring *r;
	void *tmp;
	int err;

	memset(&info, 0, sizeof(info));

	err = bpf_obj_get_info_by_fd(map_fd, &info, &len);
	if (err) {
		err = -errno;
		pr_warn("ringbuf: failed to get map info for fd=%d: %d\n",
			map_fd, err);
		return libbpf_err(err);
	}

	if (info.type != BPF_MAP_TYPE_RINGBUF) {
		pr_warn("ringbuf: map fd=%d is not BPF_MAP_TYPE_RINGBUF\n",
			map_fd);
		return libbpf_err(-EINVAL);
	}

	tmp = libbpf_reallocarray(rb->rings, rb->ring_cnt + 1, sizeof(*rb->rings));
	if (!tmp)
		return libbpf_err(-ENOMEM);
	rb->rings = tmp;

	tmp = libbpf_reallocarray(rb->events, rb->ring_cnt + 1, sizeof(*rb->events));
	if (!tmp)
		return libbpf_err(-ENOMEM);
	rb->events = tmp;

	r = &rb->rings[rb->ring_cnt];
	memset(r, 0, sizeof(*r));

	r->map_fd = map_fd;
	r->sample_cb = sample_cb;
	r->ctx = ctx;
	r->mask = info.max_entries - 1;

	/* Map writable consumer page */
	tmp = mmap(NULL, rb->page_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   map_fd, 0);
	if (tmp == MAP_FAILED) {
		err = -errno;
		pr_warn("ringbuf: failed to mmap consumer page for map fd=%d: %d\n",
			map_fd, err);
		return libbpf_err(err);
	}
	r->consumer_pos = tmp;

	/* Map read-only producer page and data pages. We map twice as big
	 * data size to allow simple reading of samples that wrap around the
	 * end of a ring buffer. See kernel implementation for details.
	 */
	tmp = mmap(NULL, rb->page_size + 2 * info.max_entries, PROT_READ,
		   MAP_SHARED, map_fd, rb->page_size);
	if (tmp == MAP_FAILED) {
		err = -errno;
		ringbuf_unmap_ring(rb, r);
		pr_warn("ringbuf: failed to mmap data pages for map fd=%d: %d\n",
			map_fd, err);
		return libbpf_err(err);
	}
	r->producer_pos = tmp;
	r->data = tmp + rb->page_size;

	e = &rb->events[rb->ring_cnt];
	memset(e, 0, sizeof(*e));

	e->events = EPOLLIN;
	e->data.fd = rb->ring_cnt;
	if (epoll_ctl(rb->epoll_fd, EPOLL_CTL_ADD, map_fd, e) < 0) {
		err = -errno;
		ringbuf_unmap_ring(rb, r);
		pr_warn("ringbuf: failed to epoll add map fd=%d: %d\n",
			map_fd, err);
		return libbpf_err(err);
	}

	rb->ring_cnt++;
	return 0;
}

void ring_buffer__free(struct ring_buffer *rb)
{
	int i;

	if (!rb)
		return;

	for (i = 0; i < rb->ring_cnt; ++i)
		ringbuf_unmap_ring(rb, &rb->rings[i]);
	if (rb->epoll_fd >= 0)
		close(rb->epoll_fd);

	free(rb->events);
	free(rb->rings);
	free(rb);
}

struct ring_buffer *
ring_buffer__new(int map_fd, ring_buffer_sample_fn sample_cb, void *ctx,
		 const struct ring_buffer_opts *opts)
{
	struct ring_buffer *rb;
	int err;

	if (!OPTS_VALID(opts, ring_buffer_opts))
		return errno = EINVAL, NULL;

	rb = calloc(1, sizeof(*rb));
	if (!rb)
		return errno = ENOMEM, NULL;

	rb->page_size = getpagesize();

	rb->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (rb->epoll_fd < 0) {
		err = -errno;
		pr_warn("ringbuf: failed to create epoll instance: %d\n", err);
		goto err_out;
	}

	err = ring_buffer__add(rb, map_fd, sample_cb, ctx);
	if (err)
		goto err_out;

	return rb;

err_out:
	ring_buffer__free(rb);
	return errno = -err, NULL;
}

static inline int roundup_len(__u32 len)
{
	/* clear out top 2 bits (discard and busy, if set) */
	len <<= 2;
	len >>= 2;
	/* add length prefix */
	len += BPF_RINGBUF_HDR_SZ;
	/* round up to 8 byte alignment */
	return (len + 7) / 8 * 8;
}

static int64_t ringbuf_process_ring(struct ring *r)
{
	int *len_ptr, len, err;
	/* 64-bit to avoid overflow in case of extreme application behavior */
	int64_t cnt = 0;
	unsigned long cons_pos, prod_pos;
	bool got_new_data;
	void *sample;

	cons_pos = smp_load_acquire(r->consumer_pos);
	do {
		got_new_data = false;
		prod_pos = smp_load_acquire(r->producer_pos);
		while (cons_pos < prod_pos) {
			len_ptr = r->data + (cons_pos & r->mask);
			len = smp_load_acquire(len_ptr);

			/* sample not committed yet, bail out for now */
			if (len & BPF_RINGBUF_BUSY_BIT)
				goto done;

			got_new_data = true;
			cons_pos += roundup_len(len);

			if ((len & BPF_RINGBUF_DISCARD_BIT) == 0) {
				sample = (void *)len_ptr + BPF_RINGBUF_HDR_SZ;
				err = r->sample_cb(r->ctx, sample, len);
				if (err < 0) {
					/* update consumer pos and bail out */
					smp_store_release(r->consumer_pos,
							  cons_pos);
					return err;
				}
				cnt++;
			}

			smp_store_release(r->consumer_pos, cons_pos);
		}
	} while (got_new_data);
done:
	return cnt;
}

/* Consume available ring buffer(s) data without event polling.
 * Returns number of records consumed across all registered ring buffers (or
 * INT_MAX, whichever is less), or negative number if any of the callbacks
 * return error.
 */
int ring_buffer__consume(struct ring_buffer *rb)
{
	int64_t err, res = 0;
	int i;

	for (i = 0; i < rb->ring_cnt; i++) {
		struct ring *ring = &rb->rings[i];

		err = ringbuf_process_ring(ring);
		if (err < 0)
			return libbpf_err(err);
		res += err;
	}
	if (res > INT_MAX)
		return INT_MAX;
	return res;
}

/* Poll for available data and consume records, if any are available.
 * Returns number of records consumed (or INT_MAX, whichever is less), or
 * negative number, if any of the registered callbacks returned error.
 */
int ring_buffer__poll(struct ring_buffer *rb, int timeout_ms)
{
	int i, cnt;
	int64_t err, res = 0;

	cnt = epoll_wait(rb->epoll_fd, rb->events, rb->ring_cnt, timeout_ms);
	if (cnt < 0)
		return libbpf_err(-errno);

	for (i = 0; i < cnt; i++) {
		__u32 ring_id = rb->events[i].data.fd;
		struct ring *ring = &rb->rings[ring_id];

		err = ringbuf_process_ring(ring);
		if (err < 0)
			return libbpf_err(err);
		res += err;
	}
	if (res > INT_MAX)
		return INT_MAX;
	return res;
}

/* Get an fd that can be used to sleep until data is available in the ring(s) */
int ring_buffer__epoll_fd(const struct ring_buffer *rb)
{
	return rb->epoll_fd;
}

static void user_ringbuf_unmap_ring(struct user_ring_buffer *rb)
{
	if (rb->consumer_pos) {
		munmap(rb->consumer_pos, rb->page_size);
		rb->consumer_pos = NULL;
	}
	if (rb->producer_pos) {
		munmap(rb->producer_pos, rb->page_size + 2 * (rb->mask + 1));
		rb->producer_pos = NULL;
	}
}

void user_ring_buffer__free(struct user_ring_buffer *rb)
{
	if (!rb)
		return;

	user_ringbuf_unmap_ring(rb);

	if (rb->epoll_fd >= 0)
		close(rb->epoll_fd);

	free(rb);
}

static int user_ringbuf_map(struct user_ring_buffer *rb, int map_fd)
{
	struct bpf_map_info info;
	__u32 len = sizeof(info);
	void *tmp;
	struct epoll_event *rb_epoll;
	int err;

	memset(&info, 0, sizeof(info));

	err = bpf_obj_get_info_by_fd(map_fd, &info, &len);
	if (err) {
		err = -errno;
		pr_warn("user ringbuf: failed to get map info for fd=%d: %d\n", map_fd, err);
		return err;
	}

	if (info.type != BPF_MAP_TYPE_USER_RINGBUF) {
		pr_warn("user ringbuf: map fd=%d is not BPF_MAP_TYPE_USER_RINGBUF\n", map_fd);
		return -EINVAL;
	}

	rb->map_fd = map_fd;
	rb->mask = info.max_entries - 1;

	/* Map read-only consumer page */
	tmp = mmap(NULL, rb->page_size, PROT_READ, MAP_SHARED, map_fd, 0);
	if (tmp == MAP_FAILED) {
		err = -errno;
		pr_warn("user ringbuf: failed to mmap consumer page for map fd=%d: %d\n",
			map_fd, err);
		return err;
	}
	rb->consumer_pos = tmp;

	/* Map read-write the producer page and data pages. We map the data
	 * region as twice the total size of the ring buffer to allow the
	 * simple reading and writing of samples that wrap around the end of
	 * the buffer.  See the kernel implementation for details.
	 */
	tmp = mmap(NULL, rb->page_size + 2 * info.max_entries,
		   PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, rb->page_size);
	if (tmp == MAP_FAILED) {
		err = -errno;
		pr_warn("user ringbuf: failed to mmap data pages for map fd=%d: %d\n",
			map_fd, err);
		return err;
	}

	rb->producer_pos = tmp;
	rb->data = tmp + rb->page_size;

	rb_epoll = &rb->event;
	rb_epoll->events = EPOLLOUT;
	if (epoll_ctl(rb->epoll_fd, EPOLL_CTL_ADD, map_fd, rb_epoll) < 0) {
		err = -errno;
		pr_warn("user ringbuf: failed to epoll add map fd=%d: %d\n", map_fd, err);
		return err;
	}

	return 0;
}

struct user_ring_buffer *
user_ring_buffer__new(int map_fd, const struct user_ring_buffer_opts *opts)
{
	struct user_ring_buffer *rb;
	int err;

	if (!OPTS_VALID(opts, user_ring_buffer_opts))
		return errno = EINVAL, NULL;

	rb = calloc(1, sizeof(*rb));
	if (!rb)
		return errno = ENOMEM, NULL;

	rb->page_size = getpagesize();

	rb->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (rb->epoll_fd < 0) {
		err = -errno;
		pr_warn("user ringbuf: failed to create epoll instance: %d\n", err);
		goto err_out;
	}

	err = user_ringbuf_map(rb, map_fd);
	if (err)
		goto err_out;

	return rb;

err_out:
	user_ring_buffer__free(rb);
	return errno = -err, NULL;
}

static void user_ringbuf_commit(struct user_ring_buffer *rb, void *sample, bool discard)
{
	__u32 new_len;
	struct ringbuf_hdr *hdr;
	uintptr_t hdr_offset;

	hdr_offset = rb->mask + 1 + (sample - rb->data) - BPF_RINGBUF_HDR_SZ;
	hdr = rb->data + (hdr_offset & rb->mask);

	new_len = hdr->len & ~BPF_RINGBUF_BUSY_BIT;
	if (discard)
		new_len |= BPF_RINGBUF_DISCARD_BIT;

	/* Synchronizes with smp_load_acquire() in __bpf_user_ringbuf_peek() in
	 * the kernel.
	 */
	__atomic_exchange_n(&hdr->len, new_len, __ATOMIC_ACQ_REL);
}

void user_ring_buffer__discard(struct user_ring_buffer *rb, void *sample)
{
	user_ringbuf_commit(rb, sample, true);
}

void user_ring_buffer__submit(struct user_ring_buffer *rb, void *sample)
{
	user_ringbuf_commit(rb, sample, false);
}

void *user_ring_buffer__reserve(struct user_ring_buffer *rb, __u32 size)
{
	__u32 avail_size, total_size, max_size;
	/* 64-bit to avoid overflow in case of extreme application behavior */
	__u64 cons_pos, prod_pos;
	struct ringbuf_hdr *hdr;

	/* Synchronizes with smp_store_release() in __bpf_user_ringbuf_peek() in
	 * the kernel.
	 */
	cons_pos = smp_load_acquire(rb->consumer_pos);
	/* Synchronizes with smp_store_release() in user_ringbuf_commit() */
	prod_pos = smp_load_acquire(rb->producer_pos);

	max_size = rb->mask + 1;
	avail_size = max_size - (prod_pos - cons_pos);
	/* Round up total size to a multiple of 8. */
	total_size = (size + BPF_RINGBUF_HDR_SZ + 7) / 8 * 8;

	if (total_size > max_size)
		return errno = E2BIG, NULL;

	if (avail_size < total_size)
		return errno = ENOSPC, NULL;

	hdr = rb->data + (prod_pos & rb->mask);
	hdr->len = size | BPF_RINGBUF_BUSY_BIT;
	hdr->pad = 0;

	/* Synchronizes with smp_load_acquire() in __bpf_user_ringbuf_peek() in
	 * the kernel.
	 */
	smp_store_release(rb->producer_pos, prod_pos + total_size);

	return (void *)rb->data + ((prod_pos + BPF_RINGBUF_HDR_SZ) & rb->mask);
}

static __u64 ns_elapsed_timespec(const struct timespec *start, const struct timespec *end)
{
	__u64 start_ns, end_ns, ns_per_s = 1000000000;

	start_ns = (__u64)start->tv_sec * ns_per_s + start->tv_nsec;
	end_ns = (__u64)end->tv_sec * ns_per_s + end->tv_nsec;

	return end_ns - start_ns;
}

void *user_ring_buffer__reserve_blocking(struct user_ring_buffer *rb, __u32 size, int timeout_ms)
{
	void *sample;
	int err, ms_remaining = timeout_ms;
	struct timespec start;

	if (timeout_ms < 0 && timeout_ms != -1)
		return errno = EINVAL, NULL;

	if (timeout_ms != -1) {
		err = clock_gettime(CLOCK_MONOTONIC, &start);
		if (err)
			return NULL;
	}

	do {
		int cnt, ms_elapsed;
		struct timespec curr;
		__u64 ns_per_ms = 1000000;

		sample = user_ring_buffer__reserve(rb, size);
		if (sample)
			return sample;
		else if (errno != ENOSPC)
			return NULL;

		/* The kernel guarantees at least one event notification
		 * delivery whenever at least one sample is drained from the
		 * ring buffer in an invocation to bpf_ringbuf_drain(). Other
		 * additional events may be delivered at any time, but only one
		 * event is guaranteed per bpf_ringbuf_drain() invocation,
		 * provided that a sample is drained, and the BPF program did
		 * not pass BPF_RB_NO_WAKEUP to bpf_ringbuf_drain(). If
		 * BPF_RB_FORCE_WAKEUP is passed to bpf_ringbuf_drain(), a
		 * wakeup event will be delivered even if no samples are
		 * drained.
		 */
		cnt = epoll_wait(rb->epoll_fd, &rb->event, 1, ms_remaining);
		if (cnt < 0)
			return NULL;

		if (timeout_ms == -1)
			continue;

		err = clock_gettime(CLOCK_MONOTONIC, &curr);
		if (err)
			return NULL;

		ms_elapsed = ns_elapsed_timespec(&start, &curr) / ns_per_ms;
		ms_remaining = timeout_ms - ms_elapsed;
	} while (ms_remaining > 0);

	/* Try one more time to reserve a sample after the specified timeout has elapsed. */
	return user_ring_buffer__reserve(rb, size);
}
