/*	$OpenBSD: cfxga.c,v 1.35 2024/05/26 08:46:28 jsg Exp $	*/

/*
 * Copyright (c) 2005, 2006, Matthieu Herrb and Miodrag Vallat
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Display driver for the Colorgraphic CompactFlash ``VoyagerVGA'' card.
 * based upon the Epson S1D13806 graphics chip.
 *
 * Our goals are:
 * - to provide a somewhat usable emulation mode for extra text display.
 * - to let an application (such as an X server) map the controller registers
 *   in order to do its own display game.
 *
 * Driving this card is somewhat a challenge since:
 * - its video memory is not directly accessible.
 * - no operation can make use of DMA.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciareg.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <dev/pcmcia/cfxgareg.h>

/*
#define CFXGADEBUG
#define ENABLE_8BIT_MODES
*/

#ifdef CFXGADEBUG
#define	DPRINTF(arg) printf arg
#else
#define	DPRINTF(arg)
#endif

struct cfxga_screen;

#define	CFXGA_MODE_640x480x16	0
#define	CFXGA_MODE_800x600x16	1
#ifdef ENABLE_8BIT_MODES
#define	CFXGA_MODE_640x480x8	2
#define	CFXGA_MODE_800x600x8	3
#define	CFXGA_NMODES		4
#else
#define	CFXGA_NMODES		2
#endif

struct cfxga_softc {
	struct device sc_dev;
	struct pcmcia_function *sc_pf;
	int	sc_state;
#define	CS_MAPPED	0x0001
#define	CS_RESET	0x0002

	struct pcmcia_mem_handle sc_pmemh;
	int sc_memwin;
	bus_addr_t sc_offset;

	int sc_mode;

	int sc_nscreens;
	LIST_HEAD(, cfxga_screen) sc_scr;
	struct cfxga_screen *sc_active;

	/* wsdisplay glue */
	struct wsscreen_descr sc_wsd[CFXGA_NMODES];
	struct wsscreen_list sc_wsl;
	struct wsscreen_descr *sc_scrlist[CFXGA_NMODES];
	struct wsdisplay_emulops sc_ops;
	struct device *sc_wsdisplay;
};

int	cfxga_match(struct device *, void *,  void *);
void	cfxga_attach(struct device *, struct device *, void *);
int	cfxga_detach(struct device *, int);
int	cfxga_activate(struct device *, int);

const struct cfattach cfxga_ca = {
	sizeof(struct cfxga_softc), cfxga_match, cfxga_attach,
	cfxga_detach, cfxga_activate
};

struct cfdriver cfxga_cd = {
	NULL, "cfxga", DV_DULL
};

int	cfxga_alloc_screen(void *, const struct wsscreen_descr *, void **,
	    int *, int *, uint32_t *);
void	cfxga_burner(void *, u_int, u_int);
void	cfxga_free_screen(void *, void *);
int	cfxga_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	cfxga_mmap(void *, off_t, int);
int	cfxga_load_font(void *, void *, struct wsdisplay_font *);
int	cfxga_list_font(void *, struct wsdisplay_font *);
int	cfxga_show_screen(void *, void *, int, void (*)(void *, int, int),
	    void *);

struct wsdisplay_accessops cfxga_accessops = {
	.ioctl = cfxga_ioctl,
	.mmap = cfxga_mmap,
	.alloc_screen = cfxga_alloc_screen,
	.free_screen = cfxga_free_screen,
	.show_screen = cfxga_show_screen,
	.load_font = cfxga_load_font,
	.list_font = cfxga_list_font,
	.burn_screen = cfxga_burner
};

/*
 * Per-screen structure
 */

struct cfxga_screen {
	LIST_ENTRY(cfxga_screen) scr_link;
	struct cfxga_softc *scr_sc;	/* parent reference */
	struct rasops_info scr_ri;	/* raster op glue */
	struct wsdisplay_charcell *scr_mem;	/* backing memory */
};
	
int	cfxga_copycols(void *, int, int, int, int);
int	cfxga_copyrows(void *, int, int, int);
int	cfxga_do_cursor(struct rasops_info *);
int	cfxga_erasecols(void *, int, int, int, uint32_t);
int	cfxga_eraserows(void *, int, int, uint32_t);
int	cfxga_putchar(void *, int, int, u_int, uint32_t);

int	cfxga_install_function(struct pcmcia_function *);
void	cfxga_remove_function(struct pcmcia_function *);

int	cfxga_expand_char(struct cfxga_screen *, u_int, int, int, uint32_t);
int	cfxga_repaint_screen(struct cfxga_screen *);
void	cfxga_reset_video(struct cfxga_softc *);
void	cfxga_reset_and_repaint(struct cfxga_softc *);
int	cfxga_solid_fill(struct cfxga_screen *, int, int, int, int, int32_t);
int	cfxga_standalone_rop(struct cfxga_screen *, u_int,
	    int, int, int, int, int, int);
int	cfxga_synchronize(struct cfxga_softc *);
u_int	cfxga_wait(struct cfxga_softc *, u_int, u_int);

#define	cfxga_clear_screen(scr) \
	cfxga_solid_fill(scr, 0, 0, scr->scr_ri.ri_width, \
	    scr->scr_ri.ri_height, scr->scr_ri.ri_devcmap[WSCOL_BLACK])

#define	cfxga_read_1(sc, addr) \
	bus_space_read_1((sc)->sc_pmemh.memt, (sc)->sc_pmemh.memh, \
	    (sc)->sc_offset + (addr))
