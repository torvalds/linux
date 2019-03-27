/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 * Define common SLI-4 structures and function prototypes.
 */

#ifndef _SLI4_H
#define _SLI4_H

#include "ocs_os.h"

#define SLI_PAGE_SIZE		(4096)
#define SLI_SUB_PAGE_MASK	(SLI_PAGE_SIZE - 1)
#define SLI_PAGE_SHIFT		12
#define SLI_ROUND_PAGE(b)	(((b) + SLI_SUB_PAGE_MASK) & ~SLI_SUB_PAGE_MASK)

#define SLI4_BMBX_TIMEOUT_MSEC		30000
#define SLI4_FW_READY_TIMEOUT_MSEC	30000

static inline uint32_t
sli_page_count(size_t bytes, uint32_t page_size)
{
	uint32_t	mask = page_size - 1;
	uint32_t	shift = 0;

	switch (page_size) {
	case 4096:
		shift = 12;
		break;
	case 8192:
		shift = 13;
		break;
	case 16384:
		shift = 14;
		break;
	case 32768:
		shift = 15;
		break;
	case 65536:
		shift = 16;
		break;
	default:
		return 0;
	}

	return (bytes + mask) >> shift;
}

/*************************************************************************
 * Common PCI configuration space register definitions
 */

#define SLI4_PCI_CLASS_REVISION		0x0008	/** register offset */
#define SLI4_PCI_REV_ID_SHIFT			0
#define SLI4_PCI_REV_ID_MASK			0xff
#define SLI4_PCI_CLASS_SHIFT			8
#define SLI4_PCI_CLASS_MASK			0xfff

#define SLI4_PCI_SOFT_RESET_CSR		0x005c	/** register offset */
#define SLI4_PCI_SOFT_RESET_MASK		0x0080

/*************************************************************************
 * Common SLI-4 register offsets and field definitions
 */

/**
 * @brief SLI_INTF - SLI Interface Definition Register
 */
#define SLI4_INTF_REG			0x0058	/** register offset */
#define SLI4_INTF_VALID_SHIFT			29
#define SLI4_INTF_VALID_MASK			0x7
#define SLI4_INTF_VALID				0x6
#define SLI4_INTF_IF_TYPE_SHIFT			12
#define SLI4_INTF_IF_TYPE_MASK			0xf
#define SLI4_INTF_SLI_FAMILY_SHIFT		8
#define SLI4_INTF_SLI_FAMILY_MASK		0xf
#define SLI4_INTF_SLI_REVISION_SHIFT		4
#define SLI4_INTF_SLI_REVISION_MASK		0xf
#define SLI4_FAMILY_CHECK_ASIC_TYPE		0xf

#define SLI4_IF_TYPE_BE3_SKH_PF		0
#define SLI4_IF_TYPE_BE3_SKH_VF		1
#define SLI4_IF_TYPE_LANCER_FC_ETH	2
#define SLI4_IF_TYPE_LANCER_RDMA	3
#define SLI4_MAX_IF_TYPES		4

/**
 * @brief ASIC_ID - SLI ASIC Type and Revision Register
 */
#define SLI4_ASIC_ID_REG			0x009c /* register offset */
#define SLI4_ASIC_REV_SHIFT			0
#define SLI4_ASIC_REV_MASK			0xf
#define SLI4_ASIC_VER_SHIFT			4
#define SLI4_ASIC_VER_MASK			0xf
#define SLI4_ASIC_GEN_SHIFT			8
#define SLI4_ASIC_GEN_MASK			0xff
#define SLI4_ASIC_GEN_BE2			0x00
#define SLI4_ASIC_GEN_BE3			0x03
#define SLI4_ASIC_GEN_SKYHAWK			0x04
#define SLI4_ASIC_GEN_CORSAIR			0x05
#define SLI4_ASIC_GEN_LANCER			0x0b


/**
 * @brief BMBX - Bootstrap Mailbox Register
 */
#define SLI4_BMBX_REG			0x0160	/* register offset */
#define SLI4_BMBX_MASK_HI			0x3
#define SLI4_BMBX_MASK_LO			0xf
#define SLI4_BMBX_RDY				BIT(0)
#define SLI4_BMBX_HI				BIT(1)
#define SLI4_BMBX_WRITE_HI(r)			((ocs_addr32_hi(r) & ~SLI4_BMBX_MASK_HI) | \
								SLI4_BMBX_HI)
#define SLI4_BMBX_WRITE_LO(r)			(((ocs_addr32_hi(r) & SLI4_BMBX_MASK_HI) << 30) | \
								(((r) & ~SLI4_BMBX_MASK_LO) >> 2))

#define SLI4_BMBX_SIZE			256


/**
 * @brief EQCQ_DOORBELL - EQ and CQ Doorbell Register
 */
#define SLI4_EQCQ_DOORBELL_REG		0x120
#define SLI4_EQCQ_DOORBELL_CI			BIT(9)
#define SLI4_EQCQ_DOORBELL_QT			BIT(10)
#define SLI4_EQCQ_DOORBELL_ARM			BIT(29)
#define SLI4_EQCQ_DOORBELL_SE			BIT(31)
#define SLI4_EQCQ_NUM_SHIFT			16
#define SLI4_EQCQ_NUM_MASK			0x01ff
#define SLI4_EQCQ_EQ_ID_MASK			0x3fff
#define SLI4_EQCQ_CQ_ID_MASK			0x7fff
#define SLI4_EQCQ_EQ_ID_MASK_LO			0x01ff
#define SLI4_EQCQ_CQ_ID_MASK_LO			0x03ff
#define SLI4_EQCQ_EQCQ_ID_MASK_HI		0xf800

/**
 * @brief SLIPORT_CONTROL - SLI Port Control Register
 */
#define SLI4_SLIPORT_CONTROL_REG	0x0408
#define SLI4_SLIPORT_CONTROL_END		BIT(30)
#define SLI4_SLIPORT_CONTROL_LITTLE_ENDIAN	(0)
#define SLI4_SLIPORT_CONTROL_BIG_ENDIAN		BIT(30)
#define SLI4_SLIPORT_CONTROL_IP			BIT(27)
#define SLI4_SLIPORT_CONTROL_IDIS		BIT(22)
#define SLI4_SLIPORT_CONTROL_FDD		BIT(31)

/**
 * @brief SLI4_SLIPORT_ERROR1 - SLI Port Error Register
 */
#define SLI4_SLIPORT_ERROR1		0x040c

/**
 * @brief SLI4_SLIPORT_ERROR2 - SLI Port Error Register
 */
#define SLI4_SLIPORT_ERROR2		0x0410

/**
 * @brief User error registers
 */
#define SLI4_UERR_STATUS_LOW_REG		0xA0
#define SLI4_UERR_STATUS_HIGH_REG		0xA4
#define SLI4_UERR_MASK_LOW_REG			0xA8
#define SLI4_UERR_MASK_HIGH_REG			0xAC

/**
 * @brief Registers for generating software UE (BE3)
 */
#define SLI4_SW_UE_CSR1			0x138
#define SLI4_SW_UE_CSR2			0x1FFFC

/**
 * @brief Registers for generating software UE (Skyhawk)
 */
#define SLI4_SW_UE_REG			0x5C 	/* register offset */

static inline uint32_t sli_eq_doorbell(uint16_t n_popped, uint16_t id, uint8_t arm)
{
	uint32_t	reg = 0;
#if BYTE_ORDER == LITTLE_ENDIAN
	struct {
		uint32_t	eq_id_lo:9,
				ci:1,			/* clear interrupt */
				qt:1,			/* queue type */
				eq_id_hi:5,
				number_popped:13,
				arm:1,
				:1,
				se:1;
	} * eq_doorbell = (void *)&reg;
#else
#error big endian version not defined
#endif

	eq_doorbell->eq_id_lo = id & SLI4_EQCQ_EQ_ID_MASK_LO;
	eq_doorbell->qt = 1;	/* EQ is type 1 (section 2.2.3.3 SLI Arch) */
	eq_doorbell->eq_id_hi = (id >> 9) & 0x1f;
	eq_doorbell->number_popped = n_popped;
	eq_doorbell->arm = arm;
	eq_doorbell->ci = TRUE;

	return reg;
}

static inline uint32_t sli_cq_doorbell(uint16_t n_popped, uint16_t id, uint8_t arm)
{
	uint32_t	reg = 0;
#if BYTE_ORDER == LITTLE_ENDIAN
	struct {
		uint32_t	cq_id_lo:10,
				qt:1,			/* queue type */
				cq_id_hi:5,
				number_popped:13,
				arm:1,
				:1,
				se:1;
	} * cq_doorbell = (void *)&reg;
#else
#error big endian version not defined
#endif

	cq_doorbell->cq_id_lo = id & SLI4_EQCQ_CQ_ID_MASK_LO;
	cq_doorbell->qt = 0;	/* CQ is type 0 (section 2.2.3.3 SLI Arch) */
	cq_doorbell->cq_id_hi = (id >> 10) & 0x1f;
	cq_doorbell->number_popped = n_popped;
	cq_doorbell->arm = arm;

	return reg;
}

/**
 * @brief MQ_DOORBELL - MQ Doorbell Register
 */
#define SLI4_MQ_DOORBELL_REG		0x0140	/* register offset */
#define SLI4_MQ_DOORBELL_NUM_SHIFT		16
#define SLI4_MQ_DOORBELL_NUM_MASK		0x3fff
#define SLI4_MQ_DOORBELL_ID_MASK		0xffff
#define SLI4_MQ_DOORBELL(n, i)			((((n) & SLI4_MQ_DOORBELL_NUM_MASK) << SLI4_MQ_DOORBELL_NUM_SHIFT) | \
						  ((i) & SLI4_MQ_DOORBELL_ID_MASK))

/**
 * @brief RQ_DOORBELL - RQ Doorbell Register
 */
#define SLI4_RQ_DOORBELL_REG		0x0a0	/* register offset */
#define SLI4_RQ_DOORBELL_NUM_SHIFT		16
#define SLI4_RQ_DOORBELL_NUM_MASK		0x3fff
#define SLI4_RQ_DOORBELL_ID_MASK		0xffff
#define SLI4_RQ_DOORBELL(n, i)			((((n) & SLI4_RQ_DOORBELL_NUM_MASK) << SLI4_RQ_DOORBELL_NUM_SHIFT) | \
						  ((i) & SLI4_RQ_DOORBELL_ID_MASK))

/**
 * @brief WQ_DOORBELL - WQ Doorbell Register
 */
#define SLI4_IO_WQ_DOORBELL_REG		0x040	/* register offset */
#define SLI4_WQ_DOORBELL_IDX_SHIFT		16
#define SLI4_WQ_DOORBELL_IDX_MASK		0x00ff
#define SLI4_WQ_DOORBELL_NUM_SHIFT		24
#define SLI4_WQ_DOORBELL_NUM_MASK		0x00ff
#define SLI4_WQ_DOORBELL_ID_MASK		0xffff
#define SLI4_WQ_DOORBELL(n, x, i)		((((n) & SLI4_WQ_DOORBELL_NUM_MASK) << SLI4_WQ_DOORBELL_NUM_SHIFT) | \
						 (((x) & SLI4_WQ_DOORBELL_IDX_MASK) << SLI4_WQ_DOORBELL_IDX_SHIFT) | \
						  ((i) & SLI4_WQ_DOORBELL_ID_MASK))

/**
 * @brief SLIPORT_SEMAPHORE - SLI Port Host and Port Status Register
 */
#define SLI4_PORT_SEMAPHORE_REG_0	0x00ac	/** register offset Interface Type 0 + 1 */
#define SLI4_PORT_SEMAPHORE_REG_1	0x0180	/** register offset Interface Type 0 + 1 */
#define SLI4_PORT_SEMAPHORE_REG_23	0x0400	/** register offset Interface Type 2 + 3 */
#define SLI4_PORT_SEMAPHORE_PORT_MASK		0x0000ffff
#define SLI4_PORT_SEMAPHORE_PORT(r)		((r) & SLI4_PORT_SEMAPHORE_PORT_MASK)
#define SLI4_PORT_SEMAPHORE_HOST_MASK		0x00ff0000
#define SLI4_PORT_SEMAPHORE_HOST_SHIFT		16
#define SLI4_PORT_SEMAPHORE_HOST(r)		(((r) & SLI4_PORT_SEMAPHORE_HOST_MASK) >> \
								SLI4_PORT_SEMAPHORE_HOST_SHIFT)
#define SLI4_PORT_SEMAPHORE_SCR2		BIT(26)	/** scratch area 2 */
#define SLI4_PORT_SEMAPHORE_SCR1		BIT(27)	/** scratch area 1 */
#define SLI4_PORT_SEMAPHORE_IPC			BIT(28)	/** IP conflict */
#define SLI4_PORT_SEMAPHORE_NIP			BIT(29)	/** no IP address */
#define SLI4_PORT_SEMAPHORE_SFI			BIT(30)	/** secondary firmware image used */
#define SLI4_PORT_SEMAPHORE_PERR		BIT(31)	/** POST fatal error */

#define SLI4_PORT_SEMAPHORE_STATUS_POST_READY	0xc000
#define SLI4_PORT_SEMAPHORE_STATUS_UNRECOV_ERR	0xf000
#define SLI4_PORT_SEMAPHORE_STATUS_ERR_MASK	0xf000
#define SLI4_PORT_SEMAPHORE_IN_ERR(r)		(SLI4_PORT_SEMAPHORE_STATUS_UNRECOV_ERR == ((r) & \
								SLI4_PORT_SEMAPHORE_STATUS_ERR_MASK))

/**
 * @brief SLIPORT_STATUS - SLI Port Status Register
 */

#define SLI4_PORT_STATUS_REG_23		0x0404	/** register offset Interface Type 2 + 3 */
#define SLI4_PORT_STATUS_FDP			BIT(21)	/** function specific dump present */
#define SLI4_PORT_STATUS_RDY			BIT(23)	/** ready */
#define SLI4_PORT_STATUS_RN			BIT(24)	/** reset needed */
#define SLI4_PORT_STATUS_DIP			BIT(25)	/** dump present */
#define SLI4_PORT_STATUS_OTI			BIT(29) /** over temp indicator */
#define SLI4_PORT_STATUS_END			BIT(30)	/** endianness */
#define SLI4_PORT_STATUS_ERR			BIT(31)	/** SLI port error */
#define SLI4_PORT_STATUS_READY(r)		((r) & SLI4_PORT_STATUS_RDY)
#define SLI4_PORT_STATUS_ERROR(r)		((r) & SLI4_PORT_STATUS_ERR)
#define SLI4_PORT_STATUS_DUMP_PRESENT(r)	((r) & SLI4_PORT_STATUS_DIP)
#define SLI4_PORT_STATUS_FDP_PRESENT(r)		((r) & SLI4_PORT_STATUS_FDP)


#define SLI4_PHSDEV_CONTROL_REG_23		0x0414	/** register offset Interface Type 2 + 3 */
#define SLI4_PHYDEV_CONTROL_DRST		BIT(0)	/** physical device reset */
#define SLI4_PHYDEV_CONTROL_FRST		BIT(1)	/** firmware reset */
#define SLI4_PHYDEV_CONTROL_DD			BIT(2)	/** diagnostic dump */
#define SLI4_PHYDEV_CONTROL_FRL_MASK		0x000000f0
#define SLI4_PHYDEV_CONTROL_FRL_SHIFT		4
#define SLI4_PHYDEV_CONTROL_FRL(r)		(((r) & SLI4_PHYDEV_CONTROL_FRL_MASK) >> \
								SLI4_PHYDEV_CONTROL_FRL_SHIFT_SHIFT)

/*************************************************************************
 * SLI-4 mailbox command formats and definitions
 */

typedef struct sli4_mbox_command_header_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:8,
			command:8,
			status:16;	/** Port writes to indicate success / fail */
#else
#error big endian version not defined
#endif
} sli4_mbox_command_header_t;

#define SLI4_MBOX_COMMAND_CONFIG_LINK	0x07
#define SLI4_MBOX_COMMAND_DUMP		0x17
#define SLI4_MBOX_COMMAND_DOWN_LINK	0x06
#define SLI4_MBOX_COMMAND_INIT_LINK	0x05
#define SLI4_MBOX_COMMAND_INIT_VFI	0xa3
#define SLI4_MBOX_COMMAND_INIT_VPI	0xa4
#define SLI4_MBOX_COMMAND_POST_XRI	0xa7
#define SLI4_MBOX_COMMAND_RELEASE_XRI	0xac
#define SLI4_MBOX_COMMAND_READ_CONFIG	0x0b
#define SLI4_MBOX_COMMAND_READ_STATUS	0x0e
#define SLI4_MBOX_COMMAND_READ_NVPARMS	0x02
#define SLI4_MBOX_COMMAND_READ_REV	0x11
#define SLI4_MBOX_COMMAND_READ_LNK_STAT	0x12
#define SLI4_MBOX_COMMAND_READ_SPARM64	0x8d
#define SLI4_MBOX_COMMAND_READ_TOPOLOGY	0x95
#define SLI4_MBOX_COMMAND_REG_FCFI	0xa0
#define SLI4_MBOX_COMMAND_REG_FCFI_MRQ	0xaf
#define SLI4_MBOX_COMMAND_REG_RPI	0x93
#define SLI4_MBOX_COMMAND_REG_RX_RQ	0xa6
#define SLI4_MBOX_COMMAND_REG_VFI	0x9f
#define SLI4_MBOX_COMMAND_REG_VPI	0x96
#define SLI4_MBOX_COMMAND_REQUEST_FEATURES 0x9d
#define SLI4_MBOX_COMMAND_SLI_CONFIG	0x9b
#define SLI4_MBOX_COMMAND_UNREG_FCFI	0xa2
#define SLI4_MBOX_COMMAND_UNREG_RPI	0x14
#define SLI4_MBOX_COMMAND_UNREG_VFI	0xa1
#define SLI4_MBOX_COMMAND_UNREG_VPI	0x97
#define SLI4_MBOX_COMMAND_WRITE_NVPARMS	0x03
#define SLI4_MBOX_COMMAND_CONFIG_AUTO_XFER_RDY	0xAD
#define SLI4_MBOX_COMMAND_CONFIG_AUTO_XFER_RDY_HP       0xAE

#define SLI4_MBOX_STATUS_SUCCESS	0x0000
#define SLI4_MBOX_STATUS_FAILURE	0x0001
#define SLI4_MBOX_STATUS_RPI_NOT_REG	0x1400

/**
 * @brief Buffer Descriptor Entry (BDE)
 */
typedef struct sli4_bde_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	buffer_length:24,
			bde_type:8;
	union {
		struct {
			uint32_t	buffer_address_low;
			uint32_t	buffer_address_high;
		} data;
		struct {
			uint32_t	offset;
			uint32_t	rsvd2;
		} imm;
		struct {
			uint32_t	sgl_segment_address_low;
			uint32_t	sgl_segment_address_high;
		} blp;
	} u;
#else
#error big endian version not defined
#endif
} sli4_bde_t;

#define SLI4_BDE_TYPE_BDE_64		0x00	/** Generic 64-bit data */
#define SLI4_BDE_TYPE_BDE_IMM		0x01	/** Immediate data */
#define SLI4_BDE_TYPE_BLP		0x40	/** Buffer List Pointer */

/**
 * @brief Scatter-Gather Entry (SGE)
 */
typedef struct sli4_sge_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	buffer_address_high;
	uint32_t	buffer_address_low;
	uint32_t	data_offset:27,
			sge_type:4,
			last:1;
	uint32_t	buffer_length;
#else
#error big endian version not defined
#endif
} sli4_sge_t;

/**
 * @brief T10 DIF Scatter-Gather Entry (SGE)
 */
typedef struct sli4_dif_sge_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	buffer_address_high;
	uint32_t	buffer_address_low;
	uint32_t	:27,
			sge_type:4,
			last:1;
	uint32_t	:32;
#else
#error big endian version not defined
#endif
} sli4_dif_sge_t;

/**
 * @brief T10 DIF Seed Scatter-Gather Entry (SGE)
 */
typedef struct sli4_diseed_sge_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	ref_tag_cmp;
	uint32_t	ref_tag_repl;
	uint32_t	app_tag_repl:16,
			:2,
			hs:1,
			ws:1,
			ic:1,
			ics:1,
			atrt:1,
			at:1,
			fwd_app_tag:1,
			repl_app_tag:1,
			head_insert:1,
			sge_type:4,
			last:1;
	uint32_t	app_tag_cmp:16,
			dif_blk_size:3,
			auto_incr_ref_tag:1,
			check_app_tag:1,
			check_ref_tag:1,
			check_crc:1,
			new_ref_tag:1,
			dif_op_rx:4,
			dif_op_tx:4;
#else
#error big endian version not defined
#endif
} sli4_diseed_sge_t;

/**
 * @brief List Segment Pointer Scatter-Gather Entry (SGE)
 */
typedef struct sli4_lsp_sge_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	buffer_address_high;
	uint32_t	buffer_address_low;
	uint32_t	:27,
			sge_type:4,
			last:1;
	uint32_t	segment_length:24,
			:8;
#else
#error big endian version not defined
#endif
} sli4_lsp_sge_t;

#define SLI4_SGE_MAX_RESERVED			3

#define SLI4_SGE_DIF_OP_IN_NODIF_OUT_CRC     0x00
#define SLI4_SGE_DIF_OP_IN_CRC_OUT_NODIF     0x01
#define SLI4_SGE_DIF_OP_IN_NODIF_OUT_CHKSUM  0x02
#define SLI4_SGE_DIF_OP_IN_CHKSUM_OUT_NODIF  0x03
#define SLI4_SGE_DIF_OP_IN_CRC_OUT_CRC       0x04
#define SLI4_SGE_DIF_OP_IN_CHKSUM_OUT_CHKSUM 0x05
#define SLI4_SGE_DIF_OP_IN_CRC_OUT_CHKSUM    0x06
#define SLI4_SGE_DIF_OP_IN_CHKSUM_OUT_CRC    0x07
#define SLI4_SGE_DIF_OP_IN_RAW_OUT_RAW       0x08

