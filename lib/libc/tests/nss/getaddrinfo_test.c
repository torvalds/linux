/*-
 * Copyright (c) 2006 Michael Bushkov <bushman@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>
#include <unistd.h>

#include <atf-c.h>

#include "freebsd_test_suite/macros.h"
#include "testutil.h"

enum test_methods {
	TEST_GETADDRINFO,
	TEST_BUILD_SNAPSHOT
};

static struct addrinfo hints;
static enum test_methods method = TEST_GETADDRINFO;

DECLARE_TEST_DATA(addrinfo)
DECLARE_TEST_FILE_SNAPSHOT(addrinfo)
DECLARE_2PASS_TEST(addrinfo)

static void clone_addrinfo(struct addrinfo *, struct addrinfo const *);
static int compare_addrinfo(struct addrinfo *, struct addrinfo *, void *);
static void dump_addrinfo(struct addrinfo *);

static void sdump_addrinfo(struct addrinfo *, char *, size_t);

IMPLEMENT_TEST_DATA(addrinfo)
IMPLEMENT_TEST_FILE_SNAPSHOT(addrinfo)
IMPLEMENT_2PASS_TEST(addrinfo)

static void
clone_addrinfo(struct addrinfo *dest, struct addrinfo const *src)
{

	ATF_REQUIRE(dest != NULL);
	ATF_REQUIRE(src != NULL);

	memcpy(dest, src, sizeof(struct addrinfo));
	if (src->ai_canonname != NULL)
		dest->ai_canonname = strdup(src->ai_canonname);

	if (src->ai_addr != NULL) {
		dest->ai_addr = malloc(src->ai_addrlen);
		ATF_REQUIRE(dest->ai_addr != NULL);
		memcpy(dest->ai_addr, src->ai_addr, src->ai_addrlen);
	}

	if (src->ai_next != NULL) {
		dest->ai_next = malloc(sizeof(struct addrinfo));
		ATF_REQUIRE(dest->ai_next != NULL);
		clone_addrinfo(dest->ai_next, src->ai_next);
	}
}

static int
compare_addrinfo_(struct addrinfo *ai1, struct addrinfo *ai2)
{

	if ((ai1 == NULL) || (ai2 == NULL))
		return (-1);

	if (ai1->ai_flags != ai2->ai_flags ||
	    ai1->ai_family != ai2->ai_family ||
	    ai1->ai_socktype != ai2->ai_socktype ||
	    ai1->ai_protocol != ai2->ai_protocol ||
	    ai1->ai_addrlen != ai2->ai_addrlen ||
	    ((ai1->ai_addr == NULL || ai2->ai_addr == NULL) &&
	     ai1->ai_addr != ai2->ai_addr) ||
	    ((ai1->ai_canonname == NULL || ai2->ai_canonname == NULL) &&
	     ai1->ai_canonname != ai2->ai_canonname))
		return (-1);

	if (ai1->ai_canonname != NULL &&
	    strcmp(ai1->ai_canonname, ai2->ai_canonname) != 0)
		return (-1);

	if (ai1->ai_addr != NULL &&
	    memcmp(ai1->ai_addr, ai2->ai_addr, ai1->ai_addrlen) != 0)
		return (-1);

	if (ai1->ai_next == NULL && ai2->ai_next == NULL)
		return (0);
	else
		return (compare_addrinfo_(ai1->ai_next, ai2->ai_next));
}

static int
compare_addrinfo(struct addrinfo *ai1, struct addrinfo *ai2,
    void *mdata __unused)
{
	int rv;

	printf("testing equality of 2 addrinfo structures\n");

	rv = compare_addrinfo_(ai1, ai2);

	if (rv == 0)
		printf("equal\n");
	else {
		dump_addrinfo(ai1);
		dump_addrinfo(ai2);
		printf("not equal\n");
	}

	return (rv);
}

static void
free_addrinfo(struct addrinfo *ai)
{
	if (ai == NULL)
		return;

	free(ai->ai_addr);
	free(ai->ai_canonname);
	free_addrinfo(ai->ai_next);
}

void
sdump_addrinfo(struct addrinfo *ai, char *buffer, size_t buflen)
{
	int written, i;

	written = snprintf(buffer, buflen, "%d %d %d %d %d ",
		ai->ai_flags, ai->ai_family, ai->ai_socktype, ai->ai_protocol,
		ai->ai_addrlen);
	buffer += written;
	if (written > (int)buflen)
		return;
	buflen -= written;

	written = snprintf(buffer, buflen, "%s ",
		ai->ai_canonname == NULL ? "(null)" : ai->ai_canonname);
	buffer += written;
	if (written > (int)buflen)
		return;
	buflen -= written;

	if (ai->ai_addr == NULL) {
		written = snprintf(buffer, buflen, "(null)");
		buffer += written;
		if (written > (int)buflen)
			return;
		buflen -= written;
	} else {
		for (i = 0; i < (int)ai->ai_addrlen; i++) {
			written = snprintf(buffer, buflen,
			    i + 1 != (int)ai->ai_addrlen ? "%d." : "%d",
			    ((unsigned char *)ai->ai_addr)[i]);
			buffer += written;
			if (written > (int)buflen)
				return;
			buflen -= written;

			if (buflen == 0)
				return;
		}
	}

	if (ai->ai_next != NULL) {
		written = snprintf(buffer, buflen, ":");
		buffer += written;
		if (written > (int)buflen)
			return;
		buflen -= written;

		sdump_addrinfo(ai->ai_next, buffer, buflen);
	}
}

void
dump_addrinfo(struct addrinfo *result)
{
	if (result != NULL) {
		char buffer[2048];
		sdump_addrinfo(result, buffer, sizeof(buffer));
		printf("%s\n", buffer);
	} else
		printf("(null)\n");
}

static int
addrinfo_read_snapshot_addr(char *addr, unsigned char *result, size_t len)
{
	char *s, *ps, *ts;

	ps = addr;
	while ((s = strsep(&ps, ".")) != NULL) {
		if (len == 0)
			return (-1);

		*result = (unsigned char)strtol(s, &ts, 10);
		++result;
		if (*ts != '\0')
			return (-1);

		--len;
	}
	if (len != 0)
		return (-1);
	else
		return (0);
}

static int
addrinfo_read_snapshot_ai(struct addrinfo *ai, char *line)
{
	char *s, *ps, *ts;
	int i, rv, *pi;

	rv = 0;
	i = 0;
	ps = line;
	memset(ai, 0, sizeof(struct addrinfo));
	while ((s = strsep(&ps, " ")) != NULL) {
		switch (i) {
		case 0:
		case 1:
		case 2:
		case 3:
			pi = &ai->ai_flags + i;
			*pi = (int)strtol(s, &ts, 10);
			if (*ts != '\0')
				goto fin;
			break;
		case 4:
			ai->ai_addrlen = (socklen_t)strtol(s, &ts, 10);
			if (*ts != '\0')
				goto fin;
			break;
		case 5:
			if (strcmp(s, "(null)") != 0) {
				ai->ai_canonname = strdup(s);
				ATF_REQUIRE(ai->ai_canonname != NULL);
			}
			break;
		case 6:
			if (strcmp(s, "(null)") != 0) {
				ai->ai_addr = calloc(1, ai->ai_addrlen);
				ATF_REQUIRE(ai->ai_addr != NULL);
				rv = addrinfo_read_snapshot_addr(s,
				    (unsigned char *)ai->ai_addr,
				    ai->ai_addrlen);

				if (rv != 0)
					goto fin;
			}
			break;
		default:
			/* NOTE: should not be reachable */
			rv = -1;
			goto fin;
		}

		++i;
	}

