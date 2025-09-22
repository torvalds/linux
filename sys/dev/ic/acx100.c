/*	$OpenBSD: acx100.c,v 1.28 2022/01/09 05:42:38 jsg Exp $ */

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
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/endian.h>
#include <sys/socket.h>
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

#define ACX100_CONF_FW_RING	0x0003
#define ACX100_CONF_MEMOPT	0x0005

#define ACX100_INTR_ENABLE	(ACXRV_INTR_TX_FINI | ACXRV_INTR_RX_FINI)
/*
 * XXX do we really care about following interrupts?
 *
 * ACXRV_INTR_INFO | ACXRV_INTR_SCAN_FINI
 */

#define ACX100_INTR_DISABLE	(uint16_t)~(ACXRV_INTR_UNKN)

#define ACX100_RATE(rate)	((rate) * 5)

#define ACX100_TXPOWER		18
#define ACX100_GPIO_POWER_LED	0x0800
#define ACX100_EE_EADDR_OFS	0x1a

#define ACX100_FW_TXRING_SIZE	(ACX_TX_DESC_CNT * sizeof(struct acx_fw_txdesc))
#define ACX100_FW_RXRING_SIZE	(ACX_RX_DESC_CNT * sizeof(struct acx_fw_rxdesc))

int	acx100_init(struct acx_softc *);
int	acx100_init_wep(struct acx_softc *);
int	acx100_init_tmplt(struct acx_softc *);
int	acx100_init_fw_ring(struct acx_softc *);
int	acx100_init_memory(struct acx_softc *);
void	acx100_init_fw_txring(struct acx_softc *, uint32_t);
void	acx100_init_fw_rxring(struct acx_softc *, uint32_t);
int	acx100_read_config(struct acx_softc *, struct acx_config *);
int	acx100_write_config(struct acx_softc *, struct acx_config *);
int	acx100_set_txpower(struct acx_softc *);
void	acx100_set_fw_txdesc_rate(struct acx_softc *,
	    struct acx_txbuf *, int);
void	acx100_set_bss_join_param(struct acx_softc *, void *, int);
int	acx100_set_wepkey(struct acx_softc *, struct ieee80211_key *, int);
void	acx100_proc_wep_rxbuf(struct acx_softc *, struct mbuf *, int *);

/*
 * NOTE:
 * Following structs' fields are little endian
 */
struct acx100_bss_join {
	uint8_t	dtim_intvl;
	uint8_t	basic_rates;
	uint8_t	all_rates;
} __packed;

struct acx100_conf_fw_ring {
	struct acx_conf	confcom;
	uint32_t	fw_ring_size;	/* total size of fw (tx + rx) ring */
	uint32_t	fw_rxring_addr;	/* start phyaddr of fw rx desc */
	uint8_t		opt;		/* see ACX100_RINGOPT_ */
	uint8_t		fw_txring_num;	/* num of TX ring */
	uint8_t		fw_rxdesc_num;	/* num of fw rx desc */
	uint8_t		reserved0;
	uint32_t	fw_ring_end[2];	/* see ACX100_SET_RING_END() */
	uint32_t	fw_txring_addr;	/* start phyaddr of fw tx desc */
	uint8_t		fw_txring_prio;	/* see ACX100_TXRING_PRIO_ */
	uint8_t		fw_txdesc_num;	/* num of fw tx desc */
	uint16_t	reserved1;
} __packed;

#define ACX100_RINGOPT_AUTO_RESET	0x1
#define ACX100_TXRING_PRIO_DEFAULT	0
#define ACX100_SET_RING_END(conf, end)			\
do {							\
	(conf)->fw_ring_end[0] = htole32(end);		\
	(conf)->fw_ring_end[1] = htole32(end + 8);	\
} while (0)

struct acx100_conf_memblk_size {
	struct acx_conf	confcom;
	uint16_t	memblk_size;	/* size of each mem block */
} __packed;

struct acx100_conf_mem {
	struct acx_conf	confcom;
	uint32_t	opt;		/* see ACX100_MEMOPT_ */
	uint32_t	h_rxring_paddr;	/* host rx desc start phyaddr */

