/*
 * arch/arm/mach-meson6/include/mach/lcd_aml.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _MACH_AML_LCD_H
#define _MACH_AML_LCD_H

#include <plat/platform_data.h>
//#include <linux/amlogic/vout/lcdoutc.h>

struct aml_lcd_platform {
	plat_data_public_t public;
	Lcd_Config_t *lcd_conf;
	/* local settings */
	int lcd_status;
};

#endif // _MACH_AML_LCD_H
