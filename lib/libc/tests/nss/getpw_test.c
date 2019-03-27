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

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "testutil.h"

enum test_methods {
	TEST_GETPWENT,
	TEST_GETPWNAM,
	TEST_GETPWUID,
	TEST_GETPWENT_2PASS,
	TEST_BUILD_SNAPSHOT
};

DECLARE_TEST_DATA(passwd)
DECLARE_TEST_FILE_SNAPSHOT(passwd)
DECLARE_1PASS_TEST(passwd)
DECLARE_2PASS_TEST(passwd)

static void clone_passwd(struct passwd *, struct passwd const *);
static int compare_passwd(struct passwd *, struct passwd *, void *);
static void free_passwd(struct passwd *);

static void sdump_passwd(struct passwd *, char *, size_t);
#ifdef DEBUG
static void dump_passwd(struct passwd *);
#endif

static int passwd_read_snapshot_func(struct passwd *, char *);

static int passwd_check_ambiguity(struct passwd_test_data *, struct passwd *);
static int passwd_fill_test_data(struct passwd_test_data *);
static int passwd_test_correctness(struct passwd *, void *);
static int passwd_test_getpwnam(struct passwd *, void *);
static int passwd_test_getpwuid(struct passwd *, void *);
static int passwd_test_getpwent(struct passwd *, void *);

IMPLEMENT_TEST_DATA(passwd)
IMPLEMENT_TEST_FILE_SNAPSHOT(passwd)
IMPLEMENT_1PASS_TEST(passwd)
IMPLEMENT_2PASS_TEST(passwd)

static void
clone_passwd(struct passwd *dest, struct passwd const *src)
{
	ATF_REQUIRE(dest != NULL);
	ATF_REQUIRE(src != NULL);

	memcpy(dest, src, sizeof(struct passwd));
	if (src->pw_name != NULL)
		dest->pw_name = strdup(src->pw_name);
	if (src->pw_passwd != NULL)
		dest->pw_passwd = strdup(src->pw_passwd);
	if (src->pw_class != NULL)
		dest->pw_class = strdup(src->pw_class);
	if (src->pw_gecos != NULL)
		dest->pw_gecos = strdup(src->pw_gecos);
	if (src->pw_dir != NULL)
		dest->pw_dir = strdup(src->pw_dir);
	if (src->pw_shell != NULL)
		dest->pw_shell = strdup(dest->pw_shell);
}

static int
compare_passwd(struct passwd *pwd1, struct passwd *pwd2, void *mdata __unused)
{
	ATF_REQUIRE(pwd1 != NULL);
	ATF_REQUIRE(pwd2 != NULL);

	if (pwd1 == pwd2)
		return (0);

	if (pwd1->pw_uid != pwd2->pw_uid ||
	    pwd1->pw_gid != pwd2->pw_gid ||
	    pwd1->pw_change != pwd2->pw_change ||
	    pwd1->pw_expire != pwd2->pw_expire ||
	    pwd1->pw_fields != pwd2->pw_fields ||
	    strcmp(pwd1->pw_name, pwd2->pw_name) != 0 ||
	    strcmp(pwd1->pw_passwd, pwd2->pw_passwd) != 0 ||
	    strcmp(pwd1->pw_class, pwd2->pw_class) != 0 ||
	    strcmp(pwd1->pw_gecos, pwd2->pw_gecos) != 0 ||
	    strcmp(pwd1->pw_dir, pwd2->pw_dir) != 0 ||
	    strcmp(pwd1->pw_shell, pwd2->pw_shell) != 0)
		return (-1);
	else
		return (0);
}

static void
free_passwd(struct passwd *pwd)
{
	free(pwd->pw_name);
	free(pwd->pw_passwd);
	free(pwd->pw_class);
	free(pwd->pw_gecos);
	free(pwd->pw_dir);
	free(pwd->pw_shell);
}

static void
sdump_passwd(struct passwd *pwd, char *buffer, size_t buflen)
{
	snprintf(buffer, buflen, "%s:%s:%d:%d:%jd:%s:%s:%s:%s:%jd:%d",
	    pwd->pw_name, pwd->pw_passwd, pwd->pw_uid, pwd->pw_gid,
	    (uintmax_t)pwd->pw_change, pwd->pw_class, pwd->pw_gecos,
	    pwd->pw_dir, pwd->pw_shell, (uintmax_t)pwd->pw_expire,
	    pwd->pw_fields);
}

#ifdef DEBUG
static void
dump_passwd(struct passwd *pwd)
{
	if (pwd != NULL) {
		char buffer[2048];
		sdump_passwd(pwd, buffer, sizeof(buffer));
		printf("%s\n", buffer);
	} else
		printf("(null)\n");
}
#endif

