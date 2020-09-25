// SPDX-License-Identifier: GPL-2.0-only
#include <crypto/hash.h>
#include <linux/export.h>
#include <linux/bvec.h>
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include <net/checksum.h>
#include <linux/scatterlist.h>
#include <linux/instrumented.h>

#define PIPE_PARANOIA /* for now */

#define iterate_iovec(i, n, __v, __p, skip, STEP) {	\
	size_t left;					\
	size_t wanted = n;				\
	__p = i->iov;					\
	__v.iov_len = min(n, __p->iov_len - skip);	\
	if (likely(__v.iov_len)) {			\
		__v.iov_base = __p->iov_base + skip;	\
		left = (STEP);				\
		__v.iov_len -= left;			\
		skip += __v.iov_len;			\
		n -= __v.iov_len;			\
	} else {					\
		left = 0;				\
	}						\
	while (unlikely(!left && n)) {			\
		__p++;					\
		__v.iov_len = min(n, __p->iov_len);	\
		if (unlikely(!__v.iov_len))		\
			continue;			\
		__v.iov_base = __p->iov_base;		\
		left = (STEP);				\
		__v.iov_len -= left;			\
		skip = __v.iov_len;			\
		n -= __v.iov_len;			\
	}						\
	n = wanted - n;					\
}

#define iterate_kvec(i, n, __v, __p, skip, STEP) {	\
	size_t wanted = n;				\
	__p = i->kvec;					\
	__v.iov_len = min(n, __p->iov_len - skip);	\
	if (likely(__v.iov_len)) {			\
		__v.iov_base = __p->iov_base + skip;	\
		(void)(STEP);				\
		skip += __v.iov_len;			\
		n -= __v.iov_len;			\
	}						\
	while (unlikely(n)) {				\
		__p++;					\
		__v.iov_len = min(n, __p->iov_len);	\
		if (unlikely(!__v.iov_len))		\
			continue;			\
		__v.iov_base = __p->iov_base;		\
		(void)(STEP);				\
		skip = __v.iov_len;			\
		n -= __v.iov_len;			\
	}						\
	n = wanted;					\
}

#define iterate_bvec(i, n, __v, __bi, skip, STEP) {	\
	struct bvec_iter __start;			\
	__start.bi_size = n;				\
	__start.bi_bvec_done = skip;			\
	__start.bi_idx = 0;				\
	for_each_bvec(__v, i->bvec, __bi, __start) {	\
		if (!__v.bv_len)			\
			continue;			\
		(void)(STEP);				\
	}						\
}

#define iterate_all_kinds(i, n, v, I, B, K) {			\
	if (likely(n)) {					\
		size_t skip = i->iov_offset;			\
		if (unlikely(i->type & ITER_BVEC)) {		\
			struct bio_vec v;			\
			struct bvec_iter __bi;			\
			iterate_bvec(i, n, v, __bi, skip, (B))	\
		} else if (unlikely(i->type & ITER_KVEC)) {	\
			const struct kvec *kvec;		\
			struct kvec v;				\
			iterate_kvec(i, n, v, kvec, skip, (K))	\
		} else if (unlikely(i->type & ITER_DISCARD)) {	\
		} else {					\
			const struct iovec *iov;		\
			struct iovec v;				\
			iterate_iovec(i, n, v, iov, skip, (I))	\
		}						\
	}							\
}

