/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2001 Scott Long
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001-2010 Adaptec, Inc.
 * Copyright (c) 2010-2012 PMC-Sierra, Inc.
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
 * Driver for the Adaptec by PMC Series 6,7,8,... families of RAID controllers
 */
#define AAC_DRIVERNAME			"aacraid"

#include "opt_aacraid.h"

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

#include <dev/aacraid/aacraid_reg.h>
#include <sys/aac_ioctl.h>
#include <dev/aacraid/aacraid_debug.h>
#include <dev/aacraid/aacraid_var.h>

#ifndef FILTER_HANDLED
#define FILTER_HANDLED	0x02
#endif

static void	aac_add_container(struct aac_softc *sc,
				  struct aac_mntinforesp *mir, int f, 
				  u_int32_t uid);
static void	aac_get_bus_info(struct aac_softc *sc);
static void	aac_container_bus(struct aac_softc *sc);
static void	aac_daemon(void *arg);
static int aac_convert_sgraw2(struct aac_softc *sc, struct aac_raw_io2 *raw,
							  int pages, int nseg, int nseg_new);

/* Command Processing */
static void	aac_timeout(struct aac_softc *sc);
static void	aac_command_thread(struct aac_softc *sc);
static int	aac_sync_fib(struct aac_softc *sc, u_int32_t command,
				     u_int32_t xferstate, struct aac_fib *fib,
				     u_int16_t datasize);
/* Command Buffer Management */
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
static void	aac_define_int_mode(struct aac_softc *sc);
static int	aac_init(struct aac_softc *sc);
static int	aac_find_pci_capability(struct aac_softc *sc, int cap);
static int	aac_setup_intr(struct aac_softc *sc);
static int	aac_check_config(struct aac_softc *sc);

/* PMC SRC interface */
static int	aac_src_get_fwstatus(struct aac_softc *sc);
static void	aac_src_qnotify(struct aac_softc *sc, int qbit);
static int	aac_src_get_istatus(struct aac_softc *sc);
static void	aac_src_clear_istatus(struct aac_softc *sc, int mask);
static void	aac_src_set_mailbox(struct aac_softc *sc, u_int32_t command,
				    u_int32_t arg0, u_int32_t arg1,
				    u_int32_t arg2, u_int32_t arg3);
static int	aac_src_get_mailbox(struct aac_softc *sc, int mb);
static void	aac_src_access_devreg(struct aac_softc *sc, int mode);
static int aac_src_send_command(struct aac_softc *sc, struct aac_command *cm);
static int aac_src_get_outb_queue(struct aac_softc *sc);
static void aac_src_set_outb_queue(struct aac_softc *sc, int index);

struct aac_interface aacraid_src_interface = {
	aac_src_get_fwstatus,
	aac_src_qnotify,
	aac_src_get_istatus,
	aac_src_clear_istatus,
	aac_src_set_mailbox,
	aac_src_get_mailbox,
	aac_src_access_devreg,
	aac_src_send_command,
	aac_src_get_outb_queue,
	aac_src_set_outb_queue
};

/* PMC SRCv interface */
static void	aac_srcv_set_mailbox(struct aac_softc *sc, u_int32_t command,
				    u_int32_t arg0, u_int32_t arg1,
				    u_int32_t arg2, u_int32_t arg3);
static int	aac_srcv_get_mailbox(struct aac_softc *sc, int mb);

struct aac_interface aacraid_srcv_interface = {
	aac_src_get_fwstatus,
	aac_src_qnotify,
	aac_src_get_istatus,
	aac_src_clear_istatus,
	aac_srcv_set_mailbox,
	aac_srcv_get_mailbox,
	aac_src_access_devreg,
	aac_src_send_command,
	aac_src_get_outb_queue,
	aac_src_set_outb_queue
};

/* Debugging and Diagnostics */
static struct aac_code_lookup aac_cpu_variant[] = {
	{"i960JX",		CPUI960_JX},
	{"i960CX",		CPUI960_CX},
	{"i960HX",		CPUI960_HX},
	{"i960RX",		CPUI960_RX},
	{"i960 80303",		CPUI960_80303},
	{"StrongARM SA110",	CPUARM_SA110},
	{"PPC603e",		CPUPPC_603e},
	{"XScale 80321",	CPU_XSCALE_80321},
	{"MIPS 4KC",		CPU_MIPS_4KC},
	{"MIPS 5KC",		CPU_MIPS_5KC},
	{"Unknown StrongARM",	CPUARM_xxx},
	{"Unknown PowerPC",	CPUPPC_xxx},
	{NULL, 0},
	{"Unknown processor",	0}
};

static struct aac_code_lookup aac_battery_platform[] = {
	{"required battery present",		PLATFORM_BAT_REQ_PRESENT},
	{"REQUIRED BATTERY NOT PRESENT",	PLATFORM_BAT_REQ_NOTPRESENT},
	{"optional battery present",		PLATFORM_BAT_OPT_PRESENT},
	{"optional battery not installed",	PLATFORM_BAT_OPT_NOTPRESENT},
	{"no battery support",			PLATFORM_BAT_NOT_SUPPORTED},
	{NULL, 0},
	{"unknown battery platform",		0}
};
static void	aac_describe_controller(struct aac_softc *sc);
static char	*aac_describe_code(struct aac_code_lookup *table,
				   u_int32_t code);

/* Management Interface */
static d_open_t		aac_open;
static d_ioctl_t	aac_ioctl;
static d_poll_t		aac_poll;
#if __FreeBSD_version >= 702000
static void		aac_cdevpriv_dtor(void *arg);
#else
static d_close_t	aac_close;
#endif
static int	aac_ioctl_sendfib(struct aac_softc *sc, caddr_t ufib);
static int	aac_ioctl_send_raw_srb(struct aac_softc *sc, caddr_t arg);
static void	aac_handle_aif(struct aac_softc *sc, struct aac_fib *fib);
static void	aac_request_aif(struct aac_softc *sc);
static int	aac_rev_check(struct aac_softc *sc, caddr_t udata);
static int	aac_open_aif(struct aac_softc *sc, caddr_t arg);
static int	aac_close_aif(struct aac_softc *sc, caddr_t arg);
static int	aac_getnext_aif(struct aac_softc *sc, caddr_t arg);
static int	aac_return_aif(struct aac_softc *sc,
			       struct aac_fib_context *ctx, caddr_t uptr);
static int	aac_query_disk(struct aac_softc *sc, caddr_t uptr);
static int	aac_get_pci_info(struct aac_softc *sc, caddr_t uptr);
static int	aac_supported_features(struct aac_softc *sc, caddr_t uptr);
static void	aac_ioctl_event(struct aac_softc *sc,
				struct aac_event *event, void *arg);
static int	aac_reset_adapter(struct aac_softc *sc);
static int	aac_get_container_info(struct aac_softc *sc, 
				       struct aac_fib *fib, int cid,
				       struct aac_mntinforesp *mir, 
				       u_int32_t *uid);		
static u_int32_t
	aac_check_adapter_health(struct aac_softc *sc, u_int8_t *bled);

static struct cdevsw aacraid_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	aac_open,
#if __FreeBSD_version < 702000
	.d_close =	aac_close,
#endif
	.d_ioctl =	aac_ioctl,
	.d_poll =	aac_poll,
	.d_name =	"aacraid",
};

MALLOC_DEFINE(M_AACRAIDBUF, "aacraid_buf", "Buffers for the AACRAID driver");

/* sysctl node */
SYSCTL_NODE(_hw, OID_AUTO, aacraid, CTLFLAG_RD, 0, "AACRAID driver parameters");

/*
 * Device Interface
 */

/*
 * Initialize the controller and softc
 */
int
aacraid_attach(struct aac_softc *sc)
{
	int error, unit;
	struct aac_fib *fib;
	struct aac_mntinforesp mir;
	int count = 0, i = 0;
	u_int32_t uid;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	sc->hint_flags = device_get_flags(sc->aac_dev);
	/*
	 * Initialize per-controller queues.
	 */
	aac_initq_free(sc);
	aac_initq_ready(sc);
	aac_initq_busy(sc);

	/* mark controller as suspended until we get ourselves organised */
	sc->aac_state |= AAC_STATE_SUSPEND;

	/*
	 * Check that the firmware on the card is supported.
	 */
	sc->msi_enabled = FALSE;
	if ((error = aac_check_firmware(sc)) != 0)
		return(error);

	/*
	 * Initialize locks
	 */
	mtx_init(&sc->aac_io_lock, "AACRAID I/O lock", NULL, MTX_DEF);
	TAILQ_INIT(&sc->aac_container_tqh);
	TAILQ_INIT(&sc->aac_ev_cmfree);

#if __FreeBSD_version >= 800000
	/* Initialize the clock daemon callout. */
	callout_init_mtx(&sc->aac_daemontime, &sc->aac_io_lock, 0);
#endif
	/*
	 * Initialize the adapter.
	 */
	if ((error = aac_alloc(sc)) != 0)
		return(error);
	if (!(sc->flags & AAC_FLAGS_SYNC_MODE)) {
		aac_define_int_mode(sc);
		if ((error = aac_init(sc)) != 0)
			return(error);
	}

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
	 * Make the control device.
	 */
	unit = device_get_unit(sc->aac_dev);
	sc->aac_dev_t = make_dev(&aacraid_cdevsw, unit, UID_ROOT, GID_OPERATOR,
				 0640, "aacraid%d", unit);
	sc->aac_dev_t->si_drv1 = sc;

	/* Create the AIF thread */
	if (aac_kthread_create((void(*)(void *))aac_command_thread, sc,
		   &sc->aifthread, 0, 0, "aacraid%daif", unit))
		panic("Could not create AIF thread");

	/* Register the shutdown method to only be called post-dump */
	if ((sc->eh = EVENTHANDLER_REGISTER(shutdown_final, aacraid_shutdown,
	    sc->aac_dev, SHUTDOWN_PRI_DEFAULT)) == NULL)
		device_printf(sc->aac_dev,
			      "shutdown event registration failed\n");

	/* Find containers */
	mtx_lock(&sc->aac_io_lock);
	aac_alloc_sync_fib(sc, &fib);
	/* loop over possible containers */
	do {
		if ((aac_get_container_info(sc, fib, i, &mir, &uid)) != 0)
			continue;
		if (i == 0) 
			count = mir.MntRespCount;
		aac_add_container(sc, &mir, 0, uid);
		i++;
	} while ((i < count) && (i < AAC_MAX_CONTAINERS));
	aac_release_sync_fib(sc);
	mtx_unlock(&sc->aac_io_lock);

	/* Register with CAM for the containers */
	TAILQ_INIT(&sc->aac_sim_tqh);
	aac_container_bus(sc);
	/* Register with CAM for the non-DASD devices */
	if ((sc->flags & AAC_FLAGS_ENABLE_CAM) != 0) 
		aac_get_bus_info(sc);

	/* poke the bus to actually attach the child devices */
	bus_generic_attach(sc->aac_dev);

	/* mark the controller up */
	sc->aac_state &= ~AAC_STATE_SUSPEND;

	/* enable interrupts now */
	AAC_ACCESS_DEVREG(sc, AAC_ENABLE_INTERRUPT);

#if __FreeBSD_version >= 800000
	mtx_lock(&sc->aac_io_lock);
	callout_reset(&sc->aac_daemontime, 60 * hz, aac_daemon, sc);
	mtx_unlock(&sc->aac_io_lock);
#else
	{
		struct timeval tv;
		tv.tv_sec = 60;
		tv.tv_usec = 0;
		sc->timeout_id = timeout(aac_daemon, (void *)sc, tvtohz(&tv));
	}
#endif

	return(0);
}

