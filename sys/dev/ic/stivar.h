/*	$OpenBSD: stivar.h,v 1.29 2024/08/17 08:45:22 miod Exp $	*/

/*
 * Copyright (c) 2000-2003 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _IC_STIVAR_H_
#define _IC_STIVAR_H_

struct sti_softc;
struct sti_screen;

/*
 * STI ROM information - one per device
 */
struct sti_rom {
	struct sti_softc	*rom_softc;	/* backpointer to device */
	int			 rom_devtype;

	bus_space_tag_t		 iot, memt;	/* XXX iot unused */
	bus_space_handle_t	 romh;
	bus_space_handle_t	 regh[STI_REGION_MAX];
	bus_addr_t		*bases;

	struct sti_dd		 rom_dd;	/* in word format */
	u_int8_t		*rom_code;

	int			 rom_enable;

	/*
	 * ROM-provided function pointers
	 */
	sti_init_t		 init;
	sti_unpmv_t		 unpmv;
	sti_blkmv_t		 blkmv;
	sti_inqconf_t		 inqconf;
	sti_scment_t		 scment;
};

/*
 * STI screen information - one per head
 */
struct sti_screen {
	struct sti_rom		*scr_rom;

#ifdef notyet
	u_int			 scr_flags;
#endif

	int			 scr_bpp;

	struct sti_font		 scr_curfont;
	struct sti_cfg		 scr_cfg;
	struct sti_ecfg		 scr_ecfg;
	u_int8_t		 name[STI_DEVNAME_LEN];

	void			*scr_romfont;	/* ROM font copy in memory... */
	u_int			 scr_fontmaxcol;/* ...or in off-screen area */
	u_int			 scr_fontbase;

	u_int8_t		 scr_rcmap[STI_NCMAP],
				 scr_gcmap[STI_NCMAP],
				 scr_bcmap[STI_NCMAP];

	u_int16_t		 fbheight, fbwidth;
	u_int16_t		 oheight, owidth;	/* offscreen size */
	bus_addr_t		 fbaddr;
	bus_size_t		 fblen;

	/* wsdisplay information */
	int			 scr_nscreens;
	u_int			 scr_wsmode;
	struct	wsscreen_descr	 scr_wsd;
	struct	wsscreen_descr	*scr_scrlist[1];
	struct	wsscreen_list	 scr_screenlist;

	/*
	 * Board-specific function data and pointers
	 */
	void			(*setupfb)(struct sti_screen *);
	int			(*putcmap)(struct sti_screen *, u_int, u_int);

	uint32_t		reg10_value;
	uint32_t		reg12_value;
	bus_addr_t		cmap_finish_register;
};

/*
 * STI device state
 */
struct sti_softc {
	struct device		 sc_dev;
#ifdef notyet
	void			*sc_ih;
#endif

	u_int			 sc_flags;
#define	STI_CONSOLE	0x0001	/* first head is console... */
#define	STI_ATTACHED	0x0002	/* ... and wsdisplay_cnattach() has been done */
#define	STI_ROM_ENABLED	0x0004	/* PCI ROM is enabled */

	bus_addr_t		 bases[STI_REGION_MAX];
	struct sti_rom		*sc_rom;
	struct sti_screen	*sc_scr;

	/* optional, required for PCI */
	void			(*sc_enable_rom)(struct sti_softc *);
	void			(*sc_disable_rom)(struct sti_softc *);
};

int	sti_attach_common(struct sti_softc *, bus_space_tag_t, bus_space_tag_t,
	    bus_space_handle_t, u_int);
void	sti_describe(struct sti_softc *);
void	sti_end_attach(void *);
u_int	sti_rom_size(bus_space_tag_t, bus_space_handle_t);

#endif /* _IC_STIVAR_H_ */
