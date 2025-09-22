/*	$OpenBSD: print-nsh.c,v 1.2 2023/02/28 10:04:50 claudio Exp $ */

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

/*
 * RFC 8300 Network Service Header (NSH)
 */

#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"

#ifndef roundup
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))
#endif

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

struct nsh_header {
	uint32_t	base;
#define NSH_VER_SHIFT		30
#define NSH_VER_MASK		(0x03 << NSH_VER_SHIFT)
#define NSH_VER_0		0x0
#define NSH_VER_RESERVED	(0x01 << NSH_VER_SHIFT)
#define NSH_OAM_SHIFT		29
#define NSH_OAM_MASK		(0x01 << NSH_OAM_SHIFT)
#define NSH_TTL_SHIFT		22
#define NSH_TTL_MASK		(0x3f << NSH_TTL_SHIFT)
#define NSH_LEN_SHIFT		16
#define NSH_LEN_MASK		(0x3f << NSH_LEN_SHIFT)
#define NSH_LEN_FACTOR		4
#define NSH_MDTYPE_SHIFT	8
#define NSH_MDTYPE_MASK		(0x0f << NSH_MDTYPE_SHIFT)
#define NSH_PROTO_SHIFT		0
#define NSH_PROTO_MASK		(0xff << NSH_PROTO_SHIFT)

	uint32_t	sp;
#define NSH_SPI_SHIFT		8
#define NSH_SPI_MASK		(0xffffff << NSH_SPI_SHIFT)
#define NSH_SI_SHIFT		0
#define NSH_SI_MASK		(0xff << NSH_SI_SHIFT)
};

#define NSH_PROTO_IPV4		0x01
#define NSH_PROTO_IPV6		0x02
#define NSH_PROTO_ETHERNET	0x03
#define NSH_PROTO_NSH		0x04
#define NSH_PROTO_MPLS		0x05
#define NSH_PROTO_EXP1		0xfe	/* Experiment 1 */
#define NSH_PROTO_EXP2		0xff	/* Experiment 2 */

#define NSH_MDTYPE_RESERVED	0x0
#define NSH_MDTYPE_1		0x1
#define NSH_MDTYPE_2		0x2
#define NSH_MDTYPE_EXP		0xf	/* Experimentation */

struct nsh_context_header {
	uint32_t	ch[4];
};

struct nsh_md_header {
	uint16_t	class;
	uint8_t		type;
	uint8_t		len;
#define NSH_MD_LEN_MASK		0x7f
};

static void	nsh_print_bytes(const void *, u_int);

static void	nsh_print_mdtype1(const u_char *, u_int);
static void	nsh_print_mdtype2(const u_char *, u_int);

