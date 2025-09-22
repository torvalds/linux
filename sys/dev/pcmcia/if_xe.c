/*	$OpenBSD: if_xe.c,v 1.64 2024/05/26 08:46:28 jsg Exp $	*/

/*
 * Copyright (c) 1999 Niklas Hallqvist, Brandon Creighton, Job de Haas
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Niklas Hallqvist,
 *	C Stone and Job de Haas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * A driver for Xircom ethernet PC-cards.
 *
 * The driver has been inspired by the xirc2ps_cs.c driver found in Linux'
 * PCMCIA package written by Werner Koch <werner.koch@guug.de>:
 * [xirc2ps_cs.c wk 14.04.97] (1.31 1998/12/09 19:32:55)
 * I will note that no code was used verbatim from that driver as it is under
 * the much too strong GNU General Public License, it was only used as a
 * "specification" of sorts.
 * Other inspirations have been if_fxp.c, if_ep_pcmcia.c and elink3.c as
 * they were found in OpenBSD 2.4.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

/*
 * Maximum number of bytes to read per interrupt.  Linux recommends
 * somewhere between 2000-22000.
 * XXX This is currently a hard maximum.
 */
#define MAX_BYTES_INTR 12000

#include <dev/mii/miivar.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciadevs.h>
#include <dev/pcmcia/if_xereg.h>

#ifdef __GNUC__
#define INLINE	__inline
#else
#define INLINE
#endif	/* __GNUC__ */

#ifdef XEDEBUG

#define XED_CONFIG	0x1
#define XED_MII		0x2
#define XED_INTR	0x4
#define XED_FIFO	0x8

#ifndef XEDEBUG_DEF
#define XEDEBUG_DEF	(XED_CONFIG|XED_INTR)
#endif	/* XEDEBUG_DEF */

int xedebug = XEDEBUG_DEF;

#define DPRINTF(cat, x) if (xedebug & (cat)) printf x

#else	/* XEDEBUG */
#define DPRINTF(cat, x) (void)0
#endif	/* XEDEBUG */

int	xe_pcmcia_match(struct device *, void *, void *);
void	xe_pcmcia_attach(struct device *, struct device *, void *);
int	xe_pcmcia_detach(struct device *, int);
int	xe_pcmcia_activate(struct device *, int);

/*
 * In case this chipset ever turns up out of pcmcia attachments (very
 * unlikely) do the driver splitup.
 */
struct xe_softc {
	struct	device sc_dev;			/* Generic device info */
	u_int32_t	sc_flags;		/* Misc. flags */
	void	*sc_ih;				/* Interrupt handler */
	struct	arpcom sc_arpcom;		/* Ethernet common part */
	struct	ifmedia sc_media;		/* Media control */
	struct	mii_data sc_mii;		/* MII media information */
	int	sc_all_mcasts;			/* Receive all multicasts */
	bus_space_tag_t sc_bst;			/* Bus cookie */
	bus_space_handle_t	sc_bsh;		/* Bus I/O handle */
	bus_size_t	sc_offset;		/* Offset of registers */
	u_int8_t	sc_rev;			/* Chip revision */
};

#define XEF_MOHAWK	0x001
#define XEF_DINGO	0x002
#define XEF_MODEM	0x004
#define XEF_UNSUPPORTED 0x008
#define XEF_CE		0x010
#define XEF_CE2		0x020
#define XEF_CE3		0x040
#define XEF_CE33	0x080
#define XEF_CE56	0x100

struct xe_pcmcia_softc {
	struct	xe_softc sc_xe;			/* Generic device info */
	struct	pcmcia_mem_handle sc_pcmh;	/* PCMCIA memspace info */
	int	sc_mem_window;			/* mem window */
	struct	pcmcia_io_handle sc_pcioh;	/* iospace info */
	int	sc_io_window;			/* io window info */
	struct	pcmcia_function *sc_pf;		/* PCMCIA function */
};

/* Autoconfig definition of driver back-end */
struct cfdriver xe_cd = {
	NULL, "xe", DV_IFNET
};

const struct cfattach xe_pcmcia_ca = {
	sizeof (struct xe_pcmcia_softc), xe_pcmcia_match, xe_pcmcia_attach,
	xe_pcmcia_detach, xe_pcmcia_activate
};

void	xe_cycle_power(struct xe_softc *);
void	xe_full_reset(struct xe_softc *);
void	xe_init(struct xe_softc *);
int	xe_intr(void *);
int	xe_ioctl(struct ifnet *, u_long, caddr_t);
int	xe_mdi_read(struct device *, int, int);
void	xe_mdi_write(struct device *, int, int, int);
int	xe_mediachange(struct ifnet *);
void	xe_mediastatus(struct ifnet *, struct ifmediareq *);
int	xe_pcmcia_funce_enaddr(struct device *, u_int8_t *);
u_int32_t xe_pcmcia_interpret_manfid(struct device *);
int	xe_pcmcia_lan_nid_ciscallback(struct pcmcia_tuple *, void *);
int	xe_pcmcia_manfid_ciscallback(struct pcmcia_tuple *, void *);
u_int16_t xe_get(struct xe_softc *);
void	xe_reset(struct xe_softc *);
void	xe_set_address(struct xe_softc *);
void	xe_start(struct ifnet *);
void	xe_statchg(struct device *);
void	xe_stop(struct xe_softc *);
void	xe_watchdog(struct ifnet *);
#ifdef XEDEBUG
void	xe_reg_dump(struct xe_softc *);
#endif	/* XEDEBUG */

