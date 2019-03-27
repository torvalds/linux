/* 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef __XEN_BLKIF_H__
#define __XEN_BLKIF_H__

#include <xen/interface/io/ring.h>
#include <xen/interface/io/blkif.h>
#include <xen/interface/io/protocols.h>

/* Not a real protocol.  Used to generate ring structs which contain
 * the elements common to all protocols only.  This way we get a
 * compiler-checkable way to use common struct elements, so we can
 * avoid using switch(protocol) in a number of places.  */
struct blkif_common_request {
	char dummy;
};
struct blkif_common_response {
	char dummy;
};

/* i386 protocol version */
#pragma pack(push, 4)
struct blkif_x86_32_request {
	uint8_t        operation;    /* BLKIF_OP_???                         */
	uint8_t        nr_segments;  /* number of segments                   */
	blkif_vdev_t   handle;       /* only for read/write requests         */
	uint64_t       id;           /* private guest value, echoed in resp  */
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	struct blkif_request_segment seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
};
struct blkif_x86_32_response {
	uint64_t        id;              /* copied from request */
	uint8_t         operation;       /* copied from request */
	int16_t         status;          /* BLKIF_RSP_???       */
};
typedef struct blkif_x86_32_request blkif_x86_32_request_t;
typedef struct blkif_x86_32_response blkif_x86_32_response_t;
#pragma pack(pop)

/* x86_64 protocol version */
struct blkif_x86_64_request {
	uint8_t        operation;    /* BLKIF_OP_???                         */
	uint8_t        nr_segments;  /* number of segments                   */
	blkif_vdev_t   handle;       /* only for read/write requests         */
	uint64_t       __attribute__((__aligned__(8))) id;
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	struct blkif_request_segment seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
};
struct blkif_x86_64_response {
	uint64_t       __attribute__((__aligned__(8))) id;
	uint8_t         operation;       /* copied from request */
	int16_t         status;          /* BLKIF_RSP_???       */
};
typedef struct blkif_x86_64_request blkif_x86_64_request_t;
typedef struct blkif_x86_64_response blkif_x86_64_response_t;

DEFINE_RING_TYPES(blkif_common, struct blkif_common_request, struct blkif_common_response);
DEFINE_RING_TYPES(blkif_x86_32, struct blkif_x86_32_request, struct blkif_x86_32_response);
DEFINE_RING_TYPES(blkif_x86_64, struct blkif_x86_64_request, struct blkif_x86_64_response);

/*
 * Maximum number of requests that can be active for a given instance
 * regardless of the protocol in use, based on the ring size.  This constant
 * facilitates resource pre-allocation in backend drivers since the size is
 * known well in advance of attaching to a front end.
 */
#define BLKIF_MAX_RING_REQUESTS(_sz) \
	MAX(__RING_SIZE((blkif_x86_64_sring_t *)NULL, _sz),	\
	    MAX(__RING_SIZE((blkif_x86_32_sring_t *)NULL, _sz),	\
		__RING_SIZE((blkif_sring_t *)NULL, _sz)))

/*
 * The number of ring pages required to support a given number of requests
 * for a given instance regardless of the protocol in use.
 */
#define BLKIF_RING_PAGES(_entries) \
	MAX(__RING_PAGES((blkif_x86_64_sring_t *)NULL, _entries),	\
	    MAX(__RING_PAGES((blkif_x86_32_sring_t *)NULL, _entries),	\
		__RING_PAGES((blkif_sring_t *)NULL, _entries)))

union blkif_back_rings {
	blkif_back_ring_t        native;
	blkif_common_back_ring_t common;
	blkif_x86_32_back_ring_t x86_32;
	blkif_x86_64_back_ring_t x86_64;
};
typedef union blkif_back_rings blkif_back_rings_t;

enum blkif_protocol {
	BLKIF_PROTOCOL_NATIVE = 1,
	BLKIF_PROTOCOL_X86_32 = 2,
	BLKIF_PROTOCOL_X86_64 = 3,
};

static void inline blkif_get_x86_32_req(blkif_request_t *dst, blkif_x86_32_request_t *src)
{
	int i, n = BLKIF_MAX_SEGMENTS_PER_REQUEST;
	dst->operation = src->operation;
	dst->nr_segments = src->nr_segments;
	dst->handle = src->handle;
	dst->id = src->id;
	dst->sector_number = src->sector_number;
	__compiler_membar();
	if (n > dst->nr_segments)
		n = dst->nr_segments;
	for (i = 0; i < n; i++)
		dst->seg[i] = src->seg[i];
}

static void inline blkif_get_x86_64_req(blkif_request_t *dst, blkif_x86_64_request_t *src)
{
	int i, n = BLKIF_MAX_SEGMENTS_PER_REQUEST;
	dst->operation = src->operation;
	dst->nr_segments = src->nr_segments;
	dst->handle = src->handle;
	dst->id = src->id;
	dst->sector_number = src->sector_number;
	__compiler_membar();
	if (n > dst->nr_segments)
		n = dst->nr_segments;
	for (i = 0; i < n; i++)
		dst->seg[i] = src->seg[i];
}

#endif /* __XEN_BLKIF_H__ */
