/**
 * Copyright (C) ARM Limited 2013-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERFDRIVER_H
#define PERFDRIVER_H

#include <stdint.h>

#include "Driver.h"

// If debugfs is not mounted at /sys/kernel/debug, update DEBUGFS_PATH
#define DEBUGFS_PATH "/sys/kernel/debug"
#define EVENTS_PATH DEBUGFS_PATH "/tracing/events"

#define SCHED_SWITCH "sched/sched_switch"
#define CPU_IDLE "power/cpu_idle"
#define CPU_FREQUENCY "power/cpu_frequency"

class Buffer;
class DynBuf;
class PerfGroup;

class PerfDriver : public SimpleDriver {
public:
	PerfDriver();
	~PerfDriver();

	bool getLegacySupport() const { return mLegacySupport; }

	bool setup();
	bool summary(Buffer *const buffer);
	void coreName(const uint64_t currTime, Buffer *const buffer, const int cpu);
	bool isSetup() const { return mIsSetup; }

	void setupCounter(Counter &counter);

	bool enable(const uint64_t currTime, PerfGroup *const group, Buffer *const buffer) const;
	void read(Buffer *const buffer, const int cpu);

	static long long getTracepointId(const char *const name, DynBuf *const printb);

private:
	void addCpuCounters(const char *const counterName, const int type, const int numCounters);
	void addUncoreCounters(const char *const counterName, const int type, const int numCounters, const bool hasCyclesCounter);

	bool mIsSetup;
	bool mLegacySupport;

	// Intentionally undefined
	PerfDriver(const PerfDriver &);
	PerfDriver &operator=(const PerfDriver &);
};

#endif // PERFDRIVER_H
