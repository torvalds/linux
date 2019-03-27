/*-
 * Copyright (c) 2017 Juniper Networks.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#include <assert.h>
#include <stdlib.h>

#include <atf-c.h>

/* null */
ATF_TC_WITHOUT_HEAD(null_handler);
ATF_TC_BODY(null_handler, tc)
{
	assert(set_constraint_handler_s(abort_handler_s) == NULL);
}

/* abort handler */
ATF_TC_WITHOUT_HEAD(abort_handler);
ATF_TC_BODY(abort_handler, tc)
{
	set_constraint_handler_s(abort_handler_s);
	assert(set_constraint_handler_s(ignore_handler_s) == abort_handler_s);
}

/* ignore handler */
ATF_TC_WITHOUT_HEAD(ignore_handler);
ATF_TC_BODY(ignore_handler, tc)
{
	set_constraint_handler_s(ignore_handler_s);
	assert(set_constraint_handler_s(abort_handler_s) == ignore_handler_s);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, null_handler);
	ATF_TP_ADD_TC(tp, abort_handler);
	ATF_TP_ADD_TC(tp, ignore_handler);
	return (atf_no_error());
}
