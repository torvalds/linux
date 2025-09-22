/*	$OpenBSD: aacvar.h,v 1.19 2024/10/22 21:50:02 jsg Exp $	*/

/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2000 Niklas Hallqvist
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
 *	$FreeBSD: /c/ncvs/src/sys/dev/aac/aacvar.h,v 1.1 2000/09/13 03:20:34 msmith Exp $
 */

/*
 * This driver would not have rewritten for OpenBSD if it was not for the
 * hardware donation from Nocom.  I want to thank them for their support.
 * Of course, credit should go to Mike Smith for the original work he did
 * in the FreeBSD driver where I found lots of inspiration.
 * - Niklas Hallqvist
 */

/* Debugging */
// #define AAC_DEBUG 0x0

#ifdef AAC_DEBUG
#define AAC_DPRINTF(mask, args) if (aac_debug & (mask)) printf args
#define AAC_D_INTR	0x001
#define AAC_D_MISC	0x002
#define AAC_D_CMD	0x004
#define AAC_D_QUEUE	0x008
#define AAC_D_IO	0x010
#define AAC_D_IOCTL	0x020
#define AAC_D_LOCK	0x040
#define AAC_D_THREAD	0x080
#define AAC_D_FIB	0x100
extern int aac_debug;

#define AAC_PRINT_FIB(sc, fib)  do { \
	if (aac_debug & AAC_D_FIB) \
		aac_print_fib((sc), (fib), __func__); \
} while (0)
#else
#define AAC_DPRINTF(mask, args)
#define AAC_PRINT_FIB(sc, fib)
#endif

struct aac_code_lookup {
	char	*string;
	u_int32_t code;
};

struct aac_softc;

/*
 * We allocate a small set of FIBs for the adapter to use to send us messages.
 */
#define AAC_ADAPTER_FIBS	8

/*
 * FIBs are allocated in page-size chunks and can grow up to the 512
 * limit imposed by the hardware.
 */
#define AAC_FIB_COUNT		(PAGE_SIZE/sizeof(struct aac_fib))
#define AAC_MAX_FIBS		512
#define AAC_FIBMAP_SIZE		(PAGE_SIZE)

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
 * booting.
 */
#define AAC_BOOT_TIMEOUT	(3 * 60)

/*
 * Timeout for immediate commands.
 */
#define AAC_IMMEDIATE_TIMEOUT	30

/*
 * Timeout for normal commands
 */
#define AAC_CMD_TIMEOUT		30		/* seconds */

/*
 * Rate at which we periodically check for timed out commands and kick the
 * controller.
 */
#define AAC_PERIODIC_INTERVAL	20		/* seconds */

/*
 * Wait this long for a lost interrupt to get detected.
 */
#define AAC_WATCH_TIMEOUT	10000		/* 10000 * 1ms = 10s */

/*
 * Delay 20ms after the qnotify in sync operations.  Experimentally deduced.
 */
#define AAC_SYNC_DELAY 20000

/*
 * The firmware interface allows for a 16-bit s/g list length.  We limit 
 * ourselves to a reasonable maximum and ensure alignment.
 */
#define AAC_MAXSGENTRIES	64	/* max S/G entries, limit 65535 */		

/*
 * We gather a number of adapter-visible items into a single structure.
 *
 * The ordering of this structure may be important; we copy the Linux driver:
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
	struct aac_fib ac_fibs[AAC_ADAPTER_FIBS];

	/* the init structure */
	struct aac_adapter_init	ac_init;

	/* arena within which the queue structures are kept */
	u_int8_t ac_qbuf[sizeof(struct aac_queue_table) + AAC_QUEUE_ALIGN];

	/* buffer for text messages from the controller */
	char	ac_printf[AAC_PRINTF_BUFSIZE];
    
	/* fib for synchronous commands */
	struct aac_fib ac_sync_fib;
};
#define AAC_COMMON_ALLOCSIZE (8192 + sizeof(struct aac_common))

/*
 * Interface operations
 */
struct aac_interface {
	int	(*aif_get_fwstatus)(struct aac_softc *);
	void	(*aif_qnotify)(struct aac_softc *, int);
	int	(*aif_get_istatus)(struct aac_softc *);
	void	(*aif_set_istatus)(struct aac_softc *, int);
	void	(*aif_set_mailbox)(struct aac_softc *, u_int32_t,
	    u_int32_t, u_int32_t, u_int32_t, u_int32_t);
	int	(*aif_get_mailbox)(struct aac_softc *, int mb);
	void	(*aif_set_interrupts)(struct aac_softc *, int);
};
extern struct aac_interface aac_fa_interface;
extern struct aac_interface aac_sa_interface;
extern struct aac_interface aac_rx_interface;
extern struct aac_interface aac_rkt_interface;

