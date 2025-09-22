/*	$OpenBSD: malo.c,v 1.125 2023/11/10 15:51:20 bluhm Exp $ */

/*
 * Copyright (c) 2006 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2006 Marcus Glocker <mglocker@openbsd.org>
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

#include "bpfilter.h"

#include <sys/param.h>

#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/malo.h>

#ifdef MALO_DEBUG
int malo_d = 1;
#define DPRINTF(l, x...)	do { if ((l) <= malo_d) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

/* internal structures and defines */
struct malo_node {
	struct ieee80211_node		ni;
};

struct malo_rx_data {
	bus_dmamap_t	map;
	struct mbuf	*m;
};

struct malo_tx_data {
	bus_dmamap_t		map;
	struct mbuf		*m;
	uint32_t		softstat;
	struct ieee80211_node	*ni;
};

/* RX descriptor used by HW */
struct malo_rx_desc {
	uint8_t		rxctrl;
	uint8_t		rssi;
	uint8_t		status;
	uint8_t		channel;
	uint16_t	len;
	uint8_t		reserved1;	/* actually unused */
	uint8_t		datarate;
	uint32_t	physdata;	/* DMA address of data */
	uint32_t	physnext;	/* DMA address of next control block */
	uint16_t	qosctrl;
	uint16_t	reserved2;
} __packed;

/* TX descriptor used by HW */
struct malo_tx_desc {
	uint32_t	status;
	uint8_t		datarate;
	uint8_t		txpriority;
	uint16_t	qosctrl;
	uint32_t	physdata;	/* DMA address of data */
	uint16_t	len;
	uint8_t		destaddr[6];
	uint32_t	physnext;	/* DMA address of next control block */
	uint32_t	reserved1;	/* SAP packet info ??? */
	uint32_t	reserved2;
} __packed;

#define MALO_RX_RING_COUNT	256
#define MALO_TX_RING_COUNT	256
#define MALO_MAX_SCATTER	8	/* XXX unknown, wild guess */
#define MALO_CMD_TIMEOUT	50	/* MALO_CMD_TIMEOUT * 100us */

/*
 * Firmware commands
 */
#define MALO_CMD_GET_HW_SPEC		0x0003
#define MALO_CMD_SET_RADIO		0x001c
#define MALO_CMD_SET_AID		0x010d
#define MALO_CMD_SET_TXPOWER		0x001e
#define MALO_CMD_SET_ANTENNA		0x0020
#define MALO_CMD_SET_PRESCAN		0x0107
#define MALO_CMD_SET_POSTSCAN		0x0108
#define MALO_CMD_SET_RATE		0x0110
#define MALO_CMD_SET_CHANNEL		0x010a
#define MALO_CMD_SET_RTS		0x0113
#define MALO_CMD_SET_SLOT		0x0114
#define MALO_CMD_RESPONSE		0x8000

#define MALO_CMD_RESULT_OK		0x0000	/* everything is fine */
#define MALO_CMD_RESULT_ERROR		0x0001	/* general error */
#define MALO_CMD_RESULT_NOSUPPORT	0x0002	/* command not valid */
#define MALO_CMD_RESULT_PENDING		0x0003	/* will be processed */
#define MALO_CMD_RESULT_BUSY		0x0004	/* command ignored */
#define MALO_CMD_RESULT_PARTIALDATA	0x0005	/* buffer too small */

struct malo_cmdheader {
	uint16_t	cmd;
	uint16_t	size;		/* size of the command, incl. header */
	uint16_t	seqnum;		/* seems not to matter that much */
	uint16_t	result;		/* set to 0 on request */
	/* following the data payload, up to 256 bytes */
};

struct malo_hw_spec {
	uint16_t	HwVersion;
	uint16_t	NumOfWCB;
	uint16_t	NumOfMCastAdr;
	uint8_t		PermanentAddress[6];
	uint16_t	RegionCode;
	uint16_t	NumberOfAntenna;
	uint32_t	FWReleaseNumber;
	uint32_t	WcbBase0;
	uint32_t	RxPdWrPtr;
	uint32_t	RxPdRdPtr;
	uint32_t	CookiePtr;
	uint32_t	WcbBase1;
	uint32_t	WcbBase2;
	uint32_t	WcbBase3;
} __packed;

struct malo_cmd_radio {
	uint16_t	action;
	uint16_t	preamble_mode;
	uint16_t	enable;
} __packed;

struct malo_cmd_aid {
	uint16_t	associd;
	uint8_t		macaddr[6];
	uint32_t	gprotection;
	uint8_t		aprates[14];
} __packed;

struct malo_cmd_txpower {
	uint16_t	action;
	uint16_t	supportpowerlvl;
	uint16_t	currentpowerlvl;
	uint16_t	reserved;
	uint16_t	powerlvllist[8];
} __packed;

struct malo_cmd_antenna {
	uint16_t	action;
	uint16_t	mode;
} __packed;

struct malo_cmd_postscan {
	uint32_t	isibss;
	uint8_t		bssid[6];
} __packed;

struct malo_cmd_channel {
	uint16_t	action;
	uint8_t		channel;
} __packed;

struct malo_cmd_rate {
	uint8_t		dataratetype;
	uint8_t		rateindex;
	uint8_t		aprates[14];
} __packed;

struct malo_cmd_rts {
	uint16_t	action;
	uint32_t	threshold;
} __packed;

struct malo_cmd_slot {
	uint16_t	action;
	uint8_t		slot;
} __packed;

#define malo_mem_write4(sc, off, x) \
	bus_space_write_4((sc)->sc_mem1_bt, (sc)->sc_mem1_bh, (off), (x))
#define malo_mem_write2(sc, off, x) \
	bus_space_write_2((sc)->sc_mem1_bt, (sc)->sc_mem1_bh, (off), (x))
#define malo_mem_write1(sc, off, x) \
	bus_space_write_1((sc)->sc_mem1_bt, (sc)->sc_mem1_bh, (off), (x))

#define malo_mem_read4(sc, off) \
	bus_space_read_4((sc)->sc_mem1_bt, (sc)->sc_mem1_bh, (off))
#define malo_mem_read1(sc, off) \
	bus_space_read_1((sc)->sc_mem1_bt, (sc)->sc_mem1_bh, (off))

#define malo_ctl_write4(sc, off, x) \
	bus_space_write_4((sc)->sc_mem2_bt, (sc)->sc_mem2_bh, (off), (x))
#define malo_ctl_read4(sc, off) \
	bus_space_read_4((sc)->sc_mem2_bt, (sc)->sc_mem2_bh, (off))
#define malo_ctl_read1(sc, off) \
	bus_space_read_1((sc)->sc_mem2_bt, (sc)->sc_mem2_bh, (off))

#define malo_ctl_barrier(sc, t) \
	bus_space_barrier((sc)->sc_mem2_bt, (sc)->sc_mem2_bh, 0x0c00, 0xff, (t))

struct cfdriver malo_cd = {
	NULL, "malo", DV_IFNET
};

int	malo_alloc_cmd(struct malo_softc *sc);
void	malo_free_cmd(struct malo_softc *sc);
void	malo_send_cmd(struct malo_softc *sc, bus_addr_t addr);
int	malo_send_cmd_dma(struct malo_softc *sc, bus_addr_t addr);
int	malo_alloc_rx_ring(struct malo_softc *sc, struct malo_rx_ring *ring,
	    int count);
void	malo_reset_rx_ring(struct malo_softc *sc, struct malo_rx_ring *ring);
void	malo_free_rx_ring(struct malo_softc *sc, struct malo_rx_ring *ring);
int	malo_alloc_tx_ring(struct malo_softc *sc, struct malo_tx_ring *ring,
	    int count);
void	malo_reset_tx_ring(struct malo_softc *sc, struct malo_tx_ring *ring);
void	malo_free_tx_ring(struct malo_softc *sc, struct malo_tx_ring *ring);
int	malo_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
void	malo_start(struct ifnet *ifp);
void	malo_watchdog(struct ifnet *ifp);
int	malo_newstate(struct ieee80211com *ic, enum ieee80211_state nstate,
	    int arg);
