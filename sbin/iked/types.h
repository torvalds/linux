/*	$OpenBSD: types.h,v 1.55 2024/07/13 12:22:46 yasuoka Exp $	*/

/*
 * Copyright (c) 2019 Tobias Heider <tobias.heider@stusta.de>
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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

#ifndef IKED_TYPES_H
#define IKED_TYPES_H

#ifndef IKED_USER
#define IKED_USER		"_iked"
#endif

#ifndef IKED_CONFIG
#define IKED_CONFIG		"/etc/iked.conf"
#endif

#define IKED_SOCKET		"/var/run/iked.sock"

#ifndef IKED_CA
#define IKED_CA			"/etc/iked/"
#endif

#define IKED_CA_DIR		"ca/"
#define IKED_CRL_DIR		"crls/"
#define IKED_CERT_DIR		"certs/"
#define IKED_PUBKEY_DIR		"pubkeys/"
#define IKED_PRIVKEY		IKED_CA "private/local.key"
#define IKED_PUBKEY		"local.pub"

#define IKED_VENDOR_ID		"OpenIKED-"
#define IKED_NAS_ID		"OpenIKED"

#define IKED_OCSP_RESPCERT	"ocsp/responder.crt"

#define IKED_OPT_VERBOSE	0x00000001
#define IKED_OPT_NOACTION	0x00000002
#define IKED_OPT_PASSIVE	0x00000004

#define IKED_IKE_PORT		500
#define IKED_NATT_PORT		4500

#define IKED_NONCE_MIN		16	/* XXX 128 bits */
#define IKED_NONCE_SIZE		32	/* XXX 256 bits */

#define IKED_COOKIE_MIN		1	/* min 1 bytes */
#define IKED_COOKIE_MAX		64	/* max 64 bytes */

#define IKED_COOKIE2_MIN	8	/* min 8 bytes */
#define IKED_COOKIE2_MAX	64	/* max 64 bytes */

#define IKED_ID_SIZE		1024	/* XXX should be dynamic */
#define IKED_PSK_SIZE		1024	/* XXX should be dynamic */
#define IKED_MSGBUF_MAX		8192
#define IKED_CFG_MAX		16	/* maximum CP attributes */
#define IKED_IPPROTO_MAX	16
#define IKED_TAG_SIZE		64
#define IKED_CYCLE_BUFFERS	8	/* # of static buffers for mapping */
#define IKED_PASSWORD_SIZE	256	/* limited by most EAP types */

#define IKED_LIFETIME_BYTES	4294967296ULL	/* 4 GB */
#define IKED_LIFETIME_SECONDS	10800	  	/* 3 hours */

#define IKED_E			0x1000	/* Decrypted flag */

struct iked_constmap {
	unsigned int	 cm_type;
	const char	*cm_name;
	const char	*cm_descr;
};

struct iked_transform {
	uint8_t				 xform_type;
	uint16_t			 xform_id;
	uint16_t			 xform_length;
	uint16_t			 xform_keylength;
	unsigned int			 xform_score;
	struct iked_constmap		*xform_map;
};

enum imsg_type {
	IMSG_NONE,
	IMSG_CTL_OK,
	IMSG_CTL_FAIL,
	IMSG_CTL_VERBOSE,
	IMSG_CTL_NOTIFY,
	IMSG_CTL_RELOAD,
	IMSG_CTL_RESET,
	IMSG_CTL_COUPLE,
	IMSG_CTL_DECOUPLE,
	IMSG_CTL_ACTIVE,
	IMSG_CTL_PASSIVE,
	IMSG_CTL_RESET_ID,
	IMSG_CTL_SHOW_SA,
	IMSG_CTL_STATIC,
	IMSG_COMPILE,
	IMSG_UDP_SOCKET,
	IMSG_PFKEY_SOCKET,
	IMSG_IKE_MESSAGE,
	IMSG_CFG_POLICY,
	IMSG_CFG_FLOW,
	IMSG_CFG_USER,
	IMSG_CFG_RADAUTH,
	IMSG_CFG_RADACCT,
	IMSG_CFG_RADSERVER,
	IMSG_CFG_RADCFGMAP,
	IMSG_CFG_RADDAE,
	IMSG_CFG_RADDAECLIENT,
	IMSG_CERTREQ,
	IMSG_CERT,
	IMSG_CERTVALID,
	IMSG_CERTINVALID,
	IMSG_SCERT,
	IMSG_IF_ADDADDR,
	IMSG_IF_DELADDR,
	IMSG_VROUTE_ADD,
	IMSG_VROUTE_DEL,
	IMSG_VROUTE_CLONE,
	IMSG_VDNS_ADD,
	IMSG_VDNS_DEL,
	IMSG_OCSP_FD,
	IMSG_OCSP_CFG,
	IMSG_AUTH,
	IMSG_PRIVKEY,
	IMSG_PUBKEY,
	IMSG_CTL_SHOW_CERTSTORE,
	IMSG_CTL_SHOW_STATS,
	IMSG_CTL_PROCFD,
	IMSG_CTL_PROCREADY,
};

enum privsep_procid {
	PROC_PARENT = 0,
	PROC_CONTROL,
	PROC_CERT,
	PROC_IKEV2,
	PROC_MAX
};

enum flushmode {
	RESET_RELOAD	= 0,
	RESET_ALL,
	RESET_CA,
	RESET_POLICY,
	RESET_SA,
	RESET_USER,
	RESET_RADIUS,
};

#ifndef nitems
#define nitems(_a)   (sizeof((_a)) / sizeof((_a)[0]))
#endif

#endif /* IKED_TYPES_H */
