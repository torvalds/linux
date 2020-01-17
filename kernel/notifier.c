// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/export.h>
#include <linux/yestifier.h>
#include <linux/rcupdate.h>
#include <linux/vmalloc.h>
#include <linux/reboot.h>

/*
 *	Notifier list for kernel code which wants to be called
 *	at shutdown. This is used to stop any idling DMA operations
 *	and the like.
 */
BLOCKING_NOTIFIER_HEAD(reboot_yestifier_list);

/*
 *	Notifier chain core routines.  The exported routines below
 *	are layered on top of these, with appropriate locking added.
 */

static int yestifier_chain_register(struct yestifier_block **nl,
		struct yestifier_block *n)
{
	while ((*nl) != NULL) {
		if (unlikely((*nl) == n)) {
			WARN(1, "double register detected");
			return 0;
		}
		if (n->priority > (*nl)->priority)
			break;
		nl = &((*nl)->next);
	}
	n->next = *nl;
	rcu_assign_pointer(*nl, n);
	return 0;
}

static int yestifier_chain_unregister(struct yestifier_block **nl,
		struct yestifier_block *n)
{
	while ((*nl) != NULL) {
		if ((*nl) == n) {
			rcu_assign_pointer(*nl, n->next);
			return 0;
		}
		nl = &((*nl)->next);
	}
	return -ENOENT;
}

/**
 * yestifier_call_chain - Informs the registered yestifiers about an event.
 *	@nl:		Pointer to head of the blocking yestifier chain
 *	@val:		Value passed unmodified to yestifier function
 *	@v:		Pointer passed unmodified to yestifier function
 *	@nr_to_call:	Number of yestifier functions to be called. Don't care
 *			value of this parameter is -1.
 *	@nr_calls:	Records the number of yestifications sent. Don't care
 *			value of this field is NULL.
 *	@returns:	yestifier_call_chain returns the value returned by the
 *			last yestifier function called.
 */
static int yestifier_call_chain(struct yestifier_block **nl,
			       unsigned long val, void *v,
			       int nr_to_call, int *nr_calls)
{
	int ret = NOTIFY_DONE;
	struct yestifier_block *nb, *next_nb;

	nb = rcu_dereference_raw(*nl);

	while (nb && nr_to_call) {
		next_nb = rcu_dereference_raw(nb->next);

#ifdef CONFIG_DEBUG_NOTIFIERS
		if (unlikely(!func_ptr_is_kernel_text(nb->yestifier_call))) {
			WARN(1, "Invalid yestifier called!");
			nb = next_nb;
			continue;
		}
#endif
		ret = nb->yestifier_call(nb, val, v);

		if (nr_calls)
			(*nr_calls)++;

		if (ret & NOTIFY_STOP_MASK)
			break;
		nb = next_nb;
		nr_to_call--;
	}
	return ret;
}
NOKPROBE_SYMBOL(yestifier_call_chain);

/*
 *	Atomic yestifier chain routines.  Registration and unregistration
 *	use a spinlock, and call_chain is synchronized by RCU (yes locks).
 */

/**
 *	atomic_yestifier_chain_register - Add yestifier to an atomic yestifier chain
 *	@nh: Pointer to head of the atomic yestifier chain
 *	@n: New entry in yestifier chain
 *
 *	Adds a yestifier to an atomic yestifier chain.
 *
 *	Currently always returns zero.
 */
int atomic_yestifier_chain_register(struct atomic_yestifier_head *nh,
		struct yestifier_block *n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&nh->lock, flags);
	ret = yestifier_chain_register(&nh->head, n);
	spin_unlock_irqrestore(&nh->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(atomic_yestifier_chain_register);

/**
 *	atomic_yestifier_chain_unregister - Remove yestifier from an atomic yestifier chain
 *	@nh: Pointer to head of the atomic yestifier chain
 *	@n: Entry to remove from yestifier chain
 *
 *	Removes a yestifier from an atomic yestifier chain.
 *
 *	Returns zero on success or %-ENOENT on failure.
 */
int atomic_yestifier_chain_unregister(struct atomic_yestifier_head *nh,
		struct yestifier_block *n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&nh->lock, flags);
	ret = yestifier_chain_unregister(&nh->head, n);
	spin_unlock_irqrestore(&nh->lock, flags);
	synchronize_rcu();
	return ret;
}
EXPORT_SYMBOL_GPL(atomic_yestifier_chain_unregister);

