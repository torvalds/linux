/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ANNOTATELISTENER_H
#define ANNOTATELISTENER_H

struct AnnotateClient;
class OlyServerSocket;

class AnnotateListener {
public:
	AnnotateListener();
	~AnnotateListener();

	void setup();
	int getSockFd();
	int getUdsFd();

	void handleSock();
	void handleUds();
	void close();
	void signal();

private:
	AnnotateClient *mClients;
	OlyServerSocket *mSock;
	OlyServerSocket *mUds;

	// Intentionally unimplemented
	AnnotateListener(const AnnotateListener &);
	AnnotateListener &operator=(const AnnotateListener &);
};

#endif // ANNOTATELISTENER_H
