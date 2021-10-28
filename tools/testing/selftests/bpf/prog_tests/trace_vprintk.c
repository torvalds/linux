// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <test_progs.h>

#include "trace_vprintk.lskel.h"

#define TRACEBUF	"/sys/kernel/debug/tracing/trace_pipe"
#define SEARCHMSG	"1,2,3,4,5,6,7,8,9,10"

void serial_test_trace_vprintk(void)
{
	struct trace_vprintk_lskel__bss *bss;
	int err = 0, iter = 0, found = 0;
	struct trace_vprintk_lskel *skel;
	char *buf = NULL;
	FILE *fp = NULL;
	size_t buflen;

	skel = trace_vprintk_lskel__open_and_load();
	if (!ASSERT_OK_PTR(skel, "trace_vprintk__open_and_load"))
		goto cleanup;

	bss = skel->bss;

	err = trace_vprintk_lskel__attach(skel);
	if (!ASSERT_OK(err, "trace_vprintk__attach"))
		goto cleanup;

	fp = fopen(TRACEBUF, "r");
	if (!ASSERT_OK_PTR(fp, "fopen(TRACEBUF)"))
		goto cleanup;

	/* We do not want to wait forever if this test fails... */
	fcntl(fileno(fp), F_SETFL, O_NONBLOCK);

	/* wait for tracepoint to trigger */
	usleep(1);
	trace_vprintk_lskel__detach(skel);

	if (!ASSERT_GT(bss->trace_vprintk_ran, 0, "bss->trace_vprintk_ran"))
		goto cleanup;

	if (!ASSERT_GT(bss->trace_vprintk_ret, 0, "bss->trace_vprintk_ret"))
		goto cleanup;

	/* verify our search string is in the trace buffer */
	while (getline(&buf, &buflen, fp) >= 0 || errno == EAGAIN) {
		if (strstr(buf, SEARCHMSG) != NULL)
			found++;
		if (found == bss->trace_vprintk_ran)
			break;
		if (++iter > 1000)
			break;
	}

	if (!ASSERT_EQ(found, bss->trace_vprintk_ran, "found"))
		goto cleanup;

	if (!ASSERT_LT(bss->null_data_vprintk_ret, 0, "bss->null_data_vprintk_ret"))
		goto cleanup;

cleanup:
	trace_vprintk_lskel__destroy(skel);
	free(buf);
	if (fp)
		fclose(fp);
}
