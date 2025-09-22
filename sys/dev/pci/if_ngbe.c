/*	$OpenBSD: if_ngbe.c,v 1.7 2025/06/24 11:04:15 stsp Exp $	*/

/*
 * Copyright (c) 2015-2017 Beijing WangXun Technology Co., Ltd.
 * Copyright (c) 2023 Kevin Lo <kevlo@openbsd.org>
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

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/intrmap.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/toeplitz.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/mii.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_ngbereg.h>

const struct pci_matchid ngbe_devices[] = {
	{ PCI_VENDOR_WANGXUN, PCI_PRODUCT_WANGXUN_WX1860A2 },
	{ PCI_VENDOR_WANGXUN, PCI_PRODUCT_WANGXUN_WX1860AL1 }
};

int			ngbe_match(struct device *, void *, void *);
void			ngbe_attach(struct device *, struct device *, void *);
int			ngbe_detach(struct device *, int);
void			ngbe_init(void *);
int			ngbe_ioctl(struct ifnet *, u_long, caddr_t);
int			ngbe_media_change(struct ifnet *);
void			ngbe_media_status(struct ifnet *, struct ifmediareq *);
int			ngbe_rxfill(struct rx_ring *);
int			ngbe_rxrinfo(struct ngbe_softc *, struct if_rxrinfo *);
void			ngbe_start(struct ifqueue *);
void			ngbe_stop(struct ngbe_softc *);
void			ngbe_update_link_status(struct ngbe_softc *);
void			ngbe_watchdog(struct ifnet *);
int			ngbe_allocate_pci_resources(struct ngbe_softc *);
void			ngbe_free_pci_resources(struct ngbe_softc *);
int			ngbe_allocate_msix(struct ngbe_softc *);
void			ngbe_setup_interface(struct ngbe_softc *);
int			ngbe_setup_msix(struct ngbe_softc *);
int			ngbe_dma_malloc(struct ngbe_softc *, bus_size_t,
			    struct ngbe_dma_alloc *);
void			ngbe_dma_free(struct ngbe_softc *,
			    struct ngbe_dma_alloc *);
int			ngbe_allocate_isb(struct ngbe_softc *);
void			ngbe_free_isb(struct ngbe_softc *);
int			ngbe_allocate_queues(struct ngbe_softc *);
void			ngbe_free_receive_structures(struct ngbe_softc *);
void			ngbe_free_receive_buffers(struct rx_ring *);
void			ngbe_free_transmit_structures(struct ngbe_softc *);
void			ngbe_free_transmit_buffers(struct tx_ring *);
int			ngbe_allocate_receive_buffers(struct rx_ring *);
int			ngbe_allocate_transmit_buffers(struct tx_ring *);
int			ngbe_setup_receive_ring(struct rx_ring *);
int			ngbe_setup_transmit_ring(struct tx_ring *);
int			ngbe_setup_receive_structures(struct ngbe_softc *);
int			ngbe_setup_transmit_structures(struct ngbe_softc *);
uint8_t *		ngbe_addr_list_itr(struct ngbe_hw *, uint8_t **,
			    uint32_t *);
void			ngbe_iff(struct ngbe_softc *);
int			ngbe_initialize_receive_unit(struct ngbe_softc *);
void			ngbe_initialize_rss_mapping(struct ngbe_softc *);
int			ngbe_initialize_transmit_unit(struct ngbe_softc *);
int			ngbe_intr_link(void *);
int			ngbe_intr_queue(void *);
void			ngbe_init_eeprom_params(struct ngbe_hw *);
int			ngbe_init_hw(struct ngbe_softc *);
void			ngbe_init_ops(struct ngbe_hw *);
void			ngbe_init_rx_addrs(struct ngbe_softc *);
void			ngbe_init_shared_code(struct ngbe_softc *);
void			ngbe_init_thermal_sensor_thresh(struct ngbe_hw *);
void			ngbe_init_uta_tables(struct ngbe_hw *);
void			ngbe_fc_autoneg(struct ngbe_softc *);
int			ngbe_fc_autoneg_copper(struct ngbe_softc *);
int			ngbe_fc_enable(struct ngbe_softc *);
int			ngbe_fmgr_cmd_op(struct ngbe_hw *, uint32_t, uint32_t);
uint32_t		ngbe_flash_read_dword(struct ngbe_hw *, uint32_t);
uint8_t			ngbe_calculate_checksum(uint8_t *, uint32_t);
int			ngbe_check_flash_load(struct ngbe_softc *, uint32_t);
int			ngbe_check_internal_phy_id(struct ngbe_softc *);
int			ngbe_check_mac_link(struct ngbe_hw *, uint32_t *, int *,
			    int);
int			ngbe_check_mng_access(struct ngbe_hw *);
int			ngbe_check_reset_blocked(struct ngbe_softc *);
void			ngbe_clear_hw_cntrs(struct ngbe_hw *);
void			ngbe_clear_vfta(struct ngbe_hw *);
void			ngbe_configure_ivars(struct ngbe_softc *);
void			ngbe_configure_pb(struct ngbe_softc *);
void			ngbe_disable_intr(struct ngbe_softc *);
int			ngbe_disable_pcie_master(struct ngbe_softc *);
void			ngbe_disable_queue(struct ngbe_softc *, uint32_t);
void			ngbe_disable_rx(struct ngbe_hw *);
void			ngbe_disable_sec_rx_path(struct ngbe_hw *);
int			ngbe_eepromcheck_cap(struct ngbe_softc *, uint16_t,
			    uint32_t *);
void			ngbe_enable_intr(struct ngbe_softc *);
void			ngbe_enable_queue(struct ngbe_softc *, uint32_t);
void			ngbe_enable_rx(struct ngbe_hw *);
void			ngbe_enable_rx_dma(struct ngbe_hw *, uint32_t);
void			ngbe_enable_sec_rx_path(struct ngbe_hw *);
int			ngbe_encap(struct tx_ring *, struct mbuf *);
int			ngbe_get_buf(struct rx_ring *, int);
void			ngbe_get_bus_info(struct ngbe_softc *);
void			ngbe_get_copper_link_capabilities(struct ngbe_hw *,
			    uint32_t *, int *);
int			ngbe_get_eeprom_semaphore(struct ngbe_softc *);
void			ngbe_get_hw_control(struct ngbe_hw *);
void			ngbe_release_hw_control(struct ngbe_softc *);
void			ngbe_get_mac_addr(struct ngbe_hw *, uint8_t *);
enum ngbe_media_type	ngbe_get_media_type(struct ngbe_hw *);
void			ngbe_gphy_dis_eee(struct ngbe_hw *);
void			ngbe_gphy_efuse_calibration(struct ngbe_softc *);
void			ngbe_gphy_wait_mdio_access_on(struct ngbe_hw *);
void			ngbe_handle_phy_event(struct ngbe_softc *);
int			ngbe_host_interface_command(struct ngbe_softc *,
			    uint32_t *, uint32_t, uint32_t, int);
int			ngbe_hpbthresh(struct ngbe_softc *);
int			ngbe_lpbthresh(struct ngbe_softc *);
int			ngbe_mng_present(struct ngbe_hw *);
int			ngbe_mta_vector(struct ngbe_hw *, uint8_t *);
int			ngbe_negotiate_fc(struct ngbe_softc *, uint32_t,
			    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int			ngbe_non_sfp_link_config(struct ngbe_softc *);
void			ngbe_pbthresh_setup(struct ngbe_softc *);
void			ngbe_phy_check_event(struct ngbe_softc *);
int			ngbe_phy_check_overtemp(struct ngbe_hw *);
void			ngbe_phy_get_advertised_pause(struct ngbe_hw *,
			    uint8_t *);
void			ngbe_phy_get_lp_advertised_pause(struct ngbe_hw *,
			    uint8_t *);
int			ngbe_phy_identify(struct ngbe_softc *);
int			ngbe_phy_init(struct ngbe_softc *);
void			ngbe_phy_led_ctrl(struct ngbe_softc *);
int			ngbe_phy_led_oem_chk(struct ngbe_softc *, uint32_t *);
int			ngbe_phy_read_reg(struct ngbe_hw *, uint32_t, uint32_t,
			    uint16_t *);
int			ngbe_phy_write_reg(struct ngbe_hw *, uint32_t, uint32_t,
			    uint16_t);
int			ngbe_phy_reset(struct ngbe_softc *);
int			ngbe_phy_set_pause_advertisement(struct ngbe_hw *,
			    uint16_t);
int			ngbe_phy_setup(struct ngbe_softc *);
int			ngbe_phy_setup_link(struct ngbe_softc *, uint32_t, int);
uint16_t		ngbe_read_pci_cfg_word(struct ngbe_softc *, uint32_t);
void			ngbe_release_eeprom_semaphore(struct ngbe_hw *);
int			ngbe_acquire_swfw_sync(struct ngbe_softc *, uint32_t);
void			ngbe_release_swfw_sync(struct ngbe_softc *, uint32_t);
void			ngbe_reset(struct ngbe_softc *);
int			ngbe_reset_hw(struct ngbe_softc *);
void			ngbe_reset_misc(struct ngbe_hw *);
int			ngbe_set_fw_drv_ver(struct ngbe_softc *, uint8_t,
			    uint8_t, uint8_t, uint8_t);
void			ngbe_set_ivar(struct ngbe_softc *, uint16_t, uint16_t,
			    int8_t);
void			ngbe_set_lan_id_multi_port_pcie(struct ngbe_hw *);
void			ngbe_set_mta(struct ngbe_hw *, uint8_t *);
void			ngbe_set_pci_config_data(struct ngbe_hw *, uint16_t);
int			ngbe_set_rar(struct ngbe_softc *, uint32_t, uint8_t *,
			    uint64_t, uint32_t);
void			ngbe_set_rx_drop_en(struct ngbe_softc *);
void			ngbe_set_rxpba(struct ngbe_hw *, int, uint32_t, int);
int			ngbe_setup_copper_link(struct ngbe_softc *, uint32_t,
			    int);
int			ngbe_setup_fc(struct ngbe_softc *);
void			ngbe_setup_gpie(struct ngbe_hw *);
void			ngbe_setup_isb(struct ngbe_softc *);
void			ngbe_setup_psrtype(struct ngbe_hw *);
void			ngbe_setup_vlan_hw_support(struct ngbe_softc *);
int			ngbe_start_hw(struct ngbe_softc *);
int			ngbe_stop_adapter(struct ngbe_softc *);
void			ngbe_rx_checksum(uint32_t, struct mbuf *);
void			ngbe_rxeof(struct rx_ring *);
void			ngbe_rxrefill(void *);
int			ngbe_tx_ctx_setup(struct tx_ring *, struct mbuf *,
			    uint32_t *, uint32_t *);
void			ngbe_txeof(struct tx_ring *);
void			ngbe_update_mc_addr_list(struct ngbe_hw *, uint8_t *,
			    uint32_t, ngbe_mc_addr_itr, int);
int			ngbe_validate_mac_addr(uint8_t *);

struct cfdriver ngbe_cd = {
	NULL, "ngbe", DV_IFNET
};

const struct cfattach ngbe_ca = {
	sizeof(struct ngbe_softc), ngbe_match, ngbe_attach, ngbe_detach
};

int
ngbe_match(struct device *parent, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, ngbe_devices,
	    nitems(ngbe_devices));
}

void
ngbe_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct ngbe_softc *sc = (struct ngbe_softc *)self;
	struct ngbe_hw *hw = &sc->hw;
	uint32_t eeprom_cksum_devcap, devcap, led_conf;
	int error;

	sc->osdep.os_sc = sc;
	sc->osdep.os_pa = *pa;

	/* Setup PCI resources. */
	if (ngbe_allocate_pci_resources(sc))
		goto fail1;

	sc->num_tx_desc = NGBE_DEFAULT_TXD;
	sc->num_rx_desc = NGBE_DEFAULT_RXD;

	/* Allocate Tx/Rx queues. */
	if (ngbe_allocate_queues(sc))
		goto fail1;

	/* Allocate multicast array memory. */
	sc->mta = mallocarray(ETHER_ADDR_LEN, NGBE_SP_RAR_ENTRIES, M_DEVBUF,
	    M_NOWAIT);
	if (sc->mta == NULL) {
		printf(": can not allocate multicast setup array\n");
		goto fail1;
	}

	/* Allocate interrupt status resources. */
	if (ngbe_allocate_isb(sc))
		goto fail2;

	hw->mac.autoneg = 1;
	hw->phy.autoneg_advertised = NGBE_LINK_SPEED_AUTONEG;
	hw->phy.force_speed = NGBE_LINK_SPEED_UNKNOWN;

	/* Initialize the shared code. */
	ngbe_init_shared_code(sc);

	sc->hw.mac.ops.set_lan_id(&sc->hw);

	/* Check if flash load is done after hw power up. */
	error = ngbe_check_flash_load(sc, NGBE_SPI_ILDR_STATUS_PERST);
	if (error)
		goto fail3;
	error = ngbe_check_flash_load(sc, NGBE_SPI_ILDR_STATUS_PWRRST);
	if (error)
		goto fail3;

	hw->phy.reset_if_overtemp = 1;
	error = sc->hw.mac.ops.reset_hw(sc);
	hw->phy.reset_if_overtemp = 0;
	if (error) {
		printf(": HW reset failed\n");
		goto fail3;
	}

	eeprom_cksum_devcap = devcap = 0;
	if (hw->bus.lan_id == 0) {
		NGBE_WRITE_REG(hw, NGBE_CALSUM_CAP_STATUS, 0);
		NGBE_WRITE_REG(hw, NGBE_EEPROM_VERSION_STORE_REG, 0);
	} else
		eeprom_cksum_devcap = NGBE_READ_REG(hw, NGBE_CALSUM_CAP_STATUS);

	hw->eeprom.ops.init_params(hw);
	hw->mac.ops.release_swfw_sync(sc, NGBE_MNG_SWFW_SYNC_SW_MB);
	if (hw->bus.lan_id == 0 || eeprom_cksum_devcap == 0) {
		/* Make sure the EEPROM is good */
		if (hw->eeprom.ops.eeprom_chksum_cap_st(sc, NGBE_CALSUM_COMMAND,
		    &devcap)) {
			printf(": eeprom checksum is not valid\n");
			goto fail3;
		}
	}

	led_conf = 0;
	if (hw->eeprom.ops.phy_led_oem_chk(sc, &led_conf))
		sc->led_conf = -1;
	else
		sc->led_conf = led_conf;

	memcpy(sc->sc_ac.ac_enaddr, sc->hw.mac.addr, ETHER_ADDR_LEN);

	error = ngbe_allocate_msix(sc);
	if (error)
		goto fail3;

	ngbe_setup_interface(sc);

	/* Reset the hardware with the new settings */
	error = hw->mac.ops.start_hw(sc);
	if (error) {
		printf(": HW init failed\n");
		goto fail3;
	}

	/* Pick up the PCI bus settings for reporting later */
	hw->mac.ops.get_bus_info(sc);

	hw->mac.ops.set_fw_drv_ver(sc, 0xff, 0xff, 0xff, 0xff);

	printf(", address %s\n", ether_sprintf(sc->hw.mac.addr));
	return;

fail3:
	ngbe_free_isb(sc);
fail2:
	ngbe_free_transmit_structures(sc);
	ngbe_free_receive_structures(sc);
	free(sc->mta, M_DEVBUF, ETHER_ADDR_LEN * NGBE_SP_RAR_ENTRIES);
fail1:
	ngbe_free_pci_resources(sc);
}

int
ngbe_detach(struct device *self, int flags)
{
	struct ngbe_softc *sc = (struct ngbe_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	ngbe_stop(sc);
	ngbe_release_hw_control(sc);

	ether_ifdetach(ifp);
	if_detach(ifp);

	ngbe_free_pci_resources(sc);

	ngbe_free_transmit_structures(sc);                                     
	ngbe_free_receive_structures(sc); 
	ngbe_free_isb(sc);
	free(sc->mta, M_DEVBUF, ETHER_ADDR_LEN * NGBE_SP_RAR_ENTRIES);

	return 0;
}

static inline uint32_t
NGBE_READ_REG_MASK(struct ngbe_hw *hw, uint32_t reg, uint32_t mask)
{
	uint32_t val;

	val = NGBE_READ_REG(hw, reg);
	if (val == NGBE_FAILED_READ_REG)
		return val;
	return val & mask;
}

static inline void
NGBE_WRITE_REG_MASK(struct ngbe_hw *hw, uint32_t reg, uint32_t mask,
    uint32_t field)
{
	uint32_t val;

	val = NGBE_READ_REG(hw, reg);
	if (val == NGBE_FAILED_READ_REG)
		return;
	val = ((val & ~mask) | (field & mask));
	NGBE_WRITE_REG(hw, reg, val);
}

static inline uint32_t
ngbe_misc_isb(struct ngbe_softc *sc, enum ngbe_isb_idx idx)
{
	return htole32(sc->isb_base[idx]);
}

void
ngbe_init(void *arg)
{
	struct ngbe_softc *sc = (struct ngbe_softc *)arg;
	struct ngbe_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i, s;

	s = splnet();

	ngbe_stop(sc);

	ngbe_setup_isb(sc);

	/* Setup the receive address. */                                
	hw->mac.ops.set_rar(sc, 0, hw->mac.addr, 0, NGBE_PSR_MAC_SWC_AD_H_AV);

	/* Get the latest mac address, user can use a LAA. */
	bcopy(sc->sc_ac.ac_enaddr, sc->hw.mac.addr, ETHER_ADDR_LEN);

	hw->mac.ops.set_rar(sc, 0, hw->mac.addr, 0, 1);

	ngbe_configure_pb(sc);

	/* Program promiscuous mode and multicast filters. */
	ngbe_iff(sc);

	ngbe_setup_vlan_hw_support(sc);

	/* Prepare transmit descriptors and buffers. */
	if (ngbe_setup_transmit_structures(sc)) {
		printf("%s: could not setup transmit structures\n",
		    DEVNAME(sc));
		ngbe_stop(sc);
		splx(s);
		return;
	}
	if (ngbe_initialize_transmit_unit(sc)) {
		ngbe_stop(sc);
		splx(s);
		return;
	}

	/* Prepare receive descriptors and buffers. */
	if (ngbe_setup_receive_structures(sc)) {
		printf("%s: could not setup receive structures\n",
		    DEVNAME(sc));
		ngbe_stop(sc);
		splx(s);
		return;
	}
	if (ngbe_initialize_receive_unit(sc)) {
		ngbe_stop(sc);
		splx(s);
		return;
	}

	ngbe_get_hw_control(hw);
	ngbe_setup_gpie(hw);
	ngbe_configure_ivars(sc);

	if (ngbe_non_sfp_link_config(sc)) {
		ngbe_stop(sc);
		splx(s);
		return;
	}

	/* Select GMII */
	NGBE_WRITE_REG(hw, NGBE_MAC_TX_CFG,
	    (NGBE_READ_REG(hw, NGBE_MAC_TX_CFG) & ~NGBE_MAC_TX_CFG_SPEED_MASK) |
	    NGBE_MAC_TX_CFG_SPEED_1G);

	/* Clear any pending interrupts, may auto mask */
	NGBE_READ_REG(hw, NGBE_PX_IC);
	NGBE_READ_REG(hw, NGBE_PX_MISC_IC);
	ngbe_enable_intr(sc);

	switch (hw->bus.lan_id) {
	case 0:
		NGBE_WRITE_REG_MASK(hw, NGBE_MIS_PRB_CTL,
		    NGBE_MIS_PRB_CTL_LAN0_UP, NGBE_MIS_PRB_CTL_LAN0_UP);
		break;
	case 1:
		NGBE_WRITE_REG_MASK(hw, NGBE_MIS_PRB_CTL,
		    NGBE_MIS_PRB_CTL_LAN1_UP, NGBE_MIS_PRB_CTL_LAN1_UP);
		break;
	case 2:
		NGBE_WRITE_REG_MASK(hw, NGBE_MIS_PRB_CTL,
		    NGBE_MIS_PRB_CTL_LAN2_UP, NGBE_MIS_PRB_CTL_LAN2_UP);
		break;
	case 3:
		NGBE_WRITE_REG_MASK(hw, NGBE_MIS_PRB_CTL,
		    NGBE_MIS_PRB_CTL_LAN3_UP, NGBE_MIS_PRB_CTL_LAN3_UP);
		break;
	}

	NGBE_WRITE_REG_MASK(hw, NGBE_CFG_PORT_CTL, NGBE_CFG_PORT_CTL_PFRSTD,
	    NGBE_CFG_PORT_CTL_PFRSTD);

	/* Now inform the stack we're ready */
	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->sc_nqueues; i++)
		ifq_clr_oactive(ifp->if_ifqs[i]);
	splx(s);
}

