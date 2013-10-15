/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "OlySocket.h"

#include <stdio.h>
#ifdef WIN32
#include <Winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#endif

#include "Logging.h"

#ifdef WIN32
#define CLOSE_SOCKET(x) closesocket(x)
#define SHUTDOWN_RX_TX SD_BOTH
#define snprintf       _snprintf
#else
#define CLOSE_SOCKET(x) close(x)
#define SHUTDOWN_RX_TX SHUT_RDWR
#endif

OlySocket::OlySocket(int port, bool multiple) {
#ifdef WIN32
  WSADATA wsaData;
  if (WSAStartup(0x0202, &wsaData) != 0) {
    logg->logError(__FILE__, __LINE__, "Windows socket initialization failed");
    handleException();
  }
#endif

  if (multiple) {
    createServerSocket(port);
  } else {
    createSingleServerConnection(port);
  }
}

OlySocket::OlySocket(int port, char* host) {
  mFDServer = 0;
  createClientSocket(host, port);
}

OlySocket::~OlySocket() {
  if (mSocketID > 0) {
    CLOSE_SOCKET(mSocketID);
  }
}

void OlySocket::shutdownConnection() {
  // Shutdown is primarily used to unblock other threads that are blocking on send/receive functions
  shutdown(mSocketID, SHUTDOWN_RX_TX);
}

void OlySocket::closeSocket() {
  // Used for closing an accepted socket but keeping the server socket active
  if (mSocketID > 0) {
    CLOSE_SOCKET(mSocketID);
    mSocketID = -1;
  }
}

void OlySocket::closeServerSocket() {
  if (CLOSE_SOCKET(mFDServer) != 0) {
    logg->logError(__FILE__, __LINE__, "Failed to close server socket.");
    handleException();
  }
  mFDServer = 0;
}

void OlySocket::createClientSocket(char* hostname, int portno) {
#ifdef WIN32
  // TODO: Implement for Windows
#else
  char buf[32];
  struct addrinfo hints, *res, *res0;

  snprintf(buf, sizeof(buf), "%d", portno);
  mSocketID = -1;
  memset((void*)&hints, 0, sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(hostname, buf, &hints, &res0)) {
    logg->logError(__FILE__, __LINE__, "Client socket failed to get address info for %s", hostname);
    handleException();
  }
  for (res=res0; res!=NULL; res = res->ai_next) {
    if ( res->ai_family != PF_INET || res->ai_socktype != SOCK_STREAM ) {
      continue;
    }
    mSocketID = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (mSocketID < 0) {
      continue;
    }
    if (connect(mSocketID, res->ai_addr, res->ai_addrlen) < 0) {
      close(mSocketID);
      mSocketID = -1;
    }
    if (mSocketID > 0) {
      break;
    }
  }
  freeaddrinfo(res0);
  if (mSocketID <= 0) {
    logg->logError(__FILE__, __LINE__, "Could not connect to client socket. Ensure ARM Streamline is running.");
    handleException();
  }
#endif
}

void OlySocket::createSingleServerConnection(int port) {
  createServerSocket(port);

  mSocketID = acceptConnection();
  closeServerSocket();
}

void OlySocket::createServerSocket(int port) {
  // Create socket
  mFDServer = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (mFDServer < 0) {
    logg->logError(__FILE__, __LINE__, "Error creating server socket");
    handleException();
  }

  // Enable address reuse, another solution would be to create the server socket once and only close it when the object exits
  int on = 1;
  if (setsockopt(mFDServer, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on)) != 0) {
    logg->logError(__FILE__, __LINE__, "Setting server socket options failed");
    handleException();
  }

  // Create sockaddr_in structure, ensuring non-populated fields are zero
  struct sockaddr_in sockaddr;
  memset((void*)&sockaddr, 0, sizeof(struct sockaddr_in));
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port   = htons(port);
  sockaddr.sin_addr.s_addr = INADDR_ANY;

  // Bind the socket to an address
  if (bind(mFDServer, (const struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
    logg->logError(__FILE__, __LINE__, "Binding of server socket failed.\nIs an instance already running?");
    handleException();
  }

  // Listen for connections on this socket
  if (listen(mFDServer, 1) < 0) {
    logg->logError(__FILE__, __LINE__, "Listening of server socket failed");
    handleException();
  }
}

// mSocketID is always set to the most recently accepted connection
// The user of this class should maintain the different socket connections, e.g. by forking the process
int OlySocket::acceptConnection() {
  if (mFDServer <= 0) {
    logg->logError(__FILE__, __LINE__, "Attempting multiple connections on a single connection server socket or attempting to accept on a client socket");
    handleException();
  }

  // Accept a connection, note that this call blocks until a client connects
  mSocketID = accept(mFDServer, NULL, NULL);
  if (mSocketID < 0) {
    logg->logError(__FILE__, __LINE__, "Socket acceptance failed");
    handleException();
  }
  return mSocketID;
}

void OlySocket::send(char* buffer, int size) {
  if (size <= 0 || buffer == NULL) {
    return;
  }

  while (size > 0) {
    int n = ::send(mSocketID, buffer, size, 0);
    if (n < 0) {
      logg->logError(__FILE__, __LINE__, "Socket send error");
      handleException();
    }
    size -= n;
    buffer += n;
  }
}

// Returns the number of bytes received
int OlySocket::receive(char* buffer, int size) {
  if (size <= 0 || buffer == NULL) {
    return 0;
  }

  int bytes = recv(mSocketID, buffer, size, 0);
  if (bytes < 0) {
    logg->logError(__FILE__, __LINE__, "Socket receive error");
    handleException();
  } else if (bytes == 0) {
    logg->logMessage("Socket disconnected");
    return -1;
  }
  return bytes;
}

// Receive exactly size bytes of data. Note, this function will block until all bytes are received
int OlySocket::receiveNBytes(char* buffer, int size) {
  int bytes = 0;
  while (size > 0 && buffer != NULL) {
    bytes = recv(mSocketID, buffer, size, 0);
    if (bytes < 0) {
      logg->logError(__FILE__, __LINE__, "Socket receive error");
      handleException();
    } else if (bytes == 0) {
      logg->logMessage("Socket disconnected");
      return -1;
    }
    buffer += bytes;
    size -= bytes;
  }
  return bytes;
}

// Receive data until a carriage return, line feed, or null is encountered, or the buffer fills
int OlySocket::receiveString(char* buffer, int size) {
  int bytes_received = 0;
  bool found = false;

  if (buffer == 0) {
    return 0;
  }

  while (!found && bytes_received < size) {
    // Receive a single character
    int bytes = recv(mSocketID, &buffer[bytes_received], 1, 0);
    if (bytes < 0) {
      logg->logError(__FILE__, __LINE__, "Socket receive error");
      handleException();
    } else if (bytes == 0) {
      logg->logMessage("Socket disconnected");
      return -1;
    }

    // Replace carriage returns and line feeds with zero
    if (buffer[bytes_received] == '\n' || buffer[bytes_received] == '\r' || buffer[bytes_received] == '\0') {
      buffer[bytes_received] = '\0';
      found = true;
    }

    bytes_received++;
  }

  return bytes_received;
}
