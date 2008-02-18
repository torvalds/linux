/* include/linux/sm501.h
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	Vincent Sanders <vince@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

extern int sm501_unit_power(struct device *dev,
			    unsigned int unit, unsigned int to);

extern unsigned long sm501_set_clock(struct device *dev,
				     int clksrc, unsigned long freq);

extern unsigned long sm501_find_clock(int clksrc, unsigned long req_freq);

/* sm501_misc_control
 *
 * Modify the SM501's MISC_CONTROL register
*/

extern int sm501_misc_control(struct device *dev,
			      unsigned long set, unsigned long clear);

/* sm501_modify_reg
 *
 * Modify a register in the SM501 which may be shared with other
 * drivers.
*/

extern unsigned long sm501_modify_reg(struct device *dev,
				      unsigned long reg,
				      unsigned long set,
				      unsigned long clear);

/* sm501_gpio_set
 *
 * set the state of the given GPIO line
*/

extern void sm501_gpio_set(struct device *dev,
			   unsigned long gpio,
			   unsigned int to,
			   unsigned int dir);

/* sm501_gpio_get
 *
 * get the state of the given GPIO line
*/

extern unsigned long sm501_gpio_get(struct device *dev,
				    unsigned long gpio);


/* Platform data definitions */

#define SM501FB_FLAG_USE_INIT_MODE	(1<<0)
#define SM501FB_FLAG_DISABLE_AT_EXIT	(1<<1)
#define SM501FB_FLAG_USE_HWCURSOR	(1<<2)
#define SM501FB_FLAG_USE_HWACCEL	(1<<3)
#define SM501FB_FLAG_PANEL_USE_FPEN	(1<<4)
#define SM501FB_FLAG_PANEL_USE_VBIASEN	(1<<5)

struct sm501_platdata_fbsub {
	struct fb_videomode	*def_mode;
	unsigned int		 def_bpp;
	unsigned long		 max_mem;
	unsigned int		 flags;
};

enum sm501_fb_routing {
	SM501_FB_OWN		= 0,	/* CRT=>CRT, Panel=>Panel */
	SM501_FB_CRT_PANEL	= 1,	/* Panel=>CRT, Panel=>Panel */
};

/* sm501_platdata_fb flag field bit definitions */

#define SM501_FBPD_SWAP_FB_ENDIAN	(1<<0)	/* need to endian swap */

/* sm501_platdata_fb
 *
 * configuration data for the framebuffer driver
*/

struct sm501_platdata_fb {
	enum sm501_fb_routing		 fb_route;
	unsigned int			 flags;
	struct sm501_platdata_fbsub	*fb_crt;
	struct sm501_platdata_fbsub	*fb_pnl;
};

/* gpio i2c */

struct sm501_platdata_gpio_i2c {
	unsigned int		pin_sda;
	unsigned int		pin_scl;
};

/* sm501_initdata
 *
 * use for initialising values that may not have been setup
 * before the driver is loaded.
*/

struct sm501_reg_init {
	unsigned long		set;
	unsigned long		mask;
};

#define SM501_USE_USB_HOST	(1<<0)
#define SM501_USE_USB_SLAVE	(1<<1)
#define SM501_USE_SSP0		(1<<2)
#define SM501_USE_SSP1		(1<<3)
#define SM501_USE_UART0		(1<<4)
#define SM501_USE_UART1		(1<<5)
#define SM501_USE_FBACCEL	(1<<6)
#define SM501_USE_AC97		(1<<7)
#define SM501_USE_I2S		(1<<8)

#define SM501_USE_ALL		(0xffffffff)

struct sm501_initdata {
	struct sm501_reg_init	gpio_low;
	struct sm501_reg_init	gpio_high;
	struct sm501_reg_init	misc_timing;
	struct sm501_reg_init	misc_control;

	unsigned long		devices;
	unsigned long		mclk;		/* non-zero to modify */
	unsigned long		m1xclk;		/* non-zero to modify */
};

/* sm501_init_gpio
 *
 * default gpio settings
*/

struct sm501_init_gpio {
	struct sm501_reg_init	gpio_data_low;
	struct sm501_reg_init	gpio_data_high;
	struct sm501_reg_init	gpio_ddr_low;
	struct sm501_reg_init	gpio_ddr_high;
};

/* sm501_platdata
 *
 * This is passed with the platform device to allow the board
 * to control the behaviour of the SM501 driver(s) which attach
 * to the device.
 *
*/

struct sm501_platdata {
	struct sm501_initdata		*init;
	struct sm501_init_gpio		*init_gpiop;
	struct sm501_platdata_fb	*fb;

	struct sm501_platdata_gpio_i2c	*gpio_i2c;
	unsigned int			 gpio_i2c_nr;
};