int
ngbe_ioctl(struct ifnet * ifp, u_long cmd, caddr_t data)
{
	struct ngbe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			ngbe_init(sc);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				ngbe_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ngbe_stop(sc);
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;
	case SIOCGIFRXR:
		error = ngbe_rxrinfo(sc, (struct if_rxrinfo *)ifr->ifr_data);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING) {
			ngbe_disable_intr(sc);
			ngbe_iff(sc);
			ngbe_enable_intr(sc);
		}
		error = 0;
	}

	splx(s);
	return error;
}

int
ngbe_media_change(struct ifnet *ifp)
{
	struct ngbe_softc *sc = ifp->if_softc;
	struct ngbe_hw *hw = &sc->hw;
	struct ifmedia *ifm = &sc->sc_media;
	uint32_t advertised = 0;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return EINVAL;

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
	case IFM_1000_T:
		advertised |= NGBE_LINK_SPEED_AUTONEG;
		break;
	case IFM_100_TX:
		advertised |= NGBE_LINK_SPEED_100_FULL;
		break;
	case IFM_10_T:
		advertised |= NGBE_LINK_SPEED_10_FULL;
		break;
	default:
		return EINVAL;
	}

	hw->mac.autotry_restart = true;
	hw->mac.ops.setup_link(sc, advertised, 1);

	return 0;
}

void
ngbe_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ngbe_softc *sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	ngbe_update_link_status(sc);

	if (!LINK_STATE_IS_UP(ifp->if_link_state))
		return;

	ifmr->ifm_status |= IFM_ACTIVE;

	switch (sc->link_speed) {
	case NGBE_LINK_SPEED_1GB_FULL:
		ifmr->ifm_active |= IFM_1000_T | IFM_FDX;
		break;
	case NGBE_LINK_SPEED_100_FULL:
		ifmr->ifm_active |= IFM_100_TX | IFM_FDX;
		break;
	case NGBE_LINK_SPEED_10_FULL:
		ifmr->ifm_active |= IFM_10_T | IFM_FDX;
		break;
	}

	switch (sc->hw.fc.current_mode) {
	case ngbe_fc_tx_pause:
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_TXPAUSE;
		break;
	case ngbe_fc_rx_pause:
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_RXPAUSE;
		break;
	case ngbe_fc_full:
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_RXPAUSE |
		    IFM_ETH_TXPAUSE;
		break;
	default:
		ifmr->ifm_active &= ~(IFM_FLOW | IFM_ETH_RXPAUSE |
		    IFM_ETH_TXPAUSE);
		break;
	}
}

int
ngbe_rxfill(struct rx_ring *rxr)
{
	struct ngbe_softc *sc = rxr->sc;
	int i, post = 0;
	u_int slots;

	bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map, 0, 
	    rxr->rxdma.dma_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);

	i = rxr->last_desc_filled;
	for (slots = if_rxr_get(&rxr->rx_ring, sc->num_rx_desc); slots > 0;
	    slots--) {
		if (++i == sc->num_rx_desc)
			i = 0;

		if (ngbe_get_buf(rxr, i) != 0)
			break;

		rxr->last_desc_filled = i;
		post = 1;
	}

	bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map, 0,
	    rxr->rxdma.dma_map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	if_rxr_put(&rxr->rx_ring, slots);

	return post;
}

int
ngbe_rxrinfo(struct ngbe_softc *sc, struct if_rxrinfo *ifri)
{
	struct if_rxring_info *ifr;
	struct rx_ring *rxr;
	int error, i, n = 0;

	if ((ifr = mallocarray(sc->sc_nqueues, sizeof(*ifr), M_DEVBUF,
	    M_WAITOK | M_CANFAIL | M_ZERO)) == NULL)
		return ENOMEM;

	for (i = 0; i < sc->sc_nqueues; i++) {
		rxr = &sc->rx_rings[i];
		ifr[n].ifr_size = MCLBYTES;
		snprintf(ifr[n].ifr_name, sizeof(ifr[n].ifr_name), "%d", i);
		ifr[n].ifr_info = rxr->rx_ring;
		n++;
	}

	error = if_rxr_info_ioctl(ifri, sc->sc_nqueues, ifr);
	free(ifr, M_DEVBUF, sc->sc_nqueues * sizeof(*ifr));

	return error;
}

void
ngbe_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct ngbe_softc *sc = ifp->if_softc;
	struct tx_ring *txr = ifq->ifq_softc;
	struct mbuf *m;
	unsigned int prod, free, used;
	int post = 0;

	if (!sc->link_up)
		return;

	prod = txr->next_avail_desc;
	free = txr->next_to_clean;
	if (free <= prod)
		free += sc->num_tx_desc;
	free -= prod;

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map, 0,
	    txr->txdma.dma_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);

	for (;;) {
		if (free <= NGBE_MAX_SCATTER + 2) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		used = ngbe_encap(txr, m);
		if (used == 0) {
			m_freem(m);
			continue;
		}

		free -= used;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		/* Set timeout in case hardware has problems transmitting */
		txr->watchdog_timer = NGBE_TX_TIMEOUT;
		ifp->if_timer = NGBE_TX_TIMEOUT;

		post = 1;
	}

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map, 0,
	    txr->txdma.dma_map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	if (post)
		NGBE_WRITE_REG(&sc->hw, NGBE_PX_TR_WP(txr->me),
		    txr->next_avail_desc);
}

void
ngbe_stop(struct ngbe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ngbe_hw *hw = &sc->hw;
	uint32_t rxdctl;
	int i, wait_loop = NGBE_MAX_RX_DESC_POLL;

	/* Tell the stack that the interface is no longer active. */
	ifp->if_flags &= ~IFF_RUNNING;
	ifp->if_timer = 0;

	ngbe_disable_pcie_master(sc);
	/* Disable receives */
	hw->mac.ops.disable_rx(hw);

	for (i = 0; i < sc->sc_nqueues; i++) {
		NGBE_WRITE_REG_MASK(hw, NGBE_PX_RR_CFG(i),
		    NGBE_PX_RR_CFG_RR_EN, 0);
		do {
			DELAY(10);
			rxdctl = NGBE_READ_REG(hw, NGBE_PX_RR_CFG(i));
		} while (--wait_loop && (rxdctl & NGBE_PX_RR_CFG_RR_EN));
		if (!wait_loop) {
			printf("%s: Rx queue %d not cleared within "
			    "the polling period\n", DEVNAME(sc), i);
			return;
		}
	}

	ngbe_disable_intr(sc);

	switch (hw->bus.lan_id) {
	case 0:
		NGBE_WRITE_REG_MASK(hw, NGBE_MIS_PRB_CTL,
		    NGBE_MIS_PRB_CTL_LAN0_UP, 0);
		break;
	case 1:
		NGBE_WRITE_REG_MASK(hw, NGBE_MIS_PRB_CTL,
		    NGBE_MIS_PRB_CTL_LAN1_UP, 0);
		break;
	case 2:
		NGBE_WRITE_REG_MASK(hw, NGBE_MIS_PRB_CTL,
		    NGBE_MIS_PRB_CTL_LAN2_UP, 0);
		break;
	case 3:
		NGBE_WRITE_REG_MASK(hw, NGBE_MIS_PRB_CTL,
		    NGBE_MIS_PRB_CTL_LAN3_UP, 0);
		break;
	}

	NGBE_WRITE_REG_MASK(hw, NGBE_MAC_TX_CFG, NGBE_MAC_TX_CFG_TE, 0);
	for (i = 0; i < sc->sc_nqueues; i++)
		NGBE_WRITE_REG(hw, NGBE_PX_TR_CFG(i), NGBE_PX_TR_CFG_SWFLSH);
	NGBE_WRITE_REG_MASK(hw, NGBE_TDM_CTL, NGBE_TDM_CTL_TE, 0);

	ngbe_reset(sc);

	hw->mac.ops.set_rar(sc, 0, hw->mac.addr, 0, NGBE_PSR_MAC_SWC_AD_H_AV);

	intr_barrier(sc->tag);
	for (i = 0; i < sc->sc_nqueues; i++) {
		struct ifqueue *ifq = ifp->if_ifqs[i];
		ifq_barrier(ifq);
		ifq_clr_oactive(ifq);

		if (sc->queues[i].tag != NULL)
			intr_barrier(sc->queues[i].tag);
		timeout_del(&sc->rx_rings[i].rx_refill);
	}

	ngbe_free_transmit_structures(sc);
	ngbe_free_receive_structures(sc);

	ngbe_update_link_status(sc);
}

