/* $OpenBSD: tga.c,v 1.46 2025/06/28 16:04:10 miod Exp $ */
/* $NetBSD: tga.c,v 1.40 2002/03/13 15:05:18 ad Exp $ */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
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
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/ioctl.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/tgareg.h>
#include <dev/pci/tgavar.h>
#include <dev/ic/bt485reg.h>
#include <dev/ic/bt485var.h>
#include <dev/ic/bt463reg.h>
#include <dev/ic/bt463var.h>
#include <dev/ic/ibm561var.h>

#include <dev/wscons/wsconsio.h>
#include <dev/rasops/rasops.h>
#include <dev/wsfont/wsfont.h>

#if defined(__alpha__) || defined(__mips__)
#include <uvm/uvm_extern.h>
#endif

#ifdef __alpha__
#include <machine/pte.h>
#endif
#ifdef __mips__
#include <mips/pte.h>
#endif

int	tgamatch(struct device *, struct cfdata *, void *);
void	tgaattach(struct device *, struct device *, void *);

struct cfdriver tga_cd = {
	NULL, "tga", DV_DULL
};

const struct cfattach tga_ca = {
	sizeof(struct tga_softc), (cfmatch_t)tgamatch, tgaattach,
};

int	tga_identify(struct tga_devconfig *);
const struct tga_conf *tga_getconf(int);
void	tga_getdevconfig(bus_space_tag_t memt, pci_chipset_tag_t pc,
	    pcitag_t tag, struct tga_devconfig *dc);
unsigned tga_getdotclock(struct tga_devconfig *dc);

struct tga_devconfig tga_console_dc;

int	tga_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	tga_mmap(void *, off_t, int);
int	tga_alloc_screen(void *, const struct wsscreen_descr *,
	    void **, int *, int *, uint32_t *);
void	tga_free_screen(void *, void *);
int	tga_show_screen(void *, void *, int,
			   void (*) (void *, int, int), void *);
int	tga_load_font(void *, void *, struct wsdisplay_font *);
int	tga_list_font(void *, struct wsdisplay_font *);
void	tga_burner(void *, u_int, u_int);

int	tga_copyrows(void *, int, int, int);
int	tga_copycols(void *, int, int, int, int);
int	tga_eraserows(void *, int, int, uint32_t);
int	tga_erasecols(void *, int, int, int, uint32_t);
int	tga_putchar(void *c, int row, int col, u_int uc, uint32_t attr);

int	tga_rop(struct rasops_info *, int, int, int, int,
	struct rasops_info *, int, int);
int	tga_rop_vtov(struct rasops_info *, int, int, int,
	int, struct rasops_info *, int, int );
void	tga2_init(struct tga_devconfig *);

void	tga_config_interrupts(struct device *);

/* RAMDAC interface functions */
int	 tga_sched_update(void *, void (*)(void *));
void	 tga_ramdac_wr(void *, u_int, u_int8_t);
u_int8_t tga_ramdac_rd(void *, u_int);
void	 tga_bt463_wr(void *, u_int, u_int8_t);
u_int8_t tga_bt463_rd(void *, u_int);
void	 tga2_ramdac_wr(void *, u_int, u_int8_t);
u_int8_t tga2_ramdac_rd(void *, u_int);

/* Interrupt handler */
int	tga_intr(void *);

/* The NULL entries will get filled in by rasops_init().
 * XXX and the non-NULL ones will be overwritten; reset after calling it.
 */
struct wsdisplay_emulops tga_emulops = {
	NULL,
	NULL,
	tga_putchar,
	tga_copycols,
	tga_erasecols,
	tga_copyrows,
	tga_eraserows,
	NULL,
	NULL
};

struct wsscreen_descr tga_stdscreen = {
	"std",
	0, 0,	/* will be filled in -- XXX shouldn't, it's global */
	&tga_emulops,
	0, 0,
	WSSCREEN_UNDERLINE | WSSCREEN_HILIT |
	    WSSCREEN_WSCOLORS | WSSCREEN_REVERSE
};

const struct wsscreen_descr *_tga_scrlist[] = {
	&tga_stdscreen,
	/* XXX other formats, graphics screen? */
};

struct wsscreen_list tga_screenlist = {
	sizeof(_tga_scrlist) / sizeof(struct wsscreen_descr *), _tga_scrlist
};

struct wsdisplay_accessops tga_accessops = {
	.ioctl = tga_ioctl,
	.mmap = tga_mmap,
	.alloc_screen = tga_alloc_screen,
	.free_screen = tga_free_screen,
	.show_screen = tga_show_screen,
	.load_font = tga_load_font,
	.list_font = tga_list_font,
	.burn_screen = tga_burner
};

void	tga_blank(struct tga_devconfig *);
void	tga_unblank(struct tga_devconfig *);

#ifdef TGA_DEBUG
#define DPRINTF(...)      printf (__VA_ARGS__)
#define DPRINTFN(n, ...)   if (tgadebug > (n)) printf (__VA_ARGS__)
int tgadebug = 0;
#else
#define DPRINTF(...)
#define DPRINTFN(n,...)
#endif

const struct pci_matchid tga_devices[] = {
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21030 },
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_PBXGB },
};

int
tgamatch(struct device *parent, struct cfdata *match, void *aux)
{
	if (pci_matchbyid((struct pci_attach_args *)aux, tga_devices,
	    sizeof(tga_devices) / sizeof(tga_devices[0])))
		return (10);	/* need to return more than vga_pci here! */

	return (0);
}

void
tga_getdevconfig(bus_space_tag_t memt, pci_chipset_tag_t pc, pcitag_t tag,
    struct tga_devconfig *dc)
{
	const struct tga_conf *tgac;
	struct rasops_info *rip;
	int cookie;
	bus_size_t pcisize;
	int i;

	dc->dc_memt = memt;

	dc->dc_pcitag = tag;

	DPRINTF("tga_getdevconfig: Getting map info\n");
	/* XXX magic number */
	if (pci_mapreg_info(pc, tag, 0x10,
	    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT,
	    &dc->dc_pcipaddr, &pcisize, NULL))
		return;

	DPRINTF("tga_getdevconfig: preparing to map\n");
	if (bus_space_map(memt, dc->dc_pcipaddr, pcisize,
	    BUS_SPACE_MAP_PREFETCHABLE | BUS_SPACE_MAP_LINEAR, &dc->dc_memh))
		return;
#ifdef __OpenBSD__
	dc->dc_vaddr = dc->dc_memh;
#else
	dc->dc_vaddr = (vaddr_t) bus_space_vaddr(memt, dc->dc_memh);
#endif
	DPRINTF("tga_getdevconfig: mapped\n");

#ifdef __alpha__
	dc->dc_paddr = ALPHA_K0SEG_TO_PHYS(dc->dc_vaddr);	/* XXX */
#endif
	DPRINTF("tga_getdevconfig: allocating subregion\n");
	bus_space_subregion(dc->dc_memt, dc->dc_memh, 
			    TGA_MEM_CREGS, TGA_CREGS_SIZE,
			    &dc->dc_regs);

	DPRINTF("tga_getdevconfig: going to identify\n");
	dc->dc_tga_type = tga_identify(dc);

	DPRINTF("tga_getdevconfig: preparing to get config\n");
	tgac = dc->dc_tgaconf = tga_getconf(dc->dc_tga_type);
	if (tgac == NULL)
		return;

#if 0
	/* XXX on the Alpha, pcisize = 4 * cspace_size. */
	if (tgac->tgac_cspace_size != pcisize)			/* sanity */
		panic("tga_getdevconfig: memory size mismatch?");
#endif

