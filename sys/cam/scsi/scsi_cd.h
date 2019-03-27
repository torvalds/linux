/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000, 2002 Kenneth D. Merry
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
 */
/*
 * Written by Julian Elischer (julian@tfs.com)
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
 *	from: scsi_cd.h,v 1.10 1997/02/22 09:44:28 peter Exp $
 * $FreeBSD$
 */
#ifndef	_SCSI_SCSI_CD_H
#define _SCSI_SCSI_CD_H 1

/*
 *	Define two bits always in the same place in byte 2 (flag byte)
 */
#define	CD_RELADDR	0x01
#define	CD_MSF		0x02

/*
 * SCSI command format
 */

struct scsi_get_config
{
	uint8_t opcode;
	uint8_t rt;
#define	SGC_RT_ALL		0x00
#define	SGC_RT_CURRENT		0x01
#define	SGC_RT_SPECIFIC		0x02
#define	SGC_RT_MASK		0x03
	uint8_t starting_feature[2];
	uint8_t reserved[3];
	uint8_t length[2];
	uint8_t control;
};

struct scsi_get_config_header
{
	uint8_t data_length[4];
	uint8_t reserved[2];
	uint8_t current_profile[2];
};

struct scsi_get_config_feature
{
	uint8_t feature_code[2];
	uint8_t flags;
#define	SGC_F_CURRENT		0x01
#define	SGC_F_PERSISTENT	0x02
#define	SGC_F_VERSION_MASK	0x2C
#define	SGC_F_VERSION_SHIFT	2
	uint8_t add_length;
	uint8_t feature_data[];
};

struct scsi_get_event_status
{
	uint8_t opcode;
	uint8_t byte2;
#define	SGESN_POLLED		1
	uint8_t reserved[2];
	uint8_t notif_class;
	uint8_t reserved2[2];
	uint8_t length[2];
	uint8_t control;
};

struct scsi_get_event_status_header
{
	uint8_t descr_length[4];
	uint8_t nea_class;
#define	SGESN_NEA		0x80
	uint8_t supported_class;
};

struct scsi_get_event_status_descr
{
	uint8_t event_code;
	uint8_t event_info[];
};

struct scsi_mechanism_status
{
	uint8_t opcode;
	uint8_t reserved[7];
	uint8_t length[2];
	uint8_t reserved2;
	uint8_t control;
};

struct scsi_mechanism_status_header
{
	uint8_t state1;
	uint8_t state2;
	uint8_t lba[3];
	uint8_t slots_num;
	uint8_t slots_length[2];
};

struct scsi_pause
{
	u_int8_t op_code;
	u_int8_t byte2;
	u_int8_t unused[6];
	u_int8_t resume;
	u_int8_t control;
};
#define	PA_PAUSE	1
#define PA_RESUME	0

struct scsi_play_msf
{
	u_int8_t op_code;
	u_int8_t byte2;
	u_int8_t unused;
	u_int8_t start_m;
	u_int8_t start_s;
	u_int8_t start_f;
	u_int8_t end_m;
	u_int8_t end_s;
	u_int8_t end_f;
	u_int8_t control;
};

struct scsi_play_track
{
	u_int8_t op_code;
	u_int8_t byte2;
	u_int8_t unused[2];
	u_int8_t start_track;
	u_int8_t start_index;
	u_int8_t unused1;
	u_int8_t end_track;
	u_int8_t end_index;
	u_int8_t control;
};

struct scsi_play_10
{
	u_int8_t op_code;
	u_int8_t byte2;
	u_int8_t blk_addr[4];
	u_int8_t unused;
	u_int8_t xfer_len[2];
	u_int8_t control;
};

struct scsi_play_12
{
	u_int8_t op_code;
	u_int8_t byte2;	/* same as above */
	u_int8_t blk_addr[4];
	u_int8_t xfer_len[4];
	u_int8_t unused;
	u_int8_t control;
};

