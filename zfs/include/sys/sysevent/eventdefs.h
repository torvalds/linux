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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2017 Joyent, Inc.
 */

#ifndef	_SYS_SYSEVENT_EVENTDEFS_H
#define	_SYS_SYSEVENT_EVENTDEFS_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * eventdefs.h contains public definitions for sysevent types (classes
 * and subclasses).  All additions/removal/changes are subject
 * to PSARC approval.
 */

/* Sysevent Class definitions */
#define	EC_NONE		"EC_none"
#define	EC_PRIV		"EC_priv"
#define	EC_PLATFORM	"EC_platform"	/* events private to platform */
#define	EC_DR		"EC_dr"	/* Dynamic reconfiguration event class */
#define	EC_ENV		"EC_env"	/* Environmental monitor event class */
#define	EC_DOMAIN	"EC_domain"	/* Domain event class */
#define	EC_AP_DRIVER	"EC_ap_driver"	/* Alternate Pathing event class */
#define	EC_IPMP		"EC_ipmp"	/* IP Multipathing event class */
#define	EC_DEV_ADD	"EC_dev_add"	/* device add event class */
#define	EC_DEV_REMOVE	"EC_dev_remove"	/* device remove event class */
#define	EC_DEV_BRANCH	"EC_dev_branch"	/* device tree branch event class */
#define	EC_DEV_STATUS	"EC_dev_status"	/* device status event class */
#define	EC_FM		"EC_fm"		/* FMA error report event */
#define	EC_ZFS		"EC_zfs"	/* ZFS event */
#define	EC_DATALINK	"EC_datalink"	/* datalink event */
#define	EC_VRRP		"EC_vrrp"	/* VRRP event */

/*
 * EC_DEV_ADD and EC_DEV_REMOVE subclass definitions - supporting attributes
 * (name/value pairs) are found in sys/sysevent/dev.h
 */
#define	ESC_DISK	"disk"		/* disk device */
#define	ESC_NETWORK	"network"	/* network interface */
#define	ESC_PRINTER	"printer"	/* printer device */
#define	ESC_LOFI	"lofi"		/* lofi device */

/*
 * EC_DEV_BRANCH subclass definitions - supporting attributes (name/value pairs)
 * are found in sys/sysevent/dev.h
 */

/* device tree branch added */
#define	ESC_DEV_BRANCH_ADD	"dev_branch_add"

/* device tree branch removed */
#define	ESC_DEV_BRANCH_REMOVE	"dev_branch_remove"

/*
 * EC_DEV_STATUS subclass definitions
 *
 * device capacity dynamically changed
 */
#define	ESC_DEV_DLE		"dev_dle"

/* LUN has received an eject request from the user */
#define	ESC_DEV_EJECT_REQUEST	"dev_eject_request"

/* FMA Fault and Error event protocol subclass */
#define	ESC_FM_ERROR		"error"
#define	ESC_FM_ERROR_REPLAY	"error_replay"

/*
 * ZFS subclass definitions.  supporting attributes (name/value paris) are found
 * in sys/fs/zfs.h
 */
#define	ESC_ZFS_RESILVER_START		"resilver_start"
#define	ESC_ZFS_RESILVER_FINISH		"resilver_finish"
#define	ESC_ZFS_VDEV_REMOVE		"vdev_remove"
#define	ESC_ZFS_VDEV_REMOVE_AUX		"vdev_remove_aux"
#define	ESC_ZFS_VDEV_REMOVE_DEV		"vdev_remove_dev"
#define	ESC_ZFS_POOL_CREATE		"pool_create"
#define	ESC_ZFS_POOL_DESTROY		"pool_destroy"
#define	ESC_ZFS_POOL_IMPORT		"pool_import"
#define	ESC_ZFS_POOL_EXPORT		"pool_export"
#define	ESC_ZFS_VDEV_ADD		"vdev_add"
#define	ESC_ZFS_VDEV_ATTACH		"vdev_attach"
#define	ESC_ZFS_VDEV_CLEAR		"vdev_clear"
#define	ESC_ZFS_VDEV_CHECK		"vdev_check"
#define	ESC_ZFS_VDEV_ONLINE		"vdev_online"
#define	ESC_ZFS_CONFIG_SYNC		"config_sync"
#define	ESC_ZFS_SCRUB_START		"scrub_start"
#define	ESC_ZFS_SCRUB_FINISH		"scrub_finish"
#define	ESC_ZFS_VDEV_SPARE		"vdev_spare"
#define	ESC_ZFS_VDEV_AUTOEXPAND		"vdev_autoexpand"
#define	ESC_ZFS_BOOTFS_VDEV_ATTACH	"bootfs_vdev_attach"
#define	ESC_ZFS_POOL_REGUID		"pool_reguid"
#define	ESC_ZFS_HISTORY_EVENT		"history_event"

/*
 * datalink subclass definitions.
 */
#define	ESC_DATALINK_PHYS_ADD	"datalink_phys_add"	/* new physical link */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SYSEVENT_EVENTDEFS_H */
