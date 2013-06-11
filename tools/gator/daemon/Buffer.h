/**
 * Copyright (C) ARM Limited 2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <semaphore.h>

#define GATOR_LIVE

class Sender;

class Buffer {
public:
	static const size_t MAXSIZE_PACK32 = 5;
	static const size_t MAXSIZE_PACK64 = 10;

	Buffer (int32_t core, int32_t buftype, const int size, sem_t *const readerSem);
	~Buffer ();

	void write (Sender * sender);

	int bytesAvailable () const;
	void commit (const uint64_t time);
	void check (const uint64_t time);

	void frame ();

	bool eventHeader (uint64_t curr_time);
	void event (int32_t key, int32_t value);
	void event64 (int64_t key, int64_t value);

	void setDone ();
	bool isDone () const;

private:
	bool commitReady () const;
	bool checkSpace (int bytes);

	void packInt (int32_t x);
	void packInt64 (int64_t x);

	const int32_t core;
	const int32_t buftype;
	const int size;
	int readPos;
	int writePos;
	int commitPos;
	bool available;
	bool done;
	char *const buf;
#ifdef GATOR_LIVE
	uint64_t commitTime;
#endif

	sem_t *const readerSem;
};

#endif // BUFFER_H
