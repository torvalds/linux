/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Brian Somers <brian@Awfulhak.org>
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
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <net/route.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <sys/un.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <ifaddrs.h>

#include "layer.h"
#include "defs.h"
#include "mbuf.h"
#include "timer.h"
#include "fsm.h"
#include "iplist.h"
#include "throughput.h"
#include "slcompress.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "ncpaddr.h"
#include "ip.h"
#include "ipcp.h"
#include "ipv6cp.h"
#include "filter.h"
#include "descriptor.h"
#include "ccp.h"
#include "link.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ncp.h"
#include "bundle.h"
#include "route.h"
#include "iface.h"
#include "log.h"
#include "proto.h"
#include "command.h"
#include "prompt.h"
#include "async.h"
#include "physical.h"
#include "probe.h"
#include "systems.h"


#ifndef NOINET6
#define IN6ADDR_LINKLOCAL_MCAST_INIT \
	{{{ 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }}}
static const struct in6_addr in6addr_linklocal_mcast =
	IN6ADDR_LINKLOCAL_MCAST_INIT;

static int ipv6cp_LayerUp(struct fsm *);
static void ipv6cp_LayerDown(struct fsm *);
static void ipv6cp_LayerStart(struct fsm *);
static void ipv6cp_LayerFinish(struct fsm *);
static void ipv6cp_InitRestartCounter(struct fsm *, int);
static void ipv6cp_SendConfigReq(struct fsm *);
static void ipv6cp_SentTerminateReq(struct fsm *);
static void ipv6cp_SendTerminateAck(struct fsm *, u_char);
static void ipv6cp_DecodeConfig(struct fsm *, u_char *, u_char *, int,
                                struct fsm_decode *);

static struct fsm_callbacks ipv6cp_Callbacks = {
  ipv6cp_LayerUp,
  ipv6cp_LayerDown,
  ipv6cp_LayerStart,
  ipv6cp_LayerFinish,
  ipv6cp_InitRestartCounter,
  ipv6cp_SendConfigReq,
  ipv6cp_SentTerminateReq,
  ipv6cp_SendTerminateAck,
  ipv6cp_DecodeConfig,
  fsm_NullRecvResetReq,
  fsm_NullRecvResetAck
};

static void
SetInterfaceID(u_char *ifid, int userandom)
{
  struct ifaddrs *ifa, *ifap = NULL;
  struct sockaddr_dl *sdl;
  const u_long i32_max = 0xffffffff;
  u_long r1, r2;

  /* configure an interface ID based on Section 4.1 of RFC 2472 */
  memset(ifid, 0, IPV6CP_IFIDLEN);

  /*
   * 1) If an IEEE global identifier (EUI-48 or EUI-64) is
   * available anywhere on the node, it should be used to construct
   * the tentative Interface-Identifier due to its uniqueness
   * properties.
   */
  if (userandom)
    goto randomid;
  if (getifaddrs(&ifap) < 0)
    goto randomid;
	
  for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
    char *cp;

    if (ifa->ifa_addr->sa_family != AF_LINK)
      continue;

    sdl = (struct sockaddr_dl *)ifa->ifa_addr;
    if (sdl->sdl_alen < 6)
      continue;
    /* we're only interested in IEEE hardware addresses */
    switch(sdl->sdl_type) {
    case IFT_ETHER:
    case IFT_FDDI:
    case IFT_L2VLAN:
      /* XXX need more cases? */
      break;
    default:
      continue;
    }

    cp = (char *)(sdl->sdl_data + sdl->sdl_nlen);
    ifid[0] = cp[0];
    ifid[0] ^= 0x02; /* reverse the u/l bit*/
    ifid[1] = cp[1];
    ifid[2] = cp[2];
    ifid[3] = 0xff;
    ifid[4] = 0xfe;
    ifid[5] = cp[3];
    ifid[6] = cp[4];
    ifid[7] = cp[5];

    freeifaddrs(ifap);
    return;
  }

  freeifaddrs(ifap);

  /*
   * 2) If an IEEE global identifier is not available a different source
   * of uniqueness should be used.
   * XXX: we skip this case.
   */

  /*
   * 3) If a good source of uniqueness cannot be found, it is
   * recommended that a random number be generated.  In this case the
   * "u" bit of the interface identifier MUST be set to zero (0).
   */
 randomid:
  randinit();
  r1 = (((u_long)random()) % i32_max) + 1;
  r2 = (((u_long)random()) % i32_max) + 1;
  memcpy(ifid, &r1, sizeof(r1));
  memcpy(ifid + 4, &r2, sizeof(r2));
  ifid[0] &= 0xfd;
  return;
}

