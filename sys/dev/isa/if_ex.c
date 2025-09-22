/*	$OpenBSD: if_ex.c,v 1.50 2024/06/22 10:22:29 jsg Exp $	*/
/*
 * Copyright (c) 1997, Donald A. Schmidt
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
 */

/*
 * Intel EtherExpress Pro/10 Ethernet driver
 *
 * Revision history:
 *
 * 30-Oct-1996: first beta version. Inet and BPF supported, but no multicast.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h> 

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/cpu.h>
#include <machine/bus.h>

#include <dev/isa/isavar.h>
#include <dev/isa/if_exreg.h>

#ifdef EX_DEBUG
#define Start_End 1
#define Rcvd_Pkts 2
#define Sent_Pkts 4
#define Status    8
static int debug_mask = 0;
static int exintr_count = 0;
#define DODEBUG(level, action) if (level & debug_mask) action
#else
#define DODEBUG(level, action)
#endif

struct ex_softc {
  	struct arpcom arpcom;	/* Ethernet common data */
	struct ifmedia ifmedia;
	int iobase;		/* I/O base address. */
	u_short irq_no; 	/* IRQ number. */
	u_int mem_size;		/* Total memory size, in bytes. */
	u_int rx_mem_size;	/* Rx memory size (by default, first 3/4 of 
				   total memory). */
  	u_int rx_lower_limit, 
	      rx_upper_limit; 	/* Lower and upper limits of receive buffer. */
  	u_int rx_head; 		/* Head of receive ring buffer. */
	u_int tx_mem_size;	/* Tx memory size (by default, last quarter of 
				   total memory). */
  	u_int tx_lower_limit, 
	      tx_upper_limit;	/* Lower and upper limits of transmit buffer. */
  	u_int tx_head, tx_tail; /* Head and tail of transmit ring buffer. */
  	u_int tx_last; 		/* Pointer to beginning of last frame in the 
				   chain. */
	bus_space_tag_t sc_iot;	/* ISA i/o space tag */
	bus_space_handle_t sc_ioh; /* ISA i/o space handle */
	void *sc_ih;		/* Device interrupt handler */
};

static char irq2eemap[] = { -1, -1, 0, 1, -1, 2, -1, -1, -1, 0, 3, 4, -1, -1, 
			    -1, -1 };
static u_char ee2irqmap[] = { 9, 3, 5, 10, 11, 0, 0, 0 };

int ex_probe(struct device *, void *, void *);
void ex_attach(struct device *, struct device *, void *);
void ex_init(struct ex_softc *);
void ex_start(struct ifnet *);
void ex_stop(struct ex_softc *);
int ex_ioctl(struct ifnet *, u_long, caddr_t);
void ex_setmulti(struct ex_softc *);
void ex_reset(struct ex_softc *);
void ex_watchdog(struct ifnet *);
uint64_t ex_get_media(struct ex_softc *);

int ex_ifmedia_upd(struct ifnet *);
void ex_ifmedia_sts(struct ifnet *, struct ifmediareq *);

u_short ex_eeprom_read(struct ex_softc *, int);
int ex_look_for_card(struct isa_attach_args *, struct ex_softc *sc);

int ex_intr(void *);
void ex_tx_intr(struct ex_softc *);
void ex_rx_intr(struct ex_softc *);

const struct cfattach ex_ca = {
	sizeof(struct ex_softc), ex_probe, ex_attach
};

struct cfdriver ex_cd = {
	NULL, "ex", DV_IFNET
};

#define CSR_READ_1(sc, off) \
	bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, (off))
#define CSR_READ_2(sc, off) \
	bus_space_read_2((sc)->sc_iot, (sc)->sc_ioh, (off))
#define CSR_READ_MULTI_2(sc, off, addr, count) \
	bus_space_read_multi_2((sc)->sc_iot, (sc)->sc_ioh, (off),	\
	    (u_int16_t *)(addr), (count))

#define CSR_WRITE_1(sc, off, value) \
	bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, (off), (value))
