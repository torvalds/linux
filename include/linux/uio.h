/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Berkeley style UIO structures	-	Alan Cox 1994.
 */
#ifndef __LINUX_UIO_H
#define __LINUX_UIO_H

#include <linux/kernel.h>
#include <linux/thread_info.h>
#include <linux/mm_types.h>
#include <uapi/linux/uio.h>

struct page;

typedef unsigned int __bitwise iov_iter_extraction_t;

struct kvec {
	void *iov_base; /* and that should *never* hold a userland pointer */
	size_t iov_len;
};

enum iter_type {
	/* iter types */
	ITER_IOVEC,
	ITER_KVEC,
	ITER_BVEC,
	ITER_XARRAY,
	ITER_DISCARD,
	ITER_UBUF,
};

#define ITER_SOURCE	1	// == WRITE
#define ITER_DEST	0	// == READ

struct iov_iter_state {
	size_t iov_offset;
	size_t count;
	unsigned long nr_segs;
};

struct iov_iter {
	u8 iter_type;
	bool copy_mc;
	bool nofault;
	bool data_source;
	bool user_backed;
	union {
		size_t iov_offset;
		int last_offset;
	};
	/*
	 * Hack alert: overlay ubuf_iovec with iovec + count, so
	 * that the members resolve correctly regardless of the type
	 * of iterator used. This means that you can use:
	 *
	 * &iter->__ubuf_iovec or iter->__iov
	 *
	 * interchangably for the user_backed cases, hence simplifying
	 * some of the cases that need to deal with both.
	 */
	union {
		/*
		 * This really should be a const, but we cannot do that without
		 * also modifying any of the zero-filling iter init functions.
		 * Leave it non-const for now, but it should be treated as such.
		 */
		struct iovec __ubuf_iovec;
		struct {
			union {
				/* use iter_iov() to get the current vec */
				const struct iovec *__iov;
				const struct kvec *kvec;
				const struct bio_vec *bvec;
				struct xarray *xarray;
				void __user *ubuf;
			};
			size_t count;
		};
	};
	union {
		unsigned long nr_segs;
		loff_t xarray_start;
	};
};

static inline const struct iovec *iter_iov(const struct iov_iter *iter)
{
	if (iter->iter_type == ITER_UBUF)
		return (const struct iovec *) &iter->__ubuf_iovec;
	return iter->__iov;
}

#define iter_iov_addr(iter)	(iter_iov(iter)->iov_base + (iter)->iov_offset)
#define iter_iov_len(iter)	(iter_iov(iter)->iov_len - (iter)->iov_offset)

static inline enum iter_type iov_iter_type(const struct iov_iter *i)
{
	return i->iter_type;
}

static inline void iov_iter_save_state(struct iov_iter *iter,
				       struct iov_iter_state *state)
{
	state->iov_offset = iter->iov_offset;
	state->count = iter->count;
	state->nr_segs = iter->nr_segs;
}

static inline bool iter_is_ubuf(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_UBUF;
}

static inline bool iter_is_iovec(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_IOVEC;
}

static inline bool iov_iter_is_kvec(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_KVEC;
}

static inline bool iov_iter_is_bvec(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_BVEC;
}

static inline bool iov_iter_is_discard(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_DISCARD;
}

static inline bool iov_iter_is_xarray(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_XARRAY;
}

static inline unsigned char iov_iter_rw(const struct iov_iter *i)
{
	return i->data_source ? WRITE : READ;
}

static inline bool user_backed_iter(const struct iov_iter *i)
{
	return i->user_backed;
}

/*
 * Total number of bytes covered by an iovec.
 *
 * NOTE that it is not safe to use this function until all the iovec's
 * segment lengths have been validated.  Because the individual lengths can
 * overflow a size_t when added together.
 */
static inline size_t iov_length(const struct iovec *iov, unsigned long nr_segs)
{
	unsigned long seg;
	size_t ret = 0;

	for (seg = 0; seg < nr_segs; seg++)
		ret += iov[seg].iov_len;
	return ret;
}

size_t copy_page_from_iter_atomic(struct page *page, unsigned offset,
				  size_t bytes, struct iov_iter *i);
