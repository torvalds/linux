// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>
#include <linux/ceph/pagelist.h>

#include "super.h"
#include "mds_client.h"

#include <linux/ceph/decode.h>

#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>
#include <linux/slab.h>

#define XATTR_CEPH_PREFIX "ceph."
#define XATTR_CEPH_PREFIX_LEN (sizeof (XATTR_CEPH_PREFIX) - 1)

static int __remove_xattr(struct ceph_inode_info *ci,
			  struct ceph_inode_xattr *xattr);

static const struct xattr_handler ceph_other_xattr_handler;

/*
 * List of handlers for synthetic system.* attributes. Other
 * attributes are handled directly.
 */
const struct xattr_handler *ceph_xattr_handlers[] = {
#ifdef CONFIG_CEPH_FS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
	&ceph_other_xattr_handler,
	NULL,
};

static bool ceph_is_valid_xattr(const char *name)
{
	return !strncmp(name, XATTR_CEPH_PREFIX, XATTR_CEPH_PREFIX_LEN) ||
	       !strncmp(name, XATTR_SECURITY_PREFIX,
			XATTR_SECURITY_PREFIX_LEN) ||
	       !strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN) ||
	       !strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
}

/*
 * These define virtual xattrs exposing the recursive directory
 * statistics and layout metadata.
 */
struct ceph_vxattr {
	char *name;
	size_t name_size;	/* strlen(name) + 1 (for '\0') */
	size_t (*getxattr_cb)(struct ceph_inode_info *ci, char *val,
			      size_t size);
	bool (*exists_cb)(struct ceph_inode_info *ci);
	unsigned int flags;
};

#define VXATTR_FLAG_READONLY		(1<<0)
#define VXATTR_FLAG_HIDDEN		(1<<1)
#define VXATTR_FLAG_RSTAT		(1<<2)

/* layouts */

static bool ceph_vxattrcb_layout_exists(struct ceph_inode_info *ci)
{
	struct ceph_file_layout *fl = &ci->i_layout;
	return (fl->stripe_unit > 0 || fl->stripe_count > 0 ||
		fl->object_size > 0 || fl->pool_id >= 0 ||
		rcu_dereference_raw(fl->pool_ns) != NULL);
}

static size_t ceph_vxattrcb_layout(struct ceph_inode_info *ci, char *val,
				   size_t size)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(ci->vfs_inode.i_sb);
	struct ceph_osd_client *osdc = &fsc->client->osdc;
	struct ceph_string *pool_ns;
	s64 pool = ci->i_layout.pool_id;
	const char *pool_name;
	const char *ns_field = " pool_namespace=";
	char buf[128];
	size_t len, total_len = 0;
	int ret;

	pool_ns = ceph_try_get_string(ci->i_layout.pool_ns);

	dout("ceph_vxattrcb_layout %p\n", &ci->vfs_inode);
	down_read(&osdc->lock);
	pool_name = ceph_pg_pool_name_by_id(osdc->osdmap, pool);
	if (pool_name) {
		len = snprintf(buf, sizeof(buf),
		"stripe_unit=%u stripe_count=%u object_size=%u pool=",
		ci->i_layout.stripe_unit, ci->i_layout.stripe_count,
	        ci->i_layout.object_size);
		total_len = len + strlen(pool_name);
	} else {
		len = snprintf(buf, sizeof(buf),
		"stripe_unit=%u stripe_count=%u object_size=%u pool=%lld",
		ci->i_layout.stripe_unit, ci->i_layout.stripe_count,
	        ci->i_layout.object_size, (unsigned long long)pool);
		total_len = len;
	}

	if (pool_ns)
		total_len += strlen(ns_field) + pool_ns->len;

	if (!size) {
		ret = total_len;
	} else if (total_len > size) {
		ret = -ERANGE;
	} else {
		memcpy(val, buf, len);
		ret = len;
		if (pool_name) {
			len = strlen(pool_name);
			memcpy(val + ret, pool_name, len);
			ret += len;
		}
		if (pool_ns) {
			len = strlen(ns_field);
			memcpy(val + ret, ns_field, len);
			ret += len;
			memcpy(val + ret, pool_ns->str, pool_ns->len);
			ret += pool_ns->len;
		}
	}
	up_read(&osdc->lock);
	ceph_put_string(pool_ns);
	return ret;
}

static size_t ceph_vxattrcb_layout_stripe_unit(struct ceph_inode_info *ci,
					       char *val, size_t size)
{
	return snprintf(val, size, "%u", ci->i_layout.stripe_unit);
}

static size_t ceph_vxattrcb_layout_stripe_count(struct ceph_inode_info *ci,
						char *val, size_t size)
{
	return snprintf(val, size, "%u", ci->i_layout.stripe_count);
}

static size_t ceph_vxattrcb_layout_object_size(struct ceph_inode_info *ci,
					       char *val, size_t size)
{
	return snprintf(val, size, "%u", ci->i_layout.object_size);
}

