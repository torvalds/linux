/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "FtraceDriver.h"

#include <regex.h>

#include "Logging.h"

class FtraceCounter : public DriverCounter {
public:
	FtraceCounter(DriverCounter *next, char *name, const char *regex);
	~FtraceCounter();

	int read(const char *const line, int64_t *values);

private:
	regex_t reg;

	// Intentionally unimplemented
	FtraceCounter(const FtraceCounter &);
	FtraceCounter &operator=(const FtraceCounter &);
};

FtraceCounter::FtraceCounter(DriverCounter *next, char *name, const char *regex) : DriverCounter(next, name) {
	int result = regcomp(&reg, regex, REG_EXTENDED);
	if (result != 0) {
		char buf[128];
		regerror(result, &reg, buf, sizeof(buf));
		logg->logError(__FILE__, __LINE__, "Invalid regex '%s': %s", regex, buf);
		handleException();
	}
}

FtraceCounter::~FtraceCounter() {
	regfree(&reg);
}

int FtraceCounter::read(const char *const line, int64_t *values) {
	regmatch_t match[2];
	int result = regexec(&reg, line, 2, match, 0);
	if (result != 0) {
		// No match
		return 0;
	}

	if (match[1].rm_so < 0) {
		logg->logError(__FILE__, __LINE__, "Parsing %s failed", getName());
		handleException();
	}

	errno = 0;
	int64_t value = strtoll(line + match[1].rm_so, NULL, 0);
	if (errno != 0) {
		logg->logError(__FILE__, __LINE__, "Parsing %s failed: %s", getName(), strerror(errno));
		handleException();
	}

	values[0] = getKey();
	values[1] = value;

	return 1;
}

FtraceDriver::FtraceDriver() : mValues(NULL) {
}

FtraceDriver::~FtraceDriver() {
	delete [] mValues;
}

void FtraceDriver::readEvents(mxml_node_t *const xml) {
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
			logg->logError(__FILE__, __LINE__, "The regex counter %s is missing the required regex attribute", counter);
			handleException();
		}
		setCounters(new FtraceCounter(getCounters(), strdup(counter), regex));
		++count;
	}

	mValues = new int64_t[2*count];
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
