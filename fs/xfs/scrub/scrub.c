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
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/btree.h"

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

	if (sc->sm->sm_ino || sc->sm->sm_agno)
		return -EINVAL;
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
		xfs_trans_cancel(sc->tp);
		sc->tp = NULL;
	}
	if (sc->ip) {
		xfs_iunlock(sc->ip, sc->ilock_flags);
		if (sc->ip != ip_in &&
		    !xfs_internal_inum(sc->mp, sc->ip->i_ino))
			iput(VFS_I(sc->ip));
		sc->ip = NULL;
	}
	if (sc->buf) {
		kmem_free(sc->buf);
		sc->buf = NULL;
	}
	return error;
}

/* Scrubbing dispatch. */

static const struct xfs_scrub_meta_ops meta_scrub_ops[] = {
	{ /* ioctl presence test */
		.setup	= xfs_scrub_setup_fs,
		.scrub	= xfs_scrub_probe,
	},
	{ /* superblock */
		.setup	= xfs_scrub_setup_ag_header,
		.scrub	= xfs_scrub_superblock,
	},
	{ /* agf */
		.setup	= xfs_scrub_setup_ag_header,
		.scrub	= xfs_scrub_agf,
	},
	{ /* agfl */
		.setup	= xfs_scrub_setup_ag_header,
		.scrub	= xfs_scrub_agfl,
	},
	{ /* agi */
		.setup	= xfs_scrub_setup_ag_header,
		.scrub	= xfs_scrub_agi,
	},
	{ /* bnobt */
		.setup	= xfs_scrub_setup_ag_allocbt,
		.scrub	= xfs_scrub_bnobt,
	},
	{ /* cntbt */
		.setup	= xfs_scrub_setup_ag_allocbt,
		.scrub	= xfs_scrub_cntbt,
	},
	{ /* inobt */
		.setup	= xfs_scrub_setup_ag_iallocbt,
		.scrub	= xfs_scrub_inobt,
	},
	{ /* finobt */
		.setup	= xfs_scrub_setup_ag_iallocbt,
		.scrub	= xfs_scrub_finobt,
		.has	= xfs_sb_version_hasfinobt,
	},
	{ /* rmapbt */
		.setup	= xfs_scrub_setup_ag_rmapbt,
		.scrub	= xfs_scrub_rmapbt,
		.has	= xfs_sb_version_hasrmapbt,
	},
	{ /* refcountbt */
		.setup	= xfs_scrub_setup_ag_refcountbt,
		.scrub	= xfs_scrub_refcountbt,
		.has	= xfs_sb_version_hasreflink,
	},
	{ /* inode record */
		.setup	= xfs_scrub_setup_inode,
		.scrub	= xfs_scrub_inode,
	},
	{ /* inode data fork */
		.setup	= xfs_scrub_setup_inode_bmap,
		.scrub	= xfs_scrub_bmap_data,
	},
	{ /* inode attr fork */
		.setup	= xfs_scrub_setup_inode_bmap,
		.scrub	= xfs_scrub_bmap_attr,
	},
	{ /* inode CoW fork */
		.setup	= xfs_scrub_setup_inode_bmap,
		.scrub	= xfs_scrub_bmap_cow,
	},
	{ /* directory */
		.setup	= xfs_scrub_setup_directory,
		.scrub	= xfs_scrub_directory,
	},
	{ /* extended attributes */
		.setup	= xfs_scrub_setup_xattr,
		.scrub	= xfs_scrub_xattr,
	},
	{ /* symbolic link */
		.setup	= xfs_scrub_setup_symlink,
		.scrub	= xfs_scrub_symlink,
	},
	{ /* parent pointers */
		.setup	= xfs_scrub_setup_parent,
		.scrub	= xfs_scrub_parent,
	},
	{ /* realtime bitmap */
		.setup	= xfs_scrub_setup_rt,
		.scrub	= xfs_scrub_rtbitmap,
		.has	= xfs_sb_version_hasrealtime,
	},
	{ /* realtime summary */
		.setup	= xfs_scrub_setup_rt,
		.scrub	= xfs_scrub_rtsummary,
		.has	= xfs_sb_version_hasrealtime,
	},
	{ /* user quota */
		.setup = xfs_scrub_setup_quota,
		.scrub = xfs_scrub_quota,
	},
	{ /* group quota */
		.setup = xfs_scrub_setup_quota,
		.scrub = xfs_scrub_quota,
	},
	{ /* project quota */
		.setup = xfs_scrub_setup_quota,
		.scrub = xfs_scrub_quota,
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

/* Dispatch metadata scrubbing. */
int
xfs_scrub_metadata(
	struct xfs_inode		*ip,
	struct xfs_scrub_metadata	*sm)
{
	struct xfs_scrub_context	sc;
	struct xfs_mount		*mp = ip->i_mount;
	const struct xfs_scrub_meta_ops	*ops;
	bool				try_harder = false;
	int				error = 0;

	trace_xfs_scrub_start(ip, sm, error);

	/* Forbidden if we are shut down or mounted norecovery. */
	error = -ESHUTDOWN;
	if (XFS_FORCED_SHUTDOWN(mp))
		goto out;
	error = -ENOTRECOVERABLE;
	if (mp->m_flags & XFS_MOUNT_NORECOVERY)
		goto out;

	/* Check our inputs. */
	error = -EINVAL;
	sm->sm_flags &= ~XFS_SCRUB_FLAGS_OUT;
	if (sm->sm_flags & ~XFS_SCRUB_FLAGS_IN)
		goto out;
	if (memchr_inv(sm->sm_reserved, 0, sizeof(sm->sm_reserved)))
		goto out;

	/* Do we know about this type of metadata? */
	error = -ENOENT;
	if (sm->sm_type >= XFS_SCRUB_TYPE_NR)
		goto out;
	ops = &meta_scrub_ops[sm->sm_type];
	if (ops->scrub == NULL)
		goto out;

	/*
	 * We won't scrub any filesystem that doesn't have the ability
	 * to record unwritten extents.  The option was made default in
	 * 2003, removed from mkfs in 2007, and cannot be disabled in
	 * v5, so if we find a filesystem without this flag it's either
	 * really old or totally unsupported.  Avoid it either way.
	 * We also don't support v1-v3 filesystems, which aren't
	 * mountable.
	 */
	error = -EOPNOTSUPP;
	if (!xfs_sb_version_hasextflgbit(&mp->m_sb))
		goto out;

	/* Does this fs even support this type of metadata? */
	error = -ENOENT;
	if (ops->has && !ops->has(&mp->m_sb))
		goto out;

	/* We don't know how to repair anything yet. */
	error = -EOPNOTSUPP;
	if (sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR)
		goto out;

	xfs_scrub_experimental_warning(mp);

retry_op:
	/* Set up for the operation. */
	memset(&sc, 0, sizeof(sc));
	sc.mp = ip->i_mount;
	sc.sm = sm;
	sc.ops = ops;
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

	if (sc.sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
			       XFS_SCRUB_OFLAG_XCORRUPT))
		xfs_alert_ratelimited(mp, "Corruption detected during scrub.");

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
