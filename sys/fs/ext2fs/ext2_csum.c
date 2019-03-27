/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017, Fedor Uporov
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/conf.h>
#include <sys/mount.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_dir.h>
#include <fs/ext2fs/htree.h>
#include <fs/ext2fs/ext2_extattr.h>
#include <fs/ext2fs/ext2_extern.h>

#define EXT2_BG_INODE_BITMAP_CSUM_HI_END	\
	(offsetof(struct ext2_gd, ext4bgd_i_bmap_csum_hi) + \
	 sizeof(uint16_t))

#define EXT2_INODE_CSUM_HI_EXTRA_END	\
	(offsetof(struct ext2fs_dinode, e2di_chksum_hi) + sizeof(uint16_t) - \
	 E2FS_REV0_INODE_SIZE)

#define EXT2_BG_BLOCK_BITMAP_CSUM_HI_LOCATION	\
	(offsetof(struct ext2_gd, ext4bgd_b_bmap_csum_hi) + \
	 sizeof(uint16_t))

void
ext2_sb_csum_set_seed(struct m_ext2fs *fs)
{

	if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_CSUM_SEED))
		fs->e2fs_csum_seed = fs->e2fs->e4fs_chksum_seed;
	else if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		fs->e2fs_csum_seed = calculate_crc32c(~0, fs->e2fs->e2fs_uuid,
		    sizeof(fs->e2fs->e2fs_uuid));
	}
	else
		fs->e2fs_csum_seed = 0;
}

int
ext2_sb_csum_verify(struct m_ext2fs *fs)
{

	if (fs->e2fs->e4fs_chksum_type != EXT4_CRC32C_CHKSUM) {
		printf(
"WARNING: mount of %s denied due bad sb csum type\n", fs->e2fs_fsmnt);
		return (EINVAL);
	}
	if (fs->e2fs->e4fs_sbchksum !=
	    calculate_crc32c(~0, (const char *)fs->e2fs,
	    offsetof(struct ext2fs, e4fs_sbchksum))) {
		printf(
"WARNING: mount of %s denied due bad sb csum=0x%x, expected=0x%x - run fsck\n",
		    fs->e2fs_fsmnt, fs->e2fs->e4fs_sbchksum, calculate_crc32c(~0,
		    (const char *)fs->e2fs, offsetof(struct ext2fs, e4fs_sbchksum)));
		return (EINVAL);
	}

	return (0);
}

void
ext2_sb_csum_set(struct m_ext2fs *fs)
{

	fs->e2fs->e4fs_sbchksum = calculate_crc32c(~0, (const char *)fs->e2fs,
	    offsetof(struct ext2fs, e4fs_sbchksum));
}

static uint32_t
ext2_extattr_blk_csum(struct inode *ip, uint64_t facl,
    struct ext2fs_extattr_header *header)
{
	struct m_ext2fs *fs;
	uint32_t crc, old_crc;

	fs = ip->i_e2fs;

	old_crc = header->h_checksum;

	header->h_checksum = 0;
	crc = calculate_crc32c(fs->e2fs_csum_seed, (uint8_t *)&facl, sizeof(facl));
	crc = calculate_crc32c(crc, (uint8_t *)header, fs->e2fs_bsize);
	header->h_checksum = old_crc;

	return (crc);
}

int
ext2_extattr_blk_csum_verify(struct inode *ip, struct buf *bp)
{
	struct ext2fs_extattr_header *header;

	header = (struct ext2fs_extattr_header *)bp->b_data;

	if (EXT2_HAS_RO_COMPAT_FEATURE(ip->i_e2fs, EXT2F_ROCOMPAT_METADATA_CKSUM) &&
	    (header->h_checksum != ext2_extattr_blk_csum(ip, ip->i_facl, header))) {
		printf("WARNING: bad extattr csum detected, ip=%lu - run fsck\n",
		    (unsigned long)ip->i_number);
		return (EIO);
	}

	return (0);
}

