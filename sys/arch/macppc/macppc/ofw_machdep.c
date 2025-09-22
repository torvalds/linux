/*	$OpenBSD: ofw_machdep.c,v 1.65 2024/04/13 23:44:11 jsg Exp $	*/
/*	$NetBSD: ofw_machdep.c,v 1.1 1996/09/30 16:34:50 ws Exp $	*/

/*
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "akbd.h"
#include "ukbd.h"
#include "wsdisplay.h"
#include "zstty.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <powerpc/powerpc.h>
#include <machine/autoconf.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pci.h>

#include <macppc/macppc/ofw_machdep.h>

#if NAKBD > 0
#include <dev/adb/akbdvar.h>
#endif

#if NUKBD > 0
#include <dev/usb/ukbdvar.h>
#endif

#if NWSDISPLAY > 0
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#endif

struct mem_region64 {
	uint64_t start;
	uint32_t size;
} __packed;

#define	OFMEM_REGIONS	32
static struct mem_region   OFmem[OFMEM_REGIONS + 1], OFavail[OFMEM_REGIONS + 3];
static struct mem_region64 OFmem64[OFMEM_REGIONS + 1];

/*
 * Section 5.1.7. Memory Management Unit properties.
 */
struct ofw_map {
	uint32_t	om_virt;
	uint32_t	om_size;
	uint32_t	om_phys;
	uint32_t	om_mode;
} __packed;

struct ofw_map64 {
	uint32_t	om_virt;
	uint32_t	om_size;
	uint64_t	om_phys;
	uint32_t	om_mode;
} __packed;

static struct ofw_map	ofw_maps[OFMEM_REGIONS];
static struct ofw_map64	ofw_maps64[OFMEM_REGIONS];

#if NWSDISPLAY > 0
struct ofwfb {
	struct rasops_info	ofw_ri;
	struct wsscreen_descr	ofw_wsd;
};

/* Early boot framebuffer */
static struct ofwfb ofwfb;
#endif

int	save_ofw_mapping(void);
void	ofw_consinit(int);
void	ofw_read_mem_regions(int, int, int);

int	ofw_set_param(struct wsdisplay_param *);
int	ofw_get_param(struct wsdisplay_param *);

/*
 * This is called during initppc, before the system is really initialized.
 * It shall provide the total and the available regions of RAM.
 * Both lists must have a zero-size entry as terminator.
 * The available regions need not take the kernel into account, but needs
 * to provide space for two additional entry beyond the terminating one.
 */
void
ppc_mem_regions(struct mem_region **memp, struct mem_region **availp)
{
	*memp = OFmem;

	/* HACK */
	if (OFmem[0].size == 0) {
		*memp = OFavail;
	}

	*availp = OFavail;
}

void
ofw_read_mem_regions(int phandle, int address_cells, int size_cells)
{
	int nreg, navail;
	int i, j;

	switch (address_cells) {
	default:
	case 1:
		nreg = OF_getprop(phandle, "reg", OFmem,
		    sizeof(OFmem[0]) * OFMEM_REGIONS) / sizeof(OFmem[0]);
		break;
	case 2:
		nreg = OF_getprop(phandle, "reg", OFmem64,
		    sizeof(OFmem64[0]) * OFMEM_REGIONS) / sizeof(OFmem64[0]);
		break;
	}

	navail = OF_getprop(phandle, "available", OFavail,
	    sizeof(OFavail[0]) * OFMEM_REGIONS) / sizeof(OFavail[0]);
	if (nreg <= 0 || navail <= 0)
		panic("no memory?");

	/* Eliminate empty or unreachable regions. */
	switch (address_cells) {
	default:
	case 1:
		for (i = 0, j = 0; i < nreg; i++) {
			if (OFmem[i].size == 0)
				continue;
			if (i != j) {
				OFmem[j].start = OFmem[i].start;
				OFmem[j].size = OFmem[i].size;
				OFmem[i].start = 0;
				OFmem[i].size = 0;
			}
			j++;
		}
		break;
	case 2:
		for (i = 0, j = 0; i < nreg; i++) {
			if (OFmem64[i].size == 0)
				continue;
			if (OFmem64[i].start >= 1ULL << 32)
				continue;
			OFmem[j].start = OFmem64[i].start;
			if (OFmem64[i].start + OFmem64[i].size >= 1ULL << 32)
				OFmem[j].size = (1ULL << 32) - OFmem64[i].start;
			else
				OFmem[j].size = OFmem64[i].size;
			j++;
		}
		break;
	}
}

typedef void (fwcall_f)(int, int);
extern fwcall_f *fwcall;
fwcall_f fwentry;

int OF_stdout;
int OF_stdin;

/*
 * Called early in the boot process, we are still running on the stack
 * provided by the bootloader using the firmware's page table.
 */