static int
ipcp_SetIPv6address(struct ipv6cp *ipv6cp, u_char *myifid, u_char *hisifid)
{
  struct bundle *bundle = ipv6cp->fsm.bundle;
  struct in6_addr myaddr, hisaddr;
  struct ncprange myrange, range;
  struct ncpaddr addr;
  struct sockaddr_storage ssdst, ssgw, ssmask;
  struct sockaddr *sadst, *sagw, *samask;

  sadst = (struct sockaddr *)&ssdst;
  sagw = (struct sockaddr *)&ssgw;
  samask = (struct sockaddr *)&ssmask;

  memset(&myaddr, '\0', sizeof myaddr);
  memset(&hisaddr, '\0', sizeof hisaddr);

  myaddr.s6_addr[0] = 0xfe;
  myaddr.s6_addr[1] = 0x80;
  memcpy(&myaddr.s6_addr[8], myifid, IPV6CP_IFIDLEN);
#if 0
  myaddr.s6_addr[8] |= 0x02;	/* set 'universal' bit */
#endif

  hisaddr.s6_addr[0] = 0xfe;
  hisaddr.s6_addr[1] = 0x80;
  memcpy(&hisaddr.s6_addr[8], hisifid, IPV6CP_IFIDLEN);
#if 0
  hisaddr.s6_addr[8] |= 0x02;	/* set 'universal' bit */
#endif

  ncpaddr_setip6(&ipv6cp->myaddr, &myaddr);
  ncpaddr_setip6(&ipv6cp->hisaddr, &hisaddr);
  ncprange_set(&myrange, &ipv6cp->myaddr, 64);

  if (!iface_Add(bundle->iface, &bundle->ncp, &myrange, &ipv6cp->hisaddr,
                 IFACE_ADD_FIRST|IFACE_FORCE_ADD|IFACE_SYSTEM))
    return 0;

  if (!Enabled(bundle, OPT_IFACEALIAS))
    iface_Clear(bundle->iface, &bundle->ncp, AF_INET6,
                IFACE_CLEAR_ALIASES|IFACE_SYSTEM);

  ncpaddr_setip6(&addr, &in6addr_linklocal_mcast);
  ncprange_set(&range, &addr, 32);
  rt_Set(bundle, RTM_ADD, &range, &ipv6cp->myaddr, 1, 0);

  if (bundle->ncp.cfg.sendpipe > 0 || bundle->ncp.cfg.recvpipe > 0) {
    ncprange_getsa(&myrange, &ssgw, &ssmask);
    if (ncpaddr_isset(&ipv6cp->hisaddr))
      ncpaddr_getsa(&ipv6cp->hisaddr, &ssdst);
    else
      sadst = NULL;
    rt_Update(bundle, sadst, sagw, samask, NULL, NULL);
  }

  if (Enabled(bundle, OPT_SROUTES))
    route_Change(bundle, bundle->ncp.route, &ipv6cp->myaddr, &ipv6cp->hisaddr);

#ifndef NORADIUS
  if (bundle->radius.valid)
    route_Change(bundle, bundle->radius.ipv6routes, &ipv6cp->myaddr,
                 &ipv6cp->hisaddr);
#endif

  return 1;	/* Ok */
}