#define SLI4_SGE_TYPE_DATA		0x00
#define SLI4_SGE_TYPE_CHAIN		0x03	/** Skyhawk only */
#define SLI4_SGE_TYPE_DIF		0x04	/** Data Integrity Field */
#define SLI4_SGE_TYPE_LSP		0x05	/** List Segment Pointer */
#define SLI4_SGE_TYPE_PEDIF		0x06	/** Post Encryption Engine DIF */
#define SLI4_SGE_TYPE_PESEED		0x07	/** Post Encryption Engine DIF Seed */
#define SLI4_SGE_TYPE_DISEED		0x08	/** DIF Seed */
#define SLI4_SGE_TYPE_ENC		0x09	/** Encryption */
#define SLI4_SGE_TYPE_ATM		0x0a	/** DIF Application Tag Mask */
#define SLI4_SGE_TYPE_SKIP		0x0c	/** SKIP */

#define OCS_MAX_SGE_SIZE		0x80000000 /* Maximum data allowed in a SGE */

/**
 * @brief CONFIG_LINK
 */
typedef struct sli4_cmd_config_link_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	maxbbc:8,	/** Max buffer-to-buffer credit */
			:24;
	uint32_t	alpa:8,
			n_port_id:16,
			:8;
	uint32_t	rsvd3;
	uint32_t	e_d_tov;
	uint32_t	lp_tov;
	uint32_t	r_a_tov;
	uint32_t	r_t_tov;
	uint32_t	al_tov;
	uint32_t	rsvd9;
	uint32_t	:8,
			bbscn:4,	/** buffer-to-buffer state change number */
			cscn:1,		/** configure BBSCN */
			:19;
#else
#error big endian version not defined
#endif
} sli4_cmd_config_link_t;

/**
 * @brief DUMP Type 4
 */
#define SLI4_WKI_TAG_SAT_TEM 0x1040
typedef struct sli4_cmd_dump4_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	type:4,
			:28;
	uint32_t	wki_selection:16,
			:16;
	uint32_t	resv;
	uint32_t	returned_byte_cnt;
	uint32_t	resp_data[59];
#else
#error big endian version not defined
#endif
} sli4_cmd_dump4_t;

/**
 * @brief FW_INITIALIZE - initialize a SLI port
 *
 * @note This command uses a different format than all others.
 */

extern const uint8_t sli4_fw_initialize[8];

/**
 * @brief FW_DEINITIALIZE - deinitialize a SLI port
 *
 * @note This command uses a different format than all others.
 */

extern const uint8_t sli4_fw_deinitialize[8];

/**
 * @brief INIT_LINK - initialize the link for a FC/FCoE port
 */
typedef struct sli4_cmd_init_link_flags_s {
	uint32_t	loopback:1,
			topology:2,
			#define FC_TOPOLOGY_FCAL	0
			#define FC_TOPOLOGY_P2P		1
			:3,
			unfair:1,
			skip_lirp_lilp:1,
			gen_loop_validity_check:1,
			skip_lisa:1,
			enable_topology_failover:1,
			fixed_speed:1,
			:3,
			select_hightest_al_pa:1,
			:16; 	/* pad to 32 bits */
} sli4_cmd_init_link_flags_t;

#define SLI4_INIT_LINK_F_LOOP_BACK	BIT(0)
#define SLI4_INIT_LINK_F_UNFAIR		BIT(6)
#define SLI4_INIT_LINK_F_NO_LIRP	BIT(7)
#define SLI4_INIT_LINK_F_LOOP_VALID_CHK	BIT(8)
#define SLI4_INIT_LINK_F_NO_LISA	BIT(9)
#define SLI4_INIT_LINK_F_FAIL_OVER	BIT(10)
#define SLI4_INIT_LINK_F_NO_AUTOSPEED	BIT(11)
#define SLI4_INIT_LINK_F_PICK_HI_ALPA	BIT(15)

#define SLI4_INIT_LINK_F_P2P_ONLY	1
#define SLI4_INIT_LINK_F_FCAL_ONLY	2

#define SLI4_INIT_LINK_F_FCAL_FAIL_OVER	0
#define SLI4_INIT_LINK_F_P2P_FAIL_OVER	1

typedef struct sli4_cmd_init_link_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	selective_reset_al_pa:8,
			:24;
	sli4_cmd_init_link_flags_t link_flags;
	uint32_t	link_speed_selection_code;
			#define FC_LINK_SPEED_1G		1
			#define FC_LINK_SPEED_2G		2
			#define FC_LINK_SPEED_AUTO_1_2		3
			#define FC_LINK_SPEED_4G		4
			#define FC_LINK_SPEED_AUTO_4_1		5
			#define FC_LINK_SPEED_AUTO_4_2		6
			#define FC_LINK_SPEED_AUTO_4_2_1	7
			#define FC_LINK_SPEED_8G		8
			#define FC_LINK_SPEED_AUTO_8_1		9
			#define FC_LINK_SPEED_AUTO_8_2		10
			#define FC_LINK_SPEED_AUTO_8_2_1	11
			#define FC_LINK_SPEED_AUTO_8_4		12
			#define FC_LINK_SPEED_AUTO_8_4_1	13
			#define FC_LINK_SPEED_AUTO_8_4_2	14
			#define FC_LINK_SPEED_10G		16
			#define FC_LINK_SPEED_16G		17
			#define FC_LINK_SPEED_AUTO_16_8_4	18
			#define FC_LINK_SPEED_AUTO_16_8		19
			#define FC_LINK_SPEED_32G		20
			#define FC_LINK_SPEED_AUTO_32_16_8	21
			#define FC_LINK_SPEED_AUTO_32_16	22
#else
#error big endian version not defined
#endif
} sli4_cmd_init_link_t;

/**
 * @brief INIT_VFI - initialize the VFI resource
 */
typedef struct sli4_cmd_init_vfi_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	vfi:16,
			:12,
			vp:1,
			vf:1,
			vt:1,
			vr:1;
	uint32_t	fcfi:16,
			vpi:16;
	uint32_t	vf_id:13,
			pri:3,
			:16;
	uint32_t	:24,
			hop_count:8;
#else
#error big endian version not defined
#endif
} sli4_cmd_init_vfi_t;

/**
 * @brief INIT_VPI - initialize the VPI resource
 */
typedef struct sli4_cmd_init_vpi_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	vpi:16,
			vfi:16;
#else
#error big endian version not defined
#endif
} sli4_cmd_init_vpi_t;

/**
 * @brief POST_XRI - post XRI resources to the SLI Port
 */
typedef struct sli4_cmd_post_xri_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	xri_base:16,
			xri_count:12,
			enx:1,
			dl:1,
			di:1,
			val:1;
#else
#error big endian version not defined
#endif
} sli4_cmd_post_xri_t;

/**
 * @brief RELEASE_XRI - Release XRI resources from the SLI Port
 */
typedef struct sli4_cmd_release_xri_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	released_xri_count:5,
			:11,
			xri_count:5,
			:11;
	struct {
		uint32_t	xri_tag0:16,
				xri_tag1:16;
	} xri_tbl[62];
#else
#error big endian version not defined
#endif
} sli4_cmd_release_xri_t;

/**
 * @brief READ_CONFIG - read SLI port configuration parameters
 */
typedef struct sli4_cmd_read_config_s {
	sli4_mbox_command_header_t	hdr;
} sli4_cmd_read_config_t;

typedef struct sli4_res_read_config_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:31,
			ext:1;		/** Resource Extents */
	uint32_t	:24,
			topology:8;
	uint32_t	rsvd3;
	uint32_t	e_d_tov:16,
			:16;
	uint32_t	rsvd5;
	uint32_t	r_a_tov:16,
			:16;
	uint32_t	rsvd7;
	uint32_t	rsvd8;
	uint32_t	lmt:16,		/** Link Module Type */
			:16;
	uint32_t	rsvd10;
	uint32_t	rsvd11;
	uint32_t	xri_base:16,
			xri_count:16;
	uint32_t	rpi_base:16,
			rpi_count:16;
	uint32_t	vpi_base:16,
			vpi_count:16;
	uint32_t	vfi_base:16,
			vfi_count:16;
	uint32_t	:16,
			fcfi_count:16;
	uint32_t	rq_count:16,
			eq_count:16;
	uint32_t	wq_count:16,
			cq_count:16;
	uint32_t	pad[45];
#else
#error big endian version not defined
#endif
} sli4_res_read_config_t;

#define SLI4_READ_CFG_TOPO_FCOE			0x0	/** FCoE topology */
#define SLI4_READ_CFG_TOPO_FC			0x1	/** FC topology unknown */
#define SLI4_READ_CFG_TOPO_FC_DA		0x2	/** FC Direct Attach (non FC-AL) topology */
#define SLI4_READ_CFG_TOPO_FC_AL		0x3	/** FC-AL topology */

/**
 * @brief READ_NVPARMS - read SLI port configuration parameters
 */
typedef struct sli4_cmd_read_nvparms_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rsvd1;
	uint32_t	rsvd2;
	uint32_t	rsvd3;
	uint32_t	rsvd4;
	uint8_t		wwpn[8];
	uint8_t		wwnn[8];
	uint32_t	hard_alpa:8,
			preferred_d_id:24;
#else
#error big endian version not defined
#endif
} sli4_cmd_read_nvparms_t;

/**
 * @brief WRITE_NVPARMS - write SLI port configuration parameters
 */
typedef struct sli4_cmd_write_nvparms_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rsvd1;
	uint32_t	rsvd2;
	uint32_t	rsvd3;
	uint32_t	rsvd4;
	uint8_t		wwpn[8];
	uint8_t		wwnn[8];
	uint32_t	hard_alpa:8,
			preferred_d_id:24;
#else
#error big endian version not defined
#endif
} sli4_cmd_write_nvparms_t;

/**
 * @brief READ_REV - read the Port revision levels
 */
typedef struct sli4_cmd_read_rev_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:16,
			sli_level:4,
			fcoem:1,
			ceev:2,
			:6,
			vpd:1,
			:2;
	uint32_t	first_hw_revision;
	uint32_t	second_hw_revision;
	uint32_t	rsvd4;
	uint32_t	third_hw_revision;
	uint32_t	fc_ph_low:8,
			fc_ph_high:8,
			feature_level_low:8,
			feature_level_high:8;
	uint32_t	rsvd7;
	uint32_t	first_fw_id;
	char		first_fw_name[16];
	uint32_t	second_fw_id;
	char		second_fw_name[16];
	uint32_t	rsvd18[30];
	uint32_t	available_length:24,
			:8;
	uint32_t	physical_address_low;
	uint32_t	physical_address_high;
	uint32_t	returned_vpd_length;
	uint32_t	actual_vpd_length;
#else
#error big endian version not defined
#endif
} sli4_cmd_read_rev_t;

/**
 * @brief READ_SPARM64 - read the Port service parameters
 */
typedef struct sli4_cmd_read_sparm64_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rsvd1;
	uint32_t	rsvd2;
	sli4_bde_t	bde_64;
	uint32_t	vpi:16,
			:16;
	uint32_t	port_name_start:16,
			port_name_length:16;
	uint32_t	node_name_start:16,
			node_name_length:16;
#else
#error big endian version not defined
#endif
} sli4_cmd_read_sparm64_t;

#define SLI4_READ_SPARM64_VPI_DEFAULT	0
#define SLI4_READ_SPARM64_VPI_SPECIAL	UINT16_MAX

#define SLI4_READ_SPARM64_WWPN_OFFSET	(4 * sizeof(uint32_t))
#define SLI4_READ_SPARM64_WWNN_OFFSET	(SLI4_READ_SPARM64_WWPN_OFFSET + sizeof(uint64_t))

typedef struct sli4_port_state_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	nx_port_recv_state:2,
			nx_port_trans_state:2,
			nx_port_state_machine:4,
			link_speed:8,
			:14,
			tf:1,
			lu:1;
#else
#error big endian version not defined
#endif
} sli4_port_state_t;

/**
 * @brief READ_TOPOLOGY - read the link event information
 */
typedef struct sli4_cmd_read_topology_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	event_tag;
	uint32_t	attention_type:8,
			il:1,
			pb_recvd:1,
			:22;
	uint32_t	topology:8,
			lip_type:8,
			lip_al_ps:8,
			al_pa_granted:8;
	sli4_bde_t	bde_loop_map;
	sli4_port_state_t link_down;
	sli4_port_state_t link_current;
	uint32_t	max_bbc:8,
			init_bbc:8,
			bbscn:4,
			cbbscn:4,
			:8;
	uint32_t	r_t_tov:9,
			:3,
			al_tov:4,
			lp_tov:16;
	uint32_t	acquired_al_pa:8,
			:7,
			pb:1,
			specified_al_pa:16;
	uint32_t	initial_n_port_id:24,
			:8;
#else
#error big endian version not defined
#endif
} sli4_cmd_read_topology_t;

#define SLI4_MIN_LOOP_MAP_BYTES	128

#define SLI4_READ_TOPOLOGY_LINK_UP	0x1
#define SLI4_READ_TOPOLOGY_LINK_DOWN	0x2
#define SLI4_READ_TOPOLOGY_LINK_NO_ALPA	0x3

#define SLI4_READ_TOPOLOGY_UNKNOWN	0x0
#define SLI4_READ_TOPOLOGY_NPORT	0x1
#define SLI4_READ_TOPOLOGY_FC_AL	0x2

#define SLI4_READ_TOPOLOGY_SPEED_NONE	0x00
#define SLI4_READ_TOPOLOGY_SPEED_1G	0x04
#define SLI4_READ_TOPOLOGY_SPEED_2G	0x08
#define SLI4_READ_TOPOLOGY_SPEED_4G	0x10
#define SLI4_READ_TOPOLOGY_SPEED_8G	0x20
#define SLI4_READ_TOPOLOGY_SPEED_10G	0x40
#define SLI4_READ_TOPOLOGY_SPEED_16G	0x80
#define SLI4_READ_TOPOLOGY_SPEED_32G	0x90

/**
 * @brief REG_FCFI - activate a FC Forwarder
 */
#define SLI4_CMD_REG_FCFI_NUM_RQ_CFG	4
typedef struct sli4_cmd_reg_fcfi_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	fcf_index:16,
			fcfi:16;
	uint32_t	rq_id_1:16,
			rq_id_0:16;
	uint32_t	rq_id_3:16,
			rq_id_2:16;
	struct {
		uint32_t	r_ctl_mask:8,
				r_ctl_match:8,
				type_mask:8,
				type_match:8;
	} rq_cfg[SLI4_CMD_REG_FCFI_NUM_RQ_CFG];
	uint32_t	vlan_tag:12,
			vv:1,
			:19;
#else
#error big endian version not defined
#endif
} sli4_cmd_reg_fcfi_t;

#define SLI4_CMD_REG_FCFI_MRQ_NUM_RQ_CFG	4
#define SLI4_CMD_REG_FCFI_MRQ_MAX_NUM_RQ	32
#define SLI4_CMD_REG_FCFI_SET_FCFI_MODE		0
#define SLI4_CMD_REG_FCFI_SET_MRQ_MODE		1

typedef struct sli4_cmd_reg_fcfi_mrq_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	fcf_index:16,
			fcfi:16;

	uint32_t	rq_id_1:16,
			rq_id_0:16;

	uint32_t	rq_id_3:16,
			rq_id_2:16;

	struct {
		uint32_t	r_ctl_mask:8,
				r_ctl_match:8,
				type_mask:8,
				type_match:8;
	} rq_cfg[SLI4_CMD_REG_FCFI_MRQ_NUM_RQ_CFG];

	uint32_t	vlan_tag:12,
			vv:1,
			mode:1,
			:18;

	uint32_t	num_mrq_pairs:8,
			mrq_filter_bitmask:4,
			rq_selection_policy:4,
			:16;
#endif
} sli4_cmd_reg_fcfi_mrq_t;

/**
 * @brief REG_RPI - register a Remote Port Indicator
 */
typedef struct sli4_cmd_reg_rpi_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rpi:16,
			:16;
	uint32_t	remote_n_port_id:24,
			upd:1,
			:2,
			etow:1,
			:1,
			terp:1,
			:1,
			ci:1;
	sli4_bde_t	bde_64;
	uint32_t	vpi:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_cmd_reg_rpi_t;
#define SLI4_REG_RPI_BUF_LEN			0x70


/**
 * @brief REG_VFI - register a Virtual Fabric Indicator
 */
typedef struct sli4_cmd_reg_vfi_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	vfi:16,
			:12,
			vp:1,
			upd:1,
			:2;
	uint32_t	fcfi:16,
			vpi:16;			/* vp=TRUE */
	uint8_t		wwpn[8];		/* vp=TRUE */
	sli4_bde_t	sparm;			/* either FLOGI or PLOGI */
	uint32_t	e_d_tov;
	uint32_t	r_a_tov;
	uint32_t	local_n_port_id:24,	/* vp=TRUE */
			:8;
#else
#error big endian version not defined
#endif
} sli4_cmd_reg_vfi_t;

/**
 * @brief REG_VPI - register a Virtual Port Indicator
 */
typedef struct sli4_cmd_reg_vpi_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rsvd1;
	uint32_t	local_n_port_id:24,
			upd:1,
			:7;
	uint8_t		wwpn[8];
	uint32_t	rsvd5;
	uint32_t	vpi:16,
			vfi:16;
#else
#error big endian version not defined
#endif
} sli4_cmd_reg_vpi_t;

/**
 * @brief REQUEST_FEATURES - request / query SLI features
 */
typedef union {
#if BYTE_ORDER == LITTLE_ENDIAN
	struct {
		uint32_t	iaab:1,		/** inhibit auto-ABTS originator */
				npiv:1,		/** NPIV support */
				dif:1,		/** DIF/DIX support */
				vf:1,		/** virtual fabric support */
				fcpi:1,		/** FCP initiator support */
				fcpt:1,		/** FCP target support */
				fcpc:1,		/** combined FCP initiator/target */
				:1,
				rqd:1,		/** recovery qualified delay */
				iaar:1,		/** inhibit auto-ABTS responder */
				hlm:1,		/** High Login Mode */
				perfh:1,	/** performance hints */
				rxseq:1,	/** RX Sequence Coalescing */
				rxri:1,		/** Release XRI variant of Coalescing */
				dcl2:1,		/** Disable Class 2 */
				rsco:1,		/** Receive Sequence Coalescing Optimizations */
				mrqp:1,		/** Multi RQ Pair Mode Support */
				:15;
	} flag;
	uint32_t	dword;
#else
#error big endian version not defined
#endif
} sli4_features_t;

typedef struct sli4_cmd_request_features_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	qry:1,
			:31;
#else
#error big endian version not defined
#endif
	sli4_features_t	command;
	sli4_features_t	response;
} sli4_cmd_request_features_t;

/**
 * @brief SLI_CONFIG - submit a configuration command to Port
 *
 * Command is either embedded as part of the payload (embed) or located
 * in a separate memory buffer (mem)
 */


typedef struct sli4_sli_config_pmd_s {
	uint32_t	address_low;
	uint32_t	address_high;
	uint32_t	length:24,
			:8;
} sli4_sli_config_pmd_t;

typedef struct sli4_cmd_sli_config_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	emb:1,
			:2,
			pmd_count:5,
			:24;
	uint32_t	payload_length;
	uint32_t	rsvd3;
	uint32_t	rsvd4;
	uint32_t	rsvd5;
	union {
		uint8_t			embed[58 * sizeof(uint32_t)];
		sli4_sli_config_pmd_t   mem;
	} payload;
#else
#error big endian version not defined
#endif
} sli4_cmd_sli_config_t;

/**
 * @brief READ_STATUS - read tx/rx status of a particular port
 *
 */

typedef struct sli4_cmd_read_status_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	cc:1,
			:31;
	uint32_t	rsvd2;
	uint32_t	transmit_kbyte_count;
	uint32_t	receive_kbyte_count;
	uint32_t	transmit_frame_count;
	uint32_t	receive_frame_count;
	uint32_t	transmit_sequence_count;
	uint32_t	receive_sequence_count;
	uint32_t	total_exchanges_originator;
	uint32_t	total_exchanges_responder;
	uint32_t	receive_p_bsy_count;
	uint32_t	receive_f_bsy_count;
	uint32_t	dropped_frames_due_to_no_rq_buffer_count;
	uint32_t	empty_rq_timeout_count;
	uint32_t	dropped_frames_due_to_no_xri_count;
	uint32_t	empty_xri_pool_count;

#else
#error big endian version not defined
#endif
} sli4_cmd_read_status_t;

/**
 * @brief READ_LNK_STAT - read link status of a particular port
 *
 */

typedef struct sli4_cmd_read_link_stats_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rec:1,
			gec:1,
			w02of:1,
			w03of:1,
			w04of:1,
			w05of:1,
			w06of:1,
			w07of:1,
			w08of:1,
			w09of:1,
			w10of:1,
			w11of:1,
			w12of:1,
			w13of:1,
			w14of:1,
			w15of:1,
			w16of:1,
			w17of:1,
			w18of:1,
			w19of:1,
			w20of:1,
			w21of:1,
			resv0:8,
			clrc:1,
			clof:1;
	uint32_t	link_failure_error_count;
	uint32_t	loss_of_sync_error_count;
	uint32_t	loss_of_signal_error_count;
	uint32_t	primitive_sequence_error_count;
	uint32_t	invalid_transmission_word_error_count;
	uint32_t	crc_error_count;
	uint32_t	primitive_sequence_event_timeout_count;
	uint32_t	elastic_buffer_overrun_error_count;
	uint32_t	arbitration_fc_al_timout_count;
	uint32_t	advertised_receive_bufftor_to_buffer_credit;
	uint32_t	current_receive_buffer_to_buffer_credit;
	uint32_t	advertised_transmit_buffer_to_buffer_credit;
	uint32_t	current_transmit_buffer_to_buffer_credit;
	uint32_t	received_eofa_count;
	uint32_t	received_eofdti_count;
	uint32_t	received_eofni_count;
	uint32_t	received_soff_count;
	uint32_t	received_dropped_no_aer_count;
	uint32_t	received_dropped_no_available_rpi_resources_count;
	uint32_t	received_dropped_no_available_xri_resources_count;

