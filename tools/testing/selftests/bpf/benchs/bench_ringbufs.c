// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <asm/barrier.h>
#include <linux/perf_event.h>
#include <linux/ring_buffer.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <argp.h>
#include <stdlib.h>
#include "bench.h"
#include "ringbuf_bench.skel.h"
#include "perfbuf_bench.skel.h"

static struct {
	bool back2back;
	int batch_cnt;
	bool sampled;
	int sample_rate;
	int ringbuf_sz; /* per-ringbuf, in bytes */
	bool ringbuf_use_output; /* use slower output API */
	int perfbuf_sz; /* per-CPU size, in pages */
} args = {
	.back2back = false,
	.batch_cnt = 500,
	.sampled = false,
	.sample_rate = 500,
	.ringbuf_sz = 512 * 1024,
	.ringbuf_use_output = false,
	.perfbuf_sz = 128,
};

enum {
	ARG_RB_BACK2BACK = 2000,
	ARG_RB_USE_OUTPUT = 2001,
	ARG_RB_BATCH_CNT = 2002,
	ARG_RB_SAMPLED = 2003,
	ARG_RB_SAMPLE_RATE = 2004,
};

static const struct argp_option opts[] = {
	{ "rb-b2b", ARG_RB_BACK2BACK, NULL, 0, "Back-to-back mode"},
	{ "rb-use-output", ARG_RB_USE_OUTPUT, NULL, 0, "Use bpf_ringbuf_output() instead of bpf_ringbuf_reserve()"},
	{ "rb-batch-cnt", ARG_RB_BATCH_CNT, "CNT", 0, "Set BPF-side record batch count"},
	{ "rb-sampled", ARG_RB_SAMPLED, NULL, 0, "Notification sampling"},
	{ "rb-sample-rate", ARG_RB_SAMPLE_RATE, "RATE", 0, "Notification sample rate"},
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case ARG_RB_BACK2BACK:
		args.back2back = true;
		break;
	case ARG_RB_USE_OUTPUT:
		args.ringbuf_use_output = true;
		break;
	case ARG_RB_BATCH_CNT:
		args.batch_cnt = strtol(arg, NULL, 10);
		if (args.batch_cnt < 0) {
			fprintf(stderr, "Invalid batch count.");
			argp_usage(state);
		}
		break;
	case ARG_RB_SAMPLED:
		args.sampled = true;
		break;
	case ARG_RB_SAMPLE_RATE:
		args.sample_rate = strtol(arg, NULL, 10);
		if (args.sample_rate < 0) {
			fprintf(stderr, "Invalid perfbuf sample rate.");
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

/* exported into benchmark runner */
const struct argp bench_ringbufs_argp = {
	.options = opts,
	.parser = parse_arg,
};

/* RINGBUF-LIBBPF benchmark */

static struct counter buf_hits;

static inline void bufs_trigger_batch()
{
	(void)syscall(__NR_getpgid);
}

static void bufs_validate()
{
	if (env.consumer_cnt != 1) {
		fprintf(stderr, "rb-libbpf benchmark doesn't support multi-consumer!\n");
		exit(1);
	}

	if (args.back2back && env.producer_cnt > 1) {
		fprintf(stderr, "back-to-back mode makes sense only for single-producer case!\n");
		exit(1);
	}
}

static void *bufs_sample_producer(void *input)
{
	if (args.back2back) {
		/* initial batch to get everything started */
		bufs_trigger_batch();
		return NULL;
	}

	while (true)
		bufs_trigger_batch();
	return NULL;
}

static struct ringbuf_libbpf_ctx {
	struct ringbuf_bench *skel;
	struct ring_buffer *ringbuf;
} ringbuf_libbpf_ctx;

static void ringbuf_libbpf_measure(struct bench_res *res)
{
	struct ringbuf_libbpf_ctx *ctx = &ringbuf_libbpf_ctx;

	res->hits = atomic_swap(&buf_hits.value, 0);
	res->drops = atomic_swap(&ctx->skel->bss->dropped, 0);
}

static struct ringbuf_bench *ringbuf_setup_skeleton()
{
	struct ringbuf_bench *skel;

	setup_libbpf();

	skel = ringbuf_bench__open();
	if (!skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	skel->rodata->batch_cnt = args.batch_cnt;
	skel->rodata->use_output = args.ringbuf_use_output ? 1 : 0;

	if (args.sampled)
		/* record data + header take 16 bytes */
		skel->rodata->wakeup_data_size = args.sample_rate * 16;

	bpf_map__resize(skel->maps.ringbuf, args.ringbuf_sz);

	if (ringbuf_bench__load(skel)) {
		fprintf(stderr, "failed to load skeleton\n");
		exit(1);
	}

	return skel;
}

static int buf_process_sample(void *ctx, void *data, size_t len)
{
	atomic_inc(&buf_hits.value);
	return 0;
}

static void ringbuf_libbpf_setup()
{
	struct ringbuf_libbpf_ctx *ctx = &ringbuf_libbpf_ctx;
	struct bpf_link *link;

	ctx->skel = ringbuf_setup_skeleton();
	ctx->ringbuf = ring_buffer__new(bpf_map__fd(ctx->skel->maps.ringbuf),
					buf_process_sample, NULL, NULL);
	if (!ctx->ringbuf) {
		fprintf(stderr, "failed to create ringbuf\n");
		exit(1);
	}

	link = bpf_program__attach(ctx->skel->progs.bench_ringbuf);
	if (IS_ERR(link)) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}
}

static void *ringbuf_libbpf_consumer(void *input)
{
	struct ringbuf_libbpf_ctx *ctx = &ringbuf_libbpf_ctx;

	while (ring_buffer__poll(ctx->ringbuf, -1) >= 0) {
		if (args.back2back)
			bufs_trigger_batch();
	}
	fprintf(stderr, "ringbuf polling failed!\n");
	return NULL;
}

/* RINGBUF-CUSTOM benchmark */
struct ringbuf_custom {
	__u64 *consumer_pos;
	__u64 *producer_pos;
	__u64 mask;
	void *data;
	int map_fd;
};

static struct ringbuf_custom_ctx {
	struct ringbuf_bench *skel;
	struct ringbuf_custom ringbuf;
	int epoll_fd;
	struct epoll_event event;
} ringbuf_custom_ctx;

static void ringbuf_custom_measure(struct bench_res *res)
{
	struct ringbuf_custom_ctx *ctx = &ringbuf_custom_ctx;

	res->hits = atomic_swap(&buf_hits.value, 0);
	res->drops = atomic_swap(&ctx->skel->bss->dropped, 0);
}

static void ringbuf_custom_setup()
{
	struct ringbuf_custom_ctx *ctx = &ringbuf_custom_ctx;
	const size_t page_size = getpagesize();
	struct bpf_link *link;
	struct ringbuf_custom *r;
	void *tmp;
	int err;

	ctx->skel = ringbuf_setup_skeleton();

	ctx->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (ctx->epoll_fd < 0) {
		fprintf(stderr, "failed to create epoll fd: %d\n", -errno);
		exit(1);
	}

	r = &ctx->ringbuf;
	r->map_fd = bpf_map__fd(ctx->skel->maps.ringbuf);
	r->mask = args.ringbuf_sz - 1;

	/* Map writable consumer page */
	tmp = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   r->map_fd, 0);
	if (tmp == MAP_FAILED) {
		fprintf(stderr, "failed to mmap consumer page: %d\n", -errno);
		exit(1);
	}
	r->consumer_pos = tmp;

	/* Map read-only producer page and data pages. */
	tmp = mmap(NULL, page_size + 2 * args.ringbuf_sz, PROT_READ, MAP_SHARED,
		   r->map_fd, page_size);
	if (tmp == MAP_FAILED) {
		fprintf(stderr, "failed to mmap data pages: %d\n", -errno);
		exit(1);
	}
	r->producer_pos = tmp;
	r->data = tmp + page_size;

	ctx->event.events = EPOLLIN;
	err = epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, r->map_fd, &ctx->event);
	if (err < 0) {
		fprintf(stderr, "failed to epoll add ringbuf: %d\n", -errno);
		exit(1);
	}

	link = bpf_program__attach(ctx->skel->progs.bench_ringbuf);
	if (IS_ERR(link)) {
		fprintf(stderr, "failed to attach program\n");
		exit(1);
	}
}

