/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 */
#ifndef __SOUND_HDA_INTEL_H
#define __SOUND_HDA_INTEL_H

#include "hda_controller.h"

struct hda_intel {
	struct azx chip;

	/* sync probing */
	struct completion probe_wait;
	struct delayed_work probe_work;

	/* card list (for power_save trigger) */
	struct list_head list;

	/* extra flags */
	unsigned int irq_pending_warned:1;
	unsigned int probe_continued:1;
	unsigned int runtime_pm_disabled:1;

	/* vga_switcheroo setup */
	unsigned int use_vga_switcheroo:1;
	unsigned int vga_switcheroo_registered:1;
	unsigned int init_failed:1; /* delayed init failed */
	unsigned int freed:1; /* resources already released */

	bool need_i915_power:1; /* the hda controller needs i915 power */

	int probe_retry;	/* being probe-retry */
};

struct hda_intel_stream {
	struct azx_dev azx_dev;

	/* for pending irqs */
	struct hda_intel *hda;
	struct work_struct irq_pending_work;
	bool irq_pending;
};

#define azx_dev_to_istream(azx_dev) \
	container_of(azx_dev, struct hda_intel_stream, azx_dev)

#endif
