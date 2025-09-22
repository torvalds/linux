/* $OpenBSD: i8259.c,v 1.24 2025/06/12 21:04:37 dv Exp $ */
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

#include <string.h>

#include <sys/types.h>

#include <dev/isa/isareg.h>
#include <dev/vmm/vmm.h>

#include <unistd.h>
#include <pthread.h>

#include "atomicio.h"
#include "i8259.h"
#include "vmd.h"
#include "vmm.h"

struct i8259 {
	uint8_t irr;
	uint8_t imr;
	uint8_t isr;
	uint8_t smm;
	uint8_t poll;
	uint8_t cur_icw;
	uint8_t init_mode;
	uint8_t vec;
	uint8_t irq_conn;
	uint8_t next_ocw_read;
	uint8_t auto_eoi;
	uint8_t rotate_auto_eoi;
	uint8_t lowest_pri;
	uint8_t asserted;
};

/* Edge Level Control Registers */
uint8_t elcr[2];

#define PIC_IRR 0
#define PIC_ISR 1

/* Master and slave PICs */
struct i8259 pics[2];
pthread_mutex_t pic_mtx;

/*
 * i8259_pic_name
 *
 * Converts a pic ID (MASTER, SLAVE} to a string, suitable for printing in
 * debug or log messages.
 *
 * Parameters:
 *  picid: PIC ID
 *
 * Return value:
 *  string representation of the PIC ID supplied
 */
static const char *
i8259_pic_name(uint8_t picid)
{
	switch (picid) {
	case MASTER: return "master";
	case SLAVE: return "slave";
	default: return "unknown";
	}
}

/*
 * i8259_init
 *
 * Initialize the emulated i8259 PIC.
 */
void
i8259_init(void)
{
	memset(&pics, 0, sizeof(pics));
	pics[MASTER].cur_icw = 1;
	pics[SLAVE].cur_icw = 1;

	elcr[MASTER] = 0;
	elcr[SLAVE] = 0;

	if (pthread_mutex_init(&pic_mtx, NULL) != 0)
		fatalx("unable to create pic mutex");
}

/*
 * i8259_is_pending
 *
 * Determine if an IRQ is pending on either the slave or master PIC.
 *
 * Return Values:
 *  1 if an IRQ (any IRQ) is pending, 0 otherwise
 */
uint8_t
i8259_is_pending(void)
{
	uint8_t master_pending;
	uint8_t slave_pending;

	mutex_lock(&pic_mtx);
	master_pending = pics[MASTER].irr & ~(pics[MASTER].imr | (1 << 2));
	slave_pending = pics[SLAVE].irr & ~pics[SLAVE].imr;
	mutex_unlock(&pic_mtx);

	return (master_pending || slave_pending);
}

/*
 * i8259_ack
 *
 * This function is called when the vcpu exits and is ready to accept an
 * interrupt.
 *
 * Return values:
 *  interrupt vector to inject, 0xFFFF if no irq pending
 */
