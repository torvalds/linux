/*-
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ffs_vfsops.c	8.8 (Berkeley) 4/18/94
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/mutex.h>

#include <geom/geom.h>
#include <geom/geom_vfs.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/inode.h>

#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/ext2_extents.h>


static int	ext2_flushfiles(struct mount *mp, int flags, struct thread *td);
static int	ext2_mountfs(struct vnode *, struct mount *);
static int	ext2_reload(struct mount *mp, struct thread *td);
static int	ext2_sbupdate(struct ext2mount *, int);
static int	ext2_cgupdate(struct ext2mount *, int);
static vfs_unmount_t		ext2_unmount;
static vfs_root_t		ext2_root;
static vfs_statfs_t		ext2_statfs;
static vfs_sync_t		ext2_sync;
static vfs_vget_t		ext2_vget;
static vfs_fhtovp_t		ext2_fhtovp;
static vfs_mount_t		ext2_mount;

MALLOC_DEFINE(M_EXT2NODE, "ext2_node", "EXT2 vnode private part");
static MALLOC_DEFINE(M_EXT2MNT, "ext2_mount", "EXT2 mount structure");

static struct vfsops ext2fs_vfsops = {
	.vfs_fhtovp =		ext2_fhtovp,
	.vfs_mount =		ext2_mount,
	.vfs_root =		ext2_root,	/* root inode via vget */
	.vfs_statfs =		ext2_statfs,
	.vfs_sync =		ext2_sync,
	.vfs_unmount =		ext2_unmount,
	.vfs_vget =		ext2_vget,
};

VFS_SET(ext2fs_vfsops, ext2fs, 0);

static int	ext2_check_sb_compat(struct ext2fs *es, struct cdev *dev,
		    int ronly);
static int	ext2_compute_sb_data(struct vnode * devvp,
		    struct ext2fs * es, struct m_ext2fs * fs);

static const char *ext2_opts[] = { "acls", "async", "noatime", "noclusterr", 
    "noclusterw", "noexec", "export", "force", "from", "multilabel",
    "suiddir", "nosymfollow", "sync", "union", NULL };

/*
 * VFS Operations.
 *
 * mount system call
 */
static int
ext2_mount(struct mount *mp)
{
	struct vfsoptlist *opts;
	struct vnode *devvp;
	struct thread *td;
	struct ext2mount *ump = NULL;
	struct m_ext2fs *fs;
	struct nameidata nd, *ndp = &nd;
	accmode_t accmode;
	char *path, *fspec;
	int error, flags, len;

	td = curthread;
	opts = mp->mnt_optnew;

	if (vfs_filteropt(opts, ext2_opts))
		return (EINVAL);

	vfs_getopt(opts, "fspath", (void **)&path, NULL);
	/* Double-check the length of path.. */
	if (strlen(path) >= MAXMNTLEN)
		return (ENAMETOOLONG);

	fspec = NULL;
	error = vfs_getopt(opts, "from", (void **)&fspec, &len);
	if (!error && fspec[len - 1] != '\0')
		return (EINVAL);

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		ump = VFSTOEXT2(mp);
		fs = ump->um_e2fs;
		error = 0;
		if (fs->e2fs_ronly == 0 &&
		    vfs_flagopt(opts, "ro", NULL, 0)) {
			error = VFS_SYNC(mp, MNT_WAIT);
			if (error)
				return (error);
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			error = ext2_flushfiles(mp, flags, td);
			if (error == 0 && fs->e2fs_wasvalid &&
			    ext2_cgupdate(ump, MNT_WAIT) == 0) {
				fs->e2fs->e2fs_state |= E2FS_ISCLEAN;
				ext2_sbupdate(ump, MNT_WAIT);
			}
			fs->e2fs_ronly = 1;
			vfs_flagopt(opts, "ro", &mp->mnt_flag, MNT_RDONLY);
			g_topology_lock();
			g_access(ump->um_cp, 0, -1, 0);
			g_topology_unlock();
		}
		if (!error && (mp->mnt_flag & MNT_RELOAD))
			error = ext2_reload(mp, td);
		if (error)
			return (error);
		devvp = ump->um_devvp;
		if (fs->e2fs_ronly && !vfs_flagopt(opts, "ro", NULL, 0)) {
			if (ext2_check_sb_compat(fs->e2fs, devvp->v_rdev, 0))
				return (EPERM);

			/*
			 * If upgrade to read-write by non-root, then verify
			 * that user has necessary permissions on the device.
			 */
			vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
			error = VOP_ACCESS(devvp, VREAD | VWRITE,
			    td->td_ucred, td);
			if (error)
				error = priv_check(td, PRIV_VFS_MOUNT_PERM);
			if (error) {
				VOP_UNLOCK(devvp, 0);
				return (error);
			}
			VOP_UNLOCK(devvp, 0);
			g_topology_lock();
			error = g_access(ump->um_cp, 0, 1, 0);
			g_topology_unlock();
			if (error)
				return (error);

			if ((fs->e2fs->e2fs_state & E2FS_ISCLEAN) == 0 ||
			    (fs->e2fs->e2fs_state & E2FS_ERRORS)) {
				if (mp->mnt_flag & MNT_FORCE) {
					printf(
"WARNING: %s was not properly dismounted\n", fs->e2fs_fsmnt);
				} else {
					printf(
"WARNING: R/W mount of %s denied.  Filesystem is not clean - run fsck\n",
					    fs->e2fs_fsmnt);
					return (EPERM);
				}
			}
			fs->e2fs->e2fs_state &= ~E2FS_ISCLEAN;
			(void)ext2_cgupdate(ump, MNT_WAIT);
			fs->e2fs_ronly = 0;
			MNT_ILOCK(mp);
			mp->mnt_flag &= ~MNT_RDONLY;
			MNT_IUNLOCK(mp);
		}
		if (vfs_flagopt(opts, "export", NULL, 0)) {
			/* Process export requests in vfs_mount.c. */
			return (error);
		}
	}

	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible disk device.
	 */
	if (fspec == NULL)
		return (EINVAL);
	NDINIT(ndp, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, fspec, td);
	if ((error = namei(ndp)) != 0)
		return (error);
	NDFREE(ndp, NDF_ONLY_PNBUF);
	devvp = ndp->ni_vp;

	if (!vn_isdisk(devvp, &error)) {
		vput(devvp);
		return (error);
	}

	/*
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 *
	 * XXXRW: VOP_ACCESS() enough?
	 */
	accmode = VREAD;
	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		accmode |= VWRITE;
	error = VOP_ACCESS(devvp, accmode, td->td_ucred, td);
	if (error)
		error = priv_check(td, PRIV_VFS_MOUNT_PERM);
	if (error) {
		vput(devvp);
		return (error);
	}

	if ((mp->mnt_flag & MNT_UPDATE) == 0) {
		error = ext2_mountfs(devvp, mp);
	} else {
		if (devvp != ump->um_devvp) {
			vput(devvp);
			return (EINVAL);	/* needs translation */
		} else
			vput(devvp);
	}
	if (error) {
		vrele(devvp);
		return (error);
	}
	ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;

	/*
	 * Note that this strncpy() is ok because of a check at the start
	 * of ext2_mount().
	 */
	strncpy(fs->e2fs_fsmnt, path, MAXMNTLEN);
	fs->e2fs_fsmnt[MAXMNTLEN - 1] = '\0';
	vfs_mountedfrom(mp, fspec);
	return (0);
}

