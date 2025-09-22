/*	$OpenBSD: ntp.h,v 1.15 2023/11/15 15:52:09 otto Exp $ */

/*
 * Copyright (c) 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2004 Alexander Guy <alexander.guy@andern.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _NTP_H_
#define _NTP_H_

/* Style borrowed from NTP ref/tcpdump and updated for SNTPv4 (RFC2030). */

/*
 * RFC Section 3
 *
 *    0			  1		      2			  3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |			       Integer Part			     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |			       Fraction Part			     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *    0			  1		      2			  3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |		  Integer Part	     |	   Fraction Part	     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
struct l_fixedpt {
	u_int32_t int_partl;
	u_int32_t fractionl;
};
#define L_DENOMINATOR	(UINT32_MAX + 1ULL)

struct s_fixedpt {
	u_int16_t int_parts;
	u_int16_t fractions;
};
#define S_DENOMINATOR	(UINT16_MAX + 1)

/* RFC Section 4
 *
 *    0			  1		      2			  3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |LI | VN  | Mode|	  Stratum    |	    Poll     |	 Precision   |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |			   Synchronizing Distance		     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |			  Synchronizing Dispersion		     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |			Reference Clock Identifier		     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |								     |
 *   |		       Reference Timestamp (64 bits)		     |
 *   |								     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |								     |
 *   |		       Originate Timestamp (64 bits)		     |
 *   |								     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |								     |
 *   |			Receive Timestamp (64 bits)		     |
 *   |								     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |								     |
 *   |			Transmit Timestamp (64 bits)		     |
 *   |								     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                 Key Identifier (optional) (32)                |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                                                               |
 *   |                                                               |
 *   |                 Message Digest (optional) (128)               |
 *   |                                                               |
 *   |                                                               |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

#define	NTP_DIGESTSIZE		16
#define	NTP_MSGSIZE_NOAUTH	48
#define	NTP_MSGSIZE		(NTP_MSGSIZE_NOAUTH + 4 + NTP_DIGESTSIZE)

struct ntp_msg {
	u_int8_t status;	/* status of local clock and leap info */
	u_int8_t stratum;	/* Stratum level */
	u_int8_t ppoll;		/* poll value */
	int8_t precision;
	struct s_fixedpt rootdelay;
	struct s_fixedpt dispersion;
	u_int32_t refid;
	struct l_fixedpt reftime;
	struct l_fixedpt orgtime;
	struct l_fixedpt rectime;
	struct l_fixedpt xmttime;
} __packed;

struct ntp_query {
	int			fd;
	struct ntp_msg		msg;
	double			xmttime;
};

/*
 *	Leap Second Codes (high order two bits)
 */
#define	LI_NOWARNING	(0 << 6)	/* no warning */
#define	LI_PLUSSEC	(1 << 6)	/* add a second (61 seconds) */
#define	LI_MINUSSEC	(2 << 6)	/* minus a second (59 seconds) */
#define	LI_ALARM	(3 << 6)	/* alarm condition */

/*
 *	Status Masks
 */
#define	MODEMASK	(7 << 0)
#define	VERSIONMASK	(7 << 3)
#define LIMASK		(3 << 6)

/*
 *	Mode values
 */
#define	MODE_RES0	0	/* reserved */
#define	MODE_SYM_ACT	1	/* symmetric active */
#define	MODE_SYM_PAS	2	/* symmetric passive */
#define	MODE_CLIENT	3	/* client */
#define	MODE_SERVER	4	/* server */
#define	MODE_BROADCAST	5	/* broadcast */
#define	MODE_RES1	6	/* reserved for NTP control message */
#define	MODE_RES2	7	/* reserved for private use */

#define	JAN_1970	2208988800UL	/* 1970 - 1900 in seconds */

/*
 * The era we're in if we have no reason to assume otherwise.
 * If lfp_to_d() sees an offset <= INT32_MAX the era is is assumed to be
 * NTP_ERA + 1.
 * Once the actual year is well into era 1, (after 2036) define NTP_ERA to 1
 * and adapt (remove) the test in lfp_to_d().
 * Once more than half of era 1 has elapsed (after 2104), re-inroduce the test
 * to move to era 2 if offset <= INT32_MAX, repeat for each half era.
 */
#define NTP_ERA		0

#define SECS_IN_ERA	(UINT32_MAX + 1ULL)

#define	NTP_VERSION	4
#define	NTP_MAXSTRATUM	15

#endif	/* _NTP_H_ */
