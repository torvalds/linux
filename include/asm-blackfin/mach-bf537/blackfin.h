/*
 * File:         include/asm-blackfin/mach-bf537/blackfin.h
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

#define BF537_FAMILY

#include "bf537.h"
#include "mem_map.h"
#include "defBF534.h"
#include "anomaly.h"

#if defined(CONFIG_BF537) || defined(CONFIG_BF536)
#include "defBF537.h"
#endif

#if !(defined(__ASSEMBLY__) || defined(ASSEMBLY))
#include "cdefBF534.h"

/* UART 0*/
#define bfin_read_UART_THR() bfin_read_UART0_THR()
#define bfin_write_UART_THR(val) bfin_write_UART0_THR(val)
#define bfin_read_UART_RBR() bfin_read_UART0_RBR()
#define bfin_write_UART_RBR(val) bfin_write_UART0_RBR(val)
#define bfin_read_UART_DLL() bfin_read_UART0_DLL()
#define bfin_write_UART_DLL(val) bfin_write_UART0_DLL(val)
#define bfin_read_UART_IER() bfin_read_UART0_IER()
#define bfin_write_UART_IER(val) bfin_write_UART0_IER(val)
#define bfin_read_UART_DLH() bfin_read_UART0_DLH()
#define bfin_write_UART_DLH(val) bfin_write_UART0_DLH(val)
#define bfin_read_UART_IIR() bfin_read_UART0_IIR()
#define bfin_write_UART_IIR(val) bfin_write_UART0_IIR(val)
#define bfin_read_UART_LCR() bfin_read_UART0_LCR()
#define bfin_write_UART_LCR(val) bfin_write_UART0_LCR(val)
#define bfin_read_UART_MCR() bfin_read_UART0_MCR()
#define bfin_write_UART_MCR(val) bfin_write_UART0_MCR(val)
#define bfin_read_UART_LSR() bfin_read_UART0_LSR()
#define bfin_write_UART_LSR(val) bfin_write_UART0_LSR(val)
#define bfin_read_UART_SCR() bfin_read_UART0_SCR()
#define bfin_write_UART_SCR(val) bfin_write_UART0_SCR(val)
#define bfin_read_UART_GCTL() bfin_read_UART0_GCTL()
#define bfin_write_UART_GCTL(val) bfin_write_UART0_GCTL(val)

#if defined(CONFIG_BF537) || defined(CONFIG_BF536)
#include "cdefBF537.h"
#endif
#endif

/* MAP used DEFINES from BF533 to BF537 - so we don't need to change them in the driver, kernel, etc. */

/* UART_IIR Register */
#define STATUS(x)	((x << 1) & 0x06)
#define STATUS_P1	0x02
#define STATUS_P0	0x01

/* UART 0*/

/* DMA Channnel */
#define bfin_read_CH_UART_RX() bfin_read_CH_UART0_RX()
#define bfin_write_CH_UART_RX(val) bfin_write_CH_UART0_RX(val)
#define CH_UART_RX CH_UART0_RX
#define bfin_read_CH_UART_TX() bfin_read_CH_UART0_TX()
#define bfin_write_CH_UART_TX(val) bfin_write_CH_UART0_TX(val)
#define CH_UART_TX CH_UART0_TX

/* System Interrupt Controller */
#define bfin_read_IRQ_UART_RX() bfin_read_IRQ_UART0_RX()
#define bfin_write_IRQ_UART_RX(val) bfin_write_IRQ_UART0_RX(val)
#define IRQ_UART_RX IRQ_UART0_RX
#define bfin_read_IRQ_UART_TX() bfin_read_IRQ_UART0_TX()
#define bfin_write_IRQ_UART_TX(val) bfin_write_IRQ_UART0_TX(val)
#define	IRQ_UART_TX IRQ_UART0_TX
#define bfin_read_IRQ_UART_ERROR() bfin_read_IRQ_UART0_ERROR()
#define bfin_write_IRQ_UART_ERROR(val) bfin_write_IRQ_UART0_ERROR(val)
#define	IRQ_UART_ERROR IRQ_UART0_ERROR

