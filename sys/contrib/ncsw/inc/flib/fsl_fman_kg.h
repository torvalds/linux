/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FSL_FMAN_KG_H
#define __FSL_FMAN_KG_H

#include "common/general.h"

#define FM_KG_NUM_OF_GENERIC_REGS	8 /**< Num of generic KeyGen regs */
#define FMAN_MAX_NUM_OF_HW_PORTS	64
/**< Total num of masks allowed on KG extractions */
#define FM_KG_EXTRACT_MASKS_NUM		4
#define FM_KG_NUM_CLS_PLAN_ENTR		8 /**< Num of class. plan regs */
#define FM_KG_CLS_PLAN_GRPS_NUM		32 /**< Max num of class. groups */

struct fman_kg_regs {
	uint32_t fmkg_gcr;
	uint32_t res004;
	uint32_t res008;
	uint32_t fmkg_eer;
	uint32_t fmkg_eeer;
	uint32_t res014;
	uint32_t res018;
	uint32_t fmkg_seer;
	uint32_t fmkg_seeer;
	uint32_t fmkg_gsr;
	uint32_t fmkg_tpc;
	uint32_t fmkg_serc;
	uint32_t res030[4];
	uint32_t fmkg_fdor;
	uint32_t fmkg_gdv0r;
	uint32_t fmkg_gdv1r;
	uint32_t res04c[6];
	uint32_t fmkg_feer;
	uint32_t res068[38];
	uint32_t fmkg_indirect[63];
	uint32_t fmkg_ar;
};

struct fman_kg_scheme_regs {
	uint32_t kgse_mode; /**< MODE */
	uint32_t kgse_ekfc; /**< Extract Known Fields Command */
	uint32_t kgse_ekdv; /**< Extract Known Default Value */
	uint32_t kgse_bmch; /**< Bit Mask Command High */
	uint32_t kgse_bmcl; /**< Bit Mask Command Low */
	uint32_t kgse_fqb; /**< Frame Queue Base */
	uint32_t kgse_hc; /**< Hash Command */
	uint32_t kgse_ppc; /**< Policer Profile Command */
	uint32_t kgse_gec[FM_KG_NUM_OF_GENERIC_REGS];
				/**< Generic Extract Command */
	uint32_t kgse_spc; /**< KeyGen Scheme Entry Statistic Packet Counter */
	uint32_t kgse_dv0; /**< KeyGen Scheme Entry Default Value 0 */
	uint32_t kgse_dv1; /**< KeyGen Scheme Entry Default Value 1 */
	uint32_t kgse_ccbs; /**< KeyGen Scheme Entry Coarse Classification Bit*/
	uint32_t kgse_mv; /**< KeyGen Scheme Entry Match vector */
	uint32_t kgse_om; /**< KeyGen Scheme Entry Operation Mode bits */
	uint32_t kgse_vsp; /**< KeyGen Scheme Entry Virtual Storage Profile */
};

struct fman_kg_pe_regs{
	uint32_t fmkg_pe_sp;
	uint32_t fmkg_pe_cpp;
};

struct fman_kg_cp_regs {
	uint32_t kgcpe[FM_KG_NUM_CLS_PLAN_ENTR];
};


#define FM_KG_KGAR_GO				0x80000000
#define FM_KG_KGAR_READ				0x40000000
#define FM_KG_KGAR_WRITE			0x00000000
#define FM_KG_KGAR_SEL_SCHEME_ENTRY		0x00000000
#define FM_KG_KGAR_SCM_WSEL_UPDATE_CNT		0x00008000

#define KG_SCH_PP_SHIFT_HIGH			0x80000000
#define KG_SCH_PP_NO_GEN			0x10000000
#define KG_SCH_PP_SHIFT_LOW			0x0000F000
#define KG_SCH_MODE_NIA_PLCR			0x40000000
#define KG_SCH_GEN_EXTRACT_TYPE			0x00008000
#define KG_SCH_BITMASK_MASK			0x000000FF
#define KG_SCH_GEN_VALID			0x80000000
#define KG_SCH_GEN_MASK				0x00FF0000
#define FM_PCD_KG_KGAR_ERR			0x20000000
#define FM_PCD_KG_KGAR_SEL_CLS_PLAN_ENTRY	0x01000000
#define FM_PCD_KG_KGAR_SEL_PORT_ENTRY		0x02000000
#define FM_PCD_KG_KGAR_SEL_PORT_WSEL_SP		0x00008000
#define FM_PCD_KG_KGAR_SEL_PORT_WSEL_CPP	0x00004000
#define FM_PCD_KG_KGAR_WSEL_MASK		0x0000FF00
#define KG_SCH_HASH_CONFIG_NO_FQID		0x80000000
#define KG_SCH_HASH_CONFIG_SYM			0x40000000

