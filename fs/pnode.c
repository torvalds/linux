/*
 *  linux/fs/pnode.c
 *
 * (C) Copyright IBM Corporation 2005.
 *	Released under GPL v2.
 *	Author : Ram Pai (linuxram@us.ibm.com)
 *
 */
#include <linux/namespace.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include "pnode.h"

/* return the next shared peer mount of @p */
static inline struct vfsmount *next_peer(struct vfsmount *p)
{
	return list_entry(p->mnt_share.next, struct vfsmount, mnt_share);
}

void change_mnt_propagation(struct vfsmount *mnt, int type)
{
	if (type == MS_SHARED) {
		set_mnt_shared(mnt);
	} else {
		list_del_init(&mnt->mnt_share);
		mnt->mnt_flags &= ~MNT_PNODE_MASK;
	}
}

/*
 * get the next mount in the propagation tree.
 * @m: the mount seen last
 * @origin: the original mount from where the tree walk initiated
 */
static struct vfsmount *propagation_next(struct vfsmount *m,
					 struct vfsmount *origin)
{
	m = next_peer(m);
	if (m == origin)
		return NULL;
	return m;
}

/*
 * mount 'source_mnt' under the destination 'dest_mnt' at
 * dentry 'dest_dentry'. And propagate that mount to
 * all the peer and slave mounts of 'dest_mnt'.
 * Link all the new mounts into a propagation tree headed at
 * source_mnt. Also link all the new mounts using ->mnt_list
 * headed at source_mnt's ->mnt_list
 *
 * @dest_mnt: destination mount.
 * @dest_dentry: destination dentry.
 * @source_mnt: source mount.
 * @tree_list : list of heads of trees to be attached.
 */
int propagate_mnt(struct vfsmount *dest_mnt, struct dentry *dest_dentry,
		    struct vfsmount *source_mnt, struct list_head *tree_list)
{
	struct vfsmount *m, *child;
	int ret = 0;
	struct vfsmount *prev_dest_mnt = dest_mnt;
	struct vfsmount *prev_src_mnt  = source_mnt;
	LIST_HEAD(tmp_list);
	LIST_HEAD(umount_list);

	for (m = propagation_next(dest_mnt, dest_mnt); m;
			m = propagation_next(m, dest_mnt)) {
		int type = CL_PROPAGATION;

		if (IS_MNT_NEW(m))
			continue;

		if (IS_MNT_SHARED(m))
			type |= CL_MAKE_SHARED;

		if (!(child = copy_tree(source_mnt, source_mnt->mnt_root,
						type))) {
			ret = -ENOMEM;
			list_splice(tree_list, tmp_list.prev);
			goto out;
		}

		if (is_subdir(dest_dentry, m->mnt_root)) {
			mnt_set_mountpoint(m, dest_dentry, child);
			list_add_tail(&child->mnt_hash, tree_list);
		} else {
			/*
			 * This can happen if the parent mount was bind mounted
			 * on some subdirectory of a shared/slave mount.
			 */
			list_add_tail(&child->mnt_hash, &tmp_list);
		}
		prev_dest_mnt = m;
		prev_src_mnt  = child;
	}
out:
	spin_lock(&vfsmount_lock);
	while (!list_empty(&tmp_list)) {
		child = list_entry(tmp_list.next, struct vfsmount, mnt_hash);
		list_del_init(&child->mnt_hash);
		umount_tree(child, &umount_list);
	}
	spin_unlock(&vfsmount_lock);
	release_mounts(&umount_list);
	return ret;
}
