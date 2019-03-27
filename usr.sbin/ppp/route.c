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
#include <sys/socket.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if_dl.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/un.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <termios.h>
#include <unistd.h>

#include "layer.h"
#include "defs.h"
#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "iplist.h"
#include "timer.h"
#include "throughput.h"
#include "lqr.h"
#include "hdlc.h"
#include "fsm.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "slcompress.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "filter.h"
#include "descriptor.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "route.h"
#include "prompt.h"
#include "iface.h"
#include "id.h"


static void
p_sockaddr(struct prompt *prompt, struct sockaddr *phost,
           struct sockaddr *pmask, int width)
{
  struct ncprange range;
  char buf[29];
  struct sockaddr_dl *dl = (struct sockaddr_dl *)phost;

  if (log_IsKept(LogDEBUG)) {
    char tmp[50];

    log_Printf(LogDEBUG, "Found the following sockaddr:\n");
    log_Printf(LogDEBUG, "  Family %d, len %d\n",
               (int)phost->sa_family, (int)phost->sa_len);
    inet_ntop(phost->sa_family, phost->sa_data, tmp, sizeof tmp);
    log_Printf(LogDEBUG, "  Addr %s\n", tmp);
    if (pmask) {
      inet_ntop(pmask->sa_family, pmask->sa_data, tmp, sizeof tmp);
      log_Printf(LogDEBUG, "  Mask %s\n", tmp);
    }
  }

  switch (phost->sa_family) {
  case AF_INET:
#ifndef NOINET6
  case AF_INET6:
#endif
    ncprange_setsa(&range, phost, pmask);
    if (ncprange_isdefault(&range))
      prompt_Printf(prompt, "%-*s ", width - 1, "default");
    else
      prompt_Printf(prompt, "%-*s ", width - 1, ncprange_ntoa(&range));
    return;

  case AF_LINK:
    if (dl->sdl_nlen)
      snprintf(buf, sizeof buf, "%.*s", dl->sdl_nlen, dl->sdl_data);
    else if (dl->sdl_alen) {
      if (dl->sdl_type == IFT_ETHER) {
        if (dl->sdl_alen < sizeof buf / 3) {
          int f;
          u_char *MAC;

          MAC = (u_char *)dl->sdl_data + dl->sdl_nlen;
          for (f = 0; f < dl->sdl_alen; f++)
            sprintf(buf+f*3, "%02x:", MAC[f]);
          buf[f*3-1] = '\0';
        } else
          strcpy(buf, "??:??:??:??:??:??");
      } else
        sprintf(buf, "<IFT type %d>", dl->sdl_type);
    }  else if (dl->sdl_slen)
      sprintf(buf, "<slen %d?>", dl->sdl_slen);
    else
      sprintf(buf, "link#%d", dl->sdl_index);
    break;

  default:
    sprintf(buf, "<AF type %d>", phost->sa_family);
    break;
  }

  prompt_Printf(prompt, "%-*s ", width-1, buf);
}

static struct bits {
  u_int32_t b_mask;
  char b_val;
} bits[] = {
  { RTF_UP, 'U' },
  { RTF_GATEWAY, 'G' },
  { RTF_HOST, 'H' },
  { RTF_REJECT, 'R' },
  { RTF_DYNAMIC, 'D' },
  { RTF_MODIFIED, 'M' },
  { RTF_DONE, 'd' },
  { RTF_XRESOLVE, 'X' },
  { RTF_STATIC, 'S' },
  { RTF_PROTO1, '1' },
  { RTF_PROTO2, '2' },
  { RTF_BLACKHOLE, 'B' },
#ifdef RTF_LLINFO
  { RTF_LLINFO, 'L' },
#endif
#ifdef RTF_CLONING  
  { RTF_CLONING, 'C' },
#endif
#ifdef RTF_PROTO3
  { RTF_PROTO3, '3' },
#endif
#ifdef RTF_BROADCAST
  { RTF_BROADCAST, 'b' },
#endif
  { 0, '\0' }
};

static void
p_flags(struct prompt *prompt, u_int32_t f, unsigned max)
{
  char name[33], *flags;
  register struct bits *p = bits;

  if (max > sizeof name - 1)
    max = sizeof name - 1;

  for (flags = name; p->b_mask && flags - name < (int)max; p++)
    if (p->b_mask & f)
      *flags++ = p->b_val;
  *flags = '\0';
  prompt_Printf(prompt, "%-*.*s", (int)max, (int)max, name);
}