#define FM_EX_KG_DOUBLE_ECC			0x80000000
#define FM_EX_KG_KEYSIZE_OVERFLOW		0x40000000

/* ECC capture register */
#define KG_FMKG_SERC_CAP			0x80000000
#define KG_FMKG_SERC_CET			0x40000000
#define KG_FMKG_SERC_CNT_MSK			0x00FF0000
#define KG_FMKG_SERC_CNT_SHIFT			16
#define KG_FMKG_SERC_ADDR_MSK			0x000003FF

/* Masks */
#define FM_KG_KGGCR_EN				0x80000000
#define KG_SCH_GEN_VALID			0x80000000
#define KG_SCH_GEN_EXTRACT_TYPE			0x00008000
#define KG_ERR_TYPE_DOUBLE			0x40000000
#define KG_ERR_ADDR_MASK			0x00000FFF
#define KG_SCH_MODE_EN				0x80000000

/* shifts */
#define FM_KG_KGAR_NUM_SHIFT			16
#define FM_KG_PE_CPP_MASK_SHIFT			16
#define FM_KG_KGAR_WSEL_SHIFT			8

#define FM_KG_SCH_GEN_HT_INVALID		0

#define FM_KG_MASK_SEL_GEN_BASE			0x20

#define KG_GET_MASK_SEL_SHIFT(shift, i)	\
switch (i)				\
{					\
	case 0: (shift) = 26; break;	\
	case 1: (shift) = 20; break;	\
	case 2: (shift) = 10; break;	\
	case 3: (shift) = 4; break;	\
	default: (shift) = 0;		\
}

#define KG_GET_MASK_OFFSET_SHIFT(shift, i)	\
switch (i)				\
{					\
	case 0: (shift) = 16; break;	\
	case 1: (shift) = 0; break;	\
	case 2: (shift) = 28; break;	\
	case 3: (shift) = 24; break;	\
	default: (shift) = 0;		\
}

#define KG_GET_MASK_SHIFT(shift, i)	\
switch (i)				\
{					\
	case 0: shift = 24; break;	\
	case 1: shift = 16; break;	\
	case 2: shift = 8;  break;	\
	case 3: shift = 0;  break;	\
	default: shift = 0;		\
}

/* Port entry CPP register */
#define FMAN_KG_PE_CPP_MASK_SHIFT	16

/* Scheme registers */
#define FMAN_KG_SCH_MODE_EN		0x80000000
#define FMAN_KG_SCH_MODE_NIA_PLCR	0x40000000
#define FMAN_KG_SCH_MODE_CCOBASE_SHIFT	24

#define FMAN_KG_SCH_DEF_MAC_ADDR_SHIFT	30
#define FMAN_KG_SCH_DEF_VLAN_TCI_SHIFT	28
#define FMAN_KG_SCH_DEF_ETYPE_SHIFT	26
#define FMAN_KG_SCH_DEF_PPP_SID_SHIFT	24
#define FMAN_KG_SCH_DEF_PPP_PID_SHIFT	22
#define FMAN_KG_SCH_DEF_MPLS_SHIFT	20
#define FMAN_KG_SCH_DEF_IP_ADDR_SHIFT	18
#define FMAN_KG_SCH_DEF_PTYPE_SHIFT	16
#define FMAN_KG_SCH_DEF_IP_TOS_TC_SHIFT	14
#define FMAN_KG_SCH_DEF_IPv6_FL_SHIFT	12
#define FMAN_KG_SCH_DEF_IPSEC_SPI_SHIFT	10
#define FMAN_KG_SCH_DEF_L4_PORT_SHIFT	8
#define FMAN_KG_SCH_DEF_TCP_FLG_SHIFT	6

#define FMAN_KG_SCH_GEN_VALID		0x80000000
#define FMAN_KG_SCH_GEN_SIZE_MAX	16
#define FMAN_KG_SCH_GEN_OR		0x00008000

