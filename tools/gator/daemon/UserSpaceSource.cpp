/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define __STDC_FORMAT_MACROS

#include "UserSpaceSource.h"

#include <inttypes.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "Child.h"
#include "DriverSource.h"
#include "Logging.h"
#include "SessionData.h"

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

	for (int i = 0; i < ARRAY_LENGTH(gSessionData->usDrivers); ++i) {
		gSessionData->usDrivers[i]->start();
	}

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
			gSessionData->mMonotonicStarted = monotonicStarted;
		}
	}

	uint64_t nextTime = 0;
	while (gSessionData->mSessionIsActive) {
		const uint64_t currTime = getTime() - monotonicStarted;
		// Sample ten times a second ignoring gSessionData->mSampleRate
		nextTime += NS_PER_S/10;//gSessionData->mSampleRate;
		if (nextTime < currTime) {
			logg->logMessage("Too slow, currTime: %" PRIi64 " nextTime: %" PRIi64, currTime, nextTime);
			nextTime = currTime;
		}

		if (mBuffer.eventHeader(currTime)) {
			for (int i = 0; i < ARRAY_LENGTH(gSessionData->usDrivers); ++i) {
				gSessionData->usDrivers[i]->read(&mBuffer);
			}
			// Only check after writing all counters so that time and corresponding counters appear in the same frame
			mBuffer.check(currTime);
		}

		if (gSessionData->mOneShot && gSessionData->mSessionIsActive && (mBuffer.bytesAvailable() <= 0)) {
			logg->logMessage("One shot (counters)");
			child->endSession();
		}

		usleep((nextTime - currTime)/NS_PER_US);
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