struct scsi_play_rel_12
{
	u_int8_t op_code;
	u_int8_t byte2;	/* same as above */
	u_int8_t blk_addr[4];
	u_int8_t xfer_len[4];
	u_int8_t track;
	u_int8_t control;
};

struct scsi_read_header
{
	u_int8_t op_code;
	u_int8_t byte2;
	u_int8_t blk_addr[4];
	u_int8_t unused;
	u_int8_t data_len[2];
	u_int8_t control;
};

struct scsi_read_subchannel
{
	u_int8_t op_code;
	u_int8_t byte1;
	u_int8_t byte2;
#define	SRS_SUBQ	0x40
	u_int8_t subchan_format;
	u_int8_t unused[2];
	u_int8_t track;
	u_int8_t data_len[2];
	u_int8_t control;
};

struct scsi_read_toc
{
	u_int8_t op_code;
	u_int8_t byte2;
	u_int8_t format;
	u_int8_t unused[3];
	u_int8_t from_track;
	u_int8_t data_len[2];
	u_int8_t control;
};

struct scsi_read_toc_hdr
{
	uint8_t data_length[2];
	uint8_t first;
	uint8_t last;
};

struct scsi_read_toc_type01_descr
{
	uint8_t reserved;
	uint8_t addr_ctl;
	uint8_t track_number;
	uint8_t reserved2;
	uint8_t track_start[4];
};

struct scsi_read_cd_capacity
{
	u_int8_t op_code;
	u_int8_t byte2;
	u_int8_t addr_3;	/* Most Significant */
	u_int8_t addr_2;
	u_int8_t addr_1;
	u_int8_t addr_0;	/* Least Significant */
	u_int8_t unused[3];
	u_int8_t control;
};

struct scsi_set_speed
{
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t readspeed[2];
	u_int8_t writespeed[2];
	u_int8_t reserved[5];
	u_int8_t control;
};

struct scsi_report_key 
{
	u_int8_t opcode;
	u_int8_t reserved0;
	u_int8_t lba[4];
	u_int8_t reserved1[2];
	u_int8_t alloc_len[2];
	u_int8_t agid_keyformat;
#define RK_KF_AGID_MASK		0xc0
#define RK_KF_AGID_SHIFT	6
#define RK_KF_KEYFORMAT_MASK	0x3f
#define RK_KF_AGID		0x00
#define RK_KF_CHALLENGE		0x01
#define RF_KF_KEY1		0x02
#define RK_KF_KEY2		0x03
#define RF_KF_TITLE		0x04
#define RF_KF_ASF		0x05
#define RK_KF_RPC_SET		0x06
#define RF_KF_RPC_REPORT	0x08
#define RF_KF_INV_AGID		0x3f
	u_int8_t control;
};

/*
 * See the report key structure for key format and AGID definitions.
 */
struct scsi_send_key
{
	u_int8_t opcode;
	u_int8_t reserved[7];
	u_int8_t param_len[2];
	u_int8_t agid_keyformat;
	u_int8_t control;
};

struct scsi_read_dvd_structure
{
	u_int8_t opcode;
	u_int8_t reserved;
	u_int8_t address[4];
	u_int8_t layer_number;
	u_int8_t format;
#define RDS_FORMAT_PHYSICAL		0x00
#define RDS_FORMAT_COPYRIGHT		0x01
#define RDS_FORMAT_DISC_KEY		0x02
#define RDS_FORMAT_BCA			0x03
#define RDS_FORMAT_MANUFACTURER		0x04
#define RDS_FORMAT_CMGS_CPM		0x05
#define RDS_FORMAT_PROT_DISCID		0x06
#define RDS_FORMAT_DISC_KEY_BLOCK	0x07
#define RDS_FORMAT_DDS			0x08
#define RDS_FORMAT_DVDRAM_MEDIA_STAT	0x09
#define RDS_FORMAT_SPARE_AREA		0x0a
#define RDS_FORMAT_RMD_BORDEROUT	0x0c
#define RDS_FORMAT_RMD			0x0d
#define RDS_FORMAT_LEADIN		0x0e
#define RDS_FORMAT_DISC_ID		0x0f
#define RDS_FORMAT_DCB			0x30
#define RDS_FORMAT_WRITE_PROT		0xc0
#define RDS_FORMAT_STRUCTURE_LIST	0xff
	u_int8_t alloc_len[2];
	u_int8_t agid;
	u_int8_t control;
};

