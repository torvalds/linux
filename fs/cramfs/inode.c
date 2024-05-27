/*
 * Compressed rom filesystem for Linux.
 *
 * Copyright (C) 1999 Linus Torvalds.
 *
 * This file is released under the GPL.
 */

/*
 * These are the VFS interfaces to the compressed rom filesystem.
 * The actual compression is based on zlib, see the other files.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/pfn_t.h>
#include <linux/ramfs.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/blkdev.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/super.h>
#include <linux/fs_context.h>
#include <linux/slab.h>
#include <linux/vfs.h>
#include <linux/mutex.h>
#include <uapi/linux/cramfs_fs.h>
#include <linux/uaccess.h>

#include "internal.h"

/*
 * cramfs super-block data in memory
 */
struct cramfs_sb_info {
	unsigned long magic;
	unsigned long size;
	unsigned long blocks;
	unsigned long files;
	unsigned long flags;
	void *linear_virt_addr;
	resource_size_t linear_phys_addr;
	size_t mtd_point_size;
};

static inline struct cramfs_sb_info *CRAMFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static const struct super_operations cramfs_ops;
static const struct inode_operations cramfs_dir_inode_operations;
static const struct file_operations cramfs_directory_operations;
static const struct file_operations cramfs_physmem_fops;
static const struct address_space_operations cramfs_aops;

static DEFINE_MUTEX(read_mutex);


/* These macros may change in future, to provide better st_ino semantics. */
#define OFFSET(x)	((x)->i_ino)

static unsigned long cramino(const struct cramfs_inode *cino, unsigned int offset)
{
	if (!cino->offset)
		return offset + 1;
	if (!cino->size)
		return offset + 1;

	/*
	 * The file mode test fixes buggy mkcramfs implementations where
	 * cramfs_inode->offset is set to a non zero value for entries
	 * which did not contain data, like devices node and fifos.
	 */
	switch (cino->mode & S_IFMT) {
	case S_IFREG:
	case S_IFDIR:
	case S_IFLNK:
		return cino->offset << 2;
	default:
		break;
	}
	return offset + 1;
}

static struct inode *get_cramfs_inode(struct super_block *sb,
	const struct cramfs_inode *cramfs_inode, unsigned int offset)
{
	struct inode *inode;
	static struct timespec64 zerotime;

	inode = iget_locked(sb, cramino(cramfs_inode, offset));
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	switch (cramfs_inode->mode & S_IFMT) {
	case S_IFREG:
		inode->i_fop = &generic_ro_fops;
		inode->i_data.a_ops = &cramfs_aops;
		if (IS_ENABLED(CONFIG_CRAMFS_MTD) &&
		    CRAMFS_SB(sb)->flags & CRAMFS_FLAG_EXT_BLOCK_POINTERS &&
		    CRAMFS_SB(sb)->linear_phys_addr)
			inode->i_fop = &cramfs_physmem_fops;
		break;
	case S_IFDIR:
		inode->i_op = &cramfs_dir_inode_operations;
		inode->i_fop = &cramfs_directory_operations;
		break;
	case S_IFLNK:
		inode->i_op = &page_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_data.a_ops = &cramfs_aops;
		break;
	default:
		init_special_inode(inode, cramfs_inode->mode,
				old_decode_dev(cramfs_inode->size));
	}

	inode->i_mode = cramfs_inode->mode;
	i_uid_write(inode, cramfs_inode->uid);
	i_gid_write(inode, cramfs_inode->gid);

	/* if the lower 2 bits are zero, the inode contains data */
	if (!(inode->i_ino & 3)) {
		inode->i_size = cramfs_inode->size;
		inode->i_blocks = (cramfs_inode->size - 1) / 512 + 1;
	}

	/* Struct copy intentional */
	inode_set_mtime_to_ts(inode,
			      inode_set_atime_to_ts(inode, inode_set_ctime_to_ts(inode, zerotime)));
	/* inode->i_nlink is left 1 - arguably wrong for directories,
	   but it's the best we can do without reading the directory
	   contents.  1 yields the right result in GNU find, even
	   without -noleaf option. */

	unlock_new_inode(inode);

	return inode;
}