#else
#error big endian version not defined
#endif
} sli4_cmd_read_link_stats_t;

/**
 * @brief Format a WQE with WQ_ID Association performance hint
 *
 * @par Description
 * PHWQ works by over-writing part of Word 10 in the WQE with the WQ ID.
 *
 * @param entry Pointer to the WQE.
 * @param q_id Queue ID.
 *
 * @return None.
 */
static inline void
sli_set_wq_id_association(void *entry, uint16_t q_id)
{
	uint32_t *wqe = entry;

	/*
	 * Set Word 10, bit 0 to zero
	 * Set Word 10, bits 15:1 to the WQ ID
	 */
#if BYTE_ORDER == LITTLE_ENDIAN
	wqe[10] &= ~0xffff;
	wqe[10] |= q_id << 1;
#else
#error big endian version not defined
#endif
}

/**
 * @brief UNREG_FCFI - unregister a FCFI
 */
typedef struct sli4_cmd_unreg_fcfi_s {
	sli4_mbox_command_header_t	hdr;
	uint32_t	rsvd1;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	fcfi:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_cmd_unreg_fcfi_t;

/**
 * @brief UNREG_RPI - unregister one or more RPI
 */
typedef struct sli4_cmd_unreg_rpi_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	index:16,
			:13,
			dp:1,
			ii:2;
	uint32_t	destination_n_port_id:24,
			:8;
#else
#error big endian version not defined
#endif
} sli4_cmd_unreg_rpi_t;

#define SLI4_UNREG_RPI_II_RPI			0x0
#define SLI4_UNREG_RPI_II_VPI			0x1
#define SLI4_UNREG_RPI_II_VFI			0x2
#define SLI4_UNREG_RPI_II_FCFI			0x3

/**
 * @brief UNREG_VFI - unregister one or more VFI
 */
typedef struct sli4_cmd_unreg_vfi_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rsvd1;
	uint32_t	index:16,
			:14,
			ii:2;
#else
#error big endian version not defined
#endif
} sli4_cmd_unreg_vfi_t;

#define SLI4_UNREG_VFI_II_VFI			0x0
#define SLI4_UNREG_VFI_II_FCFI			0x3

enum {
	SLI4_UNREG_TYPE_PORT,
	SLI4_UNREG_TYPE_DOMAIN,
	SLI4_UNREG_TYPE_FCF,
	SLI4_UNREG_TYPE_ALL
};

/**
 * @brief UNREG_VPI - unregister one or more VPI
 */
typedef struct sli4_cmd_unreg_vpi_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rsvd1;
	uint32_t	index:16,
			:14,
			ii:2;
#else
#error big endian version not defined
#endif
} sli4_cmd_unreg_vpi_t;

#define SLI4_UNREG_VPI_II_VPI			0x0
#define SLI4_UNREG_VPI_II_VFI			0x2
#define SLI4_UNREG_VPI_II_FCFI			0x3


/**
 * @brief AUTO_XFER_RDY - Configure the auto-generate XFER-RDY feature.
 */
typedef struct sli4_cmd_config_auto_xfer_rdy_s {
	sli4_mbox_command_header_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	resv;
	uint32_t	max_burst_len;
#else
#error big endian version not defined
#endif
} sli4_cmd_config_auto_xfer_rdy_t;

typedef struct sli4_cmd_config_auto_xfer_rdy_hp_s {
        sli4_mbox_command_header_t      hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
        uint32_t        resv;
        uint32_t        max_burst_len;
        uint32_t        esoc:1,
                        :31;
        uint32_t        block_size:16,
                        :16;
#else
#error big endian version not defined
#endif
} sli4_cmd_config_auto_xfer_rdy_hp_t;


/*************************************************************************
 * SLI-4 common configuration command formats and definitions
 */

#define SLI4_CFG_STATUS_SUCCESS			0x00
#define SLI4_CFG_STATUS_FAILED			0x01
#define SLI4_CFG_STATUS_ILLEGAL_REQUEST		0x02
#define SLI4_CFG_STATUS_ILLEGAL_FIELD		0x03

#define SLI4_MGMT_STATUS_FLASHROM_READ_FAILED	0xcb

#define SLI4_CFG_ADD_STATUS_NO_STATUS		0x00
#define SLI4_CFG_ADD_STATUS_INVALID_OPCODE	0x1e

/**
 * Subsystem values.
 */
#define SLI4_SUBSYSTEM_COMMON			0x01
#define SLI4_SUBSYSTEM_LOWLEVEL			0x0B
#define SLI4_SUBSYSTEM_FCFCOE			0x0c
#define SLI4_SUBSYSTEM_DMTF			0x11

#define	SLI4_OPC_LOWLEVEL_SET_WATCHDOG		0X36

/**
 * Common opcode (OPC) values.
 */
#define SLI4_OPC_COMMON_FUNCTION_RESET			0x3d
#define SLI4_OPC_COMMON_CREATE_CQ			0x0c
#define SLI4_OPC_COMMON_CREATE_CQ_SET			0x1d
#define SLI4_OPC_COMMON_DESTROY_CQ			0x36
#define SLI4_OPC_COMMON_MODIFY_EQ_DELAY			0x29
#define SLI4_OPC_COMMON_CREATE_EQ			0x0d
#define SLI4_OPC_COMMON_DESTROY_EQ			0x37
#define SLI4_OPC_COMMON_CREATE_MQ_EXT			0x5a
#define SLI4_OPC_COMMON_DESTROY_MQ			0x35
#define SLI4_OPC_COMMON_GET_CNTL_ATTRIBUTES		0x20
#define SLI4_OPC_COMMON_NOP				0x21
#define SLI4_OPC_COMMON_GET_RESOURCE_EXTENT_INFO	0x9a
#define SLI4_OPC_COMMON_GET_SLI4_PARAMETERS		0xb5
#define SLI4_OPC_COMMON_QUERY_FW_CONFIG			0x3a
#define SLI4_OPC_COMMON_GET_PORT_NAME			0x4d

#define SLI4_OPC_COMMON_WRITE_FLASHROM			0x07
#define SLI4_OPC_COMMON_MANAGE_FAT			0x44
#define SLI4_OPC_COMMON_READ_TRANSCEIVER_DATA		0x49
#define SLI4_OPC_COMMON_GET_CNTL_ADDL_ATTRIBUTES	0x79
#define SLI4_OPC_COMMON_GET_EXT_FAT_CAPABILITIES	0x7d
#define SLI4_OPC_COMMON_SET_EXT_FAT_CAPABILITIES	0x7e
#define SLI4_OPC_COMMON_EXT_FAT_CONFIGURE_SNAPSHOT	0x7f
#define SLI4_OPC_COMMON_EXT_FAT_RETRIEVE_SNAPSHOT	0x80
#define SLI4_OPC_COMMON_EXT_FAT_READ_STRING_TABLE	0x82
#define SLI4_OPC_COMMON_GET_FUNCTION_CONFIG		0xa0
#define SLI4_OPC_COMMON_GET_PROFILE_CONFIG		0xa4
#define SLI4_OPC_COMMON_SET_PROFILE_CONFIG		0xa5
#define SLI4_OPC_COMMON_GET_PROFILE_LIST		0xa6
#define SLI4_OPC_COMMON_GET_ACTIVE_PROFILE		0xa7
#define SLI4_OPC_COMMON_SET_ACTIVE_PROFILE		0xa8
#define SLI4_OPC_COMMON_READ_OBJECT			0xab
#define SLI4_OPC_COMMON_WRITE_OBJECT			0xac
#define SLI4_OPC_COMMON_DELETE_OBJECT			0xae
#define SLI4_OPC_COMMON_READ_OBJECT_LIST		0xad
#define SLI4_OPC_COMMON_SET_DUMP_LOCATION		0xb8
#define SLI4_OPC_COMMON_SET_FEATURES			0xbf
#define SLI4_OPC_COMMON_GET_RECONFIG_LINK_INFO		0xc9
#define SLI4_OPC_COMMON_SET_RECONFIG_LINK_ID		0xca

/**
 * DMTF opcode (OPC) values.
 */
#define SLI4_OPC_DMTF_EXEC_CLP_CMD			0x01

/**
 * @brief Generic Command Request header
 */
typedef struct sli4_req_hdr_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	opcode:8,
			subsystem:8,
			:16;
	uint32_t	timeout;
	uint32_t	request_length;
	uint32_t	version:8,
			:24;
#else
#error big endian version not defined
#endif
} sli4_req_hdr_t;

/**
 * @brief Generic Command Response header
 */
typedef struct sli4_res_hdr_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	opcode:8,
			subsystem:8,
			:16;
	uint32_t	status:8,
			additional_status:8,
			:16;
	uint32_t	response_length;
	uint32_t	actual_response_length;
#else
#error big endian version not defined
#endif
} sli4_res_hdr_t;

/**
 * @brief COMMON_FUNCTION_RESET
 *
 * Resets the Port, returning it to a power-on state. This configuration
 * command does not have a payload and should set/expect the lengths to
 * be zero.
 */
typedef struct sli4_req_common_function_reset_s {
	sli4_req_hdr_t	hdr;
} sli4_req_common_function_reset_t;


typedef struct sli4_res_common_function_reset_s {
	sli4_res_hdr_t	hdr;
} sli4_res_common_function_reset_t;

/**
 * @brief COMMON_CREATE_CQ_V0
 *
 * Create a Completion Queue.
 */
typedef struct sli4_req_common_create_cq_v0_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:16,
			:16;
	uint32_t	:12,
			clswm:2,
			nodelay:1,
			:12,
			cqecnt:2,
			valid:1,
			:1,
			evt:1;
	uint32_t	:22,
			eq_id:8,
			:1,
			arm:1;
	uint32_t	rsvd[2];
	struct {
		uint32_t	low;
		uint32_t	high;
	} page_physical_address[0];
#else
#error big endian version not defined
#endif
} sli4_req_common_create_cq_v0_t;

/**
 * @brief COMMON_CREATE_CQ_V2
 *
 * Create a Completion Queue.
 */
typedef struct sli4_req_common_create_cq_v2_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:16,
			page_size:8,
			:8,
	uint32_t	:12,
			clswm:2,
			nodelay:1,
			autovalid:1,
			:11,
			cqecnt:2,
			valid:1,
			:1,
			evt:1;
	uint32_t	eq_id:16,
			:15,
			arm:1;
	uint32_t	cqe_count:16,
			:16;
	uint32_t	rsvd[1];
	struct {
		uint32_t	low;
		uint32_t	high;
	} page_physical_address[0];
#else
#error big endian version not defined
#endif
} sli4_req_common_create_cq_v2_t;



/**
 * @brief COMMON_CREATE_CQ_SET_V0
 *
 * Create a set of Completion Queues.
 */
typedef struct sli4_req_common_create_cq_set_v0_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:16,
			page_size:8,
			:8;
	uint32_t	:12,
			clswm:2,
			nodelay:1,
			autovalid:1,
			rsvd:11,
			cqecnt:2,
			valid:1,
			:1,
			evt:1;
	uint32_t	num_cq_req:16,
			cqe_count:15,
			arm:1;
	uint16_t	eq_id[16];
	struct {
		uint32_t	low;
		uint32_t	high;
	} page_physical_address[0];
#else
#error big endian version not defined
#endif
} sli4_req_common_create_cq_set_v0_t;

/**
 * CQE count.
 */
#define SLI4_CQ_CNT_256			0
#define SLI4_CQ_CNT_512			1
#define SLI4_CQ_CNT_1024		2
#define SLI4_CQ_CNT_LARGE		3

#define SLI4_CQE_BYTES			(4 * sizeof(uint32_t))

#define SLI4_COMMON_CREATE_CQ_V2_MAX_PAGES 8

/**
 * @brief Generic Common Create EQ/CQ/MQ/WQ/RQ Queue completion
 */
typedef struct sli4_res_common_create_queue_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t q_id:16,
		:8,
		ulp:8;
	uint32_t db_offset;
	uint32_t db_rs:16,
		 db_fmt:16;
#else
#error big endian version not defined
#endif
} sli4_res_common_create_queue_t;


typedef struct sli4_res_common_create_queue_set_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t q_id:16,
		num_q_allocated:16;
#else
#error big endian version not defined
#endif
} sli4_res_common_create_queue_set_t;


/**
 * @brief Common Destroy CQ
 */
typedef struct sli4_req_common_destroy_cq_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	cq_id:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_req_common_destroy_cq_t;

/**
 * @brief COMMON_MODIFY_EQ_DELAY
 *
 * Modify the delay multiplier for EQs
 */
typedef struct sli4_req_common_modify_eq_delay_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_eq;
	struct {
		uint32_t	eq_id;
		uint32_t	phase;
		uint32_t	delay_multiplier;
	} eq_delay_record[8];
#else
#error big endian version not defined
#endif
} sli4_req_common_modify_eq_delay_t;

/**
 * @brief COMMON_CREATE_EQ
 *
 * Create an Event Queue.
 */
typedef struct sli4_req_common_create_eq_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:16,
			:16;
	uint32_t	:29,
			valid:1,
			:1,
			eqesz:1;
	uint32_t	:26,
			count:3,
			:2,
			arm:1;
	uint32_t	:13,
			delay_multiplier:10,
			:9;
	uint32_t	rsvd;
	struct {
		uint32_t	low;
		uint32_t	high;
	} page_address[8];
#else
#error big endian version not defined
#endif
} sli4_req_common_create_eq_t;

#define SLI4_EQ_CNT_256			0
#define SLI4_EQ_CNT_512			1
#define SLI4_EQ_CNT_1024		2
#define SLI4_EQ_CNT_2048		3
#define SLI4_EQ_CNT_4096		4

#define SLI4_EQE_SIZE_4			0
#define SLI4_EQE_SIZE_16		1

/**
 * @brief Common Destroy EQ
 */
typedef struct sli4_req_common_destroy_eq_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	eq_id:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_req_common_destroy_eq_t;

/**
 * @brief COMMON_CREATE_MQ_EXT
 *
 * Create a Mailbox Queue; accommodate v0 and v1 forms.
 */
typedef struct sli4_req_common_create_mq_ext_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:16,
			cq_id_v1:16;
	uint32_t	async_event_bitmap;
	uint32_t	async_cq_id_v1:16,
			ring_size:4,
			:2,
			cq_id_v0:10;
	uint32_t	:31,
			val:1;
	uint32_t	acqv:1,
			async_cq_id_v0:10,
			:21;
	uint32_t	rsvd9;
	struct {
		uint32_t	low;
		uint32_t	high;
	} page_physical_address[8];
#else
#error big endian version not defined
#endif
} sli4_req_common_create_mq_ext_t;

#define SLI4_MQE_SIZE_16		0x05
#define SLI4_MQE_SIZE_32		0x06
#define SLI4_MQE_SIZE_64		0x07
#define SLI4_MQE_SIZE_128		0x08

#define SLI4_ASYNC_EVT_LINK_STATE	BIT(1)
#define SLI4_ASYNC_EVT_FCOE_FIP		BIT(2)
#define SLI4_ASYNC_EVT_DCBX		BIT(3)
#define SLI4_ASYNC_EVT_ISCSI		BIT(4)
#define SLI4_ASYNC_EVT_GRP5		BIT(5)
#define SLI4_ASYNC_EVT_FC		BIT(16)
#define SLI4_ASYNC_EVT_SLI_PORT		BIT(17)
#define SLI4_ASYNC_EVT_VF		BIT(18)
#define SLI4_ASYNC_EVT_MR		BIT(19)

#define SLI4_ASYNC_EVT_ALL	\
		SLI4_ASYNC_EVT_LINK_STATE 	| \
		SLI4_ASYNC_EVT_FCOE_FIP		| \
		SLI4_ASYNC_EVT_DCBX		| \
		SLI4_ASYNC_EVT_ISCSI		| \
		SLI4_ASYNC_EVT_GRP5		| \
		SLI4_ASYNC_EVT_FC		| \
		SLI4_ASYNC_EVT_SLI_PORT		| \
		SLI4_ASYNC_EVT_VF		|\
		SLI4_ASYNC_EVT_MR

#define SLI4_ASYNC_EVT_FC_FCOE \
		SLI4_ASYNC_EVT_LINK_STATE	| \
		SLI4_ASYNC_EVT_FCOE_FIP		| \
		SLI4_ASYNC_EVT_GRP5		| \
		SLI4_ASYNC_EVT_FC		| \
		SLI4_ASYNC_EVT_SLI_PORT

/**
 * @brief Common Destroy MQ
 */
typedef struct sli4_req_common_destroy_mq_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	mq_id:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_req_common_destroy_mq_t;

/**
 * @brief COMMON_GET_CNTL_ATTRIBUTES
 *
 * Query for information about the SLI Port
 */
typedef struct sli4_res_common_get_cntl_attributes_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint8_t		version_string[32];
	uint8_t		manufacturer_name[32];
	uint32_t	supported_modes;
	uint32_t	eprom_version_lo:8,
			eprom_version_hi:8,
			:16;
	uint32_t	mbx_data_structure_version;
	uint32_t	ep_firmware_data_structure_version;
	uint8_t		ncsi_version_string[12];
	uint32_t	default_extended_timeout;
	uint8_t		model_number[32];
	uint8_t		description[64];
	uint8_t		serial_number[32];
	uint8_t		ip_version_string[32];
	uint8_t		fw_version_string[32];
	uint8_t		bios_version_string[32];
	uint8_t		redboot_version_string[32];
	uint8_t		driver_version_string[32];
	uint8_t		fw_on_flash_version_string[32];
	uint32_t	functionalities_supported;
	uint32_t	max_cdb_length:16,
			asic_revision:8,
			generational_guid0:8;
	uint32_t	generational_guid1_12[3];
	uint32_t	generational_guid13:24,
			hba_port_count:8;
	uint32_t	default_link_down_timeout:16,
			iscsi_version_min_max:8,
			multifunctional_device:8;
	uint32_t	cache_valid:8,
			hba_status:8,
			max_domains_supported:8,
			port_number:6,
			port_type:2;
	uint32_t	firmware_post_status;
	uint32_t	hba_mtu;
	uint32_t	iscsi_features:8,
			rsvd121:24;
	uint32_t	pci_vendor_id:16,
			pci_device_id:16;
	uint32_t	pci_sub_vendor_id:16,
			pci_sub_system_id:16;
	uint32_t	pci_bus_number:8,
			pci_device_number:8,
			pci_function_number:8,
			interface_type:8;
	uint64_t	unique_identifier;
	uint32_t	number_of_netfilters:8,
			rsvd130:24;
#else
#error big endian version not defined
#endif
} sli4_res_common_get_cntl_attributes_t;

/**
 * @brief COMMON_GET_CNTL_ATTRIBUTES
 *
 * This command queries the controller information from the Flash ROM.
 */
typedef struct sli4_req_common_get_cntl_addl_attributes_s {
	sli4_req_hdr_t	hdr;
} sli4_req_common_get_cntl_addl_attributes_t;


typedef struct sli4_res_common_get_cntl_addl_attributes_s {
	sli4_res_hdr_t	hdr;
	uint16_t	ipl_file_number;
	uint8_t		ipl_file_version;
	uint8_t		rsvd0;
	uint8_t		on_die_temperature;
	uint8_t		rsvd1[3];
	uint32_t	driver_advanced_features_supported;
	uint32_t	rsvd2[4];
	char		fcoe_universal_bios_version[32];
	char		fcoe_x86_bios_version[32];
	char		fcoe_efi_bios_version[32];
	char		fcoe_fcode_version[32];
	char		uefi_bios_version[32];
	char		uefi_nic_version[32];
	char		uefi_fcode_version[32];
	char		uefi_iscsi_version[32];
	char		iscsi_x86_bios_version[32];
	char		pxe_x86_bios_version[32];
	uint8_t		fcoe_default_wwpn[8];
	uint8_t		ext_phy_version[32];
	uint8_t		fc_universal_bios_version[32];
	uint8_t		fc_x86_bios_version[32];
	uint8_t		fc_efi_bios_version[32];
	uint8_t		fc_fcode_version[32];
	uint8_t		ext_phy_crc_label[8];
	uint8_t		ipl_file_name[16];
	uint8_t		rsvd3[72];
} sli4_res_common_get_cntl_addl_attributes_t;

/**
 * @brief COMMON_NOP
 *
 * This command does not do anything; it only returns the payload in the completion.
 */
typedef struct sli4_req_common_nop_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	context[2];
#else
#error big endian version not defined
#endif
} sli4_req_common_nop_t;

typedef struct sli4_res_common_nop_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	context[2];
#else
#error big endian version not defined
#endif
} sli4_res_common_nop_t;

/**
 * @brief COMMON_GET_RESOURCE_EXTENT_INFO
 */
typedef struct sli4_req_common_get_resource_extent_info_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	resource_type:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_req_common_get_resource_extent_info_t;

#define SLI4_RSC_TYPE_ISCSI_INI_XRI	0x0c
#define SLI4_RSC_TYPE_FCOE_VFI		0x20
#define SLI4_RSC_TYPE_FCOE_VPI		0x21
#define SLI4_RSC_TYPE_FCOE_RPI		0x22
#define SLI4_RSC_TYPE_FCOE_XRI		0x23

