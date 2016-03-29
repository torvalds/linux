/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * super.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef OCFS2_SUPER_H
#define OCFS2_SUPER_H

int ocfs2_publish_get_mount_state(struct ocfs2_super *osb,
				  int node_num);

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
