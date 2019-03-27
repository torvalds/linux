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


#include "fsl_fman_memac.h"


uint32_t fman_memac_get_event(struct memac_regs *regs, uint32_t ev_mask)
{
    return ioread32be(&regs->ievent) & ev_mask;
}

uint32_t fman_memac_get_interrupt_mask(struct memac_regs *regs)
{
    return ioread32be(&regs->imask);
}

void fman_memac_ack_event(struct memac_regs *regs, uint32_t ev_mask)
{
    iowrite32be(ev_mask, &regs->ievent);
}

void fman_memac_set_promiscuous(struct memac_regs *regs, bool val)
{
    uint32_t tmp;

    tmp = ioread32be(&regs->command_config);

    if (val)
        tmp |= CMD_CFG_PROMIS_EN;
    else
        tmp &= ~CMD_CFG_PROMIS_EN;

    iowrite32be(tmp, &regs->command_config);
}

void fman_memac_clear_addr_in_paddr(struct memac_regs *regs,
                    uint8_t paddr_num)
{
    if (paddr_num == 0) {
        iowrite32be(0, &regs->mac_addr0.mac_addr_l);
        iowrite32be(0, &regs->mac_addr0.mac_addr_u);
    } else {
        iowrite32be(0x0, &regs->mac_addr[paddr_num - 1].mac_addr_l);
        iowrite32be(0x0, &regs->mac_addr[paddr_num - 1].mac_addr_u);
    }
}

void fman_memac_add_addr_in_paddr(struct memac_regs *regs,
                    uint8_t *adr,
                    uint8_t paddr_num)
{
    uint32_t tmp0, tmp1;

    tmp0 = (uint32_t)(adr[0] |
            adr[1] << 8 |
            adr[2] << 16 |
            adr[3] << 24);
    tmp1 = (uint32_t)(adr[4] | adr[5] << 8);

    if (paddr_num == 0) {
        iowrite32be(tmp0, &regs->mac_addr0.mac_addr_l);
        iowrite32be(tmp1, &regs->mac_addr0.mac_addr_u);
    } else {
        iowrite32be(tmp0, &regs->mac_addr[paddr_num-1].mac_addr_l);
        iowrite32be(tmp1, &regs->mac_addr[paddr_num-1].mac_addr_u);
    }
}

void fman_memac_enable(struct memac_regs *regs, bool apply_rx, bool apply_tx)
{
    uint32_t tmp;

    tmp = ioread32be(&regs->command_config);

    if (apply_rx)
        tmp |= CMD_CFG_RX_EN;

    if (apply_tx)
        tmp |= CMD_CFG_TX_EN;

    iowrite32be(tmp, &regs->command_config);
}

void fman_memac_disable(struct memac_regs *regs, bool apply_rx, bool apply_tx)
{
    uint32_t tmp;

    tmp = ioread32be(&regs->command_config);

    if (apply_rx)
        tmp &= ~CMD_CFG_RX_EN;

    if (apply_tx)
        tmp &= ~CMD_CFG_TX_EN;

    iowrite32be(tmp, &regs->command_config);
}

void fman_memac_reset_stat(struct memac_regs *regs)
{
    uint32_t tmp;

    tmp = ioread32be(&regs->statn_config);

    tmp |= STATS_CFG_CLR;

    iowrite32be(tmp, &regs->statn_config);

    while (ioread32be(&regs->statn_config) & STATS_CFG_CLR);
}

void fman_memac_reset(struct memac_regs *regs)
{
    uint32_t tmp;

    tmp = ioread32be(&regs->command_config);

    tmp |= CMD_CFG_SW_RESET;

    iowrite32be(tmp, &regs->command_config);

    while (ioread32be(&regs->command_config) & CMD_CFG_SW_RESET);
}

