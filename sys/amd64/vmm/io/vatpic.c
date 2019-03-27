/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <x86/apicreg.h>
#include <dev/ic/i8259.h>

#include <machine/vmm.h>

#include "vmm_ktr.h"
#include "vmm_lapic.h"
#include "vioapic.h"
#include "vatpic.h"

static MALLOC_DEFINE(M_VATPIC, "atpic", "bhyve virtual atpic (8259)");

#define	VATPIC_LOCK(vatpic)		mtx_lock_spin(&((vatpic)->mtx))
#define	VATPIC_UNLOCK(vatpic)		mtx_unlock_spin(&((vatpic)->mtx))
#define	VATPIC_LOCKED(vatpic)		mtx_owned(&((vatpic)->mtx))

enum irqstate {
	IRQSTATE_ASSERT,
	IRQSTATE_DEASSERT,
	IRQSTATE_PULSE
};

struct atpic {
	bool		ready;
	int		icw_num;
	int		rd_cmd_reg;

	bool		aeoi;
	bool		poll;
	bool		rotate;
	bool		sfn;		/* special fully-nested mode */

	int		irq_base;
	uint8_t		request;	/* Interrupt Request Register (IIR) */
	uint8_t		service;	/* Interrupt Service (ISR) */
	uint8_t		mask;		/* Interrupt Mask Register (IMR) */
	uint8_t		smm;		/* special mask mode */

	int		acnt[8];	/* sum of pin asserts and deasserts */
	int		lowprio;	/* lowest priority irq */

	bool		intr_raised;
};

struct vatpic {
	struct vm	*vm;
	struct mtx	mtx;
	struct atpic	atpic[2];
	uint8_t		elc[2];
};

#define	VATPIC_CTR0(vatpic, fmt)					\
	VM_CTR0((vatpic)->vm, fmt)

#define	VATPIC_CTR1(vatpic, fmt, a1)					\
	VM_CTR1((vatpic)->vm, fmt, a1)

#define	VATPIC_CTR2(vatpic, fmt, a1, a2)				\
	VM_CTR2((vatpic)->vm, fmt, a1, a2)

#define	VATPIC_CTR3(vatpic, fmt, a1, a2, a3)				\
	VM_CTR3((vatpic)->vm, fmt, a1, a2, a3)

#define	VATPIC_CTR4(vatpic, fmt, a1, a2, a3, a4)			\
	VM_CTR4((vatpic)->vm, fmt, a1, a2, a3, a4)

/*
 * Loop over all the pins in priority order from highest to lowest.
 */
#define	ATPIC_PIN_FOREACH(pinvar, atpic, tmpvar)			\
	for (tmpvar = 0, pinvar = (atpic->lowprio + 1) & 0x7;		\
	    tmpvar < 8;							\
	    tmpvar++, pinvar = (pinvar + 1) & 0x7)

static void vatpic_set_pinstate(struct vatpic *vatpic, int pin, bool newstate);

static __inline bool
master_atpic(struct vatpic *vatpic, struct atpic *atpic)
{

	if (atpic == &vatpic->atpic[0])
		return (true);
	else
		return (false);
}

static __inline int
vatpic_get_highest_isrpin(struct atpic *atpic)
{
	int bit, pin;
	int i;

	ATPIC_PIN_FOREACH(pin, atpic, i) {
                bit = (1 << pin);

		if (atpic->service & bit) {
			/*
			 * An IS bit that is masked by an IMR bit will not be
			 * cleared by a non-specific EOI in Special Mask Mode.
			 */
			if (atpic->smm && (atpic->mask & bit) != 0)
				continue;
			else
				return (pin);
		}
	}

	return (-1);
}

static __inline int
vatpic_get_highest_irrpin(struct atpic *atpic)
{
	int serviced;
	int bit, pin, tmp;

	/*
	 * In 'Special Fully-Nested Mode' when an interrupt request from
	 * a slave is in service, the slave is not locked out from the
	 * master's priority logic.
	 */
	serviced = atpic->service;
	if (atpic->sfn)
		serviced &= ~(1 << 2);

	/*
	 * In 'Special Mask Mode', when a mask bit is set in OCW1 it inhibits
	 * further interrupts at that level and enables interrupts from all
	 * other levels that are not masked. In other words the ISR has no
	 * bearing on the levels that can generate interrupts.
	 */
	if (atpic->smm)
		serviced = 0;

	ATPIC_PIN_FOREACH(pin, atpic, tmp) {
		bit = 1 << pin;

		/*
		 * If there is already an interrupt in service at the same
		 * or higher priority then bail.
		 */
		if ((serviced & bit) != 0)
			break;

		/*
		 * If an interrupt is asserted and not masked then return
		 * the corresponding 'pin' to the caller.
		 */
		if ((atpic->request & bit) != 0 && (atpic->mask & bit) == 0)
			return (pin);
	}

	return (-1);
}

