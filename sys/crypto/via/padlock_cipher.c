/*-
 * Copyright (c) 2005-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2004 Mark R V Murray
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*	$OpenBSD: via.c,v 1.3 2004/06/15 23:36:55 deraadt Exp $	*/
/*-
 * Copyright (c) 2003 Jason Wright
 * Copyright (c) 2003, 2004 Theo de Raadt
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/libkern.h>
#include <sys/pcpu.h>
#include <sys/uio.h>

#include <opencrypto/cryptodev.h>
#include <crypto/rijndael/rijndael.h>

#include <crypto/via/padlock.h>

#define	PADLOCK_ROUND_COUNT_AES128	10
#define	PADLOCK_ROUND_COUNT_AES192	12
#define	PADLOCK_ROUND_COUNT_AES256	14

#define	PADLOCK_ALGORITHM_TYPE_AES	0

#define	PADLOCK_KEY_GENERATION_HW	0
#define	PADLOCK_KEY_GENERATION_SW	1

#define	PADLOCK_DIRECTION_ENCRYPT	0
#define	PADLOCK_DIRECTION_DECRYPT	1

#define	PADLOCK_KEY_SIZE_128	0
#define	PADLOCK_KEY_SIZE_192	1
#define	PADLOCK_KEY_SIZE_256	2

MALLOC_DECLARE(M_PADLOCK);

static __inline void
padlock_cbc(void *in, void *out, size_t count, void *key, union padlock_cw *cw,
    void *iv)
{
#ifdef __GNUCLIKE_ASM
	/* The .byte line is really VIA C3 "xcrypt-cbc" instruction */
	__asm __volatile(
		"pushf				\n\t"
		"popf				\n\t"
		"rep				\n\t"
		".byte	0x0f, 0xa7, 0xd0"
			: "+a" (iv), "+c" (count), "+D" (out), "+S" (in)
			: "b" (key), "d" (cw)
			: "cc", "memory"
		);
#endif
}

static void
padlock_cipher_key_setup(struct padlock_session *ses, caddr_t key, int klen)
{
	union padlock_cw *cw;
	int i;

	cw = &ses->ses_cw;
	if (cw->cw_key_generation == PADLOCK_KEY_GENERATION_SW) {
		/* Build expanded keys for both directions */
		rijndaelKeySetupEnc(ses->ses_ekey, key, klen);
		rijndaelKeySetupDec(ses->ses_dkey, key, klen);
		for (i = 0; i < 4 * (RIJNDAEL_MAXNR + 1); i++) {
			ses->ses_ekey[i] = ntohl(ses->ses_ekey[i]);
			ses->ses_dkey[i] = ntohl(ses->ses_dkey[i]);
		}
	} else {
		bcopy(key, ses->ses_ekey, klen);
		bcopy(key, ses->ses_dkey, klen);
	}
}

int
padlock_cipher_setup(struct padlock_session *ses, struct cryptoini *encini)
{
	union padlock_cw *cw;

	if (encini->cri_klen != 128 && encini->cri_klen != 192 &&
	    encini->cri_klen != 256) {
		return (EINVAL);
	}

	cw = &ses->ses_cw;
	bzero(cw, sizeof(*cw));
	cw->cw_algorithm_type = PADLOCK_ALGORITHM_TYPE_AES;
	cw->cw_key_generation = PADLOCK_KEY_GENERATION_SW;
	cw->cw_intermediate = 0;
	switch (encini->cri_klen) {
	case 128:
		cw->cw_round_count = PADLOCK_ROUND_COUNT_AES128;
		cw->cw_key_size = PADLOCK_KEY_SIZE_128;
#ifdef HW_KEY_GENERATION
		/* This doesn't buy us much, that's why it is commented out. */
		cw->cw_key_generation = PADLOCK_KEY_GENERATION_HW;
#endif
		break;
	case 192:
		cw->cw_round_count = PADLOCK_ROUND_COUNT_AES192;
		cw->cw_key_size = PADLOCK_KEY_SIZE_192;
		break;
	case 256:
		cw->cw_round_count = PADLOCK_ROUND_COUNT_AES256;
		cw->cw_key_size = PADLOCK_KEY_SIZE_256;
		break;
	}
	if (encini->cri_key != NULL) {
		padlock_cipher_key_setup(ses, encini->cri_key,
		    encini->cri_klen);
	}

	arc4rand(ses->ses_iv, sizeof(ses->ses_iv), 0);
	return (0);
}

