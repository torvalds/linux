/* $OpenBSD: dec_6600.c,v 1.16 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: dec_6600.c,v 1.7 2000/06/20 03:48:54 matt Exp $ */

/*
 * Copyright (c) 2009 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 1995, 1996, 1997 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/termios.h>
#include <dev/cons.h>

#include <machine/rpb.h>
#include <machine/autoconf.h>
#include <machine/cpuconf.h>
#include <machine/bus.h>
#include <machine/logout.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <alpha/pci/tsreg.h>
#include <alpha/pci/tsvar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <dev/ata/atavar.h>

#include "pckbd.h"

#ifndef CONSPEED
#define CONSPEED TTYDEF_SPEED
#endif

#define	DR_VERBOSE(f) while (0)

static int comcnrate __attribute__((unused)) = CONSPEED;

void dec_6600_init(void);
static void dec_6600_cons_init(void);
static void dec_6600_device_register(struct device *, void *);
static void dec_6600_mcheck_handler(unsigned long, struct trapframe *,
	    unsigned long, unsigned long);
#ifndef SMALL_KERNEL
static void dec_6600_environmental_mcheck(unsigned long, struct trapframe *,
	    unsigned long, unsigned long);
static void dec_6600_mcheck(unsigned long, struct trapframe *, unsigned long,
	    unsigned long);
static void dec_6600_print_syndrome(int, unsigned long);
#endif

void
dec_6600_init(void)
{

	platform.family = "6600";

	if ((platform.model = alpha_dsr_sysname()) == NULL) {
		/* XXX Don't know the system variations, yet. */
		platform.model = alpha_unknown_sysname();
	}

	platform.iobus = "tsc";
	platform.cons_init = dec_6600_cons_init;
	platform.device_register = dec_6600_device_register;
	platform.mcheck_handler = dec_6600_mcheck_handler;
	STQP(TS_C_DIM0) = 0UL;
	STQP(TS_C_DIM1) = 0UL;
}

static void
dec_6600_cons_init(void)
{
	struct ctb *ctb;
	u_int64_t ctbslot;
	struct tsp_config *tsp;

	ctb = (struct ctb *)(((caddr_t)hwrpb) + hwrpb->rpb_ctb_off);
	ctbslot = ctb->ctb_turboslot;

	/* Console hose defaults to hose 0. */
	tsp_console_hose = 0;

	tsp = tsp_init(0, tsp_console_hose);

	switch (ctb->ctb_term_type) {
	case CTB_PRINTERPORT:
		/* serial console ... */
		/* XXX */
		{
			/*
			 * Delay to allow PROM putchars to complete.
			 * FIFO depth * character time,
			 * character time = (1000000 / (defaultrate / 10))
			 */
			DELAY(160000000 / comcnrate);

			if(comcnattach(&tsp->pc_iot, 0x3f8, comcnrate,
			    COM_FREQ,
			    (TTYDEF_CFLAG & ~(CSIZE | PARENB)) | CS8))
				panic("can't init serial console");

			break;
		}

	case CTB_GRAPHICS:
#if NPCKBD > 0
		/* display console ... */
		/* XXX */
		(void) pckbc_cnattach(&tsp->pc_iot, IO_KBD, KBCMDP, 0);

		if (CTB_TURBOSLOT_TYPE(ctbslot) ==
		    CTB_TURBOSLOT_TYPE_ISA)
			isa_display_console(&tsp->pc_iot, &tsp->pc_memt);
		else {
			/* The display PCI might be different */
			tsp_console_hose = CTB_TURBOSLOT_HOSE(ctbslot);
			tsp = tsp_init(0, tsp_console_hose);
			pci_display_console(&tsp->pc_iot, &tsp->pc_memt,
			    &tsp->pc_pc, CTB_TURBOSLOT_BUS(ctbslot),
			    CTB_TURBOSLOT_SLOT(ctbslot), 0);
		}
#else
		panic("not configured to use display && keyboard console");
#endif
		break;

	default:
		printf("ctb_term_type = 0x%lx ctb_turboslot = 0x%lx"
		    " hose = %ld\n",
		    (unsigned long)ctb->ctb_term_type,
		    (unsigned long)ctbslot,
		    (unsigned long)CTB_TURBOSLOT_HOSE(ctbslot));

		panic("consinit: unknown console type %lu",
		    (unsigned long)ctb->ctb_term_type);
	}
}

