/*
 * LZ4 - Fast LZ compression algorithm
 * Header File
 * Copyright (C) 2011-2013, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at :
 * - LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
 * - LZ4 source repository : http://code.google.com/p/lz4/
 */
/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>

static int real_LZ4_compress(const char *source, char *dest, int isize,
    int osize);
static int LZ4_compressBound(int isize);
static int LZ4_uncompress_unknownOutputSize(const char *source, char *dest,
    int isize, int maxOutputSize);
static int LZ4_compressCtx(void *ctx, const char *source, char *dest,
    int isize, int osize);
static int LZ4_compress64kCtx(void *ctx, const char *source, char *dest,
    int isize, int osize);

static kmem_cache_t *lz4_ctx_cache;

/*ARGSUSED*/
size_t
lz4_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
	uint32_t bufsiz;
	char *dest = d_start;

	ASSERT(d_len >= sizeof (bufsiz));

	bufsiz = real_LZ4_compress(s_start, &dest[sizeof (bufsiz)], s_len,
	    d_len - sizeof (bufsiz));

	/* Signal an error if the compression routine returned zero. */
	if (bufsiz == 0)
		return (s_len);

	/*
	 * Encode the compresed buffer size at the start. We'll need this in
	 * decompression to counter the effects of padding which might be
	 * added to the compressed buffer and which, if unhandled, would
	 * confuse the hell out of our decompression function.
	 */
	*(uint32_t *)dest = BE_32(bufsiz);

	return (bufsiz + sizeof (bufsiz));
}

/*ARGSUSED*/
int
lz4_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
	const char *src = s_start;
	uint32_t bufsiz = BE_IN32(src);

	/* invalid compressed buffer size encoded at start */
	if (bufsiz + sizeof (bufsiz) > s_len)
		return (1);

	/*
	 * Returns 0 on success (decompression function returned non-negative)
	 * and non-zero on failure (decompression function returned negative).
	 */
	return (LZ4_uncompress_unknownOutputSize(&src[sizeof (bufsiz)],
	    d_start, bufsiz, d_len) < 0);
}

/*
 * LZ4 API Description:
 *
 * Simple Functions:
 * real_LZ4_compress() :
 * 	isize  : is the input size. Max supported value is ~1.9GB
 * 	return : the number of bytes written in buffer dest
 *		 or 0 if the compression fails (if LZ4_COMPRESSMIN is set).
 * 	note : destination buffer must be already allocated.
 * 		destination buffer must be sized to handle worst cases
 * 		situations (input data not compressible) worst case size
 * 		evaluation is provided by function LZ4_compressBound().
 *
 * Advanced Functions
 *
 * LZ4_compressBound() :
 * 	Provides the maximum size that LZ4 may output in a "worst case"
 * 	scenario (input data not compressible) primarily useful for memory
 * 	allocation of output buffer.
 *
 * 	isize  : is the input size. Max supported value is ~1.9GB
 * 	return : maximum output size in a "worst case" scenario
 * 	note : this function is limited by "int" range (2^31-1)
 *
 * LZ4_uncompress_unknownOutputSize() :
 * 	isize  : is the input size, therefore the compressed size
 * 	maxOutputSize : is the size of the destination buffer (which must be
 * 		already allocated)
 * 	return : the number of bytes decoded in the destination buffer
 * 		(necessarily <= maxOutputSize). If the source stream is
 * 		malformed, the function will stop decoding and return a
 * 		negative result, indicating the byte position of the faulty
 * 		instruction. This function never writes beyond dest +
 * 		maxOutputSize, and is therefore protected against malicious
 * 		data packets.
 * 	note   : Destination buffer must be already allocated.
 *
 * LZ4_compressCtx() :
 * 	This function explicitly handles the CTX memory structure.
 *
 * 	ILLUMOS CHANGES: the CTX memory structure must be explicitly allocated
 * 	by the caller (either on the stack or using kmem_zalloc). Passing NULL
 * 	isn't valid.
 *
 * LZ4_compress64kCtx() :
 * 	Same as LZ4_compressCtx(), but specific to small inputs (<64KB).
 * 	isize *Must* be <64KB, otherwise the output will be corrupted.
 *
 * 	ILLUMOS CHANGES: the CTX memory structure must be explicitly allocated
 * 	by the caller (either on the stack or using kmem_zalloc). Passing NULL
 * 	isn't valid.
 */

