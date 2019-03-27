/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997,1998 Maxim Bolotin and Oleg Sharoiko.
 * All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 *
 * Device driver for Crystal Semiconductor CS8920 based ethernet
 *   adapters. By Maxim Bolotin and Oleg Sharoiko, 27-April-1997
 */

/*
#define	 CS_DEBUG 
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/bpf.h>

#include <dev/cs/if_csvar.h>
#include <dev/cs/if_csreg.h>

#ifdef  CS_USE_64K_DMA
#define CS_DMA_BUFFER_SIZE 65536
#else
#define CS_DMA_BUFFER_SIZE 16384
#endif

static void	cs_init(void *);
static void	cs_init_locked(struct cs_softc *);
static int	cs_ioctl(struct ifnet *, u_long, caddr_t);
static void	cs_start(struct ifnet *);
static void	cs_start_locked(struct ifnet *);
static void	cs_stop(struct cs_softc *);
static void	cs_reset(struct cs_softc *);
static void	cs_watchdog(void *);

static int	cs_mediachange(struct ifnet *);
static void	cs_mediastatus(struct ifnet *, struct ifmediareq *);
static int      cs_mediaset(struct cs_softc *, int);

static void	cs_write_mbufs(struct cs_softc*, struct mbuf*);
static void	cs_xmit_buf(struct cs_softc*);
static int	cs_get_packet(struct cs_softc*);
static void	cs_setmode(struct cs_softc*);

static int	get_eeprom_data(struct cs_softc *sc, int, int, uint16_t *);
static int	get_eeprom_cksum(int, int, uint16_t *);
static int	wait_eeprom_ready( struct cs_softc *);
static void	control_dc_dc( struct cs_softc *, int );
static int	enable_tp(struct cs_softc *);
static int	enable_aui(struct cs_softc *);
static int	enable_bnc(struct cs_softc *);
static int      cs_duplex_auto(struct cs_softc *);

devclass_t cs_devclass;
driver_intr_t	csintr;

/* sysctl vars */
static SYSCTL_NODE(_hw, OID_AUTO, cs, CTLFLAG_RD, 0, "cs device parameters");

int	cs_ignore_cksum_failure = 0;
SYSCTL_INT(_hw_cs, OID_AUTO, ignore_checksum_failure, CTLFLAG_RWTUN,
    &cs_ignore_cksum_failure, 0,
  "ignore checksum errors in cs card EEPROM");

static int	cs_recv_delay = 570;
SYSCTL_INT(_hw_cs, OID_AUTO, recv_delay, CTLFLAG_RWTUN, &cs_recv_delay, 570, "");

static int cs8900_eeint2irq[16] = {
	 10,  11,  12,   5, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255 
};

static int cs8900_irq2eeint[16] = {
	255, 255, 255, 255, 255,   3, 255, 255,
	255,   0,   1,   2, 255, 255, 255, 255
};

static int
get_eeprom_data(struct cs_softc *sc, int off, int len, uint16_t *buffer)
{
	int i;

#ifdef CS_DEBUG
	device_printf(sc->dev, "EEPROM data from %x for %x:\n", off, len);
#endif
	for (i=0; i < len; i++) {
		if (wait_eeprom_ready(sc) < 0)
			return (-1);
		/* Send command to EEPROM to read */
		cs_writereg(sc, PP_EECMD, (off + i) | EEPROM_READ_CMD);
		if (wait_eeprom_ready(sc) < 0)
			return (-1);
		buffer[i] = cs_readreg(sc, PP_EEData);

#ifdef CS_DEBUG
		printf("%04x ",buffer[i]);
#endif
	}

#ifdef CS_DEBUG
	printf("\n");
#endif
	return (0);
}

static int
get_eeprom_cksum(int off, int len, uint16_t *buffer)
{
	int i;
	uint16_t cksum=0;

	for (i = 0; i < len; i++)
		cksum += buffer[i];
	cksum &= 0xffff;
	if (cksum == 0 || cs_ignore_cksum_failure)
		return (0);
	return (-1);
}

