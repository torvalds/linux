/**
 * Copyright (C) ARM Limited 2013-2015. All rights reserved.
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
#include "DynBuf.h"
#include "Logging.h"
#include "Monitor.h"
#include "PerfBuffer.h"
#include "SessionData.h"

static const int schedSwitchKey = getEventKey();
static const int clockKey = getEventKey();

#define DEFAULT_PEA_ARGS(pea, additionalSampleType) \
	pea.size = sizeof(pea); \
	/* Emit time, read_format below, group leader id, and raw tracepoint info */ \
	pea.sample_type = (gSessionData->perf.getLegacySupport() \
			   ? PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_ID \
			   : PERF_SAMPLE_IDENTIFIER ) | PERF_SAMPLE_TIME | additionalSampleType; \
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

PerfGroup::PerfGroup(PerfBuffer *const pb) : mPb(pb), mSchedSwitchId(-1) {
	memset(&mAttrs, 0, sizeof(mAttrs));
	memset(&mFlags, 0, sizeof(mFlags));
	memset(&mKeys, -1, sizeof(mKeys));
	memset(&mFds, -1, sizeof(mFds));
	memset(&mLeaders, -1, sizeof(mLeaders));
}

PerfGroup::~PerfGroup() {
	for (int pos = ARRAY_LENGTH(mFds) - 1; pos >= 0; --pos) {
		if (mFds[pos] >= 0) {
			close(mFds[pos]);
		}
	}
}

int PerfGroup::doAdd(const uint64_t currTime, Buffer *const buffer, const int key, const __u32 type, const __u64 config, const __u64 sample, const __u64 sampleType, const int flags) {
	int i;
	for (i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
		if (mKeys[i] < 0) {
			break;
		}
	}

	if (i >= ARRAY_LENGTH(mKeys)) {
		logg->logMessage("Too many counters");
		return -1;
	}

	DEFAULT_PEA_ARGS(mAttrs[i], sampleType);
	mAttrs[i].type = type;
	mAttrs[i].config = config;
	mAttrs[i].sample_period = sample;
	// always be on the CPU but only a group leader can be pinned
	mAttrs[i].pinned = (flags & PERF_GROUP_LEADER ? 1 : 0);
	mAttrs[i].mmap = (flags & PERF_GROUP_MMAP ? 1 : 0);
	mAttrs[i].comm = (flags & PERF_GROUP_COMM ? 1 : 0);
	mAttrs[i].freq = (flags & PERF_GROUP_FREQ ? 1 : 0);
	mAttrs[i].task = (flags & PERF_GROUP_TASK ? 1 : 0);
	mAttrs[i].sample_id_all = (flags & PERF_GROUP_SAMPLE_ID_ALL ? 1 : 0);
	mFlags[i] = flags;

	mKeys[i] = key;

	buffer->marshalPea(currTime, &mAttrs[i], key);

	return i;
}

/* Counters from different hardware PMUs need to be in different
 * groups. Software counters can be in the same group as the CPU and
 * should be marked as PERF_GROUP_CPU. The big and little clusters can
 * be in the same group as only one or the other will be available on
 * a given CPU.
 */
int PerfGroup::getEffectiveType(const int type, const int flags) {
	const int effectiveType = flags & PERF_GROUP_CPU ? (int)PERF_TYPE_HARDWARE : type;
	if (effectiveType >= ARRAY_LENGTH(mLeaders)) {
		logg->logError("perf type is too large, please increase the size of PerfGroup::mLeaders");
		handleException();
	}
	return effectiveType;
}

bool PerfGroup::createCpuGroup(const uint64_t currTime, Buffer *const buffer) {
	if (mSchedSwitchId < 0) {
		DynBuf b;
		mSchedSwitchId = PerfDriver::getTracepointId(SCHED_SWITCH, &b);
		if (mSchedSwitchId < 0) {
			logg->logMessage("Unable to read sched_switch id");
			return false;
		}
	}

	mLeaders[PERF_TYPE_HARDWARE] = doAdd(currTime, buffer, schedSwitchKey, PERF_TYPE_TRACEPOINT, mSchedSwitchId, 1, PERF_SAMPLE_READ | PERF_SAMPLE_RAW, PERF_GROUP_MMAP | PERF_GROUP_COMM | PERF_GROUP_TASK | PERF_GROUP_SAMPLE_ID_ALL | PERF_GROUP_PER_CPU | PERF_GROUP_LEADER | PERF_GROUP_CPU);
	if (mLeaders[PERF_TYPE_HARDWARE] < 0) {
		return false;
	}

	if (gSessionData->mSampleRate > 0 && !gSessionData->mIsEBS && doAdd(currTime, buffer, clockKey, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK, 1000000000UL / gSessionData->mSampleRate, PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU) < 0) {
		return false;
	}

	return true;
}

