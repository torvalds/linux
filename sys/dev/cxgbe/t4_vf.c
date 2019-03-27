/*-
 * Copyright (c) 2016 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <dev/pci/pcivar.h>
#if defined(__i386__) || defined(__amd64__)
#include <vm/vm.h>
#include <vm/pmap.h>
#endif

#include "common/common.h"
#include "common/t4_regs.h"
#include "t4_ioctl.h"
#include "t4_mp_ring.h"

/*
 * Some notes:
 *
 * The Virtual Interfaces are connected to an internal switch on the chip
 * which allows VIs attached to the same port to talk to each other even when
 * the port link is down.  As a result, we might want to always report a
 * VF's link as being "up".
 *
 * XXX: Add a TUNABLE and possible per-device sysctl for this?
 */

struct intrs_and_queues {
	uint16_t intr_type;	/* MSI, or MSI-X */
	uint16_t nirq;		/* Total # of vectors */
	uint16_t ntxq;		/* # of NIC txq's for each port */
	uint16_t nrxq;		/* # of NIC rxq's for each port */
};

struct {
	uint16_t device;
	char *desc;
} t4vf_pciids[] = {
	{0x4800, "Chelsio T440-dbg VF"},
	{0x4801, "Chelsio T420-CR VF"},
	{0x4802, "Chelsio T422-CR VF"},
	{0x4803, "Chelsio T440-CR VF"},
	{0x4804, "Chelsio T420-BCH VF"},
	{0x4805, "Chelsio T440-BCH VF"},
	{0x4806, "Chelsio T440-CH VF"},
	{0x4807, "Chelsio T420-SO VF"},
	{0x4808, "Chelsio T420-CX VF"},
	{0x4809, "Chelsio T420-BT VF"},
	{0x480a, "Chelsio T404-BT VF"},
	{0x480e, "Chelsio T440-LP-CR VF"},
}, t5vf_pciids[] = {
	{0x5800, "Chelsio T580-dbg VF"},
	{0x5801,  "Chelsio T520-CR VF"},	/* 2 x 10G */
	{0x5802,  "Chelsio T522-CR VF"},	/* 2 x 10G, 2 X 1G */
	{0x5803,  "Chelsio T540-CR VF"},	/* 4 x 10G */
	{0x5807,  "Chelsio T520-SO VF"},	/* 2 x 10G, nomem */
	{0x5809,  "Chelsio T520-BT VF"},	/* 2 x 10GBaseT */
	{0x580a,  "Chelsio T504-BT VF"},	/* 4 x 1G */
	{0x580d,  "Chelsio T580-CR VF"},	/* 2 x 40G */
	{0x580e,  "Chelsio T540-LP-CR VF"},	/* 4 x 10G */
	{0x5810,  "Chelsio T580-LP-CR VF"},	/* 2 x 40G */
	{0x5811,  "Chelsio T520-LL-CR VF"},	/* 2 x 10G */
	{0x5812,  "Chelsio T560-CR VF"},	/* 1 x 40G, 2 x 10G */
	{0x5814,  "Chelsio T580-LP-SO-CR VF"},	/* 2 x 40G, nomem */
	{0x5815,  "Chelsio T502-BT VF"},	/* 2 x 1G */
#ifdef notyet
	{0x5804,  "Chelsio T520-BCH VF"},
	{0x5805,  "Chelsio T540-BCH VF"},
	{0x5806,  "Chelsio T540-CH VF"},
	{0x5808,  "Chelsio T520-CX VF"},
	{0x580b,  "Chelsio B520-SR VF"},
	{0x580c,  "Chelsio B504-BT VF"},
	{0x580f,  "Chelsio Amsterdam VF"},
	{0x5813,  "Chelsio T580-CHR VF"},
#endif
}, t6vf_pciids[] = {
	{0x6800, "Chelsio T6-DBG-25 VF"},	/* 2 x 10/25G, debug */
	{0x6801, "Chelsio T6225-CR VF"},	/* 2 x 10/25G */
	{0x6802, "Chelsio T6225-SO-CR VF"},	/* 2 x 10/25G, nomem */
	{0x6803, "Chelsio T6425-CR VF"},	/* 4 x 10/25G */
	{0x6804, "Chelsio T6425-SO-CR VF"},	/* 4 x 10/25G, nomem */
	{0x6805, "Chelsio T6225-OCP-SO VF"},	/* 2 x 10/25G, nomem */
	{0x6806, "Chelsio T62100-OCP-SO VF"},	/* 2 x 40/50/100G, nomem */
	{0x6807, "Chelsio T62100-LP-CR VF"},	/* 2 x 40/50/100G */
	{0x6808, "Chelsio T62100-SO-CR VF"},	/* 2 x 40/50/100G, nomem */
	{0x6809, "Chelsio T6210-BT VF"},	/* 2 x 10GBASE-T */
	{0x680d, "Chelsio T62100-CR VF"},	/* 2 x 40/50/100G */
	{0x6810, "Chelsio T6-DBG-100 VF"},	/* 2 x 40/50/100G, debug */
	{0x6811, "Chelsio T6225-LL-CR VF"},	/* 2 x 10/25G */
	{0x6814, "Chelsio T61100-OCP-SO VF"},	/* 1 x 40/50/100G, nomem */
	{0x6815, "Chelsio T6201-BT VF"},	/* 2 x 1000BASE-T */

	/* Custom */
	{0x6880, "Chelsio T6225 80 VF"},
	{0x6881, "Chelsio T62100 81 VF"},
};

