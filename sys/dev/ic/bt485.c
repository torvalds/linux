/* $OpenBSD: bt485.c,v 1.16 2025/06/28 16:04:10 miod Exp $ */
/* $NetBSD: bt485.c,v 1.2 2000/04/02 18:55:01 nathanw Exp $ */

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
  *	 NetBSD: tga_bt485.c,v 1.4 1999/03/24 05:51:21 mrg Exp 
  */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <dev/pci/pcivar.h>
#include <dev/ic/bt485reg.h>
#include <dev/ic/bt485var.h>
#include <dev/ic/ramdac.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

/*
 * Functions exported via the RAMDAC configuration table.
 */
void	bt485_init(struct ramdac_cookie *);
int	bt485_set_cmap(struct ramdac_cookie *,
	    struct wsdisplay_cmap *);
int	bt485_get_cmap(struct ramdac_cookie *,
	    struct wsdisplay_cmap *);
int	bt485_set_cursor(struct ramdac_cookie *,
	    struct wsdisplay_cursor *);
int	bt485_get_cursor(struct ramdac_cookie *,
	    struct wsdisplay_cursor *);
int	bt485_set_curpos(struct ramdac_cookie *,
	    struct wsdisplay_curpos *);
int	bt485_get_curpos(struct ramdac_cookie *,
	    struct wsdisplay_curpos *);
int	bt485_get_curmax(struct ramdac_cookie *,
	    struct wsdisplay_curpos *);

/* XXX const */
struct ramdac_funcs bt485_funcsstruct = {
	"Bt485",
	bt485_register,
	bt485_init,
	bt485_set_cmap,
	bt485_get_cmap,
	bt485_set_cursor,
	bt485_get_cursor,
	bt485_set_curpos,
	bt485_get_curpos,
	bt485_get_curmax,
	NULL,			/* check_curcmap; not needed */
	NULL,			/* set_curcmap; not needed */
	NULL,			/* get_curcmap; not needed */
	NULL,			/* no dot clock to set */
};

/*
 * Private data.
 */
struct bt485data {
	void            *cookie;        /* This is what is passed
					 * around, and is probably
					 * struct tga_devconfig *
					 */
	
	int             (*ramdac_sched_update)(void *, void (*)(void *));
	void            (*ramdac_wr)(void *, u_int, u_int8_t);
	u_int8_t        (*ramdac_rd)(void *, u_int);

	int	changed;			/* what changed; see below */
	int	curenb;				/* cursor enabled */
	struct wsdisplay_curpos curpos;		/* current cursor position */
	struct wsdisplay_curpos curhot;		/* cursor hotspot */
	char curcmap_r[2];			/* cursor colormap */
	char curcmap_g[2];
	char curcmap_b[2];
	struct wsdisplay_curpos cursize;	/* current cursor size */
	char curimage[512];			/* cursor image data */
	char curmask[512];			/* cursor mask data */
	char cmap_r[256];				/* colormap */
	char cmap_g[256];
	char cmap_b[256];
};

#define	DATA_ENB_CHANGED	0x01	/* cursor enable changed */
#define	DATA_CURCMAP_CHANGED	0x02	/* cursor colormap changed */
#define	DATA_CURSHAPE_CHANGED	0x04	/* cursor size, image, mask changed */
#define	DATA_CMAP_CHANGED	0x08	/* colormap changed */
#define	DATA_ALL_CHANGED	0x0f

#define	CURSOR_MAX_SIZE		64

/*
 * Internal functions.
 */
inline void	bt485_wr_i(struct bt485data *, u_int8_t, u_int8_t);
inline u_int8_t bt485_rd_i(struct bt485data *, u_int8_t);
void	bt485_update(void *);
void	bt485_update_curpos(struct bt485data *);

/*****************************************************************************/

/*
 * Functions exported via the RAMDAC configuration table.
 */

struct ramdac_funcs *
bt485_funcs(void)
{
	return &bt485_funcsstruct;
}

