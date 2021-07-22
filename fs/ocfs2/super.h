/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * super.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_SUPER_H
#define OCFS2_SUPER_H

__printf(3, 4)
int __ocfs2_error(struct super_block *sb, const char *function,
		   const char *fmt, ...);

#define ocfs2_error(sb, fmt, ...)					\
	__ocfs2_error(sb, __PRETTY_FUNCTION__, fmt, ##__VA_ARGS__)

__printf(3, 4)
void __ocfs2_abort(struct super_block *sb, const char *function,
		   const char *fmt, ...);

#define ocfs2_abort(sb, fmt, ...)					\
	__ocfs2_abort(sb, __PRETTY_FUNCTION__, fmt, ##__VA_ARGS__)

/*
 * Void signal blockers, because in-kernel sigprocmask() only fails
 * when SIG_* is wrong.
 */
void ocfs2_block_signals(sigset_t *oldset);
void ocfs2_unblock_signals(sigset_t *oldset);

#endif /* OCFS2_SUPER_H */
