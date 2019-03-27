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
 *
 October 2, 1994

 Modified by: Andres Vega Garcia

 INRIA - Sophia Antipolis, France
 e-mail: avega@sophia.inria.fr
 finger: avega@pax.inria.fr

 */

/*
 * Ethernet software status per interface.
 */
struct vx_softc {
	struct ifnet *vx_ifp;
	bus_space_tag_t vx_bst;
	bus_space_handle_t vx_bsh;
	void *vx_intrhand;
	struct resource *vx_irq;
	struct resource *vx_res;
#define MAX_MBS  8			/* # of mbufs we keep around	 */
	struct mbuf *vx_mb[MAX_MBS];	/* spare mbuf storage.		 */
	int vx_next_mb;			/* Which mbuf to use next. 	 */
	int vx_last_mb;			/* Last mbuf.			 */
	char vx_connectors;		/* Connectors on this card.	 */
	char vx_connector;		/* Connector to use.		 */
	short vx_tx_start_thresh;	/* Current TX_start_thresh.	 */
	int vx_tx_succ_ok;		/* # packets sent in sequence	 */
					/* w/o underrun			 */
	struct callout vx_callout;	/* Callout for timeouts		 */
	struct callout vx_watchdog;
	struct mtx vx_mtx;
	int vx_buffill_pending;
	int vx_timer;
};

#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->vx_bst, sc->vx_bsh, reg, val)
#define CSR_WRITE_2(sc, reg, val)	\
	bus_space_write_2(sc->vx_bst, sc->vx_bsh, reg, val)
#define CSR_WRITE_1(sc, reg, val)	\
	bus_space_write_1(sc->vx_bst, sc->vx_bsh, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->vx_bst, sc->vx_bsh, reg)
#define CSR_READ_2(sc, reg)		\
	bus_space_read_2(sc->vx_bst, sc->vx_bsh, reg)
#define CSR_READ_1(sc, reg)		\
	bus_space_read_1(sc->vx_bst, sc->vx_bsh, reg)

#define	VX_LOCK(sc)		mtx_lock(&(sc)->vx_mtx)
#define	VX_UNLOCK(sc)		mtx_unlock(&(sc)->vx_mtx)
#define	VX_LOCK_ASSERT(sc)	mtx_assert(&(sc)->vx_mtx, MA_OWNED)

int	vx_attach(device_t);
void	vx_stop(struct vx_softc *);
void	vx_intr(void *);
int	vx_busy_eeprom(struct vx_softc *);
