/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CCNDRIVER_H
#define CCNDRIVER_H

#include "Driver.h"

class CCNDriver : public Driver {
public:
	CCNDriver();
	~CCNDriver();

	bool claimCounter(const Counter &counter) const;
	void resetCounters();
	void setupCounter(Counter &counter);

	void readEvents(mxml_node_t *const);
	int writeCounters(mxml_node_t *const root) const;
	void writeEvents(mxml_node_t *const) const;

private:
	enum NodeType {
		NT_UNKNOWN,
		NT_HNF,
		NT_RNI,
		NT_SBAS,
	};

	NodeType *mNodeTypes;
	int mXpCount;

	// Intentionally unimplemented
	CCNDriver(const CCNDriver &);
	CCNDriver &operator=(const CCNDriver &);
};

#endif // CCNDRIVER_H