#define	cfxga_read_2(sc, addr) \
	bus_space_read_2((sc)->sc_pmemh.memt, (sc)->sc_pmemh.memh, \
	    (sc)->sc_offset + (addr))
#define	cfxga_write_1(sc, addr, val) \
	bus_space_write_1((sc)->sc_pmemh.memt, (sc)->sc_pmemh.memh, \
	    (sc)->sc_offset + (addr), (val))
#define	cfxga_write_2(sc, addr, val) \
	bus_space_write_2((sc)->sc_pmemh.memt, (sc)->sc_pmemh.memh, \
	    (sc)->sc_offset + (addr), (val))

#define	cfxga_stop_memory_blt(sc) \
	(void)cfxga_read_2(sc, CFREG_BITBLT_DATA)

const char *cfxga_modenames[CFXGA_NMODES] = {
	"640x480x16",
	"800x600x16",
#ifdef ENABLE_8BIT_MODES
	"640x480x8",
	"800x600x8"
#endif
};

/*
 * This card is very poorly engineered, specificationwise. It does not
 * provide any CIS information, and has no vendor/product numbers as
 * well: as such, there is no easy way to differentiate it from any
 * other cheapo PCMCIA card.
 *
 * The best we can do is probe for a chip ID. This is not perfect but better
 * than matching blindly. Of course this requires us to play some nasty games
 * behind the PCMCIA framework to be able to do this probe, and correctly fail
 * if this is not the card we are looking for.
 *
 * In shorter words: some card designers ought to be shot, as a service
 * to the community.
 */

/*
 * Create the necessary pcmcia function structures to alleviate the lack
 * of any CIS information on this device.
 * Actually, we hijack the fake function created by the pcmcia framework.
 */
int
cfxga_install_function(struct pcmcia_function *pf)
{
	struct pcmcia_config_entry *cfe;

	/* Get real. */
	pf->pf_flags &= ~PFF_FAKE;

	/* Tell the pcmcia framework where the CCR is. */
	pf->ccr_base = 0x800;
	pf->ccr_mask = 0x67;

	/* Create a simple cfe. */
	cfe = (struct pcmcia_config_entry *)malloc(sizeof *cfe,
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cfe == NULL) {
		DPRINTF(("%s: cfe allocation failed\n", __func__));
		return (ENOMEM);
	}

	cfe->number = 42;	/* have to put some value... */
	cfe->flags = PCMCIA_CFE_IO16;
	cfe->iftype = PCMCIA_IFTYPE_MEMORY;

	SIMPLEQ_INSERT_TAIL(&pf->cfe_head, cfe, cfe_list);

	pcmcia_function_init(pf, cfe);
	return (0);
}

/*
 * Undo the changes done above.
 * Such a function is necessary since we need a full-blown pcmcia world
 * set up in order to do the device probe, but if we don't match the card,
 * leaving this state will cause trouble during other probes.
 */
void
cfxga_remove_function(struct pcmcia_function *pf)
{
	struct pcmcia_config_entry *cfe;

	/* we are the first and only entry... */
	cfe = SIMPLEQ_FIRST(&pf->cfe_head);
	SIMPLEQ_REMOVE_HEAD(&pf->cfe_head, cfe_list);
	free(cfe, M_DEVBUF, 0);

	/* And we're a figment of the kernel's imagination again. */
	pf->pf_flags |= PFF_FAKE;
}

int 
cfxga_match(struct device *parent, void *match, void *aux)
{
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_function *pf = pa->pf;
	struct pcmcia_mem_handle h;
	int rc;
	int win;
	bus_addr_t ptr;
	u_int8_t id = 0;

	if (pa->product != PCMCIA_PRODUCT_INVALID ||
	    pa->manufacturer != PCMCIA_VENDOR_INVALID)
		return (0);

	/* Only a card with no CIS will have a fake function... */
	if ((pf->pf_flags & PFF_FAKE) == 0)
		return (0);

	if (cfxga_install_function(pf) != 0)
		return (0);

	if (pcmcia_function_enable(pf) != 0) {
		DPRINTF(("%s: function enable failed\n", __func__));
		return (0);
	}

	rc = pcmcia_mem_alloc(pf, CFXGA_MEM_RANGE, &h);
	if (rc != 0)
		goto out;

	rc = pcmcia_mem_map(pf, PCMCIA_MEM_ATTR, 0, CFXGA_MEM_RANGE,
	    &h, &ptr, &win);
	if (rc != 0)
		goto out2;

	id = (bus_space_read_1(h.memt, h.memh, ptr + CFREG_REV) &
	    CR_PRODUCT_MASK) >> CR_PRODUCT_SHIFT;

	pcmcia_mem_unmap(pa->pf, win);
out2:
	pcmcia_mem_free(pa->pf, &h);
out:
	pcmcia_function_disable(pf);
	cfxga_remove_function(pf);

	/*
	 * Be sure to return a value greater than com's if we match,
	 * otherwise it can win due to the way config(8) will order devices...
	 */
	return (id == PRODUCT_S1D13806 ? 10 : 0);
}

int
cfxga_activate(struct device *dev, int act)
{
	struct cfxga_softc *sc = (void *)dev;
	int rv = 0;

	switch (act) {
	case DVACT_DEACTIVATE:
		pcmcia_function_disable(sc->sc_pf);
		break;
	default:
		rv = config_activate_children(dev, act);
		break;
	}
	return (rv);
}

