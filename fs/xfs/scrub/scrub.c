/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_itable.h"
#include "xfs_alloc.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_refcount.h"
#include "xfs_refcount_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_quota.h"
#include "xfs_qm.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_log.h"
#include "xfs_trans_priv.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/btree.h"
#include "scrub/repair.h"

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
xfs_scrub_probe(
	struct xfs_scrub_context	*sc)
{
	int				error = 0;

	if (xfs_scrub_should_terminate(sc, &error))
		return error;

	return 0;
}

/* Scrub setup and teardown */

/* Free all the resources and finish the transactions. */
STATIC int
xfs_scrub_teardown(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip_in,
	int				error)
{
	xfs_scrub_ag_free(sc, &sc->sa);
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
			iput(VFS_I(sc->ip));
		sc->ip = NULL;
	}
	if (sc->has_quotaofflock)
		mutex_unlock(&sc->mp->m_quotainfo->qi_quotaofflock);
	if (sc->buf) {
		kmem_free(sc->buf);
		sc->buf = NULL;
	}
	return error;
}

/* Scrubbing dispatch. */

static const struct xfs_scrub_meta_ops meta_scrub_ops[] = {
	[XFS_SCRUB_TYPE_PROBE] = {	/* ioctl presence test */
		.type	= ST_NONE,
		.setup	= xfs_scrub_setup_fs,
		.scrub	= xfs_scrub_probe,
		.repair = xfs_repair_probe,
	},
	[XFS_SCRUB_TYPE_SB] = {		/* superblock */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_fs,
		.scrub	= xfs_scrub_superblock,
		.repair	= xfs_repair_superblock,
	},
	[XFS_SCRUB_TYPE_AGF] = {	/* agf */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_fs,
		.scrub	= xfs_scrub_agf,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_AGFL]= {	/* agfl */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_fs,
		.scrub	= xfs_scrub_agfl,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_AGI] = {	/* agi */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_fs,
		.scrub	= xfs_scrub_agi,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_BNOBT] = {	/* bnobt */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_ag_allocbt,
		.scrub	= xfs_scrub_bnobt,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_CNTBT] = {	/* cntbt */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_ag_allocbt,
		.scrub	= xfs_scrub_cntbt,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_INOBT] = {	/* inobt */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_ag_iallocbt,
		.scrub	= xfs_scrub_inobt,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_FINOBT] = {	/* finobt */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_ag_iallocbt,
		.scrub	= xfs_scrub_finobt,
		.has	= xfs_sb_version_hasfinobt,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_RMAPBT] = {	/* rmapbt */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_ag_rmapbt,
		.scrub	= xfs_scrub_rmapbt,
		.has	= xfs_sb_version_hasrmapbt,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_REFCNTBT] = {	/* refcountbt */
		.type	= ST_PERAG,
		.setup	= xfs_scrub_setup_ag_refcountbt,
		.scrub	= xfs_scrub_refcountbt,
		.has	= xfs_sb_version_hasreflink,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_INODE] = {	/* inode record */
		.type	= ST_INODE,
		.setup	= xfs_scrub_setup_inode,
		.scrub	= xfs_scrub_inode,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_BMBTD] = {	/* inode data fork */
		.type	= ST_INODE,
		.setup	= xfs_scrub_setup_inode_bmap,
		.scrub	= xfs_scrub_bmap_data,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_BMBTA] = {	/* inode attr fork */
		.type	= ST_INODE,
		.setup	= xfs_scrub_setup_inode_bmap,
		.scrub	= xfs_scrub_bmap_attr,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_BMBTC] = {	/* inode CoW fork */
		.type	= ST_INODE,
		.setup	= xfs_scrub_setup_inode_bmap,
		.scrub	= xfs_scrub_bmap_cow,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_DIR] = {	/* directory */
		.type	= ST_INODE,
		.setup	= xfs_scrub_setup_directory,
		.scrub	= xfs_scrub_directory,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_XATTR] = {	/* extended attributes */
		.type	= ST_INODE,
		.setup	= xfs_scrub_setup_xattr,
		.scrub	= xfs_scrub_xattr,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_SYMLINK] = {	/* symbolic link */
		.type	= ST_INODE,
		.setup	= xfs_scrub_setup_symlink,
		.scrub	= xfs_scrub_symlink,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_PARENT] = {	/* parent pointers */
		.type	= ST_INODE,
		.setup	= xfs_scrub_setup_parent,
		.scrub	= xfs_scrub_parent,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_RTBITMAP] = {	/* realtime bitmap */
		.type	= ST_FS,
		.setup	= xfs_scrub_setup_rt,
		.scrub	= xfs_scrub_rtbitmap,
		.has	= xfs_sb_version_hasrealtime,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_RTSUM] = {	/* realtime summary */
		.type	= ST_FS,
		.setup	= xfs_scrub_setup_rt,
		.scrub	= xfs_scrub_rtsummary,
		.has	= xfs_sb_version_hasrealtime,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_UQUOTA] = {	/* user quota */
		.type	= ST_FS,
		.setup	= xfs_scrub_setup_quota,
		.scrub	= xfs_scrub_quota,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_GQUOTA] = {	/* group quota */
		.type	= ST_FS,
		.setup	= xfs_scrub_setup_quota,
		.scrub	= xfs_scrub_quota,
		.repair	= xfs_repair_notsupported,
	},
	[XFS_SCRUB_TYPE_PQUOTA] = {	/* project quota */
		.type	= ST_FS,
		.setup	= xfs_scrub_setup_quota,
		.scrub	= xfs_scrub_quota,
		.repair	= xfs_repair_notsupported,
	},
};

