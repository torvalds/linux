/*	$OpenBSD: igetest.c,v 1.4 2018/07/17 17:06:49 tb Exp $	*/
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/aes.h>

#define TEST_SIZE	128
#define BIG_TEST_SIZE 10240

static void hexdump(FILE *f,const char *title,const unsigned char *s,int l)
    {
    int n=0;

    fprintf(f,"%s",title);
    for( ; n < l ; ++n)
		{
		if((n%16) == 0)
			fprintf(f,"\n%04x",n);
		fprintf(f," %02x",s[n]);
		}
    fprintf(f,"\n");
    }

#define MAX_VECTOR_SIZE	64

struct ige_test
	{
	const unsigned char key[16];
	const unsigned char iv[32];
	const unsigned char in[MAX_VECTOR_SIZE];
	const unsigned char out[MAX_VECTOR_SIZE];
	const size_t length;
	const int encrypt;
	};

static struct ige_test const ige_test_vectors[] = {
{ { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f }, /* key */
  { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f }, /* iv */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* in */
  { 0x1a, 0x85, 0x19, 0xa6, 0x55, 0x7b, 0xe6, 0x52,
    0xe9, 0xda, 0x8e, 0x43, 0xda, 0x4e, 0xf4, 0x45,
    0x3c, 0xf4, 0x56, 0xb4, 0xca, 0x48, 0x8a, 0xa3,
    0x83, 0xc7, 0x9c, 0x98, 0xb3, 0x47, 0x97, 0xcb }, /* out */
  32, AES_ENCRYPT }, /* test vector 0 */

{ { 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20,
    0x61, 0x6e, 0x20, 0x69, 0x6d, 0x70, 0x6c, 0x65 }, /* key */
  { 0x6d, 0x65, 0x6e, 0x74, 0x61, 0x74, 0x69, 0x6f,
    0x6e, 0x20, 0x6f, 0x66, 0x20, 0x49, 0x47, 0x45,
    0x20, 0x6d, 0x6f, 0x64, 0x65, 0x20, 0x66, 0x6f,
    0x72, 0x20, 0x4f, 0x70, 0x65, 0x6e, 0x53, 0x53 }, /* iv */
  { 0x4c, 0x2e, 0x20, 0x4c, 0x65, 0x74, 0x27, 0x73,
    0x20, 0x68, 0x6f, 0x70, 0x65, 0x20, 0x42, 0x65,
    0x6e, 0x20, 0x67, 0x6f, 0x74, 0x20, 0x69, 0x74,
    0x20, 0x72, 0x69, 0x67, 0x68, 0x74, 0x21, 0x0a }, /* in */
  { 0x99, 0x70, 0x64, 0x87, 0xa1, 0xcd, 0xe6, 0x13,
    0xbc, 0x6d, 0xe0, 0xb6, 0xf2, 0x4b, 0x1c, 0x7a,
    0xa4, 0x48, 0xc8, 0xb9, 0xc3, 0x40, 0x3e, 0x34,
    0x67, 0xa8, 0xca, 0xd8, 0x93, 0x40, 0xf5, 0x3b }, /* out */
  32, AES_DECRYPT }, /* test vector 1 */
};

static int run_test_vectors(void)
	{
	unsigned int n;
	int errs = 0;

	for(n=0 ; n < sizeof(ige_test_vectors)/sizeof(ige_test_vectors[0]) ; ++n)
		{
		const struct ige_test * const v = &ige_test_vectors[n];
		AES_KEY key;
		unsigned char buf[MAX_VECTOR_SIZE];
		unsigned char iv[AES_BLOCK_SIZE*2];

		assert(v->length <= MAX_VECTOR_SIZE);

		if(v->encrypt == AES_ENCRYPT)
			AES_set_encrypt_key(v->key, 8*sizeof v->key, &key);
		else
			AES_set_decrypt_key(v->key, 8*sizeof v->key, &key);
		memcpy(iv, v->iv, sizeof iv);
		AES_ige_encrypt(v->in, buf, v->length, &key, iv, v->encrypt);

		if(memcmp(v->out, buf, v->length))
			{
			printf("IGE test vector %d failed\n", n);
			hexdump(stdout, "key", v->key, sizeof v->key);
			hexdump(stdout, "iv", v->iv, sizeof v->iv);
			hexdump(stdout, "in", v->in, v->length);
			hexdump(stdout, "expected", v->out, v->length);
			hexdump(stdout, "got", buf, v->length);

			++errs;
			}

                /* try with in == out */
		memcpy(iv, v->iv, sizeof iv);
                memcpy(buf, v->in, v->length);
		AES_ige_encrypt(buf, buf, v->length, &key, iv, v->encrypt);

		if(memcmp(v->out, buf, v->length))
			{
			printf("IGE test vector %d failed (with in == out)\n", n);
			hexdump(stdout, "key", v->key, sizeof v->key);
			hexdump(stdout, "iv", v->iv, sizeof v->iv);
			hexdump(stdout, "in", v->in, v->length);
			hexdump(stdout, "expected", v->out, v->length);
			hexdump(stdout, "got", buf, v->length);

			++errs;
			}
		}

	return errs;
	}