static int route_nifs = -1;

const char *
Index2Nam(int idx)
{
  /*
   * XXX: Maybe we should select() on the routing socket so that we can
   *      notice interfaces that come & go (PCCARD support).
   *      Or we could even support a signal that resets these so that
   *      the PCCARD insert/remove events can signal ppp.
   */
  static char **ifs;		/* Figure these out once */
  static int debug_done;	/* Debug once */

  if (idx > route_nifs || (idx > 0 && ifs[idx-1] == NULL)) {
    int mib[6], have, had;
    size_t needed;
    char *buf, *ptr, *end;
    struct sockaddr_dl *dl;
    struct if_msghdr *ifm;

    if (ifs) {
      free(ifs);
      ifs = NULL;
      route_nifs = 0;
    }
    debug_done = 0;

    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;
    mib[3] = 0;
    mib[4] = NET_RT_IFLIST;
    mib[5] = 0;

    if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0) {
      log_Printf(LogERROR, "Index2Nam: sysctl: estimate: %s\n",
                 strerror(errno));
      return NumStr(idx, NULL, 0);
    }
    if ((buf = malloc(needed)) == NULL)
      return NumStr(idx, NULL, 0);
    if (sysctl(mib, 6, buf, &needed, NULL, 0) < 0) {
      free(buf);
      return NumStr(idx, NULL, 0);
    }
    end = buf + needed;

    have = 0;
    for (ptr = buf; ptr < end; ptr += ifm->ifm_msglen) {
      ifm = (struct if_msghdr *)ptr;
      if (ifm->ifm_type != RTM_IFINFO)
        continue;
      dl = (struct sockaddr_dl *)(ifm + 1);
      if (ifm->ifm_index > 0) {
        if (ifm->ifm_index > have) {
          char **newifs;

          had = have;
          have = ifm->ifm_index + 5;
          if (had)
            newifs = (char **)realloc(ifs, sizeof(char *) * have);
          else
            newifs = (char **)malloc(sizeof(char *) * have);
          if (!newifs) {
            log_Printf(LogDEBUG, "Index2Nam: %s\n", strerror(errno));
            route_nifs = 0;
            if (ifs) {
              free(ifs);
              ifs = NULL;
            }
            free(buf);
            return NumStr(idx, NULL, 0);
          }
          ifs = newifs;
          memset(ifs + had, '\0', sizeof(char *) * (have - had));
        }
        if (ifs[ifm->ifm_index-1] == NULL) {
          ifs[ifm->ifm_index-1] = (char *)malloc(dl->sdl_nlen+1);
          if (ifs[ifm->ifm_index-1] == NULL)
	    log_Printf(LogDEBUG, "Skipping interface %d: Out of memory\n",
                  ifm->ifm_index);
	  else {
	    memcpy(ifs[ifm->ifm_index-1], dl->sdl_data, dl->sdl_nlen);
	    ifs[ifm->ifm_index-1][dl->sdl_nlen] = '\0';
	    if (route_nifs < ifm->ifm_index)
	      route_nifs = ifm->ifm_index;
	  }
        }
      } else if (log_IsKept(LogDEBUG))
        log_Printf(LogDEBUG, "Skipping out-of-range interface %d!\n",
                  ifm->ifm_index);
    }
    free(buf);
  }

  if (log_IsKept(LogDEBUG) && !debug_done) {
    int f;

    log_Printf(LogDEBUG, "Found the following interfaces:\n");
    for (f = 0; f < route_nifs; f++)
      if (ifs[f] != NULL)
        log_Printf(LogDEBUG, " Index %d, name \"%s\"\n", f+1, ifs[f]);
    debug_done = 1;
  }

  if (idx < 1 || idx > route_nifs || ifs[idx-1] == NULL)
    return NumStr(idx, NULL, 0);

  return ifs[idx-1];
}

