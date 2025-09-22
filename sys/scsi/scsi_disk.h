/*	$OpenBSD: scsi_disk.h,v 1.43 2022/01/11 23:10:11 jsg Exp $	*/
/*	$NetBSD: scsi_disk.h,v 1.10 1996/07/05 16:19:05 christos Exp $	*/

/*
 * SCSI interface description
 */

/*
 * Some lines of this file come from a file of the name "scsi.h"
 * distributed by OSF as part of mach2.5,
 *  so the following disclaimer has been kept.
 *
 * Copyright 1990 by Open Software Foundation,
 * Grenoble, FRANCE
 *
 *		All Rights Reserved
 *
 *   Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OSF or Open Software
 * Foundation not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission.
 *
 *   OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Largely written by Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * Ported to run under 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 */

/*
 * SCSI command format
 */

#ifndef	_SCSI_SCSI_DISK_H
#define _SCSI_SCSI_DISK_H

/*
 * XXX Is this also used by ATAPI?
 */
#define	FORMAT_UNIT		0x04
struct scsi_format_unit {
	u_int8_t opcode;
	u_int8_t flags;
#define	SFU_DLF_MASK	0x07
#define	SFU_CMPLST	0x08
#define	SFU_FMTDATA	0x10
	u_int8_t vendor_specific;
	u_int8_t interleave[2];
	u_int8_t control;
};

/*
 * If the FmtData bit is set, a FORMAT UNIT parameter list is transferred
 * to the target during the DATA OUT phase.  The parameter list includes
 *
 *	Defect list header
 *	Initialization pattern descriptor (if any)
 *	Defect descriptor(s) (if any)
 */

struct scsi_format_unit_defect_list_header {
	u_int8_t reserved;
	u_int8_t flags;
#define	DLH_VS		0x01		/* vendor specific */
#define	DLH_IMMED	0x02		/* immediate return */
#define	DLH_DSP		0x04		/* disable saving parameters */
#define	DLH_IP		0x08		/* initialization pattern */
#define	DLH_STPF	0x10		/* stop format */
#define	DLH_DCRT	0x20		/* disable certification */
#define	DLH_DPRY	0x40		/* disable primary */
#define	DLH_FOV		0x80		/* format options valid */
	u_int8_t defect_lst_len[2];
};

/*
 * See Table 117 of the SCSI-2 specification for a description of
 * the IP modifier.
 */
struct scsi_initialization_pattern_descriptor {
	u_int8_t ip_modifier;
	u_int8_t pattern_type;
#define	IP_TYPE_DEFAULT		0x01
#define	IP_TYPE_REPEAT		0x01
				/* 0x02 -> 0x7f: reserved */
				/* 0x80 -> 0xff: vendor-specific */
	u_int8_t pattern_length[2];
#if 0
	u_int8_t pattern[...];
#endif /* 0 */
};

/*
 * Defect descriptors.  These are used as the defect lists in the FORMAT UNIT
 * and READ DEFECT DATA commands, and as the translate page of the
 * SEND DIAGNOSTIC and RECEIVE DIAGNOSTIC RESULTS commands.
 */

/* Block format */
struct scsi_defect_descriptor_bf {
	u_int8_t block_address[4];
};

/* Bytes from index format */
struct scsi_defect_descriptor_bfif {
	u_int8_t cylinder[2];
	u_int8_t head;
	u_int8_t bytes_from_index[2];
};

/* Physical sector format */
struct scsi_defect_descriptor_psf {
	u_int8_t cylinder[2];
	u_int8_t head;
	u_int8_t sector[2];
};


struct scsi_reassign_blocks {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[3];
	u_int8_t control;
};

/*
 * XXX Is this also used by ATAPI?
 */
#define	REZERO_UNIT		0x01
struct scsi_rezero_unit {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t reserved[3];
	u_int8_t control;
};

struct scsi_rw {
	u_int8_t opcode;
	u_int8_t addr[3];
#define	SRW_TOPADDR	0x1F	/* only 5 bits here */
	u_int8_t length;
	u_int8_t control;
};

struct scsi_rw_10 {
	u_int8_t opcode;
	u_int8_t byte2;
#define	SRWB_RELADDR	0x01
	u_int8_t addr[4];
	u_int8_t reserved;
	u_int8_t length[2];
	u_int8_t control;
};

