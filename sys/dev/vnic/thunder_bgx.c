/*
 * Copyright (C) 2015 Cavium Inc.
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
 *
 */
#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitset.h>
#include <sys/bitstring.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/pciio.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/cpuset.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "thunder_bgx.h"
#include "thunder_bgx_var.h"
#include "nic_reg.h"
#include "nic.h"

#include "lmac_if.h"

#define	THUNDER_BGX_DEVSTR	"ThunderX BGX Ethernet I/O Interface"

MALLOC_DEFINE(M_BGX, "thunder_bgx", "ThunderX BGX dynamic memory");

#define BGX_NODE_ID_MASK	0x1
#define BGX_NODE_ID_SHIFT	24

#define DRV_NAME	"thunder-BGX"
#define DRV_VERSION	"1.0"

static int bgx_init_phy(struct bgx *);

static struct bgx *bgx_vnic[MAX_BGX_THUNDER];
static int lmac_count __unused; /* Total no of LMACs in system */

static int bgx_xaui_check_link(struct lmac *lmac);
static void bgx_get_qlm_mode(struct bgx *);
static void bgx_init_hw(struct bgx *);
static int bgx_lmac_enable(struct bgx *, uint8_t);
static void bgx_lmac_disable(struct bgx *, uint8_t);

static int thunder_bgx_probe(device_t);
static int thunder_bgx_attach(device_t);
static int thunder_bgx_detach(device_t);

static device_method_t thunder_bgx_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		thunder_bgx_probe),
	DEVMETHOD(device_attach,	thunder_bgx_attach),
	DEVMETHOD(device_detach,	thunder_bgx_detach),

	DEVMETHOD_END,
};

static driver_t thunder_bgx_driver = {
	"bgx",
	thunder_bgx_methods,
	sizeof(struct lmac),
};

static devclass_t thunder_bgx_devclass;

DRIVER_MODULE(thunder_bgx, pci, thunder_bgx_driver, thunder_bgx_devclass, 0, 0);
MODULE_VERSION(thunder_bgx, 1);
MODULE_DEPEND(thunder_bgx, pci, 1, 1, 1);
MODULE_DEPEND(thunder_bgx, ether, 1, 1, 1);
MODULE_DEPEND(thunder_bgx, thunder_mdio, 1, 1, 1);

static int
thunder_bgx_probe(device_t dev)
{
	uint16_t vendor_id;
	uint16_t device_id;

	vendor_id = pci_get_vendor(dev);
	device_id = pci_get_device(dev);

	if (vendor_id == PCI_VENDOR_ID_CAVIUM &&
	    device_id == PCI_DEVICE_ID_THUNDER_BGX) {
		device_set_desc(dev, THUNDER_BGX_DEVSTR);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
thunder_bgx_attach(device_t dev)
{
	struct bgx *bgx;
	uint8_t lmacid;
	int err;
	int rid;
	struct lmac *lmac;

	bgx = malloc(sizeof(*bgx), M_BGX, (M_WAITOK | M_ZERO));
	bgx->dev = dev;

	lmac = device_get_softc(dev);
	lmac->bgx = bgx;
	/* Enable bus mastering */
	pci_enable_busmaster(dev);
	/* Allocate resources - configuration registers */
	rid = PCIR_BAR(PCI_CFG_REG_BAR_NUM);
	bgx->reg_base = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (bgx->reg_base == NULL) {
		device_printf(dev, "Could not allocate CSR memory space\n");
		err = ENXIO;
		goto err_disable_device;
	}

	bgx->bgx_id = (rman_get_start(bgx->reg_base) >> BGX_NODE_ID_SHIFT) &
	    BGX_NODE_ID_MASK;
	bgx->bgx_id += nic_get_node_id(bgx->reg_base) * MAX_BGX_PER_CN88XX;

	bgx_vnic[bgx->bgx_id] = bgx;
	bgx_get_qlm_mode(bgx);

	err = bgx_init_phy(bgx);
	if (err != 0)
		goto err_free_res;

	bgx_init_hw(bgx);

	/* Enable all LMACs */
	for (lmacid = 0; lmacid < bgx->lmac_count; lmacid++) {
		err = bgx_lmac_enable(bgx, lmacid);
		if (err) {
			device_printf(dev, "BGX%d failed to enable lmac%d\n",
				bgx->bgx_id, lmacid);
			goto err_free_res;
		}
	}

	return (0);

err_free_res:
	bgx_vnic[bgx->bgx_id] = NULL;
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(bgx->reg_base), bgx->reg_base);
err_disable_device:
	free(bgx, M_BGX);
	pci_disable_busmaster(dev);

	return (err);
}

static int
thunder_bgx_detach(device_t dev)
{
	struct lmac *lmac;
	struct bgx *bgx;
	uint8_t lmacid;

	lmac = device_get_softc(dev);
	bgx = lmac->bgx;
	/* Disable all LMACs */
	for (lmacid = 0; lmacid < bgx->lmac_count; lmacid++)
		bgx_lmac_disable(bgx, lmacid);

	bgx_vnic[bgx->bgx_id] = NULL;
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(bgx->reg_base), bgx->reg_base);
	free(bgx, M_BGX);
	pci_disable_busmaster(dev);

	return (0);
}

