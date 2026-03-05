// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS kernel inode handling.
 *
 * Copyright (c) 2001-2014 Anton Altaparmakov and Tuxera Inc.
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include <linux/writeback.h>
#include <linux/seq_file.h>

#include "lcnalloc.h"
#include "time.h"
#include "ntfs.h"
#include "index.h"
#include "attrlist.h"
#include "reparse.h"
#include "ea.h"
#include "attrib.h"
#include "iomap.h"
#include "object_id.h"

/*
 * ntfs_test_inode - compare two (possibly fake) inodes for equality
 * @vi:		vfs inode which to test
 * @data:	data which is being tested with
 *
 * Compare the ntfs attribute embedded in the ntfs specific part of the vfs
 * inode @vi for equality with the ntfs attribute @data.
 *
 * If searching for the normal file/directory inode, set @na->type to AT_UNUSED.
 * @na->name and @na->name_len are then ignored.
 *
 * Return 1 if the attributes match and 0 if not.
 *
 * NOTE: This function runs with the inode_hash_lock spin lock held so it is not
 * allowed to sleep.
 */
int ntfs_test_inode(struct inode *vi, void *data)
{
	struct ntfs_attr *na = data;
	struct ntfs_inode *ni = NTFS_I(vi);

	if (vi->i_ino != na->mft_no)
		return 0;

	/* If !NInoAttr(ni), @vi is a normal file or directory inode. */
	if (likely(!NInoAttr(ni))) {
		/* If not looking for a normal inode this is a mismatch. */
		if (unlikely(na->type != AT_UNUSED))
			return 0;
	} else {
		/* A fake inode describing an attribute. */
		if (ni->type != na->type)
			return 0;
		if (ni->name_len != na->name_len)
			return 0;
		if (na->name_len && memcmp(ni->name, na->name,
				na->name_len * sizeof(__le16)))
			return 0;
		if (!ni->ext.base_ntfs_ino)
			return 0;
	}

	/* Match! */
	return 1;
}

/*
 * ntfs_init_locked_inode - initialize an inode
 * @vi:		vfs inode to initialize
 * @data:	data which to initialize @vi to
 *
 * Initialize the vfs inode @vi with the values from the ntfs attribute @data in
 * order to enable ntfs_test_inode() to do its work.
 *
 * If initializing the normal file/directory inode, set @na->type to AT_UNUSED.
 * In that case, @na->name and @na->name_len should be set to NULL and 0,
 * respectively. Although that is not strictly necessary as
 * ntfs_read_locked_inode() will fill them in later.
 *
 * Return 0 on success and error.
 *
 * NOTE: This function runs with the inode->i_lock spin lock held so it is not
 * allowed to sleep. (Hence the GFP_ATOMIC allocation.)
 */
static int ntfs_init_locked_inode(struct inode *vi, void *data)
{
	struct ntfs_attr *na = data;
	struct ntfs_inode *ni = NTFS_I(vi);

	vi->i_ino = (unsigned long)na->mft_no;

	if (na->type == AT_INDEX_ALLOCATION)
		NInoSetMstProtected(ni);
	else
		ni->type = na->type;

	ni->name = na->name;
	ni->name_len = na->name_len;
	ni->folio = NULL;
	atomic_set(&ni->count, 1);

	/* If initializing a normal inode, we are done. */
	if (likely(na->type == AT_UNUSED))
		return 0;

	/* It is a fake inode. */
	NInoSetAttr(ni);

	/*
	 * We have I30 global constant as an optimization as it is the name
	 * in >99.9% of named attributes! The other <0.1% incur a GFP_ATOMIC
	 * allocation but that is ok. And most attributes are unnamed anyway,
	 * thus the fraction of named attributes with name != I30 is actually
	 * absolutely tiny.
	 */
	if (na->name_len && na->name != I30) {
		unsigned int i;

		i = na->name_len * sizeof(__le16);
		ni->name = kmalloc(i + sizeof(__le16), GFP_ATOMIC);
		if (!ni->name)
			return -ENOMEM;
		memcpy(ni->name, na->name, i);
		ni->name[na->name_len] = 0;
	}
	return 0;
}

static int ntfs_read_locked_inode(struct inode *vi);
static int ntfs_read_locked_attr_inode(struct inode *base_vi, struct inode *vi);
static int ntfs_read_locked_index_inode(struct inode *base_vi,
		struct inode *vi);

/*
 * ntfs_iget - obtain a struct inode corresponding to a specific normal inode
 * @sb:		super block of mounted volume
 * @mft_no:	mft record number / inode number to obtain
 *
 * Obtain the struct inode corresponding to a specific normal inode (i.e. a
 * file or directory).
 *
 * If the inode is in the cache, it is just returned with an increased
 * reference count. Otherwise, a new struct inode is allocated and initialized,
 * and finally ntfs_read_locked_inode() is called to read in the inode and
 * fill in the remainder of the inode structure.
 *
 * Return the struct inode on success. Check the return value with IS_ERR() and
 * if true, the function failed and the error code is obtained from PTR_ERR().
 */
struct inode *ntfs_iget(struct super_block *sb, u64 mft_no)
{
	struct inode *vi;
	int err;
	struct ntfs_attr na;

	na.mft_no = mft_no;
	na.type = AT_UNUSED;
	na.name = NULL;
	na.name_len = 0;

	vi = iget5_locked(sb, mft_no, ntfs_test_inode,
			ntfs_init_locked_inode, &na);
	if (unlikely(!vi))
		return ERR_PTR(-ENOMEM);

	err = 0;

	/* If this is a freshly allocated inode, need to read it now. */
	if (inode_state_read_once(vi) & I_NEW) {
		err = ntfs_read_locked_inode(vi);
		unlock_new_inode(vi);
	}
	/*
	 * There is no point in keeping bad inodes around if the failure was
	 * due to ENOMEM. We want to be able to retry again later.
	 */
	if (unlikely(err == -ENOMEM)) {
		iput(vi);
		vi = ERR_PTR(err);
	}
	return vi;
}

/*
 * ntfs_attr_iget - obtain a struct inode corresponding to an attribute
 * @base_vi:	vfs base inode containing the attribute
 * @type:	attribute type
 * @name:	Unicode name of the attribute (NULL if unnamed)
 * @name_len:	length of @name in Unicode characters (0 if unnamed)
 *
 * Obtain the (fake) struct inode corresponding to the attribute specified by
 * @type, @name, and @name_len, which is present in the base mft record
 * specified by the vfs inode @base_vi.
 *
 * If the attribute inode is in the cache, it is just returned with an
 * increased reference count. Otherwise, a new struct inode is allocated and
 * initialized, and finally ntfs_read_locked_attr_inode() is called to read the
 * attribute and fill in the inode structure.
 *
 * Note, for index allocation attributes, you need to use ntfs_index_iget()
 * instead of ntfs_attr_iget() as working with indices is a lot more complex.
 *
 * Return the struct inode of the attribute inode on success. Check the return
 * value with IS_ERR() and if true, the function failed and the error code is
 * obtained from PTR_ERR().
 */
struct inode *ntfs_attr_iget(struct inode *base_vi, __le32 type,
		__le16 *name, u32 name_len)
{
	struct inode *vi;
	int err;
	struct ntfs_attr na;

	/* Make sure no one calls ntfs_attr_iget() for indices. */
	WARN_ON(type == AT_INDEX_ALLOCATION);

	na.mft_no = base_vi->i_ino;
	na.type = type;
	na.name = name;
	na.name_len = name_len;

	vi = iget5_locked(base_vi->i_sb, na.mft_no, ntfs_test_inode,
			ntfs_init_locked_inode, &na);
	if (unlikely(!vi))
		return ERR_PTR(-ENOMEM);
	err = 0;

	/* If this is a freshly allocated inode, need to read it now. */
	if (inode_state_read_once(vi) & I_NEW) {
		err = ntfs_read_locked_attr_inode(base_vi, vi);
		unlock_new_inode(vi);
	}
	/*
	 * There is no point in keeping bad attribute inodes around. This also
	 * simplifies things in that we never need to check for bad attribute
	 * inodes elsewhere.
	 */
	if (unlikely(err)) {
		iput(vi);
		vi = ERR_PTR(err);
	}
	return vi;
}

/*
 * ntfs_index_iget - obtain a struct inode corresponding to an index
 * @base_vi:	vfs base inode containing the index related attributes
 * @name:	Unicode name of the index
 * @name_len:	length of @name in Unicode characters
 *
 * Obtain the (fake) struct inode corresponding to the index specified by @name
 * and @name_len, which is present in the base mft record specified by the vfs
 * inode @base_vi.
 *
 * If the index inode is in the cache, it is just returned with an increased
 * reference count.  Otherwise, a new struct inode is allocated and
 * initialized, and finally ntfs_read_locked_index_inode() is called to read
 * the index related attributes and fill in the inode structure.
 *
 * Return the struct inode of the index inode on success. Check the return
 * value with IS_ERR() and if true, the function failed and the error code is
 * obtained from PTR_ERR().
 */
struct inode *ntfs_index_iget(struct inode *base_vi, __le16 *name,
		u32 name_len)
{
	struct inode *vi;
	int err;
	struct ntfs_attr na;

	na.mft_no = base_vi->i_ino;
	na.type = AT_INDEX_ALLOCATION;
	na.name = name;
	na.name_len = name_len;

	vi = iget5_locked(base_vi->i_sb, na.mft_no, ntfs_test_inode,
			ntfs_init_locked_inode, &na);
	if (unlikely(!vi))
		return ERR_PTR(-ENOMEM);

	err = 0;

	/* If this is a freshly allocated inode, need to read it now. */
	if (inode_state_read_once(vi) & I_NEW) {
		err = ntfs_read_locked_index_inode(base_vi, vi);
		unlock_new_inode(vi);
	}
	/*
	 * There is no point in keeping bad index inodes around.  This also
	 * simplifies things in that we never need to check for bad index
	 * inodes elsewhere.
	 */
	if (unlikely(err)) {
		iput(vi);
		vi = ERR_PTR(err);
	}
	return vi;
}

struct inode *ntfs_alloc_big_inode(struct super_block *sb)
{
	struct ntfs_inode *ni;

	ntfs_debug("Entering.");
	ni = alloc_inode_sb(sb, ntfs_big_inode_cache, GFP_NOFS);
	if (likely(ni != NULL)) {
		ni->state = 0;
		ni->type = 0;
		ni->mft_no = 0;
		return VFS_I(ni);
	}
	ntfs_error(sb, "Allocation of NTFS big inode structure failed.");
	return NULL;
}

void ntfs_free_big_inode(struct inode *inode)
{
	kmem_cache_free(ntfs_big_inode_cache, NTFS_I(inode));
}

static int ntfs_non_resident_dealloc_clusters(struct ntfs_inode *ni)
{
	struct super_block *sb = ni->vol->sb;
	struct ntfs_attr_search_ctx *actx;
	int err = 0;

	actx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!actx)
		return -ENOMEM;
	WARN_ON(actx->mrec->link_count != 0);

	/**
	 * ntfs_truncate_vfs cannot be called in evict() context due
	 * to some limitations, which are the @ni vfs inode is marked
	 * with I_FREEING, and etc.
	 */
	if (NInoRunlistDirty(ni)) {
		err = ntfs_cluster_free_from_rl(ni->vol, ni->runlist.rl);
		if (err)
			ntfs_error(sb,
					"Failed to free clusters. Leaving inconsistent metadata.\n");
	}

	while ((err = ntfs_attrs_walk(actx)) == 0) {
		if (actx->attr->non_resident &&
				(!NInoRunlistDirty(ni) || actx->attr->type != AT_DATA)) {
			struct runlist_element *rl;
			size_t new_rl_count;

			rl = ntfs_mapping_pairs_decompress(ni->vol, actx->attr, NULL,
					&new_rl_count);
			if (IS_ERR(rl)) {
				err = PTR_ERR(rl);
				ntfs_error(sb,
					   "Failed to decompress runlist. Leaving inconsistent metadata.\n");
				continue;
			}

			err = ntfs_cluster_free_from_rl(ni->vol, rl);
			if (err)
				ntfs_error(sb,
					   "Failed to free attribute clusters. Leaving inconsistent metadata.\n");
			kvfree(rl);
		}
	}

	ntfs_release_dirty_clusters(ni->vol, ni->i_dealloc_clusters);
	ntfs_attr_put_search_ctx(actx);
	return err;
}

int ntfs_drop_big_inode(struct inode *inode)
{
	struct ntfs_inode *ni = NTFS_I(inode);

	if (!inode_unhashed(inode) && inode_state_read_once(inode) & I_SYNC) {
		if (ni->type == AT_DATA || ni->type == AT_INDEX_ALLOCATION) {
			if (!inode->i_nlink) {
				struct ntfs_inode *ni = NTFS_I(inode);

				if (ni->data_size == 0)
					return 0;

				/* To avoid evict_inode call simultaneously */
				atomic_inc(&inode->i_count);
				spin_unlock(&inode->i_lock);

				truncate_setsize(VFS_I(ni), 0);
				ntfs_truncate_vfs(VFS_I(ni), 0, 1);

				sb_start_intwrite(inode->i_sb);
				i_size_write(inode, 0);
				ni->allocated_size = ni->initialized_size = ni->data_size = 0;

				truncate_inode_pages_final(inode->i_mapping);
				sb_end_intwrite(inode->i_sb);

				spin_lock(&inode->i_lock);
				atomic_dec(&inode->i_count);
			}
		}
		return 0;
	}

	return inode_generic_drop(inode);
}

static inline struct ntfs_inode *ntfs_alloc_extent_inode(void)
{
	struct ntfs_inode *ni;

	ntfs_debug("Entering.");
	ni = kmem_cache_alloc(ntfs_inode_cache, GFP_NOFS);
	if (likely(ni != NULL)) {
		ni->state = 0;
		return ni;
	}
	ntfs_error(NULL, "Allocation of NTFS inode structure failed.");
	return NULL;
}

static void ntfs_destroy_extent_inode(struct ntfs_inode *ni)
{
	ntfs_debug("Entering.");

	if (!atomic_dec_and_test(&ni->count))
		WARN_ON(1);
	if (ni->folio)
		folio_put(ni->folio);
	kfree(ni->mrec);
	kmem_cache_free(ntfs_inode_cache, ni);
}

static struct lock_class_key attr_inode_mrec_lock_class;
static struct lock_class_key attr_list_inode_mrec_lock_class;

/*
 * The attribute runlist lock has separate locking rules from the
 * normal runlist lock, so split the two lock-classes:
 */
static struct lock_class_key attr_list_rl_lock_class;

/*
 * __ntfs_init_inode - initialize ntfs specific part of an inode
 * @sb:		super block of mounted volume
 * @ni:		freshly allocated ntfs inode which to initialize
 *
 * Initialize an ntfs inode to defaults.
 *
 * NOTE: ni->mft_no, ni->state, ni->type, ni->name, and ni->name_len are left
 * untouched. Make sure to initialize them elsewhere.
 */
void __ntfs_init_inode(struct super_block *sb, struct ntfs_inode *ni)
{
	ntfs_debug("Entering.");
	rwlock_init(&ni->size_lock);
	ni->initialized_size = ni->allocated_size = 0;
	ni->seq_no = 0;
	atomic_set(&ni->count, 1);
	ni->vol = NTFS_SB(sb);
	ntfs_init_runlist(&ni->runlist);
	mutex_init(&ni->mrec_lock);
	if (ni->type == AT_ATTRIBUTE_LIST) {
		lockdep_set_class(&ni->mrec_lock,
				  &attr_list_inode_mrec_lock_class);
		lockdep_set_class(&ni->runlist.lock,
				  &attr_list_rl_lock_class);
	} else if (NInoAttr(ni)) {
		lockdep_set_class(&ni->mrec_lock,
				  &attr_inode_mrec_lock_class);
	}

	ni->folio = NULL;
	ni->folio_ofs = 0;
	ni->mrec = NULL;
	ni->attr_list_size = 0;
	ni->attr_list = NULL;
	ni->itype.index.block_size = 0;
	ni->itype.index.vcn_size = 0;
	ni->itype.index.collation_rule = 0;
	ni->itype.index.block_size_bits = 0;
	ni->itype.index.vcn_size_bits = 0;
	mutex_init(&ni->extent_lock);
	ni->nr_extents = 0;
	ni->ext.base_ntfs_ino = NULL;
	ni->flags = 0;
	ni->mft_lcn[0] = LCN_RL_NOT_MAPPED;
	ni->mft_lcn_count = 0;
	ni->target = NULL;
	ni->i_dealloc_clusters = 0;
}

/*
 * Extent inodes get MFT-mapped in a nested way, while the base inode
 * is still mapped. Teach this nesting to the lock validator by creating
 * a separate class for nested inode's mrec_lock's:
 */
static struct lock_class_key extent_inode_mrec_lock_key;

