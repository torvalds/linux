/* $OpenBSD: bt463.c,v 1.15 2025/06/28 16:04:10 miod Exp $ */
/* $NetBSD: bt463.c,v 1.2 2000/06/13 17:21:06 nathanw Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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

 /* This code was derived from and originally located in sys/dev/pci/
  *	 NetBSD: tga_bt463.c,v 1.5 2000/03/04 10:27:59 elric Exp 
  */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/tgareg.h>
#include <dev/pci/tgavar.h>
#include <dev/ic/bt463reg.h>
#include <dev/ic/bt463var.h>

#include <dev/wscons/wsconsio.h>

/*
 * Functions exported via the RAMDAC configuration table.
 */
void	bt463_init(struct ramdac_cookie *);
int	bt463_set_cmap(struct ramdac_cookie *,
	    struct wsdisplay_cmap *);
int	bt463_get_cmap(struct ramdac_cookie *,
	    struct wsdisplay_cmap *);
int	bt463_set_cursor(struct ramdac_cookie *,
	    struct wsdisplay_cursor *);
int	bt463_get_cursor(struct ramdac_cookie *,
	    struct wsdisplay_cursor *);
int	bt463_set_curpos(struct ramdac_cookie *,
	    struct wsdisplay_curpos *);
int	bt463_get_curpos(struct ramdac_cookie *,
	    struct wsdisplay_curpos *);
int	bt463_get_curmax(struct ramdac_cookie *,
	    struct wsdisplay_curpos *);
int	bt463_check_curcmap(struct ramdac_cookie *,
	    struct wsdisplay_cursor *cursorp);
void	bt463_set_curcmap(struct ramdac_cookie *,
	    struct wsdisplay_cursor *cursorp);
int	bt463_get_curcmap(struct ramdac_cookie *,
	    struct wsdisplay_cursor *cursorp);

#ifdef BT463_DEBUG
int bt463_store(void *);
int bt463_debug(void *);
int bt463_readback(void *);
void	bt463_copyback(void *);
#endif

struct ramdac_funcs bt463_funcsstruct = {
	"Bt463",
	bt463_register,
	bt463_init,
	bt463_set_cmap,
	bt463_get_cmap,
	bt463_set_cursor,
	bt463_get_cursor,
	bt463_set_curpos,
	bt463_get_curpos,
	bt463_get_curmax,
	bt463_check_curcmap,
	bt463_set_curcmap,
	bt463_get_curcmap,
	NULL,
};

/*
 * Private data.
 */
struct bt463data {
	void            *cookie;        /* This is what is passed
					 * around, and is probably
					 * struct tga_devconfig *
					 */
	
	int             (*ramdac_sched_update)(void *, void (*)(void *));
	void            (*ramdac_wr)(void *, u_int, u_int8_t);
	u_int8_t        (*ramdac_rd)(void *, u_int);

	int	changed;			/* what changed; see below */
	char curcmap_r[2];			/* cursor colormap */
	char curcmap_g[2];
	char curcmap_b[2];
	char cmap_r[BT463_NCMAP_ENTRIES];	/* colormap */
	char cmap_g[BT463_NCMAP_ENTRIES];
	char cmap_b[BT463_NCMAP_ENTRIES];
	int window_type[16]; /* 16 24-bit window type table entries */
};

/* When we're doing console initialization, there's no
 * way to get our cookie back to the video card's softc
 * before we call sched_update. So we stash it here, 
 * and bt463_update will look for it here first.
 */
static struct bt463data *console_data;


#define BTWREG(data, addr, val) do { bt463_wraddr((data), (addr)); \
	(data)->ramdac_wr((data)->cookie, BT463_REG_IREG_DATA, (val)); } while (0)
#define BTWNREG(data, val) (data)->ramdac_wr((data)->cookie, \
	BT463_REG_IREG_DATA, (val))
#define BTRREG(data, addr) (bt463_wraddr((data), (addr)), \
	(data)->ramdac_rd((data)->cookie, BT463_REG_IREG_DATA))
