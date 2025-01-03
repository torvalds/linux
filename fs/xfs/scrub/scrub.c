// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
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
#include "xfs_scrub.h"
#include "xfs_buf_mem.h"
#include "xfs_rmap.h"
#include "xfs_exchrange.h"
#include "xfs_exchmaps.h"
#include "xfs_dir2.h"
#include "xfs_parent.h"
#include "xfs_icache.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/health.h"
#include "scrub/stats.h"
#include "scrub/xfile.h"
#include "scrub/tempfile.h"
#include "scrub/orphanage.h"

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

static inline void
xchk_fsgates_disable(
	struct xfs_scrub	*sc)
{
	if (!(sc->flags & XCHK_FSGATES_ALL))
		return;

	trace_xchk_fsgates_disable(sc, sc->flags & XCHK_FSGATES_ALL);

	if (sc->flags & XCHK_FSGATES_DRAIN)
		xfs_drain_wait_disable();

	if (sc->flags & XCHK_FSGATES_QUOTA)
		xfs_dqtrx_hook_disable();

	if (sc->flags & XCHK_FSGATES_DIRENTS)
		xfs_dir_hook_disable();

	if (sc->flags & XCHK_FSGATES_RMAP)
		xfs_rmap_hook_disable();

	sc->flags &= ~XCHK_FSGATES_ALL;
}

/* Free the resources associated with a scrub subtype. */
void
xchk_scrub_free_subord(
	struct xfs_scrub_subord	*sub)
{
	struct xfs_scrub	*sc = sub->parent_sc;

	ASSERT(sc->ip == sub->sc.ip);
	ASSERT(sc->orphanage == sub->sc.orphanage);
	ASSERT(sc->tempip == sub->sc.tempip);

	sc->sm->sm_type = sub->old_smtype;
	sc->sm->sm_flags = sub->old_smflags |
				(sc->sm->sm_flags & XFS_SCRUB_FLAGS_OUT);
	sc->tp = sub->sc.tp;

	if (sub->sc.buf) {
		if (sub->sc.buf_cleanup)
			sub->sc.buf_cleanup(sub->sc.buf);
		kvfree(sub->sc.buf);
	}
	if (sub->sc.xmbtp)
		xmbuf_free(sub->sc.xmbtp);
	if (sub->sc.xfile)
		xfile_destroy(sub->sc.xfile);

	sc->ilock_flags = sub->sc.ilock_flags;
	sc->orphanage_ilock_flags = sub->sc.orphanage_ilock_flags;
	sc->temp_ilock_flags = sub->sc.temp_ilock_flags;

	kfree(sub);
}