inline struct ntfs_inode *ntfs_new_extent_inode(struct super_block *sb,
		u64 mft_no)
{
	struct ntfs_inode *ni = ntfs_alloc_extent_inode();

	ntfs_debug("Entering.");
	if (likely(ni != NULL)) {
		__ntfs_init_inode(sb, ni);
		lockdep_set_class(&ni->mrec_lock, &extent_inode_mrec_lock_key);
		ni->mft_no = mft_no;
		ni->type = AT_UNUSED;
		ni->name = NULL;
		ni->name_len = 0;
	}
	return ni;
}

/*
 * ntfs_is_extended_system_file - check if a file is in the $Extend directory
 * @ctx:	initialized attribute search context
 *
 * Search all file name attributes in the inode described by the attribute
 * search context @ctx and check if any of the names are in the $Extend system
 * directory.
 *
 * Return values:
 *	   3: file is $ObjId in $Extend directory
 *	   2: file is $Reparse in $Extend directory
 *	   1: file is in $Extend directory
 *	   0: file is not in $Extend directory
 *    -errno: failed to determine if the file is in the $Extend directory
 */
static int ntfs_is_extended_system_file(struct ntfs_attr_search_ctx *ctx)
{
	int nr_links, err;

	/* Restart search. */
	ntfs_attr_reinit_search_ctx(ctx);

	/* Get number of hard links. */
	nr_links = le16_to_cpu(ctx->mrec->link_count);

	/* Loop through all hard links. */
	while (!(err = ntfs_attr_lookup(AT_FILE_NAME, NULL, 0, 0, 0, NULL, 0,
			ctx))) {
		struct file_name_attr *file_name_attr;
		struct attr_record *attr = ctx->attr;
		u8 *p, *p2;

		nr_links--;
		/*
		 * Maximum sanity checking as we are called on an inode that
		 * we suspect might be corrupt.
		 */
		p = (u8 *)attr + le32_to_cpu(attr->length);
		if (p < (u8 *)ctx->mrec || (u8 *)p > (u8 *)ctx->mrec +
				le32_to_cpu(ctx->mrec->bytes_in_use)) {
err_corrupt_attr:
			ntfs_error(ctx->ntfs_ino->vol->sb,
					"Corrupt file name attribute. You should run chkdsk.");
			return -EIO;
		}
		if (attr->non_resident) {
			ntfs_error(ctx->ntfs_ino->vol->sb,
					"Non-resident file name. You should run chkdsk.");
			return -EIO;
		}
		if (attr->flags) {
			ntfs_error(ctx->ntfs_ino->vol->sb,
					"File name with invalid flags. You should run chkdsk.");
			return -EIO;
		}
		if (!(attr->data.resident.flags & RESIDENT_ATTR_IS_INDEXED)) {
			ntfs_error(ctx->ntfs_ino->vol->sb,
					"Unindexed file name. You should run chkdsk.");
			return -EIO;
		}
		file_name_attr = (struct file_name_attr *)((u8 *)attr +
				le16_to_cpu(attr->data.resident.value_offset));
		p2 = (u8 *)file_name_attr + le32_to_cpu(attr->data.resident.value_length);
		if (p2 < (u8 *)attr || p2 > p)
			goto err_corrupt_attr;
		/* This attribute is ok, but is it in the $Extend directory? */
		if (MREF_LE(file_name_attr->parent_directory) == FILE_Extend) {
			unsigned char *s;

			s = ntfs_attr_name_get(ctx->ntfs_ino->vol,
					file_name_attr->file_name,
					file_name_attr->file_name_length);
			if (!s)
				return 1;
			if (!strcmp("$Reparse", s)) {
				ntfs_attr_name_free(&s);
				return 2; /* it's reparse point file */
			}
			if (!strcmp("$ObjId", s)) {
				ntfs_attr_name_free(&s);
				return 3; /* it's object id file */
			}
			ntfs_attr_name_free(&s);
			return 1;	/* YES, it's an extended system file. */
		}
	}
	if (unlikely(err != -ENOENT))
		return err;
	if (unlikely(nr_links)) {
		ntfs_error(ctx->ntfs_ino->vol->sb,
			"Inode hard link count doesn't match number of name attributes. You should run chkdsk.");
		return -EIO;
	}
	return 0;	/* NO, it is not an extended system file. */
}

static struct lock_class_key ntfs_dir_inval_lock_key;

void ntfs_set_vfs_operations(struct inode *inode, mode_t mode, dev_t dev)
{
	if (S_ISDIR(mode)) {
		if (!NInoAttr(NTFS_I(inode))) {
			inode->i_op = &ntfs_dir_inode_ops;
			inode->i_fop = &ntfs_dir_ops;
		}
		inode->i_mapping->a_ops = &ntfs_aops;
		lockdep_set_class(&inode->i_mapping->invalidate_lock,
				  &ntfs_dir_inval_lock_key);
	} else if (S_ISLNK(mode)) {
		inode->i_op = &ntfs_symlink_inode_operations;
		inode->i_mapping->a_ops = &ntfs_aops;
	} else if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) || S_ISSOCK(mode)) {
		inode->i_op = &ntfs_special_inode_operations;
		init_special_inode(inode, inode->i_mode, dev);
	} else {
		if (!NInoAttr(NTFS_I(inode))) {
			inode->i_op = &ntfs_file_inode_ops;
			inode->i_fop = &ntfs_file_ops;
		}
		if (inode->i_ino == FILE_MFT)
			inode->i_mapping->a_ops = &ntfs_mft_aops;
		else
			inode->i_mapping->a_ops = &ntfs_aops;
	}
}

/*
 * ntfs_read_locked_inode - read an inode from its device
 * @vi:		inode to read
 *
 * ntfs_read_locked_inode() is called from ntfs_iget() to read the inode
 * described by @vi into memory from the device.
 *
 * The only fields in @vi that we need to/can look at when the function is
 * called are i_sb, pointing to the mounted device's super block, and i_ino,
 * the number of the inode to load.
 *
 * ntfs_read_locked_inode() maps, pins and locks the mft record number i_ino
 * for reading and sets up the necessary @vi fields as well as initializing
 * the ntfs inode.
 *
 * Q: What locks are held when the function is called?
 * A: i_state has I_NEW set, hence the inode is locked, also
 *    i_count is set to 1, so it is not going to go away
 *    i_flags is set to 0 and we have no business touching it.  Only an ioctl()
 *    is allowed to write to them. We should of course be honouring them but
 *    we need to do that using the IS_* macros defined in include/linux/fs.h.
 *    In any case ntfs_read_locked_inode() has nothing to do with i_flags.
 *
 * Return 0 on success and -errno on error.
 */
static int ntfs_read_locked_inode(struct inode *vi)
{
	struct ntfs_volume *vol = NTFS_SB(vi->i_sb);
	struct ntfs_inode *ni = NTFS_I(vi);
	struct mft_record *m;
	struct attr_record *a;
	struct standard_information *si;
	struct ntfs_attr_search_ctx *ctx;
	int err = 0;
	__le16 *name = I30;
	unsigned int name_len = 4, flags = 0;
	int extend_sys = 0;
	dev_t dev = 0;
	bool vol_err = true;

	ntfs_debug("Entering for i_ino 0x%llx.", ni->mft_no);

	if (uid_valid(vol->uid)) {
		vi->i_uid = vol->uid;
		flags |= NTFS_VOL_UID;
	} else
		vi->i_uid = GLOBAL_ROOT_UID;

	if (gid_valid(vol->gid)) {
		vi->i_gid = vol->gid;
		flags |= NTFS_VOL_GID;
	} else
		vi->i_gid = GLOBAL_ROOT_GID;

	vi->i_mode = 0777;

	/*
	 * Initialize the ntfs specific part of @vi special casing
	 * FILE_MFT which we need to do at mount time.
	 */
	if (vi->i_ino != FILE_MFT)
		ntfs_init_big_inode(vi);

	m = map_mft_record(ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_out;
	}

	ctx = ntfs_attr_get_search_ctx(ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto unm_err_out;
	}

	if (!(m->flags & MFT_RECORD_IN_USE)) {
		err = -ENOENT;
		vol_err = false;
		goto unm_err_out;
	}

	if (m->base_mft_record) {
		ntfs_error(vi->i_sb, "Inode is an extent inode!");
		goto unm_err_out;
	}

	/* Transfer information from mft record into vfs and ntfs inodes. */
	vi->i_generation = ni->seq_no = le16_to_cpu(m->sequence_number);

	if (le16_to_cpu(m->link_count) < 1) {
		ntfs_error(vi->i_sb, "Inode link count is 0!");
		goto unm_err_out;
	}
	set_nlink(vi, le16_to_cpu(m->link_count));

	/* If read-only, no one gets write permissions. */
	if (IS_RDONLY(vi))
		vi->i_mode &= ~0222;

	/*
	 * Find the standard information attribute in the mft record. At this
	 * stage we haven't setup the attribute list stuff yet, so this could
	 * in fact fail if the standard information is in an extent record, but
	 * I don't think this actually ever happens.
	 */
	ntfs_attr_reinit_search_ctx(ctx);
	err = ntfs_attr_lookup(AT_STANDARD_INFORMATION, NULL, 0, 0, 0, NULL, 0,
			ctx);
	if (unlikely(err)) {
		if (err == -ENOENT)
			ntfs_error(vi->i_sb, "$STANDARD_INFORMATION attribute is missing.");
		goto unm_err_out;
	}
	a = ctx->attr;
	/* Get the standard information attribute value. */
	if ((u8 *)a + le16_to_cpu(a->data.resident.value_offset)
			+ le32_to_cpu(a->data.resident.value_length) >
			(u8 *)ctx->mrec + vol->mft_record_size) {
		ntfs_error(vi->i_sb, "Corrupt standard information attribute in inode.");
		goto unm_err_out;
	}
	si = (struct standard_information *)((u8 *)a +
			le16_to_cpu(a->data.resident.value_offset));

	/* Transfer information from the standard information into vi. */
	/*
	 * Note: The i_?times do not quite map perfectly onto the NTFS times,
	 * but they are close enough, and in the end it doesn't really matter
	 * that much...
	 */
	/*
	 * mtime is the last change of the data within the file. Not changed
	 * when only metadata is changed, e.g. a rename doesn't affect mtime.
	 */
	ni->i_crtime = ntfs2utc(si->creation_time);

	inode_set_mtime_to_ts(vi, ntfs2utc(si->last_data_change_time));
	/*
	 * ctime is the last change of the metadata of the file. This obviously
	 * always changes, when mtime is changed. ctime can be changed on its
	 * own, mtime is then not changed, e.g. when a file is renamed.
	 */
	inode_set_ctime_to_ts(vi, ntfs2utc(si->last_mft_change_time));
	/*
	 * Last access to the data within the file. Not changed during a rename
	 * for example but changed whenever the file is written to.
	 */
	inode_set_atime_to_ts(vi, ntfs2utc(si->last_access_time));
	ni->flags = si->file_attributes;

	/* Find the attribute list attribute if present. */
	ntfs_attr_reinit_search_ctx(ctx);
	err = ntfs_attr_lookup(AT_ATTRIBUTE_LIST, NULL, 0, 0, 0, NULL, 0, ctx);
	if (err) {
		if (unlikely(err != -ENOENT)) {
			ntfs_error(vi->i_sb, "Failed to lookup attribute list attribute.");
			goto unm_err_out;
		}
	} else {
		if (vi->i_ino == FILE_MFT)
			goto skip_attr_list_load;
		ntfs_debug("Attribute list found in inode 0x%llx.", ni->mft_no);
		NInoSetAttrList(ni);
		a = ctx->attr;
		if (a->flags & ATTR_COMPRESSION_MASK) {
			ntfs_error(vi->i_sb,
				"Attribute list attribute is compressed.");
			goto unm_err_out;
		}
		if (a->flags & ATTR_IS_ENCRYPTED ||
				a->flags & ATTR_IS_SPARSE) {
			if (a->non_resident) {
				ntfs_error(vi->i_sb,
					"Non-resident attribute list attribute is encrypted/sparse.");
				goto unm_err_out;
			}
			ntfs_warning(vi->i_sb,
				"Resident attribute list attribute in inode 0x%llx is marked encrypted/sparse which is not true.  However, Windows allows this and chkdsk does not detect or correct it so we will just ignore the invalid flags and pretend they are not set.",
				ni->mft_no);
		}
		/* Now allocate memory for the attribute list. */
		ni->attr_list_size = (u32)ntfs_attr_size(a);
		if (!ni->attr_list_size) {
			ntfs_error(vi->i_sb, "Attr_list_size is zero");
			goto unm_err_out;
		}
		ni->attr_list = kvzalloc(ni->attr_list_size, GFP_NOFS);
		if (!ni->attr_list) {
			ntfs_error(vi->i_sb,
				"Not enough memory to allocate buffer for attribute list.");
			err = -ENOMEM;
			goto unm_err_out;
		}
		if (a->non_resident) {
			NInoSetAttrListNonResident(ni);
			if (a->data.non_resident.lowest_vcn) {
				ntfs_error(vi->i_sb, "Attribute list has non zero lowest_vcn.");
				goto unm_err_out;
			}

			/* Now load the attribute list. */
			err = load_attribute_list(ni, ni->attr_list, ni->attr_list_size);
			if (err) {
				ntfs_error(vi->i_sb, "Failed to load attribute list attribute.");
				goto unm_err_out;
			}
		} else /* if (!a->non_resident) */ {
			if ((u8 *)a + le16_to_cpu(a->data.resident.value_offset)
					+ le32_to_cpu(
					a->data.resident.value_length) >
					(u8 *)ctx->mrec + vol->mft_record_size) {
				ntfs_error(vi->i_sb, "Corrupt attribute list in inode.");
				goto unm_err_out;
			}
			/* Now copy the attribute list. */
			memcpy(ni->attr_list, (u8 *)a + le16_to_cpu(
					a->data.resident.value_offset),
					le32_to_cpu(
					a->data.resident.value_length));
		}
	}
skip_attr_list_load:
	err = ntfs_attr_lookup(AT_EA_INFORMATION, NULL, 0, 0, 0, NULL, 0, ctx);
	if (!err)
		NInoSetHasEA(ni);

	ntfs_ea_get_wsl_inode(vi, &dev, flags);

	if (m->flags & MFT_RECORD_IS_DIRECTORY) {
		vi->i_mode |= S_IFDIR;
		/*
		 * Apply the directory permissions mask set in the mount
		 * options.
		 */
		vi->i_mode &= ~vol->dmask;
		/* Things break without this kludge! */
		if (vi->i_nlink > 1)
			set_nlink(vi, 1);
	} else {
		if (ni->flags & FILE_ATTR_REPARSE_POINT) {
			unsigned int mode;

			mode = ntfs_make_symlink(ni);
			if (mode)
				vi->i_mode |= mode;
			else {
				vi->i_mode &= ~S_IFLNK;
				vi->i_mode |= S_IFREG;
			}
		} else
			vi->i_mode |= S_IFREG;
		/* Apply the file permissions mask set in the mount options. */
		vi->i_mode &= ~vol->fmask;
	}

	/*
	 * If an attribute list is present we now have the attribute list value
	 * in ntfs_ino->attr_list and it is ntfs_ino->attr_list_size bytes.
	 */
	if (S_ISDIR(vi->i_mode)) {
		struct index_root *ir;
		u8 *ir_end, *index_end;

view_index_meta:
		/* It is a directory, find index root attribute. */
		ntfs_attr_reinit_search_ctx(ctx);
		err = ntfs_attr_lookup(AT_INDEX_ROOT, name, name_len, CASE_SENSITIVE,
				0, NULL, 0, ctx);
		if (unlikely(err)) {
			if (err == -ENOENT)
				ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is missing.");
			goto unm_err_out;
		}
		a = ctx->attr;
		/* Set up the state. */
		if (unlikely(a->non_resident)) {
			ntfs_error(vol->sb,
				"$INDEX_ROOT attribute is not resident.");
			goto unm_err_out;
		}
		/* Ensure the attribute name is placed before the value. */
		if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
				le16_to_cpu(a->data.resident.value_offset)))) {
			ntfs_error(vol->sb,
				"$INDEX_ROOT attribute name is placed after the attribute value.");
			goto unm_err_out;
		}
		/*
		 * Compressed/encrypted index root just means that the newly
		 * created files in that directory should be created compressed/
		 * encrypted. However index root cannot be both compressed and
		 * encrypted.
		 */
		if (a->flags & ATTR_COMPRESSION_MASK) {
			NInoSetCompressed(ni);
			ni->flags |= FILE_ATTR_COMPRESSED;
		}
		if (a->flags & ATTR_IS_ENCRYPTED) {
			if (a->flags & ATTR_COMPRESSION_MASK) {
				ntfs_error(vi->i_sb, "Found encrypted and compressed attribute.");
				goto unm_err_out;
			}
			NInoSetEncrypted(ni);
			ni->flags |= FILE_ATTR_ENCRYPTED;
		}
		if (a->flags & ATTR_IS_SPARSE) {
			NInoSetSparse(ni);
			ni->flags |= FILE_ATTR_SPARSE_FILE;
		}
		ir = (struct index_root *)((u8 *)a +
				le16_to_cpu(a->data.resident.value_offset));
		ir_end = (u8 *)ir + le32_to_cpu(a->data.resident.value_length);
		if (ir_end > (u8 *)ctx->mrec + vol->mft_record_size) {
			ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is corrupt.");
			goto unm_err_out;
		}
		index_end = (u8 *)&ir->index +
				le32_to_cpu(ir->index.index_length);
		if (index_end > ir_end) {
			ntfs_error(vi->i_sb, "Directory index is corrupt.");
			goto unm_err_out;
		}

		if (extend_sys) {
			if (ir->type) {
				ntfs_error(vi->i_sb, "Indexed attribute is not zero.");
				goto unm_err_out;
			}
		} else {
			if (ir->type != AT_FILE_NAME) {
				ntfs_error(vi->i_sb, "Indexed attribute is not $FILE_NAME.");
				goto unm_err_out;
			}

			if (ir->collation_rule != COLLATION_FILE_NAME) {
				ntfs_error(vi->i_sb,
					"Index collation rule is not COLLATION_FILE_NAME.");
				goto unm_err_out;
			}
		}

		ni->itype.index.collation_rule = ir->collation_rule;
		ni->itype.index.block_size = le32_to_cpu(ir->index_block_size);
		if (ni->itype.index.block_size &
				(ni->itype.index.block_size - 1)) {
			ntfs_error(vi->i_sb, "Index block size (%u) is not a power of two.",
					ni->itype.index.block_size);
			goto unm_err_out;
		}
		if (ni->itype.index.block_size > PAGE_SIZE) {
			ntfs_error(vi->i_sb,
				"Index block size (%u) > PAGE_SIZE (%ld) is not supported.",
				ni->itype.index.block_size,
				PAGE_SIZE);
			err = -EOPNOTSUPP;
			goto unm_err_out;
		}
		if (ni->itype.index.block_size < NTFS_BLOCK_SIZE) {
			ntfs_error(vi->i_sb,
				"Index block size (%u) < NTFS_BLOCK_SIZE (%i) is not supported.",
				ni->itype.index.block_size,
				NTFS_BLOCK_SIZE);
			err = -EOPNOTSUPP;
			goto unm_err_out;
		}
		ni->itype.index.block_size_bits =
				ffs(ni->itype.index.block_size) - 1;
		/* Determine the size of a vcn in the directory index. */
		if (vol->cluster_size <= ni->itype.index.block_size) {
			ni->itype.index.vcn_size = vol->cluster_size;
			ni->itype.index.vcn_size_bits = vol->cluster_size_bits;
		} else {
			ni->itype.index.vcn_size = vol->sector_size;
			ni->itype.index.vcn_size_bits = vol->sector_size_bits;
		}

		/* Setup the index allocation attribute, even if not present. */
		ni->type = AT_INDEX_ROOT;
		ni->name = name;
		ni->name_len = name_len;
		vi->i_size = ni->initialized_size = ni->data_size =
			le32_to_cpu(a->data.resident.value_length);
		ni->allocated_size = (ni->data_size + 7) & ~7;
		/* We are done with the mft record, so we release it. */
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(ni);
		m = NULL;
		ctx = NULL;
		/* Setup the operations for this inode. */
		ntfs_set_vfs_operations(vi, S_IFDIR, 0);
		if (ir->index.flags & LARGE_INDEX)
			NInoSetIndexAllocPresent(ni);
	} else {
		/* It is a file. */
		ntfs_attr_reinit_search_ctx(ctx);

		/* Setup the data attribute, even if not present. */
		ni->type = AT_DATA;
		ni->name = AT_UNNAMED;
		ni->name_len = 0;

		/* Find first extent of the unnamed data attribute. */
		err = ntfs_attr_lookup(AT_DATA, NULL, 0, 0, 0, NULL, 0, ctx);
		if (unlikely(err)) {
			vi->i_size = ni->initialized_size =
					ni->allocated_size = 0;
			if (err != -ENOENT) {
				ntfs_error(vi->i_sb, "Failed to lookup $DATA attribute.");
				goto unm_err_out;
			}
			/*
			 * FILE_Secure does not have an unnamed $DATA
			 * attribute, so we special case it here.
			 */
			if (vi->i_ino == FILE_Secure)
				goto no_data_attr_special_case;
			/*
			 * Most if not all the system files in the $Extend
			 * system directory do not have unnamed data
			 * attributes so we need to check if the parent
			 * directory of the file is FILE_Extend and if it is
			 * ignore this error. To do this we need to get the
			 * name of this inode from the mft record as the name
			 * contains the back reference to the parent directory.
			 */
			extend_sys = ntfs_is_extended_system_file(ctx);
			if (extend_sys > 0) {
				if (m->flags & MFT_RECORD_IS_VIEW_INDEX) {
					if (extend_sys == 2) {
						name = reparse_index_name;
						name_len = 2;
						goto view_index_meta;
					} else if (extend_sys == 3) {
						name = objid_index_name;
						name_len = 2;
						goto view_index_meta;
					}
				}
				goto no_data_attr_special_case;
			}

			err = extend_sys;
			ntfs_error(vi->i_sb, "$DATA attribute is missing, err : %d", err);
			goto unm_err_out;
		}
		a = ctx->attr;
		/* Setup the state. */
		if (a->flags & (ATTR_COMPRESSION_MASK | ATTR_IS_SPARSE)) {
			if (a->flags & ATTR_COMPRESSION_MASK) {
				NInoSetCompressed(ni);
				ni->flags |= FILE_ATTR_COMPRESSED;
				if (vol->cluster_size > 4096) {
					ntfs_error(vi->i_sb,
						"Found compressed data but compression is disabled due to cluster size (%i) > 4kiB.",
						vol->cluster_size);
					goto unm_err_out;
				}
				if ((a->flags & ATTR_COMPRESSION_MASK)
						!= ATTR_IS_COMPRESSED) {
					ntfs_error(vi->i_sb,
						"Found unknown compression method or corrupt file.");
					goto unm_err_out;
				}
			}
			if (a->flags & ATTR_IS_SPARSE) {
				NInoSetSparse(ni);
				ni->flags |= FILE_ATTR_SPARSE_FILE;
			}
		}
		if (a->flags & ATTR_IS_ENCRYPTED) {
			if (NInoCompressed(ni)) {
				ntfs_error(vi->i_sb, "Found encrypted and compressed data.");
				goto unm_err_out;
			}
			NInoSetEncrypted(ni);
			ni->flags |= FILE_ATTR_ENCRYPTED;
		}
		if (a->non_resident) {
			NInoSetNonResident(ni);
			if (NInoCompressed(ni) || NInoSparse(ni)) {
				if (NInoCompressed(ni) &&
				    a->data.non_resident.compression_unit != 4) {
					ntfs_error(vi->i_sb,
						"Found non-standard compression unit (%u instead of 4).  Cannot handle this.",
						a->data.non_resident.compression_unit);
					err = -EOPNOTSUPP;
					goto unm_err_out;
				}

				if (NInoSparse(ni) &&
				    a->data.non_resident.compression_unit &&
				    a->data.non_resident.compression_unit !=
				     vol->sparse_compression_unit) {
					ntfs_error(vi->i_sb,
						   "Found non-standard compression unit (%u instead of 0 or %d).  Cannot handle this.",
						   a->data.non_resident.compression_unit,
						   vol->sparse_compression_unit);
					err = -EOPNOTSUPP;
					goto unm_err_out;
				}


				if (a->data.non_resident.compression_unit) {
					ni->itype.compressed.block_size = 1U <<
							(a->data.non_resident.compression_unit +
							vol->cluster_size_bits);
					ni->itype.compressed.block_size_bits =
							ffs(ni->itype.compressed.block_size) - 1;
					ni->itype.compressed.block_clusters =
							1U << a->data.non_resident.compression_unit;
				} else {
					ni->itype.compressed.block_size = 0;
					ni->itype.compressed.block_size_bits =
							0;
					ni->itype.compressed.block_clusters =
							0;
				}
				ni->itype.compressed.size = le64_to_cpu(
						a->data.non_resident.compressed_size);
			}
			if (a->data.non_resident.lowest_vcn) {
				ntfs_error(vi->i_sb,
					"First extent of $DATA attribute has non zero lowest_vcn.");
				goto unm_err_out;
			}
			vi->i_size = ni->data_size = le64_to_cpu(a->data.non_resident.data_size);
			ni->initialized_size = le64_to_cpu(a->data.non_resident.initialized_size);
			ni->allocated_size = le64_to_cpu(a->data.non_resident.allocated_size);
		} else { /* Resident attribute. */
			vi->i_size = ni->data_size = ni->initialized_size = le32_to_cpu(
					a->data.resident.value_length);
			ni->allocated_size = le32_to_cpu(a->length) -
					le16_to_cpu(
					a->data.resident.value_offset);
			if (vi->i_size > ni->allocated_size) {
				ntfs_error(vi->i_sb,
					"Resident data attribute is corrupt (size exceeds allocation).");
				goto unm_err_out;
			}
		}
no_data_attr_special_case:
		/* We are done with the mft record, so we release it. */
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(ni);
		m = NULL;
		ctx = NULL;
		/* Setup the operations for this inode. */
		ntfs_set_vfs_operations(vi, vi->i_mode, dev);
	}

	if (NVolSysImmutable(vol) && (ni->flags & FILE_ATTR_SYSTEM) &&
	    !S_ISFIFO(vi->i_mode) && !S_ISSOCK(vi->i_mode) && !S_ISLNK(vi->i_mode))
		vi->i_flags |= S_IMMUTABLE;

	/*
	 * The number of 512-byte blocks used on disk (for stat). This is in so
	 * far inaccurate as it doesn't account for any named streams or other
	 * special non-resident attributes, but that is how Windows works, too,
	 * so we are at least consistent with Windows, if not entirely
	 * consistent with the Linux Way. Doing it the Linux Way would cause a
	 * significant slowdown as it would involve iterating over all
	 * attributes in the mft record and adding the allocated/compressed
	 * sizes of all non-resident attributes present to give us the Linux
	 * correct size that should go into i_blocks (after division by 512).
	 */
	if (S_ISREG(vi->i_mode) && (NInoCompressed(ni) || NInoSparse(ni)))
		vi->i_blocks = ni->itype.compressed.size >> 9;
	else
		vi->i_blocks = ni->allocated_size >> 9;

	ntfs_debug("Done.");
	return 0;