#define iterate_and_advance(i, n, v, I, B, K) {			\
	if (unlikely(i->count < n))				\
		n = i->count;					\
	if (i->count) {						\
		size_t skip = i->iov_offset;			\
		if (unlikely(i->type & ITER_BVEC)) {		\
			const struct bio_vec *bvec = i->bvec;	\
			struct bio_vec v;			\
			struct bvec_iter __bi;			\
			iterate_bvec(i, n, v, __bi, skip, (B))	\
			i->bvec = __bvec_iter_bvec(i->bvec, __bi);	\
			i->nr_segs -= i->bvec - bvec;		\
			skip = __bi.bi_bvec_done;		\
		} else if (unlikely(i->type & ITER_KVEC)) {	\
			const struct kvec *kvec;		\
			struct kvec v;				\
			iterate_kvec(i, n, v, kvec, skip, (K))	\
			if (skip == kvec->iov_len) {		\
				kvec++;				\
				skip = 0;			\
			}					\
			i->nr_segs -= kvec - i->kvec;		\
			i->kvec = kvec;				\
		} else if (unlikely(i->type & ITER_DISCARD)) {	\
			skip += n;				\
		} else {					\
			const struct iovec *iov;		\
			struct iovec v;				\
			iterate_iovec(i, n, v, iov, skip, (I))	\
			if (skip == iov->iov_len) {		\
				iov++;				\
				skip = 0;			\
			}					\
			i->nr_segs -= iov - i->iov;		\
			i->iov = iov;				\
		}						\
		i->count -= n;					\
		i->iov_offset = skip;				\
	}							\
}

static int copyout(void __user *to, const void *from, size_t n)
{
	if (access_ok(to, n)) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}

