/*	$OpenBSD: if_cnmac.c,v 1.86 2024/05/20 23:13:33 jsg Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "bpfilter.h"

/*
 * XXXSEIL
 * If no free send buffer is available, free all the sent buffer and bail out.
 */
#define OCTEON_ETH_SEND_QUEUE_CHECK

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/conf.h>
#include <sys/stdint.h> /* uintptr_t */
#include <sys/syslog.h>
#include <sys/endian.h>
#include <sys/atomic.h>

#include <net/if.h>
#include <net/if_media.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/octeonvar.h>
#include <machine/octeon_model.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <octeon/dev/cn30xxciureg.h>
#include <octeon/dev/cn30xxnpireg.h>
#include <octeon/dev/cn30xxgmxreg.h>
#include <octeon/dev/cn30xxipdreg.h>
#include <octeon/dev/cn30xxpipreg.h>
#include <octeon/dev/cn30xxpowreg.h>
#include <octeon/dev/cn30xxfaureg.h>
#include <octeon/dev/cn30xxfpareg.h>
#include <octeon/dev/cn30xxbootbusreg.h>
#include <octeon/dev/cn30xxfpavar.h>
#include <octeon/dev/cn30xxgmxvar.h>
#include <octeon/dev/cn30xxfauvar.h>
#include <octeon/dev/cn30xxpowvar.h>
#include <octeon/dev/cn30xxipdvar.h>
#include <octeon/dev/cn30xxpipvar.h>
#include <octeon/dev/cn30xxpkovar.h>
#include <octeon/dev/cn30xxsmivar.h>
#include <octeon/dev/iobusvar.h>
#include <octeon/dev/if_cnmacvar.h>

#ifdef OCTEON_ETH_DEBUG
#define	OCTEON_ETH_KASSERT(x)	KASSERT(x)
#define	OCTEON_ETH_KDASSERT(x)	KDASSERT(x)
#else
#define	OCTEON_ETH_KASSERT(x)
#define	OCTEON_ETH_KDASSERT(x)
#endif

/*
 * Set the PKO to think command buffers are an odd length.  This makes it so we
 * never have to divide a command across two buffers.
 */
#define OCTEON_POOL_NWORDS_CMD	\
	    (((uint32_t)OCTEON_POOL_SIZE_CMD / sizeof(uint64_t)) - 1)
#define FPA_COMMAND_BUFFER_POOL_NWORDS	OCTEON_POOL_NWORDS_CMD	/* XXX */

CTASSERT(MCLBYTES >= OCTEON_POOL_SIZE_PKT + CACHELINESIZE);

void	cnmac_buf_init(struct cnmac_softc *);

int	cnmac_match(struct device *, void *, void *);
void	cnmac_attach(struct device *, struct device *, void *);
void	cnmac_pip_init(struct cnmac_softc *);
void	cnmac_ipd_init(struct cnmac_softc *);
void	cnmac_pko_init(struct cnmac_softc *);

void	cnmac_board_mac_addr(uint8_t *);

int	cnmac_mii_readreg(struct device *, int, int);
void	cnmac_mii_writereg(struct device *, int, int, int);
void	cnmac_mii_statchg(struct device *);

int	cnmac_mediainit(struct cnmac_softc *);
void	cnmac_mediastatus(struct ifnet *, struct ifmediareq *);
int	cnmac_mediachange(struct ifnet *);

void	cnmac_send_queue_flush_prefetch(struct cnmac_softc *);
void	cnmac_send_queue_flush_fetch(struct cnmac_softc *);
void	cnmac_send_queue_flush(struct cnmac_softc *);
int	cnmac_send_queue_is_full(struct cnmac_softc *);
void	cnmac_send_queue_add(struct cnmac_softc *,
	    struct mbuf *, uint64_t *);
void	cnmac_send_queue_del(struct cnmac_softc *,
	    struct mbuf **, uint64_t **);
int	cnmac_buf_free_work(struct cnmac_softc *, uint64_t *);

int	cnmac_ioctl(struct ifnet *, u_long, caddr_t);
void	cnmac_watchdog(struct ifnet *);
int	cnmac_init(struct ifnet *);
int	cnmac_stop(struct ifnet *, int);
void	cnmac_start(struct ifqueue *);

int	cnmac_send_cmd(struct cnmac_softc *, uint64_t, uint64_t);
uint64_t cnmac_send_makecmd_w1(int, paddr_t);
uint64_t cnmac_send_makecmd_w0(uint64_t, uint64_t, size_t, int, int);
int	cnmac_send_makecmd_gbuf(struct cnmac_softc *,
	    struct mbuf *, uint64_t *, int *);
int	cnmac_send_makecmd(struct cnmac_softc *,
	    struct mbuf *, uint64_t *, uint64_t *, uint64_t *);
int	cnmac_send_buf(struct cnmac_softc *,
	    struct mbuf *, uint64_t *);
int	cnmac_send(struct cnmac_softc *, struct mbuf *);

int	cnmac_reset(struct cnmac_softc *);
int	cnmac_configure(struct cnmac_softc *);
int	cnmac_configure_common(struct cnmac_softc *);

void	cnmac_free_task(void *);
void	cnmac_tick_free(void *arg);
void	cnmac_tick_misc(void *);

int	cnmac_recv_mbuf(struct cnmac_softc *,
	    uint64_t *, struct mbuf **, int *);
int	cnmac_recv_check(struct cnmac_softc *, uint64_t);
int	cnmac_recv(struct cnmac_softc *, uint64_t *, struct mbuf_list *);
int	cnmac_intr(void *);

int	cnmac_mbuf_alloc(int);

#if NKSTAT > 0
void	cnmac_kstat_attach(struct cnmac_softc *);
int	cnmac_kstat_read(struct kstat *);
void	cnmac_kstat_tick(struct cnmac_softc *);
#endif

/* device parameters */
int	cnmac_param_pko_cmd_w0_n2 = 1;

const struct cfattach cnmac_ca = {
	sizeof(struct cnmac_softc), cnmac_match, cnmac_attach
};

struct cfdriver cnmac_cd = { NULL, "cnmac", DV_IFNET };

