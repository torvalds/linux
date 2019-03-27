/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/kthread.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <compat/ndis/pe_var.h>
#include <compat/ndis/cfg_var.h>
#include <compat/ndis/resource_var.h>
#include <compat/ndis/ntoskrnl_var.h>
#include <compat/ndis/ndis_var.h>
#include <compat/ndis/hal_var.h>
#include <compat/ndis/usbd_var.h>
#include <dev/if_ndis/if_ndisvar.h>

#define NDIS_DUMMY_PATH "\\\\some\\bogus\\path"
#define	NDIS_FLAG_RDONLY 1

static void ndis_status_func(ndis_handle, ndis_status, void *, uint32_t);
static void ndis_statusdone_func(ndis_handle);
static void ndis_setdone_func(ndis_handle, ndis_status);
static void ndis_getdone_func(ndis_handle, ndis_status);
static void ndis_resetdone_func(ndis_handle, ndis_status, uint8_t);
static void ndis_sendrsrcavail_func(ndis_handle);
static void ndis_intrsetup(kdpc *, device_object *,
	irp *, struct ndis_softc *);
static void ndis_return(device_object *, void *);

static image_patch_table kernndis_functbl[] = {
	IMPORT_SFUNC(ndis_status_func, 4),
	IMPORT_SFUNC(ndis_statusdone_func, 1),
	IMPORT_SFUNC(ndis_setdone_func, 2),
	IMPORT_SFUNC(ndis_getdone_func, 2),
	IMPORT_SFUNC(ndis_resetdone_func, 3),
	IMPORT_SFUNC(ndis_sendrsrcavail_func, 1),
	IMPORT_SFUNC(ndis_intrsetup, 4),
	IMPORT_SFUNC(ndis_return, 1),

	{ NULL, NULL, NULL }
};

static struct nd_head ndis_devhead;

/*
 * This allows us to export our symbols to other modules.
 * Note that we call ourselves 'ndisapi' to avoid a namespace
 * collision with if_ndis.ko, which internally calls itself
 * 'ndis.'
 *
 * Note: some of the subsystems depend on each other, so the
 * order in which they're started is important. The order of
 * importance is:
 *
 * HAL - spinlocks and IRQL manipulation
 * ntoskrnl - DPC and workitem threads, object waiting
 * windrv - driver/device registration
 *
 * The HAL should also be the last thing shut down, since
 * the ntoskrnl subsystem will use spinlocks right up until
 * the DPC and workitem threads are terminated.
 */

