/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PerfBuffer.h"

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "Buffer.h"
#include "Logging.h"
#include "Sender.h"
#include "SessionData.h"

PerfBuffer::PerfBuffer() {
	for (int cpu = 0; cpu < ARRAY_LENGTH(mBuf); ++cpu) {
		mBuf[cpu] = MAP_FAILED;
		mDiscard[cpu] = false;
	}
}

PerfBuffer::~PerfBuffer() {
	for (int cpu = ARRAY_LENGTH(mBuf) - 1; cpu >= 0; --cpu) {
		if (mBuf[cpu] != MAP_FAILED) {
			munmap(mBuf[cpu], gSessionData->mPageSize + BUF_SIZE);
		}
	}
}

bool PerfBuffer::useFd(const int cpu, const int fd, const int groupFd) {
	if (fd == groupFd) {
		if (mBuf[cpu] != MAP_FAILED) {
			logg->logMessage("%s(%s:%i): cpu %i already online or not correctly cleaned up", __FUNCTION__, __FILE__, __LINE__, cpu);
			return false;
		}

		// The buffer isn't mapped yet
		mBuf[cpu] = mmap(NULL, gSessionData->mPageSize + BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (mBuf[cpu] == MAP_FAILED) {
			logg->logMessage("%s(%s:%i): mmap failed", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}

		// Check the version
		struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(mBuf[cpu]);
		if (pemp->compat_version != 0) {
			logg->logMessage("%s(%s:%i): Incompatible perf_event_mmap_page compat_version", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}
	} else {
		if (mBuf[cpu] == MAP_FAILED) {
			logg->logMessage("%s(%s:%i): cpu already online or not correctly cleaned up", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}

		if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, groupFd) < 0) {
			logg->logMessage("%s(%s:%i): ioctl failed", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}
	}

	return true;
}

void PerfBuffer::discard(const int cpu) {
	if (mBuf[cpu] != MAP_FAILED) {
		mDiscard[cpu] = true;
	}
}

bool PerfBuffer::isEmpty() {
	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		if (mBuf[cpu] != MAP_FAILED) {
			// Take a snapshot of the positions
			struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(mBuf[cpu]);
			const __u64 head = pemp->data_head;
			const __u64 tail = pemp->data_tail;

			if (head != tail) {
				return false;
			}
		}
	}

	return true;
}

bool PerfBuffer::send(Sender *const sender) {
	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		if (mBuf[cpu] == MAP_FAILED) {
			continue;
		}

		// Take a snapshot of the positions
		struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(mBuf[cpu]);
		const __u64 head = pemp->data_head;
		const __u64 tail = pemp->data_tail;

		if (head > tail) {
			const uint8_t *const b = static_cast<uint8_t *>(mBuf[cpu]) + gSessionData->mPageSize;
			const int offset = gSessionData->mLocalCapture ? 1 : 0;
			unsigned char header[7];
			header[0] = RESPONSE_APC_DATA;
			Buffer::writeLEInt(header + 1, head - tail + sizeof(header) - 5);
			// Should use real packing functions
			header[5] = FRAME_PERF;
			header[6] = cpu;

			// Write header
			sender->writeData(reinterpret_cast<const char *>(&header) + offset, sizeof(header) - offset, RESPONSE_APC_DATA);

			// Write data
			if ((head & ~BUF_MASK) == (tail & ~BUF_MASK)) {
				// Not wrapped
				sender->writeData(reinterpret_cast<const char *>(b + (tail & BUF_MASK)), head - tail, RESPONSE_APC_DATA);
			} else {
				// Wrapped
				sender->writeData(reinterpret_cast<const char *>(b + (tail & BUF_MASK)), BUF_SIZE - (tail & BUF_MASK), RESPONSE_APC_DATA);
				sender->writeData(reinterpret_cast<const char *>(b), head & BUF_MASK, RESPONSE_APC_DATA);
			}

			// Update tail with the data read
			pemp->data_tail = head;
		}

		if (mDiscard[cpu]) {
			munmap(mBuf[cpu], gSessionData->mPageSize + BUF_SIZE);
			mBuf[cpu] = MAP_FAILED;
			mDiscard[cpu] = false;
			logg->logMessage("%s(%s:%i): Unmaped cpu %i", __FUNCTION__, __FILE__, __LINE__, cpu);
		}
	}

	return true;
}