typedef struct sli4_res_common_get_resource_extent_info_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	resource_extent_count:16,
			resource_extent_size:16;
#else
#error big endian version not defined
#endif
} sli4_res_common_get_resource_extent_info_t;


#define SLI4_128BYTE_WQE_SUPPORT	0x02
/**
 * @brief COMMON_GET_SLI4_PARAMETERS
 */
typedef struct sli4_res_common_get_sli4_parameters_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	protocol_type:8,
			:24;
	uint32_t	ft:1,
			:3,
			sli_revision:4,
			sli_family:4,
			if_type:4,
			sli_hint_1:8,
			sli_hint_2:5,
			:3;
	uint32_t	eq_page_cnt:4,
			:4,
			eqe_sizes:4,
			:4,
			eq_page_sizes:8,
			eqe_count_method:4,
			:4;
	uint32_t	eqe_count_mask:16,
			:16;
	uint32_t	cq_page_cnt:4,
			:4,
			cqe_sizes:4,
			:2,
			cqv:2,
			cq_page_sizes:8,
			cqe_count_method:4,
			:4;
	uint32_t	cqe_count_mask:16,
			:16;
	uint32_t	mq_page_cnt:4,
			:10,
			mqv:2,
			mq_page_sizes:8,
			mqe_count_method:4,
			:4;
	uint32_t	mqe_count_mask:16,
			:16;
	uint32_t	wq_page_cnt:4,
			:4,
			wqe_sizes:4,
			:2,
			wqv:2,
			wq_page_sizes:8,
			wqe_count_method:4,
			:4;
	uint32_t	wqe_count_mask:16,
			:16;
	uint32_t	rq_page_cnt:4,
			:4,
			rqe_sizes:4,
			:2,
			rqv:2,
			rq_page_sizes:8,
			rqe_count_method:4,
			:4;
	uint32_t	rqe_count_mask:16,
			:12,
			rq_db_window:4;
	uint32_t	fcoe:1,
			ext:1,
			hdrr:1,
			sglr:1,
			fbrr:1,
			areg:1,
			tgt:1,
			terp:1,
			assi:1,
			wchn:1,
			tcca:1,
			trty:1,
			trir:1,
			phoff:1,
			phon:1,
			phwq:1,			/** Performance Hint WQ_ID Association */
			boundary_4ga:1,
			rxc:1,
			hlm:1,
			ipr:1,
			rxri:1,
			sglc:1,
			timm:1,
			tsmm:1,
			:1,
			oas:1,
			lc:1,
			agxf:1,
			loopback_scope:4;
	uint32_t	sge_supported_length;
	uint32_t	sgl_page_cnt:4,
			:4,
			sgl_page_sizes:8,
			sgl_pp_align:8,
			:8;
	uint32_t	min_rq_buffer_size:16,
			:16;
	uint32_t	max_rq_buffer_size;
	uint32_t	physical_xri_max:16,
			physical_rpi_max:16;
	uint32_t	physical_vpi_max:16,
			physical_vfi_max:16;
	uint32_t	rsvd19;
	uint32_t	frag_num_field_offset:16,	/* dword 20 */
			frag_num_field_size:16;
	uint32_t	sgl_index_field_offset:16,	/* dword 21 */
			sgl_index_field_size:16;
	uint32_t	chain_sge_initial_value_lo;	/* dword 22 */
	uint32_t	chain_sge_initial_value_hi;	/* dword 23 */
#else
#error big endian version not defined
#endif
} sli4_res_common_get_sli4_parameters_t;


/**
 * @brief COMMON_QUERY_FW_CONFIG
 *
 * This command retrieves firmware configuration parameters and adapter
 * resources available to the driver.
 */
typedef struct sli4_req_common_query_fw_config_s {
	sli4_req_hdr_t	hdr;
} sli4_req_common_query_fw_config_t;


#define SLI4_FUNCTION_MODE_FCOE_INI_MODE 0x40
#define SLI4_FUNCTION_MODE_FCOE_TGT_MODE 0x80
#define SLI4_FUNCTION_MODE_DUA_MODE      0x800

#define SLI4_ULP_MODE_FCOE_INI           0x40
#define SLI4_ULP_MODE_FCOE_TGT           0x80

typedef struct sli4_res_common_query_fw_config_s {
	sli4_res_hdr_t	hdr;
	uint32_t	config_number;
	uint32_t	asic_rev;
	uint32_t	physical_port;
	uint32_t	function_mode;
	uint32_t	ulp0_mode;
	uint32_t	ulp0_nic_wqid_base;
	uint32_t	ulp0_nic_wq_total; /* Dword 10 */
	uint32_t	ulp0_toe_wqid_base;
	uint32_t	ulp0_toe_wq_total;
	uint32_t	ulp0_toe_rqid_base;
	uint32_t	ulp0_toe_rq_total;
	uint32_t	ulp0_toe_defrqid_base;
	uint32_t	ulp0_toe_defrq_total;
	uint32_t	ulp0_lro_rqid_base;
	uint32_t	ulp0_lro_rq_total;
	uint32_t	ulp0_iscsi_icd_base;
	uint32_t	ulp0_iscsi_icd_total; /* Dword 20 */
	uint32_t	ulp1_mode;
	uint32_t	ulp1_nic_wqid_base;
	uint32_t	ulp1_nic_wq_total;
	uint32_t	ulp1_toe_wqid_base;
	uint32_t	ulp1_toe_wq_total;
	uint32_t	ulp1_toe_rqid_base;
	uint32_t	ulp1_toe_rq_total;
	uint32_t	ulp1_toe_defrqid_base;
	uint32_t	ulp1_toe_defrq_total;
	uint32_t	ulp1_lro_rqid_base;  /* Dword 30 */
	uint32_t	ulp1_lro_rq_total;
	uint32_t	ulp1_iscsi_icd_base;
	uint32_t	ulp1_iscsi_icd_total;
	uint32_t	function_capabilities;
	uint32_t	ulp0_cq_base;
	uint32_t	ulp0_cq_total;
	uint32_t	ulp0_eq_base;
	uint32_t	ulp0_eq_total;
	uint32_t	ulp0_iscsi_chain_icd_base;
	uint32_t	ulp0_iscsi_chain_icd_total;  /* Dword 40 */
	uint32_t	ulp1_iscsi_chain_icd_base;
	uint32_t	ulp1_iscsi_chain_icd_total;
} sli4_res_common_query_fw_config_t;

/**
 * @brief COMMON_GET_PORT_NAME
 */
typedef struct sli4_req_common_get_port_name_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	pt:2,		/* only COMMON_GET_PORT_NAME_V1 */
			:30;
#else
#error big endian version not defined
#endif
} sli4_req_common_get_port_name_t;

typedef struct sli4_res_common_get_port_name_s {
	sli4_res_hdr_t	hdr;
	char		port_name[4];
} sli4_res_common_get_port_name_t;

/**
 * @brief COMMON_WRITE_FLASHROM
 */
typedef struct sli4_req_common_write_flashrom_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	flash_rom_access_opcode;
	uint32_t	flash_rom_access_operation_type;
	uint32_t	data_buffer_size;
	uint32_t	offset;
	uint8_t		data_buffer[4];
#else
#error big endian version not defined
#endif
} sli4_req_common_write_flashrom_t;

#define SLI4_MGMT_FLASHROM_OPCODE_FLASH			0x01
#define SLI4_MGMT_FLASHROM_OPCODE_SAVE			0x02
#define SLI4_MGMT_FLASHROM_OPCODE_CLEAR			0x03
#define SLI4_MGMT_FLASHROM_OPCODE_REPORT		0x04
#define SLI4_MGMT_FLASHROM_OPCODE_IMAGE_INFO		0x05
#define SLI4_MGMT_FLASHROM_OPCODE_IMAGE_CRC		0x06
#define SLI4_MGMT_FLASHROM_OPCODE_OFFSET_BASED_FLASH	0x07
#define SLI4_MGMT_FLASHROM_OPCODE_OFFSET_BASED_SAVE	0x08
#define SLI4_MGMT_PHY_FLASHROM_OPCODE_FLASH		0x09
#define SLI4_MGMT_PHY_FLASHROM_OPCODE_SAVE		0x0a

#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_ISCSI		0x00
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_REDBOOT		0x01
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_BIOS		0x02
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_PXE_BIOS		0x03
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_CODE_CONTROL	0x04
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_IPSEC_CFG		0x05
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_INIT_DATA		0x06
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_ROM_OFFSET	0x07
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_FCOE_BIOS		0x08
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_ISCSI_BAK		0x09
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_FCOE_ACT		0x0a
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_FCOE_BAK		0x0b
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_CODE_CTRL_P	0x0c
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_NCSI		0x0d
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_NIC		0x0e
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_DCBX		0x0f
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_PXE_BIOS_CFG	0x10
#define SLI4_FLASH_ROM_ACCESS_OP_TYPE_ALL_CFG_DATA	0x11

/**
 * @brief COMMON_MANAGE_FAT
 */
typedef struct sli4_req_common_manage_fat_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	fat_operation;
	uint32_t	read_log_offset;
	uint32_t	read_log_length;
	uint32_t	data_buffer_size;
	uint32_t	data_buffer;		/* response only */
#else
#error big endian version not defined
#endif
} sli4_req_common_manage_fat_t;

/**
 * @brief COMMON_GET_EXT_FAT_CAPABILITIES
 */
typedef struct sli4_req_common_get_ext_fat_capabilities_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	parameter_type;
#else
#error big endian version not defined
#endif
} sli4_req_common_get_ext_fat_capabilities_t;

/**
 * @brief COMMON_SET_EXT_FAT_CAPABILITIES
 */
typedef struct sli4_req_common_set_ext_fat_capabilities_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	maximum_log_entries;
	uint32_t	log_entry_size;
	uint32_t	logging_type:8,
			maximum_logging_functions:8,
			maximum_logging_ports:8,
			:8;
	uint32_t	supported_modes;
	uint32_t	number_modules;
	uint32_t	debug_module[14];
#else
#error big endian version not defined
#endif
} sli4_req_common_set_ext_fat_capabilities_t;

/**
 * @brief COMMON_EXT_FAT_CONFIGURE_SNAPSHOT
 */
typedef struct sli4_req_common_ext_fat_configure_snapshot_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	total_log_entries;
#else
#error big endian version not defined
#endif
} sli4_req_common_ext_fat_configure_snapshot_t;

/**
 * @brief COMMON_EXT_FAT_RETRIEVE_SNAPSHOT
 */
typedef struct sli4_req_common_ext_fat_retrieve_snapshot_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	snapshot_mode;
	uint32_t	start_index;
	uint32_t	number_log_entries;
#else
#error big endian version not defined
#endif
} sli4_req_common_ext_fat_retrieve_snapshot_t;

typedef struct sli4_res_common_ext_fat_retrieve_snapshot_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	number_log_entries;
	uint32_t	version:8,
			physical_port:8,
			function_id:16;
	uint32_t	trace_level;
	uint32_t	module_mask[2];
	uint32_t	trace_table_index;
	uint32_t	timestamp;
	uint8_t		string_data[16];
	uint32_t	data[6];
#else
#error big endian version not defined
#endif
} sli4_res_common_ext_fat_retrieve_snapshot_t;

/**
 * @brief COMMON_EXT_FAT_READ_STRING_TABLE
 */
typedef struct sli4_req_common_ext_fat_read_string_table_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	byte_offset;
	uint32_t	number_bytes;
#else
#error big endian version not defined
#endif
} sli4_req_common_ext_fat_read_string_table_t;

typedef struct sli4_res_common_ext_fat_read_string_table_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	number_returned_bytes;
	uint32_t	number_remaining_bytes;
	uint32_t	table_data0:8,
			:24;
	uint8_t		table_data[0];
#else
#error big endian version not defined
#endif
} sli4_res_common_ext_fat_read_string_table_t;

/**
 * @brief COMMON_READ_TRANSCEIVER_DATA
 *
 * This command reads SFF transceiver data(Format is defined
 * by the SFF-8472 specification).
 */
typedef struct sli4_req_common_read_transceiver_data_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	page_number;
	uint32_t	port;
#else
#error big endian version not defined
#endif
} sli4_req_common_read_transceiver_data_t;

typedef struct sli4_res_common_read_transceiver_data_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	page_number;
	uint32_t	port;
	uint32_t	page_data[32];
	uint32_t	page_data_2[32];
#else
#error big endian version not defined
#endif
} sli4_res_common_read_transceiver_data_t;

/**
 * @brief COMMON_READ_OBJECT
 */
typedef struct sli4_req_common_read_object_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	desired_read_length:24,
			:8;
	uint32_t	read_offset;
	uint8_t		object_name[104];
	uint32_t	host_buffer_descriptor_count;
	sli4_bde_t	host_buffer_descriptor[0];
#else
#error big endian version not defined
#endif
} sli4_req_common_read_object_t;

typedef struct sli4_res_common_read_object_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	actual_read_length;
	uint32_t	resv:31,
			eof:1;
#else
#error big endian version not defined
#endif
} sli4_res_common_read_object_t;

/**
 * @brief COMMON_WRITE_OBJECT
 */
typedef struct sli4_req_common_write_object_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	desired_write_length:24,
			:6,
			noc:1,
			eof:1;
	uint32_t	write_offset;
	uint8_t		object_name[104];
	uint32_t	host_buffer_descriptor_count;
	sli4_bde_t	host_buffer_descriptor[0];
#else
#error big endian version not defined
#endif
} sli4_req_common_write_object_t;

typedef struct sli4_res_common_write_object_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	actual_write_length;
	uint32_t	change_status:8,
			:24;
#else
#error big endian version not defined
#endif
} sli4_res_common_write_object_t;

/**
 * @brief COMMON_DELETE_OBJECT
 */
typedef struct sli4_req_common_delete_object_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rsvd4;
	uint32_t	rsvd5;
	uint8_t		object_name[104];
#else
#error big endian version not defined
#endif
} sli4_req_common_delete_object_t;

/**
 * @brief COMMON_READ_OBJECT_LIST
 */
typedef struct sli4_req_common_read_object_list_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	desired_read_length:24,
			:8;
	uint32_t	read_offset;
	uint8_t		object_name[104];
	uint32_t	host_buffer_descriptor_count;
	sli4_bde_t	host_buffer_descriptor[0];
#else
#error big endian version not defined
#endif
} sli4_req_common_read_object_list_t;

/**
 * @brief COMMON_SET_DUMP_LOCATION
 */
typedef struct sli4_req_common_set_dump_location_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	buffer_length:24,
			:5,
			fdb:1,
			blp:1,
			qry:1;
	uint32_t	buf_addr_low;
	uint32_t	buf_addr_high;
#else
#error big endian version not defined
#endif
} sli4_req_common_set_dump_location_t;

typedef struct sli4_res_common_set_dump_location_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	buffer_length:24,
			:8;
#else
#error big endian version not defined
#endif
}sli4_res_common_set_dump_location_t;

/**
 * @brief COMMON_SET_SET_FEATURES
 */
#define SLI4_SET_FEATURES_DIF_SEED			0x01
#define SLI4_SET_FEATURES_XRI_TIMER			0x03
#define SLI4_SET_FEATURES_MAX_PCIE_SPEED		0x04
#define SLI4_SET_FEATURES_FCTL_CHECK			0x05
#define SLI4_SET_FEATURES_FEC				0x06
#define SLI4_SET_FEATURES_PCIE_RECV_DETECT		0x07
#define SLI4_SET_FEATURES_DIF_MEMORY_MODE		0x08
#define SLI4_SET_FEATURES_DISABLE_SLI_PORT_PAUSE_STATE	0x09
#define SLI4_SET_FEATURES_ENABLE_PCIE_OPTIONS		0x0A
#define SLI4_SET_FEATURES_SET_CONFIG_AUTO_XFER_RDY_T10PI	0x0C
#define SLI4_SET_FEATURES_ENABLE_MULTI_RECEIVE_QUEUE	0x0D
#define SLI4_SET_FEATURES_SET_FTD_XFER_HINT		0x0F
#define SLI4_SET_FEATURES_SLI_PORT_HEALTH_CHECK		0x11

typedef struct sli4_req_common_set_features_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	feature;
	uint32_t	param_len;
	uint32_t	params[8];
#else
#error big endian version not defined
#endif
} sli4_req_common_set_features_t;

typedef struct sli4_req_common_set_features_dif_seed_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	seed:16,
		:16;
#else
#error big endian version not defined
#endif
} sli4_req_common_set_features_dif_seed_t;

typedef struct sli4_req_common_set_features_t10_pi_mem_model_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	tmm:1,
		:31;
#else
#error big endian version not defined
#endif
} sli4_req_common_set_features_t10_pi_mem_model_t;

typedef struct sli4_req_common_set_features_multirq_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	isr:1,			/*<< Include Sequence Reporting */
			agxfe:1,		/*<< Auto Generate XFER-RDY Feature Enabled */
			:30;
	uint32_t	num_rqs:8,
			rq_select_policy:4,
			:20;
#else
#error big endian version not defined
#endif
} sli4_req_common_set_features_multirq_t;

typedef struct sli4_req_common_set_features_xfer_rdy_t10pi_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rtc:1,
			atv:1,
			tmm:1,
			:1,
			p_type:3,
			blk_size:3,
			:22;
	uint32_t	app_tag:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_req_common_set_features_xfer_rdy_t10pi_t;

typedef struct sli4_req_common_set_features_health_check_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	hck:1,
			qry:1,
			:30;
#else
#error big endian version not defined
#endif
} sli4_req_common_set_features_health_check_t;

typedef struct sli4_req_common_set_features_set_fdt_xfer_hint_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	fdt_xfer_hint;
#else
#error big endian version not defined
#endif
} sli4_req_common_set_features_set_fdt_xfer_hint_t;

/**
 * @brief DMTF_EXEC_CLP_CMD
 */
typedef struct sli4_req_dmtf_exec_clp_cmd_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	cmd_buf_length;
	uint32_t	resp_buf_length;
	uint32_t	cmd_buf_addr_low;
	uint32_t	cmd_buf_addr_high;
	uint32_t	resp_buf_addr_low;
	uint32_t	resp_buf_addr_high;
#else
#error big endian version not defined
#endif
} sli4_req_dmtf_exec_clp_cmd_t;

typedef struct sli4_res_dmtf_exec_clp_cmd_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:32;
	uint32_t	resp_length;
	uint32_t	:32;
	uint32_t	:32;
	uint32_t	:32;
	uint32_t	:32;
	uint32_t	clp_status;
	uint32_t	clp_detailed_status;
#else
#error big endian version not defined
#endif
} sli4_res_dmtf_exec_clp_cmd_t;

/**
 * @brief Resource descriptor
 */

#define SLI4_RESOURCE_DESCRIPTOR_TYPE_PCIE	0x50
#define SLI4_RESOURCE_DESCRIPTOR_TYPE_NIC	0x51
#define SLI4_RESOURCE_DESCRIPTOR_TYPE_ISCSI	0x52
#define SLI4_RESOURCE_DESCRIPTOR_TYPE_FCFCOE	0x53
#define SLI4_RESOURCE_DESCRIPTOR_TYPE_RDMA	0x54
#define SLI4_RESOURCE_DESCRIPTOR_TYPE_PORT	0x55
#define SLI4_RESOURCE_DESCRIPTOR_TYPE_ISAP	0x56

#define SLI4_PROTOCOL_NIC_TOE			0x01
#define SLI4_PROTOCOL_ISCSI			0x02
#define SLI4_PROTOCOL_FCOE			0x04
#define SLI4_PROTOCOL_NIC_TOE_RDMA		0x08
#define SLI4_PROTOCOL_FC			0x10
#define SLI4_PROTOCOL_DEFAULT			0xff

typedef struct sli4_resource_descriptor_v1_s {
	uint32_t	descriptor_type:8,
			descriptor_length:8,
			:16;
	uint32_t	type_specific[0];
} sli4_resource_descriptor_v1_t;

typedef struct sli4_pcie_resource_descriptor_v1_s {
	uint32_t	descriptor_type:8,
			descriptor_length:8,
			:14,
			imm:1,
			nosv:1;
	uint32_t	:16,
			pf_number:10,
			:6;
	uint32_t        rsvd1;
	uint32_t        sriov_state:8,
			pf_state:8,
			pf_type:8,
			:8;
	uint32_t        number_of_vfs:16,
			:16;
	uint32_t        mission_roles:8,
			:19,
			pchg:1,
			schg:1,
			xchg:1,
			xrom:2;
	uint32_t        rsvd2[16];
} sli4_pcie_resource_descriptor_v1_t;

typedef struct sli4_isap_resource_descriptor_v1_s {
	uint32_t        descriptor_type:8,
			descriptor_length:8,
			:16;
	uint32_t        iscsi_tgt:1,
			iscsi_ini:1,
			iscsi_dif:1,
			:29;
	uint32_t        rsvd1[3];
	uint32_t        fcoe_tgt:1,
			fcoe_ini:1,
			fcoe_dif:1,
			:29;
	uint32_t        rsvd2[7];
	uint32_t        mc_type0:8,
			mc_type1:8,
			mc_type2:8,
			mc_type3:8;
	uint32_t        rsvd3[3];
} sli4_isap_resouce_descriptor_v1_t;

/**
 * @brief COMMON_GET_FUNCTION_CONFIG
 */
typedef struct sli4_req_common_get_function_config_s {
	sli4_req_hdr_t  hdr;
} sli4_req_common_get_function_config_t;

typedef struct sli4_res_common_get_function_config_s {
	sli4_res_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t        desc_count;
	uint32_t        desc[54];
#else
#error big endian version not defined
#endif
} sli4_res_common_get_function_config_t;

/**
 * @brief COMMON_GET_PROFILE_CONFIG
 */