/* MMR Registers*/
#define bfin_read_UART_THR() bfin_read_UART0_THR()
#define bfin_write_UART_THR(val) bfin_write_UART0_THR(val)
#define UART_THR UART0_THR
#define bfin_read_UART_RBR() bfin_read_UART0_RBR()
#define bfin_write_UART_RBR(val) bfin_write_UART0_RBR(val)
#define UART_RBR UART0_RBR
#define bfin_read_UART_DLL() bfin_read_UART0_DLL()
#define bfin_write_UART_DLL(val) bfin_write_UART0_DLL(val)
#define UART_DLL UART0_DLL
#define bfin_read_UART_IER() bfin_read_UART0_IER()
#define bfin_write_UART_IER(val) bfin_write_UART0_IER(val)
#define UART_IER UART0_IER
#define bfin_read_UART_DLH() bfin_read_UART0_DLH()
#define bfin_write_UART_DLH(val) bfin_write_UART0_DLH(val)
#define UART_DLH UART0_DLH
#define bfin_read_UART_IIR() bfin_read_UART0_IIR()
#define bfin_write_UART_IIR(val) bfin_write_UART0_IIR(val)
#define UART_IIR UART0_IIR
#define bfin_read_UART_LCR() bfin_read_UART0_LCR()
#define bfin_write_UART_LCR(val) bfin_write_UART0_LCR(val)
#define UART_LCR UART0_LCR
#define bfin_read_UART_MCR() bfin_read_UART0_MCR()
#define bfin_write_UART_MCR(val) bfin_write_UART0_MCR(val)
#define UART_MCR UART0_MCR
#define bfin_read_UART_LSR() bfin_read_UART0_LSR()
#define bfin_write_UART_LSR(val) bfin_write_UART0_LSR(val)
#define UART_LSR UART0_LSR
#define bfin_read_UART_SCR() bfin_read_UART0_SCR()
#define bfin_write_UART_SCR(val) bfin_write_UART0_SCR(val)
#define UART_SCR  UART0_SCR
#define bfin_read_UART_GCTL() bfin_read_UART0_GCTL()
#define bfin_write_UART_GCTL(val) bfin_write_UART0_GCTL(val)
#define UART_GCTL UART0_GCTL

/* DPMC*/
#define bfin_read_STOPCK_OFF() bfin_read_STOPCK()
#define bfin_write_STOPCK_OFF(val) bfin_write_STOPCK(val)
#define STOPCK_OFF STOPCK

