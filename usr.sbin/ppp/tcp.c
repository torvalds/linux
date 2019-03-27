/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Brian Somers <brian@Awfulhak.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

#include "layer.h"
#include "defs.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "lqr.h"
#include "hdlc.h"
#include "throughput.h"
#include "fsm.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "async.h"
#include "descriptor.h"
#include "physical.h"
#include "tcp.h"

static int
tcp_OpenConnection(const char *name, char *host, char *port)
{
  struct sockaddr_in dest;
  int sock;
  struct servent *sp;

  dest.sin_family = AF_INET;
  dest.sin_addr = GetIpAddr(host);
  if (dest.sin_addr.s_addr == INADDR_NONE) {
    log_Printf(LogWARN, "%s: %s: unknown host\n", name, host);
    return -2;
  }
  dest.sin_port = htons(atoi(port));
  if (dest.sin_port == 0) {
    sp = getservbyname(port, "tcp");
    if (sp)
      dest.sin_port = sp->s_port;
    else {
      log_Printf(LogWARN, "%s: %s: unknown service\n", name, port);
      return -2;
    }
  }
  log_Printf(LogPHASE, "%s: Connecting to %s:%s/tcp\n", name, host, port);

  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return -2;

  if (connect(sock, (struct sockaddr *)&dest, sizeof dest) < 0) {
    log_Printf(LogWARN, "%s: connect: %s\n", name, strerror(errno));
    close(sock);
    return -2;
  }

  return sock;
}

static struct device tcpdevice = {
  TCP_DEVICE,
  "tcp",
  0,
  { CD_NOTREQUIRED, 0 },
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

struct device *
tcp_iov2device(int type, struct physical *p, struct iovec *iov,
               int *niov, int maxiov __unused, int *auxfd __unused,
	       int *nauxfd __unused)
{
  if (type == TCP_DEVICE) {
    free(iov[(*niov)++].iov_base);
    physical_SetupStack(p, tcpdevice.name, PHYSICAL_FORCE_ASYNC);
    return &tcpdevice;
  }

  return NULL;
}

struct device *
tcp_Create(struct physical *p)
{
  char *cp, *host, *port, *svc;

  if (p->fd < 0) {
    if ((cp = strchr(p->name.full, ':')) != NULL && !strchr(cp + 1, ':')) {
      *cp = '\0';
      host = p->name.full;
      port = cp + 1;
      svc = strchr(port, '/');
      if (svc && strcasecmp(svc, "/tcp")) {
        *cp = ':';
        return 0;
      }
      if (svc) {
        p->fd--;     /* We own the device but maybe can't use it - change fd */
        *svc = '\0';
      }
      if (*host && *port) {
        p->fd = tcp_OpenConnection(p->link.name, host, port);
        *cp = ':';
        if (svc)
          *svc = '/';
        if (p->fd >= 0)
          log_Printf(LogDEBUG, "%s: Opened tcp socket %s\n", p->link.name,
                     p->name.full);
      } else {
        if (svc)
          *svc = '/';
        *cp = ':';
      }
    }
  }

  if (p->fd >= 0) {
    /* See if we're a tcp socket */
    struct stat st;

    if (fstat(p->fd, &st) != -1 && (st.st_mode & S_IFSOCK)) {
      int type, sz;

      sz = sizeof type;
      if (getsockopt(p->fd, SOL_SOCKET, SO_TYPE, &type, &sz) == -1) {
        log_Printf(LogPHASE, "%s: Link is a closed socket !\n", p->link.name);
        close(p->fd);
        p->fd = -1;
        return NULL;
      }

      if (sz == sizeof type && type == SOCK_STREAM) {
        struct sockaddr_in sock;
        struct sockaddr *sockp = (struct sockaddr *)&sock;

        if (*p->name.full == '\0') {
          sz = sizeof sock;
          if (getpeername(p->fd, sockp, &sz) != 0 ||
              sz != sizeof(struct sockaddr_in) || sock.sin_family != AF_INET) {
            log_Printf(LogDEBUG, "%s: Link is SOCK_STREAM, but not inet\n",
                       p->link.name);
            return NULL;
          }

          log_Printf(LogPHASE, "%s: Link is a tcp socket\n", p->link.name);

          snprintf(p->name.full, sizeof p->name.full, "%s:%d/tcp",
                   inet_ntoa(sock.sin_addr), ntohs(sock.sin_port));
          p->name.base = p->name.full;
        }
        physical_SetupStack(p, tcpdevice.name, PHYSICAL_FORCE_ASYNC);
        if (p->cfg.cd.necessity != CD_DEFAULT)
          log_Printf(LogWARN, "Carrier settings ignored\n");
        return &tcpdevice;
      }
    }
  }

  return NULL;
}
