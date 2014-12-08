/*
 * oxfw.h - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "../lib.h"
#include "../fcp.h"
#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp.h"
#include "../cmp.h"

struct device_info {
	const char *driver_name;
	const char *vendor_name;
	const char *model_name;
	int (*pcm_constraints)(struct snd_pcm_runtime *runtime);
	unsigned int mixer_channels;
	u8 mute_fb_id;
	u8 volume_fb_id;
};

struct snd_oxfw {
	struct snd_card *card;
	struct fw_unit *unit;
	const struct device_info *device_info;
	struct mutex mutex;
	struct cmp_connection in_conn;
	struct amdtp_stream rx_stream;
	bool mute;
	s16 volume[6];
	s16 volume_min;
	s16 volume_max;
};

/*
 * AV/C Stream Format Information Specification 1.1 Working Draft
 * (Apr 2005, 1394TA)
 */
int avc_stream_set_format(struct fw_unit *unit, enum avc_general_plug_dir dir,
			  unsigned int pid, u8 *format, unsigned int len);
int avc_stream_get_format(struct fw_unit *unit,
			  enum avc_general_plug_dir dir, unsigned int pid,
			  u8 *buf, unsigned int *len, unsigned int eid);
static inline int
avc_stream_get_format_single(struct fw_unit *unit,
			     enum avc_general_plug_dir dir, unsigned int pid,
			     u8 *buf, unsigned int *len)
{
	return avc_stream_get_format(unit, dir, pid, buf, len, 0xff);
}
static inline int
avc_stream_get_format_list(struct fw_unit *unit,
			   enum avc_general_plug_dir dir, unsigned int pid,
			   u8 *buf, unsigned int *len,
			   unsigned int eid)
{
	return avc_stream_get_format(unit, dir, pid, buf, len, eid);
}

/*
 * AV/C Digital Interface Command Set General Specification 4.2
 * (Sep 2004, 1394TA)
 */
int avc_general_inquiry_sig_fmt(struct fw_unit *unit, unsigned int rate,
				enum avc_general_plug_dir dir,
				unsigned short pid);

int snd_oxfw_stream_init_simplex(struct snd_oxfw *oxfw);
int snd_oxfw_stream_start_simplex(struct snd_oxfw *oxfw);
void snd_oxfw_stream_stop_simplex(struct snd_oxfw *oxfw);
void snd_oxfw_stream_destroy_simplex(struct snd_oxfw *oxfw);
void snd_oxfw_stream_update_simplex(struct snd_oxfw *oxfw);

int firewave_constraints(struct snd_pcm_runtime *runtime);
int lacie_speakers_constraints(struct snd_pcm_runtime *runtime);
int snd_oxfw_create_pcm(struct snd_oxfw *oxfw);

int snd_oxfw_create_mixer(struct snd_oxfw *oxfw);
