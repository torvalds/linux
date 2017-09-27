/* Copyright 2008 - 2016 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FSL_BMAN_H
#define __FSL_BMAN_H

/* wrapper for 48-bit buffers */
struct bm_buffer {
	union {
		struct {
			__be16 bpid; /* hi 8-bits reserved */
			__be16 hi; /* High 16-bits of 48-bit address */
			__be32 lo; /* Low 32-bits of 48-bit address */
		};
		__be64 data;
	};
} __aligned(8);
/*
 * Restore the 48 bit address previously stored in BMan
 * hardware pools as a dma_addr_t
 */
static inline dma_addr_t bm_buf_addr(const struct bm_buffer *buf)
{
	return be64_to_cpu(buf->data) & 0xffffffffffffLLU;
}

static inline u64 bm_buffer_get64(const struct bm_buffer *buf)
{
	return be64_to_cpu(buf->data) & 0xffffffffffffLLU;
}

static inline void bm_buffer_set64(struct bm_buffer *buf, u64 addr)
{
	buf->hi = cpu_to_be16(upper_32_bits(addr));
	buf->lo = cpu_to_be32(lower_32_bits(addr));
}

static inline u8 bm_buffer_get_bpid(const struct bm_buffer *buf)
{
	return be16_to_cpu(buf->bpid) & 0xff;
}

static inline void bm_buffer_set_bpid(struct bm_buffer *buf, int bpid)
{
	buf->bpid = cpu_to_be16(bpid & 0xff);
}

/* Managed portal, high-level i/face */

/* Portal and Buffer Pools */
struct bman_portal;
struct bman_pool;

#define BM_POOL_MAX		64 /* max # of buffer pools */

/**
 * bman_new_pool - Allocates a Buffer Pool object
 *
 * Creates a pool object, and returns a reference to it or NULL on error.
 */
struct bman_pool *bman_new_pool(void);

/**
 * bman_free_pool - Deallocates a Buffer Pool object
 * @pool: the pool object to release
 */
void bman_free_pool(struct bman_pool *pool);

/**
 * bman_get_bpid - Returns a pool object's BPID.
 * @pool: the pool object
 *
 * The returned value is the index of the encapsulated buffer pool,
 * in the range of [0, @BM_POOL_MAX-1].
 */
int bman_get_bpid(const struct bman_pool *pool);

/**
 * bman_release - Release buffer(s) to the buffer pool
 * @pool: the buffer pool object to release to
 * @bufs: an array of buffers to release
 * @num: the number of buffers in @bufs (1-8)
 *
 * Adds the given buffers to RCR entries. If the RCR ring is unresponsive,
 * the function will return -ETIMEDOUT. Otherwise, it returns zero.
 */
int bman_release(struct bman_pool *pool, const struct bm_buffer *bufs, u8 num);

/**
 * bman_acquire - Acquire buffer(s) from a buffer pool
 * @pool: the buffer pool object to acquire from
 * @bufs: array for storing the acquired buffers
 * @num: the number of buffers desired (@bufs is at least this big)
 *
 * Issues an "Acquire" command via the portal's management command interface.
 * The return value will be the number of buffers obtained from the pool, or a
 * negative error code if a h/w error or pool starvation was encountered. In
 * the latter case, the content of @bufs is undefined.
 */
int bman_acquire(struct bman_pool *pool, struct bm_buffer *bufs, u8 num);

#endif	/* __FSL_BMAN_H */
