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
#include <sysexits.h>
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
#include "main.h"
#include "udp.h"


#define UDP_CONNECTED		1
#define UDP_UNCONNECTED		2
#define UDP_MAYBEUNCONNECTED	3

struct udpdevice {
  struct device dev;		/* What struct physical knows about */
  struct sockaddr_in sock;	/* peer address */
  unsigned connected : 2;	/* Have we connect()d ? */
};

#define device2udp(d) ((d)->type == UDP_DEVICE ? (struct udpdevice *)d : NULL)

unsigned
udp_DeviceSize(void)
{
  return sizeof(struct udpdevice);
}

static ssize_t
udp_Sendto(struct physical *p, const void *v, size_t n)
{
  struct udpdevice *dev = device2udp(p->handler);
  int ret;

  switch (dev->connected) {
    case UDP_CONNECTED:
      ret = write(p->fd, v, n);
      break;

    case UDP_UNCONNECTED:
    default:
      ret = sendto(p->fd, v, n, 0, (struct sockaddr *)&dev->sock,
                   sizeof dev->sock);
      break;
  }
  if (dev->connected == UDP_MAYBEUNCONNECTED) {
    if (ret == -1 && errno == EISCONN) {
      dev->connected = UDP_CONNECTED;
      ret = write(p->fd, v, n);
    } else
      dev->connected = UDP_UNCONNECTED;
  }

  return ret;
}

static ssize_t
udp_Recvfrom(struct physical *p, void *v, size_t n)
{
  struct udpdevice *dev = device2udp(p->handler);
  int sz, ret;

  if (dev->connected == UDP_CONNECTED)
    return read(p->fd, v, n);

  sz = sizeof dev->sock;
  ret = recvfrom(p->fd, v, n, 0, (struct sockaddr *)&dev->sock, &sz);

  if (*p->name.full == '\0') {
    snprintf(p->name.full, sizeof p->name.full, "%s:%d/udp",
             inet_ntoa(dev->sock.sin_addr), ntohs(dev->sock.sin_port));
    p->name.base = p->name.full;
  }

  return ret;
}

static void
udp_Free(struct physical *p)
{
  struct udpdevice *dev = device2udp(p->handler);

  free(dev);
}

static void
udp_device2iov(struct device *d, struct iovec *iov, int *niov,
               int maxiov __unused, int *auxfd __unused, int *nauxfd __unused)
{
  int sz = physical_MaxDeviceSize();

  iov[*niov].iov_base = realloc(d, sz);
  if (iov[*niov].iov_base == NULL) {
    log_Printf(LogALERT, "Failed to allocate memory: %d\n", sz);
    AbortProgram(EX_OSERR);
  }
  iov[*niov].iov_len = sz;
  (*niov)++;
}

static const struct device baseudpdevice = {
  UDP_DEVICE,
  "udp",
  0,
  { CD_NOTREQUIRED, 0 },
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  udp_Free,
  udp_Recvfrom,
  udp_Sendto,
  udp_device2iov,
  NULL,
  NULL,
  NULL
};

struct device *
udp_iov2device(int type, struct physical *p, struct iovec *iov, int *niov,
               int maxiov __unused, int *auxfd __unused, int *nauxfd __unused)
{
  if (type == UDP_DEVICE) {
    struct udpdevice *dev = (struct udpdevice *)iov[(*niov)++].iov_base;

    dev = realloc(dev, sizeof *dev);	/* Reduce to the correct size */
    if (dev == NULL) {
      log_Printf(LogALERT, "Failed to allocate memory: %d\n",
                 (int)(sizeof *dev));
      AbortProgram(EX_OSERR);
    }

    /* Refresh function pointers etc */
    memcpy(&dev->dev, &baseudpdevice, sizeof dev->dev);

    physical_SetupStack(p, dev->dev.name, PHYSICAL_FORCE_SYNC);
    return &dev->dev;
  }

  return NULL;
}