uint16_t
i8259_ack(void)
{
	uint8_t high_prio_m, high_prio_s;
	uint8_t i;
	uint16_t ret;

	ret = 0xFFFF;

	mutex_lock(&pic_mtx);

	if (pics[MASTER].asserted == 0 && pics[SLAVE].asserted == 0) {
		log_warnx("%s: i8259 ack without assert?", __func__);
		goto ret;
	}

	high_prio_m = pics[MASTER].lowest_pri + 1;
	if (high_prio_m > 7)
		high_prio_m = 0;

	high_prio_s = pics[SLAVE].lowest_pri + 1;
	if (high_prio_s > 7)
		high_prio_s = 0;

	i = high_prio_m;
	do {
		if ((pics[MASTER].irr & (1 << i)) && i != 2 &&
		    !(pics[MASTER].imr & (1 << i))) {
			/* Master PIC has highest prio and ready IRQ */
			pics[MASTER].irr &= ~(1 << i);
			pics[MASTER].isr |= (1 << i);

			if (pics[MASTER].irr == 0)
				pics[MASTER].asserted = 0;

			ret = i + pics[MASTER].vec;
			goto ret;
		}

		i++;

		if (i > 7)
			i = 0;

	} while (i != high_prio_m);

	i = high_prio_s;
	do {
		if ((pics[SLAVE].irr & (1 << i)) &&
		    !(pics[SLAVE].imr & (1 << i))) {
			/* Slave PIC has highest prio and ready IRQ */
			pics[SLAVE].irr &= ~(1 << i);
			pics[MASTER].irr &= ~(1 << 2);

			pics[SLAVE].isr |= (1 << i);
			pics[MASTER].isr |= (1 << 2);

			if (pics[SLAVE].irr == 0) {
				pics[SLAVE].asserted = 0;
				if (pics[MASTER].irr == 0)
					pics[MASTER].asserted = 0;
			}

			ret = i + pics[SLAVE].vec;
			goto ret;
		}

		i++;

		if (i > 7)
			i = 0;
	} while (i != high_prio_s);

	log_warnx("%s: ack without pending irq?", __func__);
ret:
	mutex_unlock(&pic_mtx);
	return (ret);
}

/*
 * i8259_assert_irq
 *
 * Asserts the IRQ specified
 *
 * Parameters:
 *  irq: the IRQ to assert
 */
void
i8259_assert_irq(uint8_t irq)
{
	mutex_lock(&pic_mtx);
	if (irq <= 7) {
		SET(pics[MASTER].irr, 1 << irq);
		pics[MASTER].asserted = 1;
	} else {
		irq -= 8;
		SET(pics[SLAVE].irr, 1 << irq);
		pics[SLAVE].asserted = 1;

		/* Assert cascade IRQ on master PIC */
		SET(pics[MASTER].irr, 1 << 2);
		pics[MASTER].asserted = 1;
	}
	mutex_unlock(&pic_mtx);
}

/*
 * i8259_deassert_irq
 *
 * Deasserts the IRQ specified
 *
 * Parameters:
 *  irq: the IRQ to deassert
 */
void
i8259_deassert_irq(uint8_t irq)
{
	mutex_lock(&pic_mtx);
	if (irq <= 7) {
		if (elcr[MASTER] & (1 << irq))
			CLR(pics[MASTER].irr, 1 << irq);
	} else {
		irq -= 8;
		if (elcr[SLAVE] & (1 << irq)) {
			CLR(pics[SLAVE].irr, 1 << irq);

			/*
			 * Deassert cascade IRQ on master if no IRQs on
			 * slave
			 */
			if (pics[SLAVE].irr == 0)
				CLR(pics[MASTER].irr, 1 << 2);
		}
	}
	mutex_unlock(&pic_mtx);
}

/*
 * i8259_write_datareg
 *
 * Write to a specified data register in the emulated PIC during PIC
 * initialization. The data write follows the state model in the i8259 (in
 * other words, data is expected to be written in a specific order).
 *
 * Parameters:
 *  n: PIC to write to (MASTER/SLAVE)
 *  data: data to write
 */
