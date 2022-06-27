// SPDX-License-Identifier: GPL-2.0-only
#include <crypto/hash.h>
#include <linux/export.h>
#include <linux/bvec.h>
#include <linux/fault-inject-usercopy.h>
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include <net/checksum.h>
#include <linux/scatterlist.h>
#include <linux/instrumented.h>

#define PIPE_PARANOIA /* for now */

/* covers iovec and kvec alike */
#define iterate_iovec(i, n, base, len, off, __p, STEP) {	\
	size_t off = 0;						\
	size_t skip = i->iov_offset;				\
	do {							\
		len = min(n, __p->iov_len - skip);		\
		if (likely(len)) {				\
			base = __p->iov_base + skip;		\
			len -= (STEP);				\
			off += len;				\
			skip += len;				\
			n -= len;				\
			if (skip < __p->iov_len)		\
				break;				\
		}						\
		__p++;						\
		skip = 0;					\
	} while (n);						\
	i->iov_offset = skip;					\
	n = off;						\
}

#define iterate_bvec(i, n, base, len, off, p, STEP) {		\
	size_t off = 0;						\
	unsigned skip = i->iov_offset;				\
	while (n) {						\
		unsigned offset = p->bv_offset + skip;		\
		unsigned left;					\
		void *kaddr = kmap_local_page(p->bv_page +	\
					offset / PAGE_SIZE);	\
		base = kaddr + offset % PAGE_SIZE;		\
		len = min(min(n, (size_t)(p->bv_len - skip)),	\
		     (size_t)(PAGE_SIZE - offset % PAGE_SIZE));	\
		left = (STEP);					\
		kunmap_local(kaddr);				\
		len -= left;					\
		off += len;					\
		skip += len;					\
		if (skip == p->bv_len) {			\
			skip = 0;				\
			p++;					\
		}						\
		n -= len;					\
		if (left)					\
			break;					\
	}							\
	i->iov_offset = skip;					\
	n = off;						\
}

#define iterate_xarray(i, n, base, len, __off, STEP) {		\
	__label__ __out;					\
	size_t __off = 0;					\
	struct page *head = NULL;				\
	loff_t start = i->xarray_start + i->iov_offset;		\
	unsigned offset = start % PAGE_SIZE;			\
	pgoff_t index = start / PAGE_SIZE;			\
	int j;							\
								\
	XA_STATE(xas, i->xarray, index);			\
								\
	rcu_read_lock();					\
	xas_for_each(&xas, head, ULONG_MAX) {			\
		unsigned left;					\
		if (xas_retry(&xas, head))			\
			continue;				\
		if (WARN_ON(xa_is_value(head)))			\
			break;					\
		if (WARN_ON(PageHuge(head)))			\
			break;					\
		for (j = (head->index < index) ? index - head->index : 0; \
		     j < thp_nr_pages(head); j++) {		\
			void *kaddr = kmap_local_page(head + j);	\
			base = kaddr + offset;			\
			len = PAGE_SIZE - offset;		\
			len = min(n, len);			\
			left = (STEP);				\
			kunmap_local(kaddr);			\
			len -= left;				\
			__off += len;				\
			n -= len;				\
			if (left || n == 0)			\
				goto __out;			\
			offset = 0;				\
		}						\
	}							\
__out:								\
	rcu_read_unlock();					\
	i->iov_offset += __off;						\
	n = __off;						\
}

#define __iterate_and_advance(i, n, base, len, off, I, K) {	\
	if (unlikely(i->count < n))				\
		n = i->count;					\
	if (likely(n)) {					\
		if (likely(iter_is_iovec(i))) {			\
			const struct iovec *iov = i->iov;	\
			void __user *base;			\
			size_t len;				\
			iterate_iovec(i, n, base, len, off,	\
						iov, (I))	\
			i->nr_segs -= iov - i->iov;		\
			i->iov = iov;				\
		} else if (iov_iter_is_bvec(i)) {		\
			const struct bio_vec *bvec = i->bvec;	\
			void *base;				\
			size_t len;				\
			iterate_bvec(i, n, base, len, off,	\
						bvec, (K))	\
			i->nr_segs -= bvec - i->bvec;		\
			i->bvec = bvec;				\
		} else if (iov_iter_is_kvec(i)) {		\
			const struct kvec *kvec = i->kvec;	\
			void *base;				\
			size_t len;				\
			iterate_iovec(i, n, base, len, off,	\
						kvec, (K))	\
			i->nr_segs -= kvec - i->kvec;		\
			i->kvec = kvec;				\
		} else if (iov_iter_is_xarray(i)) {		\
			void *base;				\
			size_t len;				\
			iterate_xarray(i, n, base, len, off,	\
							(K))	\
		}						\
		i->count -= n;					\
	}							\
}
#define iterate_and_advance(i, n, base, len, off, I, K) \
	__iterate_and_advance(i, n, base, len, off, I, ((void)(K),0))

static int copyout(void __user *to, const void *from, size_t n)
{
	if (should_fail_usercopy())
		return n;
	if (access_ok(to, n)) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}

static int copyin(void *to, const void __user *from, size_t n)
{
	if (should_fail_usercopy())
		return n;
	if (access_ok(from, n)) {
		instrument_copy_from_user(to, from, n);
		n = raw_copy_from_user(to, from, n);
	}
	return n;
}

static size_t copy_page_to_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	void *kaddr, *from;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	might_fault();
	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	if (IS_ENABLED(CONFIG_HIGHMEM) && !fault_in_writeable(buf, copy)) {
		kaddr = kmap_atomic(page);
		from = kaddr + offset;

		/* first chunk, usually the only one */
		left = copyout(buf, from, copy);
		copy -= left;
		skip += copy;
		from += copy;
		bytes -= copy;

		while (unlikely(!left && bytes)) {
			iov++;
			buf = iov->iov_base;
			copy = min(bytes, iov->iov_len);
			left = copyout(buf, from, copy);
			copy -= left;
			skip = copy;
			from += copy;
			bytes -= copy;
		}
		if (likely(!bytes)) {
			kunmap_atomic(kaddr);
			goto done;
		}
		offset = from - kaddr;
		buf += copy;
		kunmap_atomic(kaddr);
		copy = min(bytes, iov->iov_len - skip);
	}
	/* Too bad - revert to non-atomic kmap */

	kaddr = kmap(page);
	from = kaddr + offset;
	left = copyout(buf, from, copy);
	copy -= left;
	skip += copy;
	from += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = copyout(buf, from, copy);
		copy -= left;
		skip = copy;
		from += copy;
		bytes -= copy;
	}
	kunmap(page);

done:
	if (skip == iov->iov_len) {
		iov++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	return wanted - bytes;
}

static size_t copy_page_from_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	void *kaddr, *to;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	might_fault();
	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	if (IS_ENABLED(CONFIG_HIGHMEM) && !fault_in_readable(buf, copy)) {
		kaddr = kmap_atomic(page);
		to = kaddr + offset;

		/* first chunk, usually the only one */
		left = copyin(to, buf, copy);
		copy -= left;
		skip += copy;
		to += copy;
		bytes -= copy;

		while (unlikely(!left && bytes)) {
			iov++;
			buf = iov->iov_base;
			copy = min(bytes, iov->iov_len);
			left = copyin(to, buf, copy);
			copy -= left;
			skip = copy;
			to += copy;
			bytes -= copy;
		}
		if (likely(!bytes)) {
			kunmap_atomic(kaddr);
			goto done;
		}
		offset = to - kaddr;
		buf += copy;
		kunmap_atomic(kaddr);
		copy = min(bytes, iov->iov_len - skip);
	}
	/* Too bad - revert to non-atomic kmap */

	kaddr = kmap(page);
	to = kaddr + offset;
	left = copyin(to, buf, copy);
	copy -= left;
	skip += copy;
	to += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = copyin(to, buf, copy);
		copy -= left;
		skip = copy;
		to += copy;
		bytes -= copy;
	}
	kunmap(page);

