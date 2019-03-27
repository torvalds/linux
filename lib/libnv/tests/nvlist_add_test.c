/*-
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/nv.h>

#include <errno.h>
#include <stdio.h>
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
	const nvlist_t *cnvl;
	nvlist_t *nvl;

	printf("1..94\n");

	nvl = nvlist_create(0);

	CHECK(!nvlist_exists_null(nvl, "nvlist/null"));
	nvlist_add_null(nvl, "nvlist/null");
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_null(nvl, "nvlist/null"));

	CHECK(!nvlist_exists_bool(nvl, "nvlist/bool/true"));
	nvlist_add_bool(nvl, "nvlist/bool/true", true);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_bool(nvl, "nvlist/bool/true"));

	CHECK(!nvlist_exists_bool(nvl, "nvlist/bool/false"));
	nvlist_add_bool(nvl, "nvlist/bool/false", false);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_bool(nvl, "nvlist/bool/false"));

	CHECK(!nvlist_exists_number(nvl, "nvlist/number/0"));
	nvlist_add_number(nvl, "nvlist/number/0", 0);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_number(nvl, "nvlist/number/0"));

	CHECK(!nvlist_exists_number(nvl, "nvlist/number/1"));
	nvlist_add_number(nvl, "nvlist/number/1", 1);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_number(nvl, "nvlist/number/1"));

	CHECK(!nvlist_exists_number(nvl, "nvlist/number/-1"));
	nvlist_add_number(nvl, "nvlist/number/-1", -1);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_number(nvl, "nvlist/number/-1"));

	CHECK(!nvlist_exists_number(nvl, "nvlist/number/UINT64_MAX"));
	nvlist_add_number(nvl, "nvlist/number/UINT64_MAX", UINT64_MAX);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_number(nvl, "nvlist/number/UINT64_MAX"));

	CHECK(!nvlist_exists_number(nvl, "nvlist/number/INT64_MIN"));
	nvlist_add_number(nvl, "nvlist/number/INT64_MIN", INT64_MIN);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_number(nvl, "nvlist/number/INT64_MIN"));

	CHECK(!nvlist_exists_number(nvl, "nvlist/number/INT64_MAX"));
	nvlist_add_number(nvl, "nvlist/number/INT64_MAX", INT64_MAX);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_number(nvl, "nvlist/number/INT64_MAX"));

	CHECK(!nvlist_exists_string(nvl, "nvlist/string/"));
	nvlist_add_string(nvl, "nvlist/string/", "");
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_string(nvl, "nvlist/string/"));

	CHECK(!nvlist_exists_string(nvl, "nvlist/string/x"));
	nvlist_add_string(nvl, "nvlist/string/x", "x");
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_string(nvl, "nvlist/string/x"));

	CHECK(!nvlist_exists_string(nvl, "nvlist/string/abcdefghijklmnopqrstuvwxyz"));
	nvlist_add_string(nvl, "nvlist/string/abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz");
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_string(nvl, "nvlist/string/abcdefghijklmnopqrstuvwxyz"));

	CHECK(!nvlist_exists_string(nvl, "nvlist/stringf/"));
	nvlist_add_stringf(nvl, "nvlist/stringf/", "%s", "");
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_string(nvl, "nvlist/stringf/"));

	CHECK(!nvlist_exists_string(nvl, "nvlist/stringf/x"));
	nvlist_add_stringf(nvl, "nvlist/stringf/x", "%s", "x");
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_string(nvl, "nvlist/stringf/x"));

	CHECK(!nvlist_exists_string(nvl, "nvlist/stringf/666Xabc"));
	nvlist_add_stringf(nvl, "nvlist/stringf/666Xabc", "%d%c%s", 666, 'X', "abc");
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_string(nvl, "nvlist/stringf/666Xabc"));

	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/descriptor/STDERR_FILENO"));
	nvlist_add_descriptor(nvl, "nvlist/descriptor/STDERR_FILENO", STDERR_FILENO);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor/STDERR_FILENO"));

	CHECK(!nvlist_exists_binary(nvl, "nvlist/binary/x"));
	nvlist_add_binary(nvl, "nvlist/binary/x", "x", 1);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary/x"));

	CHECK(!nvlist_exists_binary(nvl, "nvlist/binary/abcdefghijklmnopqrstuvwxyz"));
	nvlist_add_binary(nvl, "nvlist/binary/abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz", sizeof("abcdefghijklmnopqrstuvwxyz"));
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary/abcdefghijklmnopqrstuvwxyz"));

	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	nvlist_add_nvlist(nvl, "nvlist/nvlist", nvl);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));

	CHECK(nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(nvlist_exists_bool(nvl, "nvlist/bool/true"));
	CHECK(nvlist_exists_bool(nvl, "nvlist/bool/false"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number/0"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number/1"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number/-1"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number/UINT64_MAX"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number/INT64_MIN"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number/INT64_MAX"));
	CHECK(nvlist_exists_string(nvl, "nvlist/string/"));
	CHECK(nvlist_exists_string(nvl, "nvlist/string/x"));
	CHECK(nvlist_exists_string(nvl, "nvlist/string/abcdefghijklmnopqrstuvwxyz"));
	CHECK(nvlist_exists_string(nvl, "nvlist/stringf/"));
	CHECK(nvlist_exists_string(nvl, "nvlist/stringf/x"));
	CHECK(nvlist_exists_string(nvl, "nvlist/stringf/666Xabc"));
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor/STDERR_FILENO"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary/x"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary/abcdefghijklmnopqrstuvwxyz"));
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));

	cnvl = nvlist_get_nvlist(nvl, "nvlist/nvlist");
	CHECK(nvlist_exists_null(cnvl, "nvlist/null"));
	CHECK(nvlist_exists_bool(cnvl, "nvlist/bool/true"));
	CHECK(nvlist_exists_bool(cnvl, "nvlist/bool/false"));
	CHECK(nvlist_exists_number(cnvl, "nvlist/number/0"));
	CHECK(nvlist_exists_number(cnvl, "nvlist/number/1"));
	CHECK(nvlist_exists_number(cnvl, "nvlist/number/-1"));
	CHECK(nvlist_exists_number(cnvl, "nvlist/number/UINT64_MAX"));
	CHECK(nvlist_exists_number(cnvl, "nvlist/number/INT64_MIN"));
	CHECK(nvlist_exists_number(cnvl, "nvlist/number/INT64_MAX"));
	CHECK(nvlist_exists_string(cnvl, "nvlist/string/"));
	CHECK(nvlist_exists_string(cnvl, "nvlist/string/x"));
	CHECK(nvlist_exists_string(cnvl, "nvlist/string/abcdefghijklmnopqrstuvwxyz"));
	CHECK(nvlist_exists_string(cnvl, "nvlist/stringf/"));
	CHECK(nvlist_exists_string(cnvl, "nvlist/stringf/x"));
	CHECK(nvlist_exists_string(cnvl, "nvlist/stringf/666Xabc"));
	CHECK(nvlist_exists_descriptor(cnvl, "nvlist/descriptor/STDERR_FILENO"));
	CHECK(nvlist_exists_binary(cnvl, "nvlist/binary/x"));
	CHECK(nvlist_exists_binary(cnvl, "nvlist/binary/abcdefghijklmnopqrstuvwxyz"));

	nvlist_destroy(nvl);

	return (0);
}