static void
aac_daemon(void *arg)
{
	struct aac_softc *sc;
	struct timeval tv;
	struct aac_command *cm;
	struct aac_fib *fib;

	sc = arg;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

#if __FreeBSD_version >= 800000
	mtx_assert(&sc->aac_io_lock, MA_OWNED);
	if (callout_pending(&sc->aac_daemontime) ||
	    callout_active(&sc->aac_daemontime) == 0)
		return;
#else
	mtx_lock(&sc->aac_io_lock);
#endif
	getmicrotime(&tv);

	if (!aacraid_alloc_command(sc, &cm)) {
		fib = cm->cm_fib;
		cm->cm_timestamp = time_uptime;
		cm->cm_datalen = 0;
		cm->cm_flags |= AAC_CMD_WAIT;

		fib->Header.Size = 
			sizeof(struct aac_fib_header) + sizeof(u_int32_t);
		fib->Header.XferState =
			AAC_FIBSTATE_HOSTOWNED   |
			AAC_FIBSTATE_INITIALISED |
			AAC_FIBSTATE_EMPTY	 |
			AAC_FIBSTATE_FROMHOST	 |
			AAC_FIBSTATE_REXPECTED   |
			AAC_FIBSTATE_NORM	 |
			AAC_FIBSTATE_ASYNC	 |
			AAC_FIBSTATE_FAST_RESPONSE;
		fib->Header.Command = SendHostTime;
		*(uint32_t *)fib->data = tv.tv_sec;

		aacraid_map_command_sg(cm, NULL, 0, 0);
		aacraid_release_command(cm);
	}

#if __FreeBSD_version >= 800000
	callout_schedule(&sc->aac_daemontime, 30 * 60 * hz);
#else
	mtx_unlock(&sc->aac_io_lock);
	tv.tv_sec = 30 * 60;
	tv.tv_usec = 0;
	sc->timeout_id = timeout(aac_daemon, (void *)sc, tvtohz(&tv));
#endif
}

void
aacraid_add_event(struct aac_softc *sc, struct aac_event *event)
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

	return;
}

/*
 * Request information of container #cid
 */
static int
aac_get_container_info(struct aac_softc *sc, struct aac_fib *sync_fib, int cid,
		       struct aac_mntinforesp *mir, u_int32_t *uid)
{
	struct aac_command *cm;
	struct aac_fib *fib;
	struct aac_mntinfo *mi;
	struct aac_cnt_config *ccfg;
	int rval;

	if (sync_fib == NULL) {
		if (aacraid_alloc_command(sc, &cm)) {
			device_printf(sc->aac_dev,
				"Warning, no free command available\n");
			return (-1);
		}
		fib = cm->cm_fib;
	} else {
		fib = sync_fib;
	}

	mi = (struct aac_mntinfo *)&fib->data[0];
	/* 4KB support?, 64-bit LBA? */
	if (sc->aac_support_opt2 & AAC_SUPPORTED_VARIABLE_BLOCK_SIZE)
		mi->Command = VM_NameServeAllBlk;
	else if (sc->flags & AAC_FLAGS_LBA_64BIT) 
		mi->Command = VM_NameServe64;
	else
		mi->Command = VM_NameServe;
	mi->MntType = FT_FILESYS;
	mi->MntCount = cid;

	if (sync_fib) {
		if (aac_sync_fib(sc, ContainerCommand, 0, fib,
			 sizeof(struct aac_mntinfo))) {
			device_printf(sc->aac_dev, "Error probing container %d\n", cid);
			return (-1);
		}
	} else {
		cm->cm_timestamp = time_uptime;
		cm->cm_datalen = 0;

		fib->Header.Size = 
			sizeof(struct aac_fib_header) + sizeof(struct aac_mntinfo);
		fib->Header.XferState =
			AAC_FIBSTATE_HOSTOWNED   |
			AAC_FIBSTATE_INITIALISED |
			AAC_FIBSTATE_EMPTY	 |
			AAC_FIBSTATE_FROMHOST	 |
			AAC_FIBSTATE_REXPECTED   |
			AAC_FIBSTATE_NORM	 |
			AAC_FIBSTATE_ASYNC	 |
			AAC_FIBSTATE_FAST_RESPONSE;
		fib->Header.Command = ContainerCommand;
		if (aacraid_wait_command(cm) != 0) {
			device_printf(sc->aac_dev, "Error probing container %d\n", cid);
			aacraid_release_command(cm);
			return (-1);
		}
	}
	bcopy(&fib->data[0], mir, sizeof(struct aac_mntinforesp));

	/* UID */
	*uid = cid;
	if (mir->MntTable[0].VolType != CT_NONE && 
		!(mir->MntTable[0].ContentState & AAC_FSCS_HIDDEN)) {
		if (!(sc->aac_support_opt2 & AAC_SUPPORTED_VARIABLE_BLOCK_SIZE)) {
			mir->MntTable[0].ObjExtension.BlockDevice.BlockSize = 0x200;
			mir->MntTable[0].ObjExtension.BlockDevice.bdLgclPhysMap = 0;
		}
		ccfg = (struct aac_cnt_config *)&fib->data[0];
		bzero(ccfg, sizeof (*ccfg) - CT_PACKET_SIZE);
		ccfg->Command = VM_ContainerConfig;
		ccfg->CTCommand.command = CT_CID_TO_32BITS_UID;
		ccfg->CTCommand.param[0] = cid;

		if (sync_fib) {
			rval = aac_sync_fib(sc, ContainerCommand, 0, fib,
				sizeof(struct aac_cnt_config));
			if (rval == 0 && ccfg->Command == ST_OK &&
				ccfg->CTCommand.param[0] == CT_OK &&
				mir->MntTable[0].VolType != CT_PASSTHRU)
				*uid = ccfg->CTCommand.param[1];
		} else {
			fib->Header.Size = 
				sizeof(struct aac_fib_header) + sizeof(struct aac_cnt_config);
			fib->Header.XferState =
				AAC_FIBSTATE_HOSTOWNED   |
				AAC_FIBSTATE_INITIALISED |
				AAC_FIBSTATE_EMPTY	 |
				AAC_FIBSTATE_FROMHOST	 |
				AAC_FIBSTATE_REXPECTED   |
				AAC_FIBSTATE_NORM	 |
				AAC_FIBSTATE_ASYNC	 |
				AAC_FIBSTATE_FAST_RESPONSE;
			fib->Header.Command = ContainerCommand;
			rval = aacraid_wait_command(cm);
			if (rval == 0 && ccfg->Command == ST_OK &&
				ccfg->CTCommand.param[0] == CT_OK &&
				mir->MntTable[0].VolType != CT_PASSTHRU)
				*uid = ccfg->CTCommand.param[1];
			aacraid_release_command(cm);
		}
	}

	return (0);
}

/*
 * Create a device to represent a new container
 */
static void
aac_add_container(struct aac_softc *sc, struct aac_mntinforesp *mir, int f, 
		  u_int32_t uid)
{
	struct aac_container *co;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, ""); 

	/*
	 * Check container volume type for validity.  Note that many of
	 * the possible types may never show up.
	 */
	if ((mir->Status == ST_OK) && (mir->MntTable[0].VolType != CT_NONE)) {
		co = (struct aac_container *)malloc(sizeof *co, M_AACRAIDBUF,
		       M_NOWAIT | M_ZERO);
		if (co == NULL) {
			panic("Out of memory?!");
		}

		co->co_found = f;
		bcopy(&mir->MntTable[0], &co->co_mntobj,
		      sizeof(struct aac_mntobj));
		co->co_uid = uid;
		TAILQ_INSERT_TAIL(&sc->aac_container_tqh, co, co_link);
	}
}

/*
 * Allocate resources associated with (sc)
 */
