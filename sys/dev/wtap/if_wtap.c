/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Monthadar Al Jaberi, TerraNet AB
 * All rights reserved.
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include "if_wtapvar.h"
#include <sys/uio.h>    /* uio struct */
#include <sys/jail.h>
#include <net/if_var.h>
#include <net/vnet.h>

#include <net80211/ieee80211_ratectl.h>
#include "if_medium.h"

/*
 * This _requires_ vimage to be useful.
 */
#ifndef	VIMAGE
#error	if_wtap requires VIMAGE.
#endif	/* VIMAGE */

/* device for IOCTL and read/write for debuggin purposes */
/* Function prototypes */
static	d_open_t	wtap_node_open;
static	d_close_t	wtap_node_close;
static	d_write_t	wtap_node_write;
static	d_ioctl_t	wtap_node_ioctl;

static struct cdevsw wtap_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open = 	wtap_node_open,
	.d_close = 	wtap_node_close,
	.d_write = 	wtap_node_write,
	.d_ioctl =	wtap_node_ioctl,
	.d_name =	"wtapnode",
};

static int
wtap_node_open(struct cdev *dev, int oflags, int devtype, struct thread *p)
{

	int err = 0;
	uprintf("Opened device \"echo\" successfully.\n");
	return(err);
}

static int
wtap_node_close(struct cdev *dev, int fflag, int devtype, struct thread *p)
{

	uprintf("Closing device \"echo.\"\n");
	return(0);
}

static int
wtap_node_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	int err = 0;
	struct mbuf *m;
	struct ifnet *ifp;
	struct wtap_softc *sc;
	uint8_t buf[1024];
	struct epoch_tracker et;
	int buf_len;

	uprintf("write device %s \"echo.\"\n", devtoname(dev));
	buf_len = MIN(uio->uio_iov->iov_len, 1024);
	err = copyin(uio->uio_iov->iov_base, buf, buf_len);

	if (err != 0) {
		uprintf("Write failed: bad address!\n");
		return (err);
	}

	MGETHDR(m, M_NOWAIT, MT_DATA);
	m_copyback(m, 0, buf_len, buf);

	CURVNET_SET(TD_TO_VNET(curthread));
	NET_EPOCH_ENTER(et);

	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		printf("ifp->if_xname = %s\n", ifp->if_xname);
		if(strcmp(devtoname(dev), ifp->if_xname) == 0){
			printf("found match, correspoding wtap = %s\n",
			    ifp->if_xname);
			sc = (struct wtap_softc *)ifp->if_softc;
			printf("wtap id = %d\n", sc->id);
			wtap_inject(sc, m);
		}
	}

	NET_EPOCH_EXIT(et);
	CURVNET_RESTORE();

	return(err);
}

int
wtap_node_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
    int fflag, struct thread *td)
{
	int error = 0;

	switch(cmd) {
	default:
		DWTAP_PRINTF("Unknown WTAP IOCTL\n");
		error = EINVAL;
	}
	return error;
}

static int wtap_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_bpf_params *params);

static int
wtap_medium_enqueue(struct wtap_vap *avp, struct mbuf *m)
{

	return medium_transmit(avp->av_md, avp->id, m);
}

static int
wtap_media_change(struct ifnet *ifp)
{

	DWTAP_PRINTF("%s\n", __func__);
	int error = ieee80211_media_change(ifp);
	/* NB: only the fixed rate can change and that doesn't need a reset */
	return (error == ENETRESET ? 0 : error);
}

/*
 * Intercept management frames to collect beacon rssi data
 * and to do ibss merges.
 */
static void
wtap_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m,
    int subtype, const struct ieee80211_rx_stats *stats, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
#if 0
	DWTAP_PRINTF("[%d] %s\n", myath_id(ni), __func__);
#endif
	WTAP_VAP(vap)->av_recv_mgmt(ni, m, subtype, stats, rssi, nf);
}

static int
wtap_reset_vap(struct ieee80211vap *vap, u_long cmd)
{

	DWTAP_PRINTF("%s\n", __func__);
	return 0;
}

static void
wtap_beacon_update(struct ieee80211vap *vap, int item)
{
	struct ieee80211_beacon_offsets *bo = &vap->iv_bcn_off;

	DWTAP_PRINTF("%s\n", __func__);
	setbit(bo->bo_flags, item);
}

/*
 * Allocate and setup an initial beacon frame.
 */
