/*-
 * Copyright (c) 2016 Mahdi Mokhtari <mokhi64@gmail.com>
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
#include <thread>

static FILE *output = NULL;

struct Foo {
	Foo() { ATF_REQUIRE(fprintf(output, "Created\n") > 0); }
	~Foo() { ATF_REQUIRE(fprintf(output, "Destroyed\n") > 0); }
	void use() { ATF_REQUIRE(fprintf(output, "Used\n") > 0); }
};

struct Bar {
	Bar() {}
	~Bar() {
		thread_local static Foo foo;
		ATF_REQUIRE(fprintf(output, "DIED\n") > 0);
	}
	void use() {}
};

extern "C" int __cxa_thread_atexit(void (*)(void *), void *, void *);

static void
again(void *arg)
{

	__cxa_thread_atexit(again, arg, &output);
}

struct Baz {
	Baz() {}
	~Baz() {
		again(NULL);
	}
	void use() {}
};

static thread_local Foo f;
static thread_local Foo g;
static thread_local Bar h;
static thread_local Baz e;

/*
 * This test must be linked to libpthread.
 */
ATF_TEST_CASE_WITHOUT_HEAD(cxx__thr);
ATF_TEST_CASE_BODY(cxx__thr)
{
	void *libthr_handle;

	/* Avoid coredump during f construction. */
	output = stderr;

	libthr_handle = dlopen("libthr.so.3", RTLD_LAZY | RTLD_GLOBAL |
	    RTLD_NOLOAD);
	ATF_REQUIRE(libthr_handle != NULL);
	dlclose(libthr_handle);
}

/*
 * In this test f.use() will test cxa_thread_atexit() in non-threaded mode.
 * After f.use() main will be threaded and we'll have one additional thread
 * with its own TLS data.
 */
ATF_TEST_CASE_WITHOUT_HEAD(cxx__thread_local_before);
ATF_TEST_CASE_BODY(cxx__thread_local_before)
{
	static const char out_log[] = "Created\nCreated\nUsed\nCreated\n"
	    "Created\nUsed\nCreated\nDIED\nDestroyed\nDestroyed\nDestroyed\n";

	ATF_REQUIRE((output = fopen("test_before.txt", "w")) != NULL);

	f.use();
	std::thread t([]() { f.use(); });
	t.join();

	fflush(output);

	ATF_REQUIRE(atf::utils::compare_file("test_before.txt", out_log));
}

/*
 * In this test, f.use() will test __cxa_thread_atexit()
 * in threaded mode (but still in main-threaed).
 */
ATF_TEST_CASE_WITHOUT_HEAD(cxx__thread_local_after);
ATF_TEST_CASE_BODY(cxx__thread_local_after)
{
	static const char out_log[] = "Created\nCreated\nUsed\nCreated\n"
	    "DIED\nDestroyed\nDestroyed\nDestroyed\nCreated\nCreated\nUsed\n";

	ATF_REQUIRE((output = fopen("test_after.txt", "w")) != NULL);

	std::thread t([]() { g.use(); });
	t.join();
	sleep(1);
	g.use();

	fflush(output);

	ATF_REQUIRE(atf::utils::compare_file("test_after.txt", out_log));
}

/*
 * In this test, we register a new dtor while dtors are being run
 * in __cxa_thread_atexit().
 */
ATF_TEST_CASE_WITHOUT_HEAD(cxx__thread_local_add_while_calling_dtors);
ATF_TEST_CASE_BODY(cxx__thread_local_add_while_calling_dtors)
{
	static const char out_log[] = "Created\nCreated\nCreated\nDIED\n"
	    "Destroyed\nDestroyed\nDestroyed\n";

	ATF_REQUIRE((output = fopen("test_add_meanwhile.txt", "w")) != NULL);

	std::thread t([]() { h.use(); });
	t.join();
	sleep(1);

	fflush(output);

	ATF_REQUIRE(atf::utils::compare_file("test_add_meanwhile.txt", out_log));
}

ATF_TEST_CASE_WITHOUT_HEAD(cxx__thread_inf_dtors);
ATF_TEST_CASE_BODY(cxx__thread_inf_dtors)
{

	/*
	 * Only added to make isolated run of this test not
	 * coredumping.  Construction of Foo objects require filled
	 * output.
	 */
	output = stderr;

	std::thread t([]() { e.use(); });
	t.join();
}

ATF_INIT_TEST_CASES(tcs)
{

	ATF_ADD_TEST_CASE(tcs, cxx__thr);
	ATF_ADD_TEST_CASE(tcs, cxx__thread_local_before);
	ATF_ADD_TEST_CASE(tcs, cxx__thread_local_after);
	ATF_ADD_TEST_CASE(tcs, cxx__thread_local_add_while_calling_dtors);
	ATF_ADD_TEST_CASE(tcs, cxx__thread_inf_dtors);
}
