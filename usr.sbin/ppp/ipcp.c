/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Toshiharu OHNO <tony-o@iij.ad.jp>
 *                           Internet Initiative Japan, Inc (IIJ)
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
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>
#include <netdb.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <resolv.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#ifndef NONAT
#ifdef LOCALNAT
#include "alias.h"
#else
#include <alias.h>
#endif
#endif

#include "layer.h"
#include "ua.h"
#include "defs.h"
#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "fsm.h"
#include "proto.h"
#include "iplist.h"
#include "throughput.h"
#include "slcompress.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "ncpaddr.h"
#include "ip.h"
#include "ipcp.h"
#include "filter.h"
#include "descriptor.h"
#include "vjcomp.h"
#include "async.h"
#include "ccp.h"
#include "link.h"
#include "physical.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "id.h"
#include "arp.h"
#include "systems.h"
#include "prompt.h"
#include "route.h"
#include "iface.h"

#undef REJECTED
#define	REJECTED(p, x)	((p)->peer_reject & (1<<(x)))
#define issep(ch) ((ch) == ' ' || (ch) == '\t')
#define isip(ch) (((ch) >= '0' && (ch) <= '9') || (ch) == '.')

struct compreq {
  u_short proto;
  u_char slots;
  u_char compcid;
};

static int IpcpLayerUp(struct fsm *);
static void IpcpLayerDown(struct fsm *);
static void IpcpLayerStart(struct fsm *);
static void IpcpLayerFinish(struct fsm *);
static void IpcpInitRestartCounter(struct fsm *, int);
static void IpcpSendConfigReq(struct fsm *);
static void IpcpSentTerminateReq(struct fsm *);
static void IpcpSendTerminateAck(struct fsm *, u_char);
static void IpcpDecodeConfig(struct fsm *, u_char *, u_char *, int,
                             struct fsm_decode *);

extern struct libalias *la;

static struct fsm_callbacks ipcp_Callbacks = {
  IpcpLayerUp,
  IpcpLayerDown,
  IpcpLayerStart,
  IpcpLayerFinish,
  IpcpInitRestartCounter,
  IpcpSendConfigReq,
  IpcpSentTerminateReq,
  IpcpSendTerminateAck,
  IpcpDecodeConfig,
  fsm_NullRecvResetReq,
  fsm_NullRecvResetAck
};

static const char *
protoname(int proto)
{
  static struct {
    int id;
    const char *txt;
  } cftypes[] = {
    /* Check out the latest ``Assigned numbers'' rfc (rfc1700.txt) */
    { 1, "IPADDRS" },		/* IP-Addresses */	/* deprecated */
    { 2, "COMPPROTO" },		/* IP-Compression-Protocol */
    { 3, "IPADDR" },		/* IP-Address */
    { 129, "PRIDNS" },		/* 129: Primary DNS Server Address */
    { 130, "PRINBNS" },		/* 130: Primary NBNS Server Address */
    { 131, "SECDNS" },		/* 131: Secondary DNS Server Address */
    { 132, "SECNBNS" }		/* 132: Secondary NBNS Server Address */
  };
  unsigned f;

  for (f = 0; f < sizeof cftypes / sizeof *cftypes; f++)
    if (cftypes[f].id == proto)
      return cftypes[f].txt;

  return NumStr(proto, NULL, 0);
}

void
ipcp_AddInOctets(struct ipcp *ipcp, int n)
{
  throughput_addin(&ipcp->throughput, n);
}

void
ipcp_AddOutOctets(struct ipcp *ipcp, int n)
{
  throughput_addout(&ipcp->throughput, n);
}

void
ipcp_LoadDNS(struct ipcp *ipcp)
{
  int fd;

  ipcp->ns.dns[0].s_addr = ipcp->ns.dns[1].s_addr = INADDR_NONE;

  if (ipcp->ns.resolv != NULL) {
    free(ipcp->ns.resolv);
    ipcp->ns.resolv = NULL;
  }
  if (ipcp->ns.resolv_nons != NULL) {
    free(ipcp->ns.resolv_nons);
    ipcp->ns.resolv_nons = NULL;
  }
  ipcp->ns.resolver = 0;

  if ((fd = open(_PATH_RESCONF, O_RDONLY)) != -1) {
    struct stat st;

    if (fstat(fd, &st) == 0) {
      ssize_t got;

      /*
       * Note, ns.resolv and ns.resolv_nons are assumed to always point to
       * buffers of the same size!  See the strcpy() below.
       */
      if ((ipcp->ns.resolv_nons = (char *)malloc(st.st_size + 1)) == NULL)
        log_Printf(LogERROR, "Failed to malloc %lu for %s: %s\n",
                   (unsigned long)st.st_size, _PATH_RESCONF, strerror(errno));
      else if ((ipcp->ns.resolv = (char *)malloc(st.st_size + 1)) == NULL) {
        log_Printf(LogERROR, "Failed(2) to malloc %lu for %s: %s\n",
                   (unsigned long)st.st_size, _PATH_RESCONF, strerror(errno));
        free(ipcp->ns.resolv_nons);
        ipcp->ns.resolv_nons = NULL;
      } else if ((got = read(fd, ipcp->ns.resolv, st.st_size)) != st.st_size) {
        if (got == -1)
          log_Printf(LogERROR, "Failed to read %s: %s\n",
                     _PATH_RESCONF, strerror(errno));
        else
          log_Printf(LogERROR, "Failed to read %s, got %lu not %lu\n",
                     _PATH_RESCONF, (unsigned long)got,
                     (unsigned long)st.st_size);
        free(ipcp->ns.resolv_nons);
        ipcp->ns.resolv_nons = NULL;
        free(ipcp->ns.resolv);
        ipcp->ns.resolv = NULL;
      } else {
        char *cp, *cp_nons, *ncp, ch;
        int n;

        ipcp->ns.resolv[st.st_size] = '\0';
        ipcp->ns.resolver = 1;

        cp_nons = ipcp->ns.resolv_nons;
        cp = ipcp->ns.resolv;
        n = 0;

        while ((ncp = strstr(cp, "nameserver")) != NULL) {
          if (ncp != cp) {
            memcpy(cp_nons, cp, ncp - cp);
            cp_nons += ncp - cp;
          }
          if ((ncp != cp && ncp[-1] != '\n') || !issep(ncp[10])) {
            memcpy(cp_nons, ncp, 9);
            cp_nons += 9;
            cp = ncp + 9;	/* Can't match "nameserver" at cp... */
            continue;
          }

          for (cp = ncp + 11; issep(*cp); cp++)	/* Skip whitespace */
            ;

          for (ncp = cp; isip(*ncp); ncp++)		/* Jump over IP */
            ;

          ch = *ncp;
          *ncp = '\0';
          if (n < 2 && inet_aton(cp, ipcp->ns.dns))
            n++;
          *ncp = ch;

          if ((cp = strchr(ncp, '\n')) == NULL)	/* Point at next line */
            cp = ncp + strlen(ncp);
          else
            cp++;
        }
        /*
         * Note, cp_nons and cp always point to buffers of the same size, so
         * strcpy is ok!
         */
        strcpy(cp_nons, cp);	/* Copy the end - including the NUL */
        cp_nons += strlen(cp_nons) - 1;
        while (cp_nons >= ipcp->ns.resolv_nons && *cp_nons == '\n')
          *cp_nons-- = '\0';
        if (n == 2 && ipcp->ns.dns[0].s_addr == INADDR_ANY) {
          ipcp->ns.dns[0].s_addr = ipcp->ns.dns[1].s_addr;
          ipcp->ns.dns[1].s_addr = INADDR_ANY;
        }
        bundle_AdjustDNS(ipcp->fsm.bundle);
      }
    } else
      log_Printf(LogERROR, "Failed to stat opened %s: %s\n",
                 _PATH_RESCONF, strerror(errno));

    close(fd);
  }
}