/* Register read/write APIs */
static uint64_t
bgx_reg_read(struct bgx *bgx, uint8_t lmac, uint64_t offset)
{
	bus_space_handle_t addr;

	addr = ((uint32_t)lmac << 20) + offset;

	return (bus_read_8(bgx->reg_base, addr));
}

static void
bgx_reg_write(struct bgx *bgx, uint8_t lmac, uint64_t offset, uint64_t val)
{
	bus_space_handle_t addr;

	addr = ((uint32_t)lmac << 20) + offset;

	bus_write_8(bgx->reg_base, addr, val);
}

static void
bgx_reg_modify(struct bgx *bgx, uint8_t lmac, uint64_t offset, uint64_t val)
{
	bus_space_handle_t addr;

	addr = ((uint32_t)lmac << 20) + offset;

	bus_write_8(bgx->reg_base, addr, val | bus_read_8(bgx->reg_base, addr));
}

static int
bgx_poll_reg(struct bgx *bgx, uint8_t lmac, uint64_t reg, uint64_t mask,
    boolean_t zero)
{
	int timeout = 10;
	uint64_t reg_val;

	while (timeout) {
		reg_val = bgx_reg_read(bgx, lmac, reg);
		if (zero && !(reg_val & mask))
			return (0);
		if (!zero && (reg_val & mask))
			return (0);

		DELAY(100);
		timeout--;
	}
	return (ETIMEDOUT);
}

/* Return number of BGX present in HW */
u_int
bgx_get_map(int node)
{
	int i;
	u_int map = 0;

	for (i = 0; i < MAX_BGX_PER_CN88XX; i++) {
		if (bgx_vnic[(node * MAX_BGX_PER_CN88XX) + i])
			map |= (1 << i);
	}

	return (map);
}

/* Return number of LMAC configured for this BGX */
int
bgx_get_lmac_count(int node, int bgx_idx)
{
	struct bgx *bgx;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (bgx != NULL)
		return (bgx->lmac_count);

	return (0);
}

/* Returns the current link status of LMAC */
void
bgx_get_lmac_link_state(int node, int bgx_idx, int lmacid, void *status)
{
	struct bgx_link_status *link = (struct bgx_link_status *)status;
	struct bgx *bgx;
	struct lmac *lmac;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (bgx == NULL)
		return;

	lmac = &bgx->lmac[lmacid];
	link->link_up = lmac->link_up;
	link->duplex = lmac->last_duplex;
	link->speed = lmac->last_speed;
}

const uint8_t
*bgx_get_lmac_mac(int node, int bgx_idx, int lmacid)
{
	struct bgx *bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];

	if (bgx != NULL)
		return (bgx->lmac[lmacid].mac);

	return (NULL);
}

void
bgx_set_lmac_mac(int node, int bgx_idx, int lmacid, const uint8_t *mac)
{
	struct bgx *bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];

	if (bgx == NULL)
		return;

	memcpy(bgx->lmac[lmacid].mac, mac, ETHER_ADDR_LEN);
}

static void
bgx_sgmii_change_link_state(struct lmac *lmac)
{
	struct bgx *bgx = lmac->bgx;
	uint64_t cmr_cfg;
	uint64_t port_cfg = 0;
	uint64_t misc_ctl = 0;

	cmr_cfg = bgx_reg_read(bgx, lmac->lmacid, BGX_CMRX_CFG);
	cmr_cfg &= ~CMR_EN;
	bgx_reg_write(bgx, lmac->lmacid, BGX_CMRX_CFG, cmr_cfg);

	port_cfg = bgx_reg_read(bgx, lmac->lmacid, BGX_GMP_GMI_PRTX_CFG);
	misc_ctl = bgx_reg_read(bgx, lmac->lmacid, BGX_GMP_PCS_MISCX_CTL);

	if (lmac->link_up) {
		misc_ctl &= ~PCS_MISC_CTL_GMX_ENO;
		port_cfg &= ~GMI_PORT_CFG_DUPLEX;
		port_cfg |=  (lmac->last_duplex << 2);
	} else {
		misc_ctl |= PCS_MISC_CTL_GMX_ENO;
	}

	switch (lmac->last_speed) {
	case 10:
		port_cfg &= ~GMI_PORT_CFG_SPEED; /* speed 0 */
		port_cfg |= GMI_PORT_CFG_SPEED_MSB;  /* speed_msb 1 */
		port_cfg &= ~GMI_PORT_CFG_SLOT_TIME; /* slottime 0 */
		misc_ctl &= ~PCS_MISC_CTL_SAMP_PT_MASK;
		misc_ctl |= 50; /* samp_pt */
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_SLOT, 64);
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_BURST, 0);
		break;
	case 100:
		port_cfg &= ~GMI_PORT_CFG_SPEED; /* speed 0 */
		port_cfg &= ~GMI_PORT_CFG_SPEED_MSB; /* speed_msb 0 */
		port_cfg &= ~GMI_PORT_CFG_SLOT_TIME; /* slottime 0 */
		misc_ctl &= ~PCS_MISC_CTL_SAMP_PT_MASK;
		misc_ctl |= 5; /* samp_pt */
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_SLOT, 64);
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_BURST, 0);
		break;
	case 1000:
		port_cfg |= GMI_PORT_CFG_SPEED; /* speed 1 */
		port_cfg &= ~GMI_PORT_CFG_SPEED_MSB; /* speed_msb 0 */
		port_cfg |= GMI_PORT_CFG_SLOT_TIME; /* slottime 1 */
		misc_ctl &= ~PCS_MISC_CTL_SAMP_PT_MASK;
		misc_ctl |= 1; /* samp_pt */
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_SLOT, 512);
		if (lmac->last_duplex)
			bgx_reg_write(bgx, lmac->lmacid,
				      BGX_GMP_GMI_TXX_BURST, 0);
		else
			bgx_reg_write(bgx, lmac->lmacid,
				      BGX_GMP_GMI_TXX_BURST, 8192);
		break;
	default:
		break;
	}
	bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_PCS_MISCX_CTL, misc_ctl);
	bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_PRTX_CFG, port_cfg);

	port_cfg = bgx_reg_read(bgx, lmac->lmacid, BGX_GMP_GMI_PRTX_CFG);

	/* renable lmac */
	cmr_cfg |= CMR_EN;
	bgx_reg_write(bgx, lmac->lmacid, BGX_CMRX_CFG, cmr_cfg);
}

