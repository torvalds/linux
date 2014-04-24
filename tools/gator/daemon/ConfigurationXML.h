/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef COUNTERS_H
#define COUNTERS_H

#include "mxml/mxml.h"

class ConfigurationXML {
public:
	static void getDefaultConfigurationXml(const char * & xml, unsigned int & len);
	static void getPath(char* path);
	static void remove();

	ConfigurationXML();
	~ConfigurationXML();
	const char* getConfigurationXML() {return mConfigurationXML;}
	void validate(void);

private:
	char* mConfigurationXML;
	int mIndex;

	int parse(const char* xmlFile);
	int configurationsTag(mxml_node_t *node);
	void configurationTag(mxml_node_t *node);

	// Intentionally unimplemented
	ConfigurationXML(const ConfigurationXML &);
	ConfigurationXML &operator=(const ConfigurationXML &);
};

#endif // COUNTERS_H
