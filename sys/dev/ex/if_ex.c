/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996, Javier Martín Rueda (jmrueda@diatel.upm.es)
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * MAINTAINER: Matthew N. Dodd <winter@jurai.net>
 *                             <mdodd@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Intel EtherExpress Pro/10, Pro/10+ Ethernet driver
 *
 * Revision history:
 *
 * dd-mmm-yyyy: Multicast support ported from NetBSD's if_iy driver.
 * 30-Oct-1996: first beta version. Inet and BPF supported, but no multicast.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h> 
#include <net/if_types.h> 
#include <net/ethernet.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>


#include <isa/isavar.h>
#include <isa/pnpvar.h>

#include <dev/ex/if_exreg.h>
#include <dev/ex/if_exvar.h>

#ifdef EXDEBUG
# define Start_End 1
# define Rcvd_Pkts 2
# define Sent_Pkts 4
# define Status    8
static int debug_mask = 0;
# define DODEBUG(level, action) if (level & debug_mask) action
#else
# define DODEBUG(level, action)
#endif

devclass_t ex_devclass;

char irq2eemap[] =
	{ -1, -1, 0, 1, -1, 2, -1, -1, -1, 0, 3, 4, -1, -1, -1, -1 };
u_char ee2irqmap[] =
	{ 9, 3, 5, 10, 11, 0, 0, 0 };
                
char plus_irq2eemap[] =
	{ -1, -1, -1, 0, 1, 2, -1, 3, -1, 4, 5, 6, 7, -1, -1, -1 };
u_char plus_ee2irqmap[] =
	{ 3, 4, 5, 7, 9, 10, 11, 12 };

/* Network Interface Functions */
static void	ex_init(void *);
static void	ex_init_locked(struct ex_softc *);
static void	ex_start(struct ifnet *);
static void	ex_start_locked(struct ifnet *);
static int	ex_ioctl(struct ifnet *, u_long, caddr_t);
static void	ex_watchdog(void *);

/* ifmedia Functions	*/
static int	ex_ifmedia_upd(struct ifnet *);
static void	ex_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int	ex_get_media(struct ex_softc *);

static void	ex_reset(struct ex_softc *);
static void	ex_setmulti(struct ex_softc *);

static void	ex_tx_intr(struct ex_softc *);
static void	ex_rx_intr(struct ex_softc *);

void
ex_get_address(struct ex_softc *sc, u_char *enaddr)
{
	uint16_t	eaddr_tmp;

	eaddr_tmp = ex_eeprom_read(sc, EE_Eth_Addr_Lo);
	enaddr[5] = eaddr_tmp & 0xff;
	enaddr[4] = eaddr_tmp >> 8;
	eaddr_tmp = ex_eeprom_read(sc, EE_Eth_Addr_Mid);
	enaddr[3] = eaddr_tmp & 0xff;
	enaddr[2] = eaddr_tmp >> 8;
	eaddr_tmp = ex_eeprom_read(sc, EE_Eth_Addr_Hi);
	enaddr[1] = eaddr_tmp & 0xff;
	enaddr[0] = eaddr_tmp >> 8;
	
	return;
}

int
ex_card_type(u_char *enaddr)
{
	if ((enaddr[0] == 0x00) && (enaddr[1] == 0xA0) && (enaddr[2] == 0xC9))
		return (CARD_TYPE_EX_10_PLUS);

	return (CARD_TYPE_EX_10);
}

/*
 * Caller is responsible for eventually calling
 * ex_release_resources() on failure.
 */
int
ex_alloc_resources(device_t dev)
{
	struct ex_softc *	sc = device_get_softc(dev);
	int			error = 0;

	sc->ioport = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
					    &sc->ioport_rid, RF_ACTIVE);
	if (!sc->ioport) {
		device_printf(dev, "No I/O space?!\n");
		error = ENOMEM;
		goto bad;
	}

	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
					RF_ACTIVE);

	if (!sc->irq) {
		device_printf(dev, "No IRQ?!\n");
		error = ENOMEM;
		goto bad;
	}

bad:
	return (error);
}