static int
ndis_modevent(module_t mod, int cmd, void *arg)
{
	int			error = 0;
	image_patch_table	*patch;

	switch (cmd) {
	case MOD_LOAD:
		/* Initialize subsystems */
		hal_libinit();
		ntoskrnl_libinit();
		windrv_libinit();
		ndis_libinit();
		usbd_libinit();

		patch = kernndis_functbl;
		while (patch->ipt_func != NULL) {
			windrv_wrap((funcptr)patch->ipt_func,
			    (funcptr *)&patch->ipt_wrap,
			    patch->ipt_argcnt, patch->ipt_ftype);
			patch++;
		}

		TAILQ_INIT(&ndis_devhead);
		break;
	case MOD_SHUTDOWN:
		if (TAILQ_FIRST(&ndis_devhead) == NULL) {
			/* Shut down subsystems */
			ndis_libfini();
			usbd_libfini();
			windrv_libfini();
			ntoskrnl_libfini();
			hal_libfini();

			patch = kernndis_functbl;
			while (patch->ipt_func != NULL) {
				windrv_unwrap(patch->ipt_wrap);
				patch++;
			}
		}
		break;
	case MOD_UNLOAD:
		/* Shut down subsystems */
		ndis_libfini();
		usbd_libfini();
		windrv_libfini();
		ntoskrnl_libfini();
		hal_libfini();

		patch = kernndis_functbl;
		while (patch->ipt_func != NULL) {
			windrv_unwrap(patch->ipt_wrap);
			patch++;
		}

		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}
DEV_MODULE(ndisapi, ndis_modevent, NULL);
MODULE_VERSION(ndisapi, 1);

static void
ndis_sendrsrcavail_func(adapter)
	ndis_handle		adapter;
{
}

static void
ndis_status_func(adapter, status, sbuf, slen)
	ndis_handle		adapter;
	ndis_status		status;
	void			*sbuf;
	uint32_t		slen;
{
	ndis_miniport_block	*block;
	struct ndis_softc	*sc;
	struct ifnet		*ifp;

	block = adapter;
	sc = device_get_softc(block->nmb_physdeviceobj->do_devext);
	ifp = NDISUSB_GET_IFNET(sc);
	if ( ifp && ifp->if_flags & IFF_DEBUG)
		device_printf(sc->ndis_dev, "status: %x\n", status);
}

static void
ndis_statusdone_func(adapter)
	ndis_handle		adapter;
{
	ndis_miniport_block	*block;
	struct ndis_softc	*sc;
	struct ifnet		*ifp;

	block = adapter;
	sc = device_get_softc(block->nmb_physdeviceobj->do_devext);
	ifp = NDISUSB_GET_IFNET(sc);
	if (ifp && ifp->if_flags & IFF_DEBUG)
		device_printf(sc->ndis_dev, "status complete\n");
}

static void
ndis_setdone_func(adapter, status)
	ndis_handle		adapter;
	ndis_status		status;
{
	ndis_miniport_block	*block;
	block = adapter;

	block->nmb_setstat = status;
	KeSetEvent(&block->nmb_setevent, IO_NO_INCREMENT, FALSE);
}

static void
ndis_getdone_func(adapter, status)
	ndis_handle		adapter;
	ndis_status		status;
{
	ndis_miniport_block	*block;
	block = adapter;

	block->nmb_getstat = status;
	KeSetEvent(&block->nmb_getevent, IO_NO_INCREMENT, FALSE);
}

static void
ndis_resetdone_func(ndis_handle adapter, ndis_status status,
	uint8_t addressingreset)
{
	ndis_miniport_block	*block;
	struct ndis_softc	*sc;
	struct ifnet		*ifp;

	block = adapter;
	sc = device_get_softc(block->nmb_physdeviceobj->do_devext);
	ifp = NDISUSB_GET_IFNET(sc);

	if (ifp && ifp->if_flags & IFF_DEBUG)
		device_printf(sc->ndis_dev, "reset done...\n");
	KeSetEvent(&block->nmb_resetevent, IO_NO_INCREMENT, FALSE);
}

int
ndis_create_sysctls(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	ndis_cfg		*vals;
	char			buf[256];
	struct sysctl_oid	*oidp;
	struct sysctl_ctx_entry	*e;

	if (arg == NULL)
		return (EINVAL);

	sc = arg;
	/*
	device_printf(sc->ndis_dev, "ndis_create_sysctls() sc=%p\n", sc);
	*/
	vals = sc->ndis_regvals;

	TAILQ_INIT(&sc->ndis_cfglist_head);

	/* Add the driver-specific registry keys. */

	while(1) {
		if (vals->nc_cfgkey == NULL)
			break;

		if (vals->nc_idx != sc->ndis_devidx) {
			vals++;
			continue;
		}

		/* See if we already have a sysctl with this name */

		oidp = NULL;
		TAILQ_FOREACH(e, device_get_sysctl_ctx(sc->ndis_dev), link) {
			oidp = e->entry;
			if (strcasecmp(oidp->oid_name, vals->nc_cfgkey) == 0)
				break;
			oidp = NULL;
		}

		if (oidp != NULL) {
			vals++;
			continue;
		}

		ndis_add_sysctl(sc, vals->nc_cfgkey, vals->nc_cfgdesc,
		    vals->nc_val, CTLFLAG_RW);
		vals++;
	}

	/* Now add a couple of builtin keys. */

	/*
	 * Environment can be either Windows (0) or WindowsNT (1).
	 * We qualify as the latter.
	 */
	ndis_add_sysctl(sc, "Environment",
	    "Windows environment", "1", NDIS_FLAG_RDONLY);

	/* NDIS version should be 5.1. */
	ndis_add_sysctl(sc, "NdisVersion",
	    "NDIS API Version", "0x00050001", NDIS_FLAG_RDONLY);

	/*
	 * Some miniport drivers rely on the existence of the SlotNumber,
	 * NetCfgInstanceId and DriverDesc keys.
	 */
	ndis_add_sysctl(sc, "SlotNumber", "Slot Numer", "01", NDIS_FLAG_RDONLY);
	ndis_add_sysctl(sc, "NetCfgInstanceId", "NetCfgInstanceId",
	    "{12345678-1234-5678-CAFE0-123456789ABC}", NDIS_FLAG_RDONLY);
	ndis_add_sysctl(sc, "DriverDesc", "Driver Description",
	    "NDIS Network Adapter", NDIS_FLAG_RDONLY);

	/* Bus type (PCI, PCMCIA, etc...) */
	sprintf(buf, "%d", (int)sc->ndis_iftype);
	ndis_add_sysctl(sc, "BusType", "Bus Type", buf, NDIS_FLAG_RDONLY);

	if (sc->ndis_res_io != NULL) {
		sprintf(buf, "0x%jx", rman_get_start(sc->ndis_res_io));
		ndis_add_sysctl(sc, "IOBaseAddress",
		    "Base I/O Address", buf, NDIS_FLAG_RDONLY);
	}

	if (sc->ndis_irq != NULL) {
		sprintf(buf, "%ju", rman_get_start(sc->ndis_irq));
		ndis_add_sysctl(sc, "InterruptNumber",
		    "Interrupt Number", buf, NDIS_FLAG_RDONLY);
	}

	return (0);
}

int
ndis_add_sysctl(arg, key, desc, val, flag_rdonly)
	void			*arg;
	char			*key;
	char			*desc;
	char			*val;
	int			flag_rdonly;
{
	struct ndis_softc	*sc;
	struct ndis_cfglist	*cfg;
	char			descstr[256];

	sc = arg;

	cfg = malloc(sizeof(struct ndis_cfglist), M_DEVBUF, M_NOWAIT|M_ZERO);

	if (cfg == NULL) {
		printf("failed for %s\n", key);
		return (ENOMEM);
	}

	cfg->ndis_cfg.nc_cfgkey = strdup(key, M_DEVBUF);
	if (desc == NULL) {
		snprintf(descstr, sizeof(descstr), "%s (dynamic)", key);
		cfg->ndis_cfg.nc_cfgdesc = strdup(descstr, M_DEVBUF);
	} else
		cfg->ndis_cfg.nc_cfgdesc = strdup(desc, M_DEVBUF);
	strcpy(cfg->ndis_cfg.nc_val, val);

	TAILQ_INSERT_TAIL(&sc->ndis_cfglist_head, cfg, link);

	if (flag_rdonly != 0) {
		cfg->ndis_oid =
		    SYSCTL_ADD_STRING(device_get_sysctl_ctx(sc->ndis_dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->ndis_dev)),
		    OID_AUTO, cfg->ndis_cfg.nc_cfgkey, CTLFLAG_RD,
		    cfg->ndis_cfg.nc_val, sizeof(cfg->ndis_cfg.nc_val),
		    cfg->ndis_cfg.nc_cfgdesc);
	} else {
		cfg->ndis_oid =
		    SYSCTL_ADD_STRING(device_get_sysctl_ctx(sc->ndis_dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->ndis_dev)),
		    OID_AUTO, cfg->ndis_cfg.nc_cfgkey, CTLFLAG_RW,
		    cfg->ndis_cfg.nc_val, sizeof(cfg->ndis_cfg.nc_val),
		    cfg->ndis_cfg.nc_cfgdesc);
	}
	return (0);
}

