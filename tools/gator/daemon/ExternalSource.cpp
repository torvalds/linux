/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ExternalSource.h"

#include <fcntl.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "Logging.h"
#include "OlySocket.h"
#include "SessionData.h"

static const char MALI_VIDEO[] = "\0mali-video";
static const char MALI_VIDEO_STARTUP[] = "\0mali-video-startup";
static const char MALI_VIDEO_V1[] = "MALI_VIDEO 1\n";

static bool setNonblock(const int fd) {
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		logg->logMessage("fcntl getfl failed");
		return false;
	}

	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		logg->logMessage("fcntl setfl failed");
		return false;
	}

	return true;
}

ExternalSource::ExternalSource(sem_t *senderSem) : mBuffer(0, FRAME_EXTERNAL, 128*1024, senderSem), mMonitor(), mMveStartupUds(MALI_VIDEO_STARTUP, sizeof(MALI_VIDEO_STARTUP)), mInterruptFd(-1), mMveUds(-1) {
	sem_init(&mBufferSem, 0, 0);
}

ExternalSource::~ExternalSource() {
}

void ExternalSource::waitFor(const uint64_t currTime, const int bytes) {
	while (mBuffer.bytesAvailable() <= bytes) {
		mBuffer.check(currTime);
		sem_wait(&mBufferSem);
	}
}

void ExternalSource::configureConnection(const int fd, const char *const handshake, size_t size) {
	if (!setNonblock(fd)) {
		logg->logError(__FILE__, __LINE__, "Unable to set nonblock on fh");
		handleException();
	}

	if (!mMonitor.add(fd)) {
		logg->logError(__FILE__, __LINE__, "Unable to add fh to monitor");
		handleException();
	}

	// Write the handshake to the circular buffer
	waitFor(1, Buffer::MAXSIZE_PACK32 + 4 + size - 1);
	mBuffer.packInt(fd);
	mBuffer.writeLEInt((unsigned char *)mBuffer.getWritePos(), size - 1);
	mBuffer.advanceWrite(4);
	mBuffer.writeBytes(handshake, size - 1);
}

bool ExternalSource::connectMve() {
	if (!gSessionData->maliVideo.countersEnabled()) {
		return true;
	}

	mMveUds = OlySocket::connect(MALI_VIDEO, sizeof(MALI_VIDEO));
	if (mMveUds < 0) {
		return false;
	}

	if (!gSessionData->maliVideo.start(mMveUds)) {
		return false;
	}

	configureConnection(mMveUds, MALI_VIDEO_V1, sizeof(MALI_VIDEO_V1));

	return true;
}

bool ExternalSource::prepare() {
	if (!mMonitor.init() || !setNonblock(mMveStartupUds.getFd()) || !mMonitor.add(mMveStartupUds.getFd())) {
		return false;
	}

	connectMve();

	return true;
}

void ExternalSource::run() {
	int pipefd[2];

	prctl(PR_SET_NAME, (unsigned long)&"gatord-external", 0, 0, 0);

	if (pipe(pipefd) != 0) {
		logg->logError(__FILE__, __LINE__, "pipe failed");
		handleException();
	}
	mInterruptFd = pipefd[1];

	if (!mMonitor.add(pipefd[0])) {
		logg->logError(__FILE__, __LINE__, "Monitor::add failed");
		handleException();
	}

	while (gSessionData->mSessionIsActive) {
		struct epoll_event events[16];
		// Clear any pending sem posts
		while (sem_trywait(&mBufferSem) == 0);
		int ready = mMonitor.wait(events, ARRAY_LENGTH(events), -1);
		if (ready < 0) {
			logg->logError(__FILE__, __LINE__, "Monitor::wait failed");
			handleException();
		}

		const uint64_t currTime = getTime();

		for (int i = 0; i < ready; ++i) {
			const int fd = events[i].data.fd;
			if (fd == mMveStartupUds.getFd()) {
				// Mali Video Engine says it's alive
				int client = mMveStartupUds.acceptConnection();
				// Don't read from this connection, establish a new connection to Mali-V500
				close(client);
				if (!connectMve()) {
					logg->logError(__FILE__, __LINE__, "Unable to configure incoming Mali video connection");
					handleException();
				}
			} else if (fd == pipefd[0]) {
				// Means interrupt has been called and mSessionIsActive should be reread
			} else {
				while (true) {
					waitFor(currTime, Buffer::MAXSIZE_PACK32 + 4);

					mBuffer.packInt(fd);
					char *const bytesPos = mBuffer.getWritePos();
					mBuffer.advanceWrite(4);
					const int contiguous = mBuffer.contiguousSpaceAvailable();
					const int bytes = read(fd, mBuffer.getWritePos(), contiguous);
					if (bytes < 0) {
						if (errno == EAGAIN) {
							// Nothing left to read, and Buffer convention dictates that writePos can't go backwards
							mBuffer.writeLEInt((unsigned char *)bytesPos, 0);
							break;
						}
						// Something else failed, close the socket
						mBuffer.writeLEInt((unsigned char *)bytesPos, -1);
						close(fd);
						break;
					} else if (bytes == 0) {
						// The other side is closed
						mBuffer.writeLEInt((unsigned char *)bytesPos, -1);
						close(fd);
						break;
					}

					mBuffer.writeLEInt((unsigned char *)bytesPos, bytes);
					mBuffer.advanceWrite(bytes);

					// Short reads also mean nothing is left to read
					if (bytes < contiguous) {
						break;
					}
				}
			}
		}

		// Only call mBufferCheck once per iteration
		mBuffer.check(currTime);
	}

	mBuffer.setDone();

	mInterruptFd = -1;
	close(pipefd[0]);
	close(pipefd[1]);
}

void ExternalSource::interrupt() {
	if (mInterruptFd >= 0) {
		int8_t c = 0;
		// Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
		if (::write(mInterruptFd, &c, sizeof(c)) != sizeof(c)) {
			logg->logError(__FILE__, __LINE__, "write failed");
			handleException();
		}
	}
}

bool ExternalSource::isDone() {
	return mBuffer.isDone();
}

void ExternalSource::write(Sender *sender) {
	// Don't send external data until the summary packet is sent so that monotonic delta is available
	if (!gSessionData->mSentSummary) {
		return;
	}
	if (!mBuffer.isDone()) {
		mBuffer.write(sender);
		sem_post(&mBufferSem);
	}
}
