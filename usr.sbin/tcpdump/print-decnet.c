/*	$OpenBSD: print-decnet.c,v 1.19 2021/12/01 18:28:45 deraadt Exp $	*/

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
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>

#ifdef	HAVE_LIBDNET
#include <netdnet/dnetdb.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "decnet.h"
#include "extract.h"
#include "interface.h"
#include "addrtoname.h"

/* Forwards */
static int print_decnet_ctlmsg(const union routehdr *, u_int, u_int);
static void print_t_info(int);
static int print_l1_routes(const char *, u_int);
static int print_l2_routes(const char *, u_int);
static void print_i_info(int);
static int print_elist(const char *, u_int);
static int print_nsp(const u_char *, u_int);
static void print_reason(int);
#ifdef	PRINT_NSPDATA
static void pdata(u_char *, int);
#endif

#ifdef	HAVE_LIBDNET
extern char *dnet_htoa(struct dn_naddr *);
#endif

void
decnet_print(const u_char *ap, u_int length, u_int caplen)
{
	static union routehdr rhcopy;
	union routehdr *rhp = &rhcopy;
	int mflags;
	int dst, src, hops;
	u_int rhlen, nsplen, pktlen;
	const u_char *nspp;

	if (length < sizeof(struct shorthdr)) {
		printf("[|decnet]");
		return;
	}

	TCHECK2(*ap, sizeof(short));
	pktlen = EXTRACT_LE_16BITS(ap);
	if (pktlen < sizeof(struct shorthdr)) {
		printf("[|decnet]");
		return;
	}
	if (pktlen > length) {
		printf("[|decnet]");
		return;
	}
	length = pktlen;

	rhlen = min(length, caplen);
	rhlen = min(rhlen, sizeof(*rhp));
	memcpy((char *)rhp, (char *)&(ap[sizeof(short)]), rhlen);

	TCHECK(rhp->rh_short.sh_flags);
	mflags = EXTRACT_LE_8BITS(rhp->rh_short.sh_flags);

	if (mflags & RMF_PAD) {
	    /* pad bytes of some sort in front of message */
	    u_int padlen = mflags & RMF_PADMASK;
	    if (vflag)
		printf("[pad:%d] ", padlen);
	    if (length < padlen + 2) {
		printf("[|decnet]");
		return;
	    }
	    TCHECK2(ap[sizeof(short)], padlen);
	    ap += padlen;
	    length -= padlen;
	    caplen -= padlen;
	    rhlen = min(length, caplen);
	    rhlen = min(rhlen, sizeof(*rhp));
	    memcpy((char *)rhp, (char *)&(ap[sizeof(short)]), rhlen);
	    mflags = EXTRACT_LE_8BITS(rhp->rh_short.sh_flags);
	}

	if (mflags & RMF_FVER) {
		printf("future-version-decnet");
		default_print(ap, min(length, caplen));
		return;
	}

	/* is it a control message? */
	if (mflags & RMF_CTLMSG) {
		if(!print_decnet_ctlmsg(rhp, length, caplen))
			goto trunc;
		return;
	}

	switch (mflags & RMF_MASK) {
	case RMF_LONG:
	    if (length < sizeof(struct longhdr)) {
		printf("[|decnet]");
		return;
	    }
	    TCHECK(rhp->rh_long);
	    dst =
		EXTRACT_LE_16BITS(rhp->rh_long.lg_dst.dne_remote.dne_nodeaddr);
	    src =
		EXTRACT_LE_16BITS(rhp->rh_long.lg_src.dne_remote.dne_nodeaddr);
	    hops = EXTRACT_LE_8BITS(rhp->rh_long.lg_visits);
	    nspp = &(ap[sizeof(short) + sizeof(struct longhdr)]);
	    nsplen = length - sizeof(struct longhdr);
	    break;
	case RMF_SHORT:
	    TCHECK(rhp->rh_short);
	    dst = EXTRACT_LE_16BITS(rhp->rh_short.sh_dst);
	    src = EXTRACT_LE_16BITS(rhp->rh_short.sh_src);
	    hops = (EXTRACT_LE_8BITS(rhp->rh_short.sh_visits) & VIS_MASK)+1;
	    nspp = &(ap[sizeof(short) + sizeof(struct shorthdr)]);
	    nsplen = length - sizeof(struct shorthdr);
	    break;
	default:
	    printf("unknown message flags under mask");
	    default_print((u_char *)ap, min(length, caplen));
	    return;
	}

	printf("%s > %s %d ",
	    dnaddr_string(src), dnaddr_string(dst), pktlen);
	if (vflag) {
	    if (mflags & RMF_RQR)
		printf("RQR ");
	    if (mflags & RMF_RTS)
		printf("RTS ");
	    if (mflags & RMF_IE)
		printf("IE ");
	    printf("%d hops ", hops);
	}

	if (!print_nsp(nspp, nsplen))
		goto trunc;
	return;

trunc:
	printf("[|decnet]");
	return;
}