#define AAC_GET_FWSTATUS(sc)		((sc)->aac_if.aif_get_fwstatus(sc))
#define AAC_QNOTIFY(sc, qbit) \
	((sc)->aac_if.aif_qnotify((sc), (qbit)))
#define AAC_GET_ISTATUS(sc)		((sc)->aac_if.aif_get_istatus(sc))
#define AAC_CLEAR_ISTATUS(sc, mask) \
	((sc)->aac_if.aif_set_istatus((sc), (mask)))
#define AAC_SET_MAILBOX(sc, command, arg0, arg1, arg2, arg3) \
	do {								\
		((sc)->aac_if.aif_set_mailbox((sc), (command), (arg0),	\
		    (arg1), (arg2), (arg3)));				\
	} while(0)
#define AAC_GET_MAILBOX(sc, mb) \
	((sc)->aac_if.aif_get_mailbox(sc, (mb)))
#define	AAC_MASK_INTERRUPTS(sc)	\
	((sc)->aac_if.aif_set_interrupts((sc), 0))
#define AAC_UNMASK_INTERRUPTS(sc) \
	((sc)->aac_if.aif_set_interrupts((sc), 1))

#define AAC_SETREG4(sc, reg, val) \
	bus_space_write_4((sc)->aac_memt, (sc)->aac_memh, (reg), (val))
#define AAC_GETREG4(sc, reg) \
	bus_space_read_4((sc)->aac_memt, (sc)->aac_memh, (reg))
#define AAC_SETREG2(sc, reg, val) \
	bus_space_write_2((sc)->aac_memt, (sc)->aac_memh, (reg), (val))
#define AAC_GETREG2(sc, reg) \
	bus_space_read_2((sc)->aac_memt, (sc)->aac_memh, (reg))
#define AAC_SETREG1(sc, reg, val) \
	bus_space_write_1((sc)->aac_memt, (sc)->aac_memh, (reg), (val))
#define AAC_GETREG1(sc, reg) \
	bus_space_read_1((sc)->aac_memt, (sc)->aac_memh, (reg))

/* Define the OS version specific locks */
typedef struct rwlock aac_lock_t;
#define AAC_LOCK_INIT(l, s)	do { \
    rw_init((l), "aaclock"); \
    AAC_DPRINTF(AAC_D_LOCK, ("%s: init lock @%s: %d\n", \
	        sc->aac_dev.dv_xname, __FUNCTION__, __LINE__)); \
} while (0)

#define AAC_LOCK_ACQUIRE(l) do { \
    AAC_DPRINTF(AAC_D_LOCK, ("%s: lock @%s: %d\n", \
	        sc->aac_dev.dv_xname, __FUNCTION__, __LINE__)); \
    rw_enter_write((l)); \
} while (0)

#define AAC_LOCK_RELEASE(l) do { \
    rw_exit_write((l)); \
    AAC_DPRINTF(AAC_D_LOCK, ("%s: unlock @%s: %d\n", \
	        sc->aac_dev.dv_xname, __FUNCTION__, __LINE__)); \
} while (0)

/*
 * Per-container data structure
 */
struct aac_container {
	struct aac_mntobj co_mntobj;
	int				co_found;
	TAILQ_ENTRY(aac_container)	co_link;
};

/*
 * A command control block, one for each corresponding command index of the
 * controller.
 */
struct aac_command {
	TAILQ_ENTRY(aac_command) cm_link;	/* list linkage */

	struct aac_softc	*cm_sc;		/* controller that owns us */

	struct aac_fib 		*cm_fib;	/* FIB for this command */
	bus_addr_t		cm_fibphys;	/* bus address of the FIB */
	void			*cm_data;
	size_t			cm_datalen;
	bus_dmamap_t		cm_datamap;
	struct aac_sg_table	*cm_sgtable;	/* pointer to s/g table */

