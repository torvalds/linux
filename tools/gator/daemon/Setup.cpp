/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Setup.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Config.h"
#include "DynBuf.h"
#include "Logging.h"
#include "SessionData.h"

#define GATOR_MSG "gator: "
#define GATOR_ERROR "gator: error: "
#define GATOR_CONFIRM "gator: confirm: "

bool getLinuxVersion(int version[3]) {
	// Check the kernel version
	struct utsname utsname;
	if (uname(&utsname) != 0) {
		logg->logMessage("uname failed");
		return false;
	}

	version[0] = 0;
	version[1] = 0;
	version[2] = 0;

	int part = 0;
	char *ch = utsname.release;
	while (*ch >= '0' && *ch <= '9' && part < 3) {
		version[part] = 10*version[part] + *ch - '0';

		++ch;
		if (*ch == '.') {
			++part;
			++ch;
		}
	}

	return true;
}

static int pgrep_gator(DynBuf *const printb) {
	DynBuf b;

	DIR *proc = opendir("/proc");
	if (proc == NULL) {
		logg->logError(GATOR_ERROR "opendir failed");
		handleException();
	}

	int self = getpid();

	struct dirent *dirent;
	while ((dirent = readdir(proc)) != NULL) {
		char *endptr;
		const int pid = strtol(dirent->d_name, &endptr, 10);
		if (*endptr != '\0' || (pid == self)) {
			// Ignore proc items that are not integers like ., cpuinfo, etc...
			continue;
		}

		if (!printb->printf("/proc/%i/stat", pid)) {
			logg->logError(GATOR_ERROR "DynBuf::printf failed");
			handleException();
		}

		if (!b.read(printb->getBuf())) {
			// This is not a fatal error - the thread just doesn't exist any more
			continue;
		}

		char *comm = strchr(b.getBuf(), '(');
		if (comm == NULL) {
			logg->logError(GATOR_ERROR "parsing stat comm begin failed");
			handleException();
		}
		++comm;
		char *const str = strrchr(comm, ')');
		if (str == NULL) {
			logg->logError(GATOR_ERROR "parsing stat comm end failed");
			handleException();
		}
		*str = '\0';

		if (strncmp(comm, "gator", 5) != 0) {
			continue;
		}

		char state;
		const int count = sscanf(str + 2, " %c ", &state);
		if (count != 1) {
			logg->logError(GATOR_ERROR "parsing stat state failed");
			handleException();
		}

		if (state == 'Z') {
			// This gator is a zombie, ignore
			continue;
		}

		// Assume there is only one gator process
		return pid;
	}

	closedir(proc);

	return -1;
}

static bool confirm(const char *const message) {
	char buf[1<<10];

	printf(GATOR_CONFIRM "%s\n", message);
	while (fgets(buf, sizeof(buf), stdin) != NULL) {
		if (strcmp(buf, "y\n") == 0) {
			return true;
		}
		if (strcmp(buf, "n\n") == 0) {
			return false;
		}
		// Ignore unrecognized input
	}

	return false;
}