static int
aac_alloc(struct aac_softc *sc)
{
	bus_size_t maxsize;

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
	if (sc->flags & AAC_FLAGS_NEW_COMM_TYPE1) 
		maxsize = sc->aac_max_fibs_alloc * (sc->aac_max_fib_size +
			sizeof(struct aac_fib_xporthdr) + 31);
	else
		maxsize = sc->aac_max_fibs_alloc * (sc->aac_max_fib_size + 31);
	if (bus_dma_tag_create(sc->aac_parent_dmat,	/* parent */
			       1, 0, 			/* algnmnt, boundary */
			       (sc->flags & AAC_FLAGS_4GB_WINDOW) ?
			       BUS_SPACE_MAXADDR_32BIT :
			       0x7fffffff,		/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       maxsize,  		/* maxsize */
			       1,			/* nsegments */
			       maxsize,			/* maxsize */
			       0,			/* flags */
			       NULL, NULL,		/* No locking needed */
			       &sc->aac_fib_dmat)) {
		device_printf(sc->aac_dev, "can't allocate FIB DMA tag\n");
		return (ENOMEM);
	}

	/*
	 * Create DMA tag for the common structure and allocate it.
	 */
	maxsize = sizeof(struct aac_common);
	maxsize += sc->aac_max_fibs * sizeof(u_int32_t);
	if (bus_dma_tag_create(sc->aac_parent_dmat, 	/* parent */
			       1, 0,			/* algnmnt, boundary */
			       (sc->flags & AAC_FLAGS_4GB_WINDOW) ?
			       BUS_SPACE_MAXADDR_32BIT :
			       0x7fffffff,		/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       maxsize, 		/* maxsize */
			       1,			/* nsegments */
			       maxsize,			/* maxsegsize */
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

	(void)bus_dmamap_load(sc->aac_common_dmat, sc->aac_common_dmamap,
			sc->aac_common, maxsize,
			aac_common_map, sc, 0);
	bzero(sc->aac_common, maxsize);

	/* Allocate some FIBs and associated command structs */
	TAILQ_INIT(&sc->aac_fibmap_tqh);
	sc->aac_commands = malloc(sc->aac_max_fibs * sizeof(struct aac_command),
				  M_AACRAIDBUF, M_WAITOK|M_ZERO);
	mtx_lock(&sc->aac_io_lock);
	while (sc->total_fibs < sc->aac_max_fibs) {
		if (aac_alloc_commands(sc) != 0)
			break;
	}
	mtx_unlock(&sc->aac_io_lock);
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
aacraid_free(struct aac_softc *sc)
{
	int i;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* remove the control device */
	if (sc->aac_dev_t != NULL)
		destroy_dev(sc->aac_dev_t);

	/* throw away any FIB buffers, discard the FIB DMA tag */
	aac_free_commands(sc);
	if (sc->aac_fib_dmat)
		bus_dma_tag_destroy(sc->aac_fib_dmat);

	free(sc->aac_commands, M_AACRAIDBUF);

	/* destroy the common area */
	if (sc->aac_common) {
		bus_dmamap_unload(sc->aac_common_dmat, sc->aac_common_dmamap);
		bus_dmamem_free(sc->aac_common_dmat, sc->aac_common,
				sc->aac_common_dmamap);
	}
	if (sc->aac_common_dmat)
		bus_dma_tag_destroy(sc->aac_common_dmat);

	/* disconnect the interrupt handler */
	for (i = 0; i < AAC_MAX_MSIX; ++i) {	
		if (sc->aac_intr[i])
			bus_teardown_intr(sc->aac_dev, 
				sc->aac_irq[i], sc->aac_intr[i]);
		if (sc->aac_irq[i])
			bus_release_resource(sc->aac_dev, SYS_RES_IRQ, 
				sc->aac_irq_rid[i], sc->aac_irq[i]);
		else
			break;
	}
	if (sc->msi_enabled)
		pci_release_msi(sc->aac_dev);

	/* destroy data-transfer DMA tag */
	if (sc->aac_buffer_dmat)
		bus_dma_tag_destroy(sc->aac_buffer_dmat);

	/* destroy the parent DMA tag */
	if (sc->aac_parent_dmat)
		bus_dma_tag_destroy(sc->aac_parent_dmat);

	/* release the register window mapping */
	if (sc->aac_regs_res0 != NULL)
		bus_release_resource(sc->aac_dev, SYS_RES_MEMORY,
				     sc->aac_regs_rid0, sc->aac_regs_res0);
	if (sc->aac_regs_res1 != NULL)
		bus_release_resource(sc->aac_dev, SYS_RES_MEMORY,
				     sc->aac_regs_rid1, sc->aac_regs_res1);
}

/*
 * Disconnect from the controller completely, in preparation for unload.
 */
int
aacraid_detach(device_t dev)
{
	struct aac_softc *sc;
	struct aac_container *co;
	struct aac_sim	*sim;
	int error;

	sc = device_get_softc(dev);
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

#if __FreeBSD_version >= 800000
	callout_drain(&sc->aac_daemontime);
#else
	untimeout(aac_daemon, (void *)sc, sc->timeout_id);
#endif
	/* Remove the child containers */
	while ((co = TAILQ_FIRST(&sc->aac_container_tqh)) != NULL) {
		TAILQ_REMOVE(&sc->aac_container_tqh, co, co_link);
		free(co, M_AACRAIDBUF);
	}

	/* Remove the CAM SIMs */
	while ((sim = TAILQ_FIRST(&sc->aac_sim_tqh)) != NULL) {
		TAILQ_REMOVE(&sc->aac_sim_tqh, sim, sim_link);
		error = device_delete_child(dev, sim->sim_dev);
		if (error)
			return (error);
		free(sim, M_AACRAIDBUF);
	}

	if (sc->aifflags & AAC_AIFFLAGS_RUNNING) {
		sc->aifflags |= AAC_AIFFLAGS_EXIT;
		wakeup(sc->aifthread);
		tsleep(sc->aac_dev, PUSER | PCATCH, "aac_dch", 30 * hz);
	}

	if (sc->aifflags & AAC_AIFFLAGS_RUNNING)
		panic("Cannot shutdown AIF thread");

	if ((error = aacraid_shutdown(dev)))
		return(error);

	EVENTHANDLER_DEREGISTER(shutdown_final, sc->eh);

	aacraid_free(sc);

	mtx_destroy(&sc->aac_io_lock);

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
aacraid_shutdown(device_t dev)
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
	cc->ContainerId = 0xfffffffe;
	if (aac_sync_fib(sc, ContainerCommand, 0, fib,
	    sizeof(struct aac_close_command)))
		printf("FAILED.\n");
	else
		printf("done\n");

	AAC_ACCESS_DEVREG(sc, AAC_DISABLE_INTERRUPT);
	aac_release_sync_fib(sc);
	mtx_unlock(&sc->aac_io_lock);

	return(0);
}

/*
 * Bring the controller to a quiescent state, ready for system suspend.
 */
int
aacraid_suspend(device_t dev)
{
	struct aac_softc *sc;

	sc = device_get_softc(dev);

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	sc->aac_state |= AAC_STATE_SUSPEND;

	AAC_ACCESS_DEVREG(sc, AAC_DISABLE_INTERRUPT);
	return(0);
}

/*
 * Bring the controller back to a state ready for operation.
 */
int
aacraid_resume(device_t dev)
{
	struct aac_softc *sc;

	sc = device_get_softc(dev);

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	sc->aac_state &= ~AAC_STATE_SUSPEND;
	AAC_ACCESS_DEVREG(sc, AAC_ENABLE_INTERRUPT);
	return(0);
}

/*
 * Interrupt handler for NEW_COMM_TYPE1, NEW_COMM_TYPE2, NEW_COMM_TYPE34 interface.
 */
void
aacraid_new_intr_type1(void *arg)
{
	struct aac_msix_ctx *ctx;
	struct aac_softc *sc;
	int vector_no;
	struct aac_command *cm;
	struct aac_fib *fib;
	u_int32_t bellbits, bellbits_shifted, index, handle;
	int isFastResponse, isAif, noMoreAif, mode;

	ctx = (struct aac_msix_ctx *)arg;
	sc = ctx->sc;
	vector_no = ctx->vector_no;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	mtx_lock(&sc->aac_io_lock);

	if (sc->msi_enabled) {
		mode = AAC_INT_MODE_MSI;
		if (vector_no == 0) {
			bellbits = AAC_MEM0_GETREG4(sc, AAC_SRC_ODBR_MSI);
			if (bellbits & 0x40000)
				mode |= AAC_INT_MODE_AIF;
			else if (bellbits & 0x1000)
				mode |= AAC_INT_MODE_SYNC;
		}
	} else {	
		mode = AAC_INT_MODE_INTX;
		bellbits = AAC_MEM0_GETREG4(sc, AAC_SRC_ODBR_R);
		if (bellbits & AAC_DB_RESPONSE_SENT_NS) {
			bellbits = AAC_DB_RESPONSE_SENT_NS;
			AAC_MEM0_SETREG4(sc, AAC_SRC_ODBR_C, bellbits);
		} else {
			bellbits_shifted = (bellbits >> AAC_SRC_ODR_SHIFT);
			AAC_MEM0_SETREG4(sc, AAC_SRC_ODBR_C, bellbits);
			if (bellbits_shifted & AAC_DB_AIF_PENDING)
				mode |= AAC_INT_MODE_AIF;
			else if (bellbits_shifted & AAC_DB_SYNC_COMMAND) 
				mode |= AAC_INT_MODE_SYNC;
		}
		/* ODR readback, Prep #238630 */
		AAC_MEM0_GETREG4(sc, AAC_SRC_ODBR_R);	
	}

	if (mode & AAC_INT_MODE_SYNC) {
		if (sc->aac_sync_cm) {	
			cm = sc->aac_sync_cm;
			cm->cm_flags |= AAC_CMD_COMPLETED;
			/* is there a completion handler? */
			if (cm->cm_complete != NULL) {
				cm->cm_complete(cm);
			} else {
				/* assume that someone is sleeping on this command */
				wakeup(cm);
			}
			sc->flags &= ~AAC_QUEUE_FRZN;
			sc->aac_sync_cm = NULL;
		}
		mode = 0;
	}

	if (mode & AAC_INT_MODE_AIF) {
		if (mode & AAC_INT_MODE_INTX) {
			aac_request_aif(sc);
			mode = 0;
		} 
	}

	if (mode) {
		/* handle async. status */
		index = sc->aac_host_rrq_idx[vector_no];
		for (;;) {
			isFastResponse = isAif = noMoreAif = 0;
			/* remove toggle bit (31) */
			handle = (sc->aac_common->ac_host_rrq[index] & 0x7fffffff);
			/* check fast response bit (30) */
			if (handle & 0x40000000) 
				isFastResponse = 1;
			/* check AIF bit (23) */
			else if (handle & 0x00800000) 
				isAif = TRUE;
			handle &= 0x0000ffff;
			if (handle == 0) 
				break;

			cm = sc->aac_commands + (handle - 1);
			fib = cm->cm_fib;
			sc->aac_rrq_outstanding[vector_no]--;
			if (isAif) {
				noMoreAif = (fib->Header.XferState & AAC_FIBSTATE_NOMOREAIF) ? 1:0;
				if (!noMoreAif)
					aac_handle_aif(sc, fib);
				aac_remove_busy(cm);
				aacraid_release_command(cm);
			} else {
				if (isFastResponse) {
					fib->Header.XferState |= AAC_FIBSTATE_DONEADAP;
					*((u_int32_t *)(fib->data)) = ST_OK;
					cm->cm_flags |= AAC_CMD_FASTRESP;
				}
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
				sc->flags &= ~AAC_QUEUE_FRZN;
			}

			sc->aac_common->ac_host_rrq[index++] = 0;
			if (index == (vector_no + 1) * sc->aac_vector_cap) 
				index = vector_no * sc->aac_vector_cap;
			sc->aac_host_rrq_idx[vector_no] = index;

			if ((isAif && !noMoreAif) || sc->aif_pending) 
				aac_request_aif(sc);
		}
	}

	if (mode & AAC_INT_MODE_AIF) {
		aac_request_aif(sc);
		AAC_ACCESS_DEVREG(sc, AAC_CLEAR_AIF_BIT);
		mode = 0;
	}

	/* see if we can start some more I/O */
	if ((sc->flags & AAC_QUEUE_FRZN) == 0)
		aacraid_startio(sc);
	mtx_unlock(&sc->aac_io_lock);
}

/*
 * Handle notification of one or more FIBs coming from the controller.
 */
static void
aac_command_thread(struct aac_softc *sc)
{
	int retval;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	mtx_lock(&sc->aac_io_lock);
	sc->aifflags = AAC_AIFFLAGS_RUNNING;

	while ((sc->aifflags & AAC_AIFFLAGS_EXIT) == 0) {

		retval = 0;
		if ((sc->aifflags & AAC_AIFFLAGS_PENDING) == 0)
			retval = msleep(sc->aifthread, &sc->aac_io_lock, PRIBIO,
					"aacraid_aifthd", AAC_PERIODIC_INTERVAL * hz);

		/*
		 * First see if any FIBs need to be allocated.  This needs
		 * to be called without the driver lock because contigmalloc
		 * will grab Giant, and would result in an LOR.
		 */
		if ((sc->aifflags & AAC_AIFFLAGS_ALLOCFIBS) != 0) {
			aac_alloc_commands(sc);
			sc->aifflags &= ~AAC_AIFFLAGS_ALLOCFIBS;
			aacraid_startio(sc);
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
	}
	sc->aifflags &= ~AAC_AIFFLAGS_RUNNING;
	mtx_unlock(&sc->aac_io_lock);
	wakeup(sc->aac_dev);

	aac_kthread_exit(0);
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
int
aacraid_wait_command(struct aac_command *cm)
{
	struct aac_softc *sc;
	int error;

	sc = cm->cm_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	/* Put the command on the ready queue and get things going */
	aac_enqueue_ready(cm);
	aacraid_startio(sc);
	error = msleep(cm, &sc->aac_io_lock, PRIBIO, "aacraid_wait", 0);
	return(error);
}

/*
 *Command Buffer Management
 */

/*
 * Allocate a command.
 */
int
aacraid_alloc_command(struct aac_softc *sc, struct aac_command **cmp)
{
	struct aac_command *cm;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if ((cm = aac_dequeue_free(sc)) == NULL) {
		if (sc->total_fibs < sc->aac_max_fibs) {
			sc->aifflags |= AAC_AIFFLAGS_ALLOCFIBS;
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
aacraid_release_command(struct aac_command *cm)
{
	struct aac_event *event;
	struct aac_softc *sc;

	sc = cm->cm_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	/* (re)initialize the command/FIB */
	cm->cm_sgtable = NULL;
	cm->cm_flags = 0;
	cm->cm_complete = NULL;
	cm->cm_ccb = NULL;
	cm->cm_passthr_dmat = 0;
	cm->cm_fib->Header.XferState = AAC_FIBSTATE_EMPTY;
	cm->cm_fib->Header.StructType = AAC_FIBTYPE_TFIB;
	cm->cm_fib->Header.Unused = 0;
	cm->cm_fib->Header.SenderSize = cm->cm_sc->aac_max_fib_size;

	/*
	 * These are duplicated in aac_start to cover the case where an
	 * intermediate stage may have destroyed them.  They're left
	 * initialized here for debugging purposes only.
	 */
	cm->cm_fib->Header.u.ReceiverFibAddress = (u_int32_t)cm->cm_fibphys;
	cm->cm_fib->Header.Handle = 0;

	aac_enqueue_free(cm);

	/*
	 * Dequeue all events so that there's no risk of events getting
	 * stranded.
	 */
	while ((event = TAILQ_FIRST(&sc->aac_ev_cmfree)) != NULL) {
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
	u_int32_t maxsize;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	if (sc->total_fibs + sc->aac_max_fibs_alloc > sc->aac_max_fibs)
		return (ENOMEM);

	fm = malloc(sizeof(struct aac_fibmap), M_AACRAIDBUF, M_NOWAIT|M_ZERO);
	if (fm == NULL)
		return (ENOMEM);

	mtx_unlock(&sc->aac_io_lock);
	/* allocate the FIBs in DMAable memory and load them */
	if (bus_dmamem_alloc(sc->aac_fib_dmat, (void **)&fm->aac_fibs,
			     BUS_DMA_NOWAIT, &fm->aac_fibmap)) {
		device_printf(sc->aac_dev,
			      "Not enough contiguous memory available.\n");
		free(fm, M_AACRAIDBUF);
		mtx_lock(&sc->aac_io_lock);
		return (ENOMEM);
	}

	maxsize = sc->aac_max_fib_size + 31;
	if (sc->flags & AAC_FLAGS_NEW_COMM_TYPE1) 
		maxsize += sizeof(struct aac_fib_xporthdr);
	/* Ignore errors since this doesn't bounce */
	(void)bus_dmamap_load(sc->aac_fib_dmat, fm->aac_fibmap, fm->aac_fibs,
			      sc->aac_max_fibs_alloc * maxsize,
			      aac_map_command_helper, &fibphys, 0);
	mtx_lock(&sc->aac_io_lock);

	/* initialize constant fields in the command structure */
	bzero(fm->aac_fibs, sc->aac_max_fibs_alloc * maxsize);
	for (i = 0; i < sc->aac_max_fibs_alloc; i++) {
		cm = sc->aac_commands + sc->total_fibs;
		fm->aac_commands = cm;
		cm->cm_sc = sc;
		cm->cm_fib = (struct aac_fib *)
			((u_int8_t *)fm->aac_fibs + i * maxsize);
		cm->cm_fibphys = fibphys + i * maxsize;
		if (sc->flags & AAC_FLAGS_NEW_COMM_TYPE1) {
			u_int64_t fibphys_aligned;
			fibphys_aligned = 
				(cm->cm_fibphys + sizeof(struct aac_fib_xporthdr) + 31) & ~31;
			cm->cm_fib = (struct aac_fib *)
				((u_int8_t *)cm->cm_fib + (fibphys_aligned - cm->cm_fibphys));
			cm->cm_fibphys = fibphys_aligned;
		} else {
			u_int64_t fibphys_aligned;
			fibphys_aligned = (cm->cm_fibphys + 31) & ~31;
			cm->cm_fib = (struct aac_fib *)
				((u_int8_t *)cm->cm_fib + (fibphys_aligned - cm->cm_fibphys));
			cm->cm_fibphys = fibphys_aligned;
		}
		cm->cm_index = sc->total_fibs;

		if ((error = bus_dmamap_create(sc->aac_buffer_dmat, 0,
					       &cm->cm_datamap)) != 0)
			break;
		if (sc->aac_max_fibs <= 1 || sc->aac_max_fibs - sc->total_fibs > 1)
			aacraid_release_command(cm);
		sc->total_fibs++;
	}

	if (i > 0) {
		TAILQ_INSERT_TAIL(&sc->aac_fibmap_tqh, fm, fm_link);
		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, "total_fibs= %d\n", sc->total_fibs);
		return (0);
	}

	bus_dmamap_unload(sc->aac_fib_dmat, fm->aac_fibmap);
	bus_dmamem_free(sc->aac_fib_dmat, fm->aac_fibs, fm->aac_fibmap);
	free(fm, M_AACRAIDBUF);
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
		free(fm, M_AACRAIDBUF);
	}
}

/*
 * Command-mapping helper function - populate this command's s/g table.
 */
void
aacraid_map_command_sg(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct aac_softc *sc;
	struct aac_command *cm;
	struct aac_fib *fib;
	int i;

	cm = (struct aac_command *)arg;
	sc = cm->cm_sc;
	fib = cm->cm_fib;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "nseg %d", nseg);
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	/* copy into the FIB */
	if (cm->cm_sgtable != NULL) {
		if (fib->Header.Command == RawIo2) {
			struct aac_raw_io2 *raw;
			struct aac_sge_ieee1212 *sg;
			u_int32_t min_size = PAGE_SIZE, cur_size;
			int conformable = TRUE;

			raw = (struct aac_raw_io2 *)&fib->data[0];
			sg = (struct aac_sge_ieee1212 *)cm->cm_sgtable;
			raw->sgeCnt = nseg;

			for (i = 0; i < nseg; i++) {
				cur_size = segs[i].ds_len;
				sg[i].addrHigh = 0;
				*(bus_addr_t *)&sg[i].addrLow = segs[i].ds_addr;
				sg[i].length = cur_size;
				sg[i].flags = 0;
				if (i == 0) {
					raw->sgeFirstSize = cur_size;
				} else if (i == 1) {
					raw->sgeNominalSize = cur_size;
					min_size = cur_size;
				} else if ((i+1) < nseg && 
					cur_size != raw->sgeNominalSize) {
					conformable = FALSE;
					if (cur_size < min_size)
						min_size = cur_size;
				}
			}

			/* not conformable: evaluate required sg elements */
			if (!conformable) {
				int j, err_found, nseg_new = nseg;
				for (i = min_size / PAGE_SIZE; i >= 1; --i) {
					err_found = FALSE;
					nseg_new = 2;
					for (j = 1; j < nseg - 1; ++j) {
						if (sg[j].length % (i*PAGE_SIZE)) {
							err_found = TRUE;
							break;
						}
						nseg_new += (sg[j].length / (i*PAGE_SIZE));
					}
					if (!err_found)
						break;
				}
				if (i>0 && nseg_new<=sc->aac_sg_tablesize && 
					!(sc->hint_flags & 4))
					nseg = aac_convert_sgraw2(sc, 
						raw, i, nseg, nseg_new);
			} else {
				raw->flags |= RIO2_SGL_CONFORMANT;
			}

			/* update the FIB size for the s/g count */
			fib->Header.Size += nseg * 
				sizeof(struct aac_sge_ieee1212);

		} else if (fib->Header.Command == RawIo) {
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
	cm->cm_fib->Header.u.ReceiverFibAddress = (u_int32_t)cm->cm_fibphys;

	/* save a pointer to the command for speedy reverse-lookup */
	cm->cm_fib->Header.Handle += cm->cm_index + 1;

	if (cm->cm_passthr_dmat == 0) {
		if (cm->cm_flags & AAC_CMD_DATAIN)
			bus_dmamap_sync(sc->aac_buffer_dmat, cm->cm_datamap,
							BUS_DMASYNC_PREREAD);
		if (cm->cm_flags & AAC_CMD_DATAOUT)
			bus_dmamap_sync(sc->aac_buffer_dmat, cm->cm_datamap,
							BUS_DMASYNC_PREWRITE);
	}

	cm->cm_flags |= AAC_CMD_MAPPED;

	if (sc->flags & AAC_FLAGS_SYNC_MODE) {
		u_int32_t wait = 0;
		aacraid_sync_command(sc, AAC_MONKER_SYNCFIB, cm->cm_fibphys, 0, 0, 0, &wait, NULL);
	} else if (cm->cm_flags & AAC_CMD_WAIT) {
		aacraid_sync_command(sc, AAC_MONKER_SYNCFIB, cm->cm_fibphys, 0, 0, 0, NULL, NULL);
	} else {
		int count = 10000000L;
		while (AAC_SEND_COMMAND(sc, cm) != 0) {
			if (--count == 0) {
				aac_unmap_command(cm);
				sc->flags |= AAC_QUEUE_FRZN;
				aac_requeue_ready(cm);
			}
			DELAY(5);			/* wait 5 usec. */
		}
	}
}


static int 
aac_convert_sgraw2(struct aac_softc *sc, struct aac_raw_io2 *raw,
				   int pages, int nseg, int nseg_new)
{
	struct aac_sge_ieee1212 *sge;
	int i, j, pos;
	u_int32_t addr_low;

	sge = malloc(nseg_new * sizeof(struct aac_sge_ieee1212), 
		M_AACRAIDBUF, M_NOWAIT|M_ZERO);
	if (sge == NULL)
		return nseg;

	for (i = 1, pos = 1; i < nseg - 1; ++i) {
		for (j = 0; j < raw->sge[i].length / (pages*PAGE_SIZE); ++j) {
			addr_low = raw->sge[i].addrLow + j * pages * PAGE_SIZE;
			sge[pos].addrLow = addr_low;
			sge[pos].addrHigh = raw->sge[i].addrHigh;
			if (addr_low < raw->sge[i].addrLow)
				sge[pos].addrHigh++;
			sge[pos].length = pages * PAGE_SIZE;
			sge[pos].flags = 0;
			pos++;
		}
	}
	sge[pos] = raw->sge[nseg-1];
	for (i = 1; i < nseg_new; ++i)
		raw->sge[i] = sge[i];

	free(sge, M_AACRAIDBUF);
	raw->sgeCnt = nseg_new;
	raw->flags |= RIO2_SGL_CONFORMANT;
	raw->sgeNominalSize = pages * PAGE_SIZE;
	return nseg_new;
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

	if (cm->cm_datalen != 0 && cm->cm_passthr_dmat == 0) {
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
	u_int32_t code, major, minor, maxsize;
	u_int32_t options = 0, atu_size = 0, status, waitCount;
	time_t then;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* check if flash update is running */
	if (AAC_GET_FWSTATUS(sc) & AAC_FLASH_UPD_PENDING) {
		then = time_uptime;
		do {
			code = AAC_GET_FWSTATUS(sc);
			if (time_uptime > (then + AAC_FWUPD_TIMEOUT)) {
				device_printf(sc->aac_dev,
						  "FATAL: controller not coming ready, "
						   "status %x\n", code);
				return(ENXIO);
			}
		} while (!(code & AAC_FLASH_UPD_SUCCESS) && !(code & AAC_FLASH_UPD_FAILED));
		/* 
		 * Delay 10 seconds. Because right now FW is doing a soft reset,
		 * do not read scratch pad register at this time
		 */
		waitCount = 10 * 10000;
		while (waitCount) {
			DELAY(100);		/* delay 100 microseconds */
			waitCount--;
		}
	}

	/*
	 * Wait for the adapter to come ready.
	 */
	then = time_uptime;
	do {
		code = AAC_GET_FWSTATUS(sc);
		if (time_uptime > (then + AAC_BOOT_TIMEOUT)) {
			device_printf(sc->aac_dev,
				      "FATAL: controller not coming ready, "
					   "status %x\n", code);
			return(ENXIO);
		}
	} while (!(code & AAC_UP_AND_RUNNING) || code == 0xffffffff);

	/*
	 * Retrieve the firmware version numbers.  Dell PERC2/QC cards with
	 * firmware version 1.x are not compatible with this driver.
	 */
	if (sc->flags & AAC_FLAGS_PERC2QC) {
		if (aacraid_sync_command(sc, AAC_MONKER_GETKERNVER, 0, 0, 0, 0,
				     NULL, NULL)) {
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
	if (aacraid_sync_command(sc, AAC_MONKER_GETINFO, 0, 0, 0, 0, &status, NULL)) {
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
			&& (sizeof(bus_addr_t) > 4)
			&& (sc->hint_flags & 0x1)) {
			device_printf(sc->aac_dev,
			    "Enabling 64-bit address support\n");
			sc->flags |= AAC_FLAGS_SG_64BIT;
		}
		if (sc->aac_if.aif_send_command) {
			if ((options & AAC_SUPPORTED_NEW_COMM_TYPE3) ||
				(options & AAC_SUPPORTED_NEW_COMM_TYPE4))
				sc->flags |= AAC_FLAGS_NEW_COMM | AAC_FLAGS_NEW_COMM_TYPE34;
			else if (options & AAC_SUPPORTED_NEW_COMM_TYPE1)
				sc->flags |= AAC_FLAGS_NEW_COMM | AAC_FLAGS_NEW_COMM_TYPE1;
			else if (options & AAC_SUPPORTED_NEW_COMM_TYPE2)
				sc->flags |= AAC_FLAGS_NEW_COMM | AAC_FLAGS_NEW_COMM_TYPE2;
		}
		if (options & AAC_SUPPORTED_64BIT_ARRAYSIZE)
			sc->flags |= AAC_FLAGS_ARRAY_64BIT;
	}

	if (!(sc->flags & AAC_FLAGS_NEW_COMM)) {
		device_printf(sc->aac_dev, "Communication interface not supported!\n");
		return (ENXIO);
	}

	if (sc->hint_flags & 2) {
		device_printf(sc->aac_dev, 
			"Sync. mode enforced by driver parameter. This will cause a significant performance decrease!\n");
		sc->flags |= AAC_FLAGS_SYNC_MODE;
	} else if (sc->flags & AAC_FLAGS_NEW_COMM_TYPE34) {
		device_printf(sc->aac_dev, 
			"Async. mode not supported by current driver, sync. mode enforced.\nPlease update driver to get full performance.\n");
		sc->flags |= AAC_FLAGS_SYNC_MODE;
	}

	/* Check for broken hardware that does a lower number of commands */
	sc->aac_max_fibs = (sc->flags & AAC_FLAGS_256FIBS ? 256:512);

	/* Remap mem. resource, if required */
	if (atu_size > rman_get_size(sc->aac_regs_res0)) {
		bus_release_resource(
			sc->aac_dev, SYS_RES_MEMORY,
			sc->aac_regs_rid0, sc->aac_regs_res0);
		sc->aac_regs_res0 = bus_alloc_resource_anywhere(
			sc->aac_dev, SYS_RES_MEMORY, &sc->aac_regs_rid0,
			atu_size, RF_ACTIVE);
		if (sc->aac_regs_res0 == NULL) {
			sc->aac_regs_res0 = bus_alloc_resource_any(
				sc->aac_dev, SYS_RES_MEMORY,
				&sc->aac_regs_rid0, RF_ACTIVE);
			if (sc->aac_regs_res0 == NULL) {
				device_printf(sc->aac_dev,
					"couldn't allocate register window\n");
				return (ENXIO);
			}
		}
		sc->aac_btag0 = rman_get_bustag(sc->aac_regs_res0);
		sc->aac_bhandle0 = rman_get_bushandle(sc->aac_regs_res0);
	}

	/* Read preferred settings */
	sc->aac_max_fib_size = sizeof(struct aac_fib);
	sc->aac_max_sectors = 128;				/* 64KB */
	sc->aac_max_aif = 1;
	if (sc->flags & AAC_FLAGS_SG_64BIT)
		sc->aac_sg_tablesize = (AAC_FIB_DATASIZE
		 - sizeof(struct aac_blockwrite64))
		 / sizeof(struct aac_sg_entry64);
	else
		sc->aac_sg_tablesize = (AAC_FIB_DATASIZE
		 - sizeof(struct aac_blockwrite))
		 / sizeof(struct aac_sg_entry);

	if (!aacraid_sync_command(sc, AAC_MONKER_GETCOMMPREF, 0, 0, 0, 0, NULL, NULL)) {
		options = AAC_GET_MAILBOX(sc, 1);
		sc->aac_max_fib_size = (options & 0xFFFF);
		sc->aac_max_sectors = (options >> 16) << 1;
		options = AAC_GET_MAILBOX(sc, 2);
		sc->aac_sg_tablesize = (options >> 16);
		options = AAC_GET_MAILBOX(sc, 3);
		sc->aac_max_fibs = ((options >> 16) & 0xFFFF);
		if (sc->aac_max_fibs == 0 || sc->aac_hwif != AAC_HWIF_SRCV)
			sc->aac_max_fibs = (options & 0xFFFF);
		options = AAC_GET_MAILBOX(sc, 4);
		sc->aac_max_aif = (options & 0xFFFF);
		options = AAC_GET_MAILBOX(sc, 5);
		sc->aac_max_msix =(sc->flags & AAC_FLAGS_NEW_COMM_TYPE2) ? options : 0;
	}

	maxsize = sc->aac_max_fib_size + 31;
	if (sc->flags & AAC_FLAGS_NEW_COMM_TYPE1) 
		maxsize += sizeof(struct aac_fib_xporthdr);
	if (maxsize > PAGE_SIZE) {
    	sc->aac_max_fib_size -= (maxsize - PAGE_SIZE);
		maxsize = PAGE_SIZE;
	}
	sc->aac_max_fibs_alloc = PAGE_SIZE / maxsize;

	if (sc->aac_max_fib_size > sizeof(struct aac_fib)) {
		sc->flags |= AAC_FLAGS_RAW_IO;
		device_printf(sc->aac_dev, "Enable Raw I/O\n");
	}
	if ((sc->flags & AAC_FLAGS_RAW_IO) &&
	    (sc->flags & AAC_FLAGS_ARRAY_64BIT)) {
		sc->flags |= AAC_FLAGS_LBA_64BIT;
		device_printf(sc->aac_dev, "Enable 64-bit array\n");
	}

#ifdef AACRAID_DEBUG
	aacraid_get_fw_debug_buffer(sc);
#endif
	return (0);
}

static int
aac_init(struct aac_softc *sc)
{
	struct aac_adapter_init	*ip;
	int i, error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* reset rrq index */
	sc->aac_fibs_pushed_no = 0;
	for (i = 0; i < sc->aac_max_msix; i++)
		sc->aac_host_rrq_idx[i] = i * sc->aac_vector_cap;

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
	ip->NoOfMSIXVectors = sc->aac_max_msix;

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

	ip->InitFlags = AAC_INITFLAGS_NEW_COMM_SUPPORTED;
	if (sc->flags & AAC_FLAGS_NEW_COMM_TYPE1) {
		ip->InitStructRevision = AAC_INIT_STRUCT_REVISION_6;
		ip->InitFlags |= (AAC_INITFLAGS_NEW_COMM_TYPE1_SUPPORTED |
			AAC_INITFLAGS_FAST_JBOD_SUPPORTED);
		device_printf(sc->aac_dev, "New comm. interface type1 enabled\n");
	} else if (sc->flags & AAC_FLAGS_NEW_COMM_TYPE2) {
		ip->InitStructRevision = AAC_INIT_STRUCT_REVISION_7;
		ip->InitFlags |= (AAC_INITFLAGS_NEW_COMM_TYPE2_SUPPORTED |
			AAC_INITFLAGS_FAST_JBOD_SUPPORTED);
		device_printf(sc->aac_dev, "New comm. interface type2 enabled\n");
	}
	ip->MaxNumAif = sc->aac_max_aif;
	ip->HostRRQ_AddrLow = 
		sc->aac_common_busaddr + offsetof(struct aac_common, ac_host_rrq);
	/* always 32-bit address */
	ip->HostRRQ_AddrHigh = 0;

	if (sc->aac_support_opt2 & AAC_SUPPORTED_POWER_MANAGEMENT) {
		ip->InitFlags |= AAC_INITFLAGS_DRIVER_SUPPORTS_PM;
		ip->InitFlags |= AAC_INITFLAGS_DRIVER_USES_UTC_TIME;
		device_printf(sc->aac_dev, "Power Management enabled\n");
	}

	ip->MaxIoCommands = sc->aac_max_fibs;
	ip->MaxIoSize = sc->aac_max_sectors << 9;
	ip->MaxFibSize = sc->aac_max_fib_size;

	/*
	 * Do controller-type-specific initialisation
	 */
	AAC_MEM0_SETREG4(sc, AAC_SRC_ODBR_C, ~0);

	/*
	 * Give the init structure to the controller.
	 */
	if (aacraid_sync_command(sc, AAC_MONKER_INITSTRUCT,
			     sc->aac_common_busaddr +
			     offsetof(struct aac_common, ac_init), 0, 0, 0,
			     NULL, NULL)) {
		device_printf(sc->aac_dev,
			      "error establishing init structure\n");
		error = EIO;
		goto out;
	}

	/*
	 * Check configuration issues 
	 */
	if ((error = aac_check_config(sc)) != 0)
		goto out;

	error = 0;
out:
	return(error);
}

static void
aac_define_int_mode(struct aac_softc *sc)
{
	device_t dev;
	int cap, msi_count, error = 0;
	uint32_t val;
	
	dev = sc->aac_dev;

	/* max. vectors from AAC_MONKER_GETCOMMPREF */
	if (sc->aac_max_msix == 0) {
		sc->aac_max_msix = 1;
		sc->aac_vector_cap = sc->aac_max_fibs;
		return;
	}

	/* OS capability */
	msi_count = pci_msix_count(dev);
	if (msi_count > AAC_MAX_MSIX)
		msi_count = AAC_MAX_MSIX;
	if (msi_count > sc->aac_max_msix)
		msi_count = sc->aac_max_msix;
	if (msi_count == 0 || (error = pci_alloc_msix(dev, &msi_count)) != 0) {
		device_printf(dev, "alloc msix failed - msi_count=%d, err=%d; "
				   "will try MSI\n", msi_count, error);
		pci_release_msi(dev);
	} else {
		sc->msi_enabled = TRUE;
		device_printf(dev, "using MSI-X interrupts (%u vectors)\n",
			msi_count);
	}

	if (!sc->msi_enabled) {
		msi_count = 1;
		if ((error = pci_alloc_msi(dev, &msi_count)) != 0) {
			device_printf(dev, "alloc msi failed - err=%d; "
				           "will use INTx\n", error);
			pci_release_msi(dev);
		} else {
			sc->msi_enabled = TRUE;
			device_printf(dev, "using MSI interrupts\n");
		}
	}

	if (sc->msi_enabled) {
		/* now read controller capability from PCI config. space */
		cap = aac_find_pci_capability(sc, PCIY_MSIX);	
		val = (cap != 0 ? pci_read_config(dev, cap + 2, 2) : 0);	
		if (!(val & AAC_PCI_MSI_ENABLE)) {
			pci_release_msi(dev);
			sc->msi_enabled = FALSE;
		}
	}

	if (!sc->msi_enabled) {
		device_printf(dev, "using legacy interrupts\n");
		sc->aac_max_msix = 1;
	} else {
		AAC_ACCESS_DEVREG(sc, AAC_ENABLE_MSIX);
		if (sc->aac_max_msix > msi_count)
			sc->aac_max_msix = msi_count;
	}
	sc->aac_vector_cap = sc->aac_max_fibs / sc->aac_max_msix;

	fwprintf(sc, HBA_FLAGS_DBG_DEBUG_B, "msi_enabled %d vector_cap %d max_fibs %d max_msix %d",
		sc->msi_enabled,sc->aac_vector_cap, sc->aac_max_fibs, sc->aac_max_msix);
}

static int
aac_find_pci_capability(struct aac_softc *sc, int cap)
{
	device_t dev;
	uint32_t status;
	uint8_t ptr;

	dev = sc->aac_dev;

	status = pci_read_config(dev, PCIR_STATUS, 2);
	if (!(status & PCIM_STATUS_CAPPRESENT))
		return (0);

	status = pci_read_config(dev, PCIR_HDRTYPE, 1);
	switch (status & PCIM_HDRTYPE) {
	case 0:
	case 1:
		ptr = PCIR_CAP_PTR;
		break;
	case 2:
		ptr = PCIR_CAP_PTR_2;
		break;
	default:
		return (0);
		break;
	}
	ptr = pci_read_config(dev, ptr, 1);

	while (ptr != 0) {
		int next, val;
		next = pci_read_config(dev, ptr + PCICAP_NEXTPTR, 1);
		val = pci_read_config(dev, ptr + PCICAP_ID, 1);
		if (val == cap)
			return (ptr);
		ptr = next;
	}

	return (0);
}

static int
aac_setup_intr(struct aac_softc *sc)
{
	int i, msi_count, rid;
	struct resource *res;
	void *tag;

	msi_count = sc->aac_max_msix;
	rid = (sc->msi_enabled ? 1:0);

	for (i = 0; i < msi_count; i++, rid++) {
		if ((res = bus_alloc_resource_any(sc->aac_dev,SYS_RES_IRQ, &rid,
			RF_SHAREABLE | RF_ACTIVE)) == NULL) {
			device_printf(sc->aac_dev,"can't allocate interrupt\n");
			return (EINVAL);
		}
		sc->aac_irq_rid[i] = rid;
		sc->aac_irq[i] = res;
		if (aac_bus_setup_intr(sc->aac_dev, res, 
			INTR_MPSAFE | INTR_TYPE_BIO, NULL, 
			aacraid_new_intr_type1, &sc->aac_msix[i], &tag)) {
			device_printf(sc->aac_dev, "can't set up interrupt\n");
			return (EINVAL);
		}
		sc->aac_msix[i].vector_no = i;
		sc->aac_msix[i].sc = sc;
		sc->aac_intr[i] = tag;
	}

	return (0);
}

static int
aac_check_config(struct aac_softc *sc)
{
	struct aac_fib *fib;
	struct aac_cnt_config *ccfg;
	struct aac_cf_status_hdr *cf_shdr;
	int rval;

	mtx_lock(&sc->aac_io_lock);
	aac_alloc_sync_fib(sc, &fib);

	ccfg = (struct aac_cnt_config *)&fib->data[0];
	bzero(ccfg, sizeof (*ccfg) - CT_PACKET_SIZE);
	ccfg->Command = VM_ContainerConfig;
	ccfg->CTCommand.command = CT_GET_CONFIG_STATUS;
	ccfg->CTCommand.param[CNT_SIZE] = sizeof(struct aac_cf_status_hdr);

	rval = aac_sync_fib(sc, ContainerCommand, 0, fib,
		sizeof (struct aac_cnt_config));
	cf_shdr = (struct aac_cf_status_hdr *)ccfg->CTCommand.data;
	if (rval == 0 && ccfg->Command == ST_OK &&
		ccfg->CTCommand.param[0] == CT_OK) {
		if (cf_shdr->action <= CFACT_PAUSE) {
			bzero(ccfg, sizeof (*ccfg) - CT_PACKET_SIZE);
			ccfg->Command = VM_ContainerConfig;
			ccfg->CTCommand.command = CT_COMMIT_CONFIG;

			rval = aac_sync_fib(sc, ContainerCommand, 0, fib,
				sizeof (struct aac_cnt_config));
			if (rval == 0 && ccfg->Command == ST_OK &&
				ccfg->CTCommand.param[0] == CT_OK) {
				/* successful completion */
				rval = 0;
			} else {
				/* auto commit aborted due to error(s) */
				rval = -2;
			}
		} else {
			/* auto commit aborted due to adapter indicating
			   config. issues too dangerous to auto commit  */
			rval = -3;
		}
	} else {
		/* error */
		rval = -1;
	}

	aac_release_sync_fib(sc);
	mtx_unlock(&sc->aac_io_lock);
	return(rval);
}

/*
 * Send a synchronous command to the controller and wait for a result.
 * Indicate if the controller completed the command with an error status.
 */
int
aacraid_sync_command(struct aac_softc *sc, u_int32_t command,
		 u_int32_t arg0, u_int32_t arg1, u_int32_t arg2, u_int32_t arg3,
		 u_int32_t *sp, u_int32_t *r1)
{
	time_t then;
	u_int32_t status;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* populate the mailbox */
	AAC_SET_MAILBOX(sc, command, arg0, arg1, arg2, arg3);

	/* ensure the sync command doorbell flag is cleared */
	if (!sc->msi_enabled)
		AAC_CLEAR_ISTATUS(sc, AAC_DB_SYNC_COMMAND);

	/* then set it to signal the adapter */
	AAC_QNOTIFY(sc, AAC_DB_SYNC_COMMAND);

	if ((command != AAC_MONKER_SYNCFIB) || (sp == NULL) || (*sp != 0)) {
		/* spin waiting for the command to complete */
		then = time_uptime;
		do {
			if (time_uptime > (then + AAC_SYNC_TIMEOUT)) {
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

		/* return parameter */
		if (r1 != NULL) 
			*r1 = AAC_GET_MAILBOX(sc, 1);

		if (status != AAC_SRB_STS_SUCCESS)
			return (-1);
	}
	return(0);
}

static int
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
	fib->Header.u.ReceiverFibAddress = sc->aac_common_busaddr +
		offsetof(struct aac_common, ac_sync_fib);

	/*
	 * Give the FIB to the controller, wait for a response.
	 */
	if (aacraid_sync_command(sc, AAC_MONKER_SYNCFIB,
		fib->Header.u.ReceiverFibAddress, 0, 0, 0, NULL, NULL)) {
		fwprintf(sc, HBA_FLAGS_DBG_ERROR_B, "IO error");
		return(EIO);
	}

	return (0);
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
	int timedout;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	/*
	 * Traverse the busy command list, bitch about late commands once
	 * only.
	 */
	timedout = 0;
	deadline = time_uptime - AAC_CMD_TIMEOUT;
	TAILQ_FOREACH(cm, &sc->aac_busy, cm_link) {
		if (cm->cm_timestamp < deadline) {
			device_printf(sc->aac_dev,
				      "COMMAND %p TIMEOUT AFTER %d SECONDS\n",
				      cm, (int)(time_uptime-cm->cm_timestamp));
			AAC_PRINT_FIB(sc, cm->cm_fib);
			timedout++;
		}
	}

	if (timedout) 
		aac_reset_adapter(sc);
	aacraid_print_queues(sc);
}

/*
 * Interface Function Vectors
 */

/*
 * Read the current firmware status word.
 */
static int
aac_src_get_fwstatus(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, AAC_SRC_OMR));
}

/*
 * Notify the controller of a change in a given queue
 */
static void
aac_src_qnotify(struct aac_softc *sc, int qbit)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG4(sc, AAC_SRC_IDBR, qbit << AAC_SRC_IDR_SHIFT);
}

/*
 * Get the interrupt reason bits
 */
static int
aac_src_get_istatus(struct aac_softc *sc)
{
	int val;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if (sc->msi_enabled) {
		val = AAC_MEM0_GETREG4(sc, AAC_SRC_ODBR_MSI);
		if (val & AAC_MSI_SYNC_STATUS)
			val = AAC_DB_SYNC_COMMAND;
		else
			val = 0;
	} else {
		val = AAC_MEM0_GETREG4(sc, AAC_SRC_ODBR_R) >> AAC_SRC_ODR_SHIFT;
	}
	return(val);
}

/*
 * Clear some interrupt reason bits
 */
static void
aac_src_clear_istatus(struct aac_softc *sc, int mask)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if (sc->msi_enabled) {
		if (mask == AAC_DB_SYNC_COMMAND)
			AAC_ACCESS_DEVREG(sc, AAC_CLEAR_SYNC_BIT);
	} else {
		AAC_MEM0_SETREG4(sc, AAC_SRC_ODBR_C, mask << AAC_SRC_ODR_SHIFT);
	}
}

/*
 * Populate the mailbox and set the command word
 */
static void
aac_src_set_mailbox(struct aac_softc *sc, u_int32_t command, u_int32_t arg0,
		    u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG4(sc, AAC_SRC_MAILBOX, command);
	AAC_MEM0_SETREG4(sc, AAC_SRC_MAILBOX + 4, arg0);
	AAC_MEM0_SETREG4(sc, AAC_SRC_MAILBOX + 8, arg1);
	AAC_MEM0_SETREG4(sc, AAC_SRC_MAILBOX + 12, arg2);
	AAC_MEM0_SETREG4(sc, AAC_SRC_MAILBOX + 16, arg3);
}

static void
aac_srcv_set_mailbox(struct aac_softc *sc, u_int32_t command, u_int32_t arg0,
		    u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	AAC_MEM0_SETREG4(sc, AAC_SRCV_MAILBOX, command);
	AAC_MEM0_SETREG4(sc, AAC_SRCV_MAILBOX + 4, arg0);
	AAC_MEM0_SETREG4(sc, AAC_SRCV_MAILBOX + 8, arg1);
	AAC_MEM0_SETREG4(sc, AAC_SRCV_MAILBOX + 12, arg2);
	AAC_MEM0_SETREG4(sc, AAC_SRCV_MAILBOX + 16, arg3);
}

/*
 * Fetch the immediate command status word
 */
static int
aac_src_get_mailbox(struct aac_softc *sc, int mb)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, AAC_SRC_MAILBOX + (mb * 4)));
}

static int
aac_srcv_get_mailbox(struct aac_softc *sc, int mb)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(AAC_MEM0_GETREG4(sc, AAC_SRCV_MAILBOX + (mb * 4)));
}

/*
 * Set/clear interrupt masks
 */
static void
aac_src_access_devreg(struct aac_softc *sc, int mode)
{
	u_int32_t val;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	switch (mode) {
	case AAC_ENABLE_INTERRUPT:
		AAC_MEM0_SETREG4(sc, AAC_SRC_OIMR, 
			(sc->msi_enabled ? AAC_INT_ENABLE_TYPE1_MSIX :
				           AAC_INT_ENABLE_TYPE1_INTX));
		break;

	case AAC_DISABLE_INTERRUPT:
		AAC_MEM0_SETREG4(sc, AAC_SRC_OIMR, AAC_INT_DISABLE_ALL);
		break;

	case AAC_ENABLE_MSIX:
		/* set bit 6 */
		val = AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		val |= 0x40;
		AAC_MEM0_SETREG4(sc, AAC_SRC_IDBR, val);		
		AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		/* unmask int. */
		val = PMC_ALL_INTERRUPT_BITS;
		AAC_MEM0_SETREG4(sc, AAC_SRC_IOAR, val);
		val = AAC_MEM0_GETREG4(sc, AAC_SRC_OIMR);
		AAC_MEM0_SETREG4(sc, AAC_SRC_OIMR, 
			val & (~(PMC_GLOBAL_INT_BIT2 | PMC_GLOBAL_INT_BIT0)));
		break;

	case AAC_DISABLE_MSIX:
		/* reset bit 6 */
		val = AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		val &= ~0x40;
		AAC_MEM0_SETREG4(sc, AAC_SRC_IDBR, val);		
		AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		break;

	case AAC_CLEAR_AIF_BIT:
		/* set bit 5 */
		val = AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		val |= 0x20;
		AAC_MEM0_SETREG4(sc, AAC_SRC_IDBR, val);		
		AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		break;

	case AAC_CLEAR_SYNC_BIT:
		/* set bit 4 */
		val = AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		val |= 0x10;
		AAC_MEM0_SETREG4(sc, AAC_SRC_IDBR, val);		
		AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		break;

	case AAC_ENABLE_INTX:
		/* set bit 7 */
		val = AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		val |= 0x80;
		AAC_MEM0_SETREG4(sc, AAC_SRC_IDBR, val);		
		AAC_MEM0_GETREG4(sc, AAC_SRC_IDBR);
		/* unmask int. */
		val = PMC_ALL_INTERRUPT_BITS;
		AAC_MEM0_SETREG4(sc, AAC_SRC_IOAR, val);
		val = AAC_MEM0_GETREG4(sc, AAC_SRC_OIMR);
		AAC_MEM0_SETREG4(sc, AAC_SRC_OIMR, 
			val & (~(PMC_GLOBAL_INT_BIT2)));
		break;
	
	default:
		break;
	}
}

/*
 * New comm. interface: Send command functions
 */
static int
aac_src_send_command(struct aac_softc *sc, struct aac_command *cm)
{
	struct aac_fib_xporthdr *pFibX;
	u_int32_t fibsize, high_addr;
	u_int64_t address;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "send command (new comm. type1)");

	if (sc->msi_enabled && cm->cm_fib->Header.Command != AifRequest &&
		sc->aac_max_msix > 1) { 
		u_int16_t vector_no, first_choice = 0xffff;
	
		vector_no = sc->aac_fibs_pushed_no % sc->aac_max_msix;
		do {
			vector_no += 1;
			if (vector_no == sc->aac_max_msix)
				vector_no = 1;
			if (sc->aac_rrq_outstanding[vector_no] < 
				sc->aac_vector_cap)
				break;
			if (0xffff == first_choice)
				first_choice = vector_no;
			else if (vector_no == first_choice)
				break;
		} while (1);
		if (vector_no == first_choice)
			vector_no = 0;
		sc->aac_rrq_outstanding[vector_no]++;
		if (sc->aac_fibs_pushed_no == 0xffffffff)
			sc->aac_fibs_pushed_no = 0;
		else
			sc->aac_fibs_pushed_no++; 
		
		cm->cm_fib->Header.Handle += (vector_no << 16);
	}		

	if (sc->flags & AAC_FLAGS_NEW_COMM_TYPE2) {
		/* Calculate the amount to the fibsize bits */
		fibsize = (cm->cm_fib->Header.Size + 127) / 128 - 1; 
		/* Fill new FIB header */
		address = cm->cm_fibphys;
		high_addr = (u_int32_t)(address >> 32);
		if (high_addr == 0L) {
			cm->cm_fib->Header.StructType = AAC_FIBTYPE_TFIB2;
			cm->cm_fib->Header.u.TimeStamp = 0L;
		} else {
			cm->cm_fib->Header.StructType = AAC_FIBTYPE_TFIB2_64;
			cm->cm_fib->Header.u.SenderFibAddressHigh = high_addr;
		}
		cm->cm_fib->Header.SenderFibAddress = (u_int32_t)address;
	} else {
		/* Calculate the amount to the fibsize bits */
		fibsize = (sizeof(struct aac_fib_xporthdr) + 
		   cm->cm_fib->Header.Size + 127) / 128 - 1; 
		/* Fill XPORT header */ 
		pFibX = (struct aac_fib_xporthdr *)
			((unsigned char *)cm->cm_fib - sizeof(struct aac_fib_xporthdr));
		pFibX->Handle = cm->cm_fib->Header.Handle;
		pFibX->HostAddress = cm->cm_fibphys;
		pFibX->Size = cm->cm_fib->Header.Size;
		address = cm->cm_fibphys - sizeof(struct aac_fib_xporthdr);
		high_addr = (u_int32_t)(address >> 32);
	}

	if (fibsize > 31) 
		fibsize = 31;
	aac_enqueue_busy(cm);
	if (high_addr) {
		AAC_MEM0_SETREG4(sc, AAC_SRC_IQUE64_H, high_addr);
		AAC_MEM0_SETREG4(sc, AAC_SRC_IQUE64_L, (u_int32_t)address + fibsize);
	} else {
		AAC_MEM0_SETREG4(sc, AAC_SRC_IQUE32, (u_int32_t)address + fibsize);
	}
	return 0;
}

/*
 * New comm. interface: get, set outbound queue index
 */
static int
aac_src_get_outb_queue(struct aac_softc *sc)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return(-1);
}

static void
aac_src_set_outb_queue(struct aac_softc *sc, int index)
{
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
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

	if (sc->supported_options & AAC_SUPPORTED_SUPPLEMENT_ADAPTER_INFO) {
		fib->data[0] = 0;
		if (aac_sync_fib(sc, RequestSupplementAdapterInfo, 0, fib, 1)) 
			device_printf(sc->aac_dev, "RequestSupplementAdapterInfo failed\n");
		else {
			struct aac_supplement_adapter_info *supp_info;

			supp_info = ((struct aac_supplement_adapter_info *)&fib->data[0]); 
			adapter_type = (char *)supp_info->AdapterTypeText;
			sc->aac_feature_bits = supp_info->FeatureBits;
			sc->aac_support_opt2 = supp_info->SupportedOptions2;
		}
	}
	device_printf(sc->aac_dev, "%s, aacraid driver %d.%d.%d-%d\n",
		adapter_type,
		AAC_DRIVER_MAJOR_VERSION, AAC_DRIVER_MINOR_VERSION,
		AAC_DRIVER_BUGFIX_LEVEL, AAC_DRIVER_BUILD);

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

	aac_release_sync_fib(sc);
	mtx_unlock(&sc->aac_io_lock);
}

/*
 * Look up a text description of a numeric error code and return a pointer to
 * same.
 */
static char *
aac_describe_code(struct aac_code_lookup *table, u_int32_t code)
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
#if __FreeBSD_version >= 702000
	device_busy(sc->aac_dev);
	devfs_set_cdevpriv(sc, aac_cdevpriv_dtor);
#endif
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

	mtx_lock(&sc->aac_io_lock);
	if ((poll_events & (POLLRDNORM | POLLIN)) != 0) {
		for (ctx = sc->fibctx; ctx; ctx = ctx->next) {
			if (ctx->ctx_idx != sc->aifq_idx || ctx->ctx_wrap) {
				revents |= poll_events & (POLLIN | POLLRDNORM);
				break;
			}
		}
	}
	mtx_unlock(&sc->aac_io_lock);

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
		if (aacraid_alloc_command(sc, (struct aac_command **)arg)) {
			aacraid_add_event(sc, event);
			return;
		}
		free(event, M_AACRAIDBUF);
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
	if (aacraid_alloc_command(sc, &cm)) {
		struct aac_event *event;

		event = malloc(sizeof(struct aac_event), M_AACRAIDBUF,
		    M_NOWAIT | M_ZERO);
		if (event == NULL) {
			error = EBUSY;
			mtx_unlock(&sc->aac_io_lock);
			goto out;
		}
		event->ev_type = AAC_EVENT_CMFREE;
		event->ev_callback = aac_ioctl_event;
		event->ev_arg = &cm;
		aacraid_add_event(sc, event);
		msleep(cm, &sc->aac_io_lock, 0, "aacraid_ctlsfib", 0);
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
	cm->cm_datalen = 0;

	/*
	 * Pass the FIB to the controller, wait for it to complete.
	 */
	mtx_lock(&sc->aac_io_lock);
	error = aacraid_wait_command(cm);
	mtx_unlock(&sc->aac_io_lock);
	if (error != 0) {
		device_printf(sc->aac_dev,
			      "aacraid_wait_command return %d\n", error);
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
		aacraid_release_command(cm);
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
	struct aac_fib *fib;
	struct aac_srb *srbcmd;
	struct aac_srb *user_srb = (struct aac_srb *)arg;
	void *user_reply;
	int error, transfer_data = 0;
	bus_dmamap_t orig_map = 0;
	u_int32_t fibsize = 0;
	u_int64_t srb_sg_address;
	u_int32_t srb_sg_bytecount;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	cm = NULL;

	mtx_lock(&sc->aac_io_lock);
	if (aacraid_alloc_command(sc, &cm)) {
		struct aac_event *event;

		event = malloc(sizeof(struct aac_event), M_AACRAIDBUF,
		    M_NOWAIT | M_ZERO);
		if (event == NULL) {
			error = EBUSY;
			mtx_unlock(&sc->aac_io_lock);
			goto out;
		}
		event->ev_type = AAC_EVENT_CMFREE;
		event->ev_callback = aac_ioctl_event;
		event->ev_arg = &cm;
		aacraid_add_event(sc, event);
		msleep(cm, &sc->aac_io_lock, 0, "aacraid_ctlsraw", 0);
	}
	mtx_unlock(&sc->aac_io_lock);

	cm->cm_data = NULL;
	/* save original dma map */
	orig_map = cm->cm_datamap;

	fib = cm->cm_fib;
	srbcmd = (struct aac_srb *)fib->data;
	if ((error = copyin((void *)&user_srb->data_len, &fibsize, 
		sizeof (u_int32_t)) != 0)) 
		goto out;
	if (fibsize > (sc->aac_max_fib_size-sizeof(struct aac_fib_header))) {
		error = EINVAL;
		goto out;
	}
	if ((error = copyin((void *)user_srb, srbcmd, fibsize) != 0)) 
		goto out;

	srbcmd->function = 0;		/* SRBF_ExecuteScsi */
	srbcmd->retry_limit = 0;	/* obsolete */

	/* only one sg element from userspace supported */
	if (srbcmd->sg_map.SgCount > 1) {
		error = EINVAL;
		goto out;
	}
	/* check fibsize */
	if (fibsize == (sizeof(struct aac_srb) + 
		srbcmd->sg_map.SgCount * sizeof(struct aac_sg_entry))) {
		struct aac_sg_entry *sgp = srbcmd->sg_map.SgEntry;
		struct aac_sg_entry sg;

		if ((error = copyin(sgp, &sg, sizeof(sg))) != 0)
			goto out;

		srb_sg_bytecount = sg.SgByteCount;
		srb_sg_address = (u_int64_t)sg.SgAddress;
	} else if (fibsize == (sizeof(struct aac_srb) + 
		srbcmd->sg_map.SgCount * sizeof(struct aac_sg_entry64))) {
#ifdef __LP64__
		struct aac_sg_entry64 *sgp = 
			(struct aac_sg_entry64 *)srbcmd->sg_map.SgEntry;
		struct aac_sg_entry64 sg;

		if ((error = copyin(sgp, &sg, sizeof(sg))) != 0)
			goto out;

		srb_sg_bytecount = sg.SgByteCount;
		srb_sg_address = sg.SgAddress;
		if (srb_sg_address > 0xffffffffull && 
			!(sc->flags & AAC_FLAGS_SG_64BIT))
#endif	
		{
			error = EINVAL;
			goto out;
		}
	} else {
		error = EINVAL;
		goto out;
	}
	user_reply = (char *)arg + fibsize;
	srbcmd->data_len = srb_sg_bytecount;
	if (srbcmd->sg_map.SgCount == 1) 
		transfer_data = 1;

	if (transfer_data) {
		/*
		 * Create DMA tag for the passthr. data buffer and allocate it.
		 */
		if (bus_dma_tag_create(sc->aac_parent_dmat, 	/* parent */
			1, 0,			/* algnmnt, boundary */
			(sc->flags & AAC_FLAGS_SG_64BIT) ?
			BUS_SPACE_MAXADDR_32BIT :
			0x7fffffff,		/* lowaddr */
			BUS_SPACE_MAXADDR, 	/* highaddr */
			NULL, NULL, 		/* filter, filterarg */
			srb_sg_bytecount, 	/* size */
			sc->aac_sg_tablesize,	/* nsegments */
			srb_sg_bytecount, 	/* maxsegsize */
			0,			/* flags */
			NULL, NULL,		/* No locking needed */
			&cm->cm_passthr_dmat)) {
			error = ENOMEM;
			goto out;
		}
		if (bus_dmamem_alloc(cm->cm_passthr_dmat, (void **)&cm->cm_data,
			BUS_DMA_NOWAIT, &cm->cm_datamap)) {
			error = ENOMEM;
			goto out;
		}
		/* fill some cm variables */
		cm->cm_datalen = srb_sg_bytecount;
		if (srbcmd->flags & AAC_SRB_FLAGS_DATA_IN) 
			cm->cm_flags |= AAC_CMD_DATAIN;
		if (srbcmd->flags & AAC_SRB_FLAGS_DATA_OUT)
			cm->cm_flags |= AAC_CMD_DATAOUT;

		if (srbcmd->flags & AAC_SRB_FLAGS_DATA_OUT) {
			if ((error = copyin((void *)(uintptr_t)srb_sg_address,
				cm->cm_data, cm->cm_datalen)) != 0)
				goto out;
			/* sync required for bus_dmamem_alloc() alloc. mem.? */
			bus_dmamap_sync(cm->cm_passthr_dmat, cm->cm_datamap,
				BUS_DMASYNC_PREWRITE);
		}
	}

	/* build the FIB */
	fib->Header.Size = sizeof(struct aac_fib_header) + 
		sizeof(struct aac_srb);
	fib->Header.XferState =
		AAC_FIBSTATE_HOSTOWNED   |
		AAC_FIBSTATE_INITIALISED |
		AAC_FIBSTATE_EMPTY	 |
		AAC_FIBSTATE_FROMHOST	 |
		AAC_FIBSTATE_REXPECTED   |
		AAC_FIBSTATE_NORM	 |
		AAC_FIBSTATE_ASYNC;

	fib->Header.Command = (sc->flags & AAC_FLAGS_SG_64BIT) ? 
		ScsiPortCommandU64 : ScsiPortCommand;
	cm->cm_sgtable = (struct aac_sg_table *)&srbcmd->sg_map;

	/* send command */
	if (transfer_data) {
		bus_dmamap_load(cm->cm_passthr_dmat,
			cm->cm_datamap, cm->cm_data,
			cm->cm_datalen,
			aacraid_map_command_sg, cm, 0);
	} else {
		aacraid_map_command_sg(cm, NULL, 0, 0);
	}

	/* wait for completion */
	mtx_lock(&sc->aac_io_lock);
	while (!(cm->cm_flags & AAC_CMD_COMPLETED))
		msleep(cm, &sc->aac_io_lock, 0, "aacraid_ctlsrw2", 0);
	mtx_unlock(&sc->aac_io_lock);

	/* copy data */
	if (transfer_data && (srbcmd->flags & AAC_SRB_FLAGS_DATA_IN)) {
		if ((error = copyout(cm->cm_data,
			(void *)(uintptr_t)srb_sg_address,
			cm->cm_datalen)) != 0)
			goto out;
		/* sync required for bus_dmamem_alloc() allocated mem.? */
		bus_dmamap_sync(cm->cm_passthr_dmat, cm->cm_datamap,
				BUS_DMASYNC_POSTREAD);
	}

	/* status */
	error = copyout(fib->data, user_reply, sizeof(struct aac_srb_response));

out:
	if (cm && cm->cm_data) {
		if (transfer_data)
			bus_dmamap_unload(cm->cm_passthr_dmat, cm->cm_datamap);
		bus_dmamem_free(cm->cm_passthr_dmat, cm->cm_data, cm->cm_datamap);
		cm->cm_datamap = orig_map;
	}
	if (cm && cm->cm_passthr_dmat) 
		bus_dma_tag_destroy(cm->cm_passthr_dmat);
	if (cm) { 
		mtx_lock(&sc->aac_io_lock);
		aacraid_release_command(cm);
		mtx_unlock(&sc->aac_io_lock);
	}
	return(error);
}

/*
 * Request an AIF from the controller (new comm. type1)
 */
static void
aac_request_aif(struct aac_softc *sc)
{
	struct aac_command *cm;
	struct aac_fib *fib;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if (aacraid_alloc_command(sc, &cm)) {
		sc->aif_pending = 1;
		return;
	}
	sc->aif_pending = 0;
    
	/* build the FIB */
	fib = cm->cm_fib;
	fib->Header.Size = sizeof(struct aac_fib);
	fib->Header.XferState =
        AAC_FIBSTATE_HOSTOWNED   |
        AAC_FIBSTATE_INITIALISED |
        AAC_FIBSTATE_EMPTY	 |
        AAC_FIBSTATE_FROMHOST	 |
        AAC_FIBSTATE_REXPECTED   |
        AAC_FIBSTATE_NORM	 |
        AAC_FIBSTATE_ASYNC;
	/* set AIF marker */
	fib->Header.Handle = 0x00800000;
	fib->Header.Command = AifRequest;
	((struct aac_aif_command *)fib->data)->command = AifReqEvent;

	aacraid_map_command_sg(cm, NULL, 0, 0);
}


#if __FreeBSD_version >= 702000
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
#else
static int
aac_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct aac_softc *sc;

	sc = dev->si_drv1;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	return 0;
}
#endif

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
	struct aac_fib *sync_fib;
	struct aac_mntinforesp mir;
	int next, current, found;
	int count = 0, changed = 0, i = 0;
	u_int32_t channel, uid;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	aif = (struct aac_aif_command*)&fib->data[0];
	aacraid_print_aif(sc, aif);

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
			aac_alloc_sync_fib(sc, &sync_fib);
			do {
				/*
				 * Ask the controller for its containers one at
				 * a time.
				 * XXX What if the controller's list changes
				 * midway through this enumaration?
				 * XXX This should be done async.
				 */
				if (aac_get_container_info(sc, sync_fib, i, 
					&mir, &uid) != 0)
					continue;
				if (i == 0)
					count = mir.MntRespCount;
				/*
				 * Check the container against our list.
				 * co->co_found was already set to 0 in a
				 * previous run.
				 */
				if ((mir.Status == ST_OK) &&
				    (mir.MntTable[0].VolType != CT_NONE)) {
					found = 0;
					TAILQ_FOREACH(co,
						      &sc->aac_container_tqh,
						      co_link) {
						if (co->co_mntobj.ObjectId ==
						    mir.MntTable[0].ObjectId) {
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
					aac_add_container(sc, &mir, 1, uid);
					changed = 1;
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
					co_next = TAILQ_NEXT(co, co_link);
					TAILQ_REMOVE(&sc->aac_container_tqh, co,
						     co_link);
					free(co, M_AACRAIDBUF);
					changed = 1;
					co = co_next;
				} else {
					co->co_found = 0;
					co = TAILQ_NEXT(co, co_link);
				}
			}

			/* Attach the newly created containers */
			if (changed) {
				if (sc->cam_rescan_cb != NULL) 
					sc->cam_rescan_cb(sc, 0,
				    	AAC_CAM_TARGET_WILDCARD);
			}

			break;

		case AifEnEnclosureManagement:
			switch (aif->data.EN.data.EEE.eventType) {
			case AIF_EM_DRIVE_INSERTION:
			case AIF_EM_DRIVE_REMOVAL:
				channel = aif->data.EN.data.EEE.unitID;
				if (sc->cam_rescan_cb != NULL)
					sc->cam_rescan_cb(sc,
					    ((channel>>24) & 0xF) + 1,
					    (channel & 0xFFFF));
				break;
			}
			break;

		case AifEnAddJBOD:
		case AifEnDeleteJBOD:
		case AifRawDeviceRemove:
			channel = aif->data.EN.data.ECE.container;
			if (sc->cam_rescan_cb != NULL)
				sc->cam_rescan_cb(sc, ((channel>>24) & 0xF) + 1,
				    AAC_CAM_TARGET_WILDCARD);
			break;

		default:
			break;
		}

	default:
		break;
	}

	/* Copy the AIF data to the AIF queue for ioctl retrieval */
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

	return;
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

	fibctx = malloc(sizeof(struct aac_fib_context), M_AACRAIDBUF, M_NOWAIT|M_ZERO);
	if (fibctx == NULL)
		return (ENOMEM);

	mtx_lock(&sc->aac_io_lock);
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

	error = copyout(&fibctx->unique, (void *)arg, sizeof(u_int32_t));
	mtx_unlock(&sc->aac_io_lock);
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

	mtx_lock(&sc->aac_io_lock);
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
	if (ctx)
		free(ctx, M_AACRAIDBUF);

	mtx_unlock(&sc->aac_io_lock);
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

	mtx_lock(&sc->aac_io_lock);
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
		if (!ctx) {
			mtx_unlock(&sc->aac_io_lock);
			return (EFAULT);
		}

		error = aac_return_aif(sc, ctx, agf.AifFib);
		if (error == EAGAIN && agf.Wait) {
			fwprintf(sc, HBA_FLAGS_DBG_AIF_B, "aac_getnext_aif(): waiting for AIF");
			sc->aac_state |= AAC_STATE_AIF_SLEEPER;
			while (error == EAGAIN) {
				mtx_unlock(&sc->aac_io_lock);
				error = tsleep(sc->aac_aifq, PRIBIO |
					       PCATCH, "aacaif", 0);
				mtx_lock(&sc->aac_io_lock);
				if (error == 0)
					error = aac_return_aif(sc, ctx, agf.AifFib);
			}
			sc->aac_state &= ~AAC_STATE_AIF_SLEEPER;
		}
	}
	mtx_unlock(&sc->aac_io_lock);
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

	current = ctx->ctx_idx;
	if (current == sc->aifq_idx && !ctx->ctx_wrap) {
		/* empty */
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
		f.feat.fBits.JBODSupport = 1;
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
	int error, id;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	mtx_lock(&sc->aac_io_lock);
	error = copyin(uptr, (caddr_t)&query_disk,
		       sizeof(struct aac_query_disk));
	if (error) {
		mtx_unlock(&sc->aac_io_lock);
		return (error);
	}

	id = query_disk.ContainerNumber;
	if (id == -1) {
		mtx_unlock(&sc->aac_io_lock);
		return (EINVAL);
	}

	TAILQ_FOREACH(co, &sc->aac_container_tqh, co_link) {
		if (co->co_mntobj.ObjectId == id)
			break;
		}

	if (co == NULL) {
			query_disk.Valid = 0;
			query_disk.Locked = 0;
			query_disk.Deleted = 1;		/* XXX is this right? */
	} else {
		query_disk.Valid = 1;
		query_disk.Locked = 1;
		query_disk.Deleted = 0;
		query_disk.Bus = device_get_unit(sc->aac_dev);
		query_disk.Target = 0;
		query_disk.Lun = 0;
		query_disk.UnMapped = 0;
	}

	error = copyout((caddr_t)&query_disk, uptr,
			sizeof(struct aac_query_disk));

	mtx_unlock(&sc->aac_io_lock);
	return (error);
}

