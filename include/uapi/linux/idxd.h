/* SPDX-License-Identifier: LGPL-2.1 WITH Linux-syscall-note */
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#ifndef _USR_IDXD_H_
#define _USR_IDXD_H_

#include <linux/types.h>

/* Driver command error status */
enum idxd_scmd_stat {
	IDXD_SCMD_DEV_ENABLED = 0x80000010,
	IDXD_SCMD_DEV_NOT_ENABLED = 0x80000020,
	IDXD_SCMD_WQ_ENABLED = 0x80000021,
	IDXD_SCMD_DEV_DMA_ERR = 0x80020000,
	IDXD_SCMD_WQ_NO_GRP = 0x80030000,
	IDXD_SCMD_WQ_NO_NAME = 0x80040000,
	IDXD_SCMD_WQ_NO_SVM = 0x80050000,
	IDXD_SCMD_WQ_NO_THRESH = 0x80060000,
	IDXD_SCMD_WQ_PORTAL_ERR = 0x80070000,
	IDXD_SCMD_WQ_RES_ALLOC_ERR = 0x80080000,
	IDXD_SCMD_PERCPU_ERR = 0x80090000,
	IDXD_SCMD_DMA_CHAN_ERR = 0x800a0000,
	IDXD_SCMD_CDEV_ERR = 0x800b0000,
	IDXD_SCMD_WQ_NO_SWQ_SUPPORT = 0x800c0000,
	IDXD_SCMD_WQ_NONE_CONFIGURED = 0x800d0000,
	IDXD_SCMD_WQ_NO_SIZE = 0x800e0000,
	IDXD_SCMD_WQ_NO_PRIV = 0x800f0000,
	IDXD_SCMD_WQ_IRQ_ERR = 0x80100000,
	IDXD_SCMD_WQ_USER_NO_IOMMU = 0x80110000,
	IDXD_SCMD_DEV_EVL_ERR = 0x80120000,
	IDXD_SCMD_WQ_NO_DRV_NAME = 0x80200000,
};

#define IDXD_SCMD_SOFTERR_MASK	0x80000000
#define IDXD_SCMD_SOFTERR_SHIFT	16

/* Descriptor flags */
#define IDXD_OP_FLAG_FENCE	0x0001
#define IDXD_OP_FLAG_BOF	0x0002
#define IDXD_OP_FLAG_CRAV	0x0004
#define IDXD_OP_FLAG_RCR	0x0008
#define IDXD_OP_FLAG_RCI	0x0010
#define IDXD_OP_FLAG_CRSTS	0x0020
#define IDXD_OP_FLAG_CR		0x0080
#define IDXD_OP_FLAG_CC		0x0100
#define IDXD_OP_FLAG_ADDR1_TCS	0x0200
#define IDXD_OP_FLAG_ADDR2_TCS	0x0400
#define IDXD_OP_FLAG_ADDR3_TCS	0x0800
#define IDXD_OP_FLAG_CR_TCS	0x1000
#define IDXD_OP_FLAG_STORD	0x2000
#define IDXD_OP_FLAG_DRDBK	0x4000
#define IDXD_OP_FLAG_DSTS	0x8000

/* IAX */
#define IDXD_OP_FLAG_RD_SRC2_AECS	0x010000
#define IDXD_OP_FLAG_RD_SRC2_2ND	0x020000
#define IDXD_OP_FLAG_WR_SRC2_AECS_COMP	0x040000
#define IDXD_OP_FLAG_WR_SRC2_AECS_OVFL	0x080000
#define IDXD_OP_FLAG_SRC2_STS		0x100000
#define IDXD_OP_FLAG_CRC_RFC3720	0x200000

/* Opcode */
enum dsa_opcode {
	DSA_OPCODE_NOOP = 0,
	DSA_OPCODE_BATCH,
	DSA_OPCODE_DRAIN,
	DSA_OPCODE_MEMMOVE,
	DSA_OPCODE_MEMFILL,
	DSA_OPCODE_COMPARE,
	DSA_OPCODE_COMPVAL,
	DSA_OPCODE_CR_DELTA,
	DSA_OPCODE_AP_DELTA,
	DSA_OPCODE_DUALCAST,
	DSA_OPCODE_TRANSL_FETCH,
	DSA_OPCODE_CRCGEN = 0x10,
	DSA_OPCODE_COPY_CRC,
	DSA_OPCODE_DIF_CHECK,
	DSA_OPCODE_DIF_INS,
	DSA_OPCODE_DIF_STRP,
	DSA_OPCODE_DIF_UPDT,
	DSA_OPCODE_DIX_GEN = 0x17,
	DSA_OPCODE_CFLUSH = 0x20,
};

