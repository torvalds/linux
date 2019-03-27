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


#include <linux/math64.h>
#include "fsl_fman.h"
#include "dpaa_integration_ext.h"

uint32_t fman_get_bmi_err_event(struct fman_bmi_regs *bmi_rg)
{
	uint32_t	event, mask, force;

	event = ioread32be(&bmi_rg->fmbm_ievr);
	mask = ioread32be(&bmi_rg->fmbm_ier);
	event &= mask;
	/* clear the forced events */
	force = ioread32be(&bmi_rg->fmbm_ifr);
	if (force & event)
		iowrite32be(force & ~event, &bmi_rg->fmbm_ifr);
	/* clear the acknowledged events */
	iowrite32be(event, &bmi_rg->fmbm_ievr);
	return event;
}

uint32_t fman_get_qmi_err_event(struct fman_qmi_regs *qmi_rg)
{
	uint32_t	event, mask, force;

	event = ioread32be(&qmi_rg->fmqm_eie);
	mask = ioread32be(&qmi_rg->fmqm_eien);
	event &= mask;

	/* clear the forced events */
	force = ioread32be(&qmi_rg->fmqm_eif);
	if (force & event)
		iowrite32be(force & ~event, &qmi_rg->fmqm_eif);
	/* clear the acknowledged events */
	iowrite32be(event, &qmi_rg->fmqm_eie);
	return event;
}

uint32_t fman_get_dma_com_id(struct fman_dma_regs *dma_rg)
{
	return ioread32be(&dma_rg->fmdmtcid);
}

uint64_t fman_get_dma_addr(struct fman_dma_regs *dma_rg)
{
	uint64_t addr;

	addr = (uint64_t)ioread32be(&dma_rg->fmdmtal);
	addr |= ((uint64_t)(ioread32be(&dma_rg->fmdmtah)) << 32);

	return addr;
}

uint32_t fman_get_dma_err_event(struct fman_dma_regs *dma_rg)
{
	uint32_t status, mask;

	status = ioread32be(&dma_rg->fmdmsr);
	mask = ioread32be(&dma_rg->fmdmmr);

	/* clear DMA_STATUS_BUS_ERR if mask has no DMA_MODE_BER */
	if ((mask & DMA_MODE_BER) != DMA_MODE_BER)
		status &= ~DMA_STATUS_BUS_ERR;

	/* clear relevant bits if mask has no DMA_MODE_ECC */
	if ((mask & DMA_MODE_ECC) != DMA_MODE_ECC)
		status &= ~(DMA_STATUS_FM_SPDAT_ECC |
		        DMA_STATUS_READ_ECC |
				DMA_STATUS_SYSTEM_WRITE_ECC |
				DMA_STATUS_FM_WRITE_ECC);

	/* clear set events */
	iowrite32be(status, &dma_rg->fmdmsr);

	return status;
}

uint32_t fman_get_fpm_err_event(struct fman_fpm_regs *fpm_rg)
{
	uint32_t	event;

	event = ioread32be(&fpm_rg->fmfp_ee);
	/* clear the all occurred events */
	iowrite32be(event, &fpm_rg->fmfp_ee);
	return event;
}

uint32_t fman_get_muram_err_event(struct fman_fpm_regs *fpm_rg)
{
	uint32_t	event, mask;

	event = ioread32be(&fpm_rg->fm_rcr);
	mask = ioread32be(&fpm_rg->fm_rie);

	/* clear MURAM event bit (do not clear IRAM event) */
	iowrite32be(event & ~FPM_RAM_IRAM_ECC, &fpm_rg->fm_rcr);

	if ((mask & FPM_MURAM_ECC_ERR_EX_EN))
		return event;
	else
		return 0;
}

uint32_t fman_get_iram_err_event(struct fman_fpm_regs *fpm_rg)
{
	uint32_t    event, mask;

	event = ioread32be(&fpm_rg->fm_rcr) ;
	mask = ioread32be(&fpm_rg->fm_rie);
	/* clear IRAM event bit (do not clear MURAM event) */
	iowrite32be(event & ~FPM_RAM_MURAM_ECC,
			&fpm_rg->fm_rcr);

	if ((mask & FPM_IRAM_ECC_ERR_EX_EN))
		return event;
	else
		return 0;
}

uint32_t fman_get_qmi_event(struct fman_qmi_regs *qmi_rg)
{
	uint32_t	event, mask, force;

	event = ioread32be(&qmi_rg->fmqm_ie);
	mask = ioread32be(&qmi_rg->fmqm_ien);
	event &= mask;
	/* clear the forced events */
	force = ioread32be(&qmi_rg->fmqm_if);
	if (force & event)
		iowrite32be(force & ~event, &qmi_rg->fmqm_if);
	/* clear the acknowledged events */
	iowrite32be(event, &qmi_rg->fmqm_ie);
	return event;
}

void fman_enable_time_stamp(struct fman_fpm_regs *fpm_rg,
				uint8_t count1ubit,
				uint16_t fm_clk_freq)
{
	uint32_t tmp;
	uint64_t frac;
	uint32_t intgr;
	uint32_t ts_freq = (uint32_t)(1 << count1ubit); /* in Mhz */

	/* configure timestamp so that bit 8 will count 1 microsecond
	 * Find effective count rate at TIMESTAMP least significant bits:
	 * Effective_Count_Rate = 1MHz x 2^8 = 256MHz
	 * Find frequency ratio between effective count rate and the clock:
	 * Effective_Count_Rate / CLK e.g. for 600 MHz clock:
	 * 256/600 = 0.4266666... */

	intgr = ts_freq / fm_clk_freq;
	/* we multiply by 2^16 to keep the fraction of the division
	 * we do not div back, since we write this value as a fraction
	 * see spec */

	frac = ((uint64_t)ts_freq << 16) - ((uint64_t)intgr << 16) * fm_clk_freq;
	/* we check remainder of the division in order to round up if not int */
	if (do_div(frac, fm_clk_freq))
		frac++;

	tmp = (intgr << FPM_TS_INT_SHIFT) | (uint16_t)frac;
	iowrite32be(tmp, &fpm_rg->fmfp_tsc2);

	/* enable timestamp with original clock */
	iowrite32be(FPM_TS_CTL_EN, &fpm_rg->fmfp_tsc1);
}

uint32_t fman_get_fpm_error_interrupts(struct fman_fpm_regs *fpm_rg)
{
	return ioread32be(&fpm_rg->fm_epi);
}


int fman_set_erratum_10gmac_a004_wa(struct fman_fpm_regs *fpm_rg)
{
	int timeout = 100;

	iowrite32be(0x40000000, &fpm_rg->fmfp_extc);

	while ((ioread32be(&fpm_rg->fmfp_extc) & 0x40000000) && --timeout)
		DELAY(10);

	if (!timeout)
		return -EBUSY;
	return 0;
}

void fman_set_ctrl_intr(struct fman_fpm_regs *fpm_rg,
			uint8_t event_reg_id,
			uint32_t enable_events)
{
	iowrite32be(enable_events, &fpm_rg->fmfp_cee[event_reg_id]);
}

uint32_t fman_get_ctrl_intr(struct fman_fpm_regs *fpm_rg, uint8_t event_reg_id)
{
	return ioread32be(&fpm_rg->fmfp_cee[event_reg_id]);
}

void fman_set_num_of_riscs_per_port(struct fman_fpm_regs *fpm_rg,
					uint8_t port_id,
					uint8_t num_fman_ctrls,
					uint32_t or_fman_ctrl)
{
	uint32_t tmp = 0;

	tmp = (uint32_t)(port_id << FPM_PORT_FM_CTL_PORTID_SHIFT);
	/*TODO - maybe to put CTL# according to another criteria*/
	if (num_fman_ctrls == 2)
		tmp = FPM_PRT_FM_CTL2 | FPM_PRT_FM_CTL1;
	/* order restoration */
	tmp |= (or_fman_ctrl << FPM_PRC_ORA_FM_CTL_SEL_SHIFT) | or_fman_ctrl;

	iowrite32be(tmp, &fpm_rg->fmfp_prc);
}