#define CSR_WRITE_2(sc, off, value) \
	bus_space_write_2((sc)->sc_iot, (sc)->sc_ioh, (off), (value))
#define CSR_WRITE_MULTI_2(sc, off, addr, count) \
	bus_space_write_multi_2((sc)->sc_iot, (sc)->sc_ioh, (off),	\
	    (u_int16_t *)(addr), (count))

int 
ex_look_for_card(struct isa_attach_args *ia, struct ex_softc *sc)
{
	int count1, count2;

	/*
	 * Check for the i82595 signature, and check that the round robin
	 * counter actually advances.
	 */
	if (((count1 = CSR_READ_1(sc, ID_REG)) & Id_Mask) != Id_Sig)
		return(0);
	count2 = CSR_READ_1(sc, ID_REG);
	count2 = CSR_READ_1(sc, ID_REG);
	count2 = CSR_READ_1(sc, ID_REG);
	if ((count2 & Counter_bits) == ((count1 + 0xc0) & Counter_bits))
		return(1);
	else
		return(0);
}

int 
ex_probe(struct device *parent, void *match, void *aux)
{
	struct ex_softc *sc = match;
	struct isa_attach_args *ia = aux;
	u_short eaddr_tmp;
	int tmp;

	DODEBUG(Start_End, printf("ex_probe: start\n"););

	if ((ia->ia_iobase >= 0x200) && (ia->ia_iobase <= 0x3a0)) {
		sc->sc_iot = ia->ia_iot;
		if(bus_space_map(sc->sc_iot, ia->ia_iobase, EX_IOSIZE, 0,
		    &sc->sc_ioh))
			return(0);

		if (!ex_look_for_card(ia, sc)) {
			bus_space_unmap(sc->sc_iot, sc->sc_ioh, EX_IOSIZE);
			return(0); 
		}
	} else
		return(0);

	ia->ia_iosize = EX_IOSIZE;

	/*
	 * Reset the card.
	 */
	CSR_WRITE_1(sc, CMD_REG, Reset_CMD);
	delay(200);

	/*
	 * Fill in several fields of the softc structure:
	 *	- I/O base address.
	 *	- Hardware Ethernet address.
	 *	- IRQ number (if not supplied in config file, read it from 
	 *	  EEPROM).
	 */
	sc->iobase = ia->ia_iobase;
	eaddr_tmp = ex_eeprom_read(sc, EE_Eth_Addr_Lo);
	sc->arpcom.ac_enaddr[5] = eaddr_tmp & 0xff;
	sc->arpcom.ac_enaddr[4] = eaddr_tmp >> 8;
	eaddr_tmp = ex_eeprom_read(sc, EE_Eth_Addr_Mid);
	sc->arpcom.ac_enaddr[3] = eaddr_tmp & 0xff;
	sc->arpcom.ac_enaddr[2] = eaddr_tmp >> 8;
	eaddr_tmp = ex_eeprom_read(sc, EE_Eth_Addr_Hi);
	sc->arpcom.ac_enaddr[1] = eaddr_tmp & 0xff;
	sc->arpcom.ac_enaddr[0] = eaddr_tmp >> 8;
	tmp = ex_eeprom_read(sc, EE_IRQ_No) & IRQ_No_Mask;
	if (ia->ia_irq > 0) {
		if (ee2irqmap[tmp] != ia->ia_irq)
			printf("ex: WARNING: board's EEPROM is configured for IRQ %d, using %d\n", ee2irqmap[tmp], ia->ia_irq);
		sc->irq_no = ia->ia_irq;
	}
	else {
		sc->irq_no = ee2irqmap[tmp];
		ia->ia_irq = sc->irq_no;
	}
	if (sc->irq_no == 0) {
		printf("ex: invalid IRQ.\n");
		return(0);
	}

	sc->mem_size = CARD_RAM_SIZE;	/* XXX This should be read from the card
					       itself. */

	DODEBUG(Start_End, printf("ex_probe: finish\n"););
	return(1);
}