static void
bgx_lmac_handler(void *arg)
{
	struct lmac *lmac;
	int link, duplex, speed;
	int link_changed = 0;
	int err;

	lmac = (struct lmac *)arg;

	err = LMAC_MEDIA_STATUS(lmac->phy_if_dev, lmac->lmacid,
	    &link, &duplex, &speed);
	if (err != 0)
		goto out;

	if (!link && lmac->last_link)
		link_changed = -1;

	if (link &&
	    (lmac->last_duplex != duplex ||
	     lmac->last_link != link ||
	     lmac->last_speed != speed)) {
			link_changed = 1;
	}

	lmac->last_link = link;
	lmac->last_speed = speed;
	lmac->last_duplex = duplex;

	if (!link_changed)
		goto out;

	if (link_changed > 0)
		lmac->link_up = true;
	else
		lmac->link_up = false;

	if (lmac->is_sgmii)
		bgx_sgmii_change_link_state(lmac);
	else
		bgx_xaui_check_link(lmac);

out:
	callout_reset(&lmac->check_link, hz * 2, bgx_lmac_handler, lmac);
}

uint64_t
bgx_get_rx_stats(int node, int bgx_idx, int lmac, int idx)
{
	struct bgx *bgx;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (bgx == NULL)
		return (0);

	if (idx > 8)
		lmac = (0);
	return (bgx_reg_read(bgx, lmac, BGX_CMRX_RX_STAT0 + (idx * 8)));
}

uint64_t
bgx_get_tx_stats(int node, int bgx_idx, int lmac, int idx)
{
	struct bgx *bgx;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (bgx == NULL)
		return (0);

	return (bgx_reg_read(bgx, lmac, BGX_CMRX_TX_STAT0 + (idx * 8)));
}

static void
bgx_flush_dmac_addrs(struct bgx *bgx, int lmac)
{
	uint64_t offset;

	while (bgx->lmac[lmac].dmac > 0) {
		offset = ((bgx->lmac[lmac].dmac - 1) * sizeof(uint64_t)) +
		    (lmac * MAX_DMAC_PER_LMAC * sizeof(uint64_t));
		bgx_reg_write(bgx, 0, BGX_CMR_RX_DMACX_CAM + offset, 0);
		bgx->lmac[lmac].dmac--;
	}
}

void
bgx_add_dmac_addr(uint64_t dmac, int node, int bgx_idx, int lmac)
{
	uint64_t offset;
	struct bgx *bgx;

#ifdef BGX_IN_PROMISCUOUS_MODE
	return;
#endif

	bgx_idx += node * MAX_BGX_PER_CN88XX;
	bgx = bgx_vnic[bgx_idx];

	if (!bgx) {
		device_printf(bgx->dev,
		    "BGX%d not yet initialized, ignoring DMAC addition\n",
		    bgx_idx);
		return;
	}

	dmac = dmac | (1UL << 48) | ((uint64_t)lmac << 49); /* Enable DMAC */
	if (bgx->lmac[lmac].dmac == MAX_DMAC_PER_LMAC) {
		device_printf(bgx->dev,
		    "Max DMAC filters for LMAC%d reached, ignoring\n",
		    lmac);
		return;
	}

	if (bgx->lmac[lmac].dmac == MAX_DMAC_PER_LMAC_TNS_BYPASS_MODE)
		bgx->lmac[lmac].dmac = 1;

	offset = (bgx->lmac[lmac].dmac * sizeof(uint64_t)) +
	    (lmac * MAX_DMAC_PER_LMAC * sizeof(uint64_t));
	bgx_reg_write(bgx, 0, BGX_CMR_RX_DMACX_CAM + offset, dmac);
	bgx->lmac[lmac].dmac++;

	bgx_reg_write(bgx, lmac, BGX_CMRX_RX_DMAC_CTL,
	    (CAM_ACCEPT << 3) | (MCAST_MODE_CAM_FILTER << 1) |
	    (BCAST_ACCEPT << 0));
}

