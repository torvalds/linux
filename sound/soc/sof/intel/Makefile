# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

snd-sof-acpi-intel-byt-objs := byt.o
snd-sof-acpi-intel-bdw-objs := bdw.o

snd-sof-intel-hda-common-objs := hda.o hda-loader.o hda-stream.o hda-trace.o \
				 hda-dsp.o hda-ipc.o hda-ctrl.o hda-pcm.o \
				 hda-dai.o hda-bus.o \
				 apl.o cnl.o tgl.o icl.o mtl.o hda-common-ops.o
snd-sof-intel-hda-common-$(CONFIG_SND_SOC_SOF_HDA_PROBES) += hda-probes.o

snd-sof-intel-hda-objs := hda-codec.o

snd-sof-intel-atom-objs := atom.o

obj-$(CONFIG_SND_SOC_SOF_INTEL_ATOM_HIFI_EP) += snd-sof-intel-atom.o
obj-$(CONFIG_SND_SOC_SOF_BAYTRAIL) += snd-sof-acpi-intel-byt.o
obj-$(CONFIG_SND_SOC_SOF_BROADWELL) += snd-sof-acpi-intel-bdw.o
obj-$(CONFIG_SND_SOC_SOF_HDA_COMMON) += snd-sof-intel-hda-common.o
obj-$(CONFIG_SND_SOC_SOF_HDA) += snd-sof-intel-hda.o

snd-sof-pci-intel-tng-objs := pci-tng.o
snd-sof-pci-intel-apl-objs := pci-apl.o
snd-sof-pci-intel-cnl-objs := pci-cnl.o
snd-sof-pci-intel-icl-objs := pci-icl.o
snd-sof-pci-intel-tgl-objs := pci-tgl.o
snd-sof-pci-intel-mtl-objs := pci-mtl.o

obj-$(CONFIG_SND_SOC_SOF_MERRIFIELD) += snd-sof-pci-intel-tng.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_APL) += snd-sof-pci-intel-apl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_CNL) += snd-sof-pci-intel-cnl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_ICL) += snd-sof-pci-intel-icl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_TGL) += snd-sof-pci-intel-tgl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_MTL) += snd-sof-pci-intel-mtl.o
