/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "OlySocket.h"

#include <stdio.h>
#include <string.h>
#ifdef WIN32
#include <Winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
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

int socket_cloexec(int domain, int type, int protocol) {
#ifdef SOCK_CLOEXEC
  return socket(domain, type | SOCK_CLOEXEC, protocol);
#else
  int sock = socket(domain, type, protocol);
#ifdef FD_CLOEXEC
  if (sock < 0) {
    return -1;
  }
  int fdf = fcntl(sock, F_GETFD);
  if ((fdf == -1) || (fcntl(sock, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
    close(sock);
    return -1;
  }
#endif
  return sock;
#endif
}

int accept_cloexec(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  int sock;
#ifdef SOCK_CLOEXEC
  sock = accept4(sockfd, addr, addrlen, SOCK_CLOEXEC);
  if (sock >= 0) {
    return sock;
  }
  // accept4 with SOCK_CLOEXEC may not work on all kernels, so fallback
#endif
  sock = accept(sockfd, addr, addrlen);
#ifdef FD_CLOEXEC
  if (sock < 0) {
    return -1;
  }
  int fdf = fcntl(sock, F_GETFD);
  if ((fdf == -1) || (fcntl(sock, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
    close(sock);
    return -1;
  }
#endif
  return sock;
}

OlyServerSocket::OlyServerSocket(int port) {
#ifdef WIN32
  WSADATA wsaData;
  if (WSAStartup(0x0202, &wsaData) != 0) {
    logg->logError(__FILE__, __LINE__, "Windows socket initialization failed");
    handleException();
  }
#endif

  createServerSocket(port);
}

OlySocket::OlySocket(int socketID) : mSocketID(socketID) {
}

#ifndef WIN32

#define MIN(A, B) ({ \
  const __typeof__(A) __a = A; \
  const __typeof__(B) __b = B; \
  __a > __b ? __b : __a; \
})

OlyServerSocket::OlyServerSocket(const char* path, const size_t pathSize) {
  // Create socket
  mFDServer = socket_cloexec(PF_UNIX, SOCK_STREAM, 0);
  if (mFDServer < 0) {
    logg->logError(__FILE__, __LINE__, "Error creating server socket");
    handleException();
  }

  // Create sockaddr_in structure, ensuring non-populated fields are zero
  struct sockaddr_un sockaddr;
  memset((void*)&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sun_family = AF_UNIX;
  memcpy(sockaddr.sun_path, path, MIN(pathSize, sizeof(sockaddr.sun_path)));
  sockaddr.sun_path[sizeof(sockaddr.sun_path) - 1] = '\0';

  // Bind the socket to an address
  if (bind(mFDServer, (const struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
    logg->logError(__FILE__, __LINE__, "Binding of server socket failed.");
    handleException();
  }

  // Listen for connections on this socket
  if (listen(mFDServer, 1) < 0) {
    logg->logError(__FILE__, __LINE__, "Listening of server socket failed");
    handleException();
  }
}

int OlySocket::connect(const char* path, const size_t pathSize) {
  int fd = socket_cloexec(PF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  // Create sockaddr_in structure, ensuring non-populated fields are zero
  struct sockaddr_un sockaddr;
  memset((void*)&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sun_family = AF_UNIX;
  memcpy(sockaddr.sun_path, path, MIN(pathSize, sizeof(sockaddr.sun_path)));
  sockaddr.sun_path[sizeof(sockaddr.sun_path) - 1] = '\0';

  if (::connect(fd, (const struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
    close(fd);
    return -1;
  }

  return fd;
}

#endif

OlySocket::~OlySocket() {
  if (mSocketID > 0) {
    CLOSE_SOCKET(mSocketID);
  }
}

OlyServerSocket::~OlyServerSocket() {
  if (mFDServer > 0) {
    CLOSE_SOCKET(mFDServer);
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

void OlyServerSocket::closeServerSocket() {
  if (CLOSE_SOCKET(mFDServer) != 0) {
    logg->logError(__FILE__, __LINE__, "Failed to close server socket.");
    handleException();
  }
  mFDServer = 0;
}

void OlyServerSocket::createServerSocket(int port) {
  int family = AF_INET6;

  // Create socket
  mFDServer = socket_cloexec(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (mFDServer < 0) {
    family = AF_INET;
    mFDServer = socket_cloexec(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mFDServer < 0) {
      logg->logError(__FILE__, __LINE__, "Error creating server socket");
      handleException();
    }
  }

  // Enable address reuse, another solution would be to create the server socket once and only close it when the object exits
  int on = 1;
  if (setsockopt(mFDServer, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on)) != 0) {
    logg->logError(__FILE__, __LINE__, "Setting server socket options failed");
    handleException();
  }

  // Create sockaddr_in structure, ensuring non-populated fields are zero
  struct sockaddr_in6 sockaddr;
  memset((void*)&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin6_family = family;
  sockaddr.sin6_port = htons(port);
  sockaddr.sin6_addr = in6addr_any;

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
int OlyServerSocket::acceptConnection() {
  int socketID;
  if (mFDServer <= 0) {
    logg->logError(__FILE__, __LINE__, "Attempting multiple connections on a single connection server socket or attempting to accept on a client socket");
    handleException();
  }

  // Accept a connection, note that this call blocks until a client connects
  socketID = accept_cloexec(mFDServer, NULL, NULL);
  if (socketID < 0) {
    logg->logError(__FILE__, __LINE__, "Socket acceptance failed");
    handleException();
  }
  return socketID;
}

void OlySocket::send(const char* buffer, int size) {
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
