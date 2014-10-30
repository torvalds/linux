/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Proc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "Buffer.h"
#include "DynBuf.h"
#include "Logging.h"
#include "SessionData.h"

struct ProcStat {
	// From linux-dev/include/linux/sched.h
#define TASK_COMM_LEN 16
	// TASK_COMM_LEN may grow, so be ready for it to get larger
	char comm[2*TASK_COMM_LEN];
	long numThreads;
};

static bool readProcStat(ProcStat *const ps, const char *const pathname, DynBuf *const b) {
	if (!b->read(pathname)) {
		logg->logMessage("%s(%s:%i): DynBuf::read failed, likely because the thread exited", __FUNCTION__, __FILE__, __LINE__);
		// This is not a fatal error - the thread just doesn't exist any more
		return true;
	}

	char *comm = strchr(b->getBuf(), '(');
	if (comm == NULL) {
		logg->logMessage("%s(%s:%i): parsing stat failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}
	++comm;
	char *const str = strrchr(comm, ')');
	if (str == NULL) {
		logg->logMessage("%s(%s:%i): parsing stat failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}
	*str = '\0';
	strncpy(ps->comm, comm, sizeof(ps->comm) - 1);
	ps->comm[sizeof(ps->comm) - 1] = '\0';

	const int count = sscanf(str + 2, " %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %ld", &ps->numThreads);
	if (count != 1) {
		logg->logMessage("%s(%s:%i): sscanf failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	return true;
}

static const char APP_PROCESS[] = "app_process";

static const char *readProcExe(DynBuf *const printb, const int pid, const int tid, DynBuf *const b) {
	if (tid == -1 ? !printb->printf("/proc/%i/exe", pid)
			: !printb->printf("/proc/%i/task/%i/exe", pid, tid)) {
		logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
		return NULL;
	}

	const int err = b->readlink(printb->getBuf());
	const char *image;
	if (err == 0) {
		image = strrchr(b->getBuf(), '/');
		if (image == NULL) {
			image = b->getBuf();
		} else {
			++image;
		}
	} else if (err == -ENOENT) {
		// readlink /proc/[pid]/exe returns ENOENT for kernel threads
		image = "\0";
	} else {
		logg->logMessage("%s(%s:%i): DynBuf::readlink failed", __FUNCTION__, __FILE__, __LINE__);
		return NULL;
	}

	// Android apps are run by app_process but the cmdline is changed to reference the actual app name
	// On 64-bit android app_process can be app_process32 or app_process64
	if (strncmp(image, APP_PROCESS, sizeof(APP_PROCESS) - 1) != 0) {
		return image;
	}

	if (tid == -1 ? !printb->printf("/proc/%i/cmdline", pid)
			: !printb->printf("/proc/%i/task/%i/cmdline", pid, tid)) {
		logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
		return NULL;
	}

	if (!b->read(printb->getBuf())) {
		logg->logMessage("%s(%s:%i): DynBuf::read failed, likely because the thread exited", __FUNCTION__, __FILE__, __LINE__);
		return NULL;
	}

	return b->getBuf();
}

static bool readProcTask(const uint64_t currTime, Buffer *const buffer, const int pid, DynBuf *const printb, DynBuf *const b1, DynBuf *const b2) {
	bool result = false;

	if (!b1->printf("/proc/%i/task", pid)) {
		logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
		return result;
	}
	DIR *task = opendir(b1->getBuf());
	if (task == NULL) {
		logg->logMessage("%s(%s:%i): opendir failed", __FUNCTION__, __FILE__, __LINE__);
		// This is not a fatal error - the thread just doesn't exist any more
		return true;
	}

	struct dirent *dirent;
	while ((dirent = readdir(task)) != NULL) {
		char *endptr;
		const int tid = strtol(dirent->d_name, &endptr, 10);
		if (*endptr != '\0') {
			// Ignore task items that are not integers like ., etc...
			continue;
		}

		if (!printb->printf("/proc/%i/task/%i/stat", pid, tid)) {
			logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}
		ProcStat ps;
		if (!readProcStat(&ps, printb->getBuf(), b1)) {
			logg->logMessage("%s(%s:%i): readProcStat failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}

		const char *const image = readProcExe(printb, pid, tid, b2);
		if (image == NULL) {
			logg->logMessage("%s(%s:%i): readImage failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}

		buffer->comm(currTime, pid, tid, image, ps.comm);
	}

	result = true;

 fail:
	closedir(task);

	return result;
}

bool readProcComms(const uint64_t currTime, Buffer *const buffer, DynBuf *const printb, DynBuf *const b1, DynBuf *const b2) {
	bool result = false;

	DIR *proc = opendir("/proc");
	if (proc == NULL) {
		logg->logMessage("%s(%s:%i): opendir failed", __FUNCTION__, __FILE__, __LINE__);
		return result;
	}

	struct dirent *dirent;
	while ((dirent = readdir(proc)) != NULL) {
		char *endptr;
		const int pid = strtol(dirent->d_name, &endptr, 10);
		if (*endptr != '\0') {
			// Ignore proc items that are not integers like ., cpuinfo, etc...
			continue;
		}

		if (!printb->printf("/proc/%i/stat", pid)) {
			logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}
		ProcStat ps;
		if (!readProcStat(&ps, printb->getBuf(), b1)) {
			logg->logMessage("%s(%s:%i): readProcStat failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}

		if (ps.numThreads <= 1) {
			const char *const image = readProcExe(printb, pid, -1, b1);
			if (image == NULL) {
				logg->logMessage("%s(%s:%i): readImage failed", __FUNCTION__, __FILE__, __LINE__);
				goto fail;
			}

			buffer->comm(currTime, pid, pid, image, ps.comm);
		} else {
			if (!readProcTask(currTime, buffer, pid, printb, b1, b2)) {
				logg->logMessage("%s(%s:%i): readProcTask failed", __FUNCTION__, __FILE__, __LINE__);
				goto fail;
			}
		}
	}

	result = true;

 fail:
	closedir(proc);

	return result;
}

bool readProcMaps(const uint64_t currTime, Buffer *const buffer, DynBuf *const printb, DynBuf *const b) {
	bool result = false;

	DIR *proc = opendir("/proc");
	if (proc == NULL) {
		logg->logMessage("%s(%s:%i): opendir failed", __FUNCTION__, __FILE__, __LINE__);
		return result;
	}

	struct dirent *dirent;
	while ((dirent = readdir(proc)) != NULL) {
		char *endptr;
		const int pid = strtol(dirent->d_name, &endptr, 10);
		if (*endptr != '\0') {
			// Ignore proc items that are not integers like ., cpuinfo, etc...
			continue;
		}

		if (!printb->printf("/proc/%i/maps", pid)) {
			logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}
		if (!b->read(printb->getBuf())) {
			logg->logMessage("%s(%s:%i): DynBuf::read failed, likely because the process exited", __FUNCTION__, __FILE__, __LINE__);
			// This is not a fatal error - the process just doesn't exist any more
			continue;
		}

		buffer->maps(currTime, pid, pid, b->getBuf());
	}

	result = true;

 fail:
	closedir(proc);

	return result;
}

bool readKallsyms(const uint64_t currTime, Buffer *const buffer, const bool *const isDone) {
	int fd = ::open("/proc/kallsyms", O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		logg->logMessage("%s(%s:%i): open failed", __FUNCTION__, __FILE__, __LINE__);
		return true;
	};

	char buf[1<<12];
	ssize_t pos = 0;
	while (gSessionData->mSessionIsActive && !ACCESS_ONCE(*isDone)) {
		// Assert there is still space in the buffer
		if (sizeof(buf) - pos - 1 == 0) {
			logg->logError(__FILE__, __LINE__, "no space left in buffer");
			handleException();
		}

		{
			// -1 to reserve space for \0
			const ssize_t bytes = ::read(fd, buf + pos, sizeof(buf) - pos - 1);
			if (bytes < 0) {
				logg->logError(__FILE__, __LINE__, "read failed", __FUNCTION__, __FILE__, __LINE__);
				handleException();
			}
			if (bytes == 0) {
				// Assert the buffer is empty
				if (pos != 0) {
					logg->logError(__FILE__, __LINE__, "buffer not empty on eof");
					handleException();
				}
				break;
			}
			pos += bytes;
		}

		ssize_t newline;
		// Find the last '\n'
		for (newline = pos - 1; newline >= 0; --newline) {
			if (buf[newline] == '\n') {
				const char was = buf[newline + 1];
				buf[newline + 1] = '\0';
				buffer->kallsyms(currTime, buf);
				// Sleep 3 ms to avoid sending out too much data too quickly
				usleep(3000);
				buf[0] = was;
				// Assert the memory regions do not overlap
				if (pos - newline >= newline + 1) {
					logg->logError(__FILE__, __LINE__, "memcpy src and dst overlap");
					handleException();
				}
				if (pos - newline - 2 > 0) {
					memcpy(buf + 1, buf + newline + 2, pos - newline - 2);
				}
				pos -= newline + 1;
				break;
			}
		}
	}

	close(fd);

	return true;
}
