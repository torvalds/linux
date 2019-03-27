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
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifndef NOINET6
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#endif
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <sys/un.h>

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "layer.h"
#include "proto.h"
#include "mbuf.h"
#include "log.h"
#include "defs.h"
#include "timer.h"
#include "fsm.h"
#include "lqr.h"
#include "hdlc.h"
#include "throughput.h"
#include "iplist.h"
#include "slcompress.h"
#include "ncpaddr.h"
#include "ip.h"
#include "ipcp.h"
#include "filter.h"
#include "descriptor.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "tun.h"


#define OPCODE_QUERY	0
#define OPCODE_IQUERY	1
#define OPCODE_STATUS	2

struct dns_header {
  u_short id;
  unsigned qr : 1;
  unsigned opcode : 4;
  unsigned aa : 1;
  unsigned tc : 1;
  unsigned rd : 1;
  unsigned ra : 1;
  unsigned z : 3;
  unsigned rcode : 4;
  u_short qdcount;
  u_short ancount;
  u_short nscount;
  u_short arcount;
};

static const char *
dns_Qclass2Txt(u_short qclass)
{
  static char failure[6];
  struct {
    u_short id;
    const char *txt;
  } qtxt[] = {
    /* rfc1035 */
    { 1, "IN" }, { 2, "CS" }, { 3, "CH" }, { 4, "HS" }, { 255, "*" }
  };
  unsigned f;

  for (f = 0; f < sizeof qtxt / sizeof *qtxt; f++)
    if (qtxt[f].id == qclass)
      return qtxt[f].txt;

  return HexStr(qclass, failure, sizeof failure);
}

static const char *
dns_Qtype2Txt(u_short qtype)
{
  static char failure[6];
  struct {
    u_short id;
    const char *txt;
  } qtxt[] = {
    /* rfc1035/rfc1700 */
    { 1, "A" }, { 2, "NS" }, { 3, "MD" }, { 4, "MF" }, { 5, "CNAME" },
    { 6, "SOA" }, { 7, "MB" }, { 8, "MG" }, { 9, "MR" }, { 10, "NULL" },
    { 11, "WKS" }, { 12, "PTR" }, { 13, "HINFO" }, { 14, "MINFO" },
    { 15, "MX" }, { 16, "TXT" }, { 17, "RP" }, { 18, "AFSDB" },
    { 19, "X25" }, { 20, "ISDN" }, { 21, "RT" }, { 22, "NSAP" },
    { 23, "NSAP-PTR" }, { 24, "SIG" }, { 25, "KEY" }, { 26, "PX" },
    { 27, "GPOS" }, { 28, "AAAA" }, { 252, "AXFR" }, { 253, "MAILB" },
    { 254, "MAILA" }, { 255, "*" }
  };
  unsigned f;

  for (f = 0; f < sizeof qtxt / sizeof *qtxt; f++)
    if (qtxt[f].id == qtype)
      return qtxt[f].txt;

  return HexStr(qtype, failure, sizeof failure);
}

static __inline int
PortMatch(int op, u_short pport, u_short rport)
{
  switch (op) {
  case OP_EQ:
    return pport == rport;
  case OP_GT:
    return pport > rport;
  case OP_LT:
    return pport < rport;
  default:
    return 0;
  }
}

/*
 * Return a text string representing the cproto protocol number.
 *
 * The purpose of this routine is calculate this result, for
 * the many times it is needed in FilterCheck, only on demand
 * (i.e. when the corresponding logging functions are invoked).
 *
 * This optimization saves, over the previous implementation, which
 * calculated prototxt at the beginning of FilterCheck, an
 * open/read/close system call sequence per packet, approximately
 * halving the ppp system overhead and reducing the overall (u + s)
 * time by 38%.
 *
 * The caching performed here is just a side effect.
 */
static const char *
prototxt(int cproto)
{
  static int oproto = -1;
  static char protobuff[16] = "-1";
  struct protoent *pe;

  if (cproto == oproto)
	return protobuff;
  if ((pe = getprotobynumber(cproto)) == NULL)
    snprintf(protobuff, sizeof protobuff, "%d", cproto);
  else
    snprintf(protobuff, sizeof protobuff, "%s", pe->p_name);
  oproto = cproto;
  return (protobuff);
}

