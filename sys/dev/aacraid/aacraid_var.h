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
 *
 *	$FreeBSD$
 */

#include <sys/bio.h>
#if __FreeBSD_version >= 800000
#include <sys/callout.h>
#endif
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>
#include <sys/selinfo.h>
#include <geom/geom_disk.h>

#define	AAC_TYPE_DEVO			1
#define	AAC_TYPE_ALPHA			2
#define	AAC_TYPE_BETA			3
#define	AAC_TYPE_RELEASE		4

#define	AAC_DRIVER_MAJOR_VERSION	3
#define	AAC_DRIVER_MINOR_VERSION	2
#define	AAC_DRIVER_BUGFIX_LEVEL		5
#define	AAC_DRIVER_TYPE			AAC_TYPE_RELEASE

#ifndef AAC_DRIVER_BUILD
# define AAC_DRIVER_BUILD 1
#endif

#if __FreeBSD_version <= 601000
#define bus_get_dma_tag(x)	NULL
#endif

/* **************************** NewBUS interrupt Crock ************************/
#if __FreeBSD_version < 700031
#define	aac_bus_setup_intr(d, i, f, U, if, ifa, hp)	\
	bus_setup_intr(d, i, f, if, ifa, hp)
#else
#define	aac_bus_setup_intr	bus_setup_intr
#endif

/* **************************** NewBUS CAM Support ****************************/
#if __FreeBSD_version < 700049
#define aac_xpt_bus_register(sim, parent, bus)	\
	xpt_bus_register(sim, bus)
#else
#define aac_xpt_bus_register	xpt_bus_register
#endif

/**************************** Kernel Thread Support ***************************/
#if __FreeBSD_version > 800001
#define aac_kthread_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg) \
	kproc_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg)
#define	aac_kthread_exit(status)	\
	kproc_exit(status)
#else
#define aac_kthread_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg) \
	kthread_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg)
#define	aac_kthread_exit(status)	\
	kthread_exit(status)
#endif

/*
 * Driver Parameter Definitions
 */

/*
 * We allocate a small set of FIBs for the adapter to use to send us messages.
 */
#define AAC_ADAPTER_FIBS	8

/*
 * The controller reports status events in AIFs.  We hang on to a number of
 * these in order to pass them out to user-space management tools.
 */
#define AAC_AIFQ_LENGTH		64

/*
 * Firmware messages are passed in the printf buffer.
 */
#define AAC_PRINTF_BUFSIZE	256

/*
 * We wait this many seconds for the adapter to come ready if it is still 
 * booting
 */
#define AAC_BOOT_TIMEOUT	(3 * 60)

/*
 * We wait this many seconds for the adapter to come ready  
 * after flash update
 */
#define AAC_FWUPD_TIMEOUT	(5 * 60)

/*
 * Timeout for sync. commands.
 */
#define AAC_SYNC_TIMEOUT	180		/* seconds */

/*
 * Timeout for normal commands
 */
#define AAC_CMD_TIMEOUT		180		/* seconds */

/*
 * Rate at which we periodically check for timed out commands and kick the
 * controller.
 */
#define AAC_PERIODIC_INTERVAL	20		/* seconds */

#define PASSTHROUGH_BUS		0
#define CONTAINER_BUS		1
/*
 * Per-container data structure
 */
struct aac_container
{
	struct aac_mntobj		co_mntobj;
	int				co_found;
	u_int32_t		co_uid;       
	TAILQ_ENTRY(aac_container)	co_link;
};

/*
 * Per-SIM data structure
 */
struct aac_cam;
struct aac_sim
{
	device_t		sim_dev;
	int			TargetsPerBus;
	int			BusNumber;
	int			BusType;
	int			InitiatorBusId;
	struct aac_softc	*aac_sc;
	struct aac_cam		*aac_cam;
	TAILQ_ENTRY(aac_sim)	sim_link;
};

/*
 * Per-disk structure
 */