void 
cfxga_attach(struct device *parent, struct device *self, void *aux)
{
	struct cfxga_softc *sc = (void *)self;
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_function *pf = pa->pf;
	struct wsemuldisplaydev_attach_args waa;
	struct wsscreen_descr *wsd;
	u_int i;

	LIST_INIT(&sc->sc_scr);
	sc->sc_nscreens = 0;
	sc->sc_pf = pf;

	if (cfxga_install_function(pf) != 0) {
		printf(": pcmcia function setup failed\n");
		return;
	}

	if (pcmcia_function_enable(pf)) {
		printf(": function enable failed\n");
		return;
	}

	if (pcmcia_mem_alloc(pf, CFXGA_MEM_RANGE, &sc->sc_pmemh) != 0) {
		printf(": can't allocate memory space\n");
		return;
	}

	if (pcmcia_mem_map(pf, PCMCIA_MEM_ATTR, 0, CFXGA_MEM_RANGE,
	    &sc->sc_pmemh, &sc->sc_offset, &sc->sc_memwin) != 0) {
		printf(": can't map frame buffer registers\n");
		pcmcia_mem_free(pf, &sc->sc_pmemh);
		return;
	}

	SET(sc->sc_state, CS_MAPPED);

	printf("\n");

	sc->sc_mode = WSDISPLAYIO_MODE_EMUL;

	/*
	 * We actually defer real initialization to the creation of the
	 * first wsdisplay screen, since we do not know which mode to pick
	 * yet.
	 */

	for (wsd = sc->sc_wsd, i = 0; i < CFXGA_NMODES; wsd++, i++) {
		strlcpy(wsd->name, cfxga_modenames[i], sizeof(wsd->name));
		wsd->textops = &sc->sc_ops;
		sc->sc_scrlist[i] = wsd;
	}
	sc->sc_wsl.nscreens = CFXGA_NMODES;
	sc->sc_wsl.screens = (const struct wsscreen_descr **)sc->sc_scrlist;

	waa.console = 0;
	waa.scrdata = &sc->sc_wsl;
	waa.accessops = &cfxga_accessops;
	waa.accesscookie = sc;
	waa.defaultscreens = 1;

	if ((sc->sc_wsdisplay =
	    config_found(self, &waa, wsemuldisplaydevprint)) == NULL) {
		/* otherwise wscons will do this */
		if (sc->sc_active != NULL)
			cfxga_clear_screen(sc->sc_active);
		else
			cfxga_burner(sc, 0, 0);
	}
}

int
cfxga_detach(struct device *dev, int flags)
{
	struct cfxga_softc *sc = (void *)dev;

	/*
	 * Detach all children, and hope wsdisplay detach code is correct...
	 */
	if (sc->sc_wsdisplay != NULL) {
		config_detach(sc->sc_wsdisplay, DETACH_FORCE);
		/* sc->sc_wsdisplay = NULL; */
	}

	if (ISSET(sc->sc_state, CS_MAPPED)) {
		pcmcia_mem_unmap(sc->sc_pf, sc->sc_memwin);
		pcmcia_mem_free(sc->sc_pf, &sc->sc_pmemh);
		/* CLR(sc->sc_state, CS_MAPPED); */
	}

	return (0);
}

/*
 * Wscons operations
 */

int
cfxga_alloc_screen(void *v, const struct wsscreen_descr *type, void **cookiep,
    int *curxp, int *curyp, uint32_t *attrp)
{
	struct cfxga_softc *sc = v;
	struct cfxga_screen *scr;
	struct rasops_info *ri;
	u_int mode, width, height, depth, scrsize;

	scr = malloc(sizeof *scr, M_DEVBUF,
	    (cold ? M_NOWAIT : M_WAITOK) | M_ZERO);
	if (scr == NULL)
		return (ENOMEM);

	mode = type - sc->sc_wsd;
#ifdef DIAGNOSTIC
	if (mode >= CFXGA_NMODES)
		mode = CFXGA_MODE_640x480x16;
#endif
	switch (mode) {
	default:
	case CFXGA_MODE_640x480x16:
		width = 640;
		height = 480;
		depth = 16;
		break;
	case CFXGA_MODE_800x600x16:
		width = 800;
		height = 600;
		depth = 16;
		break;
#ifdef ENABLE_8BIT_MODES
	case CFXGA_MODE_640x480x8:
		width = 640;
		height = 480;
		depth = 8;
		break;
	case CFXGA_MODE_800x600x8:
		width = 800;
		height = 600;
		depth = 8;
		break;
#endif
	}

	ri = &scr->scr_ri;
	ri->ri_hw = (void *)scr;
	ri->ri_bits = NULL;
	ri->ri_depth = depth;
	ri->ri_width = width;
	ri->ri_height = height;
	ri->ri_stride = width * depth / 8;
	ri->ri_flg = 0;

	/* swap B and R at 16 bpp */
	if (depth == 16) {
		ri->ri_rnum = 5;
		ri->ri_rpos = 11;
		ri->ri_gnum = 6;
		ri->ri_gpos = 5;
		ri->ri_bnum = 5;
		ri->ri_bpos = 0;
	}

	if (type->nrows == 0)	/* first screen creation */
		rasops_init(ri, 100, 100);
	else
		rasops_init(ri, type->nrows, type->ncols);

	/*
	 * Allocate backing store to remember non-visible screen contents in
	 * emulation mode.
	 */
	scr->scr_mem = mallocarray(ri->ri_rows,
	    ri->ri_cols * sizeof(struct wsdisplay_charcell), M_DEVBUF,
	    (cold ? M_NOWAIT : M_WAITOK) | M_ZERO);
	if (scr->scr_mem == NULL) {
		free(scr, M_DEVBUF, 0);
		return (ENOMEM);
	}
	scrsize = ri->ri_rows * ri->ri_cols * sizeof(struct wsdisplay_charcell);

	ri->ri_ops.copycols = cfxga_copycols;
	ri->ri_ops.copyrows = cfxga_copyrows;
	ri->ri_ops.erasecols = cfxga_erasecols;
	ri->ri_ops.eraserows = cfxga_eraserows;
	ri->ri_ops.putchar = cfxga_putchar;
	ri->ri_do_cursor = cfxga_do_cursor;

	/*
	 * Finish initializing our screen descriptions, now that we know
	 * the actual console emulation parameters.
	 */
	if (type->nrows == 0) {
		struct wsscreen_descr *wsd = (struct wsscreen_descr *)type;

		wsd->nrows = ri->ri_rows;
		wsd->ncols = ri->ri_cols;
		bcopy(&ri->ri_ops, &sc->sc_ops, sizeof(sc->sc_ops));
		wsd->fontwidth = ri->ri_font->fontwidth;
		wsd->fontheight = ri->ri_font->fontheight;
		wsd->capabilities = ri->ri_caps;
	}

	scr->scr_sc = sc;
	LIST_INSERT_HEAD(&sc->sc_scr, scr, scr_link);
	sc->sc_nscreens++;

	ri->ri_ops.pack_attr(ri, 0, 0, 0, attrp);

	*cookiep = ri;
	*curxp = *curyp = 0;
	
	return (0);
}

