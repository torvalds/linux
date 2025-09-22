/* md5.c - Functions to compute MD5 message digest of files or memory blocks
   according to the definition of MD5 in RFC 1321 from April 1992.
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   NOTE: This source is derived from an old version taken from the GNU C
   Library (glibc).

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Written by Ulrich Drepper <drepper@gnu.ai.mit.edu>, 1995.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>

#if STDC_HEADERS || defined _LIBC
# include <stdlib.h>
# include <string.h>
#else
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
# endif
#endif

#include "ansidecl.h"
#include "md5.h"

#ifdef _LIBC
# include <endian.h>
# if __BYTE_ORDER == __BIG_ENDIAN
#  define WORDS_BIGENDIAN 1
# endif
#endif

#ifdef WORDS_BIGENDIAN
# define SWAP(n)							\
    (((n) << 24) | (((n) & 0xff00) << 8) | (((n) >> 8) & 0xff00) | ((n) >> 24))
#else
# define SWAP(n) (n)
#endif


/* This array contains the bytes used to pad the buffer to the next
   64-byte boundary.  (RFC 1321, 3.1: Step 1)  */
static const unsigned char fillbuf[64] = { 0x80, 0 /* , 0, 0, ...  */ };


/* Initialize structure containing state of computation.
   (RFC 1321, 3.3: Step 3)  */
void
md5_init_ctx (struct md5_ctx *ctx)
{
  ctx->A = (md5_uint32) 0x67452301;
  ctx->B = (md5_uint32) 0xefcdab89;
  ctx->C = (md5_uint32) 0x98badcfe;
  ctx->D = (md5_uint32) 0x10325476;

  ctx->total[0] = ctx->total[1] = 0;
  ctx->buflen = 0;
}

/* Put result from CTX in first 16 bytes following RESBUF.  The result
   must be in little endian byte order.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
void *
md5_read_ctx (const struct md5_ctx *ctx, void *resbuf)
{
  ((md5_uint32 *) resbuf)[0] = SWAP (ctx->A);
  ((md5_uint32 *) resbuf)[1] = SWAP (ctx->B);
  ((md5_uint32 *) resbuf)[2] = SWAP (ctx->C);
  ((md5_uint32 *) resbuf)[3] = SWAP (ctx->D);

  return resbuf;
}

/* Process the remaining bytes in the internal buffer and the usual
   prolog according to the standard and write the result to RESBUF.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
void *
md5_finish_ctx (struct md5_ctx *ctx, void *resbuf)
{
  /* Take yet unprocessed bytes into account.  */
  md5_uint32 bytes = ctx->buflen;
  size_t pad;

  /* Now count remaining bytes.  */
  ctx->total[0] += bytes;
  if (ctx->total[0] < bytes)
    ++ctx->total[1];

  pad = bytes >= 56 ? 64 + 56 - bytes : 56 - bytes;
  memcpy (&ctx->buffer[bytes], fillbuf, pad);

  /* Put the 64-bit file length in *bits* at the end of the buffer.  */
  *(md5_uint32 *) &ctx->buffer[bytes + pad] = SWAP (ctx->total[0] << 3);
  *(md5_uint32 *) &ctx->buffer[bytes + pad + 4] = SWAP ((ctx->total[1] << 3) |
							(ctx->total[0] >> 29));

  /* Process last bytes.  */
  md5_process_block (ctx->buffer, bytes + pad + 8, ctx);

  return md5_read_ctx (ctx, resbuf);
}

/* Compute MD5 message digest for bytes read from STREAM.  The
   resulting message digest number will be written into the 16 bytes
   beginning at RESBLOCK.  */
