/*	$FreeBSD$	*/
/*	$OpenBSD: ip_ipsp.h,v 1.119 2002/03/14 01:27:11 millert Exp $	*/
/*-
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr),
 * Niels Provos (provos@physnet.uni-hamburg.de) and
 * Niklas Hallqvist (niklas@appli.se).
 *
 * The original version of this code was written by John Ioannidis
 * for BSD/OS in Athens, Greece, in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Additional features in 1999 by Angelos D. Keromytis and Niklas Hallqvist.
 *
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
 * Copyright (c) 1999 Niklas Hallqvist.
 * Copyright (c) 2001, Angelos D. Keromytis.
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#ifndef _NETIPSEC_XFORM_H_
#define _NETIPSEC_XFORM_H_

#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <opencrypto/xform.h>

#define	AH_HMAC_HASHLEN		12	/* 96 bits of authenticator */
#define	AH_HMAC_MAXHASHLEN	(SHA2_512_HASH_LEN/2)	/* Keep this updated */
#define	AH_HMAC_INITIAL_RPL	1	/* replay counter initial value */

#ifdef _KERNEL
struct secpolicy;
struct secasvar;

/*
 * Packet tag assigned on completion of IPsec processing; used
 * to speedup security policy checking for INBOUND packets.
 */
struct xform_history {
	union sockaddr_union	dst;		/* destination address */
	uint32_t		spi;		/* Security Parameters Index */
	uint8_t			proto;		/* IPPROTO_ESP or IPPROTO_AH */
	uint8_t			mode;		/* transport or tunnel */
};

/*
 * Opaque data structure hung off a crypto operation descriptor.
 */
struct xform_data {
	struct secpolicy	*sp;		/* security policy */
	struct secasvar		*sav;		/* related SA */
	crypto_session_t	cryptoid;	/* used crypto session */
	u_int			idx;		/* IPsec request index */
	int			protoff;	/* current protocol offset */
	int			skip;		/* data offset */
	uint8_t			nxt;		/* next protocol, e.g. IPV4 */
	struct vnet		*vnet;
};

#define	XF_IP4		1	/* unused */
#define	XF_AH		2	/* AH */
#define	XF_ESP		3	/* ESP */
#define	XF_TCPSIGNATURE	5	/* TCP MD5 Signature option, RFC 2358 */
#define	XF_IPCOMP	6	/* IPCOMP */

struct xformsw {
	u_short			xf_type;	/* xform ID */
	const char		*xf_name;	/* human-readable name */
	int	(*xf_init)(struct secasvar*, struct xformsw*);	/* setup */
	int	(*xf_zeroize)(struct secasvar*);		/* cleanup */
	int	(*xf_input)(struct mbuf*, struct secasvar*,	/* input */
			int, int);
	int	(*xf_output)(struct mbuf*,			/* output */
	    struct secpolicy *, struct secasvar *, u_int, int, int);

	volatile u_int		xf_cntr;
	LIST_ENTRY(xformsw)	chain;
};

const struct enc_xform * enc_algorithm_lookup(int);
const struct auth_hash * auth_algorithm_lookup(int);
const struct comp_algo * comp_algorithm_lookup(int);

void xform_attach(void *);
void xform_detach(void *);
int xform_init(struct secasvar *, u_short);

struct cryptoini;
/* XF_AH */
int xform_ah_authsize(const struct auth_hash *);
extern int ah_init0(struct secasvar *, struct xformsw *, struct cryptoini *);
extern int ah_zeroize(struct secasvar *sav);
extern size_t ah_hdrsiz(struct secasvar *);

/* XF_ESP */
extern size_t esp_hdrsiz(struct secasvar *sav);

#endif /* _KERNEL */
#endif /* _NETIPSEC_XFORM_H_ */