static int
wait_eeprom_ready(struct cs_softc *sc)
{
	int i;

	/*
	 * From the CS8900A datasheet, section 3.5.2:
	 * "Before issuing any command to the EEPROM, the host must wait
	 * for the SIBUSY bit (Register 16, SelfST, bit 8) to clear.  After
	 * each command has been issued, the host must wait again for SIBUSY
	 * to clear."
	 *
	 * Before we issue the command, we should be !busy, so that will
	 * be fast.  The datasheet suggests that clock out from the part
	 * per word will be on the order of 25us, which is consistent with
	 * the 1MHz serial clock and 16bits...  We should never hit 100,
	 * let alone 15,000 here.  The original code did an unconditional
	 * 30ms DELAY here.  Bad Kharma.  cs_readreg takes ~2us.
	 */
	for (i = 0; i < 15000; i++)	/* 30ms max */
		if (!(cs_readreg(sc, PP_SelfST) & SI_BUSY))
			return (0);
	return (1);
}

static void
control_dc_dc(struct cs_softc *sc, int on_not_off)
{
	unsigned int self_control = HCB1_ENBL;

	if (((sc->adapter_cnf & A_CNF_DC_DC_POLARITY)!=0) ^ on_not_off)
		self_control |= HCB1;
	else
		self_control &= ~HCB1;
	cs_writereg(sc, PP_SelfCTL, self_control);
	DELAY(500000);	/* Bad! */
}


static int
cs_duplex_auto(struct cs_softc *sc)
{
	int i, error=0;

	cs_writereg(sc, PP_AutoNegCTL,
	    RE_NEG_NOW | ALLOW_FDX | AUTO_NEG_ENABLE);
	for (i=0; cs_readreg(sc, PP_AutoNegST) & AUTO_NEG_BUSY; i++) {
		if (i > 4000) {
			device_printf(sc->dev,
			    "full/half duplex auto negotiation timeout\n");
			error = ETIMEDOUT;
			break;
		}
		DELAY(1000);
	}
	return (error);
}

static int
enable_tp(struct cs_softc *sc)
{

	cs_writereg(sc, PP_LineCTL, sc->line_ctl & ~AUI_ONLY);
	control_dc_dc(sc, 0);
	return (0);
}

static int
enable_aui(struct cs_softc *sc)
{

	cs_writereg(sc, PP_LineCTL,
	    (sc->line_ctl & ~AUTO_AUI_10BASET) | AUI_ONLY);
	control_dc_dc(sc, 0);
	return (0);
}

static int
enable_bnc(struct cs_softc *sc)
{

	cs_writereg(sc, PP_LineCTL,
	    (sc->line_ctl & ~AUTO_AUI_10BASET) | AUI_ONLY);
	control_dc_dc(sc, 1);
	return (0);
}

