/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright 2004-2011 Red Hat, Inc.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/dlm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/gfs2_ondisk.h>

#include "incore.h"
#include "glock.h"
#include "util.h"
#include "sys.h"
#include "trace_gfs2.h"

extern struct workqueue_struct *gfs2_control_wq;

/**
 * gfs2_update_stats - Update time based stats
 * @mv: Pointer to mean/variance structure to update
 * @sample: New data to include
 *
 * @delta is the difference between the current rtt sample and the
 * running average srtt. We add 1/8 of that to the srtt in order to
 * update the current srtt estimate. The varience estimate is a bit
 * more complicated. We subtract the abs value of the @delta from
 * the current variance estimate and add 1/4 of that to the running
 * total.
 *
 * Note that the index points at the array entry containing the smoothed
 * mean value, and the variance is always in the following entry
 *
 * Reference: TCP/IP Illustrated, vol 2, p. 831,832
 * All times are in units of integer nanoseconds. Unlike the TCP/IP case,
 * they are not scaled fixed point.
 */

static inline void gfs2_update_stats(struct gfs2_lkstats *s, unsigned index,
				     s64 sample)
{
	s64 delta = sample - s->stats[index];
	s->stats[index] += (delta >> 3);
	index++;
	s->stats[index] += ((abs64(delta) - s->stats[index]) >> 2);
}

/**
 * gfs2_update_reply_times - Update locking statistics
 * @gl: The glock to update
 *
 * This assumes that gl->gl_dstamp has been set earlier.
 *
 * The rtt (lock round trip time) is an estimate of the time
 * taken to perform a dlm lock request. We update it on each
 * reply from the dlm.
 *
 * The blocking flag is set on the glock for all dlm requests
 * which may potentially block due to lock requests from other nodes.
 * DLM requests where the current lock state is exclusive, the
 * requested state is null (or unlocked) or where the TRY or
 * TRY_1CB flags are set are classified as non-blocking. All
 * other DLM requests are counted as (potentially) blocking.
 */
static inline void gfs2_update_reply_times(struct gfs2_glock *gl)
{
	struct gfs2_pcpu_lkstats *lks;
	const unsigned gltype = gl->gl_name.ln_type;
	unsigned index = test_bit(GLF_BLOCKING, &gl->gl_flags) ?
			 GFS2_LKS_SRTTB : GFS2_LKS_SRTT;
	s64 rtt;

	preempt_disable();
	rtt = ktime_to_ns(ktime_sub(ktime_get_real(), gl->gl_dstamp));
	lks = this_cpu_ptr(gl->gl_sbd->sd_lkstats);
	gfs2_update_stats(&gl->gl_stats, index, rtt);		/* Local */
	gfs2_update_stats(&lks->lkstats[gltype], index, rtt);	/* Global */
	preempt_enable();

	trace_gfs2_glock_lock_time(gl, rtt);
}

/**
 * gfs2_update_request_times - Update locking statistics
 * @gl: The glock to update
 *
 * The irt (lock inter-request times) measures the average time
 * between requests to the dlm. It is updated immediately before
 * each dlm call.
 */

static inline void gfs2_update_request_times(struct gfs2_glock *gl)
{
	struct gfs2_pcpu_lkstats *lks;
	const unsigned gltype = gl->gl_name.ln_type;
	ktime_t dstamp;
	s64 irt;

	preempt_disable();
	dstamp = gl->gl_dstamp;
	gl->gl_dstamp = ktime_get_real();
	irt = ktime_to_ns(ktime_sub(gl->gl_dstamp, dstamp));
	lks = this_cpu_ptr(gl->gl_sbd->sd_lkstats);
	gfs2_update_stats(&gl->gl_stats, GFS2_LKS_SIRT, irt);		/* Local */
	gfs2_update_stats(&lks->lkstats[gltype], GFS2_LKS_SIRT, irt);	/* Global */
	preempt_enable();
}
 
static void gdlm_ast(void *arg)
{
	struct gfs2_glock *gl = arg;
	unsigned ret = gl->gl_state;

	gfs2_update_reply_times(gl);
	BUG_ON(gl->gl_lksb.sb_flags & DLM_SBF_DEMOTED);

	if ((gl->gl_lksb.sb_flags & DLM_SBF_VALNOTVALID) && gl->gl_lksb.sb_lvbptr)
		memset(gl->gl_lksb.sb_lvbptr, 0, GDLM_LVB_SIZE);

	switch (gl->gl_lksb.sb_status) {
	case -DLM_EUNLOCK: /* Unlocked, so glock can be freed */
		gfs2_glock_free(gl);
		return;
	case -DLM_ECANCEL: /* Cancel while getting lock */
		ret |= LM_OUT_CANCELED;
		goto out;
	case -EAGAIN: /* Try lock fails */
	case -EDEADLK: /* Deadlock detected */
		goto out;
	case -ETIMEDOUT: /* Canceled due to timeout */
		ret |= LM_OUT_ERROR;
		goto out;
	case 0: /* Success */
		break;
	default: /* Something unexpected */
		BUG();
	}

	ret = gl->gl_req;
	if (gl->gl_lksb.sb_flags & DLM_SBF_ALTMODE) {
		if (gl->gl_req == LM_ST_SHARED)
			ret = LM_ST_DEFERRED;
		else if (gl->gl_req == LM_ST_DEFERRED)
			ret = LM_ST_SHARED;
		else
			BUG();
	}

	set_bit(GLF_INITIAL, &gl->gl_flags);
	gfs2_glock_complete(gl, ret);
	return;
out:
	if (!test_bit(GLF_INITIAL, &gl->gl_flags))
		gl->gl_lksb.sb_lkid = 0;
	gfs2_glock_complete(gl, ret);
}

static void gdlm_bast(void *arg, int mode)
{
	struct gfs2_glock *gl = arg;

	switch (mode) {
	case DLM_LOCK_EX:
		gfs2_glock_cb(gl, LM_ST_UNLOCKED);
		break;
	case DLM_LOCK_CW:
		gfs2_glock_cb(gl, LM_ST_DEFERRED);
		break;
	case DLM_LOCK_PR:
		gfs2_glock_cb(gl, LM_ST_SHARED);
		break;
	default:
		pr_err("unknown bast mode %d\n", mode);
		BUG();
	}
}

