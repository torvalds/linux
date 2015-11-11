/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __GLOCK_DOT_H__
#define __GLOCK_DOT_H__

#include <linux/sched.h>
#include <linux/parser.h>
#include "incore.h"

/* Options for hostdata parser */

enum {
	Opt_jid,
	Opt_id,
	Opt_first,
	Opt_nodir,
	Opt_err,
};

/*
 * lm_lockname types
 */

#define LM_TYPE_RESERVED	0x00
#define LM_TYPE_NONDISK		0x01
#define LM_TYPE_INODE		0x02
#define LM_TYPE_RGRP		0x03
#define LM_TYPE_META		0x04
#define LM_TYPE_IOPEN		0x05
#define LM_TYPE_FLOCK		0x06
#define LM_TYPE_PLOCK		0x07
#define LM_TYPE_QUOTA		0x08
#define LM_TYPE_JOURNAL		0x09

/*
 * lm_lock() states
 *
 * SHARED is compatible with SHARED, not with DEFERRED or EX.
 * DEFERRED is compatible with DEFERRED, not with SHARED or EX.
 */

#define LM_ST_UNLOCKED		0
#define LM_ST_EXCLUSIVE		1
#define LM_ST_DEFERRED		2
#define LM_ST_SHARED		3

/*
 * lm_lock() flags
 *
 * LM_FLAG_TRY
 * Don't wait to acquire the lock if it can't be granted immediately.
 *
 * LM_FLAG_TRY_1CB
 * Send one blocking callback if TRY is set and the lock is not granted.
 *
 * LM_FLAG_NOEXP
 * GFS sets this flag on lock requests it makes while doing journal recovery.
 * These special requests should not be blocked due to the recovery like
 * ordinary locks would be.
 *
 * LM_FLAG_ANY
 * A SHARED request may also be granted in DEFERRED, or a DEFERRED request may
 * also be granted in SHARED.  The preferred state is whichever is compatible
 * with other granted locks, or the specified state if no other locks exist.
 *
 * LM_FLAG_PRIORITY
 * Override fairness considerations.  Suppose a lock is held in a shared state
 * and there is a pending request for the deferred state.  A shared lock
 * request with the priority flag would be allowed to bypass the deferred
 * request and directly join the other shared lock.  A shared lock request
 * without the priority flag might be forced to wait until the deferred
 * requested had acquired and released the lock.
 */

#define LM_FLAG_TRY		0x00000001
#define LM_FLAG_TRY_1CB		0x00000002
#define LM_FLAG_NOEXP		0x00000004
#define LM_FLAG_ANY		0x00000008
#define LM_FLAG_PRIORITY	0x00000010
#define GL_ASYNC		0x00000040
#define GL_EXACT		0x00000080
#define GL_SKIP			0x00000100
#define GL_NOCACHE		0x00000400
  
/*
 * lm_async_cb return flags
 *
 * LM_OUT_ST_MASK
 * Masks the lower two bits of lock state in the returned value.
 *
 * LM_OUT_CANCELED
 * The lock request was canceled.
 *
 */

#define LM_OUT_ST_MASK		0x00000003
#define LM_OUT_CANCELED		0x00000008
#define LM_OUT_ERROR		0x00000004

/*
 * lm_recovery_done() messages
 */

#define LM_RD_GAVEUP		308
#define LM_RD_SUCCESS		309

#define GLR_TRYFAILED		13

#define GL_GLOCK_MAX_HOLD        (long)(HZ / 5)
#define GL_GLOCK_DFT_HOLD        (long)(HZ / 5)
#define GL_GLOCK_MIN_HOLD        (long)(10)
#define GL_GLOCK_HOLD_INCR       (long)(HZ / 20)
#define GL_GLOCK_HOLD_DECR       (long)(HZ / 40)

struct lm_lockops {
	const char *lm_proto_name;
	int (*lm_mount) (struct gfs2_sbd *sdp, const char *table);
	void (*lm_first_done) (struct gfs2_sbd *sdp);
	void (*lm_recovery_result) (struct gfs2_sbd *sdp, unsigned int jid,
				    unsigned int result);
	void (*lm_unmount) (struct gfs2_sbd *sdp);
	void (*lm_withdraw) (struct gfs2_sbd *sdp);
	void (*lm_put_lock) (struct gfs2_glock *gl);
	int (*lm_lock) (struct gfs2_glock *gl, unsigned int req_state,
			unsigned int flags);
	void (*lm_cancel) (struct gfs2_glock *gl);
	const match_table_t *lm_tokens;
};