done:
	if (skip == iov->iov_len) {
		iov++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	return wanted - bytes;
}

#ifdef PIPE_PARANOIA
static bool sanity(const struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_head = pipe->head;
	unsigned int p_tail = pipe->tail;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int p_occupancy = pipe_occupancy(p_head, p_tail);
	unsigned int i_head = i->head;
	unsigned int idx;

	if (i->iov_offset) {
		struct pipe_buffer *p;
		if (unlikely(p_occupancy == 0))
			goto Bad;	// pipe must be non-empty
		if (unlikely(i_head != p_head - 1))
			goto Bad;	// must be at the last buffer...

		p = &pipe->bufs[i_head & p_mask];
		if (unlikely(p->offset + p->len != i->iov_offset))
			goto Bad;	// ... at the end of segment
	} else {
		if (i_head != p_head)
			goto Bad;	// must be right after the last buffer
	}
	return true;
Bad:
	printk(KERN_ERR "idx = %d, offset = %zd\n", i_head, i->iov_offset);
	printk(KERN_ERR "head = %d, tail = %d, buffers = %d\n",
			p_head, p_tail, pipe->ring_size);
	for (idx = 0; idx < pipe->ring_size; idx++)
		printk(KERN_ERR "[%p %p %d %d]\n",
			pipe->bufs[idx].ops,
			pipe->bufs[idx].page,
			pipe->bufs[idx].offset,
			pipe->bufs[idx].len);
	WARN_ON(1);
	return false;
}
#else
#define sanity(i) true
#endif

static size_t copy_page_to_iter_pipe(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	struct pipe_buffer *buf;
	unsigned int p_tail = pipe->tail;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int i_head = i->head;
	size_t off;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	if (!sanity(i))
		return 0;

	off = i->iov_offset;
	buf = &pipe->bufs[i_head & p_mask];
	if (off) {
		if (offset == off && buf->page == page) {
			/* merge with the last one */
			buf->len += bytes;
			i->iov_offset += bytes;
			goto out;
		}
		i_head++;
		buf = &pipe->bufs[i_head & p_mask];
	}
	if (pipe_full(i_head, p_tail, pipe->max_usage))
		return 0;

	buf->ops = &page_cache_pipe_buf_ops;
	buf->flags = 0;
	get_page(page);
	buf->page = page;
	buf->offset = offset;
	buf->len = bytes;

	pipe->head = i_head + 1;
	i->iov_offset = offset + bytes;
	i->head = i_head;
out:
	i->count -= bytes;
	return bytes;
}

/*
 * fault_in_iov_iter_readable - fault in iov iterator for reading
 * @i: iterator
 * @size: maximum length
 *
 * Fault in one or more iovecs of the given iov_iter, to a maximum length of
 * @size.  For each iovec, fault in each page that constitutes the iovec.
 *
 * Returns the number of bytes not faulted in (like copy_to_user() and
 * copy_from_user()).
 *
 * Always returns 0 for non-userspace iterators.
 */
size_t fault_in_iov_iter_readable(const struct iov_iter *i, size_t size)
{
	if (iter_is_iovec(i)) {
		size_t count = min(size, iov_iter_count(i));
		const struct iovec *p;
		size_t skip;

		size -= count;
		for (p = i->iov, skip = i->iov_offset; count; p++, skip = 0) {
			size_t len = min(count, p->iov_len - skip);
			size_t ret;

			if (unlikely(!len))
				continue;
			ret = fault_in_readable(p->iov_base + skip, len);
			count -= len - ret;
			if (ret)
				break;
		}
		return count + size;
	}
	return 0;
}
EXPORT_SYMBOL(fault_in_iov_iter_readable);

/*
 * fault_in_iov_iter_writeable - fault in iov iterator for writing
 * @i: iterator
 * @size: maximum length
 *
 * Faults in the iterator using get_user_pages(), i.e., without triggering
 * hardware page faults.  This is primarily useful when we already know that
 * some or all of the pages in @i aren't in memory.
 *
 * Returns the number of bytes not faulted in, like copy_to_user() and
 * copy_from_user().
 *
 * Always returns 0 for non-user-space iterators.
 */
size_t fault_in_iov_iter_writeable(const struct iov_iter *i, size_t size)
{
	if (iter_is_iovec(i)) {
		size_t count = min(size, iov_iter_count(i));
		const struct iovec *p;
		size_t skip;

		size -= count;
		for (p = i->iov, skip = i->iov_offset; count; p++, skip = 0) {
			size_t len = min(count, p->iov_len - skip);
			size_t ret;

			if (unlikely(!len))
				continue;
			ret = fault_in_safe_writeable(p->iov_base + skip, len);
			count -= len - ret;
			if (ret)
				break;
		}
		return count + size;
	}
	return 0;
}
EXPORT_SYMBOL(fault_in_iov_iter_writeable);

void iov_iter_init(struct iov_iter *i, unsigned int direction,
			const struct iovec *iov, unsigned long nr_segs,
			size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	*i = (struct iov_iter) {
		.iter_type = ITER_IOVEC,
		.nofault = false,
		.data_source = direction,
		.iov = iov,
		.nr_segs = nr_segs,
		.iov_offset = 0,
		.count = count
	};
}
EXPORT_SYMBOL(iov_iter_init);

static inline bool allocated(struct pipe_buffer *buf)
{
	return buf->ops == &default_pipe_buf_ops;
}

static inline void data_start(const struct iov_iter *i,
			      unsigned int *iter_headp, size_t *offp)
{
	unsigned int p_mask = i->pipe->ring_size - 1;
	unsigned int iter_head = i->head;
	size_t off = i->iov_offset;

	if (off && (!allocated(&i->pipe->bufs[iter_head & p_mask]) ||
		    off == PAGE_SIZE)) {
		iter_head++;
		off = 0;
	}
	*iter_headp = iter_head;
	*offp = off;
}

static size_t push_pipe(struct iov_iter *i, size_t size,
			int *iter_headp, size_t *offp)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_tail = pipe->tail;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int iter_head;
	size_t off;
	ssize_t left;

	if (unlikely(size > i->count))
		size = i->count;
	if (unlikely(!size))
		return 0;

	left = size;
	data_start(i, &iter_head, &off);
	*iter_headp = iter_head;
	*offp = off;
	if (off) {
		left -= PAGE_SIZE - off;
		if (left <= 0) {
			pipe->bufs[iter_head & p_mask].len += size;
			return size;
		}
		pipe->bufs[iter_head & p_mask].len = PAGE_SIZE;
		iter_head++;
	}
	while (!pipe_full(iter_head, p_tail, pipe->max_usage)) {
		struct pipe_buffer *buf = &pipe->bufs[iter_head & p_mask];
		struct page *page = alloc_page(GFP_USER);
		if (!page)
			break;

		buf->ops = &default_pipe_buf_ops;
		buf->flags = 0;
		buf->page = page;
		buf->offset = 0;
		buf->len = min_t(ssize_t, left, PAGE_SIZE);
		left -= buf->len;
		iter_head++;
		pipe->head = iter_head;

		if (left == 0)
			return size;
	}
	return size - left;
}