void
cfxga_burner(void *v, u_int on, u_int flags)
{
	struct cfxga_softc *sc = (void *)v;
	u_int8_t mode;

	mode = cfxga_read_1(sc, CFREG_MODE) & LCD_MODE_SWIVEL_BIT_0;

	if (on)
		cfxga_write_1(sc, CFREG_MODE, mode | MODE_CRT);
	else
		cfxga_write_1(sc, CFREG_MODE, mode | MODE_NO_DISPLAY);
}

void
cfxga_free_screen(void *v, void *cookie)
{
	struct cfxga_softc *sc = v;
	struct rasops_info *ri = cookie;
	struct cfxga_screen *scr = ri->ri_hw;

	LIST_REMOVE(scr, scr_link);
	sc->sc_nscreens--;

	if (scr == sc->sc_active) {
		sc->sc_active = NULL;
		cfxga_burner(sc, 0, 0);
	}

	free(scr->scr_mem, M_DEVBUF, 0);
	free(scr, M_DEVBUF, 0);
}

int
cfxga_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct cfxga_softc *sc = v;
	struct cfxga_screen *scr;
	struct wsdisplay_fbinfo *wdf;
	int mode;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_CFXGA;
		break;

	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		scr = sc->sc_active;
		if (scr == NULL) {
			/* try later...after running wsconscfg to add screens */
			wdf->height = wdf->width = wdf->depth = 0;
			wdf->stride = wdf->offset = wdf->cmsize = 0;
		} else {
			wdf->height = scr->scr_ri.ri_height;
			wdf->width = scr->scr_ri.ri_width;
			wdf->depth = scr->scr_ri.ri_depth;
			wdf->stride = scr->scr_ri.ri_stride;
			wdf->offset = 0;
			wdf->cmsize = scr->scr_ri.ri_depth <= 8 ?
			    (1 << scr->scr_ri.ri_depth) : 0;
		}
		break;

	case WSDISPLAYIO_SMODE:
		mode = *(u_int *)data;
		if (mode == sc->sc_mode)
			break;
		switch (mode) {
		case WSDISPLAYIO_MODE_EMUL:
			cfxga_reset_and_repaint(sc);
			break;
		case WSDISPLAYIO_MODE_MAPPED:
			break;
		default:
			return (EINVAL);
		}
		sc->sc_mode = mode;
		break;

	/* these operations are handled by the wscons code... */
	case WSDISPLAYIO_GVIDEO:
	case WSDISPLAYIO_SVIDEO:
		break;

	/* these operations are not supported... */
	case WSDISPLAYIO_GETCMAP:
	case WSDISPLAYIO_PUTCMAP:
	case WSDISPLAYIO_LINEBYTES:
	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return (-1);
	}

	return (0);
}

paddr_t
cfxga_mmap(void *v, off_t off, int prot)
{
	return (-1);
}

int
cfxga_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	struct cfxga_softc *sc = v;
	struct rasops_info *ri = cookie;
	struct cfxga_screen *scr = ri->ri_hw, *old;

	old = sc->sc_active;
	if (old == scr)
		return (0);

	sc->sc_active = scr;
	cfxga_reset_and_repaint(sc);	/* will turn video on if scr != NULL */

	return (0);
}

int
cfxga_load_font(void *v, void *emulcookie, struct wsdisplay_font *font)
{
	struct cfxga_softc *sc = v;
	struct cfxga_screen *scr = sc->sc_active;

	if (scr == NULL)
		return ENXIO;

	return rasops_load_font(&scr->scr_ri, emulcookie, font);
}

