/*	$OpenBSD: print-wg.c,v 1.7 2021/09/16 12:35:20 visa Exp $ */

/*
 * Copyright (C) 2015-2020 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2020 Matt Dunwoodie <ncon@noconroy.net>
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

#include <sys/types.h>

#include <stdio.h>
#include <stddef.h>

#include "interface.h"
#include "extract.h"

#define INITIATION	1
#define RESPONSE	2
#define COOKIE		3
#define DATA		4

struct wg_initiation {
	uint32_t	type;
	uint32_t	sender;
	uint8_t		fill[140]; /* Includes ephemeral + MAC */
};

struct wg_response {
	uint32_t	type;
	uint32_t	sender;
	uint32_t	receiver;
	uint8_t		fill[80]; /* Includes ephemeral + MAC */
};

struct wg_cookie {
	uint32_t	type;
	uint32_t	receiver;
	uint8_t		fill[56]; /* Includes nonce + encrypted cookie */
};

struct wg_data {
	uint32_t	type;
	uint32_t	receiver;
	uint64_t	nonce;
	/* uint8_t	data[variable]; - Variable length data */
	uint8_t		mac[16];
};

/*
 * Check if packet is a WireGuard packet, as WireGuard may run on any port.
 */
uint32_t
wg_match(const u_char *bp, u_int length)
{
	uint32_t type;

	if (length < sizeof(type))
		return 0;

	if (snapend - bp < sizeof(type)) {
                /*
		 * we don't have enough bytes to tell if it is wg,
                 * so don't claim it, and don't claim it's truncated
                 * wireguard either.
		 */
		return (0);
	}

	type = EXTRACT_LE_32BITS(bp);

	if (type == INITIATION && length == sizeof(struct wg_initiation))
		return INITIATION;
	if (type == RESPONSE && length == sizeof(struct wg_response))
		return RESPONSE;
	if (type == COOKIE && length == sizeof(struct wg_cookie))
		return COOKIE;
	if (type == DATA && length >= sizeof(struct wg_data))
		return DATA;
	return 0;
}

/*
 * Print WireGuard packet
 */
void
wg_print(const u_char *bp, u_int length)
{
	uint32_t		 type;
	uint64_t		 datalength;
	struct wg_initiation	*initiation = (void *)bp;
	struct wg_response	*response = (void *)bp;
	struct wg_cookie	*cookie = (void *)bp;
	struct wg_data		*data = (void *)bp;
	u_int			 caplen;

	caplen = snapend - bp;
	if (caplen < sizeof(type))
		goto trunc;

	if ((type = wg_match(bp, length)) == 0) {
		/* doesn't match */
		printf("[wg] unknown");
		return;
	}

	switch (type) {
	case INITIATION:
		printf("[wg] initiation ");
		if (caplen < offsetof(struct wg_initiation, fill))
			goto trunc;
		printf("from 0x%08x", letoh32(initiation->sender));
		break;
	case RESPONSE:
		printf("[wg] response ");
		if (caplen < offsetof(struct wg_response, fill))
			goto trunc;
		printf("from 0x%08x to 0x%08x",
		    letoh32(response->sender), letoh32(response->receiver));
		break;
	case COOKIE:
		printf("[wg] cookie ");
		if (caplen < offsetof(struct wg_cookie, fill))
			goto trunc;
		printf(" to 0x%08x", letoh32(cookie->receiver));
		break;
	case DATA:
		datalength = length - sizeof(struct wg_data);
		if (datalength != 0)
			printf("[wg] data length %llu ", datalength);
		else
			printf("[wg] keepalive ");
		if (caplen < offsetof(struct wg_data, mac))
			goto trunc;
		/* data->nonce may be unaligned. */
		printf("to 0x%08x nonce %llu",
		    letoh32(data->receiver), EXTRACT_LE_64BITS(&data->nonce));
		break;
	}
	return;

trunc:
	printf("[|wg]");
}
