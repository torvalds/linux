/*	$OpenBSD: chap_ms.c,v 1.9 2022/01/07 07:33:35 tb Exp $	*/

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

static u_int8_t sha1_pad1[40] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static u_int8_t sha1_pad2[40] = {
	0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
	0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
	0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
	0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2
};

u_int8_t	 get7bits(u_int8_t *, int);
void		 mschap_des_addparity(u_int8_t *, u_int8_t *);
void		 mschap_des_encrypt(u_int8_t *, u_int8_t *, u_int8_t *);
void		 mschap_challenge_response(u_int8_t *, u_int8_t *, u_int8_t *);

u_int8_t
get7bits(u_int8_t *in, int start)
{
	u_int	 word;

	word = (u_int)in[start / 8] << 8;
	word |= (u_int)in[start / 8 + 1];
	word >>= 15 - (start % 8 + 7);

	return (word & 0xfe);
}

/* IN  56 bit DES key missing parity bits
   OUT 64 bit DES key with parity bits added */
void
mschap_des_addparity(u_int8_t *key, u_int8_t *des_key)
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
mschap_des_encrypt(u_int8_t *clear, u_int8_t *key, u_int8_t *cipher)
{
	DES_cblock		des_key;
	DES_key_schedule	key_schedule;

	mschap_des_addparity(key, des_key);

	DES_set_key(&des_key, &key_schedule);
	DES_ecb_encrypt((DES_cblock *)clear, (DES_cblock *)cipher,
	    &key_schedule, 1);
}

void
mschap_challenge_response(u_int8_t *challenge, u_int8_t *pwhash,
    u_int8_t *response)
{
	u_int8_t	 padpwhash[21 + 1];

	bzero(&padpwhash, sizeof(padpwhash));
	memcpy(padpwhash, pwhash, MSCHAP_HASH_SZ);

	mschap_des_encrypt(challenge, padpwhash + 0, response + 0);
	mschap_des_encrypt(challenge, padpwhash + 7, response + 8);
	mschap_des_encrypt(challenge, padpwhash + 14, response + 16);
}

void
mschap_ntpassword_hash(u_int8_t *in, int inlen, u_int8_t *hash)
{
	EVP_MD_CTX	*ctx;
	u_int		 mdlen;

	ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(ctx, EVP_md4(), NULL);
	EVP_DigestUpdate(ctx, in, inlen);
	EVP_DigestFinal_ex(ctx, hash, &mdlen);
	EVP_MD_CTX_free(ctx);
}

void
mschap_challenge_hash(u_int8_t *peer_challenge, u_int8_t *auth_challenge,
    u_int8_t *username, int usernamelen, u_int8_t *challenge)
{
	EVP_MD_CTX	*ctx;
	u_int8_t	 md[SHA_DIGEST_LENGTH];
	u_int		 mdlen;
	u_int8_t	*name;

	if ((name = strrchr(username, '\\')) == NULL)
		name = username;
	else
		name++;

	ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
	EVP_DigestUpdate(ctx, peer_challenge, MSCHAPV2_CHALLENGE_SZ);
	EVP_DigestUpdate(ctx, auth_challenge, MSCHAPV2_CHALLENGE_SZ);
	EVP_DigestUpdate(ctx, name, strlen(name));
	EVP_DigestFinal_ex(ctx, md, &mdlen);
	EVP_MD_CTX_free(ctx);

	memcpy(challenge, md, MSCHAP_CHALLENGE_SZ);
}

void
mschap_nt_response(u_int8_t *auth_challenge, u_int8_t *peer_challenge,
    u_int8_t *username, int usernamelen, u_int8_t *password, int passwordlen,
    u_int8_t *response)
{
	u_int8_t challenge[MSCHAP_CHALLENGE_SZ];
	u_int8_t password_hash[MSCHAP_HASH_SZ];

	mschap_challenge_hash(peer_challenge, auth_challenge,
	    username, usernamelen, challenge);

	mschap_ntpassword_hash(password, passwordlen, password_hash);
	mschap_challenge_response(challenge, password_hash, response);
}

