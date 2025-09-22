/*	$OpenBSD: autoconf.c,v 1.64 2025/06/28 13:24:21 miod Exp $	*/

/*
 * Copyright (c) 1998-2003 Michael Shalayeff
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)autoconf.c	8.4 (Berkeley) 10/1/93
 */

#include "pci.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/disklabel.h>
#include <sys/conf.h>
#include <sys/reboot.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <uvm/uvm_extern.h>

#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <dev/cons.h>

#include <hppa/dev/cpudevs.h>

#if NPCI > 0
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#endif

/* device we booted from */
struct device *bootdv;
void	dumpconf(void);

void (*cold_hook)(int); /* see below */

/*
 * LED blinking thing
 */
#ifdef USELEDS
#include <sys/kernel.h>

struct timeout heartbeat_tmo;
void heartbeat(void *);
#endif

#include "cd.h"
#include "sd.h"
#include "st.h"
#include "mpath.h"

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#if NMPATH > 0
#include <scsi/mpathvar.h>
#endif

#ifdef USELEDS
/*
 * turn the heartbeat alive.
 * right thing would be to pass counter to each subsequent timeout
 * as an argument to heartbeat() incrementing every turn,
 * i.e. avoiding the static hbcnt, but doing timeout_set() on each
 * timeout_add() sounds ugly, guts of struct timeout looks ugly
 * to ponder in even more.
 */
void
heartbeat(void *v)
{
	static u_int hbcnt = 0, ocp_total, ocp_idle;
	int toggle, cp_mask, cp_total, cp_idle;
	struct schedstate_percpu *spc = &(curcpu()->ci_schedstate);

	timeout_add(&heartbeat_tmo, hz / 16);

	cp_idle = spc->spc_cp_time[CP_IDLE];
	cp_total = spc->spc_cp_time[CP_USER] + spc->spc_cp_time[CP_NICE] +
	    spc->spc_cp_time[CP_SYS] + spc->spc_cp_time[CP_INTR] +
	    spc->spc_cp_time[CP_IDLE];
	if (cp_total == ocp_total)
		cp_total = ocp_total + 1;
	if (cp_idle == ocp_idle)
		cp_idle = ocp_idle + 1;
	cp_mask = 0xf0 >> (cp_idle - ocp_idle) * 4 / (cp_total - ocp_total);
	cp_mask &= 0xf0;
	ocp_total = cp_total;
	ocp_idle = cp_idle;
	/*
	 * do this:
	 *
	 *   |~| |~|
	 *  _| |_| |_,_,_,_
	 *   0 1 2 3 4 6 7
	 */
	toggle = 0;
	if (hbcnt++ < 8 && hbcnt & 1)
		toggle = PALED_HEARTBEAT;
	hbcnt &= 15;
	ledctl(cp_mask,
	    (~cp_mask & 0xf0) | PALED_NETRCV | PALED_NETSND | PALED_DISK,
	    toggle);
}
#endif

/*
 * This is called by configure to set dumplo and dumpsize.
 * Dumps always skip the first CLBYTES of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf(void)
{
	extern int dumpsize;
	int nblks, dumpblks;	/* size of dump area */

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpblks = cpu_dumpsize();
	if (dumpblks < 0)
		return;
	dumpblks += ctod(physmem);

	/* If dump won't fit (incl. room for possible label), punt. */
	if (dumpblks > (nblks - ctod(1)))
		return;

	/* Put dump at end of partition */
	dumplo = nblks - dumpblks;

	/* dumpsize is in page units, and doesn't include headers. */
	dumpsize = physmem;
}

void	print_devpath(const char *label, struct pz_device *pz);

void
print_devpath(const char *label, struct pz_device *pz)
{
	int i;

	printf("%s: ", label);

	for (i = 0; i < 6; i++)
		if (pz->pz_bc[i] >= 0)
			printf("%d/", pz->pz_bc[i]);

	printf("%d.%x", pz->pz_mod, pz->pz_layers[0]);
	for (i = 1; i < 6 && pz->pz_layers[i]; i++)
		printf(".%x", pz->pz_layers[i]);

	printf(" class=%d flags=%b hpa=0x%x spa=0x%x io=0x%x\n", pz->pz_class,
	    pz->pz_flags, PZF_BITS, pz->pz_hpa, pz->pz_spa, pz->pz_iodc_io);
}

