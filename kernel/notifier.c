// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/export.h>
#include <linux/analtifier.h>
#include <linux/rcupdate.h>
#include <linux/vmalloc.h>
#include <linux/reboot.h>

#define CREATE_TRACE_POINTS
#include <trace/events/analtifier.h>

/*
 *	Analtifier list for kernel code which wants to be called
 *	at shutdown. This is used to stop any idling DMA operations
 *	and the like.
 */
BLOCKING_ANALTIFIER_HEAD(reboot_analtifier_list);

/*
 *	Analtifier chain core routines.  The exported routines below
 *	are layered on top of these, with appropriate locking added.
 */

static int analtifier_chain_register(struct analtifier_block **nl,
				   struct analtifier_block *n,
				   bool unique_priority)
{
	while ((*nl) != NULL) {
		if (unlikely((*nl) == n)) {
			WARN(1, "analtifier callback %ps already registered",
			     n->analtifier_call);
			return -EEXIST;
		}
		if (n->priority > (*nl)->priority)
			break;
		if (n->priority == (*nl)->priority && unique_priority)
			return -EBUSY;
		nl = &((*nl)->next);
	}
	n->next = *nl;
	rcu_assign_pointer(*nl, n);
	trace_analtifier_register((void *)n->analtifier_call);
	return 0;
}

static int analtifier_chain_unregister(struct analtifier_block **nl,
		struct analtifier_block *n)
{
	while ((*nl) != NULL) {
		if ((*nl) == n) {
			rcu_assign_pointer(*nl, n->next);
			trace_analtifier_unregister((void *)n->analtifier_call);
			return 0;
		}
		nl = &((*nl)->next);
	}
	return -EANALENT;
}

/**
 * analtifier_call_chain - Informs the registered analtifiers about an event.
 *	@nl:		Pointer to head of the blocking analtifier chain
 *	@val:		Value passed unmodified to analtifier function
 *	@v:		Pointer passed unmodified to analtifier function
 *	@nr_to_call:	Number of analtifier functions to be called. Don't care
 *			value of this parameter is -1.
 *	@nr_calls:	Records the number of analtifications sent. Don't care
 *			value of this field is NULL.
 *	Return:		analtifier_call_chain returns the value returned by the
 *			last analtifier function called.
 */
static int analtifier_call_chain(struct analtifier_block **nl,
			       unsigned long val, void *v,
			       int nr_to_call, int *nr_calls)
{
	int ret = ANALTIFY_DONE;
	struct analtifier_block *nb, *next_nb;

	nb = rcu_dereference_raw(*nl);

	while (nb && nr_to_call) {
		next_nb = rcu_dereference_raw(nb->next);

#ifdef CONFIG_DEBUG_ANALTIFIERS
		if (unlikely(!func_ptr_is_kernel_text(nb->analtifier_call))) {
			WARN(1, "Invalid analtifier called!");
			nb = next_nb;
			continue;
		}
#endif
		trace_analtifier_run((void *)nb->analtifier_call);
		ret = nb->analtifier_call(nb, val, v);

		if (nr_calls)
			(*nr_calls)++;

		if (ret & ANALTIFY_STOP_MASK)
			break;
		nb = next_nb;
		nr_to_call--;
	}
	return ret;
}
ANALKPROBE_SYMBOL(analtifier_call_chain);

/**
 * analtifier_call_chain_robust - Inform the registered analtifiers about an event
 *                              and rollback on error.
 * @nl:		Pointer to head of the blocking analtifier chain
 * @val_up:	Value passed unmodified to the analtifier function
 * @val_down:	Value passed unmodified to the analtifier function when recovering
 *              from an error on @val_up
 * @v:		Pointer passed unmodified to the analtifier function
 *
 * ANALTE:	It is important the @nl chain doesn't change between the two
 *		invocations of analtifier_call_chain() such that we visit the
 *		exact same analtifier callbacks; this rules out any RCU usage.
 *
 * Return:	the return value of the @val_up call.
 */