void fman_set_order_restoration_per_port(struct fman_fpm_regs *fpm_rg,
					uint8_t port_id,
					bool independent_mode,
					bool is_rx_port)
{
	uint32_t tmp = 0;

	tmp = (uint32_t)(port_id << FPM_PORT_FM_CTL_PORTID_SHIFT);
	if (independent_mode) {
		if (is_rx_port)
			tmp |= (FPM_PRT_FM_CTL1 <<
				FPM_PRC_ORA_FM_CTL_SEL_SHIFT) | FPM_PRT_FM_CTL1;
		else
			tmp |= (FPM_PRT_FM_CTL2 <<
				FPM_PRC_ORA_FM_CTL_SEL_SHIFT) | FPM_PRT_FM_CTL2;
	} else {
		tmp |= (FPM_PRT_FM_CTL2|FPM_PRT_FM_CTL1);

		/* order restoration */
		if (port_id % 2)
			tmp |= (FPM_PRT_FM_CTL1 <<
					FPM_PRC_ORA_FM_CTL_SEL_SHIFT);
		else
			tmp |= (FPM_PRT_FM_CTL2 <<
					FPM_PRC_ORA_FM_CTL_SEL_SHIFT);
	}
	iowrite32be(tmp, &fpm_rg->fmfp_prc);
}

uint8_t fman_get_qmi_deq_th(struct fman_qmi_regs *qmi_rg)
{
	return (uint8_t)ioread32be(&qmi_rg->fmqm_gc);
}

uint8_t fman_get_qmi_enq_th(struct fman_qmi_regs *qmi_rg)
{
	return (uint8_t)(ioread32be(&qmi_rg->fmqm_gc) >> 8);
}

void fman_set_qmi_enq_th(struct fman_qmi_regs *qmi_rg, uint8_t val)
{
	uint32_t tmp_reg;

	tmp_reg = ioread32be(&qmi_rg->fmqm_gc);
	tmp_reg &= ~QMI_CFG_ENQ_MASK;
	tmp_reg |= ((uint32_t)val << 8);
	iowrite32be(tmp_reg, &qmi_rg->fmqm_gc);
}

void fman_set_qmi_deq_th(struct fman_qmi_regs *qmi_rg, uint8_t val)
{
	uint32_t tmp_reg;

	tmp_reg = ioread32be(&qmi_rg->fmqm_gc);
	tmp_reg &= ~QMI_CFG_DEQ_MASK;
	tmp_reg |= (uint32_t)val;
	iowrite32be(tmp_reg, &qmi_rg->fmqm_gc);
}

void fman_qmi_disable_dispatch_limit(struct fman_fpm_regs *fpm_rg)
{
	iowrite32be(0, &fpm_rg->fmfp_mxd);
}

void fman_set_liodn_per_port(struct fman_rg *fman_rg, uint8_t port_id,
				uint16_t liodn_base,
				uint16_t liodn_ofst)
{
	uint32_t tmp;

	if ((port_id > 63) || (port_id < 1))
	        return;

	/* set LIODN base for this port */
	tmp = ioread32be(&fman_rg->dma_rg->fmdmplr[port_id / 2]);
	if (port_id % 2) {
		tmp &= ~FM_LIODN_BASE_MASK;
		tmp |= (uint32_t)liodn_base;
	} else {
		tmp &= ~(FM_LIODN_BASE_MASK << DMA_LIODN_SHIFT);
		tmp |= (uint32_t)liodn_base << DMA_LIODN_SHIFT;
	}
	iowrite32be(tmp, &fman_rg->dma_rg->fmdmplr[port_id / 2]);
	iowrite32be((uint32_t)liodn_ofst,
			&fman_rg->bmi_rg->fmbm_spliodn[port_id - 1]);
}

bool fman_is_port_stalled(struct fman_fpm_regs *fpm_rg, uint8_t port_id)
{
	return (bool)!!(ioread32be(&fpm_rg->fmfp_ps[port_id]) & FPM_PS_STALLED);
}

void fman_resume_stalled_port(struct fman_fpm_regs *fpm_rg, uint8_t port_id)
{
	uint32_t	tmp;

	tmp = (uint32_t)((port_id << FPM_PORT_FM_CTL_PORTID_SHIFT) |
				FPM_PRC_REALSE_STALLED);
	iowrite32be(tmp, &fpm_rg->fmfp_prc);
}

int fman_reset_mac(struct fman_fpm_regs *fpm_rg, uint8_t mac_id, bool is_10g)
{
	uint32_t msk, timeout = 100;

	/* Get the relevant bit mask */
	if (is_10g) {
		switch (mac_id) {
		case(0):
			msk = FPM_RSTC_10G0_RESET;
			break;
        case(1):
            msk = FPM_RSTC_10G1_RESET;
            break;
		default:
			return -EINVAL;
		}
	} else {
		switch (mac_id) {
		case(0):
			msk = FPM_RSTC_1G0_RESET;
			break;
		case(1):
			msk = FPM_RSTC_1G1_RESET;
			break;
		case(2):
			msk = FPM_RSTC_1G2_RESET;
			break;
		case(3):
			msk = FPM_RSTC_1G3_RESET;
			break;
		case(4):
			msk = FPM_RSTC_1G4_RESET;
			break;
        case (5):
            msk = FPM_RSTC_1G5_RESET;
            break;
        case (6):
            msk = FPM_RSTC_1G6_RESET;
            break;
        case (7):
            msk = FPM_RSTC_1G7_RESET;
            break;
		default:
			return -EINVAL;
		}
	}
	/* reset */
	iowrite32be(msk, &fpm_rg->fm_rstc);
	while ((ioread32be(&fpm_rg->fm_rstc) & msk) && --timeout)
		DELAY(10);

	if (!timeout)
		return -EBUSY;
	return 0;
}

uint16_t fman_get_size_of_fifo(struct fman_bmi_regs *bmi_rg, uint8_t port_id)
{
	uint32_t tmp_reg;

    if ((port_id > 63) || (port_id < 1))
            return 0;

	tmp_reg = ioread32be(&bmi_rg->fmbm_pfs[port_id - 1]);
	return (uint16_t)((tmp_reg & BMI_FIFO_SIZE_MASK) + 1);
}

uint32_t fman_get_total_fifo_size(struct fman_bmi_regs *bmi_rg)
{
	uint32_t reg, res;

	reg = ioread32be(&bmi_rg->fmbm_cfg1);
	res = (reg >> BMI_CFG1_FIFO_SIZE_SHIFT) & 0x3ff;
	return res * FMAN_BMI_FIFO_UNITS;
}

uint16_t fman_get_size_of_extra_fifo(struct fman_bmi_regs *bmi_rg,
					uint8_t port_id)
{
	uint32_t tmp_reg;

    if ((port_id > 63) || (port_id < 1))
            return 0;

	tmp_reg = ioread32be(&bmi_rg->fmbm_pfs[port_id-1]);
	return (uint16_t)((tmp_reg & BMI_EXTRA_FIFO_SIZE_MASK) >>
				BMI_EXTRA_FIFO_SIZE_SHIFT);
}

