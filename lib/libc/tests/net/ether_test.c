/*-
 * Copyright (c) 2007 Robert N. M. Watson
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include <net/ethernet.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <atf-c.h>

static const char *ether_line_string = "01:23:45:67:89:ab ether_line_hostname";
static const char *ether_line_hostname = "ether_line_hostname";
static const struct ether_addr	 ether_line_addr = {
	{ 0x01, 0x23, 0x45, 0x67, 0x89, 0xab }
};

ATF_TC_WITHOUT_HEAD(ether_line);
ATF_TC_BODY(ether_line, tc)
{
	struct ether_addr e;
	char hostname[256];

	ATF_REQUIRE_MSG(ether_line(ether_line_string, &e, hostname) == 0,
	    "ether_line failed; errno=%d", errno);
	ATF_REQUIRE_MSG(bcmp(&e, &ether_line_addr, ETHER_ADDR_LEN) == 0,
	    "bad address");
	ATF_REQUIRE_MSG(strcmp(hostname, ether_line_hostname) == 0,
	    "bad hostname");
}

static const char *ether_line_bad_1_string = "x";

ATF_TC_WITHOUT_HEAD(ether_line_bad_1);
ATF_TC_BODY(ether_line_bad_1, tc)
{
	struct ether_addr e;
	char hostname[256];

	ATF_REQUIRE_MSG(ether_line(ether_line_bad_1_string, &e, hostname) != 0,
	    "ether_line succeeded unexpectedly");
}

static const char *ether_line_bad_2_string = "x x";

ATF_TC_WITHOUT_HEAD(ether_line_bad_2);
ATF_TC_BODY(ether_line_bad_2, tc)
{
	struct ether_addr e;
	char hostname[256];

	ATF_REQUIRE_MSG(ether_line(ether_line_bad_2_string, &e, hostname) != 0,
	    "ether_line succeeded unexpectedly");
}

static const char *ether_aton_string = "01:23:45:67:89:ab";
static const struct ether_addr	 ether_aton_addr = {
	{ 0x01, 0x23, 0x45, 0x67, 0x89, 0xab }
};

ATF_TC_WITHOUT_HEAD(ether_aton_r);
ATF_TC_BODY(ether_aton_r, tc)
{
	struct ether_addr e, *ep;

	ep = ether_aton_r(ether_aton_string, &e);

	ATF_REQUIRE_MSG(ep != NULL, "ether_aton_r failed; errno=%d", errno);
	ATF_REQUIRE_MSG(ep == &e,
	    "ether_aton_r returned different pointers; %p != %p", ep, &e);
}

static const char		*ether_aton_bad_string = "x";

ATF_TC_WITHOUT_HEAD(ether_aton_r_bad);
ATF_TC_BODY(ether_aton_r_bad, tc)
{
	struct ether_addr e, *ep;

	ep = ether_aton_r(ether_aton_bad_string, &e);
	ATF_REQUIRE_MSG(ep == NULL, "ether_aton_r succeeded unexpectedly");
}

ATF_TC_WITHOUT_HEAD(ether_aton);
ATF_TC_BODY(ether_aton, tc)
{
	struct ether_addr *ep;

	ep = ether_aton(ether_aton_string);
	ATF_REQUIRE_MSG(ep != NULL, "ether_aton failed");
	ATF_REQUIRE_MSG(bcmp(ep, &ether_aton_addr, ETHER_ADDR_LEN) == 0,
	    "bad address");
}

ATF_TC_WITHOUT_HEAD(ether_aton_bad);
ATF_TC_BODY(ether_aton_bad, tc)
{
	struct ether_addr *ep;

	ep = ether_aton(ether_aton_bad_string);
	ATF_REQUIRE_MSG(ep == NULL, "ether_aton succeeded unexpectedly");
}

static const char *ether_ntoa_string = "01:23:45:67:89:ab";
static const struct ether_addr	 ether_ntoa_addr = {
	{ 0x01, 0x23, 0x45, 0x67, 0x89, 0xab }
};

ATF_TC_WITHOUT_HEAD(ether_ntoa_r);
ATF_TC_BODY(ether_ntoa_r, tc)
{
	char buf[256], *cp;

	cp = ether_ntoa_r(&ether_ntoa_addr, buf);
	ATF_REQUIRE_MSG(cp != NULL, "ether_ntoa_r failed");
	ATF_REQUIRE_MSG(cp == buf,
	    "ether_ntoa_r returned a different pointer; %p != %p", cp, buf);
	ATF_REQUIRE_MSG(strcmp(cp, ether_ntoa_string) == 0,
	    "strings did not match (`%s` != `%s`)", cp, ether_ntoa_string);
}

ATF_TC_WITHOUT_HEAD(ether_ntoa);
ATF_TC_BODY(ether_ntoa, tc)
{
	char *cp;

	cp = ether_ntoa(&ether_ntoa_addr);
	ATF_REQUIRE_MSG(cp != NULL, "ether_ntoa failed");
	ATF_REQUIRE_MSG(strcmp(cp, ether_ntoa_string) == 0,
	    "strings did not match (`%s` != `%s`)", cp, ether_ntoa_string);
}

#if 0
ATF_TC_WITHOUT_HEAD(ether_ntohost);
ATF_TC_BODY(ether_ntohost, tc)
{

}

ATF_TC_WITHOUT_HEAD(ether_hostton);
ATF_TC_BODY(ether_hostton, tc)
{

}
#endif

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ether_line);
	ATF_TP_ADD_TC(tp, ether_line_bad_1);
	ATF_TP_ADD_TC(tp, ether_line_bad_2);
	ATF_TP_ADD_TC(tp, ether_aton_r);
	ATF_TP_ADD_TC(tp, ether_aton_r_bad);
	ATF_TP_ADD_TC(tp, ether_aton);
	ATF_TP_ADD_TC(tp, ether_aton_bad);
	ATF_TP_ADD_TC(tp, ether_ntoa_r);
	ATF_TP_ADD_TC(tp, ether_ntoa);
#if 0
	ATF_TP_ADD_TC(tp, ether_ntohost);
	ATF_TP_ADD_TC(tp, ether_hostton);
#endif

	return (atf_no_error());
}