static int
wtap_beacon_alloc(struct wtap_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct wtap_vap *avp = WTAP_VAP(vap);

	DWTAP_PRINTF("[%s] %s\n", ether_sprintf(ni->ni_macaddr), __func__);

	/*
	 * NB: the beacon data buffer must be 32-bit aligned;
	 * we assume the mbuf routines will return us something
	 * with this alignment (perhaps should assert).
	 */
	avp->beacon = ieee80211_beacon_alloc(ni);
	if (avp->beacon == NULL) {
		printf("%s: cannot get mbuf\n", __func__);
		return ENOMEM;
	}
	callout_init(&avp->av_swba, 0);
	avp->bf_node = ieee80211_ref_node(ni);

	return 0;
}

static void
wtap_beacon_config(struct wtap_softc *sc, struct ieee80211vap *vap)
{

	DWTAP_PRINTF("%s\n", __func__);
}

static void
wtap_beacon_intrp(void *arg)
{
	struct wtap_vap *avp = arg;
	struct ieee80211vap *vap = arg;
	struct mbuf *m;

	if (vap->iv_state < IEEE80211_S_RUN) {
	    DWTAP_PRINTF("Skip beacon, not running, state %d", vap->iv_state);
	    return ;
	}
	DWTAP_PRINTF("[%d] beacon intrp\n", avp->id);	//burst mode
	/*
	 * Update dynamic beacon contents.  If this returns
	 * non-zero then we need to remap the memory because
	 * the beacon frame changed size (probably because
	 * of the TIM bitmap).
	 */
	m = m_dup(avp->beacon, M_NOWAIT);
	if (ieee80211_beacon_update(avp->bf_node, m, 0)) {
		printf("%s, need to remap the memory because the beacon frame"
		    " changed size.\n",__func__);
	}

	if (ieee80211_radiotap_active_vap(vap))
	    ieee80211_radiotap_tx(vap, m);

#if 0
	medium_transmit(avp->av_md, avp->id, m);
#endif
	wtap_medium_enqueue(avp, m);
	callout_schedule(&avp->av_swba, avp->av_bcinterval);
}

static int
wtap_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct wtap_softc *sc = ic->ic_softc;
	struct wtap_vap *avp = WTAP_VAP(vap);
	struct ieee80211_node *ni = NULL;
	int error;

	DWTAP_PRINTF("%s\n", __func__);

	ni = ieee80211_ref_node(vap->iv_bss);
	/*
	 * Invoke the parent method to do net80211 work.
	 */
	error = avp->av_newstate(vap, nstate, arg);
	if (error != 0)
		goto bad;

	if (nstate == IEEE80211_S_RUN) {
		/* NB: collect bss node again, it may have changed */
		ieee80211_free_node(ni);
		ni = ieee80211_ref_node(vap->iv_bss);
		switch (vap->iv_opmode) {
		case IEEE80211_M_MBSS:
			error = wtap_beacon_alloc(sc, ni);
			if (error != 0)
				goto bad;
			wtap_beacon_config(sc, vap);
			callout_reset(&avp->av_swba, avp->av_bcinterval,
			    wtap_beacon_intrp, vap);
			break;
		default:
			goto bad;
		}
	} else if (nstate == IEEE80211_S_INIT) {
		callout_stop(&avp->av_swba);
	}
	ieee80211_free_node(ni);
	return 0;
bad:
	printf("%s: bad\n", __func__);
	ieee80211_free_node(ni);
	return error;
}

static void
wtap_bmiss(struct ieee80211vap *vap)
{
	struct wtap_vap *avp = (struct wtap_vap *)vap;

	DWTAP_PRINTF("%s\n", __func__);
	avp->av_bmiss(vap);
}

