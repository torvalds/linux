#ifndef _uart_h_included_
#define _uart_h_included_

/*
 * THIS FILE IS GENERATED -- DO NOT MODIFY BY HAND
 *
 * include/asm-xtensa/xtensa/xt2000-uart.h -- NatSemi PC16552D DUART
 * definitions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002 Tensilica Inc.
 */


#include <xtensa/xt2000.h>


/* 16550 UART DEVICE REGISTERS
   The XT2000 board aligns each register to a 32-bit word but the UART device only uses
   one byte of the word, which is the least-significant byte regardless of the
   endianness of the core (ie. byte offset 0 for little-endian and 3 for big-endian).
   So if using word accesses then endianness doesn't matter.
   The macros provided here do that.
*/
struct uart_dev_s {
  union {
    unsigned int rxb;	/* DLAB=0: receive buffer, read-only */
    unsigned int txb;	/* DLAB=0: transmit buffer, write-only */
    unsigned int dll;	/* DLAB=1: divisor, least-significant byte latch (was write-only?) */
  } w0;
  union {
    unsigned int ier;	/* DLAB=0: interrupt-enable register (was write-only?) */
    unsigned int dlm;	/* DLAB=1: divisor, most-significant byte latch (was write-only?) */
  } w1;

  union {
    unsigned int isr;	/* DLAB=0: interrupt status register, read-only */
    unsigned int fcr;	/* DLAB=0: FIFO control register, write-only */
    unsigned int afr;	/* DLAB=1: alternate function register */
  } w2;

  unsigned int lcr;	/* line control-register, write-only */
  unsigned int mcr;	/* modem control-regsiter, write-only */
  unsigned int lsr;	/* line status register, read-only */
  unsigned int msr;	/* modem status register, read-only */
  unsigned int scr;	/* scratch regsiter, read/write */
};

#define _RXB(u) ((u)->w0.rxb)
#define _TXB(u) ((u)->w0.txb)
#define _DLL(u) ((u)->w0.dll)
#define _IER(u) ((u)->w1.ier)
#define _DLM(u) ((u)->w1.dlm)
#define _ISR(u) ((u)->w2.isr)
#define _FCR(u) ((u)->w2.fcr)
#define _AFR(u) ((u)->w2.afr)
#define _LCR(u) ((u)->lcr)
#define _MCR(u) ((u)->mcr)
#define _LSR(u) ((u)->lsr)
#define _MSR(u) ((u)->msr)
#define _SCR(u) ((u)->scr)

typedef volatile struct uart_dev_s uart_dev_t;

/* IER bits */
#define RCVR_DATA_REG_INTENABLE 0x01
#define XMIT_HOLD_REG_INTENABLE    0x02
#define RCVR_STATUS_INTENABLE   0x04
#define MODEM_STATUS_INTENABLE     0x08

/* FCR bits */
#define _FIFO_ENABLE      0x01
#define RCVR_FIFO_RESET  0x02
#define XMIT_FIFO_RESET  0x04
#define DMA_MODE_SELECT  0x08
#define RCVR_TRIGGER_LSB 0x40
#define RCVR_TRIGGER_MSB 0x80

/* AFR bits */
#define AFR_CONC_WRITE	0x01
#define AFR_BAUDOUT_SEL	0x02
#define AFR_RXRDY_SEL	0x04

/* ISR bits */
#define INT_STATUS(r)   ((r)&1)
#define INT_PRIORITY(r) (((r)>>1)&0x7)

/* LCR bits */
#define WORD_LENGTH(n)  (((n)-5)&0x3)
#define STOP_BIT_ENABLE 0x04
#define PARITY_ENABLE   0x08
#define EVEN_PARITY     0x10
#define FORCE_PARITY    0x20
#define XMIT_BREAK      0x40
#define DLAB_ENABLE     0x80

/* MCR bits */
#define _DTR 0x01
#define _RTS 0x02
#define _OP1 0x04
#define _OP2 0x08
#define LOOP_BACK 0x10

/* LSR Bits */
#define RCVR_DATA_READY 0x01
#define OVERRUN_ERROR   0x02
#define PARITY_ERROR    0x04
#define FRAMING_ERROR   0x08
#define BREAK_INTERRUPT 0x10
#define XMIT_HOLD_EMPTY 0x20
#define XMIT_EMPTY      0x40
#define FIFO_ERROR      0x80
#define RCVR_READY(u)   (_LSR(u)&RCVR_DATA_READY)
#define XMIT_READY(u)   (_LSR(u)&XMIT_HOLD_EMPTY)

/* MSR bits */
#define _RDR       0x01
#define DELTA_DSR 0x02
#define DELTA_RI  0x04
#define DELTA_CD  0x08
#define _CTS       0x10
#define _DSR       0x20
#define _RI        0x40
#define _CD        0x80

/* prototypes */
void uart_init( uart_dev_t *u, int bitrate );
void uart_out( uart_dev_t *u, char c );
void uart_puts( uart_dev_t *u, char *s );
char uart_in( uart_dev_t *u );
void uart_enable_rcvr_int( uart_dev_t *u );
void uart_disable_rcvr_int( uart_dev_t *u );

#ifdef DUART16552_1_VADDR
/*  DUART present.  */
#define DUART_1_BASE	(*(uart_dev_t*)DUART16552_1_VADDR)
#define DUART_2_BASE	(*(uart_dev_t*)DUART16552_2_VADDR)
#define UART1_PUTS(s)	uart_puts( &DUART_1_BASE, s )
#define UART2_PUTS(s)	uart_puts( &DUART_2_BASE, s )
#else
/*  DUART not configured, use dummy placeholders to allow compiles to work.  */
#define DUART_1_BASE	(*(uart_dev_t*)0)
#define DUART_2_BASE	(*(uart_dev_t*)0)
#define UART1_PUTS(s)
#define UART2_PUTS(s)
#endif

/*  Compute 16-bit divisor for baudrate generator, with rounding:  */
#define DUART_DIVISOR(crystal,speed)	(((crystal)/16 + (speed)/2)/(speed))

#endif /*_uart_h_included_*/

