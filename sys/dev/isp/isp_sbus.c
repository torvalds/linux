/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997-2006 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
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
 * SBus specific probe and attach routines for Qlogic ISP SCSI adapters.
 * FreeBSD Version.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/linker.h>
#include <sys/firmware.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/ofw_machdep.h>
#include <machine/resource.h>
#include <sys/rman.h>
#include <sparc64/sbus/sbusvar.h>

#include <dev/isp/isp_freebsd.h>

static uint32_t isp_sbus_rd_reg(ispsoftc_t *, int);
static void isp_sbus_wr_reg(ispsoftc_t *, int, uint32_t);
static void isp_sbus_run_isr(ispsoftc_t *);
static int isp_sbus_mbxdma(ispsoftc_t *);
static void isp_sbus_mbxdmafree(ispsoftc_t *);
static int isp_sbus_dmasetup(ispsoftc_t *, XS_T *, void *);
static void isp_sbus_dumpregs(ispsoftc_t *, const char *);

static struct ispmdvec mdvec = {
	isp_sbus_run_isr,
	isp_sbus_rd_reg,
	isp_sbus_wr_reg,
	isp_sbus_mbxdma,
	isp_sbus_dmasetup,
	isp_common_dmateardown,
	NULL,
	isp_sbus_dumpregs,
	NULL,
	BIU_BURST_ENABLE|BIU_PCI_CONF1_FIFO_64
};

static int isp_sbus_probe (device_t);
static int isp_sbus_attach (device_t);
static int isp_sbus_detach (device_t);


#define	ISP_SBD(isp)	((struct isp_sbussoftc *)isp)->sbus_dev
struct isp_sbussoftc {
	ispsoftc_t			sbus_isp;
	device_t			sbus_dev;
	struct resource *		regs;
	void *				irq;
	int				iqd;
	int				rgd;
	void *				ih;
	int16_t				sbus_poff[_NREG_BLKS];
	sdparam				sbus_param;
	struct isp_spi			sbus_spi;
	struct ispmdvec			sbus_mdvec;
};


static device_method_t isp_sbus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		isp_sbus_probe),
	DEVMETHOD(device_attach,	isp_sbus_attach),
	DEVMETHOD(device_detach,	isp_sbus_detach),
	{ 0, 0 }
};

static driver_t isp_sbus_driver = {
	"isp", isp_sbus_methods, sizeof (struct isp_sbussoftc)
};
static devclass_t isp_devclass;
DRIVER_MODULE(isp, sbus, isp_sbus_driver, isp_devclass, 0, 0);
MODULE_DEPEND(isp, cam, 1, 1, 1);
MODULE_DEPEND(isp, firmware, 1, 1, 1);

static int
isp_sbus_probe(device_t dev)
{
	int found = 0;
	const char *name = ofw_bus_get_name(dev);
	if (strcmp(name, "SUNW,isp") == 0 ||
	    strcmp(name, "QLGC,isp") == 0 ||
	    strcmp(name, "ptisp") == 0 ||
	    strcmp(name, "PTI,ptisp") == 0) {
		found++;
	}
	if (!found)
		return (ENXIO);
	
	if (isp_announced == 0 && bootverbose) {
		printf("Qlogic ISP Driver, FreeBSD Version %d.%d, "
		    "Core Version %d.%d\n",
		    ISP_PLATFORM_VERSION_MAJOR, ISP_PLATFORM_VERSION_MINOR,
		    ISP_CORE_VERSION_MAJOR, ISP_CORE_VERSION_MINOR);
		isp_announced++;
	}
	return (0);
}