unm_err_out:
	if (!err)
		err = -EIO;
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(ni);
err_out:
	if (err != -EOPNOTSUPP && err != -ENOMEM && vol_err == true) {
		ntfs_error(vol->sb,
			"Failed with error code %i.  Marking corrupt inode 0x%llx as bad.  Run chkdsk.",
			err, ni->mft_no);
		NVolSetErrors(vol);
	}
	return err;
}

/*
 * ntfs_read_locked_attr_inode - read an attribute inode from its base inode
 * @base_vi:	base inode
 * @vi:		attribute inode to read
 *
 * ntfs_read_locked_attr_inode() is called from ntfs_attr_iget() to read the
 * attribute inode described by @vi into memory from the base mft record
 * described by @base_ni.
 *
 * ntfs_read_locked_attr_inode() maps, pins and locks the base inode for
 * reading and looks up the attribute described by @vi before setting up the
 * necessary fields in @vi as well as initializing the ntfs inode.
 *
 * Q: What locks are held when the function is called?
 * A: i_state has I_NEW set, hence the inode is locked, also
 *    i_count is set to 1, so it is not going to go away
 *
 * Return 0 on success and -errno on error.
 *
 * Note this cannot be called for AT_INDEX_ALLOCATION.
 */
static int ntfs_read_locked_attr_inode(struct inode *base_vi, struct inode *vi)
{
	struct ntfs_volume *vol = NTFS_SB(vi->i_sb);
	struct ntfs_inode *ni = NTFS_I(vi), *base_ni = NTFS_I(base_vi);
	struct mft_record *m;
	struct attr_record *a;
	struct ntfs_attr_search_ctx *ctx;
	int err = 0;

	ntfs_debug("Entering for i_ino 0x%llx.", ni->mft_no);

	ntfs_init_big_inode(vi);

	/* Just mirror the values from the base inode. */
	vi->i_uid	= base_vi->i_uid;
	vi->i_gid	= base_vi->i_gid;
	set_nlink(vi, base_vi->i_nlink);
	inode_set_mtime_to_ts(vi, inode_get_mtime(base_vi));
	inode_set_ctime_to_ts(vi, inode_get_ctime(base_vi));
	inode_set_atime_to_ts(vi, inode_get_atime(base_vi));
	vi->i_generation = ni->seq_no = base_ni->seq_no;

	/* Set inode type to zero but preserve permissions. */
	vi->i_mode	= base_vi->i_mode & ~S_IFMT;

	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto unm_err_out;
	}
	/* Find the attribute. */
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err))
		goto unm_err_out;
	a = ctx->attr;
	if (a->flags & (ATTR_COMPRESSION_MASK | ATTR_IS_SPARSE)) {
		if (a->flags & ATTR_COMPRESSION_MASK) {
			NInoSetCompressed(ni);
			ni->flags |= FILE_ATTR_COMPRESSED;
			if ((ni->type != AT_DATA) || (ni->type == AT_DATA &&
					ni->name_len)) {
				ntfs_error(vi->i_sb,
					   "Found compressed non-data or named data attribute.");
				goto unm_err_out;
			}
			if (vol->cluster_size > 4096) {
				ntfs_error(vi->i_sb,
					"Found compressed attribute but compression is disabled due to cluster size (%i) > 4kiB.",
					vol->cluster_size);
				goto unm_err_out;
			}
			if ((a->flags & ATTR_COMPRESSION_MASK) !=
					ATTR_IS_COMPRESSED) {
				ntfs_error(vi->i_sb, "Found unknown compression method.");
				goto unm_err_out;
			}
		}
		/*
		 * The compressed/sparse flag set in an index root just means
		 * to compress all files.
		 */
		if (NInoMstProtected(ni) && ni->type != AT_INDEX_ROOT) {
			ntfs_error(vi->i_sb,
				"Found mst protected attribute but the attribute is %s.",
				NInoCompressed(ni) ? "compressed" : "sparse");
			goto unm_err_out;
		}
		if (a->flags & ATTR_IS_SPARSE) {
			NInoSetSparse(ni);
			ni->flags |= FILE_ATTR_SPARSE_FILE;
		}
	}
	if (a->flags & ATTR_IS_ENCRYPTED) {
		if (NInoCompressed(ni)) {
			ntfs_error(vi->i_sb, "Found encrypted and compressed data.");
			goto unm_err_out;
		}
		/*
		 * The encryption flag set in an index root just means to
		 * encrypt all files.
		 */
		if (NInoMstProtected(ni) && ni->type != AT_INDEX_ROOT) {
			ntfs_error(vi->i_sb,
				"Found mst protected attribute but the attribute is encrypted.");
			goto unm_err_out;
		}
		if (ni->type != AT_DATA) {
			ntfs_error(vi->i_sb,
				"Found encrypted non-data attribute.");
			goto unm_err_out;
		}
		NInoSetEncrypted(ni);
		ni->flags |= FILE_ATTR_ENCRYPTED;
	}
	if (!a->non_resident) {
		/* Ensure the attribute name is placed before the value. */
		if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
				le16_to_cpu(a->data.resident.value_offset)))) {
			ntfs_error(vol->sb,
				"Attribute name is placed after the attribute value.");
			goto unm_err_out;
		}
		if (NInoMstProtected(ni)) {
			ntfs_error(vi->i_sb,
				"Found mst protected attribute but the attribute is resident.");
			goto unm_err_out;
		}
		vi->i_size = ni->initialized_size = ni->data_size = le32_to_cpu(
				a->data.resident.value_length);
		ni->allocated_size = le32_to_cpu(a->length) -
				le16_to_cpu(a->data.resident.value_offset);
		if (vi->i_size > ni->allocated_size) {
			ntfs_error(vi->i_sb,
				"Resident attribute is corrupt (size exceeds allocation).");
			goto unm_err_out;
		}
	} else {
		NInoSetNonResident(ni);
		/*
		 * Ensure the attribute name is placed before the mapping pairs
		 * array.
		 */
		if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
				le16_to_cpu(
				a->data.non_resident.mapping_pairs_offset)))) {
			ntfs_error(vol->sb,
				"Attribute name is placed after the mapping pairs array.");
			goto unm_err_out;
		}
		if (NInoCompressed(ni) || NInoSparse(ni)) {
			if (NInoCompressed(ni) && a->data.non_resident.compression_unit != 4) {
				ntfs_error(vi->i_sb,
					"Found non-standard compression unit (%u instead of 4).  Cannot handle this.",
					a->data.non_resident.compression_unit);
				err = -EOPNOTSUPP;
				goto unm_err_out;
			}
			if (a->data.non_resident.compression_unit) {
				ni->itype.compressed.block_size = 1U <<
						(a->data.non_resident.compression_unit +
						vol->cluster_size_bits);
				ni->itype.compressed.block_size_bits =
						ffs(ni->itype.compressed.block_size) - 1;
				ni->itype.compressed.block_clusters = 1U <<
						a->data.non_resident.compression_unit;
			} else {
				ni->itype.compressed.block_size = 0;
				ni->itype.compressed.block_size_bits = 0;
				ni->itype.compressed.block_clusters = 0;
			}
			ni->itype.compressed.size = le64_to_cpu(
					a->data.non_resident.compressed_size);
		}
		if (a->data.non_resident.lowest_vcn) {
			ntfs_error(vi->i_sb, "First extent of attribute has non-zero lowest_vcn.");
			goto unm_err_out;
		}
		vi->i_size = ni->data_size = le64_to_cpu(a->data.non_resident.data_size);
		ni->initialized_size = le64_to_cpu(a->data.non_resident.initialized_size);
		ni->allocated_size = le64_to_cpu(a->data.non_resident.allocated_size);
	}
	vi->i_mapping->a_ops = &ntfs_aops;
	if ((NInoCompressed(ni) || NInoSparse(ni)) && ni->type != AT_INDEX_ROOT)
		vi->i_blocks = ni->itype.compressed.size >> 9;
	else
		vi->i_blocks = ni->allocated_size >> 9;
	/*
	 * Make sure the base inode does not go away and attach it to the
	 * attribute inode.
	 */
	if (!igrab(base_vi)) {
		err = -ENOENT;
		goto unm_err_out;
	}
	ni->ext.base_ntfs_ino = base_ni;
	ni->nr_extents = -1;

	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);

	ntfs_debug("Done.");
	return 0;