/*
 * We have our own block cache: don't fill up the buffer cache
 * with the rom-image, because the way the filesystem is set
 * up the accesses should be fairly regular and cached in the
 * page cache and dentry tree anyway..
 *
 * This also acts as a way to guarantee contiguous areas of up to
 * BLKS_PER_BUF*PAGE_SIZE, so that the caller doesn't need to
 * worry about end-of-buffer issues even when decompressing a full
 * page cache.
 *
 * Note: This is all optimized away at compile time when
 *       CONFIG_CRAMFS_BLOCKDEV=n.
 */
#define READ_BUFFERS (2)
/* NEXT_BUFFER(): Loop over [0..(READ_BUFFERS-1)]. */
#define NEXT_BUFFER(_ix) ((_ix) ^ 1)

/*
 * BLKS_PER_BUF_SHIFT should be at least 2 to allow for "compressed"
 * data that takes up more space than the original and with unlucky
 * alignment.
 */
#define BLKS_PER_BUF_SHIFT	(2)
#define BLKS_PER_BUF		(1 << BLKS_PER_BUF_SHIFT)
#define BUFFER_SIZE		(BLKS_PER_BUF*PAGE_SIZE)

static unsigned char read_buffers[READ_BUFFERS][BUFFER_SIZE];
static unsigned buffer_blocknr[READ_BUFFERS];
static struct super_block *buffer_dev[READ_BUFFERS];
static int next_buffer;

/*
 * Populate our block cache and return a pointer to it.
 */
static void *cramfs_blkdev_read(struct super_block *sb, unsigned int offset,
				unsigned int len)
{
	struct address_space *mapping = sb->s_bdev->bd_mapping;
	struct file_ra_state ra = {};
	struct page *pages[BLKS_PER_BUF];
	unsigned i, blocknr, buffer;
	unsigned long devsize;
	char *data;

	if (!len)
		return NULL;
	blocknr = offset >> PAGE_SHIFT;
	offset &= PAGE_SIZE - 1;

	/* Check if an existing buffer already has the data.. */
	for (i = 0; i < READ_BUFFERS; i++) {
		unsigned int blk_offset;

		if (buffer_dev[i] != sb)
			continue;
		if (blocknr < buffer_blocknr[i])
			continue;
		blk_offset = (blocknr - buffer_blocknr[i]) << PAGE_SHIFT;
		blk_offset += offset;
		if (blk_offset > BUFFER_SIZE ||
		    blk_offset + len > BUFFER_SIZE)
			continue;
		return read_buffers[i] + blk_offset;
	}

	devsize = bdev_nr_bytes(sb->s_bdev) >> PAGE_SHIFT;

	/* Ok, read in BLKS_PER_BUF pages completely first. */
	file_ra_state_init(&ra, mapping);
	page_cache_sync_readahead(mapping, &ra, NULL, blocknr, BLKS_PER_BUF);

	for (i = 0; i < BLKS_PER_BUF; i++) {
		struct page *page = NULL;

		if (blocknr + i < devsize) {
			page = read_mapping_page(mapping, blocknr + i, NULL);
			/* synchronous error? */
			if (IS_ERR(page))
				page = NULL;
		}
		pages[i] = page;
	}

	buffer = next_buffer;
	next_buffer = NEXT_BUFFER(buffer);
	buffer_blocknr[buffer] = blocknr;
	buffer_dev[buffer] = sb;

	data = read_buffers[buffer];
	for (i = 0; i < BLKS_PER_BUF; i++) {
		struct page *page = pages[i];

		if (page) {
			memcpy_from_page(data, page, 0, PAGE_SIZE);
			put_page(page);
		} else
			memset(data, 0, PAGE_SIZE);
		data += PAGE_SIZE;
	}
	return read_buffers[buffer] + offset;
}

/*
 * Return a pointer to the linearly addressed cramfs image in memory.
 */
static void *cramfs_direct_read(struct super_block *sb, unsigned int offset,
				unsigned int len)
{
	struct cramfs_sb_info *sbi = CRAMFS_SB(sb);

	if (!len)
		return NULL;
	if (len > sbi->size || offset > sbi->size - len)
		return page_address(ZERO_PAGE(0));
	return sbi->linear_virt_addr + offset;
}

