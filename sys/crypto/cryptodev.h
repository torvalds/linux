/*	$OpenBSD: cryptodev.h,v 1.82 2022/05/03 09:18:11 claudio Exp $	*/

/*
 * The author of this code is Angelos D. Keromytis (angelos@cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Athens, Greece, in
 * February 2000. Network Security Technologies Inc. (NSTI) kindly
 * supported the development of this code.
 *
 * Copyright (c) 2000 Angelos D. Keromytis
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all source code copies of any software which is or includes a copy or
 * modification of this software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 *
 * Copyright (c) 2001 Theo de Raadt
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#ifndef _CRYPTO_CRYPTO_H_
#define _CRYPTO_CRYPTO_H_

#include <sys/task.h>

/* Some initial values */
#define CRYPTO_DRIVERS_INITIAL	4
#define CRYPTO_DRIVERS_MAX	128
#define CRYPTO_SW_SESSIONS	32

/* HMAC values */
#define HMAC_MD5_BLOCK_LEN	64
#define HMAC_SHA1_BLOCK_LEN	64
#define HMAC_RIPEMD160_BLOCK_LEN 64
#define HMAC_SHA2_256_BLOCK_LEN	64
#define HMAC_SHA2_384_BLOCK_LEN	128
#define HMAC_SHA2_512_BLOCK_LEN	128
#define HMAC_MAX_BLOCK_LEN	HMAC_SHA2_512_BLOCK_LEN	/* keep in sync */
#define HMAC_IPAD_VAL		0x36
#define HMAC_OPAD_VAL		0x5C

/* Encryption algorithm block sizes */
#define DES3_BLOCK_LEN		8
#define BLOWFISH_BLOCK_LEN	8
#define CAST128_BLOCK_LEN	8
#define RIJNDAEL128_BLOCK_LEN	16
#define CHACHA20_BLOCK_LEN	64
#define EALG_MAX_BLOCK_LEN	64 /* Keep this updated */

/* Maximum hash algorithm result length */
#define AALG_MAX_RESULT_LEN	64 /* Keep this updated */

#define CRYPTO_3DES_CBC		1
#define CRYPTO_BLF_CBC		2
#define CRYPTO_CAST_CBC		3
#define CRYPTO_MD5_HMAC		4
#define CRYPTO_SHA1_HMAC	5
#define CRYPTO_RIPEMD160_HMAC	6
#define CRYPTO_RIJNDAEL128_CBC	7  /* 128 bit blocksize */
#define CRYPTO_AES_CBC		7  /* 128 bit blocksize -- the same as above */
#define CRYPTO_DEFLATE_COMP	8  /* Deflate compression algorithm */
#define CRYPTO_NULL		9
#define CRYPTO_SHA2_256_HMAC	11
#define CRYPTO_SHA2_384_HMAC	12
#define CRYPTO_SHA2_512_HMAC	13
#define CRYPTO_AES_CTR		14
#define CRYPTO_AES_XTS		15
#define CRYPTO_AES_GCM_16	16
#define CRYPTO_AES_128_GMAC	17
#define CRYPTO_AES_192_GMAC	18
#define CRYPTO_AES_256_GMAC	19
#define CRYPTO_AES_GMAC		20
#define CRYPTO_CHACHA20_POLY1305	21
#define CRYPTO_CHACHA20_POLY1305_MAC	22
#define CRYPTO_ESN		23 /* Support for Extended Sequence Numbers */
#define CRYPTO_ALGORITHM_MAX	23 /* Keep updated */

/* Algorithm flags */
#define	CRYPTO_ALG_FLAG_SUPPORTED	0x01 /* Algorithm is supported */

/* Standard initialization structure beginning */
struct cryptoini {
	int		cri_alg;	/* Algorithm to use */
	int		cri_klen;	/* Key length, in bits */
	int		cri_rnd;	/* Algorithm rounds, where relevant */
	caddr_t		cri_key;	/* key to use */
	union {
		u_int8_t	iv[EALG_MAX_BLOCK_LEN];	/* IV to use */
		u_int8_t	esn[4];			/* high-order ESN */
	} u;
#define cri_iv		u.iv
#define cri_esn		u.esn
	struct cryptoini *cri_next;
};

