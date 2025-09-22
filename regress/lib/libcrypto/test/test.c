/*	$OpenBSD: test.c,v 1.4 2025/05/31 11:36:48 tb Exp $ */
/*
 * Copyright (c) 2025 Joshua Sing <joshua@joshuasing.dev>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"

struct test {
	struct test *parent;
	char *name;
	FILE *out;
	int skipped;
	int failed;
};

static struct test *
test_new(struct test *pt, const char *name)
{
	struct test *t;

	if ((t = calloc(1, sizeof(*t))) == NULL)
		err(1, "calloc");

	if (name != NULL) {
		if ((t->name = strdup(name)) == NULL)
			err(1, "strdup");
	}

	if (pt != NULL)
		t->out = pt->out;
	t->parent = pt;

	return t;
}

struct test *
test_init(void)
{
	struct test *t;
	char *tmp_file;
	int out_fd;
	char *v;

	t = test_new(NULL, NULL);
	t->out = stderr;

	if (((v = getenv("TEST_VERBOSE")) != NULL) && strcmp(v, "0") != 0)
		return t;

	/* Create a temporary file for logging in non-verbose mode */
	if ((tmp_file = strdup("/tmp/libressl-test.XXXXXXXX")) == NULL)
		err(1, "strdup");
	if ((out_fd = mkstemp(tmp_file)) == -1)
		err(1, "mkstemp");

	unlink(tmp_file);
	free(tmp_file);
	if ((t->out = fdopen(out_fd, "w+")) == NULL)
		err(1, "fdopen");

	return t;
}

static void
test_cleanup(struct test *t)
{
	free(t->name);
	free(t);
}

int
test_result(struct test *t)
{
	int failed = t->failed;

	if (t->parent == NULL && t->out != stderr)
		fclose(t->out);

	test_cleanup(t);

	return failed;
}

void
test_fail(struct test *t)
{
	t->failed = 1;

	/* Also fail parent. */
	if (t->parent != NULL)
		test_fail(t->parent);
}

static void
test_vprintf(struct test *t, const char *fmt, va_list ap)
{
	if (vfprintf(t->out, fmt, ap) == -1)
		err(1, "vfprintf");
}

void
test_printf(struct test *t, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	test_vprintf(t, fmt, ap);
	va_end(ap);
}

static void
test_vlogf_internal(struct test *t, const char *label, const char *func,
    const char *file, int line, const char *fmt, va_list ap)
{
	char *msg = NULL;
	char *l = ": ";
	const char *filename;

	if (label == NULL) {
		label = "";
		l = "";
	}

	if (vasprintf(&msg, fmt, ap) == -1)
		err(1, "vasprintf");

	if ((filename = strrchr(file, '/')) != NULL)
		filename++;
	else
		filename = file;

	test_printf(t, "%s [%s:%d]%s%s: %s\n",
	    func, filename, line, l, label, msg);

	free(msg);
}

void
test_logf_internal(struct test *t, const char *label, const char *func,
    const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	test_vlogf_internal(t, label, func, file, line, fmt, ap);
	va_end(ap);
}

void
test_skip(struct test *t, const char *reason)
{
	t->skipped = 1;
	test_printf(t, "%s\n", reason);
}

void
test_skipf(struct test *t, const char *fmt, ...)
{
	va_list ap;

	t->skipped = 1;

	va_start(ap, fmt);
	test_vprintf(t, fmt, ap);
        if (fputc('\n', t->out) == EOF)
		err(1, "fputc");
	va_end(ap);
}

void
test_run(struct test *pt, const char *name, test_run_func *fn, const void *arg)
{
	struct test *t = test_new(pt, name);
	char *status = "PASS";
	char buf[1024];
	size_t buflen;
	int ferr;

	/* Run test */
	test_printf(t, "=== RUN   %s\n", t->name);
	fn(t, arg);

	if (t->skipped)
		status = "SKIP";
	if (t->failed)
		status = "FAIL";

	test_printf(t, "--- %s: %s\n\n", status, t->name);

	/* Print result of test */
	if (t->failed && t->out != stderr) {
		/* Copy logs to stderr */
		rewind(t->out);
		while ((buflen = fread(buf, 1, sizeof(buf), t->out)) > 0)
			fwrite(buf, 1, buflen, stderr);
		if ((ferr = ferror(t->out)) != 0)
			errx(1, "ferror: %d", ferr);
	}

	if (t->out != NULL && t->out != stderr)  {
		/* Reset output file */
		rewind(t->out);
		ftruncate(fileno(t->out), 0);
	}

	test_cleanup(t);
}
