/*	$OpenBSD: fxp.c,v 1.135 2024/08/31 16:23:09 deraadt Exp $	*/
/*	$NetBSD: if_fxp.c,v 1.2 1997/06/05 02:01:55 thorpej Exp $	*/

/*
 * Copyright (c) 1995, David Greenman
 * All rights reserved.
 *
 * Modifications to support NetBSD:
 * Copyright (c) 1997 Jason R. Thorpe.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *	Id: if_fxp.c,v 1.55 1998/08/04 08:53:12 dg Exp
 */

/*
 * Intel EtherExpress Pro/100B PCI Fast Ethernet driver
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>

#include <netinet/if_ether.h>

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/miivar.h>

#include <dev/ic/fxpreg.h>
#include <dev/ic/fxpvar.h>

/*
 * NOTE!  On the Alpha, we have an alignment constraint.  The
 * card DMAs the packet immediately following the RFA.  However,
 * the first thing in the packet is a 14-byte Ethernet header.
 * This means that the packet is misaligned.  To compensate,
 * we actually offset the RFA 2 bytes into the cluster.  This
 * aligns the packet after the Ethernet header at a 32-bit
 * boundary.  HOWEVER!  This means that the RFA is misaligned!
 */
#define	RFA_ALIGNMENT_FUDGE	(2 + sizeof(bus_dmamap_t *))

/*
 * Inline function to copy a 16-bit aligned 32-bit quantity.
 */
static __inline void fxp_lwcopy(volatile u_int32_t *,
	volatile u_int32_t *);

static __inline void
fxp_lwcopy(volatile u_int32_t *src, volatile u_int32_t *dst)
{
	volatile u_int16_t *a = (u_int16_t *)src;
	volatile u_int16_t *b = (u_int16_t *)dst;

	b[0] = a[0];
	b[1] = a[1];
}

/*
 * Template for default configuration parameters.
 * See struct fxp_cb_config for the bit definitions.
 * Note, cb_command is filled in later.
 */
static u_char fxp_cb_config_template[] = {
	0x0, 0x0,		/* cb_status */
	0x0, 0x0,		/* cb_command */
	0xff, 0xff, 0xff, 0xff,	/* link_addr */
	0x16,	/*  0 Byte count. */
	0x08,	/*  1 Fifo limit */
	0x00,	/*  2 Adaptive ifs */
	0x00,	/*  3 ctrl0 */
	0x00,	/*  4 rx_dma_bytecount */
	0x80,	/*  5 tx_dma_bytecount */
	0xb2,	/*  6 ctrl 1*/
	0x03,	/*  7 ctrl 2*/
	0x01,	/*  8 mediatype */
	0x00,	/*  9 void2 */
	0x26,	/* 10 ctrl3 */
	0x00,	/* 11 linear priority */
	0x60,	/* 12 interfrm_spacing */
	0x00,	/* 13 void31 */
	0xf2,	/* 14 void32 */
	0x48,	/* 15 promiscuous */
	0x00,	/* 16 void41 */
	0x40,	/* 17 void42 */
	0xf3,	/* 18 stripping */
	0x00,	/* 19 fdx_pin */
	0x3f,	/* 20 multi_ia */
	0x05	/* 21 mc_all */
};

void fxp_eeprom_shiftin(struct fxp_softc *, int, int);
void fxp_eeprom_putword(struct fxp_softc *, int, u_int16_t);
void fxp_write_eeprom(struct fxp_softc *, u_short *, int, int);
int fxp_mediachange(struct ifnet *);
void fxp_mediastatus(struct ifnet *, struct ifmediareq *);
void fxp_scb_wait(struct fxp_softc *);
void fxp_start(struct ifnet *);
int fxp_ioctl(struct ifnet *, u_long, caddr_t);
void fxp_load_ucode(struct fxp_softc *);
void fxp_watchdog(struct ifnet *);
int fxp_add_rfabuf(struct fxp_softc *, struct mbuf *);
int fxp_mdi_read(struct device *, int, int);
void fxp_mdi_write(struct device *, int, int, int);
void fxp_autosize_eeprom(struct fxp_softc *);
void fxp_statchg(struct device *);
void fxp_read_eeprom(struct fxp_softc *, u_int16_t *,
				    int, int);
void fxp_stats_update(void *);
void fxp_mc_setup(struct fxp_softc *, int);
void fxp_scb_cmd(struct fxp_softc *, u_int16_t);

/*
 * Set initial transmit threshold at 64 (512 bytes). This is
 * increased by 64 (512 bytes) at a time, to maximum of 192
 * (1536 bytes), if an underrun occurs.
 */
static int tx_threshold = 64;

/*
 * Interrupts coalescing code params
 */
int fxp_int_delay = FXP_INT_DELAY;
int fxp_bundle_max = FXP_BUNDLE_MAX;
int fxp_min_size_mask = FXP_MIN_SIZE_MASK;

/*
 * TxCB list index mask. This is used to do list wrap-around.
 */
#define FXP_TXCB_MASK	(FXP_NTXCB - 1)

/*
 * Maximum number of seconds that the receiver can be idle before we
 * assume it's dead and attempt to reset it by reprogramming the
 * multicast filter. This is part of a work-around for a bug in the
 * NIC. See fxp_stats_update().
 */
#define FXP_MAX_RX_IDLE	15

/*
 * Wait for the previous command to be accepted (but not necessarily
 * completed).
 */
void
fxp_scb_wait(struct fxp_softc *sc)
{
	int i = FXP_CMD_TMO;

	while ((CSR_READ_2(sc, FXP_CSR_SCB_COMMAND) & 0xff) && --i)
		DELAY(2);
	if (i == 0)
		printf("%s: warning: SCB timed out\n", sc->sc_dev.dv_xname);
}

void
fxp_eeprom_shiftin(struct fxp_softc *sc, int data, int length)
{
	u_int16_t reg;
	int x;

	/*
	 * Shift in data.
	 */
	for (x = 1 << (length - 1); x; x >>= 1) {
		if (data & x)
			reg = FXP_EEPROM_EECS | FXP_EEPROM_EEDI;
		else
			reg = FXP_EEPROM_EECS;
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
		DELAY(1);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg | FXP_EEPROM_EESK);
		DELAY(1);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
		DELAY(1);
	}
}

void
fxp_eeprom_putword(struct fxp_softc *sc, int offset, u_int16_t data)
{
	int i;

	/*
	 * Erase/write enable.
	 */
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	fxp_eeprom_shiftin(sc, 0x4, 3);
	fxp_eeprom_shiftin(sc, 0x03 << (sc->eeprom_size - 2), sc->eeprom_size);
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(1);
	/*
	 * Shift in write opcode, address, data.
	 */
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	fxp_eeprom_shiftin(sc, FXP_EEPROM_OPC_WRITE, 3);
	fxp_eeprom_shiftin(sc, offset, sc->eeprom_size);
	fxp_eeprom_shiftin(sc, data, 16);
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(1);
	/*
	 * Wait for EEPROM to finish up.
	 */
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	DELAY(1);
	for (i = 0; i < 1000; i++) {
		if (CSR_READ_2(sc, FXP_CSR_EEPROMCONTROL) & FXP_EEPROM_EEDO)
			break;
		DELAY(50);
	}
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(1);
	/*
	 * Erase/write disable.
	 */
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	fxp_eeprom_shiftin(sc, 0x4, 3);
	fxp_eeprom_shiftin(sc, 0, sc->eeprom_size);
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(1);
}

void
fxp_write_eeprom(struct fxp_softc *sc, u_short *data, int offset, int words)
{
	int i;

	for (i = 0; i < words; i++)
		fxp_eeprom_putword(sc, offset + i, data[i]);
}

/*************************************************************
 * Operating system-specific autoconfiguration glue
 *************************************************************/

struct cfdriver fxp_cd = {
	NULL, "fxp", DV_IFNET
};

