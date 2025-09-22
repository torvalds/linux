/* $NetBSD: ibm561.c,v 1.1 2001/12/12 07:46:48 elric Exp $ */
/* $OpenBSD: ibm561.c,v 1.8 2025/06/28 16:04:10 miod Exp $ */

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Roland C. Dowdeswell of Ponte, Inc.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <dev/pci/pcivar.h>
#include <dev/ic/ibm561reg.h>
#include <dev/ic/ibm561var.h>
#include <dev/ic/ramdac.h>

#include <dev/wscons/wsconsio.h>

/*
 * Functions exported via the RAMDAC configuration table.
 */
void	ibm561_init(struct ramdac_cookie *);
int	ibm561_set_cmap(struct ramdac_cookie *,
	    struct wsdisplay_cmap *);
int	ibm561_get_cmap(struct ramdac_cookie *,
	    struct wsdisplay_cmap *);
int	ibm561_set_cursor(struct ramdac_cookie *,
	    struct wsdisplay_cursor *);
int	ibm561_get_cursor(struct ramdac_cookie *,
	    struct wsdisplay_cursor *);
int	ibm561_set_curpos(struct ramdac_cookie *,
	    struct wsdisplay_curpos *);
int	ibm561_get_curpos(struct ramdac_cookie *,
	    struct wsdisplay_curpos *);
int	ibm561_get_curmax(struct ramdac_cookie *,
	    struct wsdisplay_curpos *);
int	ibm561_set_dotclock(struct ramdac_cookie *,
	    unsigned);

/* XXX const */
struct ramdac_funcs ibm561_funcsstruct = {
	"IBM561",
	ibm561_register,
	ibm561_init,
	ibm561_set_cmap,
	ibm561_get_cmap,
	ibm561_set_cursor,
	ibm561_get_cursor,
	ibm561_set_curpos,
	ibm561_get_curpos,
	ibm561_get_curmax,
	NULL,			/* check_curcmap; not needed */
	NULL,			/* set_curcmap; not needed */
	NULL,			/* get_curcmap; not needed */
	ibm561_set_dotclock, 
};

/*
 * Private data.
 */
struct ibm561data {
	void            *cookie;

	int             (*ramdac_sched_update)(void *, void (*)(void *));
	void            (*ramdac_wr)(void *, u_int, u_int8_t);
	u_int8_t        (*ramdac_rd)(void *, u_int);

#define CHANGED_CURCMAP		0x0001	/* cursor cmap */
#define CHANGED_CMAP		0x0002	/* color map */
#define CHANGED_WTYPE		0x0004	/* window types */
#define CHANGED_DOTCLOCK	0x0008	/* dot clock */
#define CHANGED_ALL		0x000f	/* or of all above */
	u_int8_t	changed;

	/* dotclock parameters */
	u_int8_t	vco_div;
	u_int8_t	pll_ref;
	u_int8_t	div_dotclock;

	/* colormaps et al. */
	u_int8_t	curcmap_r[2];
	u_int8_t	curcmap_g[2];
	u_int8_t	curcmap_b[2];

	u_int8_t	cmap_r[IBM561_NCMAP_ENTRIES];
	u_int8_t	cmap_g[IBM561_NCMAP_ENTRIES];
	u_int8_t	cmap_b[IBM561_NCMAP_ENTRIES];

	u_int16_t	gamma_r[IBM561_NGAMMA_ENTRIES];
	u_int16_t	gamma_g[IBM561_NGAMMA_ENTRIES];
	u_int16_t	gamma_b[IBM561_NGAMMA_ENTRIES];

	u_int16_t	wtype[IBM561_NWTYPES];
};

/*
 * private functions
 */
void	ibm561_update(void *);
static void ibm561_load_cmap(struct ibm561data *);
static void ibm561_load_dotclock(struct ibm561data *);
static void ibm561_regbegin(struct ibm561data *, u_int16_t);
static void ibm561_regcont(struct ibm561data *, u_int16_t, u_int8_t);
static void ibm561_regcont10bit(struct ibm561data *, u_int16_t, u_int16_t);
static void ibm561_regwr(struct ibm561data *, u_int16_t, u_int8_t);

struct ramdac_funcs *
ibm561_funcs(void)
{
	return &ibm561_funcsstruct;
}

struct ibm561data ibm561_console_data;