/*
 * Somewhere, somebody decided "hey, let's automatically create
 * a sysctl tree for each device instance as it's created -- it'll
 * make life so much easier!" Lies. Why must they turn the kernel
 * into a house of lies?
 */

int
ndis_flush_sysctls(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	struct ndis_cfglist	*cfg;
	struct sysctl_ctx_list	*clist;

	sc = arg;

	clist = device_get_sysctl_ctx(sc->ndis_dev);

	while (!TAILQ_EMPTY(&sc->ndis_cfglist_head)) {
		cfg = TAILQ_FIRST(&sc->ndis_cfglist_head);
		TAILQ_REMOVE(&sc->ndis_cfglist_head, cfg, link);
		sysctl_ctx_entry_del(clist, cfg->ndis_oid);
		sysctl_remove_oid(cfg->ndis_oid, 1, 0);
		free(cfg->ndis_cfg.nc_cfgkey, M_DEVBUF);
		free(cfg->ndis_cfg.nc_cfgdesc, M_DEVBUF);
		free(cfg, M_DEVBUF);
	}

	return (0);
}

void *
ndis_get_routine_address(functbl, name)
	struct image_patch_table *functbl;
	char			*name;
{
	int			i;

	for (i = 0; functbl[i].ipt_name != NULL; i++)
		if (strcmp(name, functbl[i].ipt_name) == 0)
			return (functbl[i].ipt_wrap);
	return (NULL);
}

static void
ndis_return(dobj, arg)
	device_object		*dobj;
	void			*arg;
{
	ndis_miniport_block	*block;
	ndis_miniport_characteristics	*ch;
	ndis_return_handler	returnfunc;
	ndis_handle		adapter;
	ndis_packet		*p;
	uint8_t			irql;
	list_entry		*l;

	block = arg;
	ch = IoGetDriverObjectExtension(dobj->do_drvobj, (void *)1);

	p = arg;
	adapter = block->nmb_miniportadapterctx;

	if (adapter == NULL)
		return;

	returnfunc = ch->nmc_return_packet_func;

	KeAcquireSpinLock(&block->nmb_returnlock, &irql);
	while (!IsListEmpty(&block->nmb_returnlist)) {
		l = RemoveHeadList((&block->nmb_returnlist));
		p = CONTAINING_RECORD(l, ndis_packet, np_list);
		InitializeListHead((&p->np_list));
		KeReleaseSpinLock(&block->nmb_returnlock, irql);
		MSCALL2(returnfunc, adapter, p);
		KeAcquireSpinLock(&block->nmb_returnlock, &irql);
	}
	KeReleaseSpinLock(&block->nmb_returnlock, irql);
}

static void
ndis_ext_free(struct mbuf *m)
{

	return (ndis_return_packet(m->m_ext.ext_arg1));
}

void
ndis_return_packet(ndis_packet *p)
{
	ndis_miniport_block	*block;

	if (p == NULL)
		return;

	/* Decrement refcount. */
	p->np_refcnt--;

	/* Release packet when refcount hits zero, otherwise return. */
	if (p->np_refcnt)
		return;

	block = ((struct ndis_softc *)p->np_softc)->ndis_block;

	KeAcquireSpinLockAtDpcLevel(&block->nmb_returnlock);
	InitializeListHead((&p->np_list));
	InsertHeadList((&block->nmb_returnlist), (&p->np_list));
	KeReleaseSpinLockFromDpcLevel(&block->nmb_returnlock);

	IoQueueWorkItem(block->nmb_returnitem,
	    (io_workitem_func)kernndis_functbl[7].ipt_wrap,
	    WORKQUEUE_CRITICAL, block);
}

void
ndis_free_bufs(b0)
	ndis_buffer		*b0;
{
	ndis_buffer		*next;

	if (b0 == NULL)
		return;

	while(b0 != NULL) {
		next = b0->mdl_next;
		IoFreeMdl(b0);
		b0 = next;
	}
}

void
ndis_free_packet(p)
	ndis_packet		*p;
{
	if (p == NULL)
		return;

	ndis_free_bufs(p->np_private.npp_head);
	NdisFreePacket(p);
}

