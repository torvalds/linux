/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#ifndef _USR_IDXD_H_
#define _USR_IDXD_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

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
	DSA_OPCODE_CRCGEN = 0x10,
	DSA_OPCODE_COPY_CRC,
	DSA_OPCODE_DIF_CHECK,
	DSA_OPCODE_DIF_INS,
	DSA_OPCODE_DIF_STRP,
	DSA_OPCODE_DIF_UPDT,
	DSA_OPCODE_CFLUSH = 0x20,
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
};

#define DSA_COMP_STATUS_MASK		0x7f
#define DSA_COMP_STATUS_WRITE		0x80

struct dsa_hw_desc {
	uint32_t	pasid:20;
	uint32_t	rsvd:11;
	uint32_t	priv:1;
	uint32_t	flags:24;
	uint32_t	opcode:8;
	uint64_t	completion_addr;
	union {
		uint64_t	src_addr;
		uint64_t	rdback_addr;
		uint64_t	pattern;
		uint64_t	desc_list_addr;
	};
	union {
		uint64_t	dst_addr;
		uint64_t	rdback_addr2;
		uint64_t	src2_addr;
		uint64_t	comp_pattern;
	};
	union {
		uint32_t	xfer_size;
		uint32_t	desc_count;
	};
	uint16_t	int_handle;
	uint16_t	rsvd1;
	union {
		uint8_t		expected_res;
		/* create delta record */
		struct {
			uint64_t	delta_addr;
			uint32_t	max_delta_size;
			uint32_t 	delt_rsvd;
			uint8_t 	expected_res_mask;
		};
		uint32_t	delta_rec_size;
		uint64_t	dest2;
		/* CRC */
		struct {
			uint32_t	crc_seed;
			uint32_t	crc_rsvd;
			uint64_t	seed_addr;
		};
		/* DIF check or strip */
		struct {
			uint8_t		src_dif_flags;
			uint8_t		dif_chk_res;
			uint8_t		dif_chk_flags;
			uint8_t		dif_chk_res2[5];
			uint32_t	chk_ref_tag_seed;
			uint16_t	chk_app_tag_mask;
			uint16_t	chk_app_tag_seed;
		};
		/* DIF insert */
		struct {
			uint8_t		dif_ins_res;
			uint8_t		dest_dif_flag;
			uint8_t		dif_ins_flags;
			uint8_t		dif_ins_res2[13];
			uint32_t	ins_ref_tag_seed;
			uint16_t	ins_app_tag_mask;
			uint16_t	ins_app_tag_seed;
		};
		/* DIF update */
		struct {
			uint8_t		src_upd_flags;
			uint8_t		upd_dest_flags;
			uint8_t		dif_upd_flags;
			uint8_t		dif_upd_res[5];
			uint32_t	src_ref_tag_seed;
			uint16_t	src_app_tag_mask;
			uint16_t	src_app_tag_seed;
			uint32_t	dest_ref_tag_seed;
			uint16_t	dest_app_tag_mask;
			uint16_t	dest_app_tag_seed;
		};

		uint8_t		op_specific[24];
	};
} __attribute__((packed));

struct dsa_raw_desc {
	uint64_t	field[8];
} __attribute__((packed));

/*
 * The status field will be modified by hardware, therefore it should be
 * volatile and prevent the compiler from optimize the read.
 */
struct dsa_completion_record {
	volatile uint8_t	status;
	union {
		uint8_t		result;
		uint8_t		dif_status;
	};
	uint16_t		rsvd;
	uint32_t		bytes_completed;
	uint64_t		fault_addr;
	union {
		/* common record */
		struct {
			uint32_t	invalid_flags:24;
			uint32_t	rsvd2:8;
		};

		uint32_t	delta_rec_size;
		uint64_t	crc_val;

		/* DIF check & strip */
		struct {
			uint32_t	dif_chk_ref_tag;
			uint16_t	dif_chk_app_tag_mask;
			uint16_t	dif_chk_app_tag;
		};

		/* DIF insert */
		struct {
			uint64_t	dif_ins_res;
			uint32_t	dif_ins_ref_tag;
			uint16_t	dif_ins_app_tag_mask;
			uint16_t	dif_ins_app_tag;
		};

		/* DIF update */
		struct {
			uint32_t	dif_upd_src_ref_tag;
			uint16_t	dif_upd_src_app_tag_mask;
			uint16_t	dif_upd_src_app_tag;
			uint32_t	dif_upd_dest_ref_tag;
			uint16_t	dif_upd_dest_app_tag_mask;
			uint16_t	dif_upd_dest_app_tag;
		};

		uint8_t		op_specific[16];
	};
} __attribute__((packed));

struct dsa_raw_completion_record {
	uint64_t	field[4];
} __attribute__((packed));

#endif
