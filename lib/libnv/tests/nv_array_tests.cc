/*-
 * Copyright (c) 2015 Mariusz Zaborski <oshogbo@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/nv.h>
#include <sys/socket.h>

#include <atf-c++.hpp>

#include <cstdio>
#include <errno.h>
#include <fcntl.h>
#include <limits>
#include <set>
#include <sstream>
#include <string>

#define fd_is_valid(fd) (fcntl((fd), F_GETFL) != -1 || errno != EBADF)

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_bool_array__basic);
ATF_TEST_CASE_BODY(nvlist_bool_array__basic)
{
	bool testbool[16];
	const bool *const_result;
	bool *result;
	nvlist_t *nvl;
	size_t num_items;
	unsigned int i;
	const char *key;

	key = "nvl/bool";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	for (i = 0; i < 16; i++)
		testbool[i] = (i % 2 == 0);

	nvlist_add_bool_array(nvl, key, testbool, 16);
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_bool_array(nvl, key));
	ATF_REQUIRE(nvlist_exists_bool_array(nvl, "nvl/bool"));

	const_result = nvlist_get_bool_array(nvl, key, &num_items);
	ATF_REQUIRE_EQ(num_items, 16);
	ATF_REQUIRE(const_result != NULL);
	for (i = 0; i < num_items; i++)
		ATF_REQUIRE_EQ(const_result[i], testbool[i]);

	result = nvlist_take_bool_array(nvl, key, &num_items);
	ATF_REQUIRE_EQ(num_items, 16);
	ATF_REQUIRE(const_result != NULL);
	for (i = 0; i < num_items; i++)
		ATF_REQUIRE_EQ(result[i], testbool[i]);

	ATF_REQUIRE(!nvlist_exists_bool_array(nvl, key));
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);

	free(result);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_string_array__basic);
ATF_TEST_CASE_BODY(nvlist_string_array__basic)
{
	const char * const *const_result;
	char **result;
	nvlist_t *nvl;
	size_t num_items;
	unsigned int i;
	const char *key;
	const char *string_arr[8] = { "a", "b", "kot", "foo",
	    "tests", "nice test", "", "abcdef" };

	key = "nvl/string";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	nvlist_add_string_array(nvl, key, string_arr, nitems(string_arr));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_string_array(nvl, key));
	ATF_REQUIRE(nvlist_exists_string_array(nvl, "nvl/string"));

	const_result = nvlist_get_string_array(nvl, key, &num_items);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE(num_items == nitems(string_arr));
	for (i = 0; i < num_items; i++) {
		if (string_arr[i] != NULL) {
			ATF_REQUIRE(strcmp(const_result[i],
			    string_arr[i]) == 0);
		} else {
			ATF_REQUIRE(const_result[i] == string_arr[i]);
		}
	}

	result = nvlist_take_string_array(nvl, key, &num_items);
	ATF_REQUIRE(result != NULL);
	ATF_REQUIRE_EQ(num_items, nitems(string_arr));
	for (i = 0; i < num_items; i++) {
		if (string_arr[i] != NULL) {
			ATF_REQUIRE_EQ(strcmp(result[i], string_arr[i]), 0);
		} else {
			ATF_REQUIRE_EQ(result[i], string_arr[i]);
		}
	}

	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);

	for (i = 0; i < num_items; i++)
		free(result[i]);
	free(result);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_descriptor_array__basic);
ATF_TEST_CASE_BODY(nvlist_descriptor_array__basic)
{
	int fd[32], *result;
	const int *const_result;
	nvlist_t *nvl;
	size_t num_items;
	unsigned int i;
	const char *key;

	for (i = 0; i < nitems(fd); i++) {
		fd[i] = dup(STDERR_FILENO);
		ATF_REQUIRE(fd_is_valid(fd[i]));
	}

	key = "nvl/descriptor";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_descriptor_array(nvl, key));

	nvlist_add_descriptor_array(nvl, key, fd, nitems(fd));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_descriptor_array(nvl, key));
	ATF_REQUIRE(nvlist_exists_descriptor_array(nvl, "nvl/descriptor"));

	const_result = nvlist_get_descriptor_array(nvl, key, &num_items);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE(num_items == nitems(fd));
	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE(fd_is_valid(const_result[i]));
		if (i > 0)
			ATF_REQUIRE(const_result[i] != const_result[i - 1]);
	}

	result = nvlist_take_descriptor_array(nvl, key, &num_items);
	ATF_REQUIRE(result != NULL);
	ATF_REQUIRE_EQ(num_items, nitems(fd));
	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE(fd_is_valid(result[i]));
		if (i > 0)
			ATF_REQUIRE(const_result[i] != const_result[i - 1]);
	}

	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);

	for (i = 0; i < num_items; i++) {
		close(result[i]);
		close(fd[i]);
	}
	free(result);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_number_array__basic);
ATF_TEST_CASE_BODY(nvlist_number_array__basic)
{
	const uint64_t *const_result;
	uint64_t *result;
	nvlist_t *nvl;
	size_t num_items;
	unsigned int i;
	const char *key;
	const uint64_t number[8] = { 0, UINT_MAX, 7, 123, 90,
	    100000, 8, 1 };

	key = "nvl/number";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	nvlist_add_number_array(nvl, key, number, nitems(number));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_number_array(nvl, key));
	ATF_REQUIRE(nvlist_exists_number_array(nvl, "nvl/number"));

	const_result = nvlist_get_number_array(nvl, key, &num_items);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE(num_items == nitems(number));
	for (i = 0; i < num_items; i++)
		ATF_REQUIRE_EQ(const_result[i], number[i]);

	result = nvlist_take_number_array(nvl, key, &num_items);
	ATF_REQUIRE(result != NULL);
	ATF_REQUIRE_EQ(num_items, nitems(number));
	for (i = 0; i < num_items; i++)
		ATF_REQUIRE_EQ(result[i], number[i]);

	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);

	free(result);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_nvlist_array__basic);
ATF_TEST_CASE_BODY(nvlist_nvlist_array__basic)
{
	nvlist_t *testnvl[8];
	const nvlist_t * const *const_result;
	nvlist_t **result;
	nvlist_t *nvl;
	size_t num_items;
	unsigned int i;
	const char *somestr[8] = { "a", "b", "c", "d", "e", "f", "g", "h" };
	const char *key;

	for (i = 0; i < 8; i++) {
		testnvl[i] = nvlist_create(0);
		ATF_REQUIRE(testnvl[i] != NULL);
		ATF_REQUIRE_EQ(nvlist_error(testnvl[i]), 0);
		nvlist_add_string(testnvl[i], "nvl/string", somestr[i]);
		ATF_REQUIRE_EQ(nvlist_error(testnvl[i]), 0);
		ATF_REQUIRE(nvlist_exists_string(testnvl[i], "nvl/string"));
	}

	key = "nvl/nvlist";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	nvlist_add_nvlist_array(nvl, key, (const nvlist_t * const *)testnvl, 8);
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_nvlist_array(nvl, key));
	ATF_REQUIRE(nvlist_exists_nvlist_array(nvl, "nvl/nvlist"));

	const_result = nvlist_get_nvlist_array(nvl, key, &num_items);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE(num_items == nitems(testnvl));

	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE_EQ(nvlist_error(const_result[i]), 0);
		if (i < num_items - 1) {
			ATF_REQUIRE(nvlist_get_array_next(const_result[i]) ==
			    const_result[i + 1]);
		} else {
			ATF_REQUIRE(nvlist_get_array_next(const_result[i]) ==
			    NULL);
		}
		ATF_REQUIRE(nvlist_get_parent(const_result[i], NULL) == nvl);
		ATF_REQUIRE(nvlist_in_array(const_result[i]));
		ATF_REQUIRE(nvlist_exists_string(const_result[i],
		    "nvl/string"));
		ATF_REQUIRE(strcmp(nvlist_get_string(const_result[i],
		    "nvl/string"), somestr[i]) == 0);
	}

	result = nvlist_take_nvlist_array(nvl, key, &num_items);
	ATF_REQUIRE(result != NULL);
	ATF_REQUIRE_EQ(num_items, 8);
	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE_EQ(nvlist_error(result[i]), 0);
		ATF_REQUIRE(nvlist_get_array_next(result[i]) == NULL);
		ATF_REQUIRE(nvlist_get_parent(result[i], NULL) == NULL);
		ATF_REQUIRE(nvlist_get_array_next(const_result[i]) == NULL);
		ATF_REQUIRE(!nvlist_in_array(const_result[i]));
	}

	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);

	for (i = 0; i < 8; i++) {
		nvlist_destroy(result[i]);
		nvlist_destroy(testnvl[i]);
	}

	free(result);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_clone_array);
ATF_TEST_CASE_BODY(nvlist_clone_array)
{
	nvlist_t *testnvl[8];
	nvlist_t *src, *dst;
	const nvlist_t *nvl;
	bool testbool[16];
	int testfd[16];
	size_t i, num_items;
	const char *string_arr[8] = { "a", "b", "kot", "foo",
	    "tests", "nice test", "", "abcdef" };
	const char *somestr[8] = { "a", "b", "c", "d", "e", "f", "g", "h" };
	const uint64_t number[8] = { 0, UINT_MAX, 7, 123, 90,
	    100000, 8, 1 };

	for (i = 0; i < nitems(testfd); i++) {
		testbool[i] = (i % 2 == 0);
		testfd[i] = dup(STDERR_FILENO);
		ATF_REQUIRE(fd_is_valid(testfd[i]));
	}
	for (i = 0; i < nitems(testnvl); i++) {
		testnvl[i] = nvlist_create(0);
		ATF_REQUIRE(nvlist_error(testnvl[i]) == 0);
		nvlist_add_string(testnvl[i], "nvl/nvl/teststr", somestr[i]);
		ATF_REQUIRE(nvlist_error(testnvl[i]) == 0);
	}

	src = nvlist_create(0);
	ATF_REQUIRE(nvlist_error(src) == 0);

	ATF_REQUIRE(!nvlist_exists_bool_array(src, "nvl/bool"));
	nvlist_add_bool_array(src, "nvl/bool", testbool, nitems(testbool));
	ATF_REQUIRE_EQ(nvlist_error(src), 0);
	ATF_REQUIRE(nvlist_exists_bool_array(src, "nvl/bool"));

	ATF_REQUIRE(!nvlist_exists_string_array(src, "nvl/string"));
	nvlist_add_string_array(src, "nvl/string", string_arr,
	    nitems(string_arr));
	ATF_REQUIRE_EQ(nvlist_error(src), 0);
	ATF_REQUIRE(nvlist_exists_string_array(src, "nvl/string"));

	ATF_REQUIRE(!nvlist_exists_descriptor_array(src, "nvl/fd"));
	nvlist_add_descriptor_array(src, "nvl/fd", testfd, nitems(testfd));
	ATF_REQUIRE_EQ(nvlist_error(src), 0);
	ATF_REQUIRE(nvlist_exists_descriptor_array(src, "nvl/fd"));

	ATF_REQUIRE(!nvlist_exists_number_array(src, "nvl/number"));
	nvlist_add_number_array(src, "nvl/number", number,
	    nitems(number));
	ATF_REQUIRE_EQ(nvlist_error(src), 0);
	ATF_REQUIRE(nvlist_exists_number_array(src, "nvl/number"));

	ATF_REQUIRE(!nvlist_exists_nvlist_array(src, "nvl/array"));
	nvlist_add_nvlist_array(src, "nvl/array",
	    (const nvlist_t * const *)testnvl, nitems(testnvl));
	ATF_REQUIRE_EQ(nvlist_error(src), 0);
	ATF_REQUIRE(nvlist_exists_nvlist_array(src, "nvl/array"));

	dst = nvlist_clone(src);
	ATF_REQUIRE(dst != NULL);

	ATF_REQUIRE(nvlist_exists_bool_array(dst, "nvl/bool"));
	(void) nvlist_get_bool_array(dst, "nvl/bool", &num_items);
	ATF_REQUIRE_EQ(num_items, nitems(testbool));
	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE(
		    nvlist_get_bool_array(dst, "nvl/bool", &num_items)[i] ==
		    nvlist_get_bool_array(src, "nvl/bool", &num_items)[i]);
	}

	ATF_REQUIRE(nvlist_exists_string_array(dst, "nvl/string"));
	(void) nvlist_get_string_array(dst, "nvl/string", &num_items);
	ATF_REQUIRE_EQ(num_items, nitems(string_arr));
	for (i = 0; i < num_items; i++) {
		if (nvlist_get_string_array(dst, "nvl/string",
		    &num_items)[i] == NULL) {
			ATF_REQUIRE(nvlist_get_string_array(dst, "nvl/string",
			    &num_items)[i] == nvlist_get_string_array(src,
			    "nvl/string", &num_items)[i]);
		} else {
			ATF_REQUIRE(strcmp(nvlist_get_string_array(dst,
			    "nvl/string", &num_items)[i], nvlist_get_string_array(
			    src, "nvl/string", &num_items)[i]) == 0);
		}
	}

	ATF_REQUIRE(nvlist_exists_descriptor_array(dst, "nvl/fd"));
	(void) nvlist_get_descriptor_array(dst, "nvl/fd", &num_items);
	ATF_REQUIRE_EQ(num_items, nitems(testfd));
	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE(fd_is_valid(
		    nvlist_get_descriptor_array(dst, "nvl/fd", &num_items)[i]));
	}
	ATF_REQUIRE(nvlist_exists_number_array(dst, "nvl/number"));
	(void) nvlist_get_number_array(dst, "nvl/number", &num_items);
	ATF_REQUIRE_EQ(num_items, nitems(number));

	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE(
		    nvlist_get_number_array(dst, "nvl/number", &num_items)[i] ==
		    nvlist_get_number_array(src, "nvl/number", &num_items)[i]);
	}

	ATF_REQUIRE(nvlist_exists_nvlist_array(dst, "nvl/array"));
	(void) nvlist_get_nvlist_array(dst, "nvl/array", &num_items);
	ATF_REQUIRE_EQ(num_items, nitems(testnvl));
	for (i = 0; i < num_items; i++) {
		nvl = nvlist_get_nvlist_array(dst, "nvl/array", &num_items)[i];
		ATF_REQUIRE(nvlist_exists_string(nvl, "nvl/nvl/teststr"));
		ATF_REQUIRE(strcmp(nvlist_get_string(nvl, "nvl/nvl/teststr"),
		    somestr[i]) == 0);
	}

	for (i = 0; i < nitems(testfd); i++) {
		close(testfd[i]);
	}
	for (i = 0; i < nitems(testnvl); i++) {
		nvlist_destroy(testnvl[i]);
	}
	nvlist_destroy(src);
	nvlist_destroy(dst);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_bool_array__move);
ATF_TEST_CASE_BODY(nvlist_bool_array__move)
{
	bool *testbool;
	const bool *const_result;
	nvlist_t *nvl;
	size_t num_items, count;
	unsigned int i;
	const char *key;

	key = "nvl/bool";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	count = 16;
	testbool = (bool*)malloc(sizeof(*testbool) * count);
	ATF_REQUIRE(testbool != NULL);
	for (i = 0; i < count; i++)
		testbool[i] = (i % 2 == 0);

	nvlist_move_bool_array(nvl, key, testbool, count);
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_bool_array(nvl, key));

	const_result = nvlist_get_bool_array(nvl, key, &num_items);
	ATF_REQUIRE_EQ(num_items, count);
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE(const_result == testbool);
	for (i = 0; i < num_items; i++)
		ATF_REQUIRE_EQ(const_result[i], (i % 2 == 0));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_string_array__move);
ATF_TEST_CASE_BODY(nvlist_string_array__move)
{
	char **teststr;
	const char * const *const_result;
	nvlist_t *nvl;
	size_t num_items, count;
	unsigned int i;
	const char *key;

	key = "nvl/string";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	count = 26;
	teststr = (char**)malloc(sizeof(*teststr) * count);
	ATF_REQUIRE(teststr != NULL);
	for (i = 0; i < count; i++) {
		teststr[i] = (char*)malloc(sizeof(**teststr) * 2);
		ATF_REQUIRE(teststr[i] != NULL);
		teststr[i][0] = 'a' + i;
		teststr[i][1] = '\0';
	}

	nvlist_move_string_array(nvl, key, teststr, count);
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_string_array(nvl, key));

	const_result = nvlist_get_string_array(nvl, key, &num_items);
	ATF_REQUIRE_EQ(num_items, count);
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE((intptr_t)const_result == (intptr_t)teststr);
	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE_EQ(const_result[i][0], (char)('a' + i));
		ATF_REQUIRE_EQ(const_result[i][1], '\0');
	}

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_nvlist_array__move);
ATF_TEST_CASE_BODY(nvlist_nvlist_array__move)
{
	nvlist **testnv;
	const nvlist * const *const_result;
	nvlist_t *nvl;
	size_t num_items, count;
	unsigned int i;
	const char *key;

	key = "nvl/nvlist";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_nvlist_array(nvl, key));

	count = 26;
	testnv = (nvlist**)malloc(sizeof(*testnv) * count);
	ATF_REQUIRE(testnv != NULL);
	for (i = 0; i < count; i++) {
		testnv[i] = nvlist_create(0);
		ATF_REQUIRE(testnv[i] != NULL);
	}

	nvlist_move_nvlist_array(nvl, key, testnv, count);
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_nvlist_array(nvl, key));

	const_result = nvlist_get_nvlist_array(nvl, key, &num_items);
	ATF_REQUIRE_EQ(num_items, count);
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE((intptr_t)const_result == (intptr_t)testnv);
	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE_EQ(nvlist_error(const_result[i]), 0);
		ATF_REQUIRE(nvlist_empty(const_result[i]));
		if (i < num_items - 1) {
			ATF_REQUIRE(nvlist_get_array_next(const_result[i]) ==
			    const_result[i + 1]);
		} else {
			ATF_REQUIRE(nvlist_get_array_next(const_result[i]) ==
			    NULL);
		}
		ATF_REQUIRE(nvlist_get_parent(const_result[i], NULL) == nvl);
		ATF_REQUIRE(nvlist_in_array(const_result[i]));
	}

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_number_array__move);
ATF_TEST_CASE_BODY(nvlist_number_array__move)
{
	uint64_t *testnumber;
	const uint64_t *const_result;
	nvlist_t *nvl;
	size_t num_items, count;
	unsigned int i;
	const char *key;

	key = "nvl/number";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	count = 1000;
	testnumber = (uint64_t*)malloc(sizeof(*testnumber) * count);
	ATF_REQUIRE(testnumber != NULL);
	for (i = 0; i < count; i++)
		testnumber[i] = i;

	nvlist_move_number_array(nvl, key, testnumber, count);
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_number_array(nvl, key));

	const_result = nvlist_get_number_array(nvl, key, &num_items);
	ATF_REQUIRE_EQ(num_items, count);
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE(const_result == testnumber);
	for (i = 0; i < num_items; i++)
		ATF_REQUIRE_EQ(const_result[i], i);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_descriptor_array__move);
ATF_TEST_CASE_BODY(nvlist_descriptor_array__move)
{
	int *testfd;
	const int *const_result;
	nvlist_t *nvl;
	size_t num_items, count;
	unsigned int i;
	const char *key;

	key = "nvl/fd";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	count = 50;
	testfd = (int*)malloc(sizeof(*testfd) * count);
	ATF_REQUIRE(testfd != NULL);
	for (i = 0; i < count; i++) {
		testfd[i] = dup(STDERR_FILENO);
		ATF_REQUIRE(fd_is_valid(testfd[i]));
	}

	nvlist_move_descriptor_array(nvl, key, testfd, count);
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_descriptor_array(nvl, key));

	const_result = nvlist_get_descriptor_array(nvl, key, &num_items);
	ATF_REQUIRE_EQ(num_items, count);
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE(const_result == testfd);
	for (i = 0; i < num_items; i++)
		ATF_REQUIRE(fd_is_valid(const_result[i]));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_arrays__error_null);
ATF_TEST_CASE_BODY(nvlist_arrays__error_null)
{
	nvlist_t *nvl;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_add_number_array(nvl, "nvl/number", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_move_number_array(nvl, "nvl/number", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_add_descriptor_array(nvl, "nvl/fd", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_move_descriptor_array(nvl, "nvl/fd", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_add_string_array(nvl, "nvl/string", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_move_string_array(nvl, "nvl/string", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_add_nvlist_array(nvl, "nvl/nvlist", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_move_nvlist_array(nvl, "nvl/nvlist", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_add_bool_array(nvl, "nvl/bool", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_move_bool_array(nvl, "nvl/bool", NULL, 0);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_arrays__bad_value);
ATF_TEST_CASE_BODY(nvlist_arrays__bad_value)
{
	nvlist_t *nvl, *nvladd[1], **nvlmove;
	int fdadd[1], *fdmove;

	nvladd[0] = NULL;
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_add_nvlist_array(nvl, "nvl/nvlist", nvladd, 1);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	nvlmove = (nvlist_t**)malloc(sizeof(*nvlmove));
	ATF_REQUIRE(nvlmove != NULL);
	nvlmove[0] = NULL;
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_move_nvlist_array(nvl, "nvl/nvlist", nvlmove, 1);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	fdadd[0] = -2;
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_add_descriptor_array(nvl, "nvl/fd", fdadd, 1);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);

	fdmove = (int*)malloc(sizeof(*fdmove));
	ATF_REQUIRE(fdmove != NULL);
	fdmove[0] = -2;
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_move_descriptor_array(nvl, "nvl/fd", fdmove, 1);
	ATF_REQUIRE(nvlist_error(nvl) != 0);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_nvlist_array__travel);
ATF_TEST_CASE_BODY(nvlist_nvlist_array__travel)
{
	nvlist_t *nvl, *test[5], *nasted;
	const nvlist_t *travel;
	const char *name;
	void *cookie;
	int type;
	unsigned int i, index;

	for (i = 0; i < nitems(test); i++) {
		test[i] = nvlist_create(0);
		ATF_REQUIRE(test[i] != NULL);
		nvlist_add_number(test[i], "nvl/number", i);
		ATF_REQUIRE(nvlist_error(test[i]) == 0);
	}
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_add_nvlist_array(nvl, "nvl/nvlist_array", test, nitems(test));
	ATF_REQUIRE(nvlist_error(nvl) == 0);
	nasted = nvlist_create(0);
	ATF_REQUIRE(nasted != NULL);
	nvlist_add_nvlist_array(nasted, "nvl/nvl/nvlist_array", test,
	    nitems(test));
	ATF_REQUIRE(nvlist_error(nasted) == 0);
	nvlist_move_nvlist(nvl, "nvl/nvl", nasted);
	ATF_REQUIRE(nvlist_error(nvl) == 0);
	nvlist_add_string(nvl, "nvl/string", "END");
	ATF_REQUIRE(nvlist_error(nvl) == 0);

	cookie = NULL;
	index = 0;
	travel = nvl;
	do {
		while ((name = nvlist_next(travel, &type, &cookie)) != NULL) {
			if (index == 0) {
				ATF_REQUIRE(type == NV_TYPE_NVLIST_ARRAY);
			} else if (index >= 1 && index <= nitems(test)) {
				ATF_REQUIRE(type == NV_TYPE_NUMBER);
			} else if (index == nitems(test) + 1) {
				ATF_REQUIRE(type == NV_TYPE_NVLIST);
			} else if (index == nitems(test) + 2) {
				ATF_REQUIRE(type == NV_TYPE_NVLIST_ARRAY);
			} else if (index >= nitems(test) + 3 &&
				   index <= 2 * nitems(test) + 2) {
				ATF_REQUIRE(type == NV_TYPE_NUMBER);
			} else if (index == 2 * nitems(test) + 3) {
				ATF_REQUIRE(type == NV_TYPE_STRING);
			}

			if (type == NV_TYPE_NVLIST) {
				travel = nvlist_get_nvlist(travel, name);
				cookie = NULL;
			} else if (type == NV_TYPE_NVLIST_ARRAY) {
				travel = nvlist_get_nvlist_array(travel, name,
				    NULL)[0];
				cookie = NULL;
			}
			index ++;
		}
	} while ((travel = nvlist_get_pararr(travel, &cookie)) != NULL);

	for (i = 0; i < nitems(test); i++)
		nvlist_destroy(test[i]);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_nvlist_array__travel_alternative);
ATF_TEST_CASE_BODY(nvlist_nvlist_array__travel_alternative)
{
	nvlist_t *nvl, *test[5], *nasted;
	const nvlist_t *travel, *tmp;
	void *cookie;
	int index, i, type;
	const char *name;

	for (i = 0; i < 5; i++) {
		test[i] = nvlist_create(0);
		ATF_REQUIRE(test[i] != NULL);
		nvlist_add_number(test[i], "nvl/number", i);
		ATF_REQUIRE(nvlist_error(test[i]) == 0);
	}
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	nvlist_add_nvlist_array(nvl, "nvl/nvlist_array", test, 5);
	ATF_REQUIRE(nvlist_error(nvl) == 0);
	nasted = nvlist_create(0);
	ATF_REQUIRE(nasted != NULL);
	nvlist_add_nvlist_array(nasted, "nvl/nvl/nvlist_array", test, 5);
	ATF_REQUIRE(nvlist_error(nasted) == 0);
	nvlist_move_nvlist(nvl, "nvl/nvl", nasted);
	ATF_REQUIRE(nvlist_error(nvl) == 0);
	nvlist_add_string(nvl, "nvl/string", "END");
	ATF_REQUIRE(nvlist_error(nvl) == 0);

	cookie = NULL;
	index = 0;
	tmp = travel = nvl;
	do {
		do {
			travel = tmp;
			while ((name = nvlist_next(travel, &type, &cookie)) !=
			    NULL) {
				if (index == 0) {
					ATF_REQUIRE(type ==
					    NV_TYPE_NVLIST_ARRAY);
				} else if (index >= 1 && index <= 5) {
					ATF_REQUIRE(type == NV_TYPE_NUMBER);
				} else if (index == 6) {
					ATF_REQUIRE(type == NV_TYPE_NVLIST);
				} else if (index == 7) {
					ATF_REQUIRE(type ==
					    NV_TYPE_NVLIST_ARRAY);
				} else if (index >= 8 && index <= 12) {
					ATF_REQUIRE(type == NV_TYPE_NUMBER);
				} else if (index == 13) {
					ATF_REQUIRE(type == NV_TYPE_STRING);
				}

				if (type == NV_TYPE_NVLIST) {
					travel = nvlist_get_nvlist(travel,
					    name);
					cookie = NULL;
				} else if (type == NV_TYPE_NVLIST_ARRAY) {
					travel = nvlist_get_nvlist_array(travel,
					    name, NULL)[0];
					cookie = NULL;
				}
				index ++;
			}
			cookie = NULL;
		} while ((tmp = nvlist_get_array_next(travel)) != NULL);
	} while ((tmp = nvlist_get_parent(travel, &cookie)) != NULL);

	for (i = 0; i < 5; i++)
		nvlist_destroy(test[i]);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_bool_array__pack);
ATF_TEST_CASE_BODY(nvlist_bool_array__pack)
{
	nvlist_t *nvl, *unpacked;
	const char *key;
	size_t packed_size, count;
	void *packed;
	unsigned int i;
	const bool *const_result;
	bool testbool[16];

	for (i = 0; i < nitems(testbool); i++)
		testbool[i] = (i % 2 == 0);

	key = "nvl/bool";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	nvlist_add_bool_array(nvl, key, testbool, nitems(testbool));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_bool_array(nvl, key));

	packed = nvlist_pack(nvl, &packed_size);
	ATF_REQUIRE(packed != NULL);

	unpacked = nvlist_unpack(packed, packed_size, 0);
	ATF_REQUIRE(unpacked != NULL);
	ATF_REQUIRE_EQ(nvlist_error(unpacked), 0);
	ATF_REQUIRE(nvlist_exists_bool_array(unpacked, key));

	const_result = nvlist_get_bool_array(unpacked, key, &count);
	ATF_REQUIRE_EQ(count, nitems(testbool));
	for (i = 0; i < count; i++) {
		ATF_REQUIRE_EQ(testbool[i], const_result[i]);
	}

	nvlist_destroy(nvl);
	nvlist_destroy(unpacked);
	free(packed);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_number_array__pack);
ATF_TEST_CASE_BODY(nvlist_number_array__pack)
{
	nvlist_t *nvl, *unpacked;
	const char *key;
	size_t packed_size, count;
	void *packed;
	unsigned int i;
	const uint64_t *const_result;
	const uint64_t number[8] = { 0, UINT_MAX, 7, 123, 90,
	    100000, 8, 1 };

	key = "nvl/number";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	nvlist_add_number_array(nvl, key, number, 8);
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_number_array(nvl, key));

	packed = nvlist_pack(nvl, &packed_size);
	ATF_REQUIRE(packed != NULL);

	unpacked = nvlist_unpack(packed, packed_size, 0);
	ATF_REQUIRE(unpacked != NULL);
	ATF_REQUIRE_EQ(nvlist_error(unpacked), 0);
	ATF_REQUIRE(nvlist_exists_number_array(unpacked, key));

	const_result = nvlist_get_number_array(unpacked, key, &count);
	ATF_REQUIRE_EQ(count, nitems(number));
	for (i = 0; i < count; i++) {
		ATF_REQUIRE_EQ(number[i], const_result[i]);
	}

	nvlist_destroy(nvl);
	nvlist_destroy(unpacked);
	free(packed);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_descriptor_array__pack);
ATF_TEST_CASE_BODY(nvlist_descriptor_array__pack)
{
	nvlist_t *nvl;
	const char *key;
	size_t num_items;
	unsigned int i;
	const int *const_result;
	int desc[32], fd, socks[2];
	pid_t pid;

	key = "nvl/descriptor";

	ATF_REQUIRE_EQ(socketpair(PF_UNIX, SOCK_STREAM, 0, socks), 0);

	pid = atf::utils::fork();
	ATF_REQUIRE(pid >= 0);
	if (pid == 0) {
		/* Child. */
		fd = socks[0];
		close(socks[1]);
		for (i = 0; i < nitems(desc); i++) {
			desc[i] = dup(STDERR_FILENO);
			ATF_REQUIRE(fd_is_valid(desc[i]));
		}

		nvl = nvlist_create(0);
		ATF_REQUIRE(nvl != NULL);
		ATF_REQUIRE(nvlist_empty(nvl));
		ATF_REQUIRE(!nvlist_exists_descriptor_array(nvl, key));

		nvlist_add_descriptor_array(nvl, key, desc, nitems(desc));
		ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
		ATF_REQUIRE(!nvlist_empty(nvl));
		ATF_REQUIRE(nvlist_exists_descriptor_array(nvl, key));

		ATF_REQUIRE(nvlist_send(fd, nvl) >= 0);

		for (i = 0; i < nitems(desc); i++)
			close(desc[i]);
	} else {
		/* Parent */
		fd = socks[1];
		close(socks[0]);

		errno = 0;
		nvl = nvlist_recv(fd, 0);
		ATF_REQUIRE(nvl != NULL);
		ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
		ATF_REQUIRE(nvlist_exists_descriptor_array(nvl, key));

		const_result = nvlist_get_descriptor_array(nvl, key, &num_items);
		ATF_REQUIRE(const_result != NULL);
		ATF_REQUIRE_EQ(num_items, nitems(desc));
		for (i = 0; i < num_items; i++)
			ATF_REQUIRE(fd_is_valid(const_result[i]));

		atf::utils::wait(pid, 0, "", "");
	}

	nvlist_destroy(nvl);
	close(fd);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_string_array__pack);