int
ndis_convert_res(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	ndis_resource_list	*rl = NULL;
	cm_partial_resource_desc	*prd = NULL;
	ndis_miniport_block	*block;
	device_t		dev;
	struct resource_list	*brl;
	struct resource_list_entry	*brle;
	int			error = 0;

	sc = arg;
	block = sc->ndis_block;
	dev = sc->ndis_dev;

	rl = malloc(sizeof(ndis_resource_list) +
	    (sizeof(cm_partial_resource_desc) * (sc->ndis_rescnt - 1)),
	    M_DEVBUF, M_NOWAIT|M_ZERO);

	if (rl == NULL)
		return (ENOMEM);

	rl->cprl_version = 5;
	rl->cprl_revision = 1;
	rl->cprl_count = sc->ndis_rescnt;
	prd = rl->cprl_partial_descs;

	brl = BUS_GET_RESOURCE_LIST(dev, dev);

	if (brl != NULL) {

		STAILQ_FOREACH(brle, brl, link) {
			switch (brle->type) {
			case SYS_RES_IOPORT:
				prd->cprd_type = CmResourceTypePort;
				prd->cprd_flags = CM_RESOURCE_PORT_IO;
				prd->cprd_sharedisp =
				    CmResourceShareDeviceExclusive;
				prd->u.cprd_port.cprd_start.np_quad =
				    brle->start;
				prd->u.cprd_port.cprd_len = brle->count;
				break;
			case SYS_RES_MEMORY:
				prd->cprd_type = CmResourceTypeMemory;
				prd->cprd_flags =
				    CM_RESOURCE_MEMORY_READ_WRITE;
				prd->cprd_sharedisp =
				    CmResourceShareDeviceExclusive;
				prd->u.cprd_mem.cprd_start.np_quad =
				    brle->start;
				prd->u.cprd_mem.cprd_len = brle->count;
				break;
			case SYS_RES_IRQ:
				prd->cprd_type = CmResourceTypeInterrupt;
				prd->cprd_flags = 0;
				/*
				 * Always mark interrupt resources as
				 * shared, since in our implementation,
				 * they will be.
				 */
				prd->cprd_sharedisp =
				    CmResourceShareShared;
				prd->u.cprd_intr.cprd_level = brle->start;
				prd->u.cprd_intr.cprd_vector = brle->start;
				prd->u.cprd_intr.cprd_affinity = 0;
				break;
			default:
				break;
			}
			prd++;
		}
	}

	block->nmb_rlist = rl;

	return (error);
}

/*
 * Map an NDIS packet to an mbuf list. When an NDIS driver receives a
 * packet, it will hand it to us in the form of an ndis_packet,
 * which we need to convert to an mbuf that is then handed off
 * to the stack. Note: we configure the mbuf list so that it uses
 * the memory regions specified by the ndis_buffer structures in
 * the ndis_packet as external storage. In most cases, this will
 * point to a memory region allocated by the driver (either by
 * ndis_malloc_withtag() or ndis_alloc_sharedmem()). We expect
 * the driver to handle free()ing this region for is, so we set up
 * a dummy no-op free handler for it.
 */ 

int
ndis_ptom(m0, p)
	struct mbuf		**m0;
	ndis_packet		*p;
{
	struct mbuf		*m = NULL, *prev = NULL;
	ndis_buffer		*buf;
	ndis_packet_private	*priv;
	uint32_t		totlen = 0;
	struct ifnet		*ifp;
	struct ether_header	*eh;
	int			diff;

	if (p == NULL || m0 == NULL)
		return (EINVAL);

	priv = &p->np_private;
	buf = priv->npp_head;
	p->np_refcnt = 0;

	for (buf = priv->npp_head; buf != NULL; buf = buf->mdl_next) {
		if (buf == priv->npp_head)
			m = m_gethdr(M_NOWAIT, MT_DATA);
		else
			m = m_get(M_NOWAIT, MT_DATA);
		if (m == NULL) {
			m_freem(*m0);
			*m0 = NULL;
			return (ENOBUFS);
		}
		m->m_len = MmGetMdlByteCount(buf);
		m_extadd(m, MmGetMdlVirtualAddress(buf), m->m_len,
		    ndis_ext_free, p, NULL, 0, EXT_NDIS);
		p->np_refcnt++;

		totlen += m->m_len;
		if (m->m_flags & M_PKTHDR)
			*m0 = m;
		else
			prev->m_next = m;
		prev = m;
	}

	/*
	 * This is a hack to deal with the Marvell 8335 driver
	 * which, when associated with an AP in WPA-PSK mode,
	 * seems to overpad its frames by 8 bytes. I don't know
	 * that the extra 8 bytes are for, and they're not there
	 * in open mode, so for now clamp the frame size at 1514
	 * until I can figure out how to deal with this properly,
	 * otherwise if_ethersubr() will spank us by discarding
	 * the 'oversize' frames.
	 */

	eh = mtod((*m0), struct ether_header *);
	ifp = NDISUSB_GET_IFNET((struct ndis_softc *)p->np_softc);
	if (ifp && totlen > ETHER_MAX_FRAME(ifp, eh->ether_type, FALSE)) {
		diff = totlen - ETHER_MAX_FRAME(ifp, eh->ether_type, FALSE);
		totlen -= diff;
		m->m_len -= diff;
	}
	(*m0)->m_pkthdr.len = totlen;

	return (0);
}

/*
 * Create an NDIS packet from an mbuf chain.
 * This is used mainly when transmitting packets, where we need
 * to turn an mbuf off an interface's send queue and transform it
 * into an NDIS packet which will be fed into the NDIS driver's
 * send routine.
 *
 * NDIS packets consist of two parts: an ndis_packet structure,
 * which is vaguely analogous to the pkthdr portion of an mbuf,
 * and one or more ndis_buffer structures, which define the
 * actual memory segments in which the packet data resides.
 * We need to allocate one ndis_buffer for each mbuf in a chain,
 * plus one ndis_packet as the header.
 */

