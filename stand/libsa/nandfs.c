/*-
 * Copyright (c) 2010-2012 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stdint.h>
#include <ufs/ufs/dinode.h>
#include <fs/nandfs/nandfs_fs.h>
#include "stand.h"
#include "string.h"
#include "zlib.h"

#define DEBUG
#undef DEBUG
#ifdef DEBUG
#define NANDFS_DEBUG(fmt, args...) do { \
    printf("NANDFS_DEBUG:" fmt "\n", ##args); } while (0)
#else
#define NANDFS_DEBUG(fmt, args...)
#endif

struct nandfs_mdt {
	uint32_t	entries_per_block;
	uint32_t	entries_per_group;
	uint32_t	blocks_per_group;
	uint32_t	groups_per_desc_block;	/* desc is super group */
	uint32_t	blocks_per_desc_block;	/* desc is super group */
};

struct bmap_buf {
	LIST_ENTRY(bmap_buf)	list;
	nandfs_daddr_t		blknr;
	uint64_t		*map;
};

struct nandfs_node {
	struct nandfs_inode	*inode;
	LIST_HEAD(, bmap_buf)	bmap_bufs;
};
struct nandfs {
	int	nf_blocksize;
	int	nf_sectorsize;
	int	nf_cpno;

	struct open_file	*nf_file;
	struct nandfs_node	*nf_opened_node;
	u_int			nf_offset;
	uint8_t			*nf_buf;
	int64_t			nf_buf_blknr;

	struct nandfs_fsdata		*nf_fsdata;
	struct nandfs_super_block	*nf_sb;
	struct nandfs_segment_summary	nf_segsum;
	struct nandfs_checkpoint	nf_checkpoint;
	struct nandfs_super_root	nf_sroot;
	struct nandfs_node		nf_ifile;
	struct nandfs_node		nf_datfile;
	struct nandfs_node		nf_cpfile;
	struct nandfs_mdt		nf_datfile_mdt;
	struct nandfs_mdt		nf_ifile_mdt;

	int nf_nindir[NANDFS_NIADDR];
};

static int nandfs_open(const char *, struct open_file *);
static int nandfs_close(struct open_file *);
static int nandfs_read(struct open_file *, void *, size_t, size_t *);
static off_t nandfs_seek(struct open_file *, off_t, int);
static int nandfs_stat(struct open_file *, struct stat *);
static int nandfs_readdir(struct open_file *, struct dirent *);

static int nandfs_buf_read(struct nandfs *, void **, size_t *);
static struct nandfs_node *nandfs_lookup_path(struct nandfs *, const char *);
static int nandfs_read_inode(struct nandfs *, struct nandfs_node *,
    nandfs_lbn_t, u_int, void *, int);
static int nandfs_read_blk(struct nandfs *, nandfs_daddr_t, void *, int);
static int nandfs_bmap_lookup(struct nandfs *, struct nandfs_node *,
    nandfs_lbn_t, nandfs_daddr_t *, int);
static int nandfs_get_checkpoint(struct nandfs *, uint64_t,
    struct nandfs_checkpoint *);
static nandfs_daddr_t nandfs_vtop(struct nandfs *, nandfs_daddr_t);
static void nandfs_calc_mdt_consts(int, struct nandfs_mdt *, int);
static void nandfs_mdt_trans(struct nandfs_mdt *, uint64_t,
    nandfs_daddr_t *, uint32_t *);
static int ioread(struct open_file *, off_t, void *, u_int);
static int nandfs_probe_sectorsize(struct open_file *);

struct fs_ops nandfs_fsops = {
	"nandfs",
	nandfs_open,
	nandfs_close,
	nandfs_read,
	null_write,
	nandfs_seek,
	nandfs_stat,
	nandfs_readdir
};

#define	NINDIR(fs)	((fs)->nf_blocksize / sizeof(nandfs_daddr_t))

/* from NetBSD's src/sys/net/if_ethersubr.c */
static uint32_t
nandfs_crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
	static const uint32_t crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
	};
	size_t i;

	crc = crc ^ ~0U;
	for (i = 0; i < len; i++) {
		crc ^= buf[i];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
	}
	return (crc ^ ~0U);
}

