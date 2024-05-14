// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 * Copyright 2018 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

/*
 *  Linux VFS extended attribute operations.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>
#include <linux/hashtable.h>

#define SYSTEM_ORANGEFS_KEY "system.pvfs2."
#define SYSTEM_ORANGEFS_KEY_LEN 13

/*
 * this function returns
 *   0 if the key corresponding to name is not meant to be printed as part
 *     of a listxattr.
 *   1 if the key corresponding to name is meant to be returned as part of
 *     a listxattr.
 * The ones that start SYSTEM_ORANGEFS_KEY are the ones to avoid printing.
 */
static int is_reserved_key(const char *key, size_t size)
{

	if (size < SYSTEM_ORANGEFS_KEY_LEN)
		return 1;

	return strncmp(key, SYSTEM_ORANGEFS_KEY, SYSTEM_ORANGEFS_KEY_LEN) ?  1 : 0;
}

static inline int convert_to_internal_xattr_flags(int setxattr_flags)
{
	int internal_flag = 0;

	if (setxattr_flags & XATTR_REPLACE) {
		/* Attribute must exist! */
		internal_flag = ORANGEFS_XATTR_REPLACE;
	} else if (setxattr_flags & XATTR_CREATE) {
		/* Attribute must not exist */
		internal_flag = ORANGEFS_XATTR_CREATE;
	}
	return internal_flag;
}

static unsigned int xattr_key(const char *key)
{
	unsigned int i = 0;
	while (key)
		i += *key++;
	return i % 16;
}

static struct orangefs_cached_xattr *find_cached_xattr(struct inode *inode,
    const char *key)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_cached_xattr *cx;
	struct hlist_head *h;
	struct hlist_node *tmp;
	h = &orangefs_inode->xattr_cache[xattr_key(key)];
	if (hlist_empty(h))
		return NULL;
	hlist_for_each_entry_safe(cx, tmp, h, node) {
/*		if (!time_before(jiffies, cx->timeout)) {
			hlist_del(&cx->node);
			kfree(cx);
			continue;
		}*/
		if (!strcmp(cx->key, key))
			return cx;
	}
	return NULL;
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
ssize_t orangefs_inode_getxattr(struct inode *inode, const char *name,
				void *buffer, size_t size)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op = NULL;
	struct orangefs_cached_xattr *cx;
	ssize_t ret = -ENOMEM;
	ssize_t length = 0;
	int fsuid;
	int fsgid;

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "%s: name %s, buffer_size %zd\n",
		     __func__, name, size);

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	if (strlen(name) >= ORANGEFS_MAX_XATTR_NAMELEN)
		return -EINVAL;

	fsuid = from_kuid(&init_user_ns, current_fsuid());
	fsgid = from_kgid(&init_user_ns, current_fsgid());

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "getxattr on inode %pU, name %s "
		     "(uid %o, gid %o)\n",
		     get_khandle_from_ino(inode),
		     name,
		     fsuid,
		     fsgid);

	down_read(&orangefs_inode->xattr_sem);

	cx = find_cached_xattr(inode, name);
	if (cx && time_before(jiffies, cx->timeout)) {
		if (cx->length == -1) {
			ret = -ENODATA;
			goto out_unlock;
		} else {
			if (size == 0) {
				ret = cx->length;
				goto out_unlock;
			}
			if (cx->length > size) {
				ret = -ERANGE;
				goto out_unlock;
			}
			memcpy(buffer, cx->val, cx->length);
			memset(buffer + cx->length, 0, size - cx->length);
			ret = cx->length;
			goto out_unlock;
		}
	}

	new_op = op_alloc(ORANGEFS_VFS_OP_GETXATTR);
	if (!new_op)
		goto out_unlock;

	new_op->upcall.req.getxattr.refn = orangefs_inode->refn;
	strcpy(new_op->upcall.req.getxattr.key, name);

	/*
	 * NOTE: Although keys are meant to be NULL terminated textual
	 * strings, I am going to explicitly pass the length just in case
	 * we change this later on...
	 */
	new_op->upcall.req.getxattr.key_sz = strlen(name) + 1;

	ret = service_operation(new_op, "orangefs_inode_getxattr",
				get_interruptible_flag(inode));
	if (ret != 0) {
		if (ret == -ENOENT) {
			ret = -ENODATA;
			gossip_debug(GOSSIP_XATTR_DEBUG,
				     "orangefs_inode_getxattr: inode %pU key %s"
				     " does not exist!\n",
				     get_khandle_from_ino(inode),
				     (char *)new_op->upcall.req.getxattr.key);
			cx = kmalloc(sizeof *cx, GFP_KERNEL);
			if (cx) {
				strcpy(cx->key, name);
				cx->length = -1;
				cx->timeout = jiffies +
				    orangefs_getattr_timeout_msecs*HZ/1000;
				hash_add(orangefs_inode->xattr_cache, &cx->node,
				    xattr_key(cx->key));
			}
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

	memcpy(buffer, new_op->downcall.resp.getxattr.val, length);
	memset(buffer + length, 0, size - length);
	gossip_debug(GOSSIP_XATTR_DEBUG,
	     "orangefs_inode_getxattr: inode %pU "
	     "key %s key_sz %d, val_len %d\n",
	     get_khandle_from_ino(inode),
	     (char *)new_op->
		upcall.req.getxattr.key,
		     (int)new_op->
		upcall.req.getxattr.key_sz,
	     (int)ret);

	ret = length;

	if (cx) {
		strcpy(cx->key, name);
		memcpy(cx->val, buffer, length);
		cx->length = length;
		cx->timeout = jiffies + HZ;
	} else {
		cx = kmalloc(sizeof *cx, GFP_KERNEL);
		if (cx) {
			strcpy(cx->key, name);
			memcpy(cx->val, buffer, length);
			cx->length = length;
			cx->timeout = jiffies + HZ;
			hash_add(orangefs_inode->xattr_cache, &cx->node,
			    xattr_key(cx->key));
		}
	}

out_release_op:
	op_release(new_op);
out_unlock:
	up_read(&orangefs_inode->xattr_sem);
	return ret;
}

static int orangefs_inode_removexattr(struct inode *inode, const char *name,
				      int flags)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op = NULL;
	struct orangefs_cached_xattr *cx;
	struct hlist_head *h;
	struct hlist_node *tmp;
	int ret = -ENOMEM;

	if (strlen(name) >= ORANGEFS_MAX_XATTR_NAMELEN)
		return -EINVAL;

	down_write(&orangefs_inode->xattr_sem);
	new_op = op_alloc(ORANGEFS_VFS_OP_REMOVEXATTR);
	if (!new_op)
		goto out_unlock;

	new_op->upcall.req.removexattr.refn = orangefs_inode->refn;
	/*
	 * NOTE: Although keys are meant to be NULL terminated
	 * textual strings, I am going to explicitly pass the
	 * length just in case we change this later on...
	 */
	strcpy(new_op->upcall.req.removexattr.key, name);
	new_op->upcall.req.removexattr.key_sz = strlen(name) + 1;

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "orangefs_inode_removexattr: key %s, key_sz %d\n",
		     (char *)new_op->upcall.req.removexattr.key,
		     (int)new_op->upcall.req.removexattr.key_sz);

	ret = service_operation(new_op,
				"orangefs_inode_removexattr",
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
		     "orangefs_inode_removexattr: returning %d\n", ret);

	op_release(new_op);

	h = &orangefs_inode->xattr_cache[xattr_key(name)];
	hlist_for_each_entry_safe(cx, tmp, h, node) {
		if (!strcmp(cx->key, name)) {
			hlist_del(&cx->node);
			kfree(cx);
			break;
		}
	}

