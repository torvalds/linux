/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Peter Grehan
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OFW_SYSCONS_H_
#define _OFW_SYSCONS_H_

struct ofwfb_softc {
	video_adapter_t	sc_va;
	struct cdev *sc_si;
	bus_space_tag_t sc_tag;
	phandle_t	sc_node;
	int	       	sc_console;

	intptr_t	sc_addr;
	int		sc_depth;
        int	       	sc_height;
        int	       	sc_width;
	int	       	sc_stride;
        int	       	sc_ncol;
        int	       	sc_nrow;

	int	       	sc_xmargin;
	int	       	sc_ymargin;

	u_char	       *sc_font;
	int		sc_font_height;

	vi_blank_display_t *sc_blank;
	vi_putc_t	*sc_putc;
	vi_putm_t	*sc_putm;
	vi_set_border_t	*sc_set_border;

#define OFWSC_MAXADDR	8
	int		sc_num_pciaddrs;
	struct ofw_pci_register sc_pciaddrs[OFWSC_MAXADDR];
};

#endif