/*
 * Returns a pointer to a buffer containing at least LEN bytes of
 * filesystem starting at byte offset OFFSET into the filesystem.
 */
static void *cramfs_read(struct super_block *sb, unsigned int offset,
			 unsigned int len)
{
	struct cramfs_sb_info *sbi = CRAMFS_SB(sb);

	if (IS_ENABLED(CONFIG_CRAMFS_MTD) && sbi->linear_virt_addr)
		return cramfs_direct_read(sb, offset, len);
	else if (IS_ENABLED(CONFIG_CRAMFS_BLOCKDEV))
		return cramfs_blkdev_read(sb, offset, len);
	else
		return NULL;
}

/*
 * For a mapping to be possible, we need a range of uncompressed and
 * contiguous blocks. Return the offset for the first block and number of
 * valid blocks for which that is true, or zero otherwise.
 */
static u32 cramfs_get_block_range(struct inode *inode, u32 pgoff, u32 *pages)
{
	struct cramfs_sb_info *sbi = CRAMFS_SB(inode->i_sb);
	int i;
	u32 *blockptrs, first_block_addr;

	/*
	 * We can dereference memory directly here as this code may be
	 * reached only when there is a direct filesystem image mapping
	 * available in memory.
	 */
	blockptrs = (u32 *)(sbi->linear_virt_addr + OFFSET(inode) + pgoff * 4);
	first_block_addr = blockptrs[0] & ~CRAMFS_BLK_FLAGS;
	i = 0;
	do {
		u32 block_off = i * (PAGE_SIZE >> CRAMFS_BLK_DIRECT_PTR_SHIFT);
		u32 expect = (first_block_addr + block_off) |
			     CRAMFS_BLK_FLAG_DIRECT_PTR |
			     CRAMFS_BLK_FLAG_UNCOMPRESSED;
		if (blockptrs[i] != expect) {
			pr_debug("range: block %d/%d got %#x expects %#x\n",
				 pgoff+i, pgoff + *pages - 1,
				 blockptrs[i], expect);
			if (i == 0)
				return 0;
			break;
		}
	} while (++i < *pages);

	*pages = i;
	return first_block_addr << CRAMFS_BLK_DIRECT_PTR_SHIFT;
}

#ifdef CONFIG_MMU

/*
 * Return true if the last page of a file in the filesystem image contains
 * some other data that doesn't belong to that file. It is assumed that the
 * last block is CRAMFS_BLK_FLAG_DIRECT_PTR | CRAMFS_BLK_FLAG_UNCOMPRESSED
 * (verified by cramfs_get_block_range() and directly accessible in memory.
 */
static bool cramfs_last_page_is_shared(struct inode *inode)
{
	struct cramfs_sb_info *sbi = CRAMFS_SB(inode->i_sb);
	u32 partial, last_page, blockaddr, *blockptrs;
	char *tail_data;

	partial = offset_in_page(inode->i_size);
	if (!partial)
		return false;
	last_page = inode->i_size >> PAGE_SHIFT;
	blockptrs = (u32 *)(sbi->linear_virt_addr + OFFSET(inode));
	blockaddr = blockptrs[last_page] & ~CRAMFS_BLK_FLAGS;
	blockaddr <<= CRAMFS_BLK_DIRECT_PTR_SHIFT;
	tail_data = sbi->linear_virt_addr + blockaddr + partial;
	return memchr_inv(tail_data, 0, PAGE_SIZE - partial) ? true : false;
}

