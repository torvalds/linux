#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/posix_acl.h>
#include "reiserfs.h"
#include <linux/errno.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/slab.h>
#include <linux/posix_acl_xattr.h>
#include "xattr.h"
#include "acl.h"
#include <asm/uaccess.h>

static int reiserfs_set_acl(struct reiserfs_transaction_handle *th,
			    struct inode *inode, int type,
			    struct posix_acl *acl);

static int
posix_acl_set(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags, int type)
{
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl;
	int error, error2;
	struct reiserfs_transaction_handle th;
	size_t jcreate_blocks;
	if (!reiserfs_posixacl(inode->i_sb))
		return -EOPNOTSUPP;
	if (!inode_owner_or_capable(inode))
		return -EPERM;

	if (value) {
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
		if (IS_ERR(acl)) {
			return PTR_ERR(acl);
		} else if (acl) {
			error = posix_acl_valid(acl);
			if (error)
				goto release_and_out;
		}
	} else
		acl = NULL;

	/* Pessimism: We can't assume that anything from the xattr root up
	 * has been created. */

	jcreate_blocks = reiserfs_xattr_jcreate_nblocks(inode) +
			 reiserfs_xattr_nblocks(inode, size) * 2;

	reiserfs_write_lock(inode->i_sb);
	error = journal_begin(&th, inode->i_sb, jcreate_blocks);
	if (error == 0) {
		error = reiserfs_set_acl(&th, inode, type, acl);
		error2 = journal_end(&th, inode->i_sb, jcreate_blocks);
		if (error2)
			error = error2;
	}
	reiserfs_write_unlock(inode->i_sb);

      release_and_out:
	posix_acl_release(acl);
	return error;
}

static int
posix_acl_get(struct dentry *dentry, const char *name, void *buffer,
		size_t size, int type)
{
	struct posix_acl *acl;
	int error;

	if (!reiserfs_posixacl(dentry->d_sb))
		return -EOPNOTSUPP;

	acl = reiserfs_get_acl(dentry->d_inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;
	error = posix_acl_to_xattr(&init_user_ns, acl, buffer, size);
	posix_acl_release(acl);

	return error;
}

/*
 * Convert from filesystem to in-memory representation.
 */
static struct posix_acl *posix_acl_from_disk(const void *value, size_t size)
{
	const char *end = (char *)value + size;
	int n, count;
	struct posix_acl *acl;

	if (!value)
		return NULL;
	if (size < sizeof(reiserfs_acl_header))
		return ERR_PTR(-EINVAL);
	if (((reiserfs_acl_header *) value)->a_version !=
	    cpu_to_le32(REISERFS_ACL_VERSION))
		return ERR_PTR(-EINVAL);
	value = (char *)value + sizeof(reiserfs_acl_header);
	count = reiserfs_acl_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;
	acl = posix_acl_alloc(count, GFP_NOFS);
	if (!acl)
		return ERR_PTR(-ENOMEM);
	for (n = 0; n < count; n++) {
		reiserfs_acl_entry *entry = (reiserfs_acl_entry *) value;
		if ((char *)value + sizeof(reiserfs_acl_entry_short) > end)
			goto fail;
		acl->a_entries[n].e_tag = le16_to_cpu(entry->e_tag);
		acl->a_entries[n].e_perm = le16_to_cpu(entry->e_perm);
		switch (acl->a_entries[n].e_tag) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			value = (char *)value +
			    sizeof(reiserfs_acl_entry_short);
			break;

		case ACL_USER:
			value = (char *)value + sizeof(reiserfs_acl_entry);
			if ((char *)value > end)
				goto fail;
			acl->a_entries[n].e_uid = 
				make_kuid(&init_user_ns,
					  le32_to_cpu(entry->e_id));
			break;
		case ACL_GROUP:
			value = (char *)value + sizeof(reiserfs_acl_entry);
			if ((char *)value > end)
				goto fail;
			acl->a_entries[n].e_gid =
				make_kgid(&init_user_ns,
					  le32_to_cpu(entry->e_id));
			break;

		default:
			goto fail;
		}
	}
	if (value != end)
		goto fail;
	return acl;

      fail:
	posix_acl_release(acl);
	return ERR_PTR(-EINVAL);
}

/*
 * Convert from in-memory to filesystem representation.
 */
