/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2001 Scott Long
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001 Adaptec, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the Adaptec 'FSA' family of PCI/SCSI RAID adapters.
 */
#define AAC_DRIVERNAME			"aac"

#include "opt_aac.h"

/* #include <stddef.h> */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/poll.h>
#include <sys/ioccom.h>

#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/signalvar.h>
#include <sys/time.h>
#include <sys/eventhandler.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/aac/aacreg.h>
#include <sys/aac_ioctl.h>
#include <dev/aac/aacvar.h>
#include <dev/aac/aac_tables.h>

static void	aac_startup(void *arg);
static void	aac_add_container(struct aac_softc *sc,
				  struct aac_mntinforesp *mir, int f);
static void	aac_get_bus_info(struct aac_softc *sc);
static void	aac_daemon(void *arg);

/* Command Processing */
static void	aac_timeout(struct aac_softc *sc);
static void	aac_complete(void *context, int pending);
static int	aac_bio_command(struct aac_softc *sc, struct aac_command **cmp);
static void	aac_bio_complete(struct aac_command *cm);
static int	aac_wait_command(struct aac_command *cm);
static void	aac_command_thread(struct aac_softc *sc);

/* Command Buffer Management */
static void	aac_map_command_sg(void *arg, bus_dma_segment_t *segs,
				   int nseg, int error);
static void	aac_map_command_helper(void *arg, bus_dma_segment_t *segs,
				       int nseg, int error);
static int	aac_alloc_commands(struct aac_softc *sc);
static void	aac_free_commands(struct aac_softc *sc);
static void	aac_unmap_command(struct aac_command *cm);

/* Hardware Interface */
static int	aac_alloc(struct aac_softc *sc);
static void	aac_common_map(void *arg, bus_dma_segment_t *segs, int nseg,
			       int error);
static int	aac_check_firmware(struct aac_softc *sc);
static int	aac_init(struct aac_softc *sc);
static int	aac_sync_command(struct aac_softc *sc, u_int32_t command,
				 u_int32_t arg0, u_int32_t arg1, u_int32_t arg2,
				 u_int32_t arg3, u_int32_t *sp);
static int	aac_setup_intr(struct aac_softc *sc);
static int	aac_enqueue_fib(struct aac_softc *sc, int queue,
				struct aac_command *cm);
static int	aac_dequeue_fib(struct aac_softc *sc, int queue,
				u_int32_t *fib_size, struct aac_fib **fib_addr);
static int	aac_enqueue_response(struct aac_softc *sc, int queue,
				     struct aac_fib *fib);

/* StrongARM interface */
static int	aac_sa_get_fwstatus(struct aac_softc *sc);
static void	aac_sa_qnotify(struct aac_softc *sc, int qbit);
static int	aac_sa_get_istatus(struct aac_softc *sc);
static void	aac_sa_clear_istatus(struct aac_softc *sc, int mask);
static void	aac_sa_set_mailbox(struct aac_softc *sc, u_int32_t command,
				   u_int32_t arg0, u_int32_t arg1,
				   u_int32_t arg2, u_int32_t arg3);
static int	aac_sa_get_mailbox(struct aac_softc *sc, int mb);
static void	aac_sa_set_interrupts(struct aac_softc *sc, int enable);

const struct aac_interface aac_sa_interface = {
	aac_sa_get_fwstatus,
	aac_sa_qnotify,
	aac_sa_get_istatus,
	aac_sa_clear_istatus,
	aac_sa_set_mailbox,
	aac_sa_get_mailbox,
	aac_sa_set_interrupts,
	NULL, NULL, NULL
};

/* i960Rx interface */
static int	aac_rx_get_fwstatus(struct aac_softc *sc);
static void	aac_rx_qnotify(struct aac_softc *sc, int qbit);
static int	aac_rx_get_istatus(struct aac_softc *sc);
static void	aac_rx_clear_istatus(struct aac_softc *sc, int mask);
static void	aac_rx_set_mailbox(struct aac_softc *sc, u_int32_t command,
				   u_int32_t arg0, u_int32_t arg1,
				   u_int32_t arg2, u_int32_t arg3);
static int	aac_rx_get_mailbox(struct aac_softc *sc, int mb);
static void	aac_rx_set_interrupts(struct aac_softc *sc, int enable);
static int aac_rx_send_command(struct aac_softc *sc, struct aac_command *cm);
static int aac_rx_get_outb_queue(struct aac_softc *sc);
static void aac_rx_set_outb_queue(struct aac_softc *sc, int index);

const struct aac_interface aac_rx_interface = {
	aac_rx_get_fwstatus,
	aac_rx_qnotify,
	aac_rx_get_istatus,
	aac_rx_clear_istatus,
	aac_rx_set_mailbox,
	aac_rx_get_mailbox,
	aac_rx_set_interrupts,
	aac_rx_send_command,
	aac_rx_get_outb_queue,
	aac_rx_set_outb_queue
};

/* Rocket/MIPS interface */
static int	aac_rkt_get_fwstatus(struct aac_softc *sc);
static void	aac_rkt_qnotify(struct aac_softc *sc, int qbit);
static int	aac_rkt_get_istatus(struct aac_softc *sc);
static void	aac_rkt_clear_istatus(struct aac_softc *sc, int mask);
static void	aac_rkt_set_mailbox(struct aac_softc *sc, u_int32_t command,
				    u_int32_t arg0, u_int32_t arg1,
				    u_int32_t arg2, u_int32_t arg3);
static int	aac_rkt_get_mailbox(struct aac_softc *sc, int mb);
static void	aac_rkt_set_interrupts(struct aac_softc *sc, int enable);
static int aac_rkt_send_command(struct aac_softc *sc, struct aac_command *cm);
static int aac_rkt_get_outb_queue(struct aac_softc *sc);
static void aac_rkt_set_outb_queue(struct aac_softc *sc, int index);

const struct aac_interface aac_rkt_interface = {
	aac_rkt_get_fwstatus,
	aac_rkt_qnotify,
	aac_rkt_get_istatus,
	aac_rkt_clear_istatus,
	aac_rkt_set_mailbox,
	aac_rkt_get_mailbox,
	aac_rkt_set_interrupts,
	aac_rkt_send_command,
	aac_rkt_get_outb_queue,
	aac_rkt_set_outb_queue
};

/* Debugging and Diagnostics */
static void		aac_describe_controller(struct aac_softc *sc);
static const char	*aac_describe_code(const struct aac_code_lookup *table,
				   u_int32_t code);

/* Management Interface */
static d_open_t		aac_open;
static d_ioctl_t	aac_ioctl;
static d_poll_t		aac_poll;
static void		aac_cdevpriv_dtor(void *arg);
static int		aac_ioctl_sendfib(struct aac_softc *sc, caddr_t ufib);
static int		aac_ioctl_send_raw_srb(struct aac_softc *sc, caddr_t arg);
static void		aac_handle_aif(struct aac_softc *sc,
					   struct aac_fib *fib);
static int		aac_rev_check(struct aac_softc *sc, caddr_t udata);
static int		aac_open_aif(struct aac_softc *sc, caddr_t arg);
static int		aac_close_aif(struct aac_softc *sc, caddr_t arg);
static int		aac_getnext_aif(struct aac_softc *sc, caddr_t arg);
static int		aac_return_aif(struct aac_softc *sc,
					struct aac_fib_context *ctx, caddr_t uptr);
static int		aac_query_disk(struct aac_softc *sc, caddr_t uptr);
static int		aac_get_pci_info(struct aac_softc *sc, caddr_t uptr);
static int		aac_supported_features(struct aac_softc *sc, caddr_t uptr);
static void		aac_ioctl_event(struct aac_softc *sc,
					struct aac_event *event, void *arg);
static struct aac_mntinforesp *
	aac_get_container_info(struct aac_softc *sc, struct aac_fib *fib, int cid);

static struct cdevsw aac_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	aac_open,
	.d_ioctl =	aac_ioctl,
	.d_poll =	aac_poll,
	.d_name =	"aac",
};

static MALLOC_DEFINE(M_AACBUF, "aacbuf", "Buffers for the AAC driver");

/* sysctl node */
SYSCTL_NODE(_hw, OID_AUTO, aac, CTLFLAG_RD, 0, "AAC driver parameters");

/*
 * Device Interface
 */

/*
 * Initialize the controller and softc
 */
int
aac_attach(struct aac_softc *sc)
{
	int error, unit;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/*
	 * Initialize per-controller queues.
	 */
	aac_initq_free(sc);
	aac_initq_ready(sc);
	aac_initq_busy(sc);
	aac_initq_bio(sc);

	/*
	 * Initialize command-completion task.
	 */
	TASK_INIT(&sc->aac_task_complete, 0, aac_complete, sc);

	/* mark controller as suspended until we get ourselves organised */
	sc->aac_state |= AAC_STATE_SUSPEND;

	/*
	 * Check that the firmware on the card is supported.
	 */
	if ((error = aac_check_firmware(sc)) != 0)
		return(error);

	/*
	 * Initialize locks
	 */
	mtx_init(&sc->aac_aifq_lock, "AAC AIF lock", NULL, MTX_DEF);
	mtx_init(&sc->aac_io_lock, "AAC I/O lock", NULL, MTX_DEF);
	mtx_init(&sc->aac_container_lock, "AAC container lock", NULL, MTX_DEF);
	TAILQ_INIT(&sc->aac_container_tqh);
	TAILQ_INIT(&sc->aac_ev_cmfree);

	/* Initialize the clock daemon callout. */
	callout_init_mtx(&sc->aac_daemontime, &sc->aac_io_lock, 0);

	/*
	 * Initialize the adapter.
	 */
	if ((error = aac_alloc(sc)) != 0)
		return(error);
	if ((error = aac_init(sc)) != 0)
		return(error);

	/*
	 * Allocate and connect our interrupt.
	 */
	if ((error = aac_setup_intr(sc)) != 0)
		return(error);

	/*
	 * Print a little information about the controller.
	 */
	aac_describe_controller(sc);

	/*
	 * Add sysctls.
	 */
	SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->aac_dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->aac_dev)),
	    OID_AUTO, "firmware_build", CTLFLAG_RD,
	    &sc->aac_revision.buildNumber, 0,
	    "firmware build number");

	/*
	 * Register to probe our containers later.
	 */
	sc->aac_ich.ich_func = aac_startup;
	sc->aac_ich.ich_arg = sc;
	if (config_intrhook_establish(&sc->aac_ich) != 0) {
		device_printf(sc->aac_dev,
			      "can't establish configuration hook\n");
		return(ENXIO);
	}

	/*
	 * Make the control device.
	 */
	unit = device_get_unit(sc->aac_dev);
	sc->aac_dev_t = make_dev(&aac_cdevsw, unit, UID_ROOT, GID_OPERATOR,
				 0640, "aac%d", unit);
	(void)make_dev_alias(sc->aac_dev_t, "afa%d", unit);
	(void)make_dev_alias(sc->aac_dev_t, "hpn%d", unit);
	sc->aac_dev_t->si_drv1 = sc;

	/* Create the AIF thread */
	if (kproc_create((void(*)(void *))aac_command_thread, sc,
		   &sc->aifthread, 0, 0, "aac%daif", unit))
		panic("Could not create AIF thread");

	/* Register the shutdown method to only be called post-dump */
	if ((sc->eh = EVENTHANDLER_REGISTER(shutdown_final, aac_shutdown,
	    sc->aac_dev, SHUTDOWN_PRI_DEFAULT)) == NULL)
		device_printf(sc->aac_dev,
			      "shutdown event registration failed\n");

	/* Register with CAM for the non-DASD devices */
	if ((sc->flags & AAC_FLAGS_ENABLE_CAM) != 0) {
		TAILQ_INIT(&sc->aac_sim_tqh);
		aac_get_bus_info(sc);
	}

	mtx_lock(&sc->aac_io_lock);
	callout_reset(&sc->aac_daemontime, 60 * hz, aac_daemon, sc);
	mtx_unlock(&sc->aac_io_lock);

	return(0);
}

static void
aac_daemon(void *arg)
{
	struct timeval tv;
	struct aac_softc *sc;
	struct aac_fib *fib;

	sc = arg;
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	if (callout_pending(&sc->aac_daemontime) ||
	    callout_active(&sc->aac_daemontime) == 0)
		return;
	getmicrotime(&tv);
	aac_alloc_sync_fib(sc, &fib);
	*(uint32_t *)fib->data = tv.tv_sec;
	aac_sync_fib(sc, SendHostTime, 0, fib, sizeof(uint32_t));
	aac_release_sync_fib(sc);
	callout_schedule(&sc->aac_daemontime, 30 * 60 * hz);
}

void
aac_add_event(struct aac_softc *sc, struct aac_event *event)
{

	switch (event->ev_type & AAC_EVENT_MASK) {
	case AAC_EVENT_CMFREE:
		TAILQ_INSERT_TAIL(&sc->aac_ev_cmfree, event, ev_links);
		break;
	default:
		device_printf(sc->aac_dev, "aac_add event: unknown event %d\n",
		    event->ev_type);
		break;
	}
}

/*
 * Request information of container #cid
 */
static struct aac_mntinforesp *
aac_get_container_info(struct aac_softc *sc, struct aac_fib *fib, int cid)
{
	struct aac_mntinfo *mi;

	mi = (struct aac_mntinfo *)&fib->data[0];
	/* use 64-bit LBA if enabled */
	mi->Command = (sc->flags & AAC_FLAGS_LBA_64BIT) ?
	    VM_NameServe64 : VM_NameServe;
	mi->MntType = FT_FILESYS;
	mi->MntCount = cid;

	if (aac_sync_fib(sc, ContainerCommand, 0, fib,
			 sizeof(struct aac_mntinfo))) {
		device_printf(sc->aac_dev, "Error probing container %d\n", cid);
		return (NULL);
	}

	return ((struct aac_mntinforesp *)&fib->data[0]);
}

/*
 * Probe for containers, create disks.
 */
static void
aac_startup(void *arg)
{
	struct aac_softc *sc;
	struct aac_fib *fib;
	struct aac_mntinforesp *mir;
	int count = 0, i = 0;

	sc = (struct aac_softc *)arg;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	mtx_lock(&sc->aac_io_lock);
	aac_alloc_sync_fib(sc, &fib);

	/* loop over possible containers */
	do {
		if ((mir = aac_get_container_info(sc, fib, i)) == NULL)
			continue;
		if (i == 0)
			count = mir->MntRespCount;
		aac_add_container(sc, mir, 0);
		i++;
	} while ((i < count) && (i < AAC_MAX_CONTAINERS));

	aac_release_sync_fib(sc);
	mtx_unlock(&sc->aac_io_lock);

	/* mark the controller up */
	sc->aac_state &= ~AAC_STATE_SUSPEND;

	/* poke the bus to actually attach the child devices */
	if (bus_generic_attach(sc->aac_dev))
		device_printf(sc->aac_dev, "bus_generic_attach failed\n");

	/* disconnect ourselves from the intrhook chain */
	config_intrhook_disestablish(&sc->aac_ich);

	/* enable interrupts now */
	AAC_UNMASK_INTERRUPTS(sc);
}

/*
 * Create a device to represent a new container
 */
static void
aac_add_container(struct aac_softc *sc, struct aac_mntinforesp *mir, int f)
{
	struct aac_container *co;
	device_t child;

	/*
	 * Check container volume type for validity.  Note that many of
	 * the possible types may never show up.
	 */
	if ((mir->Status == ST_OK) && (mir->MntTable[0].VolType != CT_NONE)) {
		co = (struct aac_container *)malloc(sizeof *co, M_AACBUF,
		       M_NOWAIT | M_ZERO);
		if (co == NULL)
			panic("Out of memory?!");
		fwprintf(sc, HBA_FLAGS_DBG_INIT_B, "id %x  name '%.16s'  size %u  type %d",
		      mir->MntTable[0].ObjectId,
		      mir->MntTable[0].FileSystemName,
		      mir->MntTable[0].Capacity, mir->MntTable[0].VolType);

		if ((child = device_add_child(sc->aac_dev, "aacd", -1)) == NULL)
			device_printf(sc->aac_dev, "device_add_child failed\n");
		else
			device_set_ivars(child, co);
		device_set_desc(child, aac_describe_code(aac_container_types,
				mir->MntTable[0].VolType));
		co->co_disk = child;
		co->co_found = f;
		bcopy(&mir->MntTable[0], &co->co_mntobj,
		      sizeof(struct aac_mntobj));
		mtx_lock(&sc->aac_container_lock);
		TAILQ_INSERT_TAIL(&sc->aac_container_tqh, co, co_link);
		mtx_unlock(&sc->aac_container_lock);
	}
}

