/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
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

static bool sendTracepointFormat(const uint64_t currTime, Buffer *const buffer, const char *const name, DynBuf *const printb, DynBuf *const b) {
	if (!printb->printf(EVENTS_PATH "/%s/format", name)) {
		logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}
	if (!b->read(printb->getBuf())) {
		logg->logMessage("%s(%s:%i): DynBuf::read failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}
	buffer->format(currTime, b->getLength(), b->getBuf());

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
			logg->logError(__FILE__, __LINE__, "sigfillset failed");
			handleException();
		}
		if ((err = pthread_sigmask(SIG_SETMASK, &set, NULL)) != 0) {
			logg->logError(__FILE__, __LINE__, "pthread_sigmask failed");
			handleException();
		}
	}

	for (;;) {
		if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
			logg->logError(__FILE__, __LINE__, "clock_gettime failed");
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
		logg->logError(__FILE__, __LINE__, "Unable to determine the number of cores on the target, opendir failed");
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
		logg->logError(__FILE__, __LINE__, "Unable to determine the number of cores on the target, no cpu# directories found");
		handleException();
	}

	if (maxCoreNum >= NR_CPUS) {
		logg->logError(__FILE__, __LINE__, "Too many cores on the target, please increase NR_CPUS in Config.h");
		handleException();
	}

	return maxCoreNum;
}

PerfSource::PerfSource(sem_t *senderSem, sem_t *startProfile) : mSummary(0, FRAME_SUMMARY, 1024, senderSem), mBuffer(0, FRAME_PERF_ATTRS, 1024*1024, senderSem), mCountersBuf(), mCountersGroup(&mCountersBuf), mIdleGroup(&mCountersBuf), mMonitor(), mUEvent(), mSenderSem(senderSem), mStartProfile(startProfile), mInterruptFd(-1), mIsDone(false) {
	long l = sysconf(_SC_PAGE_SIZE);
	if (l < 0) {
		logg->logError(__FILE__, __LINE__, "Unable to obtain the page size");
		handleException();
	}
	gSessionData->mPageSize = static_cast<int>(l);
	gSessionData->mCores = static_cast<int>(getMaxCoreNum());
}

PerfSource::~PerfSource() {
}

