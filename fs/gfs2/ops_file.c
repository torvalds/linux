/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/gfs2_ondisk.h>
#include <linux/ext2_fs.h>
#include <linux/crc32.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "lm_interface.h"
#include "incore.h"
#include "bmap.h"
#include "dir.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "lm.h"
#include "log.h"
#include "meta_io.h"
#include "ops_file.h"
#include "ops_vm.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"
#include "eaops.h"

/* "bad" is for NFS support */
struct filldir_bad_entry {
	char *fbe_name;
	unsigned int fbe_length;
	uint64_t fbe_offset;
	struct gfs2_inum fbe_inum;
	unsigned int fbe_type;
};

struct filldir_bad {
	struct gfs2_sbd *fdb_sbd;

	struct filldir_bad_entry *fdb_entry;
	unsigned int fdb_entry_num;
	unsigned int fdb_entry_off;

	char *fdb_name;
	unsigned int fdb_name_size;
	unsigned int fdb_name_off;
};

/* For regular, non-NFS */
struct filldir_reg {
	struct gfs2_sbd *fdr_sbd;
	int fdr_prefetch;

	filldir_t fdr_filldir;
	void *fdr_opaque;
};

/*
 * Most fields left uninitialised to catch anybody who tries to
 * use them. f_flags set to prevent file_accessed() from touching
 * any other part of this. Its use is purely as a flag so that we
 * know (in readpage()) whether or not do to locking.
 */
struct file gfs2_internal_file_sentinal = {
	.f_flags = O_NOATIME|O_RDONLY,
};

static int gfs2_read_actor(read_descriptor_t *desc, struct page *page,
			   unsigned long offset, unsigned long size)
{
	char *kaddr;
	unsigned long count = desc->count;

	if (size > count)
		size = count;

	kaddr = kmap(page);
	memcpy(desc->arg.buf, kaddr + offset, size);
        kunmap(page);

        desc->count = count - size;
        desc->written += size;
        desc->arg.buf += size;
        return size;
}

int gfs2_internal_read(struct gfs2_inode *ip, struct file_ra_state *ra_state,
		       char *buf, loff_t *pos, unsigned size)
{
	struct inode *inode = ip->i_vnode;
	read_descriptor_t desc;
	desc.written = 0;
	desc.arg.buf = buf;
	desc.count = size;
	desc.error = 0;
	do_generic_mapping_read(inode->i_mapping, ra_state,
				&gfs2_internal_file_sentinal, pos, &desc,
				gfs2_read_actor);
	return desc.written ? desc.written : desc.error;
}

/**
 * gfs2_llseek - seek to a location in a file
 * @file: the file
 * @offset: the offset
 * @origin: Where to seek from (SEEK_SET, SEEK_CUR, or SEEK_END)
 *
 * SEEK_END requires the glock for the file because it references the
 * file's size.
 *
 * Returns: The new offset, or errno
 */

static loff_t gfs2_llseek(struct file *file, loff_t offset, int origin)
{
	struct gfs2_inode *ip = file->f_mapping->host->u.generic_ip;
	struct gfs2_holder i_gh;
	loff_t error;

	if (origin == 2) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY,
					   &i_gh);
		if (!error) {
			error = remote_llseek(file, offset, origin);
			gfs2_glock_dq_uninit(&i_gh);
		}
	} else
		error = remote_llseek(file, offset, origin);

	return error;
}


static ssize_t gfs2_direct_IO_read(struct kiocb *iocb, const struct iovec *iov,
				   loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	ssize_t retval;

	retval = filemap_write_and_wait(mapping);
	if (retval == 0) {
		retval = mapping->a_ops->direct_IO(READ, iocb, iov, offset,
						   nr_segs);
	}
	return retval;
}

/**
 * __gfs2_file_aio_read - The main GFS2 read function
 * 
 * N.B. This is almost, but not quite the same as __generic_file_aio_read()
 * the important subtle different being that inode->i_size isn't valid
 * unless we are holding a lock, and we do this _only_ on the O_DIRECT
 * path since otherwise locking is done entirely at the page cache
 * layer.
 */