int
xe_pcmcia_match(struct device *parent, void *match, void *aux)
{
	struct pcmcia_attach_args *pa = aux;
	
	if (pa->pf->function != PCMCIA_FUNCTION_NETWORK)
		return (0);

	switch (pa->manufacturer) {
	case PCMCIA_VENDOR_COMPAQ:
	case PCMCIA_VENDOR_COMPAQ2:
		return (0);

	case PCMCIA_VENDOR_INTEL:
	case PCMCIA_VENDOR_XIRCOM:
		/* XXX Per-productid checking here. */
		return (1);

	default:
		return (0);
	}
}

void
xe_pcmcia_attach(struct device *parent, struct device *self, void *aux)
{
	struct xe_pcmcia_softc *psc = (struct xe_pcmcia_softc *)self;
	struct xe_softc *sc = &psc->sc_xe;
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_function *pf = pa->pf;
	struct pcmcia_config_entry *cfe = NULL;
	struct ifnet *ifp;
	u_int8_t myla[ETHER_ADDR_LEN], *enaddr = NULL;
	int state = 0;
	struct pcmcia_mem_handle pcmh;
	int ccr_window;
	bus_size_t ccr_offset;
	const char *intrstr;

	psc->sc_pf = pf;

#if 0
	/* Figure out what card we are. */
	sc->sc_flags = xe_pcmcia_interpret_manfid(parent);
#endif
	if (sc->sc_flags & XEF_UNSUPPORTED) {
		printf(": card unsupported\n");
		goto bad;
	}

	/* Tell the pcmcia framework where the CCR is. */
	pf->ccr_base = 0x800;
	pf->ccr_mask = 0x67;

	/* Fake a cfe. */
	SIMPLEQ_FIRST(&pa->pf->cfe_head) = cfe = (struct pcmcia_config_entry *)
	    malloc(sizeof *cfe, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!cfe) {
		printf(": function enable failed\n");
		return;
	}

	/*
	 * XXX Use preprocessor symbols instead.
	 * Enable ethernet & its interrupts, wiring them to -INT
	 * No I/O base.
	 */
	cfe->number = 0x5;
	cfe->flags = 0;		/* XXX Check! */
	cfe->iftype = PCMCIA_IFTYPE_IO;
	cfe->num_iospace = 0;
	cfe->num_memspace = 0;
	cfe->irqmask = 0x8eb0;

	/* Enable the card. */
	pcmcia_function_init(pa->pf, cfe);
	if (pcmcia_function_enable(pa->pf)) {
		printf(": function enable failed\n");
		goto bad;
	}

	state++;

	if (pcmcia_io_alloc(pa->pf, 0, 16, 16, &psc->sc_pcioh)) {
		printf(": io allocation failed\n");
		goto bad;
	}

	state++;

	if (pcmcia_io_map(pa->pf, PCMCIA_WIDTH_IO16, 0, 16, &psc->sc_pcioh,
		&psc->sc_io_window)) {
		printf(": can't map io space\n");
		goto bad;
	}
	sc->sc_bst = psc->sc_pcioh.iot;
	sc->sc_bsh = psc->sc_pcioh.ioh;
	sc->sc_offset = 0;

	printf(" port 0x%lx/%d", psc->sc_pcioh.addr, 16);

#if 0
	if (pcmcia_mem_alloc(pf, 16, &psc->sc_pcmh)) {
		printf(": pcmcia memory allocation failed\n");
		goto bad;
	}
	state++;

	if (pcmcia_mem_map(pf, PCMCIA_MEM_ATTR, 0x300, 16, &psc->sc_pcmh,
	    &sc->sc_offset, &psc->sc_mem_window)) {
		printf(": pcmcia memory mapping failed\n");
		goto bad;
	}

	sc->sc_bst = psc->sc_pcmh.memt;
	sc->sc_bsh = psc->sc_pcmh.memh;
#endif

	/* Figure out what card we are. */
	sc->sc_flags = xe_pcmcia_interpret_manfid(parent);

	/*
	 * Configuration as advised by DINGO documentation.
	 * We only know about this flag after the manfid interpretation.
	 * Dingo has some extra configuration registers in the CCR space.
	 */
	if (sc->sc_flags & XEF_DINGO) {
		if (pcmcia_mem_alloc(pf, PCMCIA_CCR_SIZE_DINGO, &pcmh)) {
			DPRINTF(XED_CONFIG, ("bad mem alloc\n"));
			goto bad;
		}

		if (pcmcia_mem_map(pf, PCMCIA_MEM_ATTR, pf->ccr_base,
		    PCMCIA_CCR_SIZE_DINGO, &pcmh, &ccr_offset,
		    &ccr_window)) {
			DPRINTF(XED_CONFIG, ("bad mem map\n"));
			pcmcia_mem_free(pf, &pcmh);
			goto bad;
		}

		bus_space_write_1(pcmh.memt, pcmh.memh,
		    ccr_offset + PCMCIA_CCR_DCOR0, PCMCIA_CCR_DCOR0_SFINT);
		bus_space_write_1(pcmh.memt, pcmh.memh,
		    ccr_offset + PCMCIA_CCR_DCOR1,
		    PCMCIA_CCR_DCOR1_FORCE_LEVIREQ | PCMCIA_CCR_DCOR1_D6);
		bus_space_write_1(pcmh.memt, pcmh.memh,
		    ccr_offset + PCMCIA_CCR_DCOR2, 0);
		bus_space_write_1(pcmh.memt, pcmh.memh,
		    ccr_offset + PCMCIA_CCR_DCOR3, 0);
		bus_space_write_1(pcmh.memt, pcmh.memh,
		    ccr_offset + PCMCIA_CCR_DCOR4, 0);

		/* We don't need them anymore and can free them (I think). */
		pcmcia_mem_unmap(pf, ccr_window);
		pcmcia_mem_free(pf, &pcmh);
	}

	/*
	 * Try to get the ethernet address from FUNCE/LAN_NID tuple.
	 */
	if (xe_pcmcia_funce_enaddr(parent, myla))
		enaddr = myla;
	ifp = &sc->sc_arpcom.ac_if;
	if (enaddr)
		bcopy(enaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	else {
		printf(", unable to get ethernet address\n");
		goto bad;
	}

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags =
	    IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = xe_ioctl;
	ifp->if_start = xe_start;
	ifp->if_watchdog = xe_watchdog;

	/* Establish the interrupt. */
	sc->sc_ih = pcmcia_intr_establish(pa->pf, IPL_NET, xe_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(", couldn't establish interrupt\n");
		goto bad;
	}
	intrstr = pcmcia_intr_string(psc->sc_pf, sc->sc_ih);
	printf("%s%s: address %s\n", *intrstr ? ", " : "", intrstr,
	    ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Reset and initialize the card. */
	xe_full_reset(sc);

	/* Initialize our media structures and probe the phy. */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = xe_mdi_read;
	sc->sc_mii.mii_writereg = xe_mdi_write;
	sc->sc_mii.mii_statchg = xe_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, IFM_IMASK, xe_mediachange,
	    xe_mediastatus);
	DPRINTF(XED_MII | XED_CONFIG,
	    ("bmsr %x\n", xe_mdi_read(&sc->sc_dev, 0, 1)));
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY,
	    0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL)
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER | IFM_AUTO, 0,
		    NULL);
	ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_AUTO);

	/*
	 * Attach the interface.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	/*
	 * Reset and initialize the card again for DINGO (as found in Linux
	 * driver).  Without this Dingo will get a watchdog timeout the first
	 * time.  The ugly media tickling seems to be necessary for getting
	 * autonegotiation to work too.
	 */
	if (sc->sc_flags & XEF_DINGO) {
		xe_full_reset(sc);
		xe_init(sc);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_AUTO);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_NONE);
		xe_stop(sc);
	}
	return;

