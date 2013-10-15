/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__COLLECTOR_H__
#define	__COLLECTOR_H__

#include <stdio.h>

class Collector {
public:
	Collector();
	~Collector();
	void start();
	void stop();
	int collect(char* buffer);
	int getBufferSize() {return mBufferSize;}

	static int readIntDriver(const char* path, int* value);
	static int readInt64Driver(const char* path, int64_t* value);
	static int writeDriver(const char* path, int value);
	static int writeDriver(const char* path, int64_t value);
	static int writeDriver(const char* path, const char* data);
	static int writeReadDriver(const char* path, int* value);
	static int writeReadDriver(const char* path, int64_t* value);

private:
	int mBufferSize;
	int mBufferFD;

	void checkVersion();
};

#endif 	//__COLLECTOR_H__
