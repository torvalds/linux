/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Jonathan Lemon
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Disk driver for Mylex DAC960 RAID adapters.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/sx.h>

#include <sys/bus.h>
#include <sys/conf.h>

#include <machine/bus.h>
#include <sys/rman.h>

#include <geom/geom_disk.h>

#include <dev/mlx/mlxio.h>
#include <dev/mlx/mlxvar.h>
#include <dev/mlx/mlxreg.h>

/* prototypes */
static int mlxd_probe(device_t dev);
static int mlxd_attach(device_t dev);
static int mlxd_detach(device_t dev);

devclass_t		mlxd_devclass;

static device_method_t mlxd_methods[] = {
    DEVMETHOD(device_probe,	mlxd_probe),
    DEVMETHOD(device_attach,	mlxd_attach),
    DEVMETHOD(device_detach,	mlxd_detach),
    { 0, 0 }
};

static driver_t mlxd_driver = {
    "mlxd",
    mlxd_methods,
    sizeof(struct mlxd_softc)
};

DRIVER_MODULE(mlxd, mlx, mlxd_driver, mlxd_devclass, 0, 0);

static int
mlxd_open(struct disk *dp)
{
    struct mlxd_softc	*sc = (struct mlxd_softc *)dp->d_drv1;

    debug_called(1);
	
    if (sc == NULL)
	return (ENXIO);

    /* controller not active? */
    MLX_CONFIG_LOCK(sc->mlxd_controller);
    MLX_IO_LOCK(sc->mlxd_controller);
    if (sc->mlxd_controller->mlx_state & MLX_STATE_SHUTDOWN) {
	MLX_IO_UNLOCK(sc->mlxd_controller);
	MLX_CONFIG_UNLOCK(sc->mlxd_controller);
	return(ENXIO);
    }

    sc->mlxd_flags |= MLXD_OPEN;
    MLX_IO_UNLOCK(sc->mlxd_controller);
    MLX_CONFIG_UNLOCK(sc->mlxd_controller);
    return (0);
}

static int
mlxd_close(struct disk *dp)
{
    struct mlxd_softc	*sc = (struct mlxd_softc *)dp->d_drv1;

    debug_called(1);

    if (sc == NULL)
	return (ENXIO);
    MLX_CONFIG_LOCK(sc->mlxd_controller);
    MLX_IO_LOCK(sc->mlxd_controller);
    sc->mlxd_flags &= ~MLXD_OPEN;
    MLX_IO_UNLOCK(sc->mlxd_controller);
    MLX_CONFIG_UNLOCK(sc->mlxd_controller);
    return (0);
}

static int
mlxd_ioctl(struct disk *dp, u_long cmd, void *addr, int flag, struct thread *td)
{
    struct mlxd_softc	*sc = (struct mlxd_softc *)dp->d_drv1;
    int error;

    debug_called(1);
	
    if (sc == NULL)
	return (ENXIO);

    if ((error = mlx_submit_ioctl(sc->mlxd_controller, sc->mlxd_drive, cmd, addr, flag, td)) != ENOIOCTL) {
	debug(0, "mlx_submit_ioctl returned %d\n", error);
	return(error);
    }
    return (ENOTTY);
}

/*
 * Read/write routine for a buffer.  Finds the proper unit, range checks
 * arguments, and schedules the transfer.  Does not wait for the transfer
 * to complete.  Multi-page transfers are supported.  All I/O requests must
 * be a multiple of a sector in length.
 */