static size_t copy_pipe_to_iter(const void *addr, size_t bytes,
				struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int i_head;
	size_t n, off;

	if (!sanity(i))
		return 0;

	bytes = n = push_pipe(i, bytes, &i_head, &off);
	if (unlikely(!n))
		return 0;
	do {
		size_t chunk = min_t(size_t, n, PAGE_SIZE - off);
		memcpy_to_page(pipe->bufs[i_head & p_mask].page, off, addr, chunk);
		i->head = i_head;
		i->iov_offset = off + chunk;
		n -= chunk;
		addr += chunk;
		off = 0;
		i_head++;
	} while (n);
	i->count -= bytes;
	return bytes;
}

static __wsum csum_and_memcpy(void *to, const void *from, size_t len,
			      __wsum sum, size_t off)
{
	__wsum next = csum_partial_copy_nocheck(from, to, len);
	return csum_block_add(sum, next, off);
}

static size_t csum_and_copy_to_pipe_iter(const void *addr, size_t bytes,
					 struct iov_iter *i, __wsum *sump)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_mask = pipe->ring_size - 1;
	__wsum sum = *sump;
	size_t off = 0;
	unsigned int i_head;
	size_t r;

	if (!sanity(i))
		return 0;

	bytes = push_pipe(i, bytes, &i_head, &r);
	while (bytes) {
		size_t chunk = min_t(size_t, bytes, PAGE_SIZE - r);
		char *p = kmap_local_page(pipe->bufs[i_head & p_mask].page);
		sum = csum_and_memcpy(p + r, addr + off, chunk, sum, off);
		kunmap_local(p);
		i->head = i_head;
		i->iov_offset = r + chunk;
		bytes -= chunk;
		off += chunk;
		r = 0;
		i_head++;
	}
	*sump = sum;
	i->count -= off;
	return off;
}

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i)))
		return copy_pipe_to_iter(addr, bytes, i);
	if (iter_is_iovec(i))
		might_fault();
	iterate_and_advance(i, bytes, base, len, off,
		copyout(base, addr + off, len),
		memcpy(base, addr + off, len)
	)

	return bytes;
}
EXPORT_SYMBOL(_copy_to_iter);

#ifdef CONFIG_ARCH_HAS_COPY_MC
static int copyout_mc(void __user *to, const void *from, size_t n)
{
	if (access_ok(to, n)) {
		instrument_copy_to_user(to, from, n);
		n = copy_mc_to_user((__force void *) to, from, n);
	}
	return n;
}

static size_t copy_mc_pipe_to_iter(const void *addr, size_t bytes,
				struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int i_head;
	size_t n, off, xfer = 0;

	if (!sanity(i))
		return 0;

	n = push_pipe(i, bytes, &i_head, &off);
	while (n) {
		size_t chunk = min_t(size_t, n, PAGE_SIZE - off);
		char *p = kmap_local_page(pipe->bufs[i_head & p_mask].page);
		unsigned long rem;
		rem = copy_mc_to_kernel(p + off, addr + xfer, chunk);
		chunk -= rem;
		kunmap_local(p);
		i->head = i_head;
		i->iov_offset = off + chunk;
		xfer += chunk;
		if (rem)
			break;
		n -= chunk;
		off = 0;
		i_head++;
	}
	i->count -= xfer;
	return xfer;
}

/**
 * _copy_mc_to_iter - copy to iter with source memory error exception handling
 * @addr: source kernel address
 * @bytes: total transfer length
 * @i: destination iterator
 *
 * The pmem driver deploys this for the dax operation
 * (dax_copy_to_iter()) for dax reads (bypass page-cache and the
 * block-layer). Upon #MC read(2) aborts and returns EIO or the bytes
 * successfully copied.
 *
 * The main differences between this and typical _copy_to_iter().
 *
 * * Typical tail/residue handling after a fault retries the copy
 *   byte-by-byte until the fault happens again. Re-triggering machine
 *   checks is potentially fatal so the implementation uses source
 *   alignment and poison alignment assumptions to avoid re-triggering
 *   hardware exceptions.
 *
 * * ITER_KVEC, ITER_PIPE, and ITER_BVEC can return short copies.
 *   Compare to copy_to_iter() where only ITER_IOVEC attempts might return
 *   a short copy.
 *
 * Return: number of bytes copied (may be %0)
 */
size_t _copy_mc_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i)))
		return copy_mc_pipe_to_iter(addr, bytes, i);
	if (iter_is_iovec(i))
		might_fault();
	__iterate_and_advance(i, bytes, base, len, off,
		copyout_mc(base, addr + off, len),
		copy_mc_to_kernel(base, addr + off, len)
	)

	return bytes;
}
EXPORT_SYMBOL_GPL(_copy_mc_to_iter);
#endif /* CONFIG_ARCH_HAS_COPY_MC */

size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return 0;
	}
	if (iter_is_iovec(i))
		might_fault();
	iterate_and_advance(i, bytes, base, len, off,
		copyin(addr + off, base, len),
		memcpy(addr + off, base, len)
	)

	return bytes;
}
EXPORT_SYMBOL(_copy_from_iter);

size_t _copy_from_iter_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, base, len, off,
		__copy_from_user_inatomic_nocache(addr + off, base, len),
		memcpy(addr + off, base, len)
	)

	return bytes;
}
EXPORT_SYMBOL(_copy_from_iter_nocache);

#ifdef CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE
/**
 * _copy_from_iter_flushcache - write destination through cpu cache
 * @addr: destination kernel address
 * @bytes: total transfer length
 * @i: source iterator
 *
 * The pmem driver arranges for filesystem-dax to use this facility via
 * dax_copy_from_iter() for ensuring that writes to persistent memory
 * are flushed through the CPU cache. It is differentiated from
 * _copy_from_iter_nocache() in that guarantees all data is flushed for
 * all iterator types. The _copy_from_iter_nocache() only attempts to
 * bypass the cache for the ITER_IOVEC case, and on some archs may use
 * instructions that strand dirty-data in the cache.
 *
 * Return: number of bytes copied (may be %0)
 */
size_t _copy_from_iter_flushcache(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, base, len, off,
		__copy_from_user_flushcache(addr + off, base, len),
		memcpy_flushcache(addr + off, base, len)
	)

	return bytes;
}
EXPORT_SYMBOL_GPL(_copy_from_iter_flushcache);
#endif

static inline bool page_copy_sane(struct page *page, size_t offset, size_t n)
{
	struct page *head;
	size_t v = n + offset;

	/*
	 * The general case needs to access the page order in order
	 * to compute the page size.
	 * However, we mostly deal with order-0 pages and thus can
	 * avoid a possible cache line miss for requests that fit all
	 * page orders.
	 */
	if (n <= v && v <= PAGE_SIZE)
		return true;

	head = compound_head(page);
	v += (page - head) << PAGE_SHIFT;

	if (likely(n <= v && v <= (page_size(head))))
		return true;
	WARN_ON(1);
	return false;
}