unm_err_out:
	if (!err)
		err = -EIO;
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
err_out:
	if (err != -ENOENT)
		ntfs_error(vol->sb,
			"Failed with error code %i while reading attribute inode (mft_no 0x%llx, type 0x%x, name_len %i).  Marking corrupt inode and base inode 0x%llx as bad.  Run chkdsk.",
			err, ni->mft_no, ni->type, ni->name_len,
			base_ni->mft_no);
	if (err != -ENOENT && err != -ENOMEM)
		NVolSetErrors(vol);
	return err;
}

/*
 * ntfs_read_locked_index_inode - read an index inode from its base inode
 * @base_vi:	base inode
 * @vi:		index inode to read
 *
 * ntfs_read_locked_index_inode() is called from ntfs_index_iget() to read the
 * index inode described by @vi into memory from the base mft record described
 * by @base_ni.
 *
 * ntfs_read_locked_index_inode() maps, pins and locks the base inode for
 * reading and looks up the attributes relating to the index described by @vi
 * before setting up the necessary fields in @vi as well as initializing the
 * ntfs inode.
 *
 * Note, index inodes are essentially attribute inodes (NInoAttr() is true)
 * with the attribute type set to AT_INDEX_ALLOCATION.  Apart from that, they
 * are setup like directory inodes since directories are a special case of
 * indices ao they need to be treated in much the same way.  Most importantly,
 * for small indices the index allocation attribute might not actually exist.
 * However, the index root attribute always exists but this does not need to
 * have an inode associated with it and this is why we define a new inode type
 * index.  Also, like for directories, we need to have an attribute inode for
 * the bitmap attribute corresponding to the index allocation attribute and we
 * can store this in the appropriate field of the inode, just like we do for
 * normal directory inodes.
 *
 * Q: What locks are held when the function is called?
 * A: i_state has I_NEW set, hence the inode is locked, also
 *    i_count is set to 1, so it is not going to go away
 *
 * Return 0 on success and -errno on error.
 */
static int ntfs_read_locked_index_inode(struct inode *base_vi, struct inode *vi)
{
	loff_t bvi_size;
	struct ntfs_volume *vol = NTFS_SB(vi->i_sb);
	struct ntfs_inode *ni = NTFS_I(vi), *base_ni = NTFS_I(base_vi), *bni;
	struct inode *bvi;
	struct mft_record *m;
	struct attr_record *a;
	struct ntfs_attr_search_ctx *ctx;
	struct index_root *ir;
	u8 *ir_end, *index_end;
	int err = 0;

	ntfs_debug("Entering for i_ino 0x%llx.", ni->mft_no);
	lockdep_assert_held(&base_ni->mrec_lock);

	ntfs_init_big_inode(vi);
	/* Just mirror the values from the base inode. */
	vi->i_uid	= base_vi->i_uid;
	vi->i_gid	= base_vi->i_gid;
	set_nlink(vi, base_vi->i_nlink);
	inode_set_mtime_to_ts(vi, inode_get_mtime(base_vi));
	inode_set_ctime_to_ts(vi, inode_get_ctime(base_vi));
	inode_set_atime_to_ts(vi, inode_get_atime(base_vi));
	vi->i_generation = ni->seq_no = base_ni->seq_no;
	/* Set inode type to zero but preserve permissions. */
	vi->i_mode	= base_vi->i_mode & ~S_IFMT;
	/* Map the mft record for the base inode. */
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto unm_err_out;
	}
	/* Find the index root attribute. */
	err = ntfs_attr_lookup(AT_INDEX_ROOT, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -ENOENT)
			ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is missing.");
		goto unm_err_out;
	}
	a = ctx->attr;
	/* Set up the state. */
	if (unlikely(a->non_resident)) {
		ntfs_error(vol->sb, "$INDEX_ROOT attribute is not resident.");
		goto unm_err_out;
	}
	/* Ensure the attribute name is placed before the value. */
	if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
			le16_to_cpu(a->data.resident.value_offset)))) {
		ntfs_error(vol->sb,
			"$INDEX_ROOT attribute name is placed after the attribute value.");
		goto unm_err_out;
	}

	ir = (struct index_root *)((u8 *)a + le16_to_cpu(a->data.resident.value_offset));
	ir_end = (u8 *)ir + le32_to_cpu(a->data.resident.value_length);
	if (ir_end > (u8 *)ctx->mrec + vol->mft_record_size) {
		ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is corrupt.");
		goto unm_err_out;
	}
	index_end = (u8 *)&ir->index + le32_to_cpu(ir->index.index_length);
	if (index_end > ir_end) {
		ntfs_error(vi->i_sb, "Index is corrupt.");
		goto unm_err_out;
	}

	ni->itype.index.collation_rule = ir->collation_rule;
	ntfs_debug("Index collation rule is 0x%x.",
			le32_to_cpu(ir->collation_rule));
	ni->itype.index.block_size = le32_to_cpu(ir->index_block_size);
	if (!is_power_of_2(ni->itype.index.block_size)) {
		ntfs_error(vi->i_sb, "Index block size (%u) is not a power of two.",
				ni->itype.index.block_size);
		goto unm_err_out;
	}
	if (ni->itype.index.block_size > PAGE_SIZE) {
		ntfs_error(vi->i_sb, "Index block size (%u) > PAGE_SIZE (%ld) is not supported.",
				ni->itype.index.block_size, PAGE_SIZE);
		err = -EOPNOTSUPP;
		goto unm_err_out;
	}
	if (ni->itype.index.block_size < NTFS_BLOCK_SIZE) {
		ntfs_error(vi->i_sb,
				"Index block size (%u) < NTFS_BLOCK_SIZE (%i) is not supported.",
				ni->itype.index.block_size, NTFS_BLOCK_SIZE);
		err = -EOPNOTSUPP;
		goto unm_err_out;
	}
	ni->itype.index.block_size_bits = ffs(ni->itype.index.block_size) - 1;
	/* Determine the size of a vcn in the index. */
	if (vol->cluster_size <= ni->itype.index.block_size) {
		ni->itype.index.vcn_size = vol->cluster_size;
		ni->itype.index.vcn_size_bits = vol->cluster_size_bits;
	} else {
		ni->itype.index.vcn_size = vol->sector_size;
		ni->itype.index.vcn_size_bits = vol->sector_size_bits;
	}

	/* Find index allocation attribute. */
	ntfs_attr_reinit_search_ctx(ctx);
	err = ntfs_attr_lookup(AT_INDEX_ALLOCATION, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -ENOENT) {
			/* No index allocation. */
			vi->i_size = ni->initialized_size = ni->allocated_size = 0;
			/* We are done with the mft record, so we release it. */
			ntfs_attr_put_search_ctx(ctx);
			unmap_mft_record(base_ni);
			m = NULL;
			ctx = NULL;
			goto skip_large_index_stuff;
		} else
			ntfs_error(vi->i_sb, "Failed to lookup $INDEX_ALLOCATION attribute.");
		goto unm_err_out;
	}
	NInoSetIndexAllocPresent(ni);
	NInoSetNonResident(ni);
	ni->type = AT_INDEX_ALLOCATION;

	a = ctx->attr;
	if (!a->non_resident) {
		ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute is resident.");
		goto unm_err_out;
	}
	/*
	 * Ensure the attribute name is placed before the mapping pairs array.
	 */
	if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
			le16_to_cpu(a->data.non_resident.mapping_pairs_offset)))) {
		ntfs_error(vol->sb,
			"$INDEX_ALLOCATION attribute name is placed after the mapping pairs array.");
		goto unm_err_out;
	}
	if (a->flags & ATTR_IS_ENCRYPTED) {
		ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute is encrypted.");
		goto unm_err_out;
	}
	if (a->flags & ATTR_IS_SPARSE) {
		ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute is sparse.");
		goto unm_err_out;
	}
	if (a->flags & ATTR_COMPRESSION_MASK) {
		ntfs_error(vi->i_sb,
			"$INDEX_ALLOCATION attribute is compressed.");
		goto unm_err_out;
	}
	if (a->data.non_resident.lowest_vcn) {
		ntfs_error(vi->i_sb,
			"First extent of $INDEX_ALLOCATION attribute has non zero lowest_vcn.");
		goto unm_err_out;
	}
	vi->i_size = ni->data_size = le64_to_cpu(a->data.non_resident.data_size);
	ni->initialized_size = le64_to_cpu(a->data.non_resident.initialized_size);
	ni->allocated_size = le64_to_cpu(a->data.non_resident.allocated_size);
	/*
	 * We are done with the mft record, so we release it.  Otherwise
	 * we would deadlock in ntfs_attr_iget().
	 */
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	m = NULL;
	ctx = NULL;
	/* Get the index bitmap attribute inode. */
	bvi = ntfs_attr_iget(base_vi, AT_BITMAP, ni->name, ni->name_len);
	if (IS_ERR(bvi)) {
		ntfs_error(vi->i_sb, "Failed to get bitmap attribute.");
		err = PTR_ERR(bvi);
		goto unm_err_out;
	}
	bni = NTFS_I(bvi);
	if (NInoCompressed(bni) || NInoEncrypted(bni) ||
			NInoSparse(bni)) {
		ntfs_error(vi->i_sb,
			"$BITMAP attribute is compressed and/or encrypted and/or sparse.");
		goto iput_unm_err_out;
	}
	/* Consistency check bitmap size vs. index allocation size. */
	bvi_size = i_size_read(bvi);
	if ((bvi_size << 3) < (vi->i_size >> ni->itype.index.block_size_bits)) {
		ntfs_error(vi->i_sb,
			"Index bitmap too small (0x%llx) for index allocation (0x%llx).",
			bvi_size << 3, vi->i_size);
		goto iput_unm_err_out;
	}
	iput(bvi);
skip_large_index_stuff:
	/* Setup the operations for this index inode. */
	ntfs_set_vfs_operations(vi, S_IFDIR, 0);
	vi->i_blocks = ni->allocated_size >> 9;
	/*
	 * Make sure the base inode doesn't go away and attach it to the
	 * index inode.
	 */
	if (!igrab(base_vi))
		goto unm_err_out;
	ni->ext.base_ntfs_ino = base_ni;
	ni->nr_extents = -1;

	ntfs_debug("Done.");
	return 0;
iput_unm_err_out:
	iput(bvi);
unm_err_out:
	if (!err)
		err = -EIO;
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
err_out:
	ntfs_error(vi->i_sb,
		"Failed with error code %i while reading index inode (mft_no 0x%llx, name_len %i.",
		err, ni->mft_no, ni->name_len);
	if (err != -EOPNOTSUPP && err != -ENOMEM)
		NVolSetErrors(vol);
	return err;
}

/*
 * load_attribute_list_mount - load an attribute list into memory
 * @vol:		ntfs volume from which to read
 * @rl:			runlist of the attribute list
 * @al_start:		destination buffer
 * @size:		size of the destination buffer in bytes
 * @initialized_size:	initialized size of the attribute list
 *
 * Walk the runlist @rl and load all clusters from it copying them into
 * the linear buffer @al. The maximum number of bytes copied to @al is @size
 * bytes. Note, @size does not need to be a multiple of the cluster size. If
 * @initialized_size is less than @size, the region in @al between
 * @initialized_size and @size will be zeroed and not read from disk.
 *
 * Return 0 on success or -errno on error.
 */
static int load_attribute_list_mount(struct ntfs_volume *vol,
		struct runlist_element *rl, u8 *al_start, const s64 size,
		const s64 initialized_size)
{
	s64 lcn;
	u8 *al = al_start;
	u8 *al_end = al + initialized_size;
	struct super_block *sb;
	int err = 0;
	loff_t rl_byte_off, rl_byte_len;

	ntfs_debug("Entering.");
	if (!vol || !rl || !al || size <= 0 || initialized_size < 0 ||
			initialized_size > size)
		return -EINVAL;
	if (!initialized_size) {
		memset(al, 0, size);
		return 0;
	}
	sb = vol->sb;

	/* Read all clusters specified by the runlist one run at a time. */
	while (rl->length) {
		lcn = ntfs_rl_vcn_to_lcn(rl, rl->vcn);
		ntfs_debug("Reading vcn = 0x%llx, lcn = 0x%llx.",
				(unsigned long long)rl->vcn,
				(unsigned long long)lcn);
		/* The attribute list cannot be sparse. */
		if (lcn < 0) {
			ntfs_error(sb, "ntfs_rl_vcn_to_lcn() failed. Cannot read attribute list.");
			goto err_out;
		}

		rl_byte_off = ntfs_cluster_to_bytes(vol, lcn);
		rl_byte_len = ntfs_cluster_to_bytes(vol, rl->length);

		if (al + rl_byte_len > al_end)
			rl_byte_len = al_end - al;

		err = ntfs_bdev_read(sb->s_bdev, al, rl_byte_off,
				   round_up(rl_byte_len, SECTOR_SIZE));
		if (err) {
			ntfs_error(sb, "Cannot read attribute list.");
			goto err_out;
		}

		if (al + rl_byte_len >= al_end) {
			if (initialized_size < size)
				goto initialize;
			goto done;
		}

		al += rl_byte_len;
		rl++;
	}
	if (initialized_size < size) {
initialize:
		memset(al_start + initialized_size, 0, size - initialized_size);
	}
done:
	return err;
	/* Real overflow! */
	ntfs_error(sb, "Attribute list buffer overflow. Read attribute list is truncated.");
err_out:
	err = -EIO;
	goto done;
}

/*
 * The MFT inode has special locking, so teach the lock validator
 * about this by splitting off the locking rules of the MFT from
 * the locking rules of other inodes. The MFT inode can never be
 * accessed from the VFS side (or even internally), only by the
 * map_mft functions.
 */
static struct lock_class_key mft_ni_runlist_lock_key, mft_ni_mrec_lock_key;

/*
 * ntfs_read_inode_mount - special read_inode for mount time use only
 * @vi:		inode to read
 *
 * Read inode FILE_MFT at mount time, only called with super_block lock
 * held from within the read_super() code path.
 *
 * This function exists because when it is called the page cache for $MFT/$DATA
 * is not initialized and hence we cannot get at the contents of mft records
 * by calling map_mft_record*().
 *
 * Further it needs to cope with the circular references problem, i.e. cannot
 * load any attributes other than $ATTRIBUTE_LIST until $DATA is loaded, because
 * we do not know where the other extent mft records are yet and again, because
 * we cannot call map_mft_record*() yet.  Obviously this applies only when an
 * attribute list is actually present in $MFT inode.
 *
 * We solve these problems by starting with the $DATA attribute before anything
 * else and iterating using ntfs_attr_lookup($DATA) over all extents.  As each
 * extent is found, we ntfs_mapping_pairs_decompress() including the implied
 * ntfs_runlists_merge().  Each step of the iteration necessarily provides
 * sufficient information for the next step to complete.
 *
 * This should work but there are two possible pit falls (see inline comments
 * below), but only time will tell if they are real pits or just smoke...
 */
