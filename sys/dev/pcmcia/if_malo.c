/*      $OpenBSD: if_malo.c,v 1.100 2024/05/26 08:46:28 jsg Exp $ */

/*
 * Copyright (c) 2007 Marcus Glocker <mglocker@openbsd.org>
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
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/malloc.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_llc.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciadevs.h>

#include <dev/pcmcia/if_malovar.h>
#include <dev/pcmcia/if_maloreg.h>

/*
 * Driver for the Marvell 88W8385 chip (Compact Flash).
 */

#ifdef CMALO_DEBUG
int cmalo_d = 1;
#define DPRINTF(l, x...)	do { if ((l) <= cmalo_d) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

int	malo_pcmcia_match(struct device *, void *, void *);
void	malo_pcmcia_attach(struct device *, struct device *, void *);
int	malo_pcmcia_detach(struct device *, int);
int	malo_pcmcia_activate(struct device *, int);
void	malo_pcmcia_wakeup(struct malo_softc *);

void	cmalo_attach(struct device *);
int	cmalo_ioctl(struct ifnet *, u_long, caddr_t);
int	cmalo_fw_alloc(struct malo_softc *);
void	cmalo_fw_free(struct malo_softc *);
int	cmalo_fw_load_helper(struct malo_softc *);
int	cmalo_fw_load_main(struct malo_softc *);
int	cmalo_init(struct ifnet *);
void	cmalo_stop(struct malo_softc *);
int	cmalo_media_change(struct ifnet *);
int	cmalo_newstate(struct ieee80211com *, enum ieee80211_state, int);
void	cmalo_detach(void *);
int	cmalo_intr(void *);
void	cmalo_intr_mask(struct malo_softc *, int);
void	cmalo_rx(struct malo_softc *);
void	cmalo_start(struct ifnet *);
void	cmalo_watchdog(struct ifnet *);
int	cmalo_tx(struct malo_softc *, struct mbuf *);
void	cmalo_tx_done(struct malo_softc *);
void	cmalo_event(struct malo_softc *);
void	cmalo_select_network(struct malo_softc *);
void	cmalo_reflect_network(struct malo_softc *);
int	cmalo_wep(struct malo_softc *);
int	cmalo_rate2bitmap(int);

void	cmalo_hexdump(void *, int);
int	cmalo_cmd_get_hwspec(struct malo_softc *);
int	cmalo_cmd_rsp_hwspec(struct malo_softc *);
int	cmalo_cmd_set_reset(struct malo_softc *);
int	cmalo_cmd_set_scan(struct malo_softc *);
int	cmalo_cmd_rsp_scan(struct malo_softc *);
int	cmalo_parse_elements(struct malo_softc *, void *, int, int);
int	cmalo_cmd_set_auth(struct malo_softc *);
int	cmalo_cmd_set_wep(struct malo_softc *, uint16_t,
	    struct ieee80211_key *);
int	cmalo_cmd_set_snmp(struct malo_softc *, uint16_t);
int	cmalo_cmd_set_radio(struct malo_softc *, uint16_t);
int	cmalo_cmd_set_channel(struct malo_softc *, uint16_t);
int	cmalo_cmd_set_txpower(struct malo_softc *, int16_t);
int	cmalo_cmd_set_antenna(struct malo_softc *, uint16_t);
int	cmalo_cmd_set_macctrl(struct malo_softc *);
int	cmalo_cmd_set_macaddr(struct malo_softc *, uint8_t *);
int	cmalo_cmd_set_assoc(struct malo_softc *);
int	cmalo_cmd_rsp_assoc(struct malo_softc *);
int	cmalo_cmd_set_80211d(struct malo_softc *);
int	cmalo_cmd_set_bgscan_config(struct malo_softc *);
int	cmalo_cmd_set_bgscan_query(struct malo_softc *);
int	cmalo_cmd_set_rate(struct malo_softc *, int);
int	cmalo_cmd_request(struct malo_softc *, uint16_t, int);
int	cmalo_cmd_response(struct malo_softc *);

/*
 * PCMCIA bus.
 */
struct malo_pcmcia_softc {
	struct malo_softc	 sc_malo;

	struct pcmcia_function	*sc_pf;
	struct pcmcia_io_handle	 sc_pcioh;
	int			 sc_io_window;
	void			*sc_ih;
};

const struct cfattach malo_pcmcia_ca = {
	sizeof(struct malo_pcmcia_softc),
	malo_pcmcia_match,
	malo_pcmcia_attach,
	malo_pcmcia_detach,
	malo_pcmcia_activate
};

int
malo_pcmcia_match(struct device *parent, void *match, void *aux)
{
	struct pcmcia_attach_args *pa = aux;

	if (pa->manufacturer == PCMCIA_VENDOR_AMBICOM &&
	    pa->product == PCMCIA_PRODUCT_AMBICOM_WL54CF)
		return (1);

	return (0);
}

void
malo_pcmcia_attach(struct device *parent, struct device *self, void *aux)
{
	struct malo_pcmcia_softc *psc = (struct malo_pcmcia_softc *)self;
	struct malo_softc *sc = &psc->sc_malo;
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_config_entry *cfe;
	const char *intrstr = NULL;

	psc->sc_pf = pa->pf;
	cfe = SIMPLEQ_FIRST(&pa->pf->cfe_head);

	/* enable card */
	pcmcia_function_init(psc->sc_pf, cfe);
	if (pcmcia_function_enable(psc->sc_pf)) {
		printf(": can't enable function\n");
		return;
	}

	/* allocate I/O space */
	if (pcmcia_io_alloc(psc->sc_pf, 0,
	    cfe->iospace[0].length, cfe->iospace[0].length, &psc->sc_pcioh)) {
		printf(": can't allocate i/o space\n");
		pcmcia_function_disable(psc->sc_pf);
		return;
	}

	/* map I/O space */
	if (pcmcia_io_map(psc->sc_pf, PCMCIA_WIDTH_IO16, 0,
	    cfe->iospace[0].length, &psc->sc_pcioh, &psc->sc_io_window)) {
		printf(": can't map i/o space\n");
		pcmcia_io_free(psc->sc_pf, &psc->sc_pcioh);
		pcmcia_function_disable(psc->sc_pf);
		return;
	}
	sc->sc_iot = psc->sc_pcioh.iot;
	sc->sc_ioh = psc->sc_pcioh.ioh;

	printf(" port 0x%lx/%ld", psc->sc_pcioh.addr, psc->sc_pcioh.size);

	/* establish interrupt */
	psc->sc_ih = pcmcia_intr_establish(psc->sc_pf, IPL_NET, cmalo_intr, sc,
	    sc->sc_dev.dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}
	intrstr = pcmcia_intr_string(psc->sc_pf, psc->sc_ih);
	if (intrstr != NULL) {
		if (*intrstr != '\0')
			printf(", %s", intrstr);
	}
	printf("\n");

	config_mountroot(self, cmalo_attach);
}

int
malo_pcmcia_detach(struct device *dev, int flags)
{
	struct malo_pcmcia_softc *psc = (struct malo_pcmcia_softc *)dev;
	struct malo_softc *sc = &psc->sc_malo;

	cmalo_detach(sc);

	pcmcia_io_unmap(psc->sc_pf, psc->sc_io_window);
	pcmcia_io_free(psc->sc_pf, &psc->sc_pcioh);

	return (0);
}