/* This isn't a stable feature, warn once per day. */
static inline void
xfs_scrub_experimental_warning(
	struct xfs_mount	*mp)
{
	static struct ratelimit_state scrub_warning = RATELIMIT_STATE_INIT(
			"xfs_scrub_warning", 86400 * HZ, 1);
	ratelimit_set_flags(&scrub_warning, RATELIMIT_MSG_ON_RELEASE);

	if (__ratelimit(&scrub_warning))
		xfs_alert(mp,
"EXPERIMENTAL online scrub feature in use. Use at your own risk!");
}

static int
xfs_scrub_validate_inputs(
	struct xfs_mount		*mp,
	struct xfs_scrub_metadata	*sm)
{
	int				error;
	const struct xfs_scrub_meta_ops	*ops;

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
	if (ops->has && !ops->has(&mp->m_sb))
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

	error = -EOPNOTSUPP;
	/*
	 * We won't scrub any filesystem that doesn't have the ability
	 * to record unwritten extents.  The option was made default in
	 * 2003, removed from mkfs in 2007, and cannot be disabled in
	 * v5, so if we find a filesystem without this flag it's either
	 * really old or totally unsupported.  Avoid it either way.
	 * We also don't support v1-v3 filesystems, which aren't
	 * mountable.
	 */
	if (!xfs_sb_version_hasextflgbit(&mp->m_sb))
		goto out;

	/*
	 * We only want to repair read-write v5+ filesystems.  Defer the check
	 * for ops->repair until after our scrub confirms that we need to
	 * perform repairs so that we avoid failing due to not supporting
	 * repairing an object that doesn't need repairs.
	 */
	if (sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR) {
		error = -EOPNOTSUPP;
		if (!xfs_sb_version_hascrc(&mp->m_sb))
			goto out;

		error = -EROFS;
		if (mp->m_flags & XFS_MOUNT_RDONLY)
			goto out;
	}

	error = 0;
out:
	return error;
}

