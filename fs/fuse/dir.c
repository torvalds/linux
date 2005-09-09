/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2005  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/namei.h>

static inline unsigned long time_to_jiffies(unsigned long sec,
					    unsigned long nsec)
{
	struct timespec ts = {sec, nsec};
	return jiffies + timespec_to_jiffies(&ts);
}

static void fuse_lookup_init(struct fuse_req *req, struct inode *dir,
			     struct dentry *entry,
			     struct fuse_entry_out *outarg)
{
	req->in.h.opcode = FUSE_LOOKUP;
	req->in.h.nodeid = get_node_id(dir);
	req->inode = dir;
	req->in.numargs = 1;
	req->in.args[0].size = entry->d_name.len + 1;
	req->in.args[0].value = entry->d_name.name;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(struct fuse_entry_out);
	req->out.args[0].value = outarg;
}

static int fuse_dentry_revalidate(struct dentry *entry, struct nameidata *nd)
{
	if (!entry->d_inode || is_bad_inode(entry->d_inode))
		return 0;
	else if (time_after(jiffies, entry->d_time)) {
		int err;
		int version;
		struct fuse_entry_out outarg;
		struct inode *inode = entry->d_inode;
		struct fuse_inode *fi = get_fuse_inode(inode);
		struct fuse_conn *fc = get_fuse_conn(inode);
		struct fuse_req *req = fuse_get_request_nonint(fc);
		if (!req)
			return 0;

		fuse_lookup_init(req, entry->d_parent->d_inode, entry, &outarg);
		request_send_nonint(fc, req);
		version = req->out.h.unique;
		err = req->out.h.error;
		fuse_put_request(fc, req);
		if (err || outarg.nodeid != get_node_id(inode) ||
		    (outarg.attr.mode ^ inode->i_mode) & S_IFMT)
			return 0;

		fuse_change_attributes(inode, &outarg.attr);
		inode->i_version = version;
		entry->d_time = time_to_jiffies(outarg.entry_valid,
						outarg.entry_valid_nsec);
		fi->i_time = time_to_jiffies(outarg.attr_valid,
					     outarg.attr_valid_nsec);
	}
	return 1;
}

static struct dentry_operations fuse_dentry_operations = {
	.d_revalidate	= fuse_dentry_revalidate,
};

static int fuse_lookup_iget(struct inode *dir, struct dentry *entry,
			    struct inode **inodep)
{
	int err;
	int version;
	struct fuse_entry_out outarg;
	struct inode *inode = NULL;
	struct fuse_conn *fc = get_fuse_conn(dir);
	struct fuse_req *req;

	if (entry->d_name.len > FUSE_NAME_MAX)
		return -ENAMETOOLONG;

	req = fuse_get_request(fc);
	if (!req)
		return -ERESTARTNOINTR;

	fuse_lookup_init(req, dir, entry, &outarg);
	request_send(fc, req);
	version = req->out.h.unique;
	err = req->out.h.error;
	if (!err) {
		inode = fuse_iget(dir->i_sb, outarg.nodeid, outarg.generation,
				  &outarg.attr, version);
		if (!inode) {
			fuse_send_forget(fc, req, outarg.nodeid, version);
			return -ENOMEM;
		}
	}
	fuse_put_request(fc, req);
	if (err && err != -ENOENT)
		return err;

	if (inode) {
		struct fuse_inode *fi = get_fuse_inode(inode);
		entry->d_time =	time_to_jiffies(outarg.entry_valid,
						outarg.entry_valid_nsec);
		fi->i_time = time_to_jiffies(outarg.attr_valid,
					     outarg.attr_valid_nsec);
	}

	entry->d_op = &fuse_dentry_operations;
	*inodep = inode;
	return 0;
}