bool PerfSource::prepare() {
	DynBuf printb;
	DynBuf b1;
	long long schedSwitchId;
	long long cpuIdleId;

	const uint64_t currTime = getTime();

	// Reread cpuinfo since cores may have changed since startup
	gSessionData->readCpuInfo();

	if (0
			|| !mMonitor.init()
			|| !mUEvent.init()
			|| !mMonitor.add(mUEvent.getFd())

			|| (schedSwitchId = PerfDriver::getTracepointId(SCHED_SWITCH, &printb)) < 0
			|| !sendTracepointFormat(currTime, &mBuffer, SCHED_SWITCH, &printb, &b1)

			|| (cpuIdleId = PerfDriver::getTracepointId(CPU_IDLE, &printb)) < 0
			|| !sendTracepointFormat(currTime, &mBuffer, CPU_IDLE, &printb, &b1)

			// Only want RAW but not IP on sched_switch and don't want TID on SAMPLE_ID
			|| !mCountersGroup.add(currTime, &mBuffer, 100/**/, PERF_TYPE_TRACEPOINT, schedSwitchId, 1, PERF_SAMPLE_RAW, PERF_GROUP_MMAP | PERF_GROUP_COMM | PERF_GROUP_TASK | PERF_GROUP_SAMPLE_ID_ALL | PERF_GROUP_PER_CPU)
			|| !mIdleGroup.add(currTime, &mBuffer, 101/**/, PERF_TYPE_TRACEPOINT, cpuIdleId, 1, PERF_SAMPLE_RAW, PERF_GROUP_PER_CPU)

			// Only want TID and IP but not RAW on timer
			|| (gSessionData->mSampleRate > 0 && !gSessionData->mIsEBS && !mCountersGroup.add(currTime, &mBuffer, 102/**/, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK, 1000000000UL / gSessionData->mSampleRate, PERF_SAMPLE_TID | PERF_SAMPLE_IP, PERF_GROUP_PER_CPU))

			|| !gSessionData->perf.enable(currTime, &mCountersGroup, &mBuffer)
			|| 0) {
		logg->logMessage("%s(%s:%i): perf setup failed, are you running Linux 3.4 or later?", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		const int result = mCountersGroup.prepareCPU(cpu, &mMonitor);
		if ((result != PG_SUCCESS) && (result != PG_CPU_OFFLINE)) {
			logg->logError(__FILE__, __LINE__, "PerfGroup::prepareCPU on mCountersGroup failed");
			handleException();
		}
	}
	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		const int result = mIdleGroup.prepareCPU(cpu, &mMonitor);
		if ((result != PG_SUCCESS) && (result != PG_CPU_OFFLINE)) {
			logg->logError(__FILE__, __LINE__, "PerfGroup::prepareCPU on mIdleGroup failed");
			handleException();
		}
	}

	int numEvents = 0;
	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		numEvents += mCountersGroup.onlineCPU(currTime, cpu, false, &mBuffer);
	}
	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		numEvents += mIdleGroup.onlineCPU(currTime, cpu, false, &mBuffer);
	}
	if (numEvents <= 0) {
		logg->logMessage("%s(%s:%i): PerfGroup::onlineCPU failed on all cores", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	// Send the summary right before the start so that the monotonic delta is close to the start time
	if (!gSessionData->perf.summary(&mSummary)) {
	  logg->logError(__FILE__, __LINE__, "PerfDriver::summary failed", __FUNCTION__, __FILE__, __LINE__);
	  handleException();
	}

	// Start the timer thread to used to sync perf and monotonic raw times
	pthread_t syncThread;
	if (pthread_create(&syncThread, NULL, syncFunc, NULL)) {
	  logg->logError(__FILE__, __LINE__, "pthread_create failed", __FUNCTION__, __FILE__, __LINE__);
	  handleException();
	}
	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if (pthread_setschedparam(syncThread, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
	  logg->logError(__FILE__, __LINE__, "pthread_setschedparam failed");
	  handleException();
	}

	mBuffer.commit(currTime);

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
		logg->logError(__FILE__, __LINE__, "setpriority failed");
		handleException();
	}

	if (!readProcMaps(args->mCurrTime, args->mBuffer, &printb, &b)) {
		logg->logError(__FILE__, __LINE__, "readProcMaps failed");
		handleException();
	}
	args->mBuffer->commit(args->mCurrTime);

	if (!readKallsyms(args->mCurrTime, args->mBuffer, &args->mIsDone)) {
		logg->logError(__FILE__, __LINE__, "readKallsyms failed");
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

	{
		DynBuf printb;
		DynBuf b1;
		DynBuf b2;

		const uint64_t currTime = getTime();

		// Start events before reading proc to avoid race conditions
		if (!mCountersGroup.start() || !mIdleGroup.start()) {
			logg->logError(__FILE__, __LINE__, "PerfGroup::start failed", __FUNCTION__, __FILE__, __LINE__);
			handleException();
		}

		if (!readProcComms(currTime, &mBuffer, &printb, &b1, &b2)) {
			logg->logError(__FILE__, __LINE__, "readProcComms failed");
			handleException();
		}
		mBuffer.commit(currTime);

		// Postpone reading kallsyms as on android adb gets too backed up and data is lost
		procThreadArgs.mBuffer = &mBuffer;
		procThreadArgs.mCurrTime = currTime;
		procThreadArgs.mIsDone = false;
		if (pthread_create(&procThread, NULL, procFunc, &procThreadArgs)) {
			logg->logError(__FILE__, __LINE__, "pthread_create failed", __FUNCTION__, __FILE__, __LINE__);
			handleException();
		}
	}

	if (pipe_cloexec(pipefd) != 0) {
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
		timeout = gSessionData->mLiveRate/NS_PER_MS;
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
		const uint64_t currTime = getTime();

		for (int i = 0; i < ready; ++i) {
			if (events[i].data.fd == mUEvent.getFd()) {
				if (!handleUEvent(currTime)) {
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

	procThreadArgs.mIsDone = true;
	pthread_join(procThread, NULL);
	mIdleGroup.stop();
	mCountersGroup.stop();
	mBuffer.setDone();
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

		if (cpu >= gSessionData->mCores) {
			logg->logError(__FILE__, __LINE__, "Only %i cores are expected but core %i reports %s", gSessionData->mCores, cpu, result.mAction);
			handleException();
		}

		if (strcmp(result.mAction, "online") == 0) {
			mBuffer.onlineCPU(currTime, currTime - gSessionData->mMonotonicStarted, cpu);
			// Only call onlineCPU if prepareCPU succeeded
			bool result = false;
			int err = mCountersGroup.prepareCPU(cpu, &mMonitor);
			if (err == PG_CPU_OFFLINE) {
				result = true;
			} else if (err == PG_SUCCESS) {
				if (mCountersGroup.onlineCPU(currTime, cpu, true, &mBuffer)) {
					err = mIdleGroup.prepareCPU(cpu, &mMonitor);
					if (err == PG_CPU_OFFLINE) {
						result = true;
					} else if (err == PG_SUCCESS) {
						if (mIdleGroup.onlineCPU(currTime, cpu, true, &mBuffer)) {
							result = true;
						}
					}
				}
			}
			mBuffer.commit(currTime);

			gSessionData->readCpuInfo();
			gSessionData->perf.coreName(currTime, &mSummary, cpu);
			mSummary.commit(currTime);
			return result;
		} else if (strcmp(result.mAction, "offline") == 0) {
			const bool result = mCountersGroup.offlineCPU(cpu) && mIdleGroup.offlineCPU(cpu);
			mBuffer.offlineCPU(currTime, currTime - gSessionData->mMonotonicStarted, cpu);
			return result;
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
		gSessionData->mSentSummary = true;
	}
	if (!mBuffer.isDone()) {
		mBuffer.write(sender);
	}
	if (!mCountersBuf.send(sender)) {
		logg->logError(__FILE__, __LINE__, "PerfBuffer::send failed");
		handleException();
	}
}
