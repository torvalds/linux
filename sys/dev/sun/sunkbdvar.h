/*	$OpenBSD: sunkbdvar.h,v 1.16 2011/11/09 14:22:38 shadchin Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#define	SUNKBD_MAX_INPUT_SIZE	64

struct sunkbd_softc {
	struct device	sc_dev;

	int		(*sc_sendcmd)(void *, u_int8_t *, u_int);
	void		(*sc_decode)(u_int8_t, u_int *, int *);

	int		sc_leds;		/* LED status */
	int		sc_id;			/* keyboard type */
	u_int8_t	sc_kbdstate;		/* keyboard state */
	int		sc_click;		/* click state */
	int		sc_layout;		/* current layout */

	int		sc_bellactive, sc_belltimeout;
	struct timeout	sc_bellto;

	struct device	*sc_wskbddev;

#ifdef WSDISPLAY_COMPAT_RAWKBD
	int		sc_rawkbd;
#endif
};

extern struct wskbd_accessops sunkbd_accessops;

void	sunkbd_attach(struct sunkbd_softc *, struct wskbddev_attach_args *);
void	sunkbd_bellstop(void *);
void	sunkbd_decode(u_int8_t, u_int *, int *);
void	sunkbd_input(struct sunkbd_softc *, u_int8_t *, u_int);
void	sunkbd_raw(struct sunkbd_softc *, u_int8_t);
int	sunkbd_setclick(struct sunkbd_softc *, int);

extern const struct wscons_keydesc sunkbd_keydesctab[];
extern struct wskbd_mapdata sunkbd_keymapdata;
extern const struct wscons_keydesc sunkbd5_keydesctab[];
extern struct wskbd_mapdata sunkbd5_keymapdata;

/*
 * Keyboard types 5 and 6 identify themselves as type 4, but might have
 * different layouts. Fortunately they will have distinct layout codes, so
 * here's a way to distinct types 5 and 6 from type 4.
 *
 * Note that ``Compact-1'' keyboards will be abusively reported as type 5.
 */
#define ISTYPE5(layout) ((layout) > 0x20)

#define	MAXSUNLAYOUT	0x062
extern const int sunkbd_layouts[MAXSUNLAYOUT];
extern const u_int8_t sunkbd_rawmap[0x80];
