/*
    v4l2 common internal API header

    This header contains internal shared ioctl definitions for use by the
    internal low-level v4l2 drivers.
    Each ioctl begins with VIDIOC_INT_ to clearly mark that it is an internal
    define,

    Copyright (C) 2005  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef V4L2_COMMON_H_
#define V4L2_COMMON_H_

#include <media/v4l2-dev.h>

/* Common printk constucts for v4l-i2c drivers. These macros create a unique
   prefix consisting of the driver name, the adapter number and the i2c
   address. */
#define v4l_printk(level, name, adapter, addr, fmt, arg...) \
	printk(level "%s %d-%04x: " fmt, name, i2c_adapter_id(adapter), addr , ## arg)

#define v4l_client_printk(level, client, fmt, arg...)			    \
	v4l_printk(level, (client)->driver->driver.name, (client)->adapter, \
		   (client)->addr, fmt , ## arg)

#define v4l_err(client, fmt, arg...) \
	v4l_client_printk(KERN_ERR, client, fmt , ## arg)

#define v4l_warn(client, fmt, arg...) \
	v4l_client_printk(KERN_WARNING, client, fmt , ## arg)

#define v4l_info(client, fmt, arg...) \
	v4l_client_printk(KERN_INFO, client, fmt , ## arg)

/* These three macros assume that the debug level is set with a module
   parameter called 'debug'. */
#define v4l_dbg(level, debug, client, fmt, arg...)			     \
	do { 								     \
		if (debug >= (level))					     \
			v4l_client_printk(KERN_DEBUG, client, fmt , ## arg); \
	} while (0)

/* ------------------------------------------------------------------------- */

/* These printk constructs can be used with v4l2_device and v4l2_subdev */
#define v4l2_printk(level, dev, fmt, arg...) \
	printk(level "%s: " fmt, (dev)->name , ## arg)

#define v4l2_err(dev, fmt, arg...) \
	v4l2_printk(KERN_ERR, dev, fmt , ## arg)

#define v4l2_warn(dev, fmt, arg...) \
	v4l2_printk(KERN_WARNING, dev, fmt , ## arg)

#define v4l2_info(dev, fmt, arg...) \
	v4l2_printk(KERN_INFO, dev, fmt , ## arg)

/* These three macros assume that the debug level is set with a module
   parameter called 'debug'. */
#define v4l2_dbg(level, debug, dev, fmt, arg...)			\
	do { 								\
		if (debug >= (level))					\
			v4l2_printk(KERN_DEBUG, dev, fmt , ## arg); 	\
	} while (0)

/* ------------------------------------------------------------------------- */

/* Priority helper functions */

struct v4l2_prio_state {
	atomic_t prios[4];
};
void v4l2_prio_init(struct v4l2_prio_state *global);
int v4l2_prio_change(struct v4l2_prio_state *global, enum v4l2_priority *local,
		     enum v4l2_priority new);
void v4l2_prio_open(struct v4l2_prio_state *global, enum v4l2_priority *local);
void v4l2_prio_close(struct v4l2_prio_state *global, enum v4l2_priority local);
enum v4l2_priority v4l2_prio_max(struct v4l2_prio_state *global);
int v4l2_prio_check(struct v4l2_prio_state *global, enum v4l2_priority local);

/* ------------------------------------------------------------------------- */

/* Control helper functions */

int v4l2_ctrl_check(struct v4l2_ext_control *ctrl, struct v4l2_queryctrl *qctrl,
		const char * const *menu_items);
const char *v4l2_ctrl_get_name(u32 id);
const char * const *v4l2_ctrl_get_menu(u32 id);
int v4l2_ctrl_query_fill(struct v4l2_queryctrl *qctrl, s32 min, s32 max, s32 step, s32 def);
int v4l2_ctrl_query_menu(struct v4l2_querymenu *qmenu,
		struct v4l2_queryctrl *qctrl, const char * const *menu_items);
#define V4L2_CTRL_MENU_IDS_END (0xffffffff)
int v4l2_ctrl_query_menu_valid_items(struct v4l2_querymenu *qmenu, const u32 *ids);

/* Note: ctrl_classes points to an array of u32 pointers. Each u32 array is a
   0-terminated array of control IDs. Each array must be sorted low to high
   and belong to the same control class. The array of u32 pointers must also
   be sorted, from low class IDs to high class IDs. */
u32 v4l2_ctrl_next(const u32 * const *ctrl_classes, u32 id);

/* ------------------------------------------------------------------------- */

/* Register/chip ident helper function */

struct i2c_client; /* forward reference */
int v4l2_chip_match_i2c_client(struct i2c_client *c, const struct v4l2_dbg_match *match);
int v4l2_chip_ident_i2c_client(struct i2c_client *c, struct v4l2_dbg_chip_ident *chip,
		u32 ident, u32 revision);
int v4l2_chip_match_host(const struct v4l2_dbg_match *match);

/* ------------------------------------------------------------------------- */

/* I2C Helper functions */

struct i2c_driver;
struct i2c_adapter;
struct i2c_client;
struct i2c_device_id;
struct v4l2_device;
struct v4l2_subdev;
struct v4l2_subdev_ops;


/* Load an i2c module and return an initialized v4l2_subdev struct.
   The client_type argument is the name of the chip that's on the adapter. */
struct v4l2_subdev *v4l2_i2c_new_subdev_cfg(struct v4l2_device *v4l2_dev,
		struct i2c_adapter *adapter, const char *client_type,
		int irq, void *platform_data,
		u8 addr, const unsigned short *probe_addrs);

/* Load an i2c module and return an initialized v4l2_subdev struct.
   The client_type argument is the name of the chip that's on the adapter. */
static inline struct v4l2_subdev *v4l2_i2c_new_subdev(struct v4l2_device *v4l2_dev,
		struct i2c_adapter *adapter, const char *client_type,
		u8 addr, const unsigned short *probe_addrs)
{
	return v4l2_i2c_new_subdev_cfg(v4l2_dev, adapter, client_type, 0, NULL,
				       addr, probe_addrs);
}

struct i2c_board_info;

struct v4l2_subdev *v4l2_i2c_new_subdev_board(struct v4l2_device *v4l2_dev,
		struct i2c_adapter *adapter, struct i2c_board_info *info,
		const unsigned short *probe_addrs);

/* Initialize an v4l2_subdev with data from an i2c_client struct */
void v4l2_i2c_subdev_init(struct v4l2_subdev *sd, struct i2c_client *client,
		const struct v4l2_subdev_ops *ops);
/* Return i2c client address of v4l2_subdev. */
unsigned short v4l2_i2c_subdev_addr(struct v4l2_subdev *sd);

enum v4l2_i2c_tuner_type {
	ADDRS_RADIO,	/* Radio tuner addresses */
	ADDRS_DEMOD,	/* Demod tuner addresses */
	ADDRS_TV,	/* TV tuner addresses */
	/* TV tuner addresses if demod is present, this excludes
	   addresses used by the demodulator from the list of
	   candidates. */
	ADDRS_TV_WITH_DEMOD,
};
/* Return a list of I2C tuner addresses to probe. Use only if the tuner
   addresses are unknown. */
const unsigned short *v4l2_i2c_tuner_addrs(enum v4l2_i2c_tuner_type type);

/* ------------------------------------------------------------------------- */

/* SPI Helper functions */
#if defined(CONFIG_SPI)

#include <linux/spi/spi.h>

struct spi_device;

/* Load an spi module and return an initialized v4l2_subdev struct.
   The client_type argument is the name of the chip that's on the adapter. */
struct v4l2_subdev *v4l2_spi_new_subdev(struct v4l2_device *v4l2_dev,
		struct spi_master *master, struct spi_board_info *info);

/* Initialize an v4l2_subdev with data from an spi_device struct */
void v4l2_spi_subdev_init(struct v4l2_subdev *sd, struct spi_device *spi,
		const struct v4l2_subdev_ops *ops);
#endif

/* ------------------------------------------------------------------------- */

/* Note: these remaining ioctls/structs should be removed as well, but they are
   still used in tuner-simple.c (TUNER_SET_CONFIG), cx18/ivtv (RESET) and
   v4l2-int-device.h (v4l2_routing). To remove these ioctls some more cleanup
   is needed in those modules. */

/* s_config */
struct v4l2_priv_tun_config {
	int tuner;
	void *priv;
};
#define TUNER_SET_CONFIG           _IOW('d', 92, struct v4l2_priv_tun_config)

#define VIDIOC_INT_RESET            	_IOW ('d', 102, u32)

struct v4l2_routing {
	u32 input;
	u32 output;
};

/* ------------------------------------------------------------------------- */

/* Miscellaneous helper functions */

void v4l_bound_align_image(unsigned int *w, unsigned int wmin,
			   unsigned int wmax, unsigned int walign,
			   unsigned int *h, unsigned int hmin,
			   unsigned int hmax, unsigned int halign,
			   unsigned int salign);
int v4l_fill_dv_preset_info(u32 preset, struct v4l2_dv_enum_preset *info);

struct v4l2_discrete_probe {
	const struct v4l2_frmsize_discrete	*sizes;
	int					num_sizes;
};

const struct v4l2_frmsize_discrete *v4l2_find_nearest_format(
		const struct v4l2_discrete_probe *probe,
		s32 width, s32 height);

#endif /* V4L2_COMMON_H_ */
