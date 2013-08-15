/*
 * LZ4 HC - High Compression Mode of LZ4
 * Copyright (C) 2011-2012, Yann Collet.
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
 *
 *  Changed for kernel use by:
 *  Chanho Min <chanho.min@lge.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/lz4.h>
#include <asm/unaligned.h>
#include "lz4defs.h"

struct lz4hc_data {
	const u8 *base;
	HTYPE hashtable[HASHTABLESIZE];
	u16 chaintable[MAXD];
	const u8 *nexttoupdate;
} __attribute__((__packed__));

static inline int lz4hc_init(struct lz4hc_data *hc4, const u8 *base)
{
	memset((void *)hc4->hashtable, 0, sizeof(hc4->hashtable));
	memset(hc4->chaintable, 0xFF, sizeof(hc4->chaintable));

#if LZ4_ARCH64
	hc4->nexttoupdate = base + 1;
#else
	hc4->nexttoupdate = base;
#endif
	hc4->base = base;
	return 1;
}

/* Update chains up to ip (excluded) */
static inline void lz4hc_insert(struct lz4hc_data *hc4, const u8 *ip)
{
	u16 *chaintable = hc4->chaintable;
	HTYPE *hashtable  = hc4->hashtable;
#if LZ4_ARCH64
	const BYTE * const base = hc4->base;
#else
	const int base = 0;
#endif

	while (hc4->nexttoupdate < ip) {
		const u8 *p = hc4->nexttoupdate;
		size_t delta = p - (hashtable[HASH_VALUE(p)] + base);
		if (delta > MAX_DISTANCE)
			delta = MAX_DISTANCE;
		chaintable[(size_t)(p) & MAXD_MASK] = (u16)delta;
		hashtable[HASH_VALUE(p)] = (p) - base;
		hc4->nexttoupdate++;
	}
}

static inline size_t lz4hc_commonlength(const u8 *p1, const u8 *p2,
		const u8 *const matchlimit)
{
	const u8 *p1t = p1;

	while (p1t < matchlimit - (STEPSIZE - 1)) {
#if LZ4_ARCH64
		u64 diff = A64(p2) ^ A64(p1t);
#else
		u32 diff = A32(p2) ^ A32(p1t);
#endif
		if (!diff) {
			p1t += STEPSIZE;
			p2 += STEPSIZE;
			continue;
		}
		p1t += LZ4_NBCOMMONBYTES(diff);
		return p1t - p1;
	}
#if LZ4_ARCH64
	if ((p1t < (matchlimit-3)) && (A32(p2) == A32(p1t))) {
		p1t += 4;
		p2 += 4;
	}
#endif

	if ((p1t < (matchlimit - 1)) && (A16(p2) == A16(p1t))) {
		p1t += 2;
		p2 += 2;
	}
	if ((p1t < matchlimit) && (*p2 == *p1t))
		p1t++;
	return p1t - p1;
}