static size_t __copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (likely(iter_is_iovec(i)))
		return copy_page_to_iter_iovec(page, offset, bytes, i);
	if (iov_iter_is_bvec(i) || iov_iter_is_kvec(i) || iov_iter_is_xarray(i)) {
		void *kaddr = kmap_local_page(page);
		size_t wanted = _copy_to_iter(kaddr + offset, bytes, i);
		kunmap_local(kaddr);
		return wanted;
	}
	if (iov_iter_is_pipe(i))
		return copy_page_to_iter_pipe(page, offset, bytes, i);
	if (unlikely(iov_iter_is_discard(i))) {
		if (unlikely(i->count < bytes))
			bytes = i->count;
		i->count -= bytes;
		return bytes;
	}
	WARN_ON(1);
	return 0;
}

size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t res = 0;
	if (unlikely(!page_copy_sane(page, offset, bytes)))
		return 0;
	page += offset / PAGE_SIZE; // first subpage
	offset %= PAGE_SIZE;
	while (1) {
		size_t n = __copy_page_to_iter(page, offset,
				min(bytes, (size_t)PAGE_SIZE - offset), i);
		res += n;
		bytes -= n;
		if (!bytes || !n)
			break;
		offset += n;
		if (offset == PAGE_SIZE) {
			page++;
			offset = 0;
		}
	}
	return res;
}
EXPORT_SYMBOL(copy_page_to_iter);

size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (unlikely(!page_copy_sane(page, offset, bytes)))
		return 0;
	if (likely(iter_is_iovec(i)))
		return copy_page_from_iter_iovec(page, offset, bytes, i);
	if (iov_iter_is_bvec(i) || iov_iter_is_kvec(i) || iov_iter_is_xarray(i)) {
		void *kaddr = kmap_local_page(page);
		size_t wanted = _copy_from_iter(kaddr + offset, bytes, i);
		kunmap_local(kaddr);
		return wanted;
	}
	WARN_ON(1);
	return 0;
}
EXPORT_SYMBOL(copy_page_from_iter);

static size_t pipe_zero(size_t bytes, struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int i_head;
	size_t n, off;

	if (!sanity(i))
		return 0;

	bytes = n = push_pipe(i, bytes, &i_head, &off);
	if (unlikely(!n))
		return 0;

	do {
		size_t chunk = min_t(size_t, n, PAGE_SIZE - off);
		char *p = kmap_local_page(pipe->bufs[i_head & p_mask].page);
		memset(p + off, 0, chunk);
		kunmap_local(p);
		i->head = i_head;
		i->iov_offset = off + chunk;
		n -= chunk;
		off = 0;
		i_head++;
	} while (n);
	i->count -= bytes;
	return bytes;
}

size_t iov_iter_zero(size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i)))
		return pipe_zero(bytes, i);
	iterate_and_advance(i, bytes, base, len, count,
		clear_user(base, len),
		memset(base, 0, len)
	)

	return bytes;
}
EXPORT_SYMBOL(iov_iter_zero);

size_t copy_page_from_iter_atomic(struct page *page, unsigned offset, size_t bytes,
				  struct iov_iter *i)
{
	char *kaddr = kmap_atomic(page), *p = kaddr + offset;
	if (unlikely(!page_copy_sane(page, offset, bytes))) {
		kunmap_atomic(kaddr);
		return 0;
	}
	if (unlikely(iov_iter_is_pipe(i) || iov_iter_is_discard(i))) {
		kunmap_atomic(kaddr);
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, base, len, off,
		copyin(p + off, base, len),
		memcpy(p + off, base, len)
	)
	kunmap_atomic(kaddr);
	return bytes;
}
EXPORT_SYMBOL(copy_page_from_iter_atomic);

static inline void pipe_truncate(struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_tail = pipe->tail;
	unsigned int p_head = pipe->head;
	unsigned int p_mask = pipe->ring_size - 1;

	if (!pipe_empty(p_head, p_tail)) {
		struct pipe_buffer *buf;
		unsigned int i_head = i->head;
		size_t off = i->iov_offset;

		if (off) {
			buf = &pipe->bufs[i_head & p_mask];
			buf->len = off - buf->offset;
			i_head++;
		}
		while (p_head != i_head) {
			p_head--;
			pipe_buf_release(pipe, &pipe->bufs[p_head & p_mask]);
		}

		pipe->head = p_head;
	}
}

static void pipe_advance(struct iov_iter *i, size_t size)
{
	struct pipe_inode_info *pipe = i->pipe;
	if (size) {
		struct pipe_buffer *buf;
		unsigned int p_mask = pipe->ring_size - 1;
		unsigned int i_head = i->head;
		size_t off = i->iov_offset, left = size;

		if (off) /* make it relative to the beginning of buffer */
			left += off - pipe->bufs[i_head & p_mask].offset;
		while (1) {
			buf = &pipe->bufs[i_head & p_mask];
			if (left <= buf->len)
				break;
			left -= buf->len;
			i_head++;
		}
		i->head = i_head;
		i->iov_offset = buf->offset + left;
	}
	i->count -= size;
	/* ... and discard everything past that point */
	pipe_truncate(i);
}

static void iov_iter_bvec_advance(struct iov_iter *i, size_t size)
{
	struct bvec_iter bi;

	bi.bi_size = i->count;
	bi.bi_bvec_done = i->iov_offset;
	bi.bi_idx = 0;
	bvec_iter_advance(i->bvec, &bi, size);

	i->bvec += bi.bi_idx;
	i->nr_segs -= bi.bi_idx;
	i->count = bi.bi_size;
	i->iov_offset = bi.bi_bvec_done;
}

static void iov_iter_iovec_advance(struct iov_iter *i, size_t size)
{
	const struct iovec *iov, *end;

	if (!i->count)
		return;
	i->count -= size;

	size += i->iov_offset; // from beginning of current segment
	for (iov = i->iov, end = iov + i->nr_segs; iov < end; iov++) {
		if (likely(size < iov->iov_len))
			break;
		size -= iov->iov_len;
	}
	i->iov_offset = size;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
}

void iov_iter_advance(struct iov_iter *i, size_t size)
{
	if (unlikely(i->count < size))
		size = i->count;
	if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i))) {
		/* iovec and kvec have identical layouts */
		iov_iter_iovec_advance(i, size);
	} else if (iov_iter_is_bvec(i)) {
		iov_iter_bvec_advance(i, size);
	} else if (iov_iter_is_pipe(i)) {
		pipe_advance(i, size);
	} else if (unlikely(iov_iter_is_xarray(i))) {
		i->iov_offset += size;
		i->count -= size;
	} else if (iov_iter_is_discard(i)) {
		i->count -= size;
	}
}
EXPORT_SYMBOL(iov_iter_advance);

