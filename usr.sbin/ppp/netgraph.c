/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Brian Somers <brian@Awfulhak.org>
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

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netgraph.h>
#include <net/ethernet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netgraph/ng_ether.h>
#include <netgraph/ng_message.h>
#include <netgraph/ng_socket.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <termios.h>
#include <sys/time.h>
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
#include "mp.h"
#include "chat.h"
#include "auth.h"
#include "chap.h"
#include "cbcp.h"
#include "datalink.h"
#include "slcompress.h"
#include "iplist.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "ipv6cp.h"
#include "ncp.h"
#include "filter.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "bundle.h"
#include "id.h"
#include "netgraph.h"


struct ngdevice {
  struct device dev;			/* What struct physical knows about */
  int cs;				/* Control socket */
  char hook[NG_HOOKSIZ];		/* Our socket node hook */
};

#define device2ng(d)	((d)->type == NG_DEVICE ? (struct ngdevice *)d : NULL)
#define NG_MSGBUFSZ	4096
#define NETGRAPH_PREFIX	"netgraph:"

unsigned
ng_DeviceSize(void)
{
  return sizeof(struct ngdevice);
}

static int
ng_MessageOut(struct ngdevice *dev, const char *data)
{
  char path[NG_PATHSIZ];
  char *fmt;
  size_t len;
  int pos, dpos;

  /*
   * We expect a node path, one or more spaces, a command, one or more
   * spaces and an ascii netgraph structure.
   */
  data += strspn(data, " \t");
  len = strcspn(data, " \t");
  if (len >= sizeof path) {
    log_Printf(LogWARN, "%s: %.*s: Node path too long\n",
                 dev->dev.name, len, data);
    return 0;
  }
  memcpy(path, data, len);
  path[len] = '\0';
  data += len;

  data += strspn(data, " \t");
  len = strcspn(data, " \t");
  for (pos = len; pos >= 0; pos--)
    if (data[pos] == '%')
      len++;
  if ((fmt = alloca(len + 4)) == NULL) {
    log_Printf(LogWARN, "%s: alloca(%d) failure... %s\n",
               dev->dev.name, len + 4, strerror(errno));
    return 0;
  }

  /*
   * This is probably a waste of time, but we really don't want to end
   * up stuffing unexpected % escapes into the kernel....
   */
  for (pos = dpos = 0; pos < (int)len;) {
    if (data[dpos] == '%')
      fmt[pos++] = '%';
    fmt[pos++] = data[dpos++];
  }
  strcpy(fmt + pos, " %s");
  data += dpos;

  data += strspn(data, " \t");
  if (NgSendAsciiMsg(dev->cs, path, fmt, data) < 0) {
    log_Printf(LogDEBUG, "%s: NgSendAsciiMsg (to %s): \"%s\", \"%s\": %s\n",
               dev->dev.name, path, fmt, data, strerror(errno));
    return 0;
  }

  return 1;
}

/*
 * Get a netgraph message
 */
static ssize_t
ng_MessageIn(struct physical *p, char *buf, size_t sz)
{
  char msgbuf[sizeof(struct ng_mesg) * 2 + NG_MSGBUFSZ];
  struct ngdevice *dev = device2ng(p->handler);
  struct ng_mesg *rep = (struct ng_mesg *)msgbuf;
  char path[NG_PATHSIZ];
  size_t len;

#ifdef BROKEN_SELECT
  struct timeval t;
  fd_set *r;
  int ret;

  if (dev->cs < 0)
    return 0;

  if ((r = mkfdset()) == NULL) {
    log_Printf(LogERROR, "DoLoop: Cannot create fd_set\n");
    return -1;
  }
  zerofdset(r);
  FD_SET(dev->cs, r);
  t.tv_sec = t.tv_usec = 0;
  ret = select(dev->cs + 1, r, NULL, NULL, &t);
  free(r);

  if (ret <= 0)
    return 0;
#endif

  if (NgRecvAsciiMsg(dev->cs, rep, sizeof msgbuf, path)) {
    log_Printf(LogWARN, "%s: NgRecvAsciiMsg: %s\n",
               dev->dev.name, strerror(errno));
    return -1;
  }

  /* XXX: Should we check rep->header.version ? */

  if (sz == 0)
    log_Printf(LogWARN, "%s: Unexpected message: %s\n", dev->dev.name,
               rep->header.cmdstr);
  else {
    log_Printf(LogDEBUG, "%s: Received message: %s\n", dev->dev.name,
               rep->header.cmdstr);
    len = strlen(rep->header.cmdstr);
    if (sz > len)
      sz = len;
    memcpy(buf, rep->header.cmdstr, sz);
  }

  return sz;
}

