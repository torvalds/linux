/*	$OpenBSD: print-pfsync.c,v 1.45 2024/12/11 04:05:53 dlg Exp $	*/

/*
 * Copyright (c) 2002 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_ipsp.h>

#include <net/pfvar.h>
#include <net/if_pfsync.h>

#include <ctype.h>
#include <netdb.h>
#include <pcap.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <vis.h>

#include "interface.h"
#include "addrtoname.h"
#include "pfctl_parser.h"
#include "pfctl.h"

void	pfsync_print(struct pfsync_header *, const u_char *, int);

void
pfsync_if_print(u_char *user, const struct pcap_pkthdr *h,
     const u_char *p)
{
	u_int caplen = h->caplen;
	snapend = p + caplen;

	ts_print(&h->ts);

	if (caplen < PFSYNC_HDRLEN) {
		printf("[|pfsync]");
		goto out;
	}

	pfsync_print((struct pfsync_header *)p,
	    p + sizeof(struct pfsync_header),
	    caplen - sizeof(struct pfsync_header));
out:
	if (xflag) {
		default_print((const u_char *)p, caplen);
	}
	putchar('\n');
}

void
pfsync_ip_print(const u_char *bp, u_int len, const u_char *bp2)
{
	struct pfsync_header *hdr = (struct pfsync_header *)bp;
	struct ip *ip = (struct ip *)bp2;

	if (vflag)
		printf("%s > %s: ", ipaddr_string(&ip->ip_src),
		    ipaddr_string(&ip->ip_dst));
	else
		printf("%s: ", ipaddr_string(&ip->ip_src));

	if (len < PFSYNC_HDRLEN)
		printf("[|pfsync]");
	else
		pfsync_print(hdr, bp + sizeof(struct pfsync_header),
		    len - sizeof(struct pfsync_header));
	putchar('\n');
}

const char *actnames[] = { PFSYNC_ACTIONS };

struct pfsync_actions {
	size_t len;
	int (*print)(int, const void *);
};

int	pfsync_print_clr(int, const void *);
int	pfsync_print_state(int, const void *);
int	pfsync_print_ins_ack(int, const void *);
int	pfsync_print_upd_c(int, const void *);
int	pfsync_print_upd_req(int, const void *);
int	pfsync_print_del_c(int, const void *);
int	pfsync_print_bus(int, const void *);
int	pfsync_print_tdb(int, const void *);
int	pfsync_print_eof(int, const void *);

struct pfsync_actions actions[] = {
	{ sizeof(struct pfsync_clr),		pfsync_print_clr },
	{ 0,					NULL },
	{ sizeof(struct pfsync_ins_ack),	pfsync_print_ins_ack },
	{ 0,					NULL },
	{ sizeof(struct pfsync_upd_c),		pfsync_print_upd_c },
	{ sizeof(struct pfsync_upd_req),	pfsync_print_upd_req },
	{ sizeof(struct pfsync_state),		pfsync_print_state },
	{ sizeof(struct pfsync_del_c),		pfsync_print_del_c },
	{ 0,					NULL },
	{ 0,					NULL },
	{ sizeof(struct pfsync_bus),		pfsync_print_bus },
	{ 0,					NULL },
	{ 0,					pfsync_print_eof },
	{ sizeof(struct pfsync_state),		pfsync_print_state },
	{ sizeof(struct pfsync_state),		pfsync_print_state },
	{ sizeof(struct pfsync_tdb),		pfsync_print_tdb },
};

void
pfsync_print(struct pfsync_header *hdr, const u_char *bp, int len)
{
	struct pfsync_subheader *subh;
	int count, plen, alen, flags = 0;
	int i;

	plen = ntohs(hdr->len);

	printf("PFSYNCv%d len %d", hdr->version, plen);

	if (hdr->version != PFSYNC_VERSION)
		return;

	plen -= sizeof(*hdr);

	if (vflag)
		flags |= PF_OPT_VERBOSE;
	if (vflag > 1)
		flags |= PF_OPT_VERBOSE2;
	if (!nflag)
		flags |= PF_OPT_USEDNS;

	while (plen > 0) {
		if (len < sizeof(*subh))
			break;

		subh = (struct pfsync_subheader *)bp;
		bp += sizeof(*subh);
		len -= sizeof(*subh);
		plen -= sizeof(*subh);

		if (subh->action >= PFSYNC_ACT_MAX) {
			printf("\n    act UNKNOWN id %d", subh->action);
			return;
		}

		count = ntohs(subh->count);
		printf("\n    act %s count %d", actnames[subh->action], count);
		alen = actions[subh->action].len;

		if (actions[subh->action].print == NULL) {
			printf("\n    unimplemented action");
			return;
		}

		for (i = 0; i < count; i++) {
			if (len < alen) {
				len = 0;
				break;
			}

			if (actions[subh->action].print(flags, bp) != 0)
				return;

			bp += alen;
			len -= alen;
			plen -= alen;
		}
	}

	if (plen > 0) {
		printf("\n    ...");
		return;
	}
	if (plen < 0) {
		printf("\n    invalid header length");
		return;
	}
	if (len > 0)
		printf("\n    invalid packet length");
}

int
pfsync_print_clr(int flags, const void *bp)
{
	const struct pfsync_clr *clr = bp;
	char ifname[IFNAMSIZ * 4 + 1];
	char *cp = ifname;
	int i;

	printf("\n\tcreatorid: %08x", htonl(clr->creatorid));
	if (clr->ifname[0] != '\0') {
		/* Treat clr->ifname as untrusted input. */
		for (i = 0; i < IFNAMSIZ && clr->ifname[i] != '\0'; i++)
			cp = vis(cp, clr->ifname[i], VIS_WHITE, 0);
		printf(" interface: %s", ifname);
	}

	return (0);
}

