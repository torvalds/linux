/*	$OpenBSD: if_vgevar.h,v 1.5 2013/03/15 01:33:23 brad Exp $	*/
/*	$FreeBSD: if_vgevar.h,v 1.1 2004/09/10 20:57:45 wpaul Exp $	*/
/*
 * Copyright (c) 2004
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#define VGE_JUMBO_MTU	9000

#define VGE_IFQ_MAXLEN 64

#define VGE_TX_DESC_CNT		256
#define VGE_RX_DESC_CNT		256	/* Must be a multiple of 4!! */
#define VGE_RING_ALIGN		256
#define VGE_RX_LIST_SZ		(VGE_RX_DESC_CNT * sizeof(struct vge_rx_desc))
#define VGE_TX_LIST_SZ		(VGE_TX_DESC_CNT * sizeof(struct vge_tx_desc))
#define VGE_TX_DESC_INC(x)	(x = (x + 1) % VGE_TX_DESC_CNT)
#define VGE_RX_DESC_INC(x)	(x = (x + 1) % VGE_RX_DESC_CNT)
#define VGE_ADDR_LO(y)		((u_int64_t) (y) & 0xFFFFFFFF)
#define VGE_ADDR_HI(y)		((u_int64_t) (y) >> 32)
#define VGE_BUFLEN(y)		((y) & 0x7FFF)
#define VGE_OWN(x)		(letoh32((x)->vge_sts) & VGE_RDSTS_OWN)
#define VGE_RXBYTES(x)		((letoh32((x)->vge_sts) & \
				 VGE_RDSTS_BUFSIZ) >> 16)
#define VGE_MIN_FRAMELEN	60

#define MAX_NUM_MULTICAST_ADDRESSES	128

struct vge_softc;

struct vge_list_data {
	struct mbuf		*vge_tx_mbuf[VGE_TX_DESC_CNT];
	struct mbuf		*vge_rx_mbuf[VGE_RX_DESC_CNT];
	int			vge_tx_prodidx;
	int			vge_rx_prodidx;
	int			vge_tx_considx;
	int			vge_tx_free;
	bus_dmamap_t		vge_tx_dmamap[VGE_TX_DESC_CNT];
	bus_dmamap_t		vge_rx_dmamap[VGE_RX_DESC_CNT];
	bus_dma_tag_t		vge_mtag;        /* mbuf mapping tag */
	bus_dma_segment_t	vge_rx_listseg;
	bus_dmamap_t		vge_rx_list_map;
	struct vge_rx_desc	*vge_rx_list;
	bus_dma_segment_t	vge_tx_listseg;
	bus_dmamap_t		vge_tx_list_map;
	struct vge_tx_desc	*vge_tx_list;
};

struct vge_softc {
	struct device		vge_dev;
	struct arpcom		arpcom;		/* interface info */
	bus_space_handle_t	vge_bhandle;	/* bus space handle */
	bus_space_tag_t		vge_btag;	/* bus space tag */
	bus_size_t		vge_bsize;
	void			*vge_intrhand;
	bus_dma_tag_t		sc_dmat;
	pci_chipset_tag_t	sc_pc;
	struct mii_data		sc_mii;
	int			vge_rx_consumed;
	int			vge_link;
	int			vge_camidx;
	struct timeout		timer_handle;
	struct mbuf		*vge_head;
	struct mbuf		*vge_tail;

	struct vge_list_data	vge_ldata;
};

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->vge_btag, sc->vge_bhandle, reg, val)
#define CSR_WRITE_2(sc, reg, val)	\
	bus_space_write_2(sc->vge_btag, sc->vge_bhandle, reg, val)
#define CSR_WRITE_1(sc, reg, val)	\
	bus_space_write_1(sc->vge_btag, sc->vge_bhandle, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->vge_btag, sc->vge_bhandle, reg)
#define CSR_READ_2(sc, reg)		\
	bus_space_read_2(sc->vge_btag, sc->vge_bhandle, reg)
#define CSR_READ_1(sc, reg)		\
	bus_space_read_1(sc->vge_btag, sc->vge_bhandle, reg)

#define CSR_SETBIT_1(sc, reg, x)	\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) | (x))
#define CSR_SETBIT_2(sc, reg, x)	\
	CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) | (x))
#define CSR_SETBIT_4(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | (x))

#define CSR_CLRBIT_1(sc, reg, x)	\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) & ~(x))
#define CSR_CLRBIT_2(sc, reg, x)	\
	CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) & ~(x))
#define CSR_CLRBIT_4(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~(x))

#define VGE_TIMEOUT		10000