/*
 * Opcodes
 */
#define READ_CD_CAPACITY	0x25	/* slightly different from disk */
#define READ_SUBCHANNEL		0x42	/* cdrom read Subchannel */
#define READ_TOC		0x43	/* cdrom read TOC */
#define READ_HEADER		0x44	/* cdrom read header */
#define PLAY_10			0x45	/* cdrom play  'play audio' mode */
#define GET_CONFIGURATION	0x46	/* Get device configuration */
#define PLAY_MSF		0x47	/* cdrom play Min,Sec,Frames mode */
#define PLAY_TRACK		0x48	/* cdrom play track/index mode */
#define PLAY_TRACK_REL		0x49	/* cdrom play track/index mode */
#define GET_EVENT_STATUS	0x4a	/* Get event status notification */
#define PAUSE			0x4b	/* cdrom pause in 'play audio' mode */
#define SEND_KEY		0xa3	/* dvd send key command */
#define REPORT_KEY		0xa4	/* dvd report key command */
#define PLAY_12			0xa5	/* cdrom pause in 'play audio' mode */
#define PLAY_TRACK_REL_BIG	0xa9	/* cdrom play track/index mode */
#define READ_DVD_STRUCTURE	0xad	/* read dvd structure */
#define SET_CD_SPEED		0xbb	/* set c/dvd speed */
#define MECHANISM_STATUS	0xbd	/* get status of c/dvd mechanics */

struct scsi_report_key_data_header
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
};

struct scsi_report_key_data_agid
{
	u_int8_t data_len[2];
	u_int8_t reserved[5];
	u_int8_t agid;
#define RKD_AGID_MASK	0xc0
#define RKD_AGID_SHIFT	6
};

struct scsi_report_key_data_challenge
{
	u_int8_t data_len[2];
	u_int8_t reserved0[2];
	u_int8_t challenge_key[10];
	u_int8_t reserved1[2];
};

struct scsi_report_key_data_key1_key2
{
	u_int8_t data_len[2];
	u_int8_t reserved0[2];
	u_int8_t key1[5];
	u_int8_t reserved1[3];
};

struct scsi_report_key_data_title
{
	u_int8_t data_len[2];
	u_int8_t reserved0[2];
	u_int8_t byte0;
#define RKD_TITLE_CPM		0x80
#define RKD_TITLE_CPM_SHIFT	7
#define RKD_TITLE_CP_SEC	0x40
#define RKD_TITLE_CP_SEC_SHIFT	6
#define RKD_TITLE_CMGS_MASK	0x30
#define RKD_TITLE_CMGS_SHIFT	4
#define RKD_TITLE_CMGS_NO_RST	0x00
#define RKD_TITLE_CMGS_RSVD	0x10
#define RKD_TITLE_CMGS_1_GEN	0x20
#define RKD_TITLE_CMGS_NO_COPY	0x30
	u_int8_t title_key[5];
	u_int8_t reserved1[2];
};

struct scsi_report_key_data_asf
{
	u_int8_t data_len[2];
	u_int8_t reserved[5];
	u_int8_t success;
#define RKD_ASF_SUCCESS	0x01
};