int
ipcp_WriteDNS(struct ipcp *ipcp)
{
  const char *paddr;
  mode_t mask;
  FILE *fp;

  if (ipcp->ns.dns[0].s_addr == INADDR_ANY &&
      ipcp->ns.dns[1].s_addr == INADDR_ANY) {
    log_Printf(LogIPCP, "%s not modified: All nameservers NAKd\n",
              _PATH_RESCONF);
    return 0;
  }

  if (ipcp->ns.dns[0].s_addr == INADDR_ANY) {
    ipcp->ns.dns[0].s_addr = ipcp->ns.dns[1].s_addr;
    ipcp->ns.dns[1].s_addr = INADDR_ANY;
  }

  mask = umask(022);
  if ((fp = ID0fopen(_PATH_RESCONF, "w")) != NULL) {
    umask(mask);
    if (ipcp->ns.resolv_nons)
      fputs(ipcp->ns.resolv_nons, fp);
    paddr = inet_ntoa(ipcp->ns.dns[0]);
    log_Printf(LogIPCP, "Primary nameserver set to %s\n", paddr);
    fprintf(fp, "\nnameserver %s\n", paddr);
    if (ipcp->ns.dns[1].s_addr != INADDR_ANY &&
        ipcp->ns.dns[1].s_addr != INADDR_NONE &&
        ipcp->ns.dns[1].s_addr != ipcp->ns.dns[0].s_addr) {
      paddr = inet_ntoa(ipcp->ns.dns[1]);
      log_Printf(LogIPCP, "Secondary nameserver set to %s\n", paddr);
      fprintf(fp, "nameserver %s\n", paddr);
    }
    if (fclose(fp) == EOF) {
      log_Printf(LogERROR, "write(): Failed updating %s: %s\n", _PATH_RESCONF,
                 strerror(errno));
      return 0;
    }
  } else {
    umask(mask);
    log_Printf(LogERROR,"fopen(\"%s\", \"w\") failed: %s\n", _PATH_RESCONF,
                 strerror(errno));
  }

  return 1;
}

void
ipcp_RestoreDNS(struct ipcp *ipcp)
{
  if (ipcp->ns.resolver) {
    ssize_t got, len;
    int fd;

    if ((fd = ID0open(_PATH_RESCONF, O_WRONLY|O_TRUNC, 0644)) != -1) {
      len = strlen(ipcp->ns.resolv);
      if ((got = write(fd, ipcp->ns.resolv, len)) != len) {
        if (got == -1)
          log_Printf(LogERROR, "Failed rewriting %s: write: %s\n",
                     _PATH_RESCONF, strerror(errno));
        else
          log_Printf(LogERROR, "Failed rewriting %s: wrote %ld of %ld\n",
                     _PATH_RESCONF, (long)got, (long)len);
      }
      close(fd);
    } else
      log_Printf(LogERROR, "Failed rewriting %s: open: %s\n", _PATH_RESCONF,
                 strerror(errno));
  } else if (remove(_PATH_RESCONF) == -1)
    log_Printf(LogERROR, "Failed removing %s: %s\n", _PATH_RESCONF,
               strerror(errno));

}

int
ipcp_Show(struct cmdargs const *arg)
{
  struct ipcp *ipcp = &arg->bundle->ncp.ipcp;

  prompt_Printf(arg->prompt, "%s [%s]\n", ipcp->fsm.name,
                State2Nam(ipcp->fsm.state));
  if (ipcp->fsm.state == ST_OPENED) {
    prompt_Printf(arg->prompt, " His side:        %s, %s\n",
                  inet_ntoa(ipcp->peer_ip), vj2asc(ipcp->peer_compproto));
    prompt_Printf(arg->prompt, " My side:         %s, %s\n",
                  inet_ntoa(ipcp->my_ip), vj2asc(ipcp->my_compproto));
    prompt_Printf(arg->prompt, " Queued packets:  %lu\n",
                  (unsigned long)ipcp_QueueLen(ipcp));
  }

  prompt_Printf(arg->prompt, "\nDefaults:\n");
  prompt_Printf(arg->prompt, " FSM retry = %us, max %u Config"
                " REQ%s, %u Term REQ%s\n", ipcp->cfg.fsm.timeout,
                ipcp->cfg.fsm.maxreq, ipcp->cfg.fsm.maxreq == 1 ? "" : "s",
                ipcp->cfg.fsm.maxtrm, ipcp->cfg.fsm.maxtrm == 1 ? "" : "s");
  prompt_Printf(arg->prompt, " My Address:      %s\n",
                ncprange_ntoa(&ipcp->cfg.my_range));
  if (ipcp->cfg.HaveTriggerAddress)
    prompt_Printf(arg->prompt, " Trigger address: %s\n",
                  inet_ntoa(ipcp->cfg.TriggerAddress));

  prompt_Printf(arg->prompt, " VJ compression:  %s (%d slots %s slot "
                "compression)\n", command_ShowNegval(ipcp->cfg.vj.neg),
                ipcp->cfg.vj.slots, ipcp->cfg.vj.slotcomp ? "with" : "without");

  if (iplist_isvalid(&ipcp->cfg.peer_list))
    prompt_Printf(arg->prompt, " His Address:     %s\n",
                  ipcp->cfg.peer_list.src);
  else
    prompt_Printf(arg->prompt, " His Address:     %s\n",
                  ncprange_ntoa(&ipcp->cfg.peer_range));

  prompt_Printf(arg->prompt, " DNS:             %s",
                ipcp->cfg.ns.dns[0].s_addr == INADDR_NONE ?
                "none" : inet_ntoa(ipcp->cfg.ns.dns[0]));
  if (ipcp->cfg.ns.dns[1].s_addr != INADDR_NONE)
    prompt_Printf(arg->prompt, ", %s",
                  inet_ntoa(ipcp->cfg.ns.dns[1]));
  prompt_Printf(arg->prompt, ", %s\n",
                command_ShowNegval(ipcp->cfg.ns.dns_neg));
  prompt_Printf(arg->prompt, " Resolver DNS:    %s",
                ipcp->ns.dns[0].s_addr == INADDR_NONE ?
                "none" : inet_ntoa(ipcp->ns.dns[0]));
  if (ipcp->ns.dns[1].s_addr != INADDR_NONE &&
      ipcp->ns.dns[1].s_addr != ipcp->ns.dns[0].s_addr)
    prompt_Printf(arg->prompt, ", %s",
                  inet_ntoa(ipcp->ns.dns[1]));
  prompt_Printf(arg->prompt, "\n NetBIOS NS:      %s, ",
                inet_ntoa(ipcp->cfg.ns.nbns[0]));
  prompt_Printf(arg->prompt, "%s\n\n",
                inet_ntoa(ipcp->cfg.ns.nbns[1]));

  throughput_disp(&ipcp->throughput, arg->prompt);

  return 0;
}

