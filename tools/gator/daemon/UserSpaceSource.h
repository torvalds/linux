/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef USERSPACESOURCE_H
#define USERSPACESOURCE_H

#include <semaphore.h>

#include "Buffer.h"
#include "Source.h"

// User space counters - currently just hwmon
class UserSpaceSource : public Source {
public:
	UserSpaceSource(sem_t *senderSem);
	~UserSpaceSource();

	bool prepare();
	void run();
	void interrupt();

	bool isDone();
	void write(Sender *sender);

private:
	Buffer mBuffer;

	// Intentionally unimplemented
	UserSpaceSource(const UserSpaceSource &);
	UserSpaceSource &operator=(const UserSpaceSource &);
};

#endif // USERSPACESOURCE_H
