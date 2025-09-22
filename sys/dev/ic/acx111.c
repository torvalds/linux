/*	$OpenBSD: acx111.c,v 1.25 2024/09/01 03:08:56 jsg Exp $ */

/*
 * Copyright (c) 2006 Jonathan Gray <jsg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/pci/pcireg.h>

#include <dev/ic/acxvar.h>
#include <dev/ic/acxreg.h>

#define ACX111_CONF_MEM		0x0003
#define ACX111_CONF_MEMINFO	0x0005

#define ACX111_INTR_ENABLE	(ACXRV_INTR_TX_FINI | ACXRV_INTR_RX_FINI)
/*
 * XXX do we really care about the following interrupts?
 *
 * ACXRV_INTR_IV_ICV_FAILURE | ACXRV_INTR_INFO |
 * ACXRV_INTR_SCAN_FINI | ACXRV_INTR_FCS_THRESHOLD
 */

#define ACX111_INTR_DISABLE	(uint16_t)~(ACXRV_INTR_CMD_FINI)

#define ACX111_RATE_2		0x0001
#define ACX111_RATE_4		0x0002
#define ACX111_RATE_11		0x0004
#define ACX111_RATE_12		0x0008
#define ACX111_RATE_18		0x0010
#define ACX111_RATE_22		0x0020
#define ACX111_RATE_24		0x0040
#define ACX111_RATE_36		0x0080
#define ACX111_RATE_44		0x0100
#define ACX111_RATE_48		0x0200
#define ACX111_RATE_72		0x0400
#define ACX111_RATE_96		0x0800
#define ACX111_RATE_108		0x1000

/* XXX skip ACX111_RATE_44 */
#define ACX111_RATE_ALL		0x1eff

#define ACX111_TXPOWER		15
#define ACX111_GPIO_POWER_LED	0x0040
#define ACX111_EE_EADDR_OFS	0x21

#define ACX111_FW_TXDESC_SIZE	(sizeof(struct acx_fw_txdesc) + 4)

#if ACX111_TXPOWER <= 12
#define ACX111_TXPOWER_VAL	1
#else
#define ACX111_TXPOWER_VAL	2
#endif

int	acx111_init(struct acx_softc *);
int	acx111_init_memory(struct acx_softc *);
void	acx111_init_fw_txring(struct acx_softc *, uint32_t);
int	acx111_write_config(struct acx_softc *, struct acx_config *);
void	acx111_set_fw_txdesc_rate(struct acx_softc *,
	    struct acx_txbuf *, int);
void	acx111_set_bss_join_param(struct acx_softc *, void *, int);

/*
 * NOTE:
 * Following structs' fields are little endian
 */
struct acx111_bss_join {
	uint16_t	basic_rates;
	uint8_t		dtim_intvl;
} __packed;

struct acx111_conf_mem {
	struct acx_conf	confcom;

	uint16_t	sta_max;	/* max num of sta, ACX111_STA_MAX */
	uint16_t	memblk_size;	/* mem block size */
	uint8_t		rx_memblk_perc;	/* percent of RX mem block, unit: 5% */
	uint8_t		fw_rxring_num;	/* num of RX ring */
	uint8_t		fw_txring_num;	/* num of TX ring */
	uint8_t		opt;		/* see ACX111_MEMOPT_ */
	uint8_t		xfer_perc;	/* frag/xfer proportion, unit: 5% */
	uint16_t	reserved0;
	uint8_t		reserved1;

	uint8_t		fw_rxdesc_num;	/* num of fw rx desc */
	uint8_t		fw_rxring_reserved1;
	uint8_t		fw_rxring_type;	/* see ACX111_RXRING_TYPE_ */
	uint8_t		fw_rxring_prio;	/* see ACX111_RXRING_PRIO_ */

	uint32_t	h_rxring_paddr; /* host rx desc start phyaddr */

	uint8_t		fw_txdesc_num;	/* num of fw tx desc */
	uint8_t		fw_txring_reserved1;
	uint8_t		fw_txring_reserved2;
	uint8_t		fw_txring_attr;	/* see ACX111_TXRING_ATTR_ */
} __packed;

#define ACX111_STA_MAX			32
#define ACX111_RX_MEMBLK_PERCENT	10	/* 50% */
#define ACX111_XFER_PERCENT		15	/* 75% */
#define ACX111_RXRING_TYPE_DEFAULT	7
#define ACX111_RXRING_PRIO_DEFAULT	0
#define ACX111_TXRING_ATTR_DEFAULT	0
#define ACX111_MEMOPT_DEFAULT		0

struct acx111_conf_meminfo {
	struct acx_conf	confcom;
	uint32_t	tx_memblk_addr;	/* start addr of tx mem blocks */
	uint32_t	rx_memblk_addr;	/* start addr of rx mem blocks */
	uint32_t	fw_rxring_start; /* start phyaddr of fw rx ring */
	uint32_t	reserved0;
	uint32_t	fw_txring_start; /* start phyaddr of fw tx ring */
	uint8_t		fw_txring_attr;	/* XXX see ACX111_TXRING_ATTR_ */
	uint16_t	reserved1;
	uint8_t		reserved2;
} __packed;