static int cramfs_physmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(file);
	struct cramfs_sb_info *sbi = CRAMFS_SB(inode->i_sb);
	unsigned int pages, max_pages, offset;
	unsigned long address, pgoff = vma->vm_pgoff;
	char *bailout_reason;
	int ret;

	ret = generic_file_readonly_mmap(file, vma);
	if (ret)
		return ret;

	/*
	 * Now try to pre-populate ptes for this vma with a direct
	 * mapping avoiding memory allocation when possible.
	 */

	/* Could COW work here? */
	bailout_reason = "vma is writable";
	if (vma->vm_flags & VM_WRITE)
		goto bailout;

	max_pages = (inode->i_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	bailout_reason = "beyond file limit";
	if (pgoff >= max_pages)
		goto bailout;
	pages = min(vma_pages(vma), max_pages - pgoff);

	offset = cramfs_get_block_range(inode, pgoff, &pages);
	bailout_reason = "unsuitable block layout";
	if (!offset)
		goto bailout;
	address = sbi->linear_phys_addr + offset;
	bailout_reason = "data is not page aligned";
	if (!PAGE_ALIGNED(address))
		goto bailout;

	/* Don't map the last page if it contains some other data */
	if (pgoff + pages == max_pages && cramfs_last_page_is_shared(inode)) {
		pr_debug("mmap: %pD: last page is shared\n", file);
		pages--;
	}

	if (!pages) {
		bailout_reason = "no suitable block remaining";
		goto bailout;
	}

	if (pages == vma_pages(vma)) {
		/*
		 * The entire vma is mappable. remap_pfn_range() will
		 * make it distinguishable from a non-direct mapping
		 * in /proc/<pid>/maps by substituting the file offset
		 * with the actual physical address.
		 */
		ret = remap_pfn_range(vma, vma->vm_start, address >> PAGE_SHIFT,
				      pages * PAGE_SIZE, vma->vm_page_prot);
	} else {
		/*
		 * Let's create a mixed map if we can't map it all.
		 * The normal paging machinery will take care of the
		 * unpopulated ptes via cramfs_read_folio().
		 */
		int i;
		vm_flags_set(vma, VM_MIXEDMAP);
		for (i = 0; i < pages && !ret; i++) {
			vm_fault_t vmf;
			unsigned long off = i * PAGE_SIZE;
			pfn_t pfn = phys_to_pfn_t(address + off, PFN_DEV);
			vmf = vmf_insert_mixed(vma, vma->vm_start + off, pfn);
			if (vmf & VM_FAULT_ERROR)
				ret = vm_fault_to_errno(vmf, 0);
		}
	}

	if (!ret)
		pr_debug("mapped %pD[%lu] at 0x%08lx (%u/%lu pages) "
			 "to vma 0x%08lx, page_prot 0x%llx\n", file,
			 pgoff, address, pages, vma_pages(vma), vma->vm_start,
			 (unsigned long long)pgprot_val(vma->vm_page_prot));
	return ret;

bailout:
	pr_debug("%pD[%lu]: direct mmap impossible: %s\n",
		 file, pgoff, bailout_reason);
	/* Didn't manage any direct map, but normal paging is still possible */
	return 0;
}

#else /* CONFIG_MMU */

static int cramfs_physmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	return is_nommu_shared_mapping(vma->vm_flags) ? 0 : -ENOSYS;
}

static unsigned long cramfs_physmem_get_unmapped_area(struct file *file,
			unsigned long addr, unsigned long len,
			unsigned long pgoff, unsigned long flags)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct cramfs_sb_info *sbi = CRAMFS_SB(sb);
	unsigned int pages, block_pages, max_pages, offset;

	pages = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	max_pages = (inode->i_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (pgoff >= max_pages || pages > max_pages - pgoff)
		return -EINVAL;
	block_pages = pages;
	offset = cramfs_get_block_range(inode, pgoff, &block_pages);
	if (!offset || block_pages != pages)
		return -ENOSYS;
	addr = sbi->linear_phys_addr + offset;
	pr_debug("get_unmapped for %pD ofs %#lx siz %lu at 0x%08lx\n",
		 file, pgoff*PAGE_SIZE, len, addr);
	return addr;
}

static unsigned int cramfs_physmem_mmap_capabilities(struct file *file)
{
	return NOMMU_MAP_COPY | NOMMU_MAP_DIRECT |
	       NOMMU_MAP_READ | NOMMU_MAP_EXEC;
}

#endif /* CONFIG_MMU */

