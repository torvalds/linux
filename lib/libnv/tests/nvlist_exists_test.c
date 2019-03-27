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
	nvlist_t *nvl;

	printf("1..232\n");

	nvl = nvlist_create(0);

	CHECK(!nvlist_exists(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/null"));
	nvlist_add_null(nvl, "nvlist/null");
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists(nvl, "nvlist/null"));
	CHECK(nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/null"));

	CHECK(!nvlist_exists(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/bool"));
	nvlist_add_bool(nvl, "nvlist/bool", true);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/bool"));
	CHECK(nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/bool"));

	CHECK(!nvlist_exists(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/number"));
	nvlist_add_number(nvl, "nvlist/number", 0);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/number"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/number"));

	CHECK(!nvlist_exists(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/string"));
	nvlist_add_string(nvl, "nvlist/string", "test");
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/string"));
	CHECK(nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/string"));

	CHECK(!nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/nvlist"));
	nvlist_add_nvlist(nvl, "nvlist/nvlist", nvl);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/nvlist"));

	CHECK(!nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/descriptor"));
	nvlist_add_descriptor(nvl, "nvlist/descriptor", STDERR_FILENO);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/descriptor"));

	CHECK(!nvlist_exists(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/binary"));
	nvlist_add_binary(nvl, "nvlist/binary", "test", 4);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/binary"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary"));

	CHECK(nvlist_exists(nvl, "nvlist/null"));
	CHECK(nvlist_exists(nvl, "nvlist/bool"));
	CHECK(nvlist_exists(nvl, "nvlist/number"));
	CHECK(nvlist_exists(nvl, "nvlist/string"));
	CHECK(nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists(nvl, "nvlist/binary"));
	CHECK(nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary"));

	nvlist_free_null(nvl, "nvlist/null");
	CHECK(!nvlist_exists(nvl, "nvlist/null"));
	CHECK(nvlist_exists(nvl, "nvlist/bool"));
	CHECK(nvlist_exists(nvl, "nvlist/number"));
	CHECK(nvlist_exists(nvl, "nvlist/string"));
	CHECK(nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary"));

	nvlist_free_bool(nvl, "nvlist/bool");
	CHECK(!nvlist_exists(nvl, "nvlist/null"));
	CHECK(!nvlist_exists(nvl, "nvlist/bool"));
	CHECK(nvlist_exists(nvl, "nvlist/number"));
	CHECK(nvlist_exists(nvl, "nvlist/string"));
	CHECK(nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary"));

	nvlist_free_number(nvl, "nvlist/number");
	CHECK(!nvlist_exists(nvl, "nvlist/null"));
	CHECK(!nvlist_exists(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists(nvl, "nvlist/number"));
	CHECK(nvlist_exists(nvl, "nvlist/string"));
	CHECK(nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary"));

	nvlist_free_string(nvl, "nvlist/string");
	CHECK(!nvlist_exists(nvl, "nvlist/null"));
	CHECK(!nvlist_exists(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists(nvl, "nvlist/number"));
	CHECK(!nvlist_exists(nvl, "nvlist/string"));
	CHECK(nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary"));

	nvlist_free_nvlist(nvl, "nvlist/nvlist");
	CHECK(!nvlist_exists(nvl, "nvlist/null"));
	CHECK(!nvlist_exists(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists(nvl, "nvlist/number"));
	CHECK(!nvlist_exists(nvl, "nvlist/string"));
	CHECK(!nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary"));

	nvlist_free_descriptor(nvl, "nvlist/descriptor");
	CHECK(!nvlist_exists(nvl, "nvlist/null"));
	CHECK(!nvlist_exists(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists(nvl, "nvlist/number"));
	CHECK(!nvlist_exists(nvl, "nvlist/string"));
	CHECK(!nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary"));

	nvlist_free_binary(nvl, "nvlist/binary");
	CHECK(!nvlist_exists(nvl, "nvlist/null"));
	CHECK(!nvlist_exists(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists(nvl, "nvlist/number"));
	CHECK(!nvlist_exists(nvl, "nvlist/string"));
	CHECK(!nvlist_exists(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists(nvl, "nvlist/binary"));
	CHECK(!nvlist_exists_null(nvl, "nvlist/null"));
	CHECK(!nvlist_exists_bool(nvl, "nvlist/bool"));
	CHECK(!nvlist_exists_number(nvl, "nvlist/number"));
	CHECK(!nvlist_exists_string(nvl, "nvlist/string"));
	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(!nvlist_exists_descriptor(nvl, "nvlist/descriptor"));
	CHECK(!nvlist_exists_binary(nvl, "nvlist/binary"));

	CHECK(nvlist_empty(nvl));

	nvlist_destroy(nvl);

	return (0);
}
