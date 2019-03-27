/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith
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

/*
 * Debugging levels:
 *  0 - quiet, only emit warnings
 *  1 - noisy, emit major function points and things done
 *  2 - extremely noisy, emit trace items in loops, etc.
 */
#ifdef MLX_DEBUG
#define debug(level, fmt, args...)	do { if (level <= MLX_DEBUG) printf("%s: " fmt "\n", __func__ , ##args); } while(0)
#define debug_called(level)		do { if (level <= MLX_DEBUG) printf(__func__ ": called\n"); } while(0)
#else
#define debug(level, fmt, args...)
#define debug_called(level)
#endif

/*
 * Regardless of the actual capacity of the controller, we will allocate space
 * for 64 s/g entries.  Typically controllers support 17 or 33 entries (64k or
 * 128k maximum transfer assuming 4k page size and non-optimal alignment), but
 * making that fit cleanly without crossing page boundaries requires rounding up
 * to the next power of two.
 */
#define MLX_MAXPHYS	(128 * 1024)
#define MLX_NSEG	64

#define MLX_NSLOTS	256		/* max number of command slots */

#define MLX_MAXDRIVES	32		/* max number of system drives */

/*
 * Structure describing a System Drive as attached to the controller.
 */
struct mlx_sysdrive 
{
    /* from MLX_CMD_ENQSYSDRIVE */
    u_int32_t		ms_size;
    int			ms_state;
    int			ms_raidlevel;

    /* synthetic geometry */
    int			ms_cylinders;
    int			ms_heads;
    int			ms_sectors;

    /* handle for attached driver */
    device_t		ms_disk;
};

/*
 * Per-command control structure.
 */
struct mlx_command 
{
    TAILQ_ENTRY(mlx_command)	mc_link;	/* list linkage */

    struct mlx_softc		*mc_sc;		/* controller that owns us */
    u_int8_t			mc_slot;	/* command slot we occupy */
    u_int16_t			mc_status;	/* command completion status */
    time_t			mc_timeout;	/* when this command expires */
    u_int8_t			mc_mailbox[16];	/* command mailbox */
    u_int32_t			mc_sgphys;	/* physical address of s/g array in controller space */
    int				mc_nsgent;	/* number of entries in s/g map */
    int				mc_flags;
#define MLX_CMD_DATAIN		(1<<0)
#define MLX_CMD_DATAOUT		(1<<1)
#define MLX_CMD_PRIORITY	(1<<2)		/* high-priority command */

    void			*mc_data;	/* data buffer */
    size_t			mc_length;
    bus_dmamap_t		mc_dmamap;	/* DMA map for data */
    u_int32_t			mc_dataphys;	/* data buffer base address controller space */

    void			(* mc_complete)(struct mlx_command *mc);	/* completion handler */
    void			*mc_private;	/* submitter-private data or wait channel */
    int				mc_command;
};

/*
 * Per-controller structure.
 */
struct mlx_softc 
{
    /* bus connections */
    device_t		mlx_dev;
    struct cdev *mlx_dev_t;
    struct resource	*mlx_mem;	/* mailbox interface window */
    int			mlx_mem_rid;
    int			mlx_mem_type;
    bus_dma_tag_t	mlx_parent_dmat;/* parent DMA tag */
    bus_dma_tag_t	mlx_buffer_dmat;/* data buffer DMA tag */
    struct resource	*mlx_irq;	/* interrupt */
    void		*mlx_intr;	/* interrupt handle */

    /* scatter/gather lists and their controller-visible mappings */
    struct mlx_sgentry	*mlx_sgtable;	/* s/g lists */
    u_int32_t		mlx_sgbusaddr;	/* s/g table base address in bus space */
    bus_dma_tag_t	mlx_sg_dmat;	/* s/g buffer DMA tag */
    bus_dmamap_t	mlx_sg_dmamap;	/* map for s/g buffers */
    
    /* controller limits and features */
    struct mlx_enquiry2	*mlx_enq2;
    int			mlx_feature;	/* controller features/quirks */
#define MLX_FEAT_PAUSEWORKS	(1<<0)	/* channel pause works as expected */

    /* controller queues and arrays */
    TAILQ_HEAD(, mlx_command)	mlx_freecmds;		/* command structures available for reuse */
    TAILQ_HEAD(, mlx_command)	mlx_work;		/* active commands */
    struct mlx_command	*mlx_busycmd[MLX_NSLOTS];	/* busy commands */
    int			mlx_busycmds;			/* count of busy commands */
    struct mlx_sysdrive	mlx_sysdrive[MLX_MAXDRIVES];	/* system drives */
    struct bio_queue_head mlx_bioq;			/* outstanding I/O operations */
    int			mlx_waitbufs;			/* number of bufs awaiting commands */