void fman_set_size_of_fifo(struct fman_bmi_regs *bmi_rg,
				uint8_t port_id,
				uint32_t sz_fifo,
				uint32_t extra_sz_fifo)
{
	uint32_t tmp;

	if ((port_id > 63) || (port_id < 1))
	        return;

	/* calculate reg */
	tmp = (uint32_t)((sz_fifo / FMAN_BMI_FIFO_UNITS - 1) |
		((extra_sz_fifo / FMAN_BMI_FIFO_UNITS) <<
				BMI_EXTRA_FIFO_SIZE_SHIFT));
	iowrite32be(tmp, &bmi_rg->fmbm_pfs[port_id - 1]);
}

uint8_t fman_get_num_of_tasks(struct fman_bmi_regs *bmi_rg, uint8_t port_id)
{
	uint32_t tmp;

    if ((port_id > 63) || (port_id < 1))
        return 0;

	tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]);
	return (uint8_t)(((tmp & BMI_NUM_OF_TASKS_MASK) >>
				BMI_NUM_OF_TASKS_SHIFT) + 1);
}

uint8_t fman_get_num_extra_tasks(struct fman_bmi_regs *bmi_rg, uint8_t port_id)
{
	uint32_t tmp;

    if ((port_id > 63) || (port_id < 1))
        return 0;

	tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]);
	return (uint8_t)((tmp & BMI_NUM_OF_EXTRA_TASKS_MASK) >>
				BMI_EXTRA_NUM_OF_TASKS_SHIFT);
}

void fman_set_num_of_tasks(struct fman_bmi_regs *bmi_rg,
				uint8_t port_id,
				uint8_t num_tasks,
				uint8_t num_extra_tasks)
{
	uint32_t tmp;

	if ((port_id > 63) || (port_id < 1))
	    return;

	/* calculate reg */
	tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]) &
			~(BMI_NUM_OF_TASKS_MASK | BMI_NUM_OF_EXTRA_TASKS_MASK);
	tmp |= (uint32_t)(((num_tasks - 1) << BMI_NUM_OF_TASKS_SHIFT) |
			(num_extra_tasks << BMI_EXTRA_NUM_OF_TASKS_SHIFT));
	iowrite32be(tmp, &bmi_rg->fmbm_pp[port_id - 1]);
}

uint8_t fman_get_num_of_dmas(struct fman_bmi_regs *bmi_rg, uint8_t port_id)
{
	uint32_t tmp;

    if ((port_id > 63) || (port_id < 1))
            return 0;

	tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]);
	return (uint8_t)(((tmp & BMI_NUM_OF_DMAS_MASK) >>
			BMI_NUM_OF_DMAS_SHIFT) + 1);
}

uint8_t fman_get_num_extra_dmas(struct fman_bmi_regs *bmi_rg, uint8_t port_id)
{
	uint32_t tmp;

	if ((port_id > 63) || (port_id < 1))
	        return 0;

	tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]);
	return (uint8_t)((tmp & BMI_NUM_OF_EXTRA_DMAS_MASK) >>
			BMI_EXTRA_NUM_OF_DMAS_SHIFT);
}

void fman_set_num_of_open_dmas(struct fman_bmi_regs *bmi_rg,
				uint8_t port_id,
				uint8_t num_open_dmas,
				uint8_t num_extra_open_dmas,
				uint8_t total_num_dmas)
{
	uint32_t tmp = 0;

	if ((port_id > 63) || (port_id < 1))
	    return;

	/* calculate reg */
	tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]) &
			~(BMI_NUM_OF_DMAS_MASK | BMI_NUM_OF_EXTRA_DMAS_MASK);
	tmp |= (uint32_t)(((num_open_dmas-1) << BMI_NUM_OF_DMAS_SHIFT) |
			(num_extra_open_dmas << BMI_EXTRA_NUM_OF_DMAS_SHIFT));
	iowrite32be(tmp, &bmi_rg->fmbm_pp[port_id - 1]);

	/* update total num of DMA's with committed number of open DMAS,
	 * and max uncommitted pool. */
    if (total_num_dmas)
    {
        tmp = ioread32be(&bmi_rg->fmbm_cfg2) & ~BMI_CFG2_DMAS_MASK;
        tmp |= (uint32_t)(total_num_dmas - 1) << BMI_CFG2_DMAS_SHIFT;
        iowrite32be(tmp, &bmi_rg->fmbm_cfg2);
    }
}

void fman_set_vsp_window(struct fman_bmi_regs *bmi_rg,
			    	     uint8_t port_id,
				         uint8_t base_storage_profile,
				         uint8_t log2_num_of_profiles)
{
	uint32_t tmp = 0;
	if ((port_id > 63) || (port_id < 1))
	    return;

    tmp = ioread32be(&bmi_rg->fmbm_spliodn[port_id-1]);
    tmp |= (uint32_t)((uint32_t)base_storage_profile & 0x3f) << 16;
    tmp |= (uint32_t)log2_num_of_profiles << 28;
    iowrite32be(tmp, &bmi_rg->fmbm_spliodn[port_id-1]);
}

void fman_set_congestion_group_pfc_priority(uint32_t *cpg_rg,
                                            uint32_t congestion_group_id,
                                            uint8_t priority_bit_map,
                                            uint32_t reg_num)
{
	uint32_t offset, tmp = 0;

    offset  = (congestion_group_id%4)*8;

    tmp = ioread32be(&cpg_rg[reg_num]);
    tmp &= ~(0xFF<<offset);
    tmp |= (uint32_t)priority_bit_map << offset;

    iowrite32be(tmp,&cpg_rg[reg_num]);
}

/*****************************************************************************/
/*                      API Init unit functions                              */
/*****************************************************************************/
void fman_defconfig(struct fman_cfg *cfg, bool is_master)
{
    memset(cfg, 0, sizeof(struct fman_cfg));

    cfg->catastrophic_err               = DEFAULT_CATASTROPHIC_ERR;
    cfg->dma_err                        = DEFAULT_DMA_ERR;
    cfg->halt_on_external_activ         = DEFAULT_HALT_ON_EXTERNAL_ACTIVATION;
    cfg->halt_on_unrecov_ecc_err        = DEFAULT_HALT_ON_UNRECOVERABLE_ECC_ERROR;
    cfg->en_iram_test_mode              = FALSE;
    cfg->en_muram_test_mode             = FALSE;
    cfg->external_ecc_rams_enable       = DEFAULT_EXTERNAL_ECC_RAMS_ENABLE;

	if (!is_master)
	    return;

    cfg->dma_aid_override               = DEFAULT_AID_OVERRIDE;
    cfg->dma_aid_mode                   = DEFAULT_AID_MODE;
    cfg->dma_comm_qtsh_clr_emer         = DEFAULT_DMA_COMM_Q_LOW;
    cfg->dma_comm_qtsh_asrt_emer        = DEFAULT_DMA_COMM_Q_HIGH;
    cfg->dma_cache_override             = DEFAULT_CACHE_OVERRIDE;
    cfg->dma_cam_num_of_entries         = DEFAULT_DMA_CAM_NUM_OF_ENTRIES;
    cfg->dma_dbg_cnt_mode               = DEFAULT_DMA_DBG_CNT_MODE;
    cfg->dma_en_emergency               = DEFAULT_DMA_EN_EMERGENCY;
    cfg->dma_sos_emergency              = DEFAULT_DMA_SOS_EMERGENCY;
    cfg->dma_watchdog                   = DEFAULT_DMA_WATCHDOG;
    cfg->dma_en_emergency_smoother      = DEFAULT_DMA_EN_EMERGENCY_SMOOTHER;
    cfg->dma_emergency_switch_counter   = DEFAULT_DMA_EMERGENCY_SWITCH_COUNTER;
    cfg->disp_limit_tsh                 = DEFAULT_DISP_LIMIT;
    cfg->prs_disp_tsh                   = DEFAULT_PRS_DISP_TH;
    cfg->plcr_disp_tsh                  = DEFAULT_PLCR_DISP_TH;
    cfg->kg_disp_tsh                    = DEFAULT_KG_DISP_TH;
    cfg->bmi_disp_tsh                   = DEFAULT_BMI_DISP_TH;
    cfg->qmi_enq_disp_tsh               = DEFAULT_QMI_ENQ_DISP_TH;
    cfg->qmi_deq_disp_tsh               = DEFAULT_QMI_DEQ_DISP_TH;
    cfg->fm_ctl1_disp_tsh               = DEFAULT_FM_CTL1_DISP_TH;
    cfg->fm_ctl2_disp_tsh               = DEFAULT_FM_CTL2_DISP_TH;
 
	cfg->pedantic_dma                   = FALSE;
	cfg->tnum_aging_period              = DEFAULT_TNUM_AGING_PERIOD;
	cfg->dma_stop_on_bus_error          = FALSE;
	cfg->qmi_deq_option_support         = FALSE;
}