int
malo_pcmcia_activate(struct device *dev, int act)
{
	struct malo_pcmcia_softc *psc = (struct malo_pcmcia_softc *)dev;
	struct malo_softc *sc = &psc->sc_malo;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	switch (act) {
	case DVACT_SUSPEND:
		if ((sc->sc_flags & MALO_DEVICE_ATTACHED) &&
		    (ifp->if_flags & IFF_RUNNING))
			cmalo_stop(sc);
		if (psc->sc_ih)
			pcmcia_intr_disestablish(psc->sc_pf, psc->sc_ih);
		psc->sc_ih = NULL;
		pcmcia_function_disable(psc->sc_pf);
		break;
	case DVACT_RESUME:
		pcmcia_function_enable(psc->sc_pf);
		psc->sc_ih = pcmcia_intr_establish(psc->sc_pf, IPL_NET,
		    cmalo_intr, sc, sc->sc_dev.dv_xname);
		break;
	case DVACT_WAKEUP:
		malo_pcmcia_wakeup(sc);
		break;
	case DVACT_DEACTIVATE:
		if ((sc->sc_flags & MALO_DEVICE_ATTACHED) &&
		    (ifp->if_flags & IFF_RUNNING))
			cmalo_stop(sc);		/* XXX tries to touch regs */
		if (psc->sc_ih)
			pcmcia_intr_disestablish(psc->sc_pf, psc->sc_ih);
		psc->sc_ih = NULL;
		pcmcia_function_disable(psc->sc_pf);
		break;
	}
	return (0);
}

void
malo_pcmcia_wakeup(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;
	
	s = splnet();
	while (sc->sc_flags & MALO_BUSY)
		tsleep_nsec(&sc->sc_flags, 0, "malopwr", INFSLP);
	sc->sc_flags |= MALO_BUSY;

	cmalo_init(ifp);

	sc->sc_flags &= ~MALO_BUSY;
	wakeup(&sc->sc_flags);
	splx(s);
}

/*
 * Driver.
 */
void
cmalo_attach(struct device *self)
{
	struct malo_softc *sc = (struct malo_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int i;

	/* disable interrupts */
	cmalo_intr_mask(sc, 0);

	/* load firmware */
	if (cmalo_fw_alloc(sc) != 0)
		return;
	if (cmalo_fw_load_helper(sc) != 0)
		return;
	if (cmalo_fw_load_main(sc) != 0)
		return;
	sc->sc_flags |= MALO_FW_LOADED;

	/* allocate command buffer */
	sc->sc_cmd = malloc(MALO_CMD_BUFFER_SIZE, M_DEVBUF, M_NOWAIT);

	/* allocate data buffer */
	sc->sc_data = malloc(MCLBYTES, M_DEVBUF, M_NOWAIT);

	/* enable interrupts */
	cmalo_intr_mask(sc, 1);

	/* we are context save here for FW commands */
	sc->sc_cmd_ctxsave = 1;

	/* get hardware specs */
	cmalo_cmd_get_hwspec(sc);

	/* setup interface */
	ifp->if_softc = sc;
	ifp->if_ioctl = cmalo_ioctl;
	ifp->if_start = cmalo_start;
	ifp->if_watchdog = cmalo_watchdog;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_caps = IEEE80211_C_MONITOR | IEEE80211_C_WEP;

	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	for (i = 0; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
	}

	/* attach interface */
	if_attach(ifp);
	ieee80211_ifattach(ifp);

	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = cmalo_newstate;
	ieee80211_media_init(ifp, cmalo_media_change, ieee80211_media_status);

	/* second attach line */
	printf("%s: address %s\n",
	    sc->sc_dev.dv_xname, ether_sprintf(ic->ic_myaddr));

	/* device attached */
	sc->sc_flags |= MALO_DEVICE_ATTACHED;
}

int
cmalo_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct malo_softc *sc = ifp->if_softc;
	struct ieee80211_nodereq_all *na;
	struct ieee80211_nodereq *nr;
	int i, j, s, error = 0;

	s = splnet();
	/*
	 * Prevent processes from entering this function while another
	 * process is tsleep'ing in it.
	 */
	while ((sc->sc_flags & MALO_BUSY) && error == 0)
		error = tsleep_nsec(&sc->sc_flags, PCATCH, "maloioc", INFSLP);
	if (error != 0) {
		splx(s);
		return error;
	}
	sc->sc_flags |= MALO_BUSY;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				cmalo_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				cmalo_stop(sc);
		}
		break;
	case SIOCS80211SCAN:
		cmalo_cmd_set_scan(sc);
		break;
	case SIOCG80211ALLNODES:
		nr = NULL;
		na = (struct ieee80211_nodereq_all *)data;

		if ((nr = malloc(sizeof(*nr), M_DEVBUF, M_WAITOK)) == NULL)
			break;

		for (na->na_nodes = i = j = 0; i < sc->sc_net_num &&
		    (na->na_size >= j + sizeof(struct ieee80211_nodereq));
		    i++) {
			bzero(nr, sizeof(*nr));

			IEEE80211_ADDR_COPY(nr->nr_macaddr,
			    sc->sc_net[i].bssid);
			IEEE80211_ADDR_COPY(nr->nr_bssid,
			    sc->sc_net[i].bssid);
			nr->nr_channel = sc->sc_net[i].channel;
			nr->nr_chan_flags = IEEE80211_CHAN_B; /* XXX */
			nr->nr_rssi = sc->sc_net[i].rssi;
			nr->nr_max_rssi = 0; /* XXX */
			nr->nr_nwid_len = strlen(sc->sc_net[i].ssid);
			bcopy(sc->sc_net[i].ssid, nr->nr_nwid,
			    nr->nr_nwid_len);
			nr->nr_intval = sc->sc_net[i].beaconintvl;
			nr->nr_capinfo = sc->sc_net[i].capinfo;
			nr->nr_flags |= IEEE80211_NODEREQ_AP;

			if (copyout(nr, (caddr_t)na->na_node + j,
			    sizeof(struct ieee80211_nodereq)))
				break;

			j += sizeof(struct ieee80211_nodereq);
			na->na_nodes++;
		}

		if (nr)
			free(nr, M_DEVBUF, 0);
		break;
	default:
		error = ieee80211_ioctl(ifp, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & (IFF_UP | IFF_RUNNING))
			cmalo_init(ifp);
		error = 0;
	}

	sc->sc_flags &= ~MALO_BUSY;
	wakeup(&sc->sc_flags);
	splx(s);

	return (error);
}

int
cmalo_fw_alloc(struct malo_softc *sc)
{
	const char *name_h = "malo8385-h";
	const char *name_m = "malo8385-m";
	int error;

	if (sc->sc_fw_h == NULL) {
		/* read helper firmware image */
		error = loadfirmware(name_h, &sc->sc_fw_h, &sc->sc_fw_h_size);
		if (error != 0) {
			printf("%s: error %d, could not read firmware %s\n",
			    sc->sc_dev.dv_xname, error, name_h);
			return (EIO);
		}
	}

	if (sc->sc_fw_m == NULL) {
		/* read main firmware image */
		error = loadfirmware(name_m, &sc->sc_fw_m, &sc->sc_fw_m_size);
		if (error != 0) {
			printf("%s: error %d, could not read firmware %s\n",
			    sc->sc_dev.dv_xname, error, name_m);
			return (EIO);
		}
	}

	return (0);
}

void
cmalo_fw_free(struct malo_softc *sc)
{
	if (sc->sc_fw_h != NULL) {
		free(sc->sc_fw_h, M_DEVBUF, 0);
		sc->sc_fw_h = NULL;
	}

	if (sc->sc_fw_m != NULL) {
		free(sc->sc_fw_m, M_DEVBUF, 0);
		sc->sc_fw_m = NULL;
	}
}

