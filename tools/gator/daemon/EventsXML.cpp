/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "EventsXML.h"

#include "CapturedXML.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

mxml_node_t *EventsXML::getTree() {
#include "events_xml.h" // defines and initializes char events_xml[] and int events_xml_len
	char path[PATH_MAX];
	mxml_node_t *xml;
	FILE *fl;

	// Avoid unused variable warning
	(void)events_xml_len;

	// Load the provided or default events xml
	if (gSessionData->mEventsXMLPath) {
		strncpy(path, gSessionData->mEventsXMLPath, PATH_MAX);
	} else {
		util->getApplicationFullPath(path, PATH_MAX);
		strncat(path, "events.xml", PATH_MAX - strlen(path) - 1);
	}
	fl = fopen(path, "r");
	if (fl) {
		xml = mxmlLoadFile(NULL, fl, MXML_NO_CALLBACK);
		fclose(fl);
	} else {
		logg->logMessage("Unable to locate events.xml, using default");
		xml = mxmlLoadString(NULL, (const char *)events_xml, MXML_NO_CALLBACK);
	}

	return xml;
}

char *EventsXML::getXML() {
	mxml_node_t *xml = getTree();

	// Add dynamic events from the drivers
	mxml_node_t *events = mxmlFindElement(xml, xml, "events", NULL, NULL, MXML_DESCEND);
	if (!events) {
		logg->logError(__FILE__, __LINE__, "Unable to find <events> node in the events.xml");
		handleException();
	}
	for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
		driver->writeEvents(events);
	}

	char *string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
	mxmlDelete(xml);

	return string;
}

void EventsXML::write(const char *path) {
	char file[PATH_MAX];

	// Set full path
	snprintf(file, PATH_MAX, "%s/events.xml", path);

	char *buf = getXML();
	if (util->writeToDisk(file, buf) < 0) {
		logg->logError(__FILE__, __LINE__, "Error writing %s\nPlease verify the path.", file);
		handleException();
	}

	free(buf);
}