void
ex_release_resources(device_t dev)
{
	struct ex_softc *	sc = device_get_softc(dev);

	if (sc->ih) {
		bus_teardown_intr(dev, sc->irq, sc->ih);
		sc->ih = NULL;
	}

	if (sc->ioport) {
		bus_release_resource(dev, SYS_RES_IOPORT,
					sc->ioport_rid, sc->ioport);
		sc->ioport = NULL;
	}

	if (sc->irq) {
		bus_release_resource(dev, SYS_RES_IRQ,
					sc->irq_rid, sc->irq);
		sc->irq = NULL;
	}

	if (sc->ifp)
		if_free(sc->ifp);

	return;
}

int
ex_attach(device_t dev)
{
	struct ex_softc *	sc = device_get_softc(dev);
	struct ifnet *		ifp;
	struct ifmedia *	ifm;
	int			error;
	uint16_t		temp;

	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		return (ENOSPC);
	}
	/* work out which set of irq <-> internal tables to use */
	if (ex_card_type(sc->enaddr) == CARD_TYPE_EX_10_PLUS) {
		sc->irq2ee = plus_irq2eemap;
		sc->ee2irq = plus_ee2irqmap;
	} else {
		sc->irq2ee = irq2eemap;
		sc->ee2irq = ee2irqmap;
	}

	sc->mem_size = CARD_RAM_SIZE;	/* XXX This should be read from the card itself. */

	/*
	 * Initialize the ifnet structure.
	 */
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_start = ex_start;
	ifp->if_ioctl = ex_ioctl;
	ifp->if_init = ex_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);

	ifmedia_init(&sc->ifmedia, 0, ex_ifmedia_upd, ex_ifmedia_sts);
	mtx_init(&sc->lock, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->timer, &sc->lock, 0);

	temp = ex_eeprom_read(sc, EE_W5);
	if (temp & EE_W5_PORT_TPE)
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T, 0, NULL);
	if (temp & EE_W5_PORT_BNC)
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_2, 0, NULL);
	if (temp & EE_W5_PORT_AUI)
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_5, 0, NULL);

	ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_AUTO, 0, NULL);
	ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_NONE, 0, NULL);
	ifmedia_set(&sc->ifmedia, ex_get_media(sc));

	ifm = &sc->ifmedia;
	ifm->ifm_media = ifm->ifm_cur->ifm_media;	
	ex_ifmedia_upd(ifp);

	/*
	 * Attach the interface.
	 */
	ether_ifattach(ifp, sc->enaddr);

	error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET | INTR_MPSAFE,
				NULL, ex_intr, (void *)sc, &sc->ih);
	if (error) {
		device_printf(dev, "bus_setup_intr() failed!\n");
		ether_ifdetach(ifp);
		mtx_destroy(&sc->lock);
		return (error);
	}

	gone_by_fcp101_dev(dev);

	return(0);
}

int
ex_detach(device_t dev)
{
	struct ex_softc	*sc;
	struct ifnet	*ifp;

	sc = device_get_softc(dev);
	ifp = sc->ifp;

	EX_LOCK(sc);
        ex_stop(sc);
	EX_UNLOCK(sc);

	ether_ifdetach(ifp);
	callout_drain(&sc->timer);

	ex_release_resources(dev);
	mtx_destroy(&sc->lock);

	return (0);
}

static void
ex_init(void *xsc)
{
	struct ex_softc *	sc = (struct ex_softc *) xsc;

	EX_LOCK(sc);
	ex_init_locked(sc);
	EX_UNLOCK(sc);
}