/*
 * Tuning parameters
 */

/*
 * COMPRESSIONLEVEL: Increasing this value improves compression ratio
 *	 Lowering this value reduces memory usage. Reduced memory usage
 *	typically improves speed, due to cache effect (ex: L1 32KB for Intel,
 *	L1 64KB for AMD). Memory usage formula : N->2^(N+2) Bytes
 *	(examples : 12 -> 16KB ; 17 -> 512KB)
 */
#define	COMPRESSIONLEVEL 12

/*
 * NOTCOMPRESSIBLE_CONFIRMATION: Decreasing this value will make the
 *	algorithm skip faster data segments considered "incompressible".
 *	This may decrease compression ratio dramatically, but will be
 *	faster on incompressible data. Increasing this value will make
 *	the algorithm search more before declaring a segment "incompressible".
 *	This could improve compression a bit, but will be slower on
 *	incompressible data. The default value (6) is recommended.
 */
#define	NOTCOMPRESSIBLE_CONFIRMATION 6

/*
 * BIG_ENDIAN_NATIVE_BUT_INCOMPATIBLE: This will provide a boost to
 * performance for big endian cpu, but the resulting compressed stream
 * will be incompatible with little-endian CPU. You can set this option
 * to 1 in situations where data will stay within closed environment.
 * This option is useless on Little_Endian CPU (such as x86).
 */
/* #define	BIG_ENDIAN_NATIVE_BUT_INCOMPATIBLE 1 */

/*
 * CPU Feature Detection
 */

/* 32 or 64 bits ? */
#if (defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || \
    defined(__amd64) || defined(__ppc64__) || defined(_WIN64) || \
    defined(__LP64__) || defined(_LP64))
#define	LZ4_ARCH64 1
#else
#define	LZ4_ARCH64 0
#endif

/*
 * Limits the amount of stack space that the algorithm may consume to hold
 * the compression lookup table. The value `9' here means we'll never use
 * more than 2k of stack (see above for a description of COMPRESSIONLEVEL).
 * If more memory is needed, it is allocated from the heap.
 */
/* FreeBSD: Use heap for all platforms for now */
#define	STACKLIMIT 0

/*
 * Little Endian or Big Endian?
 * Note: overwrite the below #define if you know your architecture endianess.
 */
#if BYTE_ORDER == BIG_ENDIAN
#define	LZ4_BIG_ENDIAN 1
#else
/*
 * Little Endian assumed. PDP Endian and other very rare endian format
 * are unsupported.
 */
#endif

/*
 * Unaligned memory access is automatically enabled for "common" CPU,
 * such as x86. For others CPU, the compiler will be more cautious, and
 * insert extra code to ensure aligned access is respected. If you know
 * your target CPU supports unaligned memory access, you may want to
 * force this option manually to improve performance
 */
#if defined(__ARM_FEATURE_UNALIGNED)
#define	LZ4_FORCE_UNALIGNED_ACCESS 1
#endif

/*
 * FreeBSD: can't use GCC's __builtin_ctz when using sparc64 because
 * gcc currently rely on libcompiler_rt.
 *
 * TODO: revisit this when situation changes.
 */
#if defined(__sparc64__)
#define	LZ4_FORCE_SW_BITCOUNT
#endif

/*
 * Compiler Options
 */
#if __STDC_VERSION__ >= 199901L	/* C99 */
/* "restrict" is a known keyword */
#else
/* Disable restrict */
#define	restrict
#endif

#define	lz4_bswap16(x) ((unsigned short int) ((((x) >> 8) & 0xffu) | \
	(((x) & 0xffu) << 8)))

#define	expect(expr, value)    (__builtin_expect((expr), (value)))

#if defined(likely)
#undef likely
#endif
#if defined(unlikely)
#undef unlikely
#endif

