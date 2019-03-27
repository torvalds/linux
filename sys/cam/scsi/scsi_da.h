/*
 * Structures and definitions for SCSI commands to Direct Access Devices
 */

/*-
 * Some lines of this file come from a file of the name "scsi.h"
 * distributed by OSF as part of mach2.5,
 *  so the following disclaimer has been kept.
 *
 * Copyright 1990 by Open Software Foundation,
 * Grenoble, FRANCE
 *
 * 		All Rights Reserved
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

/*-
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
 *
 * $FreeBSD$
 */

#ifndef	_SCSI_SCSI_DA_H
#define _SCSI_SCSI_DA_H 1

#include <sys/cdefs.h>

struct scsi_rezero_unit
{
	u_int8_t opcode;
#define SRZU_LUN_MASK 0xE0
	u_int8_t byte2;
	u_int8_t reserved[3];
	u_int8_t control;
};

/*
 * NOTE:  The lower three bits of byte2 of the format CDB are the same as
 * the lower three bits of byte2 of the read defect data CDB, below.
 */
struct scsi_format_unit
{
	u_int8_t opcode;
	u_int8_t byte2;
#define FU_FORMAT_MASK	SRDD10_DLIST_FORMAT_MASK
#define FU_BLOCK_FORMAT	SRDD10_BLOCK_FORMAT
#define FU_BFI_FORMAT	SRDD10_BYTES_FROM_INDEX_FORMAT
#define FU_PHYS_FORMAT	SRDD10_PHYSICAL_SECTOR_FORMAT
#define FU_CMPLST	0x08
#define FU_FMT_DATA	0x10
	u_int8_t vendor_specific;
	u_int8_t interleave[2];
	u_int8_t control;
};

struct scsi_reassign_blocks
{
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[3];
	u_int8_t control;
};

struct scsi_read_defect_data_10
{
	uint8_t opcode;
	uint8_t byte2;
#define SRDD10_GLIST 0x08
#define SRDD10_PLIST 0x10
#define SRDD10_DLIST_FORMAT_MASK 0x07
#define SRDD10_BLOCK_FORMAT            0x00
#define SRDD10_EXT_BFI_FORMAT 	       0x01
#define SRDD10_EXT_PHYS_FORMAT 	       0x02
#define SRDD10_LONG_BLOCK_FORMAT       0x03
#define SRDD10_BYTES_FROM_INDEX_FORMAT 0x04
#define SRDD10_PHYSICAL_SECTOR_FORMAT  0x05
#define SRDD10_VENDOR_FORMAT	       0x06
	uint8_t format;
	uint8_t reserved[4];
	uint8_t alloc_length[2];
#define	SRDD10_MAX_LENGTH		0xffff
	uint8_t control;
};

struct scsi_sanitize
{
	u_int8_t opcode;
	u_int8_t byte2;
#define SSZ_SERVICE_ACTION_OVERWRITE         0x01
#define SSZ_SERVICE_ACTION_BLOCK_ERASE       0x02
#define SSZ_SERVICE_ACTION_CRYPTO_ERASE      0x03
#define SSZ_SERVICE_ACTION_EXIT_MODE_FAILURE 0x1F
#define SSZ_UNRESTRICTED_EXIT                0x20
#define SSZ_IMMED                            0x80
	u_int8_t reserved[5];
	u_int8_t length[2];
	u_int8_t control;
};

struct scsi_sanitize_parameter_list
{
	u_int8_t byte1;
#define SSZPL_INVERT 0x80
	u_int8_t reserved;
	u_int8_t length[2];
	/* Variable length initialization pattern. */
#define SSZPL_MAX_PATTERN_LENGTH 65535
};

struct scsi_read_defect_data_12
{
	uint8_t opcode;
#define SRDD12_GLIST 0x08
#define SRDD12_PLIST 0x10
#define SRDD12_DLIST_FORMAT_MASK 0x07
#define SRDD12_BLOCK_FORMAT            SRDD10_BLOCK_FORMAT
#define SRDD12_BYTES_FROM_INDEX_FORMAT SRDD10_BYTES_FROM_INDEX_FORMAT
#define SRDD12_PHYSICAL_SECTOR_FORMAT  SRDD10_PHYSICAL_SECTOR_FORMAT
	uint8_t format;
	uint8_t address_descriptor_index[4];
	uint8_t alloc_length[4];
#define	SRDD12_MAX_LENGTH		0xffffffff
	uint8_t reserved;
	uint8_t control;
};

