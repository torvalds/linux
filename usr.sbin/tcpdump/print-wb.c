/*	$OpenBSD: print-wb.c,v 1.11 2018/07/06 05:47:22 dlg Exp $	*/

/*
 * Copyright (c) 1993, 1994, 1995, 1996
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
 */

#include <sys/types.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <stdio.h>

#include "interface.h"
#include "addrtoname.h"

/* XXX need to add byte-swapping macros! */

/*
 * Largest packet size.  Everything should fit within this space.
 * For instance, multiline objects are sent piecewise.
 */
#define MAXFRAMESIZE 1024

/*
 * Multiple drawing ops can be sent in one packet.  Each one starts on a
 * an even multiple of DOP_ALIGN bytes, which must be a power of two.
 */
#define DOP_ALIGN 4
#define DOP_ROUNDUP(x)	((((int)(x)) + (DOP_ALIGN - 1)) & ~(DOP_ALIGN - 1))
#define DOP_NEXT(d)\
	((struct dophdr *)((u_char *)(d) + \
			  DOP_ROUNDUP(ntohs((d)->dh_len) + sizeof(*(d)))))

/*
 * Format of the whiteboard packet header.
 * The transport level header.
 */
struct pkt_hdr {
	u_int32_t ph_src;		/* site id of source */
	u_int32_t ph_ts;		/* time stamp (for skew computation) */
	u_short ph_version;	/* version number */
	u_char ph_type;		/* message type */
	u_char ph_flags;	/* message flags */
};

/* Packet types */
#define PT_DRAWOP	0	/* drawing operation */
#define PT_ID		1	/* announcement packet */
#define PT_RREQ		2	/* repair request */
#define PT_RREP		3	/* repair reply */
#define PT_KILL		4	/* terminate participation */
#define PT_PREQ         5       /* page vector request */
#define PT_PREP         7       /* page vector reply */

/* flags */
#define PF_USER		0x01	/* hint that packet has interactive data */
#define PF_VIS		0x02	/* only visible ops wanted */

struct PageID {
	u_int32_t p_sid;		/* session id of initiator */
	u_int32_t p_uid;		/* page number */
};

struct dophdr {
	u_int32_t  dh_ts;		/* sender's timestamp */
	u_short	dh_len;		/* body length */
	u_char	dh_flags;
	u_char	dh_type;	/* body type */
	/* body follows */
};
/*
 * Drawing op sub-types.
 */
#define DT_RECT         2
#define DT_LINE         3
#define DT_ML           4
#define DT_DEL          5
#define DT_XFORM        6
#define DT_ELL          7
#define DT_CHAR         8
#define DT_STR          9
#define DT_NOP          10
#define DT_PSCODE       11
#define DT_PSCOMP       12
#define DT_REF          13
#define DT_SKIP         14
#define DT_HOLE         15
#define DT_MAXTYPE      15

/*
 * A drawing operation.
 */
struct pkt_dop {
	struct PageID pd_page;	/* page that operations apply to */
	u_int32_t	pd_sseq;	/* start sequence number */
	u_int32_t	pd_eseq;	/* end sequence number */
	/* drawing ops follow */
};

/*
 * A repair request.
 */
struct pkt_rreq {
        u_int32_t pr_id;           /* source id of drawops to be repaired */
        struct PageID pr_page;           /* page of drawops */
        u_int32_t pr_sseq;         /* start seqno */
        u_int32_t pr_eseq;         /* end seqno */
};

/*
 * A repair reply.
 */
struct pkt_rrep {
	u_int32_t pr_id;	/* original site id of ops  */
	struct pkt_dop pr_dop;
	/* drawing ops follow */
};

struct id_off {
        u_int32_t id;
        u_int32_t off;
};

struct pgstate {
	u_int32_t slot;
	struct PageID page;
	u_short nid;
	u_short rsvd;
        /* seqptr's */
};

/*
 * An announcement packet.
 */
struct pkt_id {
	u_int32_t pi_mslot;
        struct PageID    pi_mpage;        /* current page */
	struct pgstate pi_ps;
        /* seqptr's */
        /* null-terminated site name */
};