void
route_ParseHdr(struct rt_msghdr *rtm, struct sockaddr *sa[RTAX_MAX])
{
  char *wp;
  int rtax;

  wp = (char *)(rtm + 1);

  for (rtax = 0; rtax < RTAX_MAX; rtax++)
    if (rtm->rtm_addrs & (1 << rtax)) {
      sa[rtax] = (struct sockaddr *)wp;
      wp += ROUNDUP(sa[rtax]->sa_len);
      if (sa[rtax]->sa_family == 0)
        sa[rtax] = NULL;	/* ??? */
    } else
      sa[rtax] = NULL;
}

int
route_Show(struct cmdargs const *arg)
{
  struct rt_msghdr *rtm;
  struct sockaddr *sa[RTAX_MAX];
  char *sp, *ep, *cp;
  size_t needed;
  int mib[6];

  mib[0] = CTL_NET;
  mib[1] = PF_ROUTE;
  mib[2] = 0;
  mib[3] = 0;
  mib[4] = NET_RT_DUMP;
  mib[5] = 0;
  if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0) {
    log_Printf(LogERROR, "route_Show: sysctl: estimate: %s\n", strerror(errno));
    return (1);
  }
  sp = malloc(needed);
  if (sp == NULL)
    return (1);
  if (sysctl(mib, 6, sp, &needed, NULL, 0) < 0) {
    log_Printf(LogERROR, "route_Show: sysctl: getroute: %s\n", strerror(errno));
    free(sp);
    return (1);
  }
  ep = sp + needed;

  prompt_Printf(arg->prompt, "%-20s%-20sFlags  Netif\n",
                "Destination", "Gateway");
  for (cp = sp; cp < ep; cp += rtm->rtm_msglen) {
    rtm = (struct rt_msghdr *)cp;

    route_ParseHdr(rtm, sa);

    if (sa[RTAX_DST] && sa[RTAX_GATEWAY]) {
      p_sockaddr(arg->prompt, sa[RTAX_DST], sa[RTAX_NETMASK], 20);
      p_sockaddr(arg->prompt, sa[RTAX_GATEWAY], NULL, 20);

      p_flags(arg->prompt, rtm->rtm_flags, 6);
      prompt_Printf(arg->prompt, " %s\n", Index2Nam(rtm->rtm_index));
    } else
      prompt_Printf(arg->prompt, "<can't parse routing entry>\n");
  }
  free(sp);
  return 0;
}

/*
 *  Delete routes associated with our interface
 */
void
route_IfDelete(struct bundle *bundle, int all)
{
  struct rt_msghdr *rtm;
  struct sockaddr *sa[RTAX_MAX];
  struct ncprange range;
  int pass;
  size_t needed;
  char *sp, *cp, *ep;
  int mib[6];

  log_Printf(LogDEBUG, "route_IfDelete (%d)\n", bundle->iface->index);

  mib[0] = CTL_NET;
  mib[1] = PF_ROUTE;
  mib[2] = 0;
  mib[3] = 0;
  mib[4] = NET_RT_DUMP;
  mib[5] = 0;
  if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0) {
    log_Printf(LogERROR, "route_IfDelete: sysctl: estimate: %s\n",
              strerror(errno));
    return;
  }

  sp = malloc(needed);
  if (sp == NULL)
    return;

  if (sysctl(mib, 6, sp, &needed, NULL, 0) < 0) {
    log_Printf(LogERROR, "route_IfDelete: sysctl: getroute: %s\n",
              strerror(errno));
    free(sp);
    return;
  }
  ep = sp + needed;

  for (pass = 0; pass < 2; pass++) {
    /*
     * We do 2 passes.  The first deletes all cloned routes.  The second
     * deletes all non-cloned routes.  This is done to avoid
     * potential errors from trying to delete route X after route Y where
     * route X was cloned from route Y (and is no longer there 'cos it
     * may have gone with route Y).
     */
    if (pass == 0)
      /* So we can't tell ! */
      continue;
    for (cp = sp; cp < ep; cp += rtm->rtm_msglen) {
      rtm = (struct rt_msghdr *)cp;
      route_ParseHdr(rtm, sa);
      if (rtm->rtm_index == bundle->iface->index &&
          sa[RTAX_DST] && sa[RTAX_GATEWAY] &&
          (sa[RTAX_DST]->sa_family == AF_INET
#ifndef NOINET6
           || sa[RTAX_DST]->sa_family == AF_INET6
#endif
           ) &&
          (all || (rtm->rtm_flags & RTF_GATEWAY))) {
        if (log_IsKept(LogDEBUG)) {
          char gwstr[NCP_ASCIIBUFFERSIZE];
          struct ncpaddr gw;
          ncprange_setsa(&range, sa[RTAX_DST], sa[RTAX_NETMASK]);
          ncpaddr_setsa(&gw, sa[RTAX_GATEWAY]);
          snprintf(gwstr, sizeof gwstr, "%s", ncpaddr_ntoa(&gw));
          log_Printf(LogDEBUG, "Found %s %s\n", ncprange_ntoa(&range), gwstr);
        }
        if (sa[RTAX_GATEWAY]->sa_family == AF_INET ||
#ifndef NOINET6
            sa[RTAX_GATEWAY]->sa_family == AF_INET6 ||
#endif
            sa[RTAX_GATEWAY]->sa_family == AF_LINK) {
          if (pass == 1) {
            ncprange_setsa(&range, sa[RTAX_DST], sa[RTAX_NETMASK]);
            rt_Set(bundle, RTM_DELETE, &range, NULL, 0, 0);
          } else
            log_Printf(LogDEBUG, "route_IfDelete: Skip it (pass %d)\n", pass);
        } else
          log_Printf(LogDEBUG,
                    "route_IfDelete: Can't remove routes for family %d\n",
                    sa[RTAX_GATEWAY]->sa_family);
      }
    }
  }
  free(sp);
}


