/*-
 * Copyright (c) 2016 Adrian Chadd <adrian@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#ifndef	__IF_BWN_PHY_N_H__
#define	__IF_BWN_PHY_N_H__

extern int bwn_phy_n_attach(struct bwn_mac *mac);
extern void bwn_phy_n_detach(struct bwn_mac *mac);
extern int bwn_phy_n_prepare_hw(struct bwn_mac *mac);
extern void bwn_phy_n_init_pre(struct bwn_mac *mac);
extern int bwn_phy_n_init(struct bwn_mac *mac);
extern void bwn_phy_n_exit(struct bwn_mac *mac);
extern uint16_t bwn_phy_n_read(struct bwn_mac *mac, uint16_t reg);
extern void bwn_phy_n_write(struct bwn_mac *mac, uint16_t reg, uint16_t value);
extern uint16_t bwn_phy_n_rf_read(struct bwn_mac *mac, uint16_t reg);
extern void bwn_phy_n_rf_write(struct bwn_mac *mac, uint16_t reg, uint16_t value);
extern int bwn_phy_n_hwpctl(struct bwn_mac *mac);
extern void bwn_phy_n_rf_onoff(struct bwn_mac *mac, int on);
extern void bwn_phy_n_switch_analog(struct bwn_mac *mac, int on);
extern int bwn_phy_n_switch_channel(struct bwn_mac *mac, uint32_t newchan);
extern uint32_t bwn_phy_n_get_default_chan(struct bwn_mac *mac);
extern void bwn_phy_n_set_antenna(struct bwn_mac *mac, int antenna);
extern int bwn_phy_n_im(struct bwn_mac *mac, int mode);
extern bwn_txpwr_result_t bwn_phy_n_recalc_txpwr(struct bwn_mac *mac, int ignore_tssi);
extern void bwn_phy_n_set_txpwr(struct bwn_mac *mac);
extern void bwn_phy_n_task_15s(struct bwn_mac *mac);
extern void bwn_phy_n_task_60s(struct bwn_mac *mac);

#endif	/* __IF_BWN_PHY_N_H__ */
