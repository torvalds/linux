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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

static errno_t e;
static const char * restrict m;

void
h(const char * restrict msg, void * restrict ptr __unused, errno_t error)
{
	e = error;
	m = msg;
}

/* null ptr */
ATF_TC_WITHOUT_HEAD(null_ptr);
ATF_TC_BODY(null_ptr, tc)
{
	assert(memset_s(0, 1, 1, 1) != 0);
}

/* smax > rmax */
ATF_TC_WITHOUT_HEAD(smax_gt_rmax);
ATF_TC_BODY(smax_gt_rmax, tc)
{
	char b;

	assert(memset_s(&b, RSIZE_MAX + 1, 1, 1) != 0);
}

/* smax < 0 */
ATF_TC_WITHOUT_HEAD(smax_lt_zero);
ATF_TC_BODY(smax_lt_zero, tc)
{
	char b;

	assert(memset_s(&b, -1, 1, 1) != 0);
}

/* normal */
ATF_TC_WITHOUT_HEAD(normal);
ATF_TC_BODY(normal, tc)
{
	char b;

	b = 3;
	assert(memset_s(&b, 1, 5, 1) == 0);
	assert(b == 5);
}

/* n > rmax */
ATF_TC_WITHOUT_HEAD(n_gt_rmax);
ATF_TC_BODY(n_gt_rmax, tc)
{
	char b;

	assert(memset_s(&b, 1, 1, RSIZE_MAX + 1) != 0);
}

/* n < 0 */
ATF_TC_WITHOUT_HEAD(n_lt_zero);
ATF_TC_BODY(n_lt_zero, tc)
{
	char b;

	assert(memset_s(&b, 1, 1, -1) != 0);
}

/* n < smax */
ATF_TC_WITHOUT_HEAD(n_lt_smax);
ATF_TC_BODY(n_lt_smax, tc)
{
	char b[3] = {1, 2, 3};

	assert(memset_s(&b[0], 3, 9, 1) == 0);
	assert(b[0] == 9);
	assert(b[1] == 2);
	assert(b[2] == 3);
}

/* n > smax, handler */
ATF_TC_WITHOUT_HEAD(n_gt_smax);
ATF_TC_BODY(n_gt_smax, tc)
{
	char b[3] = {1, 2, 3};

	e = 0;
	m = NULL;
	set_constraint_handler_s(h);
	assert(memset_s(&b[0], 1, 9, 3) != 0);
	assert(e > 0);
	assert(strcmp(m, "memset_s : n > smax") == 0);
	assert(b[0] == 9);
	assert(b[1] == 2);
	assert(b[2] == 3);
}

/* smax > rmax, handler */
ATF_TC_WITHOUT_HEAD(smax_gt_rmax_handler);
ATF_TC_BODY(smax_gt_rmax_handler, tc)
{
	char b;

	e = 0;
	m = NULL;
	set_constraint_handler_s(h);
	assert(memset_s(&b, RSIZE_MAX + 1, 1, 1) != 0);
	assert(e > 0);
	assert(strcmp(m, "memset_s : smax > RSIZE_MAX") == 0);
}

/* smax < 0, handler */
ATF_TC_WITHOUT_HEAD(smax_lt_zero_handler);
ATF_TC_BODY(smax_lt_zero_handler, tc)
{
	char b;

	e = 0;
	m = NULL;
	set_constraint_handler_s(h);
	assert(memset_s(&b, -1, 1, 1) != 0);
	assert(e > 0);
	assert(strcmp(m, "memset_s : smax > RSIZE_MAX") == 0);
}

/* n > rmax, handler */
ATF_TC_WITHOUT_HEAD(n_gt_rmax_handler);
ATF_TC_BODY(n_gt_rmax_handler, tc)
{
	char b;

	e = 0;
	m = NULL;
	set_constraint_handler_s(h);
	assert(memset_s(&b, 1, 1, RSIZE_MAX + 1) != 0);
	assert(e > 0);
	assert(strcmp(m, "memset_s : n > RSIZE_MAX") == 0);
}

/* n < 0, handler */
ATF_TC_WITHOUT_HEAD(n_lt_zero_handler);
ATF_TC_BODY(n_lt_zero_handler, tc)
{
	char b;

	e = 0;
	m = NULL;
	set_constraint_handler_s(h);
	assert(memset_s(&b, 1, 1, -1) != 0);
	assert(e > 0);
	assert(strcmp(m, "memset_s : n > RSIZE_MAX") == 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, null_ptr);
	ATF_TP_ADD_TC(tp, smax_gt_rmax);
	ATF_TP_ADD_TC(tp, smax_lt_zero);
	ATF_TP_ADD_TC(tp, normal);
	ATF_TP_ADD_TC(tp, n_gt_rmax);
	ATF_TP_ADD_TC(tp, n_lt_zero);
	ATF_TP_ADD_TC(tp, n_gt_smax);
	ATF_TP_ADD_TC(tp, n_lt_smax);
	ATF_TP_ADD_TC(tp, smax_gt_rmax_handler);
	ATF_TP_ADD_TC(tp, smax_lt_zero_handler);
	ATF_TP_ADD_TC(tp, n_gt_rmax_handler);
	ATF_TP_ADD_TC(tp, n_lt_zero_handler);
	return (atf_no_error());
}