/*
 *  Update the MTU on all routes for the given interface
 */
void
route_UpdateMTU(struct bundle *bundle)
{
  struct rt_msghdr *rtm;
  struct sockaddr *sa[RTAX_MAX];
  struct ncprange dst;
  size_t needed;
  char *sp, *cp, *ep;
  int mib[6];

  log_Printf(LogDEBUG, "route_UpdateMTU (%d)\n", bundle->iface->index);

  mib[0] = CTL_NET;
  mib[1] = PF_ROUTE;
  mib[2] = 0;
  mib[3] = 0;
  mib[4] = NET_RT_DUMP;
  mib[5] = 0;
  if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0) {
    log_Printf(LogERROR, "route_IfDelete: sysctl: estimate: %s\n",
              strerror(errno));
    return;
  }

  sp = malloc(needed);
  if (sp == NULL)
    return;

  if (sysctl(mib, 6, sp, &needed, NULL, 0) < 0) {
    log_Printf(LogERROR, "route_IfDelete: sysctl: getroute: %s\n",
              strerror(errno));
    free(sp);
    return;
  }
  ep = sp + needed;

  for (cp = sp; cp < ep; cp += rtm->rtm_msglen) {
    rtm = (struct rt_msghdr *)cp;
    route_ParseHdr(rtm, sa);
    if (sa[RTAX_DST] && (sa[RTAX_DST]->sa_family == AF_INET
#ifndef NOINET6
                         || sa[RTAX_DST]->sa_family == AF_INET6
#endif
                        ) &&
        sa[RTAX_GATEWAY] && rtm->rtm_index == bundle->iface->index) {
      if (log_IsKept(LogTCPIP)) {
        ncprange_setsa(&dst, sa[RTAX_DST], sa[RTAX_NETMASK]);
        log_Printf(LogTCPIP, "route_UpdateMTU: Netif: %d (%s), dst %s,"
                   " mtu %lu\n", rtm->rtm_index, Index2Nam(rtm->rtm_index),
                   ncprange_ntoa(&dst), bundle->iface->mtu);
      }
      rt_Update(bundle, sa[RTAX_DST], sa[RTAX_GATEWAY], sa[RTAX_NETMASK],
                sa[RTAX_IFP], sa[RTAX_IFA]);
    }
  }

  free(sp);
}

int
GetIfIndex(char *name)
{
  int idx;

  idx = 1;
  while (route_nifs == -1 || idx < route_nifs)
    if (strcmp(Index2Nam(idx), name) == 0)
      return idx;
    else
      idx++;
  return -1;
}