int
cmalo_fw_load_helper(struct malo_softc *sc)
{
	uint8_t val8;
	uint16_t bsize, *uc;
	int offset, i;

	/* verify if the card is ready for firmware download */
	val8 = MALO_READ_1(sc, MALO_REG_SCRATCH);
	if (val8 == MALO_VAL_SCRATCH_FW_LOADED)
		/* firmware already loaded */
		return (0);
	if (val8 != MALO_VAL_SCRATCH_READY) {
		/* bad register value */
		printf("%s: device not ready for FW download\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}

	/* download the helper firmware */
	for (offset = 0; offset < sc->sc_fw_h_size; offset += bsize) {
		if (sc->sc_fw_h_size - offset >= MALO_FW_HELPER_BSIZE)
			bsize = MALO_FW_HELPER_BSIZE;
		else
			bsize = sc->sc_fw_h_size - offset;

		/* send a block in words and confirm it */
		DPRINTF(3, "%s: download helper FW block (%d bytes, %d off)\n",
		    sc->sc_dev.dv_xname, bsize, offset);
		MALO_WRITE_2(sc, MALO_REG_CMD_WRITE_LEN, bsize);
		uc = (uint16_t *)(sc->sc_fw_h + offset);
		for (i = 0; i < bsize / 2; i++)
			MALO_WRITE_2(sc, MALO_REG_CMD_WRITE, htole16(uc[i]));
		MALO_WRITE_1(sc, MALO_REG_HOST_STATUS, MALO_VAL_CMD_DL_OVER);
		MALO_WRITE_2(sc, MALO_REG_CARD_INTR_CAUSE,
		    MALO_VAL_CMD_DL_OVER);

		/* poll for an acknowledgement */
		for (i = 0; i < 50; i++) {
			if (MALO_READ_1(sc, MALO_REG_CARD_STATUS) ==
			    MALO_VAL_CMD_DL_OVER)
				break;
			delay(1000);
		}
		if (i == 50) {
			printf("%s: timeout while helper FW block download\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		}
	}

	/* helper firmware download done */
	MALO_WRITE_2(sc, MALO_REG_CMD_WRITE_LEN, 0);
	MALO_WRITE_1(sc, MALO_REG_HOST_STATUS, MALO_VAL_CMD_DL_OVER);
	MALO_WRITE_2(sc, MALO_REG_CARD_INTR_CAUSE, MALO_VAL_CMD_DL_OVER);
	DPRINTF(1, "%s: helper FW downloaded\n", sc->sc_dev.dv_xname);

	return (0);
}

int
cmalo_fw_load_main(struct malo_softc *sc)
{
	uint16_t val16, bsize, *uc;
	int offset, i, retry = 0;

	/* verify if the helper firmware has been loaded correctly */
	for (i = 0; i < 10; i++) {
		if (MALO_READ_1(sc, MALO_REG_RBAL) == MALO_FW_HELPER_LOADED)
			break;
		delay(1000);
	}
	if (i == 10) {
		printf("%s: helper FW not loaded\n", sc->sc_dev.dv_xname);
		return (EIO);
	}
	DPRINTF(1, "%s: helper FW loaded successfully\n", sc->sc_dev.dv_xname);

	/* download the main firmware */
	bsize = 0; /* XXX really??? */
	for (offset = 0; offset < sc->sc_fw_m_size; offset += bsize) {
		val16 = MALO_READ_2(sc, MALO_REG_RBAL);
		/*
		 * If the helper firmware serves us an odd integer then
		 * something went wrong and we retry to download the last
		 * block until we receive a good integer again, or give up.
		 */
		if (val16 & 0x0001) {
			if (retry > MALO_FW_MAIN_MAXRETRY) {
				printf("%s: main FW download failed\n",
				    sc->sc_dev.dv_xname);
				return (EIO);
			}
			retry++;
			offset -= bsize;
		} else {
			retry = 0;
			bsize = val16;
		}

		/* send a block in words and confirm it */
		DPRINTF(3, "%s: download main FW block (%d bytes, %d off)\n",
		    sc->sc_dev.dv_xname, bsize, offset);
		MALO_WRITE_2(sc, MALO_REG_CMD_WRITE_LEN, bsize);
		uc = (uint16_t *)(sc->sc_fw_m + offset);
		for (i = 0; i < bsize / 2; i++)
			MALO_WRITE_2(sc, MALO_REG_CMD_WRITE, htole16(uc[i]));
		MALO_WRITE_1(sc, MALO_REG_HOST_STATUS, MALO_VAL_CMD_DL_OVER);
		MALO_WRITE_2(sc, MALO_REG_CARD_INTR_CAUSE,
		    MALO_VAL_CMD_DL_OVER);

		/* poll for an acknowledgement */
		for (i = 0; i < 5000; i++) {
			if (MALO_READ_1(sc, MALO_REG_CARD_STATUS) ==
			    MALO_VAL_CMD_DL_OVER)
				break;
		}
		if (i == 5000) {
			printf("%s: timeout while main FW block download\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		}
	}

	DPRINTF(1, "%s: main FW downloaded\n", sc->sc_dev.dv_xname);

	/* verify if the main firmware has been loaded correctly */
	for (i = 0; i < 500; i++) {
		if (MALO_READ_1(sc, MALO_REG_SCRATCH) ==
		    MALO_VAL_SCRATCH_FW_LOADED)
			break;
		delay(1000);
	}
	if (i == 500) {
		printf("%s: main FW not loaded\n", sc->sc_dev.dv_xname);
		return (EIO);
	}

	DPRINTF(1, "%s: main FW loaded successfully\n", sc->sc_dev.dv_xname);

	return (0);
}

int
cmalo_init(struct ifnet *ifp)
{
	struct malo_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	/* reload the firmware if necessary */
	if (!(sc->sc_flags & MALO_FW_LOADED)) {
		/* disable interrupts */
		cmalo_intr_mask(sc, 0);

		/* load firmware */
		if (cmalo_fw_load_helper(sc) != 0)
			return (EIO);
		if (cmalo_fw_load_main(sc) != 0)
			return (EIO);
		sc->sc_flags |= MALO_FW_LOADED;

		/* enable interrupts */
		cmalo_intr_mask(sc, 1);
	}

	/* reset association state flag */
	sc->sc_flags &= ~MALO_ASSOC_FAILED;

	/* get current channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	sc->sc_curchan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
	DPRINTF(1, "%s: current channel is %d\n",
	    sc->sc_dev.dv_xname, sc->sc_curchan);

	/* setup device */
	if (cmalo_cmd_set_macctrl(sc) != 0)
		return (EIO);
	if (cmalo_cmd_set_txpower(sc, 15) != 0)
		return (EIO);
	if (cmalo_cmd_set_antenna(sc, 1) != 0)
		return (EIO);
	if (cmalo_cmd_set_antenna(sc, 2) != 0)
		return (EIO);
	if (cmalo_cmd_set_radio(sc, 1) != 0)
		return (EIO);
	if (cmalo_cmd_set_channel(sc, sc->sc_curchan) != 0)
		return (EIO);
	if (cmalo_cmd_set_rate(sc, ic->ic_fixed_rate) != 0)
		return (EIO);
	if (cmalo_cmd_set_snmp(sc, MALO_OID_RTSTRESH) != 0)
		return (EIO);
	if (cmalo_cmd_set_snmp(sc, MALO_OID_SHORTRETRY) != 0)
		return (EIO);
	if (cmalo_cmd_set_snmp(sc, MALO_OID_FRAGTRESH) != 0)
		return (EIO);
	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	if (cmalo_cmd_set_macaddr(sc, ic->ic_myaddr) != 0)
		return (EIO);
	if (sc->sc_ic.ic_flags & IEEE80211_F_WEPON) {
		if (cmalo_wep(sc) != 0)
			return (EIO);
	}

	/* device up */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* start network */
	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	if (sc->sc_flags & MALO_ASSOC_FAILED)
		ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);

	/* we are not context save anymore for FW commands */
	sc->sc_cmd_ctxsave = 0;

	return (0);
}

void
cmalo_stop(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	/* device down */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* change device back to initial state */
	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

	/* reset device */
	cmalo_cmd_set_reset(sc);
	sc->sc_flags &= ~MALO_FW_LOADED;
	ifp->if_timer = 0;

	DPRINTF(1, "%s: device down\n", sc->sc_dev.dv_xname);
}

int
cmalo_media_change(struct ifnet *ifp)
{
	int error;

	if ((error = ieee80211_media_change(ifp)) != ENETRESET)
		return (error);

	if (ifp->if_flags & (IFF_UP | IFF_RUNNING))
		cmalo_init(ifp);

	return (0);
}

int
cmalo_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct malo_softc *sc = ic->ic_if.if_softc;
	enum ieee80211_state ostate;

	ostate = ic->ic_state;

	if (ostate == nstate)
		goto out;

	switch (nstate) {
		case IEEE80211_S_INIT:
			DPRINTF(1, "%s: newstate is IEEE80211_S_INIT\n",
			    sc->sc_dev.dv_xname);
			break;
		case IEEE80211_S_SCAN:
			DPRINTF(1, "%s: newstate is IEEE80211_S_SCAN\n",
			    sc->sc_dev.dv_xname);
			cmalo_cmd_set_scan(sc);
			if (!sc->sc_net_num) {
				/* no networks found */
				DPRINTF(1, "%s: no networks found\n",
				    sc->sc_dev.dv_xname);
				break;
			}
			cmalo_select_network(sc);
			cmalo_cmd_set_auth(sc);
			cmalo_cmd_set_assoc(sc);
			break;
		case IEEE80211_S_AUTH:
			DPRINTF(1, "%s: newstate is IEEE80211_S_AUTH\n",
			    sc->sc_dev.dv_xname);
			break;
		case IEEE80211_S_ASSOC:
			DPRINTF(1, "%s: newstate is IEEE80211_S_ASSOC\n",
			    sc->sc_dev.dv_xname);
			break;
		case IEEE80211_S_RUN:
			DPRINTF(1, "%s: newstate is IEEE80211_S_RUN\n",
			    sc->sc_dev.dv_xname);
			cmalo_reflect_network(sc);
			break;
		default:
			break;
	}

out:
	return (sc->sc_newstate(ic, nstate, arg));
}

void
cmalo_detach(void *arg)
{
	struct malo_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (!(sc->sc_flags & MALO_DEVICE_ATTACHED))
		/* device was not properly attached */
		return;

	/* free command buffer */
	if (sc->sc_cmd != NULL)
		free(sc->sc_cmd, M_DEVBUF, 0);

	/* free data buffer */
	if (sc->sc_data != NULL)
		free(sc->sc_data, M_DEVBUF, 0);

	/* free firmware */
	cmalo_fw_free(sc);

	/* detach interface */
	ieee80211_ifdetach(ifp);
	if_detach(ifp);
}

int
cmalo_intr(void *arg)
{
	struct malo_softc *sc = arg;
	uint16_t intr = 0;

	/* read interrupt reason */
	intr = MALO_READ_2(sc, MALO_REG_HOST_INTR_CAUSE);
	if (intr == 0) {
		/* interrupt not for us */
		return (0);
	}
	if (intr == 0xffff) {
		/* card has been detached */
		return (0);
	}

	/* disable interrupts */
	cmalo_intr_mask(sc, 0);

	/* acknowledge interrupt */
	MALO_WRITE_2(sc, MALO_REG_HOST_INTR_CAUSE,
	    intr & MALO_VAL_HOST_INTR_MASK_ON);

	/* enable interrupts */
	cmalo_intr_mask(sc, 1);

	DPRINTF(2, "%s: interrupt handler called (intr = 0x%04x)\n",
	    sc->sc_dev.dv_xname, intr);

	if (intr & MALO_VAL_HOST_INTR_TX)
		/* TX frame sent */
		cmalo_tx_done(sc);
	if (intr & MALO_VAL_HOST_INTR_RX)
		/* RX frame received */
		cmalo_rx(sc);
	if (intr & MALO_VAL_HOST_INTR_CMD) {
		/* command response */
		wakeup(sc);
		if (!sc->sc_cmd_ctxsave)
			cmalo_cmd_response(sc);
	}
	if (intr & MALO_VAL_HOST_INTR_EVENT)
		/* event */
		cmalo_event(sc);

	return (1);
}

void
cmalo_intr_mask(struct malo_softc *sc, int enable)
{
	uint16_t val16;

	val16 = MALO_READ_2(sc, MALO_REG_HOST_INTR_MASK);

	DPRINTF(3, "%s: intr mask changed from 0x%04x ",
	    sc->sc_dev.dv_xname, val16);

	if (enable)
		MALO_WRITE_2(sc, MALO_REG_HOST_INTR_MASK,
		    val16 & ~MALO_VAL_HOST_INTR_MASK_ON);
	else
		MALO_WRITE_2(sc, MALO_REG_HOST_INTR_MASK,
		    val16 | MALO_VAL_HOST_INTR_MASK_ON);

	val16 = MALO_READ_2(sc, MALO_REG_HOST_INTR_MASK);

	DPRINTF(3, "to 0x%04x\n", val16);
}

void
cmalo_rx(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct malo_rx_desc *rxdesc;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	uint8_t *data;
	uint16_t psize;
	int i;

	splassert(IPL_NET);

	/* read the whole RX packet which is always 802.3 */
	psize = MALO_READ_2(sc, MALO_REG_DATA_READ_LEN);
	if (psize & 0x0001) {
		MALO_READ_MULTI_2(sc, MALO_REG_DATA_READ, sc->sc_data,
		    psize - 1);
		data = (uint8_t *)sc->sc_data;
		data[psize - 1] = MALO_READ_1(sc, MALO_REG_DATA_READ);
	} else 
		MALO_READ_MULTI_2(sc, MALO_REG_DATA_READ, sc->sc_data, psize);
	MALO_WRITE_1(sc, MALO_REG_HOST_STATUS, MALO_VAL_RX_DL_OVER);
	MALO_WRITE_2(sc, MALO_REG_CARD_INTR_CAUSE, MALO_VAL_RX_DL_OVER);

	/* access RX packet descriptor */
	rxdesc = (struct malo_rx_desc *)sc->sc_data;
	rxdesc->status = letoh16(rxdesc->status);
	rxdesc->pkglen = letoh16(rxdesc->pkglen);
	rxdesc->pkgoffset = letoh32(rxdesc->pkgoffset);

	DPRINTF(2, "RX status=%d, pkglen=%d, pkgoffset=%d\n",
	    rxdesc->status, rxdesc->pkglen, rxdesc->pkgoffset);

	if (rxdesc->status != MALO_RX_STATUS_OK)
		/* RX packet is not OK */
		return;

	/* remove the LLC / SNAP header */
	data = sc->sc_data + rxdesc->pkgoffset;
	i = (ETHER_ADDR_LEN * 2) + sizeof(struct llc);
	bcopy(data + i, data + (ETHER_ADDR_LEN * 2), rxdesc->pkglen - i);
	rxdesc->pkglen -= sizeof(struct llc);

	/* prepare mbuf */
	m = m_devget(sc->sc_data + rxdesc->pkgoffset,
	    rxdesc->pkglen, ETHER_ALIGN);
	if (m == NULL) {
		DPRINTF(1, "RX m_devget failed\n");
		ifp->if_ierrors++;
		return;
	}

	/* push the frame up to the network stack if not in monitor mode */
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		ml_enqueue(&ml, m);
		if_input(ifp, &ml);
#if NBPFILTER > 0
	} else {
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_IN);
#endif
	}

}

void
cmalo_start(struct ifnet *ifp)
{
	struct malo_softc *sc = ifp->if_softc;
	struct mbuf *m;

	/* don't transmit packets if interface is busy or down */
	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	m = ifq_dequeue(&ifp->if_snd);
	if (m == NULL)
		return;

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

	if (cmalo_tx(sc, m) != 0)
		ifp->if_oerrors++;
}

void
cmalo_watchdog(struct ifnet *ifp)
{
	DPRINTF(2, "watchdog timeout\n");

	/* accept TX packets again */
	ifq_clr_oactive(&ifp->if_snd);
}

int
cmalo_tx(struct malo_softc *sc, struct mbuf *m)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct malo_tx_desc *txdesc = sc->sc_data;
	uint8_t *data;
	uint16_t psize;

	splassert(IPL_NET);

	bzero(sc->sc_data, sizeof(*txdesc));
	psize = sizeof(*txdesc) + m->m_pkthdr.len;
	data = mtod(m, uint8_t *);

	/* prepare TX descriptor */
	txdesc->pkgoffset = htole32(sizeof(*txdesc));
	txdesc->pkglen = htole16(m->m_pkthdr.len);
	bcopy(data, txdesc->dstaddrhigh, sizeof(txdesc->dstaddrhigh));
	bcopy(data + sizeof(txdesc->dstaddrhigh), txdesc->dstaddrlow,
	    sizeof(txdesc->dstaddrlow));

	/* copy mbuf data to the buffer */
	m_copydata(m, 0, m->m_pkthdr.len, sc->sc_data + sizeof(*txdesc));
	m_freem(m);

	/* send TX packet to the device */
	MALO_WRITE_2(sc, MALO_REG_DATA_WRITE_LEN, psize);
	if (psize & 0x0001) {
		MALO_WRITE_MULTI_2(sc, MALO_REG_DATA_WRITE, sc->sc_data,
		    psize - 1);
		data = (uint8_t *)sc->sc_data;
		MALO_WRITE_1(sc, MALO_REG_DATA_WRITE, data[psize - 1]);
	} else
		MALO_WRITE_MULTI_2(sc, MALO_REG_DATA_WRITE, sc->sc_data, psize);
	MALO_WRITE_1(sc, MALO_REG_HOST_STATUS, MALO_VAL_TX_DL_OVER);
	MALO_WRITE_2(sc, MALO_REG_CARD_INTR_CAUSE, MALO_VAL_TX_DL_OVER);

	ifq_set_oactive(&ifp->if_snd);
	ifp->if_timer = 5;

	DPRINTF(2, "%s: TX status=%d, pkglen=%d, pkgoffset=%d\n",
	    sc->sc_dev.dv_xname, txdesc->status, letoh16(txdesc->pkglen),
	    sizeof(*txdesc));

	return (0);
}

void
cmalo_tx_done(struct malo_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	splassert(IPL_NET);

	DPRINTF(2, "%s: TX done\n", sc->sc_dev.dv_xname);

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;
	cmalo_start(ifp);
}

void
cmalo_event(struct malo_softc *sc)
{
	uint16_t event;

	/* read event reason */
	event = MALO_READ_2(sc, MALO_REG_CARD_STATUS);
	event &= MALO_VAL_CARD_STATUS_MASK;
	event = event >> 8;

	switch (event) {
	case MALO_EVENT_DEAUTH:
		DPRINTF(1, "%s: got deauthentication event (0x%04x)\n",
		    sc->sc_dev.dv_xname, event);
		/* try to associate again */
		cmalo_cmd_set_assoc(sc);
		break;
	case MALO_EVENT_DISASSOC:
		DPRINTF(1, "%s: got disassociation event (0x%04x)\n",
		    sc->sc_dev.dv_xname, event);
		/* try to associate again */
		cmalo_cmd_set_assoc(sc);
		break;
	default:
		DPRINTF(1, "%s: got unknown event (0x%04x)\n",
		    sc->sc_dev.dv_xname, event);
		break;
	}

	/* acknowledge event */
	MALO_WRITE_2(sc, MALO_REG_CARD_INTR_CAUSE, MALO_VAL_HOST_INTR_EVENT);
}

void
cmalo_select_network(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int i, best_rssi;

	/* reset last selected network */
	sc->sc_net_cur = 0;

	/* get desired network */
	if (ic->ic_des_esslen) {
		for (i = 0; i < sc->sc_net_num; i++) {
			if (!strcmp(ic->ic_des_essid, sc->sc_net[i].ssid)) {
				sc->sc_net_cur = i;
				DPRINTF(1, "%s: desired network found (%s)\n",
				    sc->sc_dev.dv_xname, ic->ic_des_essid);
				return;
			}
		}
		DPRINTF(1, "%s: desired network not found in scan results "
		    "(%s)\n",
		    sc->sc_dev.dv_xname, ic->ic_des_essid);
	}

	/* get network with best signal strength */
	best_rssi = sc->sc_net[0].rssi;
	for (i = 0; i < sc->sc_net_num; i++) {
		if (best_rssi < sc->sc_net[i].rssi) {
			best_rssi = sc->sc_net[i].rssi;
			sc->sc_net_cur = i;
		}
	}
	DPRINTF(1, "%s: best network found (%s)\n",
	    sc->sc_dev.dv_xname, sc->sc_net[sc->sc_net_cur].ssid);
}

void
cmalo_reflect_network(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t chan;

	/* reflect active network to our 80211 stack */

	/* BSSID */
	IEEE80211_ADDR_COPY(ic->ic_bss->ni_bssid,
	    sc->sc_net[sc->sc_net_cur].bssid);

	/* SSID */
	ic->ic_bss->ni_esslen = strlen(sc->sc_net[sc->sc_net_cur].ssid);
	bcopy(sc->sc_net[sc->sc_net_cur].ssid, ic->ic_bss->ni_essid,
	    ic->ic_bss->ni_esslen);

	/* channel */
	chan = sc->sc_net[sc->sc_net_cur].channel;
	ic->ic_bss->ni_chan = &ic->ic_channels[chan];
}

int
cmalo_wep(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int i;

	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		struct ieee80211_key *key = &ic->ic_nw_keys[i];

		if (!key->k_len)
			continue;

		DPRINTF(1, "%s: setting wep key for index %d\n",
		    sc->sc_dev.dv_xname, i);

		cmalo_cmd_set_wep(sc, i, key);
	}

	return (0);
}

