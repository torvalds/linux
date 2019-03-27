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

#ifndef DSO_LIB
#include <atf-c.h>
#endif

#include <crt.h>

typedef void (*func_ptr)(void);

extern volatile int jcr_run;
extern const func_ptr *jcr_ptr;
extern const void *jcr_func_ptr;
extern volatile int ctors_run;
extern volatile int preinit_array_run;
extern volatile int preinit_array_state;
extern volatile int init_array_run;
extern volatile int init_array_state;

#ifndef DSO_BASE
volatile int jcr_run;
const func_ptr *jcr_ptr;
volatile int ctors_run;
volatile int preinit_array_run;
volatile int preinit_array_state = -1;
volatile int init_array_run;
volatile int init_array_state = -1;

void _Jv_RegisterClasses(const func_ptr *);

__section(".jcr") __used static func_ptr jcr_func = (func_ptr)1;
const void *jcr_func_ptr = &jcr_func;

void
_Jv_RegisterClasses(const func_ptr *jcr)
{

	jcr_run = 1;
	jcr_ptr = jcr;
}
#endif

#ifndef DSO_LIB
ATF_TC_WITHOUT_HEAD(jcr_test);
ATF_TC_BODY(jcr_test, tc)
{

	ATF_REQUIRE_MSG(jcr_run == 1, ".jcr not run");
	ATF_REQUIRE_MSG(jcr_ptr == jcr_func_ptr,
	    "Incorrect pointer passed to _Jv_RegisterClasses");
}
#endif

#ifndef DSO_BASE
static void
ctors_handler(void)
{

	ctors_run = 1;
}
__section(".ctors") __used static func_ptr ctors_func =
    &ctors_handler;
#endif

#ifndef DSO_LIB
ATF_TC_WITHOUT_HEAD(ctors_test);
ATF_TC_BODY(ctors_test, tc)
{

#ifdef HAVE_CTORS
	ATF_REQUIRE_MSG(ctors_run == 1, ".ctors not run");
#else
	ATF_REQUIRE_MSG(ctors_run == 0, ".ctors run");
#endif
}
#endif

#if !defined(DSO_BASE) && !defined(DSO_LIB)
static void
preinit_array_handler(void)
{

	preinit_array_run = 1;
	preinit_array_state = init_array_run;
}
__section(".preinit_array") __used static func_ptr preinit_array_func =
    &preinit_array_handler;
#endif

#ifndef DSO_LIB
ATF_TC_WITHOUT_HEAD(preinit_array_test);
ATF_TC_BODY(preinit_array_test, tc)
{

#ifdef DSO_BASE
	/* Check .preinit_array wasn't run in a DSO */
	ATF_REQUIRE_MSG(preinit_array_run == 0, ".preinit_array run in DSO");
#else
	ATF_REQUIRE_MSG(preinit_array_run == 1, ".preinit_array not run");
	ATF_REQUIRE_MSG(preinit_array_state == 0,
	    ".preinit_array was not run before .init_array");
#endif
}
#endif

#ifndef DSO_BASE
static void
init_array_handler(void)
{

	init_array_run = 1;
	init_array_state = preinit_array_run;
}
__section(".init_array") __used static func_ptr init_array_func =
    &init_array_handler;
#endif

#ifndef DSO_LIB
ATF_TC_WITHOUT_HEAD(init_array_test);
ATF_TC_BODY(init_array_test, tc)
{

	ATF_REQUIRE_MSG(init_array_run == 1, ".init_array not run");
#ifndef DSO_BASE
	ATF_REQUIRE_MSG(init_array_state == 1,
	    ".init_array was not run after .preinit_array");
#endif
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, jcr_test);
	ATF_TP_ADD_TC(tp, ctors_test);
	ATF_TP_ADD_TC(tp, preinit_array_test);
	ATF_TP_ADD_TC(tp, init_array_test);

	return (atf_no_error());
}
#endif