struct aac_disk 
{
	device_t			ad_dev;
	struct aac_softc		*ad_controller;
	struct aac_container		*ad_container;
	struct disk			*ad_disk;
	int				ad_flags;
#define AAC_DISK_OPEN	(1<<0)
	int				ad_cylinders;
	int				ad_heads;
	int				ad_sectors;
	u_int64_t			ad_size;
	int				unit;
};

/*
 * Per-command control structure.
 */
struct aac_command
{
	TAILQ_ENTRY(aac_command) cm_link;	/* list linkage */

	struct aac_softc	*cm_sc;		/* controller that owns us */

	struct aac_fib		*cm_fib;	/* FIB associated with this
						 * command */
	u_int64_t		cm_fibphys;	/* bus address of the FIB */
	struct bio		*cm_data;	/* pointer to data in kernel
						 * space */
	u_int32_t		cm_datalen;	/* data length */
	bus_dmamap_t		cm_datamap;	/* DMA map for bio data */
	struct aac_sg_table	*cm_sgtable;	/* pointer to s/g table in
						 * command */
	int			cm_flags;
#define AAC_CMD_MAPPED		(1<<0)		/* command has had its data
						 * mapped */
#define AAC_CMD_DATAIN		(1<<1)		/* command involves data moving
						 * from controller to host */
#define AAC_CMD_DATAOUT		(1<<2)		/* command involves data moving
						 * from host to controller */
#define AAC_CMD_COMPLETED	(1<<3)		/* command has been completed */
#define AAC_CMD_TIMEDOUT	(1<<4)		/* command taken too long */
#define AAC_ON_AACQ_FREE	(1<<5)
#define AAC_ON_AACQ_READY	(1<<6)
#define AAC_ON_AACQ_BUSY	(1<<7)
#define AAC_ON_AACQ_AIF		(1<<8)
#define AAC_ON_AACQ_NORM	(1<<10)
#define AAC_ON_AACQ_MASK	((1<<5)|(1<<6)|(1<<7)|(1<<8)|(1<<10))
#define AAC_CMD_RESET		(1<<9)
#define AAC_CMD_FASTRESP	(1<<11)
#define AAC_CMD_WAIT		(1<<12)

	void			(* cm_complete)(struct aac_command *cm);
	union ccb 		*cm_ccb;
	time_t			cm_timestamp;	/* command creation time */
	int			cm_index;
	bus_dma_tag_t		cm_passthr_dmat;	/* passthrough buffer/command
							 * DMA tag */
};

struct aac_fibmap {
	TAILQ_ENTRY(aac_fibmap) fm_link;	/* list linkage */
	struct aac_fib		*aac_fibs;
	bus_dmamap_t		aac_fibmap;
	struct aac_command	*aac_commands;
};

/*
 * We gather a number of adapter-visible items into a single structure.
 *
 * The ordering of this strucure may be important; we copy the Linux driver:
 *
 * Adapter FIBs
 * Init struct
 * Queue headers (Comm Area)
 * Printf buffer
 *
 * In addition, we add:
 * Sync Fib
 */
struct aac_common {
	/* fibs for the controller to send us messages */
	struct aac_fib		ac_fibs[AAC_ADAPTER_FIBS];

	/* the init structure */
	struct aac_adapter_init	ac_init;

	/* buffer for text messages from the controller */
	char		       	ac_printf[AAC_PRINTF_BUFSIZE];
	
	/* fib for synchronous commands */
	struct aac_fib		ac_sync_fib;

	/* response buffer for SRC (new comm. type1) - must be last element */
	u_int32_t   ac_host_rrq[0];
};

/*
 * Interface operations
 */
