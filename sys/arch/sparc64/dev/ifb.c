/*	$OpenBSD: ifb.c,v 1.27 2024/05/14 08:26:13 jsg Exp $	*/

/*
 * Copyright (c) 2007, 2008, 2009 Miodrag Vallat.
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
 * Least-effort driver for the Sun Expert3D cards (based on the
 * ``Wildcat'' chips).
 *
 * There is no public documentation for these chips available.
 * Since they are no longer supported by 3DLabs (which got bought by
 * Creative), and Sun does not want to publish even minimal information
 * or source code, the best we can do is experiment.
 *
 * Quoting Alan Coopersmith in
 * http://mail.opensolaris.org/pipermail/opensolaris-discuss/2005-December/011885.html
 * ``Unfortunately, the lawyers have asked we not give details about why
 *   specific components are not being released.''
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/pciio.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/openfirm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#include <dev/rasops/rasops.h>

#include <machine/fbvar.h>

#ifdef APERTURE
extern int allowaperture;
#endif

/*
 * Parts of the following hardware knowledge come from David S. Miller's
 * XVR-500 Linux driver (drivers/video/sunxvr500.c).
 */

/*
 * The Expert3D and Expert3d-Lite cards are built around the Wildcat
 * 5110, 6210 and 7210 chips.
 *
 * The card exposes the following resources:
 * - a 32MB (ifb), 64MB (xvr600) or 128MB (jfb) aperture window in which
 *   views to the different frame buffer areas can be mapped, in the first BAR.
 * - a 64KB or 128KB PROM and registers area, in the second BAR.
 * - a 8MB ``direct burst'' memory mapping, in the third BAR.
 *
 * The location of this BAR range is pointed to by a board-specific PCI
 * configuration register.
 *
 * In the state the PROM leaves us in, the 8MB frame buffer windows map
 * the video memory as interleaved stripes, of which the non-visible parts
 * can still be addressed (probably for fast screen switching).
 *
 * Unfortunately, since we do not know how to reconfigure the stripes
 * to provide at least a linear frame buffer, we have to write to both
 * windows and have them provide the complete image.
 *
 * Moreover, high pixel values in the overlay planes (such as 0xff or 0xfe)
 * seem to enable other planes with random contents, so we'll limit ourselves
 * to 7bpp operation.
 */

/*
 * The Fcode driver sets up a communication structure, allowing third-party
 * code to reprogram the video mode while still allowing the Fcode routines
 * to access the overlay planes.
 *
 * We'll use this information as well, although so far it's unlikely
 * any code will do so, as long as the only documentation for this
 * hardware amounts to zilch.
 */

/* probably some form of signature */
#define	IFB_SHARED_SIGNATURE		0x00
#define	SIG_IFB					0x09209911
#define	SIG_JFB					0x05140213
#define	IFB_SHARED_MONITOR_MODE		0x10
#define	IFB_SHARED_WIDTH		0x14
#define	IFB_SHARED_HEIGHT		0x18
#define	IFB_SHARED_V_FREQ		0x1c
#define	IFB_SHARED_TIMING_H_FP		0x20
#define	IFB_SHARED_TIMING_H_SYNC	0x24
#define	IFB_SHARED_TIMING_H_BP		0x28
#define	IFB_SHARED_TIMING_V_FP		0x2c
#define	IFB_SHARED_TIMING_V_SYNC	0x30
#define	IFB_SHARED_TIMING_V_BP		0x34
#define	IFB_SHARED_TIMING_FLAGS		0x38
#define	IFB_SHARED_CMAP_DIRTY		0x3c
#define	IFB_SHARED_TERM8_GSR		0x4c
#define	IFB_SHARED_TERM8_SPR		0x50
#define	IFB_SHARED_TERM8_SPLR		0x54

/*
 * The Expert3D has an extra BAR that is not present on the -Lite
 * version.  This register contains bits that tell us how many BARs to
 * skip before we get to the BARs that interest us.
 */
#define IFB_PCI_CFG			0x5c
#define IFB_PCI_CFG_BAR_OFFSET(x)	((x & 0x000000e0) >> 3)

/*
 * 6000 (jfb) / 8000 (ifb) engine command
 * This register is used to issue (some) commands sequences to the
 * acceleration hardware.
 */
#define JFB_REG_ENGINE			0x6000
#define IFB_REG_ENGINE			0x8000

/*
 * 8040 component configuration
 * This register controls which parts of the board will be addressed by
 * writes to other configuration registers.
 * Apparently the low two bytes control the frame buffer windows for the
 * given head (starting at 1).
 * The high two bytes are texture related.
 */
#define	IFB_REG_COMPONENT_SELECT	0x8040

/*
 * 8044 status
 * This register has a bit that signals completion of commands issued
 * to the acceleration hardware.
 */
#define IFB_REG_STATUS			0x8044
#define IFB_REG_STATUS_DONE			0x00000004

/*
 * 8058 magnifying configuration
 * This register apparently controls magnifying.
 * bits 5-6 select the window width divider (00: by 2, 01: by 4, 10: by 8,
 *   11: by 16)
 * bits 7-8 select the zoom factor (00: disabled, 01: x2, 10: x4, 11: x8)
 */