static int copyin(void *to, const void __user *from, size_t n)
{
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

	if (IS_ENABLED(CONFIG_HIGHMEM) && !fault_in_pages_writeable(buf, copy)) {
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

	if (IS_ENABLED(CONFIG_HIGHMEM) && !fault_in_pages_readable(buf, copy)) {
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
 * Fault in one or more iovecs of the given iov_iter, to a maximum length of
 * bytes.  For each iovec, fault in each page that constitutes the iovec.
 *
 * Return 0 on success, or non-zero if the memory could not be accessed (i.e.
 * because it is an invalid address).
 */
int iov_iter_fault_in_readable(struct iov_iter *i, size_t bytes)
{
	size_t skip = i->iov_offset;
	const struct iovec *iov;
	int err;
	struct iovec v;

	if (!(i->type & (ITER_BVEC|ITER_KVEC))) {
		iterate_iovec(i, bytes, v, iov, skip, ({
			err = fault_in_pages_readable(v.iov_base, v.iov_len);
			if (unlikely(err))
			return err;
		0;}))
	}
	return 0;
}
EXPORT_SYMBOL(iov_iter_fault_in_readable);

void iov_iter_init(struct iov_iter *i, unsigned int direction,
			const struct iovec *iov, unsigned long nr_segs,
			size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	direction &= READ | WRITE;

	/* It will get better.  Eventually... */
	if (uaccess_kernel()) {
		i->type = ITER_KVEC | direction;
		i->kvec = (struct kvec *)iov;
	} else {
		i->type = ITER_IOVEC | direction;
		i->iov = iov;
	}
	i->nr_segs = nr_segs;
	i->iov_offset = 0;
	i->count = count;
}
EXPORT_SYMBOL(iov_iter_init);

static void memcpy_from_page(char *to, struct page *page, size_t offset, size_t len)
{
	char *from = kmap_atomic(page);
	memcpy(to, from + offset, len);
	kunmap_atomic(from);
}

static void memcpy_to_page(struct page *page, size_t offset, const char *from, size_t len)
{
	char *to = kmap_atomic(page);
	memcpy(to + offset, from, len);
	kunmap_atomic(to);
}

static void memzero_page(struct page *page, size_t offset, size_t len)
{
	char *addr = kmap_atomic(page);
	memset(addr + offset, 0, len);
	kunmap_atomic(addr);
}

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
	__wsum next = csum_partial_copy_nocheck(from, to, len, 0);
	return csum_block_add(sum, next, off);
}

static size_t csum_and_copy_to_pipe_iter(const void *addr, size_t bytes,
				__wsum *csum, struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int i_head;
	size_t n, r;
	size_t off = 0;
	__wsum sum = *csum;

	if (!sanity(i))
		return 0;

	bytes = n = push_pipe(i, bytes, &i_head, &r);
	if (unlikely(!n))
		return 0;
	do {
		size_t chunk = min_t(size_t, n, PAGE_SIZE - r);
		char *p = kmap_atomic(pipe->bufs[i_head & p_mask].page);
		sum = csum_and_memcpy(p + r, addr, chunk, sum, off);
		kunmap_atomic(p);
		i->head = i_head;
		i->iov_offset = r + chunk;
		n -= chunk;
		off += chunk;
		addr += chunk;
		r = 0;
		i_head++;
	} while (n);
	i->count -= bytes;
	*csum = sum;
	return bytes;
}

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	const char *from = addr;
	if (unlikely(iov_iter_is_pipe(i)))
		return copy_pipe_to_iter(addr, bytes, i);
	if (iter_is_iovec(i))
		might_fault();
	iterate_and_advance(i, bytes, v,
		copyout(v.iov_base, (from += v.iov_len) - v.iov_len, v.iov_len),
		memcpy_to_page(v.bv_page, v.bv_offset,
			       (from += v.bv_len) - v.bv_len, v.bv_len),
		memcpy(v.iov_base, (from += v.iov_len) - v.iov_len, v.iov_len)
	)

	return bytes;
}
EXPORT_SYMBOL(_copy_to_iter);

#ifdef CONFIG_ARCH_HAS_UACCESS_MCSAFE
static int copyout_mcsafe(void __user *to, const void *from, size_t n)
{
	if (access_ok(to, n)) {
		instrument_copy_to_user(to, from, n);
		n = copy_to_user_mcsafe((__force void *) to, from, n);
	}
	return n;
}

static unsigned long memcpy_mcsafe_to_page(struct page *page, size_t offset,
		const char *from, size_t len)
{
	unsigned long ret;
	char *to;

	to = kmap_atomic(page);
	ret = memcpy_mcsafe(to + offset, from, len);
	kunmap_atomic(to);

	return ret;
}

static size_t copy_pipe_to_iter_mcsafe(const void *addr, size_t bytes,
				struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int i_head;
	size_t n, off, xfer = 0;

	if (!sanity(i))
		return 0;

	bytes = n = push_pipe(i, bytes, &i_head, &off);
	if (unlikely(!n))
		return 0;
	do {
		size_t chunk = min_t(size_t, n, PAGE_SIZE - off);
		unsigned long rem;

		rem = memcpy_mcsafe_to_page(pipe->bufs[i_head & p_mask].page,
					    off, addr, chunk);
		i->head = i_head;
		i->iov_offset = off + chunk - rem;
		xfer += chunk - rem;
		if (rem)
			break;
		n -= chunk;
		addr += chunk;
		off = 0;
		i_head++;
	} while (n);
	i->count -= xfer;
	return xfer;
}

/**
 * _copy_to_iter_mcsafe - copy to user with source-read error exception handling
 * @addr: source kernel address
 * @bytes: total transfer length
 * @iter: destination iterator
 *
 * The pmem driver arranges for filesystem-dax to use this facility via
 * dax_copy_to_iter() for protecting read/write to persistent memory.
 * Unless / until an architecture can guarantee identical performance
 * between _copy_to_iter_mcsafe() and _copy_to_iter() it would be a
 * performance regression to switch more users to the mcsafe version.
 *
 * Otherwise, the main differences between this and typical _copy_to_iter().
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
 * See MCSAFE_TEST for self-test.
 */
size_t _copy_to_iter_mcsafe(const void *addr, size_t bytes, struct iov_iter *i)
{
	const char *from = addr;
	unsigned long rem, curr_addr, s_addr = (unsigned long) addr;

	if (unlikely(iov_iter_is_pipe(i)))
		return copy_pipe_to_iter_mcsafe(addr, bytes, i);
	if (iter_is_iovec(i))
		might_fault();
	iterate_and_advance(i, bytes, v,
		copyout_mcsafe(v.iov_base, (from += v.iov_len) - v.iov_len, v.iov_len),
		({
		rem = memcpy_mcsafe_to_page(v.bv_page, v.bv_offset,
                               (from += v.bv_len) - v.bv_len, v.bv_len);
		if (rem) {
			curr_addr = (unsigned long) from;
			bytes = curr_addr - s_addr - rem;
			return bytes;
		}
		}),
		({
		rem = memcpy_mcsafe(v.iov_base, (from += v.iov_len) - v.iov_len,
				v.iov_len);
		if (rem) {
			curr_addr = (unsigned long) from;
			bytes = curr_addr - s_addr - rem;
			return bytes;
		}
		})
	)

	return bytes;
}
EXPORT_SYMBOL_GPL(_copy_to_iter_mcsafe);
#endif /* CONFIG_ARCH_HAS_UACCESS_MCSAFE */

size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	char *to = addr;
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return 0;
	}
	if (iter_is_iovec(i))
		might_fault();
	iterate_and_advance(i, bytes, v,
		copyin((to += v.iov_len) - v.iov_len, v.iov_base, v.iov_len),
		memcpy_from_page((to += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len),
		memcpy((to += v.iov_len) - v.iov_len, v.iov_base, v.iov_len)
	)

	return bytes;
}
EXPORT_SYMBOL(_copy_from_iter);