/* convert gfs lock-state to dlm lock-mode */

static int make_mode(const unsigned int lmstate)
{
	switch (lmstate) {
	case LM_ST_UNLOCKED:
		return DLM_LOCK_NL;
	case LM_ST_EXCLUSIVE:
		return DLM_LOCK_EX;
	case LM_ST_DEFERRED:
		return DLM_LOCK_CW;
	case LM_ST_SHARED:
		return DLM_LOCK_PR;
	}
	pr_err("unknown LM state %d\n", lmstate);
	BUG();
	return -1;
}

static u32 make_flags(struct gfs2_glock *gl, const unsigned int gfs_flags,
		      const int req)
{
	u32 lkf = 0;

	if (gl->gl_lksb.sb_lvbptr)
		lkf |= DLM_LKF_VALBLK;

	if (gfs_flags & LM_FLAG_TRY)
		lkf |= DLM_LKF_NOQUEUE;

	if (gfs_flags & LM_FLAG_TRY_1CB) {
		lkf |= DLM_LKF_NOQUEUE;
		lkf |= DLM_LKF_NOQUEUEBAST;
	}

	if (gfs_flags & LM_FLAG_PRIORITY) {
		lkf |= DLM_LKF_NOORDER;
		lkf |= DLM_LKF_HEADQUE;
	}

	if (gfs_flags & LM_FLAG_ANY) {
		if (req == DLM_LOCK_PR)
			lkf |= DLM_LKF_ALTCW;
		else if (req == DLM_LOCK_CW)
			lkf |= DLM_LKF_ALTPR;
		else
			BUG();
	}

	if (gl->gl_lksb.sb_lkid != 0) {
		lkf |= DLM_LKF_CONVERT;
		if (test_bit(GLF_BLOCKING, &gl->gl_flags))
			lkf |= DLM_LKF_QUECVT;
	}

	return lkf;
}

static void gfs2_reverse_hex(char *c, u64 value)
{
	*c = '0';
	while (value) {
		*c-- = hex_asc[value & 0x0f];
		value >>= 4;
	}
}

static int gdlm_lock(struct gfs2_glock *gl, unsigned int req_state,
		     unsigned int flags)
{
	struct lm_lockstruct *ls = &gl->gl_sbd->sd_lockstruct;
	int req;
	u32 lkf;
	char strname[GDLM_STRNAME_BYTES] = "";

	req = make_mode(req_state);
	lkf = make_flags(gl, flags, req);
	gfs2_glstats_inc(gl, GFS2_LKS_DCOUNT);
	gfs2_sbstats_inc(gl, GFS2_LKS_DCOUNT);
	if (gl->gl_lksb.sb_lkid) {
		gfs2_update_request_times(gl);
	} else {
		memset(strname, ' ', GDLM_STRNAME_BYTES - 1);
		strname[GDLM_STRNAME_BYTES - 1] = '\0';
		gfs2_reverse_hex(strname + 7, gl->gl_name.ln_type);
		gfs2_reverse_hex(strname + 23, gl->gl_name.ln_number);
		gl->gl_dstamp = ktime_get_real();
	}
	/*
	 * Submit the actual lock request.
	 */

	return dlm_lock(ls->ls_dlm, req, &gl->gl_lksb, lkf, strname,
			GDLM_STRNAME_BYTES - 1, 0, gdlm_ast, gl, gdlm_bast);
}

static void gdlm_put_lock(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	int lvb_needs_unlock = 0;
	int error;

	if (gl->gl_lksb.sb_lkid == 0) {
		gfs2_glock_free(gl);
		return;
	}

	clear_bit(GLF_BLOCKING, &gl->gl_flags);
	gfs2_glstats_inc(gl, GFS2_LKS_DCOUNT);
	gfs2_sbstats_inc(gl, GFS2_LKS_DCOUNT);
	gfs2_update_request_times(gl);

	/* don't want to skip dlm_unlock writing the lvb when lock is ex */

	if (gl->gl_lksb.sb_lvbptr && (gl->gl_state == LM_ST_EXCLUSIVE))
		lvb_needs_unlock = 1;

	if (test_bit(SDF_SKIP_DLM_UNLOCK, &sdp->sd_flags) &&
	    !lvb_needs_unlock) {
		gfs2_glock_free(gl);
		return;
	}

	error = dlm_unlock(ls->ls_dlm, gl->gl_lksb.sb_lkid, DLM_LKF_VALBLK,
			   NULL, gl);
	if (error) {
		pr_err("gdlm_unlock %x,%llx err=%d\n",
		       gl->gl_name.ln_type,
		       (unsigned long long)gl->gl_name.ln_number, error);
		return;
	}
}

static void gdlm_cancel(struct gfs2_glock *gl)
{
	struct lm_lockstruct *ls = &gl->gl_sbd->sd_lockstruct;
	dlm_unlock(ls->ls_dlm, gl->gl_lksb.sb_lkid, DLM_LKF_CANCEL, NULL, gl);
}

