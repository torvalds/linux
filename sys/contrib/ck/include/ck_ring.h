/*
 * Copyright 2009-2015 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CK_RING_H
#define CK_RING_H

#include <ck_cc.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_string.h>

/*
 * Concurrent ring buffer.
 */

struct ck_ring {
	unsigned int c_head;
	char pad[CK_MD_CACHELINE - sizeof(unsigned int)];
	unsigned int p_tail;
	unsigned int p_head;
	char _pad[CK_MD_CACHELINE - sizeof(unsigned int) * 2];
	unsigned int size;
	unsigned int mask;
};
typedef struct ck_ring ck_ring_t;

struct ck_ring_buffer {
	void *value;
};
typedef struct ck_ring_buffer ck_ring_buffer_t;

CK_CC_INLINE static unsigned int
ck_ring_size(const struct ck_ring *ring)
{
	unsigned int c, p;

	c = ck_pr_load_uint(&ring->c_head);
	p = ck_pr_load_uint(&ring->p_tail);
	return (p - c) & ring->mask;
}

CK_CC_INLINE static unsigned int
ck_ring_capacity(const struct ck_ring *ring)
{
	return ring->size;
}

CK_CC_INLINE static void
ck_ring_init(struct ck_ring *ring, unsigned int size)
{

	ring->size = size;
	ring->mask = size - 1;
	ring->p_tail = 0;
	ring->p_head = 0;
	ring->c_head = 0;
	return;
}

/*
 * The _ck_ring_* namespace is internal only and must not used externally.
 */
CK_CC_FORCE_INLINE static bool
_ck_ring_enqueue_sp(struct ck_ring *ring,
    void *CK_CC_RESTRICT buffer,
    const void *CK_CC_RESTRICT entry,
    unsigned int ts,
    unsigned int *size)
{
	const unsigned int mask = ring->mask;
	unsigned int consumer, producer, delta;

	consumer = ck_pr_load_uint(&ring->c_head);
	producer = ring->p_tail;
	delta = producer + 1;
	if (size != NULL)
		*size = (producer - consumer) & mask;

	if (CK_CC_UNLIKELY((delta & mask) == (consumer & mask)))
		return false;

	buffer = (char *)buffer + ts * (producer & mask);
	memcpy(buffer, entry, ts);

	/*
	 * Make sure to update slot value before indicating
	 * that the slot is available for consumption.
	 */
	ck_pr_fence_store();
	ck_pr_store_uint(&ring->p_tail, delta);
	return true;
}

CK_CC_FORCE_INLINE static bool
_ck_ring_enqueue_sp_size(struct ck_ring *ring,
    void *CK_CC_RESTRICT buffer,
    const void *CK_CC_RESTRICT entry,
    unsigned int ts,
    unsigned int *size)
{
	unsigned int sz;
	bool r;

	r = _ck_ring_enqueue_sp(ring, buffer, entry, ts, &sz);
	*size = sz;
	return r;
}

CK_CC_FORCE_INLINE static bool
_ck_ring_dequeue_sc(struct ck_ring *ring,
    const void *CK_CC_RESTRICT buffer,
    void *CK_CC_RESTRICT target,
    unsigned int size)
{
	const unsigned int mask = ring->mask;
	unsigned int consumer, producer;

	consumer = ring->c_head;
	producer = ck_pr_load_uint(&ring->p_tail);

	if (CK_CC_UNLIKELY(consumer == producer))
		return false;

	/*
	 * Make sure to serialize with respect to our snapshot
	 * of the producer counter.
	 */
	ck_pr_fence_load();

	buffer = (const char *)buffer + size * (consumer & mask);
	memcpy(target, buffer, size);

	/*
	 * Make sure copy is completed with respect to consumer
	 * update.
	 */
	ck_pr_fence_store();
	ck_pr_store_uint(&ring->c_head, consumer + 1);
	return true;
}

