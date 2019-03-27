/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * All rights reserved.
 *
 * Copyright (c) 2016 Netflix, Inc.
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

/*
 * PnP enumerator using the PCI BIOS.
 */

#include <stand.h>
#include <machine/stdarg.h>
#include <bootstrap.h>
#include <isapnp.h>
#include <btxv86.h>
#include "libi386.h"

/*
 * Stupid PCI BIOS interface doesn't let you simply enumerate everything
 * that's there, instead you have to ask it if it has something.
 *
 * So we have to scan by class code, subclass code and sometimes programming
 * interface.
 */

struct pci_progif 
{
    int		pi_code;
    const char	*pi_name;
};

static struct pci_progif progif_null[] = {
    {0x0,	NULL},
    {-1,	NULL}
};

static struct pci_progif progif_display[] = {
    {0x0,	"VGA"},
    {0x1,	"8514"},
    {-1,	NULL}
};

static struct pci_progif progif_ide[] = {
    {0x00,	NULL},
    {0x01,	NULL},
    {0x02,	NULL},
    {0x03,	NULL},
    {0x04,	NULL},
    {0x05,	NULL},
    {0x06,	NULL},
    {0x07,	NULL},
    {0x08,	NULL},
    {0x09,	NULL},
    {0x0a,	NULL},
    {0x0b,	NULL},
    {0x0c,	NULL},
    {0x0d,	NULL},
    {0x0e,	NULL},
    {0x0f,	NULL},
    {0x80,	NULL},
    {0x81,	NULL},
    {0x82,	NULL},
    {0x83,	NULL},
    {0x84,	NULL},
    {0x85,	NULL},
    {0x86,	NULL},
    {0x87,	NULL},
    {0x88,	NULL},
    {0x89,	NULL},
    {0x8a,	NULL},
    {0x8b,	NULL},
    {0x8c,	NULL},
    {0x8d,	NULL},
    {0x8e,	NULL},
    {0x8f,	NULL},
    {-1,	NULL}
};

static struct pci_progif progif_serial[] = {
    {0x0,	"8250"},
    {0x1,	"16450"},
    {0x2,	"16550"},
    {-1,	NULL}
};

static struct pci_progif progif_parallel[] = {
    {0x0,	"Standard"},
    {0x1,	"Bidirectional"},
    {0x2,	"ECP"},
    {-1,	NULL}
};

static struct pci_progif progif_firewire[] = {
    {0x10,	"OHCI"},
    {-1,	NULL}
};

struct pci_subclass 
{
    int			ps_subclass;
    const char		*ps_name;
    struct pci_progif	*ps_progif;	/* if set, use for programming interface value(s) */
};

static struct pci_subclass subclass_old[] = {
    {0x0,	"Old non-VGA",		progif_null},
    {0x1,	"Old VGA",		progif_null},
    {-1,	NULL,			NULL}
};

static struct pci_subclass subclass_mass[] = {
    {0x0,	"SCSI",			progif_null},
    {0x1,	"IDE",			progif_ide},
    {0x2,	"Floppy disk",		progif_null},
    {0x3,	"IPI",			progif_null},
    {0x4,	"RAID",			progif_null},
    {0x80,	"mass storage",		progif_null},
    {-1,	NULL,			NULL}
};

static struct pci_subclass subclass_net[] = {
    {0x0,	"Ethernet",		progif_null},
    {0x1,	"Token ring",		progif_null},
    {0x2,	"FDDI",			progif_null},
    {0x3,	"ATM",			progif_null},
    {0x80,	"network",		progif_null},
    {-1,	NULL,			NULL}
};

static struct pci_subclass subclass_display[] = {
    {0x0,	NULL,			progif_display},
    {0x1,	"XGA",			progif_null},
    {0x80,	"other",		progif_null},
    {-1,	NULL,			NULL}
};

static struct pci_subclass subclass_comms[] = {
    {0x0,	"serial",		progif_serial},
    {0x1,	"parallel",		progif_parallel},
    {0x80,	"communications",	progif_null},
    {-1,	NULL,			NULL}
};

