/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Nahanni Systems, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/mman.h>

#include <machine/vmm.h>
#include <vmmapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <unistd.h>

#include "bhyvegc.h"
#include "bhyverun.h"
#include "console.h"
#include "inout.h"
#include "pci_emul.h"
#include "rfb.h"
#include "vga.h"

/*
 * bhyve Framebuffer device emulation.
 * BAR0 points to the current mode information.
 * BAR1 is the 32-bit framebuffer address.
 *
 *  -s <b>,fbuf,wait,vga=on|io|off,rfb=<ip>:port,w=width,h=height
 */

static int fbuf_debug = 1;
#define	DEBUG_INFO	1
#define	DEBUG_VERBOSE	4
#define	DPRINTF(level, params)  if (level <= fbuf_debug) printf params


#define	KB	(1024UL)
#define	MB	(1024 * 1024UL)

#define	DMEMSZ	128

#define	FB_SIZE		(16*MB)

#define COLS_MAX	1920
#define	ROWS_MAX	1200

#define COLS_DEFAULT	1024
#define ROWS_DEFAULT	768

#define COLS_MIN	640
#define ROWS_MIN	480

struct pci_fbuf_softc {
	struct pci_devinst *fsc_pi;
	struct {
		uint32_t fbsize;
		uint16_t width;
		uint16_t height;
		uint16_t depth;
		uint16_t refreshrate;
		uint8_t  reserved[116];
	} __packed memregs;

	/* rfb server */
	char      *rfb_host;
	char      *rfb_password;
	int       rfb_port;
	int       rfb_wait;
	int       vga_enabled;
	int	  vga_full;

	uint32_t  fbaddr;
	char      *fb_base;
	uint16_t  gc_width;
	uint16_t  gc_height;
	void      *vgasc;
	struct bhyvegc_image *gc_image;
};

static struct pci_fbuf_softc *fbuf_sc;

#define	PCI_FBUF_MSI_MSGS	 4

static void
pci_fbuf_usage(char *opt)
{

	fprintf(stderr, "Invalid fbuf emulation option \"%s\"\r\n", opt);
	fprintf(stderr, "fbuf: {wait,}{vga=on|io|off,}rfb=<ip>:port"
	    "{,w=width}{,h=height}\r\n");
}

static void
pci_fbuf_write(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
	       int baridx, uint64_t offset, int size, uint64_t value)
{
	struct pci_fbuf_softc *sc;
	uint8_t *p;

	assert(baridx == 0);

	sc = pi->pi_arg;

	DPRINTF(DEBUG_VERBOSE,
	    ("fbuf wr: offset 0x%lx, size: %d, value: 0x%lx\n",
	    offset, size, value));

	if (offset + size > DMEMSZ) {
		printf("fbuf: write too large, offset %ld size %d\n",
		       offset, size);
		return;
	}

	p = (uint8_t *)&sc->memregs + offset;

	switch (size) {
	case 1:
		*p = value;
		break;
	case 2:
		*(uint16_t *)p = value;
		break;
	case 4:
		*(uint32_t *)p = value;
		break;
	case 8:
		*(uint64_t *)p = value;
		break;
	default:
		printf("fbuf: write unknown size %d\n", size);
		break;
	}

	if (!sc->gc_image->vgamode && sc->memregs.width == 0 &&
	    sc->memregs.height == 0) {
		DPRINTF(DEBUG_INFO, ("switching to VGA mode\r\n"));
		sc->gc_image->vgamode = 1;
		sc->gc_width = 0;
		sc->gc_height = 0;
	} else if (sc->gc_image->vgamode && sc->memregs.width != 0 &&
	    sc->memregs.height != 0) {
		DPRINTF(DEBUG_INFO, ("switching to VESA mode\r\n"));
		sc->gc_image->vgamode = 0;
	}
}