static ssize_t __gfs2_file_aio_read(struct kiocb *iocb,
				    const struct iovec *iov,
				    unsigned long nr_segs, loff_t *ppos)
{
	struct file *filp = iocb->ki_filp;
	struct gfs2_inode *ip = filp->f_mapping->host->u.generic_ip;
	struct gfs2_holder gh;
	ssize_t retval;
	unsigned long seg;
	size_t count;

	count = 0;
	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *iv = &iov[seg];

		/*
		 * If any segment has a negative length, or the cumulative
		 * length ever wraps negative then return -EINVAL.
		 */
		count += iv->iov_len;
		if (unlikely((ssize_t)(count|iv->iov_len) < 0))
			return -EINVAL;
		if (access_ok(VERIFY_WRITE, iv->iov_base, iv->iov_len))
			continue;
		if (seg == 0)
			return -EFAULT;
		nr_segs = seg;
		count -= iv->iov_len;   /* This segment is no good */
		break;
	}

	/* coalesce the iovecs and go direct-to-BIO for O_DIRECT */
	if (filp->f_flags & O_DIRECT) {
		loff_t pos = *ppos, size;
		struct address_space *mapping;
		struct inode *inode;

		mapping = filp->f_mapping;
		inode = mapping->host;
		retval = 0;
		if (!count)
			goto out; /* skip atime */

		gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &gh);
		retval = gfs2_glock_nq_m_atime(1, &gh);
		if (retval)
			goto out;
		if (gfs2_is_stuffed(ip)) {
			gfs2_glock_dq_m(1, &gh);
			gfs2_holder_uninit(&gh);
			goto fallback_to_normal;
		}
		size = i_size_read(inode);
		if (pos < size) {
			retval = gfs2_direct_IO_read(iocb, iov, pos, nr_segs);
			if (retval > 0 && !is_sync_kiocb(iocb))
				retval = -EIOCBQUEUED;
			if (retval > 0)
				*ppos = pos + retval;
		}
		file_accessed(filp);
		gfs2_glock_dq_m(1, &gh);
		gfs2_holder_uninit(&gh);
		goto out;
	}

fallback_to_normal:
	retval = 0;
	if (count) {
		for (seg = 0; seg < nr_segs; seg++) {
			read_descriptor_t desc;

			desc.written = 0;
			desc.arg.buf = iov[seg].iov_base;
			desc.count = iov[seg].iov_len;
			if (desc.count == 0)
				continue;
			desc.error = 0;
			do_generic_file_read(filp,ppos,&desc,file_read_actor);
			retval += desc.written;
			if (desc.error) {
				retval = retval ?: desc.error;
				 break;
			}
		}
	}
out:
	return retval;
}

/**
 * gfs2_read - Read bytes from a file
 * @file: The file to read from
 * @buf: The buffer to copy into
 * @size: The amount of data requested
 * @offset: The current file offset
 *
 * Outputs: Offset - updated according to number of bytes read
 *
 * Returns: The number of bytes read, errno on failure
 */

static ssize_t gfs2_read(struct file *filp, char __user *buf, size_t size,
			 loff_t *offset)
{
	struct iovec local_iov = { .iov_base = buf, .iov_len = size };
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	ret = __gfs2_file_aio_read(&kiocb, &local_iov, 1, offset);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&kiocb);
	return ret;
}

static ssize_t gfs2_file_readv(struct file *filp, const struct iovec *iov,
			       unsigned long nr_segs, loff_t *ppos)
{
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	ret = __gfs2_file_aio_read(&kiocb, iov, nr_segs, ppos);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&kiocb);
	return ret;
}

static ssize_t gfs2_file_aio_read(struct kiocb *iocb, char __user *buf,
				  size_t count, loff_t pos)
{
        struct iovec local_iov = { .iov_base = buf, .iov_len = count };

        BUG_ON(iocb->ki_pos != pos);
        return __gfs2_file_aio_read(iocb, &local_iov, 1, &iocb->ki_pos);
}


/**
 * filldir_reg_func - Report a directory entry to the caller of gfs2_dir_read()
 * @opaque: opaque data used by the function
 * @name: the name of the directory entry
 * @length: the length of the name
 * @offset: the entry's offset in the directory
 * @inum: the inode number the entry points to
 * @type: the type of inode the entry points to
 *
 * Returns: 0 on success, 1 if buffer full
 */

