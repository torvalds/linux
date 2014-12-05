/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CPUFREQDRIVER_H
#define CPUFREQDRIVER_H

#include "Config.h"
#include "Driver.h"

class CPUFreqDriver : public PolledDriver {
private:
	typedef PolledDriver super;

public:
	CPUFreqDriver();
	~CPUFreqDriver();

	void readEvents(mxml_node_t *const root);
	void read(Buffer *const buffer);

private:
	int64_t mPrev[NR_CPUS];

	// Intentionally unimplemented
	CPUFreqDriver(const CPUFreqDriver &);
	CPUFreqDriver &operator=(const CPUFreqDriver &);
};

#endif // CPUFREQDRIVER_H