struct pkt_preq {
        struct PageID  pp_page;
        u_int32_t  pp_low;
        u_int32_t  pp_high;
};

struct pkt_prep {
        u_int32_t  pp_n;           /* size of pageid array */
        /* pgstate's follow */
};

static int
wb_id(const struct pkt_id *id, u_int len)
{
	int i;
	const char *cp;
	const struct id_off *io;
	char c;
	int nid;

	printf("wb-id:");
	len -= sizeof(*id);
	if ((u_char *)(id + 1) > snapend)
		return (-1);

	printf(" %u/%s:%u (max %u/%s:%u) ",
	       (u_int32_t)ntohl(id->pi_ps.slot),
	       ipaddr_string(&id->pi_ps.page.p_sid),
	       (u_int32_t)ntohl(id->pi_ps.page.p_uid),
	       (u_int32_t)ntohl(id->pi_mslot),
	       ipaddr_string(&id->pi_mpage.p_sid),
	       (u_int32_t)ntohl(id->pi_mpage.p_uid));

	nid = ntohs(id->pi_ps.nid);
	len -= sizeof(*io) * nid;
	io = (struct id_off *)(id + 1);
	cp = (char *)(io + nid);
	if ((u_char *)cp + len <= snapend) {
		putchar('"');
		(void)fn_print((u_char *)cp, (u_char *)cp + len);
		putchar('"');
	}

	c = '<';
	for (i = 0; i < nid && (u_char *)io < snapend; ++io, ++i) {
		printf("%c%s:%u",
		    c, ipaddr_string(&io->id), (u_int32_t)ntohl(io->off));
		c = ',';
	}
	if (i >= nid) {
		printf(">");
		return (0);
	}
	return (-1);
}

static int
wb_rreq(const struct pkt_rreq *rreq, u_int len)
{
	printf("wb-rreq:");
	if (len < sizeof(*rreq) || (u_char *)(rreq + 1) > snapend)
		return (-1);

	printf(" please repair %s %s:%u<%u:%u>",
	       ipaddr_string(&rreq->pr_id),
	       ipaddr_string(&rreq->pr_page.p_sid),
	       (u_int32_t)ntohl(rreq->pr_page.p_uid),
	       (u_int32_t)ntohl(rreq->pr_sseq),
	       (u_int32_t)ntohl(rreq->pr_eseq));
	return (0);
}

static int
wb_preq(const struct pkt_preq *preq, u_int len)
{
	printf("wb-preq:");
	if (len < sizeof(*preq) || (u_char *)(preq + 1) > snapend)
		return (-1);

	printf(" need %u/%s:%u",
	       (u_int32_t)ntohl(preq->pp_low),
	       ipaddr_string(&preq->pp_page.p_sid),
	       (u_int32_t)ntohl(preq->pp_page.p_uid));
	return (0);
}

static int
wb_prep(const struct pkt_prep *prep, u_int len)
{
	int n;
	const struct pgstate *ps;
	const u_char *ep = snapend;

	printf("wb-prep:");
	if (len < sizeof(*prep)) {
		return (-1);
	}
	n = ntohl(prep->pp_n);
	ps = (const struct pgstate *)(prep + 1);
	while (--n >= 0 && (u_char *)ps < ep) {
		const struct id_off *io, *ie;
		char c = '<';

		printf(" %u/%s:%u",
		    (u_int32_t)ntohl(ps->slot),
		    ipaddr_string(&ps->page.p_sid),
		    (u_int32_t)ntohl(ps->page.p_uid));
		io = (struct id_off *)(ps + 1);
		for (ie = io + ps->nid; io < ie && (u_char *)io < ep; ++io) {
			printf("%c%s:%u", c, ipaddr_string(&io->id),
			    (u_int32_t)ntohl(io->off));
			c = ',';
		}
		printf(">");
		ps = (struct pgstate *)io;
	}
	return ((u_char *)ps <= ep? 0 : -1);
}