static int
nandfs_check_fsdata_crc(struct nandfs_fsdata *fsdata)
{
	uint32_t fsdata_crc, comp_crc;

	if (fsdata->f_magic != NANDFS_FSDATA_MAGIC)
		return (0);

	/* Preserve crc */
	fsdata_crc = fsdata->f_sum;

	/* Calculate */
	fsdata->f_sum = (0);
	comp_crc = nandfs_crc32(0, (uint8_t *)fsdata, fsdata->f_bytes);

	/* Restore */
	fsdata->f_sum = fsdata_crc;

	/* Check CRC */
	return (fsdata_crc == comp_crc);
}

static int
nandfs_check_superblock_crc(struct nandfs_fsdata *fsdata,
    struct nandfs_super_block *super)
{
	uint32_t super_crc, comp_crc;

	/* Check super block magic */
	if (super->s_magic != NANDFS_SUPER_MAGIC)
		return (0);

	/* Preserve CRC */
	super_crc = super->s_sum;

	/* Calculate */
	super->s_sum = (0);
	comp_crc = nandfs_crc32(0, (uint8_t *)super, fsdata->f_sbbytes);

	/* Restore */
	super->s_sum = super_crc;

	/* Check CRC */
	return (super_crc == comp_crc);
}

static int
nandfs_find_super_block(struct nandfs *fs, struct open_file *f)
{
	struct nandfs_super_block *sb;
	int i, j, n, s;
	int sectors_to_read, error;

	sb = malloc(fs->nf_sectorsize);
	if (sb == NULL)
		return (ENOMEM);

	memset(fs->nf_sb, 0, sizeof(*fs->nf_sb));

	sectors_to_read = (NANDFS_NFSAREAS * fs->nf_fsdata->f_erasesize) /
	    fs->nf_sectorsize;
	for (i = 0; i < sectors_to_read; i++) {
		NANDFS_DEBUG("reading i %d offset %d\n", i,
		    i * fs->nf_sectorsize);
		error = ioread(f, i * fs->nf_sectorsize, (char *)sb,
		    fs->nf_sectorsize);
		if (error) {
			NANDFS_DEBUG("error %d\n", error);
			continue;
		}
		n = fs->nf_sectorsize / sizeof(struct nandfs_super_block);
		s = 0;
		if ((i * fs->nf_sectorsize) % fs->nf_fsdata->f_erasesize == 0) {
			if (fs->nf_sectorsize == sizeof(struct nandfs_fsdata))
				continue;
			else {
				s += (sizeof(struct nandfs_fsdata) /
				    sizeof(struct nandfs_super_block));
			}
		}

		for (j = s; j < n; j++) {
			if (!nandfs_check_superblock_crc(fs->nf_fsdata, &sb[j]))
				continue;
			NANDFS_DEBUG("magic %x wtime %jd, lastcp 0x%jx\n",
			    sb[j].s_magic, sb[j].s_wtime, sb[j].s_last_cno);
			if (sb[j].s_last_cno > fs->nf_sb->s_last_cno)
				memcpy(fs->nf_sb, &sb[j], sizeof(*fs->nf_sb));
		}
	}

	free(sb);

	return (fs->nf_sb->s_magic != 0 ? 0 : EINVAL);
}

static int
nandfs_find_fsdata(struct nandfs *fs, struct open_file *f)
{
	int offset, error, i;

	NANDFS_DEBUG("starting\n");

	offset = 0;
	for (i = 0; i < 64 * NANDFS_NFSAREAS; i++) {
		error = ioread(f, offset, (char *)fs->nf_fsdata,
		    sizeof(struct nandfs_fsdata));
		if (error)
			return (error);
		if (fs->nf_fsdata->f_magic == NANDFS_FSDATA_MAGIC) {
			NANDFS_DEBUG("found at %x, volume %s\n", offset,
			    fs->nf_fsdata->f_volume_name);
			if (nandfs_check_fsdata_crc(fs->nf_fsdata))
				break;
		}
		offset += fs->nf_sectorsize;
	}

	return (error);
}