int
cmalo_rate2bitmap(int rate)
{
	switch (rate) {
	/* CCK rates */
	case  0:	return (MALO_RATE_BITMAP_DS1);
	case  1:	return (MALO_RATE_BITMAP_DS2);
	case  2:	return (MALO_RATE_BITMAP_DS5);
	case  3:	return (MALO_RATE_BITMAP_DS11);

	/* OFDM rates */
	case  4:	return (MALO_RATE_BITMAP_OFDM6);
	case  5:	return (MALO_RATE_BITMAP_OFDM9);
	case  6:	return (MALO_RATE_BITMAP_OFDM12);
	case  7:	return (MALO_RATE_BITMAP_OFDM18);
	case  8:	return (MALO_RATE_BITMAP_OFDM24);
	case  9:	return (MALO_RATE_BITMAP_OFDM36);
	case 10:	return (MALO_RATE_BITMAP_OFDM48);
	case 11:	return (MALO_RATE_BITMAP_OFDM54);

	/* unknown rate: should not happen */
	default:	return (0);
	}
}

void
cmalo_hexdump(void *buf, int len)
{
#ifdef CMALO_DEBUG
	int i;

	if (cmalo_d >= 2) {
		for (i = 0; i < len; i++) {
			if (i % 16 == 0)
				printf("%s%5i:", i ? "\n" : "", i);
			if (i % 4 == 0)
				printf(" ");
			printf("%02x", (int)*((u_char *)buf + i));
		}
		printf("\n");
	}
#endif
}