static void
ex_init_locked(struct ex_softc *sc)
{
	struct ifnet *		ifp = sc->ifp;
	int			i;
	unsigned short		temp_reg;

	DODEBUG(Start_End, printf("%s: ex_init: start\n", ifp->if_xname););

	sc->tx_timeout = 0;

	/*
	 * Load the ethernet address into the card.
	 */
	CSR_WRITE_1(sc, CMD_REG, Bank2_Sel);
	temp_reg = CSR_READ_1(sc, EEPROM_REG);
	if (temp_reg & Trnoff_Enable)
		CSR_WRITE_1(sc, EEPROM_REG, temp_reg & ~Trnoff_Enable);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, I_ADDR_REG0 + i, IF_LLADDR(sc->ifp)[i]);

	/*
	 * - Setup transmit chaining and discard bad received frames.
	 * - Match broadcast.
	 * - Clear test mode.
	 * - Set receiving mode.
	 */
	CSR_WRITE_1(sc, REG1, CSR_READ_1(sc, REG1) | Tx_Chn_Int_Md | Tx_Chn_ErStp | Disc_Bad_Fr);
	CSR_WRITE_1(sc, REG2, CSR_READ_1(sc, REG2) | No_SA_Ins | RX_CRC_InMem);
	CSR_WRITE_1(sc, REG3, CSR_READ_1(sc, REG3) & 0x3f /* XXX constants. */ );
	/*
	 * - Set IRQ number, if this part has it.  ISA devices have this,
	 * while PC Card devices don't seem to.  Either way, we have to
	 * switch to Bank1 as the rest of this code relies on that.
	 */
	CSR_WRITE_1(sc, CMD_REG, Bank1_Sel);
	if (sc->flags & HAS_INT_NO_REG)
		CSR_WRITE_1(sc, INT_NO_REG,
		    (CSR_READ_1(sc, INT_NO_REG) & 0xf8) |
		    sc->irq2ee[sc->irq_no]);

	/*
	 * Divide the available memory in the card into rcv and xmt buffers.
	 * By default, I use the first 3/4 of the memory for the rcv buffer,
	 * and the remaining 1/4 of the memory for the xmt buffer.
	 */
	sc->rx_mem_size = sc->mem_size * 3 / 4;
	sc->tx_mem_size = sc->mem_size - sc->rx_mem_size;
	sc->rx_lower_limit = 0x0000;
	sc->rx_upper_limit = sc->rx_mem_size - 2;
	sc->tx_lower_limit = sc->rx_mem_size;
	sc->tx_upper_limit = sc->mem_size - 2;
	CSR_WRITE_1(sc, RCV_LOWER_LIMIT_REG, sc->rx_lower_limit >> 8);
	CSR_WRITE_1(sc, RCV_UPPER_LIMIT_REG, sc->rx_upper_limit >> 8);
	CSR_WRITE_1(sc, XMT_LOWER_LIMIT_REG, sc->tx_lower_limit >> 8);
	CSR_WRITE_1(sc, XMT_UPPER_LIMIT_REG, sc->tx_upper_limit >> 8);
	
	/*
	 * Enable receive and transmit interrupts, and clear any pending int.
	 */
	CSR_WRITE_1(sc, REG1, CSR_READ_1(sc, REG1) | TriST_INT);
	CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);
	CSR_WRITE_1(sc, MASK_REG, All_Int & ~(Rx_Int | Tx_Int));
	CSR_WRITE_1(sc, STATUS_REG, All_Int);

	/*
	 * Initialize receive and transmit ring buffers.
	 */
	CSR_WRITE_2(sc, RCV_BAR, sc->rx_lower_limit);
	sc->rx_head = sc->rx_lower_limit;
	CSR_WRITE_2(sc, RCV_STOP_REG, sc->rx_upper_limit | 0xfe);
	CSR_WRITE_2(sc, XMT_BAR, sc->tx_lower_limit);
	sc->tx_head = sc->tx_tail = sc->tx_lower_limit;

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	DODEBUG(Status, printf("OIDLE init\n"););
	callout_reset(&sc->timer, hz, ex_watchdog, sc);
	
	ex_setmulti(sc);
	
	/*
	 * Final reset of the board, and enable operation.
	 */
	CSR_WRITE_1(sc, CMD_REG, Sel_Reset_CMD);
	DELAY(2);
	CSR_WRITE_1(sc, CMD_REG, Rcv_Enable_CMD);

	ex_start_locked(ifp);

	DODEBUG(Start_End, printf("%s: ex_init: finish\n", ifp->if_xname););
}

static void
ex_start(struct ifnet *ifp)
{
	struct ex_softc *	sc = ifp->if_softc;

	EX_LOCK(sc);
	ex_start_locked(ifp);
	EX_UNLOCK(sc);
}

