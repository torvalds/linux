/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
/*
 * Aironet 4500/4800 802.11 PCMCIA/ISA/PCI driver for FreeBSD.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * The Aironet 4500/4800 series cards come in PCMCIA, ISA and PCI form.
 * This driver supports all three device types (PCI devices are supported
 * through an extra PCI shim: /sys/dev/an/if_an_pci.c). ISA devices can be
 * supported either using hard-coded IO port/IRQ settings or via Plug
 * and Play. The 4500 series devices support 1Mbps and 2Mbps data rates.
 * The 4800 devices support 1, 2, 5.5 and 11Mbps rates.
 *
 * Like the WaveLAN/IEEE cards, the Aironet NICs are all essentially
 * PCMCIA devices. The ISA and PCI cards are a combination of a PCMCIA
 * device and a PCMCIA to ISA or PCMCIA to PCI adapter card. There are
 * a couple of important differences though:
 *
 * - Lucent ISA card looks to the host like a PCMCIA controller with
 *   a PCMCIA WaveLAN card inserted. This means that even desktop
 *   machines need to be configured with PCMCIA support in order to
 *   use WaveLAN/IEEE ISA cards. The Aironet cards on the other hand
 *   actually look like normal ISA and PCI devices to the host, so
 *   no PCMCIA controller support is needed
 *
 * The latter point results in a small gotcha. The Aironet PCMCIA
 * cards can be configured for one of two operating modes depending
 * on how the Vpp1 and Vpp2 programming voltages are set when the
 * card is activated. In order to put the card in proper PCMCIA
 * operation (where the CIS table is visible and the interface is
 * programmed for PCMCIA operation), both Vpp1 and Vpp2 have to be
 * set to 5 volts. FreeBSD by default doesn't set the Vpp voltages,
 * which leaves the card in ISA/PCI mode, which prevents it from
 * being activated as an PCMCIA device.
 *
 * Note that some PCMCIA controller software packages for Windows NT
 * fail to set the voltages as well.
 *
 * The Aironet devices can operate in both station mode and access point
 * mode. Typically, when programmed for station mode, the card can be set
 * to automatically perform encapsulation/decapsulation of Ethernet II
 * and 802.3 frames within 802.11 frames so that the host doesn't have
 * to do it itself. This driver doesn't program the card that way: the
 * driver handles all of the encapsulation/decapsulation itself.
 */

#include "opt_inet.h"

#ifdef INET
#define ANCACHE			/* enable signal strength cache */
#endif

#include <sys/param.h>
#include <sys/ctype.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#ifdef ANCACHE
#include <sys/syslog.h>
#endif
#include <sys/sysctl.h>

#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/resource.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/ethernet.h>
#include <net/if_types.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#include <net/bpf.h>

#include <machine/md_var.h>

#include <dev/an/if_aironet_ieee.h>
#include <dev/an/if_anreg.h>

/* These are global because we need them in sys/pci/if_an_p.c. */
static void an_reset(struct an_softc *);
static int an_init_mpi350_desc(struct an_softc *);
static int an_ioctl(struct ifnet *, u_long, caddr_t);
static void an_init(void *);
static void an_init_locked(struct an_softc *);
static int an_init_tx_ring(struct an_softc *);
static void an_start(struct ifnet *);
static void an_start_locked(struct ifnet *);
static void an_watchdog(struct an_softc *);
static void an_rxeof(struct an_softc *);
static void an_txeof(struct an_softc *, int);

static void an_promisc(struct an_softc *, int);
static int an_cmd(struct an_softc *, int, int);
static int an_cmd_struct(struct an_softc *, struct an_command *,
    struct an_reply *);
static int an_read_record(struct an_softc *, struct an_ltv_gen *);
static int an_write_record(struct an_softc *, struct an_ltv_gen *);
static int an_read_data(struct an_softc *, int, int, caddr_t, int);
static int an_write_data(struct an_softc *, int, int, caddr_t, int);
static int an_seek(struct an_softc *, int, int, int);
static int an_alloc_nicmem(struct an_softc *, int, int *);
static int an_dma_malloc(struct an_softc *, bus_size_t, struct an_dma_alloc *,
    int);
static void an_dma_free(struct an_softc *, struct an_dma_alloc *);
static void an_dma_malloc_cb(void *, bus_dma_segment_t *, int, int);
static void an_stats_update(void *);
static void an_setdef(struct an_softc *, struct an_req *);
#ifdef ANCACHE
static void an_cache_store(struct an_softc *, struct ether_header *,
    struct mbuf *, u_int8_t, u_int8_t);
#endif

/* function definitions for use with the Cisco's Linux configuration
   utilities
*/

static int readrids(struct ifnet*, struct aironet_ioctl*);
static int writerids(struct ifnet*, struct aironet_ioctl*);
static int flashcard(struct ifnet*, struct aironet_ioctl*);

static int cmdreset(struct ifnet *);
static int setflashmode(struct ifnet *);
static int flashgchar(struct ifnet *,int,int);
static int flashpchar(struct ifnet *,int,int);
static int flashputbuf(struct ifnet *);
static int flashrestart(struct ifnet *);
static int WaitBusy(struct ifnet *, int);
static int unstickbusy(struct ifnet *);

static void an_dump_record	(struct an_softc *,struct an_ltv_gen *,
				    char *);

static int an_media_change	(struct ifnet *);
static void an_media_status	(struct ifnet *, struct ifmediareq *);

static int	an_dump = 0;
static int	an_cache_mode = 0;

#define DBM 0
#define PERCENT 1
#define RAW 2

static char an_conf[256];
static char an_conf_cache[256];

/* sysctl vars */

static SYSCTL_NODE(_hw, OID_AUTO, an, CTLFLAG_RD, 0,
    "Wireless driver parameters");

/* XXX violate ethernet/netgraph callback hooks */
extern	void	(*ng_ether_attach_p)(struct ifnet *ifp);
extern	void	(*ng_ether_detach_p)(struct ifnet *ifp);

static int
sysctl_an_dump(SYSCTL_HANDLER_ARGS)
{
	int	error, r, last;
	char 	*s = an_conf;

	last = an_dump;

	switch (an_dump) {
	case 0:
		strcpy(an_conf, "off");
		break;
	case 1:
		strcpy(an_conf, "type");
		break;
	case 2:
		strcpy(an_conf, "dump");
		break;
	default:
		snprintf(an_conf, 5, "%x", an_dump);
		break;
	}

	error = sysctl_handle_string(oidp, an_conf, sizeof(an_conf), req);

	if (strncmp(an_conf,"off", 3) == 0) {
		an_dump = 0;
 	}
	if (strncmp(an_conf,"dump", 4) == 0) {
		an_dump = 1;
	}
	if (strncmp(an_conf,"type", 4) == 0) {
		an_dump = 2;
	}
	if (*s == 'f') {
		r = 0;
		for (;;s++) {
			if ((*s >= '0') && (*s <= '9')) {
				r = r * 16 + (*s - '0');
			} else if ((*s >= 'a') && (*s <= 'f')) {
				r = r * 16 + (*s - 'a' + 10);
			} else {
				break;
			}
		}
		an_dump = r;
	}
	if (an_dump != last)
		printf("Sysctl changed for Aironet driver\n");

	return error;
}

SYSCTL_PROC(_hw_an, OID_AUTO, an_dump, CTLTYPE_STRING | CTLFLAG_RW,
	    0, sizeof(an_conf), sysctl_an_dump, "A", "");

static int
sysctl_an_cache_mode(SYSCTL_HANDLER_ARGS)
{
	int	error;

	switch (an_cache_mode) {
	case 1:
		strcpy(an_conf_cache, "per");
		break;
	case 2:
		strcpy(an_conf_cache, "raw");
		break;
	default:
		strcpy(an_conf_cache, "dbm");
		break;
	}

	error = sysctl_handle_string(oidp, an_conf_cache,
			sizeof(an_conf_cache), req);

	if (strncmp(an_conf_cache,"dbm", 3) == 0) {
		an_cache_mode = 0;
	}
	if (strncmp(an_conf_cache,"per", 3) == 0) {
		an_cache_mode = 1;
 	}
	if (strncmp(an_conf_cache,"raw", 3) == 0) {
		an_cache_mode = 2;
	}

	return error;
}

SYSCTL_PROC(_hw_an, OID_AUTO, an_cache_mode, CTLTYPE_STRING | CTLFLAG_RW,
	    0, sizeof(an_conf_cache), sysctl_an_cache_mode, "A", "");

/*
 * We probe for an Aironet 4500/4800 card by attempting to
 * read the default SSID list. On reset, the first entry in
 * the SSID list will contain the name "tsunami." If we don't
 * find this, then there's no card present.
 */
int
an_probe(device_t dev)
{
	struct an_softc *sc = device_get_softc(dev);
	struct an_ltv_ssidlist_new	ssid;
	int	error;

	bzero((char *)&ssid, sizeof(ssid));

	error = an_alloc_port(dev, 0, AN_IOSIZ);
	if (error != 0)
		return (0);

	/* can't do autoprobing */
	if (rman_get_start(sc->port_res) == -1)
		return(0);

	/*
	 * We need to fake up a softc structure long enough
	 * to be able to issue commands and call some of the
	 * other routines.
	 */
	ssid.an_len = sizeof(ssid);
	ssid.an_type = AN_RID_SSIDLIST;

	/* Make sure interrupts are disabled. */
	sc->mpi350 = 0;
	CSR_WRITE_2(sc, AN_INT_EN(sc->mpi350), 0);
	CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), 0xFFFF);

	sc->an_dev = dev;
	mtx_init(&sc->an_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	AN_LOCK(sc);
	an_reset(sc);

	if (an_cmd(sc, AN_CMD_READCFG, 0)) {
		AN_UNLOCK(sc);
		goto fail;
	}

	if (an_read_record(sc, (struct an_ltv_gen *)&ssid)) {
		AN_UNLOCK(sc);
		goto fail;
	}

	/* See if the ssid matches what we expect ... but doesn't have to */
	if (strcmp(ssid.an_entry[0].an_ssid, AN_DEF_SSID)) {
		AN_UNLOCK(sc);
		goto fail;
	}

	AN_UNLOCK(sc);
	return(AN_IOSIZ);
fail:
	mtx_destroy(&sc->an_mtx);
	return(0);
}

/*
 * Allocate a port resource with the given resource id.
 */
int
an_alloc_port(device_t dev, int rid, int size)
{
	struct an_softc *sc = device_get_softc(dev);
	struct resource *res;

	res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
					  size, RF_ACTIVE);
	if (res) {
		sc->port_rid = rid;
		sc->port_res = res;
		return (0);
	} else {
		return (ENOENT);
	}
}

/*
 * Allocate a memory resource with the given resource id.
 */
int an_alloc_memory(device_t dev, int rid, int size)
{
	struct an_softc *sc = device_get_softc(dev);
	struct resource *res;

	res = bus_alloc_resource_anywhere(dev, SYS_RES_MEMORY, &rid,
					  size, RF_ACTIVE);
	if (res) {
		sc->mem_rid = rid;
		sc->mem_res = res;
		sc->mem_used = size;
		return (0);
	} else {
		return (ENOENT);
	}
}

/*
 * Allocate a auxiliary memory resource with the given resource id.
 */
int an_alloc_aux_memory(device_t dev, int rid, int size)
{
	struct an_softc *sc = device_get_softc(dev);
	struct resource *res;

	res = bus_alloc_resource_anywhere(dev, SYS_RES_MEMORY, &rid,
					  size, RF_ACTIVE);
	if (res) {
		sc->mem_aux_rid = rid;
		sc->mem_aux_res = res;
		sc->mem_aux_used = size;
		return (0);
	} else {
		return (ENOENT);
	}
}

/*
 * Allocate an irq resource with the given resource id.
 */
int
an_alloc_irq(device_t dev, int rid, int flags)
{
	struct an_softc *sc = device_get_softc(dev);
	struct resource *res;

	res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
				     (RF_ACTIVE | flags));
	if (res) {
		sc->irq_rid = rid;
		sc->irq_res = res;
		return (0);
	} else {
		return (ENOENT);
	}
}

static void
an_dma_malloc_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *paddr = (bus_addr_t*) arg;
	*paddr = segs->ds_addr;
}

/*
 * Alloc DMA memory and set the pointer to it
 */
static int
an_dma_malloc(struct an_softc *sc, bus_size_t size, struct an_dma_alloc *dma,
    int mapflags)
{
	int r;

	r = bus_dmamem_alloc(sc->an_dtag, (void**) &dma->an_dma_vaddr,
			     BUS_DMA_NOWAIT, &dma->an_dma_map);
	if (r != 0)
		goto fail_1;

	r = bus_dmamap_load(sc->an_dtag, dma->an_dma_map, dma->an_dma_vaddr,
			    size,
			    an_dma_malloc_cb,
			    &dma->an_dma_paddr,
			    mapflags | BUS_DMA_NOWAIT);
	if (r != 0)
		goto fail_2;

	dma->an_dma_size = size;
	return (0);

fail_2:
	bus_dmamap_unload(sc->an_dtag, dma->an_dma_map);
fail_1:
	bus_dmamem_free(sc->an_dtag, dma->an_dma_vaddr, dma->an_dma_map);
	return (r);
}

static void
an_dma_free(struct an_softc *sc, struct an_dma_alloc *dma)
{
	bus_dmamap_unload(sc->an_dtag, dma->an_dma_map);
	bus_dmamem_free(sc->an_dtag, dma->an_dma_vaddr, dma->an_dma_map);
	dma->an_dma_vaddr = 0;
}

/*
 * Release all resources
 */
void
an_release_resources(device_t dev)
{
	struct an_softc *sc = device_get_softc(dev);
	int i;

	if (sc->port_res) {
		bus_release_resource(dev, SYS_RES_IOPORT,
				     sc->port_rid, sc->port_res);
		sc->port_res = 0;
	}
	if (sc->mem_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     sc->mem_rid, sc->mem_res);
		sc->mem_res = 0;
	}
	if (sc->mem_aux_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     sc->mem_aux_rid, sc->mem_aux_res);
		sc->mem_aux_res = 0;
	}
	if (sc->irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ,
				     sc->irq_rid, sc->irq_res);
		sc->irq_res = 0;
	}
	if (sc->an_rid_buffer.an_dma_paddr) {
		an_dma_free(sc, &sc->an_rid_buffer);
	}
	for (i = 0; i < AN_MAX_RX_DESC; i++)
		if (sc->an_rx_buffer[i].an_dma_paddr) {
			an_dma_free(sc, &sc->an_rx_buffer[i]);
		}
	for (i = 0; i < AN_MAX_TX_DESC; i++)
		if (sc->an_tx_buffer[i].an_dma_paddr) {
			an_dma_free(sc, &sc->an_tx_buffer[i]);
		}
	if (sc->an_dtag) {
		bus_dma_tag_destroy(sc->an_dtag);
	}

}

