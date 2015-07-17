/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  Linux VFS extended attribute operations.
 */

#include "protocol.h"
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>


#define SYSTEM_PVFS2_KEY "system.pvfs2."
#define SYSTEM_PVFS2_KEY_LEN 13

/*
 * this function returns
 *   0 if the key corresponding to name is not meant to be printed as part
 *     of a listxattr.
 *   1 if the key corresponding to name is meant to be returned as part of
 *     a listxattr.
 * The ones that start SYSTEM_PVFS2_KEY are the ones to avoid printing.
 */
static int is_reserved_key(const char *key, size_t size)
{

	if (size < SYSTEM_PVFS2_KEY_LEN)
		return 1;

	return strncmp(key, SYSTEM_PVFS2_KEY, SYSTEM_PVFS2_KEY_LEN) ?  1 : 0;
}

static inline int convert_to_internal_xattr_flags(int setxattr_flags)
{
	int internal_flag = 0;

	if (setxattr_flags & XATTR_REPLACE) {
		/* Attribute must exist! */
		internal_flag = PVFS_XATTR_REPLACE;
	} else if (setxattr_flags & XATTR_CREATE) {
		/* Attribute must not exist */
		internal_flag = PVFS_XATTR_CREATE;
	}
	return internal_flag;
}


/*
 * Tries to get a specified key's attributes of a given
 * file into a user-specified buffer. Note that the getxattr
 * interface allows for the users to probe the size of an
 * extended attribute by passing in a value of 0 to size.
 * Thus our return value is always the size of the attribute
 * unless the key does not exist for the file and/or if
 * there were errors in fetching the attribute value.
 */
ssize_t pvfs2_inode_getxattr(struct inode *inode, const char *prefix,
		const char *name, void *buffer, size_t size)
{
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);
	struct pvfs2_kernel_op_s *new_op = NULL;
	ssize_t ret = -ENOMEM;
	ssize_t length = 0;
	int fsuid;
	int fsgid;

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "%s: prefix %s name %s, buffer_size %zd\n",
		     __func__, prefix, name, size);

	if (name == NULL || (size > 0 && buffer == NULL)) {
		gossip_err("pvfs2_inode_getxattr: bogus NULL pointers\n");
		return -EINVAL;
	}
	if (size < 0 ||
	    (strlen(name) + strlen(prefix)) >= PVFS_MAX_XATTR_NAMELEN) {
		gossip_err("Invalid size (%d) or key length (%d)\n",
			   (int)size,
			   (int)(strlen(name) + strlen(prefix)));
		return -EINVAL;
	}

	fsuid = from_kuid(current_user_ns(), current_fsuid());
	fsgid = from_kgid(current_user_ns(), current_fsgid());

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "getxattr on inode %pU, name %s "
		     "(uid %o, gid %o)\n",
		     get_khandle_from_ino(inode),
		     name,
		     fsuid,
		     fsgid);

	down_read(&pvfs2_inode->xattr_sem);

	new_op = op_alloc(PVFS2_VFS_OP_GETXATTR);
	if (!new_op)
		goto out_unlock;

	new_op->upcall.req.getxattr.refn = pvfs2_inode->refn;
	ret = snprintf((char *)new_op->upcall.req.getxattr.key,
		       PVFS_MAX_XATTR_NAMELEN, "%s%s", prefix, name);

	/*
	 * NOTE: Although keys are meant to be NULL terminated textual
	 * strings, I am going to explicitly pass the length just in case
	 * we change this later on...
	 */
	new_op->upcall.req.getxattr.key_sz = ret + 1;

	ret = service_operation(new_op, "pvfs2_inode_getxattr",
				get_interruptible_flag(inode));
	if (ret != 0) {
		if (ret == -ENOENT) {
			ret = -ENODATA;
			gossip_debug(GOSSIP_XATTR_DEBUG,
				     "pvfs2_inode_getxattr: inode %pU key %s"
				     " does not exist!\n",
				     get_khandle_from_ino(inode),
				     (char *)new_op->upcall.req.getxattr.key);
		}
		goto out_release_op;
	}

	/*
	 * Length returned includes null terminator.
	 */
	length = new_op->downcall.resp.getxattr.val_sz;

	/*
	 * Just return the length of the queried attribute.
	 */
	if (size == 0) {
		ret = length;
		goto out_release_op;
	}

	/*
	 * Check to see if key length is > provided buffer size.
	 */
	if (length > size) {
		ret = -ERANGE;
		goto out_release_op;
	}

	memset(buffer, 0, size);
	memcpy(buffer, new_op->downcall.resp.getxattr.val, length);
	gossip_debug(GOSSIP_XATTR_DEBUG,
	     "pvfs2_inode_getxattr: inode %pU "
	     "key %s key_sz %d, val_len %d\n",
	     get_khandle_from_ino(inode),
	     (char *)new_op->
		upcall.req.getxattr.key,
		     (int)new_op->
		upcall.req.getxattr.key_sz,
	     (int)ret);

	ret = length;