struct acx111_conf_txpower {
	struct acx_conf	confcom;
	uint8_t		txpower;
} __packed;

struct acx111_conf_option {
	struct acx_conf	confcom;
	uint32_t	feature;
	uint32_t	dataflow;	/* see ACX111_DF_ */
} __packed;

#define ACX111_DF_NO_RXDECRYPT	0x00000080
#define ACX111_DF_NO_TXENCRYPT	0x00000001

struct acx111_wepkey {
	uint8_t		mac_addr[IEEE80211_ADDR_LEN];
	uint16_t	action;		/* see ACX111_WEPKEY_ACT_ */
	uint16_t	reserved;
	uint8_t		key_len;
	uint8_t		key_type;	/* see ACX111_WEPKEY_TYPE_ */
	uint8_t		index;		/* XXX ?? */
	uint8_t		key_idx;
	uint8_t		counter[6];
#define ACX111_WEPKEY_LEN	32
	uint8_t		key[ACX111_WEPKEY_LEN];
} __packed;

#define ACX111_WEPKEY_ACT_ADD		1
#define ACX111_WEPKEY_TYPE_DEFAULT	0

static const uint16_t acx111_reg[ACXREG_MAX] = {
	ACXREG(SOFT_RESET,		0x0000),

	ACXREG(FWMEM_ADDR,		0x0014),
	ACXREG(FWMEM_DATA,		0x0018),
	ACXREG(FWMEM_CTRL,		0x001c),
	ACXREG(FWMEM_START,		0x0020),

	ACXREG(EVENT_MASK,		0x0034),

	ACXREG(INTR_TRIG,		0x00b4),
	ACXREG(INTR_MASK,		0x00d4),
	ACXREG(INTR_STATUS,		0x00f0),
	ACXREG(INTR_STATUS_CLR,		0x00e4),
	ACXREG(INTR_ACK,		0x00e8),

	ACXREG(HINTR_TRIG,		0x00ec),
	ACXREG(RADIO_ENABLE,		0x01d0),

	ACXREG(EEPROM_INIT,		0x0100),
	ACXREG(EEPROM_CTRL,		0x0338),
	ACXREG(EEPROM_ADDR,		0x033c),
	ACXREG(EEPROM_DATA,		0x0340),
	ACXREG(EEPROM_CONF,		0x0344),
	ACXREG(EEPROM_INFO,		0x0390),

	ACXREG(PHY_ADDR,		0x0350),
	ACXREG(PHY_DATA,		0x0354),
	ACXREG(PHY_CTRL,		0x0358),

	ACXREG(GPIO_OUT_ENABLE,		0x0374),
	ACXREG(GPIO_OUT,		0x037c),

	ACXREG(CMD_REG_OFFSET,		0x0388),
	ACXREG(INFO_REG_OFFSET,		0x038c),

	ACXREG(RESET_SENSE,		0x0104),
	ACXREG(ECPU_CTRL,		0x0108)
};

/* XXX */
static uint16_t	acx111_rate_map[109] = {
	ACX111_RATE_2,
	ACX111_RATE_4,
	ACX111_RATE_11,
	ACX111_RATE_22,
	ACX111_RATE_12,
	ACX111_RATE_18,
	ACX111_RATE_24,
	ACX111_RATE_36,
	ACX111_RATE_48,
	ACX111_RATE_72,
	ACX111_RATE_96,
	ACX111_RATE_108
};

void
acx111_set_param(struct acx_softc *sc)
{
	sc->chip_mem1_rid = PCIR_BAR(0);
	sc->chip_mem2_rid = PCIR_BAR(1);
	sc->chip_ioreg = acx111_reg;
	sc->chip_intr_enable = ACX111_INTR_ENABLE;
#ifndef IEEE80211_STA_ONLY
	sc->chip_intr_enable |= ACXRV_INTR_DTIM;
#endif
	sc->chip_intr_disable = ACX111_INTR_DISABLE;
	sc->chip_gpio_pled = ACX111_GPIO_POWER_LED;
	sc->chip_ee_eaddr_ofs = ACX111_EE_EADDR_OFS;

	sc->chip_phymode = IEEE80211_MODE_11G;
	sc->chip_chan_flags = IEEE80211_CHAN_CCK |
	    IEEE80211_CHAN_OFDM |
	    IEEE80211_CHAN_DYN |
	    IEEE80211_CHAN_2GHZ;
	sc->sc_ic.ic_caps = IEEE80211_C_WEP | IEEE80211_C_SHSLOT;
	sc->sc_ic.ic_phytype = IEEE80211_T_OFDM;
	sc->sc_ic.ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	sc->sc_ic.ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	sc->chip_init = acx111_init;
	sc->chip_write_config = acx111_write_config;
	sc->chip_set_fw_txdesc_rate = acx111_set_fw_txdesc_rate;
	sc->chip_set_bss_join_param = acx111_set_bss_join_param;
	sc->sc_flags |= ACX_FLAG_ACX111;
}

