/*	$OpenBSD: skipjack.c,v 1.3 2001/05/05 00:31:34 angelos Exp $	*/
/*-
 * Further optimized test implementation of SKIPJACK algorithm 
 * Mark Tillotson <markt@chaos.org.uk>, 25 June 98
 * Optimizations suit RISC (lots of registers) machine best.
 *
 * based on unoptimized implementation of
 * Panu Rissanen <bande@lut.fi> 960624
 *
 * SKIPJACK and KEA Algorithm Specifications 
 * Version 2.0 
 * 29 May 1998
*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <opencrypto/skipjack.h>

static const u_int8_t ftable[0x100] =
{ 
	0xa3, 0xd7, 0x09, 0x83, 0xf8, 0x48, 0xf6, 0xf4, 
	0xb3, 0x21, 0x15, 0x78, 0x99, 0xb1, 0xaf, 0xf9, 
	0xe7, 0x2d, 0x4d, 0x8a, 0xce, 0x4c, 0xca, 0x2e, 
	0x52, 0x95, 0xd9, 0x1e, 0x4e, 0x38, 0x44, 0x28, 
	0x0a, 0xdf, 0x02, 0xa0, 0x17, 0xf1, 0x60, 0x68, 
	0x12, 0xb7, 0x7a, 0xc3, 0xe9, 0xfa, 0x3d, 0x53, 
	0x96, 0x84, 0x6b, 0xba, 0xf2, 0x63, 0x9a, 0x19, 
	0x7c, 0xae, 0xe5, 0xf5, 0xf7, 0x16, 0x6a, 0xa2, 
	0x39, 0xb6, 0x7b, 0x0f, 0xc1, 0x93, 0x81, 0x1b, 
	0xee, 0xb4, 0x1a, 0xea, 0xd0, 0x91, 0x2f, 0xb8, 
	0x55, 0xb9, 0xda, 0x85, 0x3f, 0x41, 0xbf, 0xe0, 
	0x5a, 0x58, 0x80, 0x5f, 0x66, 0x0b, 0xd8, 0x90, 
	0x35, 0xd5, 0xc0, 0xa7, 0x33, 0x06, 0x65, 0x69, 
	0x45, 0x00, 0x94, 0x56, 0x6d, 0x98, 0x9b, 0x76, 
	0x97, 0xfc, 0xb2, 0xc2, 0xb0, 0xfe, 0xdb, 0x20, 
	0xe1, 0xeb, 0xd6, 0xe4, 0xdd, 0x47, 0x4a, 0x1d, 
	0x42, 0xed, 0x9e, 0x6e, 0x49, 0x3c, 0xcd, 0x43, 
	0x27, 0xd2, 0x07, 0xd4, 0xde, 0xc7, 0x67, 0x18, 
	0x89, 0xcb, 0x30, 0x1f, 0x8d, 0xc6, 0x8f, 0xaa, 
	0xc8, 0x74, 0xdc, 0xc9, 0x5d, 0x5c, 0x31, 0xa4, 
	0x70, 0x88, 0x61, 0x2c, 0x9f, 0x0d, 0x2b, 0x87, 
	0x50, 0x82, 0x54, 0x64, 0x26, 0x7d, 0x03, 0x40, 
	0x34, 0x4b, 0x1c, 0x73, 0xd1, 0xc4, 0xfd, 0x3b, 
	0xcc, 0xfb, 0x7f, 0xab, 0xe6, 0x3e, 0x5b, 0xa5, 
	0xad, 0x04, 0x23, 0x9c, 0x14, 0x51, 0x22, 0xf0, 
	0x29, 0x79, 0x71, 0x7e, 0xff, 0x8c, 0x0e, 0xe2, 
	0x0c, 0xef, 0xbc, 0x72, 0x75, 0x6f, 0x37, 0xa1, 
	0xec, 0xd3, 0x8e, 0x62, 0x8b, 0x86, 0x10, 0xe8, 
	0x08, 0x77, 0x11, 0xbe, 0x92, 0x4f, 0x24, 0xc5, 
	0x32, 0x36, 0x9d, 0xcf, 0xf3, 0xa6, 0xbb, 0xac, 
	0x5e, 0x6c, 0xa9, 0x13, 0x57, 0x25, 0xb5, 0xe3, 
	0xbd, 0xa8, 0x3a, 0x01, 0x05, 0x59, 0x2a, 0x46
};

/*
 * For each key byte generate a table to represent the function 
 *    ftable [in ^ keybyte]
 *
 * These tables used to save an XOR in each stage of the G-function
 * the tables are hopefully pointed to by register allocated variables
 * k0, k1..k9
 */