out_unlock:
	up_write(&orangefs_inode->xattr_sem);
	return ret;
}

/*
 * Tries to set an attribute for a given key on a file.
 *
 * Returns a -ve number on error and 0 on success.  Key is text, but value
 * can be binary!
 */
int orangefs_inode_setxattr(struct inode *inode, const char *name,
			    const void *value, size_t size, int flags)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op;
	int internal_flag = 0;
	struct orangefs_cached_xattr *cx;
	struct hlist_head *h;
	struct hlist_node *tmp;
	int ret = -ENOMEM;

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "%s: name %s, buffer_size %zd\n",
		     __func__, name, size);

	if (size > ORANGEFS_MAX_XATTR_VALUELEN)
		return -EINVAL;
	if (strlen(name) >= ORANGEFS_MAX_XATTR_NAMELEN)
		return -EINVAL;

	internal_flag = convert_to_internal_xattr_flags(flags);

	/* This is equivalent to a removexattr */
	if (size == 0 && !value) {
		gossip_debug(GOSSIP_XATTR_DEBUG,
			     "removing xattr (%s)\n",
			     name);
		return orangefs_inode_removexattr(inode, name, flags);
	}

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "setxattr on inode %pU, name %s\n",
		     get_khandle_from_ino(inode),
		     name);

	down_write(&orangefs_inode->xattr_sem);
	new_op = op_alloc(ORANGEFS_VFS_OP_SETXATTR);
	if (!new_op)
		goto out_unlock;


	new_op->upcall.req.setxattr.refn = orangefs_inode->refn;
	new_op->upcall.req.setxattr.flags = internal_flag;
	/*
	 * NOTE: Although keys are meant to be NULL terminated textual
	 * strings, I am going to explicitly pass the length just in
	 * case we change this later on...
	 */
	strcpy(new_op->upcall.req.setxattr.keyval.key, name);
	new_op->upcall.req.setxattr.keyval.key_sz = strlen(name) + 1;
	memcpy(new_op->upcall.req.setxattr.keyval.val, value, size);
	new_op->upcall.req.setxattr.keyval.val_sz = size;

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "orangefs_inode_setxattr: key %s, key_sz %d "
		     " value size %zd\n",
		     (char *)new_op->upcall.req.setxattr.keyval.key,
		     (int)new_op->upcall.req.setxattr.keyval.key_sz,
		     size);

	ret = service_operation(new_op,
				"orangefs_inode_setxattr",
				get_interruptible_flag(inode));

	gossip_debug(GOSSIP_XATTR_DEBUG,
		     "orangefs_inode_setxattr: returning %d\n",
		     ret);

	/* when request is serviced properly, free req op struct */
	op_release(new_op);

	h = &orangefs_inode->xattr_cache[xattr_key(name)];
	hlist_for_each_entry_safe(cx, tmp, h, node) {
		if (!strcmp(cx->key, name)) {
			hlist_del(&cx->node);
			kfree(cx);
			break;
		}
	}

