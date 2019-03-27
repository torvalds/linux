/*-
 * Copyright (c) 2010 Jilles Tjoelker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <fnmatch.h>

struct testcase {
	const char *pattern;
	const char *string;
	int flags;
	int result;
} testcases[] = {
	{ "", "", 0, 0 },
	{ "a", "a", 0, 0 },
	{ "a", "b", 0, FNM_NOMATCH },
	{ "a", "A", 0, FNM_NOMATCH },
	{ "*", "a", 0, 0 },
	{ "*", "aa", 0, 0 },
	{ "*a", "a", 0, 0 },
	{ "*a", "b", 0, FNM_NOMATCH },
	{ "*a*", "b", 0, FNM_NOMATCH },
	{ "*a*b*", "ab", 0, 0 },
	{ "*a*b*", "qaqbq", 0, 0 },
	{ "*a*bb*", "qaqbqbbq", 0, 0 },
	{ "*a*bc*", "qaqbqbcq", 0, 0 },
	{ "*a*bb*", "qaqbqbb", 0, 0 },
	{ "*a*bc*", "qaqbqbc", 0, 0 },
	{ "*a*bb", "qaqbqbb", 0, 0 },
	{ "*a*bc", "qaqbqbc", 0, 0 },
	{ "*a*bb", "qaqbqbbq", 0, FNM_NOMATCH },
	{ "*a*bc", "qaqbqbcq", 0, FNM_NOMATCH },
	{ "*a*a*a*a*a*a*a*a*a*a*", "aaaaaaaaa", 0, FNM_NOMATCH },
	{ "*a*a*a*a*a*a*a*a*a*a*", "aaaaaaaaaa", 0, 0 },
	{ "*a*a*a*a*a*a*a*a*a*a*", "aaaaaaaaaaa", 0, 0 },
	{ ".*.*.*.*.*.*.*.*.*.*", ".........", 0, FNM_NOMATCH },
	{ ".*.*.*.*.*.*.*.*.*.*", "..........", 0, 0 },
	{ ".*.*.*.*.*.*.*.*.*.*", "...........", 0, 0 },
	{ "*?*?*?*?*?*?*?*?*?*?*", "123456789", 0, FNM_NOMATCH },
	{ "??????????*", "123456789", 0, FNM_NOMATCH },
	{ "*??????????", "123456789", 0, FNM_NOMATCH },
	{ "*?*?*?*?*?*?*?*?*?*?*", "1234567890", 0, 0 },
	{ "??????????*", "1234567890", 0, 0 },
	{ "*??????????", "1234567890", 0, 0 },
	{ "*?*?*?*?*?*?*?*?*?*?*", "12345678901", 0, 0 },
	{ "??????????*", "12345678901", 0, 0 },
	{ "*??????????", "12345678901", 0, 0 },
	{ "[x]", "x", 0, 0 },
	{ "[*]", "*", 0, 0 },
	{ "[?]", "?", 0, 0 },
	{ "[", "[", 0, 0 },
	{ "[[]", "[", 0, 0 },
	{ "[[]", "x", 0, FNM_NOMATCH },
	{ "[*]", "", 0, FNM_NOMATCH },
	{ "[*]", "x", 0, FNM_NOMATCH },
	{ "[?]", "x", 0, FNM_NOMATCH },
	{ "*[*]*", "foo*foo", 0, 0 },
	{ "*[*]*", "foo", 0, FNM_NOMATCH },
	{ "[0-9]", "0", 0, 0 },
	{ "[0-9]", "5", 0, 0 },
	{ "[0-9]", "9", 0, 0 },
	{ "[0-9]", "/", 0, FNM_NOMATCH },
	{ "[0-9]", ":", 0, FNM_NOMATCH },
	{ "[0-9]", "*", 0, FNM_NOMATCH },
	{ "[!0-9]", "0", 0, FNM_NOMATCH },
	{ "[!0-9]", "5", 0, FNM_NOMATCH },
	{ "[!0-9]", "9", 0, FNM_NOMATCH },
	{ "[!0-9]", "/", 0, 0 },
	{ "[!0-9]", ":", 0, 0 },
	{ "[!0-9]", "*", 0, 0 },
	{ "*[0-9]", "a0", 0, 0 },
	{ "*[0-9]", "a5", 0, 0 },
	{ "*[0-9]", "a9", 0, 0 },
	{ "*[0-9]", "a/", 0, FNM_NOMATCH },
	{ "*[0-9]", "a:", 0, FNM_NOMATCH },
	{ "*[0-9]", "a*", 0, FNM_NOMATCH },
	{ "*[!0-9]", "a0", 0, FNM_NOMATCH },
	{ "*[!0-9]", "a5", 0, FNM_NOMATCH },
	{ "*[!0-9]", "a9", 0, FNM_NOMATCH },
	{ "*[!0-9]", "a/", 0, 0 },
	{ "*[!0-9]", "a:", 0, 0 },
	{ "*[!0-9]", "a*", 0, 0 },
	{ "*[0-9]", "a00", 0, 0 },
	{ "*[0-9]", "a55", 0, 0 },
	{ "*[0-9]", "a99", 0, 0 },
	{ "*[0-9]", "a0a0", 0, 0 },
	{ "*[0-9]", "a5a5", 0, 0 },
	{ "*[0-9]", "a9a9", 0, 0 },
	{ "\\*", "*", 0, 0 },
	{ "\\?", "?", 0, 0 },
	{ "\\[x]", "[x]", 0, 0 },
	{ "\\[", "[", 0, 0 },
	{ "\\\\", "\\", 0, 0 },
	{ "*\\**", "foo*foo", 0, 0 },
	{ "*\\**", "foo", 0, FNM_NOMATCH },
	{ "*\\\\*", "foo\\foo", 0, 0 },
	{ "*\\\\*", "foo", 0, FNM_NOMATCH },
	{ "\\(", "(", 0, 0 },
	{ "\\a", "a", 0, 0 },
	{ "\\*", "a", 0, FNM_NOMATCH },
	{ "\\?", "a", 0, FNM_NOMATCH },
	{ "\\*", "\\*", 0, FNM_NOMATCH },
	{ "\\?", "\\?", 0, FNM_NOMATCH },
	{ "\\[x]", "\\[x]", 0, FNM_NOMATCH },
	{ "\\[x]", "\\x", 0, FNM_NOMATCH },
	{ "\\[", "\\[", 0, FNM_NOMATCH },
	{ "\\(", "\\(", 0, FNM_NOMATCH },
	{ "\\a", "\\a", 0, FNM_NOMATCH },
	{ "\\", "\\", 0, FNM_NOMATCH },
	{ "\\", "", 0, FNM_NOMATCH },
	{ "\\*", "\\*", FNM_NOESCAPE, 0 },
	{ "\\?", "\\?", FNM_NOESCAPE, 0 },
	{ "\\", "\\", FNM_NOESCAPE, 0 },
	{ "\\\\", "\\", FNM_NOESCAPE, FNM_NOMATCH },
	{ "\\\\", "\\\\", FNM_NOESCAPE, 0 },
	{ "*\\*", "foo\\foo", FNM_NOESCAPE, 0 },
	{ "*\\*", "foo", FNM_NOESCAPE, FNM_NOMATCH },
	{ "*", ".", FNM_PERIOD, FNM_NOMATCH },
	{ "?", ".", FNM_PERIOD, FNM_NOMATCH },
	{ ".*", ".", 0, 0 },
	{ ".*", "..", 0, 0 },
	{ ".*", ".a", 0, 0 },
	{ "[0-9]", ".", FNM_PERIOD, FNM_NOMATCH },
	{ "a*", "a.", 0, 0 },
	{ "a/a", "a/a", FNM_PATHNAME, 0 },
	{ "a/*", "a/a", FNM_PATHNAME, 0 },
	{ "*/a", "a/a", FNM_PATHNAME, 0 },
	{ "*/*", "a/a", FNM_PATHNAME, 0 },
	{ "a*b/*", "abbb/x", FNM_PATHNAME, 0 },
	{ "a*b/*", "abbb/.x", FNM_PATHNAME, 0 },
	{ "*", "a/a", FNM_PATHNAME, FNM_NOMATCH },
	{ "*/*", "a/a/a", FNM_PATHNAME, FNM_NOMATCH },
	{ "b/*", "b/.x", FNM_PATHNAME | FNM_PERIOD, FNM_NOMATCH },
	{ "b*/*", "a/.x", FNM_PATHNAME | FNM_PERIOD, FNM_NOMATCH },
	{ "b/.*", "b/.x", FNM_PATHNAME | FNM_PERIOD, 0 },
	{ "b*/.*", "b/.x", FNM_PATHNAME | FNM_PERIOD, 0 },
	{ "a", "A", FNM_CASEFOLD, 0 },
	{ "A", "a", FNM_CASEFOLD, 0 },
	{ "[a]", "A", FNM_CASEFOLD, 0 },
	{ "[A]", "a", FNM_CASEFOLD, 0 },
	{ "a", "b", FNM_CASEFOLD, FNM_NOMATCH },
	{ "a", "a/b", FNM_PATHNAME, FNM_NOMATCH },
	{ "*", "a/b", FNM_PATHNAME, FNM_NOMATCH },
	{ "*b", "a/b", FNM_PATHNAME, FNM_NOMATCH },
	{ "a", "a/b", FNM_PATHNAME | FNM_LEADING_DIR, 0 },
	{ "*", "a/b", FNM_PATHNAME | FNM_LEADING_DIR, 0 },
	{ "*", ".a/b", FNM_PATHNAME | FNM_LEADING_DIR, 0 },
	{ "*a", ".a/b", FNM_PATHNAME | FNM_LEADING_DIR, 0 },
	{ "*", ".a/b", FNM_PATHNAME | FNM_PERIOD | FNM_LEADING_DIR, FNM_NOMATCH },
	{ "*a", ".a/b", FNM_PATHNAME | FNM_PERIOD | FNM_LEADING_DIR, FNM_NOMATCH },
	{ "a*b/*", "abbb/.x", FNM_PATHNAME | FNM_PERIOD, FNM_NOMATCH },
};
