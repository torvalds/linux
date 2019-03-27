/*-
 * Copyright (c) 2001 Networks Associates Technology, Inc.
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
 * Written at NAI Labs at Network Associates by Robert Watson for the
 * TrustedBSD Project.
 *
 * Work sponsored by Defense Advanced Research Projects Agency under the
 * CHATS research program, CBOSS project.
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Regression test to check some basic cases and see if access() and
 * eaccess() are using the correct portions of the process credential.
 * This test relies on running with privilege, and on UFS filesystem
 * semantics.  Running the test in other environments may result
 * in incorrect failure identification.
 *
 * Note that this may also break if filesystem access control is
 * broken, or if the ability to check and set credentials is broken.
 *
 * Note that this test uses two hard-coded non-root UIDs; on multi-user
 * systems, these UIDs may be in use by an untrusted user, in which
 * case those users could interfere with the test.
 */

#define	ROOT_UID	(uid_t)0
#define	WHEEL_GID	(gid_t)0
#define	TEST_UID_ONE	(uid_t)500
#define	TEST_GID_ONE	(gid_t)500
#define	TEST_UID_TWO	(uid_t)501
#define	TEST_GID_TWO	(gid_t)501

struct file_description {
	char	*fd_name;
	uid_t	 fd_owner;
	gid_t	 fd_group;
	mode_t	 fd_mode;
};

static struct file_description fd_list[] = {
{"test1", ROOT_UID, WHEEL_GID, 0400},
{"test2", TEST_UID_ONE, WHEEL_GID,0400},
{"test3", TEST_UID_TWO, WHEEL_GID, 0400},
{"test4", ROOT_UID, WHEEL_GID, 0040},
{"test5", ROOT_UID, TEST_GID_ONE, 0040},
{"test6", ROOT_UID, TEST_GID_TWO, 0040}};

static int fd_list_count = sizeof(fd_list) /
    sizeof(struct file_description);

int
setup(void)
{
	int i, error;

	for (i = 0; i < fd_list_count; i++) {
		error = open(fd_list[i].fd_name, O_CREAT | O_EXCL, fd_list[i].fd_mode);
		if (error == -1) {
			perror("open");
			return (error);
		}
		close(error);
		error = chown(fd_list[i].fd_name, fd_list[i].fd_owner,
		    fd_list[i].fd_group);
		if (error) {
			perror("chown");
			return (error);
		}
	}
	return (0);
}

int
restoreprivilege(void)
{
	int error;

	error = setreuid(ROOT_UID, ROOT_UID);
	if (error)
		return (error);

	error = setregid(WHEEL_GID, WHEEL_GID);
	if (error)
		return (error);

	return (0);
}

int
reportprivilege(char *message)
{
	uid_t euid, ruid, suid;
	gid_t egid, rgid, sgid;
	int error;

	error = getresuid(&ruid, &euid, &suid);
	if (error) {
		perror("getresuid");
		return (error);
	}

	error = getresgid(&rgid, &egid, &sgid);
	if (error) {
		perror("getresgid");
		return (error);
	}

	if (message)
		printf("%s: ", message);
	printf("ruid: %d, euid: %d, suid: %d,     ", ruid, euid, suid);
	printf("rgid: %d, egid: %d, sgid: %d\n", rgid, egid, sgid);

	return (0);
}

int
cleanup(void)
{
	int i, error;

	error = restoreprivilege();
	if (error) {
		perror("restoreprivilege");
		return (error);
	}

	for (i = 0; i < fd_list_count; i++) {
		error = unlink(fd_list[i].fd_name);
		if (error)
			return (error);
	}

	return (0);
}