/*
 * Allocate resources associated with (sc)
 */
static int
aac_alloc(struct aac_softc *sc)
{

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/*
	 * Create DMA tag for mapping buffers into controller-addressable space.
	 */
	if (bus_dma_tag_create(sc->aac_parent_dmat, 	/* parent */
			       1, 0, 			/* algnmnt, boundary */
			       (sc->flags & AAC_FLAGS_SG_64BIT) ?
			       BUS_SPACE_MAXADDR :
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       sc->aac_max_sectors << 9, /* maxsize */
			       sc->aac_sg_tablesize,	/* nsegments */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			       BUS_DMA_ALLOCNOW,	/* flags */
			       busdma_lock_mutex,	/* lockfunc */
			       &sc->aac_io_lock,	/* lockfuncarg */
			       &sc->aac_buffer_dmat)) {
		device_printf(sc->aac_dev, "can't allocate buffer DMA tag\n");
		return (ENOMEM);
	}

	/*
	 * Create DMA tag for mapping FIBs into controller-addressable space..
	 */
	if (bus_dma_tag_create(sc->aac_parent_dmat,	/* parent */
			       1, 0, 			/* algnmnt, boundary */
			       (sc->flags & AAC_FLAGS_4GB_WINDOW) ?
			       BUS_SPACE_MAXADDR_32BIT :
			       0x7fffffff,		/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       sc->aac_max_fibs_alloc *
			       sc->aac_max_fib_size,  /* maxsize */
			       1,			/* nsegments */
			       sc->aac_max_fibs_alloc *
			       sc->aac_max_fib_size,	/* maxsize */
			       0,			/* flags */
			       NULL, NULL,		/* No locking needed */
			       &sc->aac_fib_dmat)) {
		device_printf(sc->aac_dev, "can't allocate FIB DMA tag\n");
		return (ENOMEM);
	}

	/*
	 * Create DMA tag for the common structure and allocate it.
	 */
	if (bus_dma_tag_create(sc->aac_parent_dmat, 	/* parent */
			       1, 0,			/* algnmnt, boundary */
			       (sc->flags & AAC_FLAGS_4GB_WINDOW) ?
			       BUS_SPACE_MAXADDR_32BIT :
			       0x7fffffff,		/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       8192 + sizeof(struct aac_common), /* maxsize */
			       1,			/* nsegments */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			       0,			/* flags */
			       NULL, NULL,		/* No locking needed */
			       &sc->aac_common_dmat)) {
		device_printf(sc->aac_dev,
			      "can't allocate common structure DMA tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->aac_common_dmat, (void **)&sc->aac_common,
			     BUS_DMA_NOWAIT, &sc->aac_common_dmamap)) {
		device_printf(sc->aac_dev, "can't allocate common structure\n");
		return (ENOMEM);
	}

	/*
	 * Work around a bug in the 2120 and 2200 that cannot DMA commands
	 * below address 8192 in physical memory.
	 * XXX If the padding is not needed, can it be put to use instead
	 * of ignored?
	 */
	(void)bus_dmamap_load(sc->aac_common_dmat, sc->aac_common_dmamap,
			sc->aac_common, 8192 + sizeof(*sc->aac_common),
			aac_common_map, sc, 0);

	if (sc->aac_common_busaddr < 8192) {
		sc->aac_common = (struct aac_common *)
		    ((uint8_t *)sc->aac_common + 8192);
		sc->aac_common_busaddr += 8192;
	}
	bzero(sc->aac_common, sizeof(*sc->aac_common));

	/* Allocate some FIBs and associated command structs */
	TAILQ_INIT(&sc->aac_fibmap_tqh);
	sc->aac_commands = malloc(sc->aac_max_fibs * sizeof(struct aac_command),
				  M_AACBUF, M_WAITOK|M_ZERO);
	while (sc->total_fibs < sc->aac_max_fibs) {
		if (aac_alloc_commands(sc) != 0)
			break;
	}
	if (sc->total_fibs == 0)
		return (ENOMEM);

	return (0);
}

/*
 * Free all of the resources associated with (sc)
 *
 * Should not be called if the controller is active.
 */
void
aac_free(struct aac_softc *sc)
{

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* remove the control device */
	if (sc->aac_dev_t != NULL)
		destroy_dev(sc->aac_dev_t);

	/* throw away any FIB buffers, discard the FIB DMA tag */
	aac_free_commands(sc);
	if (sc->aac_fib_dmat)
		bus_dma_tag_destroy(sc->aac_fib_dmat);

	free(sc->aac_commands, M_AACBUF);

	/* destroy the common area */
	if (sc->aac_common) {
		bus_dmamap_unload(sc->aac_common_dmat, sc->aac_common_dmamap);
		bus_dmamem_free(sc->aac_common_dmat, sc->aac_common,
				sc->aac_common_dmamap);
	}
	if (sc->aac_common_dmat)
		bus_dma_tag_destroy(sc->aac_common_dmat);

	/* disconnect the interrupt handler */
	if (sc->aac_intr)
		bus_teardown_intr(sc->aac_dev, sc->aac_irq, sc->aac_intr);
	if (sc->aac_irq != NULL) {
		bus_release_resource(sc->aac_dev, SYS_RES_IRQ,
		    rman_get_rid(sc->aac_irq), sc->aac_irq);
		pci_release_msi(sc->aac_dev);
	}

	/* destroy data-transfer DMA tag */
	if (sc->aac_buffer_dmat)
		bus_dma_tag_destroy(sc->aac_buffer_dmat);

	/* destroy the parent DMA tag */
	if (sc->aac_parent_dmat)
		bus_dma_tag_destroy(sc->aac_parent_dmat);

	/* release the register window mapping */
	if (sc->aac_regs_res0 != NULL)
		bus_release_resource(sc->aac_dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->aac_regs_res0), sc->aac_regs_res0);
	if (sc->aac_hwif == AAC_HWIF_NARK && sc->aac_regs_res1 != NULL)
		bus_release_resource(sc->aac_dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->aac_regs_res1), sc->aac_regs_res1);
}

/*
 * Disconnect from the controller completely, in preparation for unload.
 */
int
aac_detach(device_t dev)
{
	struct aac_softc *sc;
	struct aac_container *co;
	struct aac_sim	*sim;
	int error;

	sc = device_get_softc(dev);
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	callout_drain(&sc->aac_daemontime);

	mtx_lock(&sc->aac_io_lock);
	while (sc->aifflags & AAC_AIFFLAGS_RUNNING) {
		sc->aifflags |= AAC_AIFFLAGS_EXIT;
		wakeup(sc->aifthread);
		msleep(sc->aac_dev, &sc->aac_io_lock, PUSER, "aacdch", 0);
	}
	mtx_unlock(&sc->aac_io_lock);
	KASSERT((sc->aifflags & AAC_AIFFLAGS_RUNNING) == 0,
	    ("%s: invalid detach state", __func__));

	/* Remove the child containers */
	while ((co = TAILQ_FIRST(&sc->aac_container_tqh)) != NULL) {
		error = device_delete_child(dev, co->co_disk);
		if (error)
			return (error);
		TAILQ_REMOVE(&sc->aac_container_tqh, co, co_link);
		free(co, M_AACBUF);
	}

	/* Remove the CAM SIMs */
	while ((sim = TAILQ_FIRST(&sc->aac_sim_tqh)) != NULL) {
		TAILQ_REMOVE(&sc->aac_sim_tqh, sim, sim_link);
		error = device_delete_child(dev, sim->sim_dev);
		if (error)
			return (error);
		free(sim, M_AACBUF);
	}

	if ((error = aac_shutdown(dev)))
		return(error);

	EVENTHANDLER_DEREGISTER(shutdown_final, sc->eh);

	aac_free(sc);

	mtx_destroy(&sc->aac_aifq_lock);
	mtx_destroy(&sc->aac_io_lock);
	mtx_destroy(&sc->aac_container_lock);

	return(0);
}

/*
 * Bring the controller down to a dormant state and detach all child devices.
 *
 * This function is called before detach or system shutdown.
 *
 * Note that we can assume that the bioq on the controller is empty, as we won't
 * allow shutdown if any device is open.
 */
int
aac_shutdown(device_t dev)
{
	struct aac_softc *sc;
	struct aac_fib *fib;
	struct aac_close_command *cc;

	sc = device_get_softc(dev);
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	sc->aac_state |= AAC_STATE_SUSPEND;

	/*
	 * Send a Container shutdown followed by a HostShutdown FIB to the
	 * controller to convince it that we don't want to talk to it anymore.
	 * We've been closed and all I/O completed already
	 */
	device_printf(sc->aac_dev, "shutting down controller...");

	mtx_lock(&sc->aac_io_lock);
	aac_alloc_sync_fib(sc, &fib);
	cc = (struct aac_close_command *)&fib->data[0];

	bzero(cc, sizeof(struct aac_close_command));
	cc->Command = VM_CloseAll;
	cc->ContainerId = 0xffffffff;
	if (aac_sync_fib(sc, ContainerCommand, 0, fib,
	    sizeof(struct aac_close_command)))
		printf("FAILED.\n");
	else
		printf("done\n");
#if 0
	else {
		fib->data[0] = 0;
		/*
		 * XXX Issuing this command to the controller makes it shut down
		 * but also keeps it from coming back up without a reset of the
		 * PCI bus.  This is not desirable if you are just unloading the
		 * driver module with the intent to reload it later.
		 */
		if (aac_sync_fib(sc, FsaHostShutdown, AAC_FIBSTATE_SHUTDOWN,
		    fib, 1)) {
			printf("FAILED.\n");
		} else {
			printf("done.\n");
		}
	}
#endif

	AAC_MASK_INTERRUPTS(sc);
	aac_release_sync_fib(sc);
	mtx_unlock(&sc->aac_io_lock);

	return(0);
}

/*
 * Bring the controller to a quiescent state, ready for system suspend.
 */
int
aac_suspend(device_t dev)
{
	struct aac_softc *sc;

	sc = device_get_softc(dev);

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	sc->aac_state |= AAC_STATE_SUSPEND;

	AAC_MASK_INTERRUPTS(sc);
	return(0);
}

/*
 * Bring the controller back to a state ready for operation.
 */
int
aac_resume(device_t dev)
{
	struct aac_softc *sc;

	sc = device_get_softc(dev);

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	sc->aac_state &= ~AAC_STATE_SUSPEND;
	AAC_UNMASK_INTERRUPTS(sc);
	return(0);
}

/*
 * Interrupt handler for NEW_COMM interface.
 */
void
aac_new_intr(void *arg)
{
	struct aac_softc *sc;
	u_int32_t index, fast;
	struct aac_command *cm;
	struct aac_fib *fib;
	int i;

	sc = (struct aac_softc *)arg;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	mtx_lock(&sc->aac_io_lock);
	while (1) {
		index = AAC_GET_OUTB_QUEUE(sc);
		if (index == 0xffffffff)
			index = AAC_GET_OUTB_QUEUE(sc);
		if (index == 0xffffffff)
			break;
		if (index & 2) {
			if (index == 0xfffffffe) {
				/* XXX This means that the controller wants
				 * more work.  Ignore it for now.
				 */
				continue;
			}
			/* AIF */
			fib = (struct aac_fib *)malloc(sizeof *fib, M_AACBUF,
				   M_NOWAIT | M_ZERO);
			if (fib == NULL) {
				/* If we're really this short on memory,
				 * hopefully breaking out of the handler will
				 * allow something to get freed.  This
				 * actually sucks a whole lot.
				 */
				break;
			}
			index &= ~2;
			for (i = 0; i < sizeof(struct aac_fib)/4; ++i)
				((u_int32_t *)fib)[i] = AAC_MEM1_GETREG4(sc, index + i*4);
			aac_handle_aif(sc, fib);
			free(fib, M_AACBUF);

			/*
			 * AIF memory is owned by the adapter, so let it
			 * know that we are done with it.
			 */
			AAC_SET_OUTB_QUEUE(sc, index);
			AAC_CLEAR_ISTATUS(sc, AAC_DB_RESPONSE_READY);
		} else {
			fast = index & 1;
			cm = sc->aac_commands + (index >> 2);
			fib = cm->cm_fib;
			if (fast) {
				fib->Header.XferState |= AAC_FIBSTATE_DONEADAP;
				*((u_int32_t *)(fib->data)) = AAC_ERROR_NORMAL;
			}
			aac_remove_busy(cm);
 			aac_unmap_command(cm);
			cm->cm_flags |= AAC_CMD_COMPLETED;

			/* is there a completion handler? */
			if (cm->cm_complete != NULL) {
				cm->cm_complete(cm);
			} else {
				/* assume that someone is sleeping on this
				 * command
				 */
				wakeup(cm);
			}
			sc->flags &= ~AAC_QUEUE_FRZN;
		}
	}
	/* see if we can start some more I/O */
	if ((sc->flags & AAC_QUEUE_FRZN) == 0)
		aac_startio(sc);

	mtx_unlock(&sc->aac_io_lock);
}

/*
 * Interrupt filter for !NEW_COMM interface.
 */
int
aac_filter(void *arg)
{
	struct aac_softc *sc;
	u_int16_t reason;

	sc = (struct aac_softc *)arg;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	/*
	 * Read the status register directly.  This is faster than taking the
	 * driver lock and reading the queues directly.  It also saves having
	 * to turn parts of the driver lock into a spin mutex, which would be
	 * ugly.
	 */
	reason = AAC_GET_ISTATUS(sc);
	AAC_CLEAR_ISTATUS(sc, reason);

	/* handle completion processing */
	if (reason & AAC_DB_RESPONSE_READY)
		taskqueue_enqueue(taskqueue_fast, &sc->aac_task_complete);

	/* controller wants to talk to us */
	if (reason & (AAC_DB_PRINTF | AAC_DB_COMMAND_READY)) {
		/*
		 * XXX Make sure that we don't get fooled by strange messages
		 * that start with a NULL.
		 */
		if ((reason & AAC_DB_PRINTF) &&
			(sc->aac_common->ac_printf[0] == 0))
			sc->aac_common->ac_printf[0] = 32;

		/*
		 * This might miss doing the actual wakeup.  However, the
		 * msleep that this is waking up has a timeout, so it will
		 * wake up eventually.  AIFs and printfs are low enough
		 * priority that they can handle hanging out for a few seconds
		 * if needed.
		 */
		wakeup(sc->aifthread);
	}
	return (FILTER_HANDLED);
}

/*
 * Command Processing
 */

/*
 * Start as much queued I/O as possible on the controller
 */
void
aac_startio(struct aac_softc *sc)
{
	struct aac_command *cm;
	int error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	for (;;) {
		/*
		 * This flag might be set if the card is out of resources.
		 * Checking it here prevents an infinite loop of deferrals.
		 */
		if (sc->flags & AAC_QUEUE_FRZN)
			break;

		/*
		 * Try to get a command that's been put off for lack of
		 * resources
		 */
		cm = aac_dequeue_ready(sc);

		/*
		 * Try to build a command off the bio queue (ignore error
		 * return)
		 */
		if (cm == NULL)
			aac_bio_command(sc, &cm);

		/* nothing to do? */
		if (cm == NULL)
			break;

		/* don't map more than once */
		if (cm->cm_flags & AAC_CMD_MAPPED)
			panic("aac: command %p already mapped", cm);

		/*
		 * Set up the command to go to the controller.  If there are no
		 * data buffers associated with the command then it can bypass
		 * busdma.
		 */
		if (cm->cm_datalen != 0) {
			if (cm->cm_flags & AAC_REQ_BIO)
				error = bus_dmamap_load_bio(
				    sc->aac_buffer_dmat, cm->cm_datamap,
				    (struct bio *)cm->cm_private,
				    aac_map_command_sg, cm, 0);
			else
				error = bus_dmamap_load(sc->aac_buffer_dmat,
				    cm->cm_datamap, cm->cm_data,
				    cm->cm_datalen, aac_map_command_sg, cm, 0);
			if (error == EINPROGRESS) {
				fwprintf(sc, HBA_FLAGS_DBG_COMM_B, "freezing queue\n");
				sc->flags |= AAC_QUEUE_FRZN;
			} else if (error != 0)
				panic("aac_startio: unexpected error %d from "
				      "busdma", error);
		} else
			aac_map_command_sg(cm, NULL, 0, 0);
	}
}

