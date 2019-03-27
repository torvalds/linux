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
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__FreeBSD__) && !defined(NOKLDLOAD)
#include <sys/module.h>
#endif
#include <termios.h>
#ifndef __FreeBSD__
#include <time.h>
#endif
#include <unistd.h>

#if defined(__FreeBSD__) && !defined(NOKLDLOAD)
#include "id.h"
#include "log.h"
#endif
#include "defs.h"

#define	issep(c)	((c) == '\t' || (c) == ' ')

#ifdef __NetBSD__
void
randinit()
{
  srandom((time(NULL)^getpid())+random());
}
#endif

ssize_t
fullread(int fd, void *v, size_t n)
{
  size_t got, total;

  for (total = 0; total < n; total += got)
    switch ((got = read(fd, (char *)v + total, n - total))) {
      case 0:
        return total;
      case -1:
        if (errno == EINTR)
          got = 0;
        else
          return -1;
    }
  return total;
}

static struct {
  int mode;
  const char *name;
} modes[] = {
  { PHYS_INTERACTIVE, "interactive" },
  { PHYS_AUTO, "auto" },
  { PHYS_DIRECT, "direct" },
  { PHYS_DEDICATED, "dedicated" },
  { PHYS_DDIAL, "ddial" },
  { PHYS_BACKGROUND, "background" },
  { PHYS_FOREGROUND, "foreground" },
  { PHYS_ALL, "*" },
  { 0, 0 }
};

const char *
mode2Nam(int mode)
{
  int m;

  for (m = 0; modes[m].mode; m++)
    if (modes[m].mode == mode)
      return modes[m].name;

  return "unknown";
}

int
Nam2mode(const char *name)
{
  int m, got, len;

  len = strlen(name);
  got = -1;
  for (m = 0; modes[m].mode; m++)
    if (!strncasecmp(name, modes[m].name, len)) {
      if (modes[m].name[len] == '\0')
	return modes[m].mode;
      if (got != -1)
        return 0;
      got = m;
    }

  return got == -1 ? 0 : modes[got].mode;
}

struct in_addr
GetIpAddr(const char *cp)
{
  struct in_addr ipaddr;

  if (!strcasecmp(cp, "default"))
    ipaddr.s_addr = INADDR_ANY;
  else if (inet_aton(cp, &ipaddr) == 0) {
    const char *ptr;

    /* Any illegal characters ? */
    for (ptr = cp; *ptr != '\0'; ptr++)
      if (!isalnum(*ptr) && strchr("-.", *ptr) == NULL)
        break;

    if (*ptr == '\0') {
      struct hostent *hp;

      hp = gethostbyname(cp);
      if (hp && hp->h_addrtype == AF_INET)
        memcpy(&ipaddr, hp->h_addr, hp->h_length);
      else
        ipaddr.s_addr = INADDR_NONE;
    } else
      ipaddr.s_addr = INADDR_NONE;
  }

  return ipaddr;
}

static const struct speeds {
  unsigned nspeed;
  speed_t speed;
} speeds[] = {
#ifdef B50
  { 50, B50, },
#endif
#ifdef B75
  { 75, B75, },
#endif
#ifdef B110
  { 110, B110, },
#endif
#ifdef B134
  { 134, B134, },
#endif
#ifdef B150
  { 150, B150, },
#endif
#ifdef B200
  { 200, B200, },
#endif
#ifdef B300
  { 300, B300, },
#endif
#ifdef B600
  { 600, B600, },
#endif
#ifdef B1200
  { 1200, B1200, },
#endif
#ifdef B1800
  { 1800, B1800, },
#endif
#ifdef B2400
  { 2400, B2400, },
#endif
#ifdef B4800
  { 4800, B4800, },
#endif
#ifdef B9600
  { 9600, B9600, },
#endif
#ifdef B19200
  { 19200, B19200, },
#endif
#ifdef B38400
  { 38400, B38400, },
#endif
#ifndef _POSIX_SOURCE
#ifdef B7200
  { 7200, B7200, },
#endif
#ifdef B14400
  { 14400, B14400, },
#endif
#ifdef B28800
  { 28800, B28800, },
#endif
#ifdef B57600
  { 57600, B57600, },
#endif
#ifdef B76800
  { 76800, B76800, },
#endif
#ifdef B115200
  { 115200, B115200, },
#endif
#ifdef B230400
  { 230400, B230400, },
#endif
#ifdef B460800
  { 460800, B460800, },
#endif
#ifdef B921600
  { 921600, B921600, },
#endif
#ifdef EXTA
  { 19200, EXTA, },
#endif
#ifdef EXTB
  { 38400, EXTB, },
#endif
#endif				/* _POSIX_SOURCE */
  { 0, 0 }
};