ATF_TEST_CASE_BODY(nvlist_string_array__pack)
{
	nvlist_t *nvl, *unpacked;
	const char *key;
	size_t packed_size, count;
	void *packed;
	unsigned int i;
	const char * const *const_result;
	const char *string_arr[8] = { "a", "b", "kot", "foo",
	    "tests", "nice test", "", "abcdef" };

	key = "nvl/string";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	nvlist_add_string_array(nvl, key, string_arr, nitems(string_arr));
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_string_array(nvl, key));

	packed = nvlist_pack(nvl, &packed_size);
	ATF_REQUIRE(packed != NULL);

	unpacked = nvlist_unpack(packed, packed_size, 0);
	ATF_REQUIRE(unpacked != NULL);
	ATF_REQUIRE_EQ(nvlist_error(unpacked), 0);
	ATF_REQUIRE(nvlist_exists_string_array(unpacked, key));

	const_result = nvlist_get_string_array(unpacked, key, &count);
	ATF_REQUIRE_EQ(count, nitems(string_arr));
	for (i = 0; i < count; i++) {
		ATF_REQUIRE_EQ(strcmp(string_arr[i], const_result[i]), 0);
	}

	nvlist_destroy(nvl);
	nvlist_destroy(unpacked);
	free(packed);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_nvlist_array__pack);
