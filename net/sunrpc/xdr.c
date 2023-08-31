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

static void _copy_to_pages(struct page **, size_t, const char *, size_t);


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
void xdr_terminate_string(const struct xdr_buf *buf, const u32 len)
{
	char *kaddr;

	kaddr = kmap_atomic(buf->pages[0]);
	kaddr[buf->page_base + len] = '\0';
	kunmap_atomic(kaddr);
}
EXPORT_SYMBOL_GPL(xdr_terminate_string);

size_t xdr_buf_pagecount(const struct xdr_buf *buf)
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
			bvec_set_page(&buf->bvec[i], buf->pages[i], PAGE_SIZE,
				      0);
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
 * xdr_buf_to_bvec - Copy components of an xdr_buf into a bio_vec array
 * @bvec: bio_vec array to populate
 * @bvec_size: element count of @bio_vec
 * @xdr: xdr_buf to be copied
 *
 * Returns the number of entries consumed in @bvec.
 */
unsigned int xdr_buf_to_bvec(struct bio_vec *bvec, unsigned int bvec_size,
			     const struct xdr_buf *xdr)
{
	const struct kvec *head = xdr->head;
	const struct kvec *tail = xdr->tail;
	unsigned int count = 0;

	if (head->iov_len) {
		bvec_set_virt(bvec++, head->iov_base, head->iov_len);
		++count;
	}

	if (xdr->page_len) {
		unsigned int offset, len, remaining;
		struct page **pages = xdr->pages;

		offset = offset_in_page(xdr->page_base);
		remaining = xdr->page_len;
		while (remaining > 0) {
			len = min_t(unsigned int, remaining,
				    PAGE_SIZE - offset);
			bvec_set_page(bvec++, *pages++, len, offset);
			remaining -= len;
			offset = 0;
			if (unlikely(++count > bvec_size))
				goto bvec_overflow;
		}
	}

	if (tail->iov_len) {
		bvec_set_virt(bvec, tail->iov_base, tail->iov_len);
		if (unlikely(++count > bvec_size))
			goto bvec_overflow;
	}

	return count;

bvec_overflow:
	pr_warn_once("%s: bio_vec array overflow\n", __func__);
	return count - 1;
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
	xdr->buflen += len;
}
EXPORT_SYMBOL_GPL(xdr_inline_pages);

/*
 * Helper routines for doing 'memmove' like operations on a struct xdr_buf
 */

/**
 * _shift_data_left_pages
 * @pages: vector of pages containing both the source and dest memory area.
 * @pgto_base: page vector address of destination
 * @pgfrom_base: page vector address of source
 * @len: number of bytes to copy
 *
 * Note: the addresses pgto_base and pgfrom_base are both calculated in
 *       the same way:
 *            if a memory area starts at byte 'base' in page 'pages[i]',
 *            then its address is given as (i << PAGE_CACHE_SHIFT) + base
 * Alse note: pgto_base must be < pgfrom_base, but the memory areas
 * 	they point to may overlap.
 */
static void
_shift_data_left_pages(struct page **pages, size_t pgto_base,
			size_t pgfrom_base, size_t len)
{
	struct page **pgfrom, **pgto;
	char *vfrom, *vto;
	size_t copy;

	BUG_ON(pgfrom_base <= pgto_base);

	if (!len)
		return;

	pgto = pages + (pgto_base >> PAGE_SHIFT);
	pgfrom = pages + (pgfrom_base >> PAGE_SHIFT);

	pgto_base &= ~PAGE_MASK;
	pgfrom_base &= ~PAGE_MASK;

	do {
		if (pgto_base >= PAGE_SIZE) {
			pgto_base = 0;
			pgto++;
		}
		if (pgfrom_base >= PAGE_SIZE){
			pgfrom_base = 0;
			pgfrom++;
		}

		copy = len;
		if (copy > (PAGE_SIZE - pgto_base))
			copy = PAGE_SIZE - pgto_base;
		if (copy > (PAGE_SIZE - pgfrom_base))
			copy = PAGE_SIZE - pgfrom_base;

		vto = kmap_atomic(*pgto);
		if (*pgto != *pgfrom) {
			vfrom = kmap_atomic(*pgfrom);
			memcpy(vto + pgto_base, vfrom + pgfrom_base, copy);
			kunmap_atomic(vfrom);
		} else
			memmove(vto + pgto_base, vto + pgfrom_base, copy);
		flush_dcache_page(*pgto);
		kunmap_atomic(vto);

		pgto_base += copy;
		pgfrom_base += copy;

	} while ((len -= copy) != 0);
}

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

	if (!len)
		return;

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

	if (!len)
		return;

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

	if (!len)
		return;

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

static void xdr_buf_iov_zero(const struct kvec *iov, unsigned int base,
			     unsigned int len)
{
	if (base >= iov->iov_len)
		return;
	if (len > iov->iov_len - base)
		len = iov->iov_len - base;
	memset(iov->iov_base + base, 0, len);
}

/**
 * xdr_buf_pages_zero
 * @buf: xdr_buf
 * @pgbase: beginning offset
 * @len: length
 */