static void
i8259_write_datareg(uint8_t n, uint8_t data)
{
	struct i8259 *pic = &pics[n];

	if (pic->init_mode == 1) {
		if (pic->cur_icw == 2) {
			/* Set vector */
			log_debug("%s: %s pic, reset IRQ vector to 0x%x",
			    __func__, i8259_pic_name(n), data);
			pic->vec = data;
		} else if (pic->cur_icw == 3) {
			/* Set IRQ interconnects */
			if (n == SLAVE && (data & 0xf8)) {
				log_warnx("%s: %s pic invalid icw2 0x%x",
				    __func__, i8259_pic_name(n), data);
				return;
			}
			pic->irq_conn = data;
		} else if (pic->cur_icw == 4) {
			if (!(data & ICW4_UP)) {
				log_warnx("%s: %s pic init error: x86 bit "
				    "clear", __func__, i8259_pic_name(n));
				return;
			}

			if (data & ICW4_AEOI) {
				log_warnx("%s: %s pic: aeoi mode set",
				    __func__, i8259_pic_name(n));
				pic->auto_eoi = 1;
				return;
			}

			if (data & ICW4_MS) {
				log_warnx("%s: %s pic init error: M/S mode",
				    __func__, i8259_pic_name(n));
				return;
			}

			if (data & ICW4_BUF) {
				log_warnx("%s: %s pic init error: buf mode",
				    __func__, i8259_pic_name(n));
				return;
			}

			if (data & 0xe0) {
				log_warnx("%s: %s pic init error: invalid icw4 "
				    " 0x%x", __func__, i8259_pic_name(n), data);
				return;
			}
		}

		pic->cur_icw++;
		if (pic->cur_icw == 5) {
			pic->cur_icw = 1;
			pic->init_mode = 0;
		}
	} else
		pic->imr = data;
}

/*
 * i8259_specific_eoi
 *
 * Handles specific end of interrupt commands
 *
 * Parameters:
 *  n: PIC to deliver this EOI to
 *  data: interrupt to EOI
 */
static void
i8259_specific_eoi(uint8_t n, uint8_t data)
{
	if (!(pics[n].isr & (1 << (data & 0x7)))) {
		log_warnx("%s: %s pic specific eoi irq %d while not in"
		    " service", __func__, i8259_pic_name(n), (data & 0x7));
	}

	pics[n].isr &= ~(1 << (data & 0x7));
}

/*
 * i8259_nonspecific_eoi
 *
 * Handles nonspecific end of interrupt commands
 * XXX not implemented
 */
static void
i8259_nonspecific_eoi(uint8_t n, uint8_t data)
{
	int i = 0;

	while (i < 8) {
		if ((pics[n].isr & (1 << (i & 0x7)))) {
			i8259_specific_eoi(n, i);
			return;
		}
		i++;
	}
}

/*
 * i8259_rotate_priority
 *
 * Rotates the interrupt priority on the specified PIC
 *
 * Parameters:
 *  n: PIC whose priority should be rotated
 */
static void
i8259_rotate_priority(uint8_t n)
{
	pics[n].lowest_pri++;
	if (pics[n].lowest_pri > 7)
		pics[n].lowest_pri = 0;
}

/*
 * i8259_write_cmdreg
 *
 * Write to the PIC command register
 *
 * Parameters:
 *  n: PIC whose command register should be written to
 *  data: data to write
 */
