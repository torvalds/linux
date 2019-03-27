/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *       Copyright (c) 2000-03 ICP vortex GmbH
 *       Copyright (c) 2002-03 Intel Corporation
 *       Copyright (c) 2003    Adaptec Inc.
 *       All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * iir_ctrl.c: Control functions and /dev entry points for /dev/iir*
 *
 * Written by: Achim Leubner <achim_leubner@adaptec.com>
 * Fixes/Additions: Boji Tony Kannanthanam <boji.t.kannanthanam@intel.com>
 *
 * $Id: iir_ctrl.c 1.3 2003/08/26 12:31:15 achim Exp $"
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/stat.h>
#include <sys/disklabel.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <machine/bus.h>

#include <dev/iir/iir.h>

/* Entry points and other prototypes */
static struct gdt_softc *gdt_minor2softc(struct cdev *dev, int minor_no);

static d_open_t		iir_open;
static d_close_t	iir_close;
static d_write_t	iir_write;
static d_read_t		iir_read;
static d_ioctl_t	iir_ioctl;

/* Normally, this is a static structure.  But we need it in pci/iir_pci.c */
static struct cdevsw iir_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	iir_open,
	.d_close =	iir_close,
	.d_read =	iir_read,
	.d_write =	iir_write,
	.d_ioctl =	iir_ioctl,
	.d_name =	"iir",
};

#ifndef SDEV_PER_HBA
static int sdev_made = 0;
static struct sx sdev_lock;
SX_SYSINIT(iir_sdev_lock, &sdev_lock, "iir sdev");
#endif
extern int gdt_cnt;
extern gdt_statist_t gdt_stat;

/*
 * Given a controller number,
 * make a special device and return the dev_t
 */
struct cdev *
gdt_make_dev(struct gdt_softc *gdt)
{
    struct cdev *dev;

#ifdef SDEV_PER_HBA
    dev = make_dev(&iir_cdevsw, 0, UID_ROOT, GID_OPERATOR,
                   S_IRUSR | S_IWUSR, "iir%d", unit);
    dev->si_drv1 = gdt;
#else
    sx_xlock(&sdev_lock);
    if (sdev_made)
        return (NULL);
    dev = make_dev(&iir_cdevsw, 0, UID_ROOT, GID_OPERATOR,
                   S_IRUSR | S_IWUSR, "iir");
    sdev_made = 1;
    sx_xunlock(&sdev_lock);
#endif
    return (dev);
}

void
gdt_destroy_dev(struct cdev *dev)
{
    if (dev != NULL)
        destroy_dev(dev);
}

/*
 * Given a minor device number,
 * return the pointer to its softc structure
 */
static struct gdt_softc *
gdt_minor2softc(struct cdev *dev, int minor_no)
{
#ifdef SDEV_PER_HBA

    return (dev->si_drv1);
#else
    devclass_t dc;
    device_t child;

    dc = devclass_find("iir");
    if (dc == NULL)
	return (NULL);
    child = devclass_get_device(dc, minor_no);
    if (child == NULL)
	return (NULL);
    return (device_get_softc(child));
#endif
}

static int
iir_open(struct cdev *dev, int flags, int fmt, struct thread * p)
{
    GDT_DPRINTF(GDT_D_DEBUG, ("iir_open()\n"));

    return (0);
}

static int
iir_close(struct cdev *dev, int flags, int fmt, struct thread * p)
{
    GDT_DPRINTF(GDT_D_DEBUG, ("iir_close()\n"));
                
    return (0);
}

static int
iir_write(struct cdev *dev, struct uio * uio, int ioflag)
{
    GDT_DPRINTF(GDT_D_DEBUG, ("iir_write()\n"));
                
    return (0);
}

static int
iir_read(struct cdev *dev, struct uio * uio, int ioflag)
{
    GDT_DPRINTF(GDT_D_DEBUG, ("iir_read()\n"));
                
    return (0);
}

/**
 * This is the control syscall interface.
 * It should be binary compatible with UnixWare,
 * if not totally syntatically so.
 */

