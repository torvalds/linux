/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Buffer.h"

#include "Logging.h"
#include "Sender.h"
#include "SessionData.h"

#define mask (mSize - 1)

enum {
	CODE_PEA    = 1,
	CODE_KEYS   = 2,
	CODE_FORMAT = 3,
	CODE_MAPS   = 4,
	CODE_COMM   = 5,
};

// Summary Frame Messages
enum {
	MESSAGE_SUMMARY = 1,
	MESSAGE_CORE_NAME = 3,
};

// From gator_marshaling.c
#define NEWLINE_CANARY \
	/* Unix */ \
	"1\n" \
	/* Windows */ \
	"2\r\n" \
	/* Mac OS */ \
	"3\r" \
	/* RISC OS */ \
	"4\n\r" \
	/* Add another character so the length isn't 0x0a bytes */ \
	"5"

Buffer::Buffer(const int32_t core, const int32_t buftype, const int size, sem_t *const readerSem) : mCore(core), mBufType(buftype), mSize(size), mReadPos(0), mWritePos(0), mCommitPos(0), mAvailable(true), mIsDone(false), mBuf(new char[mSize]), mCommitTime(gSessionData->mLiveRate), mReaderSem(readerSem) {
	if ((mSize & mask) != 0) {
		logg->logError(__FILE__, __LINE__, "Buffer size is not a power of 2");
		handleException();
	}
	frame();
}

Buffer::~Buffer() {
	delete [] mBuf;
}

void Buffer::write(Sender *const sender) {
	if (!commitReady()) {
		return;
	}

	// determine the size of two halves
	int length1 = mCommitPos - mReadPos;
	char *buffer1 = mBuf + mReadPos;
	int length2 = 0;
	char *buffer2 = mBuf;
	if (length1 < 0) {
		length1 = mSize - mReadPos;
		length2 = mCommitPos;
	}

	logg->logMessage("Sending data length1: %i length2: %i", length1, length2);

	// start, middle or end
	if (length1 > 0) {
		sender->writeData(buffer1, length1, RESPONSE_APC_DATA);
	}

	// possible wrap around
	if (length2 > 0) {
		sender->writeData(buffer2, length2, RESPONSE_APC_DATA);
	}

	mReadPos = mCommitPos;
}

bool Buffer::commitReady() const {
	return mCommitPos != mReadPos;
}

int Buffer::bytesAvailable() const {
	int filled = mWritePos - mReadPos;
	if (filled < 0) {
		filled += mSize;
	}

	int remaining = mSize - filled;

	if (mAvailable) {
		// Give some extra room; also allows space to insert the overflow error packet
		remaining -= 200;
	} else {
		// Hysteresis, prevents multiple overflow messages
		remaining -= 2000;
	}

	return remaining;
}

bool Buffer::checkSpace(const int bytes) {
	const int remaining = bytesAvailable();

	if (remaining < bytes) {
		mAvailable = false;
	} else {
		mAvailable = true;
	}

	return mAvailable;
}

int Buffer::contiguousSpaceAvailable() const {
	int remaining = bytesAvailable();
	int contiguous = mSize - mWritePos;
	if (remaining < contiguous) {
		return remaining;
	} else {
		return contiguous;
	}
}

void Buffer::commit(const uint64_t time) {
	// post-populate the length, which does not include the response type length nor the length itself, i.e. only the length of the payload
	const int typeLength = gSessionData->mLocalCapture ? 0 : 1;
	int length = mWritePos - mCommitPos;
	if (length < 0) {
		length += mSize;
	}
	length = length - typeLength - sizeof(int32_t);
	for (size_t byte = 0; byte < sizeof(int32_t); byte++) {
		mBuf[(mCommitPos + typeLength + byte) & mask] = (length >> byte * 8) & 0xFF;
	}

	logg->logMessage("Committing data mReadPos: %i mWritePos: %i mCommitPos: %i", mReadPos, mWritePos, mCommitPos);
	mCommitPos = mWritePos;

	if (gSessionData->mLiveRate > 0) {
		while (time > mCommitTime) {
			mCommitTime += gSessionData->mLiveRate;
		}
	}

	if (!mIsDone) {
		frame();
	}

	// send a notification that data is ready
	sem_post(mReaderSem);
}

void Buffer::check(const uint64_t time) {
	int filled = mWritePos - mCommitPos;
	if (filled < 0) {
		filled += mSize;
	}
	if (filled >= ((mSize * 3) / 4) || (gSessionData->mLiveRate > 0 && time >= mCommitTime)) {
		commit(time);
	}
}

void Buffer::packInt(int32_t x) {
	int packedBytes = 0;
	int more = true;
	while (more) {
		// low order 7 bits of x
		char b = x & 0x7f;
		x >>= 7;

		if ((x == 0 && (b & 0x40) == 0) || (x == -1 && (b & 0x40) != 0)) {
			more = false;
		} else {
			b |= 0x80;
		}

		mBuf[(mWritePos + packedBytes) & mask] = b;
		packedBytes++;
	}

	mWritePos = (mWritePos + packedBytes) & mask;
}

