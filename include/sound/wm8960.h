/*
 * wm8960.h  --  WM8960 Soc Audio driver platform data
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8960_PDATA_H
#define _WM8960_PDATA_H

#define WM8960_DRES_400R 0
#define WM8960_DRES_200R 1
#define WM8960_DRES_600R 2
#define WM8960_DRES_150R 3
#define WM8960_DRES_MAX  3

struct wm8960_data {
	bool capless;  /* Headphone outputs configured in capless mode */

	bool shared_lrclk;  /* DAC and ADC LRCLKs are wired together */
    /* Disable Headphone detect through codec*/
	bool dis_hp_det; 
	/* Headphone detect polarity, only be useful when hp_jack_det=1
	* 0----High = Speaker; 1---High = Headphone;
	*/
	bool hp_det_pol;
	/* Discharge resistance for headphone outputs */
	int dres;
	/* return value:
	* 0::no headphone plugged;
	* 1::three ports headphone plugged with no mic;
	* 2::four ports headphone plugged with mic
	*/
    int (*hp_detect)(void);
    void (*device_init)(void);
    void (*device_uninit)(void);
};

#endif
