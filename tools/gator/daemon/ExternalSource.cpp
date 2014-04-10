/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ExternalSource.h"

#include <sys/prctl.h>

#include "Logging.h"
#include "OlySocket.h"
#include "SessionData.h"

ExternalSource::ExternalSource(sem_t *senderSem) : mBuffer(0, FRAME_EXTERNAL, 1024, senderSem), mSock("/tmp/gator") {
}

ExternalSource::~ExternalSource() {
}

bool ExternalSource::prepare() {
	return true;
}

void ExternalSource::run() {
	prctl(PR_SET_NAME, (unsigned long)&"gatord-uds", 0, 0, 0);

	while (gSessionData->mSessionIsActive) {
		// Will be aborted when the socket is closed at the end of the capture
		int length = mSock.receive(mBuffer.getWritePos(), mBuffer.contiguousSpaceAvailable());
		if (length <= 0) {
			break;
		}

		mBuffer.advanceWrite(length);
		mBuffer.check(0);
	}

	mBuffer.setDone();
}

void ExternalSource::interrupt() {
	// Do nothing
}

bool ExternalSource::isDone() {
	return mBuffer.isDone();
}

void ExternalSource::write(Sender *sender) {
	if (!mBuffer.isDone()) {
		mBuffer.write(sender);
	}
}
