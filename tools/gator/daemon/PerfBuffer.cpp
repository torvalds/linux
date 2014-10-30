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
		mFds[cpu] = -1;
	}
}

PerfBuffer::~PerfBuffer() {
	for (int cpu = ARRAY_LENGTH(mBuf) - 1; cpu >= 0; --cpu) {
		if (mBuf[cpu] != MAP_FAILED) {
			munmap(mBuf[cpu], gSessionData->mPageSize + BUF_SIZE);
		}
	}
}

bool PerfBuffer::useFd(const int cpu, const int fd) {
	if (mFds[cpu] < 0) {
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
		mFds[cpu] = fd;

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

		if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, mFds[cpu]) < 0) {
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

static void compressAndSend(const int cpu, const __u64 head, __u64 tail, const uint8_t *const b, Sender *const sender) {
	// Pick a big size but something smaller than the chunkSize in Sender::writeData which is 100k
	char buf[1<<16];
	int writePos = 0;
	const int typeLength = gSessionData->mLocalCapture ? 0 : 1;

	while (head > tail) {
		writePos = 0;
		if (!gSessionData->mLocalCapture) {
			buf[writePos++] = RESPONSE_APC_DATA;
		}
		// Reserve space for size
		writePos += sizeof(uint32_t);
		Buffer::packInt(buf, sizeof(buf), writePos, FRAME_PERF);
		Buffer::packInt(buf, sizeof(buf), writePos, cpu);

		while (head > tail) {
			const int count = reinterpret_cast<const struct perf_event_header *>(b + (tail & BUF_MASK))->size/sizeof(uint64_t);
			// Can this whole message be written as Streamline assumes events are not split between frames
			if (sizeof(buf) <= writePos + count*Buffer::MAXSIZE_PACK64) {
				break;
			}
			for (int i = 0; i < count; ++i) {
				// Must account for message size
				Buffer::packInt64(buf, sizeof(buf), writePos, *reinterpret_cast<const uint64_t *>(b + (tail & BUF_MASK)));
				tail += sizeof(uint64_t);
			}
		}

		// Write size
		Buffer::writeLEInt(reinterpret_cast<unsigned char *>(buf + typeLength), writePos - typeLength - sizeof(uint32_t));
		sender->writeData(buf, writePos, RESPONSE_APC_DATA);
	}
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
			compressAndSend(cpu, head, tail, b, sender);

			// Update tail with the data read
			pemp->data_tail = head;
		}

		if (mDiscard[cpu]) {
			munmap(mBuf[cpu], gSessionData->mPageSize + BUF_SIZE);
			mBuf[cpu] = MAP_FAILED;
			mDiscard[cpu] = false;
			mFds[cpu] = -1;
			logg->logMessage("%s(%s:%i): Unmaped cpu %i", __FUNCTION__, __FILE__, __LINE__, cpu);
		}
	}

	return true;
}