/* FIO USE PORT F*/
#ifdef CONFIG_BF537_PORT_F
#define bfin_read_PORT_FER() bfin_read_PORTF_FER()
#define bfin_write_PORT_FER(val) bfin_write_PORTF_FER(val)
#define bfin_read_FIO_FLAG_D() bfin_read_PORTFIO()
#define bfin_write_FIO_FLAG_D(val) bfin_write_PORTFIO(val)
#define bfin_read_FIO_FLAG_C() bfin_read_PORTFIO_CLEAR()
#define bfin_write_FIO_FLAG_C(val) bfin_write_PORTFIO_CLEAR(val)
#define bfin_read_FIO_FLAG_S() bfin_read_PORTFIO_SET()
#define bfin_write_FIO_FLAG_S(val) bfin_write_PORTFIO_SET(val)
#define bfin_read_FIO_FLAG_T() bfin_read_PORTFIO_TOGGLE()
#define bfin_write_FIO_FLAG_T(val) bfin_write_PORTFIO_TOGGLE(val)
#define bfin_read_FIO_MASKA_D() bfin_read_PORTFIO_MASKA()
#define bfin_write_FIO_MASKA_D(val) bfin_write_PORTFIO_MASKA(val)
#define bfin_read_FIO_MASKA_C() bfin_read_PORTFIO_MASKA_CLEAR()
#define bfin_write_FIO_MASKA_C(val) bfin_write_PORTFIO_MASKA_CLEAR(val)
#define bfin_read_FIO_MASKA_S() bfin_read_PORTFIO_MASKA_SET()
#define bfin_write_FIO_MASKA_S(val) bfin_write_PORTFIO_MASKA_SET(val)
#define bfin_read_FIO_MASKA_T() bfin_read_PORTFIO_MASKA_TOGGLE()
#define bfin_write_FIO_MASKA_T(val) bfin_write_PORTFIO_MASKA_TOGGLE(val)
#define bfin_read_FIO_MASKB_D() bfin_read_PORTFIO_MASKB()
#define bfin_write_FIO_MASKB_D(val) bfin_write_PORTFIO_MASKB(val)
#define bfin_read_FIO_MASKB_C() bfin_read_PORTFIO_MASKB_CLEAR()
#define bfin_write_FIO_MASKB_C(val) bfin_write_PORTFIO_MASKB_CLEAR(val)
#define bfin_read_FIO_MASKB_S() bfin_read_PORTFIO_MASKB_SET()
#define bfin_write_FIO_MASKB_S(val) bfin_write_PORTFIO_MASKB_SET(val)
#define bfin_read_FIO_MASKB_T() bfin_read_PORTFIO_MASKB_TOGGLE()
#define bfin_write_FIO_MASKB_T(val) bfin_write_PORTFIO_MASKB_TOGGLE(val)
#define bfin_read_FIO_DIR() bfin_read_PORTFIO_DIR()
#define bfin_write_FIO_DIR(val) bfin_write_PORTFIO_DIR(val)
#define bfin_read_FIO_POLAR() bfin_read_PORTFIO_POLAR()
#define bfin_write_FIO_POLAR(val) bfin_write_PORTFIO_POLAR(val)
#define bfin_read_FIO_EDGE() bfin_read_PORTFIO_EDGE()
#define bfin_write_FIO_EDGE(val) bfin_write_PORTFIO_EDGE(val)
#define bfin_read_FIO_BOTH() bfin_read_PORTFIO_BOTH()
#define bfin_write_FIO_BOTH(val) bfin_write_PORTFIO_BOTH(val)
#define bfin_read_FIO_INEN() bfin_read_PORTFIO_INEN()
#define bfin_write_FIO_INEN(val) bfin_write_PORTFIO_INEN(val)

