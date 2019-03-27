/*-
 * Copyright (c) 2005-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _PADLOCK_H_
#define _PADLOCK_H_

#include <opencrypto/cryptodev.h>
#include <crypto/rijndael/rijndael.h>

#if defined(__i386__)
#include <machine/npx.h>
#elif defined(__amd64__)
#include <machine/fpu.h>
#endif

union padlock_cw {
	uint64_t raw;
	struct {
		u_int round_count : 4;
		u_int algorithm_type : 3;
		u_int key_generation : 1;
		u_int intermediate : 1;
		u_int direction : 1;
		u_int key_size : 2;
		u_int filler0 : 20;
		u_int filler1 : 32;
		u_int filler2 : 32;
		u_int filler3 : 32;
	} __field;
};
#define	cw_round_count		__field.round_count
#define	cw_algorithm_type	__field.algorithm_type
#define	cw_key_generation	__field.key_generation
#define	cw_intermediate		__field.intermediate
#define	cw_direction		__field.direction
#define	cw_key_size		__field.key_size
#define	cw_filler0		__field.filler0
#define	cw_filler1		__field.filler1
#define	cw_filler2		__field.filler2
#define	cw_filler3		__field.filler3

struct padlock_session {
	union padlock_cw ses_cw __aligned(16);
	uint32_t	ses_ekey[4 * (RIJNDAEL_MAXNR + 1) + 4] __aligned(16);	/* 128 bit aligned */
	uint32_t	ses_dkey[4 * (RIJNDAEL_MAXNR + 1) + 4] __aligned(16);	/* 128 bit aligned */
	uint8_t		ses_iv[16] __aligned(16);			/* 128 bit aligned */
	struct auth_hash *ses_axf;
	uint8_t		*ses_ictx;
	uint8_t		*ses_octx;
	int		ses_mlen;
	struct fpu_kern_ctx *ses_fpu_ctx;
};

#define	PADLOCK_ALIGN(p)	(void *)(roundup2((uintptr_t)(p), 16))

int	padlock_cipher_setup(struct padlock_session *ses,
	    struct cryptoini *encini);
int	padlock_cipher_process(struct padlock_session *ses,
	    struct cryptodesc *enccrd, struct cryptop *crp);
int	padlock_hash_setup(struct padlock_session *ses,
	    struct cryptoini *macini);
int	padlock_hash_process(struct padlock_session *ses,
	    struct cryptodesc *maccrd, struct cryptop *crp);
void	padlock_hash_free(struct padlock_session *ses);

#endif	/* !_PADLOCK_H_ */
