/*
 * SAS structures and definitions header file
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#ifndef _SAS_H_
#define _SAS_H_

#include <linux/types.h>
#include <asm/byteorder.h>

#define SAS_ADDR_SIZE        8
#define HASHED_SAS_ADDR_SIZE 3
#define SAS_ADDR(_sa) ((unsigned long long) be64_to_cpu(*(__be64 *)(_sa)))

#define SMP_REQUEST             0x40
#define SMP_RESPONSE            0x41

#define SSP_DATA                0x01
#define SSP_XFER_RDY            0x05
#define SSP_COMMAND             0x06
#define SSP_RESPONSE            0x07
#define SSP_TASK                0x16

#define SMP_REPORT_GENERAL       0x00
#define SMP_REPORT_MANUF_INFO    0x01
#define SMP_READ_GPIO_REG        0x02
#define SMP_DISCOVER             0x10
#define SMP_REPORT_PHY_ERR_LOG   0x11
#define SMP_REPORT_PHY_SATA      0x12
#define SMP_REPORT_ROUTE_INFO    0x13
#define SMP_WRITE_GPIO_REG       0x82
#define SMP_CONF_ROUTE_INFO      0x90
#define SMP_PHY_CONTROL          0x91
#define SMP_PHY_TEST_FUNCTION    0x92

#define SMP_RESP_FUNC_ACC        0x00
#define SMP_RESP_FUNC_UNK        0x01
#define SMP_RESP_FUNC_FAILED     0x02
#define SMP_RESP_INV_FRM_LEN     0x03
#define SMP_RESP_NO_PHY          0x10
#define SMP_RESP_NO_INDEX        0x11
#define SMP_RESP_PHY_NO_SATA     0x12
#define SMP_RESP_PHY_UNK_OP      0x13
#define SMP_RESP_PHY_UNK_TESTF   0x14
#define SMP_RESP_PHY_TEST_INPROG 0x15
#define SMP_RESP_PHY_VACANT      0x16

/* SAM TMFs */
#define TMF_ABORT_TASK      0x01
#define TMF_ABORT_TASK_SET  0x02
#define TMF_CLEAR_TASK_SET  0x04
#define TMF_LU_RESET        0x08
#define TMF_CLEAR_ACA       0x40
#define TMF_QUERY_TASK      0x80

/* SAS TMF responses */
#define TMF_RESP_FUNC_COMPLETE   0x00
#define TMF_RESP_INVALID_FRAME   0x02
#define TMF_RESP_FUNC_ESUPP      0x04
#define TMF_RESP_FUNC_FAILED     0x05
#define TMF_RESP_FUNC_SUCC       0x08
#define TMF_RESP_NO_LUN          0x09
#define TMF_RESP_OVERLAPPED_TAG  0x0A

enum sas_oob_mode {
	OOB_NOT_CONNECTED,
	SATA_OOB_MODE,
	SAS_OOB_MODE
};

/* See sas_discover.c if you plan on changing these */
enum sas_device_type {
	/* these are SAS protocol defined (attached device type field) */
	SAS_PHY_UNUSED = 0,
	SAS_END_DEVICE = 1,
	SAS_EDGE_EXPANDER_DEVICE = 2,
	SAS_FANOUT_EXPANDER_DEVICE = 3,
	/* these are internal to libsas */
	SAS_HA = 4,
	SAS_SATA_DEV = 5,
	SAS_SATA_PM = 7,
	SAS_SATA_PM_PORT = 8,
	SAS_SATA_PENDING = 9,
};

enum sas_protocol {
	SAS_PROTOCOL_NONE		= 0,
	SAS_PROTOCOL_SATA		= 0x01,
	SAS_PROTOCOL_SMP		= 0x02,
	SAS_PROTOCOL_STP		= 0x04,
	SAS_PROTOCOL_SSP		= 0x08,
	SAS_PROTOCOL_ALL		= 0x0E,
	SAS_PROTOCOL_STP_ALL		= SAS_PROTOCOL_STP|SAS_PROTOCOL_SATA,
};