#define	likely(expr)	expect((expr) != 0, 1)
#define	unlikely(expr)	expect((expr) != 0, 0)

/* Basic types */
#define	BYTE	uint8_t
#define	U16	uint16_t
#define	U32	uint32_t
#define	S32	int32_t
#define	U64	uint64_t

#ifndef LZ4_FORCE_UNALIGNED_ACCESS
#pragma pack(1)
#endif

typedef struct _U16_S {
	U16 v;
} U16_S;
typedef struct _U32_S {
	U32 v;
} U32_S;
typedef struct _U64_S {
	U64 v;
} U64_S;

#ifndef LZ4_FORCE_UNALIGNED_ACCESS
#pragma pack()
#endif

#define	A64(x) (((U64_S *)(x))->v)
#define	A32(x) (((U32_S *)(x))->v)
#define	A16(x) (((U16_S *)(x))->v)

/*
 * Constants
 */
#define	MINMATCH 4

#define	HASH_LOG COMPRESSIONLEVEL
#define	HASHTABLESIZE (1 << HASH_LOG)
#define	HASH_MASK (HASHTABLESIZE - 1)

#define	SKIPSTRENGTH (NOTCOMPRESSIBLE_CONFIRMATION > 2 ? \
	NOTCOMPRESSIBLE_CONFIRMATION : 2)

/*
 * Defines if memory is allocated into the stack (local variable),
 * or into the heap (kmem_alloc()).
 */
#define	HEAPMODE (HASH_LOG > STACKLIMIT)
#define	COPYLENGTH 8
#define	LASTLITERALS 5
#define	MFLIMIT (COPYLENGTH + MINMATCH)
#define	MINLENGTH (MFLIMIT + 1)

#define	MAXD_LOG 16
#define	MAX_DISTANCE ((1 << MAXD_LOG) - 1)

#define	ML_BITS 4
#define	ML_MASK ((1U<<ML_BITS)-1)
#define	RUN_BITS (8-ML_BITS)
#define	RUN_MASK ((1U<<RUN_BITS)-1)


/*
 * Architecture-specific macros
 */
#if LZ4_ARCH64
#define	STEPSIZE 8
#define	UARCH U64
#define	AARCH A64
#define	LZ4_COPYSTEP(s, d)	A64(d) = A64(s); d += 8; s += 8;
#define	LZ4_COPYPACKET(s, d)	LZ4_COPYSTEP(s, d)
#define	LZ4_SECURECOPY(s, d, e)	if (d < e) LZ4_WILDCOPY(s, d, e)
#define	HTYPE U32
#define	INITBASE(base)		const BYTE* const base = ip
#else /* !LZ4_ARCH64 */
#define	STEPSIZE 4
#define	UARCH U32
#define	AARCH A32
#define	LZ4_COPYSTEP(s, d)	A32(d) = A32(s); d += 4; s += 4;
#define	LZ4_COPYPACKET(s, d)	LZ4_COPYSTEP(s, d); LZ4_COPYSTEP(s, d);
#define	LZ4_SECURECOPY		LZ4_WILDCOPY
#define	HTYPE const BYTE *
#define	INITBASE(base)		const int base = 0
#endif /* !LZ4_ARCH64 */

#if (defined(LZ4_BIG_ENDIAN) && !defined(BIG_ENDIAN_NATIVE_BUT_INCOMPATIBLE))
#define	LZ4_READ_LITTLEENDIAN_16(d, s, p) \
	{ U16 v = A16(p); v = lz4_bswap16(v); d = (s) - v; }
#define	LZ4_WRITE_LITTLEENDIAN_16(p, i) \
	{ U16 v = (U16)(i); v = lz4_bswap16(v); A16(p) = v; p += 2; }
#else
#define	LZ4_READ_LITTLEENDIAN_16(d, s, p) { d = (s) - A16(p); }
#define	LZ4_WRITE_LITTLEENDIAN_16(p, v)  { A16(p) = v; p += 2; }
#endif


/* Local structures */
struct refTables {
	HTYPE hashTable[HASHTABLESIZE];
};


