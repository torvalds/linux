/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "UEvent.h"

#include <sys/socket.h>
#include <linux/netlink.h>
#include <string.h>

#include <unistd.h>

#include "Logging.h"

static const char EMPTY[] = "";
static const char ACTION[] = "ACTION=";
static const char DEVPATH[] = "DEVPATH=";
static const char SUBSYSTEM[] = "SUBSYSTEM=";

UEvent::UEvent() : mFd(-1) {
}

UEvent::~UEvent() {
	if (mFd >= 0) {
		close(mFd);
	}
}

bool UEvent::init() {
	mFd = socket(PF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
	if (mFd < 0) {
		logg->logMessage("%s(%s:%i): socket failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	struct sockaddr_nl sockaddr;
	memset(&sockaddr, 0, sizeof(sockaddr));
	sockaddr.nl_family = AF_NETLINK;
	sockaddr.nl_groups = 1; // bitmask: (1 << 0) == kernel events, (1 << 1) == udev events
	sockaddr.nl_pid = 0;
	if (bind(mFd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) != 0) {
		logg->logMessage("%s(%s:%i): bind failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	return true;
}

bool UEvent::read(UEventResult *const result) {
	ssize_t bytes = recv(mFd, result->mBuf, sizeof(result->mBuf), 0);
	if (bytes <= 0) {
		logg->logMessage("%s(%s:%i): recv failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	result->mAction = EMPTY;
	result->mDevPath = EMPTY;
	result->mSubsystem = EMPTY;

	for (int pos = 0; pos < bytes; pos += strlen(result->mBuf + pos) + 1) {
		char *const str = result->mBuf + pos;
		if (strncmp(str, ACTION, sizeof(ACTION) - 1) == 0) {
			result->mAction = str + sizeof(ACTION) - 1;
		} else if (strncmp(str, DEVPATH, sizeof(DEVPATH) - 1) == 0) {
			result->mDevPath = str + sizeof(DEVPATH) - 1;
		} else if (strncmp(str, SUBSYSTEM, sizeof(SUBSYSTEM) - 1) == 0) {
			result->mSubsystem = str + sizeof(SUBSYSTEM) - 1;
		}
	}

	return true;
}