static int
isp_sbus_attach(device_t dev)
{
	struct isp_sbussoftc *sbs = device_get_softc(dev);
	ispsoftc_t *isp = &sbs->sbus_isp;
	int tval, isp_debug, role, ispburst, default_id;

	sbs->sbus_dev = dev;
	sbs->sbus_mdvec = mdvec;
	isp->isp_dev = dev;
	mtx_init(&isp->isp_lock, "isp", NULL, MTX_DEF);

	role = 0;
	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "role", &role) == 0 &&
	    ((role & ~(ISP_ROLE_INITIATOR|ISP_ROLE_TARGET)) == 0)) {
		device_printf(dev, "setting role to 0x%x\n", role);
	} else {
		role = ISP_DEFAULT_ROLES;
	}

	sbs->irq = sbs->regs = NULL;
	sbs->rgd = sbs->iqd = 0;

	sbs->regs = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sbs->rgd,
	    RF_ACTIVE);
	if (sbs->regs == NULL) {
		device_printf(dev, "unable to map registers\n");
		goto bad;
	}

	sbs->sbus_poff[BIU_BLOCK >> _BLK_REG_SHFT] = BIU_REGS_OFF;
	sbs->sbus_poff[MBOX_BLOCK >> _BLK_REG_SHFT] = SBUS_MBOX_REGS_OFF;
	sbs->sbus_poff[SXP_BLOCK >> _BLK_REG_SHFT] = SBUS_SXP_REGS_OFF;
	sbs->sbus_poff[RISC_BLOCK >> _BLK_REG_SHFT] = SBUS_RISC_REGS_OFF;
	sbs->sbus_poff[DMA_BLOCK >> _BLK_REG_SHFT] = DMA_REGS_OFF;
	isp->isp_regs = sbs->regs;
	isp->isp_mdvec = &sbs->sbus_mdvec;
	isp->isp_bustype = ISP_BT_SBUS;
	isp->isp_type = ISP_HA_SCSI_UNKNOWN;
	isp->isp_param = &sbs->sbus_param;
	isp->isp_osinfo.pc.ptr = &sbs->sbus_spi;
	isp->isp_revision = 0;	/* XXX */
	isp->isp_nchan = 1;
	if (IS_FC(isp))
		ISP_FC_PC(isp, 0)->def_role = role;

	/*
	 * Get the clock frequency and convert it from HZ to MHz,
	 * rounding up. This defaults to 25MHz if there isn't a
	 * device specific one in the OFW device tree.
	 */
	sbs->sbus_mdvec.dv_clock = (sbus_get_clockfreq(dev) + 500000)/1000000;

	/*
	 * Now figure out what the proper burst sizes, etc., to use.
	 * Unfortunately, there is no ddi_dma_burstsizes here which
	 * walks up the tree finding the limiting burst size node (if
	 * any). We just use what's here for isp.
	 */
	ispburst = sbus_get_burstsz(dev);
	if (ispburst == 0) {
		ispburst = SBUS_BURST_32 - 1;
	}
	sbs->sbus_mdvec.dv_conf1 =  0;
	if (ispburst & (1 << 5)) {
		sbs->sbus_mdvec.dv_conf1 = BIU_SBUS_CONF1_FIFO_32;
	} else if (ispburst & (1 << 4)) {
		sbs->sbus_mdvec.dv_conf1 = BIU_SBUS_CONF1_FIFO_16;
	} else if (ispburst & (1 << 3)) {
		sbs->sbus_mdvec.dv_conf1 =
		    BIU_SBUS_CONF1_BURST8 | BIU_SBUS_CONF1_FIFO_8;
	}
	if (sbs->sbus_mdvec.dv_conf1) {
		sbs->sbus_mdvec.dv_conf1 |= BIU_BURST_ENABLE;
	}

	/*
	 * We don't trust NVRAM on SBus cards
	 */
	isp->isp_confopts |= ISP_CFG_NONVRAM;

	/*
	 * Mark things if we're a PTI SBus adapter.
	 */
	if (strcmp("PTI,ptisp", ofw_bus_get_name(dev)) == 0 ||
	    strcmp("ptisp", ofw_bus_get_name(dev)) == 0) {
		SDPARAM(isp, 0)->isp_ptisp = 1;
	}

	isp->isp_osinfo.fw = firmware_get("isp_1000");
	if (isp->isp_osinfo.fw) {
		union {
			const void *cp;
			uint16_t *sp;
		} stupid;
		stupid.cp = isp->isp_osinfo.fw->data;
		isp->isp_mdvec->dv_ispfw = stupid.sp;
	}

	tval = 0;
        if (resource_int_value(device_get_name(dev), device_get_unit(dev),
            "fwload_disable", &tval) == 0 && tval != 0) {
		isp->isp_confopts |= ISP_CFG_NORELOAD;
	}

	default_id = -1;
	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
            "iid", &tval) == 0) {
		default_id = tval;
		isp->isp_confopts |= ISP_CFG_OWNLOOPID;
	}
	if (default_id == -1) {
		default_id = OF_getscsinitid(dev);
	}
	ISP_SPI_PC(isp, 0)->iid = default_id;

	isp_debug = 0;
        (void) resource_int_value(device_get_name(dev), device_get_unit(dev),
            "debug", &isp_debug);

	sbs->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sbs->iqd,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sbs->irq == NULL) {
		device_printf(dev, "could not allocate interrupt\n");
		goto bad;
	}

	if (bus_setup_intr(dev, sbs->irq, ISP_IFLAGS, NULL, isp_platform_intr,
	    isp, &sbs->ih)) {
		device_printf(dev, "could not setup interrupt\n");
		(void) bus_release_resource(dev, SYS_RES_IRQ,
		    sbs->iqd, sbs->irq);
		goto bad;
	}
	isp->isp_nirq = 1;

	/*
	 * Set up logging levels.
	 */
	if (isp_debug) {
		isp->isp_dblev = isp_debug;
	} else {
		isp->isp_dblev = ISP_LOGWARN|ISP_LOGERR;
	}
	if (bootverbose) {
		isp->isp_dblev |= ISP_LOGCONFIG|ISP_LOGINFO;
	}

	/*
	 * Make sure we're in reset state.
	 */
	ISP_LOCK(isp);
	if (isp_reinit(isp, 1) != 0) {
		ISP_UNLOCK(isp);
		goto bad;
	}
	ISP_UNLOCK(isp);
	if (isp_attach(isp)) {
		ISP_LOCK(isp);
		isp_shutdown(isp);
		ISP_UNLOCK(isp);
		goto bad;
	}
	return (0);