#define BTRNREG(data) ((data)->ramdac_rd((data)->cookie, BT463_REG_IREG_DATA))

#define	DATA_CURCMAP_CHANGED	0x01	/* cursor colormap changed */
#define	DATA_CMAP_CHANGED	0x02	/* colormap changed */
#define	DATA_WTYPE_CHANGED	0x04	/* window type table changed */
#define	DATA_ALL_CHANGED	0x07

/*
 * Internal functions.
 */
inline void bt463_wraddr(struct bt463data *, u_int16_t);

void	bt463_update(void *);

 
/*****************************************************************************/

/*
 * Functions exported via the RAMDAC configuration table.
 */

struct ramdac_funcs *
bt463_funcs(void)
{
	return &bt463_funcsstruct;
}

struct ramdac_cookie *
bt463_register(void *v, int (*sched_update)(void *, void (*)(void *)),
    void (*wr)(void *, u_int, u_int8_t), u_int8_t (*rd)(void *, u_int))
{
	struct bt463data *data;
	/*
	 * XXX -- comment out of date.  rcd.
	 * If we should allocate a new private info struct, do so.
	 * Otherwise, use the one we have (if it's there), or
	 * use the temporary one on the stack.
	 */
	data = malloc(sizeof *data, M_DEVBUF, M_WAITOK);
	/* XXX -- if !data */
	data->cookie = v;
	data->ramdac_sched_update = sched_update;
	data->ramdac_wr = wr;
	data->ramdac_rd = rd;
	return (struct ramdac_cookie *)data;
}

/*
 * This function exists solely to provide a means to init
 * the RAMDAC without first registering.  It is useful for
 * initializing the console early on.
 */
void
bt463_cninit(void *v, int (*sched_update)(void *, void (*)(void *)),
    void (*wr)(void *, u_int, u_int8_t), u_int8_t (*rd)(void *, u_int))
{
	struct bt463data tmp, *data = &tmp;
	data->cookie = v;
	data->ramdac_sched_update = sched_update;
	data->ramdac_wr = wr;
	data->ramdac_rd = rd;
	/* Set up console_data so that when bt463_update is called back,
	 * it can use the right information.
	 */
	console_data = data;
	bt463_init((struct ramdac_cookie *)data);
	console_data = NULL;
}

