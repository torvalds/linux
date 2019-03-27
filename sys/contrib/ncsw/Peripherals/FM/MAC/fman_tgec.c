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


#include "fsl_fman_tgec.h"


void fman_tgec_set_mac_address(struct tgec_regs *regs, uint8_t *adr)
{
	uint32_t tmp0, tmp1;

	tmp0 = (uint32_t)(adr[0] |
			adr[1] << 8 |
			adr[2] << 16 |
			adr[3] << 24);
	tmp1 = (uint32_t)(adr[4] | adr[5] << 8);
	iowrite32be(tmp0, &regs->mac_addr_0);
	iowrite32be(tmp1, &regs->mac_addr_1);
}

void fman_tgec_reset_stat(struct tgec_regs *regs)
{
	uint32_t tmp;

	tmp = ioread32be(&regs->command_config);

	tmp |= CMD_CFG_STAT_CLR;

	iowrite32be(tmp, &regs->command_config);

	while (ioread32be(&regs->command_config) & CMD_CFG_STAT_CLR) ;
}

#define GET_TGEC_CNTR_64(bn) \
	(((uint64_t)ioread32be(&regs->bn ## _u) << 32) | \
			ioread32be(&regs->bn ## _l))

uint64_t fman_tgec_get_counter(struct tgec_regs *regs, enum tgec_counters reg_name)
{
	uint64_t ret_val;

	switch (reg_name) {
	case E_TGEC_COUNTER_R64:
		ret_val = GET_TGEC_CNTR_64(r64);
		break;
	case E_TGEC_COUNTER_R127:
		ret_val = GET_TGEC_CNTR_64(r127);
		break;
	case E_TGEC_COUNTER_R255:
		ret_val = GET_TGEC_CNTR_64(r255);
		break;
	case E_TGEC_COUNTER_R511:
		ret_val = GET_TGEC_CNTR_64(r511);
		break;
	case E_TGEC_COUNTER_R1023:
		ret_val = GET_TGEC_CNTR_64(r1023);
		break;
	case E_TGEC_COUNTER_R1518:
		ret_val = GET_TGEC_CNTR_64(r1518);
		break;
	case E_TGEC_COUNTER_R1519X:
		ret_val = GET_TGEC_CNTR_64(r1519x);
		break;
	case E_TGEC_COUNTER_TRFRG:
		ret_val = GET_TGEC_CNTR_64(trfrg);
		break;
	case E_TGEC_COUNTER_TRJBR:
		ret_val = GET_TGEC_CNTR_64(trjbr);
		break;
	case E_TGEC_COUNTER_RDRP:
		ret_val = GET_TGEC_CNTR_64(rdrp);
		break;
	case E_TGEC_COUNTER_RALN:
		ret_val = GET_TGEC_CNTR_64(raln);
		break;
	case E_TGEC_COUNTER_TRUND:
		ret_val = GET_TGEC_CNTR_64(trund);
		break;
	case E_TGEC_COUNTER_TROVR:
		ret_val = GET_TGEC_CNTR_64(trovr);
		break;
	case E_TGEC_COUNTER_RXPF:
		ret_val = GET_TGEC_CNTR_64(rxpf);
		break;
	case E_TGEC_COUNTER_TXPF:
		ret_val = GET_TGEC_CNTR_64(txpf);
		break;
	case E_TGEC_COUNTER_ROCT:
		ret_val = GET_TGEC_CNTR_64(roct);
		break;
	case E_TGEC_COUNTER_RMCA:
		ret_val = GET_TGEC_CNTR_64(rmca);
		break;
	case E_TGEC_COUNTER_RBCA:
		ret_val = GET_TGEC_CNTR_64(rbca);
		break;
	case E_TGEC_COUNTER_RPKT:
		ret_val = GET_TGEC_CNTR_64(rpkt);
		break;
	case E_TGEC_COUNTER_RUCA:
		ret_val = GET_TGEC_CNTR_64(ruca);
		break;
	case E_TGEC_COUNTER_RERR:
		ret_val = GET_TGEC_CNTR_64(rerr);
		break;
	case E_TGEC_COUNTER_TOCT:
		ret_val = GET_TGEC_CNTR_64(toct);
		break;
	case E_TGEC_COUNTER_TMCA:
		ret_val = GET_TGEC_CNTR_64(tmca);
		break;
	case E_TGEC_COUNTER_TBCA:
		ret_val = GET_TGEC_CNTR_64(tbca);
		break;
	case E_TGEC_COUNTER_TUCA:
		ret_val = GET_TGEC_CNTR_64(tuca);
		break;
	case E_TGEC_COUNTER_TERR:
		ret_val = GET_TGEC_CNTR_64(terr);
		break;
	default:
		ret_val = 0;
	}

	return ret_val;
}

void fman_tgec_enable(struct tgec_regs *regs, bool apply_rx, bool apply_tx)
{
	uint32_t tmp;

	tmp = ioread32be(&regs->command_config);
	if (apply_rx)
		tmp |= CMD_CFG_RX_EN;
	if (apply_tx)
		tmp |= CMD_CFG_TX_EN;
	iowrite32be(tmp, &regs->command_config);
}

void fman_tgec_disable(struct tgec_regs *regs, bool apply_rx, bool apply_tx)
{
	uint32_t tmp_reg_32;

	tmp_reg_32 = ioread32be(&regs->command_config);
	if (apply_rx)
		tmp_reg_32 &= ~CMD_CFG_RX_EN;
	if (apply_tx)
		tmp_reg_32 &= ~CMD_CFG_TX_EN;
	iowrite32be(tmp_reg_32, &regs->command_config);
}

void fman_tgec_set_promiscuous(struct tgec_regs *regs, bool val)
{
	uint32_t tmp;

	tmp = ioread32be(&regs->command_config);
	if (val)
		tmp |= CMD_CFG_PROMIS_EN;
	else
		tmp &= ~CMD_CFG_PROMIS_EN;
	iowrite32be(tmp, &regs->command_config);
}

void fman_tgec_reset_filter_table(struct tgec_regs *regs)
{
	uint32_t i;
	for (i = 0; i < 512; i++)
		iowrite32be(i & ~TGEC_HASH_MCAST_EN, &regs->hashtable_ctrl);
}

void fman_tgec_set_hash_table_entry(struct tgec_regs *regs, uint32_t crc)
{
    uint32_t hash = (crc >> TGEC_HASH_MCAST_SHIFT) & TGEC_HASH_ADR_MSK;        /* Take 9 MSB bits */
	iowrite32be(hash | TGEC_HASH_MCAST_EN, &regs->hashtable_ctrl);
}

void fman_tgec_set_hash_table(struct tgec_regs *regs, uint32_t value)
{
	iowrite32be(value, &regs->hashtable_ctrl);
}

void fman_tgec_set_tx_pause_frames(struct tgec_regs *regs, uint16_t pause_time)
{
	iowrite32be((uint32_t)pause_time, &regs->pause_quant);
}

void fman_tgec_set_rx_ignore_pause_frames(struct tgec_regs *regs, bool en)
{
	uint32_t tmp;

	tmp = ioread32be(&regs->command_config);
	if (en)
		tmp |= CMD_CFG_PAUSE_IGNORE;
	else
		tmp &= ~CMD_CFG_PAUSE_IGNORE;
	iowrite32be(tmp, &regs->command_config);
}

void fman_tgec_enable_1588_time_stamp(struct tgec_regs *regs, bool en)
{
	uint32_t tmp;

	tmp = ioread32be(&regs->command_config);
	if (en)
		tmp |= CMD_CFG_EN_TIMESTAMP;
	else
		tmp &= ~CMD_CFG_EN_TIMESTAMP;
	iowrite32be(tmp, &regs->command_config);
}

uint32_t fman_tgec_get_event(struct tgec_regs *regs, uint32_t ev_mask)
{
	return ioread32be(&regs->ievent) & ev_mask;
}

void fman_tgec_ack_event(struct tgec_regs *regs, uint32_t ev_mask)
{
	iowrite32be(ev_mask, &regs->ievent);
}

uint32_t fman_tgec_get_interrupt_mask(struct tgec_regs *regs)
{
	return ioread32be(&regs->imask);
}

void fman_tgec_add_addr_in_paddr(struct tgec_regs *regs, uint8_t *adr)
{
	uint32_t tmp0, tmp1;

	tmp0 = (uint32_t)(adr[0] |
			adr[1] << 8 |
			adr[2] << 16 |
			adr[3] << 24);
	tmp1 = (uint32_t)(adr[4] | adr[5] << 8);
	iowrite32be(tmp0, &regs->mac_addr_2);
	iowrite32be(tmp1, &regs->mac_addr_3);
}

void fman_tgec_clear_addr_in_paddr(struct tgec_regs *regs)
{
	iowrite32be(0, &regs->mac_addr_2);
	iowrite32be(0, &regs->mac_addr_3);
}

uint32_t fman_tgec_get_revision(struct tgec_regs *regs)
{
	return ioread32be(&regs->tgec_id);
}

void fman_tgec_enable_interrupt(struct tgec_regs *regs, uint32_t ev_mask)
{
	iowrite32be(ioread32be(&regs->imask) | ev_mask, &regs->imask);
}

void fman_tgec_disable_interrupt(struct tgec_regs *regs, uint32_t ev_mask)
{
	iowrite32be(ioread32be(&regs->imask) & ~ev_mask, &regs->imask);
}

uint16_t fman_tgec_get_max_frame_len(struct tgec_regs *regs)
{
	return (uint16_t) ioread32be(&regs->maxfrm);
}

void fman_tgec_defconfig(struct tgec_cfg *cfg)
{
	cfg->wan_mode_enable = DEFAULT_WAN_MODE_ENABLE;
	cfg->promiscuous_mode_enable = DEFAULT_PROMISCUOUS_MODE_ENABLE;
	cfg->pause_forward_enable = DEFAULT_PAUSE_FORWARD_ENABLE;
	cfg->pause_ignore = DEFAULT_PAUSE_IGNORE;
	cfg->tx_addr_ins_enable = DEFAULT_TX_ADDR_INS_ENABLE;
	cfg->loopback_enable = DEFAULT_LOOPBACK_ENABLE;
	cfg->cmd_frame_enable = DEFAULT_CMD_FRAME_ENABLE;
	cfg->rx_error_discard = DEFAULT_RX_ERROR_DISCARD;
	cfg->send_idle_enable = DEFAULT_SEND_IDLE_ENABLE;
	cfg->no_length_check_enable = DEFAULT_NO_LENGTH_CHECK_ENABLE;
	cfg->lgth_check_nostdr = DEFAULT_LGTH_CHECK_NOSTDR;
	cfg->time_stamp_enable = DEFAULT_TIME_STAMP_ENABLE;
	cfg->tx_ipg_length = DEFAULT_TX_IPG_LENGTH;
	cfg->max_frame_length = DEFAULT_MAX_FRAME_LENGTH;
	cfg->pause_quant = DEFAULT_PAUSE_QUANT;
#ifdef FM_TX_ECC_FRMS_ERRATA_10GMAC_A004
	cfg->skip_fman11_workaround = FALSE;
#endif /* FM_TX_ECC_FRMS_ERRATA_10GMAC_A004 */
}

int fman_tgec_init(struct tgec_regs *regs, struct tgec_cfg *cfg,
		uint32_t exception_mask)
{
	uint32_t tmp;

	/* Config */
	tmp = 0x40; /* CRC forward */
	if (cfg->wan_mode_enable)
		tmp |= CMD_CFG_WAN_MODE;
	if (cfg->promiscuous_mode_enable)
		tmp |= CMD_CFG_PROMIS_EN;
	if (cfg->pause_forward_enable)
		tmp |= CMD_CFG_PAUSE_FWD;
	if (cfg->pause_ignore)
		tmp |= CMD_CFG_PAUSE_IGNORE;
	if (cfg->tx_addr_ins_enable)
		tmp |= CMD_CFG_TX_ADDR_INS;
	if (cfg->loopback_enable)
		tmp |= CMD_CFG_LOOPBACK_EN;
	if (cfg->cmd_frame_enable)
		tmp |= CMD_CFG_CMD_FRM_EN;
	if (cfg->rx_error_discard)
		tmp |= CMD_CFG_RX_ER_DISC;
	if (cfg->send_idle_enable)
		tmp |= CMD_CFG_SEND_IDLE;
	if (cfg->no_length_check_enable)
		tmp |= CMD_CFG_NO_LEN_CHK;
	if (cfg->time_stamp_enable)
		tmp |= CMD_CFG_EN_TIMESTAMP;
	iowrite32be(tmp, &regs->command_config);

	/* Max Frame Length */
	iowrite32be((uint32_t)cfg->max_frame_length, &regs->maxfrm);
	/* Pause Time */
	iowrite32be(cfg->pause_quant, &regs->pause_quant);

	/* clear all pending events and set-up interrupts */
	fman_tgec_ack_event(regs, 0xffffffff);
	fman_tgec_enable_interrupt(regs, exception_mask);

	return 0;
}

void fman_tgec_set_erratum_tx_fifo_corruption_10gmac_a007(struct tgec_regs *regs)
{
	uint32_t tmp;

	/* restore the default tx ipg Length */
	tmp = (ioread32be(&regs->tx_ipg_len) & ~TGEC_TX_IPG_LENGTH_MASK) | 12;

	iowrite32be(tmp, &regs->tx_ipg_len);
}
