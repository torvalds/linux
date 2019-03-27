/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1992 The University of Utah and the Center
 *	for Software Science (CSS).
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Center for Software Science of the University of Utah Computer
 * Science Department.  CSS requests users of this software to return
 * to css-dist@cs.utah.edu any improvements that they make and grant
 * CSS redistribution rights.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)rmp_var.h	8.1 (Berkeley) 6/4/93
 *
 * from: Utah Hdr: rmp_var.h 3.1 92/07/06
 * Author: Jeff Forys, University of Utah CSS
 *
 * $FreeBSD$
 */

/*
 *  Possible values for "rmp_type" fields.
 */

#define	RMP_BOOT_REQ	1	/* boot request packet */
#define	RMP_BOOT_REPL	129	/* boot reply packet */
#define	RMP_READ_REQ	2	/* read request packet */
#define	RMP_READ_REPL	130	/* read reply packet */
#define	RMP_BOOT_DONE	3	/* boot complete packet */

/*
 *  Useful constants.
 */

#define RMP_VERSION	2	/* protocol version */
#define RMP_TIMEOUT	600	/* timeout connection after ten minutes */
#define	RMP_PROBESID	0xffff	/* session ID for probes */
#define	RMP_HOSTLEN	13	/* max length of server's name */
#define	RMP_MACHLEN	20	/* length of machine type field */

/*
 *  RMP error codes
 */

#define	RMP_E_OKAY	0
#define	RMP_E_EOF	2	/* read reply: returned end of file */
#define	RMP_E_ABORT	3	/* abort operation */
#define	RMP_E_BUSY	4	/* boot reply: server busy */
#define	RMP_E_TIMEOUT	5	/* lengthen time out (not implemented) */
#define	RMP_E_NOFILE	16	/* boot reply: file does not exist */
#define RMP_E_OPENFILE	17	/* boot reply: file open failed */
#define	RMP_E_NODFLT	18	/* boot reply: default file does not exist */
#define RMP_E_OPENDFLT	19	/* boot reply: default file open failed */
#define	RMP_E_BADSID	25	/* read reply: bad session ID */
#define RMP_E_BADPACKET	27 	/* Bad packet detected */

/*
 *  RMPDATALEN is the maximum number of data octets that can be stuffed
 *  into an RMP packet.  This excludes the 802.2 LLC w/HP extensions.
 */
#define RMPDATALEN	(RMP_MAX_PACKET - (sizeof(struct hp_hdr) + \
			                   sizeof(struct hp_llc)))

/*
 *  Define sizes of packets we send.  Boot and Read replies are variable
 *  in length depending on the length of `s'.
 *
 *  Also, define how much space `restofpkt' can take up for outgoing
 *  Boot and Read replies.  Boot Request packets are effectively
 *  limited to 255 bytes due to the preceding 1-byte length field.
 */

#define	RMPBOOTSIZE(s)	(sizeof(struct hp_hdr) + sizeof(struct hp_llc) + \
			 sizeof(struct rmp_boot_repl) + s - sizeof(restofpkt))
#define	RMPREADSIZE(s)	(sizeof(struct hp_hdr) + sizeof(struct hp_llc) + \
			 sizeof(struct rmp_read_repl) + s - sizeof(restofpkt) \
			 - sizeof(u_int8_t))
#define	RMPDONESIZE	(sizeof(struct hp_hdr) + sizeof(struct hp_llc) + \
			 sizeof(struct rmp_boot_done))
#define	RMPBOOTDATA	255
#define	RMPREADDATA	(RMPDATALEN - \
			 (2*sizeof(u_int8_t)+sizeof(u_int16_t)+sizeof(u_word)))

/*
 * This protocol defines some field sizes as "rest of ethernet packet".
 * There is no easy way to specify this in C, so we use a one character
 * field to denote it, and index past it to the end of the packet.
 */

typedef char	restofpkt;

/*
 * Due to the RMP packet layout, we'll run into alignment problems
 * on machines that can't access (or don't, by default, align) words
 * on half-word boundaries.  If you know that your machine does not suffer
 * from this problem, add it to the vax/tahoe/m68k #define below.
 *
 * The following macros are used to deal with this problem:
 *	WORDZE(w)	Return True if u_word `w' is zero, False otherwise.
 *	ZEROWORD(w)	Set u_word `w' to zero.
 *	COPYWORD(w1,w2)	Copy u_word `w1' to `w2'.
 *	GETWORD(w,i)	Copy u_word `w' into int `i'.
 *	PUTWORD(i,w)	Copy int `i' into u_word `w'.
 *
 * N.B. Endianness is handled by use of ntohl/htonl
 */
#if defined(__vax__) || defined(__tahoe__) || defined(__m68k__)

typedef	u_int32_t	u_word;

#define	WORDZE(w)	((w) == 0)
#define	ZEROWORD(w)	(w) = 0
#define	COPYWORD(w1,w2)	(w2) = (w1)
#define	GETWORD(w, i)	(i) = ntohl(w)
#define	PUTWORD(i, w)	(w) = htonl(i)

