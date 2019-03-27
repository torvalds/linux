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

/*
 * Note: open(2) and openat(2) have 12 events each for various values of 'flag'
 * Please see: contrib/openbsm/etc/audit_event#L261
 *
 * 270:AUE_OPENAT_R:openat(2) - read:fr
 * 271:AUE_OPENAT_RC:openat(2) - read,creat:fc,fr,fa,fm
 * 272:AUE_OPENAT_RT:openat(2) - read,trunc:fd,fr,fa,fm
 * 273:AUE_OPENAT_RTC:openat(2) - read,creat,trunc:fc,fd,fr,fa,fm
 * 274:AUE_OPENAT_W:openat(2) - write:fw
 * 275:AUE_OPENAT_WC:openat(2) - write,creat:fc,fw,fa,fm
 * 276:AUE_OPENAT_WT:openat(2) - write,trunc:fd,fw,fa,fm
 * 277:AUE_OPENAT_WTC:openat(2) - write,creat,trunc:fc,fd,fw,fa,fm
 * 278:AUE_OPENAT_RW:openat(2) - read,write:fr,fw
 * 279:AUE_OPENAT_RWC:openat(2) - read,write,create:fc,fw,fr,fa,fm
 * 280:AUE_OPENAT_RWT:openat(2) - read,write,trunc:fd,fw,fr,fa,fm
 * 281:AUE_OPENAT_RWTC:openat(2) - read,write,creat,trunc:fc,fd,fw,fr,fa,fm
 */

#include <sys/syscall.h>

#include <atf-c.h>
#include <fcntl.h>

#include "utils.h"

static struct pollfd fds[1];
static mode_t o_mode = 0777;
static int filedesc;
static char extregex[80];
static const char *path = "fileforaudit";
static const char *errpath = "adirhasnoname/fileforaudit";

/*
 * Define test-cases for success and failure modes of both open(2) and openat(2)
 */