int
pfsync_print_state(int flags, const void *bp)
{
	struct pfsync_state *st = (struct pfsync_state *)bp;
	putchar('\n');
	print_state(st, flags);
	return (0);
}

int
pfsync_print_ins_ack(int flags, const void *bp)
{
	const struct pfsync_ins_ack *iack = bp;

	printf("\n\tid: %016llx creatorid: %08x", betoh64(iack->id),
	    ntohl(iack->creatorid));

	return (0);
}

int
pfsync_print_upd_c(int flags, const void *bp)
{
	const struct pfsync_upd_c *u = bp;

	printf("\n\tid: %016llx creatorid: %08x", betoh64(u->id),
	    ntohl(u->creatorid));

	return (0);
}

int
pfsync_print_upd_req(int flags, const void *bp)
{
	const struct pfsync_upd_req *ur = bp;

	printf("\n\tid: %016llx creatorid: %08x", betoh64(ur->id),
	    ntohl(ur->creatorid));

	return (0);
}

int
pfsync_print_del_c(int flags, const void *bp)
{
	const struct pfsync_del_c *d = bp;

	printf("\n\tid: %016llx creatorid: %08x", betoh64(d->id),
	    ntohl(d->creatorid));

	return (0);
}

int
pfsync_print_bus(int flags, const void *bp)
{
	const struct pfsync_bus *b = bp;
	u_int32_t endtime;
	int min, sec;
	const char *status;

	endtime = ntohl(b->endtime);
	sec = endtime % 60;
	endtime /= 60;
	min = endtime % 60;
	endtime /= 60;

	switch (b->status) {
	case PFSYNC_BUS_START:
		status = "start";
		break;
	case PFSYNC_BUS_END:
		status = "end";
		break;
	default:
		status = "UNKNOWN";
		break;
	}

	printf("\n\tcreatorid: %08x age: %.2u:%.2u:%.2u status: %s",
	    htonl(b->creatorid), endtime, min, sec, status);

	return (0);
}

int
pfsync_print_tdb(int flags, const void *bp)
{
	const struct pfsync_tdb *t = bp;

	printf("\n\tspi: 0x%08x rpl: %llu cur_bytes: %llu",
	    ntohl(t->spi), betoh64(t->rpl), betoh64(t->cur_bytes));

	return (0);
}

int
pfsync_print_eof(int flags, const void *bp)
{
	return (1);
}
