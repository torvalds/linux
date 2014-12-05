/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "MemInfoDriver.h"

#include "Logging.h"
#include "SessionData.h"

class MemInfoCounter : public DriverCounter {
public:
	MemInfoCounter(DriverCounter *next, char *const name, int64_t *const value);
	~MemInfoCounter();

	int64_t read();

private:
	int64_t *const mValue;

	// Intentionally unimplemented
	MemInfoCounter(const MemInfoCounter &);
	MemInfoCounter &operator=(const MemInfoCounter &);
};

MemInfoCounter::MemInfoCounter(DriverCounter *next, char *const name, int64_t *const value) : DriverCounter(next, name), mValue(value) {
}

MemInfoCounter::~MemInfoCounter() {
}

int64_t MemInfoCounter::read() {
	return *mValue;
}

MemInfoDriver::MemInfoDriver() : mBuf(), mMemUsed(0), mMemFree(0), mBuffers(0) {
}

MemInfoDriver::~MemInfoDriver() {
}

void MemInfoDriver::readEvents(mxml_node_t *const) {
	// Only for use with perf
	if (!gSessionData->perf.isSetup()) {
		return;
	}

	setCounters(new MemInfoCounter(getCounters(), strdup("Linux_meminfo_memused2"), &mMemUsed));
	setCounters(new MemInfoCounter(getCounters(), strdup("Linux_meminfo_memfree"), &mMemFree));
	setCounters(new MemInfoCounter(getCounters(), strdup("Linux_meminfo_bufferram"), &mBuffers));
}

void MemInfoDriver::read(Buffer *const buffer) {
	if (!countersEnabled()) {
		return;
	}

	if (!mBuf.read("/proc/meminfo")) {
		logg->logError(__FILE__, __LINE__, "Failed to read /proc/meminfo");
		handleException();
	}

	char *key = mBuf.getBuf();
	char *colon;
	int64_t memTotal = 0;
	while ((colon = strchr(key, ':')) != NULL) {
		char *end = strchr(colon + 1, '\n');
		if (end != NULL) {
			*end = '\0';
		}
		*colon = '\0';

		if (strcmp(key, "MemTotal") == 0) {
			memTotal = strtoll(colon + 1, NULL, 10) << 10;
		} else if (strcmp(key, "MemFree") == 0) {
			mMemFree = strtoll(colon + 1, NULL, 10) << 10;
		} else if (strcmp(key, "Buffers") == 0) {
			mBuffers = strtoll(colon + 1, NULL, 10) << 10;
		}

		if (end == NULL) {
			break;
		}
		key = end + 1;
	}

	mMemUsed = memTotal - mMemFree;

	super::read(buffer);
}