int
an_init_mpi350_desc(struct an_softc *sc)
{
	struct an_command	cmd_struct;
	struct an_reply		reply;
	struct an_card_rid_desc an_rid_desc;
	struct an_card_rx_desc	an_rx_desc;
	struct an_card_tx_desc	an_tx_desc;
	int			i, desc;

	AN_LOCK_ASSERT(sc);
	if(!sc->an_rid_buffer.an_dma_paddr)
		an_dma_malloc(sc, AN_RID_BUFFER_SIZE,
				 &sc->an_rid_buffer, 0);
	for (i = 0; i < AN_MAX_RX_DESC; i++)
		if(!sc->an_rx_buffer[i].an_dma_paddr)
			an_dma_malloc(sc, AN_RX_BUFFER_SIZE,
				      &sc->an_rx_buffer[i], 0);
	for (i = 0; i < AN_MAX_TX_DESC; i++)
		if(!sc->an_tx_buffer[i].an_dma_paddr)
			an_dma_malloc(sc, AN_TX_BUFFER_SIZE,
				      &sc->an_tx_buffer[i], 0);

	/*
	 * Allocate RX descriptor
	 */
	bzero(&reply,sizeof(reply));
	cmd_struct.an_cmd   = AN_CMD_ALLOC_DESC;
	cmd_struct.an_parm0 = AN_DESCRIPTOR_RX;
	cmd_struct.an_parm1 = AN_RX_DESC_OFFSET;
	cmd_struct.an_parm2 = AN_MAX_RX_DESC;
	if (an_cmd_struct(sc, &cmd_struct, &reply)) {
		if_printf(sc->an_ifp, "failed to allocate RX descriptor\n");
		return(EIO);
	}

	for (desc = 0; desc < AN_MAX_RX_DESC; desc++) {
		bzero(&an_rx_desc, sizeof(an_rx_desc));
		an_rx_desc.an_valid = 1;
		an_rx_desc.an_len = AN_RX_BUFFER_SIZE;
		an_rx_desc.an_done = 0;
		an_rx_desc.an_phys = sc->an_rx_buffer[desc].an_dma_paddr;

		for (i = 0; i < sizeof(an_rx_desc) / 4; i++)
			CSR_MEM_AUX_WRITE_4(sc, AN_RX_DESC_OFFSET
			    + (desc * sizeof(an_rx_desc))
			    + (i * 4),
			    ((u_int32_t *)(void *)&an_rx_desc)[i]);
	}

	/*
	 * Allocate TX descriptor
	 */

	bzero(&reply,sizeof(reply));
	cmd_struct.an_cmd   = AN_CMD_ALLOC_DESC;
	cmd_struct.an_parm0 = AN_DESCRIPTOR_TX;
	cmd_struct.an_parm1 = AN_TX_DESC_OFFSET;
	cmd_struct.an_parm2 = AN_MAX_TX_DESC;
	if (an_cmd_struct(sc, &cmd_struct, &reply)) {
		if_printf(sc->an_ifp, "failed to allocate TX descriptor\n");
		return(EIO);
	}

	for (desc = 0; desc < AN_MAX_TX_DESC; desc++) {
		bzero(&an_tx_desc, sizeof(an_tx_desc));
		an_tx_desc.an_offset = 0;
		an_tx_desc.an_eoc = 0;
		an_tx_desc.an_valid = 0;
		an_tx_desc.an_len = 0;
		an_tx_desc.an_phys = sc->an_tx_buffer[desc].an_dma_paddr;

		for (i = 0; i < sizeof(an_tx_desc) / 4; i++)
			CSR_MEM_AUX_WRITE_4(sc, AN_TX_DESC_OFFSET
			    + (desc * sizeof(an_tx_desc))
			    + (i * 4),
			    ((u_int32_t *)(void *)&an_tx_desc)[i]);
	}

	/*
	 * Allocate RID descriptor
	 */

	bzero(&reply,sizeof(reply));
	cmd_struct.an_cmd   = AN_CMD_ALLOC_DESC;
	cmd_struct.an_parm0 = AN_DESCRIPTOR_HOSTRW;
	cmd_struct.an_parm1 = AN_HOST_DESC_OFFSET;
	cmd_struct.an_parm2 = 1;
	if (an_cmd_struct(sc, &cmd_struct, &reply)) {
		if_printf(sc->an_ifp, "failed to allocate host descriptor\n");
		return(EIO);
	}

	bzero(&an_rid_desc, sizeof(an_rid_desc));
	an_rid_desc.an_valid = 1;
	an_rid_desc.an_len = AN_RID_BUFFER_SIZE;
	an_rid_desc.an_rid = 0;
	an_rid_desc.an_phys = sc->an_rid_buffer.an_dma_paddr;

	for (i = 0; i < sizeof(an_rid_desc) / 4; i++)
		CSR_MEM_AUX_WRITE_4(sc, AN_HOST_DESC_OFFSET + i * 4,
				    ((u_int32_t *)(void *)&an_rid_desc)[i]);

	return(0);
}

int
an_attach(struct an_softc *sc, int flags)
{
	struct ifnet		*ifp;
	int			error = EIO;
	int			i, nrate, mword;
	u_int8_t		r;

	ifp = sc->an_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(sc->an_dev, "can not if_alloc()\n");
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(sc->an_dev),
	    device_get_unit(sc->an_dev));

	sc->an_gone = 0;
	sc->an_associated = 0;
	sc->an_monitor = 0;
	sc->an_was_monitor = 0;
	sc->an_flash_buffer = NULL;

	/* Reset the NIC. */
	AN_LOCK(sc);
	an_reset(sc);
	if (sc->mpi350) {
		error = an_init_mpi350_desc(sc);
		if (error)
			goto fail;
	}

	/* Load factory config */
	if (an_cmd(sc, AN_CMD_READCFG, 0)) {
		device_printf(sc->an_dev, "failed to load config data\n");
		goto fail;
	}

	/* Read the current configuration */
	sc->an_config.an_type = AN_RID_GENCONFIG;
	sc->an_config.an_len = sizeof(struct an_ltv_genconfig);
	if (an_read_record(sc, (struct an_ltv_gen *)&sc->an_config)) {
		device_printf(sc->an_dev, "read record failed\n");
		goto fail;
	}

	/* Read the card capabilities */
	sc->an_caps.an_type = AN_RID_CAPABILITIES;
	sc->an_caps.an_len = sizeof(struct an_ltv_caps);
	if (an_read_record(sc, (struct an_ltv_gen *)&sc->an_caps)) {
		device_printf(sc->an_dev, "read record failed\n");
		goto fail;
	}

	/* Read ssid list */
	sc->an_ssidlist.an_type = AN_RID_SSIDLIST;
	sc->an_ssidlist.an_len = sizeof(struct an_ltv_ssidlist_new);
	if (an_read_record(sc, (struct an_ltv_gen *)&sc->an_ssidlist)) {
		device_printf(sc->an_dev, "read record failed\n");
		goto fail;
	}

	/* Read AP list */
	sc->an_aplist.an_type = AN_RID_APLIST;
	sc->an_aplist.an_len = sizeof(struct an_ltv_aplist);
	if (an_read_record(sc, (struct an_ltv_gen *)&sc->an_aplist)) {
		device_printf(sc->an_dev, "read record failed\n");
		goto fail;
	}

#ifdef ANCACHE
	/* Read the RSSI <-> dBm map */
	sc->an_have_rssimap = 0;
	if (sc->an_caps.an_softcaps & 8) {
		sc->an_rssimap.an_type = AN_RID_RSSI_MAP;
		sc->an_rssimap.an_len = sizeof(struct an_ltv_rssi_map);
		if (an_read_record(sc, (struct an_ltv_gen *)&sc->an_rssimap)) {
			device_printf(sc->an_dev,
			    "unable to get RSSI <-> dBM map\n");
		} else {
			device_printf(sc->an_dev, "got RSSI <-> dBM map\n");
			sc->an_have_rssimap = 1;
		}
	} else {
		device_printf(sc->an_dev, "no RSSI <-> dBM map\n");
	}
#endif
	AN_UNLOCK(sc);

	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = an_ioctl;
	ifp->if_start = an_start;
	ifp->if_init = an_init;
	ifp->if_baudrate = 10000000;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	bzero(sc->an_config.an_nodename, sizeof(sc->an_config.an_nodename));
	bcopy(AN_DEFAULT_NODENAME, sc->an_config.an_nodename,
	    sizeof(AN_DEFAULT_NODENAME) - 1);

	bzero(sc->an_ssidlist.an_entry[0].an_ssid,
	      sizeof(sc->an_ssidlist.an_entry[0].an_ssid));
	bcopy(AN_DEFAULT_NETNAME, sc->an_ssidlist.an_entry[0].an_ssid,
	    sizeof(AN_DEFAULT_NETNAME) - 1);
	sc->an_ssidlist.an_entry[0].an_len = strlen(AN_DEFAULT_NETNAME);

	sc->an_config.an_opmode =
	    AN_OPMODE_INFRASTRUCTURE_STATION;

	sc->an_tx_rate = 0;
	bzero((char *)&sc->an_stats, sizeof(sc->an_stats));

	nrate = 8;

	ifmedia_init(&sc->an_ifmedia, 0, an_media_change, an_media_status);
	if_printf(ifp, "supported rates: ");
#define	ADD(s, o)	ifmedia_add(&sc->an_ifmedia, \
	IFM_MAKEWORD(IFM_IEEE80211, (s), (o), 0), 0, NULL)
	ADD(IFM_AUTO, 0);
	ADD(IFM_AUTO, IFM_IEEE80211_ADHOC);
	for (i = 0; i < nrate; i++) {
		r = sc->an_caps.an_rates[i];
		mword = ieee80211_rate2media(NULL, r, IEEE80211_MODE_AUTO);
		if (mword == 0)
			continue;
		printf("%s%d%sMbps", (i != 0 ? " " : ""),
		    (r & IEEE80211_RATE_VAL) / 2, ((r & 0x1) != 0 ? ".5" : ""));
		ADD(mword, 0);
		ADD(mword, IFM_IEEE80211_ADHOC);
	}
	printf("\n");
	ifmedia_set(&sc->an_ifmedia, IFM_MAKEWORD(IFM_IEEE80211,
	    IFM_AUTO, 0, 0));
#undef ADD

	/*
	 * Call MI attach routine.
	 */

	ether_ifattach(ifp, sc->an_caps.an_oemaddr);
	callout_init_mtx(&sc->an_stat_ch, &sc->an_mtx, 0);

	return(0);
fail:
	AN_UNLOCK(sc);
	mtx_destroy(&sc->an_mtx);
	if (ifp != NULL)
		if_free(ifp);
	return(error);
}

int
an_detach(device_t dev)
{
	struct an_softc		*sc = device_get_softc(dev);
	struct ifnet		*ifp = sc->an_ifp;

	if (sc->an_gone) {
		device_printf(dev,"already unloaded\n");
		return(0);
	}
	AN_LOCK(sc);
	an_stop(sc);
	sc->an_gone = 1;
	ifmedia_removeall(&sc->an_ifmedia);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	AN_UNLOCK(sc);
	ether_ifdetach(ifp);
	bus_teardown_intr(dev, sc->irq_res, sc->irq_handle);
	callout_drain(&sc->an_stat_ch);
	if_free(ifp);
	an_release_resources(dev);
	mtx_destroy(&sc->an_mtx);
	return (0);
}

