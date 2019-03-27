/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
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

#include "fsl_fman_kg.h"

/****************************************/
/*       static functions               */
/****************************************/


static uint32_t build_ar_bind_scheme(uint8_t hwport_id, bool write)
{
	uint32_t rw;

	rw = write ? (uint32_t)FM_KG_KGAR_WRITE : (uint32_t)FM_KG_KGAR_READ;

	return (uint32_t)(FM_KG_KGAR_GO |
			rw |
			FM_PCD_KG_KGAR_SEL_PORT_ENTRY |
			hwport_id |
			FM_PCD_KG_KGAR_SEL_PORT_WSEL_SP);
}

static void clear_pe_all_scheme(struct fman_kg_regs *regs, uint8_t hwport_id)
{
	uint32_t ar;

	fman_kg_write_sp(regs, 0xffffffff, 0);

	ar = build_ar_bind_scheme(hwport_id, TRUE);
	fman_kg_write_ar_wait(regs, ar);
}

static uint32_t build_ar_bind_cls_plan(uint8_t hwport_id, bool write)
{
	uint32_t rw;

	rw = write ? (uint32_t)FM_KG_KGAR_WRITE : (uint32_t)FM_KG_KGAR_READ;

	return (uint32_t)(FM_KG_KGAR_GO |
			rw |
			FM_PCD_KG_KGAR_SEL_PORT_ENTRY |
			hwport_id |
			FM_PCD_KG_KGAR_SEL_PORT_WSEL_CPP);
}

static void clear_pe_all_cls_plan(struct fman_kg_regs *regs, uint8_t hwport_id)
{
	uint32_t ar;

	fman_kg_write_cpp(regs, 0);

	ar = build_ar_bind_cls_plan(hwport_id, TRUE);
	fman_kg_write_ar_wait(regs, ar);
}

static uint8_t get_gen_ht_code(enum fman_kg_gen_extract_src src,
				bool no_validation,
				uint8_t *offset)
{
	int	code;

	switch (src) {
	case E_FMAN_KG_GEN_EXTRACT_ETH:
		code = no_validation ? 0x73 : 0x3;
		break;

	case E_FMAN_KG_GEN_EXTRACT_ETYPE:
		code = no_validation ? 0x77 : 0x7;
		break;
 
	case E_FMAN_KG_GEN_EXTRACT_SNAP:
		code = no_validation ? 0x74 : 0x4;
		break;

	case E_FMAN_KG_GEN_EXTRACT_VLAN_TCI_1:
		code = no_validation ? 0x75 : 0x5;
		break;

	case E_FMAN_KG_GEN_EXTRACT_VLAN_TCI_N:
		code = no_validation ? 0x76 : 0x6;
		break;

	case E_FMAN_KG_GEN_EXTRACT_PPPoE:
		code = no_validation ? 0x78 : 0x8;
		break;

	case E_FMAN_KG_GEN_EXTRACT_MPLS_1:
		code = no_validation ? 0x79 : 0x9;
		break;

	case E_FMAN_KG_GEN_EXTRACT_MPLS_2:
		code = no_validation ? FM_KG_SCH_GEN_HT_INVALID : 0x19;
		break;

	case E_FMAN_KG_GEN_EXTRACT_MPLS_3:
		code = no_validation ? FM_KG_SCH_GEN_HT_INVALID : 0x29;
		break;

	case E_FMAN_KG_GEN_EXTRACT_MPLS_N:
		code = no_validation ? 0x7a : 0xa;
		break;

	case E_FMAN_KG_GEN_EXTRACT_IPv4_1:
		code = no_validation ? 0x7b : 0xb;
		break;

	case E_FMAN_KG_GEN_EXTRACT_IPv6_1:
		code = no_validation ? 0x7b : 0x1b;
		break;

	case E_FMAN_KG_GEN_EXTRACT_IPv4_2:
		code = no_validation ? 0x7c : 0xc;
		break;

	case E_FMAN_KG_GEN_EXTRACT_IPv6_2:
		code = no_validation ? 0x7c : 0x1c;
		break;

	case E_FMAN_KG_GEN_EXTRACT_MINENCAP:
		code = no_validation ? 0x7c : 0x2c;
		break;

	case E_FMAN_KG_GEN_EXTRACT_IP_PID:
		code = no_validation ? 0x72 : 0x2;
		break;

	case E_FMAN_KG_GEN_EXTRACT_GRE:
		code = no_validation ? 0x7d : 0xd;
		break;

	case E_FMAN_KG_GEN_EXTRACT_TCP:
		code = no_validation ? 0x7e : 0xe;
		break;

	case E_FMAN_KG_GEN_EXTRACT_UDP:
		code = no_validation ? 0x7e : 0x1e;
		break;

	case E_FMAN_KG_GEN_EXTRACT_SCTP:
		code = no_validation ? 0x7e : 0x3e;
		break;

	case E_FMAN_KG_GEN_EXTRACT_DCCP:
		code = no_validation ? 0x7e : 0x4e;
		break;

	case E_FMAN_KG_GEN_EXTRACT_IPSEC_AH:
		code = no_validation ? 0x7e : 0x2e;
		break;

	case E_FMAN_KG_GEN_EXTRACT_IPSEC_ESP:
		code = no_validation ? 0x7e : 0x6e;
		break;

	case E_FMAN_KG_GEN_EXTRACT_SHIM_1:
		code = 0x70;
		break;

	case E_FMAN_KG_GEN_EXTRACT_SHIM_2:
		code = 0x71;
		break;

	case E_FMAN_KG_GEN_EXTRACT_FROM_DFLT:
		code = 0x10;
		break;

	case E_FMAN_KG_GEN_EXTRACT_FROM_FRAME_START:
		code = 0x40;
		break;

	case E_FMAN_KG_GEN_EXTRACT_FROM_PARSE_RESULT:
		code = 0x20;
		break;

	case E_FMAN_KG_GEN_EXTRACT_FROM_END_OF_PARSE:
		code = 0x7f;
		break;

	case E_FMAN_KG_GEN_EXTRACT_FROM_FQID:
		code = 0x20;
		*offset += 0x20;
		break;

	default:
		code = FM_KG_SCH_GEN_HT_INVALID;
	}

	return (uint8_t)code;
}

