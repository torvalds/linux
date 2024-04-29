// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <linux/compiler.h>
#include <asm/barrier.h>
#include <test_progs.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <linux/perf_event.h>
#include <linux/ring_buffer.h>
#include "test_ringbuf.lskel.h"
#include "test_ringbuf_n.lskel.h"
#include "test_ringbuf_map_key.lskel.h"

#define EDONE 7777

static int duration = 0;

struct sample {
	int pid;
	int seq;
	long value;
	char comm[16];
};

static int sample_cnt;

static void atomic_inc(int *cnt)
{
	__atomic_add_fetch(cnt, 1, __ATOMIC_SEQ_CST);
}

static int atomic_xchg(int *cnt, int val)
{
	return __atomic_exchange_n(cnt, val, __ATOMIC_SEQ_CST);
}

static int process_sample(void *ctx, void *data, size_t len)
{
	struct sample *s = data;

	atomic_inc(&sample_cnt);

	switch (s->seq) {
	case 0:
		CHECK(s->value != 333, "sample1_value", "exp %ld, got %ld\n",
		      333L, s->value);
		return 0;
	case 1:
		CHECK(s->value != 777, "sample2_value", "exp %ld, got %ld\n",
		      777L, s->value);
		return -EDONE;
	default:
		/* we don't care about the rest */
		return 0;
	}
}

static struct test_ringbuf_map_key_lskel *skel_map_key;
static struct test_ringbuf_lskel *skel;
static struct ring_buffer *ringbuf;

static void trigger_samples()
{
	skel->bss->dropped = 0;
	skel->bss->total = 0;
	skel->bss->discarded = 0;

	/* trigger exactly two samples */
	skel->bss->value = 333;
	syscall(__NR_getpgid);
	skel->bss->value = 777;
	syscall(__NR_getpgid);
}

static void *poll_thread(void *input)
{
	long timeout = (long)input;

	return (void *)(long)ring_buffer__poll(ringbuf, timeout);
}