void iov_iter_advance(struct iov_iter *i, size_t bytes);
void iov_iter_revert(struct iov_iter *i, size_t bytes);
size_t fault_in_iov_iter_readable(const struct iov_iter *i, size_t bytes);
size_t fault_in_iov_iter_writeable(const struct iov_iter *i, size_t bytes);
size_t iov_iter_single_seg_count(const struct iov_iter *i);
size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i);
size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i);

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i);
size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i);
size_t _copy_from_iter_nocache(void *addr, size_t bytes, struct iov_iter *i);

static inline size_t copy_folio_to_iter(struct folio *folio, size_t offset,
		size_t bytes, struct iov_iter *i)
{
	return copy_page_to_iter(&folio->page, offset, bytes, i);
}
size_t copy_page_to_iter_nofault(struct page *page, unsigned offset,
				 size_t bytes, struct iov_iter *i);

static __always_inline __must_check
size_t copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	if (check_copy_size(addr, bytes, true))
		return _copy_to_iter(addr, bytes, i);
	return 0;
}

static __always_inline __must_check
size_t copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (check_copy_size(addr, bytes, false))
		return _copy_from_iter(addr, bytes, i);
	return 0;
}

static __always_inline __must_check
bool copy_from_iter_full(void *addr, size_t bytes, struct iov_iter *i)
{
	size_t copied = copy_from_iter(addr, bytes, i);
	if (likely(copied == bytes))
		return true;
	iov_iter_revert(i, copied);
	return false;
}

static __always_inline __must_check
size_t copy_from_iter_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	if (check_copy_size(addr, bytes, false))
		return _copy_from_iter_nocache(addr, bytes, i);
	return 0;
}

static __always_inline __must_check
bool copy_from_iter_full_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	size_t copied = copy_from_iter_nocache(addr, bytes, i);
	if (likely(copied == bytes))
		return true;
	iov_iter_revert(i, copied);
	return false;
}

#ifdef CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE
/*
 * Note, users like pmem that depend on the stricter semantics of
 * _copy_from_iter_flushcache() than _copy_from_iter_nocache() must check for
 * IS_ENABLED(CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE) before assuming that the
 * destination is flushed from the cache on return.
 */
size_t _copy_from_iter_flushcache(void *addr, size_t bytes, struct iov_iter *i);
#else
#define _copy_from_iter_flushcache _copy_from_iter_nocache
#endif

#ifdef CONFIG_ARCH_HAS_COPY_MC
size_t _copy_mc_to_iter(const void *addr, size_t bytes, struct iov_iter *i);
static inline void iov_iter_set_copy_mc(struct iov_iter *i)
{
	i->copy_mc = true;
}

static inline bool iov_iter_is_copy_mc(const struct iov_iter *i)
{
	return i->copy_mc;
}
#else
#define _copy_mc_to_iter _copy_to_iter
static inline void iov_iter_set_copy_mc(struct iov_iter *i) { }
static inline bool iov_iter_is_copy_mc(const struct iov_iter *i)
{
	return false;
}
#endif

size_t iov_iter_zero(size_t bytes, struct iov_iter *);
bool iov_iter_is_aligned(const struct iov_iter *i, unsigned addr_mask,
			unsigned len_mask);
unsigned long iov_iter_alignment(const struct iov_iter *i);
unsigned long iov_iter_gap_alignment(const struct iov_iter *i);
void iov_iter_init(struct iov_iter *i, unsigned int direction, const struct iovec *iov,
			unsigned long nr_segs, size_t count);
void iov_iter_kvec(struct iov_iter *i, unsigned int direction, const struct kvec *kvec,
			unsigned long nr_segs, size_t count);
void iov_iter_bvec(struct iov_iter *i, unsigned int direction, const struct bio_vec *bvec,
			unsigned long nr_segs, size_t count);
void iov_iter_discard(struct iov_iter *i, unsigned int direction, size_t count);
void iov_iter_xarray(struct iov_iter *i, unsigned int direction, struct xarray *xarray,
		     loff_t start, size_t count);
ssize_t iov_iter_get_pages2(struct iov_iter *i, struct page **pages,
			size_t maxsize, unsigned maxpages, size_t *start);
ssize_t iov_iter_get_pages_alloc2(struct iov_iter *i, struct page ***pages,
			size_t maxsize, size_t *start);
int iov_iter_npages(const struct iov_iter *i, int maxpages);
void iov_iter_restore(struct iov_iter *i, struct iov_iter_state *state);

const void *dup_iter(struct iov_iter *new, struct iov_iter *old, gfp_t flags);

