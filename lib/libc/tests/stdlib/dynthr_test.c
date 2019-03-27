/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (C) 2019 Andrew Gierth
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
 *
 * Though this file is initially distributed under the 2-clause BSD license,
 * the author grants permission for its redistribution under alternative
 * licenses as set forth at <https://rhodiumtoad.github.io/RELICENSE.txt>.
 * This paragraph and the RELICENSE.txt file are not part of the license and
 * may be omitted in redistributions.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

#include <atf-c.h>

typedef void (modfunc_t)(int op);

/*
 * Minimal test case for PR 235158; mutual dependencies between jemalloc and
 * libthr causing issues in thread creation.  Specifically to this case, libthr
 * uses calloc to initialize pthread mutexes, and jemalloc uses pthread mutexes.
 *
 * Deferred initialization provided by jemalloc proved to be fragile, causing
 * issues like in the referenced PR where thread creation in a shared object
 * loaded via dlopen(3) would stall unless the calling application also linked
 * against pthread.
 */
ATF_TC(maintc);
ATF_TC_HEAD(maintc, tc)
{

	atf_tc_set_md_var(tc, "timeout", "3");
}

ATF_TC_BODY(maintc, tc)
{
	char *libpath;
	modfunc_t *func;
	void *mod_handle;
	const char *srcdir;
	dlfunc_t rawfunc;

	srcdir = atf_tc_get_config_var(tc, "srcdir");
	if (asprintf(&libpath, "%s/dynthr_mod.so", srcdir) < 0)
		atf_tc_fail("failed to construct path to libthr");
	mod_handle = dlopen(libpath, RTLD_LOCAL);
	free(libpath);
	if (mod_handle == NULL)
		atf_tc_fail("failed to open dynthr_mod.so: %s", dlerror());
	rawfunc = dlfunc(mod_handle, "mod_main");
	if (rawfunc == NULL)
		atf_tc_fail("failed to resolve function mod_main");
	func = (modfunc_t *)rawfunc;
	func(1);
	func(0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, maintc);
	return (atf_no_error());
}