fin:
	if (i != 7 || rv != 0) {
		free_addrinfo(ai);
		ai = NULL;
		return (-1);
	}

	return (0);
}

static int
addrinfo_read_snapshot_func(struct addrinfo *ai, char *line)
{
	struct addrinfo *ai2;
	char *s, *ps;
	int rv;

	printf("1 line read from snapshot:\n%s\n", line);

	rv = 0;
	ps = line;

	s = strsep(&ps, ":");
	if (s == NULL)
		return (-1);

	rv = addrinfo_read_snapshot_ai(ai, s);
	if (rv != 0)
		return (-1);

	ai2 = ai;
	while ((s = strsep(&ps, ":")) != NULL) {
		ai2->ai_next = calloc(1, sizeof(struct addrinfo));
		ATF_REQUIRE(ai2->ai_next != NULL);

		rv = addrinfo_read_snapshot_ai(ai2->ai_next, s);
		if (rv != 0) {
			free_addrinfo(ai);
			ai = NULL;
			return (-1);
		}

		ai2 = ai2->ai_next;
	}

	return (0);
}

static int
addrinfo_test_correctness(struct addrinfo *ai, void *mdata __unused)
{

	printf("testing correctness with the following data:\n");
	dump_addrinfo(ai);

	if (ai == NULL)
		goto errfin;

	if (!(ai->ai_family >= 0 && ai->ai_family < AF_MAX))
		goto errfin;

	if (ai->ai_socktype != 0 && ai->ai_socktype != SOCK_STREAM &&
	    ai->ai_socktype != SOCK_DGRAM && ai->ai_socktype != SOCK_RAW)
		goto errfin;

	if (ai->ai_protocol != 0 && ai->ai_protocol != IPPROTO_UDP &&
	    ai->ai_protocol != IPPROTO_TCP)
		goto errfin;

	if ((ai->ai_flags & ~(AI_CANONNAME | AI_NUMERICHOST | AI_PASSIVE)) != 0)
		goto errfin;

	if (ai->ai_addrlen != ai->ai_addr->sa_len ||
	    ai->ai_family != ai->ai_addr->sa_family)
		goto errfin;

	printf("correct\n");

	return (0);
errfin:
	printf("incorrect\n");

	return (-1);
}