	DPRINTF("tga_getdevconfig: get revno\n");
	switch (TGARREG(dc, TGA_REG_GREV) & 0xff) {
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x04:
		dc->dc_tga2 = 0;
		break;
	case 0x20:
	case 0x21:
	case 0x22:
		dc->dc_tga2 = 1;
		break;
	default:
		panic("tga_getdevconfig: TGA Revision not recognized");
	}

	if (dc->dc_tga2) {
		tga2_init(dc);
	}
	
	i = TGARREG(dc, TGA_REG_VHCR) & 0x1ff;
	DPRINTF("tga_getdevconfig: TGA_REG_VHCR & 0x1ff = %d\n", i);
	switch (i) {		/* XXX */
	case 0:
		dc->dc_wid = 8192;
		break;

	case 1:
		dc->dc_wid = 8196;
		break;

	default:
		dc->dc_wid = (TGARREG(dc, TGA_REG_VHCR) & 0x1ff) * 4; /* XXX */
		break;
	}

	DPRINTF("tga_getdevconfig: dc->dc_wid = %d\n", dc->dc_wid);
	/*
	 * XXX XXX Turning off "odd" shouldn't be necessary,
	 * XXX XXX but I can't make X work with the weird size.
	 */
	DPRINTF("tga_getdevconfig: beginning magic incantation\n");
	if ((TGARREG(dc, TGA_REG_VHCR) & 0x00000001) != 0 &&	/* XXX */
	    (TGARREG(dc, TGA_REG_VHCR) & 0x80000000) != 0) {	/* XXX */
		TGAWREG(dc, TGA_REG_VHCR,
		    (TGARREG(dc, TGA_REG_VHCR) & ~0x80000001));
		dc->dc_wid -= 4;
	}

	dc->dc_rowbytes = dc->dc_wid * (dc->dc_tgaconf->tgac_phys_depth / 8);
	dc->dc_ht = (TGARREG(dc, TGA_REG_VVCR) & 0x7ff);	/* XXX */
	DPRINTF("tga_getdevconfig: rowbytes = %d, tgac_phys_depth = %d\n"
		"                  dc_wid = %d, dc_ht = %d\n",
		dc->dc_rowbytes, dc->dc_tgaconf->tgac_phys_depth,
		dc->dc_wid, dc->dc_ht);

	/* XXX this seems to be what DEC does */
	DPRINTF("tga_getdevconfig: more magic\n");
	TGAWREG(dc, TGA_REG_CCBR, 0);
	TGAWREG(dc, TGA_REG_VVBR, 1);
	dc->dc_videobase = dc->dc_vaddr + tgac->tgac_dbuf[0] +
	    1 * tgac->tgac_vvbr_units;
	dc->dc_blanked = 1;
	tga_unblank(dc);
	
	DPRINTF("tga_getdevconfig: dc_videobase = 0x%016llx\n"
		"                  dc_vaddr = 0x%016llx\n"
		"                  tgac_dbuf[0] = %d\n"
		"                  tgac_vvbr_units = %d\n",
		dc->dc_videobase, dc->dc_vaddr, tgac->tgac_dbuf[0],
		tgac->tgac_vvbr_units);
	       
	/*
	 * Set all bits in the pixel mask, to enable writes to all pixels.
	 * It seems that the console firmware clears some of them
	 * under some circumstances, which causes cute vertical stripes.
	 */
	DPRINTF("tga_getdevconfig: set pixel mask\n");
	TGAWREG(dc, TGA_REG_GPXR_P, 0xffffffff);

	/* clear the screen */
	DPRINTF("tga_getdevconfig: clear screen\n");
	for (i = 0; i < dc->dc_ht * dc->dc_rowbytes; i += sizeof(u_int32_t))
		*(u_int32_t *)(dc->dc_videobase + i) = 0;

	DPRINTF("tga_getdevconfig: raster ops\n");
	/* Initialize rasops descriptor */
	rip = &dc->dc_rinfo;
	rip->ri_flg = RI_CENTER;
	rip->ri_depth = tgac->tgac_phys_depth;
	rip->ri_bits = (void *)dc->dc_videobase;
	rip->ri_width = dc->dc_wid;
	rip->ri_height = dc->dc_ht;
	rip->ri_stride = dc->dc_rowbytes;
	rip->ri_hw = dc;

	if (tgac->tgac_phys_depth == 32) {
		rip->ri_rnum = 8;
		rip->ri_gnum = 8;
		rip->ri_bnum = 8;
		rip->ri_rpos = 16;
		rip->ri_gpos = 8;
		rip->ri_bpos = 0;
	}

	DPRINTF("tga_getdevconfig: wsfont_init\n");
	wsfont_init();
	if (rip->ri_width > 80*12) 
		/* High res screen, choose a big font */
		cookie = wsfont_find(NULL, 12, 0, 0);
	else 
		/*  lower res, choose a 8 pixel wide font */
		cookie = wsfont_find(NULL, 8, 0, 0);
	if (cookie <= 0)
		cookie = wsfont_find(NULL, 0, 0, 0);
	if (cookie <= 0) {
		printf("tga: no appropriate fonts.\n");
		return;
	}

	/* the accelerated tga_putchar() needs LSbit left */
	if (wsfont_lock(cookie, &rip->ri_font,
	    WSDISPLAY_FONTORDER_R2L, WSDISPLAY_FONTORDER_L2R) <= 0) {
		printf("tga: couldn't lock font\n");
		return;
	}
	rip->ri_wsfcookie = cookie;
	/* fill screen size */
	rasops_init(rip, rip->ri_height / rip->ri_font->fontheight,
	    rip->ri_width / rip->ri_font->fontwidth); 
	
	/* add our accelerated functions */
	/* XXX shouldn't have to do this; rasops should leave non-NULL 
	 * XXX entries alone.
	 */
	rip->ri_ops.copyrows = tga_copyrows;
	rip->ri_ops.eraserows = tga_eraserows;
	rip->ri_ops.erasecols = tga_erasecols;
	rip->ri_ops.copycols = tga_copycols;
	rip->ri_ops.putchar = tga_putchar;	

	tga_stdscreen.nrows = rip->ri_rows;
	tga_stdscreen.ncols = rip->ri_cols;
	tga_stdscreen.textops = &rip->ri_ops;
	tga_stdscreen.capabilities = rip->ri_caps;

	dc->dc_intrenabled = 0;
}

void
tgaattach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct tga_softc *sc = (struct tga_softc *)self;
	struct wsemuldisplaydev_attach_args aa;
	pci_intr_handle_t intrh;
	const char *intrstr;
	u_int8_t rev;
	int console;

#if defined(__alpha__)
	console = (pa->pa_tag == tga_console_dc.dc_pcitag);
#else
	console = 0;