void
ex_attach(struct device *parent, struct device *self, void *aux)
{
	struct ex_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmedia *ifm;
	int temp;

	DODEBUG(Start_End, printf("ex_attach: start\n"););

	ifp->if_softc = sc;
	bcopy(self->dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_start = ex_start;
	ifp->if_ioctl = ex_ioctl;
	ifp->if_watchdog = ex_watchdog;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;

	ifmedia_init(&sc->ifmedia, 0, ex_ifmedia_upd, ex_ifmedia_sts);

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

	if_attach(ifp);
	ether_ifattach(ifp);
	printf(": address %s\n",
	    ether_sprintf(sc->arpcom.ac_enaddr));

	sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_NET, ex_intr, sc, self->dv_xname);
	ex_init(sc);

	DODEBUG(Start_End, printf("ex_attach: finish\n"););
}

void 
ex_init(struct ex_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int s, i;
	unsigned short temp_reg;

	DODEBUG(Start_End, printf("ex_init: start\n"););

	s = splnet();
	sc->arpcom.ac_if.if_timer = 0;

	/*
	 * Load the ethernet address into the card.
	 */
	CSR_WRITE_1(sc, CMD_REG, Bank2_Sel);
	temp_reg = CSR_READ_1(sc, EEPROM_REG);
	if (temp_reg & Trnoff_Enable)
		CSR_WRITE_1(sc, EEPROM_REG, temp_reg & ~Trnoff_Enable);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, I_ADDR_REG0 + i, sc->arpcom.ac_enaddr[i]);
	/*
	 * - Setup transmit chaining and discard bad received frames.
	 * - Match broadcast.
	 * - Clear test mode.
	 * - Set receiving mode.
	 * - Set IRQ number.
	 */
	CSR_WRITE_1(sc, REG1, CSR_READ_1(sc, REG1) | Tx_Chn_Int_Md |
	    Tx_Chn_ErStp | Disc_Bad_Fr);
	CSR_WRITE_1(sc, REG2, CSR_READ_1(sc, REG2) | No_SA_Ins |
	    RX_CRC_InMem);
	CSR_WRITE_1(sc, REG3, (CSR_READ_1(sc, REG3) & 0x3f));
	CSR_WRITE_1(sc, CMD_REG, Bank1_Sel);
	CSR_WRITE_1(sc, INT_NO_REG, (CSR_READ_1(sc, INT_NO_REG) & 0xf8) | 
	    irq2eemap[sc->irq_no]);

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

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	DODEBUG(Status, printf("OIDLE init\n"););

	ex_setmulti(sc);

	/*
	 * Final reset of the board, and enable operation.
	 */
	CSR_WRITE_1(sc, CMD_REG, Sel_Reset_CMD);
	delay(2);
	CSR_WRITE_1(sc, CMD_REG, Rcv_Enable_CMD);

	ex_start(ifp);
	splx(s);

	DODEBUG(Start_End, printf("ex_init: finish\n"););
}

