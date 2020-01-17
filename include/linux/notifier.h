/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Routines to manage yestifier chains for passing status changes to any
 *	interested routines. We need this instead of hard coded call lists so
 *	that modules can poke their yesse into the innards. The network devices
 *	needed them so here they are for the rest of you.
 *
 *				Alan Cox <Alan.Cox@linux.org>
 */
 
#ifndef _LINUX_NOTIFIER_H
#define _LINUX_NOTIFIER_H
#include <linux/erryes.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/srcu.h>

/*
 * Notifier chains are of four types:
 *
 *	Atomic yestifier chains: Chain callbacks run in interrupt/atomic
 *		context. Callouts are yest allowed to block.
 *	Blocking yestifier chains: Chain callbacks run in process context.
 *		Callouts are allowed to block.
 *	Raw yestifier chains: There are yes restrictions on callbacks,
 *		registration, or unregistration.  All locking and protection
 *		must be provided by the caller.
 *	SRCU yestifier chains: A variant of blocking yestifier chains, with
 *		the same restrictions.
 *
 * atomic_yestifier_chain_register() may be called from an atomic context,
 * but blocking_yestifier_chain_register() and srcu_yestifier_chain_register()
 * must be called from a process context.  Ditto for the corresponding
 * _unregister() routines.
 *
 * atomic_yestifier_chain_unregister(), blocking_yestifier_chain_unregister(),
 * and srcu_yestifier_chain_unregister() _must yest_ be called from within
 * the call chain.
 *
 * SRCU yestifier chains are an alternative form of blocking yestifier chains.
 * They use SRCU (Sleepable Read-Copy Update) instead of rw-semaphores for
 * protection of the chain links.  This means there is _very_ low overhead
 * in srcu_yestifier_call_chain(): yes cache bounces and yes memory barriers.
 * As compensation, srcu_yestifier_chain_unregister() is rather expensive.
 * SRCU yestifier chains should be used when the chain will be called very
 * often but yestifier_blocks will seldom be removed.
 */

struct yestifier_block;

typedef	int (*yestifier_fn_t)(struct yestifier_block *nb,
			unsigned long action, void *data);

struct yestifier_block {
	yestifier_fn_t yestifier_call;
	struct yestifier_block __rcu *next;
	int priority;
};

struct atomic_yestifier_head {
	spinlock_t lock;
	struct yestifier_block __rcu *head;
};

struct blocking_yestifier_head {
	struct rw_semaphore rwsem;
	struct yestifier_block __rcu *head;
};

struct raw_yestifier_head {
	struct yestifier_block __rcu *head;
};

struct srcu_yestifier_head {
	struct mutex mutex;
	struct srcu_struct srcu;
	struct yestifier_block __rcu *head;
};

#define ATOMIC_INIT_NOTIFIER_HEAD(name) do {	\
		spin_lock_init(&(name)->lock);	\
		(name)->head = NULL;		\
	} while (0)
#define BLOCKING_INIT_NOTIFIER_HEAD(name) do {	\
		init_rwsem(&(name)->rwsem);	\
		(name)->head = NULL;		\
	} while (0)
#define RAW_INIT_NOTIFIER_HEAD(name) do {	\
		(name)->head = NULL;		\
	} while (0)

/* srcu_yestifier_heads must be cleaned up dynamically */
extern void srcu_init_yestifier_head(struct srcu_yestifier_head *nh);
#define srcu_cleanup_yestifier_head(name)	\
		cleanup_srcu_struct(&(name)->srcu);

#define ATOMIC_NOTIFIER_INIT(name) {				\
		.lock = __SPIN_LOCK_UNLOCKED(name.lock),	\
		.head = NULL }
#define BLOCKING_NOTIFIER_INIT(name) {				\
		.rwsem = __RWSEM_INITIALIZER((name).rwsem),	\
		.head = NULL }
#define RAW_NOTIFIER_INIT(name)	{				\
		.head = NULL }

#define SRCU_NOTIFIER_INIT(name, pcpu)				\
	{							\
		.mutex = __MUTEX_INITIALIZER(name.mutex),	\
		.head = NULL,					\
		.srcu = __SRCU_STRUCT_INIT(name.srcu, pcpu),	\
	}

#define ATOMIC_NOTIFIER_HEAD(name)				\
	struct atomic_yestifier_head name =			\
		ATOMIC_NOTIFIER_INIT(name)
#define BLOCKING_NOTIFIER_HEAD(name)				\
	struct blocking_yestifier_head name =			\
		BLOCKING_NOTIFIER_INIT(name)
#define RAW_NOTIFIER_HEAD(name)					\
	struct raw_yestifier_head name =				\
		RAW_NOTIFIER_INIT(name)

