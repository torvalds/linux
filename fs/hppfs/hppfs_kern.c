/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/dcache.h>
#include <linux/statfs.h>
#include <asm/uaccess.h>
#include <asm/fcntl.h>
#include "os.h"

static int init_inode(struct inode *inode, struct dentry *dentry);

struct hppfs_data {
	struct list_head list;
	char contents[PAGE_SIZE - sizeof(struct list_head)];
};

struct hppfs_private {
	struct file *proc_file;
	int host_fd;
	loff_t len;
	struct hppfs_data *contents;
};

struct hppfs_inode_info {
        struct dentry *proc_dentry;
	struct inode vfs_inode;
};

static inline struct hppfs_inode_info *HPPFS_I(struct inode *inode)
{
	return container_of(inode, struct hppfs_inode_info, vfs_inode);
}

#define HPPFS_SUPER_MAGIC 0xb00000ee

static struct super_operations hppfs_sbops;

static int is_pid(struct dentry *dentry)
{
	struct super_block *sb;
	int i;

	sb = dentry->d_sb;
	if((sb->s_op != &hppfs_sbops) || (dentry->d_parent != sb->s_root))
		return(0);

	for(i = 0; i < dentry->d_name.len; i++){
		if(!isdigit(dentry->d_name.name[i]))
			return(0);
	}
	return(1);
}

static char *dentry_name(struct dentry *dentry, int extra)
{
	struct dentry *parent;
	char *root, *name;
	const char *seg_name;
	int len, seg_len;

	len = 0;
	parent = dentry;
	while(parent->d_parent != parent){
		if(is_pid(parent))
			len += strlen("pid") + 1;
		else len += parent->d_name.len + 1;
		parent = parent->d_parent;
	}

	root = "proc";
	len += strlen(root);
	name = kmalloc(len + extra + 1, GFP_KERNEL);
	if(name == NULL) return(NULL);

	name[len] = '\0';
	parent = dentry;
	while(parent->d_parent != parent){
		if(is_pid(parent)){
			seg_name = "pid";
			seg_len = strlen("pid");
		}
		else {
			seg_name = parent->d_name.name;
			seg_len = parent->d_name.len;
		}

		len -= seg_len + 1;
		name[len] = '/';
		strncpy(&name[len + 1], seg_name, seg_len);
		parent = parent->d_parent;
	}
	strncpy(name, root, strlen(root));
	return(name);
}

struct dentry_operations hppfs_dentry_ops = {
};

static int file_removed(struct dentry *dentry, const char *file)
{
	char *host_file;
	int extra, fd;

	extra = 0;
	if(file != NULL) extra += strlen(file) + 1;

	host_file = dentry_name(dentry, extra + strlen("/remove"));
	if(host_file == NULL){
		printk("file_removed : allocation failed\n");
		return(-ENOMEM);
	}

	if(file != NULL){
		strcat(host_file, "/");
		strcat(host_file, file);
	}
	strcat(host_file, "/remove");

	fd = os_open_file(host_file, of_read(OPENFLAGS()), 0);
	kfree(host_file);
	if(fd > 0){
		os_close_file(fd);
		return(1);
	}
	return(0);
}

static void hppfs_read_inode(struct inode *ino)
{
	struct inode *proc_ino;

	if(HPPFS_I(ino)->proc_dentry == NULL)
		return;

	proc_ino = HPPFS_I(ino)->proc_dentry->d_inode;
	ino->i_uid = proc_ino->i_uid;
	ino->i_gid = proc_ino->i_gid;
	ino->i_atime = proc_ino->i_atime;
	ino->i_mtime = proc_ino->i_mtime;
	ino->i_ctime = proc_ino->i_ctime;
	ino->i_ino = proc_ino->i_ino;
	ino->i_mode = proc_ino->i_mode;
	ino->i_nlink = proc_ino->i_nlink;
	ino->i_size = proc_ino->i_size;
	ino->i_blksize = proc_ino->i_blksize;
	ino->i_blocks = proc_ino->i_blocks;
}

static struct dentry *hppfs_lookup(struct inode *ino, struct dentry *dentry,
                                  struct nameidata *nd)
{
	struct dentry *proc_dentry, *new, *parent;
	struct inode *inode;
	int err, deleted;

