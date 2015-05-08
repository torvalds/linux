/**
 * Copyright (C) ARM Limited 2013-2015. All rights reserved.
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
			logg->logMessage("cpu %i already online or not correctly cleaned up", cpu);
			return false;
		}

		// The buffer isn't mapped yet
		mBuf[cpu] = mmap(NULL, gSessionData->mPageSize + BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (mBuf[cpu] == MAP_FAILED) {
			logg->logMessage("mmap failed");
			return false;
		}
		mFds[cpu] = fd;

		// Check the version
		struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(mBuf[cpu]);
		if (pemp->compat_version != 0) {
			logg->logMessage("Incompatible perf_event_mmap_page compat_version");
			return false;
		}
	} else {
		if (mBuf[cpu] == MAP_FAILED) {
			logg->logMessage("cpu already online or not correctly cleaned up");
			return false;
		}

		if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, mFds[cpu]) < 0) {
			logg->logMessage("ioctl failed");
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
			const __u64 head = ACCESS_ONCE(pemp->data_head);
			const __u64 tail = ACCESS_ONCE(pemp->data_tail);

			if (head != tail) {
				return false;
			}
		}
	}

	return true;
}

bool PerfBuffer::isFull() {
	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		if (mBuf[cpu] != MAP_FAILED) {
			// Take a snapshot of the positions
			struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(mBuf[cpu]);
			const __u64 head = ACCESS_ONCE(pemp->data_head);

			if (head + 2000 <= (unsigned int)BUF_SIZE) {
				return true;
			}
		}
	}

	return false;
}

class PerfFrame {
public:
	PerfFrame(Sender *const sender) : mSender(sender), mWritePos(-1), mCpuSizePos(-1) {}

	void add(const int cpu, const __u64 head, __u64 tail, const uint8_t *const b) {
		cpuHeader(cpu);

		while (head > tail) {
			const int count = reinterpret_cast<const struct perf_event_header *>(b + (tail & BUF_MASK))->size/sizeof(uint64_t);
			// Can this whole message be written as Streamline assumes events are not split between frames
			if (sizeof(mBuf) <= mWritePos + count*Buffer::MAXSIZE_PACK64) {
				send();
				cpuHeader(cpu);
			}
			for (int i = 0; i < count; ++i) {
				// Must account for message size
				Buffer::packInt64(mBuf, sizeof(mBuf), mWritePos, *reinterpret_cast<const uint64_t *>(b + (tail & BUF_MASK)));
				tail += sizeof(uint64_t);
			}
		}
	}

	void send() {
		if (mWritePos > 0) {
			writeFrameSize();
			mSender->writeData(mBuf, mWritePos, RESPONSE_APC_DATA);
			mWritePos = -1;
			mCpuSizePos = -1;
		}
	}

private:
	void writeFrameSize() {
		writeCpuSize();
		const int typeLength = gSessionData->mLocalCapture ? 0 : 1;
		Buffer::writeLEInt(reinterpret_cast<unsigned char *>(mBuf + typeLength), mWritePos - typeLength - sizeof(uint32_t));
	}

	void frameHeader() {
		if (mWritePos < 0) {
			mWritePos = 0;
			mCpuSizePos = -1;
			if (!gSessionData->mLocalCapture) {
				mBuf[mWritePos++] = RESPONSE_APC_DATA;
			}
			// Reserve space for frame size
			mWritePos += sizeof(uint32_t);
			Buffer::packInt(mBuf, sizeof(mBuf), mWritePos, FRAME_PERF);
		}
	}

	void writeCpuSize() {
		if (mCpuSizePos >= 0) {
			Buffer::writeLEInt(reinterpret_cast<unsigned char *>(mBuf + mCpuSizePos), mWritePos - mCpuSizePos - sizeof(uint32_t));
		}
	}

	void cpuHeader(const int cpu) {
		if (sizeof(mBuf) <= mWritePos + Buffer::MAXSIZE_PACK32 + sizeof(uint32_t)) {
			send();
		}
		frameHeader();
		writeCpuSize();
		Buffer::packInt(mBuf, sizeof(mBuf), mWritePos, cpu);
		mCpuSizePos = mWritePos;
		// Reserve space for cpu size
		mWritePos += sizeof(uint32_t);
	}

	// Pick a big size but something smaller than the chunkSize in Sender::writeData which is 100k
	char mBuf[1<<16];
	Sender *const mSender;
	int mWritePos;
	int mCpuSizePos;

	// Intentionally unimplemented
	PerfFrame(const PerfFrame &);
	PerfFrame& operator=(const PerfFrame &);
};

bool PerfBuffer::send(Sender *const sender) {
	PerfFrame frame(sender);

	for (int cpu = 0; cpu < gSessionData->mCores; ++cpu) {
		if (mBuf[cpu] == MAP_FAILED) {
			continue;
		}

		// Take a snapshot of the positions
		struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(mBuf[cpu]);
		const __u64 head = ACCESS_ONCE(pemp->data_head);
		const __u64 tail = ACCESS_ONCE(pemp->data_tail);

		if (head > tail) {
			const uint8_t *const b = static_cast<uint8_t *>(mBuf[cpu]) + gSessionData->mPageSize;
			frame.add(cpu, head, tail, b);

			// Update tail with the data read
			pemp->data_tail = head;
		}

		if (mDiscard[cpu]) {
			munmap(mBuf[cpu], gSessionData->mPageSize + BUF_SIZE);
			mBuf[cpu] = MAP_FAILED;
			mDiscard[cpu] = false;
			mFds[cpu] = -1;
			logg->logMessage("Unmaped cpu %i", cpu);
		}
	}

	frame.send();

	return true;
}