bool PerfGroup::add(const uint64_t currTime, Buffer *const buffer, const int key, const __u32 type, const __u64 config, const __u64 sample, const __u64 sampleType, const int flags) {
	const int effectiveType = getEffectiveType(type, flags);

	// Does a group exist for this already?
	if (!(flags & PERF_GROUP_LEADER) && mLeaders[effectiveType] < 0) {
		// Create it
		if (effectiveType == PERF_TYPE_HARDWARE) {
			if (!createCpuGroup(currTime, buffer)) {
				return false;
			}
		} else {
			// Non-CPU PMUs are sampled every 100ms for Sample Rate: None and EBS, otherwise they would never be sampled
			const uint64_t timeout = gSessionData->mSampleRate > 0 && !gSessionData->mIsEBS ? 1000000000UL / gSessionData->mSampleRate : 100000000UL;
			// PERF_SAMPLE_TID | PERF_SAMPLE_IP aren't helpful on non-CPU or 'uncore' PMUs - which CPU is the right one to sample? But removing it causes problems, remove it later.
			mLeaders[effectiveType] = doAdd(currTime, buffer, clockKey, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK, timeout, PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_READ, PERF_GROUP_LEADER);
			if (mLeaders[effectiveType] < 0) {
				return false;
			}
		}
	}

	if (!(flags & PERF_GROUP_LEADER) && effectiveType != PERF_TYPE_HARDWARE && (flags & PERF_GROUP_PER_CPU)) {
		logg->logError("'uncore' counters are not permitted to be per-cpu");
		handleException();
	}

	return doAdd(currTime, buffer, key, type, config, sample, sampleType, flags) >= 0;
}

int PerfGroup::prepareCPU(const int cpu, Monitor *const monitor) {
	logg->logMessage("Onlining cpu %i", cpu);

	for (int i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
		if (mKeys[i] < 0) {
			continue;
		}

		if ((cpu != 0) && !(mFlags[i] & PERF_GROUP_PER_CPU)) {
			continue;
		}

		const int offset = i * gSessionData->mCores + cpu;
		if (mFds[offset] >= 0) {
			logg->logMessage("cpu already online or not correctly cleaned up");
			return PG_FAILURE;
		}

		logg->logMessage("perf_event_open cpu: %i type: %i config: %lli sample: %lli sample_type: 0x%llx pinned: %lli mmap: %lli comm: %lli freq: %lli task: %lli sample_id_all: %lli", cpu, mAttrs[i].type, mAttrs[i].config, mAttrs[i].sample_period, mAttrs[i].sample_type, mAttrs[i].pinned, mAttrs[i].mmap, mAttrs[i].comm, mAttrs[i].freq, mAttrs[i].task, mAttrs[i].sample_id_all);
		mFds[offset] = sys_perf_event_open(&mAttrs[i], -1, cpu, mAttrs[i].pinned ? -1 : mFds[mLeaders[getEffectiveType(mAttrs[i].type, mFlags[i])] * gSessionData->mCores + cpu], mAttrs[i].pinned ? 0 : PERF_FLAG_FD_OUTPUT);
		if (mFds[offset] < 0) {
			logg->logMessage("failed %s", strerror(errno));
			if (errno == ENODEV) {
				// The core is offline
				return PG_CPU_OFFLINE;
			}
#ifndef USE_STRICTER_CHECK
			continue;
#else
			if (errno == ENOENT) {
				// This event doesn't apply to this CPU but should apply to a different one, ex bL
				continue;
			}
			logg->logMessage("perf_event_open failed");
			return PG_FAILURE;
#endif
		}

		if (!mPb->useFd(cpu, mFds[offset])) {
			logg->logMessage("PerfBuffer::useFd failed");
			return PG_FAILURE;
		}


		if (!monitor->add(mFds[offset])) {
			logg->logMessage("Monitor::add failed");
			return PG_FAILURE;
		}
	}

	return PG_SUCCESS;
}