static void
vatpic_notify_intr(struct vatpic *vatpic)
{
	struct atpic *atpic;
	int pin;

	KASSERT(VATPIC_LOCKED(vatpic), ("vatpic_notify_intr not locked"));

	/*
	 * First check the slave.
	 */
	atpic = &vatpic->atpic[1];
	if (!atpic->intr_raised &&
	    (pin = vatpic_get_highest_irrpin(atpic)) != -1) {
		VATPIC_CTR4(vatpic, "atpic slave notify pin = %d "
		    "(imr 0x%x irr 0x%x isr 0x%x)", pin,
		    atpic->mask, atpic->request, atpic->service);

		/*
		 * Cascade the request from the slave to the master.
		 */
		atpic->intr_raised = true;
		vatpic_set_pinstate(vatpic, 2, true);
		vatpic_set_pinstate(vatpic, 2, false);
	} else {
		VATPIC_CTR3(vatpic, "atpic slave no eligible interrupts "
		    "(imr 0x%x irr 0x%x isr 0x%x)",
		    atpic->mask, atpic->request, atpic->service);
	}

	/*
	 * Then check the master.
	 */
	atpic = &vatpic->atpic[0];
	if (!atpic->intr_raised &&
	    (pin = vatpic_get_highest_irrpin(atpic)) != -1) {
		VATPIC_CTR4(vatpic, "atpic master notify pin = %d "
		    "(imr 0x%x irr 0x%x isr 0x%x)", pin,
		    atpic->mask, atpic->request, atpic->service);

		/*
		 * From Section 3.6.2, "Interrupt Modes", in the
		 * MPtable Specification, Version 1.4
		 *
		 * PIC interrupts are routed to both the Local APIC
		 * and the I/O APIC to support operation in 1 of 3
		 * modes.
		 *
		 * 1. Legacy PIC Mode: the PIC effectively bypasses
		 * all APIC components.  In this mode the local APIC is
		 * disabled and LINT0 is reconfigured as INTR to
		 * deliver the PIC interrupt directly to the CPU.
		 *
		 * 2. Virtual Wire Mode: the APIC is treated as a
		 * virtual wire which delivers interrupts from the PIC
		 * to the CPU.  In this mode LINT0 is programmed as
		 * ExtINT to indicate that the PIC is the source of
		 * the interrupt.
		 *
		 * 3. Virtual Wire Mode via I/O APIC: PIC interrupts are
		 * fielded by the I/O APIC and delivered to the appropriate
		 * CPU.  In this mode the I/O APIC input 0 is programmed
		 * as ExtINT to indicate that the PIC is the source of the
		 * interrupt.
		 */
		atpic->intr_raised = true;
		lapic_set_local_intr(vatpic->vm, -1, APIC_LVT_LINT0);
		vioapic_pulse_irq(vatpic->vm, 0);
	} else {
		VATPIC_CTR3(vatpic, "atpic master no eligible interrupts "
		    "(imr 0x%x irr 0x%x isr 0x%x)",
		    atpic->mask, atpic->request, atpic->service);
	}
}

static int
vatpic_icw1(struct vatpic *vatpic, struct atpic *atpic, uint8_t val)
{
	VATPIC_CTR1(vatpic, "atpic icw1 0x%x", val);

	atpic->ready = false;

	atpic->icw_num = 1;
	atpic->request = 0;
	atpic->mask = 0;
	atpic->lowprio = 7;
	atpic->rd_cmd_reg = 0;
	atpic->poll = 0;
	atpic->smm = 0;

	if ((val & ICW1_SNGL) != 0) {
		VATPIC_CTR0(vatpic, "vatpic cascade mode required");
		return (-1);
	}

	if ((val & ICW1_IC4) == 0) {
		VATPIC_CTR0(vatpic, "vatpic icw4 required");
		return (-1);
	}

	atpic->icw_num++;

	return (0);
}

static int
vatpic_icw2(struct vatpic *vatpic, struct atpic *atpic, uint8_t val)
{
	VATPIC_CTR1(vatpic, "atpic icw2 0x%x", val);

	atpic->irq_base = val & 0xf8;

	atpic->icw_num++;

	return (0);
}

static int
vatpic_icw3(struct vatpic *vatpic, struct atpic *atpic, uint8_t val)
{
	VATPIC_CTR1(vatpic, "atpic icw3 0x%x", val);

	atpic->icw_num++;

	return (0);
}