static void
i8259_write_cmdreg(uint8_t n, uint8_t data)
{
	struct i8259 *pic = &pics[n];

	if (data & ICW1_INIT) {
		/* Validate init params */
		if (!(data & ICW1_ICW4)) {
			log_warnx("%s: %s pic init error: no ICW4 request",
			    __func__, i8259_pic_name(n));
			return;
		}

		if (data & (ICW1_IVA1 | ICW1_IVA2 | ICW1_IVA3)) {
			log_warnx("%s: %s pic init error: IVA specified",
			    __func__, i8259_pic_name(n));
			return;
		}

		if (data & ICW1_SNGL) {
			log_warnx("%s: %s pic init error: single pic mode",
			    __func__, i8259_pic_name(n));
			return;
		}

		if (data & ICW1_ADI) {
			log_warnx("%s: %s pic init error: address interval",
			    __func__, i8259_pic_name(n));
			return;
		}

		if (data & ICW1_LTIM) {
			log_warnx("%s: %s pic init error: level trigger mode",
			    __func__, i8259_pic_name(n));
			return;
		}

		pic->init_mode = 1;
		pic->cur_icw = 2;
		pic->imr = 0;
		pic->isr = 0;
		pic->irr = 0;
		pic->asserted = 0;
		pic->lowest_pri = 7;
		pic->rotate_auto_eoi = 0;
		return;
	} else if (data & OCW_SELECT) {
			/* OCW3 */
			if (data & OCW3_ACTION) {
				if (data & OCW3_RR) {
					if (data & OCW3_RIS)
						pic->next_ocw_read = PIC_ISR;
					else
						pic->next_ocw_read = PIC_IRR;
				}
			}

			if (data & OCW3_SMACTION) {
				if (data & OCW3_SMM) {
					pic->smm = 1;
					/* XXX update intr here */
				} else
					pic->smm = 0;
			}

			if (data & OCW3_POLL) {
				pic->poll = 1;
				/* XXX update intr here */
			}

			return;
	} else {
		/* OCW2 */
		if (data & OCW2_EOI) {
			/*
			 * An EOI command was received. It could be one of
			 * several different varieties:
			 *
			 * Nonspecific EOI (0x20)
			 * Specific EOI (0x60..0x67)
			 * Nonspecific EOI + rotate (0xA0)
			 * Specific EOI + rotate (0xE0..0xE7)
			 */
			switch (data) {
			case OCW2_EOI:
				i8259_nonspecific_eoi(n, data);
				break;
			case OCW2_SEOI ... OCW2_SEOI + 7:
				i8259_specific_eoi(n, data);
				break;
			case OCW2_ROTATE_NSEOI:
				i8259_nonspecific_eoi(n, data);
				i8259_rotate_priority(n);
				break;
			case OCW2_ROTATE_SEOI ... OCW2_ROTATE_SEOI + 7:
				i8259_specific_eoi(n, data);
				i8259_rotate_priority(n);
				break;
			}
			return;
		}

		if (data == OCW2_NOP)
			return;

		if ((data & OCW2_SET_LOWPRIO) == OCW2_SET_LOWPRIO) {
			/* Set low priority value (bits 0-2) */
			pic->lowest_pri = data & 0x7;
			return;
		}

		if (data == OCW2_ROTATE_AEOI_CLEAR) {
			pic->rotate_auto_eoi = 0;
			return;
		}

		if (data == OCW2_ROTATE_AEOI_SET) {
			pic->rotate_auto_eoi = 1;
			return;
		}

		return;
	}
}

/*
 * i8259_read_datareg
 *
 * Read the PIC's IMR
 *
 * Parameters:
 *  n: PIC to read
 *
 * Return value:
 *  selected PIC's IMR
 */
static uint8_t
i8259_read_datareg(uint8_t n)
{
	struct i8259 *pic = &pics[n];

	return (pic->imr);
}

/*
 * i8259_read_cmdreg
 *
 * Read the PIC's IRR or ISR, depending on the current PIC mode (value
 * selected via the OCW3 command)
 *
 * Parameters:
 *  n: PIC to read
 *
 * Return value:
 *  selected PIC's IRR/ISR
 */
static uint8_t
i8259_read_cmdreg(uint8_t n)
{
	struct i8259 *pic = &pics[n];

	if (pic->next_ocw_read == PIC_IRR)
		return (pic->irr);
	else if (pic->next_ocw_read == PIC_ISR)
		return (pic->isr);

	fatal("%s: invalid PIC config during cmdreg read", __func__);
}

/*
 * i8259_io_write
 *
 * Callback to handle write I/O to the emulated PICs in the VM
 *
 * Parameters:
 *  vei: vm exit info for this I/O
 */
static void
i8259_io_write(struct vm_exit *vei)
{
	uint16_t port = vei->vei.vei_port;
	uint32_t data = 0;
	uint8_t n = 0;

	get_input_data(vei, &data);

	switch (port) {
	case IO_ICU1:
	case IO_ICU1 + 1:
		n = MASTER;
		break;
	case IO_ICU2:
	case IO_ICU2 + 1:
		n = SLAVE;
		break;
	default:
		fatal("%s: invalid port 0x%x", __func__, port);
	}

	mutex_lock(&pic_mtx);
	if (port == IO_ICU1 + 1 || port == IO_ICU2 + 1)
		i8259_write_datareg(n, data);
	else
		i8259_write_cmdreg(n, data);
	mutex_unlock(&pic_mtx);
}

