# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

snd-sof-intel-byt-objs := byt.o
snd-sof-intel-bdw-objs := bdw.o

snd-sof-intel-ipc-objs := intel-ipc.o

snd-sof-intel-hda-common-objs := hda.o hda-loader.o hda-stream.o hda-trace.o \
				 hda-dsp.o hda-ipc.o hda-ctrl.o hda-pcm.o \
				 hda-dai.o hda-bus.o \
				 apl.o cnl.o
snd-sof-intel-hda-common-$(CONFIG_SND_SOC_SOF_HDA_PROBES) += hda-compress.o

snd-sof-intel-hda-objs := hda-codec.o

obj-$(CONFIG_SND_SOC_SOF_INTEL_ATOM_HIFI_EP) += snd-sof-intel-byt.o
obj-$(CONFIG_SND_SOC_SOF_BROADWELL) += snd-sof-intel-bdw.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_HIFI_EP_IPC) += snd-sof-intel-ipc.o
obj-$(CONFIG_SND_SOC_SOF_HDA_COMMON) += snd-sof-intel-hda-common.o
obj-$(CONFIG_SND_SOC_SOF_HDA) += snd-sof-intel-hda.o
