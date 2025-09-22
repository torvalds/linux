/*	$OpenBSD: aac.c,v 1.97 2024/09/01 03:08:56 jsg Exp $	*/

/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2001 Scott Long
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001 Adaptec, Inc.
 * Copyright (c) 2000 Niklas Hallqvist
 * Copyright (c) 2004 Nathan Binkert
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
 *
 *	$FreeBSD: /c/ncvs/src/sys/dev/aac/aac.c,v 1.1 2000/09/13 03:20:34 msmith Exp $
 */

/*
 * Driver for the Adaptec 'FSA' family of PCI/SCSI RAID adapters.
 */

/*
 * This driver would not have rewritten for OpenBSD if it was not for the
 * hardware donation from Nocom.  I want to thank them for their support.
 * Of course, credit should go to Mike Smith for the original work he did
 * in the FreeBSD driver where I found lots of reusable code and inspiration.
 * - Niklas Hallqvist
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>
#include <sys/time.h>

#include <machine/bus.h>

#include <uvm/uvm_extern.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/aacreg.h>
#include <dev/ic/aacvar.h>
#include <dev/ic/aac_tables.h>

/* Geometry constants. */
#define AAC_MAXCYLS		1024
#define AAC_HEADS		64
#define AAC_SECS		32	/* mapping 64*32 */
#define AAC_MEDHEADS		127
#define AAC_MEDSECS		63	/* mapping 127*63 */
#define AAC_BIGHEADS		255
#define AAC_BIGSECS		63	/* mapping 255*63 */
#define AAC_SECS32		0x1f	/* round capacity */

struct scsi_xfer;

char   *aac_describe_code(struct aac_code_lookup *, u_int32_t);
void	aac_describe_controller(struct aac_softc *);
int	aac_enqueue_fib(struct aac_softc *, int, struct aac_command *);
int	aac_dequeue_fib(struct aac_softc *, int, u_int32_t *,
			struct aac_fib **);
int	aac_enqueue_response(struct aac_softc *sc, int queue,
			     struct aac_fib *fib);

void	aac_eval_mapping(u_int32_t, int *, int *, int *);
void	aac_print_printf(struct aac_softc *);
int	aac_init(struct aac_softc *);
int	aac_check_firmware(struct aac_softc *);
void	aac_internal_cache_cmd(struct scsi_xfer *);

/* Command Processing */
void	aac_timeout(struct aac_softc *);
void	aac_command_timeout(struct aac_command *);
int	aac_map_command(struct aac_command *);
void	aac_complete(void *);
int	aac_bio_command(struct aac_softc *, struct aac_command **);
void	aac_bio_complete(struct aac_command *);
int	aac_wait_command(struct aac_command *, int);
void	aac_create_thread(void *);
void	aac_command_thread(void *);

/* Command Buffer Management */
void	aac_map_command_sg(void *, bus_dma_segment_t *, int, int);
int	aac_alloc_commands(struct aac_softc *);
void	aac_free_commands(struct aac_softc *);
void	aac_unmap_command(struct aac_command *);
int	aac_wait_command(struct aac_command *, int);
void *	aac_alloc_command(void *);
void	aac_scrub_command(struct aac_command *);
void	aac_release_command(void *, void *);
int	aac_alloc_sync_fib(struct aac_softc *, struct aac_fib **, int);
void	aac_release_sync_fib(struct aac_softc *);
int	aac_sync_fib(struct aac_softc *, u_int32_t, u_int32_t,
	    struct aac_fib *, u_int16_t);

void	aac_scsi_cmd(struct scsi_xfer *);
void	aac_startio(struct aac_softc *);
void	aac_startup(struct aac_softc *);
int	aac_sync_command(struct aac_softc *, u_int32_t, u_int32_t,
    u_int32_t, u_int32_t, u_int32_t, u_int32_t *);

struct cfdriver aac_cd = {
	NULL, "aac", DV_DULL
};

const struct scsi_adapter aac_switch = {
	aac_scsi_cmd, NULL, NULL, NULL, NULL
};

/* Falcon/PPC interface */
int	aac_fa_get_fwstatus(struct aac_softc *);
void	aac_fa_qnotify(struct aac_softc *, int);
int	aac_fa_get_istatus(struct aac_softc *);
void	aac_fa_clear_istatus(struct aac_softc *, int);
void	aac_fa_set_mailbox(struct aac_softc *, u_int32_t, u_int32_t, u_int32_t,
			   u_int32_t, u_int32_t);
int	aac_fa_get_mailbox(struct aac_softc *, int);
void	aac_fa_set_interrupts(struct aac_softc *, int);

struct aac_interface aac_fa_interface = {
	aac_fa_get_fwstatus,
	aac_fa_qnotify,
	aac_fa_get_istatus,
	aac_fa_clear_istatus,
	aac_fa_set_mailbox,
	aac_fa_get_mailbox,
	aac_fa_set_interrupts
};

/* StrongARM interface */
int	aac_sa_get_fwstatus(struct aac_softc *);
void	aac_sa_qnotify(struct aac_softc *, int);
int	aac_sa_get_istatus(struct aac_softc *);
void	aac_sa_clear_istatus(struct aac_softc *, int);
void	aac_sa_set_mailbox(struct aac_softc *, u_int32_t, u_int32_t,
    u_int32_t, u_int32_t, u_int32_t);
int	aac_sa_get_mailbox(struct aac_softc *, int);
void	aac_sa_set_interrupts(struct aac_softc *, int);

struct aac_interface aac_sa_interface = {
	aac_sa_get_fwstatus,
	aac_sa_qnotify,
	aac_sa_get_istatus,
	aac_sa_clear_istatus,
	aac_sa_set_mailbox,
	aac_sa_get_mailbox,
	aac_sa_set_interrupts
};

/* i960Rx interface */
int	aac_rx_get_fwstatus(struct aac_softc *);
void	aac_rx_qnotify(struct aac_softc *, int);
int	aac_rx_get_istatus(struct aac_softc *);
void	aac_rx_clear_istatus(struct aac_softc *, int);
void	aac_rx_set_mailbox(struct aac_softc *, u_int32_t, u_int32_t,
    u_int32_t, u_int32_t, u_int32_t);
int	aac_rx_get_mailbox(struct aac_softc *, int);
void	aac_rx_set_interrupts(struct aac_softc *, int);

struct aac_interface aac_rx_interface = {
	aac_rx_get_fwstatus,
	aac_rx_qnotify,
	aac_rx_get_istatus,
	aac_rx_clear_istatus,
	aac_rx_set_mailbox,
	aac_rx_get_mailbox,
	aac_rx_set_interrupts
};

/* Rocket/MIPS interface */
int	aac_rkt_get_fwstatus(struct aac_softc *);
void	aac_rkt_qnotify(struct aac_softc *, int);
int	aac_rkt_get_istatus(struct aac_softc *);
void	aac_rkt_clear_istatus(struct aac_softc *, int);
void	aac_rkt_set_mailbox(struct aac_softc *, u_int32_t,
				    u_int32_t, u_int32_t,
				    u_int32_t, u_int32_t);
int	aac_rkt_get_mailbox(struct aac_softc *, int);
void	aac_rkt_set_interrupts(struct aac_softc *, int);

struct aac_interface aac_rkt_interface = {
	aac_rkt_get_fwstatus,
	aac_rkt_qnotify,
	aac_rkt_get_istatus,
	aac_rkt_clear_istatus,
	aac_rkt_set_mailbox,
	aac_rkt_get_mailbox,
	aac_rkt_set_interrupts
};

#ifdef AAC_DEBUG
int	aac_debug = AAC_DEBUG;
#endif

int
aac_attach(struct aac_softc *sc)
{
	struct scsibus_attach_args saa;
	int error;

	/*
	 * Initialise per-controller queues.
	 */
	mtx_init(&sc->aac_free_mtx, IPL_BIO);
	aac_initq_free(sc);
	aac_initq_ready(sc);
	aac_initq_busy(sc);
	aac_initq_bio(sc);

	/* disable interrupts before we enable anything */
	AAC_MASK_INTERRUPTS(sc);

	/* mark controller as suspended until we get ourselves organised */
	sc->aac_state |= AAC_STATE_SUSPEND;

	/*
	 * Check that the firmware on the card is supported.
	 */
	error = aac_check_firmware(sc);
	if (error)
		return (error);

	/*
	 * Initialize locks
	 */
	AAC_LOCK_INIT(&sc->aac_sync_lock, "AAC sync FIB lock");
	AAC_LOCK_INIT(&sc->aac_aifq_lock, "AAC AIF lock");
	AAC_LOCK_INIT(&sc->aac_io_lock, "AAC I/O lock");
	AAC_LOCK_INIT(&sc->aac_container_lock, "AAC container lock");
	TAILQ_INIT(&sc->aac_container_tqh);

	/* Initialize the local AIF queue pointers */
	sc->aac_aifq_head = sc->aac_aifq_tail = AAC_AIFQ_LENGTH;

	/*
	 * Initialise the adapter.
	 */
	error = aac_init(sc);
	if (error)
		return (error);


	saa.saa_adapter_softc = sc;
	saa.saa_adapter = &aac_switch;
	saa.saa_adapter_buswidth = AAC_MAX_CONTAINERS;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_luns = 8;
	saa.saa_openings = (sc->total_fibs - 8) /
	    (sc->aac_container_count ? sc->aac_container_count : 1);
	saa.saa_pool = &sc->aac_iopool;
	saa.saa_wwpn = saa.saa_wwnn = 0;
	saa.saa_quirks = saa.saa_flags = 0;

	config_found(&sc->aac_dev, &saa, scsiprint);

	/* Create the AIF thread */
	sc->aifthread = 0;
	sc->aifflags = 0;
	kthread_create_deferred(aac_create_thread, sc);

	return (0);
}

void
aac_create_thread(void *arg)
{
	struct aac_softc *sc = arg;

	if (kthread_create(aac_command_thread, sc, &sc->aifthread,
	    sc->aac_dev.dv_xname)) {
		/* TODO disable aac */
		printf("%s: failed to create kernel thread, disabled",
		sc->aac_dev.dv_xname);
	}
	AAC_DPRINTF(AAC_D_MISC, ("%s: aac_create_thread\n",
	    sc->aac_dev.dv_xname));

}

/*
 * Probe for containers, create disks.
 */
