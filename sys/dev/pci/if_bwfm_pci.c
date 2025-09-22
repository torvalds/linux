/*	$OpenBSD: if_bwfm_pci.c,v 1.78 2025/08/21 17:03:58 mbuhl Exp $	*/
/*
 * Copyright (c) 2010-2016 Broadcom Corporation
 * Copyright (c) 2017 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/socket.h>

#if defined(__HAVE_FDT)
#include <machine/fdt.h>
#include <dev/ofw/openfirm.h>
#endif

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/bwfmvar.h>
#include <dev/ic/bwfmreg.h>
#include <dev/pci/if_bwfm_pci.h>

#define BWFM_DMA_D2H_SCRATCH_BUF_LEN		8
#define BWFM_DMA_D2H_RINGUPD_BUF_LEN		1024
#define BWFM_DMA_H2D_IOCTL_BUF_LEN		ETHER_MAX_LEN

#define BWFM_NUM_TX_MSGRINGS			2
#define BWFM_NUM_RX_MSGRINGS			3

#define BWFM_NUM_IOCTL_PKTIDS			8
#define BWFM_NUM_TX_PKTIDS			2048
#define BWFM_NUM_RX_PKTIDS			1024

#define BWFM_NUM_IOCTL_DESCS			1
#define BWFM_NUM_TX_DESCS			1
#define BWFM_NUM_RX_DESCS			1

#ifdef BWFM_DEBUG
#define DPRINTF(x)	do { if (bwfm_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (bwfm_debug >= (n)) printf x; } while (0)
static int bwfm_debug = 2;
#else
#define DPRINTF(x)	do { ; } while (0)
#define DPRINTFN(n, x)	do { ; } while (0)
#endif

#define DEVNAME(sc)	((sc)->sc_sc.sc_dev.dv_xname)

enum ring_status {
	RING_CLOSED,
	RING_CLOSING,
	RING_OPEN,
	RING_OPENING,
};

struct bwfm_pci_msgring {
	uint32_t		 w_idx_addr;
	uint32_t		 r_idx_addr;
	uint32_t		 w_ptr;
	uint32_t		 r_ptr;
	int			 nitem;
	int			 itemsz;
	enum ring_status	 status;
	struct bwfm_pci_dmamem	*ring;
	struct mbuf		*m;

	int			 fifo;
	uint8_t			 mac[ETHER_ADDR_LEN];
};

struct bwfm_pci_ioctl {
	uint16_t		 transid;
	uint16_t		 retlen;
	int16_t			 status;
	struct mbuf		*m;
	TAILQ_ENTRY(bwfm_pci_ioctl) next;
};

struct bwfm_pci_buf {
	bus_dmamap_t	 bb_map;
	struct mbuf	*bb_m;
};

struct bwfm_pci_pkts {
	struct bwfm_pci_buf	*pkts;
	uint32_t		 npkt;
	int			 last;
};

struct bwfm_pci_softc {
	struct bwfm_softc	 sc_sc;
	pci_chipset_tag_t	 sc_pc;
	pcitag_t		 sc_tag;
	pcireg_t		 sc_id;
	void 			*sc_ih;

	int			 sc_initialized;

	bus_space_tag_t		 sc_reg_iot;
	bus_space_handle_t	 sc_reg_ioh;
	bus_size_t		 sc_reg_ios;

	bus_space_tag_t		 sc_pcie_iot;
	bus_space_handle_t	 sc_pcie_ioh;
	bus_size_t		 sc_pcie_ios;

	bus_space_tag_t		 sc_tcm_iot;
	bus_space_handle_t	 sc_tcm_ioh;
	bus_size_t		 sc_tcm_ios;

	bus_dma_tag_t		 sc_dmat;

	uint32_t		 sc_shared_address;
	uint32_t		 sc_shared_flags;
	uint8_t			 sc_shared_version;

	uint8_t			 sc_dma_idx_sz;
	struct bwfm_pci_dmamem	*sc_dma_idx_buf;
	size_t			 sc_dma_idx_bufsz;

	uint16_t		 sc_max_rxbufpost;
	uint32_t		 sc_rx_dataoffset;
	uint32_t		 sc_htod_mb_data_addr;
	uint32_t		 sc_dtoh_mb_data_addr;
	uint32_t		 sc_ring_info_addr;

	uint32_t		 sc_console_base_addr;
	uint32_t		 sc_console_buf_addr;
	uint32_t		 sc_console_buf_size;
	uint32_t		 sc_console_readidx;

	uint16_t		 sc_max_flowrings;
	uint16_t		 sc_max_submissionrings;
	uint16_t		 sc_max_completionrings;

	struct bwfm_pci_msgring	 sc_ctrl_submit;
	struct bwfm_pci_msgring	 sc_rxpost_submit;
	struct bwfm_pci_msgring	 sc_ctrl_complete;
	struct bwfm_pci_msgring	 sc_tx_complete;
	struct bwfm_pci_msgring	 sc_rx_complete;
	struct bwfm_pci_msgring	*sc_flowrings;

	struct bwfm_pci_dmamem	*sc_scratch_buf;
	struct bwfm_pci_dmamem	*sc_ringupd_buf;

	TAILQ_HEAD(, bwfm_pci_ioctl) sc_ioctlq;
	uint16_t		 sc_ioctl_transid;

	struct if_rxring	 sc_ioctl_ring;
	struct if_rxring	 sc_event_ring;
	struct if_rxring	 sc_rxbuf_ring;

	struct bwfm_pci_pkts	 sc_ioctl_pkts;
	struct bwfm_pci_pkts	 sc_rx_pkts;
	struct bwfm_pci_pkts	 sc_tx_pkts;
	int			 sc_tx_pkts_full;

	uint8_t			 sc_mbdata_done;
	uint8_t			 sc_pcireg64;
	uint8_t			 sc_mb_via_ctl;
};

struct bwfm_pci_dmamem {
	bus_dmamap_t		bdm_map;
	bus_dma_segment_t	bdm_seg;
	size_t			bdm_size;
	caddr_t			bdm_kva;
};

#define BWFM_PCI_DMA_MAP(_bdm)	((_bdm)->bdm_map)
#define BWFM_PCI_DMA_LEN(_bdm)	((_bdm)->bdm_size)
#define BWFM_PCI_DMA_DVA(_bdm)	((uint64_t)(_bdm)->bdm_map->dm_segs[0].ds_addr)
#define BWFM_PCI_DMA_KVA(_bdm)	((void *)(_bdm)->bdm_kva)

int		 bwfm_pci_match(struct device *, void *, void *);
void		 bwfm_pci_attach(struct device *, struct device *, void *);
int		 bwfm_pci_detach(struct device *, int);
int		 bwfm_pci_activate(struct device *, int);
void		 bwfm_pci_cleanup(struct bwfm_pci_softc *);

#if defined(__HAVE_FDT)
int		 bwfm_pci_read_otp(struct bwfm_pci_softc *);
void		 bwfm_pci_process_otp_tuple(struct bwfm_pci_softc *, uint8_t,
		    uint8_t, uint8_t *);
#endif

int		 bwfm_pci_intr(void *);
void		 bwfm_pci_intr_enable(struct bwfm_pci_softc *);
void		 bwfm_pci_intr_disable(struct bwfm_pci_softc *);
uint32_t	 bwfm_pci_intr_status(struct bwfm_pci_softc *);
void		 bwfm_pci_intr_ack(struct bwfm_pci_softc *, uint32_t);
uint32_t	 bwfm_pci_intmask(struct bwfm_pci_softc *);
void		 bwfm_pci_hostready(struct bwfm_pci_softc *);
int		 bwfm_pci_load_microcode(struct bwfm_pci_softc *, const u_char *,
		    size_t, const u_char *, size_t);
void		 bwfm_pci_select_core(struct bwfm_pci_softc *, int );

struct bwfm_pci_dmamem *
		 bwfm_pci_dmamem_alloc(struct bwfm_pci_softc *, bus_size_t,
		    bus_size_t);
void		 bwfm_pci_dmamem_free(struct bwfm_pci_softc *, struct bwfm_pci_dmamem *);
int		 bwfm_pci_pktid_avail(struct bwfm_pci_softc *,
		    struct bwfm_pci_pkts *);
int		 bwfm_pci_pktid_new(struct bwfm_pci_softc *,
		    struct bwfm_pci_pkts *, struct mbuf *,
		    uint32_t *, paddr_t *);
struct mbuf *	 bwfm_pci_pktid_free(struct bwfm_pci_softc *,
		    struct bwfm_pci_pkts *, uint32_t);
void		 bwfm_pci_fill_rx_ioctl_ring(struct bwfm_pci_softc *,
		    struct if_rxring *, uint32_t);
void		 bwfm_pci_fill_rx_buf_ring(struct bwfm_pci_softc *);
void		 bwfm_pci_fill_rx_rings(struct bwfm_pci_softc *);
int		 bwfm_pci_setup_ring(struct bwfm_pci_softc *, struct bwfm_pci_msgring *,
		    int, size_t, uint32_t, uint32_t, int, uint32_t, uint32_t *);
int		 bwfm_pci_setup_flowring(struct bwfm_pci_softc *, struct bwfm_pci_msgring *,
		    int, size_t);

void		 bwfm_pci_ring_bell(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *);
void		 bwfm_pci_ring_update_rptr(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *);
void		 bwfm_pci_ring_update_wptr(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *);
void		 bwfm_pci_ring_write_rptr(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *);
void		 bwfm_pci_ring_write_wptr(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *);
void *		 bwfm_pci_ring_write_reserve(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *);
void *		 bwfm_pci_ring_write_reserve_multi(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *, int, int *);
void *		 bwfm_pci_ring_read_avail(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *, int *);
void		 bwfm_pci_ring_read_commit(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *, int);
void		 bwfm_pci_ring_write_commit(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *);
void		 bwfm_pci_ring_write_cancel(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *, int);

void		 bwfm_pci_ring_rx(struct bwfm_pci_softc *,
		    struct bwfm_pci_msgring *, struct mbuf_list *);
void		 bwfm_pci_msg_rx(struct bwfm_pci_softc *, void *,
		    struct mbuf_list *);

uint32_t	 bwfm_pci_buscore_read(struct bwfm_softc *, uint32_t);
void		 bwfm_pci_buscore_write(struct bwfm_softc *, uint32_t,
		    uint32_t);
int		 bwfm_pci_buscore_prepare(struct bwfm_softc *);
int		 bwfm_pci_buscore_reset(struct bwfm_softc *);
void		 bwfm_pci_buscore_activate(struct bwfm_softc *, uint32_t);

int		 bwfm_pci_flowring_lookup(struct bwfm_pci_softc *,
		     struct mbuf *);
void		 bwfm_pci_flowring_create(struct bwfm_pci_softc *,
		     struct mbuf *);
void		 bwfm_pci_flowring_create_cb(struct bwfm_softc *, void *);
void		 bwfm_pci_flowring_delete(struct bwfm_pci_softc *, int);
void		 bwfm_pci_flowring_delete_cb(struct bwfm_softc *, void *);

int		 bwfm_pci_preinit(struct bwfm_softc *);
void		 bwfm_pci_stop(struct bwfm_softc *);
int		 bwfm_pci_txcheck(struct bwfm_softc *);
int		 bwfm_pci_txdata(struct bwfm_softc *, struct mbuf *);

int		 bwfm_pci_send_mb_data(struct bwfm_pci_softc *, uint32_t);
void		 bwfm_pci_handle_mb_data(struct bwfm_pci_softc *);

#ifdef BWFM_DEBUG
void		 bwfm_pci_debug_console(struct bwfm_pci_softc *);
#endif

int		 bwfm_pci_msgbuf_query_dcmd(struct bwfm_softc *, int,
		    int, char *, size_t *);
int		 bwfm_pci_msgbuf_set_dcmd(struct bwfm_softc *, int,
		    int, char *, size_t);
void		 bwfm_pci_msgbuf_rxioctl(struct bwfm_pci_softc *,
		    struct msgbuf_ioctl_resp_hdr *);
int		 bwfm_pci_msgbuf_h2d_mb_write(struct bwfm_pci_softc *,
		    uint32_t);

struct bwfm_buscore_ops bwfm_pci_buscore_ops = {
	.bc_read = bwfm_pci_buscore_read,
	.bc_write = bwfm_pci_buscore_write,
	.bc_prepare = bwfm_pci_buscore_prepare,
	.bc_reset = bwfm_pci_buscore_reset,
	.bc_setup = NULL,
	.bc_activate = bwfm_pci_buscore_activate,
};

struct bwfm_bus_ops bwfm_pci_bus_ops = {
	.bs_preinit = bwfm_pci_preinit,
	.bs_stop = bwfm_pci_stop,
	.bs_txcheck = bwfm_pci_txcheck,
	.bs_txdata = bwfm_pci_txdata,
	.bs_txctl = NULL,
};

struct bwfm_proto_ops bwfm_pci_msgbuf_ops = {
	.proto_query_dcmd = bwfm_pci_msgbuf_query_dcmd,
	.proto_set_dcmd = bwfm_pci_msgbuf_set_dcmd,
	.proto_rx = NULL,
	.proto_rxctl = NULL,
};

const struct cfattach bwfm_pci_ca = {
	sizeof(struct bwfm_pci_softc),
	bwfm_pci_match,
	bwfm_pci_attach,
	bwfm_pci_detach,
	bwfm_pci_activate,
};

static const struct pci_matchid bwfm_pci_devices[] = {
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4350 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4356 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM43602 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4371 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4378 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4387 },
};

int
bwfm_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, bwfm_pci_devices,
	    nitems(bwfm_pci_devices)));
}

void
bwfm_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct bwfm_pci_softc *sc = (struct bwfm_pci_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	const char *intrstr;
	pci_intr_handle_t ih;

	if (pci_mapreg_map(pa, PCI_MAPREG_START + 0x08,
	    PCI_MAPREG_MEM_TYPE_64BIT, 0, &sc->sc_tcm_iot, &sc->sc_tcm_ioh,
	    NULL, &sc->sc_tcm_ios, 0)) {
		printf(": can't map bar1\n");
		return;
	}

	if (pci_mapreg_map(pa, PCI_MAPREG_START + 0x00,
	    PCI_MAPREG_MEM_TYPE_64BIT, 0, &sc->sc_reg_iot, &sc->sc_reg_ioh,
	    NULL, &sc->sc_reg_ios, 0)) {
		printf(": can't map bar0\n");
		goto bar1;
	}

	sc->sc_pcie_iot = sc->sc_reg_iot;
	bus_space_subregion(sc->sc_reg_iot, sc->sc_reg_ioh, 0x2000,
	    sc->sc_reg_ios - 0x2000, &sc->sc_pcie_ioh);

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_id = pa->pa_id;
	sc->sc_dmat = pa->pa_dmat;

	/* Map and establish the interrupt. */
	if (pci_intr_map_msi(pa, &ih) != 0 && pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		goto bar0;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);

	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_NET,
	    bwfm_pci_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto bar1;
	}
	printf(": %s\n", intrstr);

