#ifndef _SLHC_H
#define _SLHC_H
/*
 * Definitions for tcp compression routines.
 *
 * $Header: slcompress.h,v 1.10 89/12/31 08:53:02 van Exp $
 *
 * Copyright (c) 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Van Jacobson (van@helios.ee.lbl.gov), Dec 31, 1989:
 *	- Initial distribution.
 *
 *
 * modified for KA9Q Internet Software Package by
 * Katie Stevens (dkstevens@ucdavis.edu)
 * University of California, Davis
 * Computing Services
 *	- 01-31-90	initial adaptation
 *
 *	- Feb 1991	Bill_Simpson@um.cc.umich.edu
 *			variable number of conversation slots
 *			allow zero or one slots
 *			separate routines
 *			status display
 */

/*
 * Compressed packet format:
 *
 * The first octet contains the packet type (top 3 bits), TCP
 * 'push' bit, and flags that indicate which of the 4 TCP sequence
 * numbers have changed (bottom 5 bits).  The next octet is a
 * conversation number that associates a saved IP/TCP header with
 * the compressed packet.  The next two octets are the TCP checksum
 * from the original datagram.  The next 0 to 15 octets are
 * sequence number changes, one change per bit set in the header
 * (there may be no changes and there are two special cases where
 * the receiver implicitly knows what changed -- see below).
 *
 * There are 5 numbers which can change (they are always inserted
 * in the following order): TCP urgent pointer, window,
 * acknowledgment, sequence number and IP ID.  (The urgent pointer
 * is different from the others in that its value is sent, not the
 * change in value.)  Since typical use of SLIP links is biased
 * toward small packets (see comments on MTU/MSS below), changes
 * use a variable length coding with one octet for numbers in the
 * range 1 - 255 and 3 octets (0, MSB, LSB) for numbers in the
 * range 256 - 65535 or 0.  (If the change in sequence number or
 * ack is more than 65535, an uncompressed packet is sent.)
 */

/*
 * Packet types (must not conflict with IP protocol version)
 *
 * The top nibble of the first octet is the packet type.  There are
 * three possible types: IP (not proto TCP or tcp with one of the
 * control flags set); uncompressed TCP (a normal IP/TCP packet but
 * with the 8-bit protocol field replaced by an 8-bit connection id --
 * this type of packet syncs the sender & receiver); and compressed
 * TCP (described above).
 *
 * LSB of 4-bit field is TCP "PUSH" bit (a worthless anachronism) and
 * is logically part of the 4-bit "changes" field that follows.  Top
 * three bits are actual packet type.  For backward compatibility
 * and in the interest of conserving bits, numbers are chosen so the
 * IP protocol version number (4) which normally appears in this nibble
 * means "IP packet".
 */


#include <linux/ip.h>
#include <linux/tcp.h>

/* SLIP compression masks for len/vers byte */
#define SL_TYPE_IP 0x40
#define SL_TYPE_UNCOMPRESSED_TCP 0x70
#define SL_TYPE_COMPRESSED_TCP 0x80
#define SL_TYPE_ERROR 0x00

/* Bits in first octet of compressed packet */
#define NEW_C	0x40	/* flag bits for what changed in a packet */
#define NEW_I	0x20
#define NEW_S	0x08
#define NEW_A	0x04
#define NEW_W	0x02
#define NEW_U	0x01

/* reserved, special-case values of above */
#define SPECIAL_I (NEW_S|NEW_W|NEW_U)		/* echoed interactive traffic */
#define SPECIAL_D (NEW_S|NEW_A|NEW_W|NEW_U)	/* unidirectional data */
#define SPECIALS_MASK (NEW_S|NEW_A|NEW_W|NEW_U)

#define TCP_PUSH_BIT 0x10

/*
 * data type and sizes conversion assumptions:
 *
 *	VJ code		KA9Q style	generic
 *	u_char		byte_t		unsigned char	 8 bits
 *	u_short		int16		unsigned short	16 bits
 *	u_int		int16		unsigned short	16 bits
 *	u_long		unsigned long	unsigned long	32 bits
 *	int		int32		long		32 bits
 */

typedef __u8 byte_t;
typedef __u32 int32;

/*
 * "state" data for each active tcp conversation on the wire.  This is
 * basically a copy of the entire IP/TCP header from the last packet
 * we saw from the conversation together with a small identifier
 * the transmit & receive ends of the line use to locate saved header.
 */
struct cstate {
	byte_t	cs_this;	/* connection id number (xmit) */
	struct cstate *next;	/* next in ring (xmit) */
	struct iphdr cs_ip;	/* ip/tcp hdr from most recent packet */
	struct tcphdr cs_tcp;
	unsigned char cs_ipopt[64];
	unsigned char cs_tcpopt[64];
	int cs_hsize;
};
#define NULLSLSTATE	(struct cstate *)0

/*
 * all the state data for one serial line (we need one of these per line).
 */
struct slcompress {
	struct cstate *tstate;	/* transmit connection states (array)*/
	struct cstate *rstate;	/* receive connection states (array)*/

	byte_t tslot_limit;	/* highest transmit slot id (0-l)*/
	byte_t rslot_limit;	/* highest receive slot id (0-l)*/

	byte_t xmit_oldest;	/* oldest xmit in ring */
	byte_t xmit_current;	/* most recent xmit id */
	byte_t recv_current;	/* most recent rcvd id */

	byte_t flags;
#define SLF_TOSS	0x01	/* tossing rcvd frames until id received */

	int32 sls_o_nontcp;	/* outbound non-TCP packets */
	int32 sls_o_tcp;	/* outbound TCP packets */
	int32 sls_o_uncompressed;	/* outbound uncompressed packets */
	int32 sls_o_compressed;	/* outbound compressed packets */
	int32 sls_o_searches;	/* searches for connection state */
	int32 sls_o_misses;	/* times couldn't find conn. state */

	int32 sls_i_uncompressed;	/* inbound uncompressed packets */
	int32 sls_i_compressed;	/* inbound compressed packets */
	int32 sls_i_error;	/* inbound error packets */
	int32 sls_i_tossed;	/* inbound packets tossed because of error */

	int32 sls_i_runt;
	int32 sls_i_badcheck;
};
#define NULLSLCOMPR	(struct slcompress *)0

/* In slhc.c: */
struct slcompress *slhc_init(int rslots, int tslots);
void slhc_free(struct slcompress *comp);

int slhc_compress(struct slcompress *comp, unsigned char *icp, int isize,
		  unsigned char *ocp, unsigned char **cpp, int compress_cid);
int slhc_uncompress(struct slcompress *comp, unsigned char *icp, int isize);
int slhc_remember(struct slcompress *comp, unsigned char *icp, int isize);
int slhc_toss(struct slcompress *comp);

#endif	/* _SLHC_H */
