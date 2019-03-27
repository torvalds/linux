/*	$FreeBSD$	*/
/*	$KAME: rijndael-api-fst.h,v 1.6 2001/05/27 00:23:23 itojun Exp $	*/

/*
 * rijndael-api-fst.h   v2.3   April '2000
 *
 * Optimised ANSI C code
 *
 */

#ifndef __RIJNDAEL_API_FST_H
#define __RIJNDAEL_API_FST_H

#include <crypto/rijndael/rijndael.h>

/*  Generic Defines  */
#define     DIR_ENCRYPT           0 /*  Are we encrpyting?  */
#define     DIR_DECRYPT           1 /*  Are we decrpyting?  */
#define     MODE_ECB              1 /*  Are we ciphering in ECB mode?   */
#define     MODE_CBC              2 /*  Are we ciphering in CBC mode?   */
#define     MODE_CFB1             3 /*  Are we ciphering in 1-bit CFB mode? */
#define     BITSPERBLOCK        128 /* Default number of bits in a cipher block */

/*  Error Codes  */
#define     BAD_KEY_DIR          -1 /*  Key direction is invalid, e.g., unknown value */
#define     BAD_KEY_MAT          -2 /*  Key material not of correct length */
#define     BAD_KEY_INSTANCE     -3 /*  Key passed is not valid */
#define     BAD_CIPHER_MODE      -4 /*  Params struct passed to cipherInit invalid */
#define     BAD_CIPHER_STATE     -5 /*  Cipher in wrong state (e.g., not initialized) */
#define     BAD_BLOCK_LENGTH     -6
#define     BAD_CIPHER_INSTANCE  -7
#define     BAD_DATA             -8 /*  Data contents are invalid, e.g., invalid padding */
#define     BAD_OTHER            -9 /*  Unknown error */

/*  Algorithm-specific Defines  */
#define     RIJNDAEL_MAX_KEY_SIZE         64 /* # of ASCII char's needed to represent a key */
#define     RIJNDAEL_MAX_IV_SIZE          16 /* # bytes needed to represent an IV  */

/*  Typedefs  */

/*  The structure for key information */
typedef struct {
    u_int8_t  direction;            /* Key used for encrypting or decrypting? */
    int   keyLen;                   /* Length of the key  */
    char  keyMaterial[RIJNDAEL_MAX_KEY_SIZE+1];  /* Raw key data in ASCII, e.g., user input or KAT values */
	int   Nr;                       /* key-length-dependent number of rounds */
	u_int32_t   rk[4*(RIJNDAEL_MAXNR + 1)];        /* key schedule */
	u_int32_t   ek[4*(RIJNDAEL_MAXNR + 1)];        /* CFB1 key schedule (encryption only) */
} keyInstance;

/*  The structure for cipher information */
typedef struct {                    /* changed order of the components */
    u_int8_t mode;                  /* MODE_ECB, MODE_CBC, or MODE_CFB1 */
    u_int8_t IV[RIJNDAEL_MAX_IV_SIZE]; /* A possible Initialization Vector for ciphering */
} cipherInstance;

/*  Function prototypes  */

int rijndael_makeKey(keyInstance *, u_int8_t, int, const char *);

int rijndael_cipherInit(cipherInstance *, u_int8_t, char *);

int rijndael_blockEncrypt(cipherInstance *, keyInstance *, const u_int8_t *,
	int, u_int8_t *);
int rijndael_padEncrypt(cipherInstance *, keyInstance *, const u_int8_t *,
	int, u_int8_t *);

int rijndael_blockDecrypt(cipherInstance *, keyInstance *, const u_int8_t *,
	int, u_int8_t *);
int rijndael_padDecrypt(cipherInstance *, keyInstance *, const u_int8_t *,
	int, u_int8_t *);

#endif /*  __RIJNDAEL_API_FST_H */
