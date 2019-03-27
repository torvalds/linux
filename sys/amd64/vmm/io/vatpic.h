/*-
 * Copyright (c) 2014 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VATPIC_H_
#define	_VATPIC_H_

#include <isa/isareg.h>

#define	ICU_IMR_OFFSET	1

#define	IO_ELCR1	0x4d0
#define	IO_ELCR2	0x4d1

struct vatpic *vatpic_init(struct vm *vm);
void vatpic_cleanup(struct vatpic *vatpic);

int vatpic_master_handler(struct vm *vm, int vcpuid, bool in, int port,
    int bytes, uint32_t *eax);
int vatpic_slave_handler(struct vm *vm, int vcpuid, bool in, int port,
    int bytes, uint32_t *eax);
int vatpic_elc_handler(struct vm *vm, int vcpuid, bool in, int port, int bytes,
    uint32_t *eax);

int vatpic_assert_irq(struct vm *vm, int irq);
int vatpic_deassert_irq(struct vm *vm, int irq);
int vatpic_pulse_irq(struct vm *vm, int irq);
int vatpic_set_irq_trigger(struct vm *vm, int irq, enum vm_intr_trigger trigger);

void vatpic_pending_intr(struct vm *vm, int *vecptr);
void vatpic_intr_accepted(struct vm *vm, int vector);

#endif	/* _VATPIC_H_ */