#ifdef CONFIG_XFS_ONLINE_REPAIR
static inline void xfs_scrub_postmortem(struct xfs_scrub_context *sc)
{
	/*
	 * Userspace asked us to repair something, we repaired it, rescanned
	 * it, and the rescan says it's still broken.  Scream about this in
	 * the system logs.
	 */
	if ((sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR) &&
	    (sc->sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
				 XFS_SCRUB_OFLAG_XCORRUPT)))
		xfs_repair_failure(sc->mp);
}
#else
static inline void xfs_scrub_postmortem(struct xfs_scrub_context *sc)
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
	struct xfs_inode		*ip,
	struct xfs_scrub_metadata	*sm)
{
	struct xfs_scrub_context	sc;
	struct xfs_mount		*mp = ip->i_mount;
	bool				try_harder = false;
	bool				already_fixed = false;
	int				error = 0;

	BUILD_BUG_ON(sizeof(meta_scrub_ops) !=
		(sizeof(struct xfs_scrub_meta_ops) * XFS_SCRUB_TYPE_NR));

	trace_xfs_scrub_start(ip, sm, error);

	/* Forbidden if we are shut down or mounted norecovery. */
	error = -ESHUTDOWN;
	if (XFS_FORCED_SHUTDOWN(mp))
		goto out;
	error = -ENOTRECOVERABLE;
	if (mp->m_flags & XFS_MOUNT_NORECOVERY)
		goto out;

	error = xfs_scrub_validate_inputs(mp, sm);
	if (error)
		goto out;

	xfs_scrub_experimental_warning(mp);

retry_op:
	/* Set up for the operation. */
	memset(&sc, 0, sizeof(sc));
	sc.mp = ip->i_mount;
	sc.sm = sm;
	sc.ops = &meta_scrub_ops[sm->sm_type];
	sc.try_harder = try_harder;
	sc.sa.agno = NULLAGNUMBER;
	error = sc.ops->setup(&sc, ip);
	if (error)
		goto out_teardown;

	/* Scrub for errors. */
	error = sc.ops->scrub(&sc);
	if (!try_harder && error == -EDEADLOCK) {
		/*
		 * Scrubbers return -EDEADLOCK to mean 'try harder'.
		 * Tear down everything we hold, then set up again with
		 * preparation for worst-case scenarios.
		 */
		error = xfs_scrub_teardown(&sc, ip, 0);
		if (error)
			goto out;
		try_harder = true;
		goto retry_op;
	} else if (error)
		goto out_teardown;

	if ((sc.sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR) && !already_fixed) {
		bool needs_fix;

		/* Let debug users force us into the repair routines. */
		if (XFS_TEST_ERROR(false, mp, XFS_ERRTAG_FORCE_SCRUB_REPAIR))
			sc.sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;

		needs_fix = (sc.sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
						XFS_SCRUB_OFLAG_XCORRUPT |
						XFS_SCRUB_OFLAG_PREEN));
		/*
		 * If userspace asked for a repair but it wasn't necessary,
		 * report that back to userspace.
		 */
		if (!needs_fix) {
			sc.sm->sm_flags |= XFS_SCRUB_OFLAG_NO_REPAIR_NEEDED;
			goto out_nofix;
		}

		/*
		 * If it's broken, userspace wants us to fix it, and we haven't
		 * already tried to fix it, then attempt a repair.
		 */
		error = xfs_repair_attempt(ip, &sc, &already_fixed);
		if (error == -EAGAIN) {
			if (sc.try_harder)
				try_harder = true;
			error = xfs_scrub_teardown(&sc, ip, 0);
			if (error) {
				xfs_repair_failure(mp);
				goto out;
			}
			goto retry_op;
		}
	}

out_nofix:
	xfs_scrub_postmortem(&sc);
out_teardown:
	error = xfs_scrub_teardown(&sc, ip, error);
out:
	trace_xfs_scrub_done(ip, sm, error);
	if (error == -EFSCORRUPTED || error == -EFSBADCRC) {
		sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
		error = 0;
	}
	return error;
}
