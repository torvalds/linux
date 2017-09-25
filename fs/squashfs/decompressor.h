#ifndef DECOMPRESSOR_H
#define DECOMPRESSOR_H
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * decompressor.h
 */

struct squashfs_decompressor {
	void	*(*init)(struct squashfs_sb_info *, void *);
	void	*(*comp_opts)(struct squashfs_sb_info *, void *, int);
	void	(*free)(void *);
	int	(*decompress)(struct squashfs_sb_info *, void *,
		struct buffer_head **, int, int, int,
		struct squashfs_page_actor *);
	int	id;
	char	*name;
	int	supported;
};

static inline void *squashfs_comp_opts(struct squashfs_sb_info *msblk,
							void *buff, int length)
{
	return msblk->decompressor->comp_opts ?
		msblk->decompressor->comp_opts(msblk, buff, length) : NULL;
}

#ifdef CONFIG_SQUASHFS_XZ
extern const struct squashfs_decompressor squashfs_xz_comp_ops;
#endif

#ifdef CONFIG_SQUASHFS_LZ4
extern const struct squashfs_decompressor squashfs_lz4_comp_ops;
#endif

#ifdef CONFIG_SQUASHFS_LZO
extern const struct squashfs_decompressor squashfs_lzo_comp_ops;
#endif

#ifdef CONFIG_SQUASHFS_ZLIB
extern const struct squashfs_decompressor squashfs_zlib_comp_ops;
#endif

#ifdef CONFIG_SQUASHFS_ZSTD
extern const struct squashfs_decompressor squashfs_zstd_comp_ops;
#endif

#endif