static uint32_t build_ar_scheme(uint8_t scheme,
				uint8_t hwport_id,
				bool update_counter,
				bool write)
{
	uint32_t rw;

	rw = (uint32_t)(write ? FM_KG_KGAR_WRITE : FM_KG_KGAR_READ);

	return (uint32_t)(FM_KG_KGAR_GO |
			rw |
			FM_KG_KGAR_SEL_SCHEME_ENTRY |
			hwport_id |
			((uint32_t)scheme << FM_KG_KGAR_NUM_SHIFT) |
			(update_counter ? FM_KG_KGAR_SCM_WSEL_UPDATE_CNT : 0));
}

static uint32_t build_ar_cls_plan(uint8_t grp,
					uint8_t entries_mask,
					uint8_t hwport_id,
					bool write)
{
	uint32_t rw;

	rw = (uint32_t)(write ? FM_KG_KGAR_WRITE : FM_KG_KGAR_READ);

	return (uint32_t)(FM_KG_KGAR_GO |
			rw |
			FM_PCD_KG_KGAR_SEL_CLS_PLAN_ENTRY |
			hwport_id |
			((uint32_t)grp << FM_KG_KGAR_NUM_SHIFT) |
			((uint32_t)entries_mask << FM_KG_KGAR_WSEL_SHIFT));
}

int fman_kg_write_ar_wait(struct fman_kg_regs *regs, uint32_t fmkg_ar)
{
	iowrite32be(fmkg_ar, &regs->fmkg_ar);
	/* Wait for GO to be idle and read error */
	while ((fmkg_ar = ioread32be(&regs->fmkg_ar)) & FM_KG_KGAR_GO) ;
	if (fmkg_ar & FM_PCD_KG_KGAR_ERR)
		return -EINVAL;
	return 0;
}