static void
ex_start_locked(struct ifnet *ifp)
{
	struct ex_softc *	sc = ifp->if_softc;
	int			i, len, data_len, avail, dest, next;
	unsigned char		tmp16[2];
	struct mbuf *		opkt;
	struct mbuf *		m;

	DODEBUG(Start_End, printf("ex_start%d: start\n", unit););

	/*
	 * Main loop: send outgoing packets to network card until there are no
	 * more packets left, or the card cannot accept any more yet.
	 */
	while (((opkt = ifp->if_snd.ifq_head) != NULL) &&
	       !(ifp->if_drv_flags & IFF_DRV_OACTIVE)) {

		/*
		 * Ensure there is enough free transmit buffer space for
		 * this packet, including its header. Note: the header
		 * cannot wrap around the end of the transmit buffer and
		 * must be kept together, so we allow space for twice the
		 * length of the header, just in case.
		 */

		for (len = 0, m = opkt; m != NULL; m = m->m_next) {
			len += m->m_len;
		}

		data_len = len;

		DODEBUG(Sent_Pkts, printf("1. Sending packet with %d data bytes. ", data_len););

		if (len & 1) {
			len += XMT_HEADER_LEN + 1;
		} else {
			len += XMT_HEADER_LEN;
		}

		if ((i = sc->tx_tail - sc->tx_head) >= 0) {
			avail = sc->tx_mem_size - i;
		} else {
			avail = -i;
		}

		DODEBUG(Sent_Pkts, printf("i=%d, avail=%d\n", i, avail););

		if (avail >= len + XMT_HEADER_LEN) {
			IF_DEQUEUE(&ifp->if_snd, opkt);

#ifdef EX_PSA_INTR      
			/*
			 * Disable rx and tx interrupts, to avoid corruption
			 * of the host address register by interrupt service
			 * routines.
			 * XXX Is this necessary with splimp() enabled?
			 */
			CSR_WRITE_1(sc, MASK_REG, All_Int);
#endif

			/*
			 * Compute the start and end addresses of this
			 * frame in the tx buffer.
			 */
			dest = sc->tx_tail;
			next = dest + len;

			if (next > sc->tx_upper_limit) {
				if ((sc->tx_upper_limit + 2 - sc->tx_tail) <=
				    XMT_HEADER_LEN) {
					dest = sc->tx_lower_limit;
					next = dest + len;
				} else {
					next = sc->tx_lower_limit +
						next - sc->tx_upper_limit - 2;
				}
			}

			/*
			 * Build the packet frame in the card's ring buffer.
			 */
			DODEBUG(Sent_Pkts, printf("2. dest=%d, next=%d. ", dest, next););

			CSR_WRITE_2(sc, HOST_ADDR_REG, dest);
			CSR_WRITE_2(sc, IO_PORT_REG, Transmit_CMD);
			CSR_WRITE_2(sc, IO_PORT_REG, 0);
			CSR_WRITE_2(sc, IO_PORT_REG, next);
			CSR_WRITE_2(sc, IO_PORT_REG, data_len);

			/*
			 * Output the packet data to the card. Ensure all
			 * transfers are 16-bit wide, even if individual
			 * mbufs have odd length.
			 */
			for (m = opkt, i = 0; m != NULL; m = m->m_next) {
				DODEBUG(Sent_Pkts, printf("[%d]", m->m_len););
				if (i) {
					tmp16[1] = *(mtod(m, caddr_t));
					CSR_WRITE_MULTI_2(sc, IO_PORT_REG,
					    (uint16_t *) tmp16, 1);
				}
				CSR_WRITE_MULTI_2(sc, IO_PORT_REG,
				    (uint16_t *) (mtod(m, caddr_t) + i),
				    (m->m_len - i) / 2);
				if ((i = (m->m_len - i) & 1) != 0) {
					tmp16[0] = *(mtod(m, caddr_t) +
						   m->m_len - 1);
				}
			}
			if (i)
				CSR_WRITE_MULTI_2(sc, IO_PORT_REG, 
				    (uint16_t *) tmp16, 1);
			/*
			 * If there were other frames chained, update the
			 * chain in the last one.
			 */
			if (sc->tx_head != sc->tx_tail) {
				if (sc->tx_tail != dest) {
					CSR_WRITE_2(sc, HOST_ADDR_REG,
					     sc->tx_last + XMT_Chain_Point);
					CSR_WRITE_2(sc, IO_PORT_REG, dest);
				}
				CSR_WRITE_2(sc, HOST_ADDR_REG,
				     sc->tx_last + XMT_Byte_Count);
				i = CSR_READ_2(sc, IO_PORT_REG);
				CSR_WRITE_2(sc, HOST_ADDR_REG,
				     sc->tx_last + XMT_Byte_Count);
				CSR_WRITE_2(sc, IO_PORT_REG, i | Ch_bit);
			}
	
			/*
			 * Resume normal operation of the card:
			 * - Make a dummy read to flush the DRAM write
			 *   pipeline.
			 * - Enable receive and transmit interrupts.
			 * - Send Transmit or Resume_XMT command, as
			 *   appropriate.
			 */
			CSR_READ_2(sc, IO_PORT_REG);
#ifdef EX_PSA_INTR
			CSR_WRITE_1(sc, MASK_REG, All_Int & ~(Rx_Int | Tx_Int));
#endif
			if (sc->tx_head == sc->tx_tail) {
				CSR_WRITE_2(sc, XMT_BAR, dest);
				CSR_WRITE_1(sc, CMD_REG, Transmit_CMD);
				sc->tx_head = dest;
				DODEBUG(Sent_Pkts, printf("Transmit\n"););
			} else {
				CSR_WRITE_1(sc, CMD_REG, Resume_XMT_List_CMD);
				DODEBUG(Sent_Pkts, printf("Resume\n"););
			}
	
			sc->tx_last = dest;
			sc->tx_tail = next;
     	 
			BPF_MTAP(ifp, opkt);

			sc->tx_timeout = 2;
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			m_freem(opkt);
		} else {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			DODEBUG(Status, printf("OACTIVE start\n"););
		}
	}

	DODEBUG(Start_End, printf("ex_start%d: finish\n", unit););
}

