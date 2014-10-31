/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Command.h"

#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Logging.h"
#include "SessionData.h"

static int getUid(const char *const name, char *const shPath, const char *const tmpDir) {
	// Lookups may fail when using a different libc or a statically compiled executable
	char gatorTemp[32];
	snprintf(gatorTemp, sizeof(gatorTemp), "%s/gator_temp", tmpDir);

	const int fd = open(gatorTemp, 600, O_CREAT | O_CLOEXEC);
	if (fd < 0) {
		return -1;
	}
	close(fd);

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "chown %s %s || rm %s", name, gatorTemp, gatorTemp);

	const int pid = fork();
	if (pid < 0) {
		logg->logError(__FILE__, __LINE__, "fork failed");
		handleException();
	}
	if (pid == 0) {
		char cargv1[] = "-c";
		char *cargv[] = {
			shPath,
			cargv1,
			cmd,
			NULL,
		};

		execv(cargv[0], cargv);
		exit(-1);
	}
	while ((waitpid(pid, NULL, 0) < 0) && (errno == EINTR));

	struct stat st;
	int result = -1;
	if (stat(gatorTemp, &st) == 0) {
		result = st.st_uid;
	}
	unlink(gatorTemp);
	return result;
}

static int getUid(const char *const name) {
	// Look up the username
	struct passwd *const user = getpwnam(name);
	if (user != NULL) {
		return user->pw_uid;
	}


	// Are we on Linux
	char cargv0l[] = "/bin/sh";
	if ((access(cargv0l, X_OK) == 0) && (access("/tmp", W_OK) == 0)) {
		return getUid(name, cargv0l, "/tmp");
	}

	// Are we on android
	char cargv0a[] = "/system/bin/sh";
	if ((access(cargv0a, X_OK) == 0) && (access("/data", W_OK) == 0)) {
		return getUid(name, cargv0a, "/data");
	}

	return -1;
}

void *commandThread(void *) {
	prctl(PR_SET_NAME, (unsigned long)&"gatord-command", 0, 0, 0);

	const char *const name = gSessionData->mCaptureUser == NULL ? "nobody" : gSessionData->mCaptureUser;
	const int uid = getUid(name);
	if (uid < 0) {
		logg->logError(__FILE__, __LINE__, "Unable to lookup the user %s, please double check that the user exists", name);
		handleException();
	}

	sleep(3);

	char buf[128];
	int pipefd[2];
	if (pipe_cloexec(pipefd) != 0) {
		logg->logError(__FILE__, __LINE__, "pipe failed");
		handleException();
	}

	const int pid = fork();
	if (pid < 0) {
		logg->logError(__FILE__, __LINE__, "fork failed");
		handleException();
	}
	if (pid == 0) {
		char cargv0l[] = "/bin/sh";
		char cargv0a[] = "/system/bin/sh";
		char cargv1[] = "-c";
		char *cargv[] = {
			cargv0l,
			cargv1,
			gSessionData->mCaptureCommand,
			NULL,
		};

		buf[0] = '\0';
		close(pipefd[0]);

		// Gator runs at a high priority, reset the priority to the default
		if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
			snprintf(buf, sizeof(buf), "setpriority failed");
			goto fail_exit;
		}

		if (setuid(uid) != 0) {
			snprintf(buf, sizeof(buf), "setuid failed");
			goto fail_exit;
		}

		{
			const char *const path = gSessionData->mCaptureWorkingDir == NULL ? "/" : gSessionData->mCaptureWorkingDir;
			if (chdir(path) != 0) {
				snprintf(buf, sizeof(buf), "Unable to cd to %s, please verify the directory exists and is accessable to %s", path, name);
				goto fail_exit;
			}
		}

		execv(cargv[0], cargv);
		cargv[0] = cargv0a;
		execv(cargv[0], cargv);
		snprintf(buf, sizeof(buf), "execv failed");

	fail_exit:
		if (buf[0] != '\0') {
			const ssize_t bytes = write(pipefd[1], buf, sizeof(buf));
			// Can't do anything if this fails
			(void)bytes;
		}

		exit(-1);
	}

	close(pipefd[1]);
	const ssize_t bytes = read(pipefd[0], buf, sizeof(buf));
	if (bytes > 0) {
		logg->logError(__FILE__, __LINE__, buf);
		handleException();
	}
	close(pipefd[0]);

	return NULL;
}