#define	IFB_REG_MAGNIFY			0x8058
#define	IFB_REG_MAGNIFY_DISABLE			0x00000000
#define	IFB_REG_MAGNIFY_X2			0x00000040
#define	IFB_REG_MAGNIFY_X4			0x00000080
#define	IFB_REG_MAGNIFY_X8			0x000000c0
#define	IFB_REG_MAGNIFY_WINDIV2			0x00000000
#define	IFB_REG_MAGNIFY_WINDIV4			0x00000010
#define	IFB_REG_MAGNIFY_WINDIV8			0x00000020
#define	IFB_REG_MAGNIFY_WINDIV16		0x00000030

/*
 * 8070 display resolution
 * Contains the size of the display, as ((height - 1) << 16) | (width - 1)
 */
#define	IFB_REG_RESOLUTION		0x8070
/*
 * 8074 configuration register
 * Contains 0x1a000088 | ((Log2 stride) << 16)
 */
#define	IFB_REG_CONFIG			0x8074
/*
 * 8078 32bit frame buffer window #0 (8 to 9 MB)
 * Contains the offset (relative to BAR0) of the 32 bit frame buffer window.
 */
#define	IFB_REG_FB32_0			0x8078
/*
 * 807c 32bit frame buffer window #1 (8 to 9 MB)
 * Contains the offset (relative to BAR0) of the 32 bit frame buffer window.
 */
#define	IFB_REG_FB32_1			0x807c
/*
 * 8080 8bit frame buffer window #0 (2 to 2.2 MB)
 * Contains the offset (relative to BAR0) of the 8 bit frame buffer window.
 */
#define	IFB_REG_FB8_0			0x8080
/*
 * 8084 8bit frame buffer window #1 (2 to 2.2 MB)
 * Contains the offset (relative to BAR0) of the 8 bit frame buffer window.
 */
#define	IFB_REG_FB8_1			0x8084
/*
 * 8088 unknown window (as large as a 32 bit frame buffer)
 */
#define	IFB_REG_FB_UNK0			0x8088
/*
 * 808c unknown window (as large as a 8 bit frame buffer)
 */
#define	IFB_REG_FB_UNK1			0x808c
/*
 * 8090 unknown window (as large as a 8 bit frame buffer)
 */
#define	IFB_REG_FB_UNK2			0x8090

/*
 * 80bc RAMDAC palette index register
 */
#define	IFB_REG_CMAP_INDEX		0x80bc
/*
 * 80c0 RAMDAC palette data register
 */
#define	IFB_REG_CMAP_DATA		0x80c0

/*
 * 80e4 DPMS state register
 * States ``off'' and ``suspend'' need chip reprogramming before video can
 * be enabled again.
 */
#define	IFB_REG_DPMS_STATE		0x80e4
#define	IFB_REG_DPMS_OFF			0x00000000
#define	IFB_REG_DPMS_SUSPEND			0x00000001
#define	IFB_REG_DPMS_STANDBY			0x00000002
#define	IFB_REG_DPMS_ON				0x00000003

/*
 * (some) ROP codes
 */

#define	IFB_ROP_CLEAR	0x00000000	/* clear bits in rop mask */
#define	IFB_ROP_SRC	0x00330000	/* copy src bits matching rop mask */
#define	IFB_ROP_XOR	0x00cc0000	/* xor src bits with rop mask */
#define	IFB_ROP_SET	0x00ff0000	/* set bits in rop mask */

#define IFB_COORDS(x, y)	((x) | (y) << 16)

/* blitter directions */
#define IFB_BLT_DIR_BACKWARDS_Y		(0x08 | 0x02)
#define IFB_BLT_DIR_BACKWARDS_X		(0x04 | 0x01)

#define	IFB_PIXELMASK	0x7f	/* 7bpp */

struct ifb_softc {
	struct sunfb sc_sunfb;

	bus_space_tag_t sc_mem_t;
	pcitag_t sc_pcitag;

	/* overlays mappings */
	bus_space_handle_t sc_mem_h;
	bus_addr_t sc_membase, sc_fb8bank0_base, sc_fb8bank1_base;
	bus_size_t sc_memlen;
	vaddr_t	sc_memvaddr, sc_fb8bank0_vaddr, sc_fb8bank1_vaddr;

	/* registers mapping */
	bus_space_handle_t sc_reg_h;
	bus_addr_t sc_regbase;
	bus_size_t sc_reglen;

	/* communication area */
	volatile uint32_t *sc_comm;

	/* acceleration information */
	u_int	sc_acceltype;
#define	IFB_ACCEL_NONE			0
#define	IFB_ACCEL_IFB			1	/* Expert3D style */
#define	IFB_ACCEL_JFB			2	/* XVR-500 style */
	void (*sc_rop)(void *, int, int, int, int, int, int, uint32_t, int32_t);

	/* wsdisplay related goo */
	u_int	sc_mode;
	struct wsdisplay_emulops sc_old_ops;
	u_int8_t sc_cmap_red[256];
	u_int8_t sc_cmap_green[256];
	u_int8_t sc_cmap_blue[256];
};

