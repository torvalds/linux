/*	$OpenBSD: autoconf.c,v 1.47 2022/09/02 20:06:56 miod Exp $	*/
/*
 * Copyright (c) 1996, 1997 Per Fogelstrom
 * Copyright (c) 1995 Theo de Raadt
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Ralph Campbell.
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
 * from: Utah Hdr: autoconf.c 1.31 91/01/21
 *
 *	from: @(#)autoconf.c	8.1 (Berkeley) 6/10/93
 *      $Id: autoconf.c,v 1.47 2022/09/02 20:06:56 miod Exp $
 */

/*
 * Setup the system to run on the current machine.
 *
 * cpu_configure() is called at boot time.  Available
 * devices are determined (from possibilities mentioned in ioconf.c),
 * and the drivers are initialized.
 */

#include "sd.h"
#include "mpath.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/disklabel.h>
#include <sys/conf.h>
#include <sys/reboot.h>
#include <sys/device.h>
#include <dev/cons.h>
#include <uvm/uvm_extern.h>
#include <machine/autoconf.h>

#include <sys/disk.h>
#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <dev/ata/atavar.h>

#if NMPATH > 0
#include <scsi/mpathvar.h>
#endif

void	dumpconf(void);
static	struct devmap *findtype(char **);
void	parseofwbp(char *);
int	getpno(char **);

/*
 * The following several variables are related to
 * the configuration process, and are used in initializing
 * the machine.
 */
int	cold = 1;	/* if 1, still working on cold-start */
char	bootdev[16];	/* to hold boot dev name */
struct device *bootdv = NULL;
enum devclass bootdev_class = DV_DULL;
int	bootdev_type = 0;
int	bootdev_unit = 0;

/*
 *  Configure all devices found that we know about.
 *  This is done at boot time.
 */
void
cpu_configure(void)
{
	(void)splhigh();	/* To be really sure.. */

	softintr_init();

	if (config_rootfound("mainbus", "mainbus") == 0)
		panic("no mainbus found");
	(void)spl0();
	cold = 0;
}

struct devmap {
	char *att;
	char *dev;
	int   type;
};
#define	T_IFACE	0x10

#define	T_BUS	0x00
#define	T_SCSI	0x11
#define	T_IDE	0x12
#define	T_DISK	0x21

static struct devmap *
findtype(char **s)
{
	static struct devmap devmap[] = {
		{ "/ht",		NULL, T_BUS },
		{ "/ht@",		NULL, T_BUS },
		{ "/pci@",		NULL, T_BUS },
		{ "/pci",		NULL, T_BUS },
		{ "/AppleKiwi@",	NULL, T_BUS },
		{ "/AppleKiwi",		NULL, T_BUS },
		{ "/mac-io@",		NULL, T_BUS },
		{ "/mac-io",		NULL, T_BUS },
		{ "/@",			NULL, T_BUS },
		{ "/LSILogic,sas@",	"sd", T_SCSI },
		{ "/scsi@",		"sd", T_SCSI },
		{ "/ide",		"wd", T_IDE },
		{ "/ata",		"wd", T_IDE },
		{ "/k2-sata-root",	NULL, T_BUS },
		{ "/k2-sata",		"wd", T_IDE },
		{ "/disk@",		"sd", T_DISK },
		{ "/disk",		"wd", T_DISK },
		{ "/usb@",		"sd", T_SCSI },
		{ "/ADPT,2940U2B@",	"sd", T_SCSI },
		{ "/bcom5704@4",	"bge0", T_IFACE },
		{ "/bcom5704@4,1",	"bge1", T_IFACE },
		{ "/ethernet",		"gem0", T_IFACE },
		{ "/enet",		"mc0", T_IFACE },
		{ NULL, NULL }
	};
	struct devmap *dp = &devmap[0];

	while (dp->att) {
		if (strncmp(*s, dp->att, strlen(dp->att)) == 0) {
			*s += strlen(dp->att);
			break;
		}
		dp++;
	}
	if (dp->att == NULL)
		printf("string [%s] not found\n", *s);

	return(dp);
}

/*
 * Look at the string 'bp' and decode the boot device.
 * Boot names look like: '/pci/scsi@c/disk@0,0/bsd'
 *                       '/pci/mac-io/ide@20000/disk@0,0/bsd
 *                       '/pci/mac-io/ide/disk/bsd
 *			 '/ht@0,f2000000/pci@2/bcom5704@4/bsd'
 */
void
parseofwbp(char *bp)
{
	int	ptype;
	char   *dev, *cp;
	struct devmap *dp;

	cp = bp;
	do {
		while(*cp && *cp != '/')
			cp++;

		dp = findtype(&cp);
		if (!dp->att) {
			printf("Warning: bootpath unrecognized: %s\n", bp);
			return;
		}
	} while((dp->type & T_IFACE) == 0);

	if (dp->att && dp->type == T_IFACE) {
		bootdev_class = DV_IFNET;
		bootdev_type = dp->type;
		strlcpy(bootdev, dp->dev, sizeof bootdev);
		return;
	}
	dev = dp->dev;
	while(*cp && *cp != '/')
		cp++;
	ptype = dp->type;
	dp = findtype(&cp);
	if (dp->att && dp->type == T_DISK) {
		bootdev_class = DV_DISK;
		bootdev_type = ptype;
		bootdev_unit = getpno(&cp);
		return;
	}
	printf("Warning: boot device unrecognized: %s\n", bp);
}

int
getpno(char **cp)
{
	int val = 0, digit;
	char *cx = *cp;

	while (*cx) {
		if (*cx >= '0' && *cx <= '9')
			digit = *cx - '0';
		else if (*cx >= 'a' && *cx <= 'f')
			digit = *cx - 'a' + 0x0a;
		else
			break;
		val = val * 16 + digit;
		cx++;
	}
	*cp = cx;
	return (val);
}

void
device_register(struct device *dev, void *aux)
{
#if NSD > 0
	extern struct cfdriver scsibus_cd;
#endif
	const char *drvrname = dev->dv_cfdata->cf_driver->cd_name;
	const char *name = dev->dv_xname;

	if (bootdv != NULL || dev->dv_class != bootdev_class)
		return;

	switch (bootdev_type) {
#if NSD > 0
	case T_SCSI:
		if (dev->dv_parent->dv_cfdata->cf_driver == &scsibus_cd) {
			struct scsi_attach_args *sa = aux;

			if (sa->sa_sc_link->target == bootdev_unit)
				bootdv = dev;
		}
#endif
	case T_IDE:
		if (strcmp(drvrname, "wd") == 0) {
			struct ata_atapi_attach *aa = aux;

	    		if (aa->aa_drv_data->drive == bootdev_unit)
				bootdv = dev;
		}
		break;
	case T_IFACE:
		if (strcmp(name, bootdev) == 0)
			bootdv = dev;
		break;
	default:
		break;
	}
}

void
diskconf(void)
{
	printf("bootpath: %s\n", bootpath);

#if NMPATH > 0
	if (bootdv != NULL)
		bootdv = mpath_bootdv(bootdv);
#endif

	setroot(bootdv, 0, RB_USERREQ);
	dumpconf();
}

const struct nam2blk nam2blk[] = {
	{ "wd",		0 },
	{ "sd",		2 },
	{ "cd",		3 },
	{ "vnd",	14 },
	{ "rd",		17 },
	{ NULL,		-1 }
};
