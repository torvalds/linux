// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/scatterlist.h>
#include <linux/instrumented.h>
#include <linux/iov_iter.h>

static __always_inline
size_t copy_to_user_iter(void __user *iter_to, size_t progress,
			 size_t len, void *from, void *priv2)
{
	if (should_fail_usercopy())
		return len;
	if (access_ok(iter_to, len)) {
		from += progress;
		instrument_copy_to_user(iter_to, from, len);
		len = raw_copy_to_user(iter_to, from, len);
	}
	return len;
}

static __always_inline
size_t copy_to_user_iter_nofault(void __user *iter_to, size_t progress,
				 size_t len, void *from, void *priv2)
{
	ssize_t res;

	if (should_fail_usercopy())
		return len;

	from += progress;
	res = copy_to_user_nofault(iter_to, from, len);
	return res < 0 ? len : res;
}

static __always_inline
size_t copy_from_user_iter(void __user *iter_from, size_t progress,
			   size_t len, void *to, void *priv2)
{
	size_t res = len;

	if (should_fail_usercopy())
		return len;
	if (access_ok(iter_from, len)) {
		to += progress;
		instrument_copy_from_user_before(to, iter_from, len);
		res = raw_copy_from_user(to, iter_from, len);
		instrument_copy_from_user_after(to, iter_from, len, res);
	}
	return res;
}

static __always_inline
size_t memcpy_to_iter(void *iter_to, size_t progress,
		      size_t len, void *from, void *priv2)
{
	memcpy(iter_to, from + progress, len);
	return 0;
}

static __always_inline
size_t memcpy_from_iter(void *iter_from, size_t progress,
			size_t len, void *to, void *priv2)
{
	memcpy(to + progress, iter_from, len);
	return 0;
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
	if (iter_is_ubuf(i)) {
		size_t n = min(size, iov_iter_count(i));
		n -= fault_in_readable(i->ubuf + i->iov_offset, n);
		return size - n;
	} else if (iter_is_iovec(i)) {
		size_t count = min(size, iov_iter_count(i));
		const struct iovec *p;
		size_t skip;

		size -= count;
		for (p = iter_iov(i), skip = i->iov_offset; count; p++, skip = 0) {
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
	if (iter_is_ubuf(i)) {
		size_t n = min(size, iov_iter_count(i));
		n -= fault_in_safe_writeable(i->ubuf + i->iov_offset, n);
		return size - n;
	} else if (iter_is_iovec(i)) {
		size_t count = min(size, iov_iter_count(i));
		const struct iovec *p;
		size_t skip;

		size -= count;
		for (p = iter_iov(i), skip = i->iov_offset; count; p++, skip = 0) {
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
		.copy_mc = false,
		.nofault = false,
		.data_source = direction,
		.__iov = iov,
		.nr_segs = nr_segs,
		.iov_offset = 0,
		.count = count
	};
}
EXPORT_SYMBOL(iov_iter_init);

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	if (WARN_ON_ONCE(i->data_source))
		return 0;
	if (user_backed_iter(i))
		might_fault();
	return iterate_and_advance(i, bytes, (void *)addr,
				   copy_to_user_iter, memcpy_to_iter);
}
EXPORT_SYMBOL(_copy_to_iter);

#ifdef CONFIG_ARCH_HAS_COPY_MC
static __always_inline
size_t copy_to_user_iter_mc(void __user *iter_to, size_t progress,
			    size_t len, void *from, void *priv2)
{
	if (access_ok(iter_to, len)) {
		from += progress;
		instrument_copy_to_user(iter_to, from, len);
		len = copy_mc_to_user(iter_to, from, len);
	}
	return len;
}

static __always_inline
size_t memcpy_to_iter_mc(void *iter_to, size_t progress,
			 size_t len, void *from, void *priv2)
{
	return copy_mc_to_kernel(iter_to, from + progress, len);
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
 * * ITER_KVEC and ITER_BVEC can return short copies.  Compare to
 *   copy_to_iter() where only ITER_IOVEC attempts might return a short copy.
 *
 * Return: number of bytes copied (may be %0)
 */
size_t _copy_mc_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	if (WARN_ON_ONCE(i->data_source))
		return 0;
	if (user_backed_iter(i))
		might_fault();
	return iterate_and_advance(i, bytes, (void *)addr,
				   copy_to_user_iter_mc, memcpy_to_iter_mc);
}
EXPORT_SYMBOL_GPL(_copy_mc_to_iter);
#endif /* CONFIG_ARCH_HAS_COPY_MC */

static __always_inline
size_t memcpy_from_iter_mc(void *iter_from, size_t progress,
			   size_t len, void *to, void *priv2)
{
	return copy_mc_to_kernel(to + progress, iter_from, len);
}

static size_t __copy_from_iter_mc(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(i->count < bytes))
		bytes = i->count;
	if (unlikely(!bytes))
		return 0;
	return iterate_bvec(i, bytes, addr, NULL, memcpy_from_iter_mc);
}

static __always_inline
size_t __copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_copy_mc(i)))
		return __copy_from_iter_mc(addr, bytes, i);
	return iterate_and_advance(i, bytes, addr,
				   copy_from_user_iter, memcpy_from_iter);
}

