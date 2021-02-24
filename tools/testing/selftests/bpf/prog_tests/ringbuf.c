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
#include "test_ringbuf.skel.h"

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

static struct test_ringbuf *skel;
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

void test_ringbuf(void)
{
	const size_t rec_sz = BPF_RINGBUF_HDR_SZ + sizeof(struct sample);
	pthread_t thread;
	long bg_ret = -1;
	int err, cnt;

	skel = test_ringbuf__open_and_load();
	if (CHECK(!skel, "skel_open_load", "skeleton open&load failed\n"))
		return;

	/* only trigger BPF program for current process */
	skel->bss->pid = getpid();

	ringbuf = ring_buffer__new(bpf_map__fd(skel->maps.ringbuf),
				   process_sample, NULL, NULL);
	if (CHECK(!ringbuf, "ringbuf_create", "failed to create ringbuf\n"))
		goto cleanup;

	err = test_ringbuf__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attachment failed: %d\n", err))
		goto cleanup;

	trigger_samples();

	/* 2 submitted + 1 discarded records */
	CHECK(skel->bss->avail_data != 3 * rec_sz,
	      "err_avail_size", "exp %ld, got %ld\n",
	      3L * rec_sz, skel->bss->avail_data);
	CHECK(skel->bss->ring_size != 4096,
	      "err_ring_size", "exp %ld, got %ld\n",
	      4096L, skel->bss->ring_size);
	CHECK(skel->bss->cons_pos != 0,
	      "err_cons_pos", "exp %ld, got %ld\n",
	      0L, skel->bss->cons_pos);
	CHECK(skel->bss->prod_pos != 3 * rec_sz,
	      "err_prod_pos", "exp %ld, got %ld\n",
	      3L * rec_sz, skel->bss->prod_pos);

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

	test_ringbuf__detach(skel);
cleanup:
	ring_buffer__free(ringbuf);
	test_ringbuf__destroy(skel);
}