/*
 * Handle notification of one or more FIBs coming from the controller.
 */
static void
aac_command_thread(struct aac_softc *sc)
{
	struct aac_fib *fib;
	u_int32_t fib_size;
	int size, retval;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	mtx_lock(&sc->aac_io_lock);
	sc->aifflags = AAC_AIFFLAGS_RUNNING;

	while ((sc->aifflags & AAC_AIFFLAGS_EXIT) == 0) {

		retval = 0;
		if ((sc->aifflags & AAC_AIFFLAGS_PENDING) == 0)
			retval = msleep(sc->aifthread, &sc->aac_io_lock, PRIBIO,
					"aifthd", AAC_PERIODIC_INTERVAL * hz);

		/*
		 * First see if any FIBs need to be allocated.  This needs
		 * to be called without the driver lock because contigmalloc
		 * can sleep.
		 */
		if ((sc->aifflags & AAC_AIFFLAGS_ALLOCFIBS) != 0) {
			mtx_unlock(&sc->aac_io_lock);
			aac_alloc_commands(sc);
			mtx_lock(&sc->aac_io_lock);
			sc->aifflags &= ~AAC_AIFFLAGS_ALLOCFIBS;
			aac_startio(sc);
		}

		/*
		 * While we're here, check to see if any commands are stuck.
		 * This is pretty low-priority, so it's ok if it doesn't
		 * always fire.
		 */
		if (retval == EWOULDBLOCK)
			aac_timeout(sc);

		/* Check the hardware printf message buffer */
		if (sc->aac_common->ac_printf[0] != 0)
			aac_print_printf(sc);

		/* Also check to see if the adapter has a command for us. */
		if (sc->flags & AAC_FLAGS_NEW_COMM)
			continue;
		for (;;) {
			if (aac_dequeue_fib(sc, AAC_HOST_NORM_CMD_QUEUE,
					   &fib_size, &fib))
				break;

			AAC_PRINT_FIB(sc, fib);

			switch (fib->Header.Command) {
			case AifRequest:
				aac_handle_aif(sc, fib);
				break;
			default:
				device_printf(sc->aac_dev, "unknown command "
					      "from controller\n");
				break;
			}

			if ((fib->Header.XferState == 0) ||
			    (fib->Header.StructType != AAC_FIBTYPE_TFIB)) {
				break;
			}

			/* Return the AIF to the controller. */
			if (fib->Header.XferState & AAC_FIBSTATE_FROMADAP) {
				fib->Header.XferState |= AAC_FIBSTATE_DONEHOST;
				*(AAC_FSAStatus*)fib->data = ST_OK;

				/* XXX Compute the Size field? */
				size = fib->Header.Size;
				if (size > sizeof(struct aac_fib)) {
					size = sizeof(struct aac_fib);
					fib->Header.Size = size;
				}
				/*
				 * Since we did not generate this command, it
				 * cannot go through the normal
				 * enqueue->startio chain.
				 */
				aac_enqueue_response(sc,
						 AAC_ADAP_NORM_RESP_QUEUE,
						 fib);
			}
		}
	}
	sc->aifflags &= ~AAC_AIFFLAGS_RUNNING;
	mtx_unlock(&sc->aac_io_lock);
	wakeup(sc->aac_dev);

	kproc_exit(0);
}

/*
 * Process completed commands.
 */
static void
aac_complete(void *context, int pending)
{
	struct aac_softc *sc;
	struct aac_command *cm;
	struct aac_fib *fib;
	u_int32_t fib_size;

	sc = (struct aac_softc *)context;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	mtx_lock(&sc->aac_io_lock);

	/* pull completed commands off the queue */
	for (;;) {
		/* look for completed FIBs on our queue */
		if (aac_dequeue_fib(sc, AAC_HOST_NORM_RESP_QUEUE, &fib_size,
							&fib))
			break;	/* nothing to do */

		/* get the command, unmap and hand off for processing */
		cm = sc->aac_commands + fib->Header.SenderData;
		if (cm == NULL) {
			AAC_PRINT_FIB(sc, fib);
			break;
		}
		if ((cm->cm_flags & AAC_CMD_TIMEDOUT) != 0)
			device_printf(sc->aac_dev,
			    "COMMAND %p COMPLETED AFTER %d SECONDS\n",
			    cm, (int)(time_uptime-cm->cm_timestamp));

		aac_remove_busy(cm);

 		aac_unmap_command(cm);
		cm->cm_flags |= AAC_CMD_COMPLETED;

		/* is there a completion handler? */
		if (cm->cm_complete != NULL) {
			cm->cm_complete(cm);
		} else {
			/* assume that someone is sleeping on this command */
			wakeup(cm);
		}
	}

	/* see if we can start some more I/O */
	sc->flags &= ~AAC_QUEUE_FRZN;
	aac_startio(sc);

	mtx_unlock(&sc->aac_io_lock);
}

/*
 * Handle a bio submitted from a disk device.
 */
void
aac_submit_bio(struct bio *bp)
{
	struct aac_disk *ad;
	struct aac_softc *sc;

	ad = (struct aac_disk *)bp->bio_disk->d_drv1;
	sc = ad->ad_controller;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* queue the BIO and try to get some work done */
	aac_enqueue_bio(sc, bp);
	aac_startio(sc);
}

/*
 * Get a bio and build a command to go with it.
 */
static int
aac_bio_command(struct aac_softc *sc, struct aac_command **cmp)
{
	struct aac_command *cm;
	struct aac_fib *fib;
	struct aac_disk *ad;
	struct bio *bp;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* get the resources we will need */
	cm = NULL;
	bp = NULL;
	if (aac_alloc_command(sc, &cm))	/* get a command */
		goto fail;
	if ((bp = aac_dequeue_bio(sc)) == NULL)
		goto fail;

	/* fill out the command */
	cm->cm_datalen = bp->bio_bcount;
	cm->cm_complete = aac_bio_complete;
	cm->cm_flags = AAC_REQ_BIO;
	cm->cm_private = bp;
	cm->cm_timestamp = time_uptime;

	/* build the FIB */
	fib = cm->cm_fib;
	fib->Header.Size = sizeof(struct aac_fib_header);
	fib->Header.XferState =
		AAC_FIBSTATE_HOSTOWNED   |
		AAC_FIBSTATE_INITIALISED |
		AAC_FIBSTATE_EMPTY	 |
		AAC_FIBSTATE_FROMHOST	 |
		AAC_FIBSTATE_REXPECTED   |
		AAC_FIBSTATE_NORM	 |
		AAC_FIBSTATE_ASYNC	 |
		AAC_FIBSTATE_FAST_RESPONSE;

	/* build the read/write request */
	ad = (struct aac_disk *)bp->bio_disk->d_drv1;

	if (sc->flags & AAC_FLAGS_RAW_IO) {
		struct aac_raw_io *raw;
		raw = (struct aac_raw_io *)&fib->data[0];
		fib->Header.Command = RawIo;
		raw->BlockNumber = (u_int64_t)bp->bio_pblkno;
		raw->ByteCount = bp->bio_bcount;
		raw->ContainerId = ad->ad_container->co_mntobj.ObjectId;
		raw->BpTotal = 0;
		raw->BpComplete = 0;
		fib->Header.Size += sizeof(struct aac_raw_io);
		cm->cm_sgtable = (struct aac_sg_table *)&raw->SgMapRaw;
		if (bp->bio_cmd == BIO_READ) {
			raw->Flags = 1;
			cm->cm_flags |= AAC_CMD_DATAIN;
		} else {
			raw->Flags = 0;
			cm->cm_flags |= AAC_CMD_DATAOUT;
		}
	} else if ((sc->flags & AAC_FLAGS_SG_64BIT) == 0) {
		fib->Header.Command = ContainerCommand;
		if (bp->bio_cmd == BIO_READ) {
			struct aac_blockread *br;
			br = (struct aac_blockread *)&fib->data[0];
			br->Command = VM_CtBlockRead;
			br->ContainerId = ad->ad_container->co_mntobj.ObjectId;
			br->BlockNumber = bp->bio_pblkno;
			br->ByteCount = bp->bio_bcount;
			fib->Header.Size += sizeof(struct aac_blockread);
			cm->cm_sgtable = &br->SgMap;
			cm->cm_flags |= AAC_CMD_DATAIN;
		} else {
			struct aac_blockwrite *bw;
			bw = (struct aac_blockwrite *)&fib->data[0];
			bw->Command = VM_CtBlockWrite;
			bw->ContainerId = ad->ad_container->co_mntobj.ObjectId;
			bw->BlockNumber = bp->bio_pblkno;
			bw->ByteCount = bp->bio_bcount;
			bw->Stable = CUNSTABLE;
			fib->Header.Size += sizeof(struct aac_blockwrite);
			cm->cm_flags |= AAC_CMD_DATAOUT;
			cm->cm_sgtable = &bw->SgMap;
		}
	} else {
		fib->Header.Command = ContainerCommand64;
		if (bp->bio_cmd == BIO_READ) {
			struct aac_blockread64 *br;
			br = (struct aac_blockread64 *)&fib->data[0];
			br->Command = VM_CtHostRead64;
			br->ContainerId = ad->ad_container->co_mntobj.ObjectId;
			br->SectorCount = bp->bio_bcount / AAC_BLOCK_SIZE;
			br->BlockNumber = bp->bio_pblkno;
			br->Pad = 0;
			br->Flags = 0;
			fib->Header.Size += sizeof(struct aac_blockread64);
			cm->cm_flags |= AAC_CMD_DATAIN;
			cm->cm_sgtable = (struct aac_sg_table *)&br->SgMap64;
		} else {
			struct aac_blockwrite64 *bw;
			bw = (struct aac_blockwrite64 *)&fib->data[0];
			bw->Command = VM_CtHostWrite64;
			bw->ContainerId = ad->ad_container->co_mntobj.ObjectId;
			bw->SectorCount = bp->bio_bcount / AAC_BLOCK_SIZE;
			bw->BlockNumber = bp->bio_pblkno;
			bw->Pad = 0;
			bw->Flags = 0;
			fib->Header.Size += sizeof(struct aac_blockwrite64);
			cm->cm_flags |= AAC_CMD_DATAOUT;
			cm->cm_sgtable = (struct aac_sg_table *)&bw->SgMap64;
		}
	}

	*cmp = cm;
	return(0);

fail:
	if (bp != NULL)
		aac_enqueue_bio(sc, bp);
	if (cm != NULL)
		aac_release_command(cm);
	return(ENOMEM);
}

/*
 * Handle a bio-instigated command that has been completed.
 */
static void
aac_bio_complete(struct aac_command *cm)
{
	struct aac_blockread_response *brr;
	struct aac_blockwrite_response *bwr;
	struct bio *bp;
	AAC_FSAStatus status;

	/* fetch relevant status and then release the command */
	bp = (struct bio *)cm->cm_private;
	if (bp->bio_cmd == BIO_READ) {
		brr = (struct aac_blockread_response *)&cm->cm_fib->data[0];
		status = brr->Status;
	} else {
		bwr = (struct aac_blockwrite_response *)&cm->cm_fib->data[0];
		status = bwr->Status;
	}
	aac_release_command(cm);

	/* fix up the bio based on status */
	if (status == ST_OK) {
		bp->bio_resid = 0;
	} else {
		bp->bio_error = EIO;
		bp->bio_flags |= BIO_ERROR;
	}
	aac_biodone(bp);
}

/*
 * Submit a command to the controller, return when it completes.
 * XXX This is very dangerous!  If the card has gone out to lunch, we could
 *     be stuck here forever.  At the same time, signals are not caught
 *     because there is a risk that a signal could wakeup the sleep before
 *     the card has a chance to complete the command.  Since there is no way
 *     to cancel a command that is in progress, we can't protect against the
 *     card completing a command late and spamming the command and data
 *     memory.  So, we are held hostage until the command completes.
 */
static int
aac_wait_command(struct aac_command *cm)
{
	struct aac_softc *sc;
	int error;

	sc = cm->cm_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* Put the command on the ready queue and get things going */
	aac_enqueue_ready(cm);
	aac_startio(sc);
	error = msleep(cm, &sc->aac_io_lock, PRIBIO, "aacwait", 0);
	return(error);
}

/*
 *Command Buffer Management
 */

/*
 * Allocate a command.
 */
int
aac_alloc_command(struct aac_softc *sc, struct aac_command **cmp)
{
	struct aac_command *cm;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if ((cm = aac_dequeue_free(sc)) == NULL) {
		if (sc->total_fibs < sc->aac_max_fibs) {
			mtx_lock(&sc->aac_io_lock);
			sc->aifflags |= AAC_AIFFLAGS_ALLOCFIBS;
			mtx_unlock(&sc->aac_io_lock);
			wakeup(sc->aifthread);
		}
		return (EBUSY);
	}

	*cmp = cm;
	return(0);
}

/*
 * Release a command back to the freelist.
 */
void
aac_release_command(struct aac_command *cm)
{
	struct aac_event *event;
	struct aac_softc *sc;

	sc = cm->cm_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* (re)initialize the command/FIB */
	cm->cm_datalen = 0;
	cm->cm_sgtable = NULL;
	cm->cm_flags = 0;
	cm->cm_complete = NULL;
	cm->cm_private = NULL;
	cm->cm_queue = AAC_ADAP_NORM_CMD_QUEUE;
	cm->cm_fib->Header.XferState = AAC_FIBSTATE_EMPTY;
	cm->cm_fib->Header.StructType = AAC_FIBTYPE_TFIB;
	cm->cm_fib->Header.Flags = 0;
	cm->cm_fib->Header.SenderSize = cm->cm_sc->aac_max_fib_size;

	/*
	 * These are duplicated in aac_start to cover the case where an
	 * intermediate stage may have destroyed them.  They're left
	 * initialized here for debugging purposes only.
	 */
	cm->cm_fib->Header.ReceiverFibAddress = (u_int32_t)cm->cm_fibphys;
	cm->cm_fib->Header.SenderData = 0;

	aac_enqueue_free(cm);

	if ((event = TAILQ_FIRST(&sc->aac_ev_cmfree)) != NULL) {
		TAILQ_REMOVE(&sc->aac_ev_cmfree, event, ev_links);
		event->ev_callback(sc, event, event->ev_arg);
	}
}

/*
 * Map helper for command/FIB allocation.
 */
static void
aac_map_command_helper(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	uint64_t	*fibphys;

	fibphys = (uint64_t *)arg;

	*fibphys = segs[0].ds_addr;
}

/*
 * Allocate and initialize commands/FIBs for this adapter.
 */