/* ---- buffer management */

const struct cnmac_pool_param {
	int			poolno;
	size_t			size;
	size_t			nelems;
} cnmac_pool_params[] = {
#define	_ENTRY(x)	{ OCTEON_POOL_NO_##x, OCTEON_POOL_SIZE_##x, OCTEON_POOL_NELEMS_##x }
	_ENTRY(WQE),
	_ENTRY(CMD),
	_ENTRY(SG)
#undef	_ENTRY
};
struct cn30xxfpa_buf	*cnmac_pools[8];
#define	cnmac_fb_wqe	cnmac_pools[OCTEON_POOL_NO_WQE]
#define	cnmac_fb_cmd	cnmac_pools[OCTEON_POOL_NO_CMD]
#define	cnmac_fb_sg	cnmac_pools[OCTEON_POOL_NO_SG]

uint64_t cnmac_mac_addr = 0;
uint32_t cnmac_mac_addr_offset = 0;

int	cnmac_mbufs_to_alloc;
int	cnmac_npowgroups = 0;

void
cnmac_buf_init(struct cnmac_softc *sc)
{
	static int once;
	int i;
	const struct cnmac_pool_param *pp;
	struct cn30xxfpa_buf *fb;

	if (once == 1)
		return;
	once = 1;

	for (i = 0; i < (int)nitems(cnmac_pool_params); i++) {
		pp = &cnmac_pool_params[i];
		cn30xxfpa_buf_init(pp->poolno, pp->size, pp->nelems, &fb);
		cnmac_pools[pp->poolno] = fb;
	}
}

/* ---- autoconf */

int
cnmac_match(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = (struct cfdata *)match;
	struct cn30xxgmx_attach_args *ga = aux;

	if (strcmp(cf->cf_driver->cd_name, ga->ga_name) != 0) {
		return 0;
	}
	return 1;
}

void
cnmac_attach(struct device *parent, struct device *self, void *aux)
{
	struct cnmac_softc *sc = (void *)self;
	struct cn30xxgmx_attach_args *ga = aux;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	if (cnmac_npowgroups >= OCTEON_POW_GROUP_MAX) {
		printf(": out of POW groups\n");
		return;
	}

	atomic_add_int(&cnmac_mbufs_to_alloc,
	    cnmac_mbuf_alloc(CNMAC_MBUFS_PER_PORT));

	sc->sc_regt = ga->ga_regt;
	sc->sc_dmat = ga->ga_dmat;
	sc->sc_port = ga->ga_portno;
	sc->sc_port_type = ga->ga_port_type;
	sc->sc_gmx = ga->ga_gmx;
	sc->sc_gmx_port = ga->ga_gmx_port;
	sc->sc_smi = ga->ga_smi;
	sc->sc_phy_addr = ga->ga_phy_addr;
	sc->sc_powgroup = cnmac_npowgroups++;

	sc->sc_init_flag = 0;

	/*
	 * XXX
	 * Setting PIP_IP_OFFSET[OFFSET] to 8 causes panic ... why???
	 */
	sc->sc_ip_offset = 0/* XXX */;

	cnmac_board_mac_addr(sc->sc_arpcom.ac_enaddr);
	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	ml_init(&sc->sc_sendq);
	sc->sc_soft_req_thresh = 15/* XXX */;
	sc->sc_ext_callback_cnt = 0;

	task_set(&sc->sc_free_task, cnmac_free_task, sc);
	timeout_set(&sc->sc_tick_misc_ch, cnmac_tick_misc, sc);
	timeout_set(&sc->sc_tick_free_ch, cnmac_tick_free, sc);

	cn30xxfau_op_init(&sc->sc_fau_done,
	    OCTEON_CVMSEG_ETHER_OFFSET(sc->sc_dev.dv_unit, csm_ether_fau_done),
	    OCT_FAU_REG_ADDR_END - (8 * (sc->sc_dev.dv_unit + 1))/* XXX */);
	cn30xxfau_op_set_8(&sc->sc_fau_done, 0);

	cnmac_pip_init(sc);
	cnmac_ipd_init(sc);
	cnmac_pko_init(sc);

	cnmac_configure_common(sc);

	sc->sc_gmx_port->sc_ipd = sc->sc_ipd;
	sc->sc_gmx_port->sc_port_mii = &sc->sc_mii;
	sc->sc_gmx_port->sc_port_ac = &sc->sc_arpcom;

	/* XXX */
	sc->sc_pow = &cn30xxpow_softc;

	cnmac_mediainit(sc);

	strncpy(ifp->if_xname, sc->sc_dev.dv_xname, sizeof(ifp->if_xname));
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = cnmac_ioctl;
	ifp->if_qstart = cnmac_start;
	ifp->if_watchdog = cnmac_watchdog;
	ifp->if_hardmtu = CNMAC_MAX_MTU;
	ifq_init_maxlen(&ifp->if_snd, max(GATHER_QUEUE_SIZE, IFQ_MAXLEN));

	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_CSUM_TCPv4 |
	    IFCAP_CSUM_UDPv4 | IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;

	cn30xxgmx_set_filter(sc->sc_gmx_port);

	if_attach(ifp);
	ether_ifattach(ifp);

	cnmac_buf_init(sc);

#if NKSTAT > 0
	cnmac_kstat_attach(sc);
#endif

	sc->sc_ih = octeon_intr_establish(POW_WORKQ_IRQ(sc->sc_powgroup),
	    IPL_NET | IPL_MPSAFE, cnmac_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL)
		panic("%s: could not set up interrupt", sc->sc_dev.dv_xname);
}

/* ---- submodules */

void
cnmac_pip_init(struct cnmac_softc *sc)
{
	struct cn30xxpip_attach_args pip_aa;

	pip_aa.aa_port = sc->sc_port;
	pip_aa.aa_regt = sc->sc_regt;
	pip_aa.aa_tag_type = POW_TAG_TYPE_ORDERED/* XXX */;
	pip_aa.aa_receive_group = sc->sc_powgroup;
	pip_aa.aa_ip_offset = sc->sc_ip_offset;
	cn30xxpip_init(&pip_aa, &sc->sc_pip);
	cn30xxpip_port_config(sc->sc_pip);
}