/* Configure BGX LMAC in internal loopback mode */
void
bgx_lmac_internal_loopback(int node, int bgx_idx,
    int lmac_idx, boolean_t enable)
{
	struct bgx *bgx;
	struct lmac *lmac;
	uint64_t cfg;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (bgx == NULL)
		return;

	lmac = &bgx->lmac[lmac_idx];
	if (lmac->is_sgmii) {
		cfg = bgx_reg_read(bgx, lmac_idx, BGX_GMP_PCS_MRX_CTL);
		if (enable)
			cfg |= PCS_MRX_CTL_LOOPBACK1;
		else
			cfg &= ~PCS_MRX_CTL_LOOPBACK1;
		bgx_reg_write(bgx, lmac_idx, BGX_GMP_PCS_MRX_CTL, cfg);
	} else {
		cfg = bgx_reg_read(bgx, lmac_idx, BGX_SPUX_CONTROL1);
		if (enable)
			cfg |= SPU_CTL_LOOPBACK;
		else
			cfg &= ~SPU_CTL_LOOPBACK;
		bgx_reg_write(bgx, lmac_idx, BGX_SPUX_CONTROL1, cfg);
	}
}

static int
bgx_lmac_sgmii_init(struct bgx *bgx, int lmacid)
{
	uint64_t cfg;

	bgx_reg_modify(bgx, lmacid, BGX_GMP_GMI_TXX_THRESH, 0x30);
	/* max packet size */
	bgx_reg_modify(bgx, lmacid, BGX_GMP_GMI_RXX_JABBER, MAX_FRAME_SIZE);

	/* Disable frame alignment if using preamble */
	cfg = bgx_reg_read(bgx, lmacid, BGX_GMP_GMI_TXX_APPEND);
	if (cfg & 1)
		bgx_reg_write(bgx, lmacid, BGX_GMP_GMI_TXX_SGMII_CTL, 0);

	/* Enable lmac */
	bgx_reg_modify(bgx, lmacid, BGX_CMRX_CFG, CMR_EN);

	/* PCS reset */
	bgx_reg_modify(bgx, lmacid, BGX_GMP_PCS_MRX_CTL, PCS_MRX_CTL_RESET);
	if (bgx_poll_reg(bgx, lmacid, BGX_GMP_PCS_MRX_CTL,
	    PCS_MRX_CTL_RESET, TRUE) != 0) {
		device_printf(bgx->dev, "BGX PCS reset not completed\n");
		return (ENXIO);
	}

	/* power down, reset autoneg, autoneg enable */
	cfg = bgx_reg_read(bgx, lmacid, BGX_GMP_PCS_MRX_CTL);
	cfg &= ~PCS_MRX_CTL_PWR_DN;
	cfg |= (PCS_MRX_CTL_RST_AN | PCS_MRX_CTL_AN_EN);
	bgx_reg_write(bgx, lmacid, BGX_GMP_PCS_MRX_CTL, cfg);

	if (bgx_poll_reg(bgx, lmacid, BGX_GMP_PCS_MRX_STATUS,
	    PCS_MRX_STATUS_AN_CPT, FALSE) != 0) {
		device_printf(bgx->dev, "BGX AN_CPT not completed\n");
		return (ENXIO);
	}

	return (0);
}