int	ifb_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	ifb_mmap(void *, off_t, int);
void	ifb_burner(void *, u_int, u_int);

struct wsdisplay_accessops ifb_accessops = {
	.ioctl = ifb_ioctl,
	.mmap = ifb_mmap,
	.burn_screen = ifb_burner
};

int	ifbmatch(struct device *, void *, void *);
void	ifbattach(struct device *, struct device *, void *);

const struct cfattach ifb_ca = {
	sizeof (struct ifb_softc), ifbmatch, ifbattach
};

struct cfdriver ifb_cd = {
	NULL, "ifb", DV_DULL
};

int	ifb_accel_identify(const char *);
static inline
u_int	ifb_dac_value(u_int, u_int, u_int);
int	ifb_getcmap(struct ifb_softc *, struct wsdisplay_cmap *);
static inline
int	ifb_is_console(int);
int	ifb_mapregs(struct ifb_softc *, struct pci_attach_args *);
int	ifb_putcmap(struct ifb_softc *, struct wsdisplay_cmap *);
void	ifb_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);
void	ifb_setcolormap(struct sunfb *,
	    void (*)(void *, u_int, u_int8_t, u_int8_t, u_int8_t));

void	ifb_copyrect(struct ifb_softc *, int, int, int, int, int, int);
void	ifb_fillrect(struct ifb_softc *, int, int, int, int, int);
static inline
void	ifb_rop(struct ifb_softc *, int, int, int, int, int, int, uint32_t,
	    int32_t);
void	ifb_rop_common(struct ifb_softc *, bus_addr_t, int, int, int, int,
	    int, int, uint32_t, int32_t);
void	ifb_rop_ifb(void *, int, int, int, int, int, int, uint32_t, int32_t);
void	ifb_rop_jfb(void *, int, int, int, int, int, int, uint32_t, int32_t);
int	ifb_rop_wait(struct ifb_softc *);

int	ifb_putchar_dumb(void *, int, int, u_int, uint32_t);
int	ifb_copycols_dumb(void *, int, int, int, int);
int	ifb_erasecols_dumb(void *, int, int, int, uint32_t);
int	ifb_copyrows_dumb(void *, int, int, int);
int	ifb_eraserows_dumb(void *, int, int, uint32_t);
int	ifb_do_cursor_dumb(struct rasops_info *);

int	ifb_copycols(void *, int, int, int, int);
int	ifb_erasecols(void *, int, int, int, uint32_t);
int	ifb_copyrows(void *, int, int, int);
int	ifb_eraserows(void *, int, int, uint32_t);
int	ifb_do_cursor(struct rasops_info *);

int
ifbmatch(struct device *parent, void *cf, void *aux)
{
	return ifb_ident(aux);
}