bad:
	if (state > 2)
		pcmcia_io_unmap(pf, psc->sc_io_window);
	if (state > 1)
		pcmcia_io_free(pf, &psc->sc_pcioh);
	if (state > 0)
		pcmcia_function_disable(pa->pf);
	free(cfe, M_DEVBUF, 0);
}

int
xe_pcmcia_detach(struct device *dev, int flags)
{
	struct xe_pcmcia_softc *psc = (struct xe_pcmcia_softc *)dev;
	struct xe_softc *sc = &psc->sc_xe;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int rv = 0;

	mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);

	pcmcia_io_unmap(psc->sc_pf, psc->sc_io_window);
	pcmcia_io_free(psc->sc_pf, &psc->sc_pcioh);

	ether_ifdetach(ifp);
	if_detach(ifp);

	return (rv);
}

int
xe_pcmcia_activate(struct device *dev, int act)
{
	struct xe_pcmcia_softc *sc = (struct xe_pcmcia_softc *)dev;
	struct ifnet *ifp = &sc->sc_xe.sc_arpcom.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			xe_stop(&sc->sc_xe);
		ifp->if_flags &= ~IFF_RUNNING;
		if (sc->sc_xe.sc_ih)
			pcmcia_intr_disestablish(sc->sc_pf, sc->sc_xe.sc_ih);
		sc->sc_xe.sc_ih = NULL;
		pcmcia_function_disable(sc->sc_pf);
		break;
	case DVACT_RESUME:
		pcmcia_function_enable(sc->sc_pf);
		sc->sc_xe.sc_ih = pcmcia_intr_establish(sc->sc_pf, IPL_NET,
		    xe_intr, sc, sc->sc_xe.sc_dev.dv_xname);
		/* XXX this is a ridiculous */
		xe_reset(&sc->sc_xe);
		if ((ifp->if_flags & IFF_UP) == 0)
			xe_stop(&sc->sc_xe);
		break;
	case DVACT_DEACTIVATE:
		ifp->if_timer = 0;
		ifp->if_flags &= ~IFF_RUNNING;
		if (sc->sc_xe.sc_ih)
			pcmcia_intr_disestablish(sc->sc_pf, sc->sc_xe.sc_ih);
		sc->sc_xe.sc_ih = NULL;
		pcmcia_function_disable(sc->sc_pf);
		break;
	}
	return (0);
}

/*
 * XXX These two functions might be OK to factor out into pcmcia.c since
 * if_sm_pcmcia.c uses similar ones.
 */
int
xe_pcmcia_funce_enaddr(struct device *parent, u_int8_t *myla)
{
	/* XXX The Linux driver has more ways to do this in case of failure. */
	return (pcmcia_scan_cis(parent, xe_pcmcia_lan_nid_ciscallback, myla));
}

int
xe_pcmcia_lan_nid_ciscallback(struct pcmcia_tuple *tuple, void *arg)
{
	u_int8_t *myla = arg;
	int i;

	if (tuple->code == PCMCIA_CISTPL_FUNCE) {
		if (tuple->length < 2)
			return (0);

		switch (pcmcia_tuple_read_1(tuple, 0)) {
		case PCMCIA_TPLFE_TYPE_LAN_NID:
			if (pcmcia_tuple_read_1(tuple, 1) != ETHER_ADDR_LEN)
				return (0);
			break;

		case 0x02:
			/*
			 * Not sure about this, I don't have a CE2
			 * that puts the ethernet addr here.
			 */
		 	if (pcmcia_tuple_read_1(tuple, 1) != 13)
				return (0);
			break;

		default:
			return (0);
		}

		for (i = 0; i < ETHER_ADDR_LEN; i++)
			myla[i] = pcmcia_tuple_read_1(tuple, i + 2);
		return (1);
	}

	/* Yet another spot where this might be. */
	if (tuple->code == 0x89) {
		pcmcia_tuple_read_1(tuple, 1);
		for (i = 0; i < ETHER_ADDR_LEN; i++)
			myla[i] = pcmcia_tuple_read_1(tuple, i + 2);
		return (1);
	}
	return (0);
}