typedef struct sli4_req_common_get_profile_config_s {
	sli4_req_hdr_t  hdr;
	uint32_t        profile_id:8,
			typ:2,
			:22;
} sli4_req_common_get_profile_config_t;

typedef struct sli4_res_common_get_profile_config_s {
	sli4_res_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t        desc_count;
	uint32_t        desc[0];
#else
#error big endian version not defined
#endif
} sli4_res_common_get_profile_config_t;

/**
 * @brief COMMON_SET_PROFILE_CONFIG
 */
typedef struct sli4_req_common_set_profile_config_s {
	sli4_req_hdr_t  hdr;
	uint32_t        profile_id:8,
			:23,
			isap:1;
	uint32_t        desc_count;
	uint32_t        desc[0];
} sli4_req_common_set_profile_config_t;

typedef struct sli4_res_common_set_profile_config_s {
	sli4_res_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
#else
#error big endian version not defined
#endif
} sli4_res_common_set_profile_config_t;

/**
 * @brief Profile Descriptor for profile functions
 */
typedef struct sli4_profile_descriptor_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t        profile_id:8,
			:8,
			profile_index:8,
			:8;
	uint32_t        profile_description[128];
#else
#error big endian version not defined
#endif
} sli4_profile_descriptor_t;

/* We don't know in advance how many descriptors there are.  We have
   to pick a number that we think will be big enough and ask for that
   many. */

#define MAX_PRODUCT_DESCRIPTORS 40

/**
 * @brief COMMON_GET_PROFILE_LIST
 */
typedef struct sli4_req_common_get_profile_list_s {
	sli4_req_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t        start_profile_index:8,
			:24;
#else
#error big endian version not defined
#endif
} sli4_req_common_get_profile_list_t;

typedef struct sli4_res_common_get_profile_list_s {
	sli4_res_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t        profile_descriptor_count;
	sli4_profile_descriptor_t profile_descriptor[MAX_PRODUCT_DESCRIPTORS];
#else
#error big endian version not defined
#endif
} sli4_res_common_get_profile_list_t;

/**
 * @brief COMMON_GET_ACTIVE_PROFILE
 */
typedef struct sli4_req_common_get_active_profile_s {
	sli4_req_hdr_t  hdr;
} sli4_req_common_get_active_profile_t;

typedef struct sli4_res_common_get_active_profile_s {
	sli4_res_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t        active_profile_id:8,
			:8,
			next_profile_id:8,
			:8;
#else
#error big endian version not defined
#endif
} sli4_res_common_get_active_profile_t;

/**
 * @brief COMMON_SET_ACTIVE_PROFILE
 */
typedef struct sli4_req_common_set_active_profile_s {
	sli4_req_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t        active_profile_id:8,
			:23,
			fd:1;
#else
#error big endian version not defined
#endif
} sli4_req_common_set_active_profile_t;

typedef struct sli4_res_common_set_active_profile_s {
	sli4_res_hdr_t  hdr;
} sli4_res_common_set_active_profile_t;

/**
 * @brief Link Config Descriptor for link config functions
 */
typedef struct sli4_link_config_descriptor_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t        link_config_id:8,
			:24;
	uint32_t        config_description[8];
#else
#error big endian version not defined
#endif
} sli4_link_config_descriptor_t;

#define MAX_LINK_CONFIG_DESCRIPTORS 10

/**
 * @brief COMMON_GET_RECONFIG_LINK_INFO
 */
typedef struct sli4_req_common_get_reconfig_link_info_s {
	sli4_req_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
#else
#error big endian version not defined
#endif
} sli4_req_common_get_reconfig_link_info_t;

typedef struct sli4_res_common_get_reconfig_link_info_s {
	sli4_res_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	active_link_config_id:8,
			:8,
			next_link_config_id:8,
			:8;
	uint32_t	link_configuration_descriptor_count;
	sli4_link_config_descriptor_t	desc[MAX_LINK_CONFIG_DESCRIPTORS];
#else
#error big endian version not defined
#endif
} sli4_res_common_get_reconfig_link_info_t;

/**
 * @brief COMMON_SET_RECONFIG_LINK_ID
 */
typedef struct sli4_req_common_set_reconfig_link_id_s {
	sli4_req_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	next_link_config_id:8,
			:23,
			fd:1;
#else
#error big endian version not defined
#endif
} sli4_req_common_set_reconfig_link_id_t;

typedef struct sli4_res_common_set_reconfig_link_id_s {
	sli4_res_hdr_t  hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
#else
#error big endian version not defined
#endif
} sli4_res_common_set_reconfig_link_id_t;


typedef struct sli4_req_lowlevel_set_watchdog_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	watchdog_timeout:16,
			:16;
#else
#error big endian version not defined
#endif

} sli4_req_lowlevel_set_watchdog_t;


typedef struct sli4_res_lowlevel_set_watchdog_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rsvd;
#else
#error big endian version not defined
#endif
} sli4_res_lowlevel_set_watchdog_t;

/**
 * @brief Event Queue Entry
 */
typedef struct sli4_eqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	vld:1,		/** valid */
			major_code:3,
			minor_code:12,
			resource_id:16;
#else
#error big endian version not defined
#endif
} sli4_eqe_t;

#define SLI4_MAJOR_CODE_STANDARD	0
#define SLI4_MAJOR_CODE_SENTINEL	1

/**
 * @brief Mailbox Completion Queue Entry
 *
 * A CQE generated on the completion of a MQE from a MQ.
 */
typedef struct sli4_mcqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	completion_status:16, /** values are protocol specific */
			extended_status:16;
	uint32_t	mqe_tag_low;
	uint32_t	mqe_tag_high;
	uint32_t	:27,
			con:1,		/** consumed - command now being executed */
			cmp:1,		/** completed - command still executing if clear */
			:1,
			ae:1,		/** async event - this is an ACQE */
			val:1;		/** valid - contents of CQE are valid */
#else
#error big endian version not defined
#endif
} sli4_mcqe_t;


/**
 * @brief Asynchronous Completion Queue Entry
 *
 * A CQE generated asynchronously in response to the link or other internal events.
 */
typedef struct sli4_acqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	event_data[3];
	uint32_t	:8,
			event_code:8,
			event_type:8,	/** values are protocol specific */
			:6,
			ae:1,		/** async event - this is an ACQE */
			val:1;		/** valid - contents of CQE are valid */
#else
#error big endian version not defined
#endif
} sli4_acqe_t;

#define SLI4_ACQE_EVENT_CODE_LINK_STATE		0x01
#define SLI4_ACQE_EVENT_CODE_FCOE_FIP		0x02
#define SLI4_ACQE_EVENT_CODE_DCBX		0x03
#define SLI4_ACQE_EVENT_CODE_ISCSI		0x04
#define SLI4_ACQE_EVENT_CODE_GRP_5		0x05
#define SLI4_ACQE_EVENT_CODE_FC_LINK_EVENT	0x10
#define SLI4_ACQE_EVENT_CODE_SLI_PORT_EVENT	0x11
#define SLI4_ACQE_EVENT_CODE_VF_EVENT		0x12
#define SLI4_ACQE_EVENT_CODE_MR_EVENT		0x13

/**
 * @brief Register name enums
 */
typedef enum {
	SLI4_REG_BMBX,
	SLI4_REG_EQCQ_DOORBELL,
	SLI4_REG_FCOE_RQ_DOORBELL,
	SLI4_REG_IO_WQ_DOORBELL,
	SLI4_REG_MQ_DOORBELL,
	SLI4_REG_PHYSDEV_CONTROL,
	SLI4_REG_SLIPORT_CONTROL,
	SLI4_REG_SLIPORT_ERROR1,
	SLI4_REG_SLIPORT_ERROR2,
	SLI4_REG_SLIPORT_SEMAPHORE,
	SLI4_REG_SLIPORT_STATUS,
	SLI4_REG_UERR_MASK_HI,
	SLI4_REG_UERR_MASK_LO,
	SLI4_REG_UERR_STATUS_HI,
	SLI4_REG_UERR_STATUS_LO,
	SLI4_REG_SW_UE_CSR1,
	SLI4_REG_SW_UE_CSR2,
	SLI4_REG_MAX			/* must be last */
} sli4_regname_e;

typedef struct sli4_reg_s {
	uint32_t	rset;
	uint32_t	off;
} sli4_reg_t;

typedef enum {
	SLI_QTYPE_EQ,
	SLI_QTYPE_CQ,
	SLI_QTYPE_MQ,
	SLI_QTYPE_WQ,
	SLI_QTYPE_RQ,
	SLI_QTYPE_MAX,			/* must be last */
} sli4_qtype_e;

#define SLI_USER_MQ_COUNT	1	/** User specified max mail queues */
#define SLI_MAX_CQ_SET_COUNT	16
#define SLI_MAX_RQ_SET_COUNT	16

typedef enum {
	SLI_QENTRY_ASYNC,
	SLI_QENTRY_MQ,
	SLI_QENTRY_RQ,
	SLI_QENTRY_WQ,
	SLI_QENTRY_WQ_RELEASE,
	SLI_QENTRY_OPT_WRITE_CMD,
	SLI_QENTRY_OPT_WRITE_DATA,
	SLI_QENTRY_XABT,
	SLI_QENTRY_MAX			/* must be last */
} sli4_qentry_e;

typedef struct sli4_queue_s {
	/* Common to all queue types */
	ocs_dma_t	dma;
	ocs_lock_t	lock;
	uint32_t	index;		/** current host entry index */
	uint16_t	size;		/** entry size */
	uint16_t	length;		/** number of entries */
	uint16_t	n_posted;	/** number entries posted */
	uint16_t	id;		/** Port assigned xQ_ID */
	uint16_t	ulp;		/** ULP assigned to this queue */
	uint32_t	doorbell_offset;/** The offset for the doorbell */
	uint16_t	doorbell_rset;	/** register set for the doorbell */
	uint8_t		type;		/** queue type ie EQ, CQ, ... */
	uint32_t	proc_limit;	/** limit number of CQE processed per iteration */
	uint32_t	posted_limit;	/** number of CQE/EQE to process before ringing doorbell */
	uint32_t	max_num_processed;
	time_t		max_process_time;

	/* Type specific gunk */
	union {
		uint32_t	r_idx;	/** "read" index (MQ only) */
		struct {
			uint32_t	is_mq:1,/** CQ contains MQ/Async completions */
					is_hdr:1,/** is a RQ for packet headers */
					rq_batch:1;/** RQ index incremented by 8 */
		} flag;
	} u;
} sli4_queue_t;

static inline void
sli_queue_lock(sli4_queue_t *q)
{
	ocs_lock(&q->lock);
}

static inline void
sli_queue_unlock(sli4_queue_t *q)
{
	ocs_unlock(&q->lock);
}


#define SLI4_QUEUE_DEFAULT_CQ	UINT16_MAX /** Use the default CQ */

#define SLI4_QUEUE_RQ_BATCH	8

typedef enum {
	SLI4_CB_LINK,
	SLI4_CB_FIP,
	SLI4_CB_MAX			/* must be last */
} sli4_callback_e;

typedef enum {
	SLI_LINK_STATUS_UP,
	SLI_LINK_STATUS_DOWN,
	SLI_LINK_STATUS_NO_ALPA,
	SLI_LINK_STATUS_MAX,
} sli4_link_status_e;

typedef enum {
	SLI_LINK_TOPO_NPORT = 1,	/** fabric or point-to-point */
	SLI_LINK_TOPO_LOOP,
	SLI_LINK_TOPO_LOOPBACK_INTERNAL,
	SLI_LINK_TOPO_LOOPBACK_EXTERNAL,
	SLI_LINK_TOPO_NONE,
	SLI_LINK_TOPO_MAX,
} sli4_link_topology_e;

/* TODO do we need both sli4_port_type_e & sli4_link_medium_e */
typedef enum {
	SLI_LINK_MEDIUM_ETHERNET,
	SLI_LINK_MEDIUM_FC,
	SLI_LINK_MEDIUM_MAX,
} sli4_link_medium_e;

typedef struct sli4_link_event_s {
	sli4_link_status_e	status;		/* link up/down */
	sli4_link_topology_e	topology;
	sli4_link_medium_e	medium;		/* Ethernet / FC */
	uint32_t		speed;		/* Mbps */
	uint8_t			*loop_map;
	uint32_t		fc_id;
} sli4_link_event_t;

/**
 * @brief Fields retrieved from skyhawk that used used to build chained SGL
 */
typedef struct sli4_sgl_chaining_params_s {
	uint8_t		chaining_capable;
	uint16_t	frag_num_field_offset;
	uint16_t	sgl_index_field_offset;
	uint64_t	frag_num_field_mask;
	uint64_t	sgl_index_field_mask;
	uint32_t	chain_sge_initial_value_lo;
	uint32_t	chain_sge_initial_value_hi;
} sli4_sgl_chaining_params_t;

typedef struct sli4_fip_event_s {
	uint32_t	type;
	uint32_t	index;		/* FCF index or UINT32_MAX if invalid */
} sli4_fip_event_t;

typedef enum {
	SLI_RSRC_FCOE_VFI,
	SLI_RSRC_FCOE_VPI,
	SLI_RSRC_FCOE_RPI,
	SLI_RSRC_FCOE_XRI,
	SLI_RSRC_FCOE_FCFI,
	SLI_RSRC_MAX			/* must be last */
} sli4_resource_e;

typedef enum {
	SLI4_PORT_TYPE_FC,
	SLI4_PORT_TYPE_NIC,
	SLI4_PORT_TYPE_MAX		/* must be last */
} sli4_port_type_e;

typedef enum {
	SLI4_ASIC_TYPE_BE3 = 1,
	SLI4_ASIC_TYPE_SKYHAWK,
	SLI4_ASIC_TYPE_LANCER,
	SLI4_ASIC_TYPE_CORSAIR,
	SLI4_ASIC_TYPE_LANCERG6,
} sli4_asic_type_e;

typedef enum {
	SLI4_ASIC_REV_FPGA = 1,
	SLI4_ASIC_REV_A0,
	SLI4_ASIC_REV_A1,
	SLI4_ASIC_REV_A2,
	SLI4_ASIC_REV_A3,
	SLI4_ASIC_REV_B0,
	SLI4_ASIC_REV_B1,
	SLI4_ASIC_REV_C0,
	SLI4_ASIC_REV_D0,
} sli4_asic_rev_e;

typedef struct sli4_s {
	ocs_os_handle_t	os;
	sli4_port_type_e port_type;

	uint32_t	sli_rev;	/* SLI revision number */
	uint32_t	sli_family;
	uint32_t	if_type;	/* SLI Interface type */

	sli4_asic_type_e asic_type;	/*<< ASIC type */
	sli4_asic_rev_e asic_rev;	/*<< ASIC revision */
	uint32_t	physical_port;

	struct {
		uint16_t		e_d_tov;
		uint16_t		r_a_tov;
		uint16_t		max_qcount[SLI_QTYPE_MAX];
		uint32_t		max_qentries[SLI_QTYPE_MAX];
		uint16_t		count_mask[SLI_QTYPE_MAX];
		uint16_t		count_method[SLI_QTYPE_MAX];
		uint32_t		qpage_count[SLI_QTYPE_MAX];
		uint16_t		link_module_type;
		uint8_t			rq_batch;
		uint16_t		rq_min_buf_size;
		uint32_t		rq_max_buf_size;
		uint8_t			topology;
		uint8_t			wwpn[8];
		uint8_t			wwnn[8];
		uint32_t		fw_rev[2];
		uint8_t			fw_name[2][16];
		char			ipl_name[16];
		uint32_t		hw_rev[3];
		uint8_t			port_number;
		char			port_name[2];
		char			bios_version_string[32];
		uint8_t			dual_ulp_capable;
		uint8_t			is_ulp_fc[2];
		/*
		 * Tracks the port resources using extents metaphor. For
		 * devices that don't implement extents (i.e.
		 * has_extents == FALSE), the code models each resource as
		 * a single large extent.
		 */
		struct {
			uint32_t	number;	/* number of extents */
			uint32_t	size;	/* number of elements in each extent */
			uint32_t	n_alloc;/* number of elements allocated */
			uint32_t	*base;
			ocs_bitmap_t	*use_map;/* bitmap showing resources in use */
			uint32_t	map_size;/* number of bits in bitmap */
		} extent[SLI_RSRC_MAX];
		sli4_features_t		features;
		uint32_t		has_extents:1,
					auto_reg:1,
					auto_xfer_rdy:1,
					hdr_template_req:1,
					perf_hint:1,
					perf_wq_id_association:1,
					cq_create_version:2,
					mq_create_version:2,
					high_login_mode:1,
					sgl_pre_registered:1,
					sgl_pre_registration_required:1,
					t10_dif_inline_capable:1,
					t10_dif_separate_capable:1;
		uint32_t		sge_supported_length;
		uint32_t		sgl_page_sizes;
		uint32_t		max_sgl_pages;
		sli4_sgl_chaining_params_t sgl_chaining_params;
		size_t			wqe_size;
	} config;

	/*
	 * Callback functions
	 */
	int32_t		(*link)(void *, void *);
	void		*link_arg;
	int32_t		(*fip)(void *, void *);
	void		*fip_arg;

	ocs_dma_t	bmbx;
#if defined(OCS_INCLUDE_DEBUG)
	/* Save pointer to physical memory descriptor for non-embedded SLI_CONFIG
	 * commands for BMBX dumping purposes */
	ocs_dma_t	*bmbx_non_emb_pmd;
#endif

	struct {
		ocs_dma_t	data;
		uint32_t	length;
	} vpd;
} sli4_t;

/**
 * Get / set parameter functions
 */
static inline uint32_t
sli_get_max_rsrc(sli4_t *sli4, sli4_resource_e rsrc)
{
	if (rsrc >= SLI_RSRC_MAX) {
		return 0;
	}

	return sli4->config.extent[rsrc].size;
}

static inline uint32_t
sli_get_max_queue(sli4_t *sli4, sli4_qtype_e qtype)
{
	if (qtype >= SLI_QTYPE_MAX) {
		return 0;
	}
	return sli4->config.max_qcount[qtype];
}

static inline uint32_t
sli_get_max_qentries(sli4_t *sli4, sli4_qtype_e qtype)
{

	return sli4->config.max_qentries[qtype];
}

static inline uint32_t
sli_get_max_sge(sli4_t *sli4)
{
	return sli4->config.sge_supported_length;
}

static inline uint32_t
sli_get_max_sgl(sli4_t *sli4)
{

	if (sli4->config.sgl_page_sizes != 1) {
		ocs_log_test(sli4->os, "unsupported SGL page sizes %#x\n",
				sli4->config.sgl_page_sizes);
		return 0;
	}

	return ((sli4->config.max_sgl_pages * SLI_PAGE_SIZE) / sizeof(sli4_sge_t));
}

static inline sli4_link_medium_e
sli_get_medium(sli4_t *sli4)
{
	switch (sli4->config.topology) {
	case SLI4_READ_CFG_TOPO_FCOE:
		return SLI_LINK_MEDIUM_ETHERNET;
	case SLI4_READ_CFG_TOPO_FC:
	case SLI4_READ_CFG_TOPO_FC_DA:
	case SLI4_READ_CFG_TOPO_FC_AL:
		return SLI_LINK_MEDIUM_FC;
	default:
		return SLI_LINK_MEDIUM_MAX;
	}
}

static inline void
sli_skh_chain_sge_build(sli4_t *sli4, sli4_sge_t *sge, uint32_t xri_index, uint32_t frag_num, uint32_t offset)
{
	sli4_sgl_chaining_params_t *cparms = &sli4->config.sgl_chaining_params;


	ocs_memset(sge, 0, sizeof(*sge));
	sge->sge_type = SLI4_SGE_TYPE_CHAIN;
	sge->buffer_address_high = (uint32_t)cparms->chain_sge_initial_value_hi;
	sge->buffer_address_low =
		(uint32_t)((cparms->chain_sge_initial_value_lo |
			    (((uintptr_t)(xri_index & cparms->sgl_index_field_mask)) <<
			     cparms->sgl_index_field_offset) |
			    (((uintptr_t)(frag_num & cparms->frag_num_field_mask)) <<
			     cparms->frag_num_field_offset)  |
			    offset) >> 3);
}

static inline uint32_t
sli_get_sli_rev(sli4_t *sli4)
{
	return sli4->sli_rev;
}

static inline uint32_t
sli_get_sli_family(sli4_t *sli4)
{
	return sli4->sli_family;
}

static inline uint32_t
sli_get_if_type(sli4_t *sli4)
{
	return sli4->if_type;
}

static inline void *
sli_get_wwn_port(sli4_t *sli4)
{
	return sli4->config.wwpn;
}

static inline void *
sli_get_wwn_node(sli4_t *sli4)
{
	return sli4->config.wwnn;
}

static inline void *
sli_get_vpd(sli4_t *sli4)
{
	return sli4->vpd.data.virt;
}

static inline uint32_t
sli_get_vpd_len(sli4_t *sli4)
{
	return sli4->vpd.length;
}

static inline uint32_t
sli_get_fw_revision(sli4_t *sli4, uint32_t which)
{
	return sli4->config.fw_rev[which];
}

static inline void *
sli_get_fw_name(sli4_t *sli4, uint32_t which)
{
	return sli4->config.fw_name[which];
}

static inline char *
sli_get_ipl_name(sli4_t *sli4)
{
	return sli4->config.ipl_name;
}

static inline uint32_t
sli_get_hw_revision(sli4_t *sli4, uint32_t which)
{
	return sli4->config.hw_rev[which];
}

static inline uint32_t
sli_get_auto_xfer_rdy_capable(sli4_t *sli4)
{
	return sli4->config.auto_xfer_rdy;
}

static inline uint32_t
sli_get_dif_capable(sli4_t *sli4)
{
	return sli4->config.features.flag.dif;
}

