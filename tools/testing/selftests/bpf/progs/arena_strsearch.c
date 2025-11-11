// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include "bpf_experimental.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 100); /* number of pages */
} arena SEC(".maps");

#include "bpf_arena_strsearch.h"

struct glob_test {
	char const __arena *pat, *str;
	bool expected;
};

static bool test(char const __arena *pat, char const __arena *str, bool expected)
{
	bool match = glob_match(pat, str);
	bool success = match == expected;

	/* bpf_printk("glob_match %s %s res %d ok %d", pat, str, match, success); */
	return success;
}

/*
 * The tests are all jammed together in one array to make it simpler
 * to place that array in the .init.rodata section.  The obvious
 * "array of structures containing char *" has no way to force the
 * pointed-to strings to be in a particular section.
 *
 * Anyway, a test consists of:
 * 1. Expected glob_match result: '1' or '0'.
 * 2. Pattern to match: null-terminated string
 * 3. String to match against: null-terminated string
 *
 * The list of tests is terminated with a final '\0' instead of
 * a glob_match result character.
 */
static const char __arena glob_tests[] =
	/* Some basic tests */
	"1" "a\0" "a\0"
	"0" "a\0" "b\0"
	"0" "a\0" "aa\0"
	"0" "a\0" "\0"
	"1" "\0" "\0"
	"0" "\0" "a\0"
	/* Simple character class tests */
	"1" "[a]\0" "a\0"
	"0" "[a]\0" "b\0"
	"0" "[!a]\0" "a\0"
	"1" "[!a]\0" "b\0"
	"1" "[ab]\0" "a\0"
	"1" "[ab]\0" "b\0"
	"0" "[ab]\0" "c\0"
	"1" "[!ab]\0" "c\0"
	"1" "[a-c]\0" "b\0"
	"0" "[a-c]\0" "d\0"
	/* Corner cases in character class parsing */
	"1" "[a-c-e-g]\0" "-\0"
	"0" "[a-c-e-g]\0" "d\0"
	"1" "[a-c-e-g]\0" "f\0"
	"1" "[]a-ceg-ik[]\0" "a\0"
	"1" "[]a-ceg-ik[]\0" "]\0"
	"1" "[]a-ceg-ik[]\0" "[\0"
	"1" "[]a-ceg-ik[]\0" "h\0"
	"0" "[]a-ceg-ik[]\0" "f\0"
	"0" "[!]a-ceg-ik[]\0" "h\0"
	"0" "[!]a-ceg-ik[]\0" "]\0"
	"1" "[!]a-ceg-ik[]\0" "f\0"
	/* Simple wild cards */
	"1" "?\0" "a\0"
	"0" "?\0" "aa\0"
	"0" "??\0" "a\0"
	"1" "?x?\0" "axb\0"
	"0" "?x?\0" "abx\0"
	"0" "?x?\0" "xab\0"
	/* Asterisk wild cards (backtracking) */
	"0" "*??\0" "a\0"
	"1" "*??\0" "ab\0"
	"1" "*??\0" "abc\0"
	"1" "*??\0" "abcd\0"
	"0" "??*\0" "a\0"
	"1" "??*\0" "ab\0"
	"1" "??*\0" "abc\0"
	"1" "??*\0" "abcd\0"
	"0" "?*?\0" "a\0"
	"1" "?*?\0" "ab\0"
	"1" "?*?\0" "abc\0"
	"1" "?*?\0" "abcd\0"
	"1" "*b\0" "b\0"
	"1" "*b\0" "ab\0"
	"0" "*b\0" "ba\0"
	"1" "*b\0" "bb\0"
	"1" "*b\0" "abb\0"
	"1" "*b\0" "bab\0"
	"1" "*bc\0" "abbc\0"
	"1" "*bc\0" "bc\0"
	"1" "*bc\0" "bbc\0"
	"1" "*bc\0" "bcbc\0"
	/* Multiple asterisks (complex backtracking) */
	"1" "*ac*\0" "abacadaeafag\0"
	"1" "*ac*ae*ag*\0" "abacadaeafag\0"
	"1" "*a*b*[bc]*[ef]*g*\0" "abacadaeafag\0"
	"0" "*a*b*[ef]*[cd]*g*\0" "abacadaeafag\0"
	"1" "*abcd*\0" "abcabcabcabcdefg\0"
	"1" "*ab*cd*\0" "abcabcabcabcdefg\0"
	"1" "*abcd*abcdef*\0" "abcabcdabcdeabcdefg\0"
	"0" "*abcd*\0" "abcabcabcabcefg\0"
	"0" "*ab*cd*\0" "abcabcabcabcefg\0";

bool skip = false;

SEC("syscall")
int arena_strsearch(void *ctx)
{
	unsigned successes = 0;
	unsigned n = 0;
	char const __arena *p = glob_tests;

	/*
	 * Tests are jammed together in a string.  The first byte is '1'
	 * or '0' to indicate the expected outcome, or '\0' to indicate the
	 * end of the tests.  Then come two null-terminated strings: the
	 * pattern and the string to match it against.
	 */
	while (*p) {
		bool expected = *p++ & 1;
		char const __arena *pat = p;

		cond_break;
		p += bpf_arena_strlen(p) + 1;
		successes += test(pat, p, expected);
		p += bpf_arena_strlen(p) + 1;
		n++;
	}

	n -= successes;
	/* bpf_printk("glob: %u self-tests passed, %u failed\n", successes, n); */

	return n ? -1 : 0;
}

char _license[] SEC("license") = "GPL";