void fman_regconfig(struct fman_rg *fman_rg, struct fman_cfg *cfg)
{
	uint32_t tmp_reg;

    /* read the values from the registers as they are initialized by the HW with
     * the required values.
     */
    tmp_reg = ioread32be(&fman_rg->bmi_rg->fmbm_cfg1);
    cfg->total_fifo_size =
        (((tmp_reg & BMI_TOTAL_FIFO_SIZE_MASK) >> BMI_CFG1_FIFO_SIZE_SHIFT) + 1) * FMAN_BMI_FIFO_UNITS;

    tmp_reg = ioread32be(&fman_rg->bmi_rg->fmbm_cfg2);
    cfg->total_num_of_tasks =
        (uint8_t)(((tmp_reg & BMI_TOTAL_NUM_OF_TASKS_MASK) >> BMI_CFG2_TASKS_SHIFT) + 1);

    tmp_reg = ioread32be(&fman_rg->dma_rg->fmdmtr);
    cfg->dma_comm_qtsh_asrt_emer = (uint8_t)(tmp_reg >> DMA_THRESH_COMMQ_SHIFT);

    tmp_reg = ioread32be(&fman_rg->dma_rg->fmdmhy);
    cfg->dma_comm_qtsh_clr_emer  = (uint8_t)(tmp_reg >> DMA_THRESH_COMMQ_SHIFT);

    tmp_reg = ioread32be(&fman_rg->dma_rg->fmdmmr);
    cfg->dma_cache_override      = (enum fman_dma_cache_override)((tmp_reg & DMA_MODE_CACHE_OR_MASK) >> DMA_MODE_CACHE_OR_SHIFT);
    cfg->dma_cam_num_of_entries  = (uint8_t)((((tmp_reg & DMA_MODE_CEN_MASK) >> DMA_MODE_CEN_SHIFT) +1)*DMA_CAM_UNITS);
    cfg->dma_aid_override        = (bool)((tmp_reg & DMA_MODE_AID_OR)? TRUE:FALSE);
    cfg->dma_dbg_cnt_mode        = (enum fman_dma_dbg_cnt_mode)((tmp_reg & DMA_MODE_DBG_MASK) >> DMA_MODE_DBG_SHIFT);
    cfg->dma_en_emergency        = (bool)((tmp_reg & DMA_MODE_EB)? TRUE : FALSE);

    tmp_reg = ioread32be(&fman_rg->fpm_rg->fmfp_mxd);
    cfg->disp_limit_tsh          = (uint8_t)((tmp_reg & FPM_DISP_LIMIT_MASK) >> FPM_DISP_LIMIT_SHIFT);

    tmp_reg = ioread32be(&fman_rg->fpm_rg->fmfp_dist1);
    cfg->prs_disp_tsh            = (uint8_t)((tmp_reg & FPM_THR1_PRS_MASK ) >> FPM_THR1_PRS_SHIFT);
    cfg->plcr_disp_tsh           = (uint8_t)((tmp_reg & FPM_THR1_KG_MASK ) >> FPM_THR1_KG_SHIFT);
    cfg->kg_disp_tsh             = (uint8_t)((tmp_reg & FPM_THR1_PLCR_MASK ) >> FPM_THR1_PLCR_SHIFT);
    cfg->bmi_disp_tsh            = (uint8_t)((tmp_reg & FPM_THR1_BMI_MASK ) >> FPM_THR1_BMI_SHIFT);

    tmp_reg = ioread32be(&fman_rg->fpm_rg->fmfp_dist2);
    cfg->qmi_enq_disp_tsh        = (uint8_t)((tmp_reg & FPM_THR2_QMI_ENQ_MASK ) >> FPM_THR2_QMI_ENQ_SHIFT);
    cfg->qmi_deq_disp_tsh        = (uint8_t)((tmp_reg & FPM_THR2_QMI_DEQ_MASK ) >> FPM_THR2_QMI_DEQ_SHIFT);
    cfg->fm_ctl1_disp_tsh        = (uint8_t)((tmp_reg & FPM_THR2_FM_CTL1_MASK ) >> FPM_THR2_FM_CTL1_SHIFT);
    cfg->fm_ctl2_disp_tsh        = (uint8_t)((tmp_reg & FPM_THR2_FM_CTL2_MASK ) >> FPM_THR2_FM_CTL2_SHIFT);

    tmp_reg = ioread32be(&fman_rg->dma_rg->fmdmsetr);
    cfg->dma_sos_emergency       = tmp_reg;

    tmp_reg = ioread32be(&fman_rg->dma_rg->fmdmwcr);
    cfg->dma_watchdog            = tmp_reg/cfg->clk_freq;

    tmp_reg = ioread32be(&fman_rg->dma_rg->fmdmemsr);
    cfg->dma_en_emergency_smoother = (bool)((tmp_reg & DMA_EMSR_EMSTR_MASK)? TRUE : FALSE);
    cfg->dma_emergency_switch_counter = (tmp_reg & DMA_EMSR_EMSTR_MASK);
}

void fman_reset(struct fman_fpm_regs *fpm_rg)
{
	iowrite32be(FPM_RSTC_FM_RESET, &fpm_rg->fm_rstc);
}