out_release_op:
	op_release(new_op);
out_unlock:
	up_read(&pvfs2_inode->xattr_sem);
	return ret;
}

static int pvfs2_inode_removexattr(struct inode *inode,
			    const char *prefix,
			    const char *name,
			    int flags)
{
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);
	struct pvfs2_kernel_op_s *new_op = NULL;
	int ret = -ENOMEM;

	down_write(&pvfs2_inode->xattr_sem);
	new_op = op_alloc(PVFS2_VFS_OP_REMOVEXATTR);
	if (!new_op)
		goto out_unlock;

	new_op->upcall.req.removexattr.refn = pvfs2_inode->refn;
	/*
	 * NOTE: Although keys are meant to be NULL terminated
	 * textual strings, I am going to explicitly pass the
	 * length just in case we change this later on...
	 */
	ret = snprintf((char *)new_op->upcall.req.removexattr.key,
		       PVFS_MAX_XATTR_NAMELEN,
		       "%s%s",
		       (prefix ? prefix : ""),
		       name);
	new_op->upcall.req.removexattr.key_sz = ret + 1;

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "pvfs2_inode_removexattr: key %s, key_sz %d\n",
		     (char *)new_op->upcall.req.removexattr.key,
		     (int)new_op->upcall.req.removexattr.key_sz);

	ret = service_operation(new_op,
				"pvfs2_inode_removexattr",
				get_interruptible_flag(inode));
	if (ret == -ENOENT) {
		/*
		 * Request to replace a non-existent attribute is an error.
		 */
		if (flags & XATTR_REPLACE)
			ret = -ENODATA;
		else
			ret = 0;
	}

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "pvfs2_inode_removexattr: returning %d\n", ret);

	op_release(new_op);
out_unlock:
	up_write(&pvfs2_inode->xattr_sem);
	return ret;
}

/*
 * Tries to set an attribute for a given key on a file.
 *
 * Returns a -ve number on error and 0 on success.  Key is text, but value
 * can be binary!
 */
