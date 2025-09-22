/* Public domain. */

#ifndef _LINUX_SEQLOCK_H
#define _LINUX_SEQLOCK_H

#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/atomic.h>
#include <linux/lockdep.h>
#include <linux/processor.h>
#include <linux/preempt.h>
#include <linux/compiler.h>

typedef struct {
	unsigned int sequence;
} seqcount_t;

static inline void
__seqcount_init(seqcount_t *s, const char *name,
    struct lock_class_key *key)
{
	s->sequence = 0;
}

static inline void
seqcount_init(seqcount_t *s)
{
	__seqcount_init(s, NULL, NULL);
}

static inline unsigned int
__read_seqcount_begin(const seqcount_t *s)
{
	unsigned int r;
	for (;;) {
		r = s->sequence;
		if ((r & 1) == 0)
			break;
		cpu_relax();
	}
	return r;
}

static inline unsigned int
read_seqcount_begin(const seqcount_t *s)
{
	unsigned int r = __read_seqcount_begin(s);
	membar_consumer();
	return r;
}

static inline int
__read_seqcount_retry(const seqcount_t *s, unsigned start)
{
	return (s->sequence != start);
}

static inline int
read_seqcount_retry(const seqcount_t *s, unsigned start)
{
	membar_consumer();
	return __read_seqcount_retry(s, start);
}

static inline void
write_seqcount_begin(seqcount_t *s)
{
	s->sequence++;
	membar_producer();
}

static inline void
write_seqcount_end(seqcount_t *s)
{
	membar_producer();
	s->sequence++;
}

static inline unsigned int
raw_read_seqcount(const seqcount_t *s)
{
	unsigned int r = s->sequence;
	membar_consumer();
	return r;
}

typedef struct {
	unsigned int seq;
	struct mutex lock;
} seqlock_t;

static inline void
seqlock_init(seqlock_t *sl, int wantipl)
{ 
	sl->seq = 0;
	mtx_init(&sl->lock, wantipl);
}

static inline void
write_seqlock(seqlock_t *sl)
{
	mtx_enter(&sl->lock);
	sl->seq++;
	membar_producer();
}

static inline void
__write_seqlock_irqsave(seqlock_t *sl)
{
	mtx_enter(&sl->lock);
	sl->seq++;
	membar_producer();
}
#define write_seqlock_irqsave(_sl, _flags) do {			\
		_flags = 0;					\
		__write_seqlock_irqsave(_sl);			\
	} while (0)

static inline void
write_sequnlock(seqlock_t *sl)
{
	membar_producer();
	sl->seq++;
	mtx_leave(&sl->lock);
}

static inline void
__write_sequnlock_irqrestore(seqlock_t *sl)
{
	membar_producer();
	sl->seq++;
	mtx_leave(&sl->lock);
}
#define write_sequnlock_irqrestore(_sl, _flags) do {		\
		(void)(_flags);					\
		__write_sequnlock_irqrestore(_sl);		\
	} while (0)

static inline unsigned int
read_seqbegin(seqlock_t *sl)
{
	return READ_ONCE(sl->seq);
}

static inline unsigned int
read_seqretry(seqlock_t *sl, unsigned int pos)
{
	return sl->seq != pos;
}

typedef struct {
	seqcount_t seq;
	struct rwlock lock;
} seqcount_mutex_t;

#define seqcount_mutex_init(s, l)	seqcount_init(&(s)->seq)

static inline unsigned int
seqprop_sequence(const seqcount_mutex_t *sm)
{
	return READ_ONCE(sm->seq.sequence);
}

#endif
