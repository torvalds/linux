// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PC Speaker beeper driver for Linux
 *
 *  Copyright (c) 2002 Vojtech Pavlik
 *  Copyright (c) 1992 Orest Zborowski
 */


#include <linux/init.h>
#include <linux/input.h>
#include <linux/io.h>
#include "pcsp.h"
#include "pcsp_input.h"

static void pcspkr_do_sound(unsigned int count)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&i8253_lock, flags);

	if (count) {
		/* set command for counter 2, 2 byte write */
		outb_p(0xB6, 0x43);
		/* select desired HZ */
		outb_p(count & 0xff, 0x42);
		outb((count >> 8) & 0xff, 0x42);
		/* enable counter 2 */
		outb_p(inb_p(0x61) | 3, 0x61);
	} else {
		/* disable counter 2 */
		outb(inb_p(0x61) & 0xFC, 0x61);
	}

	raw_spin_unlock_irqrestore(&i8253_lock, flags);
}

void pcspkr_stop_sound(void)
{
	pcspkr_do_sound(0);
}

static int pcspkr_input_event(struct input_dev *dev, unsigned int type,
			      unsigned int code, int value)
{
	unsigned int count = 0;

	if (atomic_read(&pcsp_chip.timer_active) || !pcsp_chip.pcspkr)
		return 0;

	switch (type) {
	case EV_SND:
		switch (code) {
		case SND_BELL:
			if (value)
				value = 1000;
			break;
		case SND_TONE:
			break;
		default:
			return -1;
		}
		break;

	default:
		return -1;
	}

	if (value > 20 && value < 32767)
		count = PIT_TICK_RATE / value;

	pcspkr_do_sound(count);

	return 0;
}

int pcspkr_input_init(struct input_dev **rdev, struct device *dev)
{
	int err;

	struct input_dev *input_dev = devm_input_allocate_device(dev);
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = "PC Speaker";
	input_dev->phys = "isa0061/input0";
	input_dev->id.bustype = BUS_ISA;
	input_dev->id.vendor = 0x001f;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = dev;

	input_dev->evbit[0] = BIT(EV_SND);
	input_dev->sndbit[0] = BIT(SND_BELL) | BIT(SND_TONE);
	input_dev->event = pcspkr_input_event;

	err = input_register_device(input_dev);
	if (err)
		return err;

	*rdev = input_dev;
	return 0;
}