void
ipv6cp_Init(struct ipv6cp *ipv6cp, struct bundle *bundle, struct link *l,
                 const struct fsm_parent *parent)
{
  static const char * const timer_names[] =
    {"IPV6CP restart", "IPV6CP openmode", "IPV6CP stopped"};
  int n;

  fsm_Init(&ipv6cp->fsm, "IPV6CP", PROTO_IPV6CP, 1, IPV6CP_MAXCODE, LogIPV6CP,
           bundle, l, parent, &ipv6cp_Callbacks, timer_names);

  ipv6cp->cfg.fsm.timeout = DEF_FSMRETRY;
  ipv6cp->cfg.fsm.maxreq = DEF_FSMTRIES;
  ipv6cp->cfg.fsm.maxtrm = DEF_FSMTRIES;

  SetInterfaceID(ipv6cp->my_ifid, 0);
  do {
    SetInterfaceID(ipv6cp->his_ifid, 1);
  } while (memcmp(ipv6cp->his_ifid, ipv6cp->my_ifid, IPV6CP_IFIDLEN) == 0);

  if (probe.ipv6_available) {
    n = 100;
    while (n &&
           !ipcp_SetIPv6address(ipv6cp, ipv6cp->my_ifid, ipv6cp->his_ifid)) {
      do {
	n--;
    	SetInterfaceID(ipv6cp->my_ifid, 1);
      } while (n
	&& memcmp(ipv6cp->his_ifid, ipv6cp->my_ifid, IPV6CP_IFIDLEN) == 0);
    }
  }

  throughput_init(&ipv6cp->throughput, SAMPLE_PERIOD);
  memset(ipv6cp->Queue, '\0', sizeof ipv6cp->Queue);
  ipv6cp_Setup(ipv6cp);
}

void
ipv6cp_Destroy(struct ipv6cp *ipv6cp)
{
  throughput_destroy(&ipv6cp->throughput);
}

void
ipv6cp_Setup(struct ipv6cp *ipv6cp)
{
  ncpaddr_init(&ipv6cp->myaddr);
  ncpaddr_init(&ipv6cp->hisaddr);

  ipv6cp->his_reject = 0;
  ipv6cp->my_reject = 0;
}

void
ipv6cp_SetLink(struct ipv6cp *ipv6cp, struct link *l)
{
  ipv6cp->fsm.link = l;
}

int
ipv6cp_Show(struct cmdargs const *arg)
{
  struct ipv6cp *ipv6cp = &arg->bundle->ncp.ipv6cp;

  prompt_Printf(arg->prompt, "%s [%s]\n", ipv6cp->fsm.name,
                State2Nam(ipv6cp->fsm.state));
  if (ipv6cp->fsm.state == ST_OPENED) {
    prompt_Printf(arg->prompt, " His side:        %s\n",
                  ncpaddr_ntoa(&ipv6cp->hisaddr));
    prompt_Printf(arg->prompt, " My side:         %s\n",
                  ncpaddr_ntoa(&ipv6cp->myaddr));
    prompt_Printf(arg->prompt, " Queued packets:  %lu\n",
                  (unsigned long)ipv6cp_QueueLen(ipv6cp));
  }

  prompt_Printf(arg->prompt, "\nDefaults:\n");
  prompt_Printf(arg->prompt, "  FSM retry = %us, max %u Config"
                " REQ%s, %u Term REQ%s\n\n", ipv6cp->cfg.fsm.timeout,
                ipv6cp->cfg.fsm.maxreq, ipv6cp->cfg.fsm.maxreq == 1 ? "" : "s",
                ipv6cp->cfg.fsm.maxtrm, ipv6cp->cfg.fsm.maxtrm == 1 ? "" : "s");

  throughput_disp(&ipv6cp->throughput, arg->prompt);

  return 0;
}

struct mbuf *
ipv6cp_Input(struct bundle *bundle, struct link *l, struct mbuf *bp)
{
  /* Got PROTO_IPV6CP from link */
  m_settype(bp, MB_IPV6CPIN);
  if (bundle_Phase(bundle) == PHASE_NETWORK)
    fsm_Input(&bundle->ncp.ipv6cp.fsm, bp);
  else {
    if (bundle_Phase(bundle) < PHASE_NETWORK)
      log_Printf(LogIPV6CP, "%s: Error: Unexpected IPV6CP in phase %s"
                 " (ignored)\n", l->name, bundle_PhaseName(bundle));
    m_freem(bp);
  }
  return NULL;
}

void
ipv6cp_AddInOctets(struct ipv6cp *ipv6cp, int n)
{
  throughput_addin(&ipv6cp->throughput, n);
}

void
ipv6cp_AddOutOctets(struct ipv6cp *ipv6cp, int n)
{
  throughput_addout(&ipv6cp->throughput, n);
}

