/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "FSDriver.h"

#include <fcntl.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Buffer.h"
#include "Counter.h"
#include "DriverSource.h"
#include "Logging.h"
#include "SessionData.h"

class FSCounter {
public:
	FSCounter(FSCounter *next, char *name, const char *regex);
	~FSCounter();

	FSCounter *getNext() const { return next; }
	int getKey() const { return key; }
	bool isEnabled() const { return enabled; }
	void setEnabled(const bool enabled) { this->enabled = enabled; }
	const char *getName() const { return name; }
	int64_t read();

private:
	FSCounter *const next;
	regex_t reg;
	char *name;
	const int key;
	int enabled : 1,
		useRegex : 1;

	// Intentionally unimplemented
	FSCounter(const FSCounter &);
	FSCounter &operator=(const FSCounter &);
};

FSCounter::FSCounter(FSCounter *next, char *name, const char *regex) : next(next), name(name), key(getEventKey()), enabled(false), useRegex(regex != NULL) {
	if (useRegex) {
		int result = regcomp(&reg, regex, REG_EXTENDED);
		if (result != 0) {
			char buf[128];
			regerror(result, &reg, buf, sizeof(buf));
			logg->logError(__FILE__, __LINE__, "Invalid regex '%s': %s", regex, buf);
			handleException();
		}
	}
}

FSCounter::~FSCounter() {
	free(name);
	if (useRegex) {
		regfree(&reg);
	}
}

int64_t FSCounter::read() {
	int64_t value;
	if (useRegex) {
		char buf[4096];
		size_t pos = 0;
		const int fd = open(name, O_RDONLY);
		if (fd < 0) {
			goto fail;
		}
		while (pos < sizeof(buf) - 1) {
			const ssize_t bytes = ::read(fd, buf + pos, sizeof(buf) - pos - 1);
			if (bytes < 0) {
				goto fail;
			} else if (bytes == 0) {
				break;
			}
			pos += bytes;
		}
		close(fd);
		buf[pos] = '\0';

		regmatch_t match[2];
		int result = regexec(&reg, buf, 2, match, 0);
		if (result != 0) {
			regerror(result, &reg, buf, sizeof(buf));
			logg->logError(__FILE__, __LINE__, "Parsing %s failed: %s", name, buf);
			handleException();
		}

		if (match[1].rm_so < 0) {
			logg->logError(__FILE__, __LINE__, "Parsing %s failed", name);
			handleException();
		}
		char *endptr;
		errno = 0;
		value = strtoll(buf + match[1].rm_so, &endptr, 0);
		if (errno != 0) {
			logg->logError(__FILE__, __LINE__, "Parsing %s failed: %s", name, strerror(errno));
			handleException();
		}
	} else {
		if (DriverSource::readInt64Driver(name, &value) != 0) {
			goto fail;
		}
	}
	return value;

 fail:
	logg->logError(__FILE__, __LINE__, "Unable to read %s", name);
	handleException();
}

FSDriver::FSDriver() : counters(NULL) {
}

FSDriver::~FSDriver() {
	while (counters != NULL) {
		FSCounter * counter = counters;
		counters = counter->getNext();
		delete counter;
	}
}

void FSDriver::setup(mxml_node_t *const xml) {
	// fs driver does not currently work with perf
	if (gSessionData->perf.isSetup()) {
		return;
	}

	mxml_node_t *node = xml;
	while (true) {
		node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
		if (node == NULL) {
			break;
		}
		const char *counter = mxmlElementGetAttr(node, "counter");
		if ((counter != NULL) && (counter[0] == '/')) {
			const char *regex = mxmlElementGetAttr(node, "regex");
			counters = new FSCounter(counters, strdup(counter), regex);
		}
	}
}

FSCounter *FSDriver::findCounter(const Counter &counter) const {
	for (FSCounter * fsCounter = counters; fsCounter != NULL; fsCounter = fsCounter->getNext()) {
		if (strcmp(fsCounter->getName(), counter.getType()) == 0) {
			return fsCounter;
		}
	}

	return NULL;
}

bool FSDriver::claimCounter(const Counter &counter) const {
	return findCounter(counter) != NULL;
}

bool FSDriver::countersEnabled() const {
	for (FSCounter *counter = counters; counter != NULL; counter = counter->getNext()) {
		if (counter->isEnabled()) {
			return true;
		}
	}
	return false;
}

void FSDriver::resetCounters() {
	for (FSCounter * counter = counters; counter != NULL; counter = counter->getNext()) {
		counter->setEnabled(false);
	}
}

void FSDriver::setupCounter(Counter &counter) {
	FSCounter *const fsCounter = findCounter(counter);
	if (fsCounter == NULL) {
		counter.setEnabled(false);
		return;
	}
	fsCounter->setEnabled(true);
	counter.setKey(fsCounter->getKey());
}

int FSDriver::writeCounters(mxml_node_t *root) const {
	int count = 0;
	for (FSCounter * counter = counters; counter != NULL; counter = counter->getNext()) {
		if (access(counter->getName(), R_OK) == 0) {
			mxml_node_t *node = mxmlNewElement(root, "counter");
			mxmlElementSetAttr(node, "name", counter->getName());
			++count;
		}
	}

	return count;
}

void FSDriver::start() {
}

void FSDriver::read(Buffer * const buffer) {
	for (FSCounter * counter = counters; counter != NULL; counter = counter->getNext()) {
		if (!counter->isEnabled()) {
			continue;
		}
		buffer->event(counter->getKey(), counter->read());
	}
}
