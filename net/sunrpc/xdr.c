// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/net/sunrpc/xdr.c
 *
 * Generic XDR support.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/errno.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/bvec.h>
#include <trace/events/sunrpc.h>

/*
 * XDR functions for basic NFS types
 */
__be32 *
xdr_encode_netobj(__be32 *p, const struct xdr_netobj *obj)
{
	unsigned int	quadlen = XDR_QUADLEN(obj->len);

	p[quadlen] = 0;		/* zero trailing bytes */
	*p++ = cpu_to_be32(obj->len);
	memcpy(p, obj->data, obj->len);
	return p + XDR_QUADLEN(obj->len);
}
EXPORT_SYMBOL_GPL(xdr_encode_netobj);

__be32 *
xdr_decode_netobj(__be32 *p, struct xdr_netobj *obj)
{
	unsigned int	len;

	if ((len = be32_to_cpu(*p++)) > XDR_MAX_NETOBJ)
		return NULL;
	obj->len  = len;
	obj->data = (u8 *) p;
	return p + XDR_QUADLEN(len);
}
EXPORT_SYMBOL_GPL(xdr_decode_netobj);

/**
 * xdr_encode_opaque_fixed - Encode fixed length opaque data
 * @p: pointer to current position in XDR buffer.
 * @ptr: pointer to data to encode (or NULL)
 * @nbytes: size of data.
 *
 * Copy the array of data of length nbytes at ptr to the XDR buffer
 * at position p, then align to the next 32-bit boundary by padding
 * with zero bytes (see RFC1832).
 * Note: if ptr is NULL, only the padding is performed.
 *
 * Returns the updated current XDR buffer position
 *
 */
__be32 *xdr_encode_opaque_fixed(__be32 *p, const void *ptr, unsigned int nbytes)
{
	if (likely(nbytes != 0)) {
		unsigned int quadlen = XDR_QUADLEN(nbytes);
		unsigned int padding = (quadlen << 2) - nbytes;

		if (ptr != NULL)
			memcpy(p, ptr, nbytes);
		if (padding != 0)
			memset((char *)p + nbytes, 0, padding);
		p += quadlen;
	}
	return p;
}
EXPORT_SYMBOL_GPL(xdr_encode_opaque_fixed);

/**
 * xdr_encode_opaque - Encode variable length opaque data
 * @p: pointer to current position in XDR buffer.
 * @ptr: pointer to data to encode (or NULL)
 * @nbytes: size of data.
 *
 * Returns the updated current XDR buffer position
 */
__be32 *xdr_encode_opaque(__be32 *p, const void *ptr, unsigned int nbytes)
{
	*p++ = cpu_to_be32(nbytes);
	return xdr_encode_opaque_fixed(p, ptr, nbytes);
}
EXPORT_SYMBOL_GPL(xdr_encode_opaque);

__be32 *
xdr_encode_string(__be32 *p, const char *string)
{
	return xdr_encode_array(p, string, strlen(string));
}
EXPORT_SYMBOL_GPL(xdr_encode_string);

__be32 *
xdr_decode_string_inplace(__be32 *p, char **sp,
			  unsigned int *lenp, unsigned int maxlen)
{
	u32 len;

	len = be32_to_cpu(*p++);
	if (len > maxlen)
		return NULL;
	*lenp = len;
	*sp = (char *) p;
	return p + XDR_QUADLEN(len);
}
EXPORT_SYMBOL_GPL(xdr_decode_string_inplace);

/**
 * xdr_terminate_string - '\0'-terminate a string residing in an xdr_buf
 * @buf: XDR buffer where string resides
 * @len: length of string, in bytes
 *
 */
void
xdr_terminate_string(struct xdr_buf *buf, const u32 len)
{
	char *kaddr;

	kaddr = kmap_atomic(buf->pages[0]);
	kaddr[buf->page_base + len] = '\0';
	kunmap_atomic(kaddr);
}
EXPORT_SYMBOL_GPL(xdr_terminate_string);

