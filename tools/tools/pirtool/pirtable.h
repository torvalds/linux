/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002-2006 Bruce M. Simpson.
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
 * 3. Neither the name of Bruce M. Simpson nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRUCE M. SIMPSON AND AFFILIATES
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
 *
 * $FreeBSD$
 */

/*
 * All fields and quantities in this file are in little-endian byte order,
 * unless otherwise specified.
 */

#ifndef _PIRTABLE_H
#define _PIRTABLE_H

#define PIR_BASE	0xF0000
#define PIR_SIZE	0x10000
#define PIR_OFFSET	16

#define PIR_DEV(x)	(((x) & 0xF8) >> 3)
#define PIR_FUNC(x)	((x) & 0x7)

typedef struct {
	uint8_t		bus;		/* bus number of this device */
	uint8_t		devfunc;	/* only upper 5 device bits valid */
	uint8_t		inta_link;	/* how INTA is linked */
	uint16_t	inta_irqs;	/* how INTA may be routed (bitset) */
	uint8_t		intb_link;
	uint16_t	intb_irqs;
	uint8_t		intc_link;
	uint16_t	intc_irqs;
	uint8_t		intd_link;
	uint16_t	intd_irqs;	/* how this pin may be routed */
	uint8_t		slot;		/* physical slot number on bus,
					 * slot 0 if motherboard */
	uint8_t		reserved00;	/* must be zero */
} __packed pir_entry_t;

typedef struct {
	uint32_t	signature;	/* $PIR */
	uint8_t		minor;		/* minor version (0) */
	uint8_t		major;		/* major version (1) */
	uint16_t	size;		/* total size of table */
	uint8_t		bus;		/* Bus number of router */
	uint8_t		devfunc;	/* Dev/Func of router */
	uint16_t	excl_irqs;	/* PCI Exclusive IRQs */
	uint32_t	compatible;	/* Device/Vendor ID of a register
					 * compatible PCI IRQ router device */
	uint32_t	miniport_data;	/* Windows specific */
	uint8_t		reserved00[11]; /* Must be zero */
	uint8_t		checksum;	/* Inverse mod-256 sum of table bytes */
	pir_entry_t	entry[1];	/* 1..N device entries */
} __packed pir_table_t;

#endif /* _PIRTABLE_H */