void fman_kg_write_sp(struct fman_kg_regs *regs, uint32_t sp, bool add)
{

	struct fman_kg_pe_regs *kgpe_regs;
	uint32_t tmp;

	kgpe_regs = (struct fman_kg_pe_regs *)&(regs->fmkg_indirect[0]);
	tmp = ioread32be(&kgpe_regs->fmkg_pe_sp);

	if (add)
		tmp |= sp;
	else /* clear */
		tmp &= ~sp;

	iowrite32be(tmp, &kgpe_regs->fmkg_pe_sp);

}

void fman_kg_write_cpp(struct fman_kg_regs *regs, uint32_t cpp)
{
	struct fman_kg_pe_regs *kgpe_regs;

	kgpe_regs = (struct fman_kg_pe_regs *)&(regs->fmkg_indirect[0]);

	iowrite32be(cpp, &kgpe_regs->fmkg_pe_cpp);
}

void fman_kg_get_event(struct fman_kg_regs *regs,
			uint32_t *event,
			uint32_t *scheme_idx)
{
	uint32_t mask, force;

	*event = ioread32be(&regs->fmkg_eer);
	mask = ioread32be(&regs->fmkg_eeer);
	*scheme_idx = ioread32be(&regs->fmkg_seer);
	*scheme_idx &= ioread32be(&regs->fmkg_seeer);

	*event &= mask;

	/* clear the forced events */
	force = ioread32be(&regs->fmkg_feer);
	if (force & *event)
		iowrite32be(force & ~*event ,&regs->fmkg_feer);

	iowrite32be(*event, &regs->fmkg_eer);
	iowrite32be(*scheme_idx, &regs->fmkg_seer);
}


void fman_kg_init(struct fman_kg_regs *regs,
			uint32_t exceptions,
			uint32_t dflt_nia)
{
	uint32_t tmp;
	int i;

	iowrite32be(FM_EX_KG_DOUBLE_ECC | FM_EX_KG_KEYSIZE_OVERFLOW,
			&regs->fmkg_eer);

	tmp = 0;
	if (exceptions & FM_EX_KG_DOUBLE_ECC)
        	tmp |= FM_EX_KG_DOUBLE_ECC;

	if (exceptions & FM_EX_KG_KEYSIZE_OVERFLOW)
		tmp |= FM_EX_KG_KEYSIZE_OVERFLOW;

	iowrite32be(tmp, &regs->fmkg_eeer);
	iowrite32be(0, &regs->fmkg_fdor);
	iowrite32be(0, &regs->fmkg_gdv0r);
	iowrite32be(0, &regs->fmkg_gdv1r);
	iowrite32be(dflt_nia, &regs->fmkg_gcr);

	/* Clear binding between ports to schemes and classification plans
	 * so that all ports are not bound to any scheme/classification plan */
	for (i = 0; i < FMAN_MAX_NUM_OF_HW_PORTS; i++) {
		clear_pe_all_scheme(regs, (uint8_t)i);
		clear_pe_all_cls_plan(regs, (uint8_t)i);
	}
}

void fman_kg_enable_scheme_interrupts(struct fman_kg_regs *regs)
{
	/* enable and enable all scheme interrupts */
	iowrite32be(0xFFFFFFFF, &regs->fmkg_seer);
	iowrite32be(0xFFFFFFFF, &regs->fmkg_seeer);
}

void fman_kg_enable(struct fman_kg_regs *regs)
{
	iowrite32be(ioread32be(&regs->fmkg_gcr) | FM_KG_KGGCR_EN,
			&regs->fmkg_gcr);
}

void fman_kg_disable(struct fman_kg_regs *regs)
{
	iowrite32be(ioread32be(&regs->fmkg_gcr) & ~FM_KG_KGGCR_EN,
			&regs->fmkg_gcr);
}

void fman_kg_set_data_after_prs(struct fman_kg_regs *regs, uint8_t offset)
{
	iowrite32be(offset, &regs->fmkg_fdor);
}

void fman_kg_set_dflt_val(struct fman_kg_regs *regs,
				uint8_t def_id,
				uint32_t val)
{
	if(def_id == 0)
		iowrite32be(val, &regs->fmkg_gdv0r);
	else
		iowrite32be(val, &regs->fmkg_gdv1r);
}