/* Describe boundaries of a single crypto operation */
struct cryptodesc {
	int		crd_skip;	/* How many bytes to ignore from start */
	int		crd_len;	/* How many bytes to process */
	int		crd_inject;	/* Where to inject results, if applicable */
	int		crd_flags;

#define	CRD_F_ENCRYPT		0x01	/* Set when doing encryption */
#define	CRD_F_IV_PRESENT	0x02	/* When encrypting, IV is already in
					   place, so don't copy. */
#define	CRD_F_IV_EXPLICIT	0x04	/* IV explicitly provided */
#define CRD_F_COMP		0x10    /* Set when doing compression */
#define CRD_F_ESN		0x20	/* Set when ESN field is provided */

	struct cryptoini	CRD_INI; /* Initialization/context data */
#define crd_esn		CRD_INI.cri_esn
#define crd_iv		CRD_INI.cri_iv
#define crd_key		CRD_INI.cri_key
#define crd_rnd		CRD_INI.cri_rnd
#define crd_alg		CRD_INI.cri_alg
#define crd_klen	CRD_INI.cri_klen
};

/* Structure describing complete operation */
struct cryptop {
	u_int64_t	crp_sid;	/* Session ID */
	int		crp_ilen;	/* Input data total length */
	int		crp_olen;	/* Result total length */
	int		crp_alloctype;	/* Type of buf to allocate if needed */

	int		crp_flags;

#define CRYPTO_F_IMBUF	0x0001	/* Input/output are mbuf chains, otherwise contig */
#define CRYPTO_F_IOV	0x0002	/* Input/output are uio */

	void 		*crp_buf;	/* Data to be processed */

	struct cryptodesc *crp_desc;	/* List of processing descriptors */
	struct cryptodesc crp_sdesc[2];	/* Static array for small ops */
	int		 crp_ndesc;	/* Amount of descriptors to use */
	int		 crp_ndescalloc;/* Amount of descriptors allocated */

	caddr_t		crp_mac;
};

#define CRYPTO_BUF_IOV		0x1
#define CRYPTO_BUF_MBUF		0x2

#define CRYPTO_OP_DECRYPT	0x0
#define CRYPTO_OP_ENCRYPT	0x1

/* Crypto capabilities structure */
struct cryptocap {
	u_int64_t	cc_operations;	/* Counter of how many ops done */
	u_int64_t	cc_bytes;	/* Counter of how many bytes done */

	u_int32_t	cc_sessions;	/* How many sessions allocated */

	/* Symmetric/hash algorithms supported */
	int		cc_alg[CRYPTO_ALGORITHM_MAX + 1];

	u_int8_t	cc_flags;
#define CRYPTOCAP_F_CLEANUP     0x01
#define CRYPTOCAP_F_SOFTWARE    0x02
#define CRYPTOCAP_F_MPSAFE      0x04

	int		(*cc_newsession) (u_int32_t *, struct cryptoini *);
	int		(*cc_process) (struct cryptop *);
	int		(*cc_freesession) (u_int64_t);
};

void	crypto_init(void);

int	crypto_newsession(u_int64_t *, struct cryptoini *, int);
int	crypto_freesession(u_int64_t);
int	crypto_register(u_int32_t, int *,
	    int (*)(u_int32_t *, struct cryptoini *), int (*)(u_int64_t),
	    int (*)(struct cryptop *));
int	crypto_unregister(u_int32_t, int);
int32_t	crypto_get_driverid(u_int8_t);
int	crypto_invoke(struct cryptop *);

void	cuio_copydata(struct uio *, int, int, caddr_t);
void	cuio_copyback(struct uio *, int, int, const void *);
int	cuio_getptr(struct uio *, int, int *);
int	cuio_apply(struct uio *, int, int,
	    int (*f)(caddr_t, caddr_t, unsigned int), caddr_t);

struct	cryptop *crypto_getreq(int);
void	crypto_freereq(struct cryptop *);
#endif /* _CRYPTO_CRYPTO_H_ */