int fman_memac_init(struct memac_regs *regs,
        struct memac_cfg *cfg,
        enum enet_interface enet_interface,
        enum enet_speed enet_speed,
	bool slow_10g_if,
        uint32_t exceptions)
{
    uint32_t    tmp;

    /* Config */
    tmp = 0;
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
        tmp |= CMD_CFG_CNT_FRM_EN;
    if (cfg->send_idle_enable)
        tmp |= CMD_CFG_SEND_IDLE;
    if (cfg->no_length_check_enable)
        tmp |= CMD_CFG_NO_LEN_CHK;
    if (cfg->rx_sfd_any)
        tmp |= CMD_CFG_SFD_ANY;
    if (cfg->pad_enable)
        tmp |= CMD_CFG_TX_PAD_EN;
    if (cfg->wake_on_lan)
        tmp |= CMD_CFG_MG;

    tmp |= CMD_CFG_CRC_FWD;

    iowrite32be(tmp, &regs->command_config);

    /* Max Frame Length */
    iowrite32be((uint32_t)cfg->max_frame_length, &regs->maxfrm);

    /* Pause Time */
    iowrite32be((uint32_t)cfg->pause_quanta, &regs->pause_quanta[0]);
    iowrite32be((uint32_t)0, &regs->pause_thresh[0]);

    /* IF_MODE */
    tmp = 0;
    switch (enet_interface) {
    case E_ENET_IF_XGMII:
    case E_ENET_IF_XFI:
        tmp |= IF_MODE_XGMII;
        break;
    default:
        tmp |= IF_MODE_GMII;
        if (enet_interface == E_ENET_IF_RGMII && !cfg->loopback_enable)
            tmp |= IF_MODE_RGMII | IF_MODE_RGMII_AUTO;
    }
    iowrite32be(tmp, &regs->if_mode);

	/* TX_FIFO_SECTIONS */
	tmp = 0;
	if (enet_interface == E_ENET_IF_XGMII ||
		enet_interface == E_ENET_IF_XFI) {
		if(slow_10g_if) {
			tmp |= (TX_FIFO_SECTIONS_TX_AVAIL_SLOW_10G |
				TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_10G);
		} else {
			tmp |= (TX_FIFO_SECTIONS_TX_AVAIL_10G |
				TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_10G);
		}
	} else {
		tmp |= (TX_FIFO_SECTIONS_TX_AVAIL_1G |
				TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_1G);
	}
	iowrite32be(tmp, &regs->tx_fifo_sections);

    /* clear all pending events and set-up interrupts */
    fman_memac_ack_event(regs, 0xffffffff);
    fman_memac_set_exception(regs, exceptions, TRUE);

    return 0;
}

void fman_memac_set_exception(struct memac_regs *regs, uint32_t val, bool enable)
{
    uint32_t tmp;

    tmp = ioread32be(&regs->imask);
    if (enable)
        tmp |= val;
    else
        tmp &= ~val;

    iowrite32be(tmp, &regs->imask);
}

void fman_memac_reset_filter_table(struct memac_regs *regs)
{
	uint32_t i;
	for (i = 0; i < 64; i++)
		iowrite32be(i & ~HASH_CTRL_MCAST_EN, &regs->hashtable_ctrl);
}

void fman_memac_set_hash_table_entry(struct memac_regs *regs, uint32_t crc)
{
	iowrite32be(crc | HASH_CTRL_MCAST_EN, &regs->hashtable_ctrl);
}

void fman_memac_set_hash_table(struct memac_regs *regs, uint32_t val)
{
    iowrite32be(val, &regs->hashtable_ctrl);
}

uint16_t fman_memac_get_max_frame_len(struct memac_regs *regs)
{
    uint32_t tmp;

    tmp = ioread32be(&regs->maxfrm);

    return(uint16_t)tmp;
}


