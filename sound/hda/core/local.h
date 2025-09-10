/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Local helper macros and functions for HD-audio core drivers
 */

#ifndef __HDAC_LOCAL_H
#define __HDAC_LOCAL_H

extern const struct attribute_group *hdac_dev_attr_groups[];
int hda_widget_sysfs_init(struct hdac_device *codec);
int hda_widget_sysfs_reinit(struct hdac_device *codec, hda_nid_t start_nid,
			    int num_nodes);
void hda_widget_sysfs_exit(struct hdac_device *codec);

int snd_hdac_bus_add_device(struct hdac_bus *bus, struct hdac_device *codec);
void snd_hdac_bus_remove_device(struct hdac_bus *bus,
				struct hdac_device *codec);
void snd_hdac_bus_queue_event(struct hdac_bus *bus, u32 res, u32 res_ex);
int snd_hdac_bus_exec_verb(struct hdac_bus *bus, unsigned int addr,
			   unsigned int cmd, unsigned int *res);

int snd_hdac_exec_verb(struct hdac_device *codec, unsigned int cmd,
		       unsigned int flags, unsigned int *res);

#endif /* __HDAC_LOCAL_H */