struct aac_interface 
{
	int	(*aif_get_fwstatus)(struct aac_softc *sc);
	void	(*aif_qnotify)(struct aac_softc *sc, int qbit);
	int	(*aif_get_istatus)(struct aac_softc *sc);
	void	(*aif_clr_istatus)(struct aac_softc *sc, int mask);
	void	(*aif_set_mailbox)(struct aac_softc *sc, u_int32_t command,
				   u_int32_t arg0, u_int32_t arg1,
				   u_int32_t arg2, u_int32_t arg3);
	int	(*aif_get_mailbox)(struct aac_softc *sc, int mb);
	void	(*aif_access_devreg)(struct aac_softc *sc, int enable);
	int (*aif_send_command)(struct aac_softc *sc, struct aac_command *cm);
	int (*aif_get_outb_queue)(struct aac_softc *sc);
	void (*aif_set_outb_queue)(struct aac_softc *sc, int index);
};
extern struct aac_interface	aacraid_src_interface;
extern struct aac_interface	aacraid_srcv_interface;

#define AAC_GET_FWSTATUS(sc)		((sc)->aac_if.aif_get_fwstatus((sc)))
#define AAC_QNOTIFY(sc, qbit)		((sc)->aac_if.aif_qnotify((sc), (qbit)))
#define AAC_GET_ISTATUS(sc)		((sc)->aac_if.aif_get_istatus((sc)))
#define AAC_CLEAR_ISTATUS(sc, mask)	((sc)->aac_if.aif_clr_istatus((sc), \
					(mask)))
#define AAC_SET_MAILBOX(sc, command, arg0, arg1, arg2, arg3) \
	((sc)->aac_if.aif_set_mailbox((sc), (command), (arg0), (arg1), (arg2), \
	(arg3)))
#define AAC_GET_MAILBOX(sc, mb)		((sc)->aac_if.aif_get_mailbox((sc), \
					(mb)))
#define	AAC_ACCESS_DEVREG(sc, mode)	((sc)->aac_if.aif_access_devreg((sc), \
					mode))
#define AAC_SEND_COMMAND(sc, cm)	((sc)->aac_if.aif_send_command((sc), (cm)))
#define AAC_GET_OUTB_QUEUE(sc)		((sc)->aac_if.aif_get_outb_queue((sc)))
#define AAC_SET_OUTB_QUEUE(sc, idx)	((sc)->aac_if.aif_set_outb_queue((sc), (idx)))

#define AAC_MEM0_SETREG4(sc, reg, val)	bus_space_write_4(sc->aac_btag0, \
					sc->aac_bhandle0, reg, val)
#define AAC_MEM0_GETREG4(sc, reg)	bus_space_read_4(sc->aac_btag0, \
					sc->aac_bhandle0, reg)
#define AAC_MEM0_SETREG2(sc, reg, val)	bus_space_write_2(sc->aac_btag0, \
					sc->aac_bhandle0, reg, val)
#define AAC_MEM0_GETREG2(sc, reg)	bus_space_read_2(sc->aac_btag0, \
					sc->aac_bhandle0, reg)
#define AAC_MEM0_SETREG1(sc, reg, val)	bus_space_write_1(sc->aac_btag0, \
					sc->aac_bhandle0, reg, val)
#define AAC_MEM0_GETREG1(sc, reg)	bus_space_read_1(sc->aac_btag0, \
					sc->aac_bhandle0, reg)

#define AAC_MEM1_SETREG4(sc, reg, val)	bus_space_write_4(sc->aac_btag1, \
					sc->aac_bhandle1, reg, val)
#define AAC_MEM1_GETREG4(sc, reg)	bus_space_read_4(sc->aac_btag1, \
					sc->aac_bhandle1, reg)
#define AAC_MEM1_SETREG2(sc, reg, val)	bus_space_write_2(sc->aac_btag1, \
					sc->aac_bhandle1, reg, val)
#define AAC_MEM1_GETREG2(sc, reg)	bus_space_read_2(sc->aac_btag1, \
					sc->aac_bhandle1, reg)
#define AAC_MEM1_SETREG1(sc, reg, val)	bus_space_write_1(sc->aac_btag1, \
					sc->aac_bhandle1, reg, val)
#define AAC_MEM1_GETREG1(sc, reg)	bus_space_read_1(sc->aac_btag1, \
					sc->aac_bhandle1, reg)