size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (WARN_ON_ONCE(!i->data_source))
		return 0;

	if (user_backed_iter(i))
		might_fault();
	return __copy_from_iter(addr, bytes, i);
}
EXPORT_SYMBOL(_copy_from_iter);

static __always_inline
size_t copy_from_user_iter_nocache(void __user *iter_from, size_t progress,
				   size_t len, void *to, void *priv2)
{
	return __copy_from_user_inatomic_nocache(to + progress, iter_from, len);
}

size_t _copy_from_iter_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	if (WARN_ON_ONCE(!i->data_source))
		return 0;

	return iterate_and_advance(i, bytes, addr,
				   copy_from_user_iter_nocache,
				   memcpy_from_iter);
}
EXPORT_SYMBOL(_copy_from_iter_nocache);

#ifdef CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE
static __always_inline
size_t copy_from_user_iter_flushcache(void __user *iter_from, size_t progress,
				      size_t len, void *to, void *priv2)
{
	return __copy_from_user_flushcache(to + progress, iter_from, len);
}

static __always_inline
size_t memcpy_from_iter_flushcache(void *iter_from, size_t progress,
				   size_t len, void *to, void *priv2)
{
	memcpy_flushcache(to + progress, iter_from, len);
	return 0;
}

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
	if (WARN_ON_ONCE(!i->data_source))
		return 0;

	return iterate_and_advance(i, bytes, addr,
				   copy_from_user_iter_flushcache,
				   memcpy_from_iter_flushcache);
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

	if (WARN_ON(n > v || v > page_size(head)))
		return false;
	return true;
}

