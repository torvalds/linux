/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cec-pin.h - low-level CEC pin control
 *
 * Copyright 2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef LINUX_CEC_PIN_H
#define LINUX_CEC_PIN_H

#include <linux/types.h>
#include <media/cec.h>

/**
 * struct cec_pin_ops - low-level CEC pin operations
 * @read:	read the CEC pin. Returns > 0 if high, 0 if low, or an error
 *		if negative.
 * @low:	drive the CEC pin low.
 * @high:	stop driving the CEC pin. The pull-up will drive the pin
 *		high, unless someone else is driving the pin low.
 * @enable_irq:	optional, enable the interrupt to detect pin voltage changes.
 * @disable_irq: optional, disable the interrupt.
 * @free:	optional. Free any allocated resources. Called when the
 *		adapter is deleted.
 * @status:	optional, log status information.
 * @read_hpd:	optional. Read the HPD pin. Returns > 0 if high, 0 if low or
 *		an error if negative.
 * @read_5v:	optional. Read the 5V pin. Returns > 0 if high, 0 if low or
 *		an error if negative.
 * @received:	optional. High-level CEC message callback. Allows the driver
 *		to process CEC messages.
 *
 * These operations (except for the @received op) are used by the
 * cec pin framework to manipulate the CEC pin.
 */
struct cec_pin_ops {
	int  (*read)(struct cec_adapter *adap);
	void (*low)(struct cec_adapter *adap);
	void (*high)(struct cec_adapter *adap);
	bool (*enable_irq)(struct cec_adapter *adap);
	void (*disable_irq)(struct cec_adapter *adap);
	void (*free)(struct cec_adapter *adap);
	void (*status)(struct cec_adapter *adap, struct seq_file *file);
	int  (*read_hpd)(struct cec_adapter *adap);
	int  (*read_5v)(struct cec_adapter *adap);

	/* High-level CEC message callback */
	int (*received)(struct cec_adapter *adap, struct cec_msg *msg);
};

/**
 * cec_pin_changed() - update pin state from interrupt
 *
 * @adap:	pointer to the cec adapter
 * @value:	when true the pin is high, otherwise it is low
 *
 * If changes of the CEC voltage are detected via an interrupt, then
 * cec_pin_changed is called from the interrupt with the new value.
 */
void cec_pin_changed(struct cec_adapter *adap, bool value);

/**
 * cec_pin_allocate_adapter() - allocate a pin-based cec adapter
 *
 * @pin_ops:	low-level pin operations
 * @priv:	will be stored in adap->priv and can be used by the adapter ops.
 *		Use cec_get_drvdata(adap) to get the priv pointer.
 * @name:	the name of the CEC adapter. Note: this name will be copied.
 * @caps:	capabilities of the CEC adapter. This will be ORed with
 *		CEC_CAP_MONITOR_ALL and CEC_CAP_MONITOR_PIN.
 *
 * Allocate a cec adapter using the cec pin framework.
 *
 * Return: a pointer to the cec adapter or an error pointer
 */
struct cec_adapter *cec_pin_allocate_adapter(const struct cec_pin_ops *pin_ops,
					void *priv, const char *name, u32 caps);

#endif
