/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Source.h"

#include "Logging.h"

Source::Source() : mThreadID() {
}

Source::~Source() {
}

void Source::start() {
	if (pthread_create(&mThreadID, NULL, runStatic, this)) {
		logg->logError("Failed to create source thread");
		handleException();
	}
}

void Source::join() {
	pthread_join(mThreadID, NULL);
}

void *Source::runStatic(void *arg) {
	static_cast<Source *>(arg)->run();
	return NULL;
}
