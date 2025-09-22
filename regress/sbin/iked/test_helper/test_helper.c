/*	$OpenBSD: test_helper.c,v 1.4 2025/05/22 04:34:18 bluhm Exp $	*/
/*
 * Copyright (c) 2011 Damien Miller <djm@mindrot.org>
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

/* Utility functions/framework for regress tests */

#include <sys/types.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <openssl/bn.h>

#include <vis.h>

#include "test_helper.h"

#define TEST_CHECK_INT(r, pred) do {		\
		switch (pred) {			\
		case TEST_EQ:			\
			if (r == 0)		\
				return;		\
			break;			\
		case TEST_NE:			\
			if (r != 0)		\
				return;		\
			break;			\
		case TEST_LT:			\
			if (r < 0)		\
				return;		\
			break;			\
		case TEST_LE:			\
			if (r <= 0)		\
				return;		\
			break;			\
		case TEST_GT:			\
			if (r > 0)		\
				return;		\
			break;			\
		case TEST_GE:			\
			if (r >= 0)		\
				return;		\
			break;			\
		default:			\
			abort();		\
		}				\
	} while (0)

#define TEST_CHECK(x1, x2, pred) do {		\
		switch (pred) {			\
		case TEST_EQ:			\
			if (x1 == x2)		\
				return;		\
			break;			\
		case TEST_NE:			\
			if (x1 != x2)		\
				return;		\
			break;			\
		case TEST_LT:			\
			if (x1 < x2)		\
				return;		\
			break;			\
		case TEST_LE:			\
			if (x1 <= x2)		\
				return;		\
			break;			\
		case TEST_GT:			\
			if (x1 > x2)		\
				return;		\
			break;			\
		case TEST_GE:			\
			if (x1 >= x2)		\
				return;		\
			break;			\
		default:			\
			abort();		\
		}				\
	} while (0)

extern char *__progname;

static int verbose_mode = 0;
static int quiet_mode = 0;
static char *active_test_name = NULL;
static u_int test_number = 0;
static test_onerror_func_t *test_onerror = NULL;
static void *onerror_ctx = NULL;
static const char *data_dir = NULL;

int
main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "vqd:")) != -1) {
		switch (ch) {
		case 'd':
			data_dir = optarg;
			break;
		case 'q':
			verbose_mode = 0;
			quiet_mode = 1;
			break;
		case 'v':
			verbose_mode = 1;
			quiet_mode = 0;
			break;
		default:
			fprintf(stderr, "Unrecognised command line option\n");
			fprintf(stderr, "Usage: %s [-v]\n", __progname);
			exit(1);
		}
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	if (!quiet_mode)
		printf("%s: ", __progname);
	if (verbose_mode)
		printf("\n");

	tests();

	if (!quiet_mode)
		printf(" %u tests ok\n", test_number);
	return 0;
}

const char *
test_data_file(const char *name)
{
	static char ret[PATH_MAX];

	if (data_dir != NULL)
		snprintf(ret, sizeof(ret), "%s/%s", data_dir, name);
	else
		strlcpy(ret, name, sizeof(ret));
	if (access(ret, F_OK) != 0) {
		fprintf(stderr, "Cannot access data file %s: %s\n",
		    ret, strerror(errno));
		exit(1);
	}
	return ret;
}

void
test_start(const char *n)
{
	assert(active_test_name == NULL);
	assert((active_test_name = strdup(n)) != NULL);
	if (verbose_mode)
		printf("test %u - \"%s\": ", test_number, active_test_name);
	test_number++;
}

void
set_onerror_func(test_onerror_func_t *f, void *ctx)
{
	test_onerror = f;
	onerror_ctx = ctx;
}

void
test_done(void)
{
	assert(active_test_name != NULL);
	free(active_test_name);
	active_test_name = NULL;
	if (verbose_mode)
		printf("OK\n");
	else if (!quiet_mode) {
		printf(".");
		fflush(stdout);
	}
}

