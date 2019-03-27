/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

/*
 * Embedded-data Block Pointers
 *
 * Normally, block pointers point (via their DVAs) to a block which holds data.
 * If the data that we need to store is very small, this is an inefficient
 * use of space, because a block must be at minimum 1 sector (typically 512
 * bytes or 4KB).  Additionally, reading these small blocks tends to generate
 * more random reads.
 *
 * Embedded-data Block Pointers allow small pieces of data (the "payload",
 * up to 112 bytes) to be stored in the block pointer itself, instead of
 * being pointed to.  The "Pointer" part of this name is a bit of a
 * misnomer, as nothing is pointed to.
 *
 * BP_EMBEDDED_TYPE_DATA block pointers allow highly-compressible data to
 * be embedded in the block pointer.  The logic for this is handled in
 * the SPA, by the zio pipeline.  Therefore most code outside the zio
 * pipeline doesn't need special-cases to handle these block pointers.
 *
 * See spa.h for details on the exact layout of embedded block pointers.
 */

/*
 * buf must be at least BPE_GET_PSIZE(bp) bytes long (which will never be
 * more than BPE_PAYLOAD_SIZE bytes).
 */
void
decode_embedded_bp_compressed(const blkptr_t *bp, void *buf)
{
	int psize;
	uint8_t *buf8 = buf;
	uint64_t w = 0;
	const uint64_t *bp64 = (const uint64_t *)bp;

	ASSERT(BP_IS_EMBEDDED(bp));

	psize = BPE_GET_PSIZE(bp);

	/*
	 * Decode the words of the block pointer into the byte array.
	 * Low bits of first word are the first byte (little endian).
	 */
	for (int i = 0; i < psize; i++) {
		if (i % sizeof (w) == 0) {
			/* beginning of a word */
			ASSERT3P(bp64, <, bp + 1);
			w = *bp64;
			bp64++;
			if (!BPE_IS_PAYLOADWORD(bp, bp64))
				bp64++;
		}
		buf8[i] = BF64_GET(w, (i % sizeof (w)) * NBBY, NBBY);
	}
}