static int
aac_alloc_commands(struct aac_softc *sc)
{
	struct aac_command *cm;
	struct aac_fibmap *fm;
	uint64_t fibphys;
	int i, error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if (sc->total_fibs + sc->aac_max_fibs_alloc > sc->aac_max_fibs)
		return (ENOMEM);

	fm = malloc(sizeof(struct aac_fibmap), M_AACBUF, M_NOWAIT|M_ZERO);
	if (fm == NULL)
		return (ENOMEM);

	/* allocate the FIBs in DMAable memory and load them */
	if (bus_dmamem_alloc(sc->aac_fib_dmat, (void **)&fm->aac_fibs,
			     BUS_DMA_NOWAIT, &fm->aac_fibmap)) {
		device_printf(sc->aac_dev,
			      "Not enough contiguous memory available.\n");
		free(fm, M_AACBUF);
		return (ENOMEM);
	}

	/* Ignore errors since this doesn't bounce */
	(void)bus_dmamap_load(sc->aac_fib_dmat, fm->aac_fibmap, fm->aac_fibs,
			      sc->aac_max_fibs_alloc * sc->aac_max_fib_size,
			      aac_map_command_helper, &fibphys, 0);

	/* initialize constant fields in the command structure */
	bzero(fm->aac_fibs, sc->aac_max_fibs_alloc * sc->aac_max_fib_size);
	for (i = 0; i < sc->aac_max_fibs_alloc; i++) {
		cm = sc->aac_commands + sc->total_fibs;
		fm->aac_commands = cm;
		cm->cm_sc = sc;
		cm->cm_fib = (struct aac_fib *)
			((u_int8_t *)fm->aac_fibs + i*sc->aac_max_fib_size);
		cm->cm_fibphys = fibphys + i*sc->aac_max_fib_size;
		cm->cm_index = sc->total_fibs;

		if ((error = bus_dmamap_create(sc->aac_buffer_dmat, 0,
					       &cm->cm_datamap)) != 0)
			break;
		mtx_lock(&sc->aac_io_lock);
		aac_release_command(cm);
		sc->total_fibs++;
		mtx_unlock(&sc->aac_io_lock);
	}

	if (i > 0) {
		mtx_lock(&sc->aac_io_lock);
		TAILQ_INSERT_TAIL(&sc->aac_fibmap_tqh, fm, fm_link);
		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, "total_fibs= %d\n", sc->total_fibs);
		mtx_unlock(&sc->aac_io_lock);
		return (0);
	}

	bus_dmamap_unload(sc->aac_fib_dmat, fm->aac_fibmap);
	bus_dmamem_free(sc->aac_fib_dmat, fm->aac_fibs, fm->aac_fibmap);
	free(fm, M_AACBUF);
	return (ENOMEM);
}

/*
 * Free FIBs owned by this adapter.
 */
static void
aac_free_commands(struct aac_softc *sc)
{
	struct aac_fibmap *fm;
	struct aac_command *cm;
	int i;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	while ((fm = TAILQ_FIRST(&sc->aac_fibmap_tqh)) != NULL) {

		TAILQ_REMOVE(&sc->aac_fibmap_tqh, fm, fm_link);
		/*
		 * We check against total_fibs to handle partially
		 * allocated blocks.
		 */
		for (i = 0; i < sc->aac_max_fibs_alloc && sc->total_fibs--; i++) {
			cm = fm->aac_commands + i;
			bus_dmamap_destroy(sc->aac_buffer_dmat, cm->cm_datamap);
		}
		bus_dmamap_unload(sc->aac_fib_dmat, fm->aac_fibmap);
		bus_dmamem_free(sc->aac_fib_dmat, fm->aac_fibs, fm->aac_fibmap);
		free(fm, M_AACBUF);
	}
}

/*
 * Command-mapping helper function - populate this command's s/g table.
 */
static void
aac_map_command_sg(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct aac_softc *sc;
	struct aac_command *cm;
	struct aac_fib *fib;
	int i;

	cm = (struct aac_command *)arg;
	sc = cm->cm_sc;
	fib = cm->cm_fib;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* copy into the FIB */
	if (cm->cm_sgtable != NULL) {
		if (fib->Header.Command == RawIo) {
			struct aac_sg_tableraw *sg;
			sg = (struct aac_sg_tableraw *)cm->cm_sgtable;
			sg->SgCount = nseg;
			for (i = 0; i < nseg; i++) {
				sg->SgEntryRaw[i].SgAddress = segs[i].ds_addr;
				sg->SgEntryRaw[i].SgByteCount = segs[i].ds_len;
				sg->SgEntryRaw[i].Next = 0;
				sg->SgEntryRaw[i].Prev = 0;
				sg->SgEntryRaw[i].Flags = 0;
			}
			/* update the FIB size for the s/g count */
			fib->Header.Size += nseg*sizeof(struct aac_sg_entryraw);
		} else if ((cm->cm_sc->flags & AAC_FLAGS_SG_64BIT) == 0) {
			struct aac_sg_table *sg;
			sg = cm->cm_sgtable;
			sg->SgCount = nseg;
			for (i = 0; i < nseg; i++) {
				sg->SgEntry[i].SgAddress = segs[i].ds_addr;
				sg->SgEntry[i].SgByteCount = segs[i].ds_len;
			}
			/* update the FIB size for the s/g count */
			fib->Header.Size += nseg*sizeof(struct aac_sg_entry);
		} else {
			struct aac_sg_table64 *sg;
			sg = (struct aac_sg_table64 *)cm->cm_sgtable;
			sg->SgCount = nseg;
			for (i = 0; i < nseg; i++) {
				sg->SgEntry64[i].SgAddress = segs[i].ds_addr;
				sg->SgEntry64[i].SgByteCount = segs[i].ds_len;
			}
			/* update the FIB size for the s/g count */
			fib->Header.Size += nseg*sizeof(struct aac_sg_entry64);
		}
	}

	/* Fix up the address values in the FIB.  Use the command array index
	 * instead of a pointer since these fields are only 32 bits.  Shift
	 * the SenderFibAddress over to make room for the fast response bit
	 * and for the AIF bit
	 */
	cm->cm_fib->Header.SenderFibAddress = (cm->cm_index << 2);
	cm->cm_fib->Header.ReceiverFibAddress = (u_int32_t)cm->cm_fibphys;

	/* save a pointer to the command for speedy reverse-lookup */
	cm->cm_fib->Header.SenderData = cm->cm_index;

	if (cm->cm_flags & AAC_CMD_DATAIN)
		bus_dmamap_sync(sc->aac_buffer_dmat, cm->cm_datamap,
				BUS_DMASYNC_PREREAD);
	if (cm->cm_flags & AAC_CMD_DATAOUT)
		bus_dmamap_sync(sc->aac_buffer_dmat, cm->cm_datamap,
				BUS_DMASYNC_PREWRITE);
	cm->cm_flags |= AAC_CMD_MAPPED;

	if (sc->flags & AAC_FLAGS_NEW_COMM) {
		int count = 10000000L;
		while (AAC_SEND_COMMAND(sc, cm) != 0) {
			if (--count == 0) {
				aac_unmap_command(cm);
				sc->flags |= AAC_QUEUE_FRZN;
				aac_requeue_ready(cm);
			}
			DELAY(5);			/* wait 5 usec. */
		}
	} else {
		/* Put the FIB on the outbound queue */
		if (aac_enqueue_fib(sc, cm->cm_queue, cm) == EBUSY) {
			aac_unmap_command(cm);
			sc->flags |= AAC_QUEUE_FRZN;
			aac_requeue_ready(cm);
		}
	}
}

/*
 * Unmap a command from controller-visible space.
 */
static void
aac_unmap_command(struct aac_command *cm)
{
	struct aac_softc *sc;

	sc = cm->cm_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if (!(cm->cm_flags & AAC_CMD_MAPPED))
		return;

	if (cm->cm_datalen != 0) {
		if (cm->cm_flags & AAC_CMD_DATAIN)
			bus_dmamap_sync(sc->aac_buffer_dmat, cm->cm_datamap,
					BUS_DMASYNC_POSTREAD);
		if (cm->cm_flags & AAC_CMD_DATAOUT)
			bus_dmamap_sync(sc->aac_buffer_dmat, cm->cm_datamap,
					BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->aac_buffer_dmat, cm->cm_datamap);
	}
	cm->cm_flags &= ~AAC_CMD_MAPPED;
}

/*
 * Hardware Interface
 */

/*
 * Initialize the adapter.
 */
static void
aac_common_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct aac_softc *sc;

	sc = (struct aac_softc *)arg;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	sc->aac_common_busaddr = segs[0].ds_addr;
}

static int
aac_check_firmware(struct aac_softc *sc)
{
	u_int32_t code, major, minor, options = 0, atu_size = 0;
	int rid, status;
	time_t then;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	/*
	 * Wait for the adapter to come ready.
	 */
	then = time_uptime;
	do {
		code = AAC_GET_FWSTATUS(sc);
		if (code & AAC_SELF_TEST_FAILED) {
			device_printf(sc->aac_dev, "FATAL: selftest failed\n");
			return(ENXIO);
		}
		if (code & AAC_KERNEL_PANIC) {
			device_printf(sc->aac_dev,
				      "FATAL: controller kernel panic");
			return(ENXIO);
		}
		if (time_uptime > (then + AAC_BOOT_TIMEOUT)) {
			device_printf(sc->aac_dev,
				      "FATAL: controller not coming ready, "
					   "status %x\n", code);
			return(ENXIO);
		}
	} while (!(code & AAC_UP_AND_RUNNING));

	/*
	 * Retrieve the firmware version numbers.  Dell PERC2/QC cards with
	 * firmware version 1.x are not compatible with this driver.
	 */
	if (sc->flags & AAC_FLAGS_PERC2QC) {
		if (aac_sync_command(sc, AAC_MONKER_GETKERNVER, 0, 0, 0, 0,
				     NULL)) {
			device_printf(sc->aac_dev,
				      "Error reading firmware version\n");
			return (EIO);
		}

		/* These numbers are stored as ASCII! */
		major = (AAC_GET_MAILBOX(sc, 1) & 0xff) - 0x30;
		minor = (AAC_GET_MAILBOX(sc, 2) & 0xff) - 0x30;
		if (major == 1) {
			device_printf(sc->aac_dev,
			    "Firmware version %d.%d is not supported.\n",
			    major, minor);
			return (EINVAL);
		}
	}

	/*
	 * Retrieve the capabilities/supported options word so we know what
	 * work-arounds to enable.  Some firmware revs don't support this
	 * command.
	 */
	if (aac_sync_command(sc, AAC_MONKER_GETINFO, 0, 0, 0, 0, &status)) {
		if (status != AAC_SRB_STS_INVALID_REQUEST) {
			device_printf(sc->aac_dev,
			     "RequestAdapterInfo failed\n");
			return (EIO);
		}
	} else {
		options = AAC_GET_MAILBOX(sc, 1);
		atu_size = AAC_GET_MAILBOX(sc, 2);
		sc->supported_options = options;

		if ((options & AAC_SUPPORTED_4GB_WINDOW) != 0 &&
		    (sc->flags & AAC_FLAGS_NO4GB) == 0)
			sc->flags |= AAC_FLAGS_4GB_WINDOW;
		if (options & AAC_SUPPORTED_NONDASD)
			sc->flags |= AAC_FLAGS_ENABLE_CAM;
		if ((options & AAC_SUPPORTED_SGMAP_HOST64) != 0
		     && (sizeof(bus_addr_t) > 4)) {
			device_printf(sc->aac_dev,
			    "Enabling 64-bit address support\n");
			sc->flags |= AAC_FLAGS_SG_64BIT;
		}
		if ((options & AAC_SUPPORTED_NEW_COMM)
		 && sc->aac_if->aif_send_command)
			sc->flags |= AAC_FLAGS_NEW_COMM;
		if (options & AAC_SUPPORTED_64BIT_ARRAYSIZE)
			sc->flags |= AAC_FLAGS_ARRAY_64BIT;
	}

	/* Check for broken hardware that does a lower number of commands */
	sc->aac_max_fibs = (sc->flags & AAC_FLAGS_256FIBS ? 256:512);

	/* Remap mem. resource, if required */
	if ((sc->flags & AAC_FLAGS_NEW_COMM) &&
	    atu_size > rman_get_size(sc->aac_regs_res1)) {
		rid = rman_get_rid(sc->aac_regs_res1);
		bus_release_resource(sc->aac_dev, SYS_RES_MEMORY, rid,
		    sc->aac_regs_res1);
		sc->aac_regs_res1 = bus_alloc_resource_anywhere(sc->aac_dev,
		    SYS_RES_MEMORY, &rid, atu_size, RF_ACTIVE);
		if (sc->aac_regs_res1 == NULL) {
			sc->aac_regs_res1 = bus_alloc_resource_any(
			    sc->aac_dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
			if (sc->aac_regs_res1 == NULL) {
				device_printf(sc->aac_dev,
				    "couldn't allocate register window\n");
				return (ENXIO);
			}
			sc->flags &= ~AAC_FLAGS_NEW_COMM;
		}
		sc->aac_btag1 = rman_get_bustag(sc->aac_regs_res1);
		sc->aac_bhandle1 = rman_get_bushandle(sc->aac_regs_res1);

		if (sc->aac_hwif == AAC_HWIF_NARK) {
			sc->aac_regs_res0 = sc->aac_regs_res1;
			sc->aac_btag0 = sc->aac_btag1;
			sc->aac_bhandle0 = sc->aac_bhandle1;
		}
	}

	/* Read preferred settings */
	sc->aac_max_fib_size = sizeof(struct aac_fib);
	sc->aac_max_sectors = 128;				/* 64KB */
	if (sc->flags & AAC_FLAGS_SG_64BIT)
		sc->aac_sg_tablesize = (AAC_FIB_DATASIZE
		 - sizeof(struct aac_blockwrite64))
		 / sizeof(struct aac_sg_entry64);
	else
		sc->aac_sg_tablesize = (AAC_FIB_DATASIZE
		 - sizeof(struct aac_blockwrite))
		 / sizeof(struct aac_sg_entry);

	if (!aac_sync_command(sc, AAC_MONKER_GETCOMMPREF, 0, 0, 0, 0, NULL)) {
		options = AAC_GET_MAILBOX(sc, 1);
		sc->aac_max_fib_size = (options & 0xFFFF);
		sc->aac_max_sectors = (options >> 16) << 1;
		options = AAC_GET_MAILBOX(sc, 2);
		sc->aac_sg_tablesize = (options >> 16);
		options = AAC_GET_MAILBOX(sc, 3);
		sc->aac_max_fibs = (options & 0xFFFF);
	}
	if (sc->aac_max_fib_size > PAGE_SIZE)
		sc->aac_max_fib_size = PAGE_SIZE;
	sc->aac_max_fibs_alloc = PAGE_SIZE / sc->aac_max_fib_size;

	if (sc->aac_max_fib_size > sizeof(struct aac_fib)) {
		sc->flags |= AAC_FLAGS_RAW_IO;
		device_printf(sc->aac_dev, "Enable Raw I/O\n");
	}
	if ((sc->flags & AAC_FLAGS_RAW_IO) &&
	    (sc->flags & AAC_FLAGS_ARRAY_64BIT)) {
		sc->flags |= AAC_FLAGS_LBA_64BIT;
		device_printf(sc->aac_dev, "Enable 64-bit array\n");
	}

	return (0);
}

static int
aac_init(struct aac_softc *sc)
{
	struct aac_adapter_init	*ip;
	u_int32_t qoffset;
	int error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/*
	 * Fill in the init structure.  This tells the adapter about the
	 * physical location of various important shared data structures.
	 */
	ip = &sc->aac_common->ac_init;
	ip->InitStructRevision = AAC_INIT_STRUCT_REVISION;
	if (sc->aac_max_fib_size > sizeof(struct aac_fib)) {
		ip->InitStructRevision = AAC_INIT_STRUCT_REVISION_4;
		sc->flags |= AAC_FLAGS_RAW_IO;
	}
	ip->MiniPortRevision = AAC_INIT_STRUCT_MINIPORT_REVISION;

	ip->AdapterFibsPhysicalAddress = sc->aac_common_busaddr +
					 offsetof(struct aac_common, ac_fibs);
	ip->AdapterFibsVirtualAddress = 0;
	ip->AdapterFibsSize = AAC_ADAPTER_FIBS * sizeof(struct aac_fib);
	ip->AdapterFibAlign = sizeof(struct aac_fib);

	ip->PrintfBufferAddress = sc->aac_common_busaddr +
				  offsetof(struct aac_common, ac_printf);
	ip->PrintfBufferSize = AAC_PRINTF_BUFSIZE;

	/*
	 * The adapter assumes that pages are 4K in size, except on some
 	 * broken firmware versions that do the page->byte conversion twice,
	 * therefore 'assuming' that this value is in 16MB units (2^24).
	 * Round up since the granularity is so high.
	 */
	ip->HostPhysMemPages = ctob(physmem) / AAC_PAGE_SIZE;
	if (sc->flags & AAC_FLAGS_BROKEN_MEMMAP) {
		ip->HostPhysMemPages =
		    (ip->HostPhysMemPages + AAC_PAGE_SIZE) / AAC_PAGE_SIZE;
	}
	ip->HostElapsedSeconds = time_uptime;	/* reset later if invalid */

	ip->InitFlags = 0;
	if (sc->flags & AAC_FLAGS_NEW_COMM) {
		ip->InitFlags |= AAC_INITFLAGS_NEW_COMM_SUPPORTED;
		device_printf(sc->aac_dev, "New comm. interface enabled\n");
	}

	ip->MaxIoCommands = sc->aac_max_fibs;
	ip->MaxIoSize = sc->aac_max_sectors << 9;
	ip->MaxFibSize = sc->aac_max_fib_size;

	/*
	 * Initialize FIB queues.  Note that it appears that the layout of the
	 * indexes and the segmentation of the entries may be mandated by the
	 * adapter, which is only told about the base of the queue index fields.
	 *
	 * The initial values of the indices are assumed to inform the adapter
	 * of the sizes of the respective queues, and theoretically it could
	 * work out the entire layout of the queue structures from this.  We
	 * take the easy route and just lay this area out like everyone else
	 * does.
	 *
	 * The Linux driver uses a much more complex scheme whereby several
	 * header records are kept for each queue.  We use a couple of generic
	 * list manipulation functions which 'know' the size of each list by
	 * virtue of a table.
	 */
	qoffset = offsetof(struct aac_common, ac_qbuf) + AAC_QUEUE_ALIGN;
	qoffset &= ~(AAC_QUEUE_ALIGN - 1);
	sc->aac_queues =
	    (struct aac_queue_table *)((uintptr_t)sc->aac_common + qoffset);
	ip->CommHeaderAddress = sc->aac_common_busaddr + qoffset;

	sc->aac_queues->qt_qindex[AAC_HOST_NORM_CMD_QUEUE][AAC_PRODUCER_INDEX] =
		AAC_HOST_NORM_CMD_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_HOST_NORM_CMD_QUEUE][AAC_CONSUMER_INDEX] =
		AAC_HOST_NORM_CMD_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_HOST_HIGH_CMD_QUEUE][AAC_PRODUCER_INDEX] =
		AAC_HOST_HIGH_CMD_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_HOST_HIGH_CMD_QUEUE][AAC_CONSUMER_INDEX] =
		AAC_HOST_HIGH_CMD_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_ADAP_NORM_CMD_QUEUE][AAC_PRODUCER_INDEX] =
		AAC_ADAP_NORM_CMD_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_ADAP_NORM_CMD_QUEUE][AAC_CONSUMER_INDEX] =
		AAC_ADAP_NORM_CMD_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_ADAP_HIGH_CMD_QUEUE][AAC_PRODUCER_INDEX] =
		AAC_ADAP_HIGH_CMD_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_ADAP_HIGH_CMD_QUEUE][AAC_CONSUMER_INDEX] =
		AAC_ADAP_HIGH_CMD_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_HOST_NORM_RESP_QUEUE][AAC_PRODUCER_INDEX]=
		AAC_HOST_NORM_RESP_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_HOST_NORM_RESP_QUEUE][AAC_CONSUMER_INDEX]=
		AAC_HOST_NORM_RESP_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_HOST_HIGH_RESP_QUEUE][AAC_PRODUCER_INDEX]=
		AAC_HOST_HIGH_RESP_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_HOST_HIGH_RESP_QUEUE][AAC_CONSUMER_INDEX]=
		AAC_HOST_HIGH_RESP_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_ADAP_NORM_RESP_QUEUE][AAC_PRODUCER_INDEX]=
		AAC_ADAP_NORM_RESP_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_ADAP_NORM_RESP_QUEUE][AAC_CONSUMER_INDEX]=
		AAC_ADAP_NORM_RESP_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_ADAP_HIGH_RESP_QUEUE][AAC_PRODUCER_INDEX]=
		AAC_ADAP_HIGH_RESP_ENTRIES;
	sc->aac_queues->qt_qindex[AAC_ADAP_HIGH_RESP_QUEUE][AAC_CONSUMER_INDEX]=
		AAC_ADAP_HIGH_RESP_ENTRIES;
	sc->aac_qentries[AAC_HOST_NORM_CMD_QUEUE] =
		&sc->aac_queues->qt_HostNormCmdQueue[0];
	sc->aac_qentries[AAC_HOST_HIGH_CMD_QUEUE] =
		&sc->aac_queues->qt_HostHighCmdQueue[0];
	sc->aac_qentries[AAC_ADAP_NORM_CMD_QUEUE] =
		&sc->aac_queues->qt_AdapNormCmdQueue[0];
	sc->aac_qentries[AAC_ADAP_HIGH_CMD_QUEUE] =
		&sc->aac_queues->qt_AdapHighCmdQueue[0];
	sc->aac_qentries[AAC_HOST_NORM_RESP_QUEUE] =
		&sc->aac_queues->qt_HostNormRespQueue[0];
	sc->aac_qentries[AAC_HOST_HIGH_RESP_QUEUE] =
		&sc->aac_queues->qt_HostHighRespQueue[0];
	sc->aac_qentries[AAC_ADAP_NORM_RESP_QUEUE] =
		&sc->aac_queues->qt_AdapNormRespQueue[0];
	sc->aac_qentries[AAC_ADAP_HIGH_RESP_QUEUE] =
		&sc->aac_queues->qt_AdapHighRespQueue[0];

	/*
	 * Do controller-type-specific initialisation
	 */
	switch (sc->aac_hwif) {
	case AAC_HWIF_I960RX:
		AAC_MEM0_SETREG4(sc, AAC_RX_ODBR, ~0);
		break;
	case AAC_HWIF_RKT:
		AAC_MEM0_SETREG4(sc, AAC_RKT_ODBR, ~0);
		break;
	default:
		break;
	}

	/*
	 * Give the init structure to the controller.
	 */
	if (aac_sync_command(sc, AAC_MONKER_INITSTRUCT,
			     sc->aac_common_busaddr +
			     offsetof(struct aac_common, ac_init), 0, 0, 0,
			     NULL)) {
		device_printf(sc->aac_dev,
			      "error establishing init structure\n");
		error = EIO;
		goto out;
	}

	error = 0;