int ntfs_read_inode_mount(struct inode *vi)
{
	s64 next_vcn, last_vcn, highest_vcn;
	struct super_block *sb = vi->i_sb;
	struct ntfs_volume *vol = NTFS_SB(sb);
	struct ntfs_inode *ni = NTFS_I(vi);
	struct mft_record *m = NULL;
	struct attr_record *a;
	struct ntfs_attr_search_ctx *ctx;
	unsigned int i, nr_blocks;
	int err;
	size_t new_rl_count;

	ntfs_debug("Entering.");

	/* Initialize the ntfs specific part of @vi. */
	ntfs_init_big_inode(vi);


	/* Setup the data attribute. It is special as it is mst protected. */
	NInoSetNonResident(ni);
	NInoSetMstProtected(ni);
	NInoSetSparseDisabled(ni);
	ni->type = AT_DATA;
	ni->name = AT_UNNAMED;
	ni->name_len = 0;
	/*
	 * This sets up our little cheat allowing us to reuse the async read io
	 * completion handler for directories.
	 */
	ni->itype.index.block_size = vol->mft_record_size;
	ni->itype.index.block_size_bits = vol->mft_record_size_bits;

	/* Very important! Needed to be able to call map_mft_record*(). */
	vol->mft_ino = vi;

	/* Allocate enough memory to read the first mft record. */
	if (vol->mft_record_size > 64 * 1024) {
		ntfs_error(sb, "Unsupported mft record size %i (max 64kiB).",
				vol->mft_record_size);
		goto err_out;
	}

	i = vol->mft_record_size;
	if (i < sb->s_blocksize)
		i = sb->s_blocksize;

	m = kzalloc(i, GFP_NOFS);
	if (!m) {
		ntfs_error(sb, "Failed to allocate buffer for $MFT record 0.");
		goto err_out;
	}

	/* Determine the first block of the $MFT/$DATA attribute. */
	nr_blocks = ntfs_bytes_to_sector(vol, vol->mft_record_size);
	if (!nr_blocks)
		nr_blocks = 1;

	/* Load $MFT/$DATA's first mft record. */
	err = ntfs_bdev_read(sb->s_bdev, (char *)m,
			     ntfs_cluster_to_bytes(vol, vol->mft_lcn), i);
	if (err) {
		ntfs_error(sb, "Device read failed.");
		goto err_out;
	}

	if (le32_to_cpu(m->bytes_allocated) != vol->mft_record_size) {
		ntfs_error(sb, "Incorrect mft record size %u in superblock, should be %u.",
				le32_to_cpu(m->bytes_allocated), vol->mft_record_size);
		goto err_out;
	}

	/* Apply the mst fixups. */
	if (post_read_mst_fixup((struct ntfs_record *)m, vol->mft_record_size)) {
		ntfs_error(sb, "MST fixup failed. $MFT is corrupt.");
		goto err_out;
	}

	if (ntfs_mft_record_check(vol, m, FILE_MFT)) {
		ntfs_error(sb, "ntfs_mft_record_check failed. $MFT is corrupt.");
		goto err_out;
	}

	/* Need this to sanity check attribute list references to $MFT. */
	vi->i_generation = ni->seq_no = le16_to_cpu(m->sequence_number);

	/* Provides read_folio() for map_mft_record(). */
	vi->i_mapping->a_ops = &ntfs_mft_aops;

	ctx = ntfs_attr_get_search_ctx(ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto err_out;
	}

	/* Find the attribute list attribute if present. */
	err = ntfs_attr_lookup(AT_ATTRIBUTE_LIST, NULL, 0, 0, 0, NULL, 0, ctx);
	if (err) {
		if (unlikely(err != -ENOENT)) {
			ntfs_error(sb,
				"Failed to lookup attribute list attribute. You should run chkdsk.");
			goto put_err_out;
		}
	} else /* if (!err) */ {
		struct attr_list_entry *al_entry, *next_al_entry;
		u8 *al_end;
		static const char *es = "  Not allowed.  $MFT is corrupt.  You should run chkdsk.";

		ntfs_debug("Attribute list attribute found in $MFT.");
		NInoSetAttrList(ni);
		a = ctx->attr;
		if (a->flags & ATTR_COMPRESSION_MASK) {
			ntfs_error(sb,
				"Attribute list attribute is compressed.%s",
				es);
			goto put_err_out;
		}
		if (a->flags & ATTR_IS_ENCRYPTED ||
				a->flags & ATTR_IS_SPARSE) {
			if (a->non_resident) {
				ntfs_error(sb,
					"Non-resident attribute list attribute is encrypted/sparse.%s",
					es);
				goto put_err_out;
			}
			ntfs_warning(sb,
				"Resident attribute list attribute in $MFT system file is marked encrypted/sparse which is not true.  However, Windows allows this and chkdsk does not detect or correct it so we will just ignore the invalid flags and pretend they are not set.");
		}
		/* Now allocate memory for the attribute list. */
		ni->attr_list_size = (u32)ntfs_attr_size(a);
		if (!ni->attr_list_size) {
			ntfs_error(sb, "Attr_list_size is zero");
			goto put_err_out;
		}
		ni->attr_list = kvzalloc(round_up(ni->attr_list_size, SECTOR_SIZE),
					 GFP_NOFS);
		if (!ni->attr_list) {
			ntfs_error(sb, "Not enough memory to allocate buffer for attribute list.");
			goto put_err_out;
		}
		if (a->non_resident) {
			struct runlist_element *rl;
			size_t new_rl_count;

			NInoSetAttrListNonResident(ni);
			if (a->data.non_resident.lowest_vcn) {
				ntfs_error(sb,
					"Attribute list has non zero lowest_vcn. $MFT is corrupt. You should run chkdsk.");
				goto put_err_out;
			}

			rl = ntfs_mapping_pairs_decompress(vol, a, NULL, &new_rl_count);
			if (IS_ERR(rl)) {
				err = PTR_ERR(rl);
				ntfs_error(sb,
					   "Mapping pairs decompression failed with error code %i.",
					   -err);
				goto put_err_out;
			}

			err = load_attribute_list_mount(vol, rl, ni->attr_list, ni->attr_list_size,
					le64_to_cpu(a->data.non_resident.initialized_size));
			kvfree(rl);
			if (err) {
				ntfs_error(sb,
					   "Failed to load attribute list with error code %i.",
					   -err);
				goto put_err_out;
			}
		} else /* if (!ctx.attr->non_resident) */ {
			if ((u8 *)a + le16_to_cpu(
					a->data.resident.value_offset) +
					le32_to_cpu(a->data.resident.value_length) >
					(u8 *)ctx->mrec + vol->mft_record_size) {
				ntfs_error(sb, "Corrupt attribute list attribute.");
				goto put_err_out;
			}
			/* Now copy the attribute list. */
			memcpy(ni->attr_list, (u8 *)a + le16_to_cpu(
					a->data.resident.value_offset),
					le32_to_cpu(a->data.resident.value_length));
		}
		/* The attribute list is now setup in memory. */
		al_entry = (struct attr_list_entry *)ni->attr_list;
		al_end = (u8 *)al_entry + ni->attr_list_size;
		for (;; al_entry = next_al_entry) {
			/* Out of bounds check. */
			if ((u8 *)al_entry < ni->attr_list ||
					(u8 *)al_entry > al_end)
				goto em_put_err_out;
			/* Catch the end of the attribute list. */
			if ((u8 *)al_entry == al_end)
				goto em_put_err_out;
			if (!al_entry->length)
				goto em_put_err_out;
			if ((u8 *)al_entry + 6 > al_end ||
			    (u8 *)al_entry + le16_to_cpu(al_entry->length) > al_end)
				goto em_put_err_out;
			next_al_entry = (struct attr_list_entry *)((u8 *)al_entry +
					le16_to_cpu(al_entry->length));
			if (le32_to_cpu(al_entry->type) > le32_to_cpu(AT_DATA))
				goto em_put_err_out;
			if (al_entry->type != AT_DATA)
				continue;
			/* We want an unnamed attribute. */
			if (al_entry->name_length)
				goto em_put_err_out;
			/* Want the first entry, i.e. lowest_vcn == 0. */
			if (al_entry->lowest_vcn)
				goto em_put_err_out;
			/* First entry has to be in the base mft record. */
			if (MREF_LE(al_entry->mft_reference) != vi->i_ino) {
				/* MFT references do not match, logic fails. */
				ntfs_error(sb,
					"BUG: The first $DATA extent of $MFT is not in the base mft record.");
				goto put_err_out;
			} else {
				/* Sequence numbers must match. */
				if (MSEQNO_LE(al_entry->mft_reference) !=
						ni->seq_no)
					goto em_put_err_out;
				/* Got it. All is ok. We can stop now. */
				break;
			}
		}
	}

	ntfs_attr_reinit_search_ctx(ctx);

	/* Now load all attribute extents. */
	a = NULL;
	next_vcn = last_vcn = highest_vcn = 0;
	while (!(err = ntfs_attr_lookup(AT_DATA, NULL, 0, 0, next_vcn, NULL, 0,
			ctx))) {
		struct runlist_element *nrl;

		/* Cache the current attribute. */
		a = ctx->attr;
		/* $MFT must be non-resident. */
		if (!a->non_resident) {
			ntfs_error(sb,
				"$MFT must be non-resident but a resident extent was found. $MFT is corrupt. Run chkdsk.");
			goto put_err_out;
		}
		/* $MFT must be uncompressed and unencrypted. */
		if (a->flags & ATTR_COMPRESSION_MASK ||
				a->flags & ATTR_IS_ENCRYPTED ||
				a->flags & ATTR_IS_SPARSE) {
			ntfs_error(sb,
				"$MFT must be uncompressed, non-sparse, and unencrypted but a compressed/sparse/encrypted extent was found. $MFT is corrupt. Run chkdsk.");
			goto put_err_out;
		}
		/*
		 * Decompress the mapping pairs array of this extent and merge
		 * the result into the existing runlist. No need for locking
		 * as we have exclusive access to the inode at this time and we
		 * are a mount in progress task, too.
		 */
		nrl = ntfs_mapping_pairs_decompress(vol, a, &ni->runlist,
						    &new_rl_count);
		if (IS_ERR(nrl)) {
			ntfs_error(sb,
				"ntfs_mapping_pairs_decompress() failed with error code %ld.",
				PTR_ERR(nrl));
			goto put_err_out;
		}
		ni->runlist.rl = nrl;
		ni->runlist.count = new_rl_count;

		/* Are we in the first extent? */
		if (!next_vcn) {
			if (a->data.non_resident.lowest_vcn) {
				ntfs_error(sb,
					"First extent of $DATA attribute has non zero lowest_vcn. $MFT is corrupt. You should run chkdsk.");
				goto put_err_out;
			}
			/* Get the last vcn in the $DATA attribute. */
			last_vcn = ntfs_bytes_to_cluster(vol,
					le64_to_cpu(a->data.non_resident.allocated_size));
			/* Fill in the inode size. */
			vi->i_size = le64_to_cpu(a->data.non_resident.data_size);
			ni->initialized_size = le64_to_cpu(a->data.non_resident.initialized_size);
			ni->allocated_size = le64_to_cpu(a->data.non_resident.allocated_size);
			/*
			 * Verify the number of mft records does not exceed
			 * 2^32 - 1.
			 */
			if ((vi->i_size >> vol->mft_record_size_bits) >=
					(1ULL << 32)) {
				ntfs_error(sb, "$MFT is too big! Aborting.");
				goto put_err_out;
			}
			/*
			 * We have got the first extent of the runlist for
			 * $MFT which means it is now relatively safe to call
			 * the normal ntfs_read_inode() function.
			 * Complete reading the inode, this will actually
			 * re-read the mft record for $MFT, this time entering
			 * it into the page cache with which we complete the
			 * kick start of the volume. It should be safe to do
			 * this now as the first extent of $MFT/$DATA is
			 * already known and we would hope that we don't need
			 * further extents in order to find the other
			 * attributes belonging to $MFT. Only time will tell if
			 * this is really the case. If not we will have to play
			 * magic at this point, possibly duplicating a lot of
			 * ntfs_read_inode() at this point. We will need to
			 * ensure we do enough of its work to be able to call
			 * ntfs_read_inode() on extents of $MFT/$DATA. But lets
			 * hope this never happens...
			 */
			err = ntfs_read_locked_inode(vi);
			if (err) {
				ntfs_error(sb, "ntfs_read_inode() of $MFT failed.\n");
				ntfs_attr_put_search_ctx(ctx);
				/* Revert to the safe super operations. */
				kfree(m);
				return -1;
			}
			/*
			 * Re-initialize some specifics about $MFT's inode as
			 * ntfs_read_inode() will have set up the default ones.
			 */
			/* Set uid and gid to root. */
			vi->i_uid = GLOBAL_ROOT_UID;
			vi->i_gid = GLOBAL_ROOT_GID;
			/* Regular file. No access for anyone. */
			vi->i_mode = S_IFREG;
			/* No VFS initiated operations allowed for $MFT. */
			vi->i_op = &ntfs_empty_inode_ops;
			vi->i_fop = &ntfs_empty_file_ops;
		}

		/* Get the lowest vcn for the next extent. */
		highest_vcn = le64_to_cpu(a->data.non_resident.highest_vcn);
		next_vcn = highest_vcn + 1;

		/* Only one extent or error, which we catch below. */
		if (next_vcn <= 0)
			break;

		/* Avoid endless loops due to corruption. */
		if (next_vcn < le64_to_cpu(a->data.non_resident.lowest_vcn)) {
			ntfs_error(sb, "$MFT has corrupt attribute list attribute. Run chkdsk.");
			goto put_err_out;
		}
	}
	if (err != -ENOENT) {
		ntfs_error(sb, "Failed to lookup $MFT/$DATA attribute extent. Run chkdsk.\n");
		goto put_err_out;
	}
	if (!a) {
		ntfs_error(sb, "$MFT/$DATA attribute not found. $MFT is corrupt. Run chkdsk.");
		goto put_err_out;
	}
	if (highest_vcn && highest_vcn != last_vcn - 1) {
		ntfs_error(sb, "Failed to load the complete runlist for $MFT/$DATA. Run chkdsk.");
		ntfs_debug("highest_vcn = 0x%llx, last_vcn - 1 = 0x%llx",
				(unsigned long long)highest_vcn,
				(unsigned long long)last_vcn - 1);
		goto put_err_out;
	}
	ntfs_attr_put_search_ctx(ctx);
	ntfs_debug("Done.");
	kfree(m);

	/*
	 * Split the locking rules of the MFT inode from the
	 * locking rules of other inodes:
	 */
	lockdep_set_class(&ni->runlist.lock, &mft_ni_runlist_lock_key);
	lockdep_set_class(&ni->mrec_lock, &mft_ni_mrec_lock_key);

	return 0;

em_put_err_out:
	ntfs_error(sb,
		"Couldn't find first extent of $DATA attribute in attribute list. $MFT is corrupt. Run chkdsk.");
put_err_out:
	ntfs_attr_put_search_ctx(ctx);
err_out:
	ntfs_error(sb, "Failed. Marking inode as bad.");
	kfree(m);
	return -1;
}

static void __ntfs_clear_inode(struct ntfs_inode *ni)
{
	/* Free all alocated memory. */
	if (NInoNonResident(ni) && ni->runlist.rl) {
		kvfree(ni->runlist.rl);
		ni->runlist.rl = NULL;
	}

	if (ni->attr_list) {
		kvfree(ni->attr_list);
		ni->attr_list = NULL;
	}

	if (ni->name_len && ni->name != I30 &&
	    ni->name != reparse_index_name &&
	    ni->name != objid_index_name) {
		WARN_ON(!ni->name);
		kfree(ni->name);
	}
}

void ntfs_clear_extent_inode(struct ntfs_inode *ni)
{
	ntfs_debug("Entering for inode 0x%llx.", ni->mft_no);

	WARN_ON(NInoAttr(ni));
	WARN_ON(ni->nr_extents != -1);

	__ntfs_clear_inode(ni);
	ntfs_destroy_extent_inode(ni);
}

static int ntfs_delete_base_inode(struct ntfs_inode *ni)
{
	struct super_block *sb = ni->vol->sb;
	int err;

	if (NInoAttr(ni) || ni->nr_extents == -1)
		return 0;

	err = ntfs_non_resident_dealloc_clusters(ni);

	/*
	 * Deallocate extent mft records and free extent inodes.
	 * No need to lock as no one else has a reference.
	 */
	while (ni->nr_extents) {
		err = ntfs_mft_record_free(ni->vol, *(ni->ext.extent_ntfs_inos));
		if (err)
			ntfs_error(sb,
				"Failed to free extent MFT record. Leaving inconsistent metadata.\n");
		ntfs_inode_close(*(ni->ext.extent_ntfs_inos));
	}

	/* Deallocate base mft record */
	err = ntfs_mft_record_free(ni->vol, ni);
	if (err)
		ntfs_error(sb, "Failed to free base MFT record. Leaving inconsistent metadata.\n");
	return err;
}

/*
 * ntfs_evict_big_inode - clean up the ntfs specific part of an inode
 * @vi:		vfs inode pending annihilation
 *
 * When the VFS is going to remove an inode from memory, ntfs_clear_big_inode()
 * is called, which deallocates all memory belonging to the NTFS specific part
 * of the inode and returns.
 *
 * If the MFT record is dirty, we commit it before doing anything else.
 */
