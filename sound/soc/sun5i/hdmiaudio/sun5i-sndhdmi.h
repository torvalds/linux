/*
 * sound\soc\sun5i\hdmiaudio\sun5i-sndhdmi.h
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

#ifndef SUN5I_SNDHDMI_H_
#define SUN5I_SNDHDMI_H_

struct sun5i_sndhdmi_platform_data {
	int hdmiaudio_bclk;
	int hdmiaudio_ws;
	int hdmiaudio_data;
	void (*power)(int);
	int model;
}
#endif