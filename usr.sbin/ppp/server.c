/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Brian Somers <brian@Awfulhak.org>
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
#include <netinet/in.h>
#include <sys/un.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "log.h"
#include "descriptor.h"
#include "server.h"
#include "prompt.h"
#include "ncpaddr.h"
#include "probe.h"

static int
server_UpdateSet(struct fdescriptor *d, fd_set *r, fd_set *w, fd_set *e, int *n)
{
  struct server *s = descriptor2server(d);
  struct prompt *p;
  int sets;

  sets = 0;
  if (r && s->fd >= 0) {
    if (*n < s->fd + 1)
      *n = s->fd + 1;
    FD_SET(s->fd, r);
    log_Printf(LogTIMER, "server: fdset(r) %d\n", s->fd);
    sets++;
  }

  for (p = log_PromptList(); p; p = p->next)
    sets += descriptor_UpdateSet(&p->desc, r, w, e, n);

  return sets;
}

static int
server_IsSet(struct fdescriptor *d, const fd_set *fdset)
{
  struct server *s = descriptor2server(d);
  struct prompt *p;

  if (s->fd >= 0 && FD_ISSET(s->fd, fdset))
    return 1;

  for (p = log_PromptList(); p; p = p->next)
    if (descriptor_IsSet(&p->desc, fdset))
      return 1;

  return 0;
}

static void
server_Read(struct fdescriptor *d, struct bundle *bundle, const fd_set *fdset)
{
  struct server *s = descriptor2server(d);
  struct sockaddr_storage ss;
  struct sockaddr *sa = (struct sockaddr *)&ss;
  struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
#ifndef NOINET6
  struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
#endif
  int ssize = sizeof ss, wfd;
  struct prompt *p;
  struct ncpaddr addr;

  if (s->fd >= 0 && FD_ISSET(s->fd, fdset)) {
    wfd = accept(s->fd, sa, &ssize);
    if (wfd < 0)
      log_Printf(LogERROR, "server_Read: accept(): %s\n", strerror(errno));
    else if (sa->sa_len == 0) {
      close(wfd);
      wfd = -1;
    }
  } else
    wfd = -1;

  if (wfd >= 0)
    switch (sa->sa_family) {
      case AF_LOCAL:
        log_Printf(LogPHASE, "Connected to local client.\n");
        break;

      case AF_INET:
        ncpaddr_setsa(&addr, sa);
        if (ntohs(sin->sin_port) < 1024) {
          log_Printf(LogALERT, "Rejected client connection from %s:%u"
                    "(invalid port number) !\n",
                    ncpaddr_ntoa(&addr), ntohs(sin->sin_port));
          close(wfd);
          wfd = -1;
          break;
        }
        log_Printf(LogPHASE, "Connected to client from %s:%u\n",
                  ncpaddr_ntoa(&addr), ntohs(sin->sin_port));
        break;

#ifndef NOINET6
      case AF_INET6:
        ncpaddr_setsa(&addr, sa);
        if (ntohs(sin6->sin6_port) < 1024) {
          log_Printf(LogALERT, "Rejected client connection from %s:%u"
                    "(invalid port number) !\n",
                    ncpaddr_ntoa(&addr), ntohs(sin6->sin6_port));
          close(wfd);
          wfd = -1;
          break;
        }
        log_Printf(LogPHASE, "Connected to client from %s:%u\n",
                  ncpaddr_ntoa(&addr), ntohs(sin6->sin6_port));
        break;
#endif

      default:
        write(wfd, "Unrecognised access !\n", 22);
        close(wfd);
        wfd = -1;
        break;
    }

  if (wfd >= 0) {
    if ((p = prompt_Create(s, bundle, wfd)) == NULL) {
      write(wfd, "Connection refused.\n", 20);
      close(wfd);
    } else {
      switch (sa->sa_family) {
        case AF_LOCAL:
          p->src.type = "local";
          strncpy(p->src.from, s->cfg.sockname, sizeof p->src.from - 1);
          p->src.from[sizeof p->src.from - 1] = '\0';
          break;
        case AF_INET:
          p->src.type = "ip";
          snprintf(p->src.from, sizeof p->src.from, "%s:%u",
                   ncpaddr_ntoa(&addr), ntohs(sin->sin_port));
          break;
#ifndef NOINET6
        case AF_INET6:
          p->src.type = "ip6";
          snprintf(p->src.from, sizeof p->src.from, "%s:%u",
                   ncpaddr_ntoa(&addr), ntohs(sin6->sin6_port));
          break;
#endif
      }
      prompt_TtyCommandMode(p);
      prompt_Required(p);
    }
  }

  log_PromptListChanged = 0;
  for (p = log_PromptList(); p; p = p->next)
    if (descriptor_IsSet(&p->desc, fdset)) {
      descriptor_Read(&p->desc, bundle, fdset);
      if (log_PromptListChanged)
        break;
    }
}