/* fib context (IOCTL) */
struct aac_fib_context {
	u_int32_t		unique;
	int			ctx_idx;
	int			ctx_wrap;
	struct aac_fib_context *next, *prev;
};

/* MSIX context */
struct aac_msix_ctx {
	int			vector_no;	
	struct aac_softc  	*sc;
};

/*
 * Per-controller structure.
 */
struct aac_softc 
{
	/* bus connections */
	device_t		aac_dev;
	struct resource		*aac_regs_res0, *aac_regs_res1; /* reg. if. window */
	int			aac_regs_rid0, aac_regs_rid1;		/* resource ID */
	bus_space_handle_t	aac_bhandle0, aac_bhandle1;		/* bus space handle */
	bus_space_tag_t		aac_btag0, aac_btag1;		/* bus space tag */
	bus_dma_tag_t		aac_parent_dmat;	/* parent DMA tag */
	bus_dma_tag_t		aac_buffer_dmat;	/* data buffer/command
							 * DMA tag */
	struct resource		*aac_irq[AAC_MAX_MSIX];	 /* interrupt */
	int			aac_irq_rid[AAC_MAX_MSIX];
	void			*aac_intr[AAC_MAX_MSIX]; /* interrupt handle */
	struct aac_msix_ctx	aac_msix[AAC_MAX_MSIX]; /* context */
	eventhandler_tag	eh;
#if __FreeBSD_version >= 800000
	struct callout	aac_daemontime;		/* clock daemon callout */
#else	
	struct callout_handle	timeout_id;	/* timeout handle */
#endif

	/* controller features, limits and status */
	int			aac_state;
#define AAC_STATE_SUSPEND	(1<<0)
#define	AAC_STATE_UNUSED0	(1<<1)
#define AAC_STATE_INTERRUPTS_ON	(1<<2)
#define AAC_STATE_AIF_SLEEPER	(1<<3)
#define AAC_STATE_RESET		(1<<4)
	struct FsaRevision		aac_revision;

	/* controller hardware interface */
	int			aac_hwif;
#define AAC_HWIF_SRC		5
#define AAC_HWIF_SRCV		6
#define AAC_HWIF_UNKNOWN	-1
	bus_dma_tag_t		aac_common_dmat;	/* common structure
							 * DMA tag */
	bus_dmamap_t		aac_common_dmamap;	/* common structure
							 * DMA map */
	struct aac_common	*aac_common;
	u_int32_t		aac_common_busaddr;
	u_int32_t		aac_host_rrq_idx[AAC_MAX_MSIX];
	u_int32_t		aac_rrq_outstanding[AAC_MAX_MSIX];
	u_int32_t		aac_fibs_pushed_no;
	struct aac_interface	aac_if;

	/* command/fib resources */
	bus_dma_tag_t		aac_fib_dmat;	/* DMA tag for allocing FIBs */
	TAILQ_HEAD(,aac_fibmap)	aac_fibmap_tqh;
	u_int			total_fibs;
	struct aac_command	*aac_commands;

	/* command management */
	TAILQ_HEAD(,aac_command) aac_free;	/* command structures 
						 * available for reuse */
	TAILQ_HEAD(,aac_command) aac_ready;	/* commands on hold for
						 * controller resources */
	TAILQ_HEAD(,aac_command) aac_busy;
	TAILQ_HEAD(,aac_event)	aac_ev_cmfree;
	struct bio_queue_head	aac_bioq;

	struct aac_qstat	aac_qstat[AACQ_COUNT];	/* queue statistics */

	/* connected containters */
	TAILQ_HEAD(,aac_container)	aac_container_tqh;
	struct mtx		aac_container_lock;

	/*
	 * The general I/O lock.  This protects the sync fib, the lists, the
	 * queues, and the registers.
	 */
	struct mtx		aac_io_lock;

	struct intr_config_hook	aac_ich;

	/* sync. transfer mode */
	struct aac_command *aac_sync_cm;