	u_int			cm_flags;
#define AAC_CMD_MAPPED		(1<<0)	/* command has had its data mapped */
#define AAC_CMD_DATAIN		(1<<1)	/* command involves data moving
					 * from controller to host */
#define AAC_CMD_DATAOUT		(1<<2)	/* command involves data moving
					 * from host to controller */
#define AAC_CMD_COMPLETED	(1<<3)	/* command has been completed */
#define AAC_CMD_TIMEDOUT	(1<<4)	/* command taken too long */
#define AAC_ON_AACQ_FREE	(1<<5)
#define AAC_ON_AACQ_READY	(1<<6)
#define AAC_ON_AACQ_BUSY	(1<<7)
#define AAC_ON_AACQ_BIO		(1<<8)
#define AAC_ON_AACQ_MASK	((1<<5)|(1<<6)|(1<<7)|(1<<8))
#define AAC_QUEUE_FRZN		(1<<9)	/* Freeze the processing of
					 * commands on the queue. */
#define AAC_ACF_WATCHDOG 	(1<<10)

	void			(*cm_complete)(struct aac_command *);
	void			*cm_private;
	u_int32_t		cm_blkno;
	u_int32_t		cm_bcount;
	time_t			cm_timestamp;	/* command creation time */
	int			cm_queue;
	int			cm_index;
};

struct aac_fibmap {
	TAILQ_ENTRY(aac_fibmap) fm_link;	/* list linkage */
	struct aac_fib		*aac_fibs;
	bus_dmamap_t		aac_fibmap;
 	bus_dma_segment_t	aac_seg;
	int			aac_nsegs;
	struct aac_command	*aac_commands;
};

/*
 * Command queue statistics
 */
#define AACQ_FREE       0
#define AACQ_BIO        1
#define AACQ_READY      2
#define AACQ_BUSY       3
#define AACQ_COUNT      4       /* total number of queues */

struct aac_qstat {
        u_int32_t       q_length;
        u_int32_t       q_max;
};

/*
 * Per-controller structure.
 */
struct aac_softc {
	struct device aac_dev;
	void   *aac_ih;

	bus_space_tag_t aac_memt;
	bus_space_handle_t aac_memh;
	bus_dma_tag_t aac_dmat;		/* parent DMA tag */

	/* controller features, limits and status */
	int	aac_state;
#define AAC_STATE_SUSPEND	(1<<0)
#define	AAC_STATE_OPEN		(1<<1)
#define AAC_STATE_INTERRUPTS_ON	(1<<2)
#define AAC_STATE_AIF_SLEEPER	(1<<3)
	struct FsaRevision aac_revision;

	int	aac_hwif;	/* controller hardware interface */
#define AAC_HWIF_I960RX		0
#define AAC_HWIF_STRONGARM	1
#define AAC_HWIF_FALCON		2
#define AAC_HWIF_RKT		3
#define AAC_HWIF_UNKNOWN	-1

	struct aac_common *aac_common;
	bus_dmamap_t		aac_common_map;
	u_int32_t aac_common_busaddr;
	struct aac_interface aac_if;

	/* command/fib resources */
	TAILQ_HEAD(,aac_fibmap)	aac_fibmap_tqh;
	u_int			total_fibs;
	struct aac_command	*aac_commands;
	struct scsi_iopool	aac_iopool;

	/* command management */
	struct mutex		 aac_free_mtx;
	TAILQ_HEAD(,aac_command) aac_free;	/* command structures 
						 * available for reuse */
	TAILQ_HEAD(,aac_command) aac_ready;	/* commands on hold for
						 * controller resources */
	TAILQ_HEAD(,aac_command) aac_busy;
	TAILQ_HEAD(,aac_command) aac_bio;

	/* command management */
	struct aac_queue_table *aac_queues;
	struct aac_queue_entry *aac_qentries[AAC_QUEUE_COUNT];

	struct aac_qstat	aac_qstat[AACQ_COUNT];	/* queue statistics */

	/* connected containers */
	TAILQ_HEAD(,aac_container)	aac_container_tqh;
	aac_lock_t		aac_container_lock;

	/* Protect the sync fib */
#define AAC_SYNC_LOCK_FORCE	(1 << 0)
	aac_lock_t		aac_sync_lock;

	aac_lock_t		aac_io_lock;

	struct {
		u_int8_t hd_present;
		u_int8_t hd_is_logdrv;
		u_int8_t hd_is_arraydrv;
		u_int8_t hd_is_master;
		u_int8_t hd_is_parity;
		u_int8_t hd_is_hotfix;
		u_int8_t hd_master_no;
		u_int8_t hd_lock;
		u_int8_t hd_heads;
		u_int8_t hd_secs;
		u_int16_t hd_devtype;
		u_int32_t hd_size;
		u_int8_t hd_ldr_no;
		u_int8_t hd_rw_attribs;
		u_int32_t hd_start_sec;
	} aac_hdr[AAC_MAX_CONTAINERS];
	int			aac_container_count;