void
ex_stop(struct ex_softc *sc)
{
	
	DODEBUG(Start_End, printf("ex_stop%d: start\n", unit););

	EX_ASSERT_LOCKED(sc);
	/*
	 * Disable card operation:
	 * - Disable the interrupt line.
	 * - Flush transmission and disable reception.
	 * - Mask and clear all interrupts.
	 * - Reset the 82595.
	 */
	CSR_WRITE_1(sc, CMD_REG, Bank1_Sel);
	CSR_WRITE_1(sc, REG1, CSR_READ_1(sc, REG1) & ~TriST_INT);
	CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);
	CSR_WRITE_1(sc, CMD_REG, Rcv_Stop);
	sc->tx_head = sc->tx_tail = sc->tx_lower_limit;
	sc->tx_last = 0; /* XXX I think these two lines are not necessary, because ex_init will always be called again to reinit the interface. */
	CSR_WRITE_1(sc, MASK_REG, All_Int);
	CSR_WRITE_1(sc, STATUS_REG, All_Int);
	CSR_WRITE_1(sc, CMD_REG, Reset_CMD);
	DELAY(200);
	sc->ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->tx_timeout = 0;
	callout_stop(&sc->timer);

	DODEBUG(Start_End, printf("ex_stop%d: finish\n", unit););

	return;
}

void
ex_intr(void *arg)
{
	struct ex_softc *sc = (struct ex_softc *)arg;
	struct ifnet 	*ifp = sc->ifp;
	int		int_status, send_pkts;
	int		loops = 100;

	DODEBUG(Start_End, printf("ex_intr%d: start\n", unit););

	EX_LOCK(sc);
	send_pkts = 0;
	while (loops-- > 0 &&
	    (int_status = CSR_READ_1(sc, STATUS_REG)) & (Tx_Int | Rx_Int)) {
		/* don't loop forever */
		if (int_status == 0xff)
			break;
		if (int_status & Rx_Int) {
			CSR_WRITE_1(sc, STATUS_REG, Rx_Int);
			ex_rx_intr(sc);
		} else if (int_status & Tx_Int) {
			CSR_WRITE_1(sc, STATUS_REG, Tx_Int);
			ex_tx_intr(sc);
			send_pkts = 1;
		}
	}
	if (loops == 0)
		printf("100 loops are not enough\n");

	/*
	 * If any packet has been transmitted, and there are queued packets to
	 * be sent, attempt to send more packets to the network card.
	 */
	if (send_pkts && (ifp->if_snd.ifq_head != NULL))
		ex_start_locked(ifp);
	EX_UNLOCK(sc);

	DODEBUG(Start_End, printf("ex_intr%d: finish\n", unit););

	return;
}

