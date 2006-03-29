/*
 * ST M48T86 / Dallas DS12887 RTC driver
 * Copyright (c) 2006 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

struct m48t86_ops
{
	void (*writeb)(unsigned char value, unsigned long addr);
	unsigned char (*readb)(unsigned long addr);
};