static struct ieee80211vap *
wtap_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ],
    int unit, enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	 struct wtap_softc *sc = ic->ic_softc;
	 struct ieee80211vap *vap;
	 struct wtap_vap *avp;
	 int error;
	struct ieee80211_node *ni;

	 DWTAP_PRINTF("%s\n", __func__);

	avp = malloc(sizeof(struct wtap_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	avp->id = sc->id;
	avp->av_md = sc->sc_md;
	avp->av_bcinterval = msecs_to_ticks(BEACON_INTRERVAL + 100*sc->id);
	vap = (struct ieee80211vap *) avp;
	error = ieee80211_vap_setup(ic, vap, name, unit, IEEE80211_M_MBSS,
	    flags | IEEE80211_CLONE_NOBEACONS, bssid);
	if (error) {
		free(avp, M_80211_VAP);
		return (NULL);
	}

	/* override various methods */
	avp->av_recv_mgmt = vap->iv_recv_mgmt;
	vap->iv_recv_mgmt = wtap_recv_mgmt;
	vap->iv_reset = wtap_reset_vap;
	vap->iv_update_beacon = wtap_beacon_update;
	avp->av_newstate = vap->iv_newstate;
	vap->iv_newstate = wtap_newstate;
	avp->av_bmiss = vap->iv_bmiss;
	vap->iv_bmiss = wtap_bmiss;

	/* complete setup */
	ieee80211_vap_attach(vap, wtap_media_change, ieee80211_media_status,
	    mac);
	avp->av_dev = make_dev(&wtap_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    "%s", (const char *)sc->name);

	/* TODO this is a hack to force it to choose the rate we want */
	ni = ieee80211_ref_node(vap->iv_bss);
	ni->ni_txrate = 130;
	ieee80211_free_node(ni);
	return vap;
}

static void
wtap_vap_delete(struct ieee80211vap *vap)
{
	struct wtap_vap *avp = WTAP_VAP(vap);

	DWTAP_PRINTF("%s\n", __func__);
	destroy_dev(avp->av_dev);
	callout_stop(&avp->av_swba);
	ieee80211_vap_detach(vap);
	free(avp, M_80211_VAP);
}

static void
wtap_parent(struct ieee80211com *ic)
{
	struct wtap_softc *sc = ic->ic_softc;

	if (ic->ic_nrunning > 0) {
		sc->up = 1;
		ieee80211_start_all(ic);
	} else
		sc->up = 0;
}

static void
wtap_scan_start(struct ieee80211com *ic)
{

#if 0
	DWTAP_PRINTF("%s\n", __func__);
#endif
}

static void
wtap_scan_end(struct ieee80211com *ic)
{

#if 0
	DWTAP_PRINTF("%s\n", __func__);
#endif
}

static void
wtap_set_channel(struct ieee80211com *ic)
{

#if 0
	DWTAP_PRINTF("%s\n", __func__);
#endif
}

static int
wtap_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_bpf_params *params)
{
#if 0
	DWTAP_PRINTF("%s, %p\n", __func__, m);
#endif
	struct ieee80211vap	*vap = ni->ni_vap;
	struct wtap_vap 	*avp = WTAP_VAP(vap);

	if (ieee80211_radiotap_active_vap(vap)) {
		ieee80211_radiotap_tx(vap, m);
	}
	if (m->m_flags & M_TXCB)
		ieee80211_process_callback(ni, m, 0);
	ieee80211_free_node(ni);
	return wtap_medium_enqueue(avp, m);
}

void
wtap_inject(struct wtap_softc *sc, struct mbuf *m)
{
      struct wtap_buf *bf = (struct wtap_buf *)malloc(sizeof(struct wtap_buf),
          M_WTAP_RXBUF, M_NOWAIT | M_ZERO);
      KASSERT(bf != NULL, ("could not allocated a new wtap_buf\n"));
      bf->m = m;

      mtx_lock(&sc->sc_mtx);
      STAILQ_INSERT_TAIL(&sc->sc_rxbuf, bf, bf_list);
      taskqueue_enqueue(sc->sc_tq, &sc->sc_rxtask);
      mtx_unlock(&sc->sc_mtx);
}

void
wtap_rx_deliver(struct wtap_softc *sc, struct mbuf *m)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	int type;
#if 0
	DWTAP_PRINTF("%s\n", __func__);
#endif

	DWTAP_PRINTF("[%d] receiving m=%p\n", sc->id, m);
	if (m == NULL) {		/* NB: shouldn't happen */
		ic_printf(ic, "%s: no mbuf!\n", __func__);
	}

	ieee80211_dump_pkt(ic, mtod(m, caddr_t), 0,0,0);

	/*
	  * Locate the node for sender, track state, and then
	  * pass the (referenced) node up to the 802.11 layer
	  * for its use.
	  */
	ni = ieee80211_find_rxnode_withkey(ic,
	    mtod(m, const struct ieee80211_frame_min *),IEEE80211_KEYIX_NONE);
	if (ni != NULL) {
		/*
		 * Sending station is known, dispatch directly.
		 */
		type = ieee80211_input(ni, m, 1<<7, 10);
		ieee80211_free_node(ni);
	} else {
		type = ieee80211_input_all(ic, m, 1<<7, 10);
	}
}