bad:
	if (isp->isp_nirq > 0) {
		(void) bus_teardown_intr(dev, sbs->irq, sbs->ih);
		(void) bus_release_resource(dev, SYS_RES_IRQ, sbs->iqd,
		    sbs->irq);
	}

	if (sbs->regs) {
		(void) bus_release_resource(dev, SYS_RES_MEMORY, sbs->rgd,
		    sbs->regs);
	}
	mtx_destroy(&isp->isp_lock);
	return (ENXIO);
}

static int
isp_sbus_detach(device_t dev)
{
	struct isp_sbussoftc *sbs = device_get_softc(dev);
	ispsoftc_t *isp = &sbs->sbus_isp;
	int status;

	status = isp_detach(isp);
	if (status)
		return (status);
	ISP_LOCK(isp);
	isp_shutdown(isp);
	ISP_UNLOCK(isp);
	if (isp->isp_nirq > 0) {
		(void) bus_teardown_intr(dev, sbs->irq, sbs->ih);
		(void) bus_release_resource(dev, SYS_RES_IRQ, sbs->iqd,
		    sbs->irq);
	}
	(void) bus_release_resource(dev, SYS_RES_MEMORY, sbs->rgd, sbs->regs);
	isp_sbus_mbxdmafree(isp);
	mtx_destroy(&isp->isp_lock);
	return (0);
}

#define	IspVirt2Off(a, x)	\
	(((struct isp_sbussoftc *)a)->sbus_poff[((x) & _BLK_REG_MASK) >> \
	_BLK_REG_SHFT] + ((x) & 0xff))