#endif
	if (console) {
		sc->sc_dc = &tga_console_dc;
		sc->nscreens = 1;
	} else {
		sc->sc_dc = malloc(sizeof(struct tga_devconfig), M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (sc->sc_dc == NULL)
			return;
		tga_getdevconfig(pa->pa_memt, pa->pa_pc, pa->pa_tag,
		    sc->sc_dc);
	}
	if (sc->sc_dc->dc_vaddr == 0) {
		printf(": can't map mem space\n");
		return;
	}

	/* XXX say what's going on. */
	intrstr = NULL;
	if (pci_intr_map(pa, &intrh)) {
		printf(": can't map interrupt");
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, intrh);
	sc->sc_intr = pci_intr_establish(pa->pa_pc, intrh, IPL_TTY, tga_intr,
	    sc->sc_dc, sc->sc_dev.dv_xname);
	if (sc->sc_intr == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf("at %s", intrstr);
		printf("\n");
		return;
	}

	rev = PCI_REVISION(pa->pa_class);
	switch (rev) {
	case 0x1:
	case 0x2:
	case 0x3:
		printf(": DC21030 step %c", 'A' + rev - 1);
		break;
	case 0x20:
		printf(": TGA2 abstract software model");
		break;
	case 0x21:
	case 0x22:
		printf(": TGA2 pass %d", rev - 0x20);
		break;

	default:
		printf("unknown stepping (0x%x)", rev);
		break;
	}
	printf(", ");

	/*
	 * Get RAMDAC function vectors and call the RAMDAC functions
	 * to allocate its private storage and pass that back to us.
	 */

	DPRINTF("tgaattach: Get RAMDAC functions\n");
	sc->sc_dc->dc_ramdac_funcs = sc->sc_dc->dc_tgaconf->ramdac_funcs();
	if (!sc->sc_dc->dc_tga2) {
	    DPRINTF("tgaattach: !sc->sc_dc->dc_tga2\n");
	    DPRINTF("tgaattach: sc->sc_dc->dc_tgaconf->ramdac_funcs %s "
		    "bt485_funcs\n",
		    (sc->sc_dc->dc_tgaconf->ramdac_funcs == bt485_funcs)
		    ? "==" : "!=");
	    if (sc->sc_dc->dc_tgaconf->ramdac_funcs == bt485_funcs) 
		  sc->sc_dc->dc_ramdac_cookie = 
			sc->sc_dc->dc_ramdac_funcs->ramdac_register(sc->sc_dc,
		    tga_sched_update, tga_ramdac_wr, tga_ramdac_rd);
		else
		  sc->sc_dc->dc_ramdac_cookie = 
			sc->sc_dc->dc_ramdac_funcs->ramdac_register(sc->sc_dc,
		    tga_sched_update, tga_bt463_wr, tga_bt463_rd);
	} else {
	        DPRINTF("tgaattach: sc->sc_dc->dc_tga2\n");
		sc->sc_dc->dc_ramdac_cookie = 
			sc->sc_dc->dc_ramdac_funcs->ramdac_register(sc->sc_dc, 
			tga_sched_update, tga2_ramdac_wr, tga2_ramdac_rd);

		/* XXX this is a bit of a hack, setting the dotclock here */
		if (sc->sc_dc->dc_tgaconf->ramdac_funcs != bt485_funcs)
			(*sc->sc_dc->dc_ramdac_funcs->ramdac_set_dotclock)
				(sc->sc_dc->dc_ramdac_cookie,
				 tga_getdotclock(sc->sc_dc));
	}
	DPRINTF("tgaattach: sc->sc_dc->dc_ramdac_cookie = 0x%016llx\n",
		sc->sc_dc->dc_ramdac_cookie);
	/*
	 * Initialize the RAMDAC.  Initialization includes disabling
	 * cursor, setting a sane colormap, etc.
	 */
	DPRINTF("tgaattach: Initializing RAMDAC.\n");
	(*sc->sc_dc->dc_ramdac_funcs->ramdac_init)(sc->sc_dc->dc_ramdac_cookie);
	TGAWREG(sc->sc_dc, TGA_REG_SISR, 0x00000001); /* XXX */

	if (sc->sc_dc->dc_tgaconf == NULL) {
		printf("unknown board configuration\n");
		return;
	}
	printf("board type %s\n", sc->sc_dc->dc_tgaconf->tgac_name);
	printf("%s: %d x %d, %dbpp, %s RAMDAC\n", sc->sc_dev.dv_xname,
	    sc->sc_dc->dc_wid, sc->sc_dc->dc_ht,
	    sc->sc_dc->dc_tgaconf->tgac_phys_depth,
	    sc->sc_dc->dc_ramdac_funcs->ramdac_name);

	if (intrstr != NULL)
		printf("%s: interrupting at %s\n", sc->sc_dev.dv_xname,
		    intrstr);

	aa.console = console;
	aa.scrdata = &tga_screenlist;
	aa.accessops = &tga_accessops;
	aa.accesscookie = sc;
	aa.defaultscreens = 0;

	config_found(self, &aa, wsemuldisplaydevprint);

#ifdef __NetBSD__
	config_interrupts(self, tga_config_interrupts);
#else
	tga_config_interrupts(self);
#endif
}

void 
tga_config_interrupts(struct device *d)
{
	struct tga_softc *sc = (struct tga_softc *)d;
	sc->sc_dc->dc_intrenabled = 1;
}
	

int
tga_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct tga_softc *sc = v;
	struct tga_devconfig *dc = sc->sc_dc;
	struct ramdac_funcs *dcrf = dc->dc_ramdac_funcs;
	struct ramdac_cookie *dcrc = dc->dc_ramdac_cookie;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_TGA;
		break;

	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		switch (sc->sc_mode) {
		case WSDISPLAYIO_MODE_DUMBFB:
			/* in dump fb mode start the framebuffer at 0 */
			TGAWREG(dc, TGA_REG_VVBR, 0);
			break;
		default:
			/* XXX it this useful, except for not breaking Xtga? */
			TGAWREG(dc, TGA_REG_VVBR, 1);
			break;			
		}
		break;

	case WSDISPLAYIO_GINFO:
#define	wsd_fbip ((struct wsdisplay_fbinfo *)data)
		wsd_fbip->height = sc->sc_dc->dc_ht;
		wsd_fbip->width = sc->sc_dc->dc_wid;
		wsd_fbip->depth = sc->sc_dc->dc_tgaconf->tgac_phys_depth;
		wsd_fbip->stride = sc->sc_dc->dc_rowbytes;
		wsd_fbip->offset = 0;
		wsd_fbip->cmsize = 1024;		/* XXX ??? */
