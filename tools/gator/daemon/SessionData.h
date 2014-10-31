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

#include "AnnotateListener.h"
#include "Config.h"
#include "Counter.h"
#include "FtraceDriver.h"
#include "KMod.h"
#include "MaliVideoDriver.h"
#include "PerfDriver.h"

#define PROTOCOL_VERSION 20
// Differentiates development versions (timestamp) from release versions
#define PROTOCOL_DEV 1000

#define NS_PER_S 1000000000LL
#define NS_PER_MS 1000000LL
#define NS_PER_US 1000LL

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
	void readModel();
	void readCpuInfo();

	PolledDriver *usDrivers[6];
	KMod kmod;
	PerfDriver perf;
	MaliVideoDriver maliVideo;
	FtraceDriver ftraceDriver;
	AnnotateListener annotateListener;

	char mCoreName[MAX_STRING_LEN];
	struct ImageLinkList *mImages;
	char *mConfigurationXMLPath;
	char *mSessionXMLPath;
	char *mEventsXMLPath;
	char *mTargetPath;
	char *mAPCDir;
	char *mCaptureWorkingDir;
	char *mCaptureCommand;
	char *mCaptureUser;

	bool mWaitingOnCommand;
	bool mSessionIsActive;
	bool mLocalCapture;
	// halt processing of the driver data until profiling is complete or the buffer is filled
	bool mOneShot;
	bool mIsEBS;
	bool mSentSummary;
	bool mAllowCommands;

	int64_t mMonotonicStarted;
	int mBacktraceDepth;
	// number of MB to use for the entire collection buffer
	int mTotalBufferSize;
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
int pipe_cloexec(int pipefd[2]);
FILE *fopen_cloexec(const char *path, const char *mode);

#endif // SESSION_DATA_H
