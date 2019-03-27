/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith <msmith@freebsd.org>
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
 * $FreeBSD$
 */

#ifndef _SYS_EVENTHANDLER_H_
#define _SYS_EVENTHANDLER_H_

#include <sys/lock.h>
#include <sys/ktr.h>
#include <sys/mutex.h>
#include <sys/queue.h>

struct eventhandler_entry {
	TAILQ_ENTRY(eventhandler_entry)	ee_link;
	int				ee_priority;
#define	EHE_DEAD_PRIORITY	(-1)
	void				*ee_arg;
};

#ifdef VIMAGE
struct eventhandler_entry_vimage {
	void	(* func)(void);		/* Original function registered. */
	void	*ee_arg;		/* Original argument registered. */
	void	*sparep[2];
};
#endif

struct eventhandler_list {
	char				*el_name;
	int				el_flags;	/* Unused. */
	u_int				el_runcount;
	struct mtx			el_lock;
	TAILQ_ENTRY(eventhandler_list)	el_link;
	TAILQ_HEAD(,eventhandler_entry)	el_entries;
};

typedef struct eventhandler_entry	*eventhandler_tag;

#define	EHL_LOCK(p)		mtx_lock(&(p)->el_lock)
#define	EHL_UNLOCK(p)		mtx_unlock(&(p)->el_lock)
#define	EHL_LOCK_ASSERT(p, x)	mtx_assert(&(p)->el_lock, x)

/*
 * Macro to invoke the handlers for a given event.
 */
