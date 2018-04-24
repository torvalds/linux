// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/bug.h>
#include <linux/atomic.h>
#include <linux/errseq.h>

/*
 * An errseq_t is a way of recording errors in one place, and allowing any
 * number of "subscribers" to tell whether it has changed since a previous
 * point where it was sampled.
 *
 * It's implemented as an unsigned 32-bit value. The low order bits are
 * designated to hold an error code (between 0 and -MAX_ERRNO). The upper bits
 * are used as a counter. This is done with atomics instead of locking so that
 * these functions can be called from any context.
 *
 * The general idea is for consumers to sample an errseq_t value. That value
 * can later be used to tell whether any new errors have occurred since that
 * sampling was done.
 *
 * Note that there is a risk of collisions if new errors are being recorded
 * frequently, since we have so few bits to use as a counter.
 *
 * To mitigate this, one bit is used as a flag to tell whether the value has
 * been sampled since a new value was recorded. That allows us to avoid bumping
 * the counter if no one has sampled it since the last time an error was
 * recorded.
 *
 * A new errseq_t should always be zeroed out.  A errseq_t value of all zeroes
 * is the special (but common) case where there has never been an error. An all
 * zero value thus serves as the "epoch" if one wishes to know whether there
 * has ever been an error set since it was first initialized.
 */

/* The low bits are designated for error code (max of MAX_ERRNO) */
#define ERRSEQ_SHIFT		ilog2(MAX_ERRNO + 1)

/* This bit is used as a flag to indicate whether the value has been seen */
#define ERRSEQ_SEEN		(1 << ERRSEQ_SHIFT)

/* The lowest bit of the counter */
#define ERRSEQ_CTR_INC		(1 << (ERRSEQ_SHIFT + 1))

/**
 * errseq_set - set a errseq_t for later reporting
 * @eseq: errseq_t field that should be set
 * @err: error to set (must be between -1 and -MAX_ERRNO)
 *
 * This function sets the error in *eseq, and increments the sequence counter
 * if the last sequence was sampled at some point in the past.
 *
 * Any error set will always overwrite an existing error.
 *
 * We do return the latest value here, primarily for debugging purposes. The
 * return value should not be used as a previously sampled value in later calls
 * as it will not have the SEEN flag set.
 */
errseq_t errseq_set(errseq_t *eseq, int err)
{
	errseq_t cur, old;

	/* MAX_ERRNO must be able to serve as a mask */
	BUILD_BUG_ON_NOT_POWER_OF_2(MAX_ERRNO + 1);

	/*
	 * Ensure the error code actually fits where we want it to go. If it
	 * doesn't then just throw a warning and don't record anything. We
	 * also don't accept zero here as that would effectively clear a
	 * previous error.
	 */
	old = READ_ONCE(*eseq);

	if (WARN(unlikely(err == 0 || (unsigned int)-err > MAX_ERRNO),
				"err = %d\n", err))
		return old;

	for (;;) {
		errseq_t new;

		/* Clear out error bits and set new error */
		new = (old & ~(MAX_ERRNO|ERRSEQ_SEEN)) | -err;

		/* Only increment if someone has looked at it */
		if (old & ERRSEQ_SEEN)
			new += ERRSEQ_CTR_INC;

		/* If there would be no change, then call it done */
		if (new == old) {
			cur = new;
			break;
		}

		/* Try to swap the new value into place */
		cur = cmpxchg(eseq, old, new);

		/*
		 * Call it success if we did the swap or someone else beat us
		 * to it for the same value.
		 */
		if (likely(cur == old || cur == new))
			break;

		/* Raced with an update, try again */
		old = cur;
	}
	return cur;
}
EXPORT_SYMBOL(errseq_set);

/**
 * errseq_sample - grab current errseq_t value
 * @eseq: pointer to errseq_t to be sampled
 *
 * This function allows callers to initialise their errseq_t variable.
 * If the error has been "seen", new callers will not see an old error.
 * If there is an unseen error in @eseq, the caller of this function will
 * see it the next time it checks for an error.
 *
 * Context: Any context.
 * Return: The current errseq value.
 */
errseq_t errseq_sample(errseq_t *eseq)
{
	errseq_t old = READ_ONCE(*eseq);

	/* If nobody has seen this error yet, then we can be the first. */
	if (!(old & ERRSEQ_SEEN))
		old = 0;
	return old;
}
EXPORT_SYMBOL(errseq_sample);

/**
 * errseq_check - has an error occurred since a particular sample point?
 * @eseq: pointer to errseq_t value to be checked
 * @since: previously-sampled errseq_t from which to check
 *
 * Grab the value that eseq points to, and see if it has changed "since"
 * the given value was sampled. The "since" value is not advanced, so there
 * is no need to mark the value as seen.
 *
 * Returns the latest error set in the errseq_t or 0 if it hasn't changed.
 */
int errseq_check(errseq_t *eseq, errseq_t since)
{
	errseq_t cur = READ_ONCE(*eseq);

	if (likely(cur == since))
		return 0;
	return -(cur & MAX_ERRNO);
}
EXPORT_SYMBOL(errseq_check);

/**
 * errseq_check_and_advance - check an errseq_t and advance to current value
 * @eseq: pointer to value being checked and reported
 * @since: pointer to previously-sampled errseq_t to check against and advance
 *
 * Grab the eseq value, and see whether it matches the value that "since"
 * points to. If it does, then just return 0.
 *
 * If it doesn't, then the value has changed. Set the "seen" flag, and try to
 * swap it into place as the new eseq value. Then, set that value as the new
 * "since" value, and return whatever the error portion is set to.
 *
 * Note that no locking is provided here for concurrent updates to the "since"
 * value. The caller must provide that if necessary. Because of this, callers
 * may want to do a lockless errseq_check before taking the lock and calling
 * this.
 */
int errseq_check_and_advance(errseq_t *eseq, errseq_t *since)
{
	int err = 0;
	errseq_t old, new;

	/*
	 * Most callers will want to use the inline wrapper to check this,
	 * so that the common case of no error is handled without needing
	 * to take the lock that protects the "since" value.
	 */
	old = READ_ONCE(*eseq);
	if (old != *since) {
		/*
		 * Set the flag and try to swap it into place if it has
		 * changed.
		 *
		 * We don't care about the outcome of the swap here. If the
		 * swap doesn't occur, then it has either been updated by a
		 * writer who is altering the value in some way (updating
		 * counter or resetting the error), or another reader who is
		 * just setting the "seen" flag. Either outcome is OK, and we
		 * can advance "since" and return an error based on what we
		 * have.
		 */
		new = old | ERRSEQ_SEEN;
		if (new != old)
			cmpxchg(eseq, old, new);
		*since = new;
		err = -(new & MAX_ERRNO);
	}
	return err;
}
EXPORT_SYMBOL(errseq_check_and_advance);
