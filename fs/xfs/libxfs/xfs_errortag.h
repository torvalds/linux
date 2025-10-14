/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * Copyright (C) 2017 Oracle.
 * All Rights Reserved.
 */
#if !defined(__XFS_ERRORTAG_H_) || defined(XFS_ERRTAG)
#define __XFS_ERRORTAG_H_

/*
 * There are two ways to use this header file.  The first way is to #include it
 * bare, which will define all the XFS_ERRTAG_* error injection knobs for use
 * with the XFS_TEST_ERROR macro.  The second way is to enclose the #include
 * with a #define for an XFS_ERRTAG macro, in which case the header will define
 " an XFS_ERRTAGS macro that expands to invoke that XFS_ERRTAG macro for each
 * defined error injection knob.
 */

/*
 * These are the actual error injection tags.  The numbers should be consecutive
 * because arrays are sized based on the maximum.
 */
#define XFS_ERRTAG_NOERROR				0
#define XFS_ERRTAG_IFLUSH_1				1
#define XFS_ERRTAG_IFLUSH_2				2
#define XFS_ERRTAG_IFLUSH_3				3
#define XFS_ERRTAG_IFLUSH_4				4
#define XFS_ERRTAG_IFLUSH_5				5
#define XFS_ERRTAG_IFLUSH_6				6
#define XFS_ERRTAG_DA_READ_BUF				7
#define XFS_ERRTAG_BTREE_CHECK_LBLOCK			8
#define XFS_ERRTAG_BTREE_CHECK_SBLOCK			9
#define XFS_ERRTAG_ALLOC_READ_AGF			10
#define XFS_ERRTAG_IALLOC_READ_AGI			11
#define XFS_ERRTAG_ITOBP_INOTOBP			12
#define XFS_ERRTAG_IUNLINK				13
#define XFS_ERRTAG_IUNLINK_REMOVE			14
#define XFS_ERRTAG_DIR_INO_VALIDATE			15
#define XFS_ERRTAG_BULKSTAT_READ_CHUNK			16
#define XFS_ERRTAG_IODONE_IOERR				17
#define XFS_ERRTAG_STRATREAD_IOERR			18
#define XFS_ERRTAG_STRATCMPL_IOERR			19
#define XFS_ERRTAG_DIOWRITE_IOERR			20
#define XFS_ERRTAG_BMAPIFORMAT				21
#define XFS_ERRTAG_FREE_EXTENT				22
#define XFS_ERRTAG_RMAP_FINISH_ONE			23
#define XFS_ERRTAG_REFCOUNT_CONTINUE_UPDATE		24
#define XFS_ERRTAG_REFCOUNT_FINISH_ONE			25
#define XFS_ERRTAG_BMAP_FINISH_ONE			26
#define XFS_ERRTAG_AG_RESV_CRITICAL			27

/*
 * Drop-writes support removed because write error handling cannot trash
 * pre-existing delalloc extents in any useful way anymore. We retain the
 * definition so that we can reject it as an invalid value in
 * xfs_errortag_valid().
 */
#define XFS_ERRTAG_DROP_WRITES				28
#define XFS_ERRTAG_LOG_BAD_CRC				29
#define XFS_ERRTAG_LOG_ITEM_PIN				30
#define XFS_ERRTAG_BUF_LRU_REF				31
#define XFS_ERRTAG_FORCE_SCRUB_REPAIR			32
#define XFS_ERRTAG_FORCE_SUMMARY_RECALC			33
#define XFS_ERRTAG_IUNLINK_FALLBACK			34
#define XFS_ERRTAG_BUF_IOERROR				35
#define XFS_ERRTAG_REDUCE_MAX_IEXTENTS			36
#define XFS_ERRTAG_BMAP_ALLOC_MINLEN_EXTENT		37
#define XFS_ERRTAG_AG_RESV_FAIL				38
#define XFS_ERRTAG_LARP					39
#define XFS_ERRTAG_DA_LEAF_SPLIT			40
#define XFS_ERRTAG_ATTR_LEAF_TO_NODE			41
#define XFS_ERRTAG_WB_DELAY_MS				42
#define XFS_ERRTAG_WRITE_DELAY_MS			43
#define XFS_ERRTAG_EXCHMAPS_FINISH_ONE			44
#define XFS_ERRTAG_METAFILE_RESV_CRITICAL		45
#define XFS_ERRTAG_MAX					46

/*
 * Random factors for above tags, 1 means always, 2 means 1/2 time, etc.
 */
