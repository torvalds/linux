/*	$OpenBSD: chap_ms.c,v 1.10 2021/02/04 19:59:15 tobhe Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 1997-2001 Brian Somers <brian@Awfulhak.org>
 * Copyright (c) 1997 Gabor Kincses <gabor@acm.org>
 * Copyright (c) 1995 Eric Rosenquist
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
  */

#include <sys/types.h>

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include <openssl/evp.h>
#include <openssl/des.h>
#include <openssl/md4.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "chap_ms.h"

extern __dead void fatalx(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));

/*
 * Documentation & specifications:
 *
 * MS-CHAP (CHAP80)	RFC2433
 * MS-CHAP-V2 (CHAP81)	RFC2759
 * MPPE key management	RFC3079
 *
 * Security analysis:
 * Schneier/Mudge/Wagner, "MS-CHAP-v2", Oct 99
 * "It is unclear to us why this protocol is so complicated."
 */

static uint8_t sha1_pad1[40] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t sha1_pad2[40] = {
	0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
	0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
	0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
	0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2
};

uint8_t		 get7bits(uint8_t *, int);
void		 mschap_des_addparity(uint8_t *, uint8_t *);
void		 mschap_des_encrypt(uint8_t *, uint8_t *, uint8_t *);
void		 mschap_challenge_response(uint8_t *, uint8_t *, uint8_t *);

uint8_t
get7bits(uint8_t *in, int start)
{
	unsigned int	 word;

	word = (unsigned int)in[start / 8] << 8;
	word |= (unsigned int)in[start / 8 + 1];
	word >>= 15 - (start % 8 + 7);

	return (word & 0xfe);
}

/* IN  56 bit DES key missing parity bits
   OUT 64 bit DES key with parity bits added */
void
mschap_des_addparity(uint8_t *key, uint8_t *des_key)
{
	des_key[0] = get7bits(key,  0);
	des_key[1] = get7bits(key,  7);
	des_key[2] = get7bits(key, 14);
	des_key[3] = get7bits(key, 21);
	des_key[4] = get7bits(key, 28);
	des_key[5] = get7bits(key, 35);
	des_key[6] = get7bits(key, 42);
	des_key[7] = get7bits(key, 49);

	DES_set_odd_parity((DES_cblock *)des_key);
}

void
mschap_des_encrypt(uint8_t *clear, uint8_t *key, uint8_t *cipher)
{
	DES_cblock		des_key;
	DES_key_schedule	key_schedule;

	mschap_des_addparity(key, des_key);

	DES_set_key(&des_key, &key_schedule);
	DES_ecb_encrypt((DES_cblock *)clear, (DES_cblock *)cipher,
	    &key_schedule, 1);
}

void
mschap_challenge_response(uint8_t *challenge, uint8_t *pwhash,
    uint8_t *response)
{
	uint8_t		 padpwhash[21 + 1];

	bzero(&padpwhash, sizeof(padpwhash));
	memcpy(padpwhash, pwhash, MSCHAP_HASH_SZ);

	mschap_des_encrypt(challenge, padpwhash + 0, response + 0);
	mschap_des_encrypt(challenge, padpwhash + 7, response + 8);
	mschap_des_encrypt(challenge, padpwhash + 14, response + 16);
}

void
mschap_ntpassword_hash(uint8_t *in, int inlen, uint8_t *hash)
{
	EVP_MD_CTX	 *ctx;
	unsigned int	 mdlen;

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		fatalx("%s: EVP_MD_CTX_NEW()", __func__);
	EVP_DigestInit(ctx, EVP_md4());
	EVP_DigestUpdate(ctx, in, inlen);
	EVP_DigestFinal(ctx, hash, &mdlen);
	EVP_MD_CTX_free(ctx);
}

