/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * Copyright (C) 2017 Oracle.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef __XFS_ERRORTAG_H_
#define __XFS_ERRORTAG_H_

/*
 * error injection tags - the labels can be anything you want
 * but each tag should have its own unique number
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
 * DEBUG mode instrumentation to test and/or trigger delayed allocation
 * block killing in the event of failed writes. When enabled, all
 * buffered writes are silenty dropped and handled as if they failed.
 * All delalloc blocks in the range of the write (including pre-existing
 * delalloc blocks!) are tossed as part of the write failure error
 * handling sequence.
 */
#define XFS_ERRTAG_DROP_WRITES				28
#define XFS_ERRTAG_LOG_BAD_CRC				29
#define XFS_ERRTAG_LOG_ITEM_PIN				30
#define XFS_ERRTAG_BUF_LRU_REF				31
#define XFS_ERRTAG_FORCE_SCRUB_REPAIR			32
#define XFS_ERRTAG_MAX					33

/*
 * Random factors for above tags, 1 means always, 2 means 1/2 time, etc.
 */
#define XFS_RANDOM_DEFAULT				100
#define XFS_RANDOM_IFLUSH_1				XFS_RANDOM_DEFAULT
#define XFS_RANDOM_IFLUSH_2				XFS_RANDOM_DEFAULT
#define XFS_RANDOM_IFLUSH_3				XFS_RANDOM_DEFAULT
#define XFS_RANDOM_IFLUSH_4				XFS_RANDOM_DEFAULT
#define XFS_RANDOM_IFLUSH_5				XFS_RANDOM_DEFAULT
#define XFS_RANDOM_IFLUSH_6				XFS_RANDOM_DEFAULT
#define XFS_RANDOM_DA_READ_BUF				XFS_RANDOM_DEFAULT
#define XFS_RANDOM_BTREE_CHECK_LBLOCK			(XFS_RANDOM_DEFAULT/4)
#define XFS_RANDOM_BTREE_CHECK_SBLOCK			XFS_RANDOM_DEFAULT
#define XFS_RANDOM_ALLOC_READ_AGF			XFS_RANDOM_DEFAULT
#define XFS_RANDOM_IALLOC_READ_AGI			XFS_RANDOM_DEFAULT
#define XFS_RANDOM_ITOBP_INOTOBP			XFS_RANDOM_DEFAULT
#define XFS_RANDOM_IUNLINK				XFS_RANDOM_DEFAULT
#define XFS_RANDOM_IUNLINK_REMOVE			XFS_RANDOM_DEFAULT
#define XFS_RANDOM_DIR_INO_VALIDATE			XFS_RANDOM_DEFAULT
#define XFS_RANDOM_BULKSTAT_READ_CHUNK			XFS_RANDOM_DEFAULT
#define XFS_RANDOM_IODONE_IOERR				(XFS_RANDOM_DEFAULT/10)
#define XFS_RANDOM_STRATREAD_IOERR			(XFS_RANDOM_DEFAULT/10)
#define XFS_RANDOM_STRATCMPL_IOERR			(XFS_RANDOM_DEFAULT/10)
#define XFS_RANDOM_DIOWRITE_IOERR			(XFS_RANDOM_DEFAULT/10)
#define XFS_RANDOM_BMAPIFORMAT				XFS_RANDOM_DEFAULT
#define XFS_RANDOM_FREE_EXTENT				1
#define XFS_RANDOM_RMAP_FINISH_ONE			1
#define XFS_RANDOM_REFCOUNT_CONTINUE_UPDATE		1
#define XFS_RANDOM_REFCOUNT_FINISH_ONE			1
#define XFS_RANDOM_BMAP_FINISH_ONE			1
#define XFS_RANDOM_AG_RESV_CRITICAL			4
#define XFS_RANDOM_DROP_WRITES				1
#define XFS_RANDOM_LOG_BAD_CRC				1
#define XFS_RANDOM_LOG_ITEM_PIN				1
#define XFS_RANDOM_BUF_LRU_REF				2
#define XFS_RANDOM_FORCE_SCRUB_REPAIR			1

#endif /* __XFS_ERRORTAG_H_ */