static int filldir_reg_func(void *opaque, const char *name, unsigned int length,
			    uint64_t offset, struct gfs2_inum *inum,
			    unsigned int type)
{
	struct filldir_reg *fdr = (struct filldir_reg *)opaque;
	struct gfs2_sbd *sdp = fdr->fdr_sbd;
	int error;

	error = fdr->fdr_filldir(fdr->fdr_opaque, name, length, offset,
				 inum->no_formal_ino, type);
	if (error)
		return 1;

	if (fdr->fdr_prefetch && !(length == 1 && *name == '.')) {
		gfs2_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs2_inode_glops,
				       LM_ST_SHARED, LM_FLAG_TRY | LM_FLAG_ANY);
		gfs2_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs2_iopen_glops,
				       LM_ST_SHARED, LM_FLAG_TRY);
	}

	return 0;
}

/**
 * readdir_reg - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * Returns: errno
 */

static int readdir_reg(struct file *file, void *dirent, filldir_t filldir)
{
	struct inode *dir = file->f_mapping->host;
	struct gfs2_inode *dip = dir->u.generic_ip;
	struct filldir_reg fdr;
	struct gfs2_holder d_gh;
	uint64_t offset = file->f_pos;
	int error;

	fdr.fdr_sbd = dip->i_sbd;
	fdr.fdr_prefetch = 1;
	fdr.fdr_filldir = filldir;
	fdr.fdr_opaque = dirent;

	gfs2_holder_init(dip->i_gl, LM_ST_SHARED, GL_ATIME, &d_gh);
	error = gfs2_glock_nq_atime(&d_gh);
	if (error) {
		gfs2_holder_uninit(&d_gh);
		return error;
	}

	error = gfs2_dir_read(dir, &offset, &fdr, filldir_reg_func);

	gfs2_glock_dq_uninit(&d_gh);

	file->f_pos = offset;

	return error;
}

/**
 * filldir_bad_func - Report a directory entry to the caller of gfs2_dir_read()
 * @opaque: opaque data used by the function
 * @name: the name of the directory entry
 * @length: the length of the name
 * @offset: the entry's offset in the directory
 * @inum: the inode number the entry points to
 * @type: the type of inode the entry points to
 *
 * For supporting NFS.
 *
 * Returns: 0 on success, 1 if buffer full
 */

static int filldir_bad_func(void *opaque, const char *name, unsigned int length,
			    uint64_t offset, struct gfs2_inum *inum,
			    unsigned int type)
{
	struct filldir_bad *fdb = (struct filldir_bad *)opaque;
	struct gfs2_sbd *sdp = fdb->fdb_sbd;
	struct filldir_bad_entry *fbe;

	if (fdb->fdb_entry_off == fdb->fdb_entry_num ||
	    fdb->fdb_name_off + length > fdb->fdb_name_size)
		return 1;

	fbe = &fdb->fdb_entry[fdb->fdb_entry_off];
	fbe->fbe_name = fdb->fdb_name + fdb->fdb_name_off;
	memcpy(fbe->fbe_name, name, length);
	fbe->fbe_length = length;
	fbe->fbe_offset = offset;
	fbe->fbe_inum = *inum;
	fbe->fbe_type = type;

	fdb->fdb_entry_off++;
	fdb->fdb_name_off += length;

	if (!(length == 1 && *name == '.')) {
		gfs2_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs2_inode_glops,
				       LM_ST_SHARED, LM_FLAG_TRY | LM_FLAG_ANY);
		gfs2_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs2_iopen_glops,
				       LM_ST_SHARED, LM_FLAG_TRY);
	}

	return 0;
}

/**
 * readdir_bad - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * For supporting NFS.
 *
 * Returns: errno
 */

