/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 Andrew Turner
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef DSO_LIB
#include <atf-c++.hpp>
#endif

extern volatile int constructor_run;
extern bool run_destructor_test;

#ifndef DSO_BASE
volatile int constructor_run;
bool run_destructor_test = false;
#endif

struct Foo {
	Foo() {
		constructor_run = 1;
	}
	~Foo() {
		if (run_destructor_test)
			_exit(1);
	}
};
extern Foo foo;

#ifndef DSO_BASE
Foo foo;
#endif

#ifndef DSO_LIB
ATF_TEST_CASE_WITHOUT_HEAD(cxx_constructor);
ATF_TEST_CASE_BODY(cxx_constructor)
{

	ATF_REQUIRE(constructor_run == 1);
}

ATF_TEST_CASE_WITHOUT_HEAD(cxx_destructor);
ATF_TEST_CASE_BODY(cxx_destructor)
{
	pid_t pid, wpid;
	int status;

	pid = fork();
	switch(pid) {
	case -1:
		break;
	case 0:
		run_destructor_test = true;
		exit(0);
	default:
		while ((wpid = waitpid(pid, &status, 0)) == -1 &&
		    errno == EINTR)
			;
		ATF_REQUIRE(WEXITSTATUS(status) == 1);
		break;
	}
}

ATF_INIT_TEST_CASES(tcs)
{

	ATF_ADD_TEST_CASE(tcs, cxx_constructor);
	ATF_ADD_TEST_CASE(tcs, cxx_destructor);
}
#endif
