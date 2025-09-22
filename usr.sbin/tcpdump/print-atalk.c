/*	$OpenBSD: print-atalk.c,v 1.35 2021/12/01 18:28:45 deraadt Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Format and print AppleTalk packets.
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/if_ether.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "interface.h"
#include "addrtoname.h"
#include "ethertype.h"
#include "extract.h"			/* must come after interface.h */
#include "appletalk.h"
#include "savestr.h"
#include "privsep.h"

static struct tok type2str[] = {
	{ ddpRTMP,		"rtmp" },
	{ ddpRTMPrequest,	"rtmpReq" },
	{ ddpECHO,		"echo" },
	{ ddpIP,		"IP" },
	{ ddpARP,		"ARP" },
	{ ddpKLAP,		"KLAP" },
	{ 0,			NULL }
};

struct aarp {
	u_int16_t	htype, ptype;
	u_int8_t	halen, palen;
	u_int16_t	op;
	u_int8_t	hsaddr[6];
	u_int8_t	psaddr[4];
	u_int8_t	hdaddr[6];
	u_int8_t	pdaddr[4];
};

static char tstr[] = "[|atalk]";

static void atp_print(const struct atATP *, u_int);
static void atp_bitmap_print(u_char);
static void nbp_print(const struct atNBP *, u_int, u_short, u_char, u_char);
static const char *print_cstring(const char *, const u_char *);
static const struct atNBPtuple *nbp_tuple_print(const struct atNBPtuple *,
						const u_char *,
						u_short, u_char, u_char);
static const struct atNBPtuple *nbp_name_print(const struct atNBPtuple *,
					       const u_char *);
static const char *ataddr_string(u_short, u_char);
static void ddp_print(const u_char *, u_int, int, u_short, u_char, u_char);
static const char *ddpskt_string(int);

/*
 * Print AppleTalk Datagram Delivery Protocol packets
 * without the LLAP encapsulating header (i.e.
 * from Ethertalk)
 */
void
atalk_print(const u_char *bp, u_int length)
{
	const struct atDDP *dp;
	u_short snet;

	if (length < ddpSize) {
		printf(" [|ddp %d]", length);
		return;
	}
	dp = (const struct atDDP *)bp;
	snet = EXTRACT_16BITS(&dp->srcNet);
	printf("%s.%s", ataddr_string(snet, dp->srcNode),
	    ddpskt_string(dp->srcSkt));
	printf(" > %s.%s:",
	    ataddr_string(EXTRACT_16BITS(&dp->dstNet), dp->dstNode),
	    ddpskt_string(dp->dstSkt));
	bp += ddpSize;
	length -= ddpSize;
	ddp_print(bp, length, dp->type, snet, dp->srcNode, dp->srcSkt);
}

/*
 * Print AppleTalk Datagram Delivery Protocol packets
 * from localtalk (i.e. the 230 Kbps net built into
 * every Macintosh). We can get these from a localtalk
 * interface if we have one, or from UDP encapsulated tunnels.
 */
void
atalk_print_llap(const u_char *bp, u_int length)
{
	const struct LAP *lp;
	const struct atDDP *dp;
	const struct atShortDDP *sdp;
	u_short snet;

	if (length < sizeof(*lp)) {
		printf(" [|llap %d]", length);
		return;
	}

	lp = (struct LAP *)bp;
	bp += sizeof(*lp);
	length -= sizeof(*lp);
	switch (lp->type) {

	case lapShortDDP:
		if (length < ddpSSize) {
			printf(" [|sddp %d]", length);
			return;
		}
		sdp = (const struct atShortDDP *)bp;
		printf("%s.%s",
		    ataddr_string(0, lp->src), ddpskt_string(sdp->srcSkt));
		printf(" > %s.%s:",
		    ataddr_string(0, lp->dst), ddpskt_string(sdp->dstSkt));
		bp += ddpSSize;
		length -= ddpSSize;
		ddp_print(bp, length, sdp->type, 0, lp->src, sdp->srcSkt);
		break;

	case lapDDP:
		if (length < ddpSize) {
			printf(" [|ddp %d]", length);
			return;
		}
		dp = (const struct atDDP *)bp;
		snet = EXTRACT_16BITS(&dp->srcNet);
		printf("%s.%s", ataddr_string(snet, dp->srcNode),
		    ddpskt_string(dp->srcSkt));
		printf(" > %s.%s:",
		    ataddr_string(EXTRACT_16BITS(&dp->dstNet), dp->dstNode),
		    ddpskt_string(dp->dstSkt));
		bp += ddpSize;
		length -= ddpSize;
		ddp_print(bp, length, dp->type, snet, dp->srcNode, dp->srcSkt);
		break;

#ifdef notdef
	case lapKLAP:
		klap_print(bp, length);
		break;
#endif

	default:
		printf("%d > %d at-lap#%d %d",
		    lp->src, lp->dst, lp->type, length);
		break;
	}
}