void ntfs_evict_big_inode(struct inode *vi)
{
	struct ntfs_inode *ni = NTFS_I(vi);

	truncate_inode_pages_final(&vi->i_data);

	if (!vi->i_nlink) {
		if (!NInoAttr(ni)) {
			/* Never called with extent inodes */
			WARN_ON(ni->nr_extents == -1);
			ntfs_delete_base_inode(ni);
		}
		goto release;
	}

	if (NInoDirty(ni)) {
		/* Committing the inode also commits all extent inodes. */
		ntfs_commit_inode(vi);

		if (NInoDirty(ni)) {
			ntfs_debug("Failed to commit dirty inode 0x%llx.  Losing data!",
				   ni->mft_no);
			NInoClearAttrListDirty(ni);
			NInoClearDirty(ni);
		}
	}

	/* No need to lock at this stage as no one else has a reference. */
	if (ni->nr_extents > 0) {
		int i;

		for (i = 0; i < ni->nr_extents; i++) {
			if (ni->ext.extent_ntfs_inos[i])
				ntfs_clear_extent_inode(ni->ext.extent_ntfs_inos[i]);
		}
		ni->nr_extents = 0;
		kvfree(ni->ext.extent_ntfs_inos);
	}

release:
	clear_inode(vi);
	__ntfs_clear_inode(ni);

	if (NInoAttr(ni)) {
		/* Release the base inode if we are holding it. */
		if (ni->nr_extents == -1) {
			iput(VFS_I(ni->ext.base_ntfs_ino));
			ni->nr_extents = 0;
			ni->ext.base_ntfs_ino = NULL;
		}
	}

	if (!atomic_dec_and_test(&ni->count))
		WARN_ON(1);
	if (ni->folio)
		folio_put(ni->folio);
	kfree(ni->mrec);
	kvfree(ni->target);
}

/*
 * ntfs_show_options - show mount options in /proc/mounts
 * @sf:		seq_file in which to write our mount options
 * @root:	root of the mounted tree whose mount options to display
 *
 * Called by the VFS once for each mounted ntfs volume when someone reads
 * /proc/mounts in order to display the NTFS specific mount options of each
 * mount. The mount options of fs specified by @root are written to the seq file
 * @sf and success is returned.
 */
int ntfs_show_options(struct seq_file *sf, struct dentry *root)
{
	struct ntfs_volume *vol = NTFS_SB(root->d_sb);
	int i;

	if (uid_valid(vol->uid))
		seq_printf(sf, ",uid=%i", from_kuid_munged(&init_user_ns, vol->uid));
	if (gid_valid(vol->gid))
		seq_printf(sf, ",gid=%i", from_kgid_munged(&init_user_ns, vol->gid));
	if (vol->fmask == vol->dmask)
		seq_printf(sf, ",umask=0%o", vol->fmask);
	else {
		seq_printf(sf, ",fmask=0%o", vol->fmask);
		seq_printf(sf, ",dmask=0%o", vol->dmask);
	}
	seq_printf(sf, ",iocharset=%s", vol->nls_map->charset);
	if (NVolCaseSensitive(vol))
		seq_puts(sf, ",case_sensitive");
	else
		seq_puts(sf, ",nocase");
	if (NVolShowSystemFiles(vol))
		seq_puts(sf, ",show_sys_files,showmeta");
	for (i = 0; on_errors_arr[i].val; i++) {
		if (on_errors_arr[i].val == vol->on_errors)
			seq_printf(sf, ",errors=%s", on_errors_arr[i].str);
	}
	seq_printf(sf, ",mft_zone_multiplier=%i", vol->mft_zone_multiplier);
	if (NVolSysImmutable(vol))
		seq_puts(sf, ",sys_immutable");
	if (!NVolShowHiddenFiles(vol))
		seq_puts(sf, ",nohidden");
	if (NVolHideDotFiles(vol))
		seq_puts(sf, ",hide_dot_files");
	if (NVolCheckWindowsNames(vol))
		seq_puts(sf, ",windows_names");
	if (NVolDiscard(vol))
		seq_puts(sf, ",discard");
	if (NVolDisableSparse(vol))
		seq_puts(sf, ",disable_sparse");
	if (vol->sb->s_flags & SB_POSIXACL)
		seq_puts(sf, ",acl");
	return 0;
}

int ntfs_extend_initialized_size(struct inode *vi, const loff_t offset,
				 const loff_t new_size, bool bsync)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	loff_t old_init_size;
	unsigned long flags;
	int err;

	read_lock_irqsave(&ni->size_lock, flags);
	old_init_size = ni->initialized_size;
	read_unlock_irqrestore(&ni->size_lock, flags);

	if (!NInoNonResident(ni))
		return -EINVAL;
	if (old_init_size >= new_size)
		return 0;

	err = ntfs_attr_map_whole_runlist(ni);
	if (err)
		return err;

	if (!NInoCompressed(ni) && old_init_size < offset) {
		err = iomap_zero_range(vi, old_init_size,
				       offset - old_init_size,
				       NULL, &ntfs_seek_iomap_ops,
				       &ntfs_iomap_folio_ops, NULL);
		if (err)
			return err;
		if (bsync)
			err = filemap_write_and_wait_range(vi->i_mapping,
							   old_init_size,
							   offset - 1);
	}


	mutex_lock(&ni->mrec_lock);
	err = ntfs_attr_set_initialized_size(ni, new_size);
	mutex_unlock(&ni->mrec_lock);
	if (err)
		truncate_setsize(vi, old_init_size);
	return err;
}

int ntfs_truncate_vfs(struct inode *vi, loff_t new_size, loff_t i_size)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	int err;

	mutex_lock(&ni->mrec_lock);
	err = __ntfs_attr_truncate_vfs(ni, new_size, i_size);
	mutex_unlock(&ni->mrec_lock);
	if (err < 0)
		return err;

	inode_set_mtime_to_ts(vi, inode_set_ctime_current(vi));
	return 0;
}

/*
 * ntfs_inode_sync_standard_information - update standard information attribute
 * @vi:	inode to update standard information
 * @m:	mft record
 *
 * Return 0 on success or -errno on error.
 */
static int ntfs_inode_sync_standard_information(struct inode *vi, struct mft_record *m)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_attr_search_ctx *ctx;
	struct standard_information *si;
	__le64 nt;
	int err = 0;
	bool modified = false;

	/* Update the access times in the standard information attribute. */
	ctx = ntfs_attr_get_search_ctx(ni, m);
	if (unlikely(!ctx))
		return -ENOMEM;
	err = ntfs_attr_lookup(AT_STANDARD_INFORMATION, NULL, 0,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		ntfs_attr_put_search_ctx(ctx);
		return err;
	}
	si = (struct standard_information *)((u8 *)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset));
	if (si->file_attributes != ni->flags) {
		si->file_attributes = ni->flags;
		modified = true;
	}

	/* Update the creation times if they have changed. */
	nt = utc2ntfs(ni->i_crtime);
	if (si->creation_time != nt) {
		ntfs_debug("Updating creation time for inode 0x%llx: old = 0x%llx, new = 0x%llx",
				ni->mft_no, le64_to_cpu(si->creation_time),
				le64_to_cpu(nt));
		si->creation_time = nt;
		modified = true;
	}

	/* Update the access times if they have changed. */
	nt = utc2ntfs(inode_get_mtime(vi));
	if (si->last_data_change_time != nt) {
		ntfs_debug("Updating mtime for inode 0x%llx: old = 0x%llx, new = 0x%llx",
				ni->mft_no, le64_to_cpu(si->last_data_change_time),
				le64_to_cpu(nt));
		si->last_data_change_time = nt;
		modified = true;
	}

	nt = utc2ntfs(inode_get_ctime(vi));
	if (si->last_mft_change_time != nt) {
		ntfs_debug("Updating ctime for inode 0x%llx: old = 0x%llx, new = 0x%llx",
				ni->mft_no, le64_to_cpu(si->last_mft_change_time),
				le64_to_cpu(nt));
		si->last_mft_change_time = nt;
		modified = true;
	}
	nt = utc2ntfs(inode_get_atime(vi));
	if (si->last_access_time != nt) {
		ntfs_debug("Updating atime for inode 0x%llx: old = 0x%llx, new = 0x%llx",
				ni->mft_no,
				le64_to_cpu(si->last_access_time),
				le64_to_cpu(nt));
		si->last_access_time = nt;
		modified = true;
	}

	/*
	 * If we just modified the standard information attribute we need to
	 * mark the mft record it is in dirty.  We do this manually so that
	 * mark_inode_dirty() is not called which would redirty the inode and
	 * hence result in an infinite loop of trying to write the inode.
	 * There is no need to mark the base inode nor the base mft record
	 * dirty, since we are going to write this mft record below in any case
	 * and the base mft record may actually not have been modified so it
	 * might not need to be written out.
	 * NOTE: It is not a problem when the inode for $MFT itself is being
	 * written out as ntfs_mft_mark_dirty() will only set I_DIRTY_PAGES
	 * on the $MFT inode and hence ntfs_write_inode() will not be
	 * re-invoked because of it which in turn is ok since the dirtied mft
	 * record will be cleaned and written out to disk below, i.e. before
	 * this function returns.
	 */
	if (modified)
		NInoSetDirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);

	return err;
}

/*
 * ntfs_inode_sync_filename - update FILE_NAME attributes
 * @ni:	ntfs inode to update FILE_NAME attributes
 *
 * Update all FILE_NAME attributes for inode @ni in the index.
 *
 * Return 0 on success or error.
 */
int ntfs_inode_sync_filename(struct ntfs_inode *ni)
{
	struct inode *index_vi;
	struct super_block *sb = VFS_I(ni)->i_sb;
	struct ntfs_attr_search_ctx *ctx = NULL;
	struct ntfs_index_context *ictx;
	struct ntfs_inode *index_ni;
	struct file_name_attr *fn;
	struct file_name_attr *fnx;
	struct reparse_point *rpp;
	__le32 reparse_tag;
	int err = 0;
	unsigned long flags;

	ntfs_debug("Entering for inode %llu\n", ni->mft_no);

	ctx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!ctx)
		return -ENOMEM;

	/* Collect the reparse tag, if any */
	reparse_tag = cpu_to_le32(0);
	if (ni->flags & FILE_ATTR_REPARSE_POINT) {
		if (!ntfs_attr_lookup(AT_REPARSE_POINT, NULL,
					0, CASE_SENSITIVE, 0, NULL, 0, ctx)) {
			rpp = (struct reparse_point *)((u8 *)ctx->attr +
					le16_to_cpu(ctx->attr->data.resident.value_offset));
			reparse_tag = rpp->reparse_tag;
		}
		ntfs_attr_reinit_search_ctx(ctx);
	}

	/* Walk through all FILE_NAME attributes and update them. */
	while (!(err = ntfs_attr_lookup(AT_FILE_NAME, NULL, 0, 0, 0, NULL, 0, ctx))) {
		fn = (struct file_name_attr *)((u8 *)ctx->attr +
				le16_to_cpu(ctx->attr->data.resident.value_offset));
		if (MREF_LE(fn->parent_directory) == ni->mft_no)
			continue;

		index_vi = ntfs_iget(sb, MREF_LE(fn->parent_directory));
		if (IS_ERR(index_vi)) {
			ntfs_error(sb, "Failed to open inode %lld with index",
					(long long)MREF_LE(fn->parent_directory));
			continue;
		}

		index_ni = NTFS_I(index_vi);

		mutex_lock_nested(&index_ni->mrec_lock, NTFS_INODE_MUTEX_PARENT);
		if (NInoBeingDeleted(ni)) {
			iput(index_vi);
			mutex_unlock(&index_ni->mrec_lock);
			continue;
		}

		ictx = ntfs_index_ctx_get(index_ni, I30, 4);
		if (!ictx) {
			ntfs_error(sb, "Failed to get index ctx, inode %llu",
					index_ni->mft_no);
			iput(index_vi);
			mutex_unlock(&index_ni->mrec_lock);
			continue;
		}

		err = ntfs_index_lookup(fn, sizeof(struct file_name_attr), ictx);
		if (err) {
			ntfs_debug("Index lookup failed, inode %llu",
					index_ni->mft_no);
			ntfs_index_ctx_put(ictx);
			iput(index_vi);
			mutex_unlock(&index_ni->mrec_lock);
			continue;
		}
		/* Update flags and file size. */
		fnx = (struct file_name_attr *)ictx->data;
		fnx->file_attributes =
			(fnx->file_attributes & ~FILE_ATTR_VALID_FLAGS) |
			(ni->flags & FILE_ATTR_VALID_FLAGS);
		if (ctx->mrec->flags & MFT_RECORD_IS_DIRECTORY)
			fnx->data_size = fnx->allocated_size = 0;
		else {
			read_lock_irqsave(&ni->size_lock, flags);
			if (NInoSparse(ni) || NInoCompressed(ni))
				fnx->allocated_size = cpu_to_le64(ni->itype.compressed.size);
			else
				fnx->allocated_size = cpu_to_le64(ni->allocated_size);
			fnx->data_size = cpu_to_le64(ni->data_size);

			/*
			 * The file name record has also to be fixed if some
			 * attribute update implied the unnamed data to be
			 * made non-resident
			 */
			fn->allocated_size = fnx->allocated_size;
			fn->data_size = fnx->data_size;
			read_unlock_irqrestore(&ni->size_lock, flags);
		}

		/* update or clear the reparse tag in the index */
		fnx->type.rp.reparse_point_tag = reparse_tag;
		fnx->creation_time = fn->creation_time;
		fnx->last_data_change_time = fn->last_data_change_time;
		fnx->last_mft_change_time = fn->last_mft_change_time;
		fnx->last_access_time = fn->last_access_time;
		ntfs_index_entry_mark_dirty(ictx);
		ntfs_icx_ib_sync_write(ictx);
		NInoSetDirty(ctx->ntfs_ino);
		ntfs_index_ctx_put(ictx);
		mutex_unlock(&index_ni->mrec_lock);
		iput(index_vi);
	}
	/* Check for real error occurred. */
	if (err != -ENOENT) {
		ntfs_error(sb, "Attribute lookup failed, err : %d, inode %llu", err,
				ni->mft_no);
	} else
		err = 0;

	ntfs_attr_put_search_ctx(ctx);
	return err;
}

int ntfs_get_block_mft_record(struct ntfs_inode *mft_ni, struct ntfs_inode *ni)
{
	s64 vcn;
	struct runlist_element *rl;

	if (ni->mft_lcn[0] != LCN_RL_NOT_MAPPED)
		return 0;

	vcn = (s64)ni->mft_no << mft_ni->vol->mft_record_size_bits >>
	      mft_ni->vol->cluster_size_bits;

	rl = mft_ni->runlist.rl;
	if (!rl) {
		ntfs_error(mft_ni->vol->sb, "$MFT runlist is not present");
		return -EIO;
	}

	/* Seek to element containing target vcn. */
	while (rl->length && rl[1].vcn <= vcn)
		rl++;
	ni->mft_lcn[0] = ntfs_rl_vcn_to_lcn(rl, vcn);
	ni->mft_lcn_count = 1;

	if (mft_ni->vol->cluster_size < mft_ni->vol->mft_record_size &&
	    (rl->length - (vcn - rl->vcn)) <= 1) {
		rl++;
		ni->mft_lcn[1] = ntfs_rl_vcn_to_lcn(rl, vcn + 1);
		ni->mft_lcn_count++;
	}
	return 0;
}

/*
 * __ntfs_write_inode - write out a dirty inode
 * @vi:		inode to write out
 * @sync:	if true, write out synchronously
 *
 * Write out a dirty inode to disk including any extent inodes if present.
 *
 * If @sync is true, commit the inode to disk and wait for io completion.  This
 * is done using write_mft_record().
 *
 * If @sync is false, just schedule the write to happen but do not wait for i/o
 * completion.
 *
 * Return 0 on success and -errno on error.
 */
