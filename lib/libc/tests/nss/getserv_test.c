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
	TEST_GETSERVENT,
	TEST_GETSERVBYNAME,
	TEST_GETSERVBYPORT,
	TEST_GETSERVENT_2PASS,
	TEST_BUILD_SNAPSHOT
};

DECLARE_TEST_DATA(servent)
DECLARE_TEST_FILE_SNAPSHOT(servent)
DECLARE_1PASS_TEST(servent)
DECLARE_2PASS_TEST(servent)

static void clone_servent(struct servent *, struct servent const *);
static int compare_servent(struct servent *, struct servent *, void *);
static void dump_servent(struct servent *);
static void free_servent(struct servent *);

static void sdump_servent(struct servent *, char *, size_t);
static int servent_read_snapshot_func(struct servent *, char *);

static int servent_check_ambiguity(struct servent_test_data *,
	struct servent *);
static int servent_fill_test_data(struct servent_test_data *);
static int servent_test_correctness(struct servent *, void *);
static int servent_test_getservbyname(struct servent *, void *);
static int servent_test_getservbyport(struct servent *, void *);
static int servent_test_getservent(struct servent *, void *);

IMPLEMENT_TEST_DATA(servent)
IMPLEMENT_TEST_FILE_SNAPSHOT(servent)
IMPLEMENT_1PASS_TEST(servent)
IMPLEMENT_2PASS_TEST(servent)

static void
clone_servent(struct servent *dest, struct servent const *src)
{
	ATF_REQUIRE(dest != NULL);
	ATF_REQUIRE(src != NULL);

	char **cp;
	int aliases_num;

	memset(dest, 0, sizeof(struct servent));

	if (src->s_name != NULL) {
		dest->s_name = strdup(src->s_name);
		ATF_REQUIRE(dest->s_name != NULL);
	}

	if (src->s_proto != NULL) {
		dest->s_proto = strdup(src->s_proto);
		ATF_REQUIRE(dest->s_proto != NULL);
	}
	dest->s_port = src->s_port;

	if (src->s_aliases != NULL) {
		aliases_num = 0;
		for (cp = src->s_aliases; *cp; ++cp)
			++aliases_num;

		dest->s_aliases = calloc(aliases_num + 1, sizeof(char *));
		ATF_REQUIRE(dest->s_aliases != NULL);

		for (cp = src->s_aliases; *cp; ++cp) {
			dest->s_aliases[cp - src->s_aliases] = strdup(*cp);
			ATF_REQUIRE(dest->s_aliases[cp - src->s_aliases] != NULL);
		}
	}
}

static void
free_servent(struct servent *serv)
{
	char **cp;

	ATF_REQUIRE(serv != NULL);

	free(serv->s_name);
	free(serv->s_proto);

	for (cp = serv->s_aliases; *cp; ++cp)
		free(*cp);
	free(serv->s_aliases);
}

static  int
compare_servent(struct servent *serv1, struct servent *serv2, void *mdata)
{
	char **c1, **c2;

	if (serv1 == serv2)
		return 0;

	if ((serv1 == NULL) || (serv2 == NULL))
		goto errfin;

	if ((strcmp(serv1->s_name, serv2->s_name) != 0) ||
		(strcmp(serv1->s_proto, serv2->s_proto) != 0) ||
		(serv1->s_port != serv2->s_port))
			goto errfin;

	c1 = serv1->s_aliases;
	c2 = serv2->s_aliases;

	if ((serv1->s_aliases == NULL) || (serv2->s_aliases == NULL))
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
		dump_servent(serv1);
		dump_servent(serv2);
	}

	return (-1);
}