static void
an_rxeof(struct an_softc *sc)
{
	struct ifnet   *ifp;
	struct ether_header *eh;
	struct ieee80211_frame *ih;
	struct an_rxframe rx_frame;
	struct an_rxframe_802_3 rx_frame_802_3;
	struct mbuf    *m;
	int		len, id, error = 0, i, count = 0;
	int		ieee80211_header_len;
	u_char		*bpf_buf;
	u_short		fc1;
	struct an_card_rx_desc an_rx_desc;
	u_int8_t	*buf;

	AN_LOCK_ASSERT(sc);

	ifp = sc->an_ifp;

	if (!sc->mpi350) {
		id = CSR_READ_2(sc, AN_RX_FID);

		if (sc->an_monitor && (ifp->if_flags & IFF_PROMISC)) {
			/* read raw 802.11 packet */
			bpf_buf = sc->buf_802_11;

			/* read header */
			if (an_read_data(sc, id, 0x0, (caddr_t)&rx_frame,
					 sizeof(rx_frame))) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				return;
			}

			/*
			 * skip beacon by default since this increases the
			 * system load a lot
			 */

			if (!(sc->an_monitor & AN_MONITOR_INCLUDE_BEACON) &&
			    (rx_frame.an_frame_ctl & 
			     IEEE80211_FC0_SUBTYPE_BEACON)) {
				return;
			}

			if (sc->an_monitor & AN_MONITOR_AIRONET_HEADER) {
				len = rx_frame.an_rx_payload_len
					+ sizeof(rx_frame);
				/* Check for insane frame length */
				if (len > sizeof(sc->buf_802_11)) {
					if_printf(ifp, "oversized packet "
					       "received (%d, %d)\n",
					       len, MCLBYTES);
					if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
					return;
				}

				bcopy((char *)&rx_frame,
				      bpf_buf, sizeof(rx_frame));

				error = an_read_data(sc, id, sizeof(rx_frame),
					    (caddr_t)bpf_buf+sizeof(rx_frame),
					    rx_frame.an_rx_payload_len);
			} else {
				fc1=rx_frame.an_frame_ctl >> 8;
				ieee80211_header_len =
					sizeof(struct ieee80211_frame);
				if ((fc1 & IEEE80211_FC1_DIR_TODS) &&
				    (fc1 & IEEE80211_FC1_DIR_FROMDS)) {
					ieee80211_header_len += ETHER_ADDR_LEN;
				}

				len = rx_frame.an_rx_payload_len
					+ ieee80211_header_len;
				/* Check for insane frame length */
				if (len > sizeof(sc->buf_802_11)) {
					if_printf(ifp, "oversized packet "
					       "received (%d, %d)\n",
					       len, MCLBYTES);
					if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
					return;
				}

				ih = (struct ieee80211_frame *)bpf_buf;

				bcopy((char *)&rx_frame.an_frame_ctl,
				      (char *)ih, ieee80211_header_len);

				error = an_read_data(sc, id, sizeof(rx_frame) +
					    rx_frame.an_gaplen,
					    (caddr_t)ih +ieee80211_header_len,
					    rx_frame.an_rx_payload_len);
			}
			/* dump raw 802.11 packet to bpf and skip ip stack */
			BPF_TAP(ifp, bpf_buf, len);
		} else {
			MGETHDR(m, M_NOWAIT, MT_DATA);
			if (m == NULL) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				return;
			}
			if (!(MCLGET(m, M_NOWAIT))) {
				m_freem(m);
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				return;
			}
			m->m_pkthdr.rcvif = ifp;
			/* Read Ethernet encapsulated packet */

#ifdef ANCACHE
			/* Read NIC frame header */
			if (an_read_data(sc, id, 0, (caddr_t)&rx_frame,
					 sizeof(rx_frame))) {
				m_freem(m);
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				return;
			}
#endif
			/* Read in the 802_3 frame header */
			if (an_read_data(sc, id, 0x34,
					 (caddr_t)&rx_frame_802_3,
					 sizeof(rx_frame_802_3))) {
				m_freem(m);
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				return;
			}
			if (rx_frame_802_3.an_rx_802_3_status != 0) {
				m_freem(m);
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				return;
			}
			/* Check for insane frame length */
			len = rx_frame_802_3.an_rx_802_3_payload_len;
			if (len > sizeof(sc->buf_802_11)) {
				m_freem(m);
				if_printf(ifp, "oversized packet "
				       "received (%d, %d)\n",
				       len, MCLBYTES);
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				return;
			}
			m->m_pkthdr.len = m->m_len =
				rx_frame_802_3.an_rx_802_3_payload_len + 12;

			eh = mtod(m, struct ether_header *);

			bcopy((char *)&rx_frame_802_3.an_rx_dst_addr,
			      (char *)&eh->ether_dhost, ETHER_ADDR_LEN);
			bcopy((char *)&rx_frame_802_3.an_rx_src_addr,
			      (char *)&eh->ether_shost, ETHER_ADDR_LEN);

			/* in mbuf header type is just before payload */
			error = an_read_data(sc, id, 0x44,
				    (caddr_t)&(eh->ether_type),
				    rx_frame_802_3.an_rx_802_3_payload_len);

			if (error) {
				m_freem(m);
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				return;
			}
			if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

			/* Receive packet. */
#ifdef ANCACHE
			an_cache_store(sc, eh, m,
				rx_frame.an_rx_signal_strength,
				rx_frame.an_rsvd0);
#endif
			AN_UNLOCK(sc);
			(*ifp->if_input)(ifp, m);
			AN_LOCK(sc);
		}

	} else { /* MPI-350 */
		for (count = 0; count < AN_MAX_RX_DESC; count++){
			for (i = 0; i < sizeof(an_rx_desc) / 4; i++)
				((u_int32_t *)(void *)&an_rx_desc)[i]
					= CSR_MEM_AUX_READ_4(sc,
						AN_RX_DESC_OFFSET
						+ (count * sizeof(an_rx_desc))
						+ (i * 4));

			if (an_rx_desc.an_done && !an_rx_desc.an_valid) {
				buf = sc->an_rx_buffer[count].an_dma_vaddr;

				MGETHDR(m, M_NOWAIT, MT_DATA);
				if (m == NULL) {
					if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
					return;
				}
				if (!(MCLGET(m, M_NOWAIT))) {
					m_freem(m);
					if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
					return;
				}
				m->m_pkthdr.rcvif = ifp;
				/* Read Ethernet encapsulated packet */

				/*
				 * No ANCACHE support since we just get back
				 * an Ethernet packet no 802.11 info
				 */
#if 0
#ifdef ANCACHE
				/* Read NIC frame header */
				bcopy(buf, (caddr_t)&rx_frame,
				      sizeof(rx_frame));
#endif
#endif
				/* Check for insane frame length */
				len = an_rx_desc.an_len + 12;
				if (len > MCLBYTES) {
					m_freem(m);
					if_printf(ifp, "oversized packet "
					       "received (%d, %d)\n",
					       len, MCLBYTES);
					if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
					return;
				}

				m->m_pkthdr.len = m->m_len =
					an_rx_desc.an_len + 12;

				eh = mtod(m, struct ether_header *);

				bcopy(buf, (char *)eh,
				      m->m_pkthdr.len);

				if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

				/* Receive packet. */
#if 0
#ifdef ANCACHE
				an_cache_store(sc, eh, m,
					rx_frame.an_rx_signal_strength,
					rx_frame.an_rsvd0);
#endif
#endif
				AN_UNLOCK(sc);
				(*ifp->if_input)(ifp, m);
				AN_LOCK(sc);

				an_rx_desc.an_valid = 1;
				an_rx_desc.an_len = AN_RX_BUFFER_SIZE;
				an_rx_desc.an_done = 0;
				an_rx_desc.an_phys =
					sc->an_rx_buffer[count].an_dma_paddr;

				for (i = 0; i < sizeof(an_rx_desc) / 4; i++)
					CSR_MEM_AUX_WRITE_4(sc,
					    AN_RX_DESC_OFFSET
					    + (count * sizeof(an_rx_desc))
					    + (i * 4),
					    ((u_int32_t *)(void *)&an_rx_desc)[i]);

			} else {
				if_printf(ifp, "Didn't get valid RX packet "
				       "%x %x %d\n",
				       an_rx_desc.an_done,
				       an_rx_desc.an_valid, an_rx_desc.an_len);
			}
		}
	}
}

static void
an_txeof(struct an_softc *sc, int status)
{
	struct ifnet		*ifp;
	int			id, i;

	AN_LOCK_ASSERT(sc);
	ifp = sc->an_ifp;

	sc->an_timer = 0;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	if (!sc->mpi350) {
		id = CSR_READ_2(sc, AN_TX_CMP_FID(sc->mpi350));

		if (status & AN_EV_TX_EXC) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		} else
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		for (i = 0; i < AN_TX_RING_CNT; i++) {
			if (id == sc->an_rdata.an_tx_ring[i]) {
				sc->an_rdata.an_tx_ring[i] = 0;
				break;
			}
		}

		AN_INC(sc->an_rdata.an_tx_cons, AN_TX_RING_CNT);
	} else { /* MPI 350 */
		id = CSR_READ_2(sc, AN_TX_CMP_FID(sc->mpi350));
		if (!sc->an_rdata.an_tx_empty){
			if (status & AN_EV_TX_EXC) {
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			} else
				if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			AN_INC(sc->an_rdata.an_tx_cons, AN_MAX_TX_DESC);
			if (sc->an_rdata.an_tx_prod ==
			    sc->an_rdata.an_tx_cons)
				sc->an_rdata.an_tx_empty = 1;
		}
	}

	return;
}

/*
 * We abuse the stats updater to check the current NIC status. This
 * is important because we don't want to allow transmissions until
 * the NIC has synchronized to the current cell (either as the master
 * in an ad-hoc group, or as a station connected to an access point).
 *
 * Note that this function will be called via callout(9) with a lock held.
 */
static void
an_stats_update(void *xsc)
{
	struct an_softc		*sc;
	struct ifnet		*ifp;

	sc = xsc;
	AN_LOCK_ASSERT(sc);
	ifp = sc->an_ifp;
	if (sc->an_timer > 0 && --sc->an_timer == 0)
		an_watchdog(sc);

	sc->an_status.an_type = AN_RID_STATUS;
	sc->an_status.an_len = sizeof(struct an_ltv_status);
	if (an_read_record(sc, (struct an_ltv_gen *)&sc->an_status))
		return;

	if (sc->an_status.an_opmode & AN_STATUS_OPMODE_IN_SYNC)
		sc->an_associated = 1;
	else
		sc->an_associated = 0;

	/* Don't do this while we're transmitting */
	if (ifp->if_drv_flags & IFF_DRV_OACTIVE) {
		callout_reset(&sc->an_stat_ch, hz, an_stats_update, sc);
		return;
	}

	sc->an_stats.an_len = sizeof(struct an_ltv_stats);
	sc->an_stats.an_type = AN_RID_32BITS_CUM;
	if (an_read_record(sc, (struct an_ltv_gen *)&sc->an_stats.an_len))
		return;

	callout_reset(&sc->an_stat_ch, hz, an_stats_update, sc);

	return;
}

void
an_intr(void *xsc)
{
	struct an_softc		*sc;
	struct ifnet		*ifp;
	u_int16_t		status;

	sc = (struct an_softc*)xsc;

	AN_LOCK(sc);

	if (sc->an_gone) {
		AN_UNLOCK(sc);
		return;
	}

	ifp = sc->an_ifp;

	/* Disable interrupts. */
	CSR_WRITE_2(sc, AN_INT_EN(sc->mpi350), 0);

	status = CSR_READ_2(sc, AN_EVENT_STAT(sc->mpi350));
	CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), ~AN_INTRS(sc->mpi350));

	if (status & AN_EV_MIC) {
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_MIC);
	}

	if (status & AN_EV_LINKSTAT) {
		if (CSR_READ_2(sc, AN_LINKSTAT(sc->mpi350))
		    == AN_LINKSTAT_ASSOCIATED)
			sc->an_associated = 1;
		else
			sc->an_associated = 0;
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_LINKSTAT);
	}

	if (status & AN_EV_RX) {
		an_rxeof(sc);
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_RX);
	}

	if (sc->mpi350 && status & AN_EV_TX_CPY) {
		an_txeof(sc, status);
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_TX_CPY);
	}

	if (status & AN_EV_TX) {
		an_txeof(sc, status);
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_TX);
	}

	if (status & AN_EV_TX_EXC) {
		an_txeof(sc, status);
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_TX_EXC);
	}

	if (status & AN_EV_ALLOC)
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_ALLOC);

	/* Re-enable interrupts. */
	CSR_WRITE_2(sc, AN_INT_EN(sc->mpi350), AN_INTRS(sc->mpi350));

	if ((ifp->if_flags & IFF_UP) && !IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		an_start_locked(ifp);

	AN_UNLOCK(sc);

	return;
}


static int
an_cmd_struct(struct an_softc *sc, struct an_command *cmd,
    struct an_reply *reply)
{
	int			i;

	AN_LOCK_ASSERT(sc);
	for (i = 0; i != AN_TIMEOUT; i++) {
		if (CSR_READ_2(sc, AN_COMMAND(sc->mpi350)) & AN_CMD_BUSY) {
			DELAY(1000);
		} else
			break;
	}

	if( i == AN_TIMEOUT) {
		printf("BUSY\n");
		return(ETIMEDOUT);
	}

	CSR_WRITE_2(sc, AN_PARAM0(sc->mpi350), cmd->an_parm0);
	CSR_WRITE_2(sc, AN_PARAM1(sc->mpi350), cmd->an_parm1);
	CSR_WRITE_2(sc, AN_PARAM2(sc->mpi350), cmd->an_parm2);
	CSR_WRITE_2(sc, AN_COMMAND(sc->mpi350), cmd->an_cmd);

	for (i = 0; i < AN_TIMEOUT; i++) {
		if (CSR_READ_2(sc, AN_EVENT_STAT(sc->mpi350)) & AN_EV_CMD)
			break;
		DELAY(1000);
	}

	reply->an_resp0 = CSR_READ_2(sc, AN_RESP0(sc->mpi350));
	reply->an_resp1 = CSR_READ_2(sc, AN_RESP1(sc->mpi350));
	reply->an_resp2 = CSR_READ_2(sc, AN_RESP2(sc->mpi350));
	reply->an_status = CSR_READ_2(sc, AN_STATUS(sc->mpi350));

	if (CSR_READ_2(sc, AN_COMMAND(sc->mpi350)) & AN_CMD_BUSY)
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350),
		    AN_EV_CLR_STUCK_BUSY);

	/* Ack the command */
	CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_CMD);

	if (i == AN_TIMEOUT)
		return(ETIMEDOUT);

	return(0);
}

static int
an_cmd(struct an_softc *sc, int cmd, int val)
{
	int			i, s = 0;

	AN_LOCK_ASSERT(sc);
	CSR_WRITE_2(sc, AN_PARAM0(sc->mpi350), val);
	CSR_WRITE_2(sc, AN_PARAM1(sc->mpi350), 0);
	CSR_WRITE_2(sc, AN_PARAM2(sc->mpi350), 0);
	CSR_WRITE_2(sc, AN_COMMAND(sc->mpi350), cmd);

	for (i = 0; i < AN_TIMEOUT; i++) {
		if (CSR_READ_2(sc, AN_EVENT_STAT(sc->mpi350)) & AN_EV_CMD)
			break;
		else {
			if (CSR_READ_2(sc, AN_COMMAND(sc->mpi350)) == cmd)
				CSR_WRITE_2(sc, AN_COMMAND(sc->mpi350), cmd);
		}
	}

	for (i = 0; i < AN_TIMEOUT; i++) {
		CSR_READ_2(sc, AN_RESP0(sc->mpi350));
		CSR_READ_2(sc, AN_RESP1(sc->mpi350));
		CSR_READ_2(sc, AN_RESP2(sc->mpi350));
		s = CSR_READ_2(sc, AN_STATUS(sc->mpi350));
		if ((s & AN_STAT_CMD_CODE) == (cmd & AN_STAT_CMD_CODE))
			break;
	}

	/* Ack the command */
	CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_CMD);

	if (CSR_READ_2(sc, AN_COMMAND(sc->mpi350)) & AN_CMD_BUSY)
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_CLR_STUCK_BUSY);

	if (i == AN_TIMEOUT)
		return(ETIMEDOUT);

	return(0);
}

/*
 * This reset sequence may look a little strange, but this is the
 * most reliable method I've found to really kick the NIC in the
 * head and force it to reboot correctly.
 */
static void
an_reset(struct an_softc *sc)
{
	if (sc->an_gone)
		return;

	AN_LOCK_ASSERT(sc);
	an_cmd(sc, AN_CMD_ENABLE, 0);
	an_cmd(sc, AN_CMD_FW_RESTART, 0);
	an_cmd(sc, AN_CMD_NOOP2, 0);

	if (an_cmd(sc, AN_CMD_FORCE_SYNCLOSS, 0) == ETIMEDOUT)
		device_printf(sc->an_dev, "reset failed\n");

	an_cmd(sc, AN_CMD_DISABLE, 0);

	return;
}

/*
 * Read an LTV record from the NIC.
 */