void	malo_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni,
	    int isnew);
struct ieee80211_node *
	malo_node_alloc(struct ieee80211com *ic);
int	malo_media_change(struct ifnet *ifp);
void	malo_media_status(struct ifnet *ifp, struct ifmediareq *imr);
int	malo_chip2rate(int chip_rate);
int	malo_fix2rate(int fix_rate);
void	malo_next_scan(void *arg);
void	malo_tx_intr(struct malo_softc *sc);
int	malo_tx_mgt(struct malo_softc *sc, struct mbuf *m0,
	    struct ieee80211_node *ni);
int	malo_tx_data(struct malo_softc *sc, struct mbuf *m0,
	    struct ieee80211_node *ni);
void	malo_tx_setup_desc(struct malo_softc *sc, struct malo_tx_desc *desc,
	    int len, int rate, const bus_dma_segment_t *segs, int nsegs);
void	malo_rx_intr(struct malo_softc *sc);
int	malo_load_bootimg(struct malo_softc *sc);
int	malo_load_firmware(struct malo_softc *sc);

int	malo_set_slot(struct malo_softc *sc);
void	malo_update_slot(struct ieee80211com *ic);
#ifdef MALO_DEBUG
void	malo_hexdump(void *buf, int len);
#endif
static char *
	malo_cmd_string(uint16_t cmd);
static char *
	malo_cmd_string_result(uint16_t result);
int	malo_cmd_get_spec(struct malo_softc *sc);
int	malo_cmd_set_prescan(struct malo_softc *sc);
int	malo_cmd_set_postscan(struct malo_softc *sc, uint8_t *macaddr,
	    uint8_t ibsson);
int	malo_cmd_set_channel(struct malo_softc *sc, uint8_t channel);
int	malo_cmd_set_antenna(struct malo_softc *sc, uint16_t antenna_type);
int	malo_cmd_set_radio(struct malo_softc *sc, uint16_t mode,
	    uint16_t preamble);
int	malo_cmd_set_aid(struct malo_softc *sc, uint8_t *bssid,
	    uint16_t associd);
int	malo_cmd_set_txpower(struct malo_softc *sc, unsigned int powerlevel);
int	malo_cmd_set_rts(struct malo_softc *sc, uint32_t threshold);
int	malo_cmd_set_slot(struct malo_softc *sc, uint8_t slot);
int	malo_cmd_set_rate(struct malo_softc *sc, uint8_t rate);
void	malo_cmd_response(struct malo_softc *sc);

int
malo_intr(void *arg)
{
	struct malo_softc *sc = arg;
	uint32_t status;

	status = malo_ctl_read4(sc, 0x0c30);
	if (status == 0xffffffff || status == 0)
		/* not for us */
		return (0);

	if (status & 0x1)
		malo_tx_intr(sc);
	if (status & 0x2)
		malo_rx_intr(sc);
	if (status & 0x4) {
		/* XXX cmd done interrupt handling doesn't work yet */
		DPRINTF(1, "%s: got cmd done interrupt\n", sc->sc_dev.dv_xname);
		//malo_cmd_response(sc);
	}

	if (status & ~0x7)
		DPRINTF(1, "%s: unknown interrupt %x\n",
		    sc->sc_dev.dv_xname, status);

	/* just ack the interrupt */
	malo_ctl_write4(sc, 0x0c30, 0);

	return (1);
}

int
malo_attach(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int i;

	/* initialize channel scanning timer */
	timeout_set(&sc->sc_scan_to, malo_next_scan, sc);

	/* allocate DMA structures */
	malo_alloc_cmd(sc);
	malo_alloc_rx_ring(sc, &sc->sc_rxring, MALO_RX_RING_COUNT);
	malo_alloc_tx_ring(sc, &sc->sc_txring, MALO_TX_RING_COUNT);

	/* setup interface */
	ifp->if_softc = sc;
	ifp->if_ioctl = malo_ioctl;
	ifp->if_start = malo_start;
	ifp->if_watchdog = malo_watchdog;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifq_init_maxlen(&ifp->if_snd, IFQ_MAXLEN);

	/* set supported rates */
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
	sc->sc_last_txrate = -1;

	/* set channels */
	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_PUREG |
		    IEEE80211_CHAN_B |
		    IEEE80211_CHAN_G;
	}

	/* set the rest */
	ic->ic_caps =
	    IEEE80211_C_IBSS |
	    IEEE80211_C_MONITOR |
	    IEEE80211_C_SHPREAMBLE |
	    IEEE80211_C_SHSLOT |
	    IEEE80211_C_WEP |
	    IEEE80211_C_RSN;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_max_rssi = 75;
	for (i = 0; i < 6; i++)
		ic->ic_myaddr[i] = malo_ctl_read1(sc, 0xa528 + i);

	/* show our mac address */
	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));

	/* attach interface */
	if_attach(ifp);
	ieee80211_ifattach(ifp);

	/* post attach vector functions */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = malo_newstate;
	ic->ic_newassoc = malo_newassoc;
	ic->ic_node_alloc = malo_node_alloc;
	ic->ic_updateslot = malo_update_slot;

	ieee80211_media_init(ifp, malo_media_change, malo_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + 64);

	sc->sc_rxtap_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(MALO_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(MALO_TX_RADIOTAP_PRESENT);
#endif

	return (0);
}

int
malo_detach(void *arg)
{
	struct malo_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	/* remove channel scanning timer */
	timeout_del(&sc->sc_scan_to);

	malo_stop(sc);
	ieee80211_ifdetach(ifp);
	if_detach(ifp);
	malo_free_cmd(sc);
	malo_free_rx_ring(sc, &sc->sc_rxring);
	malo_free_tx_ring(sc, &sc->sc_txring);

	return (0);
}

int
malo_alloc_cmd(struct malo_softc *sc)
{
	int error, nsegs;

	error = bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1,
	    PAGE_SIZE, 0, BUS_DMA_ALLOCNOW, &sc->sc_cmd_dmam);
	if (error != 0) {
		printf("%s: can not create DMA tag\n", sc->sc_dev.dv_xname);
		return (-1);
	}

	error = bus_dmamem_alloc(sc->sc_dmat, PAGE_SIZE, PAGE_SIZE,
	    0, &sc->sc_cmd_dmas, 1, &nsegs, BUS_DMA_WAITOK);
	if (error != 0) {
		printf("%s: error alloc dma memory\n", sc->sc_dev.dv_xname);
		return (-1);
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->sc_cmd_dmas, nsegs,
	    PAGE_SIZE, (caddr_t *)&sc->sc_cmd_mem, BUS_DMA_WAITOK);
	if (error != 0) {
		printf("%s: error map dma memory\n", sc->sc_dev.dv_xname);
		return (-1);
	}

	error = bus_dmamap_load(sc->sc_dmat, sc->sc_cmd_dmam,
	    sc->sc_cmd_mem, PAGE_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: error load dma memory\n", sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat, &sc->sc_cmd_dmas, nsegs);
		return (-1);
	}

	sc->sc_cookie = sc->sc_cmd_mem;
	*sc->sc_cookie = htole32(0xaa55aa55);
	sc->sc_cmd_mem = (caddr_t)sc->sc_cmd_mem + sizeof(uint32_t);
	sc->sc_cookie_dmaaddr = sc->sc_cmd_dmam->dm_segs[0].ds_addr;
	sc->sc_cmd_dmaaddr = sc->sc_cmd_dmam->dm_segs[0].ds_addr +
	    sizeof(uint32_t);

	return (0);
}

void
malo_free_cmd(struct malo_softc *sc)
{
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, sc->sc_cmd_dmam);
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_cookie, PAGE_SIZE);
	bus_dmamem_free(sc->sc_dmat, &sc->sc_cmd_dmas, 1);
}

