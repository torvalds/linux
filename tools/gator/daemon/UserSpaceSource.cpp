/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "UserSpaceSource.h"

#include <sys/prctl.h>
#include <unistd.h>

#include "Child.h"
#include "DriverSource.h"
#include "Logging.h"
#include "SessionData.h"

#define NS_PER_S ((uint64_t)1000000000)
#define NS_PER_US 1000

extern Child *child;

UserSpaceSource::UserSpaceSource(sem_t *senderSem) : mBuffer(0, FRAME_BLOCK_COUNTER, gSessionData->mTotalBufferSize*1024*1024, senderSem) {
}

UserSpaceSource::~UserSpaceSource() {
}

bool UserSpaceSource::prepare() {
	return true;
}

void UserSpaceSource::run() {
	prctl(PR_SET_NAME, (unsigned long)&"gatord-counters", 0, 0, 0);

	gSessionData->hwmon.start();

	int64_t monotonic_started = 0;
	while (monotonic_started <= 0) {
		usleep(10);

		if (DriverSource::readInt64Driver("/dev/gator/started", &monotonic_started) == -1) {
			logg->logError(__FILE__, __LINE__, "Error reading gator driver start time");
			handleException();
		}
	}

	uint64_t next_time = 0;
	while (gSessionData->mSessionIsActive) {
		struct timespec ts;
#ifndef CLOCK_MONOTONIC_RAW
		// Android doesn't have this defined but it was added in Linux 2.6.28
#define CLOCK_MONOTONIC_RAW 4
#endif
		if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
			logg->logError(__FILE__, __LINE__, "Failed to get uptime");
			handleException();
		}
		const uint64_t curr_time = (NS_PER_S*ts.tv_sec + ts.tv_nsec) - monotonic_started;
		// Sample ten times a second ignoring gSessionData->mSampleRate
		next_time += NS_PER_S/10;//gSessionData->mSampleRate;
		if (next_time < curr_time) {
			logg->logMessage("Too slow, curr_time: %lli next_time: %lli", curr_time, next_time);
			next_time = curr_time;
		}

		if (mBuffer.eventHeader(curr_time)) {
			gSessionData->hwmon.read(&mBuffer);
			// Only check after writing all counters so that time and corresponding counters appear in the same frame
			mBuffer.check(curr_time);
		}

		if (mBuffer.bytesAvailable() <= 0) {
			logg->logMessage("One shot (counters)");
			child->endSession();
		}

		usleep((next_time - curr_time)/NS_PER_US);
	}

	mBuffer.setDone();
}

void UserSpaceSource::interrupt() {
	// Do nothing
}

bool UserSpaceSource::isDone() {
	return mBuffer.isDone();
}

void UserSpaceSource::write(Sender *sender) {
	if (!mBuffer.isDone()) {
		mBuffer.write(sender);
	}
}
