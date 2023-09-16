// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022 Linutronix GmbH, John Ogness
// Copyright (C) 2022 Intel, Thomas Gleixner

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "internal.h"
/*
 * Printk console printing implementation for consoles which does not depend
 * on the legacy style console_lock mechanism.
 *
 * The state of the console is maintained in the "nbcon_state" atomic
 * variable.
 *
 * The console is locked when:
 *
 *   - The 'prio' field contains the priority of the context that owns the
 *     console. Only higher priority contexts are allowed to take over the
 *     lock. A value of 0 (NBCON_PRIO_NONE) means the console is not locked.
 *
 *   - The 'cpu' field denotes on which CPU the console is locked. It is used
 *     to prevent busy waiting on the same CPU. Also it informs the lock owner
 *     that it has lost the lock in a more complex scenario when the lock was
 *     taken over by a higher priority context, released, and taken on another
 *     CPU with the same priority as the interrupted owner.
 *
 * The acquire mechanism uses a few more fields:
 *
 *   - The 'req_prio' field is used by the handover approach to make the
 *     current owner aware that there is a context with a higher priority
 *     waiting for the friendly handover.
 *
 *   - The 'unsafe' field allows to take over the console in a safe way in the
 *     middle of emitting a message. The field is set only when accessing some
 *     shared resources or when the console device is manipulated. It can be
 *     cleared, for example, after emitting one character when the console
 *     device is in a consistent state.
 *
 *   - The 'unsafe_takeover' field is set when a hostile takeover took the
 *     console in an unsafe state. The console will stay in the unsafe state
 *     until re-initialized.
 *
 * The acquire mechanism uses three approaches:
 *
 *   1) Direct acquire when the console is not owned or is owned by a lower
 *      priority context and is in a safe state.
 *
 *   2) Friendly handover mechanism uses a request/grant handshake. It is used
 *      when the current owner has lower priority and the console is in an
 *      unsafe state.
 *
 *      The requesting context:
 *
 *        a) Sets its priority into the 'req_prio' field.
 *
 *        b) Waits (with a timeout) for the owning context to unlock the
 *           console.
 *
 *        c) Takes the lock and clears the 'req_prio' field.
 *
 *      The owning context:
 *
 *        a) Observes the 'req_prio' field set on exit from the unsafe
 *           console state.
 *
 *        b) Gives up console ownership by clearing the 'prio' field.
 *
 *   3) Unsafe hostile takeover allows to take over the lock even when the
 *      console is an unsafe state. It is used only in panic() by the final
 *      attempt to flush consoles in a try and hope mode.
 *
 *      Note that separate record buffers are used in panic(). As a result,
 *      the messages can be read and formatted without any risk even after
 *      using the hostile takeover in unsafe state.
 *
 * The release function simply clears the 'prio' field.
 *
 * All operations on @console::nbcon_state are atomic cmpxchg based to
 * handle concurrency.
 *
 * The acquire/release functions implement only minimal policies:
 *
 *   - Preference for higher priority contexts.
 *   - Protection of the panic CPU.
 *
 * All other policy decisions must be made at the call sites:
 *
 *   - What is marked as an unsafe section.
 *   - Whether to spin-wait if there is already an owner and the console is
 *     in an unsafe state.
 *   - Whether to attempt an unsafe hostile takeover.
 *
 * The design allows to implement the well known:
 *
 *     acquire()
 *     output_one_printk_record()
 *     release()
 *
 * The output of one printk record might be interrupted with a higher priority
 * context. The new owner is supposed to reprint the entire interrupted record
 * from scratch.
 */

/**
 * nbcon_state_set - Helper function to set the console state
 * @con:	Console to update
 * @new:	The new state to write
 *
 * Only to be used when the console is not yet or no longer visible in the
 * system. Otherwise use nbcon_state_try_cmpxchg().
 */
static inline void nbcon_state_set(struct console *con, struct nbcon_state *new)
{
	atomic_set(&ACCESS_PRIVATE(con, nbcon_state), new->atom);
}