static const char *
pred_name(enum test_predicate p)
{
	switch (p) {
	case TEST_EQ:
		return "EQ";
	case TEST_NE:
		return "NE";
	case TEST_LT:
		return "LT";
	case TEST_LE:
		return "LE";
	case TEST_GT:
		return "GT";
	case TEST_GE:
		return "GE";
	default:
		return "UNKNOWN";
	}
}

static void
test_die(void)
{
	if (test_onerror != NULL)
		test_onerror(onerror_ctx);
	abort();
}

static void
test_header(const char *file, int line, const char *a1, const char *a2,
    const char *name, enum test_predicate pred)
{
	fprintf(stderr, "\n%s:%d test #%u \"%s\"\n", 
	    file, line, test_number, active_test_name);
	fprintf(stderr, "ASSERT_%s_%s(%s%s%s) failed:\n",
	    name, pred_name(pred), a1,
	    a2 != NULL ? ", " : "", a2 != NULL ? a2 : "");
}

void
assert_string(const char *file, int line, const char *a1, const char *a2,
    const char *aa1, const char *aa2, enum test_predicate pred)
{
	int r = strcmp(aa1, aa2);

	TEST_CHECK_INT(r, pred);
	test_header(file, line, a1, a2, "STRING", pred);
	fprintf(stderr, "%12s = %s (len %zu)\n", a1, aa1, strlen(aa1));
	fprintf(stderr, "%12s = %s (len %zu)\n", a2, aa2, strlen(aa2));
	test_die();
}

static char *
tohex(const void *_s, size_t l)
{
	uint8_t *s = (uint8_t *)_s;
	size_t i, j;
	const char *hex = "0123456789abcdef";
	char *r = malloc((l * 2) + 1);

	assert(r != NULL);
	for (i = j = 0; i < l; i++) {
		r[j++] = hex[(s[i] >> 4) & 0xf];
		r[j++] = hex[s[i] & 0xf];
	}
	r[j] = '\0';
	return r;
}

void
assert_mem(const char *file, int line, const char *a1, const char *a2,
    const void *aa1, const void *aa2, size_t l, enum test_predicate pred)
{
	int r = memcmp(aa1, aa2, l);

	TEST_CHECK_INT(r, pred);
	test_header(file, line, a1, a2, "STRING", pred);
	fprintf(stderr, "%12s = %s (len %zu)\n", a1,
	    tohex(aa1, MINIMUM(l, 256)), l);
	fprintf(stderr, "%12s = %s (len %zu)\n", a2,
	    tohex(aa2, MINIMUM(l, 256)), l);
	test_die();
}

static int
memvalcmp(const uint8_t *s, u_char v, size_t l, size_t *where)
{
	size_t i;

	for (i = 0; i < l; i++) {
		if (s[i] != v) {
			*where = i;
			return 1;
		}
	}
	return 0;
}

void
assert_mem_filled(const char *file, int line, const char *a1,
    const void *aa1, u_char v, size_t l, enum test_predicate pred)
{
	size_t where = -1;
	int r = memvalcmp(aa1, v, l, &where);
	char tmp[64];

	if (l == 0)
		return;
	TEST_CHECK_INT(r, pred);
	test_header(file, line, a1, NULL, "MEM_ZERO", pred);
	fprintf(stderr, "%20s = %s%s (len %zu)\n", a1,
	    tohex(aa1, MINIMUM(l, 20)), l > 20 ? "..." : "", l);
	snprintf(tmp, sizeof(tmp), "(%s)[%zu]", a1, where);
	fprintf(stderr, "%20s = 0x%02x (expected 0x%02x)\n", tmp,
	    ((u_char *)aa1)[where], v);
	test_die();
}

void
assert_int(const char *file, int line, const char *a1, const char *a2,
    int aa1, int aa2, enum test_predicate pred)
{
	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "INT", pred);
	fprintf(stderr, "%12s = %d\n", a1, aa1);
	fprintf(stderr, "%12s = %d\n", a2, aa2);
	test_die();
}

