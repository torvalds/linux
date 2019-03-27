/*
 * Copyright 2012 Freescale Semiconductor Inc.
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

#include "fsl_fman_prs.h"

uint32_t fman_prs_get_err_event(struct fman_prs_regs *regs, uint32_t ev_mask)
{
	return ioread32be(&regs->fmpr_perr) & ev_mask;
}

uint32_t fman_prs_get_err_ev_mask(struct fman_prs_regs *regs)
{
	return ioread32be(&regs->fmpr_perer);
}

void fman_prs_ack_err_event(struct fman_prs_regs *regs, uint32_t event)
{
	iowrite32be(event, &regs->fmpr_perr);
}

uint32_t fman_prs_get_expt_event(struct fman_prs_regs *regs, uint32_t ev_mask)
{
	return ioread32be(&regs->fmpr_pevr) & ev_mask;
}

uint32_t fman_prs_get_expt_ev_mask(struct fman_prs_regs *regs)
{
	return ioread32be(&regs->fmpr_pever);
}

void fman_prs_ack_expt_event(struct fman_prs_regs *regs, uint32_t event)
{
	iowrite32be(event, &regs->fmpr_pevr);
}

void fman_prs_defconfig(struct fman_prs_cfg *cfg)
{
	cfg->port_id_stat = 0;
	cfg->max_prs_cyc_lim = DEFAULT_MAX_PRS_CYC_LIM;
	cfg->prs_exceptions = 0x03000000;
}

int fman_prs_init(struct fman_prs_regs *regs, struct fman_prs_cfg *cfg)
{
	uint32_t tmp;

	iowrite32be(cfg->max_prs_cyc_lim, &regs->fmpr_rpclim);
	iowrite32be((FM_PCD_PRS_SINGLE_ECC | FM_PCD_PRS_PORT_IDLE_STS),
			&regs->fmpr_pevr);

	if (cfg->prs_exceptions & FM_PCD_EX_PRS_SINGLE_ECC)
		iowrite32be(FM_PCD_PRS_SINGLE_ECC, &regs->fmpr_pever);
	else
		iowrite32be(0, &regs->fmpr_pever);

	iowrite32be(FM_PCD_PRS_DOUBLE_ECC, &regs->fmpr_perr);

	tmp = 0;
	if (cfg->prs_exceptions & FM_PCD_EX_PRS_DOUBLE_ECC)
		tmp |= FM_PCD_PRS_DOUBLE_ECC;
	iowrite32be(tmp, &regs->fmpr_perer);

	iowrite32be(cfg->port_id_stat, &regs->fmpr_ppsc);

	return 0;
}

void fman_prs_enable(struct fman_prs_regs *regs)
{
	uint32_t tmp;

	tmp = ioread32be(&regs->fmpr_rpimac) | FM_PCD_PRS_RPIMAC_EN;
	iowrite32be(tmp, &regs->fmpr_rpimac);
}

void fman_prs_disable(struct fman_prs_regs *regs)
{
	uint32_t tmp;

	tmp = ioread32be(&regs->fmpr_rpimac) & ~FM_PCD_PRS_RPIMAC_EN;
	iowrite32be(tmp, &regs->fmpr_rpimac);
}

int fman_prs_is_enabled(struct fman_prs_regs *regs)
{
	return ioread32be(&regs->fmpr_rpimac) & FM_PCD_PRS_RPIMAC_EN;
}

void fman_prs_set_stst_port_msk(struct fman_prs_regs *regs, uint32_t pid_msk)
{
	iowrite32be(pid_msk, &regs->fmpr_ppsc);
}

void fman_prs_set_stst(struct fman_prs_regs *regs, bool enable)
{
	if (enable)
		iowrite32be(FM_PCD_PRS_PPSC_ALL_PORTS, &regs->fmpr_ppsc);
	else
		iowrite32be(0, &regs->fmpr_ppsc);
}