static void ringbuf_subtest(void)
{
	const size_t rec_sz = BPF_RINGBUF_HDR_SZ + sizeof(struct sample);
	pthread_t thread;
	long bg_ret = -1;
	int err, cnt, rb_fd;
	int page_size = getpagesize();
	void *mmap_ptr, *tmp_ptr;
	struct ring *ring;
	int map_fd;
	unsigned long avail_data, ring_size, cons_pos, prod_pos;

	skel = test_ringbuf_lskel__open();
	if (CHECK(!skel, "skel_open", "skeleton open failed\n"))
		return;

	skel->maps.ringbuf.max_entries = page_size;

	err = test_ringbuf_lskel__load(skel);
	if (CHECK(err != 0, "skel_load", "skeleton load failed\n"))
		goto cleanup;

	rb_fd = skel->maps.ringbuf.map_fd;
	/* good read/write cons_pos */
	mmap_ptr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, rb_fd, 0);
	ASSERT_OK_PTR(mmap_ptr, "rw_cons_pos");
	tmp_ptr = mremap(mmap_ptr, page_size, 2 * page_size, MREMAP_MAYMOVE);
	if (!ASSERT_ERR_PTR(tmp_ptr, "rw_extend"))
		goto cleanup;
	ASSERT_ERR(mprotect(mmap_ptr, page_size, PROT_EXEC), "exec_cons_pos_protect");
	ASSERT_OK(munmap(mmap_ptr, page_size), "unmap_rw");

	/* bad writeable prod_pos */
	mmap_ptr = mmap(NULL, page_size, PROT_WRITE, MAP_SHARED, rb_fd, page_size);
	err = -errno;
	ASSERT_ERR_PTR(mmap_ptr, "wr_prod_pos");
	ASSERT_EQ(err, -EPERM, "wr_prod_pos_err");

	/* bad writeable data pages */
	mmap_ptr = mmap(NULL, page_size, PROT_WRITE, MAP_SHARED, rb_fd, 2 * page_size);
	err = -errno;
	ASSERT_ERR_PTR(mmap_ptr, "wr_data_page_one");
	ASSERT_EQ(err, -EPERM, "wr_data_page_one_err");
	mmap_ptr = mmap(NULL, page_size, PROT_WRITE, MAP_SHARED, rb_fd, 3 * page_size);
	ASSERT_ERR_PTR(mmap_ptr, "wr_data_page_two");
	mmap_ptr = mmap(NULL, 2 * page_size, PROT_WRITE, MAP_SHARED, rb_fd, 2 * page_size);
	ASSERT_ERR_PTR(mmap_ptr, "wr_data_page_all");

	/* good read-only pages */
	mmap_ptr = mmap(NULL, 4 * page_size, PROT_READ, MAP_SHARED, rb_fd, 0);
	if (!ASSERT_OK_PTR(mmap_ptr, "ro_prod_pos"))
		goto cleanup;

	ASSERT_ERR(mprotect(mmap_ptr, 4 * page_size, PROT_WRITE), "write_protect");
	ASSERT_ERR(mprotect(mmap_ptr, 4 * page_size, PROT_EXEC), "exec_protect");
	ASSERT_ERR_PTR(mremap(mmap_ptr, 0, 4 * page_size, MREMAP_MAYMOVE), "ro_remap");
	ASSERT_OK(munmap(mmap_ptr, 4 * page_size), "unmap_ro");

	/* good read-only pages with initial offset */
	mmap_ptr = mmap(NULL, page_size, PROT_READ, MAP_SHARED, rb_fd, page_size);
	if (!ASSERT_OK_PTR(mmap_ptr, "ro_prod_pos"))
		goto cleanup;

	ASSERT_ERR(mprotect(mmap_ptr, page_size, PROT_WRITE), "write_protect");
	ASSERT_ERR(mprotect(mmap_ptr, page_size, PROT_EXEC), "exec_protect");
	ASSERT_ERR_PTR(mremap(mmap_ptr, 0, 3 * page_size, MREMAP_MAYMOVE), "ro_remap");
	ASSERT_OK(munmap(mmap_ptr, page_size), "unmap_ro");

	/* only trigger BPF program for current process */
	skel->bss->pid = getpid();

	ringbuf = ring_buffer__new(skel->maps.ringbuf.map_fd,
				   process_sample, NULL, NULL);
	if (CHECK(!ringbuf, "ringbuf_create", "failed to create ringbuf\n"))
		goto cleanup;

	err = test_ringbuf_lskel__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attachment failed: %d\n", err))
		goto cleanup;

	trigger_samples();

	ring = ring_buffer__ring(ringbuf, 0);
	if (!ASSERT_OK_PTR(ring, "ring_buffer__ring_idx_0"))
		goto cleanup;

	map_fd = ring__map_fd(ring);
	ASSERT_EQ(map_fd, skel->maps.ringbuf.map_fd, "ring_map_fd");

	/* 2 submitted + 1 discarded records */
	CHECK(skel->bss->avail_data != 3 * rec_sz,
	      "err_avail_size", "exp %ld, got %ld\n",
	      3L * rec_sz, skel->bss->avail_data);
	CHECK(skel->bss->ring_size != page_size,
	      "err_ring_size", "exp %ld, got %ld\n",
	      (long)page_size, skel->bss->ring_size);
	CHECK(skel->bss->cons_pos != 0,
	      "err_cons_pos", "exp %ld, got %ld\n",
	      0L, skel->bss->cons_pos);
	CHECK(skel->bss->prod_pos != 3 * rec_sz,
	      "err_prod_pos", "exp %ld, got %ld\n",
	      3L * rec_sz, skel->bss->prod_pos);

	/* verify getting this data directly via the ring object yields the same
	 * results
	 */
	avail_data = ring__avail_data_size(ring);
	ASSERT_EQ(avail_data, 3 * rec_sz, "ring_avail_size");
	ring_size = ring__size(ring);
	ASSERT_EQ(ring_size, page_size, "ring_ring_size");
	cons_pos = ring__consumer_pos(ring);
	ASSERT_EQ(cons_pos, 0, "ring_cons_pos");
	prod_pos = ring__producer_pos(ring);
	ASSERT_EQ(prod_pos, 3 * rec_sz, "ring_prod_pos");

	/* poll for samples */
	err = ring_buffer__poll(ringbuf, -1);

	/* -EDONE is used as an indicator that we are done */
	if (CHECK(err != -EDONE, "err_done", "done err: %d\n", err))
		goto cleanup;
	cnt = atomic_xchg(&sample_cnt, 0);
	CHECK(cnt != 2, "cnt", "exp %d samples, got %d\n", 2, cnt);

	/* we expect extra polling to return nothing */
	err = ring_buffer__poll(ringbuf, 0);
	if (CHECK(err != 0, "extra_samples", "poll result: %d\n", err))
		goto cleanup;
	cnt = atomic_xchg(&sample_cnt, 0);
	CHECK(cnt != 0, "cnt", "exp %d samples, got %d\n", 0, cnt);

	CHECK(skel->bss->dropped != 0, "err_dropped", "exp %ld, got %ld\n",
	      0L, skel->bss->dropped);
	CHECK(skel->bss->total != 2, "err_total", "exp %ld, got %ld\n",
	      2L, skel->bss->total);
	CHECK(skel->bss->discarded != 1, "err_discarded", "exp %ld, got %ld\n",
	      1L, skel->bss->discarded);

	/* now validate consumer position is updated and returned */
	trigger_samples();
	CHECK(skel->bss->cons_pos != 3 * rec_sz,
	      "err_cons_pos", "exp %ld, got %ld\n",
	      3L * rec_sz, skel->bss->cons_pos);
	err = ring_buffer__poll(ringbuf, -1);
	CHECK(err <= 0, "poll_err", "err %d\n", err);
	cnt = atomic_xchg(&sample_cnt, 0);
	CHECK(cnt != 2, "cnt", "exp %d samples, got %d\n", 2, cnt);

	/* start poll in background w/ long timeout */
	err = pthread_create(&thread, NULL, poll_thread, (void *)(long)10000);
	if (CHECK(err, "bg_poll", "pthread_create failed: %d\n", err))
		goto cleanup;

	/* turn off notifications now */
	skel->bss->flags = BPF_RB_NO_WAKEUP;

	/* give background thread a bit of a time */
	usleep(50000);
	trigger_samples();
	/* sleeping arbitrarily is bad, but no better way to know that
	 * epoll_wait() **DID NOT** unblock in background thread
	 */
	usleep(50000);
	/* background poll should still be blocked */
	err = pthread_tryjoin_np(thread, (void **)&bg_ret);
	if (CHECK(err != EBUSY, "try_join", "err %d\n", err))
		goto cleanup;

	/* BPF side did everything right */
	CHECK(skel->bss->dropped != 0, "err_dropped", "exp %ld, got %ld\n",
	      0L, skel->bss->dropped);
	CHECK(skel->bss->total != 2, "err_total", "exp %ld, got %ld\n",
	      2L, skel->bss->total);
	CHECK(skel->bss->discarded != 1, "err_discarded", "exp %ld, got %ld\n",
	      1L, skel->bss->discarded);
	cnt = atomic_xchg(&sample_cnt, 0);
	CHECK(cnt != 0, "cnt", "exp %d samples, got %d\n", 0, cnt);

	/* clear flags to return to "adaptive" notification mode */
	skel->bss->flags = 0;

	/* produce new samples, no notification should be triggered, because
	 * consumer is now behind
	 */
	trigger_samples();

	/* background poll should still be blocked */
	err = pthread_tryjoin_np(thread, (void **)&bg_ret);
	if (CHECK(err != EBUSY, "try_join", "err %d\n", err))
		goto cleanup;

	/* still no samples, because consumer is behind */
	cnt = atomic_xchg(&sample_cnt, 0);
	CHECK(cnt != 0, "cnt", "exp %d samples, got %d\n", 0, cnt);

	skel->bss->dropped = 0;
	skel->bss->total = 0;
	skel->bss->discarded = 0;

	skel->bss->value = 333;
	syscall(__NR_getpgid);
	/* now force notifications */
	skel->bss->flags = BPF_RB_FORCE_WAKEUP;
	skel->bss->value = 777;
	syscall(__NR_getpgid);

	/* now we should get a pending notification */
	usleep(50000);
	err = pthread_tryjoin_np(thread, (void **)&bg_ret);
	if (CHECK(err, "join_bg", "err %d\n", err))
		goto cleanup;

	if (CHECK(bg_ret <= 0, "bg_ret", "epoll_wait result: %ld", bg_ret))
		goto cleanup;

	/* due to timing variations, there could still be non-notified
	 * samples, so consume them here to collect all the samples
	 */
	err = ring_buffer__consume(ringbuf);
	CHECK(err < 0, "rb_consume", "failed: %d\b", err);

	/* also consume using ring__consume to make sure it works the same */
	err = ring__consume(ring);
	ASSERT_GE(err, 0, "ring_consume");

	/* 3 rounds, 2 samples each */
	cnt = atomic_xchg(&sample_cnt, 0);
	CHECK(cnt != 6, "cnt", "exp %d samples, got %d\n", 6, cnt);

	/* BPF side did everything right */
	CHECK(skel->bss->dropped != 0, "err_dropped", "exp %ld, got %ld\n",
	      0L, skel->bss->dropped);
	CHECK(skel->bss->total != 2, "err_total", "exp %ld, got %ld\n",
	      2L, skel->bss->total);
	CHECK(skel->bss->discarded != 1, "err_discarded", "exp %ld, got %ld\n",
	      1L, skel->bss->discarded);

	test_ringbuf_lskel__detach(skel);
