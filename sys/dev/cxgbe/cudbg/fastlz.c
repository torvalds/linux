/*
   FastLZ - lightning-fast lossless compression library

   Copyright (C) 2007 Ariya Hidayat (ariya@kde.org)
   Copyright (C) 2006 Ariya Hidayat (ariya@kde.org)
   Copyright (C) 2005 Ariya Hidayat (ariya@kde.org)

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
   */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "osdep.h"
#include "fastlz.h"

#if !defined(FASTLZ__COMPRESSOR) && !defined(FASTLZ_DECOMPRESSOR)

/*
 * Always check for bound when decompressing.
 * Generally it is best to leave it defined.
 */
#define FASTLZ_SAFE

#if defined(WIN32) || defined(__NT__) || defined(_WIN32) || defined(__WIN32__)
#if defined(_MSC_VER) || defined(__GNUC__)
/* #include <windows.h> */
#pragma warning(disable : 4242)
#pragma warning(disable : 4244)
#endif
#endif

/*
 * Give hints to the compiler for branch prediction optimization.
 */
#if defined(__GNUC__) && (__GNUC__ > 2)
#define FASTLZ_EXPECT_CONDITIONAL(c)	(__builtin_expect((c), 1))
#define FASTLZ_UNEXPECT_CONDITIONAL(c)	(__builtin_expect((c), 0))
#else
#define FASTLZ_EXPECT_CONDITIONAL(c)	(c)
#define FASTLZ_UNEXPECT_CONDITIONAL(c)	(c)
#endif

/*
 * Use inlined functions for supported systems.
 */
#if defined(__GNUC__) || defined(__DMC__) || defined(__POCC__) ||\
	defined(__WATCOMC__) || defined(__SUNPRO_C)
#define FASTLZ_INLINE inline
#elif defined(__BORLANDC__) || defined(_MSC_VER) || defined(__LCC__)
#define FASTLZ_INLINE __inline
#else
#define FASTLZ_INLINE
#endif

/*
 * Prevent accessing more than 8-bit at once, except on x86 architectures.
 */
#if !defined(FASTLZ_STRICT_ALIGN)
#define FASTLZ_STRICT_ALIGN
#if defined(__i386__) || defined(__386)  /* GNU C, Sun Studio */
#undef FASTLZ_STRICT_ALIGN
#elif defined(__i486__) || defined(__i586__) || defined(__i686__) /* GNU C */
#undef FASTLZ_STRICT_ALIGN
#elif defined(_M_IX86) /* Intel, MSVC */
#undef FASTLZ_STRICT_ALIGN
#elif defined(__386)
#undef FASTLZ_STRICT_ALIGN
#elif defined(_X86_) /* MinGW */
#undef FASTLZ_STRICT_ALIGN
#elif defined(__I86__) /* Digital Mars */
#undef FASTLZ_STRICT_ALIGN
#endif
#endif

/*
 * FIXME: use preprocessor magic to set this on different platforms!
 */

#define MAX_COPY       32
#define MAX_LEN       264  /* 256 + 8 */
#define MAX_DISTANCE 8192

#if !defined(FASTLZ_STRICT_ALIGN)
#define FASTLZ_READU16(p) (*((const unsigned short *)(p)))
#else
#define FASTLZ_READU16(p) ((p)[0] | (p)[1]<<8)
#endif

#define HASH_LOG  13
#define HASH_SIZE (1 << HASH_LOG)
#define HASH_MASK  (HASH_SIZE - 1)
#define HASH_FUNCTION(v, p) {\
				v = FASTLZ_READU16(p);\
				v ^= FASTLZ_READU16(p + 1)^\
				     (v>>(16 - HASH_LOG));\
				v &= HASH_MASK;\
			    }

#undef FASTLZ_LEVEL
#define FASTLZ_LEVEL 1

#undef FASTLZ_COMPRESSOR
#undef FASTLZ_DECOMPRESSOR
#define FASTLZ_COMPRESSOR fastlz1_compress
#define FASTLZ_DECOMPRESSOR fastlz1_decompress
static FASTLZ_INLINE int FASTLZ_COMPRESSOR(const void *input, int length,
					   void *output);