	deleted = file_removed(dentry, NULL);
	if(deleted < 0)
		return(ERR_PTR(deleted));
	else if(deleted)
		return(ERR_PTR(-ENOENT));

	err = -ENOMEM;
	parent = HPPFS_I(ino)->proc_dentry;
	mutex_lock(&parent->d_inode->i_mutex);
	proc_dentry = d_lookup(parent, &dentry->d_name);
	if(proc_dentry == NULL){
		proc_dentry = d_alloc(parent, &dentry->d_name);
		if(proc_dentry == NULL){
			mutex_unlock(&parent->d_inode->i_mutex);
			goto out;
		}
		new = (*parent->d_inode->i_op->lookup)(parent->d_inode,
						       proc_dentry, NULL);
		if(new){
			dput(proc_dentry);
			proc_dentry = new;
		}
	}
	mutex_unlock(&parent->d_inode->i_mutex);

	if(IS_ERR(proc_dentry))
		return(proc_dentry);

	inode = iget(ino->i_sb, 0);
	if(inode == NULL)
		goto out_dput;

	err = init_inode(inode, proc_dentry);
	if(err)
		goto out_put;

	hppfs_read_inode(inode);

 	d_add(dentry, inode);
	dentry->d_op = &hppfs_dentry_ops;
	return(NULL);

 out_put:
	iput(inode);
 out_dput:
	dput(proc_dentry);
 out:
	return(ERR_PTR(err));
}

static struct inode_operations hppfs_file_iops = {
};

static ssize_t read_proc(struct file *file, char __user *buf, ssize_t count,
			 loff_t *ppos, int is_user)
{
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t n;

	read = file->f_dentry->d_inode->i_fop->read;

	if(!is_user)
		set_fs(KERNEL_DS);

	n = (*read)(file, buf, count, &file->f_pos);

	if(!is_user)
		set_fs(USER_DS);

	if(ppos) *ppos = file->f_pos;
	return n;
}

static ssize_t hppfs_read_file(int fd, char __user *buf, ssize_t count)
{
	ssize_t n;
	int cur, err;
	char *new_buf;

	n = -ENOMEM;
	new_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if(new_buf == NULL){
		printk("hppfs_read_file : kmalloc failed\n");
		goto out;
	}
	n = 0;
	while(count > 0){
		cur = min_t(ssize_t, count, PAGE_SIZE);
		err = os_read_file(fd, new_buf, cur);
		if(err < 0){
			printk("hppfs_read : read failed, errno = %d\n",
			       err);
			n = err;
			goto out_free;
		}
		else if(err == 0)
			break;

		if(copy_to_user(buf, new_buf, err)){
			n = -EFAULT;
			goto out_free;
		}
		n += err;
		count -= err;
	}
 out_free:
	kfree(new_buf);
 out:
	return n;
}

static ssize_t hppfs_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	struct hppfs_private *hppfs = file->private_data;
	struct hppfs_data *data;
	loff_t off;
	int err;

	if(hppfs->contents != NULL){
		if(*ppos >= hppfs->len) return(0);

		data = hppfs->contents;
		off = *ppos;
		while(off >= sizeof(data->contents)){
			data = list_entry(data->list.next, struct hppfs_data,
					  list);
			off -= sizeof(data->contents);
		}

		if(off + count > hppfs->len)
			count = hppfs->len - off;
		copy_to_user(buf, &data->contents[off], count);
		*ppos += count;
	}
	else if(hppfs->host_fd != -1){
		err = os_seek_file(hppfs->host_fd, *ppos);
		if(err){
			printk("hppfs_read : seek failed, errno = %d\n", err);
			return(err);
		}
		count = hppfs_read_file(hppfs->host_fd, buf, count);
		if(count > 0)
			*ppos += count;
	}
	else count = read_proc(hppfs->proc_file, buf, count, ppos, 1);

	return(count);
}

static ssize_t hppfs_write(struct file *file, const char __user *buf, size_t len,
			   loff_t *ppos)
{
	struct hppfs_private *data = file->private_data;
	struct file *proc_file = data->proc_file;
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
	int err;

	write = proc_file->f_dentry->d_inode->i_fop->write;

	proc_file->f_pos = file->f_pos;
	err = (*write)(proc_file, buf, len, &proc_file->f_pos);
	file->f_pos = proc_file->f_pos;

	return(err);
}