/**************************************************************************//**
 @Function      FM_Init

 @Description   Initializes the FM module

 @Param[in]     h_Fm - FM module descriptor

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
int fman_dma_init(struct fman_dma_regs *dma_rg, struct fman_cfg *cfg)
{
	uint32_t    tmp_reg;

	/**********************/
	/* Init DMA Registers */
	/**********************/
	/* clear status reg events */
	/* oren - check!!!  */
	tmp_reg = (DMA_STATUS_BUS_ERR | DMA_STATUS_READ_ECC |
			DMA_STATUS_SYSTEM_WRITE_ECC | DMA_STATUS_FM_WRITE_ECC);
	iowrite32be(ioread32be(&dma_rg->fmdmsr) | tmp_reg,
			&dma_rg->fmdmsr);

	/* configure mode register */
	tmp_reg = 0;
	tmp_reg |= cfg->dma_cache_override << DMA_MODE_CACHE_OR_SHIFT;
	if (cfg->dma_aid_override)
		tmp_reg |= DMA_MODE_AID_OR;
	if (cfg->exceptions & FMAN_EX_DMA_BUS_ERROR)
		tmp_reg |= DMA_MODE_BER;
	if ((cfg->exceptions & FMAN_EX_DMA_SYSTEM_WRITE_ECC) |
		(cfg->exceptions & FMAN_EX_DMA_READ_ECC) |
		(cfg->exceptions & FMAN_EX_DMA_FM_WRITE_ECC))
		tmp_reg |= DMA_MODE_ECC;
	if (cfg->dma_stop_on_bus_error)
		tmp_reg |= DMA_MODE_SBER;
	if(cfg->dma_axi_dbg_num_of_beats)
	    tmp_reg |= (uint32_t)(DMA_MODE_AXI_DBG_MASK &
		           ((cfg->dma_axi_dbg_num_of_beats - 1) << DMA_MODE_AXI_DBG_SHIFT));

	if (cfg->dma_en_emergency) {
		tmp_reg |= cfg->dma_emergency_bus_select;
		tmp_reg |= cfg->dma_emergency_level << DMA_MODE_EMER_LVL_SHIFT;
	if (cfg->dma_en_emergency_smoother)
		iowrite32be(cfg->dma_emergency_switch_counter,
				&dma_rg->fmdmemsr);
	}
	tmp_reg |= ((cfg->dma_cam_num_of_entries / DMA_CAM_UNITS) - 1) <<
			DMA_MODE_CEN_SHIFT;
	tmp_reg |= DMA_MODE_SECURE_PROT;
	tmp_reg |= cfg->dma_dbg_cnt_mode << DMA_MODE_DBG_SHIFT;
	tmp_reg |= cfg->dma_aid_mode << DMA_MODE_AID_MODE_SHIFT;

	if (cfg->pedantic_dma)
		tmp_reg |= DMA_MODE_EMER_READ;

	iowrite32be(tmp_reg, &dma_rg->fmdmmr);

	/* configure thresholds register */
	tmp_reg = ((uint32_t)cfg->dma_comm_qtsh_asrt_emer <<
			DMA_THRESH_COMMQ_SHIFT) |
			((uint32_t)cfg->dma_read_buf_tsh_asrt_emer <<
			DMA_THRESH_READ_INT_BUF_SHIFT) |
			((uint32_t)cfg->dma_write_buf_tsh_asrt_emer);

	iowrite32be(tmp_reg, &dma_rg->fmdmtr);

	/* configure hysteresis register */
	tmp_reg = ((uint32_t)cfg->dma_comm_qtsh_clr_emer <<
		DMA_THRESH_COMMQ_SHIFT) |
		((uint32_t)cfg->dma_read_buf_tsh_clr_emer <<
		DMA_THRESH_READ_INT_BUF_SHIFT) |
		((uint32_t)cfg->dma_write_buf_tsh_clr_emer);

	iowrite32be(tmp_reg, &dma_rg->fmdmhy);

	/* configure emergency threshold */
	iowrite32be(cfg->dma_sos_emergency, &dma_rg->fmdmsetr);

	/* configure Watchdog */
	iowrite32be((cfg->dma_watchdog * cfg->clk_freq),
			&dma_rg->fmdmwcr);

	iowrite32be(cfg->cam_base_addr, &dma_rg->fmdmebcr);

	return 0;
}

int fman_fpm_init(struct fman_fpm_regs *fpm_rg, struct fman_cfg *cfg)
{
	uint32_t tmp_reg;
	int i;

	/**********************/
	/* Init FPM Registers */
	/**********************/
	tmp_reg = (uint32_t)(cfg->disp_limit_tsh << FPM_DISP_LIMIT_SHIFT);
	iowrite32be(tmp_reg, &fpm_rg->fmfp_mxd);

	tmp_reg = (((uint32_t)cfg->prs_disp_tsh << FPM_THR1_PRS_SHIFT) |
		((uint32_t)cfg->kg_disp_tsh << FPM_THR1_KG_SHIFT) |
		((uint32_t)cfg->plcr_disp_tsh << FPM_THR1_PLCR_SHIFT) |
		((uint32_t)cfg->bmi_disp_tsh << FPM_THR1_BMI_SHIFT));
	iowrite32be(tmp_reg, &fpm_rg->fmfp_dist1);

	tmp_reg = (((uint32_t)cfg->qmi_enq_disp_tsh << FPM_THR2_QMI_ENQ_SHIFT) |
		((uint32_t)cfg->qmi_deq_disp_tsh << FPM_THR2_QMI_DEQ_SHIFT) |
		((uint32_t)cfg->fm_ctl1_disp_tsh << FPM_THR2_FM_CTL1_SHIFT) |
		((uint32_t)cfg->fm_ctl2_disp_tsh << FPM_THR2_FM_CTL2_SHIFT));
	iowrite32be(tmp_reg, &fpm_rg->fmfp_dist2);

	/* define exceptions and error behavior */
	tmp_reg = 0;
	/* Clear events */
	tmp_reg |= (FPM_EV_MASK_STALL | FPM_EV_MASK_DOUBLE_ECC |
		FPM_EV_MASK_SINGLE_ECC);
	/* enable interrupts */
	if (cfg->exceptions & FMAN_EX_FPM_STALL_ON_TASKS)
		tmp_reg |= FPM_EV_MASK_STALL_EN;
	if (cfg->exceptions & FMAN_EX_FPM_SINGLE_ECC)
		tmp_reg |= FPM_EV_MASK_SINGLE_ECC_EN;
	if (cfg->exceptions & FMAN_EX_FPM_DOUBLE_ECC)
		tmp_reg |= FPM_EV_MASK_DOUBLE_ECC_EN;
	tmp_reg |= (cfg->catastrophic_err  << FPM_EV_MASK_CAT_ERR_SHIFT);
	tmp_reg |= (cfg->dma_err << FPM_EV_MASK_DMA_ERR_SHIFT);
	if (!cfg->halt_on_external_activ)
		tmp_reg |= FPM_EV_MASK_EXTERNAL_HALT;
	if (!cfg->halt_on_unrecov_ecc_err)
		tmp_reg |= FPM_EV_MASK_ECC_ERR_HALT;
	iowrite32be(tmp_reg, &fpm_rg->fmfp_ee);

	/* clear all fmCtls event registers */
	for (i = 0; i < cfg->num_of_fman_ctrl_evnt_regs; i++)
		iowrite32be(0xFFFFFFFF, &fpm_rg->fmfp_cev[i]);

	/* RAM ECC -  enable and clear events*/
	/* first we need to clear all parser memory,
	 * as it is uninitialized and may cause ECC errors */
	/* event bits */
	tmp_reg = (FPM_RAM_MURAM_ECC | FPM_RAM_IRAM_ECC);
	/* Rams enable not effected by RCR bit, but by a COP configuration */
	if (cfg->external_ecc_rams_enable)
		tmp_reg |= FPM_RAM_RAMS_ECC_EN_SRC_SEL;

	/* enable test mode */
	if (cfg->en_muram_test_mode)
		tmp_reg |= FPM_RAM_MURAM_TEST_ECC;
	if (cfg->en_iram_test_mode)
		tmp_reg |= FPM_RAM_IRAM_TEST_ECC;
	iowrite32be(tmp_reg, &fpm_rg->fm_rcr);

	tmp_reg = 0;
	if (cfg->exceptions & FMAN_EX_IRAM_ECC) {
		tmp_reg |= FPM_IRAM_ECC_ERR_EX_EN;
		fman_enable_rams_ecc(fpm_rg);
	}
	if (cfg->exceptions & FMAN_EX_NURAM_ECC) {
		tmp_reg |= FPM_MURAM_ECC_ERR_EX_EN;
		fman_enable_rams_ecc(fpm_rg);
	}
	iowrite32be(tmp_reg, &fpm_rg->fm_rie);

	return 0;
}