static int
ext2_check_sb_compat(struct ext2fs *es, struct cdev *dev, int ronly)
{
	uint32_t i, mask;

	if (es->e2fs_magic != E2FS_MAGIC) {
		printf("ext2fs: %s: wrong magic number %#x (expected %#x)\n",
		    devtoname(dev), es->e2fs_magic, E2FS_MAGIC);
		return (1);
	}
	if (es->e2fs_rev > E2FS_REV0) {
		mask = es->e2fs_features_incompat & ~(EXT2F_INCOMPAT_SUPP);
		if (mask) {
			printf("WARNING: mount of %s denied due to "
			    "unsupported optional features:\n", devtoname(dev));
			for (i = 0;
			    i < sizeof(incompat)/sizeof(struct ext2_feature);
			    i++)
				if (mask & incompat[i].mask)
					printf("%s ", incompat[i].name);
			printf("\n");
			return (1);
		}
		mask = es->e2fs_features_rocompat & ~EXT2F_ROCOMPAT_SUPP;
		if (!ronly && mask) {
			printf("WARNING: R/W mount of %s denied due to "
			    "unsupported optional features:\n", devtoname(dev));
			for (i = 0;
			    i < sizeof(ro_compat)/sizeof(struct ext2_feature);
			    i++)
				if (mask & ro_compat[i].mask)
					printf("%s ", ro_compat[i].name);
			printf("\n");
			return (1);
		}
	}
	return (0);
}

static e4fs_daddr_t
ext2_cg_location(struct m_ext2fs *fs, int number)
{
	int cg, descpb, logical_sb, has_super = 0;

	/*
	 * Adjust logical superblock block number.
	 * Godmar thinks: if the blocksize is greater than 1024, then
	 * the superblock is logically part of block zero.
	 */
	logical_sb = fs->e2fs_bsize > SBSIZE ? 0 : 1;

	if (!EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_META_BG) ||
	    number < fs->e2fs->e3fs_first_meta_bg)
		return (logical_sb + number + 1);

	if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_64BIT))
		descpb = fs->e2fs_bsize / sizeof(struct ext2_gd);
	else
		descpb = fs->e2fs_bsize / E2FS_REV0_GD_SIZE;

	cg = descpb * number;

	if (ext2_cg_has_sb(fs, cg))
		has_super = 1;

	return (has_super + cg * (e4fs_daddr_t)EXT2_BLOCKS_PER_GROUP(fs) +
	    fs->e2fs->e2fs_first_dblock);
}

static int
ext2_cg_validate(struct m_ext2fs *fs)
{
	uint64_t b_bitmap;
	uint64_t i_bitmap;
	uint64_t i_tables;
	uint64_t first_block, last_block, last_cg_block;
	struct ext2_gd *gd;
	unsigned int i, cg_count;

	first_block = fs->e2fs->e2fs_first_dblock;
	last_cg_block = ext2_cg_number_gdb(fs, 0);
	cg_count = fs->e2fs_gcount;

	for (i = 0; i < fs->e2fs_gcount; i++) {
		gd = &fs->e2fs_gd[i];

		if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_FLEX_BG) ||
		    i == fs->e2fs_gcount - 1) {
			last_block = fs->e2fs_bcount - 1;
		} else {
			last_block = first_block +
			    (EXT2_BLOCKS_PER_GROUP(fs) - 1);
		}

		if ((cg_count == fs->e2fs_gcount) &&
		    !(gd->ext4bgd_flags & EXT2_BG_INODE_ZEROED))
			cg_count = i;

		b_bitmap = e2fs_gd_get_b_bitmap(gd);
		if (b_bitmap == 0) {
			printf("ext2fs: cg %u: block bitmap is zero\n", i);
			return (EINVAL);

		}
		if (b_bitmap <= last_cg_block) {
			printf("ext2fs: cg %u: block bitmap overlaps gds\n", i);
			return (EINVAL);
		}
		if (b_bitmap < first_block || b_bitmap > last_block) {
			printf("ext2fs: cg %u: block bitmap not in group, blk=%ju\n",
			    i, b_bitmap);
			return (EINVAL);
		}

		i_bitmap = e2fs_gd_get_i_bitmap(gd);
		if (i_bitmap == 0) {
			printf("ext2fs: cg %u: inode bitmap is zero\n", i);
			return (EINVAL);
		}
		if (i_bitmap <= last_cg_block) {
			printf("ext2fs: cg %u: inode bitmap overlaps gds\n", i);
			return (EINVAL);
		}
		if (i_bitmap < first_block || i_bitmap > last_block) {
			printf("ext2fs: cg %u: inode bitmap not in group blk=%ju\n",
			    i, i_bitmap);
			return (EINVAL);
		}

		i_tables = e2fs_gd_get_i_tables(gd);
		if (i_tables == 0) {
			printf("ext2fs: cg %u: inode table is zero\n", i);
			return (EINVAL);
		}
		if (i_tables <= last_cg_block) {
			printf("ext2fs: cg %u: inode talbes overlaps gds\n", i);
			return (EINVAL);
		}
		if (i_tables < first_block ||
		    i_tables + fs->e2fs_itpg - 1 > last_block) {
			printf("ext2fs: cg %u: inode tables not in group blk=%ju\n",
			    i, i_tables);
			return (EINVAL);
		}

		if (!EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_FLEX_BG))
			first_block += EXT2_BLOCKS_PER_GROUP(fs);
	}

	return (0);
}