/* From the spec; local phys only */
enum phy_func {
	PHY_FUNC_NOP,
	PHY_FUNC_LINK_RESET,		  /* Enables the phy */
	PHY_FUNC_HARD_RESET,
	PHY_FUNC_DISABLE,
	PHY_FUNC_CLEAR_ERROR_LOG = 5,
	PHY_FUNC_CLEAR_AFFIL,
	PHY_FUNC_TX_SATA_PS_SIGNAL,
	PHY_FUNC_RELEASE_SPINUP_HOLD = 0x10, /* LOCAL PORT ONLY! */
	PHY_FUNC_SET_LINK_RATE,
	PHY_FUNC_GET_EVENTS,
};

/* SAS LLDD would need to report only _very_few_ of those, like BROADCAST.
 * Most of those are here for completeness.
 */
enum sas_prim {
	SAS_PRIM_AIP_NORMAL = 1,
	SAS_PRIM_AIP_R0     = 2,
	SAS_PRIM_AIP_R1     = 3,
	SAS_PRIM_AIP_R2     = 4,
	SAS_PRIM_AIP_WC     = 5,
	SAS_PRIM_AIP_WD     = 6,
	SAS_PRIM_AIP_WP     = 7,
	SAS_PRIM_AIP_RWP    = 8,

	SAS_PRIM_BC_CH      = 9,
	SAS_PRIM_BC_RCH0    = 10,
	SAS_PRIM_BC_RCH1    = 11,
	SAS_PRIM_BC_R0      = 12,
	SAS_PRIM_BC_R1      = 13,
	SAS_PRIM_BC_R2      = 14,
	SAS_PRIM_BC_R3      = 15,
	SAS_PRIM_BC_R4      = 16,

	SAS_PRIM_NOTIFY_ENSP= 17,
	SAS_PRIM_NOTIFY_R0  = 18,
	SAS_PRIM_NOTIFY_R1  = 19,
	SAS_PRIM_NOTIFY_R2  = 20,

	SAS_PRIM_CLOSE_CLAF = 21,
	SAS_PRIM_CLOSE_NORM = 22,
	SAS_PRIM_CLOSE_R0   = 23,
	SAS_PRIM_CLOSE_R1   = 24,

	SAS_PRIM_OPEN_RTRY  = 25,
	SAS_PRIM_OPEN_RJCT  = 26,
	SAS_PRIM_OPEN_ACPT  = 27,

	SAS_PRIM_DONE       = 28,
	SAS_PRIM_BREAK      = 29,

	SATA_PRIM_DMAT      = 33,
	SATA_PRIM_PMNAK     = 34,
	SATA_PRIM_PMACK     = 35,
	SATA_PRIM_PMREQ_S   = 36,
	SATA_PRIM_PMREQ_P   = 37,
	SATA_SATA_R_ERR     = 38,
};

enum sas_open_rej_reason {
	/* Abandon open */
	SAS_OREJ_UNKNOWN   = 0,
	SAS_OREJ_BAD_DEST  = 1,
	SAS_OREJ_CONN_RATE = 2,
	SAS_OREJ_EPROTO    = 3,
	SAS_OREJ_RESV_AB0  = 4,
	SAS_OREJ_RESV_AB1  = 5,
	SAS_OREJ_RESV_AB2  = 6,
	SAS_OREJ_RESV_AB3  = 7,
	SAS_OREJ_WRONG_DEST= 8,
	SAS_OREJ_STP_NORES = 9,

	/* Retry open */
	SAS_OREJ_NO_DEST   = 10,
	SAS_OREJ_PATH_BLOCKED = 11,
	SAS_OREJ_RSVD_CONT0 = 12,
	SAS_OREJ_RSVD_CONT1 = 13,
	SAS_OREJ_RSVD_INIT0 = 14,
	SAS_OREJ_RSVD_INIT1 = 15,
	SAS_OREJ_RSVD_STOP0 = 16,
	SAS_OREJ_RSVD_STOP1 = 17,
	SAS_OREJ_RSVD_RETRY = 18,
};