void
cnmac_ipd_init(struct cnmac_softc *sc)
{
	struct cn30xxipd_attach_args ipd_aa;

	ipd_aa.aa_port = sc->sc_port;
	ipd_aa.aa_regt = sc->sc_regt;
	ipd_aa.aa_first_mbuff_skip = 0/* XXX */;
	ipd_aa.aa_not_first_mbuff_skip = 0/* XXX */;
	cn30xxipd_init(&ipd_aa, &sc->sc_ipd);
}

void
cnmac_pko_init(struct cnmac_softc *sc)
{
	struct cn30xxpko_attach_args pko_aa;

	pko_aa.aa_port = sc->sc_port;
	pko_aa.aa_regt = sc->sc_regt;
	pko_aa.aa_cmdptr = &sc->sc_cmdptr;
	pko_aa.aa_cmd_buf_pool = OCTEON_POOL_NO_CMD;
	pko_aa.aa_cmd_buf_size = OCTEON_POOL_NWORDS_CMD;
	cn30xxpko_init(&pko_aa, &sc->sc_pko);
}

/* ---- XXX */

void
cnmac_board_mac_addr(uint8_t *enaddr)
{
	int id;

	/* Initialize MAC addresses from the global address base. */
	if (cnmac_mac_addr == 0) {
		memcpy((uint8_t *)&cnmac_mac_addr + 2,
		    octeon_boot_info->mac_addr_base, 6);

		/*
		 * Should be allowed to fail hard if couldn't read the
		 * mac_addr_base address...
		 */
		if (cnmac_mac_addr == 0)
			return;

		/*
		 * Calculate the offset from the mac_addr_base that will be used
		 * for the next sc->sc_port.
		 */
		id = octeon_get_chipid();

		switch (octeon_model_family(id)) {
		case OCTEON_MODEL_FAMILY_CN56XX:
			cnmac_mac_addr_offset = 1;
			break;
		/*
		case OCTEON_MODEL_FAMILY_CN52XX:
		case OCTEON_MODEL_FAMILY_CN63XX:
			cnmac_mac_addr_offset = 2;
			break;
		*/
		default:
			cnmac_mac_addr_offset = 0;
			break;
		}

		enaddr += cnmac_mac_addr_offset;
	}

	/* No more MAC addresses to assign. */
	if (cnmac_mac_addr_offset >= octeon_boot_info->mac_addr_count)
		return;

	if (enaddr)
		memcpy(enaddr, (uint8_t *)&cnmac_mac_addr + 2, 6);

	cnmac_mac_addr++;
	cnmac_mac_addr_offset++;
}

/* ---- media */

int
cnmac_mii_readreg(struct device *self, int phy_no, int reg)
{
	struct cnmac_softc *sc = (struct cnmac_softc *)self;
	return cn30xxsmi_read(sc->sc_smi, phy_no, reg);
}

void
cnmac_mii_writereg(struct device *self, int phy_no, int reg, int value)
{
	struct cnmac_softc *sc = (struct cnmac_softc *)self;
	cn30xxsmi_write(sc->sc_smi, phy_no, reg, value);
}

void
cnmac_mii_statchg(struct device *self)
{
	struct cnmac_softc *sc = (struct cnmac_softc *)self;

	cn30xxpko_port_enable(sc->sc_pko, 0);
	cn30xxgmx_port_enable(sc->sc_gmx_port, 0);

	cnmac_reset(sc);

	cn30xxpko_port_enable(sc->sc_pko, 1);
	cn30xxgmx_port_enable(sc->sc_gmx_port, 1);
}

int
cnmac_mediainit(struct cnmac_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mii_softc *child;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = cnmac_mii_readreg;
	sc->sc_mii.mii_writereg = cnmac_mii_writereg;
	sc->sc_mii.mii_statchg = cnmac_mii_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, cnmac_mediachange,
	    cnmac_mediastatus);

	mii_attach(&sc->sc_dev, &sc->sc_mii,
	    0xffffffff, sc->sc_phy_addr, MII_OFFSET_ANY, MIIF_DOPAUSE);

	child = LIST_FIRST(&sc->sc_mii.mii_phys);
	if (child == NULL) {
                /* No PHY attached. */
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER | IFM_MANUAL,
			    0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_MANUAL);
	} else {
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_AUTO);
	}

	return 0;
}

void
cnmac_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct cnmac_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
	ifmr->ifm_active = (sc->sc_mii.mii_media_active & ~IFM_ETH_FMASK) |
	    sc->sc_gmx_port->sc_port_flowflags;
}

int
cnmac_mediachange(struct ifnet *ifp)
{
	struct cnmac_softc *sc = ifp->if_softc;

	if ((ifp->if_flags & IFF_UP) == 0)
		return 0;

	return mii_mediachg(&sc->sc_mii);
}

/* ---- send buffer garbage collection */

void
cnmac_send_queue_flush_prefetch(struct cnmac_softc *sc)
{
	OCTEON_ETH_KASSERT(sc->sc_prefetch == 0);
	cn30xxfau_op_inc_fetch_8(&sc->sc_fau_done, 0);
	sc->sc_prefetch = 1;
}

void
cnmac_send_queue_flush_fetch(struct cnmac_softc *sc)
{
#ifndef  OCTEON_ETH_DEBUG
	if (!sc->sc_prefetch)
		return;
#endif
	OCTEON_ETH_KASSERT(sc->sc_prefetch == 1);
	sc->sc_hard_done_cnt = cn30xxfau_op_inc_read_8(&sc->sc_fau_done);
	OCTEON_ETH_KASSERT(sc->sc_hard_done_cnt <= 0);
	sc->sc_prefetch = 0;
}

void
cnmac_send_queue_flush(struct cnmac_softc *sc)
{
	const int64_t sent_count = sc->sc_hard_done_cnt;
	int i;

	OCTEON_ETH_KASSERT(sent_count <= 0);

	for (i = 0; i < 0 - sent_count; i++) {
		struct mbuf *m;
		uint64_t *gbuf;

		cnmac_send_queue_del(sc, &m, &gbuf);

		cn30xxfpa_buf_put_paddr(cnmac_fb_sg, XKPHYS_TO_PHYS(gbuf));

		m_freem(m);
	}

	cn30xxfau_op_add_8(&sc->sc_fau_done, i);
}

