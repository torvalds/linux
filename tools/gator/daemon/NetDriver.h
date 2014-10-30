/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NETDRIVER_H
#define NETDRIVER_H

#include "Driver.h"
#include "DynBuf.h"

class NetDriver : public PolledDriver {
private:
	typedef PolledDriver super;

public:
	NetDriver();
	~NetDriver();

	void readEvents(mxml_node_t *const root);
	void start();
	void read(Buffer *const buffer);

private:
	bool doRead();

	DynBuf mBuf;
	int64_t mReceiveBytes;
	int64_t mTransmitBytes;

	// Intentionally unimplemented
	NetDriver(const NetDriver &);
	NetDriver &operator=(const NetDriver &);
};

#endif // NETDRIVER_H