enum sas_gpio_reg_type {
	SAS_GPIO_REG_CFG   = 0,
	SAS_GPIO_REG_RX    = 1,
	SAS_GPIO_REG_RX_GP = 2,
	SAS_GPIO_REG_TX    = 3,
	SAS_GPIO_REG_TX_GP = 4,
};

struct  dev_to_host_fis {
	u8     fis_type;	  /* 0x34 */
	u8     flags;
	u8     status;
	u8     error;

	u8     lbal;
	union { u8 lbam; u8 byte_count_low; };
	union { u8 lbah; u8 byte_count_high; };
	u8     device;

	u8     lbal_exp;
	u8     lbam_exp;
	u8     lbah_exp;
	u8     _r_a;

	union { u8  sector_count; u8 interrupt_reason; };
	u8     sector_count_exp;
	u8     _r_b;
	u8     _r_c;

	u32    _r_d;
} __attribute__ ((packed));

struct host_to_dev_fis {
	u8     fis_type;	  /* 0x27 */
	u8     flags;
	u8     command;
	u8     features;

	u8     lbal;
	union { u8 lbam; u8 byte_count_low; };
	union { u8 lbah; u8 byte_count_high; };
	u8     device;

	u8     lbal_exp;
	u8     lbam_exp;
	u8     lbah_exp;
	u8     features_exp;

	union { u8  sector_count; u8 interrupt_reason; };
	u8     sector_count_exp;
	u8     _r_a;
	u8     control;

	u32    _r_b;
} __attribute__ ((packed));

/* Prefer to have code clarity over header file clarity.
 */
#ifdef __LITTLE_ENDIAN_BITFIELD
struct sas_identify_frame {
	/* Byte 0 */
	u8  frame_type:4;
	u8  dev_type:3;
	u8  _un0:1;

	/* Byte 1 */
	u8  _un1;

	/* Byte 2 */
	union {
		struct {
			u8  _un20:1;
			u8  smp_iport:1;
			u8  stp_iport:1;
			u8  ssp_iport:1;
			u8  _un247:4;
		};
		u8 initiator_bits;
	};

	/* Byte 3 */
	union {
		struct {
			u8  _un30:1;
			u8 smp_tport:1;
			u8 stp_tport:1;
			u8 ssp_tport:1;
			u8 _un347:4;
		};
		u8 target_bits;
	};

	/* Byte 4 - 11 */
	u8 _un4_11[8];

	/* Byte 12 - 19 */
	u8 sas_addr[SAS_ADDR_SIZE];

	/* Byte 20 */
	u8 phy_id;

	u8 _un21_27[7];

	__be32 crc;
} __attribute__ ((packed));

struct ssp_frame_hdr {
	u8     frame_type;
	u8     hashed_dest_addr[HASHED_SAS_ADDR_SIZE];
	u8     _r_a;
	u8     hashed_src_addr[HASHED_SAS_ADDR_SIZE];
	__be16 _r_b;

	u8     changing_data_ptr:1;
	u8     retransmit:1;
	u8     retry_data_frames:1;
	u8     _r_c:5;

	u8     num_fill_bytes:2;
	u8     _r_d:6;

	u32    _r_e;
	__be16 tag;
	__be16 tptt;
	__be32 data_offs;
} __attribute__ ((packed));

struct ssp_response_iu {
	u8     _r_a[10];

	u8     datapres:2;
	u8     _r_b:6;

	u8     status;

	u32    _r_c;

	__be32 sense_data_len;
	__be32 response_data_len;

	u8     resp_data[0];
	u8     sense_data[0];
} __attribute__ ((packed));

struct ssp_command_iu {
	u8     lun[8];
	u8     _r_a;

	union {
		struct {
			u8  attr:3;
			u8  prio:4;
			u8  efb:1;
		};
		u8 efb_prio_attr;
	};

	u8    _r_b;

	u8    _r_c:2;
	u8    add_cdb_len:6;

	u8    cdb[16];
	u8    add_cdb[0];
} __attribute__ ((packed));