#if defined(__HAVE_FDT)
	sc->sc_sc.sc_node = PCITAG_NODE(pa->pa_tag);
	if (sc->sc_sc.sc_node) {
		if (OF_getproplen(sc->sc_sc.sc_node, "brcm,cal-blob") > 0) {
			sc->sc_sc.sc_calsize = OF_getproplen(sc->sc_sc.sc_node,
			    "brcm,cal-blob");
			sc->sc_sc.sc_cal = malloc(sc->sc_sc.sc_calsize,
			    M_DEVBUF, M_WAITOK);
			OF_getprop(sc->sc_sc.sc_node, "brcm,cal-blob",
			    sc->sc_sc.sc_cal, sc->sc_sc.sc_calsize);
		}
	}
#endif

	sc->sc_sc.sc_bus_ops = &bwfm_pci_bus_ops;
	sc->sc_sc.sc_proto_ops = &bwfm_pci_msgbuf_ops;
	bwfm_attach(&sc->sc_sc);
	config_mountroot(self, bwfm_attachhook);
	return;

bar0:
	bus_space_unmap(sc->sc_reg_iot, sc->sc_reg_ioh, sc->sc_reg_ios);
bar1:
	bus_space_unmap(sc->sc_tcm_iot, sc->sc_tcm_ioh, sc->sc_tcm_ios);
}

int
bwfm_pci_preinit(struct bwfm_softc *bwfm)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	struct bwfm_pci_ringinfo ringinfo;
	const char *chip = NULL;
	u_char *ucode, *nvram;
	size_t size, nvsize, nvlen;
	uint32_t d2h_w_idx_ptr, d2h_r_idx_ptr;
	uint32_t h2d_w_idx_ptr, h2d_r_idx_ptr;
	uint32_t idx_offset, reg;
	int i;

	if (sc->sc_initialized)
		return 0;

	sc->sc_sc.sc_buscore_ops = &bwfm_pci_buscore_ops;
	if (bwfm_chip_attach(&sc->sc_sc) != 0) {
		printf("%s: cannot attach chip\n", DEVNAME(sc));
		return 1;
	}

#if defined(__HAVE_FDT)
	if (bwfm_pci_read_otp(sc)) {
		printf("%s: cannot read OTP\n", DEVNAME(sc));
		return 1;
	}