int
cs_cs89x0_probe(device_t dev)
{
	int i;
	int error;
	rman_res_t irq, junk;
	struct cs_softc *sc = device_get_softc(dev);
	unsigned rev_type = 0;
	uint16_t id;
	char chip_revision;
	uint16_t eeprom_buff[CHKSUM_LEN];
	int chip_type, pp_isaint;

	sc->dev = dev;
	error = cs_alloc_port(dev, 0, CS_89x0_IO_PORTS);
	if (error)
		return (error);

	if ((cs_inw(sc, ADD_PORT) & ADD_MASK) != ADD_SIG) {
		/* Chip not detected. Let's try to reset it */
		if (bootverbose)
			device_printf(dev, "trying to reset the chip.\n");
		cs_outw(sc, ADD_PORT, PP_SelfCTL);
		i = cs_inw(sc, DATA_PORT);
		cs_outw(sc, ADD_PORT, PP_SelfCTL);
		cs_outw(sc, DATA_PORT, i | POWER_ON_RESET);
		if ((cs_inw(sc, ADD_PORT) & ADD_MASK) != ADD_SIG)
			return (ENXIO);
	}

	for (i = 0; i < 10000; i++) {
		id = cs_readreg(sc, PP_ChipID);
		if (id == CHIP_EISA_ID_SIG)
			break;
	}
	if (i == 10000)
		return (ENXIO);

	rev_type = cs_readreg(sc, PRODUCT_ID_ADD);
	chip_type = rev_type & ~REVISON_BITS;
	chip_revision = ((rev_type & REVISON_BITS) >> 8) + 'A';

	sc->chip_type = chip_type;

	if (chip_type == CS8900) {
		pp_isaint = PP_CS8900_ISAINT;
		sc->send_cmd = TX_CS8900_AFTER_ALL;
	} else {
		pp_isaint = PP_CS8920_ISAINT;
		sc->send_cmd = TX_CS8920_AFTER_ALL;
	}

	/*
	 * Clear some fields so that fail of EEPROM will left them clean
	 */
	sc->auto_neg_cnf = 0;
	sc->adapter_cnf  = 0;
	sc->isa_config   = 0;

	/*
	 * If no interrupt specified, use what the board tells us.
	 */
	error = bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, &junk);

	/*
	 * Get data from EEPROM
	 */
	if((cs_readreg(sc, PP_SelfST) & EEPROM_PRESENT) == 0) {
		device_printf(dev, "No EEPROM, assuming defaults.\n");
	} else if (get_eeprom_data(sc,START_EEPROM_DATA,CHKSUM_LEN, eeprom_buff)<0) {
		device_printf(dev, "EEPROM read failed, assuming defaults.\n");
	} else if (get_eeprom_cksum(START_EEPROM_DATA,CHKSUM_LEN, eeprom_buff)<0) {
		device_printf(dev, "EEPROM cheksum bad, assuming defaults.\n");
	} else {
		sc->auto_neg_cnf = eeprom_buff[AUTO_NEG_CNF_OFFSET];
		sc->adapter_cnf = eeprom_buff[ADAPTER_CNF_OFFSET];
		sc->isa_config = eeprom_buff[ISA_CNF_OFFSET];
		for (i=0; i<ETHER_ADDR_LEN/2; i++) {
			sc->enaddr[i*2] = eeprom_buff[i];
			sc->enaddr[i*2+1] = eeprom_buff[i] >> 8;
		}
		/*
		 * If no interrupt specified, use what the
		 * board tells us.
		 */
		if (error) {
			irq = sc->isa_config & INT_NO_MASK;
			error = 0;
			if (chip_type == CS8900) {
				irq = cs8900_eeint2irq[irq];
			} else {
				if (irq > CS8920_NO_INTS)
					irq = 255;
			}
			if (irq == 255) {
				device_printf(dev, "invalid irq in EEPROM.\n");
				error = EINVAL;
			}
			if (!error)
				bus_set_resource(dev, SYS_RES_IRQ, 0,
				    irq, 1);
		}
	}

	if (!error && !(sc->flags & CS_NO_IRQ)) {
		if (chip_type == CS8900) {
			if (irq < 16)
				irq = cs8900_irq2eeint[irq];
			else
				irq = 255;
		} else {
			if (irq > CS8920_NO_INTS)
				irq = 255;
		}
		if (irq == 255)
			error = EINVAL;
	}

	if (error) {
	       	device_printf(dev, "Unknown or invalid irq\n");
		return (error);
	}

	if (!(sc->flags & CS_NO_IRQ))
		cs_writereg(sc, pp_isaint, irq);

	if (bootverbose)
		 device_printf(dev, "CS89%c0%s rev %c media%s%s%s\n",
			chip_type == CS8900 ? '0' : '2',
			chip_type == CS8920M ? "M" : "",
			chip_revision,
			(sc->adapter_cnf & A_CNF_10B_T) ? " TP"  : "",
			(sc->adapter_cnf & A_CNF_AUI)   ? " AUI" : "",
			(sc->adapter_cnf & A_CNF_10B_2) ? " BNC" : "");

	if ((sc->adapter_cnf & A_CNF_EXTND_10B_2) &&
	    (sc->adapter_cnf & A_CNF_LOW_RX_SQUELCH))
		sc->line_ctl = LOW_RX_SQUELCH;
	else
		sc->line_ctl = 0;

	return (0);
}

/*
 * Allocate a port resource with the given resource id.
 */
int
cs_alloc_port(device_t dev, int rid, int size)
{
	struct cs_softc *sc = device_get_softc(dev);
	struct resource *res;

	res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
	    size, RF_ACTIVE);
	if (res == NULL)
		return (ENOENT);
	sc->port_rid = rid;
	sc->port_res = res;
	return (0);
}

/*
 * Allocate an irq resource with the given resource id.
 */
int
cs_alloc_irq(device_t dev, int rid)
{
	struct cs_softc *sc = device_get_softc(dev);
	struct resource *res;

	res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (res == NULL)
		return (ENOENT);
	sc->irq_rid = rid;
	sc->irq_res = res;
	return (0);
}

/*
 * Release all resources
 */
void
cs_release_resources(device_t dev)
{
	struct cs_softc *sc = device_get_softc(dev);

	if (sc->port_res) {
		bus_release_resource(dev, SYS_RES_IOPORT,
		    sc->port_rid, sc->port_res);
		sc->port_res = 0;
	}
	if (sc->irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ,
		    sc->irq_rid, sc->irq_res);
		sc->irq_res = 0;
	}
}

