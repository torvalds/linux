/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SESSION_XML_H
#define SESSION_XML_H

#include "mxml/mxml.h"

struct ImageLinkList;

struct ConfigParameters {
	char buffer_mode[64];	// buffer mode, "streaming", "low", "normal", "high" defines oneshot and buffer size
	char sample_rate[64];	// capture mode, "high", "normal", or "low"
	int duration;		// length of profile in seconds
	bool call_stack_unwinding;	// whether stack unwinding is performed
	int live_rate;
	struct ImageLinkList *images;	// linked list of image strings
};

class SessionXML {
public:
	SessionXML(const char* str);
	~SessionXML();
	void parse();
	ConfigParameters parameters;
private:
	char*  mSessionXML;
	char*  mPath;
	void sessionTag(mxml_node_t *tree, mxml_node_t *node);
	void sessionImage(mxml_node_t *node);
};

#endif // SESSION_XML_H
