/*
 * hugetlbpage-backed filesystem.  Based on ramfs.
 *
 * William Irwin, 2002
 *
 * Copyright (C) 2002 Linus Torvalds.
 */

#include <linux/module.h>
#include <linux/thread_info.h>
#include <asm/current.h>
#include <linux/sched.h>		/* remove ASAP */
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/capability.h>
#include <linux/ctype.h>
#include <linux/backing-dev.h>
#include <linux/hugetlb.h>
#include <linux/pagevec.h>
#include <linux/parser.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/dnotify.h>
#include <linux/statfs.h>
#include <linux/security.h>
#include <linux/magic.h>

#include <asm/uaccess.h>

static const struct super_operations hugetlbfs_ops;
static const struct address_space_operations hugetlbfs_aops;
const struct file_operations hugetlbfs_file_operations;
static const struct inode_operations hugetlbfs_dir_inode_operations;
static const struct inode_operations hugetlbfs_inode_operations;

static struct backing_dev_info hugetlbfs_backing_dev_info = {
	.name		= "hugetlbfs",
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
};

int sysctl_hugetlb_shm_group;

enum {
	Opt_size, Opt_nr_inodes,
	Opt_mode, Opt_uid, Opt_gid,
	Opt_pagesize,
	Opt_err,
};

static const match_table_t tokens = {
	{Opt_size,	"size=%s"},
	{Opt_nr_inodes,	"nr_inodes=%s"},
	{Opt_mode,	"mode=%o"},
	{Opt_uid,	"uid=%u"},
	{Opt_gid,	"gid=%u"},
	{Opt_pagesize,	"pagesize=%s"},
	{Opt_err,	NULL},
};

static void huge_pagevec_release(struct pagevec *pvec)
{
	int i;

	for (i = 0; i < pagevec_count(pvec); ++i)
		put_page(pvec->pages[i]);

	pagevec_reinit(pvec);
}

static int hugetlbfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	loff_t len, vma_len;
	int ret;
	struct hstate *h = hstate_file(file);

	/*
	 * vma address alignment (but not the pgoff alignment) has
	 * already been checked by prepare_hugepage_range.  If you add
	 * any error returns here, do so after setting VM_HUGETLB, so
	 * is_vm_hugetlb_page tests below unmap_region go the right
	 * way when do_mmap_pgoff unwinds (may be important on powerpc
	 * and ia64).
	 */
	vma->vm_flags |= VM_HUGETLB | VM_RESERVED;
	vma->vm_ops = &hugetlb_vm_ops;

	if (vma->vm_pgoff & ~(huge_page_mask(h) >> PAGE_SHIFT))
		return -EINVAL;

	vma_len = (loff_t)(vma->vm_end - vma->vm_start);

	mutex_lock(&inode->i_mutex);
	file_accessed(file);

	ret = -ENOMEM;
	len = vma_len + ((loff_t)vma->vm_pgoff << PAGE_SHIFT);

	if (hugetlb_reserve_pages(inode,
				vma->vm_pgoff >> huge_page_order(h),
				len >> huge_page_shift(h), vma,
				vma->vm_flags))
		goto out;

	ret = 0;
	hugetlb_prefault_arch_hook(vma->vm_mm);
	if (vma->vm_flags & VM_WRITE && inode->i_size < len)
		inode->i_size = len;
out:
	mutex_unlock(&inode->i_mutex);

	return ret;
}

/*
 * Called under down_write(mmap_sem).
 */

#ifndef HAVE_ARCH_HUGETLB_UNMAPPED_AREA
static unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;
	struct hstate *h = hstate_file(file);

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (len > TASK_SIZE)
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
		return addr;
	}

	if (addr) {
		addr = ALIGN(addr, huge_page_size(h));
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	start_addr = mm->free_area_cache;

	if (len <= mm->cached_hole_size)
		start_addr = TASK_UNMAPPED_BASE;

full_search:
	addr = ALIGN(start_addr, huge_page_size(h));

	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr) {
			/*
			 * Start a new search - just in case we missed
			 * some holes.
			 */
			if (start_addr != TASK_UNMAPPED_BASE) {
				start_addr = TASK_UNMAPPED_BASE;
				goto full_search;
			}
			return -ENOMEM;
		}

		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = ALIGN(vma->vm_end, huge_page_size(h));
	}
}
#endif

