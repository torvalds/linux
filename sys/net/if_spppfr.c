/*-
 * Synchronous Frame Relay link level subroutines.
 * ANSI T1.617-compaible link management signaling
 * implemented for Frame Relay mode.
 * Cisco-type Frame Relay framing added, thanks Alex Tutubalin.
 * Only one DLCI per channel for now.
 *
 * Copyright (C) 1994-2000 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Copyright (C) 1999-2004 Cronyx Engineering.
 * Author: Kurakin Roman, <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Cronyx Id: if_spppfr.c,v 1.1.2.10 2004/06/29 09:02:30 rik Exp $
 * $FreeBSD$
 */

#include <sys/param.h>

#if defined(__FreeBSD__)
#include "opt_inet.h"
#include "opt_inet6.h"
#endif

#ifdef NetBSD1_3
#  if NetBSD1_3 > 6
#      include "opt_inet.h"
#      include "opt_inet6.h"
#      include "opt_iso.h"
#  endif
#endif

#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#if defined(__FreeBSD__)
#include <sys/random.h>
#endif
#include <sys/malloc.h>
#include <sys/mbuf.h>

#if defined (__OpenBSD__)
#include <sys/md5k.h>
#else
#include <sys/md5.h>
#endif

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <net/slcompress.h>

#if defined (__NetBSD__) || defined (__OpenBSD__)
#include <machine/cpu.h> /* XXX for softnet */
#endif

#include <machine/stdarg.h>

#include <netinet/in_var.h>
#ifdef INET
#include <netinet/ip.h>
#include <netinet/tcp.h>
#endif

#if defined (__FreeBSD__) || defined (__OpenBSD__)
#  include <netinet/if_ether.h>
#else
#  include <net/ethertypes.h>
#endif

#include <net/if_sppp.h>

/*
 * Frame Relay.
 */
#define FR_UI		0x03	/* Unnumbered Information */
#define FR_IP           0xCC    /* IP protocol identifier */
#define FR_PADDING      0x00    /* NLPID padding */
#define FR_SIGNALING    0x08    /* Q.933/T1.617 signaling identifier */
#define FR_SNAP         0x80    /* NLPID snap */

/*
 * Header flags.
 */
#define FR_DE           0x02    /* discard eligibility */
#define FR_FECN         0x04    /* forward notification */
#define FR_BECN         0x08    /* backward notification */

/*
 * Signaling message types.
 */
#define FR_MSG_ENQUIRY  0x75    /* status enquiry */
#define FR_MSG_STATUS   0x7d    /* status */

#define FR_ENQUIRY_SIZE	14

/*
 * Message field types.
 */
#define FR_FLD_RTYPE    0x01    /* report type */
#define FR_FLD_VERIFY   0x03    /* link verification */
#define FR_FLD_PVC      0x07    /* PVC status */
#define FR_FLD_LSHIFT5  0x95    /* locking shift 5 */

/*
 * Report types.
 */
#define FR_RTYPE_FULL   0       /* full status */
#define FR_RTYPE_SHORT  1       /* link verification only */
#define FR_RTYPE_SINGLE 2       /* single PVC status */

/* PVC status field. */
#define FR_DLCI_DELETE  0x04    /* PVC is deleted */
#define FR_DLCI_ACTIVE  0x02    /* PVC is operational */
#define FR_DLCI_NEW     0x08    /* PVC is new */

struct arp_req {
	unsigned short  htype;          /* hardware type = ARPHRD_FRELAY */
	unsigned short  ptype;          /* protocol type = ETHERTYPE_IP */
	unsigned char   halen;          /* hardware address length = 2 */
	unsigned char   palen;          /* protocol address length = 4 */
	unsigned short  op;             /* ARP/RARP/InARP request/reply */
	unsigned short  hsource;        /* hardware source address */
	unsigned short  psource1;       /* protocol source */
	unsigned short  psource2;
	unsigned short  htarget;        /* hardware target address */
	unsigned short  ptarget1;       /* protocol target */
	unsigned short  ptarget2;
} __packed;