void iov_iter_revert(struct iov_iter *i, size_t unroll)
{
	if (!unroll)
		return;
	if (WARN_ON(unroll > MAX_RW_COUNT))
		return;
	i->count += unroll;
	if (unlikely(iov_iter_is_pipe(i))) {
		struct pipe_inode_info *pipe = i->pipe;
		unsigned int p_mask = pipe->ring_size - 1;
		unsigned int i_head = i->head;
		size_t off = i->iov_offset;
		while (1) {
			struct pipe_buffer *b = &pipe->bufs[i_head & p_mask];
			size_t n = off - b->offset;
			if (unroll < n) {
				off -= unroll;
				break;
			}
			unroll -= n;
			if (!unroll && i_head == i->start_head) {
				off = 0;
				break;
			}
			i_head--;
			b = &pipe->bufs[i_head & p_mask];
			off = b->offset + b->len;
		}
		i->iov_offset = off;
		i->head = i_head;
		pipe_truncate(i);
		return;
	}
	if (unlikely(iov_iter_is_discard(i)))
		return;
	if (unroll <= i->iov_offset) {
		i->iov_offset -= unroll;
		return;
	}
	unroll -= i->iov_offset;
	if (iov_iter_is_xarray(i)) {
		BUG(); /* We should never go beyond the start of the specified
			* range since we might then be straying into pages that
			* aren't pinned.
			*/
	} else if (iov_iter_is_bvec(i)) {
		const struct bio_vec *bvec = i->bvec;
		while (1) {
			size_t n = (--bvec)->bv_len;
			i->nr_segs++;
			if (unroll <= n) {
				i->bvec = bvec;
				i->iov_offset = n - unroll;
				return;
			}
			unroll -= n;
		}
	} else { /* same logics for iovec and kvec */
		const struct iovec *iov = i->iov;
		while (1) {
			size_t n = (--iov)->iov_len;
			i->nr_segs++;
			if (unroll <= n) {
				i->iov = iov;
				i->iov_offset = n - unroll;
				return;
			}
			unroll -= n;
		}
	}
}
EXPORT_SYMBOL(iov_iter_revert);

/*
 * Return the count of just the current iov_iter segment.
 */
size_t iov_iter_single_seg_count(const struct iov_iter *i)
{
	if (i->nr_segs > 1) {
		if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i)))
			return min(i->count, i->iov->iov_len - i->iov_offset);
		if (iov_iter_is_bvec(i))
			return min(i->count, i->bvec->bv_len - i->iov_offset);
	}
	return i->count;
}
EXPORT_SYMBOL(iov_iter_single_seg_count);

void iov_iter_kvec(struct iov_iter *i, unsigned int direction,
			const struct kvec *kvec, unsigned long nr_segs,
			size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	*i = (struct iov_iter){
		.iter_type = ITER_KVEC,
		.data_source = direction,
		.kvec = kvec,
		.nr_segs = nr_segs,
		.iov_offset = 0,
		.count = count
	};
}
EXPORT_SYMBOL(iov_iter_kvec);

void iov_iter_bvec(struct iov_iter *i, unsigned int direction,
			const struct bio_vec *bvec, unsigned long nr_segs,
			size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	*i = (struct iov_iter){
		.iter_type = ITER_BVEC,
		.data_source = direction,
		.bvec = bvec,
		.nr_segs = nr_segs,
		.iov_offset = 0,
		.count = count
	};
}
EXPORT_SYMBOL(iov_iter_bvec);

void iov_iter_pipe(struct iov_iter *i, unsigned int direction,
			struct pipe_inode_info *pipe,
			size_t count)
{
	BUG_ON(direction != READ);
	WARN_ON(pipe_full(pipe->head, pipe->tail, pipe->ring_size));
	*i = (struct iov_iter){
		.iter_type = ITER_PIPE,
		.data_source = false,
		.pipe = pipe,
		.head = pipe->head,
		.start_head = pipe->head,
		.iov_offset = 0,
		.count = count
	};
}
EXPORT_SYMBOL(iov_iter_pipe);

/**
 * iov_iter_xarray - Initialise an I/O iterator to use the pages in an xarray
 * @i: The iterator to initialise.
 * @direction: The direction of the transfer.
 * @xarray: The xarray to access.
 * @start: The start file position.
 * @count: The size of the I/O buffer in bytes.
 *
 * Set up an I/O iterator to either draw data out of the pages attached to an
 * inode or to inject data into those pages.  The pages *must* be prevented
 * from evaporation, either by taking a ref on them or locking them by the
 * caller.
 */
void iov_iter_xarray(struct iov_iter *i, unsigned int direction,
		     struct xarray *xarray, loff_t start, size_t count)
{
	BUG_ON(direction & ~1);
	*i = (struct iov_iter) {
		.iter_type = ITER_XARRAY,
		.data_source = direction,
		.xarray = xarray,
		.xarray_start = start,
		.count = count,
		.iov_offset = 0
	};
}
EXPORT_SYMBOL(iov_iter_xarray);

/**
 * iov_iter_discard - Initialise an I/O iterator that discards data
 * @i: The iterator to initialise.
 * @direction: The direction of the transfer.
 * @count: The size of the I/O buffer in bytes.
 *
 * Set up an I/O iterator that just discards everything that's written to it.
 * It's only available as a READ iterator.
 */
void iov_iter_discard(struct iov_iter *i, unsigned int direction, size_t count)
{
	BUG_ON(direction != READ);
	*i = (struct iov_iter){
		.iter_type = ITER_DISCARD,
		.data_source = false,
		.count = count,
		.iov_offset = 0
	};
}
EXPORT_SYMBOL(iov_iter_discard);

static unsigned long iov_iter_alignment_iovec(const struct iov_iter *i)
{
	unsigned long res = 0;
	size_t size = i->count;
	size_t skip = i->iov_offset;
	unsigned k;

	for (k = 0; k < i->nr_segs; k++, skip = 0) {
		size_t len = i->iov[k].iov_len - skip;
		if (len) {
			res |= (unsigned long)i->iov[k].iov_base + skip;
			if (len > size)
				len = size;
			res |= len;
			size -= len;
			if (!size)
				break;
		}
	}
	return res;
}

static unsigned long iov_iter_alignment_bvec(const struct iov_iter *i)
{
	unsigned res = 0;
	size_t size = i->count;
	unsigned skip = i->iov_offset;
	unsigned k;

	for (k = 0; k < i->nr_segs; k++, skip = 0) {
		size_t len = i->bvec[k].bv_len - skip;
		res |= (unsigned long)i->bvec[k].bv_offset + skip;
		if (len > size)
			len = size;
		res |= len;
		size -= len;
		if (!size)
			break;
	}
	return res;
}

unsigned long iov_iter_alignment(const struct iov_iter *i)
{
	/* iovec and kvec have identical layouts */
	if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i)))
		return iov_iter_alignment_iovec(i);

	if (iov_iter_is_bvec(i))
		return iov_iter_alignment_bvec(i);

	if (iov_iter_is_pipe(i)) {
		unsigned int p_mask = i->pipe->ring_size - 1;
		size_t size = i->count;

		if (size && i->iov_offset && allocated(&i->pipe->bufs[i->head & p_mask]))
			return size | i->iov_offset;
		return size;
	}

	if (iov_iter_is_xarray(i))
		return (i->xarray_start + i->iov_offset) | i->count;

	return 0;
}
EXPORT_SYMBOL(iov_iter_alignment);

unsigned long iov_iter_gap_alignment(const struct iov_iter *i)
{
	unsigned long res = 0;
	unsigned long v = 0;
	size_t size = i->count;
	unsigned k;

	if (WARN_ON(!iter_is_iovec(i)))
		return ~0U;

	for (k = 0; k < i->nr_segs; k++) {
		if (i->iov[k].iov_len) {
			unsigned long base = (unsigned long)i->iov[k].iov_base;
			if (v) // if not the first one
				res |= base | v; // this start | previous end
			v = base + i->iov[k].iov_len;
			if (size <= i->iov[k].iov_len)
				break;
			size -= i->iov[k].iov_len;
		}
	}
	return res;
}
EXPORT_SYMBOL(iov_iter_gap_alignment);

