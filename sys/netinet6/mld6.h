/*	$OpenBSD: mld6.h,v 1.2 2010/03/22 21:29:22 jsg Exp $	*/
/*	$FreeBSD: mld6.h,v 1.1 2009/04/29 11:31:23 bms Exp $	*/
/*-
 * Copyright (c) 2009 Bruce Simpson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 */

#ifndef _NETINET6_MLD6_H_
#define _NETINET6_MLD6_H_

/*
 * Multicast Listener Discovery (MLD) definitions.
 */

/* Minimum length of any MLD protocol message. */
#define MLD_MINLEN	sizeof(struct icmp6_hdr)

/*
 * MLD v2 query format.
 * See <netinet/icmp6.h> for struct mld_hdr
 * (MLDv1 query and host report format).
 */
struct mldv2_query {
	struct icmp6_hdr	mld_icmp6_hdr;	/* ICMPv6 header */
	struct in6_addr		mld_addr;	/* address being queried */
	uint8_t		mld_misc;	/* reserved/suppress/robustness   */
	uint8_t		mld_qqi;	/* querier's query interval       */
	uint16_t	mld_numsrc;	/* number of sources              */
	/* followed by 1..numsrc source addresses */
} __packed;
#define MLD_V2_QUERY_MINLEN		sizeof(struct mldv2_query)
#define MLD_MRC_EXP(x)			((ntohs((x)) >> 12) & 0x0007)
#define MLD_MRC_MANT(x)			(ntohs((x)) & 0x0fff)
#define MLD_QQIC_EXP(x)			(((x) >> 4) & 0x07)
#define MLD_QQIC_MANT(x)		((x) & 0x0f)
#define MLD_QRESV(x)			(((x) >> 4) & 0x0f)
#define MLD_SFLAG(x)			(((x) >> 3) & 0x01)
#define MLD_QRV(x)			((x) & 0x07)

/*
 * MLDv2 host membership report header.
 * mld_type: MLDV2_LISTENER_REPORT
 */
struct mldv2_report {
	struct icmp6_hdr	mld_icmp6_hdr;
	/* followed by 1..numgrps records */
} __packed;
/* overlaid on struct icmp6_hdr. */
#define mld_numrecs	mld_icmp6_hdr.icmp6_data16[1]

struct mldv2_record {
	uint8_t			mr_type;	/* record type */
	uint8_t			mr_datalen;	/* length of auxiliary data */
	uint16_t		mr_numsrc;	/* number of sources */
	struct in6_addr		mr_addr;	/* address being reported */
	/* followed by 1..numsrc source addresses */
} __packed;
#define MLD_V2_REPORT_MAXRECS		65535

/*
 * MLDv2 report modes.
 */
#define MLD_DO_NOTHING			0	/* don't send a record */
#define MLD_MODE_IS_INCLUDE		1	/* MODE_IN */
#define MLD_MODE_IS_EXCLUDE		2	/* MODE_EX */
#define MLD_CHANGE_TO_INCLUDE_MODE	3	/* TO_IN */
#define MLD_CHANGE_TO_EXCLUDE_MODE	4	/* TO_EX */
#define MLD_ALLOW_NEW_SOURCES		5	/* ALLOW_NEW */
#define MLD_BLOCK_OLD_SOURCES		6	/* BLOCK_OLD */

/*
 * MLDv2 query types.
 */
#define MLD_V2_GENERAL_QUERY		1
#define MLD_V2_GROUP_QUERY		2
#define MLD_V2_GROUP_SOURCE_QUERY	3

/*
 * Maximum report interval for MLDv1 host membership reports (in seconds)
 */
#define MLD_V1_MAX_RI			10

/*
 * MLD_TIMER_SCALE denotes that the MLD code field specifies
 * time in milliseconds.
 */
#define MLD_TIMER_SCALE			1000

#endif /* _NETINET6_MLD6_H_ */
