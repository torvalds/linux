// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_pt_log.c: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include <linux/zalloc.h>
#include <linux/kernel.h>

#include "intel-pt-log.h"
#include "intel-pt-insn-decoder.h"

#include "intel-pt-pkt-decoder.h"

#define MAX_LOG_NAME 256

#define DFLT_BUF_SZ	(16 * 1024)

struct log_buf {
	char			*buf;
	size_t			buf_sz;
	size_t			head;
	bool			wrapped;
	FILE			*backend;
};

static FILE *f;
static char log_name[MAX_LOG_NAME];
bool intel_pt_enable_logging;
static bool intel_pt_dump_log_on_error;
static unsigned int intel_pt_log_on_error_size;
static struct log_buf log_buf;

void *intel_pt_log_fp(void)
{
	return f;
}

void intel_pt_log_enable(bool dump_log_on_error, unsigned int log_on_error_size)
{
	intel_pt_enable_logging = true;
	intel_pt_dump_log_on_error = dump_log_on_error;
	intel_pt_log_on_error_size = log_on_error_size;
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

static ssize_t log_buf__write(void *cookie, const char *buf, size_t size)
{
	struct log_buf *b = cookie;
	size_t sz = size;

	if (!b->buf)
		return size;

	while (sz) {
		size_t space = b->buf_sz - b->head;
		size_t n = min(space, sz);

		memcpy(b->buf + b->head, buf, n);
		sz -= n;
		buf += n;
		b->head += n;
		if (sz && b->head >= b->buf_sz) {
			b->head = 0;
			b->wrapped = true;
		}
	}
	return size;
}

static int log_buf__close(void *cookie)
{
	struct log_buf *b = cookie;

	zfree(&b->buf);
	return 0;
}

static FILE *log_buf__open(struct log_buf *b, FILE *backend, unsigned int sz)
{
	cookie_io_functions_t fns = {
		.write = log_buf__write,
		.close = log_buf__close,
	};
	FILE *file;

	memset(b, 0, sizeof(*b));
	b->buf_sz = sz;
	b->buf = malloc(b->buf_sz);
	b->backend = backend;
	file = fopencookie(b, "a", fns);
	if (!file)
		zfree(&b->buf);
	return file;
}

static void log_buf__dump(struct log_buf *b)
{
	if (!b->buf)
		return;

	fflush(f);
	fprintf(b->backend, "Dumping debug log buffer (first line may be sliced)\n");
	if (b->wrapped)
		fwrite(b->buf + b->head, b->buf_sz - b->head, 1, b->backend);
	fwrite(b->buf, b->head, 1, b->backend);
	fprintf(b->backend, "End of debug log buffer dump\n");

	b->head = 0;
	b->wrapped = false;
}

void intel_pt_log_dump_buf(void)
{
	log_buf__dump(&log_buf);
}

static int intel_pt_log_open(void)
{
	if (!intel_pt_enable_logging)
		return -1;

	if (f)
		return 0;

	if (log_name[0])
		f = fopen(log_name, "w+");
	else
		f = stdout;
	if (f && intel_pt_dump_log_on_error)
		f = log_buf__open(&log_buf, f, intel_pt_log_on_error_size);
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