int
fxp_activate(struct device *self, int act)
{
	struct fxp_softc *sc = (struct fxp_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;	

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			fxp_stop(sc, 1, 0);
		break;
	case DVACT_WAKEUP:
		if (ifp->if_flags & IFF_UP)
			fxp_wakeup(sc);
		break;
	}
	return (0);
}

void
fxp_wakeup(struct fxp_softc *sc)
{
	int s = splnet();

	/* force reload of the microcode */
	sc->sc_flags &= ~FXPF_UCODELOADED;

	fxp_init(sc);
	splx(s);
}

/*************************************************************
 * End of operating system-specific autoconfiguration glue
 *************************************************************/

/*
 * Do generic parts of attach.
 */
int
fxp_attach(struct fxp_softc *sc, const char *intrstr)
{
	struct ifnet *ifp;
	struct mbuf *m;
	bus_dmamap_t rxmap;
	u_int16_t data;
	u_int8_t enaddr[6];
	int i, err;

	/*
	 * Reset to a stable state.
	 */
	CSR_WRITE_4(sc, FXP_CSR_PORT, FXP_PORT_SOFTWARE_RESET);
	DELAY(10);

	if (bus_dmamem_alloc(sc->sc_dmat, sizeof(struct fxp_ctrl),
	    PAGE_SIZE, 0, &sc->sc_cb_seg, 1, &sc->sc_cb_nseg,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO))
		goto fail;
	if (bus_dmamem_map(sc->sc_dmat, &sc->sc_cb_seg, sc->sc_cb_nseg,
	    sizeof(struct fxp_ctrl), (caddr_t *)&sc->sc_ctrl,
	    BUS_DMA_NOWAIT)) {
		bus_dmamem_free(sc->sc_dmat, &sc->sc_cb_seg, sc->sc_cb_nseg);
		goto fail;
	}
	if (bus_dmamap_create(sc->sc_dmat, sizeof(struct fxp_ctrl),
	    1, sizeof(struct fxp_ctrl), 0, BUS_DMA_NOWAIT,
	    &sc->tx_cb_map)) {
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_ctrl,
		    sizeof(struct fxp_ctrl));
		bus_dmamem_free(sc->sc_dmat, &sc->sc_cb_seg, sc->sc_cb_nseg);
		goto fail;
	}
	if (bus_dmamap_load(sc->sc_dmat, sc->tx_cb_map, (caddr_t)sc->sc_ctrl,
	    sizeof(struct fxp_ctrl), NULL, BUS_DMA_NOWAIT)) {
		bus_dmamap_destroy(sc->sc_dmat, sc->tx_cb_map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_ctrl,
		    sizeof(struct fxp_ctrl));
		bus_dmamem_free(sc->sc_dmat, &sc->sc_cb_seg, sc->sc_cb_nseg);
		goto fail;
	}

	for (i = 0; i < FXP_NTXCB; i++) {
		if ((err = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    FXP_NTXSEG, MCLBYTES, 0, 0, &sc->txs[i].tx_map)) != 0) {
			printf("%s: unable to create tx dma map %d, error %d\n",
			    sc->sc_dev.dv_xname, i, err);
			goto fail;
		}
		sc->txs[i].tx_mbuf = NULL;
		sc->txs[i].tx_cb = sc->sc_ctrl->tx_cb + i;
		sc->txs[i].tx_off = offsetof(struct fxp_ctrl, tx_cb[i]);
		sc->txs[i].tx_next = &sc->txs[(i + 1) & FXP_TXCB_MASK];
	}

	/*
	 * Pre-allocate some receive buffers.
	 */
	sc->sc_rxfree = 0;
	for (i = 0; i < FXP_NRFABUFS_MIN; i++) {
		if ((err = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, 0, &sc->sc_rxmaps[i])) != 0) {
			printf("%s: unable to create rx dma map %d, error %d\n",
			    sc->sc_dev.dv_xname, i, err);
			goto fail;
		}
		sc->rx_bufs++;
	}
	for (i = 0; i < FXP_NRFABUFS_MIN; i++)
		if (fxp_add_rfabuf(sc, NULL) != 0)
			goto fail;

	/*
	 * Find out how large of an SEEPROM we have.
	 */
	fxp_autosize_eeprom(sc);

	/*
	 * Get info about the primary PHY
	 */
	fxp_read_eeprom(sc, (u_int16_t *)&data, FXP_EEPROM_REG_PHY, 1);
	sc->phy_primary_addr = data & 0xff;
	sc->phy_primary_device = (data >> 8) & 0x3f;
	sc->phy_10Mbps_only = data >> 15;

	/*
	 * Only 82558 and newer cards can do this.
	 */
	if (sc->sc_revision >= FXP_REV_82558_A4) {
		sc->sc_int_delay = fxp_int_delay;
		sc->sc_bundle_max = fxp_bundle_max;
		sc->sc_min_size_mask = fxp_min_size_mask;
	}
	/*
	 * Read MAC address.
	 */
	fxp_read_eeprom(sc, (u_int16_t *)enaddr, FXP_EEPROM_REG_MAC, 3);

	ifp = &sc->sc_arpcom.ac_if;
	bcopy(enaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = fxp_ioctl;
	ifp->if_start = fxp_start;
	ifp->if_watchdog = fxp_watchdog;
	ifq_init_maxlen(&ifp->if_snd, FXP_NTXCB - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	printf(": %s, address %s\n", intrstr,
	    ether_sprintf(sc->sc_arpcom.ac_enaddr));

	if (sc->sc_flags & FXPF_DISABLE_STANDBY) {
		fxp_read_eeprom(sc, &data, FXP_EEPROM_REG_ID, 1);
		if (data & FXP_EEPROM_REG_ID_STB) {
			u_int16_t cksum;

			printf("%s: Disabling dynamic standby mode in EEPROM",
			    sc->sc_dev.dv_xname);
			data &= ~FXP_EEPROM_REG_ID_STB;
			fxp_write_eeprom(sc, &data, FXP_EEPROM_REG_ID, 1);
			printf(", New ID 0x%x", data);
			cksum = 0;
			for (i = 0; i < (1 << sc->eeprom_size) - 1; i++) {
				fxp_read_eeprom(sc, &data, i, 1);
				cksum += data;
			}
			i = (1 << sc->eeprom_size) - 1;
			cksum = 0xBABA - cksum;
			fxp_read_eeprom(sc, &data, i, 1);
			fxp_write_eeprom(sc, &cksum, i, 1);
			printf(", cksum @ 0x%x: 0x%x -> 0x%x\n",
			    i, data, cksum);
		}
	}

	/* Receiver lock-up workaround detection. */
	fxp_read_eeprom(sc, &data, FXP_EEPROM_REG_COMPAT, 1);
	if ((data & (FXP_EEPROM_REG_COMPAT_MC10|FXP_EEPROM_REG_COMPAT_MC100))
	    != (FXP_EEPROM_REG_COMPAT_MC10|FXP_EEPROM_REG_COMPAT_MC100))
		sc->sc_flags |= FXPF_RECV_WORKAROUND;

	/*
	 * Initialize our media structures and probe the MII.
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = fxp_mdi_read;
	sc->sc_mii.mii_writereg = fxp_mdi_write;
	sc->sc_mii.mii_statchg = fxp_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, fxp_mediachange,
	    fxp_mediastatus);
	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, MIIF_NOISOLATE);
	/* If no phy found, just use auto mode */
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_MANUAL,
		    0, NULL);
		printf("%s: no phy found, using manual mode\n",
		    sc->sc_dev.dv_xname);
	}

	if (ifmedia_match(&sc->sc_mii.mii_media, IFM_ETHER|IFM_MANUAL, 0))
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_MANUAL);
	else if (ifmedia_match(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO, 0))
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);
	else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_10_T);

	/*
	 * Attach the interface.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	/*
	 * Initialize timeout for statistics update.
	 */
	timeout_set(&sc->stats_update_to, fxp_stats_update, sc);

	return (0);

 fail:
	printf("%s: Failed to malloc memory\n", sc->sc_dev.dv_xname);
	if (sc->tx_cb_map != NULL) {
		bus_dmamap_unload(sc->sc_dmat, sc->tx_cb_map);
		bus_dmamap_destroy(sc->sc_dmat, sc->tx_cb_map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_ctrl,
		    sizeof(struct fxp_cb_tx) * FXP_NTXCB);
		bus_dmamem_free(sc->sc_dmat, &sc->sc_cb_seg, sc->sc_cb_nseg);
	}
	m = sc->rfa_headm;
	while (m != NULL) {
		rxmap = *((bus_dmamap_t *)m->m_ext.ext_buf);
		bus_dmamap_unload(sc->sc_dmat, rxmap);
		FXP_RXMAP_PUT(sc, rxmap);
		m = m_free(m);
	}
	return (ENOMEM);
}