struct scsi_zbc_out
{
	uint8_t opcode;
	uint8_t service_action;
#define	ZBC_OUT_SA_CLOSE	0x01
#define	ZBC_OUT_SA_FINISH	0x02
#define	ZBC_OUT_SA_OPEN		0x03
#define	ZBC_OUT_SA_RWP		0x04
	uint8_t zone_id[8];
	uint8_t reserved[4];
	uint8_t zone_flags;
#define	ZBC_OUT_ALL		0x01
	uint8_t control;
};

struct scsi_zbc_in
{
	uint8_t opcode;
	uint8_t service_action;
#define	ZBC_IN_SA_REPORT_ZONES	0x00
	uint8_t zone_start_lba[8];
	uint8_t length[4];
	uint8_t zone_options;
#define	ZBC_IN_PARTIAL		0x80
#define	ZBC_IN_REP_ALL_ZONES	0x00
#define	ZBC_IN_REP_EMPTY	0x01
#define	ZBC_IN_REP_IMP_OPEN	0x02
#define	ZBC_IN_REP_EXP_OPEN	0x03
#define	ZBC_IN_REP_CLOSED	0x04
#define	ZBC_IN_REP_FULL		0x05
#define	ZBC_IN_REP_READONLY	0x06
#define	ZBC_IN_REP_OFFLINE	0x07
#define	ZBC_IN_REP_RESET	0x10
#define	ZBC_IN_REP_NON_SEQ	0x11
#define	ZBC_IN_REP_NON_WP	0x3f
#define	ZBC_IN_REP_MASK		0x3f
	uint8_t control;
};

struct scsi_report_zones_desc {
	uint8_t zone_type;
#define	SRZ_TYPE_CONVENTIONAL	0x01
#define	SRZ_TYPE_SEQ_REQUIRED	0x02
#define	SRZ_TYPE_SEQ_PREFERRED	0x03
#define	SRZ_TYPE_MASK		0x0f
	uint8_t zone_flags;
#define	SRZ_ZONE_COND_SHIFT	4
#define	SRZ_ZONE_COND_MASK	0xf0
#define	SRZ_ZONE_COND_NWP	0x00
#define	SRZ_ZONE_COND_EMPTY	0x10
#define	SRZ_ZONE_COND_IMP_OPEN	0x20
#define	SRZ_ZONE_COND_EXP_OPEN	0x30
#define	SRZ_ZONE_COND_CLOSED	0x40
#define	SRZ_ZONE_COND_READONLY	0xd0
#define	SRZ_ZONE_COND_FULL	0xe0
#define	SRZ_ZONE_COND_OFFLINE	0xf0
#define	SRZ_ZONE_NON_SEQ	0x02
#define	SRZ_ZONE_RESET		0x01
	uint8_t reserved[6];
	uint8_t zone_length[8];
	uint8_t zone_start_lba[8];
	uint8_t write_pointer_lba[8];
	uint8_t reserved2[32];
};

struct scsi_report_zones_hdr {
	uint8_t length[4];
	uint8_t byte4;
#define	SRZ_SAME_ALL_DIFFERENT	 0x00 /* Lengths and types vary */
#define	SRZ_SAME_ALL_SAME	 0x01 /* Lengths and types the same */
#define	SRZ_SAME_LAST_DIFFERENT	 0x02 /* Types same, last length varies */
#define SRZ_SAME_TYPES_DIFFERENT 0x03 /* Types vary, length the same */
#define	SRZ_SAME_MASK		 0x0f
	uint8_t reserved[3];
	uint8_t maximum_lba[8];
	uint8_t reserved2[48];
	struct scsi_report_zones_desc desc_list[];
};

/*
 * Opcodes
 */