/*
 * dlm/gfs2 recovery coordination using dlm_recover callbacks
 *
 *  1. dlm_controld sees lockspace members change
 *  2. dlm_controld blocks dlm-kernel locking activity
 *  3. dlm_controld within dlm-kernel notifies gfs2 (recover_prep)
 *  4. dlm_controld starts and finishes its own user level recovery
 *  5. dlm_controld starts dlm-kernel dlm_recoverd to do kernel recovery
 *  6. dlm_recoverd notifies gfs2 of failed nodes (recover_slot)
 *  7. dlm_recoverd does its own lock recovery
 *  8. dlm_recoverd unblocks dlm-kernel locking activity
 *  9. dlm_recoverd notifies gfs2 when done (recover_done with new generation)
 * 10. gfs2_control updates control_lock lvb with new generation and jid bits
 * 11. gfs2_control enqueues journals for gfs2_recover to recover (maybe none)
 * 12. gfs2_recover dequeues and recovers journals of failed nodes
 * 13. gfs2_recover provides recovery results to gfs2_control (recovery_result)
 * 14. gfs2_control updates control_lock lvb jid bits for recovered journals
 * 15. gfs2_control unblocks normal locking when all journals are recovered
 *
 * - failures during recovery
 *
 * recover_prep() may set BLOCK_LOCKS (step 3) again before gfs2_control
 * clears BLOCK_LOCKS (step 15), e.g. another node fails while still
 * recovering for a prior failure.  gfs2_control needs a way to detect
 * this so it can leave BLOCK_LOCKS set in step 15.  This is managed using
 * the recover_block and recover_start values.
 *
 * recover_done() provides a new lockspace generation number each time it
 * is called (step 9).  This generation number is saved as recover_start.
 * When recover_prep() is called, it sets BLOCK_LOCKS and sets
 * recover_block = recover_start.  So, while recover_block is equal to
 * recover_start, BLOCK_LOCKS should remain set.  (recover_spin must
 * be held around the BLOCK_LOCKS/recover_block/recover_start logic.)
 *
 * - more specific gfs2 steps in sequence above
 *
 *  3. recover_prep sets BLOCK_LOCKS and sets recover_block = recover_start
 *  6. recover_slot records any failed jids (maybe none)
 *  9. recover_done sets recover_start = new generation number
 * 10. gfs2_control sets control_lock lvb = new gen + bits for failed jids
 * 12. gfs2_recover does journal recoveries for failed jids identified above
 * 14. gfs2_control clears control_lock lvb bits for recovered jids
 * 15. gfs2_control checks if recover_block == recover_start (step 3 occured
 *     again) then do nothing, otherwise if recover_start > recover_block
 *     then clear BLOCK_LOCKS.
 *
 * - parallel recovery steps across all nodes
 *
 * All nodes attempt to update the control_lock lvb with the new generation
 * number and jid bits, but only the first to get the control_lock EX will
 * do so; others will see that it's already done (lvb already contains new
 * generation number.)
 *
 * . All nodes get the same recover_prep/recover_slot/recover_done callbacks
 * . All nodes attempt to set control_lock lvb gen + bits for the new gen
 * . One node gets control_lock first and writes the lvb, others see it's done
 * . All nodes attempt to recover jids for which they see control_lock bits set
 * . One node succeeds for a jid, and that one clears the jid bit in the lvb
 * . All nodes will eventually see all lvb bits clear and unblock locks
 *
 * - is there a problem with clearing an lvb bit that should be set
 *   and missing a journal recovery?
 *
 * 1. jid fails
 * 2. lvb bit set for step 1
 * 3. jid recovered for step 1
 * 4. jid taken again (new mount)
 * 5. jid fails (for step 4)
 * 6. lvb bit set for step 5 (will already be set)
 * 7. lvb bit cleared for step 3
 *
 * This is not a problem because the failure in step 5 does not
 * require recovery, because the mount in step 4 could not have
 * progressed far enough to unblock locks and access the fs.  The
 * control_mount() function waits for all recoveries to be complete
 * for the latest lockspace generation before ever unblocking locks
 * and returning.  The mount in step 4 waits until the recovery in
 * step 1 is done.
 *
 * - special case of first mounter: first node to mount the fs
 *
 * The first node to mount a gfs2 fs needs to check all the journals
 * and recover any that need recovery before other nodes are allowed
 * to mount the fs.  (Others may begin mounting, but they must wait
 * for the first mounter to be done before taking locks on the fs
 * or accessing the fs.)  This has two parts:
 *
 * 1. The mounted_lock tells a node it's the first to mount the fs.
 * Each node holds the mounted_lock in PR while it's mounted.
 * Each node tries to acquire the mounted_lock in EX when it mounts.
 * If a node is granted the mounted_lock EX it means there are no
 * other mounted nodes (no PR locks exist), and it is the first mounter.
 * The mounted_lock is demoted to PR when first recovery is done, so
 * others will fail to get an EX lock, but will get a PR lock.
 *
 * 2. The control_lock blocks others in control_mount() while the first
 * mounter is doing first mount recovery of all journals.
 * A mounting node needs to acquire control_lock in EX mode before
 * it can proceed.  The first mounter holds control_lock in EX while doing
 * the first mount recovery, blocking mounts from other nodes, then demotes
 * control_lock to NL when it's done (others_may_mount/first_done),
 * allowing other nodes to continue mounting.
 *
 * first mounter:
 * control_lock EX/NOQUEUE success
 * mounted_lock EX/NOQUEUE success (no other PR, so no other mounters)
 * set first=1
 * do first mounter recovery
 * mounted_lock EX->PR
 * control_lock EX->NL, write lvb generation
 *
 * other mounter:
 * control_lock EX/NOQUEUE success (if fail -EAGAIN, retry)
 * mounted_lock EX/NOQUEUE fail -EAGAIN (expected due to other mounters PR)
 * mounted_lock PR/NOQUEUE success
 * read lvb generation
 * control_lock EX->NL
 * set first=0
 *
 * - mount during recovery
 *
 * If a node mounts while others are doing recovery (not first mounter),
 * the mounting node will get its initial recover_done() callback without
 * having seen any previous failures/callbacks.
 *
 * It must wait for all recoveries preceding its mount to be finished
 * before it unblocks locks.  It does this by repeating the "other mounter"
 * steps above until the lvb generation number is >= its mount generation
 * number (from initial recover_done) and all lvb bits are clear.
 *
 * - control_lock lvb format
 *
 * 4 bytes generation number: the latest dlm lockspace generation number
 * from recover_done callback.  Indicates the jid bitmap has been updated
 * to reflect all slot failures through that generation.
 * 4 bytes unused.
 * GDLM_LVB_SIZE-8 bytes of jid bit map. If bit N is set, it indicates
 * that jid N needs recovery.
 */

#define JID_BITMAP_OFFSET 8 /* 4 byte generation number + 4 byte unused */

static void control_lvb_read(struct lm_lockstruct *ls, uint32_t *lvb_gen,
			     char *lvb_bits)
{
	__le32 gen;
	memcpy(lvb_bits, ls->ls_control_lvb, GDLM_LVB_SIZE);
	memcpy(&gen, lvb_bits, sizeof(__le32));
	*lvb_gen = le32_to_cpu(gen);
}