CK_CC_FORCE_INLINE static bool
_ck_ring_enqueue_mp(struct ck_ring *ring,
    void *buffer,
    const void *entry,
    unsigned int ts,
    unsigned int *size)
{
	const unsigned int mask = ring->mask;
	unsigned int producer, consumer, delta;
	bool r = true;

	producer = ck_pr_load_uint(&ring->p_head);

	for (;;) {
		/*
		 * The snapshot of producer must be up to date with respect to
		 * consumer.
		 */
		ck_pr_fence_load();
		consumer = ck_pr_load_uint(&ring->c_head);

		delta = producer + 1;

		/*
		 * Only try to CAS if the producer is not clearly stale (not
		 * less than consumer) and the buffer is definitely not full.
		 */
		if (CK_CC_LIKELY((producer - consumer) < mask)) {
			if (ck_pr_cas_uint_value(&ring->p_head,
			    producer, delta, &producer) == true) {
				break;
			}
		} else {
			unsigned int new_producer;

			/*
			 * Slow path.  Either the buffer is full or we have a
			 * stale snapshot of p_head.  Execute a second read of
			 * p_read that must be ordered wrt the snapshot of
			 * c_head.
			 */
			ck_pr_fence_load();
			new_producer = ck_pr_load_uint(&ring->p_head);

			/*
			 * Only fail if we haven't made forward progress in
			 * production: the buffer must have been full when we
			 * read new_producer (or we wrapped around UINT_MAX
			 * during this iteration).
			 */
			if (producer == new_producer) {
				r = false;
				goto leave;
			}

			/*
			 * p_head advanced during this iteration. Try again.
			 */
			producer = new_producer;
		}
	}

	buffer = (char *)buffer + ts * (producer & mask);
	memcpy(buffer, entry, ts);

	/*
	 * Wait until all concurrent producers have completed writing
	 * their data into the ring buffer.
	 */
	while (ck_pr_load_uint(&ring->p_tail) != producer)
		ck_pr_stall();

	/*
	 * Ensure that copy is completed before updating shared producer
	 * counter.
	 */
	ck_pr_fence_store();
	ck_pr_store_uint(&ring->p_tail, delta);

leave:
	if (size != NULL)
		*size = (producer - consumer) & mask;

	return r;
}

CK_CC_FORCE_INLINE static bool
_ck_ring_enqueue_mp_size(struct ck_ring *ring,
    void *buffer,
    const void *entry,
    unsigned int ts,
    unsigned int *size)
{
	unsigned int sz;
	bool r;

	r = _ck_ring_enqueue_mp(ring, buffer, entry, ts, &sz);
	*size = sz;
	return r;
}

CK_CC_FORCE_INLINE static bool
_ck_ring_trydequeue_mc(struct ck_ring *ring,
    const void *buffer,
    void *data,
    unsigned int size)
{
	const unsigned int mask = ring->mask;
	unsigned int consumer, producer;

	consumer = ck_pr_load_uint(&ring->c_head);
	ck_pr_fence_load();
	producer = ck_pr_load_uint(&ring->p_tail);

	if (CK_CC_UNLIKELY(consumer == producer))
		return false;

	ck_pr_fence_load();

	buffer = (const char *)buffer + size * (consumer & mask);
	memcpy(data, buffer, size);

	ck_pr_fence_store_atomic();
	return ck_pr_cas_uint(&ring->c_head, consumer, consumer + 1);
}

CK_CC_FORCE_INLINE static bool
_ck_ring_dequeue_mc(struct ck_ring *ring,
    const void *buffer,
    void *data,
    unsigned int ts)
{
	const unsigned int mask = ring->mask;
	unsigned int consumer, producer;

	consumer = ck_pr_load_uint(&ring->c_head);

	do {
		const char *target;

		/*
		 * Producer counter must represent state relative to
		 * our latest consumer snapshot.
		 */
		ck_pr_fence_load();
		producer = ck_pr_load_uint(&ring->p_tail);

		if (CK_CC_UNLIKELY(consumer == producer))
			return false;

		ck_pr_fence_load();

		target = (const char *)buffer + ts * (consumer & mask);
		memcpy(data, target, ts);

		/* Serialize load with respect to head update. */
		ck_pr_fence_store_atomic();
	} while (ck_pr_cas_uint_value(&ring->c_head,
				      consumer,
				      consumer + 1,
				      &consumer) == false);

	return true;
}

/*
 * The ck_ring_*_spsc namespace is the public interface for interacting with a
 * ring buffer containing pointers. Correctness is only provided if there is up
 * to one concurrent consumer and up to one concurrent producer.
 */
CK_CC_INLINE static bool
ck_ring_enqueue_spsc_size(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    const void *entry,
    unsigned int *size)
{

	return _ck_ring_enqueue_sp_size(ring, buffer, &entry,
	    sizeof(entry), size);
}

CK_CC_INLINE static bool
ck_ring_enqueue_spsc(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    const void *entry)
{

	return _ck_ring_enqueue_sp(ring, buffer,
	    &entry, sizeof(entry), NULL);
}

