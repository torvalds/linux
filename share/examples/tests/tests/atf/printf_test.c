/* $FreeBSD$
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2013 Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of Google Inc. nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

/*
 * INTRODUCTION
 *
 * This sample test program implements various test cases for the printf(3)
 * family of functions in order to demonstrate the usage of the ATF C API
 * (see atf-c-api(3)).
 *
 * Note that this test program is called printf_test because it is intended
 * to validate various functions of the printf(3) family.  For this reason,
 * each test is prefixed with the name of the function under test followed
 * by a description of the specific condition being validated.  You should
 * use a similar naming scheme for your own tests.
 */

#include <atf-c.h>
#include <stdio.h>
#include <string.h>

/*
 * This is the simplest form of a test case definition: a test case
 * without a header.
 *
 * In most cases, this is the definition you will want to use.  However,
 * make absolutely sure that the test case name is descriptive enough.
 * Multi-word test case names are encouraged.  Keep in mind that these
 * are exposed to the reader in the test reports, and the goal is for
 * the combination of the test program plus the name of the test case to
 * give a pretty clear idea of what specific condition the test is
 * validating.
 */
ATF_TC_WITHOUT_HEAD(snprintf__two_formatters);
ATF_TC_BODY(snprintf__two_formatters, tc)
{
	char buffer[128];

	/* This first require-style check invokes the function we are
	 * interested in testing.  This will cause the test to fail if
	 * the condition provided to ATF_REQUIRE is not met. */
	ATF_REQUIRE(snprintf(buffer, sizeof(buffer), "%s, %s!",
	    "Hello", "tests") > 0);

	/* This second check-style check compares that the result of the
	 * snprintf call we performed above is correct.  We use a check
	 * instead of a require. */
	ATF_CHECK_STREQ("Hello, tests!", buffer);
}

/*
 * This is a more complex form of a test case definition: a test case
 * with a header and a body.  You should always favor the simpler
 * definition above unless you have to override specific metadata
 * variables.
 *
 * See atf-test-case(4) and kyua-atf-interface(1) for details on all
 * available properties.
 */
ATF_TC(snprintf__overflow);
ATF_TC_HEAD(snprintf__overflow, tc)
{
	/* In this specific case, we define a textual description for
	 * the test case, which is later exported to the reports for
	 * documentation purposes.
	 *
	 * However, note again that you should favor highly descriptive
	 * test case names to textual descriptions.  */
	atf_tc_set_md_var(tc, "descr", "This test case validates the proper "
	    "truncation of the output string from snprintf when it does not "
	    "fit the provided buffer.");
}
ATF_TC_BODY(snprintf__overflow, tc)
{
	char buffer[10];

	/* This is a similar test to the above, but in this case we do the
	 * test ourselves and forego the ATF_* macros.  Note that we use the
	 * atf_tc_fail() function instead of exit(2) or similar because we
	 * want Kyua to have access to the failure message.
	 *
	 * In general, prefer using the ATF_* macros wherever possible.  Only
	 * resort to manual tests when the macros are unsuitable (and consider
	 * filing a feature request to get a new macro if you think your case
	 * is generic enough). */
	if (snprintf(buffer, sizeof(buffer), "0123456789abcdef") != 16)
		atf_tc_fail("snprintf did not return the expected number "
		    "of characters");

	ATF_CHECK(strcmp(buffer, "012345678") == 0);
}

/*
 * Another simple test case, but this time with side-effects.  This
 * particular test case modifies the contents of the current directory
 * and does not clean up after itself, which is perfectly fine.
 */
ATF_TC_WITHOUT_HEAD(fprintf__simple_string);
ATF_TC_BODY(fprintf__simple_string, tc)
{
	const char *contents = "This is a message\n";

	FILE *output = fopen("test.txt", "w");
	ATF_REQUIRE(fprintf(output, "%s", contents) > 0);
	fclose(output);

	/* The ATF C library provides more than just macros to verify the
	 * outcome of expressions.  It also includes various helper functions
	 * to work with files and processes.  Here is just a simple
	 * example. */
	ATF_REQUIRE(atf_utils_compare_file("test.txt", contents));

	/* Of special note here is that we are NOT deleting the
	 * temporary files we created in this test.  Kyua takes care of
	 * this cleanup automatically and tests can (and should) rely on
	 * this behavior. */
}

/*
 * Lastly, we tell ATF which test cases exist in this program.  This
 * function should not do anything other than this registration.
 */
ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, snprintf__two_formatters);
	ATF_TP_ADD_TC(tp, snprintf__overflow);
	ATF_TP_ADD_TC(tp, fprintf__simple_string);

	return (atf_no_error());
}
