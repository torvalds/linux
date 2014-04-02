/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "SessionXML.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

static const char*	TAG_SESSION = "session";
static const char*	TAG_IMAGE	= "image";

static const char*	ATTR_VERSION            = "version";		
static const char*	ATTR_CALL_STACK_UNWINDING = "call_stack_unwinding";
static const char*	ATTR_BUFFER_MODE        = "buffer_mode";
static const char*	ATTR_SAMPLE_RATE        = "sample_rate";	
static const char*	ATTR_DURATION           = "duration";
static const char*	ATTR_PATH               = "path";
static const char*	ATTR_LIVE_RATE      = "live_rate";

SessionXML::SessionXML(const char* str) {
	parameters.buffer_mode[0] = 0;
	parameters.sample_rate[0] = 0;
	parameters.duration = 0;
	parameters.call_stack_unwinding = false;
	parameters.live_rate = 0;
	parameters.images = NULL;
	mPath = 0;
	mSessionXML = (char*)str;
	logg->logMessage(mSessionXML);
}

SessionXML::~SessionXML() {
	if (mPath != 0) {
		free(mSessionXML);
	}
}

void SessionXML::parse() {
	mxml_node_t *tree;
	mxml_node_t *node;

	tree = mxmlLoadString(NULL, mSessionXML, MXML_NO_CALLBACK);
	node = mxmlFindElement(tree, tree, TAG_SESSION, NULL, NULL, MXML_DESCEND);

	if (node) {
		sessionTag(tree, node);
		mxmlDelete(tree);
		return;
	}

	logg->logError(__FILE__, __LINE__, "No session tag found in the session.xml file");
	handleException();
}

void SessionXML::sessionTag(mxml_node_t *tree, mxml_node_t *node) {
	int version = 0;
	if (mxmlElementGetAttr(node, ATTR_VERSION)) version = strtol(mxmlElementGetAttr(node, ATTR_VERSION), NULL, 10);
	if (version != 1) {
		logg->logError(__FILE__, __LINE__, "Invalid session.xml version: %d", version);
		handleException();
	}

	// copy to pre-allocated strings
	if (mxmlElementGetAttr(node, ATTR_BUFFER_MODE)) {
		strncpy(parameters.buffer_mode, mxmlElementGetAttr(node, ATTR_BUFFER_MODE), sizeof(parameters.buffer_mode));
		parameters.buffer_mode[sizeof(parameters.buffer_mode) - 1] = 0; // strncpy does not guarantee a null-terminated string
	}
	if (mxmlElementGetAttr(node, ATTR_SAMPLE_RATE)) {
		strncpy(parameters.sample_rate, mxmlElementGetAttr(node, ATTR_SAMPLE_RATE), sizeof(parameters.sample_rate));
		parameters.sample_rate[sizeof(parameters.sample_rate) - 1] = 0; // strncpy does not guarantee a null-terminated string
	}

	// integers/bools
	parameters.call_stack_unwinding = util->stringToBool(mxmlElementGetAttr(node, ATTR_CALL_STACK_UNWINDING), false);
	if (mxmlElementGetAttr(node, ATTR_DURATION)) parameters.duration = strtol(mxmlElementGetAttr(node, ATTR_DURATION), NULL, 10);
	if (mxmlElementGetAttr(node, ATTR_LIVE_RATE)) parameters.live_rate = strtol(mxmlElementGetAttr(node, ATTR_LIVE_RATE), NULL, 10);

	// parse subtags
	node = mxmlGetFirstChild(node);
	while (node) {
		if (mxmlGetType(node) != MXML_ELEMENT) {
			node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
			continue;
		}
		if (strcmp(TAG_IMAGE, mxmlGetElement(node)) == 0) {
			sessionImage(node);
		}
		node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
	}
}

void SessionXML::sessionImage(mxml_node_t *node) {
	int length = strlen(mxmlElementGetAttr(node, ATTR_PATH));
	struct ImageLinkList *image;

	image = (struct ImageLinkList *)malloc(sizeof(struct ImageLinkList));
	image->path = (char*)malloc(length + 1);
	image->path = strdup(mxmlElementGetAttr(node, ATTR_PATH));
	image->next = parameters.images;
	parameters.images = image;
}