/* Free all the resources and finish the transactions. */
STATIC int
xchk_teardown(
	struct xfs_scrub	*sc,
	int			error)
{
	xchk_ag_free(sc, &sc->sa);
	if (sc->tp) {
		if (error == 0 && (sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR))
			error = xfs_trans_commit(sc->tp);
		else
			xfs_trans_cancel(sc->tp);
		sc->tp = NULL;
	}
	if (sc->sr.rtg)
		xchk_rtgroup_free(sc, &sc->sr);
	if (sc->ip) {
		if (sc->ilock_flags)
			xchk_iunlock(sc, sc->ilock_flags);
		xchk_irele(sc, sc->ip);
		sc->ip = NULL;
	}
	if (sc->flags & XCHK_HAVE_FREEZE_PROT) {
		sc->flags &= ~XCHK_HAVE_FREEZE_PROT;
		mnt_drop_write_file(sc->file);
	}
	if (sc->xmbtp) {
		xmbuf_free(sc->xmbtp);
		sc->xmbtp = NULL;
	}
	if (sc->xfile) {
		xfile_destroy(sc->xfile);
		sc->xfile = NULL;
	}
	if (sc->buf) {
		if (sc->buf_cleanup)
			sc->buf_cleanup(sc->buf);
		kvfree(sc->buf);
		sc->buf_cleanup = NULL;
		sc->buf = NULL;
	}

	xrep_tempfile_rele(sc);
	xrep_orphanage_rele(sc);
	xchk_fsgates_disable(sc);
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
		.setup	= xchk_setup_agheader,
		.scrub	= xchk_superblock,
		.repair	= xrep_superblock,
	},
	[XFS_SCRUB_TYPE_AGF] = {	/* agf */
		.type	= ST_PERAG,
		.setup	= xchk_setup_agheader,
		.scrub	= xchk_agf,
		.repair	= xrep_agf,
	},
	[XFS_SCRUB_TYPE_AGFL]= {	/* agfl */
		.type	= ST_PERAG,
		.setup	= xchk_setup_agheader,
		.scrub	= xchk_agfl,
		.repair	= xrep_agfl,
	},
	[XFS_SCRUB_TYPE_AGI] = {	/* agi */
		.type	= ST_PERAG,
		.setup	= xchk_setup_agheader,
		.scrub	= xchk_agi,
		.repair	= xrep_agi,
	},
	[XFS_SCRUB_TYPE_BNOBT] = {	/* bnobt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_allocbt,
		.scrub	= xchk_allocbt,
		.repair	= xrep_allocbt,
		.repair_eval = xrep_revalidate_allocbt,
	},
	[XFS_SCRUB_TYPE_CNTBT] = {	/* cntbt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_allocbt,
		.scrub	= xchk_allocbt,
		.repair	= xrep_allocbt,
		.repair_eval = xrep_revalidate_allocbt,
	},
	[XFS_SCRUB_TYPE_INOBT] = {	/* inobt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_iallocbt,
		.scrub	= xchk_iallocbt,
		.repair	= xrep_iallocbt,
		.repair_eval = xrep_revalidate_iallocbt,
	},
	[XFS_SCRUB_TYPE_FINOBT] = {	/* finobt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_iallocbt,
		.scrub	= xchk_iallocbt,
		.has	= xfs_has_finobt,
		.repair	= xrep_iallocbt,
		.repair_eval = xrep_revalidate_iallocbt,
	},
	[XFS_SCRUB_TYPE_RMAPBT] = {	/* rmapbt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_rmapbt,
		.scrub	= xchk_rmapbt,
		.has	= xfs_has_rmapbt,
		.repair	= xrep_rmapbt,
	},
	[XFS_SCRUB_TYPE_REFCNTBT] = {	/* refcountbt */
		.type	= ST_PERAG,
		.setup	= xchk_setup_ag_refcountbt,
		.scrub	= xchk_refcountbt,
		.has	= xfs_has_reflink,
		.repair	= xrep_refcountbt,
	},
	[XFS_SCRUB_TYPE_INODE] = {	/* inode record */
		.type	= ST_INODE,
		.setup	= xchk_setup_inode,
		.scrub	= xchk_inode,
		.repair	= xrep_inode,
	},
	[XFS_SCRUB_TYPE_BMBTD] = {	/* inode data fork */
		.type	= ST_INODE,
		.setup	= xchk_setup_inode_bmap,
		.scrub	= xchk_bmap_data,
		.repair	= xrep_bmap_data,
	},
	[XFS_SCRUB_TYPE_BMBTA] = {	/* inode attr fork */
		.type	= ST_INODE,
		.setup	= xchk_setup_inode_bmap,
		.scrub	= xchk_bmap_attr,
		.repair	= xrep_bmap_attr,
	},
	[XFS_SCRUB_TYPE_BMBTC] = {	/* inode CoW fork */
		.type	= ST_INODE,
		.setup	= xchk_setup_inode_bmap,
		.scrub	= xchk_bmap_cow,
		.repair	= xrep_bmap_cow,
	},
	[XFS_SCRUB_TYPE_DIR] = {	/* directory */
		.type	= ST_INODE,
		.setup	= xchk_setup_directory,
		.scrub	= xchk_directory,
		.repair	= xrep_directory,
	},
	[XFS_SCRUB_TYPE_XATTR] = {	/* extended attributes */
		.type	= ST_INODE,
		.setup	= xchk_setup_xattr,
		.scrub	= xchk_xattr,
		.repair	= xrep_xattr,
	},
	[XFS_SCRUB_TYPE_SYMLINK] = {	/* symbolic link */
		.type	= ST_INODE,
		.setup	= xchk_setup_symlink,
		.scrub	= xchk_symlink,
		.repair	= xrep_symlink,
	},
	[XFS_SCRUB_TYPE_PARENT] = {	/* parent pointers */
		.type	= ST_INODE,
		.setup	= xchk_setup_parent,
		.scrub	= xchk_parent,
		.repair	= xrep_parent,
	},
	[XFS_SCRUB_TYPE_RTBITMAP] = {	/* realtime bitmap */
		.type	= ST_RTGROUP,
		.setup	= xchk_setup_rtbitmap,
		.scrub	= xchk_rtbitmap,
		.repair	= xrep_rtbitmap,
	},
	[XFS_SCRUB_TYPE_RTSUM] = {	/* realtime summary */
		.type	= ST_RTGROUP,
		.setup	= xchk_setup_rtsummary,
		.scrub	= xchk_rtsummary,
		.repair	= xrep_rtsummary,
	},
	[XFS_SCRUB_TYPE_UQUOTA] = {	/* user quota */
		.type	= ST_FS,
		.setup	= xchk_setup_quota,
		.scrub	= xchk_quota,
		.repair	= xrep_quota,
	},
	[XFS_SCRUB_TYPE_GQUOTA] = {	/* group quota */
		.type	= ST_FS,
		.setup	= xchk_setup_quota,
		.scrub	= xchk_quota,
		.repair	= xrep_quota,
	},
	[XFS_SCRUB_TYPE_PQUOTA] = {	/* project quota */
		.type	= ST_FS,
		.setup	= xchk_setup_quota,
		.scrub	= xchk_quota,
		.repair	= xrep_quota,
	},
	[XFS_SCRUB_TYPE_FSCOUNTERS] = {	/* fs summary counters */
		.type	= ST_FS,
		.setup	= xchk_setup_fscounters,
		.scrub	= xchk_fscounters,
		.repair	= xrep_fscounters,
	},
	[XFS_SCRUB_TYPE_QUOTACHECK] = {	/* quota counters */
		.type	= ST_FS,
		.setup	= xchk_setup_quotacheck,
		.scrub	= xchk_quotacheck,
		.repair	= xrep_quotacheck,
	},
	[XFS_SCRUB_TYPE_NLINKS] = {	/* inode link counts */
		.type	= ST_FS,
		.setup	= xchk_setup_nlinks,
		.scrub	= xchk_nlinks,
		.repair	= xrep_nlinks,
	},
	[XFS_SCRUB_TYPE_HEALTHY] = {	/* fs healthy; clean all reminders */
		.type	= ST_FS,
		.setup	= xchk_setup_fs,
		.scrub	= xchk_health_record,
		.repair = xrep_notsupported,
	},
	[XFS_SCRUB_TYPE_DIRTREE] = {	/* directory tree structure */
		.type	= ST_INODE,
		.setup	= xchk_setup_dirtree,
		.scrub	= xchk_dirtree,
		.has	= xfs_has_parent,
		.repair	= xrep_dirtree,
	},
	[XFS_SCRUB_TYPE_METAPATH] = {	/* metadata directory tree path */
		.type	= ST_GENERIC,
		.setup	= xchk_setup_metapath,
		.scrub	= xchk_metapath,
		.has	= xfs_has_metadir,
		.repair	= xrep_metapath,
	},
	[XFS_SCRUB_TYPE_RGSUPER] = {	/* realtime group superblock */
		.type	= ST_RTGROUP,
		.setup	= xchk_setup_rgsuperblock,
		.scrub	= xchk_rgsuperblock,
		.has	= xfs_has_rtsb,
		.repair = xrep_rgsuperblock,
	},
};

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
	case ST_GENERIC:
		break;
	case ST_RTGROUP:
		if (sm->sm_ino || sm->sm_gen)
			goto out;
		if (xfs_has_rtgroups(mp)) {
			/*
			 * On a rtgroups filesystem, there won't be an rtbitmap
			 * or rtsummary file for group 0 unless there's
			 * actually a realtime volume attached.  However, older
			 * xfs_scrub always calls the rtbitmap/rtsummary
			 * scrubbers with sm_agno==0 so transform the error
			 * code to ENOENT.
			 */
			if (sm->sm_agno >= mp->m_sb.sb_rgcount) {
				if (sm->sm_agno == 0)
					error = -ENOENT;
				goto out;
			}
		} else {
			/*
			 * Prior to rtgroups, the rtbitmap/rtsummary scrubbers
			 * accepted sm_agno==0, so we still accept that for
			 * scrubbing pre-rtgroups filesystems.
			 */
			if (sm->sm_agno != 0)
				goto out;
		}
		break;
	default:
		goto out;
	}

	/* No rebuild without repair. */
	if ((sm->sm_flags & XFS_SCRUB_IFLAG_FORCE_REBUILD) &&
	    !(sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR))
		return -EINVAL;

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