static void control_lvb_write(struct lm_lockstruct *ls, uint32_t lvb_gen,
			      char *lvb_bits)
{
	__le32 gen;
	memcpy(ls->ls_control_lvb, lvb_bits, GDLM_LVB_SIZE);
	gen = cpu_to_le32(lvb_gen);
	memcpy(ls->ls_control_lvb, &gen, sizeof(__le32));
}

static int all_jid_bits_clear(char *lvb)
{
	return !memchr_inv(lvb + JID_BITMAP_OFFSET, 0,
			GDLM_LVB_SIZE - JID_BITMAP_OFFSET);
}

static void sync_wait_cb(void *arg)
{
	struct lm_lockstruct *ls = arg;
	complete(&ls->ls_sync_wait);
}

static int sync_unlock(struct gfs2_sbd *sdp, struct dlm_lksb *lksb, char *name)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	int error;

	error = dlm_unlock(ls->ls_dlm, lksb->sb_lkid, 0, lksb, ls);
	if (error) {
		fs_err(sdp, "%s lkid %x error %d\n",
		       name, lksb->sb_lkid, error);
		return error;
	}

	wait_for_completion(&ls->ls_sync_wait);

	if (lksb->sb_status != -DLM_EUNLOCK) {
		fs_err(sdp, "%s lkid %x status %d\n",
		       name, lksb->sb_lkid, lksb->sb_status);
		return -1;
	}
	return 0;
}

static int sync_lock(struct gfs2_sbd *sdp, int mode, uint32_t flags,
		     unsigned int num, struct dlm_lksb *lksb, char *name)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	char strname[GDLM_STRNAME_BYTES];
	int error, status;

	memset(strname, 0, GDLM_STRNAME_BYTES);
	snprintf(strname, GDLM_STRNAME_BYTES, "%8x%16x", LM_TYPE_NONDISK, num);

	error = dlm_lock(ls->ls_dlm, mode, lksb, flags,
			 strname, GDLM_STRNAME_BYTES - 1,
			 0, sync_wait_cb, ls, NULL);
	if (error) {
		fs_err(sdp, "%s lkid %x flags %x mode %d error %d\n",
		       name, lksb->sb_lkid, flags, mode, error);
		return error;
	}

	wait_for_completion(&ls->ls_sync_wait);

	status = lksb->sb_status;

	if (status && status != -EAGAIN) {
		fs_err(sdp, "%s lkid %x flags %x mode %d status %d\n",
		       name, lksb->sb_lkid, flags, mode, status);
	}

	return status;
}

static int mounted_unlock(struct gfs2_sbd *sdp)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	return sync_unlock(sdp, &ls->ls_mounted_lksb, "mounted_lock");
}

static int mounted_lock(struct gfs2_sbd *sdp, int mode, uint32_t flags)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	return sync_lock(sdp, mode, flags, GFS2_MOUNTED_LOCK,
			 &ls->ls_mounted_lksb, "mounted_lock");
}

static int control_unlock(struct gfs2_sbd *sdp)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	return sync_unlock(sdp, &ls->ls_control_lksb, "control_lock");
}

static int control_lock(struct gfs2_sbd *sdp, int mode, uint32_t flags)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	return sync_lock(sdp, mode, flags, GFS2_CONTROL_LOCK,
			 &ls->ls_control_lksb, "control_lock");
}