static void
dec_6600_device_register(struct device *dev, void *aux)
{
	static int found, initted, diskboot, netboot;
	static struct device *primarydev, *pcidev, *ctrlrdev;
	struct bootdev_data *b = bootdev_data;
	struct device *parent = dev->dv_parent;
	struct cfdata *cf = dev->dv_cfdata;
	struct cfdriver *cd = cf->cf_driver;

	if (found)
		return;

	if (!initted) {
		diskboot = (strncasecmp(b->protocol, "SCSI", 4) == 0) ||
		    (strncasecmp(b->protocol, "IDE", 3) == 0);
		netboot = (strncasecmp(b->protocol, "BOOTP", 5) == 0) ||
		    (strncasecmp(b->protocol, "MOP", 3) == 0);
		DR_VERBOSE(printf("diskboot = %d, netboot = %d\n", diskboot,
		    netboot));
		initted = 1;
	}

	if (primarydev == NULL) {
		if (strcmp(cd->cd_name, "tsp"))
			return;
		else {
			struct tsp_attach_args *tsp = aux;

			if (b->bus != tsp->tsp_slot)
				return;
			primarydev = dev;
			DR_VERBOSE(printf("\nprimarydev = %s\n",
			    dev->dv_xname));
			return;
		}
	}

	if (pcidev == NULL) {
		if (strcmp(cd->cd_name, "pci"))
			return;
		/*
		 * Try to find primarydev anywhere in the ancestry.  This is
		 * necessary if the PCI bus is hidden behind a bridge.
		 */
		while (parent) {
			if (parent == primarydev)
				break;
			parent = parent->dv_parent;
		}
		if (!parent)
			return;
		else {
			struct pcibus_attach_args *pba = aux;

			if ((b->slot / 1000) != pba->pba_bus)
				return;
	
			pcidev = dev;
			DR_VERBOSE(printf("\npcidev = %s\n", dev->dv_xname));
			return;
		}
	}

	if (ctrlrdev == NULL) {
		if (parent != pcidev)
			return;
		else {
			struct pci_attach_args *pa = aux;
			int slot;

			slot = pa->pa_bus * 1000 + pa->pa_function * 100 +
			    pa->pa_device;
			if (b->slot != slot)
				return;
	
			if (netboot) {
				booted_device = dev;
				DR_VERBOSE(printf("\nbooted_device = %s\n",
				    dev->dv_xname));
				found = 1;
			} else {
				ctrlrdev = dev;
				DR_VERBOSE(printf("\nctrlrdev = %s\n",
				    dev->dv_xname));
			}
			return;
		}
	}

	if (!diskboot)
		return;

	if (!strcmp(cd->cd_name, "sd") || !strcmp(cd->cd_name, "st") ||
	    !strcmp(cd->cd_name, "cd")) {
		struct scsi_attach_args *sa = aux;
		struct scsi_link *periph = sa->sa_sc_link;
		int unit;

		if (parent->dv_parent != ctrlrdev)
			return;

		unit = periph->target * 100 + periph->lun;
		if (b->unit != unit)
			return;

		/* we've found it! */
		booted_device = dev;
		DR_VERBOSE(printf("\nbooted_device = %s\n", dev->dv_xname));
		found = 1;
	}

	/*
	 * Support to boot from IDE drives.
	 */
	if (!strcmp(cd->cd_name, "wd")) {
		struct ata_atapi_attach *aa_link = aux;

		if ((strcmp("pciide", parent->dv_cfdata->cf_driver->cd_name) != 0))
			return;
		if (parent != ctrlrdev)
			return;

		DR_VERBOSE(printf("\nAtapi info: drive: %d, channel %d\n",
		    aa_link->aa_drv_data->drive, aa_link->aa_channel));
		DR_VERBOSE(printf("Bootdev info: unit: %d, channel: %d\n",
		    b->unit, b->channel));
		if (b->unit != aa_link->aa_drv_data->drive ||
		    b->channel != aa_link->aa_channel)
			return;

		/* we've found it! */
		booted_device = dev;
		DR_VERBOSE(printf("booted_device = %s\n", dev->dv_xname));
		found = 1;
	}
}