void
ext2_extattr_blk_csum_set(struct inode *ip, struct buf *bp)
{
	struct ext2fs_extattr_header *header;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(ip->i_e2fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return;

	header = (struct ext2fs_extattr_header *)bp->b_data;
	header->h_checksum = ext2_extattr_blk_csum(ip, ip->i_facl, header);
}

void
ext2_init_dirent_tail(struct ext2fs_direct_tail *tp)
{
	memset(tp, 0, sizeof(struct ext2fs_direct_tail));
	tp->e2dt_rec_len = sizeof(struct ext2fs_direct_tail);
	tp->e2dt_reserved_ft = EXT2_FT_DIR_CSUM;
}

int
ext2_is_dirent_tail(struct inode *ip, struct ext2fs_direct_2 *ep)
{
	struct m_ext2fs *fs;
	struct ext2fs_direct_tail *tp;

	fs = ip->i_e2fs;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return (0);

	tp = (struct ext2fs_direct_tail *)ep;
	if (tp->e2dt_reserved_zero1 == 0 &&
	    tp->e2dt_rec_len == sizeof(struct ext2fs_direct_tail) &&
	    tp->e2dt_reserved_zero2 == 0 &&
	    tp->e2dt_reserved_ft == EXT2_FT_DIR_CSUM)
		return (1);

	return (0);
}

struct ext2fs_direct_tail *
ext2_dirent_get_tail(struct inode *ip, struct ext2fs_direct_2 *ep)
{
	struct ext2fs_direct_2 *dep;
	void *top;
	unsigned int rec_len;

	dep = ep;
	top = EXT2_DIRENT_TAIL(ep, ip->i_e2fs->e2fs_bsize);
	rec_len = dep->e2d_reclen;

	while (rec_len && !(rec_len & 0x3)) {
		dep = (struct ext2fs_direct_2 *)(((char *)dep) + rec_len);
		if ((void *)dep >= top)
			break;
		rec_len = dep->e2d_reclen;
	}

	if (dep != top)
		return (NULL);

	if (ext2_is_dirent_tail(ip, dep))
		return ((struct ext2fs_direct_tail *)dep);

	return (NULL);
}

static uint32_t
ext2_dirent_csum(struct inode *ip, struct ext2fs_direct_2 *ep, int size)
{
	struct m_ext2fs *fs;
	char *buf;
	uint32_t inum, gen, crc;

	fs = ip->i_e2fs;

	buf = (char *)ep;

	inum = ip->i_number;
	gen = ip->i_gen;
	crc = calculate_crc32c(fs->e2fs_csum_seed, (uint8_t *)&inum, sizeof(inum));
	crc = calculate_crc32c(crc, (uint8_t *)&gen, sizeof(gen));
	crc = calculate_crc32c(crc, (uint8_t *)buf, size);

	return (crc);
}

int
ext2_dirent_csum_verify(struct inode *ip, struct ext2fs_direct_2 *ep)
{
	uint32_t calculated;
	struct ext2fs_direct_tail *tp;

	tp = ext2_dirent_get_tail(ip, ep);
	if (tp == NULL)
		return (0);

	calculated = ext2_dirent_csum(ip, ep, (char *)tp - (char *)ep);
	if (calculated != tp->e2dt_checksum)
		return (EIO);

	return (0);
}

static struct ext2fs_htree_count *
ext2_get_dx_count(struct inode *ip, struct ext2fs_direct_2 *ep, int *offset)
{
	struct ext2fs_direct_2 *dp;
	struct ext2fs_htree_root_info *root;
	int count_offset;

	if (ep->e2d_reclen == EXT2_BLOCK_SIZE(ip->i_e2fs))
		count_offset = 8;
	else if (ep->e2d_reclen == 12) {
		dp = (struct ext2fs_direct_2 *)(((char *)ep) + 12);
		if (dp->e2d_reclen != EXT2_BLOCK_SIZE(ip->i_e2fs) - 12)
			return (NULL);

		root = (struct ext2fs_htree_root_info *)(((char *)dp + 12));
		if (root->h_reserved1 ||
		    root->h_info_len != sizeof(struct ext2fs_htree_root_info))
			return (NULL);

		count_offset = 32;
	} else
		return (NULL);

	if (offset)
		*offset = count_offset;

	return ((struct ext2fs_htree_count *)(((char *)ep) + count_offset));
}

static uint32_t
ext2_dx_csum(struct inode *ip, struct ext2fs_direct_2 *ep, int count_offset,
    int count, struct ext2fs_htree_tail *tp)
{
	struct m_ext2fs *fs;
	char *buf;
	int size;
	uint32_t inum, old_csum, gen, crc;

	fs = ip->i_e2fs;

	buf = (char *)ep;

	size = count_offset + (count * sizeof(struct ext2fs_htree_entry));
	old_csum = tp->ht_checksum;
	tp->ht_checksum = 0;

	inum = ip->i_number;
	gen = ip->i_gen;
	crc = calculate_crc32c(fs->e2fs_csum_seed, (uint8_t *)&inum, sizeof(inum));
	crc = calculate_crc32c(crc, (uint8_t *)&gen, sizeof(gen));
	crc = calculate_crc32c(crc, (uint8_t *)buf, size);
	crc = calculate_crc32c(crc, (uint8_t *)tp, sizeof(struct ext2fs_htree_tail));
	tp->ht_checksum = old_csum;

	return (crc);
}

int
ext2_dx_csum_verify(struct inode *ip, struct ext2fs_direct_2 *ep)
{
	uint32_t calculated;
	struct ext2fs_htree_count *cp;
	struct ext2fs_htree_tail *tp;
	int count_offset, limit, count;

	cp = ext2_get_dx_count(ip, ep, &count_offset);
	if (cp == NULL)
		return (0);

	limit = cp->h_entries_max;
	count = cp->h_entries_num;
	if (count_offset + (limit * sizeof(struct ext2fs_htree_entry)) >
	    ip->i_e2fs->e2fs_bsize - sizeof(struct ext2fs_htree_tail))
		return (EIO);

	tp = (struct ext2fs_htree_tail *)(((struct ext2fs_htree_entry *)cp) + limit);
	calculated = ext2_dx_csum(ip, ep,  count_offset, count, tp);

	if (tp->ht_checksum != calculated)
		return (EIO);

	return (0);
}

int
ext2_dir_blk_csum_verify(struct inode *ip, struct buf *bp)
{
	struct m_ext2fs *fs;
	struct ext2fs_direct_2 *ep;
	int error = 0;

	fs = ip->i_e2fs;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return (error);

	ep = (struct ext2fs_direct_2 *)bp->b_data;

	if (ext2_dirent_get_tail(ip, ep) != NULL)
		error = ext2_dirent_csum_verify(ip, ep);
	else if (ext2_get_dx_count(ip, ep, NULL) != NULL)
		error = ext2_dx_csum_verify(ip, ep);

	if (error)
		printf("WARNING: bad directory csum detected, ip=%lu"
		    " - run fsck\n", (unsigned long)ip->i_number);

	return (error);
}

void
ext2_dirent_csum_set(struct inode *ip, struct ext2fs_direct_2 *ep)
{
	struct m_ext2fs *fs;
	struct ext2fs_direct_tail *tp;

	fs = ip->i_e2fs;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return;

	tp = ext2_dirent_get_tail(ip, ep);
	if (tp == NULL)
		return;

	tp->e2dt_checksum =
	    ext2_dirent_csum(ip, ep, (char *)tp - (char *)ep);
}

void
ext2_dx_csum_set(struct inode *ip, struct ext2fs_direct_2 *ep)
{
	struct m_ext2fs *fs;
	struct ext2fs_htree_count *cp;
	struct ext2fs_htree_tail *tp;
	int count_offset, limit, count;

	fs = ip->i_e2fs;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return;

	cp = ext2_get_dx_count(ip, ep, &count_offset);
	if (cp == NULL)
		return;

	limit = cp->h_entries_max;
	count = cp->h_entries_num;
	if (count_offset + (limit * sizeof(struct ext2fs_htree_entry)) >
	    ip->i_e2fs->e2fs_bsize - sizeof(struct ext2fs_htree_tail))
		return;

	tp = (struct ext2fs_htree_tail *)(((struct ext2fs_htree_entry *)cp) + limit);
	tp->ht_checksum = ext2_dx_csum(ip, ep,  count_offset, count, tp);
}

static uint32_t
ext2_extent_blk_csum(struct inode *ip, struct ext4_extent_header *ehp)
{
	struct m_ext2fs *fs;
	size_t size;
	uint32_t inum, gen, crc;

	fs = ip->i_e2fs;

	size = EXT4_EXTENT_TAIL_OFFSET(ehp) +
	    offsetof(struct ext4_extent_tail, et_checksum);

	inum = ip->i_number;
	gen = ip->i_gen;
	crc = calculate_crc32c(fs->e2fs_csum_seed, (uint8_t *)&inum, sizeof(inum));
	crc = calculate_crc32c(crc, (uint8_t *)&gen, sizeof(gen));
	crc = calculate_crc32c(crc, (uint8_t *)ehp, size);

	return (crc);
}

int
ext2_extent_blk_csum_verify(struct inode *ip, void *data)
{
	struct m_ext2fs *fs;
	struct ext4_extent_header *ehp;
	struct ext4_extent_tail *etp;
	uint32_t provided, calculated;

	fs = ip->i_e2fs;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return (0);

	ehp = (struct ext4_extent_header *)data;
	etp = (struct ext4_extent_tail *)(((char *)ehp) +
	    EXT4_EXTENT_TAIL_OFFSET(ehp));

	provided = etp->et_checksum;
	calculated = ext2_extent_blk_csum(ip, ehp);

	if (provided != calculated) {
		printf("WARNING: bad extent csum detected, ip=%lu - run fsck\n",
		    (unsigned long)ip->i_number);
		return (EIO);
	}

	return (0);
}

void
ext2_extent_blk_csum_set(struct inode *ip, void *data)
{
	struct m_ext2fs *fs;
	struct ext4_extent_header *ehp;
	struct ext4_extent_tail *etp;

	fs = ip->i_e2fs;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return;

	ehp = (struct ext4_extent_header *)data;
	etp = (struct ext4_extent_tail *)(((char *)data) +
	    EXT4_EXTENT_TAIL_OFFSET(ehp));

	etp->et_checksum = ext2_extent_blk_csum(ip,
	    (struct ext4_extent_header *)data);
}

int
ext2_gd_i_bitmap_csum_verify(struct m_ext2fs *fs, int cg, struct buf *bp)
{
	uint32_t hi, provided, calculated;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return (0);

	provided = fs->e2fs_gd[cg].ext4bgd_i_bmap_csum;
	calculated = calculate_crc32c(fs->e2fs_csum_seed, bp->b_data,
	    fs->e2fs->e2fs_ipg / 8);
	if (fs->e2fs->e3fs_desc_size >= EXT2_BG_INODE_BITMAP_CSUM_HI_END) {
		hi = fs->e2fs_gd[cg].ext4bgd_i_bmap_csum_hi;
		provided |= (hi << 16);
	} else
		calculated &= 0xFFFF;

	if (provided != calculated) {
		printf("WARNING: bad inode bitmap csum detected, "
		    "cg=%d - run fsck\n", cg);
		return (EIO);
	}

	return (0);
}

void
ext2_gd_i_bitmap_csum_set(struct m_ext2fs *fs, int cg, struct buf *bp)
{
	uint32_t csum;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return;

	csum = calculate_crc32c(fs->e2fs_csum_seed, bp->b_data,
	    fs->e2fs->e2fs_ipg / 8);
	fs->e2fs_gd[cg].ext4bgd_i_bmap_csum = csum & 0xFFFF;
	if (fs->e2fs->e3fs_desc_size >= EXT2_BG_INODE_BITMAP_CSUM_HI_END)
		fs->e2fs_gd[cg].ext4bgd_i_bmap_csum_hi = csum >> 16;
}

int
ext2_gd_b_bitmap_csum_verify(struct m_ext2fs *fs, int cg, struct buf *bp)
{
	uint32_t hi, provided, calculated, size;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return (0);

	size = fs->e2fs_fpg / 8;
	provided = fs->e2fs_gd[cg].ext4bgd_b_bmap_csum;
	calculated = calculate_crc32c(fs->e2fs_csum_seed, bp->b_data, size);
	if (fs->e2fs->e3fs_desc_size >= EXT2_BG_BLOCK_BITMAP_CSUM_HI_LOCATION) {
		hi = fs->e2fs_gd[cg].ext4bgd_b_bmap_csum_hi;
		provided |= (hi << 16);
	} else
		calculated &= 0xFFFF;

	if (provided != calculated) {
		printf("WARNING: bad block bitmap csum detected, "
		    "cg=%d - run fsck\n", cg);
		return (EIO);
	}

	return (0);
}

void
ext2_gd_b_bitmap_csum_set(struct m_ext2fs *fs, int cg, struct buf *bp)
{
	uint32_t csum, size;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return;

	size = fs->e2fs_fpg / 8;
	csum = calculate_crc32c(fs->e2fs_csum_seed, bp->b_data, size);
	fs->e2fs_gd[cg].ext4bgd_b_bmap_csum = csum & 0xFFFF;
	if (fs->e2fs->e3fs_desc_size >= EXT2_BG_BLOCK_BITMAP_CSUM_HI_LOCATION)
		fs->e2fs_gd[cg].ext4bgd_b_bmap_csum_hi = csum >> 16;
}

static uint32_t
ext2_ei_csum(struct inode *ip, struct ext2fs_dinode *ei)
{
	struct m_ext2fs *fs;
	uint32_t inode_csum_seed, inum, gen, crc;
	uint16_t dummy_csum = 0;
	unsigned int offset, csum_size;

	fs = ip->i_e2fs;
	offset = offsetof(struct ext2fs_dinode, e2di_chksum_lo);
	csum_size = sizeof(dummy_csum);
	inum = ip->i_number;
	crc = calculate_crc32c(fs->e2fs_csum_seed,
	    (uint8_t *)&inum, sizeof(inum));
	gen = ip->i_gen;
	inode_csum_seed = calculate_crc32c(crc,
	    (uint8_t *)&gen, sizeof(gen));

	crc = calculate_crc32c(inode_csum_seed, (uint8_t *)ei, offset);
	crc = calculate_crc32c(crc, (uint8_t *)&dummy_csum, csum_size);
	offset += csum_size;
	crc = calculate_crc32c(crc, (uint8_t *)ei + offset,
	    E2FS_REV0_INODE_SIZE - offset);

	if (EXT2_INODE_SIZE(fs) > E2FS_REV0_INODE_SIZE) {
		offset = offsetof(struct ext2fs_dinode, e2di_chksum_hi);
		crc = calculate_crc32c(crc, (uint8_t *)ei +
		    E2FS_REV0_INODE_SIZE, offset - E2FS_REV0_INODE_SIZE);

		if ((EXT2_INODE_SIZE(ip->i_e2fs) > E2FS_REV0_INODE_SIZE &&
		    ei->e2di_extra_isize >= EXT2_INODE_CSUM_HI_EXTRA_END)) {
			crc = calculate_crc32c(crc, (uint8_t *)&dummy_csum,
			    csum_size);
			offset += csum_size;
		}

		crc = calculate_crc32c(crc, (uint8_t *)ei + offset,
		    EXT2_INODE_SIZE(fs) - offset);
	}

	return (crc);
}

int
ext2_ei_csum_verify(struct inode *ip, struct ext2fs_dinode *ei)
{
	struct m_ext2fs *fs;
	const static struct ext2fs_dinode ei_zero;
	uint32_t hi, provided, calculated;

	fs = ip->i_e2fs;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return (0);

	provided = ei->e2di_chksum_lo;
	calculated = ext2_ei_csum(ip, ei);

	if ((EXT2_INODE_SIZE(fs) > E2FS_REV0_INODE_SIZE &&
	    ei->e2di_extra_isize >= EXT2_INODE_CSUM_HI_EXTRA_END)) {
		hi = ei->e2di_chksum_hi;
		provided |= hi << 16;
	} else
		calculated &= 0xFFFF;

	if (provided != calculated) {
		/*
		 * If it is first time used dinode,
		 * it is expected that it will be zeroed
		 * and we will not return checksum error in this case.
		 */
		if (!memcmp(ei, &ei_zero, sizeof(struct ext2fs_dinode)))
			return (0);

		printf("WARNING: Bad inode %ju csum - run fsck\n", ip->i_number);

		return (EIO);
	}

	return (0);
}

void
ext2_ei_csum_set(struct inode *ip, struct ext2fs_dinode *ei)
{
	struct m_ext2fs *fs;
	uint32_t crc;

	fs = ip->i_e2fs;

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		return;

	crc = ext2_ei_csum(ip, ei);

	ei->e2di_chksum_lo = crc & 0xFFFF;
	if ((EXT2_INODE_SIZE(fs) > E2FS_REV0_INODE_SIZE &&
	    ei->e2di_extra_isize >= EXT2_INODE_CSUM_HI_EXTRA_END))
		ei->e2di_chksum_hi = crc >> 16;
}

static uint16_t
ext2_crc16(uint16_t crc, const void *buffer, unsigned int len)
{
	const unsigned char *cp = buffer;
	/* CRC table for the CRC-16. The poly is 0x8005 (x16 + x15 + x2 + 1). */
	static uint16_t const crc16_table[256] = {
		0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
		0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
		0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
		0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
		0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
		0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
		0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
		0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
		0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
		0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
		0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
		0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
		0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
		0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
		0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
		0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
		0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
		0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
		0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
		0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
		0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
		0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
		0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
		0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
		0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
		0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
		0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
		0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
		0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
		0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
		0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
		0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
	};

	while (len--)
		crc = (((crc >> 8) & 0xffU) ^
		    crc16_table[(crc ^ *cp++) & 0xffU]) & 0x0000ffffU;
	return crc;
}

static uint16_t
ext2_gd_csum(struct m_ext2fs *fs, uint32_t block_group, struct ext2_gd *gd)
{
	size_t offset;
	uint32_t csum32;
	uint16_t crc, dummy_csum;

	offset = offsetof(struct ext2_gd, ext4bgd_csum);

	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		csum32 = calculate_crc32c(fs->e2fs_csum_seed,
		    (uint8_t *)&block_group, sizeof(block_group));
		csum32 = calculate_crc32c(csum32, (uint8_t *)gd, offset);
		dummy_csum = 0;
		csum32 = calculate_crc32c(csum32, (uint8_t *)&dummy_csum,
		    sizeof(dummy_csum));
		offset += sizeof(dummy_csum);
		if (offset < fs->e2fs->e3fs_desc_size)
			csum32 = calculate_crc32c(csum32, (uint8_t *)gd + offset,
			    fs->e2fs->e3fs_desc_size - offset);

		crc = csum32 & 0xFFFF;
		return (crc);
	} else if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_GDT_CSUM)) {
		crc = ext2_crc16(~0, fs->e2fs->e2fs_uuid,
		    sizeof(fs->e2fs->e2fs_uuid));
		crc = ext2_crc16(crc, (uint8_t *)&block_group,
		    sizeof(block_group));
		crc = ext2_crc16(crc, (uint8_t *)gd, offset);
		offset += sizeof(gd->ext4bgd_csum); /* skip checksum */
		if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_64BIT) &&
		    offset < fs->e2fs->e3fs_desc_size)
			crc = ext2_crc16(crc, (uint8_t *)gd + offset,
			    fs->e2fs->e3fs_desc_size - offset);
		return (crc);
	}

	return (0);
}

int
ext2_gd_csum_verify(struct m_ext2fs *fs, struct cdev *dev)
{
	unsigned int i;
	int error = 0;

	for (i = 0; i < fs->e2fs_gcount; i++) {
		if (fs->e2fs_gd[i].ext4bgd_csum !=
		    ext2_gd_csum(fs, i, &fs->e2fs_gd[i])) {
			printf(
"WARNING: mount of %s denied due bad gd=%d csum=0x%x, expected=0x%x - run fsck\n",
			    devtoname(dev), i, fs->e2fs_gd[i].ext4bgd_csum,
			    ext2_gd_csum(fs, i, &fs->e2fs_gd[i]));
			error = EIO;
			break;
		}
	}

	return (error);
}

void
ext2_gd_csum_set(struct m_ext2fs *fs)
{
	unsigned int i;

	for (i = 0; i < fs->e2fs_gcount; i++)
		    fs->e2fs_gd[i].ext4bgd_csum = 
			ext2_gd_csum(fs, i, &fs->e2fs_gd[i]);
}