static int
print_decnet_ctlmsg(const union routehdr *rhp, u_int length,
    u_int caplen)
{
	int mflags = EXTRACT_LE_8BITS(rhp->rh_short.sh_flags);
	union controlmsg *cmp = (union controlmsg *)rhp;
	int src, dst, info, blksize, eco, ueco, hello, other, vers;
	etheraddr srcea, rtea;
	int priority;
	char *rhpx = (char *)rhp;
	int ret;

	switch (mflags & RMF_CTLMASK) {
	case RMF_INIT:
	    printf("init ");
	    if (length < sizeof(struct initmsg))
		goto trunc;
	    TCHECK(cmp->cm_init);
	    src = EXTRACT_LE_16BITS(cmp->cm_init.in_src);
	    info = EXTRACT_LE_8BITS(cmp->cm_init.in_info);
	    blksize = EXTRACT_LE_16BITS(cmp->cm_init.in_blksize);
	    vers = EXTRACT_LE_8BITS(cmp->cm_init.in_vers);
	    eco = EXTRACT_LE_8BITS(cmp->cm_init.in_eco);
	    ueco = EXTRACT_LE_8BITS(cmp->cm_init.in_ueco);
	    hello = EXTRACT_LE_16BITS(cmp->cm_init.in_hello);
	    print_t_info(info);
	    printf("src %sblksize %d vers %d eco %d ueco %d hello %d",
		dnaddr_string(src), blksize, vers, eco, ueco, hello);

	    ret = 1;
	    break;
	case RMF_VER:
	    printf("verification ");
	    if (length < sizeof(struct verifmsg))
		goto trunc;
	    TCHECK(cmp->cm_ver);
	    src = EXTRACT_LE_16BITS(cmp->cm_ver.ve_src);
	    other = EXTRACT_LE_8BITS(cmp->cm_ver.ve_fcnval);
	    printf("src %s fcnval %o", dnaddr_string(src), other);
	    ret = 1;
	    break;
	case RMF_TEST:
	    printf("test ");
	    if (length < sizeof(struct testmsg))
		goto trunc;
	    TCHECK(cmp->cm_test);
	    src = EXTRACT_LE_16BITS(cmp->cm_test.te_src);
	    other = EXTRACT_LE_8BITS(cmp->cm_test.te_data);
	    printf("src %s data %o", dnaddr_string(src), other);
	    ret = 1;
	    break;
	case RMF_L1ROUT:
	    printf("lev-1-routing ");
	    if (length < sizeof(struct l1rout))
		goto trunc;
	    TCHECK(cmp->cm_l1rou);
	    src = EXTRACT_LE_16BITS(cmp->cm_l1rou.r1_src);
	    printf("src %s ", dnaddr_string(src));
	    ret = print_l1_routes(&(rhpx[sizeof(struct l1rout)]),
				length - sizeof(struct l1rout));
	    break;
	case RMF_L2ROUT:
	    printf("lev-2-routing ");
	    if (length < sizeof(struct l2rout))
		goto trunc;
	    TCHECK(cmp->cm_l2rout);
	    src = EXTRACT_LE_16BITS(cmp->cm_l2rout.r2_src);
	    printf("src %s ", dnaddr_string(src));
	    ret = print_l2_routes(&(rhpx[sizeof(struct l2rout)]),
				length - sizeof(struct l2rout));
	    break;
	case RMF_RHELLO:
	    printf("router-hello ");
	    if (length < sizeof(struct rhellomsg))
		goto trunc;
	    TCHECK(cmp->cm_rhello);
	    vers = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_vers);
	    eco = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_eco);
	    ueco = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_ueco);
	    memcpy((char *)&srcea, (char *)&(cmp->cm_rhello.rh_src),
		sizeof(srcea));
	    src = EXTRACT_LE_16BITS(srcea.dne_remote.dne_nodeaddr);
	    info = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_info);
	    blksize = EXTRACT_LE_16BITS(cmp->cm_rhello.rh_blksize);
	    priority = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_priority);
	    hello = EXTRACT_LE_16BITS(cmp->cm_rhello.rh_hello);
	    print_i_info(info);
	    printf("vers %d eco %d ueco %d src %s blksize %d pri %d hello %d",
		vers, eco, ueco, dnaddr_string(src), blksize, priority, hello);
	    ret = print_elist(&(rhpx[sizeof(struct rhellomsg)]),
				length - sizeof(struct rhellomsg));
	    break;
	case RMF_EHELLO:
	    printf("endnode-hello ");
	    if (length < sizeof(struct ehellomsg))
		goto trunc;
	    TCHECK(cmp->cm_ehello);
	    vers = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_vers);
	    eco = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_eco);
	    ueco = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_ueco);
	    memcpy((char *)&srcea, (char *)&(cmp->cm_ehello.eh_src),
		sizeof(srcea));
	    src = EXTRACT_LE_16BITS(srcea.dne_remote.dne_nodeaddr);
	    info = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_info);
	    blksize = EXTRACT_LE_16BITS(cmp->cm_ehello.eh_blksize);
	    /*seed*/
	    memcpy((char *)&rtea, (char *)&(cmp->cm_ehello.eh_router),
		sizeof(rtea));
	    dst = EXTRACT_LE_16BITS(rtea.dne_remote.dne_nodeaddr);
	    hello = EXTRACT_LE_16BITS(cmp->cm_ehello.eh_hello);
	    other = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_data);
	    print_i_info(info);
	    printf(
	"vers %d eco %d ueco %d src %s blksize %d rtr %s hello %d data %o",
		 vers, eco, ueco, dnaddr_string(src),
		 blksize, dnaddr_string(dst), hello, other);
	    ret = 1;
	    break;

	default:
	    printf("unknown control message");
	    default_print((u_char *)rhp, min(length, caplen));
	    ret = 1;
	    break;
	}
	return (ret);