#endif

	bwfm_pci_select_core(sc, BWFM_AGENT_CORE_PCIE2);
	bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
	    BWFM_PCI_PCIE2REG_CONFIGADDR, 0x4e0);
	reg = bus_space_read_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
	    BWFM_PCI_PCIE2REG_CONFIGDATA);
	bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
	    BWFM_PCI_PCIE2REG_CONFIGDATA, reg);

	switch (bwfm->sc_chip.ch_chip) {
	case BRCM_CC_4350_CHIP_ID:
		if (bwfm->sc_chip.ch_chiprev <= 7)
			chip = "4350c2";
		else
			chip = "4350";
		break;
	case BRCM_CC_4355_CHIP_ID:
		chip = "4355c1";
		break;
	case BRCM_CC_4356_CHIP_ID:
		chip = "4356";
		break;
	case BRCM_CC_4364_CHIP_ID:
		if (bwfm->sc_chip.ch_chiprev <= 3)
			chip = "4364b2";
		else
			chip = "4364b3";
		break;
	case BRCM_CC_43602_CHIP_ID:
		chip = "43602";
		break;
	case BRCM_CC_4371_CHIP_ID:
		chip = "4371";
		break;
	case BRCM_CC_4377_CHIP_ID:
		chip = "4377b3";
		break;
	case BRCM_CC_4378_CHIP_ID:
		if (bwfm->sc_chip.ch_chiprev <= 3)
			chip = "4378b1";
		else
			chip = "4378b3";
		break;
	case BRCM_CC_4387_CHIP_ID:
		chip = "4387c2";
		break;
	default:
		printf("%s: unknown firmware for chip %s\n",
		    DEVNAME(sc), bwfm->sc_chip.ch_name);
		return 1;
	}

	if (bwfm_loadfirmware(bwfm, chip, "-pcie", &ucode, &size,
	    &nvram, &nvsize, &nvlen) != 0)
		return 1;

	/* Retrieve RAM size from firmware. */
	if (size >= BWFM_RAMSIZE + 8) {
		uint32_t *ramsize = (uint32_t *)&ucode[BWFM_RAMSIZE];
		if (letoh32(ramsize[0]) == BWFM_RAMSIZE_MAGIC)
			bwfm->sc_chip.ch_ramsize = letoh32(ramsize[1]);
	}

	if (bwfm_pci_load_microcode(sc, ucode, size, nvram, nvlen) != 0) {
		printf("%s: could not load microcode\n",
		    DEVNAME(sc));
		free(ucode, M_DEVBUF, size);
		free(nvram, M_DEVBUF, nvsize);
		return 1;
	}
	free(ucode, M_DEVBUF, size);
	free(nvram, M_DEVBUF, nvsize);

	sc->sc_shared_flags = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_INFO);
	sc->sc_shared_version = sc->sc_shared_flags;
	if (sc->sc_shared_version > BWFM_SHARED_INFO_MAX_VERSION ||
	    sc->sc_shared_version < BWFM_SHARED_INFO_MIN_VERSION) {
		printf("%s: PCIe version %d unsupported\n",
		    DEVNAME(sc), sc->sc_shared_version);
		return 1;
	}

	if (sc->sc_shared_version >= 6) {
		uint32_t host_cap;

		if ((sc->sc_shared_flags & BWFM_SHARED_INFO_USE_MAILBOX) == 0)
			sc->sc_mb_via_ctl = 1;

		host_cap = sc->sc_shared_version;
		if (sc->sc_shared_flags & BWFM_SHARED_INFO_HOSTRDY_DB1)
			host_cap |= BWFM_SHARED_HOST_CAP_H2D_ENABLE_HOSTRDY;
		if (sc->sc_shared_flags & BWFM_SHARED_INFO_SHARED_DAR)
			host_cap |= BWFM_SHARED_HOST_CAP_H2D_DAR;
		host_cap |= BWFM_SHARED_HOST_CAP_DS_NO_OOB_DW;

		bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
		    sc->sc_shared_address + BWFM_SHARED_HOST_CAP, host_cap);
		bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
		    sc->sc_shared_address + BWFM_SHARED_HOST_CAP2, 0);
	}

	sc->sc_dma_idx_sz = 0;
	if (sc->sc_shared_flags & BWFM_SHARED_INFO_DMA_INDEX) {
		if (sc->sc_shared_flags & BWFM_SHARED_INFO_DMA_2B_IDX)
			sc->sc_dma_idx_sz = sizeof(uint16_t);
		else
			sc->sc_dma_idx_sz = sizeof(uint32_t);
	}

	/* Maximum RX data buffers in the ring. */
	sc->sc_max_rxbufpost = bus_space_read_2(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_MAX_RXBUFPOST);
	if (sc->sc_max_rxbufpost == 0)
		sc->sc_max_rxbufpost = BWFM_SHARED_MAX_RXBUFPOST_DEFAULT;

	/* Alternative offset of data in a packet */
	sc->sc_rx_dataoffset = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_RX_DATAOFFSET);

	/* For Power Management */
	sc->sc_htod_mb_data_addr = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_HTOD_MB_DATA_ADDR);
	sc->sc_dtoh_mb_data_addr = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_DTOH_MB_DATA_ADDR);

	/* Ring information */
	sc->sc_ring_info_addr = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_RING_INFO_ADDR);

	/* Firmware's "dmesg" */
	sc->sc_console_base_addr = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_CONSOLE_ADDR);
	sc->sc_console_buf_addr = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_console_base_addr + BWFM_CONSOLE_BUFADDR);
	sc->sc_console_buf_size = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_console_base_addr + BWFM_CONSOLE_BUFSIZE);

	/* Read ring information. */
	bus_space_read_region_1(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_ring_info_addr, (void *)&ringinfo, sizeof(ringinfo));

	if (sc->sc_shared_version >= 6) {
		sc->sc_max_submissionrings = le16toh(ringinfo.max_submissionrings);
		sc->sc_max_flowrings = le16toh(ringinfo.max_flowrings);
		sc->sc_max_completionrings = le16toh(ringinfo.max_completionrings);
	} else {
		sc->sc_max_submissionrings = le16toh(ringinfo.max_flowrings);
		sc->sc_max_flowrings = sc->sc_max_submissionrings -
		    BWFM_NUM_TX_MSGRINGS;
		sc->sc_max_completionrings = BWFM_NUM_RX_MSGRINGS;
	}

	if (sc->sc_dma_idx_sz == 0) {
		d2h_w_idx_ptr = letoh32(ringinfo.d2h_w_idx_ptr);
		d2h_r_idx_ptr = letoh32(ringinfo.d2h_r_idx_ptr);
		h2d_w_idx_ptr = letoh32(ringinfo.h2d_w_idx_ptr);
		h2d_r_idx_ptr = letoh32(ringinfo.h2d_r_idx_ptr);
		idx_offset = sizeof(uint32_t);
	} else {
		uint64_t address;

		/* Each TX/RX Ring has a Read and Write Ptr */
		sc->sc_dma_idx_bufsz = (sc->sc_max_submissionrings +
		    sc->sc_max_completionrings) * sc->sc_dma_idx_sz * 2;
		sc->sc_dma_idx_buf = bwfm_pci_dmamem_alloc(sc,
		    sc->sc_dma_idx_bufsz, 8);
		if (sc->sc_dma_idx_buf == NULL) {
			/* XXX: Fallback to TCM? */
			printf("%s: cannot allocate idx buf\n",
			    DEVNAME(sc));
			return 1;
		}

		idx_offset = sc->sc_dma_idx_sz;
		h2d_w_idx_ptr = 0;
		address = BWFM_PCI_DMA_DVA(sc->sc_dma_idx_buf);
		ringinfo.h2d_w_idx_hostaddr_low =
		    htole32(address & 0xffffffff);
		ringinfo.h2d_w_idx_hostaddr_high =
		    htole32(address >> 32);

		h2d_r_idx_ptr = h2d_w_idx_ptr +
		    sc->sc_max_submissionrings * idx_offset;
		address += sc->sc_max_submissionrings * idx_offset;
		ringinfo.h2d_r_idx_hostaddr_low =
		    htole32(address & 0xffffffff);
		ringinfo.h2d_r_idx_hostaddr_high =
		    htole32(address >> 32);

		d2h_w_idx_ptr = h2d_r_idx_ptr +
		    sc->sc_max_submissionrings * idx_offset;
		address += sc->sc_max_submissionrings * idx_offset;
		ringinfo.d2h_w_idx_hostaddr_low =
		    htole32(address & 0xffffffff);
		ringinfo.d2h_w_idx_hostaddr_high =
		    htole32(address >> 32);

		d2h_r_idx_ptr = d2h_w_idx_ptr +
		    sc->sc_max_completionrings * idx_offset;
		address += sc->sc_max_completionrings * idx_offset;
		ringinfo.d2h_r_idx_hostaddr_low =
		    htole32(address & 0xffffffff);
		ringinfo.d2h_r_idx_hostaddr_high =
		    htole32(address >> 32);

		bus_space_write_region_1(sc->sc_tcm_iot, sc->sc_tcm_ioh,
		    sc->sc_ring_info_addr, (void *)&ringinfo, sizeof(ringinfo));
	}

	uint32_t ring_mem_ptr = letoh32(ringinfo.ringmem);
	/* TX ctrl ring: Send ctrl buffers, send IOCTLs */
	if (bwfm_pci_setup_ring(sc, &sc->sc_ctrl_submit, 64, 40,
	    h2d_w_idx_ptr, h2d_r_idx_ptr, 0, idx_offset,
	    &ring_mem_ptr))
		goto cleanup;
	/* TX rxpost ring: Send clean data mbufs for RX */
	if (bwfm_pci_setup_ring(sc, &sc->sc_rxpost_submit, 1024, 32,
	    h2d_w_idx_ptr, h2d_r_idx_ptr, 1, idx_offset,
	    &ring_mem_ptr))
		goto cleanup;
	/* RX completion rings: recv our filled buffers back */
	if (bwfm_pci_setup_ring(sc, &sc->sc_ctrl_complete, 64, 24,
	    d2h_w_idx_ptr, d2h_r_idx_ptr, 0, idx_offset,
	    &ring_mem_ptr))
		goto cleanup;
	if (bwfm_pci_setup_ring(sc, &sc->sc_tx_complete, 1024,
	    sc->sc_shared_version >= 7 ? 24 : 16,
	    d2h_w_idx_ptr, d2h_r_idx_ptr, 1, idx_offset,
	    &ring_mem_ptr))
		goto cleanup;
	if (bwfm_pci_setup_ring(sc, &sc->sc_rx_complete, 1024,
	    sc->sc_shared_version >= 7 ? 40 : 32,
	    d2h_w_idx_ptr, d2h_r_idx_ptr, 2, idx_offset,
	    &ring_mem_ptr))
		goto cleanup;

	/* Dynamic TX rings for actual data */
	sc->sc_flowrings = malloc(sc->sc_max_flowrings *
	    sizeof(struct bwfm_pci_msgring), M_DEVBUF, M_WAITOK | M_ZERO);
	for (i = 0; i < sc->sc_max_flowrings; i++) {
		struct bwfm_pci_msgring *ring = &sc->sc_flowrings[i];
		ring->w_idx_addr = h2d_w_idx_ptr + (i + 2) * idx_offset;
		ring->r_idx_addr = h2d_r_idx_ptr + (i + 2) * idx_offset;
	}

	/* Scratch and ring update buffers for firmware */
	if ((sc->sc_scratch_buf = bwfm_pci_dmamem_alloc(sc,
	    BWFM_DMA_D2H_SCRATCH_BUF_LEN, 8)) == NULL)
		goto cleanup;
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_DMA_SCRATCH_ADDR_LOW,
	    BWFM_PCI_DMA_DVA(sc->sc_scratch_buf) & 0xffffffff);
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_DMA_SCRATCH_ADDR_HIGH,
	    BWFM_PCI_DMA_DVA(sc->sc_scratch_buf) >> 32);
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_DMA_SCRATCH_LEN,
	    BWFM_DMA_D2H_SCRATCH_BUF_LEN);

	if ((sc->sc_ringupd_buf = bwfm_pci_dmamem_alloc(sc,
	    BWFM_DMA_D2H_RINGUPD_BUF_LEN, 8)) == NULL)
		goto cleanup;
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_DMA_RINGUPD_ADDR_LOW,
	    BWFM_PCI_DMA_DVA(sc->sc_ringupd_buf) & 0xffffffff);
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_DMA_RINGUPD_ADDR_HIGH,
	    BWFM_PCI_DMA_DVA(sc->sc_ringupd_buf) >> 32);
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_shared_address + BWFM_SHARED_DMA_RINGUPD_LEN,
	    BWFM_DMA_D2H_RINGUPD_BUF_LEN);

	bwfm_pci_select_core(sc, BWFM_AGENT_CORE_PCIE2);
	bwfm_pci_intr_enable(sc);
	bwfm_pci_hostready(sc);

	/* Maps RX mbufs to a packet id and back. */
	sc->sc_rx_pkts.npkt = BWFM_NUM_RX_PKTIDS;
	sc->sc_rx_pkts.pkts = malloc(BWFM_NUM_RX_PKTIDS *
	    sizeof(struct bwfm_pci_buf), M_DEVBUF, M_WAITOK | M_ZERO);
	for (i = 0; i < BWFM_NUM_RX_PKTIDS; i++)
		bus_dmamap_create(sc->sc_dmat, MSGBUF_MAX_CTL_PKT_SIZE,
		    BWFM_NUM_RX_DESCS, MSGBUF_MAX_CTL_PKT_SIZE, 0, BUS_DMA_WAITOK,
		    &sc->sc_rx_pkts.pkts[i].bb_map);

	/* Maps TX mbufs to a packet id and back. */
	sc->sc_tx_pkts.npkt = BWFM_NUM_TX_PKTIDS;
	sc->sc_tx_pkts.pkts = malloc(BWFM_NUM_TX_PKTIDS
	    * sizeof(struct bwfm_pci_buf), M_DEVBUF, M_WAITOK | M_ZERO);
	for (i = 0; i < BWFM_NUM_TX_PKTIDS; i++)
		bus_dmamap_create(sc->sc_dmat, MSGBUF_MAX_PKT_SIZE,
		    BWFM_NUM_TX_DESCS, MSGBUF_MAX_PKT_SIZE, 0, BUS_DMA_WAITOK,
		    &sc->sc_tx_pkts.pkts[i].bb_map);
	sc->sc_tx_pkts_full = 0;

	/* Maps IOCTL mbufs to a packet id and back. */
	sc->sc_ioctl_pkts.npkt = BWFM_NUM_IOCTL_PKTIDS;
	sc->sc_ioctl_pkts.pkts = malloc(BWFM_NUM_IOCTL_PKTIDS
	    * sizeof(struct bwfm_pci_buf), M_DEVBUF, M_WAITOK | M_ZERO);
	for (i = 0; i < BWFM_NUM_IOCTL_PKTIDS; i++)
		bus_dmamap_create(sc->sc_dmat, MSGBUF_MAX_PKT_SIZE,
		    BWFM_NUM_IOCTL_DESCS, MSGBUF_MAX_PKT_SIZE, 0, BUS_DMA_WAITOK,
		    &sc->sc_ioctl_pkts.pkts[i].bb_map);

	/*
	 * For whatever reason, could also be a bug somewhere in this
	 * driver, the firmware needs a bunch of RX buffers otherwise
	 * it won't send any RX complete messages.
	 */
	if_rxr_init(&sc->sc_rxbuf_ring, min(256, sc->sc_max_rxbufpost),
	    sc->sc_max_rxbufpost);
	if_rxr_init(&sc->sc_ioctl_ring, 8, 8);
	if_rxr_init(&sc->sc_event_ring, 8, 8);
	bwfm_pci_fill_rx_rings(sc);

	TAILQ_INIT(&sc->sc_ioctlq);

#ifdef BWFM_DEBUG
	sc->sc_console_readidx = 0;
	bwfm_pci_debug_console(sc);
#endif

	sc->sc_initialized = 1;
	return 0;

cleanup:
	if (sc->sc_ringupd_buf)
		bwfm_pci_dmamem_free(sc, sc->sc_ringupd_buf);
	if (sc->sc_scratch_buf)
		bwfm_pci_dmamem_free(sc, sc->sc_scratch_buf);
	if (sc->sc_rx_complete.ring)
		bwfm_pci_dmamem_free(sc, sc->sc_rx_complete.ring);
	if (sc->sc_tx_complete.ring)
		bwfm_pci_dmamem_free(sc, sc->sc_tx_complete.ring);
	if (sc->sc_ctrl_complete.ring)
		bwfm_pci_dmamem_free(sc, sc->sc_ctrl_complete.ring);
	if (sc->sc_rxpost_submit.ring)
		bwfm_pci_dmamem_free(sc, sc->sc_rxpost_submit.ring);
	if (sc->sc_ctrl_submit.ring)
		bwfm_pci_dmamem_free(sc, sc->sc_ctrl_submit.ring);
	if (sc->sc_dma_idx_buf)
		bwfm_pci_dmamem_free(sc, sc->sc_dma_idx_buf);
	return 1;
}

int
bwfm_pci_load_microcode(struct bwfm_pci_softc *sc, const u_char *ucode, size_t size,
    const u_char *nvram, size_t nvlen)
{
	struct bwfm_softc *bwfm = (void *)sc;
	struct bwfm_core *core;
	struct bwfm_pci_random_seed_footer footer;
	uint32_t addr, shared, written;
	uint8_t *rndbuf;
	int i;

	if (bwfm->sc_chip.ch_chip == BRCM_CC_43602_CHIP_ID) {
		bwfm_pci_select_core(sc, BWFM_AGENT_CORE_ARM_CR4);
		bus_space_write_4(sc->sc_reg_iot, sc->sc_reg_ioh,
		    BWFM_PCI_ARMCR4REG_BANKIDX, 5);
		bus_space_write_4(sc->sc_reg_iot, sc->sc_reg_ioh,
		    BWFM_PCI_ARMCR4REG_BANKPDA, 0);
		bus_space_write_4(sc->sc_reg_iot, sc->sc_reg_ioh,
		    BWFM_PCI_ARMCR4REG_BANKIDX, 7);
		bus_space_write_4(sc->sc_reg_iot, sc->sc_reg_ioh,
		    BWFM_PCI_ARMCR4REG_BANKPDA, 0);
	}

	for (i = 0; i < size; i++)
		bus_space_write_1(sc->sc_tcm_iot, sc->sc_tcm_ioh,
		    bwfm->sc_chip.ch_rambase + i, ucode[i]);

	/* Firmware replaces this with a pointer once up. */
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    bwfm->sc_chip.ch_rambase + bwfm->sc_chip.ch_ramsize - 4, 0);

	if (nvram) {
		addr = bwfm->sc_chip.ch_rambase + bwfm->sc_chip.ch_ramsize -
		    nvlen;
		for (i = 0; i < nvlen; i++)
			bus_space_write_1(sc->sc_tcm_iot, sc->sc_tcm_ioh,
			    addr + i, nvram[i]);

		footer.length = htole32(BWFM_RANDOM_SEED_LENGTH);
		footer.magic = htole32(BWFM_RANDOM_SEED_MAGIC);
		addr -= sizeof(footer);
		for (i = 0; i < sizeof(footer); i++)
			bus_space_write_1(sc->sc_tcm_iot, sc->sc_tcm_ioh,
			    addr + i, ((uint8_t *)&footer)[i]);

		rndbuf = malloc(BWFM_RANDOM_SEED_LENGTH, M_TEMP, M_WAITOK);
		arc4random_buf(rndbuf, BWFM_RANDOM_SEED_LENGTH);
		addr -= BWFM_RANDOM_SEED_LENGTH;
		for (i = 0; i < BWFM_RANDOM_SEED_LENGTH; i++)
			bus_space_write_1(sc->sc_tcm_iot, sc->sc_tcm_ioh,
			    addr + i, rndbuf[i]);
		free(rndbuf, M_TEMP, BWFM_RANDOM_SEED_LENGTH);
	}

	written = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    bwfm->sc_chip.ch_rambase + bwfm->sc_chip.ch_ramsize - 4);

	/* Load reset vector from firmware and kickstart core. */
	if (bwfm->sc_chip.ch_chip == BRCM_CC_43602_CHIP_ID) {
		core = bwfm_chip_get_core(bwfm, BWFM_AGENT_INTERNAL_MEM);
		bwfm->sc_chip.ch_core_reset(bwfm, core, 0, 0, 0);
	}
	bwfm_chip_set_active(bwfm, *(uint32_t *)ucode);

	for (i = 0; i < 100; i++) {
		delay(50 * 1000);
		shared = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
		    bwfm->sc_chip.ch_rambase + bwfm->sc_chip.ch_ramsize - 4);
		if (shared != written)
			break;
	}
	if (shared == written) {
		printf("%s: firmware did not come up\n", DEVNAME(sc));
		return 1;
	}
	if (shared < bwfm->sc_chip.ch_rambase ||
	    shared >= bwfm->sc_chip.ch_rambase + bwfm->sc_chip.ch_ramsize) {
		printf("%s: invalid shared RAM address 0x%08x\n", DEVNAME(sc),
		    shared);
		return 1;
	}

	sc->sc_shared_address = shared;
	return 0;
}