static int readdir_bad(struct file *file, void *dirent, filldir_t filldir)
{
	struct inode *dir = file->f_mapping->host;
	struct gfs2_inode *dip = dir->u.generic_ip;
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct filldir_reg fdr;
	unsigned int entries, size;
	struct filldir_bad *fdb;
	struct gfs2_holder d_gh;
	uint64_t offset = file->f_pos;
	unsigned int x;
	struct filldir_bad_entry *fbe;
	int error;

	entries = gfs2_tune_get(sdp, gt_entries_per_readdir);
	size = sizeof(struct filldir_bad) +
	    entries * (sizeof(struct filldir_bad_entry) + GFS2_FAST_NAME_SIZE);

	fdb = kzalloc(size, GFP_KERNEL);
	if (!fdb)
		return -ENOMEM;

	fdb->fdb_sbd = sdp;
	fdb->fdb_entry = (struct filldir_bad_entry *)(fdb + 1);
	fdb->fdb_entry_num = entries;
	fdb->fdb_name = ((char *)fdb) + sizeof(struct filldir_bad) +
		entries * sizeof(struct filldir_bad_entry);
	fdb->fdb_name_size = entries * GFS2_FAST_NAME_SIZE;

	gfs2_holder_init(dip->i_gl, LM_ST_SHARED, GL_ATIME, &d_gh);
	error = gfs2_glock_nq_atime(&d_gh);
	if (error) {
		gfs2_holder_uninit(&d_gh);
		goto out;
	}

	error = gfs2_dir_read(dir, &offset, fdb, filldir_bad_func);

	gfs2_glock_dq_uninit(&d_gh);

	fdr.fdr_sbd = sdp;
	fdr.fdr_prefetch = 0;
	fdr.fdr_filldir = filldir;
	fdr.fdr_opaque = dirent;

	for (x = 0; x < fdb->fdb_entry_off; x++) {
		fbe = &fdb->fdb_entry[x];

		error = filldir_reg_func(&fdr,
					 fbe->fbe_name, fbe->fbe_length,
					 fbe->fbe_offset,
					 &fbe->fbe_inum, fbe->fbe_type);
		if (error) {
			file->f_pos = fbe->fbe_offset;
			error = 0;
			goto out;
		}
	}

	file->f_pos = offset;

 out:
	kfree(fdb);

	return error;
}

/**
 * gfs2_readdir - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * Returns: errno
 */

static int gfs2_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	int error;

	if (strcmp(current->comm, "nfsd") != 0)
		error = readdir_reg(file, dirent, filldir);
	else
		error = readdir_bad(file, dirent, filldir);

	return error;
}

const struct gfs2_flag_eattr {
	u32 flag;
	u32 ext2;
} gfs2_flag_eattrs[] = {
	{
		.flag = GFS2_DIF_IMMUTABLE,
		.ext2 = EXT2_IMMUTABLE_FL,
	}, {
		.flag = GFS2_DIF_APPENDONLY,
		.ext2 = EXT2_APPEND_FL,
	}, {
		.flag = GFS2_DIF_JDATA,
		.ext2 = EXT2_JOURNAL_DATA_FL,
	}, {
		.flag = GFS2_DIF_EXHASH,
		.ext2 = EXT2_INDEX_FL,
	}, {
		.flag = GFS2_DIF_EA_INDIRECT,
	}, {
		.flag = GFS2_DIF_DIRECTIO,
	}, {
		.flag = GFS2_DIF_NOATIME,
		.ext2 = EXT2_NOATIME_FL,
	}, {
		.flag = GFS2_DIF_SYNC,
		.ext2 = EXT2_SYNC_FL,
	}, {
		.flag = GFS2_DIF_SYSTEM,
	}, {
		.flag = GFS2_DIF_TRUNC_IN_PROG,
	}, {
		.flag = GFS2_DIF_INHERIT_JDATA,
	}, {
		.flag = GFS2_DIF_INHERIT_DIRECTIO,
	}, {
	},
};

static const struct gfs2_flag_eattr *get_by_ext2(u32 ext2)
{
	const struct gfs2_flag_eattr *p = gfs2_flag_eattrs;
	for(; p->flag; p++) {
		if (ext2 == p->ext2)
			return p;
	}
	return NULL;
}

static const struct gfs2_flag_eattr *get_by_gfs2(u32 gfs2)
{
	const struct gfs2_flag_eattr *p = gfs2_flag_eattrs;
	for(; p->flag; p++) {
		if (gfs2 == p->flag)
			return p;
	}
	return NULL;
}

static u32 gfs2_flags_to_ext2(u32 gfs2)
{
	const struct gfs2_flag_eattr *ea;
	u32 ext2 = 0;
	u32 mask = 1;

	for(; mask != 0; mask <<=1) {
		if (mask & gfs2) {
			ea = get_by_gfs2(mask);
			if (ea)
				ext2 |= ea->ext2;
		}
	}
	return ext2;
}