static void
wtap_rx_proc(void *arg, int npending)
{
	struct wtap_softc *sc = (struct wtap_softc *)arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *m;
	struct ieee80211_node *ni;
	int type;
	struct wtap_buf *bf;

#if 0
	DWTAP_PRINTF("%s\n", __func__);
#endif

	for(;;) {
		mtx_lock(&sc->sc_mtx);
		bf = STAILQ_FIRST(&sc->sc_rxbuf);
		if (bf == NULL) {
			mtx_unlock(&sc->sc_mtx);
			return;
		}
		STAILQ_REMOVE_HEAD(&sc->sc_rxbuf, bf_list);
		mtx_unlock(&sc->sc_mtx);
		KASSERT(bf != NULL, ("wtap_buf is NULL\n"));
		m = bf->m;
		DWTAP_PRINTF("[%d] receiving m=%p\n", sc->id, bf->m);
		if (m == NULL) {		/* NB: shouldn't happen */
			ic_printf(ic, "%s: no mbuf!\n", __func__);
			free(bf, M_WTAP_RXBUF);
			return;
		}
#if 0
		ieee80211_dump_pkt(ic, mtod(m, caddr_t), 0,0,0);
#endif

		/*
		 * Locate the node for sender, track state, and then
		 * pass the (referenced) node up to the 802.11 layer
		 * for its use.
		 */
		ni = ieee80211_find_rxnode_withkey(ic,
		    mtod(m, const struct ieee80211_frame_min *),
		    IEEE80211_KEYIX_NONE);
		if (ni != NULL) {
			/*
			 * Sending station is known, dispatch directly.
			 */
			type = ieee80211_input(ni, m, 1<<7, 10);
			ieee80211_free_node(ni);
		} else {
			type = ieee80211_input_all(ic, m, 1<<7, 10);
		}
		
		/* The mbufs are freed by the Net80211 stack */
		free(bf, M_WTAP_RXBUF);
	}
}

static void
wtap_newassoc(struct ieee80211_node *ni, int isnew)
{

	DWTAP_PRINTF("%s\n", __func__);
}

/*
 * Callback from the 802.11 layer to update WME parameters.
 */
static int
wtap_wme_update(struct ieee80211com *ic)
{

	DWTAP_PRINTF("%s\n", __func__);
	return 0;
}

static void
wtap_update_mcast(struct ieee80211com *ic)
{

	DWTAP_PRINTF("%s\n", __func__);
}

static void
wtap_update_promisc(struct ieee80211com *ic)
{

	DWTAP_PRINTF("%s\n", __func__);
}

static int
wtap_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct ieee80211_node *ni =
	    (struct ieee80211_node *) m->m_pkthdr.rcvif;
	struct ieee80211vap *vap = ni->ni_vap;
	struct wtap_vap *avp = WTAP_VAP(vap);

	if(ni == NULL){
		printf("m->m_pkthdr.rcvif is NULL we cant radiotap_tx\n");
	}else{
		if (ieee80211_radiotap_active_vap(vap))
			ieee80211_radiotap_tx(vap, m);
	}
	if (m->m_flags & M_TXCB)
		ieee80211_process_callback(ni, m, 0);
	ieee80211_free_node(ni);
	return wtap_medium_enqueue(avp, m);
}

static struct ieee80211_node *
wtap_node_alloc(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct ieee80211_node *ni;

	DWTAP_PRINTF("%s\n", __func__);

	ni = malloc(sizeof(struct ieee80211_node), M_80211_NODE,
	    M_NOWAIT|M_ZERO);
	if (ni == NULL)
		return (NULL);

	ni->ni_txrate = 130;
	return ni;
}

static void
wtap_node_free(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct wtap_softc *sc = ic->ic_softc;

	DWTAP_PRINTF("%s\n", __func__);
	sc->sc_node_free(ni);
}