#define REZERO_UNIT		0x01
#define FORMAT_UNIT		0x04
#define	REASSIGN_BLOCKS		0x07
#define MODE_SELECT		0x15
#define MODE_SENSE		0x1a
#define	READ_FORMAT_CAPACITIES	0x23
#define	WRITE_AND_VERIFY	0x2e
#define	VERIFY			0x2f
#define READ_DEFECT_DATA_10	0x37
#define SANITIZE		0x48
#define	ZBC_OUT			0x94
#define	ZBC_IN			0x95
#define READ_DEFECT_DATA_12	0xb7

struct format_defect_list_header
{
	u_int8_t reserved;
	u_int8_t byte2;
#define FU_DLH_VS	0x01
#define FU_DLH_IMMED	0x02
#define FU_DLH_DSP	0x04
#define FU_DLH_IP	0x08
#define FU_DLH_STPF	0x10
#define FU_DLH_DCRT	0x20
#define FU_DLH_DPRY	0x40
#define FU_DLH_FOV	0x80
	u_int8_t defect_list_length[2];
};

struct format_ipat_descriptor
{
	u_int8_t byte1;
#define	FU_INIT_NO_HDR		0x00
#define FU_INIT_LBA_MSB		0x40
#define FU_INIT_LBA_EACH	0x80
#define FU_INIT_SI		0x20
	u_int8_t pattern_type;
#define FU_INIT_PAT_DEFAULT	0x00
#define FU_INIT_PAT_REPEAT	0x01
	u_int8_t pat_length[2];
};

struct scsi_read_format_capacities
{
	uint8_t	opcode;		/* READ_FORMAT_CAPACITIES */
	uint8_t	byte2;
#define	SRFC_LUN_MASK	0xE0
	uint8_t	reserved0[5];
	uint8_t	alloc_length[2];
	uint8_t	reserved1[3];
};

struct scsi_verify_10
{
	uint8_t	opcode;		/* VERIFY(10) */
	uint8_t	byte2;
#define	SVFY_LUN_MASK	0xE0
#define	SVFY_RELADR	0x01
#define	SVFY_BYTCHK	0x02
#define	SVFY_DPO	0x10
	uint8_t	addr[4];	/* LBA to begin verification at */
	uint8_t	group;
	uint8_t	length[2];		/* number of blocks to verify */
	uint8_t	control;
};

struct scsi_verify_12
{
	uint8_t	opcode;		/* VERIFY(12) */
	uint8_t	byte2;
	uint8_t	addr[4];	/* LBA to begin verification at */
	uint8_t	length[4];		/* number of blocks to verify */
	uint8_t	group;
	uint8_t	control;
};

struct scsi_verify_16
{
	uint8_t	opcode;		/* VERIFY(16) */
	uint8_t	byte2;
	uint8_t	addr[8];	/* LBA to begin verification at */
	uint8_t	length[4];		/* number of blocks to verify */
	uint8_t	group;
	uint8_t	control;
};

struct scsi_compare_and_write
{
	uint8_t	opcode;		/* COMPARE AND WRITE */
	uint8_t	byte2;
	uint8_t	addr[8];	/* LBA to begin verification at */
	uint8_t	reserved[3];
	uint8_t	length;		/* number of blocks */
	uint8_t	group;
	uint8_t	control;
};

struct scsi_write_and_verify
{
	uint8_t	opcode;		/* WRITE_AND_VERIFY */
	uint8_t	byte2;
#define	SWVY_LUN_MASK	0xE0
#define	SWVY_RELADR	0x01
#define	SWVY_BYTECHK	0x02
#define	SWVY_DPO	0x10
	uint8_t	addr[4];	/* LBA to begin verification at */
	uint8_t	reserved0[1];
	uint8_t	len[2];		/* number of blocks to write and verify */
	uint8_t	reserved1[3];
};

/*
 * Replies to READ_FORMAT_CAPACITIES look like this:
 *
 * struct format_capacity_list_header
 * struct format_capacity_descriptor[1..n]
 *
 * These are similar, but not totally identical to, the
 * defect list used to format a rigid disk.
 *
 * The appropriate csio_decode() format string looks like this:
 * "{} *i3 {Len} i1 {Blocks} i4 {} *b6 {Code} b2 {Blocklen} i3"
 *
 * If the capacity_list_length is greater than
 * sizeof(struct format_capacity_descriptor), then there are
 * additional format capacity descriptors available which
 * denote which format(s) the drive can handle.
 *
 * (Source: USB Mass Storage UFI Specification)
 */

