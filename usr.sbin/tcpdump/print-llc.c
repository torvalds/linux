/*	$OpenBSD: print-llc.c,v 1.23 2023/02/28 10:04:50 claudio Exp $	*/

/*
 * Copyright (c) 1992, 1993, 1994, 1995, 1996, 1997
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
 * Code by Matt Thomas, Digital Equipment Corporation
 *	with an awful lot of hacking by Jeffrey Mogul, DECWRL
 */

#include <sys/time.h>

#include <netinet/in.h>

#include <ctype.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"			/* must come after interface.h */
#include "ethertype.h"

#include "llc.h"

static struct tok cmd2str[] = {
	{ LLC_UI,	"ui" },
	{ LLC_TEST,	"test" },
	{ LLC_XID,	"xid" },
	{ LLC_UA,	"ua" },
	{ LLC_DISC,	"disc" },
	{ LLC_DM,	"dm" },
	{ LLC_SABME,	"sabme" },
	{ LLC_FRMR,	"frmr" },
	{ 0,		NULL }
};

/*
 * Returns non-zero IFF it succeeds in printing the header
 */
int
llc_print(const u_char *p, u_int length, u_int caplen,
	  const u_char *esrc, const u_char *edst)
{
	struct llc llc;
	u_short et;
#if 0
	u_short control;
#endif
	int ret;

	if (caplen < 3) {
		printf("[|llc]");
		default_print((u_char *)p, caplen);
		return(0);
	}

	/* Watch out for possible alignment problems */
	memcpy((char *)&llc, (char *)p, min(caplen, sizeof(llc)));

	if (llc.ssap == LLCSAP_GLOBAL && llc.dsap == LLCSAP_GLOBAL) {
		ipx_print(p, length);
		return (1);
	}
#ifdef notyet
	else if (p[0] == 0xf0 && p[1] == 0xf0)
		netbios_print(p, length);
#endif
	if (llc.ssap == LLCSAP_ISONS && llc.dsap == LLCSAP_ISONS
	    && llc.llcui == LLC_UI) {
		isoclns_print(p + 3, length - 3, caplen - 3, esrc, edst);
		return (1);
	}

	if (llc.ssap == LLCSAP_SNAP && llc.dsap == LLCSAP_SNAP
	    && llc.llcui == LLC_UI) {
		if (caplen < sizeof(llc)) {
		    printf("[|llc-snap]");
		    default_print((u_char *)p, caplen);
		    return (0);
		}

		/* Cisco Discovery Protocol  - SNAP & ether type 0x2000 */
		if (llc.ethertype[0] == 0x20 && llc.ethertype[1] == 0x00) {
			cdp_print(p, length, caplen, 8);
			return (1);
		}
		/* Shared Spanning Tree Protocol - SNAP & ether type 0x010b */
		if (llc.ethertype[0] == 0x01 && llc.ethertype[1] == 0x0b) {
			stp_print(p, length);
			return (1);
		}

		if (vflag)
			printf("snap %s ", protoid_string(llc.llcpi));

		caplen -= sizeof(llc);
		length -= sizeof(llc);
		p += sizeof(llc);

		/* This is an encapsulated Ethernet packet */
		et = EXTRACT_16BITS(&llc.ethertype[0]);

		/*
		 * Some protocols have special handling if they are 802.3
		 * SNAP encapsulated vs vers II encapsulated. Handle
		 * those special protocols here, and hand the rest to
		 * print-ether.c so we don't have to duplicate
		 * all that code here.
		 */
		switch (et) {
		case ETHERTYPE_ATALK:
			atalk_print(p, length);
			ret = 1;
			break;
		default:
			ret = ether_encap_print(et, p, length, caplen);
			break;
		}

		if (ret)
			return (ret);
	}

	if (llc.ssap == LLCSAP_8021D && llc.dsap == LLCSAP_8021D) {
		stp_print(p, length);
		return (1);
	}

#if 0
	if (llc.ssap == 0xf0 && llc.dsap == 0xf0) {
		/*
		 * we don't actually have a full netbeui parser yet, but the
		 * smb parser can handle many smb-in-netbeui packets, which
		 * is very useful, so we call that
		 */

		/*
		 * Skip the DSAP and LSAP.
		 */
		p += 2;
		length -= 2;
		caplen -= 2;

		/*
		 * OK, what type of LLC frame is this?  The length
		 * of the control field depends on that - S or I
		 * frames have a two-byte control field, and U frames
		 * have a one-byte control field.
		 */
		if ((llc.llcu & LLC_U_FMT) == LLC_U_FMT) {
			control = llc.llcu;
			p += 1;
			length -= 1;
			caplen -= 1;
		} else {
			control = llc.llcis;
			p += 2;
			length -= 2;
			caplen -= 2;
		}

		netbeui_print(control, p, p + min(caplen, length));
		return (1);
	}
#endif

	if ((llc.ssap & ~LLC_GSAP) == llc.dsap) {
		if (eflag)
			printf("%s ", llcsap_string(llc.dsap));
		else
			printf("%s > %s %s ",
			    etheraddr_string(esrc),
			    etheraddr_string(edst),
			    llcsap_string(llc.dsap));
	} else {
		if (eflag)
			printf("%s > %s ",
			    llcsap_string(llc.ssap & ~LLC_GSAP),
			    llcsap_string(llc.dsap));
		else
			printf("%s %s > %s %s ",
			    etheraddr_string(esrc),
			    llcsap_string(llc.ssap & ~LLC_GSAP),
			    etheraddr_string(edst),
			    llcsap_string(llc.dsap));
	}

	if ((llc.llcu & LLC_U_FMT) == LLC_U_FMT) {
		const char *m;
		char f;
		m = tok2str(cmd2str, "%02x", LLC_U_CMD(llc.llcu));
		switch ((llc.ssap & LLC_GSAP) | (llc.llcu & LLC_U_POLL)) {
		    case 0:			f = 'C'; break;
		    case LLC_GSAP:		f = 'R'; break;
		    case LLC_U_POLL:		f = 'P'; break;
		    case LLC_GSAP|LLC_U_POLL:	f = 'F'; break;
		    default:			f = '?'; break;
		}

		printf("%s/%c", m, f);

		if (caplen < 6) {
			default_print(p, caplen);
			return (0);
		}
		p += 3;
		length -= 3;
		caplen -= 3;

		if ((llc.llcu & ~LLC_U_POLL) == LLC_XID) {
		    if (*p == LLC_XID_FI) {
			printf(": %02x %02x", p[1], p[2]);
			p += 3;
			length -= 3;
			caplen -= 3;
		    }
		}

#if 0
		if (!strcmp(m,"ui") && f=='C') {
			/*
			 * we don't have a proper ipx decoder yet, but there
			 * is a partial one in the smb code
			 */
			ipx_netbios_print(p,p+min(caplen,length));
		}
#endif

	} else {
		char f;
		if (caplen < 4) {
			default_print(p, caplen);
			return (0);
		}
		llc.llcis = ntohs(llc.llcis);
		switch ((llc.ssap & LLC_GSAP) | (llc.llcu & LLC_U_POLL)) {
		    case 0:			f = 'C'; break;
		    case LLC_GSAP:		f = 'R'; break;
		    case LLC_U_POLL:		f = 'P'; break;
		    case LLC_GSAP|LLC_U_POLL:	f = 'F'; break;
		    default:			f = '?'; break;
		}

		if ((llc.llcu & LLC_S_FMT) == LLC_S_FMT) {
			static char *llc_s[] = { "rr", "rej", "rnr", "03" };
			printf("%s (r=%d,%c)",
			    llc_s[LLC_S_CMD(llc.llcis)],
			    LLC_IS_NR(llc.llcis),
			    f);
		} else {
			printf("I (s=%d,r=%d,%c)",
			    LLC_I_NS(llc.llcis),
			    LLC_IS_NR(llc.llcis),
			    f);
		}
		p += 4;
		length -= 4;
		caplen -= 4;
	}
	printf(" len=%d", length);
	return(1);
}
