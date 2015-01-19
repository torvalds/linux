/*
 * VDAC SWITCH definitions
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __AML_VDAC_SWITCH_H__
#define __AML_VDAC_SWITCH_H__

#include <linux/types.h>

enum aml_vdac_switch_type {
	VOUT_CVBS,
	VOUT_COMPONENT,
	VOUT_VGA,
	VOUT_MAX
};

#ifdef CONFIG_USE_OF
struct aml_vdac_hw_ctrl {
    unsigned int pin1;
    unsigned int val1;
    unsigned int pin2;
    unsigned int val2;
};

struct aml_vdac_hw_switch {
    struct aml_vdac_hw_ctrl cvbs;
    struct aml_vdac_hw_ctrl ypbr;
    struct aml_vdac_hw_ctrl vga;
};
#endif

struct aml_vdac_switch_platform_data {
	void (*vdac_switch_change_type_func)(unsigned type);
};

#endif
