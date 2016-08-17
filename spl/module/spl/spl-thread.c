/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Thread Implementation.
\*****************************************************************************/

#include <sys/thread.h>
#include <sys/kmem.h>
#include <sys/tsd.h>

/*
 * Thread interfaces
 */
typedef struct thread_priv_s {
	unsigned long tp_magic;		/* Magic */
        int tp_name_size;		/* Name size */
        char *tp_name;			/* Name (without _thread suffix) */
	void (*tp_func)(void *);	/* Registered function */
	void *tp_args;			/* Args to be passed to function */
	size_t tp_len;			/* Len to be passed to function */
	int tp_state;			/* State to start thread at */
	pri_t tp_pri;			/* Priority to start threat at */
} thread_priv_t;

static int
thread_generic_wrapper(void *arg)
{
	thread_priv_t *tp = (thread_priv_t *)arg;
	void (*func)(void *);
	void *args;

	ASSERT(tp->tp_magic == TP_MAGIC);
	func = tp->tp_func;
	args = tp->tp_args;
	set_current_state(tp->tp_state);
	set_user_nice((kthread_t *)current, PRIO_TO_NICE(tp->tp_pri));
	kmem_free(tp->tp_name, tp->tp_name_size);
	kmem_free(tp, sizeof(thread_priv_t));

	if (func)
		func(args);

	return 0;
}

void
__thread_exit(void)
{
	tsd_exit();
	complete_and_exit(NULL, 0);
	/* Unreachable */
}
EXPORT_SYMBOL(__thread_exit);

/* thread_create() may block forever if it cannot create a thread or
 * allocate memory.  This is preferable to returning a NULL which Solaris
 * style callers likely never check for... since it can't fail. */
kthread_t *
__thread_create(caddr_t stk, size_t  stksize, thread_func_t func,
		const char *name, void *args, size_t len, proc_t *pp,
		int state, pri_t pri)
{
	thread_priv_t *tp;
	struct task_struct *tsk;
	char *p;

	/* Option pp is simply ignored */
	/* Variable stack size unsupported */
	ASSERT(stk == NULL);

	tp = kmem_alloc(sizeof(thread_priv_t), KM_PUSHPAGE);
	if (tp == NULL)
		return (NULL);

	tp->tp_magic = TP_MAGIC;
	tp->tp_name_size = strlen(name) + 1;

	tp->tp_name = kmem_alloc(tp->tp_name_size, KM_PUSHPAGE);
        if (tp->tp_name == NULL) {
		kmem_free(tp, sizeof(thread_priv_t));
		return (NULL);
	}

	strncpy(tp->tp_name, name, tp->tp_name_size);

	/* Strip trailing "_thread" from passed name which will be the func
	 * name since the exposed API has no parameter for passing a name.
	 */
	p = strstr(tp->tp_name, "_thread");
	if (p)
		p[0] = '\0';

	tp->tp_func  = func;
	tp->tp_args  = args;
	tp->tp_len   = len;
	tp->tp_state = state;
	tp->tp_pri   = pri;

	tsk = spl_kthread_create(thread_generic_wrapper, (void *)tp,
			     "%s", tp->tp_name);
	if (IS_ERR(tsk))
		return (NULL);

	wake_up_process(tsk);
	return ((kthread_t *)tsk);
}
EXPORT_SYMBOL(__thread_create);

/*
 * spl_kthread_create - Wrapper providing pre-3.13 semantics for
 * kthread_create() in which it is not killable and less likely
 * to return -ENOMEM.
 */
struct task_struct *
spl_kthread_create(int (*func)(void *), void *data, const char namefmt[], ...)
{
	struct task_struct *tsk;
	va_list args;
	char name[TASK_COMM_LEN];

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);
	do {
		tsk = kthread_create(func, data, "%s", name);
		if (IS_ERR(tsk)) {
			if (signal_pending(current)) {
				clear_thread_flag(TIF_SIGPENDING);
				continue;
			}
			if (PTR_ERR(tsk) == -ENOMEM)
				continue;
			return (NULL);
		} else
			return (tsk);
	} while (1);
}
EXPORT_SYMBOL(spl_kthread_create);
