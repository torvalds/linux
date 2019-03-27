/*-
 * Copyright 2014 Jonathan Anderson.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <atf-c.h>
#include <fcntl.h>
#include <stdio.h>


struct descriptors {
	int	binary;
	int	testdir;
	int	root;
	int	etc;
	int	usr;
};

static void	setup(struct descriptors *, const atf_tc_t *);
static void	expect_success(int binary, char *pathfds);
static void	expect_missing_library(int binary, char *pathfds);

static void	try_to_run(int binary, int expected_exit_status,
    char * const *env, const char *expected_out, const char *expected_err);
static int	opendir(const char *name);
static int	opendirat(int parent, const char *name);


ATF_TC_WITHOUT_HEAD(missing_library);
ATF_TC_BODY(missing_library, tc)
{
	struct descriptors files;

	setup(&files, tc);
	expect_missing_library(files.binary, NULL);
}


ATF_TC_WITHOUT_HEAD(wrong_library_directories);
ATF_TC_BODY(wrong_library_directories, tc)
{
	struct descriptors files;
	char *pathfds;

	setup(&files, tc);
	ATF_REQUIRE(
		asprintf(&pathfds, "LD_LIBRARY_PATH_FDS=%d", files.etc) > 0);

	expect_missing_library(files.binary, pathfds);
}


ATF_TC_WITHOUT_HEAD(bad_library_directories);
ATF_TC_BODY(bad_library_directories, tc)
{
	struct descriptors files;
	char *pathfds;

	setup(&files, tc);
	ATF_REQUIRE(asprintf(&pathfds, "LD_LIBRARY_PATH_FDS=::") > 0);

	expect_missing_library(files.binary, pathfds);
}


ATF_TC_WITHOUT_HEAD(single_library_directory);
ATF_TC_BODY(single_library_directory, tc)
{
	struct descriptors files;
	char *pathfds;

	setup(&files, tc);
	ATF_REQUIRE(
	    asprintf(&pathfds, "LD_LIBRARY_PATH_FDS=%d", files.testdir) > 0);

	expect_success(files.binary, pathfds);
}


ATF_TC_WITHOUT_HEAD(first_library_directory);
ATF_TC_BODY(first_library_directory, tc)
{
	struct descriptors files;
	char *pathfds;

	setup(&files, tc);
	ATF_REQUIRE(
	    asprintf(&pathfds, "LD_LIBRARY_PATH_FDS=%d:%d",
		files.testdir, files.etc) > 0);

	expect_success(files.binary, pathfds);
}


ATF_TC_WITHOUT_HEAD(middle_library_directory);
ATF_TC_BODY(middle_library_directory, tc)
{
	struct descriptors files;
	char *pathfds;

	setup(&files, tc);
	ATF_REQUIRE(
	    asprintf(&pathfds, "LD_LIBRARY_PATH_FDS=%d:%d:%d",
		files.root, files.testdir, files.usr) > 0);

	expect_success(files.binary, pathfds);
}


ATF_TC_WITHOUT_HEAD(last_library_directory);
ATF_TC_BODY(last_library_directory, tc)
{
	struct descriptors files;
	char *pathfds;

	setup(&files, tc);
	ATF_REQUIRE(
	    asprintf(&pathfds, "LD_LIBRARY_PATH_FDS=%d:%d",
		files.root, files.testdir) > 0);

	expect_success(files.binary, pathfds);
}



/* Register test cases with ATF. */
ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, missing_library);
	ATF_TP_ADD_TC(tp, wrong_library_directories);
	ATF_TP_ADD_TC(tp, bad_library_directories);
	ATF_TP_ADD_TC(tp, single_library_directory);
	ATF_TP_ADD_TC(tp, first_library_directory);
	ATF_TP_ADD_TC(tp, middle_library_directory);
	ATF_TP_ADD_TC(tp, last_library_directory);

	return atf_no_error();
}


static void
setup(struct descriptors *dp, const atf_tc_t *tc)
{

	dp->testdir = opendir(atf_tc_get_config_var(tc, "srcdir"));
	ATF_REQUIRE(dp->testdir >= 0);
	ATF_REQUIRE(
	    (dp->binary = openat(dp->testdir, "target", O_RDONLY)) >= 0);

	ATF_REQUIRE((dp->root = opendir("/")) >= 0);
	ATF_REQUIRE((dp->etc = opendirat(dp->root, "etc")) >= 0);
	ATF_REQUIRE((dp->usr = opendirat(dp->root, "usr")) >= 0);
}

static void
expect_success(int binary, char *pathfds)
{
	char * const env[] = { pathfds, NULL };
	try_to_run(binary, 0, env, "the hypotenuse of 3 and 4 is 5\n", "");
}

static void
expect_missing_library(int binary, char *pathfds)
{
	char * const env[] = { pathfds, NULL };
	try_to_run(binary, 1, env, "",
	   "ld-elf.so.1: Shared object \"libpythagoras.so.0\" not found,"
	    " required by \"target\"\n");
}


static void
try_to_run(int binary, int exit_status, char * const *env,
        const char *expected_out, const char *expected_err)
{
	pid_t child = atf_utils_fork();

	if (child == 0) {
		char * const args[] = { "target", NULL };

		fexecve(binary, args, env);
		atf_tc_fail("fexecve() failed");
	}

	atf_utils_wait(child, exit_status, expected_out, expected_err);
}


static int
opendir(const char *name)
{
	return open(name, O_RDONLY | O_DIRECTORY);
}

static int
opendirat(int parent, const char *name)
{
	return openat(parent, name, O_RDONLY | O_DIRECTORY);
}
