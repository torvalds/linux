/*
 * File:         include/asm-blackfin/mach-bf561/blackfin.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _MACH_BLACKFIN_H_
#define _MACH_BLACKFIN_H_

#define BF561_FAMILY

#include "bf561.h"
#include "mem_map.h"
#include "defBF561.h"
#include "anomaly.h"

#if !defined(__ASSEMBLY__)
#include "cdefBF561.h"
#endif

#define bfin_read_FIO_FLAG_D() bfin_read_FIO0_FLAG_D()
#define bfin_write_FIO_FLAG_D(val) bfin_write_FIO0_FLAG_D(val)
#define bfin_read_FIO_DIR() bfin_read_FIO0_DIR()
#define bfin_write_FIO_DIR(val) bfin_write_FIO0_DIR(val)
#define bfin_read_FIO_INEN() bfin_read_FIO0_INEN()
#define bfin_write_FIO_INEN(val) bfin_write_FIO0_INEN(val)

#define SIC_IWR0 SICA_IWR0
#define SIC_IWR1 SICA_IWR1
#define SIC_IAR0 SICA_IAR0
#define bfin_write_SIC_IMASK0 bfin_write_SICA_IMASK0
#define bfin_write_SIC_IMASK1 bfin_write_SICA_IMASK1
#define bfin_write_SIC_IWR0   bfin_write_SICA_IWR0
#define bfin_write_SIC_IWR1   bfin_write_SICA_IWR1

#define bfin_read_SIC_IMASK0 bfin_read_SICA_IMASK0
#define bfin_read_SIC_IMASK1 bfin_read_SICA_IMASK1
#define bfin_read_SIC_IWR0   bfin_read_SICA_IWR0
#define bfin_read_SIC_IWR1   bfin_read_SICA_IWR1
#define bfin_read_SIC_ISR0   bfin_read_SICA_ISR0
#define bfin_read_SIC_ISR1   bfin_read_SICA_ISR1

#define bfin_read_SIC_IMASK(x)		bfin_read32(SICA_IMASK0 + (x << 2))
#define bfin_write_SIC_IMASK(x, val)	bfin_write32((SICA_IMASK0 + (x << 2)), val)
#define bfin_read_SIC_ISR(x)		bfin_read32(SICA_ISR0 + (x << 2))
#define bfin_write_SIC_ISR(x, val)	bfin_write32((SICA_ISR0 + (x << 2)), val)

#define BFIN_UART_NR_PORTS      1

#define OFFSET_THR              0x00	/* Transmit Holding register            */
#define OFFSET_RBR              0x00	/* Receive Buffer register              */
#define OFFSET_DLL              0x00	/* Divisor Latch (Low-Byte)             */
#define OFFSET_IER              0x04	/* Interrupt Enable Register            */
#define OFFSET_DLH              0x04	/* Divisor Latch (High-Byte)            */
#define OFFSET_IIR              0x08	/* Interrupt Identification Register    */
#define OFFSET_LCR              0x0C	/* Line Control Register                */
#define OFFSET_MCR              0x10	/* Modem Control Register               */
#define OFFSET_LSR              0x14	/* Line Status Register                 */
#define OFFSET_MSR              0x18	/* Modem Status Register                */
#define OFFSET_SCR              0x1C	/* SCR Scratch Register                 */
#define OFFSET_GCTL             0x24	/* Global Control Register              */

#endif				/* _MACH_BLACKFIN_H_ */
