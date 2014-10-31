/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERFSOURCE_H
#define PERFSOURCE_H

#include <semaphore.h>

#include "Buffer.h"
#include "Monitor.h"
#include "PerfBuffer.h"
#include "PerfGroup.h"
#include "Source.h"
#include "UEvent.h"

class Sender;

class PerfSource : public Source {
public:
	PerfSource(sem_t *senderSem, sem_t *startProfile);
	~PerfSource();

	bool prepare();
	void run();
	void interrupt();

	bool isDone();
	void write(Sender *sender);

private:
	bool handleUEvent(const uint64_t currTime);

	Buffer mSummary;
	Buffer mBuffer;
	PerfBuffer mCountersBuf;
	PerfGroup mCountersGroup;
	PerfGroup mIdleGroup;
	Monitor mMonitor;
	UEvent mUEvent;
	sem_t *const mSenderSem;
	sem_t *const mStartProfile;
	int mInterruptFd;
	bool mIsDone;

	// Intentionally undefined
	PerfSource(const PerfSource &);
	PerfSource &operator=(const PerfSource &);
};

#endif // PERFSOURCE_H
