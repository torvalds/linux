#include "ceph_debug.h"
#include "super.h"
#include "decode.h"

#include <linux/xattr.h>
#include <linux/slab.h>

static bool ceph_is_valid_xattr(const char *name)
{
	return !strncmp(name, "ceph.", 5) ||
	       !strncmp(name, XATTR_SECURITY_PREFIX,
			XATTR_SECURITY_PREFIX_LEN) ||
	       !strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN) ||
	       !strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
}

/*
 * These define virtual xattrs exposing the recursive directory
 * statistics and layout metadata.
 */
struct ceph_vxattr_cb {
	bool readonly;
	char *name;
	size_t (*getxattr_cb)(struct ceph_inode_info *ci, char *val,
			      size_t size);
};

/* directories */

static size_t ceph_vxattrcb_entries(struct ceph_inode_info *ci, char *val,
					size_t size)
{
	return snprintf(val, size, "%lld", ci->i_files + ci->i_subdirs);
}

static size_t ceph_vxattrcb_files(struct ceph_inode_info *ci, char *val,
				      size_t size)
{
	return snprintf(val, size, "%lld", ci->i_files);
}

static size_t ceph_vxattrcb_subdirs(struct ceph_inode_info *ci, char *val,
					size_t size)
{
	return snprintf(val, size, "%lld", ci->i_subdirs);
}

static size_t ceph_vxattrcb_rentries(struct ceph_inode_info *ci, char *val,
					 size_t size)
{
	return snprintf(val, size, "%lld", ci->i_rfiles + ci->i_rsubdirs);
}

static size_t ceph_vxattrcb_rfiles(struct ceph_inode_info *ci, char *val,
				       size_t size)
{
	return snprintf(val, size, "%lld", ci->i_rfiles);
}

static size_t ceph_vxattrcb_rsubdirs(struct ceph_inode_info *ci, char *val,
					 size_t size)
{
	return snprintf(val, size, "%lld", ci->i_rsubdirs);
}

static size_t ceph_vxattrcb_rbytes(struct ceph_inode_info *ci, char *val,
				       size_t size)
{
	return snprintf(val, size, "%lld", ci->i_rbytes);
}

static size_t ceph_vxattrcb_rctime(struct ceph_inode_info *ci, char *val,
				       size_t size)
{
	return snprintf(val, size, "%ld.%ld", (long)ci->i_rctime.tv_sec,
			(long)ci->i_rctime.tv_nsec);
}

static struct ceph_vxattr_cb ceph_dir_vxattrs[] = {
	{ true, "ceph.dir.entries", ceph_vxattrcb_entries},
	{ true, "ceph.dir.files", ceph_vxattrcb_files},
	{ true, "ceph.dir.subdirs", ceph_vxattrcb_subdirs},
	{ true, "ceph.dir.rentries", ceph_vxattrcb_rentries},
	{ true, "ceph.dir.rfiles", ceph_vxattrcb_rfiles},
	{ true, "ceph.dir.rsubdirs", ceph_vxattrcb_rsubdirs},
	{ true, "ceph.dir.rbytes", ceph_vxattrcb_rbytes},
	{ true, "ceph.dir.rctime", ceph_vxattrcb_rctime},
	{ true, NULL, NULL }
};

/* files */

static size_t ceph_vxattrcb_layout(struct ceph_inode_info *ci, char *val,
				   size_t size)
{
	int ret;

	ret = snprintf(val, size,
		"chunk_bytes=%lld\nstripe_count=%lld\nobject_size=%lld\n",
		(unsigned long long)ceph_file_layout_su(ci->i_layout),
		(unsigned long long)ceph_file_layout_stripe_count(ci->i_layout),
		(unsigned long long)ceph_file_layout_object_size(ci->i_layout));
	if (ceph_file_layout_pg_preferred(ci->i_layout))
		ret += snprintf(val + ret, size, "preferred_osd=%lld\n",
			    (unsigned long long)ceph_file_layout_pg_preferred(
				    ci->i_layout));
	return ret;
}

static struct ceph_vxattr_cb ceph_file_vxattrs[] = {
	{ true, "ceph.layout", ceph_vxattrcb_layout},
	{ NULL, NULL }
};

static struct ceph_vxattr_cb *ceph_inode_vxattrs(struct inode *inode)
{
	if (S_ISDIR(inode->i_mode))
		return ceph_dir_vxattrs;
	else if (S_ISREG(inode->i_mode))
		return ceph_file_vxattrs;
	return NULL;
}

