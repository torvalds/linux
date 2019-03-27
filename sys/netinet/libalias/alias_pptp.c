/*
 * alias_pptp.c
 *
 * Copyright (c) 2000 Whistle Communications, Inc.
 * All rights reserved.
 *
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 *
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Erik Salander <erik@whistle.com>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Includes */
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/limits.h>
#include <sys/kernel.h>
#include <sys/module.h>
#else
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <stdio.h>
#endif

#include <netinet/tcp.h>

#ifdef _KERNEL
#include <netinet/libalias/alias.h>
#include <netinet/libalias/alias_local.h>
#include <netinet/libalias/alias_mod.h>
#else
#include "alias.h"
#include "alias_local.h"
#include "alias_mod.h"
#endif

#define PPTP_CONTROL_PORT_NUMBER 1723

static void
AliasHandlePptpOut(struct libalias *, struct ip *, struct alias_link *);

static void
AliasHandlePptpIn(struct libalias *, struct ip *, struct alias_link *);

static int
AliasHandlePptpGreOut(struct libalias *, struct ip *);

static int
AliasHandlePptpGreIn(struct libalias *, struct ip *);

static int
fingerprint(struct libalias *la, struct alias_data *ah)
{

	if (ah->dport == NULL || ah->sport == NULL || ah->lnk == NULL)
		return (-1);
	if (ntohs(*ah->dport) == PPTP_CONTROL_PORT_NUMBER
	    || ntohs(*ah->sport) == PPTP_CONTROL_PORT_NUMBER)
		return (0);
	return (-1);
}

static int
fingerprintgre(struct libalias *la, struct alias_data *ah)
{

	return (0);
}

static int
protohandlerin(struct libalias *la, struct ip *pip, struct alias_data *ah)
{
	
	AliasHandlePptpIn(la, pip, ah->lnk);
	return (0);
}

static int
protohandlerout(struct libalias *la, struct ip *pip, struct alias_data *ah)
{
	
	AliasHandlePptpOut(la, pip, ah->lnk);
	return (0);
}

static int
protohandlergrein(struct libalias *la, struct ip *pip, struct alias_data *ah)
{

	if (la->packetAliasMode & PKT_ALIAS_PROXY_ONLY ||
	    AliasHandlePptpGreIn(la, pip) == 0)
		return (0);
	return (-1);
}

static int
protohandlergreout(struct libalias *la, struct ip *pip, struct alias_data *ah)
{

	if (AliasHandlePptpGreOut(la, pip) == 0)
		return (0);
	return (-1);
}

/* Kernel module definition. */
struct proto_handler handlers[] = {
	{
	  .pri = 200,
	  .dir = IN,
	  .proto = TCP,
	  .fingerprint = &fingerprint,
	  .protohandler = &protohandlerin
	},
	{
	  .pri = 210,
	  .dir = OUT,
	  .proto = TCP,
	  .fingerprint = &fingerprint,
	  .protohandler = &protohandlerout
	},
/*
 * WATCH OUT!!! these 2 handlers NEED a priority of INT_MAX (highest possible)
 * cause they will ALWAYS process packets, so they must be the last one
 * in chain: look fingerprintgre() above.
 */
	{
	  .pri = INT_MAX,
	  .dir = IN,
	  .proto = IP,
	  .fingerprint = &fingerprintgre,
	  .protohandler = &protohandlergrein
	},
	{
	  .pri = INT_MAX,
	  .dir = OUT,
	  .proto = IP,
	  .fingerprint = &fingerprintgre,
	  .protohandler = &protohandlergreout
	},
	{ EOH }
};
static int
mod_handler(module_t mod, int type, void *data)
{
	int error;

	switch (type) {
	case MOD_LOAD:
		error = 0;
		LibAliasAttachHandlers(handlers);
		break;
	case MOD_UNLOAD:
		error = 0;
		LibAliasDetachHandlers(handlers);
		break;
	default:
		error = EINVAL;
	}
	return (error);
}