static int open_host_sock(char *host_file, int *filter_out)
{
	char *end;
	int fd;

	end = &host_file[strlen(host_file)];
	strcpy(end, "/rw");
	*filter_out = 1;
	fd = os_connect_socket(host_file);
	if(fd > 0)
		return(fd);

	strcpy(end, "/r");
	*filter_out = 0;
	fd = os_connect_socket(host_file);
	return(fd);
}

static void free_contents(struct hppfs_data *head)
{
	struct hppfs_data *data;
	struct list_head *ele, *next;

	if(head == NULL) return;

	list_for_each_safe(ele, next, &head->list){
		data = list_entry(ele, struct hppfs_data, list);
		kfree(data);
	}
	kfree(head);
}

static struct hppfs_data *hppfs_get_data(int fd, int filter,
					 struct file *proc_file,
					 struct file *hppfs_file,
					 loff_t *size_out)
{
	struct hppfs_data *data, *new, *head;
	int n, err;

	err = -ENOMEM;
	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if(data == NULL){
		printk("hppfs_get_data : head allocation failed\n");
		goto failed;
	}

	INIT_LIST_HEAD(&data->list);

	head = data;
	*size_out = 0;

	if(filter){
		while((n = read_proc(proc_file, data->contents,
				     sizeof(data->contents), NULL, 0)) > 0)
			os_write_file(fd, data->contents, n);
		err = os_shutdown_socket(fd, 0, 1);
		if(err){
			printk("hppfs_get_data : failed to shut down "
			       "socket\n");
			goto failed_free;
		}
	}
	while(1){
		n = os_read_file(fd, data->contents, sizeof(data->contents));
		if(n < 0){
			err = n;
			printk("hppfs_get_data : read failed, errno = %d\n",
			       err);
			goto failed_free;
		}
		else if(n == 0)
			break;

		*size_out += n;

		if(n < sizeof(data->contents))
			break;

		new = kmalloc(sizeof(*data), GFP_KERNEL);
		if(new == 0){
			printk("hppfs_get_data : data allocation failed\n");
			err = -ENOMEM;
			goto failed_free;
		}

		INIT_LIST_HEAD(&new->list);
		list_add(&new->list, &data->list);
		data = new;
	}
	return(head);

 failed_free:
	free_contents(head);
 failed:
	return(ERR_PTR(err));
}

static struct hppfs_private *hppfs_data(void)
{
	struct hppfs_private *data;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if(data == NULL)
		return(data);

	*data = ((struct hppfs_private ) { .host_fd  		= -1,
					   .len  		= -1,
					   .contents 		= NULL } );
	return(data);
}

static int file_mode(int fmode)
{
	if(fmode == (FMODE_READ | FMODE_WRITE))
		return(O_RDWR);
	if(fmode == FMODE_READ)
		return(O_RDONLY);
	if(fmode == FMODE_WRITE)
		return(O_WRONLY);
	return(0);
}

static int hppfs_open(struct inode *inode, struct file *file)
{
	struct hppfs_private *data;
	struct dentry *proc_dentry;
	char *host_file;
	int err, fd, type, filter;

	err = -ENOMEM;
	data = hppfs_data();
	if(data == NULL)
		goto out;

	host_file = dentry_name(file->f_dentry, strlen("/rw"));
	if(host_file == NULL)
		goto out_free2;

	proc_dentry = HPPFS_I(inode)->proc_dentry;

	/* XXX This isn't closed anywhere */
	data->proc_file = dentry_open(dget(proc_dentry), NULL,
				      file_mode(file->f_mode));
	err = PTR_ERR(data->proc_file);
	if(IS_ERR(data->proc_file))
		goto out_free1;

	type = os_file_type(host_file);
	if(type == OS_TYPE_FILE){
		fd = os_open_file(host_file, of_read(OPENFLAGS()), 0);
		if(fd >= 0)
			data->host_fd = fd;
		else printk("hppfs_open : failed to open '%s', errno = %d\n",
			    host_file, -fd);

		data->contents = NULL;
	}
	else if(type == OS_TYPE_DIR){
		fd = open_host_sock(host_file, &filter);
		if(fd > 0){
			data->contents = hppfs_get_data(fd, filter,
							data->proc_file,
							file, &data->len);
			if(!IS_ERR(data->contents))
				data->host_fd = fd;
		}
		else printk("hppfs_open : failed to open a socket in "
			    "'%s', errno = %d\n", host_file, -fd);
	}
	kfree(host_file);

	file->private_data = data;
	return(0);

 out_free1:
	kfree(host_file);
 out_free2:
	free_contents(data->contents);
	kfree(data);
 out:
	return(err);
}

