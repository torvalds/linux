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
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>
#include <unistd.h>

#include <atf-c.h>

#include "testutil.h"

enum test_methods {
	TEST_GETPROTOENT,
	TEST_GETPROTOBYNAME,
	TEST_GETPROTOBYNUMBER,
	TEST_GETPROTOENT_2PASS,
	TEST_BUILD_SNAPSHOT
};

DECLARE_TEST_DATA(protoent)
DECLARE_TEST_FILE_SNAPSHOT(protoent)
DECLARE_1PASS_TEST(protoent)
DECLARE_2PASS_TEST(protoent)

static void clone_protoent(struct protoent *, struct protoent const *);
static int compare_protoent(struct protoent *, struct protoent *, void *);
static void dump_protoent(struct protoent *);
static void free_protoent(struct protoent *);

static void sdump_protoent(struct protoent *, char *, size_t);
static int protoent_read_snapshot_func(struct protoent *, char *);

static int protoent_check_ambiguity(struct protoent_test_data *,
	struct protoent *);
static int protoent_fill_test_data(struct protoent_test_data *);
static int protoent_test_correctness(struct protoent *, void *);
static int protoent_test_getprotobyname(struct protoent *, void *);
static int protoent_test_getprotobynumber(struct protoent *, void *);
static int protoent_test_getprotoent(struct protoent *, void *);

IMPLEMENT_TEST_DATA(protoent)
IMPLEMENT_TEST_FILE_SNAPSHOT(protoent)
IMPLEMENT_1PASS_TEST(protoent)
IMPLEMENT_2PASS_TEST(protoent)

static void
clone_protoent(struct protoent *dest, struct protoent const *src)
{
	assert(dest != NULL);
	assert(src != NULL);

	char **cp;
	int aliases_num;

	memset(dest, 0, sizeof(struct protoent));

	if (src->p_name != NULL) {
		dest->p_name = strdup(src->p_name);
		assert(dest->p_name != NULL);
	}

	dest->p_proto = src->p_proto;

	if (src->p_aliases != NULL) {
		aliases_num = 0;
		for (cp = src->p_aliases; *cp; ++cp)
			++aliases_num;

		dest->p_aliases = calloc(aliases_num + 1, sizeof(char *));
		assert(dest->p_aliases != NULL);

		for (cp = src->p_aliases; *cp; ++cp) {
			dest->p_aliases[cp - src->p_aliases] = strdup(*cp);
			assert(dest->p_aliases[cp - src->p_aliases] != NULL);
		}
	}
}

static void
free_protoent(struct protoent *pe)
{
	char **cp;

	assert(pe != NULL);

	free(pe->p_name);

	for (cp = pe->p_aliases; *cp; ++cp)
		free(*cp);
	free(pe->p_aliases);
}

static  int
compare_protoent(struct protoent *pe1, struct protoent *pe2, void *mdata)
{
	char **c1, **c2;

	if (pe1 == pe2)
		return 0;

	if ((pe1 == NULL) || (pe2 == NULL))
		goto errfin;

	if ((strcmp(pe1->p_name, pe2->p_name) != 0) ||
		(pe1->p_proto != pe2->p_proto))
			goto errfin;

	c1 = pe1->p_aliases;
	c2 = pe2->p_aliases;

	if ((pe1->p_aliases == NULL) || (pe2->p_aliases == NULL))
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
		dump_protoent(pe1);
		dump_protoent(pe2);
	}

	return (-1);
}

