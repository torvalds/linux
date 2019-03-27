/*-
 * Structure and function declarations for the
 * SCSI Sequential Access Peripheral driver for CAM.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999, 2000 Matthew Jacob
 * Copyright (c) 2013, 2014, 2015 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_SCSI_SCSI_SA_H
#define _SCSI_SCSI_SA_H 1

#include <sys/cdefs.h>

struct scsi_read_block_limits
{
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[3];
	u_int8_t control;
};

struct scsi_read_block_limits_data
{
	u_int8_t gran;
#define	RBL_GRAN_MASK	0x1F
#define RBL_GRAN(rblim) ((rblim)->gran & RBL_GRAN_MASK)
	u_int8_t maximum[3];
	u_int8_t minimum[2];
};

struct scsi_sa_rw
{
	u_int8_t opcode;
        u_int8_t sli_fixed;
#define SAR_SLI		0x02
#define SARW_FIXED	0x01
	u_int8_t length[3];
        u_int8_t control;
};

struct scsi_load_unload
{
	u_int8_t opcode;
        u_int8_t immediate;
#define SLU_IMMED	0x01
	u_int8_t reserved[2];
	u_int8_t eot_reten_load;
#define SLU_EOT		0x04
#define SLU_RETEN	0x02
#define SLU_LOAD	0x01
        u_int8_t control;
};

struct scsi_rewind
{
	u_int8_t opcode;
        u_int8_t immediate;
#define SREW_IMMED	0x01
	u_int8_t reserved[3];
        u_int8_t control;
};

typedef enum {
	SS_BLOCKS,
	SS_FILEMARKS,
	SS_SEQFILEMARKS,
	SS_EOD,
	SS_SETMARKS,
	SS_SEQSETMARKS
} scsi_space_code;

struct scsi_space
{
	u_int8_t opcode;
        u_int8_t code;
#define SREW_IMMED	0x01
	u_int8_t count[3];
        u_int8_t control;
};

struct scsi_write_filemarks
{
	u_int8_t opcode;
        u_int8_t byte2;
#define SWFMRK_IMMED	0x01
#define SWFMRK_WSMK	0x02
	u_int8_t num_marks[3];
        u_int8_t control;
};

/*
 * Reserve and release unit have the same exact cdb format, but different
 * opcodes.
 */
struct scsi_reserve_release_unit
{
	u_int8_t opcode;
	u_int8_t lun_thirdparty;
#define SRRU_LUN_MASK	0xE0
#define SRRU_3RD_PARTY	0x10
#define SRRU_3RD_SHAMT	1
#define SRRU_3RD_MASK	0xE
	u_int8_t reserved[3];
	u_int8_t control;
};

/*
 * Erase a tape
 */
struct scsi_erase
{
	u_int8_t opcode;
	u_int8_t lun_imm_long;
#define SE_LUN_MASK	0xE0
#define SE_LONG		0x1
#define SE_IMMED	0x2
	u_int8_t reserved[3];
	u_int8_t control;
};

/*
 * Set tape capacity.
 */
struct scsi_set_capacity
{
	u_int8_t opcode;
	u_int8_t byte1;
#define	SA_SSC_IMMED		0x01
	u_int8_t reserved;
	u_int8_t cap_proportion[2];
	u_int8_t control;
};

/*
 * Format tape media.  The CDB opcode is the same as the disk-specific
 * FORMAT UNIT command, but the fields are different inside the CDB.  Thus
 * the reason for a separate definition here.
 */
struct scsi_format_medium
{
	u_int8_t opcode;
	u_int8_t byte1;
#define	SFM_IMMED		0x01
#define	SFM_VERIFY		0x02
	u_int8_t byte2;
#define	SFM_FORMAT_DEFAULT	0x00
#define	SFM_FORMAT_PARTITION	0x01
#define	SFM_FORMAT_DEF_PART	0x02
#define	SFM_FORMAT_MASK		0x0f
	u_int8_t length[2];
	u_int8_t control;
};

struct scsi_allow_overwrite
{
	u_int8_t opcode;
	u_int8_t reserved1;
	u_int8_t allow_overwrite;
#define	SAO_ALLOW_OVERWRITE_DISABLED	0x00
#define	SAO_ALLOW_OVERWRITE_CUR_POS	0x01
#define	SAO_ALLOW_OVERWRITE_FORMAT	0x02
	u_int8_t partition;
	u_int8_t logical_id[8];
	u_int8_t reserved2[3];
	u_int8_t control;
};

/*
 * Dev specific mode page masks.
 */
#define SMH_SA_WP		0x80
#define	SMH_SA_BUF_MODE_MASK	0x70
#define SMH_SA_BUF_MODE_NOBUF	0x00
#define SMH_SA_BUF_MODE_SIBUF	0x10	/* Single-Initiator buffering */
#define SMH_SA_BUF_MODE_MIBUF	0x20	/* Multi-Initiator buffering */
#define SMH_SA_SPEED_MASK	0x0F
#define SMH_SA_SPEED_DEFAULT	0x00

/*
 * Sequential-access specific mode page numbers.
 */
#define SA_DEVICE_CONFIGURATION_PAGE	0x10
#define SA_MEDIUM_PARTITION_PAGE_1	0x11
#define SA_MEDIUM_PARTITION_PAGE_2	0x12
#define SA_MEDIUM_PARTITION_PAGE_3	0x13
#define SA_MEDIUM_PARTITION_PAGE_4	0x14
#define SA_DATA_COMPRESSION_PAGE	0x0f	/* SCSI-3 */

/*
 * Mode page definitions.
 */