static void *posix_acl_to_disk(const struct posix_acl *acl, size_t * size)
{
	reiserfs_acl_header *ext_acl;
	char *e;
	int n;

	*size = reiserfs_acl_size(acl->a_count);
	ext_acl = kmalloc(sizeof(reiserfs_acl_header) +
						  acl->a_count *
						  sizeof(reiserfs_acl_entry),
						  GFP_NOFS);
	if (!ext_acl)
		return ERR_PTR(-ENOMEM);
	ext_acl->a_version = cpu_to_le32(REISERFS_ACL_VERSION);
	e = (char *)ext_acl + sizeof(reiserfs_acl_header);
	for (n = 0; n < acl->a_count; n++) {
		const struct posix_acl_entry *acl_e = &acl->a_entries[n];
		reiserfs_acl_entry *entry = (reiserfs_acl_entry *) e;
		entry->e_tag = cpu_to_le16(acl->a_entries[n].e_tag);
		entry->e_perm = cpu_to_le16(acl->a_entries[n].e_perm);
		switch (acl->a_entries[n].e_tag) {
		case ACL_USER:
			entry->e_id = cpu_to_le32(
				from_kuid(&init_user_ns, acl_e->e_uid));
			e += sizeof(reiserfs_acl_entry);
			break;
		case ACL_GROUP:
			entry->e_id = cpu_to_le32(
				from_kgid(&init_user_ns, acl_e->e_gid));
			e += sizeof(reiserfs_acl_entry);
			break;

		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			e += sizeof(reiserfs_acl_entry_short);
			break;

		default:
			goto fail;
		}
	}
	return (char *)ext_acl;

      fail:
	kfree(ext_acl);
	return ERR_PTR(-EINVAL);
}

/*
 * Inode operation get_posix_acl().
 *
 * inode->i_mutex: down
 * BKL held [before 2.5.x]
 */
struct posix_acl *reiserfs_get_acl(struct inode *inode, int type)
{
	char *name, *value;
	struct posix_acl *acl;
	int size;
	int retval;

	acl = get_cached_acl(inode, type);
	if (acl != ACL_NOT_CACHED)
		return acl;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		BUG();
	}

	size = reiserfs_xattr_get(inode, name, NULL, 0);
	if (size < 0) {
		if (size == -ENODATA || size == -ENOSYS) {
			set_cached_acl(inode, type, NULL);
			return NULL;
		}
		return ERR_PTR(size);
	}

	value = kmalloc(size, GFP_NOFS);
	if (!value)
		return ERR_PTR(-ENOMEM);

	retval = reiserfs_xattr_get(inode, name, value, size);
	if (retval == -ENODATA || retval == -ENOSYS) {
		/* This shouldn't actually happen as it should have
		   been caught above.. but just in case */
		acl = NULL;
	} else if (retval < 0) {
		acl = ERR_PTR(retval);
	} else {
		acl = posix_acl_from_disk(value, retval);
	}
	if (!IS_ERR(acl))
		set_cached_acl(inode, type, acl);

	kfree(value);
	return acl;
}

/*
 * Inode operation set_posix_acl().
 *
 * inode->i_mutex: down
 * BKL held [before 2.5.x]
 */
static int
reiserfs_set_acl(struct reiserfs_transaction_handle *th, struct inode *inode,
		 int type, struct posix_acl *acl)
{
	char *name;
	void *value = NULL;
	size_t size = 0;
	int error;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		if (acl) {
			error = posix_acl_equiv_mode(acl, &inode->i_mode);
			if (error < 0)
				return error;
			else {
				if (error == 0)
					acl = NULL;
			}
		}
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EACCES : 0;
		break;
	default:
		return -EINVAL;
	}

	if (acl) {
		value = posix_acl_to_disk(acl, &size);
		if (IS_ERR(value))
			return (int)PTR_ERR(value);
	}

	error = reiserfs_xattr_set_handle(th, inode, name, value, size, 0);

	/*
	 * Ensure that the inode gets dirtied if we're only using
	 * the mode bits and an old ACL didn't exist. We don't need
	 * to check if the inode is hashed here since we won't get
	 * called by reiserfs_inherit_default_acl().
	 */
	if (error == -ENODATA) {
		error = 0;
		if (type == ACL_TYPE_ACCESS) {
			inode->i_ctime = CURRENT_TIME_SEC;
			mark_inode_dirty(inode);
		}
	}

	kfree(value);

	if (!error)
		set_cached_acl(inode, type, acl);

	return error;
}

/* dir->i_mutex: locked,
 * inode is new and not released into the wild yet */
int
reiserfs_inherit_default_acl(struct reiserfs_transaction_handle *th,
			     struct inode *dir, struct dentry *dentry,
			     struct inode *inode)
{
	struct posix_acl *acl;
	int err = 0;

	/* ACLs only get applied to files and directories */
	if (S_ISLNK(inode->i_mode))
		return 0;

	/* ACLs can only be used on "new" objects, so if it's an old object
	 * there is nothing to inherit from */
	if (get_inode_sd_version(dir) == STAT_DATA_V1)
		goto apply_umask;

	/* Don't apply ACLs to objects in the .reiserfs_priv tree.. This
	 * would be useless since permissions are ignored, and a pain because
	 * it introduces locking cycles */
	if (IS_PRIVATE(dir)) {
		inode->i_flags |= S_PRIVATE;
		goto apply_umask;
	}

	acl = reiserfs_get_acl(dir, ACL_TYPE_DEFAULT);
	if (IS_ERR(acl))
		return PTR_ERR(acl);