/* Macros */
#define	LZ4_HASH_FUNCTION(i) (((i) * 2654435761U) >> ((MINMATCH * 8) - \
	HASH_LOG))
#define	LZ4_HASH_VALUE(p) LZ4_HASH_FUNCTION(A32(p))
#define	LZ4_WILDCOPY(s, d, e) do { LZ4_COPYPACKET(s, d) } while (d < e);
#define	LZ4_BLINDCOPY(s, d, l) { BYTE* e = (d) + l; LZ4_WILDCOPY(s, d, e); \
	d = e; }


/* Private functions */
#if LZ4_ARCH64

static inline int
LZ4_NbCommonBytes(register U64 val)
{
#if defined(LZ4_BIG_ENDIAN)
#if !defined(LZ4_FORCE_SW_BITCOUNT)
	return (__builtin_clzll(val) >> 3);
#else
	int r;
	if (!(val >> 32)) {
		r = 4;
	} else {
		r = 0;
		val >>= 32;
	}
	if (!(val >> 16)) {
		r += 2;
		val >>= 8;
	} else {
		val >>= 24;
	}
	r += (!val);
	return (r);
#endif
#else
#if !defined(LZ4_FORCE_SW_BITCOUNT)
	return (__builtin_ctzll(val) >> 3);
#else
	static const int DeBruijnBytePos[64] =
	    { 0, 0, 0, 0, 0, 1, 1, 2, 0, 3, 1, 3, 1, 4, 2, 7, 0, 2, 3, 6, 1, 5,
		3, 5, 1, 3, 4, 4, 2, 5, 6, 7, 7, 0, 1, 2, 3, 3, 4, 6, 2, 6, 5,
		5, 3, 4, 5, 6, 7, 1, 2, 4, 6, 4,
		4, 5, 7, 2, 6, 5, 7, 6, 7, 7
	};
	return DeBruijnBytePos[((U64) ((val & -val) * 0x0218A392CDABBD3F)) >>
	    58];
#endif
#endif
}

#else

static inline int
LZ4_NbCommonBytes(register U32 val)
{
#if defined(LZ4_BIG_ENDIAN)
#if !defined(LZ4_FORCE_SW_BITCOUNT)
	return (__builtin_clz(val) >> 3);
#else
	int r;
	if (!(val >> 16)) {
		r = 2;
		val >>= 8;
	} else {
		r = 0;
		val >>= 24;
	}
	r += (!val);
	return (r);
#endif
#else
#if !defined(LZ4_FORCE_SW_BITCOUNT)
	return (__builtin_ctz(val) >> 3);
#else
	static const int DeBruijnBytePos[32] = {
		0, 0, 3, 0, 3, 1, 3, 0,
		3, 2, 2, 1, 3, 2, 0, 1,
		3, 3, 1, 2, 2, 2, 2, 0,
		3, 1, 2, 0, 1, 0, 1, 1
	};
	return DeBruijnBytePos[((U32) ((val & -(S32) val) * 0x077CB531U)) >>
	    27];
#endif
#endif
}

#endif

/* Public functions */

static int
LZ4_compressBound(int isize)
{
	return (isize + (isize / 255) + 16);
}

/* Compression functions */