/*
 * Install the interface into kernel networking data structures
 */
int
cs_attach(device_t dev)
{
	int error, media=0;
	struct cs_softc *sc = device_get_softc(dev);
	struct ifnet *ifp;

	sc->dev = dev;
	
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		cs_release_resources(dev);
		return (ENOMEM);
	}

	mtx_init(&sc->lock, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->timer, &sc->lock, 0);

	CS_LOCK(sc);
	cs_stop(sc);
	CS_UNLOCK(sc);

	ifp->if_softc=sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_start=cs_start;
	ifp->if_ioctl=cs_ioctl;
	ifp->if_init=cs_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);

	ifp->if_flags=(IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);

	/*
	 * this code still in progress (DMA support)
	 *

	sc->recv_ring=malloc(CS_DMA_BUFFER_SIZE<<1, M_DEVBUF, M_NOWAIT);
	if (sc->recv_ring == NULL) {
		log(LOG_ERR,
		"%s: Couldn't allocate memory for NIC\n", ifp->if_xname);
		return(0);
	}
	if ((sc->recv_ring-(sc->recv_ring & 0x1FFFF))
	    < (128*1024-CS_DMA_BUFFER_SIZE))
	    sc->recv_ring+=16*1024;

	*/

	sc->buffer=malloc(ETHER_MAX_LEN-ETHER_CRC_LEN,M_DEVBUF,M_NOWAIT);
	if (sc->buffer == NULL) {
		device_printf(sc->dev, "Couldn't allocate memory for NIC\n");
		if_free(ifp);
		mtx_destroy(&sc->lock);
		cs_release_resources(dev);
		return(ENOMEM);
	}

	/*
	 * Initialize the media structures.
	 */
	ifmedia_init(&sc->media, 0, cs_mediachange, cs_mediastatus);

	if (sc->adapter_cnf & A_CNF_10B_T) {
		ifmedia_add(&sc->media, IFM_ETHER|IFM_10_T, 0, NULL);
		if (sc->chip_type != CS8900) {
			ifmedia_add(&sc->media,
				IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
			ifmedia_add(&sc->media,
				IFM_ETHER|IFM_10_T|IFM_HDX, 0, NULL);
		}
	} 

	if (sc->adapter_cnf & A_CNF_10B_2)
		ifmedia_add(&sc->media, IFM_ETHER|IFM_10_2, 0, NULL);

	if (sc->adapter_cnf & A_CNF_AUI)
		ifmedia_add(&sc->media, IFM_ETHER|IFM_10_5, 0, NULL);

	if (sc->adapter_cnf & A_CNF_MEDIA)
		ifmedia_add(&sc->media, IFM_ETHER|IFM_AUTO, 0, NULL);

	/* Set default media from EEPROM */
	switch (sc->adapter_cnf & A_CNF_MEDIA_TYPE) {
	case A_CNF_MEDIA_AUTO:  media = IFM_ETHER|IFM_AUTO; break;
	case A_CNF_MEDIA_10B_T: media = IFM_ETHER|IFM_10_T; break;
	case A_CNF_MEDIA_10B_2: media = IFM_ETHER|IFM_10_2; break;
	case A_CNF_MEDIA_AUI:   media = IFM_ETHER|IFM_10_5; break;
	default:
		device_printf(sc->dev, "no media, assuming 10baseT\n");
		sc->adapter_cnf |= A_CNF_10B_T;
		ifmedia_add(&sc->media, IFM_ETHER|IFM_10_T, 0, NULL);
		if (sc->chip_type != CS8900) {
			ifmedia_add(&sc->media,
			    IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
			ifmedia_add(&sc->media,
			    IFM_ETHER|IFM_10_T|IFM_HDX, 0, NULL);
		}
		media = IFM_ETHER | IFM_10_T;
		break;
	}
	ifmedia_set(&sc->media, media);
	cs_mediaset(sc, media);

	ether_ifattach(ifp, sc->enaddr);

  	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, csintr, sc, &sc->irq_handle);
	if (error) {
		ether_ifdetach(ifp);
		free(sc->buffer, M_DEVBUF);
		if_free(ifp);
		mtx_destroy(&sc->lock);
		cs_release_resources(dev);
		return (error);
	}

	gone_by_fcp101_dev(dev);

	return (0);
}