size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t res = 0;
	if (!page_copy_sane(page, offset, bytes))
		return 0;
	if (WARN_ON_ONCE(i->data_source))
		return 0;
	page += offset / PAGE_SIZE; // first subpage
	offset %= PAGE_SIZE;
	while (1) {
		void *kaddr = kmap_local_page(page);
		size_t n = min(bytes, (size_t)PAGE_SIZE - offset);
		n = _copy_to_iter(kaddr + offset, n, i);
		kunmap_local(kaddr);
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

size_t copy_page_to_iter_nofault(struct page *page, unsigned offset, size_t bytes,
				 struct iov_iter *i)
{
	size_t res = 0;

	if (!page_copy_sane(page, offset, bytes))
		return 0;
	if (WARN_ON_ONCE(i->data_source))
		return 0;
	page += offset / PAGE_SIZE; // first subpage
	offset %= PAGE_SIZE;
	while (1) {
		void *kaddr = kmap_local_page(page);
		size_t n = min(bytes, (size_t)PAGE_SIZE - offset);

		n = iterate_and_advance(i, n, kaddr + offset,
					copy_to_user_iter_nofault,
					memcpy_to_iter);
		kunmap_local(kaddr);
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
EXPORT_SYMBOL(copy_page_to_iter_nofault);

size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t res = 0;
	if (!page_copy_sane(page, offset, bytes))
		return 0;
	page += offset / PAGE_SIZE; // first subpage
	offset %= PAGE_SIZE;
	while (1) {
		void *kaddr = kmap_local_page(page);
		size_t n = min(bytes, (size_t)PAGE_SIZE - offset);
		n = _copy_from_iter(kaddr + offset, n, i);
		kunmap_local(kaddr);
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
EXPORT_SYMBOL(copy_page_from_iter);

static __always_inline
size_t zero_to_user_iter(void __user *iter_to, size_t progress,
			 size_t len, void *priv, void *priv2)
{
	return clear_user(iter_to, len);
}

static __always_inline
size_t zero_to_iter(void *iter_to, size_t progress,
		    size_t len, void *priv, void *priv2)
{
	memset(iter_to, 0, len);
	return 0;
}

size_t iov_iter_zero(size_t bytes, struct iov_iter *i)
{
	return iterate_and_advance(i, bytes, NULL,
				   zero_to_user_iter, zero_to_iter);
}
EXPORT_SYMBOL(iov_iter_zero);

size_t copy_page_from_iter_atomic(struct page *page, size_t offset,
		size_t bytes, struct iov_iter *i)
{
	size_t n, copied = 0;

	if (!page_copy_sane(page, offset, bytes))
		return 0;
	if (WARN_ON_ONCE(!i->data_source))
		return 0;

	do {
		char *p;

		n = bytes - copied;
		if (PageHighMem(page)) {
			page += offset / PAGE_SIZE;
			offset %= PAGE_SIZE;
			n = min_t(size_t, n, PAGE_SIZE - offset);
		}

		p = kmap_atomic(page) + offset;
		n = __copy_from_iter(p, n, i);
		kunmap_atomic(p);
		copied += n;
		offset += n;
	} while (PageHighMem(page) && copied != bytes && n > 0);

	return copied;
}
EXPORT_SYMBOL(copy_page_from_iter_atomic);

static void iov_iter_bvec_advance(struct iov_iter *i, size_t size)
{
	const struct bio_vec *bvec, *end;

	if (!i->count)
		return;
	i->count -= size;

	size += i->iov_offset;

	for (bvec = i->bvec, end = bvec + i->nr_segs; bvec < end; bvec++) {
		if (likely(size < bvec->bv_len))
			break;
		size -= bvec->bv_len;
	}
	i->iov_offset = size;
	i->nr_segs -= bvec - i->bvec;
	i->bvec = bvec;
}

static void iov_iter_iovec_advance(struct iov_iter *i, size_t size)
{
	const struct iovec *iov, *end;

	if (!i->count)
		return;
	i->count -= size;

	size += i->iov_offset; // from beginning of current segment
	for (iov = iter_iov(i), end = iov + i->nr_segs; iov < end; iov++) {
		if (likely(size < iov->iov_len))
			break;
		size -= iov->iov_len;
	}
	i->iov_offset = size;
	i->nr_segs -= iov - iter_iov(i);
	i->__iov = iov;
}

void iov_iter_advance(struct iov_iter *i, size_t size)
{
	if (unlikely(i->count < size))
		size = i->count;
	if (likely(iter_is_ubuf(i)) || unlikely(iov_iter_is_xarray(i))) {
		i->iov_offset += size;
		i->count -= size;
	} else if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i))) {
		/* iovec and kvec have identical layouts */
		iov_iter_iovec_advance(i, size);
	} else if (iov_iter_is_bvec(i)) {
		iov_iter_bvec_advance(i, size);
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
	if (unlikely(iov_iter_is_discard(i)))
		return;
	if (unroll <= i->iov_offset) {
		i->iov_offset -= unroll;
		return;
	}
	unroll -= i->iov_offset;
	if (iov_iter_is_xarray(i) || iter_is_ubuf(i)) {
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
		const struct iovec *iov = iter_iov(i);
		while (1) {
			size_t n = (--iov)->iov_len;
			i->nr_segs++;
			if (unroll <= n) {
				i->__iov = iov;
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
			return min(i->count, iter_iov(i)->iov_len - i->iov_offset);
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
		.copy_mc = false,
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
		.copy_mc = false,
		.data_source = direction,
		.bvec = bvec,
		.nr_segs = nr_segs,
		.iov_offset = 0,
		.count = count
	};
}
EXPORT_SYMBOL(iov_iter_bvec);

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
		.copy_mc = false,
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
		.copy_mc = false,
		.data_source = false,
		.count = count,
		.iov_offset = 0
	};
}
EXPORT_SYMBOL(iov_iter_discard);

static bool iov_iter_aligned_iovec(const struct iov_iter *i, unsigned addr_mask,
				   unsigned len_mask)
{
	size_t size = i->count;
	size_t skip = i->iov_offset;
	unsigned k;

	for (k = 0; k < i->nr_segs; k++, skip = 0) {
		const struct iovec *iov = iter_iov(i) + k;
		size_t len = iov->iov_len - skip;

		if (len > size)
			len = size;
		if (len & len_mask)
			return false;
		if ((unsigned long)(iov->iov_base + skip) & addr_mask)
			return false;

		size -= len;
		if (!size)
			break;
	}
	return true;
}

static bool iov_iter_aligned_bvec(const struct iov_iter *i, unsigned addr_mask,
				  unsigned len_mask)
{
	size_t size = i->count;
	unsigned skip = i->iov_offset;
	unsigned k;

	for (k = 0; k < i->nr_segs; k++, skip = 0) {
		size_t len = i->bvec[k].bv_len - skip;

		if (len > size)
			len = size;
		if (len & len_mask)
			return false;
		if ((unsigned long)(i->bvec[k].bv_offset + skip) & addr_mask)
			return false;

		size -= len;
		if (!size)
			break;
	}
	return true;
}

/**
 * iov_iter_is_aligned() - Check if the addresses and lengths of each segments
 * 	are aligned to the parameters.
 *
 * @i: &struct iov_iter to restore
 * @addr_mask: bit mask to check against the iov element's addresses
 * @len_mask: bit mask to check against the iov element's lengths
 *
 * Return: false if any addresses or lengths intersect with the provided masks
 */
bool iov_iter_is_aligned(const struct iov_iter *i, unsigned addr_mask,
			 unsigned len_mask)
{
	if (likely(iter_is_ubuf(i))) {
		if (i->count & len_mask)
			return false;
		if ((unsigned long)(i->ubuf + i->iov_offset) & addr_mask)
			return false;
		return true;
	}

	if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i)))
		return iov_iter_aligned_iovec(i, addr_mask, len_mask);

	if (iov_iter_is_bvec(i))
		return iov_iter_aligned_bvec(i, addr_mask, len_mask);

	if (iov_iter_is_xarray(i)) {
		if (i->count & len_mask)
			return false;
		if ((i->xarray_start + i->iov_offset) & addr_mask)
			return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(iov_iter_is_aligned);

static unsigned long iov_iter_alignment_iovec(const struct iov_iter *i)
{
	unsigned long res = 0;
	size_t size = i->count;
	size_t skip = i->iov_offset;
	unsigned k;

	for (k = 0; k < i->nr_segs; k++, skip = 0) {
		const struct iovec *iov = iter_iov(i) + k;
		size_t len = iov->iov_len - skip;
		if (len) {
			res |= (unsigned long)iov->iov_base + skip;
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
	if (likely(iter_is_ubuf(i))) {
		size_t size = i->count;
		if (size)
			return ((unsigned long)i->ubuf + i->iov_offset) | size;
		return 0;
	}

	/* iovec and kvec have identical layouts */
	if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i)))
		return iov_iter_alignment_iovec(i);

	if (iov_iter_is_bvec(i))
		return iov_iter_alignment_bvec(i);

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

	if (iter_is_ubuf(i))
		return 0;

	if (WARN_ON(!iter_is_iovec(i)))
		return ~0U;

	for (k = 0; k < i->nr_segs; k++) {
		const struct iovec *iov = iter_iov(i) + k;
		if (iov->iov_len) {
			unsigned long base = (unsigned long)iov->iov_base;
			if (v) // if not the first one
				res |= base | v; // this start | previous end
			v = base + iov->iov_len;
			if (size <= iov->iov_len)
				break;
			size -= iov->iov_len;
		}
	}
	return res;
}
EXPORT_SYMBOL(iov_iter_gap_alignment);