void fman_kg_set_exception(struct fman_kg_regs *regs,
				uint32_t exception,
				bool enable)
{
	uint32_t tmp;

	tmp = ioread32be(&regs->fmkg_eeer);

	if (enable) {
		tmp |= exception;
	} else {
		tmp &= ~exception;
	}

	iowrite32be(tmp, &regs->fmkg_eeer);
}

void fman_kg_get_exception(struct fman_kg_regs *regs,
				uint32_t *events,
				uint32_t *scheme_ids,
				bool clear)
{
	uint32_t mask;

	*events = ioread32be(&regs->fmkg_eer);
	mask = ioread32be(&regs->fmkg_eeer);
	*events &= mask;
 
	*scheme_ids = 0;

	if (*events & FM_EX_KG_KEYSIZE_OVERFLOW) {
		*scheme_ids = ioread32be(&regs->fmkg_seer);
		mask = ioread32be(&regs->fmkg_seeer);
		*scheme_ids &= mask;
	}

	if (clear) {
		iowrite32be(*scheme_ids, &regs->fmkg_seer);
		iowrite32be(*events, &regs->fmkg_eer);
	}
}

void fman_kg_get_capture(struct fman_kg_regs *regs,
				struct fman_kg_ex_ecc_attr *ecc_attr,
				bool clear)
{
	uint32_t tmp;

	tmp = ioread32be(&regs->fmkg_serc);

	if (tmp & KG_FMKG_SERC_CAP) {
		/* Captured data is valid */
		ecc_attr->valid = TRUE;
		ecc_attr->double_ecc =
			(bool)((tmp & KG_FMKG_SERC_CET) ? TRUE : FALSE);
		ecc_attr->single_ecc_count =
			(uint8_t)((tmp & KG_FMKG_SERC_CNT_MSK) >>
					KG_FMKG_SERC_CNT_SHIFT);
		ecc_attr->addr = (uint16_t)(tmp & KG_FMKG_SERC_ADDR_MSK);

		if (clear)
			iowrite32be(KG_FMKG_SERC_CAP, &regs->fmkg_serc);
	} else {
		/* No ECC error is captured */
		ecc_attr->valid = FALSE;
	}
}

int fman_kg_build_scheme(struct fman_kg_scheme_params *params,
				struct fman_kg_scheme_regs *scheme_regs)
{
	struct fman_kg_extract_params *extract_params;
	struct fman_kg_gen_extract_params *gen_params;
	uint32_t tmp_reg, i, select, mask, fqb;
	uint8_t offset, shift, ht;

	/* Zero out all registers so no need to care about unused ones */
	memset(scheme_regs, 0, sizeof(struct fman_kg_scheme_regs));

	/* Mode register */
	tmp_reg = fm_kg_build_nia(params->next_engine,
			params->next_engine_action);
	if (tmp_reg == KG_NIA_INVALID) {
		return -EINVAL;
	}

	if (params->next_engine == E_FMAN_PCD_PLCR) {
		tmp_reg |= FMAN_KG_SCH_MODE_NIA_PLCR;
	}
	else if (params->next_engine == E_FMAN_PCD_CC) {
		tmp_reg |= (uint32_t)params->cc_params.base_offset <<
				FMAN_KG_SCH_MODE_CCOBASE_SHIFT;
	}

	tmp_reg |= FMAN_KG_SCH_MODE_EN;
	scheme_regs->kgse_mode = tmp_reg;

	/* Match vector */
	scheme_regs->kgse_mv = params->match_vector;

	extract_params = &params->extract_params;

	/* Scheme default values registers */
	scheme_regs->kgse_dv0 = extract_params->def_scheme_0;
	scheme_regs->kgse_dv1 = extract_params->def_scheme_1;

	/* Extract Known Fields Command register */
	scheme_regs->kgse_ekfc = extract_params->known_fields;

