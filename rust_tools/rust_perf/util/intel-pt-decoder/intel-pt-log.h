/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_pt_log.h: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 */

#ifndef INCLUDE__INTEL_PT_LOG_H__
#define INCLUDE__INTEL_PT_LOG_H__

#include <linux/compiler.h>
#include <stdint.h>
#include <inttypes.h>

struct intel_pt_pkt;

void *intel_pt_log_fp(void);
void intel_pt_log_enable(bool dump_log_on_error, unsigned int log_on_error_size);
void intel_pt_log_disable(void);
void intel_pt_log_set_name(const char *name);
void intel_pt_log_dump_buf(void);

void __intel_pt_log_packet(const struct intel_pt_pkt *packet, int pkt_len,
			   uint64_t pos, const unsigned char *buf);

struct intel_pt_insn;

void __intel_pt_log_insn(struct intel_pt_insn *intel_pt_insn, uint64_t ip);
void __intel_pt_log_insn_no_data(struct intel_pt_insn *intel_pt_insn,
				 uint64_t ip);

void __intel_pt_log(const char *fmt, ...) __printf(1, 2);

#define intel_pt_log(fmt, ...) \
	do { \
		if (intel_pt_enable_logging) \
			__intel_pt_log(fmt, ##__VA_ARGS__); \
	} while (0)

#define intel_pt_log_packet(arg, ...) \
	do { \
		if (intel_pt_enable_logging) \
			__intel_pt_log_packet(arg, ##__VA_ARGS__); \
	} while (0)

#define intel_pt_log_insn(arg, ...) \
	do { \
		if (intel_pt_enable_logging) \
			__intel_pt_log_insn(arg, ##__VA_ARGS__); \
	} while (0)

#define intel_pt_log_insn_no_data(arg, ...) \
	do { \
		if (intel_pt_enable_logging) \
			__intel_pt_log_insn_no_data(arg, ##__VA_ARGS__); \
	} while (0)

#define x64_fmt "0x%" PRIx64

extern bool intel_pt_enable_logging;

static inline void intel_pt_log_at(const char *msg, uint64_t u)
{
	intel_pt_log("%s at " x64_fmt "\n", msg, u);
}

static inline void intel_pt_log_to(const char *msg, uint64_t u)
{
	intel_pt_log("%s to " x64_fmt "\n", msg, u);
}

#define intel_pt_log_var(var, fmt) intel_pt_log("%s: " #var " " fmt "\n", __func__, var)

#define intel_pt_log_x32(var) intel_pt_log_var(var, "%#x")
#define intel_pt_log_x64(var) intel_pt_log_var(var, "%#" PRIx64)

#endif