struct scsi_rw_12 {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t addr[4];
	u_int8_t length[4];
	u_int8_t reserved;
	u_int8_t control;
};

struct scsi_rw_16 {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t addr[8];
	u_int8_t length[4];
	u_int8_t reserved;
	u_int8_t control;
};

struct scsi_write_same_10 {
	u_int8_t opcode;
	u_int8_t flags;
#define WRITE_SAME_F_LBDATA	(1 << 1)
#define WRITE_SAME_F_PBDATA	(1 << 2)
	u_int8_t lba[4];
	u_int8_t group_number;
	u_int8_t length[2];
	u_int8_t control;
};

struct scsi_write_same_16 {
	u_int8_t opcode;
	u_int8_t flags;
/* includes WRITE SAME 10 flags */
#define WRITE_SAME_F_UNMAP	(1 << 3)
#define WRITE_SAME_F_ANCHOR	(1 << 4)
	u_int8_t lba[8];
	u_int8_t length[4];
	u_int8_t group_number;
	u_int8_t control;
};

struct scsi_unmap {
	u_int8_t opcode;
	u_int8_t anchor;
	u_int8_t _reserved[4];
	u_int8_t group_number;
	u_int8_t list_len[2];
	u_int8_t control;
};

struct scsi_unmap_data {
	u_int8_t data_length[2];
	u_int8_t desc_length[2];
	u_int8_t _reserved[4];

	/* followed by struct scsi_unmap_desc */
};

struct scsi_unmap_desc {
	u_int8_t logical_addr[8];
	u_int8_t logical_blocks[4];
	u_int8_t _reserved[4];
};

struct scsi_read_capacity {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t addr[4];
	u_int8_t unused[3];
	u_int8_t control;
};

struct scsi_read_capacity_16 {
	u_int8_t opcode;
	u_int8_t byte2;
#define	SRC16_SERVICE_ACTION	0x10
	u_int8_t addr[8];
	u_int8_t length[4];
	u_int8_t reserved;
	u_int8_t control;
};

struct scsi_start_stop {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[2];
	u_int8_t how;
#define	SSS_STOP		0x00
#define	SSS_START		0x01
#define	SSS_LOEJ		0x02
	u_int8_t control;
};


/*
 * XXX Does ATAPI have an equivalent?
 */
struct scsi_synchronize_cache {
	u_int8_t opcode;
	u_int8_t flags;
#define	SSC_RELADR	0x01
#define	SSC_IMMED	0x02
	u_int8_t addr[4];
	u_int8_t reserved;
	u_int8_t length[2];
	u_int8_t control;
};



/*
 * Disk specific opcodes
 */
#define REASSIGN_BLOCKS		0x07
#define READ_COMMAND		0x08
#define WRITE_COMMAND		0x0a
#define READ_CAPACITY		0x25
#define READ_CAPACITY_16	0x9e
#define READ_10			0x28
#define WRITE_10		0x2a
#define READ_12			0xa8
#define WRITE_12		0xaa
#define READ_16			0x88
#define WRITE_16		0x8a
#define SYNCHRONIZE_CACHE	0x35
#define WRITE_SAME_10		0x41
#define WRITE_SAME_16		0x93
#define UNMAP			0x42


struct scsi_reassign_blocks_data {
	u_int8_t reserved[2];
	u_int8_t length[2];
	struct {
		u_int8_t dlbaddr[4];
	} defect_descriptor[1];
};

/* Only the lower 6 bits of the pg_code field are used for page #. */
#define PAGE_DISK_FORMAT	3
#define PAGE_RIGID_GEOMETRY	4
#define PAGE_FLEX_GEOMETRY	5
#define PAGE_REDUCED_GEOMETRY	6
#define PAGE_CACHING_MODE	8