/*
 * From NetBSD:
 *
 * Figure out EEPROM size.
 *
 * 559's can have either 64-word or 256-word EEPROMs, the 558
 * datasheet only talks about 64-word EEPROMs, and the 557 datasheet
 * talks about the existence of 16 to 256 word EEPROMs.
 *
 * The only known sizes are 64 and 256, where the 256 version is used
 * by CardBus cards to store CIS information.
 *
 * The address is shifted in msb-to-lsb, and after the last
 * address-bit the EEPROM is supposed to output a `dummy zero' bit,
 * after which follows the actual data. We try to detect this zero, by
 * probing the data-out bit in the EEPROM control register just after
 * having shifted in a bit. If the bit is zero, we assume we've
 * shifted enough address bits. The data-out should be tri-state,
 * before this, which should translate to a logical one.
 *
 * Other ways to do this would be to try to read a register with known
 * contents with a varying number of address bits, but no such
 * register seem to be available. The high bits of register 10 are 01
 * on the 558 and 559, but apparently not on the 557.
 *
 * The Linux driver computes a checksum on the EEPROM data, but the
 * value of this checksum is not very well documented.
 */
void
fxp_autosize_eeprom(struct fxp_softc *sc)
{
	u_int16_t reg;
	int x;

	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	/*
	 * Shift in read opcode.
	 */
	for (x = 3; x > 0; x--) {
		if (FXP_EEPROM_OPC_READ & (1 << (x - 1))) {
			reg = FXP_EEPROM_EECS | FXP_EEPROM_EEDI;
		} else {
			reg = FXP_EEPROM_EECS;
		}
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL,
		    reg | FXP_EEPROM_EESK);
		DELAY(4);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
		DELAY(4);
	}
	/*
	 * Shift in address.
	 * Wait for the dummy zero following a correct address shift.
	 */
	for (x = 1; x <= 8; x++) {
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL,
			FXP_EEPROM_EECS | FXP_EEPROM_EESK);
		DELAY(4);
		if ((CSR_READ_2(sc, FXP_CSR_EEPROMCONTROL) & FXP_EEPROM_EEDO) == 0)
			break;
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
		DELAY(4);
	}
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(4);
	sc->eeprom_size = x;
}

/*
 * Read from the serial EEPROM. Basically, you manually shift in
 * the read opcode (one bit at a time) and then shift in the address,
 * and then you shift out the data (all of this one bit at a time).
 * The word size is 16 bits, so you have to provide the address for
 * every 16 bits of data.
 */
void
fxp_read_eeprom(struct fxp_softc *sc, u_short *data, int offset,
    int words)
{
	u_int16_t reg;
	int i, x;

	for (i = 0; i < words; i++) {
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
		/*
		 * Shift in read opcode.
		 */
		for (x = 3; x > 0; x--) {
			if (FXP_EEPROM_OPC_READ & (1 << (x - 1))) {
				reg = FXP_EEPROM_EECS | FXP_EEPROM_EEDI;
			} else {
				reg = FXP_EEPROM_EECS;
			}
			CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
			CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL,
			    reg | FXP_EEPROM_EESK);
			DELAY(4);
			CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
			DELAY(4);
		}
		/*
		 * Shift in address.
		 */
		for (x = sc->eeprom_size; x > 0; x--) {
			if ((i + offset) & (1 << (x - 1))) {
				reg = FXP_EEPROM_EECS | FXP_EEPROM_EEDI;
			} else {
				reg = FXP_EEPROM_EECS;
			}
			CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
			CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL,
			    reg | FXP_EEPROM_EESK);
			DELAY(4);
			CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
			DELAY(4);
		}
		reg = FXP_EEPROM_EECS;
		data[i] = 0;
		/*
		 * Shift out data.
		 */
		for (x = 16; x > 0; x--) {
			CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL,
			    reg | FXP_EEPROM_EESK);
			DELAY(4);
			if (CSR_READ_2(sc, FXP_CSR_EEPROMCONTROL) &
			    FXP_EEPROM_EEDO)
				data[i] |= (1 << (x - 1));
			CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
			DELAY(4);
		}
		data[i] = letoh16(data[i]);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
		DELAY(4);
	}
}

/*
 * Start packet transmission on the interface.
 */