	/* management interface */
	struct cdev *aac_dev_t;
	struct mtx		aac_aifq_lock;
	struct aac_fib		aac_aifq[AAC_AIFQ_LENGTH];
	int			aifq_idx;
	int			aifq_filled;
	int 			aif_pending;
	struct aac_fib_context *fibctx;
	struct selinfo		rcv_select;
	struct proc		*aifthread;
	int			aifflags;
#define AAC_AIFFLAGS_RUNNING	(1 << 0)
#define AAC_AIFFLAGS_AIF	(1 << 1)
#define	AAC_AIFFLAGS_EXIT	(1 << 2)
#define AAC_AIFFLAGS_EXITED	(1 << 3)
#define AAC_AIFFLAGS_PRINTF	(1 << 4)
#define	AAC_AIFFLAGS_ALLOCFIBS	(1 << 5)
#define AAC_AIFFLAGS_PENDING	(AAC_AIFFLAGS_AIF | AAC_AIFFLAGS_PRINTF | \
				 AAC_AIFFLAGS_ALLOCFIBS)
	u_int32_t		flags;
#define AAC_FLAGS_PERC2QC	(1 << 0)
#define	AAC_FLAGS_ENABLE_CAM	(1 << 1)	/* No SCSI passthrough */
#define	AAC_FLAGS_CAM_NORESET	(1 << 2)	/* Fake SCSI resets */
#define	AAC_FLAGS_CAM_PASSONLY	(1 << 3)	/* Only create pass devices */
#define	AAC_FLAGS_SG_64BIT	(1 << 4)	/* Use 64-bit S/G addresses */
#define	AAC_FLAGS_4GB_WINDOW	(1 << 5)	/* Device can access host mem
						 * 2GB-4GB range */
#define	AAC_FLAGS_NO4GB		(1 << 6)	/* Can't access host mem >2GB */
#define	AAC_FLAGS_256FIBS	(1 << 7)	/* Can only do 256 commands */
#define	AAC_FLAGS_BROKEN_MEMMAP (1 << 8)	/* Broken HostPhysMemPages */
#define AAC_FLAGS_SLAVE	(1 << 9)
#define AAC_FLAGS_MASTER	(1 << 10)
#define AAC_FLAGS_NEW_COMM	(1 << 11)	/* New comm. interface supported */
#define AAC_FLAGS_RAW_IO	(1 << 12)	/* Raw I/O interface */
#define AAC_FLAGS_ARRAY_64BIT	(1 << 13)	/* 64-bit array size */
#define AAC_FLAGS_LBA_64BIT	(1 << 14)	/* 64-bit LBA support */
#define AAC_QUEUE_FRZN		(1 << 15)	/* Freeze the processing of
						 * commands on the queue. */
#define AAC_FLAGS_NEW_COMM_TYPE1 (1 << 16)	/* New comm. type1 supported */
#define AAC_FLAGS_NEW_COMM_TYPE2 (1 << 17)	/* New comm. type2 supported */
#define AAC_FLAGS_NEW_COMM_TYPE34 (1 << 18)	/* New comm. type3/4 */
#define AAC_FLAGS_SYNC_MODE (1 << 18)	/* Sync. transfer mode */
	u_int32_t		hint_flags;		/* driver parameters */
	int	sim_freezed;				/* flag for sim_freeze/release */		
	u_int32_t		supported_options;
	u_int32_t		scsi_method_id;
	TAILQ_HEAD(,aac_sim)	aac_sim_tqh;

	u_int32_t	aac_max_fibs;           /* max. FIB count */
	u_int32_t	aac_max_fibs_alloc;		/* max. alloc. per alloc_commands() */
	u_int32_t	aac_max_fib_size;		/* max. FIB size */
	u_int32_t	aac_sg_tablesize;		/* max. sg count from host */
	u_int32_t	aac_max_sectors;		/* max. I/O size from host (blocks) */
	u_int32_t	aac_feature_bits;		/* feature bits from suppl. info */
	u_int32_t	aac_support_opt2;		/* supp. options from suppl. info */
	u_int32_t	aac_max_aif;			/* max. AIF count */
	u_int32_t	aac_max_msix;			/* max. MSI-X vectors */
	u_int32_t	aac_vector_cap;			/* MSI-X vector capab.*/
	int		msi_enabled;			/* MSI/MSI-X enabled */