int
ipcp_vjset(struct cmdargs const *arg)
{
  if (arg->argc != arg->argn+2)
    return -1;
  if (!strcasecmp(arg->argv[arg->argn], "slots")) {
    int slots;

    slots = atoi(arg->argv[arg->argn+1]);
    if (slots < 4 || slots > 16)
      return 1;
    arg->bundle->ncp.ipcp.cfg.vj.slots = slots;
    return 0;
  } else if (!strcasecmp(arg->argv[arg->argn], "slotcomp")) {
    if (!strcasecmp(arg->argv[arg->argn+1], "on"))
      arg->bundle->ncp.ipcp.cfg.vj.slotcomp = 1;
    else if (!strcasecmp(arg->argv[arg->argn+1], "off"))
      arg->bundle->ncp.ipcp.cfg.vj.slotcomp = 0;
    else
      return 2;
    return 0;
  }
  return -1;
}

void
ipcp_Init(struct ipcp *ipcp, struct bundle *bundle, struct link *l,
          const struct fsm_parent *parent)
{
  struct hostent *hp;
  struct in_addr host;
  char name[MAXHOSTNAMELEN];
  static const char * const timer_names[] =
    {"IPCP restart", "IPCP openmode", "IPCP stopped"};

  fsm_Init(&ipcp->fsm, "IPCP", PROTO_IPCP, 1, IPCP_MAXCODE, LogIPCP,
           bundle, l, parent, &ipcp_Callbacks, timer_names);

  ipcp->cfg.vj.slots = DEF_VJ_STATES;
  ipcp->cfg.vj.slotcomp = 1;
  memset(&ipcp->cfg.my_range, '\0', sizeof ipcp->cfg.my_range);

  host.s_addr = htonl(INADDR_LOOPBACK);
  ipcp->cfg.netmask.s_addr = INADDR_ANY;
  if (gethostname(name, sizeof name) == 0) {
    hp = gethostbyname(name);
    if (hp && hp->h_addrtype == AF_INET && hp->h_length == sizeof host.s_addr)
      memcpy(&host.s_addr, hp->h_addr, sizeof host.s_addr);
  }
  ncprange_setip4(&ipcp->cfg.my_range, host, ipcp->cfg.netmask);
  ncprange_setip4(&ipcp->cfg.peer_range, ipcp->cfg.netmask, ipcp->cfg.netmask);

  iplist_setsrc(&ipcp->cfg.peer_list, "");
  ipcp->cfg.HaveTriggerAddress = 0;

  ipcp->cfg.ns.dns[0].s_addr = INADDR_NONE;
  ipcp->cfg.ns.dns[1].s_addr = INADDR_NONE;
  ipcp->cfg.ns.dns_neg = 0;
  ipcp->cfg.ns.nbns[0].s_addr = INADDR_ANY;
  ipcp->cfg.ns.nbns[1].s_addr = INADDR_ANY;

  ipcp->cfg.fsm.timeout = DEF_FSMRETRY;
  ipcp->cfg.fsm.maxreq = DEF_FSMTRIES;
  ipcp->cfg.fsm.maxtrm = DEF_FSMTRIES;
  ipcp->cfg.vj.neg = NEG_ENABLED|NEG_ACCEPTED;

  memset(&ipcp->vj, '\0', sizeof ipcp->vj);

  ipcp->ns.resolv = NULL;
  ipcp->ns.resolv_nons = NULL;
  ipcp->ns.writable = 1;
  ipcp_LoadDNS(ipcp);

  throughput_init(&ipcp->throughput, SAMPLE_PERIOD);
  memset(ipcp->Queue, '\0', sizeof ipcp->Queue);
  ipcp_Setup(ipcp, INADDR_NONE);
}

void
ipcp_Destroy(struct ipcp *ipcp)
{
  throughput_destroy(&ipcp->throughput);

  if (ipcp->ns.resolv != NULL) {
    free(ipcp->ns.resolv);
    ipcp->ns.resolv = NULL;
  }
  if (ipcp->ns.resolv_nons != NULL) {
    free(ipcp->ns.resolv_nons);
    ipcp->ns.resolv_nons = NULL;
  }
}

void
ipcp_SetLink(struct ipcp *ipcp, struct link *l)
{
  ipcp->fsm.link = l;
}

void
ipcp_Setup(struct ipcp *ipcp, u_int32_t mask)
{
  struct iface *iface = ipcp->fsm.bundle->iface;
  struct ncpaddr ipaddr;
  struct in_addr peer;
  int pos;
  unsigned n;

  ipcp->fsm.open_mode = 0;
  ipcp->ifmask.s_addr = mask == INADDR_NONE ? ipcp->cfg.netmask.s_addr : mask;

  if (iplist_isvalid(&ipcp->cfg.peer_list)) {
    /* Try to give the peer a previously configured IP address */
    for (n = 0; n < iface->addrs; n++) {
      if (!ncpaddr_getip4(&iface->addr[n].peer, &peer))
        continue;
      if ((pos = iplist_ip2pos(&ipcp->cfg.peer_list, peer)) != -1) {
        ncpaddr_setip4(&ipaddr, iplist_setcurpos(&ipcp->cfg.peer_list, pos));
        break;
      }
    }
    if (n == iface->addrs)
      /* Ok, so none of 'em fit.... pick a random one */
      ncpaddr_setip4(&ipaddr, iplist_setrandpos(&ipcp->cfg.peer_list));

    ncprange_sethost(&ipcp->cfg.peer_range, &ipaddr);
  }

  ipcp->heis1172 = 0;
  ipcp->peer_req = 0;
  ncprange_getip4addr(&ipcp->cfg.peer_range, &ipcp->peer_ip);
  ipcp->peer_compproto = 0;

  if (ipcp->cfg.HaveTriggerAddress) {
    /*
     * Some implementations of PPP require that we send a
     * *special* value as our address, even though the rfc specifies
     * full negotiation (e.g. "0.0.0.0" or Not "0.0.0.0").
     */
    ipcp->my_ip = ipcp->cfg.TriggerAddress;
    log_Printf(LogIPCP, "Using trigger address %s\n",
              inet_ntoa(ipcp->cfg.TriggerAddress));
  } else {
    /*
     * Otherwise, if we've used an IP number before and it's still within
     * the network specified on the ``set ifaddr'' line, we really
     * want to keep that IP number so that we can keep any existing
     * connections that are bound to that IP.
     */
    for (n = 0; n < iface->addrs; n++) {
      ncprange_getaddr(&iface->addr[n].ifa, &ipaddr);
      if (ncprange_contains(&ipcp->cfg.my_range, &ipaddr)) {
        ncpaddr_getip4(&ipaddr, &ipcp->my_ip);
        break;
      }
    }
    if (n == iface->addrs)
      ncprange_getip4addr(&ipcp->cfg.my_range, &ipcp->my_ip);
  }

  if (IsEnabled(ipcp->cfg.vj.neg)
#ifndef NORADIUS
      || (ipcp->fsm.bundle->radius.valid && ipcp->fsm.bundle->radius.vj)
#endif
     )
    ipcp->my_compproto = (PROTO_VJCOMP << 16) +
                         ((ipcp->cfg.vj.slots - 1) << 8) +
                         ipcp->cfg.vj.slotcomp;
  else
    ipcp->my_compproto = 0;
  sl_compress_init(&ipcp->vj.cslc, ipcp->cfg.vj.slots - 1);

  ipcp->peer_reject = 0;
  ipcp->my_reject = 0;

  /* Copy startup values into ipcp->ns.dns */
  if (ipcp->cfg.ns.dns[0].s_addr != INADDR_NONE)
    memcpy(ipcp->ns.dns, ipcp->cfg.ns.dns, sizeof ipcp->ns.dns);
}