#define bfin_read_FIO_FLAG_D() bfin_read_PORTFIO()
#define bfin_write_FIO_FLAG_D(val) bfin_write_PORTFIO(val)
#define FIO_FLAG_D		PORTFIO
#define bfin_read_FIO_FLAG_C() bfin_read_PORTFIO_CLEAR()
#define bfin_write_FIO_FLAG_C(val) bfin_write_PORTFIO_CLEAR(val)
#define FIO_FLAG_C		PORTFIO_CLEAR
#define bfin_read_FIO_FLAG_S() bfin_read_PORTFIO_SET()
#define bfin_write_FIO_FLAG_S(val) bfin_write_PORTFIO_SET(val)
#define FIO_FLAG_S		PORTFIO_SET
#define bfin_read_FIO_FLAG_T() bfin_read_PORTFIO_TOGGLE()
#define bfin_write_FIO_FLAG_T(val) bfin_write_PORTFIO_TOGGLE(val)
#define FIO_FLAG_T		PORTFIO_TOGGLE
#define bfin_read_FIO_MASKA_D() bfin_read_PORTFIO_MASKA()
#define bfin_write_FIO_MASKA_D(val) bfin_write_PORTFIO_MASKA(val)
#define FIO_MASKA_D	    PORTFIO_MASKA
#define bfin_read_FIO_MASKA_C() bfin_read_PORTFIO_MASKA_CLEAR()
#define bfin_write_FIO_MASKA_C(val) bfin_write_PORTFIO_MASKA_CLEAR(val)
#define FIO_MASKA_C     PORTFIO_MASKA_CLEAR
#define bfin_read_FIO_MASKA_S() bfin_read_PORTFIO_MASKA_SET()
#define bfin_write_FIO_MASKA_S(val) bfin_write_PORTFIO_MASKA_SET(val)
#define FIO_MASKA_S     PORTFIO_MASKA_SET
#define bfin_read_FIO_MASKA_T() bfin_read_PORTFIO_MASKA_TOGGLE()
#define bfin_write_FIO_MASKA_T(val) bfin_write_PORTFIO_MASKA_TOGGLE(val)
#define FIO_MASKA_T     PORTFIO_MASKA_TOGGLE
#define bfin_read_FIO_MASKB_D() bfin_read_PORTFIO_MASKB()
#define bfin_write_FIO_MASKB_D(val) bfin_write_PORTFIO_MASKB(val)
#define FIO_MASKB_D     PORTFIO_MASKB
#define bfin_read_FIO_MASKB_C() bfin_read_PORTFIO_MASKB_CLEAR()
#define bfin_write_FIO_MASKB_C(val) bfin_write_PORTFIO_MASKB_CLEAR(val)
#define FIO_MASKB_C     PORTFIO_MASKB_CLEAR
#define bfin_read_FIO_MASKB_S() bfin_read_PORTFIO_MASKB_SET()
#define bfin_write_FIO_MASKB_S(val) bfin_write_PORTFIO_MASKB_SET(val)
#define FIO_MASKB_S     PORTFIO_MASKB_SET
#define bfin_read_FIO_MASKB_T() bfin_read_PORTFIO_MASKB_TOGGLE()
#define bfin_write_FIO_MASKB_T(val) bfin_write_PORTFIO_MASKB_TOGGLE(val)
#define FIO_MASKB_T     PORTFIO_MASKB_TOGGLE
#define bfin_read_FIO_DIR() bfin_read_PORTFIO_DIR()
#define bfin_write_FIO_DIR(val) bfin_write_PORTFIO_DIR(val)
#define FIO_DIR		    PORTFIO_DIR
#define bfin_read_FIO_POLAR() bfin_read_PORTFIO_POLAR()
#define bfin_write_FIO_POLAR(val) bfin_write_PORTFIO_POLAR(val)
#define FIO_POLAR		PORTFIO_POLAR
#define bfin_read_FIO_EDGE() bfin_read_PORTFIO_EDGE()
#define bfin_write_FIO_EDGE(val) bfin_write_PORTFIO_EDGE(val)
#define FIO_EDGE		PORTFIO_EDGE
#define bfin_read_FIO_BOTH() bfin_read_PORTFIO_BOTH()
#define bfin_write_FIO_BOTH(val) bfin_write_PORTFIO_BOTH(val)
#define FIO_BOTH		PORTFIO_BOTH
#define bfin_read_FIO_INEN() bfin_read_PORTFIO_INEN()
#define bfin_write_FIO_INEN(val) bfin_write_PORTFIO_INEN(val)
#define FIO_INEN		PORTFIO_INEN
#endif

