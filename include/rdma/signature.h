/* SPDX-License-Identifier: (GPL-2.0 OR Linux-OpenIB) */
/*
 * Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved.
 */

#ifndef _RDMA_SIGNATURE_H_
#define _RDMA_SIGNATURE_H_

enum ib_signature_prot_cap {
	IB_PROT_T10DIF_TYPE_1 = 1,
	IB_PROT_T10DIF_TYPE_2 = 1 << 1,
	IB_PROT_T10DIF_TYPE_3 = 1 << 2,
};

enum ib_signature_guard_cap {
	IB_GUARD_T10DIF_CRC	= 1,
	IB_GUARD_T10DIF_CSUM	= 1 << 1,
};

/**
 * enum ib_signature_type - Signature types
 * @IB_SIG_TYPE_NONE: Unprotected.
 * @IB_SIG_TYPE_T10_DIF: Type T10-DIF
 */
enum ib_signature_type {
	IB_SIG_TYPE_NONE,
	IB_SIG_TYPE_T10_DIF,
};

/**
 * enum ib_t10_dif_bg_type - Signature T10-DIF block-guard types
 * @IB_T10DIF_CRC: Corresponds to T10-PI mandated CRC checksum rules.
 * @IB_T10DIF_CSUM: Corresponds to IP checksum rules.
 */
enum ib_t10_dif_bg_type {
	IB_T10DIF_CRC,
	IB_T10DIF_CSUM,
};

/**
 * struct ib_t10_dif_domain - Parameters specific for T10-DIF
 *     domain.
 * @bg_type: T10-DIF block guard type (CRC|CSUM)
 * @pi_interval: protection information interval.
 * @bg: seed of guard computation.
 * @app_tag: application tag of guard block
 * @ref_tag: initial guard block reference tag.
 * @ref_remap: Indicate wethear the reftag increments each block
 * @app_escape: Indicate to skip block check if apptag=0xffff
 * @ref_escape: Indicate to skip block check if reftag=0xffffffff
 * @apptag_check_mask: check bitmask of application tag.
 */
struct ib_t10_dif_domain {
	enum ib_t10_dif_bg_type bg_type;
	u16			pi_interval;
	u16			bg;
	u16			app_tag;
	u32			ref_tag;
	bool			ref_remap;
	bool			app_escape;
	bool			ref_escape;
	u16			apptag_check_mask;
};

/**
 * struct ib_sig_domain - Parameters for signature domain
 * @sig_type: specific signauture type
 * @sig: union of all signature domain attributes that may
 *     be used to set domain layout.
 */
struct ib_sig_domain {
	enum ib_signature_type sig_type;
	union {
		struct ib_t10_dif_domain dif;
	} sig;
};

/**
 * struct ib_sig_attrs - Parameters for signature handover operation
 * @check_mask: bitmask for signature byte check (8 bytes)
 * @mem: memory domain layout descriptor.
 * @wire: wire domain layout descriptor.
 * @meta_length: metadata length
 */
struct ib_sig_attrs {
	u8			check_mask;
	struct ib_sig_domain	mem;
	struct ib_sig_domain	wire;
	int			meta_length;
};

enum ib_sig_err_type {
	IB_SIG_BAD_GUARD,
	IB_SIG_BAD_REFTAG,
	IB_SIG_BAD_APPTAG,
};

/*
 * Signature check masks (8 bytes in total) according to the T10-PI standard:
 *  -------- -------- ------------
 * | GUARD  | APPTAG |   REFTAG   |
 * |  2B    |  2B    |    4B      |
 *  -------- -------- ------------
 */
enum {
	IB_SIG_CHECK_GUARD = 0xc0,
	IB_SIG_CHECK_APPTAG = 0x30,
	IB_SIG_CHECK_REFTAG = 0x0f,
};

/*
 * struct ib_sig_err - signature error descriptor
 */
struct ib_sig_err {
	enum ib_sig_err_type	err_type;
	u32			expected;
	u32			actual;
	u64			sig_err_offset;
	u32			key;
};

#endif /* _RDMA_SIGNATURE_H_ */