static int
numaddresses(struct in_addr mask)
{
  u_int32_t bit, haddr;
  int n;

  haddr = ntohl(mask.s_addr);
  bit = 1;
  n = 1;

  do {
    if (!(haddr & bit))
      n <<= 1;
  } while (bit <<= 1);

  return n;
}

static int
ipcp_proxyarp(struct ipcp *ipcp,
              int (*proxyfun)(struct bundle *, struct in_addr),
              const struct iface_addr *addr)
{
  struct bundle *bundle = ipcp->fsm.bundle;
  struct in_addr peer, mask, ip;
  int n, ret;

  if (!ncpaddr_getip4(&addr->peer, &peer)) {
    log_Printf(LogERROR, "Oops, ipcp_proxyarp() called with unexpected addr\n");
    return 0;
  }

  ret = 0;

  if (Enabled(bundle, OPT_PROXYALL)) {
    ncprange_getip4mask(&addr->ifa, &mask);
    if ((n = numaddresses(mask)) > 256) {
      log_Printf(LogWARN, "%s: Too many addresses for proxyall\n",
                 ncprange_ntoa(&addr->ifa));
      return 0;
    }
    ip.s_addr = peer.s_addr & mask.s_addr;
    if (n >= 4) {
      ip.s_addr = htonl(ntohl(ip.s_addr) + 1);
      n -= 2;
    }
    while (n) {
      if (!((ip.s_addr ^ peer.s_addr) & mask.s_addr)) {
        if (!(ret = (*proxyfun)(bundle, ip)))
          break;
        n--;
      }
      ip.s_addr = htonl(ntohl(ip.s_addr) + 1);
    }
    ret = !n;
  } else if (Enabled(bundle, OPT_PROXY))
    ret = (*proxyfun)(bundle, peer);

  return ret;
}

static int
ipcp_SetIPaddress(struct ipcp *ipcp, struct in_addr myaddr,
                  struct in_addr hisaddr)
{
  struct bundle *bundle = ipcp->fsm.bundle;
  struct ncpaddr myncpaddr, hisncpaddr;
  struct ncprange myrange;
  struct in_addr mask;
  struct sockaddr_storage ssdst, ssgw, ssmask;
  struct sockaddr *sadst, *sagw, *samask;

  sadst = (struct sockaddr *)&ssdst;
  sagw = (struct sockaddr *)&ssgw;
  samask = (struct sockaddr *)&ssmask;

  ncpaddr_setip4(&hisncpaddr, hisaddr);
  ncpaddr_setip4(&myncpaddr, myaddr);
  ncprange_sethost(&myrange, &myncpaddr);

  mask = addr2mask(myaddr);

  if (ipcp->ifmask.s_addr != INADDR_ANY &&
      (ipcp->ifmask.s_addr & mask.s_addr) == mask.s_addr)
    ncprange_setip4mask(&myrange, ipcp->ifmask);

  if (!iface_Add(bundle->iface, &bundle->ncp, &myrange, &hisncpaddr,
                 IFACE_ADD_FIRST|IFACE_FORCE_ADD|IFACE_SYSTEM))
    return 0;

  if (!Enabled(bundle, OPT_IFACEALIAS))
    iface_Clear(bundle->iface, &bundle->ncp, AF_INET,
                IFACE_CLEAR_ALIASES|IFACE_SYSTEM);

  if (bundle->ncp.cfg.sendpipe > 0 || bundle->ncp.cfg.recvpipe > 0) {
    ncprange_getsa(&myrange, &ssgw, &ssmask);
    ncpaddr_getsa(&hisncpaddr, &ssdst);
    rt_Update(bundle, sadst, sagw, samask, NULL, NULL);
  }

  if (Enabled(bundle, OPT_SROUTES))
    route_Change(bundle, bundle->ncp.route, &myncpaddr, &hisncpaddr);

#ifndef NORADIUS
  if (bundle->radius.valid)
    route_Change(bundle, bundle->radius.routes, &myncpaddr, &hisncpaddr);
#endif

  return 1;	/* Ok */
}

static struct in_addr
ChooseHisAddr(struct bundle *bundle, struct in_addr gw)
{
  struct in_addr try;
  u_long f;

  for (f = 0; f < bundle->ncp.ipcp.cfg.peer_list.nItems; f++) {
    try = iplist_next(&bundle->ncp.ipcp.cfg.peer_list);
    log_Printf(LogDEBUG, "ChooseHisAddr: Check item %ld (%s)\n",
              f, inet_ntoa(try));
    if (ipcp_SetIPaddress(&bundle->ncp.ipcp, gw, try)) {
      log_Printf(LogIPCP, "Selected IP address %s\n", inet_ntoa(try));
      break;
    }
  }

  if (f == bundle->ncp.ipcp.cfg.peer_list.nItems) {
    log_Printf(LogDEBUG, "ChooseHisAddr: All addresses in use !\n");
    try.s_addr = INADDR_ANY;
  }

  return try;
}

static void
IpcpInitRestartCounter(struct fsm *fp, int what)
{
  /* Set fsm timer load */
  struct ipcp *ipcp = fsm2ipcp(fp);

  fp->FsmTimer.load = ipcp->cfg.fsm.timeout * SECTICKS;
  switch (what) {
    case FSM_REQ_TIMER:
      fp->restart = ipcp->cfg.fsm.maxreq;
      break;
    case FSM_TRM_TIMER:
      fp->restart = ipcp->cfg.fsm.maxtrm;
      break;
    default:
      fp->restart = 1;
      break;
  }
}