/* FIO USE PORT G*/
#ifdef CONFIG_BF537_PORT_G
#define bfin_read_PORT_FER() bfin_read_PORTG_FER()
#define bfin_write_PORT_FER(val) bfin_write_PORTG_FER(val)
#define bfin_read_FIO_FLAG_D() bfin_read_PORTGIO()
#define bfin_write_FIO_FLAG_D(val) bfin_write_PORTGIO(val)
#define bfin_read_FIO_FLAG_C() bfin_read_PORTGIO_CLEAR()
#define bfin_write_FIO_FLAG_C(val) bfin_write_PORTGIO_CLEAR(val)
#define bfin_read_FIO_FLAG_S() bfin_read_PORTGIO_SET()
#define bfin_write_FIO_FLAG_S(val) bfin_write_PORTGIO_SET(val)
#define bfin_read_FIO_FLAG_T() bfin_read_PORTGIO_TOGGLE()
#define bfin_write_FIO_FLAG_T(val) bfin_write_PORTGIO_TOGGLE(val)
#define bfin_read_FIO_MASKA_D() bfin_read_PORTGIO_MASKA()
#define bfin_write_FIO_MASKA_D(val) bfin_write_PORTGIO_MASKA(val)
#define bfin_read_FIO_MASKA_C() bfin_read_PORTGIO_MASKA_CLEAR()
#define bfin_write_FIO_MASKA_C(val) bfin_write_PORTGIO_MASKA_CLEAR(val)
#define bfin_read_FIO_MASKA_S() bfin_read_PORTGIO_MASKA_SET()
#define bfin_write_FIO_MASKA_S(val) bfin_write_PORTGIO_MASKA_SET(val)
#define bfin_read_FIO_MASKA_T() bfin_read_PORTGIO_MASKA_TOGGLE()
#define bfin_write_FIO_MASKA_T(val) bfin_write_PORTGIO_MASKA_TOGGLE(val)
#define bfin_read_FIO_MASKB_D() bfin_read_PORTGIO_MASKB()
#define bfin_write_FIO_MASKB_D(val) bfin_write_PORTGIO_MASKB(val)
#define bfin_read_FIO_MASKB_C() bfin_read_PORTGIO_MASKB_CLEAR()
#define bfin_write_FIO_MASKB_C(val) bfin_write_PORTGIO_MASKB_CLEAR(val)
#define bfin_read_FIO_MASKB_S() bfin_read_PORTGIO_MASKB_SET()
#define bfin_write_FIO_MASKB_S(val) bfin_write_PORTGIO_MASKB_SET(val)
#define bfin_read_FIO_MASKB_T() bfin_read_PORTGIO_MASKB_TOGGLE()
#define bfin_write_FIO_MASKB_T(val) bfin_write_PORTGIO_MASKB_TOGGLE(val)
#define bfin_read_FIO_DIR() bfin_read_PORTGIO_DIR()
#define bfin_write_FIO_DIR(val) bfin_write_PORTGIO_DIR(val)
#define bfin_read_FIO_POLAR() bfin_read_PORTGIO_POLAR()
#define bfin_write_FIO_POLAR(val) bfin_write_PORTGIO_POLAR(val)
#define bfin_read_FIO_EDGE() bfin_read_PORTGIO_EDGE()
#define bfin_write_FIO_EDGE(val) bfin_write_PORTGIO_EDGE(val)
#define bfin_read_FIO_BOTH() bfin_read_PORTGIO_BOTH()
#define bfin_write_FIO_BOTH(val) bfin_write_PORTGIO_BOTH(val)
#define bfin_read_FIO_INEN() bfin_read_PORTGIO_INEN()
#define bfin_write_FIO_INEN(val) bfin_write_PORTGIO_INEN(val)

