// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <test_progs.h>
#include <sys/epoll.h>
#include "test_ringbuf_multi.skel.h"

static int duration = 0;

struct sample {
	int pid;
	int seq;
	long value;
	char comm[16];
};

static int process_sample(void *ctx, void *data, size_t len)
{
	int ring = (unsigned long)ctx;
	struct sample *s = data;

	switch (s->seq) {
	case 0:
		CHECK(ring != 1, "sample1_ring", "exp %d, got %d\n", 1, ring);
		CHECK(s->value != 333, "sample1_value", "exp %ld, got %ld\n",
		      333L, s->value);
		break;
	case 1:
		CHECK(ring != 2, "sample2_ring", "exp %d, got %d\n", 2, ring);
		CHECK(s->value != 777, "sample2_value", "exp %ld, got %ld\n",
		      777L, s->value);
		break;
	default:
		CHECK(true, "extra_sample", "unexpected sample seq %d, val %ld\n",
		      s->seq, s->value);
		return -1;
	}

	return 0;
}

void test_ringbuf_multi(void)
{
	struct test_ringbuf_multi *skel;
	struct ring_buffer *ringbuf = NULL;
	int err;
	int page_size = getpagesize();
	int proto_fd = -1;

	skel = test_ringbuf_multi__open();
	if (CHECK(!skel, "skel_open", "skeleton open failed\n"))
		return;

	err = bpf_map__set_max_entries(skel->maps.ringbuf1, page_size);
	if (CHECK(err != 0, "bpf_map__set_max_entries", "bpf_map__set_max_entries failed\n"))
		goto cleanup;

	err = bpf_map__set_max_entries(skel->maps.ringbuf2, page_size);
	if (CHECK(err != 0, "bpf_map__set_max_entries", "bpf_map__set_max_entries failed\n"))
		goto cleanup;

	err = bpf_map__set_max_entries(bpf_map__inner_map(skel->maps.ringbuf_arr), page_size);
	if (CHECK(err != 0, "bpf_map__set_max_entries", "bpf_map__set_max_entries failed\n"))
		goto cleanup;

	proto_fd = bpf_create_map(BPF_MAP_TYPE_RINGBUF, 0, 0, page_size, 0);
	if (CHECK(proto_fd < 0, "bpf_create_map", "bpf_create_map failed\n"))
		goto cleanup;

	err = bpf_map__set_inner_map_fd(skel->maps.ringbuf_hash, proto_fd);
	if (CHECK(err != 0, "bpf_map__set_inner_map_fd", "bpf_map__set_inner_map_fd failed\n"))
		goto cleanup;

	err = test_ringbuf_multi__load(skel);
	if (CHECK(err != 0, "skel_load", "skeleton load failed\n"))
		goto cleanup;

	close(proto_fd);
	proto_fd = -1;

	/* only trigger BPF program for current process */
	skel->bss->pid = getpid();

	ringbuf = ring_buffer__new(bpf_map__fd(skel->maps.ringbuf1),
				   process_sample, (void *)(long)1, NULL);
	if (CHECK(!ringbuf, "ringbuf_create", "failed to create ringbuf\n"))
		goto cleanup;

	err = ring_buffer__add(ringbuf, bpf_map__fd(skel->maps.ringbuf2),
			      process_sample, (void *)(long)2);
	if (CHECK(err, "ringbuf_add", "failed to add another ring\n"))
		goto cleanup;

	err = test_ringbuf_multi__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attachment failed: %d\n", err))
		goto cleanup;

	/* trigger few samples, some will be skipped */
	skel->bss->target_ring = 0;
	skel->bss->value = 333;
	syscall(__NR_getpgid);

	/* skipped, no ringbuf in slot 1 */
	skel->bss->target_ring = 1;
	skel->bss->value = 555;
	syscall(__NR_getpgid);

	skel->bss->target_ring = 2;
	skel->bss->value = 777;
	syscall(__NR_getpgid);

	/* poll for samples, should get 2 ringbufs back */
	err = ring_buffer__poll(ringbuf, -1);
	if (CHECK(err != 2, "poll_res", "expected 2 records, got %d\n", err))
		goto cleanup;

	/* expect extra polling to return nothing */
	err = ring_buffer__poll(ringbuf, 0);
	if (CHECK(err < 0, "extra_samples", "poll result: %d\n", err))
		goto cleanup;

	CHECK(skel->bss->dropped != 0, "err_dropped", "exp %ld, got %ld\n",
	      0L, skel->bss->dropped);
	CHECK(skel->bss->skipped != 1, "err_skipped", "exp %ld, got %ld\n",
	      1L, skel->bss->skipped);
	CHECK(skel->bss->total != 2, "err_total", "exp %ld, got %ld\n",
	      2L, skel->bss->total);

cleanup:
	if (proto_fd >= 0)
		close(proto_fd);
	ring_buffer__free(ringbuf);
	test_ringbuf_multi__destroy(skel);
}