/*ARGSUSED*/
static int
LZ4_compressCtx(void *ctx, const char *source, char *dest, int isize,
    int osize)
{
#if HEAPMODE
	struct refTables *srt = (struct refTables *)ctx;
	HTYPE *HashTable = (HTYPE *) (srt->hashTable);
#else
	HTYPE HashTable[HASHTABLESIZE] = { 0 };
#endif

	const BYTE *ip = (BYTE *) source;
	INITBASE(base);
	const BYTE *anchor = ip;
	const BYTE *const iend = ip + isize;
	const BYTE *const oend = (BYTE *) dest + osize;
	const BYTE *const mflimit = iend - MFLIMIT;
#define	matchlimit (iend - LASTLITERALS)

	BYTE *op = (BYTE *) dest;

	int len, length;
	const int skipStrength = SKIPSTRENGTH;
	U32 forwardH;


	/* Init */
	if (isize < MINLENGTH)
		goto _last_literals;

	/* First Byte */
	HashTable[LZ4_HASH_VALUE(ip)] = ip - base;
	ip++;
	forwardH = LZ4_HASH_VALUE(ip);

	/* Main Loop */
	for (;;) {
		int findMatchAttempts = (1U << skipStrength) + 3;
		const BYTE *forwardIp = ip;
		const BYTE *ref;
		BYTE *token;

		/* Find a match */
		do {
			U32 h = forwardH;
			int step = findMatchAttempts++ >> skipStrength;
			ip = forwardIp;
			forwardIp = ip + step;

			if unlikely(forwardIp > mflimit) {
				goto _last_literals;
			}

			forwardH = LZ4_HASH_VALUE(forwardIp);
			ref = base + HashTable[h];
			HashTable[h] = ip - base;

		} while ((ref < ip - MAX_DISTANCE) || (A32(ref) != A32(ip)));

		/* Catch up */
		while ((ip > anchor) && (ref > (BYTE *) source) &&
		    unlikely(ip[-1] == ref[-1])) {
			ip--;
			ref--;
		}

		/* Encode Literal length */
		length = ip - anchor;
		token = op++;

		/* Check output limit */
		if unlikely(op + length + (2 + 1 + LASTLITERALS) +
		    (length >> 8) > oend)
			return (0);

		if (length >= (int)RUN_MASK) {
			*token = (RUN_MASK << ML_BITS);
			len = length - RUN_MASK;
			for (; len > 254; len -= 255)
				*op++ = 255;
			*op++ = (BYTE)len;
		} else
			*token = (length << ML_BITS);

		/* Copy Literals */
		LZ4_BLINDCOPY(anchor, op, length);

		_next_match:
		/* Encode Offset */
		LZ4_WRITE_LITTLEENDIAN_16(op, ip - ref);

		/* Start Counting */
		ip += MINMATCH;
		ref += MINMATCH;	/* MinMatch verified */
		anchor = ip;
		while likely(ip < matchlimit - (STEPSIZE - 1)) {
			UARCH diff = AARCH(ref) ^ AARCH(ip);
			if (!diff) {
				ip += STEPSIZE;
				ref += STEPSIZE;
				continue;
			}
			ip += LZ4_NbCommonBytes(diff);
			goto _endCount;
		}
#if LZ4_ARCH64
		if ((ip < (matchlimit - 3)) && (A32(ref) == A32(ip))) {
			ip += 4;
			ref += 4;
		}
#endif
		if ((ip < (matchlimit - 1)) && (A16(ref) == A16(ip))) {
			ip += 2;
			ref += 2;
		}
		if ((ip < matchlimit) && (*ref == *ip))
			ip++;
		_endCount:

		/* Encode MatchLength */
		len = (ip - anchor);
		/* Check output limit */
		if unlikely(op + (1 + LASTLITERALS) + (len >> 8) > oend)
			return (0);
		if (len >= (int)ML_MASK) {
			*token += ML_MASK;
			len -= ML_MASK;
			for (; len > 509; len -= 510) {
				*op++ = 255;
				*op++ = 255;
			}
			if (len > 254) {
				len -= 255;
				*op++ = 255;
			}
			*op++ = (BYTE)len;
		} else
			*token += len;

		/* Test end of chunk */
		if (ip > mflimit) {
			anchor = ip;
			break;
		}
		/* Fill table */
		HashTable[LZ4_HASH_VALUE(ip - 2)] = ip - 2 - base;

		/* Test next position */
		ref = base + HashTable[LZ4_HASH_VALUE(ip)];
		HashTable[LZ4_HASH_VALUE(ip)] = ip - base;
		if ((ref > ip - (MAX_DISTANCE + 1)) && (A32(ref) == A32(ip))) {
			token = op++;
			*token = 0;
			goto _next_match;
		}
		/* Prepare next loop */
		anchor = ip++;
		forwardH = LZ4_HASH_VALUE(ip);
	}

	_last_literals:
	/* Encode Last Literals */
	{
		int lastRun = iend - anchor;
		if (op + lastRun + 1 + ((lastRun + 255 - RUN_MASK) / 255) >
		    oend)
			return (0);
		if (lastRun >= (int)RUN_MASK) {
			*op++ = (RUN_MASK << ML_BITS);
			lastRun -= RUN_MASK;
			for (; lastRun > 254; lastRun -= 255) {
				*op++ = 255;
			}
			*op++ = (BYTE)lastRun;
		} else
			*op++ = (lastRun << ML_BITS);
		(void) memcpy(op, anchor, iend - anchor);
		op += iend - anchor;
	}

	/* End */
	return (int)(((char *)op) - dest);
}