static int
vatpic_icw4(struct vatpic *vatpic, struct atpic *atpic, uint8_t val)
{
	VATPIC_CTR1(vatpic, "atpic icw4 0x%x", val);

	if ((val & ICW4_8086) == 0) {
		VATPIC_CTR0(vatpic, "vatpic microprocessor mode required");
		return (-1);
	}

	if ((val & ICW4_AEOI) != 0)
		atpic->aeoi = true;

	if ((val & ICW4_SFNM) != 0) {
		if (master_atpic(vatpic, atpic)) {
			atpic->sfn = true;
		} else {
			VATPIC_CTR1(vatpic, "Ignoring special fully nested "
			    "mode on slave atpic: %#x", val);
		}
	}

	atpic->icw_num = 0;
	atpic->ready = true;

	return (0);
}

static int
vatpic_ocw1(struct vatpic *vatpic, struct atpic *atpic, uint8_t val)
{
	VATPIC_CTR1(vatpic, "atpic ocw1 0x%x", val);

	atpic->mask = val & 0xff;

	return (0);
}

static int
vatpic_ocw2(struct vatpic *vatpic, struct atpic *atpic, uint8_t val)
{
	VATPIC_CTR1(vatpic, "atpic ocw2 0x%x", val);

	atpic->rotate = ((val & OCW2_R) != 0);

	if ((val & OCW2_EOI) != 0) {
		int isr_bit;

		if ((val & OCW2_SL) != 0) {
			/* specific EOI */
			isr_bit = val & 0x7;
		} else {
			/* non-specific EOI */
			isr_bit = vatpic_get_highest_isrpin(atpic);
		}

		if (isr_bit != -1) {
			atpic->service &= ~(1 << isr_bit);

			if (atpic->rotate)
				atpic->lowprio = isr_bit;
		}
	} else if ((val & OCW2_SL) != 0 && atpic->rotate == true) {
		/* specific priority */
		atpic->lowprio = val & 0x7;
	}

	return (0);
}

static int
vatpic_ocw3(struct vatpic *vatpic, struct atpic *atpic, uint8_t val)
{
	VATPIC_CTR1(vatpic, "atpic ocw3 0x%x", val);

	if (val & OCW3_ESMM) {
		atpic->smm = val & OCW3_SMM ? 1 : 0;
		VATPIC_CTR2(vatpic, "%s atpic special mask mode %s",
		    master_atpic(vatpic, atpic) ? "master" : "slave",
		    atpic->smm ?  "enabled" : "disabled");
	}

	if (val & OCW3_RR) {
		/* read register command */
		atpic->rd_cmd_reg = val & OCW3_RIS;

		/* Polling mode */
		atpic->poll = ((val & OCW3_P) != 0);
	}

	return (0);
}

static void
vatpic_set_pinstate(struct vatpic *vatpic, int pin, bool newstate)
{
	struct atpic *atpic;
	int oldcnt, newcnt;
	bool level;

	KASSERT(pin >= 0 && pin < 16,
	    ("vatpic_set_pinstate: invalid pin number %d", pin));
	KASSERT(VATPIC_LOCKED(vatpic),
	    ("vatpic_set_pinstate: vatpic is not locked"));

	atpic = &vatpic->atpic[pin >> 3];

	oldcnt = atpic->acnt[pin & 0x7];
	if (newstate)
		atpic->acnt[pin & 0x7]++;
	else
		atpic->acnt[pin & 0x7]--;
	newcnt = atpic->acnt[pin & 0x7];

	if (newcnt < 0) {
		VATPIC_CTR2(vatpic, "atpic pin%d: bad acnt %d", pin, newcnt);
	}

	level = ((vatpic->elc[pin >> 3] & (1 << (pin & 0x7))) != 0);

	if ((oldcnt == 0 && newcnt == 1) || (newcnt > 0 && level == true)) {
		/* rising edge or level */
		VATPIC_CTR1(vatpic, "atpic pin%d: asserted", pin);
		atpic->request |= (1 << (pin & 0x7));
	} else if (oldcnt == 1 && newcnt == 0) {
		/* falling edge */
		VATPIC_CTR1(vatpic, "atpic pin%d: deasserted", pin);
		if (level)
			atpic->request &= ~(1 << (pin & 0x7));
	} else {
		VATPIC_CTR3(vatpic, "atpic pin%d: %s, ignored, acnt %d",
		    pin, newstate ? "asserted" : "deasserted", newcnt);
	}

	vatpic_notify_intr(vatpic);
}

