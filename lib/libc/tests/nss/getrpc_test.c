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

#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>
#include <unistd.h>

#include <atf-c.h>

#include "testutil.h"

enum test_methods {
	TEST_GETRPCENT,
	TEST_GETRPCBYNAME,
	TEST_GETRPCBYNUMBER,
	TEST_GETRPCENT_2PASS,
	TEST_BUILD_SNAPSHOT
};

DECLARE_TEST_DATA(rpcent)
DECLARE_TEST_FILE_SNAPSHOT(rpcent)
DECLARE_1PASS_TEST(rpcent)
DECLARE_2PASS_TEST(rpcent)

static void clone_rpcent(struct rpcent *, struct rpcent const *);
static int compare_rpcent(struct rpcent *, struct rpcent *, void *);
static void dump_rpcent(struct rpcent *);
static void free_rpcent(struct rpcent *);

static void sdump_rpcent(struct rpcent *, char *, size_t);
static int rpcent_read_snapshot_func(struct rpcent *, char *);

static int rpcent_check_ambiguity(struct rpcent_test_data *,
	struct rpcent *);
static int rpcent_fill_test_data(struct rpcent_test_data *);
static int rpcent_test_correctness(struct rpcent *, void *);
static int rpcent_test_getrpcbyname(struct rpcent *, void *);
static int rpcent_test_getrpcbynumber(struct rpcent *, void *);
static int rpcent_test_getrpcent(struct rpcent *, void *);

IMPLEMENT_TEST_DATA(rpcent)
IMPLEMENT_TEST_FILE_SNAPSHOT(rpcent)
IMPLEMENT_1PASS_TEST(rpcent)
IMPLEMENT_2PASS_TEST(rpcent)

static void
clone_rpcent(struct rpcent *dest, struct rpcent const *src)
{
	ATF_REQUIRE(dest != NULL);
	ATF_REQUIRE(src != NULL);

	char **cp;
	int aliases_num;

	memset(dest, 0, sizeof(struct rpcent));

	if (src->r_name != NULL) {
		dest->r_name = strdup(src->r_name);
		ATF_REQUIRE(dest->r_name != NULL);
	}

	dest->r_number = src->r_number;

	if (src->r_aliases != NULL) {
		aliases_num = 0;
		for (cp = src->r_aliases; *cp; ++cp)
			++aliases_num;

		dest->r_aliases = calloc(aliases_num + 1, sizeof(char *));
		ATF_REQUIRE(dest->r_aliases != NULL);

		for (cp = src->r_aliases; *cp; ++cp) {
			dest->r_aliases[cp - src->r_aliases] = strdup(*cp);
			ATF_REQUIRE(dest->r_aliases[cp - src->r_aliases] != NULL);
		}
	}
}

static void
free_rpcent(struct rpcent *rpc)
{
	char **cp;

	ATF_REQUIRE(rpc != NULL);

	free(rpc->r_name);

	for (cp = rpc->r_aliases; *cp; ++cp)
		free(*cp);
	free(rpc->r_aliases);
}

static  int
compare_rpcent(struct rpcent *rpc1, struct rpcent *rpc2, void *mdata)
{
	char **c1, **c2;

	if (rpc1 == rpc2)
		return 0;

	if ((rpc1 == NULL) || (rpc2 == NULL))
		goto errfin;

	if ((strcmp(rpc1->r_name, rpc2->r_name) != 0) ||
		(rpc1->r_number != rpc2->r_number))
			goto errfin;

	c1 = rpc1->r_aliases;
	c2 = rpc2->r_aliases;

	if ((rpc1->r_aliases == NULL) || (rpc2->r_aliases == NULL))
		goto errfin;

	for (;*c1 && *c2; ++c1, ++c2)
		if (strcmp(*c1, *c2) != 0)
			goto errfin;

	if ((*c1 != '\0') || (*c2 != '\0'))
		goto errfin;

	return 0;

errfin:
	if (mdata == NULL) {
		printf("following structures are not equal:\n");
		dump_rpcent(rpc1);
		dump_rpcent(rpc2);
	}

	return (-1);
}

static void
sdump_rpcent(struct rpcent *rpc, char *buffer, size_t buflen)
{
	char **cp;
	int written;

	written = snprintf(buffer, buflen, "%s %d",
		rpc->r_name, rpc->r_number);
	buffer += written;
	if (written > (int)buflen)
		return;
	buflen -= written;

	if (rpc->r_aliases != NULL) {
		if (*(rpc->r_aliases) != '\0') {
			for (cp = rpc->r_aliases; *cp; ++cp) {
				written = snprintf(buffer, buflen, " %s", *cp);
				buffer += written;
				if (written > (int)buflen)
					return;
				buflen -= written;

				if (buflen == 0)
					return;
			}
		} else
			snprintf(buffer, buflen, " noaliases");
	} else
		snprintf(buffer, buflen, " (null)");
}