int main(int argc, char **argv)
	{
	unsigned char rkey[16];
	unsigned char rkey2[16];
	AES_KEY key;
	AES_KEY key2;
	unsigned char plaintext[BIG_TEST_SIZE];
	unsigned char ciphertext[BIG_TEST_SIZE];
	unsigned char checktext[BIG_TEST_SIZE];
	unsigned char iv[AES_BLOCK_SIZE*4];
	unsigned char saved_iv[AES_BLOCK_SIZE*4];
	int err = 0;
	unsigned int n;
	unsigned matches;

	assert(BIG_TEST_SIZE >= TEST_SIZE);

	arc4random_buf(rkey, sizeof(rkey));
	arc4random_buf(plaintext, sizeof(plaintext));
	arc4random_buf(iv, sizeof(iv));
	memcpy(saved_iv, iv, sizeof(saved_iv));

	/* Forward IGE only... */

	/* Straight encrypt/decrypt */
	AES_set_encrypt_key(rkey, 8*sizeof rkey, &key);
	AES_ige_encrypt(plaintext, ciphertext, TEST_SIZE, &key, iv,
					AES_ENCRYPT);

	AES_set_decrypt_key(rkey, 8*sizeof rkey, &key);
	memcpy(iv, saved_iv, sizeof iv);
	AES_ige_encrypt(ciphertext, checktext, TEST_SIZE, &key, iv,
					AES_DECRYPT);

	if(memcmp(checktext, plaintext, TEST_SIZE))
		{
		printf("Encrypt+decrypt doesn't match\n");
		hexdump(stdout, "Plaintext", plaintext, TEST_SIZE);
		hexdump(stdout, "Checktext", checktext, TEST_SIZE);
		++err;
		}

	/* Now check encrypt chaining works */
	AES_set_encrypt_key(rkey, 8*sizeof rkey, &key);
	memcpy(iv, saved_iv, sizeof iv);
	AES_ige_encrypt(plaintext, ciphertext, TEST_SIZE/2, &key, iv,
					AES_ENCRYPT);
	AES_ige_encrypt(plaintext+TEST_SIZE/2,
					ciphertext+TEST_SIZE/2, TEST_SIZE/2,
					&key, iv, AES_ENCRYPT);

	AES_set_decrypt_key(rkey, 8*sizeof rkey, &key);
	memcpy(iv, saved_iv, sizeof iv);
	AES_ige_encrypt(ciphertext, checktext, TEST_SIZE, &key, iv,
					AES_DECRYPT);

	if(memcmp(checktext, plaintext, TEST_SIZE))
		{
		printf("Chained encrypt+decrypt doesn't match\n");
		hexdump(stdout, "Plaintext", plaintext, TEST_SIZE);
		hexdump(stdout, "Checktext", checktext, TEST_SIZE);
		++err;
		}

	/* And check decrypt chaining */
	AES_set_encrypt_key(rkey, 8*sizeof rkey, &key);
	memcpy(iv, saved_iv, sizeof iv);
	AES_ige_encrypt(plaintext, ciphertext, TEST_SIZE/2, &key, iv,
					AES_ENCRYPT);
	AES_ige_encrypt(plaintext+TEST_SIZE/2,
					ciphertext+TEST_SIZE/2, TEST_SIZE/2,
					&key, iv, AES_ENCRYPT);

	AES_set_decrypt_key(rkey, 8*sizeof rkey, &key);
	memcpy(iv, saved_iv, sizeof iv);
	AES_ige_encrypt(ciphertext, checktext, TEST_SIZE/2, &key, iv,
					AES_DECRYPT);
	AES_ige_encrypt(ciphertext+TEST_SIZE/2,
					checktext+TEST_SIZE/2, TEST_SIZE/2, &key, iv,
					AES_DECRYPT);

	if(memcmp(checktext, plaintext, TEST_SIZE))
		{
		printf("Chained encrypt+chained decrypt doesn't match\n");
		hexdump(stdout, "Plaintext", plaintext, TEST_SIZE);
		hexdump(stdout, "Checktext", checktext, TEST_SIZE);
		++err;
		}

	/* make sure garble extends forwards only */
	AES_set_encrypt_key(rkey, 8*sizeof rkey, &key);
	memcpy(iv, saved_iv, sizeof iv);
	AES_ige_encrypt(plaintext, ciphertext, sizeof plaintext, &key, iv,
					AES_ENCRYPT);

	/* corrupt halfway through */
	++ciphertext[sizeof ciphertext/2];
	AES_set_decrypt_key(rkey, 8*sizeof rkey, &key);
	memcpy(iv, saved_iv, sizeof iv);
	AES_ige_encrypt(ciphertext, checktext, sizeof checktext, &key, iv,
					AES_DECRYPT);

	matches=0;
	for(n=0 ; n < sizeof checktext ; ++n)
		if(checktext[n] == plaintext[n])
			++matches;

	if(matches > sizeof checktext/2+sizeof checktext/100)
		{
		printf("More than 51%% matches after garbling\n");
		++err;
		}

	if(matches < sizeof checktext/2)
		{
		printf("Garble extends backwards!\n");
		++err;
		}

	/* make sure garble extends both ways */
	AES_set_encrypt_key(rkey, 8*sizeof rkey, &key);
	AES_set_encrypt_key(rkey2, 8*sizeof rkey2, &key2);
	AES_ige_encrypt(plaintext, ciphertext, sizeof plaintext, &key, iv,
					AES_ENCRYPT);

	/* corrupt halfway through */
	++ciphertext[sizeof ciphertext/2];
	AES_set_decrypt_key(rkey, 8*sizeof rkey, &key);
	AES_set_decrypt_key(rkey2, 8*sizeof rkey2, &key2);
	AES_ige_encrypt(ciphertext, checktext, sizeof checktext, &key, iv,
					AES_DECRYPT);

	matches=0;
	for(n=0 ; n < sizeof checktext ; ++n)
		if(checktext[n] == plaintext[n])
			++matches;

	if(matches > sizeof checktext/100)
		{
		printf("More than 1%% matches after bidirectional garbling\n");
		++err;
		}

	/* make sure garble extends both ways (2) */
	AES_set_encrypt_key(rkey, 8*sizeof rkey, &key);
	AES_set_encrypt_key(rkey2, 8*sizeof rkey2, &key2);
	AES_ige_encrypt(plaintext, ciphertext, sizeof plaintext, &key, iv,
					AES_ENCRYPT);

	/* corrupt right at the end */
	++ciphertext[sizeof ciphertext-1];
	AES_set_decrypt_key(rkey, 8*sizeof rkey, &key);
	AES_set_decrypt_key(rkey2, 8*sizeof rkey2, &key2);
	AES_ige_encrypt(ciphertext, checktext, sizeof checktext, &key, iv,
					AES_DECRYPT);

	matches=0;
	for(n=0 ; n < sizeof checktext ; ++n)
		if(checktext[n] == plaintext[n])
			++matches;

	if(matches > sizeof checktext/100)
		{
		printf("More than 1%% matches after bidirectional garbling (2)\n");
		++err;
		}

	/* make sure garble extends both ways (3) */
	AES_set_encrypt_key(rkey, 8*sizeof rkey, &key);
	AES_set_encrypt_key(rkey2, 8*sizeof rkey2, &key2);
	AES_ige_encrypt(plaintext, ciphertext, sizeof plaintext, &key, iv,
					AES_ENCRYPT);

	/* corrupt right at the start */
	++ciphertext[0];
	AES_set_decrypt_key(rkey, 8*sizeof rkey, &key);
	AES_set_decrypt_key(rkey2, 8*sizeof rkey2, &key2);
	AES_ige_encrypt(ciphertext, checktext, sizeof checktext, &key, iv,
					AES_DECRYPT);

	matches=0;
	for(n=0 ; n < sizeof checktext ; ++n)
		if(checktext[n] == plaintext[n])
			++matches;

	if(matches > sizeof checktext/100)
		{
		printf("More than 1%% matches after bidirectional garbling (3)\n");
		++err;
		}

	err += run_test_vectors();

	return err;
	}