#define bfin_read_FIO_FLAG_D() bfin_read_PORTGIO()
#define bfin_write_FIO_FLAG_D(val) bfin_write_PORTGIO(val)
#define FIO_FLAG_D		PORTGIO
#define bfin_read_FIO_FLAG_C() bfin_read_PORTGIO_CLEAR()
#define bfin_write_FIO_FLAG_C(val) bfin_write_PORTGIO_CLEAR(val)
#define FIO_FLAG_C		PORTGIO_CLEAR
#define bfin_read_FIO_FLAG_S() bfin_read_PORTGIO_SET()
#define bfin_write_FIO_FLAG_S(val) bfin_write_PORTGIO_SET(val)
#define FIO_FLAG_S		PORTGIO_SET
#define bfin_read_FIO_FLAG_T() bfin_read_PORTGIO_TOGGLE()
#define bfin_write_FIO_FLAG_T(val) bfin_write_PORTGIO_TOGGLE(val)
#define FIO_FLAG_T		PORTGIO_TOGGLE
#define bfin_read_FIO_MASKA_D() bfin_read_PORTGIO_MASKA()
#define bfin_write_FIO_MASKA_D(val) bfin_write_PORTGIO_MASKA(val)
#define FIO_MASKA_D	    PORTGIO_MASKA
#define bfin_read_FIO_MASKA_C() bfin_read_PORTGIO_MASKA_CLEAR()
#define bfin_write_FIO_MASKA_C(val) bfin_write_PORTGIO_MASKA_CLEAR(val)
#define FIO_MASKA_C	    PORTGIO_MASKA_CLEAR
#define bfin_read_FIO_MASKA_S() bfin_read_PORTGIO_MASKA_SET()
#define bfin_write_FIO_MASKA_S(val) bfin_write_PORTGIO_MASKA_SET(val)
#define FIO_MASKA_S	    PORTGIO_MASKA_SET
#define bfin_read_FIO_MASKA_T() bfin_read_PORTGIO_MASKA_TOGGLE()
#define bfin_write_FIO_MASKA_T(val) bfin_write_PORTGIO_MASKA_TOGGLE(val)
#define FIO_MASKA_T	    PORTGIO_MASKA_TOGGLE
#define bfin_read_FIO_MASKB_D() bfin_read_PORTGIO_MASKB()
#define bfin_write_FIO_MASKB_D(val) bfin_write_PORTGIO_MASKB(val)
#define FIO_MASKB_D	    PORTGIO_MASKB
#define bfin_read_FIO_MASKB_C() bfin_read_PORTGIO_MASKB_CLEAR()
#define bfin_write_FIO_MASKB_C(val) bfin_write_PORTGIO_MASKB_CLEAR(val)
#define FIO_MASKB_C	    PORTGIO_MASKB_CLEAR
#define bfin_read_FIO_MASKB_S() bfin_read_PORTGIO_MASKB_SET()
#define bfin_write_FIO_MASKB_S(val) bfin_write_PORTGIO_MASKB_SET(val)
#define FIO_MASKB_S	    PORTGIO_MASKB_SET
#define bfin_read_FIO_MASKB_T() bfin_read_PORTGIO_MASKB_TOGGLE()
#define bfin_write_FIO_MASKB_T(val) bfin_write_PORTGIO_MASKB_TOGGLE(val)
#define FIO_MASKB_T	    PORTGIO_MASKB_TOGGLE
#define bfin_read_FIO_DIR() bfin_read_PORTGIO_DIR()
#define bfin_write_FIO_DIR(val) bfin_write_PORTGIO_DIR(val)
#define FIO_DIR		    PORTGIO_DIR
#define bfin_read_FIO_POLAR() bfin_read_PORTGIO_POLAR()
#define bfin_write_FIO_POLAR(val) bfin_write_PORTGIO_POLAR(val)
#define FIO_POLAR		PORTGIO_POLAR
#define bfin_read_FIO_EDGE() bfin_read_PORTGIO_EDGE()
#define bfin_write_FIO_EDGE(val) bfin_write_PORTGIO_EDGE(val)
#define FIO_EDGE		PORTGIO_EDGE
#define bfin_read_FIO_BOTH() bfin_read_PORTGIO_BOTH()
#define bfin_write_FIO_BOTH(val) bfin_write_PORTGIO_BOTH(val)
#define FIO_BOTH		PORTGIO_BOTH
#define bfin_read_FIO_INEN() bfin_read_PORTGIO_INEN()
#define bfin_write_FIO_INEN(val) bfin_write_PORTGIO_INEN(val)
#define FIO_INEN		PORTGIO_INEN

#endif

