/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_SCSI_DEVINFO_H
#define _SCSI_SCSI_DEVINFO_H
/*
 * Flags for SCSI devices that need special treatment
 */

/* Only scan LUN 0 */
#define BLIST_NOLUN		((__force blist_flags_t)(1ULL << 0))
/* Known to have LUNs, force scanning.
 * DEPRECATED: Use max_luns=N */
#define BLIST_FORCELUN		((__force blist_flags_t)(1ULL << 1))
/* Flag for broken handshaking */
#define BLIST_BORKEN		((__force blist_flags_t)(1ULL << 2))
/* unlock by special command */
#define BLIST_KEY		((__force blist_flags_t)(1ULL << 3))
/* Do not use LUNs in parallel */
#define BLIST_SINGLELUN		((__force blist_flags_t)(1ULL << 4))
/* Buggy Tagged Command Queuing */
#define BLIST_NOTQ		((__force blist_flags_t)(1ULL << 5))
/* Non consecutive LUN numbering */
#define BLIST_SPARSELUN		((__force blist_flags_t)(1ULL << 6))
/* Avoid LUNS >= 5 */
#define BLIST_MAX5LUN		((__force blist_flags_t)(1ULL << 7))
/* Treat as (removable) CD-ROM */
#define BLIST_ISROM		((__force blist_flags_t)(1ULL << 8))
/* LUNs past 7 on a SCSI-2 device */
#define BLIST_LARGELUN		((__force blist_flags_t)(1ULL << 9))
/* override additional length field */
#define BLIST_INQUIRY_36	((__force blist_flags_t)(1ULL << 10))
/* ignore MEDIA CHANGE unit attention after resuming from runtime suspend */
#define BLIST_IGN_MEDIA_CHANGE	((__force blist_flags_t)(1ULL << 11))
/* do not do automatic start on add */
#define BLIST_NOSTARTONADD	((__force blist_flags_t)(1ULL << 12))
/* do not ask for VPD page size first on some broken targets */
#define BLIST_NO_VPD_SIZE	((__force blist_flags_t)(1ULL << 13))
#define __BLIST_UNUSED_14	((__force blist_flags_t)(1ULL << 14))
#define __BLIST_UNUSED_15	((__force blist_flags_t)(1ULL << 15))
#define __BLIST_UNUSED_16	((__force blist_flags_t)(1ULL << 16))
/* try REPORT_LUNS even for SCSI-2 devs (if HBA supports more than 8 LUNs) */
#define BLIST_REPORTLUN2	((__force blist_flags_t)(1ULL << 17))
/* don't try REPORT_LUNS scan (SCSI-3 devs) */
#define BLIST_NOREPORTLUN	((__force blist_flags_t)(1ULL << 18))
/* don't use PREVENT-ALLOW commands */
#define BLIST_NOT_LOCKABLE	((__force blist_flags_t)(1ULL << 19))
/* device is actually for RAID config */
#define BLIST_NO_ULD_ATTACH	((__force blist_flags_t)(1ULL << 20))
/* select without ATN */
#define BLIST_SELECT_NO_ATN	((__force blist_flags_t)(1ULL << 21))
/* retry HARDWARE_ERROR */
#define BLIST_RETRY_HWERROR	((__force blist_flags_t)(1ULL << 22))
/* maximum 512 sector cdb length */
#define BLIST_MAX_512		((__force blist_flags_t)(1ULL << 23))
#define __BLIST_UNUSED_24	((__force blist_flags_t)(1ULL << 24))
/* Disable T10 PI (DIF) */
#define BLIST_NO_DIF		((__force blist_flags_t)(1ULL << 25))
/* Ignore SBC-3 VPD pages */
#define BLIST_SKIP_VPD_PAGES	((__force blist_flags_t)(1ULL << 26))
#define __BLIST_UNUSED_27	((__force blist_flags_t)(1ULL << 27))
/* Attempt to read VPD pages */
#define BLIST_TRY_VPD_PAGES	((__force blist_flags_t)(1ULL << 28))
/* don't try to issue RSOC */
#define BLIST_NO_RSOC		((__force blist_flags_t)(1ULL << 29))
/* maximum 1024 sector cdb length */
#define BLIST_MAX_1024		((__force blist_flags_t)(1ULL << 30))
/* Use UNMAP limit for WRITE SAME */
#define BLIST_UNMAP_LIMIT_WS	((__force blist_flags_t)(1ULL << 31))
/* Always retry ABORTED_COMMAND with Internal Target Failure */
#define BLIST_RETRY_ITF		((__force blist_flags_t)(1ULL << 32))
/* Always retry ABORTED_COMMAND with ASC 0xc1 */
#define BLIST_RETRY_ASC_C1	((__force blist_flags_t)(1ULL << 33))

#define __BLIST_LAST_USED BLIST_RETRY_ASC_C1

#define __BLIST_HIGH_UNUSED (~(__BLIST_LAST_USED | \
			       (__force blist_flags_t) \
			       ((__force __u64)__BLIST_LAST_USED - 1ULL)))
#define __BLIST_UNUSED_MASK (__BLIST_UNUSED_14 | \
			     __BLIST_UNUSED_15 | \
			     __BLIST_UNUSED_16 | \
			     __BLIST_UNUSED_24 | \
			     __BLIST_UNUSED_27 | \
			     __BLIST_HIGH_UNUSED)

#endif