static int
bgx_lmac_xaui_init(struct bgx *bgx, int lmacid, int lmac_type)
{
	uint64_t cfg;

	/* Reset SPU */
	bgx_reg_modify(bgx, lmacid, BGX_SPUX_CONTROL1, SPU_CTL_RESET);
	if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_CONTROL1,
	    SPU_CTL_RESET, TRUE) != 0) {
		device_printf(bgx->dev, "BGX SPU reset not completed\n");
		return (ENXIO);
	}

	/* Disable LMAC */
	cfg = bgx_reg_read(bgx, lmacid, BGX_CMRX_CFG);
	cfg &= ~CMR_EN;
	bgx_reg_write(bgx, lmacid, BGX_CMRX_CFG, cfg);

	bgx_reg_modify(bgx, lmacid, BGX_SPUX_CONTROL1, SPU_CTL_LOW_POWER);
	/* Set interleaved running disparity for RXAUI */
	if (bgx->lmac_type != BGX_MODE_RXAUI) {
		bgx_reg_modify(bgx, lmacid,
		    BGX_SPUX_MISC_CONTROL, SPU_MISC_CTL_RX_DIS);
	} else {
		bgx_reg_modify(bgx, lmacid, BGX_SPUX_MISC_CONTROL,
		    SPU_MISC_CTL_RX_DIS | SPU_MISC_CTL_INTLV_RDISP);
	}

	/* clear all interrupts */
	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_RX_INT);
	bgx_reg_write(bgx, lmacid, BGX_SMUX_RX_INT, cfg);
	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_TX_INT);
	bgx_reg_write(bgx, lmacid, BGX_SMUX_TX_INT, cfg);
	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_INT);
	bgx_reg_write(bgx, lmacid, BGX_SPUX_INT, cfg);

	if (bgx->use_training) {
		bgx_reg_write(bgx, lmacid, BGX_SPUX_BR_PMD_LP_CUP, 0x00);
		bgx_reg_write(bgx, lmacid, BGX_SPUX_BR_PMD_LD_CUP, 0x00);
		bgx_reg_write(bgx, lmacid, BGX_SPUX_BR_PMD_LD_REP, 0x00);
		/* training enable */
		bgx_reg_modify(bgx, lmacid, BGX_SPUX_BR_PMD_CRTL,
		    SPU_PMD_CRTL_TRAIN_EN);
	}

	/* Append FCS to each packet */
	bgx_reg_modify(bgx, lmacid, BGX_SMUX_TX_APPEND, SMU_TX_APPEND_FCS_D);

	/* Disable forward error correction */
	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_FEC_CONTROL);
	cfg &= ~SPU_FEC_CTL_FEC_EN;
	bgx_reg_write(bgx, lmacid, BGX_SPUX_FEC_CONTROL, cfg);

	/* Disable autoneg */
	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_AN_CONTROL);
	cfg = cfg & ~(SPU_AN_CTL_AN_EN | SPU_AN_CTL_XNP_EN);
	bgx_reg_write(bgx, lmacid, BGX_SPUX_AN_CONTROL, cfg);

	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_AN_ADV);
	if (bgx->lmac_type == BGX_MODE_10G_KR)
		cfg |= (1 << 23);
	else if (bgx->lmac_type == BGX_MODE_40G_KR)
		cfg |= (1 << 24);
	else
		cfg &= ~((1 << 23) | (1 << 24));
	cfg = cfg & (~((1UL << 25) | (1UL << 22) | (1UL << 12)));
	bgx_reg_write(bgx, lmacid, BGX_SPUX_AN_ADV, cfg);

	cfg = bgx_reg_read(bgx, 0, BGX_SPU_DBG_CONTROL);
	cfg &= ~SPU_DBG_CTL_AN_ARB_LINK_CHK_EN;
	bgx_reg_write(bgx, 0, BGX_SPU_DBG_CONTROL, cfg);

	/* Enable lmac */
	bgx_reg_modify(bgx, lmacid, BGX_CMRX_CFG, CMR_EN);

	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_CONTROL1);
	cfg &= ~SPU_CTL_LOW_POWER;
	bgx_reg_write(bgx, lmacid, BGX_SPUX_CONTROL1, cfg);

	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_TX_CTL);
	cfg &= ~SMU_TX_CTL_UNI_EN;
	cfg |= SMU_TX_CTL_DIC_EN;
	bgx_reg_write(bgx, lmacid, BGX_SMUX_TX_CTL, cfg);

	/* take lmac_count into account */
	bgx_reg_modify(bgx, lmacid, BGX_SMUX_TX_THRESH, (0x100 - 1));
	/* max packet size */
	bgx_reg_modify(bgx, lmacid, BGX_SMUX_RX_JABBER, MAX_FRAME_SIZE);

	return (0);
}

static int
bgx_xaui_check_link(struct lmac *lmac)
{
	struct bgx *bgx = lmac->bgx;
	int lmacid = lmac->lmacid;
	int lmac_type = bgx->lmac_type;
	uint64_t cfg;

	bgx_reg_modify(bgx, lmacid, BGX_SPUX_MISC_CONTROL, SPU_MISC_CTL_RX_DIS);
	if (bgx->use_training) {
		cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_INT);
		if ((cfg & (1UL << 13)) == 0) {
			cfg = (1UL << 13) | (1UL << 14);
			bgx_reg_write(bgx, lmacid, BGX_SPUX_INT, cfg);
			cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_BR_PMD_CRTL);
			cfg |= (1UL << 0);
			bgx_reg_write(bgx, lmacid, BGX_SPUX_BR_PMD_CRTL, cfg);
			return (ENXIO);
		}
	}

	/* wait for PCS to come out of reset */
	if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_CONTROL1,
	    SPU_CTL_RESET, TRUE) != 0) {
		device_printf(bgx->dev, "BGX SPU reset not completed\n");
		return (ENXIO);
	}

	if ((lmac_type == BGX_MODE_10G_KR) || (lmac_type == BGX_MODE_XFI) ||
	    (lmac_type == BGX_MODE_40G_KR) || (lmac_type == BGX_MODE_XLAUI)) {
		if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_BR_STATUS1,
		    SPU_BR_STATUS_BLK_LOCK, FALSE)) {
			device_printf(bgx->dev,
			    "SPU_BR_STATUS_BLK_LOCK not completed\n");
			return (ENXIO);
		}
	} else {
		if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_BX_STATUS,
		    SPU_BX_STATUS_RX_ALIGN, FALSE) != 0) {
			device_printf(bgx->dev,
			    "SPU_BX_STATUS_RX_ALIGN not completed\n");
			return (ENXIO);
		}
	}

	/* Clear rcvflt bit (latching high) and read it back */
	bgx_reg_modify(bgx, lmacid, BGX_SPUX_STATUS2, SPU_STATUS2_RCVFLT);
	if (bgx_reg_read(bgx, lmacid, BGX_SPUX_STATUS2) & SPU_STATUS2_RCVFLT) {
		device_printf(bgx->dev, "Receive fault, retry training\n");
		if (bgx->use_training) {
			cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_INT);
			if ((cfg & (1UL << 13)) == 0) {
				cfg = (1UL << 13) | (1UL << 14);
				bgx_reg_write(bgx, lmacid, BGX_SPUX_INT, cfg);
				cfg = bgx_reg_read(bgx, lmacid,
				    BGX_SPUX_BR_PMD_CRTL);
				cfg |= (1UL << 0);
				bgx_reg_write(bgx, lmacid,
				    BGX_SPUX_BR_PMD_CRTL, cfg);
				return (ENXIO);
			}
		}
		return (ENXIO);
	}

	/* Wait for MAC RX to be ready */
	if (bgx_poll_reg(bgx, lmacid, BGX_SMUX_RX_CTL,
	    SMU_RX_CTL_STATUS, TRUE) != 0) {
		device_printf(bgx->dev, "SMU RX link not okay\n");
		return (ENXIO);
	}

	/* Wait for BGX RX to be idle */
	if (bgx_poll_reg(bgx, lmacid, BGX_SMUX_CTL,
	    SMU_CTL_RX_IDLE, FALSE) != 0) {
		device_printf(bgx->dev, "SMU RX not idle\n");
		return (ENXIO);
	}

	/* Wait for BGX TX to be idle */
	if (bgx_poll_reg(bgx, lmacid, BGX_SMUX_CTL,
	    SMU_CTL_TX_IDLE, FALSE) != 0) {
		device_printf(bgx->dev, "SMU TX not idle\n");
		return (ENXIO);
	}

	if ((bgx_reg_read(bgx, lmacid, BGX_SPUX_STATUS2) &
	    SPU_STATUS2_RCVFLT) != 0) {
		device_printf(bgx->dev, "Receive fault\n");
		return (ENXIO);
	}

	/* Receive link is latching low. Force it high and verify it */
	bgx_reg_modify(bgx, lmacid, BGX_SPUX_STATUS1, SPU_STATUS1_RCV_LNK);
	if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_STATUS1,
	    SPU_STATUS1_RCV_LNK, FALSE) != 0) {
		device_printf(bgx->dev, "SPU receive link down\n");
		return (ENXIO);
	}

	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_MISC_CONTROL);
	cfg &= ~SPU_MISC_CTL_RX_DIS;
	bgx_reg_write(bgx, lmacid, BGX_SPUX_MISC_CONTROL, cfg);
	return (0);
}

