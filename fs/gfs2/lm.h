/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __LM_DOT_H__
#define __LM_DOT_H__

struct gfs2_sbd;

#define GFS2_MIN_LVB_SIZE 32

int gfs2_lm_mount(struct gfs2_sbd *sdp, int silent);
void gfs2_lm_others_may_mount(struct gfs2_sbd *sdp);
void gfs2_lm_unmount(struct gfs2_sbd *sdp);
int gfs2_lm_withdraw(struct gfs2_sbd *sdp, char *fmt, ...)
				__attribute__ ((format(printf, 2, 3)));
int gfs2_lm_get_lock(struct gfs2_sbd *sdp, struct lm_lockname *name,
		     void **lockp);
void gfs2_lm_put_lock(struct gfs2_sbd *sdp, void *lock);
unsigned int gfs2_lm_lock(struct gfs2_sbd *sdp, void *lock,
			 unsigned int cur_state, unsigned int req_state,
			 unsigned int flags);
unsigned int gfs2_lm_unlock(struct gfs2_sbd *sdp, void *lock,
			   unsigned int cur_state);
void gfs2_lm_cancel(struct gfs2_sbd *sdp, void *lock);
int gfs2_lm_hold_lvb(struct gfs2_sbd *sdp, void *lock, char **lvbp);
void gfs2_lm_unhold_lvb(struct gfs2_sbd *sdp, void *lock, char *lvb);
int gfs2_lm_plock_get(struct gfs2_sbd *sdp, struct lm_lockname *name,
		      struct file *file, struct file_lock *fl);
int gfs2_lm_plock(struct gfs2_sbd *sdp, struct lm_lockname *name,
		  struct file *file, int cmd, struct file_lock *fl);
int gfs2_lm_punlock(struct gfs2_sbd *sdp, struct lm_lockname *name,
		    struct file *file, struct file_lock *fl);
void gfs2_lm_recovery_done(struct gfs2_sbd *sdp, unsigned int jid,
			   unsigned int message);

#endif /* __LM_DOT_H__ */