unsigned
SpeedToUnsigned(speed_t speed)
{
  const struct speeds *sp;

  for (sp = speeds; sp->nspeed; sp++) {
    if (sp->speed == speed) {
      return sp->nspeed;
    }
  }
  return 0;
}

speed_t
UnsignedToSpeed(unsigned nspeed)
{
  const struct speeds *sp;

  for (sp = speeds; sp->nspeed; sp++) {
    if (sp->nspeed == nspeed) {
      return sp->speed;
    }
  }
  return B0;
}

char *
findblank(char *p, int flags)
{
  int instring;

  instring = 0;
  while (*p) {
    if (*p == '\\') {
      if (flags & PARSE_REDUCE) {
        memmove(p, p + 1, strlen(p));
        if (!*p)
          break;
      } else
        p++;
    } else if (*p == '"') {
      memmove(p, p + 1, strlen(p));
      instring = !instring;
      continue;
    } else if (!instring && (issep(*p) ||
                             (*p == '#' && !(flags & PARSE_NOHASH))))
      return p;
    p++;
  }

  return instring ? NULL : p;
}

int
MakeArgs(char *script, char **pvect, int maxargs, int flags)
{
  int nargs;

  nargs = 0;
  while (*script) {
    script += strspn(script, " \t");
    if (*script == '#' && !(flags & PARSE_NOHASH)) {
      *script = '\0';
      break;
    }
    if (*script) {
      if (nargs >= maxargs - 1)
        break;
      *pvect++ = script;
      nargs++;
      script = findblank(script, flags);
      if (script == NULL)
        return -1;
      else if (!(flags & PARSE_NOHASH) && *script == '#')
        *script = '\0';
      else if (*script)
        *script++ = '\0';
    }
  }
  *pvect = NULL;
  return nargs;
}

const char *
NumStr(long val, char *buf, size_t sz)
{
  static char result[23];		/* handles 64 bit numbers */

  if (buf == NULL || sz == 0) {
    buf = result;
    sz = sizeof result;
  }
  snprintf(buf, sz, "<%ld>", val);
  return buf;
}

const char *
HexStr(long val, char *buf, size_t sz)
{
  static char result[21];		/* handles 64 bit numbers */

  if (buf == NULL || sz == 0) {
    buf = result;
    sz = sizeof result;
  }
  snprintf(buf, sz, "<0x%lx>", val);
  return buf;
}

const char *
ex_desc(int ex)
{
  static char num[12];		/* Used immediately if returned */
  static const char * const desc[] = {
    "normal", "start", "sock", "modem", "dial", "dead", "done",
    "reboot", "errdead", "hangup", "term", "nodial", "nologin",
    "redial", "reconnect"
  };

  if (ex >= 0 && ex < (int)(sizeof desc / sizeof *desc))
    return desc[ex];
  snprintf(num, sizeof num, "%d", ex);
  return num;
}

void
SetTitle(const char *title)
{
  if (title == NULL)
    setproctitle(NULL);
  else if (title[0] == '-' && title[1] != '\0')
    setproctitle("-%s", title + 1);
  else
    setproctitle("%s", title);
}

fd_set *
mkfdset()
{
  return (fd_set *)malloc(howmany(getdtablesize(), NFDBITS) * sizeof (fd_mask));
}

void
zerofdset(fd_set *s)
{
  memset(s, '\0', howmany(getdtablesize(), NFDBITS) * sizeof (fd_mask));
}

void
Concatinate(char *buf, size_t sz, int argc, const char *const *argv)
{
  int i, n;
  unsigned pos;

  *buf = '\0';
  for (pos = i = 0; i < argc; i++) {
    n = snprintf(buf + pos, sz - pos, "%s%s", i ? " " : "", argv[i]);
    if (n < 0) {
      buf[pos] = '\0';
      break;
    }
    if ((pos += n) >= sz)
      break;
  }
}

#if defined(__FreeBSD__) && !defined(NOKLDLOAD)
int
loadmodules(int how, const char *module, ...)
{
  int loaded = 0;
  va_list ap;

  va_start(ap, module);
  while (module != NULL) {
    if (modfind(module) == -1) {
      if (ID0kldload(module) == -1) {
        if (how == LOAD_VERBOSLY)
          log_Printf(LogWARN, "%s: Cannot load module\n", module);
      } else
        loaded++;
    }
    module = va_arg(ap, const char *);
  }
  va_end(ap);
  return loaded;
}
#else
int
loadmodules(int how __unused, const char *module __unused, ...)
{
  return 0;
}
#endif