struct page_disk_format {
	u_int8_t pg_code;	/* page code (should be 3) */
	u_int8_t pg_length;	/* page length (should be 0x16) */
	u_int8_t trk_z[2];	/* tracks per zone */
	u_int8_t alt_sec[2];	/* alternate sectors per zone */
	u_int8_t alt_trk_z[2];	/* alternate tracks per zone */
	u_int8_t alt_trk_v[2];	/* alternate tracks per volume */
	u_int8_t ph_sec_t[2];	/* physical sectors per track */
	u_int8_t bytes_s[2];	/* bytes per sector */
	u_int8_t interleave[2];	/* interleave */
	u_int8_t trk_skew[2];	/* track skew factor */
	u_int8_t cyl_skew[2];	/* cylinder skew */
	u_int8_t flags;		/* various */
#define	DISK_FMT_SURF	0x10
#define	DISK_FMT_RMB	0x20
#define	DISK_FMT_HSEC	0x40
#define	DISK_FMT_SSEC	0x80
	u_int8_t reserved1;
	u_int8_t reserved2;
	u_int8_t reserved3;
};

struct page_rigid_geometry {
	u_int8_t pg_code;	/* page code (should be 4) */
	u_int8_t pg_length;	/* page length (should be 0x12 or 0x16)	*/
	u_int8_t ncyl[3];	/* number of cylinders */
	u_int8_t nheads;	/* number of heads */
	u_int8_t st_cyl_wp[3];	/* starting cyl., write precomp */
	u_int8_t st_cyl_rwc[3];	/* starting cyl., red. write cur */
	u_int8_t driv_step[2];	/* drive step rate */
	u_int8_t land_zone[3];	/* landing zone cylinder */
	u_int8_t sp_sync_ctl;	/* spindle synch control */
#define SPINDLE_SYNCH_MASK	0x03	/* mask of valid bits */
#define SPINDLE_SYNCH_NONE	0x00	/* synch disabled or not supported */
#define SPINDLE_SYNCH_SLAVE	0x01	/* disk is a slave */
#define SPINDLE_SYNCH_MASTER	0x02	/* disk is a master */
#define SPINDLE_SYNCH_MCONTROL	0x03	/* disk is a master control */
	u_int8_t rot_offset;	/* rotational offset (for spindle synch) */
	u_int8_t reserved1;
	u_int8_t rpm[2];	/* media rotation speed */
	u_int8_t reserved2;
	u_int8_t reserved3;
};

struct page_flex_geometry {
	u_int8_t pg_code;	/* page code (should be 5) */
	u_int8_t pg_length;	/* page length (should be 0x1a or 0x1e) */
	u_int8_t xfr_rate[2];
	u_int8_t nheads;	/* number of heads */
	u_int8_t ph_sec_tr;	/* physical sectors per track */
	u_int8_t bytes_s[2];	/* bytes per sector */
	u_int8_t ncyl[2];	/* number of cylinders */
	u_int8_t st_cyl_wp[2];	/* start cyl., write precomp */
	u_int8_t st_cyl_rwc[2];	/* start cyl., red. write cur */
	u_int8_t driv_step[2];	/* drive step rate */
	u_int8_t driv_step_w;	/* drive step pulse width */
	u_int8_t head_settle[2];/* head settle delay */
	u_int8_t motor_on;	/* motor on delay */
	u_int8_t motor_off;	/* motor off delay */
	u_int8_t flags;		/* various flags */
#define MOTOR_ON		0x20	/* motor on (pin 16)? */
#define START_AT_SECTOR_1	0x40	/* start at sector 1  */
#define READY_VALID		0x20	/* RDY (pin 34) valid */
	u_int8_t step_p_cyl;	/* step pulses per cylinder */
	u_int8_t write_pre;	/* write precompensation */
	u_int8_t head_load;	/* head load delay */
	u_int8_t head_unload;	/* head unload delay */
	u_int8_t pin_34_2;	/* pin 34 (6) pin 2 (7/11) definition */
	u_int8_t pin_4_1;	/* pin 4 (8/9) pin 1 (13) definition */
	u_int8_t rpm[2];	/* media rotation speed */
	u_int8_t reserved1;
	u_int8_t reserved2;
};

struct page_reduced_geometry {
	u_int8_t pg_code;	/* page code (should be 6) */
	u_int8_t pg_length;	/* page length (should be 0x0B) */
	u_int8_t wcd;		/* bit 0 = write cache disable */
	u_int8_t bytes_s[2];	/* bytes per sector */
	u_int8_t sectors[5];	/* total number of sectors */
	u_int8_t pow_perf;	/* power/performance level */
	u_int8_t flags;		/* various */
#define	LOCK_DISABLED	0x1
#define	FORMAT_DISABLED	0x2
#define	WRITE_DISABLED	0x4
#define	READ_DISABLED	0x8
	u_int8_t reserved;
};