size_t
xdr_buf_pagecount(struct xdr_buf *buf)
{
	if (!buf->page_len)
		return 0;
	return (buf->page_base + buf->page_len + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

int
xdr_alloc_bvec(struct xdr_buf *buf, gfp_t gfp)
{
	size_t i, n = xdr_buf_pagecount(buf);

	if (n != 0 && buf->bvec == NULL) {
		buf->bvec = kmalloc_array(n, sizeof(buf->bvec[0]), gfp);
		if (!buf->bvec)
			return -ENOMEM;
		for (i = 0; i < n; i++) {
			buf->bvec[i].bv_page = buf->pages[i];
			buf->bvec[i].bv_len = PAGE_SIZE;
			buf->bvec[i].bv_offset = 0;
		}
	}
	return 0;
}

void
xdr_free_bvec(struct xdr_buf *buf)
{
	kfree(buf->bvec);
	buf->bvec = NULL;
}

/**
 * xdr_inline_pages - Prepare receive buffer for a large reply
 * @xdr: xdr_buf into which reply will be placed
 * @offset: expected offset where data payload will start, in bytes
 * @pages: vector of struct page pointers
 * @base: offset in first page where receive should start, in bytes
 * @len: expected size of the upper layer data payload, in bytes
 *
 */
void
xdr_inline_pages(struct xdr_buf *xdr, unsigned int offset,
		 struct page **pages, unsigned int base, unsigned int len)
{
	struct kvec *head = xdr->head;
	struct kvec *tail = xdr->tail;
	char *buf = (char *)head->iov_base;
	unsigned int buflen = head->iov_len;

	head->iov_len  = offset;

	xdr->pages = pages;
	xdr->page_base = base;
	xdr->page_len = len;

	tail->iov_base = buf + offset;
	tail->iov_len = buflen - offset;
	if ((xdr->page_len & 3) == 0)
		tail->iov_len -= sizeof(__be32);

	xdr->buflen += len;
}
EXPORT_SYMBOL_GPL(xdr_inline_pages);

/*
 * Helper routines for doing 'memmove' like operations on a struct xdr_buf
 */

/**
 * _shift_data_right_pages
 * @pages: vector of pages containing both the source and dest memory area.
 * @pgto_base: page vector address of destination
 * @pgfrom_base: page vector address of source
 * @len: number of bytes to copy
 *
 * Note: the addresses pgto_base and pgfrom_base are both calculated in
 *       the same way:
 *            if a memory area starts at byte 'base' in page 'pages[i]',
 *            then its address is given as (i << PAGE_SHIFT) + base
 * Also note: pgfrom_base must be < pgto_base, but the memory areas
 * 	they point to may overlap.
 */
static void
_shift_data_right_pages(struct page **pages, size_t pgto_base,
		size_t pgfrom_base, size_t len)
{
	struct page **pgfrom, **pgto;
	char *vfrom, *vto;
	size_t copy;

	BUG_ON(pgto_base <= pgfrom_base);

	pgto_base += len;
	pgfrom_base += len;

	pgto = pages + (pgto_base >> PAGE_SHIFT);
	pgfrom = pages + (pgfrom_base >> PAGE_SHIFT);

	pgto_base &= ~PAGE_MASK;
	pgfrom_base &= ~PAGE_MASK;

	do {
		/* Are any pointers crossing a page boundary? */
		if (pgto_base == 0) {
			pgto_base = PAGE_SIZE;
			pgto--;
		}
		if (pgfrom_base == 0) {
			pgfrom_base = PAGE_SIZE;
			pgfrom--;
		}

		copy = len;
		if (copy > pgto_base)
			copy = pgto_base;
		if (copy > pgfrom_base)
			copy = pgfrom_base;
		pgto_base -= copy;
		pgfrom_base -= copy;

		vto = kmap_atomic(*pgto);
		if (*pgto != *pgfrom) {
			vfrom = kmap_atomic(*pgfrom);
			memcpy(vto + pgto_base, vfrom + pgfrom_base, copy);
			kunmap_atomic(vfrom);
		} else
			memmove(vto + pgto_base, vto + pgfrom_base, copy);
		flush_dcache_page(*pgto);
		kunmap_atomic(vto);

	} while ((len -= copy) != 0);
}

/**
 * _copy_to_pages
 * @pages: array of pages
 * @pgbase: page vector address of destination
 * @p: pointer to source data
 * @len: length
 *
 * Copies data from an arbitrary memory location into an array of pages
 * The copy is assumed to be non-overlapping.
 */
static void
_copy_to_pages(struct page **pages, size_t pgbase, const char *p, size_t len)
{
	struct page **pgto;
	char *vto;
	size_t copy;

	pgto = pages + (pgbase >> PAGE_SHIFT);
	pgbase &= ~PAGE_MASK;

	for (;;) {
		copy = PAGE_SIZE - pgbase;
		if (copy > len)
			copy = len;

		vto = kmap_atomic(*pgto);
		memcpy(vto + pgbase, p, copy);
		kunmap_atomic(vto);

		len -= copy;
		if (len == 0)
			break;

		pgbase += copy;
		if (pgbase == PAGE_SIZE) {
			flush_dcache_page(*pgto);
			pgbase = 0;
			pgto++;
		}
		p += copy;
	}
	flush_dcache_page(*pgto);
}

/**
 * _copy_from_pages
 * @p: pointer to destination
 * @pages: array of pages
 * @pgbase: offset of source data
 * @len: length
 *
 * Copies data into an arbitrary memory location from an array of pages
 * The copy is assumed to be non-overlapping.
 */
void
_copy_from_pages(char *p, struct page **pages, size_t pgbase, size_t len)
{
	struct page **pgfrom;
	char *vfrom;
	size_t copy;

	pgfrom = pages + (pgbase >> PAGE_SHIFT);
	pgbase &= ~PAGE_MASK;

	do {
		copy = PAGE_SIZE - pgbase;
		if (copy > len)
			copy = len;

		vfrom = kmap_atomic(*pgfrom);
		memcpy(p, vfrom + pgbase, copy);
		kunmap_atomic(vfrom);

		pgbase += copy;
		if (pgbase == PAGE_SIZE) {
			pgbase = 0;
			pgfrom++;
		}
		p += copy;

	} while ((len -= copy) != 0);
}
EXPORT_SYMBOL_GPL(_copy_from_pages);

/**
 * xdr_shrink_bufhead
 * @buf: xdr_buf
 * @len: bytes to remove from buf->head[0]
 *
 * Shrinks XDR buffer's header kvec buf->head[0] by
 * 'len' bytes. The extra data is not lost, but is instead
 * moved into the inlined pages and/or the tail.
 */
static unsigned int
xdr_shrink_bufhead(struct xdr_buf *buf, size_t len)
{
	struct kvec *head, *tail;
	size_t copy, offs;
	unsigned int pglen = buf->page_len;
	unsigned int result;

	result = 0;
	tail = buf->tail;
	head = buf->head;

	WARN_ON_ONCE(len > head->iov_len);
	if (len > head->iov_len)
		len = head->iov_len;

	/* Shift the tail first */
	if (tail->iov_len != 0) {
		if (tail->iov_len > len) {
			copy = tail->iov_len - len;
			memmove((char *)tail->iov_base + len,
					tail->iov_base, copy);
			result += copy;
		}
		/* Copy from the inlined pages into the tail */
		copy = len;
		if (copy > pglen)
			copy = pglen;
		offs = len - copy;
		if (offs >= tail->iov_len)
			copy = 0;
		else if (copy > tail->iov_len - offs)
			copy = tail->iov_len - offs;
		if (copy != 0) {
			_copy_from_pages((char *)tail->iov_base + offs,
					buf->pages,
					buf->page_base + pglen + offs - len,
					copy);
			result += copy;
		}
		/* Do we also need to copy data from the head into the tail ? */
		if (len > pglen) {
			offs = copy = len - pglen;
			if (copy > tail->iov_len)
				copy = tail->iov_len;
			memcpy(tail->iov_base,
					(char *)head->iov_base +
					head->iov_len - offs,
					copy);
			result += copy;
		}
	}
	/* Now handle pages */
	if (pglen != 0) {
		if (pglen > len)
			_shift_data_right_pages(buf->pages,
					buf->page_base + len,
					buf->page_base,
					pglen - len);
		copy = len;
		if (len > pglen)
			copy = pglen;
		_copy_to_pages(buf->pages, buf->page_base,
				(char *)head->iov_base + head->iov_len - len,
				copy);
		result += copy;
	}
	head->iov_len -= len;
	buf->buflen -= len;
	/* Have we truncated the message? */
	if (buf->len > buf->buflen)
		buf->len = buf->buflen;

	return result;
}

/**
 * xdr_shrink_pagelen - shrinks buf->pages by up to @len bytes
 * @buf: xdr_buf
 * @len: bytes to remove from buf->pages
 *
 * The extra data is not lost, but is instead moved into buf->tail.
 * Returns the actual number of bytes moved.
 */
static unsigned int
xdr_shrink_pagelen(struct xdr_buf *buf, size_t len)
{
	struct kvec *tail;
	size_t copy;
	unsigned int pglen = buf->page_len;
	unsigned int tailbuf_len;
	unsigned int result;

	result = 0;
	tail = buf->tail;
	if (len > buf->page_len)
		len = buf-> page_len;
	tailbuf_len = buf->buflen - buf->head->iov_len - buf->page_len;

	/* Shift the tail first */
	if (tailbuf_len != 0) {
		unsigned int free_space = tailbuf_len - tail->iov_len;

		if (len < free_space)
			free_space = len;
		tail->iov_len += free_space;

		copy = len;
		if (tail->iov_len > len) {
			char *p = (char *)tail->iov_base + len;
			memmove(p, tail->iov_base, tail->iov_len - len);
			result += tail->iov_len - len;
		} else
			copy = tail->iov_len;
		/* Copy from the inlined pages into the tail */
		_copy_from_pages((char *)tail->iov_base,
				buf->pages, buf->page_base + pglen - len,
				copy);
		result += copy;
	}
	buf->page_len -= len;
	buf->buflen -= len;
	/* Have we truncated the message? */
	if (buf->len > buf->buflen)
		buf->len = buf->buflen;

	return result;
}

void
xdr_shift_buf(struct xdr_buf *buf, size_t len)
{
	xdr_shrink_bufhead(buf, len);
}
EXPORT_SYMBOL_GPL(xdr_shift_buf);

/**
 * xdr_stream_pos - Return the current offset from the start of the xdr_stream
 * @xdr: pointer to struct xdr_stream
 */
unsigned int xdr_stream_pos(const struct xdr_stream *xdr)
{
	return (unsigned int)(XDR_QUADLEN(xdr->buf->len) - xdr->nwords) << 2;
}
EXPORT_SYMBOL_GPL(xdr_stream_pos);

/**
 * xdr_init_encode - Initialize a struct xdr_stream for sending data.
 * @xdr: pointer to xdr_stream struct
 * @buf: pointer to XDR buffer in which to encode data
 * @p: current pointer inside XDR buffer
 * @rqst: pointer to controlling rpc_rqst, for debugging
 *
 * Note: at the moment the RPC client only passes the length of our
 *	 scratch buffer in the xdr_buf's header kvec. Previously this
 *	 meant we needed to call xdr_adjust_iovec() after encoding the
 *	 data. With the new scheme, the xdr_stream manages the details
 *	 of the buffer length, and takes care of adjusting the kvec
 *	 length for us.
 */
void xdr_init_encode(struct xdr_stream *xdr, struct xdr_buf *buf, __be32 *p,
		     struct rpc_rqst *rqst)
{
	struct kvec *iov = buf->head;
	int scratch_len = buf->buflen - buf->page_len - buf->tail[0].iov_len;

	xdr_set_scratch_buffer(xdr, NULL, 0);
	BUG_ON(scratch_len < 0);
	xdr->buf = buf;
	xdr->iov = iov;
	xdr->p = (__be32 *)((char *)iov->iov_base + iov->iov_len);
	xdr->end = (__be32 *)((char *)iov->iov_base + scratch_len);
	BUG_ON(iov->iov_len > scratch_len);

	if (p != xdr->p && p != NULL) {
		size_t len;

		BUG_ON(p < xdr->p || p > xdr->end);
		len = (char *)p - (char *)xdr->p;
		xdr->p = p;
		buf->len += len;
		iov->iov_len += len;
	}
	xdr->rqst = rqst;
}
EXPORT_SYMBOL_GPL(xdr_init_encode);

/**
 * xdr_commit_encode - Ensure all data is written to buffer
 * @xdr: pointer to xdr_stream
 *
 * We handle encoding across page boundaries by giving the caller a
 * temporary location to write to, then later copying the data into
 * place; xdr_commit_encode does that copying.
 *
 * Normally the caller doesn't need to call this directly, as the
 * following xdr_reserve_space will do it.  But an explicit call may be
 * required at the end of encoding, or any other time when the xdr_buf
 * data might be read.
 */
inline void xdr_commit_encode(struct xdr_stream *xdr)
{
	int shift = xdr->scratch.iov_len;
	void *page;

	if (shift == 0)
		return;
	page = page_address(*xdr->page_ptr);
	memcpy(xdr->scratch.iov_base, page, shift);
	memmove(page, page + shift, (void *)xdr->p - page);
	xdr->scratch.iov_len = 0;
}
EXPORT_SYMBOL_GPL(xdr_commit_encode);

static __be32 *xdr_get_next_encode_buffer(struct xdr_stream *xdr,
		size_t nbytes)
{
	__be32 *p;
	int space_left;
	int frag1bytes, frag2bytes;

	if (nbytes > PAGE_SIZE)
		goto out_overflow; /* Bigger buffers require special handling */
	if (xdr->buf->len + nbytes > xdr->buf->buflen)
		goto out_overflow; /* Sorry, we're totally out of space */
	frag1bytes = (xdr->end - xdr->p) << 2;
	frag2bytes = nbytes - frag1bytes;
	if (xdr->iov)
		xdr->iov->iov_len += frag1bytes;
	else
		xdr->buf->page_len += frag1bytes;
	xdr->page_ptr++;
	xdr->iov = NULL;
	/*
	 * If the last encode didn't end exactly on a page boundary, the
	 * next one will straddle boundaries.  Encode into the next
	 * page, then copy it back later in xdr_commit_encode.  We use
	 * the "scratch" iov to track any temporarily unused fragment of
	 * space at the end of the previous buffer:
	 */
	xdr->scratch.iov_base = xdr->p;
	xdr->scratch.iov_len = frag1bytes;
	p = page_address(*xdr->page_ptr);
	/*
	 * Note this is where the next encode will start after we've
	 * shifted this one back:
	 */
	xdr->p = (void *)p + frag2bytes;
	space_left = xdr->buf->buflen - xdr->buf->len;
	xdr->end = (void *)p + min_t(int, space_left, PAGE_SIZE);
	xdr->buf->page_len += frag2bytes;
	xdr->buf->len += nbytes;
	return p;
out_overflow:
	trace_rpc_xdr_overflow(xdr, nbytes);
	return NULL;
}

/**
 * xdr_reserve_space - Reserve buffer space for sending
 * @xdr: pointer to xdr_stream
 * @nbytes: number of bytes to reserve
 *
 * Checks that we have enough buffer space to encode 'nbytes' more
 * bytes of data. If so, update the total xdr_buf length, and
 * adjust the length of the current kvec.
 */
__be32 * xdr_reserve_space(struct xdr_stream *xdr, size_t nbytes)
{
	__be32 *p = xdr->p;
	__be32 *q;

	xdr_commit_encode(xdr);
	/* align nbytes on the next 32-bit boundary */
	nbytes += 3;
	nbytes &= ~3;
	q = p + (nbytes >> 2);
	if (unlikely(q > xdr->end || q < p))
		return xdr_get_next_encode_buffer(xdr, nbytes);
	xdr->p = q;
	if (xdr->iov)
		xdr->iov->iov_len += nbytes;
	else
		xdr->buf->page_len += nbytes;
	xdr->buf->len += nbytes;
	return p;
}
EXPORT_SYMBOL_GPL(xdr_reserve_space);

/**
 * xdr_truncate_encode - truncate an encode buffer
 * @xdr: pointer to xdr_stream
 * @len: new length of buffer
 *
 * Truncates the xdr stream, so that xdr->buf->len == len,
 * and xdr->p points at offset len from the start of the buffer, and
 * head, tail, and page lengths are adjusted to correspond.
 *
 * If this means moving xdr->p to a different buffer, we assume that
 * that the end pointer should be set to the end of the current page,
 * except in the case of the head buffer when we assume the head
 * buffer's current length represents the end of the available buffer.
 *
 * This is *not* safe to use on a buffer that already has inlined page
 * cache pages (as in a zero-copy server read reply), except for the
 * simple case of truncating from one position in the tail to another.
 *
 */
void xdr_truncate_encode(struct xdr_stream *xdr, size_t len)
{
	struct xdr_buf *buf = xdr->buf;
	struct kvec *head = buf->head;
	struct kvec *tail = buf->tail;
	int fraglen;
	int new;

	if (len > buf->len) {
		WARN_ON_ONCE(1);
		return;
	}
	xdr_commit_encode(xdr);

	fraglen = min_t(int, buf->len - len, tail->iov_len);
	tail->iov_len -= fraglen;
	buf->len -= fraglen;
	if (tail->iov_len) {
		xdr->p = tail->iov_base + tail->iov_len;
		WARN_ON_ONCE(!xdr->end);
		WARN_ON_ONCE(!xdr->iov);
		return;
	}
	WARN_ON_ONCE(fraglen);
	fraglen = min_t(int, buf->len - len, buf->page_len);
	buf->page_len -= fraglen;
	buf->len -= fraglen;

	new = buf->page_base + buf->page_len;

	xdr->page_ptr = buf->pages + (new >> PAGE_SHIFT);

	if (buf->page_len) {
		xdr->p = page_address(*xdr->page_ptr);
		xdr->end = (void *)xdr->p + PAGE_SIZE;
		xdr->p = (void *)xdr->p + (new % PAGE_SIZE);
		WARN_ON_ONCE(xdr->iov);
		return;
	}
	if (fraglen)
		xdr->end = head->iov_base + head->iov_len;
	/* (otherwise assume xdr->end is already set) */
	xdr->page_ptr--;
	head->iov_len = len;
	buf->len = len;
	xdr->p = head->iov_base + head->iov_len;
	xdr->iov = buf->head;
}
EXPORT_SYMBOL(xdr_truncate_encode);

/**
 * xdr_restrict_buflen - decrease available buffer space
 * @xdr: pointer to xdr_stream
 * @newbuflen: new maximum number of bytes available
 *
 * Adjust our idea of how much space is available in the buffer.
 * If we've already used too much space in the buffer, returns -1.
 * If the available space is already smaller than newbuflen, returns 0
 * and does nothing.  Otherwise, adjusts xdr->buf->buflen to newbuflen
 * and ensures xdr->end is set at most offset newbuflen from the start
 * of the buffer.
 */
int xdr_restrict_buflen(struct xdr_stream *xdr, int newbuflen)
{
	struct xdr_buf *buf = xdr->buf;
	int left_in_this_buf = (void *)xdr->end - (void *)xdr->p;
	int end_offset = buf->len + left_in_this_buf;

	if (newbuflen < 0 || newbuflen < buf->len)
		return -1;
	if (newbuflen > buf->buflen)
		return 0;
	if (newbuflen < end_offset)
		xdr->end = (void *)xdr->end + newbuflen - end_offset;
	buf->buflen = newbuflen;
	return 0;
}
EXPORT_SYMBOL(xdr_restrict_buflen);

/**
 * xdr_write_pages - Insert a list of pages into an XDR buffer for sending
 * @xdr: pointer to xdr_stream
 * @pages: list of pages
 * @base: offset of first byte
 * @len: length of data in bytes
 *
 */
void xdr_write_pages(struct xdr_stream *xdr, struct page **pages, unsigned int base,
		 unsigned int len)
{
	struct xdr_buf *buf = xdr->buf;
	struct kvec *iov = buf->tail;
	buf->pages = pages;
	buf->page_base = base;
	buf->page_len = len;

	iov->iov_base = (char *)xdr->p;
	iov->iov_len  = 0;
	xdr->iov = iov;

	if (len & 3) {
		unsigned int pad = 4 - (len & 3);

		BUG_ON(xdr->p >= xdr->end);
		iov->iov_base = (char *)xdr->p + (len & 3);
		iov->iov_len  += pad;
		len += pad;
		*xdr->p++ = 0;
	}
	buf->buflen += len;
	buf->len += len;
}
EXPORT_SYMBOL_GPL(xdr_write_pages);

static void xdr_set_iov(struct xdr_stream *xdr, struct kvec *iov,
		unsigned int len)
{
	if (len > iov->iov_len)
		len = iov->iov_len;
	xdr->p = (__be32*)iov->iov_base;
	xdr->end = (__be32*)(iov->iov_base + len);
	xdr->iov = iov;
	xdr->page_ptr = NULL;
}

static int xdr_set_page_base(struct xdr_stream *xdr,
		unsigned int base, unsigned int len)
{
	unsigned int pgnr;
	unsigned int maxlen;
	unsigned int pgoff;
	unsigned int pgend;
	void *kaddr;

	maxlen = xdr->buf->page_len;
	if (base >= maxlen)
		return -EINVAL;
	maxlen -= base;
	if (len > maxlen)
		len = maxlen;

	base += xdr->buf->page_base;

	pgnr = base >> PAGE_SHIFT;
	xdr->page_ptr = &xdr->buf->pages[pgnr];
	kaddr = page_address(*xdr->page_ptr);

	pgoff = base & ~PAGE_MASK;
	xdr->p = (__be32*)(kaddr + pgoff);

	pgend = pgoff + len;
	if (pgend > PAGE_SIZE)
		pgend = PAGE_SIZE;
	xdr->end = (__be32*)(kaddr + pgend);
	xdr->iov = NULL;
	return 0;
}

static void xdr_set_next_page(struct xdr_stream *xdr)
{
	unsigned int newbase;

	newbase = (1 + xdr->page_ptr - xdr->buf->pages) << PAGE_SHIFT;
	newbase -= xdr->buf->page_base;

	if (xdr_set_page_base(xdr, newbase, PAGE_SIZE) < 0)
		xdr_set_iov(xdr, xdr->buf->tail, xdr->nwords << 2);
}

static bool xdr_set_next_buffer(struct xdr_stream *xdr)
{
	if (xdr->page_ptr != NULL)
		xdr_set_next_page(xdr);
	else if (xdr->iov == xdr->buf->head) {
		if (xdr_set_page_base(xdr, 0, PAGE_SIZE) < 0)
			xdr_set_iov(xdr, xdr->buf->tail, xdr->nwords << 2);
	}
	return xdr->p != xdr->end;
}

/**
 * xdr_init_decode - Initialize an xdr_stream for decoding data.
 * @xdr: pointer to xdr_stream struct
 * @buf: pointer to XDR buffer from which to decode data
 * @p: current pointer inside XDR buffer
 * @rqst: pointer to controlling rpc_rqst, for debugging
 */
void xdr_init_decode(struct xdr_stream *xdr, struct xdr_buf *buf, __be32 *p,
		     struct rpc_rqst *rqst)
{
	xdr->buf = buf;
	xdr->scratch.iov_base = NULL;
	xdr->scratch.iov_len = 0;
	xdr->nwords = XDR_QUADLEN(buf->len);
	if (buf->head[0].iov_len != 0)
		xdr_set_iov(xdr, buf->head, buf->len);
	else if (buf->page_len != 0)
		xdr_set_page_base(xdr, 0, buf->len);
	else
		xdr_set_iov(xdr, buf->head, buf->len);
	if (p != NULL && p > xdr->p && xdr->end >= p) {
		xdr->nwords -= p - xdr->p;
		xdr->p = p;
	}
	xdr->rqst = rqst;
}
EXPORT_SYMBOL_GPL(xdr_init_decode);

/**
 * xdr_init_decode_pages - Initialize an xdr_stream for decoding into pages
 * @xdr: pointer to xdr_stream struct
 * @buf: pointer to XDR buffer from which to decode data
 * @pages: list of pages to decode into
 * @len: length in bytes of buffer in pages
 */
void xdr_init_decode_pages(struct xdr_stream *xdr, struct xdr_buf *buf,
			   struct page **pages, unsigned int len)
{
	memset(buf, 0, sizeof(*buf));
	buf->pages =  pages;
	buf->page_len =  len;
	buf->buflen =  len;
	buf->len = len;
	xdr_init_decode(xdr, buf, NULL, NULL);
}
EXPORT_SYMBOL_GPL(xdr_init_decode_pages);

static __be32 * __xdr_inline_decode(struct xdr_stream *xdr, size_t nbytes)
{
	unsigned int nwords = XDR_QUADLEN(nbytes);
	__be32 *p = xdr->p;
	__be32 *q = p + nwords;

	if (unlikely(nwords > xdr->nwords || q > xdr->end || q < p))
		return NULL;
	xdr->p = q;
	xdr->nwords -= nwords;
	return p;
}

/**
 * xdr_set_scratch_buffer - Attach a scratch buffer for decoding data.
 * @xdr: pointer to xdr_stream struct
 * @buf: pointer to an empty buffer
 * @buflen: size of 'buf'
 *
 * The scratch buffer is used when decoding from an array of pages.
 * If an xdr_inline_decode() call spans across page boundaries, then
 * we copy the data into the scratch buffer in order to allow linear
 * access.
 */
void xdr_set_scratch_buffer(struct xdr_stream *xdr, void *buf, size_t buflen)
{
	xdr->scratch.iov_base = buf;
	xdr->scratch.iov_len = buflen;
}
EXPORT_SYMBOL_GPL(xdr_set_scratch_buffer);

static __be32 *xdr_copy_to_scratch(struct xdr_stream *xdr, size_t nbytes)
{
	__be32 *p;
	char *cpdest = xdr->scratch.iov_base;
	size_t cplen = (char *)xdr->end - (char *)xdr->p;

	if (nbytes > xdr->scratch.iov_len)
		goto out_overflow;
	p = __xdr_inline_decode(xdr, cplen);
	if (p == NULL)
		return NULL;
	memcpy(cpdest, p, cplen);
	if (!xdr_set_next_buffer(xdr))
		goto out_overflow;
	cpdest += cplen;
	nbytes -= cplen;
	p = __xdr_inline_decode(xdr, nbytes);
	if (p == NULL)
		return NULL;
	memcpy(cpdest, p, nbytes);
	return xdr->scratch.iov_base;
out_overflow:
	trace_rpc_xdr_overflow(xdr, nbytes);
	return NULL;
}

/**
 * xdr_inline_decode - Retrieve XDR data to decode
 * @xdr: pointer to xdr_stream struct
 * @nbytes: number of bytes of data to decode
 *
 * Check if the input buffer is long enough to enable us to decode
 * 'nbytes' more bytes of data starting at the current position.
 * If so return the current pointer, then update the current
 * pointer position.
 */
__be32 * xdr_inline_decode(struct xdr_stream *xdr, size_t nbytes)
{
	__be32 *p;

	if (unlikely(nbytes == 0))
		return xdr->p;
	if (xdr->p == xdr->end && !xdr_set_next_buffer(xdr))
		goto out_overflow;
	p = __xdr_inline_decode(xdr, nbytes);
	if (p != NULL)
		return p;
	return xdr_copy_to_scratch(xdr, nbytes);
out_overflow:
	trace_rpc_xdr_overflow(xdr, nbytes);
	return NULL;
}
EXPORT_SYMBOL_GPL(xdr_inline_decode);

static unsigned int xdr_align_pages(struct xdr_stream *xdr, unsigned int len)
{
	struct xdr_buf *buf = xdr->buf;
	struct kvec *iov;
	unsigned int nwords = XDR_QUADLEN(len);
	unsigned int cur = xdr_stream_pos(xdr);
	unsigned int copied, offset;

	if (xdr->nwords == 0)
		return 0;

	/* Realign pages to current pointer position */
	iov = buf->head;
	if (iov->iov_len > cur) {
		offset = iov->iov_len - cur;
		copied = xdr_shrink_bufhead(buf, offset);
		trace_rpc_xdr_alignment(xdr, offset, copied);
		xdr->nwords = XDR_QUADLEN(buf->len - cur);
	}

	if (nwords > xdr->nwords) {
		nwords = xdr->nwords;
		len = nwords << 2;
	}
	if (buf->page_len <= len)
		len = buf->page_len;
	else if (nwords < xdr->nwords) {
		/* Truncate page data and move it into the tail */
		offset = buf->page_len - len;
		copied = xdr_shrink_pagelen(buf, offset);
		trace_rpc_xdr_alignment(xdr, offset, copied);
		xdr->nwords = XDR_QUADLEN(buf->len - cur);
	}
	return len;
}

/**
 * xdr_read_pages - Ensure page-based XDR data to decode is aligned at current pointer position
 * @xdr: pointer to xdr_stream struct
 * @len: number of bytes of page data
 *
 * Moves data beyond the current pointer position from the XDR head[] buffer
 * into the page list. Any data that lies beyond current position + "len"
 * bytes is moved into the XDR tail[].
 *
 * Returns the number of XDR encoded bytes now contained in the pages
 */
unsigned int xdr_read_pages(struct xdr_stream *xdr, unsigned int len)
{
	struct xdr_buf *buf = xdr->buf;
	struct kvec *iov;
	unsigned int nwords;
	unsigned int end;
	unsigned int padding;

	len = xdr_align_pages(xdr, len);
	if (len == 0)
		return 0;
	nwords = XDR_QUADLEN(len);
	padding = (nwords << 2) - len;
	xdr->iov = iov = buf->tail;
	/* Compute remaining message length.  */
	end = ((xdr->nwords - nwords) << 2) + padding;
	if (end > iov->iov_len)
		end = iov->iov_len;

	/*
	 * Position current pointer at beginning of tail, and
	 * set remaining message length.
	 */
	xdr->p = (__be32 *)((char *)iov->iov_base + padding);
	xdr->end = (__be32 *)((char *)iov->iov_base + end);
	xdr->page_ptr = NULL;
	xdr->nwords = XDR_QUADLEN(end - padding);
	return len;
}
EXPORT_SYMBOL_GPL(xdr_read_pages);

/**
 * xdr_enter_page - decode data from the XDR page
 * @xdr: pointer to xdr_stream struct
 * @len: number of bytes of page data
 *
 * Moves data beyond the current pointer position from the XDR head[] buffer
 * into the page list. Any data that lies beyond current position + "len"
 * bytes is moved into the XDR tail[]. The current pointer is then
 * repositioned at the beginning of the first XDR page.
 */
void xdr_enter_page(struct xdr_stream *xdr, unsigned int len)
{
	len = xdr_align_pages(xdr, len);
	/*
	 * Position current pointer at beginning of tail, and
	 * set remaining message length.
	 */
	if (len != 0)
		xdr_set_page_base(xdr, 0, len);
}
EXPORT_SYMBOL_GPL(xdr_enter_page);

static const struct kvec empty_iov = {.iov_base = NULL, .iov_len = 0};

void
xdr_buf_from_iov(struct kvec *iov, struct xdr_buf *buf)
{
	buf->head[0] = *iov;
	buf->tail[0] = empty_iov;
	buf->page_len = 0;
	buf->buflen = buf->len = iov->iov_len;
}
EXPORT_SYMBOL_GPL(xdr_buf_from_iov);

/**
 * xdr_buf_subsegment - set subbuf to a portion of buf
 * @buf: an xdr buffer
 * @subbuf: the result buffer
 * @base: beginning of range in bytes
 * @len: length of range in bytes
 *
 * sets @subbuf to an xdr buffer representing the portion of @buf of
 * length @len starting at offset @base.
 *
 * @buf and @subbuf may be pointers to the same struct xdr_buf.
 *
 * Returns -1 if base of length are out of bounds.
 */
int
xdr_buf_subsegment(struct xdr_buf *buf, struct xdr_buf *subbuf,
			unsigned int base, unsigned int len)
{
	subbuf->buflen = subbuf->len = len;
	if (base < buf->head[0].iov_len) {
		subbuf->head[0].iov_base = buf->head[0].iov_base + base;
		subbuf->head[0].iov_len = min_t(unsigned int, len,
						buf->head[0].iov_len - base);
		len -= subbuf->head[0].iov_len;
		base = 0;
	} else {
		base -= buf->head[0].iov_len;
		subbuf->head[0].iov_len = 0;
	}

	if (base < buf->page_len) {
		subbuf->page_len = min(buf->page_len - base, len);
		base += buf->page_base;
		subbuf->page_base = base & ~PAGE_MASK;
		subbuf->pages = &buf->pages[base >> PAGE_SHIFT];
		len -= subbuf->page_len;
		base = 0;
	} else {
		base -= buf->page_len;
		subbuf->page_len = 0;
	}

	if (base < buf->tail[0].iov_len) {
		subbuf->tail[0].iov_base = buf->tail[0].iov_base + base;
		subbuf->tail[0].iov_len = min_t(unsigned int, len,
						buf->tail[0].iov_len - base);
		len -= subbuf->tail[0].iov_len;
		base = 0;
	} else {
		base -= buf->tail[0].iov_len;
		subbuf->tail[0].iov_len = 0;
	}

	if (base || len)
		return -1;
	return 0;
}
EXPORT_SYMBOL_GPL(xdr_buf_subsegment);

static void __read_bytes_from_xdr_buf(struct xdr_buf *subbuf, void *obj, unsigned int len)
{
	unsigned int this_len;

	this_len = min_t(unsigned int, len, subbuf->head[0].iov_len);
	memcpy(obj, subbuf->head[0].iov_base, this_len);
	len -= this_len;
	obj += this_len;
	this_len = min_t(unsigned int, len, subbuf->page_len);
	if (this_len)
		_copy_from_pages(obj, subbuf->pages, subbuf->page_base, this_len);
	len -= this_len;
	obj += this_len;
	this_len = min_t(unsigned int, len, subbuf->tail[0].iov_len);
	memcpy(obj, subbuf->tail[0].iov_base, this_len);
}

/* obj is assumed to point to allocated memory of size at least len: */
int read_bytes_from_xdr_buf(struct xdr_buf *buf, unsigned int base, void *obj, unsigned int len)
{
	struct xdr_buf subbuf;
	int status;

	status = xdr_buf_subsegment(buf, &subbuf, base, len);
	if (status != 0)
		return status;
	__read_bytes_from_xdr_buf(&subbuf, obj, len);
	return 0;
}
EXPORT_SYMBOL_GPL(read_bytes_from_xdr_buf);

static void __write_bytes_to_xdr_buf(struct xdr_buf *subbuf, void *obj, unsigned int len)
{
	unsigned int this_len;

	this_len = min_t(unsigned int, len, subbuf->head[0].iov_len);
	memcpy(subbuf->head[0].iov_base, obj, this_len);
	len -= this_len;
	obj += this_len;
	this_len = min_t(unsigned int, len, subbuf->page_len);
	if (this_len)
		_copy_to_pages(subbuf->pages, subbuf->page_base, obj, this_len);
	len -= this_len;
	obj += this_len;
	this_len = min_t(unsigned int, len, subbuf->tail[0].iov_len);
	memcpy(subbuf->tail[0].iov_base, obj, this_len);
}

/* obj is assumed to point to allocated memory of size at least len: */
int write_bytes_to_xdr_buf(struct xdr_buf *buf, unsigned int base, void *obj, unsigned int len)
{
	struct xdr_buf subbuf;
	int status;

	status = xdr_buf_subsegment(buf, &subbuf, base, len);
	if (status != 0)
		return status;
	__write_bytes_to_xdr_buf(&subbuf, obj, len);
	return 0;
}
EXPORT_SYMBOL_GPL(write_bytes_to_xdr_buf);

int
xdr_decode_word(struct xdr_buf *buf, unsigned int base, u32 *obj)
{
	__be32	raw;
	int	status;

	status = read_bytes_from_xdr_buf(buf, base, &raw, sizeof(*obj));
	if (status)
		return status;
	*obj = be32_to_cpu(raw);
	return 0;
}
EXPORT_SYMBOL_GPL(xdr_decode_word);

int
xdr_encode_word(struct xdr_buf *buf, unsigned int base, u32 obj)
{
	__be32	raw = cpu_to_be32(obj);

	return write_bytes_to_xdr_buf(buf, base, &raw, sizeof(obj));
}
EXPORT_SYMBOL_GPL(xdr_encode_word);

/* Returns 0 on success, or else a negative error code. */
static int
xdr_xcode_array2(struct xdr_buf *buf, unsigned int base,
		 struct xdr_array2_desc *desc, int encode)
{
	char *elem = NULL, *c;
	unsigned int copied = 0, todo, avail_here;
	struct page **ppages = NULL;
	int err;

	if (encode) {
		if (xdr_encode_word(buf, base, desc->array_len) != 0)
			return -EINVAL;
	} else {
		if (xdr_decode_word(buf, base, &desc->array_len) != 0 ||
		    desc->array_len > desc->array_maxlen ||
		    (unsigned long) base + 4 + desc->array_len *
				    desc->elem_size > buf->len)
			return -EINVAL;
	}
	base += 4;

	if (!desc->xcode)
		return 0;

	todo = desc->array_len * desc->elem_size;

	/* process head */
	if (todo && base < buf->head->iov_len) {
		c = buf->head->iov_base + base;
		avail_here = min_t(unsigned int, todo,
				   buf->head->iov_len - base);
		todo -= avail_here;

		while (avail_here >= desc->elem_size) {
			err = desc->xcode(desc, c);
			if (err)
				goto out;
			c += desc->elem_size;
			avail_here -= desc->elem_size;
		}
		if (avail_here) {
			if (!elem) {
				elem = kmalloc(desc->elem_size, GFP_KERNEL);
				err = -ENOMEM;
				if (!elem)
					goto out;
			}
			if (encode) {
				err = desc->xcode(desc, elem);
				if (err)
					goto out;
				memcpy(c, elem, avail_here);
			} else
				memcpy(elem, c, avail_here);
			copied = avail_here;
		}
		base = buf->head->iov_len;  /* align to start of pages */
	}

	/* process pages array */
	base -= buf->head->iov_len;
	if (todo && base < buf->page_len) {
		unsigned int avail_page;

		avail_here = min(todo, buf->page_len - base);
		todo -= avail_here;

		base += buf->page_base;
		ppages = buf->pages + (base >> PAGE_SHIFT);
		base &= ~PAGE_MASK;
		avail_page = min_t(unsigned int, PAGE_SIZE - base,
					avail_here);
		c = kmap(*ppages) + base;

		while (avail_here) {
			avail_here -= avail_page;
			if (copied || avail_page < desc->elem_size) {
				unsigned int l = min(avail_page,
					desc->elem_size - copied);
				if (!elem) {
					elem = kmalloc(desc->elem_size,
						       GFP_KERNEL);
					err = -ENOMEM;
					if (!elem)
						goto out;
				}
				if (encode) {
					if (!copied) {
						err = desc->xcode(desc, elem);
						if (err)
							goto out;
					}
					memcpy(c, elem + copied, l);
					copied += l;
					if (copied == desc->elem_size)
						copied = 0;
				} else {
					memcpy(elem + copied, c, l);
					copied += l;
					if (copied == desc->elem_size) {
						err = desc->xcode(desc, elem);
						if (err)
							goto out;
						copied = 0;
					}
				}
				avail_page -= l;
				c += l;
			}
			while (avail_page >= desc->elem_size) {
				err = desc->xcode(desc, c);
				if (err)
					goto out;
				c += desc->elem_size;
				avail_page -= desc->elem_size;
			}
			if (avail_page) {
				unsigned int l = min(avail_page,
					    desc->elem_size - copied);
				if (!elem) {
					elem = kmalloc(desc->elem_size,
						       GFP_KERNEL);
					err = -ENOMEM;
					if (!elem)
						goto out;
				}
				if (encode) {
					if (!copied) {
						err = desc->xcode(desc, elem);
						if (err)
							goto out;
					}
					memcpy(c, elem + copied, l);
					copied += l;
					if (copied == desc->elem_size)
						copied = 0;
				} else {
					memcpy(elem + copied, c, l);
					copied += l;
					if (copied == desc->elem_size) {
						err = desc->xcode(desc, elem);
						if (err)
							goto out;
						copied = 0;
					}
				}
			}
			if (avail_here) {
				kunmap(*ppages);
				ppages++;
				c = kmap(*ppages);
			}

			avail_page = min(avail_here,
				 (unsigned int) PAGE_SIZE);
		}
		base = buf->page_len;  /* align to start of tail */
	}

	/* process tail */
	base -= buf->page_len;
	if (todo) {
		c = buf->tail->iov_base + base;
		if (copied) {
			unsigned int l = desc->elem_size - copied;

			if (encode)
				memcpy(c, elem + copied, l);
			else {
				memcpy(elem + copied, c, l);
				err = desc->xcode(desc, elem);
				if (err)
					goto out;
			}
			todo -= l;
			c += l;
		}
		while (todo) {
			err = desc->xcode(desc, c);
			if (err)
				goto out;
			c += desc->elem_size;
			todo -= desc->elem_size;
		}
	}
	err = 0;

out:
	kfree(elem);
	if (ppages)
		kunmap(*ppages);
	return err;
}

int
xdr_decode_array2(struct xdr_buf *buf, unsigned int base,
		  struct xdr_array2_desc *desc)
{
	if (base >= buf->len)
		return -EINVAL;

	return xdr_xcode_array2(buf, base, desc, 0);
}
EXPORT_SYMBOL_GPL(xdr_decode_array2);

int
xdr_encode_array2(struct xdr_buf *buf, unsigned int base,
		  struct xdr_array2_desc *desc)
{
	if ((unsigned long) base + 4 + desc->array_len * desc->elem_size >
	    buf->head->iov_len + buf->page_len + buf->tail->iov_len)
		return -EINVAL;

	return xdr_xcode_array2(buf, base, desc, 1);
}
EXPORT_SYMBOL_GPL(xdr_encode_array2);

int
xdr_process_buf(struct xdr_buf *buf, unsigned int offset, unsigned int len,
		int (*actor)(struct scatterlist *, void *), void *data)
{
	int i, ret = 0;
	unsigned int page_len, thislen, page_offset;
	struct scatterlist      sg[1];

	sg_init_table(sg, 1);

	if (offset >= buf->head[0].iov_len) {
		offset -= buf->head[0].iov_len;
	} else {
		thislen = buf->head[0].iov_len - offset;
		if (thislen > len)
			thislen = len;
		sg_set_buf(sg, buf->head[0].iov_base + offset, thislen);
		ret = actor(sg, data);
		if (ret)
			goto out;
		offset = 0;
		len -= thislen;
	}
	if (len == 0)
		goto out;

	if (offset >= buf->page_len) {
		offset -= buf->page_len;
	} else {
		page_len = buf->page_len - offset;
		if (page_len > len)
			page_len = len;
		len -= page_len;
		page_offset = (offset + buf->page_base) & (PAGE_SIZE - 1);
		i = (offset + buf->page_base) >> PAGE_SHIFT;
		thislen = PAGE_SIZE - page_offset;
		do {
			if (thislen > page_len)
				thislen = page_len;
			sg_set_page(sg, buf->pages[i], thislen, page_offset);
			ret = actor(sg, data);
			if (ret)
				goto out;
			page_len -= thislen;
			i++;
			page_offset = 0;
			thislen = PAGE_SIZE;
		} while (page_len != 0);
		offset = 0;
	}
	if (len == 0)
		goto out;
	if (offset < buf->tail[0].iov_len) {
		thislen = buf->tail[0].iov_len - offset;
		if (thislen > len)
			thislen = len;
		sg_set_buf(sg, buf->tail[0].iov_base + offset, thislen);
		ret = actor(sg, data);
		len -= thislen;
	}
	if (len != 0)
		ret = -EINVAL;
out:
	return ret;
}
EXPORT_SYMBOL_GPL(xdr_process_buf);

/**
 * xdr_stream_decode_opaque - Decode variable length opaque
 * @xdr: pointer to xdr_stream
 * @ptr: location to store opaque data
 * @size: size of storage buffer @ptr
 *
 * Return values:
 *   On success, returns size of object stored in *@ptr
 *   %-EBADMSG on XDR buffer overflow
 *   %-EMSGSIZE on overflow of storage buffer @ptr
 */
ssize_t xdr_stream_decode_opaque(struct xdr_stream *xdr, void *ptr, size_t size)
{
	ssize_t ret;
	void *p;

	ret = xdr_stream_decode_opaque_inline(xdr, &p, size);
	if (ret <= 0)
		return ret;
	memcpy(ptr, p, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(xdr_stream_decode_opaque);

/**
 * xdr_stream_decode_opaque_dup - Decode and duplicate variable length opaque
 * @xdr: pointer to xdr_stream
 * @ptr: location to store pointer to opaque data
 * @maxlen: maximum acceptable object size
 * @gfp_flags: GFP mask to use
 *
 * Return values:
 *   On success, returns size of object stored in *@ptr
 *   %-EBADMSG on XDR buffer overflow
 *   %-EMSGSIZE if the size of the object would exceed @maxlen
 *   %-ENOMEM on memory allocation failure
 */
ssize_t xdr_stream_decode_opaque_dup(struct xdr_stream *xdr, void **ptr,
		size_t maxlen, gfp_t gfp_flags)
{
	ssize_t ret;
	void *p;

	ret = xdr_stream_decode_opaque_inline(xdr, &p, maxlen);
	if (ret > 0) {
		*ptr = kmemdup(p, ret, gfp_flags);
		if (*ptr != NULL)
			return ret;
		ret = -ENOMEM;
	}
	*ptr = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(xdr_stream_decode_opaque_dup);

/**
 * xdr_stream_decode_string - Decode variable length string
 * @xdr: pointer to xdr_stream
 * @str: location to store string
 * @size: size of storage buffer @str
 *
 * Return values:
 *   On success, returns length of NUL-terminated string stored in *@str
 *   %-EBADMSG on XDR buffer overflow
 *   %-EMSGSIZE on overflow of storage buffer @str
 */
ssize_t xdr_stream_decode_string(struct xdr_stream *xdr, char *str, size_t size)
{
	ssize_t ret;
	void *p;

	ret = xdr_stream_decode_opaque_inline(xdr, &p, size);
	if (ret > 0) {
		memcpy(str, p, ret);
		str[ret] = '\0';
		return strlen(str);
	}
	*str = '\0';
	return ret;
}
EXPORT_SYMBOL_GPL(xdr_stream_decode_string);

/**
 * xdr_stream_decode_string_dup - Decode and duplicate variable length string
 * @xdr: pointer to xdr_stream
 * @str: location to store pointer to string
 * @maxlen: maximum acceptable string length
 * @gfp_flags: GFP mask to use
 *
 * Return values:
 *   On success, returns length of NUL-terminated string stored in *@ptr
 *   %-EBADMSG on XDR buffer overflow
 *   %-EMSGSIZE if the size of the string would exceed @maxlen
 *   %-ENOMEM on memory allocation failure
 */
ssize_t xdr_stream_decode_string_dup(struct xdr_stream *xdr, char **str,
		size_t maxlen, gfp_t gfp_flags)
{
	void *p;
	ssize_t ret;

	ret = xdr_stream_decode_opaque_inline(xdr, &p, maxlen);
	if (ret > 0) {
		char *s = kmalloc(ret + 1, gfp_flags);
		if (s != NULL) {
			memcpy(s, p, ret);
			s[ret] = '\0';
			*str = s;
			return strlen(s);
		}
		ret = -ENOMEM;
	}
	*str = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(xdr_stream_decode_string_dup);
