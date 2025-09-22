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

#include <sys/types.h>
#include <crypto/aes.h>
#include <crypto/cmac.h>
#include <stdio.h>
#include <string.h>

static void
print_hex(char *str, unsigned char *buf, int len)
{
      int     i;

      for ( i=0; i<len; i++ ) {
          if ( (i % 16) == 0 && i != 0 ) printf("%s", str);
          printf("%02x", buf[i]);
          if ( (i % 4) == 3 ) printf(" ");
          if ( (i % 16) == 15 ) printf("\n");
      }
      if ( (i % 16) != 0 ) printf("\n");
}

static void
print128(unsigned char *bytes)
{
      int         j;
      for (j=0; j<16;j++) {
          printf("%02x",bytes[j]);
          if ( (j%4) == 3 ) printf(" ");
      }
}

int
main(void)
{
      unsigned char T[16];
      unsigned char M[64] = {
          0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
          0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
          0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
          0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
          0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
          0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
          0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
          0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
      };
      unsigned char key[16] = {
          0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
          0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
      };
      AES_CMAC_CTX ctx;

      printf("--------------------------------------------------\n");
      printf("K              "); print128(key); printf("\n");

      printf("\nExample 1: len = 0\n");
      printf("M              "); printf("<empty string>\n");

      AES_CMAC_SetKey(&ctx, key);
      AES_CMAC_Init(&ctx);
      AES_CMAC_Update(&ctx, M, 0);
      AES_CMAC_Final(T, &ctx);
      printf("AES_CMAC       "); print128(T); printf("\n");

      printf("\nExample 2: len = 16\n");
      printf("M              "); print_hex("                ",M,16);

      AES_CMAC_SetKey(&ctx, key);
      AES_CMAC_Init(&ctx);
      AES_CMAC_Update(&ctx, M, 16);
      AES_CMAC_Final(T, &ctx);
      printf("AES_CMAC       "); print128(T); printf("\n");
      printf("\nExample 3: len = 40\n");
      printf("M              "); print_hex("               ",M,40);

      AES_CMAC_SetKey(&ctx, key);
      AES_CMAC_Init(&ctx);
      AES_CMAC_Update(&ctx, M, 40);
      AES_CMAC_Final(T, &ctx);
      printf("AES_CMAC       "); print128(T); printf("\n");

      printf("\nExample 4: len = 64\n");
      printf("M              "); print_hex("               ",M,64);
      AES_CMAC_SetKey(&ctx, key);
      AES_CMAC_Init(&ctx);
      AES_CMAC_Update(&ctx, M, 64);
      AES_CMAC_Final(T, &ctx);
      printf("AES_CMAC       "); print128(T); printf("\n");

      printf("\nExample 4bis: len = 64\n");
      printf("M              "); print_hex("               ",M,64);
      AES_CMAC_SetKey(&ctx, key);
      AES_CMAC_Init(&ctx);
      AES_CMAC_Update(&ctx, M, 40);
      AES_CMAC_Update(&ctx, M + 40, 24);
      AES_CMAC_Final(T, &ctx);
      printf("AES_CMAC       "); print128(T); printf("\n");

      printf("\nExample 4ter: len = 64\n");
      printf("M              "); print_hex("               ",M,64);
      AES_CMAC_SetKey(&ctx, key);
      AES_CMAC_Init(&ctx);
      AES_CMAC_Update(&ctx, M, 16);
      AES_CMAC_Update(&ctx, M + 16, 16);
      AES_CMAC_Update(&ctx, M + 32, 10);
      AES_CMAC_Update(&ctx, M + 42, 0);
      AES_CMAC_Update(&ctx, M + 42, 14);
      AES_CMAC_Update(&ctx, M + 56, 8);
      AES_CMAC_Final(T, &ctx);
      printf("AES_CMAC       "); print128(T); printf("\n");

      printf("--------------------------------------------------\n");

      return 0;
}

void
explicit_bzero(void *b, size_t len)
{
	bzero(b, len);
}