#define	BXR2(isp, off)		bus_read_2((isp)->isp_regs, (off))

static void
isp_sbus_run_isr(ispsoftc_t *isp)
{
	uint16_t isr, sema, info;

	isr = BXR2(isp, IspVirt2Off(isp, BIU_ISR));
	sema = BXR2(isp, IspVirt2Off(isp, BIU_SEMA));
	isp_prt(isp, ISP_LOGDEBUG3, "ISR 0x%x SEMA 0x%x", isr, sema);
	isr &= INT_PENDING_MASK(isp);
	sema &= BIU_SEMA_LOCK;
	if (isr == 0 && sema == 0)
		return;
	if (sema != 0) {
		info = BXR2(isp, IspVirt2Off(isp, OUTMAILBOX0));
		if (info & MBOX_COMMAND_COMPLETE)
			isp_intr_mbox(isp, info);
		else
			isp_intr_async(isp, info);
		if (isp->isp_state == ISP_RUNSTATE)
			isp_intr_respq(isp);
	} else
		isp_intr_respq(isp);
	ISP_WRITE(isp, HCCR, HCCR_CMD_CLEAR_RISC_INT);
	if (sema)
		ISP_WRITE(isp, BIU_SEMA, 0);
}

static uint32_t
isp_sbus_rd_reg(ispsoftc_t *isp, int regoff)
{
	uint16_t rval;
	struct isp_sbussoftc *sbs = (struct isp_sbussoftc *) isp;
	int offset = sbs->sbus_poff[(regoff & _BLK_REG_MASK) >> _BLK_REG_SHFT];
	offset += (regoff & 0xff);
	rval = BXR2(isp, offset);
	isp_prt(isp, ISP_LOGDEBUG3,
	    "isp_sbus_rd_reg(off %x) = %x", regoff, rval);
	return (rval);
}

static void
isp_sbus_wr_reg(ispsoftc_t *isp, int regoff, uint32_t val)
{
	struct isp_sbussoftc *sbs = (struct isp_sbussoftc *) isp;
	int offset = sbs->sbus_poff[(regoff & _BLK_REG_MASK) >> _BLK_REG_SHFT];
	offset += (regoff & 0xff);
	isp_prt(isp, ISP_LOGDEBUG3,
	    "isp_sbus_wr_reg(off %x) = %x", regoff, val);
	bus_write_2(isp->isp_regs, offset, val);
	MEMORYBARRIER(isp, SYNC_REG, offset, 2, -1);
}

struct imush {
	bus_addr_t maddr;
	int error;
};

static void imc(void *, bus_dma_segment_t *, int, int);

static void
imc(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct imush *imushp = (struct imush *) arg;

	if (!(imushp->error = error))
		imushp->maddr = segs[0].ds_addr;
}