uint64_t
pci_fbuf_read(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
	      int baridx, uint64_t offset, int size)
{
	struct pci_fbuf_softc *sc;
	uint8_t *p;
	uint64_t value;

	assert(baridx == 0);

	sc = pi->pi_arg;


	if (offset + size > DMEMSZ) {
		printf("fbuf: read too large, offset %ld size %d\n",
		       offset, size);
		return (0);
	}

	p = (uint8_t *)&sc->memregs + offset;
	value = 0;
	switch (size) {
	case 1:
		value = *p;
		break;
	case 2:
		value = *(uint16_t *)p;
		break;
	case 4:
		value = *(uint32_t *)p;
		break;
	case 8:
		value = *(uint64_t *)p;
		break;
	default:
		printf("fbuf: read unknown size %d\n", size);
		break;
	}

	DPRINTF(DEBUG_VERBOSE,
	    ("fbuf rd: offset 0x%lx, size: %d, value: 0x%lx\n",
	     offset, size, value));

	return (value);
}

static int
pci_fbuf_parse_opts(struct pci_fbuf_softc *sc, char *opts)
{
	char	*uopts, *xopts, *config;
	char	*tmpstr;
	int	ret;

	ret = 0;
	uopts = strdup(opts);
	for (xopts = strtok(uopts, ",");
	     xopts != NULL;
	     xopts = strtok(NULL, ",")) {
		if (strcmp(xopts, "wait") == 0) {
			sc->rfb_wait = 1;
			continue;
		}

		if ((config = strchr(xopts, '=')) == NULL) {
			pci_fbuf_usage(xopts);
			ret = -1;
			goto done;
		}

		*config++ = '\0';

		DPRINTF(DEBUG_VERBOSE, ("pci_fbuf option %s = %s\r\n",
		   xopts, config));

		if (!strcmp(xopts, "tcp") || !strcmp(xopts, "rfb")) {
			/*
			 * IPv4 -- host-ip:port
			 * IPv6 -- [host-ip%zone]:port
			 * XXX for now port is mandatory.
			 */
			tmpstr = strsep(&config, "]");
			if (config) {
				if (tmpstr[0] == '[')
					tmpstr++;
				sc->rfb_host = tmpstr;
				if (config[0] == ':')
					config++;
				else {
					pci_fbuf_usage(xopts);
					ret = -1;
					goto done;
				}
				sc->rfb_port = atoi(config);
			} else {
				config = tmpstr;
				tmpstr = strsep(&config, ":");
				if (!config)
					sc->rfb_port = atoi(tmpstr);
				else {
					sc->rfb_port = atoi(config);
					sc->rfb_host = tmpstr;
				}
			}
	        } else if (!strcmp(xopts, "vga")) {
			if (!strcmp(config, "off")) {
				sc->vga_enabled = 0;
			} else if (!strcmp(config, "io")) {
				sc->vga_enabled = 1;
				sc->vga_full = 0;
			} else if (!strcmp(config, "on")) {
				sc->vga_enabled = 1;
				sc->vga_full = 1;
			} else {
				pci_fbuf_usage(xopts);
				ret = -1;
				goto done;
			}
	        } else if (!strcmp(xopts, "w")) {
		        sc->memregs.width = atoi(config);
			if (sc->memregs.width > COLS_MAX) {
				pci_fbuf_usage(xopts);
				ret = -1;
				goto done;
			} else if (sc->memregs.width == 0)
				sc->memregs.width = 1920;
		} else if (!strcmp(xopts, "h")) {
			sc->memregs.height = atoi(config);
			if (sc->memregs.height > ROWS_MAX) {
				pci_fbuf_usage(xopts);
				ret = -1;
				goto done;
			} else if (sc->memregs.height == 0)
				sc->memregs.height = 1080;
		} else if (!strcmp(xopts, "password")) {
			sc->rfb_password = config;
		} else {
			pci_fbuf_usage(xopts);
			ret = -1;
			goto done;
		}
	}

done:
	return (ret);
}


extern void vga_render(struct bhyvegc *gc, void *arg);