int
cnmac_send_queue_is_full(struct cnmac_softc *sc)
{
#ifdef OCTEON_ETH_SEND_QUEUE_CHECK
	int64_t nofree_cnt;

	nofree_cnt = ml_len(&sc->sc_sendq) + sc->sc_hard_done_cnt; 

	if (__predict_false(nofree_cnt == GATHER_QUEUE_SIZE - 1)) {
		cnmac_send_queue_flush(sc);
		return 1;
	}

#endif
	return 0;
}

void
cnmac_send_queue_add(struct cnmac_softc *sc, struct mbuf *m,
    uint64_t *gbuf)
{
	OCTEON_ETH_KASSERT(m->m_flags & M_PKTHDR);

	m->m_pkthdr.ph_cookie = gbuf;
	ml_enqueue(&sc->sc_sendq, m);

	if (m->m_ext.ext_free_fn != 0)
		sc->sc_ext_callback_cnt++;
}

void
cnmac_send_queue_del(struct cnmac_softc *sc, struct mbuf **rm,
    uint64_t **rgbuf)
{
	struct mbuf *m;
	m = ml_dequeue(&sc->sc_sendq);
	OCTEON_ETH_KASSERT(m != NULL);

	*rm = m;
	*rgbuf = m->m_pkthdr.ph_cookie;

	if (m->m_ext.ext_free_fn != 0) {
		sc->sc_ext_callback_cnt--;
		OCTEON_ETH_KASSERT(sc->sc_ext_callback_cnt >= 0);
	}
}

int
cnmac_buf_free_work(struct cnmac_softc *sc, uint64_t *work)
{
	paddr_t addr, pktbuf;
	uint64_t word3;
	unsigned int back, nbufs;

	nbufs = (work[2] & PIP_WQE_WORD2_IP_BUFS) >>
	    PIP_WQE_WORD2_IP_BUFS_SHIFT;
	word3 = work[3];
	while (nbufs-- > 0) {
		addr = word3 & PIP_WQE_WORD3_ADDR;
		back = (word3 & PIP_WQE_WORD3_BACK) >>
		    PIP_WQE_WORD3_BACK_SHIFT;
		pktbuf = (addr & ~(CACHELINESIZE - 1)) - back * CACHELINESIZE;

		cn30xxfpa_store(pktbuf, OCTEON_POOL_NO_PKT,
		    OCTEON_POOL_SIZE_PKT / CACHELINESIZE);

		if (nbufs > 0)
			memcpy(&word3, (void *)PHYS_TO_XKPHYS(addr -
			    sizeof(word3), CCA_CACHED), sizeof(word3));
	}

	cn30xxfpa_buf_put_paddr(cnmac_fb_wqe, XKPHYS_TO_PHYS(work));

	return 0;
}

/* ---- ifnet interfaces */

int
cnmac_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct cnmac_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			cnmac_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				cnmac_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				cnmac_stop(ifp, 0);
		}
		break;

	case SIOCSIFMEDIA:
		/* Flow control requires full-duplex mode. */
		if (IFM_SUBTYPE(ifr->ifr_media) == IFM_AUTO ||
		    (ifr->ifr_media & IFM_FDX) == 0) {
			ifr->ifr_media &= ~IFM_ETH_FMASK;
		}
		if (IFM_SUBTYPE(ifr->ifr_media) != IFM_AUTO) {
			if ((ifr->ifr_media & IFM_ETH_FMASK) == IFM_FLOW) {
				ifr->ifr_media |=
				    IFM_ETH_TXPAUSE | IFM_ETH_RXPAUSE;
			}
			sc->sc_gmx_port->sc_port_flowflags = 
				ifr->ifr_media & IFM_ETH_FMASK;
		}
		/* FALLTHROUGH */
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ISSET(ifp->if_flags, IFF_RUNNING))
			cn30xxgmx_set_filter(sc->sc_gmx_port);
		error = 0;
	}

	splx(s);
	return (error);
}

/* ---- send (output) */

uint64_t
cnmac_send_makecmd_w0(uint64_t fau0, uint64_t fau1, size_t len, int segs,
    int ipoffp1)
{
	return cn30xxpko_cmd_word0(
		OCT_FAU_OP_SIZE_64,		/* sz1 */
		OCT_FAU_OP_SIZE_64,		/* sz0 */
		1, fau1, 1, fau0,		/* s1, reg1, s0, reg0 */
		0,				/* le */
		cnmac_param_pko_cmd_w0_n2,	/* n2 */
		1, 0,				/* q, r */
		(segs == 1) ? 0 : 1,		/* g */
		ipoffp1, 0, 1,			/* ipoffp1, ii, df */
		segs, (int)len);		/* segs, totalbytes */
}

uint64_t 
cnmac_send_makecmd_w1(int size, paddr_t addr)
{
	return cn30xxpko_cmd_word1(
		0, 0,				/* i, back */
		OCTEON_POOL_NO_SG,		/* pool */
		size, addr);			/* size, addr */
}

#define KVTOPHYS(addr)	cnmac_kvtophys((vaddr_t)(addr))

static inline paddr_t
cnmac_kvtophys(vaddr_t kva)
{
	KASSERT(IS_XKPHYS(kva));
	return XKPHYS_TO_PHYS(kva);
}

int
cnmac_send_makecmd_gbuf(struct cnmac_softc *sc, struct mbuf *m0,
    uint64_t *gbuf, int *rsegs)
{
	struct mbuf *m;
	int segs = 0;

	for (m = m0; m != NULL; m = m->m_next) {
		if (__predict_false(m->m_len == 0))
			continue;

		if (segs >= OCTEON_POOL_SIZE_SG / sizeof(uint64_t))
			goto defrag;
		gbuf[segs] = cnmac_send_makecmd_w1(m->m_len,
		    KVTOPHYS(m->m_data));
		segs++;
	}

	*rsegs = segs;

	return 0;

defrag:
	if (m_defrag(m0, M_DONTWAIT) != 0)
		return 1;
	gbuf[0] = cnmac_send_makecmd_w1(m0->m_len, KVTOPHYS(m0->m_data));
	*rsegs = 1;
	return 0;
}