/**
 * nbcon_state_read - Helper function to read the console state
 * @con:	Console to read
 * @state:	The state to store the result
 */
static inline void nbcon_state_read(struct console *con, struct nbcon_state *state)
{
	state->atom = atomic_read(&ACCESS_PRIVATE(con, nbcon_state));
}

/**
 * nbcon_state_try_cmpxchg() - Helper function for atomic_try_cmpxchg() on console state
 * @con:	Console to update
 * @cur:	Old/expected state
 * @new:	New state
 *
 * Return: True on success. False on fail and @cur is updated.
 */
static inline bool nbcon_state_try_cmpxchg(struct console *con, struct nbcon_state *cur,
					   struct nbcon_state *new)
{
	return atomic_try_cmpxchg(&ACCESS_PRIVATE(con, nbcon_state), &cur->atom, new->atom);
}

/**
 * nbcon_context_try_acquire_direct - Try to acquire directly
 * @ctxt:	The context of the caller
 * @cur:	The current console state
 *
 * Acquire the console when it is released. Also acquire the console when
 * the current owner has a lower priority and the console is in a safe state.
 *
 * Return:	0 on success. Otherwise, an error code on failure. Also @cur
 *		is updated to the latest state when failed to modify it.
 *
 * Errors:
 *
 *	-EPERM:		A panic is in progress and this is not the panic CPU.
 *			Or the current owner or waiter has the same or higher
 *			priority. No acquire method can be successful in
 *			this case.
 *
 *	-EBUSY:		The current owner has a lower priority but the console
 *			in an unsafe state. The caller should try using
 *			the handover acquire method.
 */
static int nbcon_context_try_acquire_direct(struct nbcon_context *ctxt,
					    struct nbcon_state *cur)
{
	unsigned int cpu = smp_processor_id();
	struct console *con = ctxt->console;
	struct nbcon_state new;

	do {
		if (other_cpu_in_panic())
			return -EPERM;

		if (ctxt->prio <= cur->prio || ctxt->prio <= cur->req_prio)
			return -EPERM;

		if (cur->unsafe)
			return -EBUSY;

		/*
		 * The console should never be safe for a direct acquire
		 * if an unsafe hostile takeover has ever happened.
		 */
		WARN_ON_ONCE(cur->unsafe_takeover);

		new.atom = cur->atom;
		new.prio	= ctxt->prio;
		new.req_prio	= NBCON_PRIO_NONE;
		new.unsafe	= cur->unsafe_takeover;
		new.cpu		= cpu;

	} while (!nbcon_state_try_cmpxchg(con, cur, &new));

	return 0;
}

static bool nbcon_waiter_matches(struct nbcon_state *cur, int expected_prio)
{
	/*
	 * The request context is well defined by the @req_prio because:
	 *
	 * - Only a context with a higher priority can take over the request.
	 * - There are only three priorities.
	 * - Only one CPU is allowed to request PANIC priority.
	 * - Lower priorities are ignored during panic() until reboot.
	 *
	 * As a result, the following scenario is *not* possible:
	 *
	 * 1. Another context with a higher priority directly takes ownership.
	 * 2. The higher priority context releases the ownership.
	 * 3. A lower priority context takes the ownership.
	 * 4. Another context with the same priority as this context
	 *    creates a request and starts waiting.
	 */

	return (cur->req_prio == expected_prio);
}

/**
 * nbcon_context_try_acquire_requested - Try to acquire after having
 *					 requested a handover
 * @ctxt:	The context of the caller
 * @cur:	The current console state
 *
 * This is a helper function for nbcon_context_try_acquire_handover().
 * It is called when the console is in an unsafe state. The current
 * owner will release the console on exit from the unsafe region.
 *
 * Return:	0 on success and @cur is updated to the new console state.
 *		Otherwise an error code on failure.
 *
 * Errors:
 *
 *	-EPERM:		A panic is in progress and this is not the panic CPU
 *			or this context is no longer the waiter.
 *
 *	-EBUSY:		The console is still locked. The caller should
 *			continue waiting.
 *
 * Note: The caller must still remove the request when an error has occurred
 *       except when this context is no longer the waiter.
 */
