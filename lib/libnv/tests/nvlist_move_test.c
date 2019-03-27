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
	const nvlist_t *cnvl;
	nvlist_t *nvl;
	void *ptr;
	size_t size;
	int fd;

	printf("1..52\n");

	nvl = nvlist_create(0);

	CHECK(!nvlist_exists_string(nvl, "nvlist/string/"));
	ptr = strdup("");
	CHECK(ptr != NULL);
	nvlist_move_string(nvl, "nvlist/string/", ptr);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_string(nvl, "nvlist/string/"));
	CHECK(ptr == nvlist_get_string(nvl, "nvlist/string/"));

	CHECK(!nvlist_exists_string(nvl, "nvlist/string/x"));
	ptr = strdup("x");
	CHECK(ptr != NULL);
	nvlist_move_string(nvl, "nvlist/string/x", ptr);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_string(nvl, "nvlist/string/x"));
	CHECK(ptr == nvlist_get_string(nvl, "nvlist/string/x"));

	CHECK(!nvlist_exists_string(nvl,
	    "nvlist/string/abcdefghijklmnopqrstuvwxyz"));
	ptr = strdup("abcdefghijklmnopqrstuvwxyz");
	CHECK(ptr != NULL);
	nvlist_move_string(nvl, "nvlist/string/abcdefghijklmnopqrstuvwxyz",
	    ptr);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_string(nvl,
	    "nvlist/string/abcdefghijklmnopqrstuvwxyz"));
	CHECK(ptr ==
	    nvlist_get_string(nvl, "nvlist/string/abcdefghijklmnopqrstuvwxyz"));

	CHECK(!nvlist_exists_descriptor(nvl,
	    "nvlist/descriptor/STDERR_FILENO"));
	fd = dup(STDERR_FILENO);
	CHECK(fd >= 0);
	nvlist_move_descriptor(nvl, "nvlist/descriptor/STDERR_FILENO", fd);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor/STDERR_FILENO"));
	CHECK(fd ==
	    nvlist_get_descriptor(nvl, "nvlist/descriptor/STDERR_FILENO"));

	CHECK(!nvlist_exists_binary(nvl, "nvlist/binary/x"));
	ptr = malloc(1);
	CHECK(ptr != NULL);
	memcpy(ptr, "x", 1);
	nvlist_move_binary(nvl, "nvlist/binary/x", ptr, 1);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary/x"));
	CHECK(ptr == nvlist_get_binary(nvl, "nvlist/binary/x", NULL));
	CHECK(ptr == nvlist_get_binary(nvl, "nvlist/binary/x", &size));
	CHECK(size == 1);

	CHECK(!nvlist_exists_binary(nvl,
	    "nvlist/binary/abcdefghijklmnopqrstuvwxyz"));
	ptr = malloc(sizeof("abcdefghijklmnopqrstuvwxyz"));
	CHECK(ptr != NULL);
	memcpy(ptr, "abcdefghijklmnopqrstuvwxyz",
	    sizeof("abcdefghijklmnopqrstuvwxyz"));
	nvlist_move_binary(nvl, "nvlist/binary/abcdefghijklmnopqrstuvwxyz",
	    ptr, sizeof("abcdefghijklmnopqrstuvwxyz"));
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_binary(nvl,
	    "nvlist/binary/abcdefghijklmnopqrstuvwxyz"));
	CHECK(ptr == nvlist_get_binary(nvl,
	    "nvlist/binary/abcdefghijklmnopqrstuvwxyz", NULL));
	CHECK(ptr == nvlist_get_binary(nvl,
	    "nvlist/binary/abcdefghijklmnopqrstuvwxyz", &size));
	CHECK(size == sizeof("abcdefghijklmnopqrstuvwxyz"));

	CHECK(!nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	ptr = nvlist_clone(nvl);
	CHECK(ptr != NULL);
	nvlist_move_nvlist(nvl, "nvlist/nvlist", ptr);
	CHECK(nvlist_error(nvl) == 0);
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));
	CHECK(ptr == nvlist_get_nvlist(nvl, "nvlist/nvlist"));

	CHECK(nvlist_exists_string(nvl, "nvlist/string/"));
	CHECK(nvlist_exists_string(nvl, "nvlist/string/x"));
	CHECK(nvlist_exists_string(nvl,
	    "nvlist/string/abcdefghijklmnopqrstuvwxyz"));
	CHECK(nvlist_exists_descriptor(nvl, "nvlist/descriptor/STDERR_FILENO"));
	CHECK(nvlist_exists_binary(nvl, "nvlist/binary/x"));
	CHECK(nvlist_exists_binary(nvl,
	    "nvlist/binary/abcdefghijklmnopqrstuvwxyz"));
	CHECK(nvlist_exists_nvlist(nvl, "nvlist/nvlist"));

	cnvl = nvlist_get_nvlist(nvl, "nvlist/nvlist");
	CHECK(nvlist_exists_string(cnvl, "nvlist/string/"));
	CHECK(nvlist_exists_string(cnvl, "nvlist/string/x"));
	CHECK(nvlist_exists_string(cnvl,
	    "nvlist/string/abcdefghijklmnopqrstuvwxyz"));
	CHECK(nvlist_exists_descriptor(cnvl,
	    "nvlist/descriptor/STDERR_FILENO"));
	CHECK(nvlist_exists_binary(cnvl, "nvlist/binary/x"));
	CHECK(nvlist_exists_binary(cnvl,
	    "nvlist/binary/abcdefghijklmnopqrstuvwxyz"));

	nvlist_destroy(nvl);

	return (0);
}