static int
isp_sbus_mbxdma(ispsoftc_t *isp)
{
	caddr_t base;
	uint32_t len;
	int i, error;
	struct imush im;

	/* Already been here? If so, leave... */
	if (isp->isp_xflist != NULL)
		return (0);
	if (isp->isp_rquest != NULL && isp->isp_maxcmds == 0)
		return (0);
	ISP_UNLOCK(isp);
	if (isp->isp_rquest != NULL)
		goto gotmaxcmds;

	if (bus_dma_tag_create(bus_get_dma_tag(ISP_SBD(isp)), 1,
	    BUS_SPACE_MAXADDR_24BIT+1, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR_32BIT, NULL, NULL, BUS_SPACE_MAXSIZE_32BIT,
	    ISP_NSEG_MAX, BUS_SPACE_MAXADDR_24BIT, 0,
	    busdma_lock_mutex, &isp->isp_lock, &isp->isp_osinfo.dmat)) {
		isp_prt(isp, ISP_LOGERR, "could not create master dma tag");
		goto bad;
	}

	/*
	 * Allocate and map the request queue.
	 */
	len = ISP_QUEUE_SIZE(RQUEST_QUEUE_LEN(isp));
	if (bus_dma_tag_create(isp->isp_osinfo.dmat, QENTRY_LEN, BUS_SPACE_MAXADDR_24BIT+1,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    len, 1, len, 0, busdma_lock_mutex, &isp->isp_lock,
	    &isp->isp_osinfo.reqdmat)) {
		isp_prt(isp, ISP_LOGERR, "cannot create request DMA tag");
		goto bad;
	}
	if (bus_dmamem_alloc(isp->isp_osinfo.reqdmat, (void **)&base,
	    BUS_DMA_COHERENT, &isp->isp_osinfo.reqmap) != 0) {
		isp_prt(isp, ISP_LOGERR, "cannot allocate request DMA memory");
		bus_dma_tag_destroy(isp->isp_osinfo.reqdmat);
		goto bad;
	}
	isp->isp_rquest = base;
	im.error = 0;
	if (bus_dmamap_load(isp->isp_osinfo.reqdmat, isp->isp_osinfo.reqmap,
	    base, len, imc, &im, 0) || im.error) {
		isp_prt(isp, ISP_LOGERR, "error loading request DMA map %d", im.error);
		goto bad;
	}
	isp_prt(isp, ISP_LOGDEBUG0, "request area @ 0x%jx/0x%jx",
	    (uintmax_t)im.maddr, (uintmax_t)len);
	isp->isp_rquest_dma = im.maddr;

	/*
	 * Allocate and map the result queue.
	 */
	len = ISP_QUEUE_SIZE(RESULT_QUEUE_LEN(isp));
	if (bus_dma_tag_create(isp->isp_osinfo.dmat, QENTRY_LEN, BUS_SPACE_MAXADDR_24BIT+1,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    len, 1, len, 0, busdma_lock_mutex, &isp->isp_lock,
	    &isp->isp_osinfo.respdmat)) {
		isp_prt(isp, ISP_LOGERR, "cannot create response DMA tag");
		goto bad;
	}
	if (bus_dmamem_alloc(isp->isp_osinfo.respdmat, (void **)&base,
	    BUS_DMA_COHERENT, &isp->isp_osinfo.respmap) != 0) {
		isp_prt(isp, ISP_LOGERR, "cannot allocate response DMA memory");
		bus_dma_tag_destroy(isp->isp_osinfo.respdmat);
		goto bad;
	}
	isp->isp_result = base;
	im.error = 0;
	if (bus_dmamap_load(isp->isp_osinfo.respdmat, isp->isp_osinfo.respmap,
	    base, len, imc, &im, 0) || im.error) {
		isp_prt(isp, ISP_LOGERR, "error loading response DMA map %d", im.error);
		goto bad;
	}
	isp_prt(isp, ISP_LOGDEBUG0, "response area @ 0x%jx/0x%jx",
	    (uintmax_t)im.maddr, (uintmax_t)len);
	isp->isp_result_dma = im.maddr;

	if (isp->isp_maxcmds == 0) {
		ISP_LOCK(isp);
		return (0);
	}

gotmaxcmds:
	len = sizeof (struct isp_pcmd) * isp->isp_maxcmds;
	isp->isp_osinfo.pcmd_pool = (struct isp_pcmd *)
	    malloc(len, M_DEVBUF, M_WAITOK | M_ZERO);
	for (i = 0; i < isp->isp_maxcmds; i++) {
		struct isp_pcmd *pcmd = &isp->isp_osinfo.pcmd_pool[i];
		error = bus_dmamap_create(isp->isp_osinfo.dmat, 0, &pcmd->dmap);
		if (error) {
			isp_prt(isp, ISP_LOGERR,
			    "error %d creating per-cmd DMA maps", error);
			while (--i >= 0) {
				bus_dmamap_destroy(isp->isp_osinfo.dmat,
				    isp->isp_osinfo.pcmd_pool[i].dmap);
			}
			goto bad;
		}
		callout_init_mtx(&pcmd->wdog, &isp->isp_lock, 0);
		if (i == isp->isp_maxcmds-1) {
			pcmd->next = NULL;
		} else {
			pcmd->next = &isp->isp_osinfo.pcmd_pool[i+1];
		}
	}
	isp->isp_osinfo.pcmd_free = &isp->isp_osinfo.pcmd_pool[0];

	len = sizeof (isp_hdl_t *) * isp->isp_maxcmds;
	isp->isp_xflist = (isp_hdl_t *) malloc(len, M_DEVBUF, M_WAITOK | M_ZERO);
	for (len = 0; len < isp->isp_maxcmds - 1; len++)
		isp->isp_xflist[len].cmd = &isp->isp_xflist[len+1];
	isp->isp_xffree = isp->isp_xflist;

	ISP_LOCK(isp);
	return (0);

bad:
	isp_sbus_mbxdmafree(isp);
	ISP_LOCK(isp);
	return (1);
}