void
fxp_start(struct ifnet *ifp)
{
	struct fxp_softc *sc = ifp->if_softc;
	struct fxp_txsw *txs = sc->sc_cbt_prod;
	struct fxp_cb_tx *txc;
	struct mbuf *m0;
	int cnt = sc->sc_cbt_cnt, seg, error;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	while (1) {
		if (cnt >= (FXP_NTXCB - 2)) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		txs = txs->tx_next;

		m0 = ifq_dequeue(&ifp->if_snd);
		if (m0 == NULL)
			break;

		error = bus_dmamap_load_mbuf(sc->sc_dmat, txs->tx_map,
		    m0, BUS_DMA_NOWAIT);
		switch (error) {
		case 0:
			break;
		case EFBIG:
			if (m_defrag(m0, M_DONTWAIT) == 0 &&
			    bus_dmamap_load_mbuf(sc->sc_dmat, txs->tx_map,
			    m0, BUS_DMA_NOWAIT) == 0)
				break;
			/* FALLTHROUGH */
		default:
			ifp->if_oerrors++;
			m_freem(m0);
			/* try next packet */
			continue;
		}

		txs->tx_mbuf = m0;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif

		FXP_MBUF_SYNC(sc, txs->tx_map, BUS_DMASYNC_PREWRITE);

		txc = txs->tx_cb;
		txc->tbd_number = txs->tx_map->dm_nsegs;
		txc->cb_status = 0;
		txc->cb_command = htole16(FXP_CB_COMMAND_XMIT | FXP_CB_COMMAND_SF);
		txc->tx_threshold = tx_threshold;
		for (seg = 0; seg < txs->tx_map->dm_nsegs; seg++) {
			txc->tbd[seg].tb_addr =
			    htole32(txs->tx_map->dm_segs[seg].ds_addr);
			txc->tbd[seg].tb_size =
			    htole32(txs->tx_map->dm_segs[seg].ds_len);
		}
		FXP_TXCB_SYNC(sc, txs,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		++cnt;
		sc->sc_cbt_prod = txs;
	}

	if (cnt != sc->sc_cbt_cnt) {
		/* We enqueued at least one. */
		ifp->if_timer = 5;

		txs = sc->sc_cbt_prod;
		txs = txs->tx_next;
		sc->sc_cbt_prod = txs;
		txs->tx_cb->cb_command =
		    htole16(FXP_CB_COMMAND_I | FXP_CB_COMMAND_NOP | FXP_CB_COMMAND_S);
		FXP_TXCB_SYNC(sc, txs,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		FXP_TXCB_SYNC(sc, sc->sc_cbt_prev,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
		sc->sc_cbt_prev->tx_cb->cb_command &=
		    htole16(~(FXP_CB_COMMAND_S | FXP_CB_COMMAND_I));
		FXP_TXCB_SYNC(sc, sc->sc_cbt_prev,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		sc->sc_cbt_prev = txs;

		fxp_scb_wait(sc);
		fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_RESUME);

		sc->sc_cbt_cnt = cnt + 1;
	}
}

/*
 * Process interface interrupts.
 */
int
fxp_intr(void *arg)
{
	struct fxp_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	u_int16_t statack;
	bus_dmamap_t rxmap;
	int claimed = 0;
	int rnr = 0;

	/*
	 * If the interface isn't running, don't try to
	 * service the interrupt.. just ack it and bail.
	 */
	if ((ifp->if_flags & IFF_RUNNING) == 0) {
		statack = CSR_READ_2(sc, FXP_CSR_SCB_STATUS);
		if (statack) {
			claimed = 1;
			CSR_WRITE_2(sc, FXP_CSR_SCB_STATUS,
			    statack & FXP_SCB_STATACK_MASK);
		}
		return claimed;
	}

	while ((statack = CSR_READ_2(sc, FXP_CSR_SCB_STATUS)) &
	    FXP_SCB_STATACK_MASK) {
		claimed = 1;
		rnr = (statack & (FXP_SCB_STATACK_RNR | 
		                  FXP_SCB_STATACK_SWI)) ? 1 : 0;
		/*
		 * First ACK all the interrupts in this pass.
		 */
		CSR_WRITE_2(sc, FXP_CSR_SCB_STATUS,
		    statack & FXP_SCB_STATACK_MASK);

		/*
		 * Free any finished transmit mbuf chains.
		 */
		if (statack & (FXP_SCB_STATACK_CXTNO|FXP_SCB_STATACK_CNA)) {
			int txcnt = sc->sc_cbt_cnt;
			struct fxp_txsw *txs = sc->sc_cbt_cons;

			FXP_TXCB_SYNC(sc, txs,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

			while ((txcnt > 0) &&
			   ((txs->tx_cb->cb_status & htole16(FXP_CB_STATUS_C)) ||
			   (txs->tx_cb->cb_command & htole16(FXP_CB_COMMAND_NOP)))) {
				if (txs->tx_mbuf != NULL) {
					FXP_MBUF_SYNC(sc, txs->tx_map,
					    BUS_DMASYNC_POSTWRITE);
					bus_dmamap_unload(sc->sc_dmat,
					    txs->tx_map);
					m_freem(txs->tx_mbuf);
					txs->tx_mbuf = NULL;
				}
				--txcnt;
				txs = txs->tx_next;
				FXP_TXCB_SYNC(sc, txs,
				    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
			}
			sc->sc_cbt_cnt = txcnt;
			/* Did we transmit any packets? */
			if (sc->sc_cbt_cons != txs)
				ifq_clr_oactive(&ifp->if_snd);
			ifp->if_timer = sc->sc_cbt_cnt ? 5 : 0;
			sc->sc_cbt_cons = txs;

			if (!ifq_empty(&ifp->if_snd)) {
				/*
				 * Try to start more packets transmitting.
				 */
				fxp_start(ifp);
			}
		}
		/*
		 * Process receiver interrupts. If a Receive Unit
		 * not ready (RNR) condition exists, get whatever
		 * packets we can and re-start the receiver.
		 */
		if (statack & (FXP_SCB_STATACK_FR | FXP_SCB_STATACK_RNR |
			       FXP_SCB_STATACK_SWI)) {
			struct mbuf *m;
			u_int8_t *rfap;
rcvloop:
			m = sc->rfa_headm;
			rfap = m->m_ext.ext_buf + RFA_ALIGNMENT_FUDGE;
			rxmap = *((bus_dmamap_t *)m->m_ext.ext_buf);
			bus_dmamap_sync(sc->sc_dmat, rxmap,
			    0, MCLBYTES, BUS_DMASYNC_POSTREAD |
			    BUS_DMASYNC_POSTWRITE);

			if (*(u_int16_t *)(rfap +
			    offsetof(struct fxp_rfa, rfa_status)) &
			    htole16(FXP_RFA_STATUS_C)) {
				if (*(u_int16_t *)(rfap +
				    offsetof(struct fxp_rfa, rfa_status)) &
				    htole16(FXP_RFA_STATUS_RNR))
					rnr = 1;

				/*
				 * Remove first packet from the chain.
				 */
				sc->rfa_headm = m->m_next;
				m->m_next = NULL;

				/*
				 * Add a new buffer to the receive chain.
				 * If this fails, the old buffer is recycled
				 * instead.
				 */
				if (fxp_add_rfabuf(sc, m) == 0) {
					u_int16_t total_len;

					total_len = htole16(*(u_int16_t *)(rfap +
					    offsetof(struct fxp_rfa,
					    actual_size))) &
					    (MCLBYTES - 1);
					if (total_len <
					    sizeof(struct ether_header)) {
						m_freem(m);
						goto rcvloop;
					}
					if (*(u_int16_t *)(rfap +
					    offsetof(struct fxp_rfa,
					    rfa_status)) &
					    htole16(FXP_RFA_STATUS_CRC)) {
						m_freem(m);
						goto rcvloop;
					}

					m->m_pkthdr.len = m->m_len = total_len;
					ml_enqueue(&ml, m);
				}
				goto rcvloop;
			}
		}
		if (rnr) {
			rxmap = *((bus_dmamap_t *)
			    sc->rfa_headm->m_ext.ext_buf);
			fxp_scb_wait(sc);
			CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL,
				    rxmap->dm_segs[0].ds_addr +
				    RFA_ALIGNMENT_FUDGE);
			fxp_scb_cmd(sc, FXP_SCB_COMMAND_RU_START);

		}
	}

	if_input(ifp, &ml);

	return (claimed);
}

/*
 * Update packet in/out/collision statistics. The i82557 doesn't
 * allow you to access these counters without doing a fairly
 * expensive DMA to get _all_ of the statistics it maintains, so
 * we do this operation here only once per second. The statistics
 * counters in the kernel are updated from the previous dump-stats
 * DMA and then a new dump-stats DMA is started. The on-chip
 * counters are zeroed when the DMA completes. If we can't start
 * the DMA immediately, we don't wait - we just prepare to read
 * them again next time.
 */
void
fxp_stats_update(void *arg)
{
	struct fxp_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct fxp_stats *sp = &sc->sc_ctrl->stats;
	int s;

	FXP_STATS_SYNC(sc, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	ifp->if_collisions += letoh32(sp->tx_total_collisions);
	if (sp->rx_good) {
		sc->rx_idle_secs = 0;
	} else if (sc->sc_flags & FXPF_RECV_WORKAROUND)
		sc->rx_idle_secs++;
	ifp->if_ierrors +=
	    letoh32(sp->rx_crc_errors) +
	    letoh32(sp->rx_alignment_errors) +
	    letoh32(sp->rx_rnr_errors) +
	    letoh32(sp->rx_overrun_errors);
	/*
	 * If any transmit underruns occurred, bump up the transmit
	 * threshold by another 512 bytes (64 * 8).
	 */
	if (sp->tx_underruns) {
		ifp->if_oerrors += letoh32(sp->tx_underruns);
		if (tx_threshold < 192)
			tx_threshold += 64;
	}
	s = splnet();
	/*
	 * If we haven't received any packets in FXP_MAX_RX_IDLE seconds,
	 * then assume the receiver has locked up and attempt to clear
	 * the condition by reprogramming the multicast filter. This is
	 * a work-around for a bug in the 82557 where the receiver locks
	 * up if it gets certain types of garbage in the synchronization
	 * bits prior to the packet header. This bug is supposed to only
	 * occur in 10Mbps mode, but has been seen to occur in 100Mbps
	 * mode as well (perhaps due to a 10/100 speed transition).
	 */
	if (sc->rx_idle_secs > FXP_MAX_RX_IDLE) {
		sc->rx_idle_secs = 0;
		fxp_init(sc);
		splx(s);
		return;
	}
	/*
	 * If there is no pending command, start another stats
	 * dump. Otherwise punt for now.
	 */
	FXP_STATS_SYNC(sc, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	if (!(CSR_READ_2(sc, FXP_CSR_SCB_COMMAND) & 0xff)) {
		/*
		 * Start another stats dump.
		 */
		fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_DUMPRESET);
	} else {
		/*
		 * A previous command is still waiting to be accepted.
		 * Just zero our copy of the stats and wait for the
		 * next timer event to update them.
		 */
		sp->tx_good = 0;
		sp->tx_underruns = 0;
		sp->tx_total_collisions = 0;

		sp->rx_good = 0;
		sp->rx_crc_errors = 0;
		sp->rx_alignment_errors = 0;
		sp->rx_rnr_errors = 0;
		sp->rx_overrun_errors = 0;
	}

	/* Tick the MII clock. */
	mii_tick(&sc->sc_mii);

	splx(s);
	/*
	 * Schedule another timeout one second from now.
	 */
	timeout_add_sec(&sc->stats_update_to, 1);
}

void
fxp_detach(struct fxp_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	/* Get rid of our timeouts and mbufs */
	fxp_stop(sc, 1, 1);

	/* Detach any PHYs we might have. */
	if (LIST_FIRST(&sc->sc_mii.mii_phys) != NULL)
		mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete any remaining media. */
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);

#ifndef SMALL_KERNEL
	if (sc->sc_ucodebuf)
		free(sc->sc_ucodebuf, M_DEVBUF, sc->sc_ucodelen);
#endif
}

/*
 * Stop the interface. Cancels the statistics updater and resets
 * the interface.
 */
void
fxp_stop(struct fxp_softc *sc, int drain, int softonly)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int i;

	/*
	 * Cancel stats updater.
	 */
	timeout_del(&sc->stats_update_to);

	/*
	 * Turn down interface (done early to avoid bad interactions
	 * between panics, and the watchdog timer)
	 */
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (!softonly)
		mii_down(&sc->sc_mii);

	/*
	 * Issue software reset.
	 */
	if (!softonly) {
		CSR_WRITE_4(sc, FXP_CSR_PORT, FXP_PORT_SELECTIVE_RESET);
		DELAY(10);
	}

	/*
	 * Release any xmit buffers.
	 */
	for (i = 0; i < FXP_NTXCB; i++) {
		if (sc->txs[i].tx_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, sc->txs[i].tx_map);
			m_freem(sc->txs[i].tx_mbuf);
			sc->txs[i].tx_mbuf = NULL;
		}
	}
	sc->sc_cbt_cnt = 0;

	if (drain) {
		bus_dmamap_t rxmap;
		struct mbuf *m;

		/*
		 * Free all the receive buffers then reallocate/reinitialize
		 */
		m = sc->rfa_headm;
		while (m != NULL) {
			rxmap = *((bus_dmamap_t *)m->m_ext.ext_buf);
			bus_dmamap_unload(sc->sc_dmat, rxmap);
			FXP_RXMAP_PUT(sc, rxmap);
			m = m_free(m);
			sc->rx_bufs--;
		}
		sc->rfa_headm = NULL;
		sc->rfa_tailm = NULL;
		for (i = 0; i < FXP_NRFABUFS_MIN; i++) {
			if (fxp_add_rfabuf(sc, NULL) != 0) {
				/*
				 * This "can't happen" - we're at splnet()
				 * and we just freed all the buffers we need
				 * above.
				 */
				panic("fxp_stop: no buffers!");
			}
			sc->rx_bufs++;
		}
	}
}

/*
 * Watchdog/transmission transmit timeout handler. Called when a
 * transmission is started on the interface, but no interrupt is
 * received before the timeout. This usually indicates that the
 * card has wedged for some reason.
 */
void
fxp_watchdog(struct ifnet *ifp)
{
	struct fxp_softc *sc = ifp->if_softc;

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;

	fxp_init(sc);
}

/*
 * Submit a command to the i82557.
 */
void
fxp_scb_cmd(struct fxp_softc *sc, u_int16_t cmd)
{
	CSR_WRITE_2(sc, FXP_CSR_SCB_COMMAND, cmd);
}

void
fxp_init(void *xsc)
{
	struct fxp_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct fxp_cb_config *cbp;
	struct fxp_cb_ias *cb_ias;
	struct fxp_cb_tx *txp;
	bus_dmamap_t rxmap;
	int i, prm, save_bf, lrxen, allm, bufs;

	splassert(IPL_NET);

	/*
	 * Cancel any pending I/O
	 */
	fxp_stop(sc, 0, 0);

	/*
	 * Initialize base of CBL and RFA memory. Loading with zero
	 * sets it up for regular linear addressing.
	 */
	fxp_scb_wait(sc);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, 0);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_BASE);

	fxp_scb_wait(sc);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, 0);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_RU_BASE);

