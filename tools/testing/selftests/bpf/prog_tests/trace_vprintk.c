// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <test_progs.h>

#include "trace_vprintk.lskel.h"

#define SEARCHMSG	"1,2,3,4,5,6,7,8,9,10"

static void trace_pipe_cb(const char *str, void *data)
{
	if (strstr(str, SEARCHMSG) != NULL)
		(*(int *)data)++;
}

void serial_test_trace_vprintk(void)
{
	struct trace_vprintk_lskel__bss *bss;
	struct trace_vprintk_lskel *skel;
	int err = 0, found = 0;

	skel = trace_vprintk_lskel__open_and_load();
	if (!ASSERT_OK_PTR(skel, "trace_vprintk__open_and_load"))
		goto cleanup;

	bss = skel->bss;

	err = trace_vprintk_lskel__attach(skel);
	if (!ASSERT_OK(err, "trace_vprintk__attach"))
		goto cleanup;

	/* wait for tracepoint to trigger */
	usleep(1);
	trace_vprintk_lskel__detach(skel);

	if (!ASSERT_GT(bss->trace_vprintk_ran, 0, "bss->trace_vprintk_ran"))
		goto cleanup;

	if (!ASSERT_GT(bss->trace_vprintk_ret, 0, "bss->trace_vprintk_ret"))
		goto cleanup;

	/* verify our search string is in the trace buffer */
	ASSERT_OK(read_trace_pipe_iter(trace_pipe_cb, &found, 1000),
		 "read_trace_pipe_iter");

	if (!ASSERT_EQ(found, bss->trace_vprintk_ran, "found"))
		goto cleanup;

	if (!ASSERT_LT(bss->null_data_vprintk_ret, 0, "bss->null_data_vprintk_ret"))
		goto cleanup;

cleanup:
	trace_vprintk_lskel__destroy(skel);
}