static size_t ceph_vxattrcb_layout_pool(struct ceph_inode_info *ci,
					char *val, size_t size)
{
	int ret;
	struct ceph_fs_client *fsc = ceph_sb_to_client(ci->vfs_inode.i_sb);
	struct ceph_osd_client *osdc = &fsc->client->osdc;
	s64 pool = ci->i_layout.pool_id;
	const char *pool_name;

	down_read(&osdc->lock);
	pool_name = ceph_pg_pool_name_by_id(osdc->osdmap, pool);
	if (pool_name)
		ret = snprintf(val, size, "%s", pool_name);
	else
		ret = snprintf(val, size, "%lld", (unsigned long long)pool);
	up_read(&osdc->lock);
	return ret;
}

static size_t ceph_vxattrcb_layout_pool_namespace(struct ceph_inode_info *ci,
						  char *val, size_t size)
{
	int ret = 0;
	struct ceph_string *ns = ceph_try_get_string(ci->i_layout.pool_ns);
	if (ns) {
		ret = snprintf(val, size, "%.*s", (int)ns->len, ns->str);
		ceph_put_string(ns);
	}
	return ret;
}

/* directories */

static size_t ceph_vxattrcb_dir_entries(struct ceph_inode_info *ci, char *val,
					size_t size)
{
	return snprintf(val, size, "%lld", ci->i_files + ci->i_subdirs);
}

static size_t ceph_vxattrcb_dir_files(struct ceph_inode_info *ci, char *val,
				      size_t size)
{
	return snprintf(val, size, "%lld", ci->i_files);
}

static size_t ceph_vxattrcb_dir_subdirs(struct ceph_inode_info *ci, char *val,
					size_t size)
{
	return snprintf(val, size, "%lld", ci->i_subdirs);
}

static size_t ceph_vxattrcb_dir_rentries(struct ceph_inode_info *ci, char *val,
					 size_t size)
{
	return snprintf(val, size, "%lld", ci->i_rfiles + ci->i_rsubdirs);
}

static size_t ceph_vxattrcb_dir_rfiles(struct ceph_inode_info *ci, char *val,
				       size_t size)
{
	return snprintf(val, size, "%lld", ci->i_rfiles);
}

static size_t ceph_vxattrcb_dir_rsubdirs(struct ceph_inode_info *ci, char *val,
					 size_t size)
{
	return snprintf(val, size, "%lld", ci->i_rsubdirs);
}

static size_t ceph_vxattrcb_dir_rbytes(struct ceph_inode_info *ci, char *val,
				       size_t size)
{
	return snprintf(val, size, "%lld", ci->i_rbytes);
}

static size_t ceph_vxattrcb_dir_rctime(struct ceph_inode_info *ci, char *val,
				       size_t size)
{
	return snprintf(val, size, "%lld.09%ld", ci->i_rctime.tv_sec,
			ci->i_rctime.tv_nsec);
}

/* quotas */

static bool ceph_vxattrcb_quota_exists(struct ceph_inode_info *ci)
{
	bool ret = false;
	spin_lock(&ci->i_ceph_lock);
	if ((ci->i_max_files || ci->i_max_bytes) &&
	    ci->i_vino.snap == CEPH_NOSNAP &&
	    ci->i_snap_realm &&
	    ci->i_snap_realm->ino == ci->i_vino.ino)
		ret = true;
	spin_unlock(&ci->i_ceph_lock);
	return ret;
}

static size_t ceph_vxattrcb_quota(struct ceph_inode_info *ci, char *val,
				  size_t size)
{
	return snprintf(val, size, "max_bytes=%llu max_files=%llu",
			ci->i_max_bytes, ci->i_max_files);
}

static size_t ceph_vxattrcb_quota_max_bytes(struct ceph_inode_info *ci,
					    char *val, size_t size)
{
	return snprintf(val, size, "%llu", ci->i_max_bytes);
}

static size_t ceph_vxattrcb_quota_max_files(struct ceph_inode_info *ci,
					    char *val, size_t size)
{
	return snprintf(val, size, "%llu", ci->i_max_files);
}

#define CEPH_XATTR_NAME(_type, _name)	XATTR_CEPH_PREFIX #_type "." #_name
#define CEPH_XATTR_NAME2(_type, _name, _name2)	\
	XATTR_CEPH_PREFIX #_type "." #_name "." #_name2

#define XATTR_NAME_CEPH(_type, _name, _flags)				\
	{								\
		.name = CEPH_XATTR_NAME(_type, _name),			\
		.name_size = sizeof (CEPH_XATTR_NAME(_type, _name)), \
		.getxattr_cb = ceph_vxattrcb_ ## _type ## _ ## _name, \
		.exists_cb = NULL,					\
		.flags = (VXATTR_FLAG_READONLY | _flags),		\
	}
#define XATTR_RSTAT_FIELD(_type, _name)			\
	XATTR_NAME_CEPH(_type, _name, VXATTR_FLAG_RSTAT)