/*
 * This computes the fields of the m_ext2fs structure from the
 * data in the ext2fs structure read in.
 */
static int
ext2_compute_sb_data(struct vnode *devvp, struct ext2fs *es,
    struct m_ext2fs *fs)
{
	struct buf *bp;
	uint32_t e2fs_descpb, e2fs_gdbcount_alloc;
	int i, j;
	int g_count = 0;
	int error;

	/* Check checksum features */
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_GDT_CSUM) &&
	    EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		printf("ext2fs: incorrect checksum features combination\n");
		return (EINVAL);
	}

	/* Precompute checksum seed for all metadata */
	ext2_sb_csum_set_seed(fs);

	/* Verify sb csum if possible */
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		error = ext2_sb_csum_verify(fs);
		if (error) {
			return (error);
		}
	}

	/* Check for block size = 1K|2K|4K */
	if (es->e2fs_log_bsize > 2) {
		printf("ext2fs: bad block size: %d\n", es->e2fs_log_bsize);
		return (EINVAL);
	}

	fs->e2fs_bshift = EXT2_MIN_BLOCK_LOG_SIZE + es->e2fs_log_bsize;
	fs->e2fs_bsize = 1U << fs->e2fs_bshift;
	fs->e2fs_fsbtodb = es->e2fs_log_bsize + 1;
	fs->e2fs_qbmask = fs->e2fs_bsize - 1;

	/* Check for fragment size */
	if (es->e2fs_log_fsize >
	    (EXT2_MAX_FRAG_LOG_SIZE - EXT2_MIN_BLOCK_LOG_SIZE)) {
		printf("ext2fs: invalid log cluster size: %u\n",
		    es->e2fs_log_fsize);
		return (EINVAL);
	}

	fs->e2fs_fsize = EXT2_MIN_FRAG_SIZE << es->e2fs_log_fsize;
	if (fs->e2fs_fsize != fs->e2fs_bsize) {
		printf("ext2fs: fragment size (%u) != block size %u\n",
		    fs->e2fs_fsize, fs->e2fs_bsize);
		return (EINVAL);
	}

	fs->e2fs_fpb = fs->e2fs_bsize / fs->e2fs_fsize;

	/* Check reserved gdt blocks for future filesystem expansion */
	if (es->e2fs_reserved_ngdb > (fs->e2fs_bsize / 4)) {
		printf("ext2fs: number of reserved GDT blocks too large: %u\n",
		    es->e2fs_reserved_ngdb);
		return (EINVAL);
	}

	if (es->e2fs_rev == E2FS_REV0) {
		fs->e2fs_isize = E2FS_REV0_INODE_SIZE;
	} else {
		fs->e2fs_isize = es->e2fs_inode_size;

		/*
		 * Check first ino.
		 */
		if (es->e2fs_first_ino < EXT2_FIRSTINO) {
			printf("ext2fs: invalid first ino: %u\n",
			    es->e2fs_first_ino);
			return (EINVAL);
		}

		/*
		 * Simple sanity check for superblock inode size value.
		 */
		if (EXT2_INODE_SIZE(fs) < E2FS_REV0_INODE_SIZE ||
		    EXT2_INODE_SIZE(fs) > fs->e2fs_bsize ||
		    (fs->e2fs_isize & (fs->e2fs_isize - 1)) != 0) {
			printf("ext2fs: invalid inode size %u\n",
			    fs->e2fs_isize);
			return (EINVAL);
		}
	}

	/* Check group descriptors */
	if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_64BIT) &&
	    es->e3fs_desc_size != E2FS_64BIT_GD_SIZE) {
			printf("ext2fs: unsupported 64bit descriptor size %u\n",
			    es->e3fs_desc_size);
			return (EINVAL);
	}

	fs->e2fs_bpg = es->e2fs_bpg;
	fs->e2fs_fpg = es->e2fs_fpg;
	if (fs->e2fs_bpg == 0 || fs->e2fs_fpg == 0) {
		printf("ext2fs: zero blocks/fragments per group\n");
		return (EINVAL);
	}
	if (fs->e2fs_bpg != fs->e2fs_bsize * 8) {
		printf("ext2fs: non-standard group size unsupported %d\n",
		    fs->e2fs_bpg);
		return (EINVAL);
	}

	fs->e2fs_ipb = fs->e2fs_bsize / EXT2_INODE_SIZE(fs);
	if (fs->e2fs_ipb == 0 ||
	    fs->e2fs_ipb > fs->e2fs_bsize / E2FS_REV0_INODE_SIZE) {
		printf("ext2fs: bad inodes per block size\n");
		return (EINVAL);
	}

	fs->e2fs_ipg = es->e2fs_ipg;
	if (fs->e2fs_ipg < fs->e2fs_ipb || fs->e2fs_ipg >  fs->e2fs_bsize * 8) {
		printf("ext2fs: invalid inodes per group: %u\n", fs->e2fs_ipb);
		return (EINVAL);
	}

	fs->e2fs_itpg = fs->e2fs_ipg / fs->e2fs_ipb;

	fs->e2fs_bcount = es->e2fs_bcount;
	fs->e2fs_rbcount = es->e2fs_rbcount;
	fs->e2fs_fbcount = es->e2fs_fbcount;
	if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_64BIT)) {
		fs->e2fs_bcount |= (uint64_t)(es->e4fs_bcount_hi) << 32;
		fs->e2fs_rbcount |= (uint64_t)(es->e4fs_rbcount_hi) << 32;
		fs->e2fs_fbcount |= (uint64_t)(es->e4fs_fbcount_hi) << 32;
	}
	if (fs->e2fs_rbcount > fs->e2fs_bcount ||
	    fs->e2fs_fbcount > fs->e2fs_bcount) {
		printf("ext2fs: invalid block count\n");
		return (EINVAL);
	}
	if (es->e2fs_first_dblock >= fs->e2fs_bcount) {
		printf("ext2fs: first data block out of range\n");
		return (EINVAL);
	}

	fs->e2fs_gcount = howmany(fs->e2fs_bcount - es->e2fs_first_dblock,
	    EXT2_BLOCKS_PER_GROUP(fs));
	if (fs->e2fs_gcount > ((uint64_t)1 << 32) - EXT2_DESCS_PER_BLOCK(fs)) {
		printf("ext2fs: groups count too large: %u\n", fs->e2fs_gcount);
		return (EINVAL);
	}

	/* Check for extra isize in big inodes. */
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_EXTRA_ISIZE) &&
	    EXT2_INODE_SIZE(fs) < sizeof(struct ext2fs_dinode)) {
		printf("ext2fs: no space for extra inode timestamps\n");
		return (EINVAL);
	}

	/* s_resuid / s_resgid ? */

	if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_64BIT)) {
		e2fs_descpb = fs->e2fs_bsize / E2FS_64BIT_GD_SIZE;
		e2fs_gdbcount_alloc = howmany(fs->e2fs_gcount, e2fs_descpb);
	} else {
		e2fs_descpb = fs->e2fs_bsize / E2FS_REV0_GD_SIZE;
		e2fs_gdbcount_alloc = howmany(fs->e2fs_gcount,
		    fs->e2fs_bsize / sizeof(struct ext2_gd));
	}
	fs->e2fs_gdbcount = howmany(fs->e2fs_gcount, e2fs_descpb);
	fs->e2fs_gd = malloc(e2fs_gdbcount_alloc * fs->e2fs_bsize,
	    M_EXT2MNT, M_WAITOK | M_ZERO);
	fs->e2fs_contigdirs = malloc(fs->e2fs_gcount *
	    sizeof(*fs->e2fs_contigdirs), M_EXT2MNT, M_WAITOK | M_ZERO);

	for (i = 0; i < fs->e2fs_gdbcount; i++) {
		error = bread(devvp,
		    fsbtodb(fs, ext2_cg_location(fs, i)),
		    fs->e2fs_bsize, NOCRED, &bp);
		if (error) {
			/*
			 * fs->e2fs_gd and fs->e2fs_contigdirs
			 * will be freed later by the caller,
			 * because this function could be called from
			 * MNT_UPDATE path.
			 */
			brelse(bp);
			return (error);
		}
		if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_64BIT)) {
			memcpy(&fs->e2fs_gd[
			    i * fs->e2fs_bsize / sizeof(struct ext2_gd)],
			    bp->b_data, fs->e2fs_bsize);
		} else {
			for (j = 0; j < e2fs_descpb &&
			    g_count < fs->e2fs_gcount; j++, g_count++)
				memcpy(&fs->e2fs_gd[g_count],
				    bp->b_data + j * E2FS_REV0_GD_SIZE,
				    E2FS_REV0_GD_SIZE);
		}
		brelse(bp);
		bp = NULL;
	}

	/* Validate cgs consistency */
	error = ext2_cg_validate(fs);
	if (error)
		return (error);

	/* Verfy cgs csum */
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_GDT_CSUM) ||
	    EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		error = ext2_gd_csum_verify(fs, devvp->v_rdev);
		if (error)
			return (error);
	}
	/* Initialization for the ext2 Orlov allocator variant. */
	fs->e2fs_total_dir = 0;
	for (i = 0; i < fs->e2fs_gcount; i++)
		fs->e2fs_total_dir += e2fs_gd_get_ndirs(&fs->e2fs_gd[i]);

	if (es->e2fs_rev == E2FS_REV0 ||
	    !EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_LARGEFILE))
		fs->e2fs_maxfilesize = 0x7fffffff;
	else {
		fs->e2fs_maxfilesize = 0xffffffffffff;
		if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_HUGE_FILE))
			fs->e2fs_maxfilesize = 0x7fffffffffffffff;
	}
	if (es->e4fs_flags & E2FS_UNSIGNED_HASH) {
		fs->e2fs_uhash = 3;
	} else if ((es->e4fs_flags & E2FS_SIGNED_HASH) == 0) {
#ifdef __CHAR_UNSIGNED__
		es->e4fs_flags |= E2FS_UNSIGNED_HASH;
		fs->e2fs_uhash = 3;
#else
		es->e4fs_flags |= E2FS_SIGNED_HASH;
#endif
	}
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		error = ext2_sb_csum_verify(fs);

	return (error);
}