void
ngbe_update_link_status(struct ngbe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ngbe_hw *hw = &sc->hw;
	uint32_t reg, speed = 0;
	int link_state = LINK_STATE_DOWN;

	hw->mac.ops.check_link(hw, &sc->link_speed, &sc->link_up, 0);

	ifp->if_baudrate = 0;
	if (sc->link_up) {
		link_state = LINK_STATE_FULL_DUPLEX;

		switch (sc->link_speed) {
		case NGBE_LINK_SPEED_UNKNOWN:
			ifp->if_baudrate = 0;
			break;
		case NGBE_LINK_SPEED_1GB_FULL:
			ifp->if_baudrate = IF_Gbps(1);
			speed = 2;
			break;
		case NGBE_LINK_SPEED_100_FULL:
			ifp->if_baudrate = IF_Mbps(100);
			speed = 1;
			break;
		case NGBE_LINK_SPEED_10_FULL:
			ifp->if_baudrate = IF_Mbps(10);
			break;
		}
		NGBE_WRITE_REG_MASK(hw, NGBE_CFG_LAN_SPEED, 0x3, speed);

		/* Update any flow control changes */
		hw->mac.ops.fc_enable(sc);

		ngbe_set_rx_drop_en(sc);

		if (sc->link_speed & (NGBE_LINK_SPEED_1GB_FULL |
		    NGBE_LINK_SPEED_100_FULL | NGBE_LINK_SPEED_10_FULL)) {
			NGBE_WRITE_REG(hw, NGBE_MAC_TX_CFG,
			    (NGBE_READ_REG(hw, NGBE_MAC_TX_CFG) &
			    ~NGBE_MAC_TX_CFG_SPEED_MASK) | NGBE_MAC_TX_CFG_TE |
			    NGBE_MAC_TX_CFG_SPEED_1G);
		}
		
		reg = NGBE_READ_REG(hw, NGBE_MAC_RX_CFG);
		NGBE_WRITE_REG(hw, NGBE_MAC_RX_CFG, reg);
		NGBE_WRITE_REG(hw, NGBE_MAC_PKT_FLT, NGBE_MAC_PKT_FLT_PR);
		reg = NGBE_READ_REG(hw, NGBE_MAC_WDG_TIMEOUT);
		NGBE_WRITE_REG(hw, NGBE_MAC_WDG_TIMEOUT, reg);
	}

	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

void
ngbe_watchdog(struct ifnet *ifp)
{
	struct ngbe_softc *sc = ifp->if_softc;
	struct tx_ring *txr = sc->tx_rings;
	int i, tx_hang = 0;

	/*
	 * The timer is set to 5 every time ixgbe_start() queues a packet.
	 * Anytime all descriptors are clean the timer is set to 0.
	 */
	for (i = 0; i < sc->sc_nqueues; i++, txr++) {
		if (txr->watchdog_timer == 0 || --txr->watchdog_timer)
			continue;
		else {
			tx_hang = 1;
			break;
		}
	}
	if (!tx_hang)
		return;

	printf("%s: watchdog timeout\n", DEVNAME(sc));
	ifp->if_oerrors++;

	ifp->if_flags &= ~IFF_RUNNING;
	ngbe_init(sc);
}

int
ngbe_allocate_pci_resources(struct ngbe_softc *sc)
{
	struct ngbe_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;
	pcireg_t memtype;

	memtype = PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_64BIT;
	if (pci_mapreg_map(pa, NGBE_PCIREG, memtype, 0, &os->os_memt,
	    &os->os_memh, &os->os_membase, &os->os_memsize, 0)) {
		printf(": unable to map registers\n");
		return ENXIO;
	}
	sc->hw.back = os;

	if (ngbe_setup_msix(sc))
		return EINVAL;

	return 0;
}

void
ngbe_free_pci_resources(struct ngbe_softc *sc)
{
	struct ngbe_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;

	if (sc->tag)
		pci_intr_disestablish(pa->pa_pc, sc->tag);
	sc->tag = NULL;
	if (os->os_membase)
		bus_space_unmap(os->os_memt, os->os_memh, os->os_memsize);
	os->os_membase = 0;
}

int
ngbe_allocate_msix(struct ngbe_softc *sc)
{
	struct ngbe_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;
	struct ngbe_queue *nq;
	pci_intr_handle_t ih;
	int i, error = 0;

	for (i = 0, nq = sc->queues; i < sc->sc_nqueues; i++, nq++) {
		if (pci_intr_map_msix(pa, i, &ih)) {
			printf(": unable to map msi-x vector %d", i);
			error = ENXIO;
			goto fail;
		}

		nq->tag = pci_intr_establish_cpu(pa->pa_pc, ih,
		    IPL_NET | IPL_MPSAFE, intrmap_cpu(sc->sc_intrmap, i),
		    ngbe_intr_queue, nq, nq->name);
		if (nq->tag == NULL) {
			printf(": unable to establish interrupt %d\n", i);
			error = ENXIO;
			goto fail;
		}

		nq->msix = i;
	}

	/* Now the link status/control last MSI-X vector */
	if (pci_intr_map_msix(pa, i, &ih)) {
		printf(": unable to map link vector\n");
		error = ENXIO;
		goto fail;
	}

	sc->tag = pci_intr_establish(pa->pa_pc, ih, IPL_NET | IPL_MPSAFE,
		ngbe_intr_link, sc, sc->sc_dev.dv_xname);
	if (sc->tag == NULL) {
		printf(": unable to establish link interrupt\n");
		error = ENXIO;
		goto fail;
	}

	sc->linkvec = i;
	printf(", %s, %d queue%s", pci_intr_string(pa->pa_pc, ih), i,
	    (i > 1) ? "s" : "");

	return 0;
fail:
	for (nq = sc->queues; i > 0; i--, nq++) {
		if (nq->tag == NULL)
			continue;
		pci_intr_disestablish(pa->pa_pc, nq->tag);
		nq->tag = NULL;
	}

	return error;
}

void
ngbe_setup_interface(struct ngbe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i;

	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = ngbe_ioctl;
	ifp->if_qstart = ngbe_start;
	ifp->if_watchdog = ngbe_watchdog;
	ifp->if_hardmtu = NGBE_MAX_JUMBO_FRAME_SIZE - ETHER_HDR_LEN - 
	    ETHER_CRC_LEN;
	ifq_init_maxlen(&ifp->if_snd, sc->num_tx_desc - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	/* Initialize ifmedia structures. */
	ifmedia_init(&sc->sc_media, IFM_IMASK, ngbe_media_change,
	    ngbe_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_10_T | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_100_TX | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);

	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);
	sc->sc_media.ifm_media = sc->sc_media.ifm_cur->ifm_media;

	if_attach(ifp);
	ether_ifattach(ifp);

	if_attach_queues(ifp, sc->sc_nqueues);
	if_attach_iqueues(ifp, sc->sc_nqueues);
	for (i = 0; i < sc->sc_nqueues; i++) {
		struct ifqueue *ifq = ifp->if_ifqs[i];
		struct ifiqueue *ifiq = ifp->if_iqs[i];
		struct tx_ring *txr = &sc->tx_rings[i];
		struct rx_ring *rxr = &sc->rx_rings[i];

		ifq->ifq_softc = txr;
		txr->ifq = ifq;

		ifiq->ifiq_softc = rxr;
		rxr->ifiq = ifiq;
	}
}

int
ngbe_setup_msix(struct ngbe_softc *sc)
{
	struct ngbe_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;
	int nmsix;

	nmsix = pci_intr_msix_count(pa);
	if (nmsix <= 1) {
		printf(": not enough msi-x vectors\n");
		return EINVAL;
	}

	/* Give one vector to events. */
	nmsix--;

	sc->sc_intrmap = intrmap_create(&sc->sc_dev, nmsix, NGBE_MAX_VECTORS,
	    INTRMAP_POWEROF2);
	sc->sc_nqueues = intrmap_count(sc->sc_intrmap);

	return 0;
}

int
ngbe_dma_malloc(struct ngbe_softc *sc, bus_size_t size,
    struct ngbe_dma_alloc *dma)
{
	struct ngbe_osdep *os = &sc->osdep;

	dma->dma_tag = os->os_pa.pa_dmat;

	if (bus_dmamap_create(dma->dma_tag, size, 1, size, 0, BUS_DMA_NOWAIT,
	    &dma->dma_map))
		return 1;
	if (bus_dmamem_alloc(dma->dma_tag, size, PAGE_SIZE, 0, &dma->dma_seg,
	    1, &dma->dma_nseg, BUS_DMA_NOWAIT))
		goto destroy;
	if (bus_dmamem_map(dma->dma_tag, &dma->dma_seg, dma->dma_nseg, size,
	    &dma->dma_vaddr, BUS_DMA_NOWAIT | BUS_DMA_COHERENT))
		goto free;
	if (bus_dmamap_load(dma->dma_tag, dma->dma_map, dma->dma_vaddr, size,
	    NULL, BUS_DMA_NOWAIT))
		goto unmap;

	dma->dma_size = size;

	return 0;
unmap:
	bus_dmamem_unmap(dma->dma_tag, dma->dma_vaddr, size);
free:
	bus_dmamem_free(dma->dma_tag, &dma->dma_seg, dma->dma_nseg);
destroy:
	bus_dmamap_destroy(dma->dma_tag, dma->dma_map);
	dma->dma_map = NULL;
	dma->dma_tag = NULL;
	return 1;
}

void
ngbe_dma_free(struct ngbe_softc *sc, struct ngbe_dma_alloc *dma)
{
	if (dma->dma_tag == NULL)
		return;

	if (dma->dma_map != NULL) {
		bus_dmamap_sync(dma->dma_tag, dma->dma_map, 0,
		    dma->dma_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dma->dma_tag, dma->dma_map);
		bus_dmamem_unmap(dma->dma_tag, dma->dma_vaddr, dma->dma_size);
		bus_dmamem_free(dma->dma_tag, &dma->dma_seg, dma->dma_nseg);
		bus_dmamap_destroy(dma->dma_tag, dma->dma_map);
		dma->dma_map = NULL;
	}
}

int
ngbe_allocate_isb(struct ngbe_softc *sc)
{
	int isize;

	isize = sizeof(uint32_t) * NGBE_ISB_MAX;
	if (ngbe_dma_malloc(sc, isize, &sc->isbdma)) {
		printf("%s: unable to allocate interrupt status resources\n",
		    DEVNAME(sc));
		return ENOMEM;
	}
	sc->isb_base = (uint32_t *)sc->isbdma.dma_vaddr;
	bzero((void *)sc->isb_base, isize);

	return 0;
}

void
ngbe_free_isb(struct ngbe_softc *sc)
{
	ngbe_dma_free(sc, &sc->isbdma);
}

int
ngbe_allocate_queues(struct ngbe_softc *sc)
{
	struct ngbe_queue *nq;
	struct tx_ring *txr;
	struct rx_ring *rxr;
	int i, rsize, rxconf, tsize, txconf;

	/* Allocate the top level queue structs. */
	sc->queues = mallocarray(sc->sc_nqueues, sizeof(struct ngbe_queue),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->queues == NULL) {
		printf("%s: unable to allocate queue\n", DEVNAME(sc));
		goto fail;
	}

	/* Allocate the Tx ring. */
	sc->tx_rings = mallocarray(sc->sc_nqueues, sizeof(struct tx_ring),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->tx_rings == NULL) {
		printf("%s: unable to allocate Tx ring\n", DEVNAME(sc));
		goto fail;
	}
	
	/* Allocate the Rx ring. */
	sc->rx_rings = mallocarray(sc->sc_nqueues, sizeof(struct rx_ring),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->rx_rings == NULL) {
		printf("%s: unable to allocate Rx ring\n", DEVNAME(sc));
		goto rx_fail;
	}

	txconf = rxconf = 0;

	/* Set up the Tx queues. */
	tsize = roundup2(sc->num_tx_desc * sizeof(union ngbe_tx_desc),
	    PAGE_SIZE);
	for (i = 0; i < sc->sc_nqueues; i++, txconf++) {
		txr = &sc->tx_rings[i];
		txr->sc = sc;
		txr->me = i;

		if (ngbe_dma_malloc(sc, tsize, &txr->txdma)) {
			printf("%s: unable to allocate Tx descriptor\n",
			    DEVNAME(sc));
			goto err_tx_desc;
		}
		txr->tx_base = (union ngbe_tx_desc *)txr->txdma.dma_vaddr;
		bzero((void *)txr->tx_base, tsize);
	}

	/* Set up the Rx queues. */
	rsize = roundup2(sc->num_rx_desc * sizeof(union ngbe_rx_desc),
	    PAGE_SIZE);
	for (i = 0; i < sc->sc_nqueues; i++, rxconf++) {
		rxr = &sc->rx_rings[i];
		rxr->sc = sc;
		rxr->me = i;
		timeout_set(&rxr->rx_refill, ngbe_rxrefill, rxr);

		if (ngbe_dma_malloc(sc, rsize, &rxr->rxdma)) {
			printf("%s: unable to allocate Rx descriptor\n",
			    DEVNAME(sc));
			goto err_rx_desc;
		}
		rxr->rx_base = (union ngbe_rx_desc *)rxr->rxdma.dma_vaddr;
		bzero((void *)rxr->rx_base, rsize);
	}

	/* Set up the queue holding structs. */
	for (i = 0; i < sc->sc_nqueues; i++) {
		nq = &sc->queues[i];
		nq->sc = sc;
		nq->txr = &sc->tx_rings[i];
		nq->rxr = &sc->rx_rings[i];
		snprintf(nq->name, sizeof(nq->name), "%s:%d", DEVNAME(sc), i);
	}

	return 0;

err_rx_desc:
	for (rxr = sc->rx_rings; rxconf > 0; rxr++, rxconf--)
		ngbe_dma_free(sc, &rxr->rxdma);
err_tx_desc:
	for (txr = sc->tx_rings; txconf > 0; txr++, txconf--)
		ngbe_dma_free(sc, &txr->txdma);
	free(sc->rx_rings, M_DEVBUF, sc->sc_nqueues * sizeof(struct rx_ring));
	sc->rx_rings = NULL;
rx_fail:
	free(sc->tx_rings, M_DEVBUF, sc->sc_nqueues * sizeof(struct tx_ring));
	sc->tx_rings = NULL;
fail:
	return ENOMEM;
}

void
ngbe_free_receive_structures(struct ngbe_softc *sc)
{
	struct rx_ring *rxr;
	int i;

	for (i = 0, rxr = sc->rx_rings; i < sc->sc_nqueues; i++, rxr++)
		if_rxr_init(&rxr->rx_ring, 0, 0);

	for (i = 0, rxr = sc->rx_rings; i < sc->sc_nqueues; i++, rxr++)
		ngbe_free_receive_buffers(rxr);
}

void
ngbe_free_receive_buffers(struct rx_ring *rxr)
{
	struct ngbe_softc *sc;
	struct ngbe_rx_buf *rxbuf;
	int i;

	sc = rxr->sc;
	if (rxr->rx_buffers != NULL) {
		for (i = 0; i < sc->num_rx_desc; i++) {
			rxbuf = &rxr->rx_buffers[i];
			if (rxbuf->buf != NULL) {
				bus_dmamap_sync(rxr->rxdma.dma_tag, rxbuf->map,
				    0, rxbuf->map->dm_mapsize,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(rxr->rxdma.dma_tag,
				    rxbuf->map);
				m_freem(rxbuf->buf);
				rxbuf->buf = NULL;
			}
			bus_dmamap_destroy(rxr->rxdma.dma_tag, rxbuf->map);
			rxbuf->map = NULL;
		}
		free(rxr->rx_buffers, M_DEVBUF,
		    sc->num_rx_desc * sizeof(struct ngbe_rx_buf));
		rxr->rx_buffers = NULL;
	}
}

void
ngbe_free_transmit_structures(struct ngbe_softc *sc)
{
	struct tx_ring *txr = sc->tx_rings;
	int i;

	for (i = 0; i < sc->sc_nqueues; i++, txr++)
		ngbe_free_transmit_buffers(txr);
}

void
ngbe_free_transmit_buffers(struct tx_ring *txr)
{
	struct ngbe_softc *sc = txr->sc;
	struct ngbe_tx_buf *tx_buffer;
	int i;

	if (txr->tx_buffers == NULL)
		return;

	tx_buffer = txr->tx_buffers;
	for (i = 0; i < sc->num_tx_desc; i++, tx_buffer++) {
		if (tx_buffer->map != NULL && tx_buffer->map->dm_nsegs > 0) {
			bus_dmamap_sync(txr->txdma.dma_tag, tx_buffer->map,
			    0, tx_buffer->map->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(txr->txdma.dma_tag, tx_buffer->map);
		}
		if (tx_buffer->m_head != NULL) {
			m_freem(tx_buffer->m_head);
			tx_buffer->m_head = NULL;
		}
		if (tx_buffer->map != NULL) {
			bus_dmamap_destroy(txr->txdma.dma_tag, tx_buffer->map);
			tx_buffer->map = NULL;
		}
	}

	if (txr->tx_buffers != NULL)
		free(txr->tx_buffers, M_DEVBUF,
		    sc->num_tx_desc * sizeof(struct ngbe_tx_buf));
	txr->tx_buffers = NULL;
	txr->txtag = NULL;
}

int
ngbe_allocate_receive_buffers(struct rx_ring *rxr)
{
	struct ngbe_softc *sc = rxr->sc;
	struct ngbe_rx_buf *rxbuf;
	int i, error;

	rxr->rx_buffers = mallocarray(sc->num_rx_desc,
	    sizeof(struct ngbe_rx_buf), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (rxr->rx_buffers == NULL) {
		printf("%s: unable to allocate rx_buffer memory\n",
		    DEVNAME(sc));
		error = ENOMEM;
		goto fail;
	}

	rxbuf = rxr->rx_buffers;
	for (i = 0; i < sc->num_rx_desc; i++, rxbuf++) {
		error = bus_dmamap_create(rxr->rxdma.dma_tag,
		    NGBE_MAX_JUMBO_FRAME_SIZE, 1, NGBE_MAX_JUMBO_FRAME_SIZE, 0,
		    BUS_DMA_NOWAIT, &rxbuf->map);
		if (error) {
			printf("%s: unable to create RX DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}
	}
	bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map, 0,
	    rxr->rxdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return 0;
fail:
	return error;
}

int
ngbe_allocate_transmit_buffers(struct tx_ring *txr)
{
	struct ngbe_softc *sc = txr->sc;
	struct ngbe_tx_buf *txbuf;
	int error, i;

	txr->tx_buffers = mallocarray(sc->num_tx_desc,
	    sizeof(struct ngbe_tx_buf), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (txr->tx_buffers == NULL) {
		printf("%s: unable to allocate tx_buffer memory\n",
		    DEVNAME(sc));
		error = ENOMEM;
		goto fail;
	}
	txr->txtag = txr->txdma.dma_tag;

	/* Create the descriptor buffer dma maps. */
	for (i = 0; i < sc->num_tx_desc; i++) {
		txbuf = &txr->tx_buffers[i];
		error = bus_dmamap_create(txr->txdma.dma_tag, NGBE_TSO_SIZE,
		    NGBE_MAX_SCATTER, PAGE_SIZE, 0, BUS_DMA_NOWAIT,
		    &txbuf->map);
		if (error != 0) {
			printf("%s: unable to create TX DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}
	}

	return 0;
fail:
	return error;
}

int
ngbe_setup_receive_ring(struct rx_ring *rxr)
{
	struct ngbe_softc *sc = rxr->sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int rsize;

	rsize = roundup2(sc->num_rx_desc * sizeof(union ngbe_rx_desc),
	    PAGE_SIZE);

	/* Clear the ring contents. */
	bzero((void *)rxr->rx_base, rsize);

	if (ngbe_allocate_receive_buffers(rxr))
		return ENOMEM;

	/* Setup our descriptor indices. */
	rxr->next_to_check = 0;
	rxr->last_desc_filled = sc->num_rx_desc - 1;

	if_rxr_init(&rxr->rx_ring, 2 * ((ifp->if_hardmtu / MCLBYTES) + 1),
	    sc->num_rx_desc - 1);

	ngbe_rxfill(rxr);
	if (if_rxr_inuse(&rxr->rx_ring) == 0) {
		printf("%s: unable to fill any rx descriptors\n", DEVNAME(sc));
		return ENOBUFS;
	}

	return 0;
}

int
ngbe_setup_transmit_ring(struct tx_ring *txr)
{
	struct ngbe_softc *sc = txr->sc;

	/* Now allocate transmit buffers for the ring. */
	if (ngbe_allocate_transmit_buffers(txr))
		return ENOMEM;

	/* Clear the old ring contents */
	bzero((void *)txr->tx_base,
	    (sizeof(union ngbe_tx_desc)) * sc->num_tx_desc);

	/* Reset indices. */
	txr->next_avail_desc = 0;
	txr->next_to_clean = 0;

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map, 0,
	    txr->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return 0;
}

int
ngbe_setup_receive_structures(struct ngbe_softc *sc)
{
	struct rx_ring *rxr = sc->rx_rings;
	int i;

	for (i = 0; i < sc->sc_nqueues; i++, rxr++) {
		if (ngbe_setup_receive_ring(rxr))
			goto fail;
	}

	return 0;
fail:
	ngbe_free_receive_structures(sc);
	return ENOBUFS;
}

int
ngbe_setup_transmit_structures(struct ngbe_softc *sc)
{
	struct tx_ring *txr = sc->tx_rings;
	int i;

	for (i = 0; i < sc->sc_nqueues; i++, txr++) {
		if (ngbe_setup_transmit_ring(txr))
			goto fail;
	}

	return 0;
fail:
	ngbe_free_transmit_structures(sc);
	return ENOBUFS;
}

uint8_t *
ngbe_addr_list_itr(struct ngbe_hw *hw, uint8_t **mc_addr_ptr, uint32_t *vmdq)
{
	uint8_t *addr = *mc_addr_ptr;
	uint8_t *newptr;
	*vmdq = 0;

	newptr = addr + ETHER_ADDR_LEN;
	*mc_addr_ptr = newptr;
	return addr;
}

void 
ngbe_iff(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct arpcom *ac = &sc->sc_ac;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t fctrl, vlanctrl;
	uint8_t *mta, *update_ptr;
	int mcnt = 0;

	mta = sc->mta;
	bzero(mta, sizeof(uint8_t) * ETHER_ADDR_LEN * NGBE_SP_RAR_ENTRIES);

	fctrl = NGBE_READ_REG_MASK(hw, NGBE_PSR_CTL,
	    ~(NGBE_PSR_CTL_UPE | NGBE_PSR_CTL_MPE));
	vlanctrl = NGBE_READ_REG_MASK(hw, NGBE_PSR_VLAN_CTL,
	    ~(NGBE_PSR_VLAN_CTL_VFE | NGBE_PSR_VLAN_CTL_CFIEN));
	ifp->if_flags &= ~IFF_ALLMULTI;

	/* Set all bits that we expect to always be set */
	fctrl |= NGBE_PSR_CTL_BAM | NGBE_PSR_CTL_MFE;
	vlanctrl |= NGBE_PSR_VLAN_CTL_VFE;

	hw->addr_ctrl.user_set_promisc = 0;
	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt > NGBE_SP_RAR_ENTRIES) {
		ifp->if_flags |= IFF_ALLMULTI;
		fctrl |= NGBE_PSR_CTL_MPE;
		if (ifp->if_flags & IFF_PROMISC) {
			fctrl |= NGBE_PSR_CTL_UPE;
			vlanctrl &= ~NGBE_PSR_VLAN_CTL_VFE;
		}
	} else {
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			bcopy(enm->enm_addrlo, &mta[mcnt * ETHER_ADDR_LEN],
			    ETHER_ADDR_LEN);
			mcnt++;

			ETHER_NEXT_MULTI(step, enm);
		}

		update_ptr = mta;
		hw->mac.ops.update_mc_addr_list(hw, update_ptr, mcnt,
		    ngbe_addr_list_itr, 1);
	}

	NGBE_WRITE_REG(hw, NGBE_PSR_VLAN_CTL, vlanctrl);
	NGBE_WRITE_REG(hw, NGBE_PSR_CTL, fctrl);
}

int
ngbe_initialize_receive_unit(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	struct rx_ring *rxr = sc->rx_rings;
	uint32_t bufsz, mhadd, rxctrl, rxdctl, srrctl;
	int i, wait_loop = NGBE_MAX_RX_DESC_POLL;
	int error = 0;

	/* Disable receives while setting up the descriptors */
	hw->mac.ops.disable_rx(hw);

	ngbe_setup_psrtype(hw);

	/* Enable hw crc stripping */
	NGBE_WRITE_REG_MASK(hw, NGBE_RSEC_CTL, NGBE_RSEC_CTL_CRC_STRIP,
	    NGBE_RSEC_CTL_CRC_STRIP);

	if (sc->sc_nqueues > 1) {
		NGBE_WRITE_REG_MASK(hw, NGBE_PSR_CTL, NGBE_PSR_CTL_PCSD,
		    NGBE_PSR_CTL_PCSD);
		ngbe_initialize_rss_mapping(sc);
	}

	mhadd = NGBE_READ_REG(hw, NGBE_PSR_MAX_SZ);
	if (mhadd != NGBE_MAX_JUMBO_FRAME_SIZE)
		NGBE_WRITE_REG(hw, NGBE_PSR_MAX_SZ, NGBE_MAX_JUMBO_FRAME_SIZE);
	
	bufsz = MCLBYTES >> NGBE_PX_RR_CFG_BSIZEPKT_SHIFT;

	for (i = 0; i < sc->sc_nqueues; i++, rxr++) {
		uint64_t rdba = rxr->rxdma.dma_map->dm_segs[0].ds_addr;

		/* Disable queue to avoid issues while updating state */
		NGBE_WRITE_REG_MASK(hw, NGBE_PX_RR_CFG(i),
		    NGBE_PX_RR_CFG_RR_EN, 0);

		/* Hardware may take up to 100us to actually disable Rx queue */
		do {
			DELAY(10);
			rxdctl = NGBE_READ_REG(hw, NGBE_PX_RR_CFG(i));
		} while (--wait_loop && (rxdctl & NGBE_PX_RR_CFG_RR_EN));
		if (!wait_loop) {
			printf("%s: Rx queue %d not cleared within "
			    "the polling period\n", DEVNAME(sc), i);
			error = ETIMEDOUT;
			goto out;
		}

		NGBE_WRITE_REG(hw, NGBE_PX_RR_BAL(i),
		    (rdba & 0x00000000ffffffffULL));
		NGBE_WRITE_REG(hw, NGBE_PX_RR_BAH(i), (rdba >> 32));

		rxdctl = NGBE_READ_REG(hw, NGBE_PX_RR_CFG(i));
		rxdctl |=
		    (sc->num_rx_desc / 128) << NGBE_PX_RR_CFG_RR_SIZE_SHIFT;
		rxdctl |= 0x1 << NGBE_PX_RR_CFG_RR_THER_SHIFT;
		NGBE_WRITE_REG(hw, NGBE_PX_RR_CFG(i), rxdctl);

		/* Reset head and tail pointers */
		NGBE_WRITE_REG(hw, NGBE_PX_RR_RP(i), 0);
		NGBE_WRITE_REG(hw, NGBE_PX_RR_WP(i), 0);

		/* Set up the SRRCTL register */
		srrctl = NGBE_READ_REG_MASK(hw, NGBE_PX_RR_CFG(i),
		    ~(NGBE_PX_RR_CFG_RR_HDR_SZ | NGBE_PX_RR_CFG_RR_BUF_SZ |
		    NGBE_PX_RR_CFG_SPLIT_MODE));
		srrctl |= bufsz;
		NGBE_WRITE_REG(hw, NGBE_PX_RR_CFG(i), srrctl);

		/* Enable receive descriptor ring */
		NGBE_WRITE_REG_MASK(hw, NGBE_PX_RR_CFG(i),
		    NGBE_PX_RR_CFG_RR_EN, NGBE_PX_RR_CFG_RR_EN);

		do {
			msec_delay(1);
			rxdctl = NGBE_READ_REG(hw, NGBE_PX_RR_CFG(i));
		} while (--wait_loop && !(rxdctl & NGBE_PX_RR_CFG_RR_EN));
		if (!wait_loop) {
			printf("%s: Rx queue %d not set within "
			    "the polling period\n", DEVNAME(sc), i);
			error = ETIMEDOUT;
			goto out;
		}
		NGBE_WRITE_REG(hw, NGBE_PX_RR_WP(i), rxr->last_desc_filled);
	}

	/* Enable all receives */
	rxctrl = NGBE_READ_REG(hw, NGBE_RDB_PB_CTL);
	rxctrl |= NGBE_RDB_PB_CTL_PBEN;
	hw->mac.ops.enable_rx_dma(hw, rxctrl);
out:
	return error;
}

void
ngbe_initialize_rss_mapping(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t reta = 0, rss_field, rss_key[10];
	int i, j, queue_id;

	/* Set up the redirection table */
	for (i = 0, j = 0; i < 128; i++, j++) {
		if (j == sc->sc_nqueues)
			j = 0;
		queue_id = j;
		/*
		 * The low 8 bits are for hash value (n+0);
		 * The next 8 bits are for hash value (n+1), etc.
		 */
		reta = reta >> 8;
		reta = reta | (((uint32_t)queue_id) << 24);
		if ((i & 3) == 3) {
			NGBE_WRITE_REG(hw, NGBE_RDB_RSSTBL(i >> 2), reta);
			reta = 0;
		}
	}

	/* Set up random bits */
	stoeplitz_to_key(&rss_key, sizeof(rss_key));

	/* Fill out hash function seeds */
	for (i = 0; i < 10; i++)
		NGBE_WRITE_REG(hw, NGBE_RDB_RSSRK(i), rss_key[i]);

	/* Perform hash on these packet types */
	rss_field = NGBE_RDB_RA_CTL_RSS_EN | NGBE_RDB_RA_CTL_RSS_IPV4 |
	    NGBE_RDB_RA_CTL_RSS_IPV4_TCP | NGBE_RDB_RA_CTL_RSS_IPV6 |
	    NGBE_RDB_RA_CTL_RSS_IPV6_TCP;

	NGBE_WRITE_REG(hw, NGBE_RDB_RA_CTL, rss_field);
}

int
ngbe_initialize_transmit_unit(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct tx_ring *txr;
	uint64_t tdba;
	uint32_t txdctl;
	int i, wait_loop = NGBE_MAX_RX_DESC_POLL;
	int error = 0;

	/* TDM_CTL.TE must be before Tx queues are enabled */
	NGBE_WRITE_REG_MASK(hw, NGBE_TDM_CTL, NGBE_TDM_CTL_TE,
	    NGBE_TDM_CTL_TE);

	/* Setup the base and length of the Tx descriptor ring. */
	for (i = 0; i < sc->sc_nqueues; i++) {
		txr = &sc->tx_rings[i];
		tdba = txr->txdma.dma_map->dm_segs[0].ds_addr;

		/* Disable queue to avoid issues while updating state */
		NGBE_WRITE_REG(hw, NGBE_PX_TR_CFG(i), NGBE_PX_TR_CFG_SWFLSH);
		NGBE_WRITE_FLUSH(hw);

		NGBE_WRITE_REG(hw, NGBE_PX_TR_BAL(i),
		    (tdba & 0x00000000ffffffffULL));
		NGBE_WRITE_REG(hw, NGBE_PX_TR_BAH(i), (tdba >> 32));

		/* Reset head and tail pointers */
		NGBE_WRITE_REG(hw, NGBE_PX_TR_RP(i), 0);
		NGBE_WRITE_REG(hw, NGBE_PX_TR_WP(i), 0);

		txr->watchdog_timer = 0;

		txdctl = NGBE_PX_TR_CFG_ENABLE;
		txdctl |= 4 << NGBE_PX_TR_CFG_TR_SIZE_SHIFT;
		txdctl |= 0x20 << NGBE_PX_TR_CFG_WTHRESH_SHIFT;

		/* Enable queue */
		NGBE_WRITE_REG(hw, NGBE_PX_TR_CFG(i), txdctl);

		/* Poll to verify queue is enabled */
		do {
			msec_delay(1);
			txdctl = NGBE_READ_REG(hw, NGBE_PX_TR_CFG(i));
		} while (--wait_loop && !(txdctl & NGBE_PX_TR_CFG_ENABLE));
		if (!wait_loop) {
			printf("%s: Tx queue %d not set within "
			    "the polling period\n", DEVNAME(sc), i);
			error = ETIMEDOUT;
			goto out;
		}
	}

	ifp->if_timer = 0;

	NGBE_WRITE_REG_MASK(hw, NGBE_TSEC_BUF_AE, 0x3ff, 0x10);
	NGBE_WRITE_REG_MASK(hw, NGBE_TSEC_CTL, 0x2, 0);
	NGBE_WRITE_REG_MASK(hw, NGBE_TSEC_CTL, 0x1, 1);

	/* Enable mac transmitter */
	NGBE_WRITE_REG_MASK(hw, NGBE_MAC_TX_CFG, NGBE_MAC_TX_CFG_TE,
	    NGBE_MAC_TX_CFG_TE);
out:
	return error;
}

int
ngbe_intr_link(void *arg)
{
	struct ngbe_softc *sc = (struct ngbe_softc *)arg;
	uint32_t eicr;

	eicr = ngbe_misc_isb(sc, NGBE_ISB_MISC);
	if (eicr & (NGBE_PX_MISC_IC_PHY | NGBE_PX_MISC_IC_GPIO)) {
		KERNEL_LOCK();
		ngbe_handle_phy_event(sc);
		ngbe_update_link_status(sc);
		KERNEL_UNLOCK();
	}
	ngbe_enable_queue(sc, sc->linkvec);
	return 1;
}

int
ngbe_intr_queue(void *arg)
{
	struct ngbe_queue *nq = arg;
	struct ngbe_softc *sc = nq->sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct rx_ring *rxr = nq->rxr;
	struct tx_ring *txr = nq->txr;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		ngbe_rxeof(rxr);
		ngbe_txeof(txr);
		ngbe_rxrefill(rxr);
	}

	ngbe_enable_queue(sc, nq->msix);

	return 1;
}

void
ngbe_init_eeprom_params(struct ngbe_hw *hw)
{
	struct ngbe_eeprom_info *eeprom = &hw->eeprom;

	if (eeprom->type == ngbe_eeprom_uninitialized) {
		eeprom->type = ngbe_eeprom_none;

		if (!(NGBE_READ_REG(hw, NGBE_SPI_STATUS) &
		    NGBE_SPI_STATUS_FLASH_BYPASS))
			eeprom->type = ngbe_flash;
	}

	eeprom->sw_region_offset = 0x80;
}

int
ngbe_init_hw(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	int status;

	/* Reset the hardware */
	status = hw->mac.ops.reset_hw(sc);
	
	if (!status)
		status = hw->mac.ops.start_hw(sc);

	return status;
}

void
ngbe_init_ops(struct ngbe_hw *hw)
{
	struct ngbe_mac_info *mac = &hw->mac;
	struct ngbe_phy_info *phy = &hw->phy;
	struct ngbe_eeprom_info *eeprom = &hw->eeprom;

	phy->ops.reset = ngbe_phy_reset;
	phy->ops.read_reg = ngbe_phy_read_reg;
	phy->ops.write_reg = ngbe_phy_write_reg;
	phy->ops.setup_link = ngbe_phy_setup_link;
	phy->ops.phy_led_ctrl = ngbe_phy_led_ctrl;
	phy->ops.check_overtemp = ngbe_phy_check_overtemp;
	phy->ops.identify = ngbe_phy_identify;
	phy->ops.init = ngbe_phy_init;
	phy->ops.check_event = ngbe_phy_check_event;
	phy->ops.get_adv_pause = ngbe_phy_get_advertised_pause;
	phy->ops.get_lp_adv_pause = ngbe_phy_get_lp_advertised_pause;
	phy->ops.set_adv_pause = ngbe_phy_set_pause_advertisement;
	phy->ops.setup_once = ngbe_phy_setup;

	/* MAC */
	mac->ops.init_hw = ngbe_init_hw;
	mac->ops.clear_hw_cntrs = ngbe_clear_hw_cntrs;
	mac->ops.get_mac_addr = ngbe_get_mac_addr;
	mac->ops.stop_adapter = ngbe_stop_adapter;
	mac->ops.get_bus_info = ngbe_get_bus_info;
	mac->ops.set_lan_id = ngbe_set_lan_id_multi_port_pcie;
	mac->ops.acquire_swfw_sync = ngbe_acquire_swfw_sync;
	mac->ops.release_swfw_sync = ngbe_release_swfw_sync;
	mac->ops.reset_hw = ngbe_reset_hw;
	mac->ops.get_media_type = ngbe_get_media_type;
	mac->ops.disable_sec_rx_path = ngbe_disable_sec_rx_path;
	mac->ops.enable_sec_rx_path = ngbe_enable_sec_rx_path;
	mac->ops.enable_rx_dma = ngbe_enable_rx_dma;
	mac->ops.start_hw = ngbe_start_hw;

	/* RAR, Multicast, VLAN */
	mac->ops.set_rar = ngbe_set_rar;
	mac->ops.init_rx_addrs = ngbe_init_rx_addrs;
	mac->ops.update_mc_addr_list = ngbe_update_mc_addr_list;
	mac->ops.enable_rx = ngbe_enable_rx;
	mac->ops.disable_rx = ngbe_disable_rx;
	mac->ops.clear_vfta = ngbe_clear_vfta;
	mac->ops.init_uta_tables = ngbe_init_uta_tables;

	/* Flow Control */
	mac->ops.fc_enable = ngbe_fc_enable;
	mac->ops.setup_fc = ngbe_setup_fc;

	/* Link */
	mac->ops.check_link = ngbe_check_mac_link;
	mac->ops.setup_rxpba = ngbe_set_rxpba;

	mac->mcft_size = NGBE_SP_MC_TBL_SIZE;
	mac->vft_size = NGBE_SP_VFT_TBL_SIZE;
	mac->num_rar_entries = NGBE_SP_RAR_ENTRIES;
	mac->rx_pb_size = NGBE_SP_RX_PB_SIZE;
	mac->max_rx_queues = NGBE_SP_MAX_RX_QUEUES;
	mac->max_tx_queues = NGBE_SP_MAX_TX_QUEUES;

	/* EEPROM */
	eeprom->ops.init_params = ngbe_init_eeprom_params;
	eeprom->ops.eeprom_chksum_cap_st = ngbe_eepromcheck_cap;
	eeprom->ops.phy_led_oem_chk = ngbe_phy_led_oem_chk;

	/* Manageability interface */
	mac->ops.set_fw_drv_ver = ngbe_set_fw_drv_ver;
	mac->ops.init_thermal_sensor_thresh = ngbe_init_thermal_sensor_thresh;
}

void
ngbe_init_rx_addrs(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t rar_entries = hw->mac.num_rar_entries;
	uint32_t i, psrctl;

	/*
	 * If the current mac address is valid, assume it is a software
	 * override to the permanent address.
	 * Otherwise, use the permanent address from the eeprom.
	 */
	if (ngbe_validate_mac_addr(hw->mac.addr)) {
		/* Get the MAC address from the RAR0 for later reference */
		hw->mac.ops.get_mac_addr(hw, hw->mac.addr);
	}
	hw->addr_ctrl.overflow_promisc = 0;
	hw->addr_ctrl.rar_used_count = 1;

	/* Zero out the other receive addresses. */
	for (i = 1; i < rar_entries; i++) {
		NGBE_WRITE_REG(hw, NGBE_PSR_MAC_SWC_IDX, i);
		NGBE_WRITE_REG(hw, NGBE_PSR_MAC_SWC_AD_L, 0);
		NGBE_WRITE_REG(hw, NGBE_PSR_MAC_SWC_AD_H, 0);
	}

	/* Clear the MTA */
	hw->addr_ctrl.mta_in_use = 0;
	psrctl = NGBE_READ_REG(hw, NGBE_PSR_CTL);
	psrctl &= ~(NGBE_PSR_CTL_MO | NGBE_PSR_CTL_MFE);
	psrctl |= hw->mac.mc_filter_type << NGBE_PSR_CTL_MO_SHIFT;
	NGBE_WRITE_REG(hw, NGBE_PSR_CTL, psrctl);

	for (i = 0; i < hw->mac.mcft_size; i++)
		NGBE_WRITE_REG(hw, NGBE_PSR_MC_TBL(i), 0);

	hw->mac.ops.init_uta_tables(hw);
}

void
ngbe_init_shared_code(struct ngbe_softc *sc)
{
	struct ngbe_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;
	struct ngbe_hw *hw = &sc->hw;

	hw->subsystem_device_id = PCI_PRODUCT(pci_conf_read(pa->pa_pc,
	    pa->pa_tag, PCI_SUBSYS_ID_REG));  

	hw->phy.type = ngbe_phy_internal;

	NGBE_WRITE_REG(hw, NGBE_MDIO_CLAUSE_SELECT, 0xf);

	ngbe_init_ops(hw);

	/* Default flow control settings. */
	hw->fc.requested_mode = ngbe_fc_full;
	hw->fc.current_mode = ngbe_fc_full;

	hw->fc.pause_time = NGBE_DEFAULT_FCPAUSE;
	hw->fc.disable_fc_autoneg = 0;
}

void
ngbe_init_thermal_sensor_thresh(struct ngbe_hw *hw)
{
	/* Only support thermal sensors attached to SP physical port 0 */
	if (hw->bus.lan_id)
		return;

	NGBE_WRITE_REG(hw, NGBE_TS_INT_EN, NGBE_TS_INT_EN_DALARM_INT_EN |
	    NGBE_TS_INT_EN_ALARM_INT_EN);
	NGBE_WRITE_REG(hw, NGBE_TS_EN, NGBE_TS_EN_ENA);

	NGBE_WRITE_REG(hw, NGBE_TS_ALARM_THRE, 0x344);
	NGBE_WRITE_REG(hw, NGBE_TS_DALARM_THRE, 0x330);
}

void
ngbe_init_uta_tables(struct ngbe_hw *hw)
{
	int i;

	for (i = 0; i < 128; i++)
		NGBE_WRITE_REG(hw, NGBE_PSR_UC_TBL(i), 0);
}

void
ngbe_fc_autoneg(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t speed;
	int link_up;
	int error = EINVAL;

	/*
	 * AN should have completed when the cable was plugged in.
	 * Look for reasons to bail out.  Bail out if:
	 * - FC autoneg is disabled, or if
	 * - link is not up.
	 */
	if (hw->fc.disable_fc_autoneg) {
		printf("%s: flow control autoneg is disabled\n", DEVNAME(sc));
		goto out;
	}

	hw->mac.ops.check_link(hw, &speed, &link_up, 0);
	if (!link_up)
		goto out;

	switch (hw->phy.media_type) {
	/* Autoneg flow control on fiber adapters */
	case ngbe_media_type_fiber:
		break;

	/* Autoneg flow control on copper adapters */
	case ngbe_media_type_copper:
		error = ngbe_fc_autoneg_copper(sc);
		break;
	default:
		break;
	}
out:
	if (error) {
		hw->fc.fc_was_autonegged = 0;
		hw->fc.current_mode = hw->fc.requested_mode;
	} else
		hw->fc.fc_was_autonegged = 1;
}

int
ngbe_fc_autoneg_copper(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint8_t technology_ability_reg, lp_technology_ability_reg;

	technology_ability_reg = lp_technology_ability_reg = 0;
	if (!((hw->subsystem_device_id & OEM_MASK) == RGMII_FPGA)) {
		hw->phy.ops.get_adv_pause(hw, &technology_ability_reg);
		hw->phy.ops.get_lp_adv_pause(hw, &lp_technology_ability_reg);
	}

	return ngbe_negotiate_fc(sc, (uint32_t)technology_ability_reg,
	    (uint32_t)lp_technology_ability_reg, NGBE_TAF_SYM_PAUSE,
	    NGBE_TAF_ASM_PAUSE, NGBE_TAF_SYM_PAUSE, NGBE_TAF_ASM_PAUSE);
}

int
ngbe_fc_enable(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t mflcn, fccfg;
	uint32_t fcrtl, fcrth;
	uint32_t reg;
	int error = 0;

	/* Validate the water mark configuration */
	if (!hw->fc.pause_time) {
		error = EINVAL;
		goto out;
	}

	/* Low water mark of zero causes XOFF floods */
	if ((hw->fc.current_mode & ngbe_fc_tx_pause) && hw->fc.high_water) {
		if (!hw->fc.low_water ||
		    hw->fc.low_water >= hw->fc.high_water) {
			printf("%s: invalid water mark configuration\n",
			    DEVNAME(sc));
			error = EINVAL;
			goto out;
		}
	}

	/* Negotiate the fc mode to use */
	ngbe_fc_autoneg(sc);

	/* Disable any previous flow control settings */
	mflcn = NGBE_READ_REG(hw, NGBE_MAC_RX_FLOW_CTRL);
	mflcn &= ~NGBE_MAC_RX_FLOW_CTRL_RFE;

	fccfg = NGBE_READ_REG(hw, NGBE_RDB_RFCC);
	fccfg &= ~NGBE_RDB_RFCC_RFCE_802_3X;

	/*
	 * The possible values of fc.current_mode are:
	 * 0: Flow control is completely disabled
	 * 1: Rx flow control is enabled (we can receive pause frames,
	 *    but not send pause frames).
	 * 2: Tx flow control is enabled (we can send pause frames but
	 *    we do not support receiving pause frames).
	 * 3: Both Rx and Tx flow control (symmetric) are enabled.
	 * other: Invalid.
	 */
	switch (hw->fc.current_mode) {
	case ngbe_fc_none:
		/*
		 * Flow control is disabled by software override or autoneg.
		 * The code below will actually disable it in the HW.
		 */
		break;
	case ngbe_fc_rx_pause:
		/*
		 * Rx Flow control is enabled and Tx Flow control is
		 * disabled by software override. Since there really
		 * isn't a way to advertise that we are capable of RX
		 * Pause ONLY, we will advertise that we support both
		 * symmetric and asymmetric Rx PAUSE.  Later, we will
		 * disable the adapter's ability to send PAUSE frames.
		 */
		mflcn |= NGBE_MAC_RX_FLOW_CTRL_RFE;
		break;
	case ngbe_fc_tx_pause:
		/*
		 * Tx Flow control is enabled, and Rx Flow control is
		 * disabled by software override.
		 */
		fccfg |= NGBE_RDB_RFCC_RFCE_802_3X;
		break;
	case ngbe_fc_full:
		/* Flow control (both Rx and Tx) is enabled by SW override. */
		mflcn |= NGBE_MAC_RX_FLOW_CTRL_RFE;
		fccfg |= NGBE_RDB_RFCC_RFCE_802_3X;
		break;
	default:
		printf("%s: flow control param set incorrectly\n", DEVNAME(sc));
		error = EINVAL;
		goto out;
	}

	/* Set 802.3x based flow control settings. */
	NGBE_WRITE_REG(hw, NGBE_MAC_RX_FLOW_CTRL, mflcn);
	NGBE_WRITE_REG(hw, NGBE_RDB_RFCC, fccfg);

	/* Set up and enable Rx high/low water mark thresholds, enable XON. */
	if ((hw->fc.current_mode & ngbe_fc_tx_pause) && hw->fc.high_water) {
		/* 32Byte granularity */
		fcrtl = (hw->fc.low_water << 10) | NGBE_RDB_RFCL_XONE;
		NGBE_WRITE_REG(hw, NGBE_RDB_RFCL, fcrtl);
		fcrth = (hw->fc.high_water << 10) | NGBE_RDB_RFCH_XOFFE;
	} else {
		NGBE_WRITE_REG(hw, NGBE_RDB_RFCL, 0);
		/*
		 * In order to prevent Tx hangs when the internal Tx
		 * switch is enabled we must set the high water mark
		 * to the Rx packet buffer size - 24KB.  This allows
		 * the Tx switch to function even under heavy Rx
		 * workloads.
		 */
		fcrth = NGBE_READ_REG(hw, NGBE_RDB_PB_SZ) - 24576;
	}

	NGBE_WRITE_REG(hw, NGBE_RDB_RFCH, fcrth);

	/* Configure pause time (2 TCs per register) */
	reg = hw->fc.pause_time * 0x00010000;
	NGBE_WRITE_REG(hw, NGBE_RDB_RFCV, reg);

	/* Configure flow control refresh threshold value */
	NGBE_WRITE_REG(hw, NGBE_RDB_RFCRT, hw->fc.pause_time / 2);
out:
	return error;
}

int
ngbe_fmgr_cmd_op(struct ngbe_hw *hw, uint32_t cmd, uint32_t cmd_addr)
{
	uint32_t val;
	int timeout = 0;

	val = (cmd << SPI_CLK_CMD_OFFSET) | cmd_addr |
	    (SPI_CLK_DIV << SPI_CLK_DIV_OFFSET);
	NGBE_WRITE_REG(hw, NGBE_SPI_CMD, val);
	for (;;) {
		if (NGBE_READ_REG(hw, NGBE_SPI_STATUS) & 0x1)
			break;
		if (timeout == SPI_TIME_OUT_VALUE)
			return ETIMEDOUT;

		timeout++;
		DELAY(10);
	}

	return 0;
}

uint32_t
ngbe_flash_read_dword(struct ngbe_hw *hw, uint32_t addr)
{
	int status = ngbe_fmgr_cmd_op(hw, SPI_CMD_READ_DWORD, addr);
	if (status)
		return status;

	return NGBE_READ_REG(hw, NGBE_SPI_DATA);
}

uint8_t
ngbe_calculate_checksum(uint8_t *buffer, uint32_t length)
{
	uint32_t i;
	uint8_t sum = 0;

	if (!buffer)
		return 0;

	for (i = 0; i < length; i++)
		sum += buffer[i];
	return (uint8_t)(0 - sum);
}

int
ngbe_check_flash_load(struct ngbe_softc *sc, uint32_t check_bit)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t reg = 0;
	int i, error = 0;

	/* if there's flash existing */
	if (!(NGBE_READ_REG(hw, NGBE_SPI_STATUS) &
	    NGBE_SPI_STATUS_FLASH_BYPASS)) {
		/* wait hw load flash done */
		for (i = 0; i < NGBE_MAX_FLASH_LOAD_POLL_TIME; i++) {
			reg = NGBE_READ_REG(hw, NGBE_SPI_ILDR_STATUS);
			if (!(reg & check_bit))
				break;
			msec_delay(200);
		}
		if (i == NGBE_MAX_FLASH_LOAD_POLL_TIME) {
			error = ETIMEDOUT;
			printf("%s: hardware loading flash failed\n",
			    DEVNAME(sc));
		}
	}
	return error;
}

int
ngbe_check_internal_phy_id(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint16_t phy_id, phy_id_high, phy_id_low;

	ngbe_gphy_wait_mdio_access_on(hw);

	ngbe_phy_read_reg(hw, NGBE_MDI_PHY_ID1_OFFSET, 0, &phy_id_high);
	phy_id = phy_id_high << 6;
	ngbe_phy_read_reg(hw, NGBE_MDI_PHY_ID2_OFFSET, 0, &phy_id_low);
	phy_id |= (phy_id_low & NGBE_MDI_PHY_ID_MASK) >> 10;

	if (NGBE_INTERNAL_PHY_ID != phy_id) {
		printf("%s: internal phy id 0x%x not supported\n",
		    DEVNAME(sc), phy_id);
		return ENOTSUP;
	} else
		hw->phy.id = (uint32_t)phy_id;

	return 0;
}

int
ngbe_check_mac_link(struct ngbe_hw *hw, uint32_t *speed, int *link_up,
    int link_up_wait_to_complete)
{
	uint32_t status = 0;
	uint16_t speed_sta, value = 0;
	int i;

	if ((hw->subsystem_device_id & OEM_MASK) == RGMII_FPGA) {
		*link_up = 1;
		*speed = NGBE_LINK_SPEED_1GB_FULL;
		return status;
	}

	if (link_up_wait_to_complete) {
		for (i = 0; i < NGBE_LINK_UP_TIME; i++) {
			status = hw->phy.ops.read_reg(hw,
			    NGBE_MDIO_AUTO_NEG_STATUS,
			    NGBE_INTERNAL_PHY_PAGE_OFFSET, &value);
			if (!status && (value & 0x4)) {
				*link_up = 1;
				break;
			} else
				*link_up = 0;
			msec_delay(100);
		}
	} else {
		status = hw->phy.ops.read_reg(hw, NGBE_MDIO_AUTO_NEG_STATUS,
		    NGBE_INTERNAL_PHY_PAGE_OFFSET, &value);
		if (!status && (value & 0x4))
			*link_up = 1;
		else
			*link_up = 0;
	}

	speed_sta = value & 0x38;
	if (*link_up) {
		if (speed_sta == 0x28)
			*speed = NGBE_LINK_SPEED_1GB_FULL;
		else if (speed_sta == 0x18)
			*speed = NGBE_LINK_SPEED_100_FULL;
		else if (speed_sta == 0x8)
			*speed = NGBE_LINK_SPEED_10_FULL;
	} else
		*speed = NGBE_LINK_SPEED_UNKNOWN;
		
	return status;
}

int
ngbe_check_mng_access(struct ngbe_hw *hw)
{
	if (!ngbe_mng_present(hw))
		return 0;
	return 1;
}

int
ngbe_check_reset_blocked(struct ngbe_softc *sc)
{
	uint32_t mmngc;

	mmngc = NGBE_READ_REG(&sc->hw, NGBE_MIS_ST);
	if (mmngc & NGBE_MIS_ST_MNG_VETO) {
		printf("%s: MNG_VETO bit detected\n", DEVNAME(sc));
		return 1;
	}

	return 0;
}

void
ngbe_clear_hw_cntrs(struct ngbe_hw *hw)
{
	uint16_t i;

	NGBE_READ_REG(hw, NGBE_RX_CRC_ERROR_FRAMES_LOW);
	NGBE_READ_REG(hw, NGBE_RX_LEN_ERROR_FRAMES_LOW);
	NGBE_READ_REG(hw, NGBE_RDB_LXONTXC);
	NGBE_READ_REG(hw, NGBE_RDB_LXOFFTXC);
	NGBE_READ_REG(hw, NGBE_MAC_LXOFFRXC);

	for (i = 0; i < 8; i++) {
		NGBE_WRITE_REG_MASK(hw, NGBE_MMC_CONTROL, NGBE_MMC_CONTROL_UP,
		    i << 16);
		NGBE_READ_REG(hw, NGBE_MAC_PXOFFRXC);
	}

	NGBE_READ_REG(hw, NGBE_PX_GPRC);
	NGBE_READ_REG(hw, NGBE_PX_GPTC);
	NGBE_READ_REG(hw, NGBE_PX_GORC_MSB);
	NGBE_READ_REG(hw, NGBE_PX_GOTC_MSB);

	NGBE_READ_REG(hw, NGBE_RX_BC_FRAMES_GOOD_LOW);
	NGBE_READ_REG(hw, NGBE_RX_UNDERSIZE_FRAMES_GOOD);
	NGBE_READ_REG(hw, NGBE_RX_OVERSIZE_FRAMES_GOOD);
	NGBE_READ_REG(hw, NGBE_RX_FRAME_CNT_GOOD_BAD_LOW);
	NGBE_READ_REG(hw, NGBE_TX_FRAME_CNT_GOOD_BAD_LOW);
	NGBE_READ_REG(hw, NGBE_TX_MC_FRAMES_GOOD_LOW);
	NGBE_READ_REG(hw, NGBE_TX_BC_FRAMES_GOOD_LOW);
	NGBE_READ_REG(hw, NGBE_RDM_DRP_PKT);
}

void
ngbe_clear_vfta(struct ngbe_hw *hw)
{
	uint32_t offset;

	for (offset = 0; offset < hw->mac.vft_size; offset++) {
		NGBE_WRITE_REG(hw, NGBE_PSR_VLAN_TBL(offset), 0);
		/* Errata 5 */
		hw->mac.vft_shadow[offset] = 0;
	}

	for (offset = 0; offset < NGBE_PSR_VLAN_SWC_ENTRIES; offset++) {
		NGBE_WRITE_REG(hw, NGBE_PSR_VLAN_SWC_IDX, offset);
		NGBE_WRITE_REG(hw, NGBE_PSR_VLAN_SWC, 0);
		NGBE_WRITE_REG(hw, NGBE_PSR_VLAN_SWC_VM_L, 0);
	}
}

void
ngbe_configure_ivars(struct ngbe_softc *sc)
{
	struct ngbe_queue *nq = sc->queues;
	uint32_t newitr;
	int i;

	/* Populate MSIX to EITR select */
	NGBE_WRITE_REG(&sc->hw, NGBE_PX_ITRSEL, 0);

	newitr = (4000000 / NGBE_MAX_INTS_PER_SEC) & NGBE_MAX_EITR;
	newitr |= NGBE_PX_ITR_CNT_WDIS;

	for (i = 0; i < sc->sc_nqueues; i++, nq++) {
		/* Rx queue entry */
		ngbe_set_ivar(sc, i, nq->msix, 0);
		/* Tx queue entry */
		ngbe_set_ivar(sc, i, nq->msix, 1);
		NGBE_WRITE_REG(&sc->hw, NGBE_PX_ITR(nq->msix), newitr);
	}

	/* For the Link interrupt */
	ngbe_set_ivar(sc, 0, sc->linkvec, -1);
	NGBE_WRITE_REG(&sc->hw, NGBE_PX_ITR(sc->linkvec), 1950);
}

void
ngbe_configure_pb(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	
	hw->mac.ops.setup_rxpba(hw, 0, 0, PBA_STRATEGY_EQUAL);
	ngbe_pbthresh_setup(sc);
}

void
ngbe_disable_intr(struct ngbe_softc *sc)
{
	struct ngbe_queue *nq;
	int i;

	NGBE_WRITE_REG(&sc->hw, NGBE_PX_MISC_IEN, 0);
	for (i = 0, nq = sc->queues; i < sc->sc_nqueues; i++, nq++)
		ngbe_disable_queue(sc, nq->msix);
	NGBE_WRITE_FLUSH(&sc->hw);
}

int
ngbe_disable_pcie_master(struct ngbe_softc *sc)
{
	int i, error = 0;

	/* Exit if master requests are blocked */
	if (!(NGBE_READ_REG(&sc->hw, NGBE_PX_TRANSACTION_PENDING)))
		goto out;

	/* Poll for master request bit to clear */
	for (i = 0; i < NGBE_PCI_MASTER_DISABLE_TIMEOUT; i++) {
		DELAY(100);
		if (!(NGBE_READ_REG(&sc->hw, NGBE_PX_TRANSACTION_PENDING)))
			goto out;
	}
	printf("%s: PCIe transaction pending bit did not clear\n",
	    DEVNAME(sc));
	error = ETIMEDOUT;
out:
	return error;
}

void
ngbe_disable_queue(struct ngbe_softc *sc, uint32_t vector)
{
	uint64_t queue = 1ULL << vector;
	uint32_t mask;

	mask = (queue & 0xffffffff);
	if (mask)
		NGBE_WRITE_REG(&sc->hw, NGBE_PX_IMS, mask);
}

void
ngbe_disable_rx(struct ngbe_hw *hw)
{
	uint32_t rxctrl, psrctrl;

	rxctrl = NGBE_READ_REG(hw, NGBE_RDB_PB_CTL);
	if (rxctrl & NGBE_RDB_PB_CTL_PBEN) {
		psrctrl = NGBE_READ_REG(hw, NGBE_PSR_CTL);
		if (psrctrl & NGBE_PSR_CTL_SW_EN) {
			psrctrl &= ~NGBE_PSR_CTL_SW_EN;
			NGBE_WRITE_REG(hw, NGBE_PSR_CTL, psrctrl);
			hw->mac.set_lben = 1;
		} else
			hw->mac.set_lben = 0;
		rxctrl &= ~NGBE_RDB_PB_CTL_PBEN;
		NGBE_WRITE_REG(hw, NGBE_RDB_PB_CTL, rxctrl);

		NGBE_WRITE_REG_MASK(hw, NGBE_MAC_RX_CFG, NGBE_MAC_RX_CFG_RE,
		    0);
	}
}

void
ngbe_disable_sec_rx_path(struct ngbe_hw *hw)
{
	uint32_t secrxreg;
	int i;

	NGBE_WRITE_REG_MASK(hw, NGBE_RSEC_CTL, NGBE_RSEC_CTL_RX_DIS,
	    NGBE_RSEC_CTL_RX_DIS);
	for (i = 0; i < 40; i++) {
		secrxreg = NGBE_READ_REG(hw, NGBE_RSEC_ST);
		if (secrxreg & NGBE_RSEC_ST_RSEC_RDY)
			break;
		else
			DELAY(1000);
	}
}

int
ngbe_eepromcheck_cap(struct ngbe_softc *sc, uint16_t offset, uint32_t *data)
{
	struct ngbe_hw *hw = &sc->hw;
	struct ngbe_hic_read_shadow_ram buffer;
	uint32_t tmp;
	int status;

	buffer.hdr.req.cmd = FW_EEPROM_CHECK_STATUS;
	buffer.hdr.req.buf_lenh = 0;
	buffer.hdr.req.buf_lenl = 0;
	buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

	/* Convert offset from words to bytes */
	buffer.address = 0;
	/* one word */
	buffer.length = 0;

	status = ngbe_host_interface_command(sc, (uint32_t *)&buffer,
	    sizeof(buffer), NGBE_HI_COMMAND_TIMEOUT, 0);
	if (status)
		return status;

	if (ngbe_check_mng_access(hw)) {
		tmp = NGBE_READ_REG_ARRAY(hw, NGBE_MNG_MBOX, 1);
		if (tmp == NGBE_CHECKSUM_CAP_ST_PASS)
			status = 0;
		else
			status = EINVAL;
	} else
		status = EINVAL;

	return status;
}

void
ngbe_enable_intr(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	struct ngbe_queue *nq;
	uint32_t mask;
	int i;

	/* Enable misc interrupt */
	mask = NGBE_PX_MISC_IEN_MASK;

	mask |= NGBE_PX_MISC_IEN_OVER_HEAT;
	NGBE_WRITE_REG(hw, NGBE_GPIO_DDR, 0x1);
	NGBE_WRITE_REG(hw, NGBE_GPIO_INTEN, 0x3);
	NGBE_WRITE_REG(hw, NGBE_GPIO_INTTYPE_LEVEL, 0x0);

	NGBE_WRITE_REG(hw, NGBE_GPIO_POLARITY, 0x3);

	NGBE_WRITE_REG(hw, NGBE_PX_MISC_IEN, mask);

	/* Enable all queues */
	for (i = 0, nq = sc->queues; i < sc->sc_nqueues; i++, nq++)
		ngbe_enable_queue(sc, nq->msix);
	NGBE_WRITE_FLUSH(hw);

	ngbe_enable_queue(sc, sc->linkvec);
}

void
ngbe_enable_queue(struct ngbe_softc *sc, uint32_t vector)
{
	uint64_t queue = 1ULL << vector;
	uint32_t mask;

	mask = (queue & 0xffffffff);
	if (mask)
		NGBE_WRITE_REG(&sc->hw, NGBE_PX_IMC, mask);
}

void
ngbe_enable_rx(struct ngbe_hw *hw)
{
	uint32_t val;

	/* Enable mac receiver */
	NGBE_WRITE_REG_MASK(hw, NGBE_MAC_RX_CFG, NGBE_MAC_RX_CFG_RE,
	    NGBE_MAC_RX_CFG_RE);

	NGBE_WRITE_REG_MASK(hw, NGBE_RSEC_CTL, 0x2, 0);

	NGBE_WRITE_REG_MASK(hw, NGBE_RDB_PB_CTL, NGBE_RDB_PB_CTL_PBEN,
	    NGBE_RDB_PB_CTL_PBEN);

	if (hw->mac.set_lben) {
		val = NGBE_READ_REG(hw, NGBE_PSR_CTL);
		val |= NGBE_PSR_CTL_SW_EN;
		NGBE_WRITE_REG(hw, NGBE_PSR_CTL, val);
		hw->mac.set_lben = 0;
	}
}

void
ngbe_enable_rx_dma(struct ngbe_hw *hw, uint32_t reg)
{
	/*
	 * Workaround for emerald silicon errata when enabling the Rx datapath.
	 * If traffic is incoming before we enable the Rx unit, it could hang
	 * the Rx DMA unit.  Therefore, make sure the security engine is
	 * completely disabled prior to enabling the Rx unit.
	 */
	hw->mac.ops.disable_sec_rx_path(hw);

	if (reg & NGBE_RDB_PB_CTL_PBEN)
		hw->mac.ops.enable_rx(hw);
	else
		hw->mac.ops.disable_rx(hw);

	hw->mac.ops.enable_sec_rx_path(hw);
}

void
ngbe_enable_sec_rx_path(struct ngbe_hw *hw)
{
	NGBE_WRITE_REG_MASK(hw, NGBE_RSEC_CTL, NGBE_RSEC_CTL_RX_DIS, 0);
	NGBE_WRITE_FLUSH(hw);
}

int
ngbe_encap(struct tx_ring *txr, struct mbuf *m)
{
	struct ngbe_softc *sc = txr->sc;
	uint32_t olinfo_status = 0, cmd_type_len;
	int i, j, ntxc;
	int first, last = 0;
	bus_dmamap_t map;
	struct ngbe_tx_buf *txbuf;
	union ngbe_tx_desc *txd = NULL;

	/* Basic descriptor defines */
	cmd_type_len = NGBE_TXD_DTYP_DATA | NGBE_TXD_IFCS;

	/*
	 * Important to capture the first descriptor
	 * used because it will contain the index of
	 * the one we tell the hardware to report back
	 */
	first = txr->next_avail_desc;
	txbuf = &txr->tx_buffers[first];
	map = txbuf->map;

	/*
	 * Set the appropriate offload context
	 * this will becomes the first descriptor.
	 */
	ntxc = ngbe_tx_ctx_setup(txr, m, &cmd_type_len, &olinfo_status);
	if (ntxc == -1)
		goto fail;

	/*
	 * Map the packet for DMA.
	 */
	switch (bus_dmamap_load_mbuf(txr->txdma.dma_tag, map, m,
	    BUS_DMA_NOWAIT)) {
	case 0:
		break;
	case EFBIG:
		if (m_defrag(m, M_NOWAIT) == 0 &&
		    bus_dmamap_load_mbuf(txr->txdma.dma_tag, map, m,
		    BUS_DMA_NOWAIT) == 0)
			break;
		/* FALLTHROUGH */
	default:
		return 0;
	}

	i = txr->next_avail_desc + ntxc;
	if (i >= sc->num_tx_desc)
		i -= sc->num_tx_desc;

	for (j = 0; j < map->dm_nsegs; j++) {
		txd = &txr->tx_base[i];

		txd->read.buffer_addr = htole64(map->dm_segs[j].ds_addr);
		txd->read.cmd_type_len =
		    htole32(cmd_type_len | map->dm_segs[j].ds_len);
		txd->read.olinfo_status = htole32(olinfo_status);
		last = i;

		if (++i == sc->num_tx_desc)
			i = 0;
	}

	txd->read.cmd_type_len |= htole32(NGBE_TXD_EOP | NGBE_TXD_RS);

	bus_dmamap_sync(txr->txdma.dma_tag, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	/* Set the index of the descriptor that will be marked done */
	txbuf->m_head = m;
	txbuf->eop_index = last;

	txr->next_avail_desc = i;

	return ntxc + j;

fail:
	bus_dmamap_unload(txr->txdma.dma_tag, txbuf->map);
	return 0;
}

int
ngbe_get_buf(struct rx_ring *rxr, int i)
{
	struct ngbe_softc *sc = rxr->sc;
	struct ngbe_rx_buf *rxbuf;
	struct mbuf *m;
	union ngbe_rx_desc *rxdesc;
	int error;

	rxbuf = &rxr->rx_buffers[i];
	rxdesc = &rxr->rx_base[i];
	if (rxbuf->buf) {
		printf("%s: slot %d already has an mbuf\n", DEVNAME(sc), i);
		return ENOBUFS;
	}

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES + ETHER_ALIGN);
	if (!m)
		return ENOBUFS;

	m->m_data += (m->m_ext.ext_size - (MCLBYTES + ETHER_ALIGN));
	m->m_len = m->m_pkthdr.len = MCLBYTES + ETHER_ALIGN;

	error = bus_dmamap_load_mbuf(rxr->rxdma.dma_tag, rxbuf->map, m,
	    BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		return error;
	}

	bus_dmamap_sync(rxr->rxdma.dma_tag, rxbuf->map, 0,
	    rxbuf->map->dm_mapsize, BUS_DMASYNC_PREREAD);
	rxbuf->buf = m;

	rxdesc->read.pkt_addr = htole64(rxbuf->map->dm_segs[0].ds_addr);

	return 0;
}

void
ngbe_get_bus_info(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint16_t link_status;

	/* Get the negotiated link width and speed from PCI config space */
	link_status = ngbe_read_pci_cfg_word(sc, NGBE_PCI_LINK_STATUS);

	ngbe_set_pci_config_data(hw, link_status);
}

void
ngbe_get_copper_link_capabilities(struct ngbe_hw *hw, uint32_t *speed,
    int *autoneg)
{
	*speed = 0;

	if (hw->mac.autoneg)
		*autoneg = 1;
	else
		*autoneg = 0;

	*speed = NGBE_LINK_SPEED_10_FULL | NGBE_LINK_SPEED_100_FULL |
	    NGBE_LINK_SPEED_1GB_FULL;
}

int
ngbe_get_eeprom_semaphore(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t swsm;
	int i, timeout = 2000;
	int status = ETIMEDOUT;

	/* Get SMBI software semaphore between device drivers first */
	for (i = 0; i < timeout; i++) {
		/*
		 * If the SMBI bit is 0 when we read it, then the bit will be
		 * set and we have the semaphore.
		 */
		swsm = NGBE_READ_REG(hw, NGBE_MIS_SWSM);
		if (!(swsm & NGBE_MIS_SWSM_SMBI)) {
			status = 0;
			break;
		}	
		DELAY(50);
	}

	if (i == timeout) {
		printf("%s: cannot access the eeprom - SMBI semaphore not "
		    "granted\n", DEVNAME(sc));
		/*
		 * this release is particularly important because our attempts
		 * above to get the semaphore may have succeeded, and if there
		 * was a timeout, we should unconditionally clear the semaphore
		 * bits to free the driver to make progress.
		 */
		ngbe_release_eeprom_semaphore(hw);
		DELAY(50);

		/* 
		 * One last try if the SMBI bit is 0 when we read it,
		 * then the bit will be set and we have the semaphore.
		 */
		swsm = NGBE_READ_REG(hw, NGBE_MIS_SWSM);
		if (!(swsm & NGBE_MIS_SWSM_SMBI))
			status = 0;
	}

	return status;
}

void
ngbe_get_hw_control(struct ngbe_hw *hw)
{
	 /* Let firmware know the driver has taken over */
	NGBE_WRITE_REG_MASK(hw, NGBE_CFG_PORT_CTL,
	    NGBE_CFG_PORT_CTL_DRV_LOAD, NGBE_CFG_PORT_CTL_DRV_LOAD);
}

void
ngbe_release_hw_control(struct ngbe_softc *sc)
{
	/* Let firmware take over control of hw. */
	NGBE_WRITE_REG_MASK(&sc->hw, NGBE_CFG_PORT_CTL,
	    NGBE_CFG_PORT_CTL_DRV_LOAD, 0);
}

void
ngbe_get_mac_addr(struct ngbe_hw *hw, uint8_t *mac_addr)
{
	uint32_t rar_high, rar_low;
	int i;

	NGBE_WRITE_REG(hw, NGBE_PSR_MAC_SWC_IDX, 0);
	rar_high = NGBE_READ_REG(hw, NGBE_PSR_MAC_SWC_AD_H);
	rar_low = NGBE_READ_REG(hw, NGBE_PSR_MAC_SWC_AD_L);

	for (i = 0; i < 2; i++)
		mac_addr[i] = (uint8_t)(rar_high >> (1 - i) * 8);

	for (i = 0; i < 4; i++)
		mac_addr[i + 2] = (uint8_t)(rar_low >> (3 - i) * 8);
}

enum ngbe_media_type
ngbe_get_media_type(struct ngbe_hw *hw)
{
	enum ngbe_media_type media_type = ngbe_media_type_copper;

	return media_type;
}

void
ngbe_gphy_dis_eee(struct ngbe_hw *hw)
{
	uint16_t val = 0;

	hw->phy.ops.write_reg(hw, 0x11, 0xa4b, 0x1110);
	hw->phy.ops.write_reg(hw, MII_MMDACR, 0x0, MMDACR_FN_ADDRESS | 0x07);
	hw->phy.ops.write_reg(hw, MII_MMDAADR, 0x0, 0x003c);
	hw->phy.ops.write_reg(hw, MII_MMDACR, 0x0, MMDACR_FN_DATANPI | 0x07);
	hw->phy.ops.write_reg(hw, MII_MMDAADR, 0x0, 0);

	/* Disable 10/100M Half Duplex */
	msec_delay(100);
	hw->phy.ops.read_reg(hw, MII_ANAR, 0, &val);
	val &= ~(ANAR_TX | ANAR_10);
	hw->phy.ops.write_reg(hw, MII_ANAR, 0x0, val);
}

void
ngbe_gphy_efuse_calibration(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t efuse[2];

	ngbe_gphy_wait_mdio_access_on(hw);

	efuse[0] = sc->gphy_efuse[0];
	efuse[1] = sc->gphy_efuse[1];

	if (!efuse[0] && !efuse[1])
		efuse[0] = efuse[1] = 0xffffffff;

	/* Calibration */
	efuse[0] |= 0xf0000100;
	efuse[1] |= 0xff807fff;

	/* EODR, Efuse Output Data Register */
	ngbe_phy_write_reg(hw, 16, 0xa46, (efuse[0] >> 0) & 0xffff);
	ngbe_phy_write_reg(hw, 17, 0xa46, (efuse[0] >> 16) & 0xffff);
	ngbe_phy_write_reg(hw, 18, 0xa46, (efuse[1] >> 0) & 0xffff);
	ngbe_phy_write_reg(hw, 19, 0xa46, (efuse[1] >> 16) & 0xffff);

	/* Set efuse ready */
	ngbe_phy_write_reg(hw, 20, 0xa46, 0x01);
	ngbe_gphy_wait_mdio_access_on(hw);
	ngbe_phy_write_reg(hw, 27, NGBE_INTERNAL_PHY_PAGE_OFFSET, 0x8011);
	ngbe_phy_write_reg(hw, 28, NGBE_INTERNAL_PHY_PAGE_OFFSET, 0x5737);
	ngbe_gphy_dis_eee(hw);
}

void
ngbe_gphy_wait_mdio_access_on(struct ngbe_hw *hw)
{
	uint16_t val = 0;
	int i;

	for (i = 0; i < 100; i++) {
		ngbe_phy_read_reg(hw, 29, NGBE_INTERNAL_PHY_PAGE_OFFSET, &val);
		if (val & 0x20)
			break;
		DELAY(1000);
	}
}

void
ngbe_handle_phy_event(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t reg;

	reg = NGBE_READ_REG(hw, NGBE_GPIO_INTSTATUS);
	NGBE_WRITE_REG(hw, NGBE_GPIO_EOI, reg);
	if (!((hw->subsystem_device_id & OEM_MASK) == RGMII_FPGA))
		hw->phy.ops.check_event(sc);
}

int
ngbe_host_interface_command(struct ngbe_softc *sc, uint32_t *buffer,
    uint32_t length, uint32_t timeout, int return_data)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t hicr, i, bi, dword_len;
	uint32_t hdr_size = sizeof(struct ngbe_hic_hdr);
	uint32_t buf[64] = {};
	uint16_t buf_len;
	int status = 0;

	if (length == 0 || length > NGBE_HI_MAX_BLOCK_BYTE_LENGTH) {
		printf("%s: buffer length failure\n", DEVNAME(sc));
		return EINVAL;
	}

	if (hw->mac.ops.acquire_swfw_sync(sc, NGBE_MNG_SWFW_SYNC_SW_MB))
		return EINVAL;

	/* Calculate length in DWORDs. We must be multiple of DWORD */
	if ((length % (sizeof(uint32_t))) != 0) {
		printf("%s: buffer length failure, not aligned to dword\n",
		    DEVNAME(sc));
		status = EINVAL;
		goto rel_out;
        }

	if (ngbe_check_mng_access(hw)) {
		hicr = NGBE_READ_REG(hw, NGBE_MNG_MBOX_CTL);
		if ((hicr & NGBE_MNG_MBOX_CTL_FWRDY))
			printf("%s: fwrdy is set before command\n",
			    DEVNAME(sc));
	}

	dword_len = length >> 2;

	/* 
	 * The device driver writes the relevant command block 
	 * into the ram area.
	 */
	for (i = 0; i < dword_len; i++) {
		if (ngbe_check_mng_access(hw)) {
			NGBE_WRITE_REG_ARRAY(hw, NGBE_MNG_MBOX, i,
			    htole32(buffer[i]));
		} else {
			status = EINVAL;
			goto rel_out;
		}
	}

	/* Setting this bit tells the ARC that a new command is pending. */
	if (ngbe_check_mng_access(hw)) {
		NGBE_WRITE_REG_MASK(hw, NGBE_MNG_MBOX_CTL,
		    NGBE_MNG_MBOX_CTL_SWRDY, NGBE_MNG_MBOX_CTL_SWRDY);
	} else {
		status = EINVAL;
		goto rel_out;
	}
		
	for (i = 0; i < timeout; i++) {
		if (ngbe_check_mng_access(hw)) {
			hicr = NGBE_READ_REG(hw, NGBE_MNG_MBOX_CTL);
			if ((hicr & NGBE_MNG_MBOX_CTL_FWRDY))
				break;
		}
		msec_delay(1);
	}

	buf[0] = NGBE_READ_REG(hw, NGBE_MNG_MBOX);
	/* Check command completion */
	if (timeout != 0 && i == timeout) {
		printf("%s: command has failed with no status valid\n",
		    DEVNAME(sc));
		if ((buffer[0] & 0xff) != (~buf[0] >> 24)) {
			status = EINVAL;
			goto rel_out;
		}
	}

	if (!return_data)
		goto rel_out;

	/* Calculate length in DWORDs */
	dword_len = hdr_size >> 2;
		
	/* First pull in the header so we know the buffer length */
	for (bi = 0; bi < dword_len; bi++) {
		if (ngbe_check_mng_access(hw)) {
			buffer[bi] = NGBE_READ_REG_ARRAY(hw, NGBE_MNG_MBOX, bi);
			le32_to_cpus(&buffer[bi]);
		} else {
			status = EINVAL;
			goto rel_out;
		}
	}

	/* If there is any thing in data position pull it in */
	buf_len = ((struct ngbe_hic_hdr *)buffer)->buf_len;
	if (buf_len == 0)
		goto rel_out;

	if (length < buf_len + hdr_size) {
		printf("%s: buffer not large enough for reply message\n",
		    DEVNAME(sc));
		status = EINVAL;
		goto rel_out;
	}
	
	/* Calculate length in DWORDs, add 3 for odd lengths */
	dword_len = (buf_len + 3) >> 2;

	/* Pull in the rest of the buffer (bi is where we left off) */
	for (; bi <= dword_len; bi++) {
		if (ngbe_check_mng_access(hw)) {
			buffer[bi] = NGBE_READ_REG_ARRAY(hw, NGBE_MNG_MBOX, bi);
			le32_to_cpus(&buffer[bi]);
		} else {
			status = EINVAL;
			goto rel_out;
		}
	}

rel_out:
	hw->mac.ops.release_swfw_sync(sc, NGBE_MNG_SWFW_SYNC_SW_MB);
	return status;
}

int
ngbe_hpbthresh(struct ngbe_softc *sc)
{
	uint32_t dv_id, rx_pba;
	int kb, link, marker, tc;

	/* Calculate max LAN frame size */
	tc = link = sc->sc_ac.ac_if.if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN +
	    NGBE_ETH_FRAMING;

	/* Calculate delay value for device */
	dv_id = NGBE_DV(link, tc);

	/* Delay value is calculated in bit times convert to KB */
	kb = NGBE_BT2KB(dv_id);
	rx_pba = NGBE_READ_REG(&sc->hw, NGBE_RDB_PB_SZ) >> NGBE_RDB_PB_SZ_SHIFT;

	marker = rx_pba - kb;

	return marker;
}

int
ngbe_lpbthresh(struct ngbe_softc *sc)
{
	uint32_t dv_id;
	int tc;

	/* Calculate max LAN frame size */
	tc = sc->sc_ac.ac_if.if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;

	/* Calculate delay value for device */
	dv_id = NGBE_LOW_DV(tc);

	/* Delay value is calculated in bit times convert to KB */
	return NGBE_BT2KB(dv_id);
}

int
ngbe_mng_present(struct ngbe_hw *hw)
{
	uint32_t fwsm;

	fwsm = NGBE_READ_REG(hw, NGBE_MIS_ST);

	return fwsm & NGBE_MIS_ST_MNG_INIT_DN;
}

int
ngbe_mta_vector(struct ngbe_hw *hw, uint8_t *mc_addr)
{
	uint32_t vector = 0;
	int rshift;

	/* pick bits [47:32] of the address. */
	vector = mc_addr[4] | (((uint16_t)mc_addr[5]) << 8);
	switch (hw->mac.mc_filter_type) {
	case 0:	/* bits 47:36 */
	case 1:	/* bits 46:35 */
	case 2:	/* bits 45:34 */
		rshift = 4 - hw->mac.mc_filter_type;
		break;
	case 3:	/* bits 43:32 */
		rshift = 0;
		break;
	default:	/* Invalid mc_filter_type */
		vector = rshift = 0;
		break;
	}
	vector = (vector >> rshift) & 0x0fff;

	return vector;
}

int
ngbe_negotiate_fc(struct ngbe_softc *sc, uint32_t adv_reg, uint32_t lp_reg,
    uint32_t adv_sym, uint32_t adv_asm, uint32_t lp_sym, uint32_t lp_asm)
{
	struct ngbe_hw *hw = &sc->hw;

	if ((!(adv_reg)) || (!(lp_reg)))
		return EINVAL;

	if ((adv_reg & adv_sym) && (lp_reg & lp_sym)) {
		/*
		 * Now we need to check if the user selected Rx ONLY
		 * of pause frames.  In this case, we had to advertise
		 * FULL flow control because we could not advertise RX
		 * ONLY. Hence, we must now check to see if we need to
		 * turn OFF the TRANSMISSION of PAUSE frames.
		 */
		if (hw->fc.requested_mode == ngbe_fc_full)
			hw->fc.current_mode = ngbe_fc_full;
		else
			hw->fc.current_mode = ngbe_fc_rx_pause;

	} else if (!(adv_reg & adv_sym) && (adv_reg & adv_asm) &&
	    (lp_reg & lp_sym) && (lp_reg & lp_asm))
	    	hw->fc.current_mode = ngbe_fc_tx_pause;
	else if ((adv_reg & adv_sym) && (adv_reg & adv_asm) &&
	    !(lp_reg & lp_sym) && (lp_reg & lp_asm))
	    	hw->fc.current_mode = ngbe_fc_rx_pause;
	else
		hw->fc.current_mode = ngbe_fc_none;

	return 0;
}

int
ngbe_non_sfp_link_config(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t speed;
	int error;

	if (hw->mac.autoneg)
		speed = hw->phy.autoneg_advertised;
	else
		speed = hw->phy.force_speed;

	msec_delay(50);
	if (hw->phy.type == ngbe_phy_internal) {
		error = hw->phy.ops.setup_once(sc);
		if (error)
			return error;
	}

	error = hw->mac.ops.setup_link(sc, speed, 0);
	return error;
}

void
ngbe_pbthresh_setup(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;

	hw->fc.high_water = ngbe_hpbthresh(sc);
	hw->fc.low_water = ngbe_lpbthresh(sc);
	
	/* Low water marks must not be larger than high water marks */
	if (hw->fc.low_water > hw->fc.high_water)
		hw->fc.low_water = 0;
}

void
ngbe_phy_check_event(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint16_t value = 0;

	hw->phy.ops.read_reg(hw, NGBE_MDIO_AUTO_NEG_LSC,
	    NGBE_INTERNAL_PHY_PAGE_OFFSET, &value);
}

int
ngbe_phy_check_overtemp(struct ngbe_hw *hw)
{
	uint32_t ts_state;
	int status = 0;

	/* Check that the LASI temp alarm status was triggered */
	ts_state = NGBE_READ_REG(hw, NGBE_TS_ALARM_ST);

	if (ts_state & NGBE_TS_ALARM_ST_ALARM)
		status = 1;

	return status;
}

void
ngbe_phy_get_advertised_pause(struct ngbe_hw *hw, uint8_t *pause_bit)
{
	uint16_t value;

	hw->phy.ops.read_reg(hw, 4, 0, &value);
	*pause_bit = (uint8_t)((value >> 10) & 0x3);
}

void
ngbe_phy_get_lp_advertised_pause(struct ngbe_hw *hw, uint8_t *pause_bit)
{
	uint16_t value;

	hw->phy.ops.read_reg(hw, NGBE_MDIO_AUTO_NEG_LSC,
	    NGBE_INTERNAL_PHY_PAGE_OFFSET, &value);
	hw->phy.ops.read_reg(hw, MII_BMSR, 0, &value);
	value = (value & BMSR_ACOMP) ? 1 : 0;

	/* If AN complete then check lp adv pause */
	hw->phy.ops.read_reg(hw, MII_ANLPAR, 0, &value);
	*pause_bit = (uint8_t)((value >> 10) & 0x3);
}

int
ngbe_phy_identify(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	int error;

	switch(hw->phy.type) {
	case ngbe_phy_internal:
		error = ngbe_check_internal_phy_id(sc);
		break;
	default:
		error = ENOTSUP;
	}

	return error;
}

int
ngbe_phy_init(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint16_t value;
	uint8_t lan_id = hw->bus.lan_id;
	int error;

	/* Set fwsw semaphore mask for phy first */
	if (!hw->phy.phy_semaphore_mask)
		hw->phy.phy_semaphore_mask = NGBE_MNG_SWFW_SYNC_SW_PHY;

	/* Init phy.addr according to HW design */
	hw->phy.addr = 0;

	/* Identify the PHY or SFP module */
	error = hw->phy.ops.identify(sc);
	if (error == ENOTSUP)
		return error;

	/* Enable interrupts, only link status change and an done is allowed */
	if (hw->phy.type == ngbe_phy_internal) {
		value = NGBE_INTPHY_INT_LSC | NGBE_INTPHY_INT_ANC;
		hw->phy.ops.write_reg(hw, 0x12, 0xa42, value);
		sc->gphy_efuse[0] =
		    ngbe_flash_read_dword(hw, 0xfe010 + lan_id * 8);
		sc->gphy_efuse[1] =
		    ngbe_flash_read_dword(hw, 0xfe010 + lan_id * 8 + 4);
	}

	return error;
}

void
ngbe_phy_led_ctrl(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint16_t value;

	if (sc->led_conf != -1)
		value = sc->led_conf & 0xffff;
	else
		value = 0x205b;
	hw->phy.ops.write_reg(hw, 16, 0xd04, value);
	hw->phy.ops.write_reg(hw, 17, 0xd04, 0);

	hw->phy.ops.read_reg(hw, 18, 0xd04, &value);
	if (sc->led_conf != -1) {
		value &= ~0x73;
		value |= sc->led_conf >> 16;
	} else {
		value &= 0xfffc;
		/* Act led blinking mode set to 60ms */
		value |= 0x2;
	}
	hw->phy.ops.write_reg(hw, 18, 0xd04, value);
}

int
ngbe_phy_led_oem_chk(struct ngbe_softc *sc, uint32_t *data)
{
	struct ngbe_hw *hw = &sc->hw;
	struct ngbe_hic_read_shadow_ram buffer;
	uint32_t tmp;
	int status;

	buffer.hdr.req.cmd = FW_PHY_LED_CONF;
	buffer.hdr.req.buf_lenh = 0;
	buffer.hdr.req.buf_lenl = 0;
	buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

	/* Convert offset from words to bytes */
	buffer.address = 0;
	/* One word */
	buffer.length = 0;

	status = ngbe_host_interface_command(sc, (uint32_t *)&buffer,
	    sizeof(buffer), NGBE_HI_COMMAND_TIMEOUT, 0);
	if (status)
		return status;

	if (ngbe_check_mng_access(hw)) {
		tmp = NGBE_READ_REG_ARRAY(hw, NGBE_MNG_MBOX, 1);
		if (tmp == NGBE_CHECKSUM_CAP_ST_PASS) {
			tmp = NGBE_READ_REG_ARRAY(hw, NGBE_MNG_MBOX, 2);
			*data = tmp;
			status = 0;
		} else if (tmp == NGBE_CHECKSUM_CAP_ST_FAIL) {
			*data = tmp;
			status = EINVAL;
		} else
			status = EINVAL;
	} else {
		status = EINVAL;
		return status;
	}

	return status;
}

int
ngbe_phy_read_reg(struct ngbe_hw *hw, uint32_t off, uint32_t page,
    uint16_t *data)
{
	*data = 0;

	if (!((page == NGBE_INTERNAL_PHY_PAGE_OFFSET) &&
	    ((off == NGBE_MDIO_AUTO_NEG_STATUS) ||
	    (off == NGBE_MDIO_AUTO_NEG_LSC)))) {
		NGBE_WRITE_REG(hw,
		    NGBE_PHY_CONFIG(NGBE_INTERNAL_PHY_PAGE_SELECT_OFFSET),
		    page);
	}
	*data = NGBE_READ_REG(hw, NGBE_PHY_CONFIG(off)) & 0xffff;

	return 0;
}

int
ngbe_phy_write_reg(struct ngbe_hw *hw, uint32_t off, uint32_t page,
    uint16_t data)
{
	if (!((page == NGBE_INTERNAL_PHY_PAGE_OFFSET) &&
	    ((off == NGBE_MDIO_AUTO_NEG_STATUS) ||
	    (off == NGBE_MDIO_AUTO_NEG_LSC)))) {
		NGBE_WRITE_REG(hw,
		    NGBE_PHY_CONFIG(NGBE_INTERNAL_PHY_PAGE_SELECT_OFFSET),
		    page);
	}
	NGBE_WRITE_REG(hw, NGBE_PHY_CONFIG(off), data);

	return 0;
}

int
ngbe_phy_reset(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint16_t value;
	int i, status;

	/* only support internal phy */
	if (hw->phy.type != ngbe_phy_internal) {
		printf("%s: operation not supported\n", DEVNAME(sc));
		return EINVAL;
	}

	/* Don't reset PHY if it's shut down due to overtemp. */
	if (!hw->phy.reset_if_overtemp && hw->phy.ops.check_overtemp(hw) != 0) {
		printf("%s: overtemp! skip phy reset\n", DEVNAME(sc));
		return EINVAL;
	}

	/* Blocked by MNG FW so bail */
	status = ngbe_check_reset_blocked(sc);
	if (status)
		return status;

	value = NGBE_MDI_PHY_RESET;
	status = hw->phy.ops.write_reg(hw, 0, 0, value);
	for (i = 0; i < NGBE_PHY_RST_WAIT_PERIOD; i++) {
		status = hw->phy.ops.read_reg(hw, 0, 0, &value);
		if (!(value & NGBE_MDI_PHY_RESET))
			break;
		msec_delay(1);
	}

	if (i == NGBE_PHY_RST_WAIT_PERIOD) {
		printf("%s: phy mode reset did not complete\n", DEVNAME(sc));
		return ETIMEDOUT;
	}

	return status;
}

int
ngbe_phy_set_pause_advertisement(struct ngbe_hw *hw, uint16_t pause_bit)
{
	uint16_t value;
	int status;

	status = hw->phy.ops.read_reg(hw, MII_ANAR, 0, &value);
	value &= ~0xc00;
	value |= pause_bit;
	status = hw->phy.ops.write_reg(hw, MII_ANAR, 0, value);
	return status;
}

int
ngbe_phy_setup(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint16_t value = 0;
	int i;

	for (i = 0; i < 15; i++) {
		if (!NGBE_READ_REG_MASK(hw, NGBE_MIS_ST,
		    NGBE_MIS_ST_GPHY_IN_RST(hw->bus.lan_id)))
			break;
		msec_delay(1);
	}
	if (i == 15) {
		printf("%s: gphy reset exceeds maximum time\n", DEVNAME(sc));
		return ETIMEDOUT;
	}

	ngbe_gphy_efuse_calibration(sc);
	hw->phy.ops.write_reg(hw, 20, 0xa46, 2);
	ngbe_gphy_wait_mdio_access_on(hw);

	for (i = 0; i < 100; i++) {
		hw->phy.ops.read_reg(hw, 16, 0xa42, &value);
		if ((value & 0x7) == 3)
			break;
		DELAY(1000);
	}
	if (i == 100) {
		printf("%s: phy reset exceeds maximum time\n", DEVNAME(sc));
		return ETIMEDOUT;
	}

	return 0;
}

int
ngbe_phy_setup_link(struct ngbe_softc *sc, uint32_t speed, int need_restart)
{
	struct ngbe_hw *hw = &sc->hw;
	uint16_t value = 0;
	int status;

	if (!hw->mac.autoneg) {
		status = hw->phy.ops.reset(sc);
		if (status) {
			printf("%s: phy reset failed\n", DEVNAME(sc));
			return status;
		}

		switch (speed) {
		case NGBE_LINK_SPEED_1GB_FULL:
			value = NGBE_MDI_PHY_SPEED_SELECT1;
			break;
		case NGBE_LINK_SPEED_100_FULL:
	      		value = NGBE_MDI_PHY_SPEED_SELECT0;
			break;
		case NGBE_LINK_SPEED_10_FULL:
			value = 0;
			break;
		default:
			value = NGBE_MDI_PHY_SPEED_SELECT0 |
			    NGBE_MDI_PHY_SPEED_SELECT1;
			printf("%s: unknown speed = 0x%x\n",
			    DEVNAME(sc), speed);
			break;
		}
		/* duplex full */
		value |= NGBE_MDI_PHY_DUPLEX;
		hw->phy.ops.write_reg(hw, 0, 0, value);

		goto skip_an;
	}

	/* Disable 10/100M Half Duplex */
	hw->phy.ops.read_reg(hw, 4, 0, &value);
	value &= 0xff5f;
	hw->phy.ops.write_reg(hw, 4, 0, value);

	/* Set advertise enable according to input speed */
	hw->phy.ops.read_reg(hw, 9, 0, &value);
	if (!(speed & NGBE_LINK_SPEED_1GB_FULL))
		value &= 0xfdff;
	else 
		value |= 0x200;
	hw->phy.ops.write_reg(hw, 9, 0, value);

	hw->phy.ops.read_reg(hw, 4, 0, &value);
	if (!(speed & NGBE_LINK_SPEED_100_FULL))
		value &= 0xfeff;
	else
		value |= 0x100;
	hw->phy.ops.write_reg(hw, 4, 0, value);

	hw->phy.ops.read_reg(hw, 4, 0, &value);
	if (!(speed & NGBE_LINK_SPEED_10_FULL))
		value &= 0xffbf;
	else
		value |= 0x40;
	hw->phy.ops.write_reg(hw, 4, 0, value);

	/* Restart AN and wait AN done interrupt */
	value = NGBE_MDI_PHY_RESTART_AN | NGBE_MDI_PHY_ANE;
	hw->phy.ops.write_reg(hw, 0, 0, value);

skip_an:
	hw->phy.ops.phy_led_ctrl(sc);
	hw->phy.ops.check_event(sc);
	
	return 0;
}

uint16_t
ngbe_read_pci_cfg_word(struct ngbe_softc *sc, uint32_t reg)
{
	struct ngbe_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;
	uint32_t value;
	int high = 0;

	if (reg & 0x2) {
		high = 1;
		reg &= ~0x2;
	}
	value = pci_conf_read(pa->pa_pc, pa->pa_tag, reg);

	if (high)
		value >>= 16;

	return (value & 0xffff);
}

void
ngbe_release_eeprom_semaphore(struct ngbe_hw *hw)
{
	if (ngbe_check_mng_access(hw)) {
		NGBE_WRITE_REG_MASK(hw, NGBE_MIS_SWSM, NGBE_MIS_SWSM_SMBI, 0);
		NGBE_WRITE_FLUSH(hw);
	}
}

int
ngbe_acquire_swfw_sync(struct ngbe_softc *sc, uint32_t mask)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t gssr = 0;
	uint32_t swmask = mask;
	uint32_t fwmask = mask << 16;
	int i, timeout = 200;

	for (i = 0; i < timeout; i++) {
		/*
		 * SW NVM semaphore bit is used for access to all
		 * SW_FW_SYNC bits (not just NVM)
		 */
		if (ngbe_get_eeprom_semaphore(sc))
			return 1;
		if (ngbe_check_mng_access(hw)) {
			gssr = NGBE_READ_REG(hw, NGBE_MNG_SWFW_SYNC);
			if (!(gssr & (fwmask | swmask))) {
				gssr |= swmask;
				NGBE_WRITE_REG(hw, NGBE_MNG_SWFW_SYNC, gssr);
				ngbe_release_eeprom_semaphore(hw);
				return 0;
			} else {
				/* Resource is currently in use by FW or SW */
				ngbe_release_eeprom_semaphore(hw);
				msec_delay(5);
			}
		}
	}

	printf("%s: semaphore failed\n", DEVNAME(sc));

	/* If time expired clear the bits holding the lock and retry */
	if (gssr & (fwmask | swmask))
		ngbe_release_swfw_sync(sc, gssr & (fwmask | swmask));

	msec_delay(5);
	return 1;
}

void
ngbe_release_swfw_sync(struct ngbe_softc *sc, uint32_t mask)
{
	struct ngbe_hw *hw = &sc->hw;

	ngbe_get_eeprom_semaphore(sc);
	if (ngbe_check_mng_access(hw))
		NGBE_WRITE_REG_MASK(hw, NGBE_MNG_SWFW_SYNC, mask, 0);

	ngbe_release_eeprom_semaphore(hw);
}

void
ngbe_reset(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	int error;

	error = hw->mac.ops.init_hw(sc);
	switch (error) {
	case 0:
		break;
	default:
		printf("%s: hardware error\n", DEVNAME(sc));
		break;
	}
}

int
ngbe_reset_hw(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	struct ngbe_mac_info *mac = &hw->mac;
	uint32_t i, reset_status, rst_delay;
	uint32_t reset = 0;
	int status = 0;

	status = hw->mac.ops.stop_adapter(sc);
	if (status)
		goto reset_hw_out;

	/* Identify PHY and related function pointers */
	if (!((hw->subsystem_device_id & OEM_MASK) == RGMII_FPGA)) {
		status = hw->phy.ops.init(sc);
		if (status)
			goto reset_hw_out;
	}

	if (ngbe_get_media_type(hw) == ngbe_media_type_copper) {
		mac->ops.setup_link = ngbe_setup_copper_link;
		mac->ops.get_link_capabilities =
		    ngbe_get_copper_link_capabilities;
	}

	/*
	 * Issue global reset to the MAC.  Needs to be SW reset if link is up.
	 * If link reset is used when link is up, it might reset the PHY when
	 * mng is using it.  If link is down or the flag to force full link
	 * reset is set, then perform link reset.
	 */
	if (hw->force_full_reset) {
	 	rst_delay = (NGBE_READ_REG(hw, NGBE_MIS_RST_ST) &
		    NGBE_MIS_RST_ST_RST_INIT) >> NGBE_MIS_RST_ST_RST_INI_SHIFT;
		if (hw->reset_type == NGBE_SW_RESET) {
			for (i = 0; i < rst_delay + 20; i++) {
				reset_status =
				    NGBE_READ_REG(hw, NGBE_MIS_RST_ST);
				if (!(reset_status &
				    NGBE_MIS_RST_ST_DEV_RST_ST_MASK))
					break;
				msec_delay(100);
			}
			
			if (reset_status & NGBE_MIS_RST_ST_DEV_RST_ST_MASK) {
				status = ETIMEDOUT;
				printf("%s: software reset polling failed to "
				    "complete\n", DEVNAME(sc));
				goto reset_hw_out;
			}
			status = ngbe_check_flash_load(sc,
			    NGBE_SPI_ILDR_STATUS_SW_RESET);
			if (status)
				goto reset_hw_out;
		} else if (hw->reset_type == NGBE_GLOBAL_RESET) {
			msec_delay(100 * rst_delay + 2000);
		}
	} else {
		if (hw->bus.lan_id == 0)
			reset = NGBE_MIS_RST_LAN0_RST;
		else if (hw->bus.lan_id == 1)
			reset = NGBE_MIS_RST_LAN1_RST;
		else if (hw->bus.lan_id == 2)
			reset = NGBE_MIS_RST_LAN2_RST;
		else if (hw->bus.lan_id == 3)
			reset = NGBE_MIS_RST_LAN3_RST;

		NGBE_WRITE_REG(hw, NGBE_MIS_RST,
		    reset | NGBE_READ_REG(hw, NGBE_MIS_RST));
		NGBE_WRITE_FLUSH(hw);
		msec_delay(15);
	}

	ngbe_reset_misc(hw);

	/* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/*
	 * Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 32,
	 * since we modify this value when programming the SAN MAC address.
	 */
	hw->mac.num_rar_entries = NGBE_SP_RAR_ENTRIES;
	hw->mac.ops.init_rx_addrs(sc);

reset_hw_out:
	return status;
}

void
ngbe_reset_misc(struct ngbe_hw *hw)
{
	int i;

	/* Receive packets of size > 2048 */
	NGBE_WRITE_REG_MASK(hw, NGBE_MAC_RX_CFG, NGBE_MAC_RX_CFG_JE,
	    NGBE_MAC_RX_CFG_JE);

	/* Clear counters on read */
	NGBE_WRITE_REG_MASK(hw, NGBE_MMC_CONTROL, NGBE_MMC_CONTROL_RSTONRD,
	    NGBE_MMC_CONTROL_RSTONRD);

	NGBE_WRITE_REG_MASK(hw, NGBE_MAC_RX_FLOW_CTRL,
	    NGBE_MAC_RX_FLOW_CTRL_RFE, NGBE_MAC_RX_FLOW_CTRL_RFE);

	NGBE_WRITE_REG(hw, NGBE_MAC_PKT_FLT, NGBE_MAC_PKT_FLT_PR);

	NGBE_WRITE_REG_MASK(hw, NGBE_MIS_RST_ST, NGBE_MIS_RST_ST_RST_INIT,
	    0x1e00);

	/* errata 4: initialize mng flex tbl and wakeup flex tbl */
	NGBE_WRITE_REG(hw, NGBE_PSR_MNG_FLEX_SEL, 0);
	for (i = 0; i < 16; i++) {
		NGBE_WRITE_REG(hw, NGBE_PSR_MNG_FLEX_DW_L(i), 0);
		NGBE_WRITE_REG(hw, NGBE_PSR_MNG_FLEX_DW_H(i), 0);
		NGBE_WRITE_REG(hw, NGBE_PSR_MNG_FLEX_MSK(i), 0);
	}
	NGBE_WRITE_REG(hw, NGBE_PSR_LAN_FLEX_SEL, 0);
	for (i = 0; i < 16; i++) {
		NGBE_WRITE_REG(hw, NGBE_PSR_LAN_FLEX_DW_L(i), 0);
		NGBE_WRITE_REG(hw, NGBE_PSR_LAN_FLEX_DW_H(i), 0);
		NGBE_WRITE_REG(hw, NGBE_PSR_LAN_FLEX_MSK(i), 0);
	}

	/* Set pause frame dst mac addr */
	NGBE_WRITE_REG(hw, NGBE_RDB_PFCMACDAL, 0xc2000001);
	NGBE_WRITE_REG(hw, NGBE_RDB_PFCMACDAH, 0x0180);

	NGBE_WRITE_REG(hw, NGBE_MDIO_CLAUSE_SELECT, 0xf);

	ngbe_init_thermal_sensor_thresh(hw);
}

int
ngbe_set_fw_drv_ver(struct ngbe_softc *sc, uint8_t maj, uint8_t min,
    uint8_t build, uint8_t sub)
{
	struct ngbe_hw *hw = &sc->hw;
	struct ngbe_hic_drv_info fw_cmd;
	int i, error = 0;

	fw_cmd.hdr.cmd = FW_CEM_CMD_DRIVER_INFO;
	fw_cmd.hdr.buf_len = FW_CEM_CMD_DRIVER_INFO_LEN;
	fw_cmd.hdr.cmd_or_resp.cmd_resv = FW_CEM_CMD_RESERVED;
	fw_cmd.port_num = (uint8_t)hw->bus.lan_id;
	fw_cmd.ver_maj = maj;
	fw_cmd.ver_min = min;
	fw_cmd.ver_build = build;
	fw_cmd.ver_sub = sub;
	fw_cmd.hdr.checksum = 0;
	fw_cmd.hdr.checksum = ngbe_calculate_checksum((uint8_t *)&fw_cmd,
	    (FW_CEM_HDR_LEN + fw_cmd.hdr.buf_len));
	fw_cmd.pad = 0;
	fw_cmd.pad2 = 0;

	DELAY(5000);
	for (i = 0; i <= FW_CEM_MAX_RETRIES; i++) {
		error = ngbe_host_interface_command(sc, (uint32_t *)&fw_cmd,
		    sizeof(fw_cmd), NGBE_HI_COMMAND_TIMEOUT, 1);
		if (error)
			continue;

		if (fw_cmd.hdr.cmd_or_resp.ret_status ==
		    FW_CEM_RESP_STATUS_SUCCESS)
			error = 0;
		else
			error = EINVAL;
		break;
	}

	return error;
}

void
ngbe_set_ivar(struct ngbe_softc *sc, uint16_t entry, uint16_t vector, int8_t
type)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t ivar, index;

	vector |= NGBE_PX_IVAR_ALLOC_VAL;

	if (type == -1) {
		/* other causes */
		index = 0;
		ivar = NGBE_READ_REG(hw, NGBE_PX_MISC_IVAR);
		ivar &= ~((uint32_t)0xff << index);
		ivar |= ((uint32_t)vector << index);
		NGBE_WRITE_REG(hw, NGBE_PX_MISC_IVAR, ivar);
	} else {
		/* Tx or Rx causes */
		index = ((16 * (entry & 1)) + (8 * type));
		ivar = NGBE_READ_REG(hw, NGBE_PX_IVAR(entry >> 1));
		ivar &= ~((uint32_t)0xff << index);
		ivar |= ((uint32_t)vector << index);
		NGBE_WRITE_REG(hw, NGBE_PX_IVAR(entry >> 1), ivar);
	}
}

void
ngbe_set_lan_id_multi_port_pcie(struct ngbe_hw *hw)
{
	struct ngbe_bus_info *bus = &hw->bus;
	uint32_t reg = 0;

	reg = NGBE_READ_REG(hw, NGBE_CFG_PORT_ST);
	bus->lan_id = NGBE_CFG_PORT_ST_LAN_ID(reg);
}

void
ngbe_set_mta(struct ngbe_hw *hw, uint8_t *mc_addr)
{
	uint32_t vector, vector_bit, vector_reg;

	hw->addr_ctrl.mta_in_use++;

	vector = ngbe_mta_vector(hw, mc_addr);

	/*
	 * The MTA is a register array of 128 32-bit registers. It is treated
	 * like an array of 4096 bits.  We want to set bit
	 * BitArray[vector_value]. So we figure out what register the bit is
	 * in, read it, OR in the new bit, then write back the new value.  The
	 * register is determined by the upper 7 bits of the vector value and
	 * the bit within that register are determined by the lower 5 bits of
	 * the value.
	 */
	vector_reg = (vector >> 5) & 0x7f;
	vector_bit = vector & 0x1f;
	hw->mac.mta_shadow[vector_reg] |= (1 << vector_bit);
}

void
ngbe_set_pci_config_data(struct ngbe_hw *hw, uint16_t link_status)
{
	if (hw->bus.type == ngbe_bus_type_unknown)
		hw->bus.type = ngbe_bus_type_pci_express;

	switch (link_status & NGBE_PCI_LINK_WIDTH) {
	case NGBE_PCI_LINK_WIDTH_1:
		hw->bus.width = ngbe_bus_width_pcie_x1;
		break;
	case NGBE_PCI_LINK_WIDTH_2:
		hw->bus.width = ngbe_bus_width_pcie_x2;
		break;
	case NGBE_PCI_LINK_WIDTH_4:
		hw->bus.width = ngbe_bus_width_pcie_x4;
		break;
	case NGBE_PCI_LINK_WIDTH_8:
		hw->bus.width = ngbe_bus_width_pcie_x8;
		break;
	default:
		hw->bus.width = ngbe_bus_width_unknown;
		break;
	}

	switch (link_status & NGBE_PCI_LINK_SPEED) {
	case NGBE_PCI_LINK_SPEED_2500:
		hw->bus.speed = ngbe_bus_speed_2500;
		break;
	case NGBE_PCI_LINK_SPEED_5000:
		hw->bus.speed = ngbe_bus_speed_5000;
		break;
	case NGBE_PCI_LINK_SPEED_8000:
		hw->bus.speed = ngbe_bus_speed_8000;
		break;
	default:
		hw->bus.speed = ngbe_bus_speed_unknown;
		break;
	}
}

int
ngbe_set_rar(struct ngbe_softc *sc, uint32_t index, uint8_t *addr,
    uint64_t pools, uint32_t enable_addr)
{
	struct ngbe_hw *hw = &sc->hw;
	uint32_t rar_entries = hw->mac.num_rar_entries;
	uint32_t rar_low, rar_high;

	/* Make sure we are using a valid rar index range */
	if (index >= rar_entries) {
		printf("%s: RAR index %d is out of range\n",
		    DEVNAME(sc), index);
		return EINVAL;
	}

	/* Select the MAC address */
	NGBE_WRITE_REG(hw, NGBE_PSR_MAC_SWC_IDX, index);

	/* Setup VMDq pool mapping */
	NGBE_WRITE_REG(hw, NGBE_PSR_MAC_SWC_VM, pools & 0xffffffff);

	/*
	 * HW expects these in little endian so we reverse the byte
	 * order from network order (big endian) to little endian
	 *
	 * Some parts put the VMDq setting in the extra RAH bits,
	 * so save everything except the lower 16 bits that hold part
	 * of the address and the address valid bit.
	 */
	rar_low = ((uint32_t)addr[5] | ((uint32_t)addr[4] << 8) |
	    ((uint32_t)addr[3] << 16) | ((uint32_t)addr[2] << 24));
	rar_high = ((uint32_t)addr[1] | ((uint32_t)addr[0] << 8));
	if (enable_addr != 0)
		rar_high |= NGBE_PSR_MAC_SWC_AD_H_AV;

	NGBE_WRITE_REG(hw, NGBE_PSR_MAC_SWC_AD_L, rar_low);
	NGBE_WRITE_REG_MASK(hw, NGBE_PSR_MAC_SWC_AD_H,
	    (NGBE_PSR_MAC_SWC_AD_H_AD(~0) | NGBE_PSR_MAC_SWC_AD_H_ADTYPE(~0) |
	    NGBE_PSR_MAC_SWC_AD_H_AV), rar_high);

	return 0;
}

void
ngbe_set_rx_drop_en(struct ngbe_softc *sc)
{
	uint32_t srrctl;
	int i;

	if ((sc->sc_nqueues > 1) &&
	    !(sc->hw.fc.current_mode & ngbe_fc_tx_pause)) {
		for (i = 0; i < sc->sc_nqueues; i++) {
			srrctl = NGBE_READ_REG(&sc->hw, NGBE_PX_RR_CFG(i));
			srrctl |= NGBE_PX_RR_CFG_DROP_EN;
			NGBE_WRITE_REG(&sc->hw, NGBE_PX_RR_CFG(i), srrctl);
		}
			
	} else {
		for (i = 0; i < sc->sc_nqueues; i++) {
			srrctl = NGBE_READ_REG(&sc->hw, NGBE_PX_RR_CFG(i));
			srrctl &= ~NGBE_PX_RR_CFG_DROP_EN;
			NGBE_WRITE_REG(&sc->hw, NGBE_PX_RR_CFG(i), srrctl);
		}
	}
}

void
ngbe_set_rxpba(struct ngbe_hw *hw, int num_pb, uint32_t headroom, int strategy)
{
	uint32_t pbsize = hw->mac.rx_pb_size;
	uint32_t txpktsize, txpbthresh, rxpktsize = 0;

	/* Reserve headroom */
	pbsize -= headroom;

	if (!num_pb)
		num_pb = 1;

	/*
	 * Divide remaining packet buffer space amongst the number of packet
	 * buffers requested using supplied strategy.
	 */
	switch (strategy) {
	case PBA_STRATEGY_EQUAL:
		rxpktsize = (pbsize / num_pb) << NGBE_RDB_PB_SZ_SHIFT;
		NGBE_WRITE_REG(hw, NGBE_RDB_PB_SZ, rxpktsize);
		break;
	default:
		break;
	}

	/* Only support an equally distributed Tx packet buffer strategy. */
	txpktsize = NGBE_TDB_PB_SZ_MAX / num_pb;
	txpbthresh = (txpktsize / 1024) - NGBE_TXPKT_SIZE_MAX;

	NGBE_WRITE_REG(hw, NGBE_TDB_PB_SZ, txpktsize);
	NGBE_WRITE_REG(hw, NGBE_TDM_PB_THRE, txpbthresh);
}

int
ngbe_setup_copper_link(struct ngbe_softc *sc, uint32_t speed, int need_restart)
{
	struct ngbe_hw *hw = &sc->hw;
	int status = 0;

	/* Setup the PHY according to input speed */
	if (!((hw->subsystem_device_id & OEM_MASK) == RGMII_FPGA))
		status = hw->phy.ops.setup_link(sc, speed, need_restart);

	return status;
}

int
ngbe_setup_fc(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	uint16_t pcap_backplane = 0;
	int error = 0;

	/* Validate the requested mode */
	if (hw->fc.strict_ieee && hw->fc.requested_mode == ngbe_fc_rx_pause) {
		printf("%s: ngbe_fc_rx_pause not valid in strict IEEE mode\n",
		    DEVNAME(sc));
		error = EINVAL;
		goto out;
	}

	/*
	 * Gig parts do not have a word in the EEPROM to determine the
	 * default flow control setting, so we explicitly set it to full.
	 */
	if (hw->fc.requested_mode == ngbe_fc_default)
		hw->fc.requested_mode = ngbe_fc_full;

	/*
	 * The possible values of fc.requested_mode are:
	 * 0: Flow control is completely disabled
	 * 1: Rx flow control is enabled (we can receive pause frames,
	 *    but not send pause frames).
	 * 2: Tx flow control is enabled (we can send pause frames but
	 *    we do not support receiving pause frames).
	 * 3: Both Rx and Tx flow control (symmetric) are enabled.
	 * other: Invalid.
	 */
	switch (hw->fc.requested_mode) {
	case ngbe_fc_none:
		/* Flow control completely disabled by software override. */
		break;
	case ngbe_fc_tx_pause:
		/* 
		 * Tx Flow control is enabled, and Rx Flow control is
		 * disabled by software override.
		 */
		pcap_backplane |= NGBE_SR_AN_MMD_ADV_REG1_PAUSE_ASM;
		break;
	case ngbe_fc_rx_pause:
		/*
		 * Rx Flow control is enabled and Tx Flow control is
		 * disabled by software override. Since there really
		 * isn't a way to advertise that we are capable of RX
		 * Pause ONLY, we will advertise that we support both
		 * symmetric and asymmetric Rx PAUSE, as such we fall
		 * through to the fc_full statement.  Later, we will
		 * disable the adapter's ability to send PAUSE frames.
		 */
	case ngbe_fc_full:
		/* Flow control (both Rx and Tx) is enabled by SW override. */
		pcap_backplane |= NGBE_SR_AN_MMD_ADV_REG1_PAUSE_SYM |
		    NGBE_SR_AN_MMD_ADV_REG1_PAUSE_ASM;
		break;
	default:
		printf("%s: flow control param set incorrectly\n", DEVNAME(sc));
		error = EINVAL;
		goto out;
	}

	/* AUTOC restart handles negotiation of 1G on backplane and copper. */
	if ((hw->phy.media_type == ngbe_media_type_copper) &&
	    !((hw->subsystem_device_id & OEM_MASK) == RGMII_FPGA))
		error = hw->phy.ops.set_adv_pause(hw, pcap_backplane);
out:
	return error;
}

void
ngbe_setup_gpie(struct ngbe_hw *hw)
{
	uint32_t gpie;

	gpie = NGBE_PX_GPIE_MODEL;

	/*
	 * use EIAM to auto-mask when MSI-X interrupt is asserted
	 * this saves a register write for every interrupt.
	 */
	NGBE_WRITE_REG(hw, NGBE_PX_GPIE, gpie);
}

void
ngbe_setup_isb(struct ngbe_softc *sc)
{
	uint64_t idba = sc->isbdma.dma_map->dm_segs[0].ds_addr;

	/* Set ISB address */
	NGBE_WRITE_REG(&sc->hw, NGBE_PX_ISB_ADDR_L,
	    (idba & 0x00000000ffffffffULL));
	NGBE_WRITE_REG(&sc->hw, NGBE_PX_ISB_ADDR_H, (idba >> 32));
}

void
ngbe_setup_psrtype(struct ngbe_hw *hw)
{
	uint32_t psrtype;

	/* PSRTYPE must be initialized in adapters */
	psrtype = NGBE_RDB_PL_CFG_L4HDR | NGBE_RDB_PL_CFG_L3HDR |
	    NGBE_RDB_PL_CFG_L2HDR | NGBE_RDB_PL_CFG_TUN_TUNHDR |
	    NGBE_RDB_PL_CFG_TUN_OUTER_L2HDR;

	NGBE_WRITE_REG(hw, NGBE_RDB_PL_CFG(0), psrtype);
}

void
ngbe_setup_vlan_hw_support(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	int i;

	for (i = 0; i < sc->sc_nqueues; i++) {
		NGBE_WRITE_REG_MASK(hw, NGBE_PX_RR_CFG(i),
		    NGBE_PX_RR_CFG_VLAN, NGBE_PX_RR_CFG_VLAN);
	}
}

int
ngbe_start_hw(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	int error;

	/* Set the media type */
	hw->phy.media_type = hw->mac.ops.get_media_type(hw);

	/* Clear the VLAN filter table */
	hw->mac.ops.clear_vfta(hw);

	/* Clear statistics registers */
	hw->mac.ops.clear_hw_cntrs(hw);

	NGBE_WRITE_FLUSH(hw);

	/* Setup flow control */
	error = hw->mac.ops.setup_fc(sc);

	/* Clear adapter stopped flag */
	hw->adapter_stopped = 0;

	/* We need to run link autotry after the driver loads */
	hw->mac.autotry_restart = 1;

	return error;
}

int
ngbe_stop_adapter(struct ngbe_softc *sc)
{
	struct ngbe_hw *hw = &sc->hw;
	int i;

	/*
	 * Set the adapter_stopped flag so other driver functions stop touching
	 * the hardware.
	 */
	hw->adapter_stopped = 1;

	/* Disable the receive unit. */
	hw->mac.ops.disable_rx(hw);

	/* Clear any pending interrupts, flush previous writes. */
	NGBE_WRITE_REG(hw, NGBE_PX_MISC_IC, 0xffffffff);

	NGBE_WRITE_REG(hw, NGBE_BME_CTL, 0x3);

	/* Disable the transmit unit.  Each queue must be disabled. */
	for (i = 0; i < hw->mac.max_tx_queues; i++) {
		NGBE_WRITE_REG_MASK(hw, NGBE_PX_TR_CFG(i),
		    NGBE_PX_TR_CFG_SWFLSH | NGBE_PX_TR_CFG_ENABLE,
		    NGBE_PX_TR_CFG_SWFLSH);
	}

	/* Disable the receive unit by stopping each queue */
	for (i = 0; i < hw->mac.max_rx_queues; i++) {
		NGBE_WRITE_REG_MASK(hw, NGBE_PX_RR_CFG(i),
		    NGBE_PX_RR_CFG_RR_EN, 0);
	}

	/* Flush all queues disables. */
	NGBE_WRITE_FLUSH(hw);
	msec_delay(2);

	return ngbe_disable_pcie_master(sc);
}

void
ngbe_rx_checksum(uint32_t staterr, struct mbuf *m)
{
	if (staterr & NGBE_RXD_STAT_IPCS) {
		if (!(staterr & NGBE_RXD_ERR_IPE))
			m->m_pkthdr.csum_flags = M_IPV4_CSUM_IN_OK;
		else
			m->m_pkthdr.csum_flags = 0;
	}
	if (staterr & NGBE_RXD_STAT_L4CS) {
		if (!(staterr & NGBE_RXD_ERR_TCPE))
			m->m_pkthdr.csum_flags |=
			    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
	}
}

void
ngbe_rxeof(struct rx_ring *rxr)
{
	struct ngbe_softc *sc = rxr->sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *mp, *m;
	struct ngbe_rx_buf *rxbuf, *nxbuf;
	union ngbe_rx_desc *rxdesc;
	uint32_t staterr = 0;
	uint16_t len, vtag;
	uint8_t eop = 0;
	int i, nextp;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return;

	i = rxr->next_to_check;
	while (if_rxr_inuse(&rxr->rx_ring) > 0) {
		uint32_t hash;
		uint16_t hashtype;

		bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
		    i * sizeof(union ngbe_rx_desc), sizeof(union ngbe_rx_desc),
		    BUS_DMASYNC_POSTREAD);

		rxdesc = &rxr->rx_base[i];
		staterr = letoh32(rxdesc->wb.upper.status_error);
		if (!ISSET(staterr, NGBE_RXD_STAT_DD)) {
			bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
			    i * sizeof(union ngbe_rx_desc),
			    sizeof(union ngbe_rx_desc), BUS_DMASYNC_PREREAD);
			break;
		}

		/* Zero out the receive descriptors status. */
		rxdesc->wb.upper.status_error = 0;
		rxbuf = &rxr->rx_buffers[i];

		/* Pull the mbuf off the ring. */
		bus_dmamap_sync(rxr->rxdma.dma_tag, rxbuf->map, 0,
		    rxbuf->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(rxr->rxdma.dma_tag, rxbuf->map);

		mp = rxbuf->buf;
		len = letoh16(rxdesc->wb.upper.length);
		vtag = letoh16(rxdesc->wb.upper.vlan);
		eop = ((staterr & NGBE_RXD_STAT_EOP) != 0);
		hash = letoh32(rxdesc->wb.lower.hi_dword.rss);
		hashtype = le16toh(rxdesc->wb.lower.lo_dword.hs_rss.pkt_info) &
		    NGBE_RXD_RSSTYPE_MASK;

		if (staterr & NGBE_RXD_ERR_RXE) {
			if (rxbuf->fmp) {
				m_freem(rxbuf->fmp);
				rxbuf->fmp = NULL;
			}

			m_freem(mp);
			rxbuf->buf = NULL;
			goto next_desc;
		}

		if (mp == NULL) {
			panic("%s: ngbe_rxeof: NULL mbuf in slot %d "
			    "(nrx %d, filled %d)", DEVNAME(sc), i,
			    if_rxr_inuse(&rxr->rx_ring), rxr->last_desc_filled);
		}

		if (!eop) {
			/*
			 * Figure out the next descriptor of this frame.
			 */
			nextp = i + 1;
			if (nextp == sc->num_rx_desc)
				nextp = 0;
			nxbuf = &rxr->rx_buffers[nextp];
			/* prefetch(nxbuf); */
		}

		mp->m_len = len;

		m = rxbuf->fmp;
		rxbuf->buf = rxbuf->fmp = NULL;

		if (m != NULL)
			m->m_pkthdr.len += mp->m_len;
		else {
			m = mp;
			m->m_pkthdr.len = mp->m_len;
#if NVLAN > 0
			if (staterr & NGBE_RXD_STAT_VP) {
				m->m_pkthdr.ether_vtag = vtag;
				m->m_flags |= M_VLANTAG;
			}
#endif
		}

		/* Pass the head pointer on */
		if (eop == 0) {
			nxbuf->fmp = m;
			m = NULL;
			mp->m_next = nxbuf->buf;
		} else {
			ngbe_rx_checksum(staterr, m);

			if (hashtype != NGBE_RXD_RSSTYPE_NONE) {
				m->m_pkthdr.ph_flowid = hash;
				SET(m->m_pkthdr.csum_flags, M_FLOWID);
			}

			ml_enqueue(&ml, m);
		}
next_desc:
		if_rxr_put(&rxr->rx_ring, 1);
		bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
		    i * sizeof(union ngbe_rx_desc), sizeof(union ngbe_rx_desc),
		    BUS_DMASYNC_PREREAD);

		/* Advance our pointers to the next descriptor. */
		if (++i == sc->num_rx_desc)
			i = 0;
	}
	rxr->next_to_check = i;

	if (ifiq_input(rxr->ifiq, &ml))
		if_rxr_livelocked(&rxr->rx_ring);

	if (!(staterr & NGBE_RXD_STAT_DD))
		return;
}