u_int32_t
xe_pcmcia_interpret_manfid(struct device *parent)
{
	u_int32_t flags = 0;
	struct pcmcia_softc *psc = (struct pcmcia_softc *)parent;
	char *tptr;

	if (!pcmcia_scan_cis(parent, xe_pcmcia_manfid_ciscallback, &flags))
		return (XEF_UNSUPPORTED);

	if (flags & XEF_CE) {
		tptr = memchr(psc->card.cis1_info[2], 'C',
		    strlen(psc->card.cis1_info[2]));
		/* XXX not sure if other CE2s hide "CE2" in different places */
		if (tptr && *(tptr + 1) == 'E' && *(tptr + 2) == '2') {
			flags ^= (XEF_CE | XEF_UNSUPPORTED);
			flags |= XEF_CE2;
		}
	}
	return (flags);
}

int
xe_pcmcia_manfid_ciscallback(struct pcmcia_tuple *tuple, void *arg)
{
	u_int32_t *flagsp = arg;
	u_int8_t media, product;

	if (tuple->code == PCMCIA_CISTPL_MANFID) {
		if (tuple->length < 2)
			return (0);

		media = pcmcia_tuple_read_1(tuple, 3);
		product = pcmcia_tuple_read_1(tuple, 4);

		if (!(product & XEPROD_CREDITCARD) ||
		    !(media & XEMEDIA_ETHER)) {
			*flagsp |= XEF_UNSUPPORTED;
			return (1);
		}

		if (media & XEMEDIA_MODEM)
			*flagsp |= XEF_MODEM;

		switch (product & XEPROD_IDMASK) {
		case 1:
			/* XXX Can be CE2 too (we double-check later). */
			*flagsp |= XEF_CE | XEF_UNSUPPORTED;
			break;
		case 2:
			*flagsp |= XEF_CE2;
			break;
		case 3:
			if (!(*flagsp & XEF_MODEM))
				*flagsp |= XEF_MOHAWK;
			*flagsp |= XEF_CE3;
			break;
		case 4:
			*flagsp |= XEF_CE33;
			break;
		case 5:
			*flagsp |= XEF_CE56 | XEF_MOHAWK;
			break;
		case 6:
		case 7:
			*flagsp |= XEF_CE56 | XEF_MOHAWK | XEF_DINGO;
			break;
		default:
			*flagsp |= XEF_UNSUPPORTED;
			break;
		}

		return (1);
	}
	return (0);
}

int
xe_intr(void *arg)
{
	struct xe_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int8_t esr, rsr, isr, rx_status, savedpage;
	u_int16_t tx_status, recvcount = 0, tempint;

	ifp->if_timer = 0;	/* turn watchdog timer off */

	if (sc->sc_flags & XEF_MOHAWK) {
		/* Disable interrupt (Linux does it). */
		bus_space_write_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + CR,
		    0);
	}

	savedpage =
	    bus_space_read_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + PR);

	PAGE(sc, 0);
	esr = bus_space_read_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + ESR);
	isr = bus_space_read_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + ISR0);
	rsr = bus_space_read_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + RSR);
				
	/* Check to see if card has been ejected. */
	if (isr == 0xff) {
		printf("%s: interrupt for dead card\n", sc->sc_dev.dv_xname);
		goto end;
	}

	PAGE(sc, 40);
	rx_status =
	    bus_space_read_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + RXST0);
	tx_status =
	    bus_space_read_2(sc->sc_bst, sc->sc_bsh, sc->sc_offset + TXST0);

	/*
	 * XXX Linux writes to RXST0 and TXST* here.  My CE2 works just fine
	 * without it, and I can't see an obvious reason for it.
	 */

	PAGE(sc, 0);
	while (esr & FULL_PKT_RCV) {
		if (!(rsr & RSR_RX_OK))
			break;

		/* Compare bytes read this interrupt to hard maximum. */
		if (recvcount > MAX_BYTES_INTR) {
			DPRINTF(XED_INTR,
			    ("%s: too many bytes this interrupt\n",
			    sc->sc_dev.dv_xname));
			ifp->if_iqdrops++;
			/* Drop packet. */
			bus_space_write_2(sc->sc_bst, sc->sc_bsh,
			    sc->sc_offset + DO0, DO_SKIP_RX_PKT);
		}
		tempint = xe_get(sc);
		recvcount += tempint;
		esr = bus_space_read_1(sc->sc_bst, sc->sc_bsh,
		    sc->sc_offset + ESR);
		rsr = bus_space_read_1(sc->sc_bst, sc->sc_bsh,
		    sc->sc_offset + RSR);
	}
	
	/* Packet too long? */
	if (rsr & RSR_TOO_LONG) {
		ifp->if_ierrors++;
		DPRINTF(XED_INTR,
		    ("%s: packet too long\n", sc->sc_dev.dv_xname));
	}

	/* CRC error? */
	if (rsr & RSR_CRCERR) {
		ifp->if_ierrors++;
		DPRINTF(XED_INTR,
		    ("%s: CRC error detected\n", sc->sc_dev.dv_xname));
	}

	/* Alignment error? */
	if (rsr & RSR_ALIGNERR) {
		ifp->if_ierrors++;
		DPRINTF(XED_INTR,
		    ("%s: alignment error detected\n", sc->sc_dev.dv_xname));
	}

	/* Check for rx overrun. */
	if (rx_status & RX_OVERRUN) {
		bus_space_write_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + CR,
		    CLR_RX_OVERRUN);
		DPRINTF(XED_INTR, ("overrun cleared\n"));
	}
			
	/* Try to start more packets transmitting. */
	if (ifq_empty(&ifp->if_snd) == 0)
		xe_start(ifp);

	/* Detected excessive collisions? */
	if (tx_status & EXCESSIVE_COLL) {
		DPRINTF(XED_INTR,
		    ("%s: excessive collisions\n", sc->sc_dev.dv_xname));
		bus_space_write_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + CR,
		    RESTART_TX);
		ifp->if_oerrors++;
	}
	
	if (tx_status & TX_ABORT)
		ifp->if_oerrors++;

