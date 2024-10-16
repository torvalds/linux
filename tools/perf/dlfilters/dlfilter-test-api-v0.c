// SPDX-License-Identifier: GPL-2.0
/*
 * dlfilter-test-api-v0.c: test original (v0) API for perf --dlfilter shared object
 * Copyright (c) 2021, Intel Corporation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*
 * Copy original (v0) API instead of including current API
 */
#include <linux/perf_event.h>
#include <linux/types.h>

/* Definitions for perf_dlfilter_sample flags */
enum {
	PERF_DLFILTER_FLAG_BRANCH	= 1ULL << 0,
	PERF_DLFILTER_FLAG_CALL		= 1ULL << 1,
	PERF_DLFILTER_FLAG_RETURN	= 1ULL << 2,
	PERF_DLFILTER_FLAG_CONDITIONAL	= 1ULL << 3,
	PERF_DLFILTER_FLAG_SYSCALLRET	= 1ULL << 4,
	PERF_DLFILTER_FLAG_ASYNC	= 1ULL << 5,
	PERF_DLFILTER_FLAG_INTERRUPT	= 1ULL << 6,
	PERF_DLFILTER_FLAG_TX_ABORT	= 1ULL << 7,
	PERF_DLFILTER_FLAG_TRACE_BEGIN	= 1ULL << 8,
	PERF_DLFILTER_FLAG_TRACE_END	= 1ULL << 9,
	PERF_DLFILTER_FLAG_IN_TX	= 1ULL << 10,
	PERF_DLFILTER_FLAG_VMENTRY	= 1ULL << 11,
	PERF_DLFILTER_FLAG_VMEXIT	= 1ULL << 12,
};

/*
 * perf sample event information (as per perf script and <linux/perf_event.h>)
 */
struct perf_dlfilter_sample {
	__u32 size; /* Size of this structure (for compatibility checking) */
	__u16 ins_lat;		/* Refer PERF_SAMPLE_WEIGHT_TYPE in <linux/perf_event.h> */
	__u16 p_stage_cyc;	/* Refer PERF_SAMPLE_WEIGHT_TYPE in <linux/perf_event.h> */
	__u64 ip;
	__s32 pid;
	__s32 tid;
	__u64 time;
	__u64 addr;
	__u64 id;
	__u64 stream_id;
	__u64 period;
	__u64 weight;		/* Refer PERF_SAMPLE_WEIGHT_TYPE in <linux/perf_event.h> */
	__u64 transaction;	/* Refer PERF_SAMPLE_TRANSACTION in <linux/perf_event.h> */
	__u64 insn_cnt;	/* For instructions-per-cycle (IPC) */
	__u64 cyc_cnt;		/* For instructions-per-cycle (IPC) */
	__s32 cpu;
	__u32 flags;		/* Refer PERF_DLFILTER_FLAG_* above */
	__u64 data_src;		/* Refer PERF_SAMPLE_DATA_SRC in <linux/perf_event.h> */
	__u64 phys_addr;	/* Refer PERF_SAMPLE_PHYS_ADDR in <linux/perf_event.h> */
	__u64 data_page_size;	/* Refer PERF_SAMPLE_DATA_PAGE_SIZE in <linux/perf_event.h> */
	__u64 code_page_size;	/* Refer PERF_SAMPLE_CODE_PAGE_SIZE in <linux/perf_event.h> */
	__u64 cgroup;		/* Refer PERF_SAMPLE_CGROUP in <linux/perf_event.h> */
	__u8  cpumode;		/* Refer CPUMODE_MASK etc in <linux/perf_event.h> */
	__u8  addr_correlates_sym; /* True => resolve_addr() can be called */
	__u16 misc;		/* Refer perf_event_header in <linux/perf_event.h> */
	__u32 raw_size;		/* Refer PERF_SAMPLE_RAW in <linux/perf_event.h> */
	const void *raw_data;	/* Refer PERF_SAMPLE_RAW in <linux/perf_event.h> */
	__u64 brstack_nr;	/* Number of brstack entries */
	const struct perf_branch_entry *brstack; /* Refer <linux/perf_event.h> */
	__u64 raw_callchain_nr;	/* Number of raw_callchain entries */
	const __u64 *raw_callchain; /* Refer <linux/perf_event.h> */
	const char *event;
};

/*
 * Address location (as per perf script)
 */
