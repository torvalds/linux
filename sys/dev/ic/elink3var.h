/*	$OpenBSD: elink3var.h,v 1.19 2009/11/23 16:36:22 claudio Exp $	*/
/*	$NetBSD: elink3var.h,v 1.12 1997/03/30 22:47:11 jonathan Exp $	*/

/*
 * Copyright (c) 1994 Herb Peyerl <hpeyerl@beer.org>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Herb Peyerl.
 * 4. The name of Herb Peyerl may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Ethernet software status per interface.
 */
struct ep_softc {
	struct device sc_dev;
	void *sc_ih;
	struct timeout sc_epmbuffill_tmo;

	struct arpcom sc_arpcom;	/* Ethernet common part		*/
	struct mii_data sc_mii;		/* MII/media control		*/
	bus_space_tag_t sc_iot;		/* bus cookie			*/
	bus_space_handle_t sc_ioh;	/* bus i/o handle		*/
	u_int	ep_connectors;		/* Connectors on this card.	*/
#define MAX_MBS	8			/* # of mbufs we keep around	*/
	struct mbuf *mb[MAX_MBS];	/* spare mbuf storage.		*/
	int	next_mb;		/* Which mbuf to use next. 	*/
	int	tx_start_thresh;	/* Current TX_start_thresh.	*/
	int	tx_succ_ok;		/* # packets sent in sequence   */
					/* w/o underrun			*/

	u_int	ep_flags;		/* capabilities flag (from EEPROM) */
#define EP_FLAGS_PNP			0x0001
#define EP_FLAGS_FULLDUPLEX		0x0002
#define EP_FLAGS_LARGEPKT		0x0004	/* 4k packet support */
#define EP_FLAGS_SLAVEDMA		0x0008
#define EP_FLAGS_SECONDDMA		0x0010
#define EP_FLAGS_FULLDMA		0x0020
#define EP_FLAGS_FRAGMENTDMA		0x0040
#define EP_FLAGS_CRC_PASSTHRU		0x0080
#define EP_FLAGS_TXDONE			0x0100
#define EP_FLAGS_NO_TXLENGTH		0x0200
#define EP_FLAGS_RXREPEAT		0x0400
#define EP_FLAGS_SNOOPING		0x0800
#define EP_FLAGS_100MBIT		0x1000
#define EP_FLAGS_POWERMGMT		0x2000
#define EP_FLAGS_MII			0x4000

	u_short ep_chipset;		/* Chipset family on this board */
#define EP_CHIPSET_UNKNOWN		0x00	/* unknown (assume 3c509) */
#define EP_CHIPSET_3C509		0x01	/* PIO: 3c509, 3c589 */
#define EP_CHIPSET_VORTEX		0x02	/* 100mbit, single-pkt dma */
#define EP_CHIPSET_BOOMERANG		0x03	/* Saner dma plus PIO */
#define EP_CHIPSET_BOOMERANG2		0x04	/* Saner dma, no PIO */
#define EP_CHIPSET_ROADRUNNER		0x05	/* Like Boomerang, but PCMCIA */

	u_char	bustype;
#define EP_BUS_ISA	  	0x0
#define	EP_BUS_PCMCIA	  	0x1
#define	EP_BUS_EISA	  	0x2
#define EP_BUS_PCI	  	0x3

#define EP_IS_BUS_32(a)	((a) & 0x2)

	u_char	txashift;		/* shift in SET_TX_AVAIL_THRESH */
};

u_int16_t epreadeeprom(bus_space_tag_t, bus_space_handle_t, int);
void	epconfig(struct ep_softc *, u_short, u_int8_t *);
int	epintr(void *);
void	epstop(struct ep_softc *);
void	epinit(struct ep_softc *);
int	ep_detach(struct device *);