static inline uint32_t
sli_is_dif_inline_capable(sli4_t *sli4)
{
	return sli_get_dif_capable(sli4) && sli4->config.t10_dif_inline_capable;
}

static inline uint32_t
sli_is_dif_separate_capable(sli4_t *sli4)
{
	return sli_get_dif_capable(sli4) && sli4->config.t10_dif_separate_capable;
}

static inline uint32_t
sli_get_is_dual_ulp_capable(sli4_t *sli4)
{
	return sli4->config.dual_ulp_capable;
}

static inline uint32_t
sli_get_is_sgl_chaining_capable(sli4_t *sli4)
{
	return sli4->config.sgl_chaining_params.chaining_capable;
}

static inline uint32_t
sli_get_is_ulp_enabled(sli4_t *sli4, uint16_t ulp)
{
	return sli4->config.is_ulp_fc[ulp];
}

static inline uint32_t
sli_get_hlm_capable(sli4_t *sli4)
{
	return sli4->config.features.flag.hlm;
}

static inline int32_t
sli_set_hlm(sli4_t *sli4, uint32_t value)
{
	if (value && !sli4->config.features.flag.hlm) {
		ocs_log_test(sli4->os, "HLM not supported\n");
		return -1;
	}

	sli4->config.high_login_mode = value != 0 ? TRUE : FALSE;

	return 0;
}

static inline uint32_t
sli_get_hlm(sli4_t *sli4)
{
	return sli4->config.high_login_mode;
}

static inline uint32_t
sli_get_sgl_preregister_required(sli4_t *sli4)
{
	return sli4->config.sgl_pre_registration_required;
}

static inline uint32_t
sli_get_sgl_preregister(sli4_t *sli4)
{
	return sli4->config.sgl_pre_registered;
}

static inline int32_t
sli_set_sgl_preregister(sli4_t *sli4, uint32_t value)
{
	if ((value == 0) && sli4->config.sgl_pre_registration_required) {
		ocs_log_test(sli4->os, "SGL pre-registration required\n");
		return -1;
	}

	sli4->config.sgl_pre_registered = value != 0 ? TRUE : FALSE;

	return 0;
}

static inline sli4_asic_type_e
sli_get_asic_type(sli4_t *sli4)
{
	return sli4->asic_type;
}

static inline sli4_asic_rev_e
sli_get_asic_rev(sli4_t *sli4)
{
	return sli4->asic_rev;
}

static inline int32_t
sli_set_topology(sli4_t *sli4, uint32_t value)
{
	int32_t	rc = 0;

	switch (value) {
	case SLI4_READ_CFG_TOPO_FCOE:
	case SLI4_READ_CFG_TOPO_FC:
	case SLI4_READ_CFG_TOPO_FC_DA:
	case SLI4_READ_CFG_TOPO_FC_AL:
		sli4->config.topology = value;
		break;
	default:
		ocs_log_test(sli4->os, "unsupported topology %#x\n", value);
		rc = -1;
	}

	return rc;
}

static inline uint16_t
sli_get_link_module_type(sli4_t *sli4)
{
	return sli4->config.link_module_type;
}

static inline char *
sli_get_portnum(sli4_t *sli4)
{
	return sli4->config.port_name;
}

static inline char *
sli_get_bios_version_string(sli4_t *sli4)
{
	return sli4->config.bios_version_string;
}

static inline uint32_t
sli_convert_mask_to_count(uint32_t method, uint32_t mask)
{
	uint32_t count = 0;

	if (method) {
		count = 1 << ocs_lg2(mask);
		count *= 16;
	} else {
		count = mask;
	}

	return count;
}

/**
 * @brief Common Create Queue function prototype
 */
typedef int32_t (*sli4_create_q_fn_t)(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t, uint16_t);

/**
 * @brief Common Destroy Queue function prototype
 */
typedef int32_t (*sli4_destroy_q_fn_t)(sli4_t *, void *, size_t, uint16_t);


/****************************************************************************
 * Function prototypes
 */
extern int32_t sli_cmd_config_auto_xfer_rdy(sli4_t *, void *, size_t, uint32_t);
extern int32_t sli_cmd_config_auto_xfer_rdy_hp(sli4_t *, void *, size_t, uint32_t, uint32_t, uint32_t);
extern int32_t sli_cmd_config_link(sli4_t *, void *, size_t);
extern int32_t sli_cmd_down_link(sli4_t *, void *, size_t);
extern int32_t sli_cmd_dump_type4(sli4_t *, void *, size_t, uint16_t);
extern int32_t sli_cmd_common_read_transceiver_data(sli4_t *, void *, size_t, uint32_t, ocs_dma_t *);
extern int32_t sli_cmd_read_link_stats(sli4_t *, void *, size_t,uint8_t, uint8_t, uint8_t);
extern int32_t sli_cmd_read_status(sli4_t *sli4, void *buf, size_t size, uint8_t clear_counters);
extern int32_t sli_cmd_init_link(sli4_t *, void *, size_t, uint32_t, uint8_t);
extern int32_t sli_cmd_init_vfi(sli4_t *, void *, size_t, uint16_t, uint16_t, uint16_t);
extern int32_t sli_cmd_init_vpi(sli4_t *, void *, size_t, uint16_t, uint16_t);
extern int32_t sli_cmd_post_xri(sli4_t *, void *, size_t,  uint16_t, uint16_t);
extern int32_t sli_cmd_release_xri(sli4_t *, void *, size_t,  uint8_t);
extern int32_t sli_cmd_read_sparm64(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t);
extern int32_t sli_cmd_read_topology(sli4_t *, void *, size_t, ocs_dma_t *);
extern int32_t sli_cmd_read_nvparms(sli4_t *, void *, size_t);
extern int32_t sli_cmd_write_nvparms(sli4_t *, void *, size_t, uint8_t *, uint8_t *, uint8_t, uint32_t);
typedef struct {
	uint16_t rq_id;
	uint8_t r_ctl_mask;
	uint8_t r_ctl_match;
	uint8_t type_mask;
	uint8_t type_match;
} sli4_cmd_rq_cfg_t;
extern int32_t sli_cmd_reg_fcfi(sli4_t *, void *, size_t, uint16_t,
				sli4_cmd_rq_cfg_t rq_cfg[SLI4_CMD_REG_FCFI_NUM_RQ_CFG], uint16_t);
extern int32_t sli_cmd_reg_fcfi_mrq(sli4_t *, void *, size_t, uint8_t, uint16_t, uint16_t, uint8_t, uint8_t , uint16_t, sli4_cmd_rq_cfg_t *);

extern int32_t sli_cmd_reg_rpi(sli4_t *, void *, size_t, uint32_t, uint16_t, uint16_t, ocs_dma_t *, uint8_t, uint8_t);
extern int32_t sli_cmd_reg_vfi(sli4_t *, void *, size_t, ocs_domain_t *);
extern int32_t sli_cmd_reg_vpi(sli4_t *, void *, size_t, ocs_sli_port_t *, uint8_t);
extern int32_t sli_cmd_sli_config(sli4_t *, void *, size_t, uint32_t, ocs_dma_t *);
extern int32_t sli_cmd_unreg_fcfi(sli4_t *, void *, size_t, uint16_t);
extern int32_t sli_cmd_unreg_rpi(sli4_t *, void *, size_t, uint16_t, sli4_resource_e, uint32_t);
extern int32_t sli_cmd_unreg_vfi(sli4_t *, void *, size_t, ocs_domain_t *, uint32_t);
extern int32_t sli_cmd_unreg_vpi(sli4_t *, void *, size_t, uint16_t, uint32_t);
extern int32_t sli_cmd_common_nop(sli4_t *, void *, size_t, uint64_t);
extern int32_t sli_cmd_common_get_resource_extent_info(sli4_t *, void *, size_t, uint16_t);
extern int32_t sli_cmd_common_get_sli4_parameters(sli4_t *, void *, size_t);
extern int32_t sli_cmd_common_write_object(sli4_t *, void *, size_t,
		uint16_t, uint16_t, uint32_t, uint32_t, char *, ocs_dma_t *);
extern int32_t sli_cmd_common_delete_object(sli4_t *, void *, size_t, char *);
extern int32_t sli_cmd_common_read_object(sli4_t *, void *, size_t, uint32_t,
		uint32_t, char *, ocs_dma_t *);
extern int32_t sli_cmd_dmtf_exec_clp_cmd(sli4_t *sli4, void *buf, size_t size,
		ocs_dma_t *cmd,
		ocs_dma_t *resp);
extern int32_t sli_cmd_common_set_dump_location(sli4_t *sli4, void *buf, size_t size,
						uint8_t query, uint8_t is_buffer_list,
						ocs_dma_t *buffer, uint8_t fdb);
extern int32_t sli_cmd_common_set_features(sli4_t *, void *, size_t, uint32_t, uint32_t, void*);
extern int32_t sli_cmd_common_get_profile_list(sli4_t *sli4, void *buf,
		size_t size, uint32_t start_profile_index, ocs_dma_t *dma);
extern int32_t sli_cmd_common_get_active_profile(sli4_t *sli4, void *buf,
		size_t size);
extern int32_t sli_cmd_common_set_active_profile(sli4_t *sli4, void *buf,
		size_t size,
		uint32_t fd,
		uint32_t active_profile_id);
extern int32_t sli_cmd_common_get_reconfig_link_info(sli4_t *sli4, void *buf,
		size_t size, ocs_dma_t *dma);
extern int32_t sli_cmd_common_set_reconfig_link_id(sli4_t *sli4, void *buf,
		size_t size, ocs_dma_t *dma,
		uint32_t fd, uint32_t active_link_config_id);
extern int32_t sli_cmd_common_get_function_config(sli4_t *sli4, void *buf,
		size_t size);
extern int32_t sli_cmd_common_get_profile_config(sli4_t *sli4, void *buf,
		size_t size, ocs_dma_t *dma);
extern int32_t sli_cmd_common_set_profile_config(sli4_t *sli4, void *buf,
		size_t size, ocs_dma_t *dma,
		uint8_t profile_id, uint32_t descriptor_count,
		uint8_t isap);

extern int32_t sli_cqe_mq(void *);
extern int32_t sli_cqe_async(sli4_t *, void *);

extern int32_t sli_setup(sli4_t *, ocs_os_handle_t, sli4_port_type_e);
extern void sli_calc_max_qentries(sli4_t *sli4);
extern int32_t sli_init(sli4_t *);
extern int32_t sli_reset(sli4_t *);
extern int32_t sli_fw_reset(sli4_t *);
extern int32_t sli_teardown(sli4_t *);
extern int32_t sli_callback(sli4_t *, sli4_callback_e, void *, void *);
extern int32_t sli_bmbx_command(sli4_t *);
extern int32_t __sli_queue_init(sli4_t *, sli4_queue_t *, uint32_t, size_t, uint32_t, uint32_t);
extern int32_t __sli_create_queue(sli4_t *, sli4_queue_t *);
extern int32_t sli_eq_modify_delay(sli4_t *sli4, sli4_queue_t *eq, uint32_t num_eq, uint32_t shift, uint32_t delay_mult);
extern int32_t sli_queue_alloc(sli4_t *, uint32_t, sli4_queue_t *, uint32_t, sli4_queue_t *, uint16_t);
extern int32_t sli_cq_alloc_set(sli4_t *, sli4_queue_t *qs[], uint32_t, uint32_t, sli4_queue_t *eqs[]);
extern int32_t sli_get_queue_entry_size(sli4_t *, uint32_t);
extern int32_t sli_queue_free(sli4_t *, sli4_queue_t *, uint32_t, uint32_t);
extern int32_t sli_queue_reset(sli4_t *, sli4_queue_t *);
extern int32_t sli_queue_is_empty(sli4_t *, sli4_queue_t *);
extern int32_t sli_queue_eq_arm(sli4_t *, sli4_queue_t *, uint8_t);
extern int32_t sli_queue_arm(sli4_t *, sli4_queue_t *, uint8_t);
extern int32_t _sli_queue_write(sli4_t *, sli4_queue_t *, uint8_t *);
extern int32_t sli_queue_write(sli4_t *, sli4_queue_t *, uint8_t *);
extern int32_t sli_queue_read(sli4_t *, sli4_queue_t *, uint8_t *);
extern int32_t sli_queue_index(sli4_t *, sli4_queue_t *);
extern int32_t _sli_queue_poke(sli4_t *, sli4_queue_t *, uint32_t, uint8_t *);
extern int32_t sli_queue_poke(sli4_t *, sli4_queue_t *, uint32_t, uint8_t *);
extern int32_t sli_resource_alloc(sli4_t *, sli4_resource_e, uint32_t *, uint32_t *);
extern int32_t sli_resource_free(sli4_t *, sli4_resource_e, uint32_t);
extern int32_t sli_resource_reset(sli4_t *, sli4_resource_e);
extern int32_t sli_eq_parse(sli4_t *, uint8_t *, uint16_t *);
extern int32_t sli_cq_parse(sli4_t *, sli4_queue_t *, uint8_t *, sli4_qentry_e *, uint16_t *);

extern int32_t sli_raise_ue(sli4_t *, uint8_t);
extern int32_t sli_dump_is_ready(sli4_t *);
extern int32_t sli_dump_is_present(sli4_t *);
extern int32_t sli_reset_required(sli4_t *);
extern int32_t sli_fw_error_status(sli4_t *);
extern int32_t sli_fw_ready(sli4_t *);
extern uint32_t sli_reg_read(sli4_t *, sli4_regname_e);
extern void sli_reg_write(sli4_t *, sli4_regname_e, uint32_t);
extern int32_t sli_link_is_configurable(sli4_t *);

#include "ocs_fcp.h"

/**
 * @brief Maximum value for a FCFI
 *
 * Note that although most commands provide a 16 bit field for the FCFI,
 * the FC/FCoE Asynchronous Recived CQE format only provides 6 bits for
 * the returned FCFI. Then effectively, the FCFI cannot be larger than
 * 1 << 6 or 64.
 */
#define SLI4_MAX_FCFI	64

/**
 * @brief Maximum value for FCF index
 *
 * The SLI-4 specification uses a 16 bit field in most places for the FCF
 * index, but practically, this value will be much smaller. Arbitrarily
 * limit the max FCF index to match the max FCFI value.
 */
#define SLI4_MAX_FCF_INDEX	SLI4_MAX_FCFI

/*************************************************************************
 * SLI-4 FC/FCoE mailbox command formats and definitions.
 */

/**
 * FC/FCoE opcode (OPC) values.
 */
#define SLI4_OPC_FCOE_WQ_CREATE			0x1
#define SLI4_OPC_FCOE_WQ_DESTROY		0x2
#define SLI4_OPC_FCOE_POST_SGL_PAGES		0x3
#define SLI4_OPC_FCOE_RQ_CREATE			0x5
#define SLI4_OPC_FCOE_RQ_DESTROY		0x6
#define SLI4_OPC_FCOE_READ_FCF_TABLE		0x8
#define SLI4_OPC_FCOE_POST_HDR_TEMPLATES	0xb
#define SLI4_OPC_FCOE_REDISCOVER_FCF		0x10

/* Use the default CQ associated with the WQ */
#define SLI4_CQ_DEFAULT 0xffff

typedef struct sli4_physical_page_descriptor_s {
	uint32_t	low;
	uint32_t	high;
} sli4_physical_page_descriptor_t;

/**
 * @brief FCOE_WQ_CREATE
 *
 * Create a Work Queue for FC/FCoE use.
 */
#define SLI4_FCOE_WQ_CREATE_V0_MAX_PAGES	4

typedef struct sli4_req_fcoe_wq_create_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:8,
			dua:1,
			:7,
			cq_id:16;
	sli4_physical_page_descriptor_t page_physical_address[SLI4_FCOE_WQ_CREATE_V0_MAX_PAGES];
	uint32_t	bqu:1,
			:7,
			ulp:8,
			:16;
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_wq_create_t;

/**
 * @brief FCOE_WQ_CREATE_V1
 *
 * Create a version 1 Work Queue for FC/FCoE use.
 */
typedef struct sli4_req_fcoe_wq_create_v1_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:16,
			cq_id:16;
	uint32_t	page_size:8,
			wqe_size:4,
			:4,
			wqe_count:16;
	uint32_t	rsvd6;
	sli4_physical_page_descriptor_t page_physical_address[8];
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_wq_create_v1_t;

#define SLI4_FCOE_WQ_CREATE_V1_MAX_PAGES	8

/**
 * @brief FCOE_WQ_DESTROY
 *
 * Destroy an FC/FCoE Work Queue.
 */
typedef struct sli4_req_fcoe_wq_destroy_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	wq_id:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_wq_destroy_t;

/**
 * @brief FCOE_POST_SGL_PAGES
 *
 * Register the scatter gather list (SGL) memory and associate it with an XRI.
 */
typedef struct sli4_req_fcoe_post_sgl_pages_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	xri_start:16,
			xri_count:16;
	struct {
		uint32_t	page0_low;
		uint32_t	page0_high;
		uint32_t	page1_low;
		uint32_t	page1_high;
	} page_set[10];
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_post_sgl_pages_t;

/**
 * @brief FCOE_RQ_CREATE
 *
 * Create a Receive Queue for FC/FCoE use.
 */
typedef struct sli4_req_fcoe_rq_create_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:16,
			dua:1,
			bqu:1,
			:6,
			ulp:8;
	uint32_t	:16,
			rqe_count:4,
			:12;
	uint32_t	rsvd6;
	uint32_t	buffer_size:16,
			cq_id:16;
	uint32_t	rsvd8;
	sli4_physical_page_descriptor_t page_physical_address[8];
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_rq_create_t;

#define SLI4_FCOE_RQ_CREATE_V0_MAX_PAGES	8
#define SLI4_FCOE_RQ_CREATE_V0_MIN_BUF_SIZE	128
#define SLI4_FCOE_RQ_CREATE_V0_MAX_BUF_SIZE	2048

/**
 * @brief FCOE_RQ_CREATE_V1
 *
 * Create a version 1 Receive Queue for FC/FCoE use.
 */
typedef struct sli4_req_fcoe_rq_create_v1_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:16,
			:13,
			dim:1,
			dfd:1,
			dnb:1;
	uint32_t	page_size:8,
			rqe_size:4,
			:4,
			rqe_count:16;
	uint32_t	rsvd6;
	uint32_t	:16,
			cq_id:16;
	uint32_t	buffer_size;
	sli4_physical_page_descriptor_t page_physical_address[8];
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_rq_create_v1_t;


/**
 * @brief FCOE_RQ_CREATE_V2
 *
 * Create a version 2 Receive Queue for FC/FCoE use.
 */
typedef struct sli4_req_fcoe_rq_create_v2_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	num_pages:16,
			rq_count:8,
			:5,
			dim:1,
			dfd:1,
			dnb:1;
	uint32_t	page_size:8,
			rqe_size:4,
			:4,
			rqe_count:16;
	uint32_t	hdr_buffer_size:16,
			payload_buffer_size:16;
	uint32_t	base_cq_id:16,
			:16;
	uint32_t	rsvd;
	sli4_physical_page_descriptor_t page_physical_address[0];
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_rq_create_v2_t;


#define SLI4_FCOE_RQ_CREATE_V1_MAX_PAGES	8
#define SLI4_FCOE_RQ_CREATE_V1_MIN_BUF_SIZE	64
#define SLI4_FCOE_RQ_CREATE_V1_MAX_BUF_SIZE	2048

#define SLI4_FCOE_RQE_SIZE_8			0x2
#define SLI4_FCOE_RQE_SIZE_16			0x3
#define SLI4_FCOE_RQE_SIZE_32			0x4
#define SLI4_FCOE_RQE_SIZE_64			0x5
#define SLI4_FCOE_RQE_SIZE_128			0x6

#define SLI4_FCOE_RQ_PAGE_SIZE_4096		0x1
#define SLI4_FCOE_RQ_PAGE_SIZE_8192		0x2
#define SLI4_FCOE_RQ_PAGE_SIZE_16384		0x4
#define SLI4_FCOE_RQ_PAGE_SIZE_32768		0x8
#define SLI4_FCOE_RQ_PAGE_SIZE_64536		0x10

#define SLI4_FCOE_RQE_SIZE			8

/**
 * @brief FCOE_RQ_DESTROY
 *
 * Destroy an FC/FCoE Receive Queue.
 */
typedef struct sli4_req_fcoe_rq_destroy_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rq_id:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_rq_destroy_t;

/**
 * @brief FCOE_READ_FCF_TABLE
 *
 * Retrieve a FCF database (also known as a table) entry created by the SLI Port
 * during FIP discovery.
 */
typedef struct sli4_req_fcoe_read_fcf_table_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	fcf_index:16,
			:16;
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_read_fcf_table_t;

/* A FCF index of -1 on the request means return the first valid entry */
#define SLI4_FCOE_FCF_TABLE_FIRST		(UINT16_MAX)

/**
 * @brief FCF table entry
 *
 * This is the information returned by the FCOE_READ_FCF_TABLE command.
 */
typedef struct sli4_fcf_entry_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	max_receive_size;
	uint32_t	fip_keep_alive;
	uint32_t	fip_priority;
	uint8_t		fcf_mac_address[6];
	uint8_t		fcf_available;
	uint8_t		mac_address_provider;
	uint8_t		fabric_name_id[8];
	uint8_t		fc_map[3];
	uint8_t		val:1,
			fc:1,
			:5,
			sol:1;
	uint32_t	fcf_index:16,
			fcf_state:16;
	uint8_t		vlan_bitmap[512];
	uint8_t		switch_name[8];
#else
#error big endian version not defined
#endif
} sli4_fcf_entry_t;

/**
 * @brief FCOE_READ_FCF_TABLE response.
 */
