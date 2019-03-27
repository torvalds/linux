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

#include <sys/ioctl.h>

#include <bsm/libbsm.h>
#include <security/audit/audit_ioctl.h>

#include <atf-c.h>
#include <fcntl.h>
#include <unistd.h>

#include "utils.h"

static int filedesc;
static char ioregex[80];
static const char *auclass = "io";
static struct pollfd fds[1];
static unsigned long request = AUDITPIPE_FLUSH;


ATF_TC_WITH_CLEANUP(ioctl_success);
ATF_TC_HEAD(ioctl_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"ioctl(2) call");
}

ATF_TC_BODY(ioctl_success, tc)
{
	/* auditpipe(4) supports quite a few ioctls */
	ATF_REQUIRE((filedesc = open("/dev/auditpipe", O_RDONLY)) != -1);
	/* Prepare the regex to be checked in the audit record */
	snprintf(ioregex, sizeof(ioregex),
	"ioctl.*%#lx.*%#x.*return,success", request, filedesc);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE(ioctl(filedesc, request) != -1);
	check_audit(fds, ioregex, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(ioctl_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(ioctl_failure);
ATF_TC_HEAD(ioctl_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"ioctl(2) call");
}

ATF_TC_BODY(ioctl_failure, tc)
{
	snprintf(ioregex, sizeof(ioregex),
	"ioctl.*%#lx.*return,failure : Bad file descriptor", request);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid file descriptor */
	ATF_REQUIRE_EQ(-1, ioctl(-1, request));
	check_audit(fds, ioregex, pipefd);
}

ATF_TC_CLEANUP(ioctl_failure, tc)
{
	cleanup();
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, ioctl_success);
	ATF_TP_ADD_TC(tp, ioctl_failure);

	return (atf_no_error());
}