static void gfs2_control_func(struct work_struct *work)
{
	struct gfs2_sbd *sdp = container_of(work, struct gfs2_sbd, sd_control_work.work);
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	uint32_t block_gen, start_gen, lvb_gen, flags;
	int recover_set = 0;
	int write_lvb = 0;
	int recover_size;
	int i, error;

	spin_lock(&ls->ls_recover_spin);
	/*
	 * No MOUNT_DONE means we're still mounting; control_mount()
	 * will set this flag, after which this thread will take over
	 * all further clearing of BLOCK_LOCKS.
	 *
	 * FIRST_MOUNT means this node is doing first mounter recovery,
	 * for which recovery control is handled by
	 * control_mount()/control_first_done(), not this thread.
	 */
	if (!test_bit(DFL_MOUNT_DONE, &ls->ls_recover_flags) ||
	     test_bit(DFL_FIRST_MOUNT, &ls->ls_recover_flags)) {
		spin_unlock(&ls->ls_recover_spin);
		return;
	}
	block_gen = ls->ls_recover_block;
	start_gen = ls->ls_recover_start;
	spin_unlock(&ls->ls_recover_spin);

	/*
	 * Equal block_gen and start_gen implies we are between
	 * recover_prep and recover_done callbacks, which means
	 * dlm recovery is in progress and dlm locking is blocked.
	 * There's no point trying to do any work until recover_done.
	 */

	if (block_gen == start_gen)
		return;

	/*
	 * Propagate recover_submit[] and recover_result[] to lvb:
	 * dlm_recoverd adds to recover_submit[] jids needing recovery
	 * gfs2_recover adds to recover_result[] journal recovery results
	 *
	 * set lvb bit for jids in recover_submit[] if the lvb has not
	 * yet been updated for the generation of the failure
	 *
	 * clear lvb bit for jids in recover_result[] if the result of
	 * the journal recovery is SUCCESS
	 */

	error = control_lock(sdp, DLM_LOCK_EX, DLM_LKF_CONVERT|DLM_LKF_VALBLK);
	if (error) {
		fs_err(sdp, "control lock EX error %d\n", error);
		return;
	}

	control_lvb_read(ls, &lvb_gen, ls->ls_lvb_bits);

	spin_lock(&ls->ls_recover_spin);
	if (block_gen != ls->ls_recover_block ||
	    start_gen != ls->ls_recover_start) {
		fs_info(sdp, "recover generation %u block1 %u %u\n",
			start_gen, block_gen, ls->ls_recover_block);
		spin_unlock(&ls->ls_recover_spin);
		control_lock(sdp, DLM_LOCK_NL, DLM_LKF_CONVERT);
		return;
	}

	recover_size = ls->ls_recover_size;

	if (lvb_gen <= start_gen) {
		/*
		 * Clear lvb bits for jids we've successfully recovered.
		 * Because all nodes attempt to recover failed journals,
		 * a journal can be recovered multiple times successfully
		 * in succession.  Only the first will really do recovery,
		 * the others find it clean, but still report a successful
		 * recovery.  So, another node may have already recovered
		 * the jid and cleared the lvb bit for it.
		 */
		for (i = 0; i < recover_size; i++) {
			if (ls->ls_recover_result[i] != LM_RD_SUCCESS)
				continue;

			ls->ls_recover_result[i] = 0;

			if (!test_bit_le(i, ls->ls_lvb_bits + JID_BITMAP_OFFSET))
				continue;

			__clear_bit_le(i, ls->ls_lvb_bits + JID_BITMAP_OFFSET);
			write_lvb = 1;
		}
	}

	if (lvb_gen == start_gen) {
		/*
		 * Failed slots before start_gen are already set in lvb.
		 */
		for (i = 0; i < recover_size; i++) {
			if (!ls->ls_recover_submit[i])
				continue;
			if (ls->ls_recover_submit[i] < lvb_gen)
				ls->ls_recover_submit[i] = 0;
		}
	} else if (lvb_gen < start_gen) {
		/*
		 * Failed slots before start_gen are not yet set in lvb.
		 */
		for (i = 0; i < recover_size; i++) {
			if (!ls->ls_recover_submit[i])
				continue;
			if (ls->ls_recover_submit[i] < start_gen) {
				ls->ls_recover_submit[i] = 0;
				__set_bit_le(i, ls->ls_lvb_bits + JID_BITMAP_OFFSET);
			}
		}
		/* even if there are no bits to set, we need to write the
		   latest generation to the lvb */
		write_lvb = 1;
	} else {
		/*
		 * we should be getting a recover_done() for lvb_gen soon
		 */
	}
	spin_unlock(&ls->ls_recover_spin);

	if (write_lvb) {
		control_lvb_write(ls, start_gen, ls->ls_lvb_bits);
		flags = DLM_LKF_CONVERT | DLM_LKF_VALBLK;
	} else {
		flags = DLM_LKF_CONVERT;
	}

	error = control_lock(sdp, DLM_LOCK_NL, flags);
	if (error) {
		fs_err(sdp, "control lock NL error %d\n", error);
		return;
	}

	/*
	 * Everyone will see jid bits set in the lvb, run gfs2_recover_set(),
	 * and clear a jid bit in the lvb if the recovery is a success.
	 * Eventually all journals will be recovered, all jid bits will
	 * be cleared in the lvb, and everyone will clear BLOCK_LOCKS.
	 */

	for (i = 0; i < recover_size; i++) {
		if (test_bit_le(i, ls->ls_lvb_bits + JID_BITMAP_OFFSET)) {
			fs_info(sdp, "recover generation %u jid %d\n",
				start_gen, i);
			gfs2_recover_set(sdp, i);
			recover_set++;
		}
	}
	if (recover_set)
		return;

	/*
	 * No more jid bits set in lvb, all recovery is done, unblock locks
	 * (unless a new recover_prep callback has occured blocking locks
	 * again while working above)
	 */

	spin_lock(&ls->ls_recover_spin);
	if (ls->ls_recover_block == block_gen &&
	    ls->ls_recover_start == start_gen) {
		clear_bit(DFL_BLOCK_LOCKS, &ls->ls_recover_flags);
		spin_unlock(&ls->ls_recover_spin);
		fs_info(sdp, "recover generation %u done\n", start_gen);
		gfs2_glock_thaw(sdp);
	} else {
		fs_info(sdp, "recover generation %u block2 %u %u\n",
			start_gen, block_gen, ls->ls_recover_block);
		spin_unlock(&ls->ls_recover_spin);
	}
}

static int control_mount(struct gfs2_sbd *sdp)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	uint32_t start_gen, block_gen, mount_gen, lvb_gen;
	int mounted_mode;
	int retries = 0;
	int error;

	memset(&ls->ls_mounted_lksb, 0, sizeof(struct dlm_lksb));
	memset(&ls->ls_control_lksb, 0, sizeof(struct dlm_lksb));
	memset(&ls->ls_control_lvb, 0, GDLM_LVB_SIZE);
	ls->ls_control_lksb.sb_lvbptr = ls->ls_control_lvb;
	init_completion(&ls->ls_sync_wait);

	set_bit(DFL_BLOCK_LOCKS, &ls->ls_recover_flags);

	error = control_lock(sdp, DLM_LOCK_NL, DLM_LKF_VALBLK);
	if (error) {
		fs_err(sdp, "control_mount control_lock NL error %d\n", error);
		return error;
	}

	error = mounted_lock(sdp, DLM_LOCK_NL, 0);
	if (error) {
		fs_err(sdp, "control_mount mounted_lock NL error %d\n", error);
		control_unlock(sdp);
		return error;
	}
	mounted_mode = DLM_LOCK_NL;

restart:
	if (retries++ && signal_pending(current)) {
		error = -EINTR;
		goto fail;
	}

	/*
	 * We always start with both locks in NL. control_lock is
	 * demoted to NL below so we don't need to do it here.
	 */

	if (mounted_mode != DLM_LOCK_NL) {
		error = mounted_lock(sdp, DLM_LOCK_NL, DLM_LKF_CONVERT);
		if (error)
			goto fail;
		mounted_mode = DLM_LOCK_NL;
	}

	/*
	 * Other nodes need to do some work in dlm recovery and gfs2_control
	 * before the recover_done and control_lock will be ready for us below.
	 * A delay here is not required but often avoids having to retry.
	 */

	msleep_interruptible(500);

	/*
	 * Acquire control_lock in EX and mounted_lock in either EX or PR.
	 * control_lock lvb keeps track of any pending journal recoveries.
	 * mounted_lock indicates if any other nodes have the fs mounted.
	 */

	error = control_lock(sdp, DLM_LOCK_EX, DLM_LKF_CONVERT|DLM_LKF_NOQUEUE|DLM_LKF_VALBLK);
	if (error == -EAGAIN) {
		goto restart;
	} else if (error) {
		fs_err(sdp, "control_mount control_lock EX error %d\n", error);
		goto fail;
	}

	error = mounted_lock(sdp, DLM_LOCK_EX, DLM_LKF_CONVERT|DLM_LKF_NOQUEUE);
	if (!error) {
		mounted_mode = DLM_LOCK_EX;
		goto locks_done;
	} else if (error != -EAGAIN) {
		fs_err(sdp, "control_mount mounted_lock EX error %d\n", error);
		goto fail;
	}

	error = mounted_lock(sdp, DLM_LOCK_PR, DLM_LKF_CONVERT|DLM_LKF_NOQUEUE);
	if (!error) {
		mounted_mode = DLM_LOCK_PR;
		goto locks_done;
	} else {
		/* not even -EAGAIN should happen here */
		fs_err(sdp, "control_mount mounted_lock PR error %d\n", error);
		goto fail;
	}