static int want_pages_array(struct page ***res, size_t size,
			    size_t start, unsigned int maxpages)
{
	unsigned int count = DIV_ROUND_UP(size + start, PAGE_SIZE);

	if (count > maxpages)
		count = maxpages;
	WARN_ON(!count);	// caller should've prevented that
	if (!*res) {
		*res = kvmalloc_array(count, sizeof(struct page *), GFP_KERNEL);
		if (!*res)
			return 0;
	}
	return count;
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
				     struct page ***pages, size_t maxsize,
				     unsigned maxpages, size_t *_start_offset)
{
	unsigned nr, offset, count;
	pgoff_t index;
	loff_t pos;

	pos = i->xarray_start + i->iov_offset;
	index = pos >> PAGE_SHIFT;
	offset = pos & ~PAGE_MASK;
	*_start_offset = offset;

	count = want_pages_array(pages, maxsize, offset, maxpages);
	if (!count)
		return -ENOMEM;
	nr = iter_xarray_populate_pages(*pages, i->xarray, index, count);
	if (nr == 0)
		return 0;

	maxsize = min_t(size_t, nr * PAGE_SIZE - offset, maxsize);
	i->iov_offset += maxsize;
	i->count -= maxsize;
	return maxsize;
}

/* must be done on non-empty ITER_UBUF or ITER_IOVEC one */
static unsigned long first_iovec_segment(const struct iov_iter *i, size_t *size)
{
	size_t skip;
	long k;

	if (iter_is_ubuf(i))
		return (unsigned long)i->ubuf + i->iov_offset;

	for (k = 0, skip = i->iov_offset; k < i->nr_segs; k++, skip = 0) {
		const struct iovec *iov = iter_iov(i) + k;
		size_t len = iov->iov_len - skip;

		if (unlikely(!len))
			continue;
		if (*size > len)
			*size = len;
		return (unsigned long)iov->iov_base + skip;
	}
	BUG(); // if it had been empty, we wouldn't get called
}