	if (acl) {
		/* Copy the default ACL to the default ACL of a new directory */
		if (S_ISDIR(inode->i_mode)) {
			err = reiserfs_set_acl(th, inode, ACL_TYPE_DEFAULT,
					       acl);
			if (err)
				goto cleanup;
		}

		/* Now we reconcile the new ACL and the mode,
		   potentially modifying both */
		err = posix_acl_create(&acl, GFP_NOFS, &inode->i_mode);
		if (err < 0)
			return err;

		/* If we need an ACL.. */
		if (err > 0)
			err = reiserfs_set_acl(th, inode, ACL_TYPE_ACCESS, acl);
	      cleanup:
		posix_acl_release(acl);
	} else {
	      apply_umask:
		/* no ACL, apply umask */
		inode->i_mode &= ~current_umask();
	}

	return err;
}

/* This is used to cache the default acl before a new object is created.
 * The biggest reason for this is to get an idea of how many blocks will
 * actually be required for the create operation if we must inherit an ACL.
 * An ACL write can add up to 3 object creations and an additional file write
 * so we'd prefer not to reserve that many blocks in the journal if we can.
 * It also has the advantage of not loading the ACL with a transaction open,
 * this may seem silly, but if the owner of the directory is doing the
 * creation, the ACL may not be loaded since the permissions wouldn't require
 * it.
 * We return the number of blocks required for the transaction.
 */
int reiserfs_cache_default_acl(struct inode *inode)
{
	struct posix_acl *acl;
	int nblocks = 0;

	if (IS_PRIVATE(inode))
		return 0;

	acl = reiserfs_get_acl(inode, ACL_TYPE_DEFAULT);

	if (acl && !IS_ERR(acl)) {
		int size = reiserfs_acl_size(acl->a_count);

		/* Other xattrs can be created during inode creation. We don't
		 * want to claim too many blocks, so we check to see if we
		 * we need to create the tree to the xattrs, and then we
		 * just want two files. */
		nblocks = reiserfs_xattr_jcreate_nblocks(inode);
		nblocks += JOURNAL_BLOCKS_PER_OBJECT(inode->i_sb);

		REISERFS_I(inode)->i_flags |= i_has_xattr_dir;

		/* We need to account for writes + bitmaps for two files */
		nblocks += reiserfs_xattr_nblocks(inode, size) * 4;
		posix_acl_release(acl);
	}

	return nblocks;
}

int reiserfs_acl_chmod(struct inode *inode)
{
	struct reiserfs_transaction_handle th;
	struct posix_acl *acl;
	size_t size;
	int depth;
	int error;

	if (IS_PRIVATE(inode))
		return 0;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	if (get_inode_sd_version(inode) == STAT_DATA_V1 ||
	    !reiserfs_posixacl(inode->i_sb)) {
		return 0;
	}

	reiserfs_write_unlock(inode->i_sb);
	acl = reiserfs_get_acl(inode, ACL_TYPE_ACCESS);
	reiserfs_write_lock(inode->i_sb);
	if (!acl)
		return 0;
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	error = posix_acl_chmod(&acl, GFP_NOFS, inode->i_mode);
	if (error)
		return error;

	size = reiserfs_xattr_nblocks(inode, reiserfs_acl_size(acl->a_count));
	depth = reiserfs_write_lock_once(inode->i_sb);
	error = journal_begin(&th, inode->i_sb, size * 2);
	if (!error) {
		int error2;
		error = reiserfs_set_acl(&th, inode, ACL_TYPE_ACCESS, acl);
		error2 = journal_end(&th, inode->i_sb, size * 2);
		if (error2)
			error = error2;
	}
	reiserfs_write_unlock_once(inode->i_sb, depth);
	posix_acl_release(acl);
	return error;
}

static size_t posix_acl_access_list(struct dentry *dentry, char *list,
				    size_t list_size, const char *name,
				    size_t name_len, int type)
{
	const size_t size = sizeof(POSIX_ACL_XATTR_ACCESS);
	if (!reiserfs_posixacl(dentry->d_sb))
		return 0;
	if (list && size <= list_size)
		memcpy(list, POSIX_ACL_XATTR_ACCESS, size);
	return size;
}

const struct xattr_handler reiserfs_posix_acl_access_handler = {
	.prefix = POSIX_ACL_XATTR_ACCESS,
	.flags = ACL_TYPE_ACCESS,
	.get = posix_acl_get,
	.set = posix_acl_set,
	.list = posix_acl_access_list,
};

static size_t posix_acl_default_list(struct dentry *dentry, char *list,
				     size_t list_size, const char *name,
				     size_t name_len, int type)
{
	const size_t size = sizeof(POSIX_ACL_XATTR_DEFAULT);
	if (!reiserfs_posixacl(dentry->d_sb))
		return 0;
	if (list && size <= list_size)
		memcpy(list, POSIX_ACL_XATTR_DEFAULT, size);
	return size;
}

const struct xattr_handler reiserfs_posix_acl_default_handler = {
	.prefix = POSIX_ACL_XATTR_DEFAULT,
	.flags = ACL_TYPE_DEFAULT,
	.get = posix_acl_get,
	.set = posix_acl_set,
	.list = posix_acl_default_list,
};