void 
ex_start(struct ifnet *ifp)
{
	struct ex_softc *sc = ifp->if_softc;
	int i, len, data_len, avail, dest, next;
	unsigned char tmp16[2];
	struct mbuf *opkt;
	struct mbuf *m;

	DODEBUG(Start_End, printf("ex_start: start\n"););

	/*
 	 * Main loop: send outgoing packets to network card until there are no
 	 * more packets left, or the card cannot accept any more yet.
 	 */
	while (!ifq_is_oactive(&ifp->if_snd)) {
		opkt = ifq_deq_begin(&ifp->if_snd);
		if (opkt == NULL)
			break;

		/*
		 * Ensure there is enough free transmit buffer space for this 
		 * packet, including its header. Note: the header cannot wrap 
		 * around the end of the transmit buffer and must be kept 
		 * together, so we allow space for twice the length of the 
		 * header, just in case.
		 */
		for (len = 0, m = opkt; m != NULL; m = m->m_next)
 			len += m->m_len;
    		data_len = len;
   		DODEBUG(Sent_Pkts, printf("1. Sending packet with %d data bytes. ", data_len););
		if (len & 1)
   			len += XMT_HEADER_LEN + 1;
		else
			len += XMT_HEADER_LEN;
		if ((i = sc->tx_tail - sc->tx_head) >= 0)
			avail = sc->tx_mem_size - i;
		else
			avail = -i;
		DODEBUG(Sent_Pkts, printf("i=%d, avail=%d\n", i, avail););
    		if (avail >= len + XMT_HEADER_LEN) {
      			ifq_deq_commit(&ifp->if_snd, opkt);

#ifdef EX_PSA_INTR      
			/*
 			 * Disable rx and tx interrupts, to avoid corruption of
			 * the host address register by interrupt service 
			 * routines. XXX Is this necessary with splnet() 
			 * enabled?
			 */
			CSR_WRITE_2(sc, MASK_REG, All_Int);
#endif

      			/* 
			 * Compute the start and end addresses of this frame 
			 * in the tx buffer.
			 */
      			dest = sc->tx_tail;
			next = dest + len;
			if (next > sc->tx_upper_limit) {
				if ((sc->tx_upper_limit + 2 - sc->tx_tail) <= 
				    XMT_HEADER_LEN) {
	  				dest = sc->tx_lower_limit;
	  				next = dest + len;
				} else
	  				next = sc->tx_lower_limit + next - 
					    sc->tx_upper_limit - 2;
      			}

			/* Build the packet frame in the card's ring buffer. */
			DODEBUG(Sent_Pkts, printf("2. dest=%d, next=%d. ", dest, next););
			CSR_WRITE_2(sc, HOST_ADDR_REG, dest);
			CSR_WRITE_2(sc, IO_PORT_REG, Transmit_CMD);
			CSR_WRITE_2(sc, IO_PORT_REG, 0);
			CSR_WRITE_2(sc, IO_PORT_REG, next);
			CSR_WRITE_2(sc, IO_PORT_REG, data_len);

			/*
 			 * Output the packet data to the card. Ensure all 
			 * transfers are 16-bit wide, even if individual mbufs 
			 * have odd length.
			 */

			for (m = opkt, i = 0; m != NULL; m = m->m_next) {
				DODEBUG(Sent_Pkts, printf("[%d]", m->m_len););
				if (i) {
					tmp16[1] = *(mtod(m, caddr_t));
					CSR_WRITE_MULTI_2(sc, IO_PORT_REG, tmp16, 1);
				}
				CSR_WRITE_MULTI_2(sc, IO_PORT_REG, mtod(m, caddr_t) 
				    + i, (m->m_len - i) / 2);
				if ((i = (m->m_len - i) & 1))
					tmp16[0] = *(mtod(m, caddr_t) + 
					    m->m_len - 1);
			}
			if (i)
				CSR_WRITE_MULTI_2(sc, IO_PORT_REG, tmp16, 1);

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
				CSR_WRITE_2(sc, HOST_ADDR_REG, sc->tx_last + 
				    XMT_Byte_Count);
				i = CSR_READ_2(sc, IO_PORT_REG);
				CSR_WRITE_2(sc, HOST_ADDR_REG, sc->tx_last + 
				    XMT_Byte_Count);
				CSR_WRITE_2(sc, IO_PORT_REG, i | Ch_bit);
      			}

      			/*
			 * Resume normal operation of the card:
			 * -Make a dummy read to flush the DRAM write pipeline.
			 * -Enable receive and transmit interrupts.
			 * -Send Transmit or Resume_XMT command, as appropriate.
			 */
			CSR_READ_2(sc, IO_PORT_REG);
#ifdef EX_PSA_INTR
			CSR_WRITE_2(sc, MASK_REG, All_Int & ~(Rx_Int | Tx_Int));
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
#if NBPFILTER > 0
			if (ifp->if_bpf != NULL)
				bpf_mtap(ifp->if_bpf, opkt,
				    BPF_DIRECTION_OUT);
#endif
			ifp->if_timer = 2;
			m_freem(opkt);
		} else {
			ifq_deq_rollback(&ifp->if_snd, opkt);
			ifq_set_oactive(&ifp->if_snd);
			DODEBUG(Status, printf("OACTIVE start\n"););
		}
	}

	DODEBUG(Start_End, printf("ex_start: finish\n"););
}