static void
IpcpSendConfigReq(struct fsm *fp)
{
  /* Send config REQ please */
  struct physical *p = link2physical(fp->link);
  struct ipcp *ipcp = fsm2ipcp(fp);
  u_char buff[MAX_FSM_OPT_LEN];
  struct fsm_opt *o;

  o = (struct fsm_opt *)buff;

  if ((p && !physical_IsSync(p)) || !REJECTED(ipcp, TY_IPADDR)) {
    memcpy(o->data, &ipcp->my_ip.s_addr, 4);
    INC_FSM_OPT(TY_IPADDR, 6, o);
  }

  if (ipcp->my_compproto && !REJECTED(ipcp, TY_COMPPROTO)) {
    if (ipcp->heis1172) {
      u_int16_t proto = PROTO_VJCOMP;

      ua_htons(&proto, o->data);
      INC_FSM_OPT(TY_COMPPROTO, 4, o);
    } else {
      struct compreq req;

      req.proto = htons(ipcp->my_compproto >> 16);
      req.slots = (ipcp->my_compproto >> 8) & 255;
      req.compcid = ipcp->my_compproto & 1;
      memcpy(o->data, &req, 4);
      INC_FSM_OPT(TY_COMPPROTO, 6, o);
    }
  }

  if (IsEnabled(ipcp->cfg.ns.dns_neg)) {
    if (!REJECTED(ipcp, TY_PRIMARY_DNS - TY_ADJUST_NS)) {
      memcpy(o->data, &ipcp->ns.dns[0].s_addr, 4);
      INC_FSM_OPT(TY_PRIMARY_DNS, 6, o);
    }

    if (!REJECTED(ipcp, TY_SECONDARY_DNS - TY_ADJUST_NS)) {
      memcpy(o->data, &ipcp->ns.dns[1].s_addr, 4);
      INC_FSM_OPT(TY_SECONDARY_DNS, 6, o);
    }
  }

  fsm_Output(fp, CODE_CONFIGREQ, fp->reqid, buff, (u_char *)o - buff,
             MB_IPCPOUT);
}

static void
IpcpSentTerminateReq(struct fsm *fp __unused)
{
  /* Term REQ just sent by FSM */
}

static void
IpcpSendTerminateAck(struct fsm *fp, u_char id)
{
  /* Send Term ACK please */
  fsm_Output(fp, CODE_TERMACK, id, NULL, 0, MB_IPCPOUT);
}

static void
IpcpLayerStart(struct fsm *fp)
{
  /* We're about to start up ! */
  struct ipcp *ipcp = fsm2ipcp(fp);

  log_Printf(LogIPCP, "%s: LayerStart.\n", fp->link->name);
  throughput_start(&ipcp->throughput, "IPCP throughput",
                   Enabled(fp->bundle, OPT_THROUGHPUT));
  fp->more.reqs = fp->more.naks = fp->more.rejs = ipcp->cfg.fsm.maxreq * 3;
  ipcp->peer_req = 0;
}

static void
IpcpLayerFinish(struct fsm *fp)
{
  /* We're now down */
  struct ipcp *ipcp = fsm2ipcp(fp);

  log_Printf(LogIPCP, "%s: LayerFinish.\n", fp->link->name);
  throughput_stop(&ipcp->throughput);
  throughput_log(&ipcp->throughput, LogIPCP, NULL);
}

/*
 * Called from iface_Add() via ncp_IfaceAddrAdded()
 */
void
ipcp_IfaceAddrAdded(struct ipcp *ipcp, const struct iface_addr *addr)
{
  struct bundle *bundle = ipcp->fsm.bundle;

  if (Enabled(bundle, OPT_PROXY) || Enabled(bundle, OPT_PROXYALL))
    ipcp_proxyarp(ipcp, arp_SetProxy, addr);
}

/*
 * Called from iface_Clear() and iface_Delete() via ncp_IfaceAddrDeleted()
 */
void
ipcp_IfaceAddrDeleted(struct ipcp *ipcp, const struct iface_addr *addr)
{
  struct bundle *bundle = ipcp->fsm.bundle;

  if (Enabled(bundle, OPT_PROXY) || Enabled(bundle, OPT_PROXYALL))
    ipcp_proxyarp(ipcp, arp_ClearProxy, addr);
}

static void
IpcpLayerDown(struct fsm *fp)
{
  /* About to come down */
  struct ipcp *ipcp = fsm2ipcp(fp);
  static int recursing;
  char addr[16];

  if (!recursing++) {
    snprintf(addr, sizeof addr, "%s", inet_ntoa(ipcp->my_ip));
    log_Printf(LogIPCP, "%s: LayerDown: %s\n", fp->link->name, addr);

#ifndef NORADIUS
    radius_Flush(&fp->bundle->radius);
    radius_Account(&fp->bundle->radius, &fp->bundle->radacct,
                   fp->bundle->links, RAD_STOP, &ipcp->throughput);

    if (*fp->bundle->radius.cfg.file && fp->bundle->radius.filterid)
      system_Select(fp->bundle, fp->bundle->radius.filterid, LINKDOWNFILE,
                    NULL, NULL);
    radius_StopTimer(&fp->bundle->radius);
#endif

    /*
     * XXX this stuff should really live in the FSM.  Our config should
     * associate executable sections in files with events.
     */
    if (system_Select(fp->bundle, addr, LINKDOWNFILE, NULL, NULL) < 0) {
      if (bundle_GetLabel(fp->bundle)) {
         if (system_Select(fp->bundle, bundle_GetLabel(fp->bundle),
                          LINKDOWNFILE, NULL, NULL) < 0)
         system_Select(fp->bundle, "MYADDR", LINKDOWNFILE, NULL, NULL);
      } else
        system_Select(fp->bundle, "MYADDR", LINKDOWNFILE, NULL, NULL);
    }

    ipcp_Setup(ipcp, INADDR_NONE);
  }
  recursing--;
}

int
ipcp_InterfaceUp(struct ipcp *ipcp)
{
  if (!ipcp_SetIPaddress(ipcp, ipcp->my_ip, ipcp->peer_ip)) {
    log_Printf(LogERROR, "ipcp_InterfaceUp: unable to set ip address\n");
    return 0;
  }

  if (!iface_SetFlags(ipcp->fsm.bundle->iface->name, IFF_UP)) {
    log_Printf(LogERROR, "ipcp_InterfaceUp: Can't set the IFF_UP flag on %s\n",
               ipcp->fsm.bundle->iface->name);
    return 0;
  }

#ifndef NONAT
  if (ipcp->fsm.bundle->NatEnabled)
    LibAliasSetAddress(la, ipcp->my_ip);
#endif

  return 1;
}