static void xdr_buf_pages_zero(const struct xdr_buf *buf, unsigned int pgbase,
			       unsigned int len)
{
	struct page **pages = buf->pages;
	struct page **page;
	char *vpage;
	unsigned int zero;

	if (!len)
		return;
	if (pgbase >= buf->page_len) {
		xdr_buf_iov_zero(buf->tail, pgbase - buf->page_len, len);
		return;
	}
	if (pgbase + len > buf->page_len) {
		xdr_buf_iov_zero(buf->tail, 0, pgbase + len - buf->page_len);
		len = buf->page_len - pgbase;
	}

	pgbase += buf->page_base;

	page = pages + (pgbase >> PAGE_SHIFT);
	pgbase &= ~PAGE_MASK;

	do {
		zero = PAGE_SIZE - pgbase;
		if (zero > len)
			zero = len;

		vpage = kmap_atomic(*page);
		memset(vpage + pgbase, 0, zero);
		kunmap_atomic(vpage);

		flush_dcache_page(*page);
		pgbase = 0;
		page++;

	} while ((len -= zero) != 0);
}

static unsigned int xdr_buf_pages_fill_sparse(const struct xdr_buf *buf,
					      unsigned int buflen, gfp_t gfp)
{
	unsigned int i, npages, pagelen;

	if (!(buf->flags & XDRBUF_SPARSE_PAGES))
		return buflen;
	if (buflen <= buf->head->iov_len)
		return buflen;
	pagelen = buflen - buf->head->iov_len;
	if (pagelen > buf->page_len)
		pagelen = buf->page_len;
	npages = (pagelen + buf->page_base + PAGE_SIZE - 1) >> PAGE_SHIFT;
	for (i = 0; i < npages; i++) {
		if (!buf->pages[i])
			continue;
		buf->pages[i] = alloc_page(gfp);
		if (likely(buf->pages[i]))
			continue;
		buflen -= pagelen;
		pagelen = i << PAGE_SHIFT;
		if (pagelen > buf->page_base)
			buflen += pagelen - buf->page_base;
		break;
	}
	return buflen;
}

static void xdr_buf_try_expand(struct xdr_buf *buf, unsigned int len)
{
	struct kvec *head = buf->head;
	struct kvec *tail = buf->tail;
	unsigned int sum = head->iov_len + buf->page_len + tail->iov_len;
	unsigned int free_space, newlen;

	if (sum > buf->len) {
		free_space = min_t(unsigned int, sum - buf->len, len);
		newlen = xdr_buf_pages_fill_sparse(buf, buf->len + free_space,
						   GFP_KERNEL);
		free_space = newlen - buf->len;
		buf->len = newlen;
		len -= free_space;
		if (!len)
			return;
	}

	if (buf->buflen > sum) {
		/* Expand the tail buffer */
		free_space = min_t(unsigned int, buf->buflen - sum, len);
		tail->iov_len += free_space;
		buf->len += free_space;
	}
}

static void xdr_buf_tail_copy_right(const struct xdr_buf *buf,
				    unsigned int base, unsigned int len,
				    unsigned int shift)
{
	const struct kvec *tail = buf->tail;
	unsigned int to = base + shift;

	if (to >= tail->iov_len)
		return;
	if (len + to > tail->iov_len)
		len = tail->iov_len - to;
	memmove(tail->iov_base + to, tail->iov_base + base, len);
}

static void xdr_buf_pages_copy_right(const struct xdr_buf *buf,
				     unsigned int base, unsigned int len,
				     unsigned int shift)
{
	const struct kvec *tail = buf->tail;
	unsigned int to = base + shift;
	unsigned int pglen = 0;
	unsigned int talen = 0, tato = 0;

	if (base >= buf->page_len)
		return;
	if (len > buf->page_len - base)
		len = buf->page_len - base;
	if (to >= buf->page_len) {
		tato = to - buf->page_len;
		if (tail->iov_len >= len + tato)
			talen = len;
		else if (tail->iov_len > tato)
			talen = tail->iov_len - tato;
	} else if (len + to >= buf->page_len) {
		pglen = buf->page_len - to;
		talen = len - pglen;
		if (talen > tail->iov_len)
			talen = tail->iov_len;
	} else
		pglen = len;

	_copy_from_pages(tail->iov_base + tato, buf->pages,
			 buf->page_base + base + pglen, talen);
	_shift_data_right_pages(buf->pages, buf->page_base + to,
				buf->page_base + base, pglen);
}

static void xdr_buf_head_copy_right(const struct xdr_buf *buf,
				    unsigned int base, unsigned int len,
				    unsigned int shift)
{
	const struct kvec *head = buf->head;
	const struct kvec *tail = buf->tail;
	unsigned int to = base + shift;
	unsigned int pglen = 0, pgto = 0;
	unsigned int talen = 0, tato = 0;

	if (base >= head->iov_len)
		return;
	if (len > head->iov_len - base)
		len = head->iov_len - base;
	if (to >= buf->page_len + head->iov_len) {
		tato = to - buf->page_len - head->iov_len;
		talen = len;
	} else if (to >= head->iov_len) {
		pgto = to - head->iov_len;
		pglen = len;
		if (pgto + pglen > buf->page_len) {
			talen = pgto + pglen - buf->page_len;
			pglen -= talen;
		}
	} else {
		pglen = len - to;
		if (pglen > buf->page_len) {
			talen = pglen - buf->page_len;
			pglen = buf->page_len;
		}
	}

	len -= talen;
	base += len;
	if (talen + tato > tail->iov_len)
		talen = tail->iov_len > tato ? tail->iov_len - tato : 0;
	memcpy(tail->iov_base + tato, head->iov_base + base, talen);

	len -= pglen;
	base -= pglen;
	_copy_to_pages(buf->pages, buf->page_base + pgto, head->iov_base + base,
		       pglen);

	base -= len;
	memmove(head->iov_base + to, head->iov_base + base, len);
}