void
bt463_init(struct ramdac_cookie *rc)
{
	struct bt463data *data = (struct bt463data *)rc;

	int i;

	/*
	 * Init the BT463 for normal operation.
	 */


	/*
	 * Setup:
	 * reg 0: 4:1 multiplexing, 25/75 blink.
	 * reg 1: Overlay mapping: mapped to common palette, 
	 *        14 window type entries, 24-plane configuration mode,
	 *        4 overlay planes, underlays disabled, no cursor. 
	 * reg 2: sync-on-green enabled, pedestal enabled.
	 */

	BTWREG(data, BT463_IREG_COMMAND_0, 0x40);
	BTWREG(data, BT463_IREG_COMMAND_1, 0x48);
	BTWREG(data, BT463_IREG_COMMAND_2, 0xC0);

	/*
	 * Initialize the read mask.
	 */
	bt463_wraddr(data, BT463_IREG_READ_MASK_P0_P7);
	for (i = 0; i < 4; i++)
		BTWNREG(data, 0xff);

	/*
	 * Initialize the blink mask.
	 */
	bt463_wraddr(data, BT463_IREG_BLINK_MASK_P0_P7);
	for (i = 0; i < 4; i++)
		BTWNREG(data, 0);


	/*
	 * Clear test register
	 */
	BTWREG(data, BT463_IREG_TEST, 0);

	/*
	 * Initialize the RAMDAC info struct to hold all of our
	 * data, and fill it in.
	 */
	data->changed = DATA_ALL_CHANGED;

	/* initial cursor colormap: 0 is black, 1 is white */
	data->curcmap_r[0] = data->curcmap_g[0] = data->curcmap_b[0] = 0;
	data->curcmap_r[1] = data->curcmap_g[1] = data->curcmap_b[1] = 0xff;

	/* Initial colormap: 0 is black, everything else is white */
	data->cmap_r[0] = data->cmap_g[0] = data->cmap_b[0] = 0;
	for (i = 1; i < 256; i++) {
		data->cmap_r[i] = rasops_cmap[3*i + 0];
		data->cmap_g[i] = rasops_cmap[3*i + 1];
		data->cmap_b[i] = rasops_cmap[3*i + 2];
	}

	/* Initialize the window type table:
	 *
	 * Entry 0: 24-plane truecolor, overlays enabled, bypassed.
	 *
	 *  Lookup table bypass:      yes (    1 << 23 & 0x800000)  800000
	 *  Colormap address:       0x000 (0x000 << 17 & 0x7e0000)       0 
	 *  Overlay mask:             0xf (  0xf << 13 & 0x01e000)   1e000
	 *  Overlay location:    P<27:24> (    0 << 12 & 0x001000)       0
	 *  Display mode:       Truecolor (    0 <<  9 & 0x000e00)     000
	 *  Number of planes:           8 (    8 <<  5 & 0x0001e0)     100
	 *  Plane shift:                0 (    0 <<  0 & 0x00001f)       0
	 *                                                        --------
	 *                                                        0x81e100
	 */	  
	data->window_type[0] = 0x81e100;

	/* Entry 1: 8-plane pseudocolor in the bottom 8 bits, 
	 *          overlays enabled, colormap starting at 0. 
	 *
	 *  Lookup table bypass:       no (    0 << 23 & 0x800000)       0
	 *  Colormap address:       0x000 (0x000 << 17 & 0x7e0000)       0 
	 *  Overlay mask:             0xf (  0xf << 13 & 0x01e000) 0x1e000
	 *  Overlay location:    P<27:24> (    0 << 12 & 0x001000)       0
	 *  Display mode:     Pseudocolor (    1 <<  9 & 0x000e00)   0x200
	 *  Number of planes:           8 (    8 <<  5 & 0x0001e0)   0x100
	 *  Plane shift:               16 ( 0x10 <<  0 & 0x00001f)      10
	 *                                                        --------
	 *                                                        0x01e310
	 */	  
	data->window_type[1] = 0x01e310;

	/* The colormap interface to the world only supports one colormap, 
	 * so having an entry for the 'alternate' colormap in the bt463 
	 * probably isn't useful.
	 */

	/* Fill the remaining table entries with clones of entry 0 until we 
	 * figure out a better use for them.
	 */

	for (i = 2; i < BT463_NWTYPE_ENTRIES; i++) {
		data->window_type[i] = 0x81e100;
	}

	data->ramdac_sched_update(data->cookie, bt463_update);

}

int
bt463_set_cmap(struct ramdac_cookie *rc, struct wsdisplay_cmap *cmapp)
{
	struct bt463data *data = (struct bt463data *)rc;
	u_int count, index;
	int s, error;

	index = cmapp->index;
	count = cmapp->count;

	if (index >= BT463_NCMAP_ENTRIES || count > BT463_NCMAP_ENTRIES - index)
		return (EINVAL);

	s = spltty();

	if ((error = copyin(cmapp->red, &data->cmap_r[index], count)) != 0) {
		splx(s);
		return (error);
	}
	if ((error = copyin(cmapp->green, &data->cmap_g[index], count)) != 0) {
		splx(s);
		return (error);
	}
	if ((error = copyin(cmapp->blue, &data->cmap_b[index], count)) != 0) {
		splx(s);
		return (error);
	}

	data->changed |= DATA_CMAP_CHANGED;

	data->ramdac_sched_update(data->cookie, bt463_update);
	splx(s);

	return (0);
}

int
bt463_get_cmap(struct ramdac_cookie *rc, struct wsdisplay_cmap *cmapp)
{
	struct bt463data *data = (struct bt463data *)rc;
	u_int count, index;
	int error;

	count = cmapp->count;
	index = cmapp->index;

	if (index >= BT463_NCMAP_ENTRIES || count > BT463_NCMAP_ENTRIES - index)
		return (EINVAL);

	error = copyout(&data->cmap_r[index], cmapp->red, count);
	if (error)
		return (error);
	error = copyout(&data->cmap_g[index], cmapp->green, count);
	if (error)
		return (error);
	error = copyout(&data->cmap_b[index], cmapp->blue, count);
	return (error);
}