void
ngbe_rxrefill(void *xrxr)
{
	struct rx_ring *rxr = xrxr;
	struct ngbe_softc *sc = rxr->sc;

	if (ngbe_rxfill(rxr))
		NGBE_WRITE_REG(&sc->hw, NGBE_PX_RR_WP(rxr->me),
		    rxr->last_desc_filled);
	else if (if_rxr_inuse(&rxr->rx_ring) == 0)
		timeout_add(&rxr->rx_refill, 1);
}

int
ngbe_tx_ctx_setup(struct tx_ring *txr, struct mbuf *m, uint32_t *cmd_type_len,
    uint32_t *olinfo_status)
{
	struct ngbe_tx_context_desc *txd;
	struct ngbe_tx_buf *tx_buffer;
	uint32_t vlan_macip_lens = 0, type_tucmd_mlhl = 0;
	int ctxd = txr->next_avail_desc;
	int offload = 0;

	/* Indicate the whole packet as payload when not doing TSO */
	*olinfo_status |= m->m_pkthdr.len << NGBE_TXD_PAYLEN_SHIFT;

#if NVLAN > 0
	if (ISSET(m->m_flags, M_VLANTAG)) {
		uint32_t vtag = m->m_pkthdr.ether_vtag;
		vlan_macip_lens |= (vtag << NGBE_TXD_VLAN_SHIFT);
		*cmd_type_len |= NGBE_TXD_VLE;
		offload |= 1;
	}
#endif

	if (!offload)
		return 0;

	txd = (struct ngbe_tx_context_desc *)&txr->tx_base[ctxd];
	tx_buffer = &txr->tx_buffers[ctxd];

	type_tucmd_mlhl |= NGBE_TXD_DTYP_CTXT;

	/* Now copy bits into descriptor */
	txd->vlan_macip_lens = htole32(vlan_macip_lens);
	txd->type_tucmd_mlhl = htole32(type_tucmd_mlhl);
	txd->seqnum_seed = htole32(0);
	txd->mss_l4len_idx = htole32(0);

	tx_buffer->m_head = NULL;
	tx_buffer->eop_index = -1;

	return 1;
}