	/*
	 * Memory blocks are controlled by hardware
	 * once after they are initialized
	 */
	uint32_t	rx_memblk_addr;	/* start addr of rx mem blocks */
	uint32_t	tx_memblk_addr;	/* start addr of tx mem blocks */
	uint16_t	rx_memblk_num;	/* num of RX mem block */
	uint16_t	tx_memblk_num;	/* num of TX mem block */
} __packed;

#define ACX100_MEMOPT_MEM_INSTR		0x00000000 /* memory access instruct */
#define ACX100_MEMOPT_HOSTDESC		0x00010000 /* host indirect desc */
#define ACX100_MEMOPT_MEMBLOCK		0x00020000 /* local mem block list */
#define ACX100_MEMOPT_IO_INSTR		0x00040000 /* IO instruct */
#define ACX100_MEMOPT_PCICONF		0x00080000 /* PCI conf space */

#define ACX100_MEMBLK_ALIGN		0x20

struct acx100_conf_cca_mode {
	struct acx_conf	confcom;
	uint8_t		cca_mode;
	uint8_t		unknown;
} __packed;

struct acx100_conf_ed_thresh {
	struct acx_conf	confcom;
	uint8_t		ed_thresh;
	uint8_t		unknown[3];
} __packed;

struct acx100_conf_wepkey {
	struct acx_conf	confcom;
	uint8_t		action;	/* see ACX100_WEPKEY_ACT_ */
	uint8_t		key_len;
	uint8_t		key_idx;
#define ACX100_WEPKEY_LEN	29
	uint8_t		key[ACX100_WEPKEY_LEN];
} __packed;

#define ACX100_WEPKEY_ACT_ADD	1

static const uint16_t	acx100_reg[ACXREG_MAX] = {
	ACXREG(SOFT_RESET,		0x0000),

	ACXREG(FWMEM_ADDR,		0x0014),
	ACXREG(FWMEM_DATA,		0x0018),
	ACXREG(FWMEM_CTRL,		0x001c),
	ACXREG(FWMEM_START,		0x0020),

	ACXREG(EVENT_MASK,		0x0034),

	ACXREG(INTR_TRIG,		0x007c),
	ACXREG(INTR_MASK,		0x0098),
	ACXREG(INTR_STATUS,		0x00a4),
	ACXREG(INTR_STATUS_CLR,		0x00a8),
	ACXREG(INTR_ACK,		0x00ac),

	ACXREG(HINTR_TRIG,		0x00b0),
	ACXREG(RADIO_ENABLE,		0x0104),

	ACXREG(EEPROM_INIT,		0x02d0),
	ACXREG(EEPROM_CTRL,		0x0250),
	ACXREG(EEPROM_ADDR,		0x0254),
	ACXREG(EEPROM_DATA,		0x0258),
	ACXREG(EEPROM_CONF,		0x025c),
	ACXREG(EEPROM_INFO,		0x02ac),

	ACXREG(PHY_ADDR,		0x0268),
	ACXREG(PHY_DATA,		0x026c),
	ACXREG(PHY_CTRL,		0x0270),

	ACXREG(GPIO_OUT_ENABLE,		0x0290),
	ACXREG(GPIO_OUT,		0x0298),

	ACXREG(CMD_REG_OFFSET,		0x02a4),
	ACXREG(INFO_REG_OFFSET,		0x02a8),

	ACXREG(RESET_SENSE,		0x02d4),
	ACXREG(ECPU_CTRL,		0x02d8)
};

static const uint8_t	acx100_txpower_maxim[21] = {
	63, 63, 63, 62,
	61, 61, 60, 60,
	59, 58, 57, 55,
	53, 50, 47, 43,
	38, 31, 23, 13,
	0
};

static const uint8_t	acx100_txpower_rfmd[21] = {
	 0,  0,  0,  1,
	 2,  2,  3,  3,
	 4,  5,  6,  8,
	10, 13, 16, 20,
	25, 32, 41, 50,
	63
};

