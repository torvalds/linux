/*	$OpenBSD: rbus.h,v 1.10 2010/09/22 02:28:37 jsg Exp $ */
/*	$NetBSD: rbus.h,v 1.3 1999/12/15 12:28:55 kleink Exp $	*/
/*
 * Copyright (c) 1999
 *     HAYAKAWA Koichi.  All rights reserved.
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

#ifndef _DEV_CARDBUS_RBUS_H_
#define _DEV_CARDBUS_RBUS_H_

/*
 * This file defines the rbus (pseudo) class
 *
 * What is rbus?
 *
 *  Ths rbus is a recursive bus-space administrator.  This means a
 *  parent bus-space administrator, which usually belongs to a bus
 *  bridge, makes some child bus-space administrators and gives
 *  (restricted) bus-space to children.  There is a root bus-space
 *  administrator which maintains the whole bus-space.
 *
 * Why recursive?
 *
 *  The recursive bus-space administration has two reasons. For one
 *  this modelling matches the actual memory and io space management 
 *  of bridge devices quite well. Furthermore  is the rbus a
 *  distributed management system, as such it plays well with
 *  multi-thread kernel.
 *
 * Abstraction
 *
 *  rbus can model a bus-to-bus bridge in two ways: dedicated or shared
 *  Dedicated: the bridge has its own bus space.
 *  Shared: the bridge has bus space, but this bus space is
 *  shared with other bus bridges. 
 */


/* require sys/extent.h */
/* require machine/bus.h */

struct extent;


/*
 *     General rule
 *
 * 1) When a rbustag has its own space (whether shared or dedicated),
 *    allocate from rb_ext.
 */
struct rbustag {
	bus_space_tag_t rb_bt;
	struct extent *rb_ext;
	bus_addr_t rb_start;
	bus_addr_t rb_end;
	bus_addr_t rb_offset;
#if notyet
	int (*rb_space_alloc)(struct rbustag *, bus_addr_t, bus_addr_t,
	    bus_addr_t, bus_size_t, bus_addr_t, bus_addr_t,
	    int, bus_addr_t *, bus_space_handle_t *);
	int (*rbus_space_free)(struct rbustag *, bus_space_handle_t,
	    bus_size_t, bus_addr_t *);
#endif
	int rb_flags;
#define RBUS_SPACE_INVALID   0x00
#define RBUS_SPACE_SHARE     0x01
#define RBUS_SPACE_DEDICATE  0x02
#define RBUS_SPACE_MASK      0x03
	/* your own data below */
	void *rb_md;
};

typedef struct rbustag *rbus_tag_t;


/*
 * These functions sugarcoat rbus interface to make rbus being used
 * easier.  These functions should be member functions of rbus
 * `class'.
 */
int	rbus_space_alloc(rbus_tag_t, bus_addr_t, bus_size_t, bus_addr_t,
	    bus_addr_t, int, bus_addr_t *, bus_space_handle_t *);

int	rbus_space_alloc_subregion(rbus_tag_t, bus_addr_t, bus_addr_t,
	    bus_addr_t, bus_size_t, bus_addr_t, bus_addr_t, int,
	    bus_addr_t *, bus_space_handle_t *);

int	rbus_space_free(rbus_tag_t, bus_space_handle_t, bus_size_t,
	    bus_addr_t *);


/*
 * These functions create rbus instance.  These functions are
 * so-called-as a constructor of rbus.
 *
 */

rbus_tag_t	rbus_new_body(bus_space_tag_t, struct extent *,
		      bus_addr_t, bus_addr_t, int);

rbus_tag_t	rbus_new_root_delegate(bus_space_tag_t, bus_addr_t, bus_size_t);
rbus_tag_t	rbus_new_root_share(bus_space_tag_t, struct extent *,
		    bus_addr_t, bus_size_t);

/*
 * Machine-dependent definitions.
 */
#include <machine/rbus_machdep.h>

#endif /* !_DEV_CARDBUS_RBUS_H_ */