static int
rpcent_read_snapshot_func(struct rpcent *rpc, char *line)
{
	StringList *sl;
	char *s, *ps, *ts;
	int i;

	printf("1 line read from snapshot:\n%s\n", line);

	i = 0;
	sl = NULL;
	ps = line;
	memset(rpc, 0, sizeof(struct rpcent));
	while ((s = strsep(&ps, " ")) != NULL) {
		switch (i) {
		case 0:
				rpc->r_name = strdup(s);
				ATF_REQUIRE(rpc->r_name != NULL);
			break;

		case 1:
			rpc->r_number = (int)strtol(s, &ts, 10);
			if (*ts != '\0') {
				free(rpc->r_name);
				return (-1);
			}
			break;

		default:
			if (sl == NULL) {
				if (strcmp(s, "(null)") == 0)
					return (0);

				sl = sl_init();
				ATF_REQUIRE(sl != NULL);

				if (strcmp(s, "noaliases") != 0) {
					ts = strdup(s);
					ATF_REQUIRE(ts != NULL);
					sl_add(sl, ts);
				}
			} else {
				ts = strdup(s);
				ATF_REQUIRE(ts != NULL);
				sl_add(sl, ts);
			}
			break;
		}
		i++;
	}

	if (i < 3) {
		free(rpc->r_name);
		memset(rpc, 0, sizeof(struct rpcent));
		return (-1);
	}

	sl_add(sl, NULL);
	rpc->r_aliases = sl->sl_str;

	/* NOTE: is it a dirty hack or not? */
	free(sl);
	return (0);
}

static void
dump_rpcent(struct rpcent *result)
{
	if (result != NULL) {
		char buffer[1024];
		sdump_rpcent(result, buffer, sizeof(buffer));
		printf("%s\n", buffer);
	} else
		printf("(null)\n");
}

static int
rpcent_fill_test_data(struct rpcent_test_data *td)
{
	struct rpcent *rpc;

	setrpcent(1);
	while ((rpc = getrpcent()) != NULL) {
		if (rpcent_test_correctness(rpc, NULL) == 0)
			TEST_DATA_APPEND(rpcent, td, rpc);
		else
			return (-1);
	}
	endrpcent();

	return (0);
}

static int
rpcent_test_correctness(struct rpcent *rpc, void *mdata __unused)
{

	printf("testing correctness with the following data:\n");
	dump_rpcent(rpc);

	if (rpc == NULL)
		goto errfin;

	if (rpc->r_name == NULL)
		goto errfin;

	if (rpc->r_number < 0)
		goto errfin;

	if (rpc->r_aliases == NULL)
		goto errfin;

	printf("correct\n");

	return (0);
errfin:
	printf("incorrect\n");

	return (-1);
}

/* rpcent_check_ambiguity() is needed when one port+rpc is associated with
 * more than one piece (these cases are usually marked as PROBLEM in
 * /etc/peices. This functions is needed also when one piece+rpc is
 * associated with several ports. We have to check all the rpcent structures
 * to make sure that rpc really exists and correct */
static int
rpcent_check_ambiguity(struct rpcent_test_data *td, struct rpcent *rpc)
{

	return (TEST_DATA_FIND(rpcent, td, rpc, compare_rpcent,
		NULL) != NULL ? 0 : -1);
}

static int
rpcent_test_getrpcbyname(struct rpcent *rpc_model, void *mdata)
{
	char **alias;
	struct rpcent *rpc;

	printf("testing getrpcbyname() with the following data:\n");
	dump_rpcent(rpc_model);

	rpc = getrpcbyname(rpc_model->r_name);
	if (rpcent_test_correctness(rpc, NULL) != 0)
		goto errfin;

	if ((compare_rpcent(rpc, rpc_model, NULL) != 0) &&
	    (rpcent_check_ambiguity((struct rpcent_test_data *)mdata, rpc)
	    !=0))
	    goto errfin;

	for (alias = rpc_model->r_aliases; *alias; ++alias) {
		rpc = getrpcbyname(*alias);

		if (rpcent_test_correctness(rpc, NULL) != 0)
			goto errfin;

		if ((compare_rpcent(rpc, rpc_model, NULL) != 0) &&
		    (rpcent_check_ambiguity(
		    (struct rpcent_test_data *)mdata, rpc) != 0))
		    goto errfin;
	}

	printf("ok\n");
	return (0);

errfin:
	printf("not ok\n");

	return (-1);
}

static int
rpcent_test_getrpcbynumber(struct rpcent *rpc_model, void *mdata)
{
	struct rpcent *rpc;

	printf("testing getrpcbyport() with the following data...\n");
	dump_rpcent(rpc_model);

	rpc = getrpcbynumber(rpc_model->r_number);
	if (rpcent_test_correctness(rpc, NULL) != 0 ||
	    (compare_rpcent(rpc, rpc_model, NULL) != 0 &&
	     rpcent_check_ambiguity((struct rpcent_test_data *)mdata, rpc)
	    != 0)) {
		printf("not ok\n");
		return (-1);
	} else {
		printf("ok\n");
		return (0);
	}
}