static inline ssize_t __pipe_get_pages(struct iov_iter *i,
				size_t maxsize,
				struct page **pages,
				int iter_head,
				size_t *start)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_mask = pipe->ring_size - 1;
	ssize_t n = push_pipe(i, maxsize, &iter_head, start);
	if (!n)
		return -EFAULT;

	maxsize = n;
	n += *start;
	while (n > 0) {
		get_page(*pages++ = pipe->bufs[iter_head & p_mask].page);
		iter_head++;
		n -= PAGE_SIZE;
	}

	return maxsize;
}

static ssize_t pipe_get_pages(struct iov_iter *i,
		   struct page **pages, size_t maxsize, unsigned maxpages,
		   size_t *start)
{
	unsigned int iter_head, npages;
	size_t capacity;

	if (!sanity(i))
		return -EFAULT;

	data_start(i, &iter_head, start);
	/* Amount of free space: some of this one + all after this one */
	npages = pipe_space_for_user(iter_head, i->pipe->tail, i->pipe);
	capacity = min(npages, maxpages) * PAGE_SIZE - *start;

	return __pipe_get_pages(i, min(maxsize, capacity), pages, iter_head, start);
}

static ssize_t iter_xarray_populate_pages(struct page **pages, struct xarray *xa,
					  pgoff_t index, unsigned int nr_pages)
{
	XA_STATE(xas, xa, index);
	struct page *page;
	unsigned int ret = 0;

	rcu_read_lock();
	for (page = xas_load(&xas); page; page = xas_next(&xas)) {
		if (xas_retry(&xas, page))
			continue;

		/* Has the page moved or been split? */
		if (unlikely(page != xas_reload(&xas))) {
			xas_reset(&xas);
			continue;
		}

		pages[ret] = find_subpage(page, xas.xa_index);
		get_page(pages[ret]);
		if (++ret == nr_pages)
			break;
	}
	rcu_read_unlock();
	return ret;
}

static ssize_t iter_xarray_get_pages(struct iov_iter *i,
				     struct page **pages, size_t maxsize,
				     unsigned maxpages, size_t *_start_offset)
{
	unsigned nr, offset;
	pgoff_t index, count;
	size_t size = maxsize;
	loff_t pos;

	if (!size || !maxpages)
		return 0;

	pos = i->xarray_start + i->iov_offset;
	index = pos >> PAGE_SHIFT;
	offset = pos & ~PAGE_MASK;
	*_start_offset = offset;

	count = 1;
	if (size > PAGE_SIZE - offset) {
		size -= PAGE_SIZE - offset;
		count += size >> PAGE_SHIFT;
		size &= ~PAGE_MASK;
		if (size)
			count++;
	}

	if (count > maxpages)
		count = maxpages;

	nr = iter_xarray_populate_pages(pages, i->xarray, index, count);
	if (nr == 0)
		return 0;

	return min_t(size_t, nr * PAGE_SIZE - offset, maxsize);
}

/* must be done on non-empty ITER_IOVEC one */
static unsigned long first_iovec_segment(const struct iov_iter *i,
					 size_t *size, size_t *start,
					 size_t maxsize, unsigned maxpages)
{
	size_t skip;
	long k;

	for (k = 0, skip = i->iov_offset; k < i->nr_segs; k++, skip = 0) {
		unsigned long addr = (unsigned long)i->iov[k].iov_base + skip;
		size_t len = i->iov[k].iov_len - skip;

		if (unlikely(!len))
			continue;
		if (len > maxsize)
			len = maxsize;
		len += (*start = addr % PAGE_SIZE);
		if (len > maxpages * PAGE_SIZE)
			len = maxpages * PAGE_SIZE;
		*size = len;
		return addr & PAGE_MASK;
	}
	BUG(); // if it had been empty, we wouldn't get called
}

/* must be done on non-empty ITER_BVEC one */
static struct page *first_bvec_segment(const struct iov_iter *i,
				       size_t *size, size_t *start,
				       size_t maxsize, unsigned maxpages)
{
	struct page *page;
	size_t skip = i->iov_offset, len;

	len = i->bvec->bv_len - skip;
	if (len > maxsize)
		len = maxsize;
	skip += i->bvec->bv_offset;
	page = i->bvec->bv_page + skip / PAGE_SIZE;
	len += (*start = skip % PAGE_SIZE);
	if (len > maxpages * PAGE_SIZE)
		len = maxpages * PAGE_SIZE;
	*size = len;
	return page;
}

ssize_t iov_iter_get_pages(struct iov_iter *i,
		   struct page **pages, size_t maxsize, unsigned maxpages,
		   size_t *start)
{
	size_t len;
	int n, res;

	if (maxsize > i->count)
		maxsize = i->count;
	if (!maxsize)
		return 0;

	if (likely(iter_is_iovec(i))) {
		unsigned int gup_flags = 0;
		unsigned long addr;

		if (iov_iter_rw(i) != WRITE)
			gup_flags |= FOLL_WRITE;
		if (i->nofault)
			gup_flags |= FOLL_NOFAULT;

		addr = first_iovec_segment(i, &len, start, maxsize, maxpages);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		res = get_user_pages_fast(addr, n, gup_flags, pages);
		if (unlikely(res <= 0))
			return res;
		return (res == n ? len : res * PAGE_SIZE) - *start;
	}
	if (iov_iter_is_bvec(i)) {
		struct page *page;

		page = first_bvec_segment(i, &len, start, maxsize, maxpages);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		while (n--)
			get_page(*pages++ = page++);
		return len - *start;
	}
	if (iov_iter_is_pipe(i))
		return pipe_get_pages(i, pages, maxsize, maxpages, start);
	if (iov_iter_is_xarray(i))
		return iter_xarray_get_pages(i, pages, maxsize, maxpages, start);
	return -EFAULT;
}
EXPORT_SYMBOL(iov_iter_get_pages);

static struct page **get_pages_array(size_t n)
{
	return kvmalloc_array(n, sizeof(struct page *), GFP_KERNEL);
}

static ssize_t pipe_get_pages_alloc(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	struct page **p;
	unsigned int iter_head, npages;
	ssize_t n;

	if (!sanity(i))
		return -EFAULT;

	data_start(i, &iter_head, start);
	/* Amount of free space: some of this one + all after this one */
	npages = pipe_space_for_user(iter_head, i->pipe->tail, i->pipe);
	n = npages * PAGE_SIZE - *start;
	if (maxsize > n)
		maxsize = n;
	else
		npages = DIV_ROUND_UP(maxsize + *start, PAGE_SIZE);
	p = get_pages_array(npages);
	if (!p)
		return -ENOMEM;
	n = __pipe_get_pages(i, maxsize, p, iter_head, start);
	if (n > 0)
		*pages = p;
	else
		kvfree(p);
	return n;
}