void
mschap_challenge_hash(uint8_t *peer_challenge, uint8_t *auth_challenge,
    uint8_t *username, int usernamelen, uint8_t *challenge)
{
	EVP_MD_CTX	*ctx;
	uint8_t		 md[SHA_DIGEST_LENGTH];
	unsigned int	 mdlen;
	uint8_t		*name;

	if ((name = strrchr(username, '\\')) == NULL)
		name = username;
	else
		name++;

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		fatalx("%s: EVP_MD_CTX_NEW()", __func__);
	EVP_DigestInit(ctx, EVP_sha1());
	EVP_DigestUpdate(ctx, peer_challenge, MSCHAPV2_CHALLENGE_SZ);
	EVP_DigestUpdate(ctx, auth_challenge, MSCHAPV2_CHALLENGE_SZ);
	EVP_DigestUpdate(ctx, name, strlen(name));
	EVP_DigestFinal(ctx, md, &mdlen);
	EVP_MD_CTX_free(ctx);

	memcpy(challenge, md, MSCHAP_CHALLENGE_SZ);
}

void
mschap_nt_response(uint8_t *auth_challenge, uint8_t *peer_challenge,
    uint8_t *username, int usernamelen, uint8_t *password, int passwordlen,
    uint8_t *response)
{
	uint8_t		 challenge[MSCHAP_CHALLENGE_SZ];
	uint8_t		 password_hash[MSCHAP_HASH_SZ];

	mschap_challenge_hash(peer_challenge, auth_challenge,
	    username, usernamelen, challenge);

	mschap_ntpassword_hash(password, passwordlen, password_hash);
	mschap_challenge_response(challenge, password_hash, response);
}

void
mschap_auth_response(uint8_t *password, int passwordlen,
    uint8_t *ntresponse, uint8_t *auth_challenge, uint8_t *peer_challenge,
    uint8_t *username, int usernamelen, uint8_t *auth_response)
{
	EVP_MD_CTX	*ctx;
	uint8_t		 password_hash[MSCHAP_HASH_SZ];
	uint8_t		 password_hash2[MSCHAP_HASH_SZ];
	uint8_t		 challenge[MSCHAP_CHALLENGE_SZ];
	uint8_t		 md[SHA_DIGEST_LENGTH], *ptr;
	unsigned int	 mdlen;
	int		 i;
	const uint8_t	 hex[] = "0123456789ABCDEF";
	static uint8_t	 magic1[39] = {
		0x4D, 0x61, 0x67, 0x69, 0x63, 0x20, 0x73, 0x65, 0x72, 0x76,
		0x65, 0x72, 0x20, 0x74, 0x6F, 0x20, 0x63, 0x6C, 0x69, 0x65,
		0x6E, 0x74, 0x20, 0x73, 0x69, 0x67, 0x6E, 0x69, 0x6E, 0x67,
		0x20, 0x63, 0x6F, 0x6E, 0x73, 0x74, 0x61, 0x6E, 0x74
	};
	static uint8_t	 magic2[41] = {
		0x50, 0x61, 0x64, 0x20, 0x74, 0x6F, 0x20, 0x6D, 0x61, 0x6B,
		0x65, 0x20, 0x69, 0x74, 0x20, 0x64, 0x6F, 0x20, 0x6D, 0x6F,
		0x72, 0x65, 0x20, 0x74, 0x68, 0x61, 0x6E, 0x20, 0x6F, 0x6E,
		0x65, 0x20, 0x69, 0x74, 0x65, 0x72, 0x61, 0x74, 0x69, 0x6F,
		0x6E
	};

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		fatalx("%s: EVP_MD_CTX_NEW()", __func__);
	mschap_ntpassword_hash(password, passwordlen, password_hash);
	mschap_ntpassword_hash(password_hash, MSCHAP_HASH_SZ, password_hash2);

	EVP_DigestInit(ctx, EVP_sha1());
	EVP_DigestUpdate(ctx, password_hash2, sizeof(password_hash2));
	EVP_DigestUpdate(ctx, ntresponse, 24);
	EVP_DigestUpdate(ctx, magic1, 39);
	EVP_DigestFinal(ctx, md, &mdlen);

	mschap_challenge_hash(peer_challenge, auth_challenge,
	    username, usernamelen, challenge);

	EVP_DigestInit(ctx, EVP_sha1());
	EVP_DigestUpdate(ctx, md, sizeof(md));
	EVP_DigestUpdate(ctx, challenge, sizeof(challenge));
	EVP_DigestUpdate(ctx, magic2, 41);
	EVP_DigestFinal(ctx, md, &mdlen);
	EVP_MD_CTX_free(ctx);

	/*
	 * Encode the value of 'Digest' as "S=" followed by
	 * 40 ASCII hexadecimal digits and return it in
	 * AuthenticatorResponse.
	 * For example,
	 *   "S=0123456789ABCDEF0123456789ABCDEF01234567"
	 */
	ptr = auth_response;
	*ptr++ = 'S';
	*ptr++ = '=';
	for (i = 0; i < SHA_DIGEST_LENGTH; i++) {
		*ptr++ = hex[md[i] >> 4];
		*ptr++ = hex[md[i] & 0x0f];
	}
}