static d_ioctl_t t4vf_ioctl;

static struct cdevsw t4vf_cdevsw = {
       .d_version = D_VERSION,
       .d_ioctl = t4vf_ioctl,
       .d_name = "t4vf",
};

static int
t4vf_probe(device_t dev)
{
	uint16_t d;
	size_t i;

	d = pci_get_device(dev);
	for (i = 0; i < nitems(t4vf_pciids); i++) {
		if (d == t4vf_pciids[i].device) {
			device_set_desc(dev, t4vf_pciids[i].desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
t5vf_probe(device_t dev)
{
	uint16_t d;
	size_t i;

	d = pci_get_device(dev);
	for (i = 0; i < nitems(t5vf_pciids); i++) {
		if (d == t5vf_pciids[i].device) {
			device_set_desc(dev, t5vf_pciids[i].desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
t6vf_probe(device_t dev)
{
	uint16_t d;
	size_t i;

	d = pci_get_device(dev);
	for (i = 0; i < nitems(t6vf_pciids); i++) {
		if (d == t6vf_pciids[i].device) {
			device_set_desc(dev, t6vf_pciids[i].desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

#define FW_PARAM_DEV(param) \
	(V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_DEV) | \
	 V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DEV_##param))
#define FW_PARAM_PFVF(param) \
	(V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_PFVF) | \
	 V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_PFVF_##param))

static int
get_params__pre_init(struct adapter *sc)
{
	int rc;
	uint32_t param[3], val[3];

	param[0] = FW_PARAM_DEV(FWREV);
	param[1] = FW_PARAM_DEV(TPREV);
	param[2] = FW_PARAM_DEV(CCLK);
	rc = -t4vf_query_params(sc, nitems(param), param, val);
	if (rc != 0) {
		device_printf(sc->dev,
		    "failed to query parameters (pre_init): %d.\n", rc);
		return (rc);
	}

	sc->params.fw_vers = val[0];
	sc->params.tp_vers = val[1];
	sc->params.vpd.cclk = val[2];

	snprintf(sc->fw_version, sizeof(sc->fw_version), "%u.%u.%u.%u",
	    G_FW_HDR_FW_VER_MAJOR(sc->params.fw_vers),
	    G_FW_HDR_FW_VER_MINOR(sc->params.fw_vers),
	    G_FW_HDR_FW_VER_MICRO(sc->params.fw_vers),
	    G_FW_HDR_FW_VER_BUILD(sc->params.fw_vers));

	snprintf(sc->tp_version, sizeof(sc->tp_version), "%u.%u.%u.%u",
	    G_FW_HDR_FW_VER_MAJOR(sc->params.tp_vers),
	    G_FW_HDR_FW_VER_MINOR(sc->params.tp_vers),
	    G_FW_HDR_FW_VER_MICRO(sc->params.tp_vers),
	    G_FW_HDR_FW_VER_BUILD(sc->params.tp_vers));

	return (0);
}

static int
get_params__post_init(struct adapter *sc)
{
	int rc;

	rc = -t4vf_get_sge_params(sc);
	if (rc != 0) {
		device_printf(sc->dev,
		    "unable to retrieve adapter SGE parameters: %d\n", rc);
		return (rc);
	}

	rc = -t4vf_get_rss_glb_config(sc);
	if (rc != 0) {
		device_printf(sc->dev,
		    "unable to retrieve adapter RSS parameters: %d\n", rc);
		return (rc);
	}
	if (sc->params.rss.mode != FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL) {
		device_printf(sc->dev,
		    "unable to operate with global RSS mode %d\n",
		    sc->params.rss.mode);
		return (EINVAL);
	}

	rc = t4_read_chip_settings(sc);
	if (rc != 0)
		return (rc);

	/*
	 * Grab our Virtual Interface resource allocation, extract the
	 * features that we're interested in and do a bit of sanity testing on
	 * what we discover.
	 */
	rc = -t4vf_get_vfres(sc);
	if (rc != 0) {
		device_printf(sc->dev,
		    "unable to get virtual interface resources: %d\n", rc);
		return (rc);
	}

	/*
	 * Check for various parameter sanity issues.
	 */
	if (sc->params.vfres.pmask == 0) {
		device_printf(sc->dev, "no port access configured/usable!\n");
		return (EINVAL);
	}
	if (sc->params.vfres.nvi == 0) {
		device_printf(sc->dev,
		    "no virtual interfaces configured/usable!\n");
		return (EINVAL);
	}
	sc->params.portvec = sc->params.vfres.pmask;

	return (0);
}

static int
set_params__post_init(struct adapter *sc)
{
	uint32_t param, val;

	/* ask for encapsulated CPLs */
	param = FW_PARAM_PFVF(CPLFW4MSG_ENCAP);
	val = 1;
	(void)t4vf_set_params(sc, 1, &param, &val);

	/* Enable 32b port caps if the firmware supports it. */
	param = FW_PARAM_PFVF(PORT_CAPS32);
	val = 1;
	if (t4vf_set_params(sc, 1, &param, &val) == 0)
		sc->params.port_caps32 = 1;

	return (0);
}

#undef FW_PARAM_PFVF
#undef FW_PARAM_DEV

static int
cfg_itype_and_nqueues(struct adapter *sc, struct intrs_and_queues *iaq)
{
	struct vf_resources *vfres;
	int nrxq, ntxq, nports;
	int itype, iq_avail, navail, rc;

	/*
	 * Figure out the layout of queues across our VIs and ensure
	 * we can allocate enough interrupts for our layout.
	 */
	vfres = &sc->params.vfres;
	nports = sc->params.nports;
	bzero(iaq, sizeof(*iaq));

	for (itype = INTR_MSIX; itype != 0; itype >>= 1) {
		if (itype == INTR_INTX)
			continue;

		if (itype == INTR_MSIX)
			navail = pci_msix_count(sc->dev);
		else
			navail = pci_msi_count(sc->dev);

		if (navail == 0)
			continue;

		iaq->intr_type = itype;

		/*
		 * XXX: The Linux driver reserves an Ingress Queue for
		 * forwarded interrupts when using MSI (but not MSI-X).
		 * It seems it just always asks for 2 interrupts and
		 * forwards all rxqs to the forwarded interrupt.
		 *
		 * We must reserve one IRQ for the for the firmware
		 * event queue.
		 *
		 * Every rxq requires an ingress queue with a free
		 * list and interrupts and an egress queue.  Every txq
		 * requires an ETH egress queue.
		 */
		iaq->nirq = T4VF_EXTRA_INTR;

		/*
		 * First, determine how many queues we can allocate.
		 * Start by finding the upper bound on rxqs from the
		 * limit on ingress queues.
		 */
		iq_avail = vfres->niqflint - iaq->nirq;
		if (iq_avail < nports) {
			device_printf(sc->dev,
			    "Not enough ingress queues (%d) for %d ports\n",
			    vfres->niqflint, nports);
			return (ENXIO);
		}

		/*
		 * Try to honor the cap on interrupts.  If there aren't
		 * enough interrupts for at least one interrupt per
		 * port, then don't bother, we will just forward all
		 * interrupts to one interrupt in that case.
		 */
		if (iaq->nirq + nports <= navail) {
			if (iq_avail > navail - iaq->nirq)
				iq_avail = navail - iaq->nirq;
		}

		nrxq = nports * t4_nrxq;
		if (nrxq > iq_avail) {
			/*
			 * Too many ingress queues.  Use what we can.
			 */
			nrxq = (iq_avail / nports) * nports;
		}
		KASSERT(nrxq <= iq_avail, ("too many ingress queues"));

		/*
		 * Next, determine the upper bound on txqs from the limit
		 * on ETH queues.
		 */
		if (vfres->nethctrl < nports) {
			device_printf(sc->dev,
			    "Not enough ETH queues (%d) for %d ports\n",
			    vfres->nethctrl, nports);
			return (ENXIO);
		}

		ntxq = nports * t4_ntxq;
		if (ntxq > vfres->nethctrl) {
			/*
			 * Too many ETH queues.  Use what we can.
			 */
			ntxq = (vfres->nethctrl / nports) * nports;
		}
		KASSERT(ntxq <= vfres->nethctrl, ("too many ETH queues"));

		/*
		 * Finally, ensure we have enough egress queues.
		 */
		if (vfres->neq < nports * 2) {
			device_printf(sc->dev,
			    "Not enough egress queues (%d) for %d ports\n",
			    vfres->neq, nports);
			return (ENXIO);
		}
		if (nrxq + ntxq > vfres->neq) {
			/* Just punt and use 1 for everything. */
			nrxq = ntxq = nports;
		}
		KASSERT(nrxq <= iq_avail, ("too many ingress queues"));
		KASSERT(ntxq <= vfres->nethctrl, ("too many ETH queues"));
		KASSERT(nrxq + ntxq <= vfres->neq, ("too many egress queues"));

		/*
		 * Do we have enough interrupts?  For MSI the interrupts
		 * have to be a power of 2 as well.
		 */
		iaq->nirq += nrxq;
		iaq->ntxq = ntxq;
		iaq->nrxq = nrxq;
		if (iaq->nirq <= navail &&
		    (itype != INTR_MSI || powerof2(iaq->nirq))) {
			navail = iaq->nirq;
			if (itype == INTR_MSIX)
				rc = pci_alloc_msix(sc->dev, &navail);
			else
				rc = pci_alloc_msi(sc->dev, &navail);
			if (rc != 0) {
				device_printf(sc->dev,
		    "failed to allocate vectors:%d, type=%d, req=%d, rcvd=%d\n",
				    itype, rc, iaq->nirq, navail);
				return (rc);
			}
			if (navail == iaq->nirq) {
				return (0);
			}
			pci_release_msi(sc->dev);
		}

		/* Fall back to a single interrupt. */
		iaq->nirq = 1;
		navail = iaq->nirq;
		if (itype == INTR_MSIX)
			rc = pci_alloc_msix(sc->dev, &navail);
		else
			rc = pci_alloc_msi(sc->dev, &navail);
		if (rc != 0)
			device_printf(sc->dev,
		    "failed to allocate vectors:%d, type=%d, req=%d, rcvd=%d\n",
			    itype, rc, iaq->nirq, navail);
		return (rc);
	}

	device_printf(sc->dev,
	    "failed to find a usable interrupt type.  "
	    "allowed=%d, msi-x=%d, msi=%d, intx=1", t4_intr_types,
	    pci_msix_count(sc->dev), pci_msi_count(sc->dev));

	return (ENXIO);
}

static int
t4vf_attach(device_t dev)
{
	struct adapter *sc;
	int rc = 0, i, j, rqidx, tqidx;
	struct make_dev_args mda;
	struct intrs_and_queues iaq;
	struct sge *s;

	sc = device_get_softc(dev);
	sc->dev = dev;
	pci_enable_busmaster(dev);
	pci_set_max_read_req(dev, 4096);
	sc->params.pci.mps = pci_get_max_payload(dev);

	sc->flags |= IS_VF;
	TUNABLE_INT_FETCH("hw.cxgbe.dflags", &sc->debug_flags);

	sc->sge_gts_reg = VF_SGE_REG(A_SGE_VF_GTS);
	sc->sge_kdoorbell_reg = VF_SGE_REG(A_SGE_VF_KDOORBELL);
	snprintf(sc->lockname, sizeof(sc->lockname), "%s",
	    device_get_nameunit(dev));
	mtx_init(&sc->sc_lock, sc->lockname, 0, MTX_DEF);
	t4_add_adapter(sc);

	mtx_init(&sc->sfl_lock, "starving freelists", 0, MTX_DEF);
	TAILQ_INIT(&sc->sfl);
	callout_init_mtx(&sc->sfl_callout, &sc->sfl_lock, 0);

	mtx_init(&sc->reg_lock, "indirect register access", 0, MTX_DEF);

	rc = t4_map_bars_0_and_4(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	rc = -t4vf_prep_adapter(sc);
	if (rc != 0)
		goto done;

	t4_init_devnames(sc);
	if (sc->names == NULL) {
		rc = ENOTSUP;
		goto done; /* error message displayed already */
	}

	/*
	 * Leave the 'pf' and 'mbox' values as zero.  This ensures
	 * that various firmware messages do not set the fields which
	 * is the correct thing to do for a VF.
	 */

	memset(sc->chan_map, 0xff, sizeof(sc->chan_map));

	make_dev_args_init(&mda);
	mda.mda_devsw = &t4vf_cdevsw;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_WHEEL;
	mda.mda_mode = 0600;
	mda.mda_si_drv1 = sc;
	rc = make_dev_s(&mda, &sc->cdev, "%s", device_get_nameunit(dev));
	if (rc != 0)
		device_printf(dev, "failed to create nexus char device: %d.\n",
		    rc);

#if defined(__i386__)
	if ((cpu_feature & CPUID_CX8) == 0) {
		device_printf(dev, "64 bit atomics not available.\n");
		rc = ENOTSUP;
		goto done;
	}
#endif

	/*
	 * Some environments do not properly handle PCIE FLRs -- e.g. in Linux
	 * 2.6.31 and later we can't call pci_reset_function() in order to
	 * issue an FLR because of a self- deadlock on the device semaphore.
	 * Meanwhile, the OS infrastructure doesn't issue FLRs in all the
	 * cases where they're needed -- for instance, some versions of KVM
	 * fail to reset "Assigned Devices" when the VM reboots.  Therefore we
	 * use the firmware based reset in order to reset any per function
	 * state.
	 */
	rc = -t4vf_fw_reset(sc);
	if (rc != 0) {
		device_printf(dev, "FW reset failed: %d\n", rc);
		goto done;
	}
	sc->flags |= FW_OK;

	/*
	 * Grab basic operational parameters.  These will predominantly have
	 * been set up by the Physical Function Driver or will be hard coded
	 * into the adapter.  We just have to live with them ...  Note that
	 * we _must_ get our VPD parameters before our SGE parameters because
	 * we need to know the adapter's core clock from the VPD in order to
	 * properly decode the SGE Timer Values.
	 */
	rc = get_params__pre_init(sc);
	if (rc != 0)
		goto done; /* error message displayed already */
	rc = get_params__post_init(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	rc = set_params__post_init(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	rc = t4_map_bar_2(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	rc = t4_create_dma_tag(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	/*
	 * The number of "ports" which we support is equal to the number of
	 * Virtual Interfaces with which we've been provisioned.
	 */
	sc->params.nports = imin(sc->params.vfres.nvi, MAX_NPORTS);

	/*
	 * We may have been provisioned with more VIs than the number of
	 * ports we're allowed to access (our Port Access Rights Mask).
	 * Just use a single VI for each port.
	 */
	sc->params.nports = imin(sc->params.nports,
	    bitcount32(sc->params.vfres.pmask));

#ifdef notyet
	/*
	 * XXX: The Linux VF driver will lower nports if it thinks there
	 * are too few resources in vfres (niqflint, nethctrl, neq).
	 */
#endif

	/*
	 * First pass over all the ports - allocate VIs and initialize some
	 * basic parameters like mac address, port type, etc.
	 */
	for_each_port(sc, i) {
		struct port_info *pi;

		pi = malloc(sizeof(*pi), M_CXGBE, M_ZERO | M_WAITOK);
		sc->port[i] = pi;

		/* These must be set before t4_port_init */
		pi->adapter = sc;
		pi->port_id = i;
		pi->nvi = 1;
		pi->vi = malloc(sizeof(struct vi_info) * pi->nvi, M_CXGBE,
		    M_ZERO | M_WAITOK);

		/*
		 * Allocate the "main" VI and initialize parameters
		 * like mac addr.
		 */
		rc = -t4_port_init(sc, sc->mbox, sc->pf, 0, i);
		if (rc != 0) {
			device_printf(dev, "unable to initialize port %d: %d\n",
			    i, rc);
			free(pi->vi, M_CXGBE);
			free(pi, M_CXGBE);
			sc->port[i] = NULL;
			goto done;
		}

		/* No t4_link_start. */

		snprintf(pi->lockname, sizeof(pi->lockname), "%sp%d",
		    device_get_nameunit(dev), i);
		mtx_init(&pi->pi_lock, pi->lockname, 0, MTX_DEF);
		sc->chan_map[pi->tx_chan] = i;

		/* All VIs on this port share this media. */
		ifmedia_init(&pi->media, IFM_IMASK, cxgbe_media_change,
		    cxgbe_media_status);

		pi->dev = device_add_child(dev, sc->names->vf_ifnet_name, -1);
		if (pi->dev == NULL) {
			device_printf(dev,
			    "failed to add device for port %d.\n", i);
			rc = ENXIO;
			goto done;
		}
		pi->vi[0].dev = pi->dev;
		device_set_softc(pi->dev, pi);
	}

	/*
	 * Interrupt type, # of interrupts, # of rx/tx queues, etc.
	 */
	rc = cfg_itype_and_nqueues(sc, &iaq);
	if (rc != 0)
		goto done; /* error message displayed already */

	sc->intr_type = iaq.intr_type;
	sc->intr_count = iaq.nirq;

	s = &sc->sge;
	s->nrxq = sc->params.nports * iaq.nrxq;
	s->ntxq = sc->params.nports * iaq.ntxq;
	s->neq = s->ntxq + s->nrxq;	/* the free list in an rxq is an eq */
	s->neq += sc->params.nports;	/* ctrl queues: 1 per port */
	s->niq = s->nrxq + 1;		/* 1 extra for firmware event queue */

	s->rxq = malloc(s->nrxq * sizeof(struct sge_rxq), M_CXGBE,
	    M_ZERO | M_WAITOK);
	s->txq = malloc(s->ntxq * sizeof(struct sge_txq), M_CXGBE,
	    M_ZERO | M_WAITOK);
	s->iqmap = malloc(s->niq * sizeof(struct sge_iq *), M_CXGBE,
	    M_ZERO | M_WAITOK);
	s->eqmap = malloc(s->neq * sizeof(struct sge_eq *), M_CXGBE,
	    M_ZERO | M_WAITOK);

	sc->irq = malloc(sc->intr_count * sizeof(struct irq), M_CXGBE,
	    M_ZERO | M_WAITOK);

	/*
	 * Second pass over the ports.  This time we know the number of rx and
	 * tx queues that each port should get.
	 */
	rqidx = tqidx = 0;
	for_each_port(sc, i) {
		struct port_info *pi = sc->port[i];
		struct vi_info *vi;

		if (pi == NULL)
			continue;

		for_each_vi(pi, j, vi) {
			vi->pi = pi;
			vi->qsize_rxq = t4_qsize_rxq;
			vi->qsize_txq = t4_qsize_txq;

			vi->first_rxq = rqidx;
			vi->first_txq = tqidx;
			vi->tmr_idx = t4_tmr_idx;
			vi->pktc_idx = t4_pktc_idx;
			vi->nrxq = j == 0 ? iaq.nrxq: 1;
			vi->ntxq = j == 0 ? iaq.ntxq: 1;

			rqidx += vi->nrxq;
			tqidx += vi->ntxq;

			vi->rsrv_noflowq = 0;
		}
	}

	rc = t4_setup_intr_handlers(sc);
	if (rc != 0) {
		device_printf(dev,
		    "failed to setup interrupt handlers: %d\n", rc);
		goto done;
	}

	rc = bus_generic_attach(dev);
	if (rc != 0) {
		device_printf(dev,
		    "failed to attach all child ports: %d\n", rc);
		goto done;
	}

	device_printf(dev,
	    "%d ports, %d %s interrupt%s, %d eq, %d iq\n",
	    sc->params.nports, sc->intr_count, sc->intr_type == INTR_MSIX ?
	    "MSI-X" : "MSI", sc->intr_count > 1 ? "s" : "", sc->sge.neq,
	    sc->sge.niq);

done:
	if (rc != 0)
		t4_detach_common(dev);
	else
		t4_sysctls(sc);

	return (rc);
}

static void
get_regs(struct adapter *sc, struct t4_regdump *regs, uint8_t *buf)
{

	/* 0x3f is used as the revision for VFs. */
	regs->version = chip_id(sc) | (0x3f << 10);
	t4_get_regs(sc, buf, regs->len);
}

static void
t4_clr_vi_stats(struct adapter *sc)
{
	int reg;

	for (reg = A_MPS_VF_STAT_TX_VF_BCAST_BYTES_L;
	     reg <= A_MPS_VF_STAT_RX_VF_ERR_FRAMES_H; reg += 4)
		t4_write_reg(sc, VF_MPS_REG(reg), 0);
}

static int
t4vf_ioctl(struct cdev *dev, unsigned long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	int rc;
	struct adapter *sc = dev->si_drv1;

	rc = priv_check(td, PRIV_DRIVER);
	if (rc != 0)
		return (rc);

	switch (cmd) {
	case CHELSIO_T4_GETREG: {
		struct t4_reg *edata = (struct t4_reg *)data;

		if ((edata->addr & 0x3) != 0 || edata->addr >= sc->mmio_len)
			return (EFAULT);

		if (edata->size == 4)
			edata->val = t4_read_reg(sc, edata->addr);
		else if (edata->size == 8)
			edata->val = t4_read_reg64(sc, edata->addr);
		else
			return (EINVAL);

		break;
	}
	case CHELSIO_T4_SETREG: {
		struct t4_reg *edata = (struct t4_reg *)data;

		if ((edata->addr & 0x3) != 0 || edata->addr >= sc->mmio_len)
			return (EFAULT);

		if (edata->size == 4) {
			if (edata->val & 0xffffffff00000000)
				return (EINVAL);
			t4_write_reg(sc, edata->addr, (uint32_t) edata->val);
		} else if (edata->size == 8)
			t4_write_reg64(sc, edata->addr, edata->val);
		else
			return (EINVAL);
		break;
	}
	case CHELSIO_T4_REGDUMP: {
		struct t4_regdump *regs = (struct t4_regdump *)data;
		int reglen = t4_get_regs_len(sc);
		uint8_t *buf;

		if (regs->len < reglen) {
			regs->len = reglen; /* hint to the caller */
			return (ENOBUFS);
		}

		regs->len = reglen;
		buf = malloc(reglen, M_CXGBE, M_WAITOK | M_ZERO);
		get_regs(sc, regs, buf);
		rc = copyout(buf, regs->data, reglen);
		free(buf, M_CXGBE);
		break;
	}
	case CHELSIO_T4_CLEAR_STATS: {
		int i, v;
		u_int port_id = *(uint32_t *)data;
		struct port_info *pi;
		struct vi_info *vi;

		if (port_id >= sc->params.nports)
			return (EINVAL);
		pi = sc->port[port_id];

		/* MAC stats */
		pi->tx_parse_error = 0;
		t4_clr_vi_stats(sc);

		/*
		 * Since this command accepts a port, clear stats for
		 * all VIs on this port.
		 */
		for_each_vi(pi, v, vi) {
			if (vi->flags & VI_INIT_DONE) {
				struct sge_rxq *rxq;
				struct sge_txq *txq;

				for_each_rxq(vi, i, rxq) {
#if defined(INET) || defined(INET6)
					rxq->lro.lro_queued = 0;
					rxq->lro.lro_flushed = 0;
#endif
					rxq->rxcsum = 0;
					rxq->vlan_extraction = 0;
				}

				for_each_txq(vi, i, txq) {
					txq->txcsum = 0;
					txq->tso_wrs = 0;
					txq->vlan_insertion = 0;
					txq->imm_wrs = 0;
					txq->sgl_wrs = 0;
					txq->txpkt_wrs = 0;
					txq->txpkts0_wrs = 0;
					txq->txpkts1_wrs = 0;
					txq->txpkts0_pkts = 0;
					txq->txpkts1_pkts = 0;
					mp_ring_reset_stats(txq->r);
				}
			}
		}
		break;
	}
	case CHELSIO_T4_SCHED_CLASS:
		rc = t4_set_sched_class(sc, (struct t4_sched_params *)data);
		break;
	case CHELSIO_T4_SCHED_QUEUE:
		rc = t4_set_sched_queue(sc, (struct t4_sched_queue *)data);
		break;
	default:
		rc = ENOTTY;
	}

	return (rc);
}

static device_method_t t4vf_methods[] = {
	DEVMETHOD(device_probe,		t4vf_probe),
	DEVMETHOD(device_attach,	t4vf_attach),
	DEVMETHOD(device_detach,	t4_detach_common),

	DEVMETHOD_END
};

static driver_t t4vf_driver = {
	"t4vf",
	t4vf_methods,
	sizeof(struct adapter)
};

static device_method_t t5vf_methods[] = {
	DEVMETHOD(device_probe,		t5vf_probe),
	DEVMETHOD(device_attach,	t4vf_attach),
	DEVMETHOD(device_detach,	t4_detach_common),

	DEVMETHOD_END
};

static driver_t t5vf_driver = {
	"t5vf",
	t5vf_methods,
	sizeof(struct adapter)
};

static device_method_t t6vf_methods[] = {
	DEVMETHOD(device_probe,		t6vf_probe),
	DEVMETHOD(device_attach,	t4vf_attach),
	DEVMETHOD(device_detach,	t4_detach_common),

	DEVMETHOD_END
};

static driver_t t6vf_driver = {
	"t6vf",
	t6vf_methods,
	sizeof(struct adapter)
};

static driver_t cxgbev_driver = {
	"cxgbev",
	cxgbe_methods,
	sizeof(struct port_info)
};

static driver_t cxlv_driver = {
	"cxlv",
	cxgbe_methods,
	sizeof(struct port_info)
};

static driver_t ccv_driver = {
	"ccv",
	cxgbe_methods,
	sizeof(struct port_info)
};

static devclass_t t4vf_devclass, t5vf_devclass, t6vf_devclass;
static devclass_t cxgbev_devclass, cxlv_devclass, ccv_devclass;

DRIVER_MODULE(t4vf, pci, t4vf_driver, t4vf_devclass, 0, 0);
MODULE_VERSION(t4vf, 1);
MODULE_DEPEND(t4vf, t4nex, 1, 1, 1);

DRIVER_MODULE(t5vf, pci, t5vf_driver, t5vf_devclass, 0, 0);
MODULE_VERSION(t5vf, 1);
MODULE_DEPEND(t5vf, t5nex, 1, 1, 1);

DRIVER_MODULE(t6vf, pci, t6vf_driver, t6vf_devclass, 0, 0);
MODULE_VERSION(t6vf, 1);
MODULE_DEPEND(t6vf, t6nex, 1, 1, 1);

DRIVER_MODULE(cxgbev, t4vf, cxgbev_driver, cxgbev_devclass, 0, 0);
MODULE_VERSION(cxgbev, 1);

DRIVER_MODULE(cxlv, t5vf, cxlv_driver, cxlv_devclass, 0, 0);
MODULE_VERSION(cxlv, 1);

DRIVER_MODULE(ccv, t6vf, ccv_driver, ccv_devclass, 0, 0);
MODULE_VERSION(ccv, 1);