/*
 * Create a new scrub context from an existing one, but with a different scrub
 * type.
 */
struct xfs_scrub_subord *
xchk_scrub_create_subord(
	struct xfs_scrub	*sc,
	unsigned int		subtype)
{
	struct xfs_scrub_subord	*sub;

	sub = kzalloc(sizeof(*sub), XCHK_GFP_FLAGS);
	if (!sub)
		return ERR_PTR(-ENOMEM);

	sub->old_smtype = sc->sm->sm_type;
	sub->old_smflags = sc->sm->sm_flags;
	sub->parent_sc = sc;
	memcpy(&sub->sc, sc, sizeof(struct xfs_scrub));
	sub->sc.ops = &meta_scrub_ops[subtype];
	sub->sc.sm->sm_type = subtype;
	sub->sc.sm->sm_flags &= ~XFS_SCRUB_FLAGS_OUT;
	sub->sc.buf = NULL;
	sub->sc.buf_cleanup = NULL;
	sub->sc.xfile = NULL;
	sub->sc.xmbtp = NULL;

	return sub;
}

/* Dispatch metadata scrubbing. */
STATIC int
xfs_scrub_metadata(
	struct file			*file,
	struct xfs_scrub_metadata	*sm)
{
	struct xchk_stats_run		run = { };
	struct xfs_scrub		*sc;
	struct xfs_mount		*mp = XFS_I(file_inode(file))->i_mount;
	u64				check_start;
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