static void xdr_buf_tail_shift_right(const struct xdr_buf *buf,
				     unsigned int base, unsigned int len,
				     unsigned int shift)
{
	const struct kvec *tail = buf->tail;

	if (base >= tail->iov_len || !shift || !len)
		return;
	xdr_buf_tail_copy_right(buf, base, len, shift);
}

static void xdr_buf_pages_shift_right(const struct xdr_buf *buf,
				      unsigned int base, unsigned int len,
				      unsigned int shift)
{
	if (!shift || !len)
		return;
	if (base >= buf->page_len) {
		xdr_buf_tail_shift_right(buf, base - buf->page_len, len, shift);
		return;
	}
	if (base + len > buf->page_len)
		xdr_buf_tail_shift_right(buf, 0, base + len - buf->page_len,
					 shift);
	xdr_buf_pages_copy_right(buf, base, len, shift);
}

static void xdr_buf_head_shift_right(const struct xdr_buf *buf,
				     unsigned int base, unsigned int len,
				     unsigned int shift)
{
	const struct kvec *head = buf->head;

	if (!shift)
		return;
	if (base >= head->iov_len) {
		xdr_buf_pages_shift_right(buf, head->iov_len - base, len,
					  shift);
		return;
	}
	if (base + len > head->iov_len)
		xdr_buf_pages_shift_right(buf, 0, base + len - head->iov_len,
					  shift);
	xdr_buf_head_copy_right(buf, base, len, shift);
}

static void xdr_buf_tail_copy_left(const struct xdr_buf *buf, unsigned int base,
				   unsigned int len, unsigned int shift)
{
	const struct kvec *tail = buf->tail;

	if (base >= tail->iov_len)
		return;
	if (len > tail->iov_len - base)
		len = tail->iov_len - base;
	/* Shift data into head */
	if (shift > buf->page_len + base) {
		const struct kvec *head = buf->head;
		unsigned int hdto =
			head->iov_len + buf->page_len + base - shift;
		unsigned int hdlen = len;

		if (WARN_ONCE(shift > head->iov_len + buf->page_len + base,
			      "SUNRPC: Misaligned data.\n"))
			return;
		if (hdto + hdlen > head->iov_len)
			hdlen = head->iov_len - hdto;
		memcpy(head->iov_base + hdto, tail->iov_base + base, hdlen);
		base += hdlen;
		len -= hdlen;
		if (!len)
			return;
	}
	/* Shift data into pages */
	if (shift > base) {
		unsigned int pgto = buf->page_len + base - shift;
		unsigned int pglen = len;

		if (pgto + pglen > buf->page_len)
			pglen = buf->page_len - pgto;
		_copy_to_pages(buf->pages, buf->page_base + pgto,
			       tail->iov_base + base, pglen);
		base += pglen;
		len -= pglen;
		if (!len)
			return;
	}
	memmove(tail->iov_base + base - shift, tail->iov_base + base, len);
}

static void xdr_buf_pages_copy_left(const struct xdr_buf *buf,
				    unsigned int base, unsigned int len,
				    unsigned int shift)
{
	unsigned int pgto;

	if (base >= buf->page_len)
		return;
	if (len > buf->page_len - base)
		len = buf->page_len - base;
	/* Shift data into head */
	if (shift > base) {
		const struct kvec *head = buf->head;
		unsigned int hdto = head->iov_len + base - shift;
		unsigned int hdlen = len;

		if (WARN_ONCE(shift > head->iov_len + base,
			      "SUNRPC: Misaligned data.\n"))
			return;
		if (hdto + hdlen > head->iov_len)
			hdlen = head->iov_len - hdto;
		_copy_from_pages(head->iov_base + hdto, buf->pages,
				 buf->page_base + base, hdlen);
		base += hdlen;
		len -= hdlen;
		if (!len)
			return;
	}
	pgto = base - shift;
	_shift_data_left_pages(buf->pages, buf->page_base + pgto,
			       buf->page_base + base, len);
}

static void xdr_buf_tail_shift_left(const struct xdr_buf *buf,
				    unsigned int base, unsigned int len,
				    unsigned int shift)
{
	if (!shift || !len)
		return;
	xdr_buf_tail_copy_left(buf, base, len, shift);
}

static void xdr_buf_pages_shift_left(const struct xdr_buf *buf,
				     unsigned int base, unsigned int len,
				     unsigned int shift)
{
	if (!shift || !len)
		return;
	if (base >= buf->page_len) {
		xdr_buf_tail_shift_left(buf, base - buf->page_len, len, shift);
		return;
	}
	xdr_buf_pages_copy_left(buf, base, len, shift);
	len += base;
	if (len <= buf->page_len)
		return;
	xdr_buf_tail_copy_left(buf, 0, len - buf->page_len, shift);
}

