/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "SessionData.h"

#include <string.h>

#include "SessionXML.h"
#include "Logging.h"

SessionData* gSessionData = NULL;

SessionData::SessionData() {
	initialize();
}

SessionData::~SessionData() {
}

void SessionData::initialize() {
	mWaitingOnCommand = false;
	mSessionIsActive = false;
	mLocalCapture = false;
	mOneShot = false;
	readCpuInfo();
	mConfigurationXMLPath = NULL;
	mSessionXMLPath = NULL;
	mEventsXMLPath = NULL;
	mTargetPath = NULL;
	mAPCDir = NULL;
	mSampleRate = 0;
	mLiveRate = 0;
	mDuration = 0;
	mBacktraceDepth = 0;
	mTotalBufferSize = 0;
	// sysconf(_SC_NPROCESSORS_CONF) is unreliable on 2.6 Android, get the value from the kernel module
	mCores = 1;
	mPageSize = 0;
}

void SessionData::parseSessionXML(char* xmlString) {
	SessionXML session(xmlString);
	session.parse();

	// Set session data values - use prime numbers just below the desired value to reduce the chance of events firing at the same time
	if (strcmp(session.parameters.sample_rate, "high") == 0) {
		mSampleRate = 9973; // 10000
	} else if (strcmp(session.parameters.sample_rate, "normal") == 0) {
		mSampleRate = 997; // 1000
	} else if (strcmp(session.parameters.sample_rate, "low") == 0) {
		mSampleRate = 97; // 100
	} else if (strcmp(session.parameters.sample_rate, "none") == 0) {
		mSampleRate = 0;
	} else {
		logg->logError(__FILE__, __LINE__, "Invalid sample rate (%s) in session xml.", session.parameters.sample_rate);
		handleException();
	}
	mBacktraceDepth = session.parameters.call_stack_unwinding == true ? 128 : 0;
	mDuration = session.parameters.duration;

	// Determine buffer size (in MB) based on buffer mode
	mOneShot = true;
	if (strcmp(session.parameters.buffer_mode, "streaming") == 0) {
		mOneShot = false;
		mTotalBufferSize = 1;
	} else if (strcmp(session.parameters.buffer_mode, "small") == 0) {
		mTotalBufferSize = 1;
	} else if (strcmp(session.parameters.buffer_mode, "normal") == 0) {
		mTotalBufferSize = 4;
	} else if (strcmp(session.parameters.buffer_mode, "large") == 0) {
		mTotalBufferSize = 16;
	} else {
		logg->logError(__FILE__, __LINE__, "Invalid value for buffer mode in session xml.");
		handleException();
	}

	mImages = session.parameters.images;
	// Convert milli- to nanoseconds
	mLiveRate = session.parameters.live_rate * (int64_t)1000000;
	if (mLiveRate > 0 && mLocalCapture) {
		logg->logMessage("Local capture is not compatable with live, disabling live");
		mLiveRate = 0;
	}
}

void SessionData::readCpuInfo() {
	char temp[256]; // arbitrarily large amount
	strcpy(mCoreName, "unknown");
	memset(&mCpuIds, -1, sizeof(mCpuIds));
	mMaxCpuId = -1;

	FILE* f = fopen("/proc/cpuinfo", "r");	
	if (f == NULL) {
		logg->logMessage("Error opening /proc/cpuinfo\n"
			"The core name in the captured xml file will be 'unknown'.");
		return;
	}

	bool foundCoreName = false;
	int processor = 0;
	while (fgets(temp, sizeof(temp), f)) {
		if (strlen(temp) > 0) {
			temp[strlen(temp) - 1] = 0;	// Replace the line feed with a null
		}

		const bool foundHardware = strstr(temp, "Hardware") != 0;
		const bool foundCPUPart = strstr(temp, "CPU part") != 0;
		const bool foundProcessor = strstr(temp, "processor") != 0;
		if (foundHardware || foundCPUPart || foundProcessor) {
			char* position = strchr(temp, ':');
			if (position == NULL || (unsigned int)(position - temp) + 2 >= strlen(temp)) {
				logg->logMessage("Unknown format of /proc/cpuinfo\n"
					"The core name in the captured xml file will be 'unknown'.");
				return;
			}
			position += 2;

			if (foundHardware) {
				strncpy(mCoreName, position, sizeof(mCoreName));
				mCoreName[sizeof(mCoreName) - 1] = 0; // strncpy does not guarantee a null-terminated string
				foundCoreName = true;
			}

			if (foundCPUPart) {
				mCpuIds[processor] = strtol(position, NULL, 0);
				// If this does not have the full topology in /proc/cpuinfo, mCpuIds[0] may not have the 1 CPU part emitted - this guarantees it's in mMaxCpuId
				if (mCpuIds[processor] > mMaxCpuId) {
					mMaxCpuId = mCpuIds[processor];
				}
			}

			if (foundProcessor) {
				processor = strtol(position, NULL, 0);
			}
		}
	}

	if (!foundCoreName) {
		logg->logMessage("Could not determine core name from /proc/cpuinfo\n"
						 "The core name in the captured xml file will be 'unknown'.");
	}
	fclose(f);
 }

int getEventKey() {
	// key 0 is reserved as a timestamp
	// key 1 is reserved as the marker for thread specific counters
	// Odd keys are assigned by the driver, even keys by the daemon
	static int key = 2;

	const int ret = key;
	key += 2;
	return ret;
}