int fman_bmi_init(struct fman_bmi_regs *bmi_rg, struct fman_cfg *cfg)
{
	uint32_t tmp_reg;

	/**********************/
	/* Init BMI Registers */
	/**********************/

	/* define common resources */
	tmp_reg = cfg->fifo_base_addr;
	tmp_reg = tmp_reg / BMI_FIFO_ALIGN;

	tmp_reg |= ((cfg->total_fifo_size / FMAN_BMI_FIFO_UNITS - 1) <<
			BMI_CFG1_FIFO_SIZE_SHIFT);
	iowrite32be(tmp_reg, &bmi_rg->fmbm_cfg1);

	tmp_reg = ((uint32_t)(cfg->total_num_of_tasks - 1) <<
			BMI_CFG2_TASKS_SHIFT);
	/* num of DMA's will be dynamically updated when each port is set */
	iowrite32be(tmp_reg, &bmi_rg->fmbm_cfg2);

	/* define unmaskable exceptions, enable and clear events */
	tmp_reg = 0;
	iowrite32be(BMI_ERR_INTR_EN_LIST_RAM_ECC |
			BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC |
			BMI_ERR_INTR_EN_STATISTICS_RAM_ECC |
			BMI_ERR_INTR_EN_DISPATCH_RAM_ECC,
			&bmi_rg->fmbm_ievr);

	if (cfg->exceptions & FMAN_EX_BMI_LIST_RAM_ECC)
		tmp_reg |= BMI_ERR_INTR_EN_LIST_RAM_ECC;
	if (cfg->exceptions & FMAN_EX_BMI_PIPELINE_ECC)
		tmp_reg |= BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC;
	if (cfg->exceptions & FMAN_EX_BMI_STATISTICS_RAM_ECC)
		tmp_reg |= BMI_ERR_INTR_EN_STATISTICS_RAM_ECC;
	if (cfg->exceptions & FMAN_EX_BMI_DISPATCH_RAM_ECC)
		tmp_reg |= BMI_ERR_INTR_EN_DISPATCH_RAM_ECC;
	iowrite32be(tmp_reg, &bmi_rg->fmbm_ier);

	return 0;
}

int fman_qmi_init(struct fman_qmi_regs *qmi_rg, struct fman_cfg *cfg)
{
	uint32_t tmp_reg;
	uint16_t period_in_fm_clocks;
	uint8_t remainder;
	/**********************/
	/* Init QMI Registers */
	/**********************/
	/* Clear error interrupt events */

	iowrite32be(QMI_ERR_INTR_EN_DOUBLE_ECC | QMI_ERR_INTR_EN_DEQ_FROM_DEF,
			&qmi_rg->fmqm_eie);
	tmp_reg = 0;
	if (cfg->exceptions & FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID)
		tmp_reg |= QMI_ERR_INTR_EN_DEQ_FROM_DEF;
	if (cfg->exceptions & FMAN_EX_QMI_DOUBLE_ECC)
		tmp_reg |= QMI_ERR_INTR_EN_DOUBLE_ECC;
	/* enable events */
	iowrite32be(tmp_reg, &qmi_rg->fmqm_eien);

	if (cfg->tnum_aging_period) {
		/* tnum_aging_period is in units of usec, p_FmClockFreq in Mhz */
		period_in_fm_clocks = (uint16_t)
				(cfg->tnum_aging_period * cfg->clk_freq);
		/* period_in_fm_clocks must be a 64 multiply */
		remainder = (uint8_t)(period_in_fm_clocks % 64);
		if (remainder)
			tmp_reg = (uint32_t)((period_in_fm_clocks / 64) + 1);
		else{
			tmp_reg = (uint32_t)(period_in_fm_clocks / 64);
			if (!tmp_reg)
				tmp_reg = 1;
		}
		tmp_reg <<= QMI_TAPC_TAP;
		iowrite32be(tmp_reg, &qmi_rg->fmqm_tapc);
	}
	tmp_reg = 0;
	/* Clear interrupt events */
	iowrite32be(QMI_INTR_EN_SINGLE_ECC, &qmi_rg->fmqm_ie);
	if (cfg->exceptions & FMAN_EX_QMI_SINGLE_ECC)
		tmp_reg |= QMI_INTR_EN_SINGLE_ECC;
	/* enable events */
	iowrite32be(tmp_reg, &qmi_rg->fmqm_ien);

	return 0;
}

int fman_enable(struct fman_rg *fman_rg, struct fman_cfg *cfg)
{
	uint32_t cfg_reg = 0;

	/**********************/
	/* Enable all modules */
	/**********************/
	/* clear & enable global counters  - calculate reg and save for later,
	because it's the same reg for QMI enable */
	cfg_reg = QMI_CFG_EN_COUNTERS;
	if (cfg->qmi_deq_option_support)
		cfg_reg |= (uint32_t)(((cfg->qmi_def_tnums_thresh) << 8) |
				(uint32_t)cfg->qmi_def_tnums_thresh);

	iowrite32be(BMI_INIT_START, &fman_rg->bmi_rg->fmbm_init);
	iowrite32be(cfg_reg | QMI_CFG_ENQ_EN | QMI_CFG_DEQ_EN,
			&fman_rg->qmi_rg->fmqm_gc);

	return 0;
}

void fman_free_resources(struct fman_rg *fman_rg)
{
	/* disable BMI and QMI */
	iowrite32be(0, &fman_rg->bmi_rg->fmbm_init);
	iowrite32be(0, &fman_rg->qmi_rg->fmqm_gc);

	/* release BMI resources */
	iowrite32be(0, &fman_rg->bmi_rg->fmbm_cfg2);
	iowrite32be(0, &fman_rg->bmi_rg->fmbm_cfg1);

	/* disable ECC */
	iowrite32be(0, &fman_rg->fpm_rg->fm_rcr);
}

/****************************************************/
/*       API Run-time Control uint functions        */
/****************************************************/
uint32_t fman_get_normal_pending(struct fman_fpm_regs *fpm_rg)
{
	return ioread32be(&fpm_rg->fm_npi);
}

uint32_t fman_get_controller_event(struct fman_fpm_regs *fpm_rg, uint8_t reg_id)
{
	uint32_t event;

	event = ioread32be(&fpm_rg->fmfp_fcev[reg_id]) &
			ioread32be(&fpm_rg->fmfp_cee[reg_id]);
	iowrite32be(event, &fpm_rg->fmfp_cev[reg_id]);

	return event;
}

uint32_t fman_get_error_pending(struct fman_fpm_regs *fpm_rg)
{
	return ioread32be(&fpm_rg->fm_epi);
}

void fman_set_ports_bandwidth(struct fman_bmi_regs *bmi_rg, uint8_t *weights)
{
	int i;
	uint8_t shift;
	uint32_t tmp = 0;

	for (i = 0; i < 64; i++) {
		if (weights[i] > 1) { /* no need to write 1 since it is 0 */
			/* Add this port to tmp_reg */
			/* (each 8 ports result in one register)*/
			shift = (uint8_t)(32 - 4 * ((i % 8) + 1));
			tmp |= ((weights[i] - 1) << shift);
		}
		if (i % 8 == 7) { /* last in this set */
			iowrite32be(tmp, &bmi_rg->fmbm_arb[i / 8]);
			tmp = 0;
		}
	}
}

void fman_enable_rams_ecc(struct fman_fpm_regs *fpm_rg)
{
	uint32_t tmp;

	tmp = ioread32be(&fpm_rg->fm_rcr);
	if (tmp & FPM_RAM_RAMS_ECC_EN_SRC_SEL)
		iowrite32be(tmp | FPM_RAM_IRAM_ECC_EN,
				&fpm_rg->fm_rcr);
	else
		iowrite32be(tmp | FPM_RAM_RAMS_ECC_EN |
				FPM_RAM_IRAM_ECC_EN,
				&fpm_rg->fm_rcr);
}