static inline int lz4hc_insertandfindbestmatch(struct lz4hc_data *hc4,
		const u8 *ip, const u8 *const matchlimit, const u8 **matchpos)
{
	u16 *const chaintable = hc4->chaintable;
	HTYPE *const hashtable = hc4->hashtable;
	const u8 *ref;
#if LZ4_ARCH64
	const BYTE * const base = hc4->base;
#else
	const int base = 0;
#endif
	int nbattempts = MAX_NB_ATTEMPTS;
	size_t repl = 0, ml = 0;
	u16 delta;

	/* HC4 match finder */
	lz4hc_insert(hc4, ip);
	ref = hashtable[HASH_VALUE(ip)] + base;

	/* potential repetition */
	if (ref >= ip-4) {
		/* confirmed */
		if (A32(ref) == A32(ip)) {
			delta = (u16)(ip-ref);
			repl = ml  = lz4hc_commonlength(ip + MINMATCH,
					ref + MINMATCH, matchlimit) + MINMATCH;
			*matchpos = ref;
		}
		ref -= (size_t)chaintable[(size_t)(ref) & MAXD_MASK];
	}

	while ((ref >= ip - MAX_DISTANCE) && nbattempts) {
		nbattempts--;
		if (*(ref + ml) == *(ip + ml)) {
			if (A32(ref) == A32(ip)) {
				size_t mlt =
					lz4hc_commonlength(ip + MINMATCH,
					ref + MINMATCH, matchlimit) + MINMATCH;
				if (mlt > ml) {
					ml = mlt;
					*matchpos = ref;
				}
			}
		}
		ref -= (size_t)chaintable[(size_t)(ref) & MAXD_MASK];
	}

	/* Complete table */
	if (repl) {
		const BYTE *ptr = ip;
		const BYTE *end;
		end = ip + repl - (MINMATCH-1);
		/* Pre-Load */
		while (ptr < end - delta) {
			chaintable[(size_t)(ptr) & MAXD_MASK] = delta;
			ptr++;
		}
		do {
			chaintable[(size_t)(ptr) & MAXD_MASK] = delta;
			/* Head of chain */
			hashtable[HASH_VALUE(ptr)] = (ptr) - base;
			ptr++;
		} while (ptr < end);
		hc4->nexttoupdate = end;
	}

	return (int)ml;
}

static inline int lz4hc_insertandgetwidermatch(struct lz4hc_data *hc4,
	const u8 *ip, const u8 *startlimit, const u8 *matchlimit, int longest,
	const u8 **matchpos, const u8 **startpos)
{
	u16 *const chaintable = hc4->chaintable;
	HTYPE *const hashtable = hc4->hashtable;
#if LZ4_ARCH64
	const BYTE * const base = hc4->base;
#else
	const int base = 0;
#endif
	const u8 *ref;
	int nbattempts = MAX_NB_ATTEMPTS;
	int delta = (int)(ip - startlimit);

	/* First Match */
	lz4hc_insert(hc4, ip);
	ref = hashtable[HASH_VALUE(ip)] + base;

	while ((ref >= ip - MAX_DISTANCE) && (ref >= hc4->base)
		&& (nbattempts)) {
		nbattempts--;
		if (*(startlimit + longest) == *(ref - delta + longest)) {
			if (A32(ref) == A32(ip)) {
				const u8 *reft = ref + MINMATCH;
				const u8 *ipt = ip + MINMATCH;
				const u8 *startt = ip;

				while (ipt < matchlimit-(STEPSIZE - 1)) {
					#if LZ4_ARCH64
					u64 diff = A64(reft) ^ A64(ipt);
					#else
					u32 diff = A32(reft) ^ A32(ipt);
					#endif

					if (!diff) {
						ipt += STEPSIZE;
						reft += STEPSIZE;
						continue;
					}
					ipt += LZ4_NBCOMMONBYTES(diff);
					goto _endcount;
				}
				#if LZ4_ARCH64
				if ((ipt < (matchlimit - 3))
					&& (A32(reft) == A32(ipt))) {
					ipt += 4;
					reft += 4;
				}
				ipt += 2;
				#endif
				if ((ipt < (matchlimit - 1))
					&& (A16(reft) == A16(ipt))) {
					reft += 2;
				}
				if ((ipt < matchlimit) && (*reft == *ipt))
					ipt++;
_endcount:
				reft = ref;

				while ((startt > startlimit)
					&& (reft > hc4->base)
					&& (startt[-1] == reft[-1])) {
					startt--;
					reft--;
				}

				if ((ipt - startt) > longest) {
					longest = (int)(ipt - startt);
					*matchpos = reft;
					*startpos = startt;
				}
			}
		}
		ref -= (size_t)chaintable[(size_t)(ref) & MAXD_MASK];
	}
	return longest;
}