end:
	/* Reenable interrupts. */
	PAGE(sc, savedpage);
	bus_space_write_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + CR,
	    ENABLE_INT);

	return (1);
}

u_int16_t
xe_get(struct xe_softc *sc)
{
	u_int8_t rsr;
	struct mbuf *top, **mp, *m;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int16_t pktlen, len, recvcount = 0;
	u_int8_t *data;
	
	PAGE(sc, 0);
	rsr = bus_space_read_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + RSR);

	pktlen =
	    bus_space_read_2(sc->sc_bst, sc->sc_bsh, sc->sc_offset + RBC0) &
	    RBC_COUNT_MASK;
	if (pktlen == 0) {
		/*
		 * XXX At least one CE2 sets RBC0 == 0 occasionally, and only
		 * when MPE is set.  It is not known why.
		 */
		return (0);
	}
	recvcount += pktlen;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (recvcount);
	m->m_pkthdr.len = pktlen;
	len = MHLEN;
	top = 0;
	mp = &top;
	
	while (pktlen > 0) {
		if (top) {
			MGET(m, M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				m_freem(top);
				return (recvcount);
			}
			len = MLEN;
		}
		if (pktlen >= MINCLSIZE) {
			MCLGET(m, M_DONTWAIT);
			if (!(m->m_flags & M_EXT)) {
				m_freem(m);
				m_freem(top);
				return (recvcount);
			}
			len = MCLBYTES;
		}
		if (!top) {
			caddr_t newdata = (caddr_t)ALIGN(m->m_data +
			    sizeof (struct ether_header)) -
			    sizeof (struct ether_header);
			len -= newdata - m->m_data;
			m->m_data = newdata;
		}
		len = min(pktlen, len);

		data = mtod(m, u_int8_t *);
		if (len > 1) {
		        len &= ~1;
			bus_space_read_raw_multi_2(sc->sc_bst, sc->sc_bsh,
			    sc->sc_offset + EDP, data, len);
		} else
			*data = bus_space_read_1(sc->sc_bst, sc->sc_bsh,
			    sc->sc_offset + EDP);
		m->m_len = len;
		pktlen -= len;
		*mp = m;
		mp = &m->m_next;
	}

	/* Skip Rx packet. */
	bus_space_write_2(sc->sc_bst, sc->sc_bsh, sc->sc_offset + DO0,
	    DO_SKIP_RX_PKT);

	ml_enqueue(&ml, top);
	if_input(ifp, &ml);

	return (recvcount);
}


/*
 * Serial management for the MII.
 * The DELAY's below stem from the fact that the maximum frequency
 * acceptable on the MDC pin is 2.5 MHz and fast processors can easily
 * go much faster than that.
 */

/* Let the MII serial management be idle for one period. */
static INLINE void xe_mdi_idle(struct xe_softc *);
static INLINE void
xe_mdi_idle(struct xe_softc *sc)
{
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	bus_size_t offset = sc->sc_offset;

	/* Drive MDC low... */
	bus_space_write_1(bst, bsh, offset + GP2, MDC_LOW);
	DELAY(1);

	/* and high again. */
	bus_space_write_1(bst, bsh, offset + GP2, MDC_HIGH);
	DELAY(1);
}

/* Pulse out one bit of data. */
static INLINE void xe_mdi_pulse(struct xe_softc *, int);
static INLINE void
xe_mdi_pulse(struct xe_softc *sc, int data)
{
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	bus_size_t offset = sc->sc_offset;
	u_int8_t bit = data ? MDIO_HIGH : MDIO_LOW;

	/* First latch the data bit MDIO with clock bit MDC low...*/
	bus_space_write_1(bst, bsh, offset + GP2, bit | MDC_LOW);
	DELAY(1);

	/* then raise the clock again, preserving the data bit. */
	bus_space_write_1(bst, bsh, offset + GP2, bit | MDC_HIGH);
	DELAY(1);
}

/* Probe one bit of data. */
static INLINE int xe_mdi_probe(struct xe_softc *sc);
static INLINE int
xe_mdi_probe(struct xe_softc *sc)
{
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	bus_size_t offset = sc->sc_offset;
	u_int8_t x;

	/* Pull clock bit MDCK low... */
	bus_space_write_1(bst, bsh, offset + GP2, MDC_LOW);
	DELAY(1);

	/* Read data and drive clock high again. */
	x = bus_space_read_1(bst, bsh, offset + GP2) & MDIO;
	bus_space_write_1(bst, bsh, offset + GP2, MDC_HIGH);
	DELAY(1);

	return (x);
}

/* Pulse out a sequence of data bits. */
static INLINE void xe_mdi_pulse_bits(struct xe_softc *, u_int32_t, int);
static INLINE void
xe_mdi_pulse_bits(struct xe_softc *sc, u_int32_t data, int len)
{
	u_int32_t mask;

	for (mask = 1 << (len - 1); mask; mask >>= 1)
		xe_mdi_pulse(sc, data & mask);
}