void
malo_send_cmd(struct malo_softc *sc, bus_addr_t addr)
{
	malo_ctl_write4(sc, 0x0c10, (uint32_t)addr);
	malo_ctl_barrier(sc, BUS_SPACE_BARRIER_WRITE);
	malo_ctl_write4(sc, 0x0c18, 2); /* CPU_TRANSFER_CMD */
	malo_ctl_barrier(sc, BUS_SPACE_BARRIER_WRITE);
}

int
malo_send_cmd_dma(struct malo_softc *sc, bus_addr_t addr)
{
	int i;
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;

	malo_ctl_write4(sc, 0x0c10, (uint32_t)addr);
	malo_ctl_barrier(sc, BUS_SPACE_BARRIER_WRITE);
	malo_ctl_write4(sc, 0x0c18, 2); /* CPU_TRANSFER_CMD */
	malo_ctl_barrier(sc, BUS_SPACE_BARRIER_WRITE);

	for (i = 0; i < MALO_CMD_TIMEOUT; i++) {
		delay(100);
		bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
		if (hdr->cmd & htole16(0x8000))
			break;
	}
	if (i == MALO_CMD_TIMEOUT) {
		printf("%s: timeout while waiting for cmd response!\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	malo_cmd_response(sc);

	return (0);
}

int
malo_alloc_rx_ring(struct malo_softc *sc, struct malo_rx_ring *ring, int count)
{
	struct malo_rx_desc *desc;
	struct malo_rx_data *data;
	int i, nsegs, error;

	ring->count = count;
	ring->cur = ring->next = 0;

	error = bus_dmamap_create(sc->sc_dmat,
	    count * sizeof(struct malo_rx_desc), 1,
	    count * sizeof(struct malo_rx_desc), 0,
	    BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat,
	    count * sizeof(struct malo_rx_desc),
	    PAGE_SIZE, 0, &ring->seg, 1, &nsegs,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    count * sizeof(struct malo_rx_desc), (caddr_t *)&ring->desc,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, ring->desc,
	    count * sizeof(struct malo_rx_desc), NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ring->physaddr = ring->map->dm_segs->ds_addr;

	ring->data = mallocarray(count, sizeof (struct malo_rx_data),
	    M_DEVBUF, M_NOWAIT);
	if (ring->data == NULL) {
		printf("%s: could not allocate soft data\n",
		    sc->sc_dev.dv_xname);
		error = ENOMEM;
		goto fail;
	}

	/*
	 * Pre-allocate Rx buffers and populate Rx ring.
	 */
	bzero(ring->data, count * sizeof (struct malo_rx_data));
	for (i = 0; i < count; i++) {
		desc = &ring->desc[i];
		data = &ring->data[i];

		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &data->map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		MGETHDR(data->m, M_DONTWAIT, MT_DATA);
		if (data->m == NULL) {
			printf("%s: could not allocate rx mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		MCLGET(data->m, M_DONTWAIT);
		if (!(data->m->m_flags & M_EXT)) {
			printf("%s: could not allocate rx mbuf cluster\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(data->m, void *), MCLBYTES, NULL, BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: could not load rx buf DMA map",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		desc->status = 1;
		desc->physdata = htole32(data->map->dm_segs->ds_addr);
		desc->physnext = htole32(ring->physaddr +
		    (i + 1) % count * sizeof(struct malo_rx_desc));
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	return (0);

fail:	malo_free_rx_ring(sc, ring);
	return (error);
}

void
malo_reset_rx_ring(struct malo_softc *sc, struct malo_rx_ring *ring)
{
	int i;

	for (i = 0; i < ring->count; i++)
		ring->desc[i].status = 0;

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->cur = ring->next = 0;
}

void
malo_free_rx_ring(struct malo_softc *sc, struct malo_rx_ring *ring)
{
	struct malo_rx_data *data;
	int i;

	if (ring->desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)ring->desc,
		    ring->count * sizeof(struct malo_rx_desc));
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	if (ring->data != NULL) {
		for (i = 0; i < ring->count; i++) {
			data = &ring->data[i];

			if (data->m != NULL) {
				bus_dmamap_sync(sc->sc_dmat, data->map, 0,
				    data->map->dm_mapsize,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->sc_dmat, data->map);
				m_freem(data->m);
			}

			if (data->map != NULL)
				bus_dmamap_destroy(sc->sc_dmat, data->map);
		}
		free(ring->data, M_DEVBUF, 0);
	}
}

int
malo_alloc_tx_ring(struct malo_softc *sc, struct malo_tx_ring *ring,
    int count)
{
	int i, nsegs, error;

	ring->count = count;
	ring->queued = 0;
	ring->cur = ring->next = ring->stat = 0;

	error = bus_dmamap_create(sc->sc_dmat,
	    count * sizeof(struct malo_tx_desc), 1,
	    count * sizeof(struct malo_tx_desc), 0, BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat,
	    count * sizeof(struct malo_tx_desc), PAGE_SIZE, 0,
	    &ring->seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    count * sizeof(struct malo_tx_desc), (caddr_t *)&ring->desc,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, ring->desc,
	    count * sizeof(struct malo_tx_desc), NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ring->physaddr = ring->map->dm_segs->ds_addr;

	ring->data = mallocarray(count, sizeof(struct malo_tx_data),
	    M_DEVBUF, M_NOWAIT);
	if (ring->data == NULL) {
		printf("%s: could not allocate soft data\n",
		    sc->sc_dev.dv_xname);
		error = ENOMEM;
		goto fail;
	}

	memset(ring->data, 0, count * sizeof(struct malo_tx_data));
	for (i = 0; i < count; i++) {
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    MALO_MAX_SCATTER, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &ring->data[i].map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
		ring->desc[i].physnext = htole32(ring->physaddr +
		    (i + 1) % count * sizeof(struct malo_tx_desc));
	}

	return (0);

fail:	malo_free_tx_ring(sc, ring);
	return (error);
}

void
malo_reset_tx_ring(struct malo_softc *sc, struct malo_tx_ring *ring)
{
	struct malo_tx_desc *desc;
	struct malo_tx_data *data;
	int i;

	for (i = 0; i < ring->count; i++) {
		desc = &ring->desc[i];
		data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}

		/*
		 * The node has already been freed at that point so don't call
		 * ieee80211_release_node() here.
		 */
		data->ni = NULL;

		desc->status = 0;
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->queued = 0;
	ring->cur = ring->next = ring->stat = 0;
}

void
malo_free_tx_ring(struct malo_softc *sc, struct malo_tx_ring *ring)
{
	struct malo_tx_data *data;
	int i;

	if (ring->desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)ring->desc,
		    ring->count * sizeof(struct malo_tx_desc));
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	if (ring->data != NULL) {
		for (i = 0; i < ring->count; i++) {
			data = &ring->data[i];

			if (data->m != NULL) {
				bus_dmamap_sync(sc->sc_dmat, data->map, 0,
				    data->map->dm_mapsize,
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->sc_dmat, data->map);
				m_freem(data->m);
			}

			/*
			 * The node has already been freed at that point so
			 * don't call ieee80211_release_node() here.
			 */
			data->ni = NULL;

			if (data->map != NULL)
				bus_dmamap_destroy(sc->sc_dmat, data->map);
		}
		free(ring->data, M_DEVBUF, 0);
	}
}

int
malo_init(struct ifnet *ifp)
{
	struct malo_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t chan;
	int error;

	DPRINTF(1, "%s: %s\n", ifp->if_xname, __func__);

	/* if interface already runs stop it first */
	if (ifp->if_flags & IFF_RUNNING)
		malo_stop(sc);

	/* power on cardbus socket */
	if (sc->sc_enable)
		sc->sc_enable(sc);

	/* disable interrupts */
	malo_ctl_read4(sc, 0x0c30);
	malo_ctl_write4(sc, 0x0c30, 0);
	malo_ctl_write4(sc, 0x0c34, 0);
	malo_ctl_write4(sc, 0x0c3c, 0);

	/* load firmware */
	if ((error = malo_load_bootimg(sc)))
		goto fail;
	if ((error = malo_load_firmware(sc)))
		goto fail;

	/* enable interrupts */
	malo_ctl_write4(sc, 0x0c34, 0x1f);
	malo_ctl_barrier(sc, BUS_SPACE_BARRIER_WRITE);
	malo_ctl_write4(sc, 0x0c3c, 0x1f);
	malo_ctl_barrier(sc, BUS_SPACE_BARRIER_WRITE);

	if ((error = malo_cmd_get_spec(sc)))
		goto fail;

	/* select default channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);

	/* initialize hardware */
	if ((error = malo_cmd_set_channel(sc, chan))) {
		printf("%s: setting channel failed!\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if ((error = malo_cmd_set_antenna(sc, 1))) {
		printf("%s: setting RX antenna failed!\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if ((error = malo_cmd_set_antenna(sc, 2))) {
		printf("%s: setting TX antenna failed!\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if ((error = malo_cmd_set_radio(sc, 1, 5))) {
		printf("%s: turn radio on failed!\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if ((error = malo_cmd_set_txpower(sc, 100))) {
		printf("%s: setting TX power failed!\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if ((error = malo_cmd_set_rts(sc, IEEE80211_RTS_MAX))) {
		printf("%s: setting RTS failed!\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		/* start background scanning */
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	else
		/* in monitor mode change directly into run state */
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);

	return (0);

fail:
	/* reset adapter */
	DPRINTF(1, "%s: malo_init failed, resetting card\n",
	    sc->sc_dev.dv_xname);
	malo_stop(sc);
	return (error);
}

int
malo_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct malo_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s, error = 0;
	uint8_t chan;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				malo_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				malo_stop(sc);
		}
		break;
	case SIOCS80211CHANNEL:
		/* allow fast channel switching in monitor mode */
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error == ENETRESET &&
		    ic->ic_opmode == IEEE80211_M_MONITOR) {
			if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
			    (IFF_UP | IFF_RUNNING)) {
				ic->ic_bss->ni_chan = ic->ic_ibss_chan;
				chan = ieee80211_chan2ieee(ic,
				    ic->ic_bss->ni_chan);
				malo_cmd_set_channel(sc, chan);
			}
			error = 0;
		}
		break;
	default:
		error = ieee80211_ioctl(ifp, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			malo_init(ifp);
		error = 0;
	}

	splx(s);

	return (error);
}

void
malo_start(struct ifnet *ifp)
{
	struct malo_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *m0;
	struct ieee80211_node *ni;

	DPRINTF(2, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->sc_txring.queued >= MALO_TX_RING_COUNT - 1) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m0 = mq_dequeue(&ic->ic_mgtq);
		if (m0 != NULL) {
			ni = m0->m_pkthdr.ph_cookie;
#if NBPFILTER > 0
			if (ic->ic_rawbpf != NULL)
				bpf_mtap(ic->ic_rawbpf, m0, BPF_DIRECTION_OUT);
#endif
			if (malo_tx_mgt(sc, m0, ni) != 0)
				break;
		} else {
			if (ic->ic_state != IEEE80211_S_RUN)
				break;

			m0 = ifq_dequeue(&ifp->if_snd);
			if (m0 == NULL)
				break;
#if NBPFILTER > 0
			if (ifp->if_bpf != NULL)
				bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif
			m0 = ieee80211_encap(ifp, m0, &ni);
			if (m0 == NULL)
				continue;
#if NBPFILTER > 0
			if (ic->ic_rawbpf != NULL)
				bpf_mtap(ic->ic_rawbpf, m0, BPF_DIRECTION_OUT);
#endif
			if (malo_tx_data(sc, m0, ni) != 0) {
				if (ni != NULL)
					ieee80211_release_node(ic, ni);
				ifp->if_oerrors++;
				break;
			}
		}
	}
}

void
malo_stop(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	DPRINTF(1, "%s: %s\n", ifp->if_xname, __func__);

	/* reset adapter */
	if (ifp->if_flags & IFF_RUNNING)
		malo_ctl_write4(sc, 0x0c18, (1 << 15));

	/* device is not running anymore */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* change back to initial state */
	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

	/* reset RX / TX rings */
	malo_reset_tx_ring(sc, &sc->sc_txring);
	malo_reset_rx_ring(sc, &sc->sc_rxring);

	/* set initial rate */
	sc->sc_last_txrate = -1;

	/* power off cardbus socket */
	if (sc->sc_disable)
		sc->sc_disable(sc);
}

void
malo_watchdog(struct ifnet *ifp)
{

}

int
malo_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct malo_softc *sc = ic->ic_if.if_softc;
	enum ieee80211_state ostate;
	uint8_t chan;
	int rate;

	DPRINTF(2, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	ostate = ic->ic_state;
	timeout_del(&sc->sc_scan_to);

	switch (nstate) {
	case IEEE80211_S_INIT:
		break;
	case IEEE80211_S_SCAN:
		if (ostate == IEEE80211_S_INIT) {
			if (malo_cmd_set_prescan(sc) != 0)
				DPRINTF(1, "%s: can't set prescan\n",
				    sc->sc_dev.dv_xname);
		} else {
			chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);

			malo_cmd_set_channel(sc, chan);
		}
		timeout_add_msec(&sc->sc_scan_to, 500);
		break;
	case IEEE80211_S_AUTH:
		DPRINTF(1, "%s: newstate AUTH\n", sc->sc_dev.dv_xname);
		malo_cmd_set_postscan(sc, ic->ic_myaddr, 1);
		chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
		malo_cmd_set_channel(sc, chan);
		break;
	case IEEE80211_S_ASSOC:
		DPRINTF(1, "%s: newstate ASSOC\n", sc->sc_dev.dv_xname);
		if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
			malo_cmd_set_radio(sc, 1, 3); /* short preamble */
		else
			malo_cmd_set_radio(sc, 1, 1); /* long preamble */

		malo_cmd_set_aid(sc, ic->ic_bss->ni_bssid,
		    ic->ic_bss->ni_associd);

		if (ic->ic_fixed_rate == -1)
			/* automatic rate adaption */
			malo_cmd_set_rate(sc, 0);
		else {
			/* fixed rate */
			rate = malo_fix2rate(ic->ic_fixed_rate);
			malo_cmd_set_rate(sc, rate);
		}

		malo_set_slot(sc);
		break;
	case IEEE80211_S_RUN:
		DPRINTF(1, "%s: newstate RUN\n", sc->sc_dev.dv_xname);
		break;
	default:
		break;
	}

	return (sc->sc_newstate(ic, nstate, arg));
}

void
malo_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{

}

struct ieee80211_node *
malo_node_alloc(struct ieee80211com *ic)
{
	struct malo_node *wn;

	wn = malloc(sizeof(*wn), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (wn == NULL)
		return (NULL);

	return ((struct ieee80211_node *)wn);
}

int
malo_media_change(struct ifnet *ifp)
{
	int error;

	DPRINTF(1, "%s: %s\n", ifp->if_xname, __func__);

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return (error);

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		malo_init(ifp);

	return (0);
}

void
malo_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct malo_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_IEEE80211;
	if (ic->ic_state == IEEE80211_S_RUN)
		imr->ifm_status |= IFM_ACTIVE;

	/* report last TX rate used by chip */
	imr->ifm_active |= ieee80211_rate2media(ic, sc->sc_last_txrate,
	    ic->ic_curmode);

	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		imr->ifm_active |= IFM_IEEE80211_ADHOC;
		break;
	case IEEE80211_M_AHDEMO:
		break;
	case IEEE80211_M_HOSTAP:
		break;
#endif
	case IEEE80211_M_MONITOR:
		imr->ifm_active |= IFM_IEEE80211_MONITOR;
		break;
	default:
		break;
	}

	switch (ic->ic_curmode) {
		case IEEE80211_MODE_11B:
			imr->ifm_active |= IFM_IEEE80211_11B;
			break;
		case IEEE80211_MODE_11G:
			imr->ifm_active |= IFM_IEEE80211_11G;
			break;
	}
}

int
malo_chip2rate(int chip_rate)
{
	switch (chip_rate) {
	/* CCK rates */
	case  0:	return (2);
	case  1:	return (4);
	case  2:	return (11);
	case  3:	return (22);

	/* OFDM rates */
	case  4:	return (0); /* reserved */
	case  5:	return (12);
	case  6:	return (18);
	case  7:	return (24);
	case  8:	return (36);
	case  9:	return (48);
	case 10:	return (72);
	case 11:	return (96);
	case 12:	return (108);

	/* no rate select yet or unknown rate */
	default:	return (-1);
	}
}

int
malo_fix2rate(int fix_rate)
{
	switch (fix_rate) {
	/* CCK rates */
	case  0:	return (2);
	case  1:	return (4);
	case  2:	return (11);
	case  3:	return (22);

	/* OFDM rates */
	case  4:	return (12);
	case  5:	return (18);
	case  6:	return (24);
	case  7:	return (36);
	case  8:	return (48);
	case  9:	return (72);
	case 10:	return (96);
	case 11:	return (108);

	/* unknown rate: should not happen */
	default:	return (0);
	}
}

void
malo_next_scan(void *arg)
{
	struct malo_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	DPRINTF(1, "%s: %s\n", ifp->if_xname, __func__);

	s = splnet();

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);

	splx(s);
}

void
malo_tx_intr(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct malo_tx_desc *desc;
	struct malo_tx_data *data;
	struct malo_node *rn;
	int stat;

	DPRINTF(2, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	stat = sc->sc_txring.stat;
	for (;;) {
		desc = &sc->sc_txring.desc[sc->sc_txring.stat];
		data = &sc->sc_txring.data[sc->sc_txring.stat];
		rn = (struct malo_node *)data->ni;

		/* check if TX descriptor is not owned by FW anymore */
		if ((letoh32(desc->status) & 0x80000000) ||
		    !(letoh32(data->softstat) & 0x80))
			break;

		/* if no frame has been sent, ignore */
		if (rn == NULL)
			goto next;

		/* check TX state */
		switch (letoh32(desc->status) & 0x1) {
		case 0x1:
			DPRINTF(2, "%s: data frame was sent successfully\n",
			    sc->sc_dev.dv_xname);
			break;
		default:
			DPRINTF(1, "%s: data frame sending error\n",
			    sc->sc_dev.dv_xname);
			ifp->if_oerrors++;
			break;
		}

		/* save last used TX rate */
		sc->sc_last_txrate = malo_chip2rate(desc->datarate);

		/* cleanup TX data and TX descriptor */
		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->map);
		m_freem(data->m);
		ieee80211_release_node(ic, data->ni);
		data->m = NULL;
		data->ni = NULL;
		data->softstat &= htole32(~0x80);
		desc->status = 0;
		desc->len = 0;

		DPRINTF(2, "%s: tx done idx=%d\n",
		    sc->sc_dev.dv_xname, sc->sc_txring.stat);

		sc->sc_txring.queued--;
next:
		if (++sc->sc_txring.stat >= sc->sc_txring.count)
			sc->sc_txring.stat = 0;
		if (sc->sc_txring.stat == stat)
			break;
	}

	sc->sc_tx_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);
	malo_start(ifp);
}

int
malo_tx_mgt(struct malo_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct malo_tx_desc *desc;
	struct malo_tx_data *data;
	struct ieee80211_frame *wh;
	int error;

	DPRINTF(2, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	desc = &sc->sc_txring.desc[sc->sc_txring.cur];
	data = &sc->sc_txring.data[sc->sc_txring.cur];

	if (m0->m_len < sizeof(struct ieee80211_frame)) {
		m0 = m_pullup(m0, sizeof(struct ieee80211_frame));
		if (m0 == NULL) {
			ifp->if_ierrors++;
			return (ENOBUFS);
		}
	}
	wh = mtod(m0, struct ieee80211_frame *);

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct malo_tx_radiotap_hdr *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = sc->sc_last_txrate;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_txtap_len;
		mb.m_next = m0;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
	}
#endif
	/*
	 * inject FW specific fields into the 802.11 frame
	 *
	 *  2 bytes FW len (inject)
	 * 24 bytes 802.11 frame header
	 *  6 bytes addr4 (inject)
	 *  n bytes 802.11 frame body
	 */
	if (m_leadingspace(m0) < 8) {
		if (m_trailingspace(m0) < 8)
			panic("%s: not enough space for mbuf dance",
			    sc->sc_dev.dv_xname);
		bcopy(m0->m_data, m0->m_data + 8, m0->m_len);
		m0->m_data += 8;
	}

	/* move frame header */
	bcopy(m0->m_data, m0->m_data - 6, sizeof(*wh));
	m0->m_data -= 8;
	m0->m_len += 8;
	m0->m_pkthdr.len += 8;
	*mtod(m0, uint16_t *) = htole16(m0->m_len - 32); /* FW len */

	error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m0,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		m_freem(m0);
		return (error);
	}

	data->m = m0;
	data->ni = ni;
	data->softstat |= htole32(0x80);

	malo_tx_setup_desc(sc, desc, m0->m_pkthdr.len, 0,
	    data->map->dm_segs, data->map->dm_nsegs);

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_txring.map,
	    sc->sc_txring.cur * sizeof(struct malo_tx_desc),
	    sizeof(struct malo_tx_desc), BUS_DMASYNC_PREWRITE);

	DPRINTF(2, "%s: sending mgmt frame, pktlen=%u, idx=%u\n",
	    sc->sc_dev.dv_xname, m0->m_pkthdr.len, sc->sc_txring.cur);

	sc->sc_txring.queued++;
	sc->sc_txring.cur = (sc->sc_txring.cur + 1) % MALO_TX_RING_COUNT;

	/* kick mgmt TX */
	malo_ctl_write4(sc, 0x0c18, 1);
	malo_ctl_barrier(sc, BUS_SPACE_BARRIER_WRITE);

	return (0);
}

int
malo_tx_data(struct malo_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct malo_tx_desc *desc;
	struct malo_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	struct mbuf *mnew;
	int error;

	DPRINTF(2, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	desc = &sc->sc_txring.desc[sc->sc_txring.cur];
	data = &sc->sc_txring.data[sc->sc_txring.cur];

	if (m0->m_len < sizeof(struct ieee80211_frame)) {
		m0 = m_pullup(m0, sizeof(struct ieee80211_frame));
		if (m0 == NULL) {
			ifp->if_ierrors++;
			return (ENOBUFS);
		}
	}
	wh = mtod(m0, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		k = ieee80211_get_txkey(ic, wh, ni);
		if ((m0 = ieee80211_encrypt(ic, m0, k)) == NULL)
			return (ENOBUFS);

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct malo_tx_radiotap_hdr *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = sc->sc_last_txrate;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_txtap_len;
		mb.m_next = m0;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
	}
#endif

	/*
	 * inject FW specific fields into the 802.11 frame
	 *
	 *  2 bytes FW len (inject)
	 * 24 bytes 802.11 frame header
	 *  6 bytes addr4 (inject)
	 *  n bytes 802.11 frame body
	 *
	 * For now copy all into a new mcluster.
	 */
	MGETHDR(mnew, M_DONTWAIT, MT_DATA);
	if (mnew == NULL)
		return (ENOBUFS);
	MCLGET(mnew, M_DONTWAIT);
	if (!(mnew->m_flags & M_EXT)) {
		m_free(mnew);
		return (ENOBUFS);
	}

	*mtod(mnew, uint16_t *) = htole16(m0->m_pkthdr.len - 24); /* FW len */
	bcopy(wh, mtod(mnew, caddr_t) + 2, sizeof(*wh));
	bzero(mtod(mnew, caddr_t) + 26, 6);
	m_copydata(m0, sizeof(*wh), m0->m_pkthdr.len - sizeof(*wh),
	    mtod(mnew, caddr_t) + 32);
	mnew->m_pkthdr.len = mnew->m_len = m0->m_pkthdr.len + 8;
	m_freem(m0);
	m0 = mnew;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m0,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		m_freem(m0);
		return (error);
	}

	data->m = m0;
	data->ni = ni;
	data->softstat |= htole32(0x80);

	malo_tx_setup_desc(sc, desc, m0->m_pkthdr.len, 1,
	    data->map->dm_segs, data->map->dm_nsegs);

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_txring.map,
	    sc->sc_txring.cur * sizeof(struct malo_tx_desc),
	    sizeof(struct malo_tx_desc), BUS_DMASYNC_PREWRITE);

	DPRINTF(2, "%s: sending data frame, pktlen=%u, idx=%u\n",
	    sc->sc_dev.dv_xname, m0->m_pkthdr.len, sc->sc_txring.cur);

	sc->sc_txring.queued++;
	sc->sc_txring.cur = (sc->sc_txring.cur + 1) % MALO_TX_RING_COUNT;

	/* kick data TX */
	malo_ctl_write4(sc, 0x0c18, 1);
	malo_ctl_barrier(sc, BUS_SPACE_BARRIER_WRITE);

	return (0);
}

void
malo_tx_setup_desc(struct malo_softc *sc, struct malo_tx_desc *desc,
    int len, int rate, const bus_dma_segment_t *segs, int nsegs)
{
	desc->len = htole16(segs[0].ds_len);
	desc->datarate = rate; /* 0 = mgmt frame, 1 = data frame */
	desc->physdata = htole32(segs[0].ds_addr);
	desc->status = htole32(0x00000001 | 0x80000000);
}

void
malo_rx_intr(struct malo_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct malo_rx_desc *desc;
	struct malo_rx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct mbuf *mnew, *m;
	uint32_t rxRdPtr, rxWrPtr;
	int error, i;

	rxRdPtr = malo_mem_read4(sc, sc->sc_RxPdRdPtr);
	rxWrPtr = malo_mem_read4(sc, sc->sc_RxPdWrPtr);

	for (i = 0; i < MALO_RX_RING_COUNT && rxRdPtr != rxWrPtr; i++) {
		desc = &sc->sc_rxring.desc[sc->sc_rxring.cur];
		data = &sc->sc_rxring.data[sc->sc_rxring.cur];

		bus_dmamap_sync(sc->sc_dmat, sc->sc_rxring.map,
		    sc->sc_rxring.cur * sizeof(struct malo_rx_desc),
		    sizeof(struct malo_rx_desc), BUS_DMASYNC_POSTREAD);

		DPRINTF(3, "%s: rx intr idx=%d, rxctrl=0x%02x, rssi=%d, "
		    "status=0x%02x, channel=%d, len=%d, res1=%02x, rate=%d, "
		    "physdata=0x%04x, physnext=0x%04x, qosctrl=%02x, res2=%d\n",
		    sc->sc_dev.dv_xname,
		    sc->sc_rxring.cur, desc->rxctrl, desc->rssi, desc->status,
		    desc->channel, letoh16(desc->len), desc->reserved1,
		    desc->datarate, letoh32(desc->physdata),
		    letoh32(desc->physnext), desc->qosctrl, desc->reserved2);

		if ((desc->rxctrl & 0x80) == 0)
			break;

		MGETHDR(mnew, M_DONTWAIT, MT_DATA);
		if (mnew == NULL) {
			ifp->if_ierrors++;
			goto skip;
		}

		MCLGET(mnew, M_DONTWAIT);
		if (!(mnew->m_flags & M_EXT)) {
			m_freem(mnew);
			ifp->if_ierrors++;
			goto skip;
		}

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, data->map);

		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(mnew, void *), MCLBYTES, NULL, BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(mnew);

			error = bus_dmamap_load(sc->sc_dmat, data->map,
			    mtod(data->m, void *), MCLBYTES, NULL,
			    BUS_DMA_NOWAIT);
			if (error != 0) {
				panic("%s: could not load old rx mbuf",
				    sc->sc_dev.dv_xname);
			}
			ifp->if_ierrors++;
			goto skip;
		}

		/*
		 * New mbuf successfully loaded
		 */
		m = data->m;
		data->m = mnew;
		desc->physdata = htole32(data->map->dm_segs->ds_addr);

		/* finalize mbuf */
		m->m_pkthdr.len = m->m_len = letoh16(desc->len);

		/*
		 * cut out FW specific fields from the 802.11 frame
		 *
		 *  2 bytes FW len (cut out)
		 * 24 bytes 802.11 frame header
		 *  6 bytes addr4 (cut out)
		 *  n bytes 802.11 frame data
		 */
		bcopy(m->m_data, m->m_data + 6, 26);
		m_adj(m, 8);

#if NBPFILTER > 0
		if (sc->sc_drvbpf != NULL) {
			struct mbuf mb;
			struct malo_rx_radiotap_hdr *tap = &sc->sc_rxtap;

			tap->wr_flags = 0;
			tap->wr_chan_freq =
			    htole16(ic->ic_bss->ni_chan->ic_freq);
			tap->wr_chan_flags =
			    htole16(ic->ic_bss->ni_chan->ic_flags);
			tap->wr_rssi = desc->rssi;
			tap->wr_max_rssi = ic->ic_max_rssi;

			mb.m_data = (caddr_t)tap;
			mb.m_len = sc->sc_rxtap_len;
			mb.m_next = m;
			mb.m_nextpkt = NULL;
			mb.m_type = 0;
			mb.m_flags = 0;
			bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
		}
#endif

		wh = mtod(m, struct ieee80211_frame *);
		ni = ieee80211_find_rxnode(ic, wh);

		/* send the frame to the 802.11 layer */
		memset(&rxi, 0, sizeof(rxi));
		rxi.rxi_rssi = desc->rssi;
		ieee80211_inputm(ifp, m, ni, &rxi, &ml);

		/* node is no longer needed */
		ieee80211_release_node(ic, ni);

skip:
		desc->rxctrl = 0;
		rxRdPtr = letoh32(desc->physnext);

		bus_dmamap_sync(sc->sc_dmat, sc->sc_rxring.map,
		    sc->sc_rxring.cur * sizeof(struct malo_rx_desc),
		    sizeof(struct malo_rx_desc), BUS_DMASYNC_PREWRITE);

		sc->sc_rxring.cur = (sc->sc_rxring.cur + 1) %
		    MALO_RX_RING_COUNT;
	}
	if_input(ifp, &ml);

	malo_mem_write4(sc, sc->sc_RxPdRdPtr, rxRdPtr);
}

