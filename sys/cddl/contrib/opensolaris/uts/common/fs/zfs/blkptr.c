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
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>

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

void
encode_embedded_bp_compressed(blkptr_t *bp, void *data,
    enum zio_compress comp, int uncompressed_size, int compressed_size)
{
	uint64_t *bp64 = (uint64_t *)bp;
	uint64_t w = 0;
	uint8_t *data8 = data;

	ASSERT3U(compressed_size, <=, BPE_PAYLOAD_SIZE);
	ASSERT(uncompressed_size == compressed_size ||
	    comp != ZIO_COMPRESS_OFF);
	ASSERT3U(comp, >=, ZIO_COMPRESS_OFF);
	ASSERT3U(comp, <, ZIO_COMPRESS_FUNCTIONS);

	bzero(bp, sizeof (*bp));
	BP_SET_EMBEDDED(bp, B_TRUE);
	BP_SET_COMPRESS(bp, comp);
	BP_SET_BYTEORDER(bp, ZFS_HOST_BYTEORDER);
	BPE_SET_LSIZE(bp, uncompressed_size);
	BPE_SET_PSIZE(bp, compressed_size);

	/*
	 * Encode the byte array into the words of the block pointer.
	 * First byte goes into low bits of first word (little endian).
	 */
	for (int i = 0; i < compressed_size; i++) {
		BF64_SET(w, (i % sizeof (w)) * NBBY, NBBY, data8[i]);
		if (i % sizeof (w) == sizeof (w) - 1) {
			/* we've reached the end of a word */
			ASSERT3P(bp64, <, bp + 1);
			*bp64 = w;
			bp64++;
			if (!BPE_IS_PAYLOADWORD(bp, bp64))
				bp64++;
			w = 0;
		}
	}
	/* write last partial word */
	if (bp64 < (uint64_t *)(bp + 1))
		*bp64 = w;
}

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

/*
 * Fill in the buffer with the (decompressed) payload of the embedded
 * blkptr_t.  Takes into account compression and byteorder (the payload is
 * treated as a stream of bytes).
 * Return 0 on success, or ENOSPC if it won't fit in the buffer.
 */
int
decode_embedded_bp(const blkptr_t *bp, void *buf, int buflen)
{
	int lsize, psize;

	ASSERT(BP_IS_EMBEDDED(bp));

	lsize = BPE_GET_LSIZE(bp);
	psize = BPE_GET_PSIZE(bp);

	if (lsize > buflen)
		return (ENOSPC);
	ASSERT3U(lsize, ==, buflen);

	if (BP_GET_COMPRESS(bp) != ZIO_COMPRESS_OFF) {
		uint8_t dstbuf[BPE_PAYLOAD_SIZE];
		decode_embedded_bp_compressed(bp, dstbuf);
		VERIFY0(zio_decompress_data_buf(BP_GET_COMPRESS(bp),
		    dstbuf, buf, psize, buflen));
	} else {
		ASSERT3U(lsize, ==, psize);
		decode_embedded_bp_compressed(bp, buf);
	}

	return (0);
}