void    
ifbattach(struct device *parent, struct device *self, void *aux)
{
	struct ifb_softc *sc = (struct ifb_softc *)self;
	struct pci_attach_args *paa = aux;
	struct rasops_info *ri;
	uint32_t dev_comm;
	int node, console;
	char *name, *text;
	char namebuf[32];

	sc->sc_mem_t = paa->pa_memt;
	sc->sc_pcitag = paa->pa_tag;

	node = PCITAG_NODE(paa->pa_tag);
	console = ifb_is_console(node);

	printf("\n");

	/*
	 * Multiple heads appear as PCI subfunctions.
	 * However, the ofw node for it lacks most properties,
	 * and its BAR only give access to registers, not
	 * frame buffer memory.
	 */
	if (!node_has_property(node, "device_type")) {
		printf("%s: secondary output not supported yet\n",
		    self->dv_xname);
		return;
	}

	/*
	 * Describe the beast.
	 */

	name = text = getpropstringA(node, "name", namebuf);
	if (strncmp(text, "SUNW,", 5) == 0)
		text += 5;
	printf("%s: %s", self->dv_xname, text);
	text = getpropstring(node, "model");
	if (*text != '\0')
		printf(" (%s)", text);

	if (ifb_mapregs(sc, paa))
		return;

	sc->sc_fb8bank0_base = bus_space_read_4(sc->sc_mem_t, sc->sc_reg_h,
	      IFB_REG_FB8_0);
	sc->sc_fb8bank1_base = bus_space_read_4(sc->sc_mem_t, sc->sc_reg_h,
	      IFB_REG_FB8_1);

	sc->sc_memvaddr = (vaddr_t)bus_space_vaddr(sc->sc_mem_t, sc->sc_mem_h);
	sc->sc_fb8bank0_vaddr = sc->sc_memvaddr +
	    sc->sc_fb8bank0_base - sc->sc_membase;
	sc->sc_fb8bank1_vaddr = sc->sc_memvaddr +
	    sc->sc_fb8bank1_base - sc->sc_membase;

	/*
	 * The values stored into the node properties might have been
	 * modified since the Fcode was last run. Pick the geometry
	 * information from the configuration registers instead.
	 * This replaces
	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, 0);
	 */

	sc->sc_sunfb.sf_width = (bus_space_read_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_RESOLUTION) & 0xffff) + 1;
	sc->sc_sunfb.sf_height = (bus_space_read_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_RESOLUTION) >> 16) + 1;
	sc->sc_sunfb.sf_depth = 8;
	sc->sc_sunfb.sf_linebytes = 1 << (bus_space_read_4(sc->sc_mem_t,
	    sc->sc_reg_h, IFB_REG_CONFIG) >> 16);
	sc->sc_sunfb.sf_fbsize =
	    sc->sc_sunfb.sf_height * sc->sc_sunfb.sf_linebytes;

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	ri = &sc->sc_sunfb.sf_ro;
	ri->ri_bits = NULL;
	ri->ri_hw = sc;

	fbwscons_init(&sc->sc_sunfb, RI_BSWAP, console);

	/*
	 * Find out what flavour of ifb we are...
	 */

	sc->sc_acceltype = ifb_accel_identify(name);

	switch (sc->sc_acceltype) {
	case IFB_ACCEL_IFB:
		sc->sc_rop = ifb_rop_ifb;
		break;
	case IFB_ACCEL_JFB:
		/*
		 * Remember the address of the communication area
		 */
		if (OF_getprop(node, "dev-comm", &dev_comm,
		    sizeof dev_comm) != -1) {
			sc->sc_comm = (volatile uint32_t *)(vaddr_t)dev_comm;
		}
		sc->sc_rop = ifb_rop_jfb;
		break;
	}

	/*
	 * Clear the unwanted pixel planes: all if non console (thus
	 * white background), and all planes above 7bpp otherwise.
	 * This also allows to check whether the accelerated code works,
	 * or not.
	 */

	if (sc->sc_acceltype != IFB_ACCEL_NONE) {
		ifb_rop(sc, 0, 0, 0, 0, sc->sc_sunfb.sf_width,
		    sc->sc_sunfb.sf_height, IFB_ROP_CLEAR,
		    console ? ~IFB_PIXELMASK : ~0);
		if (ifb_rop_wait(sc) == 0) {
			/* fall back to dumb software operation */
			sc->sc_acceltype = IFB_ACCEL_NONE;
		}
	}

	if (sc->sc_acceltype == IFB_ACCEL_NONE) {
		/* due to the way we will handle updates */
		ri->ri_flg &= ~RI_FULLCLEAR;

		if (!console) {
			bzero((void *)sc->sc_fb8bank0_vaddr,
			    sc->sc_sunfb.sf_fbsize);
			bzero((void *)sc->sc_fb8bank1_vaddr,
			    sc->sc_sunfb.sf_fbsize);
		}
	}

	/* pick centering delta */
	sc->sc_fb8bank0_vaddr += ri->ri_bits - ri->ri_origbits;
	sc->sc_fb8bank1_vaddr += ri->ri_bits - ri->ri_origbits;

	sc->sc_old_ops = ri->ri_ops;	/* structure copy */

	if (sc->sc_acceltype != IFB_ACCEL_NONE) {
		ri->ri_ops.copyrows = ifb_copyrows;
		ri->ri_ops.copycols = ifb_copycols;
		ri->ri_ops.eraserows = ifb_eraserows;
		ri->ri_ops.erasecols = ifb_erasecols;
		ri->ri_ops.putchar = ifb_putchar_dumb;
		ri->ri_do_cursor = ifb_do_cursor;
	} else {
		ri->ri_ops.copyrows = ifb_copyrows_dumb;
		ri->ri_ops.copycols = ifb_copycols_dumb;
		ri->ri_ops.eraserows = ifb_eraserows_dumb;
		ri->ri_ops.erasecols = ifb_erasecols_dumb;
		ri->ri_ops.putchar = ifb_putchar_dumb;
		ri->ri_do_cursor = ifb_do_cursor_dumb;
	}

	ifb_setcolormap(&sc->sc_sunfb, ifb_setcolor);
	sc->sc_mode = WSDISPLAYIO_MODE_EMUL;

	if (console)
		fbwscons_console_init(&sc->sc_sunfb, -1);
	fbwscons_attach(&sc->sc_sunfb, &ifb_accessops, console);
}

/*
 * Attempt to identify what kind of ifb we are talking to, so as to setup
 * proper acceleration information.
 */
int
ifb_accel_identify(const char *name)
{
	if (strcmp(name, "SUNW,Expert3D") == 0 ||
	    strcmp(name, "SUNW,Expert3D-Lite") == 0)
		return IFB_ACCEL_IFB;	/* ifblite */

	if (strcmp(name, "SUNW,XVR-1200") == 0)
		return IFB_ACCEL_JFB;	/* jfb */

	if (strcmp(name, "SUNW,XVR-600") == 0)
		return IFB_ACCEL_JFB;	/* xvr600 */

	/* XVR-500 is bobcat */

	return IFB_ACCEL_NONE;
}