static int
IpcpLayerUp(struct fsm *fp)
{
  /* We're now up */
  struct ipcp *ipcp = fsm2ipcp(fp);
  char tbuff[16];

  log_Printf(LogIPCP, "%s: LayerUp.\n", fp->link->name);
  snprintf(tbuff, sizeof tbuff, "%s", inet_ntoa(ipcp->my_ip));
  log_Printf(LogIPCP, "myaddr %s hisaddr = %s\n",
             tbuff, inet_ntoa(ipcp->peer_ip));

  if (ipcp->peer_compproto >> 16 == PROTO_VJCOMP)
    sl_compress_init(&ipcp->vj.cslc, (ipcp->peer_compproto >> 8) & 255);

  if (!ipcp_InterfaceUp(ipcp))
    return 0;

#ifndef NORADIUS
  radius_Account_Set_Ip(&fp->bundle->radacct, &ipcp->peer_ip, &ipcp->ifmask);
  radius_Account(&fp->bundle->radius, &fp->bundle->radacct, fp->bundle->links,
                 RAD_START, &ipcp->throughput);

  if (*fp->bundle->radius.cfg.file && fp->bundle->radius.filterid)
    system_Select(fp->bundle, fp->bundle->radius.filterid, LINKUPFILE,
                  NULL, NULL);
  radius_StartTimer(fp->bundle);
#endif

  /*
   * XXX this stuff should really live in the FSM.  Our config should
   * associate executable sections in files with events.
   */
  if (system_Select(fp->bundle, tbuff, LINKUPFILE, NULL, NULL) < 0) {
    if (bundle_GetLabel(fp->bundle)) {
      if (system_Select(fp->bundle, bundle_GetLabel(fp->bundle),
                       LINKUPFILE, NULL, NULL) < 0)
        system_Select(fp->bundle, "MYADDR", LINKUPFILE, NULL, NULL);
    } else
      system_Select(fp->bundle, "MYADDR", LINKUPFILE, NULL, NULL);
  }

  fp->more.reqs = fp->more.naks = fp->more.rejs = ipcp->cfg.fsm.maxreq * 3;
  log_DisplayPrompts();

  return 1;
}

static void
ipcp_ValidateReq(struct ipcp *ipcp, struct in_addr ip, struct fsm_decode *dec)
{
  struct bundle *bundle = ipcp->fsm.bundle;
  struct iface *iface = bundle->iface;
  struct in_addr myaddr, peer;
  unsigned n;

  if (iplist_isvalid(&ipcp->cfg.peer_list)) {
    ncprange_getip4addr(&ipcp->cfg.my_range, &myaddr);
    if (ip.s_addr == INADDR_ANY ||
        iplist_ip2pos(&ipcp->cfg.peer_list, ip) < 0 ||
        !ipcp_SetIPaddress(ipcp, myaddr, ip)) {
      log_Printf(LogIPCP, "%s: Address invalid or already in use\n",
                 inet_ntoa(ip));
      /*
       * If we've already had a valid address configured for the peer,
       * try NAKing with that so that we don't have to upset things
       * too much.
       */
      for (n = 0; n < iface->addrs; n++) {
        if (!ncpaddr_getip4(&iface->addr[n].peer, &peer))
          continue;
        if (iplist_ip2pos(&ipcp->cfg.peer_list, peer) >= 0) {
          ipcp->peer_ip = peer;
          break;
        }
      }

      if (n == iface->addrs) {
        /* Just pick an IP number from our list */
        ipcp->peer_ip = ChooseHisAddr(bundle, myaddr);
      }

      if (ipcp->peer_ip.s_addr == INADDR_ANY) {
        *dec->rejend++ = TY_IPADDR;
        *dec->rejend++ = 6;
        memcpy(dec->rejend, &ip.s_addr, 4);
        dec->rejend += 4;
      } else {
        *dec->nakend++ = TY_IPADDR;
        *dec->nakend++ = 6;
        memcpy(dec->nakend, &ipcp->peer_ip.s_addr, 4);
        dec->nakend += 4;
      }
      return;
    }
  } else if (ip.s_addr == INADDR_ANY ||
             !ncprange_containsip4(&ipcp->cfg.peer_range, ip)) {
    /*
     * If the destination address is not acceptable, NAK with what we
     * want to use.
     */
    *dec->nakend++ = TY_IPADDR;
    *dec->nakend++ = 6;
    for (n = 0; n < iface->addrs; n++)
      if (ncprange_contains(&ipcp->cfg.peer_range, &iface->addr[n].peer)) {
        /* We prefer the already-configured address */
        ncpaddr_getip4addr(&iface->addr[n].peer, (u_int32_t *)dec->nakend);
        break;
      }

    if (n == iface->addrs)
      memcpy(dec->nakend, &ipcp->peer_ip.s_addr, 4);

    dec->nakend += 4;
    return;
  }

  ipcp->peer_ip = ip;
  *dec->ackend++ = TY_IPADDR;
  *dec->ackend++ = 6;
  memcpy(dec->ackend, &ip.s_addr, 4);
  dec->ackend += 4;
}

static void
IpcpDecodeConfig(struct fsm *fp, u_char *cp, u_char *end, int mode_type,
                 struct fsm_decode *dec)
{
  /* Deal with incoming PROTO_IPCP */
  struct ncpaddr ncpaddr;
  struct ipcp *ipcp = fsm2ipcp(fp);
  int gotdnsnak;
  u_int32_t compproto;
  struct compreq pcomp;
  struct in_addr ipaddr, dstipaddr, have_ip;
  char tbuff[100], tbuff2[100];
  struct fsm_opt *opt, nak;

  gotdnsnak = 0;