int
malo_load_bootimg(struct malo_softc *sc)
{
	char *name = "malo8335-h";
	uint8_t	*ucode;
	size_t usize;
	int error, i;

	/* load boot firmware */
	if ((error = loadfirmware(name, &ucode, &usize)) != 0) {
		printf("%s: error %d, could not read firmware %s\n",
		    sc->sc_dev.dv_xname, error, name);
		return (EIO);
	}

	/*
	 * It seems we are putting this code directly onto the stack of
	 * the ARM cpu. I don't know why we need to instruct the DMA
	 * engine to move the code. This is a big riddle without docu.
	 */
	DPRINTF(1, "%s: loading boot firmware\n", sc->sc_dev.dv_xname);
	malo_mem_write2(sc, 0xbef8, 0x001);
	malo_mem_write2(sc, 0xbefa, usize);
	malo_mem_write4(sc, 0xbefc, 0);

	bus_space_write_region_1(sc->sc_mem1_bt, sc->sc_mem1_bh, 0xbf00,
	    ucode, usize);

	/*
	 * we loaded the firmware into card memory now tell the CPU
	 * to fetch the code and execute it. The memory mapped via the
	 * first bar is internally mapped to 0xc0000000.
	 */
	malo_send_cmd(sc, 0xc000bef8);

	/* wait for the device to go into FW loading mode */
	for (i = 0; i < 10; i++) {
		delay(50);
		malo_ctl_barrier(sc, BUS_SPACE_BARRIER_READ);
		if (malo_ctl_read4(sc, 0x0c14) == 0x5)
			break;
	}
	if (i == 10) {
		printf("%s: timeout at boot firmware load!\n",
		    sc->sc_dev.dv_xname);
		free(ucode, M_DEVBUF, usize);
		return (ETIMEDOUT);
	}
	free(ucode, M_DEVBUF, usize);

	/* tell the card we're done and... */
	malo_mem_write2(sc, 0xbef8, 0x001);
	malo_mem_write2(sc, 0xbefa, 0);
	malo_mem_write4(sc, 0xbefc, 0);
	malo_send_cmd(sc, 0xc000bef8);

	DPRINTF(1, "%s: boot firmware loaded\n", sc->sc_dev.dv_xname);

	return (0);
}

