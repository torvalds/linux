/*	$OpenBSD: rthread.h,v 1.64 2019/02/13 13:22:14 mpi Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Private data structures that back up the typedefs in pthread.h.
 * Since only the thread library cares about their size or arrangement,
 * it should be possible to switch libraries without relinking.
 *
 * Do not reorder _atomic_lock_t and sem_t variables in the structs.
 * This is due to alignment requirements of certain arches like hppa.
 * The current requirement is 16 bytes.
 *
 * THE MACHINE DEPENDENT CERROR CODE HAS HARD CODED OFFSETS INTO PTHREAD_T!
 */

#ifndef _RTHREAD_H_
#define _RTHREAD_H_

#include <semaphore.h>
#include "thread_private.h"

#ifdef __LP64__
#define RTHREAD_STACK_SIZE_DEF (512 * 1024)
#else
#define RTHREAD_STACK_SIZE_DEF (256 * 1024)
#endif

struct stack {
	SLIST_ENTRY(stack)	link;	/* link for free default stacks */
	void	*sp;			/* machine stack pointer */
	void	*base;			/* bottom of allocated area */
	size_t	guardsize;		/* size of PROT_NONE zone or */
					/* ==1 if application alloced */
	size_t	len;			/* total size of allocated stack */
};

#define	PTHREAD_MIN_PRIORITY	0
#define	PTHREAD_MAX_PRIORITY	31


struct pthread_rwlockattr {
	int pshared;
};

struct pthread_barrier {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int threshold;
	int in;
	int out;
	int generation;
};

struct pthread_barrierattr {
	int pshared;
};

struct pthread_spinlock {
	_atomic_lock_t lock;
	pthread_t owner;
};


#define	ROUND_TO_PAGE(size) \
	(((size) + (_thread_pagesize - 1)) & ~(_thread_pagesize - 1))

__BEGIN_HIDDEN_DECLS
int	_sem_wait(sem_t, int, const struct timespec *, int *);
int	_sem_post(sem_t);

void	_rthread_init(void);
struct stack *_rthread_alloc_stack(pthread_t);
void	_rthread_free_stack(struct stack *);
#ifndef NO_PIC
void	_rthread_dl_lock(int what);
#endif

extern int _threads_ready;
extern size_t _thread_pagesize;
extern LIST_HEAD(listhead, pthread) _thread_list;
extern _atomic_lock_t _thread_lock;
extern struct pthread_attr _rthread_attr_default;
__END_HIDDEN_DECLS

void	_thread_dump_info(void);

#define REDIRECT_SYSCALL(x)		typeof(x) x asm("_thread_sys_"#x)

#endif /* _RTHREAD_H_ */