int
ndis_mtop(m0, p)
	struct mbuf		*m0;
	ndis_packet		**p;
{
	struct mbuf		*m;
	ndis_buffer		*buf = NULL, *prev = NULL;
	ndis_packet_private	*priv;

	if (p == NULL || *p == NULL || m0 == NULL)
		return (EINVAL);

	priv = &(*p)->np_private;
	priv->npp_totlen = m0->m_pkthdr.len;

	for (m = m0; m != NULL; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		buf = IoAllocateMdl(m->m_data, m->m_len, FALSE, FALSE, NULL);
		if (buf == NULL) {
			ndis_free_packet(*p);
			*p = NULL;
			return (ENOMEM);
		}
		MmBuildMdlForNonPagedPool(buf);

		if (priv->npp_head == NULL)
			priv->npp_head = buf;
		else
			prev->mdl_next = buf;
		prev = buf;
	}

	priv->npp_tail = buf;

	return (0);
}

int
ndis_get_supported_oids(arg, oids, oidcnt)
	void			*arg;
	ndis_oid		**oids;
	int			*oidcnt;
{
	int			len, rval;
	ndis_oid		*o;

	if (arg == NULL || oids == NULL || oidcnt == NULL)
		return (EINVAL);
	len = 0;
	ndis_get_info(arg, OID_GEN_SUPPORTED_LIST, NULL, &len);

	o = malloc(len, M_DEVBUF, M_NOWAIT);
	if (o == NULL)
		return (ENOMEM);

	rval = ndis_get_info(arg, OID_GEN_SUPPORTED_LIST, o, &len);

	if (rval) {
		free(o, M_DEVBUF);
		return (rval);
	}

	*oids = o;
	*oidcnt = len / 4;

	return (0);
}

int
ndis_set_info(arg, oid, buf, buflen)
	void			*arg;
	ndis_oid		oid;
	void			*buf;
	int			*buflen;
{
	struct ndis_softc	*sc;
	ndis_status		rval;
	ndis_handle		adapter;
	ndis_setinfo_handler	setfunc;
	uint32_t		byteswritten = 0, bytesneeded = 0;
	uint8_t			irql;
	uint64_t		duetime;

	/*
	 * According to the NDIS spec, MiniportQueryInformation()
	 * and MiniportSetInformation() requests are handled serially:
	 * once one request has been issued, we must wait for it to
 	 * finish before allowing another request to proceed.
	 */

	sc = arg;

	KeResetEvent(&sc->ndis_block->nmb_setevent);

	KeAcquireSpinLock(&sc->ndis_block->nmb_lock, &irql);

	if (sc->ndis_block->nmb_pendingreq != NULL) {
		KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);
		panic("ndis_set_info() called while other request pending");
	} else
		sc->ndis_block->nmb_pendingreq = (ndis_request *)sc;

	setfunc = sc->ndis_chars->nmc_setinfo_func;
	adapter = sc->ndis_block->nmb_miniportadapterctx;

	if (adapter == NULL || setfunc == NULL ||
	    sc->ndis_block->nmb_devicectx == NULL) {
		sc->ndis_block->nmb_pendingreq = NULL;
		KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);
		return (ENXIO);
	}

	rval = MSCALL6(setfunc, adapter, oid, buf, *buflen,
	    &byteswritten, &bytesneeded);

	sc->ndis_block->nmb_pendingreq = NULL;

	KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);

	if (rval == NDIS_STATUS_PENDING) {
		/* Wait up to 5 seconds. */
		duetime = (5 * 1000000) * -10;
		KeWaitForSingleObject(&sc->ndis_block->nmb_setevent,
		    0, 0, FALSE, &duetime);
		rval = sc->ndis_block->nmb_setstat;
	}

	if (byteswritten)
		*buflen = byteswritten;
	if (bytesneeded)
		*buflen = bytesneeded;

	if (rval == NDIS_STATUS_INVALID_LENGTH)
		return (ENOSPC);

	if (rval == NDIS_STATUS_INVALID_OID)
		return (EINVAL);

	if (rval == NDIS_STATUS_NOT_SUPPORTED ||
	    rval == NDIS_STATUS_NOT_ACCEPTED)
		return (ENOTSUP);

	if (rval != NDIS_STATUS_SUCCESS)
		return (ENODEV);

	return (0);
}

typedef void (*ndis_senddone_func)(ndis_handle, ndis_packet *, ndis_status);

int
ndis_send_packets(arg, packets, cnt)
	void			*arg;
	ndis_packet		**packets;
	int			cnt;
{
	struct ndis_softc	*sc;
	ndis_handle		adapter;
	ndis_sendmulti_handler	sendfunc;
	ndis_senddone_func		senddonefunc;
	int			i;
	ndis_packet		*p;
	uint8_t			irql = 0;

	sc = arg;
	adapter = sc->ndis_block->nmb_miniportadapterctx;
	if (adapter == NULL)
		return (ENXIO);
	sendfunc = sc->ndis_chars->nmc_sendmulti_func;
	senddonefunc = sc->ndis_block->nmb_senddone_func;

	if (NDIS_SERIALIZED(sc->ndis_block))
		KeAcquireSpinLock(&sc->ndis_block->nmb_lock, &irql);

	MSCALL3(sendfunc, adapter, packets, cnt);

	for (i = 0; i < cnt; i++) {
		p = packets[i];
		/*
		 * Either the driver already handed the packet to
		 * ndis_txeof() due to a failure, or it wants to keep
		 * it and release it asynchronously later. Skip to the
		 * next one.
		 */
		if (p == NULL || p->np_oob.npo_status == NDIS_STATUS_PENDING)
			continue;
		MSCALL3(senddonefunc, sc->ndis_block, p, p->np_oob.npo_status);
	}

	if (NDIS_SERIALIZED(sc->ndis_block))
		KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);

	return (0);
}