	/* Entry Extract Known Default Value register */
	tmp_reg = 0;
	tmp_reg |= extract_params->known_fields_def.mac_addr <<
			FMAN_KG_SCH_DEF_MAC_ADDR_SHIFT;
	tmp_reg |= extract_params->known_fields_def.vlan_tci <<
			FMAN_KG_SCH_DEF_VLAN_TCI_SHIFT;
	tmp_reg |= extract_params->known_fields_def.etype <<
			FMAN_KG_SCH_DEF_ETYPE_SHIFT;
	tmp_reg |= extract_params->known_fields_def.ppp_sid <<
			FMAN_KG_SCH_DEF_PPP_SID_SHIFT;
	tmp_reg |= extract_params->known_fields_def.ppp_pid <<
			FMAN_KG_SCH_DEF_PPP_PID_SHIFT;
	tmp_reg |= extract_params->known_fields_def.mpls <<
			FMAN_KG_SCH_DEF_MPLS_SHIFT;
	tmp_reg |= extract_params->known_fields_def.ip_addr <<
			FMAN_KG_SCH_DEF_IP_ADDR_SHIFT;
	tmp_reg |= extract_params->known_fields_def.ptype <<
			FMAN_KG_SCH_DEF_PTYPE_SHIFT;
	tmp_reg |= extract_params->known_fields_def.ip_tos_tc <<
			FMAN_KG_SCH_DEF_IP_TOS_TC_SHIFT;
	tmp_reg |= extract_params->known_fields_def.ipv6_fl <<
			FMAN_KG_SCH_DEF_IPv6_FL_SHIFT;
	tmp_reg |= extract_params->known_fields_def.ipsec_spi <<
			FMAN_KG_SCH_DEF_IPSEC_SPI_SHIFT;
	tmp_reg |= extract_params->known_fields_def.l4_port <<
			FMAN_KG_SCH_DEF_L4_PORT_SHIFT;
	tmp_reg |= extract_params->known_fields_def.tcp_flg <<
			FMAN_KG_SCH_DEF_TCP_FLG_SHIFT;

	scheme_regs->kgse_ekdv = tmp_reg;

	/* Generic extract registers */
	if (extract_params->gen_extract_num > FM_KG_NUM_OF_GENERIC_REGS) {
		return -EINVAL;
	}

	for (i = 0; i < extract_params->gen_extract_num; i++) {
		gen_params = extract_params->gen_extract + i;

		tmp_reg = FMAN_KG_SCH_GEN_VALID;
		tmp_reg |= (uint32_t)gen_params->def_val <<
				FMAN_KG_SCH_GEN_DEF_SHIFT;

		if (gen_params->type == E_FMAN_KG_HASH_EXTRACT) {
			if ((gen_params->extract > FMAN_KG_SCH_GEN_SIZE_MAX) ||
					(gen_params->extract == 0)) {
				return -EINVAL;
			}
		} else {
			tmp_reg |= FMAN_KG_SCH_GEN_OR;
		}

		tmp_reg |= (uint32_t)gen_params->extract <<
				FMAN_KG_SCH_GEN_SIZE_SHIFT;
		tmp_reg |= (uint32_t)gen_params->mask <<
				FMAN_KG_SCH_GEN_MASK_SHIFT;

		offset = gen_params->offset;
		ht = get_gen_ht_code(gen_params->src,
				gen_params->no_validation,
				&offset);
		tmp_reg |= (uint32_t)ht << FMAN_KG_SCH_GEN_HT_SHIFT;
		tmp_reg |= offset;

		scheme_regs->kgse_gec[i] = tmp_reg;
	}

	/* Masks registers */
	if (extract_params->masks_num > FM_KG_EXTRACT_MASKS_NUM) {
		return -EINVAL;
	}

	select = 0;
	mask = 0;
	fqb = 0;
	for (i = 0; i < extract_params->masks_num; i++) {
		/* MCSx fields */
		KG_GET_MASK_SEL_SHIFT(shift, i);
		if (extract_params->masks[i].is_known) {
			/* Mask known field */
			select |= extract_params->masks[i].field_or_gen_idx <<
					shift;
		} else {
			/* Mask generic extract */
			select |= (extract_params->masks[i].field_or_gen_idx +
					FM_KG_MASK_SEL_GEN_BASE) << shift;
		}

		/* MOx fields - spread between se_bmch and se_fqb registers */
		KG_GET_MASK_OFFSET_SHIFT(shift, i);
		if (i < 2) {
			select |= (uint32_t)extract_params->masks[i].offset <<
					shift;
		} else {
			fqb |= (uint32_t)extract_params->masks[i].offset <<
					shift;
		}

		/* BMx fields */
		KG_GET_MASK_SHIFT(shift, i);
		mask |= (uint32_t)extract_params->masks[i].mask << shift;
	}