struct ramdac_cookie *
bt485_register(void *v, int (*sched_update)(void *, void (*)(void *)),
    void (*wr)(void *, u_int, u_int8_t), u_int8_t (*rd)(void *, u_int))
{
	struct bt485data *data;
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
bt485_cninit(void *v, int (*sched_update)(void *, void (*)(void *)),
    void (*wr)(void *, u_int, u_int8_t), u_int8_t (*rd)(void *, u_int))
{
	struct bt485data tmp, *data = &tmp;
	data->cookie = v;
	data->ramdac_sched_update = sched_update;
	data->ramdac_wr = wr;
	data->ramdac_rd = rd;
	bt485_init((struct ramdac_cookie *)data);
}

void
bt485_init(struct ramdac_cookie *rc)
{
	u_int8_t regval;
	struct bt485data *data = (struct bt485data *)rc;
	int i;

	/*
	 * Init the BT485 for normal operation.
	 */

	/*
	 * Allow indirect register access.  (Actually, this is
	 * already enabled.  In fact, if it is _disabled_, for
	 * some reason the monitor appears to lose sync!!! (?!?!)
	 */
	regval = data->ramdac_rd(data->cookie, BT485_REG_COMMAND_0);
	regval |= 0x80;
	/*
	 * Set the RAMDAC to 8 bit resolution, rather than 6 bit
	 * resolution.
	 */
	regval |= 0x02;
	data->ramdac_wr(data->cookie, BT485_REG_COMMAND_0, regval);

	/* Set the RAMDAC to 8BPP (no interesting options). */
	data->ramdac_wr(data->cookie, BT485_REG_COMMAND_1, 0x40);

	/* Disable the cursor (for now) */
	regval = data->ramdac_rd(data->cookie, BT485_REG_COMMAND_2);
	regval &= ~0x03;
	regval |= 0x24;
	data->ramdac_wr(data->cookie, BT485_REG_COMMAND_2, regval);

	/* Use a 64x64x2 cursor */
	regval = bt485_rd_i(data, BT485_IREG_COMMAND_3);
	regval |= 0x04;
	regval |= 0x08;
	bt485_wr_i(data, BT485_IREG_COMMAND_3, regval);

	/* Set the Pixel Mask to something useful */
	data->ramdac_wr(data->cookie, BT485_REG_PIXMASK, 0xff);

	/*
	 * Initialize the RAMDAC info struct to hold all of our
	 * data, and fill it in.
	 */
	data->changed = DATA_ALL_CHANGED;

	data->curenb = 0;				/* cursor disabled */
	data->curpos.x = data->curpos.y = 0;		/* right now at 0,0 */
	data->curhot.x = data->curhot.y = 0;		/* hot spot at 0,0 */

	/* initial cursor colormap: 0 is black, 1 is white */
	data->curcmap_r[0] = data->curcmap_g[0] = data->curcmap_b[0] = 0;
	data->curcmap_r[1] = data->curcmap_g[1] = data->curcmap_b[1] = 0xff;

	/* initial cursor data: 64x64 block of white. */
	data->cursize.x = data->cursize.y = 64;
	for (i = 0; i < 512; i++)
		data->curimage[i] = data->curmask[i] = 0xff;

	/* Initial colormap: 0 is black, everything else is white */
	data->cmap_r[0] = data->cmap_g[0] = data->cmap_b[0] = 0;
	for (i = 0; i < 256; i++) {
		data->cmap_r[i] = rasops_cmap[3*i + 0];
		data->cmap_g[i] = rasops_cmap[3*i + 1];
		data->cmap_b[i] = rasops_cmap[3*i + 2];
	}

	bt485_update((void *)data);
}

int
bt485_set_cmap(struct ramdac_cookie *rc, struct wsdisplay_cmap *cmapp)
{
	struct bt485data *data = (struct bt485data *)rc;
	u_int count, index;
	int s, error;

#ifdef DIAGNOSTIC
	if (rc == NULL)
		panic("bt485_set_cmap: rc");
	if (cmapp == NULL)
		panic("bt485_set_cmap: cmapp");
#endif
	index = cmapp->index;
	count = cmapp->count;

	if (index >= 256 || count > 256 - index)
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

	data->ramdac_sched_update(data->cookie, bt485_update);
#ifdef __alpha__
	alpha_mb();
#endif
	splx(s);

	return (0);
}

int
bt485_get_cmap(struct ramdac_cookie *rc, struct wsdisplay_cmap *cmapp)
{
	struct bt485data *data = (struct bt485data *)rc;
	u_int count, index;
	int error;

	if (cmapp->index >= 256 || cmapp->count > 256 - cmapp->index)
		return (EINVAL);

	count = cmapp->count;
	index = cmapp->index;

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
bt485_set_cursor(struct ramdac_cookie *rc, struct wsdisplay_cursor *cursorp)
{
	struct bt485data *data = (struct bt485data *)rc;
	u_int count, index;
	int error;
	int v, s;

	v = cursorp->which;

	/*
	 * For DOCMAP and DOSHAPE, verify that parameters are OK
	 * before we do anything that we can't recover from.
	 */
	if (v & WSDISPLAY_CURSOR_DOCMAP) {
		index = cursorp->cmap.index;
		count = cursorp->cmap.count;
		if (index >= 2 || count > 2 - index)
			return (EINVAL);
	}
	if (v & WSDISPLAY_CURSOR_DOSHAPE) {
		if ((u_int)cursorp->size.x > CURSOR_MAX_SIZE ||
		    (u_int)cursorp->size.y > CURSOR_MAX_SIZE)
			return (EINVAL);
	}

	if (v & (WSDISPLAY_CURSOR_DOPOS | WSDISPLAY_CURSOR_DOCUR)) {
		if (v & WSDISPLAY_CURSOR_DOPOS)
			data->curpos = cursorp->pos;
		if (v & WSDISPLAY_CURSOR_DOCUR)
			data->curhot = cursorp->hot;
		bt485_update_curpos(data);
	}

	s = spltty();

	/* Parameters are OK; perform the requested operations. */
	if (v & WSDISPLAY_CURSOR_DOCUR) {
		data->curenb = cursorp->enable;
		data->changed |= DATA_ENB_CHANGED;
	}
	if (v & WSDISPLAY_CURSOR_DOCMAP) {
		index = cursorp->cmap.index;
		count = cursorp->cmap.count;
		if ((error = copyin(cursorp->cmap.red,
		    &data->curcmap_r[index], count)) != 0) {
			splx(s);
			return (error);
		}
		if ((error = copyin(cursorp->cmap.green,
		    &data->curcmap_g[index], count)) != 0) {
			splx(s);
			return (error);
		}
		if ((error = copyin(cursorp->cmap.blue,
		    &data->curcmap_b[index], count)) != 0) {
			splx(s);
			return (error);
		}
		data->changed |= DATA_CURCMAP_CHANGED;
	}
	if (v & WSDISPLAY_CURSOR_DOSHAPE) {
		data->cursize = cursorp->size;
		count = (CURSOR_MAX_SIZE / NBBY) * data->cursize.y;
		bzero(data->curimage, sizeof data->curimage);
		bzero(data->curmask, sizeof data->curmask);
		if ((error = copyin(cursorp->image, data->curimage,
		    count)) != 0) {
			splx(s);
			return (error);
		}
		if ((error = copyin(cursorp->mask, data->curmask,
		    count)) != 0) {
			splx(s);
			return (error);
		}
		data->changed |= DATA_CURSHAPE_CHANGED;
	}

	if (data->changed)
		data->ramdac_sched_update(data->cookie, bt485_update);
	splx(s);

	return (0);
}

int
bt485_get_cursor(struct ramdac_cookie *rc, struct wsdisplay_cursor *cursorp)
{
	struct bt485data *data = (struct bt485data *)rc;
	int error, count;

	/* we return everything they want */
	cursorp->which = WSDISPLAY_CURSOR_DOALL;

	cursorp->enable = data->curenb;	/* DOCUR */
	cursorp->pos = data->curpos;	/* DOPOS */
	cursorp->hot = data->curhot;	/* DOHOT */

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

	cursorp->size = data->cursize;	/* DOSHAPE */
	if (cursorp->image != NULL) {
		count = (CURSOR_MAX_SIZE / NBBY) * data->cursize.y;
		error = copyout(data->curimage, cursorp->image, count);
		if (error)
			return (error);
		error = copyout(data->curmask, cursorp->mask, count);
		if (error)
			return (error);
	}

	return (0);
}

int
bt485_set_curpos(struct ramdac_cookie *rc, struct wsdisplay_curpos *curposp)
{
	struct bt485data *data = (struct bt485data *)rc;

	data->curpos = *curposp;
	bt485_update_curpos(data);

	return (0);
}

int
bt485_get_curpos(struct ramdac_cookie *rc, struct wsdisplay_curpos *curposp)
{
	struct bt485data *data = (struct bt485data *)rc;

	*curposp = data->curpos;
	return (0);
}

int
bt485_get_curmax(struct ramdac_cookie *rc, struct wsdisplay_curpos *curposp)
{

	curposp->x = curposp->y = CURSOR_MAX_SIZE;
	return (0);
}

/*****************************************************************************/

/*
 * Internal functions.
 */

inline void
bt485_wr_i(struct bt485data *data, u_int8_t ireg, u_int8_t val)
{
	data->ramdac_wr(data->cookie, BT485_REG_PCRAM_WRADDR, ireg);
	data->ramdac_wr(data->cookie, BT485_REG_EXTENDED, val);
}

inline u_int8_t
bt485_rd_i(struct bt485data *data, u_int8_t ireg)
{
	data->ramdac_wr(data->cookie, BT485_REG_PCRAM_WRADDR, ireg);
	return (data->ramdac_rd(data->cookie, BT485_REG_EXTENDED));
}

void
bt485_update(void *vp)
{
	struct bt485data *data = vp;
	u_int8_t regval;
	int count, i, v;

	v = data->changed;
	data->changed = 0;

	if (v & DATA_ENB_CHANGED) {
		regval = data->ramdac_rd(data->cookie, BT485_REG_COMMAND_2);
		if (data->curenb)
			regval |= 0x01;
		else
			regval &= ~0x03;
                data->ramdac_wr(data->cookie, BT485_REG_COMMAND_2, regval);
	}

	if (v & DATA_CURCMAP_CHANGED) {
		/* addr[9:0] assumed to be 0 */
		/* set addr[7:0] to 1 */
                data->ramdac_wr(data->cookie, BT485_REG_COC_WRADDR, 0x01);

		/* spit out the cursor data */
		for (i = 0; i < 2; i++) {
                	data->ramdac_wr(data->cookie, BT485_REG_COCDATA,
			    data->curcmap_r[i]);
                	data->ramdac_wr(data->cookie, BT485_REG_COCDATA,
			    data->curcmap_g[i]);
                	data->ramdac_wr(data->cookie, BT485_REG_COCDATA,
			    data->curcmap_b[i]);
		}
	}

	if (v & DATA_CURSHAPE_CHANGED) {
		count = (CURSOR_MAX_SIZE / NBBY) * data->cursize.y;

		/*
		 * Write the cursor image data:
		 *	set addr[9:8] to 0,
		 *	set addr[7:0] to 0,
		 *	spit it all out.
		 */
		regval = bt485_rd_i(data, BT485_IREG_COMMAND_3);
		regval &= ~0x03;
		bt485_wr_i(data, BT485_IREG_COMMAND_3, regval);
                data->ramdac_wr(data->cookie, BT485_REG_PCRAM_WRADDR, 0);
		for (i = 0; i < count; i++)
			data->ramdac_wr(data->cookie, BT485_REG_CURSOR_RAM,
			    data->curimage[i]);
		
		/*
		 * Write the cursor mask data:
		 *	set addr[9:8] to 2,
		 *	set addr[7:0] to 0,
		 *	spit it all out.
		 */
		regval = bt485_rd_i(data, BT485_IREG_COMMAND_3);
		regval &= ~0x03; regval |= 0x02;
		bt485_wr_i(data, BT485_IREG_COMMAND_3, regval);
                data->ramdac_wr(data->cookie, BT485_REG_PCRAM_WRADDR, 0);
		for (i = 0; i < count; i++)
			data->ramdac_wr(data->cookie, BT485_REG_CURSOR_RAM,
			    data->curmask[i]);

		/* set addr[9:0] back to 0 */
		regval = bt485_rd_i(data, BT485_IREG_COMMAND_3);
		regval &= ~0x03;
		bt485_wr_i(data, BT485_IREG_COMMAND_3, regval);
	}

	if (v & DATA_CMAP_CHANGED) {
		/* addr[9:0] assumed to be 0 */
		/* set addr[7:0] to 0 */
                data->ramdac_wr(data->cookie, BT485_REG_PCRAM_WRADDR, 0x00);

		/* spit out the cursor data */
		for (i = 0; i < 256; i++) {
                	data->ramdac_wr(data->cookie, BT485_REG_PALETTE,
			    data->cmap_r[i]);
                	data->ramdac_wr(data->cookie, BT485_REG_PALETTE,
			    data->cmap_g[i]);
                	data->ramdac_wr(data->cookie, BT485_REG_PALETTE,
			    data->cmap_b[i]);
		}
	}
}

void
bt485_update_curpos(struct bt485data *data)
{
	void *cookie = data->cookie;
	int s, x, y;

	s = spltty();

	x = data->curpos.x + CURSOR_MAX_SIZE - data->curhot.x;
	y = data->curpos.y + CURSOR_MAX_SIZE - data->curhot.y;
	data->ramdac_wr(cookie, BT485_REG_CURSOR_X_LOW, x & 0xff);
	data->ramdac_wr(cookie, BT485_REG_CURSOR_X_HIGH, (x >> 8) & 0x0f);
	data->ramdac_wr(cookie, BT485_REG_CURSOR_Y_LOW, y & 0xff);
	data->ramdac_wr(cookie, BT485_REG_CURSOR_Y_HIGH, (y >> 8) & 0x0f);

	splx(s);
}
