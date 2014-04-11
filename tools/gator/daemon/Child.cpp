/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Child.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/prctl.h>

#include "Logging.h"
#include "CapturedXML.h"
#include "SessionData.h"
#include "LocalCapture.h"
#include "Sender.h"
#include "OlyUtility.h"
#include "OlySocket.h"
#include "StreamlineSetup.h"
#include "ConfigurationXML.h"
#include "Driver.h"
#include "PerfSource.h"
#include "DriverSource.h"
#include "UserSpaceSource.h"
#include "ExternalSource.h"

static sem_t haltPipeline, senderThreadStarted, startProfile, senderSem; // Shared by Child and spawned threads
static Source *primarySource = NULL;
static Source *userSpaceSource = NULL;
static Source *externalSource = NULL;
static Sender* sender = NULL;        // Shared by Child.cpp and spawned threads
Child* child = NULL;                 // shared by Child.cpp and main.cpp

extern void cleanUp();
void handleException() {
	if (child && child->numExceptions++ > 0) {
		// it is possible one of the below functions itself can cause an exception, thus allow only one exception
		logg->logMessage("Received multiple exceptions, terminating the child");
		exit(1);
	}
	fprintf(stderr, "%s", logg->getLastError());

	if (child && child->socket) {
		if (sender) {
			// send the error, regardless of the command sent by Streamline
			sender->writeData(logg->getLastError(), strlen(logg->getLastError()), RESPONSE_ERROR);

			// cannot close the socket before Streamline issues the command, so wait for the command before exiting
			if (gSessionData->mWaitingOnCommand) {
				char discard;
				child->socket->receiveNBytes(&discard, 1);
			}

			// Ensure all data is flushed
			child->socket->shutdownConnection();

			// this indirectly calls close socket which will ensure the data has been sent
			delete sender;
		}
	}

	if (gSessionData->mLocalCapture)
		cleanUp();

	exit(1);
}

// CTRL C Signal Handler for child process
static void child_handler(int signum) {
	static bool beenHere = false;
	if (beenHere == true) {
		logg->logMessage("Gator is being forced to shut down.");
		exit(1);
	}
	beenHere = true;
	logg->logMessage("Gator is shutting down.");
	if (signum == SIGALRM || !primarySource) {
		exit(1);
	} else {
		child->endSession();
		alarm(5); // Safety net in case endSession does not complete within 5 seconds
	}
}

static void *durationThread(void *) {
	prctl(PR_SET_NAME, (unsigned long)&"gatord-duration", 0, 0, 0);
	sem_wait(&startProfile);
	if (gSessionData->mSessionIsActive) {
		// Time out after duration seconds
		// Add a second for host-side filtering
		sleep(gSessionData->mDuration + 1);
		if (gSessionData->mSessionIsActive) {
			logg->logMessage("Duration expired.");
			child->endSession();
		}
	}
	logg->logMessage("Exit duration thread");
	return 0;
}

static void *stopThread(void *) {
	OlySocket* socket = child->socket;

	prctl(PR_SET_NAME, (unsigned long)&"gatord-stopper", 0, 0, 0);
	while (gSessionData->mSessionIsActive) {
		// This thread will stall until the APC_STOP or PING command is received over the socket or the socket is disconnected
		unsigned char header[5];
		const int result = socket->receiveNBytes((char*)&header, sizeof(header));
		const char type = header[0];
		const int length = (header[1] << 0) | (header[2] << 8) | (header[3] << 16) | (header[4] << 24);
		if (result == -1) {
			child->endSession();
		} else if (result > 0) {
			if ((type != COMMAND_APC_STOP) && (type != COMMAND_PING)) {
				logg->logMessage("INVESTIGATE: Received unknown command type %d", type);
			} else {
				// verify a length of zero
				if (length == 0) {
					if (type == COMMAND_APC_STOP) {
						logg->logMessage("Stop command received.");
						child->endSession();
					} else {
						// Ping is used to make sure gator is alive and requires an ACK as the response
						logg->logMessage("Ping command received.");
						sender->writeData(NULL, 0, RESPONSE_ACK);
					}
				} else {
					logg->logMessage("INVESTIGATE: Received stop command but with length = %d", length);
				}
			}
		}
	}

	logg->logMessage("Exit stop thread");
	return 0;
}

static void *senderThread(void *) {
	char end_sequence[] = {RESPONSE_APC_DATA, 0, 0, 0, 0};

	sem_post(&senderThreadStarted);
	prctl(PR_SET_NAME, (unsigned long)&"gatord-sender", 0, 0, 0);
	sem_wait(&haltPipeline);

	while (!primarySource->isDone() || (userSpaceSource != NULL && !userSpaceSource->isDone()) || (externalSource != NULL && !externalSource->isDone())) {
		sem_wait(&senderSem);

		primarySource->write(sender);
		if (userSpaceSource != NULL) {
			userSpaceSource->write(sender);
		}
		if (externalSource != NULL) {
			externalSource->write(sender);
		}
	}

	// write end-of-capture sequence
	if (!gSessionData->mLocalCapture) {
		sender->writeData(end_sequence, sizeof(end_sequence), RESPONSE_APC_DATA);
	}

	logg->logMessage("Exit sender thread");
	return 0;
}

Child::Child() {
	initialization();
	gSessionData->mLocalCapture = true;
}

