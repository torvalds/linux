/*
 * sound\soc\sun4i\hdmiaudio\sndhdmi.h
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * chenpailin <chenpailin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef SNDHDMI_H
#define SNDHDMI_H
#include <linux/drv_hdmi.h>

struct sndhdmi_platform_data {
	void (*power) (int);
	int model;
	/*
	  ALSA SOC usually puts the device in standby mode when it's not used
	  for sometime. If you unset is_powered_on_standby the driver will
	  turn off the ADC/DAC when this callback is invoked and turn it back
	  on when needed. Unfortunately this will result in a very light bump
	  (it can be audible only with good earphones). If this bothers you
	  set is_powered_on_standby, you will have slightly higher power
	  consumption. Please note that sending the L3 command for ADC is
	  enough to make the bump, so it doesn't make difference if you
	  completely take off power from the codec.
	*/
	int is_powered_on_standby;
};

#endif
