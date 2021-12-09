// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_quota.h"
#include "xfs_qm.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/health.h"

/*
 * Online Scrub and Repair
 *
 * Traditionally, XFS (the kernel driver) did not know how to check or
 * repair on-disk data structures.  That task was left to the xfs_check
 * and xfs_repair tools, both of which require taking the filesystem
 * offline for a thorough but time consuming examination.  Online
 * scrub & repair, on the other hand, enables us to check the metadata
 * for obvious errors while carefully stepping around the filesystem's
 * ongoing operations, locking rules, etc.
 *
 * Given that most XFS metadata consist of records stored in a btree,
 * most of the checking functions iterate the btree blocks themselves
 * looking for irregularities.  When a record block is encountered, each
 * record can be checked for obviously bad values.  Record values can
 * also be cross-referenced against other btrees to look for potential
 * misunderstandings between pieces of metadata.
 *
 * It is expected that the checkers responsible for per-AG metadata
 * structures will lock the AG headers (AGI, AGF, AGFL), iterate the
 * metadata structure, and perform any relevant cross-referencing before
 * unlocking the AG and returning the results to userspace.  These
 * scrubbers must not keep an AG locked for too long to avoid tying up
 * the block and inode allocators.
 *
 * Block maps and b-trees rooted in an inode present a special challenge
 * because they can involve extents from any AG.  The general scrubber
 * structure of lock -> check -> xref -> unlock still holds, but AG
 * locking order rules /must/ be obeyed to avoid deadlocks.  The
 * ordering rule, of course, is that we must lock in increasing AG
 * order.  Helper functions are provided to track which AG headers we've
 * already locked.  If we detect an imminent locking order violation, we
 * can signal a potential deadlock, in which case the scrubber can jump
 * out to the top level, lock all the AGs in order, and retry the scrub.
 *
 * For file data (directories, extended attributes, symlinks) scrub, we
 * can simply lock the inode and walk the data.  For btree data
 * (directories and attributes) we follow the same btree-scrubbing
 * strategy outlined previously to check the records.
 *
 * We use a bit of trickery with transactions to avoid buffer deadlocks
 * if there is a cycle in the metadata.  The basic problem is that
 * travelling down a btree involves locking the current buffer at each
 * tree level.  If a pointer should somehow point back to a buffer that
 * we've already examined, we will deadlock due to the second buffer
 * locking attempt.  Note however that grabbing a buffer in transaction
 * context links the locked buffer to the transaction.  If we try to
 * re-grab the buffer in the context of the same transaction, we avoid
 * the second lock attempt and continue.  Between the verifier and the
 * scrubber, something will notice that something is amiss and report
 * the corruption.  Therefore, each scrubber will allocate an empty
 * transaction, attach buffers to it, and cancel the transaction at the
 * end of the scrub run.  Cancelling a non-dirty transaction simply
 * unlocks the buffers.
 *
 * There are four pieces of data that scrub can communicate to
 * userspace.  The first is the error code (errno), which can be used to
 * communicate operational errors in performing the scrub.  There are
 * also three flags that can be set in the scrub context.  If the data
 * structure itself is corrupt, the CORRUPT flag will be set.  If
 * the metadata is correct but otherwise suboptimal, the PREEN flag
 * will be set.
 *
 * We perform secondary validation of filesystem metadata by
 * cross-referencing every record with all other available metadata.
 * For example, for block mapping extents, we verify that there are no
 * records in the free space and inode btrees corresponding to that
 * space extent and that there is a corresponding entry in the reverse
 * mapping btree.  Inconsistent metadata is noted by setting the
 * XCORRUPT flag; btree query function errors are noted by setting the
 * XFAIL flag and deleting the cursor to prevent further attempts to
 * cross-reference with a defective btree.
 *
 * If a piece of metadata proves corrupt or suboptimal, the userspace
 * program can ask the kernel to apply some tender loving care (TLC) to
 * the metadata object by setting the REPAIR flag and re-calling the
 * scrub ioctl.  "Corruption" is defined by metadata violating the
 * on-disk specification; operations cannot continue if the violation is
 * left untreated.  It is possible for XFS to continue if an object is
 * "suboptimal", however performance may be degraded.  Repairs are
 * usually performed by rebuilding the metadata entirely out of
 * redundant metadata.  Optimizing, on the other hand, can sometimes be
 * done without rebuilding entire structures.
 *
 * Generally speaking, the repair code has the following code structure:
 * Lock -> scrub -> repair -> commit -> re-lock -> re-scrub -> unlock.
 * The first check helps us figure out if we need to rebuild or simply
 * optimize the structure so that the rebuild knows what to do.  The
 * second check evaluates the completeness of the repair; that is what
 * is reported to userspace.
 *
 * A quick note on symbol prefixes:
 * - "xfs_" are general XFS symbols.
 * - "xchk_" are symbols related to metadata checking.
 * - "xrep_" are symbols related to metadata repair.
 * - "xfs_scrub_" are symbols that tie online fsck to the rest of XFS.
 */