int
ifb_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct ifb_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;
	struct pcisel *sel;
	int mode;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_IFB;
		break;

	case WSDISPLAYIO_SMODE:
		mode = *(u_int *)data;
		if (mode == WSDISPLAYIO_MODE_EMUL)
			ifb_setcolormap(&sc->sc_sunfb, ifb_setcolor);
		sc->sc_mode = mode;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width  = sc->sc_sunfb.sf_width;
		wdf->depth  = sc->sc_sunfb.sf_depth;
		wdf->stride = sc->sc_sunfb.sf_linebytes;
		wdf->offset = 0;
		wdf->cmsize = 256;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;

	case WSDISPLAYIO_GETCMAP:
		return ifb_getcmap(sc, (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		return ifb_putcmap(sc, (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_GPCIID:
		sel = (struct pcisel *)data;
		sel->pc_bus = PCITAG_BUS(sc->sc_pcitag);
		sel->pc_dev = PCITAG_DEV(sc->sc_pcitag);
		sel->pc_func = PCITAG_FUN(sc->sc_pcitag);
		break;

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return -1; /* not supported yet */
        }

	return 0;
}

static inline
u_int
ifb_dac_value(u_int r, u_int g, u_int b)
{
	/*
	 * Convert 8 bit values to 10 bit scale, by shifting and inserting
	 * the former high bits in the low two bits.
	 * Simply shifting is slightly too dull.
	 */
	r = (r << 2) | (r >> 6);
	g = (g << 2) | (g >> 6);
	b = (b << 2) | (b >> 6);

	return (b << 20) | (g << 10) | r;
}

int
ifb_getcmap(struct ifb_softc *sc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	int error;

	if (index >= 256 || count > 256 - index)
		return EINVAL;

	error = copyout(&sc->sc_cmap_red[index], cm->red, count);
	if (error)
		return error;
	error = copyout(&sc->sc_cmap_green[index], cm->green, count);
	if (error)
		return error;
	error = copyout(&sc->sc_cmap_blue[index], cm->blue, count);
	if (error)
		return error;
	return 0;
}

int
ifb_putcmap(struct ifb_softc *sc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	u_int i;
	int error;
	u_char *r, *g, *b;

	if (index >= 256 || count > 256 - index)
		return EINVAL;

	if ((error = copyin(cm->red, &sc->sc_cmap_red[index], count)) != 0)
		return error;
	if ((error = copyin(cm->green, &sc->sc_cmap_green[index], count)) != 0)
		return error;
	if ((error = copyin(cm->blue, &sc->sc_cmap_blue[index], count)) != 0)
		return error;

	r = &sc->sc_cmap_red[index];
	g = &sc->sc_cmap_green[index];
	b = &sc->sc_cmap_blue[index];

	for (i = 0; i < count; i++) {
		bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
		    IFB_REG_CMAP_INDEX, index);
		bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, IFB_REG_CMAP_DATA,
		    ifb_dac_value(*r, *g, *b));
		r++, g++, b++, index++;
	}
	return 0;
}

void
ifb_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct ifb_softc *sc = v;

	sc->sc_cmap_red[index] = r;
	sc->sc_cmap_green[index] = g;
	sc->sc_cmap_blue[index] = b;

	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, IFB_REG_CMAP_INDEX,
	    index);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, IFB_REG_CMAP_DATA,
	    ifb_dac_value(r, g, b));
}

/* similar in spirit to fbwscons_setcolormap() */
void
ifb_setcolormap(struct sunfb *sf,
    void (*setcolor)(void *, u_int, u_int8_t, u_int8_t, u_int8_t))
{
	struct rasops_info *ri = &sf->sf_ro;
	int i;
	const u_char *color;

	/*
	 * Compensate for overlay plane limitations. Since we'll operate
	 * in 7bpp mode, our basic colors will use positions 00 to 0f,
	 * and the inverted colors will use positions 7f to 70.
	 */

	for (i = 0x00; i < 0x10; i++) {
		color = &rasops_cmap[i * 3];
		setcolor(sf, i, color[0], color[1], color[2]);
	}
	for (i = 0x70; i < 0x80; i++) {
		color = &rasops_cmap[(0xf0 | i) * 3];
		setcolor(sf, i, color[0], color[1], color[2]);
	}

	/*
	 * Proper operation apparently needs black to be 01, always.
	 * Replace black, red and white with white, black and red.
	 * Kind of ugly, but it works.
	 */
	ri->ri_devcmap[WSCOL_WHITE] = 0x00000000;
	ri->ri_devcmap[WSCOL_BLACK] = 0x01010101;
	ri->ri_devcmap[WSCOL_RED] = 0x07070707;

	color = &rasops_cmap[(WSCOL_WHITE + 8) * 3];	/* real white */
	setcolor(sf, 0, color[0], color[1], color[2]);
	setcolor(sf, IFB_PIXELMASK ^ 0, ~color[0], ~color[1], ~color[2]);
	color = &rasops_cmap[WSCOL_BLACK * 3];
	setcolor(sf, 1, color[0], color[1], color[2]);
	setcolor(sf, IFB_PIXELMASK ^ 1, ~color[0], ~color[1], ~color[2]);
	color = &rasops_cmap[WSCOL_RED * 3];
	setcolor(sf, 7, color[0], color[1], color[2]);
	setcolor(sf, IFB_PIXELMASK ^ 7, ~color[0], ~color[1], ~color[2]);
}

