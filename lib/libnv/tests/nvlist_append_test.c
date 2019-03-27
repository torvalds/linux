/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/nv.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int ntest = 1;

#define	CHECK(expr)	do {						\
	if ((expr))							\
		printf("ok # %d %s:%u\n", ntest, __FILE__, __LINE__);	\
	else								\
		printf("not ok # %d %s:%u\n", ntest, __FILE__, __LINE__);\
	ntest++;							\
} while (0)

int
main(void)
{
	const bool *bool_result;
	const char * const *string_result;
	const nvlist_t * const *nvl_result;
	nvlist_t *nvl, *nvl1, *nvl2, **items;
	unsigned int i;
	size_t nitems;

	printf("1..32\n");

	nvl = nvlist_create(0);

	for (i = 0; i < 16; i++)
		nvlist_append_bool_array(nvl, "nvl/bool", i % 2 == 0);

	CHECK(nvlist_error(nvl) == 0);
	CHECK(!nvlist_empty(nvl));
	CHECK(nvlist_exists_bool_array(nvl, "nvl/bool"));

	bool_result = nvlist_get_bool_array(nvl, "nvl/bool", &nitems);
	CHECK(nitems == 16);
	CHECK(bool_result != NULL);
	for (i = 0; i < nitems; i++)
		CHECK(bool_result[i] == (i % 2 == 0));


	nvlist_append_string_array(nvl, "nvl/string", "a");
	nvlist_append_string_array(nvl, "nvl/string", "abc");
	string_result = nvlist_get_string_array(nvl, "nvl/string", &nitems);
	CHECK(nitems == 2);
	CHECK(strcmp(string_result[0], "a") == 0);
	CHECK(strcmp(string_result[1], "abc") == 0);


	nvl1 = nvlist_create(0);
	nvlist_add_string(nvl1, "key1", "test1");
	nvlist_append_nvlist_array(nvl, "nvl/nvl", nvl1);
	nvlist_destroy(nvl1);

	nvl2 = nvlist_create(0);
	nvlist_add_string(nvl2, "key2", "test2");
	nvlist_append_nvlist_array(nvl, "nvl/nvl", nvl2);
	nvlist_destroy(nvl2);

	nvl_result = nvlist_get_nvlist_array(nvl, "nvl/nvl", &nitems);
	CHECK(nitems == 2);
	CHECK(strcmp(nvlist_get_string(nvl_result[0], "key1"), "test1") == 0);
	CHECK(strcmp(nvlist_get_string(nvl_result[1], "key2"), "test2") == 0);

	nvl1 = nvlist_create(0);
	nvlist_add_number(nvl1, "key1", 10);
	nvlist_append_nvlist_array(nvl, "nvl/nvl_array", nvl1);
	nvlist_destroy(nvl1);

	nvl2 = nvlist_create(0);
	nvlist_add_number(nvl2, "key1", 20);
	nvlist_append_nvlist_array(nvl, "nvl/nvl_array", nvl2);
	nvlist_destroy(nvl2);

	items = nvlist_take_nvlist_array(nvl, "nvl/nvl_array", &nitems);
	CHECK(nvlist_get_number(items[0], "key1") == 10);
	CHECK(nvlist_get_number(items[1], "key1") == 20);
	CHECK(nvlist_error(items[0]) == 0);
	CHECK(nvlist_error(items[1]) == 0);

	nvlist_move_nvlist_array(nvl, "nvl/nvl_new_array", items, nitems);
	CHECK(nvlist_error(nvl) == 0);

	nvlist_destroy(nvl);

	return (0);
}