void
mschap_auth_response(u_int8_t *password, int passwordlen,
    u_int8_t *ntresponse, u_int8_t *auth_challenge, u_int8_t *peer_challenge,
    u_int8_t *username, int usernamelen, u_int8_t *auth_response)
{
	EVP_MD_CTX	*ctx;
	u_int8_t	 password_hash[MSCHAP_HASH_SZ];
	u_int8_t	 password_hash2[MSCHAP_HASH_SZ];
	u_int8_t	 challenge[MSCHAP_CHALLENGE_SZ];
	u_int8_t	 md[SHA_DIGEST_LENGTH], *ptr;
	u_int		 mdlen;
	int		 i;
	const u_int8_t	 hex[] = "0123456789ABCDEF";
	static u_int8_t	 magic1[39] = {
		0x4D, 0x61, 0x67, 0x69, 0x63, 0x20, 0x73, 0x65, 0x72, 0x76,
		0x65, 0x72, 0x20, 0x74, 0x6F, 0x20, 0x63, 0x6C, 0x69, 0x65,
		0x6E, 0x74, 0x20, 0x73, 0x69, 0x67, 0x6E, 0x69, 0x6E, 0x67,
		0x20, 0x63, 0x6F, 0x6E, 0x73, 0x74, 0x61, 0x6E, 0x74
	};
	static u_int8_t	 magic2[41] = {
		0x50, 0x61, 0x64, 0x20, 0x74, 0x6F, 0x20, 0x6D, 0x61, 0x6B,
		0x65, 0x20, 0x69, 0x74, 0x20, 0x64, 0x6F, 0x20, 0x6D, 0x6F,
		0x72, 0x65, 0x20, 0x74, 0x68, 0x61, 0x6E, 0x20, 0x6F, 0x6E,
		0x65, 0x20, 0x69, 0x74, 0x65, 0x72, 0x61, 0x74, 0x69, 0x6F,
		0x6E
	};

	mschap_ntpassword_hash(password, passwordlen, password_hash);
	mschap_ntpassword_hash(password_hash, MSCHAP_HASH_SZ, password_hash2);

	ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
	EVP_DigestUpdate(ctx, password_hash2, sizeof(password_hash2));
	EVP_DigestUpdate(ctx, ntresponse, 24);
	EVP_DigestUpdate(ctx, magic1, 39);
	EVP_DigestFinal_ex(ctx, md, &mdlen);

	mschap_challenge_hash(peer_challenge, auth_challenge,
	    username, usernamelen, challenge);

	EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
	EVP_DigestUpdate(ctx, md, sizeof(md));
	EVP_DigestUpdate(ctx, challenge, sizeof(challenge));
	EVP_DigestUpdate(ctx, magic2, 41);
	EVP_DigestFinal_ex(ctx, md, &mdlen);
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
mschap_masterkey(u_int8_t *password_hash2, u_int8_t *ntresponse,
    u_int8_t *masterkey)
{
	u_int8_t	 md[SHA_DIGEST_LENGTH];
	u_int		 mdlen;
	EVP_MD_CTX	*ctx;
	static u_int8_t	 magic1[27] = {
		0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74,
		0x68, 0x65, 0x20, 0x4d, 0x50, 0x50, 0x45, 0x20, 0x4d,
		0x61, 0x73, 0x74, 0x65, 0x72, 0x20, 0x4b, 0x65, 0x79
	};

	ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
	EVP_DigestUpdate(ctx, password_hash2, MSCHAP_HASH_SZ);
	EVP_DigestUpdate(ctx, ntresponse, 24);
	EVP_DigestUpdate(ctx, magic1, 27);
	EVP_DigestFinal_ex(ctx, md, &mdlen);
	EVP_MD_CTX_free(ctx);

	memcpy(masterkey, md, 16);
}

void
mschap_asymetric_startkey(u_int8_t *masterkey, u_int8_t *sessionkey,
    int sessionkeylen, int issend, int isserver)
{
	EVP_MD_CTX	*ctx;
	u_int8_t	 md[SHA_DIGEST_LENGTH];
	u_int		 mdlen;
	u_int8_t	*s;
	static u_int8_t	 magic2[84] = {
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
	static u_int8_t	 magic3[84] = {
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
	EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
	EVP_DigestUpdate(ctx, masterkey, 16);
	EVP_DigestUpdate(ctx, sha1_pad1, 40);
	EVP_DigestUpdate(ctx, s, 84);
	EVP_DigestUpdate(ctx, sha1_pad2, 40);
	EVP_DigestFinal_ex(ctx, md, &mdlen);
	EVP_MD_CTX_free(ctx);

	memcpy(sessionkey, md, sessionkeylen);
}

void
mschap_msk(u_int8_t *password, int passwordlen,
    u_int8_t *ntresponse, u_int8_t *msk)
{
	u_int8_t	 password_hash[MSCHAP_HASH_SZ];
	u_int8_t	 password_hash2[MSCHAP_HASH_SZ];
	u_int8_t	 masterkey[MSCHAP_MASTERKEY_SZ];
	u_int8_t	 sendkey[MSCHAP_MASTERKEY_SZ];
	u_int8_t	 recvkey[MSCHAP_MASTERKEY_SZ];

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
mschap_radiuskey(u_int8_t *plain, const u_int8_t *encrypted,
    const u_int8_t *authenticator, const u_int8_t *secret)
{
	EVP_MD_CTX	*ctx;
	u_int8_t	 b[MD5_DIGEST_LENGTH], p[32];
	u_int		 i, mdlen;

	ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
	EVP_DigestUpdate(ctx, secret, strlen(secret));
	EVP_DigestUpdate(ctx, authenticator, 16);
	EVP_DigestUpdate(ctx, encrypted, 2);
	EVP_DigestFinal_ex(ctx, b, &mdlen);

	for (i = 0; i < mdlen; i++) {
		p[i] = b[i] ^ encrypted[i+2];
	}

	EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
	EVP_DigestUpdate(ctx, secret, strlen(secret));
	EVP_DigestUpdate(ctx, encrypted + 2, mdlen);
	EVP_DigestFinal_ex(ctx, b, &mdlen);
	EVP_MD_CTX_free(ctx);

	for (i = 0; i < mdlen; i++) {
		p[i+16] = b[i] ^ encrypted[i+18];
	}

	memcpy(plain, p+1, 16);
}
