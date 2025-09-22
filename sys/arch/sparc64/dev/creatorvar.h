/*	$OpenBSD: creatorvar.h,v 1.11 2006/05/15 21:38:36 miod Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net),
 *  Federico G. Schwindt (fgsch@openbsd.org)
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
 */

/* device types */
#define FFB_CREATOR		0	/* Creator/Creator3d */
#define FFB_AFB			1	/* Elite3D */

#define	CREATOR_CFFLAG_NOACCEL	0x1

struct creator_softc {
	struct sunfb sc_sunfb;
	bus_space_tag_t sc_bt;
	bus_space_handle_t sc_pixel_h;
	bus_space_handle_t sc_fbc_h;
	bus_space_handle_t sc_dac_h;
	bus_addr_t sc_addrs[FFB_NREGS];
	bus_size_t sc_sizes[FFB_NREGS];
	int sc_nscreens, sc_nreg;
	int sc_console;
	int sc_node;
	int sc_type;
	u_int sc_mode;
	int32_t sc_fifo_cache, sc_fg_cache;
	u_int32_t sc_dacrev;
	u_int sc_curs_enabled, sc_curs_fg, sc_curs_bg;
	struct wsdisplay_curpos sc_curs_pos, sc_curs_hot, sc_curs_size;
	u_char sc_curs_image[512], sc_curs_mask[512];
};

#define	CREATOR_CURS_MAX	64

#define	FBC_WRITE(sc,r,v) \
    bus_space_write_4((sc)->sc_bt, (sc)->sc_fbc_h, (r), (v))
#define	FBC_READ(sc,r) \
    bus_space_read_4((sc)->sc_bt, (sc)->sc_fbc_h, (r))

#define	DAC_WRITE(sc,r,v) \
    bus_space_write_4((sc)->sc_bt, (sc)->sc_dac_h, (r), (v))
#define	DAC_READ(sc,r) \
    bus_space_read_4((sc)->sc_bt, (sc)->sc_dac_h, (r))