void
nsh_print(const u_char *p, u_int length)
{
	struct nsh_header nsh;
	uint32_t field, len, proto;
	int l = snapend - p;

	printf("NSH");

	if (l < sizeof(nsh))
		goto trunc;
	if (length < sizeof(nsh)) {
		printf(" encapsulation truncated");
		return;
	}

	nsh.base = EXTRACT_32BITS(p);
	nsh.sp = EXTRACT_32BITS(p + sizeof(nsh.base));

	field = (nsh.base & NSH_VER_MASK) >> NSH_VER_SHIFT;
	switch (field) {
	case NSH_VER_0:
		break;
	case NSH_VER_RESERVED:
		printf(" Reserved version");
		return;
	default:
		printf(" Unknown version %u", field);
		return;
	}

	field = (nsh.sp & NSH_SPI_MASK) >> NSH_SPI_SHIFT;
	printf(" spi %u", field);
	field = (nsh.sp & NSH_SI_MASK) >> NSH_SI_SHIFT;
	printf(" si %u", field);

	len = ((nsh.base & NSH_LEN_MASK) >> NSH_LEN_SHIFT) * NSH_LEN_FACTOR;
	if (vflag > 1) {
		field = (nsh.base & NSH_TTL_MASK) >> NSH_TTL_SHIFT;
		printf(" (ttl %u, len %u)", field, len);
	}

	if (l < len)
		goto trunc;
	if (length < len) {
		printf(" encapsulation truncated");
		return;
	}

	p += sizeof(nsh);
	l -= sizeof(nsh);
	len -= sizeof(nsh);

	field = (nsh.base & NSH_MDTYPE_MASK) >> NSH_MDTYPE_SHIFT;
	switch (field) {
	case NSH_MDTYPE_RESERVED:
		printf(" md-type-reserved");
		break;
	case NSH_MDTYPE_1:
		printf(" md1");
		if (vflag)
			nsh_print_mdtype1(p, len);
		break;
	case NSH_MDTYPE_2:
		printf(" md2");
		if (vflag)
			nsh_print_mdtype2(p, len);
		break;
	case NSH_MDTYPE_EXP:
		printf(" mdtype-experimentation");
		break;
	default:
		printf(" mdtype-unknown-0x%02x", field);
		break;
	}

	printf("%s", vflag ? "\n    " : ": ");

	p += len;
	l -= len;
	length -= len;

	proto = (nsh.base & NSH_PROTO_MASK) >> NSH_PROTO_SHIFT;

	if (nsh.base & NSH_OAM_MASK)
		printf("NSH OAM (proto 0x%0x, len %u)", proto, length);
	else {
		switch (field) {
		case NSH_PROTO_IPV4:
			ip_print(p, length);
			return;
		case NSH_PROTO_IPV6:
			ip_print(p, length);
			return;
		case NSH_PROTO_ETHERNET:
			ether_tryprint(p, length, 0);
			return;
		case NSH_PROTO_NSH:
			nsh_print(p, length);
			return;
		case NSH_PROTO_MPLS:
			mpls_print(p, length);
			return;
		case NSH_PROTO_EXP1:
			printf("NSH Experiment 1");
			break;
		case NSH_PROTO_EXP2:
			printf("NSH Experiment 2");
			break;
		default:
			printf("nsh-unknown-proto-0x%02x", field);
			break;
		}
	}

	if (vflag)
		default_print(p, l);

	return;
trunc:
	printf(" [|nsh]");
}

static void
nsh_print_mdtype1(const u_char *p, u_int len)
{
	const struct nsh_context_header *ctx;
	size_t i;

	if (len != sizeof(*ctx))
		printf("nsh-mdtype1-length-%u (not %zu)", len, sizeof(*ctx));

	printf("\n\tcontext");

	ctx = (const struct nsh_context_header *)p;
	for (i = 0; i < nitems(ctx->ch); i++) {
		printf(" ");
		nsh_print_bytes(&ctx->ch[i], sizeof(ctx->ch[i]));
	}
}

static void
nsh_print_mdtype2(const u_char *p, u_int l)
{
	if (l == 0)
		return;

	do {
		struct nsh_md_header h;
		uint8_t len;

		if (l < sizeof(h))
			goto trunc;

		memcpy(&h, p, sizeof(h));
		p += sizeof(h);
		l -= sizeof(h);

		h.class = ntohs(h.class);
		len = h.len & NSH_MD_LEN_MASK;
		printf("\n\tmd class %u type %u", h.class, h.type);
		if (len > 0) {
			printf(" ");
			nsh_print_bytes(p, len);
		}

		len = roundup(len, 4);
		if (l < len)
			goto trunc;

		p += len;
		l -= len;
	} while (l > 0);

	return;
trunc:
	printf("[|nsh md]");
}

static void
nsh_print_bytes(const void *b, u_int l)
{
	const uint8_t *p = b;
	u_int i;

	for (i = 0; i < l; i++) {
		int ch = p[i];
#if 0
		if (isprint(ch) && !isspace(ch))
			putchar(ch);
		else {
			switch (ch) {
			case '\\':
				printf("\\\\");
				break;
			case '\0':
				printf("\\0");
				break;
			default:
				printf("\\x%02x", ch);
				break;
			}
		}
#else
		printf("%02x", ch);
#endif
	}
}