out:
	return(error);
}

static int
aac_setup_intr(struct aac_softc *sc)
{

	if (sc->flags & AAC_FLAGS_NEW_COMM) {
		if (bus_setup_intr(sc->aac_dev, sc->aac_irq,
				   INTR_MPSAFE|INTR_TYPE_BIO, NULL,
				   aac_new_intr, sc, &sc->aac_intr)) {
			device_printf(sc->aac_dev, "can't set up interrupt\n");
			return (EINVAL);
		}
	} else {
		if (bus_setup_intr(sc->aac_dev, sc->aac_irq,
				   INTR_TYPE_BIO, aac_filter, NULL,
				   sc, &sc->aac_intr)) {
			device_printf(sc->aac_dev,
				      "can't set up interrupt filter\n");
			return (EINVAL);
		}
	}
	return (0);
}

/*
 * Send a synchronous command to the controller and wait for a result.
 * Indicate if the controller completed the command with an error status.
 */
static int
aac_sync_command(struct aac_softc *sc, u_int32_t command,
		 u_int32_t arg0, u_int32_t arg1, u_int32_t arg2, u_int32_t arg3,
		 u_int32_t *sp)
{
	time_t then;
	u_int32_t status;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* populate the mailbox */
	AAC_SET_MAILBOX(sc, command, arg0, arg1, arg2, arg3);

	/* ensure the sync command doorbell flag is cleared */
	AAC_CLEAR_ISTATUS(sc, AAC_DB_SYNC_COMMAND);

	/* then set it to signal the adapter */
	AAC_QNOTIFY(sc, AAC_DB_SYNC_COMMAND);

	/* spin waiting for the command to complete */
	then = time_uptime;
	do {
		if (time_uptime > (then + AAC_IMMEDIATE_TIMEOUT)) {
			fwprintf(sc, HBA_FLAGS_DBG_ERROR_B, "timed out");
			return(EIO);
		}
	} while (!(AAC_GET_ISTATUS(sc) & AAC_DB_SYNC_COMMAND));

	/* clear the completion flag */
	AAC_CLEAR_ISTATUS(sc, AAC_DB_SYNC_COMMAND);

	/* get the command status */
	status = AAC_GET_MAILBOX(sc, 0);
	if (sp != NULL)
		*sp = status;

	if (status != AAC_SRB_STS_SUCCESS)
		return (-1);
	return(0);
}

int
aac_sync_fib(struct aac_softc *sc, u_int32_t command, u_int32_t xferstate,
		 struct aac_fib *fib, u_int16_t datasize)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	if (datasize > AAC_FIB_DATASIZE)
		return(EINVAL);

	/*
	 * Set up the sync FIB
	 */
	fib->Header.XferState = AAC_FIBSTATE_HOSTOWNED |
				AAC_FIBSTATE_INITIALISED |
				AAC_FIBSTATE_EMPTY;
	fib->Header.XferState |= xferstate;
	fib->Header.Command = command;
	fib->Header.StructType = AAC_FIBTYPE_TFIB;
	fib->Header.Size = sizeof(struct aac_fib_header) + datasize;
	fib->Header.SenderSize = sizeof(struct aac_fib);
	fib->Header.SenderFibAddress = 0;	/* Not needed */
	fib->Header.ReceiverFibAddress = sc->aac_common_busaddr +
					 offsetof(struct aac_common,
						  ac_sync_fib);

	/*
	 * Give the FIB to the controller, wait for a response.
	 */
	if (aac_sync_command(sc, AAC_MONKER_SYNCFIB,
			     fib->Header.ReceiverFibAddress, 0, 0, 0, NULL)) {
		fwprintf(sc, HBA_FLAGS_DBG_ERROR_B, "IO error");
		return(EIO);
	}

	return (0);
}

/*
 * Adapter-space FIB queue manipulation
 *
 * Note that the queue implementation here is a little funky; neither the PI or
 * CI will ever be zero.  This behaviour is a controller feature.
 */
static const struct {
	int		size;
	int		notify;
} aac_qinfo[] = {
	{AAC_HOST_NORM_CMD_ENTRIES, AAC_DB_COMMAND_NOT_FULL},
	{AAC_HOST_HIGH_CMD_ENTRIES, 0},
	{AAC_ADAP_NORM_CMD_ENTRIES, AAC_DB_COMMAND_READY},
	{AAC_ADAP_HIGH_CMD_ENTRIES, 0},
	{AAC_HOST_NORM_RESP_ENTRIES, AAC_DB_RESPONSE_NOT_FULL},
	{AAC_HOST_HIGH_RESP_ENTRIES, 0},
	{AAC_ADAP_NORM_RESP_ENTRIES, AAC_DB_RESPONSE_READY},
	{AAC_ADAP_HIGH_RESP_ENTRIES, 0}
};

/*
 * Atomically insert an entry into the nominated queue, returns 0 on success or
 * EBUSY if the queue is full.
 *
 * Note: it would be more efficient to defer notifying the controller in
 *	 the case where we may be inserting several entries in rapid succession,
 *	 but implementing this usefully may be difficult (it would involve a
 *	 separate queue/notify interface).
 */
static int
aac_enqueue_fib(struct aac_softc *sc, int queue, struct aac_command *cm)
{
	u_int32_t pi, ci;
	int error;
	u_int32_t fib_size;
	u_int32_t fib_addr;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	fib_size = cm->cm_fib->Header.Size;
	fib_addr = cm->cm_fib->Header.ReceiverFibAddress;

	/* get the producer/consumer indices */
	pi = sc->aac_queues->qt_qindex[queue][AAC_PRODUCER_INDEX];
	ci = sc->aac_queues->qt_qindex[queue][AAC_CONSUMER_INDEX];

	/* wrap the queue? */
	if (pi >= aac_qinfo[queue].size)
		pi = 0;

	/* check for queue full */
	if ((pi + 1) == ci) {
		error = EBUSY;
		goto out;
	}

	/*
	 * To avoid a race with its completion interrupt, place this command on
	 * the busy queue prior to advertising it to the controller.
	 */
	aac_enqueue_busy(cm);

	/* populate queue entry */
	(sc->aac_qentries[queue] + pi)->aq_fib_size = fib_size;
	(sc->aac_qentries[queue] + pi)->aq_fib_addr = fib_addr;

	/* update producer index */
	sc->aac_queues->qt_qindex[queue][AAC_PRODUCER_INDEX] = pi + 1;

	/* notify the adapter if we know how */
	if (aac_qinfo[queue].notify != 0)
		AAC_QNOTIFY(sc, aac_qinfo[queue].notify);

	error = 0;

out:
	return(error);
}

/*
 * Atomically remove one entry from the nominated queue, returns 0 on
 * success or ENOENT if the queue is empty.
 */
static int
aac_dequeue_fib(struct aac_softc *sc, int queue, u_int32_t *fib_size,
		struct aac_fib **fib_addr)
{
	u_int32_t pi, ci;
	u_int32_t fib_index;
	int error;
	int notify;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* get the producer/consumer indices */
	pi = sc->aac_queues->qt_qindex[queue][AAC_PRODUCER_INDEX];
	ci = sc->aac_queues->qt_qindex[queue][AAC_CONSUMER_INDEX];

	/* check for queue empty */
	if (ci == pi) {
		error = ENOENT;
		goto out;
	}

	/* wrap the pi so the following test works */
	if (pi >= aac_qinfo[queue].size)
		pi = 0;

	notify = 0;
	if (ci == pi + 1)
		notify++;

	/* wrap the queue? */
	if (ci >= aac_qinfo[queue].size)
		ci = 0;

	/* fetch the entry */
	*fib_size = (sc->aac_qentries[queue] + ci)->aq_fib_size;

	switch (queue) {
	case AAC_HOST_NORM_CMD_QUEUE:
	case AAC_HOST_HIGH_CMD_QUEUE:
		/*
		 * The aq_fib_addr is only 32 bits wide so it can't be counted
		 * on to hold an address.  For AIF's, the adapter assumes
		 * that it's giving us an address into the array of AIF fibs.
		 * Therefore, we have to convert it to an index.
		 */
		fib_index = (sc->aac_qentries[queue] + ci)->aq_fib_addr /
			sizeof(struct aac_fib);
		*fib_addr = &sc->aac_common->ac_fibs[fib_index];
		break;

	case AAC_HOST_NORM_RESP_QUEUE:
	case AAC_HOST_HIGH_RESP_QUEUE:
	{
		struct aac_command *cm;

		/*
		 * As above, an index is used instead of an actual address.
		 * Gotta shift the index to account for the fast response
		 * bit.  No other correction is needed since this value was
		 * originally provided by the driver via the SenderFibAddress
		 * field.
		 */
		fib_index = (sc->aac_qentries[queue] + ci)->aq_fib_addr;
		cm = sc->aac_commands + (fib_index >> 2);
		*fib_addr = cm->cm_fib;

		/*
		 * Is this a fast response? If it is, update the fib fields in
		 * local memory since the whole fib isn't DMA'd back up.
		 */
		if (fib_index & 0x01) {
			(*fib_addr)->Header.XferState |= AAC_FIBSTATE_DONEADAP;
			*((u_int32_t*)((*fib_addr)->data)) = AAC_ERROR_NORMAL;
		}
		break;
	}
	default:
		panic("Invalid queue in aac_dequeue_fib()");
		break;
	}

	/* update consumer index */
	sc->aac_queues->qt_qindex[queue][AAC_CONSUMER_INDEX] = ci + 1;

