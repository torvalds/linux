// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <linux/kernel.h>
#include "debug.h"
#include "symbol.h"
#include "tests.h"

static int test__demangle_java(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int ret = TEST_OK;
	char *buf = NULL;
	size_t i;

	struct {
		const char *mangled, *demangled;
	} test_cases[] = {
		{ "Ljava/lang/StringLatin1;equals([B[B)Z",
		  "java.lang.StringLatin1.equals(byte[], byte[])" },
		{ "Ljava/util/zip/ZipUtils;CENSIZ([BI)J",
		  "java.util.zip.ZipUtils.CENSIZ(byte[], int)" },
		{ "Ljava/util/regex/Pattern$BmpCharProperty;match(Ljava/util/regex/Matcher;ILjava/lang/CharSequence;)Z",
		  "java.util.regex.Pattern$BmpCharProperty.match(java.util.regex.Matcher, int, java.lang.CharSequence)" },
		{ "Ljava/lang/AbstractStringBuilder;appendChars(Ljava/lang/String;II)V",
		  "java.lang.AbstractStringBuilder.appendChars(java.lang.String, int, int)" },
		{ "Ljava/lang/Object;<init>()V",
		  "java.lang.Object<init>()" },
	};

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		buf = dso__demangle_sym(/*dso=*/NULL, /*kmodule=*/0, test_cases[i].mangled);
		if (!buf) {
			pr_debug("FAILED to demangle: \"%s\"\n \"%s\"\n", test_cases[i].mangled,
				 test_cases[i].demangled);
			continue;
		}
		if (strcmp(buf, test_cases[i].demangled)) {
			pr_debug("FAILED: %s: %s != %s\n", test_cases[i].mangled,
				 buf, test_cases[i].demangled);
			ret = TEST_FAIL;
		}
		free(buf);
	}

	return ret;
}

DEFINE_SUITE("Demangle Java", demangle_java);