int
cmalo_cmd_get_hwspec(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_spec *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_HWSPEC);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_spec *)(hdr + 1);

	/* set all bits for MAC address, otherwise we won't get one back */
	memset(body->macaddr, 0xff, ETHER_ADDR_LEN);

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_rsp_hwspec(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_spec *body;
	int i;

	body = (struct malo_cmd_body_spec *)(hdr + 1);

	/* get our MAC address */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		ic->ic_myaddr[i] = body->macaddr[i];

	return (0);
}

int
cmalo_cmd_set_reset(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr);

	hdr->cmd = htole16(MALO_CMD_RESET);
	hdr->size = 0;
	hdr->seqnum = htole16(1);
	hdr->result = 0;

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 1) != 0)
		return (EIO);

	/* give the device some time to finish the reset */
	delay(100);

	return (0);
}

int
cmalo_cmd_set_scan(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_scan *body;
	struct malo_cmd_tlv_ssid *body_ssid;
	struct malo_cmd_tlv_chanlist *body_chanlist;
	struct malo_cmd_tlv_rates *body_rates;
	//struct malo_cmd_tlv_numprobes *body_numprobes;
	uint16_t psize;
	int i;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_SCAN);
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_scan *)(hdr + 1);

	body->bsstype = 0x03; /* any BSS */
	memset(body->bssid, 0xff, ETHER_ADDR_LEN);

	body_ssid = sc->sc_cmd + psize;
	body_ssid->type = htole16(MALO_TLV_TYPE_SSID);
	body_ssid->size = htole16(0);
	psize += (sizeof(*body_ssid) - 1);

	body_chanlist = sc->sc_cmd + psize;
	body_chanlist->type = htole16(MALO_TLV_TYPE_CHANLIST);
	body_chanlist->size = htole16(sizeof(body_chanlist->data));
	for (i = 0; i < CHANNELS; i++) {
		body_chanlist->data[i].radiotype = 0x00;
		body_chanlist->data[i].channumber = (i + 1);
		body_chanlist->data[i].scantype = 0x00; /* active */
		body_chanlist->data[i].minscantime = htole16(0);
		body_chanlist->data[i].maxscantime = htole16(100);
	}
	psize += sizeof(*body_chanlist);

	body_rates = sc->sc_cmd + psize;
	body_rates->type = htole16(MALO_TLV_TYPE_RATES);
	body_rates->size =
	    htole16(ic->ic_sup_rates[IEEE80211_MODE_11B].rs_nrates);
	bcopy(ic->ic_sup_rates[IEEE80211_MODE_11B].rs_rates, body_rates->data,
	    ic->ic_sup_rates[IEEE80211_MODE_11B].rs_nrates);
	psize += (sizeof(*body_rates) - 1) + letoh16(body_rates->size);