/*
 * Reload all incore data for a filesystem (used after running fsck on
 * the root filesystem and finding things to fix). The filesystem must
 * be mounted read-only.
 *
 * Things to do to update the mount:
 *	1) invalidate all cached meta-data.
 *	2) re-read superblock from disk.
 *	3) invalidate all cluster summary information.
 *	4) invalidate all inactive vnodes.
 *	5) invalidate all cached file data.
 *	6) re-read inode data for all active vnodes.
 * XXX we are missing some steps, in particular # 3, this has to be reviewed.
 */
static int
ext2_reload(struct mount *mp, struct thread *td)
{
	struct vnode *vp, *mvp, *devvp;
	struct inode *ip;
	struct buf *bp;
	struct ext2fs *es;
	struct m_ext2fs *fs;
	struct csum *sump;
	int error, i;
	int32_t *lp;

	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		return (EINVAL);
	/*
	 * Step 1: invalidate all cached meta-data.
	 */
	devvp = VFSTOEXT2(mp)->um_devvp;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	if (vinvalbuf(devvp, 0, 0, 0) != 0)
		panic("ext2_reload: dirty1");
	VOP_UNLOCK(devvp, 0);

	/*
	 * Step 2: re-read superblock from disk.
	 * constants have been adjusted for ext2
	 */
	if ((error = bread(devvp, SBLOCK, SBSIZE, NOCRED, &bp)) != 0)
		return (error);
	es = (struct ext2fs *)bp->b_data;
	if (ext2_check_sb_compat(es, devvp->v_rdev, 0) != 0) {
		brelse(bp);
		return (EIO);		/* XXX needs translation */
	}
	fs = VFSTOEXT2(mp)->um_e2fs;
	bcopy(bp->b_data, fs->e2fs, sizeof(struct ext2fs));

	if ((error = ext2_compute_sb_data(devvp, es, fs)) != 0) {
		brelse(bp);
		return (error);
	}
#ifdef UNKLAR
	if (fs->fs_sbsize < SBSIZE)
		bp->b_flags |= B_INVAL;
#endif
	brelse(bp);

	/*
	 * Step 3: invalidate all cluster summary information.
	 */
	if (fs->e2fs_contigsumsize > 0) {
		lp = fs->e2fs_maxcluster;
		sump = fs->e2fs_clustersum;
		for (i = 0; i < fs->e2fs_gcount; i++, sump++) {
			*lp++ = fs->e2fs_contigsumsize;
			sump->cs_init = 0;
			bzero(sump->cs_sum, fs->e2fs_contigsumsize + 1);
		}
	}

loop:
	MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
		/*
		 * Step 4: invalidate all cached file data.
		 */
		if (vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, td)) {
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			goto loop;
		}
		if (vinvalbuf(vp, 0, 0, 0))
			panic("ext2_reload: dirty2");

		/*
		 * Step 5: re-read inode data for all active vnodes.
		 */
		ip = VTOI(vp);
		error = bread(devvp, fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
		    (int)fs->e2fs_bsize, NOCRED, &bp);
		if (error) {
			VOP_UNLOCK(vp, 0);
			vrele(vp);
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			return (error);
		}

		error = ext2_ei2i((struct ext2fs_dinode *)((char *)bp->b_data +
		    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number)), ip);

		brelse(bp);
		VOP_UNLOCK(vp, 0);
		vrele(vp);

		if (error) {
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			return (error);
		}
	}
	return (0);
}