void
route_Change(struct bundle *bundle, struct sticky_route *r,
             const struct ncpaddr *me, const struct ncpaddr *peer)
{
  struct ncpaddr dst;

  for (; r; r = r->next) {
    ncprange_getaddr(&r->dst, &dst);
    if (ncpaddr_family(me) == AF_INET) {
      if ((r->type & ROUTE_DSTMYADDR) && !ncpaddr_equal(&dst, me)) {
        rt_Set(bundle, RTM_DELETE, &r->dst, NULL, 1, 0);
        ncprange_sethost(&r->dst, me);
        if (r->type & ROUTE_GWHISADDR)
          ncpaddr_copy(&r->gw, peer);
      } else if ((r->type & ROUTE_DSTHISADDR) && !ncpaddr_equal(&dst, peer)) {
        rt_Set(bundle, RTM_DELETE, &r->dst, NULL, 1, 0);
        ncprange_sethost(&r->dst, peer);
        if (r->type & ROUTE_GWHISADDR)
          ncpaddr_copy(&r->gw, peer);
      } else if ((r->type & ROUTE_DSTDNS0) && !ncpaddr_equal(&dst, peer)) {
        if (bundle->ncp.ipcp.ns.dns[0].s_addr == INADDR_NONE)
          continue;
        rt_Set(bundle, RTM_DELETE, &r->dst, NULL, 1, 0);
        if (r->type & ROUTE_GWHISADDR)
          ncpaddr_copy(&r->gw, peer);
      } else if ((r->type & ROUTE_DSTDNS1) && !ncpaddr_equal(&dst, peer)) {
        if (bundle->ncp.ipcp.ns.dns[1].s_addr == INADDR_NONE)
          continue;
        rt_Set(bundle, RTM_DELETE, &r->dst, NULL, 1, 0);
        if (r->type & ROUTE_GWHISADDR)
          ncpaddr_copy(&r->gw, peer);
      } else if ((r->type & ROUTE_GWHISADDR) && !ncpaddr_equal(&r->gw, peer))
        ncpaddr_copy(&r->gw, peer);
#ifndef NOINET6
    } else if (ncpaddr_family(me) == AF_INET6) {
      if ((r->type & ROUTE_DSTMYADDR6) && !ncpaddr_equal(&dst, me)) {
        rt_Set(bundle, RTM_DELETE, &r->dst, NULL, 1, 0);
        ncprange_sethost(&r->dst, me);
        if (r->type & ROUTE_GWHISADDR)
          ncpaddr_copy(&r->gw, peer);
      } else if ((r->type & ROUTE_DSTHISADDR6) && !ncpaddr_equal(&dst, peer)) {
        rt_Set(bundle, RTM_DELETE, &r->dst, NULL, 1, 0);
        ncprange_sethost(&r->dst, peer);
        if (r->type & ROUTE_GWHISADDR)
          ncpaddr_copy(&r->gw, peer);
      } else if ((r->type & ROUTE_GWHISADDR6) && !ncpaddr_equal(&r->gw, peer))
        ncpaddr_copy(&r->gw, peer);
#endif
    }
    rt_Set(bundle, RTM_ADD, &r->dst, &r->gw, 1, 0);
  }
}

void
route_Add(struct sticky_route **rp, int type, const struct ncprange *dst,
          const struct ncpaddr *gw)
{
  struct sticky_route *r;
  int dsttype = type & ROUTE_DSTANY;

  r = NULL;
  while (*rp) {
    if ((dsttype && dsttype == ((*rp)->type & ROUTE_DSTANY)) ||
        (!dsttype && ncprange_equal(&(*rp)->dst, dst))) {
      /* Oops, we already have this route - unlink it */
      free(r);			/* impossible really  */
      r = *rp;
      *rp = r->next;
    } else
      rp = &(*rp)->next;
  }

  if (r == NULL) {
    r = (struct sticky_route *)malloc(sizeof(struct sticky_route));
    if (r == NULL) {
      log_Printf(LogERROR, "route_Add: Out of memory!\n");
      return;
    }
  }
  r->type = type;
  r->next = NULL;
  ncprange_copy(&r->dst, dst);
  ncpaddr_copy(&r->gw, gw);
  *rp = r;
}