cleanup:
	ring_buffer__free(ringbuf);
	test_ringbuf_lskel__destroy(skel);
}

/*
 * Test ring_buffer__consume_n() by producing N_TOT_SAMPLES samples in the ring
 * buffer, via getpid(), and consuming them in chunks of N_SAMPLES.
 */
#define N_TOT_SAMPLES	32
#define N_SAMPLES	4

/* Sample value to verify the callback validity */
#define SAMPLE_VALUE	42L

static int process_n_sample(void *ctx, void *data, size_t len)
{
	struct sample *s = data;

	ASSERT_EQ(s->value, SAMPLE_VALUE, "sample_value");

	return 0;
}

static void ringbuf_n_subtest(void)
{
	struct test_ringbuf_n_lskel *skel_n;
	int err, i;

	skel_n = test_ringbuf_n_lskel__open();
	if (!ASSERT_OK_PTR(skel_n, "test_ringbuf_n_lskel__open"))
		return;

	skel_n->maps.ringbuf.max_entries = getpagesize();
	skel_n->bss->pid = getpid();

	err = test_ringbuf_n_lskel__load(skel_n);
	if (!ASSERT_OK(err, "test_ringbuf_n_lskel__load"))
		goto cleanup;

	ringbuf = ring_buffer__new(skel_n->maps.ringbuf.map_fd,
				   process_n_sample, NULL, NULL);
	if (!ASSERT_OK_PTR(ringbuf, "ring_buffer__new"))
		goto cleanup;

	err = test_ringbuf_n_lskel__attach(skel_n);
	if (!ASSERT_OK(err, "test_ringbuf_n_lskel__attach"))
		goto cleanup_ringbuf;

	/* Produce N_TOT_SAMPLES samples in the ring buffer by calling getpid() */
	skel_n->bss->value = SAMPLE_VALUE;
	for (i = 0; i < N_TOT_SAMPLES; i++)
		syscall(__NR_getpgid);

	/* Consume all samples from the ring buffer in batches of N_SAMPLES */
	for (i = 0; i < N_TOT_SAMPLES; i += err) {
		err = ring_buffer__consume_n(ringbuf, N_SAMPLES);
		if (!ASSERT_EQ(err, N_SAMPLES, "rb_consume"))
			goto cleanup_ringbuf;
	}

cleanup_ringbuf:
	ring_buffer__free(ringbuf);
cleanup:
	test_ringbuf_n_lskel__destroy(skel_n);
}