#define AAC_CAM_TARGET_WILDCARD ~0
	void			(*cam_rescan_cb)(struct aac_softc *, uint32_t,
				    uint32_t);
	u_int32_t	DebugFlags;		/* Debug print flags bitmap */
	u_int32_t	DebugOffset;		/* Offset from DPMEM start */
	u_int32_t	DebugHeaderSize;	/* Size of debug header */
	u_int32_t	FwDebugFlags;		/* FW Debug Flags */
	u_int32_t	FwDebugBufferSize;	/* FW Debug Buffer size */	
};

/*
 * Event callback mechanism for the driver
 */
#define AAC_EVENT_NONE		0x00
#define AAC_EVENT_CMFREE	0x01
#define	AAC_EVENT_MASK		0xff
#define AAC_EVENT_REPEAT	0x100

typedef void aac_event_cb_t(struct aac_softc *sc, struct aac_event *event,
    void *arg);
struct aac_event {
	TAILQ_ENTRY(aac_event)	ev_links;
	int			ev_type;
	aac_event_cb_t		*ev_callback;
	void			*ev_arg;
};

/*
 * Public functions
 */
extern void		aacraid_free(struct aac_softc *sc);
extern int		aacraid_attach(struct aac_softc *sc);
extern int		aacraid_detach(device_t dev);
extern int		aacraid_shutdown(device_t dev);
extern int		aacraid_suspend(device_t dev); 
extern int		aacraid_resume(device_t dev);
extern void		aacraid_new_intr_type1(void *arg);
extern void		aacraid_submit_bio(struct bio *bp);
extern void		aacraid_biodone(struct bio *bp);
extern void		aacraid_startio(struct aac_softc *sc);
extern int		aacraid_alloc_command(struct aac_softc *sc,
					  struct aac_command **cmp);
extern void		aacraid_release_command(struct aac_command *cm);
extern void		aacraid_add_event(struct aac_softc *sc, struct aac_event
				      *event);
extern void		aacraid_map_command_sg(void *arg, bus_dma_segment_t *segs,
				int nseg, int error);
extern int		aacraid_wait_command(struct aac_command *cmp);