static ssize_t iter_xarray_get_pages_alloc(struct iov_iter *i,
					   struct page ***pages, size_t maxsize,
					   size_t *_start_offset)
{
	struct page **p;
	unsigned nr, offset;
	pgoff_t index, count;
	size_t size = maxsize;
	loff_t pos;

	if (!size)
		return 0;

	pos = i->xarray_start + i->iov_offset;
	index = pos >> PAGE_SHIFT;
	offset = pos & ~PAGE_MASK;
	*_start_offset = offset;

	count = 1;
	if (size > PAGE_SIZE - offset) {
		size -= PAGE_SIZE - offset;
		count += size >> PAGE_SHIFT;
		size &= ~PAGE_MASK;
		if (size)
			count++;
	}

	p = get_pages_array(count);
	if (!p)
		return -ENOMEM;
	*pages = p;

	nr = iter_xarray_populate_pages(p, i->xarray, index, count);
	if (nr == 0)
		return 0;

	return min_t(size_t, nr * PAGE_SIZE - offset, maxsize);
}

ssize_t iov_iter_get_pages_alloc(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	struct page **p;
	size_t len;
	int n, res;

	if (maxsize > i->count)
		maxsize = i->count;
	if (!maxsize)
		return 0;

	if (likely(iter_is_iovec(i))) {
		unsigned int gup_flags = 0;
		unsigned long addr;

		if (iov_iter_rw(i) != WRITE)
			gup_flags |= FOLL_WRITE;
		if (i->nofault)
			gup_flags |= FOLL_NOFAULT;

		addr = first_iovec_segment(i, &len, start, maxsize, ~0U);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		p = get_pages_array(n);
		if (!p)
			return -ENOMEM;
		res = get_user_pages_fast(addr, n, gup_flags, p);
		if (unlikely(res <= 0)) {
			kvfree(p);
			*pages = NULL;
			return res;
		}
		*pages = p;
		return (res == n ? len : res * PAGE_SIZE) - *start;
	}
	if (iov_iter_is_bvec(i)) {
		struct page *page;

		page = first_bvec_segment(i, &len, start, maxsize, ~0U);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		*pages = p = get_pages_array(n);
		if (!p)
			return -ENOMEM;
		while (n--)
			get_page(*p++ = page++);
		return len - *start;
	}
	if (iov_iter_is_pipe(i))
		return pipe_get_pages_alloc(i, pages, maxsize, start);
	if (iov_iter_is_xarray(i))
		return iter_xarray_get_pages_alloc(i, pages, maxsize, start);
	return -EFAULT;
}
EXPORT_SYMBOL(iov_iter_get_pages_alloc);

size_t csum_and_copy_from_iter(void *addr, size_t bytes, __wsum *csum,
			       struct iov_iter *i)
{
	__wsum sum, next;
	sum = *csum;
	if (unlikely(iov_iter_is_pipe(i) || iov_iter_is_discard(i))) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, base, len, off, ({
		next = csum_and_copy_from_user(base, addr + off, len);
		sum = csum_block_add(sum, next, off);
		next ? 0 : len;
	}), ({
		sum = csum_and_memcpy(addr + off, base, len, sum, off);
	})
	)
	*csum = sum;
	return bytes;
}
EXPORT_SYMBOL(csum_and_copy_from_iter);

size_t csum_and_copy_to_iter(const void *addr, size_t bytes, void *_csstate,
			     struct iov_iter *i)
{
	struct csum_state *csstate = _csstate;
	__wsum sum, next;

	if (unlikely(iov_iter_is_discard(i))) {
		WARN_ON(1);	/* for now */
		return 0;
	}

	sum = csum_shift(csstate->csum, csstate->off);
	if (unlikely(iov_iter_is_pipe(i)))
		bytes = csum_and_copy_to_pipe_iter(addr, bytes, i, &sum);
	else iterate_and_advance(i, bytes, base, len, off, ({
		next = csum_and_copy_to_user(addr + off, base, len);
		sum = csum_block_add(sum, next, off);
		next ? 0 : len;
	}), ({
		sum = csum_and_memcpy(base, addr + off, len, sum, off);
	})
	)
	csstate->csum = csum_shift(sum, csstate->off);
	csstate->off += bytes;
	return bytes;
}
EXPORT_SYMBOL(csum_and_copy_to_iter);

size_t hash_and_copy_to_iter(const void *addr, size_t bytes, void *hashp,
		struct iov_iter *i)
{
#ifdef CONFIG_CRYPTO_HASH
	struct ahash_request *hash = hashp;
	struct scatterlist sg;
	size_t copied;

	copied = copy_to_iter(addr, bytes, i);
	sg_init_one(&sg, addr, copied);
	ahash_request_set_crypt(hash, &sg, NULL, copied);
	crypto_ahash_update(hash);
	return copied;
#else
	return 0;
#endif
}
EXPORT_SYMBOL(hash_and_copy_to_iter);

static int iov_npages(const struct iov_iter *i, int maxpages)
{
	size_t skip = i->iov_offset, size = i->count;
	const struct iovec *p;
	int npages = 0;

	for (p = i->iov; size; skip = 0, p++) {
		unsigned offs = offset_in_page(p->iov_base + skip);
		size_t len = min(p->iov_len - skip, size);

		if (len) {
			size -= len;
			npages += DIV_ROUND_UP(offs + len, PAGE_SIZE);
			if (unlikely(npages > maxpages))
				return maxpages;
		}
	}
	return npages;
}

static int bvec_npages(const struct iov_iter *i, int maxpages)
{
	size_t skip = i->iov_offset, size = i->count;
	const struct bio_vec *p;
	int npages = 0;

	for (p = i->bvec; size; skip = 0, p++) {
		unsigned offs = (p->bv_offset + skip) % PAGE_SIZE;
		size_t len = min(p->bv_len - skip, size);

		size -= len;
		npages += DIV_ROUND_UP(offs + len, PAGE_SIZE);
		if (unlikely(npages > maxpages))
			return maxpages;
	}
	return npages;
}

int iov_iter_npages(const struct iov_iter *i, int maxpages)
{
	if (unlikely(!i->count))
		return 0;
	/* iovec and kvec have identical layouts */
	if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i)))
		return iov_npages(i, maxpages);
	if (iov_iter_is_bvec(i))
		return bvec_npages(i, maxpages);
	if (iov_iter_is_pipe(i)) {
		unsigned int iter_head;
		int npages;
		size_t off;

		if (!sanity(i))
			return 0;

		data_start(i, &iter_head, &off);
		/* some of this one + all after this one */
		npages = pipe_space_for_user(iter_head, i->pipe->tail, i->pipe);
		return min(npages, maxpages);
	}
	if (iov_iter_is_xarray(i)) {
		unsigned offset = (i->xarray_start + i->iov_offset) % PAGE_SIZE;
		int npages = DIV_ROUND_UP(offset + i->count, PAGE_SIZE);
		return min(npages, maxpages);
	}
	return 0;
}
EXPORT_SYMBOL(iov_iter_npages);

const void *dup_iter(struct iov_iter *new, struct iov_iter *old, gfp_t flags)
{
	*new = *old;
	if (unlikely(iov_iter_is_pipe(new))) {
		WARN_ON(1);
		return NULL;
	}
	if (unlikely(iov_iter_is_discard(new) || iov_iter_is_xarray(new)))
		return NULL;
	if (iov_iter_is_bvec(new))
		return new->bvec = kmemdup(new->bvec,
				    new->nr_segs * sizeof(struct bio_vec),
				    flags);
	else
		/* iovec and kvec have identical layout */
		return new->iov = kmemdup(new->iov,
				   new->nr_segs * sizeof(struct iovec),
				   flags);
}
EXPORT_SYMBOL(dup_iter);

