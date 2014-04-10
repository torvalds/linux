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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Buffer.h"
#include "DynBuf.h"
#include "Logging.h"

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

static bool readProcTask(Buffer *const buffer, const int pid, const char *const image, DynBuf *const printb, DynBuf *const b) {
	bool result = false;

	if (!b->printf("/proc/%i/task", pid)) {
		logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
		return result;
	}
	DIR *task = opendir(b->getBuf());
	if (task == NULL) {
		logg->logMessage("%s(%s:%i): opendir failed", __FUNCTION__, __FILE__, __LINE__);
		return result;
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
		if (!readProcStat(&ps, printb->getBuf(), b)) {
			logg->logMessage("%s(%s:%i): readProcStat failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}

		buffer->comm(pid, tid, image, ps.comm);
	}

	result = true;

 fail:
	closedir(task);

	return result;
}

bool readProc(Buffer *const buffer, DynBuf *const printb, DynBuf *const b1, DynBuf *const b2, DynBuf *const b3) {
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

		if (!printb->printf("/proc/%i/exe", pid)) {
			logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}
		const int err = b1->readlink(printb->getBuf());
		const char *image;
		if (err == 0) {
			image = strrchr(b1->getBuf(), '/');
			if (image == NULL) {
				image = b1->getBuf();
			} else {
				++image;
			}
		} else if (err == -ENOENT) {
			// readlink /proc/[pid]/exe returns ENOENT for kernel threads
			image = "\0";
		} else {
			logg->logMessage("%s(%s:%i): DynBuf::readlink failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}

		if (!printb->printf("/proc/%i/maps", pid)) {
			logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}
		if (!b2->read(printb->getBuf())) {
			logg->logMessage("%s(%s:%i): DynBuf::read failed, likely because the process exited", __FUNCTION__, __FILE__, __LINE__);
			// This is not a fatal error - the process just doesn't exist any more
			continue;
		}

		buffer->maps(pid, pid, b2->getBuf());
		if (ps.numThreads <= 1) {
			buffer->comm(pid, pid, image, ps.comm);
		} else {
			if (!readProcTask(buffer, pid, image, printb, b3)) {
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
