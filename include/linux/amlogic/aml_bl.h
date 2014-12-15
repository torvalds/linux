
#ifndef __INCLUDE_AML_BL_H
#define __INCLUDE_AML_BL_H

struct aml_bl_platform_data {
	void (*bl_init)(void);
	void (*power_on_bl)(void);
	void (*power_off_bl)(void);
	unsigned (*get_bl_level)(void);
	void (*set_bl_level)(unsigned);
	int max_brightness;
	int dft_brightness;
};

#endif

