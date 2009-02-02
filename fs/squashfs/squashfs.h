/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@lougher.demon.co.uk>
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
 * squashfs.h
 */

#define TRACE(s, args...)	pr_debug("SQUASHFS: "s, ## args)

#define ERROR(s, args...)	pr_err("SQUASHFS error: "s, ## args)

#define WARNING(s, args...)	pr_warning("SQUASHFS: "s, ## args)

static inline struct squashfs_inode_info *squashfs_i(struct inode *inode)
{
	return list_entry(inode, struct squashfs_inode_info, vfs_inode);
}

/* block.c */
extern int squashfs_read_data(struct super_block *, void **, u64, int, u64 *,
				int);

/* cache.c */
extern struct squashfs_cache *squashfs_cache_init(char *, int, int);
extern void squashfs_cache_delete(struct squashfs_cache *);
extern struct squashfs_cache_entry *squashfs_cache_get(struct super_block *,
				struct squashfs_cache *, u64, int);
extern void squashfs_cache_put(struct squashfs_cache_entry *);
extern int squashfs_copy_data(void *, struct squashfs_cache_entry *, int, int);
extern int squashfs_read_metadata(struct super_block *, void *, u64 *,
				int *, int);
extern struct squashfs_cache_entry *squashfs_get_fragment(struct super_block *,
				u64, int);
extern struct squashfs_cache_entry *squashfs_get_datablock(struct super_block *,
				u64, int);
extern int squashfs_read_table(struct super_block *, void *, u64, int);

/* export.c */
extern __le64 *squashfs_read_inode_lookup_table(struct super_block *, u64,
				unsigned int);

/* fragment.c */
extern int squashfs_frag_lookup(struct super_block *, unsigned int, u64 *);
extern __le64 *squashfs_read_fragment_index_table(struct super_block *,
				u64, unsigned int);

/* id.c */
extern int squashfs_get_id(struct super_block *, unsigned int, unsigned int *);
extern __le64 *squashfs_read_id_index_table(struct super_block *, u64,
				unsigned short);

/* inode.c */
extern struct inode *squashfs_iget(struct super_block *, long long,
				unsigned int);
extern int squashfs_read_inode(struct inode *, long long);

/*
 * Inodes and files operations
 */

/* dir.c */
extern const struct file_operations squashfs_dir_ops;

/* export.c */
extern const struct export_operations squashfs_export_ops;

/* file.c */
extern const struct address_space_operations squashfs_aops;

/* namei.c */
extern const struct inode_operations squashfs_dir_inode_ops;

/* symlink.c */
extern const struct address_space_operations squashfs_symlink_aops;