static void
ex_tx_intr(struct ex_softc *sc)
{
	struct ifnet *	ifp = sc->ifp;
	int		tx_status;

	DODEBUG(Start_End, printf("ex_tx_intr%d: start\n", unit););

	/*
	 * - Cancel the watchdog.
	 * For all packets transmitted since last transmit interrupt:
	 * - Advance chain pointer to next queued packet.
	 * - Update statistics.
	 */

	sc->tx_timeout = 0;

	while (sc->tx_head != sc->tx_tail) {
		CSR_WRITE_2(sc, HOST_ADDR_REG, sc->tx_head);

		if (!(CSR_READ_2(sc, IO_PORT_REG) & Done_bit))
			break;

		tx_status = CSR_READ_2(sc, IO_PORT_REG);
		sc->tx_head = CSR_READ_2(sc, IO_PORT_REG);

		if (tx_status & TX_OK_bit) {
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		} else {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		}

		if_inc_counter(ifp, IFCOUNTER_COLLISIONS, tx_status & No_Collisions_bits);
	}

	/*
	 * The card should be ready to accept more packets now.
	 */

	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	DODEBUG(Status, printf("OIDLE tx_intr\n"););
	DODEBUG(Start_End, printf("ex_tx_intr%d: finish\n", unit););

	return;
}

static void
ex_rx_intr(struct ex_softc *sc)
{
	struct ifnet *		ifp = sc->ifp;
	int			rx_status;
	int			pkt_len;
	int			QQQ;
	struct mbuf *		m;
	struct mbuf *		ipkt;
	struct ether_header *	eh;

	DODEBUG(Start_End, printf("ex_rx_intr%d: start\n", unit););

	/*
	 * For all packets received since last receive interrupt:
	 * - If packet ok, read it into a new mbuf and queue it to interface,
	 *   updating statistics.
	 * - If packet bad, just discard it, and update statistics.
	 * Finally, advance receive stop limit in card's memory to new location.
	 */

	CSR_WRITE_2(sc, HOST_ADDR_REG, sc->rx_head);

	while (CSR_READ_2(sc, IO_PORT_REG) == RCV_Done) {

		rx_status = CSR_READ_2(sc, IO_PORT_REG);
		sc->rx_head = CSR_READ_2(sc, IO_PORT_REG);
		QQQ = pkt_len = CSR_READ_2(sc, IO_PORT_REG);

		if (rx_status & RCV_OK_bit) {
			MGETHDR(m, M_NOWAIT, MT_DATA);
			ipkt = m;
			if (ipkt == NULL) {
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			} else {
				ipkt->m_pkthdr.rcvif = ifp;
				ipkt->m_pkthdr.len = pkt_len;
				ipkt->m_len = MHLEN;

				while (pkt_len > 0) {
					if (pkt_len >= MINCLSIZE) {
						if (MCLGET(m, M_NOWAIT)) {
							m->m_len = MCLBYTES;
						} else {
							m_freem(ipkt);
							if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
							goto rx_another;
						}
					}
					m->m_len = min(m->m_len, pkt_len);

	  /*
	   * NOTE: I'm assuming that all mbufs allocated are of even length,
	   * except for the last one in an odd-length packet.
	   */

					CSR_READ_MULTI_2(sc, IO_PORT_REG,
					    mtod(m, uint16_t *), m->m_len / 2);

					if (m->m_len & 1) {
						*(mtod(m, caddr_t) + m->m_len - 1) = CSR_READ_1(sc, IO_PORT_REG);
					}
					pkt_len -= m->m_len;

					if (pkt_len > 0) {
						MGET(m->m_next, M_NOWAIT, MT_DATA);
						if (m->m_next == NULL) {
							m_freem(ipkt);
							if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
							goto rx_another;
						}
						m = m->m_next;
						m->m_len = MLEN;
					}
				}
				eh = mtod(ipkt, struct ether_header *);
#ifdef EXDEBUG
	if (debug_mask & Rcvd_Pkts) {
		if ((eh->ether_dhost[5] != 0xff) || (eh->ether_dhost[0] != 0xff)) {
			printf("Receive packet with %d data bytes: %6D -> ", QQQ, eh->ether_shost, ":");
			printf("%6D\n", eh->ether_dhost, ":");
		} /* QQQ */
	}
#endif
				EX_UNLOCK(sc);
				(*ifp->if_input)(ifp, ipkt);
				EX_LOCK(sc);
				if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
			}
		} else {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		}
		CSR_WRITE_2(sc, HOST_ADDR_REG, sc->rx_head);
rx_another: ;
	}

	if (sc->rx_head < sc->rx_lower_limit + 2)
		CSR_WRITE_2(sc, RCV_STOP_REG, sc->rx_upper_limit);
	else
		CSR_WRITE_2(sc, RCV_STOP_REG, sc->rx_head - 2);

	DODEBUG(Start_End, printf("ex_rx_intr%d: finish\n", unit););

	return;
}


