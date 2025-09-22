/* $OpenBSD: i8259.h,v 1.8 2025/06/12 21:04:37 dv Exp $ */
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

#include <sys/types.h>

#define MASTER			0
#define SLAVE			1

#define ELCR0			0x4D0
#define ELCR1			0x4D1

#define ICW1_ICW4		(0x1 << 0)
#define ICW1_SNGL		(0x1 << 1)
#define ICW1_ADI		(0x1 << 2)
#define ICW1_LTIM		(0x1 << 3)
#define ICW1_INIT		(0x1 << 4)
#define ICW1_IVA1		(0x1 << 5)
#define ICW1_IVA2		(0x1 << 6)
#define ICW1_IVA3		(0x1 << 7)
#define ICW4_UP			(0x1 << 0)
#define ICW4_AEOI		(0x1 << 1)
#define ICW4_MS			(0x1 << 2)
#define ICW4_BUF		(0x1 << 3)
#define ICW4_SNFM		(0x1 << 4)

#define OCW_SELECT		(0x1 << 3)

#define OCW2_ROTATE_AEOI_CLEAR	0x00
#define OCW2_EOI		0x20
#define OCW2_NOP		0x40
#define OCW2_SEOI		0x60
#define OCW2_ROTATE_AEOI_SET	0x80
#define OCW2_ROTATE_NSEOI	0xA0
#define OCW2_SET_LOWPRIO	0xC0
#define OCW2_ROTATE_SEOI	0xE0
#define OCW3_RIS		(0x1 << 0)
#define OCW3_RR			(0x1 << 1)
#define OCW3_POLL		(0x1 << 2)
#define OCW3_ACTION		(0x1 << 3)
#define OCW3_SMM		(0x1 << 5)
#define OCW3_SMACTION		(0x1 << 6)

/* PIC functions called by device emulation code */
void i8259_assert_irq(uint8_t);
void i8259_deassert_irq(uint8_t);

/* PIC functions called by the in/out exit handler */
uint8_t vcpu_exit_i8259(struct vm_run_params *);

void i8259_init(void);
uint16_t i8259_ack(void);
uint8_t i8259_is_pending(void);

/* ELCR functions */
void pic_set_elcr(uint8_t, uint8_t);
uint8_t vcpu_exit_elcr(struct vm_run_params *);