static struct pci_subclass subclass_serial[] = {
    {0x0,	"FireWire",		progif_firewire},
    {0x1,	"ACCESS.bus",		progif_null},
    {0x2,	"SSA",			progif_null},
    {0x3,	"USB",			progif_null},
    {0x4,	"Fibrechannel",		progif_null},
    {-1,	NULL,			NULL}
};

static struct pci_class
{
    int			pc_class;
    const char		*pc_name;
    struct pci_subclass	*pc_subclass;
} pci_classes[] = {
    {0x0,	"device",	subclass_old},
    {0x1,	"controller",	subclass_mass},
    {0x2,	"controller",	subclass_net},
    {0x3,	"display",	subclass_display},
    {0x7,	"controller",	subclass_comms},
    {0xc,	"controller",	subclass_serial},
    {-1,	NULL,		NULL}
};

static void	biospci_enumerate(void);
static void	biospci_addinfo(int devid, struct pci_class *pc, struct pci_subclass *psc, struct pci_progif *ppi);

struct pnphandler biospcihandler =
{
    "PCI BIOS",
    biospci_enumerate
};
static int biospci_version;

#define PCI_BIOS_PRESENT	0xb101
#define FIND_PCI_DEVICE		0xb102
#define FIND_PCI_CLASS_CODE	0xb103
#define GENERATE_SPECIAL_CYCLE	0xb106
#define READ_CONFIG_BYTE	0xb108
#define READ_CONFIG_WORD	0xb109
#define READ_CONFIG_DWORD	0xb10a
#define WRITE_CONFIG_BYTE	0xb10b
#define WRITE_CONFIG_WORD	0xb10c
#define WRITE_CONFIG_DWORD	0xb10d
#define GET_IRQ_ROUTING_OPTIONS	0xb10e
#define SET_PCI_IRQ		0xb10f

#define PCI_INT			0x1a

#define PCI_SIGNATURE		0x20494350	/* AKA "PCI " */

void
biospci_detect(void)
{
    uint16_t version, hwcap, maxbus;
    char buf[24];

    /* Find the PCI BIOS */
    v86.ctl = V86_FLAGS;
    v86.addr = PCI_INT;
    v86.eax = PCI_BIOS_PRESENT;
    v86.edi = 0x0;
    v86int();

    /* Check for OK response */
    if (V86_CY(v86.efl) || ((v86.eax & 0xff00) != 0) ||
	(v86.edx != PCI_SIGNATURE))
	return;

    version = v86.ebx & 0xffff;
    hwcap = v86.eax & 0xff;
    maxbus = v86.ecx & 0xff;
#if 0
    printf("PCI BIOS %d.%d%s%s maxbus %d\n", 
	   bcd2bin((version >> 8) & 0xf), bcd2bin(version & 0xf),
	   (hwcap & 1) ? " config1" : "", (hwcap & 2) ? " config2" : "",
	   maxbus);
#endif
    sprintf(buf, "%d", bcd2bin((version >> 8) & 0xf));
    setenv("pcibios.major", buf, 1);
    sprintf(buf, "%d", bcd2bin(version & 0xf));
    setenv("pcibios.minor", buf, 1);
    sprintf(buf, "%d", !!(hwcap & 1));
    setenv("pcibios.config1", buf, 1);
    sprintf(buf, "%d", !!(hwcap & 2));
    setenv("pcibios.config2", buf, 1);
    sprintf(buf, "%d", maxbus);
    setenv("pcibios.maxbus", buf, 1);
    biospci_version = bcd2bin((version >> 8) & 0xf) * 10 + bcd2bin(version & 0xf);
}

