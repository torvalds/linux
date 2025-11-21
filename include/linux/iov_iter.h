/* SPDX-License-Identifier: GPL-2.0-or-later */
/* I/O iterator iteration building functions.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_IOV_ITER_H
#define _LINUX_IOV_ITER_H

#include <linux/uio.h>
#include <linux/bvec.h>
#include <linux/folio_queue.h>

typedef size_t (*iov_step_f)(void *iter_base, size_t progress, size_t len,
			     void *priv, void *priv2);
typedef size_t (*iov_ustep_f)(void __user *iter_base, size_t progress, size_t len,
			      void *priv, void *priv2);

/*
 * Handle ITER_UBUF.
 */
static __always_inline
size_t iterate_ubuf(struct iov_iter *iter, size_t len, void *priv, void *priv2,
		    iov_ustep_f step)
{
	void __user *base = iter->ubuf;
	size_t progress = 0, remain;

	remain = step(base + iter->iov_offset, 0, len, priv, priv2);
	progress = len - remain;
	iter->iov_offset += progress;
	iter->count -= progress;
	return progress;
}

/*
 * Handle ITER_IOVEC.
 */
static __always_inline
size_t iterate_iovec(struct iov_iter *iter, size_t len, void *priv, void *priv2,
		     iov_ustep_f step)
{
	const struct iovec *p = iter->__iov;
	size_t progress = 0, skip = iter->iov_offset;

	do {
		size_t remain, consumed;
		size_t part = min(len, p->iov_len - skip);

		if (likely(part)) {
			remain = step(p->iov_base + skip, progress, part, priv, priv2);
			consumed = part - remain;
			progress += consumed;
			skip += consumed;
			len -= consumed;
			if (skip < p->iov_len)
				break;
		}
		p++;
		skip = 0;
	} while (len);

	iter->nr_segs -= p - iter->__iov;
	iter->__iov = p;
	iter->iov_offset = skip;
	iter->count -= progress;
	return progress;
}

/*
 * Handle ITER_KVEC.
 */
static __always_inline
size_t iterate_kvec(struct iov_iter *iter, size_t len, void *priv, void *priv2,
		    iov_step_f step)
{
	const struct kvec *p = iter->kvec;
	size_t progress = 0, skip = iter->iov_offset;

	do {
		size_t remain, consumed;
		size_t part = min(len, p->iov_len - skip);

		if (likely(part)) {
			remain = step(p->iov_base + skip, progress, part, priv, priv2);
			consumed = part - remain;
			progress += consumed;
			skip += consumed;
			len -= consumed;
			if (skip < p->iov_len)
				break;
		}
		p++;
		skip = 0;
	} while (len);

	iter->nr_segs -= p - iter->kvec;
	iter->kvec = p;
	iter->iov_offset = skip;
	iter->count -= progress;
	return progress;
}

/*
 * Handle ITER_BVEC.
 */
static __always_inline
size_t iterate_bvec(struct iov_iter *iter, size_t len, void *priv, void *priv2,
		    iov_step_f step)
{
	const struct bio_vec *p = iter->bvec;
	size_t progress = 0, skip = iter->iov_offset;

	do {
		size_t remain, consumed;
		size_t offset = p->bv_offset + skip, part;
		void *kaddr = kmap_local_page(p->bv_page + offset / PAGE_SIZE);

		part = min3(len,
			   (size_t)(p->bv_len - skip),
			   (size_t)(PAGE_SIZE - offset % PAGE_SIZE));
		remain = step(kaddr + offset % PAGE_SIZE, progress, part, priv, priv2);
		kunmap_local(kaddr);
		consumed = part - remain;
		len -= consumed;
		progress += consumed;
		skip += consumed;
		if (skip >= p->bv_len) {
			skip = 0;
			p++;
		}
		if (remain)
			break;
	} while (len);

	iter->nr_segs -= p - iter->bvec;
	iter->bvec = p;
	iter->iov_offset = skip;
	iter->count -= progress;
	return progress;
}

/*
 * Handle ITER_FOLIOQ.
 */