void
ngbe_txeof(struct tx_ring *txr)
{
	struct ngbe_softc *sc = txr->sc;
	struct ifqueue *ifq = txr->ifq;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ngbe_tx_buf *tx_buffer;
	union ngbe_tx_desc *tx_desc;
	unsigned int prod, cons, last;
	int done = 0;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return;

	prod = txr->next_avail_desc;
	cons = txr->next_to_clean;

	if (prod == cons)
		return;

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map, 0,
	    txr->txdma.dma_map->dm_mapsize, BUS_DMASYNC_POSTREAD);

	for (;;) {
		tx_buffer = &txr->tx_buffers[cons];
		last = tx_buffer->eop_index;
		tx_desc = (union ngbe_tx_desc *)&txr->tx_base[last];

		if (!ISSET(tx_desc->wb.status, NGBE_TXD_STAT_DD))
			break;

		bus_dmamap_sync(txr->txdma.dma_tag, tx_buffer->map,
		    0, tx_buffer->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(txr->txdma.dma_tag, tx_buffer->map);
		m_freem(tx_buffer->m_head);
		done = 1;

		tx_buffer->m_head = NULL;
		tx_buffer->eop_index = -1;

		cons = last + 1;
		if (cons == sc->num_tx_desc)
			cons = 0;
		if (prod == cons) {
			/* All clean, turn off the timer */
			ifp->if_timer = 0;
			break;
		}
	}

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map,
	    0, txr->txdma.dma_map->dm_mapsize, BUS_DMASYNC_PREREAD);

	txr->next_to_clean = cons;

	if (done && ifq_is_oactive(ifq))
		ifq_restart(ifq);
}