static int gfs2_flags_from_ext2(u32 *gfs2, u32 ext2)
{
	const struct gfs2_flag_eattr *ea;
	u32 mask = 1;

	for(; mask != 0; mask <<= 1) {
		if (mask & ext2) {
			ea = get_by_ext2(mask);
			if (ea == NULL)
				return -EINVAL;
			*gfs2 |= ea->flag;
		}
	}
	return 0;
}

static int get_ext2_flags(struct inode *inode, u32 __user *ptr)
{
	struct gfs2_inode *ip = inode->u.generic_ip;
	struct gfs2_holder gh;
	int error;
	u32 ext2;

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &gh);
	error = gfs2_glock_nq_m_atime(1, &gh);
	if (error)
		return error;

	ext2 = gfs2_flags_to_ext2(ip->i_di.di_flags);
	if (put_user(ext2, ptr))
		error = -EFAULT;

	gfs2_glock_dq_m(1, &gh);
	gfs2_holder_uninit(&gh);
	return error;
}

/* Flags that can be set by user space */
#define GFS2_FLAGS_USER_SET (GFS2_DIF_JDATA|			\
			     GFS2_DIF_DIRECTIO|			\
			     GFS2_DIF_IMMUTABLE|		\
			     GFS2_DIF_APPENDONLY|		\
			     GFS2_DIF_NOATIME|			\
			     GFS2_DIF_SYNC|			\
			     GFS2_DIF_SYSTEM|			\
			     GFS2_DIF_INHERIT_DIRECTIO|		\
			     GFS2_DIF_INHERIT_JDATA)

/**
 * gfs2_set_flags - set flags on an inode
 * @inode: The inode
 * @flags: The flags to set
 * @mask: Indicates which flags are valid
 *
 */
static int gfs2_set_flags(struct inode *inode, u32 flags, u32 mask)
{
	struct gfs2_inode *ip = inode->u.generic_ip;
	struct buffer_head *bh;
	struct gfs2_holder gh;
	int error;
	u32 new_flags;

	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	if (error)
		return error;

	new_flags = (ip->i_di.di_flags & ~mask) | (flags & mask);
	if ((new_flags ^ flags) == 0)
		goto out;

	error = -EINVAL;
	if ((new_flags ^ flags) & ~GFS2_FLAGS_USER_SET)
		goto out;

	if (S_ISDIR(inode->i_mode)) {
		if ((new_flags ^ flags) & (GFS2_DIF_JDATA | GFS2_DIF_DIRECTIO))
			goto out;
	} else if (S_ISREG(inode->i_mode)) {
		if ((new_flags ^ flags) & (GFS2_DIF_INHERIT_DIRECTIO|
					   GFS2_DIF_INHERIT_JDATA))
			goto out;
	} else
		goto out;

	error = -EPERM;
	if (IS_IMMUTABLE(inode) && (new_flags & GFS2_DIF_IMMUTABLE))
		goto out;
	if (IS_APPEND(inode) && (new_flags & GFS2_DIF_APPENDONLY))
		goto out;
	error = gfs2_repermission(inode, MAY_WRITE, NULL);
	if (error)
		goto out;

	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error)
		goto out;
	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	ip->i_di.di_flags = new_flags;
	gfs2_dinode_out(&ip->i_di, bh->b_data);
	brelse(bh);
out:
	gfs2_glock_dq_uninit(&gh);
	return error;
}

static int set_ext2_flags(struct inode *inode, u32 __user *ptr)
{
	u32 ext2, gfs2;
	if (get_user(ext2, ptr))
		return -EFAULT;
	if (gfs2_flags_from_ext2(&gfs2, ext2))
		return -EINVAL;
	return gfs2_set_flags(inode, gfs2, ~0);
}

int gfs2_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
	       unsigned long arg)
{
	switch(cmd) {
	case EXT2_IOC_GETFLAGS:
		return get_ext2_flags(inode, (u32 __user *)arg);
	case EXT2_IOC_SETFLAGS:
		return set_ext2_flags(inode, (u32 __user *)arg);
	}
	return -ENOTTY;
}


/**
 * gfs2_mmap -
 * @file: The file to map
 * @vma: The VMA which described the mapping
 *
 * Returns: 0 or error code
 */

