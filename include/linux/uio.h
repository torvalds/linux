/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Berkeley style UIO structures	-	Alan Cox 1994.
 */
#ifndef __LINUX_UIO_H
#define __LINUX_UIO_H

#include <linux/kernel.h>
#include <linux/thread_info.h>
#include <crypto/hash.h>
#include <uapi/linux/uio.h>

struct page;
struct pipe_inode_info;

struct kvec {
	void *iov_base; /* and that should *never* hold a userland pointer */
	size_t iov_len;
};

enum iter_type {
	/* iter types */
	ITER_IOVEC = 4,
	ITER_KVEC = 8,
	ITER_BVEC = 16,
	ITER_PIPE = 32,
	ITER_DISCARD = 64,
};

struct iov_iter {
	/*
	 * Bit 0 is the read/write bit, set if we're writing.
	 * Bit 1 is the BVEC_FLAG_NO_REF bit, set if type is a bvec and
	 * the caller isn't expecting to drop a page reference when done.
	 */
	unsigned int type;
	size_t iov_offset;
	size_t count;
	union {
		const struct iovec *iov;
		const struct kvec *kvec;
		const struct bio_vec *bvec;
		struct pipe_inode_info *pipe;
	};
	union {
		unsigned long nr_segs;
		struct {
			unsigned int head;
			unsigned int start_head;
		};
	};
};

static inline enum iter_type iov_iter_type(const struct iov_iter *i)
{
	return i->type & ~(READ | WRITE);
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

static inline bool iov_iter_is_pipe(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_PIPE;
}

static inline bool iov_iter_is_discard(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_DISCARD;
}

static inline unsigned char iov_iter_rw(const struct iov_iter *i)
{
	return i->type & (READ | WRITE);
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

static inline struct iovec iov_iter_iovec(const struct iov_iter *iter)
{
	return (struct iovec) {
		.iov_base = iter->iov->iov_base + iter->iov_offset,
		.iov_len = min(iter->count,
			       iter->iov->iov_len - iter->iov_offset),
	};
}

size_t iov_iter_copy_from_user_atomic(struct page *page,
		struct iov_iter *i, unsigned long offset, size_t bytes);
void iov_iter_advance(struct iov_iter *i, size_t bytes);
void iov_iter_revert(struct iov_iter *i, size_t bytes);
int iov_iter_fault_in_readable(struct iov_iter *i, size_t bytes);
size_t iov_iter_single_seg_count(const struct iov_iter *i);
size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i);
size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i);

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i);
size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i);
bool _copy_from_iter_full(void *addr, size_t bytes, struct iov_iter *i);
size_t _copy_from_iter_nocache(void *addr, size_t bytes, struct iov_iter *i);
bool _copy_from_iter_full_nocache(void *addr, size_t bytes, struct iov_iter *i);

static __always_inline __must_check
size_t copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, true)))
		return 0;
	else
		return _copy_to_iter(addr, bytes, i);
}

static __always_inline __must_check
size_t copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, false)))
		return 0;
	else
		return _copy_from_iter(addr, bytes, i);
}

static __always_inline __must_check
bool copy_from_iter_full(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, false)))
		return false;
	else
		return _copy_from_iter_full(addr, bytes, i);
}

static __always_inline __must_check
size_t copy_from_iter_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, false)))
		return 0;
	else
		return _copy_from_iter_nocache(addr, bytes, i);
}

static __always_inline __must_check
bool copy_from_iter_full_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, false)))
		return false;
	else
		return _copy_from_iter_full_nocache(addr, bytes, i);
}

#ifdef CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE
/*
 * Note, users like pmem that depend on the stricter semantics of
 * copy_from_iter_flushcache() than copy_from_iter_nocache() must check for
 * IS_ENABLED(CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE) before assuming that the
 * destination is flushed from the cache on return.
 */
size_t _copy_from_iter_flushcache(void *addr, size_t bytes, struct iov_iter *i);
#else
#define _copy_from_iter_flushcache _copy_from_iter_nocache
#endif

#ifdef CONFIG_ARCH_HAS_UACCESS_MCSAFE
size_t _copy_to_iter_mcsafe(const void *addr, size_t bytes, struct iov_iter *i);
#else
#define _copy_to_iter_mcsafe _copy_to_iter
#endif

static __always_inline __must_check
size_t copy_from_iter_flushcache(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, false)))
		return 0;
	else
		return _copy_from_iter_flushcache(addr, bytes, i);
}

static __always_inline __must_check
size_t copy_to_iter_mcsafe(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, true)))
		return 0;
	else
		return _copy_to_iter_mcsafe(addr, bytes, i);
}

size_t iov_iter_zero(size_t bytes, struct iov_iter *);
unsigned long iov_iter_alignment(const struct iov_iter *i);
unsigned long iov_iter_gap_alignment(const struct iov_iter *i);
void iov_iter_init(struct iov_iter *i, unsigned int direction, const struct iovec *iov,
			unsigned long nr_segs, size_t count);
void iov_iter_kvec(struct iov_iter *i, unsigned int direction, const struct kvec *kvec,
			unsigned long nr_segs, size_t count);
void iov_iter_bvec(struct iov_iter *i, unsigned int direction, const struct bio_vec *bvec,
			unsigned long nr_segs, size_t count);
void iov_iter_pipe(struct iov_iter *i, unsigned int direction, struct pipe_inode_info *pipe,
			size_t count);
void iov_iter_discard(struct iov_iter *i, unsigned int direction, size_t count);
ssize_t iov_iter_get_pages(struct iov_iter *i, struct page **pages,
			size_t maxsize, unsigned maxpages, size_t *start);
ssize_t iov_iter_get_pages_alloc(struct iov_iter *i, struct page ***pages,
			size_t maxsize, size_t *start);
int iov_iter_npages(const struct iov_iter *i, int maxpages);

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
size_t csum_and_copy_to_iter(const void *addr, size_t bytes, void *csump, struct iov_iter *i);
size_t csum_and_copy_from_iter(void *addr, size_t bytes, __wsum *csum, struct iov_iter *i);
bool csum_and_copy_from_iter_full(void *addr, size_t bytes, __wsum *csum, struct iov_iter *i);
size_t hash_and_copy_to_iter(const void *addr, size_t bytes, void *hashp,
		struct iov_iter *i);

ssize_t import_iovec(int type, const struct iovec __user * uvector,
		 unsigned nr_segs, unsigned fast_segs,
		 struct iovec **iov, struct iov_iter *i);

#ifdef CONFIG_COMPAT
struct compat_iovec;
ssize_t compat_import_iovec(int type, const struct compat_iovec __user * uvector,
		 unsigned nr_segs, unsigned fast_segs,
		 struct iovec **iov, struct iov_iter *i);
#endif

int import_single_range(int type, void __user *buf, size_t len,
		 struct iovec *iov, struct iov_iter *i);

int iov_iter_for_each_range(struct iov_iter *i, size_t bytes,
			    int (*f)(struct kvec *vec, void *context),
			    void *context);

#endif