/*
 * Scrub probe -- userspace uses this to probe if we're willing to scrub
 * or repair a given mountpoint.  This will be used by xfs_scrub to
 * probe the kernel's abilities to scrub (and repair) the metadata.  We
 * do this by validating the ioctl inputs from userspace, preparing the
 * filesystem for a scrub (or a repair) operation, and immediately
 * returning to userspace.  Userspace can use the returned errno and
 * structure state to decide (in broad terms) if scrub/repair are
 * supported by the running kernel.
 */
static int
xchk_probe(
	struct xfs_scrub	*sc)
{
	int			error = 0;

	if (xchk_should_terminate(sc, &error))
		return error;

	return 0;
}

/* Scrub setup and teardown */

/* Free all the resources and finish the transactions. */
STATIC int
xchk_teardown(
	struct xfs_scrub	*sc,
	int			error)
{
	struct xfs_inode	*ip_in = XFS_I(file_inode(sc->file));

	xchk_ag_free(sc, &sc->sa);
	if (sc->tp) {
		if (error == 0 && (sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR))
			error = xfs_trans_commit(sc->tp);
		else
			xfs_trans_cancel(sc->tp);
		sc->tp = NULL;
	}
	if (sc->ip) {
		if (sc->ilock_flags)
			xfs_iunlock(sc->ip, sc->ilock_flags);
		if (sc->ip != ip_in &&
		    !xfs_internal_inum(sc->mp, sc->ip->i_ino))
			xfs_irele(sc->ip);
		sc->ip = NULL;
	}
	if (sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR)
		mnt_drop_write_file(sc->file);
	if (sc->flags & XCHK_REAPING_DISABLED)
		xchk_start_reaping(sc);
	if (sc->flags & XCHK_HAS_QUOTAOFFLOCK) {
		mutex_unlock(&sc->mp->m_quotainfo->qi_quotaofflock);
		sc->flags &= ~XCHK_HAS_QUOTAOFFLOCK;
	}
	if (sc->buf) {
		kmem_free(sc->buf);
		sc->buf = NULL;
	}
	return error;
}

/* Scrubbing dispatch. */

