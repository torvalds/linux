# SPDX-License-Identifier: GPL-2.0
snd-hda-core-objs := hda_bus_type.o hdac_bus.o hdac_device.o hdac_sysfs.o \
	hdac_regmap.o hdac_controller.o hdac_stream.o array.o hdmi_chmap.o

snd-hda-core-objs += trace.o
CFLAGS_trace.o := -I$(src)

# for sync with i915 gfx driver
snd-hda-core-$(CONFIG_SND_HDA_COMPONENT) += hdac_component.o
snd-hda-core-$(CONFIG_SND_HDA_I915) += hdac_i915.o

obj-$(CONFIG_SND_HDA_CORE) += snd-hda-core.o

#extended hda
obj-$(CONFIG_SND_HDA_EXT_CORE) += ext/

snd-intel-nhlt-objs := intel-nhlt.o
obj-$(CONFIG_SND_INTEL_NHLT) += snd-intel-nhlt.o