void
route_Delete(struct sticky_route **rp, int type, const struct ncprange *dst)
{
  struct sticky_route *r;
  int dsttype = type & ROUTE_DSTANY;

  for (; *rp; rp = &(*rp)->next) {
    if ((dsttype && dsttype == ((*rp)->type & ROUTE_DSTANY)) ||
        (!dsttype && ncprange_equal(dst, &(*rp)->dst))) {
      r = *rp;
      *rp = r->next;
      free(r);
      break;
    }
  }
}

void
route_DeleteAll(struct sticky_route **rp)
{
  struct sticky_route *r, *rn;

  for (r = *rp; r; r = rn) {
    rn = r->next;
    free(r);
  }
  *rp = NULL;
}

void
route_ShowSticky(struct prompt *p, struct sticky_route *r, const char *tag,
                 int indent)
{
  int tlen = strlen(tag);

  if (tlen + 2 > indent)
    prompt_Printf(p, "%s:\n%*s", tag, indent, "");
  else
    prompt_Printf(p, "%s:%*s", tag, indent - tlen - 1, "");

  for (; r; r = r->next) {
    prompt_Printf(p, "%*sadd ", tlen ? 0 : indent, "");
    tlen = 0;
    if (r->type & ROUTE_DSTMYADDR)
      prompt_Printf(p, "MYADDR");
    else if (r->type & ROUTE_DSTMYADDR6)
      prompt_Printf(p, "MYADDR6");
    else if (r->type & ROUTE_DSTHISADDR)
      prompt_Printf(p, "HISADDR");
    else if (r->type & ROUTE_DSTHISADDR6)
      prompt_Printf(p, "HISADDR6");
    else if (r->type & ROUTE_DSTDNS0)
      prompt_Printf(p, "DNS0");
    else if (r->type & ROUTE_DSTDNS1)
      prompt_Printf(p, "DNS1");
    else if (ncprange_isdefault(&r->dst))
      prompt_Printf(p, "default");
    else
      prompt_Printf(p, "%s", ncprange_ntoa(&r->dst));

    if (r->type & ROUTE_GWHISADDR)
      prompt_Printf(p, " HISADDR\n");
    else if (r->type & ROUTE_GWHISADDR6)
      prompt_Printf(p, " HISADDR6\n");
    else
      prompt_Printf(p, " %s\n", ncpaddr_ntoa(&r->gw));
  }
}

struct rtmsg {
  struct rt_msghdr m_rtm;
  char m_space[256];
};

static size_t
memcpy_roundup(char *cp, const void *data, size_t len)
{
  size_t padlen;

  padlen = ROUNDUP(len);
  memcpy(cp, data, len);
  if (padlen > len)
    memset(cp + len, '\0', padlen - len);

  return padlen;
}

#if defined(__KAME__) && !defined(NOINET6)
static void
add_scope(struct sockaddr *sa, int ifindex)
{
  struct sockaddr_in6 *sa6;

  if (sa->sa_family != AF_INET6)
    return;
  sa6 = (struct sockaddr_in6 *)sa;
  if (!IN6_IS_ADDR_LINKLOCAL(&sa6->sin6_addr) &&
      !IN6_IS_ADDR_MC_LINKLOCAL(&sa6->sin6_addr))
    return;
  if (*(u_int16_t *)&sa6->sin6_addr.s6_addr[2] != 0)
    return;
  *(u_int16_t *)&sa6->sin6_addr.s6_addr[2] = htons(ifindex);
}
#endif

