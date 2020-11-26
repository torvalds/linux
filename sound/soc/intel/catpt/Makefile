snd-soc-catpt-objs := device.o dsp.o loader.o ipc.o messages.o pcm.o sysfs.o

# tell define_trace.h where to find the trace header
CFLAGS_device.o := -I$(src)

obj-$(CONFIG_SND_SOC_INTEL_CATPT) += snd-soc-catpt.o