	/* Finish with rest of BMx fileds -
	 * don't mask bits for unused masks by setting
	 * corresponding BMx field = 0xFF */
	for (i = extract_params->masks_num; i < FM_KG_EXTRACT_MASKS_NUM; i++) {
		KG_GET_MASK_SHIFT(shift, i);
		mask |= 0xFF << shift;
	}

	scheme_regs->kgse_bmch = select;
	scheme_regs->kgse_bmcl = mask;

	/* Finish with FQB register initialization.
	 * Check fqid is 24-bit value. */
	if (params->base_fqid & ~0x00FFFFFF) {
		return -EINVAL;
	}

	fqb |= params->base_fqid;
	scheme_regs->kgse_fqb = fqb;

	/* Hash Configuration register */
	tmp_reg = 0;
	if (params->hash_params.use_hash) {
		/* Check hash mask is 24-bit value */
		if (params->hash_params.mask & ~0x00FFFFFF) {
			return -EINVAL;
		}

		/* Hash function produces 64-bit value, 24 bits of that
		 * are used to generate fq_id and policer profile.
		 * Thus, maximal shift is 40 bits to allow 24 bits out of 64.
		 */
		if (params->hash_params.shift_r > FMAN_KG_SCH_HASH_HSHIFT_MAX) {
			return -EINVAL;
		}

		tmp_reg |= params->hash_params.mask;
		tmp_reg |= (uint32_t)params->hash_params.shift_r <<
				FMAN_KG_SCH_HASH_HSHIFT_SHIFT;

		if (params->hash_params.sym) {
			tmp_reg |= FMAN_KG_SCH_HASH_SYM;
		}

	}

	if (params->bypass_fqid_gen) {
		tmp_reg |= FMAN_KG_SCH_HASH_NO_FQID_GEN;
	}

	scheme_regs->kgse_hc = tmp_reg;

	/* Policer Profile register */
	if (params->policer_params.bypass_pp_gen) {
		tmp_reg = 0;
	} else {
		/* Lower 8 bits of 24-bits extracted from hash result
		 * are used for policer profile generation.
		 * That leaves maximum shift value = 23. */
		if (params->policer_params.shift > FMAN_KG_SCH_PP_SHIFT_MAX) {
			return -EINVAL;
		}

		tmp_reg = params->policer_params.base;
		tmp_reg |= ((uint32_t)params->policer_params.shift <<
				FMAN_KG_SCH_PP_SH_SHIFT) &
				FMAN_KG_SCH_PP_SH_MASK;
		tmp_reg |= ((uint32_t)params->policer_params.shift <<
				FMAN_KG_SCH_PP_SL_SHIFT) &
				FMAN_KG_SCH_PP_SL_MASK;
		tmp_reg |= (uint32_t)params->policer_params.mask <<
				FMAN_KG_SCH_PP_MASK_SHIFT;
	}

	scheme_regs->kgse_ppc = tmp_reg;

	/* Coarse Classification Bit Select register */
	if (params->next_engine == E_FMAN_PCD_CC) {
		scheme_regs->kgse_ccbs = params->cc_params.qlcv_bits_sel;
	}

	/* Packets Counter register */
	if (params->update_counter) {
		scheme_regs->kgse_spc = params->counter_value;
	}

	return 0;
}