#define FMAN_KG_SCH_GEN_DEF_SHIFT	29
#define FMAN_KG_SCH_GEN_SIZE_SHIFT	24
#define FMAN_KG_SCH_GEN_MASK_SHIFT	16
#define FMAN_KG_SCH_GEN_HT_SHIFT	8

#define FMAN_KG_SCH_HASH_HSHIFT_SHIFT	24
#define FMAN_KG_SCH_HASH_HSHIFT_MAX	0x28
#define FMAN_KG_SCH_HASH_SYM		0x40000000
#define FMAN_KG_SCH_HASH_NO_FQID_GEN	0x80000000

#define FMAN_KG_SCH_PP_SH_SHIFT		27
#define FMAN_KG_SCH_PP_SL_SHIFT		12
#define FMAN_KG_SCH_PP_SH_MASK		0x80000000
#define FMAN_KG_SCH_PP_SL_MASK		0x0000F000
#define FMAN_KG_SCH_PP_SHIFT_MAX	0x17
#define FMAN_KG_SCH_PP_MASK_SHIFT	16
#define FMAN_KG_SCH_PP_NO_GEN		0x10000000

enum fman_kg_gen_extract_src {
	E_FMAN_KG_GEN_EXTRACT_ETH,
	E_FMAN_KG_GEN_EXTRACT_ETYPE,
	E_FMAN_KG_GEN_EXTRACT_SNAP,
	E_FMAN_KG_GEN_EXTRACT_VLAN_TCI_1,
	E_FMAN_KG_GEN_EXTRACT_VLAN_TCI_N,
	E_FMAN_KG_GEN_EXTRACT_PPPoE,
	E_FMAN_KG_GEN_EXTRACT_MPLS_1,
	E_FMAN_KG_GEN_EXTRACT_MPLS_2,
	E_FMAN_KG_GEN_EXTRACT_MPLS_3,
	E_FMAN_KG_GEN_EXTRACT_MPLS_N,
	E_FMAN_KG_GEN_EXTRACT_IPv4_1,
	E_FMAN_KG_GEN_EXTRACT_IPv6_1,
	E_FMAN_KG_GEN_EXTRACT_IPv4_2,
	E_FMAN_KG_GEN_EXTRACT_IPv6_2,
	E_FMAN_KG_GEN_EXTRACT_MINENCAP,
	E_FMAN_KG_GEN_EXTRACT_IP_PID,
	E_FMAN_KG_GEN_EXTRACT_GRE,
	E_FMAN_KG_GEN_EXTRACT_TCP,
	E_FMAN_KG_GEN_EXTRACT_UDP,
	E_FMAN_KG_GEN_EXTRACT_SCTP,
	E_FMAN_KG_GEN_EXTRACT_DCCP,
	E_FMAN_KG_GEN_EXTRACT_IPSEC_AH,
	E_FMAN_KG_GEN_EXTRACT_IPSEC_ESP,
	E_FMAN_KG_GEN_EXTRACT_SHIM_1,
	E_FMAN_KG_GEN_EXTRACT_SHIM_2,
	E_FMAN_KG_GEN_EXTRACT_FROM_DFLT,
	E_FMAN_KG_GEN_EXTRACT_FROM_FRAME_START,
	E_FMAN_KG_GEN_EXTRACT_FROM_PARSE_RESULT,
	E_FMAN_KG_GEN_EXTRACT_FROM_END_OF_PARSE,
	E_FMAN_KG_GEN_EXTRACT_FROM_FQID
};

struct fman_kg_ex_ecc_attr
{
	bool		valid;
	bool		double_ecc;
	uint16_t	addr;
	uint8_t		single_ecc_count;
};

enum fman_kg_def_select
{
	E_FMAN_KG_DEF_GLOBAL_0,
	E_FMAN_KG_DEF_GLOBAL_1,
	E_FMAN_KG_DEF_SCHEME_0,
	E_FMAN_KG_DEF_SCHEME_1
};

struct fman_kg_extract_def
{
	enum fman_kg_def_select	mac_addr;
	enum fman_kg_def_select	vlan_tci;
	enum fman_kg_def_select	etype;
	enum fman_kg_def_select	ppp_sid;
	enum fman_kg_def_select	ppp_pid;
	enum fman_kg_def_select	mpls;
	enum fman_kg_def_select	ip_addr;
	enum fman_kg_def_select	ptype;
	enum fman_kg_def_select	ip_tos_tc;
	enum fman_kg_def_select	ipv6_fl;
	enum fman_kg_def_select	ipsec_spi;
	enum fman_kg_def_select	l4_port;
	enum fman_kg_def_select	tcp_flg;
};

