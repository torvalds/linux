/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SOURCE_H
#define SOURCE_H

#include <pthread.h>

class Sender;

class Source {
public:
	Source();
	virtual ~Source();

	virtual bool prepare() = 0;
	void start();
	virtual void run() = 0;
	virtual void interrupt() = 0;
	void join();

	virtual bool isDone() = 0;
	virtual void write(Sender *sender) = 0;

private:
	static void *runStatic(void *arg);

	pthread_t mThreadID;

	// Intentionally undefined
	Source(const Source &);
	Source &operator=(const Source &);
};

#endif // SOURCE_H