int
malo_load_firmware(struct malo_softc *sc)
{
	struct malo_cmdheader *hdr;
	char *name = "malo8335-m";
	void *data;
	uint8_t *ucode;
	size_t size, count, bsize;
	int i, sn, error;

	/* load real firmware now */
	if ((error = loadfirmware(name, &ucode, &size)) != 0) {
		printf("%s: error %d, could not read firmware %s\n",
		    sc->sc_dev.dv_xname, error, name);
		return (EIO);
	}

	DPRINTF(1, "%s: uploading firmware\n", sc->sc_dev.dv_xname);

	hdr = sc->sc_cmd_mem;
	data = hdr + 1;
	sn = 1;
	for (count = 0; count < size; count += bsize) {
		bsize = MIN(256, size - count);

		hdr->cmd = htole16(0x0001);
		hdr->size = htole16(bsize);
		hdr->seqnum = htole16(sn++);
		hdr->result = 0;

		bcopy(ucode + count, data, bsize);

		bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
		    BUS_DMASYNC_PREWRITE);
		malo_send_cmd(sc, sc->sc_cmd_dmaaddr);
		bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
		    BUS_DMASYNC_POSTWRITE);
		delay(500);
	}
	free(ucode, M_DEVBUF, size);

	DPRINTF(1, "%s: firmware upload finished\n", sc->sc_dev.dv_xname);

	/*
	 * send a command with size 0 to tell that the firmware has been
	 * uploaded
	 */
	hdr->cmd = htole16(0x0001);
	hdr->size = 0;
	hdr->seqnum = htole16(sn++);
	hdr->result = 0;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE);
	malo_send_cmd(sc, sc->sc_cmd_dmaaddr);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_POSTWRITE);
	delay(100);

	DPRINTF(1, "%s: loading firmware\n", sc->sc_dev.dv_xname);

	/* wait until firmware has been loaded */
	for (i = 0; i < 200; i++) {
		malo_ctl_write4(sc, 0x0c10, 0x5a);
		delay(500);
		malo_ctl_barrier(sc, BUS_SPACE_BARRIER_WRITE |
		     BUS_SPACE_BARRIER_READ);
		if (malo_ctl_read4(sc, 0x0c14) == 0xf0f1f2f4)
			break;
	}
	if (i == 200) {
		printf("%s: timeout at firmware load!\n", sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	DPRINTF(1, "%s: firmware loaded\n", sc->sc_dev.dv_xname);

	return (0);
}

int
malo_set_slot(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_flags & IEEE80211_F_SHSLOT) {
		/* set short slot */
		if (malo_cmd_set_slot(sc, 1)) {
			printf("%s: setting short slot failed\n",
			    sc->sc_dev.dv_xname);
			return (ENXIO);
		}
	} else {
		/* set long slot */
		if (malo_cmd_set_slot(sc, 0)) {
			printf("%s: setting long slot failed\n",
			    sc->sc_dev.dv_xname);
			return (ENXIO);
		}
	}

	return (0);
}

