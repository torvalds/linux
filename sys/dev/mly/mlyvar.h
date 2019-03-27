/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000, 2001 Michael Smith
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

/********************************************************************************
 ********************************************************************************
                                                     Driver Parameter Definitions
 ********************************************************************************
 ********************************************************************************/

/*
 * The firmware interface allows for a 16-bit command identifier.  A lookup
 * table this size (256k) would be too expensive, so we cap ourselves at a
 * reasonable limit.
 */
#define MLY_MAX_COMMANDS	256	/* max commands per controller */

/*
 * The firmware interface allows for a 16-bit s/g list length.  We limit 
 * ourselves to a reasonable maximum and ensure alignment.
 */
#define MLY_MAX_SGENTRIES	64	/* max S/G entries, limit 65535 */		

/*
 * The interval at which we poke the controller for status updates (in seconds).
 */
#define MLY_PERIODIC_INTERVAL	1

/********************************************************************************
 ********************************************************************************
                                                      Cross-version Compatibility
 ********************************************************************************
 ********************************************************************************/

# include <sys/taskqueue.h>

/********************************************************************************
 ********************************************************************************
                                                      Driver Variable Definitions
 ********************************************************************************
 ********************************************************************************/

/*
 * Debugging levels:
 *  0 - quiet, only emit warnings
 *  1 - noisy, emit major function points and things done
 *  2 - extremely noisy, emit trace items in loops, etc.
 */
#ifdef MLY_DEBUG
# define debug(level, fmt, args...)	do { if (level <= MLY_DEBUG) printf("%s: " fmt "\n", __func__ , ##args); } while(0)
# define debug_called(level)		do { if (level <= MLY_DEBUG) printf("%s: called\n", __func__); } while(0)
# define debug_struct(s)		printf("  SIZE %s: %d\n", #s, sizeof(struct s))
# define debug_union(s)			printf("  SIZE %s: %d\n", #s, sizeof(union s))
# define debug_field(s, f)		printf("  OFFSET %s.%s: %d\n", #s, #f, ((int)&(((struct s *)0)->f)))
extern void		mly_printstate0(void);
extern struct mly_softc	*mly_softc0;
#else
# define debug(level, fmt, args...)
# define debug_called(level)
# define debug_struct(s)
#endif

#define mly_printf(sc, fmt, args...)	device_printf(sc->mly_dev, fmt , ##args)

/*
 * Per-device structure, used to save persistent state on devices.
 *
 * Note that this isn't really Bus/Target/Lun since we don't support
 * lun != 0 at this time.
 */
struct mly_btl {
    int			mb_flags;
#define MLY_BTL_PHYSICAL	(1<<0)		/* physical device */
#define MLY_BTL_LOGICAL		(1<<1)		/* logical device */
#define MLY_BTL_PROTECTED	(1<<2)		/* device is protected - I/O not allowed */
#define MLY_BTL_RESCAN		(1<<3)		/* device needs to be rescanned */
    char		mb_name[16];		/* peripheral attached to this device */
    int			mb_state;		/* see 8.1 */
    int			mb_type;		/* see 8.2 */

    /* physical devices only */
    int			mb_speed;		/* interface transfer rate */
    int			mb_width;		/* interface width */
};

/*
 * Per-command control structure.
 */
struct mly_command {
    TAILQ_ENTRY(mly_command)	mc_link;	/* list linkage */

    struct mly_softc		*mc_sc;		/* controller that owns us */
    u_int16_t			mc_slot;	/* command slot we occupy */
    int				mc_flags;
#define MLY_CMD_BUSY		(1<<0)		/* command is being run, or ready to run, or not completed */
#define MLY_CMD_COMPLETE	(1<<1)		/* command has been completed */
#define MLY_CMD_MAPPED		(1<<3)		/* command has had its data mapped */
#define MLY_CMD_DATAIN		(1<<4)		/* data moves controller->system */
#define MLY_CMD_DATAOUT		(1<<5)		/* data moves system->controller */
#define MLY_CMD_CCB		(1<<6)		/* data is ccb. */
    u_int16_t			mc_status;	/* command completion status */
    u_int8_t			mc_sense;	/* sense data length */
    int32_t			mc_resid;	/* I/O residual count */