static int
hugetlbfs_read_actor(struct page *page, unsigned long offset,
			char __user *buf, unsigned long count,
			unsigned long size)
{
	char *kaddr;
	unsigned long left, copied = 0;
	int i, chunksize;

	if (size > count)
		size = count;

	/* Find which 4k chunk and offset with in that chunk */
	i = offset >> PAGE_CACHE_SHIFT;
	offset = offset & ~PAGE_CACHE_MASK;

	while (size) {
		chunksize = PAGE_CACHE_SIZE;
		if (offset)
			chunksize -= offset;
		if (chunksize > size)
			chunksize = size;
		kaddr = kmap(&page[i]);
		left = __copy_to_user(buf, kaddr + offset, chunksize);
		kunmap(&page[i]);
		if (left) {
			copied += (chunksize - left);
			break;
		}
		offset = 0;
		size -= chunksize;
		buf += chunksize;
		copied += chunksize;
		i++;
	}
	return copied ? copied : -EFAULT;
}

/*
 * Support for read() - Find the page attached to f_mapping and copy out the
 * data. Its *very* similar to do_generic_mapping_read(), we can't use that
 * since it has PAGE_CACHE_SIZE assumptions.
 */
static ssize_t hugetlbfs_read(struct file *filp, char __user *buf,
			      size_t len, loff_t *ppos)
{
	struct hstate *h = hstate_file(filp);
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	unsigned long index = *ppos >> huge_page_shift(h);
	unsigned long offset = *ppos & ~huge_page_mask(h);
	unsigned long end_index;
	loff_t isize;
	ssize_t retval = 0;

	mutex_lock(&inode->i_mutex);

	/* validate length */
	if (len == 0)
		goto out;

	isize = i_size_read(inode);
	if (!isize)
		goto out;

	end_index = (isize - 1) >> huge_page_shift(h);
	for (;;) {
		struct page *page;
		unsigned long nr, ret;
		int ra;

		/* nr is the maximum number of bytes to copy from this page */
		nr = huge_page_size(h);
		if (index >= end_index) {
			if (index > end_index)
				goto out;
			nr = ((isize - 1) & ~huge_page_mask(h)) + 1;
			if (nr <= offset) {
				goto out;
			}
		}
		nr = nr - offset;

		/* Find the page */
		page = find_get_page(mapping, index);
		if (unlikely(page == NULL)) {
			/*
			 * We have a HOLE, zero out the user-buffer for the
			 * length of the hole or request.
			 */
			ret = len < nr ? len : nr;
			if (clear_user(buf, ret))
				ra = -EFAULT;
			else
				ra = 0;
		} else {
			/*
			 * We have the page, copy it to user space buffer.
			 */
			ra = hugetlbfs_read_actor(page, offset, buf, len, nr);
			ret = ra;
		}
		if (ra < 0) {
			if (retval == 0)
				retval = ra;
			if (page)
				page_cache_release(page);
			goto out;
		}

		offset += ret;
		retval += ret;
		len -= ret;
		index += offset >> huge_page_shift(h);
		offset &= ~huge_page_mask(h);

		if (page)
			page_cache_release(page);

		/* short read or no more work */
		if ((ret != nr) || (len == 0))
			break;
	}
out:
	*ppos = ((loff_t)index << huge_page_shift(h)) + offset;
	mutex_unlock(&inode->i_mutex);
	return retval;
}

static int hugetlbfs_write_begin(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	return -EINVAL;
}

static int hugetlbfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	BUG();
	return -EINVAL;
}

static void truncate_huge_page(struct page *page)
{
	cancel_dirty_page(page, /* No IO accounting for huge pages? */0);
	ClearPageUptodate(page);
	remove_from_page_cache(page);
	put_page(page);
}

static void truncate_hugepages(struct inode *inode, loff_t lstart)
{
	struct hstate *h = hstate_inode(inode);
	struct address_space *mapping = &inode->i_data;
	const pgoff_t start = lstart >> huge_page_shift(h);
	struct pagevec pvec;
	pgoff_t next;
	int i, freed = 0;

	pagevec_init(&pvec, 0);
	next = start;
	while (1) {
		if (!pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
			if (next == start)
				break;
			next = start;
			continue;
		}

		for (i = 0; i < pagevec_count(&pvec); ++i) {
			struct page *page = pvec.pages[i];

			lock_page(page);
			if (page->index > next)
				next = page->index;
			++next;
			truncate_huge_page(page);
			unlock_page(page);
			freed++;
		}
		huge_pagevec_release(&pvec);
	}
	BUG_ON(!lstart && mapping->nrpages);
	hugetlb_unreserve_pages(inode, start, freed);
}

