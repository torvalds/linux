/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERF_BUFFER
#define PERF_BUFFER

#include "Config.h"

#define BUF_SIZE (gSessionData->mTotalBufferSize * 1024 * 1024)
#define BUF_MASK (BUF_SIZE - 1)

class Sender;

class PerfBuffer {
public:
	PerfBuffer();
	~PerfBuffer();

	bool useFd(const int cpu, const int fd);
	void discard(const int cpu);
	bool isEmpty();
	bool send(Sender *const sender);

private:
	void *mBuf[NR_CPUS];
	// After the buffer is flushed it should be unmaped
	bool mDiscard[NR_CPUS];
	// fd that corresponds to the mBuf
	int mFds[NR_CPUS];

	// Intentionally undefined
	PerfBuffer(const PerfBuffer &);
	PerfBuffer &operator=(const PerfBuffer &);
};

#endif // PERF_BUFFER