/* Read a PHY register. */
int
xe_mdi_read(struct device *self, int phy, int reg)
{
	struct xe_softc *sc = (struct xe_softc *)self;
	int i;
	u_int32_t mask;
	u_int32_t data = 0;

	PAGE(sc, 2);
	for (i = 0; i < 32; i++)	/* Synchronize. */
		xe_mdi_pulse(sc, 1);
	xe_mdi_pulse_bits(sc, 0x06, 4); /* Start + Read opcode */
	xe_mdi_pulse_bits(sc, phy, 5);	/* PHY address */
	xe_mdi_pulse_bits(sc, reg, 5);	/* PHY register */
	xe_mdi_idle(sc);		/* Turn around. */
	xe_mdi_probe(sc);		/* Drop initial zero bit. */

	for (mask = 1 << 15; mask; mask >>= 1)
		if (xe_mdi_probe(sc))
			data |= mask;
	xe_mdi_idle(sc);

	DPRINTF(XED_MII,
	    ("xe_mdi_read: phy %d reg %d -> %x\n", phy, reg, data));
	return (data);
}

/* Write a PHY register. */
void
xe_mdi_write(struct device *self, int phy, int reg, int value)
{
	struct xe_softc *sc = (struct xe_softc *)self;
	int i;

	PAGE(sc, 2);
	for (i = 0; i < 32; i++)	/* Synchronize. */
		xe_mdi_pulse(sc, 1);
	xe_mdi_pulse_bits(sc, 0x05, 4); /* Start + Write opcode */
	xe_mdi_pulse_bits(sc, phy, 5);	/* PHY address */
	xe_mdi_pulse_bits(sc, reg, 5);	/* PHY register */
	xe_mdi_pulse_bits(sc, 0x02, 2); /* Turn around. */
	xe_mdi_pulse_bits(sc, value, 16);	/* Write the data */
	xe_mdi_idle(sc);		/* Idle away. */

	DPRINTF(XED_MII,
	    ("xe_mdi_write: phy %d reg %d val %x\n", phy, reg, value));
}

void
xe_statchg(struct device *self)
{
}

/*
 * Change media according to request.
 */
int
xe_mediachange(struct ifnet *ifp)
{
	if (ifp->if_flags & IFF_UP)
		xe_init(ifp->if_softc);
	return (0);
}

/*
 * Notify the world which media we're using.
 */
void
xe_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct xe_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

void
xe_reset(struct xe_softc *sc)
{
	int s;

	s = splnet();
	xe_stop(sc);
	xe_full_reset(sc);
	xe_init(sc);
	splx(s);
}

void
xe_watchdog(struct ifnet *ifp)
{
	struct xe_softc *sc = ifp->if_softc;

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	++sc->sc_arpcom.ac_if.if_oerrors;

	xe_reset(sc);
}

void
xe_stop(struct xe_softc *sc)
{
	/* Disable interrupts. */
	PAGE(sc, 0);
	bus_space_write_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + CR, 0);

	PAGE(sc, 1);
	bus_space_write_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + IMR0, 0);
	
	/* Power down, wait. */
	PAGE(sc, 4);
	bus_space_write_1(sc->sc_bst, sc->sc_bsh, sc->sc_offset + GP1, 0);
	DELAY(40000);
	
	/* Cancel watchdog timer. */
	sc->sc_arpcom.ac_if.if_timer = 0;
}

void
xe_init(struct xe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int s;

	DPRINTF(XED_CONFIG, ("xe_init\n"));

	s = splnet();

	xe_set_address(sc);

	/* Set current media. */
	mii_mediachg(&sc->sc_mii);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	splx(s);
}

/*
 * Start outputting on the interface.
 * Always called as splnet().
 */
void
xe_start(struct ifnet *ifp)
{
	struct xe_softc *sc = ifp->if_softc;
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	bus_size_t offset = sc->sc_offset;
	unsigned int s, len, pad = 0;
	struct mbuf *m0, *m;
	u_int16_t space;

	/* Don't transmit if interface is busy or not running. */
	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	/* Peek at the next packet. */
	m0 = ifq_deq_begin(&ifp->if_snd);
	if (m0 == NULL)
		return;

	/* We need to use m->m_pkthdr.len, so require the header. */
	if (!(m0->m_flags & M_PKTHDR))
		panic("xe_start: no header mbuf");

	len = m0->m_pkthdr.len;

	/* Pad to ETHER_MIN_LEN - ETHER_CRC_LEN. */
	if (len < ETHER_MIN_LEN - ETHER_CRC_LEN)
		pad = ETHER_MIN_LEN - ETHER_CRC_LEN - len;

	PAGE(sc, 0);
	space = bus_space_read_2(bst, bsh, offset + TSO0) & 0x7fff;
	if (len + pad + 2 > space) {
		ifq_deq_rollback(&ifp->if_snd, m0);
		DPRINTF(XED_FIFO,
		    ("%s: not enough space in output FIFO (%d > %d)\n",
		    sc->sc_dev.dv_xname, len + pad + 2, space));
		return;
	}

	ifq_deq_commit(&ifp->if_snd, m0);

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif

	/*
	 * Do the output at splhigh() so that an interrupt from another device
	 * won't cause a FIFO underrun.
	 */
	s = splhigh();

	bus_space_write_2(bst, bsh, offset + TSO2, (u_int16_t)len + pad + 2);
	bus_space_write_2(bst, bsh, offset + EDP, (u_int16_t)len + pad);
	for (m = m0; m; ) {
		if (m->m_len > 1)
			bus_space_write_raw_multi_2(bst, bsh, offset + EDP,
			    mtod(m, u_int8_t *), m->m_len & ~1);
		if (m->m_len & 1)
			bus_space_write_1(bst, bsh, offset + EDP,
			    *(mtod(m, u_int8_t *) + m->m_len - 1));
		m0 = m_free(m);
		m = m0;
	}
	if (sc->sc_flags & XEF_MOHAWK)
		bus_space_write_1(bst, bsh, offset + CR, TX_PKT | ENABLE_INT);
	else {
		for (; pad > 1; pad -= 2)
			bus_space_write_2(bst, bsh, offset + EDP, 0);
		if (pad == 1)
			bus_space_write_1(bst, bsh, offset + EDP, 0);
	}

	splx(s);

	ifp->if_timer = 5;
}

