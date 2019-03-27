/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Brian Somers <brian@Awfulhak.org>
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

#include <sys/time.h>
#include <sys/socket.h>

#include <stdio.h>
#include <unistd.h>

#include "probe.h"
#include "log.h"
#include "id.h"

struct probe probe;

/* Does select() alter the passed time value ? */
static int
select_changes_time(void)
{
  struct timeval t;

  t.tv_sec = 0;
  t.tv_usec = 100000;
  select(0, NULL, NULL, NULL, &t);
  return t.tv_usec != 100000;
}

#ifndef NOINET6
static int
ipv6_available(void)
{
  int s;

  if ((s = ID0socket(PF_INET6, SOCK_DGRAM, 0)) == -1)
    return 0;

  close(s);
  return 1;
}
#endif

void
probe_Init()
{
  probe.select_changes_time = select_changes_time() ? 1 : 0;
  log_Printf(LogDEBUG, "Select changes time: %s\n",
             probe.select_changes_time ? "yes" : "no");
#ifndef NOINET6
  probe.ipv6_available = ipv6_available() ? 1 : 0;
  log_Printf(LogDEBUG, "IPv6 available: %s\n",
             probe.ipv6_available ? "yes" : "no");
#endif
}
