/*	$OpenBSD: centralvar.h,v 1.1 2004/09/22 21:31:51 jason Exp $	*/

/*
 * Copyright (c) 2004 Jason L. Wright (jason@thought.net)
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

struct central_reg {
	u_int32_t	cbr_slot;
	u_int32_t	cbr_offset;
	u_int32_t	cbr_size;
};

struct central_attach_args {
	char *ca_name;
	bus_space_tag_t ca_bustag;
	struct central_reg *ca_reg;
	int ca_node;
	int ca_nreg;
};

struct central_range {
	u_int32_t	cspace;		/* Client space */
	u_int32_t	coffset;	/* Client offset */
	u_int32_t	pspace;		/* Parent space */
	u_int32_t	poffset;	/* Parent offset */
	u_int32_t	size;		/* Size in bytes of this range */ 
};

#define	central_bus_map(t, slot, offset, sz, flags, hp)		\
    bus_space_map(t, BUS_ADDR(slot, offset), sz, flags, hp)