	xfs_warn_experimental(mp, XFS_EXPERIMENTAL_SCRUB);

	sc = kzalloc(sizeof(struct xfs_scrub), XCHK_GFP_FLAGS);
	if (!sc) {
		error = -ENOMEM;
		goto out;
	}

	sc->mp = mp;
	sc->file = file;
	sc->sm = sm;
	sc->ops = &meta_scrub_ops[sm->sm_type];
	sc->sick_mask = xchk_health_mask_for_scrub_type(sm->sm_type);
	sc->relax = INIT_XCHK_RELAX;
retry_op:
	/*
	 * When repairs are allowed, prevent freezing or readonly remount while
	 * scrub is running with a real transaction.
	 */
	if (sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR) {
		error = mnt_want_write_file(sc->file);
		if (error)
			goto out_sc;

		sc->flags |= XCHK_HAVE_FREEZE_PROT;
	}

	/* Set up for the operation. */
	error = sc->ops->setup(sc);
	if (error == -EDEADLOCK && !(sc->flags & XCHK_TRY_HARDER))
		goto try_harder;
	if (error == -ECHRNG && !(sc->flags & XCHK_NEED_DRAIN))
		goto need_drain;
	if (error)
		goto out_teardown;

	/* Scrub for errors. */
	check_start = xchk_stats_now();
	if ((sc->flags & XREP_ALREADY_FIXED) && sc->ops->repair_eval != NULL)
		error = sc->ops->repair_eval(sc);
	else
		error = sc->ops->scrub(sc);
	run.scrub_ns += xchk_stats_elapsed_ns(check_start);
	if (error == -EDEADLOCK && !(sc->flags & XCHK_TRY_HARDER))
		goto try_harder;
	if (error == -ECHRNG && !(sc->flags & XCHK_NEED_DRAIN))
		goto need_drain;
	if (error || (sm->sm_flags & XFS_SCRUB_OFLAG_INCOMPLETE))
		goto out_teardown;

