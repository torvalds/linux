snd-hda-intel-objs := hda_intel.o
snd-hda-controller-objs := hda_controller.o
# for haswell power well
snd-hda-intel-$(CONFIG_SND_HDA_I915) +=	hda_i915.o

snd-hda-codec-y := hda_codec.o hda_jack.o hda_auto_parser.o hda_sysfs.o
snd-hda-codec-$(CONFIG_PROC_FS) += hda_proc.o
snd-hda-codec-$(CONFIG_SND_HDA_HWDEP) += hda_hwdep.o
snd-hda-codec-$(CONFIG_SND_HDA_INPUT_BEEP) += hda_beep.o

# for trace-points
CFLAGS_hda_codec.o := -I$(src)
CFLAGS_hda_controller.o := -I$(src)

snd-hda-codec-generic-objs :=	hda_generic.o
snd-hda-codec-realtek-objs :=	patch_realtek.o
snd-hda-codec-cmedia-objs :=	patch_cmedia.o
snd-hda-codec-analog-objs :=	patch_analog.o
snd-hda-codec-idt-objs :=	patch_sigmatel.o
snd-hda-codec-si3054-objs :=	patch_si3054.o
snd-hda-codec-cirrus-objs :=	patch_cirrus.o
snd-hda-codec-ca0110-objs :=	patch_ca0110.o
snd-hda-codec-ca0132-objs :=	patch_ca0132.o
snd-hda-codec-conexant-objs :=	patch_conexant.o
snd-hda-codec-via-objs :=	patch_via.o
snd-hda-codec-hdmi-objs :=	patch_hdmi.o hda_eld.o

# common driver
obj-$(CONFIG_SND_HDA) := snd-hda-codec.o
obj-$(CONFIG_SND_HDA) += snd-hda-controller.o

# codec drivers
obj-$(CONFIG_SND_HDA_GENERIC) += snd-hda-codec-generic.o
obj-$(CONFIG_SND_HDA_CODEC_REALTEK) += snd-hda-codec-realtek.o
obj-$(CONFIG_SND_HDA_CODEC_CMEDIA) += snd-hda-codec-cmedia.o
obj-$(CONFIG_SND_HDA_CODEC_ANALOG) += snd-hda-codec-analog.o
obj-$(CONFIG_SND_HDA_CODEC_SIGMATEL) += snd-hda-codec-idt.o
obj-$(CONFIG_SND_HDA_CODEC_SI3054) += snd-hda-codec-si3054.o
obj-$(CONFIG_SND_HDA_CODEC_CIRRUS) += snd-hda-codec-cirrus.o
obj-$(CONFIG_SND_HDA_CODEC_CA0110) += snd-hda-codec-ca0110.o
obj-$(CONFIG_SND_HDA_CODEC_CA0132) += snd-hda-codec-ca0132.o
obj-$(CONFIG_SND_HDA_CODEC_CONEXANT) += snd-hda-codec-conexant.o
obj-$(CONFIG_SND_HDA_CODEC_VIA) += snd-hda-codec-via.o
obj-$(CONFIG_SND_HDA_CODEC_HDMI) += snd-hda-codec-hdmi.o

# this must be the last entry after codec drivers;
# otherwise the codec patches won't be hooked before the PCI probe
# when built in kernel
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-intel.o
