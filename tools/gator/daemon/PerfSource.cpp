/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PerfSource.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "Child.h"
#include "DynBuf.h"
#include "Logging.h"
#include "PerfDriver.h"
#include "Proc.h"
#include "SessionData.h"

#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif

extern Child *child;

static const int cpuIdleKey = getEventKey();

static bool sendTracepointFormat(const uint64_t currTime, Buffer *const buffer, const char *const name, DynBuf *const printb, DynBuf *const b) {
	if (!printb->printf(EVENTS_PATH "/%s/format", name)) {
		logg->logMessage("DynBuf::printf failed");
		return false;
	}
	if (!b->read(printb->getBuf())) {
		logg->logMessage("DynBuf::read failed");
		return false;
	}
	buffer->marshalFormat(currTime, b->getLength(), b->getBuf());

	return true;
}

static void *syncFunc(void *arg)
{
	struct timespec ts;
	int64_t nextTime = gSessionData->mMonotonicStarted;
	int err;
	(void)arg;

	prctl(PR_SET_NAME, (unsigned long)&"gatord-sync", 0, 0, 0);

	// Mask all signals so that this thread will not be woken up
	{
		sigset_t set;
		if (sigfillset(&set) != 0) {
			logg->logError("sigfillset failed");
			handleException();
		}
		if ((err = pthread_sigmask(SIG_SETMASK, &set, NULL)) != 0) {
			logg->logError("pthread_sigmask failed");
			handleException();
		}
	}

	for (;;) {
		if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
			logg->logError("clock_gettime failed");
			handleException();
		}
		const int64_t currTime = ts.tv_sec * NS_PER_S + ts.tv_nsec;

		// Wake up once a second
		nextTime += NS_PER_S;

		// Always sleep more than 1 ms, hopefully things will line up better next time
		const int64_t sleepTime = max(nextTime - currTime, (int64_t)(NS_PER_MS + 1));
		ts.tv_sec = sleepTime/NS_PER_S;
		ts.tv_nsec = sleepTime % NS_PER_S;

		err = nanosleep(&ts, NULL);
		if (err != 0) {
			fprintf(stderr, "clock_nanosleep failed: %s\n", strerror(err));
			return NULL;
		}
	}

	return NULL;
}

static long getMaxCoreNum() {
	DIR *dir = opendir("/sys/devices/system/cpu");
	if (dir == NULL) {
		logg->logError("Unable to determine the number of cores on the target, opendir failed");
		handleException();
	}

	long maxCoreNum = -1;
	struct dirent *dirent;
	while ((dirent = readdir(dir)) != NULL) {
		if (strncmp(dirent->d_name, "cpu", 3) == 0) {
			char *endptr;
			errno = 0;
			long coreNum = strtol(dirent->d_name + 3, &endptr, 10);
			if ((errno == 0) && (*endptr == '\0') && (coreNum >= maxCoreNum)) {
				maxCoreNum = coreNum + 1;
			}
		}
	}
	closedir(dir);

	if (maxCoreNum < 1) {
		logg->logError("Unable to determine the number of cores on the target, no cpu# directories found");
		handleException();
	}

	if (maxCoreNum >= NR_CPUS) {
		logg->logError("Too many cores on the target, please increase NR_CPUS in Config.h");
		handleException();
	}

	return maxCoreNum;
}

PerfSource::PerfSource(sem_t *senderSem, sem_t *startProfile) : mSummary(0, FRAME_SUMMARY, 1024, senderSem), mBuffer(NULL), mCountersBuf(), mCountersGroup(&mCountersBuf), mMonitor(), mUEvent(), mSenderSem(senderSem), mStartProfile(startProfile), mInterruptFd(-1), mIsDone(false) {
	long l = sysconf(_SC_PAGE_SIZE);
	if (l < 0) {
		logg->logError("Unable to obtain the page size");
		handleException();
	}
	gSessionData->mPageSize = static_cast<int>(l);
	gSessionData->mCores = static_cast<int>(getMaxCoreNum());
}

PerfSource::~PerfSource() {
	delete mBuffer;
}