/*
 * Check a packet against the given filter
 * Returns 0 to accept the packet, non-zero to drop the packet.
 * If psecs is not NULL, populate it with the timeout associated
 * with the filter rule matched.
 *
 * If filtering is enabled, the initial fragment of a datagram must
 * contain the complete protocol header, and subsequent fragments
 * must not attempt to over-write it.
 *
 * One (and only one) of pip or pip6 must be set.
 */
int
FilterCheck(const unsigned char *packet,
#ifdef NOINET6
	    u_int32_t family __unused,
#else
	    u_int32_t family,
#endif
            const struct filter *filter, unsigned *psecs)
{
  int gotinfo;			/* true if IP payload decoded */
  int cproto;			/* IPPROTO_* protocol number if (gotinfo) */
  int estab, syn, finrst;	/* TCP state flags if (gotinfo) */
  u_short sport, dport;		/* src, dest port from packet if (gotinfo) */
  int n;			/* filter rule to process */
  int len;			/* bytes used in dbuff */
  int didname;			/* true if filter header printed */
  int match;			/* true if condition matched */
  int mindata;			/* minimum data size or zero */
  const struct filterent *fp = filter->rule;
  char dbuff[100], dstip[NCP_ASCIIBUFFERSIZE];
  struct ncpaddr srcaddr, dstaddr;
  const char *payload;		/* IP payload */
  int datalen;			/* IP datagram length */

  if (fp->f_action == A_NONE)
    return 0;		/* No rule is given. Permit this packet */

#ifndef NOINET6
  if (family == AF_INET6) {
    const struct ip6_hdr *pip6 = (const struct ip6_hdr *)packet;

    ncpaddr_setip6(&srcaddr, &pip6->ip6_src);
    ncpaddr_setip6(&dstaddr, &pip6->ip6_dst);
    datalen = ntohs(pip6->ip6_plen);
    payload = packet + sizeof *pip6;
    cproto = pip6->ip6_nxt;
  } else
#endif
  {
    /*
     * Deny any packet fragment that tries to over-write the header.
     * Since we no longer have the real header available, punt on the
     * largest normal header - 20 bytes for TCP without options, rounded
     * up to the next possible fragment boundary.  Since the smallest
     * `legal' MTU is 576, and the smallest recommended MTU is 296, any
     * fragmentation within this range is dubious at best
     */
    const struct ip *pip = (const struct ip *)packet;

    len = ntohs(pip->ip_off) & IP_OFFMASK;	/* fragment offset */
    if (len > 0) {		/* Not first fragment within datagram */
      if (len < (24 >> 3)) {	/* don't allow fragment to over-write header */
        log_Printf(LogFILTER, " error: illegal header\n");
        return 1;
      }
      /* permit fragments on in and out filter */
      if (!filter->fragok) {
        log_Printf(LogFILTER, " error: illegal fragmentation\n");
        return 1;
      } else
        return 0;
    }

    ncpaddr_setip4(&srcaddr, pip->ip_src);
    ncpaddr_setip4(&dstaddr, pip->ip_dst);
    datalen = ntohs(pip->ip_len) - (pip->ip_hl << 2);
    payload = packet + (pip->ip_hl << 2);
    cproto = pip->ip_p;
  }


  gotinfo = estab = syn = finrst = didname = 0;
  sport = dport = 0;

  for (n = 0; n < MAXFILTERS; ) {
    if (fp->f_action == A_NONE) {
      n++;
      fp++;
      continue;
    }

    if (!didname) {
      log_Printf(LogDEBUG, "%s filter:\n", filter->name);
      didname = 1;
    }

    match = 0;

    if ((ncprange_family(&fp->f_src) == AF_UNSPEC ||
         ncprange_contains(&fp->f_src, &srcaddr)) &&
        (ncprange_family(&fp->f_dst) == AF_UNSPEC ||
         ncprange_contains(&fp->f_dst, &dstaddr))) {
      if (fp->f_proto != 0) {
        if (!gotinfo) {
          const struct tcphdr *th;
          const struct udphdr *uh;
          const struct icmp *ih;
#ifndef NOINET6
          const struct icmp6_hdr *ih6;
#endif
          mindata = 0;
          sport = dport = 0;
          estab = syn = finrst = -1;

          switch (cproto) {
          case IPPROTO_ICMP:
            mindata = 8;	/* ICMP must be at least 8 octets */
            ih = (const struct icmp *)payload;
            sport = ih->icmp_type;
            if (log_IsKept(LogDEBUG))
              snprintf(dbuff, sizeof dbuff, "sport = %d", sport);
            break;

#ifndef NOINET6
          case IPPROTO_ICMPV6:
            mindata = 8;	/* ICMP must be at least 8 octets */
            ih6 = (const struct icmp6_hdr *)payload;
            sport = ih6->icmp6_type;
            if (log_IsKept(LogDEBUG))
              snprintf(dbuff, sizeof dbuff, "sport = %d", sport);
            break;
#endif

          case IPPROTO_IGMP:
            mindata = 8;	/* IGMP uses 8-octet messages */
            break;

#ifdef IPPROTO_GRE
          case IPPROTO_GRE:
            mindata = 2;	/* GRE uses 2-octet+ messages */
            break;
#endif
#ifdef IPPROTO_OSPFIGP
          case IPPROTO_OSPFIGP:
            mindata = 8;	/* IGMP uses 8-octet messages */
            break;
#endif
#ifndef NOINET6
          case IPPROTO_IPV6:
            mindata = 20;	/* RFC2893 Section 3.5: 5 * 32bit words */
            break;
#endif

          case IPPROTO_UDP:
            mindata = 8;	/* UDP header is 8 octets */
            uh = (const struct udphdr *)payload;
            sport = ntohs(uh->uh_sport);
            dport = ntohs(uh->uh_dport);
            if (log_IsKept(LogDEBUG))
              snprintf(dbuff, sizeof dbuff, "sport = %d, dport = %d",
                       sport, dport);
            break;

          case IPPROTO_TCP:
            th = (const struct tcphdr *)payload;
            /*
             * TCP headers are variable length.  The following code
             * ensures that the TCP header length isn't de-referenced if
             * the datagram is too short
             */
            if (datalen < 20 || datalen < (th->th_off << 2)) {
              log_Printf(LogFILTER, " error: TCP header incorrect\n");
              return 1;
            }
            sport = ntohs(th->th_sport);
            dport = ntohs(th->th_dport);
            estab = (th->th_flags & TH_ACK);
            syn = (th->th_flags & TH_SYN);
            finrst = (th->th_flags & (TH_FIN|TH_RST));
            if (log_IsKept(LogDEBUG)) {
              if (!estab)
                snprintf(dbuff, sizeof dbuff,
                         "flags = %02x, sport = %d, dport = %d",
                         th->th_flags, sport, dport);
              else
                *dbuff = '\0';
            }
            break;
          default:
            break;
          }

          if (datalen < mindata) {
            log_Printf(LogFILTER, " error: proto %s must be at least"
                       " %d octets\n", prototxt(cproto), mindata);
            return 1;
          }

          if (log_IsKept(LogDEBUG)) {
            if (estab != -1) {
              len = strlen(dbuff);
              snprintf(dbuff + len, sizeof dbuff - len,
                       ", estab = %d, syn = %d, finrst = %d",
                       estab, syn, finrst);
            }
            log_Printf(LogDEBUG, " Filter: proto = %s, %s\n",
                       prototxt(cproto), dbuff);
          }
          gotinfo = 1;
        }

        if (log_IsKept(LogDEBUG)) {
          if (fp->f_srcop != OP_NONE) {
            snprintf(dbuff, sizeof dbuff, ", src %s %d",
                     filter_Op2Nam(fp->f_srcop), fp->f_srcport);
            len = strlen(dbuff);
          } else
            len = 0;
          if (fp->f_dstop != OP_NONE) {
            snprintf(dbuff + len, sizeof dbuff - len,
                     ", dst %s %d", filter_Op2Nam(fp->f_dstop),
                     fp->f_dstport);
          } else if (!len)
            *dbuff = '\0';

          log_Printf(LogDEBUG, "  rule = %d: Address match, "
                     "check against proto %d%s, action = %s\n",
                     n, fp->f_proto, dbuff, filter_Action2Nam(fp->f_action));
        }

        if (cproto == fp->f_proto) {
          if ((fp->f_srcop == OP_NONE ||
               PortMatch(fp->f_srcop, sport, fp->f_srcport)) &&
              (fp->f_dstop == OP_NONE ||
               PortMatch(fp->f_dstop, dport, fp->f_dstport)) &&
              (fp->f_estab == 0 || estab) &&
              (fp->f_syn == 0 || syn) &&
              (fp->f_finrst == 0 || finrst)) {
            match = 1;
          }
        }
      } else {
        /* Address is matched and no protocol specified. Make a decision. */
        log_Printf(LogDEBUG, "  rule = %d: Address match, action = %s\n", n,
                   filter_Action2Nam(fp->f_action));
        match = 1;
      }
    } else
      log_Printf(LogDEBUG, "  rule = %d: Address mismatch\n", n);

    if (match != fp->f_invert) {
      /* Take specified action */
      if (fp->f_action < A_NONE)
        fp = &filter->rule[n = fp->f_action];
      else {
        if (fp->f_action == A_PERMIT) {
          if (psecs != NULL)
            *psecs = fp->timeout;
          if (strcmp(filter->name, "DIAL") == 0) {
            /* If dial filter then even print out accept packets */
            if (log_IsKept(LogFILTER)) {
              snprintf(dstip, sizeof dstip, "%s", ncpaddr_ntoa(&dstaddr));
              log_Printf(LogFILTER, "%sbound rule = %d accept %s "
                         "src = %s:%d dst = %s:%d\n", filter->name, n,
                         prototxt(cproto), ncpaddr_ntoa(&srcaddr), sport,
                         dstip, dport);
            }
          }
          return 0;
        } else {
          if (log_IsKept(LogFILTER)) {
            snprintf(dstip, sizeof dstip, "%s", ncpaddr_ntoa(&dstaddr));
            log_Printf(LogFILTER,
                       "%sbound rule = %d deny %s src = %s/%d dst = %s/%d\n",
                       filter->name, n, prototxt(cproto),
                       ncpaddr_ntoa(&srcaddr), sport, dstip, dport);
          }
          return 1;
        }		/* Explicit match.  Deny this packet */
      }
    } else {
      n++;
      fp++;
    }
  }

  if (log_IsKept(LogFILTER)) {
    snprintf(dstip, sizeof dstip, "%s", ncpaddr_ntoa(&dstaddr));
    log_Printf(LogFILTER,
               "%sbound rule = implicit deny %s src = %s/%d dst = %s/%d\n",
               filter->name, prototxt(cproto), ncpaddr_ntoa(&srcaddr),
               sport, dstip, dport);
  }

  return 1;		/* No rule matched, deny this packet */
}