/* FIO USE PORT H*/
#ifdef CONFIG_BF537_PORT_H
#define bfin_read_PORT_FER() bfin_read_PORTH_FER()
#define bfin_write_PORT_FER(val) bfin_write_PORTH_FER(val)
#define bfin_read_FIO_FLAG_D() bfin_read_PORTHIO()
#define bfin_write_FIO_FLAG_D(val) bfin_write_PORTHIO(val)
#define bfin_read_FIO_FLAG_C() bfin_read_PORTHIO_CLEAR()
#define bfin_write_FIO_FLAG_C(val) bfin_write_PORTHIO_CLEAR(val)
#define bfin_read_FIO_FLAG_S() bfin_read_PORTHIO_SET()
#define bfin_write_FIO_FLAG_S(val) bfin_write_PORTHIO_SET(val)
#define bfin_read_FIO_FLAG_T() bfin_read_PORTHIO_TOGGLE()
#define bfin_write_FIO_FLAG_T(val) bfin_write_PORTHIO_TOGGLE(val)
#define bfin_read_FIO_MASKA_D() bfin_read_PORTHIO_MASKA()
#define bfin_write_FIO_MASKA_D(val) bfin_write_PORTHIO_MASKA(val)
#define bfin_read_FIO_MASKA_C() bfin_read_PORTHIO_MASKA_CLEAR()
#define bfin_write_FIO_MASKA_C(val) bfin_write_PORTHIO_MASKA_CLEAR(val)
#define bfin_read_FIO_MASKA_S() bfin_read_PORTHIO_MASKA_SET()
#define bfin_write_FIO_MASKA_S(val) bfin_write_PORTHIO_MASKA_SET(val)
#define bfin_read_FIO_MASKA_T() bfin_read_PORTHIO_MASKA_TOGGLE()
#define bfin_write_FIO_MASKA_T(val) bfin_write_PORTHIO_MASKA_TOGGLE(val)
#define bfin_read_FIO_MASKB_D() bfin_read_PORTHIO_MASKB()
#define bfin_write_FIO_MASKB_D(val) bfin_write_PORTHIO_MASKB(val)
#define bfin_read_FIO_MASKB_C() bfin_read_PORTHIO_MASKB_CLEAR()
#define bfin_write_FIO_MASKB_C(val) bfin_write_PORTHIO_MASKB_CLEAR(val)
#define bfin_read_FIO_MASKB_S() bfin_read_PORTHIO_MASKB_SET()
#define bfin_write_FIO_MASKB_S(val) bfin_write_PORTHIO_MASKB_SET(val)
#define bfin_read_FIO_MASKB_T() bfin_read_PORTHIO_MASKB_TOGGLE()
#define bfin_write_FIO_MASKB_T(val) bfin_write_PORTHIO_MASKB_TOGGLE(val)
#define bfin_read_FIO_DIR() bfin_read_PORTHIO_DIR()
#define bfin_write_FIO_DIR(val) bfin_write_PORTHIO_DIR(val)
#define bfin_read_FIO_POLAR() bfin_read_PORTHIO_POLAR()
#define bfin_write_FIO_POLAR(val) bfin_write_PORTHIO_POLAR(val)
#define bfin_read_FIO_EDGE() bfin_read_PORTHIO_EDGE()
#define bfin_write_FIO_EDGE(val) bfin_write_PORTHIO_EDGE(val)
#define bfin_read_FIO_BOTH() bfin_read_PORTHIO_BOTH()
#define bfin_write_FIO_BOTH(val) bfin_write_PORTHIO_BOTH(val)
#define bfin_read_FIO_INEN() bfin_read_PORTHIO_INEN()
#define bfin_write_FIO_INEN(val) bfin_write_PORTHIO_INEN(val)