/* Note : this function is valid only if isize < LZ4_64KLIMIT */
#define	LZ4_64KLIMIT ((1 << 16) + (MFLIMIT - 1))
#define	HASHLOG64K (HASH_LOG + 1)
#define	HASH64KTABLESIZE (1U << HASHLOG64K)
#define	LZ4_HASH64K_FUNCTION(i)	(((i) * 2654435761U) >> ((MINMATCH*8) - \
	HASHLOG64K))
#define	LZ4_HASH64K_VALUE(p)	LZ4_HASH64K_FUNCTION(A32(p))

/*ARGSUSED*/
static int
LZ4_compress64kCtx(void *ctx, const char *source, char *dest, int isize,
    int osize)
{
#if HEAPMODE
	struct refTables *srt = (struct refTables *)ctx;
	U16 *HashTable = (U16 *) (srt->hashTable);
#else
	U16 HashTable[HASH64KTABLESIZE] = { 0 };
#endif

	const BYTE *ip = (BYTE *) source;
	const BYTE *anchor = ip;
	const BYTE *const base = ip;
	const BYTE *const iend = ip + isize;
	const BYTE *const oend = (BYTE *) dest + osize;
	const BYTE *const mflimit = iend - MFLIMIT;
#define	matchlimit (iend - LASTLITERALS)

	BYTE *op = (BYTE *) dest;

	int len, length;
	const int skipStrength = SKIPSTRENGTH;
	U32 forwardH;

	/* Init */
	if (isize < MINLENGTH)
		goto _last_literals;

	/* First Byte */
	ip++;
	forwardH = LZ4_HASH64K_VALUE(ip);

	/* Main Loop */
	for (;;) {
		int findMatchAttempts = (1U << skipStrength) + 3;
		const BYTE *forwardIp = ip;
		const BYTE *ref;
		BYTE *token;

		/* Find a match */
		do {
			U32 h = forwardH;
			int step = findMatchAttempts++ >> skipStrength;
			ip = forwardIp;
			forwardIp = ip + step;

			if (forwardIp > mflimit) {
				goto _last_literals;
			}

			forwardH = LZ4_HASH64K_VALUE(forwardIp);
			ref = base + HashTable[h];
			HashTable[h] = ip - base;

		} while (A32(ref) != A32(ip));

		/* Catch up */
		while ((ip > anchor) && (ref > (BYTE *) source) &&
		    (ip[-1] == ref[-1])) {
			ip--;
			ref--;
		}

		/* Encode Literal length */
		length = ip - anchor;
		token = op++;

		/* Check output limit */
		if unlikely(op + length + (2 + 1 + LASTLITERALS) +
		    (length >> 8) > oend)
			return (0);

		if (length >= (int)RUN_MASK) {
			*token = (RUN_MASK << ML_BITS);
			len = length - RUN_MASK;
			for (; len > 254; len -= 255)
				*op++ = 255;
			*op++ = (BYTE)len;
		} else
			*token = (length << ML_BITS);

		/* Copy Literals */
		LZ4_BLINDCOPY(anchor, op, length);

		_next_match:
		/* Encode Offset */
		LZ4_WRITE_LITTLEENDIAN_16(op, ip - ref);

		/* Start Counting */
		ip += MINMATCH;
		ref += MINMATCH;	/* MinMatch verified */
		anchor = ip;
		while (ip < matchlimit - (STEPSIZE - 1)) {
			UARCH diff = AARCH(ref) ^ AARCH(ip);
			if (!diff) {
				ip += STEPSIZE;
				ref += STEPSIZE;
				continue;
			}
			ip += LZ4_NbCommonBytes(diff);
			goto _endCount;
		}
#if LZ4_ARCH64
		if ((ip < (matchlimit - 3)) && (A32(ref) == A32(ip))) {
			ip += 4;
			ref += 4;
		}
#endif
		if ((ip < (matchlimit - 1)) && (A16(ref) == A16(ip))) {
			ip += 2;
			ref += 2;
		}
		if ((ip < matchlimit) && (*ref == *ip))
			ip++;
		_endCount:

		/* Encode MatchLength */
		len = (ip - anchor);
		/* Check output limit */
		if unlikely(op + (1 + LASTLITERALS) + (len >> 8) > oend)
			return (0);
		if (len >= (int)ML_MASK) {
			*token += ML_MASK;
			len -= ML_MASK;
			for (; len > 509; len -= 510) {
				*op++ = 255;
				*op++ = 255;
			}
			if (len > 254) {
				len -= 255;
				*op++ = 255;
			}
			*op++ = (BYTE)len;
		} else
			*token += len;

		/* Test end of chunk */
		if (ip > mflimit) {
			anchor = ip;
			break;
		}
		/* Fill table */
		HashTable[LZ4_HASH64K_VALUE(ip - 2)] = ip - 2 - base;

		/* Test next position */
		ref = base + HashTable[LZ4_HASH64K_VALUE(ip)];
		HashTable[LZ4_HASH64K_VALUE(ip)] = ip - base;
		if (A32(ref) == A32(ip)) {
			token = op++;
			*token = 0;
			goto _next_match;
		}
		/* Prepare next loop */
		anchor = ip++;
		forwardH = LZ4_HASH64K_VALUE(ip);
	}

	_last_literals:
	/* Encode Last Literals */
	{
		int lastRun = iend - anchor;
		if (op + lastRun + 1 + ((lastRun + 255 - RUN_MASK) / 255) >
		    oend)
			return (0);
		if (lastRun >= (int)RUN_MASK) {
			*op++ = (RUN_MASK << ML_BITS);
			lastRun -= RUN_MASK;
			for (; lastRun > 254; lastRun -= 255)
				*op++ = 255;
			*op++ = (BYTE)lastRun;
		} else
			*op++ = (lastRun << ML_BITS);
		(void) memcpy(op, anchor, iend - anchor);
		op += iend - anchor;
	}

	/* End */
	return (int)(((char *)op) - dest);
}