#undef wsd_fbip
		break;

	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_dc->dc_rowbytes;
		break;

	case WSDISPLAYIO_GETCMAP:
		return (*dcrf->ramdac_get_cmap)(dcrc,
		    (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		return (*dcrf->ramdac_set_cmap)(dcrc,
		    (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	case WSDISPLAYIO_GCURPOS:
		return (*dcrf->ramdac_get_curpos)(dcrc,
		    (struct wsdisplay_curpos *)data);

	case WSDISPLAYIO_SCURPOS:
		return (*dcrf->ramdac_set_curpos)(dcrc,
		    (struct wsdisplay_curpos *)data);

	case WSDISPLAYIO_GCURMAX:
		return (*dcrf->ramdac_get_curmax)(dcrc,
		    (struct wsdisplay_curpos *)data);

	case WSDISPLAYIO_GCURSOR:
		return (*dcrf->ramdac_get_cursor)(dcrc,
		    (struct wsdisplay_cursor *)data);

	case WSDISPLAYIO_SCURSOR:
		return (*dcrf->ramdac_set_cursor)(dcrc,
		    (struct wsdisplay_cursor *)data);

	default:
		return (-1);
	}

	return (0);
}

int
tga_sched_update(void *v, void (*f)(void *))
{
	struct tga_devconfig *dc = v;

	if (dc->dc_intrenabled) {
		/* Arrange for f to be called at the next end-of-frame interrupt */
		dc->dc_ramdac_intr = f;
		TGAWREG(dc, TGA_REG_SISR, 0x00010000);
	} else {
		/* Spin until the end-of-frame, then call f */
		TGAWREG(dc, TGA_REG_SISR, 0x00010001);
		TGAREGWB(dc, TGA_REG_SISR, 1);
		while ((TGARREG(dc, TGA_REG_SISR) & 0x00000001) == 0)
			;
		f(dc->dc_ramdac_cookie);
		TGAWREG(dc, TGA_REG_SISR, 0x00000001);
		TGAREGWB(dc, TGA_REG_SISR, 1);
	}
		
	return 0;
}

int
tga_intr(void *v)
{
	struct tga_devconfig *dc = v;
	struct ramdac_cookie *dcrc= dc->dc_ramdac_cookie;

	u_int32_t reg;

	reg = TGARREG(dc, TGA_REG_SISR);
	if (( reg & 0x00010001) != 0x00010001) {
		/* Odd. We never set any of the other interrupt enables. */
		if ((reg & 0x1f) != 0) {
			/* Clear the mysterious pending interrupts. */
			TGAWREG(dc, TGA_REG_SISR, (reg & 0x1f));
			TGAREGWB(dc, TGA_REG_SISR, 1);
			/* This was our interrupt, even if we're puzzled as to why
			 * we got it.  Don't make the interrupt handler think it
			 * was a stray.  
			 */
			return -1;
		} else {
			return 0;
		}
	}
	/* if we have something to do, do it */
	if (dc->dc_ramdac_intr) {
		dc->dc_ramdac_intr(dcrc);
		dc->dc_ramdac_intr = NULL;
	}
	TGAWREG(dc, TGA_REG_SISR, 0x00000001);
	TGAREGWB(dc, TGA_REG_SISR, 1);
	return (1);
}

paddr_t
tga_mmap(void *v, off_t offset, int prot)
{
	struct tga_softc *sc = v;
	struct tga_devconfig *dc = sc->sc_dc;

	if (offset >= dc->dc_tgaconf->tgac_cspace_size || offset < 0)
		return -1;

	if (sc->sc_mode == WSDISPLAYIO_MODE_DUMBFB) {
		/* 
		 * The framebuffer starts at the upper half of tga mem
		 */
		offset += dc->dc_tgaconf->tgac_cspace_size / 2;
	}
#if defined(__alpha__) || defined(__mips__)
	return (sc->sc_dc->dc_paddr + offset);
#else
	return (-1);
#endif
}

int
tga_alloc_screen(void *v, const struct wsscreen_descr *type, void **cookiep,
    int *curxp, int *curyp, uint32_t *attrp)
{
	struct tga_softc *sc = v;
	uint32_t defattr;

	if (sc->nscreens > 0)
		return (ENOMEM);

	*cookiep = &sc->sc_dc->dc_rinfo; /* one and only for now */
	*curxp = 0;
	*curyp = 0;
	sc->sc_dc->dc_rinfo.ri_ops.pack_attr(&sc->sc_dc->dc_rinfo, 
		0, 0, 0, &defattr);
	*attrp = defattr;
	sc->nscreens++;
	return (0);
}

void
tga_free_screen(void *v, void *cookie)
{
	struct tga_softc *sc = v;

	if (sc->sc_dc == &tga_console_dc)
		panic("tga_free_screen: console");

	sc->nscreens--;
}

int
tga_show_screen(void *v, void *cookie, int waitok, void (*cb)(void *, int, int),
    void *cbarg)
{

	return (0);
}

int
tga_load_font(void *v, void *emulcookie, struct wsdisplay_font *font)
{
	struct tga_softc *sc = v;
	struct tga_devconfig *dc = sc->sc_dc;
	struct rasops_info *ri = &dc->dc_rinfo;
	int wsfcookie;
	struct wsdisplay_font *wsf;
	const char *name;

	/*
	 * We can't use rasops_load_font() directly, as we need to make
	 * sure that, when switching fonts, the font bits are set up in
	 * the correct bit order.
	 */

	if (font->data != NULL)
		return rasops_load_font(ri, emulcookie, font);

	/* allow an empty font name to revert to the initial font choice */
	name = font->name;
	if (*name == '\0')
		name = NULL;

	wsfcookie = wsfont_find(name, ri->ri_font->fontwidth,
	    ri->ri_font->fontheight, 0);
	if (wsfcookie < 0) {
		wsfcookie = wsfont_find(name, 0, 0, 0);
		if (wsfcookie < 0)
			return ENOENT;
		else
			return EINVAL;
	}
	if (wsfont_lock(wsfcookie, &wsf,
	    WSDISPLAY_FONTORDER_R2L, WSDISPLAY_FONTORDER_L2R) <= 0)
		return EINVAL;

	/* if (ri->ri_wsfcookie >= 0) */
		wsfont_unlock(ri->ri_wsfcookie);
	ri->ri_wsfcookie = wsfcookie;
	ri->ri_font = wsf;
	ri->ri_fontscale = ri->ri_font->fontheight * ri->ri_font->stride;

	return 0;
}

int
tga_list_font(void *v, struct wsdisplay_font *font)
{
	struct tga_softc *sc = v;
	struct tga_devconfig *dc = sc->sc_dc;
	struct rasops_info *ri = &dc->dc_rinfo;

	return rasops_list_font(ri, font);
}

int
tga_cnattach(bus_space_tag_t iot, bus_space_tag_t memt, pci_chipset_tag_t pc,
    int bus, int device, int function)
{
	struct tga_devconfig *dcp = &tga_console_dc;
	uint32_t defattr;

	tga_getdevconfig(memt, pc,
	    pci_make_tag(pc, bus, device, function), dcp);

	/* sanity checks */
	if (dcp->dc_vaddr == 0)
		panic("tga_console(%d, %d): can't map mem space",
		    device, function);
	if (dcp->dc_tgaconf == NULL)
		panic("tga_console(%d, %d): unknown board configuration",
		    device, function);

	/*
	 * Initialize the RAMDAC but DO NOT allocate any private storage.
	 * Initialization includes disabling cursor, setting a sane
	 * colormap, etc.  It will be reinitialized in tgaattach().
	 */
	if (dcp->dc_tga2) {
		if (dcp->dc_tgaconf->ramdac_funcs == bt485_funcs)
			bt485_cninit(dcp, tga_sched_update, tga2_ramdac_wr,
				     tga2_ramdac_rd);
		else
			ibm561_cninit(dcp, tga_sched_update, tga2_ramdac_wr,
				      tga2_ramdac_rd, tga_getdotclock(dcp));
	} else {
		if (dcp->dc_tgaconf->ramdac_funcs == bt485_funcs)
			bt485_cninit(dcp, tga_sched_update, tga_ramdac_wr,
				tga_ramdac_rd);
		else {
			bt463_cninit(dcp, tga_sched_update, tga_bt463_wr,
				tga_bt463_rd);
		}
	}
	dcp->dc_rinfo.ri_ops.pack_attr(&dcp->dc_rinfo, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&tga_stdscreen, &dcp->dc_rinfo, 0, 0, defattr);
	
	return(0);
}

/*
 * Functions to blank and unblank the display.
 */
void
tga_burner(void *v, u_int on, u_int flags)
{
	struct tga_softc *sc = v;

	if (on) {
		tga_unblank(sc->sc_dc);
	} else {
		tga_blank(sc->sc_dc);
	}
}

void
tga_blank(struct tga_devconfig *dc)
{

	if (!dc->dc_blanked) {
		dc->dc_blanked = 1;
		/* XXX */
		TGAWREG(dc, TGA_REG_VVVR, TGARREG(dc, TGA_REG_VVVR) | VVR_BLANK);
	}
}

void
tga_unblank(struct tga_devconfig *dc)
{

	if (dc->dc_blanked) {
		dc->dc_blanked = 0;
		/* XXX */
		TGAWREG(dc, TGA_REG_VVVR, TGARREG(dc, TGA_REG_VVVR) & ~VVR_BLANK);
	}
}

/*
 * Functions to manipulate the built-in cursor handing hardware.
 */
int
tga_builtin_set_cursor(struct tga_devconfig *dc,
    struct wsdisplay_cursor *cursorp)
{
	struct ramdac_funcs *dcrf = dc->dc_ramdac_funcs;
	struct ramdac_cookie *dcrc = dc->dc_ramdac_cookie;
	u_int count, v;
	int error;

	v = cursorp->which;
	if (v & WSDISPLAY_CURSOR_DOCMAP) {
		error = dcrf->ramdac_check_curcmap(dcrc, cursorp);
		if (error)
			return (error);
	}
	if (v & WSDISPLAY_CURSOR_DOSHAPE) {
		if ((u_int)cursorp->size.x != 64 ||
		    (u_int)cursorp->size.y > 64)
			return (EINVAL);
	}
	if (v & WSDISPLAY_CURSOR_DOHOT)		/* not supported */
		return EINVAL;

	/* parameters are OK; do it */
	if (v & WSDISPLAY_CURSOR_DOCUR) {
		if (cursorp->enable)
			/* XXX */
			TGAWREG(dc, TGA_REG_VVVR, TGARREG(dc, TGA_REG_VVVR) | 0x04);
		else
			/* XXX */
			TGAWREG(dc, TGA_REG_VVVR, TGARREG(dc, TGA_REG_VVVR) & ~0x04);
	}
	if (v & WSDISPLAY_CURSOR_DOPOS) {
		TGAWREG(dc, TGA_REG_CXYR, 
		    ((cursorp->pos.y & 0xfff) << 12) | (cursorp->pos.x & 0xfff));
	}
	if (v & WSDISPLAY_CURSOR_DOCMAP) {
		/* can't fail. */
		dcrf->ramdac_set_curcmap(dcrc, cursorp);
	}
	if (v & WSDISPLAY_CURSOR_DOSHAPE) {
		/* The cursor is 2 bits deep, and there is no mask */
		count = (cursorp->size.y * 64 * 2) / NBBY;
		TGAWREG(dc, TGA_REG_CCBR,
		    (TGARREG(dc, TGA_REG_CCBR) & ~0xfc00) | (cursorp->size.y << 10));
		if ((error = copyin(cursorp->image,(char *)(dc->dc_vaddr +
		    (TGARREG(dc, TGA_REG_CCBR) & 0x3ff)), count)) != 0)
			return (error);
	}
	return (0);
}

int
tga_builtin_get_cursor(struct tga_devconfig *dc,
    struct wsdisplay_cursor *cursorp)
{
	struct ramdac_funcs *dcrf = dc->dc_ramdac_funcs;
	struct ramdac_cookie *dcrc = dc->dc_ramdac_cookie;
	int error;
	u_int count;

	cursorp->which = WSDISPLAY_CURSOR_DOALL &
	    ~(WSDISPLAY_CURSOR_DOHOT | WSDISPLAY_CURSOR_DOCMAP);
	cursorp->enable = (TGARREG(dc, TGA_REG_VVVR) & 0x04) != 0;
	cursorp->pos.x = TGARREG(dc, TGA_REG_CXYR) & 0xfff;
	cursorp->pos.y = (TGARREG(dc, TGA_REG_CXYR) >> 12) & 0xfff;
	cursorp->size.x = 64;
	cursorp->size.y = (TGARREG(dc, TGA_REG_CCBR) >> 10) & 0x3f;

	if (cursorp->image != NULL) {
		count = (cursorp->size.y * 64 * 2) / NBBY;
		error = copyout((char *)(dc->dc_vaddr +
		      (TGARREG(dc, TGA_REG_CCBR) & 0x3ff)),
		    cursorp->image, count);
		if (error)
			return (error);
		/* No mask */
	}
	error = dcrf->ramdac_get_curcmap(dcrc, cursorp);
	return (error);
}

int
tga_builtin_set_curpos(struct tga_devconfig *dc,
    struct wsdisplay_curpos *curposp)
{

	TGAWREG(dc, TGA_REG_CXYR,
	    ((curposp->y & 0xfff) << 12) | (curposp->x & 0xfff));
	return (0);
}

int
tga_builtin_get_curpos(struct tga_devconfig *dc,
    struct wsdisplay_curpos *curposp)
{

	curposp->x = TGARREG(dc, TGA_REG_CXYR) & 0xfff;
	curposp->y = (TGARREG(dc, TGA_REG_CXYR) >> 12) & 0xfff;
	return (0);
}

int
tga_builtin_get_curmax(struct tga_devconfig *dc,
    struct wsdisplay_curpos *curposp)
{

	curposp->x = curposp->y = 64;
	return (0);
}

/*
 * Copy columns (characters) in a row (line).
 */
int
tga_copycols(void *id, int row, int srccol, int dstcol, int ncols)
{
	struct rasops_info *ri = id;
	int y, srcx, dstx, nx;

	y = ri->ri_font->fontheight * row;
	srcx = ri->ri_font->fontwidth * srccol;
	dstx = ri->ri_font->fontwidth * dstcol;
	nx = ri->ri_font->fontwidth * ncols;

	tga_rop(ri, dstx, y, nx, ri->ri_font->fontheight, ri, srcx, y);

	return 0;
}

/*
 * Copy rows (lines).
 */
int
tga_copyrows(void *id, int srcrow, int dstrow, int nrows)
{
	struct rasops_info *ri = id;
	int srcy, dsty, ny;

	srcy = ri->ri_font->fontheight * srcrow;
	dsty = ri->ri_font->fontheight * dstrow;
	ny = ri->ri_font->fontheight * nrows;

	tga_rop(ri, 0, dsty, ri->ri_emuwidth, ny, ri, 0, srcy);

	return 0;
}

/*
 *  Generic TGA raster op.
 *   This covers all possible raster ops, and
 *   clips the sizes and all of that.
 */
int
tga_rop(struct rasops_info *dst, int dx, int dy, int w, int h,
    struct rasops_info *src, int sx, int sy)
{
	if (dst == NULL || src == NULL)
		return -1;

	/* Clip against src */
	if (sx < 0) {
		w += sx;
		sx = 0;
	}
	if (sy < 0) {
		h += sy;
		sy = 0;
	}
	if (sx + w > src->ri_emuwidth)
		w = src->ri_emuwidth - sx;
	if (sy + h > src->ri_emuheight)
		h = src->ri_emuheight - sy;

	/* Clip against dst.  We modify src regardless of using it,
	 * since it really doesn't matter.
	 */
	if (dx < 0) {
		w += dx;
		sx -= dx;
		dx = 0;
	}
	if (dy < 0) {
		h += dy;
		sy -= dy;
		dy = 0;
	}
	if (dx + w > dst->ri_emuwidth)
		w = dst->ri_emuwidth - dx;
	if (dy + h > dst->ri_emuheight)
		h = dst->ri_emuheight - dy;
	if (w <= 0 || h <= 0)
		return 0;	/* Vacuously true; */

	return tga_rop_vtov(dst, dx, dy, w, h, src, sx, sy);
}



/*
 * Video to Video raster ops.
 * This function deals with all raster ops that have a src and dst
 * that are on the card.
 */
int
tga_rop_vtov(struct rasops_info *dst, int dx, int dy, int w, int h,
    struct rasops_info *src, int sx, int sy)
{
	struct tga_devconfig *dc = (struct tga_devconfig *)dst->ri_hw;
	int srcb, dstb, tga_srcb, tga_dstb;
	int x, y, wb;
	int xstart, xend, xdir;
	int ystart, yend, ydir, yinc;
	int xleft, lastx, lastleft;
	int offset = 1 * dc->dc_tgaconf->tgac_vvbr_units;

	/*
	 * I don't yet want to deal with unaligned guys, really.  And we don't
	 * deal with copies from one card to another.
	 */
	if (dx % 8 != 0 || sx % 8 != 0 || src != dst) {
		/* XXX Punt! */
		/* XXX should never happen, since it's only being used to
		 * XXX copy 8-pixel-wide characters.
		 */
		return -1;
	}

        wb = w * (dst->ri_depth / 8);
	if (sy >= dy) {
		ystart = 0;
		yend = h;
		ydir = 1;
	} else {
		ystart = h;
		yend = 0;
		ydir = -1;
	}
	if (sx >= dx) {      /* moving to the left */
		xstart = 0;
		xend = w * (dst->ri_depth / 8) - 4;
		xdir = 1;
	} else {             /* moving to the right */
		xstart = wb - ( wb >= 4*64 ? 4*64 : wb >= 64 ? 64 : 4 );
		xend = 0;
		xdir = -1;
	}
#define XINC4   4
#define XINC64  64
#define XINC256 (64*4)
	yinc = ydir * dst->ri_stride;
	ystart *= dst->ri_stride;
	yend *= dst->ri_stride;

	srcb = sy * src->ri_stride + sx * (src->ri_depth/8);
	dstb = dy * dst->ri_stride + dx * (dst->ri_depth/8);
	tga_srcb = offset + (sy + src->ri_yorigin) * src->ri_stride + 
		(sx + src->ri_xorigin) * (src->ri_depth/8);
	tga_dstb = offset + (dy + dst->ri_yorigin) * dst->ri_stride + 
		(dx + dst->ri_xorigin) * (dst->ri_depth/8);

	TGAWALREG(dc, TGA_REG_GMOR, 3, 0x0007); /* Copy mode */
	TGAWALREG(dc, TGA_REG_GOPR, 3, 0x0003); /* SRC */

	/*
	 * we have 3 sizes of pixels to move in X direction:
	 * 4 * 64   (unrolled TGA ops)
	 *     64   (single TGA op)
	 *      4   (CPU, using long word)
	 */

	if (xdir == 1) {   /* move to the left */

		for (y = ystart; (ydir * y) <= (ydir * yend); y += yinc) {

			/* 4*64 byte chunks */
			for (xleft = wb, x = xstart;
			     x <= xend && xleft >= 4*64;
			     x += XINC256, xleft -= XINC256) {

				/* XXX XXX Eight writes to different addresses should fill 
				 * XXX XXX up the write buffers on 21064 and 21164 chips,
				 * XXX XXX but later CPUs might have larger write buffers which
				 * XXX XXX require further unrolling of this loop, or the
				 * XXX XXX insertion of memory barriers.
				 */
				TGAWALREG(dc, TGA_REG_GCSR, 0, tga_srcb + y + x + 0 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 0, tga_dstb + y + x + 0 * 64);
				TGAWALREG(dc, TGA_REG_GCSR, 1, tga_srcb + y + x + 1 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 1, tga_dstb + y + x + 1 * 64);
				TGAWALREG(dc, TGA_REG_GCSR, 2, tga_srcb + y + x + 2 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 2, tga_dstb + y + x + 2 * 64);
				TGAWALREG(dc, TGA_REG_GCSR, 3, tga_srcb + y + x + 3 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 3, tga_dstb + y + x + 3 * 64);
			}

			/* 64 byte chunks */
			for ( ; x <= xend && xleft >= 64;
			      x += XINC64, xleft -= XINC64) {
				TGAWALREG(dc, TGA_REG_GCSR, 0, tga_srcb + y + x + 0 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 0, tga_dstb + y + x + 0 * 64);
			}
			lastx = x; lastleft = xleft;  /* remember for CPU loop */

		}
		TGAWALREG(dc, TGA_REG_GOPR, 0, 0x0003); /* op -> dst = src */
		TGAWALREG(dc, TGA_REG_GMOR, 0, 0x0000); /* Simple mode */

		for (y = ystart; (ydir * y) <= (ydir * yend); y += yinc) {
			/* 4 byte granularity */
			for (x = lastx, xleft = lastleft;
			     x <= xend && xleft >= 4;
			     x += XINC4, xleft -= XINC4) {
				*(uint32_t *)(dst->ri_bits + dstb + y + x) =
					*(uint32_t *)(dst->ri_bits + srcb + y + x);
			}
		}
	}
	else {    /* above move to the left, below move to the right */

		for (y = ystart; (ydir * y) <= (ydir * yend); y += yinc) {

			/* 4*64 byte chunks */
			for (xleft = wb, x = xstart;
			     x >= xend && xleft >= 4*64;
			     x -= XINC256, xleft -= XINC256) {

				/* XXX XXX Eight writes to different addresses should fill 
				 * XXX XXX up the write buffers on 21064 and 21164 chips,
				 * XXX XXX but later CPUs might have larger write buffers which
				 * XXX XXX require further unrolling of this loop, or the
				 * XXX XXX insertion of memory barriers.
				 */
				TGAWALREG(dc, TGA_REG_GCSR, 0, tga_srcb + y + x + 3 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 0, tga_dstb + y + x + 3 * 64);
				TGAWALREG(dc, TGA_REG_GCSR, 1, tga_srcb + y + x + 2 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 1, tga_dstb + y + x + 2 * 64);
				TGAWALREG(dc, TGA_REG_GCSR, 2, tga_srcb + y + x + 1 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 2, tga_dstb + y + x + 1 * 64);
				TGAWALREG(dc, TGA_REG_GCSR, 3, tga_srcb + y + x + 0 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 3, tga_dstb + y + x + 0 * 64);
			}

			if (xleft) x += XINC256 - XINC64;

			/* 64 byte chunks */
			for ( ; x >= xend && xleft >= 64;
			      x -= XINC64, xleft -= XINC64) {
				TGAWALREG(dc, TGA_REG_GCSR, 0, tga_srcb + y + x + 0 * 64);
				TGAWALREG(dc, TGA_REG_GCDR, 0, tga_dstb + y + x + 0 * 64);
			}
			if (xleft) x += XINC64 - XINC4;
			lastx = x; lastleft = xleft;  /* remember for CPU loop */
		}
		TGAWALREG(dc, TGA_REG_GOPR, 0, 0x0003); /* op -> dst = src */
		TGAWALREG(dc, TGA_REG_GMOR, 0, 0x0000); /* Simple mode */

		for (y = ystart; (ydir * y) <= (ydir * yend); y += yinc) {
			/* 4 byte granularity */
			for (x = lastx, xleft = lastleft;
			     x >= xend && xleft >= 4;
			     x -= XINC4, xleft -= XINC4) {
				*(uint32_t *)(dst->ri_bits + dstb + y + x) =
					*(uint32_t *)(dst->ri_bits + srcb + y + x);
			}
		}
	}
	return 0;
}


int
tga_putchar(void *c, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri = c;
	struct tga_devconfig *dc = ri->ri_hw;
	int fs, height, width;
	int fg, bg, ul;
	u_char *fr;
	int32_t *rp;

	rp = (int32_t *)(ri->ri_bits + row*ri->ri_yscale + col*ri->ri_xscale);

	height = ri->ri_font->fontheight;
	width = ri->ri_font->fontwidth;

	uc -= ri->ri_font->firstchar;
	fr = (u_char *)ri->ri_font->data + uc * ri->ri_fontscale;
	fs = ri->ri_font->stride;

	/* Set foreground and background color. XXX memoize this somehow?
	 * The rasops code has already expanded the color entry to 32 bits
	 * for us, even for 8-bit displays, so we don't have to do anything.
	 */
	ri->ri_ops.unpack_attr(c, attr, &fg, &bg, &ul);
	TGAWREG(dc, TGA_REG_GFGR, ri->ri_devcmap[fg]);
	TGAWREG(dc, TGA_REG_GBGR, ri->ri_devcmap[bg]);
	
	/* Set raster operation to "copy"... */
	if (ri->ri_depth == 8)
		TGAWREG(dc, TGA_REG_GOPR, 0x3);
	else /* ... and in 24-bit mode, set the destination bitmap to 24-bit. */
		TGAWREG(dc, TGA_REG_GOPR, 0x3 | (0x3 << 8));

	/* Set which pixels we're drawing (of a possible 32). */
	TGAWREG(dc, TGA_REG_GPXR_P, (1 << width) - 1);

	/* Set drawing mode to opaque stipple. */
	TGAWREG(dc, TGA_REG_GMOR, 0x1);
	
	/* Insert write barrier before actually sending data */
	/* XXX Abuses the fact that there is only one write barrier on Alphas */
	TGAREGWB(dc, TGA_REG_GMOR, 1);

	while (height--) {
		/* The actual stipple write */
		*rp = fr[0] | (fr[1] << 8) | (fr[2] << 16) | (fr[3] << 24); 
						  
		fr += fs;
		rp = (int32_t *)((caddr_t)rp + ri->ri_stride);
	}

	/* Do underline */
	if (ul) {
		rp = (int32_t *)((caddr_t)rp - (ri->ri_stride << 1));
		*rp = 0xffffffff;
	}

	/* Set graphics mode back to normal. */
	TGAWREG(dc, TGA_REG_GMOR, 0);
	TGAWREG(dc, TGA_REG_GPXR_P, 0xffffffff);

	return 0;
}

int
tga_eraserows(void *c, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = c;
	struct tga_devconfig *dc = ri->ri_hw;
	int32_t color, lines, pixels;
	int fg, bg;
	int32_t *rp;

	ri->ri_ops.unpack_attr(c, attr, &fg, &bg, NULL);
	color = ri->ri_devcmap[bg];
	rp = (int32_t *)(ri->ri_bits + row*ri->ri_yscale);
	lines = num * ri->ri_font->fontheight;
	pixels = ri->ri_emuwidth - 1;

	/* Set fill color in block-color registers */
	TGAWREG(dc, TGA_REG_GBCR0, color);
	TGAWREG(dc, TGA_REG_GBCR1, color);
	if (ri->ri_depth != 8) {
		TGAWREG(dc, TGA_REG_GBCR2, color);
		TGAWREG(dc, TGA_REG_GBCR3, color);
		TGAWREG(dc, TGA_REG_GBCR4, color);
		TGAWREG(dc, TGA_REG_GBCR5, color);
		TGAWREG(dc, TGA_REG_GBCR6, color);
		TGAWREG(dc, TGA_REG_GBCR7, color);
	}

	/* Set raster operation to "copy"... */
	if (ri->ri_depth == 8)
		TGAWREG(dc, TGA_REG_GOPR, 0x3);
	else /* ... and in 24-bit mode, set the destination bitmap to 24-bit. */
		TGAWREG(dc, TGA_REG_GOPR, 0x3 | (0x3 << 8));

	/* Set which pixels we're drawing (of a possible 32). */
	TGAWREG(dc, TGA_REG_GDAR, 0xffffffff);

	/* Set drawing mode to block fill. */
	TGAWREG(dc, TGA_REG_GMOR, 0x2d);
	
	/* Insert write barrier before actually sending data */
	/* XXX Abuses the fact that there is only one write barrier on Alphas */
	TGAREGWB(dc, TGA_REG_GMOR, 1);

	while (lines--) {
		*rp = pixels;
		rp = (int32_t *)((caddr_t)rp + ri->ri_stride);
	}

	/* Set graphics mode back to normal. */
	TGAWREG(dc, TGA_REG_GMOR, 0);
	
	return 0;
}

int
tga_erasecols(void *c, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = c;
	struct tga_devconfig *dc = ri->ri_hw;
	int32_t color, lines, pixels;
	int fg, bg;
	int32_t *rp;

	ri->ri_ops.unpack_attr(c, attr, &fg, &bg, NULL);
	color = ri->ri_devcmap[bg];
	rp = (int32_t *)(ri->ri_bits + row*ri->ri_yscale + col*ri->ri_xscale);
	lines = ri->ri_font->fontheight;
	pixels = (num * ri->ri_font->fontwidth) - 1;

	/* Set fill color in block-color registers */
	TGAWREG(dc, TGA_REG_GBCR0, color);
	TGAWREG(dc, TGA_REG_GBCR1, color);
	if (ri->ri_depth != 8) {
		TGAWREG(dc, TGA_REG_GBCR2, color);
		TGAWREG(dc, TGA_REG_GBCR3, color);
		TGAWREG(dc, TGA_REG_GBCR4, color);
		TGAWREG(dc, TGA_REG_GBCR5, color);
		TGAWREG(dc, TGA_REG_GBCR6, color);
		TGAWREG(dc, TGA_REG_GBCR7, color);
	}

	/* Set raster operation to "copy"... */
	if (ri->ri_depth == 8)
		TGAWREG(dc, TGA_REG_GOPR, 0x3);
	else /* ... and in 24-bit mode, set the destination bitmap to 24-bit. */
		TGAWREG(dc, TGA_REG_GOPR, 0x3 | (0x3 << 8));

	/* Set which pixels we're drawing (of a possible 32). */
	TGAWREG(dc, TGA_REG_GDAR, 0xffffffff);

	/* Set drawing mode to block fill. */
	TGAWREG(dc, TGA_REG_GMOR, 0x2d);
	
	/* Insert write barrier before actually sending data */
	/* XXX Abuses the fact that there is only one write barrier on Alphas */
	TGAREGWB(dc, TGA_REG_GMOR, 1);

	while (lines--) {
		*rp = pixels;
		rp = (int32_t *)((caddr_t)rp + ri->ri_stride);
	}

	/* Set graphics mode back to normal. */
	TGAWREG(dc, TGA_REG_GMOR, 0);

	return 0;
}


void
tga_ramdac_wr(void *v, u_int btreg, u_int8_t val)
{
	struct tga_devconfig *dc = v;

	if (btreg > BT485_REG_MAX)
		panic("tga_ramdac_wr: reg %d out of range", btreg);

	TGAWREG(dc, TGA_REG_EPDR, (btreg << 9) | (0 << 8 ) | val); /* XXX */
	TGAREGWB(dc, TGA_REG_EPDR, 1);
}

void
tga2_ramdac_wr(void *v, u_int btreg, u_int8_t val)
{
	struct tga_devconfig *dc = v;
	bus_space_handle_t ramdac;

	if (btreg > BT485_REG_MAX)
		panic("tga_ramdac_wr: reg %d out of range", btreg);

	bus_space_subregion(dc->dc_memt, dc->dc_memh, TGA2_MEM_RAMDAC + 
		(0xe << 12) + (btreg << 8), 4, &ramdac);
	bus_space_write_4(dc->dc_memt, ramdac, 0, val & 0xff);
	bus_space_barrier(dc->dc_memt, ramdac, 0, 4, BUS_SPACE_BARRIER_WRITE);
}

u_int8_t
tga_bt463_rd(void *v, u_int btreg)
{
	struct tga_devconfig *dc = v;
	tga_reg_t rdval;

	/* 
	 * Strobe CE# (high->low->high) since status and data are latched on 
	 * the falling and rising edges (respectively) of this active-low signal.
	 */
	
	TGAREGWB(dc, TGA_REG_EPSR, 1);
	TGAWREG(dc, TGA_REG_EPSR, (btreg << 2) | 2 | 1);
	TGAREGWB(dc, TGA_REG_EPSR, 1);
	TGAWREG(dc, TGA_REG_EPSR, (btreg << 2) | 2 | 0);

	TGAREGRB(dc, TGA_REG_EPSR, 1);

	rdval = TGARREG(dc, TGA_REG_EPDR);
	TGAREGWB(dc, TGA_REG_EPSR, 1);
	TGAWREG(dc, TGA_REG_EPSR, (btreg << 2) | 2 | 1);

	return (rdval >> 16) & 0xff;
}

void
tga_bt463_wr(void *v, u_int btreg, u_int8_t val)
{
	struct tga_devconfig *dc = v;

	/* 
	 * In spite of the 21030 documentation, to set the MPU bus bits for
	 * a write, you set them in the upper bits of EPDR, not EPSR.
	 */
	
	/* 
	 * Strobe CE# (high->low->high) since status and data are latched on
	 * the falling and rising edges of this active-low signal.
	 */

	TGAREGWB(dc, TGA_REG_EPDR, 1);
	TGAWREG(dc, TGA_REG_EPDR, (btreg << 10) | 0x100 | val);
	TGAREGWB(dc, TGA_REG_EPDR, 1);
	TGAWREG(dc, TGA_REG_EPDR, (btreg << 10) | 0x000 | val);
	TGAREGWB(dc, TGA_REG_EPDR, 1);
	TGAWREG(dc, TGA_REG_EPDR, (btreg << 10) | 0x100 | val);

}

u_int8_t
tga_ramdac_rd(void *v, u_int btreg)
{
	struct tga_devconfig *dc = v;
	tga_reg_t rdval;

	if (btreg > BT485_REG_MAX)
		panic("tga_ramdac_rd: reg %d out of range", btreg);

	TGAWREG(dc, TGA_REG_EPSR, (btreg << 1) | 0x1); /* XXX */
	TGAREGWB(dc, TGA_REG_EPSR, 1);

	rdval = TGARREG(dc, TGA_REG_EPDR);
	return (rdval >> 16) & 0xff;				/* XXX */
}

u_int8_t
tga2_ramdac_rd(void *v, u_int btreg)
{
	struct tga_devconfig *dc = v;
	bus_space_handle_t ramdac;
	u_int8_t retval;

	if (btreg > BT485_REG_MAX)
		panic("tga_ramdac_rd: reg %d out of range", btreg);

	bus_space_subregion(dc->dc_memt, dc->dc_memh, TGA2_MEM_RAMDAC + 
		(0xe << 12) + (btreg << 8), 4, &ramdac);
	retval = bus_space_read_4(dc->dc_memt, ramdac, 0) & 0xff;
	bus_space_barrier(dc->dc_memt, ramdac, 0, 4, BUS_SPACE_BARRIER_READ);
	return retval;
}

#include <dev/ic/decmonitors.c>
void tga2_ics9110_wr(
	struct tga_devconfig *dc,
	int dotclock
);

struct monitor *tga_getmonitor(struct tga_devconfig *dc);

void
tga2_init(struct tga_devconfig *dc)
{
	struct	monitor *m = tga_getmonitor(dc);


	/* Deal with the dot clocks.
	 */
	if (dc->dc_tga_type == TGA_TYPE_POWERSTORM_4D20) {
		/* Set this up as a reference clock for the
		 * ibm561's PLL.
		 */
		tga2_ics9110_wr(dc, 14300000);
		/* XXX Can't set up the dotclock properly, until such time
		 * as the RAMDAC is configured.
		 */
	} else {
		/* otherwise the ics9110 is our clock. */
		tga2_ics9110_wr(dc, m->dotclock);
	}
#if 0
	TGAWREG(dc, TGA_REG_VHCR, 
	     ((m->hbp / 4) << 21) |
	     ((m->hsync / 4) << 14) |
	    (((m->hfp - 4) / 4) << 9) |
	     ((m->cols + 4) / 4));
#else
	TGAWREG(dc, TGA_REG_VHCR, 
	     ((m->hbp / 4) << 21) |
	     ((m->hsync / 4) << 14) |
	    (((m->hfp) / 4) << 9) |
	     ((m->cols) / 4));
#endif
	TGAWREG(dc, TGA_REG_VVCR, 
	    (m->vbp << 22) |
	    (m->vsync << 16) |
	    (m->vfp << 11) |
	    (m->rows));
	TGAWREG(dc, TGA_REG_VVBR, 1);
	TGAREGRWB(dc, TGA_REG_VHCR, 3);
	TGAWREG(dc, TGA_REG_VVVR, TGARREG(dc, TGA_REG_VVVR) | 1);
	TGAREGRWB(dc, TGA_REG_VVVR, 1);
	TGAWREG(dc, TGA_REG_GPMR, 0xffffffff);
	TGAREGRWB(dc, TGA_REG_GPMR, 1);
}

void
tga2_ics9110_wr(struct tga_devconfig *dc, int dotclock)
{
	bus_space_handle_t clock;
	u_int32_t valU;
	int N, M, R, V, X;
	int i;

	switch (dotclock) {
	case 130808000:
		N = 0x40; M = 0x7; V = 0x0; X = 0x1; R = 0x1; break;
	case 119840000:
		N = 0x2d; M = 0x2b; V = 0x1; X = 0x1; R = 0x1; break;
	case 108180000:
		N = 0x11; M = 0x9; V = 0x1; X = 0x1; R = 0x2; break;
	case 103994000:
		N = 0x6d; M = 0xf; V = 0x0; X = 0x1; R = 0x1; break;
	case 175000000:
		N = 0x5F; M = 0x3E; V = 0x1; X = 0x1; R = 0x1; break;
	case  75000000:
		N = 0x6e; M = 0x15; V = 0x0; X = 0x1; R = 0x1; break;
	case  74000000:
		N = 0x2a; M = 0x41; V = 0x1; X = 0x1; R = 0x1; break;
	case  69000000:
		N = 0x35; M = 0xb; V = 0x0; X = 0x1; R = 0x1; break;
	case  65000000:
		N = 0x6d; M = 0x0c; V = 0x0; X = 0x1; R = 0x2; break;
	case  50000000:
		N = 0x37; M = 0x3f; V = 0x1; X = 0x1; R = 0x2; break;
	case  40000000:
		N = 0x5f; M = 0x11; V = 0x0; X = 0x1; R = 0x2; break;
	case  31500000:
		N = 0x16; M = 0x05; V = 0x0; X = 0x1; R = 0x2; break;
	case  25175000:
		N = 0x66; M = 0x1d; V = 0x0; X = 0x1; R = 0x2; break;
	case 135000000:
		N = 0x42; M = 0x07; V = 0x0; X = 0x1; R = 0x1; break;
	case 110000000:
		N = 0x60; M = 0x32; V = 0x1; X = 0x1; R = 0x2; break;
	case 202500000:
		N = 0x60; M = 0x32; V = 0x1; X = 0x1; R = 0x2; break;
       case  14300000:         /* this one is just a ref clock */
               N = 0x03; M = 0x03; V = 0x1; X = 0x1; R = 0x3; break;
	default:
		panic("unrecognized clock rate %d", dotclock);
	}

	/* XXX -- hard coded, bad */
	valU  = N | ( M << 7 ) | (V << 14);
	valU |= (X << 15) | (R << 17);
	valU |= 0x17 << 19;

	bus_space_subregion(dc->dc_memt, dc->dc_memh, TGA2_MEM_EXTDEV +
	    TGA2_MEM_CLOCK + (0xe << 12), 4, &clock); /* XXX */

	for (i = 24; i > 0; i--) {
		u_int32_t writeval;
                
		writeval = valU & 0x1;
		if (i == 1)  
			writeval |= 0x2; 
		valU >>= 1;
		bus_space_write_4(dc->dc_memt, clock, 0, writeval);
		bus_space_barrier(dc->dc_memt, clock, 0, 4, BUS_SPACE_BARRIER_WRITE);
        }       
	bus_space_subregion(dc->dc_memt, dc->dc_memh, TGA2_MEM_EXTDEV +
	    TGA2_MEM_CLOCK + (0xe << 12) + (0x1 << 11) + (0x1 << 11), 4,
		&clock); /* XXX */
	bus_space_write_4(dc->dc_memt, clock, 0, 0x0);
	bus_space_barrier(dc->dc_memt, clock, 0, 0, BUS_SPACE_BARRIER_WRITE);
}

struct monitor *
tga_getmonitor(struct tga_devconfig *dc)
{
       return &decmonitors[(~TGARREG(dc, TGA_REG_GREV) >> 16) & 0x0f];
}

unsigned
tga_getdotclock(struct tga_devconfig *dc)
{
       return tga_getmonitor(dc)->dotclock;
}
