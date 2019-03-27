/*	$NetBSD: pcmciavar.h,v 1.12 2000/02/08 12:51:31 enami Exp $	*/
/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
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
 *	This product includes software developed by Marc Horowitz.
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Contains information about mapped/allocated i/o spaces.
 */
struct pccard_io_handle {
	bus_space_tag_t iot;		/* bus space tag (from chipset) */
	bus_space_handle_t ioh;		/* mapped space handle */
	bus_addr_t      addr;		/* resulting address in bus space */
	bus_size_t      size;		/* size of i/o space */
	int             flags;		/* misc. information */
	int		width;
};

#define	PCCARD_IO_ALLOCATED	0x01	/* i/o space was allocated */

/*
 * Contains information about allocated memory space.
 */
struct pccard_mem_handle {
	bus_space_tag_t memt;		/* bus space tag (from chipset) */
	bus_space_handle_t memh;	/* mapped space handle */
	bus_addr_t      addr;		/* resulting address in bus space */
	bus_size_t      size;		/* size of mem space */
	bus_size_t      realsize;	/* how much we really allocated */
	bus_addr_t	cardaddr;	/* Absolute address on card */
	int		kind;
};

/* Bits for kind */
#define	PCCARD_MEM_16BIT	1	/* 1 -> 16bit 0 -> 8bit */
#define PCCARD_MEM_ATTR		2	/* 1 -> attribute mem 0 -> common */

#define	PCCARD_WIDTH_AUTO	0
#define	PCCARD_WIDTH_IO8	1
#define	PCCARD_WIDTH_IO16	2

struct pccard_tuple {
	unsigned int	code;
	unsigned int	length;
	u_long		mult;		/* dist btn successive bytes */
	bus_addr_t	ptr;
	bus_space_tag_t	memt;
	bus_space_handle_t memh;
};

typedef int (*pccard_scan_t)(const struct pccard_tuple *, void *);

struct pccard_product {
	const char	*pp_name;
#define PCCARD_VENDOR_ANY (0xffffffff)
	uint32_t	pp_vendor;		/* 0 == end of table */
#define PCCARD_PRODUCT_ANY (0xffffffff)
	uint32_t	pp_product;
	const char	*pp_cis[4];
};

/**
 * Note: There's no cis3 or cis4 reported for NOMATCH / pnpinfo events for
 * pccard.  It's unclear if we actually need that for automatic loading or
 * not.  These strings are informative, according to the standard.  Some Linux
 * drivers match on them, for example.  However, FreeBSD's hardware probing is a
 * little different than Linux, so it turns out we don't need them.  Some cards
 * use CIS3 or CIS4 for a textual representation of the MAC address.  In short,
 * belief that all the entries in Linux don't actually need to be separate there
 * either, but they persist since it's hard to eliminate them and retest on old,
 * possibly rare, hardware.  Despite years of collecting ~300 different PC Cards
 * off E-Bay, I've not been able to find any that need CIS3/CIS4 to select which
 * device attaches.
 */
#define PCCARD_PNP_DESCR "D:#;V32:manufacturer;V32:product;Z:cisvendor;Z:cisproduct;"
#define PCCARD_PNP_INFO(t) \
	MODULE_PNP_INFO(PCCARD_PNP_DESCR, pccard, t, t, nitems(t) - 1)

typedef int (*pccard_product_match_fn) (device_t dev,
    const struct pccard_product *ent, int vpfmatch);

#include "card_if.h"

/*
 * Make this inline so that we don't have to worry about dangling references
 * to it in the modules or the code.
 */
static inline const struct pccard_product *
pccard_product_lookup(device_t dev, const struct pccard_product *tab,
    size_t ent_size, pccard_product_match_fn matchfn)
{
	return CARD_DO_PRODUCT_LOOKUP(device_get_parent(dev), dev,
	    tab, ent_size, matchfn);
}

#define	pccard_cis_read_1(tuple, idx0)					\
	(bus_space_read_1((tuple)->memt, (tuple)->memh, (tuple)->mult*(idx0)))

#define	pccard_tuple_read_1(tuple, idx1)				\
	(pccard_cis_read_1((tuple), ((tuple)->ptr+(2+(idx1)))))

#define	pccard_tuple_read_2(tuple, idx2)				\
	(pccard_tuple_read_1((tuple), (idx2)) |				\
	 (pccard_tuple_read_1((tuple), (idx2)+1)<<8))

#define	pccard_tuple_read_3(tuple, idx3)				\
	(pccard_tuple_read_1((tuple), (idx3)) |				\
	 (pccard_tuple_read_1((tuple), (idx3)+1)<<8) |			\
	 (pccard_tuple_read_1((tuple), (idx3)+2)<<16))

#define	pccard_tuple_read_4(tuple, idx4)				\
	(pccard_tuple_read_1((tuple), (idx4)) |				\
	 (pccard_tuple_read_1((tuple), (idx4)+1)<<8) |			\
	 (pccard_tuple_read_1((tuple), (idx4)+2)<<16) |			\
	 (pccard_tuple_read_1((tuple), (idx4)+3)<<24))

#define	pccard_tuple_read_n(tuple, n, idxn)				\
	(((n)==1)?pccard_tuple_read_1((tuple), (idxn)) :		\
	 (((n)==2)?pccard_tuple_read_2((tuple), (idxn)) :		\
	  (((n)==3)?pccard_tuple_read_3((tuple), (idxn)) :		\
	   /* n == 4 */ pccard_tuple_read_4((tuple), (idxn)))))

