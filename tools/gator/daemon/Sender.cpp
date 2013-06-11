/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include "Sender.h"
#include "Logging.h"
#include "OlySocket.h"
#include "SessionData.h"

Sender::Sender(OlySocket* socket) {
	mDataFile = NULL;
	mDataSocket = NULL;

	// Set up the socket connection
	if (socket) {
		char streamline[64] = {0};
		mDataSocket = socket;

		// Receive magic sequence - can wait forever
		// Streamline will send data prior to the magic sequence for legacy support, which should be ignored for v4+
		while (strcmp("STREAMLINE", streamline) != 0) {
			if (mDataSocket->receiveString(streamline, sizeof(streamline)) == -1) {
				logg->logError(__FILE__, __LINE__, "Socket disconnected");
				handleException();
			}
		}

		// Send magic sequence - must be done first, after which error messages can be sent
		char magic[32];
		snprintf(magic, 32, "GATOR %i\n", PROTOCOL_VERSION);
		mDataSocket->send(magic, strlen(magic));

		gSessionData->mWaitingOnCommand = true;
		logg->logMessage("Completed magic sequence");
	}

	pthread_mutex_init(&mSendMutex, NULL);
}

Sender::~Sender() {
	delete mDataSocket;
	mDataSocket = NULL;
	if (mDataFile) {
		fclose(mDataFile);
	}
}

void Sender::createDataFile(char* apcDir) {
	if (apcDir == NULL) {
		return;
	}

	mDataFileName = (char*)malloc(strlen(apcDir) + 12);
	sprintf(mDataFileName, "%s/0000000000", apcDir);
	mDataFile = fopen(mDataFileName, "wb");
	if (!mDataFile) {
		logg->logError(__FILE__, __LINE__, "Failed to open binary file: %s", mDataFileName);
		handleException();
	}
}

template<typename T>
inline T min(const T a, const T b) {
	return (a < b ? a : b);
}

void Sender::writeData(const char* data, int length, int type) {
	if (length < 0 || (data == NULL && length > 0)) {
		return;
	}

	// Multiple threads call writeData()
	pthread_mutex_lock(&mSendMutex);

	// Send data over the socket connection
	if (mDataSocket) {
		// Start alarm
		const int alarmDuration = 8;
		alarm(alarmDuration);

		// Send data over the socket, sending the type and size first
		logg->logMessage("Sending data with length %d", length);
		if (type != RESPONSE_APC_DATA) {
			// type and length already added by the Collector for apc data
			mDataSocket->send((char*)&type, 1);
			mDataSocket->send((char*)&length, sizeof(length));
		}

		// 100Kbits/sec * alarmDuration sec / 8 bits/byte
		const int chunkSize = 100*1000 * alarmDuration / 8;
		int pos = 0;
		while (true) {
			mDataSocket->send((char*)data + pos, min(length - pos, chunkSize));
			pos += chunkSize;
			if (pos >= length) {
				break;
			}

			// Reset the alarm
			alarm(alarmDuration);
			logg->logMessage("Resetting the alarm");
		}

		// Stop alarm
		alarm(0);
	}

	// Write data to disk as long as it is not meta data
	if (mDataFile && type == RESPONSE_APC_DATA) {
		logg->logMessage("Writing data with length %d", length);
		// Send data to the data file
		if (fwrite(data, 1, length, mDataFile) != (unsigned int)length) {
			logg->logError(__FILE__, __LINE__, "Failed writing binary file %s", mDataFileName);
			handleException();
		}
	}

	pthread_mutex_unlock(&mSendMutex);
}