static int
rpcent_test_getrpcent(struct rpcent *rpc, void *mdata __unused)
{

	/*
	 * Only correctness can be checked when doing 1-pass test for
	 * getrpcent().
	 */
	return (rpcent_test_correctness(rpc, NULL));
}

static int
run_tests(const char *snapshot_file, enum test_methods method)
{
	struct rpcent_test_data td, td_snap, td_2pass;
	int rv;

	TEST_DATA_INIT(rpcent, &td, clone_rpcent, free_rpcent);
	TEST_DATA_INIT(rpcent, &td_snap, clone_rpcent, free_rpcent);
	if (snapshot_file != NULL) {
		if (access(snapshot_file, W_OK | R_OK) != 0) {
			if (errno == ENOENT)
				method = TEST_BUILD_SNAPSHOT;
			else {
				printf("can't access the file %s\n",
				    snapshot_file);

				rv = -1;
				goto fin;
			}
		} else {
			if (method == TEST_BUILD_SNAPSHOT) {
				rv = 0;
				goto fin;
			}

			TEST_SNAPSHOT_FILE_READ(rpcent, snapshot_file,
				&td_snap, rpcent_read_snapshot_func);
		}
	}

	rv = rpcent_fill_test_data(&td);
	if (rv == -1)
		return (-1);
	switch (method) {
	case TEST_GETRPCBYNAME:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(rpcent, &td,
				rpcent_test_getrpcbyname, (void *)&td);
		else
			rv = DO_1PASS_TEST(rpcent, &td_snap,
				rpcent_test_getrpcbyname, (void *)&td_snap);
		break;
	case TEST_GETRPCBYNUMBER:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(rpcent, &td,
				rpcent_test_getrpcbynumber, (void *)&td);
		else
			rv = DO_1PASS_TEST(rpcent, &td_snap,
				rpcent_test_getrpcbynumber, (void *)&td_snap);
		break;
	case TEST_GETRPCENT:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(rpcent, &td, rpcent_test_getrpcent,
				(void *)&td);
		else
			rv = DO_2PASS_TEST(rpcent, &td, &td_snap,
				compare_rpcent, NULL);
		break;
	case TEST_GETRPCENT_2PASS:
			TEST_DATA_INIT(rpcent, &td_2pass, clone_rpcent, free_rpcent);
			rv = rpcent_fill_test_data(&td_2pass);
			if (rv != -1)
				rv = DO_2PASS_TEST(rpcent, &td, &td_2pass,
					compare_rpcent, NULL);
			TEST_DATA_DESTROY(rpcent, &td_2pass);
		break;
	case TEST_BUILD_SNAPSHOT:
		if (snapshot_file != NULL)
		    rv = TEST_SNAPSHOT_FILE_WRITE(rpcent, snapshot_file, &td,
			sdump_rpcent);
		break;
	default:
		rv = 0;
		break;
	}

fin:
	TEST_DATA_DESTROY(rpcent, &td_snap);
	TEST_DATA_DESTROY(rpcent, &td);

	return (rv);
}

#define	SNAPSHOT_FILE	"snapshot_rpc"

ATF_TC_WITHOUT_HEAD(build_snapshot);
ATF_TC_BODY(build_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
}

ATF_TC_WITHOUT_HEAD(getrpcbyname);
ATF_TC_BODY(getrpcbyname, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETRPCBYNAME) == 0);
}

ATF_TC_WITHOUT_HEAD(getrpcbyname_with_snapshot);
ATF_TC_BODY(getrpcbyname_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETRPCBYNAME) == 0);
}

ATF_TC_WITHOUT_HEAD(getrpcbynumber);
ATF_TC_BODY(getrpcbynumber, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETRPCBYNUMBER) == 0);
}

ATF_TC_WITHOUT_HEAD(getrpcbynumber_with_snapshot);
ATF_TC_BODY(getrpcbynumber_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETRPCBYNUMBER) == 0);
}

ATF_TC_WITHOUT_HEAD(getrpcbyent);
ATF_TC_BODY(getrpcbyent, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETRPCENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getrpcbyent_with_snapshot);
ATF_TC_BODY(getrpcbyent_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETRPCENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getrpcbyent_with_two_pass);
ATF_TC_BODY(getrpcbyent_with_two_pass, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETRPCENT_2PASS) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, build_snapshot);
	ATF_TP_ADD_TC(tp, getrpcbyname);
	ATF_TP_ADD_TC(tp, getrpcbyname_with_snapshot);
	ATF_TP_ADD_TC(tp, getrpcbynumber);
	ATF_TP_ADD_TC(tp, getrpcbynumber_with_snapshot);
	ATF_TP_ADD_TC(tp, getrpcbyent);
	ATF_TP_ADD_TC(tp, getrpcbyent_with_snapshot);
	ATF_TP_ADD_TC(tp, getrpcbyent_with_two_pass);

	return (atf_no_error());
}