static void
ip_LogDNS(const struct udphdr *uh, const char *direction)
{
  struct dns_header header;
  const u_short *pktptr;
  const u_char *ptr;
  u_short *hptr, tmp;
  unsigned len;

  ptr = (const char *)uh + sizeof *uh;
  len = ntohs(uh->uh_ulen) - sizeof *uh;
  if (len < sizeof header + 5)		/* rfc1024 */
    return;

  pktptr = (const u_short *)ptr;
  hptr = (u_short *)&header;
  ptr += sizeof header;
  len -= sizeof header;

  while (pktptr < (const u_short *)ptr) {
    *hptr++ = ntohs(*pktptr);		/* Careful of macro side-effects ! */
    pktptr++;
  }

  if (header.opcode == OPCODE_QUERY && header.qr == 0) {
    /* rfc1035 */
    char namewithdot[MAXHOSTNAMELEN + 1], *n;
    const char *qtype, *qclass;
    const u_char *end;

    n = namewithdot;
    end = ptr + len - 4;
    if (end - ptr >= (int)sizeof namewithdot)
      end = ptr + sizeof namewithdot - 1;
    while (ptr < end) {
      len = *ptr++;
      if ((int)len > end - ptr)
        len = end - ptr;
      if (n != namewithdot)
        *n++ = '.';
      memcpy(n, ptr, len);
      ptr += len;
      n += len;
    }
    *n = '\0';

    if (log_IsKept(LogDNS)) {
      memcpy(&tmp, end, sizeof tmp);
      qtype = dns_Qtype2Txt(ntohs(tmp));
      memcpy(&tmp, end + 2, sizeof tmp);
      qclass = dns_Qclass2Txt(ntohs(tmp));

      log_Printf(LogDNS, "%sbound query %s %s %s\n",
                 direction, qclass, qtype, namewithdot);
    }
  }
}

