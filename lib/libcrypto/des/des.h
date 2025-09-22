/* $OpenBSD: des.h,v 1.26 2025/06/09 17:49:45 tb Exp $ */
/* Copyright (C) 1995-1997 Eric Young (eay@cryptsoft.com)
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

#ifndef HEADER_DES_H
#define HEADER_DES_H

#include <openssl/opensslconf.h>

#ifndef DES_LONG
/* XXX - typedef to unsigned int everywhere. */
#ifdef __i386__
#define DES_LONG unsigned long
#else
#define DES_LONG unsigned int
#endif
#endif

#ifdef  __cplusplus
extern "C" {
#endif

typedef unsigned char DES_cblock[8];
typedef /* const */ unsigned char const_DES_cblock[8];
/* With "const", gcc 2.8.1 on Solaris thinks that DES_cblock *
 * and const_DES_cblock * are incompatible pointer types. */

typedef struct DES_ks {
	union {
		DES_cblock cblock;
	/* make sure things are correct size on machines with
	 * 8 byte longs */
		DES_LONG deslong[2];
	} ks[16];
} DES_key_schedule;

#define DES_KEY_SZ 	(sizeof(DES_cblock))
#define DES_SCHEDULE_SZ (sizeof(DES_key_schedule))

#define DES_ENCRYPT	1
#define DES_DECRYPT	0

#define DES_CBC_MODE	0
#define DES_PCBC_MODE	1

#define DES_ecb2_encrypt(i,o,k1,k2,e) \
	DES_ecb3_encrypt((i),(o),(k1),(k2),(k1),(e))

#define DES_ede2_cbc_encrypt(i,o,l,k1,k2,iv,e) \
	DES_ede3_cbc_encrypt((i),(o),(l),(k1),(k2),(k1),(iv),(e))

#define DES_ede2_cfb64_encrypt(i,o,l,k1,k2,iv,n,e) \
	DES_ede3_cfb64_encrypt((i),(o),(l),(k1),(k2),(k1),(iv),(n),(e))

#define DES_ede2_ofb64_encrypt(i,o,l,k1,k2,iv,n) \
	DES_ede3_ofb64_encrypt((i),(o),(l),(k1),(k2),(k1),(iv),(n))

extern int DES_check_key;	/* defaults to false */

void DES_ecb3_encrypt(const_DES_cblock *input, DES_cblock *output,
    DES_key_schedule *ks1, DES_key_schedule *ks2,
    DES_key_schedule *ks3, int enc);
DES_LONG DES_cbc_cksum(const unsigned char *input, DES_cblock *output,
    long length, DES_key_schedule *schedule,
    const_DES_cblock *ivec);
/* DES_cbc_encrypt does not update the IV!  Use DES_ncbc_encrypt instead. */
void DES_cbc_encrypt(const unsigned char *input, unsigned char *output,
    long length, DES_key_schedule *schedule, DES_cblock *ivec,
    int enc);
void DES_ncbc_encrypt(const unsigned char *input, unsigned char *output,
    long length, DES_key_schedule *schedule, DES_cblock *ivec,
    int enc);
void DES_xcbc_encrypt(const unsigned char *input, unsigned char *output,
    long length, DES_key_schedule *schedule, DES_cblock *ivec,
    const_DES_cblock *inw, const_DES_cblock *outw, int enc);
void DES_cfb_encrypt(const unsigned char *in, unsigned char *out, int numbits,
    long length, DES_key_schedule *schedule, DES_cblock *ivec,
    int enc);
void DES_ecb_encrypt(const_DES_cblock *input, DES_cblock *output,
    DES_key_schedule *ks, int enc);

/* 	This is the DES encryption function that gets called by just about
	every other DES routine in the library.  You should not use this
	function except to implement 'modes' of DES.  I say this because the
	functions that call this routine do the conversion from 'char *' to
	long, and this needs to be done to make sure 'non-aligned' memory
	access do not occur.  The characters are loaded 'little endian'.
	Data is a pointer to 2 unsigned long's and ks is the
	DES_key_schedule to use.  enc, is non zero specifies encryption,
	zero if decryption. */
void DES_encrypt1(DES_LONG *data, DES_key_schedule *ks, int enc);

/* 	This functions is the same as DES_encrypt1() except that the DES
	initial permutation (IP) and final permutation (FP) have been left
	out.  As for DES_encrypt1(), you should not use this function.
	It is used by the routines in the library that implement triple DES.
	IP() DES_encrypt2() DES_encrypt2() DES_encrypt2() FP() is the same
	as DES_encrypt1() DES_encrypt1() DES_encrypt1() except faster :-). */
void DES_encrypt2(DES_LONG *data, DES_key_schedule *ks, int enc);

void DES_encrypt3(DES_LONG *data, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3);
void DES_decrypt3(DES_LONG *data, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3);
void DES_ede3_cbc_encrypt(const unsigned char *input, unsigned char *output,
    long length,
    DES_key_schedule *ks1, DES_key_schedule *ks2,
    DES_key_schedule *ks3, DES_cblock *ivec, int enc);
void DES_ede3_cbcm_encrypt(const unsigned char *in, unsigned char *out,
    long length,
    DES_key_schedule *ks1, DES_key_schedule *ks2,
    DES_key_schedule *ks3,
    DES_cblock *ivec1, DES_cblock *ivec2,
    int enc);
void DES_ede3_cfb64_encrypt(const unsigned char *in, unsigned char *out,
    long length, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3,
    DES_cblock *ivec, int *num, int enc);
void DES_ede3_cfb_encrypt(const unsigned char *in, unsigned char *out,
    int numbits, long length, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3,
    DES_cblock *ivec, int enc);
void DES_ede3_ofb64_encrypt(const unsigned char *in, unsigned char *out,
    long length, DES_key_schedule *ks1,
    DES_key_schedule *ks2, DES_key_schedule *ks3,
    DES_cblock *ivec, int *num);
char *DES_fcrypt(const char *buf, const char *salt, char *ret);
char *DES_crypt(const char *buf, const char *salt);
void DES_ofb_encrypt(const unsigned char *in, unsigned char *out, int numbits,
    long length, DES_key_schedule *schedule, DES_cblock *ivec);
void DES_pcbc_encrypt(const unsigned char *input, unsigned char *output,
    long length, DES_key_schedule *schedule, DES_cblock *ivec,
    int enc);
DES_LONG DES_quad_cksum(const unsigned char *input, DES_cblock output[],
    long length, int out_count, DES_cblock *seed);
int DES_random_key(DES_cblock *ret);
void DES_set_odd_parity(DES_cblock *key);
int DES_check_key_parity(const_DES_cblock *key);
int DES_is_weak_key(const_DES_cblock *key);
/* DES_set_key (= set_key = DES_key_sched = key_sched) calls
 * DES_set_key_checked if global variable DES_check_key is set,
 * DES_set_key_unchecked otherwise. */
int DES_set_key(const_DES_cblock *key, DES_key_schedule *schedule);
int DES_key_sched(const_DES_cblock *key, DES_key_schedule *schedule);
int DES_set_key_checked(const_DES_cblock *key, DES_key_schedule *schedule);
void DES_set_key_unchecked(const_DES_cblock *key, DES_key_schedule *schedule);
void DES_string_to_key(const char *str, DES_cblock *key);
void DES_string_to_2keys(const char *str, DES_cblock *key1, DES_cblock *key2);
void DES_cfb64_encrypt(const unsigned char *in, unsigned char *out, long length,
    DES_key_schedule *schedule, DES_cblock *ivec, int *num,
    int enc);
void DES_ofb64_encrypt(const unsigned char *in, unsigned char *out, long length,
    DES_key_schedule *schedule, DES_cblock *ivec, int *num);

#define DES_fixup_key_parity DES_set_odd_parity

#ifdef  __cplusplus
}
#endif

#endif