int pvfs2_inode_setxattr(struct inode *inode, const char *prefix,
		const char *name, const void *value, size_t size, int flags)
{
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);
	struct pvfs2_kernel_op_s *new_op;
	int internal_flag = 0;
	int ret = -ENOMEM;

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "%s: prefix %s, name %s, buffer_size %zd\n",
		     __func__, prefix, name, size);

	if (size < 0 ||
	    size >= PVFS_MAX_XATTR_VALUELEN ||
	    flags < 0) {
		gossip_err("pvfs2_inode_setxattr: bogus values of size(%d), flags(%d)\n",
			   (int)size,
			   flags);
		return -EINVAL;
	}

	if (name == NULL ||
	    (size > 0 && value == NULL)) {
		gossip_err("pvfs2_inode_setxattr: bogus NULL pointers!\n");
		return -EINVAL;
	}

	internal_flag = convert_to_internal_xattr_flags(flags);

	if (prefix) {
		if (strlen(name) + strlen(prefix) >= PVFS_MAX_XATTR_NAMELEN) {
			gossip_err
			    ("pvfs2_inode_setxattr: bogus key size (%d)\n",
			     (int)(strlen(name) + strlen(prefix)));
			return -EINVAL;
		}
	} else {
		if (strlen(name) >= PVFS_MAX_XATTR_NAMELEN) {
			gossip_err
			    ("pvfs2_inode_setxattr: bogus key size (%d)\n",
			     (int)(strlen(name)));
			return -EINVAL;
		}
	}

	/* This is equivalent to a removexattr */
	if (size == 0 && value == NULL) {
		gossip_debug(GOSSIP_XATTR_DEBUG,
			     "removing xattr (%s%s)\n",
			     prefix,
			     name);
		return pvfs2_inode_removexattr(inode, prefix, name, flags);
	}

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "setxattr on inode %pU, name %s\n",
		     get_khandle_from_ino(inode),
		     name);

	down_write(&pvfs2_inode->xattr_sem);
	new_op = op_alloc(PVFS2_VFS_OP_SETXATTR);
	if (!new_op)
		goto out_unlock;


	new_op->upcall.req.setxattr.refn = pvfs2_inode->refn;
	new_op->upcall.req.setxattr.flags = internal_flag;
	/*
	 * NOTE: Although keys are meant to be NULL terminated textual
	 * strings, I am going to explicitly pass the length just in
	 * case we change this later on...
	 */
	ret = snprintf((char *)new_op->upcall.req.setxattr.keyval.key,
		       PVFS_MAX_XATTR_NAMELEN,
		       "%s%s",
		       prefix, name);
	new_op->upcall.req.setxattr.keyval.key_sz = ret + 1;
	memcpy(new_op->upcall.req.setxattr.keyval.val, value, size);
	new_op->upcall.req.setxattr.keyval.val_sz = size;

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "pvfs2_inode_setxattr: key %s, key_sz %d "
		     " value size %zd\n",
		     (char *)new_op->upcall.req.setxattr.keyval.key,
		     (int)new_op->upcall.req.setxattr.keyval.key_sz,
		     size);

	ret = service_operation(new_op,
				"pvfs2_inode_setxattr",
				get_interruptible_flag(inode));

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "pvfs2_inode_setxattr: returning %d\n",
		     ret);

	/* when request is serviced properly, free req op struct */
	op_release(new_op);
out_unlock:
	up_write(&pvfs2_inode->xattr_sem);
	return ret;
}

/*
 * Tries to get a specified object's keys into a user-specified buffer of a
 * given size.  Note that like the previous instances of xattr routines, this
 * also allows you to pass in a NULL pointer and 0 size to probe the size for
 * subsequent memory allocations. Thus our return value is always the size of
 * all the keys unless there were errors in fetching the keys!
 */
ssize_t pvfs2_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);
	struct pvfs2_kernel_op_s *new_op;
	__u64 token = PVFS_ITERATE_START;
	ssize_t ret = -ENOMEM;
	ssize_t total = 0;
	ssize_t length = 0;
	int count_keys = 0;
	int key_size;
	int i = 0;

	if (size > 0 && buffer == NULL) {
		gossip_err("%s: bogus NULL pointers\n", __func__);
		return -EINVAL;
	}
	if (size < 0) {
		gossip_err("Invalid size (%d)\n", (int)size);
		return -EINVAL;
	}

	down_read(&pvfs2_inode->xattr_sem);
	new_op = op_alloc(PVFS2_VFS_OP_LISTXATTR);
	if (!new_op)
		goto out_unlock;

	if (buffer && size > 0)
		memset(buffer, 0, size);

