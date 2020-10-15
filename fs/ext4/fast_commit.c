// SPDX-License-Identifier: GPL-2.0

/*
 * fs/ext4/fast_commit.c
 *
 * Written by Harshad Shirwadkar <harshadshirwadkar@gmail.com>
 *
 * Ext4 fast commits routines.
 */
#include "ext4_jbd2.h"

void ext4_fc_init(struct super_block *sb, journal_t *journal)
{
	if (!test_opt2(sb, JOURNAL_FAST_COMMIT))
		return;
	if (jbd2_fc_init(journal, EXT4_NUM_FC_BLKS)) {
		pr_warn("Error while enabling fast commits, turning off.");
		ext4_clear_feature_fast_commit(sb);
	}
}
