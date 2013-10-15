/**
 * Copyright (C) ARM Limited 2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef COUNTER_H
#define COUNTER_H

#include <string.h>

class Driver;

class Counter {
public:
	static const size_t MAX_STRING_LEN = 80;
	static const size_t MAX_DESCRIPTION_LEN = 400;

	Counter () {
		clear();
	}

	void clear () {
		mType[0] = '\0';
		mEnabled = false;
		mEvent = 0;
		mCount = 0;
		mKey = 0;
		mDriver = NULL;
	}

	void setType(const char *const type) { strncpy(mType, type, sizeof(mType)); mType[sizeof(mType) - 1] = '\0'; }
	void setEnabled(const bool enabled) { mEnabled = enabled; }
	void setEvent(const int event) { mEvent = event; }
	void setCount(const int count) { mCount = count; }
	void setKey(const int key) { mKey = key; }
	void setDriver(Driver *const driver) { mDriver = driver; }

	const char *getType() const { return mType;}
	bool isEnabled() const { return mEnabled; }
	int getEvent() const { return mEvent; }
	int getCount() const { return mCount; }
	int getKey() const { return mKey; }
	Driver *getDriver() const { return mDriver; }

private:
	// Intentionally unimplemented
	Counter(const Counter &);
	Counter & operator=(const Counter &);

	char mType[MAX_STRING_LEN];
	bool mEnabled;
	int mEvent;
	int mCount;
	int mKey;
	Driver *mDriver;
};

#endif // COUNTER_H