int
cnmac_send_makecmd(struct cnmac_softc *sc, struct mbuf *m,
    uint64_t *gbuf, uint64_t *rpko_cmd_w0, uint64_t *rpko_cmd_w1)
{
	uint64_t pko_cmd_w0, pko_cmd_w1;
	int ipoffp1;
	int segs;
	int result = 0;

	if (cnmac_send_makecmd_gbuf(sc, m, gbuf, &segs)) {
		log(LOG_WARNING, "%s: large number of transmission"
		    " data segments", sc->sc_dev.dv_xname);
		result = 1;
		goto done;
	}

	/* Get the IP packet offset for TCP/UDP checksum offloading. */
	ipoffp1 = (m->m_pkthdr.csum_flags & (M_TCP_CSUM_OUT | M_UDP_CSUM_OUT))
	    ? (ETHER_HDR_LEN + 1) : 0;

	/*
	 * segs == 1	-> link mode (single continuous buffer)
	 *		   WORD1[size] is number of bytes pointed by segment
	 *
	 * segs > 1	-> gather mode (scatter-gather buffer)
	 *		   WORD1[size] is number of segments
	 */
	pko_cmd_w0 = cnmac_send_makecmd_w0(sc->sc_fau_done.fd_regno,
	    0, m->m_pkthdr.len, segs, ipoffp1);
	pko_cmd_w1 = cnmac_send_makecmd_w1(
	    (segs == 1) ? m->m_pkthdr.len : segs,
	    (segs == 1) ? 
		KVTOPHYS(m->m_data) :
		XKPHYS_TO_PHYS(gbuf));

	*rpko_cmd_w0 = pko_cmd_w0;
	*rpko_cmd_w1 = pko_cmd_w1;

done:
	return result;
}

int
cnmac_send_cmd(struct cnmac_softc *sc, uint64_t pko_cmd_w0,
    uint64_t pko_cmd_w1)
{
	uint64_t *cmdptr;
	int result = 0;

	cmdptr = (uint64_t *)PHYS_TO_XKPHYS(sc->sc_cmdptr.cmdptr, CCA_CACHED);
	cmdptr += sc->sc_cmdptr.cmdptr_idx;

	OCTEON_ETH_KASSERT(cmdptr != NULL);

	*cmdptr++ = pko_cmd_w0;
	*cmdptr++ = pko_cmd_w1;

	OCTEON_ETH_KASSERT(sc->sc_cmdptr.cmdptr_idx + 2 <= FPA_COMMAND_BUFFER_POOL_NWORDS - 1);

	if (sc->sc_cmdptr.cmdptr_idx + 2 == FPA_COMMAND_BUFFER_POOL_NWORDS - 1) {
		paddr_t buf;

		buf = cn30xxfpa_buf_get_paddr(cnmac_fb_cmd);
		if (buf == 0) {
			log(LOG_WARNING,
			    "%s: cannot allocate command buffer from free pool allocator\n",
			    sc->sc_dev.dv_xname);
			result = 1;
			goto done;
		}
		*cmdptr++ = buf;
		sc->sc_cmdptr.cmdptr = (uint64_t)buf;
		sc->sc_cmdptr.cmdptr_idx = 0;
	} else {
		sc->sc_cmdptr.cmdptr_idx += 2;
	}

	cn30xxpko_op_doorbell_write(sc->sc_port, sc->sc_port, 2);

done:
	return result;
}

int
cnmac_send_buf(struct cnmac_softc *sc, struct mbuf *m, uint64_t *gbuf)
{
	int result = 0, error;
	uint64_t pko_cmd_w0, pko_cmd_w1;

	error = cnmac_send_makecmd(sc, m, gbuf, &pko_cmd_w0, &pko_cmd_w1);
	if (error != 0) {
		/* already logging */
		result = error;
		goto done;
	}

	error = cnmac_send_cmd(sc, pko_cmd_w0, pko_cmd_w1);
	if (error != 0) {
		/* already logging */
		result = error;
	}

done:
	return result;
}

int
cnmac_send(struct cnmac_softc *sc, struct mbuf *m)
{
	paddr_t gaddr = 0;
	uint64_t *gbuf = NULL;
	int result = 0, error;

	gaddr = cn30xxfpa_buf_get_paddr(cnmac_fb_sg);
	if (gaddr == 0) {
		log(LOG_WARNING,
		    "%s: cannot allocate gather buffer from free pool allocator\n",
		    sc->sc_dev.dv_xname);
		result = 1;
		goto done;
	}

	gbuf = (uint64_t *)(uintptr_t)PHYS_TO_XKPHYS(gaddr, CCA_CACHED);

	error = cnmac_send_buf(sc, m, gbuf);
	if (error != 0) {
		/* already logging */
		cn30xxfpa_buf_put_paddr(cnmac_fb_sg, gaddr);
		result = error;
		goto done;
	}

	cnmac_send_queue_add(sc, m, gbuf);

done:
	return result;
}

void
cnmac_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct cnmac_softc *sc = ifp->if_softc;
	struct mbuf *m;

	if (__predict_false(!cn30xxgmx_link_status(sc->sc_gmx_port))) {
		ifq_purge(ifq);
		return;
	}

	/*
	 * performance tuning
	 * presend iobdma request 
	 */
	cnmac_send_queue_flush_prefetch(sc);

	for (;;) {
		cnmac_send_queue_flush_fetch(sc); /* XXX */

		/*
		 * XXXSEIL
		 * If no free send buffer is available, free all the sent buffer
		 * and bail out.
		 */
		if (cnmac_send_queue_is_full(sc)) {
			ifq_set_oactive(ifq);
			timeout_add(&sc->sc_tick_free_ch, 1);
			return;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			return;

#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		/* XXX */
		if (ml_len(&sc->sc_sendq) > sc->sc_soft_req_thresh)
			cnmac_send_queue_flush(sc);
		if (cnmac_send(sc, m)) {
			ifp->if_oerrors++;
			m_freem(m);
			log(LOG_WARNING,
		  	  "%s: failed to transmit packet\n",
		    	  sc->sc_dev.dv_xname);
		}
		/* XXX */

		/*
		 * send next iobdma request 
		 */
		cnmac_send_queue_flush_prefetch(sc);
	}

	cnmac_send_queue_flush_fetch(sc);
}