    union mly_command_packet	*mc_packet;	/* our controller command */
    u_int64_t			mc_packetphys;	/* physical address of the mapped packet */

    void			*mc_data;	/* data buffer */
    size_t			mc_length;	/* data length */
    bus_dmamap_t		mc_datamap;	/* DMA map for data */

    void	(* mc_complete)(struct mly_command *mc);	/* completion handler */
    void	*mc_private;					/* caller-private data */

    int				mc_timestamp;
};

/*
 * Command slot regulation.
 *
 * We can't use slot 0 due to the memory mailbox implementation.
 */
#define MLY_SLOT_START		1
#define MLY_SLOT_MAX		(MLY_SLOT_START + MLY_MAX_COMMANDS)

/*
 * Per-controller structure.
 */
struct mly_softc {
    /* bus connections */
    device_t		mly_dev;
    struct cdev *mly_dev_t;
    struct resource	*mly_regs_resource;	/* register interface window */
    int			mly_regs_rid;		/* resource ID */
    bus_dma_tag_t	mly_parent_dmat;	/* parent DMA tag */
    bus_dma_tag_t	mly_buffer_dmat;	/* data buffer/command DMA tag */
    struct resource	*mly_irq;		/* interrupt */
    int			mly_irq_rid;
    void		*mly_intr;		/* interrupt handle */

    /* scatter/gather lists and their controller-visible mappings */
    struct mly_sg_entry	*mly_sg_table;		/* s/g lists */
    u_int32_t		mly_sg_busaddr;		/* s/g table base address in bus space */
    bus_dma_tag_t	mly_sg_dmat;		/* s/g buffer DMA tag */
    bus_dmamap_t	mly_sg_dmamap;		/* map for s/g buffers */

    /* controller hardware interface */
    int			mly_hwif;
#define MLY_HWIF_I960RX		0
#define MLY_HWIF_STRONGARM	1
    u_int8_t		mly_doorbell_true;	/* xor map to make hardware doorbell 'true' bits into 1s */
    u_int8_t		mly_command_mailbox;	/* register offsets */
    u_int8_t		mly_status_mailbox;
    u_int8_t		mly_idbr;
    u_int8_t		mly_odbr;
    u_int8_t		mly_error_status;
    u_int8_t		mly_interrupt_status;
    u_int8_t		mly_interrupt_mask;
    struct mly_mmbox	*mly_mmbox;			/* kernel-space address of memory mailbox */
    u_int64_t		mly_mmbox_busaddr;		/* bus-space address of memory mailbox */
    bus_dma_tag_t	mly_mmbox_dmat;			/* memory mailbox DMA tag */
    bus_dmamap_t	mly_mmbox_dmamap;		/* memory mailbox DMA map */
    u_int32_t		mly_mmbox_command_index;	/* next index to use */
    u_int32_t		mly_mmbox_status_index;		/* index we next expect status at */

    /* controller features, limits and status */
    struct mtx		mly_lock;
    int			mly_state;
#define	MLY_STATE_OPEN		(1<<1)
#define MLY_STATE_INTERRUPTS_ON	(1<<2)
#define MLY_STATE_MMBOX_ACTIVE	(1<<3)
#define MLY_STATE_CAM_FROZEN	(1<<4)
    struct mly_ioctl_getcontrollerinfo	*mly_controllerinfo;
    struct mly_param_controller		*mly_controllerparam;
    struct mly_btl			mly_btl[MLY_MAX_CHANNELS][MLY_MAX_TARGETS];

    /* command management */
    struct mly_command		mly_command[MLY_MAX_COMMANDS];	/* commands */
    union mly_command_packet	*mly_packet;		/* command packets */
    bus_dma_tag_t		mly_packet_dmat;	/* packet DMA tag */
    bus_dmamap_t		mly_packetmap;		/* packet DMA map */
    u_int64_t			mly_packetphys;		/* packet array base address */
    TAILQ_HEAD(,mly_command)	mly_free;		/* commands available for reuse */
    TAILQ_HEAD(,mly_command)	mly_busy;
    TAILQ_HEAD(,mly_command)	mly_complete;		/* commands which have been returned by the controller */
    struct mly_qstat		mly_qstat[MLYQ_COUNT];	/* queue statistics */

