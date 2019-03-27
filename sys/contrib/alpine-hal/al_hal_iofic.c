/*-
*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 *  @{
 * @file   al_hal_iofic.c
 *
 * @brief  interrupt controller hal
 *
 */

#include "al_hal_iofic.h"
#include "al_hal_iofic_regs.h"

/*
 * configure the interrupt registers, interrupts will are kept masked
 */
int al_iofic_config(void __iomem *regs_base, int group, uint32_t flags)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	al_reg_write32(&regs->ctrl[group].int_control_grp, flags);

	return 0;
}

/*
 * configure the moderation timer resolution for a given group
 */
int al_iofic_moder_res_config(void __iomem *regs_base, int group,
			     uint8_t resolution)

{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);
	uint32_t reg;

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	reg = al_reg_read32(&regs->ctrl[group].int_control_grp);
	AL_REG_FIELD_SET(reg,
			 INT_CONTROL_GRP_MOD_RES_MASK,
			 INT_CONTROL_GRP_MOD_RES_SHIFT,
			 resolution);
	al_reg_write32(&regs->ctrl[group].int_control_grp, reg);

	return 0;
}

/*
 * configure the moderation timer interval for a given legacy interrupt group
 */
int al_iofic_legacy_moder_interval_config(void __iomem *regs_base, int group,
				     uint8_t interval)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);
	uint32_t reg;

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	reg = al_reg_read32(&regs->ctrl[group].int_control_grp);
	AL_REG_FIELD_SET(reg,
			 INT_CONTROL_GRP_MOD_INTV_MASK,
			 INT_CONTROL_GRP_MOD_INTV_SHIFT,
			 interval);
	al_reg_write32(&regs->ctrl[group].int_control_grp, reg);

	return 0;
}


/*
 * configure the moderation timer interval for a given msix vector.
 */
int al_iofic_msix_moder_interval_config(void __iomem *regs_base, int group,
				       uint8_t vector, uint8_t interval)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);
	uint32_t reg;

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	reg = al_reg_read32(&regs->grp_int_mod[group][vector].grp_int_mod_reg);
	AL_REG_FIELD_SET(reg,
			 INT_MOD_INTV_MASK,
			 INT_MOD_INTV_SHIFT,
			 interval);
	al_reg_write32(&regs->grp_int_mod[group][vector].grp_int_mod_reg, reg);

	return 0;
}

/*
 * configure the target-id attributes for a given msix vector.
 */
int al_iofic_msix_tgtid_attributes_config(void __iomem *regs_base, int group,
				       uint8_t vector, uint32_t tgtid, uint8_t tgtid_en)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);
	uint32_t reg = 0;

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	AL_REG_FIELD_SET(reg,
			 INT_MSIX_TGTID_MASK,
			 INT_MSIX_TGTID_SHIFT,
			 tgtid);
	AL_REG_BIT_VAL_SET(reg,
			 INT_MSIX_TGTID_EN_SHIFT,
			 tgtid_en);

	al_reg_write32(&regs->grp_int_mod[group][vector].grp_int_tgtid_reg, reg);

	return 0;
}

/*
 * return the offset of the unmask register for a given group
 */
uint32_t __iomem * al_iofic_unmask_offset_get(void __iomem *regs_base, int group)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	return &regs->ctrl[group].int_mask_clear_grp;
}


/*
 * unmask specific interrupts for a given group
 */
void al_iofic_unmask(void __iomem *regs_base, int group, uint32_t mask)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	/*
	 * use the mask clear register, no need to read the mask register
	 * itself. write 0 to unmask, 1 has no effect
	 */
	al_reg_write32_relaxed(&regs->ctrl[group].int_mask_clear_grp, ~mask);
}

/*
 * mask specific interrupts for a given group
 */
void al_iofic_mask(void __iomem *regs_base, int group, uint32_t mask)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);
	uint32_t reg;

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	reg = al_reg_read32(&regs->ctrl[group].int_mask_grp);

	al_reg_write32(&regs->ctrl[group].int_mask_grp, reg | mask);
}

/*
 * read the mask for a given group
 */
uint32_t al_iofic_read_mask(void __iomem *regs_base, int group)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	return al_reg_read32(&regs->ctrl[group].int_mask_grp);
}

/*
 * read interrupt cause register for a given group
 */
uint32_t al_iofic_read_cause(void __iomem *regs_base, int group)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	return al_reg_read32(&regs->ctrl[group].int_cause_grp);
}

/*
 * clear bits in the interrupt cause register for a given group
 */
void al_iofic_clear_cause(void __iomem *regs_base, int group, uint32_t mask)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	/* inverse mask, writing 1 has no effect */
	al_reg_write32(&regs->ctrl[group].int_cause_grp, ~mask);
}

/*
 * Set the cause register for a given group
 */
void al_iofic_set_cause(void __iomem *regs_base, int group, uint32_t mask)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	al_reg_write32(&regs->ctrl[group].int_cause_set_grp, mask);
}


/*
 * unmask specific interrupts from aborting the udma a given group
 */
void al_iofic_abort_mask(void __iomem *regs_base, int group, uint32_t mask)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	al_reg_write32(&regs->ctrl[group].int_abort_msk_grp, mask);

}

/*
 * trigger all interrupts that are waiting for moderation timers to expire
 */
void al_iofic_interrupt_moderation_reset(void __iomem *regs_base, int group)
{
	struct al_iofic_regs __iomem *regs = (struct al_iofic_regs __iomem *)(regs_base);
	uint32_t reg = 0;

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	al_assert(regs_base);
	al_assert(group < AL_IOFIC_MAX_GROUPS);

	reg = al_reg_read32(&regs->ctrl[group].int_control_grp);
	reg |= INT_CONTROL_GRP_MOD_RST;

	al_reg_write32(&regs->ctrl[group].int_control_grp, reg);
}

/** @} end of interrupt controller group */
