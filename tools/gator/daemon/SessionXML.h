/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
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
	// buffer mode, "streaming", "low", "normal", "high" defines oneshot and buffer size
	char buffer_mode[64];
	// capture mode, "high", "normal", or "low"
	char sample_rate[64];
	// whether stack unwinding is performed
	bool call_stack_unwinding;
	int live_rate;
};

class SessionXML {
public:
	SessionXML(const char *str);
	~SessionXML();
	void parse();
	ConfigParameters parameters;
private:
	const char *mSessionXML;
	void sessionTag(mxml_node_t *tree, mxml_node_t *node);
	void sessionImage(mxml_node_t *node);

	// Intentionally unimplemented
	SessionXML(const SessionXML &);
	SessionXML &operator=(const SessionXML &);
};

#endif // SESSION_XML_H