static FASTLZ_INLINE int FASTLZ_DECOMPRESSOR(const void *input, int length,
					     void *output, int maxout);
#include "fastlz.c"

#undef FASTLZ_LEVEL
#define FASTLZ_LEVEL 2

#undef MAX_DISTANCE
#define MAX_DISTANCE 8191
#define MAX_FARDISTANCE (65535 + MAX_DISTANCE - 1)

#undef FASTLZ_COMPRESSOR
#undef FASTLZ_DECOMPRESSOR
#define FASTLZ_COMPRESSOR fastlz2_compress
#define FASTLZ_DECOMPRESSOR fastlz2_decompress
static FASTLZ_INLINE int FASTLZ_COMPRESSOR(const void *input, int length,
					   void *output);
static FASTLZ_INLINE int FASTLZ_DECOMPRESSOR(const void *input, int length,
					     void *output, int maxout);
#include "fastlz.c"

int fastlz_compress(const void *input, int length, void *output)
{
	/* for short block, choose fastlz1 */
	if (length < 65536)
		return fastlz1_compress(input, length, output);

	/* else... */
	return fastlz2_compress(input, length, output);
}

int fastlz_decompress(const void *input, int length, void *output, int maxout)
{
	/* magic identifier for compression level */
	int level = ((*(const unsigned char *)input) >> 5) + 1;

	if (level == 1)
		return fastlz1_decompress(input, length, output, maxout);
	if (level == 2)
		return fastlz2_decompress(input, length, output, maxout);

	/* unknown level, trigger error */
	return 0;
}

int fastlz_compress_level(int level, const void *input, int length,
			  void *output)
{
	if (level == 1)
		return fastlz1_compress(input, length, output);
	if (level == 2)
		return fastlz2_compress(input, length, output);

	return 0;
}

#else /* !defined(FASTLZ_COMPRESSOR) && !defined(FASTLZ_DECOMPRESSOR) */