bool _copy_from_iter_full(void *addr, size_t bytes, struct iov_iter *i)
{
	char *to = addr;
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return false;
	}
	if (unlikely(i->count < bytes))
		return false;

	if (iter_is_iovec(i))
		might_fault();
	iterate_all_kinds(i, bytes, v, ({
		if (copyin((to += v.iov_len) - v.iov_len,
				      v.iov_base, v.iov_len))
			return false;
		0;}),
		memcpy_from_page((to += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len),
		memcpy((to += v.iov_len) - v.iov_len, v.iov_base, v.iov_len)
	)

	iov_iter_advance(i, bytes);
	return true;
}
EXPORT_SYMBOL(_copy_from_iter_full);

size_t _copy_from_iter_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	char *to = addr;
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, v,
		__copy_from_user_inatomic_nocache((to += v.iov_len) - v.iov_len,
					 v.iov_base, v.iov_len),
		memcpy_from_page((to += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len),
		memcpy((to += v.iov_len) - v.iov_len, v.iov_base, v.iov_len)
	)

	return bytes;
}
EXPORT_SYMBOL(_copy_from_iter_nocache);

#ifdef CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE
/**
 * _copy_from_iter_flushcache - write destination through cpu cache
 * @addr: destination kernel address
 * @bytes: total transfer length
 * @iter: source iterator
 *
 * The pmem driver arranges for filesystem-dax to use this facility via
 * dax_copy_from_iter() for ensuring that writes to persistent memory
 * are flushed through the CPU cache. It is differentiated from
 * _copy_from_iter_nocache() in that guarantees all data is flushed for
 * all iterator types. The _copy_from_iter_nocache() only attempts to
 * bypass the cache for the ITER_IOVEC case, and on some archs may use
 * instructions that strand dirty-data in the cache.
 */
size_t _copy_from_iter_flushcache(void *addr, size_t bytes, struct iov_iter *i)
{
	char *to = addr;
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, v,
		__copy_from_user_flushcache((to += v.iov_len) - v.iov_len,
					 v.iov_base, v.iov_len),
		memcpy_page_flushcache((to += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len),
		memcpy_flushcache((to += v.iov_len) - v.iov_len, v.iov_base,
			v.iov_len)
	)

	return bytes;
}
EXPORT_SYMBOL_GPL(_copy_from_iter_flushcache);
#endif

bool _copy_from_iter_full_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	char *to = addr;
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return false;
	}
	if (unlikely(i->count < bytes))
		return false;
	iterate_all_kinds(i, bytes, v, ({
		if (__copy_from_user_inatomic_nocache((to += v.iov_len) - v.iov_len,
					     v.iov_base, v.iov_len))
			return false;
		0;}),
		memcpy_from_page((to += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len),
		memcpy((to += v.iov_len) - v.iov_len, v.iov_base, v.iov_len)
	)

	iov_iter_advance(i, bytes);
	return true;
}
EXPORT_SYMBOL(_copy_from_iter_full_nocache);

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

