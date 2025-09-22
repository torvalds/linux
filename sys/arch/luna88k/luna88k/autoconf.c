/*	$OpenBSD: autoconf.c,v 1.30 2024/10/22 21:50:02 jsg Exp $	*/
/*
 * Copyright (c) 1998 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
 * Copyright (c) 1994 Christian E. Hopps
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christian E. Hopps.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/kernel.h>

#include <uvm/uvm.h>

#include <machine/asm_macro.h>	/* enable/disable interrupts */
#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/vmparam.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/cons.h>

/*
 * The following several variables are related to
 * the configuration process, and are used in initializing
 * the machine.
 */

void	dumpconf(void);
void	get_autoboot_device(void);

int cold = 1;		/* 1 if still booting */
dev_t bootdev;		/* set by bootloader, retrieved in locore0.S */
struct device *bootdv;	/* set by device drivers (if found) */

/*
 * called at boot time, configure all devices on the system.
 */
void
cpu_configure()
{
	softintr_init();

	if (config_rootfound("mainbus", "mainbus") == 0)
		panic("no mainbus found");

	/*
	 * Switch to our final trap vectors, and unmap the PROM data area.
	 */
	set_vbr(kernel_vbr);
	pmap_unmap_firmware();

	cold = 0;

	/*
	 * Turn external interrupts on.
	 */
	set_psr(get_psr() & ~PSR_IND);
	spl0();
}

void
diskconf(void)
{
	printf("boot device: %s\n",
	    (bootdv) ? bootdv->dv_xname : "<unknown>");
	setroot(bootdv, 0, RB_USERREQ);
	dumpconf();
}

/*
 * Get 'auto-boot' information
 *
 * XXX Right now we can not handle network boot.
 */
struct autoboot_t {
	char	cont[16];
	int	targ;
	int	part;
} autoboot;

void
get_autoboot_device(void)
{
	char *value, c;
	int i, len, part;
	extern char *nvram_by_symbol(char *);		/* machdep.c */

	if ((bootdev & B_MAGICMASK) == B_DEVMAGIC) {
#ifdef DEBUG
		printf("bootdev = 0x%08x (t:%d, a:%d, c:%d, u:%d, p:%d)\n",
		    bootdev, B_TYPE(bootdev), B_ADAPTOR(bootdev),
		    B_CONTROLLER(bootdev), B_UNIT(bootdev),
		    B_PARTITION(bootdev));
#endif
		switch (B_TYPE(bootdev)) {
		case 0:
			snprintf(autoboot.cont, sizeof(autoboot.cont),
			    "spc%d", B_CONTROLLER(bootdev));
			break;
#if 0	/* not yet */
		case 1:
			snprintf(autoboot.cont, sizeof(autoboot.cont),
			    "le%d", B_CONTROLLER(bootdev));
			break;
#endif
		default:
			goto use_nvram_info;
		}
		autoboot.targ = B_UNIT(bootdev);
		autoboot.part = B_PARTITION(bootdev);
		return;
	}

use_nvram_info:
	/*
	 * Use old method if we can not get bootdev information
	 */
	printf("%s: no bootdev information, use NVRAM setting\n", __func__);

	/* Assume default controller is internal spc (spc0) */
	strlcpy(autoboot.cont, "spc0", sizeof(autoboot.cont));

	/* Get boot controller and SCSI target from NVRAM */
	value = nvram_by_symbol("boot_unit");
	if (value != NULL) {
		len = strlen(value);
		if (len == 1) {
			c = value[0];
		} else if (len == 2 && value[0] == '1') {
			/* External spc (spc1) */
			strlcpy(autoboot.cont, "spc1", sizeof(autoboot.cont));
			c = value[1];
		} else
			c = -1;

		if ((c >= '0') && (c <= '6'))
			autoboot.targ = 6 - (c - '0');
	}

	/* Get partition number from NVRAM */
	value = nvram_by_symbol("boot_partition");
	if (value != NULL) {
		len = strlen(value);
		part = 0;
		for (i = 0; i < len; i++)
			part = part * 10 + (value[i] - '0');
		autoboot.part = part;
	}
}

void
device_register(struct device *dev, void *aux)
{
	/*
	 * scsi: sd,cd  XXX: Can LUNA-88K boot from CD-ROM?
	 */
	if (strcmp("sd", dev->dv_cfdata->cf_driver->cd_name) == 0 ||
	    strcmp("cd", dev->dv_cfdata->cf_driver->cd_name) == 0) {
		struct scsi_attach_args *sa = aux;
		struct device *spcsc;

		spcsc = dev->dv_parent->dv_parent;

		if (strcmp(spcsc->dv_xname, autoboot.cont) == 0 &&
		    sa->sa_sc_link->target == autoboot.targ &&
		    sa->sa_sc_link->lun == 0) {
			bootdv = dev;
			return;
		}
	}
}

const struct nam2blk nam2blk[] = {
	{ "sd",		4 },
	{ "cd",		6 },
	{ "rd",		7 },
	{ "vnd",	8 },
	{ "wd",		9 },
	{ NULL,		-1 }
};