static ssize_t
ng_Write(struct physical *p, const void *v, size_t n)
{
  struct ngdevice *dev = device2ng(p->handler);

  switch (p->dl->state) {
    case DATALINK_DIAL:
    case DATALINK_LOGIN:
      return ng_MessageOut(dev, v) ? (ssize_t)n : -1;
  }
  return NgSendData(p->fd, dev->hook, v, n) == -1 ? -1 : (ssize_t)n;
}

static ssize_t
ng_Read(struct physical *p, void *v, size_t n)
{
  char hook[NG_HOOKSIZ];

  switch (p->dl->state) {
    case DATALINK_DIAL:
    case DATALINK_LOGIN:
      return ng_MessageIn(p, v, n);
  }

  return NgRecvData(p->fd, v, n, hook);
}

static int
ng_RemoveFromSet(struct physical *p, fd_set *r, fd_set *w, fd_set *e)
{
  struct ngdevice *dev = device2ng(p->handler);
  int result;

  if (r && dev->cs >= 0 && FD_ISSET(dev->cs, r)) {
    FD_CLR(dev->cs, r);
    log_Printf(LogTIMER, "%s: fdunset(ctrl) %d\n", p->link.name, dev->cs);
    result = 1;
  } else
    result = 0;

  /* Careful... physical_RemoveFromSet() called us ! */

  p->handler->removefromset = NULL;
  result += physical_RemoveFromSet(p, r, w, e);
  p->handler->removefromset = ng_RemoveFromSet;

  return result;
}

static void
ng_Free(struct physical *p)
{
  struct ngdevice *dev = device2ng(p->handler);

  physical_SetDescriptor(p);
  if (dev->cs != -1)
    close(dev->cs);
  free(dev);
}

static void
ng_device2iov(struct device *d, struct iovec *iov, int *niov,
              int maxiov __unused, int *auxfd, int *nauxfd)
{
  struct ngdevice *dev;
  int sz = physical_MaxDeviceSize();

  iov[*niov].iov_base = d = realloc(d, sz);
  if (d == NULL) {
    log_Printf(LogALERT, "Failed to allocate memory: %d\n", sz);
    AbortProgram(EX_OSERR);
  }
  iov[*niov].iov_len = sz;
  (*niov)++;

  dev = device2ng(d);
  *auxfd = dev->cs;
  (*nauxfd)++;
}

static const struct device basengdevice = {
  NG_DEVICE,
  "netgraph",
  0,
  { CD_REQUIRED, DEF_NGCDDELAY },
  NULL,
  ng_RemoveFromSet,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  ng_Free,
  ng_Read,
  ng_Write,
  ng_device2iov,
  NULL,
  NULL,
  NULL
};

struct device *
ng_iov2device(int type, struct physical *p, struct iovec *iov, int *niov,
              int maxiov __unused, int *auxfd, int *nauxfd)
{
  if (type == NG_DEVICE) {
    struct ngdevice *dev = (struct ngdevice *)iov[(*niov)++].iov_base;

    dev = realloc(dev, sizeof *dev);	/* Reduce to the correct size */
    if (dev == NULL) {
      log_Printf(LogALERT, "Failed to allocate memory: %d\n",
                 (int)(sizeof *dev));
      AbortProgram(EX_OSERR);
    }

    if (*nauxfd) {
      dev->cs = *auxfd;
      (*nauxfd)--;
    } else
      dev->cs = -1;

    /* Refresh function pointers etc */
    memcpy(&dev->dev, &basengdevice, sizeof dev->dev);

    /* XXX: Are netgraph always synchronous ? */
    physical_SetupStack(p, dev->dev.name, PHYSICAL_FORCE_SYNCNOACF);
    return &dev->dev;
  }

  return NULL;
}

static int
ng_UpdateSet(struct fdescriptor *d, fd_set *r, fd_set *w, fd_set *e, int *n)
{
  struct physical *p = descriptor2physical(d);
  struct ngdevice *dev = device2ng(p->handler);
  int result;

  switch (p->dl->state) {
    case DATALINK_DIAL:
    case DATALINK_LOGIN:
      if (r) {
        FD_SET(dev->cs, r);
        log_Printf(LogTIMER, "%s(ctrl): fdset(r) %d\n", p->link.name, dev->cs);
        result = 1;
      } else
        result = 0;
      break;

    default:
      result = physical_doUpdateSet(d, r, w, e, n, 0);
      break;
  }

  return result;
}