trunc:
	return (0);
}

static void
print_t_info(int info)
{
	int ntype = info & 3;
	switch (ntype) {
	case 0: printf("reserved-ntype? "); break;
	case TI_L2ROUT: printf("l2rout "); break;
	case TI_L1ROUT: printf("l1rout "); break;
	case TI_ENDNODE: printf("endnode "); break;
	}
	if (info & TI_VERIF)
	    printf("verif ");
	if (info & TI_BLOCK)
	    printf("blo ");
}

static int
print_l1_routes(const char *rp, u_int len)
{
	int count;
	int id;
	int info;

	/* The last short is a checksum */
	while (len > (3 * sizeof(short))) {
	    TCHECK2(*rp, 3 * sizeof(short));
	    count = EXTRACT_LE_16BITS(rp);
	    if (count > 1024)
		return (1);	/* seems to be bogus from here on */
	    rp += sizeof(short);
	    len -= sizeof(short);
	    id = EXTRACT_LE_16BITS(rp);
	    rp += sizeof(short);
	    len -= sizeof(short);
	    info = EXTRACT_LE_16BITS(rp);
	    rp += sizeof(short);
	    len -= sizeof(short);
	    printf("{ids %d-%d cost %d hops %d} ", id, id + count,
		RI_COST(info), RI_HOPS(info));
	}
	return (1);

trunc:
	return (0);
}

static int
print_l2_routes(const char *rp, u_int len)
{
	int count;
	int area;
	int info;

	/* The last short is a checksum */
	while (len > (3 * sizeof(short))) {
	    TCHECK2(*rp, 3 * sizeof(short));
	    count = EXTRACT_LE_16BITS(rp);
	    if (count > 1024)
		return (1);	/* seems to be bogus from here on */
	    rp += sizeof(short);
	    len -= sizeof(short);
	    area = EXTRACT_LE_16BITS(rp);
	    rp += sizeof(short);
	    len -= sizeof(short);
	    info = EXTRACT_LE_16BITS(rp);
	    rp += sizeof(short);
	    len -= sizeof(short);
	    printf("{areas %d-%d cost %d hops %d} ", area, area + count,
		RI_COST(info), RI_HOPS(info));
	}
	return (1);

trunc:
	return (0);
}

