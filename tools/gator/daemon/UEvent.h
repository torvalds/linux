/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef UEVENT_H
#define UEVENT_H

struct UEventResult {
	const char *mAction;
	const char *mDevPath;
	const char *mSubsystem;
	char mBuf[1<<13];
};

class UEvent {
public:
	UEvent();
	~UEvent();

	bool init();
	bool read(UEventResult *const result);
	int getFd() const { return mFd; }

private:
	int mFd;

	// Intentionally undefined
	UEvent(const UEvent &);
	UEvent &operator=(const UEvent &);
};

#endif // UEVENT_H
