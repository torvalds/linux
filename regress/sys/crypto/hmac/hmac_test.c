/*-
 * Copyright (c) 2008 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <crypto/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/hmac.h>
#include <string.h>

static void
print_hex(unsigned char *buf, int len)
{
	int i;

	printf("digest = 0x");
	for (i = 0; i < len; i++)
		printf("%02x", buf[i]);
	printf("\n");
}

int
main(void)
{
	HMAC_MD5_CTX md5;
	HMAC_SHA1_CTX sha1;
	HMAC_SHA256_CTX sha256;
	u_int8_t data[50], output[32];

	HMAC_MD5_Init(&md5, "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b", 16);
	HMAC_MD5_Update(&md5, "Hi There", 8);
	HMAC_MD5_Final(output, &md5);
	print_hex(output, MD5_DIGEST_LENGTH);

	HMAC_MD5_Init(&md5, "Jefe", 4);
	HMAC_MD5_Update(&md5, "what do ya want for nothing?", 28);
	HMAC_MD5_Final(output, &md5);
	print_hex(output, MD5_DIGEST_LENGTH);

	HMAC_MD5_Init(&md5, "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA", 16);
	memset(data, 0xDD, sizeof data);
	HMAC_MD5_Update(&md5, data, sizeof data);
	HMAC_MD5_Final(output, &md5);
	print_hex(output, MD5_DIGEST_LENGTH);

	HMAC_SHA1_Init(&sha1, "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b", 16);
	HMAC_SHA1_Update(&sha1, "Hi There", 8);
	HMAC_SHA1_Final(output, &sha1);
	print_hex(output, SHA1_DIGEST_LENGTH);

	HMAC_SHA1_Init(&sha1, "Jefe", 4);
	HMAC_SHA1_Update(&sha1, "what do ya want for nothing?", 28);
	HMAC_SHA1_Final(output, &sha1);
	print_hex(output, SHA1_DIGEST_LENGTH);

	HMAC_SHA1_Init(&sha1, "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA", 16);
	memset(data, 0xDD, sizeof data);
	HMAC_SHA1_Update(&sha1, data, sizeof data);
	HMAC_SHA1_Final(output, &sha1);
	print_hex(output, SHA1_DIGEST_LENGTH);

	HMAC_SHA256_Init(&sha256, "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b", 16);
	HMAC_SHA256_Update(&sha256, "Hi There", 8);
	HMAC_SHA256_Final(output, &sha256);
	print_hex(output, SHA256_DIGEST_LENGTH);

	HMAC_SHA256_Init(&sha256, "Jefe", 4);
	HMAC_SHA256_Update(&sha256, "what do ya want for nothing?", 28);
	HMAC_SHA256_Final(output, &sha256);
	print_hex(output, SHA256_DIGEST_LENGTH);

	HMAC_SHA256_Init(&sha256, "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA", 16);
	memset(data, 0xDD, sizeof data);
	HMAC_SHA256_Update(&sha256, data, sizeof data);
	HMAC_SHA256_Final(output, &sha256);
	print_hex(output, SHA256_DIGEST_LENGTH);

	return 0;
}

void
explicit_bzero(void *b, size_t len)
{
	bzero(b, len);
}