ATF_TEST_CASE_BODY(nvlist_nvlist_array__pack)
{
	nvlist_t *testnvl[8], *unpacked;
	const nvlist_t * const *const_result;
	nvlist_t *nvl;
	size_t num_items, packed_size;
	unsigned int i;
	void *packed;
	const char *somestr[8] = { "a", "b", "c", "d", "e", "f", "g", "h" };
	const char *key;

	for (i = 0; i < nitems(testnvl); i++) {
		testnvl[i] = nvlist_create(0);
		ATF_REQUIRE(testnvl[i] != NULL);
		ATF_REQUIRE_EQ(nvlist_error(testnvl[i]), 0);
		nvlist_add_string(testnvl[i], "nvl/string", somestr[i]);
		ATF_REQUIRE_EQ(nvlist_error(testnvl[i]), 0);
		ATF_REQUIRE(nvlist_exists_string(testnvl[i], "nvl/string"));
	}

	key = "nvl/nvlist";
	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(nvlist_empty(nvl));
	ATF_REQUIRE(!nvlist_exists_string_array(nvl, key));

	nvlist_add_nvlist_array(nvl, key, (const nvlist_t * const *)testnvl, 8);
	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_nvlist_array(nvl, key));
	ATF_REQUIRE(nvlist_exists_nvlist_array(nvl, "nvl/nvlist"));
	packed = nvlist_pack(nvl, &packed_size);
	ATF_REQUIRE(packed != NULL);

	unpacked = nvlist_unpack(packed, packed_size, 0);
	ATF_REQUIRE(unpacked != NULL);
	ATF_REQUIRE_EQ(nvlist_error(unpacked), 0);
	ATF_REQUIRE(nvlist_exists_nvlist_array(unpacked, key));

	const_result = nvlist_get_nvlist_array(unpacked, key, &num_items);
	ATF_REQUIRE(const_result != NULL);
	ATF_REQUIRE_EQ(num_items, nitems(testnvl));
	for (i = 0; i < num_items; i++) {
		ATF_REQUIRE_EQ(nvlist_error(const_result[i]), 0);
		if (i < num_items - 1) {
			ATF_REQUIRE(nvlist_get_array_next(const_result[i]) ==
			    const_result[i + 1]);
		} else {
			ATF_REQUIRE(nvlist_get_array_next(const_result[i]) ==
			    NULL);
		}
		ATF_REQUIRE(nvlist_get_parent(const_result[i], NULL) == unpacked);
		ATF_REQUIRE(nvlist_in_array(const_result[i]));
		ATF_REQUIRE(nvlist_exists_string(const_result[i],
		    "nvl/string"));
		ATF_REQUIRE(strcmp(nvlist_get_string(const_result[i],
		    "nvl/string"), somestr[i]) == 0);
	}

	for (i = 0; i < nitems(testnvl); i++)
		nvlist_destroy(testnvl[i]);
	nvlist_destroy(nvl);
	nvlist_destroy(unpacked);
	free(packed);
}