int
acx111_init(struct acx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	/*
	 * NOTE:
	 * Order of initialization:
	 * 1) Templates
	 * 2) Hardware memory
	 * Above order is critical to get a correct memory map
	 */
	if (acx_init_tmplt_ordered(sc) != 0) {
		printf("%s: %s can't initialize templates\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}

	if (acx111_init_memory(sc) != 0) {
		printf("%s: %s can't initialize hw memory\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}

	return (0);
}

int
acx111_init_memory(struct acx_softc *sc)
{
	struct acx111_conf_mem mem;
	struct acx111_conf_meminfo mem_info;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	/* Set memory configuration */
	bzero(&mem, sizeof(mem));

	mem.sta_max = htole16(ACX111_STA_MAX);
	mem.memblk_size = htole16(ACX_MEMBLOCK_SIZE);
	mem.rx_memblk_perc = ACX111_RX_MEMBLK_PERCENT;
	mem.opt = ACX111_MEMOPT_DEFAULT;
	mem.xfer_perc = ACX111_XFER_PERCENT;

	mem.fw_rxring_num = 1;
	mem.fw_rxring_type = ACX111_RXRING_TYPE_DEFAULT;
	mem.fw_rxring_prio = ACX111_RXRING_PRIO_DEFAULT;
	mem.fw_rxdesc_num = ACX_RX_DESC_CNT;
	mem.h_rxring_paddr = htole32(sc->sc_ring_data.rx_ring_paddr);

	mem.fw_txring_num = 1;
	mem.fw_txring_attr = ACX111_TXRING_ATTR_DEFAULT;
	mem.fw_txdesc_num = ACX_TX_DESC_CNT;

	if (acx_set_conf(sc, ACX111_CONF_MEM, &mem, sizeof(mem)) != 0) {
		printf("%s: can't set mem\n", ifp->if_xname);
		return (1);
	}

	/* Get memory configuration */
	if (acx_get_conf(sc, ACX111_CONF_MEMINFO, &mem_info,
	    sizeof(mem_info)) != 0) {
		printf("%s: can't get meminfo\n", ifp->if_xname);
		return (1);
	}

	/* Setup firmware TX descriptor ring */
	acx111_init_fw_txring(sc, letoh32(mem_info.fw_txring_start));

	/*
	 * There is no need to setup firmware RX descriptor ring,
	 * it is automatically setup by hardware.
	 */

	return (0);
}

void
acx111_init_fw_txring(struct acx_softc *sc, uint32_t fw_txdesc_start)
{
	struct acx_txbuf *tx_buf;
	uint32_t desc_paddr;
	int i;

	tx_buf = sc->sc_buf_data.tx_buf;
	desc_paddr = sc->sc_ring_data.tx_ring_paddr;

	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		tx_buf[i].tb_fwdesc_ofs = fw_txdesc_start +
		    (i * ACX111_FW_TXDESC_SIZE);

		/*
		 * Except for the following fields, rest of the fields
		 * are setup by hardware.
		 */
		FW_TXDESC_SETFIELD_4(sc, &tx_buf[i], f_tx_host_desc,
		    desc_paddr);
		FW_TXDESC_SETFIELD_1(sc, &tx_buf[i], f_tx_ctrl,
		    DESC_CTRL_HOSTOWN);

		desc_paddr += (2 * sizeof(struct acx_host_desc));
	}
}

int
acx111_write_config(struct acx_softc *sc, struct acx_config *conf)
{
	struct acx111_conf_txpower tx_power;
	struct acx111_conf_option opt;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t dataflow;

	/* Set TX power */
	tx_power.txpower = ACX111_TXPOWER_VAL;
	if (acx_set_conf(sc, ACX_CONF_TXPOWER, &tx_power,
	    sizeof(tx_power)) != 0) {
		printf("%s: %s can't set TX power\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}

	/*
	 * Turn off hardware WEP
	 */
	if (acx_get_conf(sc, ACX_CONF_OPTION, &opt, sizeof(opt)) != 0) {
		printf("%s: %s can't get option\n", ifp->if_xname, __func__);
		return (ENXIO);
	}

	dataflow = letoh32(opt.dataflow) |
	    ACX111_DF_NO_TXENCRYPT |
	    ACX111_DF_NO_RXDECRYPT;
	opt.dataflow = htole32(dataflow);

	if (acx_set_conf(sc, ACX_CONF_OPTION, &opt, sizeof(opt)) != 0) {
		printf("%s: %s can't set option\n", ifp->if_xname, __func__);
		return (ENXIO);
	}

	return (0);
}

void
acx111_set_fw_txdesc_rate(struct acx_softc *sc, struct acx_txbuf *tx_buf,
    int rate0)
{
	uint16_t rate;

	rate = acx111_rate_map[rate0];
	if (rate == 0)
		/* set rate to 1Mbit/s if rate was zero */
		rate = acx111_rate_map[2];

	FW_TXDESC_SETFIELD_2(sc, tx_buf, u.r2.rate111, rate);
}

void
acx111_set_bss_join_param(struct acx_softc *sc, void *param, int dtim_intvl)
{
	struct acx111_bss_join *bj = param;

	bj->basic_rates = htole16(ACX111_RATE_ALL);
	bj->dtim_intvl = dtim_intvl;
}