static int
passwd_read_snapshot_func(struct passwd *pwd, char *line)
{
	char *s, *ps, *ts;
	int i;

#ifdef DEBUG
	printf("1 line read from snapshot:\n%s\n", line);
#endif

	i = 0;
	ps = line;
	memset(pwd, 0, sizeof(struct passwd));
	while ((s = strsep(&ps, ":")) != NULL) {
		switch (i) {
		case 0:
			pwd->pw_name = strdup(s);
			ATF_REQUIRE(pwd->pw_name != NULL);
			break;
		case 1:
			pwd->pw_passwd = strdup(s);
			ATF_REQUIRE(pwd->pw_passwd != NULL);
			break;
		case 2:
			pwd->pw_uid = (uid_t)strtol(s, &ts, 10);
			if (*ts != '\0')
				goto fin;
			break;
		case 3:
			pwd->pw_gid = (gid_t)strtol(s, &ts, 10);
			if (*ts != '\0')
				goto fin;
			break;
		case 4:
			pwd->pw_change = (time_t)strtol(s, &ts, 10);
			if (*ts != '\0')
				goto fin;
			break;
		case 5:
			pwd->pw_class = strdup(s);
			ATF_REQUIRE(pwd->pw_class != NULL);
			break;
		case 6:
			pwd->pw_gecos = strdup(s);
			ATF_REQUIRE(pwd->pw_gecos != NULL);
			break;
		case 7:
			pwd->pw_dir = strdup(s);
			ATF_REQUIRE(pwd->pw_dir != NULL);
			break;
		case 8:
			pwd->pw_shell = strdup(s);
			ATF_REQUIRE(pwd->pw_shell != NULL);
			break;
		case 9:
			pwd->pw_expire = (time_t)strtol(s, &ts, 10);
			if (*ts != '\0')
				goto fin;
			break;
		case 10:
			pwd->pw_fields = (int)strtol(s, &ts, 10);
			if (*ts != '\0')
				goto fin;
			break;
		default:
			break;
		}
		++i;
	}

fin:
	if (i != 11) {
		free_passwd(pwd);
		memset(pwd, 0, sizeof(struct passwd));
		return (-1);
	}

	return (0);
}

static int
passwd_fill_test_data(struct passwd_test_data *td)
{
	struct passwd *pwd;

	setpassent(1);
	while ((pwd = getpwent()) != NULL) {
		if (passwd_test_correctness(pwd, NULL) == 0)
			TEST_DATA_APPEND(passwd, td, pwd);
		else
			return (-1);
	}
	endpwent();

	return (0);
}

static int
passwd_test_correctness(struct passwd *pwd, void *mdata __unused)
{

#ifdef DEBUG
	printf("testing correctness with the following data:\n");
	dump_passwd(pwd);
#endif

	if (pwd == NULL)
		return (-1);

	if (pwd->pw_name == NULL)
		goto errfin;

	if (pwd->pw_passwd == NULL)
		goto errfin;

	if (pwd->pw_class == NULL)
		goto errfin;

	if (pwd->pw_gecos == NULL)
		goto errfin;

	if (pwd->pw_dir == NULL)
		goto errfin;

	if (pwd->pw_shell == NULL)
		goto errfin;

#ifdef DEBUG
	printf("correct\n");
#endif

	return (0);
errfin:
#ifdef DEBUG
	printf("incorrect\n");
#endif

	return (-1);
}

/* passwd_check_ambiguity() is needed here because when doing the getpwent()
 * calls sequence, records from different nsswitch sources can be different,
 * though having the same pw_name/pw_uid */
static int
passwd_check_ambiguity(struct passwd_test_data *td, struct passwd *pwd)
{

	return (TEST_DATA_FIND(passwd, td, pwd, compare_passwd,
		NULL) != NULL ? 0 : -1);
}

static int
passwd_test_getpwnam(struct passwd *pwd_model, void *mdata)
{
	struct passwd *pwd;

#ifdef DEBUG
	printf("testing getpwnam() with the following data:\n");
	dump_passwd(pwd_model);
#endif

	pwd = getpwnam(pwd_model->pw_name);
	if (passwd_test_correctness(pwd, NULL) != 0)
		goto errfin;

	if ((compare_passwd(pwd, pwd_model, NULL) != 0) &&
	    (passwd_check_ambiguity((struct passwd_test_data *)mdata, pwd)
	    !=0))
	    goto errfin;

#ifdef DEBUG
	printf("ok\n");
#endif
	return (0);

errfin:
#ifdef DEBUG
	printf("not ok\n");
#endif
	return (-1);
}