#define bfin_read_FIO_FLAG_D() bfin_read_PORTHIO()
#define bfin_write_FIO_FLAG_D(val) bfin_write_PORTHIO(val)
#define FIO_FLAG_D		PORTHIO
#define bfin_read_FIO_FLAG_C() bfin_read_PORTHIO_CLEAR()
#define bfin_write_FIO_FLAG_C(val) bfin_write_PORTHIO_CLEAR(val)
#define FIO_FLAG_C		PORTHIO_CLEAR
#define bfin_read_FIO_FLAG_S() bfin_read_PORTHIO_SET()
#define bfin_write_FIO_FLAG_S(val) bfin_write_PORTHIO_SET(val)
#define FIO_FLAG_S		PORTHIO_SET
#define bfin_read_FIO_FLAG_T() bfin_read_PORTHIO_TOGGLE()
#define bfin_write_FIO_FLAG_T(val) bfin_write_PORTHIO_TOGGLE(val)
#define FIO_FLAG_T		PORTHIO_TOGGLE
#define bfin_read_FIO_MASKA_D() bfin_read_PORTHIO_MASKA()
#define bfin_write_FIO_MASKA_D(val) bfin_write_PORTHIO_MASKA(val)
#define FIO_MASKA_D	    PORTHIO_MASKA
#define bfin_read_FIO_MASKA_C() bfin_read_PORTHIO_MASKA_CLEAR()
#define bfin_write_FIO_MASKA_C(val) bfin_write_PORTHIO_MASKA_CLEAR(val)
#define FIO_MASKA_C	    PORTHIO_MASKA_CLEAR
#define bfin_read_FIO_MASKA_S() bfin_read_PORTHIO_MASKA_SET()
#define bfin_write_FIO_MASKA_S(val) bfin_write_PORTHIO_MASKA_SET(val)
#define FIO_MASKA_S	    PORTHIO_MASKA_SET
#define bfin_read_FIO_MASKA_T() bfin_read_PORTHIO_MASKA_TOGGLE()
#define bfin_write_FIO_MASKA_T(val) bfin_write_PORTHIO_MASKA_TOGGLE(val)
#define FIO_MASKA_T	    PORTHIO_MASKA_TOGGLE
#define bfin_read_FIO_MASKB_D() bfin_read_PORTHIO_MASKB()
#define bfin_write_FIO_MASKB_D(val) bfin_write_PORTHIO_MASKB(val)
#define FIO_MASKB_D	    PORTHIO_MASKB
#define bfin_read_FIO_MASKB_C() bfin_read_PORTHIO_MASKB_CLEAR()
#define bfin_write_FIO_MASKB_C(val) bfin_write_PORTHIO_MASKB_CLEAR(val)
#define FIO_MASKB_C	    PORTHIO_MASKB_CLEAR
#define bfin_read_FIO_MASKB_S() bfin_read_PORTHIO_MASKB_SET()
#define bfin_write_FIO_MASKB_S(val) bfin_write_PORTHIO_MASKB_SET(val)
#define FIO_MASKB_S	    PORTHIO_MASKB_SET
#define bfin_read_FIO_MASKB_T() bfin_read_PORTHIO_MASKB_TOGGLE()
#define bfin_write_FIO_MASKB_T(val) bfin_write_PORTHIO_MASKB_TOGGLE(val)
#define FIO_MASKB_T	    PORTHIO_MASKB_TOGGLE
#define bfin_read_FIO_DIR() bfin_read_PORTHIO_DIR()
#define bfin_write_FIO_DIR(val) bfin_write_PORTHIO_DIR(val)
#define FIO_DIR		    PORTHIO_DIR
#define bfin_read_FIO_POLAR() bfin_read_PORTHIO_POLAR()
#define bfin_write_FIO_POLAR(val) bfin_write_PORTHIO_POLAR(val)
#define FIO_POLAR		PORTHIO_POLAR
#define bfin_read_FIO_EDGE() bfin_read_PORTHIO_EDGE()
#define bfin_write_FIO_EDGE(val) bfin_write_PORTHIO_EDGE(val)
#define FIO_EDGE		PORTHIO_EDGE
#define bfin_read_FIO_BOTH() bfin_read_PORTHIO_BOTH()
#define bfin_write_FIO_BOTH(val) bfin_write_PORTHIO_BOTH(val)
#define FIO_BOTH		PORTHIO_BOTH
#define bfin_read_FIO_INEN() bfin_read_PORTHIO_INEN()
#define bfin_write_FIO_INEN(val) bfin_write_PORTHIO_INEN(val)
#define FIO_INEN		PORTHIO_INEN

#endif

/* PLL_DIV Masks													*/
#define CCLK_DIV1 CSEL_DIV1	/*          CCLK = VCO / 1                                  */
#define CCLK_DIV2 CSEL_DIV2	/*          CCLK = VCO / 2                                  */
#define CCLK_DIV4 CSEL_DIV4	/*          CCLK = VCO / 4                                  */
#define CCLK_DIV8 CSEL_DIV8	/*          CCLK = VCO / 8                                  */

#endif
