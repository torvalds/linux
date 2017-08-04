#ifndef _SCSI_SCSI_DEVINFO_H
#define _SCSI_SCSI_DEVINFO_H
/*
 * Flags for SCSI devices that need special treatment
 */
#define BLIST_NOLUN     	0x001	/* Only scan LUN 0 */
#define BLIST_FORCELUN  	0x002	/* Known to have LUNs, force scanning,
					   deprecated: Use max_luns=N */
#define BLIST_BORKEN    	0x004	/* Flag for broken handshaking */
#define BLIST_KEY       	0x008	/* unlock by special command */
#define BLIST_SINGLELUN 	0x010	/* Do not use LUNs in parallel */
#define BLIST_NOTQ		0x020	/* Buggy Tagged Command Queuing */
#define BLIST_SPARSELUN 	0x040	/* Non consecutive LUN numbering */
#define BLIST_MAX5LUN		0x080	/* Avoid LUNS >= 5 */
#define BLIST_ISROM     	0x100	/* Treat as (removable) CD-ROM */
#define BLIST_LARGELUN		0x200	/* LUNs past 7 on a SCSI-2 device */
#define BLIST_INQUIRY_36	0x400	/* override additional length field */
#define BLIST_NOSTARTONADD	0x1000	/* do not do automatic start on add */
#define BLIST_REPORTLUN2	0x20000	/* try REPORT_LUNS even for SCSI-2 devs
 					   (if HBA supports more than 8 LUNs) */
#define BLIST_NOREPORTLUN	0x40000	/* don't try REPORT_LUNS scan (SCSI-3 devs) */
#define BLIST_NOT_LOCKABLE	0x80000	/* don't use PREVENT-ALLOW commands */
#define BLIST_NO_ULD_ATTACH	0x100000 /* device is actually for RAID config */
#define BLIST_SELECT_NO_ATN	0x200000 /* select without ATN */
#define BLIST_RETRY_HWERROR	0x400000 /* retry HARDWARE_ERROR */
#define BLIST_MAX_512		0x800000 /* maximum 512 sector cdb length */
#define BLIST_NO_DIF		0x2000000 /* Disable T10 PI (DIF) */
#define BLIST_SKIP_VPD_PAGES	0x4000000 /* Ignore SBC-3 VPD pages */
#define BLIST_TRY_VPD_PAGES	0x10000000 /* Attempt to read VPD pages */
#define BLIST_NO_RSOC		0x20000000 /* don't try to issue RSOC */
#define BLIST_MAX_1024		0x40000000 /* maximum 1024 sector cdb length */

#endif