int
xe_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct xe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		xe_init(sc);
		break;

	case SIOCSIFFLAGS:
		sc->sc_all_mcasts = (ifp->if_flags & IFF_ALLMULTI) ? 1 : 0;
				
		PAGE(sc, 0x42);
		if ((ifp->if_flags & IFF_PROMISC) ||
		    (ifp->if_flags & IFF_ALLMULTI))
			bus_space_write_1(sc->sc_bst, sc->sc_bsh,
			    sc->sc_offset + SWC1,
			    SWC1_PROMISC | SWC1_MCAST_PROM);
		else
			bus_space_write_1(sc->sc_bst, sc->sc_bsh,
			    sc->sc_offset + SWC1, 0);

		/*
		 * If interface is marked up and not running, then start it.
		 * If it is marked down and running, stop it.
		 * XXX If it's up then re-initialize it. This is so flags
		 * such as IFF_PROMISC are handled.
		 */
		if (ifp->if_flags & IFF_UP) {
			xe_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				xe_stop(sc);
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		sc->sc_all_mcasts = (ifp->if_flags & IFF_ALLMULTI) ? 1 : 0;
		error = (command == SIOCADDMULTI) ?
		    ether_addmulti(ifr, &sc->sc_arpcom) :
		    ether_delmulti(ifr, &sc->sc_arpcom);

		if (error == ENETRESET) {
			/*
			 * Multicast list has changed; set the hardware
			 * filter accordingly.
			 */
			if (!sc->sc_all_mcasts &&
			    !(ifp->if_flags & IFF_PROMISC))
				xe_set_address(sc);

			/*
			 * xe_set_address() can turn on all_mcasts if we run
			 * out of space, so check it again rather than else {}.
			 */
			if (sc->sc_all_mcasts)
				xe_init(sc);
			error = 0;
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error =
		    ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, command);
		break;

	default:
		error = ENOTTY;
	}

	splx(s);
	return (error);
}

void
xe_set_address(struct xe_softc *sc)
{
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	bus_size_t offset = sc->sc_offset;
	struct arpcom *arp = &sc->sc_arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int i, page, pos, num;

	PAGE(sc, 0x50);
	for (i = 0; i < 6; i++) {
		bus_space_write_1(bst, bsh, offset + IA + i,
		    sc->sc_arpcom.ac_enaddr[(sc->sc_flags & XEF_MOHAWK) ?
		    5 - i : i]);
	}

	if (arp->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		sc->sc_all_mcasts=1;
	} else if (arp->ac_multicnt > 0) {
		if (arp->ac_multicnt > 9) {
			PAGE(sc, 0x42);
			bus_space_write_1(sc->sc_bst, sc->sc_bsh,
			    sc->sc_offset + SWC1,
			    SWC1_PROMISC | SWC1_MCAST_PROM);
			return;
		}

		ETHER_FIRST_MULTI(step, arp, enm);

		pos = IA + 6;
		for (page = 0x50, num = arp->ac_multicnt; num > 0 && enm;
		    num--) {
			for (i = 0; i < 6; i++) {
				bus_space_write_1(bst, bsh, offset + pos,
				    enm->enm_addrlo[
				    (sc->sc_flags & XEF_MOHAWK) ? 5 - i : i]);

				if (++pos > 15) {
					pos = IA;
					page++;
					PAGE(sc, page);
				}
			}
		}
	}
}

void
xe_cycle_power(struct xe_softc *sc)
{
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	bus_size_t offset = sc->sc_offset;

	PAGE(sc, 4);
	DELAY(1);
	bus_space_write_1(bst, bsh, offset + GP1, 0);
	DELAY(40000);
	if (sc->sc_flags & XEF_MOHAWK)
		bus_space_write_1(bst, bsh, offset + GP1, POWER_UP);
	else
		/* XXX What is bit 2 (aka AIC)? */
		bus_space_write_1(bst, bsh, offset + GP1, POWER_UP | 4);
	DELAY(20000);
}