typedef struct sli4_res_fcoe_read_fcf_table_s {
	sli4_res_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	event_tag;
	uint32_t	next_index:16,
			:16;
	sli4_fcf_entry_t fcf_entry;
#else
#error big endian version not defined
#endif
} sli4_res_fcoe_read_fcf_table_t;

/* A next FCF index of -1 in the response means this is the last valid entry */
#define SLI4_FCOE_FCF_TABLE_LAST		(UINT16_MAX)


/**
 * @brief FCOE_POST_HDR_TEMPLATES
 */
typedef struct sli4_req_fcoe_post_hdr_templates_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rpi_offset:16,
			page_count:16;
	sli4_physical_page_descriptor_t page_descriptor[0];
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_post_hdr_templates_t;

#define SLI4_FCOE_HDR_TEMPLATE_SIZE	64

/**
 * @brief FCOE_REDISCOVER_FCF
 */
typedef struct sli4_req_fcoe_rediscover_fcf_s {
	sli4_req_hdr_t	hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	fcf_count:16,
			:16;
	uint32_t	rsvd5;
	uint16_t	fcf_index[16];
#else
#error big endian version not defined
#endif
} sli4_req_fcoe_rediscover_fcf_t;


/**
 * Work Queue Entry (WQE) types.
 */
#define SLI4_WQE_ABORT			0x0f
#define SLI4_WQE_ELS_REQUEST64		0x8a
#define SLI4_WQE_FCP_IBIDIR64		0xac
#define SLI4_WQE_FCP_IREAD64		0x9a
#define SLI4_WQE_FCP_IWRITE64		0x98
#define SLI4_WQE_FCP_ICMND64		0x9c
#define SLI4_WQE_FCP_TRECEIVE64		0xa1
#define SLI4_WQE_FCP_CONT_TRECEIVE64	0xe5
#define SLI4_WQE_FCP_TRSP64		0xa3
#define SLI4_WQE_FCP_TSEND64		0x9f
#define SLI4_WQE_GEN_REQUEST64		0xc2
#define SLI4_WQE_SEND_FRAME		0xe1
#define SLI4_WQE_XMIT_BCAST64		0X84
#define SLI4_WQE_XMIT_BLS_RSP		0x97
#define SLI4_WQE_ELS_RSP64		0x95
#define SLI4_WQE_XMIT_SEQUENCE64	0x82
#define SLI4_WQE_REQUEUE_XRI		0x93

/**
 * WQE command types.
 */
#define SLI4_CMD_FCP_IREAD64_WQE	0x00
#define SLI4_CMD_FCP_ICMND64_WQE	0x00
#define SLI4_CMD_FCP_IWRITE64_WQE	0x01
#define SLI4_CMD_FCP_TRECEIVE64_WQE	0x02
#define SLI4_CMD_FCP_TRSP64_WQE		0x03
#define SLI4_CMD_FCP_TSEND64_WQE	0x07
#define SLI4_CMD_GEN_REQUEST64_WQE	0x08
#define SLI4_CMD_XMIT_BCAST64_WQE	0x08
#define SLI4_CMD_XMIT_BLS_RSP64_WQE	0x08
#define SLI4_CMD_ABORT_WQE		0x08
#define SLI4_CMD_XMIT_SEQUENCE64_WQE	0x08
#define SLI4_CMD_REQUEUE_XRI_WQE	0x0A
#define SLI4_CMD_SEND_FRAME_WQE		0x0a

#define SLI4_WQE_SIZE			0x05
#define SLI4_WQE_EXT_SIZE		0x06

#define SLI4_WQE_BYTES			(16 * sizeof(uint32_t))
#define SLI4_WQE_EXT_BYTES		(32 * sizeof(uint32_t))

/* Mask for ccp (CS_CTL) */
#define SLI4_MASK_CCP	0xfe /* Upper 7 bits of CS_CTL is priority */

/**
 * @brief Generic WQE
 */
typedef struct sli4_generic_wqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	cmd_spec0_5[6];
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	:2,
			ct:2,
			:4,
			command:8,
			class:3,
			:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
#else
#error big endian version not defined
#endif
} sli4_generic_wqe_t;

/**
 * @brief WQE used to abort exchanges.
 */
typedef struct sli4_abort_wqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	rsvd0;
	uint32_t	rsvd1;
	uint32_t	ext_t_tag;
	uint32_t	ia:1,
			ir:1,
			:6,
			criteria:8,
			:16;
	uint32_t	ext_t_mask;
	uint32_t	t_mask;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	:2,
			ct:2,
			:4,
			command:8,
			class:3,
			:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	t_tag;
	uint32_t	request_tag:16,
			:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
#else
#error big endian version not defined
#endif
} sli4_abort_wqe_t;

#define SLI4_ABORT_CRITERIA_XRI_TAG		0x01
#define SLI4_ABORT_CRITERIA_ABORT_TAG		0x02
#define SLI4_ABORT_CRITERIA_REQUEST_TAG		0x03
#define SLI4_ABORT_CRITERIA_EXT_ABORT_TAG	0x04

typedef enum {
	SLI_ABORT_XRI,
	SLI_ABORT_ABORT_ID,
	SLI_ABORT_REQUEST_ID,
	SLI_ABORT_MAX,		/* must be last */
} sli4_abort_type_e;

/**
 * @brief WQE used to create an ELS request.
 */
typedef struct sli4_els_request64_wqe_s {
	sli4_bde_t	els_request_payload;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	els_request_payload_length;
	uint32_t	sid:24,
			sp:1,
			:7;
	uint32_t	remote_id:24,
			:8;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	:2,
			ct:2,
			:4,
			command:8,
			class:3,
			ar:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			temporary_rpi:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			els_id:3,
			wqec:1,
			:8,
			cq_id:16;
	sli4_bde_t	els_response_payload_bde;
	uint32_t	max_response_payload_length;
#else
#error big endian version not defined
#endif
} sli4_els_request64_wqe_t;

#define SLI4_ELS_REQUEST64_CONTEXT_RPI	0x0
#define SLI4_ELS_REQUEST64_CONTEXT_VPI	0x1
#define SLI4_ELS_REQUEST64_CONTEXT_VFI	0x2
#define SLI4_ELS_REQUEST64_CONTEXT_FCFI	0x3

#define SLI4_ELS_REQUEST64_CLASS_2	0x1
#define SLI4_ELS_REQUEST64_CLASS_3	0x2

#define SLI4_ELS_REQUEST64_DIR_WRITE	0x0
#define SLI4_ELS_REQUEST64_DIR_READ	0x1

#define SLI4_ELS_REQUEST64_OTHER	0x0
#define SLI4_ELS_REQUEST64_LOGO		0x1
#define SLI4_ELS_REQUEST64_FDISC	0x2
#define SLI4_ELS_REQUEST64_FLOGIN	0x3
#define SLI4_ELS_REQUEST64_PLOGI	0x4

#define SLI4_ELS_REQUEST64_CMD_GEN		0x08
#define SLI4_ELS_REQUEST64_CMD_NON_FABRIC	0x0c
#define SLI4_ELS_REQUEST64_CMD_FABRIC		0x0d

/**
 * @brief WQE used to create an FCP initiator no data command.
 */
typedef struct sli4_fcp_icmnd64_wqe_s {
	sli4_bde_t	bde;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	payload_offset_length:16,
			fcp_cmd_buffer_length:16;
	uint32_t	rsvd4;
	uint32_t	remote_n_port_id:24,
			:8;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	dif:2,
			ct:2,
			bs:3,
			:1,
			command:8,
			class:3,
			:1,
			pu:2,
			erp:1,
			lnk:1,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	rsvd12;
	uint32_t	rsvd13;
	uint32_t	rsvd14;
	uint32_t	rsvd15;
#else
#error big endian version not defined
#endif
} sli4_fcp_icmnd64_wqe_t;

/**
 * @brief WQE used to create an FCP initiator read.
 */
typedef struct sli4_fcp_iread64_wqe_s {
	sli4_bde_t	bde;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	payload_offset_length:16,
			fcp_cmd_buffer_length:16;
	uint32_t	total_transfer_length;
	uint32_t	remote_n_port_id:24,
			:8;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	dif:2,
			ct:2,
			bs:3,
			:1,
			command:8,
			class:3,
			:1,
			pu:2,
			erp:1,
			lnk:1,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	rsvd12;
#else
#error big endian version not defined
#endif
	sli4_bde_t	first_data_bde;	/* reserved if performance hints disabled */
} sli4_fcp_iread64_wqe_t;

/**
 * @brief WQE used to create an FCP initiator write.
 */
typedef struct sli4_fcp_iwrite64_wqe_s {
	sli4_bde_t	bde;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	payload_offset_length:16,
			fcp_cmd_buffer_length:16;
	uint32_t	total_transfer_length;
	uint32_t	initial_transfer_length;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	dif:2,
			ct:2,
			bs:3,
			:1,
			command:8,
			class:3,
			:1,
			pu:2,
			erp:1,
			lnk:1,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	remote_n_port_id:24,
			:8;
#else
#error big endian version not defined
#endif
	sli4_bde_t	first_data_bde;
} sli4_fcp_iwrite64_wqe_t;


typedef struct sli4_fcp_128byte_wqe_s {
	uint32_t dw[32];	
} sli4_fcp_128byte_wqe_t;

/**
 * @brief WQE used to create an FCP target receive, and FCP target
 * receive continue.
 */
typedef struct sli4_fcp_treceive64_wqe_s {
	sli4_bde_t	bde;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	payload_offset_length;
	uint32_t	relative_offset;
	/**
	 * DWord 5 can either be the task retry identifier (HLM=0) or
	 * the remote N_Port ID (HLM=1), or if implementing the Skyhawk
	 * T10-PI workaround, the secondary xri tag
	 */
	union {
		uint32_t	sec_xri_tag:16,
				:16;
		uint32_t	dword;
	} dword5;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	dif:2,
			ct:2,
			bs:3,
			:1,
			command:8,
			class:3,
			ar:1,
			pu:2,
			conf:1,
			lnk:1,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			remote_xid:16;
	uint32_t	ebde_cnt:4,
			:1,
			app_id_valid:1,
			:1,
			len_loc:2,
			qosd:1,
			wchn:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			sr:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	fcp_data_receive_length;

#else
#error big endian version not defined
#endif
	sli4_bde_t	first_data_bde; /* For performance hints */

} sli4_fcp_treceive64_wqe_t;

/**
 * @brief WQE used to create an FCP target response.
 */
typedef struct sli4_fcp_trsp64_wqe_s {
	sli4_bde_t	bde;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	fcp_response_length;
	uint32_t	rsvd4;
	/**
	 * DWord 5 can either be the task retry identifier (HLM=0) or
	 * the remote N_Port ID (HLM=1)
	 */
	uint32_t	dword5;
	uint32_t	xri_tag:16,
			rpi:16;
	uint32_t	:2,
			ct:2,
			dnrx:1,
			:3,
			command:8,
			class:3,
			ag:1,
			pu:2,
			conf:1,
			lnk:1,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			remote_xid:16;
	uint32_t	ebde_cnt:4,
			:1,
			app_id_valid:1,
			:1,
			len_loc:2,
			qosd:1,
			wchn:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			sr:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	rsvd12;
	uint32_t	rsvd13;
	uint32_t	rsvd14;
	uint32_t	rsvd15;
#else
#error big endian version not defined
#endif
} sli4_fcp_trsp64_wqe_t;

/**
 * @brief WQE used to create an FCP target send (DATA IN).
 */
typedef struct sli4_fcp_tsend64_wqe_s {
	sli4_bde_t	bde;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	payload_offset_length;
	uint32_t	relative_offset;
	/**
	 * DWord 5 can either be the task retry identifier (HLM=0) or
	 * the remote N_Port ID (HLM=1)
	 */
	uint32_t	dword5;
	uint32_t	xri_tag:16,
			rpi:16;
	uint32_t	dif:2,
			ct:2,
			bs:3,
			:1,
			command:8,
			class:3,
			ar:1,
			pu:2,
			conf:1,
			lnk:1,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			remote_xid:16;
	uint32_t	ebde_cnt:4,
			:1,
			app_id_valid:1,
			:1,
			len_loc:2,
			qosd:1,
			wchn:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			sr:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	fcp_data_transmit_length;

#else
#error big endian version not defined
#endif
	sli4_bde_t	first_data_bde; /* For performance hints */
} sli4_fcp_tsend64_wqe_t;

#define SLI4_IO_CONTINUATION		BIT(0)	/** The XRI associated with this IO is already active */
#define SLI4_IO_AUTO_GOOD_RESPONSE	BIT(1)	/** Automatically generate a good RSP frame */
#define SLI4_IO_NO_ABORT		BIT(2)
#define SLI4_IO_DNRX			BIT(3)	/** Set the DNRX bit because no auto xref rdy buffer is posted */

/* WQE DIF field contents */
#define SLI4_DIF_DISABLED		0
#define SLI4_DIF_PASS_THROUGH		1
#define SLI4_DIF_STRIP			2
#define SLI4_DIF_INSERT			3

/**
 * @brief WQE used to create a general request.
 */
typedef struct sli4_gen_request64_wqe_s {
	sli4_bde_t	bde;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	request_payload_length;
	uint32_t	relative_offset;
	uint32_t	:8,
			df_ctl:8,
			type:8,
			r_ctl:8;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	:2,
			ct:2,
			:4,
			command:8,
			class:3,
			:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	remote_n_port_id:24,
			:8;
	uint32_t	rsvd13;
	uint32_t	rsvd14;
	uint32_t	max_response_payload_length;
#else
#error big endian version not defined
#endif
} sli4_gen_request64_wqe_t;

/**
 * @brief WQE used to create a send frame request.
 */
typedef struct sli4_send_frame_wqe_s {
	sli4_bde_t	bde;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	frame_length;
	uint32_t	fc_header_0_1[2];
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	:2,
			ct:2,
			:4,
			command:8,
			class:3,
			:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			eof:8,
			sof:8;
	uint32_t	ebde_cnt:4,
			:3,
			lenloc:2,
			qosd:1,
			wchn:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	fc_header_2_5[4];
#else
#error big endian version not defined
#endif
} sli4_send_frame_wqe_t;

/**
 * @brief WQE used to create a transmit sequence.
 */
typedef struct sli4_xmit_sequence64_wqe_s {
	sli4_bde_t	bde;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	remote_n_port_id:24,
			:8;
	uint32_t	relative_offset;
	uint32_t	:2,
			si:1,
			ft:1,
			:2,
			xo:1,
			ls:1,
			df_ctl:8,
			type:8,
			r_ctl:8;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	dif:2,
			ct:2,
			bs:3,
			:1,
			command:8,
			class:3,
			:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			remote_xid:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			sr:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	sequence_payload_len;
	uint32_t	rsvd13;
	uint32_t	rsvd14;
	uint32_t	rsvd15;
#else
#error big endian version not defined
#endif
} sli4_xmit_sequence64_wqe_t;

/**
 * @brief WQE used unblock the specified XRI and to release it to the SLI Port's free pool.
 */
typedef struct sli4_requeue_xri_wqe_s {
	uint32_t	rsvd0;
	uint32_t	rsvd1;
	uint32_t	rsvd2;
	uint32_t	rsvd3;
	uint32_t	rsvd4;
	uint32_t	rsvd5;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	:2,
			ct:2,
			:4,
			command:8,
			class:3,
			:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	rsvd8;
	uint32_t	request_tag:16,
			:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			wchn:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	rsvd12;
	uint32_t	rsvd13;
	uint32_t	rsvd14;
	uint32_t	rsvd15;
#else
#error big endian version not defined
#endif
} sli4_requeue_xri_wqe_t;

/**
 * @brief WQE used to send a single frame sequence to broadcast address
 */
typedef struct sli4_xmit_bcast64_wqe_s {
	sli4_bde_t	sequence_payload;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	sequence_payload_length;
	uint32_t	rsvd4;
	uint32_t	:8,
			df_ctl:8,
			type:8,
			r_ctl:8;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	:2,
			ct:2,
			:4,
			command:8,
			class:3,
			:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			temporary_rpi:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	rsvd12;
	uint32_t	rsvd13;
	uint32_t	rsvd14;
	uint32_t	rsvd15;
#else
#error big endian version not defined
#endif
} sli4_xmit_bcast64_wqe_t;

/**
 * @brief WQE used to create a BLS response.
 */
typedef struct sli4_xmit_bls_rsp_wqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	payload_word0;
	uint32_t	rx_id:16,
			ox_id:16;
	uint32_t	high_seq_cnt:16,
			low_seq_cnt:16;
	uint32_t	rsvd3;
	uint32_t	local_n_port_id:24,
			:8;
	uint32_t	remote_id:24,
			:6,
			ar:1,
			xo:1;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	:2,
			ct:2,
			:4,
			command:8,
			class:3,
			:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	temporary_rpi:16,
			:16;
	uint32_t	rsvd13;
	uint32_t	rsvd14;
	uint32_t	rsvd15;
#else
#error big endian version not defined
#endif
} sli4_xmit_bls_rsp_wqe_t;

typedef enum {
	SLI_BLS_ACC,
	SLI_BLS_RJT,
	SLI_BLS_MAX
} sli_bls_type_e;

typedef struct sli_bls_payload_s {
	sli_bls_type_e	type;
	uint16_t	ox_id;
	uint16_t	rx_id;
	union {
		struct {
			uint32_t	seq_id_validity:8,
					seq_id_last:8,
					:16;
			uint16_t	ox_id;
			uint16_t	rx_id;
			uint16_t	low_seq_cnt;
			uint16_t	high_seq_cnt;
		} acc;
		struct {
			uint32_t	vendor_unique:8,
					reason_explanation:8,
					reason_code:8,
					:8;
		} rjt;
	} u;
} sli_bls_payload_t;

/**
 * @brief WQE used to create an ELS response.
 */
typedef struct sli4_xmit_els_rsp64_wqe_s {
	sli4_bde_t	els_response_payload;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	els_response_payload_length;
	uint32_t	s_id:24,
			sp:1,
			:7;
	uint32_t	remote_id:24,
			:8;
	uint32_t	xri_tag:16,
			context_tag:16;
	uint32_t	:2,
			ct:2,
			:4,
			command:8,
			class:3,
			:1,
			pu:2,
			:2,
			timer:8;
	uint32_t	abort_tag;
	uint32_t	request_tag:16,
			ox_id:16;
	uint32_t	ebde_cnt:4,
			:3,
			len_loc:2,
			qosd:1,
			:1,
			xbl:1,
			hlm:1,
			iod:1,
			dbde:1,
			wqes:1,
			pri:3,
			pv:1,
			eat:1,
			xc:1,
			:1,
			ccpe:1,
			ccp:8;
	uint32_t	cmd_type:4,
			:3,
			wqec:1,
			:8,
			cq_id:16;
	uint32_t	temporary_rpi:16,
			:16;
	uint32_t	rsvd13;
	uint32_t	rsvd14;
	uint32_t	rsvd15;
#else
#error big endian version not defined
#endif
} sli4_xmit_els_rsp64_wqe_t;

/**
 * @brief Asynchronouse Event: Link State ACQE.
 */
typedef struct sli4_link_state_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	link_number:6,
			link_type:2,
			port_link_status:8,
			port_duplex:8,
			port_speed:8;
	uint32_t	port_fault:8,
			:8,
			logical_link_speed:16;
	uint32_t	event_tag;
	uint32_t	:8,
			event_code:8,
			event_type:8,	/** values are protocol specific */
			:6,
			ae:1,		/** async event - this is an ACQE */
			val:1;		/** valid - contents of CQE are valid */
#else
#error big endian version not defined
#endif
} sli4_link_state_t;


#define SLI4_LINK_ATTN_TYPE_LINK_UP		0x01
#define SLI4_LINK_ATTN_TYPE_LINK_DOWN		0x02
#define SLI4_LINK_ATTN_TYPE_NO_HARD_ALPA	0x03

#define SLI4_LINK_ATTN_P2P			0x01
#define SLI4_LINK_ATTN_FC_AL			0x02
#define SLI4_LINK_ATTN_INTERNAL_LOOPBACK	0x03
#define SLI4_LINK_ATTN_SERDES_LOOPBACK		0x04

#define SLI4_LINK_ATTN_1G			0x01
#define SLI4_LINK_ATTN_2G			0x02
#define SLI4_LINK_ATTN_4G			0x04
#define SLI4_LINK_ATTN_8G			0x08
#define SLI4_LINK_ATTN_10G			0x0a
#define SLI4_LINK_ATTN_16G			0x10

#define SLI4_LINK_TYPE_ETHERNET			0x0
#define SLI4_LINK_TYPE_FC			0x1

/**
 * @brief Asynchronouse Event: FC Link Attention Event.
 */
typedef struct sli4_link_attention_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	link_number:8,
			attn_type:8,
			topology:8,
			port_speed:8;
	uint32_t	port_fault:8,
			shared_link_status:8,
			logical_link_speed:16;
	uint32_t	event_tag;
	uint32_t	:8,
			event_code:8,
			event_type:8,	/** values are protocol specific */
			:6,
			ae:1,		/** async event - this is an ACQE */
			val:1;		/** valid - contents of CQE are valid */
#else
#error big endian version not defined
#endif
} sli4_link_attention_t;

/**
 * @brief FC/FCoE event types.
 */
#define SLI4_LINK_STATE_PHYSICAL		0x00
#define SLI4_LINK_STATE_LOGICAL			0x01

#define SLI4_FCOE_FIP_FCF_DISCOVERED		0x01
#define SLI4_FCOE_FIP_FCF_TABLE_FULL		0x02
#define SLI4_FCOE_FIP_FCF_DEAD			0x03
#define SLI4_FCOE_FIP_FCF_CLEAR_VLINK		0x04
#define SLI4_FCOE_FIP_FCF_MODIFIED		0x05

#define SLI4_GRP5_QOS_SPEED			0x01