static const struct file_operations cramfs_physmem_fops = {
	.llseek			= generic_file_llseek,
	.read_iter		= generic_file_read_iter,
	.splice_read		= filemap_splice_read,
	.mmap			= cramfs_physmem_mmap,
#ifndef CONFIG_MMU
	.get_unmapped_area	= cramfs_physmem_get_unmapped_area,
	.mmap_capabilities	= cramfs_physmem_mmap_capabilities,
#endif
};

static void cramfs_kill_sb(struct super_block *sb)
{
	struct cramfs_sb_info *sbi = CRAMFS_SB(sb);

	generic_shutdown_super(sb);

	if (IS_ENABLED(CONFIG_CRAMFS_MTD) && sb->s_mtd) {
		if (sbi && sbi->mtd_point_size)
			mtd_unpoint(sb->s_mtd, 0, sbi->mtd_point_size);
		put_mtd_device(sb->s_mtd);
		sb->s_mtd = NULL;
	} else if (IS_ENABLED(CONFIG_CRAMFS_BLOCKDEV) && sb->s_bdev) {
		sync_blockdev(sb->s_bdev);
		bdev_fput(sb->s_bdev_file);
	}
	kfree(sbi);
}

static int cramfs_reconfigure(struct fs_context *fc)
{
	sync_filesystem(fc->root->d_sb);
	fc->sb_flags |= SB_RDONLY;
	return 0;
}

static int cramfs_read_super(struct super_block *sb, struct fs_context *fc,
			     struct cramfs_super *super)
{
	struct cramfs_sb_info *sbi = CRAMFS_SB(sb);
	unsigned long root_offset;
	bool silent = fc->sb_flags & SB_SILENT;

	/* We don't know the real size yet */
	sbi->size = PAGE_SIZE;

	/* Read the first block and get the superblock from it */
	mutex_lock(&read_mutex);
	memcpy(super, cramfs_read(sb, 0, sizeof(*super)), sizeof(*super));
	mutex_unlock(&read_mutex);

	/* Do sanity checks on the superblock */
	if (super->magic != CRAMFS_MAGIC) {
		/* check for wrong endianness */
		if (super->magic == CRAMFS_MAGIC_WEND) {
			if (!silent)
				errorfc(fc, "wrong endianness");
			return -EINVAL;
		}

		/* check at 512 byte offset */
		mutex_lock(&read_mutex);
		memcpy(super,
		       cramfs_read(sb, 512, sizeof(*super)),
		       sizeof(*super));
		mutex_unlock(&read_mutex);
		if (super->magic != CRAMFS_MAGIC) {
			if (super->magic == CRAMFS_MAGIC_WEND && !silent)
				errorfc(fc, "wrong endianness");
			else if (!silent)
				errorfc(fc, "wrong magic");
			return -EINVAL;
		}
	}

	/* get feature flags first */
	if (super->flags & ~CRAMFS_SUPPORTED_FLAGS) {
		errorfc(fc, "unsupported filesystem features");
		return -EINVAL;
	}

	/* Check that the root inode is in a sane state */
	if (!S_ISDIR(super->root.mode)) {
		errorfc(fc, "root is not a directory");
		return -EINVAL;
	}
	/* correct strange, hard-coded permissions of mkcramfs */
	super->root.mode |= 0555;

	root_offset = super->root.offset << 2;
	if (super->flags & CRAMFS_FLAG_FSID_VERSION_2) {
		sbi->size = super->size;
		sbi->blocks = super->fsid.blocks;
		sbi->files = super->fsid.files;
	} else {
		sbi->size = 1<<28;
		sbi->blocks = 0;
		sbi->files = 0;
	}
	sbi->magic = super->magic;
	sbi->flags = super->flags;
	if (root_offset == 0)
		infofc(fc, "empty filesystem");
	else if (!(super->flags & CRAMFS_FLAG_SHIFTED_ROOT_OFFSET) &&
		 ((root_offset != sizeof(struct cramfs_super)) &&
		  (root_offset != 512 + sizeof(struct cramfs_super))))
	{
		errorfc(fc, "bad root offset %lu", root_offset);
		return -EINVAL;
	}

	return 0;
}

static int cramfs_finalize_super(struct super_block *sb,
				 struct cramfs_inode *cramfs_root)
{
	struct inode *root;