/* must be done on non-empty ITER_BVEC one */
static struct page *first_bvec_segment(const struct iov_iter *i,
				       size_t *size, size_t *start)
{
	struct page *page;
	size_t skip = i->iov_offset, len;

	len = i->bvec->bv_len - skip;
	if (*size > len)
		*size = len;
	skip += i->bvec->bv_offset;
	page = i->bvec->bv_page + skip / PAGE_SIZE;
	*start = skip % PAGE_SIZE;
	return page;
}

static ssize_t __iov_iter_get_pages_alloc(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   unsigned int maxpages, size_t *start)
{
	unsigned int n, gup_flags = 0;

	if (maxsize > i->count)
		maxsize = i->count;
	if (!maxsize)
		return 0;
	if (maxsize > MAX_RW_COUNT)
		maxsize = MAX_RW_COUNT;

	if (likely(user_backed_iter(i))) {
		unsigned long addr;
		int res;

		if (iov_iter_rw(i) != WRITE)
			gup_flags |= FOLL_WRITE;
		if (i->nofault)
			gup_flags |= FOLL_NOFAULT;

		addr = first_iovec_segment(i, &maxsize);
		*start = addr % PAGE_SIZE;
		addr &= PAGE_MASK;
		n = want_pages_array(pages, maxsize, *start, maxpages);
		if (!n)
			return -ENOMEM;
		res = get_user_pages_fast(addr, n, gup_flags, *pages);
		if (unlikely(res <= 0))
			return res;
		maxsize = min_t(size_t, maxsize, res * PAGE_SIZE - *start);
		iov_iter_advance(i, maxsize);
		return maxsize;
	}
	if (iov_iter_is_bvec(i)) {
		struct page **p;
		struct page *page;

		page = first_bvec_segment(i, &maxsize, start);
		n = want_pages_array(pages, maxsize, *start, maxpages);
		if (!n)
			return -ENOMEM;
		p = *pages;
		for (int k = 0; k < n; k++)
			get_page(p[k] = page + k);
		maxsize = min_t(size_t, maxsize, n * PAGE_SIZE - *start);
		i->count -= maxsize;
		i->iov_offset += maxsize;
		if (i->iov_offset == i->bvec->bv_len) {
			i->iov_offset = 0;
			i->bvec++;
			i->nr_segs--;
		}
		return maxsize;
	}
	if (iov_iter_is_xarray(i))
		return iter_xarray_get_pages(i, pages, maxsize, maxpages, start);
	return -EFAULT;
}

ssize_t iov_iter_get_pages2(struct iov_iter *i, struct page **pages,
		size_t maxsize, unsigned maxpages, size_t *start)
{
	if (!maxpages)
		return 0;
	BUG_ON(!pages);

	return __iov_iter_get_pages_alloc(i, &pages, maxsize, maxpages, start);
}
EXPORT_SYMBOL(iov_iter_get_pages2);

ssize_t iov_iter_get_pages_alloc2(struct iov_iter *i,
		struct page ***pages, size_t maxsize, size_t *start)
{
	ssize_t len;

	*pages = NULL;

	len = __iov_iter_get_pages_alloc(i, pages, maxsize, ~0U, start);
	if (len <= 0) {
		kvfree(*pages);
		*pages = NULL;
	}
	return len;
}
EXPORT_SYMBOL(iov_iter_get_pages_alloc2);