#if defined(__FreeBSD__) && __FreeBSD_version < 501113
#define	SPP_FMT		"%s%d: "
#define	SPP_ARGS(ifp)	(ifp)->if_name, (ifp)->if_unit
#else
#define	SPP_FMT		"%s: "
#define	SPP_ARGS(ifp)	(ifp)->if_xname
#endif

/* almost every function needs these */
#define STDDCL							\
	struct ifnet *ifp = SP2IFP(sp);				\
	int debug = ifp->if_flags & IFF_DEBUG

static void sppp_fr_arp (struct sppp *sp, struct arp_req *req, u_short addr);
static void sppp_fr_signal (struct sppp *sp, unsigned char *h, int len);

void sppp_fr_input (struct sppp *sp, struct mbuf *m)
{
	STDDCL;
	u_char *h = mtod (m, u_char*);
	int isr = -1;
	int dlci, hlen, proto;

	/* Get the DLCI number. */
	if (m->m_pkthdr.len < 10) {
bad:            m_freem (m);
		return;
	}
	dlci = (h[0] << 2 & 0x3f0) | (h[1] >> 4 & 0x0f);

	/* Process signaling packets. */
	if (dlci == 0) {
		sppp_fr_signal (sp, h, m->m_pkthdr.len);
		m_freem (m);
		return;
	}

	if (dlci != sp->fr_dlci) {
		if (debug)
			printf (SPP_FMT "Received packet from invalid DLCI %d\n",
				SPP_ARGS(ifp), dlci);
		goto bad;
	}

	/* Process the packet. */
	if (ntohs (*(short*) (h+2)) == ETHERTYPE_IP) {
                /* Prehistoric IP framing? */
		h[2] = FR_UI;
		h[3] = FR_IP;
	}
	if (h[2] != FR_UI) {
		if (debug)
			printf (SPP_FMT "Invalid frame relay header flag 0x%02x\n",
				SPP_ARGS(ifp), h[2]);
		goto bad;
	}
	switch (h[3]) {
	default:
		if (debug)
			printf (SPP_FMT "Unsupported NLPID 0x%02x\n",
				SPP_ARGS(ifp), h[3]);
		goto bad;

	case FR_PADDING:
		if (h[4] != FR_SNAP) {
			if (debug)
				printf (SPP_FMT "Bad NLPID 0x%02x\n",
					SPP_ARGS(ifp), h[4]);
			goto bad;
		}
		if (h[5] || h[6] || h[7]) {
			if (debug)
				printf (SPP_FMT "Bad OID 0x%02x-0x%02x-0x%02x\n",
					SPP_ARGS(ifp),
					h[5], h[6], h[7]);
			goto bad;
		}
		proto = ntohs (*(short*) (h+8));
		if (proto == ETHERTYPE_ARP) {
			/* Process the ARP request. */
			if (m->m_pkthdr.len != 10 + sizeof (struct arp_req)) {
				if (debug)
					printf (SPP_FMT "Bad ARP request size = %d bytes\n",
						SPP_ARGS(ifp),
						m->m_pkthdr.len);
				goto bad;
			}
			sppp_fr_arp (sp, (struct arp_req*) (h + 10),
				h[0] << 8 | h[1]);
			m_freem (m);
			return;
		}
		hlen = 10;
		break;

	case FR_IP:
		proto = ETHERTYPE_IP;
		hlen = 4;
		break;
	}

	/* Remove frame relay header. */
	m_adj (m, hlen);

	switch (proto) {
	default:
		if_inc_counter(ifp, IFCOUNTER_NOPROTO, 1);
drop:		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
		m_freem (m);
		return;
#ifdef INET
	case ETHERTYPE_IP:
		isr = NETISR_IP;
		break;
#endif
	}

	if (! (ifp->if_flags & IFF_UP))
		goto drop;

	M_SETFIB(m, ifp->if_fib);

	/* Check queue. */
	if (netisr_queue(isr, m)) {	/* (0) on success. */
		if (debug)
			log(LOG_DEBUG, SPP_FMT "protocol queue overflow\n",
				SPP_ARGS(ifp));
	}
}