/*
 * Check if the given packet matches the given filter.
 * One of pip or pip6 must be set.
 */
int
PacketCheck(struct bundle *bundle, u_int32_t family,
            const unsigned char *packet, int nb, struct filter *filter,
            const char *prefix, unsigned *psecs)
{
  char logbuf[200];
  static const char *const TcpFlags[] = {
    "FIN", "SYN", "RST", "PSH", "ACK", "URG"
  };
  const struct tcphdr *th;
  const struct udphdr *uh;
  const struct icmp *icmph;
#ifndef NOINET6
  const struct icmp6_hdr *icmp6h;
#endif
  const unsigned char *payload;
  struct ncpaddr srcaddr, dstaddr;
  int cproto, mask, len, n, pri, logit, result, datalen, frag;
  unsigned loglen;
  u_char tos;

  logit = (log_IsKept(LogTCPIP) || log_IsKept(LogDNS)) &&
          (!filter || filter->logok);
  loglen = 0;
  pri = 0;

#ifndef NOINET6
  if (family == AF_INET6) {
    const struct ip6_hdr *pip6 = (const struct ip6_hdr *)packet;

    ncpaddr_setip6(&srcaddr, &pip6->ip6_src);
    ncpaddr_setip6(&dstaddr, &pip6->ip6_dst);
    datalen = ntohs(pip6->ip6_plen);
    payload = packet + sizeof *pip6;
    cproto = pip6->ip6_nxt;
    tos = 0;					/* XXX: pip6->ip6_vfc >> 4 ? */
    frag = 0;					/* XXX: ??? */
  } else
#endif
  {
    const struct ip *pip = (const struct ip *)packet;

    ncpaddr_setip4(&srcaddr, pip->ip_src);
    ncpaddr_setip4(&dstaddr, pip->ip_dst);
    datalen = ntohs(pip->ip_len) - (pip->ip_hl << 2);
    payload = packet + (pip->ip_hl << 2);
    cproto = pip->ip_p;
    tos = pip->ip_tos;
    frag = ntohs(pip->ip_off) & IP_OFFMASK;
  }

  uh = NULL;

  if (logit && loglen < sizeof logbuf) {
    if (prefix)
      snprintf(logbuf + loglen, sizeof logbuf - loglen, "%s", prefix);
    else if (filter)
      snprintf(logbuf + loglen, sizeof logbuf - loglen, "%s ", filter->name);
    else
      snprintf(logbuf + loglen, sizeof logbuf - loglen, "  ");
    loglen += strlen(logbuf + loglen);
  }

  switch (cproto) {
  case IPPROTO_ICMP:
    if (logit && loglen < sizeof logbuf) {
      len = datalen - sizeof *icmph;
      icmph = (const struct icmp *)payload;
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "ICMP: %s:%d ---> ", ncpaddr_ntoa(&srcaddr), icmph->icmp_type);
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "%s (%d/%d)", ncpaddr_ntoa(&dstaddr), len, nb);
      loglen += strlen(logbuf + loglen);
    }
    break;