int
ndis_send_packet(arg, packet)
	void			*arg;
	ndis_packet		*packet;
{
	struct ndis_softc	*sc;
	ndis_handle		adapter;
	ndis_status		status;
	ndis_sendsingle_handler	sendfunc;
	ndis_senddone_func		senddonefunc;
	uint8_t			irql = 0;

	sc = arg;
	adapter = sc->ndis_block->nmb_miniportadapterctx;
	if (adapter == NULL)
		return (ENXIO);
	sendfunc = sc->ndis_chars->nmc_sendsingle_func;
	senddonefunc = sc->ndis_block->nmb_senddone_func;

	if (NDIS_SERIALIZED(sc->ndis_block))
		KeAcquireSpinLock(&sc->ndis_block->nmb_lock, &irql);
	status = MSCALL3(sendfunc, adapter, packet,
	    packet->np_private.npp_flags);

	if (status == NDIS_STATUS_PENDING) {
		if (NDIS_SERIALIZED(sc->ndis_block))
			KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);
		return (0);
	}

	MSCALL3(senddonefunc, sc->ndis_block, packet, status);

	if (NDIS_SERIALIZED(sc->ndis_block))
		KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);

	return (0);
}

int
ndis_init_dma(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	int			i, error;

	sc = arg;

	sc->ndis_tmaps = malloc(sizeof(bus_dmamap_t) * sc->ndis_maxpkts,
	    M_DEVBUF, M_NOWAIT|M_ZERO);

	if (sc->ndis_tmaps == NULL)
		return (ENOMEM);

	for (i = 0; i < sc->ndis_maxpkts; i++) {
		error = bus_dmamap_create(sc->ndis_ttag, 0,
		    &sc->ndis_tmaps[i]);
		if (error) {
			free(sc->ndis_tmaps, M_DEVBUF);
			return (ENODEV);
		}
	}

	return (0);
}

int
ndis_destroy_dma(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	struct mbuf		*m;
	ndis_packet		*p = NULL;
	int			i;

	sc = arg;

	for (i = 0; i < sc->ndis_maxpkts; i++) {
		if (sc->ndis_txarray[i] != NULL) {
			p = sc->ndis_txarray[i];
			m = (struct mbuf *)p->np_rsvd[1];
			if (m != NULL)
				m_freem(m);
			ndis_free_packet(sc->ndis_txarray[i]);
		}
		bus_dmamap_destroy(sc->ndis_ttag, sc->ndis_tmaps[i]);
	}

	free(sc->ndis_tmaps, M_DEVBUF);

	bus_dma_tag_destroy(sc->ndis_ttag);

	return (0);
}

int
ndis_reset_nic(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	ndis_handle		adapter;
	ndis_reset_handler	resetfunc;
	uint8_t			addressing_reset;
	int			rval;
	uint8_t			irql = 0;

	sc = arg;

	NDIS_LOCK(sc);
	adapter = sc->ndis_block->nmb_miniportadapterctx;
	resetfunc = sc->ndis_chars->nmc_reset_func;

	if (adapter == NULL || resetfunc == NULL ||
	    sc->ndis_block->nmb_devicectx == NULL) {
		NDIS_UNLOCK(sc);
		return (EIO);
	}

	NDIS_UNLOCK(sc);

	KeResetEvent(&sc->ndis_block->nmb_resetevent);

	if (NDIS_SERIALIZED(sc->ndis_block))
		KeAcquireSpinLock(&sc->ndis_block->nmb_lock, &irql);

	rval = MSCALL2(resetfunc, &addressing_reset, adapter);

	if (NDIS_SERIALIZED(sc->ndis_block))
		KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);

	if (rval == NDIS_STATUS_PENDING)
		KeWaitForSingleObject(&sc->ndis_block->nmb_resetevent,
		    0, 0, FALSE, NULL);

	return (0);
}

int
ndis_halt_nic(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	ndis_handle		adapter;
	ndis_halt_handler	haltfunc;
	ndis_miniport_block	*block;
	int			empty = 0;
	uint8_t			irql;

	sc = arg;
	block = sc->ndis_block;

	if (!cold)
		KeFlushQueuedDpcs();

	/*
	 * Wait for all packets to be returned.
	 */

	while (1) {
		KeAcquireSpinLock(&block->nmb_returnlock, &irql);
		empty = IsListEmpty(&block->nmb_returnlist);
		KeReleaseSpinLock(&block->nmb_returnlock, irql);
		if (empty)
			break;
		NdisMSleep(1000);
	}

	NDIS_LOCK(sc);
	adapter = sc->ndis_block->nmb_miniportadapterctx;
	if (adapter == NULL) {
		NDIS_UNLOCK(sc);
		return (EIO);
	}

	sc->ndis_block->nmb_devicectx = NULL;

	/*
	 * The adapter context is only valid after the init
	 * handler has been called, and is invalid once the
	 * halt handler has been called.
	 */

	haltfunc = sc->ndis_chars->nmc_halt_func;
	NDIS_UNLOCK(sc);

	MSCALL1(haltfunc, adapter);

	NDIS_LOCK(sc);
	sc->ndis_block->nmb_miniportadapterctx = NULL;
	NDIS_UNLOCK(sc);

	return (0);
}

int
ndis_shutdown_nic(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	ndis_handle		adapter;
	ndis_shutdown_handler	shutdownfunc;

	sc = arg;
	NDIS_LOCK(sc);
	adapter = sc->ndis_block->nmb_miniportadapterctx;
	shutdownfunc = sc->ndis_chars->nmc_shutdown_handler;
	NDIS_UNLOCK(sc);
	if (adapter == NULL || shutdownfunc == NULL)
		return (EIO);

	if (sc->ndis_chars->nmc_rsvd0 == NULL)
		MSCALL1(shutdownfunc, adapter);
	else
		MSCALL1(shutdownfunc, sc->ndis_chars->nmc_rsvd0);

	TAILQ_REMOVE(&ndis_devhead, sc->ndis_block, link);

	return (0);
}