static void
isp_sbus_mbxdmafree(ispsoftc_t *isp)
{
	int i;

	if (isp->isp_xflist != NULL) {
		free(isp->isp_xflist, M_DEVBUF);
		isp->isp_xflist = NULL;
	}
	if (isp->isp_osinfo.pcmd_pool != NULL) {
		for (i = 0; i < isp->isp_maxcmds; i++) {
			bus_dmamap_destroy(isp->isp_osinfo.dmat,
			    isp->isp_osinfo.pcmd_pool[i].dmap);
		}
		free(isp->isp_osinfo.pcmd_pool, M_DEVBUF);
		isp->isp_osinfo.pcmd_pool = NULL;
	}
	if (isp->isp_result_dma != 0) {
		bus_dmamap_unload(isp->isp_osinfo.respdmat,
		    isp->isp_osinfo.respmap);
		isp->isp_result_dma = 0;
	}
	if (isp->isp_result != NULL) {
		bus_dmamem_free(isp->isp_osinfo.respdmat, isp->isp_result,
		    isp->isp_osinfo.respmap);
		bus_dma_tag_destroy(isp->isp_osinfo.respdmat);
		isp->isp_result = NULL;
	}
	if (isp->isp_rquest_dma != 0) {
		bus_dmamap_unload(isp->isp_osinfo.reqdmat,
		    isp->isp_osinfo.reqmap);
		isp->isp_rquest_dma = 0;
	}
	if (isp->isp_rquest != NULL) {
		bus_dmamem_free(isp->isp_osinfo.reqdmat, isp->isp_rquest,
		    isp->isp_osinfo.reqmap);
		bus_dma_tag_destroy(isp->isp_osinfo.reqdmat);
		isp->isp_rquest = NULL;
	}
}

typedef struct {
	ispsoftc_t *isp;
	void *cmd_token;
	void *rq;	/* original request */
	int error;
} mush_t;

#define	MUSHERR_NOQENTRIES	-2

