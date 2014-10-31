/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PerfGroup.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "Buffer.h"
#include "Logging.h"
#include "Monitor.h"
#include "PerfBuffer.h"
#include "SessionData.h"

#define DEFAULT_PEA_ARGS(pea, additionalSampleType) \
	pea.size = sizeof(pea); \
	/* Emit time, read_format below, group leader id, and raw tracepoint info */ \
	pea.sample_type = (gSessionData->perf.getLegacySupport() \
										 ? PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_TIME | PERF_SAMPLE_READ | PERF_SAMPLE_ID \
										 : PERF_SAMPLE_TIME | PERF_SAMPLE_READ | PERF_SAMPLE_IDENTIFIER ) | additionalSampleType; \
	/* Emit emit value in group format */ \
	pea.read_format = PERF_FORMAT_ID | PERF_FORMAT_GROUP; \
	/* start out disabled */ \
	pea.disabled = 1; \
	/* have a sampling interrupt happen when we cross the wakeup_watermark boundary */ \
	pea.watermark = 1; \
	/* Be conservative in flush size as only one buffer set is monitored */ \
	pea.wakeup_watermark = BUF_SIZE / 2

static int sys_perf_event_open(struct perf_event_attr *const attr, const pid_t pid, const int cpu, const int group_fd, const unsigned long flags) {
	int fd = syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
	if (fd < 0) {
		return -1;
	}
	int fdf = fcntl(fd, F_GETFD);
	if ((fdf == -1) || (fcntl(fd, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
		close(fd);
		return -1;
	}
	return fd;
}

PerfGroup::PerfGroup(PerfBuffer *const pb) : mPb(pb) {
	memset(&mAttrs, 0, sizeof(mAttrs));
	memset(&mPerCpu, 0, sizeof(mPerCpu));
	memset(&mKeys, -1, sizeof(mKeys));
	memset(&mFds, -1, sizeof(mFds));
}

PerfGroup::~PerfGroup() {
	for (int pos = ARRAY_LENGTH(mFds) - 1; pos >= 0; --pos) {
		if (mFds[pos] >= 0) {
			close(mFds[pos]);
		}
	}
}

bool PerfGroup::add(const uint64_t currTime, Buffer *const buffer, const int key, const __u32 type, const __u64 config, const __u64 sample, const __u64 sampleType, const int flags) {
	int i;
	for (i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
		if (mKeys[i] < 0) {
			break;
		}
	}

	if (i >= ARRAY_LENGTH(mKeys)) {
		logg->logMessage("%s(%s:%i): Too many counters", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	DEFAULT_PEA_ARGS(mAttrs[i], sampleType);
	mAttrs[i].type = type;
	mAttrs[i].config = config;
	mAttrs[i].sample_period = sample;
	// always be on the CPU but only a group leader can be pinned
	mAttrs[i].pinned = (i == 0 ? 1 : 0);
	mAttrs[i].mmap = (flags & PERF_GROUP_MMAP ? 1 : 0);
	mAttrs[i].comm = (flags & PERF_GROUP_COMM ? 1 : 0);
	mAttrs[i].freq = (flags & PERF_GROUP_FREQ ? 1 : 0);
	mAttrs[i].task = (flags & PERF_GROUP_TASK ? 1 : 0);
	mAttrs[i].sample_id_all = (flags & PERF_GROUP_SAMPLE_ID_ALL ? 1 : 0);
	mPerCpu[i] = (flags & PERF_GROUP_PER_CPU);

	mKeys[i] = key;

	buffer->pea(currTime, &mAttrs[i], key);

	return true;
}

int PerfGroup::prepareCPU(const int cpu, Monitor *const monitor) {
	logg->logMessage("%s(%s:%i): Onlining cpu %i", __FUNCTION__, __FILE__, __LINE__, cpu);

	for (int i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
		if (mKeys[i] < 0) {
			continue;
		}

		if ((cpu != 0) && !mPerCpu[i]) {
			continue;
		}

		const int offset = i * gSessionData->mCores;
		if (mFds[cpu + offset] >= 0) {
			logg->logMessage("%s(%s:%i): cpu already online or not correctly cleaned up", __FUNCTION__, __FILE__, __LINE__);
			return PG_FAILURE;
		}

		logg->logMessage("%s(%s:%i): perf_event_open cpu: %i type: %lli config: %lli sample: %lli sample_type: 0x%llx pinned: %i mmap: %i comm: %i freq: %i task: %i sample_id_all: %i", __FUNCTION__, __FILE__, __LINE__, cpu, (long long)mAttrs[i].type, (long long)mAttrs[i].config, (long long)mAttrs[i].sample_period, (long long)mAttrs[i].sample_type, mAttrs[i].pinned, mAttrs[i].mmap, mAttrs[i].comm, mAttrs[i].freq, mAttrs[i].task, mAttrs[i].sample_id_all);
		mFds[cpu + offset] = sys_perf_event_open(&mAttrs[i], -1, cpu, i == 0 ? -1 : mFds[cpu], i == 0 ? 0 : PERF_FLAG_FD_OUTPUT);
		if (mFds[cpu + offset] < 0) {
			logg->logMessage("%s(%s:%i): failed %s", __FUNCTION__, __FILE__, __LINE__, strerror(errno));
			if (errno == ENODEV) {
				return PG_CPU_OFFLINE;
			}
			continue;
		}

		if (!mPb->useFd(cpu, mFds[cpu + offset])) {
			logg->logMessage("%s(%s:%i): PerfBuffer::useFd failed", __FUNCTION__, __FILE__, __LINE__);
			return PG_FAILURE;
		}


		if (!monitor->add(mFds[cpu + offset])) {
		  logg->logMessage("%s(%s:%i): Monitor::add failed", __FUNCTION__, __FILE__, __LINE__);
		  return PG_FAILURE;
		}
	}

	return PG_SUCCESS;
}

int PerfGroup::onlineCPU(const uint64_t currTime, const int cpu, const bool start, Buffer *const buffer) {
	__u64 ids[ARRAY_LENGTH(mKeys)];
	int coreKeys[ARRAY_LENGTH(mKeys)];
	int idCount = 0;

	for (int i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
		const int fd = mFds[cpu + i * gSessionData->mCores];
		if (fd < 0) {
			continue;
		}

		coreKeys[idCount] = mKeys[i];
		if (!gSessionData->perf.getLegacySupport() && ioctl(fd, PERF_EVENT_IOC_ID, &ids[idCount]) != 0 &&
				// Workaround for running 32-bit gatord on 64-bit systems, kernel patch in the works
				ioctl(fd, (PERF_EVENT_IOC_ID & ~IOCSIZE_MASK) | (8 << _IOC_SIZESHIFT), &ids[idCount]) != 0) {
			logg->logMessage("%s(%s:%i): ioctl failed", __FUNCTION__, __FILE__, __LINE__);
			return 0;
		}
		++idCount;
	}

	if (!gSessionData->perf.getLegacySupport()) {
		buffer->keys(currTime, idCount, ids, coreKeys);
	} else {
		char buf[1024];
		ssize_t bytes = read(mFds[cpu], buf, sizeof(buf));
		if (bytes < 0) {
			logg->logMessage("read failed");
			return 0;
		}
		buffer->keysOld(currTime, idCount, coreKeys, bytes, buf);
	}

	if (start) {
		for (int i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
			int offset = i * gSessionData->mCores + cpu;
			if (mFds[offset] >= 0 && ioctl(mFds[offset], PERF_EVENT_IOC_ENABLE, 0) < 0) {
				logg->logMessage("%s(%s:%i): ioctl failed", __FUNCTION__, __FILE__, __LINE__);
				return 0;
			}
		}
	}

	if (idCount == 0) {
		logg->logMessage("%s(%s:%i): no events came online", __FUNCTION__, __FILE__, __LINE__);
	}

	return idCount;
}

bool PerfGroup::offlineCPU(const int cpu) {
	logg->logMessage("%s(%s:%i): Offlining cpu %i", __FUNCTION__, __FILE__, __LINE__, cpu);

	for (int i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
		int offset = i * gSessionData->mCores + cpu;
		if (mFds[offset] >= 0 && ioctl(mFds[offset], PERF_EVENT_IOC_DISABLE, 0) < 0) {
			logg->logMessage("%s(%s:%i): ioctl failed", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}
	}

	// Mark the buffer so that it will be released next time it's read
	mPb->discard(cpu);

	for (int i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
		if (mKeys[i] < 0) {
			continue;
		}

		int offset = i * gSessionData->mCores + cpu;
		if (mFds[offset] >= 0) {
			close(mFds[offset]);
			mFds[offset] = -1;
		}
	}

	return true;
}

bool PerfGroup::start() {
	for (int pos = 0; pos < ARRAY_LENGTH(mFds); ++pos) {
		if (mFds[pos] >= 0 && ioctl(mFds[pos], PERF_EVENT_IOC_ENABLE, 0) < 0) {
			logg->logMessage("%s(%s:%i): ioctl failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		}
	}

	return true;

 fail:
	stop();

	return false;
}

void PerfGroup::stop() {
	for (int pos = ARRAY_LENGTH(mFds) - 1; pos >= 0; --pos) {
		if (mFds[pos] >= 0) {
			ioctl(mFds[pos], PERF_EVENT_IOC_DISABLE, 0);
		}
	}
}
