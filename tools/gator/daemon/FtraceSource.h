/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FTRACESOURCE_H
#define FTRACESOURCE_H

#include <semaphore.h>
#include <stdio.h>

#include "Buffer.h"
#include "Source.h"

class FtraceSource : public Source {
public:
	FtraceSource(sem_t *senderSem);
	~FtraceSource();

	bool prepare();
	void run();
	void interrupt();

	bool isDone();
	void write(Sender *sender);

private:
	void waitFor(const int bytes);

	FILE *mFtraceFh;
	Buffer mBuffer;
	int mTid;
	int mTracingOn;

	// Intentionally unimplemented
	FtraceSource(const FtraceSource &);
	FtraceSource &operator=(const FtraceSource &);
};

#endif // FTRACESOURCE_H
