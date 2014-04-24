/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PerfSource.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "Child.h"
#include "DynBuf.h"
#include "Logging.h"
#include "PerfDriver.h"
#include "Proc.h"
#include "SessionData.h"

#define MS_PER_US 1000000

extern Child *child;

static bool sendTracepointFormat(Buffer *const buffer, const char *const name, DynBuf *const printb, DynBuf *const b) {
	if (!printb->printf(EVENTS_PATH "/%s/format", name)) {
		logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}
	if (!b->read(printb->getBuf())) {
		logg->logMessage("%s(%s:%i): DynBuf::read failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}
	buffer->format(b->getLength(), b->getBuf());

	return true;
}

PerfSource::PerfSource(sem_t *senderSem, sem_t *startProfile) : mSummary(0, FRAME_SUMMARY, 1024, senderSem), mBuffer(0, FRAME_PERF_ATTRS, 1024*1024, senderSem), mCountersBuf(), mCountersGroup(&mCountersBuf), mMonitor(), mUEvent(), mSenderSem(senderSem), mStartProfile(startProfile), mInterruptFd(-1), mIsDone(false) {
	long l = sysconf(_SC_PAGE_SIZE);
	if (l < 0) {
		logg->logError(__FILE__, __LINE__, "Unable to obtain the page size");
		handleException();
	}
	gSessionData->mPageSize = static_cast<int>(l);

	l = sysconf(_SC_NPROCESSORS_CONF);
	if (l < 0) {
		logg->logError(__FILE__, __LINE__, "Unable to obtain the number of cores");
		handleException();
	}
	gSessionData->mCores = static_cast<int>(l);
}

PerfSource::~PerfSource() {
}

struct PrepareParallelArgs {
	PerfGroup *pg;
	int cpu;
};

void *prepareParallel(void *arg) {
	const PrepareParallelArgs *const args = (PrepareParallelArgs *)arg;
	args->pg->prepareCPU(args->cpu);
	return NULL;
}

bool PerfSource::prepare() {
	DynBuf printb;
	DynBuf b1;
	DynBuf b2;
	DynBuf b3;
	long long schedSwitchId;

	if (0
			|| !mMonitor.init()
			|| !mUEvent.init()
			|| !mMonitor.add(mUEvent.getFd())

			|| (schedSwitchId = PerfDriver::getTracepointId(SCHED_SWITCH, &printb)) < 0
			|| !sendTracepointFormat(&mBuffer, SCHED_SWITCH, &printb, &b1)

			// Only want RAW but not IP on sched_switch and don't want TID on SAMPLE_ID
			|| !mCountersGroup.add(&mBuffer, 100/**/, PERF_TYPE_TRACEPOINT, schedSwitchId, 1, PERF_SAMPLE_RAW, PERF_GROUP_MMAP | PERF_GROUP_COMM | PERF_GROUP_TASK | PERF_GROUP_SAMPLE_ID_ALL)

			// Only want TID and IP but not RAW on timer
			|| (gSessionData->mSampleRate > 0 && !gSessionData->mIsEBS && !mCountersGroup.add(&mBuffer, 99/**/, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK, 1000000000UL / gSessionData->mSampleRate, PERF_SAMPLE_TID | PERF_SAMPLE_IP, 0))

			|| !gSessionData->perf.enable(&mCountersGroup, &mBuffer)
			|| 0) {
		logg->logMessage("%s(%s:%i): perf setup failed, are you running Linux 3.12 or later?", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	if (!gSessionData->perf.summary(&mSummary)) {
		logg->logMessage("%s(%s:%i): PerfDriver::summary failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	{
		// Run prepareCPU in parallel as perf_event_open can take more than 1 sec in some cases
		pthread_t threads[NR_CPUS];
		PrepareParallelArgs args[NR_CPUS];
		for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
			args[cpu].pg = &mCountersGroup;
			args[cpu].cpu = cpu;
			if (pthread_create(&threads[cpu], NULL, prepareParallel, &args[cpu]) != 0) {
				logg->logMessage("%s(%s:%i): pthread_create failed", __FUNCTION__, __FILE__, __LINE__);
				return false;
			}
		}
		for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
			if (pthread_join(threads[cpu], NULL) != 0) {
				logg->logMessage("%s(%s:%i): pthread_join failed", __FUNCTION__, __FILE__, __LINE__);
				return false;
			}
		}
	}

	int numEvents = 0;
	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		numEvents += mCountersGroup.onlineCPU(cpu, false, &mBuffer, &mMonitor);
	}
	if (numEvents <= 0) {
		logg->logMessage("%s(%s:%i): PerfGroup::onlineCPU failed on all cores", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	// Start events before reading proc to avoid race conditions
	if (!mCountersGroup.start()) {
		logg->logMessage("%s(%s:%i): PerfGroup::start failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	if (!readProc(&mBuffer, &printb, &b1, &b2, &b3)) {
		logg->logMessage("%s(%s:%i): readProc failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	mBuffer.commit(1);

	return true;
}

static const char CPU_DEVPATH[] = "/devices/system/cpu/cpu";

void PerfSource::run() {
	int pipefd[2];

	if (pipe(pipefd) != 0) {
		logg->logError(__FILE__, __LINE__, "pipe failed");
		handleException();
	}
	mInterruptFd = pipefd[1];

	if (!mMonitor.add(pipefd[0])) {
		logg->logError(__FILE__, __LINE__, "Monitor::add failed");
		handleException();
	}

	int timeout = -1;
	if (gSessionData->mLiveRate > 0) {
		timeout = gSessionData->mLiveRate/MS_PER_US;
	}

	sem_post(mStartProfile);

	while (gSessionData->mSessionIsActive) {
		// +1 for uevents, +1 for pipe
		struct epoll_event events[NR_CPUS + 2];
		int ready = mMonitor.wait(events, ARRAY_LENGTH(events), timeout);
		if (ready < 0) {
			logg->logError(__FILE__, __LINE__, "Monitor::wait failed");
			handleException();
		}

		for (int i = 0; i < ready; ++i) {
			if (events[i].data.fd == mUEvent.getFd()) {
				if (!handleUEvent()) {
					logg->logError(__FILE__, __LINE__, "PerfSource::handleUEvent failed");
					handleException();
				}
				break;
			}
		}

		// send a notification that data is ready
		sem_post(mSenderSem);

		// In one shot mode, stop collection once all the buffers are filled
		// Assume timeout == 0 in this case
		if (gSessionData->mOneShot && gSessionData->mSessionIsActive) {
			logg->logMessage("%s(%s:%i): One shot", __FUNCTION__, __FILE__, __LINE__);
			child->endSession();
		}
	}

	mCountersGroup.stop();
	mBuffer.setDone();
	mIsDone = true;

	// send a notification that data is ready
	sem_post(mSenderSem);

	mInterruptFd = -1;
	close(pipefd[0]);
	close(pipefd[1]);
}

bool PerfSource::handleUEvent() {
	UEventResult result;
	if (!mUEvent.read(&result)) {
		logg->logMessage("%s(%s:%i): UEvent::Read failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	if (strcmp(result.mSubsystem, "cpu") == 0) {
		if (strncmp(result.mDevPath, CPU_DEVPATH, sizeof(CPU_DEVPATH) - 1) != 0) {
			logg->logMessage("%s(%s:%i): Unexpected cpu DEVPATH format", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}
		char *endptr;
		errno = 0;
		int cpu = strtol(result.mDevPath + sizeof(CPU_DEVPATH) - 1, &endptr, 10);
		if (errno != 0 || *endptr != '\0') {
			logg->logMessage("%s(%s:%i): strtol failed", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}
		if (strcmp(result.mAction, "online") == 0) {
			// Only call onlineCPU if prepareCPU succeeded
			const bool result = mCountersGroup.prepareCPU(cpu) &&
				mCountersGroup.onlineCPU(cpu, true, &mBuffer, &mMonitor);
			mBuffer.commit(1);
			return result;
		} else if (strcmp(result.mAction, "offline") == 0) {
			return mCountersGroup.offlineCPU(cpu);
		}
	}

	return true;
}

void PerfSource::interrupt() {
	if (mInterruptFd >= 0) {
		int8_t c = 0;
		// Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
		if (::write(mInterruptFd, &c, sizeof(c)) != sizeof(c)) {
			logg->logError(__FILE__, __LINE__, "write failed");
			handleException();
		}
	}
}

bool PerfSource::isDone () {
	return mBuffer.isDone() && mIsDone && mCountersBuf.isEmpty();
}

void PerfSource::write (Sender *sender) {
	if (!mSummary.isDone()) {
		mSummary.write(sender);
	}
	if (!mBuffer.isDone()) {
		mBuffer.write(sender);
	}
	if (!mCountersBuf.send(sender)) {
		logg->logError(__FILE__, __LINE__, "PerfBuffer::send failed");
		handleException();
	}
}