enum iax_opcode {
	IAX_OPCODE_NOOP = 0,
	IAX_OPCODE_DRAIN = 2,
	IAX_OPCODE_MEMMOVE,
	IAX_OPCODE_DECOMPRESS = 0x42,
	IAX_OPCODE_COMPRESS,
	IAX_OPCODE_CRC64,
	IAX_OPCODE_ZERO_DECOMP_32 = 0x48,
	IAX_OPCODE_ZERO_DECOMP_16,
	IAX_OPCODE_ZERO_COMP_32 = 0x4c,
	IAX_OPCODE_ZERO_COMP_16,
	IAX_OPCODE_SCAN = 0x50,
	IAX_OPCODE_SET_MEMBER,
	IAX_OPCODE_EXTRACT,
	IAX_OPCODE_SELECT,
	IAX_OPCODE_RLE_BURST,
	IAX_OPCODE_FIND_UNIQUE,
	IAX_OPCODE_EXPAND,
};

/* Completion record status */
enum dsa_completion_status {
	DSA_COMP_NONE = 0,
	DSA_COMP_SUCCESS,
	DSA_COMP_SUCCESS_PRED,
	DSA_COMP_PAGE_FAULT_NOBOF,
	DSA_COMP_PAGE_FAULT_IR,
	DSA_COMP_BATCH_FAIL,
	DSA_COMP_BATCH_PAGE_FAULT,
	DSA_COMP_DR_OFFSET_NOINC,
	DSA_COMP_DR_OFFSET_ERANGE,
	DSA_COMP_DIF_ERR,
	DSA_COMP_BAD_OPCODE = 0x10,
	DSA_COMP_INVALID_FLAGS,
	DSA_COMP_NOZERO_RESERVE,
	DSA_COMP_XFER_ERANGE,
	DSA_COMP_DESC_CNT_ERANGE,
	DSA_COMP_DR_ERANGE,
	DSA_COMP_OVERLAP_BUFFERS,
	DSA_COMP_DCAST_ERR,
	DSA_COMP_DESCLIST_ALIGN,
	DSA_COMP_INT_HANDLE_INVAL,
	DSA_COMP_CRA_XLAT,
	DSA_COMP_CRA_ALIGN,
	DSA_COMP_ADDR_ALIGN,
	DSA_COMP_PRIV_BAD,
	DSA_COMP_TRAFFIC_CLASS_CONF,
	DSA_COMP_PFAULT_RDBA,
	DSA_COMP_HW_ERR1,
	DSA_COMP_HW_ERR_DRB,
	DSA_COMP_TRANSLATION_FAIL,
	DSA_COMP_DRAIN_EVL = 0x26,
	DSA_COMP_BATCH_EVL_ERR,
};

enum iax_completion_status {
	IAX_COMP_NONE = 0,
	IAX_COMP_SUCCESS,
	IAX_COMP_PAGE_FAULT_IR = 0x04,
	IAX_COMP_ANALYTICS_ERROR = 0x0a,
	IAX_COMP_OUTBUF_OVERFLOW,
	IAX_COMP_BAD_OPCODE = 0x10,
	IAX_COMP_INVALID_FLAGS,
	IAX_COMP_NOZERO_RESERVE,
	IAX_COMP_INVALID_SIZE,
	IAX_COMP_OVERLAP_BUFFERS = 0x16,
	IAX_COMP_INT_HANDLE_INVAL = 0x19,
	IAX_COMP_CRA_XLAT,
	IAX_COMP_CRA_ALIGN,
	IAX_COMP_ADDR_ALIGN,
	IAX_COMP_PRIV_BAD,
	IAX_COMP_TRAFFIC_CLASS_CONF,
	IAX_COMP_PFAULT_RDBA,
	IAX_COMP_HW_ERR1,
	IAX_COMP_HW_ERR_DRB,
	IAX_COMP_TRANSLATION_FAIL,
	IAX_COMP_PRS_TIMEOUT,
	IAX_COMP_WATCHDOG,
	IAX_COMP_INVALID_COMP_FLAG = 0x30,
	IAX_COMP_INVALID_FILTER_FLAG,
	IAX_COMP_INVALID_INPUT_SIZE,
	IAX_COMP_INVALID_NUM_ELEMS,
	IAX_COMP_INVALID_SRC1_WIDTH,
	IAX_COMP_INVALID_INVERT_OUT,
};

#define DSA_COMP_STATUS_MASK		0x7f
#define DSA_COMP_STATUS_WRITE		0x80
#define DSA_COMP_STATUS(status)		((status) & DSA_COMP_STATUS_MASK)

