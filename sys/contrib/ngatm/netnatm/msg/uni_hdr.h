/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/msg/uni_hdr.h,v 1.6 2004/07/08 08:22:05 brandt Exp $
 */
#ifndef _NETNATM_MSG_UNI_HDR_H_
#define _NETNATM_MSG_UNI_HDR_H_

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/stdint.h>
#else
#include <stdint.h>
#endif

#include <netnatm/msg/uni_config.h>

enum {
	UNI_PROTO	= 0x09,		/* protocol discriminator */
	PNNI_PROTO	= 0xf0,		/* PNNI protocol discriminator */
};

/*
 * Message types
 */
enum uni_msgtype {
	UNI_UNKNOWN		= 0x100,/* unknown message */

	UNI_ALERTING		= 0x01,	/* alerting */
	UNI_CALL_PROC		= 0x02, /* call proceeding */
	UNI_SETUP		= 0x05,	/* setup */
	UNI_CONNECT		= 0x07,	/* connect */
	UNI_CONNECT_ACK		= 0x0f,	/* connect ack */

	UNI_RESTART		= 0x46,	/* restart */
	UNI_RELEASE		= 0x4d,	/* release */
	UNI_RESTART_ACK		= 0x4e,	/* restart acknowledgement */
	UNI_RELEASE_COMPL	= 0x5a,	/* release complete */

	UNI_NOTIFY		= 0x6e,	/* notify user */
	UNI_STATUS_ENQ		= 0x75,	/* status enquiry */
	UNI_STATUS		= 0x7d,	/* status */

	UNI_ADD_PARTY		= 0x80,	/* add party */
	UNI_ADD_PARTY_ACK	= 0x81,	/* add party acknowledgement */
	UNI_ADD_PARTY_REJ	= 0x82,	/* add party reject */
	UNI_DROP_PARTY		= 0x83,	/* drop party */
	UNI_DROP_PARTY_ACK	= 0x84,	/* drop party acknowledgement */
	UNI_PARTY_ALERTING	= 0x85,	/* party alerting */

	UNI_LEAF_SETUP_FAIL	= 0x90,	/* leaf setup failed */
	UNI_LEAF_SETUP_REQ	= 0x91,	/* leaf setup request */

	UNI_COBISETUP		= 0x15,	/* Q.2932 COBI-setup */
	UNI_FACILITY		= 0x62,	/* Q.2932 facility */

	UNI_MODIFY_REQ		= 0x88,	/* Q.2963 Modify request */
	UNI_MODIFY_ACK		= 0x89,	/* Q.2963 Modify acknowledgement */
	UNI_MODIFY_REJ		= 0x8a,	/* Q.2963 Modify reject */
	UNI_CONN_AVAIL		= 0x8b,	/* Q.2963 Connection available */
};

/*
 * Information element types
 */
enum uni_ietype {
	UNI_IE_CAUSE		= 0x08,	/* cause */
	UNI_IE_CALLSTATE	= 0x14,	/* call state */
	UNI_IE_FACILITY		= 0x1C,	/* Q.2932 facility IE */
	UNI_IE_NOTIFY		= 0x27,	/* UNI4.0 notify */
	UNI_IE_EETD		= 0x42,	/* UNI4.0 end-to-end transit delay */
	UNI_IE_CONNED		= 0x4c,	/* UNI4.0/Q.2951 connected address */
	UNI_IE_CONNEDSUB	= 0x4d,	/* UNI4.0/Q.2951 connected subaddress */
	UNI_IE_EPREF		= 0x54,	/* endpoint reference */
	UNI_IE_EPSTATE		= 0x55,	/* enpoint state */
	UNI_IE_AAL		= 0x58,	/* ATM adaptation layer parameters */
	UNI_IE_TRAFFIC		= 0x59,	/* ATM traffic descriptor */
	UNI_IE_CONNID		= 0x5a,	/* connection identifier */
	UNI_IE_QOS		= 0x5c,	/* quality of service parameter */
	UNI_IE_BHLI		= 0x5d,	/* broadband higher layer information */
	UNI_IE_BEARER		= 0x5e,	/* broadband bearer capability */
	UNI_IE_BLLI		= 0x5f,	/* broadband lower layer information */
	UNI_IE_LSHIFT		= 0x60,	/* broadband locking shift */
	UNI_IE_NLSHIFT		= 0x61,	/* broadband non-locking shift */
	UNI_IE_SCOMPL		= 0x62,	/* broadband sending complete */
	UNI_IE_REPEAT		= 0x63,	/* broadband repeat indicator */
	UNI_IE_CALLING		= 0x6c,	/* calling party number */
	UNI_IE_CALLINGSUB	= 0x6d,	/* calling party subaddress */
	UNI_IE_CALLED		= 0x70,	/* called party number */
	UNI_IE_CALLEDSUB	= 0x71,	/* called party subaddress */
	UNI_IE_TNS		= 0x78,	/* transit network selection */
	UNI_IE_RESTART		= 0x79,	/* restart indicator */
	UNI_IE_UU		= 0x7e,	/* UNI4.0/Q.2957 user-to-user info */
	UNI_IE_GIT		= 0x7f,	/* UNI4.0 generic identifier transport*/
	UNI_IE_MINTRAFFIC	= 0x81,	/* Q.2962 minimum traffic desc */
	UNI_IE_ATRAFFIC		= 0x82,	/* Q.2962 alternate traffic desc */
	UNI_IE_ABRSETUP		= 0x84,	/* UNI4.0 ABR setup parameters */
	UNI_IE_REPORT		= 0x89,	/* Q.2963 broadband report type */
	UNI_IE_CALLED_SOFT	= 0xe0,	/* PNNI Calling party soft PVPC */
	UNI_IE_CRANKBACK	= 0xe1,	/* PNNI Crankback */
	UNI_IE_DTL		= 0xe2,	/* PNNI designated transit list */
	UNI_IE_CALLING_SOFT	= 0xe3,	/* PNNI Called party soft PVPC */
	UNI_IE_ABRADD		= 0xe4,	/* UNI4.0 ABR additional parameters */
	UNI_IE_LIJ_CALLID	= 0xe8,	/* UNI4.0 LIF call identifier */
	UNI_IE_LIJ_PARAM	= 0xe9,	/* UNI4.0 LIF parameters */
	UNI_IE_LIJ_SEQNO	= 0xea,	/* UNI4.0 LIF sequence number */
	UNI_IE_CSCOPE		= 0xeb,	/* UNI4.0 connection scope selection */
	UNI_IE_EXQOS		= 0xec,	/* UNI4.0 extended QoS parameters */
	UNI_IE_MDCR		= 0xf0,	/* UNI4.0+ Minimum desired call rate */
	UNI_IE_UNREC		= 0xfe,
};