int
rt_Set(struct bundle *bundle, int cmd, const struct ncprange *dst,
       const struct ncpaddr *gw, int bang, int quiet)
{
  struct rtmsg rtmes;
  int s, nb, wb;
  char *cp;
  const char *cmdstr;
  struct sockaddr_storage sadst, samask, sagw;
  int result = 1;

  if (bang)
    cmdstr = (cmd == RTM_ADD ? "Add!" : "Delete!");
  else
    cmdstr = (cmd == RTM_ADD ? "Add" : "Delete");
  s = ID0socket(PF_ROUTE, SOCK_RAW, 0);
  if (s < 0) {
    log_Printf(LogERROR, "rt_Set: socket(): %s\n", strerror(errno));
    return result;
  }
  memset(&rtmes, '\0', sizeof rtmes);
  rtmes.m_rtm.rtm_version = RTM_VERSION;
  rtmes.m_rtm.rtm_type = cmd;
  rtmes.m_rtm.rtm_addrs = RTA_DST;
  rtmes.m_rtm.rtm_seq = ++bundle->routing_seq;
  rtmes.m_rtm.rtm_pid = getpid();
  rtmes.m_rtm.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;

  if (cmd == RTM_ADD) {
    if (bundle->ncp.cfg.sendpipe > 0) {
      rtmes.m_rtm.rtm_rmx.rmx_sendpipe = bundle->ncp.cfg.sendpipe;
      rtmes.m_rtm.rtm_inits |= RTV_SPIPE;
    }
    if (bundle->ncp.cfg.recvpipe > 0) {
      rtmes.m_rtm.rtm_rmx.rmx_recvpipe = bundle->ncp.cfg.recvpipe;
      rtmes.m_rtm.rtm_inits |= RTV_RPIPE;
    }
  }

  ncprange_getsa(dst, &sadst, &samask);
#if defined(__KAME__) && !defined(NOINET6)
  add_scope((struct sockaddr *)&sadst, bundle->iface->index);
#endif

  cp = rtmes.m_space;
  cp += memcpy_roundup(cp, &sadst, sadst.ss_len);
  if (cmd == RTM_ADD) {
    if (gw == NULL) {
      log_Printf(LogERROR, "rt_Set: Program error\n");
      close(s);
      return result;
    }
    ncpaddr_getsa(gw, &sagw);
#if defined(__KAME__) && !defined(NOINET6)
    add_scope((struct sockaddr *)&sagw, bundle->iface->index);
#endif
    if (ncpaddr_isdefault(gw)) {
      if (!quiet)
        log_Printf(LogERROR, "rt_Set: Cannot add a route with"
                   " gateway 0.0.0.0\n");
      close(s);
      return result;
    } else {
      cp += memcpy_roundup(cp, &sagw, sagw.ss_len);
      rtmes.m_rtm.rtm_addrs |= RTA_GATEWAY;
    }
  }

  if (!ncprange_ishost(dst)) {
    cp += memcpy_roundup(cp, &samask, samask.ss_len);
    rtmes.m_rtm.rtm_addrs |= RTA_NETMASK;
  } else
    rtmes.m_rtm.rtm_flags |= RTF_HOST;

  nb = cp - (char *)&rtmes;
  rtmes.m_rtm.rtm_msglen = nb;
  wb = ID0write(s, &rtmes, nb);
  if (wb < 0) {
    log_Printf(LogTCPIP, "rt_Set failure:\n");
    log_Printf(LogTCPIP, "rt_Set:  Cmd = %s\n", cmdstr);
    log_Printf(LogTCPIP, "rt_Set:  Dst = %s\n", ncprange_ntoa(dst));
    if (gw != NULL)
      log_Printf(LogTCPIP, "rt_Set:  Gateway = %s\n", ncpaddr_ntoa(gw));
failed:
    if (cmd == RTM_ADD && (rtmes.m_rtm.rtm_errno == EEXIST ||
                           (rtmes.m_rtm.rtm_errno == 0 && errno == EEXIST))) {
      if (!bang) {
        log_Printf(LogWARN, "Add route failed: %s already exists\n",
		   ncprange_ntoa(dst));
        result = 0;	/* Don't add to our dynamic list */
      } else {
        rtmes.m_rtm.rtm_type = cmd = RTM_CHANGE;
        if ((wb = ID0write(s, &rtmes, nb)) < 0)
          goto failed;
      }
    } else if (cmd == RTM_DELETE &&
             (rtmes.m_rtm.rtm_errno == ESRCH ||
              (rtmes.m_rtm.rtm_errno == 0 && errno == ESRCH))) {
      if (!bang)
        log_Printf(LogWARN, "Del route failed: %s: Non-existent\n",
                  ncprange_ntoa(dst));
    } else if (rtmes.m_rtm.rtm_errno == 0) {
      if (!quiet || errno != ENETUNREACH)
        log_Printf(LogWARN, "%s route failed: %s: errno: %s\n", cmdstr,
                   ncprange_ntoa(dst), strerror(errno));
    } else
      log_Printf(LogWARN, "%s route failed: %s: %s\n",
		 cmdstr, ncprange_ntoa(dst), strerror(rtmes.m_rtm.rtm_errno));
  }

  if (log_IsKept(LogDEBUG)) {
    char gwstr[NCP_ASCIIBUFFERSIZE];

    if (gw)
      snprintf(gwstr, sizeof gwstr, "%s", ncpaddr_ntoa(gw));
    else
      snprintf(gwstr, sizeof gwstr, "<none>");
    log_Printf(LogDEBUG, "wrote %d: cmd = %s, dst = %s, gateway = %s\n",
               wb, cmdstr, ncprange_ntoa(dst), gwstr);
  }
  close(s);

  return result;
}