struct page_caching_mode {
	u_int8_t pg_code;	/* page code (should be 8) */
	u_int8_t pg_length;	/* page length (should be 0x12) */
	u_int8_t flags;
#define PG_CACHE_FL_RCD		(1<<0)
#define PG_CACHE_FL_MF		(1<<1)
#define PG_CACHE_FL_WCE		(1<<2)
#define PG_CACHE_FL_SIZE	(1<<3)
#define PG_CACHE_FL_DISC	(1<<4)
#define PG_CACHE_FL_CAP		(1<<5)
#define PG_CACHE_FL_ABPF	(1<<6)
#define PG_CACHE_FL_IC		(1<<7)
	u_int8_t priority;
#define PG_CACHE_PRI_DEMAND(_f)		((_f) & 0x0f)
#define PG_CACHE_PRI_WRITE(_f)		(((_f) >> 4) & 0x0f)
	u_int8_t dis_prefetch_tl[2];
	u_int8_t min_prefetch[2];
	u_int8_t max_prefetch[2];
	u_int8_t max_prefetch_ceil[2];
};

#define SI_PG_DISK_LIMITS	0xb0 /* block limits */
#define SI_PG_DISK_INFO		0xb1 /* device characteristics */
#define SI_PG_DISK_THIN		0xb2 /* thin provisioning */

struct scsi_vpd_disk_limits {
	struct scsi_vpd_hdr hdr;
#define SI_PG_DISK_LIMITS_LEN		0x10
#define SI_PG_DISK_LIMITS_LEN_THIN	0x3c

	u_int8_t		_reserved1[1];
	u_int8_t		max_comp_wr_len;
	u_int8_t		optimal_xfer_granularity[2];

	u_int8_t		max_xfer_len[4];

	u_int8_t		optimal_xfer[4];

	u_int8_t		max_xd_prefetch_len[4];

	u_int8_t		max_unmap_lba_count[4];

	u_int8_t		max_unmap_desc_count[4];

	u_int8_t		optimal_unmap_granularity[4];

	u_int8_t		unmap_granularity_align[4];
#define SI_PG_DISK_LIMITS_UGAVALID	(1U << 31)

	u_int8_t		_reserved2[28];
};

struct scsi_vpd_disk_info {
	struct scsi_vpd_hdr	hdr;
	u_int8_t		rpm[2];
#define VPD_DISK_INFO_RPM_UNDEF		0x0000
#define VPD_DISK_INFO_RPM_NONE		0x0001
	u_int8_t		_reserved1[1];
	u_int8_t		form_factor;
#define VPD_DISK_INFO_FORM_MASK		0xf
#define VPD_DISK_INFO_FORM_UNDEF	0x0
#define VPD_DISK_INFO_FORM_5_25		0x1
#define VPD_DISK_INFO_FORM_3_5		0x2
#define VPD_DISK_INFO_FORM_2_5		0x3
#define VPD_DISK_INFO_FORM_1_8		0x4
#define VPD_DISK_INFO_FORM_LT_1_8	0x5
	u_int8_t		_reserved2[56];
};

struct scsi_vpd_disk_thin {
	struct scsi_vpd_hdr	hdr;

	u_int8_t		threshold_exponent;
	u_int8_t		flags;
#define VPD_DISK_THIN_DP		(1 << 0) /* descriptor present */
#define VPD_DISK_THIN_ANC_SUP		(0x7 << 1)
#define VPD_DISK_THIN_ANC_SUP_NO	(0x0 << 1)
#define VPD_DISK_THIN_ANC_SUP_YES	(0x1 << 1)
#define VPD_DISK_THIN_TPWS		(1 << 6) /* WRITE SAME 16 */
#define VPD_DISK_THIN_TPU		(1 << 7) /* UNMAP */
	u_int8_t		_reserved1[2];

	/* followed by a designation descriptor if DP is set */
};

#endif /* _SCSI_SCSI_DISK_H */