out_unlock:
	up_write(&orangefs_inode->xattr_sem);
	return ret;
}

/*
 * Tries to get a specified object's keys into a user-specified buffer of a
 * given size.  Note that like the previous instances of xattr routines, this
 * also allows you to pass in a NULL pointer and 0 size to probe the size for
 * subsequent memory allocations. Thus our return value is always the size of
 * all the keys unless there were errors in fetching the keys!
 */
ssize_t orangefs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op;
	__u64 token = ORANGEFS_ITERATE_START;
	ssize_t ret = -ENOMEM;
	ssize_t total = 0;
	int count_keys = 0;
	int key_size;
	int i = 0;
	int returned_count = 0;

	if (size > 0 && !buffer) {
		gossip_err("%s: bogus NULL pointers\n", __func__);
		return -EINVAL;
	}

	down_read(&orangefs_inode->xattr_sem);
	new_op = op_alloc(ORANGEFS_VFS_OP_LISTXATTR);
	if (!new_op)
		goto out_unlock;

	if (buffer && size > 0)
		memset(buffer, 0, size);

try_again:
	key_size = 0;
	new_op->upcall.req.listxattr.refn = orangefs_inode->refn;
	new_op->upcall.req.listxattr.token = token;
	new_op->upcall.req.listxattr.requested_count =
	    (size == 0) ? 0 : ORANGEFS_MAX_XATTR_LISTLEN;
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
			ORANGEFS_MAX_XATTR_NAMELEN;
		goto done;
	}

	returned_count = new_op->downcall.resp.listxattr.returned_count;
	if (returned_count < 0 ||
	    returned_count > ORANGEFS_MAX_XATTR_LISTLEN) {
		gossip_err("%s: impossible value for returned_count:%d:\n",
		__func__,
		returned_count);
		ret = -EIO;
		goto done;
	}

	/*
	 * Check to see how much can be fit in the buffer. Fit only whole keys.
	 */
	for (i = 0; i < returned_count; i++) {
		if (new_op->downcall.resp.listxattr.lengths[i] < 0 ||
		    new_op->downcall.resp.listxattr.lengths[i] >
		    ORANGEFS_MAX_XATTR_NAMELEN) {
			gossip_err("%s: impossible value for lengths[%d]\n",
			    __func__,
			    new_op->downcall.resp.listxattr.lengths[i]);
			ret = -EIO;
			goto done;
		}
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
	if (token != ORANGEFS_ITERATE_END)
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
	up_read(&orangefs_inode->xattr_sem);
	return ret;
}

static int orangefs_xattr_set_default(const struct xattr_handler *handler,
				      struct user_namespace *mnt_userns,
				      struct dentry *unused,
				      struct inode *inode,
				      const char *name,
				      const void *buffer,
				      size_t size,
				      int flags)
{
	return orangefs_inode_setxattr(inode, name, buffer, size, flags);
}

static int orangefs_xattr_get_default(const struct xattr_handler *handler,
				      struct dentry *unused,
				      struct inode *inode,
				      const char *name,
				      void *buffer,
				      size_t size)
{
	return orangefs_inode_getxattr(inode, name, buffer, size);

}

static const struct xattr_handler orangefs_xattr_default_handler = {
	.prefix = "",  /* match any name => handlers called with full name */
	.get = orangefs_xattr_get_default,
	.set = orangefs_xattr_set_default,
};

const struct xattr_handler *orangefs_xattr_handlers[] = {
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
	&orangefs_xattr_default_handler,
	NULL
};