int __ntfs_write_inode(struct inode *vi, int sync)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_inode *mft_ni = NTFS_I(ni->vol->mft_ino);
	struct mft_record *m;
	int err = 0;
	bool need_iput = false;

	ntfs_debug("Entering for %sinode 0x%llx.", NInoAttr(ni) ? "attr " : "",
			ni->mft_no);

	if (NVolShutdown(ni->vol))
		return -EIO;

	/*
	 * Dirty attribute inodes are written via their real inodes so just
	 * clean them here.  Access time updates are taken care off when the
	 * real inode is written.
	 */
	if (NInoAttr(ni) || ni->nr_extents == -1) {
		NInoClearDirty(ni);
		ntfs_debug("Done.");
		return 0;
	}

	/* igrab prevents vi from being evicted while mrec_lock is hold. */
	if (igrab(vi) != NULL)
		need_iput = true;

	mutex_lock_nested(&ni->mrec_lock, NTFS_INODE_MUTEX_NORMAL);
	/* Map, pin, and lock the mft record belonging to the inode. */
	m = map_mft_record(ni);
	if (IS_ERR(m)) {
		mutex_unlock(&ni->mrec_lock);
		err = PTR_ERR(m);
		goto err_out;
	}

	if (NInoNonResident(ni) && NInoRunlistDirty(ni)) {
		down_write(&ni->runlist.lock);
		err = ntfs_attr_update_mapping_pairs(ni, 0);
		if (!err)
			NInoClearRunlistDirty(ni);
		up_write(&ni->runlist.lock);
	}

	err = ntfs_inode_sync_standard_information(vi, m);
	if (err)
		goto unm_err_out;

	/*
	 * when being umounted and inodes are evicted, write_inode()
	 * is called with all inodes being marked with I_FREEING.
	 * then ntfs_inode_sync_filename() waits infinitly because
	 * of ntfs_iget. This situation happens only where sync_filesysem()
	 * from umount fails because of a disk unplug and etc.
	 * the absent of SB_ACTIVE means umounting.
	 */
	if ((vi->i_sb->s_flags & SB_ACTIVE) && NInoTestClearFileNameDirty(ni))
		ntfs_inode_sync_filename(ni);

	/* Now the access times are updated, write the base mft record. */
	if (NInoDirty(ni)) {
		down_read(&mft_ni->runlist.lock);
		err = ntfs_get_block_mft_record(mft_ni, ni);
		up_read(&mft_ni->runlist.lock);
		if (err)
			goto unm_err_out;

		err = write_mft_record(ni, m, sync);
		if (err)
			ntfs_error(vi->i_sb, "write_mft_record failed, err : %d\n", err);
	}
	unmap_mft_record(ni);

	/* Map any unmapped extent mft records with LCNs. */
	down_read(&mft_ni->runlist.lock);
	mutex_lock(&ni->extent_lock);
	if (ni->nr_extents > 0) {
		int i;

		for (i = 0; i < ni->nr_extents; i++) {
			err = ntfs_get_block_mft_record(mft_ni,
						   ni->ext.extent_ntfs_inos[i]);
			if (err) {
				mutex_unlock(&ni->extent_lock);
				up_read(&mft_ni->runlist.lock);
				mutex_unlock(&ni->mrec_lock);
				goto err_out;
			}
		}
	}
	mutex_unlock(&ni->extent_lock);
	up_read(&mft_ni->runlist.lock);

	/* Write all attached extent mft records. */
	mutex_lock(&ni->extent_lock);
	if (ni->nr_extents > 0) {
		struct ntfs_inode **extent_nis = ni->ext.extent_ntfs_inos;
		int i;

		ntfs_debug("Writing %i extent inodes.", ni->nr_extents);
		for (i = 0; i < ni->nr_extents; i++) {
			struct ntfs_inode *tni = extent_nis[i];

			if (NInoDirty(tni)) {
				struct mft_record *tm;
				int ret;

				mutex_lock(&tni->mrec_lock);
				tm = map_mft_record(tni);
				if (IS_ERR(tm)) {
					mutex_unlock(&tni->mrec_lock);
					if (!err || err == -ENOMEM)
						err = PTR_ERR(tm);
					continue;
				}

				ret = write_mft_record(tni, tm, sync);
				unmap_mft_record(tni);
				mutex_unlock(&tni->mrec_lock);

				if (unlikely(ret)) {
					if (!err || err == -ENOMEM)
						err = ret;
				}
			}
		}
	}
	mutex_unlock(&ni->extent_lock);
	mutex_unlock(&ni->mrec_lock);

	if (unlikely(err))
		goto err_out;
	if (need_iput)
		iput(vi);
	ntfs_debug("Done.");
	return 0;
unm_err_out:
	unmap_mft_record(ni);
	mutex_unlock(&ni->mrec_lock);
err_out:
	if (err == -ENOMEM)
		mark_inode_dirty(vi);
	else {
		ntfs_error(vi->i_sb, "Failed (error %i):  Run chkdsk.", -err);
		NVolSetErrors(ni->vol);
	}
	if (need_iput)
		iput(vi);
	return err;
}

/*
 * ntfs_extent_inode_open - load an extent inode and attach it to its base
 * @base_ni:	base ntfs inode
 * @mref:	mft reference of the extent inode to load (in little endian)
 *
 * First check if the extent inode @mref is already attached to the base ntfs
 * inode @base_ni, and if so, return a pointer to the attached extent inode.
 *
 * If the extent inode is not already attached to the base inode, allocate an
 * ntfs_inode structure and initialize it for the given inode @mref. @mref
 * specifies the inode number / mft record to read, including the sequence
 * number, which can be 0 if no sequence number checking is to be performed.
 *
 * Then, allocate a buffer for the mft record, read the mft record from the
 * volume @base_ni->vol, and attach it to the ntfs_inode structure (->mrec).
 * The mft record is mst deprotected and sanity checked for validity and we
 * abort if deprotection or checks fail.
 *
 * Finally attach the ntfs inode to its base inode @base_ni and return a
 * pointer to the ntfs_inode structure on success or NULL on error, with errno
 * set to the error code.
 *
 * Note, extent inodes are never closed directly. They are automatically
 * disposed off by the closing of the base inode.
 */
static struct ntfs_inode *ntfs_extent_inode_open(struct ntfs_inode *base_ni,
		const __le64 mref)
{
	u64 mft_no = MREF_LE(mref);
	struct ntfs_inode *ni = NULL;
	struct ntfs_inode **extent_nis;
	int i;
	struct mft_record *ni_mrec;
	struct super_block *sb;

	if (!base_ni)
		return NULL;

	sb = base_ni->vol->sb;
	ntfs_debug("Opening extent inode %llu (base mft record %llu).\n",
			mft_no, base_ni->mft_no);

	/* Is the extent inode already open and attached to the base inode? */
	if (base_ni->nr_extents > 0) {
		extent_nis = base_ni->ext.extent_ntfs_inos;
		for (i = 0; i < base_ni->nr_extents; i++) {
			u16 seq_no;

			ni = extent_nis[i];
			if (mft_no != ni->mft_no)
				continue;
			ni_mrec = map_mft_record(ni);
			if (IS_ERR(ni_mrec)) {
				ntfs_error(sb, "failed to map mft record for %llu",
						ni->mft_no);
				goto out;
			}
			/* Verify the sequence number if given. */
			seq_no = MSEQNO_LE(mref);
			if (seq_no &&
			    seq_no != le16_to_cpu(ni_mrec->sequence_number)) {
				ntfs_error(sb, "Found stale extent mft reference mft=%llu",
						ni->mft_no);
				unmap_mft_record(ni);
				goto out;
			}
			unmap_mft_record(ni);
			goto out;
		}
	}
	/* Wasn't there, we need to load the extent inode. */
	ni = ntfs_new_extent_inode(base_ni->vol->sb, mft_no);
	if (!ni)
		goto out;

	ni->seq_no = (u16)MSEQNO_LE(mref);
	ni->nr_extents = -1;
	ni->ext.base_ntfs_ino = base_ni;
	/* Attach extent inode to base inode, reallocating memory if needed. */
	if (!(base_ni->nr_extents & 3)) {
		i = (base_ni->nr_extents + 4) * sizeof(struct ntfs_inode *);

		extent_nis = kvzalloc(i, GFP_NOFS);
		if (!extent_nis)
			goto err_out;
		if (base_ni->nr_extents) {
			memcpy(extent_nis, base_ni->ext.extent_ntfs_inos,
					i - 4 * sizeof(struct ntfs_inode *));
			kvfree(base_ni->ext.extent_ntfs_inos);
		}
		base_ni->ext.extent_ntfs_inos = extent_nis;
	}
	base_ni->ext.extent_ntfs_inos[base_ni->nr_extents++] = ni;

out:
	ntfs_debug("\n");
	return ni;
err_out:
	ntfs_destroy_ext_inode(ni);
	ni = NULL;
	goto out;
}

/*
 * ntfs_inode_attach_all_extents - attach all extents for target inode
 * @ni:		opened ntfs inode for which perform attach
 *
 * Return 0 on success and error.
 */
int ntfs_inode_attach_all_extents(struct ntfs_inode *ni)
{
	struct attr_list_entry *ale;
	u64 prev_attached = 0;

	if (!ni) {
		ntfs_debug("Invalid arguments.\n");
		return -EINVAL;
	}

	if (NInoAttr(ni))
		ni = ni->ext.base_ntfs_ino;

	ntfs_debug("Entering for inode 0x%llx.\n", ni->mft_no);

	/* Inode haven't got attribute list, thus nothing to attach. */
	if (!NInoAttrList(ni))
		return 0;

	if (!ni->attr_list) {
		ntfs_debug("Corrupt in-memory struct.\n");
		return -EINVAL;
	}

	/* Walk through attribute list and attach all extents. */
	ale = (struct attr_list_entry *)ni->attr_list;
	while ((u8 *)ale < ni->attr_list + ni->attr_list_size) {
		if (ni->mft_no != MREF_LE(ale->mft_reference) &&
				prev_attached != MREF_LE(ale->mft_reference)) {
			if (!ntfs_extent_inode_open(ni, ale->mft_reference)) {
				ntfs_debug("Couldn't attach extent inode.\n");
				return -1;
			}
			prev_attached = MREF_LE(ale->mft_reference);
		}
		ale = (struct attr_list_entry *)((u8 *)ale + le16_to_cpu(ale->length));
	}
	return 0;
}

/*
 * ntfs_inode_add_attrlist - add attribute list to inode and fill it
 * @ni: opened ntfs inode to which add attribute list
 *
 * Return 0 on success or error.
 */
int ntfs_inode_add_attrlist(struct ntfs_inode *ni)
{
	int err;
	struct ntfs_attr_search_ctx *ctx;
	u8 *al = NULL, *aln;
	int al_len = 0;
	struct attr_list_entry *ale = NULL;
	struct mft_record *ni_mrec;
	u32 attr_al_len;

	if (!ni)
		return -EINVAL;

	ntfs_debug("inode %llu\n", ni->mft_no);

	if (NInoAttrList(ni) || ni->nr_extents) {
		ntfs_error(ni->vol->sb, "Inode already has attribute list");
		return -EEXIST;
	}

	ni_mrec = map_mft_record(ni);
	if (IS_ERR(ni_mrec))
		return -EIO;

	/* Form attribute list. */
	ctx = ntfs_attr_get_search_ctx(ni, ni_mrec);
	if (!ctx) {
		err = -ENOMEM;
		goto err_out;
	}

	/* Walk through all attributes. */
	while (!(err = ntfs_attr_lookup(AT_UNUSED, NULL, 0, 0, 0, NULL, 0, ctx))) {
		int ale_size;

		if (ctx->attr->type == AT_ATTRIBUTE_LIST) {
			err = -EIO;
			ntfs_error(ni->vol->sb, "Attribute list already present");
			goto put_err_out;
		}

		ale_size = (sizeof(struct attr_list_entry) + sizeof(__le16) *
				ctx->attr->name_length + 7) & ~7;
		al_len += ale_size;

		aln = kvrealloc(al, al_len, GFP_NOFS);
		if (!aln) {
			err = -ENOMEM;
			ntfs_error(ni->vol->sb, "Failed to realloc %d bytes", al_len);
			goto put_err_out;
		}
		ale = (struct attr_list_entry *)(aln + ((u8 *)ale - al));
		al = aln;

		memset(ale, 0, ale_size);

		/* Add attribute to attribute list. */
		ale->type = ctx->attr->type;
		ale->length = cpu_to_le16((sizeof(struct attr_list_entry) +
					sizeof(__le16) * ctx->attr->name_length + 7) & ~7);
		ale->name_length = ctx->attr->name_length;
		ale->name_offset = (u8 *)ale->name - (u8 *)ale;
		if (ctx->attr->non_resident)
			ale->lowest_vcn =
				ctx->attr->data.non_resident.lowest_vcn;
		else
			ale->lowest_vcn = 0;
		ale->mft_reference = MK_LE_MREF(ni->mft_no,
				le16_to_cpu(ni_mrec->sequence_number));
		ale->instance = ctx->attr->instance;
		memcpy(ale->name, (u8 *)ctx->attr +
				le16_to_cpu(ctx->attr->name_offset),
				ctx->attr->name_length * sizeof(__le16));
		ale = (struct attr_list_entry *)(al + al_len);
	}

	/* Check for real error occurred. */
	if (err != -ENOENT) {
		ntfs_error(ni->vol->sb, "%s: Attribute lookup failed, inode %llu",
				__func__, ni->mft_no);
		goto put_err_out;
	}

	/* Set in-memory attribute list. */
	ni->attr_list = al;
	ni->attr_list_size = al_len;
	NInoSetAttrList(ni);

	attr_al_len = offsetof(struct attr_record, data.resident.reserved) + 1 +
		((al_len + 7) & ~7);
	/* Free space if there is not enough it for $ATTRIBUTE_LIST. */
	if (le32_to_cpu(ni_mrec->bytes_allocated) -
			le32_to_cpu(ni_mrec->bytes_in_use) < attr_al_len) {
		if (ntfs_inode_free_space(ni, (int)attr_al_len)) {
			/* Failed to free space. */
			err = -ENOSPC;
			ntfs_error(ni->vol->sb, "Failed to free space for attrlist");
			goto rollback;
		}
	}

	/* Add $ATTRIBUTE_LIST to mft record. */
	err = ntfs_resident_attr_record_add(ni, AT_ATTRIBUTE_LIST, AT_UNNAMED, 0,
					    NULL, al_len, 0);
	if (err < 0) {
		ntfs_error(ni->vol->sb, "Couldn't add $ATTRIBUTE_LIST to MFT");
		goto rollback;
	}

	err = ntfs_attrlist_update(ni);
	if (err < 0)
		goto remove_attrlist_record;

	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(ni);
	return 0;

remove_attrlist_record:
	/* Prevent ntfs_attr_recorm_rm from freeing attribute list. */
	ni->attr_list = NULL;
	NInoClearAttrList(ni);
	/* Remove $ATTRIBUTE_LIST record. */
	ntfs_attr_reinit_search_ctx(ctx);
	if (!ntfs_attr_lookup(AT_ATTRIBUTE_LIST, NULL, 0,
				CASE_SENSITIVE, 0, NULL, 0, ctx)) {
		if (ntfs_attr_record_rm(ctx))
			ntfs_error(ni->vol->sb, "Rollback failed to remove attrlist");
	} else {
		ntfs_error(ni->vol->sb, "Rollback failed to find attrlist");
	}

	/* Setup back in-memory runlist. */
	ni->attr_list = al;
	ni->attr_list_size = al_len;
	NInoSetAttrList(ni);
rollback:
	/*
	 * Scan attribute list for attributes that placed not in the base MFT
	 * record and move them to it.
	 */
	ntfs_attr_reinit_search_ctx(ctx);
	ale = (struct attr_list_entry *)al;
	while ((u8 *)ale < al + al_len) {
		if (MREF_LE(ale->mft_reference) != ni->mft_no) {
			if (!ntfs_attr_lookup(ale->type, ale->name,
						ale->name_length,
						CASE_SENSITIVE,
						le64_to_cpu(ale->lowest_vcn),
						NULL, 0, ctx)) {
				if (ntfs_attr_record_move_to(ctx, ni))
					ntfs_error(ni->vol->sb,
							"Rollback failed to move attribute");
			} else {
				ntfs_error(ni->vol->sb, "Rollback failed to find attr");
			}
			ntfs_attr_reinit_search_ctx(ctx);
		}
		ale = (struct attr_list_entry *)((u8 *)ale + le16_to_cpu(ale->length));
	}

	/* Remove in-memory attribute list. */
	ni->attr_list = NULL;
	ni->attr_list_size = 0;
	NInoClearAttrList(ni);
	NInoClearAttrListDirty(ni);
put_err_out:
	ntfs_attr_put_search_ctx(ctx);
err_out:
	kvfree(al);
	unmap_mft_record(ni);
	return err;
}

/*
 * ntfs_inode_close - close an ntfs inode and free all associated memory
 * @ni:		ntfs inode to close
 *
 * Make sure the ntfs inode @ni is clean.
 *
 * If the ntfs inode @ni is a base inode, close all associated extent inodes,
 * then deallocate all memory attached to it, and finally free the ntfs inode
 * structure itself.
 *
 * If it is an extent inode, we disconnect it from its base inode before we
 * destroy it.
 *
 * It is OK to pass NULL to this function, it is just noop in this case.
 *
 * Return 0 on success or error.
 */
int ntfs_inode_close(struct ntfs_inode *ni)
{
	int err = -1;
	struct ntfs_inode **tmp_nis;
	struct ntfs_inode *base_ni;
	s32 i;

	if (!ni)
		return 0;

	ntfs_debug("Entering for inode %llu\n", ni->mft_no);

	/* Is this a base inode with mapped extent inodes? */
	/*
	 * If the inode is an extent inode, disconnect it from the
	 * base inode before destroying it.
	 */
	base_ni = ni->ext.base_ntfs_ino;
	for (i = 0; i < base_ni->nr_extents; ++i) {
		tmp_nis = base_ni->ext.extent_ntfs_inos;
		if (tmp_nis[i] != ni)
			continue;
		/* Found it. Disconnect. */
		memmove(tmp_nis + i, tmp_nis + i + 1,
				(base_ni->nr_extents - i - 1) *
				sizeof(struct ntfs_inode *));
		/* Buffer should be for multiple of four extents. */
		if ((--base_ni->nr_extents) & 3)
			break;
		/*
		 * ElectricFence is unhappy with realloc(x,0) as free(x)
		 * thus we explicitly separate these two cases.
		 */
		if (base_ni->nr_extents) {
			/* Resize the memory buffer. */
			tmp_nis = kvrealloc(tmp_nis, base_ni->nr_extents *
					sizeof(struct ntfs_inode *), GFP_NOFS);
			/* Ignore errors, they don't really matter. */
			if (tmp_nis)
				base_ni->ext.extent_ntfs_inos = tmp_nis;
		} else if (tmp_nis) {
			kvfree(tmp_nis);
			base_ni->ext.extent_ntfs_inos = NULL;
		}
		break;
	}

	if (NInoDirty(ni))
		ntfs_error(ni->vol->sb, "Releasing dirty inode %llu!\n",
				ni->mft_no);
	if (NInoAttrList(ni) && ni->attr_list)
		kvfree(ni->attr_list);
	ntfs_destroy_ext_inode(ni);
	err = 0;
	ntfs_debug("\n");
	return err;
}