void Buffer::packInt64(int64_t x) {
	int packedBytes = 0;
	int more = true;
	while (more) {
		// low order 7 bits of x
		char b = x & 0x7f;
		x >>= 7;

		if ((x == 0 && (b & 0x40) == 0) || (x == -1 && (b & 0x40) != 0)) {
			more = false;
		} else {
			b |= 0x80;
		}

		mBuf[(mWritePos + packedBytes) & mask] = b;
		packedBytes++;
	}

	mWritePos = (mWritePos + packedBytes) & mask;
}

void Buffer::writeBytes(const void *const data, size_t count) {
	size_t i;
	for (i = 0; i < count; ++i) {
		mBuf[(mWritePos + i) & mask] = static_cast<const char *>(data)[i];
	}

	mWritePos = (mWritePos + i) & mask;
}

void Buffer::writeString(const char *const str) {
	const int len = strlen(str);
	packInt(len);
	writeBytes(str, len);
}

void Buffer::frame() {
	if (!gSessionData->mLocalCapture) {
		packInt(RESPONSE_APC_DATA);
	}
	// Reserve space for the length
	mWritePos += sizeof(int32_t);
	packInt(mBufType);
	packInt(mCore);
}

void Buffer::summary(const int64_t timestamp, const int64_t uptime, const int64_t monotonicDelta, const char *const uname) {
	packInt(MESSAGE_SUMMARY);
	writeString(NEWLINE_CANARY);
	packInt64(timestamp);
	packInt64(uptime);
	packInt64(monotonicDelta);
	writeString("uname");
	writeString(uname);
	writeString("");
	check(1);
}

void Buffer::coreName(const int core, const int cpuid, const char *const name) {
	if (checkSpace(3 * MAXSIZE_PACK32 + 0x100)) {
		packInt(MESSAGE_CORE_NAME);
		packInt(core);
		packInt(cpuid);
		writeString(name);
	}
	check(1);
}

bool Buffer::eventHeader(const uint64_t curr_time) {
	bool retval = false;
	if (checkSpace(MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
		packInt(0);	// key of zero indicates a timestamp
		packInt64(curr_time);
		retval = true;
	}

	return retval;
}

bool Buffer::eventTid(const int tid) {
	bool retval = false;
	if (checkSpace(2 * MAXSIZE_PACK32)) {
		packInt(1);	// key of 1 indicates a tid
		packInt(tid);
		retval = true;
	}

	return retval;
}

void Buffer::event(const int32_t key, const int32_t value) {
	if (checkSpace(2 * MAXSIZE_PACK32)) {
		packInt(key);
		packInt(value);
	}
}

void Buffer::event64(const int64_t key, const int64_t value) {
	if (checkSpace(2 * MAXSIZE_PACK64)) {
		packInt64(key);
		packInt64(value);
	}
}

void Buffer::pea(const struct perf_event_attr *const pea, int key) {
	if (checkSpace(2 * MAXSIZE_PACK32 + pea->size)) {
		packInt(CODE_PEA);
		writeBytes(pea, pea->size);
		packInt(key);
	} else {
		logg->logError(__FILE__, __LINE__, "Ran out of buffer space for perf attrs");
		handleException();
	}
	// Don't know the real perf time so use 1 as it will work for now
	check(1);
}

void Buffer::keys(const int count, const __u64 *const ids, const int *const keys) {
	if (checkSpace(2 * MAXSIZE_PACK32 + count * (MAXSIZE_PACK32 + MAXSIZE_PACK64))) {
		packInt(CODE_KEYS);
		packInt(count);
		for (int i = 0; i < count; ++i) {
			packInt64(ids[i]);
			packInt(keys[i]);
		}
	} else {
		logg->logError(__FILE__, __LINE__, "Ran out of buffer space for perf attrs");
		handleException();
	}
	check(1);
}

void Buffer::format(const int length, const char *const format) {
	if (checkSpace(MAXSIZE_PACK32 + length + 1)) {
		packInt(CODE_FORMAT);
		writeBytes(format, length + 1);
	} else {
		logg->logError(__FILE__, __LINE__, "Ran out of buffer space for perf attrs");
		handleException();
	}
	check(1);
}

void Buffer::maps(const int pid, const int tid, const char *const maps) {
	const int mapsLen = strlen(maps) + 1;
	if (checkSpace(3 * MAXSIZE_PACK32 + mapsLen)) {
		packInt(CODE_MAPS);
		packInt(pid);
		packInt(tid);
		writeBytes(maps, mapsLen);
	} else {
		logg->logError(__FILE__, __LINE__, "Ran out of buffer space for perf attrs");
		handleException();
	}
	check(1);
}

void Buffer::comm(const int pid, const int tid, const char *const image, const char *const comm) {
	const int imageLen = strlen(image) + 1;
	const int commLen = strlen(comm) + 1;
	if (checkSpace(3 * MAXSIZE_PACK32 + imageLen + commLen)) {
		packInt(CODE_COMM);
		packInt(pid);
		packInt(tid);
		writeBytes(image, imageLen);
		writeBytes(comm, commLen);
	} else {
		logg->logError(__FILE__, __LINE__, "Ran out of buffer space for perf attrs");
		handleException();
	}
	check(1);
}

void Buffer::setDone() {
	mIsDone = true;
	commit(0);
}

bool Buffer::isDone() const {
	return mIsDone && mReadPos == mCommitPos && mCommitPos == mWritePos;
}