	/* if we have made the queue un-full, notify the adapter */
	if (notify && (aac_qinfo[queue].notify != 0))
		AAC_QNOTIFY(sc, aac_qinfo[queue].notify);
	error = 0;

out:
	return(error);
}

/*
 * Put our response to an Adapter Initialed Fib on the response queue
 */
static int
aac_enqueue_response(struct aac_softc *sc, int queue, struct aac_fib *fib)
{
	u_int32_t pi, ci;
	int error;
	u_int32_t fib_size;
	u_int32_t fib_addr;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* Tell the adapter where the FIB is */
	fib_size = fib->Header.Size;
	fib_addr = fib->Header.SenderFibAddress;
	fib->Header.ReceiverFibAddress = fib_addr;

	/* get the producer/consumer indices */
	pi = sc->aac_queues->qt_qindex[queue][AAC_PRODUCER_INDEX];
	ci = sc->aac_queues->qt_qindex[queue][AAC_CONSUMER_INDEX];

	/* wrap the queue? */
	if (pi >= aac_qinfo[queue].size)
		pi = 0;

	/* check for queue full */
	if ((pi + 1) == ci) {
		error = EBUSY;
		goto out;
	}

	/* populate queue entry */
	(sc->aac_qentries[queue] + pi)->aq_fib_size = fib_size;
	(sc->aac_qentries[queue] + pi)->aq_fib_addr = fib_addr;

	/* update producer index */
	sc->aac_queues->qt_qindex[queue][AAC_PRODUCER_INDEX] = pi + 1;

	/* notify the adapter if we know how */
	if (aac_qinfo[queue].notify != 0)
		AAC_QNOTIFY(sc, aac_qinfo[queue].notify);

	error = 0;

out:
	return(error);
}

/*
 * Check for commands that have been outstanding for a suspiciously long time,
 * and complain about them.
 */
static void
aac_timeout(struct aac_softc *sc)
{
	struct aac_command *cm;
	time_t deadline;
	int timedout, code;

	/*
	 * Traverse the busy command list, bitch about late commands once
	 * only.
	 */
	timedout = 0;
	deadline = time_uptime - AAC_CMD_TIMEOUT;
	TAILQ_FOREACH(cm, &sc->aac_busy, cm_link) {
		if ((cm->cm_timestamp  < deadline)
		    && !(cm->cm_flags & AAC_CMD_TIMEDOUT)) {
			cm->cm_flags |= AAC_CMD_TIMEDOUT;
			device_printf(sc->aac_dev,
			    "COMMAND %p (TYPE %d) TIMEOUT AFTER %d SECONDS\n",
			    cm, cm->cm_fib->Header.Command,
			    (int)(time_uptime-cm->cm_timestamp));
			AAC_PRINT_FIB(sc, cm->cm_fib);
			timedout++;
		}
	}

	if (timedout) {
		code = AAC_GET_FWSTATUS(sc);
		if (code != AAC_UP_AND_RUNNING) {
			device_printf(sc->aac_dev, "WARNING! Controller is no "
				      "longer running! code= 0x%x\n", code);
		}
	}
}

/*
 * Interface Function Vectors
 */

/*
 * Read the current firmware status word.
 */
static int
aac_sa_get_fwstatus(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, AAC_SA_FWSTATUS));
}

static int
aac_rx_get_fwstatus(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, sc->flags & AAC_FLAGS_NEW_COMM ?
	    AAC_RX_OMR0 : AAC_RX_FWSTATUS));
}

static int
aac_rkt_get_fwstatus(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, sc->flags & AAC_FLAGS_NEW_COMM ?
	    AAC_RKT_OMR0 : AAC_RKT_FWSTATUS));
}

/*
 * Notify the controller of a change in a given queue
 */

static void
aac_sa_qnotify(struct aac_softc *sc, int qbit)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG2(sc, AAC_SA_DOORBELL1_SET, qbit);
}

static void
aac_rx_qnotify(struct aac_softc *sc, int qbit)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG4(sc, AAC_RX_IDBR, qbit);
}

static void
aac_rkt_qnotify(struct aac_softc *sc, int qbit)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG4(sc, AAC_RKT_IDBR, qbit);
}

/*
 * Get the interrupt reason bits
 */
static int
aac_sa_get_istatus(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG2(sc, AAC_SA_DOORBELL0));
}

static int
aac_rx_get_istatus(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, AAC_RX_ODBR));
}

static int
aac_rkt_get_istatus(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, AAC_RKT_ODBR));
}

/*
 * Clear some interrupt reason bits
 */
static void
aac_sa_clear_istatus(struct aac_softc *sc, int mask)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG2(sc, AAC_SA_DOORBELL0_CLEAR, mask);
}

static void
aac_rx_clear_istatus(struct aac_softc *sc, int mask)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG4(sc, AAC_RX_ODBR, mask);
}

static void
aac_rkt_clear_istatus(struct aac_softc *sc, int mask)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG4(sc, AAC_RKT_ODBR, mask);
}

/*
 * Populate the mailbox and set the command word
 */
static void
aac_sa_set_mailbox(struct aac_softc *sc, u_int32_t command,
		u_int32_t arg0, u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM1_SETREG4(sc, AAC_SA_MAILBOX, command);
	AAC_MEM1_SETREG4(sc, AAC_SA_MAILBOX + 4, arg0);
	AAC_MEM1_SETREG4(sc, AAC_SA_MAILBOX + 8, arg1);
	AAC_MEM1_SETREG4(sc, AAC_SA_MAILBOX + 12, arg2);
	AAC_MEM1_SETREG4(sc, AAC_SA_MAILBOX + 16, arg3);
}

static void
aac_rx_set_mailbox(struct aac_softc *sc, u_int32_t command,
		u_int32_t arg0, u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM1_SETREG4(sc, AAC_RX_MAILBOX, command);
	AAC_MEM1_SETREG4(sc, AAC_RX_MAILBOX + 4, arg0);
	AAC_MEM1_SETREG4(sc, AAC_RX_MAILBOX + 8, arg1);
	AAC_MEM1_SETREG4(sc, AAC_RX_MAILBOX + 12, arg2);
	AAC_MEM1_SETREG4(sc, AAC_RX_MAILBOX + 16, arg3);
}

static void
aac_rkt_set_mailbox(struct aac_softc *sc, u_int32_t command, u_int32_t arg0,
		    u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM1_SETREG4(sc, AAC_RKT_MAILBOX, command);
	AAC_MEM1_SETREG4(sc, AAC_RKT_MAILBOX + 4, arg0);
	AAC_MEM1_SETREG4(sc, AAC_RKT_MAILBOX + 8, arg1);
	AAC_MEM1_SETREG4(sc, AAC_RKT_MAILBOX + 12, arg2);
	AAC_MEM1_SETREG4(sc, AAC_RKT_MAILBOX + 16, arg3);
}

/*
 * Fetch the immediate command status word
 */
static int
aac_sa_get_mailbox(struct aac_softc *sc, int mb)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM1_GETREG4(sc, AAC_SA_MAILBOX + (mb * 4)));
}

static int
aac_rx_get_mailbox(struct aac_softc *sc, int mb)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM1_GETREG4(sc, AAC_RX_MAILBOX + (mb * 4)));
}

static int
aac_rkt_get_mailbox(struct aac_softc *sc, int mb)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM1_GETREG4(sc, AAC_RKT_MAILBOX + (mb * 4)));
}

/*
 * Set/clear interrupt masks
 */
static void
aac_sa_set_interrupts(struct aac_softc *sc, int enable)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "%sable interrupts", enable ? "en" : "dis");

	if (enable) {
		AAC_MEM0_SETREG2((sc), AAC_SA_MASK0_CLEAR, AAC_DB_INTERRUPTS);
	} else {
		AAC_MEM0_SETREG2((sc), AAC_SA_MASK0_SET, ~0);
	}
}

static void
aac_rx_set_interrupts(struct aac_softc *sc, int enable)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "%sable interrupts", enable ? "en" : "dis");

	if (enable) {
		if (sc->flags & AAC_FLAGS_NEW_COMM)
			AAC_MEM0_SETREG4(sc, AAC_RX_OIMR, ~AAC_DB_INT_NEW_COMM);
		else
			AAC_MEM0_SETREG4(sc, AAC_RX_OIMR, ~AAC_DB_INTERRUPTS);
	} else {
		AAC_MEM0_SETREG4(sc, AAC_RX_OIMR, ~0);
	}
}

static void
aac_rkt_set_interrupts(struct aac_softc *sc, int enable)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "%sable interrupts", enable ? "en" : "dis");

	if (enable) {
		if (sc->flags & AAC_FLAGS_NEW_COMM)
			AAC_MEM0_SETREG4(sc, AAC_RKT_OIMR, ~AAC_DB_INT_NEW_COMM);
		else
			AAC_MEM0_SETREG4(sc, AAC_RKT_OIMR, ~AAC_DB_INTERRUPTS);
	} else {
		AAC_MEM0_SETREG4(sc, AAC_RKT_OIMR, ~0);
	}
}

/*
 * New comm. interface: Send command functions
 */
static int
aac_rx_send_command(struct aac_softc *sc, struct aac_command *cm)
{
	u_int32_t index, device;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "send command (new comm.)");

	index = AAC_MEM0_GETREG4(sc, AAC_RX_IQUE);
	if (index == 0xffffffffL)
		index = AAC_MEM0_GETREG4(sc, AAC_RX_IQUE);
	if (index == 0xffffffffL)
		return index;
	aac_enqueue_busy(cm);
	device = index;
	AAC_MEM1_SETREG4(sc, device, (u_int32_t)(cm->cm_fibphys & 0xffffffffUL));
	device += 4;
	AAC_MEM1_SETREG4(sc, device, (u_int32_t)(cm->cm_fibphys >> 32));
	device += 4;
	AAC_MEM1_SETREG4(sc, device, cm->cm_fib->Header.Size);
	AAC_MEM0_SETREG4(sc, AAC_RX_IQUE, index);
	return 0;
}

static int
aac_rkt_send_command(struct aac_softc *sc, struct aac_command *cm)
{
	u_int32_t index, device;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "send command (new comm.)");

	index = AAC_MEM0_GETREG4(sc, AAC_RKT_IQUE);
	if (index == 0xffffffffL)
		index = AAC_MEM0_GETREG4(sc, AAC_RKT_IQUE);
	if (index == 0xffffffffL)
		return index;
	aac_enqueue_busy(cm);
	device = index;
	AAC_MEM1_SETREG4(sc, device, (u_int32_t)(cm->cm_fibphys & 0xffffffffUL));
	device += 4;
	AAC_MEM1_SETREG4(sc, device, (u_int32_t)(cm->cm_fibphys >> 32));
	device += 4;
	AAC_MEM1_SETREG4(sc, device, cm->cm_fib->Header.Size);
	AAC_MEM0_SETREG4(sc, AAC_RKT_IQUE, index);
	return 0;
}

/*
 * New comm. interface: get, set outbound queue index
 */
static int
aac_rx_get_outb_queue(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, AAC_RX_OQUE));
}

static int
aac_rkt_get_outb_queue(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, AAC_RKT_OQUE));
}

static void
aac_rx_set_outb_queue(struct aac_softc *sc, int index)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG4(sc, AAC_RX_OQUE, index);
}

static void
aac_rkt_set_outb_queue(struct aac_softc *sc, int index)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG4(sc, AAC_RKT_OQUE, index);
}

/*
 * Debugging and Diagnostics
 */

/*
 * Print some information about the controller.
 */
static void
aac_describe_controller(struct aac_softc *sc)
{
	struct aac_fib *fib;
	struct aac_adapter_info	*info;
	char *adapter_type = "Adaptec RAID controller";

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	mtx_lock(&sc->aac_io_lock);
	aac_alloc_sync_fib(sc, &fib);

	fib->data[0] = 0;
	if (aac_sync_fib(sc, RequestAdapterInfo, 0, fib, 1)) {
		device_printf(sc->aac_dev, "RequestAdapterInfo failed\n");
		aac_release_sync_fib(sc);
		mtx_unlock(&sc->aac_io_lock);
		return;
	}

	/* save the kernel revision structure for later use */
	info = (struct aac_adapter_info *)&fib->data[0];
	sc->aac_revision = info->KernelRevision;

	if (bootverbose) {
		device_printf(sc->aac_dev, "%s %dMHz, %dMB memory "
		    "(%dMB cache, %dMB execution), %s\n",
		    aac_describe_code(aac_cpu_variant, info->CpuVariant),
		    info->ClockSpeed, info->TotalMem / (1024 * 1024),
		    info->BufferMem / (1024 * 1024),
		    info->ExecutionMem / (1024 * 1024),
		    aac_describe_code(aac_battery_platform,
		    info->batteryPlatform));

		device_printf(sc->aac_dev,
		    "Kernel %d.%d-%d, Build %d, S/N %6X\n",
		    info->KernelRevision.external.comp.major,
		    info->KernelRevision.external.comp.minor,
		    info->KernelRevision.external.comp.dash,
		    info->KernelRevision.buildNumber,
		    (u_int32_t)(info->SerialNumber & 0xffffff));

		device_printf(sc->aac_dev, "Supported Options=%b\n",
			      sc->supported_options,
			      "\20"
			      "\1SNAPSHOT"
			      "\2CLUSTERS"
			      "\3WCACHE"
			      "\4DATA64"
			      "\5HOSTTIME"
			      "\6RAID50"
			      "\7WINDOW4GB"
			      "\10SCSIUPGD"
			      "\11SOFTERR"
			      "\12NORECOND"
			      "\13SGMAP64"
			      "\14ALARM"
			      "\15NONDASD"
			      "\16SCSIMGT"
			      "\17RAIDSCSI"
			      "\21ADPTINFO"
			      "\22NEWCOMM"
			      "\23ARRAY64BIT"
			      "\24HEATSENSOR");
	}

	if (sc->supported_options & AAC_SUPPORTED_SUPPLEMENT_ADAPTER_INFO) {
		fib->data[0] = 0;
		if (aac_sync_fib(sc, RequestSupplementAdapterInfo, 0, fib, 1))
			device_printf(sc->aac_dev,
			    "RequestSupplementAdapterInfo failed\n");
		else
			adapter_type = ((struct aac_supplement_adapter_info *)
			    &fib->data[0])->AdapterTypeText;
	}
	device_printf(sc->aac_dev, "%s, aac driver %d.%d.%d-%d\n",
		adapter_type,
		AAC_DRIVER_MAJOR_VERSION, AAC_DRIVER_MINOR_VERSION,
		AAC_DRIVER_BUGFIX_LEVEL, AAC_DRIVER_BUILD);

	aac_release_sync_fib(sc);
	mtx_unlock(&sc->aac_io_lock);
}

/*
 * Look up a text description of a numeric error code and return a pointer to
 * same.
 */
static const char *
aac_describe_code(const struct aac_code_lookup *table, u_int32_t code)
{
	int i;

	for (i = 0; table[i].string != NULL; i++)
		if (table[i].code == code)
			return(table[i].string);
	return(table[i + 1].string);
}

/*
 * Management Interface
 */

static int
aac_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct aac_softc *sc;

	sc = dev->si_drv1;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	device_busy(sc->aac_dev);
	devfs_set_cdevpriv(sc, aac_cdevpriv_dtor);

	return 0;
}

