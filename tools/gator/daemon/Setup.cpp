/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Config.h"
#include "DynBuf.h"
#include "Logging.h"

bool getLinuxVersion(int version[3]) {
	// Check the kernel version
	struct utsname utsname;
	if (uname(&utsname) != 0) {
		logg->logMessage("%s(%s:%i): uname failed", __FUNCTION__, __FILE__, __LINE__);
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
		logg->logError(__FILE__, __LINE__, "gator: error: opendir failed");
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
			logg->logError(__FILE__, __LINE__, "gator: error: DynBuf::printf failed");
			handleException();
		}

		if (!b.read(printb->getBuf())) {
			// This is not a fatal error - the thread just doesn't exist any more
			continue;
		}

		char *comm = strchr(b.getBuf(), '(');
		if (comm == NULL) {
			logg->logError(__FILE__, __LINE__, "gator: error: parsing stat begin failed");
			handleException();
		}
		++comm;
		char *const str = strrchr(comm, ')');
		if (str == NULL) {
			logg->logError(__FILE__, __LINE__, "gator: error: parsing stat end failed");
			handleException();
		}
		*str = '\0';

		if (strncmp(comm, "gator", 5) == 0) {
			// Assume there is only one gator process
			return pid;
		}
	}

	closedir(proc);

	return -1;
}

int update(const char *const gatorPath) {
	printf("gator: starting\n");

	int version[3];
	if (!getLinuxVersion(version)) {
		logg->logError(__FILE__, __LINE__, "gator: error: getLinuxVersion failed");
		handleException();
	}

	if (KERNEL_VERSION(version[0], version[1], version[2]) < KERNEL_VERSION(2, 6, 32)) {
		logg->logError(__FILE__, __LINE__, "gator: error: Streamline can't automatically setup gator as this kernel version is not supported. Please upgrade the kernel on your device.");
		handleException();
	}

	if (KERNEL_VERSION(version[0], version[1], version[2]) < KERNEL_VERSION(3, 4, 0)) {
		logg->logError(__FILE__, __LINE__, "gator: error: Streamline can't automatically setup gator as gator.ko is required for this version of Linux. Please build gator.ko and gatord and install them on your device.");
		handleException();
	}

	if (access("/sys/module/gator", F_OK) == 0) {
		logg->logError(__FILE__, __LINE__, "gator: error: Streamline has detected that the gator kernel module is loaded on your device. Please build an updated version of gator.ko and gatord and install them on your device.");
		handleException();
	}

	if (geteuid() != 0) {
		printf("gator: trying sudo\n");
		execlp("sudo", "sudo", gatorPath, "-u", NULL);
		// Streamline will provide the password if needed

		printf("gator: trying su\n");
		char buf[1<<10];
		snprintf(buf, sizeof(buf), "%s -u", gatorPath);
		execlp("su", "su", "-", "-c", buf, NULL);
		// Streamline will provide the password if needed

		logg->logError(__FILE__, __LINE__, "gator: error: Streamline was unable to sudo to root on your device. Please double check passwords, ensure sudo or su work with this user or try a different username.");
		handleException();
	}
	printf("gator: now root\n");

	// setenforce 0 not needed for userspace gator

	// Kill existing gator
	DynBuf gatorStatPath;
	int gator_main = pgrep_gator(&gatorStatPath);
	if (gator_main > 0) {
		if (kill(gator_main, SIGTERM) != 0) {
			logg->logError(__FILE__, __LINE__, "gator: error: kill SIGTERM failed");
			handleException();
		}
		for (int i = 0; ; ++i) {
			if (access(gatorStatPath.getBuf(), F_OK) != 0) {
				break;
			}
			if (i == 5) {
				if (kill(gator_main, SIGKILL) != 0) {
					logg->logError(__FILE__, __LINE__, "gator: error: kill SIGKILL failed");
					handleException();
				}
			} else if (i >= 10) {
				logg->logError(__FILE__, __LINE__, "gator: error: unable to kill running gator");
				handleException();
			}
			sleep(1);
		}
	}
	printf("gator: no gatord running\n");

	rename("gatord", "gatord.old");
	rename("gator.ko", "gator.ko.old");

	// Rename gatord.YYYYMMDDHHMMSSMMMM to gatord
	char *newGatorPath = strdup(gatorPath);
	char *dot = strrchr(newGatorPath, '.');
	if (dot != NULL) {
		*dot = '\0';
		if (rename(gatorPath, newGatorPath) != 0) {
			logg->logError(__FILE__, __LINE__, "gator: error: rename failed");
			handleException();
		}
	}

	// Fork and start gatord (redirect stdout and stderr)
	int child = fork();
	if (child < 0) {
		logg->logError(__FILE__, __LINE__, "gator: error: fork failed");
		handleException();
	} else if (child == 0) {
		int inFd = open("/dev/null", O_RDONLY | O_CLOEXEC);
		if (inFd < 0) {
			logg->logError(__FILE__, __LINE__, "gator: error: open of /dev/null failed");
			handleException();
		}
		int outFd = open("gatord.out", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (outFd < 0) {
			logg->logError(__FILE__, __LINE__, "gator: error: open of gatord.out failed");
			handleException();
		}
		int errFd = open("gatord.err", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (errFd < 0) {
			logg->logError(__FILE__, __LINE__, "gator: error: open of gatord.err failed");
			handleException();
		}
		if (dup2(inFd, STDIN_FILENO) < 0) {
			logg->logError(__FILE__, __LINE__, "gator: error: dup2 for stdin failed");
			handleException();
		}
		if (dup2(outFd, STDOUT_FILENO) < 0) {
			logg->logError(__FILE__, __LINE__, "gator: error: dup2 for stdout failed");
			handleException();
		}
		if (dup2(errFd, STDERR_FILENO) < 0) {
			logg->logError(__FILE__, __LINE__, "gator: error: dup2 for stderr failed");
			handleException();
		}
		execlp(newGatorPath, newGatorPath, "-a", NULL);
		logg->logError(__FILE__, __LINE__, "gator: error: execlp failed");
		handleException();
	}

	printf("gator: done\n");

	return 0;
}