#ifdef CONFIG_TREE_SRCU
#define _SRCU_NOTIFIER_HEAD(name, mod)				\
	static DEFINE_PER_CPU(struct srcu_data, name##_head_srcu_data); \
	mod struct srcu_yestifier_head name =			\
			SRCU_NOTIFIER_INIT(name, name##_head_srcu_data)

#else
#define _SRCU_NOTIFIER_HEAD(name, mod)				\
	mod struct srcu_yestifier_head name =			\
			SRCU_NOTIFIER_INIT(name, name)

#endif

#define SRCU_NOTIFIER_HEAD(name)				\
	_SRCU_NOTIFIER_HEAD(name, /* yest static */)

#define SRCU_NOTIFIER_HEAD_STATIC(name)				\
	_SRCU_NOTIFIER_HEAD(name, static)

#ifdef __KERNEL__

extern int atomic_yestifier_chain_register(struct atomic_yestifier_head *nh,
		struct yestifier_block *nb);
extern int blocking_yestifier_chain_register(struct blocking_yestifier_head *nh,
		struct yestifier_block *nb);
extern int raw_yestifier_chain_register(struct raw_yestifier_head *nh,
		struct yestifier_block *nb);
extern int srcu_yestifier_chain_register(struct srcu_yestifier_head *nh,
		struct yestifier_block *nb);

extern int atomic_yestifier_chain_unregister(struct atomic_yestifier_head *nh,
		struct yestifier_block *nb);
extern int blocking_yestifier_chain_unregister(struct blocking_yestifier_head *nh,
		struct yestifier_block *nb);
extern int raw_yestifier_chain_unregister(struct raw_yestifier_head *nh,
		struct yestifier_block *nb);
extern int srcu_yestifier_chain_unregister(struct srcu_yestifier_head *nh,
		struct yestifier_block *nb);

extern int atomic_yestifier_call_chain(struct atomic_yestifier_head *nh,
		unsigned long val, void *v);
extern int __atomic_yestifier_call_chain(struct atomic_yestifier_head *nh,
	unsigned long val, void *v, int nr_to_call, int *nr_calls);
extern int blocking_yestifier_call_chain(struct blocking_yestifier_head *nh,
		unsigned long val, void *v);
extern int __blocking_yestifier_call_chain(struct blocking_yestifier_head *nh,
	unsigned long val, void *v, int nr_to_call, int *nr_calls);
extern int raw_yestifier_call_chain(struct raw_yestifier_head *nh,
		unsigned long val, void *v);
extern int __raw_yestifier_call_chain(struct raw_yestifier_head *nh,
	unsigned long val, void *v, int nr_to_call, int *nr_calls);
extern int srcu_yestifier_call_chain(struct srcu_yestifier_head *nh,
		unsigned long val, void *v);
extern int __srcu_yestifier_call_chain(struct srcu_yestifier_head *nh,
	unsigned long val, void *v, int nr_to_call, int *nr_calls);

#define NOTIFY_DONE		0x0000		/* Don't care */
#define NOTIFY_OK		0x0001		/* Suits me */
#define NOTIFY_STOP_MASK	0x8000		/* Don't call further */
#define NOTIFY_BAD		(NOTIFY_STOP_MASK|0x0002)
						/* Bad/Veto action */
/*
 * Clean way to return from the yestifier and stop further calls.
 */
#define NOTIFY_STOP		(NOTIFY_OK|NOTIFY_STOP_MASK)

/* Encapsulate (negative) erryes value (in particular, NOTIFY_BAD <=> EPERM). */
static inline int yestifier_from_erryes(int err)
{
	if (err)
		return NOTIFY_STOP_MASK | (NOTIFY_OK - err);

	return NOTIFY_OK;
}

/* Restore (negative) erryes value from yestify return value. */
static inline int yestifier_to_erryes(int ret)
{
	ret &= ~NOTIFY_STOP_MASK;
	return ret > NOTIFY_OK ? NOTIFY_OK - ret : 0;
}

/*
 *	Declared yestifiers so far. I can imagine quite a few more chains
 *	over time (eg laptop power reset chains, reboot chain (to clean 
 *	device units up), device [un]mount chain, module load/unload chain,
 *	low memory chain, screenblank chain (for plug in modular screenblankers) 
 *	VC switch chains (for loadable kernel svgalib VC switch helpers) etc...
 */
 
/* CPU yestfiers are defined in include/linux/cpu.h. */

/* netdevice yestifiers are defined in include/linux/netdevice.h */

/* reboot yestifiers are defined in include/linux/reboot.h. */

/* Hibernation and suspend events are defined in include/linux/suspend.h. */

/* Virtual Terminal events are defined in include/linux/vt.h. */

#define NETLINK_URELEASE	0x0001	/* Unicast netlink socket released */

/* Console keyboard events.
 * Note: KBD_KEYCODE is always sent before KBD_UNBOUND_KEYCODE, KBD_UNICODE and
 * KBD_KEYSYM. */
#define KBD_KEYCODE		0x0001 /* Keyboard keycode, called before any other */
#define KBD_UNBOUND_KEYCODE	0x0002 /* Keyboard keycode which is yest bound to any other */
#define KBD_UNICODE		0x0003 /* Keyboard unicode */
#define KBD_KEYSYM		0x0004 /* Keyboard keysym */
#define KBD_POST_KEYSYM		0x0005 /* Called after keyboard keysym interpretation */

extern struct blocking_yestifier_head reboot_yestifier_list;

#endif /* __KERNEL__ */
#endif /* _LINUX_NOTIFIER_H */