int
main(int argc, char *argv[])
{
	int error, errorseen;

	if (geteuid() != 0) {
		fprintf(stderr, "testaccess must run as root.\n");
		exit (EXIT_FAILURE);
	}

	error = setup();
	if (error) {
		cleanup();
		exit (EXIT_FAILURE);
	}

	/* Make sure saved uid is set appropriately. */
	error = setresuid(ROOT_UID, ROOT_UID, ROOT_UID);
	if (error) {
		perror("setresuid");
		cleanup();
	}

	/* Clear out additional groups. */
	error = setgroups(0, NULL);
	if (error) {
		perror("setgroups");
		cleanup();
	}

	/* Make sure saved gid is set appropriately. */
	error = setresgid(WHEEL_GID, WHEEL_GID, WHEEL_GID);
	if (error) {
		perror("setresgid");
		cleanup();
	}

	/*
	 * UID-only tests.
	 */

	/* Check that saved uid is not used */
	error = setresuid(TEST_UID_ONE, TEST_UID_ONE, ROOT_UID);
	if (error) {
		perror("setresuid.1");
		cleanup();
		exit (EXIT_FAILURE);
	}

	errorseen = 0;

	error = access("test1", R_OK);
	if (!error) {
		fprintf(stderr, "saved uid used instead of real uid\n");
		errorseen++;
	}

#ifdef EACCESS_AVAILABLE
	error = eaccess("test1", R_OK);
	if (!error) {
		fprintf(stderr, "saved uid used instead of effective uid\n");
		errorseen++;
	}
#endif

	error = restoreprivilege();
	if (error) {
		perror("restoreprivilege");
		cleanup();
		exit (EXIT_FAILURE);
	}

	error = setresuid(TEST_UID_ONE, TEST_UID_TWO, ROOT_UID);
	if (error) {
		perror("setresid.2");
		cleanup();
		exit (EXIT_FAILURE);
	}

	/* Check that the real uid is used, not the effective uid */
	error = access("test2", R_OK);
	if (error) {
		fprintf(stderr, "Effective uid was used instead of real uid in access().\n");
		errorseen++;
	}

#ifdef EACCESS_AVAILABLE
	/* Check that the effective uid is used, not the real uid */
	error = eaccess("test3", R_OK);
	if (error) {
		fprintf(stderr, "Real uid was used instead of effective uid in eaccess().\n");
		errorseen++;
	}
#endif

	/* Check that the real uid is used, not the effective uid */
	error = access("test3", R_OK);
	if (!error) {
		fprintf(stderr, "Effective uid was used instead of real uid in access().\n");
		errorseen++;
	}

#ifdef EACCESS_AVAILABLE
	/* Check that the effective uid is used, not the real uid */
	error = eaccess("test2", R_OK);
	if (!error) {
		fprintf(stderr, "Real uid was used instead of effective uid in eaccess().\n");
		errorseen++;
	}
#endif

	error = restoreprivilege();
	if (error) {
		perror("restoreprivilege");
		cleanup();
		exit (EXIT_FAILURE);
	}

	error = setresgid(TEST_GID_ONE, TEST_GID_TWO, WHEEL_GID);
	if (error) {
		perror("setresgid.1");
		cleanup();
		exit (EXIT_FAILURE);
	}

	/* Set non-root effective uid to avoid excess privilege. */
	error = setresuid(TEST_UID_ONE, TEST_UID_ONE, ROOT_UID);
	if (error) {
		perror("setresuid.3");
		cleanup();
		exit (EXIT_FAILURE);
	}

	/* Check that the saved gid is not used */
	error = access("test4", R_OK);
	if (!error) {
		fprintf(stderr, "saved gid used instead of real gid\n");
	}

#ifdef EACCESS_AVAILABLE
	error = eaccess("test4", R_OK);
	if (!error) {
		fprintf(stderr, "saved gid used instead of effective gid\n");
		errorseen++;
	}
#endif

	/* Check that the real gid is used, not the effective gid */
	error = access("test5", R_OK);
	if (error) {
		fprintf(stderr, "Effective gid was used instead of real gid in access().\n");
		errorseen++;
	}

#ifdef EACCESS_AVAILABLE
	/* Check that the effective gid is used, not the real gid */
	error = eaccess("test6", R_OK);
	if (error) {
		fprintf(stderr, "Real gid was used instead of effective gid in eaccess().\n");
		errorseen++;
	}
#endif

	/* Check that the real gid is used, not the effective gid */
	error = access("test6", R_OK);
	if (!error) {
		fprintf(stderr, "Effective gid was used instead of real gid in access().\n");
		errorseen++;
	}

#ifdef EACCESS_AVAILABLE
	/* Check that the effective gid is used, not the real gid */
	error = eaccess("test5", R_OK);
	if (!error) {
		fprintf(stderr, "Real gid was used instead of effective gid in eaccess().\n");
		errorseen++;
	}
#endif

	fprintf(stderr, "%d errors seen.\n", errorseen);

	/*
	 * All tests done, restore and clean up
	 */

	error = cleanup();
	if (error) {
		perror("cleanup");
		exit (EXIT_FAILURE);
	}

	exit (EXIT_SUCCESS);
}