#define XATTR_LAYOUT_FIELD(_type, _name, _field)			\
	{								\
		.name = CEPH_XATTR_NAME2(_type, _name, _field),	\
		.name_size = sizeof (CEPH_XATTR_NAME2(_type, _name, _field)), \
		.getxattr_cb = ceph_vxattrcb_ ## _name ## _ ## _field, \
		.exists_cb = ceph_vxattrcb_layout_exists,	\
		.flags = VXATTR_FLAG_HIDDEN,			\
	}
#define XATTR_QUOTA_FIELD(_type, _name)					\
	{								\
		.name = CEPH_XATTR_NAME(_type, _name),			\
		.name_size = sizeof(CEPH_XATTR_NAME(_type, _name)),	\
		.getxattr_cb = ceph_vxattrcb_ ## _type ## _ ## _name,	\
		.exists_cb = ceph_vxattrcb_quota_exists,		\
		.flags = VXATTR_FLAG_HIDDEN,				\
	}

static struct ceph_vxattr ceph_dir_vxattrs[] = {
	{
		.name = "ceph.dir.layout",
		.name_size = sizeof("ceph.dir.layout"),
		.getxattr_cb = ceph_vxattrcb_layout,
		.exists_cb = ceph_vxattrcb_layout_exists,
		.flags = VXATTR_FLAG_HIDDEN,
	},
	XATTR_LAYOUT_FIELD(dir, layout, stripe_unit),
	XATTR_LAYOUT_FIELD(dir, layout, stripe_count),
	XATTR_LAYOUT_FIELD(dir, layout, object_size),
	XATTR_LAYOUT_FIELD(dir, layout, pool),
	XATTR_LAYOUT_FIELD(dir, layout, pool_namespace),
	XATTR_NAME_CEPH(dir, entries, 0),
	XATTR_NAME_CEPH(dir, files, 0),
	XATTR_NAME_CEPH(dir, subdirs, 0),
	XATTR_RSTAT_FIELD(dir, rentries),
	XATTR_RSTAT_FIELD(dir, rfiles),
	XATTR_RSTAT_FIELD(dir, rsubdirs),
	XATTR_RSTAT_FIELD(dir, rbytes),
	XATTR_RSTAT_FIELD(dir, rctime),
	{
		.name = "ceph.quota",
		.name_size = sizeof("ceph.quota"),
		.getxattr_cb = ceph_vxattrcb_quota,
		.exists_cb = ceph_vxattrcb_quota_exists,
		.flags = VXATTR_FLAG_HIDDEN,
	},
	XATTR_QUOTA_FIELD(quota, max_bytes),
	XATTR_QUOTA_FIELD(quota, max_files),
	{ .name = NULL, 0 }	/* Required table terminator */
};
static size_t ceph_dir_vxattrs_name_size;	/* total size of all names */

/* files */

static struct ceph_vxattr ceph_file_vxattrs[] = {
	{
		.name = "ceph.file.layout",
		.name_size = sizeof("ceph.file.layout"),
		.getxattr_cb = ceph_vxattrcb_layout,
		.exists_cb = ceph_vxattrcb_layout_exists,
		.flags = VXATTR_FLAG_HIDDEN,
	},
	XATTR_LAYOUT_FIELD(file, layout, stripe_unit),
	XATTR_LAYOUT_FIELD(file, layout, stripe_count),
	XATTR_LAYOUT_FIELD(file, layout, object_size),
	XATTR_LAYOUT_FIELD(file, layout, pool),
	XATTR_LAYOUT_FIELD(file, layout, pool_namespace),
	{ .name = NULL, 0 }	/* Required table terminator */
};
static size_t ceph_file_vxattrs_name_size;	/* total size of all names */

static struct ceph_vxattr *ceph_inode_vxattrs(struct inode *inode)
{
	if (S_ISDIR(inode->i_mode))
		return ceph_dir_vxattrs;
	else if (S_ISREG(inode->i_mode))
		return ceph_file_vxattrs;
	return NULL;
}

static size_t ceph_vxattrs_name_size(struct ceph_vxattr *vxattrs)
{
	if (vxattrs == ceph_dir_vxattrs)
		return ceph_dir_vxattrs_name_size;
	if (vxattrs == ceph_file_vxattrs)
		return ceph_file_vxattrs_name_size;
	BUG_ON(vxattrs);
	return 0;
}

/*
 * Compute the aggregate size (including terminating '\0') of all
 * virtual extended attribute names in the given vxattr table.
 */
static size_t __init vxattrs_name_size(struct ceph_vxattr *vxattrs)
{
	struct ceph_vxattr *vxattr;
	size_t size = 0;

	for (vxattr = vxattrs; vxattr->name; vxattr++) {
		if (!(vxattr->flags & VXATTR_FLAG_HIDDEN))
			size += vxattr->name_size;
	}

	return size;
}

/* Routines called at initialization and exit time */

void __init ceph_xattr_init(void)
{
	ceph_dir_vxattrs_name_size = vxattrs_name_size(ceph_dir_vxattrs);
	ceph_file_vxattrs_name_size = vxattrs_name_size(ceph_file_vxattrs);
}

void ceph_xattr_exit(void)
{
	ceph_dir_vxattrs_name_size = 0;
	ceph_file_vxattrs_name_size = 0;
}

