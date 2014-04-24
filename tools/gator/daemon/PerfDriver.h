/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERFDRIVER_H
#define PERFDRIVER_H

#include "Driver.h"

// If debugfs is not mounted at /sys/kernel/debug, update DEBUGFS_PATH
#define DEBUGFS_PATH "/sys/kernel/debug"
#define EVENTS_PATH DEBUGFS_PATH "/tracing/events"

#define SCHED_SWITCH "sched/sched_switch"

class Buffer;
class DynBuf;
class PerfCounter;
class PerfGroup;

class PerfDriver : public Driver {
public:
	PerfDriver();
	~PerfDriver();

	bool setup();
	bool summary(Buffer *const buffer);
	bool isSetup() const { return mIsSetup; }

	bool claimCounter(const Counter &counter) const;
	void resetCounters();
	void setupCounter(Counter &counter);

	int writeCounters(mxml_node_t *root) const;

	bool enable(PerfGroup *group, Buffer *const buffer) const;

	static long long getTracepointId(const char *const name, DynBuf *const printb);

private:
	PerfCounter *findCounter(const Counter &counter) const;
	void addCpuCounters(const char *const counterName, const int type, const int numCounters);

	PerfCounter *mCounters;
	bool mIsSetup;

	// Intentionally undefined
	PerfDriver(const PerfDriver &);
	PerfDriver &operator=(const PerfDriver &);
};

#endif // PERFDRIVER_H
