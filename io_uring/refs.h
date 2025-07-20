#ifndef IOU_REQ_REF_H
#define IOU_REQ_REF_H

#include <linux/atomic.h>
#include <linux/io_uring_types.h>

/*
 * Shamelessly stolen from the mm implementation of page reference checking,
 * see commit f958d7b528b1 for details.
 */
#define req_ref_zero_or_close_to_overflow(req)	\
	((unsigned int) atomic_read(&(req->refs)) + 127u <= 127u)

static inline bool req_ref_inc_not_zero(struct io_kiocb *req)
{
	WARN_ON_ONCE(!(req->flags & REQ_F_REFCOUNT));
	return atomic_inc_not_zero(&req->refs);
}

static inline bool req_ref_put_and_test_atomic(struct io_kiocb *req)
{
	WARN_ON_ONCE(!(data_race(req->flags) & REQ_F_REFCOUNT));
	WARN_ON_ONCE(req_ref_zero_or_close_to_overflow(req));
	return atomic_dec_and_test(&req->refs);
}

static inline bool req_ref_put_and_test(struct io_kiocb *req)
{
	if (likely(!(req->flags & REQ_F_REFCOUNT)))
		return true;

	WARN_ON_ONCE(req_ref_zero_or_close_to_overflow(req));
	return atomic_dec_and_test(&req->refs);
}

static inline void req_ref_get(struct io_kiocb *req)
{
	WARN_ON_ONCE(!(req->flags & REQ_F_REFCOUNT));
	WARN_ON_ONCE(req_ref_zero_or_close_to_overflow(req));
	atomic_inc(&req->refs);
}

static inline void req_ref_put(struct io_kiocb *req)
{
	WARN_ON_ONCE(!(req->flags & REQ_F_REFCOUNT));
	WARN_ON_ONCE(req_ref_zero_or_close_to_overflow(req));
	atomic_dec(&req->refs);
}

static inline void __io_req_set_refcount(struct io_kiocb *req, int nr)
{
	if (!(req->flags & REQ_F_REFCOUNT)) {
		req->flags |= REQ_F_REFCOUNT;
		atomic_set(&req->refs, nr);
	}
}

static inline void io_req_set_refcount(struct io_kiocb *req)
{
	__io_req_set_refcount(req, 1);
}
#endif