/* See SCSI-II spec 9.3.3.1 */
struct scsi_dev_conf_page {
	u_int8_t pagecode;	/* 0x10 */
	u_int8_t pagelength;	/* 0x0e */
	u_int8_t byte2;		/* CAP, CAF, Active Format */
	u_int8_t active_partition;
	u_int8_t wb_full_ratio;
	u_int8_t rb_empty_ratio;
	u_int8_t wrdelay_time[2];
	u_int8_t byte8;
#define	SA_DBR			0x80	/* data buffer recovery */
#define	SA_BIS			0x40	/* block identifiers supported */
#define	SA_RSMK			0x20	/* report setmarks */
#define	SA_AVC			0x10	/* automatic velocity control */
#define	SA_SOCF_MASK		0x0c	/* stop on consecutive formats */
#define	SA_RBO			0x02	/* recover buffer order */
#define	SA_REW			0x01	/* report early warning */
	u_int8_t gap_size;
	u_int8_t byte10;
/* from SCSI-3: SSC-4 Working draft (2/14) 8.3.3 */
#define	SA_EOD_DEF_MASK		0xe0	/* EOD defined */
#define	SA_EEG			0x10	/* Enable EOD Generation */
#define	SA_SEW			0x08	/* Synchronize at Early Warning */
#define	SA_SOFT_WP		0x04	/* Software Write Protect */
#define	SA_BAML			0x02	/* Block Address Mode Lock */
#define	SA_BAM			0x01	/* Block Address Mode */
	u_int8_t ew_bufsize[3];
	u_int8_t sel_comp_alg;
#define	SA_COMP_NONE		0x00
#define	SA_COMP_DEFAULT		0x01
	/* the following is 'reserved' in SCSI-2 but is defined in SSC-r22 */
	u_int8_t extra_wp;
#define	SA_ASOC_WP		0x04	/* Associated Write Protect */
#define	SA_PERS_WP		0x02	/* Persistent Write Protect */
#define	SA_PERM_WP		0x01	/* Permanent Write Protect */
};

/* from SCSI-3: SSC-Rev10 (6/97) */
struct scsi_data_compression_page {
	u_int8_t page_code;	/* 0x0f */
	u_int8_t page_length;	/* 0x0e */
	u_int8_t dce_and_dcc;
#define SA_DCP_DCE		0x80 	/* Data compression enable */
#define SA_DCP_DCC		0x40	/* Data compression capable */
	u_int8_t dde_and_red;
#define SA_DCP_DDE		0x80	/* Data decompression enable */
#define SA_DCP_RED_MASK		0x60	/* Report Exception on Decomp. */
#define SA_DCP_RED_SHAMT	5
#define SA_DCP_RED_0		0x00
#define SA_DCP_RED_1		0x20
#define SA_DCP_RED_2		0x40
	u_int8_t comp_algorithm[4];
	u_int8_t decomp_algorithm[4];
	u_int8_t reserved[4];
};

typedef union {
	struct { u_int8_t pagecode, pagelength; } hdr;
	struct scsi_dev_conf_page dconf;
	struct scsi_data_compression_page dcomp;
} sa_comp_t;

/*
 * Control Data Protection subpage.  This is as defined in SSC3r03.
 */
struct scsi_control_data_prot_subpage {
	uint8_t page_code;
#define	SA_CTRL_DP_PAGE_CODE		0x0a
	uint8_t subpage_code;
#define	SA_CTRL_DP_SUBPAGE_CODE		0xf0
	uint8_t length[2];
	uint8_t prot_method;
#define	SA_CTRL_DP_NO_LBP		0x00
#define	SA_CTRL_DP_REED_SOLOMON		0x01
#define	SA_CTRL_DP_METHOD_MAX		0xff
	uint8_t pi_length;
#define	SA_CTRL_DP_PI_LENGTH_MASK	0x3f
#define	SA_CTRL_DP_RS_LENGTH		4
	uint8_t prot_bits;
#define	SA_CTRL_DP_LBP_W		0x80
#define	SA_CTRL_DP_LBP_R		0x40
#define	SA_CTRL_DP_RBDP			0x20
	uint8_t reserved[];
};

/*
 * This is the Read/Write Control mode page used on IBM Enterprise Tape
 * Drives.  They are known as 3592, TS, or Jaguar drives.  The SCSI inquiry
 * data will show a Product ID "03592XXX", where XXX is 'J1A', 'E05' (TS1120),
 * 'E06' (TS1130), 'E07' (TS1140) or 'E08' (TS1150).
 *
 * This page definition is current as of the 3592 SCSI Reference v6,
 * released on December 16th, 2014.
 */
struct scsi_tape_ibm_rw_control {
	uint8_t page_code;
#define SA_IBM_RW_CTRL_PAGE_CODE		0x25
	uint8_t page_length;
	uint8_t ignore_seq_checks;
#define	SA_IBM_RW_CTRL_LOC_IGNORE_SEQ		0x04
#define	SA_IBM_RW_CTRL_SPC_BLK_IGNORE_SEQ	0x02
#define	SA_IBM_RW_CTRL_SPC_FM_IGNORE_SEQ	0x01
	uint8_t ignore_data_checks;
#define	SA_IBM_RW_CTRL_LOC_IGNORE_DATA		0x04
#define	SA_IBM_RW_CTRL_SPC_BLK_IGNORE_DATA	0x02
#define	SA_IBM_RW_CTRL_SPC_FM_IGNORE_DATA	0x01
	uint8_t reserved1;
	uint8_t leop_method;
#define	SA_IBM_RW_CTRL_LEOP_DEFAULT		0x00
#define	SA_IBM_RW_CTRL_LEOP_MAX_CAP		0x01
#define	SA_IBM_RW_CTRL_LEOP_CONST_CAP		0x02
	uint8_t leop_ew[2];
	uint8_t byte8;
#define	SA_IBM_RW_CTRL_DISABLE_FASTSYNC		0x80
#define	SA_IBM_RW_CTRL_DISABLE_SKIPSYNC		0x40
#define	SA_IBM_RW_CTRL_DISABLE_CROSS_EOD	0x08
#define	SA_IBM_RW_CTRL_DISABLE_CROSS_PERM_ERR	0x04
#define	SA_IBM_RW_CTRL_REPORT_SEG_EW		0x02
#define	SA_IBM_RW_CTRL_REPORT_HOUSEKEEPING_ERR	0x01
	uint8_t default_write_dens_bop_0;
	uint8_t pending_write_dens_bop_0;
	uint8_t reserved2[21];
};

