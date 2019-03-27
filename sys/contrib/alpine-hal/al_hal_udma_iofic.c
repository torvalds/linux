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
 * @file   al_hal_udma_iofic.c
 *
 * @brief  unit interrupts configurations
 *
 */

#include "al_hal_udma_iofic.h"
#include "al_hal_udma_regs.h"

/*
 * configure the interrupt registers, interrupts will are kept masked
 */
static int al_udma_main_iofic_config(struct al_iofic_regs __iomem *base,
				    enum al_iofic_mode mode)
{
	switch (mode) {
	case AL_IOFIC_MODE_LEGACY:
		al_iofic_config(base, AL_INT_GROUP_A,
				INT_CONTROL_GRP_SET_ON_POSEDGE |
				INT_CONTROL_GRP_MASK_MSI_X |
				INT_CONTROL_GRP_CLEAR_ON_READ);
		al_iofic_config(base, AL_INT_GROUP_B,
				INT_CONTROL_GRP_CLEAR_ON_READ |
				INT_CONTROL_GRP_MASK_MSI_X);
		al_iofic_config(base, AL_INT_GROUP_C,
				INT_CONTROL_GRP_CLEAR_ON_READ |
				INT_CONTROL_GRP_MASK_MSI_X);
		al_iofic_config(base, AL_INT_GROUP_D,
				INT_CONTROL_GRP_SET_ON_POSEDGE |
				INT_CONTROL_GRP_MASK_MSI_X |
				INT_CONTROL_GRP_CLEAR_ON_READ);
		break;
	case AL_IOFIC_MODE_MSIX_PER_Q:
		al_iofic_config(base, AL_INT_GROUP_A,
				INT_CONTROL_GRP_SET_ON_POSEDGE |
				INT_CONTROL_GRP_AUTO_MASK |
				INT_CONTROL_GRP_AUTO_CLEAR);
		al_iofic_config(base, AL_INT_GROUP_B,
				INT_CONTROL_GRP_AUTO_CLEAR |
				INT_CONTROL_GRP_AUTO_MASK |
				INT_CONTROL_GRP_CLEAR_ON_READ);
		al_iofic_config(base, AL_INT_GROUP_C,
				INT_CONTROL_GRP_AUTO_CLEAR |
				INT_CONTROL_GRP_AUTO_MASK |
				INT_CONTROL_GRP_CLEAR_ON_READ);
		al_iofic_config(base, AL_INT_GROUP_D,
				INT_CONTROL_GRP_SET_ON_POSEDGE |
				INT_CONTROL_GRP_CLEAR_ON_READ |
				INT_CONTROL_GRP_MASK_MSI_X);
		break;
	case AL_IOFIC_MODE_MSIX_PER_GROUP:
		al_iofic_config(base, AL_INT_GROUP_A,
				INT_CONTROL_GRP_SET_ON_POSEDGE |
				INT_CONTROL_GRP_AUTO_CLEAR |
				INT_CONTROL_GRP_AUTO_MASK);
		al_iofic_config(base, AL_INT_GROUP_B,
				INT_CONTROL_GRP_CLEAR_ON_READ |
				INT_CONTROL_GRP_MASK_MSI_X);
		al_iofic_config(base, AL_INT_GROUP_C,
				INT_CONTROL_GRP_CLEAR_ON_READ |
				INT_CONTROL_GRP_MASK_MSI_X);
		al_iofic_config(base, AL_INT_GROUP_D,
				INT_CONTROL_GRP_SET_ON_POSEDGE |
				INT_CONTROL_GRP_CLEAR_ON_READ |
				INT_CONTROL_GRP_MASK_MSI_X);
		break;
	default:
		al_err("%s: invalid mode (%d)\n", __func__, mode);
		return -EINVAL;
	}

	al_dbg("%s: base.%p mode %d\n", __func__, base, mode);
	return 0;
}

/*
 * configure the UDMA interrupt registers, interrupts are kept masked
 */
int al_udma_iofic_config(struct unit_regs __iomem *regs, enum al_iofic_mode mode,
			uint32_t	m2s_errors_disable,
			uint32_t	m2s_aborts_disable,
			uint32_t	s2m_errors_disable,
			uint32_t	s2m_aborts_disable)
{
	int rc;

	rc = al_udma_main_iofic_config(&regs->gen.interrupt_regs.main_iofic, mode);
	if (rc != 0)
		return rc;

	al_iofic_unmask(&regs->gen.interrupt_regs.secondary_iofic_ctrl, AL_INT_GROUP_A, ~m2s_errors_disable);
	al_iofic_abort_mask(&regs->gen.interrupt_regs.secondary_iofic_ctrl, AL_INT_GROUP_A, m2s_aborts_disable);

	al_iofic_unmask(&regs->gen.interrupt_regs.secondary_iofic_ctrl, AL_INT_GROUP_B, ~s2m_errors_disable);
	al_iofic_abort_mask(&regs->gen.interrupt_regs.secondary_iofic_ctrl, AL_INT_GROUP_B, s2m_aborts_disable);

	al_dbg("%s base.%p mode %d\n", __func__, regs, mode);
	return 0;
}

/*
 * return the offset of the unmask register for a given group
 */
uint32_t __iomem * al_udma_iofic_unmask_offset_get(
	struct unit_regs __iomem	*regs,
	enum al_udma_iofic_level	level,
	int				group)
{
	al_assert(al_udma_iofic_level_and_group_valid(level, group));
	return al_iofic_unmask_offset_get(al_udma_iofic_reg_base_get(regs, level), group);
}

/** @} end of UDMA group */
