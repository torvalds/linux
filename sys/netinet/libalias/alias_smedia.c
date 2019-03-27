/*-
 * alias_smedia.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause
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
 * Copyright (c) 2000  Junichi SATOH <junichi@astec.co.jp>
 *                                   <junichi@junichi.org>
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
 * Authors: Erik Salander <erik@whistle.com>
 *          Junichi SATOH <junichi@astec.co.jp>
 *                        <junichi@junichi.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
   Alias_smedia.c is meant to contain the aliasing code for streaming media
   protocols.  It performs special processing for RSTP sessions under TCP.
   Specifically, when a SETUP request is sent by a client, or a 200 reply
   is sent by a server, it is intercepted and modified.  The address is
   changed to the gateway machine and an aliasing port is used.

   More specifically, the "client_port" configuration parameter is
   parsed for SETUP requests.  The "server_port" configuration parameter is
   parsed for 200 replies eminating from a server.  This is intended to handle
   the unicast case.

   RTSP also allows a redirection of a stream to another client by using the
   "destination" configuration parameter.  The destination config parm would
   indicate a different IP address.  This function is NOT supported by the
   RTSP translation code below.

   The RTSP multicast functions without any address translation intervention.

   For this routine to work, the SETUP/200 must fit entirely
   into a single TCP packet.  This is typically the case, but exceptions
   can easily be envisioned under the actual specifications.

   Probably the most troubling aspect of the approach taken here is
   that the new SETUP/200 will typically be a different length, and
   this causes a certain amount of bookkeeping to keep track of the
   changes of sequence and acknowledgment numbers, since the client
   machine is totally unaware of the modification to the TCP stream.

   Initial version:  May, 2000 (eds)
*/

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#else
#include <errno.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#endif

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#ifdef _KERNEL
#include <netinet/libalias/alias.h>
#include <netinet/libalias/alias_local.h>
#include <netinet/libalias/alias_mod.h>
#else
#include "alias_local.h"
#include "alias_mod.h"
#endif

#define RTSP_CONTROL_PORT_NUMBER_1 554
#define RTSP_CONTROL_PORT_NUMBER_2 7070
#define TFTP_PORT_NUMBER 69

static void
AliasHandleRtspOut(struct libalias *, struct ip *, struct alias_link *,	
		  int maxpacketsize);
static int
fingerprint(struct libalias *la, struct alias_data *ah)
{

	if (ah->dport != NULL && ah->aport != NULL && ah->sport != NULL &&
            ntohs(*ah->dport) == TFTP_PORT_NUMBER)
		return (0);
	if (ah->dport == NULL || ah->sport == NULL || ah->lnk == NULL ||
	    ah->maxpktsize == 0)
		return (-1);
	if (ntohs(*ah->dport) == RTSP_CONTROL_PORT_NUMBER_1
	    || ntohs(*ah->sport) == RTSP_CONTROL_PORT_NUMBER_1
	    || ntohs(*ah->dport) == RTSP_CONTROL_PORT_NUMBER_2
	    || ntohs(*ah->sport) == RTSP_CONTROL_PORT_NUMBER_2)
		return (0);
	return (-1);
}

static int
protohandler(struct libalias *la, struct ip *pip, struct alias_data *ah)
{
	
	if (ntohs(*ah->dport) == TFTP_PORT_NUMBER)
		FindRtspOut(la, pip->ip_src, pip->ip_dst,
 			    *ah->sport, *ah->aport, IPPROTO_UDP);
	else AliasHandleRtspOut(la, pip, ah->lnk, ah->maxpktsize);	
	return (0);
}