static int nbcon_context_try_acquire_requested(struct nbcon_context *ctxt,
					       struct nbcon_state *cur)
{
	unsigned int cpu = smp_processor_id();
	struct console *con = ctxt->console;
	struct nbcon_state new;

	/* Note that the caller must still remove the request! */
	if (other_cpu_in_panic())
		return -EPERM;

	/*
	 * Note that the waiter will also change if there was an unsafe
	 * hostile takeover.
	 */
	if (!nbcon_waiter_matches(cur, ctxt->prio))
		return -EPERM;

	/* If still locked, caller should continue waiting. */
	if (cur->prio != NBCON_PRIO_NONE)
		return -EBUSY;

	/*
	 * The previous owner should have never released ownership
	 * in an unsafe region.
	 */
	WARN_ON_ONCE(cur->unsafe);

	new.atom = cur->atom;
	new.prio	= ctxt->prio;
	new.req_prio	= NBCON_PRIO_NONE;
	new.unsafe	= cur->unsafe_takeover;
	new.cpu		= cpu;

	if (!nbcon_state_try_cmpxchg(con, cur, &new)) {
		/*
		 * The acquire could fail only when it has been taken
		 * over by a higher priority context.
		 */
		WARN_ON_ONCE(nbcon_waiter_matches(cur, ctxt->prio));
		return -EPERM;
	}

	/* Handover success. This context now owns the console. */
	return 0;
}

/**
 * nbcon_context_try_acquire_handover - Try to acquire via handover
 * @ctxt:	The context of the caller
 * @cur:	The current console state
 *
 * The function must be called only when the context has higher priority
 * than the current owner and the console is in an unsafe state.
 * It is the case when nbcon_context_try_acquire_direct() returns -EBUSY.
 *
 * The function sets "req_prio" field to make the current owner aware of
 * the request. Then it waits until the current owner releases the console,
 * or an even higher context takes over the request, or timeout expires.
 *
 * The current owner checks the "req_prio" field on exit from the unsafe
 * region and releases the console. It does not touch the "req_prio" field
 * so that the console stays reserved for the waiter.
 *
 * Return:	0 on success. Otherwise, an error code on failure. Also @cur
 *		is updated to the latest state when failed to modify it.
 *
 * Errors:
 *
 *	-EPERM:		A panic is in progress and this is not the panic CPU.
 *			Or a higher priority context has taken over the
 *			console or the handover request.
 *
 *	-EBUSY:		The current owner is on the same CPU so that the hand
 *			shake could not work. Or the current owner is not
 *			willing to wait (zero timeout). Or the console does
 *			not enter the safe state before timeout passed. The
 *			caller might still use the unsafe hostile takeover
 *			when allowed.
 *
 *	-EAGAIN:	@cur has changed when creating the handover request.
 *			The caller should retry with direct acquire.
 */
static int nbcon_context_try_acquire_handover(struct nbcon_context *ctxt,
					      struct nbcon_state *cur)
{
	unsigned int cpu = smp_processor_id();
	struct console *con = ctxt->console;
	struct nbcon_state new;
	int timeout;
	int request_err = -EBUSY;

	/*
	 * Check that the handover is called when the direct acquire failed
	 * with -EBUSY.
	 */
	WARN_ON_ONCE(ctxt->prio <= cur->prio || ctxt->prio <= cur->req_prio);
	WARN_ON_ONCE(!cur->unsafe);

	/* Handover is not possible on the same CPU. */
	if (cur->cpu == cpu)
		return -EBUSY;

	/*
	 * Console stays unsafe after an unsafe takeover until re-initialized.
	 * Waiting is not going to help in this case.
	 */
	if (cur->unsafe_takeover)
		return -EBUSY;

	/* Is the caller willing to wait? */
	if (ctxt->spinwait_max_us == 0)
		return -EBUSY;