static void
bgx_poll_for_link(void *arg)
{
	struct lmac *lmac;
	uint64_t link;

	lmac = (struct lmac *)arg;

	/* Receive link is latching low. Force it high and verify it */
	bgx_reg_modify(lmac->bgx, lmac->lmacid,
		       BGX_SPUX_STATUS1, SPU_STATUS1_RCV_LNK);
	bgx_poll_reg(lmac->bgx, lmac->lmacid, BGX_SPUX_STATUS1,
		     SPU_STATUS1_RCV_LNK, false);

	link = bgx_reg_read(lmac->bgx, lmac->lmacid, BGX_SPUX_STATUS1);
	if (link & SPU_STATUS1_RCV_LNK) {
		lmac->link_up = 1;
		if (lmac->bgx->lmac_type == BGX_MODE_XLAUI)
			lmac->last_speed = 40000;
		else
			lmac->last_speed = 10000;
		lmac->last_duplex = 1;
	} else {
		lmac->link_up = 0;
	}

	if (lmac->last_link != lmac->link_up) {
		lmac->last_link = lmac->link_up;
		if (lmac->link_up)
			bgx_xaui_check_link(lmac);
	}

	callout_reset(&lmac->check_link, hz * 2, bgx_poll_for_link, lmac);
}

static int
bgx_lmac_enable(struct bgx *bgx, uint8_t lmacid)
{
	uint64_t __unused dmac_bcast = (1UL << 48) - 1;
	struct lmac *lmac;
	uint64_t cfg;

	lmac = &bgx->lmac[lmacid];
	lmac->bgx = bgx;

	if (bgx->lmac_type == BGX_MODE_SGMII) {
		lmac->is_sgmii = 1;
		if (bgx_lmac_sgmii_init(bgx, lmacid) != 0)
			return -1;
	} else {
		lmac->is_sgmii = 0;
		if (bgx_lmac_xaui_init(bgx, lmacid, bgx->lmac_type))
			return -1;
	}

	if (lmac->is_sgmii) {
		cfg = bgx_reg_read(bgx, lmacid, BGX_GMP_GMI_TXX_APPEND);
		cfg |= ((1UL << 2) | (1UL << 1)); /* FCS and PAD */
		bgx_reg_modify(bgx, lmacid, BGX_GMP_GMI_TXX_APPEND, cfg);
		bgx_reg_write(bgx, lmacid, BGX_GMP_GMI_TXX_MIN_PKT, 60 - 1);
	} else {
		cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_TX_APPEND);
		cfg |= ((1UL << 2) | (1UL << 1)); /* FCS and PAD */
		bgx_reg_modify(bgx, lmacid, BGX_SMUX_TX_APPEND, cfg);
		bgx_reg_write(bgx, lmacid, BGX_SMUX_TX_MIN_PKT, 60 + 4);
	}

	/* Enable lmac */
	bgx_reg_modify(bgx, lmacid, BGX_CMRX_CFG,
		       CMR_EN | CMR_PKT_RX_EN | CMR_PKT_TX_EN);

	/* Restore default cfg, incase low level firmware changed it */
	bgx_reg_write(bgx, lmacid, BGX_CMRX_RX_DMAC_CTL, 0x03);

	/* Add broadcast MAC into all LMAC's DMAC filters */
	bgx_add_dmac_addr(dmac_bcast, 0, bgx->bgx_id, lmacid);

	if ((bgx->lmac_type != BGX_MODE_XFI) &&
	    (bgx->lmac_type != BGX_MODE_XAUI) &&
	    (bgx->lmac_type != BGX_MODE_XLAUI) &&
	    (bgx->lmac_type != BGX_MODE_40G_KR) &&
	    (bgx->lmac_type != BGX_MODE_10G_KR)) {
		if (lmac->phy_if_dev == NULL) {
			device_printf(bgx->dev,
			    "LMAC%d missing interface to PHY\n", lmacid);
			return (ENXIO);
		}

		if (LMAC_PHY_CONNECT(lmac->phy_if_dev, lmac->phyaddr,
		    lmacid) != 0) {
			device_printf(bgx->dev,
			    "LMAC%d could not connect to PHY\n", lmacid);
			return (ENXIO);
		}
		mtx_init(&lmac->check_link_mtx, "BGX link poll", NULL, MTX_DEF);
		callout_init_mtx(&lmac->check_link, &lmac->check_link_mtx, 0);
		mtx_lock(&lmac->check_link_mtx);
		bgx_lmac_handler(lmac);
		mtx_unlock(&lmac->check_link_mtx);
	} else {
		mtx_init(&lmac->check_link_mtx, "BGX link poll", NULL, MTX_DEF);
		callout_init_mtx(&lmac->check_link, &lmac->check_link_mtx, 0);
		mtx_lock(&lmac->check_link_mtx);
		bgx_poll_for_link(lmac);
		mtx_unlock(&lmac->check_link_mtx);
	}

	return (0);
}