#ifdef _KERNEL
static
#endif
moduledata_t alias_mod = {
       "alias_pptp", mod_handler, NULL
};

#ifdef	_KERNEL
DECLARE_MODULE(alias_pptp, alias_mod, SI_SUB_DRIVERS, SI_ORDER_SECOND);
MODULE_VERSION(alias_pptp, 1);
MODULE_DEPEND(alias_pptp, libalias, 1, 1, 1);
#endif

/*
   Alias_pptp.c performs special processing for PPTP sessions under TCP.
   Specifically, watch PPTP control messages and alias the Call ID or the
   Peer's Call ID in the appropriate messages.  Note, PPTP requires
   "de-aliasing" of incoming packets, this is different than any other
   TCP applications that are currently (ie. FTP, IRC and RTSP) aliased.

   For Call IDs encountered for the first time, a PPTP alias link is created.
   The PPTP alias link uses the Call ID in place of the original port number.
   An alias Call ID is created.

   For this routine to work, the PPTP control messages must fit entirely
   into a single TCP packet.  This is typically the case, but is not
   required by the spec.

   Unlike some of the other TCP applications that are aliased (ie. FTP,
   IRC and RTSP), the PPTP control messages that need to be aliased are
   guaranteed to remain the same length.  The aliased Call ID is a fixed
   length field.

   Reference: RFC 2637

   Initial version:  May, 2000 (eds)

*/

/*
 * PPTP definitions
 */

struct grehdr {			/* Enhanced GRE header. */
	u_int16_t	gh_flags;	/* Flags. */
	u_int16_t	gh_protocol;	/* Protocol type. */
	u_int16_t	gh_length;	/* Payload length. */
	u_int16_t	gh_call_id;	/* Call ID. */
	u_int32_t	gh_seq_no;	/* Sequence number (optional). */
	u_int32_t	gh_ack_no;	/* Acknowledgment number
					 * (optional). */
};
typedef struct grehdr GreHdr;

/* The PPTP protocol ID used in the GRE 'proto' field. */
#define PPTP_GRE_PROTO          0x880b

/* Bits that must be set a certain way in all PPTP/GRE packets. */
#define PPTP_INIT_VALUE		((0x2001 << 16) | PPTP_GRE_PROTO)
#define PPTP_INIT_MASK		0xef7fffff

#define PPTP_MAGIC		0x1a2b3c4d
#define PPTP_CTRL_MSG_TYPE	1

enum {
	PPTP_StartCtrlConnRequest = 1,
	PPTP_StartCtrlConnReply = 2,
	PPTP_StopCtrlConnRequest = 3,
	PPTP_StopCtrlConnReply = 4,
	PPTP_EchoRequest = 5,
	PPTP_EchoReply = 6,
	PPTP_OutCallRequest = 7,
	PPTP_OutCallReply = 8,
	PPTP_InCallRequest = 9,
	PPTP_InCallReply = 10,
	PPTP_InCallConn = 11,
	PPTP_CallClearRequest = 12,
	PPTP_CallDiscNotify = 13,
	PPTP_WanErrorNotify = 14,
	PPTP_SetLinkInfo = 15
};

 /* Message structures */
struct pptpMsgHead {
	u_int16_t	length;	/* total length */
	u_int16_t	msgType;/* PPTP message type */
	u_int32_t	magic;	/* magic cookie */
	u_int16_t	type;	/* control message type */
	u_int16_t	resv0;	/* reserved */
};
typedef struct pptpMsgHead *PptpMsgHead;

struct pptpCodes {
	u_int8_t	resCode;/* Result Code */
	u_int8_t	errCode;/* Error Code */
};
typedef struct pptpCodes *PptpCode;

struct pptpCallIds {
	u_int16_t	cid1;	/* Call ID field #1 */
	u_int16_t	cid2;	/* Call ID field #2 */
};
typedef struct pptpCallIds *PptpCallId;

static PptpCallId AliasVerifyPptp(struct ip *, u_int16_t *);