#if 0
	body_numprobes = sc->sc_cmd + psize;
	body_numprobes->type = htole16(MALO_TLV_TYPE_NUMPROBES);
	body_numprobes->size = htole16(2);
	body_numprobes->numprobes = htole16(1);
	psize += sizeof(*body_numprobes);
#endif
	hdr->size = htole16(psize - sizeof(*hdr));

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_rsp_scan(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_rsp_scan *body;
	struct malo_cmd_body_rsp_scan_set *set;
	uint16_t psize;
	int i;

	bzero(sc->sc_net, sizeof(sc->sc_net));
	psize = sizeof(*hdr) + sizeof(*body);

	body = (struct malo_cmd_body_rsp_scan *)(hdr + 1);

	body->bufsize = letoh16(body->bufsize);

	DPRINTF(1, "bufsize=%d, APs=%d\n", body->bufsize, body->numofset);
	sc->sc_net_num = body->numofset;

	/* cycle through found networks */
	for (i = 0; i < body->numofset; i++) {
		set = (struct malo_cmd_body_rsp_scan_set *)(sc->sc_cmd + psize);

		set->size = letoh16(set->size);
		set->beaconintvl = letoh16(set->beaconintvl);
		set->capinfo = letoh16(set->capinfo);

		DPRINTF(1, "size=%d, bssid=%s, rssi=%d, beaconintvl=%d, "
		    "capinfo=0x%04x\n",
		    set->size, ether_sprintf(set->bssid), set->rssi,
		    set->beaconintvl, set->capinfo);

		/* save scan results */
		bcopy(set->bssid, sc->sc_net[i].bssid, sizeof(set->bssid));
		bcopy(set->timestamp, sc->sc_net[i].timestamp,
		    sizeof(set->timestamp));
		sc->sc_net[i].rssi = set->rssi;
		sc->sc_net[i].beaconintvl = set->beaconintvl;
		sc->sc_net[i].capinfo = set->capinfo;
		cmalo_parse_elements(sc, (set + 1),
		    set->size - (sizeof(*set) - sizeof(set->size)), i);

		psize += (set->size + sizeof(set->size));
	}

	return (0);
}

int
cmalo_parse_elements(struct malo_softc *sc, void *buf, int size, int pos)
{
	uint8_t eid, len;
	int i;

	DPRINTF(2, "element_size=%d, element_pos=%d\n", size, pos);

	for (i = 0; i < size; ) {
		eid = *(uint8_t *)(buf + i);
		i++;
		len = *(uint8_t *)(buf + i);
		i++;
		DPRINTF(2, "eid=%d, len=%d, ", eid, len);

		switch (eid) {
		case IEEE80211_ELEMID_SSID:
			bcopy(buf + i, sc->sc_net[pos].ssid, len);
			DPRINTF(2, "ssid=%s\n", sc->sc_net[pos].ssid);
			break;
		case IEEE80211_ELEMID_RATES:
			bcopy(buf + i, sc->sc_net[pos].rates, len);
			DPRINTF(2, "rates\n");
			break;
		case IEEE80211_ELEMID_DSPARMS:
			sc->sc_net[pos].channel = *(uint8_t *)(buf + i);
			DPRINTF(2, "chnl=%d\n", sc->sc_net[pos].channel);
			break;
		default:
			DPRINTF(2, "unknown\n");
			break;
		}

		i += len;
	}

	return (0);
}