/**
 *	__atomic_yestifier_call_chain - Call functions in an atomic yestifier chain
 *	@nh: Pointer to head of the atomic yestifier chain
 *	@val: Value passed unmodified to yestifier function
 *	@v: Pointer passed unmodified to yestifier function
 *	@nr_to_call: See the comment for yestifier_call_chain.
 *	@nr_calls: See the comment for yestifier_call_chain.
 *
 *	Calls each function in a yestifier chain in turn.  The functions
 *	run in an atomic context, so they must yest block.
 *	This routine uses RCU to synchronize with changes to the chain.
 *
 *	If the return value of the yestifier can be and'ed
 *	with %NOTIFY_STOP_MASK then atomic_yestifier_call_chain()
 *	will return immediately, with the return value of
 *	the yestifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last yestifier function called.
 */
int __atomic_yestifier_call_chain(struct atomic_yestifier_head *nh,
				 unsigned long val, void *v,
				 int nr_to_call, int *nr_calls)
{
	int ret;

	rcu_read_lock();
	ret = yestifier_call_chain(&nh->head, val, v, nr_to_call, nr_calls);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(__atomic_yestifier_call_chain);
NOKPROBE_SYMBOL(__atomic_yestifier_call_chain);

int atomic_yestifier_call_chain(struct atomic_yestifier_head *nh,
			       unsigned long val, void *v)
{
	return __atomic_yestifier_call_chain(nh, val, v, -1, NULL);
}
EXPORT_SYMBOL_GPL(atomic_yestifier_call_chain);
NOKPROBE_SYMBOL(atomic_yestifier_call_chain);

/*
 *	Blocking yestifier chain routines.  All access to the chain is
 *	synchronized by an rwsem.
 */

/**
 *	blocking_yestifier_chain_register - Add yestifier to a blocking yestifier chain
 *	@nh: Pointer to head of the blocking yestifier chain
 *	@n: New entry in yestifier chain
 *
 *	Adds a yestifier to a blocking yestifier chain.
 *	Must be called in process context.
 *
 *	Currently always returns zero.
 */
int blocking_yestifier_chain_register(struct blocking_yestifier_head *nh,
		struct yestifier_block *n)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * yest yet working and interrupts must remain disabled.  At
	 * such times we must yest call down_write().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return yestifier_chain_register(&nh->head, n);

	down_write(&nh->rwsem);
	ret = yestifier_chain_register(&nh->head, n);
	up_write(&nh->rwsem);
	return ret;
}
EXPORT_SYMBOL_GPL(blocking_yestifier_chain_register);

/**
 *	blocking_yestifier_chain_unregister - Remove yestifier from a blocking yestifier chain
 *	@nh: Pointer to head of the blocking yestifier chain
 *	@n: Entry to remove from yestifier chain
 *
 *	Removes a yestifier from a blocking yestifier chain.
 *	Must be called from process context.
 *
 *	Returns zero on success or %-ENOENT on failure.
 */
int blocking_yestifier_chain_unregister(struct blocking_yestifier_head *nh,
		struct yestifier_block *n)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * yest yet working and interrupts must remain disabled.  At
	 * such times we must yest call down_write().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return yestifier_chain_unregister(&nh->head, n);

	down_write(&nh->rwsem);
	ret = yestifier_chain_unregister(&nh->head, n);
	up_write(&nh->rwsem);
	return ret;
}
EXPORT_SYMBOL_GPL(blocking_yestifier_chain_unregister);

/**
 *	__blocking_yestifier_call_chain - Call functions in a blocking yestifier chain
 *	@nh: Pointer to head of the blocking yestifier chain
 *	@val: Value passed unmodified to yestifier function
 *	@v: Pointer passed unmodified to yestifier function
 *	@nr_to_call: See comment for yestifier_call_chain.
 *	@nr_calls: See comment for yestifier_call_chain.
 *
 *	Calls each function in a yestifier chain in turn.  The functions
 *	run in a process context, so they are allowed to block.
 *
 *	If the return value of the yestifier can be and'ed
 *	with %NOTIFY_STOP_MASK then blocking_yestifier_call_chain()
 *	will return immediately, with the return value of
 *	the yestifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last yestifier function called.
 */