static void
mlxd_strategy(struct bio *bp)
{
    struct mlxd_softc	*sc = bp->bio_disk->d_drv1;

    debug_called(1);

    /* bogus disk? */
    if (sc == NULL) {
	bp->bio_error = EINVAL;
	bp->bio_flags |= BIO_ERROR;
	goto bad;
    }

    /* XXX may only be temporarily offline - sleep? */
    MLX_IO_LOCK(sc->mlxd_controller);
    if (sc->mlxd_drive->ms_state == MLX_SYSD_OFFLINE) {
	MLX_IO_UNLOCK(sc->mlxd_controller);
	bp->bio_error = ENXIO;
	bp->bio_flags |= BIO_ERROR;
	goto bad;
    }

    mlx_submit_buf(sc->mlxd_controller, bp);
    MLX_IO_UNLOCK(sc->mlxd_controller);
    return;

 bad:
    /*
     * Correctly set the bio to indicate a failed transfer.
     */
    bp->bio_resid = bp->bio_bcount;
    biodone(bp);
    return;
}

void
mlxd_intr(struct bio *bp)
{

    debug_called(1);
	
    if (bp->bio_flags & BIO_ERROR)
	bp->bio_error = EIO;
    else
	bp->bio_resid = 0;

    biodone(bp);
}

static int
mlxd_probe(device_t dev)
{

    debug_called(1);
	
    device_set_desc(dev, "Mylex System Drive");
    return (0);
}

static int
mlxd_attach(device_t dev)
{
    struct mlxd_softc	*sc = (struct mlxd_softc *)device_get_softc(dev);
    device_t		parent;
    char		*state;
    int			s1, s2;
    
    debug_called(1);

    parent = device_get_parent(dev);
    sc->mlxd_controller = (struct mlx_softc *)device_get_softc(parent);
    sc->mlxd_unit = device_get_unit(dev);
    sc->mlxd_drive = device_get_ivars(dev);
    sc->mlxd_dev = dev;

    switch(sc->mlxd_drive->ms_state) {
    case MLX_SYSD_ONLINE:
	state = "online";
	break;
    case MLX_SYSD_CRITICAL:
	state = "critical";
	break;
    case MLX_SYSD_OFFLINE:
	state = "offline";
	break;
    default:
	state = "unknown state";
    }

    device_printf(dev, "%uMB (%u sectors) RAID %d (%s)\n",
		  sc->mlxd_drive->ms_size / ((1024 * 1024) / MLX_BLKSIZE),
		  sc->mlxd_drive->ms_size, sc->mlxd_drive->ms_raidlevel, state);

    sc->mlxd_disk = disk_alloc();
    sc->mlxd_disk->d_open = mlxd_open;
    sc->mlxd_disk->d_close = mlxd_close;
    sc->mlxd_disk->d_ioctl = mlxd_ioctl;
    sc->mlxd_disk->d_strategy = mlxd_strategy;
    sc->mlxd_disk->d_name = "mlxd";
    sc->mlxd_disk->d_unit = sc->mlxd_unit;
    sc->mlxd_disk->d_drv1 = sc;
    sc->mlxd_disk->d_sectorsize = MLX_BLKSIZE;
    sc->mlxd_disk->d_mediasize = MLX_BLKSIZE * (off_t)sc->mlxd_drive->ms_size;
    sc->mlxd_disk->d_fwsectors = sc->mlxd_drive->ms_sectors;
    sc->mlxd_disk->d_fwheads = sc->mlxd_drive->ms_heads;

    /* 
     * Set maximum I/O size to the lesser of the recommended maximum and the practical
     * maximum except on v2 cards where the maximum is set to 8 pages.
     */
    if (sc->mlxd_controller->mlx_iftype == MLX_IFTYPE_2)
	sc->mlxd_disk->d_maxsize = 8 * MLX_PAGE_SIZE;
    else {
	s1 = sc->mlxd_controller->mlx_enq2->me_maxblk * MLX_BLKSIZE;
	s2 = (sc->mlxd_controller->mlx_enq2->me_max_sg - 1) * MLX_PAGE_SIZE;
	sc->mlxd_disk->d_maxsize = imin(s1, s2);
    }

    disk_create(sc->mlxd_disk, DISK_VERSION);

    return (0);
}

static int
mlxd_detach(device_t dev)
{
    struct mlxd_softc *sc = (struct mlxd_softc *)device_get_softc(dev);

    debug_called(1);

    disk_destroy(sc->mlxd_disk);

    return(0);
}

