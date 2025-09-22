/* $OpenBSD: tgavar.h,v 1.9 2006/12/17 22:18:16 miod Exp $ */
/* $NetBSD: tgavar.h,v 1.8 2000/04/02 19:01:11 nathanw Exp $ */

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

#include <dev/ic/ramdac.h>
#include <dev/pci/tgareg.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

struct tga_devconfig;
struct fbcmap;
struct fbcursor;
struct fbcurpos;

struct tga_conf {
	char	    *tgac_name;		/* name for this board type */

	struct ramdac_funcs *(*ramdac_funcs)(void);

	int	    tgac_phys_depth;	/* physical frame buffer depth */
	vsize_t   tgac_cspace_size;	/* core space size */
	vsize_t   tgac_vvbr_units;	/* what '1' in the VVBR means */

	int	    tgac_ndbuf;		/* number of display buffers */
	vaddr_t tgac_dbuf[2];		/* display buffer offsets/addresses */
	vsize_t   tgac_dbufsz[2];	/* display buffer sizes */

	int	    tgac_nbbuf;		/* number of display buffers */
	vaddr_t tgac_bbuf[2];		/* back buffer offsets/addresses */
	vsize_t   tgac_bbufsz[2];	/* back buffer sizes */
};

struct tga_devconfig {
	bus_space_tag_t dc_memt;
	bus_space_handle_t dc_memh;

	pcitag_t   	 dc_pcitag;	/* PCI tag */
	bus_addr_t	 dc_pcipaddr;	/* PCI phys addr. */

	bus_space_handle_t dc_regs;	/* registers; XXX: need aliases */

	int	    dc_tga_type;	/* the device type; see below */
	int	    dc_tga2;		/* True if it is a TGA2 */
	const struct tga_conf *dc_tgaconf; /* device buffer configuration */

	struct ramdac_funcs
		    *dc_ramdac_funcs;	/* The RAMDAC functions */
	struct ramdac_cookie
		    *dc_ramdac_cookie;	/* the RAMDAC type; see above */

	vaddr_t dc_vaddr;		/* memory space virtual base address */
	paddr_t dc_paddr;		/* memory space physical base address */

	int	    dc_wid;		/* width of frame buffer */
	int	    dc_ht;		/* height of frame buffer */
	int	    dc_rowbytes;	/* bytes in a FB scan line */

	vaddr_t dc_videobase;		/* base of flat frame buffer */

	struct rasops_info dc_rinfo;	/* raster display data */

	int	    dc_blanked;		/* currently had video disabled */
	void	    *dc_ramdac_private; /* RAMDAC private storage */

	void	    (*dc_ramdac_intr)(void *);
	int	    dc_intrenabled;	/* can we depend on interrupts yet? */
};
	
struct tga_softc {
	struct	device sc_dev;

	struct	tga_devconfig *sc_dc;	/* device configuration */
	void	*sc_intr;		/* interrupt handler info */
	u_int	sc_mode;	        /* wscons mode used */
	/* XXX should record intr fns/arg */

	int nscreens;
};

#define	TGA_TYPE_T8_01		0	/* 8bpp, 1MB */
#define	TGA_TYPE_T8_02		1	/* 8bpp, 2MB */
#define	TGA_TYPE_T8_22		2	/* 8bpp, 4MB */
#define	TGA_TYPE_T8_44		3	/* 8bpp, 8MB */
#define	TGA_TYPE_T32_04		4	/* 32bpp, 4MB */
#define	TGA_TYPE_T32_08		5	/* 32bpp, 8MB */
#define	TGA_TYPE_T32_88		6	/* 32bpp, 16MB */
#define	TGA_TYPE_POWERSTORM_4D20	7	/* unknown */
#define	TGA_TYPE_UNKNOWN	8	/* unknown */

#define	DEVICE_IS_TGA(class, id)					\
	    (((PCI_VENDOR(id) == PCI_VENDOR_DEC &&			\
	       PCI_PRODUCT(id) == PCI_PRODUCT_DEC_21030) ||		\
	       PCI_PRODUCT(id) == PCI_PRODUCT_DEC_PBXGB) ? 10 : 0)

int tga_cnattach(bus_space_tag_t, bus_space_tag_t, pci_chipset_tag_t,
		      int, int, int);

int	tga_identify(struct tga_devconfig *);
const struct tga_conf *tga_getconf(int);

int     tga_builtin_set_cursor(struct tga_devconfig *,
	    struct wsdisplay_cursor *);
int     tga_builtin_get_cursor(struct tga_devconfig *,
	    struct wsdisplay_cursor *);
int     tga_builtin_set_curpos(struct tga_devconfig *,
	    struct wsdisplay_curpos *);
int     tga_builtin_get_curpos(struct tga_devconfig *,
	    struct wsdisplay_curpos *);
int     tga_builtin_get_curmax(struct tga_devconfig *,
	    struct wsdisplay_curpos *);

/* Read a TGA register */
#define TGARREG(dc,reg) (bus_space_read_4((dc)->dc_memt, (dc)->dc_regs, \
	(reg) << 2))

/* Write a TGA register */
#define TGAWREG(dc,reg,val) bus_space_write_4((dc)->dc_memt, (dc)->dc_regs, \
	(reg) << 2, (val))

/* Write a TGA register at an alternate aliased location */
#define TGAWALREG(dc,reg,alias,val) bus_space_write_4( \
	(dc)->dc_memt, (dc)->dc_regs, \
	((alias) * TGA_CREGS_ALIAS) + ((reg) << 2), \
	(val))

/* Insert a write barrier */
#define TGAREGWB(dc,reg, nregs) bus_space_barrier( \
	(dc)->dc_memt, (dc)->dc_regs, \
	((reg) << 2), 4 * (nregs), BUS_SPACE_BARRIER_WRITE)

/* Insert a read barrier */
#define TGAREGRB(dc,reg, nregs) bus_space_barrier( \
	(dc)->dc_memt, (dc)->dc_regs, \
	((reg) << 2), 4 * (nregs), BUS_SPACE_BARRIER_READ)

/* Insert a read/write barrier */
#define TGAREGRWB(dc,reg, nregs) bus_space_barrier( \
	(dc)->dc_memt, (dc)->dc_regs, \
	((reg) << 2), 4 * (nregs), \
	BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)