static const struct xchk_meta_ops meta_scrub_ops[] = {
	[XFS_SCRUB_TYPE_PROBE] = {	/* ioctl presence test */
		.type	= ST_NONE,
		.setup	= xchk_setup_fs,
		.scrub	= xchk_probe,
		.repair = xrep_probe,
	},
	[XFS_SCRUB_TYPE_SB] = {		/* superblock */
		.type	= ST_PERAG,
		.setup	= xchk_setup_fs,
		.scrub	= xchk_superblock,
		.repair	= xrep_superblock,
	},
	[XFS_SCRUB_TYPE_AGF] = {	/* agf */
		.type	= ST_PERAG,
		.setup	= xchk_setup_fs,
		.scrub	= xchk_agf,
		.repair	= xrep_agf,
	},
	[XFS_SCRUB_TYPE_AGFL]= {	/* agfl */
		.type	= ST_PERAG,
		.setup	= xchk_setup_fs,
		.scrub	= xchk_agfl,
		.repair	= xrep_agfl,
	},
	[XFS_SCRUB_TYPE_AGI] = {	/* agi */
		.type	= ST_PERAG,
		.setup	= xchk_setup_fs,
		.scrub	= xchk_agi,
		.repair	= xrep_agi,
	},
	[XFS_SCRUB_TYPE_BNOBT] = {	/* bnobt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_allocbt,
		.scrub	= xchk_bnobt,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_CNTBT] = {	/* cntbt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_allocbt,
		.scrub	= xchk_cntbt,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_INOBT] = {	/* inobt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_iallocbt,
		.scrub	= xchk_inobt,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_FINOBT] = {	/* finobt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_iallocbt,
		.scrub	= xchk_finobt,
		.has	= xfs_has_finobt,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_RMAPBT] = {	/* rmapbt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_rmapbt,
		.scrub	= xchk_rmapbt,
		.has	= xfs_has_rmapbt,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_REFCNTBT] = {	/* refcountbt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_refcountbt,
		.scrub	= xchk_refcountbt,
		.has	= xfs_has_reflink,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_INODE] = {	/* inode record */
		.type	= ST_INODE,
		.setup	= xchk_setup_inode,
		.scrub	= xchk_inode,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_BMBTD] = {	/* inode data fork */
		.type	= ST_INODE,
		.setup	= xchk_setup_inode_bmap,
		.scrub	= xchk_bmap_data,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_BMBTA] = {	/* inode attr fork */
		.type	= ST_INODE,
		.setup	= xchk_setup_inode_bmap,
		.scrub	= xchk_bmap_attr,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_BMBTC] = {	/* inode CoW fork */
		.type	= ST_INODE,
		.setup	= xchk_setup_inode_bmap,
		.scrub	= xchk_bmap_cow,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_DIR] = {	/* directory */
		.type	= ST_INODE,
		.setup	= xchk_setup_directory,
		.scrub	= xchk_directory,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_XATTR] = {	/* extended attributes */
		.type	= ST_INODE,
		.setup	= xchk_setup_xattr,
		.scrub	= xchk_xattr,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_SYMLINK] = {	/* symbolic link */
		.type	= ST_INODE,
		.setup	= xchk_setup_symlink,
		.scrub	= xchk_symlink,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_PARENT] = {	/* parent pointers */
		.type	= ST_INODE,
		.setup	= xchk_setup_parent,
		.scrub	= xchk_parent,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_RTBITMAP] = {	/* realtime bitmap */
		.type	= ST_FS,
		.setup	= xchk_setup_rt,
		.scrub	= xchk_rtbitmap,
		.has	= xfs_has_realtime,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_RTSUM] = {	/* realtime summary */
		.type	= ST_FS,
		.setup	= xchk_setup_rt,
		.scrub	= xchk_rtsummary,
		.has	= xfs_has_realtime,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_UQUOTA] = {	/* user quota */
		.type	= ST_FS,
		.setup	= xchk_setup_quota,
		.scrub	= xchk_quota,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_GQUOTA] = {	/* group quota */
		.type	= ST_FS,
		.setup	= xchk_setup_quota,
		.scrub	= xchk_quota,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_PQUOTA] = {	/* project quota */
		.type	= ST_FS,
		.setup	= xchk_setup_quota,
		.scrub	= xchk_quota,
		.repair	= xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_FSCOUNTERS] = {	/* fs summary counters */
		.type	= ST_FS,
		.setup	= xchk_setup_fscounters,
		.scrub	= xchk_fscounters,
		.repair	= xrep_notsupported,
	},
};

