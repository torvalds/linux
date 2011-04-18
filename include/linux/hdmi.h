/* include/linux/hdmi.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifndef __LINUX_HDMI_CORE_H
#define __LINUX_HDMI_CORE_H

#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/completion.h>


#ifdef CONFIG_HDMI_DEBUG
#define hdmi_dbg(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
#else
#define hdmi_dbg(dev, format, arg...)	
#endif



typedef int 		BOOL;

#define TRUE		1
#define FALSE 		0
/* resolution */
#define HDMI_1280x720p_50Hz 	0
#define HDMI_1280x720p_60Hz		1
#define HDMI_720x576p_50Hz		2
#define HDMI_1920x1080p_50Hz	3
/* HDMI default resolution */
#define HDMI_DEFAULT_RESOLUTION HDMI_1280x720p_50Hz
/* I2S Fs */
#define HDMI_I2S_Fs_44100 0
#define HDMI_I2S_Fs_48000 2
/* I2S default sample rate */
#define HDMI_I2S_DEFAULT_Fs HDMI_I2S_Fs_44100


#define HDMI_MAX_ID		32


struct hdmi {
	struct device *dev;
	struct work_struct changed_work;
	int id;
	int wait;
	BOOL display_on;
	BOOL plug;
	BOOL auto_switch;
	BOOL hdcp_on;
	BOOL param_conf;

	u8 resolution;
	u8 audio_fs;

	void *priv;

	struct completion	complete;

	int (*hdmi_display_on)(struct hdmi *);
	int (*hdmi_display_off)(struct hdmi *);
	int (*hdmi_set_param)(struct hdmi *);
	int (*hdmi_core_init)(struct hdmi *);
};

extern void *hdmi_get_privdata(struct hdmi *hdmi);
extern void hdmi_set_privdata(struct hdmi *hdmi, void *data);
extern int hdmi_register(struct device *parent, struct hdmi *hdmi);
extern void hdmi_unregister(struct hdmi *hdmi);
extern void hdmi_changed(struct hdmi *hdmi, int plug);

extern int hdmi_codec_set_audio_fs(unsigned char audio_fs);
extern int hdmi_fb_set_resolution(unsigned char resolution);

extern int hdmi_switch_fb(struct hdmi *hdmi, int type);
extern int hdmi_resolution_changed(struct hdmi *hdmi, int xres, int yres, int video_on);

extern struct hdmi *get_hdmi_struct(int nr);

extern int hdmi_get_default_resolution(void *screen);

#endif