static int
passwd_test_getpwuid(struct passwd *pwd_model, void *mdata)
{
	struct passwd *pwd;

#ifdef DEBUG
	printf("testing getpwuid() with the following data...\n");
	dump_passwd(pwd_model);
#endif

	pwd = getpwuid(pwd_model->pw_uid);
	if ((passwd_test_correctness(pwd, NULL) != 0) ||
	    ((compare_passwd(pwd, pwd_model, NULL) != 0) &&
	    (passwd_check_ambiguity((struct passwd_test_data *)mdata, pwd)
	    != 0))) {
#ifdef DEBUG
		printf("not ok\n");
#endif
		return (-1);
	} else {
#ifdef DEBUG
		printf("ok\n");
#endif
		return (0);
	}
}

static int
passwd_test_getpwent(struct passwd *pwd, void *mdata __unused)
{
	/* Only correctness can be checked when doing 1-pass test for
	 * getpwent(). */
	return (passwd_test_correctness(pwd, NULL));
}

static int
run_tests(const char *snapshot_file, enum test_methods method)
{
	struct passwd_test_data td, td_snap, td_2pass;
	int rv;

	TEST_DATA_INIT(passwd, &td, clone_passwd, free_passwd);
	TEST_DATA_INIT(passwd, &td_snap, clone_passwd, free_passwd);
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

			TEST_SNAPSHOT_FILE_READ(passwd, snapshot_file,
				&td_snap, passwd_read_snapshot_func);
		}
	}

	rv = passwd_fill_test_data(&td);
	if (rv == -1)
		return (-1);

	switch (method) {
	case TEST_GETPWNAM:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(passwd, &td,
				passwd_test_getpwnam, (void *)&td);
		else
			rv = DO_1PASS_TEST(passwd, &td_snap,
				passwd_test_getpwnam, (void *)&td_snap);
		break;
	case TEST_GETPWUID:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(passwd, &td,
				passwd_test_getpwuid, (void *)&td);
		else
			rv = DO_1PASS_TEST(passwd, &td_snap,
				passwd_test_getpwuid, (void *)&td_snap);
		break;
	case TEST_GETPWENT:
		if (snapshot_file == NULL)
			rv = DO_1PASS_TEST(passwd, &td, passwd_test_getpwent,
				(void *)&td);
		else
			rv = DO_2PASS_TEST(passwd, &td, &td_snap,
				compare_passwd, NULL);
		break;
	case TEST_GETPWENT_2PASS:
		TEST_DATA_INIT(passwd, &td_2pass, clone_passwd, free_passwd);
		rv = passwd_fill_test_data(&td_2pass);
		if (rv != -1)
			rv = DO_2PASS_TEST(passwd, &td, &td_2pass,
			    compare_passwd, NULL);
		TEST_DATA_DESTROY(passwd, &td_2pass);
		break;
	case TEST_BUILD_SNAPSHOT:
		if (snapshot_file != NULL)
			rv = TEST_SNAPSHOT_FILE_WRITE(passwd, snapshot_file,
			    &td, sdump_passwd);
		break;
	default:
		rv = 0;
		break;
	}

fin:
	TEST_DATA_DESTROY(passwd, &td_snap);
	TEST_DATA_DESTROY(passwd, &td);

	return (rv);
}

#define	SNAPSHOT_FILE	"snapshot_pwd"

ATF_TC_WITHOUT_HEAD(build_snapshot);
ATF_TC_BODY(build_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
}

ATF_TC_WITHOUT_HEAD(getpwent);
ATF_TC_BODY(getpwent, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPWENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getpwent_with_snapshot);
ATF_TC_BODY(getpwent_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPWENT) == 0);
}

ATF_TC_WITHOUT_HEAD(getpwent_with_two_pass);
ATF_TC_BODY(getpwent_with_two_pass, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPWENT_2PASS) == 0);
}

ATF_TC_WITHOUT_HEAD(getpwnam);
ATF_TC_BODY(getpwnam, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPWNAM) == 0);
}

ATF_TC_WITHOUT_HEAD(getpwnam_with_snapshot);
ATF_TC_BODY(getpwnam_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPWNAM) == 0);
}

ATF_TC_WITHOUT_HEAD(getpwuid);
ATF_TC_BODY(getpwuid, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPWUID) == 0);
}

ATF_TC_WITHOUT_HEAD(getpwuid_with_snapshot);
ATF_TC_BODY(getpwuid_with_snapshot, tc)
{

	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_BUILD_SNAPSHOT) == 0);
	ATF_REQUIRE(run_tests(SNAPSHOT_FILE, TEST_GETPWUID) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, build_snapshot);
	ATF_TP_ADD_TC(tp, getpwent);
	ATF_TP_ADD_TC(tp, getpwent_with_snapshot);
	ATF_TP_ADD_TC(tp, getpwent_with_two_pass);
	ATF_TP_ADD_TC(tp, getpwnam);
	ATF_TP_ADD_TC(tp, getpwnam_with_snapshot);
	ATF_TP_ADD_TC(tp, getpwuid);
	ATF_TP_ADD_TC(tp, getpwuid_with_snapshot);

	return (atf_no_error());
}