struct scsi_report_key_data_rpc
{
	u_int8_t data_len[2];
	u_int8_t rpc_scheme0;
#define RKD_RPC_SCHEME_UNKNOWN		0x00
#define RKD_RPC_SCHEME_PHASE_II		0x01
	u_int8_t reserved0;
	u_int8_t byte4;
#define RKD_RPC_TYPE_MASK		0xC0
#define RKD_RPC_TYPE_SHIFT		6
#define RKD_RPC_TYPE_NONE		0x00
#define RKD_RPC_TYPE_SET		0x40
#define RKD_RPC_TYPE_LAST_CHANCE	0x80
#define RKD_RPC_TYPE_PERM		0xC0
#define RKD_RPC_VENDOR_RESET_MASK	0x38
#define RKD_RPC_VENDOR_RESET_SHIFT	3
#define RKD_RPC_USER_RESET_MASK		0x07
#define RKD_RPC_USER_RESET_SHIFT	0
	u_int8_t region_mask;
	u_int8_t rpc_scheme1;
	u_int8_t reserved1;
};

struct scsi_send_key_data_rpc
{
	u_int8_t data_len[2];
	u_int8_t reserved0[2];
	u_int8_t region_code;
	u_int8_t reserved1[3];
};

/*
 * Common header for the return data from the READ DVD STRUCTURE command.
 */
struct scsi_read_dvd_struct_data_header
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
};

struct scsi_read_dvd_struct_data_layer_desc
{
	u_int8_t book_type_version;
#define RDSD_BOOK_TYPE_DVD_ROM	0x00
#define RDSD_BOOK_TYPE_DVD_RAM	0x10
#define RDSD_BOOK_TYPE_DVD_R	0x20
#define RDSD_BOOK_TYPE_DVD_RW	0x30
#define RDSD_BOOK_TYPE_DVD_PRW	0x90
#define RDSD_BOOK_TYPE_MASK	0xf0
#define RDSD_BOOK_TYPE_SHIFT	4
#define RDSD_BOOK_VERSION_MASK	0x0f
	/*
	 * The lower 4 bits of this field is referred to as the "minimum
	 * rate" field in MMC2, and the "maximum rate" field in MMC3.  Ugh.
	 */
	u_int8_t disc_size_max_rate;
#define RDSD_DISC_SIZE_120MM	0x00
#define RDSD_DISC_SIZE_80MM	0x10
#define RDSD_DISC_SIZE_MASK	0xf0
#define RDSD_DISC_SIZE_SHIFT	4
#define RDSD_MAX_RATE_0252	0x00
#define RDSD_MAX_RATE_0504	0x01
#define RDSD_MAX_RATE_1008	0x02
#define RDSD_MAX_RATE_NOT_SPEC	0x0f
#define RDSD_MAX_RATE_MASK	0x0f
	u_int8_t layer_info;
#define RDSD_NUM_LAYERS_MASK	0x60
#define RDSD_NUM_LAYERS_SHIFT	5
#define RDSD_NL_ONE_LAYER	0x00
#define RDSD_NL_TWO_LAYERS	0x20
#define RDSD_TRACK_PATH_MASK	0x10
#define RDSD_TRACK_PATH_SHIFT	4
#define RDSD_TP_PTP		0x00
#define RDSD_TP_OTP		0x10
#define RDSD_LAYER_TYPE_RO	0x01
#define RDSD_LAYER_TYPE_RECORD	0x02
#define RDSD_LAYER_TYPE_RW	0x04
#define RDSD_LAYER_TYPE_MASK	0x0f
	u_int8_t density;
#define RDSD_LIN_DENSITY_0267		0x00
#define RDSD_LIN_DENSITY_0293		0x10
#define RDSD_LIN_DENSITY_0409_0435	0x20
#define RDSD_LIN_DENSITY_0280_0291	0x40
/* XXX MMC2 uses 0.176um/bit instead of 0.353 as in MMC3 */
#define RDSD_LIN_DENSITY_0353		0x80
#define RDSD_LIN_DENSITY_MASK		0xf0
#define RDSD_LIN_DENSITY_SHIFT		4
#define RDSD_TRACK_DENSITY_074		0x00
#define RDSD_TRACK_DENSITY_080		0x01
#define RDSD_TRACK_DENSITY_0615		0x02
#define RDSD_TRACK_DENSITY_MASK		0x0f
	u_int8_t zeros0;
	u_int8_t main_data_start[3];
#define RDSD_MAIN_DATA_START_DVD_RO	0x30000
#define RDSD_MAIN_DATA_START_DVD_RW	0x31000
	u_int8_t zeros1;
	u_int8_t main_data_end[3];
	u_int8_t zeros2;
	u_int8_t end_sector_layer0[3];
	u_int8_t bca;
#define RDSD_BCA	0x80
#define RDSD_BCA_MASK	0x80
#define RDSD_BCA_SHIFT	7
	u_int8_t media_specific[2031];
};