static int
an_read_record(struct an_softc *sc, struct an_ltv_gen *ltv)
{
	struct an_ltv_gen	*an_ltv;
	struct an_card_rid_desc an_rid_desc;
	struct an_command	cmd;
	struct an_reply		reply;
	struct ifnet		*ifp;
	u_int16_t		*ptr;
	u_int8_t		*ptr2;
	int			i, len;

	AN_LOCK_ASSERT(sc);
	if (ltv->an_len < 4 || ltv->an_type == 0)
		return(EINVAL);

	ifp = sc->an_ifp;
	if (!sc->mpi350){
		/* Tell the NIC to enter record read mode. */
		if (an_cmd(sc, AN_CMD_ACCESS|AN_ACCESS_READ, ltv->an_type)) {
			if_printf(ifp, "RID access failed\n");
			return(EIO);
		}

		/* Seek to the record. */
		if (an_seek(sc, ltv->an_type, 0, AN_BAP1)) {
			if_printf(ifp, "seek to record failed\n");
			return(EIO);
		}

		/*
		 * Read the length and record type and make sure they
		 * match what we expect (this verifies that we have enough
		 * room to hold all of the returned data).
		 * Length includes type but not length.
		 */
		len = CSR_READ_2(sc, AN_DATA1);
		if (len > (ltv->an_len - 2)) {
			if_printf(ifp, "record length mismatch -- expected %d, "
			       "got %d for Rid %x\n",
			       ltv->an_len - 2, len, ltv->an_type);
			len = ltv->an_len - 2;
		} else {
			ltv->an_len = len + 2;
		}

		/* Now read the data. */
		len -= 2;	/* skip the type */
		ptr = &ltv->an_val;
		for (i = len; i > 1; i -= 2)
			*ptr++ = CSR_READ_2(sc, AN_DATA1);
		if (i) {
			ptr2 = (u_int8_t *)ptr;
			*ptr2 = CSR_READ_1(sc, AN_DATA1);
		}
	} else { /* MPI-350 */
		if (!sc->an_rid_buffer.an_dma_vaddr)
			return(EIO);
		an_rid_desc.an_valid = 1;
		an_rid_desc.an_len = AN_RID_BUFFER_SIZE;
		an_rid_desc.an_rid = 0;
		an_rid_desc.an_phys = sc->an_rid_buffer.an_dma_paddr;
		bzero(sc->an_rid_buffer.an_dma_vaddr, AN_RID_BUFFER_SIZE);

		bzero(&cmd, sizeof(cmd));
		bzero(&reply, sizeof(reply));
		cmd.an_cmd = AN_CMD_ACCESS|AN_ACCESS_READ;
		cmd.an_parm0 = ltv->an_type;

		for (i = 0; i < sizeof(an_rid_desc) / 4; i++)
			CSR_MEM_AUX_WRITE_4(sc, AN_HOST_DESC_OFFSET + i * 4,
			    ((u_int32_t *)(void *)&an_rid_desc)[i]);

		if (an_cmd_struct(sc, &cmd, &reply)
		    || reply.an_status & AN_CMD_QUAL_MASK) {
			if_printf(ifp, "failed to read RID %x %x %x %x %x, %d\n",
			       ltv->an_type,
			       reply.an_status,
			       reply.an_resp0,
			       reply.an_resp1,
			       reply.an_resp2,
			       i);
			return(EIO);
		}

		an_ltv = (struct an_ltv_gen *)sc->an_rid_buffer.an_dma_vaddr;
		if (an_ltv->an_len + 2 < an_rid_desc.an_len) {
			an_rid_desc.an_len = an_ltv->an_len;
		}

		len = an_rid_desc.an_len;
		if (len > (ltv->an_len - 2)) {
			if_printf(ifp, "record length mismatch -- expected %d, "
			       "got %d for Rid %x\n",
			       ltv->an_len - 2, len, ltv->an_type);
			len = ltv->an_len - 2;
		} else {
			ltv->an_len = len + 2;
		}
		bcopy(&an_ltv->an_type,
		    &ltv->an_val,
		    len);
	}

	if (an_dump)
		an_dump_record(sc, ltv, "Read");

	return(0);
}

/*
 * Same as read, except we inject data instead of reading it.
 */
static int
an_write_record(struct an_softc *sc, struct an_ltv_gen *ltv)
{
	struct an_card_rid_desc an_rid_desc;
	struct an_command	cmd;
	struct an_reply		reply;
	u_int16_t		*ptr;
	u_int8_t		*ptr2;
	int			i, len;

	AN_LOCK_ASSERT(sc);
	if (an_dump)
		an_dump_record(sc, ltv, "Write");

	if (!sc->mpi350){
		if (an_cmd(sc, AN_CMD_ACCESS|AN_ACCESS_READ, ltv->an_type))
			return(EIO);

		if (an_seek(sc, ltv->an_type, 0, AN_BAP1))
			return(EIO);

		/*
		 * Length includes type but not length.
		 */
		len = ltv->an_len - 2;
		CSR_WRITE_2(sc, AN_DATA1, len);

		len -= 2;	/* skip the type */
		ptr = &ltv->an_val;
		for (i = len; i > 1; i -= 2)
			CSR_WRITE_2(sc, AN_DATA1, *ptr++);
		if (i) {
			ptr2 = (u_int8_t *)ptr;
			CSR_WRITE_1(sc, AN_DATA0, *ptr2);
		}

		if (an_cmd(sc, AN_CMD_ACCESS|AN_ACCESS_WRITE, ltv->an_type))
			return(EIO);
	} else {
		/* MPI-350 */

		for (i = 0; i != AN_TIMEOUT; i++) {
			if (CSR_READ_2(sc, AN_COMMAND(sc->mpi350))
			    & AN_CMD_BUSY) {
				DELAY(10);
			} else
				break;
		}
		if (i == AN_TIMEOUT) {
			printf("BUSY\n");
		}

		an_rid_desc.an_valid = 1;
		an_rid_desc.an_len = ltv->an_len - 2;
		an_rid_desc.an_rid = ltv->an_type;
		an_rid_desc.an_phys = sc->an_rid_buffer.an_dma_paddr;

		bcopy(&ltv->an_type, sc->an_rid_buffer.an_dma_vaddr,
		      an_rid_desc.an_len);

		bzero(&cmd,sizeof(cmd));
		bzero(&reply,sizeof(reply));
		cmd.an_cmd = AN_CMD_ACCESS|AN_ACCESS_WRITE;
		cmd.an_parm0 = ltv->an_type;

		for (i = 0; i < sizeof(an_rid_desc) / 4; i++)
			CSR_MEM_AUX_WRITE_4(sc, AN_HOST_DESC_OFFSET + i * 4,
			    ((u_int32_t *)(void *)&an_rid_desc)[i]);

		DELAY(100000);

		if ((i = an_cmd_struct(sc, &cmd, &reply))) {
			if_printf(sc->an_ifp,
			    "failed to write RID 1 %x %x %x %x %x, %d\n",
			    ltv->an_type,
			    reply.an_status,
			    reply.an_resp0,
			    reply.an_resp1,
			    reply.an_resp2,
			    i);
			return(EIO);
		}


		if (reply.an_status & AN_CMD_QUAL_MASK) {
			if_printf(sc->an_ifp,
			    "failed to write RID 2 %x %x %x %x %x, %d\n",
			    ltv->an_type,
			    reply.an_status,
			    reply.an_resp0,
			    reply.an_resp1,
			    reply.an_resp2,
			    i);
			return(EIO);
		}
		DELAY(100000);
	}

	return(0);
}

static void
an_dump_record(struct an_softc *sc, struct an_ltv_gen *ltv, char *string)
{
	u_int8_t		*ptr2;
	int			len;
	int			i;
	int			count = 0;
	char			buf[17], temp;

	len = ltv->an_len - 4;
	if_printf(sc->an_ifp, "RID %4x, Length %4d, Mode %s\n",
		ltv->an_type, ltv->an_len - 4, string);

	if (an_dump == 1 || (an_dump == ltv->an_type)) {
		if_printf(sc->an_ifp, "\t");
		bzero(buf,sizeof(buf));

		ptr2 = (u_int8_t *)&ltv->an_val;
		for (i = len; i > 0; i--) {
			printf("%02x ", *ptr2);

			temp = *ptr2++;
			if (isprint(temp))
				buf[count] = temp;
			else
				buf[count] = '.';
			if (++count == 16) {
				count = 0;
				printf("%s\n",buf);
				if_printf(sc->an_ifp, "\t");
				bzero(buf,sizeof(buf));
			}
		}
		for (; count != 16; count++) {
			printf("   ");
		}
		printf(" %s\n",buf);
	}
}

static int
an_seek(struct an_softc *sc, int id, int off, int chan)
{
	int			i;
	int			selreg, offreg;

	switch (chan) {
	case AN_BAP0:
		selreg = AN_SEL0;
		offreg = AN_OFF0;
		break;
	case AN_BAP1:
		selreg = AN_SEL1;
		offreg = AN_OFF1;
		break;
	default:
		if_printf(sc->an_ifp, "invalid data path: %x\n", chan);
		return(EIO);
	}

	CSR_WRITE_2(sc, selreg, id);
	CSR_WRITE_2(sc, offreg, off);

	for (i = 0; i < AN_TIMEOUT; i++) {
		if (!(CSR_READ_2(sc, offreg) & (AN_OFF_BUSY|AN_OFF_ERR)))
			break;
	}

	if (i == AN_TIMEOUT)
		return(ETIMEDOUT);

	return(0);
}

static int
an_read_data(struct an_softc *sc, int id, int off, caddr_t buf, int len)
{
	int			i;
	u_int16_t		*ptr;
	u_int8_t		*ptr2;

	if (off != -1) {
		if (an_seek(sc, id, off, AN_BAP1))
			return(EIO);
	}

	ptr = (u_int16_t *)buf;
	for (i = len; i > 1; i -= 2)
		*ptr++ = CSR_READ_2(sc, AN_DATA1);
	if (i) {
		ptr2 = (u_int8_t *)ptr;
		*ptr2 = CSR_READ_1(sc, AN_DATA1);
	}

	return(0);
}

static int
an_write_data(struct an_softc *sc, int id, int off, caddr_t buf, int len)
{
	int			i;
	u_int16_t		*ptr;
	u_int8_t		*ptr2;

	if (off != -1) {
		if (an_seek(sc, id, off, AN_BAP0))
			return(EIO);
	}

	ptr = (u_int16_t *)buf;
	for (i = len; i > 1; i -= 2)
		CSR_WRITE_2(sc, AN_DATA0, *ptr++);
	if (i) {
		ptr2 = (u_int8_t *)ptr;
		CSR_WRITE_1(sc, AN_DATA0, *ptr2);
	}

	return(0);
}

/*
 * Allocate a region of memory inside the NIC and zero
 * it out.
 */
static int
an_alloc_nicmem(struct an_softc *sc, int len, int *id)
{
	int			i;

	if (an_cmd(sc, AN_CMD_ALLOC_MEM, len)) {
		if_printf(sc->an_ifp, "failed to allocate %d bytes on NIC\n",
		    len);
		return(ENOMEM);
	}

	for (i = 0; i < AN_TIMEOUT; i++) {
		if (CSR_READ_2(sc, AN_EVENT_STAT(sc->mpi350)) & AN_EV_ALLOC)
			break;
	}

	if (i == AN_TIMEOUT)
		return(ETIMEDOUT);

	CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_ALLOC);
	*id = CSR_READ_2(sc, AN_ALLOC_FID);

	if (an_seek(sc, *id, 0, AN_BAP0))
		return(EIO);

	for (i = 0; i < len / 2; i++)
		CSR_WRITE_2(sc, AN_DATA0, 0);

	return(0);
}

static void
an_setdef(struct an_softc *sc, struct an_req *areq)
{
	struct ifnet		*ifp;
	struct an_ltv_genconfig	*cfg;
	struct an_ltv_ssidlist_new	*ssid;
	struct an_ltv_aplist	*ap;
	struct an_ltv_gen	*sp;

	ifp = sc->an_ifp;

	AN_LOCK_ASSERT(sc);
	switch (areq->an_type) {
	case AN_RID_GENCONFIG:
		cfg = (struct an_ltv_genconfig *)areq;

		bcopy((char *)&cfg->an_macaddr, IF_LLADDR(sc->an_ifp),
		    ETHER_ADDR_LEN);

		bcopy((char *)cfg, (char *)&sc->an_config,
			sizeof(struct an_ltv_genconfig));
		break;
	case AN_RID_SSIDLIST:
		ssid = (struct an_ltv_ssidlist_new *)areq;
		bcopy((char *)ssid, (char *)&sc->an_ssidlist,
			sizeof(struct an_ltv_ssidlist_new));
		break;
	case AN_RID_APLIST:
		ap = (struct an_ltv_aplist *)areq;
		bcopy((char *)ap, (char *)&sc->an_aplist,
			sizeof(struct an_ltv_aplist));
		break;
	case AN_RID_TX_SPEED:
		sp = (struct an_ltv_gen *)areq;
		sc->an_tx_rate = sp->an_val;

		/* Read the current configuration */
		sc->an_config.an_type = AN_RID_GENCONFIG;
		sc->an_config.an_len = sizeof(struct an_ltv_genconfig);
		an_read_record(sc, (struct an_ltv_gen *)&sc->an_config);
		cfg = &sc->an_config;

		/* clear other rates and set the only one we want */
		bzero(cfg->an_rates, sizeof(cfg->an_rates));
		cfg->an_rates[0] = sc->an_tx_rate;

		/* Save the new rate */
		sc->an_config.an_type = AN_RID_GENCONFIG;
		sc->an_config.an_len = sizeof(struct an_ltv_genconfig);
		break;
	case AN_RID_WEP_TEMP:
		/* Cache the temp keys */
		bcopy(areq,
		    &sc->an_temp_keys[((struct an_ltv_key *)areq)->kindex],
		    sizeof(struct an_ltv_key));
	case AN_RID_WEP_PERM:
	case AN_RID_LEAPUSERNAME:
	case AN_RID_LEAPPASSWORD:
		an_init_locked(sc);

		/* Disable the MAC. */
		an_cmd(sc, AN_CMD_DISABLE, 0);

		/* Write the key */
		an_write_record(sc, (struct an_ltv_gen *)areq);

		/* Turn the MAC back on. */
		an_cmd(sc, AN_CMD_ENABLE, 0);

		break;
	case AN_RID_MONITOR_MODE:
		cfg = (struct an_ltv_genconfig *)areq;
		bpfdetach(ifp);
		if (ng_ether_detach_p != NULL)
			(*ng_ether_detach_p) (ifp);
		sc->an_monitor = cfg->an_len;

		if (sc->an_monitor & AN_MONITOR) {
			if (sc->an_monitor & AN_MONITOR_AIRONET_HEADER) {
				bpfattach(ifp, DLT_AIRONET_HEADER,
					sizeof(struct ether_header));
			} else {
				bpfattach(ifp, DLT_IEEE802_11,
					sizeof(struct ether_header));
			}
		} else {
			bpfattach(ifp, DLT_EN10MB,
				  sizeof(struct ether_header));
			if (ng_ether_attach_p != NULL)
				(*ng_ether_attach_p) (ifp);
		}
		break;
	default:
		if_printf(ifp, "unknown RID: %x\n", areq->an_type);
		return;
	}


	/* Reinitialize the card. */
	if (ifp->if_flags)
		an_init_locked(sc);

	return;
}

/*
 * Derived from Linux driver to enable promiscious mode.
 */

static void
an_promisc(struct an_softc *sc, int promisc)
{
	AN_LOCK_ASSERT(sc);
	if (sc->an_was_monitor) {
		an_reset(sc);
		if (sc->mpi350)
			an_init_mpi350_desc(sc);
	}
	if (sc->an_monitor || sc->an_was_monitor)
		an_init_locked(sc);

	sc->an_was_monitor = sc->an_monitor;
	an_cmd(sc, AN_CMD_SET_MODE, promisc ? 0xffff : 0);

	return;
}