int
cfxga_list_font(void *v, struct wsdisplay_font *font)
{
	struct cfxga_softc *sc = v;
	struct cfxga_screen *scr = sc->sc_active;

	if (scr == NULL)
		return ENXIO;

	return rasops_list_font(&scr->scr_ri, font);
}

/*
 * Real frame buffer operations
 */

void
cfxga_reset_video(struct cfxga_softc *sc)
{
	struct cfxga_screen *scr = sc->sc_active;
	struct rasops_info *ri;
#ifdef ENABLE_8BIT_MODES
	const u_int8_t *cmap;
	u_int i;
#endif

	/*
	 * Reset controller
	 */

	/* need to write to both REV and MISC at the same time */
	cfxga_write_2(sc, CFREG_REV, 0x80 | (CM_REGSEL << 8));
	delay(25000);	/* maintain reset for a short while */
	/* need to write to both REV and MISC at the same time */
	cfxga_write_2(sc, CFREG_REV, 0 | (CM_MEMSEL << 8));
	delay(25000);
	/* stop any pending blt operation */
	cfxga_write_2(sc, CFREG_BITBLT_CONTROL, 0);
	cfxga_stop_memory_blt(sc);
	cfxga_write_1(sc, CFREG_MODE, 0);	/* disable all displays */

	/*
	 * Setup common video mode parameters.
	 */

	cfxga_write_2(sc, CFREG_MEMCLK, MEMCLK_SRC_CLK3);
#if 0
	cfxga_write_1(sc, CFREG_LCD_PCLK, LCD_PCLK_SRC_CLKI | LCD_PCLK_DIV_1);
	cfxga_write_1(sc, CFREG_MPLUG_CLK,
	    MPLUG_PCLK_SRC_CLKI2 | MPLUG_PCLK_DIV_1);
#endif
	cfxga_write_2(sc, CFREG_CRTTV_PCLK, CRT_PCLK_SRC_CLKI | CRT_PCLK_DIV_1);
	cfxga_write_2(sc, CFREG_WSTATE, WSTATE_MCLK);

	/* MEMCNF and DRAM_RFRSH need to be programmed at the same time */
	cfxga_write_2(sc, CFREG_MEMCNF,
	    MEMCNF_SDRAM_INIT | (DRAM_RFRSH_50MHZ << 8));
	delay(250);
	cfxga_write_2(sc, CFREG_DRAM_TIMING, DRAM_TIMING_50MHZ);

	/*
	 * Setup mode-dependent parameters.
	 */
	if (scr == NULL)
		return;

	ri = &scr->scr_ri;
	switch (scr->scr_ri.ri_width) {
	default:
	case 640:
		cfxga_write_1(sc, CFREG_CRT_HWIDTH, (640 / 8) - 1);
		/* HNDISP and HSTART need to be programmed at the same time */
		cfxga_write_2(sc, CFREG_CRT_HNDISP, 23 | (2 << 8));
		cfxga_write_1(sc, CFREG_CRT_HPULSE, 4);
		cfxga_write_2(sc, CFREG_CRT_VHEIGHT, 480 - 1);
		/* VNDISP and VSTART need to be programmed at the same time */
		cfxga_write_2(sc, CFREG_CRT_VNDISP, 39 | (8 << 8));
		cfxga_write_1(sc, CFREG_CRT_VPULSE, 2);
		break;
	case 800:
		cfxga_write_1(sc, CFREG_CRT_HWIDTH, (800 / 8) - 1);
		/* HNDISP and HSTART need to be programmed at the same time */
		cfxga_write_2(sc, CFREG_CRT_HNDISP, 27 | (2 << 8));
		cfxga_write_1(sc, CFREG_CRT_HPULSE, 4);
		cfxga_write_2(sc, CFREG_CRT_VHEIGHT, 600 - 1);
		/* VNDISP and VSTART need to be programmed at the same time */
		cfxga_write_2(sc, CFREG_CRT_VNDISP, 25 | (8 << 8));
		cfxga_write_1(sc, CFREG_CRT_VPULSE, 2);
		break;
	}
	cfxga_write_1(sc, CFREG_CRT_MODE,
	    ri->ri_depth == 16 ? CRT_MODE_16BPP : CRT_MODE_8BPP);
	cfxga_write_2(sc, CFREG_CRT_START_LOW, 0);
	cfxga_write_1(sc, CFREG_CRT_START_HIGH, 0);
	cfxga_write_2(sc, CFREG_CRT_MEMORY, ri->ri_width * ri->ri_depth / 16);
	cfxga_write_1(sc, CFREG_CRT_PANNING, 0);
	cfxga_write_1(sc, CFREG_CRT_FIFO_THRESHOLD_HIGH, 0);
	cfxga_write_1(sc, CFREG_CRT_FIFO_THRESHOLD_LOW, 0);
	cfxga_write_1(sc, CFREG_CRT_CURSOR_CONTROL, CURSOR_INACTIVE);

#ifdef ENABLE_8BIT_MODES
	/*
	 * On 8bpp video modes, program the LUT
	 */
	if (ri->ri_depth == 8) {
#if 0
		/* Wait for retrace */
		while ((cfxga_read_1(sc, CFREG_CRT_VNDISP) &
		    CRT_VNDISP_STATUS) == 0)
			delay(1);
#endif
		cfxga_write_1(sc, CFREG_LUT_MODE, LUT_CRT);
		cfxga_write_1(sc, CFREG_LUT_ADDRESS, 0); /* autoincrements */
		cmap = rasops_cmap;
		for (i = 256 * 3; i != 0; i--)
			cfxga_write_1(sc, CFREG_LUT_DATA, *cmap++ & 0xf0);
	}
#endif

	cfxga_write_1(sc, CFREG_TV_CONTROL,
	    TV_LUMINANCE_FILTER | TV_SVIDEO_OUTPUT | TV_NTSC_OUTPUT);

	cfxga_write_1(sc, CFREG_POWER_CONF, POWERSAVE_MBO);
	cfxga_write_1(sc, CFREG_WATCHDOG, 0);

	cfxga_write_1(sc, CFREG_MODE, MODE_CRT);
	delay(25000);
}

