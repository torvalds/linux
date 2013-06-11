/**
 * Copyright (C) ARM Limited 2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Buffer.h"

#include "Logging.h"
#include "Sender.h"
#include "SessionData.h"

#define mask (size - 1)

Buffer::Buffer (const int32_t core, const int32_t buftype, const int size, sem_t *const readerSem) : core(core), buftype(buftype), size(size), readPos(0), writePos(0), commitPos(0), available(true), done(false), buf(new char[size]),
#ifdef GATOR_LIVE
		commitTime(gSessionData->mLiveRate),
#endif
		readerSem(readerSem) {
	if ((size & mask) != 0) {
		logg->logError(__FILE__, __LINE__, "Buffer size is not a power of 2");
		handleException();
	}
	frame();
}

Buffer::~Buffer () {
	delete [] buf;
}

void Buffer::write (Sender * const sender) {
	if (!commitReady()) {
		return;
	}

	// determine the size of two halves
	int length1 = commitPos - readPos;
	char * buffer1 = buf + readPos;
	int length2 = 0;
	char * buffer2 = buf;
	if (length1 < 0) {
		length1 = size - readPos;
		length2 = commitPos;
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

	readPos = commitPos;
}

bool Buffer::commitReady () const {
	return commitPos != readPos;
}

int Buffer::bytesAvailable () const {
	int filled = writePos - readPos;
	if (filled < 0) {
		filled += size;
	}

	int remaining = size - filled;

	if (available) {
		// Give some extra room; also allows space to insert the overflow error packet
		remaining -= 200;
	} else {
		// Hysteresis, prevents multiple overflow messages
		remaining -= 2000;
	}

	return remaining;
}

bool Buffer::checkSpace (const int bytes) {
	const int remaining = bytesAvailable();

	if (remaining < bytes) {
		available = false;
	} else {
		available = true;
	}

	return available;
}

void Buffer::commit (const uint64_t time) {
	// post-populate the length, which does not include the response type length nor the length itself, i.e. only the length of the payload
	const int typeLength = gSessionData->mLocalCapture ? 0 : 1;
	int length = writePos - commitPos;
	if (length < 0) {
		length += size;
	}
	length = length - typeLength - sizeof(int32_t);
	for (size_t byte = 0; byte < sizeof(int32_t); byte++) {
		buf[(commitPos + typeLength + byte) & mask] = (length >> byte * 8) & 0xFF;
	}

	logg->logMessage("Committing data readPos: %i writePos: %i commitPos: %i", readPos, writePos, commitPos);
	commitPos = writePos;

#ifdef GATOR_LIVE
	if (gSessionData->mLiveRate > 0) {
		while (time > commitTime) {
			commitTime += gSessionData->mLiveRate;
		}
	}
#endif

	if (!done) {
		frame();
	}

	// send a notification that data is ready
	sem_post(readerSem);
}

void Buffer::check (const uint64_t time) {
	int filled = writePos - commitPos;
	if (filled < 0) {
		filled += size;
	}
	if (filled >= ((size * 3) / 4)
#ifdef GATOR_LIVE
			|| (gSessionData->mLiveRate > 0 && time >= commitTime)
#endif
			) {
		commit(time);
	}
}

void Buffer::packInt (int32_t x) {
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

		buf[(writePos + packedBytes) & mask] = b;
		packedBytes++;
	}

	writePos = (writePos + packedBytes) & mask;
}

void Buffer::packInt64 (int64_t x) {
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

		buf[(writePos + packedBytes) & mask] = b;
		packedBytes++;
	}

	writePos = (writePos + packedBytes) & mask;
}

void Buffer::frame () {
	if (!gSessionData->mLocalCapture) {
		packInt(RESPONSE_APC_DATA);
	}
	// Reserve space for the length
	writePos += sizeof(int32_t);
	packInt(buftype);
	packInt(core);
}

bool Buffer::eventHeader (const uint64_t curr_time) {
	bool retval = false;
	if (checkSpace(MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
		packInt(0);	// key of zero indicates a timestamp
		packInt64(curr_time);
		retval = true;
	}

	return retval;
}

void Buffer::event (const int32_t key, const int32_t value) {
	if (checkSpace(2 * MAXSIZE_PACK32)) {
		packInt(key);
		packInt(value);
	}
}

void Buffer::event64 (const int64_t key, const int64_t value) {
	if (checkSpace(2 * MAXSIZE_PACK64)) {
		packInt64(key);
		packInt64(value);
	}
}

void Buffer::setDone () {
	done = true;
	commit(0);
}

bool Buffer::isDone () const {
	return done && readPos == commitPos && commitPos == writePos;
}