struct ramdac_cookie *
ibm561_register(void *v, int (*sched_update)(void *, void (*)(void *)),
    void (*wr)(void *, u_int, u_int8_t), u_int8_t (*rd)(void *, u_int))
{
	struct ibm561data *data;

	if (ibm561_console_data.cookie == NULL)
		data = malloc(sizeof *data, M_DEVBUF, M_WAITOK | M_ZERO);
	else
		data = &ibm561_console_data;
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
ibm561_cninit(void *v, int (*sched_update)(void *, void (*)(void *)),
    void (*wr)(void *, u_int, u_int8_t), u_int8_t (*rd)(void *, u_int),
    u_int dotclock)
{
	struct ibm561data *data = &ibm561_console_data;
	data->cookie = v;
	data->ramdac_sched_update = sched_update;
	data->ramdac_wr = wr;
	data->ramdac_rd = rd;
	ibm561_set_dotclock((struct ramdac_cookie *)data, dotclock);
	ibm561_init((struct ramdac_cookie *)data);
}

void
ibm561_init(struct ramdac_cookie *rc)
{
	struct	ibm561data *data = (struct ibm561data *)rc;
	int	i;

	/* XXX this is _essential_ */

	ibm561_load_dotclock(data);

	/* XXXrcd: bunch of magic of which I have no current clue */
	ibm561_regwr(data, IBM561_CONFIG_REG1, 0x2a);
	ibm561_regwr(data, IBM561_CONFIG_REG3, 0x41);
	ibm561_regwr(data, IBM561_CONFIG_REG4, 0x20);

	/* initialize the card a bit */
	ibm561_regwr(data, IBM561_SYNC_CNTL, 0x1);
	ibm561_regwr(data, IBM561_CONFIG_REG2, 0x19);

	ibm561_regwr(data, IBM561_CONFIG_REG1, 0x2a);
	ibm561_regwr(data, IBM561_CONFIG_REG4, 0x20);

	ibm561_regbegin(data, IBM561_WAT_SEG_REG);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regbegin(data, IBM561_CHROMAKEY0);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regcont(data, IBM561_CMD, 0x00);

	ibm561_regwr(data, IBM561_CURS_CNTL_REG, 0x00); /* XXX off? */

	/* cursor `hot spot' registers */
	ibm561_regbegin(data, IBM561_HOTSPOT_REG);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regcont(data, IBM561_CMD, 0xff);
	ibm561_regcont(data, IBM561_CMD, 0x00);
	ibm561_regcont(data, IBM561_CMD, 0xff);
	ibm561_regcont(data, IBM561_CMD, 0x00);

	/* VRAM Mask Registers (diagnostics) */
	ibm561_regbegin(data, IBM561_VRAM_MASK_REG);
	ibm561_regcont(data, IBM561_CMD, 0xff);
	ibm561_regcont(data, IBM561_CMD, 0xff);
	ibm561_regcont(data, IBM561_CMD, 0xff);
	ibm561_regcont(data, IBM561_CMD, 0xff);
	ibm561_regcont(data, IBM561_CMD, 0xff);
	ibm561_regcont(data, IBM561_CMD, 0xff);
	ibm561_regcont(data, IBM561_CMD, 0xff);

	/* let's set up some decent default colour maps and gammas */
	for (i=0; i < IBM561_NCMAP_ENTRIES; i++)
		data->cmap_r[i] = data->cmap_g[i] = data->cmap_b[i] = 0xff;
	data->cmap_r[0]   = data->cmap_g[0]   = data->cmap_b[0]   = 0x00;
	data->cmap_r[256] = data->cmap_g[256] = data->cmap_b[256] = 0x00;
	data->cmap_r[512] = data->cmap_g[512] = data->cmap_b[512] = 0x00;
	data->cmap_r[768] = data->cmap_g[768] = data->cmap_b[768] = 0x00;

	data->gamma_r[0] = data->gamma_g[0] = data->gamma_b[0] = 0x00;
	for (i=0; i < IBM561_NGAMMA_ENTRIES; i++)
		data->gamma_r[i] = data->gamma_g[i] = data->gamma_b[i] = 0xff;

	for (i=0; i < IBM561_NWTYPES; i++)
		data->wtype[i] = 0x0036;
	data->wtype[1] = 0x0028;

	/* the last step: */
	data->changed = CHANGED_ALL;
	data->ramdac_sched_update(data->cookie, ibm561_update);
}

int
ibm561_set_cmap(struct ramdac_cookie *rc, struct wsdisplay_cmap *cmapp)
{
	struct ibm561data *data = (struct ibm561data *)rc;
	u_int count, index;
	int error;
	int s;

	index = cmapp->index;
	count = cmapp->count;

	if (index >= IBM561_NCMAP_ENTRIES ||
	    count > IBM561_NCMAP_ENTRIES - index)
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
	data->changed |= CHANGED_CMAP;
	data->ramdac_sched_update(data->cookie, ibm561_update);
	splx(s);
	return (0);
}

int
ibm561_get_cmap(struct ramdac_cookie *rc, struct wsdisplay_cmap *cmapp)
{
	struct ibm561data *data = (struct ibm561data *)rc;
	u_int count, index;
	int error;

	if (cmapp->index >= IBM561_NCMAP_ENTRIES ||
	    cmapp->count > IBM561_NCMAP_ENTRIES - cmapp->index)
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

/*
 * XXX:
 *  I am leaving these functions returning EINVAL, as they are
 *  not strictly necessary for the correct functioning of the
 *  card and in fact are not used on the other TGA variants, except
 *  they are exported via ioctl(2) to userland, which does not in
 *  fact use them.
 */

int
ibm561_set_cursor(struct ramdac_cookie *rc, struct wsdisplay_cursor *cursorp)
{
	return EINVAL;
}

int
ibm561_get_cursor(struct ramdac_cookie *rc, struct wsdisplay_cursor *cursorp)
{
	return EINVAL;
}

int
ibm561_set_curpos(struct ramdac_cookie *rc, struct wsdisplay_curpos *curposp)
{
	return EINVAL;
}

int
ibm561_get_curpos(struct ramdac_cookie *rc, struct wsdisplay_curpos *curposp)
{
	return EINVAL;
}

int
ibm561_get_curmax(struct ramdac_cookie *rc, struct wsdisplay_curpos *curposp)
{
	return EINVAL;
}

int
ibm561_set_dotclock(struct ramdac_cookie *rc, unsigned dotclock)
{
	struct ibm561data *data = (struct ibm561data *)rc;

	/* XXXrcd:  a couple of these are a little hazy, vis a vis
	 *          check 175MHz and 202MHz, which are wrong...
	 */
	switch (dotclock) {
	case  25175000: data->vco_div = 0x3e; data->pll_ref = 0x09; break;
	case  31500000: data->vco_div = 0x17; data->pll_ref = 0x05; break;
	case  40000000: data->vco_div = 0x42; data->pll_ref = 0x06; break;
	case  50000000: data->vco_div = 0x45; data->pll_ref = 0x05; break;
	case  65000000: data->vco_div = 0xac; data->pll_ref = 0x0c; break;
	case  69000000: data->vco_div = 0xa9; data->pll_ref = 0x0b; break;
	case  74000000: data->vco_div = 0x9c; data->pll_ref = 0x09; break;
	case  75000000: data->vco_div = 0x93; data->pll_ref = 0x08; break;
	case 103994000: data->vco_div = 0x96; data->pll_ref = 0x06; break;
	case 108180000: data->vco_div = 0xb8; data->pll_ref = 0x08; break;
	case 110000000: data->vco_div = 0xba; data->pll_ref = 0x08; break;
	case 119840000: data->vco_div = 0x82; data->pll_ref = 0x04; break;
	case 130808000: data->vco_div = 0xc8; data->pll_ref = 0x08; break;
	case 135000000: data->vco_div = 0xc1; data->pll_ref = 0x07; break;
	case 175000000: data->vco_div = 0xe2; data->pll_ref = 0x07; break;
	case 202500000: data->vco_div = 0xe2; data->pll_ref = 0x07; break;
	default:
		return EINVAL;
	}

	data->div_dotclock = 0xb0;
	data->changed |= CHANGED_DOTCLOCK;
	return 0;
}

/*
 * Internal Functions
 */

void
ibm561_update(void *vp)
{
	struct ibm561data *data = (struct ibm561data *)vp;
	int	i;

	/* XXX see comment above ibm561_cninit() */
	if (!data)
		data = &ibm561_console_data;

	if (data->changed & CHANGED_WTYPE) {
		ibm561_regbegin(data, IBM561_FB_WINTYPE);
		for (i=0; i < IBM561_NWTYPES; i++)
			ibm561_regcont10bit(data, IBM561_CMD_FB_WAT, data->wtype[i]);

		/* XXXrcd:  quick hack here for AUX FB table */
		ibm561_regbegin(data, IBM561_AUXFB_WINTYPE);
		for (i=0; i < IBM561_NWTYPES; i++)
			ibm561_regcont(data, IBM561_CMD, 0x04);

		/* XXXrcd:  quick hack here for OL WAT table */
		ibm561_regbegin(data, IBM561_OL_WINTYPE);
		for (i=0; i < IBM561_NWTYPES; i++)
			ibm561_regcont10bit(data, IBM561_CMD_FB_WAT, 0x0231);

		/* XXXrcd:  quick hack here for AUX OL WAT table */
		ibm561_regbegin(data, IBM561_AUXOL_WINTYPE);
		for (i=0; i < IBM561_NWTYPES; i++)
			ibm561_regcont(data, IBM561_CMD, 0x0c);
	}

	if (data->changed & CHANGED_CMAP)
		ibm561_load_cmap(data);

	/* XXX:  I am not sure in what situations it is safe to
	 *       change the dotclock---hope this is good.
	 */
	if (data->changed & CHANGED_DOTCLOCK)
		ibm561_load_dotclock(data);
}

static void
ibm561_load_cmap(struct ibm561data *data)
{
	int	i;

	ibm561_regbegin(data, IBM561_CMAP_TABLE);
	for (i=0; i < IBM561_NCMAP_ENTRIES; i++) {
		ibm561_regcont(data, IBM561_CMD_CMAP, data->cmap_r[i]);
		ibm561_regcont(data, IBM561_CMD_CMAP, data->cmap_g[i]);
		ibm561_regcont(data, IBM561_CMD_CMAP, data->cmap_b[i]);
	}

	ibm561_regbegin(data, IBM561_RED_GAMMA_TABLE);
	for (i=0; i < 256; i++)
		ibm561_regcont10bit(data, IBM561_CMD_GAMMA, data->gamma_r[i]);

	ibm561_regbegin(data, IBM561_GREEN_GAMMA_TABLE);
	for (i=1; i < 256; i++)
		ibm561_regcont10bit(data, IBM561_CMD_GAMMA, data->gamma_g[i]);

	ibm561_regbegin(data, IBM561_BLUE_GAMMA_TABLE);
	for (i=1; i < 256; i++)
		ibm561_regcont10bit(data, IBM561_CMD_GAMMA, data->gamma_b[i]);

}

static void
ibm561_load_dotclock(struct ibm561data *data)
{
	/* XXX
	 * we should probably be more pro-active here, but it shouldn't
	 * actually happen...
	 */
	if (!data->vco_div || !data->pll_ref || ! data->div_dotclock) {
		panic("ibm561_load_dotclock: called uninitialized");
	}

	ibm561_regwr(data, IBM561_PLL_VCO_DIV,  data->vco_div);
	ibm561_regwr(data, IBM561_PLL_REF_REG, data->pll_ref);
	ibm561_regwr(data, IBM561_DIV_DOTCLCK, data->div_dotclock);
}

static void
ibm561_regcont10bit(struct ibm561data *data, u_int16_t reg, u_int16_t val)
{
	data->ramdac_wr(data->cookie, IBM561_CMD_GAMMA, (val >> 2) & 0xff);
	data->ramdac_wr(data->cookie, IBM561_CMD_GAMMA, (val & 0x3) << 6);
}

static void
ibm561_regbegin(struct ibm561data *data, u_int16_t reg)
{
	data->ramdac_wr(data->cookie, IBM561_ADDR_LOW, reg & 0xff);
	data->ramdac_wr(data->cookie, IBM561_ADDR_HIGH, (reg >> 8) & 0xff);
}

static void
ibm561_regcont(struct ibm561data *data, u_int16_t reg, u_int8_t val)
{
	data->ramdac_wr(data->cookie, reg, val);
}

static void
ibm561_regwr(struct ibm561data *data, u_int16_t reg, u_int8_t val)
{
	ibm561_regbegin(data, reg);
	ibm561_regcont(data, IBM561_CMD, val);
}
