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

#include <string.h>
#include <stdio.h>
#include <crypto/aes.h>
#include <crypto/key_wrap.h>

static void
print_hex(const char *str, unsigned char *buf, int len)
{
	int i;

	printf("%s", str);
	for (i = 0; i < len; i++) {
		if ((i % 8) == 0)
			printf(" ");
		printf("%02X", buf[i]);
	}
	printf("\n");
}

static void
do_test(u_int kek_len, u_int data_len)
{
	aes_key_wrap_ctx ctx;
	u_int8_t kek[32], data[32];
	u_int8_t output[64];
	u_int i;

	for (i = 0; i < kek_len; i++)
		kek[i] = i;
	printf("Input:\n");
	print_hex("KEK:\n  ", kek, kek_len);
	for (i = 0; i < 16; i++)
		data[i] = i * 16 + i;
	for (; i < data_len; i++)
		data[i] = i - 16;
	print_hex("Key Data:\n  ", data, data_len);
	aes_key_wrap_set_key(&ctx, kek, kek_len);
	aes_key_wrap(&ctx, data, data_len / 8, output);
	print_hex("Ciphertext:\n  ", output, data_len + 8);
	aes_key_unwrap(&ctx, output, output, data_len / 8);
	printf("Output:\n");
	print_hex("Key Data:\n  ", output, data_len);
	printf("====\n");
}

int
main(void)
{
	do_test(16, 16);
	do_test(24, 16);
	do_test(32, 16);
	do_test(24, 24);
	do_test(32, 24);
	do_test(32, 32);

	return 0;
}

void
explicit_bzero(void *b, size_t len)
{
	bzero(b, len);
}