/* XXX should probably pass in the snap header and do checks like arp_print() */
void
aarp_print(const u_char *bp, u_int length)
{
	const struct aarp *ap;

#define AT(member) ataddr_string((ap->member[1]<<8)|ap->member[2],ap->member[3])

	printf("aarp ");
	ap = (const struct aarp *)bp;
	if (ntohs(ap->htype) == 1 && ntohs(ap->ptype) == ETHERTYPE_ATALK &&
	    ap->halen == 6 && ap->palen == 4 )
		switch (ntohs(ap->op)) {

		case 1:				/* request */
			printf("who-has %s tell %s",
			    AT(pdaddr), AT(psaddr));
			return;

		case 2:				/* response */
			printf("reply %s is-at %s",
			    AT(pdaddr), etheraddr_string(ap->hdaddr));
			return;

		case 3:				/* probe (oy!) */
			printf("probe %s tell %s",
			    AT(pdaddr), AT(psaddr));
			return;
		}
	printf("len %u op %u htype %u ptype %#x halen %u palen %u",
	    length, ntohs(ap->op), ntohs(ap->htype), ntohs(ap->ptype),
	    ap->halen, ap->palen);
}

static void
ddp_print(const u_char *bp, u_int length, int t,
	  u_short snet, u_char snode, u_char skt)
{

	if ((intptr_t)bp & (sizeof(long)-1)) {
		static u_char *abuf = NULL;
		int clen = snapend - bp;
		if (clen > snaplen)
			clen = snaplen;

		if (abuf == NULL) {
			abuf = malloc(snaplen);
			if (abuf == NULL)
				error("ddp_print: malloc");
		}
		memmove((char *)abuf, (char *)bp, min(length, clen));
		snapend = abuf + clen;
		packetp = abuf;
		bp = abuf;
	}

	switch (t) {

	case ddpNBP:
		nbp_print((const struct atNBP *)bp, length, snet, snode, skt);
		break;

	case ddpATP:
		atp_print((const struct atATP *)bp, length);
		break;

	default:
		printf(" at-%s %d", tok2str(type2str, NULL, t), length);
		break;
	}
}

static void
atp_print(const struct atATP *ap, u_int length)
{
	char c;
	u_int32_t data;

	if ((const u_char *)(ap + 1) > snapend) {
		/* Just bail if we don't have the whole chunk. */
		printf("%s", tstr);
		return;
	}
	length -= sizeof(*ap);
	switch (ap->control & 0xc0) {

	case atpReqCode:
		printf(" atp-req%s %d",
		    ap->control & atpXO? " " : "*",
		    EXTRACT_16BITS(&ap->transID));

		atp_bitmap_print(ap->bitmap);

		if (length != 0)
			printf(" [len=%d]", length);

		switch (ap->control & (atpEOM|atpSTS)) {
		case atpEOM:
			printf(" [EOM]");
			break;
		case atpSTS:
			printf(" [STS]");
			break;
		case atpEOM|atpSTS:
			printf(" [EOM,STS]");
			break;
		}
		break;

	case atpRspCode:
		printf(" atp-resp%s%d:%d (%d)",
		    ap->control & atpEOM? "*" : " ",
		    EXTRACT_16BITS(&ap->transID), ap->bitmap, length);
		switch (ap->control & (atpXO|atpSTS)) {
		case atpXO:
			printf(" [XO]");
			break;
		case atpSTS:
			printf(" [STS]");
			break;
		case atpXO|atpSTS:
			printf(" [XO,STS]");
			break;
		}
		break;

	case atpRelCode:
		printf(" atp-rel  %d", EXTRACT_16BITS(&ap->transID));

		atp_bitmap_print(ap->bitmap);

		/* length should be zero */
		if (length)
			printf(" [len=%d]", length);

		/* there shouldn't be any control flags */
		if (ap->control & (atpXO|atpEOM|atpSTS)) {
			c = '[';
			if (ap->control & atpXO) {
				printf("%cXO", c);
				c = ',';
			}
			if (ap->control & atpEOM) {
				printf("%cEOM", c);
				c = ',';
			}
			if (ap->control & atpSTS) {
				printf("%cSTS", c);
				c = ',';
			}
			printf("]");
		}
		break;

	default:
		printf(" atp-0x%x  %d (%d)", ap->control,
		    EXTRACT_16BITS(&ap->transID), length);
		break;
	}
	data = EXTRACT_32BITS(&ap->userData);
	if (data != 0)
		printf(" 0x%x", data);
}