void
malo_update_slot(struct ieee80211com *ic)
{
	struct malo_softc *sc = ic->ic_if.if_softc;

	malo_set_slot(sc);

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
		/* TODO */
	}
#endif
}

#ifdef MALO_DEBUG
void
malo_hexdump(void *buf, int len)
{
	u_char b[16];
	int i, j, l;

	for (i = 0; i < len; i += l) {
		printf("%4i:", i);
		l = min(sizeof(b), len - i);
		bcopy(buf + i, b, l);
		
		for (j = 0; j < sizeof(b); j++) {
			if (j % 2 == 0)
				printf(" ");
			if (j % 8 == 0)
				printf(" ");
			if (j < l)
				printf("%02x", (int)b[j]);
			else
				printf("  ");
		}
		printf("  |");
		for (j = 0; j < l; j++) {
			if (b[j] >= 0x20 && b[j] <= 0x7e)
				printf("%c", b[j]);
			else
				printf(".");
		}
		printf("|\n");
	}
}
#endif

static char *
malo_cmd_string(uint16_t cmd)
{
	int i;
	static char cmd_buf[16];
	static const struct {
		uint16_t	 cmd_code;
		char		*cmd_string;
	} cmds[] = {
		{ MALO_CMD_GET_HW_SPEC,		"GetHwSpecifications"	},
		{ MALO_CMD_SET_RADIO,		"SetRadio"		},
		{ MALO_CMD_SET_AID,		"SetAid"		},
		{ MALO_CMD_SET_TXPOWER,		"SetTxPower"		},
		{ MALO_CMD_SET_ANTENNA,		"SetAntenna"		},
		{ MALO_CMD_SET_PRESCAN,		"SetPrescan"		},
		{ MALO_CMD_SET_POSTSCAN,	"SetPostscan"		},
		{ MALO_CMD_SET_RATE,		"SetRate"		},
		{ MALO_CMD_SET_CHANNEL,		"SetChannel"		},
		{ MALO_CMD_SET_RTS,		"SetRTS"		},
		{ MALO_CMD_SET_SLOT,		"SetSlot"		},
	};

	for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
		if ((letoh16(cmd) & 0x7fff) == cmds[i].cmd_code)
			return (cmds[i].cmd_string);

	snprintf(cmd_buf, sizeof(cmd_buf), "unknown %#x", cmd);
	return (cmd_buf);
}