struct pdc_memmap pdc_memmap PDC_ALIGNMENT;
struct pdc_sysmap_find pdc_find PDC_ALIGNMENT;
struct pdc_sysmap_addrs pdc_addr PDC_ALIGNMENT;
struct pdc_iodc_read pdc_iodc_read PDC_ALIGNMENT;

void
pdc_scanbus(struct device *self, struct confargs *ca, int maxmod,
    hppa_hpa_t hpa, int cpu_scan)
{
	int start, end, incr, i;

	/* Scan forwards for CPUs, backwards for everything else. */
	if (cpu_scan) {
		start = 0;
		incr = 1;
		end = maxmod;
	} else {
		start = maxmod - 1;
		incr = -1;
		end = -1;
	}

	for (i = start; i != end; i += incr) {
		struct confargs nca;
		int error;

		bzero(&nca, sizeof(nca));
		nca.ca_iot = ca->ca_iot;
		nca.ca_dmatag = ca->ca_dmatag;
		nca.ca_dp.dp_bc[0] = ca->ca_dp.dp_bc[1];
		nca.ca_dp.dp_bc[1] = ca->ca_dp.dp_bc[2];
		nca.ca_dp.dp_bc[2] = ca->ca_dp.dp_bc[3];
		nca.ca_dp.dp_bc[3] = ca->ca_dp.dp_bc[4];
		nca.ca_dp.dp_bc[4] = ca->ca_dp.dp_bc[5];
		nca.ca_dp.dp_bc[5] = ca->ca_dp.dp_mod;
		nca.ca_dp.dp_mod = i;
		nca.ca_hpamask = ca->ca_hpamask;
		nca.ca_naddrs = 0;
		nca.ca_hpa = 0;

		if (hpa) {
			nca.ca_hpa = hpa + IOMOD_HPASIZE * i;
			nca.ca_dp.dp_mod = i;
		} else if ((error = pdc_call((iodcio_t)pdc, 0, PDC_MEMMAP,
		    PDC_MEMMAP_HPA, &pdc_memmap, &nca.ca_dp)) == 0)
			nca.ca_hpa = pdc_memmap.hpa;
		else if ((error = pdc_call((iodcio_t)pdc, 0, PDC_SYSMAP,
		    PDC_SYSMAP_HPA, &pdc_memmap, &nca.ca_dp)) == 0) {
			struct device_path path;
			int im, ia;

			nca.ca_hpa = pdc_memmap.hpa;

			for (im = 0; !(error = pdc_call((iodcio_t)pdc, 0,
			    PDC_SYSMAP, PDC_SYSMAP_FIND,
			    &pdc_find, &path, im)) &&
			    pdc_find.hpa != nca.ca_hpa; im++)
				;

			if (!error)
				nca.ca_hpasz = pdc_find.size << PGSHIFT;

			if (!error && pdc_find.naddrs) {
				nca.ca_naddrs = pdc_find.naddrs;
				if (nca.ca_naddrs > 16) {
					nca.ca_naddrs = 16;
					printf("WARNING: too many (%d) addrs\n",
					    pdc_find.naddrs);
				}

				if (autoconf_verbose)
					printf(">> ADDRS:");

				for (ia = 0; !(error = pdc_call((iodcio_t)pdc,
				    0, PDC_SYSMAP, PDC_SYSMAP_ADDR, &pdc_addr,
				    im, ia + 1)) && ia < nca.ca_naddrs; ia++) {
					nca.ca_addrs[ia].addr = pdc_addr.hpa;
					nca.ca_addrs[ia].size =
					    pdc_addr.size << PGSHIFT;

					if (autoconf_verbose)
						printf(" 0x%lx[0x%x]",
						    nca.ca_addrs[ia].addr,
						    nca.ca_addrs[ia].size);
				}
				if (autoconf_verbose)
					printf("\n");
			}
		}

		if (!nca.ca_hpa)
			continue;

		if (autoconf_verbose)
			printf(">> HPA 0x%lx[0x%x]\n",
			    nca.ca_hpa, nca.ca_hpasz);

		if ((error = pdc_call((iodcio_t)pdc, 0, PDC_IODC,
		    PDC_IODC_READ, &pdc_iodc_read, nca.ca_hpa, IODC_DATA,
		    &nca.ca_type, sizeof(nca.ca_type))) < 0) {
			if (autoconf_verbose)
				printf(">> iodc_data error %d\n", error);
			continue;
		}

		nca.ca_pdc_iodc_read = &pdc_iodc_read;
		nca.ca_name = hppa_mod_info(nca.ca_type.iodc_type,
					    nca.ca_type.iodc_sv_model);

		if (autoconf_verbose) {
			printf(">> probing: flags %b bc %d/%d/%d/%d/%d/%d ",
			    nca.ca_dp.dp_flags, PZF_BITS,
			    nca.ca_dp.dp_bc[0], nca.ca_dp.dp_bc[1],
			    nca.ca_dp.dp_bc[2], nca.ca_dp.dp_bc[3],
			    nca.ca_dp.dp_bc[4], nca.ca_dp.dp_bc[5]);
			printf("mod %x hpa %lx type %x sv %x\n",
			    nca.ca_dp.dp_mod, nca.ca_hpa,
			    nca.ca_type.iodc_type, nca.ca_type.iodc_sv_model);
		}

		if (cpu_scan && nca.ca_type.iodc_type == HPPA_TYPE_NPROC &&
		    nca.ca_type.iodc_sv_model == HPPA_NPROC_HPPA)
			ncpusfound++;

		if (cpu_scan &&
		    ((nca.ca_type.iodc_type != HPPA_TYPE_NPROC ||
	            nca.ca_type.iodc_sv_model != HPPA_NPROC_HPPA) &&
		    (nca.ca_type.iodc_type != HPPA_TYPE_MEMORY ||
		    nca.ca_type.iodc_sv_model != HPPA_MEMORY_PDEP)))
			continue;

		if (!cpu_scan &&
		    ((nca.ca_type.iodc_type == HPPA_TYPE_NPROC &&
		    nca.ca_type.iodc_sv_model == HPPA_NPROC_HPPA) ||
		    (nca.ca_type.iodc_type == HPPA_TYPE_MEMORY &&
		    nca.ca_type.iodc_sv_model == HPPA_MEMORY_PDEP)))
			continue;

		config_found_sm(self, &nca, mbprint, mbsubmatch);
	}
}

