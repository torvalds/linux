/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
 * $FreeBSD$
 */
#ifndef _NETSMB_NETBIOS_H_
#define	_NETSMB_NETBIOS_H_

/*
 * make this file dirty...
 */
#ifndef _NETINET_IN_H_
#include <netinet/in.h>
#endif

#define	NMB_TCP_PORT	137

#define	NBPROTO_TCPSSN	1		/* NETBIOS session over TCP */
#define	NBPROTO_IPXSSN	11		/* NETBIOS over IPX */

#define NB_NAMELEN	16
#define	NB_ENCNAMELEN	NB_NAMELEN * 2
#define	NB_MAXLABLEN	63

#define	NB_MINSALEN	(sizeof(struct sockaddr_nb))

/*
 * name types
 */
#define	NBT_WKSTA		0x00
#define	NBT_MESSENGER		0x03
#define	NBT_RAS_SERVER		0x06
#define	NBT_DOMAIN_MASTER_BROWSER	0x1B
#define	NBT_DOMAIN_CONTROLLER	0x1C
#define	NBT_MASTER_BROWSER	0x1D
#define	NBT_NETDDE		0x1F
#define	NBT_SERVER		0x20
#define	NBT_RAS_CLIENT		0x21

/*
 * Session packet types
 */
#define	NB_SSN_MESSAGE		0x0
#define	NB_SSN_REQUEST		0x81
#define	NB_SSN_POSRESP		0x82
#define	NB_SSN_NEGRESP		0x83
#define	NB_SSN_RTGRESP		0x84
#define	NB_SSN_KEEPALIVE	0x85

/*
 * resolver: Opcodes
 */
#define	NBNS_OPCODE_QUERY	0x00
#define	NBNS_OPCODE_REGISTER	0x05
#define	NBNS_OPCODE_RELEASE	0x06
#define	NBNS_OPCODE_WACK	0x07
#define	NBNS_OPCODE_REFRESH	0x08
#define	NBNS_OPCODE_RESPONSE	0x10	/* or'ed with other opcodes */

/*
 * resolver: NM_FLAGS
 */
#define	NBNS_NMFLAG_BCAST	0x01
#define	NBNS_NMFLAG_RA		0x08	/* recursion available */
#define	NBNS_NMFLAG_RD		0x10	/* recursion desired */
#define	NBNS_NMFLAG_TC		0x20	/* truncation occurred */
#define	NBNS_NMFLAG_AA		0x40	/* authoritative answer */

/* 
 * resolver: Question types
 */
#define	NBNS_QUESTION_TYPE_NB		0x0020
#define NBNS_QUESTION_TYPE_NBSTAT	0x0021

/* 
 * resolver: Question class 
 */
#define NBNS_QUESTION_CLASS_IN	0x0001

/*
 * resolver: Limits
 */
#define	NBNS_MAXREDIRECTS	3	/* maximum number of accepted redirects */
#define	NBDG_MAXSIZE		576	/* maximum nbns datagram size */

/*
 * NETBIOS addressing
 */
union nb_tran {
	struct sockaddr_in	x_in;
	/* struct sockaddr_ipx was here. */
};

struct nb_name {
	u_int		nn_type;
	u_char		nn_name[NB_NAMELEN + 1];
	u_char *	nn_scope;
};

/*
 * Socket address
 */
struct sockaddr_nb {
	u_char		snb_len;
	u_char		snb_family;
	union nb_tran	snb_tran;		/* transport */
	u_char		snb_name[1 + NB_ENCNAMELEN + 1];	/* encoded */
};

#define	snb_addrin	snb_tran.x_in

#endif /* !_NETSMB_NETBIOS_H_ */