    /* health monitoring */
    u_int32_t			mly_event_change;	/* event status change indicator */
    u_int32_t			mly_event_counter;	/* next event for which we anticpiate status */
    u_int32_t			mly_event_waiting;	/* next event the controller will post status for */
    struct callout		mly_periodic;		/* periodic event handling */

    /* CAM connection */
    struct cam_devq		*mly_cam_devq;			/* CAM device queue */
    struct cam_sim		*mly_cam_sim[MLY_MAX_CHANNELS];	/* CAM SIMs */
    struct cam_path		*mly_cam_path;			/* rescan path */
    int				mly_cam_channels;		/* total channel count */

    /* command-completion task */
    struct task			mly_task_complete;	/* deferred-completion task */
    int				mly_qfrzn_cnt;		/* Track simq freezes */

#ifdef MLY_DEBUG
    struct callout		mly_timeout;
#endif
};

#define	MLY_LOCK(sc)		mtx_lock(&(sc)->mly_lock)
#define	MLY_UNLOCK(sc)		mtx_unlock(&(sc)->mly_lock)
#define	MLY_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->mly_lock, MA_OWNED)

/*
 * Register access helpers.
 */
#define MLY_SET_REG(sc, reg, val)	bus_write_1(sc->mly_regs_resource, reg, val)
#define MLY_GET_REG(sc, reg)		bus_read_1 (sc->mly_regs_resource, reg)
#define MLY_GET_REG2(sc, reg)		bus_read_2 (sc->mly_regs_resource, reg)
#define MLY_GET_REG4(sc, reg)		bus_read_4 (sc->mly_regs_resource, reg)

#define MLY_SET_MBOX(sc, mbox, ptr)									\
	do {												\
	    bus_write_4(sc->mly_regs_resource, mbox,      *((u_int32_t *)ptr));		\
	    bus_write_4(sc->mly_regs_resource, mbox +  4, *((u_int32_t *)ptr + 1));	\
	    bus_write_4(sc->mly_regs_resource, mbox +  8, *((u_int32_t *)ptr + 2));	\
	    bus_write_4(sc->mly_regs_resource, mbox + 12, *((u_int32_t *)ptr + 3));	\
	} while(0);
#define MLY_GET_MBOX(sc, mbox, ptr)									\
	do {												\
	    *((u_int32_t *)ptr) = bus_read_4(sc->mly_regs_resource, mbox);		\
	    *((u_int32_t *)ptr + 1) = bus_read_4(sc->mly_regs_resource, mbox + 4);	\
	    *((u_int32_t *)ptr + 2) = bus_read_4(sc->mly_regs_resource, mbox + 8);	\
	    *((u_int32_t *)ptr + 3) = bus_read_4(sc->mly_regs_resource, mbox + 12);	\
	} while(0);

#define MLY_IDBR_TRUE(sc, mask)								\
	((((MLY_GET_REG((sc), (sc)->mly_idbr)) ^ (sc)->mly_doorbell_true) & (mask)) == (mask))
#define MLY_ODBR_TRUE(sc, mask)								\
	((MLY_GET_REG((sc), (sc)->mly_odbr) & (mask)) == (mask))
#define MLY_ERROR_VALID(sc)								\
	((((MLY_GET_REG((sc), (sc)->mly_error_status)) ^ (sc)->mly_doorbell_true) & (MLY_MSG_EMPTY)) == 0)

#define MLY_MASK_INTERRUPTS(sc)								\
	do {										\
	    MLY_SET_REG((sc), (sc)->mly_interrupt_mask, MLY_INTERRUPT_MASK_DISABLE);	\
	    sc->mly_state &= ~MLY_STATE_INTERRUPTS_ON;					\
	} while(0);
