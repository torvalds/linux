/*
 *  Support for the w100 frame buffer.
 *
 *  Copyright (c) 2004 Richard Purdie
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

/*
 * This structure describes the machine which we are running on.
 * It is set by machine specific code and used in the probe routine
 * of drivers/video/w100fb.c
 */

struct w100fb_mach_info {
	void (*w100fb_ssp_send)(u8 adrs, u8 data);
	int comadj;
	int phadadj;
};