void fman_disable_rams_ecc(struct fman_fpm_regs *fpm_rg)
{
	uint32_t tmp;

	tmp = ioread32be(&fpm_rg->fm_rcr);
	if (tmp & FPM_RAM_RAMS_ECC_EN_SRC_SEL)
		iowrite32be(tmp & ~FPM_RAM_IRAM_ECC_EN,
				&fpm_rg->fm_rcr);
	else
		iowrite32be(tmp & ~(FPM_RAM_RAMS_ECC_EN | FPM_RAM_IRAM_ECC_EN),
				&fpm_rg->fm_rcr);
}

int fman_set_exception(struct fman_rg *fman_rg,
			enum fman_exceptions exception,
			bool enable)
{
	uint32_t tmp;

	switch (exception) {
	case(E_FMAN_EX_DMA_BUS_ERROR):
		tmp = ioread32be(&fman_rg->dma_rg->fmdmmr);
		if (enable)
			tmp |= DMA_MODE_BER;
		else
			tmp &= ~DMA_MODE_BER;
		/* disable bus error */
		iowrite32be(tmp, &fman_rg->dma_rg->fmdmmr);
		break;
	case(E_FMAN_EX_DMA_READ_ECC):
	case(E_FMAN_EX_DMA_SYSTEM_WRITE_ECC):
	case(E_FMAN_EX_DMA_FM_WRITE_ECC):
		tmp = ioread32be(&fman_rg->dma_rg->fmdmmr);
		if (enable)
			tmp |= DMA_MODE_ECC;
		else
			tmp &= ~DMA_MODE_ECC;
		iowrite32be(tmp, &fman_rg->dma_rg->fmdmmr);
		break;
	case(E_FMAN_EX_FPM_STALL_ON_TASKS):
		tmp = ioread32be(&fman_rg->fpm_rg->fmfp_ee);
		if (enable)
			tmp |= FPM_EV_MASK_STALL_EN;
		else
			tmp &= ~FPM_EV_MASK_STALL_EN;
		iowrite32be(tmp, &fman_rg->fpm_rg->fmfp_ee);
		break;
	case(E_FMAN_EX_FPM_SINGLE_ECC):
		tmp = ioread32be(&fman_rg->fpm_rg->fmfp_ee);
		if (enable)
			tmp |= FPM_EV_MASK_SINGLE_ECC_EN;
		else
			tmp &= ~FPM_EV_MASK_SINGLE_ECC_EN;
		iowrite32be(tmp, &fman_rg->fpm_rg->fmfp_ee);
		break;
	case(E_FMAN_EX_FPM_DOUBLE_ECC):
		tmp = ioread32be(&fman_rg->fpm_rg->fmfp_ee);
		if (enable)
			tmp |= FPM_EV_MASK_DOUBLE_ECC_EN;
		else
			tmp &= ~FPM_EV_MASK_DOUBLE_ECC_EN;
		iowrite32be(tmp, &fman_rg->fpm_rg->fmfp_ee);
		break;
	case(E_FMAN_EX_QMI_SINGLE_ECC):
		tmp = ioread32be(&fman_rg->qmi_rg->fmqm_ien);
		if (enable)
			tmp |= QMI_INTR_EN_SINGLE_ECC;
		else
			tmp &= ~QMI_INTR_EN_SINGLE_ECC;
		iowrite32be(tmp, &fman_rg->qmi_rg->fmqm_ien);
		break;
	case(E_FMAN_EX_QMI_DOUBLE_ECC):
		tmp = ioread32be(&fman_rg->qmi_rg->fmqm_eien);
		if (enable)
			tmp |= QMI_ERR_INTR_EN_DOUBLE_ECC;
		else
			tmp &= ~QMI_ERR_INTR_EN_DOUBLE_ECC;
		iowrite32be(tmp, &fman_rg->qmi_rg->fmqm_eien);
		break;
	case(E_FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID):
		tmp = ioread32be(&fman_rg->qmi_rg->fmqm_eien);
		if (enable)
			tmp |= QMI_ERR_INTR_EN_DEQ_FROM_DEF;
		else
			tmp &= ~QMI_ERR_INTR_EN_DEQ_FROM_DEF;
		iowrite32be(tmp, &fman_rg->qmi_rg->fmqm_eien);
		break;
	case(E_FMAN_EX_BMI_LIST_RAM_ECC):
		tmp = ioread32be(&fman_rg->bmi_rg->fmbm_ier);
		if (enable)
			tmp |= BMI_ERR_INTR_EN_LIST_RAM_ECC;
		else
			tmp &= ~BMI_ERR_INTR_EN_LIST_RAM_ECC;
		iowrite32be(tmp, &fman_rg->bmi_rg->fmbm_ier);
		break;
	case(E_FMAN_EX_BMI_STORAGE_PROFILE_ECC):
		tmp = ioread32be(&fman_rg->bmi_rg->fmbm_ier);
		if (enable)
			tmp |= BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC;
		else
			tmp &= ~BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC;
		iowrite32be(tmp, &fman_rg->bmi_rg->fmbm_ier);
		break;
	case(E_FMAN_EX_BMI_STATISTICS_RAM_ECC):
		tmp = ioread32be(&fman_rg->bmi_rg->fmbm_ier);
		if (enable)
			tmp |= BMI_ERR_INTR_EN_STATISTICS_RAM_ECC;
		else
			tmp &= ~BMI_ERR_INTR_EN_STATISTICS_RAM_ECC;
		iowrite32be(tmp, &fman_rg->bmi_rg->fmbm_ier);
		break;
	case(E_FMAN_EX_BMI_DISPATCH_RAM_ECC):
		tmp = ioread32be(&fman_rg->bmi_rg->fmbm_ier);
		if (enable)
			tmp |= BMI_ERR_INTR_EN_DISPATCH_RAM_ECC;
		else
			tmp &= ~BMI_ERR_INTR_EN_DISPATCH_RAM_ECC;
		iowrite32be(tmp, &fman_rg->bmi_rg->fmbm_ier);
		break;
	case(E_FMAN_EX_IRAM_ECC):
		tmp = ioread32be(&fman_rg->fpm_rg->fm_rie);
		if (enable) {
			/* enable ECC if not enabled */
			fman_enable_rams_ecc(fman_rg->fpm_rg);
			/* enable ECC interrupts */
			tmp |= FPM_IRAM_ECC_ERR_EX_EN;
		} else {
			/* ECC mechanism may be disabled,
			 * depending on driver status  */
			fman_disable_rams_ecc(fman_rg->fpm_rg);
			tmp &= ~FPM_IRAM_ECC_ERR_EX_EN;
		}
		iowrite32be(tmp, &fman_rg->fpm_rg->fm_rie);
		break;
	case(E_FMAN_EX_MURAM_ECC):
		tmp = ioread32be(&fman_rg->fpm_rg->fm_rie);
		if (enable) {
			/* enable ECC if not enabled */
			fman_enable_rams_ecc(fman_rg->fpm_rg);
			/* enable ECC interrupts */
			tmp |= FPM_MURAM_ECC_ERR_EX_EN;
		} else {
			/* ECC mechanism may be disabled,
			 * depending on driver status  */
			fman_disable_rams_ecc(fman_rg->fpm_rg);
			tmp &= ~FPM_MURAM_ECC_ERR_EX_EN;
		}
		iowrite32be(tmp, &fman_rg->fpm_rg->fm_rie);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

void fman_get_revision(struct fman_fpm_regs *fpm_rg,
			uint8_t *major,
			uint8_t *minor)
{
	uint32_t tmp;

	tmp = ioread32be(&fpm_rg->fm_ip_rev_1);
	*major = (uint8_t)((tmp & FPM_REV1_MAJOR_MASK) >> FPM_REV1_MAJOR_SHIFT);
	*minor = (uint8_t)((tmp & FPM_REV1_MINOR_MASK) >> FPM_REV1_MINOR_SHIFT);

}

uint32_t fman_get_counter(struct fman_rg *fman_rg,
				enum fman_counters reg_name)
{
	uint32_t ret_val;

	switch (reg_name) {
	case(E_FMAN_COUNTERS_ENQ_TOTAL_FRAME):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_etfc);
		break;
	case(E_FMAN_COUNTERS_DEQ_TOTAL_FRAME):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_dtfc);
		break;
	case(E_FMAN_COUNTERS_DEQ_0):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_dc0);
		break;
	case(E_FMAN_COUNTERS_DEQ_1):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_dc1);
		break;
	case(E_FMAN_COUNTERS_DEQ_2):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_dc2);
		break;
	case(E_FMAN_COUNTERS_DEQ_3):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_dc3);
		break;
	case(E_FMAN_COUNTERS_DEQ_FROM_DEFAULT):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_dfdc);
		break;
	case(E_FMAN_COUNTERS_DEQ_FROM_CONTEXT):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_dfcc);
		break;
	case(E_FMAN_COUNTERS_DEQ_FROM_FD):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_dffc);
		break;
	case(E_FMAN_COUNTERS_DEQ_CONFIRM):
		ret_val = ioread32be(&fman_rg->qmi_rg->fmqm_dcc);
		break;
	default:
		ret_val = 0;
	}
	return ret_val;
}

