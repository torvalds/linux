/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Adrian Chadd <adrian@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	__ARSWITCH_8327_H__
#define	__ARSWITCH_8327_H__

enum ar8327_pad_mode {
	AR8327_PAD_NC = 0,
	AR8327_PAD_MAC2MAC_MII,
	AR8327_PAD_MAC2MAC_GMII,
	AR8327_PAD_MAC_SGMII,
	AR8327_PAD_MAC2PHY_MII,
	AR8327_PAD_MAC2PHY_GMII,
	AR8327_PAD_MAC_RGMII,
	AR8327_PAD_PHY_GMII,
	AR8327_PAD_PHY_RGMII,
	AR8327_PAD_PHY_MII,
};

enum ar8327_clk_delay_sel {
	AR8327_CLK_DELAY_SEL0 = 0,
	AR8327_CLK_DELAY_SEL1,
	AR8327_CLK_DELAY_SEL2,
	AR8327_CLK_DELAY_SEL3,
};

/* XXX update the field types */
struct ar8327_pad_cfg {
	uint32_t mode;
	uint32_t rxclk_sel;
	uint32_t txclk_sel;
	uint32_t txclk_delay_sel;
	uint32_t rxclk_delay_sel;
	uint32_t txclk_delay_en;
	uint32_t rxclk_delay_en;
	uint32_t sgmii_delay_en;
	uint32_t pipe_rxclk_sel;
};

struct ar8327_sgmii_cfg {
	uint32_t sgmii_ctrl;
	uint32_t serdes_aen;
};

struct ar8327_led_cfg {
	uint32_t led_ctrl0;
	uint32_t led_ctrl1;
	uint32_t led_ctrl2;
	uint32_t led_ctrl3;
	uint32_t open_drain;
};

struct ar8327_port_cfg {
#define	AR8327_PORT_SPEED_10		1
#define	AR8327_PORT_SPEED_100		2
#define	AR8327_PORT_SPEED_1000		3
	uint32_t speed;
	uint32_t force_link;
	uint32_t duplex;
	uint32_t txpause;
	uint32_t rxpause;
};

extern struct ar8327_led_mapping {
	int reg;
	int shift;
} ar8327_led_mapping[AR8327_NUM_PHYS][ETHERSWITCH_PORT_MAX_LEDS];

extern	void ar8327_attach(struct arswitch_softc *sc);

#endif	/* __ARSWITCH_8327_H__ */