static int hppfs_dir_open(struct inode *inode, struct file *file)
{
	struct hppfs_private *data;
	struct dentry *proc_dentry;
	int err;

	err = -ENOMEM;
	data = hppfs_data();
	if(data == NULL)
		goto out;

	proc_dentry = HPPFS_I(inode)->proc_dentry;
	data->proc_file = dentry_open(dget(proc_dentry), NULL,
				      file_mode(file->f_mode));
	err = PTR_ERR(data->proc_file);
	if(IS_ERR(data->proc_file))
		goto out_free;

	file->private_data = data;
	return(0);

 out_free:
	kfree(data);
 out:
	return(err);
}

static loff_t hppfs_llseek(struct file *file, loff_t off, int where)
{
	struct hppfs_private *data = file->private_data;
	struct file *proc_file = data->proc_file;
	loff_t (*llseek)(struct file *, loff_t, int);
	loff_t ret;

	llseek = proc_file->f_dentry->d_inode->i_fop->llseek;
	if(llseek != NULL){
		ret = (*llseek)(proc_file, off, where);
		if(ret < 0)
			return(ret);
	}

	return(default_llseek(file, off, where));
}

static const struct file_operations hppfs_file_fops = {
	.owner		= NULL,
	.llseek		= hppfs_llseek,
	.read		= hppfs_read,
	.write		= hppfs_write,
	.open		= hppfs_open,
};

struct hppfs_dirent {
	void *vfs_dirent;
	filldir_t filldir;
	struct dentry *dentry;
};

static int hppfs_filldir(void *d, const char *name, int size,
			 loff_t offset, ino_t inode, unsigned int type)
{
	struct hppfs_dirent *dirent = d;

	if(file_removed(dirent->dentry, name))
		return(0);

	return((*dirent->filldir)(dirent->vfs_dirent, name, size, offset,
				  inode, type));
}

static int hppfs_readdir(struct file *file, void *ent, filldir_t filldir)
{
	struct hppfs_private *data = file->private_data;
	struct file *proc_file = data->proc_file;
	int (*readdir)(struct file *, void *, filldir_t);
	struct hppfs_dirent dirent = ((struct hppfs_dirent)
		                      { .vfs_dirent  	= ent,
					.filldir 	= filldir,
					.dentry  	= file->f_dentry } );
	int err;

	readdir = proc_file->f_dentry->d_inode->i_fop->readdir;

	proc_file->f_pos = file->f_pos;
	err = (*readdir)(proc_file, &dirent, hppfs_filldir);
	file->f_pos = proc_file->f_pos;

	return(err);
}

static int hppfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	return(0);
}

static const struct file_operations hppfs_dir_fops = {
	.owner		= NULL,
	.readdir	= hppfs_readdir,
	.open		= hppfs_dir_open,
	.fsync		= hppfs_fsync,
};

static int hppfs_statfs(struct dentry *dentry, struct kstatfs *sf)
{
	sf->f_blocks = 0;
	sf->f_bfree = 0;
	sf->f_bavail = 0;
	sf->f_files = 0;
	sf->f_ffree = 0;
	sf->f_type = HPPFS_SUPER_MAGIC;
	return(0);
}

static struct inode *hppfs_alloc_inode(struct super_block *sb)
{
	struct hppfs_inode_info *hi;

	hi = kmalloc(sizeof(*hi), GFP_KERNEL);
	if(hi == NULL)
		return(NULL);

	*hi = ((struct hppfs_inode_info) { .proc_dentry	= NULL });
	inode_init_once(&hi->vfs_inode);
	return(&hi->vfs_inode);
}