Child::Child(OlySocket* sock, int conn) {
	initialization();
	socket = sock;
	mNumConnections = conn;
}

Child::~Child() {
}

void Child::initialization() {
	// Set up different handlers for signals
	gSessionData->mSessionIsActive = true;
	signal(SIGINT, child_handler);
	signal(SIGTERM, child_handler);
	signal(SIGABRT, child_handler);
	signal(SIGALRM, child_handler);
	socket = NULL;
	numExceptions = 0;
	mNumConnections = 0;

	// Initialize semaphores
	sem_init(&senderThreadStarted, 0, 0);
	sem_init(&startProfile, 0, 0);
	sem_init(&senderSem, 0, 0);
}

void Child::endSession() {
	gSessionData->mSessionIsActive = false;
	primarySource->interrupt();
	sem_post(&haltPipeline);
}

void Child::run() {
	LocalCapture* localCapture = NULL;
	pthread_t durationThreadID, stopThreadID, senderThreadID;

	prctl(PR_SET_NAME, (unsigned long)&"gatord-child", 0, 0, 0);

	// Disable line wrapping when generating xml files; carriage returns and indentation to be added manually
	mxmlSetWrapMargin(0);

	// Instantiate the Sender - must be done first, after which error messages can be sent
	sender = new Sender(socket);

	if (mNumConnections > 1) {
		logg->logError(__FILE__, __LINE__, "Session already in progress");
		handleException();
	}

	// Populate gSessionData with the configuration
	{ ConfigurationXML configuration; }

	// Set up the driver; must be done after gSessionData->mPerfCounterType[] is populated
	if (!gSessionData->perf.isSetup()) {
	  primarySource = new DriverSource(&senderSem, &startProfile);
	} else {
	  primarySource = new PerfSource(&senderSem, &startProfile);
	}

	// Initialize all drivers
	for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
		driver->resetCounters();
	}

	// Set up counters using the associated driver's setup function
	for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
		Counter & counter = gSessionData->mCounters[i];
		if (counter.isEnabled()) {
			counter.getDriver()->setupCounter(counter);
		}
	}

	// Start up and parse session xml
	if (socket) {
		// Respond to Streamline requests
		StreamlineSetup ss(socket);
	} else {
		char* xmlString;
		xmlString = util->readFromDisk(gSessionData->mSessionXMLPath);
		if (xmlString == 0) {
			logg->logError(__FILE__, __LINE__, "Unable to read session xml file: %s", gSessionData->mSessionXMLPath);
			handleException();
		}
		gSessionData->parseSessionXML(xmlString);
		localCapture = new LocalCapture();
		localCapture->createAPCDirectory(gSessionData->mTargetPath);
		localCapture->copyImages(gSessionData->mImages);
		localCapture->write(xmlString);
		sender->createDataFile(gSessionData->mAPCDir);
		free(xmlString);
	}

	// Must be after session XML is parsed
	if (!primarySource->prepare()) {
		logg->logError(__FILE__, __LINE__, "Unable to prepare for capture");
		handleException();
	}

	// Sender thread shall be halted until it is signaled for one shot mode
	sem_init(&haltPipeline, 0, gSessionData->mOneShot ? 0 : 2);

	// Create the duration, stop, and sender threads
	bool thread_creation_success = true;
	if (gSessionData->mDuration > 0 && pthread_create(&durationThreadID, NULL, durationThread, NULL)) {
		thread_creation_success = false;
	} else if (socket && pthread_create(&stopThreadID, NULL, stopThread, NULL)) {
		thread_creation_success = false;
	} else if (pthread_create(&senderThreadID, NULL, senderThread, NULL)){
		thread_creation_success = false;
	}

	if (gSessionData->hwmon.countersEnabled()) {
		userSpaceSource = new UserSpaceSource(&senderSem);
		if (!userSpaceSource->prepare()) {
			logg->logError(__FILE__, __LINE__, "Unable to prepare for capture");
			handleException();
		}
		userSpaceSource->start();
	}
	if (access("/tmp/gator", F_OK) == 0) {
		externalSource = new ExternalSource(&senderSem);
		if (!externalSource->prepare()) {
			logg->logError(__FILE__, __LINE__, "Unable to prepare for capture");
			handleException();
		}
		externalSource->start();
	}

	if (!thread_creation_success) {
		logg->logError(__FILE__, __LINE__, "Failed to create gator threads");
		handleException();
	}

	// Wait until thread has started
	sem_wait(&senderThreadStarted);

	// Start profiling
	primarySource->run();

	if (externalSource != NULL) {
		externalSource->join();
	}
	if (userSpaceSource != NULL) {
		userSpaceSource->join();
	}

	// Wait for the other threads to exit
	pthread_join(senderThreadID, NULL);

	// Shutting down the connection should break the stop thread which is stalling on the socket recv() function
	if (socket) {
		logg->logMessage("Waiting on stop thread");
		socket->shutdownConnection();
		pthread_join(stopThreadID, NULL);
	}

	// Write the captured xml file
	if (gSessionData->mLocalCapture) {
		CapturedXML capturedXML;
		capturedXML.write(gSessionData->mAPCDir);
	}

	logg->logMessage("Profiling ended.");

	delete externalSource;
	delete userSpaceSource;
	delete primarySource;
	delete sender;
	delete localCapture;
}