  while (end - cp >= (int)sizeof(opt->hdr)) {
    if ((opt = fsm_readopt(&cp)) == NULL)
      break;

    snprintf(tbuff, sizeof tbuff, " %s[%d]", protoname(opt->hdr.id),
             opt->hdr.len);

    switch (opt->hdr.id) {
    case TY_IPADDR:		/* RFC1332 */
      memcpy(&ipaddr.s_addr, opt->data, 4);
      log_Printf(LogIPCP, "%s %s\n", tbuff, inet_ntoa(ipaddr));

      switch (mode_type) {
      case MODE_REQ:
        ipcp->peer_req = 1;
        ipcp_ValidateReq(ipcp, ipaddr, dec);
        break;

      case MODE_NAK:
        if (ncprange_containsip4(&ipcp->cfg.my_range, ipaddr)) {
          /* Use address suggested by peer */
          snprintf(tbuff2, sizeof tbuff2, "%s changing address: %s ", tbuff,
                   inet_ntoa(ipcp->my_ip));
          log_Printf(LogIPCP, "%s --> %s\n", tbuff2, inet_ntoa(ipaddr));
          ipcp->my_ip = ipaddr;
          ncpaddr_setip4(&ncpaddr, ipcp->my_ip);
          bundle_AdjustFilters(fp->bundle, &ncpaddr, NULL);
        } else {
          log_Printf(log_IsKept(LogIPCP) ? LogIPCP : LogPHASE,
                    "%s: Unacceptable address!\n", inet_ntoa(ipaddr));
          fsm_Close(&ipcp->fsm);
        }
        break;

      case MODE_REJ:
        ipcp->peer_reject |= (1 << opt->hdr.id);
        break;
      }
      break;

    case TY_COMPPROTO:
      memcpy(&pcomp, opt->data, sizeof pcomp);
      compproto = (ntohs(pcomp.proto) << 16) + ((int)pcomp.slots << 8) +
                  pcomp.compcid;
      log_Printf(LogIPCP, "%s %s\n", tbuff, vj2asc(compproto));

      switch (mode_type) {
      case MODE_REQ:
        if (!IsAccepted(ipcp->cfg.vj.neg))
          fsm_rej(dec, opt);
        else {
          switch (opt->hdr.len) {
          case 4:		/* RFC1172 */
            if (ntohs(pcomp.proto) == PROTO_VJCOMP) {
              log_Printf(LogWARN, "Peer is speaking RFC1172 compression "
                         "protocol !\n");
              ipcp->heis1172 = 1;
              ipcp->peer_compproto = compproto;
              fsm_ack(dec, opt);
            } else {
              pcomp.proto = htons(PROTO_VJCOMP);
              nak.hdr.id = TY_COMPPROTO;
              nak.hdr.len = 4;
              memcpy(nak.data, &pcomp, 2);
              fsm_nak(dec, &nak);
            }
            break;
          case 6:		/* RFC1332 */
            if (ntohs(pcomp.proto) == PROTO_VJCOMP) {
	      /* We know pcomp.slots' max value == MAX_VJ_STATES */
              if (pcomp.slots >= MIN_VJ_STATES) {
                /* Ok, we can do that */
                ipcp->peer_compproto = compproto;
                ipcp->heis1172 = 0;
                fsm_ack(dec, opt);
              } else {
                /* Get as close as we can to what he wants */
                ipcp->heis1172 = 0;
                pcomp.slots = MIN_VJ_STATES;
                nak.hdr.id = TY_COMPPROTO;
                nak.hdr.len = 4;
                memcpy(nak.data, &pcomp, 2);
                fsm_nak(dec, &nak);
              }
            } else {
              /* What we really want */
              pcomp.proto = htons(PROTO_VJCOMP);
              pcomp.slots = DEF_VJ_STATES;
              pcomp.compcid = 1;
              nak.hdr.id = TY_COMPPROTO;
              nak.hdr.len = 6;
              memcpy(nak.data, &pcomp, sizeof pcomp);
              fsm_nak(dec, &nak);
            }
            break;
          default:
            fsm_rej(dec, opt);
            break;
          }
        }
        break;

      case MODE_NAK:
        if (ntohs(pcomp.proto) == PROTO_VJCOMP) {
	  /* We know pcomp.slots' max value == MAX_VJ_STATES */
          if (pcomp.slots < MIN_VJ_STATES)
            pcomp.slots = MIN_VJ_STATES;
          compproto = (ntohs(pcomp.proto) << 16) + (pcomp.slots << 8) +
                      pcomp.compcid;
        } else
          compproto = 0;
        log_Printf(LogIPCP, "%s changing compproto: %08x --> %08x\n",
                   tbuff, ipcp->my_compproto, compproto);
        ipcp->my_compproto = compproto;
        break;

      case MODE_REJ:
        ipcp->peer_reject |= (1 << opt->hdr.id);
        break;
      }
      break;

    case TY_IPADDRS:		/* RFC1172 */
      memcpy(&ipaddr.s_addr, opt->data, 4);
      memcpy(&dstipaddr.s_addr, opt->data + 4, 4);
      snprintf(tbuff2, sizeof tbuff2, "%s %s,", tbuff, inet_ntoa(ipaddr));
      log_Printf(LogIPCP, "%s %s\n", tbuff2, inet_ntoa(dstipaddr));

      switch (mode_type) {
      case MODE_REQ:
        fsm_rej(dec, opt);
        break;

      case MODE_NAK:
      case MODE_REJ:
        break;
      }
      break;

    case TY_PRIMARY_DNS:	/* DNS negotiation (rfc1877) */
    case TY_SECONDARY_DNS:
      memcpy(&ipaddr.s_addr, opt->data, 4);
      log_Printf(LogIPCP, "%s %s\n", tbuff, inet_ntoa(ipaddr));

      switch (mode_type) {
      case MODE_REQ:
        if (!IsAccepted(ipcp->cfg.ns.dns_neg)) {
          ipcp->my_reject |= (1 << (opt->hdr.id - TY_ADJUST_NS));
          fsm_rej(dec, opt);
          break;
        }
        have_ip = ipcp->ns.dns[opt->hdr.id == TY_PRIMARY_DNS ? 0 : 1];

        if (opt->hdr.id == TY_PRIMARY_DNS && ipaddr.s_addr != have_ip.s_addr &&
            ipaddr.s_addr == ipcp->ns.dns[1].s_addr) {
          /* Swap 'em 'round */
          ipcp->ns.dns[0] = ipcp->ns.dns[1];
          ipcp->ns.dns[1] = have_ip;
          have_ip = ipcp->ns.dns[0];
        }

        if (ipaddr.s_addr != have_ip.s_addr) {
          /*
           * The client has got the DNS stuff wrong (first request) so
           * we'll tell 'em how it is
           */
          nak.hdr.id = opt->hdr.id;
          nak.hdr.len = 6;
          memcpy(nak.data, &have_ip.s_addr, 4);
          fsm_nak(dec, &nak);
        } else {
          /*
           * Otherwise they have it right (this time) so we send an ack packet
           * back confirming it... end of story
           */
          fsm_ack(dec, opt);
        }
        break;

      case MODE_NAK:
        if (IsEnabled(ipcp->cfg.ns.dns_neg)) {
          gotdnsnak = 1;
          memcpy(&ipcp->ns.dns[opt->hdr.id == TY_PRIMARY_DNS ? 0 : 1].s_addr,
                 opt->data, 4);
        }
        break;

      case MODE_REJ:		/* Can't do much, stop asking */
        ipcp->peer_reject |= (1 << (opt->hdr.id - TY_ADJUST_NS));
        break;
      }
      break;

    case TY_PRIMARY_NBNS:	/* M$ NetBIOS nameserver hack (rfc1877) */
    case TY_SECONDARY_NBNS:
      memcpy(&ipaddr.s_addr, opt->data, 4);
      log_Printf(LogIPCP, "%s %s\n", tbuff, inet_ntoa(ipaddr));

      switch (mode_type) {
      case MODE_REQ:
        have_ip.s_addr =
          ipcp->cfg.ns.nbns[opt->hdr.id == TY_PRIMARY_NBNS ? 0 : 1].s_addr;

        if (have_ip.s_addr == INADDR_ANY) {
          log_Printf(LogIPCP, "NBNS REQ - rejected - nbns not set\n");
          ipcp->my_reject |= (1 << (opt->hdr.id - TY_ADJUST_NS));
          fsm_rej(dec, opt);
          break;
        }

        if (ipaddr.s_addr != have_ip.s_addr) {
          nak.hdr.id = opt->hdr.id;
          nak.hdr.len = 6;
          memcpy(nak.data, &have_ip.s_addr, 4);
          fsm_nak(dec, &nak);
        } else
          fsm_ack(dec, opt);
        break;

      case MODE_NAK:
        log_Printf(LogIPCP, "MS NBNS req %d - NAK??\n", opt->hdr.id);
        break;

      case MODE_REJ:
        log_Printf(LogIPCP, "MS NBNS req %d - REJ??\n", opt->hdr.id);
        break;
      }
      break;

    default:
      if (mode_type != MODE_NOP) {
        ipcp->my_reject |= (1 << opt->hdr.id);
        fsm_rej(dec, opt);
      }
      break;
    }
  }