void
cnmac_watchdog(struct ifnet *ifp)
{
	struct cnmac_softc *sc = ifp->if_softc;

	printf("%s: device timeout\n", sc->sc_dev.dv_xname);

	cnmac_stop(ifp, 0);

	cnmac_configure(sc);

	SET(ifp->if_flags, IFF_RUNNING);
	ifp->if_timer = 0;

	ifq_restart(&ifp->if_snd);
}

int
cnmac_init(struct ifnet *ifp)
{
	struct cnmac_softc *sc = ifp->if_softc;

	/* XXX don't disable commonly used parts!!! XXX */
	if (sc->sc_init_flag == 0) {
		/* Cancel any pending I/O. */
		cnmac_stop(ifp, 0);

		/* Initialize the device */
		cnmac_configure(sc);

		cn30xxpko_enable(sc->sc_pko);
		cn30xxipd_enable(sc->sc_ipd);

		sc->sc_init_flag = 1;
	} else {
		cn30xxgmx_port_enable(sc->sc_gmx_port, 1);
	}
	cnmac_mediachange(ifp);

	cn30xxpip_stats_init(sc->sc_pip);
	cn30xxgmx_stats_init(sc->sc_gmx_port);
	cn30xxgmx_set_filter(sc->sc_gmx_port);

	timeout_add_sec(&sc->sc_tick_misc_ch, 1);
	timeout_add_sec(&sc->sc_tick_free_ch, 1);

	SET(ifp->if_flags, IFF_RUNNING);
	ifq_clr_oactive(&ifp->if_snd);

	return 0;
}

int
cnmac_stop(struct ifnet *ifp, int disable)
{
	struct cnmac_softc *sc = ifp->if_softc;

	CLR(ifp->if_flags, IFF_RUNNING);

	timeout_del(&sc->sc_tick_misc_ch);
	timeout_del(&sc->sc_tick_free_ch);

	mii_down(&sc->sc_mii);

	cn30xxgmx_port_enable(sc->sc_gmx_port, 0);

	intr_barrier(sc->sc_ih);
	ifq_barrier(&ifp->if_snd);

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	return 0;
}

/* ---- misc */

#define PKO_INDEX_MASK	((1ULL << 12/* XXX */) - 1)

int
cnmac_reset(struct cnmac_softc *sc)
{
	cn30xxgmx_reset_speed(sc->sc_gmx_port);
	cn30xxgmx_reset_flowctl(sc->sc_gmx_port);
	cn30xxgmx_reset_timing(sc->sc_gmx_port);

	return 0;
}

int
cnmac_configure(struct cnmac_softc *sc)
{
	cn30xxgmx_port_enable(sc->sc_gmx_port, 0);

	cnmac_reset(sc);

	cn30xxpko_port_config(sc->sc_pko);
	cn30xxpko_port_enable(sc->sc_pko, 1);
	cn30xxpow_config(sc->sc_pow, sc->sc_powgroup);

	cn30xxgmx_port_enable(sc->sc_gmx_port, 1);

	return 0;
}

int
cnmac_configure_common(struct cnmac_softc *sc)
{
	static int once;

	uint64_t reg;

	if (once == 1)
		return 0;
	once = 1;

	cn30xxipd_config(sc->sc_ipd);
	cn30xxpko_config(sc->sc_pko);

	/* Set padding for packets that Octeon does not recognize as IP. */
	reg = octeon_xkphys_read_8(PIP_GBL_CFG);
	reg &= ~PIP_GBL_CFG_NIP_SHF_MASK;
	reg |= ETHER_ALIGN << PIP_GBL_CFG_NIP_SHF_SHIFT;
	octeon_xkphys_write_8(PIP_GBL_CFG, reg);

	return 0;
}

int
cnmac_mbuf_alloc(int n)
{
	struct mbuf *m;
	paddr_t pktbuf;

	while (n > 0) {
		m = MCLGETL(NULL, M_NOWAIT,
		    OCTEON_POOL_SIZE_PKT + CACHELINESIZE);
		if (m == NULL || !ISSET(m->m_flags, M_EXT)) {
			m_freem(m);
			break;
		}

		m->m_data = (void *)(((vaddr_t)m->m_data + CACHELINESIZE) &
		    ~(CACHELINESIZE - 1));
		((struct mbuf **)m->m_data)[-1] = m;

		pktbuf = KVTOPHYS(m->m_data);
		m->m_pkthdr.ph_cookie = (void *)pktbuf;
		cn30xxfpa_store(pktbuf, OCTEON_POOL_NO_PKT,
		    OCTEON_POOL_SIZE_PKT / CACHELINESIZE);

		n--;
	}
	return n;
}

int
cnmac_recv_mbuf(struct cnmac_softc *sc, uint64_t *work,
    struct mbuf **rm, int *nmbuf)
{
	struct mbuf *m, *m0, *mprev, **pm;
	paddr_t addr, pktbuf;
	uint64_t word1 = work[1];
	uint64_t word2 = work[2];
	uint64_t word3 = work[3];
	unsigned int back, i, nbufs;
	unsigned int left, total, size;

	cn30xxfpa_buf_put_paddr(cnmac_fb_wqe, XKPHYS_TO_PHYS(work));

	nbufs = (word2 & PIP_WQE_WORD2_IP_BUFS) >> PIP_WQE_WORD2_IP_BUFS_SHIFT;
	if (nbufs == 0)
		panic("%s: dynamic short packet", __func__);