CK_CC_INLINE static bool
ck_ring_dequeue_spsc(struct ck_ring *ring,
    const struct ck_ring_buffer *buffer,
    void *data)
{

	return _ck_ring_dequeue_sc(ring, buffer,
	    (void **)data, sizeof(void *));
}

/*
 * The ck_ring_*_mpmc namespace is the public interface for interacting with a
 * ring buffer containing pointers. Correctness is provided for any number of
 * producers and consumers.
 */
CK_CC_INLINE static bool
ck_ring_enqueue_mpmc(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    const void *entry)
{

	return _ck_ring_enqueue_mp(ring, buffer, &entry,
	    sizeof(entry), NULL);
}

CK_CC_INLINE static bool
ck_ring_enqueue_mpmc_size(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    const void *entry,
    unsigned int *size)
{

	return _ck_ring_enqueue_mp_size(ring, buffer, &entry,
	    sizeof(entry), size);
}

CK_CC_INLINE static bool
ck_ring_trydequeue_mpmc(struct ck_ring *ring,
    const struct ck_ring_buffer *buffer,
    void *data)
{

	return _ck_ring_trydequeue_mc(ring,
	    buffer, (void **)data, sizeof(void *));
}

CK_CC_INLINE static bool
ck_ring_dequeue_mpmc(struct ck_ring *ring,
    const struct ck_ring_buffer *buffer,
    void *data)
{

	return _ck_ring_dequeue_mc(ring, buffer, (void **)data,
	    sizeof(void *));
}

/*
 * The ck_ring_*_spmc namespace is the public interface for interacting with a
 * ring buffer containing pointers. Correctness is provided for any number of
 * consumers with up to one concurrent producer.
 */
CK_CC_INLINE static bool
ck_ring_enqueue_spmc_size(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    const void *entry,
    unsigned int *size)
{

	return _ck_ring_enqueue_sp_size(ring, buffer, &entry,
	    sizeof(entry), size);
}

CK_CC_INLINE static bool
ck_ring_enqueue_spmc(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    const void *entry)
{

	return _ck_ring_enqueue_sp(ring, buffer, &entry,
	    sizeof(entry), NULL);
}

CK_CC_INLINE static bool
ck_ring_trydequeue_spmc(struct ck_ring *ring,
    const struct ck_ring_buffer *buffer,
    void *data)
{

	return _ck_ring_trydequeue_mc(ring, buffer, (void **)data, sizeof(void *));
}

CK_CC_INLINE static bool
ck_ring_dequeue_spmc(struct ck_ring *ring,
    const struct ck_ring_buffer *buffer,
    void *data)
{

	return _ck_ring_dequeue_mc(ring, buffer, (void **)data, sizeof(void *));
}

/*
 * The ck_ring_*_mpsc namespace is the public interface for interacting with a
 * ring buffer containing pointers. Correctness is provided for any number of
 * producers with up to one concurrent consumers.
 */
CK_CC_INLINE static bool
ck_ring_enqueue_mpsc(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    const void *entry)
{

	return _ck_ring_enqueue_mp(ring, buffer, &entry,
	    sizeof(entry), NULL);
}

CK_CC_INLINE static bool
ck_ring_enqueue_mpsc_size(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    const void *entry,
    unsigned int *size)
{

	return _ck_ring_enqueue_mp_size(ring, buffer, &entry,
	    sizeof(entry), size);
}

CK_CC_INLINE static bool
ck_ring_dequeue_mpsc(struct ck_ring *ring,
    const struct ck_ring_buffer *buffer,
    void *data)
{

	return _ck_ring_dequeue_sc(ring, buffer, (void **)data,
	    sizeof(void *));
}

/*
 * CK_RING_PROTOTYPE is used to define a type-safe interface for inlining
 * values of a particular type in the ring the buffer.
 */
