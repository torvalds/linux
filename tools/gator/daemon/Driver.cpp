/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Driver.h"

#include "Buffer.h"
#include "SessionData.h"

DriverCounter::DriverCounter(DriverCounter *const next, const char *const name) : mNext(next), mName(name), mKey(getEventKey()), mEnabled(false) {
}

DriverCounter::~DriverCounter() {
	delete mName;
}

Driver *Driver::head = NULL;

Driver::Driver() : next(head) {
	head = this;
}

SimpleDriver::~SimpleDriver() {
	DriverCounter *counters = mCounters;
	while (counters != NULL) {
		DriverCounter *counter = counters;
		counters = counter->getNext();
		delete counter;
	}
}

DriverCounter *SimpleDriver::findCounter(const Counter &counter) const {
	for (DriverCounter *driverCounter = mCounters; driverCounter != NULL; driverCounter = driverCounter->getNext()) {
		if (strcmp(driverCounter->getName(), counter.getType()) == 0) {
			return driverCounter;
		}
	}

	return NULL;
}

bool SimpleDriver::claimCounter(const Counter &counter) const {
	return findCounter(counter) != NULL;
}

bool SimpleDriver::countersEnabled() const {
	for (DriverCounter *counter = mCounters; counter != NULL; counter = counter->getNext()) {
		if (counter->isEnabled()) {
			return true;
		}
	}
	return false;
}

void SimpleDriver::resetCounters() {
	for (DriverCounter *counter = mCounters; counter != NULL; counter = counter->getNext()) {
		counter->setEnabled(false);
	}
}

void SimpleDriver::setupCounter(Counter &counter) {
	DriverCounter *const driverCounter = findCounter(counter);
	if (driverCounter == NULL) {
		counter.setEnabled(false);
		return;
	}
	driverCounter->setEnabled(true);
	counter.setKey(driverCounter->getKey());
}

int SimpleDriver::writeCounters(mxml_node_t *root) const {
	int count = 0;
	for (DriverCounter *counter = mCounters; counter != NULL; counter = counter->getNext()) {
		mxml_node_t *node = mxmlNewElement(root, "counter");
		mxmlElementSetAttr(node, "name", counter->getName());
		++count;
	}

	return count;
}

PolledDriver::~PolledDriver() {
}

void PolledDriver::read(Buffer *const buffer) {
	for (DriverCounter *counter = getCounters(); counter != NULL; counter = counter->getNext()) {
		if (!counter->isEnabled()) {
			continue;
		}
		buffer->event64(counter->getKey(), counter->read());
	}
}