static int
iir_ioctl(struct cdev *dev, u_long cmd, caddr_t cmdarg, int flags, struct thread * p)
{
    GDT_DPRINTF(GDT_D_DEBUG, ("iir_ioctl() cmd 0x%lx\n",cmd));

    ++gdt_stat.io_count_act;
    if (gdt_stat.io_count_act > gdt_stat.io_count_max)
        gdt_stat.io_count_max = gdt_stat.io_count_act;

    switch (cmd) {
      case GDT_IOCTL_GENERAL:
        {
            gdt_ucmd_t *ucmd;
            struct gdt_softc *gdt;

            ucmd = (gdt_ucmd_t *)cmdarg;
            gdt = gdt_minor2softc(dev, ucmd->io_node);
            if (gdt == NULL)
                return (ENXIO);
	    mtx_lock(&gdt->sc_lock);
            TAILQ_INSERT_TAIL(&gdt->sc_ucmd_queue, ucmd, links);
            ucmd->complete_flag = FALSE;
            gdt_next(gdt);
            if (!ucmd->complete_flag)
                (void) mtx_sleep(ucmd, &gdt->sc_lock, PCATCH | PRIBIO, "iirucw",
		    0);
	    mtx_unlock(&gdt->sc_lock);
            break;
        }

      case GDT_IOCTL_DRVERS:
      case GDT_IOCTL_DRVERS_OLD:
        *(int *)cmdarg = 
            (IIR_DRIVER_VERSION << 8) | IIR_DRIVER_SUBVERSION;
        break;

      case GDT_IOCTL_CTRTYPE:
      case GDT_IOCTL_CTRTYPE_OLD:
        {
            gdt_ctrt_t *p;
            struct gdt_softc *gdt; 
            
            p = (gdt_ctrt_t *)cmdarg;
            gdt = gdt_minor2softc(dev, p->io_node);
            if (gdt == NULL)
                return (ENXIO);
            /* only RP controllers */
            p->ext_type = 0x6000 | gdt->sc_device;
            if (gdt->sc_vendor == INTEL_VENDOR_ID_IIR) {
                p->oem_id = OEM_ID_INTEL;
                p->type = 0xfd;
                /* new -> subdevice into ext_type */
                if (gdt->sc_device >= 0x600)
                    p->ext_type = 0x6000 | gdt->sc_subdevice;
            } else {
                p->oem_id = OEM_ID_ICP;
                p->type = 0xfe;
                /* new -> subdevice into ext_type */
                if (gdt->sc_device >= 0x300)
                    p->ext_type = 0x6000 | gdt->sc_subdevice;
            }
            p->info = (gdt->sc_bus << 8) | (gdt->sc_slot << 3);
            p->device_id = gdt->sc_device;
            p->sub_device_id = gdt->sc_subdevice;
            break;
        }

      case GDT_IOCTL_OSVERS:
        {
            gdt_osv_t *p;

            p = (gdt_osv_t *)cmdarg;
            p->oscode = 10;
	    p->version = osreldate / 100000;
	    p->subversion = osreldate / 1000 % 100;
	    p->revision = 0;
            strcpy(p->name, ostype);
            break;
        }

      case GDT_IOCTL_CTRCNT:
        *(int *)cmdarg = gdt_cnt;
        break;

      case GDT_IOCTL_EVENT:
        {
            gdt_event_t *p;

            p = (gdt_event_t *)cmdarg;
            if (p->erase == 0xff) {
                if (p->dvr.event_source == GDT_ES_TEST)
                    p->dvr.event_data.size = sizeof(p->dvr.event_data.eu.test);
                else if (p->dvr.event_source == GDT_ES_DRIVER)
                    p->dvr.event_data.size= sizeof(p->dvr.event_data.eu.driver);
                else if (p->dvr.event_source == GDT_ES_SYNC)
                    p->dvr.event_data.size = sizeof(p->dvr.event_data.eu.sync);
                else
                    p->dvr.event_data.size = sizeof(p->dvr.event_data.eu.async);
                gdt_store_event(p->dvr.event_source, p->dvr.event_idx,
                                &p->dvr.event_data);
            } else if (p->erase == 0xfe) {
                gdt_clear_events();
            } else if (p->erase == 0) {
                p->handle = gdt_read_event(p->handle, &p->dvr);
            } else {
                gdt_readapp_event((u_int8_t)p->erase, &p->dvr);
            }
            break;
        }
        
      case GDT_IOCTL_STATIST:
        {
            gdt_statist_t *p;
            
            p = (gdt_statist_t *)cmdarg;
            bcopy(&gdt_stat, p, sizeof(gdt_statist_t));
            break;
        }

      default:
        break;
    }

    --gdt_stat.io_count_act;
    return (0);
}