void
ipv6cp_IfaceAddrAdded(struct ipv6cp *ipv6cp __unused,
		      const struct iface_addr *addr __unused)
{
}

void
ipv6cp_IfaceAddrDeleted(struct ipv6cp *ipv6cp __unused,
			const struct iface_addr *addr __unused)
{
}

int
ipv6cp_InterfaceUp(struct ipv6cp *ipv6cp)
{
  if (!ipcp_SetIPv6address(ipv6cp, ipv6cp->my_ifid, ipv6cp->his_ifid)) {
    log_Printf(LogERROR, "ipv6cp_InterfaceUp: unable to set ipv6 address\n");
    return 0;
  }

  if (!iface_SetFlags(ipv6cp->fsm.bundle->iface->name, IFF_UP)) {
    log_Printf(LogERROR, "ipv6cp_InterfaceUp: Can't set the IFF_UP"
               " flag on %s\n", ipv6cp->fsm.bundle->iface->name);
    return 0;
  }

  return 1;
}

size_t
ipv6cp_QueueLen(struct ipv6cp *ipv6cp)
{
  struct mqueue *q;
  size_t result;

  result = 0;
  for (q = ipv6cp->Queue; q < ipv6cp->Queue + IPV6CP_QUEUES(ipv6cp); q++)
    result += q->len;

  return result;
}

int
ipv6cp_PushPacket(struct ipv6cp *ipv6cp, struct link *l)
{
  struct bundle *bundle = ipv6cp->fsm.bundle;
  struct mqueue *queue;
  struct mbuf *bp;
  int m_len;
  u_int32_t secs = 0;
  unsigned alivesecs = 0;

  if (ipv6cp->fsm.state != ST_OPENED)
    return 0;

  /*
   * If ccp is not open but is required, do nothing.
   */
  if (l->ccp.fsm.state != ST_OPENED && ccp_Required(&l->ccp)) {
    log_Printf(LogPHASE, "%s: Not transmitting... waiting for CCP\n", l->name);
    return 0;
  }

  queue = ipv6cp->Queue + IPV6CP_QUEUES(ipv6cp) - 1;
  do {
    if (queue->top) {
      bp = m_dequeue(queue);
      bp = mbuf_Read(bp, &secs, sizeof secs);
      bp = m_pullup(bp);
      m_len = m_length(bp);
      if (!FilterCheck(MBUF_CTOP(bp), AF_INET6, &bundle->filter.alive,
                       &alivesecs)) {
        if (secs == 0)
          secs = alivesecs;
        bundle_StartIdleTimer(bundle, secs);
      }
      link_PushPacket(l, bp, bundle, 0, PROTO_IPV6);
      ipv6cp_AddOutOctets(ipv6cp, m_len);
      return 1;
    }
  } while (queue-- != ipv6cp->Queue);

  return 0;
}

static int
ipv6cp_LayerUp(struct fsm *fp)
{
  /* We're now up */
  struct ipv6cp *ipv6cp = fsm2ipv6cp(fp);
  char tbuff[NCP_ASCIIBUFFERSIZE];

  log_Printf(LogIPV6CP, "%s: LayerUp.\n", fp->link->name);
  if (!ipv6cp_InterfaceUp(ipv6cp))
    return 0;

  snprintf(tbuff, sizeof tbuff, "%s", ncpaddr_ntoa(&ipv6cp->myaddr));
  log_Printf(LogIPV6CP, "myaddr %s hisaddr = %s\n",
             tbuff, ncpaddr_ntoa(&ipv6cp->hisaddr));

#ifndef NORADIUS
  radius_Account_Set_Ipv6(&fp->bundle->radacct6, ipv6cp->his_ifid);
  radius_Account(&fp->bundle->radius, &fp->bundle->radacct6,
		 fp->bundle->links, RAD_START, &ipv6cp->throughput);

  /*
   * XXX: Avoid duplicate evaluation of filterid between IPCP and
   * IPV6CP.  When IPCP is enabled and rejected, filterid is not
   * evaluated.
   */
  if (!Enabled(fp->bundle, OPT_IPCP)) {
    if (*fp->bundle->radius.cfg.file && fp->bundle->radius.filterid)
      system_Select(fp->bundle, fp->bundle->radius.filterid, LINKUPFILE,
		    NULL, NULL);
  }
#endif

  /*
   * XXX this stuff should really live in the FSM.  Our config should
   * associate executable sections in files with events.
   */
  if (system_Select(fp->bundle, tbuff, LINKUPFILE, NULL, NULL) < 0) {
    /*
     * XXX: Avoid duplicate evaluation of label between IPCP and
     * IPV6CP.  When IPCP is enabled and rejected, label is not
     * evaluated.
     */
    if (bundle_GetLabel(fp->bundle) && !Enabled(fp->bundle, OPT_IPCP)) {
      if (system_Select(fp->bundle, bundle_GetLabel(fp->bundle),
			LINKUPFILE, NULL, NULL) < 0)
	system_Select(fp->bundle, "MYADDR6", LINKUPFILE, NULL, NULL);
    } else
      system_Select(fp->bundle, "MYADDR6", LINKUPFILE, NULL, NULL);
  }

  fp->more.reqs = fp->more.naks = fp->more.rejs = ipv6cp->cfg.fsm.maxreq * 3;
  log_DisplayPrompts();

  return 1;
}

