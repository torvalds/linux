# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

snd-sof-acpi-intel-byt-y := byt.o
snd-sof-acpi-intel-bdw-y := bdw.o

snd-sof-intel-hda-common-y := hda-loader.o hda-stream.o hda-trace.o \
				 hda-dsp.o hda-ipc.o hda-ctrl.o hda-pcm.o \
				 hda-dai.o hda-dai-ops.o hda-bus.o \
				 telemetry.o tracepoints.o

snd-sof-intel-hda-generic-y := hda.o hda-common-ops.o

snd-sof-intel-hda-mlink-y := hda-mlink.o

snd-sof-intel-hda-sdw-bpt-objs := hda-sdw-bpt.o

snd-sof-intel-hda-common-$(CONFIG_SND_SOC_SOF_HDA_PROBES) += hda-probes.o

snd-sof-intel-hda-y := hda-codec.o

snd-sof-intel-atom-y := atom.o

obj-$(CONFIG_SND_SOC_SOF_INTEL_ATOM_HIFI_EP) += snd-sof-intel-atom.o
obj-$(CONFIG_SND_SOC_SOF_BAYTRAIL) += snd-sof-acpi-intel-byt.o
obj-$(CONFIG_SND_SOC_SOF_BROADWELL) += snd-sof-acpi-intel-bdw.o
obj-$(CONFIG_SND_SOC_SOF_HDA_COMMON) += snd-sof-intel-hda-common.o
obj-$(CONFIG_SND_SOC_SOF_HDA_GENERIC) += snd-sof-intel-hda-generic.o
obj-$(CONFIG_SND_SOC_SOF_HDA_MLINK) += snd-sof-intel-hda-mlink.o
obj-$(CONFIG_SND_SOC_SOF_HDA) += snd-sof-intel-hda.o

obj-$(CONFIG_SND_SOF_SOF_HDA_SDW_BPT) += snd-sof-intel-hda-sdw-bpt.o

snd-sof-pci-intel-tng-y := pci-tng.o
snd-sof-pci-intel-skl-y := pci-skl.o skl.o hda-loader-skl.o
snd-sof-pci-intel-apl-y := pci-apl.o apl.o
snd-sof-pci-intel-cnl-y := pci-cnl.o cnl.o
snd-sof-pci-intel-icl-y := pci-icl.o icl.o
snd-sof-pci-intel-tgl-y := pci-tgl.o tgl.o
snd-sof-pci-intel-mtl-y := pci-mtl.o mtl.o
snd-sof-pci-intel-lnl-y := pci-lnl.o lnl.o
snd-sof-pci-intel-ptl-y := pci-ptl.o ptl.o

obj-$(CONFIG_SND_SOC_SOF_MERRIFIELD) += snd-sof-pci-intel-tng.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_SKL) += snd-sof-pci-intel-skl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_APL) += snd-sof-pci-intel-apl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_CNL) += snd-sof-pci-intel-cnl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_ICL) += snd-sof-pci-intel-icl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_TGL) += snd-sof-pci-intel-tgl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_MTL) += snd-sof-pci-intel-mtl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_LNL) += snd-sof-pci-intel-lnl.o
obj-$(CONFIG_SND_SOC_SOF_INTEL_PTL) += snd-sof-pci-intel-ptl.o