static int
ng_IsSet(struct fdescriptor *d, const fd_set *fdset)
{
  struct physical *p = descriptor2physical(d);
  struct ngdevice *dev = device2ng(p->handler);
  int result;

  result = dev->cs >= 0 && FD_ISSET(dev->cs, fdset);
  result += physical_IsSet(d, fdset);

  return result;
}

static void
ng_DescriptorRead(struct fdescriptor *d, struct bundle *bundle,
                  const fd_set *fdset)
{
  struct physical *p = descriptor2physical(d);
  struct ngdevice *dev = device2ng(p->handler);

  if (dev->cs >= 0 && FD_ISSET(dev->cs, fdset))
    ng_MessageIn(p, NULL, 0);

  if (physical_IsSet(d, fdset))
    physical_DescriptorRead(d, bundle, fdset);
}

static struct device *
ng_Abandon(struct ngdevice *dev, struct physical *p)
{
  /* Abandon our node construction */
  close(dev->cs);
  close(p->fd);
  p->fd = -2;	/* Nobody else need try.. */
  free(dev);

  return NULL;
}


/*
 * Populate the ``word'' (of size ``sz'') named ``what'' from ``from''
 * ending with any character from ``sep''.  Point ``endp'' at the next
 * word.
 */

#define GETSEGMENT(what, from, sep, endp) \
	getsegment(#what, (what), sizeof(what), from, sep, endp)

static int
getsegment(const char *what, char *word, size_t sz, const char *from,
           const char *sep, const char **endp)
{
  size_t len;

  if ((len = strcspn(from, sep)) == 0) {
    log_Printf(LogWARN, "%s name should not be empty !\n", what);
    return 0;
  }

  if (len >= sz) {
    log_Printf(LogWARN, "%s name too long, max %d !\n", what, sz - 1);
    return 0;
  }

  strncpy(word, from, len);
  word[len] = '\0';

  *endp = from + len;
  *endp += strspn(*endp, sep);

  return 1;
}

struct device *
ng_Create(struct physical *p)
{
  struct sockaddr_ng ngsock;
  u_char rbuf[2048];
  struct sockaddr *sock = (struct sockaddr *)&ngsock;
  const struct hooklist *hlist;
  const struct nodeinfo *ninfo;
  const struct linkinfo *nlink;
  struct ngdevice *dev;
  struct ng_mesg *resp;
  struct ngm_mkpeer mkp;
  struct ngm_connect ngc;
  const char *devp, *endp;
  char lasthook[NG_HOOKSIZ];
  char hook[NG_HOOKSIZ];
  char nodetype[NG_TYPESIZ + NG_NODESIZ];
  char modname[NG_TYPESIZ + 3];
  char path[NG_PATHSIZ];
  char *nodename;
  int len, sz, done;
  unsigned f;

  dev = NULL;
  if (p->fd < 0 && !strncasecmp(p->name.full, NETGRAPH_PREFIX,
                                sizeof NETGRAPH_PREFIX - 1)) {
    p->fd--;				/* We own the device - change fd */

    if ((dev = malloc(sizeof *dev)) == NULL)
      return NULL;

    loadmodules(LOAD_VERBOSLY, "netgraph", "ng_socket", NULL);

    /* Create a socket node */
    if (ID0NgMkSockNode(NULL, &dev->cs, &p->fd) == -1) {
      log_Printf(LogWARN, "Cannot create netgraph socket node: %s\n",
                 strerror(errno));
      free(dev);
      p->fd = -2;
      return NULL;
    }

    devp = p->name.full + sizeof NETGRAPH_PREFIX - 1;
    *lasthook = *path = '\0';
    log_Printf(LogDEBUG, "%s: Opening netgraph device \"%s\"\n",
               p->link.name, devp);
    done = 0;

    while (*devp != '\0' && !done) {
      if (*devp != '[') {
        if (*lasthook == '\0') {
          log_Printf(LogWARN, "%s: Netgraph devices must start with"
                     " [nodetype:nodename]\n", p->link.name);
          return ng_Abandon(dev, p);
        }

        /* Get the hook name of the new node */
        if (!GETSEGMENT(hook, devp, ".[", &endp))
          return ng_Abandon(dev, p);
        log_Printf(LogDEBUG, "%s: Got hook \"%s\"\n", p->link.name, hook);
        devp = endp;
        if (*devp == '\0') {
          log_Printf(LogWARN, "%s: Netgraph device must not end with a second"
                     " hook\n", p->link.name);
          return ng_Abandon(dev, p);
        }
        if (devp[-1] != '[') {
          log_Printf(LogWARN, "%s: Expected a [nodetype:nodename] at device"
                     " pos %d\n", p->link.name, devp - p->link.name - 1);
          return ng_Abandon(dev, p);
        }
      } else {
        /* Use lasthook as the hook name */
        strcpy(hook, lasthook);
        devp++;
      }

      /* We've got ``lasthook'' and ``hook'', get the node type */
      if (!GETSEGMENT(nodetype, devp, "]", &endp))
        return ng_Abandon(dev, p);
      log_Printf(LogDEBUG, "%s: Got node \"%s\"\n", p->link.name, nodetype);

      if ((nodename = strchr(nodetype, ':')) != NULL) {
        *nodename++ = '\0';
        if (*nodename == '\0' && *nodetype == '\0') {
          log_Printf(LogWARN, "%s: Empty [nodetype:nodename] at device"
                     " pos %d\n", p->link.name, devp - p->link.name - 1);
          return ng_Abandon(dev, p);
        }
      }

      /* Ignore optional colons after nodes */
      devp = *endp == ':' ? endp + 1 : endp;
      if (*devp == '.')
        devp++;

      if (*lasthook == '\0') {
        /* This is the first node in the chain */
        if (nodename == NULL || *nodename == '\0') {
          log_Printf(LogWARN, "%s: %s: No initial device nodename\n",
                     p->link.name, devp);
          return ng_Abandon(dev, p);
        }

        if (*nodetype != '\0') {
          /* Attempt to load the module */
          snprintf(modname, sizeof modname, "ng_%s", nodetype);
          log_Printf(LogDEBUG, "%s: Attempting to load %s.ko\n",
                     p->link.name, modname);
          loadmodules(LOAD_QUIETLY, modname, NULL);
        }

        snprintf(path, sizeof path, "%s:", nodename);
        /* XXX: If we have a node type, ensure it's correct */
      } else {
        /*
         * Ask for a list of hooks attached to the previous node.  If we
         * find the one we're interested in, and if it's connected to a
         * node of the right type using the correct hook, use that.
         * If we find the hook connected to something else, fail.
         * If we find no match, mkpeer the new node.
         */
        if (*nodetype == '\0') {
          log_Printf(LogWARN, "%s: Nodetype missing at device offset %d\n",
                     p->link.name,
                     devp - p->name.full + sizeof NETGRAPH_PREFIX - 1);
          return ng_Abandon(dev, p);
        }

        /* Get a list of node hooks */
        if (NgSendMsg(dev->cs, path, NGM_GENERIC_COOKIE, NGM_LISTHOOKS,
                      NULL, 0) < 0) {
          log_Printf(LogWARN, "%s: %s Cannot send a LISTHOOOKS message: %s\n",
                     p->link.name, path, strerror(errno));
          return ng_Abandon(dev, p);
        }

        /* Get our list back */
        resp = (struct ng_mesg *)rbuf;
        if (NgRecvMsg(dev->cs, resp, sizeof rbuf, NULL) <= 0) {
          log_Printf(LogWARN, "%s: Cannot get netgraph response: %s\n",
                     p->link.name, strerror(errno));
          return ng_Abandon(dev, p);
        }

        hlist = (const struct hooklist *)resp->data;
        ninfo = &hlist->nodeinfo;

        log_Printf(LogDEBUG, "List of netgraph node ``%s'' (id %x) hooks:\n",
                   path, ninfo->id);

        /* look for a hook already attached.  */
        for (f = 0; f < ninfo->hooks; f++) {
          nlink = &hlist->link[f];

          log_Printf(LogDEBUG, "  Found %s -> %s (type %s)\n", nlink->ourhook,
                     nlink->peerhook, nlink->nodeinfo.type);

          if (!strcmp(nlink->ourhook, lasthook)) {
            if (strcmp(nlink->peerhook, hook) ||
                strcmp(nlink->nodeinfo.type, nodetype)) {
              log_Printf(LogWARN, "%s: hook %s:%s is already in use\n",
                         p->link.name, nlink->ourhook, path);
              return ng_Abandon(dev, p);
            }
            /* The node is already hooked up nicely.... reuse it */
            break;
          }
        }

        if (f == ninfo->hooks) {
          /* Attempt to load the module */
          snprintf(modname, sizeof modname, "ng_%s", nodetype);
          log_Printf(LogDEBUG, "%s: Attempting to load %s.ko\n",
                     p->link.name, modname);
          loadmodules(LOAD_QUIETLY, modname, NULL);

          /* Create (mkpeer) the new node */

          snprintf(mkp.type, sizeof mkp.type, "%s", nodetype);
          snprintf(mkp.ourhook, sizeof mkp.ourhook, "%s", lasthook);
          snprintf(mkp.peerhook, sizeof mkp.peerhook, "%s", hook);

          log_Printf(LogDEBUG, "%s: Doing MKPEER %s%s -> %s (type %s)\n",
                     p->link.name, path, mkp.ourhook, mkp.peerhook, nodetype);

          if (NgSendMsg(dev->cs, path, NGM_GENERIC_COOKIE,
                        NGM_MKPEER, &mkp, sizeof mkp) < 0) {
            log_Printf(LogWARN, "%s Cannot create %s netgraph node: %s\n",
                       path, nodetype, strerror(errno));
            return ng_Abandon(dev, p);
          }
        }
        len = strlen(path);
        snprintf(path + len, sizeof path - len, "%s%s",
                 path[len - 1] == ':' ? "" : ".", lasthook);
      }

      /* Get a list of node hooks */
      if (NgSendMsg(dev->cs, path, NGM_GENERIC_COOKIE, NGM_LISTHOOKS,
                    NULL, 0) < 0) {
        log_Printf(LogWARN, "%s: %s Cannot send a LISTHOOOKS message: %s\n",
                   p->link.name, path, strerror(errno));
        return ng_Abandon(dev, p);
      }

      /* Get our list back */
      resp = (struct ng_mesg *)rbuf;
      if (NgRecvMsg(dev->cs, resp, sizeof rbuf, NULL) <= 0) {
        log_Printf(LogWARN, "%s: Cannot get netgraph response: %s\n",
                   p->link.name, strerror(errno));
        return ng_Abandon(dev, p);
      }

      hlist = (const struct hooklist *)resp->data;
      ninfo = &hlist->nodeinfo;

      if (*lasthook != '\0' && nodename != NULL && *nodename != '\0' &&
          strcmp(ninfo->name, nodename) &&
          NgNameNode(dev->cs, path, "%s", nodename) < 0) {
        log_Printf(LogWARN, "%s: %s: Cannot name netgraph node: %s\n",
                   p->link.name, path, strerror(errno));
        return ng_Abandon(dev, p);
      }

      if (!GETSEGMENT(lasthook, devp, " \t.[", &endp))
        return ng_Abandon(dev, p);
      log_Printf(LogDEBUG, "%s: Got hook \"%s\"\n", p->link.name, lasthook);

      len = strlen(lasthook);
      done = strchr(" \t", devp[len]) ? 1 : 0;
      devp = endp;

      if (*devp != '\0') {
        if (devp[-1] == '[')
          devp--;
      } /* else should moan about devp[-1] being '[' ? */
    }

    snprintf(dev->hook, sizeof dev->hook, "%s", lasthook);

    /* Connect the node to our socket node */
    snprintf(ngc.path, sizeof ngc.path, "%s", path);
    snprintf(ngc.ourhook, sizeof ngc.ourhook, "%s", dev->hook);
    memcpy(ngc.peerhook, ngc.ourhook, sizeof ngc.peerhook);

    log_Printf(LogDEBUG, "Connecting netgraph socket .:%s -> %s.%s\n",
               ngc.ourhook, ngc.path, ngc.peerhook);
    if (NgSendMsg(dev->cs, ".:", NGM_GENERIC_COOKIE,
                  NGM_CONNECT, &ngc, sizeof ngc) < 0) {
      log_Printf(LogWARN, "Cannot connect %s and socket netgraph "
                 "nodes: %s\n", path, strerror(errno));
      return ng_Abandon(dev, p);
    }

    /* Hook things up so that we monitor dev->cs */
    p->desc.UpdateSet = ng_UpdateSet;
    p->desc.IsSet = ng_IsSet;
    p->desc.Read = ng_DescriptorRead;

    memcpy(&dev->dev, &basengdevice, sizeof dev->dev);

  } else {
    /* See if we're a netgraph socket */

    sz = sizeof ngsock;
    if (getsockname(p->fd, sock, &sz) != -1 && sock->sa_family == AF_NETGRAPH) {
      /*
       * It's a netgraph node... We can't determine hook names etc, so we
       * stay pretty impartial....
       */
      log_Printf(LogPHASE, "%s: Link is a netgraph node\n", p->link.name);

      if ((dev = malloc(sizeof *dev)) == NULL) {
        log_Printf(LogWARN, "%s: Cannot allocate an ether device: %s\n",
                   p->link.name, strerror(errno));
        return NULL;
      }

      memcpy(&dev->dev, &basengdevice, sizeof dev->dev);
      dev->cs = -1;
      *dev->hook = '\0';
    }
  }

  if (dev) {
    physical_SetupStack(p, dev->dev.name, PHYSICAL_FORCE_SYNCNOACF);
    return &dev->dev;
  }

  return NULL;
}