static void
dma2(void *arg, bus_dma_segment_t *dm_segs, int nseg, int error)
{
	mush_t *mp = (mush_t *) arg;
	ispsoftc_t *isp = mp->isp;
	struct ccb_scsiio *csio = mp->cmd_token;
	isp_ddir_t ddir;
	int sdir;

	if (error) {
		mp->error = error;
		return;
	}
	if (nseg == 0) {
		ddir = ISP_NOXFR;
	} else {
		if ((csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
			ddir = ISP_FROM_DEVICE;
		} else {
			ddir = ISP_TO_DEVICE;
		}
		if ((csio->ccb_h.func_code == XPT_CONT_TARGET_IO) ^
		    ((csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)) {
			sdir = BUS_DMASYNC_PREREAD;
		} else {
			sdir = BUS_DMASYNC_PREWRITE;
		}
		bus_dmamap_sync(isp->isp_osinfo.dmat, PISP_PCMD(csio)->dmap,
		    sdir);
	}

	if (isp_send_cmd(isp, mp->rq, dm_segs, nseg, XS_XFRLEN(csio),
	    ddir, NULL) != CMD_QUEUED) {
		mp->error = MUSHERR_NOQENTRIES;
	}
}

static int
isp_sbus_dmasetup(ispsoftc_t *isp, struct ccb_scsiio *csio, void *ff)
{
	mush_t mush, *mp;
	int error;

	mp = &mush;
	mp->isp = isp;
	mp->cmd_token = csio;
	mp->rq = ff;
	mp->error = 0;

	error = bus_dmamap_load_ccb(isp->isp_osinfo.dmat,
	    PISP_PCMD(csio)->dmap, (union ccb *)csio, dma2, mp, 0);
	if (error == EINPROGRESS) {
		bus_dmamap_unload(isp->isp_osinfo.dmat, PISP_PCMD(csio)->dmap);
		mp->error = EINVAL;
		isp_prt(isp, ISP_LOGERR,
		    "deferred dma allocation not supported");
	} else if (error && mp->error == 0) {
#ifdef	DIAGNOSTIC
		isp_prt(isp, ISP_LOGERR, "error %d in dma mapping code", error);
#endif
		mp->error = error;
	}
	if (mp->error) {
		int retval = CMD_COMPLETE;
		if (mp->error == MUSHERR_NOQENTRIES) {
			retval = CMD_EAGAIN;
		} else if (mp->error == EFBIG) {
			XS_SETERR(csio, CAM_REQ_TOO_BIG);
		} else if (mp->error == EINVAL) {
			XS_SETERR(csio, CAM_REQ_INVALID);
		} else {
			XS_SETERR(csio, CAM_UNREC_HBA_ERROR);
		}
		return (retval);
	}
	return (CMD_QUEUED);
}

static void
isp_sbus_dumpregs(ispsoftc_t *isp, const char *msg)
{
	if (msg)
		printf("%s: %s\n", device_get_nameunit(isp->isp_dev), msg);
	else
		printf("%s:\n", device_get_nameunit(isp->isp_dev));
	printf("    biu_conf1=%x", ISP_READ(isp, BIU_CONF1));
	printf(" biu_icr=%x biu_isr=%x biu_sema=%x ", ISP_READ(isp, BIU_ICR),
	    ISP_READ(isp, BIU_ISR), ISP_READ(isp, BIU_SEMA));
	printf("risc_hccr=%x\n", ISP_READ(isp, HCCR));


	ISP_WRITE(isp, HCCR, HCCR_CMD_PAUSE);
	printf("    cdma_conf=%x cdma_sts=%x cdma_fifostat=%x\n",
		ISP_READ(isp, CDMA_CONF), ISP_READ(isp, CDMA_STATUS),
		ISP_READ(isp, CDMA_FIFO_STS));
	printf("    ddma_conf=%x ddma_sts=%x ddma_fifostat=%x\n",
		ISP_READ(isp, DDMA_CONF), ISP_READ(isp, DDMA_STATUS),
		ISP_READ(isp, DDMA_FIFO_STS));
	printf("    sxp_int=%x sxp_gross=%x sxp(scsi_ctrl)=%x\n",
		ISP_READ(isp, SXP_INTERRUPT),
		ISP_READ(isp, SXP_GROSS_ERR),
		ISP_READ(isp, SXP_PINS_CTRL));
	ISP_WRITE(isp, HCCR, HCCR_CMD_RELEASE);
	printf("    mbox regs: %x %x %x %x %x\n",
	    ISP_READ(isp, OUTMAILBOX0), ISP_READ(isp, OUTMAILBOX1),
	    ISP_READ(isp, OUTMAILBOX2), ISP_READ(isp, OUTMAILBOX3),
	    ISP_READ(isp, OUTMAILBOX4));
}