static int
nandfs_read_structures(struct nandfs *fs, struct open_file *f)
{
	int error;

	error = nandfs_find_fsdata(fs, f);
	if (error)
		return (error);

	error = nandfs_find_super_block(fs, f);

	if (error == 0)
		NANDFS_DEBUG("selected sb with w_time %jd last_pseg %jx\n",
		    fs->nf_sb->s_wtime, fs->nf_sb->s_last_pseg);

	return (error);
}

static int
nandfs_mount(struct nandfs *fs, struct open_file *f)
{
	int err = 0, level;
	uint64_t last_pseg;

	fs->nf_fsdata = malloc(sizeof(struct nandfs_fsdata));
	fs->nf_sb = malloc(sizeof(struct nandfs_super_block));

	err = nandfs_read_structures(fs, f);
	if (err) {
		free(fs->nf_fsdata);
		free(fs->nf_sb);
		return (err);
	}

	fs->nf_blocksize = 1 << (fs->nf_fsdata->f_log_block_size + 10);

	NANDFS_DEBUG("using superblock with wtime %jd\n", fs->nf_sb->s_wtime);

	fs->nf_cpno = fs->nf_sb->s_last_cno;
	last_pseg = fs->nf_sb->s_last_pseg;

	/*
	 * Calculate indirect block levels.
	 */
	nandfs_daddr_t mult;

	mult = 1;
	for (level = 0; level < NANDFS_NIADDR; level++) {
		mult *= NINDIR(fs);
		fs->nf_nindir[level] = mult;
	}

	nandfs_calc_mdt_consts(fs->nf_blocksize, &fs->nf_datfile_mdt,
	    fs->nf_fsdata->f_dat_entry_size);

	nandfs_calc_mdt_consts(fs->nf_blocksize, &fs->nf_ifile_mdt,
	    fs->nf_fsdata->f_inode_size);

	err = ioread(f, last_pseg * fs->nf_blocksize, &fs->nf_segsum,
	    sizeof(struct nandfs_segment_summary));
	if (err) {
		free(fs->nf_sb);
		free(fs->nf_fsdata);
		return (err);
	}

	err = ioread(f, (last_pseg + fs->nf_segsum.ss_nblocks - 1) *
	    fs->nf_blocksize, &fs->nf_sroot, sizeof(struct nandfs_super_root));
	if (err) {
		free(fs->nf_sb);
		free(fs->nf_fsdata);
		return (err);
	}

	fs->nf_datfile.inode = &fs->nf_sroot.sr_dat;
	LIST_INIT(&fs->nf_datfile.bmap_bufs);
	fs->nf_cpfile.inode = &fs->nf_sroot.sr_cpfile;
	LIST_INIT(&fs->nf_cpfile.bmap_bufs);

	err = nandfs_get_checkpoint(fs, fs->nf_cpno, &fs->nf_checkpoint);
	if (err) {
		free(fs->nf_sb);
		free(fs->nf_fsdata);
		return (err);
	}

	NANDFS_DEBUG("checkpoint cp_cno=%lld\n", fs->nf_checkpoint.cp_cno);
	NANDFS_DEBUG("checkpoint cp_inodes_count=%lld\n",
	    fs->nf_checkpoint.cp_inodes_count);
	NANDFS_DEBUG("checkpoint cp_ifile_inode.i_blocks=%lld\n",
	    fs->nf_checkpoint.cp_ifile_inode.i_blocks);

	fs->nf_ifile.inode = &fs->nf_checkpoint.cp_ifile_inode;
	LIST_INIT(&fs->nf_ifile.bmap_bufs);
	return (0);
}

#define NINDIR(fs)	((fs)->nf_blocksize / sizeof(nandfs_daddr_t))