static int
ex_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ex_softc *	sc = ifp->if_softc;
	struct ifreq *		ifr = (struct ifreq *)data;
	int			error = 0;

	DODEBUG(Start_End, printf("%s: ex_ioctl: start ", ifp->if_xname););

	switch(cmd) {
		case SIOCSIFFLAGS:
			DODEBUG(Start_End, printf("SIOCSIFFLAGS"););
			EX_LOCK(sc);
			if ((ifp->if_flags & IFF_UP) == 0 &&
			    (ifp->if_drv_flags & IFF_DRV_RUNNING)) {
				ex_stop(sc);
			} else {
      				ex_init_locked(sc);
			}
			EX_UNLOCK(sc);
			break;
		case SIOCADDMULTI:
		case SIOCDELMULTI:
			ex_init(sc);
			error = 0;
			break;
		case SIOCSIFMEDIA:
		case SIOCGIFMEDIA:
			error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, cmd);
			break;
		default:
			error = ether_ioctl(ifp, cmd, data);
			break;
	}

	DODEBUG(Start_End, printf("\n%s: ex_ioctl: finish\n", ifp->if_xname););

	return(error);
}

static void
ex_setmulti(struct ex_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *maddr;
	uint16_t *addr;
	int count;
	int timeout, status;
	
	ifp = sc->ifp;

	count = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(maddr, &ifp->if_multiaddrs, ifma_link) {
		if (maddr->ifma_addr->sa_family != AF_LINK)
			continue;
		count++;
	}
	if_maddr_runlock(ifp);

	if ((ifp->if_flags & IFF_PROMISC) || (ifp->if_flags & IFF_ALLMULTI)
			|| count > 63) {
		/* Interface is in promiscuous mode or there are too many
		 * multicast addresses for the card to handle */
		CSR_WRITE_1(sc, CMD_REG, Bank2_Sel);
		CSR_WRITE_1(sc, REG2, CSR_READ_1(sc, REG2) | Promisc_Mode);
		CSR_WRITE_1(sc, REG3, CSR_READ_1(sc, REG3));
		CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);
	}
	else if ((ifp->if_flags & IFF_MULTICAST) && (count > 0)) {
		/* Program multicast addresses plus our MAC address
		 * into the filter */
		CSR_WRITE_1(sc, CMD_REG, Bank2_Sel);
		CSR_WRITE_1(sc, REG2, CSR_READ_1(sc, REG2) | Multi_IA);
		CSR_WRITE_1(sc, REG3, CSR_READ_1(sc, REG3));
		CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);

		/* Borrow space from TX buffer; this should be safe
		 * as this is only called from ex_init */
		
		CSR_WRITE_2(sc, HOST_ADDR_REG, sc->tx_lower_limit);
		CSR_WRITE_2(sc, IO_PORT_REG, MC_Setup_CMD);
		CSR_WRITE_2(sc, IO_PORT_REG, 0);
		CSR_WRITE_2(sc, IO_PORT_REG, 0);
		CSR_WRITE_2(sc, IO_PORT_REG, (count + 1) * 6);

		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(maddr, &ifp->if_multiaddrs, ifma_link) {
			if (maddr->ifma_addr->sa_family != AF_LINK)
				continue;

			addr = (uint16_t*)LLADDR((struct sockaddr_dl *)
					maddr->ifma_addr);
			CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
			CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
			CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
		}
		if_maddr_runlock(ifp);

		/* Program our MAC address as well */
		/* XXX: Is this necessary?  The Linux driver does this
		 * but the NetBSD driver does not */
		addr = (uint16_t*)IF_LLADDR(sc->ifp);
		CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
		CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
		CSR_WRITE_2(sc, IO_PORT_REG, *addr++);

		CSR_READ_2(sc, IO_PORT_REG);
		CSR_WRITE_2(sc, XMT_BAR, sc->tx_lower_limit);
		CSR_WRITE_1(sc, CMD_REG, MC_Setup_CMD);

		sc->tx_head = sc->tx_lower_limit;
		sc->tx_tail = sc->tx_head + XMT_HEADER_LEN + (count + 1) * 6;

		for (timeout=0; timeout<100; timeout++) {
			DELAY(2);
			if ((CSR_READ_1(sc, STATUS_REG) & Exec_Int) == 0)
				continue;

			status = CSR_READ_1(sc, CMD_REG);
			CSR_WRITE_1(sc, STATUS_REG, Exec_Int);
			break;
		}

		sc->tx_head = sc->tx_tail;
	}
	else
	{
		/* No multicast or promiscuous mode */
		CSR_WRITE_1(sc, CMD_REG, Bank2_Sel);
		CSR_WRITE_1(sc, REG2, CSR_READ_1(sc, REG2) & 0xDE);
			/* ~(Multi_IA | Promisc_Mode) */
		CSR_WRITE_1(sc, REG3, CSR_READ_1(sc, REG3));
		CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);
	}
}