static struct ceph_vxattr_cb *ceph_match_vxattr(struct ceph_vxattr_cb *vxattr,
						const char *name)
{
	do {
		if (strcmp(vxattr->name, name) == 0)
			return vxattr;
		vxattr++;
	} while (vxattr->name);
	return NULL;
}

static int __set_xattr(struct ceph_inode_info *ci,
			   const char *name, int name_len,
			   const char *val, int val_len,
			   int dirty,
			   int should_free_name, int should_free_val,
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

	if (!xattr) {
		new = 1;
		xattr = *newxattr;
		xattr->name = name;
		xattr->name_len = name_len;
		xattr->should_free_name = should_free_name;

		ci->i_xattrs.count++;
		dout("__set_xattr count=%d\n", ci->i_xattrs.count);
	} else {
		kfree(*newxattr);
		*newxattr = NULL;
		if (xattr->should_free_val)
			kfree((void *)xattr->val);

		if (should_free_name) {
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
	xattr->dirty = dirty;
	xattr->should_free_val = (val && should_free_val);

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
	int c;

	p = &ci->i_xattrs.index.rb_node;
	while (*p) {
		parent = *p;
		xattr = rb_entry(parent, struct ceph_inode_xattr, node);
		c = strncmp(name, xattr->name, xattr->name_len);
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
		return -EOPNOTSUPP;

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

static int __remove_xattr_by_name(struct ceph_inode_info *ci,
			   const char *name)
{
	struct rb_node **p;
	struct ceph_inode_xattr *xattr;
	int err;

	p = &ci->i_xattrs.index.rb_node;
	xattr = __get_xattr(ci, name);
	err = __remove_xattr(ci, xattr);
	return err;
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
	__releases(inode->i_lock)
	__acquires(inode->i_lock)
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
		spin_unlock(&inode->i_lock);

		xattrs = kcalloc(numattr, sizeof(struct ceph_xattr *),
				 GFP_NOFS);
		err = -ENOMEM;
		if (!xattrs)
			goto bad_lock;
		memset(xattrs, 0, numattr*sizeof(struct ceph_xattr *));
		for (i = 0; i < numattr; i++) {
			xattrs[i] = kmalloc(sizeof(struct ceph_inode_xattr),
					    GFP_NOFS);
			if (!xattrs[i])
				goto bad_lock;
		}

		spin_lock(&inode->i_lock);
		if (ci->i_xattrs.version != xattr_version) {
			/* lost a race, retry */
			for (i = 0; i < numattr; i++)
				kfree(xattrs[i]);
			kfree(xattrs);
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
					  0, 0, 0, &xattrs[numattr]);

			if (err < 0)
				goto bad;
		}
		kfree(xattrs);
	}
	ci->i_xattrs.index_version = ci->i_xattrs.version;
	ci->i_xattrs.dirty = false;

	return err;
bad_lock:
	spin_lock(&inode->i_lock);
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

ssize_t ceph_getxattr(struct dentry *dentry, const char *name, void *value,
		      size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_vxattr_cb *vxattrs = ceph_inode_vxattrs(inode);
	int err;
	struct ceph_inode_xattr *xattr;
	struct ceph_vxattr_cb *vxattr = NULL;

	if (!ceph_is_valid_xattr(name))
		return -ENODATA;

	/* let's see if a virtual xattr was requested */
	if (vxattrs)
		vxattr = ceph_match_vxattr(vxattrs, name);

	spin_lock(&inode->i_lock);
	dout("getxattr %p ver=%lld index_ver=%lld\n", inode,
	     ci->i_xattrs.version, ci->i_xattrs.index_version);

	if (__ceph_caps_issued_mask(ci, CEPH_CAP_XATTR_SHARED, 1) &&
	    (ci->i_xattrs.index_version >= ci->i_xattrs.version)) {
		goto get_xattr;
	} else {
		spin_unlock(&inode->i_lock);
		/* get xattrs from mds (if we don't already have them) */
		err = ceph_do_getattr(inode, CEPH_STAT_CAP_XATTR);
		if (err)
			return err;
	}

	spin_lock(&inode->i_lock);

	if (vxattr && vxattr->readonly) {
		err = vxattr->getxattr_cb(ci, value, size);
		goto out;
	}

	err = __build_xattrs(inode);
	if (err < 0)
		goto out;

get_xattr:
	err = -ENODATA;  /* == ENOATTR */
	xattr = __get_xattr(ci, name);
	if (!xattr) {
		if (vxattr)
			err = vxattr->getxattr_cb(ci, value, size);
		goto out;
	}

	err = -ERANGE;
	if (size && size < xattr->val_len)
		goto out;

	err = xattr->val_len;
	if (size == 0)
		goto out;

	memcpy(value, xattr->val, xattr->val_len);

out:
	spin_unlock(&inode->i_lock);
	return err;
}

ssize_t ceph_listxattr(struct dentry *dentry, char *names, size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_vxattr_cb *vxattrs = ceph_inode_vxattrs(inode);
	u32 vir_namelen = 0;
	u32 namelen;
	int err;
	u32 len;
	int i;

	spin_lock(&inode->i_lock);
	dout("listxattr %p ver=%lld index_ver=%lld\n", inode,
	     ci->i_xattrs.version, ci->i_xattrs.index_version);

	if (__ceph_caps_issued_mask(ci, CEPH_CAP_XATTR_SHARED, 1) &&
	    (ci->i_xattrs.index_version >= ci->i_xattrs.version)) {
		goto list_xattr;
	} else {
		spin_unlock(&inode->i_lock);
		err = ceph_do_getattr(inode, CEPH_STAT_CAP_XATTR);
		if (err)
			return err;
	}

	spin_lock(&inode->i_lock);

	err = __build_xattrs(inode);
	if (err < 0)
		goto out;

list_xattr:
	vir_namelen = 0;
	/* include virtual dir xattrs */
	if (vxattrs)
		for (i = 0; vxattrs[i].name; i++)
			vir_namelen += strlen(vxattrs[i].name) + 1;
	/* adding 1 byte per each variable due to the null termination */
	namelen = vir_namelen + ci->i_xattrs.names_size + ci->i_xattrs.count;
	err = -ERANGE;
	if (size && namelen > size)
		goto out;

	err = namelen;
	if (size == 0)
		goto out;

	names = __copy_xattr_names(ci, names);

	/* virtual xattr names, too */
	if (vxattrs)
		for (i = 0; vxattrs[i].name; i++) {
			len = sprintf(names, "%s", vxattrs[i].name);
			names += len + 1;
		}

out:
	spin_unlock(&inode->i_lock);
	return err;
}

static int ceph_sync_setxattr(struct dentry *dentry, const char *name,
			      const char *value, size_t size, int flags)
{
	struct ceph_client *client = ceph_sb_to_client(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct inode *parent_inode = dentry->d_parent->d_inode;
	struct ceph_mds_request *req;
	struct ceph_mds_client *mdsc = &client->mdsc;
	int err;
	int i, nr_pages;
	struct page **pages = NULL;
	void *kaddr;

	/* copy value into some pages */
	nr_pages = calc_pages_for(0, size);
	if (nr_pages) {
		pages = kmalloc(sizeof(pages[0])*nr_pages, GFP_NOFS);
		if (!pages)
			return -ENOMEM;
		err = -ENOMEM;
		for (i = 0; i < nr_pages; i++) {
			pages[i] = __page_cache_alloc(GFP_NOFS);
			if (!pages[i]) {
				nr_pages = i;
				goto out;
			}
			kaddr = kmap(pages[i]);
			memcpy(kaddr, value + i*PAGE_CACHE_SIZE,
			       min(PAGE_CACHE_SIZE, size-i*PAGE_CACHE_SIZE));
		}
	}

	dout("setxattr value=%.*s\n", (int)size, value);

	/* do request */
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SETXATTR,
				       USE_AUTH_MDS);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out;
	}
	req->r_inode = igrab(inode);
	req->r_inode_drop = CEPH_CAP_XATTR_SHARED;
	req->r_num_caps = 1;
	req->r_args.setxattr.flags = cpu_to_le32(flags);
	req->r_path2 = kstrdup(name, GFP_NOFS);

	req->r_pages = pages;
	req->r_num_pages = nr_pages;
	req->r_data_len = size;

	dout("xattr.ver (before): %lld\n", ci->i_xattrs.version);
	err = ceph_mdsc_do_request(mdsc, parent_inode, req);
	ceph_mdsc_put_request(req);
	dout("xattr.ver (after): %lld\n", ci->i_xattrs.version);

out:
	if (pages) {
		for (i = 0; i < nr_pages; i++)
			__free_page(pages[i]);
		kfree(pages);
	}
	return err;
}

int ceph_setxattr(struct dentry *dentry, const char *name,
		  const void *value, size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_vxattr_cb *vxattrs = ceph_inode_vxattrs(inode);
	int err;
	int name_len = strlen(name);
	int val_len = size;
	char *newname = NULL;
	char *newval = NULL;
	struct ceph_inode_xattr *xattr = NULL;
	int issued;
	int required_blob_size;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EROFS;

	if (!ceph_is_valid_xattr(name))
		return -EOPNOTSUPP;

	if (vxattrs) {
		struct ceph_vxattr_cb *vxattr =
			ceph_match_vxattr(vxattrs, name);
		if (vxattr && vxattr->readonly)
			return -EOPNOTSUPP;
	}

	/* preallocate memory for xattr name, value, index node */
	err = -ENOMEM;
	newname = kmalloc(name_len + 1, GFP_NOFS);
	if (!newname)
		goto out;
	memcpy(newname, name, name_len + 1);

	if (val_len) {
		newval = kmalloc(val_len + 1, GFP_NOFS);
		if (!newval)
			goto out;
		memcpy(newval, value, val_len);
		newval[val_len] = '\0';
	}

	xattr = kmalloc(sizeof(struct ceph_inode_xattr), GFP_NOFS);
	if (!xattr)
		goto out;

	spin_lock(&inode->i_lock);
retry:
	issued = __ceph_caps_issued(ci, NULL);
	if (!(issued & CEPH_CAP_XATTR_EXCL))
		goto do_sync;
	__build_xattrs(inode);

	required_blob_size = __get_required_blob_size(ci, name_len, val_len);

	if (!ci->i_xattrs.prealloc_blob ||
	    required_blob_size > ci->i_xattrs.prealloc_blob->alloc_len) {
		struct ceph_buffer *blob = NULL;

		spin_unlock(&inode->i_lock);
		dout(" preaallocating new blob size=%d\n", required_blob_size);
		blob = ceph_buffer_new(required_blob_size, GFP_NOFS);
		if (!blob)
			goto out;
		spin_lock(&inode->i_lock);
		if (ci->i_xattrs.prealloc_blob)
			ceph_buffer_put(ci->i_xattrs.prealloc_blob);
		ci->i_xattrs.prealloc_blob = blob;
		goto retry;
	}

	dout("setxattr %p issued %s\n", inode, ceph_cap_string(issued));
	err = __set_xattr(ci, newname, name_len, newval,
			  val_len, 1, 1, 1, &xattr);
	__ceph_mark_dirty_caps(ci, CEPH_CAP_XATTR_EXCL);
	ci->i_xattrs.dirty = true;
	inode->i_ctime = CURRENT_TIME;
	spin_unlock(&inode->i_lock);

	return err;

do_sync:
	spin_unlock(&inode->i_lock);
	err = ceph_sync_setxattr(dentry, name, value, size, flags);
out:
	kfree(newname);
	kfree(newval);
	kfree(xattr);
	return err;
}

static int ceph_send_removexattr(struct dentry *dentry, const char *name)
{
	struct ceph_client *client = ceph_sb_to_client(dentry->d_sb);
	struct ceph_mds_client *mdsc = &client->mdsc;
	struct inode *inode = dentry->d_inode;
	struct inode *parent_inode = dentry->d_parent->d_inode;
	struct ceph_mds_request *req;
	int err;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_RMXATTR,
				       USE_AUTH_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_inode = igrab(inode);
	req->r_inode_drop = CEPH_CAP_XATTR_SHARED;
	req->r_num_caps = 1;
	req->r_path2 = kstrdup(name, GFP_NOFS);

	err = ceph_mdsc_do_request(mdsc, parent_inode, req);
	ceph_mdsc_put_request(req);
	return err;
}

int ceph_removexattr(struct dentry *dentry, const char *name)
{
	struct inode *inode = dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_vxattr_cb *vxattrs = ceph_inode_vxattrs(inode);
	int issued;
	int err;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EROFS;

	if (!ceph_is_valid_xattr(name))
		return -EOPNOTSUPP;

	if (vxattrs) {
		struct ceph_vxattr_cb *vxattr =
			ceph_match_vxattr(vxattrs, name);
		if (vxattr && vxattr->readonly)
			return -EOPNOTSUPP;
	}

	spin_lock(&inode->i_lock);
	__build_xattrs(inode);
	issued = __ceph_caps_issued(ci, NULL);
	dout("removexattr %p issued %s\n", inode, ceph_cap_string(issued));

	if (!(issued & CEPH_CAP_XATTR_EXCL))
		goto do_sync;

	err = __remove_xattr_by_name(ceph_inode(inode), name);
	__ceph_mark_dirty_caps(ci, CEPH_CAP_XATTR_EXCL);
	ci->i_xattrs.dirty = true;
	inode->i_ctime = CURRENT_TIME;

	spin_unlock(&inode->i_lock);

	return err;
do_sync:
	spin_unlock(&inode->i_lock);
	err = ceph_send_removexattr(dentry, name);
	return err;
}