void
aac_startup(struct aac_softc *sc)
{
	struct aac_fib *fib;
	struct aac_mntinfo *mi;
	struct aac_mntinforesp *mir = NULL;
	int count = 0, i = 0;


	aac_alloc_sync_fib(sc, &fib, 0);
	mi = (struct aac_mntinfo *)&fib->data[0];

	AAC_DPRINTF(AAC_D_MISC, ("%s: aac startup\n", sc->aac_dev.dv_xname));

	sc->aac_container_count = 0;
	/* loop over possible containers */
	do {
		/* request information on this container */
		bzero(mi, sizeof(struct aac_mntinfo));
		mi->Command = VM_NameServe;
		mi->MntType = FT_FILESYS;
		mi->MntCount = i;
		if (aac_sync_fib(sc, ContainerCommand, 0, fib,
				 sizeof(struct aac_mntinfo))) {
			printf("%s: error probing container %d\n",
			       sc->aac_dev.dv_xname, i);
			continue;
		}

		mir = (struct aac_mntinforesp *)&fib->data[0];
		/* XXX Need to check if count changed */
		count = mir->MntRespCount;

		/*
		 * Check container volume type for validity.  Note
		 * that many of the possible types may never show up.
		 */
		if (mir->Status == ST_OK &&
		    mir->MntTable[0].VolType != CT_NONE) {
			int drv_cyls, drv_hds, drv_secs;

			AAC_DPRINTF(AAC_D_MISC,
			    ("%s: %d: id %x  name '%.16s'  size %u  type %d\n",
			     sc->aac_dev.dv_xname, i,
			     mir->MntTable[0].ObjectId,
			     mir->MntTable[0].FileSystemName,
			     mir->MntTable[0].Capacity,
			     mir->MntTable[0].VolType));

			sc->aac_container_count++;
			sc->aac_hdr[i].hd_present = 1;
			sc->aac_hdr[i].hd_size = mir->MntTable[0].Capacity;

			/*
			 * Evaluate mapping (sectors per head, heads per cyl)
			 */
			sc->aac_hdr[i].hd_size &= ~AAC_SECS32;
			aac_eval_mapping(sc->aac_hdr[i].hd_size, &drv_cyls,
					 &drv_hds, &drv_secs);
			sc->aac_hdr[i].hd_heads = drv_hds;
			sc->aac_hdr[i].hd_secs = drv_secs;
			/* Round the size */
			sc->aac_hdr[i].hd_size = drv_cyls * drv_hds * drv_secs;

			sc->aac_hdr[i].hd_devtype = mir->MntTable[0].VolType;

			/* XXX Save the name too for use in IDENTIFY later */
		}

		i++;
	} while ((i < count) && (i < AAC_MAX_CONTAINERS));

	aac_release_sync_fib(sc);

	/* mark the controller up */
	sc->aac_state &= ~AAC_STATE_SUSPEND;

	/* enable interrupts now */
	AAC_UNMASK_INTERRUPTS(sc);
}

/*
 * Take an interrupt.
 */
int
aac_intr(void *arg)
{
	struct aac_softc *sc = arg;
	u_int16_t reason;


	/*
	 * Read the status register directly.  This is faster than taking the
	 * driver lock and reading the queues directly.  It also saves having
	 * to turn parts of the driver lock into a spin mutex, which would be
	 * ugly.
	 */
	reason = AAC_GET_ISTATUS(sc);
	AAC_CLEAR_ISTATUS(sc, reason);
	(void)AAC_GET_ISTATUS(sc);

	if (reason == 0)
		return (0);

	AAC_DPRINTF(AAC_D_INTR, ("%s: intr: sc=%p: reason=%#x\n",
				 sc->aac_dev.dv_xname, sc, reason));

	/* controller wants to talk to us */
	if (reason & (AAC_DB_PRINTF | AAC_DB_COMMAND_READY |
		      AAC_DB_RESPONSE_READY)) {

		if (reason & AAC_DB_RESPONSE_READY) {
			/* handle completion processing */
			if (sc->aifflags & AAC_AIFFLAGS_RUNNING) {
				sc->aifflags |= AAC_AIFFLAGS_COMPLETE;
			} else {
				AAC_LOCK_ACQUIRE(&sc->aac_io_lock);
				aac_complete(sc);
				AAC_LOCK_RELEASE(&sc->aac_io_lock);
			}
		}


		/*
		 * XXX Make sure that we don't get fooled by strange messages
		 * that start with a NULL.
		 */
		if (reason & AAC_DB_PRINTF)
			if (sc->aac_common->ac_printf[0] == 0)
				sc->aac_common->ac_printf[0] = 32;

		/*
		 * This might miss doing the actual wakeup.  However, the
		 * msleep that this is waking up has a timeout, so it will
		 * wake up eventually.  AIFs and printfs are low enough
		 * priority that they can handle hanging out for a few seconds
		 * if needed.
		 */
		if (sc->aifthread)
			wakeup(sc->aifthread);

	}

	return (1);
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

	AAC_DPRINTF(AAC_D_CMD, ("%s: start command", sc->aac_dev.dv_xname));

	if (sc->flags & AAC_QUEUE_FRZN) {
		AAC_DPRINTF(AAC_D_CMD, (": queue frozen"));
		return;
	}

	AAC_DPRINTF(AAC_D_CMD, ("\n"));

	for (;;) {
		/*
		 * Try to get a command that's been put off for lack of
		 * resources
		 */
		cm = aac_dequeue_ready(sc);

		/*
		 * Try to build a command off the bio queue (ignore error
		 * return)
		 */
		if (cm == NULL) {
			AAC_DPRINTF(AAC_D_CMD, ("\n"));
			aac_bio_command(sc, &cm);
			AAC_DPRINTF(AAC_D_CMD, ("%s: start done bio",
						sc->aac_dev.dv_xname));
		}

		/* nothing to do? */
		if (cm == NULL)
			break;

		/*
		 * Try to give the command to the controller.  Any error is
		 * catastrophic since it means that bus_dmamap_load() failed.
		 */
		if (aac_map_command(cm) != 0)
			panic("aac: error mapping command %p", cm);

		AAC_DPRINTF(AAC_D_CMD, ("\n%s: another command",
					sc->aac_dev.dv_xname));
	}

	AAC_DPRINTF(AAC_D_CMD, ("\n"));
}

/*
 * Deliver a command to the controller; allocate controller resources at the
 * last moment when possible.
 */
int
aac_map_command(struct aac_command *cm)
{
	struct aac_softc *sc = cm->cm_sc;
	int error = 0;

	AAC_DPRINTF(AAC_D_CMD, (": map command"));

	/* don't map more than once */
	if (cm->cm_flags & AAC_CMD_MAPPED)
		panic("aac: command %p already mapped", cm);

	if (cm->cm_datalen != 0) {
		error = bus_dmamap_load(sc->aac_dmat, cm->cm_datamap,
					cm->cm_data, cm->cm_datalen, NULL,
					BUS_DMA_NOWAIT);
		if (error)
			return (error);

		aac_map_command_sg(cm, cm->cm_datamap->dm_segs,
				   cm->cm_datamap->dm_nsegs, 0);
	} else {
		aac_map_command_sg(cm, NULL, 0, 0);
	}

	return (error);
}

/*
 * Handle notification of one or more FIBs coming from the controller.
 */