void ntfs_destroy_ext_inode(struct ntfs_inode *ni)
{
	ntfs_debug("Entering.");
	if (ni == NULL)
		return;

	ntfs_attr_close(ni);

	if (NInoDirty(ni))
		ntfs_error(ni->vol->sb, "Releasing dirty ext inode %llu!\n",
				ni->mft_no);
	if (NInoAttrList(ni) && ni->attr_list)
		kvfree(ni->attr_list);
	kfree(ni->mrec);
	kmem_cache_free(ntfs_inode_cache, ni);
}

static struct ntfs_inode *ntfs_inode_base(struct ntfs_inode *ni)
{
	if (ni->nr_extents == -1)
		return ni->ext.base_ntfs_ino;
	return ni;
}

static int ntfs_attr_position(__le32 type, struct ntfs_attr_search_ctx *ctx)
{
	int err;

	err = ntfs_attr_lookup(type, NULL, 0, CASE_SENSITIVE, 0, NULL,
				0, ctx);
	if (err) {
		__le32 atype;

		if (err != -ENOENT)
			return err;

		atype = ctx->attr->type;
		if (atype == AT_END)
			return -ENOSPC;

		/*
		 * if ntfs_external_attr_lookup return -ENOENT, ctx->al_entry
		 * could point to an attribute in an extent mft record, but
		 * ctx->attr and ctx->ntfs_ino always points to an attibute in
		 * a base mft record.
		 */
		if (ctx->al_entry &&
		    MREF_LE(ctx->al_entry->mft_reference) != ctx->ntfs_ino->mft_no) {
			ntfs_attr_reinit_search_ctx(ctx);
			err = ntfs_attr_lookup(atype, NULL, 0, CASE_SENSITIVE, 0, NULL,
					       0, ctx);
			if (err)
				return err;
		}
	}
	return 0;
}

/*
 * ntfs_inode_free_space - free space in the MFT record of inode
 * @ni:		ntfs inode in which MFT record free space
 * @size:	amount of space needed to free
 *
 * Return 0 on success or error.
 */
int ntfs_inode_free_space(struct ntfs_inode *ni, int size)
{
	struct ntfs_attr_search_ctx *ctx;
	int freed, err;
	struct mft_record *ni_mrec;
	struct super_block *sb;

	if (!ni || size < 0)
		return -EINVAL;
	ntfs_debug("Entering for inode %llu, size %d\n", ni->mft_no, size);

	sb = ni->vol->sb;
	ni_mrec = map_mft_record(ni);
	if (IS_ERR(ni_mrec))
		return -EIO;

	freed = (le32_to_cpu(ni_mrec->bytes_allocated) -
			le32_to_cpu(ni_mrec->bytes_in_use));

	unmap_mft_record(ni);

	if (size <= freed)
		return 0;

	ctx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!ctx) {
		ntfs_error(sb, "%s, Failed to get search context", __func__);
		return -ENOMEM;
	}

	/*
	 * Chkdsk complain if $STANDARD_INFORMATION is not in the base MFT
	 * record.
	 *
	 * Also we can't move $ATTRIBUTE_LIST from base MFT_RECORD, so position
	 * search context on first attribute after $STANDARD_INFORMATION and
	 * $ATTRIBUTE_LIST.
	 *
	 * Why we reposition instead of simply skip this attributes during
	 * enumeration? Because in case we have got only in-memory attribute
	 * list ntfs_attr_lookup will fail when it will try to find
	 * $ATTRIBUTE_LIST.
	 */
	err = ntfs_attr_position(AT_FILE_NAME, ctx);
	if (err)
		goto put_err_out;

	while (1) {
		int record_size;

		/*
		 * Check whether attribute is from different MFT record. If so,
		 * find next, because we don't need such.
		 */
		while (ctx->ntfs_ino->mft_no != ni->mft_no) {
retry:
			err = ntfs_attr_lookup(AT_UNUSED, NULL, 0, CASE_SENSITIVE,
						0, NULL, 0, ctx);
			if (err) {
				if (err != -ENOENT)
					ntfs_error(sb, "Attr lookup failed #2");
				else if (ctx->attr->type == AT_END)
					err = -ENOSPC;
				else
					err = 0;

				if (err)
					goto put_err_out;
			}
		}

		if (ntfs_inode_base(ctx->ntfs_ino)->mft_no == FILE_MFT &&
				ctx->attr->type == AT_DATA)
			goto retry;

		if (ctx->attr->type == AT_INDEX_ROOT)
			goto retry;

		record_size = le32_to_cpu(ctx->attr->length);

		/* Move away attribute. */
		err = ntfs_attr_record_move_away(ctx, 0);
		if (err) {
			ntfs_error(sb, "Failed to move out attribute #2");
			break;
		}
		freed += record_size;

		/* Check whether we done. */
		if (size <= freed) {
			ntfs_attr_put_search_ctx(ctx);
			return 0;
		}

		/*
		 * Reposition to first attribute after $STANDARD_INFORMATION and
		 * $ATTRIBUTE_LIST (see comments upwards).
		 */
		ntfs_attr_reinit_search_ctx(ctx);
		err = ntfs_attr_position(AT_FILE_NAME, ctx);
		if (err)
			break;
	}
put_err_out:
	ntfs_attr_put_search_ctx(ctx);
	if (err == -ENOSPC)
		ntfs_debug("No attributes left that can be moved out.\n");
	return err;
}

s64 ntfs_inode_attr_pread(struct inode *vi, s64 pos, s64 count, u8 *buf)
{
	struct address_space *mapping = vi->i_mapping;
	struct folio *folio;
	struct ntfs_inode *ni = NTFS_I(vi);
	s64 isize;
	u32 attr_len, total = 0, offset;
	pgoff_t index;
	int err = 0;

	WARN_ON(!NInoAttr(ni));
	if (!count)
		return 0;

	mutex_lock(&ni->mrec_lock);
	isize = i_size_read(vi);
	if (pos > isize) {
		mutex_unlock(&ni->mrec_lock);
		return -EINVAL;
	}
	if (pos + count > isize)
		count = isize - pos;

	if (!NInoNonResident(ni)) {
		struct ntfs_attr_search_ctx *ctx;
		u8 *attr;

		ctx = ntfs_attr_get_search_ctx(ni->ext.base_ntfs_ino, NULL);
		if (!ctx) {
			ntfs_error(vi->i_sb, "Failed to get attr search ctx");
			err = -ENOMEM;
			mutex_unlock(&ni->mrec_lock);
			goto out;
		}

		err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len, CASE_SENSITIVE,
				       0, NULL, 0, ctx);
		if (err) {
			ntfs_error(vi->i_sb, "Failed to look up attr %#x", ni->type);
			ntfs_attr_put_search_ctx(ctx);
			mutex_unlock(&ni->mrec_lock);
			goto out;
		}

		attr = (u8 *)ctx->attr + le16_to_cpu(ctx->attr->data.resident.value_offset);
		memcpy(buf, (u8 *)attr + pos, count);
		ntfs_attr_put_search_ctx(ctx);
		mutex_unlock(&ni->mrec_lock);
		return count;
	}
	mutex_unlock(&ni->mrec_lock);

	index = pos >> PAGE_SHIFT;
	do {
		/* Update @index and get the next folio. */
		folio = read_mapping_folio(mapping, index, NULL);
		if (IS_ERR(folio))
			break;

		offset = offset_in_folio(folio, pos);
		attr_len = min_t(size_t, (size_t)count, folio_size(folio) - offset);

		folio_lock(folio);
		memcpy_from_folio(buf, folio, offset, attr_len);
		folio_unlock(folio);
		folio_put(folio);

		total += attr_len;
		buf += attr_len;
		pos += attr_len;
		count -= attr_len;
		index++;
	} while (count);
out:
	return err ? (s64)err : total;
}

static inline int ntfs_enlarge_attribute(struct inode *vi, s64 pos, s64 count,
					 struct ntfs_attr_search_ctx *ctx)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	struct super_block *sb = vi->i_sb;
	int ret;

	if (pos + count <= ni->initialized_size)
		return 0;

	if (NInoEncrypted(ni) && NInoNonResident(ni))
		return -EACCES;

	if (NInoCompressed(ni))
		return -EOPNOTSUPP;

	if (pos + count > ni->data_size) {
		if (ntfs_attr_truncate(ni, pos + count)) {
			ntfs_debug("Failed to truncate attribute");
			return -1;
		}

		ntfs_attr_reinit_search_ctx(ctx);
		ret = ntfs_attr_lookup(ni->type,
				       ni->name, ni->name_len, CASE_SENSITIVE,
				       0, NULL, 0, ctx);
		if (ret) {
			ntfs_error(sb, "Failed to look up attr %#x", ni->type);
			return ret;
		}
	}

	if (!NInoNonResident(ni)) {
		if (likely(i_size_read(vi) < ni->data_size))
			i_size_write(vi, ni->data_size);
		return 0;
	}

	if (pos + count > ni->initialized_size) {
		ctx->attr->data.non_resident.initialized_size = cpu_to_le64(pos + count);
		mark_mft_record_dirty(ctx->ntfs_ino);
		ni->initialized_size = pos + count;
		if (i_size_read(vi) < ni->initialized_size)
			i_size_write(vi, ni->initialized_size);
	}
	return 0;
}

static s64 __ntfs_inode_resident_attr_pwrite(struct inode *vi,
					     s64 pos, s64 count, u8 *buf,
					     struct ntfs_attr_search_ctx *ctx)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	struct folio *folio;
	struct address_space *mapping = vi->i_mapping;
	u8 *addr;
	int err = 0;

	WARN_ON(NInoNonResident(ni));
	if (pos + count > PAGE_SIZE) {
		ntfs_error(vi->i_sb, "Out of write into resident attr %#x", ni->type);
		return -EINVAL;
	}

	/* Copy to mft record page */
	addr = (u8 *)ctx->attr + le16_to_cpu(ctx->attr->data.resident.value_offset);
	memcpy(addr + pos, buf, count);
	mark_mft_record_dirty(ctx->ntfs_ino);

	/* Keep the first page clean and uptodate */
	folio = __filemap_get_folio(mapping, 0, FGP_WRITEBEGIN | FGP_NOFS,
				   mapping_gfp_mask(mapping));
	if (IS_ERR(folio)) {
		err = PTR_ERR(folio);
		ntfs_error(vi->i_sb, "Failed to read a page 0 for attr %#x: %d",
			   ni->type, err);
		goto out;
	}
	if (!folio_test_uptodate(folio))
		folio_fill_tail(folio, 0, addr,
				le32_to_cpu(ctx->attr->data.resident.value_length));
	else
		memcpy_to_folio(folio, offset_in_folio(folio, pos), buf, count);
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	folio_put(folio);
out:
	return err ? err : count;
}

static s64 __ntfs_inode_non_resident_attr_pwrite(struct inode *vi,
						 s64 pos, s64 count, u8 *buf,
						 struct ntfs_attr_search_ctx *ctx,
						 bool sync)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	struct address_space *mapping = vi->i_mapping;
	struct folio *folio;
	pgoff_t index;
	unsigned long offset, length;
	size_t attr_len;
	s64 ret = 0, written = 0;

	WARN_ON(!NInoNonResident(ni));

	index = pos >> PAGE_SHIFT;
	while (count) {
		if (count == PAGE_SIZE) {
			folio = __filemap_get_folio(vi->i_mapping, index,
					FGP_CREAT | FGP_LOCK,
					mapping_gfp_mask(mapping));
			if (IS_ERR(folio)) {
				ret = -ENOMEM;
				break;
			}
		} else {
			folio = read_mapping_folio(mapping, index, NULL);
			if (IS_ERR(folio)) {
				ret = PTR_ERR(folio);
				ntfs_error(vi->i_sb, "Failed to read a page %lu for attr %#x: %ld",
						index, ni->type, PTR_ERR(folio));
				break;
			}

			folio_lock(folio);
		}

		if (count == PAGE_SIZE) {
			offset = 0;
			attr_len = count;
		} else {
			offset = offset_in_folio(folio, pos);
			attr_len = min_t(size_t, (size_t)count, folio_size(folio) - offset);
		}
		memcpy_to_folio(folio, offset, buf, attr_len);

		if (sync) {
			struct ntfs_volume *vol = ni->vol;
			s64 lcn, lcn_count;
			unsigned int lcn_folio_off = 0;
			struct bio *bio;
			u64 rl_length = 0;
			s64 vcn;
			struct runlist_element *rl;

			lcn_count = max_t(s64, 1, ntfs_bytes_to_cluster(vol, attr_len));
			vcn = ntfs_pidx_to_cluster(vol, folio->index);

			do {
				down_write(&ni->runlist.lock);
				rl = ntfs_attr_vcn_to_rl(ni, vcn, &lcn);
				if (IS_ERR(rl)) {
					ret = PTR_ERR(rl);
					up_write(&ni->runlist.lock);
					goto err_unlock_folio;
				}

				rl_length = rl->length - (vcn - rl->vcn);
				if (rl_length < lcn_count) {
					lcn_count -= rl_length;
				} else {
					rl_length = lcn_count;
					lcn_count = 0;
				}
				up_write(&ni->runlist.lock);

				if (vol->cluster_size_bits > PAGE_SHIFT) {
					lcn_folio_off = folio->index << PAGE_SHIFT;
					lcn_folio_off &= vol->cluster_size_mask;
				}

				bio = bio_alloc(vol->sb->s_bdev, 1, REQ_OP_WRITE,
						GFP_NOIO);
				bio->bi_iter.bi_sector =
					ntfs_bytes_to_sector(vol,
							ntfs_cluster_to_bytes(vol, lcn) +
							lcn_folio_off);

				length = min_t(unsigned long,
					       ntfs_cluster_to_bytes(vol, rl_length),
					       folio_size(folio));
				if (!bio_add_folio(bio, folio, length, offset)) {
					ret = -EIO;
					bio_put(bio);
					goto err_unlock_folio;
				}

				submit_bio_wait(bio);
				bio_put(bio);
				vcn += rl_length;
				offset += length;
			} while (lcn_count != 0);

			folio_mark_uptodate(folio);
		} else {
			folio_mark_uptodate(folio);
			folio_mark_dirty(folio);
		}
err_unlock_folio:
		folio_unlock(folio);
		folio_put(folio);

		if (ret)
			break;

		written += attr_len;
		buf += attr_len;
		pos += attr_len;
		count -= attr_len;
		index++;

		cond_resched();
	}

	return ret ? ret : written;
}

s64 ntfs_inode_attr_pwrite(struct inode *vi, s64 pos, s64 count, u8 *buf, bool sync)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_attr_search_ctx *ctx;
	s64 ret;

	WARN_ON(!NInoAttr(ni));

	ctx = ntfs_attr_get_search_ctx(ni->ext.base_ntfs_ino, NULL);
	if (!ctx) {
		ntfs_error(vi->i_sb, "Failed to get attr search ctx");
		return -ENOMEM;
	}

	ret = ntfs_attr_lookup(ni->type, ni->name, ni->name_len, CASE_SENSITIVE,
			       0, NULL, 0, ctx);
	if (ret) {
		ntfs_attr_put_search_ctx(ctx);
		ntfs_error(vi->i_sb, "Failed to look up attr %#x", ni->type);
		return ret;
	}

	mutex_lock(&ni->mrec_lock);
	ret = ntfs_enlarge_attribute(vi, pos, count, ctx);
	mutex_unlock(&ni->mrec_lock);
	if (ret)
		goto out;

	if (NInoNonResident(ni))
		ret = __ntfs_inode_non_resident_attr_pwrite(vi, pos, count, buf, ctx, sync);
	else
		ret = __ntfs_inode_resident_attr_pwrite(vi, pos, count, buf, ctx);
out:
	ntfs_attr_put_search_ctx(ctx);
	return ret;
}

struct folio *ntfs_get_locked_folio(struct address_space *mapping,
		pgoff_t index, pgoff_t end_index, struct file_ra_state *ra)
{
	struct folio *folio;

	folio = filemap_lock_folio(mapping, index);
	if (IS_ERR(folio)) {
		if (PTR_ERR(folio) != -ENOENT)
			return folio;

		page_cache_sync_readahead(mapping, ra, NULL, index,
				end_index - index);
		folio = read_mapping_folio(mapping, index, NULL);
		if (!IS_ERR(folio))
			folio_lock(folio);
	}

	return folio;
}
