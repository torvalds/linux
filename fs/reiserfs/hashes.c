
/*
 * Keyed 32-bit hash function using TEA in a Davis-Meyer function
 *   H0 = Key
 *   Hi = E Mi(Hi-1) + Hi-1
 *
 * (see Applied Cryptography, 2nd edition, p448).
 *
 * Jeremy Fitzhardinge <jeremy@zip.com.au> 1998
 * 
 * Jeremy has agreed to the contents of reiserfs/README. -Hans
 * Yura's function is added (04/07/2000)
 */

//
// keyed_hash
// yura_hash
// r5_hash
//

#include <linux/kernel.h>
#include <linux/reiserfs_fs.h>
#include <asm/types.h>

#define DELTA 0x9E3779B9
#define FULLROUNDS 10		/* 32 is overkill, 16 is strong crypto */
#define PARTROUNDS 6		/* 6 gets complete mixing */

/* a, b, c, d - data; h0, h1 - accumulated hash */
#define TEACORE(rounds)							\
	do {								\
		u32 sum = 0;						\
		int n = rounds;						\
		u32 b0, b1;						\
									\
		b0 = h0;						\
		b1 = h1;						\
									\
		do							\
		{							\
			sum += DELTA;					\
			b0 += ((b1 << 4)+a) ^ (b1+sum) ^ ((b1 >> 5)+b);	\
			b1 += ((b0 << 4)+c) ^ (b0+sum) ^ ((b0 >> 5)+d);	\
		} while(--n);						\
									\
		h0 += b0;						\
		h1 += b1;						\
	} while(0)

u32 keyed_hash(const signed char *msg, int len)
{
	u32 k[] = { 0x9464a485, 0x542e1a94, 0x3e846bff, 0xb75bcfc3 };

	u32 h0 = k[0], h1 = k[1];
	u32 a, b, c, d;
	u32 pad;
	int i;

	//      assert(len >= 0 && len < 256);

	pad = (u32) len | ((u32) len << 8);
	pad |= pad << 16;

	while (len >= 16) {
		a = (u32) msg[0] |
		    (u32) msg[1] << 8 | (u32) msg[2] << 16 | (u32) msg[3] << 24;
		b = (u32) msg[4] |
		    (u32) msg[5] << 8 | (u32) msg[6] << 16 | (u32) msg[7] << 24;
		c = (u32) msg[8] |
		    (u32) msg[9] << 8 |
		    (u32) msg[10] << 16 | (u32) msg[11] << 24;
		d = (u32) msg[12] |
		    (u32) msg[13] << 8 |
		    (u32) msg[14] << 16 | (u32) msg[15] << 24;

		TEACORE(PARTROUNDS);

		len -= 16;
		msg += 16;
	}

	if (len >= 12) {
		a = (u32) msg[0] |
		    (u32) msg[1] << 8 | (u32) msg[2] << 16 | (u32) msg[3] << 24;
		b = (u32) msg[4] |
		    (u32) msg[5] << 8 | (u32) msg[6] << 16 | (u32) msg[7] << 24;
		c = (u32) msg[8] |
		    (u32) msg[9] << 8 |
		    (u32) msg[10] << 16 | (u32) msg[11] << 24;

		d = pad;
		for (i = 12; i < len; i++) {
			d <<= 8;
			d |= msg[i];
		}
	} else if (len >= 8) {
		a = (u32) msg[0] |
		    (u32) msg[1] << 8 | (u32) msg[2] << 16 | (u32) msg[3] << 24;
		b = (u32) msg[4] |
		    (u32) msg[5] << 8 | (u32) msg[6] << 16 | (u32) msg[7] << 24;

		c = d = pad;
		for (i = 8; i < len; i++) {
			c <<= 8;
			c |= msg[i];
		}
	} else if (len >= 4) {
		a = (u32) msg[0] |
		    (u32) msg[1] << 8 | (u32) msg[2] << 16 | (u32) msg[3] << 24;

		b = c = d = pad;
		for (i = 4; i < len; i++) {
			b <<= 8;
			b |= msg[i];
		}
	} else {
		a = b = c = d = pad;
		for (i = 0; i < len; i++) {
			a <<= 8;
			a |= msg[i];
		}
	}

	TEACORE(FULLROUNDS);

/*	return 0;*/
	return h0 ^ h1;
}

/* What follows in this file is copyright 2000 by Hans Reiser, and the
 * licensing of what follows is governed by reiserfs/README */

u32 yura_hash(const signed char *msg, int len)
{
	int j, pow;
	u32 a, c;
	int i;

	for (pow = 1, i = 1; i < len; i++)
		pow = pow * 10;

	if (len == 1)
		a = msg[0] - 48;
	else
		a = (msg[0] - 48) * pow;

	for (i = 1; i < len; i++) {
		c = msg[i] - 48;
		for (pow = 1, j = i; j < len - 1; j++)
			pow = pow * 10;
		a = a + c * pow;
	}

	for (; i < 40; i++) {
		c = '0' - 48;
		for (pow = 1, j = i; j < len - 1; j++)
			pow = pow * 10;
		a = a + c * pow;
	}

	for (; i < 256; i++) {
		c = i;
		for (pow = 1, j = i; j < len - 1; j++)
			pow = pow * 10;
		a = a + c * pow;
	}

	a = a << 7;
	return a;
}

u32 r5_hash(const signed char *msg, int len)
{
	u32 a = 0;
	while (*msg) {
		a += *msg << 4;
		a += *msg >> 4;
		a *= 11;
		msg++;
	}
	return a;
}