#else

#define	_WORD_HIGHPART	0
#define	_WORD_LOWPART	1

typedef	struct _uword { u_int16_t val[2]; }	u_word;

#define	WORDZE(w) \
	((w.val[_WORD_HIGHPART] == 0) && (w.val[_WORD_LOWPART] == 0))
#define	ZEROWORD(w) \
	(w).val[_WORD_HIGHPART] = (w).val[_WORD_LOWPART] = 0
#define	COPYWORD(w1, w2) \
	{ (w2).val[_WORD_HIGHPART] = (w1).val[_WORD_HIGHPART]; \
	  (w2).val[_WORD_LOWPART] = (w1).val[_WORD_LOWPART]; \
	}
#define	GETWORD(w, i) \
	(i) = (((u_int32_t)ntohs((w).val[_WORD_HIGHPART])) << 16) | ntohs((w).val[_WORD_LOWPART])
#define	PUTWORD(i, w) \
	{ (w).val[_WORD_HIGHPART] = htons((u_int16_t) ((i >> 16) & 0xffff)); \
	  (w).val[_WORD_LOWPART] = htons((u_int16_t) (i & 0xffff)); \
	}

#endif

/*
 * Packet structures.
 */

struct rmp_raw {		/* generic RMP packet */
	u_int8_t  rmp_type;		/* packet type */
	u_int8_t  rmp_rawdata[RMPDATALEN-1];
};

struct rmp_boot_req {		/* boot request */
	u_int8_t  rmp_type;		/* packet type (RMP_BOOT_REQ) */
	u_int8_t  rmp_retcode;		/* return code (0) */
	u_word	  rmp_seqno;		/* sequence number (real time clock) */
	u_int16_t rmp_session;		/* session id (normally 0) */
	u_int16_t rmp_version;		/* protocol version (RMP_VERSION) */
	char	  rmp_machtype[RMP_MACHLEN];	/* machine type */
	u_int8_t  rmp_flnmsize;		/* length of rmp_flnm */
	restofpkt rmp_flnm;		/* name of file to be read */
};

struct rmp_boot_repl {		/* boot reply */
	u_int8_t  rmp_type;		/* packet type (RMP_BOOT_REPL) */
	u_int8_t  rmp_retcode;		/* return code (normally 0) */
	u_word	  rmp_seqno;		/* sequence number (from boot req) */
	u_int16_t rmp_session;		/* session id (generated) */
	u_int16_t rmp_version;		/* protocol version (RMP_VERSION) */
	u_int8_t  rmp_flnmsize;		/* length of rmp_flnm */
	restofpkt rmp_flnm;		/* name of file (from boot req) */
};

struct rmp_read_req {		/* read request */
	u_int8_t  rmp_type;		/* packet type (RMP_READ_REQ) */
	u_int8_t  rmp_retcode;		/* return code (0) */
	u_word	  rmp_offset;		/* file relative byte offset */
	u_int16_t rmp_session;		/* session id (from boot repl) */
	u_int16_t rmp_size;		/* max no of bytes to send */
};

struct rmp_read_repl {		/* read reply */
	u_int8_t  rmp_type;		/* packet type (RMP_READ_REPL) */
	u_int8_t  rmp_retcode;		/* return code (normally 0) */
	u_word	  rmp_offset;		/* byte offset (from read req) */
	u_int16_t rmp_session;		/* session id (from read req) */
	restofpkt rmp_data;		/* data (max size from read req) */
	u_int8_t  rmp_unused;		/* padding to 16-bit boundary */
};

struct rmp_boot_done {		/* boot complete */
	u_int8_t  rmp_type;		/* packet type (RMP_BOOT_DONE) */
	u_int8_t  rmp_retcode;		/* return code (0) */
	u_word	  rmp_unused;		/* not used (0) */
	u_int16_t rmp_session;		/* session id (from read repl) */
};

struct rmp_packet {
	struct hp_hdr hp_hdr;
	struct hp_llc hp_llc;
	union {
		struct rmp_boot_req	rmp_brq;	/* boot request */
		struct rmp_boot_repl	rmp_brpl;	/* boot reply */
		struct rmp_read_req	rmp_rrq;	/* read request */
		struct rmp_read_repl	rmp_rrpl;	/* read reply */
		struct rmp_boot_done	rmp_done;	/* boot complete */
		struct rmp_raw		rmp_raw;	/* raw data */
	} rmp_proto;
};

/*
 *  Make life easier...
 */

#define	r_type	rmp_proto.rmp_raw.rmp_type
#define	r_data	rmp_proto.rmp_raw.rmp_rawdata
#define	r_brq	rmp_proto.rmp_brq
#define	r_brpl	rmp_proto.rmp_brpl
#define	r_rrq	rmp_proto.rmp_rrq
#define	r_rrpl	rmp_proto.rmp_rrpl
#define	r_done	rmp_proto.rmp_done