	xchk_update_health(sc);

	if (xchk_could_repair(sc)) {
		/*
		 * If userspace asked for a repair but it wasn't necessary,
		 * report that back to userspace.
		 */
		if (!xrep_will_attempt(sc)) {
			sc->sm->sm_flags |= XFS_SCRUB_OFLAG_NO_REPAIR_NEEDED;
			goto out_nofix;
		}

		/*
		 * If it's broken, userspace wants us to fix it, and we haven't
		 * already tried to fix it, then attempt a repair.
		 */
		error = xrep_attempt(sc, &run);
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
	if (error != -ENOENT)
		xchk_stats_merge(mp, sm, &run);
	kfree(sc);
out:
	trace_xchk_done(XFS_I(file_inode(file)), sm, error);
	if (error == -EFSCORRUPTED || error == -EFSBADCRC) {
		sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
		error = 0;
	}
	return error;
need_drain:
	error = xchk_teardown(sc, 0);
	if (error)
		goto out_sc;
	sc->flags |= XCHK_NEED_DRAIN;
	run.retries++;
	goto retry_op;
try_harder:
	/*
	 * Scrubbers return -EDEADLOCK to mean 'try harder'.  Tear down
	 * everything we hold, then set up again with preparation for
	 * worst-case scenarios.
	 */
	error = xchk_teardown(sc, 0);
	if (error)
		goto out_sc;
	sc->flags |= XCHK_TRY_HARDER;
	run.retries++;
	goto retry_op;
}

/* Scrub one aspect of one piece of metadata. */
int
xfs_ioc_scrub_metadata(
	struct file			*file,
	void				__user *arg)
{
	struct xfs_scrub_metadata	scrub;
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&scrub, arg, sizeof(scrub)))
		return -EFAULT;

	error = xfs_scrub_metadata(file, &scrub);
	if (error)
		return error;

	if (copy_to_user(arg, &scrub, sizeof(scrub)))
		return -EFAULT;

	return 0;
}

/* Decide if there have been any scrub failures up to this point. */
static inline int
xfs_scrubv_check_barrier(
	struct xfs_mount		*mp,
	const struct xfs_scrub_vec	*vectors,
	const struct xfs_scrub_vec	*stop_vec)
{
	const struct xfs_scrub_vec	*v;
	__u32				failmask;

	failmask = stop_vec->sv_flags & XFS_SCRUB_FLAGS_OUT;

	for (v = vectors; v < stop_vec; v++) {
		if (v->sv_type == XFS_SCRUB_TYPE_BARRIER)
			continue;

		/*
		 * Runtime errors count as a previous failure, except the ones
		 * used to ask userspace to retry.
		 */
		switch (v->sv_ret) {
		case -EBUSY:
		case -ENOENT:
		case -EUSERS:
		case 0:
			break;
		default:
			return -ECANCELED;
		}

		/*
		 * If any of the out-flags on the scrub vector match the mask
		 * that was set on the barrier vector, that's a previous fail.
		 */
		if (v->sv_flags & failmask)
			return -ECANCELED;
	}

	return 0;
}

/*
 * If the caller provided us with a nonzero inode number that isn't the ioctl
 * file, try to grab a reference to it to eliminate all further untrusted inode
 * lookups.  If we can't get the inode, let each scrub function try again.
 */
STATIC struct xfs_inode *
xchk_scrubv_open_by_handle(
	struct xfs_mount		*mp,
	const struct xfs_scrub_vec_head	*head)
{
	struct xfs_trans		*tp;
	struct xfs_inode		*ip;
	int				error;

	error = xfs_trans_alloc_empty(mp, &tp);
	if (error)
		return NULL;

	error = xfs_iget(mp, tp, head->svh_ino, XCHK_IGET_FLAGS, 0, &ip);
	xfs_trans_cancel(tp);
	if (error)
		return NULL;

	if (VFS_I(ip)->i_generation != head->svh_gen) {
		xfs_irele(ip);
		return NULL;
	}

	return ip;
}