int
ndis_pnpevent_nic(arg, type)
	void			*arg;
	int			type;
{
	device_t		dev;
	struct ndis_softc	*sc;
	ndis_handle		adapter;
	ndis_pnpevent_handler	pnpeventfunc;

	dev = arg;
	sc = device_get_softc(arg);
	NDIS_LOCK(sc);
	adapter = sc->ndis_block->nmb_miniportadapterctx;
	pnpeventfunc = sc->ndis_chars->nmc_pnpevent_handler;
	NDIS_UNLOCK(sc);
	if (adapter == NULL || pnpeventfunc == NULL)
		return (EIO);

	if (sc->ndis_chars->nmc_rsvd0 == NULL)
		MSCALL4(pnpeventfunc, adapter, type, NULL, 0);
	else
		MSCALL4(pnpeventfunc, sc->ndis_chars->nmc_rsvd0, type, NULL, 0);

	return (0);
}

int
ndis_init_nic(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	ndis_miniport_block	*block;
	ndis_init_handler	initfunc;
	ndis_status		status, openstatus = 0;
	ndis_medium		mediumarray[NdisMediumMax];
	uint32_t		chosenmedium, i;

	if (arg == NULL)
		return (EINVAL);

	sc = arg;
	NDIS_LOCK(sc);
	block = sc->ndis_block;
	initfunc = sc->ndis_chars->nmc_init_func;
	NDIS_UNLOCK(sc);

	sc->ndis_block->nmb_timerlist = NULL;

	for (i = 0; i < NdisMediumMax; i++)
		mediumarray[i] = i;

	status = MSCALL6(initfunc, &openstatus, &chosenmedium,
	    mediumarray, NdisMediumMax, block, block);

	/*
	 * If the init fails, blow away the other exported routines
	 * we obtained from the driver so we can't call them later.
	 * If the init failed, none of these will work.
	 */
	if (status != NDIS_STATUS_SUCCESS) {
		NDIS_LOCK(sc);
		sc->ndis_block->nmb_miniportadapterctx = NULL;
		NDIS_UNLOCK(sc);
		return (ENXIO);
	}

	/*
	 * This may look really goofy, but apparently it is possible
	 * to halt a miniport too soon after it's been initialized.
	 * After MiniportInitialize() finishes, pause for 1 second
	 * to give the chip a chance to handle any short-lived timers
	 * that were set in motion. If we call MiniportHalt() too soon,
	 * some of the timers may not be cancelled, because the driver
	 * expects them to fire before the halt is called.
	 */

	pause("ndwait", hz);

	NDIS_LOCK(sc);
	sc->ndis_block->nmb_devicectx = sc;
	NDIS_UNLOCK(sc);

	return (0);
}

static void
ndis_intrsetup(dpc, dobj, ip, sc)
	kdpc			*dpc;
	device_object		*dobj;
	irp			*ip;
	struct ndis_softc	*sc;
{
	ndis_miniport_interrupt	*intr;

	intr = sc->ndis_block->nmb_interrupt;

	/* Sanity check. */

	if (intr == NULL)
		return;

	KeAcquireSpinLockAtDpcLevel(&intr->ni_dpccountlock);
	KeResetEvent(&intr->ni_dpcevt);
	if (KeInsertQueueDpc(&intr->ni_dpc, NULL, NULL) == TRUE)
		intr->ni_dpccnt++;
	KeReleaseSpinLockFromDpcLevel(&intr->ni_dpccountlock);
}

int
ndis_get_info(arg, oid, buf, buflen)
	void			*arg;
	ndis_oid		oid;
	void			*buf;
	int			*buflen;
{
	struct ndis_softc	*sc;
	ndis_status		rval;
	ndis_handle		adapter;
	ndis_queryinfo_handler	queryfunc;
	uint32_t		byteswritten = 0, bytesneeded = 0;
	uint8_t			irql;
	uint64_t		duetime;

	sc = arg;

	KeResetEvent(&sc->ndis_block->nmb_getevent);

	KeAcquireSpinLock(&sc->ndis_block->nmb_lock, &irql);

	if (sc->ndis_block->nmb_pendingreq != NULL) {
		KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);
		panic("ndis_get_info() called while other request pending");
	} else
		sc->ndis_block->nmb_pendingreq = (ndis_request *)sc;

	queryfunc = sc->ndis_chars->nmc_queryinfo_func;
	adapter = sc->ndis_block->nmb_miniportadapterctx;

	if (adapter == NULL || queryfunc == NULL ||
	    sc->ndis_block->nmb_devicectx == NULL) {
		sc->ndis_block->nmb_pendingreq = NULL;
		KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);
		return (ENXIO);
	}

	rval = MSCALL6(queryfunc, adapter, oid, buf, *buflen,
	    &byteswritten, &bytesneeded);

	sc->ndis_block->nmb_pendingreq = NULL;

	KeReleaseSpinLock(&sc->ndis_block->nmb_lock, irql);

	/* Wait for requests that block. */

	if (rval == NDIS_STATUS_PENDING) {
		/* Wait up to 5 seconds. */
		duetime = (5 * 1000000) * -10;
		KeWaitForSingleObject(&sc->ndis_block->nmb_getevent,
		    0, 0, FALSE, &duetime);
		rval = sc->ndis_block->nmb_getstat;
	}

	if (byteswritten)
		*buflen = byteswritten;
	if (bytesneeded)
		*buflen = bytesneeded;

	if (rval == NDIS_STATUS_INVALID_LENGTH ||
	    rval == NDIS_STATUS_BUFFER_TOO_SHORT)
		return (ENOSPC);

	if (rval == NDIS_STATUS_INVALID_OID)
		return (EINVAL);

	if (rval == NDIS_STATUS_NOT_SUPPORTED ||
	    rval == NDIS_STATUS_NOT_ACCEPTED)
		return (ENOTSUP);

	if (rval != NDIS_STATUS_SUCCESS)
		return (ENODEV);

	return (0);
}

