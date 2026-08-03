/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022-2023 Intel Corporation
 */

struct hdac_bus;
struct hdac_ext_link;

/**
 * enum hda_bus_ml_link_type - mlink link type, used by SOF link DMA
 *	allocator constraints (see struct sof_intel_hda_dev).
 *
 * @HDA_BUS_ML_LINK_HDA:  non-alt link, i.e. HDA codec or iDisp
 * @HDA_BUS_ML_LINK_SDW:  alt link, SoundWire
 * @HDA_BUS_ML_LINK_UAOL: alt link, USB Audio Offload
 * @HDA_BUS_ML_LINK_OTHER: alt link, SSP or DMIC
 */
enum hda_bus_ml_link_type {
	HDA_BUS_ML_LINK_HDA,
	HDA_BUS_ML_LINK_SDW,
	HDA_BUS_ML_LINK_UAOL,
	HDA_BUS_ML_LINK_OTHER,
};

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_MLINK)

int hda_bus_ml_init(struct hdac_bus *bus);
void hda_bus_ml_free(struct hdac_bus *bus);

int hdac_bus_eml_get_count(struct hdac_bus *bus, bool alt, int elid);
void hdac_bus_eml_enable_interrupt_unlocked(struct hdac_bus *bus, bool alt, int elid, bool enable);
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

void hda_bus_ml_reset_losidv(struct hdac_bus *bus);
int hda_bus_ml_resume(struct hdac_bus *bus);
int hda_bus_ml_suspend(struct hdac_bus *bus);

enum hda_bus_ml_link_type hda_bus_ml_link_get_type(struct hdac_ext_link *hlink);

struct hdac_ext_link *hdac_bus_eml_ssp_get_hlink(struct hdac_bus *bus);
struct hdac_ext_link *hdac_bus_eml_dmic_get_hlink(struct hdac_bus *bus);
struct hdac_ext_link *hdac_bus_eml_sdw_get_hlink(struct hdac_bus *bus);

struct mutex *hdac_bus_eml_get_mutex(struct hdac_bus *bus, bool alt, int elid);

void hdac_bus_eml_enable_offload(struct hdac_bus *bus, bool alt, int elid, bool enable);

/* microphone privacy specific function supported by ACE3+ architecture */
void hdac_bus_eml_set_mic_privacy_mask(struct hdac_bus *bus, bool alt, int elid,
				       unsigned long mask);
bool hdac_bus_eml_is_mic_privacy_changed(struct hdac_bus *bus, bool alt, int elid);
bool hdac_bus_eml_get_mic_privacy_state(struct hdac_bus *bus, bool alt, int elid);

#else

static inline int
hda_bus_ml_init(struct hdac_bus *bus) { return 0; }

static inline void hda_bus_ml_free(struct hdac_bus *bus) { }

static inline int
hdac_bus_eml_get_count(struct hdac_bus *bus, bool alt, int elid) { return 0; }

static inline void
hdac_bus_eml_enable_interrupt_unlocked(struct hdac_bus *bus, bool alt, int elid, bool enable) { }

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

static inline void hda_bus_ml_reset_losidv(struct hdac_bus *bus) { }
static inline int hda_bus_ml_resume(struct hdac_bus *bus) { return 0; }
static inline int hda_bus_ml_suspend(struct hdac_bus *bus) { return 0; }

static inline enum hda_bus_ml_link_type
hda_bus_ml_link_get_type(struct hdac_ext_link *hlink) { return HDA_BUS_ML_LINK_HDA; }

static inline struct hdac_ext_link *
hdac_bus_eml_ssp_get_hlink(struct hdac_bus *bus) { return NULL; }

static inline struct hdac_ext_link *
hdac_bus_eml_dmic_get_hlink(struct hdac_bus *bus) { return NULL; }

static inline struct hdac_ext_link *
hdac_bus_eml_sdw_get_hlink(struct hdac_bus *bus) { return NULL; }

static inline struct mutex *
hdac_bus_eml_get_mutex(struct hdac_bus *bus, bool alt, int elid) { return NULL; }

static inline void
hdac_bus_eml_enable_offload(struct hdac_bus *bus, bool alt, int elid, bool enable)
{
}

static inline void
hdac_bus_eml_set_mic_privacy_mask(struct hdac_bus *bus, bool alt, int elid,
				  unsigned long mask)
{
}

static inline bool
hdac_bus_eml_is_mic_privacy_changed(struct hdac_bus *bus, bool alt, int elid)
{
	return false;
}

static inline bool
hdac_bus_eml_get_mic_privacy_state(struct hdac_bus *bus, bool alt, int elid)
{
	return false;
}

#endif /* CONFIG_SND_SOC_SOF_HDA_MLINK */