struct xfer_rdy_iu {
	__be32 requested_offset;
	__be32 write_data_len;
	__be32 _r_a;
} __attribute__ ((packed));

struct ssp_tmf_iu {
	u8     lun[8];
	u16    _r_a;
	u8     tmf;
	u8     _r_b;
	__be16 tag;
	u8     _r_c[14];
} __attribute__ ((packed));

/* ---------- SMP ---------- */

struct report_general_resp {
	__be16  change_count;
	__be16  route_indexes;
	u8      _r_a;
	u8      num_phys;

	u8      conf_route_table:1;
	u8      configuring:1;
	u8	config_others:1;
	u8	orej_retry_supp:1;
	u8	stp_cont_awt:1;
	u8	self_config:1;
	u8	zone_config:1;
	u8	t2t_supp:1;

	u8      _r_c;

	u8      enclosure_logical_id[8];

	u8      _r_d[12];
} __attribute__ ((packed));

struct discover_resp {
	u8    _r_a[5];

	u8    phy_id;
	__be16 _r_b;

	u8    _r_c:4;
	u8    attached_dev_type:3;
	u8    _r_d:1;

	u8    linkrate:4;
	u8    _r_e:4;

	u8    attached_sata_host:1;
	u8    iproto:3;
	u8    _r_f:4;

	u8    attached_sata_dev:1;
	u8    tproto:3;
	u8    _r_g:3;
	u8    attached_sata_ps:1;

	u8    sas_addr[8];
	u8    attached_sas_addr[8];
	u8    attached_phy_id;

	u8    _r_h[7];

	u8    hmin_linkrate:4;
	u8    pmin_linkrate:4;
	u8    hmax_linkrate:4;
	u8    pmax_linkrate:4;

	u8    change_count;

	u8    pptv:4;
	u8    _r_i:3;
	u8    virtual:1;

	u8    routing_attr:4;
	u8    _r_j:4;

	u8    conn_type;
	u8    conn_el_index;
	u8    conn_phy_link;

	u8    _r_k[8];
} __attribute__ ((packed));

struct report_phy_sata_resp {
	u8    _r_a[5];

	u8    phy_id;
	u8    _r_b;

	u8    affil_valid:1;
	u8    affil_supp:1;
	u8    _r_c:6;

	u32    _r_d;

	u8    stp_sas_addr[8];

	struct dev_to_host_fis fis;

	u32   _r_e;

	u8    affil_stp_ini_addr[8];

	__be32 crc;
} __attribute__ ((packed));

struct smp_resp {
	u8    frame_type;
	u8    function;
	u8    result;
	u8    reserved;
	union {
		struct report_general_resp  rg;
		struct discover_resp        disc;
		struct report_phy_sata_resp rps;
	};
} __attribute__ ((packed));

#elif defined(__BIG_ENDIAN_BITFIELD)
struct sas_identify_frame {
	/* Byte 0 */
	u8  _un0:1;
	u8  dev_type:3;
	u8  frame_type:4;

	/* Byte 1 */
	u8  _un1;

	/* Byte 2 */
	union {
		struct {
			u8  _un247:4;
			u8  ssp_iport:1;
			u8  stp_iport:1;
			u8  smp_iport:1;
			u8  _un20:1;
		};
		u8 initiator_bits;
	};

	/* Byte 3 */
	union {
		struct {
			u8 _un347:4;
			u8 ssp_tport:1;
			u8 stp_tport:1;
			u8 smp_tport:1;
			u8 _un30:1;
		};
		u8 target_bits;
	};

	/* Byte 4 - 11 */
	u8 _un4_11[8];

	/* Byte 12 - 19 */
	u8 sas_addr[SAS_ADDR_SIZE];

	/* Byte 20 */
	u8 phy_id;

	u8 _un21_27[7];

	__be32 crc;
} __attribute__ ((packed));

struct ssp_frame_hdr {
	u8     frame_type;
	u8     hashed_dest_addr[HASHED_SAS_ADDR_SIZE];
	u8     _r_a;
	u8     hashed_src_addr[HASHED_SAS_ADDR_SIZE];
	__be16 _r_b;