/*
 * Common code for mount and mountroot.
 */
static int
ext2_mountfs(struct vnode *devvp, struct mount *mp)
{
	struct ext2mount *ump;
	struct buf *bp;
	struct m_ext2fs *fs;
	struct ext2fs *es;
	struct cdev *dev = devvp->v_rdev;
	struct g_consumer *cp;
	struct bufobj *bo;
	struct csum *sump;
	int error;
	int ronly;
	int i;
	u_long size;
	int32_t *lp;
	int32_t e2fs_maxcontig;

	ronly = vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0);
	/* XXX: use VOP_ACESS to check FS perms */
	g_topology_lock();
	error = g_vfs_open(devvp, &cp, "ext2fs", ronly ? 0 : 1);
	g_topology_unlock();
	VOP_UNLOCK(devvp, 0);
	if (error)
		return (error);

	/* XXX: should we check for some sectorsize or 512 instead? */
	if (((SBSIZE % cp->provider->sectorsize) != 0) ||
	    (SBSIZE < cp->provider->sectorsize)) {
		g_topology_lock();
		g_vfs_close(cp);
		g_topology_unlock();
		return (EINVAL);
	}

	bo = &devvp->v_bufobj;
	bo->bo_private = cp;
	bo->bo_ops = g_vfs_bufops;
	if (devvp->v_rdev->si_iosize_max != 0)
		mp->mnt_iosize_max = devvp->v_rdev->si_iosize_max;
	if (mp->mnt_iosize_max > MAXPHYS)
		mp->mnt_iosize_max = MAXPHYS;

	bp = NULL;
	ump = NULL;
	if ((error = bread(devvp, SBLOCK, SBSIZE, NOCRED, &bp)) != 0)
		goto out;
	es = (struct ext2fs *)bp->b_data;
	if (ext2_check_sb_compat(es, dev, ronly) != 0) {
		error = EINVAL;		/* XXX needs translation */
		goto out;
	}
	if ((es->e2fs_state & E2FS_ISCLEAN) == 0 ||
	    (es->e2fs_state & E2FS_ERRORS)) {
		if (ronly || (mp->mnt_flag & MNT_FORCE)) {
			printf(
"WARNING: Filesystem was not properly dismounted\n");
		} else {
			printf(
"WARNING: R/W mount denied.  Filesystem is not clean - run fsck\n");
			error = EPERM;
			goto out;
		}
	}
	ump = malloc(sizeof(*ump), M_EXT2MNT, M_WAITOK | M_ZERO);

	/*
	 * I don't know whether this is the right strategy. Note that
	 * we dynamically allocate both an m_ext2fs and an ext2fs
	 * while Linux keeps the super block in a locked buffer.
	 */
	ump->um_e2fs = malloc(sizeof(struct m_ext2fs),
	    M_EXT2MNT, M_WAITOK | M_ZERO);
	ump->um_e2fs->e2fs = malloc(sizeof(struct ext2fs),
	    M_EXT2MNT, M_WAITOK);
	mtx_init(EXT2_MTX(ump), "EXT2FS", "EXT2FS Lock", MTX_DEF);
	bcopy(es, ump->um_e2fs->e2fs, (u_int)sizeof(struct ext2fs));
	if ((error = ext2_compute_sb_data(devvp, ump->um_e2fs->e2fs, ump->um_e2fs)))
		goto out;

	/*
	 * Calculate the maximum contiguous blocks and size of cluster summary
	 * array.  In FFS this is done by newfs; however, the superblock
	 * in ext2fs doesn't have these variables, so we can calculate
	 * them here.
	 */
	e2fs_maxcontig = MAX(1, MAXPHYS / ump->um_e2fs->e2fs_bsize);
	ump->um_e2fs->e2fs_contigsumsize = MIN(e2fs_maxcontig, EXT2_MAXCONTIG);
	if (ump->um_e2fs->e2fs_contigsumsize > 0) {
		size = ump->um_e2fs->e2fs_gcount * sizeof(int32_t);
		ump->um_e2fs->e2fs_maxcluster = malloc(size, M_EXT2MNT, M_WAITOK);
		size = ump->um_e2fs->e2fs_gcount * sizeof(struct csum);
		ump->um_e2fs->e2fs_clustersum = malloc(size, M_EXT2MNT, M_WAITOK);
		lp = ump->um_e2fs->e2fs_maxcluster;
		sump = ump->um_e2fs->e2fs_clustersum;
		for (i = 0; i < ump->um_e2fs->e2fs_gcount; i++, sump++) {
			*lp++ = ump->um_e2fs->e2fs_contigsumsize;
			sump->cs_init = 0;
			sump->cs_sum = malloc((ump->um_e2fs->e2fs_contigsumsize + 1) *
			    sizeof(int32_t), M_EXT2MNT, M_WAITOK | M_ZERO);
		}
	}

	brelse(bp);
	bp = NULL;
	fs = ump->um_e2fs;
	fs->e2fs_ronly = ronly;	/* ronly is set according to mnt_flags */

	/*
	 * If the fs is not mounted read-only, make sure the super block is
	 * always written back on a sync().
	 */
	fs->e2fs_wasvalid = fs->e2fs->e2fs_state & E2FS_ISCLEAN ? 1 : 0;
	if (ronly == 0) {
		fs->e2fs_fmod = 1;	/* mark it modified */
		fs->e2fs->e2fs_state &= ~E2FS_ISCLEAN;	/* set fs invalid */
	}
	mp->mnt_data = ump;
	mp->mnt_stat.f_fsid.val[0] = dev2udev(dev);
	mp->mnt_stat.f_fsid.val[1] = mp->mnt_vfc->vfc_typenum;
	mp->mnt_maxsymlinklen = EXT2_MAXSYMLINKLEN;
	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	MNT_IUNLOCK(mp);
	ump->um_mountp = mp;
	ump->um_dev = dev;
	ump->um_devvp = devvp;
	ump->um_bo = &devvp->v_bufobj;
	ump->um_cp = cp;

	/*
	 * Setting those two parameters allowed us to use
	 * ufs_bmap w/o changse!
	 */
	ump->um_nindir = EXT2_ADDR_PER_BLOCK(fs);
	ump->um_bptrtodb = fs->e2fs->e2fs_log_bsize + 1;
	ump->um_seqinc = EXT2_FRAGS_PER_BLOCK(fs);
	if (ronly == 0)
		ext2_sbupdate(ump, MNT_WAIT);
	/*
	 * Initialize filesystem stat information in mount struct.
	 */
	MNT_ILOCK(mp);
	mp->mnt_kern_flag |= MNTK_LOOKUP_SHARED | MNTK_EXTENDED_SHARED |
	    MNTK_USES_BCACHE;
	MNT_IUNLOCK(mp);
	return (0);