void fman_memac_set_tx_pause_frames(struct memac_regs *regs,
                uint8_t priority,
                uint16_t pause_time,
                uint16_t thresh_time)
{
    uint32_t tmp;

	tmp = ioread32be(&regs->tx_fifo_sections);

	if (priority == 0xff) {
		GET_TX_EMPTY_DEFAULT_VALUE(tmp);
		iowrite32be(tmp, &regs->tx_fifo_sections);

		tmp = ioread32be(&regs->command_config);
		tmp &= ~CMD_CFG_PFC_MODE;
		priority = 0;
	} else {
		GET_TX_EMPTY_PFC_VALUE(tmp);
		iowrite32be(tmp, &regs->tx_fifo_sections);

		tmp = ioread32be(&regs->command_config);
		tmp |= CMD_CFG_PFC_MODE;
    }

    iowrite32be(tmp, &regs->command_config);

    tmp = ioread32be(&regs->pause_quanta[priority / 2]);
    if (priority % 2)
        tmp &= 0x0000FFFF;
    else
        tmp &= 0xFFFF0000;
    tmp |= ((uint32_t)pause_time << (16 * (priority % 2)));
    iowrite32be(tmp, &regs->pause_quanta[priority / 2]);

    tmp = ioread32be(&regs->pause_thresh[priority / 2]);
    if (priority % 2)
            tmp &= 0x0000FFFF;
    else
            tmp &= 0xFFFF0000;
    tmp |= ((uint32_t)thresh_time<<(16 * (priority % 2)));
    iowrite32be(tmp, &regs->pause_thresh[priority / 2]);
}

void fman_memac_set_rx_ignore_pause_frames(struct memac_regs    *regs,bool enable)
{
    uint32_t tmp;

    tmp = ioread32be(&regs->command_config);
    if (enable)
        tmp |= CMD_CFG_PAUSE_IGNORE;
    else
        tmp &= ~CMD_CFG_PAUSE_IGNORE;

    iowrite32be(tmp, &regs->command_config);
}

void fman_memac_set_wol(struct memac_regs *regs, bool enable)
{
    uint32_t tmp;

    tmp = ioread32be(&regs->command_config);

    if (enable)
        tmp |= CMD_CFG_MG;
    else
        tmp &= ~CMD_CFG_MG;

    iowrite32be(tmp, &regs->command_config);
}

