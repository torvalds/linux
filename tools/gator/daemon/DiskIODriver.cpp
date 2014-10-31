/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

// Define to get format macros from inttypes.h
#define __STDC_FORMAT_MACROS

#include "DiskIODriver.h"

#include <inttypes.h>

#include "Logging.h"
#include "SessionData.h"

class DiskIOCounter : public DriverCounter {
public:
	DiskIOCounter(DriverCounter *next, char *const name, int64_t *const value);
	~DiskIOCounter();

	int64_t read();

private:
	int64_t *const mValue;
	int64_t mPrev;

	// Intentionally unimplemented
	DiskIOCounter(const DiskIOCounter &);
	DiskIOCounter &operator=(const DiskIOCounter &);
};

DiskIOCounter::DiskIOCounter(DriverCounter *next, char *const name, int64_t *const value) : DriverCounter(next, name), mValue(value), mPrev(0) {
}

DiskIOCounter::~DiskIOCounter() {
}

int64_t DiskIOCounter::read() {
	int64_t result = *mValue - mPrev;
	mPrev = *mValue;
	// Kernel assumes a sector is 512 bytes
	return result << 9;
}

DiskIODriver::DiskIODriver() : mBuf(), mReadBytes(0), mWriteBytes(0) {
}

DiskIODriver::~DiskIODriver() {
}

void DiskIODriver::readEvents(mxml_node_t *const) {
	// Only for use with perf
	if (!gSessionData->perf.isSetup()) {
		return;
	}

	setCounters(new DiskIOCounter(getCounters(), strdup("Linux_block_rq_rd"), &mReadBytes));
	setCounters(new DiskIOCounter(getCounters(), strdup("Linux_block_rq_wr"), &mWriteBytes));
}

void DiskIODriver::doRead() {
	if (!countersEnabled()) {
		return;
	}

	if (!mBuf.read("/proc/diskstats")) {
		logg->logError(__FILE__, __LINE__, "Unable to read /proc/diskstats");
		handleException();
	}

	mReadBytes = 0;
	mWriteBytes = 0;

	char *lastName = NULL;
	int lastNameLen = -1;
	char *start = mBuf.getBuf();
	while (*start != '\0') {
		char *end = strchr(start, '\n');
		if (end != NULL) {
			*end = '\0';
		}

		int nameStart = -1;
		int nameEnd = -1;
		int64_t readBytes = -1;
		int64_t writeBytes = -1;
		const int count = sscanf(start, "%*d %*d %n%*s%n %*u %*u %" SCNu64 " %*u %*u %*u %" SCNu64, &nameStart, &nameEnd, &readBytes, &writeBytes);
		if (count != 2) {
			logg->logError(__FILE__, __LINE__, "Unable to parse /proc/diskstats");
			handleException();
		}

		// Skip partitions which are identified if the name is a substring of the last non-partition
		if ((lastName == NULL) || (strncmp(lastName, start + nameStart, lastNameLen) != 0)) {
			lastName = start + nameStart;
			lastNameLen = nameEnd - nameStart;
			mReadBytes += readBytes;
			mWriteBytes += writeBytes;
		}

		if (end == NULL) {
			break;
		}
		start = end + 1;
	}
}

void DiskIODriver::start() {
	doRead();
	// Initialize previous values
	for (DriverCounter *counter = getCounters(); counter != NULL; counter = counter->getNext()) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->read();
	}
}

void DiskIODriver::read(Buffer *const buffer) {
	doRead();
	super::read(buffer);
}
