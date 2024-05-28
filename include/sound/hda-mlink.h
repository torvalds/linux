/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022-2023 Intel Corporation. All rights reserved.
 */

struct hdac_bus;
struct hdac_ext_link;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_MLINK)

int hda_bus_ml_init(struct hdac_bus *bus);
void hda_bus_ml_free(struct hdac_bus *bus);

int hdac_bus_eml_get_count(struct hdac_bus *bus, bool alt, int elid);
void hdac_bus_eml_enable_interrupt(struct hdac_bus *bus, bool alt, int elid, bool enable);
bool hdac_bus_eml_check_interrupt(struct hdac_bus *bus, bool alt, int elid);

int hdac_bus_eml_set_syncprd_unlocked(struct hdac_bus *bus, bool alt, int elid, u32 syncprd);
int hdac_bus_eml_sdw_set_syncprd_unlocked(struct hdac_bus *bus, u32 syncprd);

int hdac_bus_eml_wait_syncpu_unlocked(struct hdac_bus *bus, bool alt, int elid);
int hdac_bus_eml_sdw_wait_syncpu_unlocked(struct hdac_bus *bus);

void hdac_bus_eml_sync_arm_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink);
void hdac_bus_eml_sdw_sync_arm_unlocked(struct hdac_bus *bus, int sublink);

int hdac_bus_eml_sync_go_unlocked(struct hdac_bus *bus, bool alt, int elid);
int hdac_bus_eml_sdw_sync_go_unlocked(struct hdac_bus *bus);

bool hdac_bus_eml_check_cmdsync_unlocked(struct hdac_bus *bus, bool alt, int elid);
bool hdac_bus_eml_sdw_check_cmdsync_unlocked(struct hdac_bus *bus);

int hdac_bus_eml_power_up(struct hdac_bus *bus, bool alt, int elid, int sublink);
int hdac_bus_eml_power_up_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink);

int hdac_bus_eml_power_down(struct hdac_bus *bus, bool alt, int elid, int sublink);
int hdac_bus_eml_power_down_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink);

int hdac_bus_eml_sdw_power_up_unlocked(struct hdac_bus *bus, int sublink);
int hdac_bus_eml_sdw_power_down_unlocked(struct hdac_bus *bus, int sublink);

int hdac_bus_eml_sdw_get_lsdiid_unlocked(struct hdac_bus *bus, int sublink, u16 *lsdiid);
int hdac_bus_eml_sdw_set_lsdiid(struct hdac_bus *bus, int sublink, int dev_num);

int hdac_bus_eml_sdw_map_stream_ch(struct hdac_bus *bus, int sublink, int y,
				   int channel_mask, int stream_id, int dir);

void hda_bus_ml_put_all(struct hdac_bus *bus);
void hda_bus_ml_reset_losidv(struct hdac_bus *bus);
int hda_bus_ml_resume(struct hdac_bus *bus);
int hda_bus_ml_suspend(struct hdac_bus *bus);

struct hdac_ext_link *hdac_bus_eml_ssp_get_hlink(struct hdac_bus *bus);
struct hdac_ext_link *hdac_bus_eml_dmic_get_hlink(struct hdac_bus *bus);
struct hdac_ext_link *hdac_bus_eml_sdw_get_hlink(struct hdac_bus *bus);

struct mutex *hdac_bus_eml_get_mutex(struct hdac_bus *bus, bool alt, int elid);

int hdac_bus_eml_enable_offload(struct hdac_bus *bus, bool alt, int elid, bool enable);

#else

static inline int
hda_bus_ml_init(struct hdac_bus *bus) { return 0; }

static inline void hda_bus_ml_free(struct hdac_bus *bus) { }

static inline int
hdac_bus_eml_get_count(struct hdac_bus *bus, bool alt, int elid) { return 0; }

static inline void
hdac_bus_eml_enable_interrupt(struct hdac_bus *bus, bool alt, int elid, bool enable) { }

static inline bool
hdac_bus_eml_check_interrupt(struct hdac_bus *bus, bool alt, int elid) { return false; }

static inline int
hdac_bus_eml_set_syncprd_unlocked(struct hdac_bus *bus, bool alt, int elid, u32 syncprd)
{
	return 0;
}

static inline int
hdac_bus_eml_sdw_set_syncprd_unlocked(struct hdac_bus *bus, u32 syncprd)
{
	return 0;
}

static inline int
hdac_bus_eml_wait_syncpu_unlocked(struct hdac_bus *bus, bool alt, int elid)
{
	return 0;
}

static inline int
hdac_bus_eml_sdw_wait_syncpu_unlocked(struct hdac_bus *bus) { return 0; }

static inline void
hdac_bus_eml_sync_arm_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink) { }

static inline void
hdac_bus_eml_sdw_sync_arm_unlocked(struct hdac_bus *bus, int sublink) { }

static inline int
hdac_bus_eml_sync_go_unlocked(struct hdac_bus *bus, bool alt, int elid) { return 0; }

static inline int
hdac_bus_eml_sdw_sync_go_unlocked(struct hdac_bus *bus) { return 0; }

static inline bool
hdac_bus_eml_check_cmdsync_unlocked(struct hdac_bus *bus, bool alt, int elid) { return false; }

static inline bool
hdac_bus_eml_sdw_check_cmdsync_unlocked(struct hdac_bus *bus) { return false; }

static inline int
hdac_bus_eml_power_up(struct hdac_bus *bus, bool alt, int elid, int sublink)
{
	return 0;
}

static inline int
hdac_bus_eml_power_up_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink)
{
	return 0;
}

static inline int
hdac_bus_eml_power_down(struct hdac_bus *bus, bool alt, int elid, int sublink)
{
	return 0;
}

static inline int
hdac_bus_eml_power_down_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink)
{
	return 0;
}

static inline int
hdac_bus_eml_sdw_power_up_unlocked(struct hdac_bus *bus, int sublink) { return 0; }

static inline int
hdac_bus_eml_sdw_power_down_unlocked(struct hdac_bus *bus, int sublink) { return 0; }

static inline int
hdac_bus_eml_sdw_get_lsdiid_unlocked(struct hdac_bus *bus, int sublink, u16 *lsdiid) { return 0; }

static inline int
hdac_bus_eml_sdw_set_lsdiid(struct hdac_bus *bus, int sublink, int dev_num) { return 0; }

static inline int
hdac_bus_eml_sdw_map_stream_ch(struct hdac_bus *bus, int sublink, int y,
			       int channel_mask, int stream_id, int dir)
{
	return 0;
}

static inline void hda_bus_ml_put_all(struct hdac_bus *bus) { }
static inline void hda_bus_ml_reset_losidv(struct hdac_bus *bus) { }
static inline int hda_bus_ml_resume(struct hdac_bus *bus) { return 0; }
static inline int hda_bus_ml_suspend(struct hdac_bus *bus) { return 0; }

static inline struct hdac_ext_link *
hdac_bus_eml_ssp_get_hlink(struct hdac_bus *bus) { return NULL; }

static inline struct hdac_ext_link *
hdac_bus_eml_dmic_get_hlink(struct hdac_bus *bus) { return NULL; }

static inline struct hdac_ext_link *
hdac_bus_eml_sdw_get_hlink(struct hdac_bus *bus) { return NULL; }

static inline struct mutex *
hdac_bus_eml_get_mutex(struct hdac_bus *bus, bool alt, int elid) { return NULL; }

static inline int
hdac_bus_eml_enable_offload(struct hdac_bus *bus, bool alt, int elid, bool enable)
{
	return 0;
}
#endif /* CONFIG_SND_SOC_SOF_HDA_MLINK */
