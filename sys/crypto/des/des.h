/*	$FreeBSD$	*/
/*	$KAME: des.h,v 1.8 2001/09/10 04:03:57 itojun Exp $	*/

/* lib/des/des.h */
/* Copyright (C) 1995-1996 Eric Young (eay@mincom.oz.au)
 * All rights reserved.
 *
 * This file is part of an SSL implementation written
 * by Eric Young (eay@mincom.oz.au).
 * The implementation was written so as to conform with Netscapes SSL
 * specification.  This library and applications are
 * FREE FOR COMMERCIAL AND NON-COMMERCIAL USE
 * as long as the following conditions are aheared to.
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.  If this code is used in a product,
 * Eric Young should be given attribution as the author of the parts used.
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
 *    This product includes software developed by Eric Young (eay@mincom.oz.au)
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

#ifdef  __cplusplus
extern "C" {
#endif

/* must be 32bit quantity */
#define DES_LONG u_int32_t

typedef unsigned char des_cblock[8];
typedef struct des_ks_struct
	{
	union   {
	des_cblock cblock;
	/* make sure things are correct size on machines with
	 * 8 byte longs */
	DES_LONG deslong[2];
	} ks;
	int weak_key;
} des_key_schedule[16];

#define DES_KEY_SZ 	(sizeof(des_cblock))
#define DES_SCHEDULE_SZ (sizeof(des_key_schedule))

#define DES_ENCRYPT	1
#define DES_DECRYPT	0

#define DES_CBC_MODE	0
#define DES_PCBC_MODE	1

extern int des_check_key;	/* defaults to false */

char *des_options(void);
void des_ecb_encrypt(des_cblock *, des_cblock *, des_key_schedule, int);

void des_encrypt1(DES_LONG *, des_key_schedule, int);
void des_encrypt2(DES_LONG *, des_key_schedule, int);
void des_encrypt3(DES_LONG *, des_key_schedule, des_key_schedule,
		      des_key_schedule);
void des_decrypt3(DES_LONG *, des_key_schedule, des_key_schedule,
		      des_key_schedule);

void des_ecb3_encrypt(des_cblock *, des_cblock *, des_key_schedule, 
			  des_key_schedule, des_key_schedule, int);

void des_ncbc_encrypt(const unsigned char *, unsigned char *, long,
			  des_key_schedule, des_cblock *, int);

void des_ede3_cbc_encrypt(const unsigned char *, unsigned char *, long,
			  des_key_schedule, des_key_schedule, 
			  des_key_schedule, des_cblock *, int);

void des_set_odd_parity(des_cblock *);
void des_fixup_key_parity(des_cblock *); 
int des_is_weak_key(des_cblock *);
int des_set_key(des_cblock *, des_key_schedule);
int des_key_sched(des_cblock *, des_key_schedule);
int des_set_key_checked(des_cblock *, des_key_schedule);
void des_set_key_unchecked(des_cblock *, des_key_schedule);
int des_check_key_parity(des_cblock *);

#ifdef  __cplusplus
}
#endif

#endif