void
pci_fbuf_render(struct bhyvegc *gc, void *arg)
{
	struct pci_fbuf_softc *sc;

	sc = arg;

	if (sc->vga_full && sc->gc_image->vgamode) {
		/* TODO: mode switching to vga and vesa should use the special
		 *      EFI-bhyve protocol port.
		 */
		vga_render(gc, sc->vgasc);
		return;
	}
	if (sc->gc_width != sc->memregs.width ||
	    sc->gc_height != sc->memregs.height) {
		bhyvegc_resize(gc, sc->memregs.width, sc->memregs.height);
		sc->gc_width = sc->memregs.width;
		sc->gc_height = sc->memregs.height;
	}

	return;
}

static int
pci_fbuf_init(struct vmctx *ctx, struct pci_devinst *pi, char *opts)
{
	int error, prot;
	struct pci_fbuf_softc *sc;
	
	if (fbuf_sc != NULL) {
		fprintf(stderr, "Only one frame buffer device is allowed.\n");
		return (-1);
	}

	sc = calloc(1, sizeof(struct pci_fbuf_softc));

	pi->pi_arg = sc;

	/* initialize config space */
	pci_set_cfgdata16(pi, PCIR_DEVICE, 0x40FB);
	pci_set_cfgdata16(pi, PCIR_VENDOR, 0xFB5D);
	pci_set_cfgdata8(pi, PCIR_CLASS, PCIC_DISPLAY);
	pci_set_cfgdata8(pi, PCIR_SUBCLASS, PCIS_DISPLAY_VGA);

	error = pci_emul_alloc_bar(pi, 0, PCIBAR_MEM32, DMEMSZ);
	assert(error == 0);

	error = pci_emul_alloc_bar(pi, 1, PCIBAR_MEM32, FB_SIZE);
	assert(error == 0);

	error = pci_emul_add_msicap(pi, PCI_FBUF_MSI_MSGS);
	assert(error == 0);

	sc->fbaddr = pi->pi_bar[1].addr;
	sc->memregs.fbsize = FB_SIZE;
	sc->memregs.width  = COLS_DEFAULT;
	sc->memregs.height = ROWS_DEFAULT;
	sc->memregs.depth  = 32;

	sc->vga_enabled = 1;
	sc->vga_full = 0;

	sc->fsc_pi = pi;

	error = pci_fbuf_parse_opts(sc, opts);
	if (error != 0)
		goto done;

	/* XXX until VGA rendering is enabled */
	if (sc->vga_full != 0) {
		fprintf(stderr, "pci_fbuf: VGA rendering not enabled");
		goto done;
	}

	sc->fb_base = vm_create_devmem(ctx, VM_FRAMEBUFFER, "framebuffer", FB_SIZE);
	if (sc->fb_base == MAP_FAILED) {
		error = -1;
		goto done;
	}
	DPRINTF(DEBUG_INFO, ("fbuf frame buffer base: %p [sz %lu]\r\n",
	        sc->fb_base, FB_SIZE));

	/*
	 * Map the framebuffer into the guest address space.
	 * XXX This may fail if the BAR is different than a prior
	 * run. In this case flag the error. This will be fixed
	 * when a change_memseg api is available.
	 */
	prot = PROT_READ | PROT_WRITE;
	if (vm_mmap_memseg(ctx, sc->fbaddr, VM_FRAMEBUFFER, 0, FB_SIZE, prot) != 0) {
		fprintf(stderr, "pci_fbuf: mapseg failed - try deleting VM and restarting\n");
		error = -1;
		goto done;
	}

	console_init(sc->memregs.width, sc->memregs.height, sc->fb_base);
	console_fb_register(pci_fbuf_render, sc);

	if (sc->vga_enabled)
		sc->vgasc = vga_init(!sc->vga_full);
	sc->gc_image = console_get_image();

	fbuf_sc = sc;

	memset((void *)sc->fb_base, 0, FB_SIZE);

	error = rfb_init(sc->rfb_host, sc->rfb_port, sc->rfb_wait, sc->rfb_password);
done:
	if (error)
		free(sc);

	return (error);
}

struct pci_devemu pci_fbuf = {
	.pe_emu =	"fbuf",
	.pe_init =	pci_fbuf_init,
	.pe_barwrite =	pci_fbuf_write,
	.pe_barread =	pci_fbuf_read
};
PCI_EMUL_SET(pci_fbuf);