#define CK_RING_PROTOTYPE(name, type)			\
CK_CC_INLINE static bool				\
ck_ring_enqueue_spsc_size_##name(struct ck_ring *a,	\
    struct type *b,					\
    struct type *c,					\
    unsigned int *d)					\
{							\
							\
	return _ck_ring_enqueue_sp_size(a, b, c,	\
	    sizeof(struct type), d);			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_enqueue_spsc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_enqueue_sp(a, b, c,		\
	    sizeof(struct type), NULL);			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_dequeue_spsc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_dequeue_sc(a, b, c,		\
	    sizeof(struct type));			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_enqueue_spmc_size_##name(struct ck_ring *a,	\
    struct type *b,					\
    struct type *c,					\
    unsigned int *d)					\
{							\
							\
	return _ck_ring_enqueue_sp_size(a, b, c,	\
	    sizeof(struct type), d);			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_enqueue_spmc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_enqueue_sp(a, b, c,		\
	    sizeof(struct type), NULL);			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_trydequeue_spmc_##name(struct ck_ring *a,	\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_trydequeue_mc(a,		\
	    b, c, sizeof(struct type));			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_dequeue_spmc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_dequeue_mc(a, b, c,		\
	    sizeof(struct type));			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_enqueue_mpsc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_enqueue_mp(a, b, c,		\
	    sizeof(struct type), NULL);			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_enqueue_mpsc_size_##name(struct ck_ring *a,	\
    struct type *b,					\
    struct type *c,					\
    unsigned int *d)					\
{							\
							\
	return _ck_ring_enqueue_mp_size(a, b, c,	\
	    sizeof(struct type), d);			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_dequeue_mpsc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_dequeue_sc(a, b, c,		\
	    sizeof(struct type));			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_enqueue_mpmc_size_##name(struct ck_ring *a,	\
    struct type *b,					\
    struct type *c,					\
    unsigned int *d)					\
{							\
							\
	return _ck_ring_enqueue_mp_size(a, b, c,	\
	    sizeof(struct type), d);			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_enqueue_mpmc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_enqueue_mp(a, b, c,		\
	    sizeof(struct type), NULL);			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_trydequeue_mpmc_##name(struct ck_ring *a,	\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_trydequeue_mc(a,		\
	    b, c, sizeof(struct type));			\
}							\
							\
CK_CC_INLINE static bool				\
ck_ring_dequeue_mpmc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_dequeue_mc(a, b, c,		\
	    sizeof(struct type));			\
}

/*
 * A single producer with one concurrent consumer.
 */
#define CK_RING_ENQUEUE_SPSC(name, a, b, c)		\
	ck_ring_enqueue_spsc_##name(a, b, c)
#define CK_RING_ENQUEUE_SPSC_SIZE(name, a, b, c, d)	\
	ck_ring_enqueue_spsc_size_##name(a, b, c, d)
#define CK_RING_DEQUEUE_SPSC(name, a, b, c)		\
	ck_ring_dequeue_spsc_##name(a, b, c)

/*
 * A single producer with any number of concurrent consumers.
 */
#define CK_RING_ENQUEUE_SPMC(name, a, b, c)		\
	ck_ring_enqueue_spmc_##name(a, b, c)
#define CK_RING_ENQUEUE_SPMC_SIZE(name, a, b, c, d)	\
	ck_ring_enqueue_spmc_size_##name(a, b, c, d)
#define CK_RING_TRYDEQUEUE_SPMC(name, a, b, c)		\
	ck_ring_trydequeue_spmc_##name(a, b, c)
#define CK_RING_DEQUEUE_SPMC(name, a, b, c)		\
	ck_ring_dequeue_spmc_##name(a, b, c)

/*
 * Any number of concurrent producers with up to one
 * concurrent consumer.
 */
#define CK_RING_ENQUEUE_MPSC(name, a, b, c)		\
	ck_ring_enqueue_mpsc_##name(a, b, c)
#define CK_RING_ENQUEUE_MPSC_SIZE(name, a, b, c, d)	\
	ck_ring_enqueue_mpsc_size_##name(a, b, c, d)
#define CK_RING_DEQUEUE_MPSC(name, a, b, c)		\
	ck_ring_dequeue_mpsc_##name(a, b, c)

/*
 * Any number of concurrent producers and consumers.
 */
#define CK_RING_ENQUEUE_MPMC(name, a, b, c)		\
	ck_ring_enqueue_mpmc_##name(a, b, c)
#define CK_RING_ENQUEUE_MPMC_SIZE(name, a, b, c, d)	\
	ck_ring_enqueue_mpmc_size_##name(a, b, c, d)
#define CK_RING_TRYDEQUEUE_MPMC(name, a, b, c)		\
	ck_ring_trydequeue_mpmc_##name(a, b, c)
#define CK_RING_DEQUEUE_MPMC(name, a, b, c)		\
	ck_ring_dequeue_mpmc_##name(a, b, c)

#endif /* CK_RING_H */