struct scsi_tape_read_position {
	u_int8_t opcode;		/* READ_POSITION */
	u_int8_t byte1;			/* set LSB to read hardware block pos */
#define	SA_RPOS_SHORT_FORM	0x00
#define	SA_RPOS_SHORT_VENDOR	0x01
#define	SA_RPOS_LONG_FORM	0x06
#define	SA_RPOS_EXTENDED_FORM	0x08
	u_int8_t reserved[5];
	u_int8_t length[2];
	u_int8_t control;
};

struct scsi_tape_position_data	{	/* Short Form */
	u_int8_t flags;
#define	SA_RPOS_BOP		0x80	/* Beginning of Partition */
#define	SA_RPOS_EOP		0x40	/* End of Partition */
#define	SA_RPOS_BCU		0x20	/* Block Count Unknown (SCSI3) */
#define	SA_RPOS_BYCU		0x10	/* Byte Count Unknown (SCSI3) */
#define	SA_RPOS_BPU		0x04	/* Block Position Unknown */
#define	SA_RPOS_PERR		0x02	/* Position Error (SCSI3) */
#define	SA_RPOS_BPEW		0x01	/* Beyond Programmable Early Warning */
#define	SA_RPOS_UNCERTAIN	SA_RPOS_BPU
	u_int8_t partition;
	u_int8_t reserved[2];
	u_int8_t firstblk[4];
	u_int8_t lastblk[4];
	u_int8_t reserved2;
	u_int8_t nbufblk[3];
	u_int8_t nbufbyte[4];
};

struct scsi_tape_position_long_data {
	u_int8_t flags;
#define	SA_RPOS_LONG_BOP	0x80	/* Beginning of Partition */
#define	SA_RPOS_LONG_EOP	0x40	/* End of Partition */
#define	SA_RPOS_LONG_MPU	0x08	/* Mark Position Unknown */
#define	SA_RPOS_LONG_LONU	0x04	/* Logical Object Number Unknown */
#define	SA_RPOS_LONG_BPEW	0x01	/* Beyond Programmable Early Warning */
	u_int8_t reserved[3];
	u_int8_t partition[4];
	u_int8_t logical_object_num[8];
	u_int8_t logical_file_num[8];
	u_int8_t set_id[8];
};

struct scsi_tape_position_ext_data {
	u_int8_t flags;
#define	SA_RPOS_EXT_BOP		0x80	/* Beginning of Partition */
#define	SA_RPOS_EXT_EOP		0x40	/* End of Partition */
#define	SA_RPOS_EXT_LOCU	0x20	/* Logical Object Count Unknown */
#define	SA_RPOS_EXT_BYCU	0x10	/* Byte Count Unknown */
#define	SA_RPOS_EXT_LOLU	0x04	/* Logical Object Location Unknown */
#define	SA_RPOS_EXT_PERR	0x02	/* Position Error */
#define	SA_RPOS_EXT_BPEW	0x01	/* Beyond Programmable Early Warning */
	u_int8_t partition;
	u_int8_t length[2];
	u_int8_t reserved;
	u_int8_t num_objects[3];
	u_int8_t first_object[8];
	u_int8_t last_object[8];
	u_int8_t bytes_in_buffer[8];
};

struct scsi_tape_locate {
	u_int8_t opcode;
	u_int8_t byte1;
#define	SA_SPOS_IMMED		0x01
#define	SA_SPOS_CP		0x02
#define	SA_SPOS_BT		0x04
	u_int8_t reserved1;
	u_int8_t blkaddr[4];
#define	SA_SPOS_MAX_BLK		0xffffffff
	u_int8_t reserved2;
	u_int8_t partition;
	u_int8_t control;
};

struct scsi_locate_16 {
	u_int8_t opcode;
	u_int8_t byte1;
#define	SA_LC_IMMEDIATE		0x01
#define	SA_LC_CP		0x02
#define	SA_LC_DEST_TYPE_MASK	0x38
#define	SA_LC_DEST_TYPE_SHIFT	3
#define	SA_LC_DEST_OBJECT	0x00
#define	SA_LC_DEST_FILE		0x01
#define	SA_LC_DEST_SET		0x02
#define	SA_LC_DEST_EOD		0x03
	u_int8_t byte2;
#define	SA_LC_BAM_IMPLICIT	0x00
#define	SA_LC_BAM_EXPLICIT	0x01
	u_int8_t partition;
	u_int8_t logical_id[8];
	u_int8_t reserved[3];
	u_int8_t control;
};

struct scsi_report_density_support {
	u_int8_t opcode;
	u_int8_t byte1;
#define	SRDS_MEDIA		0x01
#define	SRDS_MEDIUM_TYPE	0x02
	u_int8_t reserved[5];
	u_int8_t length[2];
#define	SRDS_MAX_LENGTH		0xffff
	u_int8_t control;
};