static struct ceph_vxattr *ceph_match_vxattr(struct inode *inode,
						const char *name)
{
	struct ceph_vxattr *vxattr = ceph_inode_vxattrs(inode);

	if (vxattr) {
		while (vxattr->name) {
			if (!strcmp(vxattr->name, name))
				return vxattr;
			vxattr++;
		}
	}

	return NULL;
}

static int __set_xattr(struct ceph_inode_info *ci,
			   const char *name, int name_len,
			   const char *val, int val_len,
			   int flags, int update_xattr,
			   struct ceph_inode_xattr **newxattr)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct ceph_inode_xattr *xattr = NULL;
	int c;
	int new = 0;

	p = &ci->i_xattrs.index.rb_node;
	while (*p) {
		parent = *p;
		xattr = rb_entry(parent, struct ceph_inode_xattr, node);
		c = strncmp(name, xattr->name, min(name_len, xattr->name_len));
		if (c < 0)
			p = &(*p)->rb_left;
		else if (c > 0)
			p = &(*p)->rb_right;
		else {
			if (name_len == xattr->name_len)
				break;
			else if (name_len < xattr->name_len)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;
		}
		xattr = NULL;
	}

	if (update_xattr) {
		int err = 0;

		if (xattr && (flags & XATTR_CREATE))
			err = -EEXIST;
		else if (!xattr && (flags & XATTR_REPLACE))
			err = -ENODATA;
		if (err) {
			kfree(name);
			kfree(val);
			kfree(*newxattr);
			return err;
		}
		if (update_xattr < 0) {
			if (xattr)
				__remove_xattr(ci, xattr);
			kfree(name);
			kfree(*newxattr);
			return 0;
		}
	}

	if (!xattr) {
		new = 1;
		xattr = *newxattr;
		xattr->name = name;
		xattr->name_len = name_len;
		xattr->should_free_name = update_xattr;

		ci->i_xattrs.count++;
		dout("__set_xattr count=%d\n", ci->i_xattrs.count);
	} else {
		kfree(*newxattr);
		*newxattr = NULL;
		if (xattr->should_free_val)
			kfree((void *)xattr->val);

		if (update_xattr) {
			kfree((void *)name);
			name = xattr->name;
		}
		ci->i_xattrs.names_size -= xattr->name_len;
		ci->i_xattrs.vals_size -= xattr->val_len;
	}
	ci->i_xattrs.names_size += name_len;
	ci->i_xattrs.vals_size += val_len;
	if (val)
		xattr->val = val;
	else
		xattr->val = "";

	xattr->val_len = val_len;
	xattr->dirty = update_xattr;
	xattr->should_free_val = (val && update_xattr);

	if (new) {
		rb_link_node(&xattr->node, parent, p);
		rb_insert_color(&xattr->node, &ci->i_xattrs.index);
		dout("__set_xattr_val p=%p\n", p);
	}

	dout("__set_xattr_val added %llx.%llx xattr %p %s=%.*s\n",
	     ceph_vinop(&ci->vfs_inode), xattr, name, val_len, val);

	return 0;
}

static struct ceph_inode_xattr *__get_xattr(struct ceph_inode_info *ci,
			   const char *name)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct ceph_inode_xattr *xattr = NULL;
	int name_len = strlen(name);
	int c;

	p = &ci->i_xattrs.index.rb_node;
	while (*p) {
		parent = *p;
		xattr = rb_entry(parent, struct ceph_inode_xattr, node);
		c = strncmp(name, xattr->name, xattr->name_len);
		if (c == 0 && name_len > xattr->name_len)
			c = 1;
		if (c < 0)
			p = &(*p)->rb_left;
		else if (c > 0)
			p = &(*p)->rb_right;
		else {
			dout("__get_xattr %s: found %.*s\n", name,
			     xattr->val_len, xattr->val);
			return xattr;
		}
	}

	dout("__get_xattr %s: not found\n", name);

	return NULL;
}

static void __free_xattr(struct ceph_inode_xattr *xattr)
{
	BUG_ON(!xattr);

	if (xattr->should_free_name)
		kfree((void *)xattr->name);
	if (xattr->should_free_val)
		kfree((void *)xattr->val);

	kfree(xattr);
}

static int __remove_xattr(struct ceph_inode_info *ci,
			  struct ceph_inode_xattr *xattr)
{
	if (!xattr)
		return -ENODATA;

	rb_erase(&xattr->node, &ci->i_xattrs.index);

	if (xattr->should_free_name)
		kfree((void *)xattr->name);
	if (xattr->should_free_val)
		kfree((void *)xattr->val);

	ci->i_xattrs.names_size -= xattr->name_len;
	ci->i_xattrs.vals_size -= xattr->val_len;
	ci->i_xattrs.count--;
	kfree(xattr);

	return 0;
}

