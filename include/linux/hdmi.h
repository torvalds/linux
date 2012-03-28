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
#include <linux/wakelock.h>

extern int debug_en;

#define hdmi_dbg(dev, format, arg...)		\
do{\
	if(debug_en == 1) \
		dev_printk(KERN_INFO , dev , format , ## arg);\
}while(0)



typedef int 		BOOL;

#define TRUE		1
#define FALSE 		0
#define HDMI_DISABLE   0
#define HDMI_ENABLE    1

#define MIN_SCALE		80
/* mouse event */
#define MOUSE_NONE			0x00
#define MOUSE_LEFT_PRESS	0x01
#define MOUSE_RIGHT_PRESS	0x02
#define MOUSE_MIDDLE_PRESS	0x04
#define HDMI_MOUSE_EVENT	MOUSE_NONE	
/* mode */
#define DISP_ON_LCD				0
#define DISP_ON_HDMI			1
#define DISP_ON_LCD_AND_HDMI	2
/* dual display */
#ifdef CONFIG_HDMI_DUAL_DISP
#define DUAL_DISP_CAP		HDMI_ENABLE 
#define HDMI_DEFAULT_MODE	DISP_ON_LCD_AND_HDMI
#else
#define DUAL_DISP_CAP		HDMI_DISABLE 
#define HDMI_DEFAULT_MODE	DISP_ON_HDMI
#endif
/* resolution */
#define HDMI_1920x1080p_50Hz	0
#define HDMI_1920x1080p_60Hz	1
#define HDMI_1280x720p_50Hz 	2
#define HDMI_1280x720p_60Hz		3
#define HDMI_720x576p_50Hz_4x3	4
#define HDMI_720x576p_50Hz_16x9	5
#define HDMI_720x480p_60Hz_4x3	6
#define HDMI_720x480p_60Hz_16x9	7

/* HDMI default resolution */
#define HDMI_DEFAULT_RESOLUTION  HDMI_1920x1080p_50Hz
/* I2S Fs */
#define HDMI_I2S_Fs_44100 0
#define HDMI_I2S_Fs_48000 2
/* I2S default sample rate */
#define HDMI_I2S_DEFAULT_Fs HDMI_I2S_Fs_44100


#define HDMI_MAX_ID		32
struct hdmi;
struct hdmi_ops{
	int (*set_param)(struct hdmi *);
	int (*hdmi_precent)(struct hdmi *);
	int (*insert)(struct hdmi *);
	int (*remove)(struct hdmi *);
	int (*init)(struct hdmi*);
};
struct hdmi {
	int id;
	int wait;
	BOOL display_on;
	BOOL plug;
	BOOL hdcp_on;
	BOOL param_conf;

	u8 resolution;
	u8 scale;
	u8 scale_set;
	u8 audio_fs;
	int mode;
	int dual_disp;
	struct timer_list timer;
	struct mutex lock;
	struct device *dev;
	struct delayed_work work;
	struct completion	complete;
	const struct hdmi_ops *ops;

	unsigned long		priv[0] ____cacheline_aligned;
};
extern int hdmi_is_insert(void);
extern void *hdmi_priv(struct hdmi *hdmi);
extern struct hdmi *hdmi_register(int extra, struct device *parent);
extern void hdmi_unregister(struct hdmi *hdmi);
extern void hdmi_changed(struct hdmi *hdmi, int msec);

extern int hdmi_switch_fb(struct hdmi *hdmi, int type);
extern void hdmi_suspend(struct hdmi *hdmi);
extern void hdmi_resume(struct hdmi *hdmi);
extern struct hdmi *get_hdmi_struct(int nr);

extern void hdmi_set_spk(int on);
extern void hdmi_set_backlight(int on);
extern int hdmi_get_scale(void);
extern int hdmi_set_scale(int event, char *data, int len);
extern int fb_get_video_mode(void);
extern int display_on_hdmi(void);
extern int hdmi_get_data(void);
extern int hdmi_set_data(int data);
#endif
