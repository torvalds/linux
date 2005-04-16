/*
 * linux/include/asm-mips/tx4927/smsc_fdc37m81x.h
 *
 * Interface for smsc fdc48m81x Super IO chip
 *
 * Author: MontaVista Software, Inc. source@mvista.com
 *
 * 2001-2003 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Copyright (C) 2004 MontaVista Software Inc.
 * Manish Lachwani, mlachwani@mvista.com
 */

#ifndef _SMSC_FDC37M81X_H_
#define _SMSC_FDC37M81X_H_

/* Common Registers */
#define SMSC_FDC37M81X_CONFIG_INDEX  0x00
#define SMSC_FDC37M81X_CONFIG_DATA   0x01
#define SMSC_FDC37M81X_CONF          0x02
#define SMSC_FDC37M81X_INDEX         0x03
#define SMSC_FDC37M81X_DNUM          0x07
#define SMSC_FDC37M81X_DID           0x20
#define SMSC_FDC37M81X_DREV          0x21
#define SMSC_FDC37M81X_PCNT          0x22
#define SMSC_FDC37M81X_PMGT          0x23
#define SMSC_FDC37M81X_OSC           0x24
#define SMSC_FDC37M81X_CONFPA0       0x26
#define SMSC_FDC37M81X_CONFPA1       0x27
#define SMSC_FDC37M81X_TEST4         0x2B
#define SMSC_FDC37M81X_TEST5         0x2C
#define SMSC_FDC37M81X_TEST1         0x2D
#define SMSC_FDC37M81X_TEST2         0x2E
#define SMSC_FDC37M81X_TEST3         0x2F

/* Logical device numbers */
#define SMSC_FDC37M81X_FDD           0x00
#define SMSC_FDC37M81X_PARALLEL      0x03
#define SMSC_FDC37M81X_SERIAL1       0x04
#define SMSC_FDC37M81X_SERIAL2       0x05
#define SMSC_FDC37M81X_KBD           0x07
#define SMSC_FDC37M81X_AUXIO         0x08
#define SMSC_FDC37M81X_NONE          0xff

/* Logical device Config Registers */
#define SMSC_FDC37M81X_ACTIVE        0x30
#define SMSC_FDC37M81X_BASEADDR0     0x60
#define SMSC_FDC37M81X_BASEADDR1     0x61
#define SMSC_FDC37M81X_INT           0x70
#define SMSC_FDC37M81X_INT2          0x72
#define SMSC_FDC37M81X_LDCR_F0       0xF0

/* Chip Config Values */
#define SMSC_FDC37M81X_CONFIG_ENTER  0x55
#define SMSC_FDC37M81X_CONFIG_EXIT   0xaa
#define SMSC_FDC37M81X_CHIP_ID       0x4d

unsigned long __init smsc_fdc37m81x_init(unsigned long port);

void smsc_fdc37m81x_config_beg(void);

void smsc_fdc37m81x_config_end(void);

void smsc_fdc37m81x_config_set(u8 reg, u8 val);

#endif