static int
an_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	int			error = 0;
	int			len;
	int			i, max;
	struct an_softc		*sc;
	struct ifreq		*ifr;
	struct thread		*td = curthread;
	struct ieee80211req	*ireq;
	struct ieee80211_channel	ch;
	u_int8_t		tmpstr[IEEE80211_NWID_LEN*2];
	u_int8_t		*tmpptr;
	struct an_ltv_genconfig	*config;
	struct an_ltv_key	*key;
	struct an_ltv_status	*status;
	struct an_ltv_ssidlist_new	*ssids;
	int			mode;
	struct aironet_ioctl	l_ioctl;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	ireq = (struct ieee80211req *)data;

	config = (struct an_ltv_genconfig *)&sc->areq;
	key = (struct an_ltv_key *)&sc->areq;
	status = (struct an_ltv_status *)&sc->areq;
	ssids = (struct an_ltv_ssidlist_new *)&sc->areq;

	if (sc->an_gone) {
		error = ENODEV;
		goto out;
	}

	switch (command) {
	case SIOCSIFFLAGS:
		AN_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->an_if_flags & IFF_PROMISC)) {
				an_promisc(sc, 1);
			} else if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->an_if_flags & IFF_PROMISC) {
				an_promisc(sc, 0);
			} else
				an_init_locked(sc);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				an_stop(sc);
		}
		sc->an_if_flags = ifp->if_flags;
		AN_UNLOCK(sc);
		error = 0;
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->an_ifmedia, command);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* The Aironet has no multicast filter. */
		error = 0;
		break;
	case SIOCGAIRONET:
		error = copyin(ifr_data_get_ptr(ifr), &sc->areq,
		    sizeof(sc->areq));
		if (error != 0)
			break;
		AN_LOCK(sc);
#ifdef ANCACHE
		if (sc->areq.an_type == AN_RID_ZERO_CACHE) {
			error = priv_check(td, PRIV_DRIVER);
			if (error)
				break;
			sc->an_sigitems = sc->an_nextitem = 0;
			break;
		} else if (sc->areq.an_type == AN_RID_READ_CACHE) {
			char *pt = (char *)&sc->areq.an_val;
			bcopy((char *)&sc->an_sigitems, (char *)pt,
			    sizeof(int));
			pt += sizeof(int);
			sc->areq.an_len = sizeof(int) / 2;
			bcopy((char *)&sc->an_sigcache, (char *)pt,
			    sizeof(struct an_sigcache) * sc->an_sigitems);
			sc->areq.an_len += ((sizeof(struct an_sigcache) *
			    sc->an_sigitems) / 2) + 1;
		} else
#endif
		if (an_read_record(sc, (struct an_ltv_gen *)&sc->areq)) {
			AN_UNLOCK(sc);
			error = EINVAL;
			break;
		}
		AN_UNLOCK(sc);
		error = copyout(&sc->areq, ifr_data_get_ptr(ifr),
		    sizeof(sc->areq));
		break;
	case SIOCSAIRONET:
		if ((error = priv_check(td, PRIV_DRIVER)))
			goto out;
		AN_LOCK(sc);
		error = copyin(ifr_data_get_ptr(ifr), &sc->areq,
		    sizeof(sc->areq));
		if (error != 0)
			break;
		an_setdef(sc, &sc->areq);
		AN_UNLOCK(sc);
		break;
	case SIOCGPRIVATE_0:		/* used by Cisco client utility */
		if ((error = priv_check(td, PRIV_DRIVER)))
			goto out;
		error = copyin(ifr_data_get_ptr(ifr), &l_ioctl,
		    sizeof(l_ioctl));
		if (error)
			goto out;
		mode = l_ioctl.command;

		AN_LOCK(sc);
		if (mode >= AIROGCAP && mode <= AIROGSTATSD32) {
			error = readrids(ifp, &l_ioctl);
		} else if (mode >= AIROPCAP && mode <= AIROPLEAPUSR) {
			error = writerids(ifp, &l_ioctl);
		} else if (mode >= AIROFLSHRST && mode <= AIRORESTART) {
			error = flashcard(ifp, &l_ioctl);
		} else {
			error =-1;
		}
		AN_UNLOCK(sc);
		if (!error) {
			/* copy out the updated command info */
			error = copyout(&l_ioctl, ifr_data_get_ptr(ifr),
			    sizeof(l_ioctl));
		}
		break;
	case SIOCGPRIVATE_1:		/* used by Cisco client utility */
		if ((error = priv_check(td, PRIV_DRIVER)))
			goto out;
		error = copyin(ifr_data_get_ptr(ifr), &l_ioctl,
		    sizeof(l_ioctl));
		if (error)
			goto out;
		l_ioctl.command = 0;
		error = AIROMAGIC;
		(void) copyout(&error, l_ioctl.data, sizeof(error));
		error = 0;
		break;
	case SIOCG80211:
		sc->areq.an_len = sizeof(sc->areq);
		/* was that a good idea DJA we are doing a short-cut */
		switch (ireq->i_type) {
		case IEEE80211_IOC_SSID:
			AN_LOCK(sc);
			if (ireq->i_val == -1) {
				sc->areq.an_type = AN_RID_STATUS;
				if (an_read_record(sc,
				    (struct an_ltv_gen *)&sc->areq)) {
					error = EINVAL;
					AN_UNLOCK(sc);
					break;
				}
				len = status->an_ssidlen;
				tmpptr = status->an_ssid;
			} else if (ireq->i_val >= 0) {
				sc->areq.an_type = AN_RID_SSIDLIST;
				if (an_read_record(sc,
				    (struct an_ltv_gen *)&sc->areq)) {
					error = EINVAL;
					AN_UNLOCK(sc);
					break;
				}
				max = (sc->areq.an_len - 4)
				    / sizeof(struct an_ltv_ssid_entry);
				if ( max > MAX_SSIDS ) {
					printf("To many SSIDs only using "
					    "%d of %d\n",
					    MAX_SSIDS, max);
					max = MAX_SSIDS;
				}
				if (ireq->i_val > max) {
					error = EINVAL;
					AN_UNLOCK(sc);
					break;
				} else {
					len = ssids->an_entry[ireq->i_val].an_len;
					tmpptr = ssids->an_entry[ireq->i_val].an_ssid;
				}
			} else {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			if (len > IEEE80211_NWID_LEN) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			AN_UNLOCK(sc);
			ireq->i_len = len;
			bzero(tmpstr, IEEE80211_NWID_LEN);
			bcopy(tmpptr, tmpstr, len);
			error = copyout(tmpstr, ireq->i_data,
			    IEEE80211_NWID_LEN);
			break;
		case IEEE80211_IOC_NUMSSIDS:
			AN_LOCK(sc);
			sc->areq.an_len = sizeof(sc->areq);
			sc->areq.an_type = AN_RID_SSIDLIST;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				AN_UNLOCK(sc);
				error = EINVAL;
				break;
			}
			max = (sc->areq.an_len - 4)
			    / sizeof(struct an_ltv_ssid_entry);
			AN_UNLOCK(sc);
			if ( max > MAX_SSIDS ) {
				printf("To many SSIDs only using "
				    "%d of %d\n",
				    MAX_SSIDS, max);
				max = MAX_SSIDS;
			}
			ireq->i_val = max;
			break;
		case IEEE80211_IOC_WEP:
			AN_LOCK(sc);
			sc->areq.an_type = AN_RID_ACTUALCFG;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			AN_UNLOCK(sc);
			if (config->an_authtype & AN_AUTHTYPE_PRIVACY_IN_USE) {
				if (config->an_authtype &
				    AN_AUTHTYPE_ALLOW_UNENCRYPTED)
					ireq->i_val = IEEE80211_WEP_MIXED;
				else
					ireq->i_val = IEEE80211_WEP_ON;
			} else {
				ireq->i_val = IEEE80211_WEP_OFF;
			}
			break;
		case IEEE80211_IOC_WEPKEY:
			/*
			 * XXX: I'm not entierly convinced this is
			 * correct, but it's what is implemented in
			 * ancontrol so it will have to do until we get
			 * access to actual Cisco code.
			 */
			if (ireq->i_val < 0 || ireq->i_val > 8) {
				error = EINVAL;
				break;
			}
			len = 0;
			if (ireq->i_val < 5) {
				AN_LOCK(sc);
				sc->areq.an_type = AN_RID_WEP_TEMP;
				for (i = 0; i < 5; i++) {
					if (an_read_record(sc,
					    (struct an_ltv_gen *)&sc->areq)) {
						error = EINVAL;
						break;
					}
					if (key->kindex == 0xffff)
						break;
					if (key->kindex == ireq->i_val)
						len = key->klen;
					/* Required to get next entry */
					sc->areq.an_type = AN_RID_WEP_PERM;
				}
				AN_UNLOCK(sc);
				if (error != 0) {
					break;
				}
			}
			/* We aren't allowed to read the value of the
			 * key from the card so we just output zeros
			 * like we would if we could read the card, but
			 * denied the user access.
			 */
			bzero(tmpstr, len);
			ireq->i_len = len;
			error = copyout(tmpstr, ireq->i_data, len);
			break;
		case IEEE80211_IOC_NUMWEPKEYS:
			ireq->i_val = 9; /* include home key */
			break;
		case IEEE80211_IOC_WEPTXKEY:
			/*
			 * For some strange reason, you have to read all
			 * keys before you can read the txkey.
			 */
			AN_LOCK(sc);
			sc->areq.an_type = AN_RID_WEP_TEMP;
			for (i = 0; i < 5; i++) {
				if (an_read_record(sc,
				    (struct an_ltv_gen *) &sc->areq)) {
					error = EINVAL;
					break;
				}
				if (key->kindex == 0xffff) {
					break;
				}
				/* Required to get next entry */
				sc->areq.an_type = AN_RID_WEP_PERM;
			}
			if (error != 0) {
				AN_UNLOCK(sc);
				break;
			}

			sc->areq.an_type = AN_RID_WEP_PERM;
			key->kindex = 0xffff;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			ireq->i_val = key->mac[0];
			/*
			 * Check for home mode.  Map home mode into
			 * 5th key since that is how it is stored on
			 * the card
			 */
			sc->areq.an_len  = sizeof(struct an_ltv_genconfig);
			sc->areq.an_type = AN_RID_GENCONFIG;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			if (config->an_home_product & AN_HOME_NETWORK)
				ireq->i_val = 4;
			AN_UNLOCK(sc);
			break;
		case IEEE80211_IOC_AUTHMODE:
			AN_LOCK(sc);
			sc->areq.an_type = AN_RID_ACTUALCFG;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			AN_UNLOCK(sc);
			if ((config->an_authtype & AN_AUTHTYPE_MASK) ==
			    AN_AUTHTYPE_NONE) {
			    ireq->i_val = IEEE80211_AUTH_NONE;
			} else if ((config->an_authtype & AN_AUTHTYPE_MASK) ==
			    AN_AUTHTYPE_OPEN) {
			    ireq->i_val = IEEE80211_AUTH_OPEN;
			} else if ((config->an_authtype & AN_AUTHTYPE_MASK) ==
			    AN_AUTHTYPE_SHAREDKEY) {
			    ireq->i_val = IEEE80211_AUTH_SHARED;
			} else
				error = EINVAL;
			break;
		case IEEE80211_IOC_STATIONNAME:
			AN_LOCK(sc);
			sc->areq.an_type = AN_RID_ACTUALCFG;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			AN_UNLOCK(sc);
			ireq->i_len = sizeof(config->an_nodename);
			tmpptr = config->an_nodename;
			bzero(tmpstr, IEEE80211_NWID_LEN);
			bcopy(tmpptr, tmpstr, ireq->i_len);
			error = copyout(tmpstr, ireq->i_data,
			    IEEE80211_NWID_LEN);
			break;
		case IEEE80211_IOC_CHANNEL:
			AN_LOCK(sc);
			sc->areq.an_type = AN_RID_STATUS;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			AN_UNLOCK(sc);
			ireq->i_val = status->an_cur_channel;
			break;
		case IEEE80211_IOC_CURCHAN:
			AN_LOCK(sc);
			sc->areq.an_type = AN_RID_STATUS;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			AN_UNLOCK(sc);
			bzero(&ch, sizeof(ch));
			ch.ic_freq = ieee80211_ieee2mhz(status->an_cur_channel,
			    IEEE80211_CHAN_B);
			ch.ic_flags = IEEE80211_CHAN_B;
			ch.ic_ieee = status->an_cur_channel;
			error = copyout(&ch, ireq->i_data, sizeof(ch));
			break;
		case IEEE80211_IOC_POWERSAVE:
			AN_LOCK(sc);
			sc->areq.an_type = AN_RID_ACTUALCFG;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			AN_UNLOCK(sc);
			if (config->an_psave_mode == AN_PSAVE_NONE) {
				ireq->i_val = IEEE80211_POWERSAVE_OFF;
			} else if (config->an_psave_mode == AN_PSAVE_CAM) {
				ireq->i_val = IEEE80211_POWERSAVE_CAM;
			} else if (config->an_psave_mode == AN_PSAVE_PSP) {
				ireq->i_val = IEEE80211_POWERSAVE_PSP;
			} else if (config->an_psave_mode == AN_PSAVE_PSP_CAM) {
				ireq->i_val = IEEE80211_POWERSAVE_PSP_CAM;
			} else
				error = EINVAL;
			break;
		case IEEE80211_IOC_POWERSAVESLEEP:
			AN_LOCK(sc);
			sc->areq.an_type = AN_RID_ACTUALCFG;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			AN_UNLOCK(sc);
			ireq->i_val = config->an_listen_interval;
			break;
		}
		break;
	case SIOCS80211:
		if ((error = priv_check(td, PRIV_NET80211_MANAGE)))
			goto out;
		AN_LOCK(sc);
		sc->areq.an_len = sizeof(sc->areq);
		/*
		 * We need a config structure for everything but the WEP
		 * key management and SSIDs so we get it now so avoid
		 * duplicating this code every time.
		 */
		if (ireq->i_type != IEEE80211_IOC_SSID &&
		    ireq->i_type != IEEE80211_IOC_WEPKEY &&
		    ireq->i_type != IEEE80211_IOC_WEPTXKEY) {
			sc->areq.an_type = AN_RID_GENCONFIG;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
		}
		switch (ireq->i_type) {
		case IEEE80211_IOC_SSID:
			sc->areq.an_len = sizeof(sc->areq);
			sc->areq.an_type = AN_RID_SSIDLIST;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			if (ireq->i_len > IEEE80211_NWID_LEN) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			max = (sc->areq.an_len - 4)
			    / sizeof(struct an_ltv_ssid_entry);
			if ( max > MAX_SSIDS ) {
				printf("To many SSIDs only using "
				    "%d of %d\n",
				    MAX_SSIDS, max);
				max = MAX_SSIDS;
			}
			if (ireq->i_val > max) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			} else {
				error = copyin(ireq->i_data,
				    ssids->an_entry[ireq->i_val].an_ssid,
				    ireq->i_len);
				ssids->an_entry[ireq->i_val].an_len
				    = ireq->i_len;
				sc->areq.an_len = sizeof(sc->areq);
				sc->areq.an_type = AN_RID_SSIDLIST;
				an_setdef(sc, &sc->areq);
				AN_UNLOCK(sc);
				break;
			}
			break;
		case IEEE80211_IOC_WEP:
			switch (ireq->i_val) {
			case IEEE80211_WEP_OFF:
				config->an_authtype &=
				    ~(AN_AUTHTYPE_PRIVACY_IN_USE |
				    AN_AUTHTYPE_ALLOW_UNENCRYPTED);
				break;
			case IEEE80211_WEP_ON:
				config->an_authtype |=
				    AN_AUTHTYPE_PRIVACY_IN_USE;
				config->an_authtype &=
				    ~AN_AUTHTYPE_ALLOW_UNENCRYPTED;
				break;
			case IEEE80211_WEP_MIXED:
				config->an_authtype |=
				    AN_AUTHTYPE_PRIVACY_IN_USE |
				    AN_AUTHTYPE_ALLOW_UNENCRYPTED;
				break;
			default:
				error = EINVAL;
				break;
			}
			if (error != EINVAL)
				an_setdef(sc, &sc->areq);
			AN_UNLOCK(sc);
			break;
		case IEEE80211_IOC_WEPKEY:
			if (ireq->i_val < 0 || ireq->i_val > 8 ||
			    ireq->i_len > 13) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			error = copyin(ireq->i_data, tmpstr, 13);
			if (error != 0) {
				AN_UNLOCK(sc);
				break;
			}
			/*
			 * Map the 9th key into the home mode
			 * since that is how it is stored on
			 * the card
			 */
			bzero(&sc->areq, sizeof(struct an_ltv_key));
			sc->areq.an_len = sizeof(struct an_ltv_key);
			key->mac[0] = 1;	/* The others are 0. */
			if (ireq->i_val < 4) {
				sc->areq.an_type = AN_RID_WEP_TEMP;
				key->kindex = ireq->i_val;
			} else {
				sc->areq.an_type = AN_RID_WEP_PERM;
				key->kindex = ireq->i_val - 4;
			}
			key->klen = ireq->i_len;
			bcopy(tmpstr, key->key, key->klen);
			an_setdef(sc, &sc->areq);
			AN_UNLOCK(sc);
			break;
		case IEEE80211_IOC_WEPTXKEY:
			if (ireq->i_val < 0 || ireq->i_val > 4) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}

			/*
			 * Map the 5th key into the home mode
			 * since that is how it is stored on
			 * the card
			 */
			sc->areq.an_len  = sizeof(struct an_ltv_genconfig);
			sc->areq.an_type = AN_RID_ACTUALCFG;
			if (an_read_record(sc,
			    (struct an_ltv_gen *)&sc->areq)) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			if (ireq->i_val ==  4) {
				config->an_home_product |= AN_HOME_NETWORK;
				ireq->i_val = 0;
			} else {
				config->an_home_product &= ~AN_HOME_NETWORK;
			}

			sc->an_config.an_home_product
				= config->an_home_product;

			/* update configuration */
			an_init_locked(sc);

			bzero(&sc->areq, sizeof(struct an_ltv_key));
			sc->areq.an_len = sizeof(struct an_ltv_key);
			sc->areq.an_type = AN_RID_WEP_PERM;
			key->kindex = 0xffff;
			key->mac[0] = ireq->i_val;
			an_setdef(sc, &sc->areq);
			AN_UNLOCK(sc);
			break;
		case IEEE80211_IOC_AUTHMODE:
			switch (ireq->i_val) {
			case IEEE80211_AUTH_NONE:
				config->an_authtype = AN_AUTHTYPE_NONE |
				    (config->an_authtype & ~AN_AUTHTYPE_MASK);
				break;
			case IEEE80211_AUTH_OPEN:
				config->an_authtype = AN_AUTHTYPE_OPEN |
				    (config->an_authtype & ~AN_AUTHTYPE_MASK);
				break;
			case IEEE80211_AUTH_SHARED:
				config->an_authtype = AN_AUTHTYPE_SHAREDKEY |
				    (config->an_authtype & ~AN_AUTHTYPE_MASK);
				break;
			default:
				error = EINVAL;
			}
			if (error != EINVAL) {
				an_setdef(sc, &sc->areq);
			}
			AN_UNLOCK(sc);
			break;
		case IEEE80211_IOC_STATIONNAME:
			if (ireq->i_len > 16) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			bzero(config->an_nodename, 16);
			error = copyin(ireq->i_data,
			    config->an_nodename, ireq->i_len);
			an_setdef(sc, &sc->areq);
			AN_UNLOCK(sc);
			break;
		case IEEE80211_IOC_CHANNEL:
			/*
			 * The actual range is 1-14, but if you set it
			 * to 0 you get the default so we let that work
			 * too.
			 */
			if (ireq->i_val < 0 || ireq->i_val >14) {
				error = EINVAL;
				AN_UNLOCK(sc);
				break;
			}
			config->an_ds_channel = ireq->i_val;
			an_setdef(sc, &sc->areq);
			AN_UNLOCK(sc);
			break;
		case IEEE80211_IOC_POWERSAVE:
			switch (ireq->i_val) {
			case IEEE80211_POWERSAVE_OFF:
				config->an_psave_mode = AN_PSAVE_NONE;
				break;
			case IEEE80211_POWERSAVE_CAM:
				config->an_psave_mode = AN_PSAVE_CAM;
				break;
			case IEEE80211_POWERSAVE_PSP:
				config->an_psave_mode = AN_PSAVE_PSP;
				break;
			case IEEE80211_POWERSAVE_PSP_CAM:
				config->an_psave_mode = AN_PSAVE_PSP_CAM;
				break;
			default:
				error = EINVAL;
				break;
			}
			an_setdef(sc, &sc->areq);
			AN_UNLOCK(sc);
			break;
		case IEEE80211_IOC_POWERSAVESLEEP:
			config->an_listen_interval = ireq->i_val;
			an_setdef(sc, &sc->areq);
			AN_UNLOCK(sc);
			break;
		default:
			AN_UNLOCK(sc);
			break;
		}

		/*
		if (!error) {
			AN_LOCK(sc);
			an_setdef(sc, &sc->areq);
			AN_UNLOCK(sc);
		}
		*/
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
out:

	return(error != 0);
}