static void
bgx_lmac_disable(struct bgx *bgx, uint8_t lmacid)
{
	struct lmac *lmac;
	uint64_t cmrx_cfg;

	lmac = &bgx->lmac[lmacid];

	/* Stop callout */
	callout_drain(&lmac->check_link);
	mtx_destroy(&lmac->check_link_mtx);

	cmrx_cfg = bgx_reg_read(bgx, lmacid, BGX_CMRX_CFG);
	cmrx_cfg &= ~(1 << 15);
	bgx_reg_write(bgx, lmacid, BGX_CMRX_CFG, cmrx_cfg);
	bgx_flush_dmac_addrs(bgx, lmacid);

	if ((bgx->lmac_type != BGX_MODE_XFI) &&
	    (bgx->lmac_type != BGX_MODE_XLAUI) &&
	    (bgx->lmac_type != BGX_MODE_40G_KR) &&
	    (bgx->lmac_type != BGX_MODE_10G_KR)) {
		if (lmac->phy_if_dev == NULL) {
			device_printf(bgx->dev,
			    "LMAC%d missing interface to PHY\n", lmacid);
			return;
		}
		if (LMAC_PHY_DISCONNECT(lmac->phy_if_dev, lmac->phyaddr,
		    lmacid) != 0) {
			device_printf(bgx->dev,
			    "LMAC%d could not disconnect PHY\n", lmacid);
			return;
		}
		lmac->phy_if_dev = NULL;
	}
}

static void
bgx_set_num_ports(struct bgx *bgx)
{
	uint64_t lmac_count;

	switch (bgx->qlm_mode) {
	case QLM_MODE_SGMII:
		bgx->lmac_count = 4;
		bgx->lmac_type = BGX_MODE_SGMII;
		bgx->lane_to_sds = 0;
		break;
	case QLM_MODE_XAUI_1X4:
		bgx->lmac_count = 1;
		bgx->lmac_type = BGX_MODE_XAUI;
		bgx->lane_to_sds = 0xE4;
			break;
	case QLM_MODE_RXAUI_2X2:
		bgx->lmac_count = 2;
		bgx->lmac_type = BGX_MODE_RXAUI;
		bgx->lane_to_sds = 0xE4;
			break;
	case QLM_MODE_XFI_4X1:
		bgx->lmac_count = 4;
		bgx->lmac_type = BGX_MODE_XFI;
		bgx->lane_to_sds = 0;
		break;
	case QLM_MODE_XLAUI_1X4:
		bgx->lmac_count = 1;
		bgx->lmac_type = BGX_MODE_XLAUI;
		bgx->lane_to_sds = 0xE4;
		break;
	case QLM_MODE_10G_KR_4X1:
		bgx->lmac_count = 4;
		bgx->lmac_type = BGX_MODE_10G_KR;
		bgx->lane_to_sds = 0;
		bgx->use_training = 1;
		break;
	case QLM_MODE_40G_KR4_1X4:
		bgx->lmac_count = 1;
		bgx->lmac_type = BGX_MODE_40G_KR;
		bgx->lane_to_sds = 0xE4;
		bgx->use_training = 1;
		break;
	default:
		bgx->lmac_count = 0;
		break;
	}

	/*
	 * Check if low level firmware has programmed LMAC count
	 * based on board type, if yes consider that otherwise
	 * the default static values
	 */
	lmac_count = bgx_reg_read(bgx, 0, BGX_CMR_RX_LMACS) & 0x7;
	if (lmac_count != 4)
		bgx->lmac_count = lmac_count;
}