enum fman_kg_gen_extract_type
{
	E_FMAN_KG_HASH_EXTRACT,
	E_FMAN_KG_OR_EXTRACT
};

struct fman_kg_gen_extract_params
{
	/* Hash or Or-ed extract */
	enum fman_kg_gen_extract_type	type;
	enum fman_kg_gen_extract_src	src;
	bool				no_validation;
	/* Extraction offset from the header location specified above */
	uint8_t				offset;
	/* Size of extraction for FMAN_KG_HASH_EXTRACT,
	 * hash result shift for FMAN_KG_OR_EXTRACT */
	uint8_t				extract;
	uint8_t				mask;
	/* Default value to use when header specified
	 * by fman_kg_gen_extract_src doesn't present */
	enum fman_kg_def_select		def_val;
};

struct fman_kg_extract_mask
{
	/**< Indication if mask is on known field extraction or
	 * on general extraction; TRUE for known field */
	bool		is_known;
	/**< One of FMAN_KG_EXTRACT_xxx defines for known fields mask and
	 * generic register index for generic extracts mask */
	uint32_t	field_or_gen_idx;
	/**< Byte offset from start of the extracted data specified
	 * by field_or_gen_idx */
	uint8_t		offset;
	/**< Byte mask (selected bits will be used) */
	uint8_t		mask;
};

struct fman_kg_extract_params
{
	/* Or-ed mask of FMAN_KG_EXTRACT_xxx defines */
	uint32_t				known_fields;
	struct fman_kg_extract_def		known_fields_def;
	/* Number of entries in gen_extract */
	uint8_t					gen_extract_num;
	struct fman_kg_gen_extract_params	gen_extract[FM_KG_NUM_OF_GENERIC_REGS];
	/* Number of entries in masks */
	uint8_t					masks_num;
	struct fman_kg_extract_mask		masks[FM_KG_EXTRACT_MASKS_NUM];
	uint32_t				def_scheme_0;
	uint32_t				def_scheme_1;
};

struct fman_kg_hash_params
{
	bool		use_hash;
	uint8_t		shift_r;
	uint32_t	mask; /**< 24-bit mask */
	bool		sym; /**< Symmetric hash for src and dest pairs */
};

struct fman_kg_pp_params
{
	uint8_t		base;
	uint8_t		shift;
	uint8_t		mask;
	bool		bypass_pp_gen;
};

struct fman_kg_cc_params
{
	uint8_t		base_offset;
	uint32_t	qlcv_bits_sel;
};

enum fman_pcd_engine
{
	E_FMAN_PCD_INVALID = 0,	/**< Invalid PCD engine indicated*/
	E_FMAN_PCD_DONE,	/**< No PCD Engine indicated */
	E_FMAN_PCD_KG,		/**< Keygen indicated */
	E_FMAN_PCD_CC,		/**< Coarse classification indicated */
	E_FMAN_PCD_PLCR,	/**< Policer indicated */
	E_FMAN_PCD_PRS		/**< Parser indicated */
};

struct fman_kg_cls_plan_params
{
	uint8_t entries_mask;
	uint32_t mask_vector[FM_KG_NUM_CLS_PLAN_ENTR];
};

struct fman_kg_scheme_params
{
	uint32_t			match_vector;
	struct fman_kg_extract_params	extract_params;
	struct fman_kg_hash_params	hash_params;
	uint32_t			base_fqid;
	/* What we do w/features supported per FM version ?? */
	bool				bypass_fqid_gen;
	struct fman_kg_pp_params	policer_params;
	struct fman_kg_cc_params	cc_params;
	bool				update_counter;
	/**< counter_value: Set scheme counter to the specified value;
	 * relevant only when update_counter = TRUE. */
	uint32_t			counter_value;
	enum fman_pcd_engine		next_engine;
	/**< Next engine action code */
	uint32_t			next_engine_action;
};



int fman_kg_write_ar_wait(struct fman_kg_regs *regs, uint32_t fmkg_ar);
void fman_kg_write_sp(struct fman_kg_regs *regs, uint32_t sp, bool add);
void fman_kg_write_cpp(struct fman_kg_regs *regs, uint32_t cpp);
void fman_kg_get_event(struct fman_kg_regs *regs,
			uint32_t *event,
			uint32_t *scheme_idx);