static void xdr_buf_head_shift_left(const struct xdr_buf *buf,
				    unsigned int base, unsigned int len,
				    unsigned int shift)
{
	const struct kvec *head = buf->head;
	unsigned int bytes;

	if (!shift || !len)
		return;

	if (shift > base) {
		bytes = (shift - base);
		if (bytes >= len)
			return;
		base += bytes;
		len -= bytes;
	}

	if (base < head->iov_len) {
		bytes = min_t(unsigned int, len, head->iov_len - base);
		memmove(head->iov_base + (base - shift),
			head->iov_base + base, bytes);
		base += bytes;
		len -= bytes;
	}
	xdr_buf_pages_shift_left(buf, base - head->iov_len, len, shift);
}

/**
 * xdr_shrink_bufhead
 * @buf: xdr_buf
 * @len: new length of buf->head[0]
 *
 * Shrinks XDR buffer's header kvec buf->head[0], setting it to
 * 'len' bytes. The extra data is not lost, but is instead
 * moved into the inlined pages and/or the tail.
 */
static unsigned int xdr_shrink_bufhead(struct xdr_buf *buf, unsigned int len)
{
	struct kvec *head = buf->head;
	unsigned int shift, buflen = max(buf->len, len);

	WARN_ON_ONCE(len > head->iov_len);
	if (head->iov_len > buflen) {
		buf->buflen -= head->iov_len - buflen;
		head->iov_len = buflen;
	}
	if (len >= head->iov_len)
		return 0;
	shift = head->iov_len - len;
	xdr_buf_try_expand(buf, shift);
	xdr_buf_head_shift_right(buf, len, buflen - len, shift);
	head->iov_len = len;
	buf->buflen -= shift;
	buf->len -= shift;
	return shift;
}

/**
 * xdr_shrink_pagelen - shrinks buf->pages to @len bytes
 * @buf: xdr_buf
 * @len: new page buffer length
 *
 * The extra data is not lost, but is instead moved into buf->tail.
 * Returns the actual number of bytes moved.
 */
static unsigned int xdr_shrink_pagelen(struct xdr_buf *buf, unsigned int len)
{
	unsigned int shift, buflen = buf->len - buf->head->iov_len;

	WARN_ON_ONCE(len > buf->page_len);
	if (buf->head->iov_len >= buf->len || len > buflen)
		buflen = len;
	if (buf->page_len > buflen) {
		buf->buflen -= buf->page_len - buflen;
		buf->page_len = buflen;
	}
	if (len >= buf->page_len)
		return 0;
	shift = buf->page_len - len;
	xdr_buf_try_expand(buf, shift);
	xdr_buf_pages_shift_right(buf, len, buflen - len, shift);
	buf->page_len = len;
	buf->len -= shift;
	buf->buflen -= shift;
	return shift;
}

/**
 * xdr_stream_pos - Return the current offset from the start of the xdr_stream
 * @xdr: pointer to struct xdr_stream
 */
unsigned int xdr_stream_pos(const struct xdr_stream *xdr)
{
	return (unsigned int)(XDR_QUADLEN(xdr->buf->len) - xdr->nwords) << 2;
}
EXPORT_SYMBOL_GPL(xdr_stream_pos);

static void xdr_stream_set_pos(struct xdr_stream *xdr, unsigned int pos)
{
	unsigned int blen = xdr->buf->len;

	xdr->nwords = blen > pos ? XDR_QUADLEN(blen) - XDR_QUADLEN(pos) : 0;
}

static void xdr_stream_page_set_pos(struct xdr_stream *xdr, unsigned int pos)
{
	xdr_stream_set_pos(xdr, pos + xdr->buf->head[0].iov_len);
}

/**
 * xdr_page_pos - Return the current offset from the start of the xdr pages
 * @xdr: pointer to struct xdr_stream
 */
unsigned int xdr_page_pos(const struct xdr_stream *xdr)
{
	unsigned int pos = xdr_stream_pos(xdr);

	WARN_ON(pos < xdr->buf->head[0].iov_len);
	return pos - xdr->buf->head[0].iov_len;
}
EXPORT_SYMBOL_GPL(xdr_page_pos);

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

	xdr_reset_scratch_buffer(xdr);
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
 * xdr_init_encode_pages - Initialize an xdr_stream for encoding into pages
 * @xdr: pointer to xdr_stream struct
 * @buf: pointer to XDR buffer into which to encode data
 * @pages: list of pages to decode into
 * @rqst: pointer to controlling rpc_rqst, for debugging
 *
 */
void xdr_init_encode_pages(struct xdr_stream *xdr, struct xdr_buf *buf,
			   struct page **pages, struct rpc_rqst *rqst)
{
	xdr_reset_scratch_buffer(xdr);

	xdr->buf = buf;
	xdr->page_ptr = pages;
	xdr->iov = NULL;
	xdr->p = page_address(*pages);
	xdr->end = (void *)xdr->p + min_t(u32, buf->buflen, PAGE_SIZE);
	xdr->rqst = rqst;
}
EXPORT_SYMBOL_GPL(xdr_init_encode_pages);

/**
 * __xdr_commit_encode - Ensure all data is written to buffer
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
void __xdr_commit_encode(struct xdr_stream *xdr)
{
	size_t shift = xdr->scratch.iov_len;
	void *page;

	page = page_address(*xdr->page_ptr);
	memcpy(xdr->scratch.iov_base, page, shift);
	memmove(page, page + shift, (void *)xdr->p - page);
	xdr_reset_scratch_buffer(xdr);
}
EXPORT_SYMBOL_GPL(__xdr_commit_encode);

/*
 * The buffer space to be reserved crosses the boundary between
 * xdr->buf->head and xdr->buf->pages, or between two pages
 * in xdr->buf->pages.
 */