/*
 * i8259_io_read
 *
 * Callback to handle read I/O to the emulated PICs in the VM
 *
 * Parameters:
 *  vei: vm exit info for this I/O
 *
 * Return values:
 *  data that was read, based on the port information in 'vei'
 */
static uint8_t
i8259_io_read(struct vm_exit *vei)
{
	uint16_t port = vei->vei.vei_port;
	uint8_t n = 0;
	uint8_t rv;

	switch (port) {
	case IO_ICU1:
	case IO_ICU1 + 1:
		n = MASTER;
		break;
	case IO_ICU2:
	case IO_ICU2 + 1:
		n = SLAVE;
		break;
	default:
		fatal("%s: invalid port 0x%x", __func__, port);
	}

	mutex_lock(&pic_mtx);
	if (port == IO_ICU1 + 1 || port == IO_ICU2 + 1)
		rv = i8259_read_datareg(n);
	else
		rv = i8259_read_cmdreg(n);
	mutex_unlock(&pic_mtx);

	return (rv);
}

/*
 * vcpu_exit_i8259
 *
 * Top level exit handler for PIC operations
 *
 * Parameters:
 *  vrp: VCPU run parameters (contains exit information) for this PIC operation
 *
 * Return value:
 *  Always 0xFF (PIC read/writes don't generate interrupts directly)
 */
uint8_t
vcpu_exit_i8259(struct vm_run_params *vrp)
{
	struct vm_exit *vei = vrp->vrp_exit;

	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		i8259_io_write(vei);
	} else {
		set_return_data(vei, i8259_io_read(vei));
	}

	return (0xFF);
}

/*
 * pic_set_elcr
 *
 * Sets edge or level triggered mode for the given IRQ. Used internally
 * by the vmd PCI setup code. Guest VMs writing to ELCRx will do so via
 * vcpu_exit_elcr.
 *
 * Parameters:
 *  irq: IRQ (0-15) to set
 *  val: 0 if edge triggered mode, 1 if level triggered mode
 */
void
pic_set_elcr(uint8_t irq, uint8_t val)
{
	if (irq > 15 || val > 1)
		return;

	log_debug("%s: setting %s triggered mode for irq %d", __func__,
	    val ? "level" : "edge", irq);

	if (irq > 7) {
		if (val)
			elcr[SLAVE] |= (1 << (irq - 8));
		else
			elcr[SLAVE] &= ~(1 << (irq - 8));
	} else {
		if (val)
			elcr[MASTER] |= (1 << irq);
		else
			elcr[MASTER] &= ~(1 << irq);
	}
}

/*
 * vcpu_exit_elcr
 *
 * Handler for the ELCRx registers
 *
 * Parameters:
 *  vrp: VCPU run parameters (contains exit information) for this ELCR I/O
 *
 * Return value:
 *  Always 0xFF (PIC read/writes don't generate interrupts directly)
 */
uint8_t
vcpu_exit_elcr(struct vm_run_params *vrp)
{
	struct vm_exit *vei = vrp->vrp_exit;
	uint8_t elcr_reg = vei->vei.vei_port - ELCR0;

	if (elcr_reg > 1) {
		log_debug("%s: invalid ELCR index %d", __func__, elcr_reg);
		return (0xFF);
	}

	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		log_debug("%s: ELCR[%d] set to 0x%x", __func__, elcr_reg,
		    (uint8_t)vei->vei.vei_data);
		elcr[elcr_reg] = (uint8_t)vei->vei.vei_data;
	} else {
		set_return_data(vei, elcr[elcr_reg]);
	}

	return (0xFF);
}