static int
an_init_tx_ring(struct an_softc *sc)
{
	int			i;
	int			id;

	if (sc->an_gone)
		return (0);

	if (!sc->mpi350) {
		for (i = 0; i < AN_TX_RING_CNT; i++) {
			if (an_alloc_nicmem(sc, 1518 +
			    0x44, &id))
				return(ENOMEM);
			sc->an_rdata.an_tx_fids[i] = id;
			sc->an_rdata.an_tx_ring[i] = 0;
		}
	}

	sc->an_rdata.an_tx_prod = 0;
	sc->an_rdata.an_tx_cons = 0;
	sc->an_rdata.an_tx_empty = 1;

	return(0);
}

static void
an_init(void *xsc)
{
	struct an_softc		*sc = xsc;

	AN_LOCK(sc);
	an_init_locked(sc);
	AN_UNLOCK(sc);
}

static void
an_init_locked(struct an_softc *sc)
{
	struct ifnet *ifp;

	AN_LOCK_ASSERT(sc);
	ifp = sc->an_ifp;
	if (sc->an_gone)
		return;

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		an_stop(sc);

	sc->an_associated = 0;

	/* Allocate the TX buffers */
	if (an_init_tx_ring(sc)) {
		an_reset(sc);
		if (sc->mpi350)
			an_init_mpi350_desc(sc);
		if (an_init_tx_ring(sc)) {
			if_printf(ifp, "tx buffer allocation failed\n");
			return;
		}
	}

	/* Set our MAC address. */
	bcopy((char *)IF_LLADDR(sc->an_ifp),
	    (char *)&sc->an_config.an_macaddr, ETHER_ADDR_LEN);

	if (ifp->if_flags & IFF_BROADCAST)
		sc->an_config.an_rxmode = AN_RXMODE_BC_ADDR;
	else
		sc->an_config.an_rxmode = AN_RXMODE_ADDR;

	if (ifp->if_flags & IFF_MULTICAST)
		sc->an_config.an_rxmode = AN_RXMODE_BC_MC_ADDR;

	if (ifp->if_flags & IFF_PROMISC) {
		if (sc->an_monitor & AN_MONITOR) {
			if (sc->an_monitor & AN_MONITOR_ANY_BSS) {
				sc->an_config.an_rxmode |=
				    AN_RXMODE_80211_MONITOR_ANYBSS |
				    AN_RXMODE_NO_8023_HEADER;
			} else {
				sc->an_config.an_rxmode |=
				    AN_RXMODE_80211_MONITOR_CURBSS |
				    AN_RXMODE_NO_8023_HEADER;
			}
		}
	}

#ifdef ANCACHE
	if (sc->an_have_rssimap)
		sc->an_config.an_rxmode |= AN_RXMODE_NORMALIZED_RSSI;
#endif

	/* Set the ssid list */
	sc->an_ssidlist.an_type = AN_RID_SSIDLIST;
	sc->an_ssidlist.an_len = sizeof(struct an_ltv_ssidlist_new);
	if (an_write_record(sc, (struct an_ltv_gen *)&sc->an_ssidlist)) {
		if_printf(ifp, "failed to set ssid list\n");
		return;
	}

	/* Set the AP list */
	sc->an_aplist.an_type = AN_RID_APLIST;
	sc->an_aplist.an_len = sizeof(struct an_ltv_aplist);
	if (an_write_record(sc, (struct an_ltv_gen *)&sc->an_aplist)) {
		if_printf(ifp, "failed to set AP list\n");
		return;
	}

	/* Set the configuration in the NIC */
	sc->an_config.an_len = sizeof(struct an_ltv_genconfig);
	sc->an_config.an_type = AN_RID_GENCONFIG;
	if (an_write_record(sc, (struct an_ltv_gen *)&sc->an_config)) {
		if_printf(ifp, "failed to set configuration\n");
		return;
	}

	/* Enable the MAC */
	if (an_cmd(sc, AN_CMD_ENABLE, 0)) {
		if_printf(ifp, "failed to enable MAC\n");
		return;
	}

	if (ifp->if_flags & IFF_PROMISC)
		an_cmd(sc, AN_CMD_SET_MODE, 0xffff);

	/* enable interrupts */
	CSR_WRITE_2(sc, AN_INT_EN(sc->mpi350), AN_INTRS(sc->mpi350));

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->an_stat_ch, hz, an_stats_update, sc);

	return;
}

static void
an_start(struct ifnet *ifp)
{
	struct an_softc		*sc;

	sc = ifp->if_softc;
	AN_LOCK(sc);
	an_start_locked(ifp);
	AN_UNLOCK(sc);
}

static void
an_start_locked(struct ifnet *ifp)
{
	struct an_softc		*sc;
	struct mbuf		*m0 = NULL;
	struct an_txframe_802_3	tx_frame_802_3;
	struct ether_header	*eh;
	int			id, idx, i;
	unsigned char		txcontrol;
	struct an_card_tx_desc an_tx_desc;
	u_int8_t		*buf;

	sc = ifp->if_softc;

	AN_LOCK_ASSERT(sc);
	if (sc->an_gone)
		return;

	if (ifp->if_drv_flags & IFF_DRV_OACTIVE)
		return;

	if (!sc->an_associated)
		return;

	/* We can't send in monitor mode so toss any attempts. */
	if (sc->an_monitor && (ifp->if_flags & IFF_PROMISC)) {
		for (;;) {
			IFQ_DRV_DEQUEUE(&ifp->if_snd, m0);
			if (m0 == NULL)
				break;
			m_freem(m0);
		}
		return;
	}

	idx = sc->an_rdata.an_tx_prod;

	if (!sc->mpi350) {
		bzero((char *)&tx_frame_802_3, sizeof(tx_frame_802_3));

		while (sc->an_rdata.an_tx_ring[idx] == 0) {
			IFQ_DRV_DEQUEUE(&ifp->if_snd, m0);
			if (m0 == NULL)
				break;

			id = sc->an_rdata.an_tx_fids[idx];
			eh = mtod(m0, struct ether_header *);

			bcopy((char *)&eh->ether_dhost,
			      (char *)&tx_frame_802_3.an_tx_dst_addr,
			      ETHER_ADDR_LEN);
			bcopy((char *)&eh->ether_shost,
			      (char *)&tx_frame_802_3.an_tx_src_addr,
			      ETHER_ADDR_LEN);

			/* minus src/dest mac & type */
			tx_frame_802_3.an_tx_802_3_payload_len =
				m0->m_pkthdr.len - 12;

			m_copydata(m0, sizeof(struct ether_header) - 2 ,
				   tx_frame_802_3.an_tx_802_3_payload_len,
				   (caddr_t)&sc->an_txbuf);

			txcontrol = AN_TXCTL_8023 | AN_TXCTL_HW(sc->mpi350);
			/* write the txcontrol only */
			an_write_data(sc, id, 0x08, (caddr_t)&txcontrol,
				      sizeof(txcontrol));

			/* 802_3 header */
			an_write_data(sc, id, 0x34, (caddr_t)&tx_frame_802_3,
				      sizeof(struct an_txframe_802_3));

			/* in mbuf header type is just before payload */
			an_write_data(sc, id, 0x44, (caddr_t)&sc->an_txbuf,
				      tx_frame_802_3.an_tx_802_3_payload_len);

			/*
			 * If there's a BPF listner, bounce a copy of
			 * this frame to him.
			 */
			BPF_MTAP(ifp, m0);

			m_freem(m0);
			m0 = NULL;

			sc->an_rdata.an_tx_ring[idx] = id;
			if (an_cmd(sc, AN_CMD_TX, id))
				if_printf(ifp, "xmit failed\n");

			AN_INC(idx, AN_TX_RING_CNT);

			/*
			 * Set a timeout in case the chip goes out to lunch.
			 */
			sc->an_timer = 5;
		}
	} else { /* MPI-350 */
		/* Disable interrupts. */
		CSR_WRITE_2(sc, AN_INT_EN(sc->mpi350), 0);

		while (sc->an_rdata.an_tx_empty ||
		    idx != sc->an_rdata.an_tx_cons) {
			IFQ_DRV_DEQUEUE(&ifp->if_snd, m0);
			if (m0 == NULL) {
				break;
			}
			buf = sc->an_tx_buffer[idx].an_dma_vaddr;

			eh = mtod(m0, struct ether_header *);

			/* DJA optimize this to limit bcopy */
			bcopy((char *)&eh->ether_dhost,
			      (char *)&tx_frame_802_3.an_tx_dst_addr,
			      ETHER_ADDR_LEN);
			bcopy((char *)&eh->ether_shost,
			      (char *)&tx_frame_802_3.an_tx_src_addr,
			      ETHER_ADDR_LEN);

			/* minus src/dest mac & type */
			tx_frame_802_3.an_tx_802_3_payload_len =
				m0->m_pkthdr.len - 12;

			m_copydata(m0, sizeof(struct ether_header) - 2 ,
				   tx_frame_802_3.an_tx_802_3_payload_len,
				   (caddr_t)&sc->an_txbuf);

			txcontrol = AN_TXCTL_8023 | AN_TXCTL_HW(sc->mpi350);
			/* write the txcontrol only */
			bcopy((caddr_t)&txcontrol, &buf[0x08],
			      sizeof(txcontrol));

			/* 802_3 header */
			bcopy((caddr_t)&tx_frame_802_3, &buf[0x34],
			      sizeof(struct an_txframe_802_3));

			/* in mbuf header type is just before payload */
			bcopy((caddr_t)&sc->an_txbuf, &buf[0x44],
			      tx_frame_802_3.an_tx_802_3_payload_len);


			bzero(&an_tx_desc, sizeof(an_tx_desc));
			an_tx_desc.an_offset = 0;
			an_tx_desc.an_eoc = 1;
			an_tx_desc.an_valid = 1;
			an_tx_desc.an_len =  0x44 +
			    tx_frame_802_3.an_tx_802_3_payload_len;
			an_tx_desc.an_phys
			    = sc->an_tx_buffer[idx].an_dma_paddr;
			for (i = sizeof(an_tx_desc) / 4 - 1; i >= 0; i--) {
				CSR_MEM_AUX_WRITE_4(sc, AN_TX_DESC_OFFSET
				    /* zero for now */
				    + (0 * sizeof(an_tx_desc))
				    + (i * 4),
				    ((u_int32_t *)(void *)&an_tx_desc)[i]);
			}

			/*
			 * If there's a BPF listner, bounce a copy of
			 * this frame to him.
			 */
			BPF_MTAP(ifp, m0);

			m_freem(m0);
			m0 = NULL;
			AN_INC(idx, AN_MAX_TX_DESC);
			sc->an_rdata.an_tx_empty = 0;
			CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350), AN_EV_ALLOC);

			/*
			 * Set a timeout in case the chip goes out to lunch.
			 */
			sc->an_timer = 5;
		}

		/* Re-enable interrupts. */
		CSR_WRITE_2(sc, AN_INT_EN(sc->mpi350), AN_INTRS(sc->mpi350));
	}

	if (m0 != NULL)
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;

	sc->an_rdata.an_tx_prod = idx;

	return;
}

