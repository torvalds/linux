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
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>
#include <unistd.h>

#include <atf-c.h>

#include "testutil.h"

enum test_methods {
	TEST_GETGRENT = 1,
	TEST_GETGRNAM = 2,
	TEST_GETGRGID = 4,
	TEST_GETGRENT_2PASS = 8,
	TEST_BUILD_SNAPSHOT = 16,
};

DECLARE_TEST_DATA(group)
DECLARE_TEST_FILE_SNAPSHOT(group)
DECLARE_1PASS_TEST(group)
DECLARE_2PASS_TEST(group)

static void clone_group(struct group *, struct group const *);
static int compare_group(struct group *, struct group *, void *);
static void dump_group(struct group *);
static void free_group(struct group *);

static void sdump_group(struct group *, char *, size_t);
static int group_read_snapshot_func(struct group *, char *);

static int group_check_ambiguity(struct group_test_data *,
	struct group *);
static int group_fill_test_data(struct group_test_data *);
static int group_test_correctness(struct group *, void *);
static int group_test_getgrnam(struct group *, void *);
static int group_test_getgrgid(struct group *, void *);
static int group_test_getgrent(struct group *, void *);

IMPLEMENT_TEST_DATA(group)
IMPLEMENT_TEST_FILE_SNAPSHOT(group)
IMPLEMENT_1PASS_TEST(group)
IMPLEMENT_2PASS_TEST(group)

static void
clone_group(struct group *dest, struct group const *src)
{
	ATF_REQUIRE(dest != NULL);
	ATF_REQUIRE(src != NULL);

	char **cp;
	int members_num;

	memset(dest, 0, sizeof(struct group));

	if (src->gr_name != NULL) {
		dest->gr_name = strdup(src->gr_name);
		ATF_REQUIRE(dest->gr_name != NULL);
	}

	if (src->gr_passwd != NULL) {
		dest->gr_passwd = strdup(src->gr_passwd);
		ATF_REQUIRE(dest->gr_passwd != NULL);
	}
	dest->gr_gid = src->gr_gid;

	if (src->gr_mem != NULL) {
		members_num = 0;
		for (cp = src->gr_mem; *cp; ++cp)
			++members_num;

		dest->gr_mem = calloc(members_num + 1, sizeof(char *));
		ATF_REQUIRE(dest->gr_mem != NULL);

		for (cp = src->gr_mem; *cp; ++cp) {
			dest->gr_mem[cp - src->gr_mem] = strdup(*cp);
			ATF_REQUIRE(dest->gr_mem[cp - src->gr_mem] != NULL);
		}
	}
}

static void
free_group(struct group *grp)
{
	char **cp;

	ATF_REQUIRE(grp != NULL);

	free(grp->gr_name);
	free(grp->gr_passwd);

	for (cp = grp->gr_mem; *cp; ++cp)
		free(*cp);
	free(grp->gr_mem);
}

static  int
compare_group(struct group *grp1, struct group *grp2, void *mdata)
{
	char **c1, **c2;

	if (grp1 == grp2)
		return (0);

	if (grp1 == NULL || grp2 == NULL)
		goto errfin;

	if (strcmp(grp1->gr_name, grp2->gr_name) != 0 ||
	    strcmp(grp1->gr_passwd, grp2->gr_passwd) != 0 ||
	    grp1->gr_gid != grp2->gr_gid)
			goto errfin;

	c1 = grp1->gr_mem;
	c2 = grp2->gr_mem;

	if (grp1->gr_mem == NULL || grp2->gr_mem == NULL)
		goto errfin;

	for (; *c1 && *c2; ++c1, ++c2)
		if (strcmp(*c1, *c2) != 0)
			goto errfin;

	if (*c1 != '\0' || *c2 != '\0')
		goto errfin;

	return 0;

errfin:
	if (mdata == NULL) {
		printf("following structures are not equal:\n");
		dump_group(grp1);
		dump_group(grp2);
	}

	return (-1);
}