struct perf_dlfilter_al {
	__u32 size; /* Size of this structure (for compatibility checking) */
	__u32 symoff;
	const char *sym;
	__u64 addr; /* Mapped address (from dso) */
	__u64 sym_start;
	__u64 sym_end;
	const char *dso;
	__u8  sym_binding; /* STB_LOCAL, STB_GLOBAL or STB_WEAK, refer <elf.h> */
	__u8  is_64_bit; /* Only valid if dso is not NULL */
	__u8  is_kernel_ip; /* True if in kernel space */
	__u32 buildid_size;
	__u8 *buildid;
	/* Below members are only populated by resolve_ip() */
	__u8 filtered; /* True if this sample event will be filtered out */
	const char *comm;
};

struct perf_dlfilter_fns {
	/* Return information about ip */
	const struct perf_dlfilter_al *(*resolve_ip)(void *ctx);
	/* Return information about addr (if addr_correlates_sym) */
	const struct perf_dlfilter_al *(*resolve_addr)(void *ctx);
	/* Return arguments from --dlarg option */
	char **(*args)(void *ctx, int *dlargc);
	/*
	 * Return information about address (al->size must be set before
	 * calling). Returns 0 on success, -1 otherwise.
	 */
	__s32 (*resolve_address)(void *ctx, __u64 address, struct perf_dlfilter_al *al);
	/* Return instruction bytes and length */
	const __u8 *(*insn)(void *ctx, __u32 *length);
	/* Return source file name and line number */
	const char *(*srcline)(void *ctx, __u32 *line_number);
	/* Return perf_event_attr, refer <linux/perf_event.h> */
	struct perf_event_attr *(*attr)(void *ctx);
	/* Read object code, return numbers of bytes read */
	__s32 (*object_code)(void *ctx, __u64 ip, void *buf, __u32 len);
	/* Reserved */
	void *(*reserved[120])(void *);
};

struct perf_dlfilter_fns perf_dlfilter_fns;

static int verbose;

#define pr_debug(fmt, ...) do { \
		if (verbose > 0) \
			fprintf(stderr, fmt, ##__VA_ARGS__); \
	} while (0)

static int test_fail(const char *msg)
{
	pr_debug("%s\n", msg);
	return -1;
}

#define CHECK(x) do { \
		if (!(x)) \
			return test_fail("Check '" #x "' failed\n"); \
	} while (0)

struct filter_data {
	__u64 ip;
	__u64 addr;
	int do_early;
	int early_filter_cnt;
	int filter_cnt;
};

static struct filter_data *filt_dat;

int start(void **data, void *ctx)
{
	int dlargc;
	char **dlargv;
	struct filter_data *d;
	static bool called;

	verbose = 1;

	CHECK(!filt_dat && !called);
	called = true;

	d = calloc(1, sizeof(*d));
	if (!d)
		test_fail("Failed to allocate memory");
	filt_dat = d;
	*data = d;

	dlargv = perf_dlfilter_fns.args(ctx, &dlargc);

	CHECK(dlargc == 6);
	CHECK(!strcmp(dlargv[0], "first"));
	verbose = strtol(dlargv[1], NULL, 0);
	d->ip = strtoull(dlargv[2], NULL, 0);
	d->addr = strtoull(dlargv[3], NULL, 0);
	d->do_early = strtol(dlargv[4], NULL, 0);
	CHECK(!strcmp(dlargv[5], "last"));

	pr_debug("%s API\n", __func__);

	return 0;
}

#define CHECK_SAMPLE(x) do { \
		if (sample->x != expected.x) \
			return test_fail("'" #x "' not expected value\n"); \
	} while (0)

static int check_sample(struct filter_data *d, const struct perf_dlfilter_sample *sample)
{
	struct perf_dlfilter_sample expected = {
		.ip		= d->ip,
		.pid		= 12345,
		.tid		= 12346,
		.time		= 1234567890,
		.addr		= d->addr,
		.id		= 99,
		.stream_id	= 101,
		.period		= 543212345,
		.cpu		= 31,
		.cpumode	= PERF_RECORD_MISC_USER,
		.addr_correlates_sym = 1,
		.misc		= PERF_RECORD_MISC_USER,
	};

	CHECK(sample->size >= sizeof(struct perf_dlfilter_sample));

	CHECK_SAMPLE(ip);
	CHECK_SAMPLE(pid);
	CHECK_SAMPLE(tid);
	CHECK_SAMPLE(time);
	CHECK_SAMPLE(addr);
	CHECK_SAMPLE(id);
	CHECK_SAMPLE(stream_id);
	CHECK_SAMPLE(period);
	CHECK_SAMPLE(cpu);
	CHECK_SAMPLE(cpumode);
	CHECK_SAMPLE(addr_correlates_sym);
	CHECK_SAMPLE(misc);

	CHECK(!sample->raw_data);
	CHECK_SAMPLE(brstack_nr);
	CHECK(!sample->brstack);
	CHECK_SAMPLE(raw_callchain_nr);
	CHECK(!sample->raw_callchain);

#define EVENT_NAME "branches"
	CHECK(!strncmp(sample->event, EVENT_NAME, strlen(EVENT_NAME)));

	return 0;
}