#ifndef NOINET6
  case IPPROTO_ICMPV6:
    if (logit && loglen < sizeof logbuf) {
      len = datalen - sizeof *icmp6h;
      icmp6h = (const struct icmp6_hdr *)payload;
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "ICMP: %s:%d ---> ", ncpaddr_ntoa(&srcaddr), icmp6h->icmp6_type);
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "%s (%d/%d)", ncpaddr_ntoa(&dstaddr), len, nb);
      loglen += strlen(logbuf + loglen);
    }
    break;
#endif

  case IPPROTO_UDP:
    uh = (const struct udphdr *)payload;
    if (tos == IPTOS_LOWDELAY && bundle->ncp.cfg.urgent.tos)
      pri++;

    if (!frag && ncp_IsUrgentUdpPort(&bundle->ncp, ntohs(uh->uh_sport),
                                     ntohs(uh->uh_dport)))
      pri++;

    if (logit && loglen < sizeof logbuf) {
      len = datalen - sizeof *uh;
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "UDP: %s:%d ---> ", ncpaddr_ntoa(&srcaddr), ntohs(uh->uh_sport));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "%s:%d (%d/%d)", ncpaddr_ntoa(&dstaddr), ntohs(uh->uh_dport),
               len, nb);
      loglen += strlen(logbuf + loglen);
    }

    if (Enabled(bundle, OPT_FILTERDECAP) &&
        payload[sizeof *uh] == HDLC_ADDR &&
        payload[sizeof *uh + 1] == HDLC_UI) {
      u_short proto;
      const char *type;

      memcpy(&proto, payload + sizeof *uh + 2, sizeof proto);
      type = NULL;

      switch (ntohs(proto)) {
        case PROTO_IP:
          snprintf(logbuf + loglen, sizeof logbuf - loglen, " contains ");
          result = PacketCheck(bundle, AF_INET, payload + sizeof *uh + 4,
                               nb - (payload - packet) - sizeof *uh - 4, filter,
                               logbuf, psecs);
          if (result != -2)
              return result;
          type = "IP";
          break;

        case PROTO_VJUNCOMP: type = "compressed VJ";   break;
        case PROTO_VJCOMP:   type = "uncompressed VJ"; break;
        case PROTO_MP:       type = "Multi-link"; break;
        case PROTO_ICOMPD:   type = "Individual link CCP"; break;
        case PROTO_COMPD:    type = "CCP"; break;
        case PROTO_IPCP:     type = "IPCP"; break;
        case PROTO_LCP:      type = "LCP"; break;
        case PROTO_PAP:      type = "PAP"; break;
        case PROTO_CBCP:     type = "CBCP"; break;
        case PROTO_LQR:      type = "LQR"; break;
        case PROTO_CHAP:     type = "CHAP"; break;
      }
      if (type) {
        snprintf(logbuf + loglen, sizeof logbuf - loglen,
                 " - %s data", type);
        loglen += strlen(logbuf + loglen);
      }
    }

    break;