	m0 = mprev = NULL;
	total = left = (word1 & PIP_WQE_WORD1_LEN) >> 48;
	for (i = 0; i < nbufs; i++) {
		addr = word3 & PIP_WQE_WORD3_ADDR;
		back = (word3 & PIP_WQE_WORD3_BACK) >> PIP_WQE_WORD3_BACK_SHIFT;
		pktbuf = (addr & ~(CACHELINESIZE - 1)) - back * CACHELINESIZE;
		pm = (struct mbuf **)PHYS_TO_XKPHYS(pktbuf, CCA_CACHED) - 1;
		m = *pm;
		*pm = NULL;
		if ((paddr_t)m->m_pkthdr.ph_cookie != pktbuf)
			panic("%s: packet pool is corrupted, mbuf cookie %p != "
			    "pktbuf %p", __func__, m->m_pkthdr.ph_cookie,
			    (void *)pktbuf);

		/*
		 * Because of a hardware bug in some Octeon models the size
		 * field of word3 can be wrong (erratum PKI-100).
		 * However, the hardware uses all space in a buffer before
		 * moving to the next one so it is possible to derive
		 * the size of this data segment from the size
		 * of packet data buffers.
		 */
		size = OCTEON_POOL_SIZE_PKT - (addr - pktbuf);
		if (size > left)
			size = left;

		m->m_pkthdr.ph_cookie = NULL;
		m->m_data += addr - pktbuf;
		m->m_len = size;
		left -= size;

		if (m0 == NULL)
			m0 = m;
		else {
			m->m_flags &= ~M_PKTHDR;
			mprev->m_next = m;
		}
		mprev = m;

		if (i + 1 < nbufs)
			memcpy(&word3, (void *)PHYS_TO_XKPHYS(addr -
			    sizeof(word3), CCA_CACHED), sizeof(word3));
	}

	m0->m_pkthdr.len = total;
	*rm = m0;
	*nmbuf = nbufs;

	return 0;
}

int
cnmac_recv_check(struct cnmac_softc *sc, uint64_t word2)
{
	static struct timeval rxerr_log_interval = { 0, 250000 };
	uint64_t opecode;

	if (__predict_true(!ISSET(word2, PIP_WQE_WORD2_NOIP_RE)))
		return 0;

	opecode = word2 & PIP_WQE_WORD2_NOIP_OPECODE;
	if ((sc->sc_arpcom.ac_if.if_flags & IFF_DEBUG) &&
	    ratecheck(&sc->sc_rxerr_log_last, &rxerr_log_interval))
		log(LOG_DEBUG, "%s: rx error (%lld)\n", sc->sc_dev.dv_xname,
		    opecode);

	/* XXX harmless error? */
	if (opecode == PIP_WQE_WORD2_RE_OPCODE_OVRRUN)
		return 0;

	return 1;
}

int
cnmac_recv(struct cnmac_softc *sc, uint64_t *work, struct mbuf_list *ml)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf *m;
	uint64_t word2;
	int nmbuf = 0;

	word2 = work[2];

	if (!(ifp->if_flags & IFF_RUNNING))
		goto drop;

	if (__predict_false(cnmac_recv_check(sc, word2) != 0)) {
		ifp->if_ierrors++;
		goto drop;
	}

	/* On success, this releases the work queue entry. */
	if (__predict_false(cnmac_recv_mbuf(sc, work, &m, &nmbuf) != 0)) {
		ifp->if_ierrors++;
		goto drop;
	}

	m->m_pkthdr.csum_flags = 0;
	if (__predict_true(!ISSET(word2, PIP_WQE_WORD2_IP_NI))) {
		/* Check IP checksum status. */
		if (!ISSET(word2, PIP_WQE_WORD2_IP_V6) &&
		    !ISSET(word2, PIP_WQE_WORD2_IP_IE))
			m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;

		/* Check TCP/UDP checksum status. */
		if (ISSET(word2, PIP_WQE_WORD2_IP_TU) &&
		    !ISSET(word2, PIP_WQE_WORD2_IP_FR) &&
		    !ISSET(word2, PIP_WQE_WORD2_IP_LE))
			m->m_pkthdr.csum_flags |=
			    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
	}

	ml_enqueue(ml, m);

	return nmbuf;

drop:
	cnmac_buf_free_work(sc, work);
	return 0;
}

int
cnmac_intr(void *arg)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct cnmac_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint64_t *work;
	uint64_t wqmask = 1ull << sc->sc_powgroup;
	uint32_t coreid = octeon_get_coreid();
	uint32_t port;
	int nmbuf = 0;

	_POW_WR8(sc->sc_pow, POW_PP_GRP_MSK_OFFSET(coreid), wqmask);

	cn30xxpow_tag_sw_wait();
	cn30xxpow_work_request_async(OCTEON_CVMSEG_OFFSET(csm_pow_intr),
	    POW_NO_WAIT);

	for (;;) {
		work = (uint64_t *)cn30xxpow_work_response_async(
		    OCTEON_CVMSEG_OFFSET(csm_pow_intr));
		if (work == NULL)
			break;

		cn30xxpow_tag_sw_wait();
		cn30xxpow_work_request_async(
		    OCTEON_CVMSEG_OFFSET(csm_pow_intr), POW_NO_WAIT);

		port = (work[1] & PIP_WQE_WORD1_IPRT) >> 42;
		if (port != sc->sc_port) {
			printf("%s: unexpected wqe port %u, should be %u\n",
			    sc->sc_dev.dv_xname, port, sc->sc_port);
			goto wqe_error;
		}

		nmbuf += cnmac_recv(sc, work, &ml);
	}

	_POW_WR8(sc->sc_pow, POW_WQ_INT_OFFSET, wqmask);

	if_input(ifp, &ml);

	nmbuf = cnmac_mbuf_alloc(nmbuf);
	if (nmbuf != 0)
		atomic_add_int(&cnmac_mbufs_to_alloc, nmbuf);

	return 1;

wqe_error:
	printf("word0: 0x%016llx\n", work[0]);
	printf("word1: 0x%016llx\n", work[1]);
	printf("word2: 0x%016llx\n", work[2]);
	printf("word3: 0x%016llx\n", work[3]);
	panic("wqe error");
}

/* ---- tick */