paddr_t
ifb_mmap(void *v, off_t off, int prot)
{
	struct ifb_softc *sc = (struct ifb_softc *)v;

	switch (sc->sc_mode) {
	case WSDISPLAYIO_MODE_MAPPED:
		/*
		 * In mapped mode, provide access to the two overlays,
		 * followed by the control registers, at the following
		 * addresses:
		 * 00000000	overlay 0, size up to 2MB (visible fb size)
		 * 01000000	overlay 1, size up to 2MB (visible fb size)
		 * 02000000	control registers
		 */
		off -= 0x00000000;
		if (off >= 0 && off < round_page(sc->sc_sunfb.sf_fbsize)) {
			return bus_space_mmap(sc->sc_mem_t,
			    sc->sc_fb8bank0_base,
			    off, prot, BUS_SPACE_MAP_LINEAR);
		}
		off -= 0x01000000;
		if (off >= 0 && off < round_page(sc->sc_sunfb.sf_fbsize)) {
			return bus_space_mmap(sc->sc_mem_t,
			    sc->sc_fb8bank1_base,
			    off, prot, BUS_SPACE_MAP_LINEAR);
		}
#ifdef APERTURE
		off -= 0x01000000;
		if (allowaperture != 0 && sc->sc_acceltype != IFB_ACCEL_NONE) {
			if (off >= 0 && off < round_page(sc->sc_reglen)) {
				return bus_space_mmap(sc->sc_mem_t,
				    sc->sc_regbase,
				    off, prot, BUS_SPACE_MAP_LINEAR);
			}
		}
#endif
		break;
	}

	return -1;
}

void
ifb_burner(void *v, u_int on, u_int flags)
{
	struct ifb_softc *sc = (struct ifb_softc *)v;
	int s;
	uint32_t dpms;

	s = splhigh();
	if (on)
		dpms = IFB_REG_DPMS_ON;
	else {
#ifdef notyet
		if (flags & WSDISPLAY_BURN_VBLANK)
			dpms = IFB_REG_DPMS_SUSPEND;
		else
#endif
			dpms = IFB_REG_DPMS_STANDBY;
	}
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, IFB_REG_DPMS_STATE, dpms);
	splx(s);
}

static inline int
ifb_is_console(int node)
{
	extern int fbnode;

	return fbnode == node;
}

int
ifb_mapregs(struct ifb_softc *sc, struct pci_attach_args *pa)
{
	u_int32_t cf;
	int bar, rc;

	cf = pci_conf_read(pa->pa_pc, pa->pa_tag, IFB_PCI_CFG);
	bar = PCI_MAPREG_START + IFB_PCI_CFG_BAR_OFFSET(cf);

	cf = pci_conf_read(pa->pa_pc, pa->pa_tag, bar);
	if (PCI_MAPREG_TYPE(cf) == PCI_MAPREG_TYPE_IO)
		rc = EINVAL;
	else {
		rc = pci_mapreg_map(pa, bar, cf,
		    BUS_SPACE_MAP_LINEAR, NULL, &sc->sc_mem_h,
		    &sc->sc_membase, &sc->sc_memlen, 0);
	}
	if (rc != 0) {
		printf("\n%s: can't map video memory\n",
		    sc->sc_sunfb.sf_dev.dv_xname);
		return rc;
	}

	cf = pci_conf_read(pa->pa_pc, pa->pa_tag, bar + 4);
	if (PCI_MAPREG_TYPE(cf) == PCI_MAPREG_TYPE_IO)
		rc = EINVAL;
	else {
		rc = pci_mapreg_map(pa, bar + 4, cf,
		    0, NULL, &sc->sc_reg_h,
		     &sc->sc_regbase, &sc->sc_reglen, 0x9000);
	}
	if (rc != 0) {
		printf("\n%s: can't map register space\n",
		    sc->sc_sunfb.sf_dev.dv_xname);
		return rc;
	}

	return 0;
}

/*
 * Non accelerated routines.
 */

int
ifb_putchar_dumb(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	ri->ri_bits = (void *)sc->sc_fb8bank0_vaddr;
	sc->sc_old_ops.putchar(cookie, row, col, uc, attr);
	ri->ri_bits = (void *)sc->sc_fb8bank1_vaddr;
	sc->sc_old_ops.putchar(cookie, row, col, uc, attr);

	return 0;
}

int
ifb_copycols_dumb(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	ri->ri_bits = (void *)sc->sc_fb8bank0_vaddr;
	sc->sc_old_ops.copycols(cookie, row, src, dst, num);
	ri->ri_bits = (void *)sc->sc_fb8bank1_vaddr;
	sc->sc_old_ops.copycols(cookie, row, src, dst, num);

	return 0;
}