static void
aac_container_bus(struct aac_softc *sc)
{
	struct aac_sim *sim;
	device_t child;

	sim =(struct aac_sim *)malloc(sizeof(struct aac_sim),
		M_AACRAIDBUF, M_NOWAIT | M_ZERO);
	if (sim == NULL) {
		device_printf(sc->aac_dev,
	    	"No memory to add container bus\n");
		panic("Out of memory?!");
	}
	child = device_add_child(sc->aac_dev, "aacraidp", -1);
	if (child == NULL) {
		device_printf(sc->aac_dev,
	    	"device_add_child failed for container bus\n");
		free(sim, M_AACRAIDBUF);
		panic("Out of memory?!");
	}

	sim->TargetsPerBus = AAC_MAX_CONTAINERS;
	sim->BusNumber = 0;
	sim->BusType = CONTAINER_BUS;
	sim->InitiatorBusId = -1;
	sim->aac_sc = sc;
	sim->sim_dev = child;
	sim->aac_cam = NULL;

	device_set_ivars(child, sim);
	device_set_desc(child, "Container Bus");
	TAILQ_INSERT_TAIL(&sc->aac_sim_tqh, sim, sim_link);
	/*
	device_set_desc(child, aac_describe_code(aac_container_types,
			mir->MntTable[0].VolType));
	*/
	bus_generic_attach(sc->aac_dev);
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
	int i, error;

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

	for (i = 0; i < businfo.BusCount; i++) {
		if (businfo.BusValid[i] != AAC_BUS_VALID)
			continue;

		caminf = (struct aac_sim *)malloc( sizeof(struct aac_sim),
		    M_AACRAIDBUF, M_NOWAIT | M_ZERO);
		if (caminf == NULL) {
			device_printf(sc->aac_dev,
			    "No memory to add passthrough bus %d\n", i);
			break;
		}

		child = device_add_child(sc->aac_dev, "aacraidp", -1);
		if (child == NULL) {
			device_printf(sc->aac_dev,
			    "device_add_child failed for passthrough bus %d\n",
			    i);
			free(caminf, M_AACRAIDBUF);
			break;
		}

		caminf->TargetsPerBus = businfo.TargetsPerBus;
		caminf->BusNumber = i+1;
		caminf->BusType = PASSTHROUGH_BUS;
		caminf->InitiatorBusId = businfo.InitiatorBusId[i];
		caminf->aac_sc = sc;
		caminf->sim_dev = child;
		caminf->aac_cam = NULL;

		device_set_ivars(child, caminf);
		device_set_desc(child, "SCSI Passthrough Bus");
		TAILQ_INSERT_TAIL(&sc->aac_sim_tqh, caminf, sim_link);
	}
}