#define XFS_RANDOM_DEFAULT				100

/*
 * Table of errror injection knobs.  The parameters to the XFS_ERRTAG macro are:
 *   1. The XFS_ERRTAG_ flag but without the prefix;
 *   2. The name of the sysfs knob; and
 *   3. The default value for the knob.
 */
#ifdef XFS_ERRTAG
# undef XFS_ERRTAGS
# define XFS_ERRTAGS \
XFS_ERRTAG(NOERROR,		noerror,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IFLUSH_1,		iflush1,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IFLUSH_2,		iflush2,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IFLUSH_3,		iflush3,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IFLUSH_4,		iflush4,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IFLUSH_5,		iflush5,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IFLUSH_6,		iflush6,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(DA_READ_BUF,		dareadbuf,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(BTREE_CHECK_LBLOCK,	btree_chk_lblk,		XFS_RANDOM_DEFAULT/4) \
XFS_ERRTAG(BTREE_CHECK_SBLOCK,	btree_chk_sblk,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(ALLOC_READ_AGF,	readagf,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IALLOC_READ_AGI,	readagi,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(ITOBP_INOTOBP,	itobp,			XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IUNLINK,		iunlink,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IUNLINK_REMOVE,	iunlinkrm,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(DIR_INO_VALIDATE,	dirinovalid,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(BULKSTAT_READ_CHUNK,	bulkstat,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(IODONE_IOERR,	logiodone,		XFS_RANDOM_DEFAULT/10) \
XFS_ERRTAG(STRATREAD_IOERR,	stratread,		XFS_RANDOM_DEFAULT/10) \
XFS_ERRTAG(STRATCMPL_IOERR,	stratcmpl,		XFS_RANDOM_DEFAULT/10) \
XFS_ERRTAG(DIOWRITE_IOERR,	diowrite,		XFS_RANDOM_DEFAULT/10) \
XFS_ERRTAG(BMAPIFORMAT,		bmapifmt,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(FREE_EXTENT,		free_extent,		1) \
XFS_ERRTAG(RMAP_FINISH_ONE,	rmap_finish_one,	1) \
XFS_ERRTAG(REFCOUNT_CONTINUE_UPDATE, refcount_continue_update, 1) \
XFS_ERRTAG(REFCOUNT_FINISH_ONE,	refcount_finish_one,	1) \
XFS_ERRTAG(BMAP_FINISH_ONE,	bmap_finish_one,	1) \
XFS_ERRTAG(AG_RESV_CRITICAL,	ag_resv_critical,	4) \
XFS_ERRTAG(LOG_BAD_CRC,		log_bad_crc,		1) \
XFS_ERRTAG(LOG_ITEM_PIN,	log_item_pin,		1) \
XFS_ERRTAG(BUF_LRU_REF,		buf_lru_ref,		2) \
XFS_ERRTAG(FORCE_SCRUB_REPAIR,	force_repair,		1) \
XFS_ERRTAG(FORCE_SUMMARY_RECALC, bad_summary,		1) \
XFS_ERRTAG(IUNLINK_FALLBACK,	iunlink_fallback,	XFS_RANDOM_DEFAULT/10) \
XFS_ERRTAG(BUF_IOERROR,		buf_ioerror,		XFS_RANDOM_DEFAULT) \
XFS_ERRTAG(REDUCE_MAX_IEXTENTS,	reduce_max_iextents,	1) \
XFS_ERRTAG(BMAP_ALLOC_MINLEN_EXTENT, bmap_alloc_minlen_extent, 1) \
XFS_ERRTAG(AG_RESV_FAIL,	ag_resv_fail,		1) \
XFS_ERRTAG(LARP,		larp,			1) \
XFS_ERRTAG(DA_LEAF_SPLIT,	da_leaf_split,		1) \
XFS_ERRTAG(ATTR_LEAF_TO_NODE,	attr_leaf_to_node,	1) \
XFS_ERRTAG(WB_DELAY_MS,		wb_delay_ms,		3000) \
XFS_ERRTAG(WRITE_DELAY_MS,	write_delay_ms,		3000) \
XFS_ERRTAG(EXCHMAPS_FINISH_ONE,	exchmaps_finish_one,	1) \
XFS_ERRTAG(METAFILE_RESV_CRITICAL, metafile_resv_crit,	4)
#endif /* XFS_ERRTAG */

#endif /* __XFS_ERRORTAG_H_ */
