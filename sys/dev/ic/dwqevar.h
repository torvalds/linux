/*	$OpenBSD: dwqevar.h,v 1.11 2024/02/26 18:57:50 kettenis Exp $	*/
/*
 * Copyright (c) 2008, 2019 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2017, 2022 Patrick Wildt <patrick@blueri.se>
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

enum dwqe_phy_mode {
	DWQE_PHY_MODE_UNKNOWN,
	DWQE_PHY_MODE_RMII,
	DWQE_PHY_MODE_RGMII,
	DWQE_PHY_MODE_RGMII_ID,
	DWQE_PHY_MODE_RGMII_TXID,
	DWQE_PHY_MODE_RGMII_RXID,
	DWQE_PHY_MODE_SGMII,
};

struct dwqe_buf {
	bus_dmamap_t	tb_map;
	struct mbuf	*tb_m;
};

#define DWQE_NTXDESC	256
#define DWQE_NTXSEGS	16

#define DWQE_NRXDESC	256

struct dwqe_dmamem {
	bus_dmamap_t		tdm_map;
	bus_dma_segment_t	tdm_seg;
	size_t			tdm_size;
	caddr_t			tdm_kva;
};
#define DWQE_DMA_MAP(_tdm)	((_tdm)->tdm_map)
#define DWQE_DMA_LEN(_tdm)	((_tdm)->tdm_size)
#define DWQE_DMA_DVA(_tdm)	((_tdm)->tdm_map->dm_segs[0].ds_addr)
#define DWQE_DMA_KVA(_tdm)	((void *)(_tdm)->tdm_kva)

struct dwqe_softc {
	struct device		sc_dev;
	int			sc_node;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;
	void			*sc_ih;

	struct arpcom		sc_ac;
#define sc_lladdr	sc_ac.ac_enaddr
	struct mii_data		sc_mii;
#define sc_media	sc_mii.mii_media
	int			sc_link;
	int			sc_phyloc;
	enum dwqe_phy_mode	sc_phy_mode;
	struct timeout		sc_phy_tick;
	int			sc_fixed_link;

	struct dwqe_dmamem	*sc_txring;
	struct dwqe_buf		*sc_txbuf;
	struct dwqe_desc	*sc_txdesc;
	int			sc_tx_prod;
	int			sc_tx_cons;

	struct dwqe_dmamem	*sc_rxring;
	struct dwqe_buf		*sc_rxbuf;
	struct dwqe_desc	*sc_rxdesc;
	int			sc_rx_prod;
	struct if_rxring	sc_rx_ring;
	int			sc_rx_cons;

	struct timeout		sc_rxto;
	struct task		sc_statchg_task;

	uint32_t		sc_clk;
	uint32_t		sc_clkrate;

	bus_size_t		sc_clk_sel;
	uint32_t		sc_clk_sel_125;
	uint32_t		sc_clk_sel_25;
	uint32_t		sc_clk_sel_2_5;

	int			sc_hw_feature[4];

	int			sc_force_thresh_dma_mode;
	int			sc_fixed_burst;
	int			sc_mixed_burst;
	int			sc_aal;
	int			sc_8xpbl;
	int			sc_pbl;
	int			sc_txpbl;
	int			sc_rxpbl;
	int			sc_txfifo_size;
	int			sc_rxfifo_size;
	int			sc_axi_config;
	int			sc_lpi_en;
	int			sc_xit_frm;
	int			sc_wr_osr_lmt;
	int			sc_rd_osr_lmt;

	uint32_t		sc_blen[7];
};

#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

int	dwqe_attach(struct dwqe_softc *);
void	dwqe_reset(struct dwqe_softc *);
int	dwqe_intr(void *);
uint32_t dwqe_read(struct dwqe_softc *, bus_addr_t);
void	dwqe_write(struct dwqe_softc *, bus_addr_t, uint32_t);
void	dwqe_lladdr_read(struct dwqe_softc *, uint8_t *);
void	dwqe_lladdr_write(struct dwqe_softc *);
void	dwqe_mii_statchg(struct device *);
