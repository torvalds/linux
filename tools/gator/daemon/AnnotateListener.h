/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

class AnnotateClient;
class OlyServerSocket;

class AnnotateListener {
public:
	AnnotateListener();
	~AnnotateListener();

	void setup();
	int getFd();

	void handle();
	void close();
	void signal();

private:
	AnnotateClient *mClients;
	OlyServerSocket *mSock;

	// Intentionally unimplemented
	AnnotateListener(const AnnotateListener &);
	AnnotateListener &operator=(const AnnotateListener &);
};