int fuse_do_getattr(struct inode *inode)
{
	int err;
	struct fuse_attr_out arg;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_req *req = fuse_get_request(fc);
	if (!req)
		return -ERESTARTNOINTR;

	req->in.h.opcode = FUSE_GETATTR;
	req->in.h.nodeid = get_node_id(inode);
	req->inode = inode;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(arg);
	req->out.args[0].value = &arg;
	request_send(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (!err) {
		if ((inode->i_mode ^ arg.attr.mode) & S_IFMT) {
			make_bad_inode(inode);
			err = -EIO;
		} else {
			struct fuse_inode *fi = get_fuse_inode(inode);
			fuse_change_attributes(inode, &arg.attr);
			fi->i_time = time_to_jiffies(arg.attr_valid,
						     arg.attr_valid_nsec);
		}
	}
	return err;
}

static int fuse_revalidate(struct dentry *entry)
{
	struct inode *inode = entry->d_inode;
	struct fuse_inode *fi = get_fuse_inode(inode);
	struct fuse_conn *fc = get_fuse_conn(inode);

	if (get_node_id(inode) == FUSE_ROOT_ID) {
		if (current->fsuid != fc->user_id)
			return -EACCES;
	} else if (time_before_eq(jiffies, fi->i_time))
		return 0;

	return fuse_do_getattr(inode);
}

static int fuse_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	struct fuse_conn *fc = get_fuse_conn(inode);

	if (current->fsuid != fc->user_id)
		return -EACCES;
	else {
		int mode = inode->i_mode;
		if ((mask & MAY_WRITE) && IS_RDONLY(inode) &&
                    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
                        return -EROFS;
		if ((mask & MAY_EXEC) && !S_ISDIR(mode) && !(mode & S_IXUGO))
			return -EACCES;
		return 0;
	}
}

static int parse_dirfile(char *buf, size_t nbytes, struct file *file,
			 void *dstbuf, filldir_t filldir)
{
	while (nbytes >= FUSE_NAME_OFFSET) {
		struct fuse_dirent *dirent = (struct fuse_dirent *) buf;
		size_t reclen = FUSE_DIRENT_SIZE(dirent);
		int over;
		if (!dirent->namelen || dirent->namelen > FUSE_NAME_MAX)
			return -EIO;
		if (reclen > nbytes)
			break;

		over = filldir(dstbuf, dirent->name, dirent->namelen,
			       file->f_pos, dirent->ino, dirent->type);
		if (over)
			break;

		buf += reclen;
		nbytes -= reclen;
		file->f_pos = dirent->off;
	}

	return 0;
}

static int fuse_checkdir(struct file *cfile, struct file *file)
{
	struct inode *inode;
	if (!cfile)
		return -EIO;
	inode = cfile->f_dentry->d_inode;
	if (!S_ISREG(inode->i_mode)) {
		fput(cfile);
		return -EIO;
	}

	file->private_data = cfile;
	return 0;
}

static int fuse_getdir(struct file *file)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_req *req = fuse_get_request(fc);
	struct fuse_getdir_out_i outarg;
	int err;

	if (!req)
		return -ERESTARTNOINTR;

	req->in.h.opcode = FUSE_GETDIR;
	req->in.h.nodeid = get_node_id(inode);
	req->inode = inode;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(struct fuse_getdir_out);
	req->out.args[0].value = &outarg;
	request_send(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (!err)
		err = fuse_checkdir(outarg.file, file);
	return err;
}

static int fuse_readdir(struct file *file, void *dstbuf, filldir_t filldir)
{
	struct file *cfile = file->private_data;
	char *buf;
	int ret;

	if (!cfile) {
		ret = fuse_getdir(file);
		if (ret)
			return ret;

		cfile = file->private_data;
	}

	buf = (char *) __get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = kernel_read(cfile, file->f_pos, buf, PAGE_SIZE);
	if (ret > 0)
		ret = parse_dirfile(buf, ret, file, dstbuf, filldir);

	free_page((unsigned long) buf);
	return ret;
}

static char *read_link(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_req *req = fuse_get_request(fc);
	char *link;

	if (!req)
		return ERR_PTR(-ERESTARTNOINTR);

	link = (char *) __get_free_page(GFP_KERNEL);
	if (!link) {
		link = ERR_PTR(-ENOMEM);
		goto out;
	}
	req->in.h.opcode = FUSE_READLINK;
	req->in.h.nodeid = get_node_id(inode);
	req->inode = inode;
	req->out.argvar = 1;
	req->out.numargs = 1;
	req->out.args[0].size = PAGE_SIZE - 1;
	req->out.args[0].value = link;
	request_send(fc, req);
	if (req->out.h.error) {
		free_page((unsigned long) link);
		link = ERR_PTR(req->out.h.error);
	} else
		link[req->out.args[0].size] = '\0';
 out:
	fuse_put_request(fc, req);
	return link;
}

static void free_link(char *link)
{
	if (!IS_ERR(link))
		free_page((unsigned long) link);
}

static void *fuse_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	nd_set_link(nd, read_link(dentry));
	return NULL;
}

static void fuse_put_link(struct dentry *dentry, struct nameidata *nd, void *c)
{
	free_link(nd_get_link(nd));
}

static int fuse_dir_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static int fuse_dir_release(struct inode *inode, struct file *file)
{
	struct file *cfile = file->private_data;

	if (cfile)
		fput(cfile);

	return 0;
}

static int fuse_getattr(struct vfsmount *mnt, struct dentry *entry,
			struct kstat *stat)
{
	struct inode *inode = entry->d_inode;
	int err = fuse_revalidate(entry);
	if (!err)
		generic_fillattr(inode, stat);

	return err;
}

static struct dentry *fuse_lookup(struct inode *dir, struct dentry *entry,
				  struct nameidata *nd)
{
	struct inode *inode;
	int err = fuse_lookup_iget(dir, entry, &inode);
	if (err)
		return ERR_PTR(err);
	if (inode && S_ISDIR(inode->i_mode)) {
		/* Don't allow creating an alias to a directory  */
		struct dentry *alias = d_find_alias(inode);
		if (alias && !(alias->d_flags & DCACHE_DISCONNECTED)) {
			dput(alias);
			iput(inode);
			return ERR_PTR(-EIO);
		}
	}
	return d_splice_alias(inode, entry);
}

static struct inode_operations fuse_dir_inode_operations = {
	.lookup		= fuse_lookup,
	.permission	= fuse_permission,
	.getattr	= fuse_getattr,
};

static struct file_operations fuse_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= fuse_readdir,
	.open		= fuse_dir_open,
	.release	= fuse_dir_release,
};

static struct inode_operations fuse_common_inode_operations = {
	.permission	= fuse_permission,
	.getattr	= fuse_getattr,
};

static struct inode_operations fuse_symlink_inode_operations = {
	.follow_link	= fuse_follow_link,
	.put_link	= fuse_put_link,
	.readlink	= generic_readlink,
	.getattr	= fuse_getattr,
};

void fuse_init_common(struct inode *inode)
{
	inode->i_op = &fuse_common_inode_operations;
}

void fuse_init_dir(struct inode *inode)
{
	inode->i_op = &fuse_dir_inode_operations;
	inode->i_fop = &fuse_dir_operations;
}

void fuse_init_symlink(struct inode *inode)
{
	inode->i_op = &fuse_symlink_inode_operations;
}
