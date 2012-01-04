#ifndef __OMAP_PANEL_N8X0_H
#define __OMAP_PANEL_N8X0_H

struct omap_dss_device;

struct panel_n8x0_data {
	int (*platform_enable)(struct omap_dss_device *dssdev);
	void (*platform_disable)(struct omap_dss_device *dssdev);
	int panel_reset;
	int ctrl_pwrdown;

	int (*set_backlight)(struct omap_dss_device *dssdev, int level);
};

#endif
