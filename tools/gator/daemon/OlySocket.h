/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OLY_SOCKET_H__
#define __OLY_SOCKET_H__

class OlySocket {
public:
  OlySocket(int port, const char* hostname);
  OlySocket(int socketID);
#ifndef WIN32
  OlySocket(const char* path);
#endif
  ~OlySocket();

  void closeSocket();
  void shutdownConnection();
  void send(const char* buffer, int size);
  int receive(char* buffer, int size);
  int receiveNBytes(char* buffer, int size);
  int receiveString(char* buffer, int size);

  bool isValid() const { return mSocketID >= 0; }

private:
  int mSocketID;

  void createClientSocket(const char* hostname, int port);
};

class OlyServerSocket {
public:
  OlyServerSocket(int port);
#ifndef WIN32
  OlyServerSocket(const char* path);
#endif
  ~OlyServerSocket();

  int acceptConnection();
  void closeServerSocket();

private:
  int mFDServer;

  void createServerSocket(int port);
};

#endif //__OLY_SOCKET_H__
