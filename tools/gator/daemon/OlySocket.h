/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OLY_SOCKET_H__
#define __OLY_SOCKET_H__

#include <string.h>

class OlySocket {
public:
  OlySocket(int port, bool multipleConnections = false);
  OlySocket(int port, char* hostname);
  ~OlySocket();
  int acceptConnection();
  void closeSocket();
  void closeServerSocket();
  void shutdownConnection();
  void send(char* buffer, int size);
  void sendString(const char* string) {send((char*)string, strlen(string));}
  int receive(char* buffer, int size);
  int receiveNBytes(char* buffer, int size);
  int receiveString(char* buffer, int size);
  int getSocketID() {return mSocketID;}
private:
  int mSocketID, mFDServer;
  void createClientSocket(char* hostname, int port);
  void createSingleServerConnection(int port);
  void createServerSocket(int port);
};

#endif //__OLY_SOCKET_H__
