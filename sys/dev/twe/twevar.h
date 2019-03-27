/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
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
 *	$FreeBSD$
 */

#define TWE_DRIVER_VERSION_STRING	"1.50.01.002"

#ifdef TWE_DEBUG
#define debug(level, fmt, args...)							\
	do {										\
	    if (level <= TWE_DEBUG) printf("%s: " fmt "\n", __func__ , ##args);	\
	} while(0)
#define debug_called(level)						\
	do {								\
	    if (level <= TWE_DEBUG) printf("%s: called\n", __func__);	\
	} while(0)
#else
#define debug(level, fmt, args...)
#define debug_called(level)
#endif

/*
 * Structure describing a logical unit as attached to the controller
 */
struct twe_drive
{
    /* unit properties */
    u_int32_t		td_size;
    int			td_cylinders;
    int			td_heads;
    int			td_sectors;
    int			td_sys_unit; /* device unit number */
    int			td_twe_unit; /* index into sc->twe_drive[] */

    /* unit state and type */
    u_int8_t		td_state;
    u_int8_t		td_type;
    u_int8_t		td_stripe;

    /* handle for attached driver */
    device_t		td_disk;
};

/*
 * Per-command control structure.
 *
 * Note that due to alignment constraints on the tc_command field, this *must* be 64-byte aligned.
 * We achieve this by placing the command at the beginning of the structure, and using malloc()
 * to allocate each structure.
 */
struct twe_request
{
    int				tr_tag;
    /* command payload */
    void			*tr_data;	/* data buffer */
    void			*tr_realdata;	/* copy of real data buffer pointer for alignment fixup */
    size_t			tr_length;

    TAILQ_ENTRY(twe_request)	tr_link;	/* list linkage */
    struct twe_softc		*tr_sc;		/* controller that owns us */
    int				tr_status;	/* command status */
#define TWE_CMD_SETUP		0	/* being assembled */
#define TWE_CMD_BUSY		1	/* submitted to controller */
#define TWE_CMD_COMPLETE	2	/* completed by controller (maybe with error) */
#define TWE_CMD_ERROR		3	/* encountered error, even before submission to controller */
    int				tr_flags;
#define TWE_CMD_DATAIN		(1<<0)
#define TWE_CMD_DATAOUT		(1<<1)
#define TWE_CMD_ALIGNBUF	(1<<2)	/* data in bio is misaligned, have to copy to/from private buffer */
#define TWE_CMD_SLEEPER		(1<<3)	/* owner is sleeping on this command */
#define TWE_CMD_IMMEDIATE	(1<<4)	/* immediate request */
#define TWE_CMD_MAPPED		(1<<5)
#define TWE_CMD_IN_PROGRESS	(1<<6)	/* bus_dmamap_load returned EINPROGRESS */
    void			(* tr_complete)(struct twe_request *tr);	/* completion handler */
    void			*tr_private;	/* submitter-private data or wait channel */

    TWE_PLATFORM_REQUEST	/* platform-specific request elements */
};

#define TWE_FIND_COMMAND(tr)					\
	(TWE_Command *)((u_int8_t *)(tr)->tr_sc->twe_cmd +	\
			((tr)->tr_tag * sizeof(TWE_Command)))
#define TWE_FIND_COMMANDPHYS(tr)	((tr)->tr_sc->twe_cmdphys +	\
					 ((tr)->tr_tag * sizeof(TWE_Command)))
	
/*
 * Per-controller state.
 */
struct twe_softc 
{
    /* controller queues and arrays */
    TAILQ_HEAD(, twe_request)	twe_free;			/* command structures available for reuse */
    struct bio_queue_head	twe_bioq;			/* outstanding I/O operations */
    TAILQ_HEAD(, twe_request)	twe_ready;			/* requests ready for the controller */
    TAILQ_HEAD(, twe_request)	twe_busy;			/* requests busy in the controller */
    TAILQ_HEAD(, twe_request)	twe_complete;			/* active commands (busy or waiting for completion) */
    struct twe_request		*twe_lookup[TWE_Q_LENGTH];	/* commands indexed by request ID */
    struct twe_drive	twe_drive[TWE_MAX_UNITS];		/* attached drives */

    /* asynchronous event handling */
    u_int16_t		twe_aen_queue[TWE_Q_LENGTH];	/* AENs queued for userland tool(s) */
    int			twe_aen_head, twe_aen_tail;	/* ringbuffer pointers for AEN queue */
    int			twe_wait_aen;    		/* wait-for-aen notification */
    char		twe_aen_buf[80];		/* AEN format buffer */

    /* controller status */
    int			twe_state;
#define TWE_STATE_INTEN		(1<<0)	/* interrupts have been enabled */
#define TWE_STATE_SHUTDOWN	(1<<1)	/* controller is shut down */
#define TWE_STATE_OPEN		(1<<2)	/* control device is open */
#define TWE_STATE_SUSPEND	(1<<3)	/* controller is suspended */
#define TWE_STATE_FRZN		(1<<4)	/* got EINPROGRESS */
#define TWE_STATE_CTLR_BUSY	(1<<5)	/* controller cmd queue full */
#define	TWE_STATE_DETACHING	(1<<6)	/* controller is being shut down */
    int			twe_host_id;
    struct twe_qstat	twe_qstat[TWEQ_COUNT];	/* queue statistics */

    TWE_PLATFORM_SOFTC		/* platform-specific softc elements */
};

/*
 * Interface between driver core and platform-dependant code.
 */