/* This isn't a stable feature, warn once per day. */
static inline void
xchk_experimental_warning(
	struct xfs_mount	*mp)
{
	static struct ratelimit_state scrub_warning = RATELIMIT_STATE_INIT(
			"xchk_warning", 86400 * HZ, 1);
	ratelimit_set_flags(&scrub_warning, RATELIMIT_MSG_ON_RELEASE);

	if (__ratelimit(&scrub_warning))
		xfs_alert(mp,
"EXPERIMENTAL online scrub feature in use. Use at your own risk!");
}

static int
xchk_validate_inputs(
	struct xfs_mount		*mp,
	struct xfs_scrub_metadata	*sm)
{
	int				error;
	const struct xchk_meta_ops	*ops;

	error = -EINVAL;
	/* Check our inputs. */
	sm->sm_flags &= ~XFS_SCRUB_FLAGS_OUT;
	if (sm->sm_flags & ~XFS_SCRUB_FLAGS_IN)
		goto out;
	/* sm_reserved[] must be zero */
	if (memchr_inv(sm->sm_reserved, 0, sizeof(sm->sm_reserved)))
		goto out;

	error = -ENOENT;
	/* Do we know about this type of metadata? */
	if (sm->sm_type >= XFS_SCRUB_TYPE_NR)
		goto out;
	ops = &meta_scrub_ops[sm->sm_type];
	if (ops->setup == NULL || ops->scrub == NULL)
		goto out;
	/* Does this fs even support this type of metadata? */
	if (ops->has && !ops->has(mp))
		goto out;

	error = -EINVAL;
	/* restricting fields must be appropriate for type */
	switch (ops->type) {
	case ST_NONE:
	case ST_FS:
		if (sm->sm_ino || sm->sm_gen || sm->sm_agno)
			goto out;
		break;
	case ST_PERAG:
		if (sm->sm_ino || sm->sm_gen ||
		    sm->sm_agno >= mp->m_sb.sb_agcount)
			goto out;
		break;
	case ST_INODE:
		if (sm->sm_agno || (sm->sm_gen && !sm->sm_ino))
			goto out;
		break;
	default:
		goto out;
	}

	/*
	 * We only want to repair read-write v5+ filesystems.  Defer the check
	 * for ops->repair until after our scrub confirms that we need to
	 * perform repairs so that we avoid failing due to not supporting
	 * repairing an object that doesn't need repairs.
	 */
	if (sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR) {
		error = -EOPNOTSUPP;
		if (!xfs_has_crc(mp))
			goto out;

		error = -EROFS;
		if (xfs_is_readonly(mp))
			goto out;
	}

	error = 0;
out:
	return error;
}

#ifdef CONFIG_XFS_ONLINE_REPAIR
static inline void xchk_postmortem(struct xfs_scrub *sc)
{
	/*
	 * Userspace asked us to repair something, we repaired it, rescanned
	 * it, and the rescan says it's still broken.  Scream about this in
	 * the system logs.
	 */
	if ((sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR) &&
	    (sc->sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
				 XFS_SCRUB_OFLAG_XCORRUPT)))
		xrep_failure(sc->mp);
}
#else
static inline void xchk_postmortem(struct xfs_scrub *sc)
{
	/*
	 * Userspace asked us to scrub something, it's broken, and we have no
	 * way of fixing it.  Scream in the logs.
	 */
	if (sc->sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
				XFS_SCRUB_OFLAG_XCORRUPT))
		xfs_alert_ratelimited(sc->mp,
				"Corruption detected during scrub.");
}
#endif /* CONFIG_XFS_ONLINE_REPAIR */

/* Dispatch metadata scrubbing. */
int
xfs_scrub_metadata(
	struct file			*file,
	struct xfs_scrub_metadata	*sm)
{
	struct xfs_scrub		*sc;
	struct xfs_mount		*mp = XFS_I(file_inode(file))->i_mount;
	int				error = 0;