static inline size_t iov_iter_count(const struct iov_iter *i)
{
	return i->count;
}

/*
 * Cap the iov_iter by given limit; note that the second argument is
 * *not* the new size - it's upper limit for such.  Passing it a value
 * greater than the amount of data in iov_iter is fine - it'll just do
 * nothing in that case.
 */
static inline void iov_iter_truncate(struct iov_iter *i, u64 count)
{
	/*
	 * count doesn't have to fit in size_t - comparison extends both
	 * operands to u64 here and any value that would be truncated by
	 * conversion in assignement is by definition greater than all
	 * values of size_t, including old i->count.
	 */
	if (i->count > count)
		i->count = count;
}

/*
 * reexpand a previously truncated iterator; count must be no more than how much
 * we had shrunk it.
 */
static inline void iov_iter_reexpand(struct iov_iter *i, size_t count)
{
	i->count = count;
}

static inline int
iov_iter_npages_cap(struct iov_iter *i, int maxpages, size_t max_bytes)
{
	size_t shorted = 0;
	int npages;

	if (iov_iter_count(i) > max_bytes) {
		shorted = iov_iter_count(i) - max_bytes;
		iov_iter_truncate(i, max_bytes);
	}
	npages = iov_iter_npages(i, maxpages);
	if (shorted)
		iov_iter_reexpand(i, iov_iter_count(i) + shorted);

	return npages;
}

struct csum_state {
	__wsum csum;
	size_t off;
};

size_t csum_and_copy_to_iter(const void *addr, size_t bytes, void *csstate, struct iov_iter *i);
size_t csum_and_copy_from_iter(void *addr, size_t bytes, __wsum *csum, struct iov_iter *i);

static __always_inline __must_check
bool csum_and_copy_from_iter_full(void *addr, size_t bytes,
				  __wsum *csum, struct iov_iter *i)
{
	size_t copied = csum_and_copy_from_iter(addr, bytes, csum, i);
	if (likely(copied == bytes))
		return true;
	iov_iter_revert(i, copied);
	return false;
}
size_t hash_and_copy_to_iter(const void *addr, size_t bytes, void *hashp,
		struct iov_iter *i);

struct iovec *iovec_from_user(const struct iovec __user *uvector,
		unsigned long nr_segs, unsigned long fast_segs,
		struct iovec *fast_iov, bool compat);
ssize_t import_iovec(int type, const struct iovec __user *uvec,
		 unsigned nr_segs, unsigned fast_segs, struct iovec **iovp,
		 struct iov_iter *i);
ssize_t __import_iovec(int type, const struct iovec __user *uvec,
		 unsigned nr_segs, unsigned fast_segs, struct iovec **iovp,
		 struct iov_iter *i, bool compat);
int import_single_range(int type, void __user *buf, size_t len,
		 struct iovec *iov, struct iov_iter *i);
int import_ubuf(int type, void __user *buf, size_t len, struct iov_iter *i);

static inline void iov_iter_ubuf(struct iov_iter *i, unsigned int direction,
			void __user *buf, size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	*i = (struct iov_iter) {
		.iter_type = ITER_UBUF,
		.copy_mc = false,
		.user_backed = true,
		.data_source = direction,
		.ubuf = buf,
		.count = count,
		.nr_segs = 1
	};
}
/* Flags for iov_iter_get/extract_pages*() */
/* Allow P2PDMA on the extracted pages */
#define ITER_ALLOW_P2PDMA	((__force iov_iter_extraction_t)0x01)

ssize_t iov_iter_extract_pages(struct iov_iter *i, struct page ***pages,
			       size_t maxsize, unsigned int maxpages,
			       iov_iter_extraction_t extraction_flags,
			       size_t *offset0);

/**
 * iov_iter_extract_will_pin - Indicate how pages from the iterator will be retained
 * @iter: The iterator
 *
 * Examine the iterator and indicate by returning true or false as to how, if
 * at all, pages extracted from the iterator will be retained by the extraction
 * function.
 *
 * %true indicates that the pages will have a pin placed in them that the
 * caller must unpin.  This is must be done for DMA/async DIO to force fork()
 * to forcibly copy a page for the child (the parent must retain the original
 * page).
 *
 * %false indicates that no measures are taken and that it's up to the caller
 * to retain the pages.
 */
static inline bool iov_iter_extract_will_pin(const struct iov_iter *iter)
{
	return user_backed_iter(iter);
}

#endif