void
mschap_masterkey(uint8_t *password_hash2, uint8_t *ntresponse,
    uint8_t *masterkey)
{
	uint8_t		 md[SHA_DIGEST_LENGTH];
	unsigned int	 mdlen;
	EVP_MD_CTX	*ctx;
	static uint8_t	 magic1[27] = {
		0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74,
		0x68, 0x65, 0x20, 0x4d, 0x50, 0x50, 0x45, 0x20, 0x4d,
		0x61, 0x73, 0x74, 0x65, 0x72, 0x20, 0x4b, 0x65, 0x79
	};

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		fatalx("%s: EVP_MD_CTX_NEW()", __func__);
	EVP_DigestInit(ctx, EVP_sha1());
	EVP_DigestUpdate(ctx, password_hash2, MSCHAP_HASH_SZ);
	EVP_DigestUpdate(ctx, ntresponse, 24);
	EVP_DigestUpdate(ctx, magic1, 27);
	EVP_DigestFinal(ctx, md, &mdlen);
	EVP_MD_CTX_free(ctx);

	memcpy(masterkey, md, 16);
}

void
mschap_asymetric_startkey(uint8_t *masterkey, uint8_t *sessionkey,
    int sessionkeylen, int issend, int isserver)
{
	EVP_MD_CTX	*ctx;
	uint8_t		 md[SHA_DIGEST_LENGTH];
	unsigned int	 mdlen;
	uint8_t		*s;
	static uint8_t	 magic2[84] = {
		0x4f, 0x6e, 0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6c, 0x69,
		0x65, 0x6e, 0x74, 0x20, 0x73, 0x69, 0x64, 0x65, 0x2c, 0x20,
		0x74, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74, 0x68,
		0x65, 0x20, 0x73, 0x65, 0x6e, 0x64, 0x20, 0x6b, 0x65, 0x79,
		0x3b, 0x20, 0x6f, 0x6e, 0x20, 0x74, 0x68, 0x65, 0x20, 0x73,
		0x65, 0x72, 0x76, 0x65, 0x72, 0x20, 0x73, 0x69, 0x64, 0x65,
		0x2c, 0x20, 0x69, 0x74, 0x20, 0x69, 0x73, 0x20, 0x74, 0x68,
		0x65, 0x20, 0x72, 0x65, 0x63, 0x65, 0x69, 0x76, 0x65, 0x20,
		0x6b, 0x65, 0x79, 0x2e
	};
	static uint8_t	 magic3[84] = {
		0x4f, 0x6e, 0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6c, 0x69,
		0x65, 0x6e, 0x74, 0x20, 0x73, 0x69, 0x64, 0x65, 0x2c, 0x20,
		0x74, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74, 0x68,
		0x65, 0x20, 0x72, 0x65, 0x63, 0x65, 0x69, 0x76, 0x65, 0x20,
		0x6b, 0x65, 0x79, 0x3b, 0x20, 0x6f, 0x6e, 0x20, 0x74, 0x68,
		0x65, 0x20, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x20, 0x73,
		0x69, 0x64, 0x65, 0x2c, 0x20, 0x69, 0x74, 0x20, 0x69, 0x73,
		0x20, 0x74, 0x68, 0x65, 0x20, 0x73, 0x65, 0x6e, 0x64, 0x20,
		0x6b, 0x65, 0x79, 0x2e
	};

	if (issend)
		s = isserver ? magic3 : magic2;
	else
		s = isserver ? magic2 : magic3;

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		fatalx("%s: EVP_MD_CTX_NEW()", __func__);
	EVP_DigestInit(ctx, EVP_sha1());
	EVP_DigestUpdate(ctx, masterkey, 16);
	EVP_DigestUpdate(ctx, sha1_pad1, 40);
	EVP_DigestUpdate(ctx, s, 84);
	EVP_DigestUpdate(ctx, sha1_pad2, 40);
	EVP_DigestFinal(ctx, md, &mdlen);
	EVP_MD_CTX_free(ctx);

	memcpy(sessionkey, md, sessionkeylen);
}