	/* Set it all up.. */
	sb->s_flags |= SB_RDONLY;
	sb->s_time_min = 0;
	sb->s_time_max = 0;
	sb->s_op = &cramfs_ops;
	root = get_cramfs_inode(sb, cramfs_root, 0);
	if (IS_ERR(root))
		return PTR_ERR(root);
	sb->s_root = d_make_root(root);
	if (!sb->s_root)
		return -ENOMEM;
	return 0;
}

static int cramfs_blkdev_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct cramfs_sb_info *sbi;
	struct cramfs_super super;
	int i, err;

	sbi = kzalloc(sizeof(struct cramfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	sb->s_fs_info = sbi;

	/* Invalidate the read buffers on mount: think disk change.. */
	for (i = 0; i < READ_BUFFERS; i++)
		buffer_blocknr[i] = -1;

	err = cramfs_read_super(sb, fc, &super);
	if (err)
		return err;
	return cramfs_finalize_super(sb, &super.root);
}

static int cramfs_mtd_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct cramfs_sb_info *sbi;
	struct cramfs_super super;
	int err;

	sbi = kzalloc(sizeof(struct cramfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	sb->s_fs_info = sbi;

	/* Map only one page for now.  Will remap it when fs size is known. */
	err = mtd_point(sb->s_mtd, 0, PAGE_SIZE, &sbi->mtd_point_size,
			&sbi->linear_virt_addr, &sbi->linear_phys_addr);
	if (err || sbi->mtd_point_size != PAGE_SIZE) {
		pr_err("unable to get direct memory access to mtd:%s\n",
		       sb->s_mtd->name);
		return err ? : -ENODATA;
	}

	pr_info("checking physical address %pap for linear cramfs image\n",
		&sbi->linear_phys_addr);
	err = cramfs_read_super(sb, fc, &super);
	if (err)
		return err;

	/* Remap the whole filesystem now */
	pr_info("linear cramfs image on mtd:%s appears to be %lu KB in size\n",
		sb->s_mtd->name, sbi->size/1024);
	mtd_unpoint(sb->s_mtd, 0, PAGE_SIZE);
	err = mtd_point(sb->s_mtd, 0, sbi->size, &sbi->mtd_point_size,
			&sbi->linear_virt_addr, &sbi->linear_phys_addr);
	if (err || sbi->mtd_point_size != sbi->size) {
		pr_err("unable to get direct memory access to mtd:%s\n",
		       sb->s_mtd->name);
		return err ? : -ENODATA;
	}

	return cramfs_finalize_super(sb, &super.root);
}

static int cramfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	u64 id = 0;

	if (sb->s_bdev)
		id = huge_encode_dev(sb->s_bdev->bd_dev);
	else if (sb->s_dev)
		id = huge_encode_dev(sb->s_dev);

	buf->f_type = CRAMFS_MAGIC;
	buf->f_bsize = PAGE_SIZE;
	buf->f_blocks = CRAMFS_SB(sb)->blocks;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_files = CRAMFS_SB(sb)->files;
	buf->f_ffree = 0;
	buf->f_fsid = u64_to_fsid(id);
	buf->f_namelen = CRAMFS_MAXPATHLEN;
	return 0;
}

/*
 * Read a cramfs directory entry.
 */
static int cramfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	char *buf;
	unsigned int offset;

	/* Offset within the thing. */
	if (ctx->pos >= inode->i_size)
		return 0;
	offset = ctx->pos;
	/* Directory entries are always 4-byte aligned */
	if (offset & 3)
		return -EINVAL;

	buf = kmalloc(CRAMFS_MAXPATHLEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	while (offset < inode->i_size) {
		struct cramfs_inode *de;
		unsigned long nextoffset;
		char *name;
		ino_t ino;
		umode_t mode;
		int namelen;

		mutex_lock(&read_mutex);
		de = cramfs_read(sb, OFFSET(inode) + offset, sizeof(*de)+CRAMFS_MAXPATHLEN);
		name = (char *)(de+1);

		/*
		 * Namelengths on disk are shifted by two
		 * and the name padded out to 4-byte boundaries
		 * with zeroes.
		 */
		namelen = de->namelen << 2;
		memcpy(buf, name, namelen);
		ino = cramino(de, OFFSET(inode) + offset);
		mode = de->mode;
		mutex_unlock(&read_mutex);
		nextoffset = offset + sizeof(*de) + namelen;
		for (;;) {
			if (!namelen) {
				kfree(buf);
				return -EIO;
			}
			if (buf[namelen-1])
				break;
			namelen--;
		}
		if (!dir_emit(ctx, buf, namelen, ino, mode >> 12))
			break;

		ctx->pos = offset = nextoffset;
	}
	kfree(buf);
	return 0;
}

/*
 * Lookup and fill in the inode data..
 */
static struct dentry *cramfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	unsigned int offset = 0;
	struct inode *inode = NULL;
	int sorted;

	mutex_lock(&read_mutex);
	sorted = CRAMFS_SB(dir->i_sb)->flags & CRAMFS_FLAG_SORTED_DIRS;
	while (offset < dir->i_size) {
		struct cramfs_inode *de;
		char *name;
		int namelen, retval;
		int dir_off = OFFSET(dir) + offset;

		de = cramfs_read(dir->i_sb, dir_off, sizeof(*de)+CRAMFS_MAXPATHLEN);
		name = (char *)(de+1);

		/* Try to take advantage of sorted directories */
		if (sorted && (dentry->d_name.name[0] < name[0]))
			break;

		namelen = de->namelen << 2;
		offset += sizeof(*de) + namelen;

		/* Quick check that the name is roughly the right length */
		if (((dentry->d_name.len + 3) & ~3) != namelen)
			continue;

		for (;;) {
			if (!namelen) {
				inode = ERR_PTR(-EIO);
				goto out;
			}
			if (name[namelen-1])
				break;
			namelen--;
		}
		if (namelen != dentry->d_name.len)
			continue;
		retval = memcmp(dentry->d_name.name, name, namelen);
		if (retval > 0)
			continue;
		if (!retval) {
			inode = get_cramfs_inode(dir->i_sb, de, dir_off);
			break;
		}
		/* else (retval < 0) */
		if (sorted)
			break;
	}
out:
	mutex_unlock(&read_mutex);
	return d_splice_alias(inode, dentry);
}