struct scsi_density_hdr {
	u_int8_t length[2];
	u_int8_t reserved[2];
	u_int8_t descriptor[];
};

struct scsi_density_data {
	u_int8_t primary_density_code;
	u_int8_t secondary_density_code;
	u_int8_t byte2;
#define	SDD_DLV			0x01
#define	SDD_DEFLT		0x20
#define	SDD_DUP			0x40
#define SDD_WRTOK		0x80
	u_int8_t length[2];
#define	SDD_DEFAULT_LENGTH	52
	u_int8_t bits_per_mm[3];
	u_int8_t media_width[2];
	u_int8_t tracks[2];
	u_int8_t capacity[4];
	u_int8_t assigning_org[8];
	u_int8_t density_name[8];
	u_int8_t description[20];
};

struct scsi_medium_type_data {
	u_int8_t medium_type;
	u_int8_t reserved1;
	u_int8_t length[2];
#define	SMTD_DEFAULT_LENGTH	52
	u_int8_t num_density_codes;
	u_int8_t primary_density_codes[9];
	u_int8_t media_width[2];
	u_int8_t medium_length[2];
	u_int8_t reserved2[2];
	u_int8_t assigning_org[8];
	u_int8_t medium_type_name[8];
	u_int8_t description[20];
};

/*
 * Manufacturer-assigned Serial Number VPD page.
 * Current as of SSC-5r03, 28 September 2016.
 */
struct scsi_vpd_mfg_serial_number
{
	u_int8_t device;
	u_int8_t page_code;
#define	SVPD_MFG_SERIAL_NUMBER_PAGE_CODE 0xB1
	u_int8_t page_length[2];
	u_int8_t mfg_serial_num[];
};

/*
 * Security Protocol Specific values for the Tape Data Encryption protocol
 * (0x20) used with SECURITY PROTOCOL IN.  See below for values used with
 * SECURITY PROTOCOL OUT.  Current as of SSC4r03.
 */
#define	TDE_IN_SUPPORT_PAGE		0x0000
#define	TDE_OUT_SUPPORT_PAGE		0x0001
#define	TDE_DATA_ENC_CAP_PAGE		0x0010
#define	TDE_SUPPORTED_KEY_FORMATS_PAGE	0x0011
#define	TDE_DATA_ENC_MAN_CAP_PAGE	0x0012
#define	TDE_DATA_ENC_STATUS_PAGE	0x0020
#define	TDE_NEXT_BLOCK_ENC_STATUS_PAGE	0x0021
#define	TDE_GET_ENC_MAN_ATTR_PAGE	0x0022
#define	TDE_RANDOM_NUM_PAGE		0x0030
#define	TDE_KEY_WRAP_PK_PAGE		0x0031

/*
 * Tape Data Encryption protocol pages used with SECURITY PROTOCOL IN and
 * SECURITY PROTOCOL OUT.
 */
/*
 * Tape Data Encryption In Support page (0x0000).
 */
struct tde_in_support_page {
	uint8_t page_code[2];
	uint8_t page_length[2];
	uint8_t page_codes[];
};

/*
 * Tape Data Encryption Out Support page (0x0001).
 */
struct tde_out_support_page {
	uint8_t page_code[2];
	uint8_t page_length[2];
	uint8_t page_codes[];
};

/*
 * Logical block encryption algorithm descriptor.  This is reported in the
 * Data Encryption Capabilities page.
 */
struct tde_block_enc_alg_desc {
	uint8_t alg_index;
	uint8_t reserved1;
	uint8_t desc_length[2];
	uint8_t byte4;
#define	TDE_BEA_AVFMV			0x80
#define	TDE_BEA_SDK_C			0x40
#define	TDE_BEA_MAC_C			0x20
#define	TDE_BEA_DELB_C			0x10
#define	TDE_BEA_DECRYPT_C_MASK		0x0c
#define	TDE_BEA_DECRYPT_C_EXT		0x0c
#define	TDE_BEA_DECRYPT_C_HARD		0x08
#define	TDE_BEA_DECRYPT_C_SOFT		0x04
#define	TDE_BEA_DECRYPT_C_NO_CAP	0x00
#define	TDE_BEA_ENCRYPT_C_MASK		0x03
#define	TDE_BEA_ENCRYPT_C_EXT		0x03
#define	TDE_BEA_ENCRYPT_C_HARD		0x02
#define	TDE_BEA_ENCRYPT_C_SOFT		0x01
#define	TDE_BEA_ENCRYPT_C_NO_CAP	0x00
	uint8_t byte5;
#define	TDE_BEA_AVFCLP_MASK		0xc0
#define	TDE_BEA_AVFCLP_VALID		0x80
#define	TDE_BEA_AVFCLP_NOT_VALID	0x40
#define	TDE_BEA_AVFCLP_NOT_APP		0x00
#define	TDE_BEA_NONCE_C_MASK		0x30
#define	TDE_BEA_NONCE_C_SUPPORTED	0x30
#define	TDE_BEA_NONCE_C_PROVIDED	0x20
#define	TDE_BEA_NONCE_C_GENERATED	0x10
#define	TDE_BEA_NONCE_C_NOT_REQUIRED	0x00
#define	TDE_BEA_KADF_C			0x08
#define	TDE_BEA_VCELB_C			0x04
#define	TDE_BEA_UKADF			0x02
#define	TDE_BEA_AKADF			0x01
	uint8_t max_unauth_key_bytes[2];
	uint8_t max_auth_key_bytes[2];
	uint8_t lbe_key_size[2];
	uint8_t byte12;
#define	TDE_BEA_DKAD_C_MASK		0xc0
#define	TDE_BEA_DKAD_C_CAPABLE		0xc0
#define	TDE_BEA_DKAD_C_NOT_ALLOWED	0x80
#define	TDE_BEA_DKAD_C_REQUIRED		0x40
#define	TDE_BEA_EEMC_C_MASK		0x30
#define	TDE_BEA_EEMC_C_ALLOWED		0x20
#define	TDE_BEA_EEMC_C_NOT_ALLOWED	0x10
#define	TDE_BEA_EEMC_C_NOT_SPECIFIED	0x00
	/*
	 * Raw Decryption Mode Control Capabilities (RDMC_C) field.  The
	 * descriptions are too complex to represent as a simple name.
	 */
#define	TDE_BEA_RDMC_C_MASK		0x0e
#define	TDE_BEA_RDMC_C_MODE_7		0x0e
#define	TDE_BEA_RDMC_C_MODE_6		0x0c
#define	TDE_BEA_RDMC_C_MODE_5		0x0a
#define	TDE_BEA_RDMC_C_MODE_4		0x08
#define	TDE_BEA_RDMC_C_MODE_1		0x02
#define	TDE_BEA_EAREM			0x01
	uint8_t byte13;
#define	TDE_BEA_MAX_EEDKS_MASK		0x0f
	uint8_t msdk_count[2];
	uint8_t max_eedk_size[2];
	uint8_t reserved2[2];
	uint8_t security_algo_code[4];
};