#ifndef SMALL_KERNEL
static void
dec_6600_environmental_mcheck(unsigned long mces, struct trapframe *framep,
    unsigned long vector, unsigned long logout)
{
	mc_hdr_ev6 *hdr = (mc_hdr_ev6 *)logout;
	mc_env_ev6 *env = (mc_env_ev6 *)(logout + hdr->la_system_offset);
	int silent = 0;
	int itemno;

	/*
	 * Note that we do not check for an expected machine check,
	 * since software is not supposed to trigger an environmental
	 * machine check, and there might be an environmental change
	 * just before our expected machine check occurs.
	 */

	/*
	 * Most environmental changes are handled at the RMC level,
	 * and we are either not notified (e.g. PCI door open) or
	 * drastic action is taken (e.g. the RMC will power down the
	 * system immediately if the CPU door is open).
	 *
	 * The only events we seem to be notified of are power supply
	 * failures.
	 */

	/* display CPU failures */
	for (itemno = 0; itemno < 4; itemno++) {
		if ((env->cpuir & EV6_ENV_CPUIR_CPU_ENABLE(itemno)) != 0 &&
		    (env->cpuir & EV6_ENV_CPUIR_CPU_FAIL(itemno)) != 0) {
			printf("CPU%d FAILURE\n", itemno);
			silent = 1;
		}
	}

	/* display PSU failures */
	if (env->smir & EV6_ENV_SMIR_PSU_FAILURE) {
		for (itemno = 0; itemno < 3; itemno++) {
			if ((env->psir & EV6_ENV_PSIR_PSU_FAIL(itemno)) != 0) {
				if ((env->psir &
				    EV6_ENV_PSIR_PSU_ENABLE(itemno)) != 0)
					printf("PSU%d FAILURE\n", itemno);
				else
					printf("PSU%d DISABLED\n", itemno);
			} else {
				if ((env->psir &
				    EV6_ENV_PSIR_PSU_ENABLE(itemno)) != 0)
					printf("PSU%d ENABLED\n", itemno);
			}
		}
		silent = 1;
	}

	/* if we could not print a summary, display everything */
	if (silent == 0) {
		printf("      Processor Environmental Machine Check, "
		    "Code 0x%x\n", hdr->mcheck_code);

		printf("Flags\t%016lx\n", (unsigned long)env->flags);
		printf("DIR\t%016lx\n", (unsigned long)env->c_dir);
		printf("SMIR\t%016lx\n", (unsigned long)env->smir);
		printf("CPUIR\t%016lx\n", (unsigned long)env->cpuir);
		printf("PSIR\t%016lx\n", (unsigned long)env->psir);
		printf("LM78_ISR\t%016lx\n", (unsigned long)env->lm78_isr);
		printf("Doors\t%016lx\n", (unsigned long)env->doors);
		printf("Temp Warning\t%016lx\n",
		    (unsigned long)env->temp_warning);
		printf("Fan Control\t%016lx\n",
		    (unsigned long)env->fan_control);
		printf("Fatal Power Down\t%016lx\n",
		    (unsigned long)env->fatal_power_down);
	}

	/*
	 * Apparently, these checks occur with MCES == 0, which
	 * is supposed to be an uncorrectable machine check.
	 *
	 * Until I know of a better way to tell recoverable and
	 * unrecoverable environmental checks apart, I'll use
	 * the fatal power down code to discriminate.
	 */
	if (mces == 0 && env->fatal_power_down == 0)
		return;
	else
		machine_check(mces, framep, vector, logout);
}

/*
 * Expected syndrome values per faulting bit
 */
static const uint8_t ev6_syndrome[64 + 8] = {
	0xce, 0xcb, 0xd3, 0xd5, 0xd6, 0xd9, 0xda, 0xdc,
	0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x31, 0x34,
	0x0e, 0x0b, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
	0xe3, 0xe5, 0xe6, 0xe9, 0xea, 0xec, 0xf1, 0xf4,
	0x4f, 0x4a, 0x52, 0x54, 0x57, 0x58, 0x5b, 0x5d,
	0xa2, 0xa4, 0xa7, 0xa8, 0xab, 0xad, 0xb0, 0xb5,
	0x8f, 0x8a, 0x92, 0x94, 0x97, 0x98, 0x9b, 0x9d,
	0x62, 0x64, 0x67, 0x68, 0x6b, 0x6d, 0x70, 0x75,
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
};

static void
dec_6600_print_syndrome(int sno, unsigned long syndrome)
{
	unsigned int bitno;

	syndrome &= 0xff;
	printf("Syndrome bits %d\t%02lx ", sno, syndrome);
	for (bitno = 0; bitno < nitems(ev6_syndrome); bitno++)
		if (syndrome == ev6_syndrome[bitno])
			break;

	if (bitno < 64)
		printf("(%d)\n", bitno);
	else if (bitno < nitems(ev6_syndrome))
		printf("(CB%d)\n", bitno - 64);
	else
		printf("(unknown)\n");
}