struct scsi_read_dvd_struct_data_physical
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	struct scsi_read_dvd_struct_data_layer_desc layer_desc;
};

struct scsi_read_dvd_struct_data_copyright
{
	u_int8_t data_len[2];
	u_int8_t reserved0[2];
	u_int8_t cps_type;
#define RDSD_CPS_NOT_PRESENT	0x00
#define RDSD_CPS_DATA_EXISTS	0x01
	u_int8_t region_info;
	u_int8_t reserved1[2];
};

struct scsi_read_dvd_struct_data_disc_key
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	u_int8_t disc_key[2048];
};

struct scsi_read_dvd_struct_data_bca
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	u_int8_t bca_info[188]; /* XXX 12-188 bytes */
};

struct scsi_read_dvd_struct_data_manufacturer
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	u_int8_t manuf_info[2048];
};

struct scsi_read_dvd_struct_data_copy_manage
{
	u_int8_t data_len[2];
	u_int8_t reserved0[2];
	u_int8_t byte4;
#define RDSD_CPM_NO_COPYRIGHT	0x00
#define RDSD_CPM_HAS_COPYRIGHT	0x80
#define RDSD_CPM_MASK		0x80
#define RDSD_CMGS_COPY_ALLOWED	0x00
#define RDSD_CMGS_ONE_COPY	0x20
#define RDSD_CMGS_NO_COPIES	0x30
#define RDSD_CMGS_MASK		0x30
	u_int8_t reserved1[3];
};

struct scsi_read_dvd_struct_data_prot_discid
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	u_int8_t prot_discid_data[16];
};

struct scsi_read_dvd_struct_data_disc_key_blk
{
	/*
	 * Length is 0x6ffe == 28670 for CPRM, 0x3002 == 12990 for CSS2.
	 */
	u_int8_t data_len[2];
	u_int8_t reserved;
	u_int8_t total_packs;
	u_int8_t disc_key_pack_data[28668];
};
struct scsi_read_dvd_struct_data_dds
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	u_int8_t dds_info[2048];
};

struct scsi_read_dvd_struct_data_medium_status
{
	u_int8_t data_len[2];
	u_int8_t reserved0[2];
	u_int8_t byte4;
#define RDSD_MS_CARTRIDGE	0x80
#define RDSD_MS_OUT		0x40
#define RDSD_MS_MSWI		0x08
#define RDSD_MS_CWP		0x04
#define RDSD_MS_PWP		0x02
	u_int8_t disc_type_id;
#define RDSD_DT_NEED_CARTRIDGE	0x00
#define RDSD_DT_NO_CART_NEEDED	0x01
	u_int8_t reserved1;
	u_int8_t ram_swi_info;
#define RDSD_SWI_NO_BARE	0x01
#define RDSD_SWI_UNSPEC		0xff
};

struct scsi_read_dvd_struct_data_spare_area
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	u_int8_t unused_primary[4];
	u_int8_t unused_supl[4];
	u_int8_t allocated_supl[4];
};