int
cs_detach(device_t dev)
{
	struct cs_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->ifp;

	CS_LOCK(sc);
	cs_stop(sc);
	CS_UNLOCK(sc);
	callout_drain(&sc->timer);
	ether_ifdetach(ifp);
	bus_teardown_intr(dev, sc->irq_res, sc->irq_handle);
	cs_release_resources(dev);
	free(sc->buffer, M_DEVBUF);
	if_free(ifp);
	mtx_destroy(&sc->lock);
	return (0);
}

/*
 * Initialize the board
 */
static void
cs_init(void *xsc)
{
	struct cs_softc *sc=(struct cs_softc *)xsc;

	CS_LOCK(sc);
	cs_init_locked(sc);
	CS_UNLOCK(sc);
}

static void
cs_init_locked(struct cs_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	int i, rx_cfg;

	/*
	 * reset watchdog timer
	 */
	sc->tx_timeout = 0;
	sc->buf_len = 0;
	
	/*
	 * Hardware initialization of cs
	 */

	/* Enable receiver and transmitter */
	cs_writereg(sc, PP_LineCTL,
		cs_readreg(sc, PP_LineCTL) | SERIAL_RX_ON | SERIAL_TX_ON);

	/* Configure the receiver mode */
	cs_setmode(sc);

	/*
	 * This defines what type of frames will cause interrupts
	 * Bad frames should generate interrupts so that the driver
	 * could track statistics of discarded packets
	 */
	rx_cfg = RX_OK_ENBL | RX_CRC_ERROR_ENBL | RX_RUNT_ENBL |
		 RX_EXTRA_DATA_ENBL;
	if (sc->isa_config & STREAM_TRANSFER)
		rx_cfg |= RX_STREAM_ENBL;
	cs_writereg(sc, PP_RxCFG, rx_cfg);
	cs_writereg(sc, PP_TxCFG, TX_LOST_CRS_ENBL |
		    TX_SQE_ERROR_ENBL | TX_OK_ENBL | TX_LATE_COL_ENBL |
		    TX_JBR_ENBL | TX_ANY_COL_ENBL | TX_16_COL_ENBL);
	cs_writereg(sc, PP_BufCFG, READY_FOR_TX_ENBL |
		    RX_MISS_COUNT_OVRFLOW_ENBL | TX_COL_COUNT_OVRFLOW_ENBL |
		    TX_UNDERRUN_ENBL /*| RX_DMA_ENBL*/);

	/* Write MAC address into IA filter */
	for (i=0; i<ETHER_ADDR_LEN/2; i++)
		cs_writereg(sc, PP_IA + i * 2,
		    sc->enaddr[i * 2] |
		    (sc->enaddr[i * 2 + 1] << 8) );

	/*
	 * Now enable everything
	 */
/*
#ifdef	CS_USE_64K_DMA
	cs_writereg(sc, PP_BusCTL, ENABLE_IRQ | RX_DMA_SIZE_64K);
#else
	cs_writereg(sc, PP_BusCTL, ENABLE_IRQ);
#endif
*/
	cs_writereg(sc, PP_BusCTL, ENABLE_IRQ);
	
	/*
	 * Set running and clear output active flags
	 */
	sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
	sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	callout_reset(&sc->timer, hz, cs_watchdog, sc);

	/*
	 * Start sending process
	 */
	cs_start_locked(ifp);
}

/*
 * Get the packet from the board and send it to the upper layer.
 */
static int
cs_get_packet(struct cs_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	int status, length;
	struct mbuf *m;

#ifdef CS_DEBUG
	int i;
#endif

	status = cs_inw(sc, RX_FRAME_PORT);
	length = cs_inw(sc, RX_FRAME_PORT);

#ifdef CS_DEBUG
	device_printf(sc->dev, "rcvd: stat %x, len %d\n",
		status, length);
#endif

	if (!(status & RX_OK)) {
#ifdef CS_DEBUG
		device_printf(sc->dev, "bad pkt stat %x\n", status);
#endif
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		return (-1);
	}

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m==NULL)
		return (-1);

	if (length > MHLEN) {
		if (!(MCLGET(m, M_NOWAIT))) {
			m_freem(m);
			return (-1);
		}
	}

	/* Initialize packet's header info */
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = length;
	m->m_len = length;

	/* Get the data */
	bus_read_multi_2(sc->port_res, RX_FRAME_PORT, mtod(m, uint16_t *),
	    (length + 1) >> 1);

