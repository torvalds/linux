/*	$OpenBSD: t_getpid.c,v 1.2 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_getpid.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "macros.h"

#include <sys/wait.h>

#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "atf-c.h"

static int	 maxiter = 10;
static void	*threadfunc(void *);

static void *
threadfunc(void *arg)
{
	*(pid_t *)arg = getpid();

	return NULL;
}

ATF_TC(getpid_process);
ATF_TC_HEAD(getpid_process, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test getpid(2) with processes");
}

ATF_TC_BODY(getpid_process, tc)
{
	pid_t ppid, fpid, cpid, tpid, wpid;
	int i, sta;

	for (i = 0; i < maxiter; i++) {

		tpid = getpid();
		fpid = fork();

		ATF_REQUIRE(fpid >= 0);

		if (fpid == 0) {

			cpid = getpid();
			ppid = getppid();

			if (tpid != ppid)
				_exit(EXIT_FAILURE);

			if (cpid == ppid)
				_exit(EXIT_FAILURE);

			if (tpid == fpid)
				_exit(EXIT_FAILURE);

			_exit(EXIT_SUCCESS);
		}

		wpid = wait(&sta);

		if (wpid != fpid)
			atf_tc_fail("PID mismatch");

		ATF_REQUIRE(WIFEXITED(sta) != 0);

		if (WEXITSTATUS(sta) != EXIT_SUCCESS)
			atf_tc_fail("PID mismatch");
	}
}

ATF_TC(getpid_thread);
ATF_TC_HEAD(getpid_thread, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test getpid(2) with threads");
}

ATF_TC_BODY(getpid_thread, tc)
{
	pid_t pid, tpid;
	pthread_t tid;
	int i, rv;

	for (i = 0; i < maxiter; i++) {

		pid = getpid();

		rv = pthread_create(&tid, NULL, threadfunc, &tpid);
		ATF_REQUIRE(rv == 0);

		rv = pthread_join(tid, NULL);
		ATF_REQUIRE(rv == 0);

		if (pid != tpid)
			atf_tc_fail("Unequal PIDs");
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getpid_process);
	ATF_TP_ADD_TC(tp, getpid_thread);

	return atf_no_error();
}