struct format_capacity_list_header {
	uint8_t	unused[3];
	uint8_t	capacity_list_length;
};

struct format_capacity_descriptor {
	uint8_t	nblocks[4];	/* total number of LBAs */
	uint8_t	byte4;		/* only present in max/cur descriptor */
#define FCD_CODE_MASK	0x03	/* mask for code field above */
#define FCD_UNFORMATTED	0x01	/* unformatted media present,
				 * maximum capacity returned */
#define FCD_FORMATTED	0x02	/* formatted media present,
				 * current capacity returned */
#define FCD_NOMEDIA	0x03	/* no media present,
				 * maximum device capacity returned */
	uint8_t	block_length[3];	/* length of an LBA in bytes */
};

struct scsi_reassign_blocks_data
{
	u_int8_t reserved[2];
	u_int8_t length[2];
	struct {
		u_int8_t dlbaddr[4];	/* defect logical block address */
	} defect_descriptor[1];
};


/*
 * This is the list header for the READ DEFECT DATA(10) command above.
 * It may be a bit wrong to append the 10 at the end of the data structure,
 * since it's only 4 bytes but it does tie it to the 10 byte command.
 */
struct scsi_read_defect_data_hdr_10
{
	u_int8_t reserved;
#define SRDDH10_GLIST 0x08
#define SRDDH10_PLIST 0x10
#define SRDDH10_DLIST_FORMAT_MASK 0x07
#define SRDDH10_BLOCK_FORMAT            0x00
#define SRDDH10_BYTES_FROM_INDEX_FORMAT 0x04
#define SRDDH10_PHYSICAL_SECTOR_FORMAT  0x05
	u_int8_t format;
	u_int8_t length[2];
#define	SRDDH10_MAX_LENGTH	SRDD10_MAX_LENGTH -			     \
				sizeof(struct scsi_read_defect_data_hdr_10) 
};

struct scsi_defect_desc_block
{
	u_int8_t address[4];
};

struct scsi_defect_desc_long_block
{
	u_int8_t address[8];
};

struct scsi_defect_desc_bytes_from_index
{
	u_int8_t cylinder[3];
	u_int8_t head;
#define	SDD_EXT_BFI_MADS		0x80000000
#define	SDD_EXT_BFI_FLAG_MASK		0xf0000000
#define	SDD_EXT_BFI_ENTIRE_TRACK	0x0fffffff
	u_int8_t bytes_from_index[4];
};

struct scsi_defect_desc_phys_sector
{
	u_int8_t cylinder[3];
	u_int8_t head;
#define	SDD_EXT_PHYS_MADS		0x80000000
#define	SDD_EXT_PHYS_FLAG_MASK		0xf0000000
#define	SDD_EXT_PHYS_ENTIRE_TRACK	0x0fffffff
	u_int8_t sector[4];
};

struct scsi_read_defect_data_hdr_12
{
	u_int8_t reserved;
#define SRDDH12_GLIST 0x08
#define SRDDH12_PLIST 0x10
#define SRDDH12_DLIST_FORMAT_MASK 0x07
#define SRDDH12_BLOCK_FORMAT            0x00
#define SRDDH12_BYTES_FROM_INDEX_FORMAT 0x04
#define SRDDH12_PHYSICAL_SECTOR_FORMAT  0x05
	u_int8_t format;
	u_int8_t generation[2];
	u_int8_t length[4];
#define	SRDDH12_MAX_LENGTH	SRDD12_MAX_LENGTH -			    \
				sizeof(struct scsi_read_defect_data_hdr_12)
};

