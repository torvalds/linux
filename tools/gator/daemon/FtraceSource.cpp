/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "FtraceSource.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "Child.h"
#include "DriverSource.h"
#include "Logging.h"
#include "SessionData.h"

extern Child *child;

static void handler(int signum)
{
	(void)signum;
};

FtraceSource::FtraceSource(sem_t *senderSem) : mFtraceFh(NULL), mBuffer(0, FRAME_BLOCK_COUNTER, 128*1024, senderSem), mTid(-1), mTracingOn(0) {
}

FtraceSource::~FtraceSource() {
}

bool FtraceSource::prepare() {
	{
		struct sigaction act;
		act.sa_handler = handler;
		act.sa_flags = (int)SA_RESETHAND;
		if (sigaction(SIGUSR1, &act, NULL) != 0) {
			logg->logError("sigaction failed: %s\n", strerror(errno));
			handleException();
		}
	}

	gSessionData->ftraceDriver.prepare();

	if (DriverSource::readIntDriver("/sys/kernel/debug/tracing/tracing_on", &mTracingOn)) {
		logg->logError("Unable to read if ftrace is enabled");
		handleException();
	}

	if (DriverSource::writeDriver("/sys/kernel/debug/tracing/tracing_on", "0") != 0) {
		logg->logError("Unable to turn ftrace off before truncating the buffer");
		handleException();
	}

	{
		int fd;
		fd = open("/sys/kernel/debug/tracing/trace", O_WRONLY | O_TRUNC | O_CLOEXEC, 0666);
		if (fd < 0) {
			logg->logError("Unable truncate ftrace buffer: %s", strerror(errno));
			handleException();
		}
		close(fd);
	}

	if (DriverSource::writeDriver("/sys/kernel/debug/tracing/trace_clock", "perf") != 0) {
		logg->logError("Unable to switch ftrace to the perf clock, please ensure you are running Linux 3.10 or later");
		handleException();
	}

	mFtraceFh = fopen_cloexec("/sys/kernel/debug/tracing/trace_pipe", "rb");
	if (mFtraceFh == NULL) {
		logg->logError("Unable to open trace_pipe");
		handleException();
	}

	return true;
}

void FtraceSource::run() {
	prctl(PR_SET_NAME, (unsigned long)&"gatord-ftrace", 0, 0, 0);
	mTid = syscall(__NR_gettid);

	if (DriverSource::writeDriver("/sys/kernel/debug/tracing/tracing_on", "1") != 0) {
		logg->logError("Unable to turn ftrace on");
		handleException();
	}

	// Wait until monotonicStarted is set before sending data
	int64_t monotonicStarted = 0;
	while (monotonicStarted <= 0 && gSessionData->mSessionIsActive) {
		usleep(10);

		if (gSessionData->perf.isSetup()) {
			monotonicStarted = gSessionData->mMonotonicStarted;
		} else {
			if (DriverSource::readInt64Driver("/dev/gator/started", &monotonicStarted) == -1) {
				logg->logError("Error reading gator driver start time");
				handleException();
			}
		}
	}

	while (gSessionData->mSessionIsActive) {
		char buf[1<<12];

		if (fgets(buf, sizeof(buf), mFtraceFh) == NULL) {
			if (errno == EINTR) {
				// Interrupted by interrupt - likely user request to terminate
				break;
			}
			logg->logError("Unable read trace data: %s", strerror(errno));
			handleException();
		}

		const uint64_t currTime = getTime() - gSessionData->mMonotonicStarted;

		char *const colon = strstr(buf, ": ");
		if (colon == NULL) {
			if (strstr(buf, " [LOST ") != NULL) {
				logg->logError("Ftrace events lost, aborting the capture. It is recommended to discard this report and collect a new capture. If this error occurs often, please reduce the number of ftrace counters selected or the amount of ftrace events generated.");
			} else {
				logg->logError("Unable to find colon: %s", buf);
			}
			handleException();
		}
		*colon = '\0';

		char *const space = strrchr(buf, ' ');
		if (space == NULL) {
			logg->logError("Unable to find space: %s", buf);
			handleException();
		}
		*colon = ':';

		int64_t *data = NULL;
		int count = gSessionData->ftraceDriver.read(colon + 2, &data);
		if (count > 0) {
			errno = 0;
			const long long time = strtod(space, NULL) * 1000000000;
			if (errno != 0) {
				logg->logError("Unable to parse time: %s", strerror(errno));
				handleException();
			}
			mBuffer.event64(-1, time);

			for (int i = 0; i < count; ++i) {
				mBuffer.event64(data[2*i + 0], data[2*i + 1]);
			}

			mBuffer.check(currTime);

			if (gSessionData->mOneShot && gSessionData->mSessionIsActive && (mBuffer.bytesAvailable() <= 0)) {
				logg->logMessage("One shot (ftrace)");
				child->endSession();
			}
		}

	}

	mBuffer.setDone();

	DriverSource::writeDriver("/sys/kernel/debug/tracing/tracing_on", mTracingOn);
	fclose(mFtraceFh);
	DriverSource::writeDriver("/sys/kernel/debug/tracing/trace_clock", "local");
	gSessionData->ftraceDriver.stop();
}

void FtraceSource::interrupt() {
	// Closing the underlying file handle does not result in the read on the ftrace file handle to return, so send a signal to the thread
	syscall(__NR_tgkill, getpid(), mTid, SIGUSR1);
}

bool FtraceSource::isDone() {
	return mBuffer.isDone();
}

void FtraceSource::write(Sender *sender) {
	// Don't send ftrace data until the summary packet is sent so that monotonic delta is available
	if (!gSessionData->mSentSummary) {
		return;
	}
	if (!mBuffer.isDone()) {
		mBuffer.write(sender);
	}
}