out:
	if (bp)
		brelse(bp);
	if (cp != NULL) {
		g_topology_lock();
		g_vfs_close(cp);
		g_topology_unlock();
	}
	if (ump) {
		mtx_destroy(EXT2_MTX(ump));
		free(ump->um_e2fs->e2fs_gd, M_EXT2MNT);
		free(ump->um_e2fs->e2fs_contigdirs, M_EXT2MNT);
		free(ump->um_e2fs->e2fs, M_EXT2MNT);
		free(ump->um_e2fs, M_EXT2MNT);
		free(ump, M_EXT2MNT);
		mp->mnt_data = NULL;
	}
	return (error);
}

/*
 * Unmount system call.
 */
static int
ext2_unmount(struct mount *mp, int mntflags)
{
	struct ext2mount *ump;
	struct m_ext2fs *fs;
	struct csum *sump;
	int error, flags, i, ronly;

	flags = 0;
	if (mntflags & MNT_FORCE) {
		if (mp->mnt_flag & MNT_ROOTFS)
			return (EINVAL);
		flags |= FORCECLOSE;
	}
	if ((error = ext2_flushfiles(mp, flags, curthread)) != 0)
		return (error);
	ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;
	ronly = fs->e2fs_ronly;
	if (ronly == 0 && ext2_cgupdate(ump, MNT_WAIT) == 0) {
		if (fs->e2fs_wasvalid)
			fs->e2fs->e2fs_state |= E2FS_ISCLEAN;
		ext2_sbupdate(ump, MNT_WAIT);
	}

	g_topology_lock();
	g_vfs_close(ump->um_cp);
	g_topology_unlock();
	vrele(ump->um_devvp);
	sump = fs->e2fs_clustersum;
	for (i = 0; i < fs->e2fs_gcount; i++, sump++)
		free(sump->cs_sum, M_EXT2MNT);
	free(fs->e2fs_clustersum, M_EXT2MNT);
	free(fs->e2fs_maxcluster, M_EXT2MNT);
	free(fs->e2fs_gd, M_EXT2MNT);
	free(fs->e2fs_contigdirs, M_EXT2MNT);
	free(fs->e2fs, M_EXT2MNT);
	free(fs, M_EXT2MNT);
	free(ump, M_EXT2MNT);
	mp->mnt_data = NULL;
	MNT_ILOCK(mp);
	mp->mnt_flag &= ~MNT_LOCAL;
	MNT_IUNLOCK(mp);
	return (error);
}

/*
 * Flush out all the files in a filesystem.
 */
static int
ext2_flushfiles(struct mount *mp, int flags, struct thread *td)
{
	int error;

	error = vflush(mp, 0, flags, td);
	return (error);
}

/*
 * Get filesystem statistics.
 */