static int
addrinfo_read_hostlist_func(struct addrinfo *ai, char *line)
{
	struct addrinfo *result;
	int rv;

	printf("resolving %s: ", line);
	rv = getaddrinfo(line, NULL, &hints, &result);
	if (rv == 0) {
		printf("found\n");

		rv = addrinfo_test_correctness(result, NULL);
		if (rv != 0) {
			freeaddrinfo(result);
			result = NULL;
			return (rv);
		}

		clone_addrinfo(ai, result);
		freeaddrinfo(result);
		result = NULL;
	} else {
		printf("not found\n");

 		memset(ai, 0, sizeof(struct addrinfo));
	}
	return (0);
}

static void
run_tests(char *hostlist_file, const char *snapshot_file, int ai_family)
{
	struct addrinfo_test_data td, td_snap;
	char *snapshot_file_copy;
	int rv;

	if (snapshot_file == NULL)
		snapshot_file_copy = NULL;
	else {
		snapshot_file_copy = strdup(snapshot_file);
		ATF_REQUIRE(snapshot_file_copy != NULL);
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = ai_family;
	hints.ai_flags = AI_CANONNAME;

	if (snapshot_file != NULL)
		method = TEST_BUILD_SNAPSHOT;

	TEST_DATA_INIT(addrinfo, &td, clone_addrinfo, free_addrinfo);
	TEST_DATA_INIT(addrinfo, &td_snap, clone_addrinfo, free_addrinfo);

	ATF_REQUIRE_MSG(access(hostlist_file, R_OK) == 0,
		"can't access the hostlist file %s\n", hostlist_file);

	printf("building host lists from %s\n", hostlist_file);

	rv = TEST_SNAPSHOT_FILE_READ(addrinfo, hostlist_file, &td,
		addrinfo_read_hostlist_func);
	if (rv != 0)
		goto fin;

	if (snapshot_file != NULL) {
		if (access(snapshot_file, W_OK | R_OK) != 0) {
			if (errno == ENOENT)
				method = TEST_BUILD_SNAPSHOT;
			else {
				printf("can't access the snapshot "
				    "file %s\n", snapshot_file);

				rv = -1;
				goto fin;
			}
		} else {
			rv = TEST_SNAPSHOT_FILE_READ(addrinfo, snapshot_file,
				&td_snap, addrinfo_read_snapshot_func);
			if (rv != 0) {
				printf("error reading snapshot file: %s\n",
				    strerror(errno));
				goto fin;
			}
		}
	}

	switch (method) {
	case TEST_GETADDRINFO:
		if (snapshot_file != NULL)
			ATF_CHECK(DO_2PASS_TEST(addrinfo, &td, &td_snap,
				compare_addrinfo, NULL) == 0);
		break;
	case TEST_BUILD_SNAPSHOT:
		if (snapshot_file != NULL) {
			ATF_CHECK(TEST_SNAPSHOT_FILE_WRITE(addrinfo,
			    snapshot_file, &td, sdump_addrinfo) == 0);
		}
		break;
	default:
		break;
	}

fin:
	TEST_DATA_DESTROY(addrinfo, &td_snap);
	TEST_DATA_DESTROY(addrinfo, &td);

	free(snapshot_file_copy);
}

#define	HOSTLIST_FILE	"mach"
#define	RUN_TESTS(tc, snapshot_file, ai_family) do {			\
	char *_hostlist_file;						\
	ATF_REQUIRE(0 < asprintf(&_hostlist_file, "%s/%s",		\
	    atf_tc_get_config_var(tc, "srcdir"), HOSTLIST_FILE));	\
	run_tests(_hostlist_file, snapshot_file, ai_family);		\
	free(_hostlist_file);						\
} while (0)