#define	PCCARD_SPACE_MEMORY	1
#define	PCCARD_SPACE_IO		2

#define	pccard_mfc(sc)							\
		(STAILQ_FIRST(&(sc)->card.pf_head) &&			\
		 STAILQ_NEXT(STAILQ_FIRST(&(sc)->card.pf_head),pf_list))

/* Convenience functions */

static inline int
pccard_cis_scan(device_t dev, pccard_scan_t fct, void *arg)
{
	return (CARD_CIS_SCAN(device_get_parent(dev), dev, fct, arg));
}

static inline int
pccard_attr_read_1(device_t dev, uint32_t offset, uint8_t *val)
{
	return (CARD_ATTR_READ(device_get_parent(dev), dev, offset, val));
}

static inline int
pccard_attr_write_1(device_t dev, uint32_t offset, uint8_t val)
{
	return (CARD_ATTR_WRITE(device_get_parent(dev), dev, offset, val));
}

static inline int
pccard_ccr_read_1(device_t dev, uint32_t offset, uint8_t *val)
{
	return (CARD_CCR_READ(device_get_parent(dev), dev, offset, val));
}

static inline int
pccard_ccr_write_1(device_t dev, uint32_t offset, uint8_t val)
{
	return (CARD_CCR_WRITE(device_get_parent(dev), dev, offset, val));
}

/* Hack */
int pccard_select_cfe(device_t dev, int entry);

/* ivar interface */
enum {
	PCCARD_IVAR_ETHADDR,	/* read ethernet address from CIS tupple */
	PCCARD_IVAR_VENDOR,
	PCCARD_IVAR_PRODUCT,
	PCCARD_IVAR_PRODEXT,
	PCCARD_IVAR_FUNCTION_NUMBER,
	PCCARD_IVAR_VENDOR_STR,	/* CIS string for "Manufacturer" */
	PCCARD_IVAR_PRODUCT_STR,/* CIS string for "Product" */
	PCCARD_IVAR_CIS3_STR,
	PCCARD_IVAR_CIS4_STR,
	PCCARD_IVAR_FUNCTION,
	PCCARD_IVAR_FUNCE_DISK
};

#define PCCARD_ACCESSOR(A, B, T)					\
static inline int							\
pccard_get_ ## A(device_t dev, T *t)					\
{									\
	return BUS_READ_IVAR(device_get_parent(dev), dev,		\
	    PCCARD_IVAR_ ## B, (uintptr_t *) t);			\
}

PCCARD_ACCESSOR(ether,		ETHADDR,		uint8_t)
PCCARD_ACCESSOR(vendor,		VENDOR,			uint32_t)
PCCARD_ACCESSOR(product,	PRODUCT,		uint32_t)
PCCARD_ACCESSOR(prodext,	PRODEXT,		uint16_t)
PCCARD_ACCESSOR(function_number,FUNCTION_NUMBER,	uint32_t)
PCCARD_ACCESSOR(function,	FUNCTION,		uint32_t)
PCCARD_ACCESSOR(funce_disk,	FUNCE_DISK,		uint16_t)
PCCARD_ACCESSOR(vendor_str,	VENDOR_STR,		const char *)
PCCARD_ACCESSOR(product_str,	PRODUCT_STR,		const char *)
PCCARD_ACCESSOR(cis3_str,	CIS3_STR,		const char *)
PCCARD_ACCESSOR(cis4_str,	CIS4_STR,		const char *)

/* shared memory flags */
enum {
	PCCARD_A_MEM_COM,       /* common */
	PCCARD_A_MEM_ATTR,      /* attribute */
	PCCARD_A_MEM_8BIT,      /* 8 bit */
	PCCARD_A_MEM_16BIT      /* 16 bit */
};

#define PCCARD_S(a, b) PCMCIA_STR_ ## a ## _ ## b
#define PCCARD_P(a, b) PCMCIA_PRODUCT_ ## a ## _ ## b
#define PCCARD_C(a, b) PCMCIA_CIS_ ## a ## _ ## b
#define PCMCIA_CARD_D(v, p) { PCCARD_S(v, p), PCMCIA_VENDOR_ ## v, \
		PCCARD_P(v, p), PCCARD_C(v, p) }
#define PCMCIA_CARD(v, p) { PCCARD_S(v, p), PCMCIA_VENDOR_ ## v, \
		PCCARD_P(v, p), PCCARD_C(v, p) }

/*
 * Defines to decode the get_funce_disk return value.  See the PCMCIA standard
 * for all the details of what these bits mean.
 */
#define	PFD_I_V_MASK		0x3
#define PFD_I_V_NONE_REQUIRED	0x0
#define PFD_I_V_REQ_MOD_ACC	0x1
#define PFD_I_V_REQ_ACC		0x2
#define PFD_I_V_REQ_ALWYS	0x1
#define PFD_I_S			0x4	/* 0 rotating, 1 silicon */
#define PFD_I_U			0x8	/* SN Uniq? */
#define	PFD_I_D			0x10	/* 0 - 1 drive, 1 - 2 drives */
#define PFD_P_P0		0x100
#define PFD_P_P1		0x200
#define PFD_P_P2		0x400
#define PFD_P_P3		0x800
#define PFD_P_N			0x1000	/* 3f7/377 excluded? */
#define PFD_P_E			0x2000	/* Index bit supported? */
#define	PFD_P_I			0x4000	/* twincard */
