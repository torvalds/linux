/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OLY_SOCKET_H__
#define __OLY_SOCKET_H__

#include <stddef.h>

#ifdef WIN32
typedef socklen_t int;
#else
#include <sys/socket.h>
#endif

class OlySocket {
public:
#ifndef WIN32
  static int connect(const char* path, const size_t pathSize);
#endif

  OlySocket(int socketID);
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
};

class OlyServerSocket {
public:
  OlyServerSocket(int port);
#ifndef WIN32
  OlyServerSocket(const char* path, const size_t pathSize);
#endif
  ~OlyServerSocket();

  int acceptConnection();
  void closeServerSocket();

  int getFd() { return mFDServer; }

private:
  int mFDServer;

  void createServerSocket(int port);
};

int socket_cloexec(int domain, int type, int protocol);
int accept_cloexec(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

#endif //__OLY_SOCKET_H__
