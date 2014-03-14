/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define __STDC_FORMAT_MACROS

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <inttypes.h>
#include "Collector.h"
#include "SessionData.h"
#include "Logging.h"
#include "Sender.h"

// Driver initialization independent of session settings
Collector::Collector() {
	mBufferFD = 0;

	checkVersion();

	int enable = -1;
	if (readIntDriver("/dev/gator/enable", &enable) != 0 || enable != 0) {
		logg->logError(__FILE__, __LINE__, "Driver already enabled, possibly a session is already in progress.");
		handleException();
	}

	readIntDriver("/dev/gator/cpu_cores", &gSessionData->mCores);
	if (gSessionData->mCores == 0) {
		gSessionData->mCores = 1;
	}

	mBufferSize = 0;
	if (readIntDriver("/dev/gator/buffer_size", &mBufferSize) || mBufferSize <= 0) {
		logg->logError(__FILE__, __LINE__, "Unable to read the driver buffer size");
		handleException();
	}
}

Collector::~Collector() {
	// Write zero for safety, as a zero should have already been written
	writeDriver("/dev/gator/enable", "0");

	// Calls event_buffer_release in the driver
	if (mBufferFD) {
		close(mBufferFD);
	}
}

void Collector::checkVersion() {
	int driver_version = 0;

	if (readIntDriver("/dev/gator/version", &driver_version) == -1) {
		logg->logError(__FILE__, __LINE__, "Error reading gator driver version");
		handleException();
	}

	// Verify the driver version matches the daemon version
	if (driver_version != PROTOCOL_VERSION) {
		if ((driver_version > PROTOCOL_DEV) || (PROTOCOL_VERSION > PROTOCOL_DEV)) {
			// One of the mismatched versions is development version
			logg->logError(__FILE__, __LINE__,
				"DEVELOPMENT BUILD MISMATCH: gator driver version \"%d\" is not in sync with gator daemon version \"%d\".\n"
				">> The following must be synchronized from engineering repository:\n"
				">> * gator driver\n"
				">> * gator daemon\n"
				">> * Streamline", driver_version, PROTOCOL_VERSION);
			handleException();
		} else {
			// Release version mismatch
			logg->logError(__FILE__, __LINE__, 
				"gator driver version \"%d\" is different than gator daemon version \"%d\".\n"
				">> Please upgrade the driver and daemon to the latest versions.", driver_version, PROTOCOL_VERSION);
			handleException();
		}
	}
}

void Collector::start() {
	// Set the maximum backtrace depth
	if (writeReadDriver("/dev/gator/backtrace_depth", &gSessionData->mBacktraceDepth)) {
		logg->logError(__FILE__, __LINE__, "Unable to set the driver backtrace depth");
		handleException();
	}

	// open the buffer which calls userspace_buffer_open() in the driver
	mBufferFD = open("/dev/gator/buffer", O_RDONLY);
	if (mBufferFD < 0) {
		logg->logError(__FILE__, __LINE__, "The gator driver did not set up properly. Please view the linux console or dmesg log for more information on the failure.");
		handleException();
	}

	// set the tick rate of the profiling timer
	if (writeReadDriver("/dev/gator/tick", &gSessionData->mSampleRate) != 0) {
		logg->logError(__FILE__, __LINE__, "Unable to set the driver tick");
		handleException();
	}

	// notify the kernel of the response type
	int response_type = gSessionData->mLocalCapture ? 0 : RESPONSE_APC_DATA;
	if (writeDriver("/dev/gator/response_type", response_type)) {
		logg->logError(__FILE__, __LINE__, "Unable to write the response type");
		handleException();
	}

	// Set the live rate
	if (writeReadDriver("/dev/gator/live_rate", &gSessionData->mLiveRate)) {
		logg->logError(__FILE__, __LINE__, "Unable to set the driver live rate");
		handleException();
	}

	logg->logMessage("Start the driver");

	// This command makes the driver start profiling by calling gator_op_start() in the driver
	if (writeDriver("/dev/gator/enable", "1") != 0) {
		logg->logError(__FILE__, __LINE__, "The gator driver did not start properly. Please view the linux console or dmesg log for more information on the failure.");
		handleException();
	}

	lseek(mBufferFD, 0, SEEK_SET);
}

// These commands should cause the read() function in collect() to return
void Collector::stop() {
	// This will stop the driver from profiling
	if (writeDriver("/dev/gator/enable", "0") != 0) {
		logg->logMessage("Stopping kernel failed");
	}
}

int Collector::collect(char* buffer) {
	// Calls event_buffer_read in the driver
	int bytesRead;

	errno = 0;
	bytesRead = read(mBufferFD, buffer, mBufferSize);

	// If read() returned due to an interrupt signal, re-read to obtain the last bit of collected data
	if (bytesRead == -1 && errno == EINTR) {
		bytesRead = read(mBufferFD, buffer, mBufferSize);
	}

	// return the total bytes written
	logg->logMessage("Driver read of %d bytes", bytesRead);
	return bytesRead;
}

int Collector::readIntDriver(const char* fullpath, int* value) {
	FILE* file = fopen(fullpath, "r");
	if (file == NULL) {
		return -1;
	}
	if (fscanf(file, "%u", value) != 1) {
		fclose(file);
		logg->logMessage("Invalid value in file %s", fullpath);
		return -1;
	}
	fclose(file);
	return 0;
}

int Collector::readInt64Driver(const char* fullpath, int64_t* value) {
	FILE* file = fopen(fullpath, "r");
	if (file == NULL) {
		return -1;
	}
	if (fscanf(file, "%" SCNi64, value) != 1) {
		fclose(file);
		logg->logMessage("Invalid value in file %s", fullpath);
		return -1;
	}
	fclose(file);
	return 0;
}

int Collector::writeDriver(const char* path, int value) {
	char data[40]; // Sufficiently large to hold any integer
	snprintf(data, sizeof(data), "%d", value);
	return writeDriver(path, data);
}

int Collector::writeDriver(const char* path, int64_t value) {
	char data[40]; // Sufficiently large to hold any integer
	snprintf(data, sizeof(data), "%" PRIi64, value);
	return writeDriver(path, data);
}

int Collector::writeDriver(const char* fullpath, const char* data) {
	int fd = open(fullpath, O_WRONLY);
	if (fd < 0) {
		return -1;
	}
	if (write(fd, data, strlen(data)) < 0) {
		close(fd);
		logg->logMessage("Opened but could not write to %s", fullpath);
		return -1;
	}
	close(fd);
	return 0;
}

int Collector::writeReadDriver(const char* path, int* value) {
	if (writeDriver(path, *value) || readIntDriver(path, value)) {
		return -1;
	}
	return 0;
}

int Collector::writeReadDriver(const char* path, int64_t* value) {
	if (writeDriver(path, *value) || readInt64Driver(path, value)) {
		return -1;
	}
	return 0;
}
