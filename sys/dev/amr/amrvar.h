/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c) 1999,2000 Michael Smith
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
 * Copyright (c) 2002 Eric Moore
 * Copyright (c) 2002 LSI Logic Corporation
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
 * 3. The party using or redistributing the source code and binary forms
 *    agrees to the disclaimer below and the terms and conditions set forth
 *    herein.
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
 *
 *      $FreeBSD$
 */

#include <geom/geom_disk.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#define LSI_DESC_PCI "LSILogic MegaRAID 1.53"

#ifdef AMR_DEBUG
# define debug(level, fmt, args...)	do {if (level <= AMR_DEBUG) printf("%s: " fmt "\n", __func__ , ##args);} while(0)
# define debug_called(level)		do {if (level <= AMR_DEBUG) printf("%s: called\n", __func__);} while(0)
#else
# define debug(level, fmt, args...)	do {} while (0)
# define debug_called(level)		do {} while (0)
#endif
#define xdebug(fmt, args...)	printf("%s: " fmt "\n", __func__ , ##args)

/*
 * Per-logical-drive datastructure
 */
struct amr_logdrive
{
    u_int32_t	al_size;
    int		al_state;
    int		al_properties;
    
    /* synthetic geometry */
    int		al_cylinders;
    int		al_heads;
    int		al_sectors;
    
    /* driver */
    device_t	al_disk;
};

/*
 * Due to the difficulty of using the zone allocator to create a new
 * zone from within a module, we use our own clustering to reduce 
 * memory wastage due to allocating lots of these small structures.
 *
 * 16k gives us a little under 200 command structures, which should
 * normally be plenty.  We will grab more if we need them.
 */

#define AMR_CMD_CLUSTERSIZE	(16 * 1024)

typedef STAILQ_HEAD(, amr_command)	ac_qhead_t;
typedef STAILQ_ENTRY(amr_command)	ac_link_t;

union amr_ccb {
    struct amr_passthrough	ccb_pthru;
    struct amr_ext_passthrough	ccb_epthru;
    uint8_t			bytes[128];
};

/*
 * Per-command control structure.
 */
struct amr_command
{
    ac_link_t			ac_link;

    struct amr_softc		*ac_sc;
    u_int8_t			ac_slot;
    int				ac_status;	/* command completion status */
    union {
	struct amr_sgentry	*sg32;
	struct amr_sg64entry	*sg64;
    } ac_sg;
    u_int32_t			ac_sgbusaddr;
    u_int32_t			ac_sg64_lo;
    u_int32_t			ac_sg64_hi;
    struct amr_mailbox		ac_mailbox;
    int				ac_flags;
#define AMR_CMD_DATAIN		(1<<0)
#define AMR_CMD_DATAOUT		(1<<1)
#define AMR_CMD_CCB		(1<<2)
#define AMR_CMD_PRIORITY	(1<<4)
#define AMR_CMD_MAPPED		(1<<5)
#define AMR_CMD_SLEEP		(1<<6)
#define AMR_CMD_BUSY		(1<<7)
#define AMR_CMD_SG64		(1<<8)
#define AC_IS_SG64(ac)		((ac)->ac_flags & AMR_CMD_SG64)
    u_int			ac_retries;

    struct bio			*ac_bio;
    void			(* ac_complete)(struct amr_command *ac);
    void			*ac_private;

    void			*ac_data;
    size_t			ac_length;
    bus_dmamap_t		ac_dmamap;
    bus_dmamap_t		ac_dma64map;

    bus_dma_tag_t		ac_tag;
    bus_dmamap_t		ac_datamap;
    int				ac_nsegments;
    uint32_t			ac_mb_physaddr;

    union amr_ccb		*ac_ccb;
    uint32_t			ac_ccb_busaddr;
};

struct amr_command_cluster
{
    TAILQ_ENTRY(amr_command_cluster)	acc_link;
    struct amr_command		acc_command[0];
};

#define AMR_CMD_CLUSTERCOUNT	((AMR_CMD_CLUSTERSIZE - sizeof(struct amr_command_cluster)) /	\
				 sizeof(struct amr_command))

