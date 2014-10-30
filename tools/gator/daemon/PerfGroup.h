 /**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERF_GROUP
#define PERF_GROUP

#include <stdint.h>

// Use a snapshot of perf_event.h as it may be more recent than what is on the target and if not newer features won't be supported anyways
#include "k/perf_event.h"

#include "Config.h"

class Buffer;
class Monitor;
class PerfBuffer;

enum PerfGroupFlags {
	PERF_GROUP_MMAP          = 1 << 0,
	PERF_GROUP_COMM          = 1 << 1,
	PERF_GROUP_FREQ          = 1 << 2,
	PERF_GROUP_TASK          = 1 << 3,
	PERF_GROUP_SAMPLE_ID_ALL = 1 << 4,
	PERF_GROUP_PER_CPU       = 1 << 5,
};

enum {
	PG_SUCCESS = 0,
	PG_FAILURE,
	PG_CPU_OFFLINE,
};

class PerfGroup {
public:
	PerfGroup(PerfBuffer *const pb);
	~PerfGroup();

	bool add(const uint64_t currTime, Buffer *const buffer, const int key, const __u32 type, const __u64 config, const __u64 sample, const __u64 sampleType, const int flags);
	// Safe to call concurrently
	int prepareCPU(const int cpu, Monitor *const monitor);
	// Not safe to call concurrently. Returns the number of events enabled
	int onlineCPU(const uint64_t currTime, const int cpu, const bool start, Buffer *const buffer);
	bool offlineCPU(int cpu);
	bool start();
	void stop();

private:
	// +1 for the group leader
	struct perf_event_attr mAttrs[MAX_PERFORMANCE_COUNTERS + 1];
	bool mPerCpu[MAX_PERFORMANCE_COUNTERS + 1];
	int mKeys[MAX_PERFORMANCE_COUNTERS + 1];
	int mFds[NR_CPUS * (MAX_PERFORMANCE_COUNTERS + 1)];
	PerfBuffer *const mPb;

	// Intentionally undefined
	PerfGroup(const PerfGroup &);
	PerfGroup &operator=(const PerfGroup &);
};

#endif // PERF_GROUP