int
save_ofw_mapping(void)
{
	int chosen, memory, root, mmui, mmu = -1;
	int acells, scells;
	int i, len;

	if ((chosen = OF_finddevice("/chosen")) == -1)
		return (0);

	ofw_consinit(chosen);

	/* Get memory node. */
	memory = OF_finddevice("/memory");
	if (memory == -1)
		panic("no memory?");

	/* Are physical addresses encoded in 32 or 64 bits? */
	root = OF_parent(memory);
	if (OF_getprop(root, "#address-cells", &acells, 4) <= 0)
		acells = 1;
	if (OF_getprop(root, "#size-cells", &scells, 4) <= 0)
		scells = 1;

	if (scells != 1)
		panic("unexpected memory layout %d:%d", acells, scells);

	ofw_read_mem_regions(memory, acells, scells);

	/* Get firmware mappings. */
	if (OF_getprop(chosen, "mmu", &mmui, sizeof(int)) != -1)
		mmu = OF_instance_to_package(mmui);
	if (mmu != -1) {
		len = OF_getproplen(mmu, "translations");
		if (len <= 0)
			return (0);
	}

	switch (acells) {
	case 2:
		OF_getprop(mmu, "translations", ofw_maps64, sizeof(ofw_maps64));

		for (i = 0; i < nitems(ofw_maps64); i++) {
			if (ofw_maps64[i].om_phys >= 1ULL << 32)
				continue;

			ofw_maps[i].om_virt = ofw_maps64[i].om_virt;
			ofw_maps[i].om_size = ofw_maps64[i].om_size;
			ofw_maps[i].om_phys = (uint32_t)ofw_maps64[i].om_phys;
			ofw_maps[i].om_mode = ofw_maps64[i].om_mode;
		}
		break;
	case 1:
	default:
		OF_getprop(mmu, "translations", ofw_maps, sizeof(ofw_maps));
		break;
	}

	/*
	 * Next time we'll call the firmware make sure we save and
	 * restore our MMU settings.
	 */
	fwcall = &fwentry;

	return (0);
}

static int display_ofh;
int cons_backlight;
int cons_brightness;
int cons_backlight_available;
int fbnode;

void of_display_console(void);

void
ofwconprobe(void)
{
	char type[32];
	int stdout_node;

	stdout_node = OF_instance_to_package(OF_stdout);

	/* handle different types of console */

	bzero(type, sizeof(type));
	if (OF_getprop(stdout_node,  "device_type", type, sizeof(type)) == -1) {
		return; /* XXX */
	}
	if (strcmp(type, "display") == 0) {
		of_display_console();
		return;
	}
	if (strcmp(type, "serial") == 0) {
#if NZSTTY > 0
		/* zscnprobe/zscninit do all the required initialization */
		return;
#endif
	}

	OF_stdout = OF_open("screen");
	OF_stdin = OF_open("keyboard");

	/* cross fingers that this works. */
	of_display_console();

	return;
}

#define DEVTREE_UNKNOWN 0
#define DEVTREE_USB	1
#define DEVTREE_ADB	2
int ofw_devtree = DEVTREE_UNKNOWN;

#define OFW_HAVE_USBKBD 1
#define OFW_HAVE_ADBKBD 2
int ofw_have_kbd = 0;

void ofw_recurse_keyboard(int pnode);
void ofw_find_keyboard(void);

void
ofw_recurse_keyboard(int pnode)
{
	char name[32];
	int old_devtree;
	int len;
	int node;

	for (node = OF_child(pnode); node != 0; node = OF_peer(node)) {

		len = OF_getprop(node, "name", name, 20);
		if (len == 0)
			continue;
		name[len] = 0;
		if (strcmp(name, "keyboard") == 0) {
			/* found a keyboard node, where is it? */
			if (ofw_devtree == DEVTREE_USB) {
				ofw_have_kbd |= OFW_HAVE_USBKBD;
			} else if (ofw_devtree == DEVTREE_ADB) {
				ofw_have_kbd |= OFW_HAVE_ADBKBD;
			} else {
				/* hid or some other keyboard? ignore */
			}
			continue;
		}

		old_devtree = ofw_devtree;

		if (strcmp(name, "adb") == 0) {
			ofw_devtree = DEVTREE_ADB;
		}
		if (strcmp(name, "usb") == 0) {
			ofw_devtree = DEVTREE_USB;
		}

		ofw_recurse_keyboard(node);

		ofw_devtree = old_devtree; /* nest? */
	}
}