bool PerfSource::prepare() {
	DynBuf printb;
	DynBuf b1;
	long long cpuIdleId;

	// MonotonicStarted has not yet been assigned!
	const uint64_t currTime = 0;//getTime() - gSessionData->mMonotonicStarted;

	mBuffer = new Buffer(0, FRAME_PERF_ATTRS, gSessionData->mTotalBufferSize*1024*1024, mSenderSem);

	// Reread cpuinfo since cores may have changed since startup
	gSessionData->readCpuInfo();

	if (0
			|| !mMonitor.init()
			|| !mUEvent.init()
			|| !mMonitor.add(mUEvent.getFd())

			|| !sendTracepointFormat(currTime, mBuffer, SCHED_SWITCH, &printb, &b1)

			|| (cpuIdleId = PerfDriver::getTracepointId(CPU_IDLE, &printb)) < 0
			|| !sendTracepointFormat(currTime, mBuffer, CPU_IDLE, &printb, &b1)

			|| !sendTracepointFormat(currTime, mBuffer, CPU_FREQUENCY, &printb, &b1)

			|| !mCountersGroup.createCpuGroup(currTime, mBuffer)
			|| !mCountersGroup.add(currTime, mBuffer, cpuIdleKey, PERF_TYPE_TRACEPOINT, cpuIdleId, 1, PERF_SAMPLE_RAW, PERF_GROUP_LEADER | PERF_GROUP_PER_CPU)

			|| !gSessionData->perf.enable(currTime, &mCountersGroup, mBuffer)
			|| 0) {
		logg->logMessage("perf setup failed, are you running Linux 3.4 or later?");
		return false;
	}

	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		const int result = mCountersGroup.prepareCPU(cpu, &mMonitor);
		if ((result != PG_SUCCESS) && (result != PG_CPU_OFFLINE)) {
			logg->logError("PerfGroup::prepareCPU on mCountersGroup failed");
			handleException();
		}
	}

	int numEvents = 0;
	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		numEvents += mCountersGroup.onlineCPU(currTime, cpu, false, mBuffer);
	}
	if (numEvents <= 0) {
		logg->logMessage("PerfGroup::onlineCPU failed on all cores");
		return false;
	}

	// Send the summary right before the start so that the monotonic delta is close to the start time
	if (!gSessionData->perf.summary(&mSummary)) {
		logg->logError("PerfDriver::summary failed");
		handleException();
	}

	// Start the timer thread to used to sync perf and monotonic raw times
	pthread_t syncThread;
	if (pthread_create(&syncThread, NULL, syncFunc, NULL)) {
		logg->logError("pthread_create failed");
		handleException();
	}
	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if (pthread_setschedparam(syncThread, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
		logg->logError("pthread_setschedparam failed");
		handleException();
	}

	mBuffer->commit(currTime);

	return true;
}

struct ProcThreadArgs {
	Buffer *mBuffer;
	uint64_t mCurrTime;
	bool mIsDone;
};

void *procFunc(void *arg) {
	DynBuf printb;
	DynBuf b;
	const ProcThreadArgs *const args = (ProcThreadArgs *)arg;

	prctl(PR_SET_NAME, (unsigned long)&"gatord-proc", 0, 0, 0);

	// Gator runs at a high priority, reset the priority to the default
	if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
		logg->logError("setpriority failed");
		handleException();
	}

	if (!readProcMaps(args->mCurrTime, args->mBuffer, &printb, &b)) {
		logg->logError("readProcMaps failed");
		handleException();
	}

	if (!readKallsyms(args->mCurrTime, args->mBuffer, &args->mIsDone)) {
		logg->logError("readKallsyms failed");
		handleException();
	}
	args->mBuffer->commit(args->mCurrTime);

	return NULL;
}

static const char CPU_DEVPATH[] = "/devices/system/cpu/cpu";