static struct udpdevice *
udp_CreateDevice(struct physical *p, char *host, char *port)
{
  struct udpdevice *dev;
  struct servent *sp;

  if ((dev = malloc(sizeof *dev)) == NULL) {
    log_Printf(LogWARN, "%s: Cannot allocate a udp device: %s\n",
               p->link.name, strerror(errno));
    return NULL;
  }

  dev->sock.sin_family = AF_INET;
  dev->sock.sin_addr = GetIpAddr(host);
  if (dev->sock.sin_addr.s_addr == INADDR_NONE) {
    log_Printf(LogWARN, "%s: %s: unknown host\n", p->link.name, host);
    free(dev);
    return NULL;
  }
  dev->sock.sin_port = htons(atoi(port));
  if (dev->sock.sin_port == 0) {
    sp = getservbyname(port, "udp");
    if (sp)
      dev->sock.sin_port = sp->s_port;
    else {
      log_Printf(LogWARN, "%s: %s: unknown service\n", p->link.name, port);
      free(dev);
      return NULL;
    }
  }

  log_Printf(LogPHASE, "%s: Connecting to %s:%s/udp\n", p->link.name,
             host, port);

  p->fd = socket(PF_INET, SOCK_DGRAM, 0);
  if (p->fd >= 0) {
    log_Printf(LogDEBUG, "%s: Opened udp socket %s\n", p->link.name,
               p->name.full);
    if (connect(p->fd, (struct sockaddr *)&dev->sock, sizeof dev->sock) == 0) {
      dev->connected = UDP_CONNECTED;
      return dev;
    } else
      log_Printf(LogWARN, "%s: connect: %s\n", p->name.full, strerror(errno));
  } else
    log_Printf(LogWARN, "%s: socket: %s\n", p->name.full, strerror(errno));

  close(p->fd);
  p->fd = -1;
  free(dev);

  return NULL;
}

struct device *
udp_Create(struct physical *p)
{
  char *cp, *host, *port, *svc;
  struct udpdevice *dev;

  dev = NULL;
  if (p->fd < 0) {
    if ((cp = strchr(p->name.full, ':')) != NULL && !strchr(cp + 1, ':')) {
      *cp = '\0';
      host = p->name.full;
      port = cp + 1;
      svc = strchr(port, '/');
      if (svc && strcasecmp(svc, "/udp")) {
        *cp = ':';
        return NULL;
      }
      if (svc) {
        p->fd--;     /* We own the device but maybe can't use it - change fd */
        *svc = '\0';
      }

      if (*host && *port)
        dev = udp_CreateDevice(p, host, port);

      *cp = ':';
      if (svc)
        *svc = '/';
    }
  } else {
    /* See if we're a connected udp socket */
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

      if (sz == sizeof type && type == SOCK_DGRAM) {
        struct sockaddr_in sock;
        struct sockaddr *sockp = (struct sockaddr *)&sock;

        if ((dev = malloc(sizeof *dev)) == NULL) {
          log_Printf(LogWARN, "%s: Cannot allocate a udp device: %s\n",
                     p->link.name, strerror(errno));
          return NULL;
        }

        if (getpeername(p->fd, sockp, &sz) == 0) {
          log_Printf(LogPHASE, "%s: Link is a connected udp socket\n",
                     p->link.name);
          dev->connected = UDP_CONNECTED;
	} else {
          log_Printf(LogPHASE, "%s: Link is a disconnected udp socket\n",
                     p->link.name);

          dev->connected = UDP_MAYBEUNCONNECTED;

          if (p->link.lcp.cfg.openmode != OPEN_PASSIVE) {
            log_Printf(LogPHASE, "%s:   Changing openmode to PASSIVE\n",
                       p->link.name);
            p->link.lcp.cfg.openmode = OPEN_PASSIVE;
          }
        }
      }
    }
  }

  if (dev) {
    memcpy(&dev->dev, &baseudpdevice, sizeof dev->dev);
    physical_SetupStack(p, dev->dev.name, PHYSICAL_FORCE_SYNC);
    if (p->cfg.cd.necessity != CD_DEFAULT)
      log_Printf(LogWARN, "Carrier settings ignored\n");
    return &dev->dev;
  }

  return NULL;
}
