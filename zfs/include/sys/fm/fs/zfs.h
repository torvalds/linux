/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_FM_FS_ZFS_H
#define	_SYS_FM_FS_ZFS_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZFS_ERROR_CLASS				"fs.zfs"

#define	FM_EREPORT_ZFS_CHECKSUM			"checksum"
#define	FM_EREPORT_ZFS_IO			"io"
#define	FM_EREPORT_ZFS_DATA			"data"
#define	FM_EREPORT_ZFS_DELAY			"delay"
#define	FM_EREPORT_ZFS_POOL			"zpool"
#define	FM_EREPORT_ZFS_DEVICE_UNKNOWN		"vdev.unknown"
#define	FM_EREPORT_ZFS_DEVICE_OPEN_FAILED	"vdev.open_failed"
#define	FM_EREPORT_ZFS_DEVICE_CORRUPT_DATA	"vdev.corrupt_data"
#define	FM_EREPORT_ZFS_DEVICE_NO_REPLICAS	"vdev.no_replicas"
#define	FM_EREPORT_ZFS_DEVICE_BAD_GUID_SUM	"vdev.bad_guid_sum"
#define	FM_EREPORT_ZFS_DEVICE_TOO_SMALL		"vdev.too_small"
#define	FM_EREPORT_ZFS_DEVICE_BAD_LABEL		"vdev.bad_label"
#define	FM_EREPORT_ZFS_DEVICE_BAD_ASHIFT	"vdev.bad_ashift"
#define	FM_EREPORT_ZFS_IO_FAILURE		"io_failure"
#define	FM_EREPORT_ZFS_PROBE_FAILURE		"probe_failure"
#define	FM_EREPORT_ZFS_LOG_REPLAY		"log_replay"
#define	FM_EREPORT_ZFS_CONFIG_CACHE_WRITE	"config_cache_write"

#define	FM_EREPORT_PAYLOAD_ZFS_POOL		"pool"
#define	FM_EREPORT_PAYLOAD_ZFS_POOL_FAILMODE	"pool_failmode"
#define	FM_EREPORT_PAYLOAD_ZFS_POOL_GUID	"pool_guid"
#define	FM_EREPORT_PAYLOAD_ZFS_POOL_CONTEXT	"pool_context"
#define	FM_EREPORT_PAYLOAD_ZFS_POOL_STATE	"pool_state"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID	"vdev_guid"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_TYPE	"vdev_type"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_PATH	"vdev_path"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_PHYSPATH	"vdev_physpath"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_ENC_SYSFS_PATH	"vdev_enc_sysfs_path"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_DEVID	"vdev_devid"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_FRU		"vdev_fru"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_STATE	"vdev_state"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_LASTSTATE	"vdev_laststate"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_ASHIFT	"vdev_ashift"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_COMP_TS	"vdev_complete_ts"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_DELTA_TS	"vdev_delta_ts"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_SPARE_PATHS	"vdev_spare_paths"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_SPARE_GUIDS	"vdev_spare_guids"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_READ_ERRORS	"vdev_read_errors"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_WRITE_ERRORS "vdev_write_errors"
#define	FM_EREPORT_PAYLOAD_ZFS_VDEV_CKSUM_ERRORS "vdev_cksum_errors"
#define	FM_EREPORT_PAYLOAD_ZFS_PARENT_GUID	"parent_guid"
#define	FM_EREPORT_PAYLOAD_ZFS_PARENT_TYPE	"parent_type"
#define	FM_EREPORT_PAYLOAD_ZFS_PARENT_PATH	"parent_path"
#define	FM_EREPORT_PAYLOAD_ZFS_PARENT_DEVID	"parent_devid"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJSET	"zio_objset"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJECT	"zio_object"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_LEVEL	"zio_level"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_BLKID	"zio_blkid"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_ERR		"zio_err"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_OFFSET	"zio_offset"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_SIZE		"zio_size"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_FLAGS	"zio_flags"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_STAGE	"zio_stage"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_PIPELINE	"zio_pipeline"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_DELAY	"zio_delay"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_TIMESTAMP	"zio_timestamp"
#define	FM_EREPORT_PAYLOAD_ZFS_ZIO_DELTA	"zio_delta"
#define	FM_EREPORT_PAYLOAD_ZFS_PREV_STATE	"prev_state"
#define	FM_EREPORT_PAYLOAD_ZFS_CKSUM_EXPECTED	"cksum_expected"
#define	FM_EREPORT_PAYLOAD_ZFS_CKSUM_ACTUAL	"cksum_actual"
#define	FM_EREPORT_PAYLOAD_ZFS_CKSUM_ALGO	"cksum_algorithm"
#define	FM_EREPORT_PAYLOAD_ZFS_CKSUM_BYTESWAP	"cksum_byteswap"
#define	FM_EREPORT_PAYLOAD_ZFS_BAD_OFFSET_RANGES "bad_ranges"
#define	FM_EREPORT_PAYLOAD_ZFS_BAD_RANGE_MIN_GAP "bad_ranges_min_gap"
#define	FM_EREPORT_PAYLOAD_ZFS_BAD_RANGE_SETS	"bad_range_sets"
#define	FM_EREPORT_PAYLOAD_ZFS_BAD_RANGE_CLEARS	"bad_range_clears"
#define	FM_EREPORT_PAYLOAD_ZFS_BAD_SET_BITS	"bad_set_bits"
#define	FM_EREPORT_PAYLOAD_ZFS_BAD_CLEARED_BITS	"bad_cleared_bits"
#define	FM_EREPORT_PAYLOAD_ZFS_BAD_SET_HISTOGRAM "bad_set_histogram"
#define	FM_EREPORT_PAYLOAD_ZFS_BAD_CLEARED_HISTOGRAM "bad_cleared_histogram"

#define	FM_EREPORT_FAILMODE_WAIT		"wait"
#define	FM_EREPORT_FAILMODE_CONTINUE		"continue"
#define	FM_EREPORT_FAILMODE_PANIC		"panic"

#define	FM_RESOURCE_REMOVED			"removed"
#define	FM_RESOURCE_AUTOREPLACE			"autoreplace"
#define	FM_RESOURCE_STATECHANGE			"statechange"

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FM_FS_ZFS_H */