size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (unlikely(!page_copy_sane(page, offset, bytes)))
		return 0;
	if (i->type & (ITER_BVEC|ITER_KVEC)) {
		void *kaddr = kmap_atomic(page);
		size_t wanted = copy_to_iter(kaddr + offset, bytes, i);
		kunmap_atomic(kaddr);
		return wanted;
	} else if (unlikely(iov_iter_is_discard(i)))
		return bytes;
	else if (likely(!iov_iter_is_pipe(i)))
		return copy_page_to_iter_iovec(page, offset, bytes, i);
	else
		return copy_page_to_iter_pipe(page, offset, bytes, i);
}
EXPORT_SYMBOL(copy_page_to_iter);

size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (unlikely(!page_copy_sane(page, offset, bytes)))
		return 0;
	if (unlikely(iov_iter_is_pipe(i) || iov_iter_is_discard(i))) {
		WARN_ON(1);
		return 0;
	}
	if (i->type & (ITER_BVEC|ITER_KVEC)) {
		void *kaddr = kmap_atomic(page);
		size_t wanted = _copy_from_iter(kaddr + offset, bytes, i);
		kunmap_atomic(kaddr);
		return wanted;
	} else
		return copy_page_from_iter_iovec(page, offset, bytes, i);
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
		memzero_page(pipe->bufs[i_head & p_mask].page, off, chunk);
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
	iterate_and_advance(i, bytes, v,
		clear_user(v.iov_base, v.iov_len),
		memzero_page(v.bv_page, v.bv_offset, v.bv_len),
		memset(v.iov_base, 0, v.iov_len)
	)

	return bytes;
}
EXPORT_SYMBOL(iov_iter_zero);

size_t iov_iter_copy_from_user_atomic(struct page *page,
		struct iov_iter *i, unsigned long offset, size_t bytes)
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
	iterate_all_kinds(i, bytes, v,
		copyin((p += v.iov_len) - v.iov_len, v.iov_base, v.iov_len),
		memcpy_from_page((p += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len),
		memcpy((p += v.iov_len) - v.iov_len, v.iov_base, v.iov_len)
	)
	kunmap_atomic(kaddr);
	return bytes;
}
EXPORT_SYMBOL(iov_iter_copy_from_user_atomic);

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
	if (unlikely(i->count < size))
		size = i->count;
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

void iov_iter_advance(struct iov_iter *i, size_t size)
{
	if (unlikely(iov_iter_is_pipe(i))) {
		pipe_advance(i, size);
		return;
	}
	if (unlikely(iov_iter_is_discard(i))) {
		i->count -= size;
		return;
	}
	iterate_and_advance(i, size, v, 0, 0, 0)
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
	if (iov_iter_is_bvec(i)) {
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
	if (unlikely(iov_iter_is_pipe(i)))
		return i->count;	// it is a silly place, anyway
	if (i->nr_segs == 1)
		return i->count;
	if (unlikely(iov_iter_is_discard(i)))
		return i->count;
	else if (iov_iter_is_bvec(i))
		return min(i->count, i->bvec->bv_len - i->iov_offset);
	else
		return min(i->count, i->iov->iov_len - i->iov_offset);
}
EXPORT_SYMBOL(iov_iter_single_seg_count);

void iov_iter_kvec(struct iov_iter *i, unsigned int direction,
			const struct kvec *kvec, unsigned long nr_segs,
			size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	i->type = ITER_KVEC | (direction & (READ | WRITE));
	i->kvec = kvec;
	i->nr_segs = nr_segs;
	i->iov_offset = 0;
	i->count = count;
}
EXPORT_SYMBOL(iov_iter_kvec);

void iov_iter_bvec(struct iov_iter *i, unsigned int direction,
			const struct bio_vec *bvec, unsigned long nr_segs,
			size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	i->type = ITER_BVEC | (direction & (READ | WRITE));
	i->bvec = bvec;
	i->nr_segs = nr_segs;
	i->iov_offset = 0;
	i->count = count;
}
EXPORT_SYMBOL(iov_iter_bvec);

void iov_iter_pipe(struct iov_iter *i, unsigned int direction,
			struct pipe_inode_info *pipe,
			size_t count)
{
	BUG_ON(direction != READ);
	WARN_ON(pipe_full(pipe->head, pipe->tail, pipe->ring_size));
	i->type = ITER_PIPE | READ;
	i->pipe = pipe;
	i->head = pipe->head;
	i->iov_offset = 0;
	i->count = count;
	i->start_head = i->head;
}
EXPORT_SYMBOL(iov_iter_pipe);

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
	i->type = ITER_DISCARD | READ;
	i->count = count;
	i->iov_offset = 0;
}
EXPORT_SYMBOL(iov_iter_discard);

