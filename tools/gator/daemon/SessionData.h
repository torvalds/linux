/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SESSION_DATA_H
#define SESSION_DATA_H

#include <stdint.h>

#include "Config.h"
#include "Counter.h"
#include "Hwmon.h"
#include "PerfDriver.h"

#define PROTOCOL_VERSION	18
#define PROTOCOL_DEV		1000	// Differentiates development versions (timestamp) from release versions

struct ImageLinkList {
	char* path;
	struct ImageLinkList *next;
};

class SessionData {
public:
	static const size_t MAX_STRING_LEN = 80;

	SessionData();
	~SessionData();
	void initialize();
	void parseSessionXML(char* xmlString);

	Hwmon hwmon;
	PerfDriver perf;

	char mCoreName[MAX_STRING_LEN];
	struct ImageLinkList *mImages;
	char* mConfigurationXMLPath;
	char* mSessionXMLPath;
	char* mEventsXMLPath;
	char* mTargetPath;
	char* mAPCDir;

	bool mWaitingOnCommand;
	bool mSessionIsActive;
	bool mLocalCapture;
	bool mOneShot;		// halt processing of the driver data until profiling is complete or the buffer is filled
	bool mIsEBS;
	
	int mBacktraceDepth;
	int mTotalBufferSize;	// number of MB to use for the entire collection buffer
	int mSampleRate;
	int64_t mLiveRate;
	int mDuration;
	int mCores;
	int mPageSize;
	int mCpuIds[NR_CPUS];
	int mMaxCpuId;

	// PMU Counters
	int mCounterOverflow;
	Counter mCounters[MAX_PERFORMANCE_COUNTERS];

private:
	void readCpuInfo();

	// Intentionally unimplemented
	SessionData(const SessionData &);
	SessionData &operator=(const SessionData &);
};

extern SessionData* gSessionData;

int getEventKey();

#endif // SESSION_DATA_H