static void hugetlbfs_delete_inode(struct inode *inode)
{
	truncate_hugepages(inode, 0);
	clear_inode(inode);
}

static void hugetlbfs_forget_inode(struct inode *inode) __releases(inode_lock)
{
	if (generic_detach_inode(inode)) {
		truncate_hugepages(inode, 0);
		clear_inode(inode);
		destroy_inode(inode);
	}
}

static void hugetlbfs_drop_inode(struct inode *inode)
{
	if (!inode->i_nlink)
		generic_delete_inode(inode);
	else
		hugetlbfs_forget_inode(inode);
}

static inline void
hugetlb_vmtruncate_list(struct prio_tree_root *root, pgoff_t pgoff)
{
	struct vm_area_struct *vma;
	struct prio_tree_iter iter;

	vma_prio_tree_foreach(vma, &iter, root, pgoff, ULONG_MAX) {
		unsigned long v_offset;

		/*
		 * Can the expression below overflow on 32-bit arches?
		 * No, because the prio_tree returns us only those vmas
		 * which overlap the truncated area starting at pgoff,
		 * and no vma on a 32-bit arch can span beyond the 4GB.
		 */
		if (vma->vm_pgoff < pgoff)
			v_offset = (pgoff - vma->vm_pgoff) << PAGE_SHIFT;
		else
			v_offset = 0;

		__unmap_hugepage_range(vma,
				vma->vm_start + v_offset, vma->vm_end, NULL);
	}
}

static int hugetlb_vmtruncate(struct inode *inode, loff_t offset)
{
	pgoff_t pgoff;
	struct address_space *mapping = inode->i_mapping;
	struct hstate *h = hstate_inode(inode);

	BUG_ON(offset & ~huge_page_mask(h));
	pgoff = offset >> PAGE_SHIFT;

	i_size_write(inode, offset);
	spin_lock(&mapping->i_mmap_lock);
	if (!prio_tree_empty(&mapping->i_mmap))
		hugetlb_vmtruncate_list(&mapping->i_mmap, pgoff);
	spin_unlock(&mapping->i_mmap_lock);
	truncate_hugepages(inode, offset);
	return 0;
}

static int hugetlbfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct hstate *h = hstate_inode(inode);
	int error;
	unsigned int ia_valid = attr->ia_valid;

	BUG_ON(!inode);

	error = inode_change_ok(inode, attr);
	if (error)
		goto out;

	if (ia_valid & ATTR_SIZE) {
		error = -EINVAL;
		if (!(attr->ia_size & ~huge_page_mask(h)))
			error = hugetlb_vmtruncate(inode, attr->ia_size);
		if (error)
			goto out;
		attr->ia_valid &= ~ATTR_SIZE;
	}
	error = inode_setattr(inode, attr);
out:
	return error;
}