static int
real_LZ4_compress(const char *source, char *dest, int isize, int osize)
{
#if HEAPMODE
	void *ctx = kmem_cache_alloc(lz4_ctx_cache, KM_NOSLEEP);
	int result;

	/*
	 * out of kernel memory, gently fall through - this will disable
	 * compression in zio_compress_data
	 */
	if (ctx == NULL)
		return (0);

	bzero(ctx, sizeof(struct refTables));
	if (isize < LZ4_64KLIMIT)
		result = LZ4_compress64kCtx(ctx, source, dest, isize, osize);
	else
		result = LZ4_compressCtx(ctx, source, dest, isize, osize);

	kmem_cache_free(lz4_ctx_cache, ctx);
	return (result);
#else
	if (isize < (int)LZ4_64KLIMIT)
		return (LZ4_compress64kCtx(NULL, source, dest, isize, osize));
	return (LZ4_compressCtx(NULL, source, dest, isize, osize));
#endif
}

/* Decompression functions */

/*
 * Note: The decoding function LZ4_uncompress_unknownOutputSize() is safe
 *	against "buffer overflow" attack type. They will never write nor
 *	read outside of the provided output buffers.
 *	LZ4_uncompress_unknownOutputSize() also insures that it will never
 *	read outside of the input buffer.  A corrupted input will produce
 *	an error result, a negative int, indicating the position of the
 *	error within input stream.
 */