void
ofw_find_keyboard(void)
{
	int stdin_node;
	char iname[32];
	int len, attach = 0;

	stdin_node = OF_instance_to_package(OF_stdin);
	len = OF_getprop(stdin_node, "name", iname, 20);
	iname[len] = 0;
	printf(" console in [%s]", iname);

	/* GRR, apple removed the interface once used for keyboard
	 * detection walk the OFW tree to find keyboards and what type.
	 */

	ofw_recurse_keyboard(OF_peer(0));

	len = OF_getprop(OF_peer(0), "model", iname, sizeof(iname));
	iname[len] = 0;

	if (ofw_have_kbd == (OFW_HAVE_USBKBD | OFW_HAVE_ADBKBD)) {
		/*
		 * If a PowerBook reports having ABD and USB keyboards,
		 * use the builtin ADB one for console, the USB one is
		 * certainly a HID device.
		 */
		if (strncmp(iname, "PowerBook", 9) == 0)
			ofw_have_kbd = OFW_HAVE_ADBKBD;
		else
			ofw_have_kbd = OFW_HAVE_USBKBD;
		printf("USB and ADB found");
	}
	if (ofw_have_kbd == OFW_HAVE_USBKBD) {
#if NUKBD > 0
		printf(", using USB\n");
		ukbd_cnattach();
		attach = 1;
#endif
	} else if (ofw_have_kbd == OFW_HAVE_ADBKBD) {
#if NAKBD >0
		printf(", using ADB\n");
		akbd_cnattach();
		attach = 1;
#endif
	} 
	if (attach == 0) {
#if NUKBD > 0
		printf(", no keyboard attached, trying usb anyway\n");
		ukbd_cnattach();
#else
		printf(", no keyboard found!\n");
#endif
	}
}

void
of_display_console(void)
{
	struct ofw_pci_register addr[8];
	int cons_height, cons_width, cons_linebytes, cons_depth;
	uint32_t cons_addr;
	char name[32];
	int len, err;
	int stdout_node;

	stdout_node = OF_instance_to_package(OF_stdout);
	len = OF_getprop(stdout_node, "name", name, 20);
	name[len] = 0;
	printf("console out [%s]", name);
	display_ofh = OF_stdout;
	err = OF_getprop(stdout_node, "width", &cons_width, 4);
	if ( err != 4) {
		cons_width = 0;
	}
	err = OF_getprop(stdout_node, "linebytes", &cons_linebytes, 4);
	if ( err != 4) {
		cons_linebytes = cons_width;
	}
	err = OF_getprop(stdout_node, "height", &cons_height, 4);
	if ( err != 4) {
		cons_height = 0;
	}
	err = OF_getprop(stdout_node, "depth", &cons_depth, 4);
	if ( err != 4) {
		cons_depth = 0;
	}
	err = OF_getprop(stdout_node, "address", &cons_addr, 4);
	if ( err != 4) {
		OF_interpret("frame-buffer-adr", 1, &cons_addr);
	}

	ofw_find_keyboard();

	fbnode = stdout_node;
	len = OF_getprop(stdout_node, "assigned-addresses", addr, sizeof(addr));
	if (len == -1) {
		fbnode = OF_parent(stdout_node);
		len = OF_getprop(fbnode, "name", name, 20);
		name[len] = 0;

		printf("using parent %s:", name);
		len = OF_getprop(fbnode, "assigned-addresses",
			addr, sizeof(addr));
		if (len < sizeof(addr[0])) {
			panic(": no address");
		}
	}

	if (OF_getnodebyname(0, "backlight") != 0) {
		cons_backlight_available = 1;
		cons_backlight = WSDISPLAYIO_VIDEO_ON;
		of_setbrightness(DEFAULT_BRIGHTNESS);
	
		/* wsconsctl hooks */
		ws_get_param = ofw_get_param;
		ws_set_param = ofw_set_param;
	}

#if 1
	printf(": memaddr %x, size %x ", addr[0].phys_lo, addr[0].size_lo);
	printf(": consaddr %x ", cons_addr);
	printf(": ioaddr %x, size %x", addr[1].phys_lo, addr[1].size_lo);
	printf(": width %d linebytes %d height %d depth %d\n",
		cons_width, cons_linebytes, cons_height, cons_depth);
#endif

#if NWSDISPLAY > 0
{
	struct ofwfb *fb = &ofwfb;
	struct rasops_info *ri = &fb->ofw_ri;
	uint32_t defattr;

	ri->ri_width = cons_width;
	ri->ri_height = cons_height;
	ri->ri_depth = cons_depth;
	ri->ri_stride = cons_linebytes;
	ri->ri_flg = RI_CENTER | RI_FULLCLEAR | RI_CLEAR;
	ri->ri_bits = (void *)mapiodev(cons_addr, cons_linebytes * cons_height);
	ri->ri_hw = fb;

	if (cons_depth == 8)
		of_setcolors(rasops_cmap, 0, 256);

	rasops_init(ri, 160, 160);

	strlcpy(fb->ofw_wsd.name, "std", sizeof(fb->ofw_wsd.name));
	fb->ofw_wsd.capabilities = ri->ri_caps;
	fb->ofw_wsd.ncols = ri->ri_cols;
	fb->ofw_wsd.nrows = ri->ri_rows;
	fb->ofw_wsd.textops = &ri->ri_ops;
#if 0
	fb->ofw_wsd.fontwidth = ri->ri_font->fontwidth;
	fb->ofw_wsd.fontheight = ri->ri_font->fontheight;
#endif

	ri->ri_ops.pack_attr(ri, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&fb->ofw_wsd, ri, 0, 0, defattr);
}
#endif
}