#define RINGBUF_BUSY_BIT (1 << 31)
#define RINGBUF_DISCARD_BIT (1 << 30)
#define RINGBUF_META_LEN 8

static inline int roundup_len(__u32 len)
{
	/* clear out top 2 bits */
	len <<= 2;
	len >>= 2;
	/* add length prefix */
	len += RINGBUF_META_LEN;
	/* round up to 8 byte alignment */
	return (len + 7) / 8 * 8;
}

static void ringbuf_custom_process_ring(struct ringbuf_custom *r)
{
	unsigned long cons_pos, prod_pos;
	int *len_ptr, len;
	bool got_new_data;

	cons_pos = smp_load_acquire(r->consumer_pos);
	while (true) {
		got_new_data = false;
		prod_pos = smp_load_acquire(r->producer_pos);
		while (cons_pos < prod_pos) {
			len_ptr = r->data + (cons_pos & r->mask);
			len = smp_load_acquire(len_ptr);

			/* sample not committed yet, bail out for now */
			if (len & RINGBUF_BUSY_BIT)
				return;

			got_new_data = true;
			cons_pos += roundup_len(len);

			atomic_inc(&buf_hits.value);
		}
		if (got_new_data)
			smp_store_release(r->consumer_pos, cons_pos);
		else
			break;
	}
}