static int gfs2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gfs2_inode *ip = file->f_mapping->host->u.generic_ip;
	struct gfs2_holder i_gh;
	int error;

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &i_gh);
	error = gfs2_glock_nq_atime(&i_gh);
	if (error) {
		gfs2_holder_uninit(&i_gh);
		return error;
	}

	/* This is VM_MAYWRITE instead of VM_WRITE because a call
	   to mprotect() can turn on VM_WRITE later. */

	if ((vma->vm_flags & (VM_MAYSHARE | VM_MAYWRITE)) ==
	    (VM_MAYSHARE | VM_MAYWRITE))
		vma->vm_ops = &gfs2_vm_ops_sharewrite;
	else
		vma->vm_ops = &gfs2_vm_ops_private;

	gfs2_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * gfs2_open - open a file
 * @inode: the inode to open
 * @file: the struct file for this opening
 *
 * Returns: errno
 */

static int gfs2_open(struct inode *inode, struct file *file)
{
	struct gfs2_inode *ip = inode->u.generic_ip;
	struct gfs2_holder i_gh;
	struct gfs2_file *fp;
	int error;

	fp = kzalloc(sizeof(struct gfs2_file), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	mutex_init(&fp->f_fl_mutex);

	fp->f_inode = ip;
	fp->f_vfile = file;

	gfs2_assert_warn(ip->i_sbd, !file->private_data);
	file->private_data = fp;

	if (S_ISREG(ip->i_di.di_mode)) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY,
					   &i_gh);
		if (error)
			goto fail;

		if (!(file->f_flags & O_LARGEFILE) &&
		    ip->i_di.di_size > MAX_NON_LFS) {
			error = -EFBIG;
			goto fail_gunlock;
		}

		/* Listen to the Direct I/O flag */

		if (ip->i_di.di_flags & GFS2_DIF_DIRECTIO)
			file->f_flags |= O_DIRECT;

		gfs2_glock_dq_uninit(&i_gh);
	}

	return 0;

 fail_gunlock:
	gfs2_glock_dq_uninit(&i_gh);

 fail:
	file->private_data = NULL;
	kfree(fp);

	return error;
}

/**
 * gfs2_close - called to close a struct file
 * @inode: the inode the struct file belongs to
 * @file: the struct file being closed
 *
 * Returns: errno
 */

static int gfs2_close(struct inode *inode, struct file *file)
{
	struct gfs2_sbd *sdp = inode->i_sb->s_fs_info;
	struct gfs2_file *fp;

	fp = file->private_data;
	file->private_data = NULL;

	if (gfs2_assert_warn(sdp, fp))
		return -EIO;

	kfree(fp);

	return 0;
}

/**
 * gfs2_fsync - sync the dirty data for a file (across the cluster)
 * @file: the file that points to the dentry (we ignore this)
 * @dentry: the dentry that points to the inode to sync
 *
 * Returns: errno
 */

static int gfs2_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct gfs2_inode *ip = dentry->d_inode->u.generic_ip;

	gfs2_log_flush_glock(ip->i_gl);

	return 0;
}

/**
 * gfs2_lock - acquire/release a posix lock on a file
 * @file: the file pointer
 * @cmd: either modify or retrieve lock state, possibly wait
 * @fl: type and range of lock
 *
 * Returns: errno
 */

