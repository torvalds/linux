/*-
 * Copyright (c) 1993 Herb Peyerl (hpeyerl@novatel.ca) All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2. The name
 * of the author may not be used to endorse or promote products derived from
 * this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

struct ep_board {
	u_short prod_id;	/* product ID */
	int cmd_off;		/* command offset (bit shift) */
	int mii_trans;		/* activate MII transiever */
	u_short res_cfg;	/* resource configuration */
};

/*
 * Ethernet software status per interface.
 */
struct ep_softc {
	struct ifnet *ifp;
	struct ifmedia ifmedia;	/* media info		 */
	device_t dev;

	struct mtx sc_mtx;
	struct resource *iobase;
	struct resource *irq;

	bus_space_handle_t bsh;
	bus_space_tag_t bst;
	void *ep_intrhand;

	struct callout watchdog_timer;
	int tx_timer;

	u_short ep_connectors;	/* Connectors on this card. */
	u_char ep_connector;	/* Configured connector. */

	struct mbuf *top;
	struct mbuf *mcur;
	short cur_len;

	int stat;		/* some flags */
#define	F_RX_FIRST		0x001
#define F_ENADDR_SKIP		0x002
#define	F_PROMISC		0x008
#define	F_HAS_TX_PLL		0x200

	int gone;		/* adapter is not present (for PCCARD) */
	struct ep_board epb;
	uint8_t eaddr[6];

#ifdef  EP_LOCAL_STATS
	short tx_underrun;
	short rx_no_first;
	short rx_no_mbuf;
	short rx_overrunf;
	short rx_overrunl;
#endif
};

int ep_alloc(device_t);
void ep_free(device_t);
int ep_detach(device_t);
void ep_get_media(struct ep_softc *);
int ep_attach(struct ep_softc *);
void ep_intr(void *);
int ep_get_e(struct ep_softc *, uint16_t, uint16_t *);

#define CSR_READ_1(sc, off) (bus_space_read_1((sc)->bst, (sc)->bsh, off))
#define CSR_READ_2(sc, off) (bus_space_read_2((sc)->bst, (sc)->bsh, off))
#define CSR_WRITE_1(sc, off, val) \
	bus_space_write_1(sc->bst, sc->bsh, off, val)
#define CSR_WRITE_2(sc, off, val) \
	bus_space_write_2(sc->bst, sc->bsh, off, val)
#define CSR_WRITE_MULTI_1(sc, off, addr, count) \
	bus_space_write_multi_1(sc->bst, sc->bsh, off, addr, count)
#define CSR_WRITE_MULTI_2(sc, off, addr, count) \
	bus_space_write_multi_2(sc->bst, sc->bsh, off, addr, count)
#define CSR_WRITE_MULTI_4(sc, off, addr, count) \
	bus_space_write_multi_4(sc->bst, sc->bsh, off, addr, count)
#define CSR_READ_MULTI_1(sc, off, addr, count) \
	bus_space_read_multi_1(sc->bst, sc->bsh, off, addr, count)
#define CSR_READ_MULTI_2(sc, off, addr, count) \
	bus_space_read_multi_2(sc->bst, sc->bsh, off, addr, count)
#define CSR_READ_MULTI_4(sc, off, addr, count) \
	bus_space_read_multi_4(sc->bst, sc->bsh, off, addr, count)

#define EP_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	EP_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define EP_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->dev), \
	    MTX_NETWORK_LOCK, MTX_DEF)
#define EP_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->sc_mtx);
#define EP_ASSERT_LOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define EP_ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);