int
bwfm_pci_detach(struct device *self, int flags)
{
	struct bwfm_pci_softc *sc = (struct bwfm_pci_softc *)self;

	bwfm_detach(&sc->sc_sc, flags);
	bwfm_pci_cleanup(sc);

	return 0;
}

void
bwfm_pci_cleanup(struct bwfm_pci_softc *sc)
{
	int i;

	for (i = 0; i < BWFM_NUM_RX_PKTIDS; i++) {
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_rx_pkts.pkts[i].bb_map);
		if (sc->sc_rx_pkts.pkts[i].bb_m)
			m_freem(sc->sc_rx_pkts.pkts[i].bb_m);
	}
	free(sc->sc_rx_pkts.pkts, M_DEVBUF, BWFM_NUM_RX_PKTIDS *
	    sizeof(struct bwfm_pci_buf));

	for (i = 0; i < BWFM_NUM_TX_PKTIDS; i++) {
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_tx_pkts.pkts[i].bb_map);
		if (sc->sc_tx_pkts.pkts[i].bb_m)
			m_freem(sc->sc_tx_pkts.pkts[i].bb_m);
	}
	free(sc->sc_tx_pkts.pkts, M_DEVBUF, BWFM_NUM_TX_PKTIDS *
	    sizeof(struct bwfm_pci_buf));

	for (i = 0; i < BWFM_NUM_IOCTL_PKTIDS; i++) {
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_ioctl_pkts.pkts[i].bb_map);
		if (sc->sc_ioctl_pkts.pkts[i].bb_m)
			m_freem(sc->sc_ioctl_pkts.pkts[i].bb_m);
	}
	free(sc->sc_ioctl_pkts.pkts, M_DEVBUF, BWFM_NUM_IOCTL_PKTIDS *
	    sizeof(struct bwfm_pci_buf));

	for (i = 0; i < sc->sc_max_flowrings; i++) {
		if (sc->sc_flowrings[i].status >= RING_OPEN)
			bwfm_pci_dmamem_free(sc, sc->sc_flowrings[i].ring);
	}
	free(sc->sc_flowrings, M_DEVBUF, sc->sc_max_flowrings *
	    sizeof(struct bwfm_pci_msgring));

	bwfm_pci_dmamem_free(sc, sc->sc_ringupd_buf);
	bwfm_pci_dmamem_free(sc, sc->sc_scratch_buf);
	bwfm_pci_dmamem_free(sc, sc->sc_rx_complete.ring);
	bwfm_pci_dmamem_free(sc, sc->sc_tx_complete.ring);
	bwfm_pci_dmamem_free(sc, sc->sc_ctrl_complete.ring);
	bwfm_pci_dmamem_free(sc, sc->sc_rxpost_submit.ring);
	bwfm_pci_dmamem_free(sc, sc->sc_ctrl_submit.ring);
	if (sc->sc_dma_idx_buf) {
		bwfm_pci_dmamem_free(sc, sc->sc_dma_idx_buf);
		sc->sc_dma_idx_buf = NULL;
	}

	sc->sc_initialized = 0;
}

int
bwfm_pci_activate(struct device *self, int act)
{
	struct bwfm_pci_softc *sc = (struct bwfm_pci_softc *)self;
	struct bwfm_softc *bwfm = (void *)sc;
	int error = 0;

	switch (act) {
	case DVACT_QUIESCE:
		error = bwfm_activate(bwfm, act);
		if (error)
			return error;
		if (sc->sc_initialized) {
			sc->sc_mbdata_done = 0;
			error = bwfm_pci_send_mb_data(sc,
			    BWFM_PCI_H2D_HOST_D3_INFORM);
			if (error)
				return error;
			tsleep_nsec(&sc->sc_mbdata_done, PCATCH,
			    DEVNAME(sc), SEC_TO_NSEC(2));
			if (!sc->sc_mbdata_done)
				return ETIMEDOUT;
		}
		break;
	case DVACT_WAKEUP:
		if (sc->sc_initialized) {
			/* If device can't be resumed, re-init. */
			if (bwfm_pci_intmask(sc) == 0 ||
			    bwfm_pci_send_mb_data(sc,
			    BWFM_PCI_H2D_HOST_D0_INFORM) != 0) {
				bwfm_cleanup(bwfm);
				bwfm_pci_cleanup(sc);
			} else {
				bwfm_pci_select_core(sc, BWFM_AGENT_CORE_PCIE2);
				bwfm_pci_intr_enable(sc);
				bwfm_pci_hostready(sc);
			}
		}
		error = bwfm_activate(bwfm, act);
		if (error)
			return error;
		break;
	default:
		break;
	}

	return 0;
}

#if defined(__HAVE_FDT)
int
bwfm_pci_read_otp(struct bwfm_pci_softc *sc)
{
	struct bwfm_softc *bwfm = (void *)sc;
	struct bwfm_core *core;
	uint32_t coreid, base, words;
	uint32_t page, offset, sromctl;
	uint8_t *otp;
	int i;

	switch (bwfm->sc_chip.ch_chip) {
	case BRCM_CC_4355_CHIP_ID:
		coreid = BWFM_AGENT_CORE_CHIPCOMMON;
		base = 0x8c0;
		words = 0xb2;
		break;
	case BRCM_CC_4364_CHIP_ID:
		coreid = BWFM_AGENT_CORE_CHIPCOMMON;
		base = 0x8c0;
		words = 0x1a0;
		break;
	case BRCM_CC_4377_CHIP_ID:
	case BRCM_CC_4378_CHIP_ID:
		coreid = BWFM_AGENT_CORE_GCI;
		base = 0x1120;
		words = 0x170;
		break;
	case BRCM_CC_4387_CHIP_ID:
		coreid = BWFM_AGENT_CORE_GCI;
		base = 0x113c;
		words = 0x170;
		break;
	default:
		return 0;
	}

	core = bwfm_chip_get_core(bwfm, coreid);
	if (core == NULL)
		return 1;

	/* Map OTP to shadow area */
	if (coreid == BWFM_AGENT_CORE_CHIPCOMMON) {
		bwfm_pci_select_core(sc, coreid);
		sromctl = bus_space_read_4(sc->sc_reg_iot, sc->sc_reg_ioh,
		    BWFM_CHIP_REG_SROMCONTROL);

		if (!(sromctl & BWFM_CHIP_REG_SROMCONTROL_OTP_PRESENT))
			return 0;

		bus_space_write_4(sc->sc_reg_iot, sc->sc_reg_ioh,
		    BWFM_CHIP_REG_SROMCONTROL, sromctl |
		    BWFM_CHIP_REG_SROMCONTROL_OTPSEL);
	}

	/* Map bus window to SROM/OTP shadow area */
	page = (core->co_base + base) & ~(BWFM_PCI_BAR0_REG_SIZE - 1);
	offset = (core->co_base + base) & (BWFM_PCI_BAR0_REG_SIZE - 1);
	pci_conf_write(sc->sc_pc, sc->sc_tag, BWFM_PCI_BAR0_WINDOW, page);

	otp = mallocarray(words, sizeof(uint16_t), M_TEMP, M_WAITOK);
	for (i = 0; i < words; i++)
		((uint16_t *)otp)[i] = bus_space_read_2(sc->sc_reg_iot,
		    sc->sc_reg_ioh, offset + i * sizeof(uint16_t));

	/* Unmap OTP */
	if (coreid == BWFM_AGENT_CORE_CHIPCOMMON) {
		bwfm_pci_select_core(sc, coreid);
		bus_space_write_4(sc->sc_reg_iot, sc->sc_reg_ioh,
		    BWFM_CHIP_REG_SROMCONTROL, sromctl);
	}

	for (i = 0; i < (words * sizeof(uint16_t)) - 1; i += otp[i + 1]) {
		if (otp[i + 0] == 0)
			break;
		if (i + otp[i + 1] > words * sizeof(uint16_t))
			break;
		bwfm_pci_process_otp_tuple(sc, otp[i + 0], otp[i + 1],
		    &otp[i + 2]);
	}

	free(otp, M_TEMP, words * sizeof(uint16_t));
	return 0;
}

void
bwfm_pci_process_otp_tuple(struct bwfm_pci_softc *sc, uint8_t type, uint8_t size,
    uint8_t *data)
{
	struct bwfm_softc *bwfm = (void *)sc;
	char chiprev[8] = "", module[8] = "", modrev[8] = "", vendor[8] = "", chip[8] = "";
	char board_type[128] = "";
	int len;

	switch (type) {
	case 0x15: /* system vendor OTP */
		DPRINTF(("%s: system vendor OTP\n", DEVNAME(sc)));
		if (size < sizeof(uint32_t))
			return;
		if (data[0] != 0x08 || data[1] != 0x00 ||
		    data[2] != 0x00 || data[3] != 0x00)
			return;
		size -= sizeof(uint32_t);
		data += sizeof(uint32_t);
		while (size) {
			/* reached end */
			if (data[0] == 0xff)
				break;
			for (len = 0; len < size; len++)
				if (data[len] == 0x00 || data[len] == ' ' ||
				    data[len] == 0xff)
					break;
			if (len < 3 || len > 9) /* X=abcdef */
				goto next;
			if (data[1] != '=')
				goto next;
			/* NULL-terminate string */
			if (data[len] == ' ')
				data[len] = '\0';
			switch (data[0]) {
			case 's':
				strlcpy(chiprev, &data[2], sizeof(chiprev));
				break;
			case 'M':
				strlcpy(module, &data[2], sizeof(module));
				break;
			case 'm':
				strlcpy(modrev, &data[2], sizeof(modrev));
				break;
			case 'V':
				strlcpy(vendor, &data[2], sizeof(vendor));
				break;
			}
next:
			/* skip content */
			data += len;
			size -= len;
			/* skip spacer tag */
			if (size) {
				data++;
				size--;
			}
		}
		snprintf(chip, sizeof(chip),
		    bwfm->sc_chip.ch_chip > 40000 ? "%05d" : "%04x",
		    bwfm->sc_chip.ch_chip);
		if (sc->sc_sc.sc_node) {
			OF_getprop(sc->sc_sc.sc_node, "brcm,board-type",
			    board_type, sizeof(board_type));
			if (strncmp(board_type, "apple,", 6) == 0) {
				strlcpy(sc->sc_sc.sc_fwdir, "apple-bwfm/",
				    sizeof(sc->sc_sc.sc_fwdir));
			}
		}
		strlcpy(sc->sc_sc.sc_board_type, board_type,
		    sizeof(sc->sc_sc.sc_board_type));
		strlcpy(sc->sc_sc.sc_module, module,
		    sizeof(sc->sc_sc.sc_module));
		strlcpy(sc->sc_sc.sc_vendor, vendor,
		    sizeof(sc->sc_sc.sc_vendor));
		strlcpy(sc->sc_sc.sc_modrev, modrev,
		    sizeof(sc->sc_sc.sc_modrev));
		break;
	case 0x80: /* Broadcom CIS */
		DPRINTF(("%s: Broadcom CIS\n", DEVNAME(sc)));
		break;
	default:
		DPRINTF(("%s: unknown OTP tuple\n", DEVNAME(sc)));
		break;
	}
}
#endif

