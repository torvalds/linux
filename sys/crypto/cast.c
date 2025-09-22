/*      $OpenBSD: cast.c,v 1.4 2012/04/25 04:12:27 matthew Exp $       */

/*
 *	CAST-128 in C
 *	Written by Steve Reid <sreid@sea-to-sky.net>
 *	100% Public Domain - no warranty
 *	Released 1997.10.11
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <crypto/cast.h>
#include <crypto/castsb.h>

/* Macros to access 8-bit bytes out of a 32-bit word */
#define U_INT8_Ta(x) ( (u_int8_t) (x>>24) )
#define U_INT8_Tb(x) ( (u_int8_t) ((x>>16)&255) )
#define U_INT8_Tc(x) ( (u_int8_t) ((x>>8)&255) )
#define U_INT8_Td(x) ( (u_int8_t) ((x)&255) )

/* Circular left shift */
#define ROL(x, n) ( ((x)<<(n)) | ((x)>>(32-(n))) )

/* CAST-128 uses three different round functions */
#define F1(l, r, i) \
	t = ROL(key->xkey[i] + r, key->xkey[i+16]); \
	l ^= ((cast_sbox1[U_INT8_Ta(t)] ^ cast_sbox2[U_INT8_Tb(t)]) - \
	 cast_sbox3[U_INT8_Tc(t)]) + cast_sbox4[U_INT8_Td(t)];
#define F2(l, r, i) \
	t = ROL(key->xkey[i] ^ r, key->xkey[i+16]); \
	l ^= ((cast_sbox1[U_INT8_Ta(t)] - cast_sbox2[U_INT8_Tb(t)]) + \
	 cast_sbox3[U_INT8_Tc(t)]) ^ cast_sbox4[U_INT8_Td(t)];
#define F3(l, r, i) \
	t = ROL(key->xkey[i] - r, key->xkey[i+16]); \
	l ^= ((cast_sbox1[U_INT8_Ta(t)] + cast_sbox2[U_INT8_Tb(t)]) ^ \
	 cast_sbox3[U_INT8_Tc(t)]) - cast_sbox4[U_INT8_Td(t)];


/***** Encryption Function *****/

void
cast_encrypt(cast_key *key, u_int8_t *inblock, u_int8_t *outblock)
{
	u_int32_t t, l, r;

	/* Get inblock into l,r */
	l = ((u_int32_t)inblock[0] << 24) | ((u_int32_t)inblock[1] << 16) |
	    ((u_int32_t)inblock[2] << 8) | (u_int32_t)inblock[3];
	r = ((u_int32_t)inblock[4] << 24) | ((u_int32_t)inblock[5] << 16) |
	    ((u_int32_t)inblock[6] << 8) | (u_int32_t)inblock[7];
	/* Do the work */
	F1(l, r,  0);
	F2(r, l,  1);
	F3(l, r,  2);
	F1(r, l,  3);
	F2(l, r,  4);
	F3(r, l,  5);
	F1(l, r,  6);
	F2(r, l,  7);
	F3(l, r,  8);
	F1(r, l,  9);
	F2(l, r, 10);
	F3(r, l, 11);
	/* Only do full 16 rounds if key length > 80 bits */
	if (key->rounds > 12) {
		F1(l, r, 12);
		F2(r, l, 13);
		F3(l, r, 14);
		F1(r, l, 15);
	}
	/* Put l,r into outblock */
	outblock[0] = U_INT8_Ta(r);
	outblock[1] = U_INT8_Tb(r);
	outblock[2] = U_INT8_Tc(r);
	outblock[3] = U_INT8_Td(r);
	outblock[4] = U_INT8_Ta(l);
	outblock[5] = U_INT8_Tb(l);
	outblock[6] = U_INT8_Tc(l);
	outblock[7] = U_INT8_Td(l);
	/* Wipe clean */
	t = l = r = 0;
}


/***** Decryption Function *****/

void
cast_decrypt(cast_key *key, u_int8_t *inblock, u_int8_t *outblock)
{
	u_int32_t t, l, r;

	/* Get inblock into l,r */
	r = ((u_int32_t)inblock[0] << 24) | ((u_int32_t)inblock[1] << 16) |
	    ((u_int32_t)inblock[2] << 8) | (u_int32_t)inblock[3];
	l = ((u_int32_t)inblock[4] << 24) | ((u_int32_t)inblock[5] << 16) |
	    ((u_int32_t)inblock[6] << 8) | (u_int32_t)inblock[7];
	/* Do the work */
	/* Only do full 16 rounds if key length > 80 bits */
	if (key->rounds > 12) {
		F1(r, l, 15);
		F3(l, r, 14);
		F2(r, l, 13);
		F1(l, r, 12);
	}
	F3(r, l, 11);
	F2(l, r, 10);
	F1(r, l,  9);
	F3(l, r,  8);
	F2(r, l,  7);
	F1(l, r,  6);
	F3(r, l,  5);
	F2(l, r,  4);
	F1(r, l,  3);
	F3(l, r,  2);
	F2(r, l,  1);
	F1(l, r,  0);
	/* Put l,r into outblock */
	outblock[0] = U_INT8_Ta(l);
	outblock[1] = U_INT8_Tb(l);
	outblock[2] = U_INT8_Tc(l);
	outblock[3] = U_INT8_Td(l);
	outblock[4] = U_INT8_Ta(r);
	outblock[5] = U_INT8_Tb(r);
	outblock[6] = U_INT8_Tc(r);
	outblock[7] = U_INT8_Td(r);
	/* Wipe clean */
	t = l = r = 0;
}