try_again:
	key_size = 0;
	new_op->upcall.req.listxattr.refn = pvfs2_inode->refn;
	new_op->upcall.req.listxattr.token = token;
	new_op->upcall.req.listxattr.requested_count =
	    (size == 0) ? 0 : PVFS_MAX_XATTR_LISTLEN;
	ret = service_operation(new_op, __func__,
				get_interruptible_flag(inode));
	if (ret != 0)
		goto done;

	if (size == 0) {
		/*
		 * This is a bit of a big upper limit, but I did not want to
		 * spend too much time getting this correct, since users end
		 * up allocating memory rather than us...
		 */
		total = new_op->downcall.resp.listxattr.returned_count *
			PVFS_MAX_XATTR_NAMELEN;
		goto done;
	}

	length = new_op->downcall.resp.listxattr.keylen;
	if (length == 0)
		goto done;

	/*
	 * Check to see how much can be fit in the buffer. Fit only whole keys.
	 */
	for (i = 0; i < new_op->downcall.resp.listxattr.returned_count; i++) {
		if (total + new_op->downcall.resp.listxattr.lengths[i] > size)
			goto done;

		/*
		 * Since many dumb programs try to setxattr() on our reserved
		 * xattrs this is a feeble attempt at defeating those by not
		 * listing them in the output of listxattr.. sigh
		 */
		if (is_reserved_key(new_op->downcall.resp.listxattr.key +
				    key_size,
				    new_op->downcall.resp.
					listxattr.lengths[i])) {
			gossip_debug(GOSSIP_XATTR_DEBUG, "Copying key %d -> %s\n",
					i, new_op->downcall.resp.listxattr.key +
						key_size);
			memcpy(buffer + total,
				new_op->downcall.resp.listxattr.key + key_size,
				new_op->downcall.resp.listxattr.lengths[i]);
			total += new_op->downcall.resp.listxattr.lengths[i];
			count_keys++;
		} else {
			gossip_debug(GOSSIP_XATTR_DEBUG, "[RESERVED] key %d -> %s\n",
					i, new_op->downcall.resp.listxattr.key +
						key_size);
		}
		key_size += new_op->downcall.resp.listxattr.lengths[i];
	}

	/*
	 * Since the buffer was large enough, we might have to continue
	 * fetching more keys!
	 */
	token = new_op->downcall.resp.listxattr.token;
	if (token != PVFS_ITERATE_END)
		goto try_again;

done:
	gossip_debug(GOSSIP_XATTR_DEBUG, "%s: returning %d"
		     " [size of buffer %ld] (filled in %d keys)\n",
		     __func__,
		     ret ? (int)ret : (int)total,
		     (long)size,
		     count_keys);
	op_release(new_op);
	if (ret == 0)
		ret = total;
out_unlock:
	up_read(&pvfs2_inode->xattr_sem);
	return ret;
}

int pvfs2_xattr_set_default(struct dentry *dentry,
			    const char *name,
			    const void *buffer,
			    size_t size,
			    int flags,
			    int handler_flags)
{
	return pvfs2_inode_setxattr(dentry->d_inode,
				    PVFS2_XATTR_NAME_DEFAULT_PREFIX,
				    name,
				    buffer,
				    size,
				    flags);
}

int pvfs2_xattr_get_default(struct dentry *dentry,
			    const char *name,
			    void *buffer,
			    size_t size,
			    int handler_flags)
{
	return pvfs2_inode_getxattr(dentry->d_inode,
				    PVFS2_XATTR_NAME_DEFAULT_PREFIX,
				    name,
				    buffer,
				    size);

}

static int pvfs2_xattr_set_trusted(struct dentry *dentry,
			    const char *name,
			    const void *buffer,
			    size_t size,
			    int flags,
			    int handler_flags)
{
	return pvfs2_inode_setxattr(dentry->d_inode,
				    PVFS2_XATTR_NAME_TRUSTED_PREFIX,
				    name,
				    buffer,
				    size,
				    flags);
}

static int pvfs2_xattr_get_trusted(struct dentry *dentry,
			    const char *name,
			    void *buffer,
			    size_t size,
			    int handler_flags)
{
	return pvfs2_inode_getxattr(dentry->d_inode,
				    PVFS2_XATTR_NAME_TRUSTED_PREFIX,
				    name,
				    buffer,
				    size);
}

static struct xattr_handler pvfs2_xattr_trusted_handler = {
	.prefix = PVFS2_XATTR_NAME_TRUSTED_PREFIX,
	.get = pvfs2_xattr_get_trusted,
	.set = pvfs2_xattr_set_trusted,
};

static struct xattr_handler pvfs2_xattr_default_handler = {
	/*
	 * NOTE: this is set to be the empty string.
	 * so that all un-prefixed xattrs keys get caught
	 * here!
	 */
	.prefix = PVFS2_XATTR_NAME_DEFAULT_PREFIX,
	.get = pvfs2_xattr_get_default,
	.set = pvfs2_xattr_set_default,
};

const struct xattr_handler *pvfs2_xattr_handlers[] = {
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
	&pvfs2_xattr_trusted_handler,
	&pvfs2_xattr_default_handler,
	NULL
};
