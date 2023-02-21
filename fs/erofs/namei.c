// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2022, Alibaba Cloud
 */
#include "xattr.h"

#include <trace/events/erofs.h>

struct erofs_qstr {
	const unsigned char *name;
	const unsigned char *end;
};

/* based on the end of qn is accurate and it must have the trailing '\0' */
static inline int erofs_dirnamecmp(const struct erofs_qstr *qn,
				   const struct erofs_qstr *qd,
				   unsigned int *matched)
{
	unsigned int i = *matched;

	/*
	 * on-disk error, let's only BUG_ON in the debugging mode.
	 * otherwise, it will return 1 to just skip the invalid name
	 * and go on (in consideration of the lookup performance).
	 */
	DBG_BUGON(qd->name > qd->end);

	/* qd could not have trailing '\0' */
	/* However it is absolutely safe if < qd->end */
	while (qd->name + i < qd->end && qd->name[i] != '\0') {
		if (qn->name[i] != qd->name[i]) {
			*matched = i;
			return qn->name[i] > qd->name[i] ? 1 : -1;
		}
		++i;
	}
	*matched = i;
	/* See comments in __d_alloc on the terminating NUL character */
	return qn->name[i] == '\0' ? 0 : 1;
}

#define nameoff_from_disk(off, sz)	(le16_to_cpu(off) & ((sz) - 1))

static struct erofs_dirent *find_target_dirent(struct erofs_qstr *name,
					       u8 *data,
					       unsigned int dirblksize,
					       const int ndirents)
{
	int head, back;
	unsigned int startprfx, endprfx;
	struct erofs_dirent *const de = (struct erofs_dirent *)data;

	/* since the 1st dirent has been evaluated previously */
	head = 1;
	back = ndirents - 1;
	startprfx = endprfx = 0;

	while (head <= back) {
		const int mid = head + (back - head) / 2;
		const int nameoff = nameoff_from_disk(de[mid].nameoff,
						      dirblksize);
		unsigned int matched = min(startprfx, endprfx);
		struct erofs_qstr dname = {
			.name = data + nameoff,
			.end = mid >= ndirents - 1 ?
				data + dirblksize :
				data + nameoff_from_disk(de[mid + 1].nameoff,
							 dirblksize)
		};

		/* string comparison without already matched prefix */
		int ret = erofs_dirnamecmp(name, &dname, &matched);

		if (!ret) {
			return de + mid;
		} else if (ret > 0) {
			head = mid + 1;
			startprfx = matched;
		} else {
			back = mid - 1;
			endprfx = matched;
		}
	}

	return ERR_PTR(-ENOENT);
}

static void *find_target_block_classic(struct erofs_buf *target,
				       struct inode *dir,
				       struct erofs_qstr *name,
				       int *_ndirents)
{
	unsigned int startprfx, endprfx;
	int head, back;
	void *candidate = ERR_PTR(-ENOENT);

	startprfx = endprfx = 0;
	head = 0;
	back = erofs_inode_datablocks(dir) - 1;

	while (head <= back) {
		const int mid = head + (back - head) / 2;
		struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
		struct erofs_dirent *de;

		de = erofs_bread(&buf, dir, mid, EROFS_KMAP);
		if (!IS_ERR(de)) {
			const int nameoff = nameoff_from_disk(de->nameoff,
							      EROFS_BLKSIZ);
			const int ndirents = nameoff / sizeof(*de);
			int diff;
			unsigned int matched;
			struct erofs_qstr dname;

			if (!ndirents) {
				erofs_put_metabuf(&buf);
				erofs_err(dir->i_sb,
					  "corrupted dir block %d @ nid %llu",
					  mid, EROFS_I(dir)->nid);
				DBG_BUGON(1);
				de = ERR_PTR(-EFSCORRUPTED);
				goto out;
			}

			matched = min(startprfx, endprfx);

			dname.name = (u8 *)de + nameoff;
			if (ndirents == 1)
				dname.end = (u8 *)de + EROFS_BLKSIZ;
			else
				dname.end = (u8 *)de +
					nameoff_from_disk(de[1].nameoff,
							  EROFS_BLKSIZ);

			/* string comparison without already matched prefix */
			diff = erofs_dirnamecmp(name, &dname, &matched);

			if (!diff) {
				*_ndirents = 0;
				goto out;
			} else if (diff > 0) {
				head = mid + 1;
				startprfx = matched;

				if (!IS_ERR(candidate))
					erofs_put_metabuf(target);
				*target = buf;
				candidate = de;
				*_ndirents = ndirents;
			} else {
				erofs_put_metabuf(&buf);

				back = mid - 1;
				endprfx = matched;
			}
			continue;
		}
out:		/* free if the candidate is valid */
		if (!IS_ERR(candidate))
			erofs_put_metabuf(target);
		return de;
	}
	return candidate;
}

int erofs_namei(struct inode *dir, const struct qstr *name, erofs_nid_t *nid,
		unsigned int *d_type)
{
	int ndirents;
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	struct erofs_dirent *de;
	struct erofs_qstr qn;

	if (!dir->i_size)
		return -ENOENT;

	qn.name = name->name;
	qn.end = name->name + name->len;

	ndirents = 0;

	de = find_target_block_classic(&buf, dir, &qn, &ndirents);
	if (IS_ERR(de))
		return PTR_ERR(de);

	if (ndirents)
		de = find_target_dirent(&qn, (u8 *)de, EROFS_BLKSIZ, ndirents);

	if (!IS_ERR(de)) {
		*nid = le64_to_cpu(de->nid);
		*d_type = de->file_type;
	}
	erofs_put_metabuf(&buf);
	return PTR_ERR_OR_ZERO(de);
}

static struct dentry *erofs_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	int err;
	erofs_nid_t nid;
	unsigned int d_type;
	struct inode *inode;

	trace_erofs_lookup(dir, dentry, flags);

	if (dentry->d_name.len > EROFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	err = erofs_namei(dir, &dentry->d_name, &nid, &d_type);

	if (err == -ENOENT) {
		/* negative dentry */
		inode = NULL;
	} else if (err) {
		inode = ERR_PTR(err);
	} else {
		erofs_dbg("%s, %pd (nid %llu) found, d_type %u", __func__,
			  dentry, nid, d_type);
		inode = erofs_iget(dir->i_sb, nid);
	}
	return d_splice_alias(inode, dentry);
}

const struct inode_operations erofs_dir_iops = {
	.lookup = erofs_lookup,
	.getattr = erofs_getattr,
	.listxattr = erofs_listxattr,
	.get_inode_acl = erofs_get_acl,
	.fiemap = erofs_fiemap,
};