int
md5_stream (FILE *stream, void *resblock)
{
  /* Important: BLOCKSIZE must be a multiple of 64.  */
#define BLOCKSIZE 4096
  struct md5_ctx ctx;
  char buffer[BLOCKSIZE + 72];
  size_t sum;

  /* Initialize the computation context.  */
  md5_init_ctx (&ctx);

  /* Iterate over full file contents.  */
  while (1)
    {
      /* We read the file in blocks of BLOCKSIZE bytes.  One call of the
	 computation function processes the whole buffer so that with the
	 next round of the loop another block can be read.  */
      size_t n;
      sum = 0;

      /* Read block.  Take care for partial reads.  */
      do
	{
	  n = fread (buffer + sum, 1, BLOCKSIZE - sum, stream);

	  sum += n;
	}
      while (sum < BLOCKSIZE && n != 0);
      if (n == 0 && ferror (stream))
        return 1;

      /* If end of file is reached, end the loop.  */
      if (n == 0)
	break;

      /* Process buffer with BLOCKSIZE bytes.  Note that
			BLOCKSIZE % 64 == 0
       */
      md5_process_block (buffer, BLOCKSIZE, &ctx);
    }

  /* Add the last bytes if necessary.  */
  if (sum > 0)
    md5_process_bytes (buffer, sum, &ctx);

  /* Construct result in desired memory.  */
  md5_finish_ctx (&ctx, resblock);
  return 0;
}

/* Compute MD5 message digest for LEN bytes beginning at BUFFER.  The
   result is always in little endian byte order, so that a byte-wise
   output yields to the wanted ASCII representation of the message
   digest.  */
void *
md5_buffer (const char *buffer, size_t len, void *resblock)
{
  struct md5_ctx ctx;

  /* Initialize the computation context.  */
  md5_init_ctx (&ctx);

  /* Process whole buffer but last len % 64 bytes.  */
  md5_process_bytes (buffer, len, &ctx);

  /* Put result in desired memory area.  */
  return md5_finish_ctx (&ctx, resblock);
}


void
md5_process_bytes (const void *buffer, size_t len, struct md5_ctx *ctx)
{
  /* When we already have some bits in our internal buffer concatenate
     both inputs first.  */
  if (ctx->buflen != 0)
    {
      size_t left_over = ctx->buflen;
      size_t add = 128 - left_over > len ? len : 128 - left_over;

      memcpy (&ctx->buffer[left_over], buffer, add);
      ctx->buflen += add;

      if (left_over + add > 64)
	{
	  md5_process_block (ctx->buffer, (left_over + add) & ~63, ctx);
	  /* The regions in the following copy operation cannot overlap.  */
	  memcpy (ctx->buffer, &ctx->buffer[(left_over + add) & ~63],
		  (left_over + add) & 63);
	  ctx->buflen = (left_over + add) & 63;
	}

      buffer = (const void *) ((const char *) buffer + add);
      len -= add;
    }

  /* Process available complete blocks.  */
  if (len > 64)
    {
#if !_STRING_ARCH_unaligned
/* To check alignment gcc has an appropriate operator.  Other
   compilers don't.  */
# if __GNUC__ >= 2
#  define UNALIGNED_P(p) (((md5_uintptr) p) % __alignof__ (md5_uint32) != 0)
# else
#  define UNALIGNED_P(p) (((md5_uintptr) p) % sizeof (md5_uint32) != 0)
# endif
      if (UNALIGNED_P (buffer))
        while (len > 64)
          {
            md5_process_block (memcpy (ctx->buffer, buffer, 64), 64, ctx);
            buffer = (const char *) buffer + 64;
            len -= 64;
          }
      else
#endif
      md5_process_block (buffer, len & ~63, ctx);
      buffer = (const void *) ((const char *) buffer + (len & ~63));
      len &= 63;
    }

  /* Move remaining bytes in internal buffer.  */
  if (len > 0)
    {
      memcpy (ctx->buffer, buffer, len);
      ctx->buflen = len;
    }
}


/* These are the four functions used in the four steps of the MD5 algorithm
   and defined in the RFC 1321.  The first function is a little bit optimized
   (as found in Colin Plumbs public domain implementation).  */
/* #define FF(b, c, d) ((b & c) | (~b & d)) */
#define FF(b, c, d) (d ^ (b & (c ^ d)))
#define FG(b, c, d) FF (d, b, c)
#define FH(b, c, d) (b ^ c ^ d)
#define FI(b, c, d) (c ^ (b | ~d))

/* Process LEN bytes of BUFFER, accumulating context into CTX.
   It is assumed that LEN % 64 == 0.  */