    /* controller status */
    int			mlx_geom;
#define MLX_GEOM_128_32		0	/* geoemetry translation modes */
#define MLX_GEOM_256_63		1
    int			mlx_state;
#define MLX_STATE_INTEN		(1<<0)	/* interrupts have been enabled */
#define MLX_STATE_SHUTDOWN	(1<<1)	/* controller is shut down */
#define MLX_STATE_OPEN		(1<<2)	/* control device is open */
#define MLX_STATE_SUSPEND	(1<<3)	/* controller is suspended */
#define	MLX_STATE_QFROZEN	(1<<4)  /* bio queue frozen */
    struct mtx		mlx_io_lock;
    struct sx		mlx_config_lock;
    struct callout	mlx_timeout;	/* periodic status monitor */
    time_t		mlx_lastpoll;	/* last time_second we polled for status */
    u_int16_t		mlx_lastevent;	/* sequence number of the last event we recorded */
    int			mlx_currevent;	/* sequence number last time we looked */
    int			mlx_background;	/* if != 0 rebuild or check is in progress */
#define MLX_BACKGROUND_CHECK		1	/* we started a check */
#define MLX_BACKGROUND_REBUILD		2	/* we started a rebuild */
#define MLX_BACKGROUND_SPONTANEOUS	3	/* it just happened somehow */
    struct mlx_rebuild_status mlx_rebuildstat;	/* last rebuild status */
    struct mlx_pause	mlx_pause;	/* pending pause operation details */

    int			mlx_flags;
#define MLX_SPINUP_REPORTED	(1<<0)	/* "spinning up drives" message displayed */
#define MLX_EVENTLOG_BUSY	(1<<1)	/* currently reading event log */

    /* interface-specific accessor functions */
    int			mlx_iftype;	/* interface protocol */
#define MLX_IFTYPE_2	2
#define MLX_IFTYPE_3	3
#define MLX_IFTYPE_4	4
#define MLX_IFTYPE_5	5
    int			(* mlx_tryqueue)(struct mlx_softc *sc, struct mlx_command *mc);
    int			(* mlx_findcomplete)(struct mlx_softc *sc, u_int8_t *slot, u_int16_t *status);
    void		(* mlx_intaction)(struct mlx_softc *sc, int action);
    int			(* mlx_fw_handshake)(struct mlx_softc *sc, int *error, int *param1, int *param2, int first);
#define MLX_INTACTION_DISABLE		0
#define MLX_INTACTION_ENABLE		1
};

#define	MLX_IO_LOCK(sc)			mtx_lock(&(sc)->mlx_io_lock)
#define	MLX_IO_UNLOCK(sc)		mtx_unlock(&(sc)->mlx_io_lock)
#define	MLX_IO_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->mlx_io_lock, MA_OWNED)
#define	MLX_CONFIG_LOCK(sc)		sx_xlock(&(sc)->mlx_config_lock)
#define	MLX_CONFIG_UNLOCK(sc)		sx_xunlock(&(sc)->mlx_config_lock)
#define	MLX_CONFIG_ASSERT_LOCKED(sc)	sx_assert(&(sc)->mlx_config_lock, SA_XLOCKED)

/*
 * Interface between bus connections and driver core.
 */
extern void		mlx_free(struct mlx_softc *sc);
extern int		mlx_attach(struct mlx_softc *sc);
extern void		mlx_startup(struct mlx_softc *sc);
extern void		mlx_intr(void *data);
extern int		mlx_detach(device_t dev);
extern int		mlx_shutdown(device_t dev);
extern int		mlx_suspend(device_t dev); 
extern int		mlx_resume(device_t dev);
extern d_open_t		mlx_open;
extern d_close_t	mlx_close;
extern d_ioctl_t	mlx_ioctl;

extern devclass_t	mlx_devclass;
extern devclass_t	mlxd_devclass;

/*
 * Mylex System Disk driver
 */
struct mlxd_softc 
{
    device_t		mlxd_dev;
    struct mlx_softc	*mlxd_controller;
    struct mlx_sysdrive	*mlxd_drive;
    struct disk		*mlxd_disk;
    int			mlxd_unit;
    int			mlxd_flags;
#define MLXD_OPEN	(1<<0)		/* drive is open (can't shut down) */
};

/*
 * Interface between driver core and disk driver (should be using a bus?)
 */
extern int	mlx_submit_buf(struct mlx_softc *sc, struct bio *bp);
extern int	mlx_submit_ioctl(struct mlx_softc *sc,
			struct mlx_sysdrive *drive, u_long cmd, 
			caddr_t addr, int32_t flag, struct thread *td);
extern void	mlxd_intr(struct bio *bp);