int
cmalo_cmd_set_auth(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_auth *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_AUTH);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_auth *)(hdr + 1);

	bcopy(sc->sc_net[sc->sc_net_cur].bssid, body->peermac, ETHER_ADDR_LEN);
	body->authtype = 0;

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_wep(struct malo_softc *sc, uint16_t index,
    struct ieee80211_key *key)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_wep *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_WEP);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_wep *)(hdr + 1);

	body->action = htole16(MALO_WEP_ACTION_TYPE_ADD);
	body->key_index = htole16(index);

	if (body->key_index == 0) {
		if (key->k_len > 5)
			body->key_type_1 = MALO_WEP_KEY_TYPE_104BIT;
		else
			body->key_type_1 = MALO_WEP_KEY_TYPE_40BIT;
		bcopy(key->k_key, body->key_value_1, key->k_len);
	}
	if (body->key_index == 1) {
		if (key->k_len > 5)
			body->key_type_2 = MALO_WEP_KEY_TYPE_104BIT;
		else
			body->key_type_2 = MALO_WEP_KEY_TYPE_40BIT;
		bcopy(key->k_key, body->key_value_2, key->k_len);
	}
	if (body->key_index == 2) {
		if (key->k_len > 5)
			body->key_type_3 = MALO_WEP_KEY_TYPE_104BIT;
		else
			body->key_type_3 = MALO_WEP_KEY_TYPE_40BIT;
		bcopy(key->k_key, body->key_value_3, key->k_len);
	}
	if (body->key_index == 3) {
		if (key->k_len > 5)
			body->key_type_4 = MALO_WEP_KEY_TYPE_104BIT;
		else
			body->key_type_4 = MALO_WEP_KEY_TYPE_40BIT;
		bcopy(key->k_key, body->key_value_4, key->k_len);
	}

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_snmp(struct malo_softc *sc, uint16_t oid)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_snmp *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_SNMP);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_snmp *)(hdr + 1);

	body->action = htole16(1);

	switch (oid) {
	case MALO_OID_RTSTRESH:
		body->oid = htole16(MALO_OID_RTSTRESH);
		body->size = htole16(2);
		*(uint16_t *)body->data = htole16(2347);
		break;
	case MALO_OID_SHORTRETRY:
		body->oid = htole16(MALO_OID_SHORTRETRY);
		body->size = htole16(2);
		*(uint16_t *)body->data = htole16(4);
		break;
	case MALO_OID_FRAGTRESH:
		body->oid = htole16(MALO_OID_FRAGTRESH);
		body->size = htole16(2);
		*(uint16_t *)body->data = htole16(2346);
		break;
	case MALO_OID_80211D:
		body->oid = htole16(MALO_OID_80211D);
		body->size = htole16(2);
		*(uint16_t *)body->data = htole16(1);
		break;
	default:
		break;
	}

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_radio(struct malo_softc *sc, uint16_t control)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_radio *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_RADIO);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_radio *)(hdr + 1);

	body->action = htole16(1);

	if (control) {
		body->control  = htole16(MALO_CMD_RADIO_ON);
		body->control |= htole16(MALO_CMD_RADIO_AUTO_P);
	}

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_channel(struct malo_softc *sc, uint16_t channel)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_channel *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_CHANNEL);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_channel *)(hdr + 1);

	body->action = htole16(1);
	body->channel = htole16(channel);

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}


int
cmalo_cmd_set_txpower(struct malo_softc *sc, int16_t txpower)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_txpower *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_TXPOWER);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_txpower *)(hdr + 1);

	body->action = htole16(1);
	body->txpower = htole16(txpower);

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_antenna(struct malo_softc *sc, uint16_t action)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_antenna *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_ANTENNA);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_antenna *)(hdr + 1);

	/* 1 = set RX, 2 = set TX */
	body->action = htole16(action);

	if (action == 1)
		/* set RX antenna */
		body->antenna_mode = htole16(0xffff);
	if (action == 2)
		/* set TX antenna */
		body->antenna_mode = htole16(2);

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_macctrl(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_macctrl *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_MACCTRL);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_macctrl *)(hdr + 1);

	body->action  = htole16(MALO_CMD_MACCTRL_RX_ON);
	body->action |= htole16(MALO_CMD_MACCTRL_TX_ON);
	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		body->action |= htole16(MALO_CMD_MACCTRL_PROMISC_ON);

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_macaddr(struct malo_softc *sc, uint8_t *macaddr)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_macaddr *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_MACADDR);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_macaddr *)(hdr + 1);

	body->action = htole16(1);
	bcopy(macaddr, body->macaddr, ETHER_ADDR_LEN);

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_assoc(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_assoc *body;
	struct malo_cmd_tlv_ssid *body_ssid;
	struct malo_cmd_tlv_phy *body_phy;
	struct malo_cmd_tlv_cf *body_cf;
	struct malo_cmd_tlv_rates *body_rates;
	struct malo_cmd_tlv_passeid *body_passeid;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_ASSOC);
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_assoc *)(hdr + 1);

	bcopy(sc->sc_net[sc->sc_net_cur].bssid, body->peermac, ETHER_ADDR_LEN);
	body->capinfo = htole16(sc->sc_net[sc->sc_net_cur].capinfo);
	body->listenintrv = htole16(10);

	body_ssid = sc->sc_cmd + psize;
	body_ssid->type = htole16(MALO_TLV_TYPE_SSID);
	body_ssid->size = htole16(strlen(sc->sc_net[sc->sc_net_cur].ssid));
	bcopy(sc->sc_net[sc->sc_net_cur].ssid, body_ssid->data,
	    letoh16(body_ssid->size));
	psize += (sizeof(*body_ssid) - 1) + letoh16(body_ssid->size);

	body_phy = sc->sc_cmd + psize;
	body_phy->type = htole16(MALO_TLV_TYPE_PHY);
	body_phy->size = htole16(1);
	bcopy(&sc->sc_net[sc->sc_net_cur].channel, body_phy->data, 1);
	psize += sizeof(*body_phy);

	body_cf = sc->sc_cmd + psize;
	body_cf->type = htole16(MALO_TLV_TYPE_CF);
	body_cf->size = htole16(0);
	psize += (sizeof(*body_cf) - 1);

	body_rates = sc->sc_cmd + psize;
	body_rates->type = htole16(MALO_TLV_TYPE_RATES);
	body_rates->size = htole16(strlen(sc->sc_net[sc->sc_net_cur].rates));
	bcopy(sc->sc_net[sc->sc_net_cur].rates, body_rates->data,
	    letoh16(body_rates->size));
	psize += (sizeof(*body_rates) - 1) + letoh16(body_rates->size);

	/* hack to correct FW's wrong generated rates-element-id */
	body_passeid = sc->sc_cmd + psize;
	body_passeid->type = htole16(MALO_TLV_TYPE_PASSEID);
	body_passeid->size = body_rates->size;
	bcopy(body_rates->data, body_passeid->data, letoh16(body_rates->size));
	psize += (sizeof(*body_passeid) - 1) + letoh16(body_passeid->size);

	hdr->size = htole16(psize - sizeof(*hdr));

	/* process command request */
	if (!sc->sc_cmd_ctxsave) {
		if (cmalo_cmd_request(sc, psize, 1) != 0)
			return (EIO);
		return (0);
	}
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_rsp_assoc(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_rsp_assoc *body;

	body = (struct malo_cmd_body_rsp_assoc *)(hdr + 1);

	if (body->status) {
		DPRINTF(1, "%s: association failed (status %d)\n",
		    sc->sc_dev.dv_xname, body->status);
		sc->sc_flags |= MALO_ASSOC_FAILED;
	} else
		DPRINTF(1, "%s: association successful\n",
		    sc->sc_dev.dv_xname, body->status);

	return (0);
}