void
md5_process_block (const void *buffer, size_t len, struct md5_ctx *ctx)
{
  md5_uint32 correct_words[16];
  const md5_uint32 *words = (const md5_uint32 *) buffer;
  size_t nwords = len / sizeof (md5_uint32);
  const md5_uint32 *endp = words + nwords;
  md5_uint32 A = ctx->A;
  md5_uint32 B = ctx->B;
  md5_uint32 C = ctx->C;
  md5_uint32 D = ctx->D;

  /* First increment the byte count.  RFC 1321 specifies the possible
     length of the file up to 2^64 bits.  Here we only compute the
     number of bytes.  Do a double word increment.  */
  ctx->total[0] += len;
  if (ctx->total[0] < len)
    ++ctx->total[1];

  /* Process all bytes in the buffer with 64 bytes in each round of
     the loop.  */
  while (words < endp)
    {
      md5_uint32 *cwp = correct_words;
      md5_uint32 A_save = A;
      md5_uint32 B_save = B;
      md5_uint32 C_save = C;
      md5_uint32 D_save = D;

      /* First round: using the given function, the context and a constant
	 the next context is computed.  Because the algorithms processing
	 unit is a 32-bit word and it is determined to work on words in
	 little endian byte order we perhaps have to change the byte order
	 before the computation.  To reduce the work for the next steps
	 we store the swapped words in the array CORRECT_WORDS.  */

#define OP(a, b, c, d, s, T)						\
      do								\
        {								\
	  a += FF (b, c, d) + (*cwp++ = SWAP (*words)) + T;		\
	  ++words;							\
	  CYCLIC (a, s);						\
	  a += b;							\
        }								\
      while (0)

      /* It is unfortunate that C does not provide an operator for
	 cyclic rotation.  Hope the C compiler is smart enough.  */
#define CYCLIC(w, s) (w = (w << s) | (w >> (32 - s)))

      /* Before we start, one word to the strange constants.
	 They are defined in RFC 1321 as

	 T[i] = (int) (4294967296.0 * fabs (sin (i))), i=1..64
       */

      /* Round 1.  */
      OP (A, B, C, D,  7, (md5_uint32) 0xd76aa478);
      OP (D, A, B, C, 12, (md5_uint32) 0xe8c7b756);
      OP (C, D, A, B, 17, (md5_uint32) 0x242070db);
      OP (B, C, D, A, 22, (md5_uint32) 0xc1bdceee);
      OP (A, B, C, D,  7, (md5_uint32) 0xf57c0faf);
      OP (D, A, B, C, 12, (md5_uint32) 0x4787c62a);
      OP (C, D, A, B, 17, (md5_uint32) 0xa8304613);
      OP (B, C, D, A, 22, (md5_uint32) 0xfd469501);
      OP (A, B, C, D,  7, (md5_uint32) 0x698098d8);
      OP (D, A, B, C, 12, (md5_uint32) 0x8b44f7af);
      OP (C, D, A, B, 17, (md5_uint32) 0xffff5bb1);
      OP (B, C, D, A, 22, (md5_uint32) 0x895cd7be);
      OP (A, B, C, D,  7, (md5_uint32) 0x6b901122);
      OP (D, A, B, C, 12, (md5_uint32) 0xfd987193);
      OP (C, D, A, B, 17, (md5_uint32) 0xa679438e);
      OP (B, C, D, A, 22, (md5_uint32) 0x49b40821);

      /* For the second to fourth round we have the possibly swapped words
	 in CORRECT_WORDS.  Redefine the macro to take an additional first
	 argument specifying the function to use.  */
#undef OP
#define OP(a, b, c, d, k, s, T)						\
      do 								\
	{								\
	  a += FX (b, c, d) + correct_words[k] + T;			\
	  CYCLIC (a, s);						\
	  a += b;							\
	}								\
      while (0)

#define FX(b, c, d) FG (b, c, d)

      /* Round 2.  */
      OP (A, B, C, D,  1,  5, (md5_uint32) 0xf61e2562);
      OP (D, A, B, C,  6,  9, (md5_uint32) 0xc040b340);
      OP (C, D, A, B, 11, 14, (md5_uint32) 0x265e5a51);
      OP (B, C, D, A,  0, 20, (md5_uint32) 0xe9b6c7aa);
      OP (A, B, C, D,  5,  5, (md5_uint32) 0xd62f105d);
      OP (D, A, B, C, 10,  9, (md5_uint32) 0x02441453);
      OP (C, D, A, B, 15, 14, (md5_uint32) 0xd8a1e681);
      OP (B, C, D, A,  4, 20, (md5_uint32) 0xe7d3fbc8);
      OP (A, B, C, D,  9,  5, (md5_uint32) 0x21e1cde6);
      OP (D, A, B, C, 14,  9, (md5_uint32) 0xc33707d6);
      OP (C, D, A, B,  3, 14, (md5_uint32) 0xf4d50d87);
      OP (B, C, D, A,  8, 20, (md5_uint32) 0x455a14ed);
      OP (A, B, C, D, 13,  5, (md5_uint32) 0xa9e3e905);
      OP (D, A, B, C,  2,  9, (md5_uint32) 0xfcefa3f8);
      OP (C, D, A, B,  7, 14, (md5_uint32) 0x676f02d9);
      OP (B, C, D, A, 12, 20, (md5_uint32) 0x8d2a4c8a);

#undef FX
#define FX(b, c, d) FH (b, c, d)

      /* Round 3.  */
      OP (A, B, C, D,  5,  4, (md5_uint32) 0xfffa3942);
      OP (D, A, B, C,  8, 11, (md5_uint32) 0x8771f681);
      OP (C, D, A, B, 11, 16, (md5_uint32) 0x6d9d6122);
      OP (B, C, D, A, 14, 23, (md5_uint32) 0xfde5380c);
      OP (A, B, C, D,  1,  4, (md5_uint32) 0xa4beea44);
      OP (D, A, B, C,  4, 11, (md5_uint32) 0x4bdecfa9);
      OP (C, D, A, B,  7, 16, (md5_uint32) 0xf6bb4b60);
      OP (B, C, D, A, 10, 23, (md5_uint32) 0xbebfbc70);
      OP (A, B, C, D, 13,  4, (md5_uint32) 0x289b7ec6);
      OP (D, A, B, C,  0, 11, (md5_uint32) 0xeaa127fa);
      OP (C, D, A, B,  3, 16, (md5_uint32) 0xd4ef3085);
      OP (B, C, D, A,  6, 23, (md5_uint32) 0x04881d05);
      OP (A, B, C, D,  9,  4, (md5_uint32) 0xd9d4d039);
      OP (D, A, B, C, 12, 11, (md5_uint32) 0xe6db99e5);
      OP (C, D, A, B, 15, 16, (md5_uint32) 0x1fa27cf8);
      OP (B, C, D, A,  2, 23, (md5_uint32) 0xc4ac5665);

#undef FX
#define FX(b, c, d) FI (b, c, d)

      /* Round 4.  */
      OP (A, B, C, D,  0,  6, (md5_uint32) 0xf4292244);
      OP (D, A, B, C,  7, 10, (md5_uint32) 0x432aff97);
      OP (C, D, A, B, 14, 15, (md5_uint32) 0xab9423a7);
      OP (B, C, D, A,  5, 21, (md5_uint32) 0xfc93a039);
      OP (A, B, C, D, 12,  6, (md5_uint32) 0x655b59c3);
      OP (D, A, B, C,  3, 10, (md5_uint32) 0x8f0ccc92);
      OP (C, D, A, B, 10, 15, (md5_uint32) 0xffeff47d);
      OP (B, C, D, A,  1, 21, (md5_uint32) 0x85845dd1);
      OP (A, B, C, D,  8,  6, (md5_uint32) 0x6fa87e4f);
      OP (D, A, B, C, 15, 10, (md5_uint32) 0xfe2ce6e0);
      OP (C, D, A, B,  6, 15, (md5_uint32) 0xa3014314);
      OP (B, C, D, A, 13, 21, (md5_uint32) 0x4e0811a1);
      OP (A, B, C, D,  4,  6, (md5_uint32) 0xf7537e82);
      OP (D, A, B, C, 11, 10, (md5_uint32) 0xbd3af235);
      OP (C, D, A, B,  2, 15, (md5_uint32) 0x2ad7d2bb);
      OP (B, C, D, A,  9, 21, (md5_uint32) 0xeb86d391);

      /* Add the starting values of the context.  */
      A += A_save;
      B += B_save;
      C += C_save;
      D += D_save;
    }

  /* Put checksum in context given as argument.  */
  ctx->A = A;
  ctx->B = B;
  ctx->C = C;
  ctx->D = D;
}