static void
AliasHandlePptpOut(struct libalias *la,
    struct ip *pip,		/* IP packet to examine/patch */
    struct alias_link *lnk)
{				/* The PPTP control link */
	struct alias_link *pptp_lnk;
	PptpCallId cptr;
	PptpCode codes;
	u_int16_t ctl_type;	/* control message type */
	struct tcphdr *tc;

	/* Verify valid PPTP control message */
	if ((cptr = AliasVerifyPptp(pip, &ctl_type)) == NULL)
		return;

	/* Modify certain PPTP messages */
	switch (ctl_type) {
	case PPTP_OutCallRequest:
	case PPTP_OutCallReply:
	case PPTP_InCallRequest:
	case PPTP_InCallReply:
		/*
		 * Establish PPTP link for address and Call ID found in
		 * control message.
		 */
		pptp_lnk = AddPptp(la, GetOriginalAddress(lnk), GetDestAddress(lnk),
		    GetAliasAddress(lnk), cptr->cid1);
		break;
	case PPTP_CallClearRequest:
	case PPTP_CallDiscNotify:
		/*
		 * Find PPTP link for address and Call ID found in control
		 * message.
		 */
		pptp_lnk = FindPptpOutByCallId(la, GetOriginalAddress(lnk),
		    GetDestAddress(lnk),
		    cptr->cid1);
		break;
	default:
		return;
	}

	if (pptp_lnk != NULL) {
		int accumulate = cptr->cid1;

		/* alias the Call Id */
		cptr->cid1 = GetAliasPort(pptp_lnk);

		/* Compute TCP checksum for revised packet */
		tc = (struct tcphdr *)ip_next(pip);
		accumulate -= cptr->cid1;
		ADJUST_CHECKSUM(accumulate, tc->th_sum);

		switch (ctl_type) {
		case PPTP_OutCallReply:
		case PPTP_InCallReply:
			codes = (PptpCode) (cptr + 1);
			if (codes->resCode == 1)	/* Connection
							 * established, */
				SetDestCallId(pptp_lnk,	/* note the Peer's Call
								 * ID. */
				    cptr->cid2);
			else
				SetExpire(pptp_lnk, 0);	/* Connection refused. */
			break;
		case PPTP_CallDiscNotify:	/* Connection closed. */
			SetExpire(pptp_lnk, 0);
			break;
		}
	}
}

static void
AliasHandlePptpIn(struct libalias *la,
    struct ip *pip,		/* IP packet to examine/patch */
    struct alias_link *lnk)
{				/* The PPTP control link */
	struct alias_link *pptp_lnk;
	PptpCallId cptr;
	u_int16_t *pcall_id;
	u_int16_t ctl_type;	/* control message type */
	struct tcphdr *tc;

	/* Verify valid PPTP control message */
	if ((cptr = AliasVerifyPptp(pip, &ctl_type)) == NULL)
		return;

	/* Modify certain PPTP messages */
	switch (ctl_type) {
	case PPTP_InCallConn:
	case PPTP_WanErrorNotify:
	case PPTP_SetLinkInfo:
		pcall_id = &cptr->cid1;
		break;
	case PPTP_OutCallReply:
	case PPTP_InCallReply:
		pcall_id = &cptr->cid2;
		break;
	case PPTP_CallDiscNotify:	/* Connection closed. */
		pptp_lnk = FindPptpInByCallId(la, GetDestAddress(lnk),
		    GetAliasAddress(lnk),
		    cptr->cid1);
		if (pptp_lnk != NULL)
			SetExpire(pptp_lnk, 0);
		return;
	default:
		return;
	}

	/* Find PPTP link for address and Call ID found in PPTP Control Msg */
	pptp_lnk = FindPptpInByPeerCallId(la, GetDestAddress(lnk),
	    GetAliasAddress(lnk),
	    *pcall_id);