static char *__copy_xattr_names(struct ceph_inode_info *ci,
				char *dest)
{
	struct rb_node *p;
	struct ceph_inode_xattr *xattr = NULL;

	p = rb_first(&ci->i_xattrs.index);
	dout("__copy_xattr_names count=%d\n", ci->i_xattrs.count);

	while (p) {
		xattr = rb_entry(p, struct ceph_inode_xattr, node);
		memcpy(dest, xattr->name, xattr->name_len);
		dest[xattr->name_len] = '\0';

		dout("dest=%s %p (%s) (%d/%d)\n", dest, xattr, xattr->name,
		     xattr->name_len, ci->i_xattrs.names_size);

		dest += xattr->name_len + 1;
		p = rb_next(p);
	}

	return dest;
}

void __ceph_destroy_xattrs(struct ceph_inode_info *ci)
{
	struct rb_node *p, *tmp;
	struct ceph_inode_xattr *xattr = NULL;

	p = rb_first(&ci->i_xattrs.index);

	dout("__ceph_destroy_xattrs p=%p\n", p);

	while (p) {
		xattr = rb_entry(p, struct ceph_inode_xattr, node);
		tmp = p;
		p = rb_next(tmp);
		dout("__ceph_destroy_xattrs next p=%p (%.*s)\n", p,
		     xattr->name_len, xattr->name);
		rb_erase(tmp, &ci->i_xattrs.index);

		__free_xattr(xattr);
	}

	ci->i_xattrs.names_size = 0;
	ci->i_xattrs.vals_size = 0;
	ci->i_xattrs.index_version = 0;
	ci->i_xattrs.count = 0;
	ci->i_xattrs.index = RB_ROOT;
}

static int __build_xattrs(struct inode *inode)
	__releases(ci->i_ceph_lock)
	__acquires(ci->i_ceph_lock)
{
	u32 namelen;
	u32 numattr = 0;
	void *p, *end;
	u32 len;
	const char *name, *val;
	struct ceph_inode_info *ci = ceph_inode(inode);
	int xattr_version;
	struct ceph_inode_xattr **xattrs = NULL;
	int err = 0;
	int i;

	dout("__build_xattrs() len=%d\n",
	     ci->i_xattrs.blob ? (int)ci->i_xattrs.blob->vec.iov_len : 0);

	if (ci->i_xattrs.index_version >= ci->i_xattrs.version)
		return 0; /* already built */

	__ceph_destroy_xattrs(ci);

start:
	/* updated internal xattr rb tree */
	if (ci->i_xattrs.blob && ci->i_xattrs.blob->vec.iov_len > 4) {
		p = ci->i_xattrs.blob->vec.iov_base;
		end = p + ci->i_xattrs.blob->vec.iov_len;
		ceph_decode_32_safe(&p, end, numattr, bad);
		xattr_version = ci->i_xattrs.version;
		spin_unlock(&ci->i_ceph_lock);

		xattrs = kcalloc(numattr, sizeof(struct ceph_inode_xattr *),
				 GFP_NOFS);
		err = -ENOMEM;
		if (!xattrs)
			goto bad_lock;

		for (i = 0; i < numattr; i++) {
			xattrs[i] = kmalloc(sizeof(struct ceph_inode_xattr),
					    GFP_NOFS);
			if (!xattrs[i])
				goto bad_lock;
		}

		spin_lock(&ci->i_ceph_lock);
		if (ci->i_xattrs.version != xattr_version) {
			/* lost a race, retry */
			for (i = 0; i < numattr; i++)
				kfree(xattrs[i]);
			kfree(xattrs);
			xattrs = NULL;
			goto start;
		}
		err = -EIO;
		while (numattr--) {
			ceph_decode_32_safe(&p, end, len, bad);
			namelen = len;
			name = p;
			p += len;
			ceph_decode_32_safe(&p, end, len, bad);
			val = p;
			p += len;

			err = __set_xattr(ci, name, namelen, val, len,
					  0, 0, &xattrs[numattr]);

			if (err < 0)
				goto bad;
		}
		kfree(xattrs);
	}
	ci->i_xattrs.index_version = ci->i_xattrs.version;
	ci->i_xattrs.dirty = false;

	return err;
bad_lock:
	spin_lock(&ci->i_ceph_lock);
bad:
	if (xattrs) {
		for (i = 0; i < numattr; i++)
			kfree(xattrs[i]);
		kfree(xattrs);
	}
	ci->i_xattrs.names_size = 0;
	return err;
}

static int __get_required_blob_size(struct ceph_inode_info *ci, int name_size,
				    int val_size)
{
	/*
	 * 4 bytes for the length, and additional 4 bytes per each xattr name,
	 * 4 bytes per each value
	 */
	int size = 4 + ci->i_xattrs.count*(4 + 4) +
			     ci->i_xattrs.names_size +
			     ci->i_xattrs.vals_size;
	dout("__get_required_blob_size c=%d names.size=%d vals.size=%d\n",
	     ci->i_xattrs.count, ci->i_xattrs.names_size,
	     ci->i_xattrs.vals_size);

	if (name_size)
		size += 4 + 4 + name_size + val_size;

	return size;
}

/*
 * If there are dirty xattrs, reencode xattrs into the prealloc_blob
 * and swap into place.
 */
