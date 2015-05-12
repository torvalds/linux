/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "SessionData.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "DiskIODriver.h"
#include "FSDriver.h"
#include "HwmonDriver.h"
#include "Logging.h"
#include "MemInfoDriver.h"
#include "NetDriver.h"
#include "SessionXML.h"

#define CORE_NAME_UNKNOWN "unknown"

SessionData* gSessionData = NULL;

SessionData::SessionData() {
	usDrivers[0] = new HwmonDriver();
	usDrivers[1] = new FSDriver();
	usDrivers[2] = new MemInfoDriver();
	usDrivers[3] = new NetDriver();
	usDrivers[4] = new DiskIODriver();
	initialize();
}

SessionData::~SessionData() {
}

void SessionData::initialize() {
	mWaitingOnCommand = false;
	mSessionIsActive = false;
	mLocalCapture = false;
	mOneShot = false;
	mSentSummary = false;
	mAllowCommands = false;
	const size_t cpuIdSize = sizeof(int)*NR_CPUS;
	// Share mCpuIds across all instances of gatord
	mCpuIds = (int *)mmap(NULL, cpuIdSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mCpuIds == MAP_FAILED) {
		logg->logError("Unable to mmap shared memory for cpuids");
		handleException();
	}
	memset(mCpuIds, -1, cpuIdSize);
	strcpy(mCoreName, CORE_NAME_UNKNOWN);
	readModel();
	readCpuInfo();
	mImages = NULL;
	mConfigurationXMLPath = NULL;
	mSessionXMLPath = NULL;
	mEventsXMLPath = NULL;
	mEventsXMLAppend = NULL;
	mTargetPath = NULL;
	mAPCDir = NULL;
	mCaptureWorkingDir = NULL;
	mCaptureCommand = NULL;
	mCaptureUser = NULL;
	mSampleRate = 0;
	mLiveRate = 0;
	mDuration = 0;
	mMonotonicStarted = -1;
	mBacktraceDepth = 0;
	mTotalBufferSize = 0;
	// sysconf(_SC_NPROCESSORS_CONF) is unreliable on 2.6 Android, get the value from the kernel module
	mCores = 1;
	mPageSize = 0;
	mAnnotateStart = -1;
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
		logg->logError("Invalid sample rate (%s) in session xml.", session.parameters.sample_rate);
		handleException();
	}
	mBacktraceDepth = session.parameters.call_stack_unwinding == true ? 128 : 0;

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
		logg->logError("Invalid value for buffer mode in session xml.");
		handleException();
	}

	// Convert milli- to nanoseconds
	mLiveRate = session.parameters.live_rate * (int64_t)1000000;
	if (mLiveRate > 0 && mLocalCapture) {
		logg->logMessage("Local capture is not compatable with live, disabling live");
		mLiveRate = 0;
	}

	if (!mAllowCommands && (mCaptureCommand != NULL)) {
		logg->logError("Running a command during a capture is not currently allowed. Please restart gatord with the -a flag.");
		handleException();
	}
}

void SessionData::readModel() {
	FILE *fh = fopen("/proc/device-tree/model", "rb");
	if (fh == NULL) {
		return;
	}

	char buf[256];
	if (fgets(buf, sizeof(buf), fh) != NULL) {
		strcpy(mCoreName, buf);
	}

	fclose(fh);
}

static void setImplementer(int &cpuId, const int implementer) {
	if (cpuId == -1) {
		cpuId = 0;
	}
	cpuId |= implementer << 12;
}

static void setPart(int &cpuId, const int part) {
	if (cpuId == -1) {
		cpuId = 0;
	}
	cpuId |= part;
}

