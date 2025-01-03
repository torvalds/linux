// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Oracle.
 * All Rights Reserved.
 */
#ifndef __XFS_ONDISK_H
#define __XFS_ONDISK_H

#define XFS_CHECK_STRUCT_SIZE(structname, size) \
	static_assert(sizeof(structname) == (size), \
		"XFS: sizeof(" #structname ") is wrong, expected " #size)

#define XFS_CHECK_OFFSET(structname, member, off) \
	static_assert(offsetof(structname, member) == (off), \
		"XFS: offsetof(" #structname ", " #member ") is wrong, " \
		"expected " #off)

#define XFS_CHECK_VALUE(value, expected) \
	static_assert((value) == (expected), \
		"XFS: value of " #value " is wrong, expected " #expected)

#define XFS_CHECK_SB_OFFSET(field, offset) \
	XFS_CHECK_OFFSET(struct xfs_dsb, field, offset); \
	XFS_CHECK_OFFSET(struct xfs_sb, field, offset);

static inline void __init
xfs_check_ondisk_structs(void)
{
	/* file structures */
	XFS_CHECK_STRUCT_SIZE(struct xfs_acl,			4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_acl_entry,		12);
	XFS_CHECK_STRUCT_SIZE(struct xfs_bmbt_key,		8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_bmbt_rec,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_bmdr_block,		4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dinode,		176);
	XFS_CHECK_STRUCT_SIZE(struct xfs_disk_dquot,		104);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dqblk,			136);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dsymlink_hdr,		56);
	XFS_CHECK_STRUCT_SIZE(xfs_timestamp_t,			8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_legacy_timestamp,	8);

	/* space btrees */
	XFS_CHECK_STRUCT_SIZE(struct xfs_agf,			224);
	XFS_CHECK_STRUCT_SIZE(struct xfs_agfl,			36);
	XFS_CHECK_STRUCT_SIZE(struct xfs_agi,			344);
	XFS_CHECK_STRUCT_SIZE(struct xfs_alloc_rec,		8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_btree_block,		72);
	XFS_CHECK_STRUCT_SIZE(struct xfs_btree_block_lhdr,	64);
	XFS_CHECK_STRUCT_SIZE(struct xfs_btree_block_shdr,	48);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inobt_key,		4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inobt_rec,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_refcount_key,		4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_refcount_rec,		12);
	XFS_CHECK_STRUCT_SIZE(struct xfs_rmap_key,		20);
	XFS_CHECK_STRUCT_SIZE(struct xfs_rmap_rec,		24);
	XFS_CHECK_STRUCT_SIZE(xfs_alloc_key_t,			8);
	XFS_CHECK_STRUCT_SIZE(xfs_alloc_ptr_t,			4);
	XFS_CHECK_STRUCT_SIZE(xfs_inobt_ptr_t,			4);
	XFS_CHECK_STRUCT_SIZE(xfs_refcount_ptr_t,		4);
	XFS_CHECK_STRUCT_SIZE(xfs_rmap_ptr_t,			4);
	XFS_CHECK_STRUCT_SIZE(xfs_bmdr_key_t,			8);

	/* dir/attr trees */
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr3_leaf_hdr,	80);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr3_leafblock,	80);
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
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr_leaf_entry,		8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr_leaf_hdr,		32);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr_leaf_map,		4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr_leaf_name_local,	4);

	/* realtime structures */
	XFS_CHECK_STRUCT_SIZE(struct xfs_rtsb,			56);
	XFS_CHECK_STRUCT_SIZE(union xfs_rtword_raw,		4);
	XFS_CHECK_STRUCT_SIZE(union xfs_suminfo_raw,		4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_rtbuf_blkinfo,		48);

	/*
	 * m68k has problems with struct xfs_attr_leaf_name_remote, but we pad
	 * it to 4 bytes anyway so it's not obviously a problem.  Hence for the
	 * moment we don't check this structure. This can be re-instated when
	 * the attr definitions are updated to use c99 VLA definitions.
	 *
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr_leaf_name_remote,	12);
	 */

	XFS_CHECK_OFFSET(struct xfs_attr_leaf_name_local, valuelen,	0);
	XFS_CHECK_OFFSET(struct xfs_attr_leaf_name_local, namelen,	2);
	XFS_CHECK_OFFSET(struct xfs_attr_leaf_name_local, nameval,	3);
	XFS_CHECK_OFFSET(struct xfs_attr_leaf_name_remote, valueblk,	0);
	XFS_CHECK_OFFSET(struct xfs_attr_leaf_name_remote, valuelen,	4);
	XFS_CHECK_OFFSET(struct xfs_attr_leaf_name_remote, namelen,	8);
	XFS_CHECK_OFFSET(struct xfs_attr_leaf_name_remote, name,	9);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr_leafblock,		32);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr_sf_hdr,		4);
	XFS_CHECK_OFFSET(struct xfs_attr_sf_hdr, totsize,	0);
	XFS_CHECK_OFFSET(struct xfs_attr_sf_hdr, count,		2);
	XFS_CHECK_OFFSET(struct xfs_attr_sf_entry, namelen,	0);
	XFS_CHECK_OFFSET(struct xfs_attr_sf_entry, valuelen,	1);
	XFS_CHECK_OFFSET(struct xfs_attr_sf_entry, flags,	2);
	XFS_CHECK_OFFSET(struct xfs_attr_sf_entry, nameval,	3);
	XFS_CHECK_STRUCT_SIZE(struct xfs_da_blkinfo,		12);
	XFS_CHECK_STRUCT_SIZE(struct xfs_da_intnode,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_da_node_entry,		8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_da_node_hdr,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_data_free,		4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_data_hdr,		16);
	XFS_CHECK_OFFSET(struct xfs_dir2_data_unused, freetag,	0);
	XFS_CHECK_OFFSET(struct xfs_dir2_data_unused, length,	2);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_free_hdr,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_free,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_leaf_entry,	8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_leaf_hdr,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_leaf,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_leaf_tail,	4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_sf_entry,		3);
	XFS_CHECK_OFFSET(struct xfs_dir2_sf_entry, namelen,	0);
	XFS_CHECK_OFFSET(struct xfs_dir2_sf_entry, offset,	1);
	XFS_CHECK_OFFSET(struct xfs_dir2_sf_entry, name,	3);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_sf_hdr,		10);
	XFS_CHECK_STRUCT_SIZE(struct xfs_parent_rec,		12);

	/* ondisk dir/attr structures from xfs/122 */
	XFS_CHECK_STRUCT_SIZE(struct xfs_attr_sf_entry,		3);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_data_free,	4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_data_hdr,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_data_unused,	6);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_free,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_free_hdr,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_leaf,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_leaf_entry,	8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_leaf_hdr,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_leaf_tail,	4);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_sf_entry,		3);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dir2_sf_hdr,		10);

	/* log structures */
	XFS_CHECK_STRUCT_SIZE(struct xfs_buf_log_format,	88);
	XFS_CHECK_STRUCT_SIZE(struct xfs_dq_logformat,		24);
	XFS_CHECK_STRUCT_SIZE(struct xfs_efd_log_format_32,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_efd_log_format_64,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_efi_log_format_32,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_efi_log_format_64,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_extent_32,		12);
	XFS_CHECK_STRUCT_SIZE(struct xfs_extent_64,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_log_dinode,		176);
	XFS_CHECK_STRUCT_SIZE(struct xfs_icreate_log,		28);
	XFS_CHECK_STRUCT_SIZE(xfs_log_timestamp_t,		8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_log_legacy_timestamp,	8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inode_log_format_32,	52);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inode_log_format,	56);
	XFS_CHECK_STRUCT_SIZE(struct xfs_qoff_logformat,	20);
	XFS_CHECK_STRUCT_SIZE(struct xfs_trans_header,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attri_log_format,	40);
	XFS_CHECK_STRUCT_SIZE(struct xfs_attrd_log_format,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_bui_log_format,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_bud_log_format,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_cui_log_format,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_cud_log_format,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_rui_log_format,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_rud_log_format,	16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_map_extent,		32);
	XFS_CHECK_STRUCT_SIZE(struct xfs_phys_extent,		16);

	XFS_CHECK_OFFSET(struct xfs_bui_log_format, bui_extents,	16);
	XFS_CHECK_OFFSET(struct xfs_cui_log_format, cui_extents,	16);
	XFS_CHECK_OFFSET(struct xfs_rui_log_format, rui_extents,	16);
	XFS_CHECK_OFFSET(struct xfs_efi_log_format, efi_extents,	16);
	XFS_CHECK_OFFSET(struct xfs_efi_log_format_32, efi_extents,	16);
	XFS_CHECK_OFFSET(struct xfs_efi_log_format_64, efi_extents,	16);

	/* ondisk log structures from xfs/122 */
	XFS_CHECK_STRUCT_SIZE(struct xfs_unmount_log_format,		8);
	XFS_CHECK_STRUCT_SIZE(struct xfs_xmd_log_format,		16);
	XFS_CHECK_STRUCT_SIZE(struct xfs_xmi_log_format,		88);

	/* parent pointer ioctls */
	XFS_CHECK_STRUCT_SIZE(struct xfs_getparents_rec,	32);
	XFS_CHECK_STRUCT_SIZE(struct xfs_getparents,		40);
	XFS_CHECK_STRUCT_SIZE(struct xfs_getparents_by_handle,	64);

	/*
	 * The v5 superblock format extended several v4 header structures with
	 * additional data. While new fields are only accessible on v5
	 * superblocks, it's important that the v5 structures place original v4
	 * fields/headers in the correct location on-disk. For example, we must
	 * be able to find magic values at the same location in certain blocks
	 * regardless of superblock version.
	 *
	 * The following checks ensure that various v5 data structures place the
	 * subset of v4 metadata associated with the same type of block at the
	 * start of the on-disk block. If there is no data structure definition
	 * for certain types of v4 blocks, traverse down to the first field of
	 * common metadata (e.g., magic value) and make sure it is at offset
	 * zero.
	 */
	XFS_CHECK_OFFSET(struct xfs_dir3_leaf, hdr.info.hdr,	0);
	XFS_CHECK_OFFSET(struct xfs_da3_intnode, hdr.info.hdr,	0);
	XFS_CHECK_OFFSET(struct xfs_dir3_data_hdr, hdr.magic,	0);
	XFS_CHECK_OFFSET(struct xfs_dir3_free, hdr.hdr.magic,	0);
	XFS_CHECK_OFFSET(struct xfs_attr3_leafblock, hdr.info.hdr, 0);

	XFS_CHECK_STRUCT_SIZE(struct xfs_bulkstat,		192);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inumbers,		24);
	XFS_CHECK_STRUCT_SIZE(struct xfs_bulkstat_req,		64);
	XFS_CHECK_STRUCT_SIZE(struct xfs_inumbers_req,		64);

	/*
	 * Make sure the incore inode timestamp range corresponds to hand
	 * converted values based on the ondisk format specification.
	 */
	XFS_CHECK_VALUE(XFS_BIGTIME_TIME_MIN - XFS_BIGTIME_EPOCH_OFFSET,
			XFS_LEGACY_TIME_MIN);
	XFS_CHECK_VALUE(XFS_BIGTIME_TIME_MAX - XFS_BIGTIME_EPOCH_OFFSET,
			16299260424LL);

	/* Do the same with the incore quota expiration range. */
	XFS_CHECK_VALUE(XFS_DQ_BIGTIME_EXPIRY_MIN << XFS_DQ_BIGTIME_SHIFT, 4);
	XFS_CHECK_VALUE(XFS_DQ_BIGTIME_EXPIRY_MAX << XFS_DQ_BIGTIME_SHIFT,
			16299260424LL);

	/* superblock field checks we got from xfs/122 */
	XFS_CHECK_STRUCT_SIZE(struct xfs_dsb,		288);
	XFS_CHECK_STRUCT_SIZE(struct xfs_sb,		288);
	XFS_CHECK_SB_OFFSET(sb_magicnum,		0);
	XFS_CHECK_SB_OFFSET(sb_blocksize,		4);
	XFS_CHECK_SB_OFFSET(sb_dblocks,			8);
	XFS_CHECK_SB_OFFSET(sb_rblocks,			16);
	XFS_CHECK_SB_OFFSET(sb_rextents,		24);
	XFS_CHECK_SB_OFFSET(sb_uuid,			32);
	XFS_CHECK_SB_OFFSET(sb_logstart,		48);
	XFS_CHECK_SB_OFFSET(sb_rootino,			56);
	XFS_CHECK_SB_OFFSET(sb_rbmino,			64);
	XFS_CHECK_SB_OFFSET(sb_rsumino,			72);
	XFS_CHECK_SB_OFFSET(sb_rextsize,		80);
	XFS_CHECK_SB_OFFSET(sb_agblocks,		84);
	XFS_CHECK_SB_OFFSET(sb_agcount,			88);
	XFS_CHECK_SB_OFFSET(sb_rbmblocks,		92);
	XFS_CHECK_SB_OFFSET(sb_logblocks,		96);
	XFS_CHECK_SB_OFFSET(sb_versionnum,		100);
	XFS_CHECK_SB_OFFSET(sb_sectsize,		102);
	XFS_CHECK_SB_OFFSET(sb_inodesize,		104);
	XFS_CHECK_SB_OFFSET(sb_inopblock,		106);
	XFS_CHECK_SB_OFFSET(sb_blocklog,		120);
	XFS_CHECK_SB_OFFSET(sb_fname[12],		120);
	XFS_CHECK_SB_OFFSET(sb_sectlog,			121);
	XFS_CHECK_SB_OFFSET(sb_inodelog,		122);
	XFS_CHECK_SB_OFFSET(sb_inopblog,		123);
	XFS_CHECK_SB_OFFSET(sb_agblklog,		124);
	XFS_CHECK_SB_OFFSET(sb_rextslog,		125);
	XFS_CHECK_SB_OFFSET(sb_inprogress,		126);
	XFS_CHECK_SB_OFFSET(sb_imax_pct,		127);
	XFS_CHECK_SB_OFFSET(sb_icount,			128);
	XFS_CHECK_SB_OFFSET(sb_ifree,			136);
	XFS_CHECK_SB_OFFSET(sb_fdblocks,		144);
	XFS_CHECK_SB_OFFSET(sb_frextents,		152);
	XFS_CHECK_SB_OFFSET(sb_uquotino,		160);
	XFS_CHECK_SB_OFFSET(sb_gquotino,		168);
	XFS_CHECK_SB_OFFSET(sb_qflags,			176);
	XFS_CHECK_SB_OFFSET(sb_flags,			178);
	XFS_CHECK_SB_OFFSET(sb_shared_vn,		179);
	XFS_CHECK_SB_OFFSET(sb_inoalignmt,		180);
	XFS_CHECK_SB_OFFSET(sb_unit,			184);
	XFS_CHECK_SB_OFFSET(sb_width,			188);
	XFS_CHECK_SB_OFFSET(sb_dirblklog,		192);
	XFS_CHECK_SB_OFFSET(sb_logsectlog,		193);
	XFS_CHECK_SB_OFFSET(sb_logsectsize,		194);
	XFS_CHECK_SB_OFFSET(sb_logsunit,		196);
	XFS_CHECK_SB_OFFSET(sb_features2,		200);
	XFS_CHECK_SB_OFFSET(sb_bad_features2,		204);
	XFS_CHECK_SB_OFFSET(sb_features_compat,		208);
	XFS_CHECK_SB_OFFSET(sb_features_ro_compat,	212);
	XFS_CHECK_SB_OFFSET(sb_features_incompat,	216);
	XFS_CHECK_SB_OFFSET(sb_features_log_incompat,	220);
	XFS_CHECK_SB_OFFSET(sb_crc,			224);
	XFS_CHECK_SB_OFFSET(sb_spino_align,		228);
	XFS_CHECK_SB_OFFSET(sb_pquotino,		232);
	XFS_CHECK_SB_OFFSET(sb_lsn,			240);
	XFS_CHECK_SB_OFFSET(sb_meta_uuid,		248);
	XFS_CHECK_SB_OFFSET(sb_metadirino,		264);
	XFS_CHECK_SB_OFFSET(sb_rgcount,			272);
	XFS_CHECK_SB_OFFSET(sb_rgextents,		276);
	XFS_CHECK_SB_OFFSET(sb_rgblklog,		280);
	XFS_CHECK_SB_OFFSET(sb_pad,			281);
}

#endif /* __XFS_ONDISK_H */