void hppfs_delete_inode(struct inode *ino)
{
	clear_inode(ino);
}

static void hppfs_destroy_inode(struct inode *inode)
{
	kfree(HPPFS_I(inode));
}

static struct super_operations hppfs_sbops = {
	.alloc_inode	= hppfs_alloc_inode,
	.destroy_inode	= hppfs_destroy_inode,
	.read_inode	= hppfs_read_inode,
	.delete_inode	= hppfs_delete_inode,
	.statfs		= hppfs_statfs,
};

static int hppfs_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	struct file *proc_file;
	struct dentry *proc_dentry;
	int ret;

	proc_dentry = HPPFS_I(dentry->d_inode)->proc_dentry;
	proc_file = dentry_open(dget(proc_dentry), NULL, O_RDONLY);
	if (IS_ERR(proc_file))
		return PTR_ERR(proc_file);

	ret = proc_dentry->d_inode->i_op->readlink(proc_dentry, buffer, buflen);

	fput(proc_file);

	return ret;
}

static void* hppfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct file *proc_file;
	struct dentry *proc_dentry;
	void *ret;

	proc_dentry = HPPFS_I(dentry->d_inode)->proc_dentry;
	proc_file = dentry_open(dget(proc_dentry), NULL, O_RDONLY);
	if (IS_ERR(proc_file))
		return proc_file;

	ret = proc_dentry->d_inode->i_op->follow_link(proc_dentry, nd);

	fput(proc_file);

	return ret;
}

static struct inode_operations hppfs_dir_iops = {
	.lookup		= hppfs_lookup,
};

static struct inode_operations hppfs_link_iops = {
	.readlink	= hppfs_readlink,
	.follow_link	= hppfs_follow_link,
};

static int init_inode(struct inode *inode, struct dentry *dentry)
{
	if(S_ISDIR(dentry->d_inode->i_mode)){
		inode->i_op = &hppfs_dir_iops;
		inode->i_fop = &hppfs_dir_fops;
	}
	else if(S_ISLNK(dentry->d_inode->i_mode)){
		inode->i_op = &hppfs_link_iops;
		inode->i_fop = &hppfs_file_fops;
	}
	else {
		inode->i_op = &hppfs_file_iops;
		inode->i_fop = &hppfs_file_fops;
	}

	HPPFS_I(inode)->proc_dentry = dentry;

	return(0);
}

static int hppfs_fill_super(struct super_block *sb, void *d, int silent)
{
	struct inode *root_inode;
	struct file_system_type *procfs;
	struct super_block *proc_sb;
	int err;

	err = -ENOENT;
	procfs = get_fs_type("proc");
	if(procfs == NULL)
		goto out;

	if(list_empty(&procfs->fs_supers))
		goto out;

	proc_sb = list_entry(procfs->fs_supers.next, struct super_block,
			     s_instances);

	sb->s_blocksize = 1024;
	sb->s_blocksize_bits = 10;
	sb->s_magic = HPPFS_SUPER_MAGIC;
	sb->s_op = &hppfs_sbops;

	root_inode = iget(sb, 0);
	if(root_inode == NULL)
		goto out;

	err = init_inode(root_inode, proc_sb->s_root);
	if(err)
		goto out_put;

	err = -ENOMEM;
	sb->s_root = d_alloc_root(root_inode);
	if(sb->s_root == NULL)
		goto out_put;

	hppfs_read_inode(root_inode);

	return(0);

 out_put:
	iput(root_inode);
 out:
	return(err);
}

static int hppfs_read_super(struct file_system_type *type,
			    int flags, const char *dev_name,
			    void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(type, flags, data, hppfs_fill_super, mnt);
}

static struct file_system_type hppfs_type = {
	.owner 		= THIS_MODULE,
	.name 		= "hppfs",
	.get_sb 	= hppfs_read_super,
	.kill_sb	= kill_anon_super,
	.fs_flags 	= 0,
};

static int __init init_hppfs(void)
{
	return(register_filesystem(&hppfs_type));
}

static void __exit exit_hppfs(void)
{
	unregister_filesystem(&hppfs_type);
}

module_init(init_hppfs)
module_exit(exit_hppfs)
MODULE_LICENSE("GPL");

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