int
bt463_check_curcmap(struct ramdac_cookie *rc, struct wsdisplay_cursor *cursorp)
{
	u_int index, count;
	u_int8_t spare[2];
	int error;

	index = cursorp->cmap.index;
	count = cursorp->cmap.count;

	if (index >= 2 || count > 2 - index)
		return (EINVAL);

	if ((error = copyin(&cursorp->cmap.red, &spare, count)) != 0)
		return (error);
	if ((error = copyin(&cursorp->cmap.green, &spare, count)) != 0)
		return (error);
	if ((error = copyin(&cursorp->cmap.blue, &spare, count)) != 0)
		return (error);

	return (0);
}

void
bt463_set_curcmap(struct ramdac_cookie *rc, struct wsdisplay_cursor *cursorp)
{
	struct bt463data *data = (struct bt463data *)rc;
	int count, index;

	/* can't fail; parameters have already been checked. */
	count = cursorp->cmap.count;
	index = cursorp->cmap.index;
	copyin(cursorp->cmap.red, &data->curcmap_r[index], count);
	copyin(cursorp->cmap.green, &data->curcmap_g[index], count);
	copyin(cursorp->cmap.blue, &data->curcmap_b[index], count);
	data->changed |= DATA_CURCMAP_CHANGED;
	data->ramdac_sched_update(data->cookie, bt463_update);
}

int
bt463_get_curcmap(struct ramdac_cookie *rc, struct wsdisplay_cursor *cursorp)
{
	struct bt463data *data = (struct bt463data *)rc;
	int error;

	cursorp->cmap.index = 0;	/* DOCMAP */
	cursorp->cmap.count = 2;
	if (cursorp->cmap.red != NULL) {
		error = copyout(data->curcmap_r, cursorp->cmap.red, 2);
		if (error)
			return (error);
	}
	if (cursorp->cmap.green != NULL) {
		error = copyout(data->curcmap_g, cursorp->cmap.green, 2);
		if (error)
			return (error);
	}
	if (cursorp->cmap.blue != NULL) {
		error = copyout(data->curcmap_b, cursorp->cmap.blue, 2);
		if (error)
			return (error);
	}
	return (0);
}


/*****************************************************************************/

/*
 * Internal functions.
 */

#ifdef BT463_DEBUG
int
bt463_store(void *v)
{
	struct bt463data *data = (struct bt463data *)v;	

	data->changed = DATA_ALL_CHANGED;
	data->ramdac_sched_update(data->cookie, bt463_update);
	printf("Scheduled bt463 store\n");

	return 0;
}


int
bt463_readback(void *v)
{
	struct bt463data *data = (struct bt463data *)v;	

	data->ramdac_sched_update(data->cookie, bt463_copyback);
	printf("Scheduled bt463 copyback\n");
	return 0;
}

int
bt463_debug(void *v)
{
	struct bt463data *data = (struct bt463data *)v;
	int i;
	u_int8_t val;

	printf("BT463 main regs:\n");
	for (i = 0x200; i < 0x20F; i ++) {
	  val = BTRREG(data, i);
	  printf("  $%04x %02x\n", i, val);
	}

	printf("BT463 revision register:\n");
	  val = BTRREG(data, 0x220);
	  printf("  $%04x %02x\n", 0x220, val);

	printf("BT463 window type table (from softc):\n");

	for (i = 0; i < BT463_NWTYPE_ENTRIES; i++) {
	  printf("%02x %06x\n", i, data->window_type[i]);
	}

	return 0;
}

