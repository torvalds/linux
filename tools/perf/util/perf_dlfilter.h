/* SPDX-License-Identifier: GPL-2.0 */
/*
 * perf_dlfilter.h: API for perf --dlfilter shared object
 * Copyright (c) 2021, Intel Corporation.
 */
#ifndef _LINUX_PERF_DLFILTER_H
#define _LINUX_PERF_DLFILTER_H

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

/*
 * If implemented, 'start' will be called at the beginning,
 * before any calls to 'filter_event'. Return 0 to indicate success,
 * or return a negative error code. '*data' can be assigned for use
 * by other functions. 'ctx' is needed for calls to perf_dlfilter_fns,
 * but most perf_dlfilter_fns are not valid when called from 'start'.
 */
int start(void **data, void *ctx);

/*
 * If implemented, 'stop' will be called at the end,
 * after any calls to 'filter_event'. Return 0 to indicate success, or
 * return a negative error code. 'data' is set by start(). 'ctx' is
 * needed for calls to perf_dlfilter_fns, but most perf_dlfilter_fns
 * are not valid when called from 'stop'.
 */
int stop(void *data, void *ctx);

/*
 * If implemented, 'filter_event' will be called for each sample
 * event. Return 0 to keep the sample event, 1 to filter it out, or
 * return a negative error code. 'data' is set by start(). 'ctx' is
 * needed for calls to perf_dlfilter_fns.
 */
int filter_event(void *data, const struct perf_dlfilter_sample *sample, void *ctx);

/*
 * The same as 'filter_event' except it is called before internal
 * filtering.
 */
int filter_event_early(void *data, const struct perf_dlfilter_sample *sample, void *ctx);

/*
 * If implemented, return a one-line description of the filter, and optionally
 * a longer description.
 */
const char *filter_description(const char **long_description);

#endif
