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

#ifndef __FSL_FMAN_PRS_H
#define __FSL_FMAN_PRS_H

#include "common/general.h"

#define FM_PCD_EX_PRS_DOUBLE_ECC	0x02000000
#define FM_PCD_EX_PRS_SINGLE_ECC	0x01000000

#define FM_PCD_PRS_PPSC_ALL_PORTS	0xffff0000
#define FM_PCD_PRS_RPIMAC_EN		0x00000001
#define FM_PCD_PRS_PORT_IDLE_STS	0xffff0000
#define FM_PCD_PRS_SINGLE_ECC		0x00004000
#define FM_PCD_PRS_DOUBLE_ECC		0x00004000
#define PRS_MAX_CYCLE_LIMIT		8191

#define DEFAULT_MAX_PRS_CYC_LIM		0

struct fman_prs_regs {
	uint32_t fmpr_rpclim;
	uint32_t fmpr_rpimac;
	uint32_t pmeec;
	uint32_t res00c[5];
	uint32_t fmpr_pevr;
	uint32_t fmpr_pever;
	uint32_t res028;
	uint32_t fmpr_perr;
	uint32_t fmpr_perer;
	uint32_t res034;
	uint32_t res038[10];
	uint32_t fmpr_ppsc;
	uint32_t res064;
	uint32_t fmpr_pds;
	uint32_t fmpr_l2rrs;
	uint32_t fmpr_l3rrs;
	uint32_t fmpr_l4rrs;
	uint32_t fmpr_srrs;
	uint32_t fmpr_l2rres;
	uint32_t fmpr_l3rres;
	uint32_t fmpr_l4rres;
	uint32_t fmpr_srres;
	uint32_t fmpr_spcs;
	uint32_t fmpr_spscs;
	uint32_t fmpr_hxscs;
	uint32_t fmpr_mrcs;
	uint32_t fmpr_mwcs;
	uint32_t fmpr_mrscs;
	uint32_t fmpr_mwscs;
	uint32_t fmpr_fcscs;
};

struct fman_prs_cfg {
	uint32_t	port_id_stat;
	uint16_t	max_prs_cyc_lim;
	uint32_t	prs_exceptions;
};

uint32_t fman_prs_get_err_event(struct fman_prs_regs *regs, uint32_t ev_mask);
uint32_t fman_prs_get_err_ev_mask(struct fman_prs_regs *regs);
void fman_prs_ack_err_event(struct fman_prs_regs *regs, uint32_t event);
uint32_t fman_prs_get_expt_event(struct fman_prs_regs *regs, uint32_t ev_mask);
uint32_t fman_prs_get_expt_ev_mask(struct fman_prs_regs *regs);
void fman_prs_ack_expt_event(struct fman_prs_regs *regs, uint32_t event);
void fman_prs_defconfig(struct fman_prs_cfg *cfg);
int fman_prs_init(struct fman_prs_regs *regs, struct fman_prs_cfg *cfg);
void fman_prs_enable(struct fman_prs_regs *regs);
void fman_prs_disable(struct fman_prs_regs *regs);
int fman_prs_is_enabled(struct fman_prs_regs *regs);
void fman_prs_set_stst_port_msk(struct fman_prs_regs *regs, uint32_t pid_msk);
void fman_prs_set_stst(struct fman_prs_regs *regs, bool enable);
#endif /* __FSL_FMAN_PRS_H */