static void
atp_bitmap_print(u_char bm)
{
	char c;
	int i;

	/*
	 * The '& 0xff' below is needed for compilers that want to sign
	 * extend a u_char, which is the case with the Ultrix compiler.
	 * (gcc is smart enough to eliminate it, at least on the Sparc).
	 */
	if ((bm + 1) & (bm & 0xff)) {
		c = '<';
		for (i = 0; bm; ++i) {
			if (bm & 1) {
				printf("%c%d", c, i);
				c = ',';
			}
			bm >>= 1;
		}
		printf(">");
	} else {
		for (i = 0; bm; ++i)
			bm >>= 1;
		if (i > 1)
			printf("<0-%d>", i - 1);
		else
			printf("<0>");
	}
}

static void
nbp_print(const struct atNBP *np, u_int length, u_short snet,
	  u_char snode, u_char skt)
{
	const struct atNBPtuple *tp =
			(struct atNBPtuple *)((u_char *)np + nbpHeaderSize);
	int i;
	const u_char *ep;

	if (length < nbpHeaderSize) {
		printf(" truncated-nbp %d", length);
		return;
	}

	length -= nbpHeaderSize;
	if (length < 8) {
		/* must be room for at least one tuple */
		if (np->control == nbpNATLKerr) {
			printf(" nbp-netatalk_err");
			return;
		} else if (np->control == nbpNATLKok) {
			printf(" nbp-netatalk_ok");
			return;
		}
		printf(" truncated-nbp nbp-0x%x  %d (%d)",
		    np->control, np->id, length + nbpHeaderSize);
		return;
	}
	/* ep points to end of available data */
	ep = snapend;
	if ((const u_char *)tp > ep) {
		printf("%s", tstr);
		return;
	}
	switch (i = np->control & 0xf0) {

	case nbpBrRq:
	case nbpLkUp:
		printf(i == nbpLkUp? " nbp-lkup %d:":" nbp-brRq %d:", np->id);
		if ((const u_char *)(tp + 1) > ep) {
			printf("%s", tstr);
			return;
		}
		(void)nbp_name_print(tp, ep);
		/*
		 * look for anomalies: the spec says there can only
		 * be one tuple, the address must match the source
		 * address and the enumerator should be zero.
		 */
		if ((np->control & 0xf) != 1)
			printf(" [ntup=%d]", np->control & 0xf);
		if (tp->enumerator)
			printf(" [enum=%d]", tp->enumerator);
		if (EXTRACT_16BITS(&tp->net) != snet ||
		    tp->node != snode || tp->skt != skt)
			printf(" [addr=%s.%d]",
			    ataddr_string(EXTRACT_16BITS(&tp->net),
			    tp->node), tp->skt);
		break;

	case nbpLkUpReply:
		printf(" nbp-reply %d:", np->id);

		/* print each of the tuples in the reply */
		for (i = np->control & 0xf; --i >= 0 && tp; )
			tp = nbp_tuple_print(tp, ep, snet, snode, skt);
		break;

	case nbpNATLKrgstr:
	case nbpNATLKunrgstr:
		printf((i == nbpNATLKrgstr) ?
		    " nbp-netatalk_rgstr %d:" :
		    " nbp-netatalk_unrgstr %d:",
		    np->id);
		for (i = np->control & 0xf; --i >= 0 && tp; )
			tp = nbp_tuple_print(tp, ep, snet, snode, skt);
		break;

	default:
		printf(" nbp-0x%x  %d (%d)", np->control, np->id,
		    length);
		break;
	}
}