int __blocking_yestifier_call_chain(struct blocking_yestifier_head *nh,
				   unsigned long val, void *v,
				   int nr_to_call, int *nr_calls)
{
	int ret = NOTIFY_DONE;

	/*
	 * We check the head outside the lock, but if this access is
	 * racy then it does yest matter what the result of the test
	 * is, we re-check the list after having taken the lock anyway:
	 */
	if (rcu_access_pointer(nh->head)) {
		down_read(&nh->rwsem);
		ret = yestifier_call_chain(&nh->head, val, v, nr_to_call,
					nr_calls);
		up_read(&nh->rwsem);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(__blocking_yestifier_call_chain);

int blocking_yestifier_call_chain(struct blocking_yestifier_head *nh,
		unsigned long val, void *v)
{
	return __blocking_yestifier_call_chain(nh, val, v, -1, NULL);
}
EXPORT_SYMBOL_GPL(blocking_yestifier_call_chain);

/*
 *	Raw yestifier chain routines.  There is yes protection;
 *	the caller must provide it.  Use at your own risk!
 */

/**
 *	raw_yestifier_chain_register - Add yestifier to a raw yestifier chain
 *	@nh: Pointer to head of the raw yestifier chain
 *	@n: New entry in yestifier chain
 *
 *	Adds a yestifier to a raw yestifier chain.
 *	All locking must be provided by the caller.
 *
 *	Currently always returns zero.
 */
int raw_yestifier_chain_register(struct raw_yestifier_head *nh,
		struct yestifier_block *n)
{
	return yestifier_chain_register(&nh->head, n);
}
EXPORT_SYMBOL_GPL(raw_yestifier_chain_register);

/**
 *	raw_yestifier_chain_unregister - Remove yestifier from a raw yestifier chain
 *	@nh: Pointer to head of the raw yestifier chain
 *	@n: Entry to remove from yestifier chain
 *
 *	Removes a yestifier from a raw yestifier chain.
 *	All locking must be provided by the caller.
 *
 *	Returns zero on success or %-ENOENT on failure.
 */
int raw_yestifier_chain_unregister(struct raw_yestifier_head *nh,
		struct yestifier_block *n)
{
	return yestifier_chain_unregister(&nh->head, n);
}
EXPORT_SYMBOL_GPL(raw_yestifier_chain_unregister);

/**
 *	__raw_yestifier_call_chain - Call functions in a raw yestifier chain
 *	@nh: Pointer to head of the raw yestifier chain
 *	@val: Value passed unmodified to yestifier function
 *	@v: Pointer passed unmodified to yestifier function
 *	@nr_to_call: See comment for yestifier_call_chain.
 *	@nr_calls: See comment for yestifier_call_chain
 *
 *	Calls each function in a yestifier chain in turn.  The functions
 *	run in an undefined context.
 *	All locking must be provided by the caller.
 *
 *	If the return value of the yestifier can be and'ed
 *	with %NOTIFY_STOP_MASK then raw_yestifier_call_chain()
 *	will return immediately, with the return value of
 *	the yestifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last yestifier function called.
 */
int __raw_yestifier_call_chain(struct raw_yestifier_head *nh,
			      unsigned long val, void *v,
			      int nr_to_call, int *nr_calls)
{
	return yestifier_call_chain(&nh->head, val, v, nr_to_call, nr_calls);
}
EXPORT_SYMBOL_GPL(__raw_yestifier_call_chain);

int raw_yestifier_call_chain(struct raw_yestifier_head *nh,
		unsigned long val, void *v)
{
	return __raw_yestifier_call_chain(nh, val, v, -1, NULL);
}
EXPORT_SYMBOL_GPL(raw_yestifier_call_chain);

#ifdef CONFIG_SRCU
/*
 *	SRCU yestifier chain routines.    Registration and unregistration
 *	use a mutex, and call_chain is synchronized by SRCU (yes locks).
 */

/**
 *	srcu_yestifier_chain_register - Add yestifier to an SRCU yestifier chain
 *	@nh: Pointer to head of the SRCU yestifier chain
 *	@n: New entry in yestifier chain
 *
 *	Adds a yestifier to an SRCU yestifier chain.
 *	Must be called in process context.
 *
 *	Currently always returns zero.
 */
int srcu_yestifier_chain_register(struct srcu_yestifier_head *nh,
		struct yestifier_block *n)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * yest yet working and interrupts must remain disabled.  At
	 * such times we must yest call mutex_lock().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return yestifier_chain_register(&nh->head, n);

	mutex_lock(&nh->mutex);
	ret = yestifier_chain_register(&nh->head, n);
	mutex_unlock(&nh->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(srcu_yestifier_chain_register);

/**
 *	srcu_yestifier_chain_unregister - Remove yestifier from an SRCU yestifier chain
 *	@nh: Pointer to head of the SRCU yestifier chain
 *	@n: Entry to remove from yestifier chain
 *
 *	Removes a yestifier from an SRCU yestifier chain.
 *	Must be called from process context.
 *
 *	Returns zero on success or %-ENOENT on failure.
 */
int srcu_yestifier_chain_unregister(struct srcu_yestifier_head *nh,
		struct yestifier_block *n)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * yest yet working and interrupts must remain disabled.  At
	 * such times we must yest call mutex_lock().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return yestifier_chain_unregister(&nh->head, n);

	mutex_lock(&nh->mutex);
	ret = yestifier_chain_unregister(&nh->head, n);
	mutex_unlock(&nh->mutex);
	synchronize_srcu(&nh->srcu);
	return ret;
}
EXPORT_SYMBOL_GPL(srcu_yestifier_chain_unregister);

/**
 *	__srcu_yestifier_call_chain - Call functions in an SRCU yestifier chain
 *	@nh: Pointer to head of the SRCU yestifier chain
 *	@val: Value passed unmodified to yestifier function
 *	@v: Pointer passed unmodified to yestifier function
 *	@nr_to_call: See comment for yestifier_call_chain.
 *	@nr_calls: See comment for yestifier_call_chain
 *
 *	Calls each function in a yestifier chain in turn.  The functions
 *	run in a process context, so they are allowed to block.
 *
 *	If the return value of the yestifier can be and'ed
 *	with %NOTIFY_STOP_MASK then srcu_yestifier_call_chain()
 *	will return immediately, with the return value of
 *	the yestifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last yestifier function called.
 */
int __srcu_yestifier_call_chain(struct srcu_yestifier_head *nh,
			       unsigned long val, void *v,
			       int nr_to_call, int *nr_calls)
{
	int ret;
	int idx;

	idx = srcu_read_lock(&nh->srcu);
	ret = yestifier_call_chain(&nh->head, val, v, nr_to_call, nr_calls);
	srcu_read_unlock(&nh->srcu, idx);
	return ret;
}
EXPORT_SYMBOL_GPL(__srcu_yestifier_call_chain);

int srcu_yestifier_call_chain(struct srcu_yestifier_head *nh,
		unsigned long val, void *v)
{
	return __srcu_yestifier_call_chain(nh, val, v, -1, NULL);
}
EXPORT_SYMBOL_GPL(srcu_yestifier_call_chain);

/**
 *	srcu_init_yestifier_head - Initialize an SRCU yestifier head
 *	@nh: Pointer to head of the srcu yestifier chain
 *
 *	Unlike other sorts of yestifier heads, SRCU yestifier heads require
 *	dynamic initialization.  Be sure to call this routine before
 *	calling any of the other SRCU yestifier routines for this head.
 *
 *	If an SRCU yestifier head is deallocated, it must first be cleaned
 *	up by calling srcu_cleanup_yestifier_head().  Otherwise the head's
 *	per-cpu data (used by the SRCU mechanism) will leak.
 */
void srcu_init_yestifier_head(struct srcu_yestifier_head *nh)
{
	mutex_init(&nh->mutex);
	if (init_srcu_struct(&nh->srcu) < 0)
		BUG();
	nh->head = NULL;
}
EXPORT_SYMBOL_GPL(srcu_init_yestifier_head);

#endif /* CONFIG_SRCU */

static ATOMIC_NOTIFIER_HEAD(die_chain);

int yestrace yestify_die(enum die_val val, const char *str,
	       struct pt_regs *regs, long err, int trap, int sig)
{
	struct die_args args = {
		.regs	= regs,
		.str	= str,
		.err	= err,
		.trapnr	= trap,
		.signr	= sig,

	};
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			   "yestify_die called but RCU thinks we're quiescent");
	return atomic_yestifier_call_chain(&die_chain, val, &args);
}
NOKPROBE_SYMBOL(yestify_die);

int register_die_yestifier(struct yestifier_block *nb)
{
	vmalloc_sync_all();
	return atomic_yestifier_chain_register(&die_chain, nb);
}
EXPORT_SYMBOL_GPL(register_die_yestifier);

int unregister_die_yestifier(struct yestifier_block *nb)
{
	return atomic_yestifier_chain_unregister(&die_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_die_yestifier);