void
xe_full_reset(struct xe_softc *sc)
{
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	bus_size_t offset = sc->sc_offset;

	/* Do an as extensive reset as possible on all functions. */
	xe_cycle_power(sc);
	bus_space_write_1(bst, bsh, offset + CR, SOFT_RESET);
	DELAY(20000);
	bus_space_write_1(bst, bsh, offset + CR, 0);
	DELAY(20000);
	if (sc->sc_flags & XEF_MOHAWK) {
		PAGE(sc, 4);
		/*
		 * Drive GP1 low to power up ML6692 and GP2 high to power up
		 * the 10MHz chip.  XXX What chip is that?  The phy?
		 */
		bus_space_write_1(bst, bsh, offset + GP0,
		    GP1_OUT | GP2_OUT | GP2_WR);
	}
	DELAY(500000);

	/* Get revision information.  XXX Symbolic constants. */
	sc->sc_rev = bus_space_read_1(bst, bsh, offset + BV) &
	    ((sc->sc_flags & XEF_MOHAWK) ? 0x70 : 0x30) >> 4;

	/* Media selection.  XXX Maybe manual overriding too? */
	if (!(sc->sc_flags & XEF_MOHAWK)) {
		PAGE(sc, 4);
		/*
		 * XXX I have no idea what this really does, it is from the
		 * Linux driver.
		 */
		bus_space_write_1(bst, bsh, offset + GP0, GP1_OUT);
	}
	DELAY(40000);

	/* Setup the ethernet interrupt mask. */
	PAGE(sc, 1);
	bus_space_write_1(bst, bsh, offset + IMR0,
	    ISR_TX_OFLOW | ISR_PKT_TX | ISR_MAC_INT | /* ISR_RX_EARLY | */
	    ISR_RX_FULL | ISR_RX_PKT_REJ | ISR_FORCED_INT);
#if 0
	bus_space_write_1(bst, bsh, offset + IMR0, 0xff);
#endif
	if (!(sc->sc_flags & XEF_DINGO))
		/* XXX What is this?  Not for Dingo at least. */
		bus_space_write_1(bst, bsh, offset + IMR1, 1);

	/*
	 * Disable source insertion.
	 * XXX Dingo does not have this bit, but Linux does it unconditionally.
	 */
	if (!(sc->sc_flags & XEF_DINGO)) {
		PAGE(sc, 0x42);
		bus_space_write_1(bst, bsh, offset + SWC0, 0x20);
	}

	/* Set the local memory dividing line. */
	if (sc->sc_rev != 1) {
		PAGE(sc, 2);
		/* XXX Symbolic constant preferable. */
		bus_space_write_2(bst, bsh, offset + RBS0, 0x2000);
	}

	xe_set_address(sc);

	/*
	 * Apparently the receive byte pointer can be bad after a reset, so
	 * we hardwire it correctly.
	 */
	PAGE(sc, 0);
	bus_space_write_2(bst, bsh, offset + DO0, DO_CHG_OFFSET);

	/* Setup ethernet MAC registers. XXX Symbolic constants. */
	PAGE(sc, 0x40);
	bus_space_write_1(bst, bsh, offset + RX0MSK,
	    PKT_TOO_LONG | CRC_ERR | RX_OVERRUN | RX_ABORT | RX_OK);
	bus_space_write_1(bst, bsh, offset + TX0MSK,
	    CARRIER_LOST | EXCESSIVE_COLL | TX_UNDERRUN | LATE_COLLISION |
	    SQE | TX_ABORT | TX_OK);
	if (!(sc->sc_flags & XEF_DINGO))
		/* XXX From Linux, dunno what 0xb0 means. */
		bus_space_write_1(bst, bsh, offset + TX1MSK, 0xb0);
	bus_space_write_1(bst, bsh, offset + RXST0, 0);
	bus_space_write_1(bst, bsh, offset + TXST0, 0);
	bus_space_write_1(bst, bsh, offset + TXST1, 0);

	/* Enable MII function if available. */
	if (LIST_FIRST(&sc->sc_mii.mii_phys)) {
		PAGE(sc, 2);
		bus_space_write_1(bst, bsh, offset + MSR,
		    bus_space_read_1(bst, bsh, offset + MSR) | SELECT_MII);
		DELAY(20000);
	} else {
		PAGE(sc, 0);
				
		/* XXX Do we need to do this? */
		PAGE(sc, 0x42);
		bus_space_write_1(bst, bsh, offset + SWC1, SWC1_AUTO_MEDIA);
		DELAY(50000);

		/* XXX Linux probes the media here. */
	}

	/* Configure the LED registers. */
	PAGE(sc, 2);

	/* XXX This is not good for 10base2. */
	bus_space_write_1(bst, bsh, offset + LED,
	    LED_TX_ACT << LED1_SHIFT | LED_10MB_LINK << LED0_SHIFT);
	if (sc->sc_flags & XEF_DINGO)
		bus_space_write_1(bst, bsh, offset + LED3,
		    LED_100MB_LINK << LED3_SHIFT);

	/* Enable receiver and go online. */
	PAGE(sc, 0x40);
	bus_space_write_1(bst, bsh, offset + CMD0, ENABLE_RX | ONLINE);

#if 0
	/* XXX Linux does this here - is it necessary? */
	PAGE(sc, 1);
	bus_space_write_1(bst, bsh, offset + IMR0, 0xff);
	if (!(sc->sc_flags & XEF_DINGO))
		/* XXX What is this?  Not for Dingo at least. */
		bus_space_write_1(bst, bsh, offset + IMR1, 1);
#endif

       /* Enable interrupts. */
	PAGE(sc, 0);
	bus_space_write_1(bst, bsh, offset + CR, ENABLE_INT);

	/* XXX This is pure magic for me, found in the Linux driver. */
	if ((sc->sc_flags & (XEF_DINGO | XEF_MODEM)) == XEF_MODEM) {
		if ((bus_space_read_1(bst, bsh, offset + 0x10) & 0x01) == 0)
			/* Unmask the master interrupt bit. */
			bus_space_write_1(bst, bsh, offset + 0x10, 0x11);
	}

	/*
	 * The Linux driver says this:
	 * We should switch back to page 0 to avoid a bug in revision 0
	 * where regs with offset below 8 can't be read after an access
	 * to the MAC registers.
	 */
	PAGE(sc, 0);
}

#ifdef XEDEBUG
void
xe_reg_dump(struct xe_softc *sc)
{
	int page, i;
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	bus_size_t offset = sc->sc_offset;

	printf("%x: Common registers: ", sc->sc_dev.dv_xname);
	for (i = 0; i < 8; i++) {
		printf(" %2.2x", bus_space_read_1(bst, bsh, offset + i));
	}
	printf("\n");

	for (page = 0; page < 8; page++) {
		printf("%s: Register page %2.2x: ", sc->sc_dev.dv_xname, page);
		PAGE(sc, page);
		for (i = 8; i < 16; i++) {
			printf(" %2.2x",
			    bus_space_read_1(bst, bsh, offset + i));
		}
		printf("\n");
	}

	for (page = 0x40; page < 0x5f; page++) {
		if (page == 0x43 || (page >= 0x46 && page <= 0x4f) ||
		    (page >= 0x51 && page <= 0x5e))
			continue;
		printf("%s: Register page %2.2x: ", sc->sc_dev.dv_xname, page);
		PAGE(sc, page);
		for (i = 8; i < 16; i++) {
			printf(" %2.2x",
			    bus_space_read_1(bst, bsh, offset + i));
		}
		printf("\n");
	}
}
#endif	/* XEDEBUG */