static int
nandfs_open(const char *path, struct open_file *f)
{
	struct nandfs *fs;
	struct nandfs_node *node;
	int err, bsize, level;

	NANDFS_DEBUG("nandfs_open('%s', %p)\n", path, f);

	fs = malloc(sizeof(struct nandfs));
	f->f_fsdata = fs;
	fs->nf_file = f;

	bsize = nandfs_probe_sectorsize(f);
	if (bsize < 0) {
		printf("Cannot probe medium sector size\n");
		return (EINVAL);
	}

	fs->nf_sectorsize = bsize;

	/*
	 * Calculate indirect block levels.
	 */
	nandfs_daddr_t mult;

	mult = 1;
	for (level = 0; level < NANDFS_NIADDR; level++) {
		mult *= NINDIR(fs);
		fs->nf_nindir[level] = mult;
	}

	NANDFS_DEBUG("fs %p nf_sectorsize=%x\n", fs, fs->nf_sectorsize);

	err = nandfs_mount(fs, f);
	if (err) {
		NANDFS_DEBUG("Cannot mount nandfs: %s\n", strerror(err));
		return (err);
	}

	node = nandfs_lookup_path(fs, path);
	if (node == NULL)
		return (EINVAL);

	fs->nf_offset = 0;
	fs->nf_buf = NULL;
	fs->nf_buf_blknr = -1;
	fs->nf_opened_node = node;
	LIST_INIT(&fs->nf_opened_node->bmap_bufs);
	return (0);
}

static void
nandfs_free_node(struct nandfs_node *node)
{
	struct bmap_buf *bmap, *tmp;

	free(node->inode);
	LIST_FOREACH_SAFE(bmap, &node->bmap_bufs, list, tmp) {
		LIST_REMOVE(bmap, list);
		free(bmap->map);
		free(bmap);
	}
	free(node);
}

static int
nandfs_close(struct open_file *f)
{
	struct nandfs *fs = f->f_fsdata;

	NANDFS_DEBUG("nandfs_close(%p)\n", f);

	if (fs->nf_buf != NULL)
		free(fs->nf_buf);

	nandfs_free_node(fs->nf_opened_node);
	free(fs->nf_sb);
	free(fs);
	return (0);
}

static int
nandfs_read(struct open_file *f, void *addr, size_t size, size_t *resid)
{
	struct nandfs *fs = (struct nandfs *)f->f_fsdata;
	size_t csize, buf_size;
	void *buf;
	int error = 0;

	NANDFS_DEBUG("nandfs_read(file=%p, addr=%p, size=%d)\n", f, addr, size);

	while (size != 0) {
		if (fs->nf_offset >= fs->nf_opened_node->inode->i_size)
			break;

		error = nandfs_buf_read(fs, &buf, &buf_size);
		if (error)
			break;

		csize = size;
		if (csize > buf_size)
			csize = buf_size;

		bcopy(buf, addr, csize);

		fs->nf_offset += csize;
		addr = (char *)addr + csize;
		size -= csize;
	}

	if (resid)
		*resid = size;
	return (error);
}

static off_t
nandfs_seek(struct open_file *f, off_t offset, int where)
{
	struct nandfs *fs = f->f_fsdata;
	off_t off;
	u_int size;

	NANDFS_DEBUG("nandfs_seek(file=%p, offset=%lld, where=%d)\n", f,
	    offset, where);

	size = fs->nf_opened_node->inode->i_size;

	switch (where) {
	case SEEK_SET:
		off = 0;
		break;
	case SEEK_CUR:
		off = fs->nf_offset;
		break;
	case SEEK_END:
		off = size;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}

	off += offset;
	if (off < 0 || off > size) {
		errno = EINVAL;
		return(-1);
	}

	fs->nf_offset = (u_int)off;

	return (off);
}

static int
nandfs_stat(struct open_file *f, struct stat *sb)
{
	struct nandfs *fs = f->f_fsdata;

	NANDFS_DEBUG("nandfs_stat(file=%p, stat=%p)\n", f, sb);

	sb->st_size = fs->nf_opened_node->inode->i_size;
	sb->st_mode = fs->nf_opened_node->inode->i_mode;
	sb->st_uid = fs->nf_opened_node->inode->i_uid;
	sb->st_gid = fs->nf_opened_node->inode->i_gid;
	return (0);
}