void
acx100_set_param(struct acx_softc *sc)
{
	sc->chip_mem1_rid = PCIR_BAR(1);
	sc->chip_mem2_rid = PCIR_BAR(2);
	sc->chip_ioreg = acx100_reg;
	sc->chip_hw_crypt = 1;
	sc->chip_intr_enable = ACX100_INTR_ENABLE;
#ifndef IEEE80211_STA_ONLY
	sc->chip_intr_enable |= ACXRV_INTR_DTIM;
#endif
	sc->chip_intr_disable = ACX100_INTR_DISABLE;
	sc->chip_gpio_pled = ACX100_GPIO_POWER_LED;
	sc->chip_ee_eaddr_ofs = ACX100_EE_EADDR_OFS;
	sc->chip_txdesc1_len = ACX_FRAME_HDRLEN;
	sc->chip_fw_txdesc_ctrl = DESC_CTRL_AUTODMA |
	    DESC_CTRL_RECLAIM | DESC_CTRL_FIRST_FRAG;

	sc->chip_phymode = IEEE80211_MODE_11B;
	sc->chip_chan_flags = IEEE80211_CHAN_B;
	sc->sc_ic.ic_phytype = IEEE80211_T_DS;
	sc->sc_ic.ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;

	sc->chip_init = acx100_init;
	sc->chip_set_wepkey = acx100_set_wepkey;
	sc->chip_read_config = acx100_read_config;
	sc->chip_write_config = acx100_write_config;
	sc->chip_set_fw_txdesc_rate = acx100_set_fw_txdesc_rate;
	sc->chip_set_bss_join_param = acx100_set_bss_join_param;
	sc->chip_proc_wep_rxbuf = acx100_proc_wep_rxbuf;
}