static void
sdump_group(struct group *grp, char *buffer, size_t buflen)
{
	char **cp;
	int written;

	written = snprintf(buffer, buflen, "%s:%s:%d:",
		grp->gr_name, grp->gr_passwd, grp->gr_gid);
	buffer += written;
	if (written > (int)buflen)
		return;
	buflen -= written;

	if (grp->gr_mem != NULL) {
		if (*(grp->gr_mem) != '\0') {
			for (cp = grp->gr_mem; *cp; ++cp) {
				written = snprintf(buffer, buflen, "%s%s",
				    cp == grp->gr_mem ? "" : ",", *cp);
				buffer += written;
				if (written > (int)buflen)
					return;
				buflen -= written;

				if (buflen == 0)
					return;
			}
		} else
			snprintf(buffer, buflen, "nomem");
	} else
		snprintf(buffer, buflen, "(null)");
}

static int
group_read_snapshot_func(struct group *grp, char *line)
{
	StringList *sl;
	char *s, *ps, *ts;
	const char *sep;
	int i;

	printf("1 line read from snapshot:\n%s\n", line);

	i = 0;
	sl = NULL;
	ps = line;
	sep = ":";
	memset(grp, 0, sizeof(struct group));
	while ((s = strsep(&ps, sep)) != NULL) {
		switch (i) {
		case 0:
			grp->gr_name = strdup(s);
			ATF_REQUIRE(grp->gr_name != NULL);
			break;

		case 1:
			grp->gr_passwd = strdup(s);
			ATF_REQUIRE(grp->gr_passwd != NULL);
			break;

		case 2:
			grp->gr_gid = (gid_t)strtol(s, &ts, 10);
			if (*ts != '\0') {
				free(grp->gr_name);
				free(grp->gr_passwd);
				grp->gr_name = NULL;
				grp->gr_passwd = NULL;
				return (-1);
			}
			/* Change to parsing groups. */
			sep = ",";
			break;

		default:
			if (sl == NULL) {
				if (strcmp(s, "(null)") == 0)
					return (0);

				sl = sl_init();
				ATF_REQUIRE(sl != NULL);

				if (strcmp(s, "nomem") != 0) {
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
		free(grp->gr_name);
		free(grp->gr_passwd);
		memset(grp, 0, sizeof(struct group));
		return (-1);
	}

	sl_add(sl, NULL);
	grp->gr_mem = sl->sl_str;

	/* NOTE: is it a dirty hack or not? */
	free(sl);
	return (0);
}

static void
dump_group(struct group *result)
{
	if (result != NULL) {
		char buffer[1024];
		sdump_group(result, buffer, sizeof(buffer));
		printf("%s\n", buffer);
	} else
		printf("(null)\n");
}

static int
group_fill_test_data(struct group_test_data *td)
{
	struct group *grp;

	setgroupent(1);
	while ((grp = getgrent()) != NULL) {
		if (group_test_correctness(grp, NULL) == 0)
			TEST_DATA_APPEND(group, td, grp);
		else
			return (-1);
	}
	endgrent();

	return (0);
}

static int
group_test_correctness(struct group *grp, void *mdata __unused)
{
	printf("testing correctness with the following data:\n");
	dump_group(grp);

	if (grp == NULL)
		goto errfin;

	if (grp->gr_name == NULL)
		goto errfin;

	if (grp->gr_passwd == NULL)
		goto errfin;

	if (grp->gr_mem == NULL)
		goto errfin;

	printf("correct\n");

	return (0);
errfin:
	printf("incorrect\n");

	return (-1);
}

/* group_check_ambiguity() is needed here because when doing the getgrent()
 * calls sequence, records from different nsswitch sources can be different,
 * though having the same pw_name/pw_uid */
static int
group_check_ambiguity(struct group_test_data *td, struct group *pwd)
{

	return (TEST_DATA_FIND(group, td, pwd, compare_group,
		NULL) != NULL ? 0 : -1);
}

static int
group_test_getgrnam(struct group *grp_model, void *mdata)
{
	struct group *grp;

	printf("testing getgrnam() with the following data:\n");
	dump_group(grp_model);

	grp = getgrnam(grp_model->gr_name);
	if (group_test_correctness(grp, NULL) != 0)
		goto errfin;

	if (compare_group(grp, grp_model, NULL) != 0 &&
	    group_check_ambiguity((struct group_test_data *)mdata, grp) != 0)
	    goto errfin;

	return (0);

errfin:
	return (-1);
}

static int
group_test_getgrgid(struct group *grp_model, void *mdata)
{
	struct group *grp;

	printf("testing getgrgid() with the following data...\n");
	dump_group(grp_model);

	grp = getgrgid(grp_model->gr_gid);
	if (group_test_correctness(grp, NULL) != 0 ||
	    (compare_group(grp, grp_model, NULL) != 0 &&
	     group_check_ambiguity((struct group_test_data *)mdata, grp) != 0)) {
		return (-1);
	} else {
		return (0);
	}
}

static int
group_test_getgrent(struct group *grp, void *mdata __unused)
{
	/* Only correctness can be checked when doing 1-pass test for
	 * getgrent(). */
	return (group_test_correctness(grp, NULL));
}

static int
run_tests(const char *snapshot_file, enum test_methods method)
{
	struct group_test_data td, td_snap, td_2pass;
	int rv;

	TEST_DATA_INIT(group, &td, clone_group, free_group);
	TEST_DATA_INIT(group, &td_snap, clone_group, free_group);
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

			TEST_SNAPSHOT_FILE_READ(group, snapshot_file,
				&td_snap, group_read_snapshot_func);
		}
	}

	rv = group_fill_test_data(&td);
	if (rv == -1)
		return (-1);
	switch (method) {
	case TEST_GETGRNAM:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(group, &td,
				group_test_getgrnam, (void *)&td);
		else
			rv = DO_1PASS_TEST(group, &td_snap,
				group_test_getgrnam, (void *)&td_snap);
		break;
	case TEST_GETGRGID:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(group, &td,
				group_test_getgrgid, (void *)&td);
		else
			rv = DO_1PASS_TEST(group, &td_snap,
				group_test_getgrgid, (void *)&td_snap);
		break;
	case TEST_GETGRENT:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(group, &td, group_test_getgrent,
				(void *)&td);
		else
			rv = DO_2PASS_TEST(group, &td, &td_snap,
				compare_group, NULL);
		break;
	case TEST_GETGRENT_2PASS:
			TEST_DATA_INIT(group, &td_2pass, clone_group, free_group);
			rv = group_fill_test_data(&td_2pass);
			if (rv != -1)
				rv = DO_2PASS_TEST(group, &td, &td_2pass,
					compare_group, NULL);
			TEST_DATA_DESTROY(group, &td_2pass);
		break;
	case TEST_BUILD_SNAPSHOT:
		if (snapshot_file != NULL)
			rv = TEST_SNAPSHOT_FILE_WRITE(group, snapshot_file, &td,
			    sdump_group);
		break;
	default:
		rv = 0;
		break;
	}

fin:
	TEST_DATA_DESTROY(group, &td_snap);
	TEST_DATA_DESTROY(group, &td);

	return (rv);
}

#define	SNAPSHOT_FILE	"snapshot_grp"

ATF_TC_WITHOUT_HEAD(getgrent);
ATF_TC_BODY(getgrent, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETGRENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getgrent_with_snapshot);
ATF_TC_BODY(getgrent_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETGRENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getgrent_with_two_pass);
ATF_TC_BODY(getgrent_with_two_pass, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETGRENT_2PASS) == 0);
}

ATF_TC_WITHOUT_HEAD(getgrgid);
ATF_TC_BODY(getgrgid, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETGRGID) == 0);
}

ATF_TC_WITHOUT_HEAD(getgrgid_with_snapshot);
ATF_TC_BODY(getgrgid_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETGRGID) == 0);
}

ATF_TC_WITHOUT_HEAD(getgrnam);
ATF_TC_BODY(getgrnam, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETGRNAM) == 0);
}

ATF_TC_WITHOUT_HEAD(getgrnam_with_snapshot);
ATF_TC_BODY(getgrnam_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETGRNAM) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getgrent);
	ATF_TP_ADD_TC(tp, getgrent_with_snapshot);
	ATF_TP_ADD_TC(tp, getgrent_with_two_pass);
	ATF_TP_ADD_TC(tp, getgrgid);
	ATF_TP_ADD_TC(tp, getgrgid_with_snapshot);
	ATF_TP_ADD_TC(tp, getgrnam);
	ATF_TP_ADD_TC(tp, getgrnam_with_snapshot);

	return (atf_no_error());
}
