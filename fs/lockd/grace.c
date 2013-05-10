/*
 * Common code for control of lockd and nfsv4 grace periods.
 */

#include <linux/module.h>
#include <linux/lockd/bind.h>

static LIST_HEAD(grace_list);
static DEFINE_SPINLOCK(grace_lock);

/**
 * locks_start_grace
 * @lm: who this grace period is for
 *
 * A grace period is a period during which locks should not be given
 * out.  Currently grace periods are only enforced by the two lock
 * managers (lockd and nfsd), using the locks_in_grace() function to
 * check when they are in a grace period.
 *
 * This function is called to start a grace period.
 */
void locks_start_grace(struct lock_manager *lm)
{
	spin_lock(&grace_lock);
	list_add(&lm->list, &grace_list);
	spin_unlock(&grace_lock);
}
EXPORT_SYMBOL_GPL(locks_start_grace);

/**
 * locks_end_grace
 * @lm: who this grace period is for
 *
 * Call this function to state that the given lock manager is ready to
 * resume regular locking.  The grace period will not end until all lock
 * managers that called locks_start_grace() also call locks_end_grace().
 * Note that callers count on it being safe to call this more than once,
 * and the second call should be a no-op.
 */
void locks_end_grace(struct lock_manager *lm)
{
	spin_lock(&grace_lock);
	list_del_init(&lm->list);
	spin_unlock(&grace_lock);
}
EXPORT_SYMBOL_GPL(locks_end_grace);

/**
 * locks_in_grace
 *
 * Lock managers call this function to determine when it is OK for them
 * to answer ordinary lock requests, and when they should accept only
 * lock reclaims.
 */
int locks_in_grace(void)
{
	return !list_empty(&grace_list);
}
EXPORT_SYMBOL_GPL(locks_in_grace);