int
acx100_init(struct acx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	/*
	 * NOTE:
	 * Order of initialization:
	 * 1) WEP
	 * 2) Templates
	 * 3) Firmware TX/RX ring
	 * 4) Hardware memory
	 * Above order is critical to get a correct memory map
	 */
	if (acx100_init_wep(sc) != 0) {
		printf("%s: %s can't initialize wep\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}

	if (acx100_init_tmplt(sc) != 0) {
		printf("%s: %s can't initialize templates\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}

	if (acx100_init_fw_ring(sc) != 0) {
		printf("%s: %s can't initialize fw ring\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}

	if (acx100_init_memory(sc) != 0) {
		printf("%s: %s can't initialize hw memory\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}

	return (0);
}

int
acx100_init_wep(struct acx_softc *sc)
{
	struct acx_conf_wepopt wep_opt;
	struct acx_conf_mmap mem_map;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	/* Set WEP cache start/end address */
	if (acx_get_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		printf("%s: can't get mmap\n", ifp->if_xname);
		return (1);
	}

	mem_map.wep_cache_start = htole32(letoh32(mem_map.code_end) + 4);
	mem_map.wep_cache_end = htole32(letoh32(mem_map.code_end) + 4);
	if (acx_set_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		printf("%s: can't set mmap\n", ifp->if_xname);
		return (1);
	}

	/* Set WEP options */
	wep_opt.nkey = htole16(IEEE80211_WEP_NKID + 10);
	wep_opt.opt = WEPOPT_HDWEP;
	if (acx_set_conf(sc, ACX_CONF_WEPOPT, &wep_opt, sizeof(wep_opt)) != 0) {
		printf("%s: can't set wep opt\n", ifp->if_xname);
		return (1);
	}

	return (0);
}

int
acx100_init_tmplt(struct acx_softc *sc)
{
	struct acx_conf_mmap mem_map;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	/* Set templates start address */
	if (acx_get_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		printf("%s: can't get mmap\n", ifp->if_xname);
		return (1);
	}

	mem_map.pkt_tmplt_start = mem_map.wep_cache_end;
	if (acx_set_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		printf("%s: can't set mmap\n", ifp->if_xname);
		return (1);
	}

	/* Initialize various packet templates */
	if (acx_init_tmplt_ordered(sc) != 0) {
		printf("%s: can't init tmplt\n", ifp->if_xname);
		return (1);
	}

	return (0);
}

int
acx100_init_fw_ring(struct acx_softc *sc)
{
	struct acx100_conf_fw_ring ring;
	struct acx_conf_mmap mem_map;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t txring_start, rxring_start, ring_end;

	/* Set firmware descriptor ring start address */
	if (acx_get_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		printf("%s: can't get mmap\n", ifp->if_xname);
		return (1);
	}

	txring_start = letoh32(mem_map.pkt_tmplt_end) + 4;
	rxring_start = txring_start + ACX100_FW_TXRING_SIZE;
	ring_end = rxring_start + ACX100_FW_RXRING_SIZE;

	mem_map.fw_desc_start = htole32(txring_start);
	if (acx_set_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		printf("%s: can't set mmap\n", ifp->if_xname);
		return (1);
	}

	/* Set firmware descriptor ring configure */
	bzero(&ring, sizeof(ring));
	ring.fw_ring_size = htole32(ACX100_FW_TXRING_SIZE +
	    ACX100_FW_RXRING_SIZE + 8);

	ring.fw_txring_num = 1;
	ring.fw_txring_addr = htole32(txring_start);
	ring.fw_txring_prio = ACX100_TXRING_PRIO_DEFAULT;
	ring.fw_txdesc_num = 0; /* XXX ignored?? */

	ring.fw_rxring_addr = htole32(rxring_start);
	ring.fw_rxdesc_num = 0; /* XXX ignored?? */

	ring.opt = ACX100_RINGOPT_AUTO_RESET;
	ACX100_SET_RING_END(&ring, ring_end);
	if (acx_set_conf(sc, ACX100_CONF_FW_RING, &ring, sizeof(ring)) != 0) {
		printf("%s: can't set fw ring configure\n", ifp->if_xname);
		return (1);
	}

	/* Setup firmware TX/RX descriptor ring */
	acx100_init_fw_txring(sc, txring_start);
	acx100_init_fw_rxring(sc, rxring_start);

	return (0);
}

#define MEMBLK_ALIGN(addr)	\
    (((addr) + (ACX100_MEMBLK_ALIGN - 1)) & ~(ACX100_MEMBLK_ALIGN - 1))

int
acx100_init_memory(struct acx_softc *sc)
{
	struct acx100_conf_memblk_size memblk_sz;
	struct acx100_conf_mem mem;
	struct acx_conf_mmap mem_map;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t memblk_start, memblk_end;
	int total_memblk, txblk_num, rxblk_num;

	/* Set memory block start address */
	if (acx_get_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		printf("%s: can't get mmap\n", ifp->if_xname);
		return (1);
	}

	mem_map.memblk_start =
	    htole32(MEMBLK_ALIGN(letoh32(mem_map.fw_desc_end) + 4));

	if (acx_set_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		printf("%s: can't set mmap\n", ifp->if_xname);
		return (1);
	}

	/* Set memory block size */
	memblk_sz.memblk_size = htole16(ACX_MEMBLOCK_SIZE);
	if (acx_set_conf(sc, ACX_CONF_MEMBLK_SIZE, &memblk_sz,
	    sizeof(memblk_sz)) != 0) {
		printf("%s: can't set mem block size\n", ifp->if_xname);
		return (1);
	}

	/* Get memory map after setting it */
	if (acx_get_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		printf("%s: can't get mmap again\n", ifp->if_xname);
		return (1);
	}
	memblk_start = letoh32(mem_map.memblk_start);
	memblk_end = letoh32(mem_map.memblk_end);

	/* Set memory options */
	mem.opt = htole32(ACX100_MEMOPT_MEMBLOCK | ACX100_MEMOPT_HOSTDESC);
	mem.h_rxring_paddr = htole32(sc->sc_ring_data.rx_ring_paddr);

	total_memblk = (memblk_end - memblk_start) / ACX_MEMBLOCK_SIZE;

	rxblk_num = total_memblk / 2;		/* 50% */
	txblk_num = total_memblk - rxblk_num;	/* 50% */

	DPRINTF(("%s: \ttotal memory blocks\t%d\n"
	    "\trx memory blocks\t%d\n"
	    "\ttx memory blocks\t%d\n",
	    ifp->if_xname, total_memblk, rxblk_num, txblk_num));

	mem.rx_memblk_num = htole16(rxblk_num);
	mem.tx_memblk_num = htole16(txblk_num);

	mem.rx_memblk_addr = htole32(MEMBLK_ALIGN(memblk_start));
	mem.tx_memblk_addr = htole32(MEMBLK_ALIGN(memblk_start +
	    (ACX_MEMBLOCK_SIZE * rxblk_num)));

	if (acx_set_conf(sc, ACX100_CONF_MEMOPT, &mem, sizeof(mem)) != 0) {
		printf("%s: can't set mem options\n", ifp->if_xname);
		return (1);
	}

	/* Initialize memory */
	if (acx_exec_command(sc, ACXCMD_INIT_MEM, NULL, 0, NULL, 0) != 0) {
		printf("%s: can't init mem\n", ifp->if_xname);
		return (1);
	}

	return (0);
}

#undef MEMBLK_ALIGN

void
acx100_init_fw_txring(struct acx_softc *sc, uint32_t fw_txdesc_start)
{
	struct acx_fw_txdesc fw_desc;
	struct acx_txbuf *tx_buf;
	uint32_t desc_paddr, fw_desc_offset;
	int i;

	bzero(&fw_desc, sizeof(fw_desc));
	fw_desc.f_tx_ctrl = DESC_CTRL_HOSTOWN | DESC_CTRL_RECLAIM |
	    DESC_CTRL_AUTODMA | DESC_CTRL_FIRST_FRAG;

	tx_buf = sc->sc_buf_data.tx_buf;
	fw_desc_offset = fw_txdesc_start;
	desc_paddr = sc->sc_ring_data.tx_ring_paddr;

	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		fw_desc.f_tx_host_desc = htole32(desc_paddr);

		if (i == ACX_TX_DESC_CNT - 1) {
			fw_desc.f_tx_next_desc = htole32(fw_txdesc_start);
		} else {
			fw_desc.f_tx_next_desc = htole32(fw_desc_offset +
			    sizeof(struct acx_fw_txdesc));
		}

		tx_buf[i].tb_fwdesc_ofs = fw_desc_offset;
		DESC_WRITE_REGION_1(sc, fw_desc_offset, &fw_desc,
		    sizeof(fw_desc));

		desc_paddr += (2 * sizeof(struct acx_host_desc));
		fw_desc_offset += sizeof(fw_desc);
	}
}

void
acx100_init_fw_rxring(struct acx_softc *sc, uint32_t fw_rxdesc_start)
{
	struct acx_fw_rxdesc fw_desc;
	uint32_t fw_desc_offset;
	int i;

	bzero(&fw_desc, sizeof(fw_desc));
	fw_desc.f_rx_ctrl = DESC_CTRL_RECLAIM | DESC_CTRL_AUTODMA;

	fw_desc_offset = fw_rxdesc_start;

	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		if (i == ACX_RX_DESC_CNT - 1) {
			fw_desc.f_rx_next_desc = htole32(fw_rxdesc_start);
		} else {
			fw_desc.f_rx_next_desc =
			    htole32(fw_desc_offset +
			    sizeof(struct acx_fw_rxdesc));
		}

		DESC_WRITE_REGION_1(sc, fw_desc_offset, &fw_desc,
		    sizeof(fw_desc));

		fw_desc_offset += sizeof(fw_desc);
	}
}

int
acx100_read_config(struct acx_softc *sc, struct acx_config *conf)
{
	struct acx100_conf_cca_mode cca;
	struct acx100_conf_ed_thresh ed;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	/*
	 * NOTE:
	 * CCA mode and ED threshold MUST be read during initialization
	 * or the acx100 card won't work as expected
	 */

	/* Get CCA mode */
	if (acx_get_conf(sc, ACX_CONF_CCA_MODE, &cca, sizeof(cca)) != 0) {
		printf("%s: %s can't get cca mode\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}
	conf->cca_mode = cca.cca_mode;
	DPRINTF(("%s: cca mode %02x\n", ifp->if_xname, cca.cca_mode));

	/* Get ED threshold */
	if (acx_get_conf(sc, ACX_CONF_ED_THRESH, &ed, sizeof(ed)) != 0) {
		printf("%s: %s can't get ed threshold\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}
	conf->ed_thresh = ed.ed_thresh;
	DPRINTF(("%s: ed threshold %02x\n", ifp->if_xname, ed.ed_thresh));

	return (0);
}

int
acx100_write_config(struct acx_softc *sc, struct acx_config *conf)
{
	struct acx100_conf_cca_mode cca;
	struct acx100_conf_ed_thresh ed;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	/* Set CCA mode */
	cca.cca_mode = conf->cca_mode;
	if (acx_set_conf(sc, ACX_CONF_CCA_MODE, &cca, sizeof(cca)) != 0) {
		printf("%s: %s can't set cca mode\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}

	/* Set ED threshold */
	ed.ed_thresh = conf->ed_thresh;
	if (acx_set_conf(sc, ACX_CONF_ED_THRESH, &ed, sizeof(ed)) != 0) {
		printf("%s: %s can't set ed threshold\n",
		    ifp->if_xname, __func__);
		return (ENXIO);
	}

	/* Set TX power */
	acx100_set_txpower(sc);	/* ignore return value */

	return (0);
}

int
acx100_set_txpower(struct acx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	const uint8_t *map;

	switch (sc->sc_radio_type) {
	case ACX_RADIO_TYPE_MAXIM:
		map = acx100_txpower_maxim;
		break;
	case ACX_RADIO_TYPE_RFMD:
	case ACX_RADIO_TYPE_RALINK:
		map = acx100_txpower_rfmd;
		break;
	default:
		printf("%s: TX power for radio type 0x%02x can't be set yet\n",
		    ifp->if_xname, sc->sc_radio_type);
		return (1);
	}

	acx_write_phyreg(sc, ACXRV_PHYREG_TXPOWER, map[ACX100_TXPOWER]);

	return (0);
}

void
acx100_set_fw_txdesc_rate(struct acx_softc *sc, struct acx_txbuf *tx_buf,
    int rate)
{
	FW_TXDESC_SETFIELD_1(sc, tx_buf, f_tx_rate100, ACX100_RATE(rate));
}

void
acx100_set_bss_join_param(struct acx_softc *sc, void *param, int dtim_intvl)
{
	struct acx100_bss_join *bj = param;

	bj->dtim_intvl = dtim_intvl;
	bj->basic_rates = 15;	/* XXX */
	bj->all_rates = 31;	/* XXX */
}

int
acx100_set_wepkey(struct acx_softc *sc, struct ieee80211_key *k, int k_idx)
{
	struct acx100_conf_wepkey conf_wk;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (k->k_len > ACX100_WEPKEY_LEN) {
		printf("%s: %dth WEP key size beyond %d\n",
		    ifp->if_xname, k_idx, ACX100_WEPKEY_LEN);
		return EINVAL;
	}

	conf_wk.action = ACX100_WEPKEY_ACT_ADD;
	conf_wk.key_len = k->k_len;
	conf_wk.key_idx = k_idx;
	bcopy(k->k_key, conf_wk.key, k->k_len);
	if (acx_set_conf(sc, ACX_CONF_WEPKEY, &conf_wk, sizeof(conf_wk)) != 0) {
		printf("%s: %s set %dth WEP key failed\n",
		    ifp->if_xname, __func__, k_idx);
		return ENXIO;
	}
	return 0;
}

void
acx100_proc_wep_rxbuf(struct acx_softc *sc, struct mbuf *m, int *len)
{
	int mac_hdrlen;
	struct ieee80211_frame *f;

	/*
	 * Strip leading IV and KID, and trailing CRC
	 */
	f = mtod(m, struct ieee80211_frame *);

	if (ieee80211_has_addr4(f))
		mac_hdrlen = sizeof(struct ieee80211_frame_addr4);
	else
		mac_hdrlen = sizeof(struct ieee80211_frame);

#define IEEEWEP_IVLEN	(IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN)
#define IEEEWEP_EXLEN	(IEEEWEP_IVLEN + IEEE80211_WEP_CRCLEN)

	*len = *len - IEEEWEP_EXLEN;

	/* Move MAC header toward frame body */
	memmove((uint8_t *)f + IEEEWEP_IVLEN, f, mac_hdrlen);
	m_adj(m, IEEEWEP_IVLEN);

#undef IEEEWEP_EXLEN
#undef IEEEWEP_IVLEN
}
