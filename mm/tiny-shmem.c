/*
 * tiny-shmem.c: simple shmemfs and tmpfs using ramfs code
 *
 * Matt Mackall <mpm@selenic.com> January, 2004
 * derived from mm/shmem.c and fs/ramfs/inode.c
 *
 * This is intended for small system where the benefits of the full
 * shmem code (swap-backed and resource-limited) are outweighed by
 * their complexity. On systems without swap this code should be
 * effectively equivalent, but much lighter weight.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/vfs.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/ramfs.h>

static struct file_system_type tmpfs_fs_type = {
	.name		= "tmpfs",
	.get_sb		= ramfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static struct vfsmount *shm_mnt;

static int __init init_tmpfs(void)
{
	BUG_ON(register_filesystem(&tmpfs_fs_type) != 0);

#ifdef CONFIG_TMPFS
	devfs_mk_dir("shm");
#endif
	shm_mnt = kern_mount(&tmpfs_fs_type);
	BUG_ON(IS_ERR(shm_mnt));

	return 0;
}
module_init(init_tmpfs)

/*
 * shmem_file_setup - get an unlinked file living in tmpfs
 *
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 *
 */
struct file *shmem_file_setup(char *name, loff_t size, unsigned long flags)
{
	int error;
	struct file *file;
	struct inode *inode;
	struct dentry *dentry, *root;
	struct qstr this;

	if (IS_ERR(shm_mnt))
		return (void *)shm_mnt;

	error = -ENOMEM;
	this.name = name;
	this.len = strlen(name);
	this.hash = 0; /* will go */
	root = shm_mnt->mnt_root;
	dentry = d_alloc(root, &this);
	if (!dentry)
		goto put_memory;

	error = -ENFILE;
	file = get_empty_filp();
	if (!file)
		goto put_dentry;

	error = -ENOSPC;
	inode = ramfs_get_inode(root->d_sb, S_IFREG | S_IRWXUGO, 0);
	if (!inode)
		goto close_file;

	d_instantiate(dentry, inode);
	inode->i_nlink = 0;	/* It is unlinked */

	file->f_vfsmnt = mntget(shm_mnt);
	file->f_dentry = dentry;
	file->f_mapping = inode->i_mapping;
	file->f_op = &ramfs_file_operations;
	file->f_mode = FMODE_WRITE | FMODE_READ;

	/* notify everyone as to the change of file size */
	error = do_truncate(dentry, size, 0, file);
	if (error < 0)
		goto close_file;

	return file;

close_file:
	put_filp(file);
put_dentry:
	dput(dentry);
put_memory:
	return ERR_PTR(error);
}

/*
 * shmem_zero_setup - setup a shared anonymous mapping
 *
 * @vma: the vma to be mmapped is prepared by do_mmap_pgoff
 */
int shmem_zero_setup(struct vm_area_struct *vma)
{
	struct file *file;
	loff_t size = vma->vm_end - vma->vm_start;

	file = shmem_file_setup("dev/zero", size, vma->vm_flags);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (vma->vm_file)
		fput(vma->vm_file);
	vma->vm_file = file;
	vma->vm_ops = &generic_file_vm_ops;
	return 0;
}

int shmem_unuse(swp_entry_t entry, struct page *page)
{
	return 0;
}

int shmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	file_accessed(file);
#ifndef CONFIG_MMU
	return ramfs_nommu_mmap(file, vma);
#else
	return 0;
#endif
}

#ifndef CONFIG_MMU
unsigned long shmem_get_unmapped_area(struct file *file,
				      unsigned long addr,
				      unsigned long len,
				      unsigned long pgoff,
				      unsigned long flags)
{
	return ramfs_nommu_get_unmapped_area(file, addr, len, pgoff, flags);
}
#endif
