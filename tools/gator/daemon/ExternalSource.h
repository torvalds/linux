/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXTERNALSOURCE_H
#define EXTERNALSOURCE_H

#include <semaphore.h>

#include "Buffer.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "Source.h"

// Counters from external sources like graphics drivers and annotations
class ExternalSource : public Source {
public:
	ExternalSource(sem_t *senderSem);
	~ExternalSource();

	bool prepare();
	void run();
	void interrupt();

	bool isDone();
	void write(Sender *sender);

private:
	void waitFor(const int bytes);
	void configureConnection(const int fd, const char *const handshake, size_t size);
	bool connectMali();
	bool connectMve();

	sem_t mBufferSem;
	Buffer mBuffer;
	Monitor mMonitor;
	OlyServerSocket mMveStartupUds;
	OlyServerSocket mMaliStartupUds;
	OlyServerSocket mAnnotate;
	int mInterruptFd;
	int mMaliUds;
	int mMveUds;

	// Intentionally unimplemented
	ExternalSource(const ExternalSource &);
	ExternalSource &operator=(const ExternalSource &);
};

#endif // EXTERNALSOURCE_H