#ifdef CS_DEBUG
	for (i=0;i<length;i++)
	     printf(" %02x",(unsigned char)*((char *)(m->m_data+i)));
	printf( "\n" );
#endif

	if (status & (RX_IA | RX_BROADCAST) || 
	    (ifp->if_flags & IFF_MULTICAST && status & RX_HASHED)) {
		/* Feed the packet to the upper layer */
		(*ifp->if_input)(ifp, m);
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		if (length == ETHER_MAX_LEN-ETHER_CRC_LEN)
			DELAY(cs_recv_delay);
	} else {
		m_freem(m);
	}

	return (0);
}

/*
 * Handle interrupts
 */
void
csintr(void *arg)
{
	struct cs_softc *sc = (struct cs_softc*) arg;
	struct ifnet *ifp = sc->ifp;
	int status;

#ifdef CS_DEBUG
	device_printf(sc->dev, "Interrupt.\n");
#endif

	CS_LOCK(sc);
	while ((status=cs_inw(sc, ISQ_PORT))) {

#ifdef CS_DEBUG
		device_printf(sc->dev, "from ISQ: %04x\n", status);
#endif

		switch (status & ISQ_EVENT_MASK) {
		case ISQ_RECEIVER_EVENT:
			cs_get_packet(sc);
			break;

		case ISQ_TRANSMITTER_EVENT:
			if (status & TX_OK)
				if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			else
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
			sc->tx_timeout = 0;
			break;

		case ISQ_BUFFER_EVENT:
			if (status & READY_FOR_TX) {
				ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
				sc->tx_timeout = 0;
			}

			if (status & TX_UNDERRUN) {
				ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
				sc->tx_timeout = 0;
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			}
			break;

		case ISQ_RX_MISS_EVENT:
			if_inc_counter(ifp, IFCOUNTER_IERRORS, status >> 6);
			break;

		case ISQ_TX_COL_EVENT:
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS, status >> 6);
			break;
		}
	}

	if (!(ifp->if_drv_flags & IFF_DRV_OACTIVE)) {
		cs_start_locked(ifp);
	}
	CS_UNLOCK(sc);
}

/*
 * Save the data in buffer
 */

static void
cs_write_mbufs( struct cs_softc *sc, struct mbuf *m )
{
	int len;
	struct mbuf *mp;
	unsigned char *data, *buf;

	for (mp=m, buf=sc->buffer, sc->buf_len=0; mp != NULL; mp=mp->m_next) {
		len = mp->m_len;

		/*
		 * Ignore empty parts
		 */
		if (!len)
			continue;

		/*
		 * Find actual data address
		 */
		data = mtod(mp, caddr_t);

		bcopy((caddr_t) data, (caddr_t) buf, len);
		buf += len;
		sc->buf_len += len;
	}
}


static void
cs_xmit_buf( struct cs_softc *sc )
{
	bus_write_multi_2(sc->port_res, TX_FRAME_PORT, (uint16_t *)sc->buffer,
	    (sc->buf_len + 1) >> 1);
	sc->buf_len = 0;
}

static void
cs_start(struct ifnet *ifp)
{
	struct cs_softc *sc = ifp->if_softc;

	CS_LOCK(sc);
	cs_start_locked(ifp);
	CS_UNLOCK(sc);
}

static void
cs_start_locked(struct ifnet *ifp)
{
	int length;
	struct mbuf *m, *mp;
	struct cs_softc *sc = ifp->if_softc;

	for (;;) {
		if (sc->buf_len)
			length = sc->buf_len;
		else {
			IF_DEQUEUE( &ifp->if_snd, m );

			if (m==NULL) {
				return;
			}

			for (length=0, mp=m; mp != NULL; mp=mp->m_next)
				length += mp->m_len;

			/* Skip zero-length packets */
			if (length == 0) {
				m_freem(m);
				continue;
			}

			cs_write_mbufs(sc, m);

			BPF_MTAP(ifp, m);

			m_freem(m);
		}

		/*
		 * Issue a SEND command
		 */
		cs_outw(sc, TX_CMD_PORT, sc->send_cmd);
		cs_outw(sc, TX_LEN_PORT, length );

		/*
		 * If there's no free space in the buffer then leave
		 * this packet for the next time: indicate output active
		 * and return.
		 */
		if (!(cs_readreg(sc, PP_BusST) & READY_FOR_TX_NOW)) {
			sc->tx_timeout = sc->buf_len;
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			return;
		}

		cs_xmit_buf(sc);

		/*
		 * Set the watchdog timer in case we never hear
		 * from board again. (I don't know about correct
		 * value for this timeout)
		 */
		sc->tx_timeout = length;

		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		return;
	}
}

