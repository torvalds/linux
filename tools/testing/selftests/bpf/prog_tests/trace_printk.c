// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Oracle and/or its affiliates. */

#include <test_progs.h>

#include "trace_printk.lskel.h"

#define TRACEBUF	"/sys/kernel/debug/tracing/trace_pipe"
#define SEARCHMSG	"testing,testing"

void test_trace_printk(void)
{
	int err, iter = 0, duration = 0, found = 0;
	struct trace_printk__bss *bss;
	struct trace_printk *skel;
	char *buf = NULL;
	FILE *fp = NULL;
	size_t buflen;

	skel = trace_printk__open();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;

	ASSERT_EQ(skel->rodata->fmt[0], 'T', "invalid printk fmt string");
	skel->rodata->fmt[0] = 't';

	err = trace_printk__load(skel);
	if (CHECK(err, "skel_load", "failed to load skeleton: %d\n", err))
		goto cleanup;

	bss = skel->bss;

	err = trace_printk__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	fp = fopen(TRACEBUF, "r");
	if (CHECK(fp == NULL, "could not open trace buffer",
		  "error %d opening %s", errno, TRACEBUF))
		goto cleanup;

	/* We do not want to wait forever if this test fails... */
	fcntl(fileno(fp), F_SETFL, O_NONBLOCK);

	/* wait for tracepoint to trigger */
	usleep(1);
	trace_printk__detach(skel);

	if (CHECK(bss->trace_printk_ran == 0,
		  "bpf_trace_printk never ran",
		  "ran == %d", bss->trace_printk_ran))
		goto cleanup;

	if (CHECK(bss->trace_printk_ret <= 0,
		  "bpf_trace_printk returned <= 0 value",
		  "got %d", bss->trace_printk_ret))
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

	if (CHECK(!found, "message from bpf_trace_printk not found",
		  "no instance of %s in %s", SEARCHMSG, TRACEBUF))
		goto cleanup;

cleanup:
	trace_printk__destroy(skel);
	free(buf);
	if (fp)
		fclose(fp);
}