static int
nandfs_readdir(struct open_file *f, struct dirent *d)
{
	struct nandfs *fs = f->f_fsdata;
	struct nandfs_dir_entry *dirent;
	void *buf;
	size_t buf_size;

	NANDFS_DEBUG("nandfs_readdir(file=%p, dirent=%p)\n", f, d);

	if (fs->nf_offset >= fs->nf_opened_node->inode->i_size) {
		NANDFS_DEBUG("nandfs_readdir(file=%p, dirent=%p) ENOENT\n",
		    f, d);
		return (ENOENT);
	}

	if (nandfs_buf_read(fs, &buf, &buf_size)) {
		NANDFS_DEBUG("nandfs_readdir(file=%p, dirent=%p)"
		    "buf_read failed\n", f, d);
		return (EIO);
	}

	NANDFS_DEBUG("nandfs_readdir(file=%p, dirent=%p) moving forward\n",
	    f, d);

	dirent = (struct nandfs_dir_entry *)buf;
	fs->nf_offset += dirent->rec_len;
	strncpy(d->d_name, dirent->name, dirent->name_len);
	d->d_name[dirent->name_len] = '\0';
	d->d_type = dirent->file_type;
	return (0);
}

static int
nandfs_buf_read(struct nandfs *fs, void **buf_p, size_t *size_p)
{
	nandfs_daddr_t blknr, blkoff;

	blknr = fs->nf_offset / fs->nf_blocksize;
	blkoff = fs->nf_offset % fs->nf_blocksize;

	if (blknr != fs->nf_buf_blknr) {
		if (fs->nf_buf == NULL)
			fs->nf_buf = malloc(fs->nf_blocksize);

		if (nandfs_read_inode(fs, fs->nf_opened_node, blknr, 1,
		    fs->nf_buf, 0))
			return (EIO);

		fs->nf_buf_blknr = blknr;
	}

	*buf_p = fs->nf_buf + blkoff;
	*size_p = fs->nf_blocksize - blkoff;

	NANDFS_DEBUG("nandfs_buf_read buf_p=%p size_p=%d\n", *buf_p, *size_p);

	if (*size_p > fs->nf_opened_node->inode->i_size - fs->nf_offset)
		*size_p = fs->nf_opened_node->inode->i_size - fs->nf_offset;

	return (0);
}

static struct nandfs_node *
nandfs_lookup_node(struct nandfs *fs, uint64_t ino)
{
	uint64_t blocknr;
	int entrynr;
	struct nandfs_inode *buffer;
	struct nandfs_node *node;
	struct nandfs_inode *inode;

	NANDFS_DEBUG("nandfs_lookup_node ino=%lld\n", ino);

	if (ino == 0) {
		printf("nandfs_lookup_node: invalid inode requested\n");
		return (NULL);
	}

	buffer = malloc(fs->nf_blocksize);
	inode = malloc(sizeof(struct nandfs_inode));
	node = malloc(sizeof(struct nandfs_node));

	nandfs_mdt_trans(&fs->nf_ifile_mdt, ino, &blocknr, &entrynr);

	if (nandfs_read_inode(fs, &fs->nf_ifile, blocknr, 1, buffer, 0))
		return (NULL);

	memcpy(inode, &buffer[entrynr], sizeof(struct nandfs_inode));
	node->inode = inode;
	free(buffer);
	return (node);
}