	u8     _r_c:5;
	u8     retry_data_frames:1;
	u8     retransmit:1;
	u8     changing_data_ptr:1;

	u8     _r_d:6;
	u8     num_fill_bytes:2;

	u32    _r_e;
	__be16 tag;
	__be16 tptt;
	__be32 data_offs;
} __attribute__ ((packed));

struct ssp_response_iu {
	u8     _r_a[10];

	u8     _r_b:6;
	u8     datapres:2;

	u8     status;

	u32    _r_c;

	__be32 sense_data_len;
	__be32 response_data_len;

	u8     resp_data[0];
	u8     sense_data[0];
} __attribute__ ((packed));

struct ssp_command_iu {
	u8     lun[8];
	u8     _r_a;

	union {
		struct {
			u8  efb:1;
			u8  prio:4;
			u8  attr:3;
		};
		u8 efb_prio_attr;
	};

	u8    _r_b;

	u8    add_cdb_len:6;
	u8    _r_c:2;

	u8    cdb[16];
	u8    add_cdb[0];
} __attribute__ ((packed));

struct xfer_rdy_iu {
	__be32 requested_offset;
	__be32 write_data_len;
	__be32 _r_a;
} __attribute__ ((packed));

struct ssp_tmf_iu {
	u8     lun[8];
	u16    _r_a;
	u8     tmf;
	u8     _r_b;
	__be16 tag;
	u8     _r_c[14];
} __attribute__ ((packed));

/* ---------- SMP ---------- */

struct report_general_resp {
	__be16  change_count;
	__be16  route_indexes;
	u8      _r_a;
	u8      num_phys;

	u8	t2t_supp:1;
	u8	zone_config:1;
	u8	self_config:1;
	u8	stp_cont_awt:1;
	u8	orej_retry_supp:1;
	u8	config_others:1;
	u8      configuring:1;
	u8      conf_route_table:1;

	u8      _r_c;

	u8      enclosure_logical_id[8];

	u8      _r_d[12];
} __attribute__ ((packed));

struct discover_resp {
	u8    _r_a[5];

	u8    phy_id;
	__be16 _r_b;

	u8    _r_d:1;
	u8    attached_dev_type:3;
	u8    _r_c:4;

	u8    _r_e:4;
	u8    linkrate:4;

	u8    _r_f:4;
	u8    iproto:3;
	u8    attached_sata_host:1;

	u8    attached_sata_ps:1;
	u8    _r_g:3;
	u8    tproto:3;
	u8    attached_sata_dev:1;

	u8    sas_addr[8];
	u8    attached_sas_addr[8];
	u8    attached_phy_id;

	u8    _r_h[7];

	u8    pmin_linkrate:4;
	u8    hmin_linkrate:4;
	u8    pmax_linkrate:4;
	u8    hmax_linkrate:4;

	u8    change_count;

	u8    virtual:1;
	u8    _r_i:3;
	u8    pptv:4;

	u8    _r_j:4;
	u8    routing_attr:4;

	u8    conn_type;
	u8    conn_el_index;
	u8    conn_phy_link;

	u8    _r_k[8];
} __attribute__ ((packed));

struct report_phy_sata_resp {
	u8    _r_a[5];

	u8    phy_id;
	u8    _r_b;

	u8    _r_c:6;
	u8    affil_supp:1;
	u8    affil_valid:1;

	u32   _r_d;

	u8    stp_sas_addr[8];

	struct dev_to_host_fis fis;

	u32   _r_e;

	u8    affil_stp_ini_addr[8];

	__be32 crc;
} __attribute__ ((packed));

struct smp_resp {
	u8    frame_type;
	u8    function;
	u8    result;
	u8    reserved;
	union {
		struct report_general_resp  rg;
		struct discover_resp        disc;
		struct report_phy_sata_resp rps;
	};
} __attribute__ ((packed));

#else
#error "Bitfield order not defined!"
#endif

#endif /* _SAS_H_ */