static void *ringbuf_custom_consumer(void *input)
{
	struct ringbuf_custom_ctx *ctx = &ringbuf_custom_ctx;
	int cnt;

	do {
		if (args.back2back)
			bufs_trigger_batch();
		cnt = epoll_wait(ctx->epoll_fd, &ctx->event, 1, -1);
		if (cnt > 0)
			ringbuf_custom_process_ring(&ctx->ringbuf);
	} while (cnt >= 0);
	fprintf(stderr, "ringbuf polling failed!\n");
	return 0;
}

/* PERFBUF-LIBBPF benchmark */
static struct perfbuf_libbpf_ctx {
	struct perfbuf_bench *skel;
	struct perf_buffer *perfbuf;
} perfbuf_libbpf_ctx;

static void perfbuf_measure(struct bench_res *res)
{
	struct perfbuf_libbpf_ctx *ctx = &perfbuf_libbpf_ctx;

	res->hits = atomic_swap(&buf_hits.value, 0);
	res->drops = atomic_swap(&ctx->skel->bss->dropped, 0);
}

static struct perfbuf_bench *perfbuf_setup_skeleton()
{
	struct perfbuf_bench *skel;

	setup_libbpf();

	skel = perfbuf_bench__open();
	if (!skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	skel->rodata->batch_cnt = args.batch_cnt;

	if (perfbuf_bench__load(skel)) {
		fprintf(stderr, "failed to load skeleton\n");
		exit(1);
	}

	return skel;
}

static enum bpf_perf_event_ret
perfbuf_process_sample_raw(void *input_ctx, int cpu,
			   struct perf_event_header *e)
{
	switch (e->type) {
	case PERF_RECORD_SAMPLE:
		atomic_inc(&buf_hits.value);
		break;
	case PERF_RECORD_LOST:
		break;
	default:
		return LIBBPF_PERF_EVENT_ERROR;
	}
	return LIBBPF_PERF_EVENT_CONT;
}

static void perfbuf_libbpf_setup()
{
	struct perfbuf_libbpf_ctx *ctx = &perfbuf_libbpf_ctx;
	struct perf_event_attr attr;
	struct perf_buffer_raw_opts pb_opts = {
		.event_cb = perfbuf_process_sample_raw,
		.ctx = (void *)(long)0,
		.attr = &attr,
	};
	struct bpf_link *link;

	ctx->skel = perfbuf_setup_skeleton();

	memset(&attr, 0, sizeof(attr));
	attr.config = PERF_COUNT_SW_BPF_OUTPUT,
	attr.type = PERF_TYPE_SOFTWARE;
	attr.sample_type = PERF_SAMPLE_RAW;
	/* notify only every Nth sample */
	if (args.sampled) {
		attr.sample_period = args.sample_rate;
		attr.wakeup_events = args.sample_rate;
	} else {
		attr.sample_period = 1;
		attr.wakeup_events = 1;
	}

	if (args.sample_rate > args.batch_cnt) {
		fprintf(stderr, "sample rate %d is too high for given batch count %d\n",
			args.sample_rate, args.batch_cnt);
		exit(1);
	}

	ctx->perfbuf = perf_buffer__new_raw(bpf_map__fd(ctx->skel->maps.perfbuf),
					    args.perfbuf_sz, &pb_opts);
	if (!ctx->perfbuf) {
		fprintf(stderr, "failed to create perfbuf\n");
		exit(1);
	}

	link = bpf_program__attach(ctx->skel->progs.bench_perfbuf);
	if (IS_ERR(link)) {
		fprintf(stderr, "failed to attach program\n");
		exit(1);
	}
}

static void *perfbuf_libbpf_consumer(void *input)
{
	struct perfbuf_libbpf_ctx *ctx = &perfbuf_libbpf_ctx;

	while (perf_buffer__poll(ctx->perfbuf, -1) >= 0) {
		if (args.back2back)
			bufs_trigger_batch();
	}
	fprintf(stderr, "perfbuf polling failed!\n");
	return NULL;
}

/* PERFBUF-CUSTOM benchmark */

/* copies of internal libbpf definitions */
struct perf_cpu_buf {
	struct perf_buffer *pb;
	void *base; /* mmap()'ed memory */
	void *buf; /* for reconstructing segmented data */
	size_t buf_size;
	int fd;
	int cpu;
	int map_key;
};

struct perf_buffer {
	perf_buffer_event_fn event_cb;
	perf_buffer_sample_fn sample_cb;
	perf_buffer_lost_fn lost_cb;
	void *ctx; /* passed into callbacks */