ATF_INIT_TEST_CASES(tp)
{

	ATF_ADD_TEST_CASE(tp, nvlist_bool_array__basic);
	ATF_ADD_TEST_CASE(tp, nvlist_string_array__basic);
	ATF_ADD_TEST_CASE(tp, nvlist_descriptor_array__basic);
	ATF_ADD_TEST_CASE(tp, nvlist_number_array__basic);
	ATF_ADD_TEST_CASE(tp, nvlist_nvlist_array__basic)

	ATF_ADD_TEST_CASE(tp, nvlist_clone_array)

	ATF_ADD_TEST_CASE(tp, nvlist_bool_array__move);
	ATF_ADD_TEST_CASE(tp, nvlist_string_array__move);
	ATF_ADD_TEST_CASE(tp, nvlist_nvlist_array__move);
	ATF_ADD_TEST_CASE(tp, nvlist_number_array__move);
	ATF_ADD_TEST_CASE(tp, nvlist_descriptor_array__move);

	ATF_ADD_TEST_CASE(tp, nvlist_arrays__error_null);

	ATF_ADD_TEST_CASE(tp, nvlist_arrays__bad_value)

	ATF_ADD_TEST_CASE(tp, nvlist_nvlist_array__travel)
	ATF_ADD_TEST_CASE(tp, nvlist_nvlist_array__travel_alternative)

	ATF_ADD_TEST_CASE(tp, nvlist_bool_array__pack)
	ATF_ADD_TEST_CASE(tp, nvlist_number_array__pack)
	ATF_ADD_TEST_CASE(tp, nvlist_descriptor_array__pack)
	ATF_ADD_TEST_CASE(tp, nvlist_string_array__pack)
	ATF_ADD_TEST_CASE(tp, nvlist_nvlist_array__pack)
}

