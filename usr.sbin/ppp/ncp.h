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

struct port_range {
  unsigned nports;		/* How many ports */
  unsigned maxports;		/* How many allocated (malloc) ports */
  u_short *port;		/* The actual ports */
};

struct ncp {
  struct {
    u_long sendpipe;			/* route sendpipe size */
    u_long recvpipe;			/* route recvpipe size */

    struct {
      struct port_range tcp, udp;	/* The range of urgent ports */
      unsigned tos : 1;			/* Urgent IPTOS_LOWDELAY packets ? */
      int len;				/* The size below which traffic is prioritised */
    } urgent;
  } cfg;

  int afq;			/* Next address family to queue */

  struct sticky_route *route;	/* List of dynamic routes */

  struct ipcp ipcp;		/* Our IPCP FSM */
#ifndef NOINET6
  struct ipv6cp ipv6cp;		/* Our IPV6CP FSM */
#endif
  struct mp mp;			/* Our MP */
};

extern void ncp_Init(struct ncp *, struct bundle *);
extern void ncp_Destroy(struct ncp *);
extern int ncp_fsmStart(struct ncp *, struct bundle *);
extern void ncp_IfaceAddrAdded(struct ncp *, const struct iface_addr *);
extern void ncp_IfaceAddrDeleted(struct ncp *, const struct iface_addr *);
extern void ncp_SetLink(struct ncp *, struct link *);
extern void ncp_Enqueue(struct ncp *, int, unsigned, char *, int);
extern void ncp_DeleteQueues(struct ncp *);
extern size_t ncp_QueueLen(struct ncp *);
extern size_t ncp_FillPhysicalQueues(struct ncp *, struct bundle *);
extern int ncp_PushPacket(struct ncp *, int *, struct link *);
extern int ncp_IsUrgentPort(struct port_range *, u_short, u_short);
extern int ncp_IsUrgentTcpLen(struct ncp *, int);
extern void ncp_SetUrgentTcpLen(struct ncp *, int);
extern void ncp_AddUrgentPort(struct port_range *, u_short);
extern void ncp_RemoveUrgentPort(struct port_range *, u_short);
extern void ncp_ClearUrgentPorts(struct port_range *);
extern int ncp_Show(struct cmdargs const *);
extern int ncp_LayersOpen(struct ncp *);
extern int ncp_LayersUnfinished(struct ncp *);
extern void ncp_Close(struct ncp *);
extern void ncp2initial(struct ncp *);

#define ncp_IsUrgentTcpPort(ncp, p1, p2) \
          ncp_IsUrgentPort(&(ncp)->cfg.urgent.tcp, p1, p2)
#define ncp_IsUrgentUdpPort(ncp, p1, p2) \
          ncp_IsUrgentPort(&(ncp)->cfg.urgent.udp, p1, p2)
#define ncp_AddUrgentTcpPort(ncp, p) \
          ncp_AddUrgentPort(&(ncp)->cfg.urgent.tcp, p)
#define ncp_AddUrgentUdpPort(ncp, p) \
          ncp_AddUrgentPort(&(ncp)->cfg.urgent.udp, p)
#define ncp_RemoveUrgentTcpPort(ncp, p) \
          ncp_RemoveUrgentPort(&(ncp)->cfg.urgent.tcp, p)
#define ncp_RemoveUrgentUdpPort(ncp, p) \
          ncp_RemoveUrgentPort(&(ncp)->cfg.urgent.udp, p)
#define ncp_ClearUrgentTcpPorts(ncp) \
          ncp_ClearUrgentPorts(&(ncp)->cfg.urgent.tcp)
#define ncp_ClearUrgentUdpPorts(ncp) \
          ncp_ClearUrgentPorts(&(ncp)->cfg.urgent.udp)
#define ncp_ClearUrgentTOS(ncp) (ncp)->cfg.urgent.tos = 0;
#define ncp_SetUrgentTOS(ncp) (ncp)->cfg.urgent.tos = 1;

#ifndef NOINET6
#define isncp(proto) ((proto) == PROTO_IPCP || (proto) == PROTO_IPV6CP)
#else
#define isncp(proto) ((proto) == PROTO_IPCP)
#endif