	/* management interface */
	aac_lock_t		aac_aifq_lock;
	struct aac_aif_command	aac_aifq[AAC_AIFQ_LENGTH];
	int			aac_aifq_head;
	int			aac_aifq_tail;
	struct proc		*aifthread;
	int			aifflags;
#define AAC_AIFFLAGS_RUNNING	(1 << 0)
#define AAC_AIFFLAGS_AIF	(1 << 1)
#define	AAC_AIFFLAGS_EXIT	(1 << 2)
#define AAC_AIFFLAGS_EXITED	(1 << 3)
#define	AAC_AIFFLAGS_COMPLETE	(1 << 4)
#define AAC_AIFFLAGS_PRINTF	(1 << 5)
#define AAC_AIFFLAGS_PENDING	(AAC_AIFFLAGS_AIF | AAC_AIFFLAGS_COMPLETE | \
				 AAC_AIFFLAGS_PRINTF)

	u_int32_t		flags;
#define AAC_FLAGS_PERC2QC	(1 << 0)
#define	AAC_FLAGS_ENABLE_CAM	(1 << 1)	/* No SCSI passthrough */
#define	AAC_FLAGS_CAM_NORESET	(1 << 2)	/* Fake SCSI resets */
#define	AAC_FLAGS_CAM_PASSONLY	(1 << 3)	/* Only create pass devices */
#define	AAC_FLAGS_SG_64BIT	(1 << 4)	/* Use 64-bit S/G addresses */
#define	AAC_FLAGS_4GB_WINDOW	(1 << 5)	/* Device can access host mem
						 * 2GB-4GB range */
#define	AAC_FLAGS_NO4GB		(1 << 6)	/* Can't access host mem >2GB*/
#define	AAC_FLAGS_256FIBS	(1 << 7)	/* Can only do 256 commands */
#define	AAC_FLAGS_BROKEN_MEMMAP (1 << 8)	/* Broken HostPhysMemPages */

	u_int32_t		supported_options;
	int			aac_max_fibs;
	void			*aac_sdh;
};

int	aac_attach(struct aac_softc *);
int	aac_intr(void *);

/* These all require correctly aligned buffers */
static __inline__ void
aac_enc16(u_int8_t *addr, u_int16_t value)
{
	*(u_int16_t *)addr = htole16(value);
}

static __inline__ void
aac_enc32(u_int8_t *addr, u_int32_t value)
{
	*(u_int32_t *)addr = htole32(value);
}

static __inline__ u_int16_t
aac_dec16(u_int8_t *addr)
{
	return letoh16(*(u_int16_t *)addr);
}

static __inline__ u_int32_t
aac_dec32(u_int8_t *addr)
{
	return letoh32(*(u_int32_t *)addr);
}

/* Declarations copied from aac.c */
#ifdef AAC_DEBUG
void aac_print_fib(struct aac_softc *, struct aac_fib *, const char *);
void aac_print_aif(struct aac_softc *, struct aac_aif_command *);
#endif
void aac_handle_aif(struct aac_softc *, struct aac_fib *);





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
	AAC_DPRINTF(AAC_D_CMD, (": enqueue " #name));			\
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
	AAC_DPRINTF(AAC_D_CMD, (": requeue " #name));			\
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
		AAC_DPRINTF(AAC_D_CMD, (": dequeue " #name));		\
		if ((cm->cm_flags & AAC_ON_ ## index) == 0) {		\
			printf("dequeue - command %p not in queue, flags = %#x, "	\
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
	AAC_DPRINTF(AAC_D_CMD, (": remove " #name));			\
	if ((cm->cm_flags & AAC_ON_ ## index) == 0) {			\
		printf("remove - command %p not in queue, flags = %#x, "		\
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
AACQ_COMMAND_QUEUE(bio, AACQ_BIO);

static __inline void
aac_print_printf(struct aac_softc *sc)
{
	/*
	 * XXX We have the ability to read the length of the printf string
	 * from out of the mailboxes.
	 */
	printf("** %s: %.*s", sc->aac_dev.dv_xname, AAC_PRINTF_BUFSIZE,
	       sc->aac_common->ac_printf);
	sc->aac_common->ac_printf[0] = 0;
	AAC_QNOTIFY(sc, AAC_DB_PRINTF);
}