static int
server_Write(struct fdescriptor *d __unused, struct bundle *bundle __unused,
	     const fd_set *fdset __unused)
{
  /* We never want to write here ! */
  log_Printf(LogALERT, "server_Write: Internal error: Bad call !\n");
  return 0;
}

struct server server = {
  {
    SERVER_DESCRIPTOR,
    server_UpdateSet,
    server_IsSet,
    server_Read,
    server_Write
  },
  -1,
  { "", "", 0, 0 }
};

enum server_stat
server_Reopen(struct bundle *bundle)
{
  char name[sizeof server.cfg.sockname];
  struct stat st;
  u_short port;
  mode_t mask;
  enum server_stat ret;

  if (server.cfg.sockname[0] != '\0') {
    strcpy(name, server.cfg.sockname);
    mask = server.cfg.mask;
    server_Close(bundle);
    if (server.cfg.sockname[0] != '\0' && stat(server.cfg.sockname, &st) == 0)
      if (!(st.st_mode & S_IFSOCK) || unlink(server.cfg.sockname) != 0)
        return SERVER_FAILED;
    ret = server_LocalOpen(bundle, name, mask);
  } else if (server.cfg.port != 0) {
    port = server.cfg.port;
    server_Close(bundle);
    ret = server_TcpOpen(bundle, port);
  } else
    ret = SERVER_UNSET;

  return ret;
}

enum server_stat
server_LocalOpen(struct bundle *bundle, const char *name, mode_t mask)
{
  struct sockaddr_un ifsun;
  mode_t oldmask;
  int s;

  oldmask = (mode_t)-1;		/* Silence compiler */

  if (server.cfg.sockname[0] != '\0' && !strcmp(server.cfg.sockname, name))
    server_Close(bundle);

  memset(&ifsun, '\0', sizeof ifsun);
  ifsun.sun_len = strlen(name);
  if (ifsun.sun_len > sizeof ifsun.sun_path - 1) {
    log_Printf(LogERROR, "Local: %s: Path too long\n", name);
    return SERVER_INVALID;
  }
  ifsun.sun_family = AF_LOCAL;
  strcpy(ifsun.sun_path, name);

  s = socket(PF_LOCAL, SOCK_STREAM, 0);
  if (s < 0) {
    log_Printf(LogERROR, "Local: socket: %s\n", strerror(errno));
    goto failed;
  }
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &s, sizeof s);
  if (mask != (mode_t)-1)
    oldmask = umask(mask);
  if (bind(s, (struct sockaddr *)&ifsun, sizeof ifsun) < 0) {
    if (mask != (mode_t)-1)
      umask(oldmask);
    log_Printf(LogWARN, "Local: bind: %s\n", strerror(errno));
    close(s);
    goto failed;
  }
  if (mask != (mode_t)-1)
    umask(oldmask);
  if (listen(s, 5) != 0) {
    log_Printf(LogERROR, "Local: Unable to listen to socket -"
               " BUNDLE overload?\n");
    close(s);
    unlink(name);
    goto failed;
  }
  server_Close(bundle);
  server.fd = s;
  server.cfg.port = 0;
  strncpy(server.cfg.sockname, ifsun.sun_path, sizeof server.cfg.sockname - 1);
  server.cfg.sockname[sizeof server.cfg.sockname - 1] = '\0';
  server.cfg.mask = mask;
  log_Printf(LogPHASE, "Listening at local socket %s.\n", name);

  return SERVER_OK;