struct proto_handler handlers[] = {
	{
	  .pri = 100,
	  .dir = OUT,
	  .proto = TCP|UDP,
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
       "alias_smedia", mod_handler, NULL
};

#ifdef	_KERNEL
DECLARE_MODULE(alias_smedia, alias_mod, SI_SUB_DRIVERS, SI_ORDER_SECOND);
MODULE_VERSION(alias_smedia, 1);
MODULE_DEPEND(alias_smedia, libalias, 1, 1, 1);
#endif

#define RTSP_CONTROL_PORT_NUMBER_1 554
#define RTSP_CONTROL_PORT_NUMBER_2 7070
#define RTSP_PORT_GROUP            2

#define ISDIGIT(a) (((a) >= '0') && ((a) <= '9'))

static int
search_string(char *data, int dlen, const char *search_str)
{
	int i, j, k;
	int search_str_len;

	search_str_len = strlen(search_str);
	for (i = 0; i < dlen - search_str_len; i++) {
		for (j = i, k = 0; j < dlen - search_str_len; j++, k++) {
			if (data[j] != search_str[k] &&
			    data[j] != search_str[k] - ('a' - 'A')) {
				break;
			}
			if (k == search_str_len - 1) {
				return (j + 1);
			}
		}
	}
	return (-1);
}

static int
alias_rtsp_out(struct libalias *la, struct ip *pip,
    struct alias_link *lnk,
    char *data,
    const char *port_str)
{
	int hlen, tlen, dlen;
	struct tcphdr *tc;
	int i, j, pos, state, port_dlen, new_dlen, delta;
	u_short p[2], new_len;
	u_short sport, eport, base_port;
	u_short salias = 0, ealias = 0, base_alias = 0;
	const char *transport_str = "transport:";
	char newdata[2048], *port_data, *port_newdata, stemp[80];
	int links_created = 0, pkt_updated = 0;
	struct alias_link *rtsp_lnk = NULL;
	struct in_addr null_addr;

	/* Calculate data length of TCP packet */
	tc = (struct tcphdr *)ip_next(pip);
	hlen = (pip->ip_hl + tc->th_off) << 2;
	tlen = ntohs(pip->ip_len);
	dlen = tlen - hlen;

	/* Find keyword, "Transport: " */
	pos = search_string(data, dlen, transport_str);
	if (pos < 0) {
		return (-1);
	}
	port_data = data + pos;
	port_dlen = dlen - pos;

	memcpy(newdata, data, pos);
	port_newdata = newdata + pos;

	while (port_dlen > (int)strlen(port_str)) {
		/* Find keyword, appropriate port string */
		pos = search_string(port_data, port_dlen, port_str);
		if (pos < 0) {
			break;
		}
		memcpy(port_newdata, port_data, pos + 1);
		port_newdata += (pos + 1);

		p[0] = p[1] = 0;
		sport = eport = 0;
		state = 0;
		for (i = pos; i < port_dlen; i++) {
			switch (state) {
			case 0:
				if (port_data[i] == '=') {
					state++;
				}
				break;
			case 1:
				if (ISDIGIT(port_data[i])) {
					p[0] = p[0] * 10 + port_data[i] - '0';
				} else {
					if (port_data[i] == ';') {
						state = 3;
					}
					if (port_data[i] == '-') {
						state++;
					}
				}
				break;
			case 2:
				if (ISDIGIT(port_data[i])) {
					p[1] = p[1] * 10 + port_data[i] - '0';
				} else {
					state++;
				}
				break;
			case 3:
				base_port = p[0];
				sport = htons(p[0]);
				eport = htons(p[1]);

				if (!links_created) {

					links_created = 1;
					/*
					 * Find an even numbered port
					 * number base that satisfies the
					 * contiguous number of ports we
					 * need
					 */
					null_addr.s_addr = 0;
					if (0 == (salias = FindNewPortGroup(la, null_addr,
					    FindAliasAddress(la, pip->ip_src),
					    sport, 0,
					    RTSP_PORT_GROUP,
					    IPPROTO_UDP, 1))) {
#ifdef LIBALIAS_DEBUG
						fprintf(stderr,
						    "PacketAlias/RTSP: Cannot find contiguous RTSP data ports\n");
#endif
					} else {

						base_alias = ntohs(salias);
						for (j = 0; j < RTSP_PORT_GROUP; j++) {
							/*
							 * Establish link
							 * to port found in
							 * RTSP packet
							 */
							rtsp_lnk = FindRtspOut(la, GetOriginalAddress(lnk), null_addr,
							    htons(base_port + j), htons(base_alias + j),
							    IPPROTO_UDP);
							if (rtsp_lnk != NULL) {
#ifndef NO_FW_PUNCH
								/*
								 * Punch
								 * hole in
								 * firewall
								 */
								PunchFWHole(rtsp_lnk);
#endif
							} else {
#ifdef LIBALIAS_DEBUG
								fprintf(stderr,
								    "PacketAlias/RTSP: Cannot allocate RTSP data ports\n");
#endif
								break;
							}
						}
					}
					ealias = htons(base_alias + (RTSP_PORT_GROUP - 1));
				}
				if (salias && rtsp_lnk) {

					pkt_updated = 1;

					/* Copy into IP packet */
					sprintf(stemp, "%d", ntohs(salias));
					memcpy(port_newdata, stemp, strlen(stemp));
					port_newdata += strlen(stemp);

					if (eport != 0) {
						*port_newdata = '-';
						port_newdata++;

						/* Copy into IP packet */
						sprintf(stemp, "%d", ntohs(ealias));
						memcpy(port_newdata, stemp, strlen(stemp));
						port_newdata += strlen(stemp);
					}
					*port_newdata = ';';
					port_newdata++;
				}
				state++;
				break;
			}
			if (state > 3) {
				break;
			}
		}
		port_data += i;
		port_dlen -= i;
	}

	if (!pkt_updated)
		return (-1);

	memcpy(port_newdata, port_data, port_dlen);
	port_newdata += port_dlen;
	*port_newdata = '\0';

	/* Create new packet */
	new_dlen = port_newdata - newdata;
	memcpy(data, newdata, new_dlen);

	SetAckModified(lnk);
	tc = (struct tcphdr *)ip_next(pip);
	delta = GetDeltaSeqOut(tc->th_seq, lnk);
	AddSeq(lnk, delta + new_dlen - dlen, pip->ip_hl, pip->ip_len,
	    tc->th_seq, tc->th_off);

	new_len = htons(hlen + new_dlen);
	DifferentialChecksum(&pip->ip_sum,
	    &new_len,
	    &pip->ip_len,
	    1);
	pip->ip_len = new_len;

	tc->th_sum = 0;
#ifdef _KERNEL
	tc->th_x2 = 1;
#else
	tc->th_sum = TcpChecksum(pip);
#endif
	return (0);
}

/* Support the protocol used by early versions of RealPlayer */

static int
alias_pna_out(struct libalias *la, struct ip *pip,
    struct alias_link *lnk,
    char *data,
    int dlen)
{
	struct alias_link *pna_links;
	u_short msg_id, msg_len;
	char *work;
	u_short alias_port, port;
	struct tcphdr *tc;

	work = data;
	work += 5;
	while (work + 4 < data + dlen) {
		memcpy(&msg_id, work, 2);
		work += 2;
		memcpy(&msg_len, work, 2);
		work += 2;
		if (ntohs(msg_id) == 0) {
			/* end of options */
			return (0);
		}
		if ((ntohs(msg_id) == 1) || (ntohs(msg_id) == 7)) {
			memcpy(&port, work, 2);
			pna_links = FindUdpTcpOut(la, pip->ip_src, GetDestAddress(lnk),
			    port, 0, IPPROTO_UDP, 1);
			if (pna_links != NULL) {
#ifndef NO_FW_PUNCH
				/* Punch hole in firewall */
				PunchFWHole(pna_links);
#endif
				tc = (struct tcphdr *)ip_next(pip);
				alias_port = GetAliasPort(pna_links);
				memcpy(work, &alias_port, 2);

				/* Compute TCP checksum for revised packet */
				tc->th_sum = 0;
#ifdef _KERNEL
				tc->th_x2 = 1;
#else
				tc->th_sum = TcpChecksum(pip);
#endif
			}
		}
		work += ntohs(msg_len);
	}

	return (0);
}

static void
AliasHandleRtspOut(struct libalias *la, struct ip *pip, struct alias_link *lnk, int maxpacketsize)
{
	int hlen, tlen, dlen;
	struct tcphdr *tc;
	char *data;
	const char *setup = "SETUP", *pna = "PNA", *str200 = "200";
	const char *okstr = "OK", *client_port_str = "client_port";
	const char *server_port_str = "server_port";
	int i, parseOk;

	(void)maxpacketsize;

	tc = (struct tcphdr *)ip_next(pip);
	hlen = (pip->ip_hl + tc->th_off) << 2;
	tlen = ntohs(pip->ip_len);
	dlen = tlen - hlen;

	data = (char *)pip;
	data += hlen;

	/* When aliasing a client, check for the SETUP request */
	if ((ntohs(tc->th_dport) == RTSP_CONTROL_PORT_NUMBER_1) ||
	    (ntohs(tc->th_dport) == RTSP_CONTROL_PORT_NUMBER_2)) {

		if (dlen >= (int)strlen(setup)) {
			if (memcmp(data, setup, strlen(setup)) == 0) {
				alias_rtsp_out(la, pip, lnk, data, client_port_str);
				return;
			}
		}
		if (dlen >= (int)strlen(pna)) {
			if (memcmp(data, pna, strlen(pna)) == 0) {
				alias_pna_out(la, pip, lnk, data, dlen);
			}
		}
	} else {

		/*
		 * When aliasing a server, check for the 200 reply
		 * Accommodate varying number of blanks between 200 & OK
		 */

		if (dlen >= (int)strlen(str200)) {

			for (parseOk = 0, i = 0;
			    i <= dlen - (int)strlen(str200);
			    i++) {
				if (memcmp(&data[i], str200, strlen(str200)) == 0) {
					parseOk = 1;
					break;
				}
			}
			if (parseOk) {

				i += strlen(str200);	/* skip string found */
				while (data[i] == ' ')	/* skip blank(s) */
					i++;

				if ((dlen - i) >= (int)strlen(okstr)) {

					if (memcmp(&data[i], okstr, strlen(okstr)) == 0)
						alias_rtsp_out(la, pip, lnk, data, server_port_str);

				}
			}
		}
	}
}