int fman_kg_write_scheme(struct fman_kg_regs *regs,
				uint8_t scheme_id,
				uint8_t hwport_id,
				struct fman_kg_scheme_regs *scheme_regs,
				bool update_counter)
{
	struct fman_kg_scheme_regs *kgse_regs;
	uint32_t tmp_reg;
	int err, i;

	/* Write indirect scheme registers */
	kgse_regs = (struct fman_kg_scheme_regs *)&(regs->fmkg_indirect[0]);

	iowrite32be(scheme_regs->kgse_mode, &kgse_regs->kgse_mode);
	iowrite32be(scheme_regs->kgse_ekfc, &kgse_regs->kgse_ekfc);
	iowrite32be(scheme_regs->kgse_ekdv, &kgse_regs->kgse_ekdv);
	iowrite32be(scheme_regs->kgse_bmch, &kgse_regs->kgse_bmch);
	iowrite32be(scheme_regs->kgse_bmcl, &kgse_regs->kgse_bmcl);
	iowrite32be(scheme_regs->kgse_fqb, &kgse_regs->kgse_fqb);
	iowrite32be(scheme_regs->kgse_hc, &kgse_regs->kgse_hc);
	iowrite32be(scheme_regs->kgse_ppc, &kgse_regs->kgse_ppc);
	iowrite32be(scheme_regs->kgse_spc, &kgse_regs->kgse_spc);
	iowrite32be(scheme_regs->kgse_dv0, &kgse_regs->kgse_dv0);
	iowrite32be(scheme_regs->kgse_dv1, &kgse_regs->kgse_dv1);
	iowrite32be(scheme_regs->kgse_ccbs, &kgse_regs->kgse_ccbs);
	iowrite32be(scheme_regs->kgse_mv, &kgse_regs->kgse_mv);

	for (i = 0 ; i < FM_KG_NUM_OF_GENERIC_REGS ; i++)
		iowrite32be(scheme_regs->kgse_gec[i], &kgse_regs->kgse_gec[i]);

	/* Write AR (Action register) */
	tmp_reg = build_ar_scheme(scheme_id, hwport_id, update_counter, TRUE);
	err = fman_kg_write_ar_wait(regs, tmp_reg);
	return err;
}

int fman_kg_delete_scheme(struct fman_kg_regs *regs,
				uint8_t scheme_id,
				uint8_t hwport_id)
{
	struct fman_kg_scheme_regs *kgse_regs;
	uint32_t tmp_reg;
	int err, i;

	kgse_regs = (struct fman_kg_scheme_regs *)&(regs->fmkg_indirect[0]);

	/* Clear all registers including enable bit in mode register */
	for (i = 0; i < (sizeof(struct fman_kg_scheme_regs)) / 4; ++i) {
		iowrite32be(0, ((uint32_t *)kgse_regs + i));
	}

	/* Write AR (Action register) */
	tmp_reg = build_ar_scheme(scheme_id, hwport_id, FALSE, TRUE);
	err = fman_kg_write_ar_wait(regs, tmp_reg);
	return err;
}

int fman_kg_get_scheme_counter(struct fman_kg_regs *regs,
				uint8_t scheme_id,
				uint8_t hwport_id,
				uint32_t *counter)
{
	struct fman_kg_scheme_regs  *kgse_regs;
	uint32_t                    tmp_reg;
	int                         err;

	kgse_regs = (struct fman_kg_scheme_regs *)&(regs->fmkg_indirect[0]);
 
	tmp_reg = build_ar_scheme(scheme_id, hwport_id, TRUE, FALSE);
    	err = fman_kg_write_ar_wait(regs, tmp_reg);

	if (err != 0)
		return err;

	*counter = ioread32be(&kgse_regs->kgse_spc);

	return 0;
}

int fman_kg_set_scheme_counter(struct fman_kg_regs *regs,
				uint8_t scheme_id,
				uint8_t hwport_id,
				uint32_t counter)
{
	struct fman_kg_scheme_regs *kgse_regs;
	uint32_t tmp_reg;
	int err;

	kgse_regs = (struct fman_kg_scheme_regs *)&(regs->fmkg_indirect[0]);

	tmp_reg = build_ar_scheme(scheme_id, hwport_id, TRUE, FALSE);

	err = fman_kg_write_ar_wait(regs, tmp_reg);
	if (err != 0)
		return err;
 
	/* Keygen indirect access memory contains all scheme_id registers
	 * by now. Change only counter value. */
	iowrite32be(counter, &kgse_regs->kgse_spc);

	/* Write back scheme registers */
	tmp_reg = build_ar_scheme(scheme_id, hwport_id, TRUE, TRUE);
	err = fman_kg_write_ar_wait(regs, tmp_reg);

	return err;
}