/*
 * Data Encryption Capabilities page (0x0010).
 */
struct tde_data_enc_cap_page {
	uint8_t page_code[2];
	uint8_t page_length;
	uint8_t byte4;
#define	DATA_ENC_CAP_EXTDECC_MASK		0x0c
#define	DATA_ENC_CAP_EXTDECC_NOT_REPORTED	0x00
#define	DATA_ENC_CAP_EXTDECC_NOT_CAPABLE	0x04
#define	DATA_ENC_CAP_EXTDECC_CAPABLE		0x08
#define	DATA_ENC_CAP_CFG_P_MASK			0x03
#define	DATA_ENC_CAP_CFG_P_NOT_REPORTED		0x00
#define	DATA_ENC_CAP_CFG_P_ALLOWED		0x01
#define	DATA_ENC_CAP_CFG_P_NOT_ALLOWED		0x02
	uint8_t reserved[15];
	struct tde_block_enc_alg_desc alg_descs[];
};

/*
 * Tape Data Encryption Supported Key Formats page (0x0011).
 */
struct tde_supported_key_formats_page {
	uint8_t page_code[2];
	uint8_t page_length[2];
	uint8_t key_formats_list[];
};

/*
 * Tape Data Encryption Management Capabilities page (0x0012).
 */
struct tde_data_enc_man_cap_page {
	uint8_t page_code[2];
	uint8_t page_length[2];
	uint8_t byte4;
#define	TDE_DEMC_LOCK_C		0x01
	uint8_t byte5;
#define	TDE_DEMC_CKOD_C		0x04
#define	TDE_DEMC_CKORP_C	0x02
#define	TDE_DEMC_CKORL_C	0x01
	uint8_t reserved1;
	uint8_t byte7;
#define	TDE_DEMC_AITN_C		0x04
#define	TDE_DEMC_LOCAL_C	0x02
#define	TDE_DEMC_PUBLIC_C	0x01
	uint8_t reserved2[8];
};

/*
 * Tape Data Encryption Status Page (0x0020).
 */
struct tde_data_enc_status_page {
	uint8_t page_code[2];
	uint8_t page_length[2];
	uint8_t scope;
#define	TDE_DES_IT_NEXUS_SCOPE_MASK	0xe0
#define	TDE_DES_LBE_SCOPE_MASK		0x07
	uint8_t encryption_mode;
	uint8_t decryption_mode;
	uint8_t algo_index;
	uint8_t key_instance_counter[4];
	uint8_t byte12;
#define	TDE_DES_PARAM_CTRL_MASK		0x70
#define	TDE_DES_PARAM_CTRL_MGMT		0x40
#define	TDE_DES_PARAM_CTRL_CHANGER	0x30
#define	TDE_DES_PARAM_CTRL_DRIVE	0x20
#define	TDE_DES_PARAM_CTRL_EXT		0x10
#define	TDE_DES_PARAM_CTRL_NOT_REPORTED	0x00
#define	TDE_DES_VCELB			0x08
#define	TDE_DES_CEEMS_MASK		0x06
#define	TDE_DES_RDMD			0x01
	uint8_t enc_params_kad_format;
	uint8_t asdk_count[2];
	uint8_t reserved[8];
	uint8_t key_assoc_data_desc[];
};

/*
 * Tape Data Encryption Next Block Encryption Status page (0x0021).
 */
struct tde_next_block_enc_status_page {
	uint8_t page_code[2];
	uint8_t page_length[2];
	uint8_t logical_obj_number[8];
	uint8_t status;
#define	TDE_NBES_COMP_STATUS_MASK	0xf0
#define	TDE_NBES_COMP_INCAPABLE		0x00
#define	TDE_NBES_COMP_NOT_YET		0x10
#define	TDE_NBES_COMP_NOT_A_BLOCK	0x20
#define	TDE_NBES_COMP_NOT_COMPRESSED	0x30
#define	TDE_NBES_COMP_COMPRESSED	0x40
#define	TDE_NBES_ENC_STATUS_MASK	0x0f
#define	TDE_NBES_ENC_INCAPABLE		0x00
#define	TDE_NBES_ENC_NOT_YET		0x01
#define	TDE_NBES_ENC_NOT_A_BLOCK	0x02
#define	TDE_NBES_ENC_NOT_ENCRYPTED	0x03
#define	TDE_NBES_ENC_ALG_NOT_SUPPORTED	0x04
#define	TDE_NBES_ENC_SUPPORTED_ALG	0x05
#define	TDE_NBES_ENC_NO_KEY		0x06
	uint8_t algo_index;
	uint8_t byte14;
#define	TDE_NBES_EMES			0x02
#define	TDE_NBES_RDMDS			0x01
	uint8_t next_block_kad_format;
	uint8_t key_assoc_data_desc[];
};