extern struct workqueue_struct *gfs2_delete_workqueue;
static inline struct gfs2_holder *gfs2_glock_is_locked_by_me(struct gfs2_glock *gl)
{
	struct gfs2_holder *gh;
	struct pid *pid;

	/* Look in glock's list of holders for one with current task as owner */
	spin_lock(&gl->gl_lockref.lock);
	pid = task_pid(current);
	list_for_each_entry(gh, &gl->gl_holders, gh_list) {
		if (!test_bit(HIF_HOLDER, &gh->gh_iflags))
			break;
		if (gh->gh_owner_pid == pid)
			goto out;
	}
	gh = NULL;
out:
	spin_unlock(&gl->gl_lockref.lock);

	return gh;
}

static inline int gfs2_glock_is_held_excl(struct gfs2_glock *gl)
{
	return gl->gl_state == LM_ST_EXCLUSIVE;
}

static inline int gfs2_glock_is_held_dfrd(struct gfs2_glock *gl)
{
	return gl->gl_state == LM_ST_DEFERRED;
}

static inline int gfs2_glock_is_held_shrd(struct gfs2_glock *gl)
{
	return gl->gl_state == LM_ST_SHARED;
}

static inline struct address_space *gfs2_glock2aspace(struct gfs2_glock *gl)
{
	if (gl->gl_ops->go_flags & GLOF_ASPACE)
		return (struct address_space *)(gl + 1);
	return NULL;
}

extern int gfs2_glock_get(struct gfs2_sbd *sdp, u64 number,
			  const struct gfs2_glock_operations *glops,
			  int create, struct gfs2_glock **glp);
extern void gfs2_glock_put(struct gfs2_glock *gl);
extern void gfs2_holder_init(struct gfs2_glock *gl, unsigned int state,
			     unsigned flags, struct gfs2_holder *gh);
extern void gfs2_holder_reinit(unsigned int state, unsigned flags,
			       struct gfs2_holder *gh);
extern void gfs2_holder_uninit(struct gfs2_holder *gh);
extern int gfs2_glock_nq(struct gfs2_holder *gh);
extern int gfs2_glock_poll(struct gfs2_holder *gh);
extern int gfs2_glock_wait(struct gfs2_holder *gh);
extern void gfs2_glock_dq(struct gfs2_holder *gh);
extern void gfs2_glock_dq_wait(struct gfs2_holder *gh);
extern void gfs2_glock_dq_uninit(struct gfs2_holder *gh);
extern int gfs2_glock_nq_num(struct gfs2_sbd *sdp, u64 number,
			     const struct gfs2_glock_operations *glops,
			     unsigned int state, int flags,
			     struct gfs2_holder *gh);
extern int gfs2_glock_nq_m(unsigned int num_gh, struct gfs2_holder *ghs);
extern void gfs2_glock_dq_m(unsigned int num_gh, struct gfs2_holder *ghs);
extern void gfs2_dump_glock(struct seq_file *seq, const struct gfs2_glock *gl);
#define GLOCK_BUG_ON(gl,x) do { if (unlikely(x)) { gfs2_dump_glock(NULL, gl); BUG(); } } while(0)
extern __printf(2, 3)
void gfs2_print_dbg(struct seq_file *seq, const char *fmt, ...);

/**
 * gfs2_glock_nq_init - initialize a holder and enqueue it on a glock
 * @gl: the glock
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 * Returns: 0, GLR_*, or errno
 */

static inline int gfs2_glock_nq_init(struct gfs2_glock *gl,
				     unsigned int state, int flags,
				     struct gfs2_holder *gh)
{
	int error;

	gfs2_holder_init(gl, state, flags, gh);

	error = gfs2_glock_nq(gh);
	if (error)
		gfs2_holder_uninit(gh);

	return error;
}

extern void gfs2_glock_cb(struct gfs2_glock *gl, unsigned int state);
extern void gfs2_glock_complete(struct gfs2_glock *gl, int ret);
extern void gfs2_gl_hash_clear(struct gfs2_sbd *sdp);
extern void gfs2_glock_finish_truncate(struct gfs2_inode *ip);
extern void gfs2_glock_thaw(struct gfs2_sbd *sdp);
extern void gfs2_glock_add_to_lru(struct gfs2_glock *gl);
extern void gfs2_glock_free(struct gfs2_glock *gl);

extern int __init gfs2_glock_init(void);
extern void gfs2_glock_exit(void);

extern int gfs2_create_debugfs_file(struct gfs2_sbd *sdp);
extern void gfs2_delete_debugfs_file(struct gfs2_sbd *sdp);
extern int gfs2_register_debugfs(void);
extern void gfs2_unregister_debugfs(void);

extern const struct lm_lockops gfs2_dlm_ops;

#endif /* __GLOCK_DOT_H__ */
