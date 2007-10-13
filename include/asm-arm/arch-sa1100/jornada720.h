/*
 * include/asm-arm/arch-sa1100/jornada720.h
 *
 * This file contains SSP/MCU communication definitions for HP Jornada 710/720/728
 *
 * Copyright (C) 2007 Kristoffer Ericson <Kristoffer.Ericson@gmail.com>
 *  Copyright (C) 2000 John Ankcorn <jca@lcs.mit.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

 /* HP Jornada 7xx microprocessor commands */
#define GETBATTERYDATA		0xc0
#define GETSCANKEYCODE		0x90
#define GETTOUCHSAMPLES		0xa0
#define GETCONTRAST		0xD0
#define SETCONTRAST		0xD1
#define GETBRIGHTNESS		0xD2
#define SETBRIGHTNESS		0xD3
#define CONTRASTOFF		0xD8
#define BRIGHTNESSOFF		0xD9
#define PWMOFF			0xDF
#define TXDUMMY			0x11
#define ERRORCODE		0x00