void
subkey_table_gen (u_int8_t *key, u_int8_t **key_tables)
{
	int i, k;

	for (k = 0; k < 10; k++) {
		u_int8_t   key_byte = key [k];
		u_int8_t * table = key_tables[k];
		for (i = 0; i < 0x100; i++)
			table [i] = ftable [i ^ key_byte];
	}
}


#define g(k0, k1, k2, k3, ih, il, oh, ol) \
{ \
	oh = k##k0 [il] ^ ih; \
	ol = k##k1 [oh] ^ il; \
	oh = k##k2 [ol] ^ oh; \
	ol = k##k3 [oh] ^ ol; \
}

#define g0(ih, il, oh, ol) g(0, 1, 2, 3, ih, il, oh, ol)
#define g4(ih, il, oh, ol) g(4, 5, 6, 7, ih, il, oh, ol)
#define g8(ih, il, oh, ol) g(8, 9, 0, 1, ih, il, oh, ol)
#define g2(ih, il, oh, ol) g(2, 3, 4, 5, ih, il, oh, ol)
#define g6(ih, il, oh, ol) g(6, 7, 8, 9, ih, il, oh, ol)

 
#define g_inv(k0, k1, k2, k3, ih, il, oh, ol) \
{ \
	ol = k##k3 [ih] ^ il; \
	oh = k##k2 [ol] ^ ih; \
	ol = k##k1 [oh] ^ ol; \
	oh = k##k0 [ol] ^ oh; \
}


#define g0_inv(ih, il, oh, ol) g_inv(0, 1, 2, 3, ih, il, oh, ol)
#define g4_inv(ih, il, oh, ol) g_inv(4, 5, 6, 7, ih, il, oh, ol)
#define g8_inv(ih, il, oh, ol) g_inv(8, 9, 0, 1, ih, il, oh, ol)
#define g2_inv(ih, il, oh, ol) g_inv(2, 3, 4, 5, ih, il, oh, ol)
#define g6_inv(ih, il, oh, ol) g_inv(6, 7, 8, 9, ih, il, oh, ol)

/* optimized version of Skipjack algorithm
 *
 * the appropriate g-function is inlined for each round
 *
 * the data movement is minimized by rotating the names of the 
 * variables w1..w4, not their contents (saves 3 moves per round)
 *
 * the loops are completely unrolled (needed to staticize choice of g)
 *
 * compiles to about 470 instructions on a Sparc (gcc -O)
 * which is about 58 instructions per byte, 14 per round.
 * gcc seems to leave in some unnecessary and with 0xFF operations
 * but only in the latter part of the functions.  Perhaps it
 * runs out of resources to properly optimize long inlined function?
 * in theory should get about 11 instructions per round, not 14
 */