void __ceph_build_xattrs_blob(struct ceph_inode_info *ci)
{
	struct rb_node *p;
	struct ceph_inode_xattr *xattr = NULL;
	void *dest;

	dout("__build_xattrs_blob %p\n", &ci->vfs_inode);
	if (ci->i_xattrs.dirty) {
		int need = __get_required_blob_size(ci, 0, 0);

		BUG_ON(need > ci->i_xattrs.prealloc_blob->alloc_len);

		p = rb_first(&ci->i_xattrs.index);
		dest = ci->i_xattrs.prealloc_blob->vec.iov_base;

		ceph_encode_32(&dest, ci->i_xattrs.count);
		while (p) {
			xattr = rb_entry(p, struct ceph_inode_xattr, node);

			ceph_encode_32(&dest, xattr->name_len);
			memcpy(dest, xattr->name, xattr->name_len);
			dest += xattr->name_len;
			ceph_encode_32(&dest, xattr->val_len);
			memcpy(dest, xattr->val, xattr->val_len);
			dest += xattr->val_len;

			p = rb_next(p);
		}

		/* adjust buffer len; it may be larger than we need */
		ci->i_xattrs.prealloc_blob->vec.iov_len =
			dest - ci->i_xattrs.prealloc_blob->vec.iov_base;

		if (ci->i_xattrs.blob)
			ceph_buffer_put(ci->i_xattrs.blob);
		ci->i_xattrs.blob = ci->i_xattrs.prealloc_blob;
		ci->i_xattrs.prealloc_blob = NULL;
		ci->i_xattrs.dirty = false;
		ci->i_xattrs.version++;
	}
}

static inline int __get_request_mask(struct inode *in) {
	struct ceph_mds_request *req = current->journal_info;
	int mask = 0;
	if (req && req->r_target_inode == in) {
		if (req->r_op == CEPH_MDS_OP_LOOKUP ||
		    req->r_op == CEPH_MDS_OP_LOOKUPINO ||
		    req->r_op == CEPH_MDS_OP_LOOKUPPARENT ||
		    req->r_op == CEPH_MDS_OP_GETATTR) {
			mask = le32_to_cpu(req->r_args.getattr.mask);
		} else if (req->r_op == CEPH_MDS_OP_OPEN ||
			   req->r_op == CEPH_MDS_OP_CREATE) {
			mask = le32_to_cpu(req->r_args.open.mask);
		}
	}
	return mask;
}

ssize_t __ceph_getxattr(struct inode *inode, const char *name, void *value,
		      size_t size)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_inode_xattr *xattr;
	struct ceph_vxattr *vxattr = NULL;
	int req_mask;
	int err;

	/* let's see if a virtual xattr was requested */
	vxattr = ceph_match_vxattr(inode, name);
	if (vxattr) {
		int mask = 0;
		if (vxattr->flags & VXATTR_FLAG_RSTAT)
			mask |= CEPH_STAT_RSTAT;
		err = ceph_do_getattr(inode, mask, true);
		if (err)
			return err;
		err = -ENODATA;
		if (!(vxattr->exists_cb && !vxattr->exists_cb(ci)))
			err = vxattr->getxattr_cb(ci, value, size);
		return err;
	}

	req_mask = __get_request_mask(inode);

	spin_lock(&ci->i_ceph_lock);
	dout("getxattr %p ver=%lld index_ver=%lld\n", inode,
	     ci->i_xattrs.version, ci->i_xattrs.index_version);

	if (ci->i_xattrs.version == 0 ||
	    !((req_mask & CEPH_CAP_XATTR_SHARED) ||
	      __ceph_caps_issued_mask(ci, CEPH_CAP_XATTR_SHARED, 1))) {
		spin_unlock(&ci->i_ceph_lock);

		/* security module gets xattr while filling trace */
		if (current->journal_info) {
			pr_warn_ratelimited("sync getxattr %p "
					    "during filling trace\n", inode);
			return -EBUSY;
		}

		/* get xattrs from mds (if we don't already have them) */
		err = ceph_do_getattr(inode, CEPH_STAT_CAP_XATTR, true);
		if (err)
			return err;
		spin_lock(&ci->i_ceph_lock);
	}

	err = __build_xattrs(inode);
	if (err < 0)
		goto out;

	err = -ENODATA;  /* == ENOATTR */
	xattr = __get_xattr(ci, name);
	if (!xattr)
		goto out;

	err = -ERANGE;
	if (size && size < xattr->val_len)
		goto out;

	err = xattr->val_len;
	if (size == 0)
		goto out;

	memcpy(value, xattr->val, xattr->val_len);

	if (current->journal_info &&
	    !strncmp(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN))
		ci->i_ceph_flags |= CEPH_I_SEC_INITED;
out:
	spin_unlock(&ci->i_ceph_lock);
	return err;
}