int
ifb_erasecols_dumb(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	ri->ri_bits = (void *)sc->sc_fb8bank0_vaddr;
	sc->sc_old_ops.erasecols(cookie, row, col, num, attr);
	ri->ri_bits = (void *)sc->sc_fb8bank1_vaddr;
	sc->sc_old_ops.erasecols(cookie, row, col, num, attr);

	return 0;
}

int
ifb_copyrows_dumb(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	ri->ri_bits = (void *)sc->sc_fb8bank0_vaddr;
	sc->sc_old_ops.copyrows(cookie, src, dst, num);
	ri->ri_bits = (void *)sc->sc_fb8bank1_vaddr;
	sc->sc_old_ops.copyrows(cookie, src, dst, num);

	return 0;
}

int
ifb_eraserows_dumb(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	ri->ri_bits = (void *)sc->sc_fb8bank0_vaddr;
	sc->sc_old_ops.eraserows(cookie, row, num, attr);
	ri->ri_bits = (void *)sc->sc_fb8bank1_vaddr;
	sc->sc_old_ops.eraserows(cookie, row, num, attr);

	return 0;
}

/* Similar to rasops_do_cursor(), but using a 7bit pixel mask. */

#define	CURSOR_MASK	0x7f7f7f7f

int
ifb_do_cursor_dumb(struct rasops_info *ri)
{
	struct ifb_softc *sc = ri->ri_hw;
	int full1, height, cnt, slop1, slop2, row, col;
	int ovl_offset = sc->sc_fb8bank1_vaddr - sc->sc_fb8bank0_vaddr;
	u_char *dp0, *dp1, *rp;

	row = ri->ri_crow;
	col = ri->ri_ccol;

	ri->ri_bits = (void *)sc->sc_fb8bank0_vaddr;
	rp = ri->ri_bits + row * ri->ri_yscale + col * ri->ri_xscale;
	height = ri->ri_font->fontheight;
	slop1 = (4 - ((long)rp & 3)) & 3;

	if (slop1 > ri->ri_xscale)
		slop1 = ri->ri_xscale;

	slop2 = (ri->ri_xscale - slop1) & 3;
	full1 = (ri->ri_xscale - slop1 - slop2) >> 2;

	if ((slop1 | slop2) == 0) {
		/* A common case */
		while (height--) {
			dp0 = rp;
			dp1 = dp0 + ovl_offset;
			rp += ri->ri_stride;

			for (cnt = full1; cnt; cnt--) {
				*(int32_t *)dp0 ^= CURSOR_MASK;
				*(int32_t *)dp1 ^= CURSOR_MASK;
				dp0 += 4;
				dp1 += 4;
			}
		}
	} else {
		/* XXX this is stupid.. use masks instead */
		while (height--) {
			dp0 = rp;
			dp1 = dp0 + ovl_offset;
			rp += ri->ri_stride;

			if (slop1 & 1) {
				*dp0++ ^= (u_char)CURSOR_MASK;
				*dp1++ ^= (u_char)CURSOR_MASK;
			}

			if (slop1 & 2) {
				*(int16_t *)dp0 ^= (int16_t)CURSOR_MASK;
				*(int16_t *)dp1 ^= (int16_t)CURSOR_MASK;
				dp0 += 2;
				dp1 += 2;
			}

			for (cnt = full1; cnt; cnt--) {
				*(int32_t *)dp0 ^= CURSOR_MASK;
				*(int32_t *)dp1 ^= CURSOR_MASK;
				dp0 += 4;
				dp1 += 4;
			}

			if (slop2 & 1) {
				*dp0++ ^= (u_char)CURSOR_MASK;
				*dp1++ ^= (u_char)CURSOR_MASK;
			}

			if (slop2 & 2) {
				*(int16_t *)dp0 ^= (int16_t)CURSOR_MASK;
				*(int16_t *)dp1 ^= (int16_t)CURSOR_MASK;
			}
		}
	}

	return 0;
}

/*
 * Accelerated routines.
 */

int
ifb_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	ifb_copyrect(sc, ri->ri_xorigin + src, ri->ri_yorigin + row,
	    ri->ri_xorigin + dst, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight);

	return 0;
}

int
ifb_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;
	int bg, fg;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	row *= ri->ri_font->fontheight;
	col *= ri->ri_font->fontwidth;
	num *= ri->ri_font->fontwidth;

	ifb_fillrect(sc, ri->ri_xorigin + col, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight, ri->ri_devcmap[bg]);

	return 0;
}

int
ifb_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	ifb_copyrect(sc, ri->ri_xorigin, ri->ri_yorigin + src,
	    ri->ri_xorigin, ri->ri_yorigin + dst, ri->ri_emuwidth, num);

	return 0;
}

int
ifb_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;
	int bg, fg;
	int x, y, w;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	if ((num == ri->ri_rows) && ISSET(ri->ri_flg, RI_FULLCLEAR)) {
		num = ri->ri_height;
		x = y = 0;
		w = ri->ri_width;
	} else {
		num *= ri->ri_font->fontheight;
		x = ri->ri_xorigin;
		y = ri->ri_yorigin + row * ri->ri_font->fontheight;
		w = ri->ri_emuwidth;
	}
	ifb_fillrect(sc, x, y, w, num, ri->ri_devcmap[bg]);

	return 0;
}

