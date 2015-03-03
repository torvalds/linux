snd-hda-core-objs := hda_bus_type.o hdac_bus.o hdac_device.o hdac_sysfs.o \
	array.o

snd-hda-core-objs += trace.o
CFLAGS_trace.o := -I$(src)

obj-$(CONFIG_SND_HDA_CORE) += snd-hda-core.o