unsigned long iov_iter_alignment(const struct iov_iter *i)
{
	unsigned long res = 0;
	size_t size = i->count;

	if (unlikely(iov_iter_is_pipe(i))) {
		unsigned int p_mask = i->pipe->ring_size - 1;

		if (size && i->iov_offset && allocated(&i->pipe->bufs[i->head & p_mask]))
			return size | i->iov_offset;
		return size;
	}
	iterate_all_kinds(i, size, v,
		(res |= (unsigned long)v.iov_base | v.iov_len, 0),
		res |= v.bv_offset | v.bv_len,
		res |= (unsigned long)v.iov_base | v.iov_len
	)
	return res;
}
EXPORT_SYMBOL(iov_iter_alignment);

unsigned long iov_iter_gap_alignment(const struct iov_iter *i)
{
	unsigned long res = 0;
	size_t size = i->count;

	if (unlikely(iov_iter_is_pipe(i) || iov_iter_is_discard(i))) {
		WARN_ON(1);
		return ~0U;
	}

	iterate_all_kinds(i, size, v,
		(res |= (!res ? 0 : (unsigned long)v.iov_base) |
			(size != v.iov_len ? size : 0), 0),
		(res |= (!res ? 0 : (unsigned long)v.bv_offset) |
			(size != v.bv_len ? size : 0)),
		(res |= (!res ? 0 : (unsigned long)v.iov_base) |
			(size != v.iov_len ? size : 0))
		);
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

	if (!maxsize)
		return 0;

	if (!sanity(i))
		return -EFAULT;

	data_start(i, &iter_head, start);
	/* Amount of free space: some of this one + all after this one */
	npages = pipe_space_for_user(iter_head, i->pipe->tail, i->pipe);
	capacity = min(npages, maxpages) * PAGE_SIZE - *start;

	return __pipe_get_pages(i, min(maxsize, capacity), pages, iter_head, start);
}