void update(const char *const gatorPath) {
	printf(GATOR_MSG "starting\n");

	int version[3];
	if (!getLinuxVersion(version)) {
		logg->logError(GATOR_ERROR "getLinuxVersion failed");
		handleException();
	}

	if (KERNEL_VERSION(version[0], version[1], version[2]) < KERNEL_VERSION(2, 6, 32)) {
		logg->logError(GATOR_ERROR "Streamline can't automatically setup gator as this kernel version is not supported. Please upgrade the kernel on your device.");
		handleException();
	}

	if (KERNEL_VERSION(version[0], version[1], version[2]) < KERNEL_VERSION(3, 4, 0)) {
		logg->logError(GATOR_ERROR "Streamline can't automatically setup gator as gator.ko is required for this version of Linux. Please build gator.ko and gatord and install them on your device.");
		handleException();
	}

	if (geteuid() != 0) {
		printf(GATOR_MSG "trying sudo\n");
		execlp("sudo", "sudo", gatorPath, "-u", NULL);
		// Streamline will provide the password if needed

		printf(GATOR_MSG "trying su\n");
		char buf[1<<10];
		/*
		 * Different versions of su handle additional -c command line options differently and expect the
		 * arguments in different ways. Try both ways wrapped in a shell.
		 *
		 * Then invoke another shell after su as it avoids odd failures on some Android systems
		 */
		snprintf(buf, sizeof(buf), "su -c \"sh -c '%s -u'\" || su -c sh -c '%s -u'", gatorPath, gatorPath);
		execlp("sh", "sh", "-c", buf, NULL);
		// Streamline will provide the password if needed

		logg->logError(GATOR_ERROR "Streamline was unable to sudo to root on your device. Please double check passwords, ensure sudo or su work with this user or try a different username.");
		handleException();
	}
	printf(GATOR_MSG "now root\n");

	if (access("/sys/module/gator", F_OK) == 0) {
		if (!confirm("Streamline has detected that the gator kernel module is loaded on your device. Click yes to switch to user space gator, click no to abort the install.")) {
			printf("gator: cancel\n");
			exit(-1);
		}
	}

	// setenforce 0 not needed for userspace gator

	// Kill existing gator
	DynBuf printb;
	int gator_main = pgrep_gator(&printb);
	if (gator_main > 0) {
		if (kill(gator_main, SIGTERM) != 0) {
			logg->logError(GATOR_ERROR "kill SIGTERM failed");
			handleException();
		}
		if (!printb.printf("/proc/%i/exe", gator_main)) {
			logg->logError(GATOR_ERROR "DynBuf::printf failed");
			handleException();
		}
		for (int i = 0; ; ++i) {
			// /proc/<pid>/exe exists but will not be accessible for zombies
			if (access(printb.getBuf(), F_OK) != 0) {
				break;
			}
			if (i == 5) {
				if (kill(gator_main, SIGKILL) != 0) {
					logg->logError(GATOR_ERROR "kill SIGKILL failed");
					handleException();
				}
			} else if (i >= 10) {
				logg->logError(GATOR_ERROR "unable to kill running gator");
				handleException();
			}
			sleep(1);
		}
	}
	printf(GATOR_MSG "no gatord running\n");

	umount("/dev/gator");
	syscall(__NR_delete_module, "gator", O_NONBLOCK);

	rename("gatord", "gatord.old");
	rename("gator.ko", "gator.ko.old");

	// Rename gatord.YYYYMMDDHHMMSSMMMM to gatord
	char *newGatorPath = strdup(gatorPath);
	char *dot = strrchr(newGatorPath, '.');
	if (dot != NULL) {
		*dot = '\0';
		if (rename(gatorPath, newGatorPath) != 0) {
			logg->logError(GATOR_ERROR "rename failed");
			handleException();
		}
	}

	char buf[128];
	int pipefd[2];
	if (pipe_cloexec(pipefd) != 0) {
		logg->logError(GATOR_ERROR "pipe failed");
		handleException();
	}

	// Fork and start gatord (redirect stdin, stdout and stderr so shell can close)
	int child = fork();
	if (child < 0) {
		logg->logError(GATOR_ERROR "fork failed");
		handleException();
	} else if (child == 0) {
		int inFd;
		int outFd;
		int errFd;
		int result = -1;

		buf[0] = '\0';
		close(pipefd[0]);

		inFd = open("/dev/null", O_RDONLY | O_CLOEXEC);
		if (inFd < 0) {
			snprintf(buf, sizeof(buf), GATOR_ERROR "open of /dev/null failed");
			goto fail_exit;
		}
		outFd = open("gatord.out", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (outFd < 0) {
			snprintf(buf, sizeof(buf), GATOR_ERROR "open of gatord.out failed");
			goto fail_exit;
		}
		errFd = open("gatord.err", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (errFd < 0) {
			snprintf(buf, sizeof(buf), GATOR_ERROR "open of gatord.err failed");
			goto fail_exit;
		}
		if (dup2(inFd, STDIN_FILENO) < 0) {
			snprintf(buf, sizeof(buf), GATOR_ERROR "dup2 for stdin failed");
			goto fail_exit;
		}
		fflush(stdout);
		if (dup2(outFd, STDOUT_FILENO) < 0) {
			snprintf(buf, sizeof(buf), GATOR_ERROR "dup2 for stdout failed");
			goto fail_exit;
		}
		fflush(stderr);
		if (dup2(errFd, STDERR_FILENO) < 0) {
			snprintf(buf, sizeof(buf), GATOR_ERROR "dup2 for stderr failed");
			goto fail_exit;
		}

		snprintf(buf, sizeof(buf), GATOR_MSG "done");
		result = 0;

	fail_exit:
		if (buf[0] != '\0') {
			const ssize_t bytes = write(pipefd[1], buf, sizeof(buf));
			// Can't do anything if this fails
			(void)bytes;
		}
		close(pipefd[1]);

		if (result == 0) {
			// Continue to execute gator normally
			return;
		}
		exit(-1);
	}

	close(pipefd[1]);
	const ssize_t bytes = read(pipefd[0], buf, sizeof(buf));
	if (bytes > 0) {
		logg->logError("%s", buf);
		handleException();
	}
	close(pipefd[0]);

	// Exit so parent shell can move on
	exit(0);
}