locks_done:
	/*
	 * If we got both locks above in EX, then we're the first mounter.
	 * If not, then we need to wait for the control_lock lvb to be
	 * updated by other mounted nodes to reflect our mount generation.
	 *
	 * In simple first mounter cases, first mounter will see zero lvb_gen,
	 * but in cases where all existing nodes leave/fail before mounting
	 * nodes finish control_mount, then all nodes will be mounting and
	 * lvb_gen will be non-zero.
	 */

	control_lvb_read(ls, &lvb_gen, ls->ls_lvb_bits);

	if (lvb_gen == 0xFFFFFFFF) {
		/* special value to force mount attempts to fail */
		fs_err(sdp, "control_mount control_lock disabled\n");
		error = -EINVAL;
		goto fail;
	}

	if (mounted_mode == DLM_LOCK_EX) {
		/* first mounter, keep both EX while doing first recovery */
		spin_lock(&ls->ls_recover_spin);
		clear_bit(DFL_BLOCK_LOCKS, &ls->ls_recover_flags);
		set_bit(DFL_MOUNT_DONE, &ls->ls_recover_flags);
		set_bit(DFL_FIRST_MOUNT, &ls->ls_recover_flags);
		spin_unlock(&ls->ls_recover_spin);
		fs_info(sdp, "first mounter control generation %u\n", lvb_gen);
		return 0;
	}

	error = control_lock(sdp, DLM_LOCK_NL, DLM_LKF_CONVERT);
	if (error)
		goto fail;

	/*
	 * We are not first mounter, now we need to wait for the control_lock
	 * lvb generation to be >= the generation from our first recover_done
	 * and all lvb bits to be clear (no pending journal recoveries.)
	 */

	if (!all_jid_bits_clear(ls->ls_lvb_bits)) {
		/* journals need recovery, wait until all are clear */
		fs_info(sdp, "control_mount wait for journal recovery\n");
		goto restart;
	}

	spin_lock(&ls->ls_recover_spin);
	block_gen = ls->ls_recover_block;
	start_gen = ls->ls_recover_start;
	mount_gen = ls->ls_recover_mount;

	if (lvb_gen < mount_gen) {
		/* wait for mounted nodes to update control_lock lvb to our
		   generation, which might include new recovery bits set */
		fs_info(sdp, "control_mount wait1 block %u start %u mount %u "
			"lvb %u flags %lx\n", block_gen, start_gen, mount_gen,
			lvb_gen, ls->ls_recover_flags);
		spin_unlock(&ls->ls_recover_spin);
		goto restart;
	}

	if (lvb_gen != start_gen) {
		/* wait for mounted nodes to update control_lock lvb to the
		   latest recovery generation */
		fs_info(sdp, "control_mount wait2 block %u start %u mount %u "
			"lvb %u flags %lx\n", block_gen, start_gen, mount_gen,
			lvb_gen, ls->ls_recover_flags);
		spin_unlock(&ls->ls_recover_spin);
		goto restart;
	}

	if (block_gen == start_gen) {
		/* dlm recovery in progress, wait for it to finish */
		fs_info(sdp, "control_mount wait3 block %u start %u mount %u "
			"lvb %u flags %lx\n", block_gen, start_gen, mount_gen,
			lvb_gen, ls->ls_recover_flags);
		spin_unlock(&ls->ls_recover_spin);
		goto restart;
	}

	clear_bit(DFL_BLOCK_LOCKS, &ls->ls_recover_flags);
	set_bit(DFL_MOUNT_DONE, &ls->ls_recover_flags);
	memset(ls->ls_recover_submit, 0, ls->ls_recover_size*sizeof(uint32_t));
	memset(ls->ls_recover_result, 0, ls->ls_recover_size*sizeof(uint32_t));
	spin_unlock(&ls->ls_recover_spin);
	return 0;

fail:
	mounted_unlock(sdp);
	control_unlock(sdp);
	return error;
}

static int control_first_done(struct gfs2_sbd *sdp)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	uint32_t start_gen, block_gen;
	int error;

restart:
	spin_lock(&ls->ls_recover_spin);
	start_gen = ls->ls_recover_start;
	block_gen = ls->ls_recover_block;

	if (test_bit(DFL_BLOCK_LOCKS, &ls->ls_recover_flags) ||
	    !test_bit(DFL_MOUNT_DONE, &ls->ls_recover_flags) ||
	    !test_bit(DFL_FIRST_MOUNT, &ls->ls_recover_flags)) {
		/* sanity check, should not happen */
		fs_err(sdp, "control_first_done start %u block %u flags %lx\n",
		       start_gen, block_gen, ls->ls_recover_flags);
		spin_unlock(&ls->ls_recover_spin);
		control_unlock(sdp);
		return -1;
	}

	if (start_gen == block_gen) {
		/*
		 * Wait for the end of a dlm recovery cycle to switch from
		 * first mounter recovery.  We can ignore any recover_slot
		 * callbacks between the recover_prep and next recover_done
		 * because we are still the first mounter and any failed nodes
		 * have not fully mounted, so they don't need recovery.
		 */
		spin_unlock(&ls->ls_recover_spin);
		fs_info(sdp, "control_first_done wait gen %u\n", start_gen);

		wait_on_bit(&ls->ls_recover_flags, DFL_DLM_RECOVERY,
			    TASK_UNINTERRUPTIBLE);
		goto restart;
	}

	clear_bit(DFL_FIRST_MOUNT, &ls->ls_recover_flags);
	set_bit(DFL_FIRST_MOUNT_DONE, &ls->ls_recover_flags);
	memset(ls->ls_recover_submit, 0, ls->ls_recover_size*sizeof(uint32_t));
	memset(ls->ls_recover_result, 0, ls->ls_recover_size*sizeof(uint32_t));
	spin_unlock(&ls->ls_recover_spin);

	memset(ls->ls_lvb_bits, 0, GDLM_LVB_SIZE);
	control_lvb_write(ls, start_gen, ls->ls_lvb_bits);

	error = mounted_lock(sdp, DLM_LOCK_PR, DLM_LKF_CONVERT);
	if (error)
		fs_err(sdp, "control_first_done mounted PR error %d\n", error);

	error = control_lock(sdp, DLM_LOCK_NL, DLM_LKF_CONVERT|DLM_LKF_VALBLK);
	if (error)
		fs_err(sdp, "control_first_done control NL error %d\n", error);

	return error;
}