static void
ipv6cp_LayerDown(struct fsm *fp)
{
  /* About to come down */
  struct ipv6cp *ipv6cp = fsm2ipv6cp(fp);
  static int recursing;
  char addr[NCP_ASCIIBUFFERSIZE];

  if (!recursing++) {
    snprintf(addr, sizeof addr, "%s", ncpaddr_ntoa(&ipv6cp->myaddr));
    log_Printf(LogIPV6CP, "%s: LayerDown: %s\n", fp->link->name, addr);

#ifndef NORADIUS
    radius_Flush(&fp->bundle->radius);
    radius_Account(&fp->bundle->radius, &fp->bundle->radacct6,
		   fp->bundle->links, RAD_STOP, &ipv6cp->throughput);

    /*
     * XXX: Avoid duplicate evaluation of filterid between IPCP and
     * IPV6CP.  When IPCP is enabled and rejected, filterid is not
     * evaluated.
     */
    if (!Enabled(fp->bundle, OPT_IPCP)) {
      if (*fp->bundle->radius.cfg.file && fp->bundle->radius.filterid)
	system_Select(fp->bundle, fp->bundle->radius.filterid, LINKDOWNFILE,
		      NULL, NULL);
    }
#endif

    /*
     * XXX this stuff should really live in the FSM.  Our config should
     * associate executable sections in files with events.
     */
    if (system_Select(fp->bundle, addr, LINKDOWNFILE, NULL, NULL) < 0) {
      /*
       * XXX: Avoid duplicate evaluation of label between IPCP and
       * IPV6CP.  When IPCP is enabled and rejected, label is not
       * evaluated.
       */
      if (bundle_GetLabel(fp->bundle) && !Enabled(fp->bundle, OPT_IPCP)) {
	if (system_Select(fp->bundle, bundle_GetLabel(fp->bundle),
			  LINKDOWNFILE, NULL, NULL) < 0)
	  system_Select(fp->bundle, "MYADDR6", LINKDOWNFILE, NULL, NULL);
      } else
	system_Select(fp->bundle, "MYADDR6", LINKDOWNFILE, NULL, NULL);
    }

    ipv6cp_Setup(ipv6cp);
  }
  recursing--;
}

static void
ipv6cp_LayerStart(struct fsm *fp)
{
  /* We're about to start up ! */
  struct ipv6cp *ipv6cp = fsm2ipv6cp(fp);

  log_Printf(LogIPV6CP, "%s: LayerStart.\n", fp->link->name);
  throughput_start(&ipv6cp->throughput, "IPV6CP throughput",
                   Enabled(fp->bundle, OPT_THROUGHPUT));
  fp->more.reqs = fp->more.naks = fp->more.rejs = ipv6cp->cfg.fsm.maxreq * 3;
  ipv6cp->peer_tokenreq = 0;
}

static void
ipv6cp_LayerFinish(struct fsm *fp)
{
  /* We're now down */
  struct ipv6cp *ipv6cp = fsm2ipv6cp(fp);

  log_Printf(LogIPV6CP, "%s: LayerFinish.\n", fp->link->name);
  throughput_stop(&ipv6cp->throughput);
  throughput_log(&ipv6cp->throughput, LogIPV6CP, NULL);
}