#ifdef IPPROTO_GRE
  case IPPROTO_GRE:
    if (logit && loglen < sizeof logbuf) {
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
          "GRE: %s ---> ", ncpaddr_ntoa(&srcaddr));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
              "%s (%d/%d)", ncpaddr_ntoa(&dstaddr), datalen, nb);
      loglen += strlen(logbuf + loglen);
    }
    break;
#endif

#ifdef IPPROTO_OSPFIGP
  case IPPROTO_OSPFIGP:
    if (logit && loglen < sizeof logbuf) {
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "OSPF: %s ---> ", ncpaddr_ntoa(&srcaddr));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "%s (%d/%d)", ncpaddr_ntoa(&dstaddr), datalen, nb);
      loglen += strlen(logbuf + loglen);
    }
    break;
#endif

#ifndef NOINET6
  case IPPROTO_IPV6:
    if (logit && loglen < sizeof logbuf) {
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "IPv6: %s ---> ", ncpaddr_ntoa(&srcaddr));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "%s (%d/%d)", ncpaddr_ntoa(&dstaddr), datalen, nb);
      loglen += strlen(logbuf + loglen);
    }

    if (Enabled(bundle, OPT_FILTERDECAP)) {
      snprintf(logbuf + loglen, sizeof logbuf - loglen, " contains ");
      result = PacketCheck(bundle, AF_INET6, payload, nb - (payload - packet),
                           filter, logbuf, psecs);
      if (result != -2)
        return result;
    }
    break;