/*
 * Add the frame relay header to the packet.
 * For IP the header length is 4 bytes,
 * for all other protocols - 10 bytes (RFC 1490).
 */
struct mbuf *sppp_fr_header (struct sppp *sp, struct mbuf *m,
	int family)
{
	STDDCL;
	u_char *h;
	int type, hlen;

	/* Prepend the space for Frame Relay header. */
	hlen = (family == AF_INET) ? 4 : 10;
	M_PREPEND (m, hlen, M_NOWAIT);
	if (! m)
		return 0;
	h = mtod (m, u_char*);

	/* Fill the header. */
	h[0] = sp->fr_dlci >> 2 & 0xfc;
	h[1] = sp->fr_dlci << 4 | 1;
	h[2] = FR_UI;

	switch (family) {
	default:
		if (debug)
			printf (SPP_FMT "Cannot handle address family %d\n",
				SPP_ARGS(ifp), family);
		m_freem (m);
		return 0;
#ifdef INET
	case AF_INET:
#if 0 /* Crashes on fragmented packets */
		/*
		 * Set the discard eligibility bit, if:
		 * 1) no fragmentation
		 * 2) length > 400 bytes
		 * 3a) the protocol is UDP or
		 * 3b) TCP data (no control bits)
		 */
		{
		struct ip *ip = (struct ip*) (h + hlen);
		struct tcphdr *tcp = (struct tcphdr*) ((long*)ip + ip->ip_hl);

		if (! (ip->ip_off & ~IP_DF) && ip->ip_len > 400 &&
		    (ip->ip_p == IPPROTO_UDP ||
		    ip->ip_p == IPPROTO_TCP && ! tcp->th_flags))
			h[1] |= FR_DE;
		}
#endif
		h[3] = FR_IP;
		return m;
#endif
#ifdef NS
	case AF_NS:
		type = 0x8137;
		break;
#endif
	}
	h[3] = FR_PADDING;
	h[4] = FR_SNAP;
	h[5] = 0;
	h[6] = 0;
	h[7] = 0;
	*(short*) (h+8) = htons(type);
	return m;
}

/*
 * Send periodical frame relay link verification messages via DLCI 0.
 * Called every 10 seconds (default value of T391 timer is 10 sec).
 * Every 6-th message is a full status request
 * (default value of N391 counter is 6).
 */
void sppp_fr_keepalive (struct sppp *sp)
{
	STDDCL;
	unsigned char *h, *p;
	struct mbuf *m;

	MGETHDR (m, M_NOWAIT, MT_DATA);
	if (! m)
		return;
	m->m_pkthdr.rcvif = 0;

	h = mtod (m, u_char*);
	p = h;
	*p++ = 0;                       /* DLCI = 0 */
	*p++ = 1;
	*p++ = FR_UI;
	*p++ = FR_SIGNALING;            /* NLPID = UNI call control */

	*p++ = 0;                       /* call reference length = 0 */
	*p++ = FR_MSG_ENQUIRY;          /* message type = status enquiry */

	*p++ = FR_FLD_LSHIFT5;          /* locking shift 5 */

	*p++ = FR_FLD_RTYPE;            /* report type field */
	*p++ = 1;                       /* report type length = 1 */
	if (sp->pp_seq[IDX_LCP] % 6)
		*p++ = FR_RTYPE_SHORT;  /* link verification only */
	else
		*p++ = FR_RTYPE_FULL;   /* full status needed */

	if (sp->pp_seq[IDX_LCP] >= 255)
		sp->pp_seq[IDX_LCP] = 0;
	*p++ = FR_FLD_VERIFY;           /* link verification type field */
	*p++ = 2;                       /* link verification field length = 2 */
	*p++ = ++sp->pp_seq[IDX_LCP];   /* our sequence number */
	*p++ = sp->pp_rseq[IDX_LCP];    /* last received sequence number */

	m->m_pkthdr.len = m->m_len = p - h;
	if (debug)
		printf (SPP_FMT "send lmi packet, seq=%d, rseq=%d\n",
			SPP_ARGS(ifp), (u_char) sp->pp_seq[IDX_LCP],
			(u_char) sp->pp_rseq[IDX_LCP]);

	if (! IF_HANDOFF_ADJ(&sp->pp_cpq, m, ifp, 3))
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
}