/* Vectored scrub implementation to reduce ioctl calls. */
int
xfs_ioc_scrubv_metadata(
	struct file			*file,
	void				__user *arg)
{
	struct xfs_scrub_vec_head	head;
	struct xfs_scrub_vec_head	__user *uhead = arg;
	struct xfs_scrub_vec		*vectors;
	struct xfs_scrub_vec		__user *uvectors;
	struct xfs_inode		*ip_in = XFS_I(file_inode(file));
	struct xfs_mount		*mp = ip_in->i_mount;
	struct xfs_inode		*handle_ip = NULL;
	struct xfs_scrub_vec		*v;
	size_t				vec_bytes;
	unsigned int			i;
	int				error = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&head, uhead, sizeof(head)))
		return -EFAULT;

	if (head.svh_reserved)
		return -EINVAL;
	if (head.svh_flags & ~XFS_SCRUB_VEC_FLAGS_ALL)
		return -EINVAL;
	if (head.svh_nr == 0)
		return 0;

	vec_bytes = array_size(head.svh_nr, sizeof(struct xfs_scrub_vec));
	if (vec_bytes > PAGE_SIZE)
		return -ENOMEM;

	uvectors = u64_to_user_ptr(head.svh_vectors);
	vectors = memdup_user(uvectors, vec_bytes);
	if (IS_ERR(vectors))
		return PTR_ERR(vectors);

	trace_xchk_scrubv_start(ip_in, &head);

	for (i = 0, v = vectors; i < head.svh_nr; i++, v++) {
		if (v->sv_reserved) {
			error = -EINVAL;
			goto out_free;
		}

		if (v->sv_type == XFS_SCRUB_TYPE_BARRIER &&
		    (v->sv_flags & ~XFS_SCRUB_FLAGS_OUT)) {
			error = -EINVAL;
			goto out_free;
		}

		trace_xchk_scrubv_item(mp, &head, i, v);
	}

	/*
	 * If the caller wants us to do a scrub-by-handle and the file used to
	 * call the ioctl is not the same file, load the incore inode and pin
	 * it across all the scrubv actions to avoid repeated UNTRUSTED
	 * lookups.  The reference is not passed to deeper layers of scrub
	 * because each scrubber gets to decide its own strategy and return
	 * values for getting an inode.
	 */
	if (head.svh_ino && head.svh_ino != ip_in->i_ino)
		handle_ip = xchk_scrubv_open_by_handle(mp, &head);

	/* Run all the scrubbers. */
	for (i = 0, v = vectors; i < head.svh_nr; i++, v++) {
		struct xfs_scrub_metadata	sm = {
			.sm_type		= v->sv_type,
			.sm_flags		= v->sv_flags,
			.sm_ino			= head.svh_ino,
			.sm_gen			= head.svh_gen,
			.sm_agno		= head.svh_agno,
		};

		if (v->sv_type == XFS_SCRUB_TYPE_BARRIER) {
			v->sv_ret = xfs_scrubv_check_barrier(mp, vectors, v);
			if (v->sv_ret) {
				trace_xchk_scrubv_barrier_fail(mp, &head, i, v);
				break;
			}

			continue;
		}

		v->sv_ret = xfs_scrub_metadata(file, &sm);
		v->sv_flags = sm.sm_flags;

		trace_xchk_scrubv_outcome(mp, &head, i, v);

		if (head.svh_rest_us) {
			ktime_t		expires;

			expires = ktime_add_ns(ktime_get(),
					head.svh_rest_us * 1000);
			set_current_state(TASK_KILLABLE);
			schedule_hrtimeout(&expires, HRTIMER_MODE_ABS);
		}

		if (fatal_signal_pending(current)) {
			error = -EINTR;
			goto out_free;
		}
	}

	if (copy_to_user(uvectors, vectors, vec_bytes) ||
	    copy_to_user(uhead, &head, sizeof(head))) {
		error = -EFAULT;
		goto out_free;
	}

out_free:
	if (handle_ip)
		xfs_irele(handle_ip);
	kfree(vectors);
	return error;
}
