// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <linux/kernel.h>
#include "tests.h"
#include "session.h"
#include "debug.h"
#include "demangle-java.h"

static int test__demangle_java(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int ret = TEST_OK;
	char *buf = NULL;
	size_t i;

	struct {
		const char *mangled, *demangled;
	} test_cases[] = {
		{ "Ljava/lang/StringLatin1;equals([B[B)Z",
		  "boolean java.lang.StringLatin1.equals(byte[], byte[])" },
		{ "Ljava/util/zip/ZipUtils;CENSIZ([BI)J",
		  "long java.util.zip.ZipUtils.CENSIZ(byte[], int)" },
		{ "Ljava/util/regex/Pattern$BmpCharProperty;match(Ljava/util/regex/Matcher;ILjava/lang/CharSequence;)Z",
		  "boolean java.util.regex.Pattern$BmpCharProperty.match(java.util.regex.Matcher, int, java.lang.CharSequence)" },
		{ "Ljava/lang/AbstractStringBuilder;appendChars(Ljava/lang/String;II)V",
		  "void java.lang.AbstractStringBuilder.appendChars(java.lang.String, int, int)" },
		{ "Ljava/lang/Object;<init>()V",
		  "void java.lang.Object<init>()" },
	};

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		buf = java_demangle_sym(test_cases[i].mangled, 0);
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
