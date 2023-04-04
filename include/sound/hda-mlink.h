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
void hda_bus_ml_put_all(struct hdac_bus *bus);
void hda_bus_ml_reset_losidv(struct hdac_bus *bus);
int hda_bus_ml_resume(struct hdac_bus *bus);
int hda_bus_ml_suspend(struct hdac_bus *bus);

#else

static inline int
hda_bus_ml_init(struct hdac_bus *bus) { return 0; }

static inline void hda_bus_ml_free(struct hdac_bus *bus) { }
static inline void hda_bus_ml_put_all(struct hdac_bus *bus) { }
static inline void hda_bus_ml_reset_losidv(struct hdac_bus *bus) { }
static inline int hda_bus_ml_resume(struct hdac_bus *bus) { return 0; }
static inline int hda_bus_ml_suspend(struct hdac_bus *bus) { return 0; }

#endif /* CONFIG_SND_SOC_SOF_HDA */
