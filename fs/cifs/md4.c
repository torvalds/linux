/*
   Unix SMB/Netbios implementation.
   Version 1.9.
   a implementation of MD4 designed for use in the SMB authentication protocol
   Copyright (C) Andrew Tridgell 1997-1998.
   Modified by Steve French (sfrench@us.ibm.com) 2002-2003

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
#include <linux/module.h>
#include <linux/fs.h>
#include "cifsencrypt.h"

/* NOTE: This code makes no attempt to be fast! */

static __u32
F(__u32 X, __u32 Y, __u32 Z)
{
	return (X & Y) | ((~X) & Z);
}

static __u32
G(__u32 X, __u32 Y, __u32 Z)
{
	return (X & Y) | (X & Z) | (Y & Z);
}

static __u32
H(__u32 X, __u32 Y, __u32 Z)
{
	return X ^ Y ^ Z;
}

static __u32
lshift(__u32 x, int s)
{
	x &= 0xFFFFFFFF;
	return ((x << s) & 0xFFFFFFFF) | (x >> (32 - s));
}

#define ROUND1(a,b,c,d,k,s) (*a) = lshift((*a) + F(*b,*c,*d) + X[k], s)
#define ROUND2(a,b,c,d,k,s) (*a) = lshift((*a) + G(*b,*c,*d) + X[k] + (__u32)0x5A827999,s)
#define ROUND3(a,b,c,d,k,s) (*a) = lshift((*a) + H(*b,*c,*d) + X[k] + (__u32)0x6ED9EBA1,s)

/* this applies md4 to 64 byte chunks */
static void
mdfour64(__u32 *M, __u32 *A, __u32 *B, __u32 *C, __u32 *D)
{
	int j;
	__u32 AA, BB, CC, DD;
	__u32 X[16];


	for (j = 0; j < 16; j++)
		X[j] = M[j];

	AA = *A;
	BB = *B;
	CC = *C;
	DD = *D;

	ROUND1(A, B, C, D, 0, 3);
	ROUND1(D, A, B, C, 1, 7);
	ROUND1(C, D, A, B, 2, 11);
	ROUND1(B, C, D, A, 3, 19);
	ROUND1(A, B, C, D, 4, 3);
	ROUND1(D, A, B, C, 5, 7);
	ROUND1(C, D, A, B, 6, 11);
	ROUND1(B, C, D, A, 7, 19);
	ROUND1(A, B, C, D, 8, 3);
	ROUND1(D, A, B, C, 9, 7);
	ROUND1(C, D, A, B, 10, 11);
	ROUND1(B, C, D, A, 11, 19);
	ROUND1(A, B, C, D, 12, 3);
	ROUND1(D, A, B, C, 13, 7);
	ROUND1(C, D, A, B, 14, 11);
	ROUND1(B, C, D, A, 15, 19);

	ROUND2(A, B, C, D, 0, 3);
	ROUND2(D, A, B, C, 4, 5);
	ROUND2(C, D, A, B, 8, 9);
	ROUND2(B, C, D, A, 12, 13);
	ROUND2(A, B, C, D, 1, 3);
	ROUND2(D, A, B, C, 5, 5);
	ROUND2(C, D, A, B, 9, 9);
	ROUND2(B, C, D, A, 13, 13);
	ROUND2(A, B, C, D, 2, 3);
	ROUND2(D, A, B, C, 6, 5);
	ROUND2(C, D, A, B, 10, 9);
	ROUND2(B, C, D, A, 14, 13);
	ROUND2(A, B, C, D, 3, 3);
	ROUND2(D, A, B, C, 7, 5);
	ROUND2(C, D, A, B, 11, 9);
	ROUND2(B, C, D, A, 15, 13);

	ROUND3(A, B, C, D, 0, 3);
	ROUND3(D, A, B, C, 8, 9);
	ROUND3(C, D, A, B, 4, 11);
	ROUND3(B, C, D, A, 12, 15);
	ROUND3(A, B, C, D, 2, 3);
	ROUND3(D, A, B, C, 10, 9);
	ROUND3(C, D, A, B, 6, 11);
	ROUND3(B, C, D, A, 14, 15);
	ROUND3(A, B, C, D, 1, 3);
	ROUND3(D, A, B, C, 9, 9);
	ROUND3(C, D, A, B, 5, 11);
	ROUND3(B, C, D, A, 13, 15);
	ROUND3(A, B, C, D, 3, 3);
	ROUND3(D, A, B, C, 11, 9);
	ROUND3(C, D, A, B, 7, 11);
	ROUND3(B, C, D, A, 15, 15);

	*A += AA;
	*B += BB;
	*C += CC;
	*D += DD;

	*A &= 0xFFFFFFFF;
	*B &= 0xFFFFFFFF;
	*C &= 0xFFFFFFFF;
	*D &= 0xFFFFFFFF;

	for (j = 0; j < 16; j++)
		X[j] = 0;
}

static void
copy64(__u32 *M, unsigned char *in)
{
	int i;

	for (i = 0; i < 16; i++)
		M[i] = (in[i * 4 + 3] << 24) | (in[i * 4 + 2] << 16) |
		    (in[i * 4 + 1] << 8) | (in[i * 4 + 0] << 0);
}

static void
copy4(unsigned char *out, __u32 x)
{
	out[0] = x & 0xFF;
	out[1] = (x >> 8) & 0xFF;
	out[2] = (x >> 16) & 0xFF;
	out[3] = (x >> 24) & 0xFF;
}

/* produce a md4 message digest from data of length n bytes */
void
mdfour(unsigned char *out, unsigned char *in, int n)
{
	unsigned char buf[128];
	__u32 M[16];
	__u32 b = n * 8;
	int i;
	__u32 A = 0x67452301;
	__u32 B = 0xefcdab89;
	__u32 C = 0x98badcfe;
	__u32 D = 0x10325476;

	while (n > 64) {
		copy64(M, in);
		mdfour64(M, &A, &B, &C, &D);
		in += 64;
		n -= 64;
	}

	for (i = 0; i < 128; i++)
		buf[i] = 0;
	memcpy(buf, in, n);
	buf[n] = 0x80;

	if (n <= 55) {
		copy4(buf + 56, b);
		copy64(M, buf);
		mdfour64(M, &A, &B, &C, &D);
	} else {
		copy4(buf + 120, b);
		copy64(M, buf);
		mdfour64(M, &A, &B, &C, &D);
		copy64(M, buf + 64);
		mdfour64(M, &A, &B, &C, &D);
	}

	for (i = 0; i < 128; i++)
		buf[i] = 0;
	copy64(M, buf);

	copy4(out, A);
	copy4(out + 4, B);
	copy4(out + 8, C);
	copy4(out + 12, D);

	A = B = C = D = 0;
}