/*
 * Function checks if the given buffer is already 16 bytes aligned.
 * If it is there is no need to allocate new buffer.
 * If it isn't, new buffer is allocated.
 */
static u_char *
padlock_cipher_alloc(struct cryptodesc *enccrd, struct cryptop *crp,
    int *allocated)
{
	u_char *addr;

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		goto alloc;
	else {
		if (crp->crp_flags & CRYPTO_F_IOV) {
			struct uio *uio;
			struct iovec *iov;

			uio = (struct uio *)crp->crp_buf;
			if (uio->uio_iovcnt != 1)
				goto alloc;
			iov = uio->uio_iov;
			addr = (u_char *)iov->iov_base + enccrd->crd_skip;
		} else {
			addr = (u_char *)crp->crp_buf;
		}
		if (((uintptr_t)addr & 0xf) != 0) /* 16 bytes aligned? */
			goto alloc;
		*allocated = 0;
		return (addr);
	}
alloc:
	*allocated = 1;
	addr = malloc(enccrd->crd_len + 16, M_PADLOCK, M_NOWAIT);
	return (addr);
}

int
padlock_cipher_process(struct padlock_session *ses, struct cryptodesc *enccrd,
    struct cryptop *crp)
{
	union padlock_cw *cw;
	struct thread *td;
	u_char *buf, *abuf;
	uint32_t *key;
	int allocated;

	buf = padlock_cipher_alloc(enccrd, crp, &allocated);
	if (buf == NULL)
		return (ENOMEM);
	/* Buffer has to be 16 bytes aligned. */
	abuf = PADLOCK_ALIGN(buf);

	if ((enccrd->crd_flags & CRD_F_KEY_EXPLICIT) != 0) {
		padlock_cipher_key_setup(ses, enccrd->crd_key,
		    enccrd->crd_klen);
	}

	cw = &ses->ses_cw;
	cw->cw_filler0 = 0;
	cw->cw_filler1 = 0;
	cw->cw_filler2 = 0;
	cw->cw_filler3 = 0;
	if ((enccrd->crd_flags & CRD_F_ENCRYPT) != 0) {
		cw->cw_direction = PADLOCK_DIRECTION_ENCRYPT;
		key = ses->ses_ekey;
		if ((enccrd->crd_flags & CRD_F_IV_EXPLICIT) != 0)
			bcopy(enccrd->crd_iv, ses->ses_iv, AES_BLOCK_LEN);

		if ((enccrd->crd_flags & CRD_F_IV_PRESENT) == 0) {
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    enccrd->crd_inject, AES_BLOCK_LEN, ses->ses_iv);
		}
	} else {
		cw->cw_direction = PADLOCK_DIRECTION_DECRYPT;
		key = ses->ses_dkey;
		if ((enccrd->crd_flags & CRD_F_IV_EXPLICIT) != 0)
			bcopy(enccrd->crd_iv, ses->ses_iv, AES_BLOCK_LEN);
		else {
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    enccrd->crd_inject, AES_BLOCK_LEN, ses->ses_iv);
		}
	}

	if (allocated) {
		crypto_copydata(crp->crp_flags, crp->crp_buf, enccrd->crd_skip,
		    enccrd->crd_len, abuf);
	}

	td = curthread;
	fpu_kern_enter(td, ses->ses_fpu_ctx, FPU_KERN_NORMAL | FPU_KERN_KTHR);
	padlock_cbc(abuf, abuf, enccrd->crd_len / AES_BLOCK_LEN, key, cw,
	    ses->ses_iv);
	fpu_kern_leave(td, ses->ses_fpu_ctx);

	if (allocated) {
		crypto_copyback(crp->crp_flags, crp->crp_buf, enccrd->crd_skip,
		    enccrd->crd_len, abuf);
	}

	/* copy out last block for use as next session IV */
	if ((enccrd->crd_flags & CRD_F_ENCRYPT) != 0) {
		crypto_copydata(crp->crp_flags, crp->crp_buf,
		    enccrd->crd_skip + enccrd->crd_len - AES_BLOCK_LEN,
		    AES_BLOCK_LEN, ses->ses_iv);
	}

	if (allocated) {
		bzero(buf, enccrd->crd_len + 16);
		free(buf, M_PADLOCK);
	}
	return (0);
}