void fman_kg_init(struct fman_kg_regs *regs,
			uint32_t exceptions,
			uint32_t dflt_nia);
void fman_kg_enable_scheme_interrupts(struct fman_kg_regs *regs);
void fman_kg_enable(struct fman_kg_regs *regs);
void fman_kg_disable(struct fman_kg_regs *regs);
int fman_kg_write_bind_cls_plans(struct fman_kg_regs *regs,
					uint8_t hwport_id,
					uint32_t bind_cls_plans);
int fman_kg_build_bind_cls_plans(uint8_t grp_base,
					uint8_t grp_mask,
					uint32_t *bind_cls_plans);
int fman_kg_write_bind_schemes(struct fman_kg_regs *regs,
				uint8_t hwport_id,
				uint32_t schemes);
int fman_kg_write_cls_plan(struct fman_kg_regs *regs,
				uint8_t grp_id,
				uint8_t entries_mask,
				uint8_t hwport_id,
				struct fman_kg_cp_regs *cls_plan_regs);
int fman_kg_build_cls_plan(struct fman_kg_cls_plan_params *params,
				struct fman_kg_cp_regs *cls_plan_regs);
uint32_t fman_kg_get_schemes_total_counter(struct fman_kg_regs *regs);
int fman_kg_set_scheme_counter(struct fman_kg_regs *regs,
				uint8_t scheme_id,
				uint8_t hwport_id,
				uint32_t counter);
int fman_kg_get_scheme_counter(struct fman_kg_regs *regs,
				uint8_t scheme_id,
				uint8_t hwport_id,
				uint32_t *counter);
int fman_kg_delete_scheme(struct fman_kg_regs *regs,
				uint8_t scheme_id,
				uint8_t hwport_id);
int fman_kg_write_scheme(struct fman_kg_regs *regs,
				uint8_t scheme_id,
				uint8_t hwport_id,
				struct fman_kg_scheme_regs *scheme_regs,
				bool update_counter);
int fman_kg_build_scheme(struct fman_kg_scheme_params *params,
				struct fman_kg_scheme_regs *scheme_regs);
void fman_kg_get_capture(struct fman_kg_regs *regs,
				struct fman_kg_ex_ecc_attr *ecc_attr,
				bool clear);
void fman_kg_get_exception(struct fman_kg_regs *regs,
				uint32_t *events,
				uint32_t *scheme_ids,
				bool clear);
void fman_kg_set_exception(struct fman_kg_regs *regs,
				uint32_t exception,
				bool enable);
void fman_kg_set_dflt_val(struct fman_kg_regs *regs,
				uint8_t def_id,
				uint32_t val);
void fman_kg_set_data_after_prs(struct fman_kg_regs *regs, uint8_t offset);


	
/**************************************************************************//**
  @Description       NIA Description
*//***************************************************************************/
#define KG_NIA_ORDER_RESTOR	0x00800000
#define KG_NIA_ENG_FM_CTL	0x00000000
#define KG_NIA_ENG_PRS		0x00440000
#define KG_NIA_ENG_KG		0x00480000
#define KG_NIA_ENG_PLCR		0x004C0000
#define KG_NIA_ENG_BMI		0x00500000
#define KG_NIA_ENG_QMI_ENQ	0x00540000
#define KG_NIA_ENG_QMI_DEQ	0x00580000
#define KG_NIA_ENG_MASK		0x007C0000

#define KG_NIA_AC_MASK		0x0003FFFF

#define KG_NIA_INVALID		0xFFFFFFFF

static __inline__ uint32_t fm_kg_build_nia(enum fman_pcd_engine next_engine,
					uint32_t next_engine_action)
{
	uint32_t nia;

	if (next_engine_action & ~KG_NIA_AC_MASK)
		return KG_NIA_INVALID;

	switch (next_engine) {
	case E_FMAN_PCD_DONE:
		nia = KG_NIA_ENG_BMI | next_engine_action;
		break;

	case E_FMAN_PCD_KG:
		nia = KG_NIA_ENG_KG | next_engine_action;
		break;

	case E_FMAN_PCD_CC:
		nia = KG_NIA_ENG_FM_CTL | next_engine_action;
		break;

	case E_FMAN_PCD_PLCR:
		nia = KG_NIA_ENG_PLCR | next_engine_action;
		break;

	default:
		nia = KG_NIA_INVALID;
	}

	return nia;
}

#endif /* __FSL_FMAN_KG_H */