static int iov_npages(const struct iov_iter *i, int maxpages)
{
	size_t skip = i->iov_offset, size = i->count;
	const struct iovec *p;
	int npages = 0;

	for (p = iter_iov(i); size; skip = 0, p++) {
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
	if (likely(iter_is_ubuf(i))) {
		unsigned offs = offset_in_page(i->ubuf + i->iov_offset);
		int npages = DIV_ROUND_UP(offs + i->count, PAGE_SIZE);
		return min(npages, maxpages);
	}
	/* iovec and kvec have identical layouts */
	if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i)))
		return iov_npages(i, maxpages);
	if (iov_iter_is_bvec(i))
		return bvec_npages(i, maxpages);
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
	if (iov_iter_is_bvec(new))
		return new->bvec = kmemdup(new->bvec,
				    new->nr_segs * sizeof(struct bio_vec),
				    flags);
	else if (iov_iter_is_kvec(new) || iter_is_iovec(new))
		/* iovec and kvec have identical layout */
		return new->__iov = kmemdup(new->__iov,
				   new->nr_segs * sizeof(struct iovec),
				   flags);
	return NULL;
}
EXPORT_SYMBOL(dup_iter);

static __noclone int copy_compat_iovec_from_user(struct iovec *iov,
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

static __noclone int copy_iovec_from_user(struct iovec *iov,
		const struct iovec __user *uiov, unsigned long nr_segs)
{
	int ret = -EFAULT;

	if (!user_access_begin(uiov, nr_segs * sizeof(*uiov)))
		return -EFAULT;

	do {
		void __user *buf;
		ssize_t len;

		unsafe_get_user(len, &uiov->iov_len, uaccess_end);
		unsafe_get_user(buf, &uiov->iov_base, uaccess_end);

		/* check for size_t not fitting in ssize_t .. */
		if (unlikely(len < 0)) {
			ret = -EINVAL;
			goto uaccess_end;
		}
		iov->iov_base = buf;
		iov->iov_len = len;

		uiov++; iov++;
	} while (--nr_segs);

	ret = 0;
uaccess_end:
	user_access_end();
	return ret;
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

	if (unlikely(compat))
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

/*
 * Single segment iovec supplied by the user, import it as ITER_UBUF.
 */
static ssize_t __import_iovec_ubuf(int type, const struct iovec __user *uvec,
				   struct iovec **iovp, struct iov_iter *i,
				   bool compat)
{
	struct iovec *iov = *iovp;
	ssize_t ret;

	if (compat)
		ret = copy_compat_iovec_from_user(iov, uvec, 1);
	else
		ret = copy_iovec_from_user(iov, uvec, 1);
	if (unlikely(ret))
		return ret;

	ret = import_ubuf(type, iov->iov_base, iov->iov_len, i);
	if (unlikely(ret))
		return ret;
	*iovp = NULL;
	return i->count;
}

ssize_t __import_iovec(int type, const struct iovec __user *uvec,
		 unsigned nr_segs, unsigned fast_segs, struct iovec **iovp,
		 struct iov_iter *i, bool compat)
{
	ssize_t total_len = 0;
	unsigned long seg;
	struct iovec *iov;

	if (nr_segs == 1)
		return __import_iovec_ubuf(type, uvec, iovp, i, compat);

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

	iov_iter_ubuf(i, rw, buf, len);
	return 0;
}
EXPORT_SYMBOL(import_single_range);

int import_ubuf(int rw, void __user *buf, size_t len, struct iov_iter *i)
{
	if (len > MAX_RW_COUNT)
		len = MAX_RW_COUNT;
	if (unlikely(!access_ok(buf, len)))
		return -EFAULT;

	iov_iter_ubuf(i, rw, buf, len);
	return 0;
}
EXPORT_SYMBOL_GPL(import_ubuf);

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
	if (WARN_ON_ONCE(!iov_iter_is_bvec(i) && !iter_is_iovec(i) &&
			 !iter_is_ubuf(i)) && !iov_iter_is_kvec(i))
		return;
	i->iov_offset = state->iov_offset;
	i->count = state->count;
	if (iter_is_ubuf(i))
		return;
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
		i->__iov -= state->nr_segs - i->nr_segs;
	i->nr_segs = state->nr_segs;
}

/*
 * Extract a list of contiguous pages from an ITER_XARRAY iterator.  This does not
 * get references on the pages, nor does it get a pin on them.
 */
static ssize_t iov_iter_extract_xarray_pages(struct iov_iter *i,
					     struct page ***pages, size_t maxsize,
					     unsigned int maxpages,
					     iov_iter_extraction_t extraction_flags,
					     size_t *offset0)
{
	struct page *page, **p;
	unsigned int nr = 0, offset;
	loff_t pos = i->xarray_start + i->iov_offset;
	pgoff_t index = pos >> PAGE_SHIFT;
	XA_STATE(xas, i->xarray, index);

	offset = pos & ~PAGE_MASK;
	*offset0 = offset;

	maxpages = want_pages_array(pages, maxsize, offset, maxpages);
	if (!maxpages)
		return -ENOMEM;
	p = *pages;

	rcu_read_lock();
	for (page = xas_load(&xas); page; page = xas_next(&xas)) {
		if (xas_retry(&xas, page))
			continue;

		/* Has the page moved or been split? */
		if (unlikely(page != xas_reload(&xas))) {
			xas_reset(&xas);
			continue;
		}

		p[nr++] = find_subpage(page, xas.xa_index);
		if (nr == maxpages)
			break;
	}
	rcu_read_unlock();

	maxsize = min_t(size_t, nr * PAGE_SIZE - offset, maxsize);
	iov_iter_advance(i, maxsize);
	return maxsize;
}

/*
 * Extract a list of contiguous pages from an ITER_BVEC iterator.  This does
 * not get references on the pages, nor does it get a pin on them.
 */
static ssize_t iov_iter_extract_bvec_pages(struct iov_iter *i,
					   struct page ***pages, size_t maxsize,
					   unsigned int maxpages,
					   iov_iter_extraction_t extraction_flags,
					   size_t *offset0)
{
	struct page **p, *page;
	size_t skip = i->iov_offset, offset, size;
	int k;

	for (;;) {
		if (i->nr_segs == 0)
			return 0;
		size = min(maxsize, i->bvec->bv_len - skip);
		if (size)
			break;
		i->iov_offset = 0;
		i->nr_segs--;
		i->bvec++;
		skip = 0;
	}

	skip += i->bvec->bv_offset;
	page = i->bvec->bv_page + skip / PAGE_SIZE;
	offset = skip % PAGE_SIZE;
	*offset0 = offset;

	maxpages = want_pages_array(pages, size, offset, maxpages);
	if (!maxpages)
		return -ENOMEM;
	p = *pages;
	for (k = 0; k < maxpages; k++)
		p[k] = page + k;

	size = min_t(size_t, size, maxpages * PAGE_SIZE - offset);
	iov_iter_advance(i, size);
	return size;
}

/*
 * Extract a list of virtually contiguous pages from an ITER_KVEC iterator.
 * This does not get references on the pages, nor does it get a pin on them.
 */
static ssize_t iov_iter_extract_kvec_pages(struct iov_iter *i,
					   struct page ***pages, size_t maxsize,
					   unsigned int maxpages,
					   iov_iter_extraction_t extraction_flags,
					   size_t *offset0)
{
	struct page **p, *page;
	const void *kaddr;
	size_t skip = i->iov_offset, offset, len, size;
	int k;

	for (;;) {
		if (i->nr_segs == 0)
			return 0;
		size = min(maxsize, i->kvec->iov_len - skip);
		if (size)
			break;
		i->iov_offset = 0;
		i->nr_segs--;
		i->kvec++;
		skip = 0;
	}

	kaddr = i->kvec->iov_base + skip;
	offset = (unsigned long)kaddr & ~PAGE_MASK;
	*offset0 = offset;

	maxpages = want_pages_array(pages, size, offset, maxpages);
	if (!maxpages)
		return -ENOMEM;
	p = *pages;

	kaddr -= offset;
	len = offset + size;
	for (k = 0; k < maxpages; k++) {
		size_t seg = min_t(size_t, len, PAGE_SIZE);

		if (is_vmalloc_or_module_addr(kaddr))
			page = vmalloc_to_page(kaddr);
		else
			page = virt_to_page(kaddr);

		p[k] = page;
		len -= seg;
		kaddr += PAGE_SIZE;
	}

	size = min_t(size_t, size, maxpages * PAGE_SIZE - offset);
	iov_iter_advance(i, size);
	return size;
}

/*
 * Extract a list of contiguous pages from a user iterator and get a pin on
 * each of them.  This should only be used if the iterator is user-backed
 * (IOBUF/UBUF).
 *
 * It does not get refs on the pages, but the pages must be unpinned by the
 * caller once the transfer is complete.
 *
 * This is safe to be used where background IO/DMA *is* going to be modifying
 * the buffer; using a pin rather than a ref makes forces fork() to give the
 * child a copy of the page.
 */
static ssize_t iov_iter_extract_user_pages(struct iov_iter *i,
					   struct page ***pages,
					   size_t maxsize,
					   unsigned int maxpages,
					   iov_iter_extraction_t extraction_flags,
					   size_t *offset0)
{
	unsigned long addr;
	unsigned int gup_flags = 0;
	size_t offset;
	int res;

	if (i->data_source == ITER_DEST)
		gup_flags |= FOLL_WRITE;
	if (extraction_flags & ITER_ALLOW_P2PDMA)
		gup_flags |= FOLL_PCI_P2PDMA;
	if (i->nofault)
		gup_flags |= FOLL_NOFAULT;

	addr = first_iovec_segment(i, &maxsize);
	*offset0 = offset = addr % PAGE_SIZE;
	addr &= PAGE_MASK;
	maxpages = want_pages_array(pages, maxsize, offset, maxpages);
	if (!maxpages)
		return -ENOMEM;
	res = pin_user_pages_fast(addr, maxpages, gup_flags, *pages);
	if (unlikely(res <= 0))
		return res;
	maxsize = min_t(size_t, maxsize, res * PAGE_SIZE - offset);
	iov_iter_advance(i, maxsize);
	return maxsize;
}

/**
 * iov_iter_extract_pages - Extract a list of contiguous pages from an iterator
 * @i: The iterator to extract from
 * @pages: Where to return the list of pages
 * @maxsize: The maximum amount of iterator to extract
 * @maxpages: The maximum size of the list of pages
 * @extraction_flags: Flags to qualify request
 * @offset0: Where to return the starting offset into (*@pages)[0]
 *
 * Extract a list of contiguous pages from the current point of the iterator,
 * advancing the iterator.  The maximum number of pages and the maximum amount
 * of page contents can be set.
 *
 * If *@pages is NULL, a page list will be allocated to the required size and
 * *@pages will be set to its base.  If *@pages is not NULL, it will be assumed
 * that the caller allocated a page list at least @maxpages in size and this
 * will be filled in.
 *
 * @extraction_flags can have ITER_ALLOW_P2PDMA set to request peer-to-peer DMA
 * be allowed on the pages extracted.
 *
 * The iov_iter_extract_will_pin() function can be used to query how cleanup
 * should be performed.
 *
 * Extra refs or pins on the pages may be obtained as follows:
 *
 *  (*) If the iterator is user-backed (ITER_IOVEC/ITER_UBUF), pins will be
 *      added to the pages, but refs will not be taken.
 *      iov_iter_extract_will_pin() will return true.
 *
 *  (*) If the iterator is ITER_KVEC, ITER_BVEC or ITER_XARRAY, the pages are
 *      merely listed; no extra refs or pins are obtained.
 *      iov_iter_extract_will_pin() will return 0.
 *
 * Note also:
 *
 *  (*) Use with ITER_DISCARD is not supported as that has no content.
 *
 * On success, the function sets *@pages to the new pagelist, if allocated, and
 * sets *offset0 to the offset into the first page.
 *
 * It may also return -ENOMEM and -EFAULT.
 */
ssize_t iov_iter_extract_pages(struct iov_iter *i,
			       struct page ***pages,
			       size_t maxsize,
			       unsigned int maxpages,
			       iov_iter_extraction_t extraction_flags,
			       size_t *offset0)
{
	maxsize = min_t(size_t, min_t(size_t, maxsize, i->count), MAX_RW_COUNT);
	if (!maxsize)
		return 0;

	if (likely(user_backed_iter(i)))
		return iov_iter_extract_user_pages(i, pages, maxsize,
						   maxpages, extraction_flags,
						   offset0);
	if (iov_iter_is_kvec(i))
		return iov_iter_extract_kvec_pages(i, pages, maxsize,
						   maxpages, extraction_flags,
						   offset0);
	if (iov_iter_is_bvec(i))
		return iov_iter_extract_bvec_pages(i, pages, maxsize,
						   maxpages, extraction_flags,
						   offset0);
	if (iov_iter_is_xarray(i))
		return iov_iter_extract_xarray_pages(i, pages, maxsize,
						     maxpages, extraction_flags,
						     offset0);
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(iov_iter_extract_pages);