uint32_t fman_kg_get_schemes_total_counter(struct fman_kg_regs *regs)
{
    return ioread32be(&regs->fmkg_tpc);
}

int fman_kg_build_cls_plan(struct fman_kg_cls_plan_params *params,
				struct fman_kg_cp_regs *cls_plan_regs)
{
	uint8_t entries_set, entry_bit;
	int i;

	/* Zero out all group's register */
	memset(cls_plan_regs, 0, sizeof(struct fman_kg_cp_regs));

	/* Go over all classification entries in params->entries_mask and
	 * configure the corresponding cpe register */
	entries_set = params->entries_mask;
	for (i = 0; entries_set; i++) {
		entry_bit = (uint8_t)(0x80 >> i);
		if ((entry_bit & entries_set) == 0)
			continue;
		entries_set ^= entry_bit;
		cls_plan_regs->kgcpe[i] = params->mask_vector[i];
	}

	return 0;
}

int fman_kg_write_cls_plan(struct fman_kg_regs *regs,
				uint8_t grp_id,
				uint8_t entries_mask,
				uint8_t hwport_id,
				struct fman_kg_cp_regs *cls_plan_regs)
{
	struct fman_kg_cp_regs *kgcpe_regs;
	uint32_t tmp_reg;
	int i, err;

	/* Check group index is valid and the group isn't empty */
	if (grp_id >= FM_KG_CLS_PLAN_GRPS_NUM)
		return -EINVAL;

	/* Write indirect classification plan registers */
	kgcpe_regs = (struct fman_kg_cp_regs *)&(regs->fmkg_indirect[0]);

	for (i = 0; i < FM_KG_NUM_CLS_PLAN_ENTR; i++) {
		iowrite32be(cls_plan_regs->kgcpe[i], &kgcpe_regs->kgcpe[i]);
	}

	tmp_reg = build_ar_cls_plan(grp_id, entries_mask, hwport_id, TRUE);
	err = fman_kg_write_ar_wait(regs, tmp_reg);
	return err;
}

int fman_kg_write_bind_schemes(struct fman_kg_regs *regs,
				uint8_t hwport_id,
				uint32_t schemes)
{
	struct fman_kg_pe_regs *kg_pe_regs;
	uint32_t tmp_reg;
	int err;

	kg_pe_regs = (struct fman_kg_pe_regs *)&(regs->fmkg_indirect[0]);

	iowrite32be(schemes, &kg_pe_regs->fmkg_pe_sp);

	tmp_reg = build_ar_bind_scheme(hwport_id, TRUE);
	err = fman_kg_write_ar_wait(regs, tmp_reg);
	return err;
}

int fman_kg_build_bind_cls_plans(uint8_t grp_base,
					uint8_t grp_mask,
					uint32_t *bind_cls_plans)
{
	/* Check grp_base and grp_mask are 5-bits values */
	if ((grp_base & ~0x0000001F) || (grp_mask & ~0x0000001F))
		return -EINVAL;

	*bind_cls_plans = (uint32_t) ((grp_mask << FMAN_KG_PE_CPP_MASK_SHIFT) | grp_base);
	return 0;
}


int fman_kg_write_bind_cls_plans(struct fman_kg_regs *regs,
					uint8_t hwport_id,
					uint32_t bind_cls_plans)
{
	struct fman_kg_pe_regs *kg_pe_regs;
	uint32_t tmp_reg;
	int err;

	kg_pe_regs = (struct fman_kg_pe_regs *)&(regs->fmkg_indirect[0]);

	iowrite32be(bind_cls_plans, &kg_pe_regs->fmkg_pe_cpp);

	tmp_reg = build_ar_bind_cls_plan(hwport_id, TRUE);
	err = fman_kg_write_ar_wait(regs, tmp_reg);
	return err;
}