static void
biospci_enumerate(void)
{
    int			device_index, err;
    uint32_t		locator, devid;
    struct pci_class	*pc;
    struct pci_subclass *psc;
    struct pci_progif	*ppi;

    /* Iterate over known classes */
    for (pc = pci_classes; pc->pc_class >= 0; pc++) {
	/* Iterate over subclasses */
	for (psc = pc->pc_subclass; psc->ps_subclass >= 0; psc++) {
	    /* Iterate over programming interfaces */
	    for (ppi = psc->ps_progif; ppi->pi_code >= 0; ppi++) {

		/* Scan for matches */
		for (device_index = 0; ; device_index++) {
		    /* Look for a match */
		    err = biospci_find_devclass((pc->pc_class << 16)
			+ (psc->ps_subclass << 8) + ppi->pi_code,
			device_index, &locator);
		    if (err != 0)
			break;

		    /* Read the device identifier from the nominated device */
		    err = biospci_read_config(locator, 0, BIOSPCI_32BITS, &devid);
		    if (err != 0)
			break;
		    
		    /* We have the device ID, create a PnP object and save everything */
		    biospci_addinfo(devid, pc, psc, ppi);
		}
	    }
	}
    }
}

static void
biospci_addinfo(int devid, struct pci_class *pc, struct pci_subclass *psc, struct pci_progif *ppi) 
{
    struct pnpinfo	*pi;
    char		desc[80];
    
    
    /* build the description */
    desc[0] = 0;
    if (ppi->pi_name != NULL) {
	strcat(desc, ppi->pi_name);
	strcat(desc, " ");
    }
    if (psc->ps_name != NULL) {
	strcat(desc, psc->ps_name);
	strcat(desc, " ");
    }
    if (pc->pc_name != NULL)
	strcat(desc, pc->pc_name);

    pi = pnp_allocinfo();
    pi->pi_desc = strdup(desc);
    sprintf(desc,"0x%08x", devid);
    pnp_addident(pi, desc);
    pnp_addinfo(pi);
}

int
biospci_find_devclass(uint32_t class, int index, uint32_t *locator)
{
	v86.ctl = V86_FLAGS;
	v86.addr = PCI_INT;
	v86.eax = FIND_PCI_CLASS_CODE;
	v86.ecx = class;
	v86.esi = index;
	v86int();

	 /* error */
	if (V86_CY(v86.efl) || (v86.eax & 0xff00))
		return (-1);

	*locator = v86.ebx;
	return (0);
}

static int
biospci_find_device(uint32_t devid, int index, uint32_t *locator)
{
	v86.ctl = V86_FLAGS;
	v86.addr = PCI_INT;
	v86.eax = FIND_PCI_DEVICE;
	v86.edx = devid & 0xffff;		/* EDX - Vendor ID */
	v86.ecx = (devid >> 16) & 0xffff;	/* ECX - Device ID */
	v86.esi = index;
	v86int();

	 /* error */
	if (V86_CY(v86.efl) || (v86.eax & 0xff00))
		return (-1);

	*locator = v86.ebx;
	return (0);
}
/*
 * Configuration space access methods.
 * width = 0(byte), 1(word) or 2(dword).
 */
int
biospci_write_config(uint32_t locator, int offset, int width, uint32_t val)
{
	v86.ctl = V86_FLAGS;
	v86.addr = PCI_INT;
	v86.eax = WRITE_CONFIG_BYTE + width;
	v86.ebx = locator;
	v86.edi = offset;
	v86.ecx = val;
	v86int();

	 /* error */
	if (V86_CY(v86.efl) || (v86.eax & 0xff00))
		return (-1);

	return(0);
}

int
biospci_read_config(uint32_t locator, int offset, int width, uint32_t *val)
{
	v86.ctl = V86_FLAGS;
	v86.addr = PCI_INT;
	v86.eax = READ_CONFIG_BYTE + width;
	v86.ebx = locator;
	v86.edi = offset;
	v86int();

	 /* error */
	if (V86_CY(v86.efl) || (v86.eax & 0xff00))
		return (-1);

	*val = v86.ecx;
	return (0);
}

uint32_t
biospci_locator(int8_t bus, uint8_t device, uint8_t function)
{

	return ((bus << 8) | ((device & 0x1f) << 3) | (function & 0x7));
}