void
skipjack_forwards(u_int8_t *plain, u_int8_t *cipher, u_int8_t **key_tables)
{
	u_int8_t wh1 = plain[0];  u_int8_t wl1 = plain[1];
	u_int8_t wh2 = plain[2];  u_int8_t wl2 = plain[3];
	u_int8_t wh3 = plain[4];  u_int8_t wl3 = plain[5];
	u_int8_t wh4 = plain[6];  u_int8_t wl4 = plain[7];

	u_int8_t * k0 = key_tables [0];
	u_int8_t * k1 = key_tables [1];
	u_int8_t * k2 = key_tables [2];
	u_int8_t * k3 = key_tables [3];
	u_int8_t * k4 = key_tables [4];
	u_int8_t * k5 = key_tables [5];
	u_int8_t * k6 = key_tables [6];
	u_int8_t * k7 = key_tables [7];
	u_int8_t * k8 = key_tables [8];
	u_int8_t * k9 = key_tables [9];

	/* first 8 rounds */
	g0 (wh1,wl1, wh1,wl1); wl4 ^= wl1 ^ 1; wh4 ^= wh1;
	g4 (wh4,wl4, wh4,wl4); wl3 ^= wl4 ^ 2; wh3 ^= wh4;
	g8 (wh3,wl3, wh3,wl3); wl2 ^= wl3 ^ 3; wh2 ^= wh3;
	g2 (wh2,wl2, wh2,wl2); wl1 ^= wl2 ^ 4; wh1 ^= wh2;
	g6 (wh1,wl1, wh1,wl1); wl4 ^= wl1 ^ 5; wh4 ^= wh1;
	g0 (wh4,wl4, wh4,wl4); wl3 ^= wl4 ^ 6; wh3 ^= wh4;
	g4 (wh3,wl3, wh3,wl3); wl2 ^= wl3 ^ 7; wh2 ^= wh3;
	g8 (wh2,wl2, wh2,wl2); wl1 ^= wl2 ^ 8; wh1 ^= wh2;

	/* second 8 rounds */
	wh2 ^= wh1; wl2 ^= wl1 ^ 9 ; g2 (wh1,wl1, wh1,wl1);
	wh1 ^= wh4; wl1 ^= wl4 ^ 10; g6 (wh4,wl4, wh4,wl4);
	wh4 ^= wh3; wl4 ^= wl3 ^ 11; g0 (wh3,wl3, wh3,wl3);
	wh3 ^= wh2; wl3 ^= wl2 ^ 12; g4 (wh2,wl2, wh2,wl2);
	wh2 ^= wh1; wl2 ^= wl1 ^ 13; g8 (wh1,wl1, wh1,wl1);
	wh1 ^= wh4; wl1 ^= wl4 ^ 14; g2 (wh4,wl4, wh4,wl4);
	wh4 ^= wh3; wl4 ^= wl3 ^ 15; g6 (wh3,wl3, wh3,wl3);
	wh3 ^= wh2; wl3 ^= wl2 ^ 16; g0 (wh2,wl2, wh2,wl2);

	/* third 8 rounds */
	g4 (wh1,wl1, wh1,wl1); wl4 ^= wl1 ^ 17; wh4 ^= wh1;
	g8 (wh4,wl4, wh4,wl4); wl3 ^= wl4 ^ 18; wh3 ^= wh4;
	g2 (wh3,wl3, wh3,wl3); wl2 ^= wl3 ^ 19; wh2 ^= wh3;
	g6 (wh2,wl2, wh2,wl2); wl1 ^= wl2 ^ 20; wh1 ^= wh2;
	g0 (wh1,wl1, wh1,wl1); wl4 ^= wl1 ^ 21; wh4 ^= wh1;
	g4 (wh4,wl4, wh4,wl4); wl3 ^= wl4 ^ 22; wh3 ^= wh4;
	g8 (wh3,wl3, wh3,wl3); wl2 ^= wl3 ^ 23; wh2 ^= wh3;
	g2 (wh2,wl2, wh2,wl2); wl1 ^= wl2 ^ 24; wh1 ^= wh2;

	/* last 8 rounds */
	wh2 ^= wh1; wl2 ^= wl1 ^ 25; g6 (wh1,wl1, wh1,wl1);
	wh1 ^= wh4; wl1 ^= wl4 ^ 26; g0 (wh4,wl4, wh4,wl4);
	wh4 ^= wh3; wl4 ^= wl3 ^ 27; g4 (wh3,wl3, wh3,wl3);
	wh3 ^= wh2; wl3 ^= wl2 ^ 28; g8 (wh2,wl2, wh2,wl2);
	wh2 ^= wh1; wl2 ^= wl1 ^ 29; g2 (wh1,wl1, wh1,wl1);
	wh1 ^= wh4; wl1 ^= wl4 ^ 30; g6 (wh4,wl4, wh4,wl4);
	wh4 ^= wh3; wl4 ^= wl3 ^ 31; g0 (wh3,wl3, wh3,wl3);
	wh3 ^= wh2; wl3 ^= wl2 ^ 32; g4 (wh2,wl2, wh2,wl2);

	/* pack into byte vector */
	cipher [0] = wh1;  cipher [1] = wl1;
	cipher [2] = wh2;  cipher [3] = wl2;
	cipher [4] = wh3;  cipher [5] = wl3;
	cipher [6] = wh4;  cipher [7] = wl4;
}


