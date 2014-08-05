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
#include "FSDriver.h"
#include "Hwmon.h"
#include "MaliVideoDriver.h"
#include "PerfDriver.h"

#define PROTOCOL_VERSION	19
#define PROTOCOL_DEV		1000	// Differentiates development versions (timestamp) from release versions

#define NS_PER_S ((uint64_t)1000000000)

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
	void readCpuInfo();

	Hwmon hwmon;
	FSDriver fsDriver;
	PerfDriver perf;
	MaliVideoDriver maliVideo;

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
	bool mSentSummary;

	int mBacktraceDepth;
	int mTotalBufferSize;	// number of MB to use for the entire collection buffer
	int mSampleRate;
	int64_t mLiveRate;
	int mDuration;
	int mCores;
	int mPageSize;
	int *mCpuIds;
	int mMaxCpuId;

	// PMU Counters
	int mCounterOverflow;
	Counter mCounters[MAX_PERFORMANCE_COUNTERS];

private:
	// Intentionally unimplemented
	SessionData(const SessionData &);
	SessionData &operator=(const SessionData &);
};

extern SessionData* gSessionData;

uint64_t getTime();
int getEventKey();

#endif // SESSION_DATA_H