static void
ipv6cp_InitRestartCounter(struct fsm *fp, int what)
{
  /* Set fsm timer load */
  struct ipv6cp *ipv6cp = fsm2ipv6cp(fp);

  fp->FsmTimer.load = ipv6cp->cfg.fsm.timeout * SECTICKS;
  switch (what) {
    case FSM_REQ_TIMER:
      fp->restart = ipv6cp->cfg.fsm.maxreq;
      break;
    case FSM_TRM_TIMER:
      fp->restart = ipv6cp->cfg.fsm.maxtrm;
      break;
    default:
      fp->restart = 1;
      break;
  }
}

static void
ipv6cp_SendConfigReq(struct fsm *fp)
{
  /* Send config REQ please */
  struct physical *p = link2physical(fp->link);
  struct ipv6cp *ipv6cp = fsm2ipv6cp(fp);
  u_char buff[IPV6CP_IFIDLEN+2];
  struct fsm_opt *o;

  o = (struct fsm_opt *)buff;

  if ((p && !physical_IsSync(p)) || !REJECTED(ipv6cp, TY_TOKEN)) {
    memcpy(o->data, ipv6cp->my_ifid, IPV6CP_IFIDLEN);
    INC_FSM_OPT(TY_TOKEN, IPV6CP_IFIDLEN + 2, o);
  }

  fsm_Output(fp, CODE_CONFIGREQ, fp->reqid, buff, (u_char *)o - buff,
             MB_IPV6CPOUT);
}

static void
ipv6cp_SentTerminateReq(struct fsm *fp __unused)
{
  /* Term REQ just sent by FSM */
}

static void
ipv6cp_SendTerminateAck(struct fsm *fp, u_char id)
{
  /* Send Term ACK please */
  fsm_Output(fp, CODE_TERMACK, id, NULL, 0, MB_IPV6CPOUT);
}

static const char *
protoname(unsigned proto)
{
  static const char *cftypes[] = { "IFACEID", "COMPPROTO" };

  if (proto > 0 && proto <= sizeof cftypes / sizeof *cftypes)
    return cftypes[proto - 1];

  return NumStr(proto, NULL, 0);
}

static void
ipv6cp_ValidateInterfaceID(struct ipv6cp *ipv6cp, u_char *ifid,
			   struct fsm_decode *dec)
{
  struct fsm_opt opt;
  u_char zero[IPV6CP_IFIDLEN];

  memset(zero, 0, IPV6CP_IFIDLEN);

  if (memcmp(ifid, zero, IPV6CP_IFIDLEN) != 0
      && memcmp(ifid, ipv6cp->my_ifid, IPV6CP_IFIDLEN) != 0)
    memcpy(ipv6cp->his_ifid, ifid, IPV6CP_IFIDLEN);

  opt.hdr.id = TY_TOKEN;
  opt.hdr.len = IPV6CP_IFIDLEN + 2;
  memcpy(opt.data, &ipv6cp->his_ifid, IPV6CP_IFIDLEN);
  if (memcmp(ifid, ipv6cp->his_ifid, IPV6CP_IFIDLEN) == 0)
    fsm_ack(dec, &opt);
  else
    fsm_nak(dec, &opt);
}

static void
ipv6cp_DecodeConfig(struct fsm *fp, u_char *cp, u_char *end, int mode_type,
                    struct fsm_decode *dec)
{
  /* Deal with incoming PROTO_IPV6CP */
  struct ipv6cp *ipv6cp = fsm2ipv6cp(fp);
  int n;
  char tbuff[100];
  u_char ifid[IPV6CP_IFIDLEN], zero[IPV6CP_IFIDLEN];
  struct fsm_opt *opt;

  memset(zero, 0, IPV6CP_IFIDLEN);