static __always_inline
size_t iterate_folioq(struct iov_iter *iter, size_t len, void *priv, void *priv2,
		      iov_step_f step)
{
	const struct folio_queue *folioq = iter->folioq;
	unsigned int slot = iter->folioq_slot;
	size_t progress = 0, skip = iter->iov_offset;

	if (slot == folioq_nr_slots(folioq)) {
		/* The iterator may have been extended. */
		folioq = folioq->next;
		slot = 0;
	}

	do {
		struct folio *folio = folioq_folio(folioq, slot);
		size_t part, remain = 0, consumed;
		size_t fsize;
		void *base;

		if (!folio)
			break;

		fsize = folioq_folio_size(folioq, slot);
		if (skip < fsize) {
			base = kmap_local_folio(folio, skip);
			part = umin(len, PAGE_SIZE - skip % PAGE_SIZE);
			remain = step(base, progress, part, priv, priv2);
			kunmap_local(base);
			consumed = part - remain;
			len -= consumed;
			progress += consumed;
			skip += consumed;
		}
		if (skip >= fsize) {
			skip = 0;
			slot++;
			if (slot == folioq_nr_slots(folioq) && folioq->next) {
				folioq = folioq->next;
				slot = 0;
			}
		}
		if (remain)
			break;
	} while (len);

	iter->folioq_slot = slot;
	iter->folioq = folioq;
	iter->iov_offset = skip;
	iter->count -= progress;
	return progress;
}

/*
 * Handle ITER_XARRAY.
 */
static __always_inline
size_t iterate_xarray(struct iov_iter *iter, size_t len, void *priv, void *priv2,
		      iov_step_f step)
{
	struct folio *folio;
	size_t progress = 0;
	loff_t start = iter->xarray_start + iter->iov_offset;
	pgoff_t index = start / PAGE_SIZE;
	XA_STATE(xas, iter->xarray, index);

	rcu_read_lock();
	xas_for_each(&xas, folio, ULONG_MAX) {
		size_t remain, consumed, offset, part, flen;

		if (xas_retry(&xas, folio))
			continue;
		if (WARN_ON(xa_is_value(folio)))
			break;
		if (WARN_ON(folio_test_hugetlb(folio)))
			break;

		offset = offset_in_folio(folio, start + progress);
		flen = min(folio_size(folio) - offset, len);

		while (flen) {
			void *base = kmap_local_folio(folio, offset);

			part = min_t(size_t, flen,
				     PAGE_SIZE - offset_in_page(offset));
			remain = step(base, progress, part, priv, priv2);
			kunmap_local(base);

			consumed = part - remain;
			progress += consumed;
			len -= consumed;

			if (remain || len == 0)
				goto out;
			flen -= consumed;
			offset += consumed;
		}
	}

out:
	rcu_read_unlock();
	iter->iov_offset += progress;
	iter->count -= progress;
	return progress;
}

/*
 * Handle ITER_DISCARD.
 */
static __always_inline
size_t iterate_discard(struct iov_iter *iter, size_t len, void *priv, void *priv2,
		      iov_step_f step)
{
	size_t progress = len;

	iter->count -= progress;
	return progress;
}

/**
 * iterate_and_advance2 - Iterate over an iterator
 * @iter: The iterator to iterate over.
 * @len: The amount to iterate over.
 * @priv: Data for the step functions.
 * @priv2: More data for the step functions.
 * @ustep: Function for UBUF/IOVEC iterators; given __user addresses.
 * @step: Function for other iterators; given kernel addresses.
 *
 * Iterate over the next part of an iterator, up to the specified length.  The
 * buffer is presented in segments, which for kernel iteration are broken up by
 * physical pages and mapped, with the mapped address being presented.
 *
 * Two step functions, @step and @ustep, must be provided, one for handling
 * mapped kernel addresses and the other is given user addresses which have the
 * potential to fault since no pinning is performed.
 *
 * The step functions are passed the address and length of the segment, @priv,
 * @priv2 and the amount of data so far iterated over (which can, for example,
 * be added to @priv to point to the right part of a second buffer).  The step
 * functions should return the amount of the segment they didn't process (ie. 0
 * indicates complete processsing).
 *
 * This function returns the amount of data processed (ie. 0 means nothing was
 * processed and the value of @len means processes to completion).
 */
