/*	$OpenBSD: fhcvar.h,v 1.8 2004/10/01 18:18:49 jason Exp $	*/

/*
 * Copyright (c) 2004 Jason L. Wright (jason@thought.net).
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

struct fhc_reg {
	u_int32_t	fbr_slot;
	u_int32_t	fbr_offset;
	u_int32_t	fbr_size;
};

struct fhc_range {
	u_int32_t	cspace;		/* Client space */
	u_int32_t	coffset;	/* Client offset */
	u_int32_t	pspace;		/* Parent space */
	u_int32_t	poffset;	/* Parent offset */
	u_int32_t	size;		/* Size in bytes of this range */
};

struct fhc_softc {
	struct device sc_dv;
	int sc_node;
	int sc_is_central;		/* parent is central */
	int sc_board;
	u_int32_t sc_ign;
	bus_space_tag_t sc_bt;
	bus_space_tag_t sc_cbt;
	int sc_nrange;
	struct fhc_range *sc_range;
	bus_space_handle_t sc_preg;	/* internal regs */
	bus_space_handle_t sc_ireg;	/* ign regs */
	bus_space_handle_t sc_freg;	/* fanfail regs */
	bus_space_handle_t sc_sreg;	/* system regs */
	bus_space_handle_t sc_ureg;	/* uart regs */
	bus_space_handle_t sc_treg;	/* tod regs */
	struct blink_led sc_blink;
};

void fhc_attach(struct fhc_softc *);
int fhc_get_string(int, char *, char **);

struct fhc_attach_args {
	bus_space_tag_t fa_bustag;
	char *fa_name;
	int *fa_intr;
	struct fhc_reg *fa_reg;
	u_int32_t *fa_promvaddrs;
	int fa_node;
	int fa_nreg;
	int fa_nintr;
	int fa_npromvaddrs;
};

#define	fhc_bus_map(t, slot, offset, sz, flags, hp)		\
    bus_space_map(t, BUS_ADDR(slot, offset), sz, flags, hp)