static int copy_compat_iovec_from_user(struct iovec *iov,
		const struct iovec __user *uvec, unsigned long nr_segs)
{
	const struct compat_iovec __user *uiov =
		(const struct compat_iovec __user *)uvec;
	int ret = -EFAULT, i;

	if (!user_access_begin(uiov, nr_segs * sizeof(*uiov)))
		return -EFAULT;

	for (i = 0; i < nr_segs; i++) {
		compat_uptr_t buf;
		compat_ssize_t len;

		unsafe_get_user(len, &uiov[i].iov_len, uaccess_end);
		unsafe_get_user(buf, &uiov[i].iov_base, uaccess_end);

		/* check for compat_size_t not fitting in compat_ssize_t .. */
		if (len < 0) {
			ret = -EINVAL;
			goto uaccess_end;
		}
		iov[i].iov_base = compat_ptr(buf);
		iov[i].iov_len = len;
	}

	ret = 0;
uaccess_end:
	user_access_end();
	return ret;
}

static int copy_iovec_from_user(struct iovec *iov,
		const struct iovec __user *uvec, unsigned long nr_segs)
{
	unsigned long seg;

	if (copy_from_user(iov, uvec, nr_segs * sizeof(*uvec)))
		return -EFAULT;
	for (seg = 0; seg < nr_segs; seg++) {
		if ((ssize_t)iov[seg].iov_len < 0)
			return -EINVAL;
	}

	return 0;
}

struct iovec *iovec_from_user(const struct iovec __user *uvec,
		unsigned long nr_segs, unsigned long fast_segs,
		struct iovec *fast_iov, bool compat)
{
	struct iovec *iov = fast_iov;
	int ret;

	/*
	 * SuS says "The readv() function *may* fail if the iovcnt argument was
	 * less than or equal to 0, or greater than {IOV_MAX}.  Linux has
	 * traditionally returned zero for zero segments, so...
	 */
	if (nr_segs == 0)
		return iov;
	if (nr_segs > UIO_MAXIOV)
		return ERR_PTR(-EINVAL);
	if (nr_segs > fast_segs) {
		iov = kmalloc_array(nr_segs, sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			return ERR_PTR(-ENOMEM);
	}

	if (compat)
		ret = copy_compat_iovec_from_user(iov, uvec, nr_segs);
	else
		ret = copy_iovec_from_user(iov, uvec, nr_segs);
	if (ret) {
		if (iov != fast_iov)
			kfree(iov);
		return ERR_PTR(ret);
	}

	return iov;
}

ssize_t __import_iovec(int type, const struct iovec __user *uvec,
		 unsigned nr_segs, unsigned fast_segs, struct iovec **iovp,
		 struct iov_iter *i, bool compat)
{
	ssize_t total_len = 0;
	unsigned long seg;
	struct iovec *iov;

	iov = iovec_from_user(uvec, nr_segs, fast_segs, *iovp, compat);
	if (IS_ERR(iov)) {
		*iovp = NULL;
		return PTR_ERR(iov);
	}

	/*
	 * According to the Single Unix Specification we should return EINVAL if
	 * an element length is < 0 when cast to ssize_t or if the total length
	 * would overflow the ssize_t return value of the system call.
	 *
	 * Linux caps all read/write calls to MAX_RW_COUNT, and avoids the
	 * overflow case.
	 */
	for (seg = 0; seg < nr_segs; seg++) {
		ssize_t len = (ssize_t)iov[seg].iov_len;

		if (!access_ok(iov[seg].iov_base, len)) {
			if (iov != *iovp)
				kfree(iov);
			*iovp = NULL;
			return -EFAULT;
		}

		if (len > MAX_RW_COUNT - total_len) {
			len = MAX_RW_COUNT - total_len;
			iov[seg].iov_len = len;
		}
		total_len += len;
	}

	iov_iter_init(i, type, iov, nr_segs, total_len);
	if (iov == *iovp)
		*iovp = NULL;
	else
		*iovp = iov;
	return total_len;
}

/**
 * import_iovec() - Copy an array of &struct iovec from userspace
 *     into the kernel, check that it is valid, and initialize a new
 *     &struct iov_iter iterator to access it.
 *
 * @type: One of %READ or %WRITE.
 * @uvec: Pointer to the userspace array.
 * @nr_segs: Number of elements in userspace array.
 * @fast_segs: Number of elements in @iov.
 * @iovp: (input and output parameter) Pointer to pointer to (usually small
 *     on-stack) kernel array.
 * @i: Pointer to iterator that will be initialized on success.
 *
 * If the array pointed to by *@iov is large enough to hold all @nr_segs,
 * then this function places %NULL in *@iov on return. Otherwise, a new
 * array will be allocated and the result placed in *@iov. This means that
 * the caller may call kfree() on *@iov regardless of whether the small
 * on-stack array was used or not (and regardless of whether this function
 * returns an error or not).
 *
 * Return: Negative error code on error, bytes imported on success
 */
ssize_t import_iovec(int type, const struct iovec __user *uvec,
		 unsigned nr_segs, unsigned fast_segs,
		 struct iovec **iovp, struct iov_iter *i)
{
	return __import_iovec(type, uvec, nr_segs, fast_segs, iovp, i,
			      in_compat_syscall());
}
EXPORT_SYMBOL(import_iovec);

int import_single_range(int rw, void __user *buf, size_t len,
		 struct iovec *iov, struct iov_iter *i)
{
	if (len > MAX_RW_COUNT)
		len = MAX_RW_COUNT;
	if (unlikely(!access_ok(buf, len)))
		return -EFAULT;

	iov->iov_base = buf;
	iov->iov_len = len;
	iov_iter_init(i, rw, iov, 1, len);
	return 0;
}
EXPORT_SYMBOL(import_single_range);

/**
 * iov_iter_restore() - Restore a &struct iov_iter to the same state as when
 *     iov_iter_save_state() was called.
 *
 * @i: &struct iov_iter to restore
 * @state: state to restore from
 *
 * Used after iov_iter_save_state() to bring restore @i, if operations may
 * have advanced it.
 *
 * Note: only works on ITER_IOVEC, ITER_BVEC, and ITER_KVEC
 */
void iov_iter_restore(struct iov_iter *i, struct iov_iter_state *state)
{
	if (WARN_ON_ONCE(!iov_iter_is_bvec(i) && !iter_is_iovec(i)) &&
			 !iov_iter_is_kvec(i))
		return;
	i->iov_offset = state->iov_offset;
	i->count = state->count;
	/*
	 * For the *vec iters, nr_segs + iov is constant - if we increment
	 * the vec, then we also decrement the nr_segs count. Hence we don't
	 * need to track both of these, just one is enough and we can deduct
	 * the other from that. ITER_KVEC and ITER_IOVEC are the same struct
	 * size, so we can just increment the iov pointer as they are unionzed.
	 * ITER_BVEC _may_ be the same size on some archs, but on others it is
	 * not. Be safe and handle it separately.
	 */
	BUILD_BUG_ON(sizeof(struct iovec) != sizeof(struct kvec));
	if (iov_iter_is_bvec(i))
		i->bvec -= state->nr_segs - i->nr_segs;
	else
		i->iov -= state->nr_segs - i->nr_segs;
	i->nr_segs = state->nr_segs;
}