static struct inode *hugetlbfs_get_inode(struct super_block *sb, uid_t uid, 
					gid_t gid, int mode, dev_t dev)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (inode) {
		struct hugetlbfs_inode_info *info;
		inode->i_mode = mode;
		inode->i_uid = uid;
		inode->i_gid = gid;
		inode->i_mapping->a_ops = &hugetlbfs_aops;
		inode->i_mapping->backing_dev_info =&hugetlbfs_backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		INIT_LIST_HEAD(&inode->i_mapping->private_list);
		info = HUGETLBFS_I(inode);
		/*
		 * The policy is initialized here even if we are creating a
		 * private inode because initialization simply creates an
		 * an empty rb tree and calls spin_lock_init(), later when we
		 * call mpol_free_shared_policy() it will just return because
		 * the rb tree will still be empty.
		 */
		mpol_shared_policy_init(&info->policy, NULL);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &hugetlbfs_inode_operations;
			inode->i_fop = &hugetlbfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &hugetlbfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int hugetlbfs_mknod(struct inode *dir,
			struct dentry *dentry, int mode, dev_t dev)
{
	struct inode *inode;
	int error = -ENOSPC;
	gid_t gid;

	if (dir->i_mode & S_ISGID) {
		gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else {
		gid = current_fsgid();
	}
	inode = hugetlbfs_get_inode(dir->i_sb, current_fsuid(), gid, mode, dev);
	if (inode) {
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int hugetlbfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int retval = hugetlbfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int hugetlbfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
	return hugetlbfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int hugetlbfs_symlink(struct inode *dir,
			struct dentry *dentry, const char *symname)
{
	struct inode *inode;
	int error = -ENOSPC;
	gid_t gid;

	if (dir->i_mode & S_ISGID)
		gid = dir->i_gid;
	else
		gid = current_fsgid();

	inode = hugetlbfs_get_inode(dir->i_sb, current_fsuid(),
					gid, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
		} else
			iput(inode);
	}
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;

	return error;
}

/*
 * mark the head page dirty
 */
static int hugetlbfs_set_page_dirty(struct page *page)
{
	struct page *head = compound_head(page);

	SetPageDirty(head);
	return 0;
}

static int hugetlbfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(dentry->d_sb);
	struct hstate *h = hstate_inode(dentry->d_inode);

	buf->f_type = HUGETLBFS_MAGIC;
	buf->f_bsize = huge_page_size(h);
	if (sbinfo) {
		spin_lock(&sbinfo->stat_lock);
		/* If no limits set, just report 0 for max/free/used
		 * blocks, like simple_statfs() */
		if (sbinfo->max_blocks >= 0) {
			buf->f_blocks = sbinfo->max_blocks;
			buf->f_bavail = buf->f_bfree = sbinfo->free_blocks;
			buf->f_files = sbinfo->max_inodes;
			buf->f_ffree = sbinfo->free_inodes;
		}
		spin_unlock(&sbinfo->stat_lock);
	}
	buf->f_namelen = NAME_MAX;
	return 0;
}

static void hugetlbfs_put_super(struct super_block *sb)
{
	struct hugetlbfs_sb_info *sbi = HUGETLBFS_SB(sb);

	if (sbi) {
		sb->s_fs_info = NULL;
		kfree(sbi);
	}
}

static inline int hugetlbfs_dec_free_inodes(struct hugetlbfs_sb_info *sbinfo)
{
	if (sbinfo->free_inodes >= 0) {
		spin_lock(&sbinfo->stat_lock);
		if (unlikely(!sbinfo->free_inodes)) {
			spin_unlock(&sbinfo->stat_lock);
			return 0;
		}
		sbinfo->free_inodes--;
		spin_unlock(&sbinfo->stat_lock);
	}

	return 1;
}

static void hugetlbfs_inc_free_inodes(struct hugetlbfs_sb_info *sbinfo)
{
	if (sbinfo->free_inodes >= 0) {
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_inodes++;
		spin_unlock(&sbinfo->stat_lock);
	}
}


static struct kmem_cache *hugetlbfs_inode_cachep;

static struct inode *hugetlbfs_alloc_inode(struct super_block *sb)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(sb);
	struct hugetlbfs_inode_info *p;

	if (unlikely(!hugetlbfs_dec_free_inodes(sbinfo)))
		return NULL;
	p = kmem_cache_alloc(hugetlbfs_inode_cachep, GFP_KERNEL);
	if (unlikely(!p)) {
		hugetlbfs_inc_free_inodes(sbinfo);
		return NULL;
	}
	return &p->vfs_inode;
}

static void hugetlbfs_destroy_inode(struct inode *inode)
{
	hugetlbfs_inc_free_inodes(HUGETLBFS_SB(inode->i_sb));
	mpol_free_shared_policy(&HUGETLBFS_I(inode)->policy);
	kmem_cache_free(hugetlbfs_inode_cachep, HUGETLBFS_I(inode));
}

static const struct address_space_operations hugetlbfs_aops = {
	.write_begin	= hugetlbfs_write_begin,
	.write_end	= hugetlbfs_write_end,
	.set_page_dirty	= hugetlbfs_set_page_dirty,
};


static void init_once(void *foo)
{
	struct hugetlbfs_inode_info *ei = (struct hugetlbfs_inode_info *)foo;

	inode_init_once(&ei->vfs_inode);
}

const struct file_operations hugetlbfs_file_operations = {
	.read			= hugetlbfs_read,
	.mmap			= hugetlbfs_file_mmap,
	.fsync			= noop_fsync,
	.get_unmapped_area	= hugetlb_get_unmapped_area,
};

static const struct inode_operations hugetlbfs_dir_inode_operations = {
	.create		= hugetlbfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= hugetlbfs_symlink,
	.mkdir		= hugetlbfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= hugetlbfs_mknod,
	.rename		= simple_rename,
	.setattr	= hugetlbfs_setattr,
};

static const struct inode_operations hugetlbfs_inode_operations = {
	.setattr	= hugetlbfs_setattr,
};

static const struct super_operations hugetlbfs_ops = {
	.alloc_inode    = hugetlbfs_alloc_inode,
	.destroy_inode  = hugetlbfs_destroy_inode,
	.statfs		= hugetlbfs_statfs,
	.delete_inode	= hugetlbfs_delete_inode,
	.drop_inode	= hugetlbfs_drop_inode,
	.put_super	= hugetlbfs_put_super,
	.show_options	= generic_show_options,
};

static int
hugetlbfs_parse_options(char *options, struct hugetlbfs_config *pconfig)
{
	char *p, *rest;
	substring_t args[MAX_OPT_ARGS];
	int option;
	unsigned long long size = 0;
	enum { NO_SIZE, SIZE_STD, SIZE_PERCENT } setsize = NO_SIZE;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(&args[0], &option))
 				goto bad_val;
			pconfig->uid = option;
			break;

		case Opt_gid:
			if (match_int(&args[0], &option))
 				goto bad_val;
			pconfig->gid = option;
			break;

		case Opt_mode:
			if (match_octal(&args[0], &option))
 				goto bad_val;
			pconfig->mode = option & 01777U;
			break;

		case Opt_size: {
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			size = memparse(args[0].from, &rest);
			setsize = SIZE_STD;
			if (*rest == '%')
				setsize = SIZE_PERCENT;
			break;
		}

		case Opt_nr_inodes:
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			pconfig->nr_inodes = memparse(args[0].from, &rest);
			break;

		case Opt_pagesize: {
			unsigned long ps;
			ps = memparse(args[0].from, &rest);
			pconfig->hstate = size_to_hstate(ps);
			if (!pconfig->hstate) {
				printk(KERN_ERR
				"hugetlbfs: Unsupported page size %lu MB\n",
					ps >> 20);
				return -EINVAL;
			}
			break;
		}

		default:
			printk(KERN_ERR "hugetlbfs: Bad mount option: \"%s\"\n",
				 p);
			return -EINVAL;
			break;
		}
	}

	/* Do size after hstate is set up */
	if (setsize > NO_SIZE) {
		struct hstate *h = pconfig->hstate;
		if (setsize == SIZE_PERCENT) {
			size <<= huge_page_shift(h);
			size *= h->max_huge_pages;
			do_div(size, 100);
		}
		pconfig->nr_blocks = (size >> huge_page_shift(h));
	}

	return 0;