union	disk_pages /* this is the structure copied from osf */
{
	struct format_device_page {
		u_int8_t pg_code;	/* page code (should be 3)	      */
#define	SMS_FORMAT_DEVICE_PAGE	0x03	/* only 6 bits valid */
		u_int8_t pg_length;	/* page length (should be 0x16)	      */
#define	SMS_FORMAT_DEVICE_PLEN	0x16
		u_int8_t trk_z_1;	/* tracks per zone (MSB)	      */
		u_int8_t trk_z_0;	/* tracks per zone (LSB)	      */
		u_int8_t alt_sec_1;	/* alternate sectors per zone (MSB)   */
		u_int8_t alt_sec_0;	/* alternate sectors per zone (LSB)   */
		u_int8_t alt_trk_z_1;	/* alternate tracks per zone (MSB)    */
		u_int8_t alt_trk_z_0;	/* alternate tracks per zone (LSB)    */
		u_int8_t alt_trk_v_1;	/* alternate tracks per volume (MSB)  */
		u_int8_t alt_trk_v_0;	/* alternate tracks per volume (LSB)  */
		u_int8_t ph_sec_t_1;	/* physical sectors per track (MSB)   */
		u_int8_t ph_sec_t_0;	/* physical sectors per track (LSB)   */
		u_int8_t bytes_s_1;	/* bytes per sector (MSB)	      */
		u_int8_t bytes_s_0;	/* bytes per sector (LSB)	      */
		u_int8_t interleave_1;	/* interleave (MSB)		      */
		u_int8_t interleave_0;	/* interleave (LSB)		      */
		u_int8_t trk_skew_1;	/* track skew factor (MSB)	      */
		u_int8_t trk_skew_0;	/* track skew factor (LSB)	      */
		u_int8_t cyl_skew_1;	/* cylinder skew (MSB)		      */
		u_int8_t cyl_skew_0;	/* cylinder skew (LSB)		      */
		u_int8_t flags;		/* various */
#define			DISK_FMT_SURF	0x10
#define	       		DISK_FMT_RMB	0x20
#define			DISK_FMT_HSEC	0x40
#define			DISK_FMT_SSEC	0x80
		u_int8_t reserved21;
		u_int8_t reserved22;
		u_int8_t reserved23;
	} format_device;
	struct rigid_geometry_page {
		u_int8_t pg_code;	/* page code (should be 4)	      */
#define SMS_RIGID_GEOMETRY_PAGE 0x04
		u_int8_t pg_length;	/* page length (should be 0x16)	      */
#define SMS_RIGID_GEOMETRY_PLEN 0x16		
		u_int8_t ncyl_2;	/* number of cylinders (MSB)	      */
		u_int8_t ncyl_1;	/* number of cylinders 		      */
		u_int8_t ncyl_0;	/* number of cylinders (LSB)	      */
		u_int8_t nheads;	/* number of heads 		      */
		u_int8_t st_cyl_wp_2;	/* starting cyl., write precomp (MSB) */
		u_int8_t st_cyl_wp_1;	/* starting cyl., write precomp	      */
		u_int8_t st_cyl_wp_0;	/* starting cyl., write precomp (LSB) */
		u_int8_t st_cyl_rwc_2;	/* starting cyl., red. write cur (MSB)*/
		u_int8_t st_cyl_rwc_1;	/* starting cyl., red. write cur      */
		u_int8_t st_cyl_rwc_0;	/* starting cyl., red. write cur (LSB)*/
		u_int8_t driv_step_1;	/* drive step rate (MSB)	      */
		u_int8_t driv_step_0;	/* drive step rate (LSB)	      */
		u_int8_t land_zone_2;	/* landing zone cylinder (MSB)	      */
		u_int8_t land_zone_1;	/* landing zone cylinder 	      */
		u_int8_t land_zone_0;	/* landing zone cylinder (LSB)	      */
		u_int8_t rpl;		/* rotational position locking (2 bits) */
		u_int8_t rot_offset;	/* rotational offset */
		u_int8_t reserved19;
		u_int8_t medium_rot_rate_1; /* medium rotation rate (RPM) (MSB) */
		u_int8_t medium_rot_rate_0; /* medium rotation rate (RPM) (LSB) */
		u_int8_t reserved22;
		u_int8_t reserved23;
    	} rigid_geometry;
	struct flexible_disk_page {
		u_int8_t pg_code;	/* page code (should be 5)	      */
#define SMS_FLEXIBLE_GEOMETRY_PAGE 0x05
		u_int8_t pg_length;	/* page length (should be 0x1E)	      */
#define SMS_FLEXIBLE_GEOMETRY_PLEN 0x1E
		u_int8_t xfr_rate_1;	/* transfer rate (MSB)		      */
		u_int8_t xfr_rate_0;	/* transfer rate (LSB)		      */
		u_int8_t nheads;	/* number of heads 		      */
		u_int8_t sec_per_track;	/* Sectors per track		      */
		u_int8_t bytes_s_1;	/* bytes per sector (MSB)	      */
		u_int8_t bytes_s_0;	/* bytes per sector (LSB)	      */
		u_int8_t ncyl_1;	/* number of cylinders (MSB)	      */
		u_int8_t ncyl_0;	/* number of cylinders (LSB)	      */
		u_int8_t st_cyl_wp_1;	/* starting cyl., write precomp (MSB) */
		u_int8_t st_cyl_wp_0;	/* starting cyl., write precomp (LSB) */
		u_int8_t st_cyl_rwc_1;	/* starting cyl., red. write cur (MSB)*/
		u_int8_t st_cyl_rwc_0;	/* starting cyl., red. write cur (LSB)*/		
		u_int8_t driv_step_1;	/* drive step rate (MSB)	      */
		u_int8_t driv_step_0;	/* drive step rate (LSB)	      */
		u_int8_t driv_step_pw;	/* drive step pulse width	      */
		u_int8_t head_stl_del_1;/* Head settle delay (MSB)	      */
		u_int8_t head_stl_del_0;/* Head settle delay (LSB)	      */
		u_int8_t motor_on_del;	/* Motor on delay		      */
		u_int8_t motor_off_del;	/* Motor off delay		      */
		u_int8_t trdy_ssn_mo;	/* XXX ??? */
		u_int8_t spc;		/* XXX ??? */
		u_int8_t write_comp;	/* Write compensation */
		u_int8_t head_load_del; /* Head load delay */
		u_int8_t head_uload_del;/* Head un-load delay */
		u_int8_t pin32_pin2;
		u_int8_t pin4_pint1;
		u_int8_t medium_rot_rate_1; /* medium rotation rate (RPM) (MSB) */
		u_int8_t medium_rot_rate_0; /* medium rotation rate (RPM) (LSB) */		
		u_int8_t reserved30;
		u_int8_t reserved31;
    	} flexible_disk;	
};