void
ifb_copyrect(struct ifb_softc *sc, int sx, int sy, int dx, int dy, int w, int h)
{
	ifb_rop(sc, sx, sy, dx, dy, w, h, IFB_ROP_SRC, IFB_PIXELMASK);
	ifb_rop_wait(sc);
}

void
ifb_fillrect(struct ifb_softc *sc, int x, int y, int w, int h, int bg)
{
	int32_t mask;

	/* pixels to set... */
	mask = IFB_PIXELMASK & bg;
	if (mask != 0) {
		ifb_rop(sc, x, y, x, y, w, h, IFB_ROP_SET, mask);
		ifb_rop_wait(sc);
	}

	/* pixels to clear... */
	mask = IFB_PIXELMASK & ~bg;
	if (mask != 0) {
		ifb_rop(sc, x, y, x, y, w, h, IFB_ROP_CLEAR, mask);
		ifb_rop_wait(sc);
	}
}

/*
 * Perform a raster operation on both overlay planes.
 * Puzzled by all the magic numbers in there? So are we. Isn't a dire
 * lack of documentation wonderful?
 */

static inline void
ifb_rop(struct ifb_softc *sc, int sx, int sy, int dx, int dy, int w, int h,
    uint32_t rop, int32_t planemask)
{
	(*sc->sc_rop)(sc, sx, sy, dx, dy, w, h, rop, planemask);
}

void
ifb_rop_common(struct ifb_softc *sc, bus_addr_t reg, int sx, int sy,
    int dx, int dy, int w, int h, uint32_t rop, int32_t planemask)
{
	int dir = 0;

	/*
	 * Compute rop direction. This only really matters for
	 * screen-to-screen copies.
	 */
	if (sy < dy /* && sy + h > dy */) {
		sy += h - 1;
		dy += h;
		dir |= IFB_BLT_DIR_BACKWARDS_Y;
	}
	if (sx < dx /* && sx + w > dx */) {
		sx += w - 1;
		dx += w;
		dir |= IFB_BLT_DIR_BACKWARDS_X;
	}

	/* Which one of those below is your magic number for today? */
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x61000001);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x6301c080);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x80000000);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, rop);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, planemask);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x64000303);
	/*
	 * This value is a pixel offset within the destination area. It is
	 * probably used to define complex polygon shapes, with the
	 * last pixel in the list being back to (0,0).
	 */
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, IFB_COORDS(0, 0));
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x00030000);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x2200010d);

	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x33f01000 | dir);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, IFB_COORDS(dx, dy));
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, IFB_COORDS(w, h));
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, IFB_COORDS(sx, sy));
}

void
ifb_rop_ifb(void *v, int sx, int sy, int dx, int dy, int w, int h,
    uint32_t rop, int32_t planemask)
{
	struct ifb_softc *sc = (struct ifb_softc *)v;
	bus_addr_t reg = IFB_REG_ENGINE;
	
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 2);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 1);
	/* the ``0101'' part is probably a component selection */
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x540101ff);

	ifb_rop_common(sc, reg, sx, sy, dx, dy, w, h, rop, planemask);
}

void
ifb_rop_jfb(void *v, int sx, int sy, int dx, int dy, int w, int h,
    uint32_t rop, int32_t planemask)
{
	struct ifb_softc *sc = (struct ifb_softc *)v;
	bus_addr_t reg = JFB_REG_ENGINE;
	uint32_t spr, splr;

	/*
	 * Pick the current spr and splr values from the communication
	 * area if possible.
	 */
	if (sc->sc_comm != NULL) {
		spr = sc->sc_comm[IFB_SHARED_TERM8_SPR >> 2];
		splr = sc->sc_comm[IFB_SHARED_TERM8_SPLR >> 2];
	} else {
		/* supposedly sane defaults */
		spr = 0x54ff0303;
		splr = 0x5c0000ff;
	}
	
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x00400016);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x5b000002);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x5a000000);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, spr);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, splr);

	ifb_rop_common(sc, reg, sx, sy, dx, dy, w, h, rop, planemask);

	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x5a000001);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h, reg, 0x5b000001);
}

int
ifb_rop_wait(struct ifb_softc *sc)
{
	int i;

	for (i = 1000000; i != 0; i--) {
		if (bus_space_read_4(sc->sc_mem_t, sc->sc_reg_h,
		    IFB_REG_STATUS) & IFB_REG_STATUS_DONE)
			break;
		DELAY(1);
	}

	return i;
}

int
ifb_do_cursor(struct rasops_info *ri)
{
	struct ifb_softc *sc = ri->ri_hw;
	int y, x;

	y = ri->ri_yorigin + ri->ri_crow * ri->ri_font->fontheight;
	x = ri->ri_xorigin + ri->ri_ccol * ri->ri_font->fontwidth;

	ifb_rop(sc, x, y, x, y, ri->ri_font->fontwidth, ri->ri_font->fontheight,
	    IFB_ROP_XOR, IFB_PIXELMASK);
	ifb_rop_wait(sc);

	return 0;
}
