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

#ifndef NOINET6
#define	IPV6CP_MAXCODE	CODE_CODEREJ

#define	TY_TOKEN	1
#define	TY_COMPPROTO	2

#define	IPV6CP_IFIDLEN	8		/* RFC2472 */

struct ipv6cp {
  struct fsm fsm;			/* The finite state machine */

  struct {
    struct fsm_retry fsm;		/* frequency to resend requests */
  } cfg;

  unsigned peer_tokenreq : 1;		/* Any TY_TOKEN REQs from the peer ? */

  u_char my_ifid[IPV6CP_IFIDLEN];	/* Local Interface Identifier */
  u_char his_ifid[IPV6CP_IFIDLEN];	/* Peer Interface Identifier */

  struct ncpaddr myaddr;		/* Local address */
  struct ncpaddr hisaddr;		/* Peer address */

  u_int32_t his_reject;			/* Request codes rejected by peer */
  u_int32_t my_reject;			/* Request codes I have rejected */

  struct pppThroughput throughput;	/* throughput statistics */
  struct mqueue Queue[2];		/* Output packet queues */
};

#define fsm2ipv6cp(fp) (fp->proto == PROTO_IPV6CP ? (struct ipv6cp *)fp : NULL)
#define IPV6CP_QUEUES(ipv6cp) (sizeof ipv6cp->Queue / sizeof ipv6cp->Queue[0])

struct bundle;
struct link;
struct cmdargs;
struct iface_addr;

extern void ipv6cp_Init(struct ipv6cp *, struct bundle *, struct link *,
                        const struct fsm_parent *);
extern void ipv6cp_Destroy(struct ipv6cp *);
extern void ipv6cp_Setup(struct ipv6cp *);
extern void ipv6cp_SetLink(struct ipv6cp *, struct link *);

extern int  ipv6cp_Show(struct cmdargs const *);
extern struct mbuf *ipv6cp_Input(struct bundle *, struct link *, struct mbuf *);
extern void ipv6cp_AddInOctets(struct ipv6cp *, int);
extern void ipv6cp_AddOutOctets(struct ipv6cp *, int);

extern void ipv6cp_IfaceAddrAdded(struct ipv6cp *, const struct iface_addr *);
extern void ipv6cp_IfaceAddrDeleted(struct ipv6cp *, const struct iface_addr *);
extern int  ipv6cp_InterfaceUp(struct ipv6cp *);
extern size_t ipv6cp_QueueLen(struct ipv6cp *);
extern int ipv6cp_PushPacket(struct ipv6cp *, struct link *);
#endif
