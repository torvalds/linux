/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __GLOCK_DOT_H__
#define __GLOCK_DOT_H__

/* Flags for lock requests; used in gfs2_holder gh_flag field.
   From lm_interface.h:
#define LM_FLAG_TRY		0x00000001
#define LM_FLAG_TRY_1CB		0x00000002
#define LM_FLAG_NOEXP		0x00000004
#define LM_FLAG_ANY		0x00000008
#define LM_FLAG_PRIORITY	0x00000010 */

#define GL_LOCAL_EXCL		0x00000020
#define GL_ASYNC		0x00000040
#define GL_EXACT		0x00000080
#define GL_SKIP			0x00000100
#define GL_ATIME		0x00000200
#define GL_NOCACHE		0x00000400
#define GL_SYNC			0x00000800
#define GL_NOCANCEL		0x00001000
#define GL_NEVER_RECURSE	0x00002000

#define GLR_TRYFAILED		13
#define GLR_CANCELED		14

static inline int gfs2_glock_is_locked_by_me(struct gfs2_glock *gl)
{
	struct gfs2_holder *gh;
	int locked = 0;

	/* Look in glock's list of holders for one with current task as owner */
	spin_lock(&gl->gl_spin);
	list_for_each_entry(gh, &gl->gl_holders, gh_list) {
		if (gh->gh_owner == current) {
			locked = 1;
			break;
		}
	}
	spin_unlock(&gl->gl_spin);

	return locked;
}

static inline int gfs2_glock_is_held_excl(struct gfs2_glock *gl)
{
	return (gl->gl_state == LM_ST_EXCLUSIVE);
}

static inline int gfs2_glock_is_held_dfrd(struct gfs2_glock *gl)
{
	return (gl->gl_state == LM_ST_DEFERRED);
}

static inline int gfs2_glock_is_held_shrd(struct gfs2_glock *gl)
{
	return (gl->gl_state == LM_ST_SHARED);
}

static inline int gfs2_glock_is_blocking(struct gfs2_glock *gl)
{
	int ret;
	spin_lock(&gl->gl_spin);
	ret = !list_empty(&gl->gl_waiters2) || !list_empty(&gl->gl_waiters3);
	spin_unlock(&gl->gl_spin);
	return ret;
}

struct gfs2_glock *gfs2_glock_find(struct gfs2_sbd *sdp,
				   struct lm_lockname *name);
int gfs2_glock_get(struct gfs2_sbd *sdp,
		   uint64_t number, struct gfs2_glock_operations *glops,
		   int create, struct gfs2_glock **glp);
void gfs2_glock_hold(struct gfs2_glock *gl);
int gfs2_glock_put(struct gfs2_glock *gl);

void gfs2_holder_init(struct gfs2_glock *gl, unsigned int state, int flags,
		      struct gfs2_holder *gh);
void gfs2_holder_reinit(unsigned int state, int flags, struct gfs2_holder *gh);
void gfs2_holder_uninit(struct gfs2_holder *gh);
struct gfs2_holder *gfs2_holder_get(struct gfs2_glock *gl, unsigned int state,
				    int flags, gfp_t gfp_flags);
void gfs2_holder_put(struct gfs2_holder *gh);

void gfs2_glock_xmote_th(struct gfs2_glock *gl, unsigned int state, int flags);
void gfs2_glock_drop_th(struct gfs2_glock *gl);

void gfs2_glmutex_lock(struct gfs2_glock *gl);
int gfs2_glmutex_trylock(struct gfs2_glock *gl);
void gfs2_glmutex_unlock(struct gfs2_glock *gl);

int gfs2_glock_nq(struct gfs2_holder *gh);
int gfs2_glock_poll(struct gfs2_holder *gh);
int gfs2_glock_wait(struct gfs2_holder *gh);
void gfs2_glock_dq(struct gfs2_holder *gh);

void gfs2_glock_prefetch(struct gfs2_glock *gl, unsigned int state, int flags);
void gfs2_glock_force_drop(struct gfs2_glock *gl);

int gfs2_glock_be_greedy(struct gfs2_glock *gl, unsigned int time);

int gfs2_glock_nq_init(struct gfs2_glock *gl, unsigned int state, int flags,
		       struct gfs2_holder *gh);
void gfs2_glock_dq_uninit(struct gfs2_holder *gh);
int gfs2_glock_nq_num(struct gfs2_sbd *sdp,
		      uint64_t number, struct gfs2_glock_operations *glops,
		      unsigned int state, int flags, struct gfs2_holder *gh);

int gfs2_glock_nq_m(unsigned int num_gh, struct gfs2_holder *ghs);
void gfs2_glock_dq_m(unsigned int num_gh, struct gfs2_holder *ghs);
void gfs2_glock_dq_uninit_m(unsigned int num_gh, struct gfs2_holder *ghs);

void gfs2_glock_prefetch_num(struct gfs2_sbd *sdp, uint64_t number,
			     struct gfs2_glock_operations *glops,
			     unsigned int state, int flags);

/*  Lock Value Block functions  */

int gfs2_lvb_hold(struct gfs2_glock *gl);
void gfs2_lvb_unhold(struct gfs2_glock *gl);
void gfs2_lvb_sync(struct gfs2_glock *gl);

void gfs2_glock_cb(lm_fsdata_t *fsdata, unsigned int type, void *data);

void gfs2_try_toss_inode(struct gfs2_sbd *sdp, struct gfs2_inum *inum);
void gfs2_iopen_go_callback(struct gfs2_glock *gl, unsigned int state);

void gfs2_glock_schedule_for_reclaim(struct gfs2_glock *gl);
void gfs2_reclaim_glock(struct gfs2_sbd *sdp);

void gfs2_scand_internal(struct gfs2_sbd *sdp);
void gfs2_gl_hash_clear(struct gfs2_sbd *sdp, int wait);

int gfs2_dump_lockstate(struct gfs2_sbd *sdp);

#endif /* __GLOCK_DOT_H__ */