ssize_t iov_iter_get_pages(struct iov_iter *i,
		   struct page **pages, size_t maxsize, unsigned maxpages,
		   size_t *start)
{
	if (maxsize > i->count)
		maxsize = i->count;

	if (unlikely(iov_iter_is_pipe(i)))
		return pipe_get_pages(i, pages, maxsize, maxpages, start);
	if (unlikely(iov_iter_is_discard(i)))
		return -EFAULT;

	iterate_all_kinds(i, maxsize, v, ({
		unsigned long addr = (unsigned long)v.iov_base;
		size_t len = v.iov_len + (*start = addr & (PAGE_SIZE - 1));
		int n;
		int res;

		if (len > maxpages * PAGE_SIZE)
			len = maxpages * PAGE_SIZE;
		addr &= ~(PAGE_SIZE - 1);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		res = get_user_pages_fast(addr, n,
				iov_iter_rw(i) != WRITE ?  FOLL_WRITE : 0,
				pages);
		if (unlikely(res < 0))
			return res;
		return (res == n ? len : res * PAGE_SIZE) - *start;
	0;}),({
		/* can't be more than PAGE_SIZE */
		*start = v.bv_offset;
		get_page(*pages = v.bv_page);
		return v.bv_len;
	}),({
		return -EFAULT;
	})
	)
	return 0;
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

	if (!maxsize)
		return 0;

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

ssize_t iov_iter_get_pages_alloc(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	struct page **p;

	if (maxsize > i->count)
		maxsize = i->count;

	if (unlikely(iov_iter_is_pipe(i)))
		return pipe_get_pages_alloc(i, pages, maxsize, start);
	if (unlikely(iov_iter_is_discard(i)))
		return -EFAULT;

	iterate_all_kinds(i, maxsize, v, ({
		unsigned long addr = (unsigned long)v.iov_base;
		size_t len = v.iov_len + (*start = addr & (PAGE_SIZE - 1));
		int n;
		int res;

		addr &= ~(PAGE_SIZE - 1);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		p = get_pages_array(n);
		if (!p)
			return -ENOMEM;
		res = get_user_pages_fast(addr, n,
				iov_iter_rw(i) != WRITE ?  FOLL_WRITE : 0, p);
		if (unlikely(res < 0)) {
			kvfree(p);
			return res;
		}
		*pages = p;
		return (res == n ? len : res * PAGE_SIZE) - *start;
	0;}),({
		/* can't be more than PAGE_SIZE */
		*start = v.bv_offset;
		*pages = p = get_pages_array(1);
		if (!p)
			return -ENOMEM;
		get_page(*p = v.bv_page);
		return v.bv_len;
	}),({
		return -EFAULT;
	})
	)
	return 0;
}
EXPORT_SYMBOL(iov_iter_get_pages_alloc);

size_t csum_and_copy_from_iter(void *addr, size_t bytes, __wsum *csum,
			       struct iov_iter *i)
{
	char *to = addr;
	__wsum sum, next;
	size_t off = 0;
	sum = *csum;
	if (unlikely(iov_iter_is_pipe(i) || iov_iter_is_discard(i))) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, v, ({
		int err = 0;
		next = csum_and_copy_from_user(v.iov_base,
					       (to += v.iov_len) - v.iov_len,
					       v.iov_len, 0, &err);
		if (!err) {
			sum = csum_block_add(sum, next, off);
			off += v.iov_len;
		}
		err ? v.iov_len : 0;
	}), ({
		char *p = kmap_atomic(v.bv_page);
		sum = csum_and_memcpy((to += v.bv_len) - v.bv_len,
				      p + v.bv_offset, v.bv_len,
				      sum, off);
		kunmap_atomic(p);
		off += v.bv_len;
	}),({
		sum = csum_and_memcpy((to += v.iov_len) - v.iov_len,
				      v.iov_base, v.iov_len,
				      sum, off);
		off += v.iov_len;
	})
	)
	*csum = sum;
	return bytes;
}
EXPORT_SYMBOL(csum_and_copy_from_iter);

bool csum_and_copy_from_iter_full(void *addr, size_t bytes, __wsum *csum,
			       struct iov_iter *i)
{
	char *to = addr;
	__wsum sum, next;
	size_t off = 0;
	sum = *csum;
	if (unlikely(iov_iter_is_pipe(i) || iov_iter_is_discard(i))) {
		WARN_ON(1);
		return false;
	}
	if (unlikely(i->count < bytes))
		return false;
	iterate_all_kinds(i, bytes, v, ({
		int err = 0;
		next = csum_and_copy_from_user(v.iov_base,
					       (to += v.iov_len) - v.iov_len,
					       v.iov_len, 0, &err);
		if (err)
			return false;
		sum = csum_block_add(sum, next, off);
		off += v.iov_len;
		0;
	}), ({
		char *p = kmap_atomic(v.bv_page);
		sum = csum_and_memcpy((to += v.bv_len) - v.bv_len,
				      p + v.bv_offset, v.bv_len,
				      sum, off);
		kunmap_atomic(p);
		off += v.bv_len;
	}),({
		sum = csum_and_memcpy((to += v.iov_len) - v.iov_len,
				      v.iov_base, v.iov_len,
				      sum, off);
		off += v.iov_len;
	})
	)
	*csum = sum;
	iov_iter_advance(i, bytes);
	return true;
}
EXPORT_SYMBOL(csum_and_copy_from_iter_full);