/*
 * Tape Data Encryption Get Encryption Management Attributes page (0x0022).
 */
struct tde_get_enc_man_attr_page {
	uint8_t page_code[2];
	uint8_t reserved[3];
	uint8_t byte5;
#define	TDE_GEMA_CAOD			0x01
	uint8_t page_length[2];
	uint8_t enc_mgmt_attr_desc[];
};

/*
 * Tape Data Encryption Random Number page (0x0030).
 */
struct tde_random_num_page {
	uint8_t page_code[2];
	uint8_t page_length[2];
	uint8_t random_number[32];
};

/*
 * Tape Data Encryption Device Server Key Wrapping Public Key page (0x0031).
 */
struct tde_key_wrap_pk_page {
	uint8_t page_code[2];
	uint8_t page_length[2];
	uint8_t public_key_type[4];
	uint8_t public_key_format[4];
	uint8_t public_key_length[2];
	uint8_t public_key[];
};

/*
 * Security Protocol Specific values for the Tape Data Encryption protocol
 * (0x20) used with SECURITY PROTOCOL OUT.  See above for values used with
 * SECURITY PROTOCOL IN.  Current as of SSCr03.
 */
#define	TDE_SET_DATA_ENC_PAGE		0x0010
#define	TDE_SA_ENCAP_PAGE		0x0011
#define	TDE_SET_ENC_MGMT_ATTR_PAGE	0x0022

/*
 * Tape Data Encryption Set Data Encryption page (0x0010).
 */
struct tde_set_data_enc_page {
	uint8_t page_code[2];
	uint8_t page_length[2];
	uint8_t byte4;
#define	TDE_SDE_SCOPE_MASK		0xe0
#define	TDE_SDE_SCOPE_ALL_IT_NEXUS	0x80
#define	TDE_SDE_SCOPE_LOCAL		0x40
#define	TDE_SDE_SCOPE_PUBLIC		0x00
#define	TDE_SDE_LOCK			0x01
	uint8_t byte5;
#define	TDE_SDE_CEEM_MASK		0xc0
#define	TDE_SDE_CEEM_ENCRYPT		0xc0
#define	TDE_SDE_CEEM_EXTERNAL		0x80
#define	TDE_SDE_CEEM_NO_CHECK		0x40
#define	TDE_SDE_RDMC_MASK		0x30
#define	TDE_SDE_RDMC_DISABLED		0x30
#define	TDE_SDE_RDMC_ENABLED		0x20
#define	TDE_SDE_RDMC_DEFAULT		0x00
#define	TDE_SDE_SDK			0x08
#define	TDE_SDE_CKOD			0x04
#define	TDE_SDE_CKORP			0x02
#define	TDE_SDE_CKORL			0x01
	uint8_t encryption_mode;
#define	TDE_SDE_ENC_MODE_DISABLE	0x00
#define	TDE_SDE_ENC_MODE_EXTERNAL	0x01
#define	TDE_SDE_ENC_MODE_ENCRYPT	0x02
	uint8_t decryption_mode;
#define	TDE_SDE_DEC_MODE_DISABLE	0x00
#define	TDE_SDE_DEC_MODE_RAW		0x01
#define	TDE_SDE_DEC_MODE_DECRYPT	0x02
#define	TDE_SDE_DEC_MODE_MIXED		0x03
	uint8_t algo_index;
	uint8_t lbe_key_format;
#define	TDE_SDE_KEY_PLAINTEXT		0x00
#define	TDE_SDE_KEY_VENDOR_SPEC		0x01
#define	TDE_SDE_KEY_PUBLIC_WRAP		0x02
#define	TDE_SDE_KEY_ESP_SCSI		0x03
	uint8_t kad_format;
#define	TDE_SDE_KAD_ASCII		0x02
#define	TDE_SDE_KAD_BINARY		0x01
#define	TDE_SDE_KAD_UNSPECIFIED		0x00
	uint8_t reserved[7];
	uint8_t lbe_key_length[2];
	uint8_t lbe_key[];
};

/*
 * Used for the Vendor Specific key format (0x01).
 */
struct tde_key_format_vendor {
	uint8_t t10_vendor_id[8];
	uint8_t vendor_key[];
};

/*
 * Used for the public key wrapped format (0x02).
 */
struct tde_key_format_public_wrap {
	uint8_t parameter_set[2];
#define	TDE_PARAM_SET_RSA2048		0x0000
#define	TDE_PARAM_SET_ECC521		0x0010
	uint8_t label_length[2];
	uint8_t label[];
};

/*
 * Tape Data Encryption SA Encapsulation page (0x0011).
 */
struct tde_sa_encap_page {
	uint8_t page_code[2];
	uint8_t data_desc[];
};

/*
 * Tape Data Encryption Set Encryption Management Attributes page (0x0022).
 */
struct tde_set_enc_mgmt_attr_page {
	uint8_t page_code[2];
	uint8_t reserved[3];
	uint8_t byte5;
#define	TDE_SEMA_CAOD			0x01
	uint8_t page_length[2];
	uint8_t attr_desc[];
};