/*
 * Per-controller-instance data
 */
struct amr_softc 
{
    /* bus attachments */
    device_t			amr_dev;
    struct resource		*amr_reg;		/* control registers */
    bus_space_handle_t		amr_bhandle;
    bus_space_tag_t		amr_btag;
    bus_dma_tag_t		amr_parent_dmat;	/* parent DMA tag */
    bus_dma_tag_t		amr_buffer_dmat;	/* data buffer DMA tag */
    bus_dma_tag_t		amr_buffer64_dmat;
    struct resource		*amr_irq;		/* interrupt */
    void			*amr_intr;

    /* mailbox */
    volatile struct amr_mailbox		*amr_mailbox;
    volatile struct amr_mailbox64	*amr_mailbox64;
    u_int32_t			amr_mailboxphys;
    bus_dma_tag_t		amr_mailbox_dmat;
    bus_dmamap_t		amr_mailbox_dmamap;

    /* scatter/gather lists and their controller-visible mappings */
    struct amr_sgentry		*amr_sgtable;		/* s/g lists */
    struct amr_sg64entry	*amr_sg64table;		/* 64bit s/g lists */
    u_int32_t			amr_sgbusaddr;		/* s/g table base address in bus space */
    bus_dma_tag_t		amr_sg_dmat;		/* s/g buffer DMA tag */
    bus_dmamap_t		amr_sg_dmamap;		/* map for s/g buffers */

    union amr_ccb		*amr_ccb;
    uint32_t			amr_ccb_busaddr;
    bus_dma_tag_t		amr_ccb_dmat;
    bus_dmamap_t		amr_ccb_dmamap;

    /* controller limits and features */
    int				amr_nextslot;		/* Next slot to use for newly allocated commands */
    int				amr_maxio;		/* maximum number of I/O transactions */
    int				amr_maxdrives;		/* max number of logical drives */
    int				amr_maxchan;		/* count of SCSI channels */
    
    /* connected logical drives */
    struct amr_logdrive		amr_drive[AMR_MAXLD];

    /* controller state */
    int				amr_state;
#define AMR_STATE_OPEN		(1<<0)
#define AMR_STATE_SUSPEND	(1<<1)
#define AMR_STATE_INTEN		(1<<2)
#define AMR_STATE_SHUTDOWN	(1<<3)
#define AMR_STATE_CRASHDUMP	(1<<4)
#define AMR_STATE_QUEUE_FRZN	(1<<5)
#define AMR_STATE_LD_DELETE	(1<<6)
#define AMR_STATE_REMAP_LD	(1<<7)

    /* per-controller queues */
    struct bio_queue_head 	amr_bioq;		/* pending I/O with no commands */
    ac_qhead_t			amr_ready;		/* commands ready to be submitted */
    struct amr_command		*amr_busycmd[AMR_MAXCMD];
    int				amr_busyslots;
    ac_qhead_t			amr_freecmds;
    TAILQ_HEAD(,amr_command_cluster)	amr_cmd_clusters;

    /* CAM attachments for passthrough */
    struct cam_sim		*amr_cam_sim[AMR_MAX_CHANNELS];
    TAILQ_HEAD(, ccb_hdr)	amr_cam_ccbq;
    struct cam_devq		*amr_cam_devq;

    /* control device */
    struct cdev			*amr_dev_t;
    struct mtx			amr_list_lock;

    /* controller type-specific support */
    int				amr_type;
#define AMR_TYPE_QUARTZ		(1<<0)
#define AMR_IS_QUARTZ(sc)	((sc)->amr_type & AMR_TYPE_QUARTZ)
#define AMR_TYPE_40LD		(1<<1)
#define AMR_IS_40LD(sc)		((sc)->amr_type & AMR_TYPE_40LD)
#define AMR_TYPE_SG64		(1<<2)
#define AMR_IS_SG64(sc)		((sc)->amr_type & AMR_TYPE_SG64)
    int				(* amr_submit_command)(struct amr_command *ac);
    int				(* amr_get_work)(struct amr_softc *sc, struct amr_mailbox *mbsave);
    int				(*amr_poll_command)(struct amr_command *ac);
    int				(*amr_poll_command1)(struct amr_softc *sc, struct amr_command *ac);
    int 			support_ext_cdb;	/* greater than 10 byte cdb support */

