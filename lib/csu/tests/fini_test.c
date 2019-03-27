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
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <atf-c.h>

#include <crt.h>

extern bool run_dtors_test;
extern bool run_fini_array_test;
void dso_handle_check(void);


#ifndef DSO_BASE
typedef void (*func_ptr)(void);

bool run_dtors_test = false;
bool run_fini_array_test = false;

static void
dtors_handler(void)
{

	if (run_dtors_test)
		_exit(1);
}
__section(".dtors") __used static func_ptr dtors_func =
    &dtors_handler;
#endif

#ifndef DSO_LIB
ATF_TC_WITHOUT_HEAD(dtors_test);
ATF_TC_BODY(dtors_test, tc)
{
	pid_t pid, wpid;
	int status;

	pid = fork();
	switch(pid) {
	case -1:
		break;
	case 0:
		run_dtors_test = true;
		exit(0);
	default:
		while ((wpid = waitpid(pid, &status, 0)) == -1 &&
		    errno == EINTR)
			;
#ifdef HAVE_CTORS
		ATF_REQUIRE_MSG(WEXITSTATUS(status) == 1,
		    ".dtors failed to run");
#else
		ATF_REQUIRE_MSG(WEXITSTATUS(status) == 0,
		    ".dtors incorrectly ran");
#endif
		break;
	}
}
#endif

#ifndef DSO_BASE
static void
fini_array_handler(void)
{

	if (run_fini_array_test)
		_exit(1);
}
__section(".fini_array") __used static func_ptr fini_array_func =
    &fini_array_handler;
#endif

#ifndef DSO_LIB
ATF_TC_WITHOUT_HEAD(fini_array_test);
ATF_TC_BODY(fini_array_test, tc)
{
	pid_t pid, wpid;
	int status;

	pid = fork();
	switch(pid) {
	case -1:
		break;
	case 0:
		run_fini_array_test = true;
		exit(0);
	default:
		while ((wpid = waitpid(pid, &status, 0)) == -1 &&
		    errno == EINTR)
			;
		ATF_REQUIRE_MSG(WEXITSTATUS(status) == 1,
		    ".fini_array failed to run");
		break;
	}
}
#endif

#ifndef DSO_BASE
extern void *__dso_handle;

void
dso_handle_check(void)
{
	void *dso = __dso_handle;

#ifdef DSO_LIB
	ATF_REQUIRE_MSG(dso != NULL,
	    "Null __dso_handle in DSO");
#else
	ATF_REQUIRE_MSG(dso == NULL,
	    "Invalid __dso_handle in non-DSO");
#endif
}
#endif

#ifndef DSO_LIB
ATF_TC_WITHOUT_HEAD(dso_handle_test);
ATF_TC_BODY(dso_handle_test, tc)
{

	dso_handle_check();
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, dtors_test);
	ATF_TP_ADD_TC(tp, fini_array_test);
	ATF_TP_ADD_TC(tp, dso_handle_test);

	return (atf_no_error());
}
#endif