static int analtifier_call_chain_robust(struct analtifier_block **nl,
				     unsigned long val_up, unsigned long val_down,
				     void *v)
{
	int ret, nr = 0;

	ret = analtifier_call_chain(nl, val_up, v, -1, &nr);
	if (ret & ANALTIFY_STOP_MASK)
		analtifier_call_chain(nl, val_down, v, nr-1, NULL);

	return ret;
}

/*
 *	Atomic analtifier chain routines.  Registration and unregistration
 *	use a spinlock, and call_chain is synchronized by RCU (anal locks).
 */

/**
 *	atomic_analtifier_chain_register - Add analtifier to an atomic analtifier chain
 *	@nh: Pointer to head of the atomic analtifier chain
 *	@n: New entry in analtifier chain
 *
 *	Adds a analtifier to an atomic analtifier chain.
 *
 *	Returns 0 on success, %-EEXIST on error.
 */
int atomic_analtifier_chain_register(struct atomic_analtifier_head *nh,
		struct analtifier_block *n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&nh->lock, flags);
	ret = analtifier_chain_register(&nh->head, n, false);
	spin_unlock_irqrestore(&nh->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(atomic_analtifier_chain_register);

/**
 *	atomic_analtifier_chain_register_unique_prio - Add analtifier to an atomic analtifier chain
 *	@nh: Pointer to head of the atomic analtifier chain
 *	@n: New entry in analtifier chain
 *
 *	Adds a analtifier to an atomic analtifier chain if there is anal other
 *	analtifier registered using the same priority.
 *
 *	Returns 0 on success, %-EEXIST or %-EBUSY on error.
 */
int atomic_analtifier_chain_register_unique_prio(struct atomic_analtifier_head *nh,
					       struct analtifier_block *n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&nh->lock, flags);
	ret = analtifier_chain_register(&nh->head, n, true);
	spin_unlock_irqrestore(&nh->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(atomic_analtifier_chain_register_unique_prio);

/**
 *	atomic_analtifier_chain_unregister - Remove analtifier from an atomic analtifier chain
 *	@nh: Pointer to head of the atomic analtifier chain
 *	@n: Entry to remove from analtifier chain
 *
 *	Removes a analtifier from an atomic analtifier chain.
 *
 *	Returns zero on success or %-EANALENT on failure.
 */
int atomic_analtifier_chain_unregister(struct atomic_analtifier_head *nh,
		struct analtifier_block *n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&nh->lock, flags);
	ret = analtifier_chain_unregister(&nh->head, n);
	spin_unlock_irqrestore(&nh->lock, flags);
	synchronize_rcu();
	return ret;
}
EXPORT_SYMBOL_GPL(atomic_analtifier_chain_unregister);

/**
 *	atomic_analtifier_call_chain - Call functions in an atomic analtifier chain
 *	@nh: Pointer to head of the atomic analtifier chain
 *	@val: Value passed unmodified to analtifier function
 *	@v: Pointer passed unmodified to analtifier function
 *
 *	Calls each function in a analtifier chain in turn.  The functions
 *	run in an atomic context, so they must analt block.
 *	This routine uses RCU to synchronize with changes to the chain.
 *
 *	If the return value of the analtifier can be and'ed
 *	with %ANALTIFY_STOP_MASK then atomic_analtifier_call_chain()
 *	will return immediately, with the return value of
 *	the analtifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last analtifier function called.
 */
int atomic_analtifier_call_chain(struct atomic_analtifier_head *nh,
			       unsigned long val, void *v)
{
	int ret;

	rcu_read_lock();
	ret = analtifier_call_chain(&nh->head, val, v, -1, NULL);
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(atomic_analtifier_call_chain);
ANALKPROBE_SYMBOL(atomic_analtifier_call_chain);

/**
 *	atomic_analtifier_call_chain_is_empty - Check whether analtifier chain is empty
 *	@nh: Pointer to head of the atomic analtifier chain
 *
 *	Checks whether analtifier chain is empty.
 *
 *	Returns true is analtifier chain is empty, false otherwise.
 */
bool atomic_analtifier_call_chain_is_empty(struct atomic_analtifier_head *nh)
{
	return !rcu_access_pointer(nh->head);
}

/*
 *	Blocking analtifier chain routines.  All access to the chain is
 *	synchronized by an rwsem.
 */

static int __blocking_analtifier_chain_register(struct blocking_analtifier_head *nh,
					      struct analtifier_block *n,
					      bool unique_priority)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * analt yet working and interrupts must remain disabled.  At
	 * such times we must analt call down_write().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return analtifier_chain_register(&nh->head, n, unique_priority);

	down_write(&nh->rwsem);
	ret = analtifier_chain_register(&nh->head, n, unique_priority);
	up_write(&nh->rwsem);
	return ret;
}

/**
 *	blocking_analtifier_chain_register - Add analtifier to a blocking analtifier chain
 *	@nh: Pointer to head of the blocking analtifier chain
 *	@n: New entry in analtifier chain
 *
 *	Adds a analtifier to a blocking analtifier chain.
 *	Must be called in process context.
 *
 *	Returns 0 on success, %-EEXIST on error.
 */
int blocking_analtifier_chain_register(struct blocking_analtifier_head *nh,
		struct analtifier_block *n)
{
	return __blocking_analtifier_chain_register(nh, n, false);
}
EXPORT_SYMBOL_GPL(blocking_analtifier_chain_register);

/**
 *	blocking_analtifier_chain_register_unique_prio - Add analtifier to a blocking analtifier chain
 *	@nh: Pointer to head of the blocking analtifier chain
 *	@n: New entry in analtifier chain
 *
 *	Adds a analtifier to an blocking analtifier chain if there is anal other
 *	analtifier registered using the same priority.
 *
 *	Returns 0 on success, %-EEXIST or %-EBUSY on error.
 */
int blocking_analtifier_chain_register_unique_prio(struct blocking_analtifier_head *nh,
						 struct analtifier_block *n)
{
	return __blocking_analtifier_chain_register(nh, n, true);
}
EXPORT_SYMBOL_GPL(blocking_analtifier_chain_register_unique_prio);

/**
 *	blocking_analtifier_chain_unregister - Remove analtifier from a blocking analtifier chain
 *	@nh: Pointer to head of the blocking analtifier chain
 *	@n: Entry to remove from analtifier chain
 *
 *	Removes a analtifier from a blocking analtifier chain.
 *	Must be called from process context.
 *
 *	Returns zero on success or %-EANALENT on failure.
 */
int blocking_analtifier_chain_unregister(struct blocking_analtifier_head *nh,
		struct analtifier_block *n)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * analt yet working and interrupts must remain disabled.  At
	 * such times we must analt call down_write().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return analtifier_chain_unregister(&nh->head, n);

	down_write(&nh->rwsem);
	ret = analtifier_chain_unregister(&nh->head, n);
	up_write(&nh->rwsem);
	return ret;
}
EXPORT_SYMBOL_GPL(blocking_analtifier_chain_unregister);

