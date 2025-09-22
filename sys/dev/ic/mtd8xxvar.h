/*	$OpenBSD: mtd8xxvar.h,v 1.4 2014/09/06 05:41:35 jsg Exp $	*/

/*
 * Copyright (c) 2003 Oleg Safiullin <form@pdp11.org.ru>
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

#ifndef __DEV_IC_MTD8XXVAR_H__
#define __DEV_IC_MTD8XXVAR_H__

#define MTD_RX_LIST_CNT		64
#define MTD_TX_LIST_CNT		128

/*
 * Transmit descriptor structure.
 */
struct mtd_tx_desc {
	u_int32_t	td_tsw;		/* Transmit status word */
#define TSW_OWN		0x80000000U	/* Descriptor owned by NIC */
#define TSW_TXERR	0x00008000U	/* Transmission error */
#define TSW_ABORT	0x00002000U	/* Transmission aborted */
#define TSW_CSL		0x00001000U	/* Carrier sense lost */
#define TSW_LC		0x00000800U	/* Late collision occurs */
#define TSW_EC		0x00000400U	/* Excessive collisions */
#define TSW_DFR		0x00000200U	/* Deferred */
#define TSW_HF		0x00000100U	/* Heart beat failure */
#define TSW_NCR_MASK	0x000000FFU
#define TSW_NCR_SHIFT	0
#define TSW_NCR_GET(x)	(((x) & TSW_NCR_MASK) >> TSW_NCR_SHIFT)
					/* Collision retry count */
#define TSW_UNSENT	0x00001234U	/* Unsent packet magic */
	u_int32_t	td_tcw;		/* Transmit configure word */
#define TCW_IC		0x80000000U	/* Interrupt control */
#define TCW_EIC		0x40000000U	/* Early interrupt control */
#define TCW_LD		0x20000000U	/* Last descriptor */
#define TCW_FD		0x10000000U	/* First descriptor */
#define TCW_CRC		0x08000000U	/* Append CRC field to packet */
#define TCW_PAD		0x04000000U	/* Pad zeroes to the end of packet */
#define TCW_RTLC	0x02000000U	/* Retry late collision */
#define TCW_PKTS_MASK	0x00003FF8U
#define TCW_PKTS_SHIFT	11
#define TCW_PKTS_GET(x)	(((x) & TCW_PKTS_MASK) >> TCW_PKTS_SHIFT)
					/* Packet size */
#define TCW_TBS_MASK	0x000007FFU
#define TCW_TBS_SHIFT	0
#define TCW_TBS_GET(x)	(((x) & TCW_TBS_MASK) >> TCW_TBS_SHIFT)
					/* Transmit buffer size */
	u_int32_t	td_buf;		/* Transmit buffer address */
	u_int32_t	td_next;	/* Next descriptor address */
};


/*
 * Receive descriptor structure.
 */
struct mtd_rx_desc {
	u_int32_t	rd_rsr;		/* Receive status register */
#define RSR_OWN		0x80000000U	/* Descriptor owned by NIC */
#define RSR_FLNG_MASK	0x0FFF0000U
#define RSR_FLNG_SHIFT	16
#define RSR_FLNG_GET(x)	(((x) & RSR_FLNG_MASK) >> RSR_FLNG_SHIFT)
					/* Frame length */
#define RSR_MAR		0x00004000U	/* Multicast address received */
#define RSR_BAR		0x00002000U	/* Broadcast address received */
#define RSR_PHY		0x00001000U	/* Physical address received */
#define RSR_FSD		0x00000800U	/* First descriptor */
#define RSR_LSD		0x00000400U	/* Last descriptor */
#define RSR_ES		0x00000080U	/* Error summary */
#define RSR_RUNT	0x00000040U	/* Runt packet received */
#define RSR_LONG	0x00000020U	/* Long packet received */
#define RSR_FAE		0x00000010U	/* Frame alignment error */
#define RSR_CRC		0x00000008U	/* CRC error */
#define RSR_RXER	0x00000004U	/* Receive error */
	u_int32_t	rd_rcw;		/* Receive configure word */
#define RCW_RBS_MASK	0x000007FFU
#define RCW_RBS_SHIFT	0
#define RCW_RBS_GET(x) (((x) & RCW_RBS_MASK) >> RCW_RBS_SHIFT)
	u_int32_t	rd_buf;		/* Receive buffer address */
	u_int32_t	rd_next;	/* Next descriptor address */
};


struct mtd_list_data {
	struct mtd_rx_desc	mtd_rx_list[MTD_RX_LIST_CNT];
	struct mtd_tx_desc	mtd_tx_list[MTD_TX_LIST_CNT];
};


struct mtd_swdesc {
	bus_dmamap_t		sd_map;
	struct mbuf		*sd_mbuf;
};


struct mtd_chain_data {
	struct mtd_swdesc	mtd_rx_chain[MTD_RX_LIST_CNT];
	struct mtd_swdesc	mtd_tx_chain[MTD_TX_LIST_CNT];
	int			mtd_tx_prod;
	int			mtd_tx_cons;
	int			mtd_tx_cnt;
	int			mtd_rx_prod;
};


struct mtd_softc {
	struct device		sc_dev;
	struct arpcom		sc_arpcom;
	struct mii_data		sc_mii;
	uint16_t		sc_devid;

	bus_space_handle_t	sc_bush;
	bus_space_tag_t		sc_bust;

	struct mtd_list_data	*mtd_ldata;
	struct mtd_chain_data	mtd_cdata;

	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_listmap;
	bus_dma_segment_t	sc_listseg[1];
	int			sc_listnseg;
	caddr_t			sc_listkva;
	bus_dmamap_t		sc_rx_sparemap;
	bus_dmamap_t		sc_tx_sparemap;
};

__BEGIN_DECLS
void	mtd_attach(struct mtd_softc *);
int	mtd_intr(void *);
__END_DECLS

#endif	/* __DEV_IC_MTD8XXVAR_H__ */
