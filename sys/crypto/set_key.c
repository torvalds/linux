/*	$OpenBSD: set_key.c,v 1.5 2021/03/12 10:22:46 jsg Exp $	*/

/* lib/des/set_key.c */
/* Copyright (C) 1995 Eric Young (eay@mincom.oz.au)
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

/* set_key.c v 1.4 eay 24/9/91
 * 1.4 Speed up by 400% :-)
 * 1.3 added register declarations.
 * 1.2 unrolled make_key_sched a bit more
 * 1.1 added norm_expand_bits
 * 1.0 First working version
 */
#include "des_locl.h"
#include "podd.h"
#include "sk.h"

static int check_parity(des_cblock (*key));

int des_check_key=0;

static int
check_parity(des_cblock (*key))
{
	int i;

	for (i = 0; i < DES_KEY_SZ; i++) {
		if ((*key)[i] != odd_parity[(*key)[i]])
			return(0);
	}
	return (1);
}

/* Weak and semi week keys as take from
 * %A D.W. Davies
 * %A W.L. Price
 * %T Security for Computer Networks
 * %I John Wiley & Sons
 * %D 1984
 * Many thanks to smb@ulysses.att.com (Steven Bellovin) for the reference
 * (and actual cblock values).
 */
#define NUM_WEAK_KEY	16
static des_cblock weak_keys[NUM_WEAK_KEY]={
	/* weak keys */
	{0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01},
	{0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE},
	{0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},
	{0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0},
	/* semi-weak keys */
	{0x01,0xFE,0x01,0xFE,0x01,0xFE,0x01,0xFE},
	{0xFE,0x01,0xFE,0x01,0xFE,0x01,0xFE,0x01},
	{0x1F,0xE0,0x1F,0xE0,0x0E,0xF1,0x0E,0xF1},
	{0xE0,0x1F,0xE0,0x1F,0xF1,0x0E,0xF1,0x0E},
	{0x01,0xE0,0x01,0xE0,0x01,0xF1,0x01,0xF1},
	{0xE0,0x01,0xE0,0x01,0xF1,0x01,0xF1,0x01},
	{0x1F,0xFE,0x1F,0xFE,0x0E,0xFE,0x0E,0xFE},
	{0xFE,0x1F,0xFE,0x1F,0xFE,0x0E,0xFE,0x0E},
	{0x01,0x1F,0x01,0x1F,0x01,0x0E,0x01,0x0E},
	{0x1F,0x01,0x1F,0x01,0x0E,0x01,0x0E,0x01},
	{0xE0,0xFE,0xE0,0xFE,0xF1,0xFE,0xF1,0xFE},
	{0xFE,0xE0,0xFE,0xE0,0xFE,0xF1,0xFE,0xF1}};

int
des_is_weak_key(des_cblock (*key))
{
	int i;

	for (i = 0; i < NUM_WEAK_KEY; i++) {
		/* Added == 0 to comparison, I obviously don't run
		 * this section very often :-(, thanks to
		 * engineering@MorningStar.Com for the fix
		 * eay 93/06/29 */
		if (bcmp(weak_keys[i], key, sizeof(des_cblock)) == 0)
			return (1);
	}
	return (0);
}

/* NOW DEFINED IN des_local.h
 * See ecb_encrypt.c for a pseudo description of these macros. 
 * #define PERM_OP(a, b, t, n, m) ((t) = ((((a) >> (n))^(b)) & (m)),\
 * 	(b)^=(t),\
 * 	(a) = ((a)^((t) << (n))))
 */

#define HPERM_OP(a, t, n, m) ((t) = ((((a) << (16 - (n)))^(a)) & (m)),\
	(a) = (a)^(t)^(t >> (16 - (n))))

static int shifts2[16]={0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0};

/* return 0 if key parity is odd (correct),
 * return -1 if key parity error,
 * return -2 if illegal weak key.
 */
int
des_set_key(des_cblock (*key), des_key_schedule schedule)
{
	register u_int32_t c, d, t, s;
	register unsigned char *in;
	register u_int32_t *k;
	register int i;

	if (des_check_key) {
		if (!check_parity(key))
			return(-1);

		if (des_is_weak_key(key))
			return(-2);
	}

	k = (u_int32_t *) schedule;
	in = (unsigned char *) key;

	c2l(in, c);
	c2l(in, d);

	/* do PC1 in 60 simple operations */ 
/*	PERM_OP(d, c, t, 4, 0x0f0f0f0fL);
	HPERM_OP(c, t, -2, 0xcccc0000L);
	HPERM_OP(c, t, -1, 0xaaaa0000L);
	HPERM_OP(c, t, 8, 0x00ff0000L);
	HPERM_OP(c, t, -1, 0xaaaa0000L);
	HPERM_OP(d, t, -8, 0xff000000L);
	HPERM_OP(d, t, 8, 0x00ff0000L);
	HPERM_OP(d, t, 2, 0x33330000L);
	d = ((d & 0x00aa00aaL) << 7L) | ((d & 0x55005500L) >> 7L) | (d & 0xaa55aa55L);
	d = (d >> 8) | ((c & 0xf0000000L) >> 4);
	c &= 0x0fffffffL; */

	/* I now do it in 47 simple operations :-)
	 * Thanks to John Fletcher (john_fletcher@lccmail.ocf.llnl.gov)
	 * for the inspiration. :-) */
	PERM_OP (d, c, t, 4, 0x0f0f0f0fL);
	HPERM_OP(c, t, -2, 0xcccc0000L);
	HPERM_OP(d, t, -2, 0xcccc0000L);
	PERM_OP (d, c, t, 1, 0x55555555L);
	PERM_OP (c, d, t, 8, 0x00ff00ffL);
	PERM_OP (d, c, t, 1, 0x55555555L);
	d = (((d & 0x000000ffL) << 16L) | (d & 0x0000ff00L) |
	     ((d & 0x00ff0000L) >> 16L) | ((c & 0xf0000000L) >> 4L));
	c &= 0x0fffffffL;

	for (i = 0; i < ITERATIONS; i++) {
		if (shifts2[i])
			{ c = ((c >> 2L) | (c << 26L)); d = ((d >> 2L) | (d << 26L)); }
		else
			{ c = ((c >> 1L) | (c << 27L)); d = ((d >> 1L) | (d << 27L)); }
		c &= 0x0fffffffL;
		d &= 0x0fffffffL;
		/* could be a few less shifts but I am to lazy at this
		 * point in time to investigate */
		s = des_skb[0][ (c    ) & 0x3f                ]|
		    des_skb[1][((c >> 6) & 0x03) | ((c >> 7L) & 0x3c)]|
		    des_skb[2][((c >> 13) & 0x0f) | ((c >> 14L) & 0x30)]|
		    des_skb[3][((c >> 20) & 0x01) | ((c >> 21L) & 0x06) |
						  ((c >> 22L) & 0x38)];
		t = des_skb[4][ (d    ) & 0x3f                ]|
		    des_skb[5][((d >> 7L) & 0x03) | ((d >> 8L) & 0x3c)]|
		    des_skb[6][ (d >> 15L) & 0x3f                ]|
		    des_skb[7][((d >> 21L) & 0x0f) | ((d >> 22L) & 0x30)];

		/* table contained 0213 4657 */
		*(k++) = ((t << 16L) | (s & 0x0000ffffL)) & 0xffffffffL;
		s = ((s >> 16L) | (t & 0xffff0000L));
		
		s = (s << 4L) | (s >> 28L);
		*(k++) = s & 0xffffffffL;
	}
	return (0);
}
