/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FTRACEDRIVER_H
#define FTRACEDRIVER_H

#include "Driver.h"

class FtraceDriver : public SimpleDriver {
public:
	FtraceDriver();
	~FtraceDriver();

	void readEvents(mxml_node_t *const xml);

	int read(const char *line, int64_t **buf);

private:
	int64_t *mValues;

	// Intentionally unimplemented
	FtraceDriver(const FtraceDriver &);
	FtraceDriver &operator=(const FtraceDriver &);
};

#endif // FTRACEDRIVER_H