static struct nandfs_node *
nandfs_lookup_path(struct nandfs *fs, const char *path)
{
	struct nandfs_node *node;
	struct nandfs_dir_entry *dirent;
	char *namebuf;
	uint64_t i, done, pinode, inode;
	int nlinks = 0, counter, len, link_len, nameidx;
	uint8_t *buffer, *orig;
	char *strp, *lpath;

	buffer = malloc(fs->nf_blocksize);
	orig = buffer;

	namebuf = malloc(2 * MAXPATHLEN + 2);
	strncpy(namebuf, path, MAXPATHLEN);
	namebuf[MAXPATHLEN] = '\0';
	done = nameidx = 0;
	lpath = namebuf;

	/* Get the root inode */
	node = nandfs_lookup_node(fs, NANDFS_ROOT_INO);
	inode = NANDFS_ROOT_INO;

	while ((strp = strsep(&lpath, "/")) != NULL) {
		if (*strp == '\0')
			continue;
		if ((node->inode->i_mode & IFMT) != IFDIR) {
			nandfs_free_node(node);
			node = NULL;
			goto out;
		}

		len = strlen(strp);
		NANDFS_DEBUG("%s: looking for %s\n", __func__, strp);
		for (i = 0; i < node->inode->i_blocks; i++) {
			if (nandfs_read_inode(fs, node, i, 1, orig, 0)) {
				node = NULL;
				goto out;
			}

			buffer = orig;
			done = counter = 0;
			while (1) {
				dirent = 
				    (struct nandfs_dir_entry *)(void *)buffer;
				NANDFS_DEBUG("%s: dirent.name = %s\n",
				    __func__, dirent->name);
				NANDFS_DEBUG("%s: dirent.rec_len = %d\n",
				    __func__, dirent->rec_len);
				NANDFS_DEBUG("%s: dirent.inode = %lld\n",
				    __func__, dirent->inode);
				if (len == dirent->name_len &&
				    (strncmp(strp, dirent->name, len) == 0) &&
				    dirent->inode != 0) {
					nandfs_free_node(node);
					node = nandfs_lookup_node(fs,
					    dirent->inode);
					pinode = inode;
					inode = dirent->inode;
					done = 1;
					break;
				}

				counter += dirent->rec_len;
				buffer += dirent->rec_len;

				if (counter == fs->nf_blocksize)
					break;
			}

			if (done)
				break;
		}

		if (!done) {
			node = NULL;
			goto out;
		}

		NANDFS_DEBUG("%s: %.*s has mode %o\n", __func__,
		    dirent->name_len, dirent->name, node->inode->i_mode);

		if ((node->inode->i_mode & IFMT) == IFLNK) {
			NANDFS_DEBUG("%s: %.*s is symlink\n",
			    __func__, dirent->name_len, dirent->name);
			link_len = node->inode->i_size;

			if (++nlinks > MAXSYMLINKS) {
				nandfs_free_node(node);
				node = NULL;
				goto out;
			}

			if (nandfs_read_inode(fs, node, 0, 1, orig, 0)) {
				nandfs_free_node(node);
				node = NULL;
				goto out;
			}

			NANDFS_DEBUG("%s: symlink is  %.*s\n",
			    __func__, link_len, (char *)orig);

			nameidx = (nameidx == 0) ? MAXPATHLEN + 1 : 0;
			bcopy((char *)orig, namebuf + nameidx,
			    (unsigned)link_len);
			if (lpath != NULL) {
				namebuf[nameidx + link_len++] = '/';
				strncpy(namebuf + nameidx + link_len, lpath,
				    MAXPATHLEN - link_len);
				namebuf[nameidx + MAXPATHLEN] = '\0';
			} else
				namebuf[nameidx + link_len] = '\0';

			NANDFS_DEBUG("%s: strp=%s, lpath=%s, namebuf0=%s, "
			    "namebuf1=%s, idx=%d\n", __func__, strp, lpath,
			    namebuf + 0, namebuf + MAXPATHLEN + 1, nameidx);

			lpath = namebuf + nameidx;

			nandfs_free_node(node);

			/*
			 * If absolute pathname, restart at root. Otherwise
			 * continue with out parent inode.
			 */
			inode = (orig[0] == '/') ? NANDFS_ROOT_INO : pinode;
			node = nandfs_lookup_node(fs, inode);
		}
	}

out:
	free(namebuf);
	free(orig);
	return (node);
}

static int
nandfs_read_inode(struct nandfs *fs, struct nandfs_node *node,
    nandfs_daddr_t blknr, u_int nblks, void *buf, int raw)
{
	uint64_t *pblks;
	uint64_t *vblks;
	u_int i;
	int error;

	pblks = malloc(nblks * sizeof(uint64_t));
	vblks = malloc(nblks * sizeof(uint64_t));

	NANDFS_DEBUG("nandfs_read_inode fs=%p node=%p blknr=%lld nblks=%d\n",
	    fs, node, blknr, nblks);
	for (i = 0; i < nblks; i++) {
		error = nandfs_bmap_lookup(fs, node, blknr + i, &vblks[i], raw);
		if (error) {
			free(pblks);
			free(vblks);
			return (error);
		}
		if (raw == 0)
			pblks[i] = nandfs_vtop(fs, vblks[i]);
		else
			pblks[i] = vblks[i];
	}

	for (i = 0; i < nblks; i++) {
		if (ioread(fs->nf_file, pblks[i] * fs->nf_blocksize, buf,
		    fs->nf_blocksize)) {
			free(pblks);
			free(vblks);
			return (EIO);
		}

		buf = (void *)((uintptr_t)buf + fs->nf_blocksize);
	}

	free(pblks);
	free(vblks);
	return (0);
}

