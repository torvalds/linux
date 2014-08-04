/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MALIVIDEODRIVER_H
#define MALIVIDEODRIVER_H

#include "Driver.h"

class MaliVideoCounter;

enum MaliVideoCounterType {
	MVCT_COUNTER,
	MVCT_EVENT,
	MVCT_ACTIVITY,
};

class MaliVideoDriver : public Driver {
public:
	MaliVideoDriver();
	~MaliVideoDriver();

	void setup(mxml_node_t *const xml);

	bool claimCounter(const Counter &counter) const;
	bool countersEnabled() const;
	void resetCounters();
	void setupCounter(Counter &counter);

	int writeCounters(mxml_node_t *root) const;

	bool start(const int mveUds);

private:
	MaliVideoCounter *findCounter(const Counter &counter) const;
	void marshalEnable(const MaliVideoCounterType type, char *const buf, const size_t bufsize, int &pos);

	MaliVideoCounter *mCounters;
	int mActivityCount;

	// Intentionally unimplemented
	MaliVideoDriver(const MaliVideoDriver &);
	MaliVideoDriver &operator=(const MaliVideoDriver &);
};

#endif // MALIVIDEODRIVER_H