#endif

  case IPPROTO_IPIP:
    if (logit && loglen < sizeof logbuf) {
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "IPIP: %s ---> ", ncpaddr_ntoa(&srcaddr));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "%s", ncpaddr_ntoa(&dstaddr));
      loglen += strlen(logbuf + loglen);
    }

    if (Enabled(bundle, OPT_FILTERDECAP) &&
        ((const struct ip *)payload)->ip_v == 4) {
      snprintf(logbuf + loglen, sizeof logbuf - loglen, " contains ");
      result = PacketCheck(bundle, AF_INET, payload, nb - (payload - packet),
                           filter, logbuf, psecs);
      loglen += strlen(logbuf + loglen);
      if (result != -2)
        return result;
    }
    break;

  case IPPROTO_ESP:
    if (logit && loglen < sizeof logbuf) {
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "ESP: %s ---> ", ncpaddr_ntoa(&srcaddr));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen, "%s, spi %p",
               ncpaddr_ntoa(&dstaddr), payload);
      loglen += strlen(logbuf + loglen);
    }
    break;

  case IPPROTO_AH:
    if (logit && loglen < sizeof logbuf) {
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "AH: %s ---> ", ncpaddr_ntoa(&srcaddr));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen, "%s, spi %p",
               ncpaddr_ntoa(&dstaddr), payload + sizeof(u_int32_t));
      loglen += strlen(logbuf + loglen);
    }
    break;

  case IPPROTO_IGMP:
    if (logit && loglen < sizeof logbuf) {
      uh = (const struct udphdr *)payload;
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "IGMP: %s:%d ---> ", ncpaddr_ntoa(&srcaddr),
               ntohs(uh->uh_sport));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "%s:%d", ncpaddr_ntoa(&dstaddr), ntohs(uh->uh_dport));
      loglen += strlen(logbuf + loglen);
    }
    break;

  case IPPROTO_TCP:
    th = (const struct tcphdr *)payload;
    if (tos == IPTOS_LOWDELAY && bundle->ncp.cfg.urgent.tos)
      pri++;

    if (!frag && ncp_IsUrgentTcpPort(&bundle->ncp, ntohs(th->th_sport),
                                     ntohs(th->th_dport)))
      pri++;
    else if (!frag && ncp_IsUrgentTcpLen(&bundle->ncp, datalen))
      pri++;

    if (logit && loglen < sizeof logbuf) {
      len = datalen - (th->th_off << 2);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
           "TCP: %s:%d ---> ", ncpaddr_ntoa(&srcaddr), ntohs(th->th_sport));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "%s:%d", ncpaddr_ntoa(&dstaddr), ntohs(th->th_dport));
      loglen += strlen(logbuf + loglen);
      n = 0;
      for (mask = TH_FIN; mask != 0x40; mask <<= 1) {
        if (th->th_flags & mask) {
          snprintf(logbuf + loglen, sizeof logbuf - loglen, " %s", TcpFlags[n]);
          loglen += strlen(logbuf + loglen);
        }
        n++;
      }
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "  seq:%lx  ack:%lx (%d/%d)",
               (u_long)ntohl(th->th_seq), (u_long)ntohl(th->th_ack), len, nb);
      loglen += strlen(logbuf + loglen);
      if ((th->th_flags & TH_SYN) && nb > 40) {
        const u_short *sp;

        sp = (const u_short *)(payload + 20);
        if (ntohs(sp[0]) == 0x0204) {
          snprintf(logbuf + loglen, sizeof logbuf - loglen,
                   " MSS = %d", ntohs(sp[1]));
          loglen += strlen(logbuf + loglen);
        }
      }
      snprintf(logbuf + loglen, sizeof logbuf - loglen, " pri:%d", pri);
      loglen += strlen(logbuf + loglen);
    }
    break;

  default:
    if (prefix)
      return -2;

    if (logit && loglen < sizeof logbuf) {
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "<%d>: %s ---> ", cproto, ncpaddr_ntoa(&srcaddr));
      loglen += strlen(logbuf + loglen);
      snprintf(logbuf + loglen, sizeof logbuf - loglen,
               "%s (%d)", ncpaddr_ntoa(&dstaddr), nb);
      loglen += strlen(logbuf + loglen);
    }
    break;
  }

  if (filter && FilterCheck(packet, family, filter, psecs)) {
    if (logit)
      log_Printf(LogTCPIP, "%s - BLOCKED\n", logbuf);
    result = -1;
  } else {
    /* Check Keep Alive filter */
    if (logit && log_IsKept(LogTCPIP)) {
      unsigned alivesecs;

      alivesecs = 0;
      if (filter &&
          FilterCheck(packet, family, &bundle->filter.alive, &alivesecs))
        log_Printf(LogTCPIP, "%s - NO KEEPALIVE\n", logbuf);
      else if (psecs != NULL) {
        if(*psecs == 0)
          *psecs = alivesecs;
        if (*psecs) {
          if (*psecs != alivesecs)
            log_Printf(LogTCPIP, "%s - (timeout = %d / ALIVE = %d secs)\n",
                       logbuf, *psecs, alivesecs);
          else
            log_Printf(LogTCPIP, "%s - (timeout = %d secs)\n", logbuf, *psecs);
        } else
          log_Printf(LogTCPIP, "%s\n", logbuf);
      }
    }
    result = pri;
  }

  if (filter && uh && ntohs(uh->uh_dport) == 53 && log_IsKept(LogDNS))
    ip_LogDNS(uh, filter->name);

  return result;
}