static int
nandfs_read_blk(struct nandfs *fs, nandfs_daddr_t blknr, void *buf, int phys)
{
	uint64_t pblknr;

	pblknr = (phys ? blknr : nandfs_vtop(fs, blknr));

	return (ioread(fs->nf_file, pblknr * fs->nf_blocksize, buf,
	    fs->nf_blocksize));
}

static int
nandfs_get_checkpoint(struct nandfs *fs, uint64_t cpno,
    struct nandfs_checkpoint *cp)
{
	uint64_t blocknr;
	int blockoff, cp_per_block, dlen;
	uint8_t *buf;

	NANDFS_DEBUG("nandfs_get_checkpoint(fs=%p cpno=%lld)\n", fs, cpno);

	buf = malloc(fs->nf_blocksize);

	cpno += NANDFS_CPFILE_FIRST_CHECKPOINT_OFFSET - 1;
	dlen = fs->nf_fsdata->f_checkpoint_size;
	cp_per_block = fs->nf_blocksize / dlen;
	blocknr = cpno / cp_per_block;
	blockoff = (cpno % cp_per_block) * dlen;

	if (nandfs_read_inode(fs, &fs->nf_cpfile, blocknr, 1, buf, 0)) {
		free(buf);
		return (EINVAL);
	}

	memcpy(cp, buf + blockoff, sizeof(struct nandfs_checkpoint));
	free(buf);

	return (0);
}

static uint64_t *
nandfs_get_map(struct nandfs *fs, struct nandfs_node *node, nandfs_daddr_t blknr,
    int phys)
{
	struct bmap_buf *bmap;
	uint64_t *map;

	LIST_FOREACH(bmap, &node->bmap_bufs, list) {
		if (bmap->blknr == blknr)
			return (bmap->map);
	}

	map = malloc(fs->nf_blocksize);
	if (nandfs_read_blk(fs, blknr, map, phys)) {
		free(map);
		return (NULL);
	}

	bmap = malloc(sizeof(struct bmap_buf));
	bmap->blknr = blknr;
	bmap->map = map;

	LIST_INSERT_HEAD(&node->bmap_bufs, bmap, list);

	NANDFS_DEBUG("%s:(node=%p, map=%p)\n", __func__, node, map);
	return (map);
}

static int
nandfs_bmap_lookup(struct nandfs *fs, struct nandfs_node *node,
    nandfs_lbn_t lblknr, nandfs_daddr_t *vblknr, int phys)
{
	struct nandfs_inode *ino;
	nandfs_daddr_t ind_block_num;
	uint64_t *map;
	int idx;
	int level;

	ino = node->inode;

	if (lblknr < NANDFS_NDADDR) {
		*vblknr = ino->i_db[lblknr];
		return (0);
	}

	lblknr -= NANDFS_NDADDR;

	/*
	 * nindir[0] = NINDIR
	 * nindir[1] = NINDIR**2
	 * nindir[2] = NINDIR**3
	 *	etc
	 */
	for (level = 0; level < NANDFS_NIADDR; level++) {
		NANDFS_DEBUG("lblknr=%jx fs->nf_nindir[%d]=%d\n", lblknr, level, fs->nf_nindir[level]);
		if (lblknr < fs->nf_nindir[level])
			break;
		lblknr -= fs->nf_nindir[level];
	}

	if (level == NANDFS_NIADDR) {
		/* Block number too high */
		NANDFS_DEBUG("lblknr %jx too high\n", lblknr);
		return (EFBIG);
	}

	ind_block_num = ino->i_ib[level];

	for (; level >= 0; level--) {
		if (ind_block_num == 0) {
			*vblknr = 0;	/* missing */
			return (0);
		}

		twiddle(1);
		NANDFS_DEBUG("calling get_map with %jx\n", ind_block_num);
		map = nandfs_get_map(fs, node, ind_block_num, phys);
		if (map == NULL)
			return (EIO);

		if (level > 0) {
			idx = lblknr / fs->nf_nindir[level - 1];
			lblknr %= fs->nf_nindir[level - 1];
		} else
			idx = lblknr;

		ind_block_num = ((nandfs_daddr_t *)map)[idx];
	}

	*vblknr = ind_block_num;

	return (0);
}