void
cfxga_reset_and_repaint(struct cfxga_softc *sc)
{
	cfxga_reset_video(sc);

	if (sc->sc_active != NULL)
		cfxga_repaint_screen(sc->sc_active);
	else
		cfxga_burner(sc, 0, 0);
}

/*
 * Wait for the blitter to be in a given state.
 */
u_int
cfxga_wait(struct cfxga_softc *sc, u_int mask, u_int result)
{
	u_int tries;

	for (tries = 10000; tries != 0; tries--) {
		if ((cfxga_read_1(sc, CFREG_BITBLT_CONTROL) & mask) == result)
			break;
		delay(10);
	}

	return (tries);
}

/*
 * Wait for all pending blitter operations to be complete.
 * Returns non-zero if the blitter got stuck.
 */
int
cfxga_synchronize(struct cfxga_softc *sc)
{
	/* Wait for previous operations to complete */
	if (cfxga_wait(sc, BITBLT_ACTIVE, 0) == 0) {
		DPRINTF(("%s: not ready\n", __func__));
		if (ISSET(sc->sc_state, CS_RESET))
			return (EAGAIN);
		else {
			DPRINTF(("%s: resetting...\n", sc->sc_dev.dv_xname));
			SET(sc->sc_state, CS_RESET);
			cfxga_reset_and_repaint(sc);
			CLR(sc->sc_state, CS_RESET);
		}
	}
	cfxga_stop_memory_blt(sc);
	return (0);
}

/*
 * Display a character.
 */
int
cfxga_expand_char(struct cfxga_screen *scr, u_int uc, int x, int y,
    uint32_t attr)
{
	struct cfxga_softc *sc = scr->scr_sc;
	struct rasops_info *ri = &scr->scr_ri;
	struct wsdisplay_font *font = ri->ri_font;
	u_int pos, sts, fifo_avail, chunk;
	u_int8_t *fontbits;
	int bg, fg, ul;
	u_int i;
	int rc;

	pos = (y * ri->ri_width + x) * ri->ri_depth / 8;
	fontbits = (u_int8_t *)(font->data + (uc - font->firstchar) *
	    ri->ri_fontscale);
	ri->ri_ops.unpack_attr(ri, attr, &fg, &bg, &ul);

	/* Wait for previous operations to complete */
	if ((rc = cfxga_synchronize(sc)) != 0)
		return (rc);

	cfxga_write_2(sc, CFREG_COLOR_EXPANSION,
	    ((font->fontwidth - 1) & 7) | (OP_COLOR_EXPANSION << 8));
	cfxga_write_2(sc, CFREG_BITBLT_SRC_LOW, font->fontwidth <= 8 ? 0 : 1);
	cfxga_write_2(sc, CFREG_BITBLT_SRC_HIGH, 0);
	cfxga_write_2(sc, CFREG_BITBLT_DST_LOW, pos);
	cfxga_write_2(sc, CFREG_BITBLT_DST_HIGH, pos >> 16);
	cfxga_write_2(sc, CFREG_BITBLT_OFFSET,
	    ri->ri_width * ri->ri_depth / 16);
	cfxga_write_2(sc, CFREG_BITBLT_WIDTH, font->fontwidth - 1);
	cfxga_write_2(sc, CFREG_BITBLT_HEIGHT, font->fontheight - 1);
	cfxga_write_2(sc, CFREG_BITBLT_FG, ri->ri_devcmap[fg]);
	cfxga_write_2(sc, CFREG_BITBLT_BG, ri->ri_devcmap[bg]);
	cfxga_write_2(sc, CFREG_BITBLT_CONTROL, BITBLT_ACTIVE |
	    (ri->ri_depth > 8 ? BITBLT_COLOR_16 : BITBLT_COLOR_8));

	if (cfxga_wait(sc, BITBLT_ACTIVE, BITBLT_ACTIVE) == 0)
		goto fail;	/* unlikely */
	fifo_avail = 0;

	for (i = font->fontheight; i != 0; i--) {
		/*
		 * Find out how much words we can feed before
		 * a FIFO check is needed.
		 */
		if (fifo_avail == 0) {
			sts = cfxga_read_1(sc, CFREG_BITBLT_CONTROL);
			if ((sts & BITBLT_FIFO_NOT_EMPTY) == 0)
				fifo_avail = font->fontwidth <= 8 ? 2 : 1;
			else if ((sts & BITBLT_FIFO_HALF_FULL) == 0)
				fifo_avail = font->fontwidth <= 8 ? 1 : 0;
			else {
				/*
				 * Let the cheap breathe for a short while.
				 * If this is not enough to free some FIFO
				 * entries, abort the operation.
				 */
				if (cfxga_wait(sc, BITBLT_FIFO_FULL, 0) == 0)
					goto fail;
			}
		}

		if (font->fontwidth <= 8) {
			chunk = *fontbits;
			if (ul && i == 1)
				chunk = 0xff;
		} else {
			chunk = *(u_int16_t *)fontbits;
			if (ul && i == 1)
				chunk = 0xffff;
		}
		cfxga_write_2(sc, CFREG_BITBLT_DATA, chunk);
		fontbits += font->stride;
		fifo_avail--;
	}

	return (0);

fail:
	DPRINTF(("%s: abort\n", __func__));
	cfxga_write_2(sc, CFREG_BITBLT_CONTROL, 0);
	cfxga_stop_memory_blt(sc);
	return (EINTR);
}

