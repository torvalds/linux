/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#ifndef INCLUDE__UTIL_PERF_CS_ETM_H__
#define INCLUDE__UTIL_PERF_CS_ETM_H__

#include "util/event.h"
#include "util/session.h"

/* Versionning header in case things need tro change in the future.  That way
 * decoding of old snapshot is still possible.
 */
enum {
	/* Starting with 0x0 */
	CS_HEADER_VERSION_0,
	/* PMU->type (32 bit), total # of CPUs (32 bit) */
	CS_PMU_TYPE_CPUS,
	CS_ETM_SNAPSHOT,
	CS_HEADER_VERSION_0_MAX,
};

/* Beginning of header common to both ETMv3 and V4 */
enum {
	CS_ETM_MAGIC,
	CS_ETM_CPU,
};

/* ETMv3/PTM metadata */
enum {
	/* Dynamic, configurable parameters */
	CS_ETM_ETMCR = CS_ETM_CPU + 1,
	CS_ETM_ETMTRACEIDR,
	/* RO, taken from sysFS */
	CS_ETM_ETMCCER,
	CS_ETM_ETMIDR,
	CS_ETM_PRIV_MAX,
};

/* ETMv4 metadata */
enum {
	/* Dynamic, configurable parameters */
	CS_ETMV4_TRCCONFIGR = CS_ETM_CPU + 1,
	CS_ETMV4_TRCTRACEIDR,
	/* RO, taken from sysFS */
	CS_ETMV4_TRCIDR0,
	CS_ETMV4_TRCIDR1,
	CS_ETMV4_TRCIDR2,
	CS_ETMV4_TRCIDR8,
	CS_ETMV4_TRCAUTHSTATUS,
	CS_ETMV4_PRIV_MAX,
};

/*
 * ETMv3 exception encoding number:
 * See Embedded Trace Macrocell spcification (ARM IHI 0014Q)
 * table 7-12 Encoding of Exception[3:0] for non-ARMv7-M processors.
 */
enum {
	CS_ETMV3_EXC_NONE = 0,
	CS_ETMV3_EXC_DEBUG_HALT = 1,
	CS_ETMV3_EXC_SMC = 2,
	CS_ETMV3_EXC_HYP = 3,
	CS_ETMV3_EXC_ASYNC_DATA_ABORT = 4,
	CS_ETMV3_EXC_JAZELLE_THUMBEE = 5,
	CS_ETMV3_EXC_PE_RESET = 8,
	CS_ETMV3_EXC_UNDEFINED_INSTR = 9,
	CS_ETMV3_EXC_SVC = 10,
	CS_ETMV3_EXC_PREFETCH_ABORT = 11,
	CS_ETMV3_EXC_DATA_FAULT = 12,
	CS_ETMV3_EXC_GENERIC = 13,
	CS_ETMV3_EXC_IRQ = 14,
	CS_ETMV3_EXC_FIQ = 15,
};

/*
 * ETMv4 exception encoding number:
 * See ARM Embedded Trace Macrocell Architecture Specification (ARM IHI 0064D)
 * table 6-12 Possible values for the TYPE field in an Exception instruction
 * trace packet, for ARMv7-A/R and ARMv8-A/R PEs.
 */
enum {
	CS_ETMV4_EXC_RESET = 0,
	CS_ETMV4_EXC_DEBUG_HALT = 1,
	CS_ETMV4_EXC_CALL = 2,
	CS_ETMV4_EXC_TRAP = 3,
	CS_ETMV4_EXC_SYSTEM_ERROR = 4,
	CS_ETMV4_EXC_INST_DEBUG = 6,
	CS_ETMV4_EXC_DATA_DEBUG = 7,
	CS_ETMV4_EXC_ALIGNMENT = 10,
	CS_ETMV4_EXC_INST_FAULT = 11,
	CS_ETMV4_EXC_DATA_FAULT = 12,
	CS_ETMV4_EXC_IRQ = 14,
	CS_ETMV4_EXC_FIQ = 15,
	CS_ETMV4_EXC_END = 31,
};

/* RB tree for quick conversion between traceID and metadata pointers */
struct intlist *traceid_list;

#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)

#define CS_ETM_HEADER_SIZE (CS_HEADER_VERSION_0_MAX * sizeof(u64))

#define __perf_cs_etmv3_magic 0x3030303030303030ULL
#define __perf_cs_etmv4_magic 0x4040404040404040ULL
#define CS_ETMV3_PRIV_SIZE (CS_ETM_PRIV_MAX * sizeof(u64))
#define CS_ETMV4_PRIV_SIZE (CS_ETMV4_PRIV_MAX * sizeof(u64))

#ifdef HAVE_CSTRACE_SUPPORT
int cs_etm__process_auxtrace_info(union perf_event *event,
				  struct perf_session *session);
int cs_etm__get_cpu(u8 trace_chan_id, int *cpu);
#else
static inline int
cs_etm__process_auxtrace_info(union perf_event *event __maybe_unused,
			      struct perf_session *session __maybe_unused)
{
	return -1;
}

static inline int cs_etm__get_cpu(u8 trace_chan_id __maybe_unused,
				  int *cpu __maybe_unused)
{
	return -1;
}
#endif

#endif