void
aac_command_thread(void *arg)
{
	struct aac_softc *sc = arg;
	struct aac_fib *fib;
	u_int32_t fib_size;
	int size, retval;

	AAC_DPRINTF(AAC_D_THREAD, ("%s: aac_command_thread: starting\n",
	    sc->aac_dev.dv_xname));
	AAC_LOCK_ACQUIRE(&sc->aac_io_lock);
	sc->aifflags = AAC_AIFFLAGS_RUNNING;

	while ((sc->aifflags & AAC_AIFFLAGS_EXIT) == 0) {

		AAC_DPRINTF(AAC_D_THREAD,
		    ("%s: aac_command_thread: aifflags=%#x\n",
		    sc->aac_dev.dv_xname, sc->aifflags));
		retval = 0;

		if ((sc->aifflags & AAC_AIFFLAGS_PENDING) == 0) {
			AAC_DPRINTF(AAC_D_THREAD,
				    ("%s: command thread sleeping\n",
				     sc->aac_dev.dv_xname));
			AAC_LOCK_RELEASE(&sc->aac_io_lock);
			retval = tsleep_nsec(sc->aifthread, PRIBIO, "aifthd",
			    SEC_TO_NSEC(AAC_PERIODIC_INTERVAL));
			AAC_LOCK_ACQUIRE(&sc->aac_io_lock);
		}

		if ((sc->aifflags & AAC_AIFFLAGS_COMPLETE) != 0) {
			aac_complete(sc);
			sc->aifflags &= ~AAC_AIFFLAGS_COMPLETE;
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
		while (aac_dequeue_fib(sc, AAC_HOST_NORM_CMD_QUEUE,
				       &fib_size, &fib) == 0) {

			AAC_PRINT_FIB(sc, fib);

			switch (fib->Header.Command) {
			case AifRequest:
				//aac_handle_aif(sc, fib);
				break;
			default:
				printf("%s: unknown command from controller\n",
				       sc->aac_dev.dv_xname);
				break;
			}

			if ((fib->Header.XferState == 0) ||
			    (fib->Header.StructType != AAC_FIBTYPE_TFIB))
				break;

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
	AAC_LOCK_RELEASE(&sc->aac_io_lock);

	AAC_DPRINTF(AAC_D_THREAD, ("%s: aac_command_thread: exiting\n",
	    sc->aac_dev.dv_xname));
	kthread_exit(0);
}

/*
 * Process completed commands.
 */
void
aac_complete(void *context)
{
	struct aac_softc *sc = (struct aac_softc *)context;
	struct aac_command *cm;
	struct aac_fib *fib;
	u_int32_t fib_size;

	AAC_DPRINTF(AAC_D_CMD, ("%s: complete", sc->aac_dev.dv_xname));

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

	AAC_DPRINTF(AAC_D_CMD, ("\n"));
	/* see if we can start some more I/O */
	sc->flags &= ~AAC_QUEUE_FRZN;
	aac_startio(sc);
}

/*
 * Get a bio and build a command to go with it.
 */
int
aac_bio_command(struct aac_softc *sc, struct aac_command **cmp)
{
	struct aac_command *cm;
	struct aac_fib *fib;
	struct scsi_xfer *xs;
	u_int8_t opcode = 0;

	AAC_DPRINTF(AAC_D_CMD, ("%s: bio command", sc->aac_dev.dv_xname));

	/* get the resources we will need */
	if ((cm = aac_dequeue_bio(sc)) == NULL)
		goto fail;
	xs = cm->cm_private;

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

	switch(xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
		opcode = READ_COMMAND;
		break;
	case WRITE_COMMAND:
	case WRITE_10:
		opcode = WRITE_COMMAND;
		break;
	default:
		panic("%s: invalid opcode %#x", sc->aac_dev.dv_xname,
		    xs->cmd.opcode);
	}

	/* build the read/write request */
	if ((sc->flags & AAC_FLAGS_SG_64BIT) == 0) {
		fib->Header.Command = ContainerCommand;
		if (opcode == READ_COMMAND) {
			struct aac_blockread *br;
			br = (struct aac_blockread *)&fib->data[0];
			br->Command = VM_CtBlockRead;
			br->ContainerId = xs->sc_link->target;
			br->BlockNumber = cm->cm_blkno;
			br->ByteCount = cm->cm_bcount * AAC_BLOCK_SIZE;
			fib->Header.Size += sizeof(struct aac_blockread);
			cm->cm_sgtable = &br->SgMap;
			cm->cm_flags |= AAC_CMD_DATAIN;
		} else {
			struct aac_blockwrite *bw;
			bw = (struct aac_blockwrite *)&fib->data[0];
			bw->Command = VM_CtBlockWrite;
			bw->ContainerId = xs->sc_link->target;
			bw->BlockNumber = cm->cm_blkno;
			bw->ByteCount = cm->cm_bcount * AAC_BLOCK_SIZE;
			bw->Stable = CUNSTABLE;
			fib->Header.Size += sizeof(struct aac_blockwrite);
			cm->cm_flags |= AAC_CMD_DATAOUT;
			cm->cm_sgtable = &bw->SgMap;
		}
	} else {
		fib->Header.Command = ContainerCommand64;
		if (opcode == READ_COMMAND) {
			struct aac_blockread64 *br;
			br = (struct aac_blockread64 *)&fib->data[0];
			br->Command = VM_CtHostRead64;
			br->ContainerId = xs->sc_link->target;
			br->BlockNumber = cm->cm_blkno;
			br->SectorCount = cm->cm_bcount;
			br->Pad = 0;
			br->Flags = 0;
			fib->Header.Size += sizeof(struct aac_blockread64);
			cm->cm_flags |= AAC_CMD_DATAOUT;
			cm->cm_sgtable = (struct aac_sg_table *)&br->SgMap64;
		} else {
			struct aac_blockwrite64 *bw;
			bw = (struct aac_blockwrite64 *)&fib->data[0];
			bw->Command = VM_CtHostWrite64;
			bw->ContainerId = xs->sc_link->target;
			bw->BlockNumber = cm->cm_blkno;
			bw->SectorCount = cm->cm_bcount;
			bw->Pad = 0;
			bw->Flags = 0;
			fib->Header.Size += sizeof(struct aac_blockwrite64);
			cm->cm_flags |= AAC_CMD_DATAIN;
			cm->cm_sgtable = (struct aac_sg_table *)&bw->SgMap64;
		}
	}

	*cmp = cm;
	AAC_DPRINTF(AAC_D_CMD, ("\n"));
	return(0);

fail:
	AAC_DPRINTF(AAC_D_CMD, ("\n"));
	return(ENOMEM);
}

/*
 * Handle a bio-instigated command that has been completed.
 */
void
aac_bio_complete(struct aac_command *cm)
{
	struct aac_blockread_response *brr;
	struct aac_blockwrite_response *bwr;
	struct scsi_xfer *xs = (struct scsi_xfer *)cm->cm_private;
	AAC_FSAStatus status;
	int s;

	AAC_DPRINTF(AAC_D_CMD,
		    ("%s: bio complete\n", cm->cm_sc->aac_dev.dv_xname));

	/* fetch relevant status and then release the command */
	if (xs->flags & SCSI_DATA_IN) {
		brr = (struct aac_blockread_response *)&cm->cm_fib->data[0];
		status = brr->Status;
	} else {
		bwr = (struct aac_blockwrite_response *)&cm->cm_fib->data[0];
		status = bwr->Status;
	}

	xs->error = status == ST_OK? XS_NOERROR : XS_DRIVER_STUFFUP;
	xs->resid = 0;
	s = splbio();
	scsi_done(xs);
	splx(s);
}

/*
 * Submit a command to the controller, return when it completes.
 * XXX This is very dangerous!  If the card has gone out to lunch, we could
 *     be stuck here forever.  At the same time, signals are not caught
 *     because there is a risk that a signal could wakeup the tsleep before
 *     the card has a chance to complete the command.  The passed in timeout
 *     is ignored for the same reason.  Since there is no way to cancel a
 *     command in progress, we should probably create a 'dead' queue where
 *     commands go that have been interrupted/timed-out/etc, that keeps them
 *     out of the free pool.  That way, if the card is just slow, it won't
 *     spam the memory of a command that has been recycled.
 */
int
aac_wait_command(struct aac_command *cm, int msecs)
{
	struct aac_softc *sc = cm->cm_sc;
	int error = 0;

	AAC_DPRINTF(AAC_D_CMD, (": wait for command"));

	/* Put the command on the ready queue and get things going */
	cm->cm_queue = AAC_ADAP_NORM_CMD_QUEUE;
	aac_enqueue_ready(cm);
	AAC_DPRINTF(AAC_D_CMD, ("\n"));
	aac_startio(sc);
	while (!(cm->cm_flags & AAC_CMD_COMPLETED) && (error != EWOULDBLOCK)) {
		AAC_DPRINTF(AAC_D_MISC, ("%s: sleeping until command done\n",
					 sc->aac_dev.dv_xname));
		AAC_LOCK_RELEASE(&sc->aac_io_lock);
		error = tsleep_nsec(cm, PRIBIO, "aacwait", MSEC_TO_NSEC(msecs));
		AAC_LOCK_ACQUIRE(&sc->aac_io_lock);
	}
	return (error);
}

/*
 *Command Buffer Management
 */

/*
 * Allocate a command.
 */
void *
aac_alloc_command(void *xsc)
{
	struct aac_softc *sc = xsc;
	struct aac_command *cm;

	AAC_DPRINTF(AAC_D_CMD, (": allocate command"));
	mtx_enter(&sc->aac_free_mtx);
	cm = aac_dequeue_free(sc);
	mtx_leave(&sc->aac_free_mtx);

	return (cm);
}

void
aac_scrub_command(struct aac_command *cm)
{
	cm->cm_sgtable = NULL;
	cm->cm_flags = 0;
	cm->cm_complete = NULL;
	cm->cm_private = NULL;
	cm->cm_fib->Header.XferState = AAC_FIBSTATE_EMPTY;
	cm->cm_fib->Header.StructType = AAC_FIBTYPE_TFIB;
	cm->cm_fib->Header.Flags = 0;
	cm->cm_fib->Header.SenderSize = sizeof(struct aac_fib);
}

/*
 * Release a command back to the freelist.
 */
void
aac_release_command(void *xsc, void *xcm)
{
	struct aac_softc *sc = xsc;
	struct aac_command *cm = xcm;
	AAC_DPRINTF(AAC_D_CMD, (": release command"));

	mtx_enter(&sc->aac_free_mtx);
	aac_enqueue_free(cm);
	mtx_leave(&sc->aac_free_mtx);
}

/*
 * Allocate and initialise commands/FIBs for this adapter.
 */
int
aac_alloc_commands(struct aac_softc *sc)
{
	struct aac_command *cm;
	struct aac_fibmap *fm;
	int i, error = ENOMEM;

	if (sc->total_fibs + AAC_FIB_COUNT > sc->aac_max_fibs)
		return (ENOMEM);

	fm = malloc(sizeof(*fm), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (fm == NULL)
		goto exit;

	/* allocate the FIBs in DMAable memory and load them */
	if (bus_dmamem_alloc(sc->aac_dmat, AAC_FIBMAP_SIZE, PAGE_SIZE, 0,
	    &fm->aac_seg, 1, &fm->aac_nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO)) {
		printf("%s: can't alloc FIBs\n", sc->aac_dev.dv_xname);
		error = ENOBUFS;
		goto exit_alloc;
	}

	if (bus_dmamem_map(sc->aac_dmat, &fm->aac_seg, 1,
	    AAC_FIBMAP_SIZE, (caddr_t *)&fm->aac_fibs, BUS_DMA_NOWAIT)) {
		printf("%s: can't map FIB structure\n", sc->aac_dev.dv_xname);
		error = ENOBUFS;
		goto exit_map;
	}

	if (bus_dmamap_create(sc->aac_dmat, AAC_FIBMAP_SIZE, 1,
	    AAC_FIBMAP_SIZE, 0, BUS_DMA_NOWAIT, &fm->aac_fibmap)) {
		printf("%s: can't create dma map\n", sc->aac_dev.dv_xname);
		error = ENOBUFS;
		goto exit_create;
	}

	if (bus_dmamap_load(sc->aac_dmat, fm->aac_fibmap, fm->aac_fibs,
	    AAC_FIBMAP_SIZE, NULL, BUS_DMA_NOWAIT)) {
		printf("%s: can't load dma map\n", sc->aac_dev.dv_xname);
		error = ENOBUFS;
		goto exit_load;
	}

	/* initialise constant fields in the command structure */
	AAC_LOCK_ACQUIRE(&sc->aac_io_lock);
	for (i = 0; i < AAC_FIB_COUNT; i++) {
		cm = sc->aac_commands + sc->total_fibs;
		fm->aac_commands = cm;
		cm->cm_sc = sc;
		cm->cm_fib = fm->aac_fibs + i;
		cm->cm_fibphys = fm->aac_fibmap->dm_segs[0].ds_addr +
			(i * sizeof(struct aac_fib));
		cm->cm_index = sc->total_fibs;

		if (bus_dmamap_create(sc->aac_dmat, MAXPHYS, AAC_MAXSGENTRIES,
		    MAXPHYS, 0, BUS_DMA_NOWAIT, &cm->cm_datamap)) {
			break;
		}
		aac_release_command(sc, cm);
		sc->total_fibs++;
	}

	if (i > 0) {
		TAILQ_INSERT_TAIL(&sc->aac_fibmap_tqh, fm, fm_link);
		AAC_DPRINTF(AAC_D_MISC, ("%s: total_fibs= %d\n",
					 sc->aac_dev.dv_xname,
					 sc->total_fibs));
		AAC_LOCK_RELEASE(&sc->aac_io_lock);
		return (0);
	}

 exit_load:
	bus_dmamap_destroy(sc->aac_dmat, fm->aac_fibmap);
 exit_create:
	bus_dmamem_unmap(sc->aac_dmat, (caddr_t)fm->aac_fibs, AAC_FIBMAP_SIZE);
 exit_map:
	bus_dmamem_free(sc->aac_dmat, &fm->aac_seg, fm->aac_nsegs);
 exit_alloc:
	free(fm, M_DEVBUF, sizeof *fm);
 exit:
	AAC_LOCK_RELEASE(&sc->aac_io_lock);
	return (error);
}

/*
 * Free FIBs owned by this adapter.
 */
void
aac_free_commands(struct aac_softc *sc)
{
	struct aac_fibmap *fm;
	struct aac_command *cm;
	int i;

	while ((fm = TAILQ_FIRST(&sc->aac_fibmap_tqh)) != NULL) {

		TAILQ_REMOVE(&sc->aac_fibmap_tqh, fm, fm_link);

		/*
		 * We check against total_fibs to handle partially
		 * allocated blocks.
		 */
		for (i = 0; i < AAC_FIB_COUNT && sc->total_fibs--; i++) {
			cm = fm->aac_commands + i;
			bus_dmamap_destroy(sc->aac_dmat, cm->cm_datamap);
		}

		bus_dmamap_unload(sc->aac_dmat, fm->aac_fibmap);
		bus_dmamap_destroy(sc->aac_dmat, fm->aac_fibmap);
		bus_dmamem_unmap(sc->aac_dmat, (caddr_t)fm->aac_fibs,
				 AAC_FIBMAP_SIZE);
		bus_dmamem_free(sc->aac_dmat, &fm->aac_seg, fm->aac_nsegs);
		free(fm, M_DEVBUF, sizeof *fm);
	}
}


/*
 * Command-mapping helper function - populate this command's s/g table.
 */
void
aac_map_command_sg(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct aac_command *cm = arg;
	struct aac_softc *sc = cm->cm_sc;
	struct aac_fib *fib = cm->cm_fib;
	int i;

	/* copy into the FIB */
	if (cm->cm_sgtable != NULL) {
		if ((cm->cm_sc->flags & AAC_FLAGS_SG_64BIT) == 0) {
			struct aac_sg_table *sg = cm->cm_sgtable;
			sg->SgCount = nseg;
			for (i = 0; i < nseg; i++) {
				sg->SgEntry[i].SgAddress = segs[i].ds_addr;
				sg->SgEntry[i].SgByteCount = segs[i].ds_len;
			}
			/* update the FIB size for the s/g count */
			fib->Header.Size += nseg * sizeof(struct aac_sg_entry);
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
	 * the SenderFibAddress over to make room for the fast response bit.
	 */
	cm->cm_fib->Header.SenderFibAddress = (cm->cm_index << 1);
	cm->cm_fib->Header.ReceiverFibAddress = cm->cm_fibphys;

	/* save a pointer to the command for speedy reverse-lookup */
	cm->cm_fib->Header.SenderData = cm->cm_index;

	if (cm->cm_flags & AAC_CMD_DATAIN)
		bus_dmamap_sync(sc->aac_dmat, cm->cm_datamap, 0,
				cm->cm_datamap->dm_mapsize,
				BUS_DMASYNC_PREREAD);
	if (cm->cm_flags & AAC_CMD_DATAOUT)
		bus_dmamap_sync(sc->aac_dmat, cm->cm_datamap, 0,
				cm->cm_datamap->dm_mapsize,
				BUS_DMASYNC_PREWRITE);
	cm->cm_flags |= AAC_CMD_MAPPED;

	/* put the FIB on the outbound queue */
	if (aac_enqueue_fib(sc, cm->cm_queue, cm) == EBUSY) {
		aac_remove_busy(cm);
		aac_unmap_command(cm);
		aac_requeue_ready(cm);
	}
}

/*
 * Unmap a command from controller-visible space.
 */
void
aac_unmap_command(struct aac_command *cm)
{
	struct aac_softc *sc = cm->cm_sc;

	if (!(cm->cm_flags & AAC_CMD_MAPPED))
		return;

	if (cm->cm_datalen != 0) {
		if (cm->cm_flags & AAC_CMD_DATAIN)
			bus_dmamap_sync(sc->aac_dmat, cm->cm_datamap, 0,
					cm->cm_datamap->dm_mapsize,
					BUS_DMASYNC_POSTREAD);
		if (cm->cm_flags & AAC_CMD_DATAOUT)
			bus_dmamap_sync(sc->aac_dmat, cm->cm_datamap, 0,
					cm->cm_datamap->dm_mapsize,
					BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->aac_dmat, cm->cm_datamap);
	}
	cm->cm_flags &= ~AAC_CMD_MAPPED;
}

/*
 * Hardware Interface
 */

/*
 * Initialise the adapter.
 */
int
aac_check_firmware(struct aac_softc *sc)
{
	u_int32_t major, minor, options;

	/*
	 * Retrieve the firmware version numbers.  Dell PERC2/QC cards with
	 * firmware version 1.x are not compatible with this driver.
	 */
	if (sc->flags & AAC_FLAGS_PERC2QC) {
		if (aac_sync_command(sc, AAC_MONKER_GETKERNVER, 0, 0, 0, 0,
				     NULL)) {
			printf("%s: Error reading firmware version\n",
			       sc->aac_dev.dv_xname);
			return (EIO);
		}

		/* These numbers are stored as ASCII! */
		major = (AAC_GET_MAILBOX(sc, 1) & 0xff) - 0x30;
		minor = (AAC_GET_MAILBOX(sc, 2) & 0xff) - 0x30;
		if (major == 1) {
			printf("%s: Firmware version %d.%d is not supported\n",
			       sc->aac_dev.dv_xname, major, minor);
			return (EINVAL);
		}
	}

	/*
	 * Retrieve the capabilities/supported options word so we know what
	 * work-arounds to enable.
	 */
	if (aac_sync_command(sc, AAC_MONKER_GETINFO, 0, 0, 0, 0, NULL)) {
		printf("%s: RequestAdapterInfo failed\n",
		       sc->aac_dev.dv_xname);
		return (EIO);
	}
	options = AAC_GET_MAILBOX(sc, 1);
	sc->supported_options = options;

	if ((options & AAC_SUPPORTED_4GB_WINDOW) != 0 &&
	    (sc->flags & AAC_FLAGS_NO4GB) == 0)
		sc->flags |= AAC_FLAGS_4GB_WINDOW;
	if (options & AAC_SUPPORTED_NONDASD)
		sc->flags |= AAC_FLAGS_ENABLE_CAM;
	if ((options & AAC_SUPPORTED_SGMAP_HOST64) != 0
	     && (sizeof(bus_addr_t) > 4)) {
		printf("%s: Enabling 64-bit address support\n",
		       sc->aac_dev.dv_xname);
		sc->flags |= AAC_FLAGS_SG_64BIT;
	}

	/* Check for broken hardware that does a lower number of commands */
	if ((sc->flags & AAC_FLAGS_256FIBS) == 0)
		sc->aac_max_fibs = AAC_MAX_FIBS;
	else
		sc->aac_max_fibs = 256;

	return (0);
}

int
aac_init(struct aac_softc *sc)
{
	bus_dma_segment_t seg;
	int nsegs;
	int i, error;
	int state = 0;
	struct aac_adapter_init	*ip;
	time_t then;
	u_int32_t code, qoffset;

	/*
	 * First wait for the adapter to come ready.
	 */
	then = getuptime();
	for (i = 0; i < AAC_BOOT_TIMEOUT * 1000; i++) {
		code = AAC_GET_FWSTATUS(sc);
		if (code & AAC_SELF_TEST_FAILED) {
			printf("%s: FATAL: selftest failed\n",
			    sc->aac_dev.dv_xname);
			return (ENXIO);
		}
		if (code & AAC_KERNEL_PANIC) {
			printf("%s: FATAL: controller kernel panic\n",
			    sc->aac_dev.dv_xname);
			return (ENXIO);
		}
		if (code & AAC_UP_AND_RUNNING)
			break;
		DELAY(1000);
	}
	if (i == AAC_BOOT_TIMEOUT * 1000) {
		printf("%s: FATAL: controller not coming ready, status %x\n",
		    sc->aac_dev.dv_xname, code);
		return (ENXIO);
	}

	/*
	 * Work around a bug in the 2120 and 2200 that cannot DMA commands
	 * below address 8192 in physical memory.
	 * XXX If the padding is not needed, can it be put to use instead
	 * of ignored?
	 */
	if (bus_dmamem_alloc(sc->aac_dmat, AAC_COMMON_ALLOCSIZE, PAGE_SIZE, 0,
			     &seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO)) {
		printf("%s: can't allocate common structure\n",
		    sc->aac_dev.dv_xname);
		return (ENOMEM);
	}
	state++;

	if (bus_dmamem_map(sc->aac_dmat, &seg, nsegs, AAC_COMMON_ALLOCSIZE,
			   (caddr_t *)&sc->aac_common, BUS_DMA_NOWAIT)) {
		printf("%s: can't map common structure\n",
		    sc->aac_dev.dv_xname);
		error = ENOMEM;
		goto bail_out;
	}
	state++;

	if (bus_dmamap_create(sc->aac_dmat, AAC_COMMON_ALLOCSIZE, 1,
	    AAC_COMMON_ALLOCSIZE, 0, BUS_DMA_NOWAIT, &sc->aac_common_map)) {
		printf("%s: can't create dma map\n", sc->aac_dev.dv_xname);
		error = ENOBUFS;
		goto bail_out;
	}
	state++;

	if (bus_dmamap_load(sc->aac_dmat, sc->aac_common_map, sc->aac_common,
	    AAC_COMMON_ALLOCSIZE, NULL, BUS_DMA_NOWAIT)) {
		printf("%s: can't load dma map\n", sc->aac_dev.dv_xname);
		error = ENOBUFS;
		goto bail_out;
	}
	state++;

	sc->aac_common_busaddr = sc->aac_common_map->dm_segs[0].ds_addr;

	if (sc->aac_common_busaddr < 8192) {
		sc->aac_common = (struct aac_common *)
		    ((uint8_t *)sc->aac_common + 8192);
		sc->aac_common_busaddr += 8192;
	}

	/* Allocate some FIBs and associated command structs */
	TAILQ_INIT(&sc->aac_fibmap_tqh);
	sc->aac_commands = malloc(AAC_MAX_FIBS * sizeof(struct aac_command),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	while (sc->total_fibs < AAC_MAX_FIBS) {
		if (aac_alloc_commands(sc) != 0)
			break;
	}
	if (sc->total_fibs == 0) {
		error = ENOMEM;
		goto bail_out;
	}

	scsi_iopool_init(&sc->aac_iopool, sc,
	    aac_alloc_command, aac_release_command);

	/*
	 * Fill in the init structure.  This tells the adapter about the
	 * physical location of various important shared data structures.
	 */
	ip = &sc->aac_common->ac_init;
	ip->InitStructRevision = AAC_INIT_STRUCT_REVISION;
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
	ip->HostPhysMemPages = ptoa(physmem) / AAC_PAGE_SIZE;
	if (sc->flags & AAC_FLAGS_BROKEN_MEMMAP) {
		ip->HostPhysMemPages =
		    (ip->HostPhysMemPages + AAC_PAGE_SIZE) / AAC_PAGE_SIZE;
	}
	ip->HostElapsedSeconds = getuptime(); /* reset later if invalid */

	/*
	 * Initialise FIB queues.  Note that it appears that the layout of the
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
	    (struct aac_queue_table *)((caddr_t)sc->aac_common + qoffset);
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
		AAC_SETREG4(sc, AAC_RX_ODBR, ~0);
		break;
	case AAC_HWIF_RKT:
		AAC_SETREG4(sc, AAC_RKT_ODBR, ~0);
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
		printf("%s: error establishing init structure\n",
		    sc->aac_dev.dv_xname);
		error = EIO;
		goto bail_out;
	}

	aac_describe_controller(sc);
	aac_startup(sc);

	return (0);

 bail_out:
	if (state > 3)
		bus_dmamap_unload(sc->aac_dmat, sc->aac_common_map);
	if (state > 2)
		bus_dmamap_destroy(sc->aac_dmat, sc->aac_common_map);
	if (state > 1)
		bus_dmamem_unmap(sc->aac_dmat, (caddr_t)sc->aac_common,
		    sizeof *sc->aac_common);
	if (state > 0)
		bus_dmamem_free(sc->aac_dmat, &seg, 1);

	return (error);
}

/*
 * Send a synchronous command to the controller and wait for a result.
 */
int
aac_sync_command(struct aac_softc *sc, u_int32_t command, u_int32_t arg0,
		 u_int32_t arg1, u_int32_t arg2, u_int32_t arg3, u_int32_t *sp)
{
//	time_t then;
	int i;
	u_int32_t status;
	u_int16_t reason;

	/* populate the mailbox */
	AAC_SET_MAILBOX(sc, command, arg0, arg1, arg2, arg3);

	/* ensure the sync command doorbell flag is cleared */
	AAC_CLEAR_ISTATUS(sc, AAC_DB_SYNC_COMMAND);

	/* then set it to signal the adapter */
	AAC_QNOTIFY(sc, AAC_DB_SYNC_COMMAND);

	DELAY(AAC_SYNC_DELAY);

	/* spin waiting for the command to complete */
	for (i = 0; i < AAC_IMMEDIATE_TIMEOUT * 1000; i++) {
		reason = AAC_GET_ISTATUS(sc);
		if (reason & AAC_DB_SYNC_COMMAND)
			break;
		reason = AAC_GET_ISTATUS(sc);
		if (reason & AAC_DB_SYNC_COMMAND)
			break;
		reason = AAC_GET_ISTATUS(sc);
		if (reason & AAC_DB_SYNC_COMMAND)
			break;
		DELAY(1000);
	}
	if (i == AAC_IMMEDIATE_TIMEOUT * 1000) {
		printf("aac_sync_command: failed, reason=%#x\n", reason);
		return (EIO);
	}

	/* clear the completion flag */
	AAC_CLEAR_ISTATUS(sc, AAC_DB_SYNC_COMMAND);

	/* get the command status */
	status = AAC_GET_MAILBOX(sc, 0);

	if (sp != NULL)
		*sp = status;

	return(0);
}

/*
 * Grab the sync fib area.
 */
int
aac_alloc_sync_fib(struct aac_softc *sc, struct aac_fib **fib, int flags)
{

	/*
	 * If the force flag is set, the system is shutting down, or in
	 * trouble.  Ignore the mutex.
	 */
	if (!(flags & AAC_SYNC_LOCK_FORCE))
		AAC_LOCK_ACQUIRE(&sc->aac_sync_lock);

	*fib = &sc->aac_common->ac_sync_fib;

	return (1);
}

/*
 * Release the sync fib area.
 */
void
aac_release_sync_fib(struct aac_softc *sc)
{
	AAC_LOCK_RELEASE(&sc->aac_sync_lock);
}

/*
 * Send a synchronous FIB to the controller and wait for a result.
 */
int
aac_sync_fib(struct aac_softc *sc, u_int32_t command, u_int32_t xferstate,
	     struct aac_fib *fib, u_int16_t datasize)
{

	if (datasize > AAC_FIB_DATASIZE) {
		printf("aac_sync_fib 1: datasize=%d AAC_FIB_DATASIZE %lu\n",
		    datasize, AAC_FIB_DATASIZE);
		return(EINVAL);
	}

	/*
	 * Set up the sync FIB
	 */
	fib->Header.XferState = AAC_FIBSTATE_HOSTOWNED |
				AAC_FIBSTATE_INITIALISED |
				AAC_FIBSTATE_EMPTY;
	fib->Header.XferState |= xferstate;
	fib->Header.Command = command;
	fib->Header.StructType = AAC_FIBTYPE_TFIB;
	fib->Header.Size = sizeof(struct aac_fib) + datasize;
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
		AAC_DPRINTF(AAC_D_IO, ("%s: aac_sync_fib: IO error\n",
				       sc->aac_dev.dv_xname));
		printf("aac_sync_fib 2\n");
		return(EIO);
	}

	return (0);
}

/*****************************************************************************
 * Adapter-space FIB queue manipulation
 *
 * Note that the queue implementation here is a little funky; neither the PI or
 * CI will ever be zero.  This behaviour is a controller feature.
 */
static struct {
	int size;
	int notify;
} aac_qinfo[] = {
	{ AAC_HOST_NORM_CMD_ENTRIES, AAC_DB_COMMAND_NOT_FULL },
	{ AAC_HOST_HIGH_CMD_ENTRIES, 0 },
	{ AAC_ADAP_NORM_CMD_ENTRIES, AAC_DB_COMMAND_READY },
	{ AAC_ADAP_HIGH_CMD_ENTRIES, 0 },
	{ AAC_HOST_NORM_RESP_ENTRIES, AAC_DB_RESPONSE_NOT_FULL },
	{ AAC_HOST_HIGH_RESP_ENTRIES, 0 },
	{ AAC_ADAP_NORM_RESP_ENTRIES, AAC_DB_RESPONSE_READY },
	{ AAC_ADAP_HIGH_RESP_ENTRIES, 0 }
};

/*
 * Atomically insert an entry into the nominated queue, returns 0 on success
 * or EBUSY if the queue is full.
 *
 * Note: it would be more efficient to defer notifying the controller in
 *	 the case where we may be inserting several entries in rapid
 *	 succession, but implementing this usefully may be difficult
 *	 (it would involve a separate queue/notify interface).
 */
int
aac_enqueue_fib(struct aac_softc *sc, int queue, struct aac_command *cm)
{
	u_int32_t pi, ci;
	int error;
	u_int32_t fib_size;
	u_int32_t fib_addr;

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

	/* populate queue entry */
	(sc->aac_qentries[queue] + pi)->aq_fib_size = fib_size;
	(sc->aac_qentries[queue] + pi)->aq_fib_addr = fib_addr;

	/* update producer index */
	sc->aac_queues->qt_qindex[queue][AAC_PRODUCER_INDEX] = pi + 1;

	/*
	 * To avoid a race with its completion interrupt, place this command on
	 * the busy queue prior to advertising it to the controller.
	 */
	aac_enqueue_busy(cm);

	/* notify the adapter if we know how */
	if (aac_qinfo[queue].notify != 0)
		AAC_QNOTIFY(sc, aac_qinfo[queue].notify);

	error = 0;

out:
	return (error);
}

/*
 * Atomically remove one entry from the nominated queue, returns 0 on success
 * or ENOENT if the queue is empty.
 */
int
aac_dequeue_fib(struct aac_softc *sc, int queue, u_int32_t *fib_size,
		struct aac_fib **fib_addr)
{
	u_int32_t pi, ci;
	u_int32_t fib_index;
	int notify;
	int error;

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
		cm = sc->aac_commands + (fib_index >> 1);
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
	return (error);
}

/*
 * Put our response to an Adapter Initialed Fib on the response queue
 */
int
aac_enqueue_response(struct aac_softc *sc, int queue, struct aac_fib *fib)
{
	u_int32_t pi, ci;
	int error;
	u_int32_t fib_size;
	u_int32_t fib_addr;

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

void
aac_command_timeout(struct aac_command *cm)
{
	struct aac_softc *sc = cm->cm_sc;

	printf("%s: COMMAND %p (flags=%#x) TIMEOUT AFTER %d SECONDS\n",
	       sc->aac_dev.dv_xname, cm, cm->cm_flags,
	       (int)(getuptime() - cm->cm_timestamp));

	if (cm->cm_flags & AAC_CMD_TIMEDOUT)
		return;

	cm->cm_flags |= AAC_CMD_TIMEDOUT;

	AAC_PRINT_FIB(sc, cm->cm_fib);

	if (cm->cm_flags & AAC_ON_AACQ_BIO) {
		struct scsi_xfer *xs = cm->cm_private;
		int s = splbio();
		xs->error = XS_DRIVER_STUFFUP;
		splx(s);
		scsi_done(xs);

		aac_remove_bio(cm);
		aac_unmap_command(cm);
	}
}

void
aac_timeout(struct aac_softc *sc)
{
	struct aac_command *cm;
	time_t deadline;

	/*
	 * Traverse the busy command list and timeout any commands
	 * that are past their deadline.
	 */
	deadline = getuptime() - AAC_CMD_TIMEOUT;
	TAILQ_FOREACH(cm, &sc->aac_busy, cm_link) {
		if (cm->cm_timestamp  < deadline)
			aac_command_timeout(cm);
	}
}

/*
 * Interface Function Vectors
 */

/*
 * Read the current firmware status word.
 */
int
aac_sa_get_fwstatus(struct aac_softc *sc)
{
	return (AAC_GETREG4(sc, AAC_SA_FWSTATUS));
}

int
aac_rx_get_fwstatus(struct aac_softc *sc)
{
	return (AAC_GETREG4(sc, AAC_RX_FWSTATUS));
}

int
aac_fa_get_fwstatus(struct aac_softc *sc)
{
	return (AAC_GETREG4(sc, AAC_FA_FWSTATUS));
}

int
aac_rkt_get_fwstatus(struct aac_softc *sc)
{
	return(AAC_GETREG4(sc, AAC_RKT_FWSTATUS));
}

/*
 * Notify the controller of a change in a given queue
 */

void
aac_sa_qnotify(struct aac_softc *sc, int qbit)
{
	AAC_SETREG2(sc, AAC_SA_DOORBELL1_SET, qbit);
}

void
aac_rx_qnotify(struct aac_softc *sc, int qbit)
{
	AAC_SETREG4(sc, AAC_RX_IDBR, qbit);
}

void
aac_fa_qnotify(struct aac_softc *sc, int qbit)
{
	AAC_SETREG2(sc, AAC_FA_DOORBELL1, qbit);
	AAC_FA_HACK(sc);
}

void
aac_rkt_qnotify(struct aac_softc *sc, int qbit)
{
	AAC_SETREG4(sc, AAC_RKT_IDBR, qbit);
}

/*
 * Get the interrupt reason bits
 */
int
aac_sa_get_istatus(struct aac_softc *sc)
{
	return (AAC_GETREG2(sc, AAC_SA_DOORBELL0));
}

int
aac_rx_get_istatus(struct aac_softc *sc)
{
	return (AAC_GETREG4(sc, AAC_RX_ODBR));
}

int
aac_fa_get_istatus(struct aac_softc *sc)
{
	return (AAC_GETREG2(sc, AAC_FA_DOORBELL0));
}

int
aac_rkt_get_istatus(struct aac_softc *sc)
{
	return(AAC_GETREG4(sc, AAC_RKT_ODBR));
}

/*
 * Clear some interrupt reason bits
 */
void
aac_sa_clear_istatus(struct aac_softc *sc, int mask)
{
	AAC_SETREG2(sc, AAC_SA_DOORBELL0_CLEAR, mask);
}

void
aac_rx_clear_istatus(struct aac_softc *sc, int mask)
{
	AAC_SETREG4(sc, AAC_RX_ODBR, mask);
}

void
aac_fa_clear_istatus(struct aac_softc *sc, int mask)
{
	AAC_SETREG2(sc, AAC_FA_DOORBELL0_CLEAR, mask);
	AAC_FA_HACK(sc);
}

void
aac_rkt_clear_istatus(struct aac_softc *sc, int mask)
{
	AAC_SETREG4(sc, AAC_RKT_ODBR, mask);
}

/*
 * Populate the mailbox and set the command word
 */
void
aac_sa_set_mailbox(struct aac_softc *sc, u_int32_t command, u_int32_t arg0,
		   u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	AAC_SETREG4(sc, AAC_SA_MAILBOX, command);
	AAC_SETREG4(sc, AAC_SA_MAILBOX + 4, arg0);
	AAC_SETREG4(sc, AAC_SA_MAILBOX + 8, arg1);
	AAC_SETREG4(sc, AAC_SA_MAILBOX + 12, arg2);
	AAC_SETREG4(sc, AAC_SA_MAILBOX + 16, arg3);
}

void
aac_rx_set_mailbox(struct aac_softc *sc, u_int32_t command, u_int32_t arg0,
		   u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	AAC_SETREG4(sc, AAC_RX_MAILBOX, command);
	AAC_SETREG4(sc, AAC_RX_MAILBOX + 4, arg0);
	AAC_SETREG4(sc, AAC_RX_MAILBOX + 8, arg1);
	AAC_SETREG4(sc, AAC_RX_MAILBOX + 12, arg2);
	AAC_SETREG4(sc, AAC_RX_MAILBOX + 16, arg3);
}

void
aac_fa_set_mailbox(struct aac_softc *sc, u_int32_t command, u_int32_t arg0,
		   u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	AAC_SETREG4(sc, AAC_FA_MAILBOX, command);
	AAC_FA_HACK(sc);
	AAC_SETREG4(sc, AAC_FA_MAILBOX + 4, arg0);
	AAC_FA_HACK(sc);
	AAC_SETREG4(sc, AAC_FA_MAILBOX + 8, arg1);
	AAC_FA_HACK(sc);
	AAC_SETREG4(sc, AAC_FA_MAILBOX + 12, arg2);
	AAC_FA_HACK(sc);
	AAC_SETREG4(sc, AAC_FA_MAILBOX + 16, arg3);
	AAC_FA_HACK(sc);
}

void
aac_rkt_set_mailbox(struct aac_softc *sc, u_int32_t command, u_int32_t arg0,
		    u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	AAC_SETREG4(sc, AAC_RKT_MAILBOX, command);
	AAC_SETREG4(sc, AAC_RKT_MAILBOX + 4, arg0);
	AAC_SETREG4(sc, AAC_RKT_MAILBOX + 8, arg1);
	AAC_SETREG4(sc, AAC_RKT_MAILBOX + 12, arg2);
	AAC_SETREG4(sc, AAC_RKT_MAILBOX + 16, arg3);
}

/*
 * Fetch the immediate command status word
 */
int
aac_sa_get_mailbox(struct aac_softc *sc, int mb)
{
	return (AAC_GETREG4(sc, AAC_SA_MAILBOX + (mb * 4)));
}

int
aac_rx_get_mailbox(struct aac_softc *sc, int mb)
{
	return (AAC_GETREG4(sc, AAC_RX_MAILBOX + (mb * 4)));
}

int
aac_fa_get_mailbox(struct aac_softc *sc, int mb)
{
	return (AAC_GETREG4(sc, AAC_FA_MAILBOX + (mb * 4)));
}

int
aac_rkt_get_mailbox(struct aac_softc *sc, int mb)
{
	return(AAC_GETREG4(sc, AAC_RKT_MAILBOX + (mb * 4)));
}

/*
 * Set/clear interrupt masks
 */
void
aac_sa_set_interrupts(struct aac_softc *sc, int enable)
{
	AAC_DPRINTF(AAC_D_INTR, ("%s: %sable interrupts\n",
				 sc->aac_dev.dv_xname, enable ? "en" : "dis"));

	if (enable)
		AAC_SETREG2((sc), AAC_SA_MASK0_CLEAR, AAC_DB_INTERRUPTS);
	else
		AAC_SETREG2((sc), AAC_SA_MASK0_SET, ~0);
}

void
aac_rx_set_interrupts(struct aac_softc *sc, int enable)
{
	AAC_DPRINTF(AAC_D_INTR, ("%s: %sable interrupts",
				 sc->aac_dev.dv_xname, enable ? "en" : "dis"));

	if (enable)
		AAC_SETREG4(sc, AAC_RX_OIMR, ~AAC_DB_INTERRUPTS);
	else
		AAC_SETREG4(sc, AAC_RX_OIMR, ~0);
}

void
aac_fa_set_interrupts(struct aac_softc *sc, int enable)
{
	AAC_DPRINTF(AAC_D_INTR, ("%s: %sable interrupts",
				 sc->aac_dev.dv_xname, enable ? "en" : "dis"));

	if (enable) {
		AAC_SETREG2((sc), AAC_FA_MASK0_CLEAR, AAC_DB_INTERRUPTS);
		AAC_FA_HACK(sc);
	} else {
		AAC_SETREG2((sc), AAC_FA_MASK0, ~0);
		AAC_FA_HACK(sc);
	}
}

void
aac_rkt_set_interrupts(struct aac_softc *sc, int enable)
{
	AAC_DPRINTF(AAC_D_INTR, ("%s: %sable interrupts",
				 sc->aac_dev.dv_xname, enable ? "en" : "dis"));

	if (enable)
		AAC_SETREG4(sc, AAC_RKT_OIMR, ~AAC_DB_INTERRUPTS);
	else
		AAC_SETREG4(sc, AAC_RKT_OIMR, ~0);
}

void
aac_eval_mapping(u_int32_t size, int *cyls, int *heads, int *secs)
{
	*cyls = size / AAC_HEADS / AAC_SECS;
	if (*cyls < AAC_MAXCYLS) {
		*heads = AAC_HEADS;
		*secs = AAC_SECS;
	} else {
		/* Too high for 64 * 32 */
		*cyls = size / AAC_MEDHEADS / AAC_MEDSECS;
		if (*cyls < AAC_MAXCYLS) {
			*heads = AAC_MEDHEADS;
			*secs = AAC_MEDSECS;
		} else {
			/* Too high for 127 * 63 */
			*cyls = size / AAC_BIGHEADS / AAC_BIGSECS;
			*heads = AAC_BIGHEADS;
			*secs = AAC_BIGSECS;
		}
	}
}

/* Emulated SCSI operation on cache device */
void
aac_internal_cache_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct aac_softc *sc = link->bus->sb_adapter_softc;
	struct scsi_inquiry_data inq;
	struct scsi_sense_data sd;
	struct scsi_read_cap_data rcd;
	u_int8_t target = link->target;

	AAC_DPRINTF(AAC_D_CMD, ("%s: aac_internal_cache_cmd: ",
				sc->aac_dev.dv_xname));

	switch (xs->cmd.opcode) {
	case TEST_UNIT_READY:
	case START_STOP:
#if 0
	case VERIFY:
#endif
		AAC_DPRINTF(AAC_D_CMD, ("opc %#x tgt %d ", xs->cmd.opcode,
		    target));
		break;

	case REQUEST_SENSE:
		AAC_DPRINTF(AAC_D_CMD, ("REQUEST SENSE tgt %d ", target));
		bzero(&sd, sizeof sd);
		sd.error_code = SSD_ERRCODE_CURRENT;
		sd.segment = 0;
		sd.flags = SKEY_NO_SENSE;
		aac_enc32(sd.info, 0);
		sd.extra_len = 0;
		scsi_copy_internal_data(xs, &sd, sizeof(sd));
		break;

	case INQUIRY:
		AAC_DPRINTF(AAC_D_CMD, ("INQUIRY tgt %d devtype %x ", target,
		    sc->aac_hdr[target].hd_devtype));
		bzero(&inq, sizeof inq);
		/* XXX How do we detect removable/CD-ROM devices?  */
		inq.device = T_DIRECT;
		inq.dev_qual2 = 0;
		inq.version = SCSI_REV_2;
		inq.response_format = SID_SCSI2_RESPONSE;
		inq.additional_length = SID_SCSI2_ALEN;
		inq.flags |= SID_CmdQue;
		strlcpy(inq.vendor, "Adaptec", sizeof inq.vendor);
		snprintf(inq.product, sizeof inq.product, "Container #%02d",
		    target);
		strlcpy(inq.revision, "   ", sizeof inq.revision);
		scsi_copy_internal_data(xs, &inq, sizeof(inq));
		break;

	case READ_CAPACITY:
		AAC_DPRINTF(AAC_D_CMD, ("READ CAPACITY tgt %d ", target));
		bzero(&rcd, sizeof rcd);
		_lto4b(sc->aac_hdr[target].hd_size - 1, rcd.addr);
		_lto4b(AAC_BLOCK_SIZE, rcd.length);
		scsi_copy_internal_data(xs, (u_int8_t *)&rcd, sizeof rcd);
		break;

	default:
		AAC_DPRINTF(AAC_D_CMD, ("\n"));
		printf("aac_internal_cache_cmd got bad opcode: %#x\n",
		    xs->cmd.opcode);
		xs->error = XS_DRIVER_STUFFUP;
		return;
	}

	xs->error = XS_NOERROR;
}

void
aac_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct aac_softc *sc = link->bus->sb_adapter_softc;
	u_int8_t target = link->target;
	struct aac_command *cm;
	u_int32_t blockno, blockcnt;
	struct scsi_rw *rw;
	struct scsi_rw_10 *rw10;
	int s;

	s = splbio();

	xs->error = XS_NOERROR;

	if (target >= AAC_MAX_CONTAINERS || !sc->aac_hdr[target].hd_present ||
	    link->lun != 0) {
		/*
		 * XXX Should be XS_SENSE but that would require setting up a
		 * faked sense too.
		 */
		splx(s);
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	AAC_DPRINTF(AAC_D_CMD, ("%s: aac_scsi_cmd: ", sc->aac_dev.dv_xname));

	xs->error = XS_NOERROR;
	cm = NULL;
	link = xs->sc_link;
	target = link->target;

	switch (xs->cmd.opcode) {
	case TEST_UNIT_READY:
	case REQUEST_SENSE:
	case INQUIRY:
	case START_STOP:
	case READ_CAPACITY:
#if 0
	case VERIFY:
#endif
		aac_internal_cache_cmd(xs);
		scsi_done(xs);
		goto ready;

	case PREVENT_ALLOW:
		AAC_DPRINTF(AAC_D_CMD, ("PREVENT/ALLOW "));
		/* XXX Not yet implemented */
		xs->error = XS_NOERROR;
		scsi_done(xs);
		goto ready;

	case SYNCHRONIZE_CACHE:
		AAC_DPRINTF(AAC_D_CMD, ("SYNCHRONIZE_CACHE "));
		/* XXX Not yet implemented */
		xs->error = XS_NOERROR;
		scsi_done(xs);
		goto ready;

	default:
		AAC_DPRINTF(AAC_D_CMD, ("unknown opc %#x ", xs->cmd.opcode));
		/* XXX Not yet implemented */
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		goto ready;

	case READ_COMMAND:
	case READ_10:
	case WRITE_COMMAND:
	case WRITE_10:
		AAC_DPRINTF(AAC_D_CMD, ("rw opc %#x ", xs->cmd.opcode));

		/* A read or write operation. */
		if (xs->cmdlen == 6) {
			rw = (struct scsi_rw *)&xs->cmd;
			blockno = _3btol(rw->addr) &
				(SRW_TOPADDR << 16 | 0xffff);
			blockcnt = rw->length ? rw->length : 0x100;
		} else {
			rw10 = (struct scsi_rw_10 *)&xs->cmd;
			blockno = _4btol(rw10->addr);
			blockcnt = _2btol(rw10->length);
		}

		AAC_DPRINTF(AAC_D_CMD, ("opcode=%d blkno=%d bcount=%d ",
					xs->cmd.opcode, blockno, blockcnt));

		if (blockno >= sc->aac_hdr[target].hd_size ||
		    blockno + blockcnt > sc->aac_hdr[target].hd_size) {
			AAC_DPRINTF(AAC_D_CMD, ("\n"));
			printf("%s: out of bounds %u-%u >= %u\n",
			       sc->aac_dev.dv_xname, blockno,
			       blockcnt, sc->aac_hdr[target].hd_size);
			/*
			 * XXX Should be XS_SENSE but that
			 * would require setting up a faked
			 * sense too.
			 */
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			goto ready;
		}

		cm = xs->io;
		aac_scrub_command(cm);

		/* fill out the command */
		cm->cm_data = (void *)xs->data;
		cm->cm_datalen = xs->datalen;
		cm->cm_complete = aac_bio_complete;
		cm->cm_private = xs;
		cm->cm_timestamp = getuptime();
		cm->cm_queue = AAC_ADAP_NORM_CMD_QUEUE;
		cm->cm_blkno = blockno;
		cm->cm_bcount = blockcnt;

		AAC_DPRINTF(AAC_D_CMD, ("\n"));
		aac_enqueue_bio(cm);
		aac_startio(sc);

		/* XXX what if enqueue did not start a transfer? */
		if (xs->flags & SCSI_POLL) {
			if (!aac_wait_command(cm, xs->timeout))
			{
				printf("%s: command timed out\n",
				       sc->aac_dev.dv_xname);
				xs->error = XS_DRIVER_STUFFUP;
				scsi_done(xs);
				splx(s);
				return;
			}
			scsi_done(xs);
		}
	}

 ready:
	splx(s);
	AAC_DPRINTF(AAC_D_CMD, ("%s: scsi_cmd complete\n",
				sc->aac_dev.dv_xname));
}

/*
 * Debugging and Diagnostics
 */

/*
 * Print some information about the controller.
 */
void
aac_describe_controller(struct aac_softc *sc)
{
	struct aac_fib *fib;
	struct aac_adapter_info	*info;

	aac_alloc_sync_fib(sc, &fib, 0);

	fib->data[0] = 0;
	if (aac_sync_fib(sc, RequestAdapterInfo, 0, fib, 1)) {
		printf("%s: RequestAdapterInfo failed 2\n",
		       sc->aac_dev.dv_xname);
		aac_release_sync_fib(sc);
		return;
	}
	info = (struct aac_adapter_info *)&fib->data[0];

	printf("%s: %s %dMHz, %dMB cache memory, %s\n", sc->aac_dev.dv_xname,
	       aac_describe_code(aac_cpu_variant, info->CpuVariant),
	       info->ClockSpeed, info->BufferMem / (1024 * 1024),
	       aac_describe_code(aac_battery_platform, info->batteryPlatform));

	/* save the kernel revision structure for later use */
	sc->aac_revision = info->KernelRevision;
	printf("%s: Kernel %d.%d-%d, Build %d, S/N %6X\n",
	       sc->aac_dev.dv_xname,
	       info->KernelRevision.external.comp.major,
	       info->KernelRevision.external.comp.minor,
	       info->KernelRevision.external.comp.dash,
	       info->KernelRevision.buildNumber,
	       (u_int32_t)(info->SerialNumber & 0xffffff));

	aac_release_sync_fib(sc);
}

/*
 * Look up a text description of a numeric error code and return a pointer to
 * same.
 */
char *
aac_describe_code(struct aac_code_lookup *table, u_int32_t code)
{
	int i;

	for (i = 0; table[i].string != NULL; i++)
		if (table[i].code == code)
			return(table[i].string);
	return(table[i + 1].string);
}

#ifdef AAC_DEBUG
/*
 * Print a FIB
 */
void
aac_print_fib(struct aac_softc *sc, struct aac_fib *fib, const char *caller)
{
	printf("%s: FIB @ %p\n", caller, fib);
	printf("  XferState %b\n", fib->Header.XferState, "\20"
	    "\1HOSTOWNED"
	    "\2ADAPTEROWNED"
	    "\3INITIALISED"
	    "\4EMPTY"
	    "\5FROMPOOL"
	    "\6FROMHOST"
	    "\7FROMADAP"
	    "\10REXPECTED"
	    "\11RNOTEXPECTED"
	    "\12DONEADAP"
	    "\13DONEHOST"
	    "\14HIGH"
	    "\15NORM"
	    "\16ASYNC"
	    "\17PAGEFILEIO"
	    "\20SHUTDOWN"
	    "\21LAZYWRITE"
	    "\22ADAPMICROFIB"
	    "\23BIOSFIB"
	    "\24FAST_RESPONSE"
	    "\25APIFIB\n");
	printf("  Command         %d\n", fib->Header.Command);
	printf("  StructType      %d\n", fib->Header.StructType);
	printf("  Flags           0x%x\n", fib->Header.Flags);
	printf("  Size            %d\n", fib->Header.Size);
	printf("  SenderSize      %d\n", fib->Header.SenderSize);
	printf("  SenderAddress   0x%x\n", fib->Header.SenderFibAddress);
	printf("  ReceiverAddress 0x%x\n", fib->Header.ReceiverFibAddress);
	printf("  SenderData      0x%x\n", fib->Header.SenderData);
	switch(fib->Header.Command) {
	case ContainerCommand: {
		struct aac_blockread *br = (struct aac_blockread *)fib->data;
		struct aac_blockwrite *bw = (struct aac_blockwrite *)fib->data;
		struct aac_sg_table *sg = NULL;
		int i;

		if (br->Command == VM_CtBlockRead) {
			printf("  BlockRead: container %d  0x%x/%d\n",
			    br->ContainerId, br->BlockNumber, br->ByteCount);
			    sg = &br->SgMap;
		}
		if (bw->Command == VM_CtBlockWrite) {
			printf("  BlockWrite: container %d  0x%x/%d (%s)\n",
			    bw->ContainerId, bw->BlockNumber, bw->ByteCount,
			    bw->Stable == CSTABLE ? "stable" : "unstable");
			sg = &bw->SgMap;
		}
		if (sg != NULL) {
			printf("  %d s/g entries\n", sg->SgCount);
			for (i = 0; i < sg->SgCount; i++)
				printf("  0x%08x/%d\n",
				       sg->SgEntry[i].SgAddress,
				       sg->SgEntry[i].SgByteCount);
		}
		break;
	}
	default:
		printf("   %16D\n", fib->data, " ");
		printf("   %16D\n", fib->data + 16, " ");
	break;
	}
}

/*
 * Describe an AIF we have received.
 */
void
aac_print_aif(struct aac_softc *sc, struct aac_aif_command *aif)
{
	printf("%s: print_aif: ", sc->aac_dev.dv_xname);

	switch(aif->command) {
	case AifCmdEventNotify:
		printf("EventNotify(%d)\n", aif->seqNumber);

		switch(aif->data.EN.type) {
		case AifEnGeneric:
			/* Generic notification */
			printf("\t(Generic) %.*s\n",
			       (int)sizeof(aif->data.EN.data.EG),
			       aif->data.EN.data.EG.text);
			break;
		case AifEnTaskComplete:
			/* Task has completed */
			printf("\t(TaskComplete)\n");
			break;
		case AifEnConfigChange:
			/* Adapter configuration change occurred */
			printf("\t(ConfigChange)\n");
			break;
		case AifEnContainerChange:
			/* Adapter specific container configuration change */
			printf("\t(ContainerChange) container %d,%d\n",
			       aif->data.EN.data.ECC.container[0],
			       aif->data.EN.data.ECC.container[1]);
			break;
		case AifEnDeviceFailure:
			/* SCSI device failed */
			printf("\t(DeviceFailure) handle %d\n",
			       aif->data.EN.data.EDF.deviceHandle);
			break;
		case AifEnMirrorFailover:
			/* Mirror failover started */
			printf("\t(MirrorFailover) container %d failed, "
			       "migrating from slice %d to %d\n",
			       aif->data.EN.data.EMF.container,
			       aif->data.EN.data.EMF.failedSlice,
			       aif->data.EN.data.EMF.creatingSlice);
			break;
		case AifEnContainerEvent:
			/* Significant container event */
			printf("\t(ContainerEvent) container %d event %d\n",
			       aif->data.EN.data.ECE.container,
			       aif->data.EN.data.ECE.eventType);
			break;
		case AifEnFileSystemChange:
			/* File system changed */
			printf("\t(FileSystemChange)\n");
			break;
		case AifEnConfigPause:
			/* Container pause event */
			printf("\t(ConfigPause)\n");
			break;
		case AifEnConfigResume:
			/* Container resume event */
			printf("\t(ConfigResume)\n");
			break;
		case AifEnFailoverChange:
			/* Failover space assignment changed */
			printf("\t(FailoverChange)\n");
			break;
		case AifEnRAID5RebuildDone:
			/* RAID5 rebuild finished */
			printf("\t(RAID5RebuildDone)\n");
			break;
		case AifEnEnclosureManagement:
			/* Enclosure management event */
			printf("\t(EnclosureManagement) EMPID %d unit %d "
			       "event %d\n",
			       aif->data.EN.data.EEE.empID,
			       aif->data.EN.data.EEE.unitID,
			       aif->data.EN.data.EEE.eventType);
			break;
		case AifEnBatteryEvent:
			/* Significant NV battery event */
			printf("\t(BatteryEvent) %d (state was %d, is %d\n",
			       aif->data.EN.data.EBE.transition_type,
			       aif->data.EN.data.EBE.current_state,
			       aif->data.EN.data.EBE.prior_state);
			break;
		case AifEnAddContainer:
			/* A new container was created. */
			printf("\t(AddContainer)\n");
			break;
		case AifEnDeleteContainer:
			/* A container was deleted. */
			printf("\t(DeleteContainer)\n");
			break;
		case AifEnBatteryNeedsRecond:
			/* The battery needs reconditioning */
			printf("\t(BatteryNeedsRecond)\n");
			break;
		case AifEnClusterEvent:
			/* Some cluster event */
			printf("\t(ClusterEvent) event %d\n",
			       aif->data.EN.data.ECLE.eventType);
			break;
		case AifEnDiskSetEvent:
			/* A disk set event occurred. */
			printf("(DiskSetEvent) event %d "
			       "diskset %lld creator %lld\n",
			       aif->data.EN.data.EDS.eventType,
			       aif->data.EN.data.EDS.DsNum,
			       aif->data.EN.data.EDS.CreatorId);
			break;
		case AifDenMorphComplete:
			/* A morph operation completed */
			printf("\t(MorphComplete)\n");
			break;
		case AifDenVolumeExtendComplete:
			/* A volume expand operation completed */
			printf("\t(VolumeExtendComplete)\n");
			break;
		default:
			printf("\t(%d)\n", aif->data.EN.type);
			break;
		}
		break;
	case AifCmdJobProgress:
	{
		char	*status;
		switch(aif->data.PR[0].status) {
		case AifJobStsSuccess:
			status = "success"; break;
		case AifJobStsFinished:
			status = "finished"; break;
		case AifJobStsAborted:
			status = "aborted"; break;
		case AifJobStsFailed:
			status = "failed"; break;
		case AifJobStsSuspended:
			status = "suspended"; break;
		case AifJobStsRunning:
			status = "running"; break;
		default:
			status = "unknown status"; break;
		}

		printf("JobProgress (%d) - %s (%d, %d)\n",
		       aif->seqNumber, status,
		       aif->data.PR[0].currentTick,
		       aif->data.PR[0].finalTick);

		switch(aif->data.PR[0].jd.type) {
		case AifJobScsiZero:
			/* SCSI dev clear operation */
			printf("\t(ScsiZero) handle %d\n",
				      aif->data.PR[0].jd.client.scsi_dh);
			break;
		case AifJobScsiVerify:
			/* SCSI device Verify operation NO REPAIR */
			printf("\t(ScsiVerify) handle %d\n",
				      aif->data.PR[0].jd.client.scsi_dh);
			break;
		case AifJobScsiExercise:
			/* SCSI device Exercise operation */
			printf("\t(ScsiExercise) handle %d\n",
			       aif->data.PR[0].jd.client.scsi_dh);
			break;
		case AifJobScsiVerifyRepair:
			/* SCSI device Verify operation WITH repair */
			printf("\t(ScsiVerifyRepair) handle %d\n",
			       aif->data.PR[0].jd.client.scsi_dh);
			break;
		case AifJobCtrZero:
			/* Container clear operation */
			printf("\t(ContainerZero) container %d\n",
			       aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrCopy:
			/* Container copy operation */
			printf("\t(ContainerCopy) container %d to %d\n",
			       aif->data.PR[0].jd.client.container.src,
			       aif->data.PR[0].jd.client.container.dst);
			break;
		case AifJobCtrCreateMirror:
			/* Container Create Mirror operation */
			printf("\t(ContainerCreateMirror) container %d\n",
			       aif->data.PR[0].jd.client.container.src);
			/* XXX two containers? */
			break;
		case AifJobCtrMergeMirror:
			/* Container Merge Mirror operation */
			printf("\t(ContainerMergeMirror) container %d\n",
			       aif->data.PR[0].jd.client.container.src);
			/* XXX two containers? */
			break;
		case AifJobCtrScrubMirror:
			/* Container Scrub Mirror operation */
			printf("\t(ContainerScrubMirror) container %d\n",
			       aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrRebuildRaid5:
			/* Container Rebuild Raid5 operation */
			printf("\t(ContainerRebuildRaid5) container %d\n",
			       aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrScrubRaid5:
			/* Container Scrub Raid5 operation */
			printf("\t(ContainerScrubRaid5) container %d\n",
			       aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrMorph:
			/* Container morph operation */
			printf("\t(ContainerMorph) container %d\n",
			       aif->data.PR[0].jd.client.container.src);
			/* XXX two containers? */
			break;
		case AifJobCtrPartCopy:
			/* Container Partition copy operation */
			printf("\t(ContainerPartCopy) container %d to %d\n",
			       aif->data.PR[0].jd.client.container.src,
			       aif->data.PR[0].jd.client.container.dst);
			break;
		case AifJobCtrRebuildMirror:
			/* Container Rebuild Mirror operation */
			printf("\t(ContainerRebuildMirror) container %d\n",
			       aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrCrazyCache:
			/* crazy cache */
			printf("\t(ContainerCrazyCache) container %d\n",
			       aif->data.PR[0].jd.client.container.src);
			/* XXX two containers? */
			break;
		case AifJobFsCreate:
			/* File System Create operation */
			printf("\t(FsCreate)\n");
			break;
		case AifJobFsVerify:
			/* File System Verify operation */
			printf("\t(FsVerify)\n");
			break;
		case AifJobFsExtend:
			/* File System Extend operation */
			printf("\t(FsExtend)\n");
			break;
		case AifJobApiFormatNTFS:
			/* Format a drive to NTFS */
			printf("\t(FormatNTFS)\n");
			break;
		case AifJobApiFormatFAT:
			/* Format a drive to FAT */
			printf("\t(FormatFAT)\n");
			break;
		case AifJobApiUpdateSnapshot:
			/* update the read/write half of a snapshot */
			printf("\t(UpdateSnapshot)\n");
			break;
		case AifJobApiFormatFAT32:
			/* Format a drive to FAT32 */
			printf("\t(FormatFAT32)\n");
			break;
		case AifJobCtlContinuousCtrVerify:
			/* Adapter operation */
			printf("\t(ContinuousCtrVerify)\n");
			break;
		default:
			printf("\t(%d)\n", aif->data.PR[0].jd.type);
			break;
		}
		break;
	}
	case AifCmdAPIReport:
		printf("APIReport (%d)\n", aif->seqNumber);
		break;
	case AifCmdDriverNotify:
		printf("DriverNotify (%d)\n", aif->seqNumber);
		break;
	default:
		printf("AIF %d (%d)\n", aif->command, aif->seqNumber);
		break;
	}
}
#endif