/* DMA code */
struct bwfm_pci_dmamem *
bwfm_pci_dmamem_alloc(struct bwfm_pci_softc *sc, bus_size_t size, bus_size_t align)
{
	struct bwfm_pci_dmamem *bdm;
	int nsegs;

	bdm = malloc(sizeof(*bdm), M_DEVBUF, M_WAITOK | M_ZERO);
	bdm->bdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &bdm->bdm_map) != 0)
		goto bdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, align, 0, &bdm->bdm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &bdm->bdm_seg, nsegs, size,
	    &bdm->bdm_kva, BUS_DMA_WAITOK | BUS_DMA_COHERENT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, bdm->bdm_map, bdm->bdm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	bzero(bdm->bdm_kva, size);

	return (bdm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, bdm->bdm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &bdm->bdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, bdm->bdm_map);
bdmfree:
	free(bdm, M_DEVBUF, sizeof(*bdm));

	return (NULL);
}

void
bwfm_pci_dmamem_free(struct bwfm_pci_softc *sc, struct bwfm_pci_dmamem *bdm)
{
	bus_dmamem_unmap(sc->sc_dmat, bdm->bdm_kva, bdm->bdm_size);
	bus_dmamem_free(sc->sc_dmat, &bdm->bdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, bdm->bdm_map);
	free(bdm, M_DEVBUF, sizeof(*bdm));
}

/*
 * We need a simple mapping from a packet ID to mbufs, because when
 * a transfer completed, we only know the ID so we have to look up
 * the memory for the ID.  This simply looks for an empty slot.
 */
int
bwfm_pci_pktid_avail(struct bwfm_pci_softc *sc, struct bwfm_pci_pkts *pkts)
{
	int i, idx;

	idx = pkts->last + 1;
	for (i = 0; i < pkts->npkt; i++) {
		if (idx == pkts->npkt)
			idx = 0;
		if (pkts->pkts[idx].bb_m == NULL)
			return 0;
		idx++;
	}
	return ENOBUFS;
}

int
bwfm_pci_pktid_new(struct bwfm_pci_softc *sc, struct bwfm_pci_pkts *pkts,
    struct mbuf *m, uint32_t *pktid, paddr_t *paddr)
{
	int i, idx;

	idx = pkts->last + 1;
	for (i = 0; i < pkts->npkt; i++) {
		if (idx == pkts->npkt)
			idx = 0;
		if (pkts->pkts[idx].bb_m == NULL) {
			if (bus_dmamap_load_mbuf(sc->sc_dmat,
			    pkts->pkts[idx].bb_map, m, BUS_DMA_NOWAIT) != 0) {
				if (m_defrag(m, M_DONTWAIT))
					return EFBIG;
				if (bus_dmamap_load_mbuf(sc->sc_dmat,
				    pkts->pkts[idx].bb_map, m, BUS_DMA_NOWAIT) != 0)
					return EFBIG;
			}
			bus_dmamap_sync(sc->sc_dmat, pkts->pkts[idx].bb_map,
			    0, pkts->pkts[idx].bb_map->dm_mapsize,
			    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
			pkts->last = idx;
			pkts->pkts[idx].bb_m = m;
			*pktid = idx;
			*paddr = pkts->pkts[idx].bb_map->dm_segs[0].ds_addr;
			return 0;
		}
		idx++;
	}
	return ENOBUFS;
}

struct mbuf *
bwfm_pci_pktid_free(struct bwfm_pci_softc *sc, struct bwfm_pci_pkts *pkts,
    uint32_t pktid)
{
	struct mbuf *m;

	if (pktid >= pkts->npkt || pkts->pkts[pktid].bb_m == NULL)
		return NULL;
	bus_dmamap_sync(sc->sc_dmat, pkts->pkts[pktid].bb_map, 0,
	    pkts->pkts[pktid].bb_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, pkts->pkts[pktid].bb_map);
	m = pkts->pkts[pktid].bb_m;
	pkts->pkts[pktid].bb_m = NULL;
	return m;
}

void
bwfm_pci_fill_rx_rings(struct bwfm_pci_softc *sc)
{
	bwfm_pci_fill_rx_buf_ring(sc);
	bwfm_pci_fill_rx_ioctl_ring(sc, &sc->sc_ioctl_ring,
	    MSGBUF_TYPE_IOCTLRESP_BUF_POST);
	bwfm_pci_fill_rx_ioctl_ring(sc, &sc->sc_event_ring,
	    MSGBUF_TYPE_EVENT_BUF_POST);
}

void
bwfm_pci_fill_rx_ioctl_ring(struct bwfm_pci_softc *sc, struct if_rxring *rxring,
    uint32_t msgtype)
{
	struct msgbuf_rx_ioctl_resp_or_event *req;
	struct mbuf *m;
	uint32_t pktid;
	paddr_t paddr;
	int s, slots;

	s = splnet();
	for (slots = if_rxr_get(rxring, 8); slots > 0; slots--) {
		if (bwfm_pci_pktid_avail(sc, &sc->sc_rx_pkts))
			break;
		req = bwfm_pci_ring_write_reserve(sc, &sc->sc_ctrl_submit);
		if (req == NULL)
			break;
		m = MCLGETL(NULL, M_DONTWAIT, MSGBUF_MAX_CTL_PKT_SIZE);
		if (m == NULL) {
			bwfm_pci_ring_write_cancel(sc, &sc->sc_ctrl_submit, 1);
			break;
		}
		m->m_len = m->m_pkthdr.len = MSGBUF_MAX_CTL_PKT_SIZE;
		if (bwfm_pci_pktid_new(sc, &sc->sc_rx_pkts, m, &pktid, &paddr)) {
			bwfm_pci_ring_write_cancel(sc, &sc->sc_ctrl_submit, 1);
			m_freem(m);
			break;
		}
		memset(req, 0, sizeof(*req));
		req->msg.msgtype = msgtype;
		req->msg.request_id = htole32(pktid);
		req->host_buf_len = htole16(MSGBUF_MAX_CTL_PKT_SIZE);
		req->host_buf_addr.high_addr = htole32((uint64_t)paddr >> 32);
		req->host_buf_addr.low_addr = htole32(paddr & 0xffffffff);
		bwfm_pci_ring_write_commit(sc, &sc->sc_ctrl_submit);
	}
	if_rxr_put(rxring, slots);
	splx(s);
}

void
bwfm_pci_fill_rx_buf_ring(struct bwfm_pci_softc *sc)
{
	struct msgbuf_rx_bufpost *req;
	struct mbuf *m;
	uint32_t pktid;
	paddr_t paddr;
	int s, slots;

	s = splnet();
	for (slots = if_rxr_get(&sc->sc_rxbuf_ring, sc->sc_max_rxbufpost);
	    slots > 0; slots--) {
		if (bwfm_pci_pktid_avail(sc, &sc->sc_rx_pkts))
			break;
		req = bwfm_pci_ring_write_reserve(sc, &sc->sc_rxpost_submit);
		if (req == NULL)
			break;
		m = MCLGETL(NULL, M_DONTWAIT, MSGBUF_MAX_PKT_SIZE);
		if (m == NULL) {
			bwfm_pci_ring_write_cancel(sc, &sc->sc_rxpost_submit, 1);
			break;
		}
		m->m_len = m->m_pkthdr.len = MSGBUF_MAX_PKT_SIZE;
		if (bwfm_pci_pktid_new(sc, &sc->sc_rx_pkts, m, &pktid, &paddr)) {
			bwfm_pci_ring_write_cancel(sc, &sc->sc_rxpost_submit, 1);
			m_freem(m);
			break;
		}
		memset(req, 0, sizeof(*req));
		req->msg.msgtype = MSGBUF_TYPE_RXBUF_POST;
		req->msg.request_id = htole32(pktid);
		req->data_buf_len = htole16(MSGBUF_MAX_PKT_SIZE);
		req->data_buf_addr.high_addr = htole32((uint64_t)paddr >> 32);
		req->data_buf_addr.low_addr = htole32(paddr & 0xffffffff);
		bwfm_pci_ring_write_commit(sc, &sc->sc_rxpost_submit);
	}
	if_rxr_put(&sc->sc_rxbuf_ring, slots);
	splx(s);
}

int
bwfm_pci_setup_ring(struct bwfm_pci_softc *sc, struct bwfm_pci_msgring *ring,
    int nitem, size_t itemsz, uint32_t w_idx, uint32_t r_idx,
    int idx, uint32_t idx_off, uint32_t *ring_mem)
{
	ring->w_idx_addr = w_idx + idx * idx_off;
	ring->r_idx_addr = r_idx + idx * idx_off;
	ring->w_ptr = 0;
	ring->r_ptr = 0;
	ring->nitem = nitem;
	ring->itemsz = itemsz;
	bwfm_pci_ring_write_rptr(sc, ring);
	bwfm_pci_ring_write_wptr(sc, ring);

	ring->ring = bwfm_pci_dmamem_alloc(sc, nitem * itemsz, 8);
	if (ring->ring == NULL)
		return ENOMEM;
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    *ring_mem + BWFM_RING_MEM_BASE_ADDR_LOW,
	    BWFM_PCI_DMA_DVA(ring->ring) & 0xffffffff);
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    *ring_mem + BWFM_RING_MEM_BASE_ADDR_HIGH,
	    BWFM_PCI_DMA_DVA(ring->ring) >> 32);
	bus_space_write_2(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    *ring_mem + BWFM_RING_MAX_ITEM, nitem);
	bus_space_write_2(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    *ring_mem + BWFM_RING_LEN_ITEMS, itemsz);
	*ring_mem = *ring_mem + BWFM_RING_MEM_SZ;
	return 0;
}

int
bwfm_pci_setup_flowring(struct bwfm_pci_softc *sc, struct bwfm_pci_msgring *ring,
    int nitem, size_t itemsz)
{
	ring->w_ptr = 0;
	ring->r_ptr = 0;
	ring->nitem = nitem;
	ring->itemsz = itemsz;
	bwfm_pci_ring_write_rptr(sc, ring);
	bwfm_pci_ring_write_wptr(sc, ring);

	ring->ring = bwfm_pci_dmamem_alloc(sc, nitem * itemsz, 8);
	if (ring->ring == NULL)
		return ENOMEM;
	return 0;
}

/* Ring helpers */
void
bwfm_pci_ring_bell(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring)
{
	if (sc->sc_shared_flags & BWFM_SHARED_INFO_SHARED_DAR)
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_64_PCIE2REG_H2D_MAILBOX_0, 1);
	else
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_PCIE2REG_H2D_MAILBOX_0, 1);
}

