/* camellia.h ver 1.1.0
 *
 * Copyright (c) 2006
 * NTT (Nippon Telegraph and Telephone Corporation) . All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer as
 *   the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NTT ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL NTT BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _CAMELLIA_H
#define _CAMELLIA_H

#define CAMELLIA_BLOCK_SIZE 16
#define CAMELLIA_SUBKEYWORD 68 /* (34*8/4) */

typedef struct {
    int	bits;				      /* key-length */
    uint32_t subkey[CAMELLIA_SUBKEYWORD]; /* encrypt/decrypt key schedule */
} camellia_ctx;

void camellia_set_key(camellia_ctx *, const u_char *, int);
void camellia_decrypt(const camellia_ctx *, const u_char *, u_char *);
void camellia_encrypt(const camellia_ctx *, const u_char *, u_char *);


void Camellia_Ekeygen(const int keyBitLength,
		      const unsigned char *rawKey, 
		      uint32_t *subkey);

void Camellia_EncryptBlock(const int keyBitLength,
			   const unsigned char *plaintext, 
			   const uint32_t *subkey,
			   unsigned char *cipherText);

void Camellia_DecryptBlock(const int keyBitLength, 
			   const unsigned char *cipherText, 
			   const uint32_t *subkey,
			   unsigned char *plaintext);

void camellia_setup128(const unsigned char *key, uint32_t *subkey);
void camellia_setup192(const unsigned char *key, uint32_t *subkey);
void camellia_setup256(const unsigned char *key, uint32_t *subkey);
void camellia_encrypt128(const uint32_t *subkey, uint32_t *io);
void camellia_encrypt256(const uint32_t *subkey, uint32_t *io);
void camellia_decrypt128(const uint32_t *subkey, uint32_t *io);
void camellia_decrypt256(const uint32_t *subkey, uint32_t *io);


#endif /* _CAMELLIA_H */
