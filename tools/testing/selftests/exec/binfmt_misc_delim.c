// SPDX-License-Identifier: GPL-2.0
/*
 * Test which characters may delimit the fields of a register string.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "binfmt_misc_common.h"
#include "kselftest_harness.h"

#define ENTRY	"bmdelim"
/* Shares no character with the sets below, or a refusal proves nothing. */
#define MAGIC	"bmmagic"
#define INTERP	"/bin/true"

/*
 * ASCII punctuation without '\' and '/'. The backslash is refused because
 * it would cut a magic that uses \x to escape short. '/' is accepted
 * but cannot delimit a rule that names an absolute interpreter.
 */
#define PUNCTUATION	"!\"#$%&'()*+,-.:;<=>?@[]^_`{|}~"

/* 'M', 'E' and 'B' name types, 'P' through 'D' are the flags. */
#define LETTERS		"MEBPOCFTLDqz"
#define DIGITS		"0157"
#define WHITESPACE	" \t\n"
#define CONTROL		"\001\033\177"
#define NON_ASCII	"\200\244\377"

/* ':bmdelim:E::bmmagic::/bin/true:' with @del in place of every ':'. */
static int register_with(char del)
{
	char rule[128];

	snprintf(rule, sizeof(rule), "%c%s%cE%c%c%s%c%c%s%c", del, ENTRY, del,
		 del, del, MAGIC, del, del, INTERP, del);
	return write_reg(rule);
}

/* No character of @set may delimit a register string. */
static void expect_refused(struct __test_metadata *_metadata, const char *set)
{
	const char *d;

	for (d = set; *d; d++) {
		int rc = register_with(*d);

		EXPECT_EQ(rc, -1)
			TH_LOG("%#x delimited a register string",
			       (unsigned char)*d);
		if (rc == 0) {
			unregister(ENTRY);
			continue;
		}
		EXPECT_EQ(errno, EINVAL);
	}
}

FIXTURE(delim) {
};

FIXTURE_SETUP(delim)
{
	if (getuid() != 0)
		SKIP(return, "test must be run as root");
	if (!binfmt_misc_available())
		SKIP(return, "no binfmt_misc");

	/* A kernel without the allow-list takes any character but a flag. */
	if (register_with('q') == 0) {
		unregister(ENTRY);
		SKIP(return, "kernel without the delimiter allow-list");
	}
}

FIXTURE_TEARDOWN(delim)
{
	unregister(ENTRY);
}

/* Punctuation delimits, which is all anything deployed ever uses. */
TEST_F(delim, punctuation_accepted)
{
	const char *d;

	for (d = PUNCTUATION; *d; d++) {
		EXPECT_EQ(register_with(*d), 0)
			TH_LOG("'%c' refused with errno %d", *d, errno);
		unregister(ENTRY);
	}
}

/* Letters name the types and the flags, so none of them can delimit. */
TEST_F(delim, letters_refused)
{
	expect_refused(_metadata, LETTERS);
}

/* The offset field is written in digits. */
TEST_F(delim, digits_refused)
{
	expect_refused(_metadata, DIGITS);
}

TEST_F(delim, whitespace_refused)
{
	expect_refused(_metadata, WHITESPACE);
}

TEST_F(delim, control_refused)
{
	expect_refused(_metadata, CONTROL);
}

TEST_F(delim, non_ascii_refused)
{
	expect_refused(_metadata, NON_ASCII);
}

/* The escape character would cut every magic that uses one short. */
TEST_F(delim, backslash_refused)
{
	expect_refused(_metadata, "\\");
}

TEST_HARNESS_MAIN