/*
 * Expand static jid arrays if necessary (by increments of RECOVER_SIZE_INC)
 * to accomodate the largest slot number.  (NB dlm slot numbers start at 1,
 * gfs2 jids start at 0, so jid = slot - 1)
 */

#define RECOVER_SIZE_INC 16

static int set_recover_size(struct gfs2_sbd *sdp, struct dlm_slot *slots,
			    int num_slots)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	uint32_t *submit = NULL;
	uint32_t *result = NULL;
	uint32_t old_size, new_size;
	int i, max_jid;

	if (!ls->ls_lvb_bits) {
		ls->ls_lvb_bits = kzalloc(GDLM_LVB_SIZE, GFP_NOFS);
		if (!ls->ls_lvb_bits)
			return -ENOMEM;
	}

	max_jid = 0;
	for (i = 0; i < num_slots; i++) {
		if (max_jid < slots[i].slot - 1)
			max_jid = slots[i].slot - 1;
	}

	old_size = ls->ls_recover_size;

	if (old_size >= max_jid + 1)
		return 0;

	new_size = old_size + RECOVER_SIZE_INC;

	submit = kzalloc(new_size * sizeof(uint32_t), GFP_NOFS);
	result = kzalloc(new_size * sizeof(uint32_t), GFP_NOFS);
	if (!submit || !result) {
		kfree(submit);
		kfree(result);
		return -ENOMEM;
	}

	spin_lock(&ls->ls_recover_spin);
	memcpy(submit, ls->ls_recover_submit, old_size * sizeof(uint32_t));
	memcpy(result, ls->ls_recover_result, old_size * sizeof(uint32_t));
	kfree(ls->ls_recover_submit);
	kfree(ls->ls_recover_result);
	ls->ls_recover_submit = submit;
	ls->ls_recover_result = result;
	ls->ls_recover_size = new_size;
	spin_unlock(&ls->ls_recover_spin);
	return 0;
}

static void free_recover_size(struct lm_lockstruct *ls)
{
	kfree(ls->ls_lvb_bits);
	kfree(ls->ls_recover_submit);
	kfree(ls->ls_recover_result);
	ls->ls_recover_submit = NULL;
	ls->ls_recover_result = NULL;
	ls->ls_recover_size = 0;
}

/* dlm calls before it does lock recovery */

static void gdlm_recover_prep(void *arg)
{
	struct gfs2_sbd *sdp = arg;
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;

	spin_lock(&ls->ls_recover_spin);
	ls->ls_recover_block = ls->ls_recover_start;
	set_bit(DFL_DLM_RECOVERY, &ls->ls_recover_flags);

	if (!test_bit(DFL_MOUNT_DONE, &ls->ls_recover_flags) ||
	     test_bit(DFL_FIRST_MOUNT, &ls->ls_recover_flags)) {
		spin_unlock(&ls->ls_recover_spin);
		return;
	}
	set_bit(DFL_BLOCK_LOCKS, &ls->ls_recover_flags);
	spin_unlock(&ls->ls_recover_spin);
}

/* dlm calls after recover_prep has been completed on all lockspace members;
   identifies slot/jid of failed member */

static void gdlm_recover_slot(void *arg, struct dlm_slot *slot)
{
	struct gfs2_sbd *sdp = arg;
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	int jid = slot->slot - 1;

	spin_lock(&ls->ls_recover_spin);
	if (ls->ls_recover_size < jid + 1) {
		fs_err(sdp, "recover_slot jid %d gen %u short size %d",
		       jid, ls->ls_recover_block, ls->ls_recover_size);
		spin_unlock(&ls->ls_recover_spin);
		return;
	}

	if (ls->ls_recover_submit[jid]) {
		fs_info(sdp, "recover_slot jid %d gen %u prev %u\n",
			jid, ls->ls_recover_block, ls->ls_recover_submit[jid]);
	}
	ls->ls_recover_submit[jid] = ls->ls_recover_block;
	spin_unlock(&ls->ls_recover_spin);
}

/* dlm calls after recover_slot and after it completes lock recovery */

static void gdlm_recover_done(void *arg, struct dlm_slot *slots, int num_slots,
			      int our_slot, uint32_t generation)
{
	struct gfs2_sbd *sdp = arg;
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;

	/* ensure the ls jid arrays are large enough */
	set_recover_size(sdp, slots, num_slots);

	spin_lock(&ls->ls_recover_spin);
	ls->ls_recover_start = generation;

	if (!ls->ls_recover_mount) {
		ls->ls_recover_mount = generation;
		ls->ls_jid = our_slot - 1;
	}

	if (!test_bit(DFL_UNMOUNT, &ls->ls_recover_flags))
		queue_delayed_work(gfs2_control_wq, &sdp->sd_control_work, 0);

	clear_bit(DFL_DLM_RECOVERY, &ls->ls_recover_flags);
	smp_mb__after_atomic();
	wake_up_bit(&ls->ls_recover_flags, DFL_DLM_RECOVERY);
	spin_unlock(&ls->ls_recover_spin);
}

/* gfs2_recover thread has a journal recovery result */

