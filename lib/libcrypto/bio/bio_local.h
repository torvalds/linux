/* $OpenBSD: bio_local.h,v 1.6 2024/03/02 09:18:28 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#ifndef HEADER_BIO_LOCAL_H
#define HEADER_BIO_LOCAL_H

#include <stdarg.h>

__BEGIN_HIDDEN_DECLS

struct bio_method_st {
	int type;
	const char *name;
	int (*bwrite)(BIO *, const char *, int);
	int (*bread)(BIO *, char *, int);
	int (*bputs)(BIO *, const char *);
	int (*bgets)(BIO *, char *, int);
	long (*ctrl)(BIO *, int, long, void *);
	int (*create)(BIO *);
	int (*destroy)(BIO *);
	long (*callback_ctrl)(BIO *, int, BIO_info_cb *);
} /* BIO_METHOD */;

struct bio_st {
	const BIO_METHOD *method;
	BIO_callback_fn callback;
	BIO_callback_fn_ex callback_ex;
	char *cb_arg; /* first argument for the callback */

	int init;
	int shutdown;
	int flags;	/* extra storage */
	int retry_reason;
	int num;
	void *ptr;
	struct bio_st *next_bio;	/* used by filter BIOs */
	struct bio_st *prev_bio;	/* used by filter BIOs */
	int references;
	unsigned long num_read;
	unsigned long num_write;

	CRYPTO_EX_DATA ex_data;
} /* BIO */;

typedef struct bio_f_buffer_ctx_struct {
	/* Buffers are setup like this:
	 *
	 * <---------------------- size ----------------------->
	 * +---------------------------------------------------+
	 * | consumed | remaining          | free space        |
	 * +---------------------------------------------------+
	 * <-- off --><------- len ------->
	 */

	/* BIO *bio; */ /* this is now in the BIO struct */
	int ibuf_size;	/* how big is the input buffer */
	int obuf_size;	/* how big is the output buffer */

	char *ibuf;	/* the char array */
	int ibuf_len;	/* how many bytes are in it */
	int ibuf_off;	/* write/read offset */

	char *obuf;	/* the char array */
	int obuf_len;	/* how many bytes are in it */
	int obuf_off;	/* write/read offset */
} BIO_F_BUFFER_CTX;

int BIO_vprintf(BIO *bio, const char *format, va_list args);

__END_HIDDEN_DECLS

#endif /* !HEADER_BIO_LOCAL_H */