	/*
	 * Setup a request for the handover. The caller should try to acquire
	 * the console directly when the current state has been modified.
	 */
	new.atom = cur->atom;
	new.req_prio = ctxt->prio;
	if (!nbcon_state_try_cmpxchg(con, cur, &new))
		return -EAGAIN;

	cur->atom = new.atom;

	/* Wait until there is no owner and then acquire the console. */
	for (timeout = ctxt->spinwait_max_us; timeout >= 0; timeout--) {
		/* On successful acquire, this request is cleared. */
		request_err = nbcon_context_try_acquire_requested(ctxt, cur);
		if (!request_err)
			return 0;

		/*
		 * If the acquire should be aborted, it must be ensured
		 * that the request is removed before returning to caller.
		 */
		if (request_err == -EPERM)
			break;

		udelay(1);

		/* Re-read the state because some time has passed. */
		nbcon_state_read(con, cur);
	}

	/* Timed out or aborted. Carefully remove handover request. */
	do {
		/*
		 * No need to remove request if there is a new waiter. This
		 * can only happen if a higher priority context has taken over
		 * the console or the handover request.
		 */
		if (!nbcon_waiter_matches(cur, ctxt->prio))
			return -EPERM;

		/* Unset request for handover. */
		new.atom = cur->atom;
		new.req_prio = NBCON_PRIO_NONE;
		if (nbcon_state_try_cmpxchg(con, cur, &new)) {
			/*
			 * Request successfully unset. Report failure of
			 * acquiring via handover.
			 */
			cur->atom = new.atom;
			return request_err;
		}

		/*
		 * Unable to remove request. Try to acquire in case
		 * the owner has released the lock.
		 */
	} while (nbcon_context_try_acquire_requested(ctxt, cur));

	/* Lucky timing. The acquire succeeded while removing the request. */
	return 0;
}

/**
 * nbcon_context_try_acquire_hostile - Acquire via unsafe hostile takeover
 * @ctxt:	The context of the caller
 * @cur:	The current console state
 *
 * Acquire the console even in the unsafe state.
 *
 * It can be permitted by setting the 'allow_unsafe_takeover' field only
 * by the final attempt to flush messages in panic().
 *
 * Return:	0 on success. -EPERM when not allowed by the context.
 */
static int nbcon_context_try_acquire_hostile(struct nbcon_context *ctxt,
					     struct nbcon_state *cur)
{
	unsigned int cpu = smp_processor_id();
	struct console *con = ctxt->console;
	struct nbcon_state new;

	if (!ctxt->allow_unsafe_takeover)
		return -EPERM;

	/* Ensure caller is allowed to perform unsafe hostile takeovers. */
	if (WARN_ON_ONCE(ctxt->prio != NBCON_PRIO_PANIC))
		return -EPERM;

	/*
	 * Check that try_acquire_direct() and try_acquire_handover() returned
	 * -EBUSY in the right situation.
	 */
	WARN_ON_ONCE(ctxt->prio <= cur->prio || ctxt->prio <= cur->req_prio);
	WARN_ON_ONCE(cur->unsafe != true);

	do {
		new.atom = cur->atom;
		new.cpu			= cpu;
		new.prio		= ctxt->prio;
		new.unsafe		|= cur->unsafe_takeover;
		new.unsafe_takeover	|= cur->unsafe;

	} while (!nbcon_state_try_cmpxchg(con, cur, &new));

	return 0;
}

static struct printk_buffers panic_nbcon_pbufs;

/**
 * nbcon_context_try_acquire - Try to acquire nbcon console
 * @ctxt:	The context of the caller
 *
 * Return:	True if the console was acquired. False otherwise.
 *
 * If the caller allowed an unsafe hostile takeover, on success the
 * caller should check the current console state to see if it is
 * in an unsafe state. Otherwise, on success the caller may assume
 * the console is not in an unsafe state.
 */