	size_t page_size;
	size_t mmap_size;
	struct perf_cpu_buf **cpu_bufs;
	struct epoll_event *events;
	int cpu_cnt; /* number of allocated CPU buffers */
	int epoll_fd; /* perf event FD */
	int map_fd; /* BPF_MAP_TYPE_PERF_EVENT_ARRAY BPF map FD */
};

static void *perfbuf_custom_consumer(void *input)
{
	struct perfbuf_libbpf_ctx *ctx = &perfbuf_libbpf_ctx;
	struct perf_buffer *pb = ctx->perfbuf;
	struct perf_cpu_buf *cpu_buf;
	struct perf_event_mmap_page *header;
	size_t mmap_mask = pb->mmap_size - 1;
	struct perf_event_header *ehdr;
	__u64 data_head, data_tail;
	size_t ehdr_size;
	void *base;
	int i, cnt;

	while (true) {
		if (args.back2back)
			bufs_trigger_batch();
		cnt = epoll_wait(pb->epoll_fd, pb->events, pb->cpu_cnt, -1);
		if (cnt <= 0) {
			fprintf(stderr, "perf epoll failed: %d\n", -errno);
			exit(1);
		}

		for (i = 0; i < cnt; ++i) {
			cpu_buf = pb->events[i].data.ptr;
			header = cpu_buf->base;
			base = ((void *)header) + pb->page_size;

			data_head = ring_buffer_read_head(header);
			data_tail = header->data_tail;
			while (data_head != data_tail) {
				ehdr = base + (data_tail & mmap_mask);
				ehdr_size = ehdr->size;

				if (ehdr->type == PERF_RECORD_SAMPLE)
					atomic_inc(&buf_hits.value);

				data_tail += ehdr_size;
			}
			ring_buffer_write_tail(header, data_tail);
		}
	}
	return NULL;
}

const struct bench bench_rb_libbpf = {
	.name = "rb-libbpf",
	.validate = bufs_validate,
	.setup = ringbuf_libbpf_setup,
	.producer_thread = bufs_sample_producer,
	.consumer_thread = ringbuf_libbpf_consumer,
	.measure = ringbuf_libbpf_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_rb_custom = {
	.name = "rb-custom",
	.validate = bufs_validate,
	.setup = ringbuf_custom_setup,
	.producer_thread = bufs_sample_producer,
	.consumer_thread = ringbuf_custom_consumer,
	.measure = ringbuf_custom_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_pb_libbpf = {
	.name = "pb-libbpf",
	.validate = bufs_validate,
	.setup = perfbuf_libbpf_setup,
	.producer_thread = bufs_sample_producer,
	.consumer_thread = perfbuf_libbpf_consumer,
	.measure = perfbuf_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_pb_custom = {
	.name = "pb-custom",
	.validate = bufs_validate,
	.setup = perfbuf_libbpf_setup,
	.producer_thread = bufs_sample_producer,
	.consumer_thread = perfbuf_custom_consumer,
	.measure = perfbuf_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