static bool readAndSend(const uint64_t currTime, Buffer *const buffer, const int fd, const int keyCount, const int *const keys) {
	char buf[1024];
	ssize_t bytes = read(fd, buf, sizeof(buf));
	if (bytes < 0) {
		logg->logMessage("read failed");
		return false;
	}
	buffer->marshalKeysOld(currTime, keyCount, keys, bytes, buf);

	return true;
}

int PerfGroup::onlineCPU(const uint64_t currTime, const int cpu, const bool enable, Buffer *const buffer) {
	bool addedEvents = false;

	if (!gSessionData->perf.getLegacySupport()) {
		int idCount = 0;
		int coreKeys[ARRAY_LENGTH(mKeys)];
		__u64 ids[ARRAY_LENGTH(mKeys)];

		for (int i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
			const int fd = mFds[cpu + i * gSessionData->mCores];
			if (fd < 0) {
				continue;
			}

			coreKeys[idCount] = mKeys[i];
			if (ioctl(fd, PERF_EVENT_IOC_ID, &ids[idCount]) != 0 &&
					// Workaround for running 32-bit gatord on 64-bit systems, kernel patch in the works
					ioctl(fd, (PERF_EVENT_IOC_ID & ~IOCSIZE_MASK) | (8 << _IOC_SIZESHIFT), &ids[idCount]) != 0) {
				logg->logMessage("ioctl failed");
				return 0;
			}
			++idCount;
			addedEvents = true;
		}

		buffer->marshalKeys(currTime, idCount, ids, coreKeys);
	} else {
		int idCounts[ARRAY_LENGTH(mLeaders)] = { 0 };
		int coreKeys[ARRAY_LENGTH(mLeaders)][ARRAY_LENGTH(mKeys)];
		for (int i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
			const int fd = mFds[cpu + i * gSessionData->mCores];
			if (fd < 0) {
				continue;
			}

			const int effectiveType = getEffectiveType(mAttrs[i].type, mFlags[i]);
			if (mAttrs[i].pinned && mLeaders[effectiveType] != i) {
				if (!readAndSend(currTime, buffer, fd, 1, mKeys + i)) {
					return 0;
				}
			} else {
				coreKeys[effectiveType][idCounts[effectiveType]] = mKeys[i];
				++idCounts[effectiveType];
				addedEvents = true;
			}
		}

		for (int i = 0; i < ARRAY_LENGTH(mLeaders); ++i) {
			if (idCounts[i] > 0 && !readAndSend(currTime, buffer, mFds[mLeaders[i] * gSessionData->mCores + cpu], idCounts[i], coreKeys[i])) {
					return 0;
			}
		}
	}

	if (enable) {
		for (int i = 0; i < ARRAY_LENGTH(mKeys); ++i) {
			int offset = i * gSessionData->mCores + cpu;
			if (mFds[offset] >= 0 && ioctl(mFds[offset], PERF_EVENT_IOC_ENABLE, 0) < 0) {
				logg->logMessage("ioctl failed");
				return 0;
			}
		}
	}

	if (!addedEvents) {
		logg->logMessage("no events came online");
	}

	return 1;
}

bool PerfGroup::offlineCPU(const int cpu) {
	logg->logMessage("Offlining cpu %i", cpu);

	for (int i = ARRAY_LENGTH(mKeys) - 1; i >= 0; --i) {
		int offset = i * gSessionData->mCores + cpu;
		if (mFds[offset] >= 0 && ioctl(mFds[offset], PERF_EVENT_IOC_DISABLE, 0) < 0) {
			logg->logMessage("ioctl failed");
			return false;
		}
	}

	// Mark the buffer so that it will be released next time it's read
	mPb->discard(cpu);

	for (int i = ARRAY_LENGTH(mKeys) - 1; i >= 0; --i) {
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
			logg->logMessage("ioctl failed");
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