	BUILD_BUG_ON(sizeof(meta_scrub_ops) !=
		(sizeof(struct xchk_meta_ops) * XFS_SCRUB_TYPE_NR));

	trace_xchk_start(XFS_I(file_inode(file)), sm, error);

	/* Forbidden if we are shut down or mounted norecovery. */
	error = -ESHUTDOWN;
	if (xfs_is_shutdown(mp))
		goto out;
	error = -ENOTRECOVERABLE;
	if (xfs_has_norecovery(mp))
		goto out;

	error = xchk_validate_inputs(mp, sm);
	if (error)
		goto out;

	xchk_experimental_warning(mp);

	sc = kmem_zalloc(sizeof(struct xfs_scrub), KM_NOFS | KM_MAYFAIL);
	if (!sc) {
		error = -ENOMEM;
		goto out;
	}

	sc->mp = mp;
	sc->file = file;
	sc->sm = sm;
	sc->ops = &meta_scrub_ops[sm->sm_type];
	sc->sick_mask = xchk_health_mask_for_scrub_type(sm->sm_type);
retry_op:
	/*
	 * When repairs are allowed, prevent freezing or readonly remount while
	 * scrub is running with a real transaction.
	 */
	if (sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR) {
		error = mnt_want_write_file(sc->file);
		if (error)
			goto out_sc;
	}

	/* Set up for the operation. */
	error = sc->ops->setup(sc);
	if (error)
		goto out_teardown;

	/* Scrub for errors. */
	error = sc->ops->scrub(sc);
	if (!(sc->flags & XCHK_TRY_HARDER) && error == -EDEADLOCK) {
		/*
		 * Scrubbers return -EDEADLOCK to mean 'try harder'.
		 * Tear down everything we hold, then set up again with
		 * preparation for worst-case scenarios.
		 */
		error = xchk_teardown(sc, 0);
		if (error)
			goto out_sc;
		sc->flags |= XCHK_TRY_HARDER;
		goto retry_op;
	} else if (error || (sm->sm_flags & XFS_SCRUB_OFLAG_INCOMPLETE))
		goto out_teardown;

	xchk_update_health(sc);

	if ((sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR) &&
	    !(sc->flags & XREP_ALREADY_FIXED)) {
		bool needs_fix;

		/* Let debug users force us into the repair routines. */
		if (XFS_TEST_ERROR(false, mp, XFS_ERRTAG_FORCE_SCRUB_REPAIR))
			sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;

		needs_fix = (sc->sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
						 XFS_SCRUB_OFLAG_XCORRUPT |
						 XFS_SCRUB_OFLAG_PREEN));
		/*
		 * If userspace asked for a repair but it wasn't necessary,
		 * report that back to userspace.
		 */
		if (!needs_fix) {
			sc->sm->sm_flags |= XFS_SCRUB_OFLAG_NO_REPAIR_NEEDED;
			goto out_nofix;
		}

		/*
		 * If it's broken, userspace wants us to fix it, and we haven't
		 * already tried to fix it, then attempt a repair.
		 */
		error = xrep_attempt(sc);
		if (error == -EAGAIN) {
			/*
			 * Either the repair function succeeded or it couldn't
			 * get all the resources it needs; either way, we go
			 * back to the beginning and call the scrub function.
			 */
			error = xchk_teardown(sc, 0);
			if (error) {
				xrep_failure(mp);
				goto out_sc;
			}
			goto retry_op;
		}
	}

out_nofix:
	xchk_postmortem(sc);
out_teardown:
	error = xchk_teardown(sc, error);
out_sc:
	kmem_free(sc);
out:
	trace_xchk_done(XFS_I(file_inode(file)), sm, error);
	if (error == -EFSCORRUPTED || error == -EFSBADCRC) {
		sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
		error = 0;
	}
	return error;
}