  if (gotdnsnak) {
    if (ipcp->ns.writable) {
      log_Printf(LogDEBUG, "Updating resolver\n");
      if (!ipcp_WriteDNS(ipcp)) {
        ipcp->peer_reject |= (1 << (TY_PRIMARY_DNS - TY_ADJUST_NS));
        ipcp->peer_reject |= (1 << (TY_SECONDARY_DNS - TY_ADJUST_NS));
      } else
        bundle_AdjustDNS(fp->bundle);
    } else {
      log_Printf(LogDEBUG, "Not updating resolver (readonly)\n");
      bundle_AdjustDNS(fp->bundle);
    }
  }

  if (mode_type != MODE_NOP) {
    if (mode_type == MODE_REQ && !ipcp->peer_req) {
      if (dec->rejend == dec->rej && dec->nakend == dec->nak) {
        /*
         * Pretend the peer has requested an IP.
         * We do this to ensure that we only send one NAK if the only
         * reason for the NAK is because the peer isn't sending a
         * TY_IPADDR REQ.  This stops us from repeatedly trying to tell
         * the peer that we have to have an IP address on their end.
         */
        ipcp->peer_req = 1;
      }
      ipaddr.s_addr = INADDR_ANY;
      ipcp_ValidateReq(ipcp, ipaddr, dec);
    }
    fsm_opt_normalise(dec);
  }
}

extern struct mbuf *
ipcp_Input(struct bundle *bundle, struct link *l, struct mbuf *bp)
{
  /* Got PROTO_IPCP from link */
  m_settype(bp, MB_IPCPIN);
  if (bundle_Phase(bundle) == PHASE_NETWORK)
    fsm_Input(&bundle->ncp.ipcp.fsm, bp);
  else {
    if (bundle_Phase(bundle) < PHASE_NETWORK)
      log_Printf(LogIPCP, "%s: Error: Unexpected IPCP in phase %s (ignored)\n",
                 l->name, bundle_PhaseName(bundle));
    m_freem(bp);
  }
  return NULL;
}

int
ipcp_UseHisIPaddr(struct bundle *bundle, struct in_addr hisaddr)
{
  struct ipcp *ipcp = &bundle->ncp.ipcp;
  struct in_addr myaddr;

  memset(&ipcp->cfg.peer_range, '\0', sizeof ipcp->cfg.peer_range);
  iplist_reset(&ipcp->cfg.peer_list);
  ipcp->peer_ip = hisaddr;
  ncprange_setip4host(&ipcp->cfg.peer_range, hisaddr);
  ncprange_getip4addr(&ipcp->cfg.my_range, &myaddr);

  return ipcp_SetIPaddress(ipcp, myaddr, hisaddr);
}

int
ipcp_UseHisaddr(struct bundle *bundle, const char *hisaddr, int setaddr)
{
  struct in_addr myaddr;
  struct ncp *ncp = &bundle->ncp;
  struct ipcp *ipcp = &ncp->ipcp;
  struct ncpaddr ncpaddr;

  /* Use `hisaddr' for the peers address (set iface if `setaddr') */
  memset(&ipcp->cfg.peer_range, '\0', sizeof ipcp->cfg.peer_range);
  iplist_reset(&ipcp->cfg.peer_list);
  if (strpbrk(hisaddr, ",-")) {
    iplist_setsrc(&ipcp->cfg.peer_list, hisaddr);
    if (iplist_isvalid(&ipcp->cfg.peer_list)) {
      iplist_setrandpos(&ipcp->cfg.peer_list);
      ipcp->peer_ip = ChooseHisAddr(bundle, ipcp->my_ip);
      if (ipcp->peer_ip.s_addr == INADDR_ANY) {
        log_Printf(LogWARN, "%s: None available !\n", ipcp->cfg.peer_list.src);
        return 0;
      }
      ncprange_setip4host(&ipcp->cfg.peer_range, ipcp->peer_ip);
    } else {
      log_Printf(LogWARN, "%s: Invalid range !\n", hisaddr);
      return 0;
    }
  } else if (ncprange_aton(&ipcp->cfg.peer_range, ncp, hisaddr) != 0) {
    if (ncprange_family(&ipcp->cfg.my_range) != AF_INET) {
      log_Printf(LogWARN, "%s: Not an AF_INET address !\n", hisaddr);
      return 0;
    }
    ncprange_getip4addr(&ipcp->cfg.my_range, &myaddr);
    ncprange_getip4addr(&ipcp->cfg.peer_range, &ipcp->peer_ip);

    if (setaddr && !ipcp_SetIPaddress(ipcp, myaddr, ipcp->peer_ip))
      return 0;
  } else
    return 0;

  ncpaddr_setip4(&ncpaddr, ipcp->peer_ip);
  bundle_AdjustFilters(bundle, NULL, &ncpaddr);

  return 1;	/* Ok */
}

struct in_addr
addr2mask(struct in_addr addr)
{
  u_int32_t haddr = ntohl(addr.s_addr);

  haddr = IN_CLASSA(haddr) ? IN_CLASSA_NET :
          IN_CLASSB(haddr) ? IN_CLASSB_NET :
          IN_CLASSC_NET;
  addr.s_addr = htonl(haddr);

  return addr;
}

size_t
ipcp_QueueLen(struct ipcp *ipcp)
{
  struct mqueue *q;
  size_t result;

  result = 0;
  for (q = ipcp->Queue; q < ipcp->Queue + IPCP_QUEUES(ipcp); q++)
    result += q->len;

  return result;
}

int
ipcp_PushPacket(struct ipcp *ipcp, struct link *l)
{
  struct bundle *bundle = ipcp->fsm.bundle;
  struct mqueue *queue;
  struct mbuf *bp;
  int m_len;
  u_int32_t secs = 0;
  unsigned alivesecs = 0;

  if (ipcp->fsm.state != ST_OPENED)
    return 0;

  /*
   * If ccp is not open but is required, do nothing.
   */
  if (l->ccp.fsm.state != ST_OPENED && ccp_Required(&l->ccp)) {
    log_Printf(LogPHASE, "%s: Not transmitting... waiting for CCP\n", l->name);
    return 0;
  }

  queue = ipcp->Queue + IPCP_QUEUES(ipcp) - 1;
  do {
    if (queue->top) {
      bp = m_dequeue(queue);
      bp = mbuf_Read(bp, &secs, sizeof secs);
      bp = m_pullup(bp);
      m_len = m_length(bp);
      if (!FilterCheck(MBUF_CTOP(bp), AF_INET, &bundle->filter.alive,
                       &alivesecs)) {
        if (secs == 0)
          secs = alivesecs;
        bundle_StartIdleTimer(bundle, secs);
      }
      link_PushPacket(l, bp, bundle, 0, PROTO_IP);
      ipcp_AddOutOctets(ipcp, m_len);
      return 1;
    }
  } while (queue-- != ipcp->Queue);

  return 0;
}
