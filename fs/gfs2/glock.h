/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __GLOCK_DOT_H__
#define __GLOCK_DOT_H__

#include <linux/sched.h>
#include <linux/parser.h>
#include "incore.h"
#include "util.h"

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
 *
 * LM_FLAG_NODE_SCOPE
 * This holder agrees to share the lock within this node. In other words,
 * the glock is held in EX mode according to DLM, but local holders on the
 * same node can share it.
 */

#define LM_FLAG_TRY		0x0001
#define LM_FLAG_TRY_1CB		0x0002
#define LM_FLAG_NOEXP		0x0004
#define LM_FLAG_ANY		0x0008
#define LM_FLAG_PRIORITY	0x0010
#define LM_FLAG_NODE_SCOPE	0x0020
#define GL_ASYNC		0x0040
#define GL_EXACT		0x0080
#define GL_SKIP			0x0100
#define GL_NOCACHE		0x0400
  
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
extern void gfs2_glock_hold(struct gfs2_glock *gl);
extern void gfs2_glock_put(struct gfs2_glock *gl);
extern void gfs2_glock_queue_put(struct gfs2_glock *gl);
extern void gfs2_holder_init(struct gfs2_glock *gl, unsigned int state,
			     u16 flags, struct gfs2_holder *gh);
extern void gfs2_holder_reinit(unsigned int state, u16 flags,
			       struct gfs2_holder *gh);
extern void gfs2_holder_uninit(struct gfs2_holder *gh);
extern int gfs2_glock_nq(struct gfs2_holder *gh);
extern int gfs2_glock_poll(struct gfs2_holder *gh);
extern int gfs2_glock_wait(struct gfs2_holder *gh);
extern int gfs2_glock_async_wait(unsigned int num_gh, struct gfs2_holder *ghs);
extern void gfs2_glock_dq(struct gfs2_holder *gh);
extern void gfs2_glock_dq_wait(struct gfs2_holder *gh);
extern void gfs2_glock_dq_uninit(struct gfs2_holder *gh);
extern int gfs2_glock_nq_num(struct gfs2_sbd *sdp, u64 number,
			     const struct gfs2_glock_operations *glops,
			     unsigned int state, u16 flags,
			     struct gfs2_holder *gh);
extern int gfs2_glock_nq_m(unsigned int num_gh, struct gfs2_holder *ghs);
extern void gfs2_glock_dq_m(unsigned int num_gh, struct gfs2_holder *ghs);
extern void gfs2_dump_glock(struct seq_file *seq, struct gfs2_glock *gl,
			    bool fsid);
#define GLOCK_BUG_ON(gl,x) do { if (unlikely(x)) {		\
			gfs2_dump_glock(NULL, gl, true);	\
			BUG(); } } while(0)
#define gfs2_glock_assert_warn(gl, x) do { if (unlikely(!(x))) {	\
			gfs2_dump_glock(NULL, gl, true);		\
			gfs2_assert_warn((gl)->gl_name.ln_sbd, (x)); } } \
	while (0)
#define gfs2_glock_assert_withdraw(gl, x) do { if (unlikely(!(x))) {	\
			gfs2_dump_glock(NULL, gl, true);		\
			gfs2_assert_withdraw((gl)->gl_name.ln_sbd, (x)); } } \
	while (0)

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
				     unsigned int state, u16 flags,
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
extern bool gfs2_queue_delete_work(struct gfs2_glock *gl, unsigned long delay);
extern void gfs2_cancel_delete_work(struct gfs2_glock *gl);
extern bool gfs2_delete_work_queued(const struct gfs2_glock *gl);
extern void gfs2_flush_delete_work(struct gfs2_sbd *sdp);
extern void gfs2_gl_hash_clear(struct gfs2_sbd *sdp);
extern void gfs2_glock_finish_truncate(struct gfs2_inode *ip);
extern void gfs2_glock_thaw(struct gfs2_sbd *sdp);
extern void gfs2_glock_add_to_lru(struct gfs2_glock *gl);
extern void gfs2_glock_free(struct gfs2_glock *gl);

extern int __init gfs2_glock_init(void);
extern void gfs2_glock_exit(void);

extern void gfs2_create_debugfs_file(struct gfs2_sbd *sdp);
extern void gfs2_delete_debugfs_file(struct gfs2_sbd *sdp);
extern void gfs2_register_debugfs(void);
extern void gfs2_unregister_debugfs(void);

extern const struct lm_lockops gfs2_dlm_ops;

static inline void gfs2_holder_mark_uninitialized(struct gfs2_holder *gh)
{
	gh->gh_gl = NULL;
}

static inline bool gfs2_holder_initialized(struct gfs2_holder *gh)
{
	return gh->gh_gl;
}

static inline bool gfs2_holder_queued(struct gfs2_holder *gh)
{
	return !list_empty(&gh->gh_list);
}

/**
 * glock_set_object - set the gl_object field of a glock
 * @gl: the glock
 * @object: the object
 */
static inline void glock_set_object(struct gfs2_glock *gl, void *object)
{
	spin_lock(&gl->gl_lockref.lock);
	if (gfs2_assert_warn(gl->gl_name.ln_sbd, gl->gl_object == NULL))
		gfs2_dump_glock(NULL, gl, true);
	gl->gl_object = object;
	spin_unlock(&gl->gl_lockref.lock);
}

/**
 * glock_clear_object - clear the gl_object field of a glock
 * @gl: the glock
 * @object: the object
 *
 * I'd love to similarly add this:
 *	else if (gfs2_assert_warn(gl->gl_sbd, gl->gl_object == object))
 *		gfs2_dump_glock(NULL, gl, true);
 * Unfortunately, that's not possible because as soon as gfs2_delete_inode
 * frees the block in the rgrp, another process can reassign it for an I_NEW
 * inode in gfs2_create_inode because that calls new_inode, not gfs2_iget.
 * That means gfs2_delete_inode may subsequently try to call this function
 * for a glock that's already pointing to a brand new inode. If we clear the
 * new inode's gl_object, we'll introduce metadata corruption. Function
 * gfs2_delete_inode calls clear_inode which calls gfs2_clear_inode which also
 * tries to clear gl_object, so it's more than just gfs2_delete_inode.
 *
 */
static inline void glock_clear_object(struct gfs2_glock *gl, void *object)
{
	spin_lock(&gl->gl_lockref.lock);
	if (gl->gl_object == object)
		gl->gl_object = NULL;
	spin_unlock(&gl->gl_lockref.lock);
}

extern void gfs2_inode_remember_delete(struct gfs2_glock *gl, u64 generation);
extern bool gfs2_inode_already_deleted(struct gfs2_glock *gl, u64 generation);

#endif /* __GLOCK_DOT_H__ */