void
an_stop(struct an_softc *sc)
{
	struct ifnet		*ifp;
	int			i;

	AN_LOCK_ASSERT(sc);

	if (sc->an_gone)
		return;

	ifp = sc->an_ifp;

	an_cmd(sc, AN_CMD_FORCE_SYNCLOSS, 0);
	CSR_WRITE_2(sc, AN_INT_EN(sc->mpi350), 0);
	an_cmd(sc, AN_CMD_DISABLE, 0);

	for (i = 0; i < AN_TX_RING_CNT; i++)
		an_cmd(sc, AN_CMD_DEALLOC_MEM, sc->an_rdata.an_tx_fids[i]);

	callout_stop(&sc->an_stat_ch);

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING|IFF_DRV_OACTIVE);

	if (sc->an_flash_buffer) {
		free(sc->an_flash_buffer, M_DEVBUF);
		sc->an_flash_buffer = NULL;
	}
}

static void
an_watchdog(struct an_softc *sc)
{
	struct ifnet *ifp;

	AN_LOCK_ASSERT(sc);

	if (sc->an_gone)
		return;

	ifp = sc->an_ifp;
	if_printf(ifp, "device timeout\n");

	an_reset(sc);
	if (sc->mpi350)
		an_init_mpi350_desc(sc);
	an_init_locked(sc);

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
}

int
an_shutdown(device_t dev)
{
	struct an_softc		*sc;

	sc = device_get_softc(dev);
	AN_LOCK(sc);
	an_stop(sc);
	sc->an_gone = 1;
	AN_UNLOCK(sc);

	return (0);
}

void
an_resume(device_t dev)
{
	struct an_softc		*sc;
	struct ifnet		*ifp;
	int			i;

	sc = device_get_softc(dev);
	AN_LOCK(sc);
	ifp = sc->an_ifp;

	sc->an_gone = 0;
	an_reset(sc);
	if (sc->mpi350)
		an_init_mpi350_desc(sc);
	an_init_locked(sc);

	/* Recovery temporary keys */
	for (i = 0; i < 4; i++) {
		sc->areq.an_type = AN_RID_WEP_TEMP;
		sc->areq.an_len = sizeof(struct an_ltv_key);
		bcopy(&sc->an_temp_keys[i],
		    &sc->areq, sizeof(struct an_ltv_key));
		an_setdef(sc, &sc->areq);
	}

	if (ifp->if_flags & IFF_UP)
		an_start_locked(ifp);
	AN_UNLOCK(sc);

	return;
}

#ifdef ANCACHE
/* Aironet signal strength cache code.
 * store signal/noise/quality on per MAC src basis in
 * a small fixed cache.  The cache wraps if > MAX slots
 * used.  The cache may be zeroed out to start over.
 * Two simple filters exist to reduce computation:
 * 1. ip only (literally 0x800, ETHERTYPE_IP) which may be used
 * to ignore some packets.  It defaults to ip only.
 * it could be used to focus on broadcast, non-IP 802.11 beacons.
 * 2. multicast/broadcast only.  This may be used to
 * ignore unicast packets and only cache signal strength
 * for multicast/broadcast packets (beacons); e.g., Mobile-IP
 * beacons and not unicast traffic.
 *
 * The cache stores (MAC src(index), IP src (major clue), signal,
 *	quality, noise)
 *
 * No apologies for storing IP src here.  It's easy and saves much
 * trouble elsewhere.  The cache is assumed to be INET dependent,
 * although it need not be.
 *
 * Note: the Aironet only has a single byte of signal strength value
 * in the rx frame header, and it's not scaled to anything sensible.
 * This is kind of lame, but it's all we've got.
 */

#ifdef documentation

int an_sigitems;				/* number of cached entries */
struct an_sigcache an_sigcache[MAXANCACHE];	/* array of cache entries */
int an_nextitem;				/* index/# of entries */


#endif

/* control variables for cache filtering.  Basic idea is
 * to reduce cost (e.g., to only Mobile-IP agent beacons
 * which are broadcast or multicast).  Still you might
 * want to measure signal strength anth unicast ping packets
 * on a pt. to pt. ant. setup.
 */
/* set true if you want to limit cache items to broadcast/mcast
 * only packets (not unicast).  Useful for mobile-ip beacons which
 * are broadcast/multicast at network layer.  Default is all packets
 * so ping/unicast anll work say anth pt. to pt. antennae setup.
 */
static int an_cache_mcastonly = 0;
SYSCTL_INT(_hw_an, OID_AUTO, an_cache_mcastonly, CTLFLAG_RW,
	&an_cache_mcastonly, 0, "");

/* set true if you want to limit cache items to IP packets only
*/
static int an_cache_iponly = 1;
SYSCTL_INT(_hw_an, OID_AUTO, an_cache_iponly, CTLFLAG_RW,
	&an_cache_iponly, 0, "");

/*
 * an_cache_store, per rx packet store signal
 * strength in MAC (src) indexed cache.
 */
static void
an_cache_store(struct an_softc *sc, struct ether_header *eh, struct mbuf *m,
    u_int8_t rx_rssi, u_int8_t rx_quality)
{
	struct ip *ip = NULL;
	int i;
	static int cache_slot = 0; 	/* use this cache entry */
	static int wrapindex = 0;	/* next "free" cache entry */
	int type_ipv4 = 0;

	/* filters:
	 * 1. ip only
	 * 2. configurable filter to throw out unicast packets,
	 * keep multicast only.
	 */

	if ((ntohs(eh->ether_type) == ETHERTYPE_IP)) {
		type_ipv4 = 1;
	}

	/* filter for ip packets only
	*/
	if ( an_cache_iponly && !type_ipv4) {
		return;
	}

	/* filter for broadcast/multicast only
	 */
	if (an_cache_mcastonly && ((eh->ether_dhost[0] & 1) == 0)) {
		return;
	}

#ifdef SIGDEBUG
	if_printf(sc->an_ifp, "q value %x (MSB=0x%x, LSB=0x%x) \n",
		rx_rssi & 0xffff, rx_rssi >> 8, rx_rssi & 0xff);
#endif

	/* find the ip header.  we want to store the ip_src
	 * address.
	 */
	if (type_ipv4) {
		ip = mtod(m, struct ip *);
	}

	/* do a linear search for a matching MAC address
	 * in the cache table
	 * . MAC address is 6 bytes,
	 * . var w_nextitem holds total number of entries already cached
	 */
	for (i = 0; i < sc->an_nextitem; i++) {
		if (! bcmp(eh->ether_shost , sc->an_sigcache[i].macsrc,  6 )) {
			/* Match!,
			 * so we already have this entry,
			 * update the data
			 */
			break;
		}
	}

	/* did we find a matching mac address?
	 * if yes, then overwrite a previously existing cache entry
	 */
	if (i < sc->an_nextitem )   {
		cache_slot = i;
	}
	/* else, have a new address entry,so
	 * add this new entry,
	 * if table full, then we need to replace LRU entry
	 */
	else    {

		/* check for space in cache table
		 * note: an_nextitem also holds number of entries
		 * added in the cache table
		 */
		if ( sc->an_nextitem < MAXANCACHE ) {
			cache_slot = sc->an_nextitem;
			sc->an_nextitem++;
			sc->an_sigitems = sc->an_nextitem;
		}
		/* no space found, so simply wrap anth wrap index
		 * and "zap" the next entry
		 */
		else {
			if (wrapindex == MAXANCACHE) {
				wrapindex = 0;
			}
			cache_slot = wrapindex++;
		}
	}

	/* invariant: cache_slot now points at some slot
	 * in cache.
	 */
	if (cache_slot < 0 || cache_slot >= MAXANCACHE) {
		log(LOG_ERR, "an_cache_store, bad index: %d of "
		    "[0..%d], gross cache error\n",
		    cache_slot, MAXANCACHE);
		return;
	}

	/*  store items in cache
	 *  .ip source address
	 *  .mac src
	 *  .signal, etc.
	 */
	if (type_ipv4) {
		sc->an_sigcache[cache_slot].ipsrc = ip->ip_src.s_addr;
	}
	bcopy( eh->ether_shost, sc->an_sigcache[cache_slot].macsrc,  6);


	switch (an_cache_mode) {
	case DBM:
		if (sc->an_have_rssimap) {
			sc->an_sigcache[cache_slot].signal =
				- sc->an_rssimap.an_entries[rx_rssi].an_rss_dbm;
			sc->an_sigcache[cache_slot].quality =
				- sc->an_rssimap.an_entries[rx_quality].an_rss_dbm;
		} else {
			sc->an_sigcache[cache_slot].signal = rx_rssi - 100;
			sc->an_sigcache[cache_slot].quality = rx_quality - 100;
		}
		break;
	case PERCENT:
		if (sc->an_have_rssimap) {
			sc->an_sigcache[cache_slot].signal =
				sc->an_rssimap.an_entries[rx_rssi].an_rss_pct;
			sc->an_sigcache[cache_slot].quality =
				sc->an_rssimap.an_entries[rx_quality].an_rss_pct;
		} else {
			if (rx_rssi > 100)
				rx_rssi = 100;
			if (rx_quality > 100)
				rx_quality = 100;
			sc->an_sigcache[cache_slot].signal = rx_rssi;
			sc->an_sigcache[cache_slot].quality = rx_quality;
		}
		break;
	case RAW:
		sc->an_sigcache[cache_slot].signal = rx_rssi;
		sc->an_sigcache[cache_slot].quality = rx_quality;
		break;
	}

	sc->an_sigcache[cache_slot].noise = 0;

	return;
}
#endif

static int
an_media_change(struct ifnet *ifp)
{
	struct an_softc *sc = ifp->if_softc;
	struct an_ltv_genconfig	*cfg;
	int otype = sc->an_config.an_opmode;
	int orate = sc->an_tx_rate;

	AN_LOCK(sc);
	sc->an_tx_rate = ieee80211_media2rate(
		IFM_SUBTYPE(sc->an_ifmedia.ifm_cur->ifm_media));
	if (sc->an_tx_rate < 0)
		sc->an_tx_rate = 0;

	if (orate != sc->an_tx_rate) {
		/* Read the current configuration */
		sc->an_config.an_type = AN_RID_GENCONFIG;
		sc->an_config.an_len = sizeof(struct an_ltv_genconfig);
		an_read_record(sc, (struct an_ltv_gen *)&sc->an_config);
		cfg = &sc->an_config;

		/* clear other rates and set the only one we want */
		bzero(cfg->an_rates, sizeof(cfg->an_rates));
		cfg->an_rates[0] = sc->an_tx_rate;

		/* Save the new rate */
		sc->an_config.an_type = AN_RID_GENCONFIG;
		sc->an_config.an_len = sizeof(struct an_ltv_genconfig);
	}

	if ((sc->an_ifmedia.ifm_cur->ifm_media & IFM_IEEE80211_ADHOC) != 0)
		sc->an_config.an_opmode &= ~AN_OPMODE_INFRASTRUCTURE_STATION;
	else
		sc->an_config.an_opmode |= AN_OPMODE_INFRASTRUCTURE_STATION;

	if (otype != sc->an_config.an_opmode ||
	    orate != sc->an_tx_rate)
		an_init_locked(sc);
	AN_UNLOCK(sc);

	return(0);
}

static void
an_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct an_ltv_status	status;
	struct an_softc		*sc = ifp->if_softc;

	imr->ifm_active = IFM_IEEE80211;

	AN_LOCK(sc);
	status.an_len = sizeof(status);
	status.an_type = AN_RID_STATUS;
	if (an_read_record(sc, (struct an_ltv_gen *)&status)) {
		/* If the status read fails, just lie. */
		imr->ifm_active = sc->an_ifmedia.ifm_cur->ifm_media;
		imr->ifm_status = IFM_AVALID|IFM_ACTIVE;
	}

	if (sc->an_tx_rate == 0) {
		imr->ifm_active = IFM_IEEE80211|IFM_AUTO;
	}

	if (sc->an_config.an_opmode == AN_OPMODE_IBSS_ADHOC)
		imr->ifm_active |= IFM_IEEE80211_ADHOC;
	imr->ifm_active |= ieee80211_rate2media(NULL,
		status.an_current_tx_rate, IEEE80211_MODE_AUTO);
	imr->ifm_status = IFM_AVALID;
	if (status.an_opmode & AN_STATUS_OPMODE_ASSOCIATED)
		imr->ifm_status |= IFM_ACTIVE;
	AN_UNLOCK(sc);
}

/********************** Cisco utility support routines *************/

/*
 * ReadRids & WriteRids derived from Cisco driver additions to Ben Reed's
 * Linux driver
 */