void PerfSource::run() {
	int pipefd[2];
	pthread_t procThread;
	ProcThreadArgs procThreadArgs;

	if (pipe_cloexec(pipefd) != 0) {
		logg->logError("pipe failed");
		handleException();
	}
	mInterruptFd = pipefd[1];

	if (!mMonitor.add(pipefd[0])) {
		logg->logError("Monitor::add failed");
		handleException();
	}

	{
		DynBuf printb;
		DynBuf b1;
		DynBuf b2;

		const uint64_t currTime = getTime() - gSessionData->mMonotonicStarted;

		// Start events before reading proc to avoid race conditions
		if (!mCountersGroup.start()) {
			logg->logError("PerfGroup::start failed");
			handleException();
		}

		mBuffer->perfCounterHeader(currTime);
		for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
			gSessionData->perf.read(mBuffer, cpu);
		}
		mBuffer->perfCounterFooter(currTime);

		if (!readProcComms(currTime, mBuffer, &printb, &b1, &b2)) {
			logg->logError("readProcComms failed");
			handleException();
		}
		mBuffer->commit(currTime);

		// Postpone reading kallsyms as on android adb gets too backed up and data is lost
		procThreadArgs.mBuffer = mBuffer;
		procThreadArgs.mCurrTime = currTime;
		procThreadArgs.mIsDone = false;
		if (pthread_create(&procThread, NULL, procFunc, &procThreadArgs)) {
			logg->logError("pthread_create failed");
			handleException();
		}
	}

	sem_post(mStartProfile);

	const uint64_t NO_RATE = ~0ULL;
	const uint64_t rate = gSessionData->mLiveRate > 0 && gSessionData->mSampleRate > 0 ? gSessionData->mLiveRate : NO_RATE;
	uint64_t nextTime = 0;
	int timeout = rate != NO_RATE ? 0 : -1;
	while (gSessionData->mSessionIsActive) {
		// +1 for uevents, +1 for pipe
		struct epoll_event events[NR_CPUS + 2];
		int ready = mMonitor.wait(events, ARRAY_LENGTH(events), timeout);
		if (ready < 0) {
			logg->logError("Monitor::wait failed");
			handleException();
		}
		const uint64_t currTime = getTime() - gSessionData->mMonotonicStarted;

		for (int i = 0; i < ready; ++i) {
			if (events[i].data.fd == mUEvent.getFd()) {
				if (!handleUEvent(currTime)) {
					logg->logError("PerfSource::handleUEvent failed");
					handleException();
				}
				break;
			}
		}

		// send a notification that data is ready
		sem_post(mSenderSem);

		// In one shot mode, stop collection once all the buffers are filled
		if (gSessionData->mOneShot && gSessionData->mSessionIsActive && ((mSummary.bytesAvailable() <= 0) || (mBuffer->bytesAvailable() <= 0) || mCountersBuf.isFull())) {
			logg->logMessage("One shot (perf)");
			child->endSession();
		}

		if (rate != NO_RATE) {
			while (currTime > nextTime) {
				nextTime += rate;
			}
			// + NS_PER_MS - 1 to ensure always rounding up
			timeout = max(0, (int)((nextTime + NS_PER_MS - 1 - getTime() + gSessionData->mMonotonicStarted)/NS_PER_MS));
		}
	}

	procThreadArgs.mIsDone = true;
	pthread_join(procThread, NULL);
	mCountersGroup.stop();
	mBuffer->setDone();
	mIsDone = true;

	// send a notification that data is ready
	sem_post(mSenderSem);

	mInterruptFd = -1;
	close(pipefd[0]);
	close(pipefd[1]);
}

bool PerfSource::handleUEvent(const uint64_t currTime) {
	UEventResult result;
	if (!mUEvent.read(&result)) {
		logg->logMessage("UEvent::Read failed");
		return false;
	}

	if (strcmp(result.mSubsystem, "cpu") == 0) {
		if (strncmp(result.mDevPath, CPU_DEVPATH, sizeof(CPU_DEVPATH) - 1) != 0) {
			logg->logMessage("Unexpected cpu DEVPATH format");
			return false;
		}
		char *endptr;
		errno = 0;
		int cpu = strtol(result.mDevPath + sizeof(CPU_DEVPATH) - 1, &endptr, 10);
		if (errno != 0 || *endptr != '\0') {
			logg->logMessage("strtol failed");
			return false;
		}

		if (cpu >= gSessionData->mCores) {
			logg->logError("Only %i cores are expected but core %i reports %s", gSessionData->mCores, cpu, result.mAction);
			handleException();
		}

		if (strcmp(result.mAction, "online") == 0) {
			mBuffer->onlineCPU(currTime, cpu);
			// Only call onlineCPU if prepareCPU succeeded
			bool ret = false;
			int err = mCountersGroup.prepareCPU(cpu, &mMonitor);
			if (err == PG_CPU_OFFLINE) {
				ret = true;
			} else if (err == PG_SUCCESS) {
				if (mCountersGroup.onlineCPU(currTime, cpu, true, mBuffer) > 0) {
					mBuffer->perfCounterHeader(currTime);
					gSessionData->perf.read(mBuffer, cpu);
					mBuffer->perfCounterFooter(currTime);
					ret = true;
				}
			}
			mBuffer->commit(currTime);

			gSessionData->readCpuInfo();
			gSessionData->perf.coreName(currTime, &mSummary, cpu);
			mSummary.commit(currTime);
			return ret;
		} else if (strcmp(result.mAction, "offline") == 0) {
			const bool ret = mCountersGroup.offlineCPU(cpu);
			mBuffer->offlineCPU(currTime, cpu);
			return ret;
		}
	}

	return true;
}

void PerfSource::interrupt() {
	if (mInterruptFd >= 0) {
		int8_t c = 0;
		// Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
		if (::write(mInterruptFd, &c, sizeof(c)) != sizeof(c)) {
			logg->logError("write failed");
			handleException();
		}
	}
}

bool PerfSource::isDone () {
	return mBuffer->isDone() && mIsDone && mCountersBuf.isEmpty();
}

void PerfSource::write (Sender *sender) {
	if (!mSummary.isDone()) {
		mSummary.write(sender);
		gSessionData->mSentSummary = true;
	}
	if (!mBuffer->isDone()) {
		mBuffer->write(sender);
	}
	if (!mCountersBuf.send(sender)) {
		logg->logError("PerfBuffer::send failed");
		handleException();
	}
}
