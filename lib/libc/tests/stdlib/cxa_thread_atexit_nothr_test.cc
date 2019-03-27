/*-
 * Copyright (c) 2016 Mahdi Mokhtari <mokhi64@gmail.com>
 * Copyright (c) 2016 The FreeBSD Foundation
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

#include <dlfcn.h>
#include <atf-c++.hpp>
#include <cstdio>
#include <cstdlib>

static FILE *output = NULL;

struct Foo {
	Foo() { ATF_REQUIRE(fprintf(output, "Created\n") > 0); }
	~Foo() { ATF_REQUIRE(fprintf(output, "Destroyed\n") > 0); }
	void use() { ATF_REQUIRE(fprintf(output, "Used\n") > 0); }
};

static thread_local Foo f;

/*
 * This test must not be linked to libpthread.
 */
ATF_TEST_CASE_WITHOUT_HEAD(cxx__nothr);
ATF_TEST_CASE_BODY(cxx__nothr)
{
	void *libthr_handle;

	/* Avoid coredump during f construction. */
	output = stderr;

	libthr_handle = dlopen("libthr.so.3", RTLD_LAZY | RTLD_GLOBAL |
	    RTLD_NOLOAD);
	ATF_REQUIRE(libthr_handle == NULL);
}

static void
check_local_main(void)
{
	static const char out_log[] = "Created\nUsed\nDestroyed\n";

	fflush(output);
	ATF_REQUIRE(atf::utils::compare_file("test_main.txt", out_log));
}

ATF_TEST_CASE_WITHOUT_HEAD(cxx__thread_local_main);
ATF_TEST_CASE_BODY(cxx__thread_local_main)
{

	ATF_REQUIRE((output = fopen("test_main.txt", "w")) != NULL);
	f.use();
	atexit(check_local_main);
}

extern "C" int __cxa_thread_atexit(void (*)(void *), void *, void *);

static void
again(void *arg)
{

	__cxa_thread_atexit(again, arg, &output);
}

ATF_TEST_CASE_WITHOUT_HEAD(cxx__thread_inf_dtors);
ATF_TEST_CASE_BODY(cxx__thread_inf_dtors)
{

	again(NULL);
}

ATF_INIT_TEST_CASES(tcs)
{

	ATF_ADD_TEST_CASE(tcs, cxx__nothr);
	ATF_ADD_TEST_CASE(tcs, cxx__thread_local_main);
	ATF_ADD_TEST_CASE(tcs, cxx__thread_inf_dtors);
}