ssize_t ceph_listxattr(struct dentry *dentry, char *names, size_t size)
{
	struct inode *inode = d_inode(dentry);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_vxattr *vxattrs = ceph_inode_vxattrs(inode);
	u32 vir_namelen = 0;
	u32 namelen;
	int err;
	u32 len;
	int i;

	spin_lock(&ci->i_ceph_lock);
	dout("listxattr %p ver=%lld index_ver=%lld\n", inode,
	     ci->i_xattrs.version, ci->i_xattrs.index_version);

	if (ci->i_xattrs.version == 0 ||
	    !__ceph_caps_issued_mask(ci, CEPH_CAP_XATTR_SHARED, 1)) {
		spin_unlock(&ci->i_ceph_lock);
		err = ceph_do_getattr(inode, CEPH_STAT_CAP_XATTR, true);
		if (err)
			return err;
		spin_lock(&ci->i_ceph_lock);
	}

	err = __build_xattrs(inode);
	if (err < 0)
		goto out;
	/*
	 * Start with virtual dir xattr names (if any) (including
	 * terminating '\0' characters for each).
	 */
	vir_namelen = ceph_vxattrs_name_size(vxattrs);

	/* adding 1 byte per each variable due to the null termination */
	namelen = ci->i_xattrs.names_size + ci->i_xattrs.count;
	err = -ERANGE;
	if (size && vir_namelen + namelen > size)
		goto out;

	err = namelen + vir_namelen;
	if (size == 0)
		goto out;

	names = __copy_xattr_names(ci, names);

	/* virtual xattr names, too */
	err = namelen;
	if (vxattrs) {
		for (i = 0; vxattrs[i].name; i++) {
			if (!(vxattrs[i].flags & VXATTR_FLAG_HIDDEN) &&
			    !(vxattrs[i].exists_cb &&
			      !vxattrs[i].exists_cb(ci))) {
				len = sprintf(names, "%s", vxattrs[i].name);
				names += len + 1;
				err += len + 1;
			}
		}
	}

out:
	spin_unlock(&ci->i_ceph_lock);
	return err;
}

static int ceph_sync_setxattr(struct inode *inode, const char *name,
			      const char *value, size_t size, int flags)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(inode->i_sb);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_request *req;
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_pagelist *pagelist = NULL;
	int op = CEPH_MDS_OP_SETXATTR;
	int err;

	if (size > 0) {
		/* copy value into pagelist */
		pagelist = ceph_pagelist_alloc(GFP_NOFS);
		if (!pagelist)
			return -ENOMEM;

		err = ceph_pagelist_append(pagelist, value, size);
		if (err)
			goto out;
	} else if (!value) {
		if (flags & CEPH_XATTR_REPLACE)
			op = CEPH_MDS_OP_RMXATTR;
		else
			flags |= CEPH_XATTR_REMOVE;
	}

	dout("setxattr value=%.*s\n", (int)size, value);

	/* do request */
	req = ceph_mdsc_create_request(mdsc, op, USE_AUTH_MDS);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out;
	}

	req->r_path2 = kstrdup(name, GFP_NOFS);
	if (!req->r_path2) {
		ceph_mdsc_put_request(req);
		err = -ENOMEM;
		goto out;
	}

	if (op == CEPH_MDS_OP_SETXATTR) {
		req->r_args.setxattr.flags = cpu_to_le32(flags);
		req->r_pagelist = pagelist;
		pagelist = NULL;
	}

	req->r_inode = inode;
	ihold(inode);
	req->r_num_caps = 1;
	req->r_inode_drop = CEPH_CAP_XATTR_SHARED;

	dout("xattr.ver (before): %lld\n", ci->i_xattrs.version);
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	ceph_mdsc_put_request(req);
	dout("xattr.ver (after): %lld\n", ci->i_xattrs.version);

out:
	if (pagelist)
		ceph_pagelist_release(pagelist);
	return err;
}