struct scsi_read_dvd_struct_data_rmd_borderout
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	u_int8_t rmd[30720]; 	/* maximum is 30720 bytes */
};

struct scsi_read_dvd_struct_data_rmd
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	u_int8_t last_sector_num[4];
	u_int8_t rmd_bytes[32768];  /* This is the maximum */
};

/*
 * XXX KDM this is the MMC2 version of the structure.
 * The variable positions have changed (in a semi-conflicting way) in the
 * MMC3 spec, although the overall length of the structure is the same.
 */
struct scsi_read_dvd_struct_data_leadin
{
	u_int8_t data_len[2];
	u_int8_t reserved0[2];
	u_int8_t field_id_1;
	u_int8_t app_code;
	u_int8_t disc_physical_data;
	u_int8_t last_addr[3];
	u_int8_t reserved1[2];
	u_int8_t field_id_2;
	u_int8_t rwp;
	u_int8_t rwp_wavelength;
	u_int8_t optimum_write_strategy;
	u_int8_t reserved2[4];
	u_int8_t field_id_3;
	u_int8_t manuf_id_17_12[6];
	u_int8_t reserved3;
	u_int8_t field_id_4;
	u_int8_t manuf_id_11_6[6];
	u_int8_t reserved4;
	u_int8_t field_id_5;
	u_int8_t manuf_id_5_0[6];
	u_int8_t reserved5[25];
};

struct scsi_read_dvd_struct_data_disc_id
{
	u_int8_t data_len[2];
	u_int8_t reserved[4];
	u_int8_t random_num[2];
	u_int8_t year[4];
	u_int8_t month[2];
	u_int8_t day[2];
	u_int8_t hour[2];
	u_int8_t minute[2];
	u_int8_t second[2];
};

struct scsi_read_dvd_struct_data_generic_dcb
{
	u_int8_t content_desc[4];
#define SCSI_RCB
	u_int8_t unknown_desc_actions[4];
#define RDSD_ACTION_RECORDING	0x0001
#define RDSD_ACTION_READING	0x0002
#define RDSD_ACTION_FORMAT	0x0004
#define RDSD_ACTION_MODIFY_DCB	0x0008
	u_int8_t vendor_id[32];
	u_int8_t dcb_data[32728];
};

struct scsi_read_dvd_struct_data_dcb
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	struct scsi_read_dvd_struct_data_generic_dcb dcb;
};

struct read_dvd_struct_write_prot
{
	u_int8_t data_len[2];
	u_int8_t reserved0[2];
	u_int8_t write_prot_status;
#define RDSD_WPS_MSWI		0x08
#define RDSD_WPS_CWP		0x04
#define RDSD_WPS_PWP		0x02
#define RDSD_WPS_SWPP		0x01
	u_int8_t reserved[3];
};

struct read_dvd_struct_list_entry
{
	u_int8_t format_code;
	u_int8_t sds_rds;
#define RDSD_SDS_NOT_WRITEABLE	0x00
#define RDSD_SDS_WRITEABLE	0x80
#define RDSD_SDS_MASK		0x80
#define RDSD_RDS_NOT_READABLE	0x00
#define RDSD_RDS_READABLE	0x40
#define RDSD_RDS_MASK		0x40
	u_int8_t struct_len[2];
};

struct read_dvd_struct_data_list
{
	u_int8_t data_len[2];
	u_int8_t reserved[2];
	struct read_dvd_struct_list_entry entries[0];
};

struct scsi_read_cd_cap_data
{
	u_int8_t addr_3;	/* Most significant */
	u_int8_t addr_2;
	u_int8_t addr_1;
	u_int8_t addr_0;	/* Least significant */
	u_int8_t length_3;	/* Most significant */
	u_int8_t length_2;
	u_int8_t length_1;
	u_int8_t length_0;	/* Least significant */
};