static int
vatpic_set_irqstate(struct vm *vm, int irq, enum irqstate irqstate)
{
	struct vatpic *vatpic;
	struct atpic *atpic;

	if (irq < 0 || irq > 15)
		return (EINVAL);

	vatpic = vm_atpic(vm);
	atpic = &vatpic->atpic[irq >> 3];

	if (atpic->ready == false)
		return (0);

	VATPIC_LOCK(vatpic);
	switch (irqstate) {
	case IRQSTATE_ASSERT:
		vatpic_set_pinstate(vatpic, irq, true);
		break;
	case IRQSTATE_DEASSERT:
		vatpic_set_pinstate(vatpic, irq, false);
		break;
	case IRQSTATE_PULSE:
		vatpic_set_pinstate(vatpic, irq, true);
		vatpic_set_pinstate(vatpic, irq, false);
		break;
	default:
		panic("vatpic_set_irqstate: invalid irqstate %d", irqstate);
	}
	VATPIC_UNLOCK(vatpic);

	return (0);
}

int
vatpic_assert_irq(struct vm *vm, int irq)
{
	return (vatpic_set_irqstate(vm, irq, IRQSTATE_ASSERT));
}

int
vatpic_deassert_irq(struct vm *vm, int irq)
{
	return (vatpic_set_irqstate(vm, irq, IRQSTATE_DEASSERT));
}

int
vatpic_pulse_irq(struct vm *vm, int irq)
{
	return (vatpic_set_irqstate(vm, irq, IRQSTATE_PULSE));
}

int
vatpic_set_irq_trigger(struct vm *vm, int irq, enum vm_intr_trigger trigger)
{
	struct vatpic *vatpic;

	if (irq < 0 || irq > 15)
		return (EINVAL);

	/*
	 * See comment in vatpic_elc_handler.  These IRQs must be
	 * edge triggered.
	 */
	if (trigger == LEVEL_TRIGGER) {
		switch (irq) {
		case 0:
		case 1:
		case 2:
		case 8:
		case 13:
			return (EINVAL);
		}
	}

	vatpic = vm_atpic(vm);

	VATPIC_LOCK(vatpic);

	if (trigger == LEVEL_TRIGGER)
		vatpic->elc[irq >> 3] |=  1 << (irq & 0x7);
	else
		vatpic->elc[irq >> 3] &=  ~(1 << (irq & 0x7));

	VATPIC_UNLOCK(vatpic);

	return (0);
}

void
vatpic_pending_intr(struct vm *vm, int *vecptr)
{
	struct vatpic *vatpic;
	struct atpic *atpic;
	int pin;

	vatpic = vm_atpic(vm);

	atpic = &vatpic->atpic[0];

	VATPIC_LOCK(vatpic);

	pin = vatpic_get_highest_irrpin(atpic);
	if (pin == 2) {
		atpic = &vatpic->atpic[1];
		pin = vatpic_get_highest_irrpin(atpic);
	}

	/*
	 * If there are no pins active at this moment then return the spurious
	 * interrupt vector instead.
	 */
	if (pin == -1)
		pin = 7;

	KASSERT(pin >= 0 && pin <= 7, ("%s: invalid pin %d", __func__, pin));
	*vecptr = atpic->irq_base + pin;

	VATPIC_UNLOCK(vatpic);
}

static void
vatpic_pin_accepted(struct atpic *atpic, int pin)
{
	atpic->intr_raised = false;

	if (atpic->acnt[pin] == 0)
		atpic->request &= ~(1 << pin);

	if (atpic->aeoi == true) {
		if (atpic->rotate == true)
			atpic->lowprio = pin;
	} else {
		atpic->service |= (1 << pin);
	}
}

void
vatpic_intr_accepted(struct vm *vm, int vector)
{
	struct vatpic *vatpic;
	int pin;

	vatpic = vm_atpic(vm);

	VATPIC_LOCK(vatpic);

	pin = vector & 0x7;

	if ((vector & ~0x7) == vatpic->atpic[1].irq_base) {
		vatpic_pin_accepted(&vatpic->atpic[1], pin);
		/*
		 * If this vector originated from the slave,
		 * accept the cascaded interrupt too.
		 */
		vatpic_pin_accepted(&vatpic->atpic[0], 2);
	} else {
		vatpic_pin_accepted(&vatpic->atpic[0], pin);
	}

	vatpic_notify_intr(vatpic);

	VATPIC_UNLOCK(vatpic);
}