void
bwfm_pci_ring_update_rptr(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring)
{
	if (sc->sc_dma_idx_sz == 0) {
		ring->r_ptr = bus_space_read_2(sc->sc_tcm_iot,
		    sc->sc_tcm_ioh, ring->r_idx_addr);
	} else {
		bus_dmamap_sync(sc->sc_dmat,
		    BWFM_PCI_DMA_MAP(sc->sc_dma_idx_buf), ring->r_idx_addr,
		    sizeof(uint16_t), BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		ring->r_ptr = *(uint16_t *)(BWFM_PCI_DMA_KVA(sc->sc_dma_idx_buf)
		    + ring->r_idx_addr);
	}
}

void
bwfm_pci_ring_update_wptr(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring)
{
	if (sc->sc_dma_idx_sz == 0) {
		ring->w_ptr = bus_space_read_2(sc->sc_tcm_iot,
		    sc->sc_tcm_ioh, ring->w_idx_addr);
	} else {
		bus_dmamap_sync(sc->sc_dmat,
		    BWFM_PCI_DMA_MAP(sc->sc_dma_idx_buf), ring->w_idx_addr,
		    sizeof(uint16_t), BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		ring->w_ptr = *(uint16_t *)(BWFM_PCI_DMA_KVA(sc->sc_dma_idx_buf)
		    + ring->w_idx_addr);
	}
}

void
bwfm_pci_ring_write_rptr(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring)
{
	if (sc->sc_dma_idx_sz == 0) {
		bus_space_write_2(sc->sc_tcm_iot, sc->sc_tcm_ioh,
		    ring->r_idx_addr, ring->r_ptr);
	} else {
		*(uint16_t *)(BWFM_PCI_DMA_KVA(sc->sc_dma_idx_buf)
		    + ring->r_idx_addr) = ring->r_ptr;
		bus_dmamap_sync(sc->sc_dmat,
		    BWFM_PCI_DMA_MAP(sc->sc_dma_idx_buf), ring->r_idx_addr,
		    sizeof(uint16_t), BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

void
bwfm_pci_ring_write_wptr(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring)
{
	if (sc->sc_dma_idx_sz == 0) {
		bus_space_write_2(sc->sc_tcm_iot, sc->sc_tcm_ioh,
		    ring->w_idx_addr, ring->w_ptr);
	} else {
		*(uint16_t *)(BWFM_PCI_DMA_KVA(sc->sc_dma_idx_buf)
		    + ring->w_idx_addr) = ring->w_ptr;
		bus_dmamap_sync(sc->sc_dmat,
		    BWFM_PCI_DMA_MAP(sc->sc_dma_idx_buf), ring->w_idx_addr,
		    sizeof(uint16_t), BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

/*
 * Retrieve a free descriptor to put new stuff in, but don't commit
 * to it yet so we can rollback later if any error occurs.
 */
void *
bwfm_pci_ring_write_reserve(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring)
{
	int available;
	char *ret;

	bwfm_pci_ring_update_rptr(sc, ring);

	if (ring->r_ptr > ring->w_ptr)
		available = ring->r_ptr - ring->w_ptr;
	else
		available = ring->r_ptr + (ring->nitem - ring->w_ptr);

	if (available <= 1)
		return NULL;

	ret = BWFM_PCI_DMA_KVA(ring->ring) + (ring->w_ptr * ring->itemsz);
	ring->w_ptr += 1;
	if (ring->w_ptr == ring->nitem)
		ring->w_ptr = 0;
	return ret;
}

void *
bwfm_pci_ring_write_reserve_multi(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring, int count, int *avail)
{
	int available;
	char *ret;

	bwfm_pci_ring_update_rptr(sc, ring);

	if (ring->r_ptr > ring->w_ptr)
		available = ring->r_ptr - ring->w_ptr;
	else
		available = ring->r_ptr + (ring->nitem - ring->w_ptr);

	if (available <= 1)
		return NULL;

	ret = BWFM_PCI_DMA_KVA(ring->ring) + (ring->w_ptr * ring->itemsz);
	*avail = min(count, available - 1);
	if (*avail + ring->w_ptr > ring->nitem)
		*avail = ring->nitem - ring->w_ptr;
	ring->w_ptr += *avail;
	if (ring->w_ptr == ring->nitem)
		ring->w_ptr = 0;
	return ret;
}

/*
 * Read number of descriptors available (submitted by the firmware)
 * and retrieve pointer to first descriptor.
 */
void *
bwfm_pci_ring_read_avail(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring, int *avail)
{
	bwfm_pci_ring_update_wptr(sc, ring);

	if (ring->w_ptr >= ring->r_ptr)
		*avail = ring->w_ptr - ring->r_ptr;
	else
		*avail = ring->nitem - ring->r_ptr;

	if (*avail == 0)
		return NULL;

	bus_dmamap_sync(sc->sc_dmat, BWFM_PCI_DMA_MAP(ring->ring),
	    ring->r_ptr * ring->itemsz, *avail * ring->itemsz,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	return BWFM_PCI_DMA_KVA(ring->ring) + (ring->r_ptr * ring->itemsz);
}

/*
 * Let firmware know we read N descriptors.
 */
void
bwfm_pci_ring_read_commit(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring, int nitem)
{
	ring->r_ptr += nitem;
	if (ring->r_ptr == ring->nitem)
		ring->r_ptr = 0;
	bwfm_pci_ring_write_rptr(sc, ring);
}

/*
 * Let firmware know that we submitted some descriptors.
 */
void
bwfm_pci_ring_write_commit(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring)
{
	bus_dmamap_sync(sc->sc_dmat, BWFM_PCI_DMA_MAP(ring->ring),
	    0, BWFM_PCI_DMA_LEN(ring->ring), BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	bwfm_pci_ring_write_wptr(sc, ring);
	bwfm_pci_ring_bell(sc, ring);
}

/*
 * Rollback N descriptors in case we don't actually want
 * to commit to it.
 */
void
bwfm_pci_ring_write_cancel(struct bwfm_pci_softc *sc,
    struct bwfm_pci_msgring *ring, int nitem)
{
	if (ring->w_ptr == 0)
		ring->w_ptr = ring->nitem - nitem;
	else
		ring->w_ptr -= nitem;
}

/*
 * Foreach written descriptor on the ring, pass the descriptor to
 * a message handler and let the firmware know we handled it.
 */
void
bwfm_pci_ring_rx(struct bwfm_pci_softc *sc, struct bwfm_pci_msgring *ring,
    struct mbuf_list *ml)
{
	void *buf;
	int avail, processed;

again:
	buf = bwfm_pci_ring_read_avail(sc, ring, &avail);
	if (buf == NULL)
		return;

	processed = 0;
	while (avail) {
		bwfm_pci_msg_rx(sc, buf + sc->sc_rx_dataoffset, ml);
		buf += ring->itemsz;
		processed++;
		if (processed == 48) {
			bwfm_pci_ring_read_commit(sc, ring, processed);
			processed = 0;
		}
		avail--;
	}
	if (processed)
		bwfm_pci_ring_read_commit(sc, ring, processed);
	if (ring->r_ptr == 0)
		goto again;
}

void
bwfm_pci_msg_rx(struct bwfm_pci_softc *sc, void *buf, struct mbuf_list *ml)
{
	struct ifnet *ifp = &sc->sc_sc.sc_ic.ic_if;
	struct msgbuf_ioctl_resp_hdr *resp;
	struct msgbuf_tx_status *tx;
	struct msgbuf_rx_complete *rx;
	struct msgbuf_rx_event *event;
	struct msgbuf_d2h_mailbox_data *d2h;
	struct msgbuf_common_hdr *msg;
	struct msgbuf_flowring_create_resp *fcr;
	struct msgbuf_flowring_delete_resp *fdr;
	struct bwfm_cmd_flowring_create fdcmd;
	struct bwfm_pci_msgring *ring;
	struct mbuf *m;
	int flowid;

	msg = (struct msgbuf_common_hdr *)buf;
	switch (msg->msgtype)
	{
	case MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT:
		fcr = (struct msgbuf_flowring_create_resp *)buf;
		flowid = letoh16(fcr->compl_hdr.flow_ring_id);
		if (flowid < 2)
			break;
		flowid -= 2;
		if (flowid >= sc->sc_max_flowrings)
			break;
		ring = &sc->sc_flowrings[flowid];
		if (ring->status != RING_OPENING)
			break;
		if (fcr->compl_hdr.status) {
			printf("%s: failed to open flowring %d\n",
			    DEVNAME(sc), flowid);
			ring->status = RING_CLOSED;
			if (ring->m) {
				m_freem(ring->m);
				ring->m = NULL;
			}
			ifq_restart(&ifp->if_snd);
			break;
		}
		ring->status = RING_OPEN;
		if (ring->m != NULL) {
			m = ring->m;
			ring->m = NULL;
			if (bwfm_pci_txdata(&sc->sc_sc, m))
				m_freem(ring->m);
		}
		ifq_restart(&ifp->if_snd);
		break;
	case MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT:
		fdr = (struct msgbuf_flowring_delete_resp *)buf;
		flowid = letoh16(fdr->compl_hdr.flow_ring_id);
		if (flowid < 2)
			break;
		flowid -= 2;
		if (flowid >= sc->sc_max_flowrings)
			break;
		ring = &sc->sc_flowrings[flowid];
		if (ring->status != RING_CLOSING)
			break;
		if (fdr->compl_hdr.status) {
			printf("%s: failed to delete flowring %d\n",
			    DEVNAME(sc), flowid);
			break;
		}
		fdcmd.flowid = flowid;
		bwfm_do_async(&sc->sc_sc, bwfm_pci_flowring_delete_cb,
		    &fdcmd, sizeof(fdcmd));
		break;
	case MSGBUF_TYPE_IOCTLPTR_REQ_ACK:
		m = bwfm_pci_pktid_free(sc, &sc->sc_ioctl_pkts,
		    letoh32(msg->request_id));
		if (m == NULL)
			break;
		m_freem(m);
		break;
	case MSGBUF_TYPE_IOCTL_CMPLT:
		resp = (struct msgbuf_ioctl_resp_hdr *)buf;
		bwfm_pci_msgbuf_rxioctl(sc, resp);
		if_rxr_put(&sc->sc_ioctl_ring, 1);
		bwfm_pci_fill_rx_rings(sc);
		break;
	case MSGBUF_TYPE_WL_EVENT:
		event = (struct msgbuf_rx_event *)buf;
		m = bwfm_pci_pktid_free(sc, &sc->sc_rx_pkts,
		    letoh32(event->msg.request_id));
		if (m == NULL)
			break;
		m_adj(m, sc->sc_rx_dataoffset);
		m->m_len = m->m_pkthdr.len = letoh16(event->event_data_len);
		bwfm_rx(&sc->sc_sc, m, ml);
		if_rxr_put(&sc->sc_event_ring, 1);
		bwfm_pci_fill_rx_rings(sc);
		break;
	case MSGBUF_TYPE_TX_STATUS:
		tx = (struct msgbuf_tx_status *)buf;
		m = bwfm_pci_pktid_free(sc, &sc->sc_tx_pkts,
		    letoh32(tx->msg.request_id) - 1);
		if (m == NULL)
			break;
		m_freem(m);
		if (sc->sc_tx_pkts_full) {
			sc->sc_tx_pkts_full = 0;
			ifq_restart(&ifp->if_snd);
		}
		break;
	case MSGBUF_TYPE_RX_CMPLT:
		rx = (struct msgbuf_rx_complete *)buf;
		m = bwfm_pci_pktid_free(sc, &sc->sc_rx_pkts,
		    letoh32(rx->msg.request_id));
		if (m == NULL)
			break;
		if (letoh16(rx->data_offset))
			m_adj(m, letoh16(rx->data_offset));
		else if (sc->sc_rx_dataoffset)
			m_adj(m, sc->sc_rx_dataoffset);
		m->m_len = m->m_pkthdr.len = letoh16(rx->data_len);
		bwfm_rx(&sc->sc_sc, m, ml);
		if_rxr_put(&sc->sc_rxbuf_ring, 1);
		bwfm_pci_fill_rx_rings(sc);
		break;
	case MSGBUF_TYPE_D2H_MAILBOX_DATA:
		d2h = (struct msgbuf_d2h_mailbox_data *)buf;
		if (d2h->data & BWFM_PCI_D2H_DEV_D3_ACK) {
			sc->sc_mbdata_done = 1;
			wakeup(&sc->sc_mbdata_done);
		}
		break;
	default:
		printf("%s: msgtype 0x%08x\n", __func__, msg->msgtype);
		break;
	}
}

/* Bus core helpers */
void
bwfm_pci_select_core(struct bwfm_pci_softc *sc, int id)
{
	struct bwfm_softc *bwfm = (void *)sc;
	struct bwfm_core *core;

	core = bwfm_chip_get_core(bwfm, id);
	if (core == NULL) {
		printf("%s: could not find core to select", DEVNAME(sc));
		return;
	}

	pci_conf_write(sc->sc_pc, sc->sc_tag,
	    BWFM_PCI_BAR0_WINDOW, core->co_base);
	if (pci_conf_read(sc->sc_pc, sc->sc_tag,
	    BWFM_PCI_BAR0_WINDOW) != core->co_base)
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    BWFM_PCI_BAR0_WINDOW, core->co_base);
}

uint32_t
bwfm_pci_buscore_read(struct bwfm_softc *bwfm, uint32_t reg)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	uint32_t page, offset;

	page = reg & ~(BWFM_PCI_BAR0_REG_SIZE - 1);
	offset = reg & (BWFM_PCI_BAR0_REG_SIZE - 1);
	pci_conf_write(sc->sc_pc, sc->sc_tag, BWFM_PCI_BAR0_WINDOW, page);
	return bus_space_read_4(sc->sc_reg_iot, sc->sc_reg_ioh, offset);
}

void
bwfm_pci_buscore_write(struct bwfm_softc *bwfm, uint32_t reg, uint32_t val)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	uint32_t page, offset;

	page = reg & ~(BWFM_PCI_BAR0_REG_SIZE - 1);
	offset = reg & (BWFM_PCI_BAR0_REG_SIZE - 1);
	pci_conf_write(sc->sc_pc, sc->sc_tag, BWFM_PCI_BAR0_WINDOW, page);
	bus_space_write_4(sc->sc_reg_iot, sc->sc_reg_ioh, offset, val);
}

int
bwfm_pci_buscore_prepare(struct bwfm_softc *bwfm)
{
	return 0;
}

int
bwfm_pci_buscore_reset(struct bwfm_softc *bwfm)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	struct bwfm_core *core;
	uint32_t reg;
	int i;

	bwfm_pci_select_core(sc, BWFM_AGENT_CORE_PCIE2);
	reg = pci_conf_read(sc->sc_pc, sc->sc_tag,
	    BWFM_PCI_CFGREG_LINK_STATUS_CTRL);
	pci_conf_write(sc->sc_pc, sc->sc_tag, BWFM_PCI_CFGREG_LINK_STATUS_CTRL,
	    reg & ~BWFM_PCI_CFGREG_LINK_STATUS_CTRL_ASPM_ENAB);

	bwfm_pci_select_core(sc, BWFM_AGENT_CORE_CHIPCOMMON);
	bus_space_write_4(sc->sc_reg_iot, sc->sc_reg_ioh,
	    BWFM_CHIP_REG_WATCHDOG, 4);
	delay(100 * 1000);

	bwfm_pci_select_core(sc, BWFM_AGENT_CORE_PCIE2);
	pci_conf_write(sc->sc_pc, sc->sc_tag,
	    BWFM_PCI_CFGREG_LINK_STATUS_CTRL, reg);

	core = bwfm_chip_get_core(bwfm, BWFM_AGENT_CORE_PCIE2);
	if (core->co_rev <= 13) {
		uint16_t cfg_offset[] = {
		    BWFM_PCI_CFGREG_STATUS_CMD,
		    BWFM_PCI_CFGREG_PM_CSR,
		    BWFM_PCI_CFGREG_MSI_CAP,
		    BWFM_PCI_CFGREG_MSI_ADDR_L,
		    BWFM_PCI_CFGREG_MSI_ADDR_H,
		    BWFM_PCI_CFGREG_MSI_DATA,
		    BWFM_PCI_CFGREG_LINK_STATUS_CTRL2,
		    BWFM_PCI_CFGREG_RBAR_CTRL,
		    BWFM_PCI_CFGREG_PML1_SUB_CTRL1,
		    BWFM_PCI_CFGREG_REG_BAR2_CONFIG,
		    BWFM_PCI_CFGREG_REG_BAR3_CONFIG,
		};

		for (i = 0; i < nitems(cfg_offset); i++) {
			bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
			    BWFM_PCI_PCIE2REG_CONFIGADDR, cfg_offset[i]);
			reg = bus_space_read_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
			    BWFM_PCI_PCIE2REG_CONFIGDATA);
			DPRINTFN(3, ("%s: config offset 0x%04x, value 0x%04x\n",
			    DEVNAME(sc), cfg_offset[i], reg));
			bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
			    BWFM_PCI_PCIE2REG_CONFIGDATA, reg);
		}
	}
	if (core->co_rev >= 64)
		sc->sc_pcireg64 = 1;

	reg = bwfm_pci_intr_status(sc);
	if (reg != 0xffffffff)
		bwfm_pci_intr_ack(sc, reg);

	return 0;
}

void
bwfm_pci_buscore_activate(struct bwfm_softc *bwfm, uint32_t rstvec)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh, 0, rstvec);
}

static int bwfm_pci_prio2fifo[8] = {
	0, /* best effort */
	1, /* IPTOS_PREC_IMMEDIATE */
	1, /* IPTOS_PREC_PRIORITY */
	0, /* IPTOS_PREC_FLASH */
	2, /* IPTOS_PREC_FLASHOVERRIDE */
	2, /* IPTOS_PREC_CRITIC_ECP */
	3, /* IPTOS_PREC_INTERNETCONTROL */
	3, /* IPTOS_PREC_NETCONTROL */
};

int
bwfm_pci_flowring_lookup(struct bwfm_pci_softc *sc, struct mbuf *m)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
#ifndef IEEE80211_STA_ONLY
	uint8_t *da = mtod(m, uint8_t *);
#endif
	int flowid, prio, fifo;
	int i, found;

	prio = ieee80211_classify(ic, m);
	fifo = bwfm_pci_prio2fifo[prio];

	switch (ic->ic_opmode)
	{
	case IEEE80211_M_STA:
		flowid = fifo;
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_HOSTAP:
		if (ETHER_IS_MULTICAST(da))
			da = etherbroadcastaddr;
		flowid = da[5] * 2 + fifo;
		break;
#endif
	default:
		printf("%s: state not supported\n", DEVNAME(sc));
		return ENOBUFS;
	}

	found = 0;
	flowid = flowid % sc->sc_max_flowrings;
	for (i = 0; i < sc->sc_max_flowrings; i++) {
		if (ic->ic_opmode == IEEE80211_M_STA &&
		    sc->sc_flowrings[flowid].status >= RING_OPEN &&
		    sc->sc_flowrings[flowid].fifo == fifo) {
			found = 1;
			break;
		}
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
		    sc->sc_flowrings[flowid].status >= RING_OPEN &&
		    sc->sc_flowrings[flowid].fifo == fifo &&
		    !memcmp(sc->sc_flowrings[flowid].mac, da, ETHER_ADDR_LEN)) {
			found = 1;
			break;
		}
#endif
		flowid = (flowid + 1) % sc->sc_max_flowrings;
	}

	if (found)
		return flowid;

	return -1;
}