static int cramfs_read_folio(struct file *file, struct folio *folio)
{
	struct page *page = &folio->page;
	struct inode *inode = page->mapping->host;
	u32 maxblock;
	int bytes_filled;
	void *pgdata;

	maxblock = (inode->i_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	bytes_filled = 0;
	pgdata = kmap_local_page(page);

	if (page->index < maxblock) {
		struct super_block *sb = inode->i_sb;
		u32 blkptr_offset = OFFSET(inode) + page->index * 4;
		u32 block_ptr, block_start, block_len;
		bool uncompressed, direct;

		mutex_lock(&read_mutex);
		block_ptr = *(u32 *) cramfs_read(sb, blkptr_offset, 4);
		uncompressed = (block_ptr & CRAMFS_BLK_FLAG_UNCOMPRESSED);
		direct = (block_ptr & CRAMFS_BLK_FLAG_DIRECT_PTR);
		block_ptr &= ~CRAMFS_BLK_FLAGS;

		if (direct) {
			/*
			 * The block pointer is an absolute start pointer,
			 * shifted by 2 bits. The size is included in the
			 * first 2 bytes of the data block when compressed,
			 * or PAGE_SIZE otherwise.
			 */
			block_start = block_ptr << CRAMFS_BLK_DIRECT_PTR_SHIFT;
			if (uncompressed) {
				block_len = PAGE_SIZE;
				/* if last block: cap to file length */
				if (page->index == maxblock - 1)
					block_len =
						offset_in_page(inode->i_size);
			} else {
				block_len = *(u16 *)
					cramfs_read(sb, block_start, 2);
				block_start += 2;
			}
		} else {
			/*
			 * The block pointer indicates one past the end of
			 * the current block (start of next block). If this
			 * is the first block then it starts where the block
			 * pointer table ends, otherwise its start comes
			 * from the previous block's pointer.
			 */
			block_start = OFFSET(inode) + maxblock * 4;
			if (page->index)
				block_start = *(u32 *)
					cramfs_read(sb, blkptr_offset - 4, 4);
			/* Beware... previous ptr might be a direct ptr */
			if (unlikely(block_start & CRAMFS_BLK_FLAG_DIRECT_PTR)) {
				/* See comments on earlier code. */
				u32 prev_start = block_start;
				block_start = prev_start & ~CRAMFS_BLK_FLAGS;
				block_start <<= CRAMFS_BLK_DIRECT_PTR_SHIFT;
				if (prev_start & CRAMFS_BLK_FLAG_UNCOMPRESSED) {
					block_start += PAGE_SIZE;
				} else {
					block_len = *(u16 *)
						cramfs_read(sb, block_start, 2);
					block_start += 2 + block_len;
				}
			}
			block_start &= ~CRAMFS_BLK_FLAGS;
			block_len = block_ptr - block_start;
		}

		if (block_len == 0)
			; /* hole */
		else if (unlikely(block_len > 2*PAGE_SIZE ||
				  (uncompressed && block_len > PAGE_SIZE))) {
			mutex_unlock(&read_mutex);
			pr_err("bad data blocksize %u\n", block_len);
			goto err;
		} else if (uncompressed) {
			memcpy(pgdata,
			       cramfs_read(sb, block_start, block_len),
			       block_len);
			bytes_filled = block_len;
		} else {
			bytes_filled = cramfs_uncompress_block(pgdata,
				 PAGE_SIZE,
				 cramfs_read(sb, block_start, block_len),
				 block_len);
		}
		mutex_unlock(&read_mutex);
		if (unlikely(bytes_filled < 0))
			goto err;
	}

	memset(pgdata + bytes_filled, 0, PAGE_SIZE - bytes_filled);
	flush_dcache_page(page);
	kunmap_local(pgdata);
	SetPageUptodate(page);
	unlock_page(page);
	return 0;

err:
	kunmap_local(pgdata);
	ClearPageUptodate(page);
	SetPageError(page);
	unlock_page(page);
	return 0;
}

static const struct address_space_operations cramfs_aops = {
	.read_folio = cramfs_read_folio
};

/*
 * Our operations:
 */

/*
 * A directory can only readdir
 */
static const struct file_operations cramfs_directory_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= cramfs_readdir,
};