static int
LZ4_uncompress_unknownOutputSize(const char *source, char *dest, int isize,
    int maxOutputSize)
{
	/* Local Variables */
	const BYTE *restrict ip = (const BYTE *) source;
	const BYTE *const iend = ip + isize;
	const BYTE *ref;

	BYTE *op = (BYTE *) dest;
	BYTE *const oend = op + maxOutputSize;
	BYTE *cpy;

	size_t dec32table[] = {0, 3, 2, 3, 0, 0, 0, 0};
#if LZ4_ARCH64
	size_t dec64table[] = {0, 0, 0, (size_t)-1, 0, 1, 2, 3};
#endif

	/* Main Loop */
	while (ip < iend) {
		unsigned token;
		size_t length;

		/* get runlength */
		token = *ip++;
		if ((length = (token >> ML_BITS)) == RUN_MASK) {
			int s = 255;
			while ((ip < iend) && (s == 255)) {
				s = *ip++;
				length += s;
			}
		}
		/* copy literals */
		cpy = op + length;
		/* CORNER-CASE: cpy might overflow. */
		if (cpy < op)
			goto _output_error;	/* cpy was overflowed, bail! */
		if ((cpy > oend - COPYLENGTH) ||
		    (ip + length > iend - COPYLENGTH)) {
			if (cpy > oend)
				/* Error: writes beyond output buffer */
				goto _output_error;
			if (ip + length != iend)
				/*
				 * Error: LZ4 format requires to consume all
				 * input at this stage
				 */
				goto _output_error;
			(void) memcpy(op, ip, length);
			op += length;
			/* Necessarily EOF, due to parsing restrictions */
			break;
		}
		LZ4_WILDCOPY(ip, op, cpy);
		ip -= (op - cpy);
		op = cpy;

		/* get offset */
		LZ4_READ_LITTLEENDIAN_16(ref, cpy, ip);
		ip += 2;
		if (ref < (BYTE * const) dest)
			/*
			 * Error: offset creates reference outside of
			 * destination buffer
			 */
			goto _output_error;

		/* get matchlength */
		if ((length = (token & ML_MASK)) == ML_MASK) {
			while (ip < iend) {
				int s = *ip++;
				length += s;
				if (s == 255)
					continue;
				break;
			}
		}
		/* copy repeated sequence */
		if unlikely(op - ref < STEPSIZE) {
#if LZ4_ARCH64
			size_t dec64 = dec64table[op-ref];
#else
			const int dec64 = 0;
#endif
			op[0] = ref[0];
			op[1] = ref[1];
			op[2] = ref[2];
			op[3] = ref[3];
			op += 4;
			ref += 4;
			ref -= dec32table[op-ref];
			A32(op) = A32(ref);
			op += STEPSIZE - 4;
			ref -= dec64;
		} else {
			LZ4_COPYSTEP(ref, op);
		}
		cpy = op + length - (STEPSIZE - 4);
		if (cpy > oend - COPYLENGTH) {
			if (cpy > oend)
				/*
				 * Error: request to write outside of
				 * destination buffer
				 */
				goto _output_error;
			LZ4_SECURECOPY(ref, op, (oend - COPYLENGTH));
			while (op < cpy)
				*op++ = *ref++;
			op = cpy;
			if (op == oend)
				/*
				 * Check EOF (should never happen, since
				 * last 5 bytes are supposed to be literals)
				 */
				goto _output_error;
			continue;
		}
		LZ4_SECURECOPY(ref, op, cpy);
		op = cpy;	/* correction */
	}

	/* end of decoding */
	return (int)(((char *)op) - dest);

	/* write overflow error detected */
	_output_error:
	return (int)(-(((char *)ip) - source));
}

extern void
lz4_init(void)
{

#if HEAPMODE
	lz4_ctx_cache = kmem_cache_create("lz4_ctx", sizeof(struct refTables),
	    0, NULL, NULL, NULL, NULL, NULL, 0);
#endif
}

extern void
lz4_fini(void)
{

#if HEAPMODE
	kmem_cache_destroy(lz4_ctx_cache);
#endif
}
