/*
 * include/asm-powerpc/mpc85xx.h
 *
 * MPC85xx definitions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASM_MPC85xx_H__
#define __ASM_MPC85xx_H__

#include <asm/mmu.h>

#ifdef CONFIG_85xx

#if defined(CONFIG_MPC8540_ADS) || defined(CONFIG_MPC8560_ADS)
#include <platforms/85xx/mpc85xx_ads.h>
#endif
#if defined(CONFIG_MPC8555_CDS) || defined(CONFIG_MPC8548_CDS)
#include <platforms/85xx/mpc8555_cds.h>
#endif
#ifdef CONFIG_MPC85xx_CDS
#include <platforms/85xx/mpc85xx_cds.h>
#endif

/* Let modules/drivers get at CCSRBAR */
extern phys_addr_t get_ccsrbar(void);

#ifdef MODULE
#define CCSRBAR get_ccsrbar()
#else
#define CCSRBAR BOARD_CCSRBAR
#endif

#endif /* CONFIG_85xx */
#endif /* __ASM_MPC85xx_H__ */
#endif /* __KERNEL__ */
