/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__STREAMLINE_SETUP_H__
#define	__STREAMLINE_SETUP_H__

#include <stdint.h>
#include <string.h>

class OlySocket;

// Commands from Streamline
enum {
	COMMAND_REQUEST_XML = 0,
	COMMAND_DELIVER_XML = 1,
	COMMAND_APC_START   = 2,
	COMMAND_APC_STOP    = 3,
	COMMAND_DISCONNECT  = 4,
	COMMAND_PING		= 5
};

class StreamlineSetup {
public:
	StreamlineSetup(OlySocket *socket);
	~StreamlineSetup();
private:
	OlySocket* mSocket;

	char* readCommand(int*);
	void handleRequest(char* xml);
	void handleDeliver(char* xml);
	void sendData(const char* data, uint32_t length, char type);
	void sendString(const char* string, int type) {sendData(string, strlen(string), type);}
	void sendEvents();
	void sendConfiguration();
	void sendDefaults();
	void sendCounters();
	void writeConfiguration(char* xml);

	// Intentionally unimplemented
	StreamlineSetup(const StreamlineSetup &);
	StreamlineSetup &operator=(const StreamlineSetup &);
};

#endif 	//__STREAMLINE_SETUP_H__