bad_val:
 	printk(KERN_ERR "hugetlbfs: Bad value '%s' for mount option '%s'\n",
	       args[0].from, p);
 	return -EINVAL;
}

static int
hugetlbfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	int ret;
	struct hugetlbfs_config config;
	struct hugetlbfs_sb_info *sbinfo;

	save_mount_options(sb, data);

	config.nr_blocks = -1; /* No limit on size by default */
	config.nr_inodes = -1; /* No limit on number of inodes by default */
	config.uid = current_fsuid();
	config.gid = current_fsgid();
	config.mode = 0755;
	config.hstate = &default_hstate;
	ret = hugetlbfs_parse_options(data, &config);
	if (ret)
		return ret;

	sbinfo = kmalloc(sizeof(struct hugetlbfs_sb_info), GFP_KERNEL);
	if (!sbinfo)
		return -ENOMEM;
	sb->s_fs_info = sbinfo;
	sbinfo->hstate = config.hstate;
	spin_lock_init(&sbinfo->stat_lock);
	sbinfo->max_blocks = config.nr_blocks;
	sbinfo->free_blocks = config.nr_blocks;
	sbinfo->max_inodes = config.nr_inodes;
	sbinfo->free_inodes = config.nr_inodes;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = huge_page_size(config.hstate);
	sb->s_blocksize_bits = huge_page_shift(config.hstate);
	sb->s_magic = HUGETLBFS_MAGIC;
	sb->s_op = &hugetlbfs_ops;
	sb->s_time_gran = 1;
	inode = hugetlbfs_get_inode(sb, config.uid, config.gid,
					S_IFDIR | config.mode, 0);
	if (!inode)
		goto out_free;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		goto out_free;
	}
	sb->s_root = root;
	return 0;
out_free:
	kfree(sbinfo);
	return -ENOMEM;
}