/***** Key Schedule *****/

void
cast_setkey(cast_key *key, u_int8_t *rawkey, int keybytes)
{
	u_int32_t t[4], z[4], x[4];
	int i;

	/* Set number of rounds to 12 or 16, depending on key length */
	key->rounds = (keybytes <= 10 ? 12 : 16);

	/* Copy key to workspace x */
	for (i = 0; i < 4; i++) {
		x[i] = 0;
		if ((i*4+0) < keybytes) x[i] = (u_int32_t)rawkey[i*4+0] << 24;
		if ((i*4+1) < keybytes) x[i] |= (u_int32_t)rawkey[i*4+1] << 16;
		if ((i*4+2) < keybytes) x[i] |= (u_int32_t)rawkey[i*4+2] << 8;
		if ((i*4+3) < keybytes) x[i] |= (u_int32_t)rawkey[i*4+3];
	}
	/* Generate 32 subkeys, four at a time */
	for (i = 0; i < 32; i+=4) {
		switch (i & 4) {
		case 0:
			t[0] = z[0] = x[0] ^ cast_sbox5[U_INT8_Tb(x[3])] ^
			    cast_sbox6[U_INT8_Td(x[3])] ^
			    cast_sbox7[U_INT8_Ta(x[3])] ^
			    cast_sbox8[U_INT8_Tc(x[3])] ^
			    cast_sbox7[U_INT8_Ta(x[2])];
			t[1] = z[1] = x[2] ^ cast_sbox5[U_INT8_Ta(z[0])] ^
			    cast_sbox6[U_INT8_Tc(z[0])] ^
			    cast_sbox7[U_INT8_Tb(z[0])] ^
			    cast_sbox8[U_INT8_Td(z[0])] ^
			    cast_sbox8[U_INT8_Tc(x[2])];
			t[2] = z[2] = x[3] ^ cast_sbox5[U_INT8_Td(z[1])] ^
			    cast_sbox6[U_INT8_Tc(z[1])] ^
			    cast_sbox7[U_INT8_Tb(z[1])] ^
			    cast_sbox8[U_INT8_Ta(z[1])] ^
			    cast_sbox5[U_INT8_Tb(x[2])];
			t[3] = z[3] = x[1] ^ cast_sbox5[U_INT8_Tc(z[2])] ^
			    cast_sbox6[U_INT8_Tb(z[2])] ^
			    cast_sbox7[U_INT8_Td(z[2])] ^
			    cast_sbox8[U_INT8_Ta(z[2])] ^
			    cast_sbox6[U_INT8_Td(x[2])];
			break;
		 case 4:
			t[0] = x[0] = z[2] ^ cast_sbox5[U_INT8_Tb(z[1])] ^
			    cast_sbox6[U_INT8_Td(z[1])] ^
			    cast_sbox7[U_INT8_Ta(z[1])] ^
			    cast_sbox8[U_INT8_Tc(z[1])] ^
			    cast_sbox7[U_INT8_Ta(z[0])];
			t[1] = x[1] = z[0] ^ cast_sbox5[U_INT8_Ta(x[0])] ^
			    cast_sbox6[U_INT8_Tc(x[0])] ^
			    cast_sbox7[U_INT8_Tb(x[0])] ^
			    cast_sbox8[U_INT8_Td(x[0])] ^
			    cast_sbox8[U_INT8_Tc(z[0])];
			t[2] = x[2] = z[1] ^ cast_sbox5[U_INT8_Td(x[1])] ^
			    cast_sbox6[U_INT8_Tc(x[1])] ^
			    cast_sbox7[U_INT8_Tb(x[1])] ^
			    cast_sbox8[U_INT8_Ta(x[1])] ^
			    cast_sbox5[U_INT8_Tb(z[0])];
			t[3] = x[3] = z[3] ^ cast_sbox5[U_INT8_Tc(x[2])] ^
			    cast_sbox6[U_INT8_Tb(x[2])] ^
			    cast_sbox7[U_INT8_Td(x[2])] ^
			    cast_sbox8[U_INT8_Ta(x[2])] ^
			    cast_sbox6[U_INT8_Td(z[0])];
			break;
		}
		switch (i & 12) {
		case 0:
		case 12:
			key->xkey[i+0] = cast_sbox5[U_INT8_Ta(t[2])] ^
			    cast_sbox6[U_INT8_Tb(t[2])] ^
			    cast_sbox7[U_INT8_Td(t[1])] ^
			    cast_sbox8[U_INT8_Tc(t[1])];
			key->xkey[i+1] = cast_sbox5[U_INT8_Tc(t[2])] ^
			    cast_sbox6[U_INT8_Td(t[2])] ^
			    cast_sbox7[U_INT8_Tb(t[1])] ^
			    cast_sbox8[U_INT8_Ta(t[1])];
			key->xkey[i+2] = cast_sbox5[U_INT8_Ta(t[3])] ^
			    cast_sbox6[U_INT8_Tb(t[3])] ^
			    cast_sbox7[U_INT8_Td(t[0])] ^
			    cast_sbox8[U_INT8_Tc(t[0])];
			key->xkey[i+3] = cast_sbox5[U_INT8_Tc(t[3])] ^
			    cast_sbox6[U_INT8_Td(t[3])] ^
			    cast_sbox7[U_INT8_Tb(t[0])] ^
			    cast_sbox8[U_INT8_Ta(t[0])];
			break;
		case 4:
		case 8:
			key->xkey[i+0] = cast_sbox5[U_INT8_Td(t[0])] ^
			    cast_sbox6[U_INT8_Tc(t[0])] ^
			    cast_sbox7[U_INT8_Ta(t[3])] ^
			    cast_sbox8[U_INT8_Tb(t[3])];
			key->xkey[i+1] = cast_sbox5[U_INT8_Tb(t[0])] ^
			    cast_sbox6[U_INT8_Ta(t[0])] ^
			    cast_sbox7[U_INT8_Tc(t[3])] ^
			    cast_sbox8[U_INT8_Td(t[3])];
			key->xkey[i+2] = cast_sbox5[U_INT8_Td(t[1])] ^
			    cast_sbox6[U_INT8_Tc(t[1])] ^
			    cast_sbox7[U_INT8_Ta(t[2])] ^
			    cast_sbox8[U_INT8_Tb(t[2])];
			key->xkey[i+3] = cast_sbox5[U_INT8_Tb(t[1])] ^
			    cast_sbox6[U_INT8_Ta(t[1])] ^
			    cast_sbox7[U_INT8_Tc(t[2])] ^
			    cast_sbox8[U_INT8_Td(t[2])];
			break;
		}
		switch (i & 12) {
		case 0:
			key->xkey[i+0] ^= cast_sbox5[U_INT8_Tc(z[0])];
			key->xkey[i+1] ^= cast_sbox6[U_INT8_Tc(z[1])];
			key->xkey[i+2] ^= cast_sbox7[U_INT8_Tb(z[2])];
			key->xkey[i+3] ^= cast_sbox8[U_INT8_Ta(z[3])];
			break;
		case 4:
			key->xkey[i+0] ^= cast_sbox5[U_INT8_Ta(x[2])];
			key->xkey[i+1] ^= cast_sbox6[U_INT8_Tb(x[3])];
			key->xkey[i+2] ^= cast_sbox7[U_INT8_Td(x[0])];
			key->xkey[i+3] ^= cast_sbox8[U_INT8_Td(x[1])];
			break;
		case 8:
			key->xkey[i+0] ^= cast_sbox5[U_INT8_Tb(z[2])];
			key->xkey[i+1] ^= cast_sbox6[U_INT8_Ta(z[3])];
			key->xkey[i+2] ^= cast_sbox7[U_INT8_Tc(z[0])];
			key->xkey[i+3] ^= cast_sbox8[U_INT8_Tc(z[1])];
			break;
		case 12:
			key->xkey[i+0] ^= cast_sbox5[U_INT8_Td(x[0])];
			key->xkey[i+1] ^= cast_sbox6[U_INT8_Td(x[1])];
			key->xkey[i+2] ^= cast_sbox7[U_INT8_Ta(x[2])];
			key->xkey[i+3] ^= cast_sbox8[U_INT8_Tb(x[3])];
			break;
		}
		if (i >= 16) {
			key->xkey[i+0] &= 31;
			key->xkey[i+1] &= 31;
			key->xkey[i+2] &= 31;
			key->xkey[i+3] &= 31;
		}
	}
	/* Wipe clean */
	explicit_bzero(t, sizeof(t));
	explicit_bzero(x, sizeof(x));
	explicit_bzero(z, sizeof(z));
}

/* Made in Canada */
