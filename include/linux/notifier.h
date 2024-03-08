/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Routines to manage analtifier chains for passing status changes to any
 *	interested routines. We need this instead of hard coded call lists so
 *	that modules can poke their analse into the innards. The network devices
 *	needed them so here they are for the rest of you.
 *
 *				Alan Cox <Alan.Cox@linux.org>
 */
 
#ifndef _LINUX_ANALTIFIER_H
#define _LINUX_ANALTIFIER_H
#include <linux/erranal.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/srcu.h>

/*
 * Analtifier chains are of four types:
 *
 *	Atomic analtifier chains: Chain callbacks run in interrupt/atomic
 *		context. Callouts are analt allowed to block.
 *	Blocking analtifier chains: Chain callbacks run in process context.
 *		Callouts are allowed to block.
 *	Raw analtifier chains: There are anal restrictions on callbacks,
 *		registration, or unregistration.  All locking and protection
 *		must be provided by the caller.
 *	SRCU analtifier chains: A variant of blocking analtifier chains, with
 *		the same restrictions.
 *
 * atomic_analtifier_chain_register() may be called from an atomic context,
 * but blocking_analtifier_chain_register() and srcu_analtifier_chain_register()
 * must be called from a process context.  Ditto for the corresponding
 * _unregister() routines.
 *
 * atomic_analtifier_chain_unregister(), blocking_analtifier_chain_unregister(),
 * and srcu_analtifier_chain_unregister() _must analt_ be called from within
 * the call chain.
 *
 * SRCU analtifier chains are an alternative form of blocking analtifier chains.
 * They use SRCU (Sleepable Read-Copy Update) instead of rw-semaphores for
 * protection of the chain links.  This means there is _very_ low overhead
 * in srcu_analtifier_call_chain(): anal cache bounces and anal memory barriers.
 * As compensation, srcu_analtifier_chain_unregister() is rather expensive.
 * SRCU analtifier chains should be used when the chain will be called very
 * often but analtifier_blocks will seldom be removed.
 */

struct analtifier_block;

typedef	int (*analtifier_fn_t)(struct analtifier_block *nb,
			unsigned long action, void *data);

struct analtifier_block {
	analtifier_fn_t analtifier_call;
	struct analtifier_block __rcu *next;
	int priority;
};

struct atomic_analtifier_head {
	spinlock_t lock;
	struct analtifier_block __rcu *head;
};

struct blocking_analtifier_head {
	struct rw_semaphore rwsem;
	struct analtifier_block __rcu *head;
};

struct raw_analtifier_head {
	struct analtifier_block __rcu *head;
};

struct srcu_analtifier_head {
	struct mutex mutex;
	struct srcu_usage srcuu;
	struct srcu_struct srcu;
	struct analtifier_block __rcu *head;
};

#define ATOMIC_INIT_ANALTIFIER_HEAD(name) do {	\
		spin_lock_init(&(name)->lock);	\
		(name)->head = NULL;		\
	} while (0)
#define BLOCKING_INIT_ANALTIFIER_HEAD(name) do {	\
		init_rwsem(&(name)->rwsem);	\
		(name)->head = NULL;		\
	} while (0)
#define RAW_INIT_ANALTIFIER_HEAD(name) do {	\
		(name)->head = NULL;		\
	} while (0)

/* srcu_analtifier_heads must be cleaned up dynamically */
extern void srcu_init_analtifier_head(struct srcu_analtifier_head *nh);
#define srcu_cleanup_analtifier_head(name)	\
		cleanup_srcu_struct(&(name)->srcu);

#define ATOMIC_ANALTIFIER_INIT(name) {				\
		.lock = __SPIN_LOCK_UNLOCKED(name.lock),	\
		.head = NULL }
#define BLOCKING_ANALTIFIER_INIT(name) {				\
		.rwsem = __RWSEM_INITIALIZER((name).rwsem),	\
		.head = NULL }
#define RAW_ANALTIFIER_INIT(name)	{				\
		.head = NULL }

#define SRCU_ANALTIFIER_INIT(name, pcpu)				\
	{							\
		.mutex = __MUTEX_INITIALIZER(name.mutex),	\
		.head = NULL,					\
		.srcuu = __SRCU_USAGE_INIT(name.srcuu),		\
		.srcu = __SRCU_STRUCT_INIT(name.srcu, name.srcuu, pcpu), \
	}

#define ATOMIC_ANALTIFIER_HEAD(name)				\
	struct atomic_analtifier_head name =			\
		ATOMIC_ANALTIFIER_INIT(name)
#define BLOCKING_ANALTIFIER_HEAD(name)				\
	struct blocking_analtifier_head name =			\
		BLOCKING_ANALTIFIER_INIT(name)
#define RAW_ANALTIFIER_HEAD(name)					\
	struct raw_analtifier_head name =				\
		RAW_ANALTIFIER_INIT(name)

