/* $OpenBSD: chacha.h,v 1.4 2016/08/27 04:04:56 guenther Exp $ */

/*
chacha-merged.c version 20080118
D. J. Bernstein
Public domain.

 $FreeBSD$
*/

#ifndef CHACHA_H
#define CHACHA_H

#include <sys/types.h>
#include <crypto/chacha20/_chacha.h>

#define CHACHA_MINKEYLEN 	16
#define CHACHA_NONCELEN		8
#define CHACHA_CTRLEN		8
#define CHACHA_STATELEN		(CHACHA_NONCELEN+CHACHA_CTRLEN)
#define CHACHA_BLOCKLEN		64

#ifdef CHACHA_EMBED
#define LOCAL static
#else
#define LOCAL
#endif

#ifdef CHACHA_NONCE0_CTR128
#define CHACHA_UNUSED __unused
#else
#define CHACHA_UNUSED
#endif

LOCAL void chacha_keysetup(struct chacha_ctx *x, const u_char *k, u_int kbits);
LOCAL void chacha_ivsetup(struct chacha_ctx *x, const u_char *iv CHACHA_UNUSED,
    const u_char *ctr);
LOCAL void chacha_encrypt_bytes(struct chacha_ctx *x, const u_char *m,
    u_char *c, u_int bytes);

#undef CHACHA_UNUSED

#endif	/* CHACHA_H */