static int check_al(void *ctx)
{
	const struct perf_dlfilter_al *al;

	al = perf_dlfilter_fns.resolve_ip(ctx);
	if (!al)
		return test_fail("resolve_ip() failed");

	CHECK(al->sym && !strcmp("foo", al->sym));
	CHECK(!al->symoff);

	return 0;
}

static int check_addr_al(void *ctx)
{
	const struct perf_dlfilter_al *addr_al;

	addr_al = perf_dlfilter_fns.resolve_addr(ctx);
	if (!addr_al)
		return test_fail("resolve_addr() failed");

	CHECK(addr_al->sym && !strcmp("bar", addr_al->sym));
	CHECK(!addr_al->symoff);

	return 0;
}

static int check_address_al(void *ctx, const struct perf_dlfilter_sample *sample)
{
	struct perf_dlfilter_al address_al;
	const struct perf_dlfilter_al *al;

	al = perf_dlfilter_fns.resolve_ip(ctx);
	if (!al)
		return test_fail("resolve_ip() failed");

	address_al.size = sizeof(address_al);
	if (perf_dlfilter_fns.resolve_address(ctx, sample->ip, &address_al))
		return test_fail("resolve_address() failed");

	CHECK(address_al.sym && al->sym);
	CHECK(!strcmp(address_al.sym, al->sym));
	CHECK(address_al.addr == al->addr);
	CHECK(address_al.sym_start == al->sym_start);
	CHECK(address_al.sym_end == al->sym_end);
	CHECK(address_al.dso && al->dso);
	CHECK(!strcmp(address_al.dso, al->dso));

	return 0;
}

static int check_attr(void *ctx)
{
	struct perf_event_attr *attr = perf_dlfilter_fns.attr(ctx);

	CHECK(attr);
	CHECK(attr->type == PERF_TYPE_HARDWARE);
	CHECK(attr->config == PERF_COUNT_HW_BRANCH_INSTRUCTIONS);

	return 0;
}

static int check_object_code(void *ctx, const struct perf_dlfilter_sample *sample)
{
	__u8 buf[15];

	CHECK(perf_dlfilter_fns.object_code(ctx, sample->ip, buf, sizeof(buf)) > 0);

	return 0;
}

static int do_checks(void *data, const struct perf_dlfilter_sample *sample, void *ctx, bool early)
{
	struct filter_data *d = data;

	CHECK(data && filt_dat == data);

	if (early) {
		CHECK(!d->early_filter_cnt);
		d->early_filter_cnt += 1;
	} else {
		CHECK(!d->filter_cnt);
		CHECK(d->early_filter_cnt);
		CHECK(d->do_early != 2);
		d->filter_cnt += 1;
	}

	if (check_sample(data, sample))
		return -1;

	if (check_attr(ctx))
		return -1;

	if (early && !d->do_early)
		return 0;

	if (check_al(ctx) || check_addr_al(ctx) || check_address_al(ctx, sample) ||
	    check_object_code(ctx, sample))
		return -1;

	if (early)
		return d->do_early == 2;

	return 1;
}

int filter_event_early(void *data, const struct perf_dlfilter_sample *sample, void *ctx)
{
	pr_debug("%s API\n", __func__);

	return do_checks(data, sample, ctx, true);
}

int filter_event(void *data, const struct perf_dlfilter_sample *sample, void *ctx)
{
	pr_debug("%s API\n", __func__);

	return do_checks(data, sample, ctx, false);
}

int stop(void *data, void *ctx)
{
	static bool called;

	pr_debug("%s API\n", __func__);

	CHECK(data && filt_dat == data && !called);
	called = true;

	free(data);
	filt_dat = NULL;
	return 0;
}

const char *filter_description(const char **long_description)
{
	*long_description = "Filter used by the 'dlfilter C API' perf test";
	return "dlfilter to test v0 C API";
}