uint32_t
NdisAddDevice(drv, pdo)
	driver_object		*drv;
	device_object		*pdo;
{
	device_object		*fdo;
	ndis_miniport_block	*block;
	struct ndis_softc	*sc;
	uint32_t		status;
	int			error;

	sc = device_get_softc(pdo->do_devext);

	if (sc->ndis_iftype == PCMCIABus || sc->ndis_iftype == PCIBus) {
		error = bus_setup_intr(sc->ndis_dev, sc->ndis_irq,
		    INTR_TYPE_NET | INTR_MPSAFE,
		    NULL, ntoskrnl_intr, NULL, &sc->ndis_intrhand);
		if (error)
			return (NDIS_STATUS_FAILURE);
	}

	status = IoCreateDevice(drv, sizeof(ndis_miniport_block), NULL,
	    FILE_DEVICE_UNKNOWN, 0, FALSE, &fdo);

	if (status != STATUS_SUCCESS)
		return (status);

	block = fdo->do_devext;

	block->nmb_filterdbs.nf_ethdb = block;
	block->nmb_deviceobj = fdo;
	block->nmb_physdeviceobj = pdo;
	block->nmb_nextdeviceobj = IoAttachDeviceToDeviceStack(fdo, pdo);
	KeInitializeSpinLock(&block->nmb_lock);
	KeInitializeSpinLock(&block->nmb_returnlock);
	KeInitializeEvent(&block->nmb_getevent, EVENT_TYPE_NOTIFY, TRUE);
	KeInitializeEvent(&block->nmb_setevent, EVENT_TYPE_NOTIFY, TRUE);
	KeInitializeEvent(&block->nmb_resetevent, EVENT_TYPE_NOTIFY, TRUE);
	InitializeListHead(&block->nmb_parmlist);
	InitializeListHead(&block->nmb_returnlist);
	block->nmb_returnitem = IoAllocateWorkItem(fdo);

	/*
	 * Stash pointers to the miniport block and miniport
	 * characteristics info in the if_ndis softc so the
	 * UNIX wrapper driver can get to them later.
	 */
	sc->ndis_block = block;
	sc->ndis_chars = IoGetDriverObjectExtension(drv, (void *)1);

	/*
	 * If the driver has a MiniportTransferData() function,
	 * we should allocate a private RX packet pool.
	 */

	if (sc->ndis_chars->nmc_transferdata_func != NULL) {
		NdisAllocatePacketPool(&status, &block->nmb_rxpool,
		    32, PROTOCOL_RESERVED_SIZE_IN_PACKET);
		if (status != NDIS_STATUS_SUCCESS) {
			IoDetachDevice(block->nmb_nextdeviceobj);
			IoDeleteDevice(fdo);
			return (status);
		}
		InitializeListHead((&block->nmb_packetlist));
	}

	/* Give interrupt handling priority over timers. */
	IoInitializeDpcRequest(fdo, kernndis_functbl[6].ipt_wrap);
	KeSetImportanceDpc(&fdo->do_dpc, KDPC_IMPORTANCE_HIGH);

	/* Finish up BSD-specific setup. */

	block->nmb_signature = (void *)0xcafebabe;
	block->nmb_status_func = kernndis_functbl[0].ipt_wrap;
	block->nmb_statusdone_func = kernndis_functbl[1].ipt_wrap;
	block->nmb_setdone_func = kernndis_functbl[2].ipt_wrap;
	block->nmb_querydone_func = kernndis_functbl[3].ipt_wrap;
	block->nmb_resetdone_func = kernndis_functbl[4].ipt_wrap;
	block->nmb_sendrsrc_func = kernndis_functbl[5].ipt_wrap;
	block->nmb_pendingreq = NULL;

	TAILQ_INSERT_TAIL(&ndis_devhead, block, link);

	return (STATUS_SUCCESS);
}

int
ndis_unload_driver(arg)
	void			*arg;
{
	struct ndis_softc	*sc;
	device_object		*fdo;

	sc = arg;

	if (sc->ndis_intrhand)
		bus_teardown_intr(sc->ndis_dev,
		    sc->ndis_irq, sc->ndis_intrhand);

	if (sc->ndis_block->nmb_rlist != NULL)
		free(sc->ndis_block->nmb_rlist, M_DEVBUF);

	ndis_flush_sysctls(sc);

	TAILQ_REMOVE(&ndis_devhead, sc->ndis_block, link);

	if (sc->ndis_chars->nmc_transferdata_func != NULL)
		NdisFreePacketPool(sc->ndis_block->nmb_rxpool);
	fdo = sc->ndis_block->nmb_deviceobj;
	IoFreeWorkItem(sc->ndis_block->nmb_returnitem);
	IoDetachDevice(sc->ndis_block->nmb_nextdeviceobj);
	IoDeleteDevice(fdo);

	return (0);
}