#define OPEN_AT_TC_DEFINE(mode, regex, flag, class) 			      \
ATF_TC_WITH_CLEANUP(open_ ## mode ## _success);				      \
ATF_TC_HEAD(open_ ## mode ## _success, tc) 				      \
{ 									      \
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "     \
				"open(2) call with flags = %s", #flag);       \
} 									      \
ATF_TC_BODY(open_ ## mode ## _success, tc) 				      \
{ 									      \
	snprintf(extregex, sizeof(extregex), 				      \
		"open.*%s.*fileforaudit.*return,success", regex); 	      \
	/* File needs to exist for successful open(2) invocation */ 	      \
	ATF_REQUIRE((filedesc = open(path, O_CREAT, o_mode)) != -1); 	      \
	FILE *pipefd = setup(fds, class); 				      \
	ATF_REQUIRE(syscall(SYS_open, path, flag) != -1); 		      \
	check_audit(fds, extregex, pipefd); 				      \
	close(filedesc); 						      \
} 									      \
ATF_TC_CLEANUP(open_ ## mode ## _success, tc) 				      \
{ 									      \
	cleanup(); 							      \
} 									      \
ATF_TC_WITH_CLEANUP(open_ ## mode ## _failure); 			      \
ATF_TC_HEAD(open_ ## mode ## _failure, tc) 				      \
{ 									      \
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "  \
				"open(2) call with flags = %s", #flag);       \
} 									      \
ATF_TC_BODY(open_ ## mode ## _failure, tc) 				      \
{ 									      \
	snprintf(extregex, sizeof(extregex), 				      \
		"open.*%s.*fileforaudit.*return,failure", regex); 	      \
	FILE *pipefd = setup(fds, class); 				      \
	ATF_REQUIRE_EQ(-1, syscall(SYS_open, errpath, flag)); 		      \
	check_audit(fds, extregex, pipefd); 				      \
} 									      \
ATF_TC_CLEANUP(open_ ## mode ## _failure, tc) 				      \
{ 									      \
	cleanup(); 							      \
} 									      \
ATF_TC_WITH_CLEANUP(openat_ ## mode ## _success); 			      \
ATF_TC_HEAD(openat_ ## mode ## _success, tc) 				      \
{ 									      \
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "     \
				"openat(2) call with flags = %s", #flag);     \
} 									      \
ATF_TC_BODY(openat_ ## mode ## _success, tc) 				      \
{ 									      \
	int filedesc2; 							      \
	snprintf(extregex, sizeof(extregex), 				      \
		"openat.*%s.*fileforaudit.*return,success", regex); 	      \
	/* File needs to exist for successful openat(2) invocation */ 	      \
	ATF_REQUIRE((filedesc = open(path, O_CREAT, o_mode)) != -1); 	      \
	FILE *pipefd = setup(fds, class); 				      \
	ATF_REQUIRE((filedesc2 = openat(AT_FDCWD, path, flag)) != -1); 	      \
	check_audit(fds, extregex, pipefd); 				      \
	close(filedesc2); 						      \
	close(filedesc); 						      \
} 									      \
ATF_TC_CLEANUP(openat_ ## mode ## _success, tc) 			      \
{ 									      \
	cleanup(); 							      \
} 									      \
ATF_TC_WITH_CLEANUP(openat_ ## mode ## _failure); 			      \
ATF_TC_HEAD(openat_ ## mode ## _failure, tc) 				      \
{ 									      \
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "  \
				"openat(2) call with flags = %s", #flag);     \
} 									      \
ATF_TC_BODY(openat_ ## mode ## _failure, tc) 				      \
{ 									      \
	snprintf(extregex, sizeof(extregex), 				      \
		"openat.*%s.*fileforaudit.*return,failure", regex); 	      \
	FILE *pipefd = setup(fds, class); 				      \
	ATF_REQUIRE_EQ(-1, openat(AT_FDCWD, errpath, flag)); 		      \
	check_audit(fds, extregex, pipefd); 				      \
} 									      \
ATF_TC_CLEANUP(openat_ ## mode ## _failure, tc) 			      \
{ 									      \
	cleanup(); 							      \
}

/*
 * Add both success and failure modes of open(2) and openat(2)
 */
#define OPEN_AT_TC_ADD(tp, mode) 					      \
do { 									      \
	ATF_TP_ADD_TC(tp, open_ ## mode ## _success); 			      \
	ATF_TP_ADD_TC(tp, open_ ## mode ## _failure); 			      \
	ATF_TP_ADD_TC(tp, openat_ ## mode ## _success); 		      \
	ATF_TP_ADD_TC(tp, openat_ ## mode ## _failure); 		      \
} while (0)


/*
 * Each of the 12 OPEN_AT_TC_DEFINE statement is a group of 4 test-cases
 * corresponding to separate audit events for open(2) and openat(2)
 */
OPEN_AT_TC_DEFINE(read, "read", O_RDONLY, "fr")
OPEN_AT_TC_DEFINE(read_creat, "read,creat", O_RDONLY | O_CREAT, "fr")
OPEN_AT_TC_DEFINE(read_trunc, "read,trunc", O_RDONLY | O_TRUNC, "fr")
OPEN_AT_TC_DEFINE(read_creat_trunc, "read,creat,trunc", O_RDONLY | O_CREAT
	| O_TRUNC, "fr")
OPEN_AT_TC_DEFINE(write, "write", O_WRONLY, "fw")
OPEN_AT_TC_DEFINE(write_creat, "write,creat", O_WRONLY | O_CREAT, "fw")
OPEN_AT_TC_DEFINE(write_trunc, "write,trunc", O_WRONLY | O_TRUNC, "fw")
OPEN_AT_TC_DEFINE(write_creat_trunc, "write,creat,trunc", O_WRONLY | O_CREAT
	| O_TRUNC, "fw")
OPEN_AT_TC_DEFINE(read_write, "read,write", O_RDWR, "fr")
OPEN_AT_TC_DEFINE(read_write_creat, "read,write,creat", O_RDWR | O_CREAT, "fw")
OPEN_AT_TC_DEFINE(read_write_trunc, "read,write,trunc", O_RDWR | O_TRUNC, "fr")
OPEN_AT_TC_DEFINE(read_write_creat_trunc, "read,write,creat,trunc", O_RDWR |
	O_CREAT | O_TRUNC, "fw")


ATF_TP_ADD_TCS(tp)
{
	OPEN_AT_TC_ADD(tp, read);
	OPEN_AT_TC_ADD(tp, read_creat);
	OPEN_AT_TC_ADD(tp, read_trunc);
	OPEN_AT_TC_ADD(tp, read_creat_trunc);

	OPEN_AT_TC_ADD(tp, write);
	OPEN_AT_TC_ADD(tp, write_creat);
	OPEN_AT_TC_ADD(tp, write_trunc);
	OPEN_AT_TC_ADD(tp, write_creat_trunc);

	OPEN_AT_TC_ADD(tp, read_write);
	OPEN_AT_TC_ADD(tp, read_write_creat);
	OPEN_AT_TC_ADD(tp, read_write_trunc);
	OPEN_AT_TC_ADD(tp, read_write_creat_trunc);

	return (atf_no_error());
}
