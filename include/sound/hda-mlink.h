/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022-2023 Intel Corporation. All rights reserved.
 */

struct hdac_bus;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_MLINK)

int hda_bus_ml_init(struct hdac_bus *bus);
void hda_bus_ml_free(struct hdac_bus *bus);

int hdac_bus_eml_get_count(struct hdac_bus *bus, bool alt, int elid);

int hdac_bus_eml_power_up(struct hdac_bus *bus, bool alt, int elid, int sublink);
int hdac_bus_eml_power_up_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink);

int hdac_bus_eml_power_down(struct hdac_bus *bus, bool alt, int elid, int sublink);
int hdac_bus_eml_power_down_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink);

int hdac_bus_eml_sdw_power_up_unlocked(struct hdac_bus *bus, int sublink);
int hdac_bus_eml_sdw_power_down_unlocked(struct hdac_bus *bus, int sublink);

void hda_bus_ml_put_all(struct hdac_bus *bus);
void hda_bus_ml_reset_losidv(struct hdac_bus *bus);
int hda_bus_ml_resume(struct hdac_bus *bus);
int hda_bus_ml_suspend(struct hdac_bus *bus);

#else

static inline int
hda_bus_ml_init(struct hdac_bus *bus) { return 0; }

static inline void hda_bus_ml_free(struct hdac_bus *bus) { }

static inline int
hdac_bus_eml_get_count(struct hdac_bus *bus, bool alt, int elid) { return 0; }

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

static inline void hda_bus_ml_put_all(struct hdac_bus *bus) { }
static inline void hda_bus_ml_reset_losidv(struct hdac_bus *bus) { }
static inline int hda_bus_ml_resume(struct hdac_bus *bus) { return 0; }
static inline int hda_bus_ml_suspend(struct hdac_bus *bus) { return 0; }

#endif /* CONFIG_SND_SOC_SOF_HDA */