enum uni_coding {
	UNI_CODING_ITU = 0x0,
	UNI_CODING_NET = 0x3,
};

enum uni_msgact {
	UNI_MSGACT_CLEAR	= 0x0,
	UNI_MSGACT_IGNORE	= 0x1,
	UNI_MSGACT_REPORT	= 0x2,

	UNI_MSGACT_DEFAULT	= 0x4
};

enum uni_ieact {
	UNI_IEACT_CLEAR		= 0x00,	/* clear call */
	UNI_IEACT_IGNORE	= 0x01,	/* ignore IE and proceed */
	UNI_IEACT_REPORT	= 0x02,	/* ignore IE, report and proceed */
	UNI_IEACT_MSG_IGNORE	= 0x05,	/* ignore message */
	UNI_IEACT_MSG_REPORT	= 0x06,	/* ignore message and report */

	UNI_IEACT_DEFAULT	= 0x08
};

struct uni_cref {
	u_int	flag;
	u_int	cref;
};

/*
 * Message header.
 */
struct uni_msghdr {
	struct uni_cref	cref;
	enum uni_msgact	act;		/* action indicator */
	u_int		pass:1;		/* PNNI pass along request */
};

enum {
	CREF_GLOBAL	= 0,
	CREF_DUMMY	= 0x7fffff,
};

/*
 * General information element header.
 */
struct uni_iehdr {
	enum uni_coding	coding;		/* coding standard */
	enum uni_ieact	act;		/* action indicator */
	u_int		pass : 1;	/* PNNI pass along request */
	u_int		present;	/* which optional elements are present */
#define UNI_IE_EMPTY	0x80000000
#define UNI_IE_PRESENT	0x40000000
#define UNI_IE_ERROR	0x20000000
#define UNI_IE_XXX	0x10000000
#define UNI_IE_MASK	0xf0000000
};

#define IE_ISPRESENT(IE) \
	(((IE).h.present & (UNI_IE_PRESENT|UNI_IE_EMPTY)) == UNI_IE_PRESENT)
#define IE_SETPRESENT(IE) \
	((IE).h.present = ((IE).h.present & ~UNI_IE_MASK) | \
		UNI_IE_PRESENT)

#define IE_ADDPRESENT(IE) \
	((IE).h.present = ((IE).h.present & ~UNI_IE_EMPTY) | \
		UNI_IE_PRESENT)

#define IE_ISEMPTY(IE) \
	(((IE).h.present & UNI_IE_MASK) == (UNI_IE_PRESENT | UNI_IE_EMPTY))
#define IE_SETEMPTY(IE) \
	((IE).h.present = ((IE).h.present & ~UNI_IE_MASK) | \
		UNI_IE_EMPTY | UNI_IE_PRESENT)

#define IE_ISERROR(IE) \
	(((IE).h.present & UNI_IE_MASK) == (UNI_IE_PRESENT | UNI_IE_ERROR))
#define IE_SETERROR(IE) \
	((IE).h.present = ((IE).h.present & ~UNI_IE_MASK) | \
		UNI_IE_ERROR | UNI_IE_PRESENT)

#define IE_ISGOOD(IE) \
	(((IE).h.present & UNI_IE_MASK) == (UNI_IE_PRESENT))

#endif
