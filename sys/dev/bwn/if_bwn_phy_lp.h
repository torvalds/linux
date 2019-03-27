/*-
 * Copyright (c) 2009-2010 Weongyo Jeong <weongyo@freebsd.org>
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

#ifndef	__IF_BWN_PHY_LP_H__
#define	__IF_BWN_PHY_LP_H__

extern void	bwn_phy_lp_init_pre(struct bwn_mac *);
extern int	bwn_phy_lp_init(struct bwn_mac *);
extern uint16_t	bwn_phy_lp_read(struct bwn_mac *, uint16_t);
extern void	bwn_phy_lp_write(struct bwn_mac *, uint16_t, uint16_t);
extern void	bwn_phy_lp_maskset(struct bwn_mac *, uint16_t, uint16_t,
		    uint16_t);
extern uint16_t	bwn_phy_lp_rf_read(struct bwn_mac *, uint16_t);
extern void	bwn_phy_lp_rf_write(struct bwn_mac *, uint16_t, uint16_t);
extern void	bwn_phy_lp_rf_onoff(struct bwn_mac *, int);
extern int	bwn_phy_lp_switch_channel(struct bwn_mac *, uint32_t);
extern void	bwn_phy_lp_switch_analog(struct bwn_mac *, int);
extern uint32_t	bwn_phy_lp_get_default_chan(struct bwn_mac *);
extern void	bwn_phy_lp_set_antenna(struct bwn_mac *, int);
extern void	bwn_phy_lp_task_60s(struct bwn_mac *);

#endif	/* __IF_BWN_PHY_LP_H__ */
