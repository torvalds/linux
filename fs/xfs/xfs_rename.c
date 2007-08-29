/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_refcache.h"
#include "xfs_utils.h"
#include "xfs_trans_space.h"


/*
 * Given an array of up to 4 inode pointers, unlock the pointed to inodes.
 * If there are fewer than 4 entries in the array, the empty entries will
 * be at the end and will have NULL pointers in them.
 */
STATIC void
xfs_rename_unlock4(
	xfs_inode_t	**i_tab,
	uint		lock_mode)
{
	int	i;

	xfs_iunlock(i_tab[0], lock_mode);
	for (i = 1; i < 4; i++) {
		if (i_tab[i] == NULL) {
			break;
		}
		/*
		 * Watch out for duplicate entries in the table.
		 */
		if (i_tab[i] != i_tab[i-1]) {
			xfs_iunlock(i_tab[i], lock_mode);
		}
	}
}

#ifdef DEBUG
int xfs_rename_skip, xfs_rename_nskip;
#endif

/*
 * The following routine will acquire the locks required for a rename
 * operation. The code understands the semantics of renames and will
 * validate that name1 exists under dp1 & that name2 may or may not
 * exist under dp2.
 *
 * We are renaming dp1/name1 to dp2/name2.
 *
 * Return ENOENT if dp1 does not exist, other lookup errors, or 0 for success.
 */
STATIC int
xfs_lock_for_rename(
	xfs_inode_t	*dp1,	/* old (source) directory inode */
	xfs_inode_t	*dp2,	/* new (target) directory inode */
	bhv_vname_t	*vname1,/* old entry name */
	bhv_vname_t	*vname2,/* new entry name */
	xfs_inode_t	**ipp1,	/* inode of old entry */
	xfs_inode_t	**ipp2,	/* inode of new entry, if it
				   already exists, NULL otherwise. */
	xfs_inode_t	**i_tab,/* array of inode returned, sorted */
	int		*num_inodes)  /* number of inodes in array */
{
	xfs_inode_t		*ip1, *ip2, *temp;
	xfs_ino_t		inum1, inum2;
	int			error;
	int			i, j;
	uint			lock_mode;
	int			diff_dirs = (dp1 != dp2);

	ip2 = NULL;

	/*
	 * First, find out the current inums of the entries so that we
	 * can determine the initial locking order.  We'll have to
	 * sanity check stuff after all the locks have been acquired
	 * to see if we still have the right inodes, directories, etc.
	 */
	lock_mode = xfs_ilock_map_shared(dp1);
	error = xfs_get_dir_entry(vname1, &ip1);
	if (error) {
		xfs_iunlock_map_shared(dp1, lock_mode);
		return error;
	}

	inum1 = ip1->i_ino;

	ASSERT(ip1);
	ITRACE(ip1);

	/*
	 * Unlock dp1 and lock dp2 if they are different.
	 */

	if (diff_dirs) {
		xfs_iunlock_map_shared(dp1, lock_mode);
		lock_mode = xfs_ilock_map_shared(dp2);
	}

	error = xfs_dir_lookup_int(dp2, lock_mode, vname2, &inum2, &ip2);
	if (error == ENOENT) {		/* target does not need to exist. */
		inum2 = 0;
	} else if (error) {
		/*
		 * If dp2 and dp1 are the same, the next line unlocks dp1.
		 * Got it?
		 */
		xfs_iunlock_map_shared(dp2, lock_mode);
		IRELE (ip1);
		return error;
	} else {
		ITRACE(ip2);
	}

	/*
	 * i_tab contains a list of pointers to inodes.  We initialize
	 * the table here & we'll sort it.  We will then use it to
	 * order the acquisition of the inode locks.
	 *
	 * Note that the table may contain duplicates.  e.g., dp1 == dp2.
	 */
	i_tab[0] = dp1;
	i_tab[1] = dp2;
	i_tab[2] = ip1;
	if (inum2 == 0) {
		*num_inodes = 3;
		i_tab[3] = NULL;
	} else {
		*num_inodes = 4;
		i_tab[3] = ip2;
	}

	/*
	 * Sort the elements via bubble sort.  (Remember, there are at
	 * most 4 elements to sort, so this is adequate.)
	 */
	for (i=0; i < *num_inodes; i++) {
		for (j=1; j < *num_inodes; j++) {
			if (i_tab[j]->i_ino < i_tab[j-1]->i_ino) {
				temp = i_tab[j];
				i_tab[j] = i_tab[j-1];
				i_tab[j-1] = temp;
			}
		}
	}

	/*
	 * We have dp2 locked. If it isn't first, unlock it.
	 * If it is first, tell xfs_lock_inodes so it can skip it
	 * when locking. if dp1 == dp2, xfs_lock_inodes will skip both
	 * since they are equal. xfs_lock_inodes needs all these inodes
	 * so that it can unlock and retry if there might be a dead-lock
	 * potential with the log.
	 */

	if (i_tab[0] == dp2 && lock_mode == XFS_ILOCK_SHARED) {
#ifdef DEBUG
		xfs_rename_skip++;
#endif
		xfs_lock_inodes(i_tab, *num_inodes, 1, XFS_ILOCK_SHARED);
	} else {
#ifdef DEBUG
		xfs_rename_nskip++;
#endif
		xfs_iunlock_map_shared(dp2, lock_mode);
		xfs_lock_inodes(i_tab, *num_inodes, 0, XFS_ILOCK_SHARED);
	}

	/*
	 * Set the return value. Null out any unused entries in i_tab.
	 */
	*ipp1 = *ipp2 = NULL;
	for (i=0; i < *num_inodes; i++) {
		if (i_tab[i]->i_ino == inum1) {
			*ipp1 = i_tab[i];
		}
		if (i_tab[i]->i_ino == inum2) {
			*ipp2 = i_tab[i];
		}
	}
	for (;i < 4; i++) {
		i_tab[i] = NULL;
	}
	return 0;
}