static void
sdump_servent(struct servent *serv, char *buffer, size_t buflen)
{
	char **cp;
	int written;

	written = snprintf(buffer, buflen, "%s %d %s",
		serv->s_name, ntohs(serv->s_port), serv->s_proto);
	buffer += written;
	if (written > (int)buflen)
		return;
	buflen -= written;

	if (serv->s_aliases != NULL) {
		if (*(serv->s_aliases) != '\0') {
			for (cp = serv->s_aliases; *cp; ++cp) {
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
servent_read_snapshot_func(struct servent *serv, char *line)
{
	StringList *sl;
	char *s, *ps, *ts;
	int i;

	printf("1 line read from snapshot:\n%s\n", line);

	i = 0;
	sl = NULL;
	ps = line;
	memset(serv, 0, sizeof(struct servent));
	while ( (s = strsep(&ps, " ")) != NULL) {
		switch (i) {
			case 0:
				serv->s_name = strdup(s);
				ATF_REQUIRE(serv->s_name != NULL);
			break;

			case 1:
				serv->s_port = htons(
					(int)strtol(s, &ts, 10));
				if (*ts != '\0') {
					free(serv->s_name);
					return (-1);
				}
			break;

			case 2:
				serv->s_proto = strdup(s);
				ATF_REQUIRE(serv->s_proto != NULL);
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
		++i;
	}

	if (i < 3) {
		free(serv->s_name);
		free(serv->s_proto);
		memset(serv, 0, sizeof(struct servent));
		return (-1);
	}

	sl_add(sl, NULL);
	serv->s_aliases = sl->sl_str;

	/* NOTE: is it a dirty hack or not? */
	free(sl);
	return (0);
}

static void
dump_servent(struct servent *result)
{
	if (result != NULL) {
		char buffer[1024];
		sdump_servent(result, buffer, sizeof(buffer));
		printf("%s\n", buffer);
	} else
		printf("(null)\n");
}

static int
servent_fill_test_data(struct servent_test_data *td)
{
	struct servent *serv;

	setservent(1);
	while ((serv = getservent()) != NULL) {
		if (servent_test_correctness(serv, NULL) == 0)
			TEST_DATA_APPEND(servent, td, serv);
		else
			return (-1);
	}
	endservent();

	return (0);
}

static int
servent_test_correctness(struct servent *serv, void *mdata __unused)
{
	printf("testing correctness with the following data:\n");
	dump_servent(serv);

	if (serv == NULL)
		goto errfin;

	if (serv->s_name == NULL)
		goto errfin;

	if (serv->s_proto == NULL)
		goto errfin;

	if (ntohs(serv->s_port < 0))
		goto errfin;

	if (serv->s_aliases == NULL)
		goto errfin;

	printf("correct\n");

	return (0);
errfin:
	printf("incorrect\n");

	return (-1);
}

/* servent_check_ambiguity() is needed when one port+proto is associated with
 * more than one service (these cases are usually marked as PROBLEM in
 * /etc/services. This functions is needed also when one service+proto is
 * associated with several ports. We have to check all the servent structures
 * to make sure that serv really exists and correct */
static int
servent_check_ambiguity(struct servent_test_data *td, struct servent *serv)
{

	return (TEST_DATA_FIND(servent, td, serv, compare_servent,
		NULL) != NULL ? 0 : -1);
}

static int
servent_test_getservbyname(struct servent *serv_model, void *mdata)
{
	char **alias;
	struct servent *serv;

	printf("testing getservbyname() with the following data:\n");
	dump_servent(serv_model);

	serv = getservbyname(serv_model->s_name, serv_model->s_proto);
	if (servent_test_correctness(serv, NULL) != 0)
		goto errfin;

	if ((compare_servent(serv, serv_model, NULL) != 0) &&
	    (servent_check_ambiguity((struct servent_test_data *)mdata, serv)
	    !=0))
		goto errfin;

	for (alias = serv_model->s_aliases; *alias; ++alias) {
		serv = getservbyname(*alias, serv_model->s_proto);

		if (servent_test_correctness(serv, NULL) != 0)
			goto errfin;

		if ((compare_servent(serv, serv_model, NULL) != 0) &&
		    (servent_check_ambiguity(
		    (struct servent_test_data *)mdata, serv) != 0))
		    goto errfin;
	}

	printf("ok\n");
	return (0);

errfin:
	printf("not ok\n");

	return (-1);
}

static int
servent_test_getservbyport(struct servent *serv_model, void *mdata)
{
	struct servent *serv;

	printf("testing getservbyport() with the following data...\n");
	dump_servent(serv_model);

	serv = getservbyport(serv_model->s_port, serv_model->s_proto);
	if ((servent_test_correctness(serv, NULL) != 0) ||
	    ((compare_servent(serv, serv_model, NULL) != 0) &&
	    (servent_check_ambiguity((struct servent_test_data *)mdata, serv)
	    != 0))) {
		printf("not ok\n");
		return (-1);
	} else {
		printf("ok\n");
		return (0);
	}
}

static int
servent_test_getservent(struct servent *serv, void *mdata __unused)
{
	/* Only correctness can be checked when doing 1-pass test for
	 * getservent(). */
	return (servent_test_correctness(serv, NULL));
}

static int
run_tests(const char *snapshot_file, enum test_methods method)
{
	struct servent_test_data td, td_snap, td_2pass;
	int rv;

	TEST_DATA_INIT(servent, &td, clone_servent, free_servent);
	TEST_DATA_INIT(servent, &td_snap, clone_servent, free_servent);
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

			TEST_SNAPSHOT_FILE_READ(servent, snapshot_file,
				&td_snap, servent_read_snapshot_func);
		}
	}

	rv = servent_fill_test_data(&td);
	if (rv == -1)
		return (-1);
	switch (method) {
	case TEST_GETSERVBYNAME:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(servent, &td,
				servent_test_getservbyname, (void *)&td);
		else
			rv = DO_1PASS_TEST(servent, &td_snap,
				servent_test_getservbyname, (void *)&td_snap);
		break;
	case TEST_GETSERVBYPORT:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(servent, &td,
				servent_test_getservbyport, (void *)&td);
		else
			rv = DO_1PASS_TEST(servent, &td_snap,
				servent_test_getservbyport, (void *)&td_snap);
		break;
	case TEST_GETSERVENT:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(servent, &td, servent_test_getservent,
				(void *)&td);
		else
			rv = DO_2PASS_TEST(servent, &td, &td_snap,
				compare_servent, NULL);
		break;
	case TEST_GETSERVENT_2PASS:
			TEST_DATA_INIT(servent, &td_2pass, clone_servent, free_servent);
			rv = servent_fill_test_data(&td_2pass);
			if (rv != -1)
				rv = DO_2PASS_TEST(servent, &td, &td_2pass,
					compare_servent, NULL);
			TEST_DATA_DESTROY(servent, &td_2pass);
		break;
	case TEST_BUILD_SNAPSHOT:
		if (snapshot_file != NULL)
		    rv = TEST_SNAPSHOT_FILE_WRITE(servent, snapshot_file, &td,
			sdump_servent);
		break;
	default:
		rv = 0;
		break;
	}

fin:
	TEST_DATA_DESTROY(servent, &td_snap);
	TEST_DATA_DESTROY(servent, &td);

	return (rv);
}

#define	SNAPSHOT_FILE	"snapshot_serv"

ATF_TC_WITHOUT_HEAD(build_snapshot);
ATF_TC_BODY(build_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
}

ATF_TC_WITHOUT_HEAD(getservbyname);
ATF_TC_BODY(getservbyname, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETSERVBYNAME) == 0);
}

ATF_TC_WITHOUT_HEAD(getservbyname_with_snapshot);
ATF_TC_BODY(getservbyname_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETSERVBYNAME) == 0);
}

