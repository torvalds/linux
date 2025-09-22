/* $OpenBSD: dec_kn300.c,v 1.11 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: dec_kn300.c,v 1.34 2007/03/04 15:18:10 yamt Exp $ */

/*
 * Copyright (c) 1998 by Matthew Jacob
 * NASA AMES Research Center.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/termios.h>
#include <sys/conf.h>
#include <dev/cons.h>

#include <machine/rpb.h>
#include <machine/autoconf.h>
#include <machine/frame.h>
#include <machine/cpuconf.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <alpha/mcbus/mcbusreg.h>
#include <alpha/mcbus/mcbusvar.h>
#include <alpha/pci/mcpciareg.h>
#include <alpha/pci/mcpciavar.h>
#include <alpha/pci/pci_kn300.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include "pckbd.h"

#ifndef	CONSPEED
#define	CONSPEED	TTYDEF_SPEED
#endif
static int comcnrate = CONSPEED;

#ifdef DEBUG
int bootdev_debug;
#define DPRINTF(x)	do { if (bootdev_debug) printf x; } while (0)
#else
#define DPRINTF(x)	do { } while (0)
#endif

void dec_kn300_init (void);
void dec_kn300_cons_init (void);
static void dec_kn300_device_register (struct device *, void *);

#define	ALPHASERVER_4100	"AlphaServer 4100"

const struct alpha_variation_table dec_kn300_variations[] = {
	{ 0, ALPHASERVER_4100 },
	{ 0, NULL },
};

void
dec_kn300_init(void)
{
	u_int64_t variation;
	int cachesize;

	platform.family = ALPHASERVER_4100;

	if ((platform.model = alpha_dsr_sysname()) == NULL) {
		variation = hwrpb->rpb_variation & SV_ST_MASK;
		if ((platform.model = alpha_variation_name(variation,
		    dec_kn300_variations)) == NULL)
			platform.model = alpha_unknown_sysname();
	}

	platform.iobus = "mcbus";
	platform.cons_init = dec_kn300_cons_init;
	platform.device_register = dec_kn300_device_register;

	/*
	 * Determine B-cache size by looking at the primary (console)
	 * MCPCIA's WHOAMI register.
	 */
	mcpcia_init();

	if (mcbus_primary.mcbus_valid) {
		switch (mcbus_primary.mcbus_bcache) {
		default:
		case CPU_BCache_0MB:
			/* No B-cache or invalid; default to 1MB. */
			/* FALLTHROUGH */

		case CPU_BCache_1MB:
			cachesize = (1 * 1024 * 1024);
			break;

		case CPU_BCache_2MB:
			cachesize = (2 * 1024 * 1024);
			break;

		case CPU_BCache_4MB:
			cachesize = (4 * 1024 * 1024);
			break;
		}
	} else {
		/* Default to 1MB. */
		cachesize = (1 * 1024 * 1024);
	}
}

void
dec_kn300_cons_init(void)
{
	struct ctb *ctb;
	struct mcpcia_config *ccp;
	extern struct mcpcia_config mcpcia_console_configuration;

	ccp = &mcpcia_console_configuration;
	/* It's already initialized. */

	ctb = (struct ctb *)(((char *)hwrpb) + hwrpb->rpb_ctb_off);

	switch (ctb->ctb_term_type) {
	case CTB_PRINTERPORT:
		/* serial console ... */
		/*
		 * Delay to allow PROM putchars to complete.
		 * FIFO depth * character time,
		 * character time = (1000000 / (defaultrate / 10))
		 */
		DELAY(160000000 / comcnrate);
		if (comcnattach(&ccp->cc_iot, 0x3f8, comcnrate,
		    COM_FREQ,
		    (TTYDEF_CFLAG & ~(CSIZE | PARENB)) | CS8)) {
			panic("can't init serial console");

		}
		break;

	case CTB_GRAPHICS:
#if NPCKBD > 0
		/* display console ... */
		/* XXX */
		(void) pckbc_cnattach(&ccp->cc_iot, IO_KBD, KBCMDP, 0);

		if (CTB_TURBOSLOT_TYPE(ctb->ctb_turboslot) ==
		    CTB_TURBOSLOT_TYPE_ISA)
			isa_display_console(&ccp->cc_iot, &ccp->cc_memt);
		else
			pci_display_console(&ccp->cc_iot, &ccp->cc_memt,
			    &ccp->cc_pc, CTB_TURBOSLOT_BUS(ctb->ctb_turboslot),
			    CTB_TURBOSLOT_SLOT(ctb->ctb_turboslot), 0);
#else
		panic("not configured to use display && keyboard console");
#endif
		break;

	default:
		printf("ctb->ctb_term_type = 0x%lx\n",
		    (unsigned long)ctb->ctb_term_type);
		printf("ctb->ctb_turboslot = 0x%lx\n",
		    (unsigned long)ctb->ctb_turboslot);

		panic("consinit: unknown console type %lu",
		    (unsigned long)ctb->ctb_term_type);
	}
}

static void
dec_kn300_device_register(struct device *dev, void *aux)
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
		diskboot = (strncasecmp(b->protocol, "SCSI", 4) == 0);
		netboot = (strncasecmp(b->protocol, "BOOTP", 5) == 0) ||
		    (strncasecmp(b->protocol, "MOP", 3) == 0);

		DPRINTF(("proto:%s bus:%d slot:%d chan:%d", b->protocol,
		    b->bus, b->slot, b->channel));
		if (b->remote_address)
			DPRINTF((" remote_addr:%s", b->remote_address));
		DPRINTF((" un:%d bdt:%d", b->unit, b->boot_dev_type));
		if (b->ctrl_dev_type)
			DPRINTF((" cdt:%s\n", b->ctrl_dev_type));
		else
			DPRINTF(("\n"));
		DPRINTF(("diskboot = %d, netboot = %d\n", diskboot, netboot));
		initted = 1;
	}

	if (primarydev == NULL) {
		if (strcmp(cd->cd_name, "mcpcia"))
			return;
		else {
			struct mcbus_dev_attach_args *ma = aux;

			if (b->bus != ma->ma_mid - 4)
				return;
			primarydev = dev;
			DPRINTF(("\nprimarydev = %s\n", dev->dv_xname));
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
		else {
			struct pcibus_attach_args *pba = aux;

			if ((b->slot / 1000) != pba->pba_bus)
				return;
	
			pcidev = dev;
			DPRINTF(("\npcidev = %s\n", dev->dv_xname));
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
				DPRINTF(("\nbooted_device = %s\n", dev->dv_xname));
				found = 1;
			} else {
				ctrlrdev = dev;
				DPRINTF(("\nctrlrdev = %s\n", dev->dv_xname));
			}
			return;
		}
	}

	if (!diskboot)
		return;

	if (strcmp(cd->cd_name, "sd") ||
	    strcmp(cd->cd_name, "st") ||
	    strcmp(cd->cd_name, "cd")) {
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
		DPRINTF(("\nbooted_device = %s\n", dev->dv_xname));
		found = 1;
	}
}
