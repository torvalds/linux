/* gf128mul.h - GF(2^128) multiplication functions
 *
 * Copyright (c) 2003, Dr Brian Gladman, Worcester, UK.
 * Copyright (c) 2006 Rik Snel <rsnel@cube.dyndns.org>
 *
 * Based on Dr Brian Gladman's (GPL'd) work published at
 * http://fp.gladman.plus.com/cryptography_technology/index.htm
 * See the original copyright notice below.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
/*
 ---------------------------------------------------------------------------
 Copyright (c) 2003, Dr Brian Gladman, Worcester, UK.   All rights reserved.

 LICENSE TERMS

 The free distribution and use of this software in both source and binary
 form is allowed (with or without changes) provided that:

   1. distributions of this source code include the above copyright
      notice, this list of conditions and the following disclaimer;

   2. distributions in binary form include the above copyright
      notice, this list of conditions and the following disclaimer
      in the documentation and/or other associated materials;

   3. the copyright holder's name is not used to endorse products
      built using this software without specific written permission.

 ALTERNATIVELY, provided that this notice is retained in full, this product
 may be distributed under the terms of the GNU General Public License (GPL),
 in which case the provisions of the GPL apply INSTEAD OF those given above.

 DISCLAIMER

 This software is provided 'as is' with no explicit or implied warranties
 in respect of its properties, including, but not limited to, correctness
 and/or fitness for purpose.
 ---------------------------------------------------------------------------
 Issue Date: 31/01/2006

 An implementation of field multiplication in Galois Field GF(2^128)
*/

#ifndef _CRYPTO_GF128MUL_H
#define _CRYPTO_GF128MUL_H

#include <crypto/b128ops.h>
#include <linux/slab.h>

/* Comment by Rik:
 *
 * For some background on GF(2^128) see for example: 
 * http://csrc.nist.gov/groups/ST/toolkit/BCM/documents/proposedmodes/gcm/gcm-revised-spec.pdf 
 *
 * The elements of GF(2^128) := GF(2)[X]/(X^128-X^7-X^2-X^1-1) can
 * be mapped to computer memory in a variety of ways. Let's examine
 * three common cases.
 *
 * Take a look at the 16 binary octets below in memory order. The msb's
 * are left and the lsb's are right. char b[16] is an array and b[0] is
 * the first octet.
 *
 * 10000000 00000000 00000000 00000000 .... 00000000 00000000 00000000
 *   b[0]     b[1]     b[2]     b[3]          b[13]    b[14]    b[15]
 *
 * Every bit is a coefficient of some power of X. We can store the bits
 * in every byte in little-endian order and the bytes themselves also in
 * little endian order. I will call this lle (little-little-endian).
 * The above buffer represents the polynomial 1, and X^7+X^2+X^1+1 looks
 * like 11100001 00000000 .... 00000000 = { 0xE1, 0x00, }.
 * This format was originally implemented in gf128mul and is used
 * in GCM (Galois/Counter mode) and in ABL (Arbitrary Block Length).
 *
 * Another convention says: store the bits in bigendian order and the
 * bytes also. This is bbe (big-big-endian). Now the buffer above
 * represents X^127. X^7+X^2+X^1+1 looks like 00000000 .... 10000111,
 * b[15] = 0x87 and the rest is 0. LRW uses this convention and bbe
 * is partly implemented.
 *
 * Both of the above formats are easy to implement on big-endian
 * machines.
 *
 * EME (which is patent encumbered) uses the ble format (bits are stored
 * in big endian order and the bytes in little endian). The above buffer
 * represents X^7 in this case and the primitive polynomial is b[0] = 0x87.
 *
 * The common machine word-size is smaller than 128 bits, so to make
 * an efficient implementation we must split into machine word sizes.
 * This file uses one 32bit for the moment. Machine endianness comes into
 * play. The lle format in relation to machine endianness is discussed
 * below by the original author of gf128mul Dr Brian Gladman.
 *
 * Let's look at the bbe and ble format on a little endian machine.
 *
 * bbe on a little endian machine u32 x[4]:
 *
 *  MS            x[0]           LS  MS            x[1]           LS
 *  ms   ls ms   ls ms   ls ms   ls  ms   ls ms   ls ms   ls ms   ls
 *  103..96 111.104 119.112 127.120  71...64 79...72 87...80 95...88
 *
 *  MS            x[2]           LS  MS            x[3]           LS
 *  ms   ls ms   ls ms   ls ms   ls  ms   ls ms   ls ms   ls ms   ls
 *  39...32 47...40 55...48 63...56  07...00 15...08 23...16 31...24
 *
 * ble on a little endian machine
 *
 *  MS            x[0]           LS  MS            x[1]           LS
 *  ms   ls ms   ls ms   ls ms   ls  ms   ls ms   ls ms   ls ms   ls
 *  31...24 23...16 15...08 07...00  63...56 55...48 47...40 39...32
 *
 *  MS            x[2]           LS  MS            x[3]           LS
 *  ms   ls ms   ls ms   ls ms   ls  ms   ls ms   ls ms   ls ms   ls
 *  95...88 87...80 79...72 71...64  127.120 199.112 111.104 103..96
 *
 * Multiplications in GF(2^128) are mostly bit-shifts, so you see why
 * ble (and lbe also) are easier to implement on a little-endian
 * machine than on a big-endian machine. The converse holds for bbe
 * and lle.
 *
 * Note: to have good alignment, it seems to me that it is sufficient
 * to keep elements of GF(2^128) in type u64[2]. On 32-bit wordsize
 * machines this will automatically aligned to wordsize and on a 64-bit
 * machine also.
 */