/*
 * Process the frame relay Inverse ARP request.
 */
static void sppp_fr_arp (struct sppp *sp, struct arp_req *req,
	u_short his_hardware_address)
{
	STDDCL;
	struct mbuf *m;
	struct arp_req *reply;
	u_char *h;
	u_short my_hardware_address;
	u_long his_ip_address, my_ip_address;

	if ((ntohs (req->htype) != ARPHRD_FRELAY ||
	    ntohs (req->htype) != 16) || /* for BayNetworks routers */
	    ntohs (req->ptype) != ETHERTYPE_IP) {
		if (debug)
			printf (SPP_FMT "Invalid ARP hardware/protocol type = 0x%x/0x%x\n",
				SPP_ARGS(ifp),
				ntohs (req->htype), ntohs (req->ptype));
		return;
	}
	if (req->halen != 2 || req->palen != 4) {
		if (debug)
			printf (SPP_FMT "Invalid ARP hardware/protocol address length = %d/%d\n",
				SPP_ARGS(ifp),
				req->halen, req->palen);
		return;
	}
	switch (ntohs (req->op)) {
	default:
		if (debug)
			printf (SPP_FMT "Invalid ARP op = 0x%x\n",
				SPP_ARGS(ifp), ntohs (req->op));
		return;

	case ARPOP_INVREPLY:
		/* Ignore. */
		return;

	case ARPOP_INVREQUEST:
		my_hardware_address = ntohs (req->htarget);
		his_ip_address = ntohs (req->psource1) << 16 |
			ntohs (req->psource2);
		my_ip_address = ntohs (req->ptarget1) << 16 |
			ntohs (req->ptarget2);
		break;
	}
	if (debug)
		printf (SPP_FMT "got ARP request, source=0x%04x/%d.%d.%d.%d, target=0x%04x/%d.%d.%d.%d\n",
			SPP_ARGS(ifp), ntohs (req->hsource),
			(unsigned char) (his_ip_address >> 24),
			(unsigned char) (his_ip_address >> 16),
			(unsigned char) (his_ip_address >> 8),
			(unsigned char) his_ip_address,
			my_hardware_address,
			(unsigned char) (my_ip_address >> 24),
			(unsigned char) (my_ip_address >> 16),
			(unsigned char) (my_ip_address >> 8),
			(unsigned char) my_ip_address);

	sppp_get_ip_addrs (sp, &my_ip_address, 0, 0);
	if (! my_ip_address)
		return;         /* nothing to reply */

	if (debug)
		printf (SPP_FMT "send ARP reply, source=0x%04x/%d.%d.%d.%d, target=0x%04x/%d.%d.%d.%d\n",
			SPP_ARGS(ifp), my_hardware_address,
			(unsigned char) (my_ip_address >> 24),
			(unsigned char) (my_ip_address >> 16),
			(unsigned char) (my_ip_address >> 8),
			(unsigned char) my_ip_address,
			his_hardware_address,
			(unsigned char) (his_ip_address >> 24),
			(unsigned char) (his_ip_address >> 16),
			(unsigned char) (his_ip_address >> 8),
			(unsigned char) his_ip_address);

	/* Send the Inverse ARP reply. */
	MGETHDR (m, M_NOWAIT, MT_DATA);
	if (! m)
		return;
	m->m_pkthdr.len = m->m_len = 10 + sizeof (*reply);
	m->m_pkthdr.rcvif = 0;

	h = mtod (m, u_char*);
	reply = (struct arp_req*) (h + 10);

	h[0] = his_hardware_address >> 8;
	h[1] = his_hardware_address;
	h[2] = FR_UI;
	h[3] = FR_PADDING;
	h[4] = FR_SNAP;
	h[5] = 0;
	h[6] = 0;
	h[7] = 0;
	*(short*) (h+8) = htons (ETHERTYPE_ARP);

	reply->htype    = htons (ARPHRD_FRELAY);
	reply->ptype    = htons (ETHERTYPE_IP);
	reply->halen    = 2;
	reply->palen    = 4;
	reply->op       = htons (ARPOP_INVREPLY);
	reply->hsource  = htons (my_hardware_address);
	reply->psource1 = htonl (my_ip_address);
	reply->psource2 = htonl (my_ip_address) >> 16;
	reply->htarget  = htons (his_hardware_address);
	reply->ptarget1 = htonl (his_ip_address);
	reply->ptarget2 = htonl (his_ip_address) >> 16;

	if (! IF_HANDOFF_ADJ(&sp->pp_cpq, m, ifp, 3))
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
}