int
ext2_statfs(struct mount *mp, struct statfs *sbp)
{
	struct ext2mount *ump;
	struct m_ext2fs *fs;
	uint32_t overhead, overhead_per_group, ngdb;
	int i, ngroups;

	ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;
	if (fs->e2fs->e2fs_magic != E2FS_MAGIC)
		panic("ext2_statfs");

	/*
	 * Compute the overhead (FS structures)
	 */
	overhead_per_group =
	    1 /* block bitmap */ +
	    1 /* inode bitmap */ +
	    fs->e2fs_itpg;
	overhead = fs->e2fs->e2fs_first_dblock +
	    fs->e2fs_gcount * overhead_per_group;
	if (fs->e2fs->e2fs_rev > E2FS_REV0 &&
	    fs->e2fs->e2fs_features_rocompat & EXT2F_ROCOMPAT_SPARSESUPER) {
		for (i = 0, ngroups = 0; i < fs->e2fs_gcount; i++) {
			if (ext2_cg_has_sb(fs, i))
				ngroups++;
		}
	} else {
		ngroups = fs->e2fs_gcount;
	}
	ngdb = fs->e2fs_gdbcount;
	if (fs->e2fs->e2fs_rev > E2FS_REV0 &&
	    fs->e2fs->e2fs_features_compat & EXT2F_COMPAT_RESIZE)
		ngdb += fs->e2fs->e2fs_reserved_ngdb;
	overhead += ngroups * (1 /* superblock */ + ngdb);

	sbp->f_bsize = EXT2_FRAG_SIZE(fs);
	sbp->f_iosize = EXT2_BLOCK_SIZE(fs);
	sbp->f_blocks = fs->e2fs_bcount - overhead;
	sbp->f_bfree = fs->e2fs_fbcount;
	sbp->f_bavail = sbp->f_bfree - fs->e2fs_rbcount;
	sbp->f_files = fs->e2fs->e2fs_icount;
	sbp->f_ffree = fs->e2fs->e2fs_ficount;
	return (0);
}

/*
 * Go through the disk queues to initiate sandbagged IO;
 * go through the inodes to write those that have been modified;
 * initiate the writing of the super block if it has been modified.
 *
 * Note: we are always called with the filesystem marked `MPBUSY'.
 */
static int
ext2_sync(struct mount *mp, int waitfor)
{
	struct vnode *mvp, *vp;
	struct thread *td;
	struct inode *ip;
	struct ext2mount *ump = VFSTOEXT2(mp);
	struct m_ext2fs *fs;
	int error, allerror = 0;

	td = curthread;
	fs = ump->um_e2fs;
	if (fs->e2fs_fmod != 0 && fs->e2fs_ronly != 0) {		/* XXX */
		printf("fs = %s\n", fs->e2fs_fsmnt);
		panic("ext2_sync: rofs mod");
	}

	/*
	 * Write back each (modified) inode.
	 */
loop:
	MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
		if (vp->v_type == VNON) {
			VI_UNLOCK(vp);
			continue;
		}
		ip = VTOI(vp);
		if ((ip->i_flag &
		    (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) == 0 &&
		    (vp->v_bufobj.bo_dirty.bv_cnt == 0 ||
		    waitfor == MNT_LAZY)) {
			VI_UNLOCK(vp);
			continue;
		}
		error = vget(vp, LK_EXCLUSIVE | LK_NOWAIT | LK_INTERLOCK, td);
		if (error) {
			if (error == ENOENT) {
				MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
				goto loop;
			}
			continue;
		}
		if ((error = VOP_FSYNC(vp, waitfor, td)) != 0)
			allerror = error;
		VOP_UNLOCK(vp, 0);
		vrele(vp);
	}

	/*
	 * Force stale filesystem control information to be flushed.
	 */
	if (waitfor != MNT_LAZY) {
		vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
		if ((error = VOP_FSYNC(ump->um_devvp, waitfor, td)) != 0)
			allerror = error;
		VOP_UNLOCK(ump->um_devvp, 0);
	}

	/*
	 * Write back modified superblock.
	 */
	if (fs->e2fs_fmod != 0) {
		fs->e2fs_fmod = 0;
		fs->e2fs->e2fs_wtime = time_second;
		if ((error = ext2_cgupdate(ump, waitfor)) != 0)
			allerror = error;
	}
	return (allerror);
}

/*
 * Look up an EXT2FS dinode number to find its incore vnode, otherwise read it
 * in from disk.  If it is in core, wait for the lock bit to clear, then
 * return the inode locked.  Detection and handling of mount points must be
 * done by the calling routine.
 */
