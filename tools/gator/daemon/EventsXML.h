/**
 * Copyright (C) ARM Limited 2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EVENTS_XML
#define EVENTS_XML

class EventsXML {
public:
	char* getXML();
	void write(const char* path);
};

#endif // EVENTS_XML