/*
 * XXX KDM
 * Here for CTL compatibility, reconcile this.
 */
struct scsi_format_page {
	uint8_t page_code;
	uint8_t page_length;
	uint8_t tracks_per_zone[2];
	uint8_t alt_sectors_per_zone[2];
	uint8_t alt_tracks_per_zone[2];
	uint8_t alt_tracks_per_lun[2];
	uint8_t sectors_per_track[2];
	uint8_t bytes_per_sector[2];
	uint8_t interleave[2];
	uint8_t track_skew[2];
	uint8_t cylinder_skew[2];
	uint8_t flags;
#define	SFP_SSEC	0x80
#define	SFP_HSEC	0x40
#define	SFP_RMB		0x20
#define	SFP_SURF	0x10
	uint8_t reserved[3];
};

/*
 * XXX KDM
 * Here for CTL compatibility, reconcile this.
 */
struct scsi_rigid_disk_page {
	uint8_t page_code;
#define	SMS_RIGID_DISK_PAGE		0x04
	uint8_t page_length;
	uint8_t cylinders[3];
	uint8_t heads;
	uint8_t start_write_precomp[3];
	uint8_t start_reduced_current[3];
	uint8_t step_rate[2];
	uint8_t landing_zone_cylinder[3];
	uint8_t rpl;
#define	SRDP_RPL_DISABLED	0x00
#define	SRDP_RPL_SLAVE		0x01
#define	SRDP_RPL_MASTER		0x02
#define	SRDP_RPL_MASTER_CONTROL	0x03
	uint8_t rotational_offset;
	uint8_t reserved1;
	uint8_t rotation_rate[2];
	uint8_t reserved2[2];
};


