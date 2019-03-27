/*-
 * Copyright (c) 2016 Maksym Sobolyev
 * All rights reserved.
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

#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <atf-c.h>

static const char *funname;
static char *soname;

static void
sigsegv_handler(int sig __unused)
{
        unlink(soname);
        free(soname);
        atf_tc_fail("got SIGSEGV in the %s(3)", funname);
}

ATF_TC(dlopen_empty_test);
ATF_TC_HEAD(dlopen_empty_test, tc)
{
        atf_tc_set_md_var(tc, "descr", "Tests the dlopen() of an empty file "
                      "returns an error");
}
ATF_TC_BODY(dlopen_empty_test, tc)
{
        char tempname[] = "/tmp/temp.XXXXXX";
        char *fname;
        int fd;
        void *dlh;
        struct sigaction act, oact;

        fname = mktemp(tempname);
        ATF_REQUIRE_MSG(fname != NULL, "mktemp failed; errno=%d", errno);
        asprintf(&soname, "%s.so", fname);
        ATF_REQUIRE_MSG(soname != NULL, "asprintf failed; errno=%d", ENOMEM);
        fd = open(soname, O_WRONLY | O_CREAT | O_TRUNC, DEFFILEMODE);
        ATF_REQUIRE_MSG(fd != -1, "open(\"%s\") failed; errno=%d", soname, errno);
        close(fd);

        act.sa_handler = sigsegv_handler;
        act.sa_flags = 0;
        sigemptyset(&act.sa_mask);
        ATF_CHECK_MSG(sigaction(SIGSEGV, &act, &oact) != -1,
            "sigaction() failed");

        funname = "dlopen";
        dlh = dlopen(soname, RTLD_LAZY);
        if (dlh != NULL) {
                funname = "dlclose";
                dlclose(dlh);
        }
        ATF_REQUIRE_MSG(dlh == NULL, "dlopen(\"%s\") did not fail", soname);
        unlink(soname);
        free(soname);
}

ATF_TP_ADD_TCS(tp)
{

        ATF_TP_ADD_TC(tp, dlopen_empty_test);

        return (atf_no_error());
}