void SessionData::readCpuInfo() {
	char temp[256]; // arbitrarily large amount
	mMaxCpuId = -1;

	FILE *f = fopen("/proc/cpuinfo", "r");
	if (f == NULL) {
		logg->logMessage("Error opening /proc/cpuinfo\n"
			"The core name in the captured xml file will be 'unknown'.");
		return;
	}

	bool foundCoreName = (strcmp(mCoreName, CORE_NAME_UNKNOWN) != 0);
	int processor = -1;
	while (fgets(temp, sizeof(temp), f)) {
		const size_t len = strlen(temp);

		if (len == 1) {
			// New section, clear the processor. Streamline will not know the cpus if the pre Linux 3.8 format of cpuinfo is encountered but also that no incorrect information will be transmitted.
			processor = -1;
			continue;
		}

		if (len > 0) {
			// Replace the line feed with a null
			temp[len - 1] = '\0';
		}

		const bool foundHardware = !foundCoreName && strstr(temp, "Hardware") != 0;
		const bool foundCPUImplementer = strstr(temp, "CPU implementer") != 0;
		const bool foundCPUPart = strstr(temp, "CPU part") != 0;
		const bool foundProcessor = strstr(temp, "processor") != 0;
		if (foundHardware || foundCPUImplementer || foundCPUPart || foundProcessor) {
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

			if (foundCPUImplementer) {
				const int implementer = strtol(position, NULL, 0);
				if (processor >= NR_CPUS) {
					logg->logMessage("Too many processors, please increase NR_CPUS");
				} else if (processor >= 0) {
					setImplementer(mCpuIds[processor], implementer);
				} else {
					setImplementer(mMaxCpuId, implementer);
				}
			}

			if (foundCPUPart) {
				const int cpuId = strtol(position, NULL, 0);
				if (processor >= NR_CPUS) {
					logg->logMessage("Too many processors, please increase NR_CPUS");
				} else if (processor >= 0) {
					setPart(mCpuIds[processor], cpuId);
				} else {
					setPart(mMaxCpuId, cpuId);
				}
			}

			if (foundProcessor) {
				processor = strtol(position, NULL, 0);
			}
		}
	}

	// If this does not have the full topology in /proc/cpuinfo, mCpuIds[0] may not have the 1 CPU part emitted - this guarantees it's in mMaxCpuId
	for (int i = 0; i < NR_CPUS; ++i) {
		if (mCpuIds[i] > mMaxCpuId) {
			mMaxCpuId = mCpuIds[i];
		}
	}

	if (!foundCoreName) {
		logg->logMessage("Could not determine core name from /proc/cpuinfo\n"
				 "The core name in the captured xml file will be 'unknown'.");
	}
	fclose(f);
}

uint64_t getTime() {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		logg->logError("Failed to get uptime");
		handleException();
	}
	return (NS_PER_S*ts.tv_sec + ts.tv_nsec);
}

int getEventKey() {
	// key 0 is reserved as a timestamp
	// key 1 is reserved as the marker for thread specific counters
	// key 2 is reserved as the marker for core
	// Odd keys are assigned by the driver, even keys by the daemon
	static int key = 4;

	const int ret = key;
	key += 2;
	return ret;
}

int pipe_cloexec(int pipefd[2]) {
	if (pipe(pipefd) != 0) {
		return -1;
	}

	int fdf;
	if (((fdf = fcntl(pipefd[0], F_GETFD)) == -1) || (fcntl(pipefd[0], F_SETFD, fdf | FD_CLOEXEC) != 0) ||
			((fdf = fcntl(pipefd[1], F_GETFD)) == -1) || (fcntl(pipefd[1], F_SETFD, fdf | FD_CLOEXEC) != 0)) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	return 0;
}

FILE *fopen_cloexec(const char *path, const char *mode) {
	FILE *fh = fopen(path, mode);
	if (fh == NULL) {
		return NULL;
	}
	int fd = fileno(fh);
	int fdf = fcntl(fd, F_GETFD);
	if ((fdf == -1) || (fcntl(fd, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
		fclose(fh);
		return NULL;
	}
	return fh;
}