static void gdlm_recovery_result(struct gfs2_sbd *sdp, unsigned int jid,
				 unsigned int result)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;

	if (test_bit(DFL_NO_DLM_OPS, &ls->ls_recover_flags))
		return;

	/* don't care about the recovery of own journal during mount */
	if (jid == ls->ls_jid)
		return;

	spin_lock(&ls->ls_recover_spin);
	if (test_bit(DFL_FIRST_MOUNT, &ls->ls_recover_flags)) {
		spin_unlock(&ls->ls_recover_spin);
		return;
	}
	if (ls->ls_recover_size < jid + 1) {
		fs_err(sdp, "recovery_result jid %d short size %d",
		       jid, ls->ls_recover_size);
		spin_unlock(&ls->ls_recover_spin);
		return;
	}

	fs_info(sdp, "recover jid %d result %s\n", jid,
		result == LM_RD_GAVEUP ? "busy" : "success");

	ls->ls_recover_result[jid] = result;

	/* GAVEUP means another node is recovering the journal; delay our
	   next attempt to recover it, to give the other node a chance to
	   finish before trying again */

	if (!test_bit(DFL_UNMOUNT, &ls->ls_recover_flags))
		queue_delayed_work(gfs2_control_wq, &sdp->sd_control_work,
				   result == LM_RD_GAVEUP ? HZ : 0);
	spin_unlock(&ls->ls_recover_spin);
}

const struct dlm_lockspace_ops gdlm_lockspace_ops = {
	.recover_prep = gdlm_recover_prep,
	.recover_slot = gdlm_recover_slot,
	.recover_done = gdlm_recover_done,
};

static int gdlm_mount(struct gfs2_sbd *sdp, const char *table)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	char cluster[GFS2_LOCKNAME_LEN];
	const char *fsname;
	uint32_t flags;
	int error, ops_result;

	/*
	 * initialize everything
	 */

	INIT_DELAYED_WORK(&sdp->sd_control_work, gfs2_control_func);
	spin_lock_init(&ls->ls_recover_spin);
	ls->ls_recover_flags = 0;
	ls->ls_recover_mount = 0;
	ls->ls_recover_start = 0;
	ls->ls_recover_block = 0;
	ls->ls_recover_size = 0;
	ls->ls_recover_submit = NULL;
	ls->ls_recover_result = NULL;
	ls->ls_lvb_bits = NULL;

	error = set_recover_size(sdp, NULL, 0);
	if (error)
		goto fail;

	/*
	 * prepare dlm_new_lockspace args
	 */

	fsname = strchr(table, ':');
	if (!fsname) {
		fs_info(sdp, "no fsname found\n");
		error = -EINVAL;
		goto fail_free;
	}
	memset(cluster, 0, sizeof(cluster));
	memcpy(cluster, table, strlen(table) - strlen(fsname));
	fsname++;

	flags = DLM_LSFL_FS | DLM_LSFL_NEWEXCL;

	/*
	 * create/join lockspace
	 */

	error = dlm_new_lockspace(fsname, cluster, flags, GDLM_LVB_SIZE,
				  &gdlm_lockspace_ops, sdp, &ops_result,
				  &ls->ls_dlm);
	if (error) {
		fs_err(sdp, "dlm_new_lockspace error %d\n", error);
		goto fail_free;
	}

	if (ops_result < 0) {
		/*
		 * dlm does not support ops callbacks,
		 * old dlm_controld/gfs_controld are used, try without ops.
		 */
		fs_info(sdp, "dlm lockspace ops not used\n");
		free_recover_size(ls);
		set_bit(DFL_NO_DLM_OPS, &ls->ls_recover_flags);
		return 0;
	}

	if (!test_bit(SDF_NOJOURNALID, &sdp->sd_flags)) {
		fs_err(sdp, "dlm lockspace ops disallow jid preset\n");
		error = -EINVAL;
		goto fail_release;
	}

	/*
	 * control_mount() uses control_lock to determine first mounter,
	 * and for later mounts, waits for any recoveries to be cleared.
	 */

	error = control_mount(sdp);
	if (error) {
		fs_err(sdp, "mount control error %d\n", error);
		goto fail_release;
	}

	ls->ls_first = !!test_bit(DFL_FIRST_MOUNT, &ls->ls_recover_flags);
	clear_bit(SDF_NOJOURNALID, &sdp->sd_flags);
	smp_mb__after_atomic();
	wake_up_bit(&sdp->sd_flags, SDF_NOJOURNALID);
	return 0;

fail_release:
	dlm_release_lockspace(ls->ls_dlm, 2);
fail_free:
	free_recover_size(ls);
fail:
	return error;
}

static void gdlm_first_done(struct gfs2_sbd *sdp)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	int error;

	if (test_bit(DFL_NO_DLM_OPS, &ls->ls_recover_flags))
		return;

	error = control_first_done(sdp);
	if (error)
		fs_err(sdp, "mount first_done error %d\n", error);
}

static void gdlm_unmount(struct gfs2_sbd *sdp)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;

	if (test_bit(DFL_NO_DLM_OPS, &ls->ls_recover_flags))
		goto release;

	/* wait for gfs2_control_wq to be done with this mount */

	spin_lock(&ls->ls_recover_spin);
	set_bit(DFL_UNMOUNT, &ls->ls_recover_flags);
	spin_unlock(&ls->ls_recover_spin);
	flush_delayed_work(&sdp->sd_control_work);

	/* mounted_lock and control_lock will be purged in dlm recovery */
release:
	if (ls->ls_dlm) {
		dlm_release_lockspace(ls->ls_dlm, 2);
		ls->ls_dlm = NULL;
	}

	free_recover_size(ls);
}

static const match_table_t dlm_tokens = {
	{ Opt_jid, "jid=%d"},
	{ Opt_id, "id=%d"},
	{ Opt_first, "first=%d"},
	{ Opt_nodir, "nodir=%d"},
	{ Opt_err, NULL },
};

const struct lm_lockops gfs2_dlm_ops = {
	.lm_proto_name = "lock_dlm",
	.lm_mount = gdlm_mount,
	.lm_first_done = gdlm_first_done,
	.lm_recovery_result = gdlm_recovery_result,
	.lm_unmount = gdlm_unmount,
	.lm_put_lock = gdlm_put_lock,
	.lm_lock = gdlm_lock,
	.lm_cancel = gdlm_cancel,
	.lm_tokens = &dlm_tokens,
};