void
ofwconsswitch(struct rasops_info *ri)
{
#if NWSDISPLAY > 0
	ri->ri_width = ofwfb.ofw_ri.ri_width;
	ri->ri_height = ofwfb.ofw_ri.ri_height;
	ri->ri_depth = ofwfb.ofw_ri.ri_depth;
	ri->ri_stride = ofwfb.ofw_ri.ri_stride;

	ri->ri_bits = ofwfb.ofw_ri.ri_bits /* XXX */;
#endif
}

void
of_setbacklight(int on)
{
	if (cons_backlight_available == 0)
		return;

	cons_backlight = on;

	if (on)
		OF_call_method_1("backlight-on", display_ofh, 0);
	else
		OF_call_method_1("backlight-off", display_ofh, 0);
}

void
of_setbrightness(int brightness)
{
	if (cons_backlight_available == 0)
		return;

	if (brightness < MIN_BRIGHTNESS)
		brightness = MIN_BRIGHTNESS;
	else if (brightness > MAX_BRIGHTNESS)
		brightness = MAX_BRIGHTNESS;

	cons_brightness = brightness;

	/*
	 * The OF method is called "set-contrast" but affects brightness.
	 * Don't ask.
	 */
	OF_call_method_1("set-contrast", display_ofh, 1, cons_brightness);

	/* XXX this routine should also save the brightness settings in the nvram */
}

uint8_t of_cmap[256 * 3];

void
of_setcolors(const uint8_t *cmap, unsigned int index, unsigned int count)
{
	bcopy(cmap, of_cmap, sizeof(of_cmap));
	OF_call_method_1("set-colors", display_ofh, 3, &of_cmap, index, count);
}

#include <dev/cons.h>

cons_decl(ofw);

/*
 * Console support functions
 */
void
ofwcnprobe(struct consdev *cd)
{
}

void
ofwcninit(struct consdev *cd)
{
}
void
ofwcnputc(dev_t dev, int c)
{
	char ch = c;

	OF_write(OF_stdout, &ch, 1);
}
int
ofwcngetc(dev_t dev)
{
        unsigned char ch = '\0';
        int l;

        while ((l = OF_read(OF_stdin, &ch, 1)) != 1)
                if (l != -2 && l != 0)
                        return -1;
        return ch;
}

void
ofwcnpollc(dev_t dev, int on)
{
}

struct consdev consdev_ofw = {
        ofwcnprobe,
        ofwcninit,
        ofwcngetc,
        ofwcnputc,
        ofwcnpollc,
        NULL,
};

void
ofw_consinit(int chosen)
{
	struct consdev *cp = &consdev_ofw;

	OF_getprop(chosen, "stdin", &OF_stdin, sizeof(OF_stdin));
	OF_getprop(chosen, "stdout", &OF_stdout, sizeof(OF_stdout));

	/* If the screen is to be console, but not active, open it */
	if (OF_stdout == 0)
		OF_stdout = OF_open("screen");

	cn_tab = cp;
}

int
ofw_set_param(struct wsdisplay_param *dp)
{
	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		if (cons_backlight_available != 0) {
			of_setbrightness(dp->curval);
			return 0;
		}
		break;
	case WSDISPLAYIO_PARAM_BACKLIGHT:
		if (cons_backlight_available != 0) {
			of_setbacklight(dp->curval ? WSDISPLAYIO_VIDEO_ON
			    : WSDISPLAYIO_VIDEO_OFF);
			return 0;
		}
		break;
	default:
		break;
	}

	return -1;
}

int
ofw_get_param(struct wsdisplay_param *dp)
{
	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		if (cons_backlight_available != 0) {
			dp->min = MIN_BRIGHTNESS;
			dp->max = MAX_BRIGHTNESS;
			dp->curval = cons_brightness;
			return 0;
		}
		break;
	case WSDISPLAYIO_PARAM_BACKLIGHT:
		if (cons_backlight_available != 0) {
			dp->min = 0;
			dp->max = 1;
			dp->curval = cons_backlight;
			return 0;
		}
		break;
	default:
		break;
	}

	return -1;
}