#define MLY_UNMASK_INTERRUPTS(sc)							\
	do {										\
	    MLY_SET_REG((sc), (sc)->mly_interrupt_mask, MLY_INTERRUPT_MASK_ENABLE);	\
	    sc->mly_state |= MLY_STATE_INTERRUPTS_ON;					\
	} while(0);

/*
 * Bus/target/logical ID-related macros.
 */
#define MLY_LOGDEV_ID(sc, bus, target)	(((bus) - (sc)->mly_controllerinfo->physical_channels_present) * \
					 MLY_MAX_TARGETS + (target))
#define MLY_LOGDEV_BUS(sc, logdev)	(((logdev) / MLY_MAX_TARGETS) + \
					 (sc)->mly_controllerinfo->physical_channels_present)
#define MLY_LOGDEV_TARGET(sc, logdev)	((logdev) % MLY_MAX_TARGETS)
#define MLY_BUS_IS_VIRTUAL(sc, bus)	((bus) >= (sc)->mly_controllerinfo->physical_channels_present)
#define MLY_BUS_IS_VALID(sc, bus)	(((bus) < (sc)->mly_cam_channels) && ((sc)->mly_cam_sim[(bus)] != NULL))

/********************************************************************************
 * Queue primitives
 */

#define MLYQ_ADD(sc, qname)					\
	do {							\
	    struct mly_qstat *qs = &(sc)->mly_qstat[qname];	\
								\
	    qs->q_length++;					\
	    if (qs->q_length > qs->q_max)			\
		qs->q_max = qs->q_length;			\
	} while(0)

#define MLYQ_REMOVE(sc, qname)    (sc)->mly_qstat[qname].q_length--
#define MLYQ_INIT(sc, qname)			\
	do {					\
	    sc->mly_qstat[qname].q_length = 0;	\
	    sc->mly_qstat[qname].q_max = 0;	\
	} while(0)


#define MLYQ_COMMAND_QUEUE(name, index)					\
static __inline void							\
mly_initq_ ## name (struct mly_softc *sc)				\
{									\
    TAILQ_INIT(&sc->mly_ ## name);					\
    MLYQ_INIT(sc, index);						\
}									\
static __inline void							\
mly_enqueue_ ## name (struct mly_command *mc)				\
{									\
									\
    TAILQ_INSERT_TAIL(&mc->mc_sc->mly_ ## name, mc, mc_link);		\
    MLYQ_ADD(mc->mc_sc, index);						\
}									\
static __inline void							\
mly_requeue_ ## name (struct mly_command *mc)				\
{									\
									\
    TAILQ_INSERT_HEAD(&mc->mc_sc->mly_ ## name, mc, mc_link);		\
    MLYQ_ADD(mc->mc_sc, index);						\
}									\
static __inline struct mly_command *					\
mly_dequeue_ ## name (struct mly_softc *sc)				\
{									\
    struct mly_command	*mc;						\
									\
    if ((mc = TAILQ_FIRST(&sc->mly_ ## name)) != NULL) {		\
	TAILQ_REMOVE(&sc->mly_ ## name, mc, mc_link);			\
	MLYQ_REMOVE(sc, index);						\
    }									\
    return(mc);								\
}									\
static __inline void							\
mly_remove_ ## name (struct mly_command *mc)				\
{									\
									\
    TAILQ_REMOVE(&mc->mc_sc->mly_ ## name, mc, mc_link);		\
    MLYQ_REMOVE(mc->mc_sc, index);					\
}									\
struct hack

MLYQ_COMMAND_QUEUE(free, MLYQ_FREE);
MLYQ_COMMAND_QUEUE(busy, MLYQ_BUSY);
MLYQ_COMMAND_QUEUE(complete, MLYQ_COMPLETE);

/********************************************************************************
 * space-fill a character string
 */
static __inline void
padstr(char *targ, char *src, int len)
{
    while (len-- > 0) {
	if (*src != 0) {
	    *targ++ = *src++;
	} else {
	    *targ++ = ' ';
	}
    }
}