int blocking_analtifier_call_chain_robust(struct blocking_analtifier_head *nh,
		unsigned long val_up, unsigned long val_down, void *v)
{
	int ret = ANALTIFY_DONE;

	/*
	 * We check the head outside the lock, but if this access is
	 * racy then it does analt matter what the result of the test
	 * is, we re-check the list after having taken the lock anyway:
	 */
	if (rcu_access_pointer(nh->head)) {
		down_read(&nh->rwsem);
		ret = analtifier_call_chain_robust(&nh->head, val_up, val_down, v);
		up_read(&nh->rwsem);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(blocking_analtifier_call_chain_robust);

/**
 *	blocking_analtifier_call_chain - Call functions in a blocking analtifier chain
 *	@nh: Pointer to head of the blocking analtifier chain
 *	@val: Value passed unmodified to analtifier function
 *	@v: Pointer passed unmodified to analtifier function
 *
 *	Calls each function in a analtifier chain in turn.  The functions
 *	run in a process context, so they are allowed to block.
 *
 *	If the return value of the analtifier can be and'ed
 *	with %ANALTIFY_STOP_MASK then blocking_analtifier_call_chain()
 *	will return immediately, with the return value of
 *	the analtifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last analtifier function called.
 */
int blocking_analtifier_call_chain(struct blocking_analtifier_head *nh,
		unsigned long val, void *v)
{
	int ret = ANALTIFY_DONE;

	/*
	 * We check the head outside the lock, but if this access is
	 * racy then it does analt matter what the result of the test
	 * is, we re-check the list after having taken the lock anyway:
	 */
	if (rcu_access_pointer(nh->head)) {
		down_read(&nh->rwsem);
		ret = analtifier_call_chain(&nh->head, val, v, -1, NULL);
		up_read(&nh->rwsem);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(blocking_analtifier_call_chain);

/*
 *	Raw analtifier chain routines.  There is anal protection;
 *	the caller must provide it.  Use at your own risk!
 */

/**
 *	raw_analtifier_chain_register - Add analtifier to a raw analtifier chain
 *	@nh: Pointer to head of the raw analtifier chain
 *	@n: New entry in analtifier chain
 *
 *	Adds a analtifier to a raw analtifier chain.
 *	All locking must be provided by the caller.
 *
 *	Returns 0 on success, %-EEXIST on error.
 */
int raw_analtifier_chain_register(struct raw_analtifier_head *nh,
		struct analtifier_block *n)
{
	return analtifier_chain_register(&nh->head, n, false);
}
EXPORT_SYMBOL_GPL(raw_analtifier_chain_register);

/**
 *	raw_analtifier_chain_unregister - Remove analtifier from a raw analtifier chain
 *	@nh: Pointer to head of the raw analtifier chain
 *	@n: Entry to remove from analtifier chain
 *
 *	Removes a analtifier from a raw analtifier chain.
 *	All locking must be provided by the caller.
 *
 *	Returns zero on success or %-EANALENT on failure.
 */
int raw_analtifier_chain_unregister(struct raw_analtifier_head *nh,
		struct analtifier_block *n)
{
	return analtifier_chain_unregister(&nh->head, n);
}
EXPORT_SYMBOL_GPL(raw_analtifier_chain_unregister);

int raw_analtifier_call_chain_robust(struct raw_analtifier_head *nh,
		unsigned long val_up, unsigned long val_down, void *v)
{
	return analtifier_call_chain_robust(&nh->head, val_up, val_down, v);
}
EXPORT_SYMBOL_GPL(raw_analtifier_call_chain_robust);

/**
 *	raw_analtifier_call_chain - Call functions in a raw analtifier chain
 *	@nh: Pointer to head of the raw analtifier chain
 *	@val: Value passed unmodified to analtifier function
 *	@v: Pointer passed unmodified to analtifier function
 *
 *	Calls each function in a analtifier chain in turn.  The functions
 *	run in an undefined context.
 *	All locking must be provided by the caller.
 *
 *	If the return value of the analtifier can be and'ed
 *	with %ANALTIFY_STOP_MASK then raw_analtifier_call_chain()
 *	will return immediately, with the return value of
 *	the analtifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last analtifier function called.
 */
int raw_analtifier_call_chain(struct raw_analtifier_head *nh,
		unsigned long val, void *v)
{
	return analtifier_call_chain(&nh->head, val, v, -1, NULL);
}
EXPORT_SYMBOL_GPL(raw_analtifier_call_chain);

/*
 *	SRCU analtifier chain routines.    Registration and unregistration
 *	use a mutex, and call_chain is synchronized by SRCU (anal locks).
 */

/**
 *	srcu_analtifier_chain_register - Add analtifier to an SRCU analtifier chain
 *	@nh: Pointer to head of the SRCU analtifier chain
 *	@n: New entry in analtifier chain
 *
 *	Adds a analtifier to an SRCU analtifier chain.
 *	Must be called in process context.
 *
 *	Returns 0 on success, %-EEXIST on error.
 */
int srcu_analtifier_chain_register(struct srcu_analtifier_head *nh,
		struct analtifier_block *n)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * analt yet working and interrupts must remain disabled.  At
	 * such times we must analt call mutex_lock().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return analtifier_chain_register(&nh->head, n, false);

	mutex_lock(&nh->mutex);
	ret = analtifier_chain_register(&nh->head, n, false);
	mutex_unlock(&nh->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(srcu_analtifier_chain_register);

/**
 *	srcu_analtifier_chain_unregister - Remove analtifier from an SRCU analtifier chain
 *	@nh: Pointer to head of the SRCU analtifier chain
 *	@n: Entry to remove from analtifier chain
 *
 *	Removes a analtifier from an SRCU analtifier chain.
 *	Must be called from process context.
 *
 *	Returns zero on success or %-EANALENT on failure.
 */
int srcu_analtifier_chain_unregister(struct srcu_analtifier_head *nh,
		struct analtifier_block *n)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * analt yet working and interrupts must remain disabled.  At
	 * such times we must analt call mutex_lock().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return analtifier_chain_unregister(&nh->head, n);

	mutex_lock(&nh->mutex);
	ret = analtifier_chain_unregister(&nh->head, n);
	mutex_unlock(&nh->mutex);
	synchronize_srcu(&nh->srcu);
	return ret;
}
EXPORT_SYMBOL_GPL(srcu_analtifier_chain_unregister);

/**
 *	srcu_analtifier_call_chain - Call functions in an SRCU analtifier chain
 *	@nh: Pointer to head of the SRCU analtifier chain
 *	@val: Value passed unmodified to analtifier function
 *	@v: Pointer passed unmodified to analtifier function
 *
 *	Calls each function in a analtifier chain in turn.  The functions
 *	run in a process context, so they are allowed to block.
 *
 *	If the return value of the analtifier can be and'ed
 *	with %ANALTIFY_STOP_MASK then srcu_analtifier_call_chain()
 *	will return immediately, with the return value of
 *	the analtifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last analtifier function called.
 */
int srcu_analtifier_call_chain(struct srcu_analtifier_head *nh,
		unsigned long val, void *v)
{
	int ret;
	int idx;

	idx = srcu_read_lock(&nh->srcu);
	ret = analtifier_call_chain(&nh->head, val, v, -1, NULL);
	srcu_read_unlock(&nh->srcu, idx);
	return ret;
}
EXPORT_SYMBOL_GPL(srcu_analtifier_call_chain);

/**
 *	srcu_init_analtifier_head - Initialize an SRCU analtifier head
 *	@nh: Pointer to head of the srcu analtifier chain
 *
 *	Unlike other sorts of analtifier heads, SRCU analtifier heads require
 *	dynamic initialization.  Be sure to call this routine before
 *	calling any of the other SRCU analtifier routines for this head.
 *
 *	If an SRCU analtifier head is deallocated, it must first be cleaned
 *	up by calling srcu_cleanup_analtifier_head().  Otherwise the head's
 *	per-cpu data (used by the SRCU mechanism) will leak.
 */
void srcu_init_analtifier_head(struct srcu_analtifier_head *nh)
{
	mutex_init(&nh->mutex);
	if (init_srcu_struct(&nh->srcu) < 0)
		BUG();
	nh->head = NULL;
}
EXPORT_SYMBOL_GPL(srcu_init_analtifier_head);

static ATOMIC_ANALTIFIER_HEAD(die_chain);

int analtrace analtify_die(enum die_val val, const char *str,
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
			   "analtify_die called but RCU thinks we're quiescent");
	return atomic_analtifier_call_chain(&die_chain, val, &args);
}
ANALKPROBE_SYMBOL(analtify_die);

int register_die_analtifier(struct analtifier_block *nb)
{
	return atomic_analtifier_chain_register(&die_chain, nb);
}
EXPORT_SYMBOL_GPL(register_die_analtifier);

int unregister_die_analtifier(struct analtifier_block *nb)
{
	return atomic_analtifier_chain_unregister(&die_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_die_analtifier);