static const struct inode_operations cramfs_dir_inode_operations = {
	.lookup		= cramfs_lookup,
};

static const struct super_operations cramfs_ops = {
	.statfs		= cramfs_statfs,
};

static int cramfs_get_tree(struct fs_context *fc)
{
	int ret = -ENOPROTOOPT;

	if (IS_ENABLED(CONFIG_CRAMFS_MTD)) {
		ret = get_tree_mtd(fc, cramfs_mtd_fill_super);
		if (!ret)
			return 0;
	}
	if (IS_ENABLED(CONFIG_CRAMFS_BLOCKDEV))
		ret = get_tree_bdev(fc, cramfs_blkdev_fill_super);
	return ret;
}

static const struct fs_context_operations cramfs_context_ops = {
	.get_tree	= cramfs_get_tree,
	.reconfigure	= cramfs_reconfigure,
};

/*
 * Set up the filesystem mount context.
 */
static int cramfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &cramfs_context_ops;
	return 0;
}

static struct file_system_type cramfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "cramfs",
	.init_fs_context = cramfs_init_fs_context,
	.kill_sb	= cramfs_kill_sb,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("cramfs");

static int __init init_cramfs_fs(void)
{
	int rv;

	rv = cramfs_uncompress_init();
	if (rv < 0)
		return rv;
	rv = register_filesystem(&cramfs_fs_type);
	if (rv < 0)
		cramfs_uncompress_exit();
	return rv;
}

static void __exit exit_cramfs_fs(void)
{
	cramfs_uncompress_exit();
	unregister_filesystem(&cramfs_fs_type);
}

module_init(init_cramfs_fs)
module_exit(exit_cramfs_fs)
MODULE_DESCRIPTION("Compressed ROM file system support");
MODULE_LICENSE("GPL");