#ifdef AACRAID_DEBUG
# define fwprintf(sc, flags, fmt, args...)				\
	aacraid_fw_printf(sc, flags, "%s: " fmt, __func__, ##args);

extern void	aacraid_print_queues(struct aac_softc *sc);
extern void	aacraid_print_fib(struct aac_softc *sc, struct aac_fib *fib,
			      const char *caller);
extern void	aacraid_print_aif(struct aac_softc *sc,
			      struct aac_aif_command *aif);

#define AAC_PRINT_FIB(sc, fib)	aacraid_print_fib(sc, fib, __func__)

#else
# define fwprintf(sc, flags, fmt, args...)

# define aacraid_print_queues(sc)

# define AAC_PRINT_FIB(sc, fib)
# define aacraid_print_aif(sc, aac_aif_command)
#endif

struct aac_code_lookup {
	char	*string;
	u_int32_t	code;
};

/*
 * Queue primitives for driver queues.
 */
#define AACQ_ADD(sc, qname)					\
	do {							\
		struct aac_qstat *qs;				\
								\
		qs = &(sc)->aac_qstat[qname];			\
								\
		qs->q_length++;					\
		if (qs->q_length > qs->q_max)			\
			qs->q_max = qs->q_length;		\
	} while (0)

#define AACQ_REMOVE(sc, qname)    (sc)->aac_qstat[qname].q_length--
#define AACQ_INIT(sc, qname)				\
	do {						\
		sc->aac_qstat[qname].q_length = 0;	\
		sc->aac_qstat[qname].q_max = 0;		\
	} while (0)


#define AACQ_COMMAND_QUEUE(name, index)					\
static __inline void							\
aac_initq_ ## name (struct aac_softc *sc)				\
{									\
	TAILQ_INIT(&sc->aac_ ## name);					\
	AACQ_INIT(sc, index);						\
}									\
static __inline void							\
aac_enqueue_ ## name (struct aac_command *cm)				\
{									\
	if ((cm->cm_flags & AAC_ON_AACQ_MASK) != 0) {			\
		printf("command %p is on another queue, flags = %#x\n",	\
		       cm, cm->cm_flags);				\
		panic("command is on another queue");			\
	}								\
	TAILQ_INSERT_TAIL(&cm->cm_sc->aac_ ## name, cm, cm_link);	\
	cm->cm_flags |= AAC_ON_ ## index;				\
	AACQ_ADD(cm->cm_sc, index);					\
}									\
static __inline void							\
aac_requeue_ ## name (struct aac_command *cm)				\
{									\
	if ((cm->cm_flags & AAC_ON_AACQ_MASK) != 0) {			\
		printf("command %p is on another queue, flags = %#x\n",	\
		       cm, cm->cm_flags);				\
		panic("command is on another queue");			\
	}								\
	TAILQ_INSERT_HEAD(&cm->cm_sc->aac_ ## name, cm, cm_link);	\
	cm->cm_flags |= AAC_ON_ ## index;				\
	AACQ_ADD(cm->cm_sc, index);					\
}									\
static __inline struct aac_command *					\
aac_dequeue_ ## name (struct aac_softc *sc)				\
{									\
	struct aac_command *cm;						\
									\
	if ((cm = TAILQ_FIRST(&sc->aac_ ## name)) != NULL) {		\
		if ((cm->cm_flags & AAC_ON_ ## index) == 0) {		\
			printf("command %p not in queue, flags = %#x, "	\
		       	       "bit = %#x\n", cm, cm->cm_flags,		\
			       AAC_ON_ ## index);			\
			panic("command not in queue");			\
		}							\
		TAILQ_REMOVE(&sc->aac_ ## name, cm, cm_link);		\
		cm->cm_flags &= ~AAC_ON_ ## index;			\
		AACQ_REMOVE(sc, index);					\
	}								\
	return(cm);							\
}									\
static __inline void							\
aac_remove_ ## name (struct aac_command *cm)				\
{									\
	if ((cm->cm_flags & AAC_ON_ ## index) == 0) {			\
		printf("command %p not in queue, flags = %#x, "		\
		       "bit = %#x\n", cm, cm->cm_flags, 		\
		       AAC_ON_ ## index);				\
		panic("command not in queue");				\
	}								\
	TAILQ_REMOVE(&cm->cm_sc->aac_ ## name, cm, cm_link);		\
	cm->cm_flags &= ~AAC_ON_ ## index;				\
	AACQ_REMOVE(cm->cm_sc, index);					\
}									\
struct hack

AACQ_COMMAND_QUEUE(free, AACQ_FREE);
AACQ_COMMAND_QUEUE(ready, AACQ_READY);
AACQ_COMMAND_QUEUE(busy, AACQ_BUSY);

static __inline void
aac_print_printf(struct aac_softc *sc)
{
	/*
	 * XXX We have the ability to read the length of the printf string
	 * from out of the mailboxes.
	 */
	device_printf(sc->aac_dev, "**Monitor** %.*s", AAC_PRINTF_BUFSIZE,
		      sc->aac_common->ac_printf);
	sc->aac_common->ac_printf[0] = 0;
	AAC_QNOTIFY(sc, AAC_DB_PRINTF);
}

static __inline int
aac_alloc_sync_fib(struct aac_softc *sc, struct aac_fib **fib)
{

	mtx_assert(&sc->aac_io_lock, MA_OWNED);
	*fib = &sc->aac_common->ac_sync_fib;
	return (0);
}

static __inline void
aac_release_sync_fib(struct aac_softc *sc)
{

	mtx_assert(&sc->aac_io_lock, MA_OWNED);
}
