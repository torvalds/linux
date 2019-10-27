/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 */
#ifndef __SOUND_HDA_INTEL_H
#define __SOUND_HDA_INTEL_H

#include "hda_controller.h"

struct hda_intel {
	struct azx chip;

	/* for pending irqs */
	struct work_struct irq_pending_work;

	/* sync probing */
	struct completion probe_wait;
	struct work_struct probe_work;

	/* card list (for power_save trigger) */
	struct list_head list;

	/* extra flags */
	unsigned int irq_pending_warned:1;
	unsigned int probe_continued:1;

	/* vga_switcheroo setup */
	unsigned int use_vga_switcheroo:1;
	unsigned int vga_switcheroo_registered:1;
	unsigned int init_failed:1; /* delayed init failed */

	bool need_i915_power:1; /* the hda controller needs i915 power */
};

#endif