void
assert_size_t(const char *file, int line, const char *a1, const char *a2,
    size_t aa1, size_t aa2, enum test_predicate pred)
{
	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "SIZE_T", pred);
	fprintf(stderr, "%12s = %zu\n", a1, aa1);
	fprintf(stderr, "%12s = %zu\n", a2, aa2);
	test_die();
}

void
assert_u_int(const char *file, int line, const char *a1, const char *a2,
    u_int aa1, u_int aa2, enum test_predicate pred)
{
	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "U_INT", pred);
	fprintf(stderr, "%12s = %u / 0x%x\n", a1, aa1, aa1);
	fprintf(stderr, "%12s = %u / 0x%x\n", a2, aa2, aa2);
	test_die();
}

void
assert_long_long(const char *file, int line, const char *a1, const char *a2,
    long long aa1, long long aa2, enum test_predicate pred)
{
	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "LONG LONG", pred);
	fprintf(stderr, "%12s = %lld / 0x%llx\n", a1, aa1, aa1);
	fprintf(stderr, "%12s = %lld / 0x%llx\n", a2, aa2, aa2);
	test_die();
}

void
assert_char(const char *file, int line, const char *a1, const char *a2,
    char aa1, char aa2, enum test_predicate pred)
{
	char buf[8];

	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "CHAR", pred);
	fprintf(stderr, "%12s = '%s' / 0x02%x\n", a1,
	    vis(buf, aa1, VIS_SAFE|VIS_NL|VIS_TAB|VIS_OCTAL, 0), aa1);
	fprintf(stderr, "%12s = '%s' / 0x02%x\n", a1,
	    vis(buf, aa2, VIS_SAFE|VIS_NL|VIS_TAB|VIS_OCTAL, 0), aa2);
	test_die();
}

void
assert_u8(const char *file, int line, const char *a1, const char *a2,
    uint8_t aa1, uint8_t aa2, enum test_predicate pred)
{
	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "U8", pred);
	fprintf(stderr, "%12s = 0x%02x %u\n", a1, aa1, aa1);
	fprintf(stderr, "%12s = 0x%02x %u\n", a2, aa2, aa2);
	test_die();
}

void
assert_u16(const char *file, int line, const char *a1, const char *a2,
    uint16_t aa1, uint16_t aa2, enum test_predicate pred)
{
	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "U16", pred);
	fprintf(stderr, "%12s = 0x%04x %u\n", a1, aa1, aa1);
	fprintf(stderr, "%12s = 0x%04x %u\n", a2, aa2, aa2);
	test_die();
}

void
assert_u32(const char *file, int line, const char *a1, const char *a2,
    uint32_t aa1, uint32_t aa2, enum test_predicate pred)
{
	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "U32", pred);
	fprintf(stderr, "%12s = 0x%08x %u\n", a1, aa1, aa1);
	fprintf(stderr, "%12s = 0x%08x %u\n", a2, aa2, aa2);
	test_die();
}

void
assert_u64(const char *file, int line, const char *a1, const char *a2,
    uint64_t aa1, uint64_t aa2, enum test_predicate pred)
{
	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "U64", pred);
	fprintf(stderr, "%12s = 0x%016llx %llu\n", a1,
	    (unsigned long long)aa1, (unsigned long long)aa1);
	fprintf(stderr, "%12s = 0x%016llx %llu\n", a2,
	    (unsigned long long)aa2, (unsigned long long)aa2);
	test_die();
}

void
assert_ptr(const char *file, int line, const char *a1, const char *a2,
    const void *aa1, const void *aa2, enum test_predicate pred)
{
	TEST_CHECK(aa1, aa2, pred);
	test_header(file, line, a1, a2, "PTR", pred);
	fprintf(stderr, "%12s = %p\n", a1, aa1);
	fprintf(stderr, "%12s = %p\n", a2, aa2);
	test_die();
}