extern int	twe_setup(struct twe_softc *sc);		/* do early driver/controller setup */
extern void	twe_init(struct twe_softc *sc);			/* init controller */
extern void	twe_deinit(struct twe_softc *sc);		/* stop controller */
extern void	twe_intr(struct twe_softc *sc);			/* hardware interrupt signalled */
extern void	twe_startio(struct twe_softc *sc);
extern int	twe_start(struct twe_request *tr);
extern int	twe_dump_blocks(struct twe_softc *sc, int unit,	/* crashdump block write */
				u_int32_t lba, void *data, int nblks);
extern int	twe_ioctl(struct twe_softc *sc, u_long cmd,
				  void *addr);			/* handle user request */
extern void	twe_describe_controller(struct twe_softc *sc);	/* print controller info */
extern void	twe_print_controller(struct twe_softc *sc);
extern void	twe_enable_interrupts(struct twe_softc *sc);	/* enable controller interrupts */
extern void	twe_disable_interrupts(struct twe_softc *sc);	/* disable controller interrupts */

extern int	twe_attach_drive(struct twe_softc *sc,
					 struct twe_drive *dr); /* attach drive when found in twe_init */
extern int	twe_detach_drive(struct twe_softc *sc,
					 int unit);		/* detach drive */
extern void	twe_clear_pci_parity_error(struct twe_softc *sc);
extern void	twe_clear_pci_abort(struct twe_softc *sc);
extern void	twed_intr(struct bio *bp);				/* return bio from core */
extern struct twe_request *twe_allocate_request(struct twe_softc *sc, int tag);	/* allocate request structure */
extern void	twe_free_request(struct twe_request *tr);	/* free request structure */
extern int	twe_map_request(struct twe_request *tr);	/* make request visible to controller, do s/g */
extern void	twe_unmap_request(struct twe_request *tr);	/* cleanup after transfer, unmap */

/********************************************************************************
 * Queue primitives
 */
#define TWEQ_ADD(sc, qname)					\
	do {							\
	    struct twe_qstat *qs = &(sc)->twe_qstat[qname];	\
								\
	    qs->q_length++;					\
	    if (qs->q_length > qs->q_max)			\
		qs->q_max = qs->q_length;			\
	} while(0)

#define TWEQ_REMOVE(sc, qname)					\
	do {							\
	    struct twe_qstat *qs = &(sc)->twe_qstat[qname];	\
								\
	    qs->q_length--;					\
	    if (qs->q_length < qs->q_min)			\
		qs->q_min = qs->q_length;			\
	} while(0);

#define TWEQ_INIT(sc, qname)					\
	do {							\
	    sc->twe_qstat[qname].q_length = 0;			\
	    sc->twe_qstat[qname].q_max = 0;			\
	    sc->twe_qstat[qname].q_min = 0xFFFFFFFF;		\
	} while(0)


#define TWEQ_REQUEST_QUEUE(name, index)					\
static __inline void							\
twe_initq_ ## name (struct twe_softc *sc)				\
{									\
    TAILQ_INIT(&sc->twe_ ## name);					\
    TWEQ_INIT(sc, index);						\
}									\
static __inline void							\
twe_enqueue_ ## name (struct twe_request *tr)				\
{									\
    TAILQ_INSERT_TAIL(&tr->tr_sc->twe_ ## name, tr, tr_link);		\
    TWEQ_ADD(tr->tr_sc, index);						\
}									\
static __inline void							\
twe_requeue_ ## name (struct twe_request *tr)				\
{									\
    TAILQ_INSERT_HEAD(&tr->tr_sc->twe_ ## name, tr, tr_link);		\
    TWEQ_ADD(tr->tr_sc, index);						\
}									\
static __inline struct twe_request *					\
twe_dequeue_ ## name (struct twe_softc *sc)				\
{									\
    struct twe_request	*tr;						\
									\
    if ((tr = TAILQ_FIRST(&sc->twe_ ## name)) != NULL) {		\
	TAILQ_REMOVE(&sc->twe_ ## name, tr, tr_link);			\
	TWEQ_REMOVE(sc, index);						\
    }									\
    return(tr);								\
}									\
static __inline void							\
twe_remove_ ## name (struct twe_request *tr)				\
{									\
    TAILQ_REMOVE(&tr->tr_sc->twe_ ## name, tr, tr_link);		\
    TWEQ_REMOVE(tr->tr_sc, index);					\
}

TWEQ_REQUEST_QUEUE(free, TWEQ_FREE)
TWEQ_REQUEST_QUEUE(ready, TWEQ_READY)
TWEQ_REQUEST_QUEUE(busy, TWEQ_BUSY)
TWEQ_REQUEST_QUEUE(complete, TWEQ_COMPLETE)

/*
 * outstanding bio queue
 */
static __inline void
twe_initq_bio(struct twe_softc *sc)
{
    bioq_init(&sc->twe_bioq);
    TWEQ_INIT(sc, TWEQ_BIO);
}

static __inline void
twe_enqueue_bio(struct twe_softc *sc, struct bio *bp)
{
    bioq_insert_tail(&sc->twe_bioq, bp);
    TWEQ_ADD(sc, TWEQ_BIO);
}

static __inline struct bio *
twe_dequeue_bio(struct twe_softc *sc)
{
    struct bio	*bp;

    if ((bp = bioq_first(&sc->twe_bioq)) != NULL) {
	bioq_remove(&sc->twe_bioq, bp);
	TWEQ_REMOVE(sc, TWEQ_BIO);
    }
    return(bp);
}