/*
 * Stop everything on the interface
 */
static void
cs_stop(struct cs_softc *sc)
{

	CS_ASSERT_LOCKED(sc);
	cs_writereg(sc, PP_RxCFG, 0);
	cs_writereg(sc, PP_TxCFG, 0);
	cs_writereg(sc, PP_BufCFG, 0);
	cs_writereg(sc, PP_BusCTL, 0);

	sc->ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->tx_timeout = 0;
	callout_stop(&sc->timer);
}

/*
 * Reset the interface
 */
static void
cs_reset(struct cs_softc *sc)
{

	CS_ASSERT_LOCKED(sc);
	cs_stop(sc);
	cs_init_locked(sc);
}

static uint16_t
cs_hash_index(struct sockaddr_dl *addr)
{
	uint32_t crc;
	uint16_t idx;
	caddr_t lla;

	lla = LLADDR(addr);
	crc = ether_crc32_le(lla, ETHER_ADDR_LEN);
	idx = crc >> 26;

	return (idx);
}

static void
cs_setmode(struct cs_softc *sc)
{
	int rx_ctl;
	uint16_t af[4];
	uint16_t port, mask, index;
	struct ifnet *ifp = sc->ifp;
	struct ifmultiaddr *ifma;

	/* Stop the receiver while changing filters */
	cs_writereg(sc, PP_LineCTL, cs_readreg(sc, PP_LineCTL) & ~SERIAL_RX_ON);

	if (ifp->if_flags & IFF_PROMISC) {
		/* Turn on promiscuous mode. */
		rx_ctl = RX_OK_ACCEPT | RX_PROM_ACCEPT;
	} else if (ifp->if_flags & IFF_MULTICAST) {
		/* Allow receiving frames with multicast addresses */
		rx_ctl = RX_IA_ACCEPT | RX_BROADCAST_ACCEPT |
			 RX_OK_ACCEPT | RX_MULTCAST_ACCEPT;

		/* Start with an empty filter */
		af[0] = af[1] = af[2] = af[3] = 0x0000;

		if (ifp->if_flags & IFF_ALLMULTI) {
			/* Accept all multicast frames */
			af[0] = af[1] = af[2] = af[3] = 0xffff;
		} else {
			/* 
			 * Set up the filter to only accept multicast
			 * frames we're interested in.
			 */
			if_maddr_rlock(ifp);
			CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
				struct sockaddr_dl *dl =
				    (struct sockaddr_dl *)ifma->ifma_addr;

				index = cs_hash_index(dl);
				port = (u_int16_t) (index >> 4);
				mask = (u_int16_t) (1 << (index & 0xf));
				af[port] |= mask;
			}
			if_maddr_runlock(ifp);
		}

		cs_writereg(sc, PP_LAF + 0, af[0]);
		cs_writereg(sc, PP_LAF + 2, af[1]);
		cs_writereg(sc, PP_LAF + 4, af[2]);
		cs_writereg(sc, PP_LAF + 6, af[3]);
	} else {
		/*
		 * Receive only good frames addressed for us and
		 * good broadcasts.
		 */
		rx_ctl = RX_IA_ACCEPT | RX_BROADCAST_ACCEPT |
			 RX_OK_ACCEPT;
	}

	/* Set up the filter */
	cs_writereg(sc, PP_RxCTL, RX_DEF_ACCEPT | rx_ctl);

	/* Turn on receiver */
	cs_writereg(sc, PP_LineCTL, cs_readreg(sc, PP_LineCTL) | SERIAL_RX_ON);
}

static int
cs_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct cs_softc *sc=ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error=0;

#ifdef CS_DEBUG
	if_printf(ifp, "%s command=%lx\n", __func__, command);