static void
print_i_info(int info)
{
	int ntype = info & II_TYPEMASK;
	switch (ntype) {
	case 0: printf("reserved-ntype? "); break;
	case II_L2ROUT: printf("l2rout "); break;
	case II_L1ROUT: printf("l1rout "); break;
	case II_ENDNODE: printf("endnode "); break;
	}
	if (info & II_VERIF)
	    printf("verif ");
	if (info & II_NOMCAST)
	    printf("nomcast ");
	if (info & II_BLOCK)
	    printf("blo ");
}

static int
print_elist(const char *elp, u_int len)
{
	/* Not enough examples available for me to debug this */
	return (1);
}

static int
print_nsp(const u_char *nspp, u_int nsplen)
{
	const struct nsphdr *nsphp = (struct nsphdr *)nspp;
	int dst, src, flags;

	if (nsplen < sizeof(struct nsphdr))
		goto trunc;
	TCHECK(*nsphp);
	flags = EXTRACT_LE_8BITS(nsphp->nh_flags);
	dst = EXTRACT_LE_16BITS(nsphp->nh_dst);
	src = EXTRACT_LE_16BITS(nsphp->nh_src);

	switch (flags & NSP_TYPEMASK) {
	case MFT_DATA:
	    switch (flags & NSP_SUBMASK) {
	    case MFS_BOM:
	    case MFS_MOM:
	    case MFS_EOM:
	    case MFS_BOM+MFS_EOM:
		printf("data %d>%d ", src, dst);
		{
		    struct seghdr *shp = (struct seghdr *)nspp;
		    int ack;
#ifdef	PRINT_NSPDATA
		    u_char *dp;
#endif
		    u_int data_off = sizeof(struct minseghdr);

		    if (nsplen < data_off)
			goto trunc;
		    TCHECK(shp->sh_seq[0]);
		    ack = EXTRACT_LE_16BITS(shp->sh_seq[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    printf("nak %d ", ack & SGQ_MASK);
			else
			    printf("ack %d ", ack & SGQ_MASK);
		        data_off += sizeof(short);
			if (nsplen < data_off)
			    goto trunc;
			TCHECK(shp->sh_seq[1]);
			ack = EXTRACT_LE_16BITS(shp->sh_seq[1]);
			if (ack & SGQ_OACK) {	/* ackoth field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				printf("onak %d ", ack & SGQ_MASK);
			    else
				printf("oack %d ", ack & SGQ_MASK);
			    data_off += sizeof(short);
			    if (nsplen < data_off)
				goto trunc;
			    TCHECK(shp->sh_seq[2]);
			    ack = EXTRACT_LE_16BITS(shp->sh_seq[2]);
			}
		    }
		    printf("seg %d ", ack & SGQ_MASK);
#ifdef	PRINT_NSPDATA
		    if (nsplen > data_off) {
			dp = &(nspp[data_off]);
			TCHECK2(*dp, nsplen - data_off);
			pdata(dp, nsplen - data_off);
		    }
#endif
		}
		break;
	    case MFS_ILS+MFS_INT:
		printf("intr ");
		{
		    struct seghdr *shp = (struct seghdr *)nspp;
		    int ack;
#ifdef	PRINT_NSPDATA
		    u_char *dp;
#endif
		    u_int data_off = sizeof(struct minseghdr);

		    if (nsplen < data_off)
			goto trunc;
		    TCHECK(shp->sh_seq[0]);
		    ack = EXTRACT_LE_16BITS(shp->sh_seq[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    printf("nak %d ", ack & SGQ_MASK);
			else
			    printf("ack %d ", ack & SGQ_MASK);
		        data_off += sizeof(short);
			if (nsplen < data_off)
			    goto trunc;
			TCHECK(shp->sh_seq[1]);
			ack = EXTRACT_LE_16BITS(shp->sh_seq[1]);
			if (ack & SGQ_OACK) {	/* ackdat field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				printf("nakdat %d ", ack & SGQ_MASK);
			    else
				printf("ackdat %d ", ack & SGQ_MASK);
			    data_off += sizeof(short);
			    if (nsplen < data_off)
				goto trunc;
			    TCHECK(shp->sh_seq[2]);
			    ack = EXTRACT_LE_16BITS(shp->sh_seq[2]);
			}
		    }
		    printf("seg %d ", ack & SGQ_MASK);
#ifdef	PRINT_NSPDATA
		    if (nsplen > data_off) {
			dp = &(nspp[data_off]);
			TCHECK2(*dp, nsplen - data_off);
			pdata(dp, nsplen - data_off);
		    }
#endif
		}
		break;
	    case MFS_ILS:
		printf("link-service %d>%d ", src, dst);
		{
		    struct seghdr *shp = (struct seghdr *)nspp;
		    struct lsmsg *lsmp =
			(struct lsmsg *)&(nspp[sizeof(struct seghdr)]);
		    int ack;
		    int lsflags, fcval;

		    if (nsplen < sizeof(struct seghdr) + sizeof(struct lsmsg))
			goto trunc;
		    TCHECK(shp->sh_seq[0]);
		    ack = EXTRACT_LE_16BITS(shp->sh_seq[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    printf("nak %d ", ack & SGQ_MASK);
			else
			    printf("ack %d ", ack & SGQ_MASK);
			TCHECK(shp->sh_seq[1]);
		        ack = EXTRACT_LE_16BITS(shp->sh_seq[1]);
			if (ack & SGQ_OACK) {	/* ackdat field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				printf("nakdat %d ", ack & SGQ_MASK);
			    else
				printf("ackdat %d ", ack & SGQ_MASK);
			    TCHECK(shp->sh_seq[2]);
			    ack = EXTRACT_LE_16BITS(shp->sh_seq[2]);
			}
		    }
		    printf("seg %d ", ack & SGQ_MASK);
		    TCHECK(*lsmp);
		    lsflags = EXTRACT_LE_8BITS(lsmp->ls_lsflags);
		    fcval = EXTRACT_LE_8BITS(lsmp->ls_fcval);
		    switch (lsflags & LSI_MASK) {
		    case LSI_DATA:
			printf("dat seg count %d ", fcval);
			switch (lsflags & LSM_MASK) {
			case LSM_NOCHANGE:
			    break;
			case LSM_DONOTSEND:
			    printf("donotsend-data ");
			    break;
			case LSM_SEND:
			    printf("send-data ");
			    break;
			default:
			    printf("reserved-fcmod? %x", lsflags);
			    break;
			}
			break;
		    case LSI_INTR:
			printf("intr req count %d ", fcval);
			break;
		    default:
			printf("reserved-fcval-int? %x", lsflags);
			break;
		    }
		}
		break;
	    default:
		printf("reserved-subtype? %x %d > %d", flags, src, dst);
		break;
	    }
	    break;
	case MFT_ACK:
	    switch (flags & NSP_SUBMASK) {
	    case MFS_DACK:
		printf("data-ack %d>%d ", src, dst);
		{
		    struct ackmsg *amp = (struct ackmsg *)nspp;
		    int ack;

		    if (nsplen < sizeof(struct ackmsg))
			goto trunc;
		    TCHECK(*amp);
		    ack = EXTRACT_LE_16BITS(amp->ak_acknum[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    printf("nak %d ", ack & SGQ_MASK);
			else
			    printf("ack %d ", ack & SGQ_MASK);
		        ack = EXTRACT_LE_16BITS(amp->ak_acknum[1]);
			if (ack & SGQ_OACK) {	/* ackoth field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				printf("onak %d ", ack & SGQ_MASK);
			    else
				printf("oack %d ", ack & SGQ_MASK);
			}
		    }
		}
		break;
	    case MFS_IACK:
		printf("ils-ack %d>%d ", src, dst);
		{
		    struct ackmsg *amp = (struct ackmsg *)nspp;
		    int ack;

		    if (nsplen < sizeof(struct ackmsg))
			goto trunc;
		    TCHECK(*amp);
		    ack = EXTRACT_LE_16BITS(amp->ak_acknum[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    printf("nak %d ", ack & SGQ_MASK);
			else
			    printf("ack %d ", ack & SGQ_MASK);
		        TCHECK(amp->ak_acknum[1]);
			ack = EXTRACT_LE_16BITS(amp->ak_acknum[1]);
			if (ack & SGQ_OACK) {	/* ackdat field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				printf("nakdat %d ", ack & SGQ_MASK);
			    else
				printf("ackdat %d ", ack & SGQ_MASK);
			}
		    }
		}
		break;
	    case MFS_CACK:
		printf("conn-ack %d", dst);
		break;
	    default:
		printf("reserved-acktype? %x %d > %d", flags, src, dst);
		break;
	    }
	    break;
	case MFT_CTL:
	    switch (flags & NSP_SUBMASK) {
	    case MFS_CI:
	    case MFS_RCI:
		if ((flags & NSP_SUBMASK) == MFS_CI)
		    printf("conn-initiate ");
		else
		    printf("retrans-conn-initiate ");
		printf("%d>%d ", src, dst);
		{
		    struct cimsg *cimp = (struct cimsg *)nspp;
		    int services, info, segsize;
#ifdef	PRINT_NSPDATA
		    u_char *dp;
#endif

		    if (nsplen < sizeof(struct cimsg))
			goto trunc;
		    TCHECK(*cimp);
		    services = EXTRACT_LE_8BITS(cimp->ci_services);
		    info = EXTRACT_LE_8BITS(cimp->ci_info);
		    segsize = EXTRACT_LE_16BITS(cimp->ci_segsize);

		    switch (services & COS_MASK) {
		    case COS_NONE:
			break;
		    case COS_SEGMENT:
			printf("seg ");
			break;
		    case COS_MESSAGE:
			printf("msg ");
			break;
		    case COS_CRYPTSER:
			printf("crypt ");
			break;
		    }
		    switch (info & COI_MASK) {
		    case COI_32:
			printf("ver 3.2 ");
			break;
		    case COI_31:
			printf("ver 3.1 ");
			break;
		    case COI_40:
			printf("ver 4.0 ");
			break;
		    case COI_41:
			printf("ver 4.1 ");
			break;
		    }
		    printf("segsize %d ", segsize);
#ifdef	PRINT_NSPDATA
		    if (nsplen > sizeof(struct cimsg)) {
			dp = &(nspp[sizeof(struct cimsg)]);
			TCHECK2(*dp, nsplen - sizeof(struct cimsg));
			pdata(dp, nsplen - sizeof(struct cimsg));
		    }
#endif
		}
		break;
	    case MFS_CC:
		printf("conn-confirm %d>%d ", src, dst);
		{
		    struct ccmsg *ccmp = (struct ccmsg *)nspp;
		    int services, info;
		    u_int segsize, optlen;
#ifdef	PRINT_NSPDATA
		    u_char *dp;
#endif

		    if (nsplen < sizeof(struct ccmsg))
			goto trunc;
		    TCHECK(*ccmp);
		    services = EXTRACT_LE_8BITS(ccmp->cc_services);
		    info = EXTRACT_LE_8BITS(ccmp->cc_info);
		    segsize = EXTRACT_LE_16BITS(ccmp->cc_segsize);
		    optlen = EXTRACT_LE_8BITS(ccmp->cc_optlen);

		    switch (services & COS_MASK) {
		    case COS_NONE:
			break;
		    case COS_SEGMENT:
			printf("seg ");
			break;
		    case COS_MESSAGE:
			printf("msg ");
			break;
		    case COS_CRYPTSER:
			printf("crypt ");
			break;
		    }
		    switch (info & COI_MASK) {
		    case COI_32:
			printf("ver 3.2 ");
			break;
		    case COI_31:
			printf("ver 3.1 ");
			break;
		    case COI_40:
			printf("ver 4.0 ");
			break;
		    case COI_41:
			printf("ver 4.1 ");
			break;
		    }
		    printf("segsize %d ", segsize);
		    if (optlen) {
			printf("optlen %d ", optlen);
#ifdef	PRINT_NSPDATA
			if (optlen > nsplen - sizeof(struct ccmsg))
			    goto trunc;
			dp = &(nspp[sizeof(struct ccmsg)]);
			TCHECK2(*dp, optlen);
			pdata(dp, optlen);
#endif
		    }
		}
		break;
	    case MFS_DI:
		printf("disconn-initiate %d>%d ", src, dst);
		{
		    struct dimsg *dimp = (struct dimsg *)nspp;
		    int reason;
		    u_int optlen;
#ifdef	PRINT_NSPDATA
		    u_char *dp;
#endif

		    if (nsplen < sizeof(struct dimsg))
			goto trunc;
		    TCHECK(*dimp);
		    reason = EXTRACT_LE_16BITS(dimp->di_reason);
		    optlen = EXTRACT_LE_8BITS(dimp->di_optlen);

		    print_reason(reason);
		    if (optlen) {
			printf("optlen %d ", optlen);
#ifdef	PRINT_NSPDATA
			if (optlen > nsplen - sizeof(struct dimsg))
			    goto trunc;
			dp = &(nspp[sizeof(struct dimsg)]);
			TCHECK2(*dp, optlen);
			pdata(dp, optlen);
#endif
		    }
		}
		break;
	    case MFS_DC:
		printf("disconn-confirm %d>%d ", src, dst);
		{
		    struct dcmsg *dcmp = (struct dcmsg *)nspp;
		    int reason;

		    TCHECK(*dcmp);
		    reason = EXTRACT_LE_16BITS(dcmp->dc_reason);

		    print_reason(reason);
		}
		break;
	    default:
		printf("reserved-ctltype? %x %d > %d", flags, src, dst);
		break;
	    }
	    break;
	default:
	    printf("reserved-type? %x %d > %d", flags, src, dst);
	    break;
	}
	return (1);

trunc:
	return (0);
}

static struct tok reason2str[] = {
	{ UC_OBJREJECT,		"object rejected connect" },
	{ UC_RESOURCES,		"insufficient resources" },
	{ UC_NOSUCHNODE,	"unrecognized node name" },
	{ DI_SHUT,		"node is shutting down" },
	{ UC_NOSUCHOBJ,		"unrecognized object" },
	{ UC_INVOBJFORMAT,	"invalid object name format" },
	{ UC_OBJTOOBUSY,	"object too busy" },
	{ DI_PROTOCOL,		"protocol error discovered" },
	{ DI_TPA,		"third party abort" },
	{ UC_USERABORT,		"user abort" },
	{ UC_INVNODEFORMAT,	"invalid node name format" },
	{ UC_LOCALSHUT,		"local node shutting down" },
	{ DI_LOCALRESRC,	"insufficient local resources" },
	{ DI_REMUSERRESRC,	"insufficient remote user resources" },
	{ UC_ACCESSREJECT,	"invalid access control information" },
	{ DI_BADACCNT,		"bad ACCOUNT information" },
	{ UC_NORESPONSE,	"no response from object" },
	{ UC_UNREACHABLE,	"node unreachable" },
	{ DC_NOLINK,		"no link terminate" },
	{ DC_COMPLETE,		"disconnect complete" },
	{ DI_BADIMAGE,		"bad image data in connect" },
	{ DI_SERVMISMATCH,	"cryptographic service mismatch" },
	{ 0,			NULL }
};

static void
print_reason(int reason)
{
	printf("%s ", tok2str(reason2str, "reason-%d", reason));
}

char *
dnnum_string(u_short dnaddr)
{
	char *str;
	int area = (u_short)(dnaddr & AREAMASK) >> AREASHIFT;
	int node = dnaddr & NODEMASK;
	int len = sizeof("00.0000");

	str = malloc(len);
	if (str == NULL)
		error("dnnum_string: malloc");
	snprintf(str, len, "%d.%d", area, node);
	return(str);
}

char *
dnname_string(u_short dnaddr)
{
#ifdef	HAVE_LIBDNET
	struct dn_naddr dna;

	dna.a_len = sizeof(short);
	memcpy((char *)dna.a_addr, (char *)&dnaddr, sizeof(short));
	return (savestr(dnet_htoa(&dna)));
#else
	return(dnnum_string(dnaddr));	/* punt */
#endif
}

#ifdef	PRINT_NSPDATA
static void
pdata(u_char *dp, u_int maxlen)
{
	int c;
	u_int x = maxlen;

	while (x-- > 0) {
	    c = (unsigned char)*dp++;
	    if (isprint(c))
		putchar(c);
	    else
		printf("\\%o", c & 0xFF);
	}
}
#endif