struct scsi_da_rw_recovery_page {
	u_int8_t page_code;
#define SMS_RW_ERROR_RECOVERY_PAGE	0x01
	u_int8_t page_length;
	u_int8_t byte3;
#define SMS_RWER_AWRE			0x80
#define SMS_RWER_ARRE			0x40
#define SMS_RWER_TB			0x20
#define SMS_RWER_RC			0x10
#define SMS_RWER_EER			0x08
#define SMS_RWER_PER			0x04
#define SMS_RWER_DTE			0x02
#define SMS_RWER_DCR			0x01
	u_int8_t read_retry_count;
	u_int8_t correction_span;
	u_int8_t head_offset_count;
	u_int8_t data_strobe_offset_cnt;
	u_int8_t byte8;
#define SMS_RWER_LBPERE			0x80
	u_int8_t write_retry_count;
	u_int8_t reserved2;
	u_int8_t recovery_time_limit[2];
};

struct scsi_da_verify_recovery_page {
	u_int8_t page_code;
#define SMS_VERIFY_ERROR_RECOVERY_PAGE	0x07
	u_int8_t page_length;
	u_int8_t byte3;
#define SMS_VER_EER			0x08
#define SMS_VER_PER			0x04
#define SMS_VER_DTE			0x02
#define SMS_VER_DCR			0x01
	u_int8_t read_retry_count;
	u_int8_t reserved[6];
	u_int8_t recovery_time_limit[2];
};

__BEGIN_DECLS
/*
 * XXX These are only left out of the kernel build to silence warnings.  If,
 * for some reason these functions are used in the kernel, the ifdefs should
 * be moved so they are included both in the kernel and userland.
 */
#ifndef _KERNEL
void scsi_format_unit(struct ccb_scsiio *csio, u_int32_t retries,
		      void (*cbfcnp)(struct cam_periph *, union ccb *),
		      u_int8_t tag_action, u_int8_t byte2, u_int16_t ileave,
		      u_int8_t *data_ptr, u_int32_t dxfer_len,
		      u_int8_t sense_len, u_int32_t timeout);

void scsi_read_defects(struct ccb_scsiio *csio, uint32_t retries,
		       void (*cbfcnp)(struct cam_periph *, union ccb *),
		       uint8_t tag_action, uint8_t list_format,
		       uint32_t addr_desc_index, uint8_t *data_ptr,
		       uint32_t dxfer_len, int minimum_cmd_size, 
		       uint8_t sense_len, uint32_t timeout);

void scsi_sanitize(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action, u_int8_t byte2, u_int16_t control,
		   u_int8_t *data_ptr, u_int32_t dxfer_len, u_int8_t sense_len,
		   u_int32_t timeout);

#endif /* !_KERNEL */

void scsi_zbc_out(struct ccb_scsiio *csio, uint32_t retries, 
		  void (*cbfcnp)(struct cam_periph *, union ccb *),
		  uint8_t tag_action, uint8_t service_action, uint64_t zone_id,
		  uint8_t zone_flags, uint8_t *data_ptr, uint32_t dxfer_len,
		  uint8_t sense_len, uint32_t timeout);

void scsi_zbc_in(struct ccb_scsiio *csio, uint32_t retries, 
		 void (*cbfcnp)(struct cam_periph *, union ccb *),
		 uint8_t tag_action, uint8_t service_action,
		 uint64_t zone_start_lba, uint8_t zone_options,
		 uint8_t *data_ptr, uint32_t dxfer_len, uint8_t sense_len,
		 uint32_t timeout);

int scsi_ata_zac_mgmt_out(struct ccb_scsiio *csio, uint32_t retries, 
			  void (*cbfcnp)(struct cam_periph *, union ccb *),
			  uint8_t tag_action, int use_ncq,
			  uint8_t zm_action, uint64_t zone_id,
			  uint8_t zone_flags, uint8_t *data_ptr,
			  uint32_t dxfer_len, uint8_t *cdb_storage,
			  size_t cdb_storage_len, uint8_t sense_len,
			  uint32_t timeout);

int scsi_ata_zac_mgmt_in(struct ccb_scsiio *csio, uint32_t retries, 
			 void (*cbfcnp)(struct cam_periph *, union ccb *),
			 uint8_t tag_action, int use_ncq,
			 uint8_t zm_action, uint64_t zone_id,
			 uint8_t zone_flags, uint8_t *data_ptr,
			 uint32_t dxfer_len, uint8_t *cdb_storage,
			 size_t cdb_storage_len, uint8_t sense_len,
			 uint32_t timeout);

__END_DECLS

#endif /* _SCSI_SCSI_DA_H */
