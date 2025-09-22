/*	$OpenBSD: print-dhcp6.c,v 1.13 2021/12/01 18:28:45 deraadt Exp $	*/

/*
 * Copyright (c) 2019 David Gwynne <dlg@openbsd.org>
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

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include "interface.h"
#include "extract.h"
#include "addrtoname.h"

/* Message type */
#define DH6_SOLICIT		1
#define DH6_ADVERTISE		2
#define DH6_REQUEST		3
#define DH6_CONFIRM		4
#define DH6_RENEW		5
#define DH6_REBIND		6
#define DH6_REPLY		7
#define DH6_RELEASE		8
#define DH6_DECLINE		9
#define DH6_RECONFIGURE		10
#define DH6_INFORMATION_REQUEST	11
#define DH6_RELAY_FORW		12
#define DH6_RELAY_REPL		13

static void
dhcp6opt_print(const u_char *cp, u_int length)
{
	uint16_t code, len;
	u_int i;
	int l = snapend - cp;

	while (length > 0) {
		if (l < sizeof(code))
			goto trunc;
		if (length < sizeof(code))
			goto iptrunc;

		code = EXTRACT_16BITS(cp);
		cp += sizeof(code);
		length -= sizeof(code);
		l -= sizeof(code);

		if (l < sizeof(len))
			goto trunc;
		if (length < sizeof(len))
			goto iptrunc;

		len = EXTRACT_16BITS(cp);
		cp += sizeof(len);
		length -= sizeof(len);
		l -= sizeof(len);

		printf("\n\toption %u len %u", code, len);

		if (len > 0) {
			if (l < len)
				goto trunc;
			if (length < len)
				goto iptrunc;

			printf(" ");
			for (i = 0; i < len; i++)
				printf("%02x", cp[4 + i] & 0xff);

			cp += len;
			length -= len;
			l -= len;
		}
	}
	return;

trunc:
	printf(" [|dhcp6opt]");
	return;
iptrunc:
	printf(" ip truncated");
}

static void
dhcp6_relay_print(const u_char *cp, u_int length)
{
	uint8_t msgtype;
	const char *msgname = NULL;

	msgtype = *cp;

	switch (msgtype) {
	case DH6_RELAY_FORW:
		msgname = "Relay-forward";
		break;
	case DH6_RELAY_REPL:
		msgname = "Relay-reply";
		break;
	}

	printf(" %s", msgname);
}

void
dhcp6_print(const u_char *cp, u_int length)
{
	uint8_t msgtype;
	uint32_t hdr;
	int l = snapend - cp;
	const char *msgname;

	printf("DHCPv6");

	if (l < sizeof(msgtype))
		goto trunc;
	if (length < sizeof(msgtype))
		goto iptrunc;

	msgtype = *cp;

	switch (msgtype) {
	case DH6_SOLICIT:
		msgname = "Solicit";
		break;
	case DH6_ADVERTISE:
		msgname = "Advertise";
		break;
	case DH6_REQUEST:
		msgname = "Request";
		break;
	case DH6_CONFIRM:
		msgname = "Confirm";
		break;
	case DH6_RENEW:
		msgname = "Renew";
		break;
	case DH6_REBIND:
		msgname = "Rebind";
		break;
	case DH6_REPLY:
		msgname = "Reply";
		break;
	case DH6_RELEASE:
		msgname = "Release";
		break;
	case DH6_DECLINE:
		msgname = "Decline";
		break;
	case DH6_RECONFIGURE:
		msgname = "Reconfigure";
		break;
	case DH6_INFORMATION_REQUEST:
		msgname = "Information-request";
		break;
	case DH6_RELAY_FORW:
	case DH6_RELAY_REPL:
		dhcp6_relay_print(cp, length);
		return;
	default:
		printf(" unknown message type %u", msgtype);
		return;
	}

	printf(" %s", msgname);

	if (l < sizeof(hdr))
		goto trunc;
	if (length < sizeof(hdr))
		goto iptrunc;

	hdr = EXTRACT_32BITS(cp);
	printf(" xid %x", hdr & 0xffffff);

	if (vflag) {
		cp += sizeof(hdr);
		length -= sizeof(hdr);

		dhcp6opt_print(cp, length);
	}
	return;

trunc:
	printf(" [|dhcp6]");
	return;
iptrunc:
	printf(" ip truncated");
}