#ifndef SMALL_KERNEL
	fxp_load_ucode(sc);
#endif
	/* Once through to set flags */
	fxp_mc_setup(sc, 0);

        /*
	 * In order to support receiving 802.1Q VLAN frames, we have to
	 * enable "save bad frames", since they are 4 bytes larger than
	 * the normal Ethernet maximum frame length. On i82558 and later,
	 * we have a better mechanism for this.
	 */
	save_bf = 0;
	lrxen = 0;

	if (sc->sc_revision >= FXP_REV_82558_A4)
		lrxen = 1;
	else
		save_bf = 1;

	/*
	 * Initialize base of dump-stats buffer.
	 */
	fxp_scb_wait(sc);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL,
	    sc->tx_cb_map->dm_segs->ds_addr +
	    offsetof(struct fxp_ctrl, stats));
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_DUMP_ADR);

	cbp = &sc->sc_ctrl->u.cfg;
	/*
	 * This bcopy is kind of disgusting, but there are a bunch of must be
	 * zero and must be one bits in this structure and this is the easiest
	 * way to initialize them all to proper values.
	 */
	bcopy(fxp_cb_config_template, (void *)&cbp->cb_status,
		sizeof(fxp_cb_config_template));

	prm = (ifp->if_flags & IFF_PROMISC) ? 1 : 0;
	allm = (ifp->if_flags & IFF_ALLMULTI) ? 1 : 0;

#if 0
	cbp->cb_status =	0;
	cbp->cb_command =	FXP_CB_COMMAND_CONFIG | FXP_CB_COMMAND_EL;
	cbp->link_addr =	0xffffffff;	/* (no) next command */
	cbp->byte_count =	22;		/* (22) bytes to config */
	cbp->rx_fifo_limit =	8;	/* rx fifo threshold (32 bytes) */
	cbp->tx_fifo_limit =	0;	/* tx fifo threshold (0 bytes) */
	cbp->adaptive_ifs =	0;	/* (no) adaptive interframe spacing */
	cbp->rx_dma_bytecount =	0;	/* (no) rx DMA max */
	cbp->tx_dma_bytecount =	0;	/* (no) tx DMA max */
	cbp->dma_bce =		0;	/* (disable) dma max counters */
	cbp->late_scb =		0;	/* (don't) defer SCB update */
	cbp->tno_int =		0;	/* (disable) tx not okay interrupt */
	cbp->ci_int =		1;	/* interrupt on CU idle */
	cbp->save_bf =		save_bf ? 1 : prm; /* save bad frames */
	cbp->disc_short_rx =	!prm;	/* discard short packets */
	cbp->underrun_retry =	1;	/* retry mode (1) on DMA underrun */
	cbp->mediatype =	!sc->phy_10Mbps_only; /* interface mode */
	cbp->nsai =		1;	/* (don't) disable source addr insert */
	cbp->preamble_length =	2;	/* (7 byte) preamble */
	cbp->loopback =		0;	/* (don't) loopback */
	cbp->linear_priority =	0;	/* (normal CSMA/CD operation) */
	cbp->linear_pri_mode =	0;	/* (wait after xmit only) */
	cbp->interfrm_spacing =	6;	/* (96 bits of) interframe spacing */
	cbp->promiscuous =	prm;	/* promiscuous mode */
	cbp->bcast_disable =	0;	/* (don't) disable broadcasts */
	cbp->crscdt =		0;	/* (CRS only) */
	cbp->stripping =	!prm;	/* truncate rx packet to byte count */
	cbp->padding =		1;	/* (do) pad short tx packets */
	cbp->rcv_crc_xfer =	0;	/* (don't) xfer CRC to host */
	cbp->long_rx =		lrxen;	/* (enable) long packets */
	cbp->force_fdx =	0;	/* (don't) force full duplex */
	cbp->fdx_pin_en =	1;	/* (enable) FDX# pin */
	cbp->multi_ia =		0;	/* (don't) accept multiple IAs */
	cbp->mc_all =		allm;