/*
 * Copy a memory bitmap to the frame buffer.
 *
 * This is slow - we only use this to repaint the whole frame buffer on
 * screen switches.
 */
int
cfxga_repaint_screen(struct cfxga_screen *scr)
{
	struct wsdisplay_charcell *cell = scr->scr_mem;
	struct rasops_info *ri = &scr->scr_ri;
	int x, y, cx, cy, lx, ly;
	int fg, bg;
	int rc;

	cfxga_clear_screen(scr);

	cx = ri->ri_font->fontwidth;
	cy = ri->ri_font->fontheight;

	for (ly = 0, y = ri->ri_yorigin; ly < ri->ri_rows; ly++, y += cy) {
		for (lx = 0, x = ri->ri_xorigin; lx < ri->ri_cols;
		    lx++, x += cx) {
			if (cell->uc == 0 || cell->uc == ' ') {
				ri->ri_ops.unpack_attr(ri, cell->attr,
				    &fg, &bg, NULL);
				rc = cfxga_solid_fill(scr, x, y, cx, cy,
				    ri->ri_devcmap[bg]);
			} else {
				rc = cfxga_expand_char(scr, cell->uc,
				    x, y, cell->attr);
			}
			cell++;
			if (rc != 0)
				return (rc);
		}
	}

	return (0);
}

/*
 * Perform a solid fill operation.
 */
int
cfxga_solid_fill(struct cfxga_screen *scr, int x, int y, int cx, int cy,
    int32_t srccolor)
{
	struct cfxga_softc *sc = scr->scr_sc;
	struct rasops_info *ri = &scr->scr_ri;
	u_int pos;
	int rc;

	pos = (y * ri->ri_width + x) * ri->ri_depth / 8;

	/* Wait for previous operations to complete */
	if ((rc = cfxga_synchronize(sc)) != 0)
		return (rc);

	cfxga_write_2(sc, CFREG_BITBLT_ROP, 0 | (OP_SOLID_FILL << 8));
	cfxga_write_2(sc, CFREG_BITBLT_SRC_LOW, pos);
	cfxga_write_2(sc, CFREG_BITBLT_SRC_HIGH, pos >> 16);
	cfxga_write_2(sc, CFREG_BITBLT_DST_LOW, pos);
	cfxga_write_2(sc, CFREG_BITBLT_DST_HIGH, pos >> 16);
	cfxga_write_2(sc, CFREG_BITBLT_OFFSET,
	    ri->ri_width * ri->ri_depth / 16);
	cfxga_write_2(sc, CFREG_BITBLT_WIDTH, cx - 1);
	cfxga_write_2(sc, CFREG_BITBLT_HEIGHT, cy - 1);
	cfxga_write_2(sc, CFREG_BITBLT_FG, (u_int16_t)srccolor);
	cfxga_write_2(sc, CFREG_BITBLT_CONTROL, BITBLT_ACTIVE |
	    (ri->ri_depth > 8 ? BITBLT_COLOR_16 : BITBLT_COLOR_8));

	return (0);
}

/*
 * Perform an internal frame buffer operation.
 */
int
cfxga_standalone_rop(struct cfxga_screen *scr, u_int rop, int sx, int sy,
    int dx, int dy, int cx, int cy)
{
	struct cfxga_softc *sc = scr->scr_sc;
	struct rasops_info *ri = &scr->scr_ri;
	u_int srcpos, dstpos;
	u_int opcode;
	int rc;

	srcpos = (sy * ri->ri_width + sx) * ri->ri_depth / 8;
	dstpos = (dy * ri->ri_width + dx) * ri->ri_depth / 8;

	if (dstpos <= srcpos)
		opcode = (OP_MOVE_POSITIVE_ROP << 8) | rop;
	else
		opcode = (OP_MOVE_NEGATIVE_ROP << 8) | rop;

	/* Wait for previous operations to complete */
	if ((rc = cfxga_synchronize(sc)) != 0)
		return (rc);

	cfxga_write_2(sc, CFREG_BITBLT_ROP, opcode);
	cfxga_write_2(sc, CFREG_BITBLT_SRC_LOW, srcpos);
	cfxga_write_2(sc, CFREG_BITBLT_SRC_HIGH, srcpos >> 16);
	cfxga_write_2(sc, CFREG_BITBLT_DST_LOW, dstpos);
	cfxga_write_2(sc, CFREG_BITBLT_DST_HIGH, dstpos >> 16);
	cfxga_write_2(sc, CFREG_BITBLT_OFFSET,
	    ri->ri_width * ri->ri_depth / 16);
	cfxga_write_2(sc, CFREG_BITBLT_WIDTH, cx - 1);
	cfxga_write_2(sc, CFREG_BITBLT_HEIGHT, cy - 1);
	cfxga_write_2(sc, CFREG_BITBLT_CONTROL, BITBLT_ACTIVE |
	    (ri->ri_depth > 8 ? BITBLT_COLOR_16 : BITBLT_COLOR_8));

	return (0);
}