static size_t
ip_Input(struct bundle *bundle, struct link *l, struct mbuf *bp, u_int32_t af)
{
  ssize_t nw;
  size_t nb;
  struct tun_data tun;
  char *data;
  unsigned secs, alivesecs;

  nb = m_length(bp);
  if (nb > sizeof tun.data) {
    log_Printf(LogWARN, "ip_Input: %s: Packet too large (got %zd, max %d)\n",
               l->name, nb, (int)(sizeof tun.data));
    m_freem(bp);
    return 0;
  }
  mbuf_Read(bp, tun.data, nb);

  secs = 0;
  if (PacketCheck(bundle, af, tun.data, nb, &bundle->filter.in,
                  NULL, &secs) < 0)
    return 0;

  alivesecs = 0;
  if (!FilterCheck(tun.data, af, &bundle->filter.alive, &alivesecs)) {
    if (secs == 0)
      secs = alivesecs;
    bundle_StartIdleTimer(bundle, secs);
  }

  if (bundle->dev.header) {
    tun.header.family = htonl(af);
    nb += sizeof tun - sizeof tun.data;
    data = (char *)&tun;
  } else
    data = tun.data;

  nw = write(bundle->dev.fd, data, nb);
  if (nw != (ssize_t)nb) {
    if (nw == -1)
      log_Printf(LogERROR, "ip_Input: %s: wrote %zd, got %s\n",
                 l->name, nb, strerror(errno));
    else
      log_Printf(LogERROR, "ip_Input: %s: wrote %zd, got %zd\n", l->name, nb,
	  nw);
  }

  return nb;
}

struct mbuf *
ipv4_Input(struct bundle *bundle, struct link *l, struct mbuf *bp)
{
  int nb;

  if (bundle->ncp.ipcp.fsm.state != ST_OPENED) {
    log_Printf(LogWARN, "ipv4_Input: IPCP not open - packet dropped\n");
    m_freem(bp);
    return NULL;
  }

  m_settype(bp, MB_IPIN);

  nb = ip_Input(bundle, l, bp, AF_INET);
  ipcp_AddInOctets(&bundle->ncp.ipcp, nb);

  return NULL;
}

#ifndef NOINET6
struct mbuf *
ipv6_Input(struct bundle *bundle, struct link *l, struct mbuf *bp)
{
  int nb;

  if (bundle->ncp.ipv6cp.fsm.state != ST_OPENED) {
    log_Printf(LogWARN, "ipv6_Input: IPV6CP not open - packet dropped\n");
    m_freem(bp);
    return NULL;
  }

  m_settype(bp, MB_IPV6IN);

  nb = ip_Input(bundle, l, bp, AF_INET6);
  ipv6cp_AddInOctets(&bundle->ncp.ipv6cp, nb);

  return NULL;
}
#endif
