/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MEMINFODRIVER_H
#define MEMINFODRIVER_H

#include "Driver.h"
#include "DynBuf.h"

class MemInfoDriver : public PolledDriver {
private:
	typedef PolledDriver super;

public:
	MemInfoDriver();
	~MemInfoDriver();

	void readEvents(mxml_node_t *const root);
	void read(Buffer *const buffer);

private:
	DynBuf mBuf;
	int64_t mMemUsed;
	int64_t mMemFree;
	int64_t mBuffers;

	// Intentionally unimplemented
	MemInfoDriver(const MemInfoDriver &);
	MemInfoDriver &operator=(const MemInfoDriver &);
};

#endif // MEMINFODRIVER_H
