/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ConfigurationXML.h"

#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include "Driver.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

static const char* ATTR_COUNTER            = "counter";
static const char* ATTR_REVISION           = "revision";
static const char* ATTR_EVENT              = "event";
static const char* ATTR_COUNT              = "count";

ConfigurationXML::ConfigurationXML() {
	const char * configuration_xml;
	unsigned int configuration_xml_len;
	getDefaultConfigurationXml(configuration_xml, configuration_xml_len);
	
	char path[PATH_MAX];

	getPath(path);
	mConfigurationXML = util->readFromDisk(path);

	for (int retryCount = 0; retryCount < 2; ++retryCount) {
		if (mConfigurationXML == NULL) {
			logg->logMessage("Unable to locate configuration.xml, using default in binary");
			// null-terminate configuration_xml
			mConfigurationXML = (char*)malloc(configuration_xml_len + 1);
			memcpy(mConfigurationXML, (const void*)configuration_xml, configuration_xml_len);
			mConfigurationXML[configuration_xml_len] = 0;
		}

		int ret = parse(mConfigurationXML);
		if (ret == 1) {
			remove();

			// Free the current configuration and reload
			free((void*)mConfigurationXML);
			mConfigurationXML = NULL;
			continue;
		}

		break;
	}
	
	validate();
}

ConfigurationXML::~ConfigurationXML() {
	if (mConfigurationXML) {
		free((void*)mConfigurationXML);
	}
}

int ConfigurationXML::parse(const char* configurationXML) {
	mxml_node_t *tree, *node;
	int ret;

	// clear counter overflow
	gSessionData->mCounterOverflow = 0;
	gSessionData->mIsEBS = false;
	mIndex = 0;

	// disable all counters prior to parsing the configuration xml
	for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
		gSessionData->mCounters[i].setEnabled(false);
	}

	tree = mxmlLoadString(NULL, configurationXML, MXML_NO_CALLBACK);

	node = mxmlGetFirstChild(tree);
	while (node && mxmlGetType(node) != MXML_ELEMENT)
		node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
	
	ret = configurationsTag(node);

	node = mxmlGetFirstChild(node);
	while (node) {
		if (mxmlGetType(node) != MXML_ELEMENT) {
			node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
			continue;
		}
		configurationTag(node);
		node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
	}

	mxmlDelete(tree);

	return ret;
}

void ConfigurationXML::validate(void) {
	for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
		const Counter & counter = gSessionData->mCounters[i];
		if (counter.isEnabled()) {
			if (strcmp(counter.getType(), "") == 0) {
				logg->logError(__FILE__, __LINE__, "Invalid required attribute in configuration.xml:\n  counter=\"%s\"\n  event=%d\n", counter.getType(), counter.getEvent());
				handleException();
			}

			// iterate through the remaining enabled performance counters
			for (int j = i + 1; j < MAX_PERFORMANCE_COUNTERS; j++) {
				const Counter & counter2 = gSessionData->mCounters[j];
				if (counter2.isEnabled()) {
					// check if the types are the same
					if (strcmp(counter.getType(), counter2.getType()) == 0) {
						logg->logError(__FILE__, __LINE__, "Duplicate performance counter type in configuration.xml: %s", counter.getType());
						handleException();
					}
				}
			}
		}
	}
}

#define CONFIGURATION_REVISION 3
int ConfigurationXML::configurationsTag(mxml_node_t *node) {
	const char* revision_string;
	
	revision_string = mxmlElementGetAttr(node, ATTR_REVISION);
	if (!revision_string) {
		return 1; //revision issue;
	}

	int revision = strtol(revision_string, NULL, 10);
	if (revision < CONFIGURATION_REVISION) {
		return 1; // revision issue
	}

	// A revision >= CONFIGURATION_REVISION is okay
	// Greater than can occur when Streamline is newer than gator

	return 0;
}

void ConfigurationXML::configurationTag(mxml_node_t *node) {
	// handle all other performance counters
	if (mIndex >= MAX_PERFORMANCE_COUNTERS) {
		mIndex++;
		gSessionData->mCounterOverflow = mIndex;
		return;
	}

	// read attributes
	Counter & counter = gSessionData->mCounters[mIndex];
	counter.clear();
	if (mxmlElementGetAttr(node, ATTR_COUNTER)) counter.setType(mxmlElementGetAttr(node, ATTR_COUNTER));
	if (mxmlElementGetAttr(node, ATTR_EVENT)) counter.setEvent(strtol(mxmlElementGetAttr(node, ATTR_EVENT), NULL, 16));
	if (mxmlElementGetAttr(node, ATTR_COUNT)) counter.setCount(strtol(mxmlElementGetAttr(node, ATTR_COUNT), NULL, 10));
	if (counter.getCount() > 0) {
		gSessionData->mIsEBS = true;
	}
	counter.setEnabled(true);

	// Associate a driver with each counter
	for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
		if (driver->claimCounter(counter)) {
			if (counter.getDriver() != NULL) {
				logg->logError(__FILE__, __LINE__, "More than one driver has claimed %s:%i", counter.getType(), counter.getEvent());
				handleException();
			}
			counter.setDriver(driver);
		}
	}

	// If no driver is associated with the counter, disable it
	if (counter.getDriver() == NULL) {
		logg->logMessage("No driver has claimed %s:%i", counter.getType(), counter.getEvent());
		counter.setEnabled(false);
	}

	if (counter.isEnabled()) {
		// update counter index
		mIndex++;
	}
}

void ConfigurationXML::getDefaultConfigurationXml(const char * & xml, unsigned int & len) {
#include "defaults_xml.h" // defines and initializes char defaults_xml[] and int defaults_xml_len
	xml = (const char *)defaults_xml;
	len = defaults_xml_len;
}

void ConfigurationXML::getPath(char* path) {
	if (gSessionData->mConfigurationXMLPath) {
		strncpy(path, gSessionData->mConfigurationXMLPath, PATH_MAX);
	} else {
		if (util->getApplicationFullPath(path, PATH_MAX) != 0) {
			logg->logMessage("Unable to determine the full path of gatord, the cwd will be used");
		}
		strncat(path, "configuration.xml", PATH_MAX - strlen(path) - 1);
	}
}

void ConfigurationXML::remove() {
	char path[PATH_MAX];
	getPath(path);

	if (::remove(path) != 0) {
		logg->logError(__FILE__, __LINE__, "Invalid configuration.xml file detected and unable to delete it. To resolve, delete configuration.xml on disk");
		handleException();
	}
	logg->logMessage("Invalid configuration.xml file detected and removed");
}
