/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Monitor.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "Logging.h"

Monitor::Monitor() : mFd(-1) {
}

Monitor::~Monitor() {
	if (mFd >= 0) {
		::close(mFd);
	}
}

void Monitor::close() {
	if (mFd >= 0) {
		::close(mFd);
		mFd = -1;
	}
}

bool Monitor::init() {
#ifdef EPOLL_CLOEXEC
	mFd = epoll_create1(EPOLL_CLOEXEC);
#else
	mFd = epoll_create(16);
#endif
	if (mFd < 0) {
		logg->logMessage("%s(%s:%i): epoll_create1 failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

#ifndef EPOLL_CLOEXEC
  int fdf = fcntl(mFd, F_GETFD);
  if ((fdf == -1) || (fcntl(mFd, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
		logg->logMessage("%s(%s:%i): fcntl failed", __FUNCTION__, __FILE__, __LINE__);
    ::close(mFd);
    return -1;
  }
#endif

	return true;
}

bool Monitor::add(const int fd) {
	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.data.fd = fd;
	event.events = EPOLLIN;
	if (epoll_ctl(mFd, EPOLL_CTL_ADD, fd, &event) != 0) {
		logg->logMessage("%s(%s:%i): epoll_ctl failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	return true;
}

int Monitor::wait(struct epoll_event *const events, int maxevents, int timeout) {
	int result = epoll_wait(mFd, events, maxevents, timeout);
	if (result < 0) {
		// Ignore if the call was interrupted as this will happen when SIGINT is received
		if (errno == EINTR) {
			result = 0;
		} else {
			logg->logMessage("%s(%s:%i): epoll_wait failed", __FUNCTION__, __FILE__, __LINE__);
		}
	}

	return result;
}
