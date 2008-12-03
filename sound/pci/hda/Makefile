snd-hda-intel-objs := hda_intel.o

snd-hda-codec-y := hda_codec.o
snd-hda-codec-$(CONFIG_SND_HDA_GENERIC) += hda_generic.o
snd-hda-codec-$(CONFIG_PROC_FS) += hda_proc.o
# snd-hda-codec-$(CONFIG_SND_HDA_ELD) += hda_eld.o
snd-hda-codec-$(CONFIG_SND_HDA_HWDEP) += hda_hwdep.o
snd-hda-codec-$(CONFIG_SND_HDA_INPUT_BEEP) += hda_beep.o

snd-hda-codec-realtek-objs :=	patch_realtek.o
snd-hda-codec-cmedia-objs :=	patch_cmedia.o
snd-hda-codec-analog-objs :=	patch_analog.o
snd-hda-codec-idt-objs :=	patch_sigmatel.o
snd-hda-codec-si3054-objs :=	patch_si3054.o
snd-hda-codec-atihdmi-objs :=	patch_atihdmi.o
snd-hda-codec-conexant-objs :=	patch_conexant.o
snd-hda-codec-via-objs :=	patch_via.o
snd-hda-codec-nvhdmi-objs :=	patch_nvhdmi.o
snd-hda-codec-intelhdmi-objs :=	patch_intelhdmi.o hda_eld.o

# common driver
obj-$(CONFIG_SND_HDA_INTEL) := snd-hda-codec.o

# codec drivers (note: CONFIG_SND_HDA_CODEC_XXX are booleans)
ifdef CONFIG_SND_HDA_CODEC_REALTEK
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-realtek.o
endif
ifdef CONFIG_SND_HDA_CODEC_CMEDIA
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-cmedia.o
endif
ifdef CONFIG_SND_HDA_CODEC_ANALOG
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-analog.o
endif
ifdef CONFIG_SND_HDA_CODEC_SIGMATEL
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-idt.o
endif
ifdef CONFIG_SND_HDA_CODEC_SI3054
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-si3054.o
endif
ifdef CONFIG_SND_HDA_CODEC_ATIHDMI
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-atihdmi.o
endif
ifdef CONFIG_SND_HDA_CODEC_CONEXANT
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-conexant.o
endif
ifdef CONFIG_SND_HDA_CODEC_VIA
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-via.o
endif
ifdef CONFIG_SND_HDA_CODEC_NVHDMI
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-nvhdmi.o
endif
ifdef CONFIG_SND_HDA_CODEC_INTELHDMI
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-codec-intelhdmi.o
endif

# this must be the last entry after codec drivers;
# otherwise the codec patches won't be hooked before the PCI probe
# when built in kernel
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-intel.o
