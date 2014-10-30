/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
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

#include "DriverSource.h"
#include "Logging.h"
#include "SessionData.h"

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
			logg->logError(__FILE__, __LINE__, "sigaction failed: %s\n", strerror(errno));
			handleException();
		}
	}

	if (DriverSource::readIntDriver("/sys/kernel/debug/tracing/tracing_on", &mTracingOn)) {
		logg->logError(__FILE__, __LINE__, "Unable to read if ftrace is enabled");
		handleException();
	}

	if (DriverSource::writeDriver("/sys/kernel/debug/tracing/tracing_on", "0") != 0) {
		logg->logError(__FILE__, __LINE__, "Unable to turn ftrace off before truncating the buffer");
		handleException();
	}

	{
		int fd;
		fd = open("/sys/kernel/debug/tracing/trace", O_WRONLY | O_TRUNC | O_CLOEXEC, 0666);
		if (fd < 0) {
			logg->logError(__FILE__, __LINE__, "Unable truncate ftrace buffer: %s", strerror(errno));
			handleException();
		}
		close(fd);
	}

	if (DriverSource::writeDriver("/sys/kernel/debug/tracing/trace_clock", "perf") != 0) {
		logg->logError(__FILE__, __LINE__, "Unable to switch ftrace to the perf clock, please ensure you are running Linux 3.10 or later");
		handleException();
	}

	mFtraceFh = fopen_cloexec("/sys/kernel/debug/tracing/trace_pipe", "rb");
	if (mFtraceFh == NULL) {
		logg->logError(__FILE__, __LINE__, "Unable to open trace_pipe");
		handleException();
	}

	return true;
}

void FtraceSource::run() {
	prctl(PR_SET_NAME, (unsigned long)&"gatord-ftrace", 0, 0, 0);
	mTid = syscall(__NR_gettid);

	if (DriverSource::writeDriver("/sys/kernel/debug/tracing/tracing_on", "1") != 0) {
		logg->logError(__FILE__, __LINE__, "Unable to turn ftrace on");
		handleException();
	}

	while (gSessionData->mSessionIsActive) {
		char buf[1<<12];

		if (fgets(buf, sizeof(buf), mFtraceFh) == NULL) {
			if (errno == EINTR) {
				// Interrupted by interrupt - likely user request to terminate
				break;
			}
			logg->logError(__FILE__, __LINE__, "Unable read trace data: %s", strerror(errno));
			handleException();
		}

		const uint64_t currTime = getTime();

		char *const colon = strstr(buf, ": ");
		if (colon == NULL) {
			logg->logError(__FILE__, __LINE__, "Unable find colon: %s", buf);
			handleException();
		}
		*colon = '\0';

		char *const space = strrchr(buf, ' ');
		if (space == NULL) {
			logg->logError(__FILE__, __LINE__, "Unable find space: %s", buf);
			handleException();
		}
		*colon = ':';

		int64_t *data = NULL;
		int count = gSessionData->ftraceDriver.read(colon + 2, &data);
		if (count > 0) {
			errno = 0;
			const long long time = strtod(space, NULL) * 1000000000;
			if (errno != 0) {
				logg->logError(__FILE__, __LINE__, "Unable to parse time: %s", strerror(errno));
				handleException();
			}
			mBuffer.event64(-1, time);

			for (int i = 0; i < count; ++i) {
				mBuffer.event64(data[2*i + 0], data[2*i + 1]);
			}

			mBuffer.check(currTime);
		}

	}

	mBuffer.setDone();

	DriverSource::writeDriver("/sys/kernel/debug/tracing/tracing_on", mTracingOn);
	fclose(mFtraceFh);
	DriverSource::writeDriver("/sys/kernel/debug/tracing/trace_clock", "local");
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