#define SLI4_FC_EVENT_LINK_ATTENTION		0x01
#define SLI4_FC_EVENT_SHARED_LINK_ATTENTION	0x02

#define SLI4_PORT_SPEED_NO_LINK			0x0
#define SLI4_PORT_SPEED_10_MBPS			0x1
#define SLI4_PORT_SPEED_100_MBPS		0x2
#define SLI4_PORT_SPEED_1_GBPS			0x3
#define SLI4_PORT_SPEED_10_GBPS			0x4

#define SLI4_PORT_DUPLEX_NONE			0x0
#define SLI4_PORT_DUPLEX_HWF			0x1
#define SLI4_PORT_DUPLEX_FULL			0x2

#define SLI4_PORT_LINK_STATUS_PHYSICAL_DOWN	0x0
#define SLI4_PORT_LINK_STATUS_PHYSICAL_UP	0x1
#define SLI4_PORT_LINK_STATUS_LOGICAL_DOWN	0x2
#define SLI4_PORT_LINK_STATUS_LOGICAL_UP	0x3

/**
 * @brief Asynchronouse Event: FCoE/FIP ACQE.
 */
typedef struct sli4_fcoe_fip_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	event_information;
	uint32_t	fcf_count:16,
			fcoe_event_type:16;
	uint32_t	event_tag;
	uint32_t	:8,
			event_code:8,
			event_type:8,	/** values are protocol specific */
			:6,
			ae:1,		/** async event - this is an ACQE */
			val:1;		/** valid - contents of CQE are valid */
#else
#error big endian version not defined
#endif
} sli4_fcoe_fip_t;

/**
 * @brief FC/FCoE WQ completion queue entry.
 */
typedef struct sli4_fc_wcqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	hw_status:8,
			status:8,
			request_tag:16;
	uint32_t	wqe_specific_1;
	uint32_t	wqe_specific_2;
	uint32_t	:15,
			qx:1,
			code:8,
			pri:3,
			pv:1,
			xb:1,
			:2,
			vld:1;
#else
#error big endian version not defined
#endif
} sli4_fc_wcqe_t;

/**
 * @brief FC/FCoE WQ consumed CQ queue entry.
 */
typedef struct sli4_fc_wqec_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:32;
	uint32_t	:32;
	uint32_t	wqe_index:16,
			wq_id:16;
	uint32_t	:16,
			code:8,
			:7,
			vld:1;
#else
#error big endian version not defined
#endif
} sli4_fc_wqec_t;

/**
 * @brief FC/FCoE Completion Status Codes.
 */
#define SLI4_FC_WCQE_STATUS_SUCCESS		0x00
#define SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE	0x01
#define SLI4_FC_WCQE_STATUS_REMOTE_STOP		0x02
#define SLI4_FC_WCQE_STATUS_LOCAL_REJECT	0x03
#define SLI4_FC_WCQE_STATUS_NPORT_RJT		0x04
#define SLI4_FC_WCQE_STATUS_FABRIC_RJT		0x05
#define SLI4_FC_WCQE_STATUS_NPORT_BSY		0x06
#define SLI4_FC_WCQE_STATUS_FABRIC_BSY		0x07
#define SLI4_FC_WCQE_STATUS_LS_RJT		0x09
#define SLI4_FC_WCQE_STATUS_CMD_REJECT		0x0b
#define SLI4_FC_WCQE_STATUS_FCP_TGT_LENCHECK	0x0c
#define SLI4_FC_WCQE_STATUS_RQ_BUF_LEN_EXCEEDED	0x11
#define SLI4_FC_WCQE_STATUS_RQ_INSUFF_BUF_NEEDED 0x12
#define SLI4_FC_WCQE_STATUS_RQ_INSUFF_FRM_DISC	0x13
#define SLI4_FC_WCQE_STATUS_RQ_DMA_FAILURE	0x14
#define SLI4_FC_WCQE_STATUS_FCP_RSP_TRUNCATE	0x15
#define SLI4_FC_WCQE_STATUS_DI_ERROR		0x16
#define SLI4_FC_WCQE_STATUS_BA_RJT		0x17
#define SLI4_FC_WCQE_STATUS_RQ_INSUFF_XRI_NEEDED 0x18
#define SLI4_FC_WCQE_STATUS_RQ_INSUFF_XRI_DISC	0x19
#define SLI4_FC_WCQE_STATUS_RX_ERROR_DETECT	0x1a
#define SLI4_FC_WCQE_STATUS_RX_ABORT_REQUEST	0x1b

/* driver generated status codes; better not overlap with chip's status codes! */
#define SLI4_FC_WCQE_STATUS_TARGET_WQE_TIMEOUT  0xff
#define SLI4_FC_WCQE_STATUS_SHUTDOWN		0xfe
#define SLI4_FC_WCQE_STATUS_DISPATCH_ERROR	0xfd

/**
 * @brief DI_ERROR Extended Status
 */
#define SLI4_FC_DI_ERROR_GE	(1 << 0) /* Guard Error */
#define SLI4_FC_DI_ERROR_AE	(1 << 1) /* Application Tag Error */
#define SLI4_FC_DI_ERROR_RE	(1 << 2) /* Reference Tag Error */
#define SLI4_FC_DI_ERROR_TDPV	(1 << 3) /* Total Data Placed Valid */
#define SLI4_FC_DI_ERROR_UDB	(1 << 4) /* Uninitialized DIF Block */
#define SLI4_FC_DI_ERROR_EDIR   (1 << 5) /* Error direction */

/**
 * @brief Local Reject Reason Codes.
 */
#define SLI4_FC_LOCAL_REJECT_MISSING_CONTINUE	0x01
#define SLI4_FC_LOCAL_REJECT_SEQUENCE_TIMEOUT	0x02
#define SLI4_FC_LOCAL_REJECT_INTERNAL_ERROR	0x03
#define SLI4_FC_LOCAL_REJECT_INVALID_RPI	0x04
#define SLI4_FC_LOCAL_REJECT_NO_XRI		0x05
#define SLI4_FC_LOCAL_REJECT_ILLEGAL_COMMAND	0x06
#define SLI4_FC_LOCAL_REJECT_XCHG_DROPPED	0x07
#define SLI4_FC_LOCAL_REJECT_ILLEGAL_FIELD	0x08
#define SLI4_FC_LOCAL_REJECT_NO_ABORT_MATCH	0x0c
#define SLI4_FC_LOCAL_REJECT_TX_DMA_FAILED	0x0d
#define SLI4_FC_LOCAL_REJECT_RX_DMA_FAILED	0x0e
#define SLI4_FC_LOCAL_REJECT_ILLEGAL_FRAME	0x0f
#define SLI4_FC_LOCAL_REJECT_NO_RESOURCES	0x11
#define SLI4_FC_LOCAL_REJECT_FCP_CONF_FAILURE	0x12
#define SLI4_FC_LOCAL_REJECT_ILLEGAL_LENGTH	0x13
#define SLI4_FC_LOCAL_REJECT_UNSUPPORTED_FEATURE 0x14
#define SLI4_FC_LOCAL_REJECT_ABORT_IN_PROGRESS	0x15
#define SLI4_FC_LOCAL_REJECT_ABORT_REQUESTED	0x16
#define SLI4_FC_LOCAL_REJECT_RCV_BUFFER_TIMEOUT	0x17
#define SLI4_FC_LOCAL_REJECT_LOOP_OPEN_FAILURE	0x18
#define SLI4_FC_LOCAL_REJECT_LINK_DOWN		0x1a
#define SLI4_FC_LOCAL_REJECT_CORRUPTED_DATA	0x1b
#define SLI4_FC_LOCAL_REJECT_CORRUPTED_RPI	0x1c
#define SLI4_FC_LOCAL_REJECT_OUT_OF_ORDER_DATA	0x1d
#define SLI4_FC_LOCAL_REJECT_OUT_OF_ORDER_ACK	0x1e
#define SLI4_FC_LOCAL_REJECT_DUP_FRAME		0x1f
#define SLI4_FC_LOCAL_REJECT_LINK_CONTROL_FRAME	0x20
#define SLI4_FC_LOCAL_REJECT_BAD_HOST_ADDRESS	0x21
#define SLI4_FC_LOCAL_REJECT_MISSING_HDR_BUFFER	0x23
#define SLI4_FC_LOCAL_REJECT_MSEQ_CHAIN_CORRUPTED 0x24
#define SLI4_FC_LOCAL_REJECT_ABORTMULT_REQUESTED 0x25
#define SLI4_FC_LOCAL_REJECT_BUFFER_SHORTAGE	0x28
#define SLI4_FC_LOCAL_REJECT_RCV_XRIBUF_WAITING	0x29
#define SLI4_FC_LOCAL_REJECT_INVALID_VPI	0x2e
#define SLI4_FC_LOCAL_REJECT_MISSING_XRIBUF	0x30
#define SLI4_FC_LOCAL_REJECT_INVALID_RELOFFSET	0x40
#define SLI4_FC_LOCAL_REJECT_MISSING_RELOFFSET	0x41
#define SLI4_FC_LOCAL_REJECT_INSUFF_BUFFERSPACE	0x42
#define SLI4_FC_LOCAL_REJECT_MISSING_SI		0x43
#define SLI4_FC_LOCAL_REJECT_MISSING_ES		0x44
#define SLI4_FC_LOCAL_REJECT_INCOMPLETE_XFER	0x45
#define SLI4_FC_LOCAL_REJECT_SLER_FAILURE	0x46
#define SLI4_FC_LOCAL_REJECT_SLER_CMD_RCV_FAILURE 0x47
#define SLI4_FC_LOCAL_REJECT_SLER_REC_RJT_ERR	0x48
#define SLI4_FC_LOCAL_REJECT_SLER_REC_SRR_RETRY_ERR 0x49
#define SLI4_FC_LOCAL_REJECT_SLER_SRR_RJT_ERR	0x4a
#define SLI4_FC_LOCAL_REJECT_SLER_RRQ_RJT_ERR	0x4c
#define SLI4_FC_LOCAL_REJECT_SLER_RRQ_RETRY_ERR	0x4d
#define SLI4_FC_LOCAL_REJECT_SLER_ABTS_ERR	0x4e

typedef struct sli4_fc_async_rcqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:8,
			status:8,
			rq_element_index:12,
			:4;
	uint32_t	rsvd1;
	uint32_t	fcfi:6,
			rq_id:10,
			payload_data_placement_length:16;
	uint32_t	sof_byte:8,
			eof_byte:8,
			code:8,
			header_data_placement_length:6,
			:1,
			vld:1;
#else
#error big endian version not defined
#endif
} sli4_fc_async_rcqe_t;

typedef struct sli4_fc_async_rcqe_v1_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:8,
			status:8,
			rq_element_index:12,
			:4;
	uint32_t	fcfi:6,
			:26;
	uint32_t	rq_id:16,
			payload_data_placement_length:16;
	uint32_t	sof_byte:8,
			eof_byte:8,
			code:8,
			header_data_placement_length:6,
			:1,
			vld:1;
#else
#error big endian version not defined
#endif
} sli4_fc_async_rcqe_v1_t;

#define SLI4_FC_ASYNC_RQ_SUCCESS		0x10
#define SLI4_FC_ASYNC_RQ_BUF_LEN_EXCEEDED	0x11
#define SLI4_FC_ASYNC_RQ_INSUFF_BUF_NEEDED	0x12
#define SLI4_FC_ASYNC_RQ_INSUFF_BUF_FRM_DISC	0x13
#define SLI4_FC_ASYNC_RQ_DMA_FAILURE		0x14

typedef struct sli4_fc_coalescing_rcqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:8,
			status:8,
			rq_element_index:12,
			:4;
	uint32_t	rsvd1;
	uint32_t	rq_id:16,
			sequence_reporting_placement_length:16;
	uint32_t	:16,
			code:8,
			:7,
			vld:1;
#else
#error big endian version not defined
#endif
} sli4_fc_coalescing_rcqe_t;

#define SLI4_FC_COALESCE_RQ_SUCCESS		0x10
#define SLI4_FC_COALESCE_RQ_INSUFF_XRI_NEEDED	0x18

typedef struct sli4_fc_optimized_write_cmd_cqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:8,
			status:8,
			rq_element_index:15,
			iv:1;
	uint32_t	fcfi:6,
			:8,
			oox:1,
			agxr:1,
			xri:16;
	uint32_t	rq_id:16,
			payload_data_placement_length:16;
	uint32_t	rpi:16,
			code:8,
			header_data_placement_length:6,
			:1,
			vld:1;
#else
#error big endian version not defined
#endif
} sli4_fc_optimized_write_cmd_cqe_t;

typedef struct sli4_fc_optimized_write_data_cqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	hw_status:8,
			status:8,
			xri:16;
	uint32_t	total_data_placed;
	uint32_t	extended_status;
	uint32_t	:16,
			code:8,
			pri:3,
			pv:1,
			xb:1,
			rha:1,
			:1,
			vld:1;
#else
#error big endian version not defined
#endif
} sli4_fc_optimized_write_data_cqe_t;

typedef struct sli4_fc_xri_aborted_cqe_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	:8,
			status:8,
			:16;
	uint32_t	extended_status;
	uint32_t	xri:16,
			remote_xid:16;
	uint32_t	:16,
			code:8,
			xr:1,
			:3,
			eo:1,
			br:1,
			ia:1,
			vld:1;
#else
#error big endian version not defined
#endif
} sli4_fc_xri_aborted_cqe_t;

/**
 * Code definitions applicable to all FC/FCoE CQE types.
 */
#define SLI4_CQE_CODE_OFFSET		14

#define SLI4_CQE_CODE_WORK_REQUEST_COMPLETION	0x01
#define SLI4_CQE_CODE_RELEASE_WQE		0x02
#define SLI4_CQE_CODE_RQ_ASYNC			0x04
#define SLI4_CQE_CODE_XRI_ABORTED		0x05
#define SLI4_CQE_CODE_RQ_COALESCING		0x06
#define SLI4_CQE_CODE_RQ_CONSUMPTION		0x07
#define SLI4_CQE_CODE_MEASUREMENT_REPORTING	0x08
#define SLI4_CQE_CODE_RQ_ASYNC_V1		0x09
#define SLI4_CQE_CODE_OPTIMIZED_WRITE_CMD	0x0B
#define SLI4_CQE_CODE_OPTIMIZED_WRITE_DATA	0x0C

extern int32_t sli_fc_process_link_state(sli4_t *, void *);
extern int32_t sli_fc_process_link_attention(sli4_t *, void *);
extern int32_t sli_fc_cqe_parse(sli4_t *, sli4_queue_t *, uint8_t *, sli4_qentry_e *, uint16_t *);
extern uint32_t sli_fc_response_length(sli4_t *, uint8_t *);
extern uint32_t sli_fc_io_length(sli4_t *, uint8_t *);
extern int32_t sli_fc_els_did(sli4_t *, uint8_t *, uint32_t *);
extern uint32_t sli_fc_ext_status(sli4_t *, uint8_t *);
extern int32_t sli_fc_rqe_rqid_and_index(sli4_t *, uint8_t *, uint16_t *, uint32_t *);
extern int32_t sli_fc_process_fcoe(sli4_t *, void *);
extern int32_t sli_cmd_fcoe_wq_create(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t, uint16_t);
extern int32_t sli_cmd_fcoe_wq_create_v1(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t, uint16_t);
extern int32_t sli_cmd_fcoe_wq_destroy(sli4_t *, void *, size_t, uint16_t);
extern int32_t sli_cmd_fcoe_post_sgl_pages(sli4_t *, void *, size_t, uint16_t, uint32_t, ocs_dma_t **, ocs_dma_t **,
ocs_dma_t *);
extern int32_t sli_cmd_fcoe_rq_create(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t, uint16_t, uint16_t);
extern int32_t sli_cmd_fcoe_rq_create_v1(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t, uint16_t, uint16_t);
extern int32_t sli_cmd_fcoe_rq_destroy(sli4_t *, void *, size_t, uint16_t);
extern int32_t sli_cmd_fcoe_read_fcf_table(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t);
extern int32_t sli_cmd_fcoe_post_hdr_templates(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t, ocs_dma_t *);
extern int32_t sli_cmd_fcoe_rediscover_fcf(sli4_t *, void *, size_t, uint16_t);
extern int32_t sli_fc_rq_alloc(sli4_t *, sli4_queue_t *, uint32_t, uint32_t, sli4_queue_t *, uint16_t, uint8_t);
extern int32_t sli_fc_rq_set_alloc(sli4_t *, uint32_t, sli4_queue_t *[], uint32_t, uint32_t, uint32_t, uint32_t, uint16_t);
extern uint32_t sli_fc_get_rpi_requirements(sli4_t *, uint32_t);
extern int32_t sli_abort_wqe(sli4_t *, void *, size_t, sli4_abort_type_e, uint32_t, uint32_t, uint32_t, uint16_t, uint16_t);

extern int32_t sli_els_request64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint8_t, uint32_t, uint32_t, uint8_t, uint16_t, uint16_t, uint16_t, ocs_remote_node_t *);
extern int32_t sli_fcp_iread64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t, ocs_remote_node_t *, uint8_t, uint8_t, uint8_t);
extern int32_t sli_fcp_iwrite64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint32_t, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t, ocs_remote_node_t *, uint8_t, uint8_t, uint8_t);
extern int32_t sli_fcp_icmnd64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t, uint16_t, uint16_t, uint32_t, ocs_remote_node_t *, uint8_t);

extern int32_t sli_fcp_treceive64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint32_t, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t, ocs_remote_node_t *, uint32_t, uint8_t, uint8_t, uint8_t, uint32_t);
extern int32_t sli_fcp_trsp64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint32_t, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t, ocs_remote_node_t *, uint32_t, uint8_t, uint8_t, uint32_t);
extern int32_t sli_fcp_tsend64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint32_t, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t, ocs_remote_node_t *, uint32_t, uint8_t, uint8_t, uint8_t, uint32_t);
extern int32_t sli_fcp_cont_treceive64_wqe(sli4_t *, void*, size_t, ocs_dma_t *, uint32_t, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t, ocs_remote_node_t *, uint32_t, uint8_t, uint8_t, uint8_t, uint32_t);
extern int32_t sli_gen_request64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint32_t, uint32_t,uint8_t, uint16_t, uint16_t, uint16_t, ocs_remote_node_t *, uint8_t, uint8_t, uint8_t);
extern int32_t sli_send_frame_wqe(sli4_t *sli4, void *buf, size_t size, uint8_t sof, uint8_t eof, uint32_t *hdr,
				  ocs_dma_t *payload, uint32_t req_len, uint8_t timeout,
				  uint16_t xri, uint16_t req_tag);
extern int32_t sli_xmit_sequence64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint32_t, uint8_t, uint16_t, uint16_t, uint16_t, ocs_remote_node_t *, uint8_t, uint8_t, uint8_t);
extern int32_t sli_xmit_bcast64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint32_t, uint8_t, uint16_t, uint16_t, uint16_t, ocs_remote_node_t *, uint8_t, uint8_t, uint8_t);
extern int32_t sli_xmit_bls_rsp64_wqe(sli4_t *, void *, size_t, sli_bls_payload_t *, uint16_t, uint16_t, uint16_t, ocs_remote_node_t *, uint32_t);
extern int32_t sli_xmit_els_rsp64_wqe(sli4_t *, void *, size_t, ocs_dma_t *, uint32_t, uint16_t, uint16_t, uint16_t, uint16_t, ocs_remote_node_t *, uint32_t, uint32_t);
extern int32_t sli_requeue_xri_wqe(sli4_t *, void *, size_t, uint16_t, uint16_t, uint16_t);
extern void sli4_cmd_lowlevel_set_watchdog(sli4_t *sli4, void *buf, size_t size, uint16_t timeout);

/**
 * @ingroup sli_fc
 * @brief Retrieve the received header and payload length.
 *
 * @param sli4 SLI context.
 * @param cqe Pointer to the CQ entry.
 * @param len_hdr Pointer where the header length is written.
 * @param len_data Pointer where the payload length is written.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static inline int32_t
sli_fc_rqe_length(sli4_t *sli4, void *cqe, uint32_t *len_hdr, uint32_t *len_data)
{
	sli4_fc_async_rcqe_t	*rcqe = cqe;

	*len_hdr = *len_data = 0;

	if (SLI4_FC_ASYNC_RQ_SUCCESS == rcqe->status) {
		*len_hdr  = rcqe->header_data_placement_length;
		*len_data = rcqe->payload_data_placement_length;
		return 0;
	} else {
		return -1;
	}
}

/**
 * @ingroup sli_fc
 * @brief Retrieve the received FCFI.
 *
 * @param sli4 SLI context.
 * @param cqe Pointer to the CQ entry.
 *
 * @return Returns the FCFI in the CQE. or UINT8_MAX if invalid CQE code.
 */
static inline uint8_t
sli_fc_rqe_fcfi(sli4_t *sli4, void *cqe)
{
	uint8_t code = ((uint8_t*)cqe)[SLI4_CQE_CODE_OFFSET];
	uint8_t fcfi = UINT8_MAX;

	switch(code) {
	case SLI4_CQE_CODE_RQ_ASYNC: {
		sli4_fc_async_rcqe_t *rcqe = cqe;
		fcfi = rcqe->fcfi;
		break;
	}
	case SLI4_CQE_CODE_RQ_ASYNC_V1: {
		sli4_fc_async_rcqe_v1_t *rcqev1 = cqe;
		fcfi = rcqev1->fcfi;
		break;
	}
	case SLI4_CQE_CODE_OPTIMIZED_WRITE_CMD: {
		sli4_fc_optimized_write_cmd_cqe_t *opt_wr = cqe;
		fcfi = opt_wr->fcfi;
		break;
	}
	}

	return fcfi;
}

extern const char *sli_fc_get_status_string(uint32_t status);
 
#endif /* !_SLI4_H */

