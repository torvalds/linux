/*
 * Copyright © 2008-2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef _INTEL_OPREGION_H_
#define _INTEL_OPREGION_H_

#include <linux/pci.h>
#include <linux/types.h>

struct intel_connector;
struct intel_display;
struct intel_encoder;

#ifdef CONFIG_ACPI

int intel_opregion_setup(struct intel_display *display);
void intel_opregion_cleanup(struct intel_display *display);

void intel_opregion_register(struct intel_display *display);
void intel_opregion_unregister(struct intel_display *display);

void intel_opregion_resume(struct intel_display *display);
void intel_opregion_suspend(struct intel_display *display,
			    pci_power_t state);

bool intel_opregion_asle_present(struct intel_display *display);
void intel_opregion_asle_intr(struct intel_display *display);
int intel_opregion_notify_encoder(struct intel_encoder *encoder,
				  bool enable);
int intel_opregion_notify_adapter(struct intel_display *display,
				  pci_power_t state);
int intel_opregion_get_panel_type(struct intel_display *display);
const struct drm_edid *intel_opregion_get_edid(struct intel_connector *connector);

bool intel_opregion_vbt_present(struct intel_display *display);
const void *intel_opregion_get_vbt(struct intel_display *display, size_t *size);

bool intel_opregion_headless_sku(struct intel_display *display);

void intel_opregion_debugfs_register(struct intel_display *display);

#else /* CONFIG_ACPI*/

static inline int intel_opregion_setup(struct intel_display *display)
{
	return 0;
}

static inline void intel_opregion_cleanup(struct intel_display *display)
{
}

static inline void intel_opregion_register(struct intel_display *display)
{
}

static inline void intel_opregion_unregister(struct intel_display *display)
{
}

static inline void intel_opregion_resume(struct intel_display *display)
{
}

static inline void intel_opregion_suspend(struct intel_display *display,
					  pci_power_t state)
{
}

static inline bool intel_opregion_asle_present(struct intel_display *display)
{
	return false;
}

static inline void intel_opregion_asle_intr(struct intel_display *display)
{
}

static inline int
intel_opregion_notify_encoder(struct intel_encoder *encoder, bool enable)
{
	return 0;
}

static inline int
intel_opregion_notify_adapter(struct intel_display *display, pci_power_t state)
{
	return 0;
}

static inline int intel_opregion_get_panel_type(struct intel_display *display)
{
	return -ENODEV;
}

static inline const struct drm_edid *
intel_opregion_get_edid(struct intel_connector *connector)
{
	return NULL;
}

static inline bool intel_opregion_vbt_present(struct intel_display *display)
{
	return false;
}

static inline const void *
intel_opregion_get_vbt(struct intel_display *display, size_t *size)
{
	return NULL;
}

static inline bool intel_opregion_headless_sku(struct intel_display *display)
{
	return false;
}

static inline void intel_opregion_debugfs_register(struct intel_display *display)
{
}

#endif /* CONFIG_ACPI */

#endif
