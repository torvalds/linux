#include "reiserfs.h"
#include <linux/mutex.h>

/*
 * The previous reiserfs locking scheme was heavily based on
 * the tricky properties of the Bkl:
 *
 * - it was acquired recursively by a same task
 * - the performances relied on the release-while-schedule() property
 *
 * Now that we replace it by a mutex, we still want to keep the same
 * recursive property to avoid big changes in the code structure.
 * We use our own lock_owner here because the owner field on a mutex
 * is only available in SMP or mutex debugging, also we only need this field
 * for this mutex, no need for a system wide mutex facility.
 *
 * Also this lock is often released before a call that could block because
 * reiserfs performances were partially based on the release while schedule()
 * property of the Bkl.
 */
void reiserfs_write_lock(struct super_block *s)
{
	struct reiserfs_sb_info *sb_i = REISERFS_SB(s);

	if (sb_i->lock_owner != current) {
		mutex_lock(&sb_i->lock);
		sb_i->lock_owner = current;
	}

	/* No need to protect it, only the current task touches it */
	sb_i->lock_depth++;
}

void reiserfs_write_unlock(struct super_block *s)
{
	struct reiserfs_sb_info *sb_i = REISERFS_SB(s);

	/*
	 * Are we unlocking without even holding the lock?
	 * Such a situation must raise a BUG() if we don't want
	 * to corrupt the data.
	 */
	BUG_ON(sb_i->lock_owner != current);

	if (--sb_i->lock_depth == -1) {
		sb_i->lock_owner = NULL;
		mutex_unlock(&sb_i->lock);
	}
}

int __must_check reiserfs_write_unlock_nested(struct super_block *s)
{
	struct reiserfs_sb_info *sb_i = REISERFS_SB(s);
	int depth;

	/* this can happen when the lock isn't always held */
	if (sb_i->lock_owner != current)
		return -1;

	depth = sb_i->lock_depth;

	sb_i->lock_depth = -1;
	sb_i->lock_owner = NULL;
	mutex_unlock(&sb_i->lock);

	return depth;
}

void reiserfs_write_lock_nested(struct super_block *s, int depth)
{
	struct reiserfs_sb_info *sb_i = REISERFS_SB(s);

	/* this can happen when the lock isn't always held */
	if (depth == -1)
		return;

	mutex_lock(&sb_i->lock);
	sb_i->lock_owner = current;
	sb_i->lock_depth = depth;
}

/*
 * Utility function to force a BUG if it is called without the superblock
 * write lock held.  caller is the string printed just before calling BUG()
 */
void reiserfs_check_lock_depth(struct super_block *sb, char *caller)
{
	struct reiserfs_sb_info *sb_i = REISERFS_SB(sb);

	WARN_ON(sb_i->lock_depth < 0);
}

#ifdef CONFIG_REISERFS_CHECK
void reiserfs_lock_check_recursive(struct super_block *sb)
{
	struct reiserfs_sb_info *sb_i = REISERFS_SB(sb);

	WARN_ONCE((sb_i->lock_depth > 0), "Unwanted recursive reiserfs lock!\n");
}
#endif