#define GET_MEMAC_CNTR_64(bn) \
        (ioread32be(&regs->bn ## _l) | \
        ((uint64_t)ioread32be(&regs->bn ## _u) << 32))

uint64_t fman_memac_get_counter(struct memac_regs *regs,
                enum memac_counters reg_name)
{
    uint64_t ret_val;

    switch (reg_name) {
    case E_MEMAC_COUNTER_R64:
        ret_val = GET_MEMAC_CNTR_64(r64);
        break;
    case E_MEMAC_COUNTER_R127:
        ret_val = GET_MEMAC_CNTR_64(r127);
        break;
    case E_MEMAC_COUNTER_R255:
        ret_val = GET_MEMAC_CNTR_64(r255);
        break;
    case E_MEMAC_COUNTER_R511:
        ret_val = GET_MEMAC_CNTR_64(r511);
        break;
    case E_MEMAC_COUNTER_R1023:
        ret_val = GET_MEMAC_CNTR_64(r1023);
        break;
    case E_MEMAC_COUNTER_R1518:
        ret_val = GET_MEMAC_CNTR_64(r1518);
        break;
    case E_MEMAC_COUNTER_R1519X:
        ret_val = GET_MEMAC_CNTR_64(r1519x);
        break;
    case E_MEMAC_COUNTER_RFRG:
        ret_val = GET_MEMAC_CNTR_64(rfrg);
        break;
    case E_MEMAC_COUNTER_RJBR:
        ret_val = GET_MEMAC_CNTR_64(rjbr);
        break;
    case E_MEMAC_COUNTER_RDRP:
        ret_val = GET_MEMAC_CNTR_64(rdrp);
        break;
    case E_MEMAC_COUNTER_RALN:
        ret_val = GET_MEMAC_CNTR_64(raln);
        break;
    case E_MEMAC_COUNTER_TUND:
        ret_val = GET_MEMAC_CNTR_64(tund);
        break;
    case E_MEMAC_COUNTER_ROVR:
        ret_val = GET_MEMAC_CNTR_64(rovr);
        break;
    case E_MEMAC_COUNTER_RXPF:
        ret_val = GET_MEMAC_CNTR_64(rxpf);
        break;
    case E_MEMAC_COUNTER_TXPF:
        ret_val = GET_MEMAC_CNTR_64(txpf);
        break;
    case E_MEMAC_COUNTER_ROCT:
        ret_val = GET_MEMAC_CNTR_64(roct);
        break;
    case E_MEMAC_COUNTER_RMCA:
        ret_val = GET_MEMAC_CNTR_64(rmca);
        break;
    case E_MEMAC_COUNTER_RBCA:
        ret_val = GET_MEMAC_CNTR_64(rbca);
        break;
    case E_MEMAC_COUNTER_RPKT:
        ret_val = GET_MEMAC_CNTR_64(rpkt);
        break;
    case E_MEMAC_COUNTER_RUCA:
        ret_val = GET_MEMAC_CNTR_64(ruca);
        break;
    case E_MEMAC_COUNTER_RERR:
        ret_val = GET_MEMAC_CNTR_64(rerr);
        break;
    case E_MEMAC_COUNTER_TOCT:
        ret_val = GET_MEMAC_CNTR_64(toct);
        break;
    case E_MEMAC_COUNTER_TMCA:
        ret_val = GET_MEMAC_CNTR_64(tmca);
        break;
    case E_MEMAC_COUNTER_TBCA:
        ret_val = GET_MEMAC_CNTR_64(tbca);
        break;
    case E_MEMAC_COUNTER_TUCA:
        ret_val = GET_MEMAC_CNTR_64(tuca);
        break;
    case E_MEMAC_COUNTER_TERR:
        ret_val = GET_MEMAC_CNTR_64(terr);
        break;
    default:
        ret_val = 0;
    }

    return ret_val;
}

void fman_memac_adjust_link(struct memac_regs *regs,
        enum enet_interface iface_mode,
        enum enet_speed speed, bool full_dx)
{
    uint32_t    tmp;

    tmp = ioread32be(&regs->if_mode);

    if (full_dx)
        tmp &= ~IF_MODE_HD;
    else
        tmp |= IF_MODE_HD;

    if (iface_mode == E_ENET_IF_RGMII) {
        /* Configure RGMII in manual mode */
        tmp &= ~IF_MODE_RGMII_AUTO;
        tmp &= ~IF_MODE_RGMII_SP_MASK;

        if (full_dx)
            tmp |= IF_MODE_RGMII_FD;
        else
            tmp &= ~IF_MODE_RGMII_FD;

        switch (speed) {
        case E_ENET_SPEED_1000:
            tmp |= IF_MODE_RGMII_1000;
            break;
        case E_ENET_SPEED_100:
            tmp |= IF_MODE_RGMII_100;
            break;
        case E_ENET_SPEED_10:
            tmp |= IF_MODE_RGMII_10;
            break;
        default:
            break;
        }
    }

    iowrite32be(tmp, &regs->if_mode);
}

void fman_memac_defconfig(struct memac_cfg *cfg)
{
    cfg->reset_on_init		= FALSE;
    cfg->wan_mode_enable		= FALSE;
    cfg->promiscuous_mode_enable	= FALSE;
    cfg->pause_forward_enable	= FALSE;
    cfg->pause_ignore		= FALSE;
    cfg->tx_addr_ins_enable		= FALSE;
    cfg->loopback_enable		= FALSE;
    cfg->cmd_frame_enable		= FALSE;
    cfg->rx_error_discard		= FALSE;
    cfg->send_idle_enable		= FALSE;
    cfg->no_length_check_enable	= TRUE;
    cfg->lgth_check_nostdr		= FALSE;
    cfg->time_stamp_enable		= FALSE;
    cfg->tx_ipg_length		= DEFAULT_TX_IPG_LENGTH;
    cfg->max_frame_length		= DEFAULT_FRAME_LENGTH;
    cfg->pause_quanta		= DEFAULT_PAUSE_QUANTA;
    cfg->pad_enable			= TRUE;
    cfg->phy_tx_ena_on		= FALSE;
    cfg->rx_sfd_any			= FALSE;
    cfg->rx_pbl_fwd			= FALSE;
    cfg->tx_pbl_fwd			= FALSE;
    cfg->debug_mode			= FALSE;
    cfg->wake_on_lan        = FALSE;
}