static char *
malo_cmd_string_result(uint16_t result)
{
	int i;
	static const struct {
		uint16_t	 result_code;
		char		*result_string;
	} results[] = {
		{ MALO_CMD_RESULT_OK,		"OK"		},
		{ MALO_CMD_RESULT_ERROR,	"general error"	},
		{ MALO_CMD_RESULT_NOSUPPORT,	"not supported" },
		{ MALO_CMD_RESULT_PENDING,	"pending"	},
		{ MALO_CMD_RESULT_BUSY,		"ignored"	},
		{ MALO_CMD_RESULT_PARTIALDATA,	"incomplete"	},
	};

	for (i = 0; i < sizeof(results) / sizeof(results[0]); i++)
		if (letoh16(result) == results[i].result_code)
			return (results[i].result_string);

	return ("unknown");
}

int
malo_cmd_get_spec(struct malo_softc *sc)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_hw_spec *spec;

	hdr->cmd = htole16(MALO_CMD_GET_HW_SPEC);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*spec));
	hdr->seqnum = htole16(42);	/* the one and only */
	hdr->result = 0;
	spec = (struct malo_hw_spec *)(hdr + 1);

	bzero(spec, sizeof(*spec));
	memset(spec->PermanentAddress, 0xff, ETHER_ADDR_LEN);
	spec->CookiePtr = htole32(sc->sc_cookie_dmaaddr);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE|BUS_DMASYNC_PREREAD);

	if (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr) != 0)
		return (ETIMEDOUT);

	/* get the data from the buffer */
	DPRINTF(1, "%s: get_hw_spec: V%x R%x, #WCB %d, #Mcast %d, Regcode %d, "
	    "#Ant %d\n", sc->sc_dev.dv_xname, htole16(spec->HwVersion),
	    htole32(spec->FWReleaseNumber), htole16(spec->NumOfWCB),
	    htole16(spec->NumOfMCastAdr), htole16(spec->RegionCode),
	    htole16(spec->NumberOfAntenna));

	/* tell the DMA engine where our rings are */
	malo_mem_write4(sc, letoh32(spec->RxPdRdPtr) & 0xffff,
	    sc->sc_rxring.physaddr);
	malo_mem_write4(sc, letoh32(spec->RxPdWrPtr) & 0xffff,
	    sc->sc_rxring.physaddr);
	malo_mem_write4(sc, letoh32(spec->WcbBase0) & 0xffff,
	    sc->sc_txring.physaddr);

	/* save DMA RX pointers for later use */
	sc->sc_RxPdRdPtr = letoh32(spec->RxPdRdPtr) & 0xffff;
	sc->sc_RxPdWrPtr = letoh32(spec->RxPdWrPtr) & 0xffff;

	return (0);
}

