// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Oracle and/or its affiliates. */

#include <test_progs.h>

#include "trace_printk.lskel.h"

#define SEARCHMSG	"testing,testing"

static void trace_pipe_cb(const char *str, void *data)
{
	if (strstr(str, SEARCHMSG) != NULL)
		(*(int *)data)++;
}

void serial_test_trace_printk(void)
{
	struct trace_printk_lskel__bss *bss;
	struct trace_printk_lskel *skel;
	int err = 0, found = 0;

	skel = trace_printk_lskel__open();
	if (!ASSERT_OK_PTR(skel, "trace_printk__open"))
		return;

	ASSERT_EQ(skel->rodata->fmt[0], 'T', "skel->rodata->fmt[0]");
	skel->rodata->fmt[0] = 't';

	err = trace_printk_lskel__load(skel);
	if (!ASSERT_OK(err, "trace_printk__load"))
		goto cleanup;

	bss = skel->bss;

	err = trace_printk_lskel__attach(skel);
	if (!ASSERT_OK(err, "trace_printk__attach"))
		goto cleanup;

	/* wait for tracepoint to trigger */
	usleep(1);
	trace_printk_lskel__detach(skel);

	if (!ASSERT_GT(bss->trace_printk_ran, 0, "bss->trace_printk_ran"))
		goto cleanup;

	if (!ASSERT_GT(bss->trace_printk_ret, 0, "bss->trace_printk_ret"))
		goto cleanup;

	/* verify our search string is in the trace buffer */
	ASSERT_OK(read_trace_pipe_iter(trace_pipe_cb, &found, 1000),
		 "read_trace_pipe_iter");

	if (!ASSERT_EQ(found, bss->trace_printk_ran, "found"))
		goto cleanup;

cleanup:
	trace_printk_lskel__destroy(skel);
}