int32_t
wtap_attach(struct wtap_softc *sc, const uint8_t *macaddr)
{
	struct ieee80211com *ic = &sc->sc_ic;

	DWTAP_PRINTF("%s\n", __func__);

	sc->up = 0;
	STAILQ_INIT(&sc->sc_rxbuf);
	sc->sc_tq = taskqueue_create("wtap_taskq", M_NOWAIT | M_ZERO,
	    taskqueue_thread_enqueue, &sc->sc_tq);
	taskqueue_start_threads(&sc->sc_tq, 1, PI_SOFT, "%s taskQ", sc->name);
	TASK_INIT(&sc->sc_rxtask, 0, wtap_rx_proc, sc);

	ic->ic_softc = sc;
	ic->ic_name = sc->name;
	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_MBSS;
	ic->ic_caps = IEEE80211_C_MBSS;

	ic->ic_max_keyix = 128; /* A value read from Atheros ATH_KEYMAX */

	ic->ic_regdomain.regdomain = SKU_ETSI;
	ic->ic_regdomain.country = CTRY_SWEDEN;
	ic->ic_regdomain.location = 1; /* Indoors */
	ic->ic_regdomain.isocc[0] = 'S';
	ic->ic_regdomain.isocc[1] = 'E';

	ic->ic_nchans = 1;
	ic->ic_channels[0].ic_flags = IEEE80211_CHAN_B;
	ic->ic_channels[0].ic_freq = 2412;

	IEEE80211_ADDR_COPY(ic->ic_macaddr, macaddr);
	ieee80211_ifattach(ic);

	/* override default methods */
	ic->ic_newassoc = wtap_newassoc;
	ic->ic_wme.wme_update = wtap_wme_update;
	ic->ic_vap_create = wtap_vap_create;
	ic->ic_vap_delete = wtap_vap_delete;
	ic->ic_raw_xmit = wtap_raw_xmit;
	ic->ic_update_mcast = wtap_update_mcast;
	ic->ic_update_promisc = wtap_update_promisc;
	ic->ic_transmit = wtap_transmit;
	ic->ic_parent = wtap_parent;

	sc->sc_node_alloc = ic->ic_node_alloc;
	ic->ic_node_alloc = wtap_node_alloc;
	sc->sc_node_free = ic->ic_node_free;
	ic->ic_node_free = wtap_node_free;

	ic->ic_scan_start = wtap_scan_start;
	ic->ic_scan_end = wtap_scan_end;
	ic->ic_set_channel = wtap_set_channel;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_tx_th.wt_ihdr, sizeof(sc->sc_tx_th),
	    WTAP_TX_RADIOTAP_PRESENT,
	    &sc->sc_rx_th.wr_ihdr, sizeof(sc->sc_rx_th),
	    WTAP_RX_RADIOTAP_PRESENT);

	/* Work here, we must find a way to populate the rate table */
#if 0
	if(ic->ic_rt == NULL){
		printf("no table for ic_curchan\n");
		ic->ic_rt = ieee80211_get_ratetable(&ic->ic_channels[0]);
	}
	printf("ic->ic_rt =%p\n", ic->ic_rt);
	printf("rate count %d\n", ic->ic_rt->rateCount);

	uint8_t code = ic->ic_rt->info[0].dot11Rate;
	uint8_t cix = ic->ic_rt->info[0].ctlRateIndex;
	uint8_t ctl_rate = ic->ic_rt->info[cix].dot11Rate;
	printf("code=%d, cix=%d, ctl_rate=%d\n", code, cix, ctl_rate);

	uint8_t rix0 = ic->ic_rt->rateCodeToIndex[130];
	uint8_t rix1 = ic->ic_rt->rateCodeToIndex[132];
	uint8_t rix2 = ic->ic_rt->rateCodeToIndex[139];
	uint8_t rix3 = ic->ic_rt->rateCodeToIndex[150];
	printf("rix0 %u,rix1 %u,rix2 %u,rix3 %u\n", rix0,rix1,rix2,rix3);
	printf("lpAckDuration=%u\n", ic->ic_rt->info[0].lpAckDuration);
	printf("rate=%d\n", ic->ic_rt->info[0].rateKbps);
#endif
	return 0;
}

int32_t
wtap_detach(struct wtap_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	DWTAP_PRINTF("%s\n", __func__);
	ieee80211_ageq_drain(&ic->ic_stageq);
	ieee80211_ifdetach(ic);
	return 0;
}

void
wtap_resume(struct wtap_softc *sc)
{

	DWTAP_PRINTF("%s\n", __func__);
}

void
wtap_suspend(struct wtap_softc *sc)
{

	DWTAP_PRINTF("%s\n", __func__);
}

void
wtap_shutdown(struct wtap_softc *sc)
{

	DWTAP_PRINTF("%s\n", __func__);
}

void
wtap_intr(struct wtap_softc *sc)
{

	DWTAP_PRINTF("%s\n", __func__);
}