static __always_inline
size_t iterate_and_advance2(struct iov_iter *iter, size_t len, void *priv,
			    void *priv2, iov_ustep_f ustep, iov_step_f step)
{
	if (unlikely(iter->count < len))
		len = iter->count;
	if (unlikely(!len))
		return 0;

	if (likely(iter_is_ubuf(iter)))
		return iterate_ubuf(iter, len, priv, priv2, ustep);
	if (likely(iter_is_iovec(iter)))
		return iterate_iovec(iter, len, priv, priv2, ustep);
	if (iov_iter_is_bvec(iter))
		return iterate_bvec(iter, len, priv, priv2, step);
	if (iov_iter_is_kvec(iter))
		return iterate_kvec(iter, len, priv, priv2, step);
	if (iov_iter_is_folioq(iter))
		return iterate_folioq(iter, len, priv, priv2, step);
	if (iov_iter_is_xarray(iter))
		return iterate_xarray(iter, len, priv, priv2, step);
	return iterate_discard(iter, len, priv, priv2, step);
}

/**
 * iterate_and_advance - Iterate over an iterator
 * @iter: The iterator to iterate over.
 * @len: The amount to iterate over.
 * @priv: Data for the step functions.
 * @ustep: Function for UBUF/IOVEC iterators; given __user addresses.
 * @step: Function for other iterators; given kernel addresses.
 *
 * As iterate_and_advance2(), but priv2 is always NULL.
 */
static __always_inline
size_t iterate_and_advance(struct iov_iter *iter, size_t len, void *priv,
			   iov_ustep_f ustep, iov_step_f step)
{
	return iterate_and_advance2(iter, len, priv, NULL, ustep, step);
}

/**
 * iterate_and_advance_kernel - Iterate over a kernel-internal iterator
 * @iter: The iterator to iterate over.
 * @len: The amount to iterate over.
 * @priv: Data for the step functions.
 * @priv2: More data for the step functions.
 * @step: Function for other iterators; given kernel addresses.
 *
 * Iterate over the next part of an iterator, up to the specified length.  The
 * buffer is presented in segments, which for kernel iteration are broken up by
 * physical pages and mapped, with the mapped address being presented.
 *
 * [!] Note This will only handle BVEC, KVEC, FOLIOQ, XARRAY and DISCARD-type
 * iterators; it will not handle UBUF or IOVEC-type iterators.
 *
 * A step functions, @step, must be provided, one for handling mapped kernel
 * addresses and the other is given user addresses which have the potential to
 * fault since no pinning is performed.
 *
 * The step functions are passed the address and length of the segment, @priv,
 * @priv2 and the amount of data so far iterated over (which can, for example,
 * be added to @priv to point to the right part of a second buffer).  The step
 * functions should return the amount of the segment they didn't process (ie. 0
 * indicates complete processsing).
 *
 * This function returns the amount of data processed (ie. 0 means nothing was
 * processed and the value of @len means processes to completion).
 */
static __always_inline
size_t iterate_and_advance_kernel(struct iov_iter *iter, size_t len, void *priv,
				  void *priv2, iov_step_f step)
{
	if (unlikely(iter->count < len))
		len = iter->count;
	if (unlikely(!len))
		return 0;
	if (iov_iter_is_bvec(iter))
		return iterate_bvec(iter, len, priv, priv2, step);
	if (iov_iter_is_kvec(iter))
		return iterate_kvec(iter, len, priv, priv2, step);
	if (iov_iter_is_folioq(iter))
		return iterate_folioq(iter, len, priv, priv2, step);
	if (iov_iter_is_xarray(iter))
		return iterate_xarray(iter, len, priv, priv2, step);
	return iterate_discard(iter, len, priv, priv2, step);
}

#endif /* _LINUX_IOV_ITER_H */