static noinline __be32 *xdr_get_next_encode_buffer(struct xdr_stream *xdr,
						   size_t nbytes)
{
	int space_left;
	int frag1bytes, frag2bytes;
	void *p;

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
	xdr_set_scratch_buffer(xdr, xdr->p, frag1bytes);

	/*
	 * xdr->p is where the next encode will start after
	 * xdr_commit_encode() has shifted this one back:
	 */
	p = page_address(*xdr->page_ptr);
	xdr->p = p + frag2bytes;
	space_left = xdr->buf->buflen - xdr->buf->len;
	if (space_left - frag1bytes >= PAGE_SIZE)
		xdr->end = p + PAGE_SIZE;
	else
		xdr->end = p + space_left - frag1bytes;

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
 * xdr_reserve_space_vec - Reserves a large amount of buffer space for sending
 * @xdr: pointer to xdr_stream
 * @nbytes: number of bytes to reserve
 *
 * The size argument passed to xdr_reserve_space() is determined based
 * on the number of bytes remaining in the current page to avoid
 * invalidating iov_base pointers when xdr_commit_encode() is called.
 *
 * Return values:
 *   %0: success
 *   %-EMSGSIZE: not enough space is available in @xdr
 */
int xdr_reserve_space_vec(struct xdr_stream *xdr, size_t nbytes)
{
	size_t thislen;
	__be32 *p;

	/*
	 * svcrdma requires every READ payload to start somewhere
	 * in xdr->pages.
	 */
	if (xdr->iov == xdr->buf->head) {
		xdr->iov = NULL;
		xdr->end = xdr->p;
	}

	/* XXX: Let's find a way to make this more efficient */
	while (nbytes) {
		thislen = xdr->buf->page_len % PAGE_SIZE;
		thislen = min_t(size_t, nbytes, PAGE_SIZE - thislen);

		p = xdr_reserve_space(xdr, thislen);
		if (!p)
			return -EMSGSIZE;

		nbytes -= thislen;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xdr_reserve_space_vec);

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
 * the end pointer should be set to the end of the current page,
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
 * xdr_truncate_decode - Truncate a decoding stream
 * @xdr: pointer to struct xdr_stream
 * @len: Number of bytes to remove
 *
 */
void xdr_truncate_decode(struct xdr_stream *xdr, size_t len)
{
	unsigned int nbytes = xdr_align_size(len);

	xdr->buf->len -= nbytes;
	xdr->nwords -= XDR_QUADLEN(nbytes);
}
EXPORT_SYMBOL_GPL(xdr_truncate_decode);

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
 * @pages: array of pages to insert
 * @base: starting offset of first data byte in @pages
 * @len: number of data bytes in @pages to insert
 *
 * After the @pages are added, the tail iovec is instantiated pointing to
 * end of the head buffer, and the stream is set up to encode subsequent
 * items into the tail.
 */
void xdr_write_pages(struct xdr_stream *xdr, struct page **pages, unsigned int base,
		 unsigned int len)
{
	struct xdr_buf *buf = xdr->buf;
	struct kvec *tail = buf->tail;

	buf->pages = pages;
	buf->page_base = base;
	buf->page_len = len;

	tail->iov_base = xdr->p;
	tail->iov_len = 0;
	xdr->iov = tail;

	if (len & 3) {
		unsigned int pad = 4 - (len & 3);

		BUG_ON(xdr->p >= xdr->end);
		tail->iov_base = (char *)xdr->p + (len & 3);
		tail->iov_len += pad;
		len += pad;
		*xdr->p++ = 0;
	}
	buf->buflen += len;
	buf->len += len;
}
EXPORT_SYMBOL_GPL(xdr_write_pages);

static unsigned int xdr_set_iov(struct xdr_stream *xdr, struct kvec *iov,
				unsigned int base, unsigned int len)
{
	if (len > iov->iov_len)
		len = iov->iov_len;
	if (unlikely(base > len))
		base = len;
	xdr->p = (__be32*)(iov->iov_base + base);
	xdr->end = (__be32*)(iov->iov_base + len);
	xdr->iov = iov;
	xdr->page_ptr = NULL;
	return len - base;
}

static unsigned int xdr_set_tail_base(struct xdr_stream *xdr,
				      unsigned int base, unsigned int len)
{
	struct xdr_buf *buf = xdr->buf;

	xdr_stream_set_pos(xdr, base + buf->page_len + buf->head->iov_len);
	return xdr_set_iov(xdr, buf->tail, base, len);
}

static unsigned int xdr_set_page_base(struct xdr_stream *xdr,
				      unsigned int base, unsigned int len)
{
	unsigned int pgnr;
	unsigned int maxlen;
	unsigned int pgoff;
	unsigned int pgend;
	void *kaddr;

	maxlen = xdr->buf->page_len;
	if (base >= maxlen)
		return 0;
	else
		maxlen -= base;
	if (len > maxlen)
		len = maxlen;

	xdr_stream_page_set_pos(xdr, base);
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
	return len;
}

static void xdr_set_page(struct xdr_stream *xdr, unsigned int base,
			 unsigned int len)
{
	if (xdr_set_page_base(xdr, base, len) == 0) {
		base -= xdr->buf->page_len;
		xdr_set_tail_base(xdr, base, len);
	}
}

static void xdr_set_next_page(struct xdr_stream *xdr)
{
	unsigned int newbase;

	newbase = (1 + xdr->page_ptr - xdr->buf->pages) << PAGE_SHIFT;
	newbase -= xdr->buf->page_base;
	if (newbase < xdr->buf->page_len)
		xdr_set_page_base(xdr, newbase, xdr_stream_remaining(xdr));
	else
		xdr_set_tail_base(xdr, 0, xdr_stream_remaining(xdr));
}

static bool xdr_set_next_buffer(struct xdr_stream *xdr)
{
	if (xdr->page_ptr != NULL)
		xdr_set_next_page(xdr);
	else if (xdr->iov == xdr->buf->head)
		xdr_set_page(xdr, 0, xdr_stream_remaining(xdr));
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
	xdr_reset_scratch_buffer(xdr);
	xdr->nwords = XDR_QUADLEN(buf->len);
	if (xdr_set_iov(xdr, buf->head, 0, buf->len) == 0 &&
	    xdr_set_page_base(xdr, 0, buf->len) == 0)
		xdr_set_iov(xdr, buf->tail, 0, buf->len);
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

static void xdr_realign_pages(struct xdr_stream *xdr)
{
	struct xdr_buf *buf = xdr->buf;
	struct kvec *iov = buf->head;
	unsigned int cur = xdr_stream_pos(xdr);
	unsigned int copied;

	/* Realign pages to current pointer position */
	if (iov->iov_len > cur) {
		copied = xdr_shrink_bufhead(buf, cur);
		trace_rpc_xdr_alignment(xdr, cur, copied);
		xdr_set_page(xdr, 0, buf->page_len);
	}
}

static unsigned int xdr_align_pages(struct xdr_stream *xdr, unsigned int len)
{
	struct xdr_buf *buf = xdr->buf;
	unsigned int nwords = XDR_QUADLEN(len);
	unsigned int copied;

	if (xdr->nwords == 0)
		return 0;

	xdr_realign_pages(xdr);
	if (nwords > xdr->nwords) {
		nwords = xdr->nwords;
		len = nwords << 2;
	}
	if (buf->page_len <= len)
		len = buf->page_len;
	else if (nwords < xdr->nwords) {
		/* Truncate page data and move it into the tail */
		copied = xdr_shrink_pagelen(buf, len);
		trace_rpc_xdr_alignment(xdr, len, copied);
	}
	return len;
}

/**
 * xdr_read_pages - align page-based XDR data to current pointer position
 * @xdr: pointer to xdr_stream struct
 * @len: number of bytes of page data
 *
 * Moves data beyond the current pointer position from the XDR head[] buffer
 * into the page list. Any data that lies beyond current position + @len
 * bytes is moved into the XDR tail[]. The xdr_stream current position is
 * then advanced past that data to align to the next XDR object in the tail.
 *
 * Returns the number of XDR encoded bytes now contained in the pages
 */
unsigned int xdr_read_pages(struct xdr_stream *xdr, unsigned int len)
{
	unsigned int nwords = XDR_QUADLEN(len);
	unsigned int base, end, pglen;

	pglen = xdr_align_pages(xdr, nwords << 2);
	if (pglen == 0)
		return 0;

	base = (nwords << 2) - pglen;
	end = xdr_stream_remaining(xdr) - pglen;

	xdr_set_tail_base(xdr, base, end);
	return len <= pglen ? len : pglen;
}
EXPORT_SYMBOL_GPL(xdr_read_pages);

/**
 * xdr_set_pagelen - Sets the length of the XDR pages
 * @xdr: pointer to xdr_stream struct
 * @len: new length of the XDR page data
 *
 * Either grows or shrinks the length of the xdr pages by setting pagelen to
 * @len bytes. When shrinking, any extra data is moved into buf->tail, whereas
 * when growing any data beyond the current pointer is moved into the tail.
 *
 * Returns True if the operation was successful, and False otherwise.
 */
void xdr_set_pagelen(struct xdr_stream *xdr, unsigned int len)
{
	struct xdr_buf *buf = xdr->buf;
	size_t remaining = xdr_stream_remaining(xdr);
	size_t base = 0;

	if (len < buf->page_len) {
		base = buf->page_len - len;
		xdr_shrink_pagelen(buf, len);
	} else {
		xdr_buf_head_shift_right(buf, xdr_stream_pos(xdr),
					 buf->page_len, remaining);
		if (len > buf->page_len)
			xdr_buf_try_expand(buf, len - buf->page_len);
	}
	xdr_set_tail_base(xdr, base, remaining);
}
EXPORT_SYMBOL_GPL(xdr_set_pagelen);

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

void xdr_buf_from_iov(const struct kvec *iov, struct xdr_buf *buf)
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
 * Returns -1 if base or length are out of bounds.
 */
int xdr_buf_subsegment(const struct xdr_buf *buf, struct xdr_buf *subbuf,
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
		subbuf->head[0].iov_base = buf->head[0].iov_base;
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
		subbuf->pages = buf->pages;
		subbuf->page_base = 0;
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
		subbuf->tail[0].iov_base = buf->tail[0].iov_base;
		subbuf->tail[0].iov_len = 0;
	}

	if (base || len)
		return -1;
	return 0;
}
EXPORT_SYMBOL_GPL(xdr_buf_subsegment);

/**
 * xdr_stream_subsegment - set @subbuf to a portion of @xdr
 * @xdr: an xdr_stream set up for decoding
 * @subbuf: the result buffer
 * @nbytes: length of @xdr to extract, in bytes
 *
 * Sets up @subbuf to represent a portion of @xdr. The portion
 * starts at the current offset in @xdr, and extends for a length
 * of @nbytes. If this is successful, @xdr is advanced to the next
 * XDR data item following that portion.
 *
 * Return values:
 *   %true: @subbuf has been initialized, and @xdr has been advanced.
 *   %false: a bounds error has occurred
 */
bool xdr_stream_subsegment(struct xdr_stream *xdr, struct xdr_buf *subbuf,
			   unsigned int nbytes)
{
	unsigned int start = xdr_stream_pos(xdr);
	unsigned int remaining, len;

	/* Extract @subbuf and bounds-check the fn arguments */
	if (xdr_buf_subsegment(xdr->buf, subbuf, start, nbytes))
		return false;

	/* Advance @xdr by @nbytes */
	for (remaining = nbytes; remaining;) {
		if (xdr->p == xdr->end && !xdr_set_next_buffer(xdr))
			return false;

		len = (char *)xdr->end - (char *)xdr->p;
		if (remaining <= len) {
			xdr->p = (__be32 *)((char *)xdr->p +
					(remaining + xdr_pad_size(nbytes)));
			break;
		}

		xdr->p = (__be32 *)((char *)xdr->p + len);
		xdr->end = xdr->p;
		remaining -= len;
	}

	xdr_stream_set_pos(xdr, start + nbytes);
	return true;
}
EXPORT_SYMBOL_GPL(xdr_stream_subsegment);

/**
 * xdr_stream_move_subsegment - Move part of a stream to another position
 * @xdr: the source xdr_stream
 * @offset: the source offset of the segment
 * @target: the target offset of the segment
 * @length: the number of bytes to move
 *
 * Moves @length bytes from @offset to @target in the xdr_stream, overwriting
 * anything in its space. Returns the number of bytes in the segment.
 */
unsigned int xdr_stream_move_subsegment(struct xdr_stream *xdr, unsigned int offset,
					unsigned int target, unsigned int length)
{
	struct xdr_buf buf;
	unsigned int shift;

	if (offset < target) {
		shift = target - offset;
		if (xdr_buf_subsegment(xdr->buf, &buf, offset, shift + length) < 0)
			return 0;
		xdr_buf_head_shift_right(&buf, 0, length, shift);
	} else if (offset > target) {
		shift = offset - target;
		if (xdr_buf_subsegment(xdr->buf, &buf, target, shift + length) < 0)
			return 0;
		xdr_buf_head_shift_left(&buf, shift, length, shift);
	}
	return length;
}
EXPORT_SYMBOL_GPL(xdr_stream_move_subsegment);

/**
 * xdr_stream_zero - zero out a portion of an xdr_stream
 * @xdr: an xdr_stream to zero out
 * @offset: the starting point in the stream
 * @length: the number of bytes to zero
 */
unsigned int xdr_stream_zero(struct xdr_stream *xdr, unsigned int offset,
			     unsigned int length)
{
	struct xdr_buf buf;

	if (xdr_buf_subsegment(xdr->buf, &buf, offset, length) < 0)
		return 0;
	if (buf.head[0].iov_len)
		xdr_buf_iov_zero(buf.head, 0, buf.head[0].iov_len);
	if (buf.page_len > 0)
		xdr_buf_pages_zero(&buf, 0, buf.page_len);
	if (buf.tail[0].iov_len)
		xdr_buf_iov_zero(buf.tail, 0, buf.tail[0].iov_len);
	return length;
}
EXPORT_SYMBOL_GPL(xdr_stream_zero);

/**
 * xdr_buf_trim - lop at most "len" bytes off the end of "buf"
 * @buf: buf to be trimmed
 * @len: number of bytes to reduce "buf" by
 *
 * Trim an xdr_buf by the given number of bytes by fixing up the lengths. Note
 * that it's possible that we'll trim less than that amount if the xdr_buf is
 * too small, or if (for instance) it's all in the head and the parser has
 * already read too far into it.
 */
void xdr_buf_trim(struct xdr_buf *buf, unsigned int len)
{
	size_t cur;
	unsigned int trim = len;

	if (buf->tail[0].iov_len) {
		cur = min_t(size_t, buf->tail[0].iov_len, trim);
		buf->tail[0].iov_len -= cur;
		trim -= cur;
		if (!trim)
			goto fix_len;
	}

	if (buf->page_len) {
		cur = min_t(unsigned int, buf->page_len, trim);
		buf->page_len -= cur;
		trim -= cur;
		if (!trim)
			goto fix_len;
	}

	if (buf->head[0].iov_len) {
		cur = min_t(size_t, buf->head[0].iov_len, trim);
		buf->head[0].iov_len -= cur;
		trim -= cur;
	}
fix_len:
	buf->len -= (len - trim);
}
EXPORT_SYMBOL_GPL(xdr_buf_trim);

static void __read_bytes_from_xdr_buf(const struct xdr_buf *subbuf,
				      void *obj, unsigned int len)
{
	unsigned int this_len;

	this_len = min_t(unsigned int, len, subbuf->head[0].iov_len);
	memcpy(obj, subbuf->head[0].iov_base, this_len);
	len -= this_len;
	obj += this_len;
	this_len = min_t(unsigned int, len, subbuf->page_len);
	_copy_from_pages(obj, subbuf->pages, subbuf->page_base, this_len);
	len -= this_len;
	obj += this_len;
	this_len = min_t(unsigned int, len, subbuf->tail[0].iov_len);
	memcpy(obj, subbuf->tail[0].iov_base, this_len);
}

/* obj is assumed to point to allocated memory of size at least len: */
int read_bytes_from_xdr_buf(const struct xdr_buf *buf, unsigned int base,
			    void *obj, unsigned int len)
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

static void __write_bytes_to_xdr_buf(const struct xdr_buf *subbuf,
				     void *obj, unsigned int len)
{
	unsigned int this_len;

	this_len = min_t(unsigned int, len, subbuf->head[0].iov_len);
	memcpy(subbuf->head[0].iov_base, obj, this_len);
	len -= this_len;
	obj += this_len;
	this_len = min_t(unsigned int, len, subbuf->page_len);
	_copy_to_pages(subbuf->pages, subbuf->page_base, obj, this_len);
	len -= this_len;
	obj += this_len;
	this_len = min_t(unsigned int, len, subbuf->tail[0].iov_len);
	memcpy(subbuf->tail[0].iov_base, obj, this_len);
}

/* obj is assumed to point to allocated memory of size at least len: */
int write_bytes_to_xdr_buf(const struct xdr_buf *buf, unsigned int base,
			   void *obj, unsigned int len)
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

int xdr_decode_word(const struct xdr_buf *buf, unsigned int base, u32 *obj)
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

int xdr_encode_word(const struct xdr_buf *buf, unsigned int base, u32 obj)
{
	__be32	raw = cpu_to_be32(obj);

	return write_bytes_to_xdr_buf(buf, base, &raw, sizeof(obj));
}
EXPORT_SYMBOL_GPL(xdr_encode_word);

/* Returns 0 on success, or else a negative error code. */
static int xdr_xcode_array2(const struct xdr_buf *buf, unsigned int base,
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

int xdr_decode_array2(const struct xdr_buf *buf, unsigned int base,
		      struct xdr_array2_desc *desc)
{
	if (base >= buf->len)
		return -EINVAL;

	return xdr_xcode_array2(buf, base, desc, 0);
}
EXPORT_SYMBOL_GPL(xdr_decode_array2);

int xdr_encode_array2(const struct xdr_buf *buf, unsigned int base,
		      struct xdr_array2_desc *desc)
{
	if ((unsigned long) base + 4 + desc->array_len * desc->elem_size >
	    buf->head->iov_len + buf->page_len + buf->tail->iov_len)
		return -EINVAL;

	return xdr_xcode_array2(buf, base, desc, 1);
}
EXPORT_SYMBOL_GPL(xdr_encode_array2);

int xdr_process_buf(const struct xdr_buf *buf, unsigned int offset,
		    unsigned int len,
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
		char *s = kmemdup_nul(p, ret, gfp_flags);
		if (s != NULL) {
			*str = s;
			return strlen(s);
		}
		ret = -ENOMEM;
	}
	*str = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(xdr_stream_decode_string_dup);

/**
 * xdr_stream_decode_opaque_auth - Decode struct opaque_auth (RFC5531 S8.2)
 * @xdr: pointer to xdr_stream
 * @flavor: location to store decoded flavor
 * @body: location to store decode body
 * @body_len: location to store length of decoded body
 *
 * Return values:
 *   On success, returns the number of buffer bytes consumed
 *   %-EBADMSG on XDR buffer overflow
 *   %-EMSGSIZE if the decoded size of the body field exceeds 400 octets
 */
ssize_t xdr_stream_decode_opaque_auth(struct xdr_stream *xdr, u32 *flavor,
				      void **body, unsigned int *body_len)
{
	ssize_t ret, len;

	len = xdr_stream_decode_u32(xdr, flavor);
	if (unlikely(len < 0))
		return len;
	ret = xdr_stream_decode_opaque_inline(xdr, body, RPC_MAX_AUTH_SIZE);
	if (unlikely(ret < 0))
		return ret;
	*body_len = ret;
	return len + ret;
}
EXPORT_SYMBOL_GPL(xdr_stream_decode_opaque_auth);

/**
 * xdr_stream_encode_opaque_auth - Encode struct opaque_auth (RFC5531 S8.2)
 * @xdr: pointer to xdr_stream
 * @flavor: verifier flavor to encode
 * @body: content of body to encode
 * @body_len: length of body to encode
 *
 * Return values:
 *   On success, returns length in bytes of XDR buffer consumed
 *   %-EBADMSG on XDR buffer overflow
 *   %-EMSGSIZE if the size of @body exceeds 400 octets
 */
ssize_t xdr_stream_encode_opaque_auth(struct xdr_stream *xdr, u32 flavor,
				      void *body, unsigned int body_len)
{
	ssize_t ret, len;

	if (unlikely(body_len > RPC_MAX_AUTH_SIZE))
		return -EMSGSIZE;
	len = xdr_stream_encode_u32(xdr, flavor);
	if (unlikely(len < 0))
		return len;
	ret = xdr_stream_encode_opaque(xdr, body, body_len);
	if (unlikely(ret < 0))
		return ret;
	return len + ret;
}
EXPORT_SYMBOL_GPL(xdr_stream_encode_opaque_auth);