static inline int lz4_encodesequence(const u8 **ip, u8 **op, const u8 **anchor,
		int ml, const u8 *ref)
{
	int length, len;
	u8 *token;

	/* Encode Literal length */
	length = (int)(*ip - *anchor);
	token = (*op)++;
	if (length >= (int)RUN_MASK) {
		*token = (RUN_MASK << ML_BITS);
		len = length - RUN_MASK;
		for (; len > 254 ; len -= 255)
			*(*op)++ = 255;
		*(*op)++ = (u8)len;
	} else
		*token = (length << ML_BITS);

	/* Copy Literals */
	LZ4_BLINDCOPY(*anchor, *op, length);

	/* Encode Offset */
	LZ4_WRITE_LITTLEENDIAN_16(*op, (u16)(*ip - ref));

	/* Encode MatchLength */
	len = (int)(ml - MINMATCH);
	if (len >= (int)ML_MASK) {
		*token += ML_MASK;
		len -= ML_MASK;
		for (; len > 509 ; len -= 510) {
			*(*op)++ = 255;
			*(*op)++ = 255;
		}
		if (len > 254) {
			len -= 255;
			*(*op)++ = 255;
		}
		*(*op)++ = (u8)len;
	} else
		*token += len;

	/* Prepare next loop */
	*ip += ml;
	*anchor = *ip;

	return 0;
}