/*
 * Process the input signaling packet (DLCI 0).
 * The implemented protocol is ANSI T1.617 Annex D.
 */
static void sppp_fr_signal (struct sppp *sp, unsigned char *h, int len)
{
	STDDCL;
	u_char *p;
	int dlci;

	if (h[2] != FR_UI || h[3] != FR_SIGNALING || h[4] != 0) {
		if (debug)
			printf (SPP_FMT "Invalid signaling header\n",
				SPP_ARGS(ifp));
bad:            if (debug) {
			printf ("%02x", *h++);
			while (--len > 0)
				printf ("-%02x", *h++);
			printf ("\n");
		}
		return;
	}
	if (h[5] == FR_MSG_ENQUIRY) {
		if (len == FR_ENQUIRY_SIZE &&
		    h[12] == (u_char) sp->pp_seq[IDX_LCP]) {
			sp->pp_seq[IDX_LCP] = random();
			printf (SPP_FMT "loopback detected\n",
				SPP_ARGS(ifp));
		}
		return;
	}
	if (h[5] != FR_MSG_STATUS) {
		if (debug)
			printf (SPP_FMT "Unknown signaling message: 0x%02x\n",
				SPP_ARGS(ifp), h[5]);
		goto bad;
	}

	/* Parse message fields. */
	for (p=h+6; p<h+len; ) {
		switch (*p) {
		default:
			if (debug)
				printf (SPP_FMT "Unknown signaling field 0x%x\n",
					SPP_ARGS(ifp), *p);
			break;
		case FR_FLD_LSHIFT5:
		case FR_FLD_RTYPE:
			/* Ignore. */
			break;
		case FR_FLD_VERIFY:
			if (p[1] != 2) {
				if (debug)
					printf (SPP_FMT "Invalid signaling verify field length %d\n",
						SPP_ARGS(ifp), p[1]);
				break;
			}
			sp->pp_rseq[IDX_LCP] = p[2];
			if (debug) {
				printf (SPP_FMT "got lmi reply rseq=%d, seq=%d",
					SPP_ARGS(ifp), p[2], p[3]);
				if (p[3] != (u_char) sp->pp_seq[IDX_LCP])
					printf (" (really %d)",
						(u_char) sp->pp_seq[IDX_LCP]);
				printf ("\n");
			}
			break;
		case FR_FLD_PVC:
			if (p[1] < 3) {
				if (debug)
					printf (SPP_FMT "Invalid PVC status length %d\n",
						SPP_ARGS(ifp), p[1]);
				break;
			}
			dlci = (p[2] << 4 & 0x3f0) | (p[3] >> 3 & 0x0f);
			if (! sp->fr_dlci)
				sp->fr_dlci = dlci;
			if (sp->fr_status != p[4])
				printf (SPP_FMT "DLCI %d %s%s\n",
					SPP_ARGS(ifp), dlci,
					p[4] & FR_DLCI_DELETE ? "deleted" :
					p[4] & FR_DLCI_ACTIVE ? "active" : "passive",
					p[4] & FR_DLCI_NEW ? ", new" : "");
			sp->fr_status = p[4];
			break;
		}
		if (*p & 0x80)
			++p;
		else if (p < h+len+1 && p[1])
			p += 2 + p[1];
		else {
			if (debug)
				printf (SPP_FMT "Invalid signaling field 0x%x\n",
					SPP_ARGS(ifp), *p);
			goto bad;
		}
	}
}
