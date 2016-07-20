/*
 * Copyright (c) 2016 Oracle.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_ONDISK_H
#define __XFS_ONDISK_H

#define XFS_CHECK_STRUCT_SIZE(structname, size) \
	BUILD_BUG_ON_MSG(sizeof(structname) != (size), "XFS: sizeof(" \
		#structname ") is wrong, expected " #size)

static inline void __init
xfs_check_ondisk_structs(void)
{
	/* ag/file structures */
	XFS_CHECK_STRUCT_SIZE(struct xfs_acl,			4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_acl_entry,		12);
	XFS_CHECK_STRUCT_SIZE(struct xfs_agf,			224);
	XFS_CHECK_STRUCT_SIZE(struct xfs_agfl,			36);
	XFS_CHECK_STRUCT_SIZE(struct xfs_agi,			336);
	XFS_CHECK_STRUCT_SIZE(struct xfs_bmbt_key,		8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_bmbt_rec,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_bmdr_block,		4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_btree_block,		72);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dinode,		176);
	XFS_CHECK_STRUCT_SIZE(struct xfs_disk_dquot,		104);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dqblk,			136);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dsb,			264);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dsymlink_hdr,		56);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inobt_key,		4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inobt_rec,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_timestamp,		8);
	XFS_CHECK_STRUCT_SIZE(xfs_alloc_key_t,			8);
	XFS_CHECK_STRUCT_SIZE(xfs_alloc_ptr_t,			4);
	XFS_CHECK_STRUCT_SIZE(xfs_alloc_rec_t,			8);
	XFS_CHECK_STRUCT_SIZE(xfs_inobt_ptr_t,			4);

	/* dir/attr trees */
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr3_leaf_hdr,	80);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr3_leafblock,	88);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr3_rmt_hdr,		56);
	XFS_CHECK_STRUCT_SIZE(struct xfs_da3_blkinfo,		56);
	XFS_CHECK_STRUCT_SIZE(struct xfs_da3_intnode,		64);
	XFS_CHECK_STRUCT_SIZE(struct xfs_da3_node_hdr,		64);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir3_blk_hdr,		48);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir3_data_hdr,		64);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir3_free,		64);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir3_free_hdr,		64);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir3_leaf,		64);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir3_leaf_hdr,		64);
	XFS_CHECK_STRUCT_SIZE(xfs_attr_leaf_entry_t,		8);
	XFS_CHECK_STRUCT_SIZE(xfs_attr_leaf_hdr_t,		32);
	XFS_CHECK_STRUCT_SIZE(xfs_attr_leaf_map_t,		4);
	XFS_CHECK_STRUCT_SIZE(xfs_attr_leaf_name_local_t,	4);

	/*
	 * m68k has problems with xfs_attr_leaf_name_remote_t, but we pad it to
	 * 4 bytes anyway so it's not obviously a problem.  Hence for the moment
	 * we don't check this structure. This can be re-instated when the attr
	 * definitions are updated to use c99 VLA definitions.
	 *
	XFS_CHECK_STRUCT_SIZE(xfs_attr_leaf_name_remote_t,	12);
	 */

	XFS_CHECK_STRUCT_SIZE(xfs_attr_leafblock_t,		40);
	XFS_CHECK_STRUCT_SIZE(xfs_attr_shortform_t,		8);
	XFS_CHECK_STRUCT_SIZE(xfs_da_blkinfo_t,			12);
	XFS_CHECK_STRUCT_SIZE(xfs_da_intnode_t,			16);
	XFS_CHECK_STRUCT_SIZE(xfs_da_node_entry_t,		8);
	XFS_CHECK_STRUCT_SIZE(xfs_da_node_hdr_t,		16);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_data_free_t,		4);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_data_hdr_t,		16);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_data_unused_t,		6);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_free_hdr_t,		16);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_free_t,			16);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_leaf_entry_t,		8);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_leaf_hdr_t,		16);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_leaf_t,			16);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_leaf_tail_t,		4);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_sf_entry_t,		3);
	XFS_CHECK_STRUCT_SIZE(xfs_dir2_sf_hdr_t,		10);

	/* log structures */
	XFS_CHECK_STRUCT_SIZE(struct xfs_dq_logformat,		24);
	XFS_CHECK_STRUCT_SIZE(struct xfs_efd_log_format_32,	28);
	XFS_CHECK_STRUCT_SIZE(struct xfs_efd_log_format_64,	32);
	XFS_CHECK_STRUCT_SIZE(struct xfs_efi_log_format_32,	28);
	XFS_CHECK_STRUCT_SIZE(struct xfs_efi_log_format_64,	32);
	XFS_CHECK_STRUCT_SIZE(struct xfs_extent_32,		12);
	XFS_CHECK_STRUCT_SIZE(struct xfs_extent_64,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_log_dinode,		176);
	XFS_CHECK_STRUCT_SIZE(struct xfs_icreate_log,		28);
	XFS_CHECK_STRUCT_SIZE(struct xfs_ictimestamp,		8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inode_log_format_32,	52);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inode_log_format_64,	56);
	XFS_CHECK_STRUCT_SIZE(struct xfs_qoff_logformat,	20);
	XFS_CHECK_STRUCT_SIZE(struct xfs_trans_header,		16);
}

#endif /* __XFS_ONDISK_H */