void
ngbe_update_mc_addr_list(struct ngbe_hw *hw, uint8_t *mc_addr_list,
    uint32_t mc_addr_count, ngbe_mc_addr_itr next, int clear)
{
	uint32_t i, psrctl, vmdq;

	/*
	 * Set the new number of MC addresses that we are being requested to
	 * use.
	 */
	hw->addr_ctrl.num_mc_addrs = mc_addr_count;
	hw->addr_ctrl.mta_in_use = 0;

	/* Clear mta_shadow */
	if (clear)
		memset(&hw->mac.mta_shadow, 0, sizeof(hw->mac.mta_shadow));

	/* Update mta_shadow */
	for (i = 0; i < mc_addr_count; i++)
		ngbe_set_mta(hw, next(hw, &mc_addr_list, &vmdq));

	/* Enable mta */
	for (i = 0; i < hw->mac.mcft_size; i++)
		NGBE_WRITE_REG_ARRAY(hw, NGBE_PSR_MC_TBL(0), i,
		    hw->mac.mta_shadow[i]);

	if (hw->addr_ctrl.mta_in_use > 0) {
		psrctl = NGBE_READ_REG(hw, NGBE_PSR_CTL);
		psrctl &= ~(NGBE_PSR_CTL_MO | NGBE_PSR_CTL_MFE);
		psrctl |= NGBE_PSR_CTL_MFE |
		    (hw->mac.mc_filter_type << NGBE_PSR_CTL_MO_SHIFT);
		NGBE_WRITE_REG(hw, NGBE_PSR_CTL, psrctl);
	}
}

int
ngbe_validate_mac_addr(uint8_t *mac_addr)
{
	uint32_t status = 0;

	/* Make sure it is not a multicast address */
	if (NGBE_IS_MULTICAST(mac_addr))
		status = EINVAL;
	/* Not a broadcast address */
	else if (NGBE_IS_BROADCAST(mac_addr))
		status = EINVAL;
	/* Reject the zero address */
	else if (mac_addr[0] == 0 && mac_addr[1] == 0 && mac_addr[2] == 0 &&
	    mac_addr[3] == 0 && mac_addr[4] == 0 && mac_addr[5] == 0)
		status = EINVAL;

	return status;
}
