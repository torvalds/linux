/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRIVERSOURCE_H
#define DRIVERSOURCE_H

#include <semaphore.h>
#include <stdint.h>

#include "Source.h"

class Fifo;

class DriverSource : public Source {
public:
	DriverSource(sem_t *senderSem, sem_t *startProfile);
	~DriverSource();

	bool prepare();
	void run();
	void interrupt();

	bool isDone();
	void write(Sender *sender);

	static int readIntDriver(const char *fullpath, int *value);
	static int readInt64Driver(const char *fullpath, int64_t *value);
	static int writeDriver(const char *fullpath, const char *data);
	static int writeDriver(const char *path, int value);
	static int writeDriver(const char *path, int64_t value);
	static int writeReadDriver(const char *path, int *value);
	static int writeReadDriver(const char *path, int64_t *value);

private:
	Fifo *mFifo;
	sem_t *const mSenderSem;
	sem_t *const mStartProfile;
	int mBufferSize;
	int mBufferFD;
	int mLength;

	// Intentionally unimplemented
	DriverSource(const DriverSource &);
	DriverSource &operator=(const DriverSource &);
};

#endif // DRIVERSOURCE_H