void
cnmac_free_task(void *arg)
{
	struct cnmac_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct ifqueue *ifq = &ifp->if_snd;
	int resched = 1;
	int timeout;

	if (ml_len(&sc->sc_sendq) > 0) {
		cnmac_send_queue_flush_prefetch(sc);
		cnmac_send_queue_flush_fetch(sc);
		cnmac_send_queue_flush(sc);
	}

	if (ifq_is_oactive(ifq)) {
		ifq_clr_oactive(ifq);
		cnmac_start(ifq);

		if (ifq_is_oactive(ifq)) {
			/* The start routine did rescheduling already. */
			resched = 0;
		}
	}

	if (resched) {
		timeout = (sc->sc_ext_callback_cnt > 0) ? 1 : hz;
		timeout_add(&sc->sc_tick_free_ch, timeout);
	}
}

/*
 * cnmac_tick_free
 *
 * => garbage collect send gather buffer / mbuf
 * => called at softclock
 */
void
cnmac_tick_free(void *arg)
{
	struct cnmac_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int to_alloc;

	ifq_serialize(&ifp->if_snd, &sc->sc_free_task);

	if (cnmac_mbufs_to_alloc != 0) {
		to_alloc = atomic_swap_uint(&cnmac_mbufs_to_alloc, 0);
		to_alloc = cnmac_mbuf_alloc(to_alloc);
		if (to_alloc != 0)
			atomic_add_int(&cnmac_mbufs_to_alloc, to_alloc);
	}
}

/*
 * cnmac_tick_misc
 *
 * => collect statistics
 * => check link status
 * => called at softclock
 */
void
cnmac_tick_misc(void *arg)
{
	struct cnmac_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

#if NKSTAT > 0
	cnmac_kstat_tick(sc);
#endif

	timeout_add_sec(&sc->sc_tick_misc_ch, 1);
}

#if NKSTAT > 0
#define KVE(n, t) \
	KSTAT_KV_UNIT_INITIALIZER((n), KSTAT_KV_T_COUNTER64, (t))

static const struct kstat_kv cnmac_kstat_tpl[cnmac_stat_count] = {
	[cnmac_stat_rx_toto_gmx]= KVE("rx total gmx",	KSTAT_KV_U_BYTES),
	[cnmac_stat_rx_totp_gmx]= KVE("rx total gmx",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_toto_pip]= KVE("rx total pip",	KSTAT_KV_U_BYTES),
	[cnmac_stat_rx_totp_pip]= KVE("rx total pip",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_h64]	= KVE("rx 64B",		KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_h127]	= KVE("rx 65-127B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_h255]	= KVE("rx 128-255B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_h511]	= KVE("rx 256-511B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_h1023]	= KVE("rx 512-1023B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_h1518]	= KVE("rx 1024-1518B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_hmax]	= KVE("rx 1519-maxB",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_bcast]	= KVE("rx bcast",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_mcast]	= KVE("rx mcast",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_qdpo]	= KVE("rx qos drop",	KSTAT_KV_U_BYTES),
	[cnmac_stat_rx_qdpp]	= KVE("rx qos drop",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_fcs]	= KVE("rx fcs err",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_frag]	= KVE("rx fcs undersize",KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_undersz]	= KVE("rx undersize",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_jabber]	= KVE("rx jabber",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_oversz]	= KVE("rx oversize",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_raw]	= KVE("rx raw",		KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_bad]	= KVE("rx bad",		KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_drop]	= KVE("rx drop",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_ctl]	= KVE("rx control",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_rx_dmac]	= KVE("rx dmac",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_toto]	= KVE("tx total",	KSTAT_KV_U_BYTES),
	[cnmac_stat_tx_totp]	= KVE("tx total",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_hmin]	= KVE("tx min-63B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_h64]	= KVE("tx 64B",		KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_h127]	= KVE("tx 65-127B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_h255]	= KVE("tx 128-255B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_h511]	= KVE("tx 256-511B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_h1023]	= KVE("tx 512-1023B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_h1518]	= KVE("tx 1024-1518B",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_hmax]	= KVE("tx 1519-maxB",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_bcast]	= KVE("tx bcast",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_mcast]	= KVE("tx mcast",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_coll]	= KVE("tx coll",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_defer]	= KVE("tx defer",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_scol]	= KVE("tx scoll",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_mcol]	= KVE("tx mcoll",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_ctl]	= KVE("tx control",	KSTAT_KV_U_PACKETS),
	[cnmac_stat_tx_uflow]	= KVE("tx underflow",	KSTAT_KV_U_PACKETS),
};

void
cnmac_kstat_attach(struct cnmac_softc *sc)
{
	struct kstat *ks;
	struct kstat_kv *kvs;

	mtx_init(&sc->sc_kstat_mtx, IPL_SOFTCLOCK);

	ks = kstat_create(sc->sc_dev.dv_xname, 0, "cnmac-stats", 0,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	kvs = malloc(sizeof(cnmac_kstat_tpl), M_DEVBUF, M_WAITOK | M_ZERO);
	memcpy(kvs, cnmac_kstat_tpl, sizeof(cnmac_kstat_tpl));

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = sc;
	ks->ks_data = kvs;
	ks->ks_datalen = sizeof(cnmac_kstat_tpl);
	ks->ks_read = cnmac_kstat_read;

	sc->sc_kstat = ks;
	kstat_install(ks);
}

int
cnmac_kstat_read(struct kstat *ks)
{
	struct cnmac_softc *sc = ks->ks_softc;
	struct kstat_kv *kvs = ks->ks_data;

	cn30xxpip_kstat_read(sc->sc_pip, kvs);
	cn30xxgmx_kstat_read(sc->sc_gmx_port, kvs);

	getnanouptime(&ks->ks_updated);

	return 0;
}

void
cnmac_kstat_tick(struct cnmac_softc *sc)
{
	if (sc->sc_kstat == NULL)
		return;
	if (!mtx_enter_try(&sc->sc_kstat_mtx))
		return;
	cnmac_kstat_read(sc->sc_kstat);
	mtx_leave(&sc->sc_kstat_mtx);
}
#endif