static FASTLZ_INLINE int FASTLZ_COMPRESSOR(const void *input, int length,
					   void *output)
{
	const unsigned char *ip = (const unsigned char *) input;
	const unsigned char *ip_bound = ip + length - 2;
	const unsigned char *ip_limit = ip + length - 12;
	unsigned char *op = (unsigned char *) output;
	static const unsigned char *g_htab[HASH_SIZE];

	const unsigned char **htab = g_htab;
	const unsigned char **hslot;
	unsigned int hval;

	unsigned int copy;

	/* sanity check */
	if (FASTLZ_UNEXPECT_CONDITIONAL(length < 4)) {
		if (length) {
			/* create literal copy only */
			*op++ = length - 1;
			ip_bound++;
			while (ip <= ip_bound)
				*op++ = *ip++;
			return length + 1;
		} else
			return 0;
	}

	/* initializes hash table */
	for (hslot = htab; hslot < htab + HASH_SIZE; hslot++)
		*hslot = ip;

	/* we start with literal copy */
	copy = 2;
	*op++ = MAX_COPY - 1;
	*op++ = *ip++;
	*op++ = *ip++;

	/* main loop */
	while (FASTLZ_EXPECT_CONDITIONAL(ip < ip_limit)) {
		const unsigned char *ref;
		unsigned int distance;

		/* minimum match length */
		unsigned int len = 3;

		/* comparison starting-point */
		const unsigned char *anchor = ip;

		/* check for a run */
#if FASTLZ_LEVEL == 2
		if (ip[0] == ip[-1] &&
		    FASTLZ_READU16(ip - 1) == FASTLZ_READU16(ip + 1)) {
			distance = 1;
			ip += 3;
			ref = anchor - 1 + 3;
			goto match;
		}
#endif

		/* find potential match */
		HASH_FUNCTION(hval, ip);
		hslot = htab + hval;
		ref = htab[hval];

		/* calculate distance to the match */
		distance = anchor - ref;

		/* update hash table */
		*hslot = anchor;

		if (!ref)
			goto literal;
		/* is this a match? check the first 3 bytes */
		if (distance == 0 ||
#if FASTLZ_LEVEL == 1
				(distance >= MAX_DISTANCE) ||
#else
				(distance >= MAX_FARDISTANCE) ||
#endif
				*ref++ != *ip++ || *ref++ != *ip++ ||
				*ref++ != *ip++)
			goto literal;

#if FASTLZ_LEVEL == 2
		/* far, needs at least 5-byte match */
		if (distance >= MAX_DISTANCE) {
			if (*ip++ != *ref++ || *ip++ != *ref++)
				goto literal;
			len += 2;
		}

match:
#endif

		/* last matched byte */
		ip = anchor + len;

		/* distance is biased */
		distance--;

		if (!distance) {
			/* zero distance means a run */
			unsigned char x = ip[-1];
			while (ip < ip_bound)
				if (*ref++ != x)
					break;
				else
					ip++;
		} else
			for (;;) {
				/* safe because the outer check
				 * against ip limit */
				if (*ref++ != *ip++)
					break;
				if (*ref++ != *ip++)
					break;
				if (*ref++ != *ip++)
					break;
				if (*ref++ != *ip++)
					break;
				if (*ref++ != *ip++)
					break;
				if (*ref++ != *ip++)
					break;
				if (*ref++ != *ip++)
					break;
				if (*ref++ != *ip++)
					break;
				while (ip < ip_bound)
					if (*ref++ != *ip++)
						break;
				break;
			}

		/* if we have copied something, adjust the copy count */
		if (copy)
			/* copy is biased, '0' means 1 byte copy */
			*(op - copy - 1) = copy - 1;
		else
			/* back, to overwrite the copy count */
			op--;

		/* reset literal counter */
		copy = 0;

		/* length is biased, '1' means a match of 3 bytes */
		ip -= 3;
		len = ip - anchor;

		/* encode the match */
#if FASTLZ_LEVEL == 2
		if (distance < MAX_DISTANCE) {
			if (len < 7) {
				*op++ = (len << 5) + (distance >> 8);
				*op++ = (distance & 255);
			} else {
				*op++ = (7 << 5) + (distance >> 8);
				for (len -= 7; len >= 255; len -= 255)
					*op++ = 255;
				*op++ = len;
				*op++ = (distance & 255);
			}
		} else {
			/* far away, but not yet in the another galaxy... */
			if (len < 7) {
				distance -= MAX_DISTANCE;
				*op++ = (len << 5) + 31;
				*op++ = 255;
				*op++ = distance >> 8;
				*op++ = distance & 255;
			} else {
				distance -= MAX_DISTANCE;
				*op++ = (7 << 5) + 31;
				for (len -= 7; len >= 255; len -= 255)
					*op++ = 255;
				*op++ = len;
				*op++ = 255;
				*op++ = distance >> 8;
				*op++ = distance & 255;
			}
		}
#else

		if (FASTLZ_UNEXPECT_CONDITIONAL(len > MAX_LEN - 2))
			while (len > MAX_LEN - 2) {
				*op++ = (7 << 5) + (distance >> 8);
				*op++ = MAX_LEN - 2 - 7 - 2;
				*op++ = (distance & 255);
				len -= MAX_LEN - 2;
			}

		if (len < 7) {
			*op++ = (len << 5) + (distance >> 8);
			*op++ = (distance & 255);
		} else {
			*op++ = (7 << 5) + (distance >> 8);
			*op++ = len - 7;
			*op++ = (distance & 255);
		}
#endif

		/* update the hash at match boundary */
		HASH_FUNCTION(hval, ip);
		htab[hval] = ip++;
		HASH_FUNCTION(hval, ip);
		htab[hval] = ip++;

		/* assuming literal copy */
		*op++ = MAX_COPY - 1;

		continue;

literal:
		*op++ = *anchor++;
		ip = anchor;
		copy++;
		if (FASTLZ_UNEXPECT_CONDITIONAL(copy == MAX_COPY)) {
			copy = 0;
			*op++ = MAX_COPY - 1;
		}
	}

	/* left-over as literal copy */
	ip_bound++;
	while (ip <= ip_bound) {
		*op++ = *ip++;
		copy++;
		if (copy == MAX_COPY) {
			copy = 0;
			*op++ = MAX_COPY - 1;
		}
	}

	/* if we have copied something, adjust the copy length */
	if (copy)
		*(op - copy - 1) = copy - 1;
	else
		op--;

#if FASTLZ_LEVEL == 2
	/* marker for fastlz2 */
	*(unsigned char *)output |= (1 << 5);
#endif

	return op - (unsigned char *)output;
}

