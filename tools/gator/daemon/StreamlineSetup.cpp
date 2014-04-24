/**
 * Copyright (C) ARM Limited 2011-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "StreamlineSetup.h"

#include "Buffer.h"
#include "CapturedXML.h"
#include "ConfigurationXML.h"
#include "Driver.h"
#include "EventsXML.h"
#include "Logging.h"
#include "OlySocket.h"
#include "OlyUtility.h"
#include "Sender.h"
#include "SessionData.h"

static const char* TAG_SESSION = "session";
static const char* TAG_REQUEST = "request";
static const char* TAG_CONFIGURATIONS = "configurations";

static const char* ATTR_TYPE           = "type";
static const char* VALUE_EVENTS        = "events";
static const char* VALUE_CONFIGURATION = "configuration";
static const char* VALUE_COUNTERS      = "counters";
static const char* VALUE_CAPTURED      = "captured";
static const char* VALUE_DEFAULTS      = "defaults";

StreamlineSetup::StreamlineSetup(OlySocket* s) {
	bool ready = false;
	char* data = NULL;
	int type;

	mSocket = s;

	// Receive commands from Streamline (master)
	while (!ready) {
		// receive command over socket
		gSessionData->mWaitingOnCommand = true;
		data = readCommand(&type);

		// parse and handle data
		switch (type) {
			case COMMAND_REQUEST_XML:
				handleRequest(data);
				break;
			case COMMAND_DELIVER_XML:
				handleDeliver(data);
				break;
			case COMMAND_APC_START:
				logg->logMessage("Received apc start request");
				ready = true;
				break;
			case COMMAND_APC_STOP:
				logg->logMessage("Received apc stop request before apc start request");
				exit(0);
				break;
			case COMMAND_DISCONNECT:
				logg->logMessage("Received disconnect command");
				exit(0);
				break;
			case COMMAND_PING:
				logg->logMessage("Received ping command");
				sendData(NULL, 0, RESPONSE_ACK);
				break;
			default:
				logg->logError(__FILE__, __LINE__, "Target error: Unknown command type, %d", type);
				handleException();
		}

		free(data);
	}

	if (gSessionData->mCounterOverflow > 0) {
		logg->logError(__FILE__, __LINE__, "Only %i performance counters are permitted, %i are selected", MAX_PERFORMANCE_COUNTERS, gSessionData->mCounterOverflow);
		handleException();
	}
}

StreamlineSetup::~StreamlineSetup() {
}

char* StreamlineSetup::readCommand(int* command) {
	unsigned char header[5];
	char* data;
	int response;

	// receive type and length
	response = mSocket->receiveNBytes((char*)&header, sizeof(header));

	// After receiving a single byte, we are no longer waiting on a command
	gSessionData->mWaitingOnCommand = false;

	if (response < 0) {
		logg->logError(__FILE__, __LINE__, "Target error: Unexpected socket disconnect");
		handleException();
	}

	const char type = header[0];
	const int length = (header[1] << 0) | (header[2] << 8) | (header[3] << 16) | (header[4] << 24);

	// add artificial limit
	if ((length < 0) || length > 1024 * 1024) {
		logg->logError(__FILE__, __LINE__, "Target error: Invalid length received, %d", length);
		handleException();
	}

	// allocate memory to contain the xml file, size of zero returns a zero size object
	data = (char*)calloc(length + 1, 1);
	if (data == NULL) {
		logg->logError(__FILE__, __LINE__, "Unable to allocate memory for xml");
		handleException();
	}

	// receive data
	response = mSocket->receiveNBytes(data, length);
	if (response < 0) {
		logg->logError(__FILE__, __LINE__, "Target error: Unexpected socket disconnect");
		handleException();
	}

	// null terminate the data for string parsing
	if (length > 0) {
		data[length] = 0;
	}

	*command = type;
	return data;
}

void StreamlineSetup::handleRequest(char* xml) {
	mxml_node_t *tree, *node;
	const char * attr = NULL;

	tree = mxmlLoadString(NULL, xml, MXML_NO_CALLBACK);
	node = mxmlFindElement(tree, tree, TAG_REQUEST, ATTR_TYPE, NULL, MXML_DESCEND_FIRST);
	if (node) {
		attr = mxmlElementGetAttr(node, ATTR_TYPE);
	}
	if (attr && strcmp(attr, VALUE_EVENTS) == 0) {
		sendEvents();
		logg->logMessage("Sent events xml response");
	} else if (attr && strcmp(attr, VALUE_CONFIGURATION) == 0) {
		sendConfiguration();
		logg->logMessage("Sent configuration xml response");
	} else if (attr && strcmp(attr, VALUE_COUNTERS) == 0) {
		sendCounters();
		logg->logMessage("Sent counters xml response");
	} else if (attr && strcmp(attr, VALUE_CAPTURED) == 0) {
		CapturedXML capturedXML;
		char* capturedText = capturedXML.getXML(false);
		sendData(capturedText, strlen(capturedText), RESPONSE_XML);
		free(capturedText);
		logg->logMessage("Sent captured xml response");
	} else if (attr && strcmp(attr, VALUE_DEFAULTS) == 0) {
		sendDefaults();
		logg->logMessage("Sent default configuration xml response");
	} else {
		char error[] = "Unknown request";
		sendData(error, strlen(error), RESPONSE_NAK);
		logg->logMessage("Received unknown request:\n%s", xml);
	}

	mxmlDelete(tree);
}

void StreamlineSetup::handleDeliver(char* xml) {
	mxml_node_t *tree;

	// Determine xml type
	tree = mxmlLoadString(NULL, xml, MXML_NO_CALLBACK);
	if (mxmlFindElement(tree, tree, TAG_SESSION, NULL, NULL, MXML_DESCEND_FIRST)) {
		// Session XML
		gSessionData->parseSessionXML(xml);
		sendData(NULL, 0, RESPONSE_ACK);
		logg->logMessage("Received session xml");
	} else if (mxmlFindElement(tree, tree, TAG_CONFIGURATIONS, NULL, NULL, MXML_DESCEND_FIRST)) {
		// Configuration XML
		writeConfiguration(xml);
		sendData(NULL, 0, RESPONSE_ACK);
		logg->logMessage("Received configuration xml");
	} else {
		// Unknown XML
		logg->logMessage("Received unknown XML delivery type");
		sendData(NULL, 0, RESPONSE_NAK);
	}

	mxmlDelete(tree);
}

void StreamlineSetup::sendData(const char* data, uint32_t length, char type) {
	unsigned char header[5];
	header[0] = type;
	Buffer::writeLEInt(header + 1, length);
	mSocket->send((char*)&header, sizeof(header));
	mSocket->send((const char*)data, length);
}

void StreamlineSetup::sendEvents() {
	EventsXML eventsXML;
	char* string = eventsXML.getXML();
	sendString(string, RESPONSE_XML);
	free(string);
}

void StreamlineSetup::sendConfiguration() {
	ConfigurationXML xml;

	const char* string = xml.getConfigurationXML();
	sendData(string, strlen(string), RESPONSE_XML);
}

void StreamlineSetup::sendDefaults() {
	// Send the config built into the binary
	const char* xml;
	unsigned int size;
	ConfigurationXML::getDefaultConfigurationXml(xml, size);

	// Artificial size restriction
	if (size > 1024*1024) {
		logg->logError(__FILE__, __LINE__, "Corrupt default configuration file");
		handleException();
	}

	sendData(xml, size, RESPONSE_XML);
}

void StreamlineSetup::sendCounters() {
	mxml_node_t *xml;
	mxml_node_t *counters;

	xml = mxmlNewXML("1.0");
	counters = mxmlNewElement(xml, "counters");
	int count = 0;
	for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
		count += driver->writeCounters(counters);
	}

	if (count == 0) {
		logg->logError(__FILE__, __LINE__, "No counters found, this could be because /dev/gator/events can not be read or because perf is not working correctly");
		handleException();
	}

	char* string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
	sendString(string, RESPONSE_XML);

	free(string);
	mxmlDelete(xml);
}

void StreamlineSetup::writeConfiguration(char* xml) {
	char path[PATH_MAX];

	ConfigurationXML::getPath(path);

	if (util->writeToDisk(path, xml) < 0) {
		logg->logError(__FILE__, __LINE__, "Error writing %s\nPlease verify write permissions to this path.", path);
		handleException();
	}

	// Re-populate gSessionData with the configuration, as it has now changed
	{ ConfigurationXML configuration; }

	if (gSessionData->mCounterOverflow > 0) {
		logg->logError(__FILE__, __LINE__, "Only %i performance counters counters are permitted, %i are selected", MAX_PERFORMANCE_COUNTERS, gSessionData->mCounterOverflow);
		handleException();
	}
}