const struct hppa_mod_info hppa_knownmods[] = {
#include <hppa/dev/cpudevs_data.h>
};

const char *
hppa_mod_info(int type, int sv)
{
	const struct hppa_mod_info *mi;
	static char fakeid[32];

	for (mi = hppa_knownmods; mi->mi_type >= 0 &&
	    (mi->mi_type != type || mi->mi_sv != sv); mi++);

	if (mi->mi_type < 0) {
		snprintf(fakeid, sizeof fakeid, "type %x, sv %x", type, sv);
		return fakeid;
	} else
		return mi->mi_name;
}

void
device_register(struct device *dev, void *aux)
{
#if NPCI > 0
	extern struct cfdriver pci_cd;
#endif
#if NCD > 0 || NSD > 0 || NST > 0
	extern struct cfdriver scsibus_cd;
#endif
	struct confargs *ca = aux;
	static struct device *elder = NULL;

	if (bootdv != NULL)
		return;	/* We already have a winner */

#if NPCI > 0
	if (dev->dv_parent &&
	    dev->dv_parent->dv_cfdata->cf_driver == &pci_cd) {
		struct pci_attach_args *pa = aux;
		pcireg_t addr;
		int reg;

		for (reg = PCI_MAPREG_START; reg < PCI_MAPREG_END; reg += 4) {
			addr = pci_conf_read(pa->pa_pc, pa->pa_tag, reg);
			if (PCI_MAPREG_TYPE(addr) == PCI_MAPREG_TYPE_IO)
				addr = PCI_MAPREG_IO_ADDR(addr);
			else
				addr = PCI_MAPREG_MEM_ADDR(addr);

			if (addr == (pcireg_t)PAGE0->mem_boot.pz_hpa) {
				elder = dev;
				break;
			}
		}
	} else
#endif
	if (ca->ca_hpa == (hppa_hpa_t)PAGE0->mem_boot.pz_hpa) {
		/*
		 * If hpa matches, the only thing we know is that the
		 * booted device is either this one or one of its children.
		 * And the children will not necessarily have the correct
		 * hpa value.
		 * Save this elder for now.
		 */
		elder = dev;
	} else if (elder == NULL) {
		return;	/* not the device we booted from */
	}

	/*
	 * Unfortunately, we can not match on pz_class vs dv_class on
	 * older snakes netbooting using the rbootd protocol.
	 * In this case, we'll end up with pz_class == PCL_RANDOM...
	 * Instead, trust the device class from what the kernel attached
	 * now...
	 */
	switch (dev->dv_class) {
	case DV_IFNET:
		/*
		 * Netboot is the top elder
		 */
		if (elder == dev) {
			bootdv = dev;
		}
		return;
	case DV_DISK:
	case DV_DULL:
		if ((PAGE0->mem_boot.pz_class & PCL_CLASS_MASK) != PCL_RANDOM)
			return;
		break;
	case DV_TAPE:
		if ((PAGE0->mem_boot.pz_class & PCL_CLASS_MASK) != PCL_SEQU)
			return;
		break;
	default:
		/* No idea what we were booted from, but better ask the user */
		return;
	}

	/*
	 * If control goes here, we are booted from a block device and we
	 * matched a block device.
	 *
	 * We only grok SCSI boot currently.  Match on proper device
	 * hierarchy and unit/lun values.
	 */

#if NCD > 0 || NSD > 0 || NST > 0
	if (dev->dv_parent &&
	    dev->dv_parent->dv_cfdata->cf_driver == &scsibus_cd) {
		struct scsi_attach_args *sa = aux;
		struct scsi_link *sl = sa->sa_sc_link;

		/*
		 * sd/st/cd is attached to scsibus which is attached to
		 * the controller. Hence the grandparent here should be
		 * the elder.
		 */
		if (dev->dv_parent->dv_parent != elder) {
			return;
		}

		/*
		 * And now check for proper target and lun values
		 */
		if (sl->target == PAGE0->mem_boot.pz_layers[0] &&
		    sl->lun == PAGE0->mem_boot.pz_layers[1]) {
			bootdv = dev;
		}
	}
#endif
}

/*
 * cpu_configure:
 * called at boot time, configure all devices on system
 */
void
cpu_configure(void)
{
	splhigh();
	if (config_rootfound("mainbus", "mainbus") == NULL)
		panic("no mainbus found");

	cpu_intr_init();
	spl0();

	if (cold_hook)
		(*cold_hook)(HPPA_COLD_HOT);

#ifdef USELEDS
	timeout_set(&heartbeat_tmo, heartbeat, NULL);
	heartbeat(NULL);
#endif
	cold = 0;
}

void
diskconf(void)
{
	print_devpath("bootpath", &PAGE0->mem_boot);

#if NMPATH > 0
	if (bootdv != NULL)
		bootdv = mpath_bootdv(bootdv);
#endif

	setroot(bootdv, 0, RB_USERREQ);
	dumpconf();
}

const struct nam2blk nam2blk[] = {
	{ "vnd",	2 },
	{ "rd",		3 },
	{ "sd",		4 },
	{ "cd",		6 },
	{ "fd",		7 },
	{ "wd",		8 },
	{ NULL,		-1 }
};
