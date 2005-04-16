/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/cayman.h
 *
 * Cayman definitions
 *
 * Global defintions for the SH5 Cayman board
 *
 * Copyright (C) 2002 Stuart Menefy
 */


/* Setup for the SMSC FDC37C935 / LAN91C100FD */
#define SMSC_IRQ         IRQ_IRL1

/* Setup for PCI Bus 2, which transmits interrupts via the EPLD */
#define PCI2_IRQ         IRQ_IRL3