  while (end - cp >= (int)sizeof(opt->hdr)) {
    if ((opt = fsm_readopt(&cp)) == NULL)
      break;

    snprintf(tbuff, sizeof tbuff, " %s[%d]", protoname(opt->hdr.id),
             opt->hdr.len);

    switch (opt->hdr.id) {
    case TY_TOKEN:
      memcpy(ifid, opt->data, IPV6CP_IFIDLEN);
      log_Printf(LogIPV6CP, "%s 0x%02x%02x%02x%02x%02x%02x%02x%02x\n", tbuff,
		 ifid[0], ifid[1], ifid[2], ifid[3], ifid[4], ifid[5], ifid[6], ifid[7]);

      switch (mode_type) {
      case MODE_REQ:
        ipv6cp->peer_tokenreq = 1;
        ipv6cp_ValidateInterfaceID(ipv6cp, ifid, dec);
        break;

      case MODE_NAK:
        if (memcmp(ifid, zero, IPV6CP_IFIDLEN) == 0) {
          log_Printf(log_IsKept(LogIPV6CP) ? LogIPV6CP : LogPHASE,
		     "0x0000000000000000: Unacceptable IntefaceID!\n");
          fsm_Close(&ipv6cp->fsm);
        } else if (memcmp(ifid, ipv6cp->his_ifid, IPV6CP_IFIDLEN) == 0) {
          log_Printf(log_IsKept(LogIPV6CP) ? LogIPV6CP : LogPHASE,
		     "0x%02x%02x%02x%02x%02x%02x%02x%02x: "
		     "Unacceptable IntefaceID!\n",
		     ifid[0], ifid[1], ifid[2], ifid[3],
		     ifid[4], ifid[5], ifid[6], ifid[7]);
        } else if (memcmp(ifid, ipv6cp->my_ifid, IPV6CP_IFIDLEN) != 0) {
          n = 100;
	  while (n && !ipcp_SetIPv6address(ipv6cp, ifid, ipv6cp->his_ifid)) {
	    do {
	      n--;
	      SetInterfaceID(ifid, 1);
	    } while (n && memcmp(ifid, ipv6cp->his_ifid, IPV6CP_IFIDLEN) == 0);
	  }

          if (n == 0) {
            log_Printf(log_IsKept(LogIPV6CP) ? LogIPV6CP : LogPHASE,
                       "0x0000000000000000: Unacceptable IntefaceID!\n");
            fsm_Close(&ipv6cp->fsm);
          } else {
	    log_Printf(LogIPV6CP, "%s changing IntefaceID: "
		       "0x%02x%02x%02x%02x%02x%02x%02x%02x "
		       "--> 0x%02x%02x%02x%02x%02x%02x%02x%02x\n", tbuff,
		       ipv6cp->my_ifid[0], ipv6cp->my_ifid[1],
		       ipv6cp->my_ifid[2], ipv6cp->my_ifid[3],
		       ipv6cp->my_ifid[4], ipv6cp->my_ifid[5],
		       ipv6cp->my_ifid[6], ipv6cp->my_ifid[7],
		       ifid[0], ifid[1], ifid[2], ifid[3],
		       ifid[4], ifid[5], ifid[6], ifid[7]);
            memcpy(ipv6cp->my_ifid, ifid, IPV6CP_IFIDLEN);
            bundle_AdjustFilters(fp->bundle, &ipv6cp->myaddr, NULL);
          }
        }
        break;

      case MODE_REJ:
        ipv6cp->his_reject |= (1 << opt->hdr.id);
        break;
      }
      break;

    default:
      if (mode_type != MODE_NOP) {
        ipv6cp->my_reject |= (1 << opt->hdr.id);
        fsm_rej(dec, opt);
      }
      break;
    }
  }

  if (mode_type != MODE_NOP) {
    if (mode_type == MODE_REQ && !ipv6cp->peer_tokenreq) {
      if (dec->rejend == dec->rej && dec->nakend == dec->nak) {
        /*
         * Pretend the peer has requested a TOKEN.
         * We do this to ensure that we only send one NAK if the only
         * reason for the NAK is because the peer isn't sending a
         * TY_TOKEN REQ.  This stops us from repeatedly trying to tell
         * the peer that we have to have an IP address on their end.
         */
        ipv6cp->peer_tokenreq = 1;
      }
      memset(ifid, 0, IPV6CP_IFIDLEN);
      ipv6cp_ValidateInterfaceID(ipv6cp, ifid, dec);
    }
    fsm_opt_normalise(dec);
  }
}
#endif