static void
ex_reset(struct ex_softc *sc)
{

	DODEBUG(Start_End, printf("ex_reset%d: start\n", unit););

	EX_ASSERT_LOCKED(sc);
	ex_stop(sc);
	ex_init_locked(sc);

	DODEBUG(Start_End, printf("ex_reset%d: finish\n", unit););

	return;
}

static void
ex_watchdog(void *arg)
{
	struct ex_softc *	sc = arg;
	struct ifnet *ifp = sc->ifp;

	if (sc->tx_timeout && --sc->tx_timeout == 0) {
		DODEBUG(Start_End, if_printf(ifp, "ex_watchdog: start\n"););

		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		DODEBUG(Status, printf("OIDLE watchdog\n"););

		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ex_reset(sc);
		ex_start_locked(ifp);

		DODEBUG(Start_End, if_printf(ifp, "ex_watchdog: finish\n"););
	}

	callout_reset(&sc->timer, hz, ex_watchdog, sc);
}

static int
ex_get_media(struct ex_softc *sc)
{
	int	current;
	int	media;

	media = ex_eeprom_read(sc, EE_W5);

	CSR_WRITE_1(sc, CMD_REG, Bank2_Sel);
	current = CSR_READ_1(sc, REG3);
	CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);

	if ((current & TPE_bit) && (media & EE_W5_PORT_TPE))
		return(IFM_ETHER|IFM_10_T);
	if ((current & BNC_bit) && (media & EE_W5_PORT_BNC))
		return(IFM_ETHER|IFM_10_2);

	if (media & EE_W5_PORT_AUI)
		return (IFM_ETHER|IFM_10_5);

	return (IFM_ETHER|IFM_AUTO);
}

static int
ex_ifmedia_upd(ifp)
	struct ifnet *		ifp;
{
	struct ex_softc *       sc = ifp->if_softc;

	if (IFM_TYPE(sc->ifmedia.ifm_media) != IFM_ETHER)
		return EINVAL;

	return (0);
}

static void
ex_ifmedia_sts(ifp, ifmr)
	struct ifnet *          ifp;
	struct ifmediareq *     ifmr;
{
	struct ex_softc *       sc = ifp->if_softc;

	EX_LOCK(sc);
	ifmr->ifm_active = ex_get_media(sc);
	ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;
	EX_UNLOCK(sc);

	return;
}

u_short
ex_eeprom_read(struct ex_softc *sc, int location)
{
	int i;
	u_short data = 0;
	int read_cmd = location | EE_READ_CMD;
	short ctrl_val = EECS;

	CSR_WRITE_1(sc, CMD_REG, Bank2_Sel);
	CSR_WRITE_1(sc, EEPROM_REG, EECS);
	for (i = 8; i >= 0; i--) {
		short outval = (read_cmd & (1 << i)) ? ctrl_val | EEDI : ctrl_val;
		CSR_WRITE_1(sc, EEPROM_REG, outval);
		CSR_WRITE_1(sc, EEPROM_REG, outval | EESK);
		DELAY(3);
		CSR_WRITE_1(sc, EEPROM_REG, outval);
		DELAY(2);
	}
	CSR_WRITE_1(sc, EEPROM_REG, ctrl_val);

	for (i = 16; i > 0; i--) {
		CSR_WRITE_1(sc, EEPROM_REG, ctrl_val | EESK);
		DELAY(3);
		data = (data << 1) | 
		    ((CSR_READ_1(sc, EEPROM_REG) & EEDO) ? 1 : 0);
		CSR_WRITE_1(sc, EEPROM_REG, ctrl_val);
		DELAY(2);
	}

	ctrl_val &= ~EECS;
	CSR_WRITE_1(sc, EEPROM_REG, ctrl_val | EESK);
	DELAY(3);
	CSR_WRITE_1(sc, EEPROM_REG, ctrl_val);
	DELAY(2);
	CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);
	return(data);
}