void
rt_Update(struct bundle *bundle, const struct sockaddr *dst,
          const struct sockaddr *gw, const struct sockaddr *mask,
          const struct sockaddr *ifp, const struct sockaddr *ifa)
{
  struct ncprange ncpdst;
  struct rtmsg rtmes;
  char *p;
  int s, wb;

  s = ID0socket(PF_ROUTE, SOCK_RAW, 0);
  if (s < 0) {
    log_Printf(LogERROR, "rt_Update: socket(): %s\n", strerror(errno));
    return;
  }

  memset(&rtmes, '\0', sizeof rtmes);
  rtmes.m_rtm.rtm_version = RTM_VERSION;
  rtmes.m_rtm.rtm_type = RTM_CHANGE;
  rtmes.m_rtm.rtm_addrs = 0;
  rtmes.m_rtm.rtm_seq = ++bundle->routing_seq;
  rtmes.m_rtm.rtm_pid = getpid();
  rtmes.m_rtm.rtm_flags = RTF_UP | RTF_STATIC;

  if (bundle->ncp.cfg.sendpipe > 0) {
    rtmes.m_rtm.rtm_rmx.rmx_sendpipe = bundle->ncp.cfg.sendpipe;
    rtmes.m_rtm.rtm_inits |= RTV_SPIPE;
  }

  if (bundle->ncp.cfg.recvpipe > 0) {
    rtmes.m_rtm.rtm_rmx.rmx_recvpipe = bundle->ncp.cfg.recvpipe;
    rtmes.m_rtm.rtm_inits |= RTV_RPIPE;
  }

  rtmes.m_rtm.rtm_rmx.rmx_mtu = bundle->iface->mtu;
  rtmes.m_rtm.rtm_inits |= RTV_MTU;
  p = rtmes.m_space;

  if (dst) {
    rtmes.m_rtm.rtm_addrs |= RTA_DST;
    p += memcpy_roundup(p, dst, dst->sa_len);
  }

  if (gw) {
    rtmes.m_rtm.rtm_addrs |= RTA_GATEWAY;
    p += memcpy_roundup(p, gw, gw->sa_len);
  }

  if (mask) {
    rtmes.m_rtm.rtm_addrs |= RTA_NETMASK;
    p += memcpy_roundup(p, mask, mask->sa_len);
  } else
    rtmes.m_rtm.rtm_flags |= RTF_HOST;

  if (ifa && ifp && ifp->sa_family == AF_LINK) {
    rtmes.m_rtm.rtm_addrs |= RTA_IFP;
    p += memcpy_roundup(p, ifp, ifp->sa_len);
    rtmes.m_rtm.rtm_addrs |= RTA_IFA;
    p += memcpy_roundup(p, ifa, ifa->sa_len);
  }

  rtmes.m_rtm.rtm_msglen = p - (char *)&rtmes;

  wb = ID0write(s, &rtmes, rtmes.m_rtm.rtm_msglen);
  if (wb < 0) {
    ncprange_setsa(&ncpdst, dst, mask);

    log_Printf(LogTCPIP, "rt_Update failure:\n");
    log_Printf(LogTCPIP, "rt_Update:  Dst = %s\n", ncprange_ntoa(&ncpdst));

    if (rtmes.m_rtm.rtm_errno == 0)
      log_Printf(LogWARN, "%s: Change route failed: errno: %s\n",
                 ncprange_ntoa(&ncpdst), strerror(errno));
    else
      log_Printf(LogWARN, "%s: Change route failed: %s\n",
		 ncprange_ntoa(&ncpdst), strerror(rtmes.m_rtm.rtm_errno));
  }
  close(s);
}