static void
bgx_init_hw(struct bgx *bgx)
{
	int i;

	bgx_set_num_ports(bgx);

	bgx_reg_modify(bgx, 0, BGX_CMR_GLOBAL_CFG, CMR_GLOBAL_CFG_FCS_STRIP);
	if (bgx_reg_read(bgx, 0, BGX_CMR_BIST_STATUS))
		device_printf(bgx->dev, "BGX%d BIST failed\n", bgx->bgx_id);

	/* Set lmac type and lane2serdes mapping */
	for (i = 0; i < bgx->lmac_count; i++) {
		if (bgx->lmac_type == BGX_MODE_RXAUI) {
			if (i)
				bgx->lane_to_sds = 0x0e;
			else
				bgx->lane_to_sds = 0x04;
			bgx_reg_write(bgx, i, BGX_CMRX_CFG,
			    (bgx->lmac_type << 8) | bgx->lane_to_sds);
			continue;
		}
		bgx_reg_write(bgx, i, BGX_CMRX_CFG,
		    (bgx->lmac_type << 8) | (bgx->lane_to_sds + i));
		bgx->lmac[i].lmacid_bd = lmac_count;
		lmac_count++;
	}

	bgx_reg_write(bgx, 0, BGX_CMR_TX_LMACS, bgx->lmac_count);
	bgx_reg_write(bgx, 0, BGX_CMR_RX_LMACS, bgx->lmac_count);

	/* Set the backpressure AND mask */
	for (i = 0; i < bgx->lmac_count; i++) {
		bgx_reg_modify(bgx, 0, BGX_CMR_CHAN_MSK_AND,
		    ((1UL << MAX_BGX_CHANS_PER_LMAC) - 1) <<
		    (i * MAX_BGX_CHANS_PER_LMAC));
	}

	/* Disable all MAC filtering */
	for (i = 0; i < RX_DMAC_COUNT; i++)
		bgx_reg_write(bgx, 0, BGX_CMR_RX_DMACX_CAM + (i * 8), 0x00);

	/* Disable MAC steering (NCSI traffic) */
	for (i = 0; i < RX_TRAFFIC_STEER_RULE_COUNT; i++)
		bgx_reg_write(bgx, 0, BGX_CMR_RX_STREERING + (i * 8), 0x00);
}

static void
bgx_get_qlm_mode(struct bgx *bgx)
{
	device_t dev = bgx->dev;;
	int lmac_type;
	int train_en;

	/* Read LMAC0 type to figure out QLM mode
	 * This is configured by low level firmware
	 */
	lmac_type = bgx_reg_read(bgx, 0, BGX_CMRX_CFG);
	lmac_type = (lmac_type >> 8) & 0x07;

	train_en = bgx_reg_read(bgx, 0, BGX_SPUX_BR_PMD_CRTL) &
	    SPU_PMD_CRTL_TRAIN_EN;

	switch (lmac_type) {
	case BGX_MODE_SGMII:
		bgx->qlm_mode = QLM_MODE_SGMII;
		if (bootverbose) {
			device_printf(dev, "BGX%d QLM mode: SGMII\n",
			    bgx->bgx_id);
		}
		break;
	case BGX_MODE_XAUI:
		bgx->qlm_mode = QLM_MODE_XAUI_1X4;
		if (bootverbose) {
			device_printf(dev, "BGX%d QLM mode: XAUI\n",
			    bgx->bgx_id);
		}
		break;
	case BGX_MODE_RXAUI:
		bgx->qlm_mode = QLM_MODE_RXAUI_2X2;
		if (bootverbose) {
			device_printf(dev, "BGX%d QLM mode: RXAUI\n",
			    bgx->bgx_id);
		}
		break;
	case BGX_MODE_XFI:
		if (!train_en) {
			bgx->qlm_mode = QLM_MODE_XFI_4X1;
			if (bootverbose) {
				device_printf(dev, "BGX%d QLM mode: XFI\n",
				    bgx->bgx_id);
			}
		} else {
			bgx->qlm_mode = QLM_MODE_10G_KR_4X1;
			if (bootverbose) {
				device_printf(dev, "BGX%d QLM mode: 10G_KR\n",
				    bgx->bgx_id);
			}
		}
		break;
	case BGX_MODE_XLAUI:
		if (!train_en) {
			bgx->qlm_mode = QLM_MODE_XLAUI_1X4;
			if (bootverbose) {
				device_printf(dev, "BGX%d QLM mode: XLAUI\n",
				    bgx->bgx_id);
			}
		} else {
			bgx->qlm_mode = QLM_MODE_40G_KR4_1X4;
			if (bootverbose) {
				device_printf(dev, "BGX%d QLM mode: 40G_KR4\n",
				    bgx->bgx_id);
			}
		}
		break;
	default:
		bgx->qlm_mode = QLM_MODE_SGMII;
		if (bootverbose) {
			device_printf(dev, "BGX%d QLM default mode: SGMII\n",
			    bgx->bgx_id);
		}
	}
}

static int
bgx_init_phy(struct bgx *bgx)
{
	int err;

	/* By default we fail */
	err = ENXIO;
#ifdef FDT
	err = bgx_fdt_init_phy(bgx);
#endif
#ifdef ACPI
	if (err != 0) {
		/* ARM64TODO: Add ACPI function here */
	}
#endif
	return (err);
}