/* print a counted string */
static const char *
print_cstring(const char *cp, const u_char *ep)
{
	u_int length;

	if (cp >= (const char *)ep) {
		printf("%s", tstr);
		return (0);
	}
	length = *cp++;

	/* Spec says string can be at most 32 bytes long */
	if (length > 32) {
		printf("[len=%d]", length);
		return (0);
	}
	while ((int)--length >= 0) {
		if (cp >= (char *)ep) {
			printf("%s", tstr);
			return (0);
		}
		putchar(*cp++);
	}
	return (cp);
}

static const struct atNBPtuple *
nbp_tuple_print(const struct atNBPtuple *tp,
		const u_char *ep,
		u_short snet, u_char snode,
		u_char skt)
{
	const struct atNBPtuple *tpn;

	if ((const u_char *)(tp + 1) > ep) {
		printf("%s", tstr);
		return 0;
	}
	tpn = nbp_name_print(tp, ep);

	/* if the enumerator isn't 1, print it */
	if (tp->enumerator != 1)
		printf("(%d)", tp->enumerator);

	/* if the socket doesn't match the src socket, print it */
	if (tp->skt != skt)
		printf(" %d", tp->skt);

	/* if the address doesn't match the src address, it's an anomaly */
	if (EXTRACT_16BITS(&tp->net) != snet || tp->node != snode)
		printf(" [addr=%s]",
		    ataddr_string(EXTRACT_16BITS(&tp->net), tp->node));

	return (tpn);
}

static const struct atNBPtuple *
nbp_name_print(const struct atNBPtuple *tp, const u_char *ep)
{
	const char *cp = (const char *)tp + nbpTupleSize;

	putchar(' ');

	/* Object */
	putchar('"');
	if ((cp = print_cstring(cp, ep)) != NULL) {
		/* Type */
		putchar(':');
		if ((cp = print_cstring(cp, ep)) != NULL) {
			/* Zone */
			putchar('@');
			if ((cp = print_cstring(cp, ep)) != NULL)
				putchar('"');
		}
	}
	return ((const struct atNBPtuple *)cp);
}


#define HASHNAMESIZE 4096

struct hnamemem {
	int addr;
	char *name;
	struct hnamemem *nxt;
};

static struct hnamemem hnametable[HASHNAMESIZE];

static const char *
ataddr_string(u_short atnet, u_char athost)
{
	struct hnamemem *tp, *tp2;
	int i = (atnet << 8) | athost;
	char nambuf[HOST_NAME_MAX+1 + 20];

	for (tp = &hnametable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	/* didn't have the node name -- see if we've got the net name */
	i |= 255;
	for (tp2 = &hnametable[i & (HASHNAMESIZE-1)]; tp2->nxt; tp2 = tp2->nxt)
		if (tp2->addr == i) {
			tp->addr = (atnet << 8) | athost;
			tp->nxt = newhnamemem();
			(void)snprintf(nambuf, sizeof nambuf, "%s.%d",
			    tp2->name, athost);
			tp->name = savestr(nambuf);
			return (tp->name);
		}

	tp->addr = (atnet << 8) | athost;
	tp->nxt = newhnamemem();
	if (athost != 255)
		(void)snprintf(nambuf, sizeof nambuf, "%d.%d.%d",
			atnet >> 8, atnet & 0xff, athost);
	else
		(void)snprintf(nambuf, sizeof nambuf, "%d.%d",
			atnet >> 8, atnet & 0xff);
	tp->name = savestr(nambuf);

	return (tp->name);
}

static struct tok skt2str[] = {
	{ rtmpSkt,	"rtmp" },	/* routing table maintenance */
	{ nbpSkt,	"nis" },	/* name info socket */
	{ echoSkt,	"echo" },	/* AppleTalk echo protocol */
	{ zipSkt,	"zip" },	/* zone info protocol */
	{ 0,		NULL }
};

static const char *
ddpskt_string(int skt)
{
	static char buf[12];

	if (nflag) {
		(void)snprintf(buf, sizeof buf, "%d", skt);
		return (buf);
	}
	return (tok2str(skt2str, "%d", skt));
}
