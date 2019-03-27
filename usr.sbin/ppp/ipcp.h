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

#define	IPCP_MAXCODE	CODE_CODEREJ

#define	TY_IPADDRS	1
#define	TY_COMPPROTO	2
#define	TY_IPADDR	3

/* Domain NameServer and NetBIOS NameServer options */

#define TY_PRIMARY_DNS		129
#define TY_PRIMARY_NBNS		130
#define TY_SECONDARY_DNS	131
#define TY_SECONDARY_NBNS	132
#define TY_ADJUST_NS		119 /* subtract from NS val for REJECT bit */

struct ipcp {
  struct fsm fsm;			/* The finite state machine */

  struct {
    struct {
      int slots;			/* Maximum VJ slots */
      unsigned slotcomp : 1;		/* Slot compression */
      unsigned neg : 2;			/* VJ negotiation */
    } vj;

    struct ncprange  my_range;		/* MYADDR spec */
    struct in_addr   netmask;		/* Iface netmask (unused by most OSs) */
    struct ncprange  peer_range;	/* HISADDR spec */
    struct iplist    peer_list;		/* Ranges of HISADDR values */

    struct in_addr   TriggerAddress;	/* Address to suggest in REQ */
    unsigned HaveTriggerAddress : 1;	/* Trigger address specified */

    struct {
      struct in_addr dns[2];		/* DNS addresses offered */
      unsigned dns_neg : 2;		/* dns negotiation */
      struct in_addr nbns[2];		/* NetBIOS NS addresses offered */
    } ns;

    struct fsm_retry fsm;		/* frequency to resend requests */
  } cfg;

  struct {
    struct slcompress cslc;		/* VJ state */
    struct slstat slstat;		/* VJ statistics */
  } vj;

  struct {
    unsigned resolver : 1;		/* Found resolv.conf ? */
    unsigned writable : 1;		/* Can write resolv.conf ? */
    struct in_addr dns[2];		/* Current DNS addresses */
    char *resolv;			/* Contents of resolv.conf */
    char *resolv_nons;			/* Contents of resolv.conf without ns */
  } ns;

  unsigned heis1172 : 1;		/* True if he is speaking rfc1172 */

  unsigned peer_req : 1;		/* Any TY_IPADDR REQs from the peer ? */
  struct in_addr peer_ip;		/* IP address he's willing to use */
  u_int32_t peer_compproto;		/* VJ params he's willing to use */

  struct in_addr ifmask;		/* Interface netmask */

  struct in_addr my_ip;			/* IP address I'm willing to use */
  u_int32_t my_compproto;		/* VJ params I'm willing to use */

  u_int32_t peer_reject;		/* Request codes rejected by peer */
  u_int32_t my_reject;			/* Request codes I have rejected */

  struct pppThroughput throughput;	/* throughput statistics */
  struct mqueue Queue[3];		/* Output packet queues */
};

#define fsm2ipcp(fp) (fp->proto == PROTO_IPCP ? (struct ipcp *)fp : NULL)
#define IPCP_QUEUES(ipcp) (sizeof ipcp->Queue / sizeof ipcp->Queue[0])

struct bundle;
struct link;
struct cmdargs;
struct iface_addr;

extern void ipcp_Init(struct ipcp *, struct bundle *, struct link *,
                      const struct fsm_parent *);
extern void ipcp_Destroy(struct ipcp *);
extern void ipcp_Setup(struct ipcp *, u_int32_t);
extern void ipcp_SetLink(struct ipcp *, struct link *);

extern int  ipcp_Show(struct cmdargs const *);
extern struct mbuf *ipcp_Input(struct bundle *, struct link *, struct mbuf *);
extern void ipcp_AddInOctets(struct ipcp *, int);
extern void ipcp_AddOutOctets(struct ipcp *, int);
extern int  ipcp_UseHisIPaddr(struct bundle *, struct in_addr);
extern int  ipcp_UseHisaddr(struct bundle *, const char *, int);
extern int  ipcp_vjset(struct cmdargs const *);
extern void ipcp_IfaceAddrAdded(struct ipcp *, const struct iface_addr *);
extern void ipcp_IfaceAddrDeleted(struct ipcp *, const struct iface_addr *);
extern int  ipcp_InterfaceUp(struct ipcp *);
extern struct in_addr addr2mask(struct in_addr);
extern int ipcp_WriteDNS(struct ipcp *);
extern void ipcp_RestoreDNS(struct ipcp *);
extern void ipcp_LoadDNS(struct ipcp *);
extern size_t ipcp_QueueLen(struct ipcp *);
extern int ipcp_PushPacket(struct ipcp *, struct link *);