    /* misc glue */
    device_t			amr_pass;
    int				(*amr_cam_command)(struct amr_softc *sc, struct amr_command **acp);
    struct intr_config_hook	amr_ich;		/* wait-for-interrupts probe hook */
    int				amr_allow_vol_config;
    int				amr_linux_no_adapters;
    int				amr_ld_del_supported;
    struct mtx			amr_hw_lock;
};

/*
 * Interface between bus connections and driver core.
 */
extern int              amr_attach(struct amr_softc *sc);
extern void		amr_free(struct amr_softc *sc);
extern int		amr_flush(struct amr_softc *sc);
extern int		amr_done(struct amr_softc *sc);
extern void		amr_startio(struct amr_softc *sc);

/*
 * Command buffer allocation.
 */
extern struct amr_command	*amr_alloccmd(struct amr_softc *sc);
extern void			amr_releasecmd(struct amr_command *ac);

/*
 * MegaRAID logical disk driver
 */
struct amrd_softc 
{
    device_t		amrd_dev;
    struct amr_softc	*amrd_controller;
    struct amr_logdrive	*amrd_drive;
    struct disk		*amrd_disk;
    int			amrd_unit;
};

/*
 * Interface between driver core and disk driver (should be using a bus?)
 */
extern int	amr_submit_bio(struct amr_softc *sc, struct bio *bio);
extern int 	amr_dump_blocks(struct amr_softc *sc, int unit, u_int32_t lba, void *data, int blks);
extern void	amrd_intr(void *data);

/********************************************************************************
 * Enqueue/dequeue functions
 */
static __inline void
amr_enqueue_bio(struct amr_softc *sc, struct bio *bio)
{

    bioq_insert_tail(&sc->amr_bioq, bio);
}

static __inline struct bio *
amr_dequeue_bio(struct amr_softc *sc)
{
    struct bio	*bio;

    if ((bio = bioq_first(&sc->amr_bioq)) != NULL)
	bioq_remove(&sc->amr_bioq, bio);
    return(bio);
}

static __inline void
amr_init_qhead(ac_qhead_t *head)
{

	STAILQ_INIT(head);
}

static __inline void
amr_enqueue_ready(struct amr_command *ac)
{

    STAILQ_INSERT_TAIL(&ac->ac_sc->amr_ready, ac, ac_link);
}

static __inline void
amr_requeue_ready(struct amr_command *ac)
{

    STAILQ_INSERT_HEAD(&ac->ac_sc->amr_ready, ac, ac_link);
}

static __inline struct amr_command *
amr_dequeue_ready(struct amr_softc *sc)
{
    struct amr_command	*ac;

    if ((ac = STAILQ_FIRST(&sc->amr_ready)) != NULL)
	STAILQ_REMOVE_HEAD(&sc->amr_ready, ac_link);
    return(ac);
}

static __inline void
amr_enqueue_completed(struct amr_command *ac, ac_qhead_t *head)
{

    STAILQ_INSERT_TAIL(head, ac, ac_link);
}

static __inline struct amr_command *
amr_dequeue_completed(struct amr_softc *sc, ac_qhead_t *head)
{
    struct amr_command	*ac;

    if ((ac = STAILQ_FIRST(head)) != NULL)
	STAILQ_REMOVE_HEAD(head, ac_link);
    return(ac);
}

static __inline void
amr_enqueue_free(struct amr_command *ac)
{

    STAILQ_INSERT_HEAD(&ac->ac_sc->amr_freecmds, ac, ac_link);
}

static __inline struct amr_command *
amr_dequeue_free(struct amr_softc *sc)
{
    struct amr_command	*ac;

    if ((ac = STAILQ_FIRST(&sc->amr_freecmds)) != NULL)
	STAILQ_REMOVE_HEAD(&sc->amr_freecmds, ac_link);
    return(ac);
}
