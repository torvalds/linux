/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FSDRIVER_H
#define FSDRIVER_H

#include "Driver.h"

class FSDriver : public PolledDriver {
public:
	FSDriver();
	~FSDriver();

	void readEvents(mxml_node_t *const xml);

	int writeCounters(mxml_node_t *root) const;

private:
	// Intentionally unimplemented
	FSDriver(const FSDriver &);
	FSDriver &operator=(const FSDriver &);
};

#endif // FSDRIVER_H
