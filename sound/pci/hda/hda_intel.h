/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#ifndef __SOUND_HDA_INTEL_H
#define __SOUND_HDA_INTEL_H

#include <drm/i915_component.h>
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

	/* VGA-switcheroo setup */
	unsigned int use_vga_switcheroo:1;
	unsigned int vga_switcheroo_registered:1;
	unsigned int init_failed:1; /* delayed init failed */

	/* secondary power domain for hdmi audio under vga device */
	struct dev_pm_domain hdmi_pm_domain;

	/* i915 component interface */
	struct i915_audio_component audio_component;
};

#ifdef CONFIG_SND_HDA_I915
int hda_display_power(struct hda_intel *hda, bool enable);
void haswell_set_bclk(struct hda_intel *hda);
int hda_i915_init(struct hda_intel *hda);
int hda_i915_exit(struct hda_intel *hda);
#else
static inline int hda_display_power(struct hda_intel *hda, bool enable)
{
	return 0;
}
static inline void haswell_set_bclk(struct hda_intel *hda) { return; }
static inline int hda_i915_init(struct hda_intel *hda)
{
	return -ENODEV;
}
static inline int hda_i915_exit(struct hda_intel *hda)
{
	return 0;
}
#endif

#endif