void 
bt463_copyback(void *p)
{
	struct bt463data *data = (struct bt463data *)p;
	int i;

		for (i = 0; i < BT463_NWTYPE_ENTRIES; i++) {
			bt463_wraddr(data, BT463_IREG_WINDOW_TYPE_TABLE + i);
			data->window_type[i] = (BTRNREG(data) & 0xff);        /* B0-7   */
			data->window_type[i] |= (BTRNREG(data) & 0xff) << 8;  /* B8-15  */
			data->window_type[i] |= (BTRNREG(data) & 0xff) << 16; /* B16-23 */
		}
}
#endif

inline void
bt463_wraddr(struct bt463data *data, u_int16_t ireg)
{
	data->ramdac_wr(data->cookie, BT463_REG_ADDR_LOW, ireg & 0xff);
	data->ramdac_wr(data->cookie, BT463_REG_ADDR_HIGH, (ireg >> 8) & 0xff);
}

void
bt463_update(void *p)
{
	struct bt463data *data = (struct bt463data *)p;
	int i, v;

	if (console_data != NULL) {
		/* The cookie passed in from sched_update is incorrect. Use the
		 * right one.
		 */
		data = console_data;
	}

	v = data->changed;

	/* The Bt463 won't accept window type data except during a blanking
	 * interval, so we do this early in the interrupt.
	 * Blanking the screen might also be a good idea, but it can cause 
	 * unpleasant flashing and is hard to do from this side of the
	 * ramdac interface.
	 */
	if (v & DATA_WTYPE_CHANGED) {
		/* spit out the window type data */
		for (i = 0; i < BT463_NWTYPE_ENTRIES; i++) {
			bt463_wraddr(data, BT463_IREG_WINDOW_TYPE_TABLE + i);
			BTWNREG(data, (data->window_type[i]) & 0xff);       /* B0-7   */
			BTWNREG(data, (data->window_type[i] >> 8) & 0xff);  /* B8-15   */
			BTWNREG(data, (data->window_type[i] >> 16) & 0xff); /* B16-23  */
		}
	}
	
	if (v & DATA_CURCMAP_CHANGED) {
		bt463_wraddr(data, BT463_IREG_CURSOR_COLOR_0);
		/* spit out the cursor data */
		for (i = 0; i < 2; i++) {
			BTWNREG(data, data->curcmap_r[i]);
			BTWNREG(data, data->curcmap_g[i]);
			BTWNREG(data, data->curcmap_b[i]);
		}
	}
	
	if (v & DATA_CMAP_CHANGED) {
		bt463_wraddr(data, BT463_IREG_CPALETTE_RAM);
		/* spit out the colormap data */
		for (i = 0; i < BT463_NCMAP_ENTRIES; i++) {
			data->ramdac_wr(data->cookie, BT463_REG_CMAP_DATA, 
				data->cmap_r[i]);
			data->ramdac_wr(data->cookie, BT463_REG_CMAP_DATA, 
				data->cmap_g[i]);
			data->ramdac_wr(data->cookie, BT463_REG_CMAP_DATA, 
				data->cmap_b[i]);
		}
	}

	data->changed = 0;
}

int
bt463_set_cursor(struct ramdac_cookie *rc, struct wsdisplay_cursor *cur)
{
	struct bt463data *data = (struct bt463data *)rc;
	return tga_builtin_set_cursor(data->cookie, cur);
}

int
bt463_get_cursor(struct ramdac_cookie *rc, struct wsdisplay_cursor *cur)
{
	struct bt463data *data = (struct bt463data *)rc;
	return tga_builtin_get_cursor(data->cookie, cur);
}

int
bt463_set_curpos(struct ramdac_cookie *rc, struct wsdisplay_curpos *cur)
{
	struct bt463data *data = (struct bt463data *)rc;
	return tga_builtin_set_curpos(data->cookie, cur);
}

int
bt463_get_curpos(struct ramdac_cookie *rc, struct wsdisplay_curpos *cur)
{
	struct bt463data *data = (struct bt463data *)rc;
	return tga_builtin_get_curpos(data->cookie, cur);
}

int
bt463_get_curmax(struct ramdac_cookie *rc, struct wsdisplay_curpos *cur)
{
	struct bt463data *data = (struct bt463data *)rc;
	return tga_builtin_get_curmax(data->cookie, cur);
}