int fman_modify_counter(struct fman_rg *fman_rg,
			enum fman_counters reg_name,
			uint32_t val)
{
	/* When applicable (when there is an 'enable counters' bit,
	 * check that counters are enabled */
	switch (reg_name) {
	case(E_FMAN_COUNTERS_ENQ_TOTAL_FRAME):
	case(E_FMAN_COUNTERS_DEQ_TOTAL_FRAME):
	case(E_FMAN_COUNTERS_DEQ_0):
	case(E_FMAN_COUNTERS_DEQ_1):
	case(E_FMAN_COUNTERS_DEQ_2):
	case(E_FMAN_COUNTERS_DEQ_3):
	case(E_FMAN_COUNTERS_DEQ_FROM_DEFAULT):
	case(E_FMAN_COUNTERS_DEQ_FROM_CONTEXT):
	case(E_FMAN_COUNTERS_DEQ_FROM_FD):
	case(E_FMAN_COUNTERS_DEQ_CONFIRM):
		if (!(ioread32be(&fman_rg->qmi_rg->fmqm_gc) &
				QMI_CFG_EN_COUNTERS))
			return -EINVAL;
		break;
	default:
		break;
	}
	/* Set counter */
	switch (reg_name) {
	case(E_FMAN_COUNTERS_ENQ_TOTAL_FRAME):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_etfc);
		break;
	case(E_FMAN_COUNTERS_DEQ_TOTAL_FRAME):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_dtfc);
		break;
	case(E_FMAN_COUNTERS_DEQ_0):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_dc0);
		break;
	case(E_FMAN_COUNTERS_DEQ_1):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_dc1);
		break;
	case(E_FMAN_COUNTERS_DEQ_2):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_dc2);
		break;
	case(E_FMAN_COUNTERS_DEQ_3):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_dc3);
		break;
	case(E_FMAN_COUNTERS_DEQ_FROM_DEFAULT):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_dfdc);
		break;
	case(E_FMAN_COUNTERS_DEQ_FROM_CONTEXT):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_dfcc);
		break;
	case(E_FMAN_COUNTERS_DEQ_FROM_FD):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_dffc);
		break;
	case(E_FMAN_COUNTERS_DEQ_CONFIRM):
		iowrite32be(val, &fman_rg->qmi_rg->fmqm_dcc);
		break;
	case(E_FMAN_COUNTERS_SEMAPHOR_ENTRY_FULL_REJECT):
		iowrite32be(val, &fman_rg->dma_rg->fmdmsefrc);
		break;
	case(E_FMAN_COUNTERS_SEMAPHOR_QUEUE_FULL_REJECT):
		iowrite32be(val, &fman_rg->dma_rg->fmdmsqfrc);
		break;
	case(E_FMAN_COUNTERS_SEMAPHOR_SYNC_REJECT):
		iowrite32be(val, &fman_rg->dma_rg->fmdmssrc);
		break;
	default:
		break;
	}
	return 0;
}

void fman_set_dma_emergency(struct fman_dma_regs *dma_rg,
				bool is_write,
				bool enable)
{
	uint32_t msk;

	msk = (uint32_t)(is_write ? DMA_MODE_EMER_WRITE : DMA_MODE_EMER_READ);

	if (enable)
		iowrite32be(ioread32be(&dma_rg->fmdmmr) | msk,
				&dma_rg->fmdmmr);
	else /* disable */
		iowrite32be(ioread32be(&dma_rg->fmdmmr) & ~msk,
				&dma_rg->fmdmmr);
}

void fman_set_dma_ext_bus_pri(struct fman_dma_regs *dma_rg, uint32_t pri)
{
	uint32_t tmp;

	tmp = ioread32be(&dma_rg->fmdmmr) |
			(pri << DMA_MODE_BUS_PRI_SHIFT);

	iowrite32be(tmp, &dma_rg->fmdmmr);
}

uint32_t fman_get_dma_status(struct fman_dma_regs *dma_rg)
{
	return ioread32be(&dma_rg->fmdmsr);
}

void fman_force_intr(struct fman_rg *fman_rg,
		enum fman_exceptions exception)
{
	switch (exception) {
	case E_FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID:
		iowrite32be(QMI_ERR_INTR_EN_DEQ_FROM_DEF,
				&fman_rg->qmi_rg->fmqm_eif);
		break;
	case E_FMAN_EX_QMI_SINGLE_ECC:
		iowrite32be(QMI_INTR_EN_SINGLE_ECC,
				&fman_rg->qmi_rg->fmqm_if);
		break;
	case E_FMAN_EX_QMI_DOUBLE_ECC:
		iowrite32be(QMI_ERR_INTR_EN_DOUBLE_ECC,
				&fman_rg->qmi_rg->fmqm_eif);
		break;
	case E_FMAN_EX_BMI_LIST_RAM_ECC:
		iowrite32be(BMI_ERR_INTR_EN_LIST_RAM_ECC,
				&fman_rg->bmi_rg->fmbm_ifr);
		break;
	case E_FMAN_EX_BMI_STORAGE_PROFILE_ECC:
		iowrite32be(BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC,
				&fman_rg->bmi_rg->fmbm_ifr);
		break;
	case E_FMAN_EX_BMI_STATISTICS_RAM_ECC:
		iowrite32be(BMI_ERR_INTR_EN_STATISTICS_RAM_ECC,
				&fman_rg->bmi_rg->fmbm_ifr);
		break;
	case E_FMAN_EX_BMI_DISPATCH_RAM_ECC:
		iowrite32be(BMI_ERR_INTR_EN_DISPATCH_RAM_ECC,
				&fman_rg->bmi_rg->fmbm_ifr);
		break;
	default:
		break;
	}
}

bool fman_is_qmi_halt_not_busy_state(struct fman_qmi_regs *qmi_rg)
{
	return (bool)!!(ioread32be(&qmi_rg->fmqm_gs) & QMI_GS_HALT_NOT_BUSY);
}
void fman_resume(struct fman_fpm_regs *fpm_rg)
{
	uint32_t tmp;

	tmp = ioread32be(&fpm_rg->fmfp_ee);
	/* clear tmp_reg event bits in order not to clear standing events */
	tmp &= ~(FPM_EV_MASK_DOUBLE_ECC |
			FPM_EV_MASK_STALL |
			FPM_EV_MASK_SINGLE_ECC);
	tmp |= FPM_EV_MASK_RELEASE_FM;

	iowrite32be(tmp, &fpm_rg->fmfp_ee);
}
