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

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include "log.h"
#include "defs.h"
#include "iplist.h"

static int
do_inet_aton(const char *start, const char *end, struct in_addr *ip)
{
  char ipstr[16];

  if (end - start > 15) {
    log_Printf(LogWARN, "%.*s: Invalid IP address\n", (int)(end-start), start);
    return 0;
  }
  strncpy(ipstr, start, end-start);
  ipstr[end-start] = '\0';
  return inet_aton(ipstr, ip);
}

static void
iplist_first(struct iplist *list)
{
  list->cur.pos = -1;
}

static int
iplist_setrange(struct iplist *list, char *range)
{
  char *ptr, *to;

  if ((ptr = strpbrk(range, ",-")) == NULL) {
    if (!inet_aton(range, &list->cur.ip))
      return 0;
    list->cur.lstart = ntohl(list->cur.ip.s_addr);
    list->cur.nItems = 1;
  } else {
    if (!do_inet_aton(range, ptr, &list->cur.ip))
      return 0;
    if (*ptr == ',') {
      list->cur.lstart = ntohl(list->cur.ip.s_addr);
      list->cur.nItems = 1;
    } else {
      struct in_addr endip;

      to = ptr+1;
      if ((ptr = strpbrk(to, ",-")) == NULL)
        ptr = to + strlen(to);
      if (*to == '-')
        return 0;
      if (!do_inet_aton(to, ptr, &endip))
        return 0;
      list->cur.lstart = ntohl(list->cur.ip.s_addr);
      list->cur.nItems = ntohl(endip.s_addr) - list->cur.lstart + 1;
      if (list->cur.nItems < 1)
        return 0;
    }
  }
  list->cur.srcitem = 0;
  list->cur.srcptr = range;
  return 1;
}

static int
iplist_nextrange(struct iplist *list)
{
  char *ptr, *to, *end;

  ptr = list->cur.srcptr;
  if (ptr != NULL && (ptr = strchr(ptr, ',')) != NULL)
    ptr++;
  else
    ptr = list->src;

  while (*ptr != '\0' && !iplist_setrange(list, ptr)) {
    if ((end = strchr(ptr, ',')) == NULL)
      end = ptr + strlen(ptr);
    if (end == ptr)
      return 0;
    log_Printf(LogWARN, "%.*s: Invalid IP range (skipping)\n",
               (int)(end - ptr), ptr);
    to = ptr;
    do
      *to = *end++;
    while (*to++ != '\0');
    if (*ptr == '\0')
      ptr = list->src;
  }

  return 1;
}

struct in_addr
iplist_next(struct iplist *list)
{
  if (list->cur.pos == -1) {
    list->cur.srcptr = NULL;
    if (!iplist_nextrange(list)) {
      list->cur.ip.s_addr = INADDR_ANY;
      return list->cur.ip;
    }
  } else if (++list->cur.srcitem == list->cur.nItems) {
    if (!iplist_nextrange(list)) {
      list->cur.ip.s_addr = INADDR_ANY;
      list->cur.pos = -1;
      return list->cur.ip;
    }
  } else
    list->cur.ip.s_addr = htonl(list->cur.lstart + list->cur.srcitem);
  list->cur.pos++;

  return list->cur.ip;
}

int
iplist_setsrc(struct iplist *list, const char *src)
{
  strncpy(list->src, src, sizeof list->src - 1);
  list->src[sizeof list->src - 1] = '\0';
  list->cur.srcptr = list->src;
  do {
    if (iplist_nextrange(list))
      list->nItems += list->cur.nItems;
    else
      return 0;
  } while (list->cur.srcptr != list->src);
  return 1;
}

void
iplist_reset(struct iplist *list)
{
  list->src[0] = '\0';
  list->nItems = 0;
  list->cur.pos = -1;
}

struct in_addr
iplist_setcurpos(struct iplist *list, long pos)
{
  if (pos < 0 || (unsigned)pos >= list->nItems) {
    list->cur.pos = -1;
    list->cur.ip.s_addr = INADDR_ANY;
    return list->cur.ip;
  }

  list->cur.srcptr = NULL;
  list->cur.pos = 0;
  while (1) {
    iplist_nextrange(list);
    if (pos < (int)list->cur.nItems) {
      if (pos) {
        list->cur.srcitem = pos;
        list->cur.pos += pos;
        list->cur.ip.s_addr = htonl(list->cur.lstart + list->cur.srcitem);
      }
      break;
    }
    pos -= list->cur.nItems;
    list->cur.pos += list->cur.nItems;
  }

  return list->cur.ip;
}

struct in_addr
iplist_setrandpos(struct iplist *list)
{
  randinit();
  return iplist_setcurpos(list, random() % list->nItems);
}

int
iplist_ip2pos(struct iplist *list, struct in_addr ip)
{
  struct iplist_cur cur;
  u_long f;
  int result;

  result = -1;
  memcpy(&cur, &list->cur, sizeof cur);

  for (iplist_first(list), f = 0; f < list->nItems; f++)
    if (iplist_next(list).s_addr == ip.s_addr) {
      result = list->cur.pos;
      break;
    }

  memcpy(&list->cur, &cur, sizeof list->cur);
  return result;
}
