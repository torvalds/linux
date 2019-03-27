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

#include <sys/socket.h>		/* For IFF_ defines */
#ifndef __FreeBSD__
#include <net/if.h>		/* For IFF_ defines */
#endif
#include <net/route.h>
#include <netinet/in.h>
#include <net/if_types.h>
#include <net/if_tun.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/un.h>

#include <errno.h>
#include <string.h>
#if defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/ioctl.h>
#endif
#include <stdio.h>
#include <termios.h>
#ifdef __NetBSD__
#include <unistd.h>
#endif

#include "layer.h"
#include "mbuf.h"
#include "log.h"
#include "id.h"
#include "timer.h"
#include "lqr.h"
#include "hdlc.h"
#include "defs.h"
#include "fsm.h"
#include "throughput.h"
#include "iplist.h"
#include "slcompress.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "filter.h"
#include "descriptor.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "mp.h"
#include "iface.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "tun.h"

void
tun_configure(struct bundle *bundle)
{
#ifdef __NetBSD__
  struct ifreq ifr;
  int s;

  s = socket(PF_INET, SOCK_DGRAM, 0);

  if (s < 0) {
    log_Printf(LogERROR, "tun_configure: socket(): %s\n", strerror(errno));
    return;
  }

  sprintf(ifr.ifr_name, "tun%d", bundle->unit);
  ifr.ifr_mtu = bundle->iface->mtu;
  if (ioctl(s, SIOCSIFMTU, &ifr) < 0)
      log_Printf(LogERROR, "tun_configure: ioctl(SIOCSIFMTU): %s\n",
             strerror(errno));

  close(s);
#else
  struct tuninfo info;

  memset(&info, '\0', sizeof info);
  info.type = IFT_PPP;
  info.mtu = bundle->iface->mtu;

  info.baudrate = bundle->bandwidth;
#ifdef __OpenBSD__
  info.flags = IFF_UP|IFF_POINTOPOINT|IFF_MULTICAST;
#endif
  if (ID0ioctl(bundle->dev.fd, TUNSIFINFO, &info) < 0)
    log_Printf(LogERROR, "tun_configure: ioctl(TUNSIFINFO): %s\n",
	      strerror(errno));
#endif
}
