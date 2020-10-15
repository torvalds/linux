// SPDX-License-Identifier: GPL-2.0

/*
 * fs/ext4/fast_commit.c
 *
 * Written by Harshad Shirwadkar <harshadshirwadkar@gmail.com>
 *
 * Ext4 fast commits routines.
 */
#include "ext4_jbd2.h"
/*
 * Fast commit cleanup routine. This is called after every fast commit and
 * full commit. full is true if we are called after a full commit.
 */
static void ext4_fc_cleanup(journal_t *journal, int full)
{
}

void ext4_fc_init(struct super_block *sb, journal_t *journal)
{
	if (!test_opt2(sb, JOURNAL_FAST_COMMIT))
		return;
	journal->j_fc_cleanup_callback = ext4_fc_cleanup;
	if (jbd2_fc_init(journal, EXT4_NUM_FC_BLKS)) {
		pr_warn("Error while enabling fast commits, turning off.");
		ext4_clear_feature_fast_commit(sb);
	}
}