#define _EVENTHANDLER_INVOKE(name, list, ...) do {			\
	struct eventhandler_entry *_ep;					\
	struct eventhandler_entry_ ## name *_t;				\
									\
	EHL_LOCK_ASSERT((list), MA_OWNED);				\
	(list)->el_runcount++;						\
	KASSERT((list)->el_runcount > 0,				\
	    ("eventhandler_invoke: runcount overflow"));		\
	CTR0(KTR_EVH, "eventhandler_invoke(\"" __STRING(name) "\")");	\
	TAILQ_FOREACH(_ep, &((list)->el_entries), ee_link) {		\
		if (_ep->ee_priority != EHE_DEAD_PRIORITY) {		\
			EHL_UNLOCK((list));				\
			_t = (struct eventhandler_entry_ ## name *)_ep;	\
			CTR1(KTR_EVH, "eventhandler_invoke: executing %p", \
 			    (void *)_t->eh_func);			\
			_t->eh_func(_ep->ee_arg , ## __VA_ARGS__);	\
			EHL_LOCK((list));				\
		}							\
	}								\
	KASSERT((list)->el_runcount > 0,				\
	    ("eventhandler_invoke: runcount underflow"));		\
	(list)->el_runcount--;						\
	if ((list)->el_runcount == 0)					\
		eventhandler_prune_list(list);				\
	EHL_UNLOCK((list));						\
} while (0)

/*
 * You can optionally use the EVENTHANDLER_LIST and EVENTHANDLER_DIRECT macros
 * to pre-define a symbol for the eventhandler list. This symbol can be used by
 * EVENTHANDLER_DIRECT_INVOKE, which has the advantage of not needing to do a
 * locked search of the global list of eventhandler lists. At least
 * EVENTHANDLER_LIST_DEFINE must be be used for EVENTHANDLER_DIRECT_INVOKE to
 * work. EVENTHANDLER_LIST_DECLARE is only needed if the call to
 * EVENTHANDLER_DIRECT_INVOKE is in a different compilation unit from
 * EVENTHANDLER_LIST_DEFINE. If the events are even relatively high frequency
 * it is suggested that you directly define a list for them.
 */
#define	EVENTHANDLER_LIST_DECLARE(name)					\
extern struct eventhandler_list *_eventhandler_list_ ## name		\

#define	EVENTHANDLER_LIST_DEFINE(name)					\
struct eventhandler_list *_eventhandler_list_ ## name ;			\
static void _ehl_init_ ## name (void * ctx __unused)			\
{									\
	_eventhandler_list_ ## name = eventhandler_create_list(#name);	\
}									\
SYSINIT(name ## _ehl_init, SI_SUB_EVENTHANDLER, SI_ORDER_ANY,		\
	    _ehl_init_ ## name, NULL);					\
	struct __hack

#define	EVENTHANDLER_DIRECT_INVOKE(name, ...) do {			\
	struct eventhandler_list *_el;					\
									\
	_el = _eventhandler_list_ ## name ;				\
	if (!TAILQ_EMPTY(&_el->el_entries)) {				\
		EHL_LOCK(_el);						\
		_EVENTHANDLER_INVOKE(name, _el , ## __VA_ARGS__);	\
	}								\
} while (0)

/*
 * Event handlers need to be declared, but do not need to be defined. The
 * declaration must be in scope wherever the handler is to be invoked.
 */
#define EVENTHANDLER_DECLARE(name, type)				\
struct eventhandler_entry_ ## name 					\
{									\
	struct eventhandler_entry	ee;				\
	type				eh_func;			\
};									\
struct __hack

#define EVENTHANDLER_DEFINE(name, func, arg, priority)			\
	static eventhandler_tag name ## _tag;				\
	static void name ## _evh_init(void *ctx)			\
	{								\
		name ## _tag = EVENTHANDLER_REGISTER(name, func, ctx,	\
		    priority);						\
	}								\
	SYSINIT(name ## _evh_init, SI_SUB_CONFIGURE, SI_ORDER_ANY,	\
	    name ## _evh_init, arg);					\
	struct __hack

#define EVENTHANDLER_INVOKE(name, ...)					\
do {									\
	struct eventhandler_list *_el;					\
									\
	if ((_el = eventhandler_find_list(#name)) != NULL) 		\
		_EVENTHANDLER_INVOKE(name, _el , ## __VA_ARGS__);	\
} while (0)

#define EVENTHANDLER_REGISTER(name, func, arg, priority)		\
	eventhandler_register(NULL, #name, func, arg, priority)

#define EVENTHANDLER_DEREGISTER(name, tag) 				\
do {									\
	struct eventhandler_list *_el;					\
									\
	if ((_el = eventhandler_find_list(#name)) != NULL)		\
		eventhandler_deregister(_el, tag);			\
} while(0)

#define EVENTHANDLER_DEREGISTER_NOWAIT(name, tag)			\
do {									\
	struct eventhandler_list *_el;					\
									\
	if ((_el = eventhandler_find_list(#name)) != NULL)		\
		eventhandler_deregister_nowait(_el, tag);		\
} while(0)

eventhandler_tag eventhandler_register(struct eventhandler_list *list, 
	    const char *name, void *func, void *arg, int priority);
void	eventhandler_deregister(struct eventhandler_list *list,
	    eventhandler_tag tag);
void	eventhandler_deregister_nowait(struct eventhandler_list *list,
	    eventhandler_tag tag);
struct eventhandler_list *eventhandler_find_list(const char *name);
void	eventhandler_prune_list(struct eventhandler_list *list);
struct eventhandler_list *eventhandler_create_list(const char *name);

#ifdef VIMAGE
typedef	void (*vimage_iterator_func_t)(void *, ...);

eventhandler_tag vimage_eventhandler_register(struct eventhandler_list *list,
	    const char *name, void *func, void *arg, int priority,
	    vimage_iterator_func_t);
#endif

/*
 * Standard system event queues.
 */

/* Generic priority levels */
#define	EVENTHANDLER_PRI_FIRST	0
#define	EVENTHANDLER_PRI_ANY	10000
#define	EVENTHANDLER_PRI_LAST	20000

/* Shutdown events */
typedef void (*shutdown_fn)(void *, int);

#define	SHUTDOWN_PRI_FIRST	EVENTHANDLER_PRI_FIRST
#define	SHUTDOWN_PRI_DEFAULT	EVENTHANDLER_PRI_ANY
#define	SHUTDOWN_PRI_LAST	EVENTHANDLER_PRI_LAST

EVENTHANDLER_DECLARE(shutdown_pre_sync, shutdown_fn);	/* before fs sync */
EVENTHANDLER_DECLARE(shutdown_post_sync, shutdown_fn);	/* after fs sync */
EVENTHANDLER_DECLARE(shutdown_final, shutdown_fn);

/* Power state change events */
typedef void (*power_change_fn)(void *);
EVENTHANDLER_DECLARE(power_resume, power_change_fn);
EVENTHANDLER_DECLARE(power_suspend, power_change_fn);
EVENTHANDLER_DECLARE(power_suspend_early, power_change_fn);

/* Low memory event */
typedef void (*vm_lowmem_handler_t)(void *, int);
#define	LOWMEM_PRI_DEFAULT	EVENTHANDLER_PRI_FIRST
EVENTHANDLER_DECLARE(vm_lowmem, vm_lowmem_handler_t);

/* Root mounted event */
typedef void (*mountroot_handler_t)(void *);
EVENTHANDLER_DECLARE(mountroot, mountroot_handler_t);

/* File system mount events */
struct mount;
struct vnode;
struct thread;
typedef void (*vfs_mounted_notify_fn)(void *, struct mount *, struct vnode *,
    struct thread *);
typedef void (*vfs_unmounted_notify_fn)(void *, struct mount *,
    struct thread *);
EVENTHANDLER_DECLARE(vfs_mounted, vfs_mounted_notify_fn);
EVENTHANDLER_DECLARE(vfs_unmounted, vfs_unmounted_notify_fn);

/*
 * Process events
 * process_fork and exit handlers are called without Giant.
 * exec handlers are called with Giant, but that is by accident.
 */
struct proc;
struct image_params;

typedef void (*exitlist_fn)(void *, struct proc *);
typedef void (*forklist_fn)(void *, struct proc *, struct proc *, int);
typedef void (*execlist_fn)(void *, struct proc *, struct image_params *);
typedef void (*proc_ctor_fn)(void *, struct proc *);
typedef void (*proc_dtor_fn)(void *, struct proc *);
typedef void (*proc_init_fn)(void *, struct proc *);
typedef void (*proc_fini_fn)(void *, struct proc *);
EVENTHANDLER_DECLARE(process_ctor, proc_ctor_fn);
EVENTHANDLER_DECLARE(process_dtor, proc_dtor_fn);
EVENTHANDLER_DECLARE(process_init, proc_init_fn);
EVENTHANDLER_DECLARE(process_fini, proc_fini_fn);
EVENTHANDLER_DECLARE(process_exit, exitlist_fn);
EVENTHANDLER_DECLARE(process_fork, forklist_fn);
EVENTHANDLER_DECLARE(process_exec, execlist_fn);

/*
 * application dump event
 */
typedef void (*app_coredump_start_fn)(void *, struct thread *, char *name);
typedef void (*app_coredump_progress_fn)(void *, struct thread *td, int byte_count);
typedef void (*app_coredump_finish_fn)(void *, struct thread *td);
typedef void (*app_coredump_error_fn)(void *, struct thread *td, char *msg, ...);

EVENTHANDLER_DECLARE(app_coredump_start, app_coredump_start_fn);
EVENTHANDLER_DECLARE(app_coredump_progress, app_coredump_progress_fn);
EVENTHANDLER_DECLARE(app_coredump_finish, app_coredump_finish_fn);
EVENTHANDLER_DECLARE(app_coredump_error, app_coredump_error_fn);

typedef void (*thread_ctor_fn)(void *, struct thread *);
typedef void (*thread_dtor_fn)(void *, struct thread *);
typedef void (*thread_fini_fn)(void *, struct thread *);
typedef void (*thread_init_fn)(void *, struct thread *);
EVENTHANDLER_DECLARE(thread_ctor, thread_ctor_fn);
EVENTHANDLER_DECLARE(thread_dtor, thread_dtor_fn);
EVENTHANDLER_DECLARE(thread_init, thread_init_fn);
EVENTHANDLER_DECLARE(thread_fini, thread_fini_fn);

typedef void (*uma_zone_chfn)(void *);
EVENTHANDLER_DECLARE(nmbclusters_change, uma_zone_chfn);
EVENTHANDLER_DECLARE(nmbufs_change, uma_zone_chfn);
EVENTHANDLER_DECLARE(maxsockets_change, uma_zone_chfn);

/* Kernel linker file load and unload events */
struct linker_file;
typedef void (*kld_load_fn)(void *, struct linker_file *);
typedef void (*kld_unload_fn)(void *, const char *, caddr_t, size_t);
typedef void (*kld_unload_try_fn)(void *, struct linker_file *, int *);
EVENTHANDLER_DECLARE(kld_load, kld_load_fn);
EVENTHANDLER_DECLARE(kld_unload, kld_unload_fn);
EVENTHANDLER_DECLARE(kld_unload_try, kld_unload_try_fn);

/* Generic graphics framebuffer interface */
struct fb_info;
typedef void (*register_framebuffer_fn)(void *, struct fb_info *);
typedef void (*unregister_framebuffer_fn)(void *, struct fb_info *);
EVENTHANDLER_DECLARE(register_framebuffer, register_framebuffer_fn);
EVENTHANDLER_DECLARE(unregister_framebuffer, unregister_framebuffer_fn);

/* Veto ada attachment */
struct cam_path;
struct ata_params;
typedef void (*ada_probe_veto_fn)(void *, struct cam_path *,
    struct ata_params *, int *);
EVENTHANDLER_DECLARE(ada_probe_veto, ada_probe_veto_fn);

/* Swap device events */
struct swdevt;
typedef void (*swapon_fn)(void *, struct swdevt *);
typedef void (*swapoff_fn)(void *, struct swdevt *);
EVENTHANDLER_DECLARE(swapon, swapon_fn);
EVENTHANDLER_DECLARE(swapoff, swapoff_fn);

/* newbus device events */
enum evhdev_detach {
	EVHDEV_DETACH_BEGIN,    /* Before detach() is called */
	EVHDEV_DETACH_COMPLETE, /* After detach() returns 0 */
	EVHDEV_DETACH_FAILED    /* After detach() returns err */
};
typedef void (*device_attach_fn)(void *, device_t);
typedef void (*device_detach_fn)(void *, device_t, enum evhdev_detach);
EVENTHANDLER_DECLARE(device_attach, device_attach_fn);
EVENTHANDLER_DECLARE(device_detach, device_detach_fn);

#endif /* _SYS_EVENTHANDLER_H_ */