#endif

	switch (command) {
	case SIOCSIFFLAGS:
		/*
		 * Switch interface state between "running" and
		 * "stopped", reflecting the UP flag.
		 */
		CS_LOCK(sc);
		if (sc->ifp->if_flags & IFF_UP) {
			if ((sc->ifp->if_drv_flags & IFF_DRV_RUNNING)==0) {
				cs_init_locked(sc);
			}
		} else {
			if ((sc->ifp->if_drv_flags & IFF_DRV_RUNNING)!=0) {
				cs_stop(sc);
			}
		}		
		/*
		 * Promiscuous and/or multicast flags may have changed,
		 * so reprogram the multicast filter and/or receive mode.
		 *
		 * See note about multicasts in cs_setmode
		 */
		cs_setmode(sc);
		CS_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
	    /*
	     * Multicast list has changed; set the hardware filter
	     * accordingly.
	     *
	     * See note about multicasts in cs_setmode
	     */
	    CS_LOCK(sc);
	    cs_setmode(sc);
	    CS_UNLOCK(sc);
	    error = 0;
	    break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

/*
 * Device timeout/watchdog routine. Entered if the device neglects to
 * generate an interrupt after a transmit has been started on it.
 */
static void
cs_watchdog(void *arg)
{
	struct cs_softc *sc = arg;
	struct ifnet *ifp = sc->ifp;

	CS_ASSERT_LOCKED(sc);
	if (sc->tx_timeout && --sc->tx_timeout == 0) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		log(LOG_ERR, "%s: device timeout\n", ifp->if_xname);

		/* Reset the interface */
		if (ifp->if_flags & IFF_UP)
			cs_reset(sc);
		else
			cs_stop(sc);
	}
	callout_reset(&sc->timer, hz, cs_watchdog, sc);
}

static int
cs_mediachange(struct ifnet *ifp)
{
	struct cs_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->media;
	int error;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	CS_LOCK(sc);
	error = cs_mediaset(sc, ifm->ifm_media);
	CS_UNLOCK(sc);
	return (error);
}

static void
cs_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	int line_status;
	struct cs_softc *sc = ifp->if_softc;

	CS_LOCK(sc);
	ifmr->ifm_active = IFM_ETHER;
	line_status = cs_readreg(sc, PP_LineST);
	if (line_status & TENBASET_ON) {
		ifmr->ifm_active |= IFM_10_T;
		if (sc->chip_type != CS8900) {
			if (cs_readreg(sc, PP_AutoNegST) & FDX_ACTIVE)
				ifmr->ifm_active |= IFM_FDX;
			if (cs_readreg(sc, PP_AutoNegST) & HDX_ACTIVE)
				ifmr->ifm_active |= IFM_HDX;
		}
		ifmr->ifm_status = IFM_AVALID;
		if (line_status & LINK_OK)
			ifmr->ifm_status |= IFM_ACTIVE;
	} else {
		if (line_status & AUI_ON) {
			cs_writereg(sc, PP_SelfCTL, cs_readreg(sc, PP_SelfCTL) |
			    HCB1_ENBL);
			if (((sc->adapter_cnf & A_CNF_DC_DC_POLARITY)!=0)^
			    (cs_readreg(sc, PP_SelfCTL) & HCB1))
				ifmr->ifm_active |= IFM_10_2;
			else
				ifmr->ifm_active |= IFM_10_5;
		}
	}
	CS_UNLOCK(sc);
}

static int
cs_mediaset(struct cs_softc *sc, int media)
{
	int error = 0;

	/* Stop the receiver & transmitter */
	cs_writereg(sc, PP_LineCTL, cs_readreg(sc, PP_LineCTL) &
	    ~(SERIAL_RX_ON | SERIAL_TX_ON));

#ifdef CS_DEBUG
	device_printf(sc->dev, "%s media=%x\n", __func__, media);
#endif

	switch (IFM_SUBTYPE(media)) {
	default:
	case IFM_AUTO:
		/*
		 * This chip makes it a little hard to support this, so treat
		 * it as IFM_10_T, auto duplex.
		 */
		enable_tp(sc);
		cs_duplex_auto(sc);
		break;
	case IFM_10_T:
		enable_tp(sc);
		if (media & IFM_FDX)
			cs_duplex_full(sc);
		else if (media & IFM_HDX)
			cs_duplex_half(sc);
		else
			error = cs_duplex_auto(sc);
		break;
	case IFM_10_2:
		enable_bnc(sc);
		break;
	case IFM_10_5:
		enable_aui(sc);
		break;
	}

	/*
	 * Turn the transmitter & receiver back on
	 */
	cs_writereg(sc, PP_LineCTL, cs_readreg(sc, PP_LineCTL) |
	    SERIAL_RX_ON | SERIAL_TX_ON); 

	return (error);
}