size_t csum_and_copy_to_iter(const void *addr, size_t bytes, void *csump,
			     struct iov_iter *i)
{
	const char *from = addr;
	__wsum *csum = csump;
	__wsum sum, next;
	size_t off = 0;

	if (unlikely(iov_iter_is_pipe(i)))
		return csum_and_copy_to_pipe_iter(addr, bytes, csum, i);

	sum = *csum;
	if (unlikely(iov_iter_is_discard(i))) {
		WARN_ON(1);	/* for now */
		return 0;
	}
	iterate_and_advance(i, bytes, v, ({
		int err = 0;
		next = csum_and_copy_to_user((from += v.iov_len) - v.iov_len,
					     v.iov_base,
					     v.iov_len, 0, &err);
		if (!err) {
			sum = csum_block_add(sum, next, off);
			off += v.iov_len;
		}
		err ? v.iov_len : 0;
	}), ({
		char *p = kmap_atomic(v.bv_page);
		sum = csum_and_memcpy(p + v.bv_offset,
				      (from += v.bv_len) - v.bv_len,
				      v.bv_len, sum, off);
		kunmap_atomic(p);
		off += v.bv_len;
	}),({
		sum = csum_and_memcpy(v.iov_base,
				     (from += v.iov_len) - v.iov_len,
				     v.iov_len, sum, off);
		off += v.iov_len;
	})
	)
	*csum = sum;
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

int iov_iter_npages(const struct iov_iter *i, int maxpages)
{
	size_t size = i->count;
	int npages = 0;

	if (!size)
		return 0;
	if (unlikely(iov_iter_is_discard(i)))
		return 0;

	if (unlikely(iov_iter_is_pipe(i))) {
		struct pipe_inode_info *pipe = i->pipe;
		unsigned int iter_head;
		size_t off;

		if (!sanity(i))
			return 0;

		data_start(i, &iter_head, &off);
		/* some of this one + all after this one */
		npages = pipe_space_for_user(iter_head, pipe->tail, pipe);
		if (npages >= maxpages)
			return maxpages;
	} else iterate_all_kinds(i, size, v, ({
		unsigned long p = (unsigned long)v.iov_base;
		npages += DIV_ROUND_UP(p + v.iov_len, PAGE_SIZE)
			- p / PAGE_SIZE;
		if (npages >= maxpages)
			return maxpages;
	0;}),({
		npages++;
		if (npages >= maxpages)
			return maxpages;
	}),({
		unsigned long p = (unsigned long)v.iov_base;
		npages += DIV_ROUND_UP(p + v.iov_len, PAGE_SIZE)
			- p / PAGE_SIZE;
		if (npages >= maxpages)
			return maxpages;
	})
	)
	return npages;
}
EXPORT_SYMBOL(iov_iter_npages);

const void *dup_iter(struct iov_iter *new, struct iov_iter *old, gfp_t flags)
{
	*new = *old;
	if (unlikely(iov_iter_is_pipe(new))) {
		WARN_ON(1);
		return NULL;
	}
	if (unlikely(iov_iter_is_discard(new)))
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

	if (!user_access_begin(uvec, nr_segs * sizeof(*uvec)))
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

int iov_iter_for_each_range(struct iov_iter *i, size_t bytes,
			    int (*f)(struct kvec *vec, void *context),
			    void *context)
{
	struct kvec w;
	int err = -EINVAL;
	if (!bytes)
		return 0;

	iterate_all_kinds(i, bytes, v, -EINVAL, ({
		w.iov_base = kmap(v.bv_page) + v.bv_offset;
		w.iov_len = v.bv_len;
		err = f(&w, context);
		kunmap(v.bv_page);
		err;}), ({
		w = v;
		err = f(&w, context);})
	)
	return err;
}
EXPORT_SYMBOL(iov_iter_for_each_range);