void 
ex_stop(struct ex_softc *sc)
{
	DODEBUG(Start_End, printf("ex_stop: start\n"););

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
	sc->tx_last = 0; /* XXX I think these two lines are not necessary, 
				because ex_init will always be called again 
				to reinit the interface. */
	CSR_WRITE_1(sc, MASK_REG, All_Int);
	CSR_WRITE_1(sc, STATUS_REG, All_Int);
	CSR_WRITE_1(sc, CMD_REG, Reset_CMD);
	delay(200);

	DODEBUG(Start_End, printf("ex_stop: finish\n"););
}


int 
ex_intr(void *arg)
{
	struct ex_softc *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int int_status, send_pkts;
	int handled = 0;

	DODEBUG(Start_End, printf("exintr: start\n"););

#ifdef EX_DEBUG
	if (++exintr_count != 1)
		printf("WARNING: nested interrupt (%d). Mail the author.\n", 
	 	    exintr_count);
#endif

	send_pkts = 0;
	while ((int_status = CSR_READ_1(sc, STATUS_REG)) & (Tx_Int | Rx_Int)) {
		if (int_status & Rx_Int) {
			CSR_WRITE_1(sc, STATUS_REG, Rx_Int);
			handled = 1;
			ex_rx_intr(sc);
		} else if (int_status & Tx_Int) {
			CSR_WRITE_1(sc, STATUS_REG, Tx_Int);
			handled = 1;
			ex_tx_intr(sc);
			send_pkts = 1;
		}
   	}

  	/*
	 * If any packet has been transmitted, and there are queued packets to
 	 * be sent, attempt to send more packets to the network card.
	 */

	if (send_pkts && ifq_empty(&ifp->if_snd) == 0)
		ex_start(ifp);
#ifdef EX_DEBUG
	exintr_count--;
#endif
	DODEBUG(Start_End, printf("exintr: finish\n"););

	return handled;
}

void 
ex_tx_intr(struct ex_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int tx_status;

	DODEBUG(Start_End, printf("ex_tx_intr: start\n"););
	/*
	 * - Cancel the watchdog.
	 * For all packets transmitted since last transmit interrupt:
	 * - Advance chain pointer to next queued packet.
	 * - Update statistics.
	 */
	ifp->if_timer = 0;
	while (sc->tx_head != sc->tx_tail) {
		CSR_WRITE_2(sc, HOST_ADDR_REG, sc->tx_head);
		if (!(CSR_READ_2(sc, IO_PORT_REG) & Done_bit))
			break;
		tx_status = CSR_READ_2(sc, IO_PORT_REG);
		sc->tx_head = CSR_READ_2(sc, IO_PORT_REG);
		if (!ISSET(tx_status, TX_OK_bit))
			ifp->if_oerrors++;
		ifp->if_collisions += tx_status & No_Collisions_bits;
	}

	/* The card should be ready to accept more packets now. */
	ifq_clr_oactive(&ifp->if_snd);
	DODEBUG(Status, printf("OIDLE tx_intr\n"););

	DODEBUG(Start_End, printf("ex_tx_intr: finish\n"););
}