void
bwfm_pci_flowring_create(struct bwfm_pci_softc *sc, struct mbuf *m)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct bwfm_cmd_flowring_create cmd;
#ifndef IEEE80211_STA_ONLY
	uint8_t *da = mtod(m, uint8_t *);
#endif
	struct bwfm_pci_msgring *ring;
	int flowid, prio, fifo;
	int i, found;

	prio = ieee80211_classify(ic, m);
	fifo = bwfm_pci_prio2fifo[prio];

	switch (ic->ic_opmode)
	{
	case IEEE80211_M_STA:
		flowid = fifo;
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_HOSTAP:
		if (ETHER_IS_MULTICAST(da))
			da = etherbroadcastaddr;
		flowid = da[5] * 2 + fifo;
		break;
#endif
	default:
		printf("%s: state not supported\n", DEVNAME(sc));
		return;
	}

	found = 0;
	flowid = flowid % sc->sc_max_flowrings;
	for (i = 0; i < sc->sc_max_flowrings; i++) {
		ring = &sc->sc_flowrings[flowid];
		if (ring->status == RING_CLOSED) {
			ring->status = RING_OPENING;
			found = 1;
			break;
		}
		flowid = (flowid + 1) % sc->sc_max_flowrings;
	}

	/*
	 * We cannot recover from that so far.  Only a stop/init
	 * cycle can revive this if it ever happens at all.
	 */
	if (!found) {
		printf("%s: no flowring available\n", DEVNAME(sc));
		return;
	}

	cmd.m = m;
	cmd.prio = prio;
	cmd.flowid = flowid;
	bwfm_do_async(&sc->sc_sc, bwfm_pci_flowring_create_cb, &cmd, sizeof(cmd));
}

void
bwfm_pci_flowring_create_cb(struct bwfm_softc *bwfm, void *arg)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
#ifndef IEEE80211_STA_ONLY
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
#endif
	struct bwfm_cmd_flowring_create *cmd = arg;
	struct msgbuf_tx_flowring_create_req *req;
	struct bwfm_pci_msgring *ring;
	uint8_t *da, *sa;
	int s;

	da = mtod(cmd->m, char *) + 0 * ETHER_ADDR_LEN;
	sa = mtod(cmd->m, char *) + 1 * ETHER_ADDR_LEN;

	ring = &sc->sc_flowrings[cmd->flowid];
	if (ring->status != RING_OPENING) {
		printf("%s: flowring not opening\n", DEVNAME(sc));
		return;
	}

	if (bwfm_pci_setup_flowring(sc, ring, 512, 48)) {
		printf("%s: cannot setup flowring\n", DEVNAME(sc));
		return;
	}

	s = splnet();
	req = bwfm_pci_ring_write_reserve(sc, &sc->sc_ctrl_submit);
	if (req == NULL) {
		printf("%s: cannot reserve for flowring\n", DEVNAME(sc));
		splx(s);
		return;
	}

	ring->status = RING_OPENING;
	ring->fifo = bwfm_pci_prio2fifo[cmd->prio];
	ring->m = cmd->m;
	memcpy(ring->mac, da, ETHER_ADDR_LEN);
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP && ETHER_IS_MULTICAST(da))
		memcpy(ring->mac, etherbroadcastaddr, ETHER_ADDR_LEN);
#endif

	req->msg.msgtype = MSGBUF_TYPE_FLOW_RING_CREATE;
	req->msg.ifidx = 0;
	req->msg.request_id = 0;
	req->tid = bwfm_pci_prio2fifo[cmd->prio];
	req->flow_ring_id = letoh16(cmd->flowid + 2);
	memcpy(req->da, da, ETHER_ADDR_LEN);
	memcpy(req->sa, sa, ETHER_ADDR_LEN);
	req->flow_ring_addr.high_addr =
	    letoh32(BWFM_PCI_DMA_DVA(ring->ring) >> 32);
	req->flow_ring_addr.low_addr =
	    letoh32(BWFM_PCI_DMA_DVA(ring->ring) & 0xffffffff);
	req->max_items = letoh16(512);
	req->len_item = letoh16(48);

	bwfm_pci_ring_write_commit(sc, &sc->sc_ctrl_submit);
	splx(s);
}

void
bwfm_pci_flowring_delete(struct bwfm_pci_softc *sc, int flowid)
{
	struct msgbuf_tx_flowring_delete_req *req;
	struct bwfm_pci_msgring *ring;
	int s;

	ring = &sc->sc_flowrings[flowid];
	if (ring->status != RING_OPEN) {
		printf("%s: flowring not open\n", DEVNAME(sc));
		return;
	}

	s = splnet();
	req = bwfm_pci_ring_write_reserve(sc, &sc->sc_ctrl_submit);
	if (req == NULL) {
		printf("%s: cannot reserve for flowring\n", DEVNAME(sc));
		splx(s);
		return;
	}

	ring->status = RING_CLOSING;

	req->msg.msgtype = MSGBUF_TYPE_FLOW_RING_DELETE;
	req->msg.ifidx = 0;
	req->msg.request_id = 0;
	req->flow_ring_id = letoh16(flowid + 2);
	req->reason = 0;

	bwfm_pci_ring_write_commit(sc, &sc->sc_ctrl_submit);
	splx(s);

	tsleep_nsec(ring, PCATCH, DEVNAME(sc), SEC_TO_NSEC(2));
	if (ring->status != RING_CLOSED)
		printf("%s: flowring not closing\n", DEVNAME(sc));
}

void
bwfm_pci_flowring_delete_cb(struct bwfm_softc *bwfm, void *arg)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	struct bwfm_cmd_flowring_create *cmd = arg;
	struct bwfm_pci_msgring *ring;

	ring = &sc->sc_flowrings[cmd->flowid];
	bwfm_pci_dmamem_free(sc, ring->ring);
	ring->status = RING_CLOSED;
	wakeup(ring);
}

void
bwfm_pci_stop(struct bwfm_softc *bwfm)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	struct bwfm_pci_msgring *ring;
	int i;

	for (i = 0; i < sc->sc_max_flowrings; i++) {
		ring = &sc->sc_flowrings[i];
		if (ring->status == RING_OPEN)
			bwfm_pci_flowring_delete(sc, i);
	}
}

int
bwfm_pci_txcheck(struct bwfm_softc *bwfm)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	struct bwfm_pci_msgring *ring;
	int i;

	/* If we are transitioning, we cannot send. */
	for (i = 0; i < sc->sc_max_flowrings; i++) {
		ring = &sc->sc_flowrings[i];
		if (ring->status == RING_OPENING)
			return ENOBUFS;
	}

	if (bwfm_pci_pktid_avail(sc, &sc->sc_tx_pkts)) {
		sc->sc_tx_pkts_full = 1;
		return ENOBUFS;
	}

	return 0;
}