/*  Multiply a GF128 field element by x. Field elements are held in arrays
    of bytes in which field bits 8n..8n + 7 are held in byte[n], with lower
    indexed bits placed in the more numerically significant bit positions
    within bytes.

    On little endian machines the bit indexes translate into the bit
    positions within four 32-bit words in the following way

    MS            x[0]           LS  MS            x[1]           LS
    ms   ls ms   ls ms   ls ms   ls  ms   ls ms   ls ms   ls ms   ls
    24...31 16...23 08...15 00...07  56...63 48...55 40...47 32...39

    MS            x[2]           LS  MS            x[3]           LS
    ms   ls ms   ls ms   ls ms   ls  ms   ls ms   ls ms   ls ms   ls
    88...95 80...87 72...79 64...71  120.127 112.119 104.111 96..103

    On big endian machines the bit indexes translate into the bit
    positions within four 32-bit words in the following way

    MS            x[0]           LS  MS            x[1]           LS
    ms   ls ms   ls ms   ls ms   ls  ms   ls ms   ls ms   ls ms   ls
    00...07 08...15 16...23 24...31  32...39 40...47 48...55 56...63

    MS            x[2]           LS  MS            x[3]           LS
    ms   ls ms   ls ms   ls ms   ls  ms   ls ms   ls ms   ls ms   ls
    64...71 72...79 80...87 88...95  96..103 104.111 112.119 120.127
*/

/*  A slow generic version of gf_mul, implemented for lle, bbe, and ble.
 *  It multiplies a and b and puts the result in a
 */
void gf128mul_lle(be128 *a, const be128 *b);
void gf128mul_bbe(be128 *a, const be128 *b);
void gf128mul_ble(be128 *a, const be128 *b);

/* multiply by x in ble format, needed by XTS and HEH */
void gf128mul_x_ble(be128 *a, const be128 *b);

/* 4k table optimization */
struct gf128mul_4k {
	be128 t[256];
};

struct gf128mul_4k *gf128mul_init_4k_lle(const be128 *g);
struct gf128mul_4k *gf128mul_init_4k_bbe(const be128 *g);
struct gf128mul_4k *gf128mul_init_4k_ble(const be128 *g);
void gf128mul_4k_lle(be128 *a, struct gf128mul_4k *t);
void gf128mul_4k_bbe(be128 *a, struct gf128mul_4k *t);
void gf128mul_4k_ble(be128 *a, struct gf128mul_4k *t);

static inline void gf128mul_free_4k(struct gf128mul_4k *t)
{
	kzfree(t);
}


/* 64k table optimization, implemented for lle, ble, and bbe */

struct gf128mul_64k {
	struct gf128mul_4k *t[16];
};

/* First initialize with the constant factor with which you
 * want to multiply and then call gf128mul_64k_bbe with the other
 * factor in the first argument, and the table in the second.
 * Afterwards, the result is stored in *a.
 */
struct gf128mul_64k *gf128mul_init_64k_lle(const be128 *g);
struct gf128mul_64k *gf128mul_init_64k_bbe(const be128 *g);
void gf128mul_free_64k(struct gf128mul_64k *t);
void gf128mul_64k_lle(be128 *a, struct gf128mul_64k *t);
void gf128mul_64k_bbe(be128 *a, struct gf128mul_64k *t);

#endif /* _CRYPTO_GF128MUL_H */