/*
 * Check to see if the kernel is up and running. If we are in a
 * BlinkLED state, return the BlinkLED code.
 */
static u_int32_t
aac_check_adapter_health(struct aac_softc *sc, u_int8_t *bled)
{
	u_int32_t ret;

	ret = AAC_GET_FWSTATUS(sc);

	if (ret & AAC_UP_AND_RUNNING)
		ret = 0;
	else if (ret & AAC_KERNEL_PANIC && bled)
		*bled = (ret >> 16) & 0xff;

	return (ret);
}

/*
 * Once do an IOP reset, basically have to re-initialize the card as
 * if coming up from a cold boot, and the driver is responsible for
 * any IO that was outstanding to the adapter at the time of the IOP
 * RESET. And prepare the driver for IOP RESET by making the init code
 * modular with the ability to call it from multiple places.
 */
static int
aac_reset_adapter(struct aac_softc *sc)
{
	struct aac_command *cm;
	struct aac_fib *fib;
	struct aac_pause_command *pc;
	u_int32_t status, reset_mask, waitCount, max_msix_orig;
	int msi_enabled_orig;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	if (sc->aac_state & AAC_STATE_RESET) {
		device_printf(sc->aac_dev, "aac_reset_adapter() already in progress\n");
		return (EINVAL);
	}
	sc->aac_state |= AAC_STATE_RESET;

	/* disable interrupt */
	AAC_ACCESS_DEVREG(sc, AAC_DISABLE_INTERRUPT);

	/*
	 * Abort all pending commands:
	 * a) on the controller
	 */
	while ((cm = aac_dequeue_busy(sc)) != NULL) {
		cm->cm_flags |= AAC_CMD_RESET;

		/* is there a completion handler? */
		if (cm->cm_complete != NULL) {
			cm->cm_complete(cm);
		} else {
			/* assume that someone is sleeping on this
			 * command
			 */
			wakeup(cm);
		}
	}

	/* b) in the waiting queues */
	while ((cm = aac_dequeue_ready(sc)) != NULL) {
		cm->cm_flags |= AAC_CMD_RESET;

		/* is there a completion handler? */
		if (cm->cm_complete != NULL) {
			cm->cm_complete(cm);
		} else {
			/* assume that someone is sleeping on this
			 * command
			 */
			wakeup(cm);
		}
	}

	/* flush drives */
	if (aac_check_adapter_health(sc, NULL) == 0) {
		mtx_unlock(&sc->aac_io_lock);
		(void) aacraid_shutdown(sc->aac_dev);
		mtx_lock(&sc->aac_io_lock);
	}

	/* execute IOP reset */
	if (sc->aac_support_opt2 & AAC_SUPPORTED_MU_RESET) {
		AAC_MEM0_SETREG4(sc, AAC_IRCSR, AAC_IRCSR_CORES_RST);

		/* We need to wait for 5 seconds before accessing the MU again
		 * 10000 * 100us = 1000,000us = 1000ms = 1s  
		 */
		waitCount = 5 * 10000;
		while (waitCount) {
			DELAY(100);			/* delay 100 microseconds */
			waitCount--;
		}
	} else if ((aacraid_sync_command(sc, 
		AAC_IOP_RESET_ALWAYS, 0, 0, 0, 0, &status, &reset_mask)) != 0) {
		/* call IOP_RESET for older firmware */
		if ((aacraid_sync_command(sc,
			AAC_IOP_RESET, 0, 0, 0, 0, &status, NULL)) != 0) {

			if (status == AAC_SRB_STS_INVALID_REQUEST)
				device_printf(sc->aac_dev, "IOP_RESET not supported\n");
			else
				/* probably timeout */
				device_printf(sc->aac_dev, "IOP_RESET failed\n");

			/* unwind aac_shutdown() */
			aac_alloc_sync_fib(sc, &fib);
			pc = (struct aac_pause_command *)&fib->data[0];
			pc->Command = VM_ContainerConfig;
			pc->Type = CT_PAUSE_IO;
			pc->Timeout = 1;
			pc->Min = 1;
			pc->NoRescan = 1;

			(void) aac_sync_fib(sc, ContainerCommand, 0, fib,
				sizeof (struct aac_pause_command));
			aac_release_sync_fib(sc);

			goto finish;
		}
	} else if (sc->aac_support_opt2 & AAC_SUPPORTED_DOORBELL_RESET) {
		AAC_MEM0_SETREG4(sc, AAC_SRC_IDBR, reset_mask);
		/* 
		 * We need to wait for 5 seconds before accessing the doorbell
		 * again, 10000 * 100us = 1000,000us = 1000ms = 1s  
		 */
		waitCount = 5 * 10000;
		while (waitCount) {
			DELAY(100);		/* delay 100 microseconds */
			waitCount--;
		}
	}

	/*
	 * Initialize the adapter.
	 */
	max_msix_orig = sc->aac_max_msix;
	msi_enabled_orig = sc->msi_enabled;
	sc->msi_enabled = FALSE;
	if (aac_check_firmware(sc) != 0)
		goto finish;
	if (!(sc->flags & AAC_FLAGS_SYNC_MODE)) {
		sc->aac_max_msix = max_msix_orig;
		if (msi_enabled_orig) {
			sc->msi_enabled = msi_enabled_orig;
			AAC_ACCESS_DEVREG(sc, AAC_ENABLE_MSIX);
		}
		mtx_unlock(&sc->aac_io_lock);
		aac_init(sc);
		mtx_lock(&sc->aac_io_lock);
	}

finish:
	sc->aac_state &= ~AAC_STATE_RESET;
	AAC_ACCESS_DEVREG(sc, AAC_ENABLE_INTERRUPT);
	aacraid_startio(sc);
	return (0);
}