static int
vatpic_read(struct vatpic *vatpic, struct atpic *atpic, bool in, int port,
	    int bytes, uint32_t *eax)
{
	int pin;

	VATPIC_LOCK(vatpic);

	if (atpic->poll) {
		atpic->poll = 0;
		pin = vatpic_get_highest_irrpin(atpic);
		if (pin >= 0) {
			vatpic_pin_accepted(atpic, pin);
			*eax = 0x80 | pin;
		} else {
			*eax = 0;
		}
	} else {
		if (port & ICU_IMR_OFFSET) {
			/* read interrrupt mask register */
			*eax = atpic->mask;
		} else {
			if (atpic->rd_cmd_reg == OCW3_RIS) {
				/* read interrupt service register */
				*eax = atpic->service;
			} else {
				/* read interrupt request register */
				*eax = atpic->request;
			}
		}
	}

	VATPIC_UNLOCK(vatpic);

	return (0);

}

static int
vatpic_write(struct vatpic *vatpic, struct atpic *atpic, bool in, int port,
    int bytes, uint32_t *eax)
{
	int error;
	uint8_t val;

	error = 0;
	val = *eax;

	VATPIC_LOCK(vatpic);

	if (port & ICU_IMR_OFFSET) {
		switch (atpic->icw_num) {
		case 2:
			error = vatpic_icw2(vatpic, atpic, val);
			break;
		case 3:
			error = vatpic_icw3(vatpic, atpic, val);
			break;
		case 4:
			error = vatpic_icw4(vatpic, atpic, val);
			break;
		default:
			error = vatpic_ocw1(vatpic, atpic, val);
			break;
		}
	} else {
		if (val & (1 << 4))
			error = vatpic_icw1(vatpic, atpic, val);

		if (atpic->ready) {
			if (val & (1 << 3))
				error = vatpic_ocw3(vatpic, atpic, val);
			else
				error = vatpic_ocw2(vatpic, atpic, val);
		}
	}

	if (atpic->ready)
		vatpic_notify_intr(vatpic);

	VATPIC_UNLOCK(vatpic);

	return (error);
}

int
vatpic_master_handler(struct vm *vm, int vcpuid, bool in, int port, int bytes,
    uint32_t *eax)
{
	struct vatpic *vatpic;
	struct atpic *atpic;

	vatpic = vm_atpic(vm);
	atpic = &vatpic->atpic[0];

	if (bytes != 1)
		return (-1);
 
	if (in) {
		return (vatpic_read(vatpic, atpic, in, port, bytes, eax));
	}
 
	return (vatpic_write(vatpic, atpic, in, port, bytes, eax));
}

int
vatpic_slave_handler(struct vm *vm, int vcpuid, bool in, int port, int bytes,
    uint32_t *eax)
{
	struct vatpic *vatpic;
	struct atpic *atpic;

	vatpic = vm_atpic(vm);
	atpic = &vatpic->atpic[1];

	if (bytes != 1)
		return (-1);

	if (in) {
		return (vatpic_read(vatpic, atpic, in, port, bytes, eax));
	}

	return (vatpic_write(vatpic, atpic, in, port, bytes, eax));
}

int
vatpic_elc_handler(struct vm *vm, int vcpuid, bool in, int port, int bytes,
    uint32_t *eax)
{
	struct vatpic *vatpic;
	bool is_master;

	vatpic = vm_atpic(vm);
	is_master = (port == IO_ELCR1);

	if (bytes != 1)
		return (-1);

	VATPIC_LOCK(vatpic);

	if (in) {
		if (is_master)
			*eax = vatpic->elc[0];
		else
			*eax = vatpic->elc[1];
	} else {
		/*
		 * For the master PIC the cascade channel (IRQ2), the
		 * heart beat timer (IRQ0), and the keyboard
		 * controller (IRQ1) cannot be programmed for level
		 * mode.
		 *
		 * For the slave PIC the real time clock (IRQ8) and
		 * the floating point error interrupt (IRQ13) cannot
		 * be programmed for level mode.
		 */
		if (is_master)
			vatpic->elc[0] = (*eax & 0xf8);
		else
			vatpic->elc[1] = (*eax & 0xde);
	}

	VATPIC_UNLOCK(vatpic);

	return (0);
}

struct vatpic *
vatpic_init(struct vm *vm)
{
	struct vatpic *vatpic;

	vatpic = malloc(sizeof(struct vatpic), M_VATPIC, M_WAITOK | M_ZERO);
	vatpic->vm = vm;

	mtx_init(&vatpic->mtx, "vatpic lock", NULL, MTX_SPIN);

	return (vatpic);
}

void
vatpic_cleanup(struct vatpic *vatpic)
{
	free(vatpic, M_VATPIC);
}