char *dopstr[] = {
	"dop-0!",
	"dop-1!",
	"RECT",
	"LINE",
	"ML",
	"DEL",
	"XFORM",
	"ELL",
	"CHAR",
	"STR",
	"NOP",
	"PSCODE",
	"PSCOMP",
	"REF",
	"SKIP",
	"HOLE",
};

static int
wb_dops(const struct dophdr *dh, u_int32_t ss, u_int32_t es)
{
	printf(" <");
	for ( ; ss <= es; ++ss) {
		int t = dh->dh_type;

		if (t > DT_MAXTYPE)
			printf(" dop-%d!", t);
		else {
			printf(" %s", dopstr[t]);
			if (t == DT_SKIP || t == DT_HOLE) {
				int ts = ntohl(dh->dh_ts);
				printf("%d", ts - ss + 1);
				if (ss > ts || ts > es) {
					printf("[|]");
					if (ts < ss)
						return (0);
				}
				ss = ts;
			}
		}
		dh = DOP_NEXT(dh);
		if ((u_char *)dh > snapend) {
			printf("[|wb]");
			break;
		}
	}
	printf(" >");
	return (0);
}

static int
wb_rrep(const struct pkt_rrep *rrep, u_int len)
{
	const struct pkt_dop *dop = &rrep->pr_dop;

	printf("wb-rrep:");
	len -= sizeof(*rrep);
	if ((u_char *)(rrep + 1) > snapend)
		return (-1);

	printf(" for %s %s:%u<%u:%u>",
	    ipaddr_string(&rrep->pr_id),
	    ipaddr_string(&dop->pd_page.p_sid),
	    (u_int32_t)ntohl(dop->pd_page.p_uid),
	    (u_int32_t)ntohl(dop->pd_sseq),
	    (u_int32_t)ntohl(dop->pd_eseq));

	if (vflag)
		return (wb_dops((const struct dophdr *)(dop + 1),
		    ntohl(dop->pd_sseq), ntohl(dop->pd_eseq)));
	return (0);
}

static int
wb_drawop(const struct pkt_dop *dop, u_int len)
{
	printf("wb-dop:");
	len -= sizeof(*dop);
	if ((u_char *)(dop + 1) > snapend)
		return (-1);

	printf(" %s:%u<%u:%u>",
	    ipaddr_string(&dop->pd_page.p_sid),
	    (u_int32_t)ntohl(dop->pd_page.p_uid),
	    (u_int32_t)ntohl(dop->pd_sseq),
	    (u_int32_t)ntohl(dop->pd_eseq));

	if (vflag)
		return (wb_dops((const struct dophdr *)(dop + 1),
				ntohl(dop->pd_sseq), ntohl(dop->pd_eseq)));
	return (0);
}

/*
 * Print whiteboard multicast packets.
 */
void
wb_print(const void *hdr, u_int len)
{
	const struct pkt_hdr *ph;

	ph = (const struct pkt_hdr *)hdr;
	len -= sizeof(*ph);
	if ((u_char *)(ph + 1) <= snapend) {
		if (ph->ph_flags)
			printf("*");
		switch (ph->ph_type) {

		case PT_KILL:
			printf("wb-kill");
			return;

		case PT_ID:
			if (wb_id((struct pkt_id *)(ph + 1), len) >= 0)
				return;
			break;

		case PT_RREQ:
			if (wb_rreq((struct pkt_rreq *)(ph + 1), len) >= 0)
				return;
			break;

		case PT_RREP:
			if (wb_rrep((struct pkt_rrep *)(ph + 1), len) >= 0)
				return;
			break;

		case PT_DRAWOP:
			if (wb_drawop((struct pkt_dop *)(ph + 1), len) >= 0)
				return;
			break;

		case PT_PREQ:
			if (wb_preq((struct pkt_preq *)(ph + 1), len) >= 0)
				return;
			break;

		case PT_PREP:
			if (wb_prep((struct pkt_prep *)(ph + 1), len) >= 0)
				return;
			break;

		default:
			printf("wb-%d!", ph->ph_type);
			return;
		}
	}
	printf("[|wb]");
}
