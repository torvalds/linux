/* $OpenBSD: ramdac.h,v 1.7 2008/06/26 05:42:16 ray Exp $ */
/* $NetBSD: ramdac.h,v 1.1 2000/03/04 10:23:39 elric Exp $ */

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Roland C. Dowdeswell.
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

#ifndef _DEV_IC_RAMDAC_H
#define _DEV_IC_RAMDAC_H

#include <dev/wscons/wsconsio.h>

struct ramdac_cookie;

struct ramdac_funcs {
	char	*ramdac_name;
	struct ramdac_cookie *(*ramdac_register)(void *,
		    int (*)(void *, void (*)(void *)),
		    void (*)(void *, u_int, u_int8_t),
		    u_int8_t (*)(void *, u_int));	      
	void	(*ramdac_init)(struct ramdac_cookie *);

	int	(*ramdac_set_cmap)(struct ramdac_cookie *,
		    struct wsdisplay_cmap *);
	int	(*ramdac_get_cmap)(struct ramdac_cookie *,
		    struct wsdisplay_cmap *);
	int	(*ramdac_set_cursor)(struct ramdac_cookie *,
		    struct wsdisplay_cursor *);
	int	(*ramdac_get_cursor)(struct ramdac_cookie *,
		    struct wsdisplay_cursor *);
	int	(*ramdac_set_curpos)(struct ramdac_cookie *,
		    struct wsdisplay_curpos *);
	int	(*ramdac_get_curpos)(struct ramdac_cookie *,
		    struct wsdisplay_curpos *);
	int	(*ramdac_get_curmax)(struct ramdac_cookie *,
		    struct wsdisplay_curpos *);

	/* Only called from the TGA built-in cursor handling code. */
	int	(*ramdac_check_curcmap)(struct ramdac_cookie *,
		    struct wsdisplay_cursor *);
	void	(*ramdac_set_curcmap)(struct ramdac_cookie *,
		    struct wsdisplay_cursor *);
	int	(*ramdac_get_curcmap)(struct ramdac_cookie *,
		    struct wsdisplay_cursor *);

	/* XXXrcd:  new test code for setting the DOTCLOCK */
	int	(*ramdac_set_dotclock)(struct ramdac_cookie *,
		    unsigned);
};

#endif