failed:
  if (server.fd == -1) {
    server.fd = -1;
    server.cfg.port = 0;
    strncpy(server.cfg.sockname, ifsun.sun_path,
            sizeof server.cfg.sockname - 1);
    server.cfg.sockname[sizeof server.cfg.sockname - 1] = '\0';
    server.cfg.mask = mask;
  }
  return SERVER_FAILED;
}

enum server_stat
server_TcpOpen(struct bundle *bundle, u_short port)
{
  struct sockaddr_storage ss;
  struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
#ifndef NOINET6
  struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
#endif
  int s, sz;

  if (server.cfg.port == port)
    server_Close(bundle);

  if (port == 0)
    return SERVER_INVALID;

  memset(&ss, '\0', sizeof ss);
#ifndef NOINET6
  if (probe.ipv6_available) {
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(port);
    sin6->sin6_len = (u_int8_t)sizeof ss;
    sz = sizeof *sin6;
    s = socket(PF_INET6, SOCK_STREAM, 0);
  } else
#endif
  {
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    sin->sin_len = (u_int8_t)sizeof ss;
    sin->sin_addr.s_addr = INADDR_ANY;
    sz = sizeof *sin;
    s = socket(PF_INET, SOCK_STREAM, 0);
  }

  if (s < 0) {
    log_Printf(LogERROR, "Tcp: socket: %s\n", strerror(errno));
    goto failed;
  }

#ifndef NOINET6
  if (probe.ipv6_available) {
    int off = 0;
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&off, sizeof(off));
  }
#endif

  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &s, sizeof s);
  if (bind(s, (struct sockaddr *)&ss, sz) < 0) {
    log_Printf(LogWARN, "Tcp: bind: %s\n", strerror(errno));
    close(s);
    goto failed;
  }
  if (listen(s, 5) != 0) {
    log_Printf(LogERROR, "Tcp: Unable to listen to socket: %s\n",
               strerror(errno));
    close(s);
    goto failed;
  }
  server_Close(bundle);
  server.fd = s;
  server.cfg.port = port;
  *server.cfg.sockname = '\0';
  server.cfg.mask = 0;
  log_Printf(LogPHASE, "Listening at port %d.\n", port);
  return SERVER_OK;

failed:
  if (server.fd == -1) {
    server.fd = -1;
    server.cfg.port = port;
    *server.cfg.sockname = '\0';
    server.cfg.mask = 0;
  }
  return SERVER_FAILED;
}

int
server_Close(struct bundle *bundle __unused)
{
  if (server.fd >= 0) {
    if (*server.cfg.sockname != '\0') {
      struct sockaddr_un un;
      int sz = sizeof un;

      if (getsockname(server.fd, (struct sockaddr *)&un, &sz) == 0 &&
          un.sun_family == AF_LOCAL && sz == sizeof un)
        unlink(un.sun_path);
    }
    close(server.fd);
    server.fd = -1;
    /* Drop associated prompts */
    log_DestroyPrompts(&server);

    return 1;
  }

  return 0;
}

int
server_Clear(struct bundle *bundle)
{
  int ret;

  ret = server_Close(bundle);

  server.fd = -1;
  server.cfg.port = 0;
  *server.cfg.sockname = '\0';
  server.cfg.mask = 0;

  return ret;
}