static int gfs2_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs2_inode *ip = file->f_mapping->host->u.generic_ip;
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct lm_lockname name =
		{ .ln_number = ip->i_num.no_addr,
		  .ln_type = LM_TYPE_PLOCK };

	if (!(fl->fl_flags & FL_POSIX))
		return -ENOLCK;
	if ((ip->i_di.di_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if (sdp->sd_args.ar_localflocks) {
		if (IS_GETLK(cmd)) {
			struct file_lock *tmp;
			lock_kernel();
			tmp = posix_test_lock(file, fl);
			fl->fl_type = F_UNLCK;
			if (tmp)
				memcpy(fl, tmp, sizeof(struct file_lock));
			unlock_kernel();
			return 0;
		} else {
			int error;
			lock_kernel();
			error = posix_lock_file_wait(file, fl);
			unlock_kernel();
			return error;
		}
	}

	if (IS_GETLK(cmd))
		return gfs2_lm_plock_get(sdp, &name, file, fl);
	else if (fl->fl_type == F_UNLCK)
		return gfs2_lm_punlock(sdp, &name, file, fl);
	else
		return gfs2_lm_plock(sdp, &name, file, cmd, fl);
}

/**
 * gfs2_sendfile - Send bytes to a file or socket
 * @in_file: The file to read from
 * @out_file: The file to write to
 * @count: The amount of data
 * @offset: The beginning file offset
 *
 * Outputs: offset - updated according to number of bytes read
 *
 * Returns: The number of bytes sent, errno on failure
 */

static ssize_t gfs2_sendfile(struct file *in_file, loff_t *offset, size_t count,
			     read_actor_t actor, void *target)
{
	return generic_file_sendfile(in_file, offset, count, actor, target);
}

static int do_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs2_file *fp = file->private_data;
	struct gfs2_holder *fl_gh = &fp->f_fl_gh;
	struct gfs2_inode *ip = fp->f_inode;
	struct gfs2_glock *gl;
	unsigned int state;
	int flags;
	int error = 0;

	state = (fl->fl_type == F_WRLCK) ? LM_ST_EXCLUSIVE : LM_ST_SHARED;
	flags = ((IS_SETLKW(cmd)) ? 0 : LM_FLAG_TRY) | GL_EXACT | GL_NOCACHE;

	mutex_lock(&fp->f_fl_mutex);

	gl = fl_gh->gh_gl;
	if (gl) {
		if (fl_gh->gh_state == state)
			goto out;
		gfs2_glock_hold(gl);
		flock_lock_file_wait(file,
				     &(struct file_lock){.fl_type = F_UNLCK});		
		gfs2_glock_dq_uninit(fl_gh);
	} else {
		error = gfs2_glock_get(ip->i_sbd,
				      ip->i_num.no_addr, &gfs2_flock_glops,
				      CREATE, &gl);
		if (error)
			goto out;
	}

	gfs2_holder_init(gl, state, flags, fl_gh);
	gfs2_glock_put(gl);

	error = gfs2_glock_nq(fl_gh);
	if (error) {
		gfs2_holder_uninit(fl_gh);
		if (error == GLR_TRYFAILED)
			error = -EAGAIN;
	} else {
		error = flock_lock_file_wait(file, fl);
		gfs2_assert_warn(ip->i_sbd, !error);
	}

 out:
	mutex_unlock(&fp->f_fl_mutex);

	return error;
}

static void do_unflock(struct file *file, struct file_lock *fl)
{
	struct gfs2_file *fp = file->private_data;
	struct gfs2_holder *fl_gh = &fp->f_fl_gh;

	mutex_lock(&fp->f_fl_mutex);
	flock_lock_file_wait(file, fl);
	if (fl_gh->gh_gl)
		gfs2_glock_dq_uninit(fl_gh);
	mutex_unlock(&fp->f_fl_mutex);
}

/**
 * gfs2_flock - acquire/release a flock lock on a file
 * @file: the file pointer
 * @cmd: either modify or retrieve lock state, possibly wait
 * @fl: type and range of lock
 *
 * Returns: errno
 */

static int gfs2_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs2_inode *ip = file->f_mapping->host->u.generic_ip;
	struct gfs2_sbd *sdp = ip->i_sbd;

	if (!(fl->fl_flags & FL_FLOCK))
		return -ENOLCK;
	if ((ip->i_di.di_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if (sdp->sd_args.ar_localflocks)
		return flock_lock_file_wait(file, fl);

	if (fl->fl_type == F_UNLCK) {
		do_unflock(file, fl);
		return 0;
	} else
		return do_flock(file, cmd, fl);
}

struct file_operations gfs2_file_fops = {
	.llseek = gfs2_llseek,
	.read = gfs2_read,
	.readv = gfs2_file_readv,
	.aio_read = gfs2_file_aio_read,
	.write = generic_file_write,
	.writev = generic_file_writev,
	.aio_write = generic_file_aio_write,
	.ioctl = gfs2_ioctl,
	.mmap = gfs2_mmap,
	.open = gfs2_open,
	.release = gfs2_close,
	.fsync = gfs2_fsync,
	.lock = gfs2_lock,
	.sendfile = gfs2_sendfile,
	.flock = gfs2_flock,
};

struct file_operations gfs2_dir_fops = {
	.readdir = gfs2_readdir,
	.ioctl = gfs2_ioctl,
	.open = gfs2_open,
	.release = gfs2_close,
	.fsync = gfs2_fsync,
	.lock = gfs2_lock,
	.flock = gfs2_flock,
};

