/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/fs/ufs/swab.h
 *
 * Copyright (C) 1997, 1998 Francois-Rene Rideau <fare@tunes.org>
 * Copyright (C) 1998 Jakub Jelinek <jj@ultra.linux.cz>
 * Copyright (C) 2001 Christoph Hellwig <hch@infradead.org>
 */

#ifndef _UFS_SWAB_H
#define _UFS_SWAB_H

/*
 * Notes:
 *    HERE WE ASSUME EITHER BIG OR LITTLE ENDIAN UFSes
 *    in case there are ufs implementations that have strange bytesexes,
 *    you'll need to modify code here as well as in ufs_super.c and ufs_fs.h
 *    to support them.
 */

enum {
	BYTESEX_LE,
	BYTESEX_BE
};

static inline u64
fs64_to_cpu(struct super_block *sbp, __fs64 n)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		return le64_to_cpu((__force __le64)n);
	else
		return be64_to_cpu((__force __be64)n);
}

static inline __fs64
cpu_to_fs64(struct super_block *sbp, u64 n)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		return (__force __fs64)cpu_to_le64(n);
	else
		return (__force __fs64)cpu_to_be64(n);
}

static inline u32
fs32_to_cpu(struct super_block *sbp, __fs32 n)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		return le32_to_cpu((__force __le32)n);
	else
		return be32_to_cpu((__force __be32)n);
}

static inline __fs32
cpu_to_fs32(struct super_block *sbp, u32 n)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		return (__force __fs32)cpu_to_le32(n);
	else
		return (__force __fs32)cpu_to_be32(n);
}

static inline void
fs32_add(struct super_block *sbp, __fs32 *n, int d)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		le32_add_cpu((__le32 *)n, d);
	else
		be32_add_cpu((__be32 *)n, d);
}

static inline void
fs32_sub(struct super_block *sbp, __fs32 *n, int d)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		le32_add_cpu((__le32 *)n, -d);
	else
		be32_add_cpu((__be32 *)n, -d);
}

static inline u16
fs16_to_cpu(struct super_block *sbp, __fs16 n)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		return le16_to_cpu((__force __le16)n);
	else
		return be16_to_cpu((__force __be16)n);
}

static inline __fs16
cpu_to_fs16(struct super_block *sbp, u16 n)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		return (__force __fs16)cpu_to_le16(n);
	else
		return (__force __fs16)cpu_to_be16(n);
}

static inline void
fs16_add(struct super_block *sbp, __fs16 *n, int d)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		le16_add_cpu((__le16 *)n, d);
	else
		be16_add_cpu((__be16 *)n, d);
}

static inline void
fs16_sub(struct super_block *sbp, __fs16 *n, int d)
{
	if (UFS_SB(sbp)->s_bytesex == BYTESEX_LE)
		le16_add_cpu((__le16 *)n, -d);
	else
		be16_add_cpu((__be16 *)n, -d);
}

#endif /* _UFS_SWAB_H */