	if (pptp_lnk != NULL) {
		int accumulate = *pcall_id;

		/* De-alias the Peer's Call Id. */
		*pcall_id = GetOriginalPort(pptp_lnk);

		/* Compute TCP checksum for modified packet */
		tc = (struct tcphdr *)ip_next(pip);
		accumulate -= *pcall_id;
		ADJUST_CHECKSUM(accumulate, tc->th_sum);

		if (ctl_type == PPTP_OutCallReply || ctl_type == PPTP_InCallReply) {
			PptpCode codes = (PptpCode) (cptr + 1);

			if (codes->resCode == 1)	/* Connection
							 * established, */
				SetDestCallId(pptp_lnk,	/* note the Call ID. */
				    cptr->cid1);
			else
				SetExpire(pptp_lnk, 0);	/* Connection refused. */
		}
	}
}

static		PptpCallId
AliasVerifyPptp(struct ip *pip, u_int16_t * ptype)
{				/* IP packet to examine/patch */
	int hlen, tlen, dlen;
	PptpMsgHead hptr;
	struct tcphdr *tc;

	/* Calculate some lengths */
	tc = (struct tcphdr *)ip_next(pip);
	hlen = (pip->ip_hl + tc->th_off) << 2;
	tlen = ntohs(pip->ip_len);
	dlen = tlen - hlen;

	/* Verify data length */
	if (dlen < (int)(sizeof(struct pptpMsgHead) + sizeof(struct pptpCallIds)))
		return (NULL);

	/* Move up to PPTP message header */
	hptr = (PptpMsgHead) tcp_next(tc);

	/* Return the control message type */
	*ptype = ntohs(hptr->type);

	/* Verify PPTP Control Message */
	if ((ntohs(hptr->msgType) != PPTP_CTRL_MSG_TYPE) ||
	    (ntohl(hptr->magic) != PPTP_MAGIC))
		return (NULL);

	/* Verify data length. */
	if ((*ptype == PPTP_OutCallReply || *ptype == PPTP_InCallReply) &&
	    (dlen < (int)(sizeof(struct pptpMsgHead) + sizeof(struct pptpCallIds) +
		sizeof(struct pptpCodes))))
		return (NULL);
	else
		return (PptpCallId) (hptr + 1);
}

static int
AliasHandlePptpGreOut(struct libalias *la, struct ip *pip)
{
	GreHdr *gr;
	struct alias_link *lnk;

	gr = (GreHdr *) ip_next(pip);

	/* Check GRE header bits. */
	if ((ntohl(*((u_int32_t *) gr)) & PPTP_INIT_MASK) != PPTP_INIT_VALUE)
		return (-1);

	lnk = FindPptpOutByPeerCallId(la, pip->ip_src, pip->ip_dst, gr->gh_call_id);
	if (lnk != NULL) {
		struct in_addr alias_addr = GetAliasAddress(lnk);

		/* Change source IP address. */
		DifferentialChecksum(&pip->ip_sum,
		    &alias_addr, &pip->ip_src, 2);
		pip->ip_src = alias_addr;
	}
	return (0);
}

static int
AliasHandlePptpGreIn(struct libalias *la, struct ip *pip)
{
	GreHdr *gr;
	struct alias_link *lnk;

	gr = (GreHdr *) ip_next(pip);

	/* Check GRE header bits. */
	if ((ntohl(*((u_int32_t *) gr)) & PPTP_INIT_MASK) != PPTP_INIT_VALUE)
		return (-1);

	lnk = FindPptpInByPeerCallId(la, pip->ip_src, pip->ip_dst, gr->gh_call_id);
	if (lnk != NULL) {
		struct in_addr src_addr = GetOriginalAddress(lnk);

		/* De-alias the Peer's Call Id. */
		gr->gh_call_id = GetOriginalPort(lnk);

		/* Restore original IP address. */
		DifferentialChecksum(&pip->ip_sum,
		    &src_addr, &pip->ip_dst, 2);
		pip->ip_dst = src_addr;
	}
	return (0);
}