static FASTLZ_INLINE int FASTLZ_DECOMPRESSOR(const void *input, int length,
					     void *output, int maxout)
{
	const unsigned char *ip = (const unsigned char *) input;
	const unsigned char *ip_limit  = ip + length;
	unsigned char *op = (unsigned char *) output;
	unsigned char *op_limit = op + maxout;
	unsigned int ctrl = (*ip++) & 31;
	int loop = 1;

	do {
		const unsigned char *ref = op;
		unsigned int len = ctrl >> 5;
		unsigned int ofs = (ctrl & 31) << 8;

		if (ctrl >= 32) {
#if FASTLZ_LEVEL == 2
			unsigned char code;
#endif
			len--;
			ref -= ofs;
			if (len == 7 - 1)
#if FASTLZ_LEVEL == 1
				len += *ip++;
			ref -= *ip++;
#else
			do {
				code = *ip++;
				len += code;
			} while (code == 255);
			code = *ip++;
			ref -= code;

			/* match from 16-bit distance */
			if (FASTLZ_UNEXPECT_CONDITIONAL(code == 255))
				if (FASTLZ_EXPECT_CONDITIONAL(ofs ==
							      (31 << 8))) {
					ofs = (*ip++) << 8;
					ofs += *ip++;
					ref = op - ofs - MAX_DISTANCE;
				}
#endif

#ifdef FASTLZ_SAFE
			if (FASTLZ_UNEXPECT_CONDITIONAL(op + len + 3 >
							op_limit))
				return 0;

			if (FASTLZ_UNEXPECT_CONDITIONAL(ref - 1 <
							(unsigned char *)output)
						       )
				return 0;
#endif

			if (FASTLZ_EXPECT_CONDITIONAL(ip < ip_limit))
				ctrl = *ip++;
			else
				loop = 0;

			if (ref == op) {
				/* optimize copy for a run */
				unsigned char b = ref[-1];
				*op++ = b;
				*op++ = b;
				*op++ = b;
				for (; len; --len)
					*op++ = b;
			} else {
#if !defined(FASTLZ_STRICT_ALIGN)
				const unsigned short *p;
				unsigned short *q;
#endif
				/* copy from reference */
				ref--;
				*op++ = *ref++;
				*op++ = *ref++;
				*op++ = *ref++;

#if !defined(FASTLZ_STRICT_ALIGN)
				/* copy a byte, so that now it's word aligned */
				if (len & 1) {
					*op++ = *ref++;
					len--;
				}

				/* copy 16-bit at once */
				q = (unsigned short *) op;
				op += len;
				p = (const unsigned short *) ref;
				for (len >>= 1; len > 4; len -= 4) {
					*q++ = *p++;
					*q++ = *p++;
					*q++ = *p++;
					*q++ = *p++;
				}
				for (; len; --len)
					*q++ = *p++;
#else
				for (; len; --len)
					*op++ = *ref++;
#endif
			}
		} else {
			ctrl++;
#ifdef FASTLZ_SAFE
			if (FASTLZ_UNEXPECT_CONDITIONAL(op + ctrl > op_limit))
				return 0;
			if (FASTLZ_UNEXPECT_CONDITIONAL(ip + ctrl > ip_limit))
				return 0;
#endif

			*op++ = *ip++;
			for (--ctrl; ctrl; ctrl--)
				*op++ = *ip++;

			loop = FASTLZ_EXPECT_CONDITIONAL(ip < ip_limit);
			if (loop)
				ctrl = *ip++;
		}
	} while (FASTLZ_EXPECT_CONDITIONAL(loop));

	return op - (unsigned char *)output;
}

#endif /* !defined(FASTLZ_COMPRESSOR) && !defined(FASTLZ_DECOMPRESSOR) */
