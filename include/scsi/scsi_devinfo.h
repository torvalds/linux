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
#define BLIST_INQUIRY_58	0x800	/* ... for broken inquiry responses */
#define BLIST_NOSTARTONADD	0x1000	/* do not do automatic start on add */
#define BLIST_MS_SKIP_PAGE_08	0x2000	/* do not send ms page 0x08 */
#define BLIST_MS_SKIP_PAGE_3F	0x4000	/* do not send ms page 0x3f */
#define BLIST_USE_10_BYTE_MS	0x8000	/* use 10 byte ms before 6 byte ms */
#define BLIST_MS_192_BYTES_FOR_3F	0x10000	/*  192 byte ms page 0x3f request */
#define BLIST_REPORTLUN2	0x20000	/* try REPORT_LUNS even for SCSI-2 devs
 					   (if HBA supports more than 8 LUNs) */
#define BLIST_NOREPORTLUN	0x40000	/* don't try REPORT_LUNS scan (SCSI-3 devs) */
#define BLIST_NOT_LOCKABLE	0x80000	/* don't use PREVENT-ALLOW commands */
#define BLIST_NO_ULD_ATTACH	0x100000 /* device is actually for RAID config */
#define BLIST_SELECT_NO_ATN	0x200000 /* select without ATN */
#define BLIST_RETRY_HWERROR	0x400000 /* retry HARDWARE_ERROR */
#define BLIST_MAX_512		0x800000 /* maximum 512 sector cdb length */
#define BLIST_ATTACH_PQ3	0x1000000 /* Scan: Attach to PQ3 devices */
#define BLIST_NO_DIF		0x2000000 /* Disable T10 PI (DIF) */
#define BLIST_SKIP_VPD_PAGES	0x4000000 /* Ignore SBC-3 VPD pages */
#define BLIST_SCSI3LUN		0x8000000 /* Scan more than 256 LUNs
					     for sequential scan */
#endif
