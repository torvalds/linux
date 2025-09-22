/* $OpenBSD: prf.h,v 1.10 2004/04/15 18:39:26 deraadt Exp $	 */
/* $EOM: prf.h,v 1.1 1998/07/11 20:06:22 provos Exp $	 */

/*
 * Copyright (c) 1998 Niels Provos.  All rights reserved.
 * Copyright (c) 2001 Niklas Hallqvist.  All rights reserved.
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
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#ifndef _PRF_H_
#define _PRF_H_

/* Enumeration of possible PRF - Pseudo-Random Functions.  */
enum prfs {
	PRF_HMAC = 0		/* No PRFs in drafts, this is the default */
};

struct prf {
	enum prfs       type;	/* Type of PRF */
	void           *prfctx;	/* Context for PRF */
	u_int8_t        blocksize;	/* The blocksize of PRF */
	void            (*Init) (void *);
	void            (*Update) (void *, unsigned char *, unsigned int);
	void            (*Final) (unsigned char *, void *);
};

struct prf_hash_ctx {
	struct hash    *hash;	/* Hash type to use */
	void           *ctx, *ctx2;	/* Contexts we need for later */
};

struct prf     *prf_alloc(enum prfs, int, unsigned char *, unsigned int);
void            prf_free(struct prf *);

#endif				/* _PRF_H_ */
