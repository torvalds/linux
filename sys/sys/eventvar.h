/*	$OpenBSD: eventvar.h,v 1.17 2022/07/09 12:48:21 visa Exp $	*/

/*-
 * Copyright (c) 1999,2000 Jonathan Lemon <jlemon@FreeBSD.org>
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
 *
 *	$FreeBSD: src/sys/sys/eventvar.h,v 1.3 2000/05/26 02:06:54 jake Exp $
 */

#ifndef _SYS_EVENTVAR_H_
#define _SYS_EVENTVAR_H_

#include <sys/mutex.h>
#include <sys/refcnt.h>
#include <sys/task.h>

#define KQ_NEVENTS	8		/* minimize copy{in,out} calls */
#define KQEXTENT	256		/* linear growth by this amount */

/*
 * Locking:
 *	I	immutable after creation
 *	L	kqueue_klist_lock
 *	a	atomic operations
 *	q	kq_lock
 */
struct kqueue {
	struct		mutex kq_lock;		/* lock for queue access */
	TAILQ_HEAD(, knote) kq_head;		/* [q] list of pending event */
	int		kq_count;		/* [q] # of pending events */
	struct		refcnt kq_refcnt;	/* [a] # of references */
	struct		klist kq_klist;		/* [L] knotes of other kqs */
	struct		filedesc *kq_fdp;	/* [I] fd table of this kq */

	LIST_ENTRY(kqueue) kq_next;

	u_int		kq_nknotes;		/* [q] # of registered knotes */

	int		kq_knlistsize;		/* [q] size of kq_knlist */
	struct		knlist *kq_knlist;	/* [q] list of
						 *     attached knotes */
	u_long		kq_knhashmask;		/* [q] size of kq_knhash */
	struct		knlist *kq_knhash;	/* [q] hash table for
						 *     attached knotes */
	struct		task kq_task;		/* deferring of activation */

	int		kq_state;		/* [q] */
#define KQ_SLEEP	0x02
#define KQ_DYING	0x04
#define KQ_TASK		0x08
};

#endif /* !_SYS_EVENTVAR_H_ */
