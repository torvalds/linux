/* $OpenBSD: ns8250.h,v 1.11 2025/06/12 21:04:37 dv Exp $ */
/*
 * Copyright (c) 2016 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Emulated 8250 UART
 */
#define COM1_BASE       0x3f8
#define COM1_DATA	COM1_BASE+COM_OFFSET_DATA
#define COM1_IER	COM1_BASE+COM_OFFSET_IER
#define COM1_IIR	COM1_BASE+COM_OFFSET_IIR
#define COM1_LCR	COM1_BASE+COM_OFFSET_LCR
#define COM1_MCR	COM1_BASE+COM_OFFSET_MCR
#define COM1_LSR	COM1_BASE+COM_OFFSET_LSR
#define COM1_MSR	COM1_BASE+COM_OFFSET_MSR
#define COM1_SCR	COM1_BASE+COM_OFFSET_SCR

#define COM_OFFSET_DATA 0
#define COM_OFFSET_IER  1
#define COM_OFFSET_IIR  2
#define COM_OFFSET_LCR  3
#define COM_OFFSET_MCR  4
#define COM_OFFSET_LSR  5
#define COM_OFFSET_MSR  6
#define COM_OFFSET_SCR  7

/* ns8250 port identifier */
enum ns8250_portid {
	NS8250_COM1,
	NS8250_COM2,
};

/* ns8250 UART registers */
struct ns8250_regs {
	uint8_t lcr;	/* Line Control Register */
	uint8_t fcr;	/* FIFO Control Register */
	uint8_t iir;	/* Interrupt ID Register */
	uint8_t ier;	/* Interrupt Enable Register */
	uint8_t divlo;	/* Baud rate divisor low byte */
	uint8_t divhi;	/* Baud rate divisor high byte */
	uint8_t msr;	/* Modem Status Register */
	uint8_t lsr;	/* Line Status Register */
	uint8_t mcr;	/* Modem Control Register */
	uint8_t scr;	/* Scratch Register */
	uint8_t data;	/* Unread input data */
};

/* ns8250 UART device state */
struct ns8250_dev {
	pthread_mutex_t mutex;
	struct ns8250_regs regs;
	struct event event;
	struct event rate;
	struct event wake;
	struct timeval rate_tv;
	enum ns8250_portid portid;
	int fd;
	int irq;
	uint32_t vmid;
	uint64_t byte_out;
	uint32_t baudrate;
	uint32_t pause_ct;
};

void ns8250_init(int, uint32_t);
uint8_t vcpu_exit_com(struct vm_run_params *);
uint8_t vcpu_process_com_data(struct vm_exit *, uint32_t, uint32_t);
void vcpu_process_com_lcr(struct vm_exit *);
void vcpu_process_com_lsr(struct vm_exit *);
void vcpu_process_com_ier(struct vm_exit *);
void vcpu_process_com_mcr(struct vm_exit *);
void vcpu_process_com_iir(struct vm_exit *);
void vcpu_process_com_msr(struct vm_exit *);
void vcpu_process_com_scr(struct vm_exit *);
void ns8250_stop(void);
void ns8250_start(void);
