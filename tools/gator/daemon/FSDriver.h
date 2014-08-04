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

class Buffer;
class FSCounter;

class FSDriver : public Driver {
public:
	FSDriver();
	~FSDriver();

	void setup(mxml_node_t *const xml);

	bool claimCounter(const Counter &counter) const;
	bool countersEnabled() const;
	void resetCounters();
	void setupCounter(Counter &counter);

	int writeCounters(mxml_node_t *root) const;

	void start();
	void read(Buffer * buffer);

private:
	FSCounter *findCounter(const Counter &counter) const;

	FSCounter *counters;

	// Intentionally unimplemented
	FSDriver(const FSDriver &);
	FSDriver &operator=(const FSDriver &);
};

#endif // FSDRIVER_H