/*
 * Tape Data Encryption descriptor format.
 * SSC4r03 Section 8.5.4.2.1 Table 197
 */
struct tde_data_enc_desc {
	uint8_t key_desc_type;
#define	TDE_KEY_DESC_WK_KAD		0x04
#define	TDE_KEY_DESC_M_KAD		0x03
#define	TDE_KEY_DESC_NONCE_VALUE	0x02
#define	TDE_KEY_DESC_A_KAD		0x01
#define	TDE_KEY_DESC_U_KAD		0x00
	uint8_t byte2;
#define	TDE_KEY_DESC_AUTH_MASK		0x07
#define	TDE_KEY_DESC_AUTH_FAILED	0x04
#define	TDE_KEY_DESC_AUTH_SUCCESS	0x03
#define	TDE_KEY_DESC_AUTH_NO_ATTEMPT	0x02
#define	TDE_KEY_DESC_AUTH_U_KAD		0x01
	uint8_t key_desc_length[2];
	uint8_t key_desc[];
};

/*
 * Wrapped Key descriptor format.
 * SSC4r03 Section 8.5.4.3.1 Table 200
 */
struct tde_wrapped_key_desc {
	uint8_t wrapped_key_type;
#define	TDE_WRAP_KEY_DESC_LENGTH	0x04
#define	TDE_WRAP_KEY_DESC_IDENT		0x03
#define	TDE_WRAP_KEY_DESC_INFO		0x02
#define	TDE_WRAP_KEY_DESC_ENTITY_ID	0x01
#define	TDE_WRAP_KEY_DESC_DEVICE_ID	0x00
	uint8_t reserved;
	uint8_t wrapped_desc_length[2];
	uint8_t wrapped_desc[];
};

/*
 * Encryption management attributes descriptor format.
 * SSC4r03 Section 8.5.4.4.1 Table 202
 */
struct tde_enc_mgmt_attr_desc {
	uint8_t enc_mgmt_attr_type[2];
#define	TDE_EMAD_DESIRED_KEY_MGR_OP	0x0000
#define	TDE_EMAD_LOG_BLOCK_ENC_KEY_CRIT	0x0001
#define	TDE_EMAD_LOG_BLOCK_ENC_KEY_WRAP	0x0002
	uint8_t reserved;
	uint8_t byte2;
#define	TDE_EMAD_CRIT			0x80
	uint8_t attr_length[2];
	uint8_t attributes[];
#define	TDE_EMAD_DESIRED_KEY_CREATE	0x0001
#define	TDE_EMAD_DESIRED_KEY_RESOLVE	0x0002
};

/*
 * Logical block encryption key selection criteria descriptor format.
 * SSC4r03 Section 8.5.4.4.3.1 Table 206
 */
struct tde_lb_enc_key_sel_desc {
	uint8_t lbe_key_sel_crit_type[2];
	/*
	 * The CRIT bit is the top bit of the first byte of the type.
	 */
#define	TDE_LBE_KEY_SEL_CRIT		0x80
#define	TDE_LBE_KEY_SEL_ALGO		0x0001
#define	TDE_LBE_KEY_SEL_ID		0x0002
	uint8_t lbe_key_sel_crit_length[2];
	uint8_t lbe_key_sel_crit[];
};

/*
 * Logical block encryption key wrapping attribute descriptor format.
 * SSC4r03 Section 8.5.4.4.4.1 Table 209
 */
struct tde_lb_enc_key_wrap_desc {
	uint8_t lbe_key_wrap_type[2];
	/*
	 * The CRIT bit is the top bit of the first byte of the type.
	 */
#define	TDE_LBE_KEY_WRAP_CRIT		0x80
#define	TDE_LBE_KEY_WRAP_KEKS		0x0001
	uint8_t lbe_key_wrap_length[2];
	uint8_t lbe_key_wrap_attr[];
};

/*
 * Opcodes
 */
#define REWIND			0x01
#define FORMAT_MEDIUM		0x04
#define READ_BLOCK_LIMITS	0x05
#define SA_READ			0x08
#define SA_WRITE		0x0A
#define SET_CAPACITY		0x0B
#define WRITE_FILEMARKS		0x10
#define SPACE			0x11
#define RESERVE_UNIT		0x16
#define RELEASE_UNIT		0x17
#define ERASE			0x19
#define LOAD_UNLOAD		0x1B
#define	LOCATE			0x2B
#define	READ_POSITION		0x34
#define	REPORT_DENSITY_SUPPORT	0x44
#define	ALLOW_OVERWRITE		0x82
#define	LOCATE_16		0x92

/*
 * Tape specific density codes- only enough of them here to recognize
 * some specific older units so we can choose 2FM@EOD or FIXED blocksize
 * quirks.
 */
#define SCSI_DENSITY_HALFINCH_800	0x01
#define SCSI_DENSITY_HALFINCH_1600	0x02
#define SCSI_DENSITY_HALFINCH_6250	0x03
#define SCSI_DENSITY_HALFINCH_6250C	0xC3	/* HP Compressed 6250 */
#define SCSI_DENSITY_QIC_11_4TRK	0x04
#define SCSI_DENSITY_QIC_11_9TRK	0x84	/* Vendor Unique Emulex */
#define SCSI_DENSITY_QIC_24		0x05
#define SCSI_DENSITY_HALFINCH_PE	0x06
#define SCSI_DENSITY_QIC_120		0x0f
#define SCSI_DENSITY_QIC_150		0x10    
#define	SCSI_DENSITY_QIC_525_320	0x11
#define	SCSI_DENSITY_QIC_1320		0x12
#define	SCSI_DENSITY_QIC_2GB		0x22
#define	SCSI_DENSITY_QIC_4GB		0x26
#define	SCSI_DENSITY_QIC_3080		0x29

