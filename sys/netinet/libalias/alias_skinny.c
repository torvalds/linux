/*-
 * alias_skinny.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2003 MarcusCom, Inc.
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
 * Author: Joe Marcus Clarke <marcus@FreeBSD.org>
 *
 * $FreeBSD$
 */

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#else
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#endif

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#ifdef _KERNEL
#include <netinet/libalias/alias_local.h>
#include <netinet/libalias/alias_mod.h>
#else
#include "alias_local.h"
#include "alias_mod.h"
#endif

static void
AliasHandleSkinny(struct libalias *, struct ip *, struct alias_link *);

static int
fingerprint(struct libalias *la, struct alias_data *ah)
{

	if (ah->dport == NULL || ah->sport == NULL || ah->lnk == NULL)
		return (-1);
	if (la->skinnyPort != 0 && (ntohs(*ah->sport) == la->skinnyPort ||
				    ntohs(*ah->dport) == la->skinnyPort))
		return (0);
	return (-1);
}

static int
protohandler(struct libalias *la, struct ip *pip, struct alias_data *ah)
{
	
        AliasHandleSkinny(la, pip, ah->lnk);
	return (0);
}

struct proto_handler handlers[] = {
	{
	  .pri = 110,
	  .dir = IN|OUT,
	  .proto = TCP,
	  .fingerprint = &fingerprint,
	  .protohandler = &protohandler
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
       "alias_skinny", mod_handler, NULL
};

#ifdef	_KERNEL
DECLARE_MODULE(alias_skinny, alias_mod, SI_SUB_DRIVERS, SI_ORDER_SECOND);
MODULE_VERSION(alias_skinny, 1);
MODULE_DEPEND(alias_skinny, libalias, 1, 1, 1);
#endif

/*
 * alias_skinny.c handles the translation for the Cisco Skinny Station
 * protocol.  Skinny typically uses TCP port 2000 to set up calls between
 * a Cisco Call Manager and a Cisco IP phone.  When a phone comes on line,
 * it first needs to register with the Call Manager.  To do this it sends
 * a registration message.  This message contains the IP address of the
 * IP phone.  This message must then be translated to reflect our global
 * IP address.  Along with the registration message (and usually in the
 * same packet), the phone sends an IP port message.  This message indicates
 * the TCP port over which it will communicate.
 *
 * When a call is placed from the phone, the Call Manager will send an
 * Open Receive Channel message to the phone to let the caller know someone
 * has answered.  The phone then sends back an Open Receive Channel
 * Acknowledgement.  In this packet, the phone sends its IP address again,
 * and the UDP port over which the voice traffic should flow.  These values
 * need translation.  Right after the Open Receive Channel Acknowledgement,
 * the Call Manager sends a Start Media Transmission message indicating the
 * call is connected.  This message contains the IP address and UDP port
 * number of the remote (called) party.  Once this message is translated, the
 * call can commence.  The called part sends the first UDP packet to the
 * calling phone at the pre-arranged UDP port in the Open Receive Channel
 * Acknowledgement.
 *
 * Skinny is a Cisco-proprietary protocol and is a trademark of Cisco Systems,
 * Inc.  All rights reserved.
*/

/* #define LIBALIAS_DEBUG 1 */

/* Message types that need translating */
#define REG_MSG         0x00000001
#define IP_PORT_MSG     0x00000002
#define OPNRCVCH_ACK    0x00000022
#define START_MEDIATX   0x0000008a

struct skinny_header {
	u_int32_t	len;
	u_int32_t	reserved;
	u_int32_t	msgId;
};

struct RegisterMessage {
	u_int32_t	msgId;
	char		devName   [16];
	u_int32_t	uid;
	u_int32_t	instance;
	u_int32_t	ipAddr;
	u_char		devType;
	u_int32_t	maxStreams;
};

struct IpPortMessage {
	u_int32_t	msgId;
	u_int32_t	stationIpPort;	/* Note: Skinny uses 32-bit port
					 * numbers */
};

struct OpenReceiveChannelAck {
	u_int32_t	msgId;
	u_int32_t	status;
	u_int32_t	ipAddr;
	u_int32_t	port;
	u_int32_t	passThruPartyID;
};

struct StartMediaTransmission {
	u_int32_t	msgId;
	u_int32_t	conferenceID;
	u_int32_t	passThruPartyID;
	u_int32_t	remoteIpAddr;
	u_int32_t	remotePort;
	u_int32_t	MSPacket;
	u_int32_t	payloadCap;
	u_int32_t	precedence;
	u_int32_t	silenceSuppression;
	u_short		maxFramesPerPacket;
	u_int32_t	G723BitRate;
};

typedef enum {
	ClientToServer = 0,
	ServerToClient = 1
} ConvDirection;


static int
alias_skinny_reg_msg(struct RegisterMessage *reg_msg, struct ip *pip,
    struct tcphdr *tc, struct alias_link *lnk,
    ConvDirection direction)
{
	(void)direction;

	reg_msg->ipAddr = (u_int32_t) GetAliasAddress(lnk).s_addr;

	tc->th_sum = 0;
#ifdef _KERNEL
	tc->th_x2 = 1;
#else
	tc->th_sum = TcpChecksum(pip);
#endif

	return (0);
}

static int
alias_skinny_startmedia(struct StartMediaTransmission *start_media,
    struct ip *pip, struct tcphdr *tc,
    struct alias_link *lnk, u_int32_t localIpAddr,
    ConvDirection direction)
{
	struct in_addr dst, src;

	(void)pip;
	(void)tc;
	(void)lnk;
	(void)direction;

	dst.s_addr = start_media->remoteIpAddr;
	src.s_addr = localIpAddr;

	/*
	 * XXX I should probably handle in bound global translations as
	 * well.
	 */

	return (0);
}

static int
alias_skinny_port_msg(struct IpPortMessage *port_msg, struct ip *pip,
    struct tcphdr *tc, struct alias_link *lnk,
    ConvDirection direction)
{
	(void)direction;

	port_msg->stationIpPort = (u_int32_t) ntohs(GetAliasPort(lnk));

	tc->th_sum = 0;
#ifdef _KERNEL
	tc->th_x2 = 1;
#else
	tc->th_sum = TcpChecksum(pip);
#endif
	return (0);
}

static int
alias_skinny_opnrcvch_ack(struct libalias *la, struct OpenReceiveChannelAck *opnrcvch_ack,
    struct ip *pip, struct tcphdr *tc,
    struct alias_link *lnk, u_int32_t * localIpAddr,
    ConvDirection direction)
{
	struct in_addr null_addr;
	struct alias_link *opnrcv_lnk;
	u_int32_t localPort;

	(void)lnk;
	(void)direction;

	*localIpAddr = (u_int32_t) opnrcvch_ack->ipAddr;
	localPort = opnrcvch_ack->port;

	null_addr.s_addr = INADDR_ANY;
	opnrcv_lnk = FindUdpTcpOut(la, pip->ip_src, null_addr,
	    htons((u_short) opnrcvch_ack->port), 0,
	    IPPROTO_UDP, 1);
	opnrcvch_ack->ipAddr = (u_int32_t) GetAliasAddress(opnrcv_lnk).s_addr;
	opnrcvch_ack->port = (u_int32_t) ntohs(GetAliasPort(opnrcv_lnk));

	tc->th_sum = 0;
#ifdef _KERNEL
	tc->th_x2 = 1;
#else
	tc->th_sum = TcpChecksum(pip);
#endif
	return (0);
}

static void
AliasHandleSkinny(struct libalias *la, struct ip *pip, struct alias_link *lnk)
{
	size_t hlen, tlen, dlen;
	struct tcphdr *tc;
	u_int32_t msgId, t, len, lip;
	struct skinny_header *sd;
	size_t orig_len, skinny_hdr_len = sizeof(struct skinny_header);
	ConvDirection direction;

	lip = -1;
	tc = (struct tcphdr *)ip_next(pip);
	hlen = (pip->ip_hl + tc->th_off) << 2;
	tlen = ntohs(pip->ip_len);
	dlen = tlen - hlen;

	sd = (struct skinny_header *)tcp_next(tc);

	/*
	 * XXX This direction is reserved for future use.  I still need to
	 * handle the scenario where the call manager is on the inside, and
	 * the calling phone is on the global outside.
	 */
	if (ntohs(tc->th_dport) == la->skinnyPort) {
		direction = ClientToServer;
	} else if (ntohs(tc->th_sport) == la->skinnyPort) {
		direction = ServerToClient;
	} else {
#ifdef LIBALIAS_DEBUG
		fprintf(stderr,
		    "PacketAlias/Skinny: Invalid port number, not a Skinny packet\n");
#endif
		return;
	}

	orig_len = dlen;
	/*
	 * Skinny packets can contain many messages.  We need to loop
	 * through the packet using len to determine message boundaries.
	 * This comes into play big time with port messages being in the
	 * same packet as register messages.  Also, open receive channel
	 * acks are usually buried in a packet some 400 bytes long.
	 */
	while (dlen >= skinny_hdr_len) {
		len = (sd->len);
		msgId = (sd->msgId);
		t = len;

		if (t > orig_len || t > dlen) {
#ifdef LIBALIAS_DEBUG
			fprintf(stderr,
			    "PacketAlias/Skinny: Not a skinny packet, invalid length \n");
#endif
			return;
		}
		switch (msgId) {
		case REG_MSG: {
			struct RegisterMessage *reg_mesg;

			if (len < (int)sizeof(struct RegisterMessage)) {
#ifdef LIBALIAS_DEBUG
				fprintf(stderr,
				    "PacketAlias/Skinny: Not a skinny packet, bad registration message\n");
#endif
				return;
			}
			reg_mesg = (struct RegisterMessage *)&sd->msgId;
#ifdef LIBALIAS_DEBUG
			fprintf(stderr,
			    "PacketAlias/Skinny: Received a register message");
#endif
			alias_skinny_reg_msg(reg_mesg, pip, tc, lnk, direction);
			break;
		}
		case IP_PORT_MSG: {
			struct IpPortMessage *port_mesg;

			if (len < (int)sizeof(struct IpPortMessage)) {
#ifdef LIBALIAS_DEBUG
				fprintf(stderr,
				    "PacketAlias/Skinny: Not a skinny packet, port message\n");
#endif
				return;
			}
#ifdef LIBALIAS_DEBUG
			fprintf(stderr,
			    "PacketAlias/Skinny: Received ipport message\n");
#endif
			port_mesg = (struct IpPortMessage *)&sd->msgId;
			alias_skinny_port_msg(port_mesg, pip, tc, lnk, direction);
			break;
		}
		case OPNRCVCH_ACK: {
			struct OpenReceiveChannelAck *opnrcvchn_ack;

			if (len < (int)sizeof(struct OpenReceiveChannelAck)) {
#ifdef LIBALIAS_DEBUG
				fprintf(stderr,
				    "PacketAlias/Skinny: Not a skinny packet, packet,OpnRcvChnAckMsg\n");
#endif
				return;
			}
#ifdef LIBALIAS_DEBUG
			fprintf(stderr,
			    "PacketAlias/Skinny: Received open rcv channel msg\n");
#endif
			opnrcvchn_ack = (struct OpenReceiveChannelAck *)&sd->msgId;
			alias_skinny_opnrcvch_ack(la, opnrcvchn_ack, pip, tc, lnk, &lip, direction);
			break;
		}
		case START_MEDIATX: {
			struct StartMediaTransmission *startmedia_tx;

			if (len < (int)sizeof(struct StartMediaTransmission)) {
#ifdef LIBALIAS_DEBUG
				fprintf(stderr,
				    "PacketAlias/Skinny: Not a skinny packet,StartMediaTx Message\n");
#endif
				return;
			}
			if (lip == -1) {
#ifdef LIBALIAS_DEBUG
				fprintf(stderr,
				    "PacketAlias/Skinny: received a"
				    " packet,StartMediaTx Message before"
				    " packet,OpnRcvChnAckMsg\n"
#endif
				return;
			}

#ifdef LIBALIAS_DEBUG
			fprintf(stderr,
			    "PacketAlias/Skinny: Received start media trans msg\n");
#endif
			startmedia_tx = (struct StartMediaTransmission *)&sd->msgId;
			alias_skinny_startmedia(startmedia_tx, pip, tc, lnk, lip, direction);
			break;
		}
		default:
			break;
		}
		/* Place the pointer at the next message in the packet. */
		dlen -= len + (skinny_hdr_len - sizeof(msgId));
		sd = (struct skinny_header *)(((char *)&sd->msgId) + len);
	}
}