#else
	cbp->cb_command = htole16(FXP_CB_COMMAND_CONFIG | FXP_CB_COMMAND_EL);

	if (allm && !prm)
		cbp->mc_all |= 0x08;		/* accept all multicasts */
	else
		cbp->mc_all &= ~0x08;		/* reject all multicasts */

	if (prm) {
		cbp->promiscuous |= 1;		/* promiscuous mode */
		cbp->ctrl2 &= ~0x01;		/* save short packets */
		cbp->stripping &= ~0x01;	/* don't truncate rx packets */
	} else {
		cbp->promiscuous &= ~1;		/* no promiscuous mode */
		cbp->ctrl2 |= 0x01;		/* discard short packets */
		cbp->stripping |= 0x01;		/* truncate rx packets */
	}

	if (prm || save_bf)
		cbp->ctrl1 |= 0x80;		/* save bad frames */
	else
		cbp->ctrl1 &= ~0x80;		/* discard bad frames */

	if (sc->sc_flags & FXPF_MWI_ENABLE)
		cbp->ctrl0 |= 0x01;		/* enable PCI MWI command */

	if(!sc->phy_10Mbps_only)			/* interface mode */
		cbp->mediatype |= 0x01;
	else
		cbp->mediatype &= ~0x01;

	if(lrxen)			/* long packets */
		cbp->stripping |= 0x08;
	else
		cbp->stripping &= ~0x08;

	cbp->tx_dma_bytecount = 0; /* (no) tx DMA max, dma_dce = 0 ??? */
	cbp->ctrl1 |= 0x08;	/* ci_int = 1 */
	cbp->ctrl3 |= 0x08;	/* nsai */
	cbp->fifo_limit = 0x08; /* tx and rx fifo limit */
	cbp->fdx_pin |= 0x80;	/* Enable full duplex setting by pin */
