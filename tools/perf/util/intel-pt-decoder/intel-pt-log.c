/*
 * intel_pt_log.c: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "intel-pt-log.h"
#include "intel-pt-insn-decoder.h"

#include "intel-pt-pkt-decoder.h"

#define MAX_LOG_NAME 256

static FILE *f;
static char log_name[MAX_LOG_NAME];
bool intel_pt_enable_logging;

void intel_pt_log_enable(void)
{
	intel_pt_enable_logging = true;
}

void intel_pt_log_disable(void)
{
	if (f)
		fflush(f);
	intel_pt_enable_logging = false;
}

void intel_pt_log_set_name(const char *name)
{
	strncpy(log_name, name, MAX_LOG_NAME - 5);
	strcat(log_name, ".log");
}

static void intel_pt_print_data(const unsigned char *buf, int len, uint64_t pos,
				int indent)
{
	int i;

	for (i = 0; i < indent; i++)
		fprintf(f, " ");

	fprintf(f, "  %08" PRIx64 ": ", pos);
	for (i = 0; i < len; i++)
		fprintf(f, " %02x", buf[i]);
	for (; i < 16; i++)
		fprintf(f, "   ");
	fprintf(f, " ");
}

static void intel_pt_print_no_data(uint64_t pos, int indent)
{
	int i;

	for (i = 0; i < indent; i++)
		fprintf(f, " ");

	fprintf(f, "  %08" PRIx64 ": ", pos);
	for (i = 0; i < 16; i++)
		fprintf(f, "   ");
	fprintf(f, " ");
}

static int intel_pt_log_open(void)
{
	if (!intel_pt_enable_logging)
		return -1;

	if (f)
		return 0;

	if (!log_name[0])
		return -1;

	f = fopen(log_name, "w+");
	if (!f) {
		intel_pt_enable_logging = false;
		return -1;
	}

	return 0;
}

void __intel_pt_log_packet(const struct intel_pt_pkt *packet, int pkt_len,
			   uint64_t pos, const unsigned char *buf)
{
	char desc[INTEL_PT_PKT_DESC_MAX];

	if (intel_pt_log_open())
		return;

	intel_pt_print_data(buf, pkt_len, pos, 0);
	intel_pt_pkt_desc(packet, desc, INTEL_PT_PKT_DESC_MAX);
	fprintf(f, "%s\n", desc);
}

void __intel_pt_log_insn(struct intel_pt_insn *intel_pt_insn, uint64_t ip)
{
	char desc[INTEL_PT_INSN_DESC_MAX];
	size_t len = intel_pt_insn->length;

	if (intel_pt_log_open())
		return;

	if (len > INTEL_PT_INSN_BUF_SZ)
		len = INTEL_PT_INSN_BUF_SZ;
	intel_pt_print_data(intel_pt_insn->buf, len, ip, 8);
	if (intel_pt_insn_desc(intel_pt_insn, desc, INTEL_PT_INSN_DESC_MAX) > 0)
		fprintf(f, "%s\n", desc);
	else
		fprintf(f, "Bad instruction!\n");
}

void __intel_pt_log_insn_no_data(struct intel_pt_insn *intel_pt_insn,
				 uint64_t ip)
{
	char desc[INTEL_PT_INSN_DESC_MAX];

	if (intel_pt_log_open())
		return;

	intel_pt_print_no_data(ip, 8);
	if (intel_pt_insn_desc(intel_pt_insn, desc, INTEL_PT_INSN_DESC_MAX) > 0)
		fprintf(f, "%s\n", desc);
	else
		fprintf(f, "Bad instruction!\n");
}

void __intel_pt_log(const char *fmt, ...)
{
	va_list args;

	if (intel_pt_log_open())
		return;

	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);
}
