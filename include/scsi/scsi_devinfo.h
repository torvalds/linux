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
/* do not do automatic start on add */
#define BLIST_NOSTARTONADD	((__force blist_flags_t)(1ULL << 12))
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
/* Disable T10 PI (DIF) */
#define BLIST_NO_DIF		((__force blist_flags_t)(1ULL << 25))
/* Ignore SBC-3 VPD pages */
#define BLIST_SKIP_VPD_PAGES	((__force blist_flags_t)(1ULL << 26))
/* Attempt to read VPD pages */
#define BLIST_TRY_VPD_PAGES	((__force blist_flags_t)(1ULL << 28))
/* don't try to issue RSOC */
#define BLIST_NO_RSOC		((__force blist_flags_t)(1ULL << 29))
/* maximum 1024 sector cdb length */
#define BLIST_MAX_1024		((__force blist_flags_t)(1ULL << 30))
/* Use UNMAP limit for WRITE SAME */
#define BLIST_UNMAP_LIMIT_WS	((__force blist_flags_t)(1ULL << 31))

#endif