void 
ex_rx_intr(struct ex_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	int rx_status, pkt_len, QQQ;
	struct mbuf *m, *ipkt;

	DODEBUG(Start_End, printf("ex_rx_intr: start\n"););
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
			MGETHDR(m, M_DONTWAIT, MT_DATA);
			ipkt = m;
			if (ipkt == NULL)
				ifp->if_iqdrops++;
			else {
				ipkt->m_pkthdr.len = pkt_len;
				ipkt->m_len = MHLEN;
				while (pkt_len > 0) {
					if (pkt_len >= MINCLSIZE) {
						MCLGET(m, M_DONTWAIT);
						if (m->m_flags & M_EXT)
							m->m_len = MCLBYTES;
						else {
							m_freem(ipkt);
							ifp->if_iqdrops++;
							goto rx_another;
						}
					}
					m->m_len = min(m->m_len, pkt_len);
					/*
					 * NOTE: I'm assuming that all mbufs 
					 * allocated are of even length, except
					 * for the last one in an odd-length 
					 * packet.
					 */
					CSR_READ_MULTI_2(sc, IO_PORT_REG,
					    mtod(m, caddr_t), m->m_len / 2);
					if (m->m_len & 1)
						*(mtod(m, caddr_t) + 
						    m->m_len - 1) = 
						    CSR_READ_1(sc, IO_PORT_REG);
					pkt_len -= m->m_len;
					if (pkt_len > 0) {
						MGET(m->m_next, M_DONTWAIT, 
						    MT_DATA);
					if (m->m_next == NULL) {
						m_freem(ipkt);
						ifp->if_iqdrops++;
						goto rx_another;
					}
					m = m->m_next;
					m->m_len = MLEN;
				}
			}
#ifdef EX_DEBUG
			if (debug_mask & Rcvd_Pkts) {
				if ((eh->ether_dhost[5] != 0xff) || 
				    (eh->ether_dhost[0] != 0xff)) {
					printf("Receive packet with %d data bytes: %6D -> ", QQQ, eh->ether_shost, ":");
					printf("%6D\n", eh->ether_dhost, ":");
				} /* QQQ */
			}
#endif
			ml_enqueue(&ml, ipkt);
      		}
    	} else
      		ifp->if_ierrors++;
		CSR_WRITE_2(sc, HOST_ADDR_REG, sc->rx_head);
		rx_another: ;
  	}
	if (sc->rx_head < sc->rx_lower_limit + 2)
		CSR_WRITE_2(sc, RCV_STOP_REG, sc->rx_upper_limit);
	else
		CSR_WRITE_2(sc, RCV_STOP_REG, sc->rx_head - 2);

	if_input(ifp, &ml);

	DODEBUG(Start_End, printf("ex_rx_intr: finish\n"););
}	

int 
ex_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ex_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	int s, error = 0;

	DODEBUG(Start_End, printf("ex_ioctl: start "););

	s = splnet();

	switch(cmd) {
	case SIOCSIFADDR:
		DODEBUG(Start_End, printf("SIOCSIFADDR"););
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			ex_init(sc);
		break;
	case SIOCSIFFLAGS:
		DODEBUG(Start_End, printf("SIOCSIFFLAGS"););
		if ((ifp->if_flags & IFF_UP) == 0 && ifp->if_flags & IFF_RUNNING) {
			ifp->if_flags &= ~IFF_RUNNING;
			ex_stop(sc);
		} else
			ex_init(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, cmd);
		break;
	default:
		error = ether_ioctl(ifp, &sc->arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			ex_init(sc);
		error = 0;
	}

	splx(s);
	DODEBUG(Start_End, printf("\nex_ioctl: finish\n"););
	return(error);
}