int hugetlb_get_quota(struct address_space *mapping, long delta)
{
	int ret = 0;
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(mapping->host->i_sb);

	if (sbinfo->free_blocks > -1) {
		spin_lock(&sbinfo->stat_lock);
		if (sbinfo->free_blocks - delta >= 0)
			sbinfo->free_blocks -= delta;
		else
			ret = -ENOMEM;
		spin_unlock(&sbinfo->stat_lock);
	}

	return ret;
}

void hugetlb_put_quota(struct address_space *mapping, long delta)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(mapping->host->i_sb);

	if (sbinfo->free_blocks > -1) {
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_blocks += delta;
		spin_unlock(&sbinfo->stat_lock);
	}
}

static int hugetlbfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(fs_type, flags, data, hugetlbfs_fill_super, mnt);
}

static struct file_system_type hugetlbfs_fs_type = {
	.name		= "hugetlbfs",
	.get_sb		= hugetlbfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static struct vfsmount *hugetlbfs_vfsmount;

static int can_do_hugetlb_shm(void)
{
	return capable(CAP_IPC_LOCK) || in_group_p(sysctl_hugetlb_shm_group);
}

struct file *hugetlb_file_setup(const char *name, size_t size, int acctflag,
				struct user_struct **user, int creat_flags)
{
	int error = -ENOMEM;
	struct file *file;
	struct inode *inode;
	struct path path;
	struct dentry *root;
	struct qstr quick_string;

	*user = NULL;
	if (!hugetlbfs_vfsmount)
		return ERR_PTR(-ENOENT);

	if (creat_flags == HUGETLB_SHMFS_INODE && !can_do_hugetlb_shm()) {
		*user = current_user();
		if (user_shm_lock(size, *user)) {
			WARN_ONCE(1,
			  "Using mlock ulimits for SHM_HUGETLB deprecated\n");
		} else {
			*user = NULL;
			return ERR_PTR(-EPERM);
		}
	}

	root = hugetlbfs_vfsmount->mnt_root;
	quick_string.name = name;
	quick_string.len = strlen(quick_string.name);
	quick_string.hash = 0;
	path.dentry = d_alloc(root, &quick_string);
	if (!path.dentry)
		goto out_shm_unlock;

	path.mnt = mntget(hugetlbfs_vfsmount);
	error = -ENOSPC;
	inode = hugetlbfs_get_inode(root->d_sb, current_fsuid(),
				current_fsgid(), S_IFREG | S_IRWXUGO, 0);
	if (!inode)
		goto out_dentry;

	error = -ENOMEM;
	if (hugetlb_reserve_pages(inode, 0,
			size >> huge_page_shift(hstate_inode(inode)), NULL,
			acctflag))
		goto out_inode;

	d_instantiate(path.dentry, inode);
	inode->i_size = size;
	inode->i_nlink = 0;

	error = -ENFILE;
	file = alloc_file(&path, FMODE_WRITE | FMODE_READ,
			&hugetlbfs_file_operations);
	if (!file)
		goto out_dentry; /* inode is already attached */

	return file;

out_inode:
	iput(inode);
out_dentry:
	path_put(&path);
out_shm_unlock:
	if (*user) {
		user_shm_unlock(size, *user);
		*user = NULL;
	}
	return ERR_PTR(error);
}

static int __init init_hugetlbfs_fs(void)
{
	int error;
	struct vfsmount *vfsmount;

	error = bdi_init(&hugetlbfs_backing_dev_info);
	if (error)
		return error;

	hugetlbfs_inode_cachep = kmem_cache_create("hugetlbfs_inode_cache",
					sizeof(struct hugetlbfs_inode_info),
					0, 0, init_once);
	if (hugetlbfs_inode_cachep == NULL)
		goto out2;

	error = register_filesystem(&hugetlbfs_fs_type);
	if (error)
		goto out;

	vfsmount = kern_mount(&hugetlbfs_fs_type);

	if (!IS_ERR(vfsmount)) {
		hugetlbfs_vfsmount = vfsmount;
		return 0;
	}

	error = PTR_ERR(vfsmount);

 out:
	if (error)
		kmem_cache_destroy(hugetlbfs_inode_cachep);
 out2:
	bdi_destroy(&hugetlbfs_backing_dev_info);
	return error;
}

static void __exit exit_hugetlbfs_fs(void)
{
	kmem_cache_destroy(hugetlbfs_inode_cachep);
	unregister_filesystem(&hugetlbfs_fs_type);
	bdi_destroy(&hugetlbfs_backing_dev_info);
}

module_init(init_hugetlbfs_fs)
module_exit(exit_hugetlbfs_fs)

MODULE_LICENSE("GPL");