int __ceph_setxattr(struct inode *inode, const char *name,
			const void *value, size_t size, int flags)
{
	struct ceph_vxattr *vxattr;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_cap_flush *prealloc_cf = NULL;
	int issued;
	int err;
	int dirty = 0;
	int name_len = strlen(name);
	int val_len = size;
	char *newname = NULL;
	char *newval = NULL;
	struct ceph_inode_xattr *xattr = NULL;
	int required_blob_size;
	bool check_realm = false;
	bool lock_snap_rwsem = false;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EROFS;

	vxattr = ceph_match_vxattr(inode, name);
	if (vxattr) {
		if (vxattr->flags & VXATTR_FLAG_READONLY)
			return -EOPNOTSUPP;
		if (value && !strncmp(vxattr->name, "ceph.quota", 10))
			check_realm = true;
	}

	/* pass any unhandled ceph.* xattrs through to the MDS */
	if (!strncmp(name, XATTR_CEPH_PREFIX, XATTR_CEPH_PREFIX_LEN))
		goto do_sync_unlocked;

	/* preallocate memory for xattr name, value, index node */
	err = -ENOMEM;
	newname = kmemdup(name, name_len + 1, GFP_NOFS);
	if (!newname)
		goto out;

	if (val_len) {
		newval = kmemdup(value, val_len, GFP_NOFS);
		if (!newval)
			goto out;
	}

	xattr = kmalloc(sizeof(struct ceph_inode_xattr), GFP_NOFS);
	if (!xattr)
		goto out;

	prealloc_cf = ceph_alloc_cap_flush();
	if (!prealloc_cf)
		goto out;

	spin_lock(&ci->i_ceph_lock);
retry:
	issued = __ceph_caps_issued(ci, NULL);
	if (ci->i_xattrs.version == 0 || !(issued & CEPH_CAP_XATTR_EXCL))
		goto do_sync;

	if (!lock_snap_rwsem && !ci->i_head_snapc) {
		lock_snap_rwsem = true;
		if (!down_read_trylock(&mdsc->snap_rwsem)) {
			spin_unlock(&ci->i_ceph_lock);
			down_read(&mdsc->snap_rwsem);
			spin_lock(&ci->i_ceph_lock);
			goto retry;
		}
	}

	dout("setxattr %p issued %s\n", inode, ceph_cap_string(issued));
	__build_xattrs(inode);

	required_blob_size = __get_required_blob_size(ci, name_len, val_len);

	if (!ci->i_xattrs.prealloc_blob ||
	    required_blob_size > ci->i_xattrs.prealloc_blob->alloc_len) {
		struct ceph_buffer *blob;

		spin_unlock(&ci->i_ceph_lock);
		dout(" preaallocating new blob size=%d\n", required_blob_size);
		blob = ceph_buffer_new(required_blob_size, GFP_NOFS);
		if (!blob)
			goto do_sync_unlocked;
		spin_lock(&ci->i_ceph_lock);
		if (ci->i_xattrs.prealloc_blob)
			ceph_buffer_put(ci->i_xattrs.prealloc_blob);
		ci->i_xattrs.prealloc_blob = blob;
		goto retry;
	}

	err = __set_xattr(ci, newname, name_len, newval, val_len,
			  flags, value ? 1 : -1, &xattr);

	if (!err) {
		dirty = __ceph_mark_dirty_caps(ci, CEPH_CAP_XATTR_EXCL,
					       &prealloc_cf);
		ci->i_xattrs.dirty = true;
		inode->i_ctime = current_time(inode);
	}

	spin_unlock(&ci->i_ceph_lock);
	if (lock_snap_rwsem)
		up_read(&mdsc->snap_rwsem);
	if (dirty)
		__mark_inode_dirty(inode, dirty);
	ceph_free_cap_flush(prealloc_cf);
	return err;

do_sync:
	spin_unlock(&ci->i_ceph_lock);
do_sync_unlocked:
	if (lock_snap_rwsem)
		up_read(&mdsc->snap_rwsem);

	/* security module set xattr while filling trace */
	if (current->journal_info) {
		pr_warn_ratelimited("sync setxattr %p "
				    "during filling trace\n", inode);
		err = -EBUSY;
	} else {
		err = ceph_sync_setxattr(inode, name, value, size, flags);
		if (err >= 0 && check_realm) {
			/* check if snaprealm was created for quota inode */
			spin_lock(&ci->i_ceph_lock);
			if ((ci->i_max_files || ci->i_max_bytes) &&
			    !(ci->i_snap_realm &&
			      ci->i_snap_realm->ino == ci->i_vino.ino))
				err = -EOPNOTSUPP;
			spin_unlock(&ci->i_ceph_lock);
		}
	}
out:
	ceph_free_cap_flush(prealloc_cf);
	kfree(newname);
	kfree(newval);
	kfree(xattr);
	return err;
}

static int ceph_get_xattr_handler(const struct xattr_handler *handler,
				  struct dentry *dentry, struct inode *inode,
				  const char *name, void *value, size_t size)
{
	if (!ceph_is_valid_xattr(name))
		return -EOPNOTSUPP;
	return __ceph_getxattr(inode, name, value, size);
}

static int ceph_set_xattr_handler(const struct xattr_handler *handler,
				  struct dentry *unused, struct inode *inode,
				  const char *name, const void *value,
				  size_t size, int flags)
{
	if (!ceph_is_valid_xattr(name))
		return -EOPNOTSUPP;
	return __ceph_setxattr(inode, name, value, size, flags);
}

static const struct xattr_handler ceph_other_xattr_handler = {
	.prefix = "",  /* match any name => handlers called with full name */
	.get = ceph_get_xattr_handler,
	.set = ceph_set_xattr_handler,
};

#ifdef CONFIG_SECURITY
bool ceph_security_xattr_wanted(struct inode *in)
{
	return in->i_security != NULL;
}

bool ceph_security_xattr_deadlock(struct inode *in)
{
	struct ceph_inode_info *ci;
	bool ret;
	if (!in->i_security)
		return false;
	ci = ceph_inode(in);
	spin_lock(&ci->i_ceph_lock);
	ret = !(ci->i_ceph_flags & CEPH_I_SEC_INITED) &&
	      !(ci->i_xattrs.version > 0 &&
		__ceph_caps_issued_mask(ci, CEPH_CAP_XATTR_SHARED, 0));
	spin_unlock(&ci->i_ceph_lock);
	return ret;
}
#endif