static nandfs_daddr_t
nandfs_vtop(struct nandfs *fs, nandfs_daddr_t vblocknr)
{
	nandfs_lbn_t blocknr;
	nandfs_daddr_t pblocknr;
	int entrynr;
	struct nandfs_dat_entry *dat;

	dat = malloc(fs->nf_blocksize);
	nandfs_mdt_trans(&fs->nf_datfile_mdt, vblocknr, &blocknr, &entrynr);

	if (nandfs_read_inode(fs, &fs->nf_datfile, blocknr, 1, dat, 1)) {
		free(dat);
		return (0);
	}

	NANDFS_DEBUG("nandfs_vtop entrynr=%d vblocknr=%lld pblocknr=%lld\n",
	    entrynr, vblocknr, dat[entrynr].de_blocknr);

	pblocknr = dat[entrynr].de_blocknr;
	free(dat);
	return (pblocknr);
}

static void
nandfs_calc_mdt_consts(int blocksize, struct nandfs_mdt *mdt, int entry_size)
{

	mdt->entries_per_group = blocksize * 8;	   /* bits in sector */
	mdt->entries_per_block = blocksize / entry_size;
	mdt->blocks_per_group  =
	    (mdt->entries_per_group -1) / mdt->entries_per_block + 1 + 1;
	mdt->groups_per_desc_block =
	    blocksize / sizeof(struct nandfs_block_group_desc);
	mdt->blocks_per_desc_block =
	    mdt->groups_per_desc_block * mdt->blocks_per_group + 1;
}

static void
nandfs_mdt_trans(struct nandfs_mdt *mdt, uint64_t index,
    nandfs_daddr_t *blocknr, uint32_t *entry_in_block)
{
	nandfs_daddr_t blknr;
	uint64_t group, group_offset, blocknr_in_group;
	uint64_t desc_block, desc_offset;

	/* Calculate our offset in the file */
	group = index / mdt->entries_per_group;
	group_offset = index % mdt->entries_per_group;
	desc_block = group / mdt->groups_per_desc_block;
	desc_offset = group % mdt->groups_per_desc_block;
	blocknr_in_group = group_offset / mdt->entries_per_block;

	/* To descgroup offset */
	blknr = 1 + desc_block * mdt->blocks_per_desc_block;

	/* To group offset */
	blknr += desc_offset * mdt->blocks_per_group;

	/* To actual file block */
	blknr += 1 + blocknr_in_group;

	*blocknr        = blknr;
	*entry_in_block = group_offset % mdt->entries_per_block;
}

static int
ioread(struct open_file *f, off_t pos, void *buf, u_int length)
{
	void *buffer;
	int err;
	int bsize = ((struct nandfs *)f->f_fsdata)->nf_sectorsize;
	u_int off, nsec;

	off = pos % bsize;
	pos /= bsize;
	nsec = howmany(length, bsize);

	NANDFS_DEBUG("pos=%lld length=%d off=%d nsec=%d\n", pos, length,
	    off, nsec);

	buffer = malloc(nsec * bsize);

	err = (f->f_dev->dv_strategy)(f->f_devdata, F_READ, pos,
	    nsec * bsize, buffer, NULL);

	memcpy(buf, (void *)((uintptr_t)buffer + off), length);
	free(buffer);

	return (err);
}

static int
nandfs_probe_sectorsize(struct open_file *f)
{
	void *buffer;
	int i, err;

	buffer = malloc(16 * 1024);

	NANDFS_DEBUG("probing for sector size: ");

	for (i = 512; i < (16 * 1024); i <<= 1) {
		NANDFS_DEBUG("%d ", i);
		err = (f->f_dev->dv_strategy)(f->f_devdata, F_READ, 0, i,
		    buffer, NULL);

		if (err == 0) {
			NANDFS_DEBUG("found");
			free(buffer);
			return (i);
		}
	}

	free(buffer);
	NANDFS_DEBUG("not found\n");
	return (-1);
}