struct cd_audio_page
{
	u_int8_t page_code;
#define	CD_PAGE_CODE		0x3F
#define	AUDIO_PAGE		0x0e
#define	CD_PAGE_PS		0x80
	u_int8_t param_len;
	u_int8_t flags;
#define	CD_PA_SOTC		0x02
#define	CD_PA_IMMED		0x04
	u_int8_t unused[2];
	u_int8_t format_lba;
#define	CD_PA_FORMAT_LBA	0x0F
#define	CD_PA_APR_VALID		0x80
	u_int8_t lb_per_sec[2];
	struct	port_control
	{
		u_int8_t channels;
#define	CHANNEL			0x0F
#define	CHANNEL_0		1
#define	CHANNEL_1		2
#define	CHANNEL_2		4
#define	CHANNEL_3		8
#define	LEFT_CHANNEL		CHANNEL_0
#define	RIGHT_CHANNEL		CHANNEL_1
		u_int8_t volume;
	} port[4];
#define	LEFT_PORT		0
#define	RIGHT_PORT		1
};

struct scsi_cddvd_capabilities_page_sd {
	uint8_t reserved;
	uint8_t rotation_control;
	uint8_t write_speed_supported[2];
};

struct scsi_cddvd_capabilities_page {
	uint8_t page_code;
#define	SMS_CDDVD_CAPS_PAGE		0x2a
	uint8_t page_length;
	uint8_t caps1;
	uint8_t caps2;
	uint8_t caps3;
	uint8_t caps4;
	uint8_t caps5;
	uint8_t caps6;
	uint8_t obsolete[2];
	uint8_t nvol_levels[2];
	uint8_t buffer_size[2];
	uint8_t obsolete2[2];
	uint8_t reserved;
	uint8_t digital;
	uint8_t obsolete3;
	uint8_t copy_management;
	uint8_t reserved2;
	uint8_t rotation_control;
	uint8_t cur_write_speed;
	uint8_t num_speed_descr;
	struct scsi_cddvd_capabilities_page_sd speed_descr[];
};

union cd_pages
{
	struct cd_audio_page audio;
};

struct cd_mode_data_10
{
	struct scsi_mode_header_10 header;
	struct scsi_mode_blk_desc  blk_desc;
	union cd_pages page;
};

struct cd_mode_data
{
	struct scsi_mode_header_6 header;
	struct scsi_mode_blk_desc blk_desc;
	union cd_pages page;
};

union cd_mode_data_6_10
{
	struct cd_mode_data mode_data_6;
	struct cd_mode_data_10 mode_data_10;
};

struct cd_mode_params
{
	STAILQ_ENTRY(cd_mode_params)	links;
	int				cdb_size;
	int				alloc_len;
	u_int8_t			*mode_buf;
};

__BEGIN_DECLS
void scsi_report_key(struct ccb_scsiio *csio, u_int32_t retries,
		     void (*cbfcnp)(struct cam_periph *, union ccb *),
		     u_int8_t tag_action, u_int32_t lba, u_int8_t agid,
		     u_int8_t key_format, u_int8_t *data_ptr,
		     u_int32_t dxfer_len, u_int8_t sense_len,
		     u_int32_t timeout);

void scsi_send_key(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action, u_int8_t agid, u_int8_t key_format,
		   u_int8_t *data_ptr, u_int32_t dxfer_len, u_int8_t sense_len,
		   u_int32_t timeout);

void scsi_read_dvd_structure(struct ccb_scsiio *csio, u_int32_t retries,
			     void (*cbfcnp)(struct cam_periph *, union ccb *),
			     u_int8_t tag_action, u_int32_t address,
			     u_int8_t layer_number, u_int8_t format,
			     u_int8_t agid, u_int8_t *data_ptr,
			     u_int32_t dxfer_len, u_int8_t sense_len,
			     u_int32_t timeout);

__END_DECLS

#endif /*_SCSI_SCSI_CD_H*/