void
skipjack_backwards (u_int8_t *cipher, u_int8_t *plain, u_int8_t **key_tables)
{
	/* setup 4 16-bit portions */
	u_int8_t wh1 = cipher[0];  u_int8_t wl1 = cipher[1];
	u_int8_t wh2 = cipher[2];  u_int8_t wl2 = cipher[3];
	u_int8_t wh3 = cipher[4];  u_int8_t wl3 = cipher[5];
	u_int8_t wh4 = cipher[6];  u_int8_t wl4 = cipher[7];

	u_int8_t * k0 = key_tables [0];
	u_int8_t * k1 = key_tables [1];
	u_int8_t * k2 = key_tables [2];
	u_int8_t * k3 = key_tables [3];
	u_int8_t * k4 = key_tables [4];
	u_int8_t * k5 = key_tables [5];
	u_int8_t * k6 = key_tables [6];
	u_int8_t * k7 = key_tables [7];
	u_int8_t * k8 = key_tables [8];
	u_int8_t * k9 = key_tables [9];

	/* first 8 rounds */
	g4_inv (wh2,wl2, wh2,wl2); wl3 ^= wl2 ^ 32; wh3 ^= wh2;
	g0_inv (wh3,wl3, wh3,wl3); wl4 ^= wl3 ^ 31; wh4 ^= wh3;
	g6_inv (wh4,wl4, wh4,wl4); wl1 ^= wl4 ^ 30; wh1 ^= wh4;
	g2_inv (wh1,wl1, wh1,wl1); wl2 ^= wl1 ^ 29; wh2 ^= wh1;
	g8_inv (wh2,wl2, wh2,wl2); wl3 ^= wl2 ^ 28; wh3 ^= wh2;
	g4_inv (wh3,wl3, wh3,wl3); wl4 ^= wl3 ^ 27; wh4 ^= wh3;
	g0_inv (wh4,wl4, wh4,wl4); wl1 ^= wl4 ^ 26; wh1 ^= wh4;
	g6_inv (wh1,wl1, wh1,wl1); wl2 ^= wl1 ^ 25; wh2 ^= wh1;

	/* second 8 rounds */
	wh1 ^= wh2; wl1 ^= wl2 ^ 24; g2_inv (wh2,wl2, wh2,wl2);
	wh2 ^= wh3; wl2 ^= wl3 ^ 23; g8_inv (wh3,wl3, wh3,wl3);
	wh3 ^= wh4; wl3 ^= wl4 ^ 22; g4_inv (wh4,wl4, wh4,wl4);
	wh4 ^= wh1; wl4 ^= wl1 ^ 21; g0_inv (wh1,wl1, wh1,wl1);
	wh1 ^= wh2; wl1 ^= wl2 ^ 20; g6_inv (wh2,wl2, wh2,wl2);
	wh2 ^= wh3; wl2 ^= wl3 ^ 19; g2_inv (wh3,wl3, wh3,wl3);
	wh3 ^= wh4; wl3 ^= wl4 ^ 18; g8_inv (wh4,wl4, wh4,wl4);
	wh4 ^= wh1; wl4 ^= wl1 ^ 17; g4_inv (wh1,wl1, wh1,wl1);

	/* third 8 rounds */
	g0_inv (wh2,wl2, wh2,wl2); wl3 ^= wl2 ^ 16; wh3 ^= wh2;
	g6_inv (wh3,wl3, wh3,wl3); wl4 ^= wl3 ^ 15; wh4 ^= wh3;
	g2_inv (wh4,wl4, wh4,wl4); wl1 ^= wl4 ^ 14; wh1 ^= wh4;
	g8_inv (wh1,wl1, wh1,wl1); wl2 ^= wl1 ^ 13; wh2 ^= wh1;
	g4_inv (wh2,wl2, wh2,wl2); wl3 ^= wl2 ^ 12; wh3 ^= wh2;
	g0_inv (wh3,wl3, wh3,wl3); wl4 ^= wl3 ^ 11; wh4 ^= wh3;
	g6_inv (wh4,wl4, wh4,wl4); wl1 ^= wl4 ^ 10; wh1 ^= wh4;
	g2_inv (wh1,wl1, wh1,wl1); wl2 ^= wl1 ^ 9;  wh2 ^= wh1;

	/* last 8 rounds */
	wh1 ^= wh2; wl1 ^= wl2 ^ 8; g8_inv (wh2,wl2, wh2,wl2);
	wh2 ^= wh3; wl2 ^= wl3 ^ 7; g4_inv (wh3,wl3, wh3,wl3);
	wh3 ^= wh4; wl3 ^= wl4 ^ 6; g0_inv (wh4,wl4, wh4,wl4);
	wh4 ^= wh1; wl4 ^= wl1 ^ 5; g6_inv (wh1,wl1, wh1,wl1);
	wh1 ^= wh2; wl1 ^= wl2 ^ 4; g2_inv (wh2,wl2, wh2,wl2);
	wh2 ^= wh3; wl2 ^= wl3 ^ 3; g8_inv (wh3,wl3, wh3,wl3);
	wh3 ^= wh4; wl3 ^= wl4 ^ 2; g4_inv (wh4,wl4, wh4,wl4);
	wh4 ^= wh1; wl4 ^= wl1 ^ 1; g0_inv (wh1,wl1, wh1,wl1);

	/* pack into byte vector */
	plain [0] = wh1;  plain [1] = wl1;
	plain [2] = wh2;  plain [3] = wl2;
	plain [4] = wh3;  plain [5] = wl3;
	plain [6] = wh4;  plain [7] = wl4;
}