static int process_map_key_sample(void *ctx, void *data, size_t len)
{
	struct sample *s;
	int err, val;

	s = data;
	switch (s->seq) {
	case 1:
		ASSERT_EQ(s->value, 42, "sample_value");
		err = bpf_map_lookup_elem(skel_map_key->maps.hash_map.map_fd,
					  s, &val);
		ASSERT_OK(err, "hash_map bpf_map_lookup_elem");
		ASSERT_EQ(val, 1, "hash_map val");
		return -EDONE;
	default:
		return 0;
	}
}

static void ringbuf_map_key_subtest(void)
{
	int err;

	skel_map_key = test_ringbuf_map_key_lskel__open();
	if (!ASSERT_OK_PTR(skel_map_key, "test_ringbuf_map_key_lskel__open"))
		return;

	skel_map_key->maps.ringbuf.max_entries = getpagesize();
	skel_map_key->bss->pid = getpid();

	err = test_ringbuf_map_key_lskel__load(skel_map_key);
	if (!ASSERT_OK(err, "test_ringbuf_map_key_lskel__load"))
		goto cleanup;

	ringbuf = ring_buffer__new(skel_map_key->maps.ringbuf.map_fd,
				   process_map_key_sample, NULL, NULL);
	if (!ASSERT_OK_PTR(ringbuf, "ring_buffer__new"))
		goto cleanup;

	err = test_ringbuf_map_key_lskel__attach(skel_map_key);
	if (!ASSERT_OK(err, "test_ringbuf_map_key_lskel__attach"))
		goto cleanup_ringbuf;

	syscall(__NR_getpgid);
	ASSERT_EQ(skel_map_key->bss->seq, 1, "skel_map_key->bss->seq");
	err = ring_buffer__poll(ringbuf, -1);
	ASSERT_EQ(err, -EDONE, "ring_buffer__poll");

cleanup_ringbuf:
	ring_buffer__free(ringbuf);
cleanup:
	test_ringbuf_map_key_lskel__destroy(skel_map_key);
}

void test_ringbuf(void)
{
	if (test__start_subtest("ringbuf"))
		ringbuf_subtest();
	if (test__start_subtest("ringbuf_n"))
		ringbuf_n_subtest();
	if (test__start_subtest("ringbuf_map_key"))
		ringbuf_map_key_subtest();
}