__BEGIN_DECLS
void	scsi_read_block_limits(struct ccb_scsiio *, u_int32_t,
			       void (*cbfcnp)(struct cam_periph *, union ccb *),
			       u_int8_t, struct scsi_read_block_limits_data *,
			       u_int8_t , u_int32_t);

void	scsi_sa_read_write(struct ccb_scsiio *csio, u_int32_t retries,
			   void (*cbfcnp)(struct cam_periph *, union ccb *),
			   u_int8_t tag_action, int readop, int sli,
			   int fixed, u_int32_t length, u_int8_t *data_ptr,
			   u_int32_t dxfer_len, u_int8_t sense_len,
			   u_int32_t timeout);

void	scsi_rewind(struct ccb_scsiio *csio, u_int32_t retries,
		    void (*cbfcnp)(struct cam_periph *, union ccb *),
		    u_int8_t tag_action, int immediate, u_int8_t sense_len,
		    u_int32_t timeout);

void	scsi_space(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action, scsi_space_code code,
		   u_int32_t count, u_int8_t sense_len, u_int32_t timeout);

void	scsi_load_unload(struct ccb_scsiio *csio, u_int32_t retries,         
			 void (*cbfcnp)(struct cam_periph *, union ccb *),   
			 u_int8_t tag_action, int immediate,   int eot,
			 int reten, int load, u_int8_t sense_len,
			 u_int32_t timeout);
	
void	scsi_write_filemarks(struct ccb_scsiio *csio, u_int32_t retries,
			     void (*cbfcnp)(struct cam_periph *, union ccb *),
			     u_int8_t tag_action, int immediate, int setmark,
			     u_int32_t num_marks, u_int8_t sense_len,
			     u_int32_t timeout);

void	scsi_reserve_release_unit(struct ccb_scsiio *csio, u_int32_t retries,
				  void (*cbfcnp)(struct cam_periph *,
				  union ccb *), u_int8_t tag_action,	
				  int third_party, int third_party_id,
				  u_int8_t sense_len, u_int32_t timeout,
				  int reserve);

void	scsi_erase(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action, int immediate, int long_erase,
		   u_int8_t sense_len, u_int32_t timeout);

void	scsi_data_comp_page(struct scsi_data_compression_page *page,
			    u_int8_t dce, u_int8_t dde, u_int8_t red,
			    u_int32_t comp_algorithm,
			    u_int32_t decomp_algorithm);

void	scsi_read_position(struct ccb_scsiio *csio, u_int32_t retries,
                           void (*cbfcnp)(struct cam_periph *, union ccb *),
                           u_int8_t tag_action, int hardsoft,
                           struct scsi_tape_position_data *sbp,
                           u_int8_t sense_len, u_int32_t timeout);
void	scsi_read_position_10(struct ccb_scsiio *csio, u_int32_t retries,
			      void (*cbfcnp)(struct cam_periph *, union ccb *),
			      u_int8_t tag_action, int service_action,
			      u_int8_t *data_ptr, u_int32_t length,
			      u_int32_t sense_len, u_int32_t timeout);

void	scsi_set_position(struct ccb_scsiio *csio, u_int32_t retries,
                         void (*cbfcnp)(struct cam_periph *, union ccb *),
                         u_int8_t tag_action, int hardsoft, u_int32_t blkno,
                         u_int8_t sense_len, u_int32_t timeout);

void	scsi_locate_10(struct ccb_scsiio *csio, u_int32_t retries,
		       void (*cbfcnp)(struct cam_periph *, union ccb *),
		       u_int8_t tag_action, int immed, int cp, int hard,
		       int64_t partition, u_int32_t block_address,
		       int sense_len, u_int32_t timeout);

void	scsi_locate_16(struct ccb_scsiio *csio, u_int32_t retries,
		       void (*cbfcnp)(struct cam_periph *, union ccb *),
		       u_int8_t tag_action, int immed, int cp,
		       u_int8_t dest_type, int bam, int64_t partition,
		       u_int64_t logical_id, int sense_len,
		       u_int32_t timeout);

void	scsi_report_density_support(struct ccb_scsiio *csio, u_int32_t retries,
				    void (*cbfcnp)(struct cam_periph *,
						   union ccb *),
				    u_int8_t tag_action, int media,
				    int medium_type, u_int8_t *data_ptr,
				    u_int32_t length, u_int32_t sense_len,
				    u_int32_t timeout);

void	scsi_set_capacity(struct ccb_scsiio *csio, u_int32_t retries,
			  void (*cbfcnp)(struct cam_periph *, union ccb *),
			  u_int8_t tag_action, int byte1, u_int32_t proportion,
			  u_int32_t sense_len, u_int32_t timeout);

void	scsi_format_medium(struct ccb_scsiio *csio, u_int32_t retries,
			   void (*cbfcnp)(struct cam_periph *, union ccb *),
			   u_int8_t tag_action, int byte1, int byte2, 
			   u_int8_t *data_ptr, u_int32_t length,
			   u_int32_t sense_len, u_int32_t timeout);

void	scsi_allow_overwrite(struct ccb_scsiio *csio, u_int32_t retries,
			     void (*cbfcnp)(struct cam_periph *, union ccb *),
			     u_int8_t tag_action, int allow_overwrite,
			     int partition, u_int64_t logical_id,
			     u_int32_t sense_len, u_int32_t timeout);

__END_DECLS

#endif /* _SCSI_SCSI_SA_H */