void
ex_setmulti(struct ex_softc *sc)
{
	struct arpcom *ac = &sc->arpcom;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint16_t *addr;
	int count, timeout, status;

	ifp->if_flags &= ~IFF_ALLMULTI;

	count = 0;
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		count++;
		ETHER_NEXT_MULTI(step, enm);
	}

	if (count > 63 || ac->ac_multirangecnt > 0)
		ifp->if_flags |= IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ifp->if_flags & IFF_ALLMULTI) {
		/*
		 * Interface is in promiscuous mode, there are too many
		 * multicast addresses for the card to handle or there
		 * is a multicast range
		 */
		CSR_WRITE_1(sc, CMD_REG, Bank2_Sel);
		CSR_WRITE_1(sc, REG2, CSR_READ_1(sc, REG2) | Promisc_Mode);
		CSR_WRITE_1(sc, REG3, CSR_READ_1(sc, REG3));
		CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);
	} else if (ifp->if_flags & IFF_MULTICAST && count > 0) {
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

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			addr = (uint16_t*)enm->enm_addrlo;
			CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
			CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
			CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
			ETHER_NEXT_MULTI(step, enm);
		}

		/* Program our MAC address as well */
		/* XXX: Is this necessary?  The Linux driver does this
		 * but the NetBSD driver does not */
		addr = (uint16_t*) sc->arpcom.ac_enaddr;
		CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
		CSR_WRITE_2(sc, IO_PORT_REG, *addr++);
		CSR_WRITE_2(sc, IO_PORT_REG, *addr++);

		CSR_READ_2(sc, IO_PORT_REG);
		CSR_WRITE_2(sc, XMT_BAR, sc->tx_lower_limit);
		CSR_WRITE_1(sc, CMD_REG, MC_Setup_CMD);

		sc->tx_head = sc->tx_lower_limit;
		sc->tx_tail = sc->tx_head + XMT_HEADER_LEN + (count + 1) * 6;

		for (timeout = 0; timeout < 100; timeout++) {
			DELAY(2);
			if ((CSR_READ_1(sc, STATUS_REG) & Exec_Int) == 0)
				continue;

			status = CSR_READ_1(sc, CMD_REG);
			CSR_WRITE_1(sc, STATUS_REG, Exec_Int);
			break;
		}

		sc->tx_head = sc->tx_tail;
	} else {
		/* No multicast or promiscuous mode */
		CSR_WRITE_1(sc, CMD_REG, Bank2_Sel);
		CSR_WRITE_1(sc, REG2, CSR_READ_1(sc, REG2) & 0xDE);
			/* ~(Multi_IA | Promisc_Mode) */
		CSR_WRITE_1(sc, REG3, CSR_READ_1(sc, REG3));
		CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);
	}
}

void 
ex_reset(struct ex_softc *sc)
{
	int s;

	DODEBUG(Start_End, printf("ex_reset: start\n"););
  
	s = splnet();
	ex_stop(sc);
	ex_init(sc);
	splx(s);

	DODEBUG(Start_End, printf("ex_reset: finish\n"););
}

void 
ex_watchdog(struct ifnet *ifp)
{
	struct ex_softc *sc = ifp->if_softc;

	DODEBUG(Start_End, printf("ex_watchdog: start\n"););

	ifq_clr_oactive(&ifp->if_snd);
	DODEBUG(Status, printf("OIDLE watchdog\n"););
	ifp->if_oerrors++;
	ex_reset(sc);
	ex_start(ifp);

	DODEBUG(Start_End, printf("ex_watchdog: finish\n"););
}

uint64_t
ex_get_media(struct ex_softc *sc)
{
	int	current, media;

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

int
ex_ifmedia_upd(struct ifnet *ifp)
{
	struct ex_softc *sc = ifp->if_softc;

	if (IFM_TYPE(sc->ifmedia.ifm_media) != IFM_ETHER)
		return (EINVAL);

	return (0);
}

void
ex_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ex_softc *sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;
	ifmr->ifm_active = ex_get_media(sc);
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
		short outval = (read_cmd & (1 << i)) ? ctrl_val | EEDI : 
		    ctrl_val;
		CSR_WRITE_1(sc, EEPROM_REG, outval);
		CSR_WRITE_1(sc, EEPROM_REG, outval | EESK);
		delay(3);
		CSR_WRITE_1(sc, EEPROM_REG, outval);
		delay(2);
	}
	CSR_WRITE_1(sc, EEPROM_REG, ctrl_val);
	for (i = 16; i > 0; i--) {
		CSR_WRITE_1(sc, EEPROM_REG, ctrl_val | EESK);
		delay(3);
		data = (data << 1) | ((CSR_READ_1(sc, EEPROM_REG) & EEDO) ? 1 : 0);
		CSR_WRITE_1(sc, EEPROM_REG, ctrl_val);
		delay(2);
	}
	ctrl_val &= ~EECS;
	CSR_WRITE_1(sc, EEPROM_REG, ctrl_val | EESK);
	delay(3);
	CSR_WRITE_1(sc, EEPROM_REG, ctrl_val);
	delay(2);
	CSR_WRITE_1(sc, CMD_REG, Bank0_Sel);
	return(data);
}