static int lz4_compresshcctx(struct lz4hc_data *ctx,
		const char *source,
		char *dest,
		int isize)
{
	const u8 *ip = (const u8 *)source;
	const u8 *anchor = ip;
	const u8 *const iend = ip + isize;
	const u8 *const mflimit = iend - MFLIMIT;
	const u8 *const matchlimit = (iend - LASTLITERALS);

	u8 *op = (u8 *)dest;

	int ml, ml2, ml3, ml0;
	const u8 *ref = NULL;
	const u8 *start2 = NULL;
	const u8 *ref2 = NULL;
	const u8 *start3 = NULL;
	const u8 *ref3 = NULL;
	const u8 *start0;
	const u8 *ref0;
	int lastrun;

	ip++;

	/* Main Loop */
	while (ip < mflimit) {
		ml = lz4hc_insertandfindbestmatch(ctx, ip, matchlimit, (&ref));
		if (!ml) {
			ip++;
			continue;
		}

		/* saved, in case we would skip too much */
		start0 = ip;
		ref0 = ref;
		ml0 = ml;
_search2:
		if (ip+ml < mflimit)
			ml2 = lz4hc_insertandgetwidermatch(ctx, ip + ml - 2,
				ip + 1, matchlimit, ml, &ref2, &start2);
		else
			ml2 = ml;
		/* No better match */
		if (ml2 == ml) {
			lz4_encodesequence(&ip, &op, &anchor, ml, ref);
			continue;
		}

		if (start0 < ip) {
			/* empirical */
			if (start2 < ip + ml0) {
				ip = start0;
				ref = ref0;
				ml = ml0;
			}
		}
		/*
		 * Here, start0==ip
		 * First Match too small : removed
		 */
		if ((start2 - ip) < 3) {
			ml = ml2;
			ip = start2;
			ref = ref2;
			goto _search2;
		}

_search3:
		/*
		 * Currently we have :
		 * ml2 > ml1, and
		 * ip1+3 <= ip2 (usually < ip1+ml1)
		 */
		if ((start2 - ip) < OPTIMAL_ML) {
			int correction;
			int new_ml = ml;
			if (new_ml > OPTIMAL_ML)
				new_ml = OPTIMAL_ML;
			if (ip + new_ml > start2 + ml2 - MINMATCH)
				new_ml = (int)(start2 - ip) + ml2 - MINMATCH;
			correction = new_ml - (int)(start2 - ip);
			if (correction > 0) {
				start2 += correction;
				ref2 += correction;
				ml2 -= correction;
			}
		}
		/*
		 * Now, we have start2 = ip+new_ml,
		 * with new_ml=min(ml, OPTIMAL_ML=18)
		 */
		if (start2 + ml2 < mflimit)
			ml3 = lz4hc_insertandgetwidermatch(ctx,
				start2 + ml2 - 3, start2, matchlimit,
				ml2, &ref3, &start3);
		else
			ml3 = ml2;

		/* No better match : 2 sequences to encode */
		if (ml3 == ml2) {
			/* ip & ref are known; Now for ml */
			if (start2 < ip+ml)
				ml = (int)(start2 - ip);

			/* Now, encode 2 sequences */
			lz4_encodesequence(&ip, &op, &anchor, ml, ref);
			ip = start2;
			lz4_encodesequence(&ip, &op, &anchor, ml2, ref2);
			continue;
		}

		/* Not enough space for match 2 : remove it */
		if (start3 < ip + ml + 3) {
			/*
			 * can write Seq1 immediately ==> Seq2 is removed,
			 * so Seq3 becomes Seq1
			 */
			if (start3 >= (ip + ml)) {
				if (start2 < ip + ml) {
					int correction =
						(int)(ip + ml - start2);
					start2 += correction;
					ref2 += correction;
					ml2 -= correction;
					if (ml2 < MINMATCH) {
						start2 = start3;
						ref2 = ref3;
						ml2 = ml3;
					}
				}

				lz4_encodesequence(&ip, &op, &anchor, ml, ref);
				ip  = start3;
				ref = ref3;
				ml  = ml3;

				start0 = start2;
				ref0 = ref2;
				ml0 = ml2;
				goto _search2;
			}

			start2 = start3;
			ref2 = ref3;
			ml2 = ml3;
			goto _search3;
		}

		/*
		 * OK, now we have 3 ascending matches; let's write at least
		 * the first one ip & ref are known; Now for ml
		 */
		if (start2 < ip + ml) {
			if ((start2 - ip) < (int)ML_MASK) {
				int correction;
				if (ml > OPTIMAL_ML)
					ml = OPTIMAL_ML;
				if (ip + ml > start2 + ml2 - MINMATCH)
					ml = (int)(start2 - ip) + ml2
						- MINMATCH;
				correction = ml - (int)(start2 - ip);
				if (correction > 0) {
					start2 += correction;
					ref2 += correction;
					ml2 -= correction;
				}
			} else
				ml = (int)(start2 - ip);
		}
		lz4_encodesequence(&ip, &op, &anchor, ml, ref);

		ip = start2;
		ref = ref2;
		ml = ml2;

		start2 = start3;
		ref2 = ref3;
		ml2 = ml3;

		goto _search3;
	}

	/* Encode Last Literals */
	lastrun = (int)(iend - anchor);
	if (lastrun >= (int)RUN_MASK) {
		*op++ = (RUN_MASK << ML_BITS);
		lastrun -= RUN_MASK;
		for (; lastrun > 254 ; lastrun -= 255)
			*op++ = 255;
		*op++ = (u8) lastrun;
	} else
		*op++ = (lastrun << ML_BITS);
	memcpy(op, anchor, iend - anchor);
	op += iend - anchor;
	/* End */
	return (int) (((char *)op) - dest);
}

int lz4hc_compress(const unsigned char *src, size_t src_len,
			unsigned char *dst, size_t *dst_len, void *wrkmem)
{
	int ret = -1;
	int out_len = 0;

	struct lz4hc_data *hc4 = (struct lz4hc_data *)wrkmem;
	lz4hc_init(hc4, (const u8 *)src);
	out_len = lz4_compresshcctx((struct lz4hc_data *)hc4, (const u8 *)src,
		(char *)dst, (int)src_len);

	if (out_len < 0)
		goto exit;

	*dst_len = out_len;
	return 0;

exit:
	return ret;
}
EXPORT_SYMBOL_GPL(lz4hc_compress);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZ4HC compressor");