struct dsa_hw_desc {
	__u32	pasid:20;
	__u32	rsvd:11;
	__u32	priv:1;
	__u32	flags:24;
	__u32	opcode:8;
	__u64	completion_addr;
	union {
		__u64	src_addr;
		__u64	rdback_addr;
		__u64	pattern;
		__u64	desc_list_addr;
		__u64	pattern_lower;
		__u64	transl_fetch_addr;
	};
	union {
		__u64	dst_addr;
		__u64	rdback_addr2;
		__u64	src2_addr;
		__u64	comp_pattern;
	};
	union {
		__u32	xfer_size;
		__u32	desc_count;
		__u32	region_size;
	};
	__u16	int_handle;
	__u16	rsvd1;
	union {
		__u8		expected_res;
		/* create delta record */
		struct {
			__u64	delta_addr;
			__u32	max_delta_size;
			__u32	delt_rsvd;
			__u8	expected_res_mask;
		};
		__u32	delta_rec_size;
		__u64	dest2;
		/* CRC */
		struct {
			__u32	crc_seed;
			__u32	crc_rsvd;
			__u64	seed_addr;
		};
		/* DIF check or strip */
		struct {
			__u8	src_dif_flags;
			__u8	dif_chk_res;
			__u8	dif_chk_flags;
			__u8	dif_chk_res2[5];
			__u32	chk_ref_tag_seed;
			__u16	chk_app_tag_mask;
			__u16	chk_app_tag_seed;
		};
		/* DIF insert */
		struct {
			__u8	dif_ins_res;
			__u8	dest_dif_flag;
			__u8	dif_ins_flags;
			__u8	dif_ins_res2[13];
			__u32	ins_ref_tag_seed;
			__u16	ins_app_tag_mask;
			__u16	ins_app_tag_seed;
		};
		/* DIF update */
		struct {
			__u8	src_upd_flags;
			__u8	upd_dest_flags;
			__u8	dif_upd_flags;
			__u8	dif_upd_res[5];
			__u32	src_ref_tag_seed;
			__u16	src_app_tag_mask;
			__u16	src_app_tag_seed;
			__u32	dest_ref_tag_seed;
			__u16	dest_app_tag_mask;
			__u16	dest_app_tag_seed;
		};

		/* Fill */
		__u64	pattern_upper;

		/* Translation fetch */
		struct {
			__u64	transl_fetch_res;
			__u32	region_stride;
		};

		/* DIX generate */
		struct {
			__u8	dix_gen_res;
			__u8	dest_dif_flags;
			__u8	dif_flags;
			__u8	dix_gen_res2[13];
			__u32	ref_tag_seed;
			__u16	app_tag_mask;
			__u16	app_tag_seed;
		};

		__u8		op_specific[24];
	};
} __attribute__((packed));

struct iax_hw_desc {
	__u32        pasid:20;
	__u32        rsvd:11;
	__u32        priv:1;
	__u32        flags:24;
	__u32        opcode:8;
	__u64        completion_addr;
	__u64        src1_addr;
	__u64        dst_addr;
	__u32        src1_size;
	__u16        int_handle;
	union {
		__u16        compr_flags;
		__u16        decompr_flags;
	};
	__u64	src2_addr;
	__u32	max_dst_size;
	__u32	src2_size;
	__u32	filter_flags;
	__u32	num_inputs;
} __attribute__((packed));

struct dsa_raw_desc {
	__u64	field[8];
} __attribute__((packed));

/*
 * The status field will be modified by hardware, therefore it should be
 * volatile and prevent the compiler from optimize the read.
 */
struct dsa_completion_record {
	volatile __u8	status;
	union {
		__u8		result;
		__u8		dif_status;
	};
	__u8			fault_info;
	__u8			rsvd;
	union {
		__u32		bytes_completed;
		__u32		descs_completed;
	};
	__u64		fault_addr;
	union {
		/* common record */
		struct {
			__u32	invalid_flags:24;
			__u32	rsvd2:8;
		};

		__u32	delta_rec_size;
		__u64	crc_val;

		/* DIF check & strip */
		struct {
			__u32	dif_chk_ref_tag;
			__u16	dif_chk_app_tag_mask;
			__u16	dif_chk_app_tag;
		};

		/* DIF insert */
		struct {
			__u64	dif_ins_res;
			__u32	dif_ins_ref_tag;
			__u16	dif_ins_app_tag_mask;
			__u16	dif_ins_app_tag;
		};

		/* DIF update */
		struct {
			__u32	dif_upd_src_ref_tag;
			__u16	dif_upd_src_app_tag_mask;
			__u16	dif_upd_src_app_tag;
			__u32	dif_upd_dest_ref_tag;
			__u16	dif_upd_dest_app_tag_mask;
			__u16	dif_upd_dest_app_tag;
		};

		/* DIX generate */
		struct {
			__u64	dix_gen_res;
			__u32	dix_ref_tag;
			__u16	dix_app_tag_mask;
			__u16	dix_app_tag;
		};

		__u8		op_specific[16];
	};
} __attribute__((packed));

struct dsa_raw_completion_record {
	__u64	field[4];
} __attribute__((packed));

struct iax_completion_record {
	volatile __u8        status;
	__u8                 error_code;
	__u8		     fault_info;
	__u8		     rsvd;
	__u32                bytes_completed;
	__u64                fault_addr;
	__u32                invalid_flags;
	__u32                rsvd2;
	__u32                output_size;
	__u8                 output_bits;
	__u8                 rsvd3;
	__u16                xor_csum;
	__u32                crc;
	__u32                min;
	__u32                max;
	__u32                sum;
	__u64                rsvd4[2];
} __attribute__((packed));

struct iax_raw_completion_record {
	__u64	field[8];
} __attribute__((packed));

#endif
