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

#include "DriverSource.h"
#include "Logging.h"

class FSCounter : public DriverCounter {
public:
	FSCounter(DriverCounter *next, char *name, char *path, const char *regex);
	~FSCounter();

	const char *getPath() const { return mPath; }

	int64_t read();

private:
	char *const mPath;
	regex_t mReg;
	bool mUseRegex;

	// Intentionally unimplemented
	FSCounter(const FSCounter &);
	FSCounter &operator=(const FSCounter &);
};

FSCounter::FSCounter(DriverCounter *next, char *name, char *path, const char *regex) : DriverCounter(next, name), mPath(path), mUseRegex(regex != NULL) {
	if (mUseRegex) {
		int result = regcomp(&mReg, regex, REG_EXTENDED);
		if (result != 0) {
			char buf[128];
			regerror(result, &mReg, buf, sizeof(buf));
			logg->logError(__FILE__, __LINE__, "Invalid regex '%s': %s", regex, buf);
			handleException();
		}
	}
}

FSCounter::~FSCounter() {
	free(mPath);
	if (mUseRegex) {
		regfree(&mReg);
	}
}

int64_t FSCounter::read() {
	int64_t value;
	if (mUseRegex) {
		char buf[4096];
		size_t pos = 0;
		const int fd = open(mPath, O_RDONLY | O_CLOEXEC);
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
		int result = regexec(&mReg, buf, 2, match, 0);
		if (result != 0) {
			regerror(result, &mReg, buf, sizeof(buf));
			logg->logError(__FILE__, __LINE__, "Parsing %s failed: %s", mPath, buf);
			handleException();
		}

		if (match[1].rm_so < 0) {
			logg->logError(__FILE__, __LINE__, "Parsing %s failed", mPath);
			handleException();
		}

		errno = 0;
		value = strtoll(buf + match[1].rm_so, NULL, 0);
		if (errno != 0) {
			logg->logError(__FILE__, __LINE__, "Parsing %s failed: %s", mPath, strerror(errno));
			handleException();
		}
	} else {
		if (DriverSource::readInt64Driver(mPath, &value) != 0) {
			goto fail;
		}
	}
	return value;

 fail:
	logg->logError(__FILE__, __LINE__, "Unable to read %s", mPath);
	handleException();
}

FSDriver::FSDriver() {
}

FSDriver::~FSDriver() {
}

void FSDriver::readEvents(mxml_node_t *const xml) {
	mxml_node_t *node = xml;
	while (true) {
		node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
		if (node == NULL) {
			break;
		}
		const char *counter = mxmlElementGetAttr(node, "counter");
		if (counter == NULL) {
			continue;
		}

		if (counter[0] == '/') {
			logg->logError(__FILE__, __LINE__, "Old style filesystem counter (%s) detected, please create a new unique counter value and move the filename into the path attribute, see events-Filesystem.xml for examples", counter);
			handleException();
		}

		if (strncmp(counter, "filesystem_", 11) != 0) {
			continue;
		}

		const char *path = mxmlElementGetAttr(node, "path");
		if (path == NULL) {
			logg->logError(__FILE__, __LINE__, "The filesystem counter %s is missing the required path attribute", counter);
			handleException();
		}
		const char *regex = mxmlElementGetAttr(node, "regex");
		setCounters(new FSCounter(getCounters(), strdup(counter), strdup(path), regex));
	}
}

int FSDriver::writeCounters(mxml_node_t *root) const {
	int count = 0;
	for (FSCounter *counter = static_cast<FSCounter *>(getCounters()); counter != NULL; counter = static_cast<FSCounter *>(counter->getNext())) {
		if (access(counter->getPath(), R_OK) == 0) {
			mxml_node_t *node = mxmlNewElement(root, "counter");
			mxmlElementSetAttr(node, "name", counter->getName());
			++count;
		}
	}

	return count;
}