ATF_TC_WITHOUT_HEAD(pf_unspec);
ATF_TC_BODY(pf_unspec, tc)
{

	RUN_TESTS(tc, NULL, AF_UNSPEC);
}

ATF_TC_WITHOUT_HEAD(pf_unspec_with_snapshot);
ATF_TC_BODY(pf_unspec_with_snapshot, tc)
{

	RUN_TESTS(tc, "snapshot_ai", AF_UNSPEC);
}

ATF_TC_WITHOUT_HEAD(pf_inet);
ATF_TC_BODY(pf_inet, tc)
{

	ATF_REQUIRE_FEATURE("inet");
	RUN_TESTS(tc, NULL, AF_INET);
}

ATF_TC_WITHOUT_HEAD(pf_inet_with_snapshot);
ATF_TC_BODY(pf_inet_with_snapshot, tc)
{

	ATF_REQUIRE_FEATURE("inet");
	RUN_TESTS(tc, "snapshot_ai4", AF_INET);
}

ATF_TC_WITHOUT_HEAD(pf_inet6);
ATF_TC_BODY(pf_inet6, tc)
{

	ATF_REQUIRE_FEATURE("inet6");
	RUN_TESTS(tc, NULL, AF_INET6);
}

ATF_TC_WITHOUT_HEAD(pf_inet6_with_snapshot);
ATF_TC_BODY(pf_inet6_with_snapshot, tc)
{

	ATF_REQUIRE_FEATURE("inet6");
	RUN_TESTS(tc, "snapshot_ai6", AF_INET6);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, pf_unspec);
	ATF_TP_ADD_TC(tp, pf_unspec_with_snapshot);
	ATF_TP_ADD_TC(tp, pf_inet);
	ATF_TP_ADD_TC(tp, pf_inet_with_snapshot);
	ATF_TP_ADD_TC(tp, pf_inet6);
	ATF_TP_ADD_TC(tp, pf_inet6_with_snapshot);

	return (atf_no_error());
}