static int
aac_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int flag, struct thread *td)
{
	union aac_statrequest *as;
	struct aac_softc *sc;
	int error = 0;

	as = (union aac_statrequest *)arg;
	sc = dev->si_drv1;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	switch (cmd) {
	case AACIO_STATS:
		switch (as->as_item) {
		case AACQ_FREE:
		case AACQ_BIO:
		case AACQ_READY:
		case AACQ_BUSY:
			bcopy(&sc->aac_qstat[as->as_item], &as->as_qstat,
			      sizeof(struct aac_qstat));
			break;
		default:
			error = ENOENT;
			break;
		}
	break;

	case FSACTL_SENDFIB:
	case FSACTL_SEND_LARGE_FIB:
		arg = *(caddr_t*)arg;
	case FSACTL_LNX_SENDFIB:
	case FSACTL_LNX_SEND_LARGE_FIB:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_SENDFIB");
		error = aac_ioctl_sendfib(sc, arg);
		break;
	case FSACTL_SEND_RAW_SRB:
		arg = *(caddr_t*)arg;
	case FSACTL_LNX_SEND_RAW_SRB:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_SEND_RAW_SRB");
		error = aac_ioctl_send_raw_srb(sc, arg);
		break;
	case FSACTL_AIF_THREAD:
	case FSACTL_LNX_AIF_THREAD:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_AIF_THREAD");
		error = EINVAL;
		break;
	case FSACTL_OPEN_GET_ADAPTER_FIB:
		arg = *(caddr_t*)arg;
	case FSACTL_LNX_OPEN_GET_ADAPTER_FIB:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_OPEN_GET_ADAPTER_FIB");
		error = aac_open_aif(sc, arg);
		break;
	case FSACTL_GET_NEXT_ADAPTER_FIB:
		arg = *(caddr_t*)arg;
	case FSACTL_LNX_GET_NEXT_ADAPTER_FIB:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_GET_NEXT_ADAPTER_FIB");
		error = aac_getnext_aif(sc, arg);
		break;
	case FSACTL_CLOSE_GET_ADAPTER_FIB:
		arg = *(caddr_t*)arg;
	case FSACTL_LNX_CLOSE_GET_ADAPTER_FIB:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_CLOSE_GET_ADAPTER_FIB");
		error = aac_close_aif(sc, arg);
		break;
	case FSACTL_MINIPORT_REV_CHECK:
		arg = *(caddr_t*)arg;
	case FSACTL_LNX_MINIPORT_REV_CHECK:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_MINIPORT_REV_CHECK");
		error = aac_rev_check(sc, arg);
		break;
	case FSACTL_QUERY_DISK:
		arg = *(caddr_t*)arg;
	case FSACTL_LNX_QUERY_DISK:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_QUERY_DISK");
		error = aac_query_disk(sc, arg);
		break;
	case FSACTL_DELETE_DISK:
	case FSACTL_LNX_DELETE_DISK:
		/*
		 * We don't trust the underland to tell us when to delete a
		 * container, rather we rely on an AIF coming from the
		 * controller
		 */
		error = 0;
		break;
	case FSACTL_GET_PCI_INFO:
		arg = *(caddr_t*)arg;
	case FSACTL_LNX_GET_PCI_INFO:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_GET_PCI_INFO");
		error = aac_get_pci_info(sc, arg);
		break;
	case FSACTL_GET_FEATURES:
		arg = *(caddr_t*)arg;
	case FSACTL_LNX_GET_FEATURES:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "FSACTL_GET_FEATURES");
		error = aac_supported_features(sc, arg);
		break;
	default:
		fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "unsupported cmd 0x%lx\n", cmd);
		error = EINVAL;
		break;
	}
	return(error);
}

static int
aac_poll(struct cdev *dev, int poll_events, struct thread *td)
{
	struct aac_softc *sc;
	struct aac_fib_context *ctx;
	int revents;

	sc = dev->si_drv1;
	revents = 0;

	mtx_lock(&sc->aac_aifq_lock);
	if ((poll_events & (POLLRDNORM | POLLIN)) != 0) {
		for (ctx = sc->fibctx; ctx; ctx = ctx->next) {
			if (ctx->ctx_idx != sc->aifq_idx || ctx->ctx_wrap) {
				revents |= poll_events & (POLLIN | POLLRDNORM);
				break;
			}
		}
	}
	mtx_unlock(&sc->aac_aifq_lock);

	if (revents == 0) {
		if (poll_events & (POLLIN | POLLRDNORM))
			selrecord(td, &sc->rcv_select);
	}

	return (revents);
}

static void
aac_ioctl_event(struct aac_softc *sc, struct aac_event *event, void *arg)
{

	switch (event->ev_type) {
	case AAC_EVENT_CMFREE:
		mtx_assert(&sc->aac_io_lock, MA_OWNED);
		if (aac_alloc_command(sc, (struct aac_command **)arg)) {
			aac_add_event(sc, event);
			return;
		}
		free(event, M_AACBUF);
		wakeup(arg);
		break;
	default:
		break;
	}
}

/*
 * Send a FIB supplied from userspace
 */
static int
aac_ioctl_sendfib(struct aac_softc *sc, caddr_t ufib)
{
	struct aac_command *cm;
	int size, error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	cm = NULL;

	/*
	 * Get a command
	 */
	mtx_lock(&sc->aac_io_lock);
	if (aac_alloc_command(sc, &cm)) {
		struct aac_event *event;

		event = malloc(sizeof(struct aac_event), M_AACBUF,
		    M_NOWAIT | M_ZERO);
		if (event == NULL) {
			error = EBUSY;
			mtx_unlock(&sc->aac_io_lock);
			goto out;
		}
		event->ev_type = AAC_EVENT_CMFREE;
		event->ev_callback = aac_ioctl_event;
		event->ev_arg = &cm;
		aac_add_event(sc, event);
		msleep(&cm, &sc->aac_io_lock, 0, "sendfib", 0);
	}
	mtx_unlock(&sc->aac_io_lock);

	/*
	 * Fetch the FIB header, then re-copy to get data as well.
	 */
	if ((error = copyin(ufib, cm->cm_fib,
			    sizeof(struct aac_fib_header))) != 0)
		goto out;
	size = cm->cm_fib->Header.Size + sizeof(struct aac_fib_header);
	if (size > sc->aac_max_fib_size) {
		device_printf(sc->aac_dev, "incoming FIB oversized (%d > %d)\n",
			      size, sc->aac_max_fib_size);
		size = sc->aac_max_fib_size;
	}
	if ((error = copyin(ufib, cm->cm_fib, size)) != 0)
		goto out;
	cm->cm_fib->Header.Size = size;
	cm->cm_timestamp = time_uptime;

	/*
	 * Pass the FIB to the controller, wait for it to complete.
	 */
	mtx_lock(&sc->aac_io_lock);
	error = aac_wait_command(cm);
	mtx_unlock(&sc->aac_io_lock);
	if (error != 0) {
		device_printf(sc->aac_dev,
			      "aac_wait_command return %d\n", error);
		goto out;
	}

	/*
	 * Copy the FIB and data back out to the caller.
	 */
	size = cm->cm_fib->Header.Size;
	if (size > sc->aac_max_fib_size) {
		device_printf(sc->aac_dev, "outbound FIB oversized (%d > %d)\n",
			      size, sc->aac_max_fib_size);
		size = sc->aac_max_fib_size;
	}
	error = copyout(cm->cm_fib, ufib, size);

out:
	if (cm != NULL) {
		mtx_lock(&sc->aac_io_lock);
		aac_release_command(cm);
		mtx_unlock(&sc->aac_io_lock);
	}
	return(error);
}

/*
 * Send a passthrough FIB supplied from userspace
 */
static int
aac_ioctl_send_raw_srb(struct aac_softc *sc, caddr_t arg)
{
	struct aac_command *cm;
	struct aac_event *event;
	struct aac_fib *fib;
	struct aac_srb *srbcmd, *user_srb;
	struct aac_sg_entry *sge;
	struct aac_sg_entry64 *sge64;
	void *srb_sg_address, *ureply;
	uint32_t fibsize, srb_sg_bytecount;
	int error, transfer_data;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	cm = NULL;
	transfer_data = 0;
	fibsize = 0;
	user_srb = (struct aac_srb *)arg;

	mtx_lock(&sc->aac_io_lock);
	if (aac_alloc_command(sc, &cm)) {
		 event = malloc(sizeof(struct aac_event), M_AACBUF,
		    M_NOWAIT | M_ZERO);
		if (event == NULL) {
			error = EBUSY;
			mtx_unlock(&sc->aac_io_lock);
			goto out;
		}
		event->ev_type = AAC_EVENT_CMFREE;
		event->ev_callback = aac_ioctl_event;
		event->ev_arg = &cm;
		aac_add_event(sc, event);
		msleep(cm, &sc->aac_io_lock, 0, "aacraw", 0);
	}
	mtx_unlock(&sc->aac_io_lock);

	cm->cm_data = NULL;
	fib = cm->cm_fib;
	srbcmd = (struct aac_srb *)fib->data;
	error = copyin(&user_srb->data_len, &fibsize, sizeof(uint32_t));
	if (error != 0)
		goto out;
	if (fibsize > (sc->aac_max_fib_size - sizeof(struct aac_fib_header))) {
		error = EINVAL;
		goto out;
	}
	error = copyin(user_srb, srbcmd, fibsize);
	if (error != 0)
		goto out;
	srbcmd->function = 0;
	srbcmd->retry_limit = 0;
	if (srbcmd->sg_map.SgCount > 1) {
		error = EINVAL;
		goto out;
	}

	/* Retrieve correct SG entries. */
	if (fibsize == (sizeof(struct aac_srb) +
	    srbcmd->sg_map.SgCount * sizeof(struct aac_sg_entry))) {
		struct aac_sg_entry sg;

		sge = srbcmd->sg_map.SgEntry;
		sge64 = NULL;

		if ((error = copyin(sge, &sg, sizeof(sg))) != 0)
			goto out;

		srb_sg_bytecount = sg.SgByteCount;
		srb_sg_address = (void *)(uintptr_t)sg.SgAddress;
	}
#ifdef __amd64__
	else if (fibsize == (sizeof(struct aac_srb) +
	    srbcmd->sg_map.SgCount * sizeof(struct aac_sg_entry64))) {
		struct aac_sg_entry64 sg;

		sge = NULL;
		sge64 = (struct aac_sg_entry64 *)srbcmd->sg_map.SgEntry;

		if ((error = copyin(sge64, &sg, sizeof(sg))) != 0)
			goto out;

		srb_sg_bytecount = sg.SgByteCount;
		srb_sg_address = (void *)sg.SgAddress;
		if (sge64->SgAddress > 0xffffffffull &&
		    (sc->flags & AAC_FLAGS_SG_64BIT) == 0) {
			error = EINVAL;
			goto out;
		}
	}
#endif
	else {
		error = EINVAL;
		goto out;
	}
	ureply = (char *)arg + fibsize;
	srbcmd->data_len = srb_sg_bytecount;
	if (srbcmd->sg_map.SgCount == 1)
		transfer_data = 1;

	cm->cm_sgtable = (struct aac_sg_table *)&srbcmd->sg_map;
	if (transfer_data) {
		cm->cm_datalen = srb_sg_bytecount;
		cm->cm_data = malloc(cm->cm_datalen, M_AACBUF, M_NOWAIT);
		if (cm->cm_data == NULL) {
			error = ENOMEM;
			goto out;
		}
		if (srbcmd->flags & AAC_SRB_FLAGS_DATA_IN)
			cm->cm_flags |= AAC_CMD_DATAIN;
		if (srbcmd->flags & AAC_SRB_FLAGS_DATA_OUT) {
			cm->cm_flags |= AAC_CMD_DATAOUT;
			error = copyin(srb_sg_address, cm->cm_data,
			    cm->cm_datalen);
			if (error != 0)
				goto out;
		}
	}

	fib->Header.Size = sizeof(struct aac_fib_header) +
	    sizeof(struct aac_srb);
	fib->Header.XferState =
	    AAC_FIBSTATE_HOSTOWNED   |
	    AAC_FIBSTATE_INITIALISED |
	    AAC_FIBSTATE_EMPTY       |
	    AAC_FIBSTATE_FROMHOST    |
	    AAC_FIBSTATE_REXPECTED   |
	    AAC_FIBSTATE_NORM        |
	    AAC_FIBSTATE_ASYNC       |
	    AAC_FIBSTATE_FAST_RESPONSE;
	fib->Header.Command = (sc->flags & AAC_FLAGS_SG_64BIT) != 0 ?
	    ScsiPortCommandU64 : ScsiPortCommand;

	mtx_lock(&sc->aac_io_lock);
	aac_wait_command(cm);
	mtx_unlock(&sc->aac_io_lock);

	if (transfer_data && (srbcmd->flags & AAC_SRB_FLAGS_DATA_IN) != 0) {
		error = copyout(cm->cm_data, srb_sg_address, cm->cm_datalen);
		if (error != 0)
			goto out;
	}
	error = copyout(fib->data, ureply, sizeof(struct aac_srb_response));
out:
	if (cm != NULL) {
		if (cm->cm_data != NULL)
			free(cm->cm_data, M_AACBUF);
		mtx_lock(&sc->aac_io_lock);
		aac_release_command(cm);
		mtx_unlock(&sc->aac_io_lock);
	}
	return(error);
}

/*
 * cdevpriv interface private destructor.
 */
static void
aac_cdevpriv_dtor(void *arg)
{
	struct aac_softc *sc;

	sc = arg;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	mtx_lock(&Giant);
	device_unbusy(sc->aac_dev);
	mtx_unlock(&Giant);
}

/*
 * Handle an AIF sent to us by the controller; queue it for later reference.
 * If the queue fills up, then drop the older entries.
 */
static void
aac_handle_aif(struct aac_softc *sc, struct aac_fib *fib)
{
	struct aac_aif_command *aif;
	struct aac_container *co, *co_next;
	struct aac_fib_context *ctx;
	struct aac_mntinforesp *mir;
	int next, current, found;
	int count = 0, added = 0, i = 0;
	uint32_t channel;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	aif = (struct aac_aif_command*)&fib->data[0];
	aac_print_aif(sc, aif);

	/* Is it an event that we should care about? */
	switch (aif->command) {
	case AifCmdEventNotify:
		switch (aif->data.EN.type) {
		case AifEnAddContainer:
		case AifEnDeleteContainer:
			/*
			 * A container was added or deleted, but the message
			 * doesn't tell us anything else!  Re-enumerate the
			 * containers and sort things out.
			 */
			aac_alloc_sync_fib(sc, &fib);
			do {
				/*
				 * Ask the controller for its containers one at
				 * a time.
				 * XXX What if the controller's list changes
				 * midway through this enumaration?
				 * XXX This should be done async.
				 */
				if ((mir = aac_get_container_info(sc, fib, i)) == NULL)
					continue;
				if (i == 0)
					count = mir->MntRespCount;
				/*
				 * Check the container against our list.
				 * co->co_found was already set to 0 in a
				 * previous run.
				 */
				if ((mir->Status == ST_OK) &&
				    (mir->MntTable[0].VolType != CT_NONE)) {
					found = 0;
					TAILQ_FOREACH(co,
						      &sc->aac_container_tqh,
						      co_link) {
						if (co->co_mntobj.ObjectId ==
						    mir->MntTable[0].ObjectId) {
							co->co_found = 1;
							found = 1;
							break;
						}
					}
					/*
					 * If the container matched, continue
					 * in the list.
					 */
					if (found) {
						i++;
						continue;
					}

					/*
					 * This is a new container.  Do all the
					 * appropriate things to set it up.
					 */
					aac_add_container(sc, mir, 1);
					added = 1;
				}
				i++;
			} while ((i < count) && (i < AAC_MAX_CONTAINERS));
			aac_release_sync_fib(sc);

			/*
			 * Go through our list of containers and see which ones
			 * were not marked 'found'.  Since the controller didn't
			 * list them they must have been deleted.  Do the
			 * appropriate steps to destroy the device.  Also reset
			 * the co->co_found field.
			 */
			co = TAILQ_FIRST(&sc->aac_container_tqh);
			while (co != NULL) {
				if (co->co_found == 0) {
					mtx_unlock(&sc->aac_io_lock);
					mtx_lock(&Giant);
					device_delete_child(sc->aac_dev,
							    co->co_disk);
					mtx_unlock(&Giant);
					mtx_lock(&sc->aac_io_lock);
					co_next = TAILQ_NEXT(co, co_link);
					mtx_lock(&sc->aac_container_lock);
					TAILQ_REMOVE(&sc->aac_container_tqh, co,
						     co_link);
					mtx_unlock(&sc->aac_container_lock);
					free(co, M_AACBUF);
					co = co_next;
				} else {
					co->co_found = 0;
					co = TAILQ_NEXT(co, co_link);
				}
			}

			/* Attach the newly created containers */
			if (added) {
				mtx_unlock(&sc->aac_io_lock);
				mtx_lock(&Giant);
				bus_generic_attach(sc->aac_dev);
				mtx_unlock(&Giant);
				mtx_lock(&sc->aac_io_lock);
			}

			break;

		case AifEnEnclosureManagement:
			switch (aif->data.EN.data.EEE.eventType) {
			case AIF_EM_DRIVE_INSERTION:
			case AIF_EM_DRIVE_REMOVAL:
				channel = aif->data.EN.data.EEE.unitID;
				if (sc->cam_rescan_cb != NULL)
					sc->cam_rescan_cb(sc,
					    (channel >> 24) & 0xF,
					    (channel & 0xFFFF));
				break;
			}
			break;

		case AifEnAddJBOD:
		case AifEnDeleteJBOD:
			channel = aif->data.EN.data.ECE.container;
			if (sc->cam_rescan_cb != NULL)
				sc->cam_rescan_cb(sc, (channel >> 24) & 0xF,
				    AAC_CAM_TARGET_WILDCARD);
			break;

		default:
			break;
		}

	default:
		break;
	}

	/* Copy the AIF data to the AIF queue for ioctl retrieval */
	mtx_lock(&sc->aac_aifq_lock);
	current = sc->aifq_idx;
	next = (current + 1) % AAC_AIFQ_LENGTH;
	if (next == 0)
		sc->aifq_filled = 1;
	bcopy(fib, &sc->aac_aifq[current], sizeof(struct aac_fib));
	/* modify AIF contexts */
	if (sc->aifq_filled) {
		for (ctx = sc->fibctx; ctx; ctx = ctx->next) {
			if (next == ctx->ctx_idx)
				ctx->ctx_wrap = 1;
			else if (current == ctx->ctx_idx && ctx->ctx_wrap)
				ctx->ctx_idx = next;
		}	
	}
	sc->aifq_idx = next;
	/* On the off chance that someone is sleeping for an aif... */
	if (sc->aac_state & AAC_STATE_AIF_SLEEPER)
		wakeup(sc->aac_aifq);
	/* Wakeup any poll()ers */
	selwakeuppri(&sc->rcv_select, PRIBIO);
	mtx_unlock(&sc->aac_aifq_lock);
}

