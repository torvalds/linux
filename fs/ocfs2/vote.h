/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * vote.h
 *
 * description here
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


#ifndef VOTE_H
#define VOTE_H

int ocfs2_vote_thread(void *arg);
static inline void ocfs2_kick_vote_thread(struct ocfs2_super *osb)
{
	spin_lock(&osb->vote_task_lock);
	/* make sure the voting thread gets a swipe at whatever changes
	 * the caller may have made to the voting state */
	osb->vote_wake_sequence++;
	spin_unlock(&osb->vote_task_lock);
	wake_up(&osb->vote_event);
}

int ocfs2_request_mount_vote(struct ocfs2_super *osb);
int ocfs2_request_umount_vote(struct ocfs2_super *osb);
int ocfs2_register_net_handlers(struct ocfs2_super *osb);
void ocfs2_unregister_net_handlers(struct ocfs2_super *osb);

void ocfs2_remove_node_from_vote_queues(struct ocfs2_super *osb,
					int node_num);
#endif
