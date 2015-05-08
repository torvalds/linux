/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "FtraceDriver.h"

#include <regex.h>
#include <unistd.h>

#include "DriverSource.h"
#include "Logging.h"
#include "Setup.h"

class FtraceCounter : public DriverCounter {
public:
	FtraceCounter(DriverCounter *next, char *name, const char *regex, const char *enable);
	~FtraceCounter();

	void prepare();
	int read(const char *const line, int64_t *values);
	void stop();

private:
	regex_t mReg;
	char *const mEnable;
	int mWasEnabled;

	// Intentionally unimplemented
	FtraceCounter(const FtraceCounter &);
	FtraceCounter &operator=(const FtraceCounter &);
};

FtraceCounter::FtraceCounter(DriverCounter *next, char *name, const char *regex, const char *enable) : DriverCounter(next, name), mEnable(enable == NULL ? NULL : strdup(enable)) {
	int result = regcomp(&mReg, regex, REG_EXTENDED);
	if (result != 0) {
		char buf[128];
		regerror(result, &mReg, buf, sizeof(buf));
		logg->logError("Invalid regex '%s': %s", regex, buf);
		handleException();
	}
}

FtraceCounter::~FtraceCounter() {
	regfree(&mReg);
	if (mEnable != NULL) {
		free(mEnable);
	}
}

void FtraceCounter::prepare() {
	if (mEnable == NULL) {
		return;
	}

	char buf[1<<10];
	snprintf(buf, sizeof(buf), "/sys/kernel/debug/tracing/events/%s/enable", mEnable);
	if ((DriverSource::readIntDriver(buf, &mWasEnabled) != 0) ||
			(DriverSource::writeDriver(buf, 1) != 0)) {
		logg->logError("Unable to read or write to %s", buf);
		handleException();
	}
}

int FtraceCounter::read(const char *const line, int64_t *values) {
	regmatch_t match[2];
	int result = regexec(&mReg, line, 2, match, 0);
	if (result != 0) {
		// No match
		return 0;
	}

	int64_t value;
	if (match[1].rm_so < 0) {
		value = 1;
	} else {
		errno = 0;
		value = strtoll(line + match[1].rm_so, NULL, 0);
		if (errno != 0) {
			logg->logError("Parsing %s failed: %s", getName(), strerror(errno));
			handleException();
		}
	}

	values[0] = getKey();
	values[1] = value;

	return 1;
}

void FtraceCounter::stop() {
	if (mEnable == NULL) {
		return;
	}

	char buf[1<<10];
	snprintf(buf, sizeof(buf), "/sys/kernel/debug/tracing/events/%s/enable", mEnable);
	DriverSource::writeDriver(buf, mWasEnabled);
}

FtraceDriver::FtraceDriver() : mValues(NULL) {
}

FtraceDriver::~FtraceDriver() {
	delete [] mValues;
}

void FtraceDriver::readEvents(mxml_node_t *const xml) {
	// Check the kernel version
	int release[3];
	if (!getLinuxVersion(release)) {
		logg->logError("getLinuxVersion failed");
		handleException();
	}

	// The perf clock was added in 3.10
	if (KERNEL_VERSION(release[0], release[1], release[2]) < KERNEL_VERSION(3, 10, 0)) {
		logg->logMessage("Unsupported kernel version, to use ftrace please upgrade to Linux 3.10 or later");
		return;
	}

	mxml_node_t *node = xml;
	int count = 0;
	while (true) {
		node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
		if (node == NULL) {
			break;
		}
		const char *counter = mxmlElementGetAttr(node, "counter");
		if (counter == NULL) {
			continue;
		}

		if (strncmp(counter, "ftrace_", 7) != 0) {
			continue;
		}

		const char *regex = mxmlElementGetAttr(node, "regex");
		if (regex == NULL) {
			logg->logError("The regex counter %s is missing the required regex attribute", counter);
			handleException();
		}
		bool addCounter = true;
		const char *enable = mxmlElementGetAttr(node, "enable");
		if (enable != NULL) {
			char buf[1<<10];
			snprintf(buf, sizeof(buf), "/sys/kernel/debug/tracing/events/%s/enable", enable);
			if (access(buf, W_OK) != 0) {
				logg->logMessage("Disabling counter %s, %s not found", counter, buf);
				addCounter = false;
			}
		}
		if (addCounter) {
			setCounters(new FtraceCounter(getCounters(), strdup(counter), regex, enable));
			++count;
		}
	}

	mValues = new int64_t[2*count];
}

void FtraceDriver::prepare() {
	for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<FtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->prepare();
	}
}

int FtraceDriver::read(const char *line, int64_t **buf) {
	int count = 0;

	for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<FtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		count += counter->read(line, mValues + 2*count);
	}

	*buf = mValues;
	return count;
}

void FtraceDriver::stop() {
	for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<FtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->stop();
	}
}