#endif

	/*
	 * Start the config command/DMA.
	 */
	fxp_scb_wait(sc);
	FXP_CFG_SYNC(sc, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->tx_cb_map->dm_segs->ds_addr +
	    offsetof(struct fxp_ctrl, u.cfg));
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);
	/* ...and wait for it to complete. */
	i = FXP_CMD_TMO;
	do {
		DELAY(1);
		FXP_CFG_SYNC(sc, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	} while ((cbp->cb_status & htole16(FXP_CB_STATUS_C)) == 0 && i--);

	FXP_CFG_SYNC(sc, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	if (!(cbp->cb_status & htole16(FXP_CB_STATUS_C))) {
		printf("%s: config command timeout\n", sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * Now initialize the station address.
	 */
	cb_ias = &sc->sc_ctrl->u.ias;
	cb_ias->cb_status = htole16(0);
	cb_ias->cb_command = htole16(FXP_CB_COMMAND_IAS | FXP_CB_COMMAND_EL);
	cb_ias->link_addr = htole32(0xffffffff);
	bcopy(sc->sc_arpcom.ac_enaddr, (void *)cb_ias->macaddr,
	    sizeof(sc->sc_arpcom.ac_enaddr));

	/*
	 * Start the IAS (Individual Address Setup) command/DMA.
	 */
	fxp_scb_wait(sc);
	FXP_IAS_SYNC(sc, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->tx_cb_map->dm_segs->ds_addr +
	    offsetof(struct fxp_ctrl, u.ias));
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);
	/* ...and wait for it to complete. */
	i = FXP_CMD_TMO;
	do {
		DELAY(1);
		FXP_IAS_SYNC(sc, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	} while (!(cb_ias->cb_status & htole16(FXP_CB_STATUS_C)) && i--);

	FXP_IAS_SYNC(sc, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	if (!(cb_ias->cb_status & htole16(FXP_CB_STATUS_C))) {
		printf("%s: IAS command timeout\n", sc->sc_dev.dv_xname);
		return;
	}

	/* Again, this time really upload the multicast addresses */
	fxp_mc_setup(sc, 1);

	/*
	 * Initialize transmit control block (TxCB) list.
	 */
	bzero(sc->sc_ctrl->tx_cb, sizeof(struct fxp_cb_tx) * FXP_NTXCB);
	txp = sc->sc_ctrl->tx_cb;
	for (i = 0; i < FXP_NTXCB; i++) {
		txp[i].cb_command = htole16(FXP_CB_COMMAND_NOP);
		txp[i].link_addr = htole32(sc->tx_cb_map->dm_segs->ds_addr +
		    offsetof(struct fxp_ctrl, tx_cb[(i + 1) & FXP_TXCB_MASK]));
		txp[i].tbd_array_addr =htole32(sc->tx_cb_map->dm_segs->ds_addr +
		    offsetof(struct fxp_ctrl, tx_cb[i].tbd[0]));
	}
	/*
	 * Set the suspend flag on the first TxCB and start the control
	 * unit. It will execute the NOP and then suspend.
	 */
	sc->sc_cbt_prev = sc->sc_cbt_prod = sc->sc_cbt_cons = sc->txs;
	sc->sc_cbt_cnt = 1;
	sc->sc_ctrl->tx_cb[0].cb_command = htole16(FXP_CB_COMMAND_NOP |
	    FXP_CB_COMMAND_S | FXP_CB_COMMAND_I);
	bus_dmamap_sync(sc->sc_dmat, sc->tx_cb_map, 0,
	    sc->tx_cb_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	fxp_scb_wait(sc);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->tx_cb_map->dm_segs->ds_addr +
	    offsetof(struct fxp_ctrl, tx_cb[0]));
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);

	/*
	 * Initialize receiver buffer area - RFA.
	 */
	if (ifp->if_flags & IFF_UP)
		bufs = FXP_NRFABUFS_MAX;
	else
		bufs = FXP_NRFABUFS_MIN;
	if (sc->rx_bufs > bufs) {
		while (sc->rfa_headm != NULL && sc->rx_bufs > bufs) {
			rxmap = *((bus_dmamap_t *)sc->rfa_headm->m_ext.ext_buf);
			bus_dmamap_unload(sc->sc_dmat, rxmap);
			FXP_RXMAP_PUT(sc, rxmap);
			sc->rfa_headm = m_free(sc->rfa_headm);
			sc->rx_bufs--;
		}
	} else if (sc->rx_bufs < bufs) {
		int err, tmp_rx_bufs = sc->rx_bufs;
		for (i = sc->rx_bufs; i < bufs; i++) {
			if ((err = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
			    MCLBYTES, 0, 0, &sc->sc_rxmaps[i])) != 0) {
				printf("%s: unable to create rx dma map %d, "
				  "error %d\n", sc->sc_dev.dv_xname, i, err);
				break;
			}
			sc->rx_bufs++;
		}
		for (i = tmp_rx_bufs; i < sc->rx_bufs; i++)
			if (fxp_add_rfabuf(sc, NULL) != 0)
				break;
	}
	fxp_scb_wait(sc);

	/*
	 * Set current media.
	 */
	mii_mediachg(&sc->sc_mii);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/*
	 * Request a software generated interrupt that will be used to 
	 * (re)start the RU processing.  If we direct the chip to start
	 * receiving from the start of queue now, instead of letting the
	 * interrupt handler first process all received packets, we run
	 * the risk of having it overwrite mbuf clusters while they are
	 * being processed or after they have been returned to the pool.
	 */
	CSR_WRITE_2(sc, FXP_CSR_SCB_COMMAND,
	    CSR_READ_2(sc, FXP_CSR_SCB_COMMAND) |
	    FXP_SCB_INTRCNTL_REQUEST_SWI);

	/*
	 * Start stats updater.
	 */
	timeout_add_sec(&sc->stats_update_to, 1);
}

/*
 * Change media according to request.
 */
int
fxp_mediachange(struct ifnet *ifp)
{
	struct fxp_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;

	if (mii->mii_instance) {
		struct mii_softc *miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(&sc->sc_mii);
	return (0);
}

/*
 * Notify the world which media we're using.
 */
void
fxp_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct fxp_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

/*
 * Add a buffer to the end of the RFA buffer list.
 * Return 0 if successful, 1 for failure. A failure results in
 * adding the 'oldm' (if non-NULL) on to the end of the list -
 * tossing out its old contents and recycling it.
 * The RFA struct is stuck at the beginning of mbuf cluster and the
 * data pointer is fixed up to point just past it.
 */
int
fxp_add_rfabuf(struct fxp_softc *sc, struct mbuf *oldm)
{
	u_int32_t v;
	struct mbuf *m;
	u_int8_t *rfap;
	bus_dmamap_t rxmap = NULL;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m != NULL) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			if (oldm == NULL)
				return 1;
			m = oldm;
			m->m_data = m->m_ext.ext_buf;
		}
		if (oldm == NULL) {
			rxmap = FXP_RXMAP_GET(sc);
			*((bus_dmamap_t *)m->m_ext.ext_buf) = rxmap;
			bus_dmamap_load(sc->sc_dmat, rxmap,
			    m->m_ext.ext_buf, m->m_ext.ext_size, NULL,
			    BUS_DMA_NOWAIT);
		} else if (oldm == m)
			rxmap = *((bus_dmamap_t *)oldm->m_ext.ext_buf);
		else {
			rxmap = *((bus_dmamap_t *)oldm->m_ext.ext_buf);
			bus_dmamap_unload(sc->sc_dmat, rxmap);
			bus_dmamap_load(sc->sc_dmat, rxmap,
			    m->m_ext.ext_buf, m->m_ext.ext_size, NULL,
			    BUS_DMA_NOWAIT);
			*mtod(m, bus_dmamap_t *) = rxmap;
		}
	} else {
		if (oldm == NULL)
			return 1;
		m = oldm;
		m->m_data = m->m_ext.ext_buf;
		rxmap = *mtod(m, bus_dmamap_t *);
	}

	/*
	 * Move the data pointer up so that the incoming data packet
	 * will be 32-bit aligned.
	 */
	m->m_data += RFA_ALIGNMENT_FUDGE;

	/*
	 * Get a pointer to the base of the mbuf cluster and move
	 * data start past it.
	 */
	rfap = m->m_data;
	m->m_data += sizeof(struct fxp_rfa);
	*(u_int16_t *)(rfap + offsetof(struct fxp_rfa, size)) =
	    htole16(MCLBYTES - sizeof(struct fxp_rfa) - RFA_ALIGNMENT_FUDGE);

	/*
	 * Initialize the rest of the RFA.  Note that since the RFA
	 * is misaligned, we cannot store values directly.  Instead,
	 * we use an optimized, inline copy.
	 */
	*(u_int16_t *)(rfap + offsetof(struct fxp_rfa, rfa_status)) = 0;
	*(u_int16_t *)(rfap + offsetof(struct fxp_rfa, rfa_control)) =
	    htole16(FXP_RFA_CONTROL_EL);
	*(u_int16_t *)(rfap + offsetof(struct fxp_rfa, actual_size)) = 0;

	v = -1;
	fxp_lwcopy(&v,
	    (u_int32_t *)(rfap + offsetof(struct fxp_rfa, link_addr)));
	fxp_lwcopy(&v,
	    (u_int32_t *)(rfap + offsetof(struct fxp_rfa, rbd_addr)));

	bus_dmamap_sync(sc->sc_dmat, rxmap, 0, MCLBYTES,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/*
	 * If there are other buffers already on the list, attach this
	 * one to the end by fixing up the tail to point to this one.
	 */
	if (sc->rfa_headm != NULL) {
		sc->rfa_tailm->m_next = m;
		v = htole32(rxmap->dm_segs[0].ds_addr + RFA_ALIGNMENT_FUDGE);
		rfap = sc->rfa_tailm->m_ext.ext_buf + RFA_ALIGNMENT_FUDGE;
		fxp_lwcopy(&v,
		    (u_int32_t *)(rfap + offsetof(struct fxp_rfa, link_addr)));
		*(u_int16_t *)(rfap + offsetof(struct fxp_rfa, rfa_control)) &=
		    htole16((u_int16_t)~FXP_RFA_CONTROL_EL);
		/* XXX we only need to sync the control struct */
		bus_dmamap_sync(sc->sc_dmat,
		    *((bus_dmamap_t *)sc->rfa_tailm->m_ext.ext_buf), 0,
			MCLBYTES, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	} else
		sc->rfa_headm = m;

	sc->rfa_tailm = m;

	return (m == oldm);
}

int
fxp_mdi_read(struct device *self, int phy, int reg)
{
	struct fxp_softc *sc = (struct fxp_softc *)self;
	int count = FXP_CMD_TMO;
	int value;

	CSR_WRITE_4(sc, FXP_CSR_MDICONTROL,
	    (FXP_MDI_READ << 26) | (reg << 16) | (phy << 21));

	while (((value = CSR_READ_4(sc, FXP_CSR_MDICONTROL)) & 0x10000000) == 0
	    && count--)
		DELAY(10);

	if (count <= 0)
		printf("%s: fxp_mdi_read: timed out\n", sc->sc_dev.dv_xname);

	return (value & 0xffff);
}

void
fxp_statchg(struct device *self)
{
	/* Nothing to do. */
}

void
fxp_mdi_write(struct device *self, int phy, int reg, int value)
{
	struct fxp_softc *sc = (struct fxp_softc *)self;
	int count = FXP_CMD_TMO;

	CSR_WRITE_4(sc, FXP_CSR_MDICONTROL,
	    (FXP_MDI_WRITE << 26) | (reg << 16) | (phy << 21) |
	    (value & 0xffff));

	while((CSR_READ_4(sc, FXP_CSR_MDICONTROL) & 0x10000000) == 0 &&
	    count--)
		DELAY(10);

	if (count <= 0)
		printf("%s: fxp_mdi_write: timed out\n", sc->sc_dev.dv_xname);
}

int
fxp_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct fxp_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			fxp_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				fxp_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				fxp_stop(sc, 1, 0);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, command);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			fxp_init(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

/*
 * Program the multicast filter.
 *
 * We have an artificial restriction that the multicast setup command
 * must be the first command in the chain, so we take steps to ensure
 * this. By requiring this, it allows us to keep up the performance of
 * the pre-initialized command ring (esp. link pointers) by not actually
 * inserting the mcsetup command in the ring - i.e. its link pointer
 * points to the TxCB ring, but the mcsetup descriptor itself is not part
 * of it. We then can do 'CU_START' on the mcsetup descriptor and have it
 * lead into the regular TxCB ring when it completes.
 *
 * This function must be called at splnet.
 */
void
fxp_mc_setup(struct fxp_softc *sc, int doit)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct arpcom *ac = &sc->sc_arpcom;
	struct fxp_cb_mcs *mcsp = &sc->sc_ctrl->u.mcs;
	struct ether_multistep step;
	struct ether_multi *enm;
	int i, nmcasts = 0;

	splassert(IPL_NET);

	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt >= MAXMCADDR) {
		ifp->if_flags |= IFF_ALLMULTI;
	} else {
		ETHER_FIRST_MULTI(step, &sc->sc_arpcom, enm);
		while (enm != NULL) {
			bcopy(enm->enm_addrlo,
			    (void *)&mcsp->mc_addr[nmcasts][0], ETHER_ADDR_LEN);

			nmcasts++;

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	if (doit == 0)
		return;

	/* 
	 * Initialize multicast setup descriptor.
	 */
	mcsp->cb_status = htole16(0);
	mcsp->cb_command = htole16(FXP_CB_COMMAND_MCAS | FXP_CB_COMMAND_EL);
	mcsp->link_addr = htole32(-1);
	mcsp->mc_cnt = htole16(nmcasts * ETHER_ADDR_LEN);

	/*
	 * Wait until command unit is not active. This should never
	 * be the case when nothing is queued, but make sure anyway.
	 */
	for (i = FXP_CMD_TMO; (CSR_READ_2(sc, FXP_CSR_SCB_STATUS) &
	    FXP_SCB_CUS_MASK) != FXP_SCB_CUS_IDLE && i--; DELAY(1));

	if ((CSR_READ_2(sc, FXP_CSR_SCB_STATUS) &
	    FXP_SCB_CUS_MASK) != FXP_SCB_CUS_IDLE) {
		printf("%s: timeout waiting for CU ready\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * Start the multicast setup command.
	 */
	fxp_scb_wait(sc);
	FXP_MCS_SYNC(sc, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->tx_cb_map->dm_segs->ds_addr +
	    offsetof(struct fxp_ctrl, u.mcs));
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);

	i = FXP_CMD_TMO;
	do {
		DELAY(1);
		FXP_MCS_SYNC(sc, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	} while (!(mcsp->cb_status & htole16(FXP_CB_STATUS_C)) && i--);

	FXP_MCS_SYNC(sc, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	if (!(mcsp->cb_status & htole16(FXP_CB_STATUS_C))) {
		printf("%s: multicast command timeout\n", sc->sc_dev.dv_xname);
		return;
	}

}

#ifndef SMALL_KERNEL
#include <dev/microcode/fxp/rcvbundl.h>
struct ucode {
	u_int16_t	revision;
	u_int16_t	int_delay_offset;
	u_int16_t	bundle_max_offset;
	u_int16_t	min_size_mask_offset;
	const char	*uname;
} const ucode_table[] = {
	{ FXP_REV_82558_A4, D101_CPUSAVER_DWORD,
	  0, 0,
	  "fxp-d101a" }, 

	{ FXP_REV_82558_B0, D101_CPUSAVER_DWORD,
	  0, 0,
	  "fxp-d101b0" },

	{ FXP_REV_82559_A0, D101M_CPUSAVER_DWORD, 
	  D101M_CPUSAVER_BUNDLE_MAX_DWORD, D101M_CPUSAVER_MIN_SIZE_DWORD,
	  "fxp-d101ma" },

	{ FXP_REV_82559S_A, D101S_CPUSAVER_DWORD,
	  D101S_CPUSAVER_BUNDLE_MAX_DWORD, D101S_CPUSAVER_MIN_SIZE_DWORD,
	  "fxp-d101s" },

	{ FXP_REV_82550, D102_B_CPUSAVER_DWORD,
	  D102_B_CPUSAVER_BUNDLE_MAX_DWORD, D102_B_CPUSAVER_MIN_SIZE_DWORD,
	  "fxp-d102" },

	{ FXP_REV_82550_C, D102_C_CPUSAVER_DWORD,
	  D102_C_CPUSAVER_BUNDLE_MAX_DWORD, D102_C_CPUSAVER_MIN_SIZE_DWORD,
	  "fxp-d102c" },

	{ FXP_REV_82551_F, D102_E_CPUSAVER_DWORD,
	  D102_E_CPUSAVER_BUNDLE_MAX_DWORD, D102_E_CPUSAVER_MIN_SIZE_DWORD,
	  "fxp-d102e" },

	{ FXP_REV_82551_10, D102_E_CPUSAVER_DWORD,
	  D102_E_CPUSAVER_BUNDLE_MAX_DWORD, D102_E_CPUSAVER_MIN_SIZE_DWORD,
	  "fxp-d102e" },
	
	{ 0, 0,
	  0, 0,
	  NULL }
};

void
fxp_load_ucode(struct fxp_softc *sc)
{
	const struct ucode *uc;
	struct fxp_cb_ucode *cbp = &sc->sc_ctrl->u.code;
	int i, error;

	if (sc->sc_flags & FXPF_NOUCODE)
		return;

	for (uc = ucode_table; uc->revision != 0; uc++)
		if (sc->sc_revision == uc->revision)
			break;
	if (uc->revision == 0) {
		sc->sc_flags |= FXPF_NOUCODE;
		return;	/* no ucode for this chip is found */
	}

	if (sc->sc_ucodebuf)
		goto reloadit;

	if (sc->sc_revision == FXP_REV_82550_C) {
		u_int16_t data;

		/*
		 * 82550C without the server extensions
		 * locks up with the microcode patch.
		 */
		fxp_read_eeprom(sc, &data, FXP_EEPROM_REG_COMPAT, 1);
		if ((data & FXP_EEPROM_REG_COMPAT_SRV) == 0) {
			sc->sc_flags |= FXPF_NOUCODE;
			return;
		}
	}

	error = loadfirmware(uc->uname, (u_char **)&sc->sc_ucodebuf,
	    &sc->sc_ucodelen);
	if (error) {
		printf("%s: error %d, could not read firmware %s\n",
		    sc->sc_dev.dv_xname, error, uc->uname);
		return;
	}

reloadit:
	if (sc->sc_flags & FXPF_UCODELOADED)
		return;

	cbp->cb_status = 0;
	cbp->cb_command = htole16(FXP_CB_COMMAND_UCODE|FXP_CB_COMMAND_EL);
	cbp->link_addr = 0xffffffff;	/* (no) next command */
	for (i = 0; i < (sc->sc_ucodelen / sizeof(u_int32_t)); i++)
		cbp->ucode[i] = sc->sc_ucodebuf[i];

	if (uc->int_delay_offset)
		*((u_int16_t *)&cbp->ucode[uc->int_delay_offset]) =
			htole16(sc->sc_int_delay + sc->sc_int_delay / 2);

	if (uc->bundle_max_offset)
		*((u_int16_t *)&cbp->ucode[uc->bundle_max_offset]) =
			htole16(sc->sc_bundle_max);

	if (uc->min_size_mask_offset)
		*((u_int16_t *)&cbp->ucode[uc->min_size_mask_offset]) =
			htole16(sc->sc_min_size_mask);
	
	FXP_UCODE_SYNC(sc, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	/*
	 * Download the ucode to the chip.
	 */
	fxp_scb_wait(sc);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->tx_cb_map->dm_segs->ds_addr
	      + offsetof(struct fxp_ctrl, u.code));
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);

	/* ...and wait for it to complete. */
	i = FXP_CMD_TMO;
	do {
		DELAY(2);
		FXP_UCODE_SYNC(sc, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	} while (((cbp->cb_status & htole16(FXP_CB_STATUS_C)) == 0) && --i);
	if (i == 0) {
		printf("%s: timeout loading microcode\n", sc->sc_dev.dv_xname);
		return;
	}
	sc->sc_flags |= FXPF_UCODELOADED;

#ifdef DEBUG
	printf("%s: microcode loaded, int_delay: %d usec",
	    sc->sc_dev.dv_xname, sc->sc_int_delay);

	if (uc->bundle_max_offset)
		printf(", bundle_max %d\n", sc->sc_bundle_max);
	else
		printf("\n");
#endif
}
#endif /* SMALL_KERNEL */