static int
ext2_vget(struct mount *mp, ino_t ino, int flags, struct vnode **vpp)
{
	struct m_ext2fs *fs;
	struct inode *ip;
	struct ext2mount *ump;
	struct buf *bp;
	struct vnode *vp;
	struct thread *td;
	unsigned int i, used_blocks;
	int error;

	td = curthread;
	error = vfs_hash_get(mp, ino, flags, td, vpp, NULL, NULL);
	if (error || *vpp != NULL)
		return (error);

	ump = VFSTOEXT2(mp);
	ip = malloc(sizeof(struct inode), M_EXT2NODE, M_WAITOK | M_ZERO);

	/* Allocate a new vnode/inode. */
	if ((error = getnewvnode("ext2fs", mp, &ext2_vnodeops, &vp)) != 0) {
		*vpp = NULL;
		free(ip, M_EXT2NODE);
		return (error);
	}
	vp->v_data = ip;
	ip->i_vnode = vp;
	ip->i_e2fs = fs = ump->um_e2fs;
	ip->i_ump = ump;
	ip->i_number = ino;

	lockmgr(vp->v_vnlock, LK_EXCLUSIVE, NULL);
	error = insmntque(vp, mp);
	if (error != 0) {
		free(ip, M_EXT2NODE);
		*vpp = NULL;
		return (error);
	}
	error = vfs_hash_insert(vp, ino, flags, td, vpp, NULL, NULL);
	if (error || *vpp != NULL)
		return (error);

	/* Read in the disk contents for the inode, copy into the inode. */
	if ((error = bread(ump->um_devvp, fsbtodb(fs, ino_to_fsba(fs, ino)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		/*
		 * The inode does not contain anything useful, so it would
		 * be misleading to leave it on its hash chain. With mode
		 * still zero, it will be unlinked and returned to the free
		 * list by vput().
		 */
		brelse(bp);
		vput(vp);
		*vpp = NULL;
		return (error);
	}
	/* convert ext2 inode to dinode */
	error = ext2_ei2i((struct ext2fs_dinode *)((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ino)), ip);
	if (error) {
		brelse(bp);
		vput(vp);
		*vpp = NULL;
		return (error);
	}
	ip->i_block_group = ino_to_cg(fs, ino);
	ip->i_next_alloc_block = 0;
	ip->i_next_alloc_goal = 0;

	/*
	 * Now we want to make sure that block pointers for unused
	 * blocks are zeroed out - ext2_balloc depends on this
	 * although for regular files and directories only
	 *
	 * If IN_E4EXTENTS is enabled, unused blocks are not zeroed
	 * out because we could corrupt the extent tree.
	 */
	if (!(ip->i_flag & IN_E4EXTENTS) &&
	    (S_ISDIR(ip->i_mode) || S_ISREG(ip->i_mode))) {
		used_blocks = howmany(ip->i_size, fs->e2fs_bsize);
		for (i = used_blocks; i < EXT2_NDIR_BLOCKS; i++)
			ip->i_db[i] = 0;
	}
#ifdef EXT2FS_DEBUG
	ext2_print_inode(ip);
	ext4_ext_print_extent_tree_status(ip);
#endif
	bqrelse(bp);

	/*
	 * Initialize the vnode from the inode, check for aliases.
	 * Note that the underlying vnode may have changed.
	 */
	if ((error = ext2_vinit(mp, &ext2_fifoops, &vp)) != 0) {
		vput(vp);
		*vpp = NULL;
		return (error);
	}

	/*
	 * Finish inode initialization.
	 */

	*vpp = vp;
	return (0);
}

/*
 * File handle to vnode
 *
 * Have to be really careful about stale file handles:
 * - check that the inode number is valid
 * - call ext2_vget() to get the locked inode
 * - check for an unallocated inode (i_mode == 0)
 * - check that the given client host has export rights and return
 *   those rights via. exflagsp and credanonp
 */
static int
ext2_fhtovp(struct mount *mp, struct fid *fhp, int flags, struct vnode **vpp)
{
	struct inode *ip;
	struct ufid *ufhp;
	struct vnode *nvp;
	struct m_ext2fs *fs;
	int error;

	ufhp = (struct ufid *)fhp;
	fs = VFSTOEXT2(mp)->um_e2fs;
	if (ufhp->ufid_ino < EXT2_ROOTINO ||
	    ufhp->ufid_ino > fs->e2fs_gcount * fs->e2fs->e2fs_ipg)
		return (ESTALE);

	error = VFS_VGET(mp, ufhp->ufid_ino, LK_EXCLUSIVE, &nvp);
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	ip = VTOI(nvp);
	if (ip->i_mode == 0 ||
	    ip->i_gen != ufhp->ufid_gen || ip->i_nlink <= 0) {
		vput(nvp);
		*vpp = NULLVP;
		return (ESTALE);
	}
	*vpp = nvp;
	vnode_create_vobject(*vpp, 0, curthread);
	return (0);
}

/*
 * Write a superblock and associated information back to disk.
 */
static int
ext2_sbupdate(struct ext2mount *mp, int waitfor)
{
	struct m_ext2fs *fs = mp->um_e2fs;
	struct ext2fs *es = fs->e2fs;
	struct buf *bp;
	int error = 0;

	es->e2fs_bcount = fs->e2fs_bcount & 0xffffffff;
	es->e2fs_rbcount = fs->e2fs_rbcount & 0xffffffff;
	es->e2fs_fbcount = fs->e2fs_fbcount & 0xffffffff;
	if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_64BIT)) {
		es->e4fs_bcount_hi = fs->e2fs_bcount >> 32;
		es->e4fs_rbcount_hi = fs->e2fs_rbcount >> 32;
		es->e4fs_fbcount_hi = fs->e2fs_fbcount >> 32;
	}

	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		ext2_sb_csum_set(fs);

	bp = getblk(mp->um_devvp, SBLOCK, SBSIZE, 0, 0, 0);
	bcopy((caddr_t)es, bp->b_data, (u_int)sizeof(struct ext2fs));
	if (waitfor == MNT_WAIT)
		error = bwrite(bp);
	else
		bawrite(bp);

	/*
	 * The buffers for group descriptors, inode bitmaps and block bitmaps
	 * are not busy at this point and are (hopefully) written by the
	 * usual sync mechanism. No need to write them here.
	 */
	return (error);
}
int
ext2_cgupdate(struct ext2mount *mp, int waitfor)
{
	struct m_ext2fs *fs = mp->um_e2fs;
	struct buf *bp;
	int i, j, g_count = 0, error = 0, allerror = 0;

	allerror = ext2_sbupdate(mp, waitfor);

	/* Update gd csums */
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_GDT_CSUM) ||
	    EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		ext2_gd_csum_set(fs);

	for (i = 0; i < fs->e2fs_gdbcount; i++) {
		bp = getblk(mp->um_devvp, fsbtodb(fs,
		    ext2_cg_location(fs, i)),
		    fs->e2fs_bsize, 0, 0, 0);
		if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_64BIT)) {
			memcpy(bp->b_data, &fs->e2fs_gd[
			    i * fs->e2fs_bsize / sizeof(struct ext2_gd)],
			    fs->e2fs_bsize);
		} else {
			for (j = 0; j < fs->e2fs_bsize / E2FS_REV0_GD_SIZE &&
			    g_count < fs->e2fs_gcount; j++, g_count++)
				memcpy(bp->b_data + j * E2FS_REV0_GD_SIZE,
				    &fs->e2fs_gd[g_count], E2FS_REV0_GD_SIZE);
		}
		if (waitfor == MNT_WAIT)
			error = bwrite(bp);
		else
			bawrite(bp);
	}

	if (!allerror && error)
		allerror = error;
	return (allerror);
}

/*
 * Return the root of a filesystem.
 */
static int
ext2_root(struct mount *mp, int flags, struct vnode **vpp)
{
	struct vnode *nvp;
	int error;

	error = VFS_VGET(mp, EXT2_ROOTINO, LK_EXCLUSIVE, &nvp);
	if (error)
		return (error);
	*vpp = nvp;
	return (0);
}