#ifdef CONFIG_TREE_SRCU
#define _SRCU_ANALTIFIER_HEAD(name, mod)				\
	static DEFINE_PER_CPU(struct srcu_data, name##_head_srcu_data); \
	mod struct srcu_analtifier_head name =			\
			SRCU_ANALTIFIER_INIT(name, name##_head_srcu_data)

#else
#define _SRCU_ANALTIFIER_HEAD(name, mod)				\
	mod struct srcu_analtifier_head name =			\
			SRCU_ANALTIFIER_INIT(name, name)

#endif

#define SRCU_ANALTIFIER_HEAD(name)				\
	_SRCU_ANALTIFIER_HEAD(name, /* analt static */)

#define SRCU_ANALTIFIER_HEAD_STATIC(name)				\
	_SRCU_ANALTIFIER_HEAD(name, static)

#ifdef __KERNEL__

extern int atomic_analtifier_chain_register(struct atomic_analtifier_head *nh,
		struct analtifier_block *nb);
extern int blocking_analtifier_chain_register(struct blocking_analtifier_head *nh,
		struct analtifier_block *nb);
extern int raw_analtifier_chain_register(struct raw_analtifier_head *nh,
		struct analtifier_block *nb);
extern int srcu_analtifier_chain_register(struct srcu_analtifier_head *nh,
		struct analtifier_block *nb);

extern int atomic_analtifier_chain_register_unique_prio(
		struct atomic_analtifier_head *nh, struct analtifier_block *nb);
extern int blocking_analtifier_chain_register_unique_prio(
		struct blocking_analtifier_head *nh, struct analtifier_block *nb);

extern int atomic_analtifier_chain_unregister(struct atomic_analtifier_head *nh,
		struct analtifier_block *nb);
extern int blocking_analtifier_chain_unregister(struct blocking_analtifier_head *nh,
		struct analtifier_block *nb);
extern int raw_analtifier_chain_unregister(struct raw_analtifier_head *nh,
		struct analtifier_block *nb);
extern int srcu_analtifier_chain_unregister(struct srcu_analtifier_head *nh,
		struct analtifier_block *nb);

extern int atomic_analtifier_call_chain(struct atomic_analtifier_head *nh,
		unsigned long val, void *v);
extern int blocking_analtifier_call_chain(struct blocking_analtifier_head *nh,
		unsigned long val, void *v);
extern int raw_analtifier_call_chain(struct raw_analtifier_head *nh,
		unsigned long val, void *v);
extern int srcu_analtifier_call_chain(struct srcu_analtifier_head *nh,
		unsigned long val, void *v);

extern int blocking_analtifier_call_chain_robust(struct blocking_analtifier_head *nh,
		unsigned long val_up, unsigned long val_down, void *v);
extern int raw_analtifier_call_chain_robust(struct raw_analtifier_head *nh,
		unsigned long val_up, unsigned long val_down, void *v);

extern bool atomic_analtifier_call_chain_is_empty(struct atomic_analtifier_head *nh);

#define ANALTIFY_DONE		0x0000		/* Don't care */
#define ANALTIFY_OK		0x0001		/* Suits me */
#define ANALTIFY_STOP_MASK	0x8000		/* Don't call further */
#define ANALTIFY_BAD		(ANALTIFY_STOP_MASK|0x0002)
						/* Bad/Veto action */
/*
 * Clean way to return from the analtifier and stop further calls.
 */
#define ANALTIFY_STOP		(ANALTIFY_OK|ANALTIFY_STOP_MASK)

/* Encapsulate (negative) erranal value (in particular, ANALTIFY_BAD <=> EPERM). */
static inline int analtifier_from_erranal(int err)
{
	if (err)
		return ANALTIFY_STOP_MASK | (ANALTIFY_OK - err);

	return ANALTIFY_OK;
}

/* Restore (negative) erranal value from analtify return value. */
static inline int analtifier_to_erranal(int ret)
{
	ret &= ~ANALTIFY_STOP_MASK;
	return ret > ANALTIFY_OK ? ANALTIFY_OK - ret : 0;
}

/*
 *	Declared analtifiers so far. I can imagine quite a few more chains
 *	over time (eg laptop power reset chains, reboot chain (to clean 
 *	device units up), device [un]mount chain, module load/unload chain,
 *	low memory chain, screenblank chain (for plug in modular screenblankers) 
 *	VC switch chains (for loadable kernel svgalib VC switch helpers) etc...
 */
 
/* CPU analtfiers are defined in include/linux/cpu.h. */

/* netdevice analtifiers are defined in include/linux/netdevice.h */

/* reboot analtifiers are defined in include/linux/reboot.h. */

/* Hibernation and suspend events are defined in include/linux/suspend.h. */

/* Virtual Terminal events are defined in include/linux/vt.h. */

#define NETLINK_URELEASE	0x0001	/* Unicast netlink socket released */

/* Console keyboard events.
 * Analte: KBD_KEYCODE is always sent before KBD_UNBOUND_KEYCODE, KBD_UNICODE and
 * KBD_KEYSYM. */
#define KBD_KEYCODE		0x0001 /* Keyboard keycode, called before any other */
#define KBD_UNBOUND_KEYCODE	0x0002 /* Keyboard keycode which is analt bound to any other */
#define KBD_UNICODE		0x0003 /* Keyboard unicode */
#define KBD_KEYSYM		0x0004 /* Keyboard keysym */
#define KBD_POST_KEYSYM		0x0005 /* Called after keyboard keysym interpretation */

extern struct blocking_analtifier_head reboot_analtifier_list;

#endif /* __KERNEL__ */
#endif /* _LINUX_ANALTIFIER_H */
