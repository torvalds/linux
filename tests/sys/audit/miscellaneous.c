/*-
 * Copyright (c) 2018 Aniket Pandey
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
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <bsm/audit.h>
#include <machine/sysarch.h>

#include <atf-c.h>
#include <unistd.h>

#include "utils.h"

static pid_t pid;
static char miscreg[80];
static struct pollfd fds[1];
static const char *auclass = "ot";


/*
 * Success case of audit(2) is skipped for now as the behaviour is quite
 * undeterministic. It will be added when the intermittency is resolved.
 */


ATF_TC_WITH_CLEANUP(audit_failure);
ATF_TC_HEAD(audit_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"audit(2) call");
}

ATF_TC_BODY(audit_failure, tc)
{
	pid = getpid();
	snprintf(miscreg, sizeof(miscreg), "audit.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid argument */
	ATF_REQUIRE_EQ(-1, audit(NULL, -1));
	check_audit(fds, miscreg, pipefd);
}

ATF_TC_CLEANUP(audit_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sysarch_success);
ATF_TC_HEAD(sysarch_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"sysarch(2) call");
}

ATF_TC_BODY(sysarch_success, tc)
{
	pid = getpid();
	snprintf(miscreg, sizeof(miscreg), "sysarch.*%d.*return,success", pid);

	/* Set sysnum to the syscall corresponding to the system architecture */
#if defined(I386_GET_IOPERM)		/* i386 */
	struct i386_ioperm_args i3sysarg;
	bzero(&i3sysarg, sizeof(i3sysarg));

#elif defined(AMD64_GET_FSBASE)		/* amd64 */
	register_t amd64arg;

#elif defined(MIPS_GET_TLS)		/* MIPS */
	char *mipsarg;

#elif defined(ARM_SYNC_ICACHE)		/* ARM */
	struct arm_sync_icache_args armsysarg;
	bzero(&armsysarg, sizeof(armsysarg));

#elif defined(SPARC_UTRAP_INSTALL)	/* Sparc64 */
	struct sparc_utrap_args handler = {
		.type		= UT_DIVISION_BY_ZERO,
		/* We don't want to change the previous handlers */
		.new_precise	= (void *)UTH_NOCHANGE,
		.new_deferred	= (void *)UTH_NOCHANGE,
		.old_precise	= NULL,
		.old_deferred	= NULL
	};

	struct sparc_utrap_install_args sparc64arg = {
		.num 		= ST_DIVISION_BY_ZERO,
		.handlers	= &handler
	};
#else
	/* For PowerPC, ARM64, RISCV archs, sysarch(2) is not supported */
	atf_tc_skip("sysarch(2) is not supported for the system architecture");
#endif

	FILE *pipefd = setup(fds, auclass);
#if defined(I386_GET_IOPERM)
	ATF_REQUIRE_EQ(0, sysarch(I386_GET_IOPERM, &i3sysarg));
#elif defined(AMD64_GET_FSBASE)
	ATF_REQUIRE_EQ(0, sysarch(AMD64_GET_FSBASE, &amd64arg));
#elif defined(MIPS_GET_TLS)
	ATF_REQUIRE_EQ(0, sysarch(MIPS_GET_TLS, &mipsarg));
#elif defined(ARM_SYNC_ICACHE)
	ATF_REQUIRE_EQ(0, sysarch(ARM_SYNC_ICACHE, &armsysarg));
#elif defined(SPARC_UTRAP_INSTALL)
	ATF_REQUIRE_EQ(0, sysarch(SPARC_UTRAP_INSTALL, &sparc64arg));
#endif
	check_audit(fds, miscreg, pipefd);
}

ATF_TC_CLEANUP(sysarch_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sysarch_failure);
ATF_TC_HEAD(sysarch_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
				       "sysarch(2) call for any architecture");
}

ATF_TC_BODY(sysarch_failure, tc)
{
	pid = getpid();
	snprintf(miscreg, sizeof(miscreg), "sysarch.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid argument and Bad address */
	ATF_REQUIRE_EQ(-1, sysarch(-1, NULL));
	check_audit(fds, miscreg, pipefd);
}

ATF_TC_CLEANUP(sysarch_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sysctl_success);
ATF_TC_HEAD(sysctl_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"sysctl(3) call");
}

ATF_TC_BODY(sysctl_success, tc)
{
	int mib[2], maxproc;
	size_t proclen;

	/* Set mib to retrieve the maximum number of allowed processes */
	mib[0] = CTL_KERN;
	mib[1] = KERN_MAXPROC;
	proclen = sizeof(maxproc);

	pid = getpid();
	snprintf(miscreg, sizeof(miscreg), "sysctl.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, sysctl(mib, 2, &maxproc, &proclen, NULL, 0));
	check_audit(fds, miscreg, pipefd);
}

ATF_TC_CLEANUP(sysctl_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sysctl_failure);
ATF_TC_HEAD(sysctl_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"sysctl(3) call");
}

ATF_TC_BODY(sysctl_failure, tc)
{
	pid = getpid();
	snprintf(miscreg, sizeof(miscreg), "sysctl.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid arguments */
	ATF_REQUIRE_EQ(-1, sysctl(NULL, 0, NULL, NULL, NULL, 0));
	check_audit(fds, miscreg, pipefd);
}

ATF_TC_CLEANUP(sysctl_failure, tc)
{
	cleanup();
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, audit_failure);

	ATF_TP_ADD_TC(tp, sysarch_success);
	ATF_TP_ADD_TC(tp, sysarch_failure);

	ATF_TP_ADD_TC(tp, sysctl_success);
	ATF_TP_ADD_TC(tp, sysctl_failure);

	return (atf_no_error());
}