ATF_TC_WITHOUT_HEAD(getservbyport);
ATF_TC_BODY(getservbyport, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETSERVBYPORT) == 0);
}

ATF_TC_WITHOUT_HEAD(getservbyport_with_snapshot);
ATF_TC_BODY(getservbyport_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETSERVBYPORT) == 0);
}

ATF_TC_WITHOUT_HEAD(getservbyent);
ATF_TC_BODY(getservbyent, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETSERVENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getservbyent_with_snapshot);
ATF_TC_BODY(getservbyent_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETSERVENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getservbyent_with_two_pass);
ATF_TC_BODY(getservbyent_with_two_pass, tc)
{

	ATF_REQUIRE(run_tests(NULL, TEST_GETSERVENT_2PASS) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, build_snapshot);
	ATF_TP_ADD_TC(tp, getservbyent);
	ATF_TP_ADD_TC(tp, getservbyent_with_snapshot);
	ATF_TP_ADD_TC(tp, getservbyent_with_two_pass);
	ATF_TP_ADD_TC(tp, getservbyname);
	ATF_TP_ADD_TC(tp, getservbyname_with_snapshot);
	ATF_TP_ADD_TC(tp, getservbyport);
	ATF_TP_ADD_TC(tp, getservbyport_with_snapshot);

	return (atf_no_error());
}