int
malo_cmd_set_prescan(struct malo_softc *sc)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;

	hdr->cmd = htole16(MALO_CMD_SET_PRESCAN);
	hdr->size = htole16(sizeof(*hdr));
	hdr->seqnum = 1;
	hdr->result = 0;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

int
malo_cmd_set_postscan(struct malo_softc *sc, uint8_t *macaddr, uint8_t ibsson)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_cmd_postscan *body;

	hdr->cmd = htole16(MALO_CMD_SET_POSTSCAN);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*body));
	hdr->seqnum = 1;
	hdr->result = 0;
	body = (struct malo_cmd_postscan *)(hdr + 1);

	bzero(body, sizeof(*body));
	memcpy(&body->bssid, macaddr, ETHER_ADDR_LEN);
	body->isibss = htole32(ibsson);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

int
malo_cmd_set_channel(struct malo_softc *sc, uint8_t channel)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_cmd_channel *body;

	hdr->cmd = htole16(MALO_CMD_SET_CHANNEL);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*body));
	hdr->seqnum = 1;
	hdr->result = 0;
	body = (struct malo_cmd_channel *)(hdr + 1);

	bzero(body, sizeof(*body));
	body->action = htole16(1);
	body->channel = channel;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

int
malo_cmd_set_antenna(struct malo_softc *sc, uint16_t antenna)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_cmd_antenna *body;

	hdr->cmd = htole16(MALO_CMD_SET_ANTENNA);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*body));
	hdr->seqnum = 1;
	hdr->result = 0;
	body = (struct malo_cmd_antenna *)(hdr + 1);

	bzero(body, sizeof(*body));
	body->action = htole16(antenna);
	if (antenna == 1)
		body->mode = htole16(0xffff);
	else
		body->mode = htole16(2);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

int
malo_cmd_set_radio(struct malo_softc *sc, uint16_t enable,
    uint16_t preamble_mode)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_cmd_radio *body;

	hdr->cmd = htole16(MALO_CMD_SET_RADIO);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*body));
	hdr->seqnum = 1;
	hdr->result = 0;
	body = (struct malo_cmd_radio *)(hdr + 1);

	bzero(body, sizeof(*body));
	body->action = htole16(1);
	body->preamble_mode = htole16(preamble_mode);
	body->enable = htole16(enable);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

int
malo_cmd_set_aid(struct malo_softc *sc, uint8_t *bssid, uint16_t associd)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_cmd_aid *body;

	hdr->cmd = htole16(MALO_CMD_SET_AID);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*body));
	hdr->seqnum = 1;
	hdr->result = 0;
	body = (struct malo_cmd_aid *)(hdr + 1);

	bzero(body, sizeof(*body));
	body->associd = htole16(associd);
	memcpy(&body->macaddr[0], bssid, IEEE80211_ADDR_LEN);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

int
malo_cmd_set_txpower(struct malo_softc *sc, unsigned int powerlevel)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_cmd_txpower *body;

	hdr->cmd = htole16(MALO_CMD_SET_TXPOWER);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*body));
	hdr->seqnum = 1;
	hdr->result = 0;
	body = (struct malo_cmd_txpower *)(hdr + 1);

	bzero(body, sizeof(*body));
	body->action = htole16(1);
	if (powerlevel < 30)
		body->supportpowerlvl = htole16(5);	/* LOW */
	else if (powerlevel >= 30 && powerlevel < 60)
		body->supportpowerlvl = htole16(10);	/* MEDIUM */
	else
		body->supportpowerlvl = htole16(15);	/* HIGH */

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

int
malo_cmd_set_rts(struct malo_softc *sc, uint32_t threshold)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_cmd_rts *body;

	hdr->cmd = htole16(MALO_CMD_SET_RTS);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*body));
	hdr->seqnum = 1;
	hdr->result = 0;
	body = (struct malo_cmd_rts *)(hdr + 1);

	bzero(body, sizeof(*body));
	body->action = htole16(1);
	body->threshold = htole32(threshold);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

int
malo_cmd_set_slot(struct malo_softc *sc, uint8_t slot)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_cmd_slot *body;

	hdr->cmd = htole16(MALO_CMD_SET_SLOT);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*body));
	hdr->seqnum = 1;
	hdr->result = 0;
	body = (struct malo_cmd_slot *)(hdr + 1);

	bzero(body, sizeof(*body));
	body->action = htole16(1);
	body->slot = slot;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

int
malo_cmd_set_rate(struct malo_softc *sc, uint8_t rate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;
	struct malo_cmd_rate *body;
	int i;

	hdr->cmd = htole16(MALO_CMD_SET_RATE);
	hdr->size = htole16(sizeof(*hdr) + sizeof(*body));
	hdr->seqnum = 1;
	hdr->result = 0;
	body = (struct malo_cmd_rate *)(hdr + 1);

	bzero(body, sizeof(*body));

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
		/* TODO */
	} else
#endif
	{
		body->aprates[0] = 2;
		body->aprates[1] = 4;
		body->aprates[2] = 11;
		body->aprates[3] = 22;
		if (ic->ic_curmode == IEEE80211_MODE_11G) {
			body->aprates[4] = 0;
			body->aprates[5] = 12;
			body->aprates[6] = 18;
			body->aprates[7] = 24;
			body->aprates[8] = 36;
			body->aprates[9] = 48;
			body->aprates[10] = 72;
			body->aprates[11] = 96;
			body->aprates[12] = 108;
		}
	}

	if (rate != 0) {
		/* fixed rate */
		for (i = 0; i < 13; i++) {
			if (body->aprates[i] == rate) {
				body->rateindex = i;
				body->dataratetype = 1;
				break;
			}
		}
	}

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cmd_dmam, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (malo_send_cmd_dma(sc, sc->sc_cmd_dmaaddr));
}

void
malo_cmd_response(struct malo_softc *sc)
{
	struct malo_cmdheader *hdr = sc->sc_cmd_mem;

	if (letoh16(hdr->result) != MALO_CMD_RESULT_OK) {
		printf("%s: firmware cmd %s failed with %s\n",
		    sc->sc_dev.dv_xname,
		    malo_cmd_string(hdr->cmd),
		    malo_cmd_string_result(hdr->result));
	}

#ifdef MALO_DEBUG
	printf("%s: cmd answer for %s=%s\n",
	    sc->sc_dev.dv_xname,
	    malo_cmd_string(hdr->cmd),
	    malo_cmd_string_result(hdr->result));

	if (malo_d > 2)
		malo_hexdump(hdr, letoh16(hdr->size));
#endif
}
