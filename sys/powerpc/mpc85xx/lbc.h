/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_LBC_H_
#define	_MACHINE_LBC_H_

/* Maximum number of devices on Local Bus */
#define	LBC_DEV_MAX	8

/* Local access registers */
#define	LBC85XX_BR(n)	(0x0 + (8 * n))	/* Base register 0-7 */
#define	LBC85XX_OR(n)	(0x4 + (8 * n))	/* Options register 0-7 */
#define	LBC85XX_MAR	0x068		/* UPM address register */
#define	LBC85XX_MAMR	0x070		/* UPMA mode register */
#define	LBC85XX_MBMR	0x074		/* UPMB mode register */
#define	LBC85XX_MCMR	0x078		/* UPMC mode register */
#define	LBC85XX_MRTPR	0x084		/* Memory refresh timer prescaler */
#define	LBC85XX_MDR	0x088		/* UPM data register */
#define	LBC85XX_LSOR	0x090		/* Special operation initiation */
#define	LBC85XX_LURT	0x0a0		/* UPM refresh timer */
#define	LBC85XX_LSRT	0x0a4		/* SDRAM refresh timer */
#define	LBC85XX_LTESR	0x0b0		/* Transfer error status register */
#define	LBC85XX_LTEDR	0x0b4		/* Transfer error disable register */
#define	LBC85XX_LTEIR	0x0b8		/* Transfer error interrupt register */
#define	LBC85XX_LTEATR	0x0bc		/* Transfer error attributes register */
#define	LBC85XX_LTEAR	0x0c0		/* Transfer error address register */
#define	LBC85XX_LTECCR	0x0c4		/* Transfer error ECC register */
#define	LBC85XX_LBCR	0x0d0		/* Configuration register */
#define	LBC85XX_LCRR	0x0d4		/* Clock ratio register */
#define	LBC85XX_FMR	0x0e0		/* Flash mode register */
#define	LBC85XX_FIR	0x0e4		/* Flash instruction register */
#define	LBC85XX_FCR	0x0e8		/* Flash command register */
#define	LBC85XX_FBAR	0x0ec		/* Flash block address register */
#define	LBC85XX_FPAR	0x0f0		/* Flash page address register */
#define	LBC85XX_FBCR	0x0f4		/* Flash byte count register */
#define	LBC85XX_FECC0	0x100		/* Flash ECC block 0 register */
#define	LBC85XX_FECC1	0x104		/* Flash ECC block 0 register */
#define	LBC85XX_FECC2	0x108		/* Flash ECC block 0 register */
#define	LBC85XX_FECC3	0x10c		/* Flash ECC block 0 register */

/* LBC machine select */
#define	LBCRES_MSEL_GPCM	0
#define	LBCRES_MSEL_FCM		1
#define	LBCRES_MSEL_UPMA	8
#define	LBCRES_MSEL_UPMB	9
#define	LBCRES_MSEL_UPMC	10

/* LBC data error checking modes */
#define	LBCRES_DECC_DISABLED	0
#define	LBCRES_DECC_NORMAL	1
#define	LBCRES_DECC_RMW		2

/* LBC atomic operation modes */
#define	LBCRES_ATOM_DISABLED	0
#define	LBCRES_ATOM_RAWA	1
#define	LBCRES_ATOM_WARA	2

struct lbc_memrange {
	vm_paddr_t	addr;
	vm_size_t	size;
	vm_offset_t	kva;
};

struct lbc_bank {
	vm_paddr_t	addr;		/* physical addr of the bank */
	vm_size_t	size;		/* bank size */
	vm_offset_t	kva;		/* VA of the bank */

	/*
	 * XXX the following bank attributes do not have properties specified
	 * in the LBC DTS bindings yet (11.2009), so they are mainly a
	 * placeholder for future extensions.
	 */
	int		width;		/* data bus width */
	uint8_t		msel;		/* machine select */
	uint8_t		atom;		/* atomic op mode */
	uint8_t		wp;		/* write protect */
	uint8_t		decc;		/* data error checking */
};

struct lbc_softc {
	device_t		sc_dev;

	struct resource		*sc_mres;
	bus_space_handle_t	sc_bsh;
	bus_space_tag_t		sc_bst;
	int			sc_mrid;

	int			sc_irid;
	struct resource		*sc_ires;
	void			*sc_icookie;

	struct rman		sc_rman;

	int			sc_addr_cells;
	int			sc_size_cells;

	struct lbc_memrange	sc_range[LBC_DEV_MAX];
	struct lbc_bank		sc_banks[LBC_DEV_MAX];

	uint32_t		sc_ltesr;
};

struct lbc_devinfo {
	struct ofw_bus_devinfo	di_ofw;
	struct resource_list	di_res;
	int			di_bank;
};

uint32_t	lbc_read_reg(device_t child, u_int off);
void		lbc_write_reg(device_t child, u_int off, uint32_t val);

#endif /* _MACHINE_LBC_H_ */