/*
 * Return the Revision of the driver to userspace and check to see if the
 * userspace app is possibly compatible.  This is extremely bogus since
 * our driver doesn't follow Adaptec's versioning system.  Cheat by just
 * returning what the card reported.
 */
static int
aac_rev_check(struct aac_softc *sc, caddr_t udata)
{
	struct aac_rev_check rev_check;
	struct aac_rev_check_resp rev_check_resp;
	int error = 0;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/*
	 * Copyin the revision struct from userspace
	 */
	if ((error = copyin(udata, (caddr_t)&rev_check,
			sizeof(struct aac_rev_check))) != 0) {
		return error;
	}

	fwprintf(sc, HBA_FLAGS_DBG_IOCTL_COMMANDS_B, "Userland revision= %d\n",
	      rev_check.callingRevision.buildNumber);

	/*
	 * Doctor up the response struct.
	 */
	rev_check_resp.possiblyCompatible = 1;
	rev_check_resp.adapterSWRevision.external.comp.major =
	    AAC_DRIVER_MAJOR_VERSION;
	rev_check_resp.adapterSWRevision.external.comp.minor =
	    AAC_DRIVER_MINOR_VERSION;
	rev_check_resp.adapterSWRevision.external.comp.type =
	    AAC_DRIVER_TYPE;
	rev_check_resp.adapterSWRevision.external.comp.dash =
	    AAC_DRIVER_BUGFIX_LEVEL;
	rev_check_resp.adapterSWRevision.buildNumber =
	    AAC_DRIVER_BUILD;

	return(copyout((caddr_t)&rev_check_resp, udata,
			sizeof(struct aac_rev_check_resp)));
}

/*
 * Pass the fib context to the caller
 */
static int
aac_open_aif(struct aac_softc *sc, caddr_t arg)
{
	struct aac_fib_context *fibctx, *ctx;
	int error = 0;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	fibctx = malloc(sizeof(struct aac_fib_context), M_AACBUF, M_NOWAIT|M_ZERO);
	if (fibctx == NULL)
		return (ENOMEM);

	mtx_lock(&sc->aac_aifq_lock);
	/* all elements are already 0, add to queue */
	if (sc->fibctx == NULL)
		sc->fibctx = fibctx;
	else {
		for (ctx = sc->fibctx; ctx->next; ctx = ctx->next)
			;
		ctx->next = fibctx;
		fibctx->prev = ctx;
	}

	/* evaluate unique value */
	fibctx->unique = (*(u_int32_t *)&fibctx & 0xffffffff);
	ctx = sc->fibctx;
	while (ctx != fibctx) {
		if (ctx->unique == fibctx->unique) {
			fibctx->unique++;
			ctx = sc->fibctx;
		} else {
			ctx = ctx->next;
		}
	}
	mtx_unlock(&sc->aac_aifq_lock);

	error = copyout(&fibctx->unique, (void *)arg, sizeof(u_int32_t));
	if (error)
		aac_close_aif(sc, (caddr_t)ctx);
	return error;
}

/*
 * Close the caller's fib context
 */
static int
aac_close_aif(struct aac_softc *sc, caddr_t arg)
{
	struct aac_fib_context *ctx;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	mtx_lock(&sc->aac_aifq_lock);
	for (ctx = sc->fibctx; ctx; ctx = ctx->next) {
		if (ctx->unique == *(uint32_t *)&arg) {
			if (ctx == sc->fibctx)
				sc->fibctx = NULL;
			else {
				ctx->prev->next = ctx->next;
				if (ctx->next)
					ctx->next->prev = ctx->prev;
			}
			break;
		}
	}
	mtx_unlock(&sc->aac_aifq_lock);
	if (ctx)
		free(ctx, M_AACBUF);

	return 0;
}

/*
 * Pass the caller the next AIF in their queue
 */
static int
aac_getnext_aif(struct aac_softc *sc, caddr_t arg)
{
	struct get_adapter_fib_ioctl agf;
	struct aac_fib_context *ctx;
	int error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32)) {
		struct get_adapter_fib_ioctl32 agf32;
		error = copyin(arg, &agf32, sizeof(agf32));
		if (error == 0) {
			agf.AdapterFibContext = agf32.AdapterFibContext;
			agf.Wait = agf32.Wait;
			agf.AifFib = (caddr_t)(uintptr_t)agf32.AifFib;
		}
	} else
#endif
		error = copyin(arg, &agf, sizeof(agf));
	if (error == 0) {
		for (ctx = sc->fibctx; ctx; ctx = ctx->next) {
			if (agf.AdapterFibContext == ctx->unique)
				break;
		}
		if (!ctx)
			return (EFAULT);

		error = aac_return_aif(sc, ctx, agf.AifFib);
		if (error == EAGAIN && agf.Wait) {
			fwprintf(sc, HBA_FLAGS_DBG_AIF_B, "aac_getnext_aif(): waiting for AIF");
			sc->aac_state |= AAC_STATE_AIF_SLEEPER;
			while (error == EAGAIN) {
				error = tsleep(sc->aac_aifq, PRIBIO |
					       PCATCH, "aacaif", 0);
				if (error == 0)
					error = aac_return_aif(sc, ctx, agf.AifFib);
			}
			sc->aac_state &= ~AAC_STATE_AIF_SLEEPER;
		}
	}
	return(error);
}

/*
 * Hand the next AIF off the top of the queue out to userspace.
 */
static int
aac_return_aif(struct aac_softc *sc, struct aac_fib_context *ctx, caddr_t uptr)
{
	int current, error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	mtx_lock(&sc->aac_aifq_lock);
	current = ctx->ctx_idx;
	if (current == sc->aifq_idx && !ctx->ctx_wrap) {
		/* empty */
		mtx_unlock(&sc->aac_aifq_lock);
		return (EAGAIN);
	}
	error =
		copyout(&sc->aac_aifq[current], (void *)uptr, sizeof(struct aac_fib));
	if (error)
		device_printf(sc->aac_dev,
		    "aac_return_aif: copyout returned %d\n", error);
	else {
		ctx->ctx_wrap = 0;
		ctx->ctx_idx = (current + 1) % AAC_AIFQ_LENGTH;
	}
	mtx_unlock(&sc->aac_aifq_lock);
	return(error);
}

static int
aac_get_pci_info(struct aac_softc *sc, caddr_t uptr)
{
	struct aac_pci_info {
		u_int32_t bus;
		u_int32_t slot;
	} pciinf;
	int error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	pciinf.bus = pci_get_bus(sc->aac_dev);
	pciinf.slot = pci_get_slot(sc->aac_dev);

	error = copyout((caddr_t)&pciinf, uptr,
			sizeof(struct aac_pci_info));

	return (error);
}

static int
aac_supported_features(struct aac_softc *sc, caddr_t uptr)
{
	struct aac_features f;
	int error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if ((error = copyin(uptr, &f, sizeof (f))) != 0)
		return (error);

	/*
	 * When the management driver receives FSACTL_GET_FEATURES ioctl with
	 * ALL zero in the featuresState, the driver will return the current
	 * state of all the supported features, the data field will not be
	 * valid.
	 * When the management driver receives FSACTL_GET_FEATURES ioctl with
	 * a specific bit set in the featuresState, the driver will return the
	 * current state of this specific feature and whatever data that are
	 * associated with the feature in the data field or perform whatever
	 * action needed indicates in the data field.
	 */
	if (f.feat.fValue == 0) {
		f.feat.fBits.largeLBA =
		    (sc->flags & AAC_FLAGS_LBA_64BIT) ? 1 : 0;
		/* TODO: In the future, add other features state here as well */
	} else {
		if (f.feat.fBits.largeLBA)
			f.feat.fBits.largeLBA =
			    (sc->flags & AAC_FLAGS_LBA_64BIT) ? 1 : 0;
		/* TODO: Add other features state and data in the future */
	}

	error = copyout(&f, uptr, sizeof (f));
	return (error);
}

/*
 * Give the userland some information about the container.  The AAC arch
 * expects the driver to be a SCSI passthrough type driver, so it expects
 * the containers to have b:t:l numbers.  Fake it.
 */
static int
aac_query_disk(struct aac_softc *sc, caddr_t uptr)
{
	struct aac_query_disk query_disk;
	struct aac_container *co;
	struct aac_disk	*disk;
	int error, id;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	disk = NULL;

	error = copyin(uptr, (caddr_t)&query_disk,
		       sizeof(struct aac_query_disk));
	if (error)
		return (error);

	id = query_disk.ContainerNumber;
	if (id == -1)
		return (EINVAL);

	mtx_lock(&sc->aac_container_lock);
	TAILQ_FOREACH(co, &sc->aac_container_tqh, co_link) {
		if (co->co_mntobj.ObjectId == id)
			break;
		}

	if (co == NULL) {
			query_disk.Valid = 0;
			query_disk.Locked = 0;
			query_disk.Deleted = 1;		/* XXX is this right? */
	} else {
		disk = device_get_softc(co->co_disk);
		query_disk.Valid = 1;
		query_disk.Locked =
		    (disk->ad_flags & AAC_DISK_OPEN) ? 1 : 0;
		query_disk.Deleted = 0;
		query_disk.Bus = device_get_unit(sc->aac_dev);
		query_disk.Target = disk->unit;
		query_disk.Lun = 0;
		query_disk.UnMapped = 0;
		sprintf(&query_disk.diskDeviceName[0], "%s%d",
			disk->ad_disk->d_name, disk->ad_disk->d_unit);
	}
	mtx_unlock(&sc->aac_container_lock);

	error = copyout((caddr_t)&query_disk, uptr,
			sizeof(struct aac_query_disk));

	return (error);
}

static void
aac_get_bus_info(struct aac_softc *sc)
{
	struct aac_fib *fib;
	struct aac_ctcfg *c_cmd;
	struct aac_ctcfg_resp *c_resp;
	struct aac_vmioctl *vmi;
	struct aac_vmi_businf_resp *vmi_resp;
	struct aac_getbusinf businfo;
	struct aac_sim *caminf;
	device_t child;
	int i, found, error;

	mtx_lock(&sc->aac_io_lock);
	aac_alloc_sync_fib(sc, &fib);
	c_cmd = (struct aac_ctcfg *)&fib->data[0];
	bzero(c_cmd, sizeof(struct aac_ctcfg));

	c_cmd->Command = VM_ContainerConfig;
	c_cmd->cmd = CT_GET_SCSI_METHOD;
	c_cmd->param = 0;

	error = aac_sync_fib(sc, ContainerCommand, 0, fib,
	    sizeof(struct aac_ctcfg));
	if (error) {
		device_printf(sc->aac_dev, "Error %d sending "
		    "VM_ContainerConfig command\n", error);
		aac_release_sync_fib(sc);
		mtx_unlock(&sc->aac_io_lock);
		return;
	}

	c_resp = (struct aac_ctcfg_resp *)&fib->data[0];
	if (c_resp->Status != ST_OK) {
		device_printf(sc->aac_dev, "VM_ContainerConfig returned 0x%x\n",
		    c_resp->Status);
		aac_release_sync_fib(sc);
		mtx_unlock(&sc->aac_io_lock);
		return;
	}

	sc->scsi_method_id = c_resp->param;

	vmi = (struct aac_vmioctl *)&fib->data[0];
	bzero(vmi, sizeof(struct aac_vmioctl));

	vmi->Command = VM_Ioctl;
	vmi->ObjType = FT_DRIVE;
	vmi->MethId = sc->scsi_method_id;
	vmi->ObjId = 0;
	vmi->IoctlCmd = GetBusInfo;

	error = aac_sync_fib(sc, ContainerCommand, 0, fib,
	    sizeof(struct aac_vmi_businf_resp));
	if (error) {
		device_printf(sc->aac_dev, "Error %d sending VMIoctl command\n",
		    error);
		aac_release_sync_fib(sc);
		mtx_unlock(&sc->aac_io_lock);
		return;
	}

	vmi_resp = (struct aac_vmi_businf_resp *)&fib->data[0];
	if (vmi_resp->Status != ST_OK) {
		device_printf(sc->aac_dev, "VM_Ioctl returned %d\n",
		    vmi_resp->Status);
		aac_release_sync_fib(sc);
		mtx_unlock(&sc->aac_io_lock);
		return;
	}

	bcopy(&vmi_resp->BusInf, &businfo, sizeof(struct aac_getbusinf));
	aac_release_sync_fib(sc);
	mtx_unlock(&sc->aac_io_lock);

	found = 0;
	for (i = 0; i < businfo.BusCount; i++) {
		if (businfo.BusValid[i] != AAC_BUS_VALID)
			continue;

		caminf = (struct aac_sim *)malloc( sizeof(struct aac_sim),
		    M_AACBUF, M_NOWAIT | M_ZERO);
		if (caminf == NULL) {
			device_printf(sc->aac_dev,
			    "No memory to add passthrough bus %d\n", i);
			break;
		}

		child = device_add_child(sc->aac_dev, "aacp", -1);
		if (child == NULL) {
			device_printf(sc->aac_dev,
			    "device_add_child failed for passthrough bus %d\n",
			    i);
			free(caminf, M_AACBUF);
			break;
		}

		caminf->TargetsPerBus = businfo.TargetsPerBus;
		caminf->BusNumber = i;
		caminf->InitiatorBusId = businfo.InitiatorBusId[i];
		caminf->aac_sc = sc;
		caminf->sim_dev = child;

		device_set_ivars(child, caminf);
		device_set_desc(child, "SCSI Passthrough Bus");
		TAILQ_INSERT_TAIL(&sc->aac_sim_tqh, caminf, sim_link);

		found = 1;
	}

	if (found)
		bus_generic_attach(sc->aac_dev);
}
