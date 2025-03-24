/******************************************************************************
 * acpi.h
 * acpi file for domain 0 kernel
 *
 * Copyright (c) 2011 Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
 * Copyright (c) 2011 Yu Ke <ke.yu@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _XEN_ACPI_H
#define _XEN_ACPI_H

#include <linux/types.h>

typedef int (*get_gsi_from_sbdf_t)(u32 sbdf);

#ifdef CONFIG_XEN_DOM0
#include <asm/xen/hypervisor.h>
#include <xen/xen.h>
#include <linux/acpi.h>

int xen_acpi_notify_hypervisor_sleep(u8 sleep_state,
				     u32 pm1a_cnt, u32 pm1b_cnd);
int xen_acpi_notify_hypervisor_extended_sleep(u8 sleep_state,
				     u32 val_a, u32 val_b);

static inline int xen_acpi_suspend_lowlevel(void)
{
	/*
	* Xen will save and restore CPU context, so
	* we can skip that and just go straight to
	* the suspend.
	*/
	acpi_enter_sleep_state(ACPI_STATE_S3);
	return 0;
}

static inline void xen_acpi_sleep_register(void)
{
	if (xen_initial_domain()) {
		acpi_os_set_prepare_sleep(
			&xen_acpi_notify_hypervisor_sleep);
		acpi_os_set_prepare_extended_sleep(
			&xen_acpi_notify_hypervisor_extended_sleep);

		acpi_suspend_lowlevel = xen_acpi_suspend_lowlevel;
	}
}
int xen_pvh_setup_gsi(int gsi, int trigger, int polarity);
int xen_acpi_get_gsi_info(struct pci_dev *dev,
						  int *gsi_out,
						  int *trigger_out,
						  int *polarity_out);
void xen_acpi_register_get_gsi_func(get_gsi_from_sbdf_t func);
int xen_acpi_get_gsi_from_sbdf(u32 sbdf);
#else
static inline void xen_acpi_sleep_register(void)
{
}

static inline int xen_pvh_setup_gsi(int gsi, int trigger, int polarity)
{
	return -1;
}

static inline int xen_acpi_get_gsi_info(struct pci_dev *dev,
						  int *gsi_out,
						  int *trigger_out,
						  int *polarity_out)
{
	return -1;
}

static inline void xen_acpi_register_get_gsi_func(get_gsi_from_sbdf_t func)
{
}

static inline int xen_acpi_get_gsi_from_sbdf(u32 sbdf)
{
	return -1;
}
#endif

#endif	/* _XEN_ACPI_H */
