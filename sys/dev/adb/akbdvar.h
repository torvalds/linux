/*	$OpenBSD: akbdvar.h,v 1.4 2012/01/29 10:54:21 mpi Exp $	*/
/*	$NetBSD: akbdvar.h,v 1.4 1999/02/17 14:56:56 tsubai Exp $	*/

/*
 * Copyright (C) 1998	Colin Wood
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
 *	This product includes software developed by Colin Wood.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ADB_AKBDVAR_H_
#define _ADB_AKBDVAR_H_

/*
 * State info, per keyboard instance.
 */
struct akbd_softc {
	struct	device	sc_dev;

	/* ADB info */
	int		origaddr;	/* ADB device type (ADBADDR_KBD) */
	int		adbaddr;	/* current ADB address */
	int		handler_id;	/* type of keyboard */

	u_int8_t	sc_leds;	/* current LED state */
	u_int8_t	sc_iso;

	int		sc_caps;	/* capslock key state */
	struct device	*sc_wskbddev;

#ifdef WSDISPLAY_COMPAT_RAWKBD
	int sc_rawkbd;
#endif /* defined(WSDISPLAY_COMPAT_RAWKBD) */
};

/* LED register bits, inverse of actual register value */
#define LED_NUMLOCK	0x1
#define LED_CAPSLOCK	0x2
#define LED_SCROLL_LOCK	0x4

int	akbd_cnattach(void);
int	akbd_is_console(void);

extern struct wskbd_mapdata akbd_keymapdata;
extern int adb_polledkey;

#endif /* _ADB_AKBDVAR_H_ */