static int
readrids(struct ifnet *ifp, struct aironet_ioctl *l_ioctl)
{
	unsigned short  rid;
	struct an_softc *sc;
	int error;

	switch (l_ioctl->command) {
	case AIROGCAP:
		rid = AN_RID_CAPABILITIES;
		break;
	case AIROGCFG:
		rid = AN_RID_GENCONFIG;
		break;
	case AIROGSLIST:
		rid = AN_RID_SSIDLIST;
		break;
	case AIROGVLIST:
		rid = AN_RID_APLIST;
		break;
	case AIROGDRVNAM:
		rid = AN_RID_DRVNAME;
		break;
	case AIROGEHTENC:
		rid = AN_RID_ENCAPPROTO;
		break;
	case AIROGWEPKTMP:
		rid = AN_RID_WEP_TEMP;
		break;
	case AIROGWEPKNV:
		rid = AN_RID_WEP_PERM;
		break;
	case AIROGSTAT:
		rid = AN_RID_STATUS;
		break;
	case AIROGSTATSD32:
		rid = AN_RID_32BITS_DELTA;
		break;
	case AIROGSTATSC32:
		rid = AN_RID_32BITS_CUM;
		break;
	default:
		rid = 999;
		break;
	}

	if (rid == 999)	/* Is bad command */
		return -EINVAL;

	sc = ifp->if_softc;
	sc->areq.an_len  = AN_MAX_DATALEN;
	sc->areq.an_type = rid;

	an_read_record(sc, (struct an_ltv_gen *)&sc->areq);

	l_ioctl->len = sc->areq.an_len - 4;	/* just data */

	AN_UNLOCK(sc);
	/* the data contains the length at first */
	if (copyout(&(sc->areq.an_len), l_ioctl->data,
		    sizeof(sc->areq.an_len))) {
		error = -EFAULT;
		goto lock_exit;
	}
	/* Just copy the data back */
	if (copyout(&(sc->areq.an_val), l_ioctl->data + 2,
		    l_ioctl->len)) {
		error = -EFAULT;
		goto lock_exit;
	}
	error = 0;
lock_exit:
	AN_LOCK(sc);
	return (error);
}

static int
writerids(struct ifnet *ifp, struct aironet_ioctl *l_ioctl)
{
	struct an_softc *sc;
	int		rid, command, error;

	sc = ifp->if_softc;
	AN_LOCK_ASSERT(sc);
	rid = 0;
	command = l_ioctl->command;

	switch (command) {
	case AIROPSIDS:
		rid = AN_RID_SSIDLIST;
		break;
	case AIROPCAP:
		rid = AN_RID_CAPABILITIES;
		break;
	case AIROPAPLIST:
		rid = AN_RID_APLIST;
		break;
	case AIROPCFG:
		rid = AN_RID_GENCONFIG;
		break;
	case AIROPMACON:
		an_cmd(sc, AN_CMD_ENABLE, 0);
		return 0;
		break;
	case AIROPMACOFF:
		an_cmd(sc, AN_CMD_DISABLE, 0);
		return 0;
		break;
	case AIROPSTCLR:
		/*
		 * This command merely clears the counts does not actually
		 * store any data only reads rid. But as it changes the cards
		 * state, I put it in the writerid routines.
		 */

		rid = AN_RID_32BITS_DELTACLR;
		sc = ifp->if_softc;
		sc->areq.an_len = AN_MAX_DATALEN;
		sc->areq.an_type = rid;

		an_read_record(sc, (struct an_ltv_gen *)&sc->areq);
		l_ioctl->len = sc->areq.an_len - 4;	/* just data */

		AN_UNLOCK(sc);
		/* the data contains the length at first */
		error = copyout(&(sc->areq.an_len), l_ioctl->data,
			    sizeof(sc->areq.an_len));
		if (error) {
			AN_LOCK(sc);
			return -EFAULT;
		}
		/* Just copy the data */
		error = copyout(&(sc->areq.an_val), l_ioctl->data + 2,
			    l_ioctl->len);
		AN_LOCK(sc);
		if (error)
			return -EFAULT;
		return 0;
		break;
	case AIROPWEPKEY:
		rid = AN_RID_WEP_TEMP;
		break;
	case AIROPWEPKEYNV:
		rid = AN_RID_WEP_PERM;
		break;
	case AIROPLEAPUSR:
		rid = AN_RID_LEAPUSERNAME;
		break;
	case AIROPLEAPPWD:
		rid = AN_RID_LEAPPASSWORD;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (rid) {
		if (l_ioctl->len > sizeof(sc->areq.an_val) + 4)
			return -EINVAL;
		sc->areq.an_len = l_ioctl->len + 4;	/* add type & length */
		sc->areq.an_type = rid;

		/* Just copy the data back */
		AN_UNLOCK(sc);
		error = copyin((l_ioctl->data) + 2, &sc->areq.an_val,
		       l_ioctl->len);
		AN_LOCK(sc);
		if (error)
			return -EFAULT;

		an_cmd(sc, AN_CMD_DISABLE, 0);
		an_write_record(sc, (struct an_ltv_gen *)&sc->areq);
		an_cmd(sc, AN_CMD_ENABLE, 0);
		return 0;
	}
	return -EOPNOTSUPP;
}

/*
 * General Flash utilities derived from Cisco driver additions to Ben Reed's
 * Linux driver
 */

#define FLASH_DELAY(_sc, x)	msleep(ifp, &(_sc)->an_mtx, PZERO, \
	"flash", ((x) / hz) + 1);
#define FLASH_COMMAND	0x7e7e
#define FLASH_SIZE	32 * 1024

static int
unstickbusy(struct ifnet *ifp)
{
	struct an_softc *sc = ifp->if_softc;

	if (CSR_READ_2(sc, AN_COMMAND(sc->mpi350)) & AN_CMD_BUSY) {
		CSR_WRITE_2(sc, AN_EVENT_ACK(sc->mpi350),
			    AN_EV_CLR_STUCK_BUSY);
		return 1;
	}
	return 0;
}

/*
 * Wait for busy completion from card wait for delay uSec's Return true for
 * success meaning command reg is clear
 */

static int
WaitBusy(struct ifnet *ifp, int uSec)
{
	int		statword = 0xffff;
	int		delay = 0;
	struct an_softc	*sc = ifp->if_softc;

	while ((statword & AN_CMD_BUSY) && delay <= (1000 * 100)) {
		FLASH_DELAY(sc, 10);
		delay += 10;
		statword = CSR_READ_2(sc, AN_COMMAND(sc->mpi350));

		if ((AN_CMD_BUSY & statword) && (delay % 200)) {
			unstickbusy(ifp);
		}
	}

	return 0 == (AN_CMD_BUSY & statword);
}

/*
 * STEP 1) Disable MAC and do soft reset on card.
 */

static int
cmdreset(struct ifnet *ifp)
{
	int		status;
	struct an_softc	*sc = ifp->if_softc;

	AN_LOCK(sc);
	an_stop(sc);

	an_cmd(sc, AN_CMD_DISABLE, 0);

	if (!(status = WaitBusy(ifp, AN_TIMEOUT))) {
		if_printf(ifp, "Waitbusy hang b4 RESET =%d\n", status);
		AN_UNLOCK(sc);
		return -EBUSY;
	}
	CSR_WRITE_2(sc, AN_COMMAND(sc->mpi350), AN_CMD_FW_RESTART);

	FLASH_DELAY(sc, 1000);	/* WAS 600 12/7/00 */


	if (!(status = WaitBusy(ifp, 100))) {
		if_printf(ifp, "Waitbusy hang AFTER RESET =%d\n", status);
		AN_UNLOCK(sc);
		return -EBUSY;
	}
	AN_UNLOCK(sc);
	return 0;
}

/*
 * STEP 2) Put the card in legendary flash mode
 */

static int
setflashmode(struct ifnet *ifp)
{
	int		status;
	struct an_softc	*sc = ifp->if_softc;

	CSR_WRITE_2(sc, AN_SW0(sc->mpi350), FLASH_COMMAND);
	CSR_WRITE_2(sc, AN_SW1(sc->mpi350), FLASH_COMMAND);
	CSR_WRITE_2(sc, AN_SW0(sc->mpi350), FLASH_COMMAND);
	CSR_WRITE_2(sc, AN_COMMAND(sc->mpi350), FLASH_COMMAND);

	/*
	 * mdelay(500); // 500ms delay
	 */

	FLASH_DELAY(sc, 500);

	if (!(status = WaitBusy(ifp, AN_TIMEOUT))) {
		printf("Waitbusy hang after setflash mode\n");
		return -EIO;
	}
	return 0;
}

/*
 * Get a character from the card matching matchbyte Step 3)
 */

static int
flashgchar(struct ifnet *ifp, int matchbyte, int dwelltime)
{
	int		rchar;
	unsigned char	rbyte = 0;
	int		success = -1;
	struct an_softc	*sc = ifp->if_softc;


	do {
		rchar = CSR_READ_2(sc, AN_SW1(sc->mpi350));

		if (dwelltime && !(0x8000 & rchar)) {
			dwelltime -= 10;
			FLASH_DELAY(sc, 10);
			continue;
		}
		rbyte = 0xff & rchar;

		if ((rbyte == matchbyte) && (0x8000 & rchar)) {
			CSR_WRITE_2(sc, AN_SW1(sc->mpi350), 0);
			success = 1;
			break;
		}
		if (rbyte == 0x81 || rbyte == 0x82 || rbyte == 0x83 || rbyte == 0x1a || 0xffff == rchar)
			break;
		CSR_WRITE_2(sc, AN_SW1(sc->mpi350), 0);

	} while (dwelltime > 0);
	return success;
}

/*
 * Put character to SWS0 wait for dwelltime x 50us for  echo .
 */

static int
flashpchar(struct ifnet *ifp, int byte, int dwelltime)
{
	int		echo;
	int		pollbusy, waittime;
	struct an_softc	*sc = ifp->if_softc;

	byte |= 0x8000;

	if (dwelltime == 0)
		dwelltime = 200;

	waittime = dwelltime;

	/*
	 * Wait for busy bit d15 to go false indicating buffer empty
	 */
	do {
		pollbusy = CSR_READ_2(sc, AN_SW0(sc->mpi350));

		if (pollbusy & 0x8000) {
			FLASH_DELAY(sc, 50);
			waittime -= 50;
			continue;
		} else
			break;
	}
	while (waittime >= 0);

	/* timeout for busy clear wait */

	if (waittime <= 0) {
		if_printf(ifp, "flash putchar busywait timeout!\n");
		return -1;
	}
	/*
	 * Port is clear now write byte and wait for it to echo back
	 */
	do {
		CSR_WRITE_2(sc, AN_SW0(sc->mpi350), byte);
		FLASH_DELAY(sc, 50);
		dwelltime -= 50;
		echo = CSR_READ_2(sc, AN_SW1(sc->mpi350));
	} while (dwelltime >= 0 && echo != byte);


	CSR_WRITE_2(sc, AN_SW1(sc->mpi350), 0);

	return echo == byte;
}

/*
 * Transfer 32k of firmware data from user buffer to our buffer and send to
 * the card
 */

static int
flashputbuf(struct ifnet *ifp)
{
	unsigned short *bufp;
	int		nwords;
	struct an_softc	*sc = ifp->if_softc;

	/* Write stuff */

	bufp = sc->an_flash_buffer;

	if (!sc->mpi350) {
		CSR_WRITE_2(sc, AN_AUX_PAGE, 0x100);
		CSR_WRITE_2(sc, AN_AUX_OFFSET, 0);

		for (nwords = 0; nwords != FLASH_SIZE / 2; nwords++) {
			CSR_WRITE_2(sc, AN_AUX_DATA, bufp[nwords] & 0xffff);
		}
	} else {
		for (nwords = 0; nwords != FLASH_SIZE / 4; nwords++) {
			CSR_MEM_AUX_WRITE_4(sc, 0x8000,
				((u_int32_t *)bufp)[nwords] & 0xffff);
		}
	}

	CSR_WRITE_2(sc, AN_SW0(sc->mpi350), 0x8000);

	return 0;
}

/*
 * After flashing restart the card.
 */

static int
flashrestart(struct ifnet *ifp)
{
	int		status = 0;
	struct an_softc	*sc = ifp->if_softc;

	FLASH_DELAY(sc, 1024);		/* Added 12/7/00 */

	an_init_locked(sc);

	FLASH_DELAY(sc, 1024);		/* Added 12/7/00 */
	return status;
}

/*
 * Entry point for flash ioclt.
 */

static int
flashcard(struct ifnet *ifp, struct aironet_ioctl *l_ioctl)
{
	int		z = 0, status;
	struct an_softc	*sc;

	sc = ifp->if_softc;
	if (sc->mpi350) {
		if_printf(ifp, "flashing not supported on MPI 350 yet\n");
		return(-1);
	}
	status = l_ioctl->command;

	switch (l_ioctl->command) {
	case AIROFLSHRST:
		return cmdreset(ifp);
		break;
	case AIROFLSHSTFL:
		if (sc->an_flash_buffer) {
			free(sc->an_flash_buffer, M_DEVBUF);
			sc->an_flash_buffer = NULL;
		}
		sc->an_flash_buffer = malloc(FLASH_SIZE, M_DEVBUF, M_WAITOK);
		if (sc->an_flash_buffer)
			return setflashmode(ifp);
		else
			return ENOBUFS;
		break;
	case AIROFLSHGCHR:	/* Get char from aux */
		if (l_ioctl->len > sizeof(sc->areq)) {
			return -EINVAL;
		}
		AN_UNLOCK(sc);
		status = copyin(l_ioctl->data, &sc->areq, l_ioctl->len);
		AN_LOCK(sc);
		if (status)
			return status;
		z = *(int *)&sc->areq;
		if ((status = flashgchar(ifp, z, 8000)) == 1)
			return 0;
		else
			return -1;
	case AIROFLSHPCHR:	/* Send char to card. */
		if (l_ioctl->len > sizeof(sc->areq)) {
			return -EINVAL;
		}
		AN_UNLOCK(sc);
		status = copyin(l_ioctl->data, &sc->areq, l_ioctl->len);
		AN_LOCK(sc);
		if (status)
			return status;
		z = *(int *)&sc->areq;
		if ((status = flashpchar(ifp, z, 8000)) == -1)
			return -EIO;
		else
			return 0;
		break;
	case AIROFLPUTBUF:	/* Send 32k to card */
		if (l_ioctl->len > FLASH_SIZE) {
			if_printf(ifp, "Buffer to big, %x %x\n",
			       l_ioctl->len, FLASH_SIZE);
			return -EINVAL;
		}
		AN_UNLOCK(sc);
		status = copyin(l_ioctl->data, sc->an_flash_buffer, l_ioctl->len);
		AN_LOCK(sc);
		if (status)
			return status;

		if ((status = flashputbuf(ifp)) != 0)
			return -EIO;
		else
			return 0;
		break;
	case AIRORESTART:
		if ((status = flashrestart(ifp)) != 0) {
			if_printf(ifp, "FLASHRESTART returned %d\n", status);
			return -EIO;
		} else
			return 0;

		break;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}