int
bwfm_pci_txdata(struct bwfm_softc *bwfm, struct mbuf *m)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	struct bwfm_pci_msgring *ring;
	struct msgbuf_tx_msghdr *tx;
	uint32_t pktid;
	paddr_t paddr;
	int flowid, ret;

	flowid = bwfm_pci_flowring_lookup(sc, m);
	if (flowid < 0) {
		/*
		 * We cannot send the packet right now as there is
		 * no flowring yet.  The flowring will be created
		 * asynchronously.  While the ring is transitioning
		 * the TX check will tell the upper layers that we
		 * cannot send packets right now.  When the flowring
		 * is created the queue will be restarted and this
		 * mbuf will be transmitted.
		 */
		bwfm_pci_flowring_create(sc, m);
		return 0;
	}

	ring = &sc->sc_flowrings[flowid];
	if (ring->status == RING_OPENING ||
	    ring->status == RING_CLOSING) {
		printf("%s: tried to use a flow that was "
		    "transitioning in status %d\n",
		    DEVNAME(sc), ring->status);
		return ENOBUFS;
	}

	tx = bwfm_pci_ring_write_reserve(sc, ring);
	if (tx == NULL)
		return ENOBUFS;

	memset(tx, 0, sizeof(*tx));
	tx->msg.msgtype = MSGBUF_TYPE_TX_POST;
	tx->msg.ifidx = 0;
	tx->flags = BWFM_MSGBUF_PKT_FLAGS_FRAME_802_3;
	tx->flags |= ieee80211_classify(&sc->sc_sc.sc_ic, m) <<
	    BWFM_MSGBUF_PKT_FLAGS_PRIO_SHIFT;
	tx->seg_cnt = 1;
	memcpy(tx->txhdr, mtod(m, char *), ETHER_HDR_LEN);

	ret = bwfm_pci_pktid_new(sc, &sc->sc_tx_pkts, m, &pktid, &paddr);
	if (ret) {
		if (ret == ENOBUFS) {
			printf("%s: no pktid available for TX\n",
			    DEVNAME(sc));
			sc->sc_tx_pkts_full = 1;
		}
		bwfm_pci_ring_write_cancel(sc, ring, 1);
		return ret;
	}
	paddr += ETHER_HDR_LEN;

	tx->msg.request_id = htole32(pktid + 1);
	tx->data_len = htole16(m->m_len - ETHER_HDR_LEN);
	tx->data_buf_addr.high_addr = htole32((uint64_t)paddr >> 32);
	tx->data_buf_addr.low_addr = htole32(paddr & 0xffffffff);

	bwfm_pci_ring_write_commit(sc, ring);
	return 0;
}

int
bwfm_pci_send_mb_data(struct bwfm_pci_softc *sc, uint32_t htod_mb_data)
{
	struct bwfm_softc *bwfm = (void *)sc;
	struct bwfm_core *core;
	uint32_t reg;
	int i;

	if (sc->sc_mb_via_ctl)
		return bwfm_pci_msgbuf_h2d_mb_write(sc, htod_mb_data);

	for (i = 0; i < 100; i++) {
		reg = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
		    sc->sc_htod_mb_data_addr);
		if (reg == 0)
			break;
		delay(10 * 1000);
	}
	if (i == 100) {
		DPRINTF(("%s: MB transaction already pending\n", DEVNAME(sc)));
		return EIO;
	}

	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_htod_mb_data_addr, htod_mb_data);
	pci_conf_write(sc->sc_pc, sc->sc_tag, BWFM_PCI_REG_SBMBX, 1);

	core = bwfm_chip_get_core(bwfm, BWFM_AGENT_CORE_PCIE2);
	if (core->co_rev <= 13)
		pci_conf_write(sc->sc_pc, sc->sc_tag, BWFM_PCI_REG_SBMBX, 1);

	return 0;
}

void
bwfm_pci_handle_mb_data(struct bwfm_pci_softc *sc)
{
	uint32_t reg;

	reg = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_dtoh_mb_data_addr);
	if (reg == 0)
		return;

	bus_space_write_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_dtoh_mb_data_addr, 0);

	if (reg & BWFM_PCI_D2H_DEV_D3_ACK) {
		sc->sc_mbdata_done = 1;
		wakeup(&sc->sc_mbdata_done);
	}

	/* TODO: support more events */
	if (reg & ~BWFM_PCI_D2H_DEV_D3_ACK)
		printf("%s: handle MB data 0x%08x\n", DEVNAME(sc), reg);
}

#ifdef BWFM_DEBUG
void
bwfm_pci_debug_console(struct bwfm_pci_softc *sc)
{
	uint32_t newidx = bus_space_read_4(sc->sc_tcm_iot, sc->sc_tcm_ioh,
	    sc->sc_console_base_addr + BWFM_CONSOLE_WRITEIDX);

	if (newidx != sc->sc_console_readidx)
		DPRINTFN(3, ("BWFM CONSOLE: "));
	while (newidx != sc->sc_console_readidx) {
		uint8_t ch = bus_space_read_1(sc->sc_tcm_iot, sc->sc_tcm_ioh,
		    sc->sc_console_buf_addr + sc->sc_console_readidx);
		sc->sc_console_readidx++;
		if (sc->sc_console_readidx == sc->sc_console_buf_size)
			sc->sc_console_readidx = 0;
		if (ch == '\r')
			continue;
		DPRINTFN(3, ("%c", ch));
	}
}
#endif

int
bwfm_pci_intr(void *v)
{
	struct bwfm_pci_softc *sc = (void *)v;
	struct ifnet *ifp = &sc->sc_sc.sc_ic.ic_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	uint32_t status, mask;

	if (!sc->sc_initialized)
		return 0;

	status = bwfm_pci_intr_status(sc);
	/* FIXME: interrupt status seems to be zero? */
	if (status == 0 && sc->sc_pcireg64)
		status |= BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H_DB;
	if (status == 0)
		return 0;

	bwfm_pci_intr_disable(sc);
	bwfm_pci_intr_ack(sc, status);

	if (!sc->sc_pcireg64 &&
	    (status & (BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_FN0_0 |
	    BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_FN0_1)))
		bwfm_pci_handle_mb_data(sc);

	mask = BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H_DB;
	if (sc->sc_pcireg64)
		mask = BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H_DB;

	if (status & mask) {
		bwfm_pci_ring_rx(sc, &sc->sc_ctrl_complete, &ml);
		bwfm_pci_ring_rx(sc, &sc->sc_rx_complete, &ml);
		bwfm_pci_ring_rx(sc, &sc->sc_tx_complete, &ml);

		if (ifiq_input(&ifp->if_rcv, &ml))
			if_rxr_livelocked(&sc->sc_rxbuf_ring);
	}

#ifdef BWFM_DEBUG
	bwfm_pci_debug_console(sc);
#endif

	bwfm_pci_intr_enable(sc);
	return 1;
}

void
bwfm_pci_intr_enable(struct bwfm_pci_softc *sc)
{
	if (sc->sc_pcireg64)
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_64_PCIE2REG_MAILBOXMASK,
		    BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H_DB);
	else
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_PCIE2REG_MAILBOXMASK,
		    BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_FN0_0 |
		    BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_FN0_1 |
		    BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H_DB);
}

void
bwfm_pci_intr_disable(struct bwfm_pci_softc *sc)
{
	if (sc->sc_pcireg64)
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_64_PCIE2REG_MAILBOXMASK, 0);
	else
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_PCIE2REG_MAILBOXMASK, 0);
}

uint32_t
bwfm_pci_intr_status(struct bwfm_pci_softc *sc)
{
	if (sc->sc_pcireg64)
		return bus_space_read_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_64_PCIE2REG_MAILBOXINT);
	else
		return bus_space_read_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_PCIE2REG_MAILBOXINT);
}

void
bwfm_pci_intr_ack(struct bwfm_pci_softc *sc, uint32_t status)
{
	if (sc->sc_pcireg64)
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_64_PCIE2REG_MAILBOXINT, status);
	else
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_PCIE2REG_MAILBOXINT, status);
}

uint32_t
bwfm_pci_intmask(struct bwfm_pci_softc *sc)
{
	if (sc->sc_pcireg64)
		return bus_space_read_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_64_PCIE2REG_INTMASK);
	else
		return bus_space_read_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_PCIE2REG_INTMASK);
}

void
bwfm_pci_hostready(struct bwfm_pci_softc *sc)
{
	if ((sc->sc_shared_flags & BWFM_SHARED_INFO_HOSTRDY_DB1) == 0)
		return;

	if (sc->sc_shared_flags & BWFM_SHARED_INFO_SHARED_DAR)
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_64_PCIE2REG_H2D_MAILBOX_1, 1);
	else
		bus_space_write_4(sc->sc_pcie_iot, sc->sc_pcie_ioh,
		    BWFM_PCI_PCIE2REG_H2D_MAILBOX_1, 1);
}

/* Msgbuf protocol implementation */
int
bwfm_pci_msgbuf_query_dcmd(struct bwfm_softc *bwfm, int ifidx,
    int cmd, char *buf, size_t *len)
{
	struct bwfm_pci_softc *sc = (void *)bwfm;
	struct msgbuf_ioctl_req_hdr *req;
	struct bwfm_pci_ioctl *ctl;
	struct mbuf *m;
	uint32_t pktid;
	paddr_t paddr;
	size_t buflen;
	int s;

	buflen = min(*len, BWFM_DMA_H2D_IOCTL_BUF_LEN);
	m = MCLGETL(NULL, M_DONTWAIT, buflen);
	if (m == NULL)
		return 1;
	m->m_len = m->m_pkthdr.len = buflen;

	if (buf)
		memcpy(mtod(m, char *), buf, buflen);
	else
		memset(mtod(m, char *), 0, buflen);

	s = splnet();
	req = bwfm_pci_ring_write_reserve(sc, &sc->sc_ctrl_submit);
	if (req == NULL) {
		splx(s);
		m_freem(m);
		return 1;
	}

	if (bwfm_pci_pktid_new(sc, &sc->sc_ioctl_pkts, m, &pktid, &paddr)) {
		bwfm_pci_ring_write_cancel(sc, &sc->sc_ctrl_submit, 1);
		splx(s);
		m_freem(m);
		return 1;
	}

	ctl = malloc(sizeof(*ctl), M_TEMP, M_WAITOK|M_ZERO);
	ctl->transid = sc->sc_ioctl_transid++;
	TAILQ_INSERT_TAIL(&sc->sc_ioctlq, ctl, next);

	req->msg.msgtype = MSGBUF_TYPE_IOCTLPTR_REQ;
	req->msg.ifidx = 0;
	req->msg.flags = 0;
	req->msg.request_id = htole32(pktid);
	req->cmd = htole32(cmd);
	req->output_buf_len = htole16(*len);
	req->trans_id = htole16(ctl->transid);

	req->input_buf_len = htole16(m->m_len);
	req->req_buf_addr.high_addr = htole32((uint64_t)paddr >> 32);
	req->req_buf_addr.low_addr = htole32(paddr & 0xffffffff);

	bwfm_pci_ring_write_commit(sc, &sc->sc_ctrl_submit);
	splx(s);

	tsleep_nsec(ctl, PWAIT, "bwfm", SEC_TO_NSEC(5));
	TAILQ_REMOVE(&sc->sc_ioctlq, ctl, next);

	if (ctl->m == NULL) {
		free(ctl, M_TEMP, sizeof(*ctl));
		return 1;
	}

	*len = min(ctl->retlen, m->m_len);
	*len = min(*len, buflen);
	if (buf)
		m_copydata(ctl->m, 0, *len, buf);
	m_freem(ctl->m);

	if (ctl->status < 0) {
		free(ctl, M_TEMP, sizeof(*ctl));
		return 1;
	}

	free(ctl, M_TEMP, sizeof(*ctl));
	return 0;
}

int
bwfm_pci_msgbuf_set_dcmd(struct bwfm_softc *bwfm, int ifidx,
    int cmd, char *buf, size_t len)
{
	return bwfm_pci_msgbuf_query_dcmd(bwfm, ifidx, cmd, buf, &len);
}

void
bwfm_pci_msgbuf_rxioctl(struct bwfm_pci_softc *sc,
    struct msgbuf_ioctl_resp_hdr *resp)
{
	struct bwfm_pci_ioctl *ctl, *tmp;
	struct mbuf *m;

	m = bwfm_pci_pktid_free(sc, &sc->sc_rx_pkts,
	    letoh32(resp->msg.request_id));

	TAILQ_FOREACH_SAFE(ctl, &sc->sc_ioctlq, next, tmp) {
		if (ctl->transid != letoh16(resp->trans_id))
			continue;
		ctl->m = m;
		ctl->retlen = letoh16(resp->resp_len);
		ctl->status = letoh16(resp->compl_hdr.status);
		wakeup(ctl);
		return;
	}

	m_freem(m);
}

int
bwfm_pci_msgbuf_h2d_mb_write(struct bwfm_pci_softc *sc, uint32_t data)
{
	struct msgbuf_h2d_mailbox_data *req;
	int s;

	s = splnet();
	req = bwfm_pci_ring_write_reserve(sc, &sc->sc_ctrl_submit);
	if (req == NULL) {
		splx(s);
		return ENOBUFS;
	}

	req->msg.msgtype = MSGBUF_TYPE_H2D_MAILBOX_DATA;
	req->msg.ifidx = -1;
	req->msg.flags = 0;
	req->msg.request_id = 0;
	req->data = data;

	bwfm_pci_ring_write_commit(sc, &sc->sc_ctrl_submit);
	splx(s);

	return 0;
}