int
cmalo_cmd_set_80211d(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_80211d *body;
	struct malo_cmd_tlv_80211d *body_80211d;
	uint16_t psize;
	int i;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_80211D);
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_80211d *)(hdr + 1);

	body->action = htole16(1);

	body_80211d = sc->sc_cmd + psize;
	body_80211d->type = htole16(MALO_TLV_TYPE_80211D);
	body_80211d->size = htole16(sizeof(body_80211d->data) +
	    sizeof(body_80211d->countrycode));
	bcopy("EU ", body_80211d->countrycode,
	    sizeof(body_80211d->countrycode));
	for (i = 0; i < CHANNELS; i++) {
		body_80211d->data[i].firstchannel = 1;
		body_80211d->data[i].numchannels = 12;
		body_80211d->data[i].maxtxpower = 10;
	}
	psize += sizeof(*body_80211d);

	hdr->size = htole16(psize - sizeof(*hdr));
	
	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_bgscan_config(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_bgscan_config *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_BGSCAN_CONFIG);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_bgscan_config *)(hdr + 1);

	body->action = htole16(1);
	body->enable = 1;
	body->bsstype = 0x03;
	body->chperscan = 12;
	body->scanintvl = htole32(100);
	body->maxscanres = htole16(12);

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_bgscan_query(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_bgscan_query *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_BGSCAN_QUERY);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_bgscan_query *)(hdr + 1);

	body->flush = 0;

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_set_rate(struct malo_softc *sc, int rate)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	struct malo_cmd_body_rate *body;
	uint16_t psize;

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);
	psize = sizeof(*hdr) + sizeof(*body);

	hdr->cmd = htole16(MALO_CMD_RATE);
	hdr->size = htole16(sizeof(*body));
	hdr->seqnum = htole16(1);
	hdr->result = 0;
	body = (struct malo_cmd_body_rate *)(hdr + 1);

	body->action = htole16(1);
	if (rate == -1) {
 		body->hwauto = htole16(1);
		body->ratebitmap = htole16(MALO_RATE_BITMAP_AUTO);
	} else {
 		body->hwauto = 0;
		body->ratebitmap = htole16(cmalo_rate2bitmap(rate));
	}

	/* process command request */
	if (cmalo_cmd_request(sc, psize, 0) != 0)
		return (EIO);

	/* process command response */
	cmalo_cmd_response(sc);

	return (0);
}

int
cmalo_cmd_request(struct malo_softc *sc, uint16_t psize, int no_response)
{
	uint8_t *cmd;

	cmalo_hexdump(sc->sc_cmd, psize);

	/* send command request */
	MALO_WRITE_2(sc, MALO_REG_CMD_WRITE_LEN, psize);
	if (psize & 0x0001) {
		MALO_WRITE_MULTI_2(sc, MALO_REG_CMD_WRITE, sc->sc_cmd,
		    psize - 1);
		cmd = (uint8_t *)sc->sc_cmd;
		MALO_WRITE_1(sc, MALO_REG_CMD_WRITE, cmd[psize - 1]);
	} else
		MALO_WRITE_MULTI_2(sc, MALO_REG_CMD_WRITE, sc->sc_cmd, psize);
	MALO_WRITE_1(sc, MALO_REG_HOST_STATUS, MALO_VAL_CMD_DL_OVER);
	MALO_WRITE_2(sc, MALO_REG_CARD_INTR_CAUSE, MALO_VAL_CMD_DL_OVER);

	if (no_response)
		/* we don't expect a response */
		return (0);

	/* wait for the command response */
	if (tsleep_nsec(sc, 0, "malocmd", SEC_TO_NSEC(5))) {
		printf("%s: timeout while waiting for cmd response\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}

	return (0);
}

int
cmalo_cmd_response(struct malo_softc *sc)
{
	struct malo_cmd_header *hdr = sc->sc_cmd;
	uint16_t psize;
	uint8_t *cmd;
	int s;

	s = splnet();

	bzero(sc->sc_cmd, MALO_CMD_BUFFER_SIZE);

	/* read the whole command response */
	psize = MALO_READ_2(sc, MALO_REG_CMD_READ_LEN);
	if (psize & 0x0001) {
		MALO_READ_MULTI_2(sc, MALO_REG_CMD_READ, sc->sc_cmd,
		    psize - 1);
		cmd = (uint8_t *)sc->sc_cmd;
		cmd[psize - 1] = MALO_READ_1(sc, MALO_REG_CMD_READ);
	} else
		MALO_READ_MULTI_2(sc, MALO_REG_CMD_READ, sc->sc_cmd, psize);

	cmalo_hexdump(sc->sc_cmd, psize);

	/*
	 * We convert the header values into the machines correct endianness,
	 * so we don't have to letoh16() all over the code.  The body is
	 * kept in the cards order, little endian.  We need to take care
	 * about the body endianness in the corresponding response routines.
	 */
	hdr->cmd = letoh16(hdr->cmd);
	hdr->size = letoh16(hdr->size);
	hdr->seqnum = letoh16(hdr->seqnum);
	hdr->result = letoh16(hdr->result);

	/* check for a valid command response */
	if (!(hdr->cmd & MALO_CMD_RESP)) {
		printf("%s: got invalid command response (0x%04x)\n",
		    sc->sc_dev.dv_xname, hdr->cmd);
		splx(s);
		return (EIO);
	}
	hdr->cmd &= ~MALO_CMD_RESP;

	/* association cmd response is special */
	if (hdr->cmd == 0x0012)
		hdr->cmd = MALO_CMD_ASSOC;

	/* to which command does the response belong */
	switch (hdr->cmd) {
	case MALO_CMD_HWSPEC:
		DPRINTF(1, "%s: got hwspec cmd response\n",
		    sc->sc_dev.dv_xname);
		cmalo_cmd_rsp_hwspec(sc);
		break;
	case MALO_CMD_RESET:
		/* reset will not send back a response */
		break;
	case MALO_CMD_SCAN:
		DPRINTF(1, "%s: got scan cmd response\n",
		    sc->sc_dev.dv_xname);
		cmalo_cmd_rsp_scan(sc);
		break;
	case MALO_CMD_AUTH:
		/* do nothing */
		DPRINTF(1, "%s: got auth cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_WEP:
		/* do nothing */
		DPRINTF(1, "%s: got wep cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_SNMP:
		/* do nothing */
		DPRINTF(1, "%s: got snmp cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_RADIO:
		/* do nothing */
		DPRINTF(1, "%s: got radio cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_CHANNEL:
		/* do nothing */
		DPRINTF(1, "%s: got channel cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_TXPOWER:
		/* do nothing */
		DPRINTF(1, "%s: got txpower cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_ANTENNA:
		/* do nothing */
		DPRINTF(1, "%s: got antenna cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_MACCTRL:
		/* do nothing */
		DPRINTF(1, "%s: got macctrl cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_MACADDR:
		/* do nothing */
		DPRINTF(1, "%s: got macaddr cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_ASSOC:
		/* do nothing */
		DPRINTF(1, "%s: got assoc cmd response\n",
		    sc->sc_dev.dv_xname);
		cmalo_cmd_rsp_assoc(sc);
		break;
	case MALO_CMD_80211D:
		/* do nothing */
		DPRINTF(1, "%s: got 80211d cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_BGSCAN_CONFIG:
		/* do nothing */
		DPRINTF(1, "%s: got bgscan config cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_BGSCAN_QUERY:
		/* do nothing */
		DPRINTF(1, "%s: got bgscan query cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	case MALO_CMD_RATE:
		/* do nothing */
		DPRINTF(1, "%s: got rate cmd response\n",
		    sc->sc_dev.dv_xname);
		break;
	default:
		printf("%s: got unknown cmd response (0x%04x)\n",
		    sc->sc_dev.dv_xname, hdr->cmd);
		break;
	}

	splx(s);

	return (0);
}