void
mschap_msk(uint8_t *password, int passwordlen,
    uint8_t *ntresponse, uint8_t *msk)
{
	uint8_t		 password_hash[MSCHAP_HASH_SZ];
	uint8_t		 password_hash2[MSCHAP_HASH_SZ];
	uint8_t		 masterkey[MSCHAP_MASTERKEY_SZ];
	uint8_t		 sendkey[MSCHAP_MASTERKEY_SZ];
	uint8_t		 recvkey[MSCHAP_MASTERKEY_SZ];

	mschap_ntpassword_hash(password, passwordlen, password_hash);
	mschap_ntpassword_hash(password_hash, MSCHAP_HASH_SZ, password_hash2);

	mschap_masterkey(password_hash2, ntresponse, masterkey);
	mschap_asymetric_startkey(masterkey, recvkey, sizeof(recvkey), 0, 1);
	mschap_asymetric_startkey(masterkey, sendkey, sizeof(sendkey), 1, 1);

	/* 16 bytes receive key + 16 bytes send key + 32 bytes 0 padding */
	bzero(msk, MSCHAP_MSK_SZ);
	memcpy(msk, &recvkey, sizeof(recvkey));
	memcpy(msk + sizeof(recvkey), &sendkey, sizeof(sendkey));
}

void
mschap_radiuskey(uint8_t *plain, const uint8_t *crypted,
    const uint8_t *authenticator, const uint8_t *secret)
{
	EVP_MD_CTX	*ctx;
	uint8_t		 b[MD5_DIGEST_LENGTH], p[32];
	unsigned int	 i, mdlen;

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		fatalx("%s: EVP_MD_CTX_NEW()", __func__);
	EVP_DigestInit(ctx, EVP_md5());
	EVP_DigestUpdate(ctx, secret, strlen(secret));
	EVP_DigestUpdate(ctx, authenticator, 16);
	EVP_DigestUpdate(ctx, crypted, 2);
	EVP_DigestFinal(ctx, b, &mdlen);

	for (i = 0; i < mdlen; i++) {
		p[i] = b[i] ^ crypted[i+2];
	}

	EVP_DigestInit(ctx, EVP_md5());
	EVP_DigestUpdate(ctx, secret, strlen(secret));
	EVP_DigestUpdate(ctx, crypted + 2, mdlen);
	EVP_DigestFinal(ctx, b, &mdlen);
	EVP_MD_CTX_free(ctx);

	for (i = 0; i < mdlen; i++) {
		p[i+16] = b[i] ^ crypted[i+18];
	}

	memcpy(plain, p+1, 16);
}