/*
 * xfs_rename
 */
int
xfs_rename(
	xfs_inode_t	*src_dp,
	bhv_vname_t	*src_vname,
	bhv_vnode_t	*target_dir_vp,
	bhv_vname_t	*target_vname)
{
	bhv_vnode_t	*src_dir_vp = XFS_ITOV(src_dp);
	xfs_trans_t	*tp;
	xfs_inode_t	*target_dp, *src_ip, *target_ip;
	xfs_mount_t	*mp = src_dp->i_mount;
	int		new_parent;		/* moving to a new dir */
	int		src_is_directory;	/* src_name is a directory */
	int		error;
	xfs_bmap_free_t free_list;
	xfs_fsblock_t   first_block;
	int		cancel_flags;
	int		committed;
	xfs_inode_t	*inodes[4];
	int		target_ip_dropped = 0;	/* dropped target_ip link? */
	int		spaceres;
	int		target_link_zero = 0;
	int		num_inodes;
	char		*src_name = VNAME(src_vname);
	char		*target_name = VNAME(target_vname);
	int		src_namelen = VNAMELEN(src_vname);
	int		target_namelen = VNAMELEN(target_vname);

	vn_trace_entry(src_dp, "xfs_rename", (inst_t *)__return_address);
	vn_trace_entry(xfs_vtoi(target_dir_vp), "xfs_rename", (inst_t *)__return_address);

	/*
	 * Find the XFS behavior descriptor for the target directory
	 * vnode since it was not handed to us.
	 */
	target_dp = xfs_vtoi(target_dir_vp);
	if (target_dp == NULL) {
		return XFS_ERROR(EXDEV);
	}

	if (DM_EVENT_ENABLED(src_dp, DM_EVENT_RENAME) ||
	    DM_EVENT_ENABLED(target_dp, DM_EVENT_RENAME)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_RENAME,
					src_dir_vp, DM_RIGHT_NULL,
					target_dir_vp, DM_RIGHT_NULL,
					src_name, target_name,
					0, 0, 0);
		if (error) {
			return error;
		}
	}
	/* Return through std_return after this point. */

	/*
	 * Lock all the participating inodes. Depending upon whether
	 * the target_name exists in the target directory, and
	 * whether the target directory is the same as the source
	 * directory, we can lock from 2 to 4 inodes.
	 * xfs_lock_for_rename() will return ENOENT if src_name
	 * does not exist in the source directory.
	 */
	tp = NULL;
	error = xfs_lock_for_rename(src_dp, target_dp, src_vname,
			target_vname, &src_ip, &target_ip, inodes,
			&num_inodes);

	if (error) {
		/*
		 * We have nothing locked, no inode references, and
		 * no transaction, so just get out.
		 */
		goto std_return;
	}

	ASSERT(src_ip != NULL);

	if ((src_ip->i_d.di_mode & S_IFMT) == S_IFDIR) {
		/*
		 * Check for link count overflow on target_dp
		 */
		if (target_ip == NULL && (src_dp != target_dp) &&
		    target_dp->i_d.di_nlink >= XFS_MAXLINK) {
			error = XFS_ERROR(EMLINK);
			xfs_rename_unlock4(inodes, XFS_ILOCK_SHARED);
			goto rele_return;
		}
	}

	/*
	 * If we are using project inheritance, we only allow renames
	 * into our tree when the project IDs are the same; else the
	 * tree quota mechanism would be circumvented.
	 */
	if (unlikely((target_dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) &&
		     (target_dp->i_d.di_projid != src_ip->i_d.di_projid))) {
		error = XFS_ERROR(EXDEV);
		xfs_rename_unlock4(inodes, XFS_ILOCK_SHARED);
		goto rele_return;
	}

	new_parent = (src_dp != target_dp);
	src_is_directory = ((src_ip->i_d.di_mode & S_IFMT) == S_IFDIR);

	/*
	 * Drop the locks on our inodes so that we can start the transaction.
	 */
	xfs_rename_unlock4(inodes, XFS_ILOCK_SHARED);

	XFS_BMAP_INIT(&free_list, &first_block);
	tp = xfs_trans_alloc(mp, XFS_TRANS_RENAME);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	spaceres = XFS_RENAME_SPACE_RES(mp, target_namelen);
	error = xfs_trans_reserve(tp, spaceres, XFS_RENAME_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_RENAME_LOG_COUNT);
	if (error == ENOSPC) {
		spaceres = 0;
		error = xfs_trans_reserve(tp, 0, XFS_RENAME_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES, XFS_RENAME_LOG_COUNT);
	}
	if (error) {
		xfs_trans_cancel(tp, 0);
		goto rele_return;
	}

	/*
	 * Attach the dquots to the inodes
	 */
	if ((error = XFS_QM_DQVOPRENAME(mp, inodes))) {
		xfs_trans_cancel(tp, cancel_flags);
		goto rele_return;
	}

	/*
	 * Reacquire the inode locks we dropped above.
	 */
	xfs_lock_inodes(inodes, num_inodes, 0, XFS_ILOCK_EXCL);

	/*
	 * Join all the inodes to the transaction. From this point on,
	 * we can rely on either trans_commit or trans_cancel to unlock
	 * them.  Note that we need to add a vnode reference to the
	 * directories since trans_commit & trans_cancel will decrement
	 * them when they unlock the inodes.  Also, we need to be careful
	 * not to add an inode to the transaction more than once.
	 */
	VN_HOLD(src_dir_vp);
	xfs_trans_ijoin(tp, src_dp, XFS_ILOCK_EXCL);
	if (new_parent) {
		VN_HOLD(target_dir_vp);
		xfs_trans_ijoin(tp, target_dp, XFS_ILOCK_EXCL);
	}
	if ((src_ip != src_dp) && (src_ip != target_dp)) {
		xfs_trans_ijoin(tp, src_ip, XFS_ILOCK_EXCL);
	}
	if ((target_ip != NULL) &&
	    (target_ip != src_ip) &&
	    (target_ip != src_dp) &&
	    (target_ip != target_dp)) {
		xfs_trans_ijoin(tp, target_ip, XFS_ILOCK_EXCL);
	}

	/*
	 * Set up the target.
	 */
	if (target_ip == NULL) {
		/*
		 * If there's no space reservation, check the entry will
		 * fit before actually inserting it.
		 */
		if (spaceres == 0 &&
		    (error = xfs_dir_canenter(tp, target_dp, target_name,
						target_namelen)))
			goto error_return;
		/*
		 * If target does not exist and the rename crosses
		 * directories, adjust the target directory link count
		 * to account for the ".." reference from the new entry.
		 */
		error = xfs_dir_createname(tp, target_dp, target_name,
					   target_namelen, src_ip->i_ino,
					   &first_block, &free_list, spaceres);
		if (error == ENOSPC)
			goto error_return;
		if (error)
			goto abort_return;
		xfs_ichgtime(target_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		if (new_parent && src_is_directory) {
			error = xfs_bumplink(tp, target_dp);
			if (error)
				goto abort_return;
		}
	} else { /* target_ip != NULL */
		/*
		 * If target exists and it's a directory, check that both
		 * target and source are directories and that target can be
		 * destroyed, or that neither is a directory.
		 */
		if ((target_ip->i_d.di_mode & S_IFMT) == S_IFDIR) {
			/*
			 * Make sure target dir is empty.
			 */
			if (!(xfs_dir_isempty(target_ip)) ||
			    (target_ip->i_d.di_nlink > 2)) {
				error = XFS_ERROR(EEXIST);
				goto error_return;
			}
		}

		/*
		 * Link the source inode under the target name.
		 * If the source inode is a directory and we are moving
		 * it across directories, its ".." entry will be
		 * inconsistent until we replace that down below.
		 *
		 * In case there is already an entry with the same
		 * name at the destination directory, remove it first.
		 */
		error = xfs_dir_replace(tp, target_dp, target_name,
					target_namelen, src_ip->i_ino,
					&first_block, &free_list, spaceres);
		if (error)
			goto abort_return;
		xfs_ichgtime(target_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		/*
		 * Decrement the link count on the target since the target
		 * dir no longer points to it.
		 */
		error = xfs_droplink(tp, target_ip);
		if (error)
			goto abort_return;
		target_ip_dropped = 1;

		if (src_is_directory) {
			/*
			 * Drop the link from the old "." entry.
			 */
			error = xfs_droplink(tp, target_ip);
			if (error)
				goto abort_return;
		}

		/* Do this test while we still hold the locks */
		target_link_zero = (target_ip)->i_d.di_nlink==0;

	} /* target_ip != NULL */

	/*
	 * Remove the source.
	 */
	if (new_parent && src_is_directory) {
		/*
		 * Rewrite the ".." entry to point to the new
		 * directory.
		 */
		error = xfs_dir_replace(tp, src_ip, "..", 2, target_dp->i_ino,
					&first_block, &free_list, spaceres);
		ASSERT(error != EEXIST);
		if (error)
			goto abort_return;
		xfs_ichgtime(src_ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	} else {
		/*
		 * We always want to hit the ctime on the source inode.
		 * We do it in the if clause above for the 'new_parent &&
		 * src_is_directory' case, and here we get all the other
		 * cases.  This isn't strictly required by the standards
		 * since the source inode isn't really being changed,
		 * but old unix file systems did it and some incremental
		 * backup programs won't work without it.
		 */
		xfs_ichgtime(src_ip, XFS_ICHGTIME_CHG);
	}

	/*
	 * Adjust the link count on src_dp.  This is necessary when
	 * renaming a directory, either within one parent when
	 * the target existed, or across two parent directories.
	 */
	if (src_is_directory && (new_parent || target_ip != NULL)) {

		/*
		 * Decrement link count on src_directory since the
		 * entry that's moved no longer points to it.
		 */
		error = xfs_droplink(tp, src_dp);
		if (error)
			goto abort_return;
	}

	error = xfs_dir_removename(tp, src_dp, src_name, src_namelen,
			src_ip->i_ino, &first_block, &free_list, spaceres);
	if (error)
		goto abort_return;
	xfs_ichgtime(src_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	/*
	 * Update the generation counts on all the directory inodes
	 * that we're modifying.
	 */
	src_dp->i_gen++;
	xfs_trans_log_inode(tp, src_dp, XFS_ILOG_CORE);

	if (new_parent) {
		target_dp->i_gen++;
		xfs_trans_log_inode(tp, target_dp, XFS_ILOG_CORE);
	}

	/*
	 * If there was a target inode, take an extra reference on
	 * it here so that it doesn't go to xfs_inactive() from
	 * within the commit.
	 */
	if (target_ip != NULL) {
		IHOLD(target_ip);
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * rename transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	/*
	 * Take refs. for vop_link_removed calls below.  No need to worry
	 * about directory refs. because the caller holds them.
	 *
	 * Do holds before the xfs_bmap_finish since it might rele them down
	 * to zero.
	 */

	if (target_ip_dropped)
		IHOLD(target_ip);
	IHOLD(src_ip);

	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		xfs_trans_cancel(tp, (XFS_TRANS_RELEASE_LOG_RES |
				 XFS_TRANS_ABORT));
		if (target_ip != NULL) {
			IRELE(target_ip);
		}
		if (target_ip_dropped) {
			IRELE(target_ip);
		}
		IRELE(src_ip);
		goto std_return;
	}

	/*
	 * trans_commit will unlock src_ip, target_ip & decrement
	 * the vnode references.
	 */
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (target_ip != NULL) {
		xfs_refcache_purge_ip(target_ip);
		IRELE(target_ip);
	}
	/*
	 * Let interposed file systems know about removed links.
	 */
	if (target_ip_dropped)
		IRELE(target_ip);

	IRELE(src_ip);

	/* Fall through to std_return with error = 0 or errno from
	 * xfs_trans_commit	 */
std_return:
	if (DM_EVENT_ENABLED(src_dp, DM_EVENT_POSTRENAME) ||
	    DM_EVENT_ENABLED(target_dp, DM_EVENT_POSTRENAME)) {
		(void) XFS_SEND_NAMESP (mp, DM_EVENT_POSTRENAME,
					src_dir_vp, DM_RIGHT_NULL,
					target_dir_vp, DM_RIGHT_NULL,
					src_name, target_name,
					0, error, 0);
	}
	return error;

 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */
 error_return:
	xfs_bmap_cancel(&free_list);
	xfs_trans_cancel(tp, cancel_flags);
	goto std_return;

 rele_return:
	IRELE(src_ip);
	if (target_ip != NULL) {
		IRELE(target_ip);
	}
	goto std_return;
}