__maybe_unused
static bool nbcon_context_try_acquire(struct nbcon_context *ctxt)
{
	unsigned int cpu = smp_processor_id();
	struct console *con = ctxt->console;
	struct nbcon_state cur;
	int err;

	nbcon_state_read(con, &cur);
try_again:
	err = nbcon_context_try_acquire_direct(ctxt, &cur);
	if (err != -EBUSY)
		goto out;

	err = nbcon_context_try_acquire_handover(ctxt, &cur);
	if (err == -EAGAIN)
		goto try_again;
	if (err != -EBUSY)
		goto out;

	err = nbcon_context_try_acquire_hostile(ctxt, &cur);
out:
	if (err)
		return false;

	/* Acquire succeeded. */

	/* Assign the appropriate buffer for this context. */
	if (atomic_read(&panic_cpu) == cpu)
		ctxt->pbufs = &panic_nbcon_pbufs;
	else
		ctxt->pbufs = con->pbufs;

	return true;
}

static bool nbcon_owner_matches(struct nbcon_state *cur, int expected_cpu,
				int expected_prio)
{
	/*
	 * Since consoles can only be acquired by higher priorities,
	 * owning contexts are uniquely identified by @prio. However,
	 * since contexts can unexpectedly lose ownership, it is
	 * possible that later another owner appears with the same
	 * priority. For this reason @cpu is also needed.
	 */

	if (cur->prio != expected_prio)
		return false;

	if (cur->cpu != expected_cpu)
		return false;

	return true;
}

/**
 * nbcon_context_release - Release the console
 * @ctxt:	The nbcon context from nbcon_context_try_acquire()
 */
__maybe_unused
static void nbcon_context_release(struct nbcon_context *ctxt)
{
	unsigned int cpu = smp_processor_id();
	struct console *con = ctxt->console;
	struct nbcon_state cur;
	struct nbcon_state new;

	nbcon_state_read(con, &cur);

	do {
		if (!nbcon_owner_matches(&cur, cpu, ctxt->prio))
			break;

		new.atom = cur.atom;
		new.prio = NBCON_PRIO_NONE;

		/*
		 * If @unsafe_takeover is set, it is kept set so that
		 * the state remains permanently unsafe.
		 */
		new.unsafe |= cur.unsafe_takeover;

	} while (!nbcon_state_try_cmpxchg(con, &cur, &new));

	ctxt->pbufs = NULL;
}

/**
 * nbcon_alloc - Allocate buffers needed by the nbcon console
 * @con:	Console to allocate buffers for
 *
 * Return:	True on success. False otherwise and the console cannot
 *		be used.
 *
 * This is not part of nbcon_init() because buffer allocation must
 * be performed earlier in the console registration process.
 */
bool nbcon_alloc(struct console *con)
{
	if (con->flags & CON_BOOT) {
		/*
		 * Boot console printing is synchronized with legacy console
		 * printing, so boot consoles can share the same global printk
		 * buffers.
		 */
		con->pbufs = &printk_shared_pbufs;
	} else {
		con->pbufs = kmalloc(sizeof(*con->pbufs), GFP_KERNEL);
		if (!con->pbufs) {
			con_printk(KERN_ERR, con, "failed to allocate printing buffer\n");
			return false;
		}
	}

	return true;
}

/**
 * nbcon_init - Initialize the nbcon console specific data
 * @con:	Console to initialize
 *
 * nbcon_alloc() *must* be called and succeed before this function
 * is called.
 */
void nbcon_init(struct console *con)
{
	struct nbcon_state state = { };

	/* nbcon_alloc() must have been called and successful! */
	BUG_ON(!con->pbufs);

	nbcon_state_set(con, &state);
}

/**
 * nbcon_free - Free and cleanup the nbcon console specific data
 * @con:	Console to free/cleanup nbcon data
 */
void nbcon_free(struct console *con)
{
	struct nbcon_state state = { };

	nbcon_state_set(con, &state);

	/* Boot consoles share global printk buffers. */
	if (!(con->flags & CON_BOOT))
		kfree(con->pbufs);

	con->pbufs = NULL;
}