static void
dec_6600_mcheck(unsigned long mces, struct trapframe *framep,
    unsigned long vector, unsigned long logout)
{
	mc_hdr_ev6 *hdr = (mc_hdr_ev6 *)logout;
	struct mchkinfo *mcp;

	/*
	 * If we expected a machine check, don't decode it.
	 */
	mcp = &curcpu()->ci_mcinfo;
	if (mcp->mc_expected) {
		machine_check(mces, framep, vector, logout);
		return;
	}

	printf("      Processor Machine Check (%lx), Code 0x%x\n",
	    vector, hdr->mcheck_code);

	if (vector == ALPHA_SYS_MCHECK) {
#ifdef notyet
		mc_sys_ev6 *sys = (mc_sys_ev6 *)(logout + hdr->la_system_offset);
#endif
		/* XXX Decode and report P-Chip errors */
	} else /* ALPHA_PROC_MCHECK */ {
		mc_cpu_ev6 *cpu = (mc_cpu_ev6 *)(logout + hdr->la_cpu_offset);
		size_t cpu_size = hdr->la_system_offset - hdr->la_cpu_offset;

		printf("Dcache status\t0x%05lx\n",
		    (unsigned long)cpu->dc_stat & EV6_DC_STAT_MASK);
		dec_6600_print_syndrome(0, cpu->c_syndrome_0);
		dec_6600_print_syndrome(1, cpu->c_syndrome_1);
		/* C_STAT */
		printf("C_STAT\t");
		switch (cpu->c_stat & EV6_C_STAT_MASK) {
		case EV6_C_STAT_DBL_ISTREAM_BC_ECC_ERR:
			printf("Bcache instruction stream double ECC error\n");
			break;
		case EV6_C_STAT_DBL_ISTREAM_MEM_ECC_ERR:
			printf("Memory instruction stream double ECC error\n");
			break;
		case EV6_C_STAT_DBL_DSTREAM_BC_ECC_ERR:
			printf("Bcache data stream double ECC error\n");
			break;
		case EV6_C_STAT_DBL_DSTREAM_MEM_ECC_ERR:
			printf("Memory data stream double ECC error\n");
			break;
		case EV6_C_STAT_SNGL_ISTREAM_BC_ECC_ERR:
			printf("Bcache instruction stream single ECC error\n");
			break;
		case EV6_C_STAT_SNGL_ISTREAM_MEM_ECC_ERR:
			printf("Memory instruction stream single ECC error\n");
			break;
		case EV6_C_STAT_SNGL_BC_PROBE_HIT_ERR:
		case EV6_C_STAT_SNGL_BC_PROBE_HIT_ERR2:
			printf("Bcache probe hit error\n");
			break;
		case EV6_C_STAT_SNGL_DSTREAM_DC_ECC_ERR:
			printf("Dcache data stream single ECC error\n");
			break;
		case EV6_C_STAT_SNGL_DSTREAM_BC_ECC_ERR:
			printf("Bcache data stream single ECC error\n");
			break;
		case EV6_C_STAT_SNGL_DSTREAM_MEM_ECC_ERR:
			printf("Memory data stream single ECC error\n");
			break;
		case EV6_C_STAT_SNGL_DC_DUPLICATE_TAG_PERR:
			printf("Dcache duplicate tag error\n");
			break;
		case EV6_C_STAT_SNGL_BC_TAG_PERR:
			printf("Bcache tag error\n");
			break;
		case EV6_C_STAT_NO_ERROR:
			if (cpu->dc_stat & EV6_DC_STAT_STORE_DATA_ECC_ERROR) {
				printf("Bcache/Dcache victim read ECC error\n");
				break;
			}
			/* FALLTHROUGH */
		default:
			printf("%02lx\n", (unsigned long)cpu->c_stat);
			break;
		}
		/* C_ADDR */
		printf("Error address\t");
		if ((cpu->c_stat & EV6_C_STAT_MASK) ==
		    EV6_C_STAT_SNGL_DSTREAM_DC_ECC_ERR)
			printf("0xXXXXXXXXXXX%05lx\n",
			    (unsigned long)cpu->c_addr & 0xfffc0);
		else
			printf("0x%016lx\n",
			    (unsigned long)cpu->c_addr & 0xffffffffffffffc0);

		if (cpu_size > offsetof(mc_cpu_ev6, exc_addr)) {
			printf("Exception address\t0x%016lx%s\n",
			    (unsigned long)cpu->exc_addr & 0xfffffffffffffffc,
			    cpu->exc_addr & 1 ? " in PAL mode" : "");
			/* other fields are not really informative */
		}
	}

	machine_check(mces, framep, vector, logout);
}
#endif

static void
dec_6600_mcheck_handler(unsigned long mces, struct trapframe *framep,
    unsigned long vector, unsigned long param)
{
#ifdef SMALL_KERNEL
	/*
	 * Even though we can not afford the machine check
	 * analysis code, we need to ignore environmental
	 * changes.
	 */
	if (vector == ALPHA_ENV_MCHECK)
		return;

	machine_check(mces, framep, vector, param);
#else
	switch (vector) {
	case ALPHA_ENV_MCHECK:
		dec_6600_environmental_mcheck(mces, framep, vector, param);
		break;
	case ALPHA_PROC_MCHECK:
	case ALPHA_SYS_MCHECK:
		dec_6600_mcheck(mces, framep, vector, param);
		break;
	default:
		machine_check(mces, framep, vector, param);
		break;
	}
#endif
}