/*
 * Text console raster operations.
 *
 * We shadow all these operations on a memory copy of the frame buffer.
 * Since we are running in emulation mode only, this could be optimized
 * by only storing actual character cell values (a la mda).
 */

int
cfxga_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct cfxga_screen *scr = ri->ri_hw;
	int sx, dx, y, cx, cy;

	/* Copy columns in backing store. */
	memmove(scr->scr_mem + row * ri->ri_cols + dst,
	    scr->scr_mem + row * ri->ri_cols + src,
	    num * sizeof(struct wsdisplay_charcell));

	if (scr != scr->scr_sc->sc_active)
		return 0;

	sx = src * ri->ri_font->fontwidth + ri->ri_xorigin;
	dx = dst * ri->ri_font->fontwidth + ri->ri_xorigin;
	y = row * ri->ri_font->fontheight + ri->ri_yorigin;
	cx = num * ri->ri_font->fontwidth;
	cy = ri->ri_font->fontheight;
	return cfxga_standalone_rop(scr, ROP_SRC, sx, y, dx, y, cx, cy);
}

int
cfxga_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct cfxga_screen *scr = ri->ri_hw;
	int x, sy, dy, cx, cy;

	/* Copy rows in backing store. */
	memmove(scr->scr_mem + dst * ri->ri_cols,
	    scr->scr_mem + src * ri->ri_cols,
	    num * ri->ri_cols * sizeof(struct wsdisplay_charcell));

	if (scr != scr->scr_sc->sc_active)
		return 0;

	x = ri->ri_xorigin;
	sy = src * ri->ri_font->fontheight + ri->ri_yorigin;
	dy = dst * ri->ri_font->fontheight + ri->ri_yorigin;
	cx = ri->ri_emuwidth;
	cy = num * ri->ri_font->fontheight;
	return cfxga_standalone_rop(scr, ROP_SRC, x, sy, x, dy, cx, cy);
}

int
cfxga_do_cursor(struct rasops_info *ri)
{
	struct cfxga_screen *scr = ri->ri_hw;
	int x, y, cx, cy;

	if (scr != scr->scr_sc->sc_active)
		return 0;

	x = ri->ri_ccol * ri->ri_font->fontwidth + ri->ri_xorigin;
	y = ri->ri_crow * ri->ri_font->fontheight + ri->ri_yorigin;
	cx = ri->ri_font->fontwidth;
	cy = ri->ri_font->fontheight;
	return cfxga_standalone_rop(scr, ROP_ONES ^ ROP_SRC /* i.e. not SRC */,
	    x, y, x, y, cx, cy);
}

int
cfxga_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct cfxga_screen *scr = ri->ri_hw;
	int fg, bg;
	int x, y, cx, cy;

	/* Erase columns in backing store. */
	for (x = col; x < col + num; x++) {
		scr->scr_mem[row * ri->ri_cols + x].uc = 0;
		scr->scr_mem[row * ri->ri_cols + x].attr = attr;
	}

	if (scr != scr->scr_sc->sc_active)
		return 0;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	x = col * ri->ri_font->fontwidth + ri->ri_xorigin;
	y = row * ri->ri_font->fontheight + ri->ri_yorigin;
	cx = num * ri->ri_font->fontwidth;
	cy = ri->ri_font->fontheight;
	return cfxga_solid_fill(scr, x, y, cx, cy, ri->ri_devcmap[bg]);
}

int
cfxga_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct cfxga_screen *scr = ri->ri_hw;
	int fg, bg;
	int x, y, cx, cy;

	/* Erase rows in backing store. */
	for (x = 0; x < ri->ri_cols; x++) {
		scr->scr_mem[row * ri->ri_cols + x].uc = 0;
		scr->scr_mem[row * ri->ri_cols + x].attr = attr;
	}
	for (y = 1; y < num; y++)
		memmove(scr->scr_mem + (row + y) * ri->ri_cols,
		    scr->scr_mem + row * ri->ri_cols,
		    ri->ri_cols * sizeof(struct wsdisplay_charcell));

	if (scr != scr->scr_sc->sc_active)
		return 0;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	x = ri->ri_xorigin;
	y = row * ri->ri_font->fontheight + ri->ri_yorigin;
	cx = ri->ri_emuwidth;
	cy = num * ri->ri_font->fontheight;
	return cfxga_solid_fill(scr, x, y, cx, cy, ri->ri_devcmap[bg]);
}

int
cfxga_putchar(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct cfxga_screen *scr = ri->ri_hw;
	int x, y;

	scr->scr_mem[row * ri->ri_cols + col].uc = uc;
	scr->scr_mem[row * ri->ri_cols + col].attr = attr;

	if (scr != scr->scr_sc->sc_active)
		return 0;

	x = col * ri->ri_font->fontwidth + ri->ri_xorigin;
	y = row * ri->ri_font->fontheight + ri->ri_yorigin;

	if (uc == ' ') {
		int cx, cy, fg, bg;

		ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
		cx = ri->ri_font->fontwidth;
		cy = ri->ri_font->fontheight;
		return cfxga_solid_fill(scr, x, y, cx, cy, ri->ri_devcmap[bg]);
	} else {
		return cfxga_expand_char(scr, uc, x, y, attr);
	}
}
