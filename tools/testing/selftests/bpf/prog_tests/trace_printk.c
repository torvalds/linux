// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Oracle and/or its affiliates. */

#include <test_progs.h>

#include "trace_printk.lskel.h"

#define TRACEBUF	"/sys/kernel/debug/tracing/trace_pipe"
#define SEARCHMSG	"testing,testing"

void test_trace_printk(void)
{
	int err = 0, iter = 0, found = 0;
	struct trace_printk__bss *bss;
	struct trace_printk *skel;
	char *buf = NULL;
	FILE *fp = NULL;
	size_t buflen;

	skel = trace_printk__open();
	if (!ASSERT_OK_PTR(skel, "trace_printk__open"))
		return;

	ASSERT_EQ(skel->rodata->fmt[0], 'T', "skel->rodata->fmt[0]");
	skel->rodata->fmt[0] = 't';

	err = trace_printk__load(skel);
	if (!ASSERT_OK(err, "trace_printk__load"))
		goto cleanup;

	bss = skel->bss;

	err = trace_printk__attach(skel);
	if (!ASSERT_OK(err, "trace_printk__attach"))
		goto cleanup;

	fp = fopen(TRACEBUF, "r");
	if (!ASSERT_OK_PTR(fp, "fopen(TRACEBUF)"))
		goto cleanup;

	/* We do not want to wait forever if this test fails... */
	fcntl(fileno(fp), F_SETFL, O_NONBLOCK);

	/* wait for tracepoint to trigger */
	usleep(1);
	trace_printk__detach(skel);

	if (!ASSERT_GT(bss->trace_printk_ran, 0, "bss->trace_printk_ran"))
		goto cleanup;

	if (!ASSERT_GT(bss->trace_printk_ret, 0, "bss->trace_printk_ret"))
		goto cleanup;

	/* verify our search string is in the trace buffer */
	while (getline(&buf, &buflen, fp) >= 0 || errno == EAGAIN) {
		if (strstr(buf, SEARCHMSG) != NULL)
			found++;
		if (found == bss->trace_printk_ran)
			break;
		if (++iter > 1000)
			break;
	}

	if (!ASSERT_EQ(found, bss->trace_printk_ran, "found"))
		goto cleanup;

cleanup:
	trace_printk__destroy(skel);
	free(buf);
	if (fp)
		fclose(fp);
}