static void
sdump_protoent(struct protoent *pe, char *buffer, size_t buflen)
{
	char **cp;
	int written;

	written = snprintf(buffer, buflen, "%s %d",
		pe->p_name, pe->p_proto);
	buffer += written;
	if (written > (int)buflen)
		return;
	buflen -= written;

	if (pe->p_aliases != NULL) {
		if (*(pe->p_aliases) != '\0') {
			for (cp = pe->p_aliases; *cp; ++cp) {
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
protoent_read_snapshot_func(struct protoent *pe, char *line)
{
	StringList *sl;
	char *s, *ps, *ts;
	int i;

	printf("1 line read from snapshot:\n%s\n", line);

	i = 0;
	sl = NULL;
	ps = line;
	memset(pe, 0, sizeof(struct protoent));
	while ( (s = strsep(&ps, " ")) != NULL) {
		switch (i) {
			case 0:
				pe->p_name = strdup(s);
				assert(pe->p_name != NULL);
			break;

			case 1:
				pe->p_proto = (int)strtol(s, &ts, 10);
				if (*ts != '\0') {
					free(pe->p_name);
					return (-1);
				}
			break;

			default:
				if (sl == NULL) {
					if (strcmp(s, "(null)") == 0)
						return (0);

					sl = sl_init();
					assert(sl != NULL);

					if (strcmp(s, "noaliases") != 0) {
						ts = strdup(s);
						assert(ts != NULL);
						sl_add(sl, ts);
					}
				} else {
					ts = strdup(s);
					assert(ts != NULL);
					sl_add(sl, ts);
				}
			break;
		}
		++i;
	}

	if (i < 3) {
		free(pe->p_name);
		memset(pe, 0, sizeof(struct protoent));
		return (-1);
	}

	sl_add(sl, NULL);
	pe->p_aliases = sl->sl_str;

	/* NOTE: is it a dirty hack or not? */
	free(sl);
	return (0);
}

static void
dump_protoent(struct protoent *result)
{
	if (result != NULL) {
		char buffer[1024];
		sdump_protoent(result, buffer, sizeof(buffer));
		printf("%s\n", buffer);
	} else
		printf("(null)\n");
}

static int
protoent_fill_test_data(struct protoent_test_data *td)
{
	struct protoent *pe;

	setprotoent(1);
	while ((pe = getprotoent()) != NULL) {
		if (protoent_test_correctness(pe, NULL) == 0)
			TEST_DATA_APPEND(protoent, td, pe);
		else
			return (-1);
	}
	endprotoent();

	return (0);
}

static int
protoent_test_correctness(struct protoent *pe, void *mdata __unused)
{
	printf("testing correctness with the following data:\n");
	dump_protoent(pe);

	if (pe == NULL)
		goto errfin;

	if (pe->p_name == NULL)
		goto errfin;

	if (pe->p_proto < 0)
		goto errfin;

	if (pe->p_aliases == NULL)
		goto errfin;

	printf("correct\n");

	return (0);
errfin:
	printf("incorrect\n");

	return (-1);
}

/* protoent_check_ambiguity() is needed when one port+proto is associated with
 * more than one piece (these cases are usually marked as PROBLEM in
 * /etc/peices. This functions is needed also when one piece+proto is
 * associated with several ports. We have to check all the protoent structures
 * to make sure that pe really exists and correct */
static int
protoent_check_ambiguity(struct protoent_test_data *td, struct protoent *pe)
{

	return (TEST_DATA_FIND(protoent, td, pe, compare_protoent,
		NULL) != NULL ? 0 : -1);
}

static int
protoent_test_getprotobyname(struct protoent *pe_model, void *mdata)
{
	char **alias;
	struct protoent *pe;

	printf("testing getprotobyname() with the following data:\n");
	dump_protoent(pe_model);

	pe = getprotobyname(pe_model->p_name);
	if (protoent_test_correctness(pe, NULL) != 0)
		goto errfin;

	if ((compare_protoent(pe, pe_model, NULL) != 0) &&
	    (protoent_check_ambiguity((struct protoent_test_data *)mdata, pe)
	    !=0))
	    goto errfin;

	for (alias = pe_model->p_aliases; *alias; ++alias) {
		pe = getprotobyname(*alias);

		if (protoent_test_correctness(pe, NULL) != 0)
			goto errfin;

		if ((compare_protoent(pe, pe_model, NULL) != 0) &&
		    (protoent_check_ambiguity(
		    (struct protoent_test_data *)mdata, pe) != 0))
		    goto errfin;
	}

	printf("ok\n");
	return (0);

errfin:
	printf("not ok\n");

	return (-1);
}

static int
protoent_test_getprotobynumber(struct protoent *pe_model, void *mdata)
{
	struct protoent *pe;

	printf("testing getprotobyport() with the following data...\n");
	dump_protoent(pe_model);

	pe = getprotobynumber(pe_model->p_proto);
	if ((protoent_test_correctness(pe, NULL) != 0) ||
	    ((compare_protoent(pe, pe_model, NULL) != 0) &&
	    (protoent_check_ambiguity((struct protoent_test_data *)mdata, pe)
	    != 0))) {
		printf("not ok\n");
		return (-1);
	} else {
		printf("ok\n");
		return (0);
	}
}

static int
protoent_test_getprotoent(struct protoent *pe, void *mdata __unused)
{
	/* Only correctness can be checked when doing 1-pass test for
	 * getprotoent(). */
	return (protoent_test_correctness(pe, NULL));
}

static int
run_tests(const char *snapshot_file, enum test_methods method)
{
	struct protoent_test_data td, td_snap, td_2pass;
	int rv;

	TEST_DATA_INIT(protoent, &td, clone_protoent, free_protoent);
	TEST_DATA_INIT(protoent, &td_snap, clone_protoent, free_protoent);
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

			TEST_SNAPSHOT_FILE_READ(protoent, snapshot_file,
				&td_snap, protoent_read_snapshot_func);
		}
	}

	rv = protoent_fill_test_data(&td);
	if (rv == -1)
		return (-1);
	switch (method) {
	case TEST_GETPROTOBYNAME:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(protoent, &td,
				protoent_test_getprotobyname, (void *)&td);
		else
			rv = DO_1PASS_TEST(protoent, &td_snap,
				protoent_test_getprotobyname, (void *)&td_snap);
		break;
	case TEST_GETPROTOBYNUMBER:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(protoent, &td,
				protoent_test_getprotobynumber, (void *)&td);
		else
			rv = DO_1PASS_TEST(protoent, &td_snap,
				protoent_test_getprotobynumber, (void *)&td_snap);
		break;
	case TEST_GETPROTOENT:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(protoent, &td,
				protoent_test_getprotoent, (void *)&td);
		else
			rv = DO_2PASS_TEST(protoent, &td, &td_snap,
				compare_protoent, NULL);
		break;
	case TEST_GETPROTOENT_2PASS:
		TEST_DATA_INIT(protoent, &td_2pass, clone_protoent,
		    free_protoent);
		rv = protoent_fill_test_data(&td_2pass);
		if (rv != -1)
			rv = DO_2PASS_TEST(protoent, &td, &td_2pass,
				compare_protoent, NULL);
		TEST_DATA_DESTROY(protoent, &td_2pass);
		break;
	case TEST_BUILD_SNAPSHOT:
		if (snapshot_file != NULL)
			rv = TEST_SNAPSHOT_FILE_WRITE(protoent, snapshot_file,
			    &td, sdump_protoent);
		break;
	default:
		rv = 0;
		break;
	}

fin:
	TEST_DATA_DESTROY(protoent, &td_snap);
	TEST_DATA_DESTROY(protoent, &td);

	return (rv);
}

#define	SNAPSHOT_FILE	"snapshot_proto"

ATF_TC_WITHOUT_HEAD(build_snapshot);
ATF_TC_BODY(build_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
}

ATF_TC_WITHOUT_HEAD(getprotoent);
ATF_TC_BODY(getprotoent, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETPROTOENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getprotoent_with_snapshot);
ATF_TC_BODY(getprotoent_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPROTOENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getprotoent_with_two_pass);
ATF_TC_BODY(getprotoent_with_two_pass, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETPROTOENT_2PASS) == 0);
}

ATF_TC_WITHOUT_HEAD(getprotobyname);
ATF_TC_BODY(getprotobyname, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETPROTOBYNAME) == 0);
}

ATF_TC_WITHOUT_HEAD(getprotobyname_with_snapshot);
ATF_TC_BODY(getprotobyname_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPROTOBYNAME) == 0);
}

ATF_TC_WITHOUT_HEAD(getprotobynumber);
ATF_TC_BODY(getprotobynumber, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETPROTOBYNUMBER) == 0);
}

ATF_TC_WITHOUT_HEAD(getprotobynumber_with_snapshot);
ATF_TC_BODY(getprotobynumber_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPROTOBYNUMBER) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, build_snapshot);
	ATF_TP_ADD_TC(tp, getprotoent);
	ATF_TP_ADD_TC(tp, getprotoent_with_snapshot);
	ATF_TP_ADD_TC(tp, getprotoent_with_two_pass);
	ATF_TP_ADD_TC(tp, getprotobyname);
	ATF_TP_ADD_TC(tp, getprotobyname_with_snapshot);
	ATF_TP_ADD_TC(tp, getprotobynumber);
	ATF_TP_ADD_TC(tp, getprotobynumber_with_snapshot);

	return (atf_no_error());
}
